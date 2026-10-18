// GJBaseGameLayer hook: tick control, fast loop, replay/serve, section solver, tracing.
#include "mod/playlayer_helpers.hpp"
#include "mod/profsample.hpp"
#include <Geode/modify/GameObject.hpp>   // cfg objwatch: ActWatchObject
#ifdef GEODE_IS_WINDOWS
#include <psapi.h>
#endif

using namespace p1;

namespace kp { bool engineStepping(); }

struct SnapExtra {
    secsolve::DashState dash, dash2;
    psnap::Touch touch;
    bool damageVerified = true;
};
static std::vector<SnapExtra> g_snapExtras;

static double g_secRestMs = 0.0;
static double g_secStoreMs = 0.0;
static double g_secResetMs = 0.0;
static double g_secSnapMs = 0.0;
static double g_secStepMs = 0.0;

static void clearCheckpointsUnsectioned(PlayLayer* pl) {
    static_assert(offsetof(CheckpointObject, m_physicalCheckpointObject) == gdoff::kCheckpointObject,
                  "the object removeCheckpoint takes out of its section");
    if (!pl || !pl->m_checkpointArray) return;
    for (unsigned i = 0; i < pl->m_checkpointArray->count(); ++i) {
        auto* cp = static_cast<CheckpointObject*>(pl->m_checkpointArray->objectAtIndex(i));
        GameObject* o = cp ? cp->m_physicalCheckpointObject : nullptr;
        if (o && o->m_outerSectionIndex >= 0) {
            pl->removeObjectFromSection(o);
            o->m_outerSectionIndex = -1;   // out: a later removal of it is GD's own no-op
        }
        if (o) {
            auto*& attached = *reinterpret_cast<cocos2d::CCNode**>(
                reinterpret_cast<char*>(o) + gdoff::kObjGlowSprite);
            if (attached) {
                attached->removeFromParentAndCleanup(true);
                attached->release();
                attached = nullptr;
            }
            o->removeFromParentAndCleanup(true);
        }
    }
    pl->m_checkpointArray->removeAllObjects();
}

static long long privateMB() {
#ifndef GEODE_IS_WINDOWS
    return -1;
#else
    using Fn = int(__stdcall*)(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "K32GetProcessMemoryInfo");
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (!fn || !fn(GetCurrentProcess(), &pmc, sizeof(pmc))) return -1;
    return (long long)(pmc.PagefileUsage / (1024 * 1024));
#endif
}

static void procMemMB(size_t& cur, size_t& peak) {
#ifndef GEODE_IS_WINDOWS
    cur = peak = 0;   // Windows only; 0 = not measured
#else
    using Fn = int(__stdcall*)(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD);
    static Fn fn = (Fn)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                      "K32GetProcessMemoryInfo");
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    cur = peak = 0;
    if (fn && fn(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        cur = pmc.WorkingSetSize / (1024 * 1024);
        peak = pmc.PeakWorkingSetSize / (1024 * 1024);
    }
#endif
}

static size_t layerWsMB() {
    size_t cur = 0, peak = 0;
    procMemMB(cur, peak);
    return cur;
}

static int runningActionsOn(cocos2d::CCNode* t) {
    if (!t) return -1;
    auto* d = cocos2d::CCDirector::sharedDirector();
    if (!d || !d->getActionManager()) return -1;
    return d->getActionManager()->numberOfRunningActionsInTarget(t);
}

static void sectionCensus(GJBaseGameLayer* layer, size_t& live, size_t& phys) {
    live = phys = 0;
    if (!layer) return;
    const uint8_t* b = (const uint8_t*)layer;
    // std::vector<T> element count, for an 8-byte or 4-byte T.
    auto vecN = [](const uint8_t* v, size_t elem) -> size_t {
        if (!v) return 0;
        const uint8_t* beg = *(const uint8_t* const*)v;
        const uint8_t* end = *(const uint8_t* const*)(v + 8);
        if (!beg || !end || end < beg) return 0;
        return (size_t)(end - beg) / elem;
    };
    const size_t fam[2][2] = {
        {gdoff::kLayerNonEffectObjects, gdoff::kLayerNonEffectObjectsSizes},
        {gdoff::kLayerCollisionBlockSections, gdoff::kLayerCollisionBlockSectionSizes}};
    for (const auto& f : fam) {
        const uint8_t* secV = b + f[0];
        const uint8_t* cntV = b + f[1];
        const size_t nSec = std::min(vecN(secV, 8), vecN(cntV, 8));
        const uint8_t* const* secB = *(const uint8_t* const* const*)secV;
        const uint8_t* const* cntB = *(const uint8_t* const* const*)cntV;
        if (!secB || !cntB) continue;
        for (size_t i = 0; i < nSec; ++i) {
            const uint8_t* inner = secB[i];
            const uint8_t* ints = cntB[i];
            if (!inner || !ints) continue;
            const size_t nSub = std::min(vecN(inner, 8), vecN(ints, 4));
            const uint8_t* const* leafB = *(const uint8_t* const* const*)inner;
            const int* cv = *(const int* const*)ints;
            if (!leafB || !cv) continue;
            for (size_t j = 0; j < nSub; ++j) {
                if (!leafB[j]) continue;
                if (cv[j] > 0) live += (size_t)cv[j];
                phys += vecN(leafB[j], 8);
            }
        }
    }
}

struct SecRestTimer {
    std::chrono::steady_clock::time_point t0{std::chrono::steady_clock::now()};
    ~SecRestTimer() {
        g_secRestMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    }
};

static bool spiderRectWindow() {
    if (g_cfg.hbTo != 0) return g_tick >= g_cfg.hbFrom && g_tick <= g_cfg.hbTo;
    return g_tick >= 1750 && g_tick <= 1900;
}

static void printQueryHits(const char* tag, cocos2d::CCArray* arr) {
    std::string s = std::string(tag) + ": t=" + std::to_string(g_tick)
                    + " n=" + std::to_string(arr ? arr->count() : 0);
    int k = 0;
    if (arr) {
        for (auto* o : CCArrayExt<GameObject*>(arr)) {
            if (!o) continue;
            if (k++ >= 24) { s += " ..."; break; }
            s += " " + std::to_string(o->m_uniqueID);
        }
    }
    writeResult(s);
}

namespace {
geode::Hook* g_updSecHook = nullptr;
geode::Hook* g_remSecHook = nullptr;
bool g_updSecOn = true, g_remSecOn = true;
long long g_secHookFlips = 0;
}
inline void setSectionHook(geode::Hook* h, bool& on, bool want, const char* name) {
    if (!h || on == want) return;
    if (auto r = want ? h->enable() : h->disable(); r.isErr()) {
        writeResult(std::string("sectionhooks: ") + name + (want ? " enable" : " disable")
                    + " FAILED: " + r.unwrapErr());
        return;
    }
    on = want;
    if (++g_secHookFlips <= 4)
        writeResult(std::string("sectionhooks: ") + name + (want ? " enabled" : " disabled")
                    + " (t=" + std::to_string((long long)g_tick) + ")");
}
inline void syncSectionHooks() {
    const bool lazy = itermap::g_sectionHookLazy;
    setSectionHook(g_updSecHook, g_updSecOn, !lazy || secsolve::g_active, "updateObjectSection");
    setSectionHook(g_remSecHook, g_remSecOn,
                   !lazy || (g_started && (g_cfg.dpSolve || g_serveMode || secsolve::g_on)),
                   "removeObjectFromSection");
}

namespace {
geode::Hook* g_actHook = nullptr;
bool g_actHookOn = false;
}
class $modify(ActWatchObject, GameObject) {
    static void onModify(auto& self) {
        if (auto h = self.getHook("GameObject::activateObject")) {
            g_actHook = h.unwrap();
            g_actHook->setAutoEnable(false);
        }
    }
    void activateObject() {
        if (!itermap::g_objWatch.empty()
            && std::find(itermap::g_objWatch.begin(), itermap::g_objWatch.end(), m_uniqueID)
                   != itermap::g_objWatch.end()) {
            auto* pl = PlayLayer::get();
            char b[220];
            snprintf(b, sizeof(b), "objwatch: activateObject uid=%d t=%lld player.x=%.2f restoring=%d run=%s%lld",
                     m_uniqueID, (long long)g_tick, pl && pl->m_player1 ? pl->m_player1->getPositionX() : -1.f,
                     (int)seekcp::g_restoring, seekcp::g_from < 0 ? "head" : "cp@",
                     seekcp::g_from < 0 ? 0LL : seekcp::g_from);
            writeResult(b);
#ifdef GEODE_IS_WINDOWS
            postmortem::writeBacktrace("objwatch-bt:");
#endif
        }
        GameObject::activateObject();
    }
};
inline void syncActWatchHook() {
    const bool want = !itermap::g_objWatch.empty();
    if (!g_actHook || want == g_actHookOn) return;
    if (auto r = want ? g_actHook->enable() : g_actHook->disable(); r.isOk()) g_actHookOn = want;
}

// ---- Tracing: frame / step structure ----
class $modify(GJBaseGameLayer) {
    static void onModify(auto& self) {
        if (auto h = self.getHook("GJBaseGameLayer::updateObjectSection")) g_updSecHook = h.unwrap();
        if (auto h = self.getHook("GJBaseGameLayer::removeObjectFromSection")) g_remSecHook = h.unwrap();
        secsolve::g_onActive = [] { syncSectionHooks(); };
        itermap::g_cpAhead = [](long long now, long long target) { return seekcp::aheadOf(now, target); };
        itermap::g_cpLine = [] { return seekcp::summary(); };
    }
    cocos2d::CCArray* staticObjectsInRect(cocos2d::CCRect rect, bool enabledGroups) {
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver && spiderRectWindow()) {
            {
                char b[192];
                snprintf(b, sizeof(b),
                         "srect: t=%lld o=(%.3f,%.3f) s=(%.3f,%.3f) eg=%d",
                         (long long)g_tick, rect.origin.x, rect.origin.y,
                         rect.size.width, rect.size.height, (int)enabledGroups);
                writeResult(b);
            }
            auto* r = GJBaseGameLayer::staticObjectsInRect(rect, enabledGroups);
            printQueryHits("shit", r);
            return r;
        }
        return GJBaseGameLayer::staticObjectsInRect(rect, enabledGroups);
    }
    cocos2d::CCArray* damagingObjectsInRect(cocos2d::CCRect rect, bool enabledGroups) {
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver && spiderRectWindow()) {
            {
                char b[192];
                snprintf(b, sizeof(b),
                         "drect: t=%lld o=(%.3f,%.3f) s=(%.3f,%.3f) eg=%d",
                         (long long)g_tick, rect.origin.x, rect.origin.y,
                         rect.size.width, rect.size.height, (int)enabledGroups);
                writeResult(b);
            }
            auto* r = GJBaseGameLayer::damagingObjectsInRect(rect, enabledGroups);
            printQueryHits("dhit", r);
            return r;
        }
        return GJBaseGameLayer::damagingObjectsInRect(rect, enabledGroups);
    }
    void pickupItem(EffectGameObject* object) {
        if (object && g_started && !g_sessionOver && g_cfg.coinMode) {
            const int uid = object->m_uniqueID;
            size_t i = 0;
            for (; i < solver::g_coins.size(); ++i)
                if (solver::g_coins[i].uid == uid) break;
            const bool known = i < solver::g_coins.size();
            if (known) {
                if (i < solver::g_coinGdTick.size() && solver::g_coinGdTick[i] < 0)
                    solver::g_coinGdTick[i] = g_tick;
            } else {
                ++solver::g_coinGdUnmatched;
            }
            if (++solver::g_coinLogLines <= 400) {
                auto p = object->getPosition();
                float px = 0.f, py = 0.f;
                if (m_player1) { px = m_player1->getPositionX(); py = m_player1->getPositionY(); }
                char b[224];
                snprintf(b, sizeof(b),
                         "coingd: t=%lld uid=%d id=%d obj=(%.2f,%.2f) player=(%.2f,%.2f) "
                         "coin=%d secretid=%d item=%d",
                         (long long)g_tick, uid, object->m_objectID, p.x, p.y, px, py,
                         known ? (int)i : -1, object->m_secretCoinID, object->m_itemID);
                writeResult(b);
            }
        }
        GJBaseGameLayer::pickupItem(object);
    }
    bool hasUniqueCoin(EffectGameObject* object) {
        const bool r = GJBaseGameLayer::hasUniqueCoin(object);
        bool ours = false;
        if (object && r && g_started && !g_sessionOver && g_cfg.coinMode && botDriving())
            for (const auto& c : solver::g_coins)
                if (c.uid == object->m_uniqueID) { ours = true; break; }
        if (object && g_started && !g_sessionOver && g_cfg.coinMode
            && ++solver::g_coinLogLines <= 400) {
            char ub[192];
            snprintf(ub, sizeof(ub), "coinunique: t=%lld uid=%d id=%d -> %d%s",
                     (long long)g_tick, object->m_uniqueID, object->m_objectID,
                     r ? 1 : 0, ours ? " (in the save; asked as 0 for this session)" : "");
            writeResult(ub);
        }
        return ours ? false : r;
    }
    void updateCounters(int itemId, int value) {
        if (g_started && !g_sessionOver && (g_cfg.coinRoute || g_cfg.worldGates))
            solver::g_itemCounts[itemId] = value;
        if (g_started && !g_sessionOver && (g_cfg.coinMode || g_cfg.worldGates)
            && ++solver::g_coinLogLines <= 400) {
            char b[128];
            snprintf(b, sizeof(b), "itemcnt: t=%lld item=%d v=%d",
                     (long long)g_tick, itemId, value);
            writeResult(b);
        }
        GJBaseGameLayer::updateCounters(itemId, value);
    }
    void rotateGameplay(RotateGameplayGameObject* object) {
        // Absolute angle. Same convention as the model's nf (lround(rot/90) & 3).
        if (object)
            g_gameFrame = ((int)std::lround(object->getRotation() / 90.0)) & 3;
        ++g_rotGameplayCalls;
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver) {
            static int lines = 0;
            if (++lines <= 200) {
                auto p = object ? object->getPosition() : cocos2d::CCPoint(0, 0);
                char b[256];
                snprintf(b, sizeof(b),
                         "rot: t=%lld uid=%d id=%d obj=(%.1f,%.1f) orot=%.1f "
                         "px=%.3f py=%.3f isRev=%d endRev=%d mvDir=%d gndDir=%d",
                         (long long)g_tick,
                         object ? object->m_uniqueID : -1,
                         object ? object->m_objectID : -1,
                         p.x, p.y, object ? object->getRotation() : 0.f,
                         m_player1 ? m_player1->getPositionX() : 0.f,
                         m_player1 ? m_player1->getPositionY() : 0.f,
                         object ? (int)object->m_isReverse : -1,
                         object ? (int)object->m_endReversed : -1,
                         object ? (int)object->m_moveDirection : -1,
                         object ? (int)object->m_groundDirection : -1);
                writeResult(b);
            }
        }
        GJBaseGameLayer::rotateGameplay(object);
    }

    void processOptionsTrigger(GameOptionsTrigger* object) {
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver) {
            static int lines = 0;
            if (++lines <= 200) {
                auto p = object ? object->getPosition() : cocos2d::CCPoint(0, 0);
                char b[256];
                snprintf(b, sizeof(b),
                         "opt: t=%lld uid=%d id=%d obj=(%.1f,%.1f) "
                         "px=%.3f py=%.3f p1=%d p2=%d touch=%d spawn=%d "
                         "multi=%d was=%d",
                         (long long)g_tick,
                         object ? object->m_uniqueID : -1,
                         object ? object->m_objectID : -1,
                         p.x, p.y,
                         m_player1 ? m_player1->getPositionX() : 0.f,
                         m_player1 ? m_player1->getPositionY() : 0.f,
                         object ? (int)object->m_disableP1Controls : -9,
                         object ? (int)object->m_disableP2Controls : -9,
                         object ? (int)object->m_isTouchTriggered : -1,
                         object ? (int)object->m_isSpawnTriggered : -1,
                         object ? (int)object->m_isMultiTriggered : -1,
                         m_player1 ? (int)m_player1->m_controlsDisabled : -1);
                writeResult(b);
            }
        }
        GJBaseGameLayer::processOptionsTrigger(object);
    }

    void toggleGroupTriggered(int group, bool activate,
                              gd::vector<int> const& remapKeys,
                              int triggerID, int controlID) {
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver) {
            static int lines = 0;
            if (++lines <= 2000) {
                char b[192];
                snprintf(b, sizeof(b),
                         "togl: t=%lld g=%d on=%d trig=%d ctl=%d px=%.3f",
                         (long long)g_tick, group, activate ? 1 : 0,
                         triggerID, controlID,
                         m_player1 ? m_player1->getPositionX() : 0.f);
                writeResult(b);
            }
        }
        GJBaseGameLayer::toggleGroupTriggered(group, activate, remapKeys,
                                              triggerID, controlID);
    }

    void toggleGroup(int id, bool activate) {
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver) {
            static int lines = 0;
            if (++lines <= 2000) {
                char b[128];
                snprintf(b, sizeof(b), "toglD: t=%lld g=%d on=%d px=%.3f",
                         (long long)g_tick, id, activate ? 1 : 0,
                         m_player1 ? m_player1->getPositionX() : 0.f);
                writeResult(b);
            }
        }
        GJBaseGameLayer::toggleGroup(id, activate);
    }

    void collisionCheckObjects(PlayerObject* player,
                               gd::vector<GameObject*>* objects,
                               int objectCount, float dt) {
        if (g_cfg.watchUid > 0 && g_started && !g_sessionOver && objects
            && player == m_player1
            && g_tick >= g_cfg.hbFrom
            && (g_cfg.hbTo == 0 || g_tick <= g_cfg.hbTo)) {
            static int lines = 0;
            if (++lines <= 2000) {
                int has = 0;
                GameObject* found = nullptr;
                const int n = (int)objects->size();
                const int lim = objectCount < n ? objectCount : n;
                for (int i = 0; i < lim; ++i) {
                    GameObject* o = (*objects)[i];
                    if (o && o->m_uniqueID == g_cfg.watchUid) {
                        has = 1; found = o; break;
                    }
                }
                const auto prct = player->getObjectRect();
                char ob[192] = "";
                if (found) {
                    const auto orct = found->getObjectRect();
                    snprintf(ob, sizeof(ob),
                             " orect=(%.3f,%.3f,%.3f,%.3f) oid=%d otype=%d"
                             " act=%d actP=%d grpOff=%d grpOffT=%d claim=%d",
                             orct.origin.x, orct.origin.y,
                             orct.size.width, orct.size.height,
                             found->m_objectID, (int)found->getType(),
                             found->hasBeenActivated() ? 1 : 0,
                             found->hasBeenActivatedByPlayer(player) ? 1 : 0,
                             (int)found->m_isGroupDisabled,
                             (int)found->m_isGroupDisabledTemp,
                             (int)*((unsigned char*)found + gdoff::kRingClaimTouch));
                }
                char b[448];
                snprintf(b, sizeof(b),
                         "ccl: t=%lld n=%d has=%d "
                         "prect=(%.3f,%.3f,%.3f,%.3f)%s",
                         (long long)g_tick, lim, has,
                         prct.origin.x, prct.origin.y,
                         prct.size.width, prct.size.height, ob);
                writeResult(b);
            }
        }
        GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
    }

    void flipGravity(PlayerObject* p, bool flip, bool noEffects) {
        const bool watch = g_cfg.hitboxTrace && g_started && !g_sessionOver
                           && p && p == m_player1;
        const int before = watch ? (int)p->m_isUpsideDown : 0;
        GJBaseGameLayer::flipGravity(p, flip, noEffects);
        if (!watch) return;
        static int lines = 0;
        if (++lines > 2000) return;
        char b[192];
        snprintf(b, sizeof(b),
                 "fg: t=%lld flip=%d noFx=%d up %d->%d x=%.3f y=%.3f vy=%.3f",
                 (long long)g_tick, flip ? 1 : 0, noEffects ? 1 : 0,
                 before, (int)p->m_isUpsideDown,
                 p->getPositionX(), p->getPositionY(), p->m_yVelocity);
        writeResult(b);
    }

    void update(float dt) {
        // an update of the Kairos engine is its search's (kp::engineStepping, top).
        if (kp::engineStepping()) {
            GJBaseGameLayer::update(dt);
            return;
        }
        if (hookdepth::g_slots[hookdepth::BGL_UPDATE].cur > 0) {
            ++g_reentrantUpdates;
            GJBaseGameLayer::update(dt);
            return;
        }
        hookdepth::Guard hg(hookdepth::BGL_UPDATE);
        stallwatch::Mark sm(stallwatch::UPDATE);
        if (g_preludeResetPending && g_preludePendingAttempt != g_attempt) g_preludeResetPending = false;
        if (g_preludeResetPending && g_started && !g_sessionOver) {
            g_preludeResetPending = false;
            ++g_preludeResets;
            ++g_finishedAttempts;   // the prelude attempt ended by its `until`
            if (auto* pl = PlayLayer::get()) pl->resetLevel();
        }
        if (secsolve::inFlight() && secsolve::g_sliceEnded)
            secsolve::g_outsideMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - secsolve::g_sliceEnd).count();
        ++g_frame;
        if (g_restoreProbe > 0) {
            --g_restoreProbe;
            const long long before = g_pcCalls;
            const int timerBefore = m_resumeTimer;
            GJBaseGameLayer::update(dt);
            char pb[192];
            snprintf(pb, sizeof(pb),
                     "restoreprobe: resumeTimer %d->%d  pcCalls=+%lld  "
                     "tick=%lld dt=%.6f",
                     timerBefore, m_resumeTimer, g_pcCalls - before,
                     (long long)g_tick, dt);
            writeResult(pb);
            return;
        }
        if (g_started && g_cfg.framedt > 0) dt = g_cfg.framedt;
        if (g_slowmo > 1.f) dt /= g_slowmo;
        if (g_watchSpeed < 1.f && !(g_cfg.fastdt > 0 && !g_realtimeOverride))
            dt *= g_watchSpeed;
        if (g_started) {
            if (!g_sessionOver) pollCommandFile();
            pollHotkeys();
        }
        if (!g_swapReq.empty()) {
            const std::string path = g_swapReq;
            g_swapReq.clear();
            geode::Loader::get()->queueInMainThread([path] { swapLevelNow(path); });
        }
        {
            static int s_titleTick = 0;
            if ((++s_titleTick & 63) == 0) updateWindowTitle();
        }
        if (g_lagMs > 0 && g_started && !g_sessionOver) {
            if (g_lagAtTick < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(g_lagMs));
            } else if (!g_lagFired && g_tick >= g_lagAtTick) {
                g_lagFired = true;
                writeResult("lag: stalling " + std::to_string(g_lagMs)
                    + "ms at tick=" + std::to_string((long long)g_tick));
                std::this_thread::sleep_for(std::chrono::milliseconds(g_lagMs));
            }
        }
        if (g_syncProbe && g_started && !g_sessionOver) {
            static std::chrono::steady_clock::time_point s_prevFrame{};
            auto nowFrame = std::chrono::steady_clock::now();
            if (s_prevFrame.time_since_epoch().count() != 0) {
                double frameMs = std::chrono::duration<double, std::milli>(
                    nowFrame - s_prevFrame).count();
                if (frameMs > 100.0) {
                    double musicSec = 0.0;
                    if (auto* fe = FMODAudioEngine::sharedEngine())
                        musicSec = fe->getMusicTimeMS(0) / 1000.0;
                    char hb[192];
                    snprintf(hb, sizeof(hb),
                        "hitch: frame=%.0fms tick=%lld game=%.2f music=%.2f d=%.2f",
                        frameMs, (long long)g_tick, (double)g_tick / 240.0, musicSec,
                        musicSec - (double)g_tick / 240.0);
                    writeResult(hb);
                }
            }
            s_prevFrame = nowFrame;
        }
        if (g_syncProbe && g_started && !g_sessionOver) {
            static std::chrono::steady_clock::time_point s_lastSync{};
            auto nowSync = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(nowSync - s_lastSync).count() >= 0.5) {
                s_lastSync = nowSync;
                auto* pl = PlayLayer::get();
                double musicSec = 0.0;
                if (auto* fe = FMODAudioEngine::sharedEngine())
                    musicSec = fe->getMusicTimeMS(0) / 1000.0;
                double gameSec = (double)g_tick / 240.0;
                double wantSec = -1.0;
                if (m_player1) wantSec = (double)this->timeForPos(
                    m_player1->getPosition(), 0, 0, true, 0);
                char sb[352];
                snprintf(sb, sizeof(sb),
                    "sync: real=%.2f game=%.2f attempt=%.2f music=%.2f "
                    "d(music-game)=%.2f want=%.2f d(game-want)=%.2f "
                    "spd=%.2f tick=%lld x=%.0f locked=%d endAnim=%d",
                    std::chrono::duration<double>(nowSync - solver::g_solveStart).count(),
                    gameSec, pl ? pl->m_attemptTime : -1.0, musicSec, musicSec - gameSec,
                    wantSec, gameSec - wantSec,
                    m_player1 ? (float)m_player1->m_playerSpeed : -1.f,
                    (long long)g_tick,
                    m_player1 ? m_player1->getPositionX() : -1.f,
                    m_player1 ? (int)m_player1->m_isLocked : -1,
                    pl ? (int)pl->m_levelEndAnimationStarted : -1);
                writeResult(sb);
            }
        }
        if (g_started && !g_sessionOver) {
            auto* pl = PlayLayer::get();
            if (pl) {
                if (g_cfg.practiceAt >= 0 && !g_practiceOn && g_tick >= g_cfg.practiceAt) {
                    g_practiceOn = true;
                    pl->togglePracticeMode(true);
                    ev("PRACTICE_on");
                    writeResult("practice_on: tick=" + std::to_string(g_tick));
                }
                if (g_nextSnap < g_cfg.snapAt.size()
                    && g_tick >= g_cfg.snapAt[g_nextSnap]) {
                    const int asked = g_cfg.snapAt[g_nextSnap];
                    while (g_nextSnap < g_cfg.snapAt.size()
                           && g_cfg.snapAt[g_nextSnap] <= g_tick) ++g_nextSnap;
                    // Taken before the checkpoint, as the section search takes its head's.
                    std::vector<uint8_t> snapPlayer;
                    SnapExtra extra;
                    if (g_cfg.snapPlayer && m_player1) {
                        psnap::captureRaw(m_player1, this, snapPlayer);
                        secCaptureDash(extra.dash);
                        if (m_player2) secCaptureDash(extra.dash2, m_player2);
                        psnap::captureTouch(m_player1, extra.touch, this);
                        extra.damageVerified = pl->m_damageVerified;
                    }
                    if (auto* cp = pl->markCheckpoint()) {
                        cp->retain();
                        int snapHeld = 0;
                        if (m_player1) {
                            auto hb = m_player1->m_holdingButtons.find(1);
                            snapHeld = (hb != m_player1->m_holdingButtons.end()
                                        && hb->second) ? 1 : 0;
                        }
                        g_snaps.push_back({(long long)g_tick, cp,
                                           g_nextInput, g_nextToggle,
                                           snapHeld, {}, std::move(snapPlayer)});
                        g_snapExtras.push_back(std::move(extra));
                        char sb[192];
                        snprintf(sb, sizeof(sb),
                                 "snap: asked=%d at=%lld x=%.3f y=%.3f "
                                 "in=%zu tog=%zu",
                                 asked, (long long)g_tick,
                                 m_player1 ? m_player1->getPositionX() : -1.0,
                                 m_player1 ? m_player1->getPositionY() : -1.0,
                                 g_nextInput, g_nextToggle);
                        writeResult(sb);
                    } else {
                        writeResult("snap: asked=" + std::to_string(asked)
                                    + " FAILED (markCheckpoint returned 0)");
                    }
                }
                if (g_cfg.checkpointAt >= 0 && !g_ckpt && g_tick >= g_cfg.checkpointAt) {
                    if (g_cfg.vyTestOn && m_player1)
                        m_player1->m_yVelocity = g_cfg.vyTest;
                    if (m_player1) psnap::captureRaw(m_player1, this, secsolve::g_headPlain);
                    // ...and each body's m_jumpBuffered, which a held restore raises (secjbkeep)
                    secsolve::g_headJb[0] = m_player1 ? (int8_t)(m_player1->m_jumpBuffered ? 1 : 0)
                                                      : (int8_t)-1;
                    secsolve::g_headJb[1] = (m_player2 && m_player2 != m_player1)
                                                ? (int8_t)(m_player2->m_jumpBuffered ? 1 : 0)
                                                : (int8_t)-1;
                    g_ckpt = markCheckpointAtPhys(pl);
                    if (g_ckpt) {
                        g_ckpt->retain();
                        g_ckptTick = g_tick;
                        g_headHeld = 0;
                        if (m_player1) {
                            auto it = m_player1->m_holdingButtons.find(1);
                            g_headHeld = (it != m_player1->m_holdingButtons.end()
                                          && it->second) ? 1 : 0;
                        }
                        ev("CHECKPOINT_made", (double)g_ckptTick);
                        if (g_cfg.oobTest && m_player1)
                            reinterpret_cast<char*>(m_player1)[kOobLatchOff] = 1;
                        secCaptureDash(secsolve::g_ckptDash);
                        // ...and player 2's, for a section search's head (cfg secheaddash)
                        if (m_player2 && m_player2 != m_player1)
                            secCaptureDash(secsolve::g_ckptDash2, m_player2);
                        else
                            secsolve::g_ckptDash2 = secsolve::DashState{};
                        g_ckptNextInput = g_nextInput;
                        g_ckptNextToggle = g_nextToggle;
                        g_ckptVy = m_player1 ? m_player1->m_yVelocity : 0.0;
                        g_ckptVyRel = m_player1 ? m_player1->m_yVelocityRelated : 0.0;
                        g_ckptOobLatch = m_player1
                            ? (unsigned char)reinterpret_cast<const char*>(m_player1)[kOobLatchOff]
                            : 0;
                        char cb[192];
                        snprintf(cb, sizeof(cb),
                                 "checkpoint: tick=%lld x=%.3f y=%.3f oobLatch=%d",
                                 (long long)g_ckptTick,
                                 m_player1 ? m_player1->getPositionX() : -1.0,
                                 m_player1 ? m_player1->getPositionY() : -1.0,
                                 (int)g_ckptOobLatch);
                        writeResult(cb);
                    } else {
                        const bool dead = m_player1 && m_player1->m_isDead;
                        writeResult("checkpoint FAILED at tick=" + std::to_string(g_tick)
                                    + " (player dead=" + std::to_string(dead ? 1 : 0) + ")");
                        dpsolve::secRungPrefixFailed(
                            g_tick, m_player1 ? m_player1->getPositionX() : 0.f,
                            "could not take the checkpoint");
                    }
                }
                if (g_cfg.restoreLoop > 0 && g_ckpt && !g_restoreLoopDone) {
                    g_restoreLoopDone = true;
                    const auto rt0 = std::chrono::steady_clock::now();
                    static std::vector<CheckpointObject*> s_held;
                    if (g_cfg.restoreLoopHold > 0) {
                        const auto ht0 = std::chrono::steady_clock::now();
                        for (int i = 0; i < g_cfg.restoreLoopHold; ++i)
                            if (auto* hc = pl->markCheckpoint()) {
                                hc->retain();
                                s_held.push_back(hc);
                            }
                        char hb2[160];
                        snprintf(hb2, sizeof(hb2),
                                 "restoreloop_hold: live=%zu madeMs=%.0f",
                                 s_held.size(),
                                 std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - ht0).count());
                        writeResult(hb2);
                    }
                    static std::vector<char*> s_heap;
                    if (g_cfg.restoreLoopHeapMB > 0 && s_heap.empty()) {
                        const size_t blk = 4u << 20;
                        const auto gt0 = std::chrono::steady_clock::now();
                        for (int mb = 0; mb < g_cfg.restoreLoopHeapMB; mb += 4) {
                            char* p = (char*)malloc(blk);
                            if (!p) break;
                            for (size_t o = 0; o < blk; o += 4096)
                                p[o] = (char)(mb + 1);
                            s_heap.push_back(p);
                        }
                        size_t hc = 0, hp = 0;
                        procMemMB(hc, hp);
                        char gb[160];
                        snprintf(gb, sizeof(gb),
                                 "restoreloop_heap: blocks=%zu MB=%zu wsMB=%zu "
                                 "madeMs=%.0f",
                                 s_heap.size(), s_heap.size() * 4, hc,
                                 std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - gt0).count());
                        writeResult(gb);
                    }
                    const int bucket = 50;
                    int bn = 0;
                    auto bt0 = std::chrono::steady_clock::now();
                    for (int i = 0; i < g_cfg.restoreLoop; ++i) {
                        if (g_cfg.restoreLoopDrainAt > 0
                            && i == g_cfg.restoreLoopDrainAt) {
                            size_t bc = 0, bp = 0;
                            procMemMB(bc, bp);
                            cocos2d::CCPoolManager::sharedPoolManager()->pop();
                            size_t ac = 0, ap = 0;
                            procMemMB(ac, ap);
                            char db[160];
                            snprintf(db, sizeof(db),
                                     "restoreloop_drain: at=%d wsMB %zu -> %zu",
                                     i, bc, ac);
                            writeResult(db);
                            bn = 0;
                            bt0 = std::chrono::steady_clock::now();
                        }
                        if (!g_cfg.restoreLoopKeep) clearCheckpointsUnsectioned(pl);
                        CheckpointObject* src = g_ckpt;
                        if (g_cfg.restoreLoopCycle && !s_held.empty())
                            src = s_held[(size_t)i % s_held.size()];
                        pl->storeCheckpoint(src);
                        if (g_cfg.restoreLoopClearQ) pl->m_queuedButtons.clear();
                        g_restorePending = true;
                        pl->resetLevel();
                        g_restorePending = false;
                        if (g_cfg.restoreLoopDrain)
                            cocos2d::CCPoolManager::sharedPoolManager()->pop();
                        if (++bn == bucket) {
                            bn = 0;
                            const auto bt1 = std::chrono::steady_clock::now();
                            size_t wsCur = 0, wsPeak = 0;
                            procMemMB(wsCur, wsPeak);
                            size_t secLive = 0, secPhys = 0;
                            sectionCensus(pl, secLive, secPhys);
                            char sb[224];
                            snprintf(sb, sizeof(sb),
                                     "restoreloop_series: i=%d perMs=%.4f arr=%d "
                                     "wsMB=%zu peakMB=%zu actN=%d "
                                     "secLive=%zu secPhys=%zu",
                                     i + 1,
                                     std::chrono::duration<double, std::milli>(
                                         bt1 - bt0).count() / bucket,
                                     pl->m_checkpointArray
                                         ? (int)pl->m_checkpointArray->count() : -1,
                                     wsCur, wsPeak, runningActionsOn(m_player1),
                                     secLive, secPhys);
                            writeResult(sb);
                            bt0 = bt1;
                        }
                    }
                    const double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - rt0).count();
                    char rb[192];
                    snprintf(rb, sizeof(rb),
                             "restoreloop: n=%d totalMs=%.1f perMs=%.4f "
                             "perSec=%.0f ckptTick=%lld",
                             g_cfg.restoreLoop, ms, ms / g_cfg.restoreLoop,
                             1000.0 * g_cfg.restoreLoop / (ms > 0 ? ms : 1e-9),
                             (long long)g_ckptTick);
                    writeResult(rb);
                }
                if (secsolve::g_on && !g_ckpt && !secsolve::g_warnedNoCkpt
                    && g_cfg.checkpointAt < 0) {
                    secsolve::g_warnedNoCkpt = true;
                    writeResult("secsolve: NOT RUNNING -- secsolve=1 needs a "
                                "checkpoint (set practiceat= and checkpointat=)"
                                "; secstart alone does nothing");
                }
                if (secsolve::g_on && g_ckpt && !secsolve::g_done
                    && g_tick >= secsolve::g_startTick) {
                    secsolve::g_done = true;
                    if (secsolve::g_worldDiff > 0) runWorldDiff(pl);
                    else if (secsolve::g_snapCmp > 0) runSnapCompare(pl);
                    else if (secsolve::g_verify) runSectionVerify(pl);
                    else {
                        secsolve::g_task = runSectionSolve(pl);
                        secsolve::g_taskLayer = pl;
                    }
                }
                if (g_cfg.snapVerify > 0 && !g_snapVerified && !g_snaps.empty()
                    && g_nextSnap >= g_cfg.snapAt.size()
                    && g_tick > g_snaps.back().tick + g_cfg.snapVerify) {
                    g_snapVerified = true;
                    int okCount = 0;
                    for (auto& s : g_snaps) {
                        std::vector<SnapState> got;
                        clearCheckpointsUnsectioned(pl);
                        pl->storeCheckpoint(s.cp);
                        g_restorePending = true;
                        pl->resetLevel();
                        g_restorePending = false;
                        pl->m_queuedButtons.clear();
                        const bool putPlayer = g_cfg.snapPlayer && m_player1 && !s.player.empty();
                        if (s.held && putPlayer) {
                            m_player1->m_holdingButtons[1] = true;
                            if (m_player2) m_player2->m_holdingButtons[1] = true;
                        } else if (s.held) {
                            g_injecting = true;
                            this->handleButton(true, 1, true);
                            g_injecting = false;
                        }
                        if (putPlayer) psnap::restore(m_player1, this, s.player.data());
                        const size_t si = (size_t)(&s - g_snaps.data());
                        if (putPlayer && si < g_snapExtras.size()) {
                            const SnapExtra& e = g_snapExtras[si];
                            secRestoreDash(e.dash);
                            if (m_player2) secRestoreDash(e.dash2, m_player2);
                            psnap::restoreTouch(m_player1, e.touch, this);
                            pl->m_damageVerified = e.damageVerified;
                        }
                        g_nextInput = s.nextInput;
                        g_nextToggle = s.nextToggle;
                        g_tick = s.tick;
                        g_snapProbe = &got;
                        const long long target = s.tick + g_cfg.snapVerify;
                        for (int guard = 0; g_tick < target && guard < 4096; ++guard)
                            GJBaseGameLayer::update((float)(1.0 / 60.0));
                        g_snapProbe = nullptr;
                        std::unordered_map<long long, const SnapState*> byT;
                        for (const auto& g : got) byT[g.t] = &g;
                        double worst = 0.0;
                        long long firstBad = -1;
                        size_t n = 0;
                        for (const auto& h : s.head) {
                            auto it = byT.find(h.t);
                            if (it == byT.end()) continue;
                            ++n;
                            const double d = std::max(
                                std::fabs(h.y - it->second->y),
                                std::fabs(h.vy - it->second->vy));
                            if (d > worst) worst = d;
                            if (d > 1e-9 && firstBad < 0) firstBad = h.t;
                        }
                        const bool ok = (n == s.head.size() && worst <= 1e-9);
                        okCount += ok;
                        char vb[224];
                        snprintf(vb, sizeof(vb),
                                 "snapverify: at=%lld held=%d compared=%zu/%zu "
                                 "worst=%.9f firstBad=%lld %s",
                                 s.tick, s.held, n, s.head.size(), worst,
                                 firstBad, ok ? "OK" : "MISMATCH");
                        writeResult(vb);
                    }
                    writeResult("snapverify: " + std::to_string(okCount) + "/"
                                + std::to_string(g_snaps.size()) + " snapshots "
                                "reproduce the head run");
                }
                if (g_cfg.restoreAt >= 0 && g_ckpt && !g_restoreDone && g_tick >= g_cfg.restoreAt) {
                    g_restoreDone = true;
                    long long fromTick = g_tick;
                    clearCheckpointsUnsectioned(pl);
                    pl->storeCheckpoint(g_ckpt);
                    g_restorePending = true;
                    pl->resetLevel();
                    g_restorePending = false;
                    ev("RESTORE_done", (double)fromTick, (double)g_ckptTick);
                    const int wasDash = m_player1 ? (int)m_player1->m_isDashing : -1;
                    secRestoreDash(secsolve::g_ckptDash);
                    const double wasVy = m_player1 ? m_player1->m_yVelocity : 0.0;
                    if (m_player1) m_player1->m_yVelocity = g_ckptVy;
                    g_restoreProbe = 3;    // report the next three updates
                    g_nextInput = g_ckptNextInput;
                    g_nextToggle = g_ckptNextToggle;
                    g_tick = g_ckptTick;
                    const int wasLatch = m_player1
                        ? (int)(unsigned char)reinterpret_cast<const char*>(m_player1)[kOobLatchOff]
                        : -1;
                    if (m_player1)
                        reinterpret_cast<char*>(m_player1)[kOobLatchOff] =
                            (char)g_ckptOobLatch;
                    const char* pb1 = m_player1
                        ? reinterpret_cast<const char*>(m_player1) : nullptr;
                    writeResult("restore: from=" + std::to_string(fromTick)
                        + " to=" + std::to_string(g_ckptTick) + " tickNow=" + std::to_string(g_tick)
                        + " oobLatch=" + std::to_string(pb1 ? (int)(unsigned char)pb1[kOobLatchOff] : -1)
                        + " oobAtCkpt=" + std::to_string((int)g_ckptOobLatch)
                        + " oobGDLeft=" + std::to_string(wasLatch)
                        + " dashGDLeft=" + std::to_string(wasDash)
                        + " dashAtCkpt=" + std::to_string((int)secsolve::g_ckptDash.on));
                    {
                        char vb[224];
                        snprintf(vb, sizeof(vb),
                                 "restorevy: ckpt=%.12f gdLeft=%.12f now=%.12f",
                                 g_ckptVy, wasVy,
                                 m_player1 ? m_player1->m_yVelocity : 0.0);
                        writeResult(vb);
                    }
                }
            }
        }
        {
            const bool searching = secsolve::inFlight();
            const auto ot0 = std::chrono::steady_clock::now();
            updateOverlays(this);
            if (searching)
                secsolve::g_overlayMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - ot0).count();
        }
        audio::sync();
        notify::watchdog();   // un-freeze and hide a notification a purge left pinned
        notify::selfTest();   // cfg `notifytest=1` only
        hitbox::perFrame(this);
        if (g_stallResetPending && !secsolve::inFlight()) {
            g_stallResetPending = false;
            if (auto* pl = PlayLayer::get()) {
                writeResult("stall: resetLevel at the frame boundary");
                pl->resetLevel();
            }
        }
        if (!secsolve::inFlight())
            dpsolve::poll();   // installs the in-process solver's plan at a frame boundary
        if (static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this && !secsolve::inFlight()) {
            if (cpflight::g_pending.rec) cpStart(PlayLayer::get());
            if (takeover::g_restorePending) takeoverRestore(PlayLayer::get());
            if (cpflight::g_headPending) {
                cpflight::g_headPending = false;
                if (auto* pl = PlayLayer::get()) pl->resetLevel();
            }
            if (cpflight::g_controlPending) {   // the probe's control: the same plan from the head
                cpflight::g_controlPending = false;
                if (auto* pl = PlayLayer::get()) {
                    cpflight::g_probeCap.controlCarry = cpflight::sigDiff(
                        cpflight::g_probeCap.sourceSig, cpflight::carrySig(pl));
                    cpflight::g_controlStarting = true;
                    pl->resetLevel();
                    cpflight::g_controlStarting = false;
                    g_paused = false;   // held since the probe's end, as the probe's own start is
                }
            }
        }
        pollProbeHotkeys();
        handleProbeRequest(this);
        if (secsolve::inFlight()) {
            if (PlayLayer::get() != secsolve::g_taskLayer) {
                writeResult("secsolve: the level was rebuilt during the search - dropping it");
                secsolve::g_task.destroy();
                secsolve::g_taskLayer = nullptr;
                secsolve::setSearching(false);
                secsolve::g_noKill = false;
            } else if (g_sessionOver) {
                const int before = postmortem::privateMB();
                secsolve::reset();   // the task destroyed, its tables and checkpoints let go
                writeResult("secsolve: the session ended during the search - dropped it (private "
                            + std::to_string(before) + " -> " + std::to_string(postmortem::privateMB())
                            + " MB)");
                dpsolve::memCensus("search dropped");   // cfg memcensus (print only)
            } else {
                secsolve::g_sliceStart = std::chrono::steady_clock::now();
                if (secsolve::g_sliceEnded) {
                    secsolve::g_frameGapMs += std::chrono::duration<double, std::milli>(
                        secsolve::g_sliceStart - secsolve::g_sliceEnd).count();
                    ++secsolve::g_frames;
                }
                secsolve::g_task.resume();
                secsolve::g_sliceEnd = std::chrono::steady_clock::now();
                secsolve::g_sliceEnded = true;
                if (secsolve::g_task.done()) {
                    secsolve::g_task.destroy();
                    secsolve::g_taskLayer = nullptr;
                    secsolve::g_sliceEnded = false;
                }
                return;
            }
        }
        if (g_visResetPending && g_started && !g_sessionOver) {
            g_visResetPending = false;
            if (auto* pl = PlayLayer::get()) {
                log::info("watch: reset level, then resume rendering");
                pl->resetLevel();
                g_realtimeOverride = true;
                return;   // this frame ends here
            }
            g_realtimeOverride = true;
        }
        if (probe::g_pause && !itermap::seeking()) {
            if (probe::g_step > 0) {
                GJBaseGameLayer::update(probe::g_step / 240.0f);
                probe::g_step = 0;
            }
            return; // freeze game time completely
        }
        if (takeover::g_waiting && std::chrono::steady_clock::now() >= takeover::g_waitUntil) {
            takeover::g_waiting = false;
            g_paused = false;
        }
        if (g_pauseAtX > 0.f && !g_pauseAtXFired && !g_paused && g_started
            && !g_sessionOver
            && m_player1 && m_player1->getPositionX() >= g_pauseAtX) {
            g_pauseAtXFired = true;   // once only; otherwise F7 could not resume
            g_paused = true;
            writeResult("pauseatx: paused at x=" + std::to_string(m_player1->getPositionX())
                + " y=" + std::to_string(m_player1->getPositionY())
                + " tick=" + std::to_string((long long)g_tick));
        }
        {
            static bool s_tickFired = false;
            if (!g_started) s_tickFired = false;
            if (itermap::g_pauseAtTick > 0 && !s_tickFired && !g_paused && g_started && !g_sessionOver
                && !itermap::seeking() && m_player1 && g_tick >= itermap::g_pauseAtTick) {
                s_tickFired = true;
                g_paused = true;
                writeResult("pauseattick: paused at tick=" + std::to_string((long long)g_tick)
                    + " x=" + std::to_string(m_player1->getPositionX())
                    + " y=" + std::to_string(m_player1->getPositionY())
                    + " run=" + (seekcp::g_from < 0 ? std::string("head") : "cp@" + std::to_string(seekcp::g_from)));
            }
        }
        if (g_started && g_paused && !itermap::seeking()) {   // ...and the same for this stop
            if (g_stepTicks > 0) {
                ev("STEP_exec", g_stepTicks);
                GJBaseGameLayer::update(g_stepTicks / 240.0f);
                g_stepTicks = 0;
                flushAll();
            }
            return;
        }
        if (itermap::g_seekRestart && g_started) {
            itermap::g_seekRestart = false;
            if (itermap::g_seekRevive) {
                itermap::g_seekRevive = false;
                if (g_sessionOver && !g_serveMode) {
                    g_sessionOver = false;
                    g_finishedAttempts = 0;
                    ++g_cfg.maxAttempts;
                    writeResult("session_resume: the seek bar restarted the replay");
                }
            }
            g_paused = false;
            clearManualPause();
            g_pauseAtXFired = false;
            if (!g_sessionOver) {
                if (auto* pl = PlayLayer::get()) {
                    itermap::dismissEndScreen();   // or every clear stacks another one
                    audio::muteForSeek();
                    // cfg seekcp: from the latest checkpoint at or before the target, if any
                    if (!(itermap::g_seekTick >= 0 && seekCpRestore(pl, itermap::g_seekTick)))
                        pl->resetLevel();
                    audio::hushNow();
                    return;   // this frame ends here; the seek runs from the next one
                }
            }
            itermap::endSeek("nothing to restart");
        }
        if (itermap::seeking()) {
            const float sx = m_player1 ? m_player1->getPositionX() : 0.f;
            const bool sdead = !m_player1 || m_player1->m_isDead;
            const bool over = g_sessionOver;
            if (over) itermap::endSeek("the session ended");
            else if (itermap::seekArrived(sdead, dt))
                itermap::endSeek(sdead ? "died on the way" : "arrived");
            if (!itermap::seeking() && itermap::g_seekRepause) {
                itermap::g_seekRepause = false;
                probe::g_pause = true;
                long long fin = 0;
                if (itermap::g_seekStepTo >= 0)
                    fin = itermap::g_seekStepTo - itermap::g_nowTick;
                probe::g_step = (fin > 0 && fin < 240) ? (int)fin : 0;
                if (itermap::g_seekStepTo >= 0) {
                    char sb[192];
                    snprintf(sb, sizeof(sb),
                             "itermap: [stepback] landed t=%lld, wanted t=%lld, finishing +%d",
                             (long long)itermap::g_nowTick,
                             (long long)itermap::g_seekStepTo, probe::g_step);
                    writeResult(sb);
                }
                itermap::g_seekStepTo = -1;
            }
            if (!itermap::seeking() && g_fxSweep) sweepCircleWaves();
        }
        ev("BGL_update_pre", dt);
        bool wantWatch = (g_watchAtTick > 0 && g_tick >= g_watchAtTick)
            || (g_watchAtX > 0.f && m_player1 && m_player1->getPositionX() >= g_watchAtX);
        if (!wantWatch && g_watchAfterSec > 0.0) {
            double el = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - solver::g_solveStart).count();
            if (el >= g_watchAfterSec) wantWatch = true;
        }
        if (wantWatch && !g_realtimeOverride && g_cfg.fastdt > 0
            && g_watchStartTick < 0) {
            g_realtimeOverride = true; watchSpeedSet(WATCH_SPEED_1X);
            g_visRefresh = true;
            g_visResetPending = true;
            if (g_watchPurge) purgeDanglingActions();
            g_watchStartTick = g_tick;
            float wx = m_player1 ? m_player1->getPositionX() : -1.f;
            log::info("watchat: tick={} x={} -> realtime (F8 equivalent)",
                (long long)g_tick, wx);
            writeResult("watchat: tick=" + std::to_string((long long)g_tick)
                + " x=" + std::to_string(wx) + " -> realtime (F8 equivalent)");
            writeResult(fxCensus("watchat"));
        }
        if (g_watchCycleSec > 0.0 && g_cfg.fastdt > 0 && g_started && !g_sessionOver) {
            double el = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - solver::g_solveStart).count();
            if (g_watchCycleLast < 0.0) g_watchCycleLast = el;
            if (el - g_watchCycleLast >= g_watchCycleSec) {
                g_watchCycleLast = el;
                ++g_watchFlips;
                renderTogglePress();
                writeResult("watchcycle: press=" + std::to_string(g_watchFlips)
                    + " tick=" + std::to_string((long long)g_tick)
                    + " -> " + (g_realtimeOverride ? "realtime" : "FAST/pending"));
            }
        }
        if (g_watchBackTicks > 0 && g_realtimeOverride && g_watchStartTick >= 0
            && g_tick - g_watchStartTick >= g_watchBackTicks) {
            g_realtimeOverride = false; watchSpeedSet(WATCH_SPEED_1X);
            g_watchBackTicks = 0;
            log::info("watchback: tick={} -> FAST (frame skip again)", (long long)g_tick);
            writeResult("watchback: tick=" + std::to_string((long long)g_tick)
                + " -> FAST (frame skip again)");
        }
        profsample::frame(g_started && !g_sessionOver);   // cfg profsample (off)
        syncSectionHooks();                               // cfg sectionhooklazy (off)
        syncActWatchHook();                               // cfg objwatch (print only)
        const bool lockedNow = g_started && !g_sessionOver && m_player1
                   && m_player1->m_isLocked && m_player1->getPositionX() > 500.f;
        const auto nowLock = std::chrono::steady_clock::now();
        if (!lockedNow) g_endzoneLockStart = {};
        else if (g_endzoneLockStart.time_since_epoch().count() == 0)
            g_endzoneLockStart = nowLock;
        if (g_started && !g_sessionOver && g_cfg.fastdt > 0 && !g_realtimeOverride) {
            if (g_fxSweep && g_cfg.skipRender) {
                const int ran = g_cfg.frameBudgetMs > 0.0 ? std::max(1, g_lastBatchCalls)
                                                          : std::max(1, g_cfg.fastloops);
                float gameSecs = g_cfg.fastdt * ran;
                if (auto* d = cocos2d::CCDirector::sharedDirector())
                    if (auto* am = d->getActionManager())
                        static_cast<ActionManagerTick*>(am)->update(gameSecs);
                sweepCircleWaves();
            }
            stallwatch::Mark fm(stallwatch::FASTLOOP);
            int fastLoops = lockedNow ? 1 : std::max(1, g_cfg.fastloops);
            const long long headAt = (secsolve::g_on && !secsolve::g_done && !g_ckpt
                                      && g_cfg.practiceAt > 0)
                                         ? (long long)g_cfg.practiceAt - 8 : -1;
            if (!lockedNow && headAt > 0 && g_tick < headAt && g_secSaveFastloops > fastLoops)
                fastLoops = g_secSaveFastloops;
            if (lockedNow) {
                static long long s_lockLogged = -1;
                if (s_lockLogged != g_attempt) {
                    s_lockLogged = g_attempt;
                    writeResult("endzone: player locked at x="
                        + std::to_string((int)m_player1->getPositionX())
                        + " tick=" + std::to_string((long long)g_tick)
                        + " - dropping to 1 tick/frame for the end sequence");
                }
            }
            const auto batchT0 = std::chrono::steady_clock::now();
            g_lastBatchCalls = 0;
            for (int i = 0; i < fastLoops; ++i) {
                if (g_sessionOver) break;
                if (i > 0 && g_cfg.frameBudgetMs > 0.0
                    && std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - batchT0).count()
                           >= g_cfg.frameBudgetMs)
                    break;
                if (m_player1 && m_player1->m_isLocked
                    && m_player1->getPositionX() > 500.f) break;
                if (g_serveMode && g_serveWait) break;
                // ...and the section head's last ticks go back to one update a frame (above).
                if (i > 0 && headAt > 0 && g_tick >= headAt) break;
                if (g_serveReset) {
                    g_serveReset = false;
                    if (auto* pl = PlayLayer::get()) pl->resetLevel();
                }
                if (g_preludeResetPending && g_preludePendingAttempt != g_attempt) g_preludeResetPending = false;
                if (g_preludeResetPending) {
                    g_preludeResetPending = false;
                    ++g_preludeResets;
                    ++g_finishedAttempts;   // the prelude attempt ended by its `until`
                    if (auto* pl = PlayLayer::get()) pl->resetLevel();
                }
                if (i > 0 && takeover::stopsBatch()) break;   // the takeover's checkpoint is in
                { stallwatch::Mark gm(stallwatch::GD_UPDATE);
                  GJBaseGameLayer::update(g_cfg.fastdt); }
                ++g_lastBatchCalls;
                cpAfterUpdate();   // cfg cpflight: between two update calls, where a restore lands
                takeoverAfterUpdate();
            }
        } else {
            int loops = (g_started && !g_sessionOver)
                            ? std::max(1, (int)g_watchSpeed) : 1;
            const int approach =
                takeover::approachLoops(g_tick, m_player1 ? m_player1->getPositionX() : 0.f);
            const bool seekNow = g_started && !g_sessionOver && (itermap::seeking() || approach > 0);
            const float stepDt = seekNow ? (1.f / 60.f) : dt;
            if (g_started && !g_sessionOver)
                loops = std::max(loops, std::max(itermap::seekLoops(stepDt), approach));
            if (g_paused || probe::g_pause) loops = 0;
            bool locked = lockedNow && g_endzoneBurnOn
                && std::chrono::duration<double>(nowLock - g_endzoneLockStart).count()
                   >= ENDZONE_BURN_AFTER_SEC;
            if (!locked && g_endzoneBurn && (!m_player1 || !m_player1->m_isLocked)) {
                g_endzoneBurn = false;   // burn over: restore rendering
            }
            if (locked) {
                loops = std::max(loops, 1800);
                if (!g_endzoneBurn) {
                    g_endzoneStart = nowLock;
                    writeResult("endzone: still locked "
                        + std::to_string((int)ENDZONE_BURN_AFTER_SEC)
                        + "s after the goal (tick=" + std::to_string((long long)g_tick)
                        + ") -> burning the end animation (render off, 1800 ticks/frame)");
                }
                g_endzoneBurn = true;
            }
            const bool seekCounts = itermap::seeking();
            if (seekCounts) ++itermap::g_seekFrames;
            // cfg seekblind: screen off up to the last checkpoint slot at or before the target.
            long long blindCut = -1;
            if (seekCounts && itermap::g_seekBlind && !itermap::g_seekBlindDone && itermap::g_seekTick >= 0)
                blindCut = seekBlindCut(itermap::g_seekTick);
            itermap::g_seekBlindNow = blindCut > (long long)g_tick;
            for (int i = 0; i < loops; ++i) {
                if (i > 0 && g_sessionOver) break;
                if (i > 0 && takeover::stopsBatch()) break;   // the takeover's checkpoint is in
                const int ve = itermap::g_seekVisEvery;
                itermap::g_seekVisSkip = itermap::g_seekBlindNow
                    || (seekCounts && ve > 1 && i + 1 < loops && (i % ve) != ve - 1);
                stallwatch::Mark gm(stallwatch::GD_UPDATE);
                GJBaseGameLayer::update(stepDt);   // pinned while seeking; see above
                itermap::g_seekVisSkip = false;
                if (seekCounts) ++itermap::g_seekBatches;
                cpAfterUpdate();
                takeoverAfterUpdate();
                if (itermap::g_seekBlindNow
                    && ((long long)g_tick >= blindCut || !m_player1 || m_player1->m_isDead)) {
                    endSeekBlind(blindCut);
                    break;
                }
            }
        }
        if (g_sessionOver || g_retryDone) {
            static int s_beats = 0;
            static std::chrono::steady_clock::time_point s_last{};
            static std::chrono::steady_clock::time_point s_end = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            // Fire the equivalent of a retry (cfg `retryafter`)
            if (g_retryAfterSec > 0.0 && !g_retryDone
                && std::chrono::duration<double>(now - s_end).count() >= g_retryAfterSec) {
                g_retryDone = true;
                if (auto* pl = PlayLayer::get()) {
                    writeResult("retry: calling resetLevel (what the Retry button does)");
                    flushAll();
                    pl->resetLevel();
                }
            }
            if (s_beats < 24
                && std::chrono::duration<double>(now - s_last).count() > 2.0) {
                s_last = now;
                ++s_beats;
                char b[224];
                auto* pl = PlayLayer::get();
                snprintf(b, sizeof(b),
                    "post-session heartbeat %d: tick=%lld x=%.1f started=%d over=%d "
                    "paused=%d gdStarted=%d endAnim=%d ourPause=%d probePause=%d "
                    "pcCalls=%lld visitDraws=%lld",
                    s_beats, (long long)g_tick,
                    m_player1 ? m_player1->getPositionX() : -1.f,
                    g_started ? 1 : 0, g_sessionOver ? 1 : 0,
                    pl ? (pl->m_isPaused ? 1 : 0) : -1,
                    pl ? (pl->m_started ? 1 : 0) : -1,
                    pl ? (pl->m_levelEndAnimationStarted ? 1 : 0) : -1,
                    g_paused ? 1 : 0, probe::g_pause ? 1 : 0,
                    g_pcCalls, g_visitDraws);
                writeResult(b);
                flushAll();
            }
        }
        ev("BGL_update_post", dt);
    }

    void visit() {
        if (g_started && !g_sessionOver && !takeover::personDriving()) {
            if (auto* pl = PlayLayer::get())
                if (pl->m_isPaused) {
                    ++g_unfocusPauseBlocked;
                    if (g_unfocusPauseBlocked <= 5)
                        writeResult("pause: found the game paused mid-session -> resuming"
                            " (tick=" + std::to_string(g_tick) + ")");
                    if (auto* pause = pl->getChildByType<PauseLayer>(0)) pause->onResume(nullptr);
                }
        }
        if (renderSuppressed()) {   // fast mode's own skip, or F8
            ++g_visitSkips; return;
        }
        if (secsolve::inFlight()) { ++g_visitSkips; return; }
        if (g_endzoneBurn) { ++g_visitSkips; return; }   // burning the end animation (see below)
        if (itermap::g_seekBlindNow || (itermap::g_seekBlindDone && itermap::g_seekRestart)) {
            ++g_visitSkips; return;
        }
        ++g_visitDraws;
        hookdepth::Guard hg(hookdepth::VISIT);
        const bool seekCounts = itermap::seeking();
        const auto t0 = seekCounts ? std::chrono::steady_clock::now()
                                   : std::chrono::steady_clock::time_point{};
        safeVisit();
        if (seekCounts)
            itermap::g_seekDrawMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
    }

    void safeVisit() {
#ifdef GEODE_IS_WINDOWS
        __try {
            GJBaseGameLayer::visit();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            logVisitCrashSwallowed();
        }
#else
        // No SEH on Android: the draw runs unguarded, and a fault in it ends the game.
        GJBaseGameLayer::visit();
#endif
    }

    void secRestore(PlayLayer* pl) { secRestoreFrom(pl, g_ckpt); }

    void secCaptureDash(secsolve::DashState& d, PlayerObject* who = nullptr) {
        auto* p = who ? who : m_player1;
        if (!p) { d = secsolve::DashState{}; return; }
        d.on = p->m_isDashing;
        d.ring = reinterpret_cast<GameObject*>(p->m_dashRing);
        d.x = p->m_dashX;
        d.y = p->m_dashY;
        d.angle = p->m_dashAngle;
        d.startTime = p->m_dashStartTime;
        d.lastGround = p->m_lastGroundObject;
        d.maybeLastGround = p->m_maybeLastGroundObject;
        d.preLastGround = p->m_preLastGroundObject;
        d.collided = p->m_collidedObject;
        d.collideLeft = p->m_collidingWithLeft;
        d.collideRight = p->m_collidingWithRight;
        d.slope = p->m_currentSlope;
        d.slope2 = p->m_currentSlope2;
        d.potentialSlope = p->m_currentPotentialSlope;
        d.snappedTo = p->m_objectSnappedTo;
        d.lastPortal = p->m_lastActivatedPortal;
        d.jumpBuf = (int8_t)(p->m_jumpBuffered ? 1 : 0);
    }
    void secRestoreDash(const secsolve::DashState& d, PlayerObject* who = nullptr) {
        auto* p = who ? who : m_player1;
        if (!p) return;
        p->m_isDashing = d.on;
        p->m_dashRing = reinterpret_cast<DashRingObject*>(d.ring);
        p->m_dashX = d.x;
        p->m_dashY = d.y;
        p->m_dashAngle = d.angle;
        p->m_dashStartTime = d.startTime;
        p->m_lastGroundObject = d.lastGround;
        p->m_maybeLastGroundObject = d.maybeLastGround;
        p->m_preLastGroundObject = d.preLastGround;
        p->m_collidedObject = d.collided;
        p->m_collidingWithLeft = d.collideLeft;
        p->m_collidingWithRight = d.collideRight;
        p->m_currentSlope = d.slope;
        p->m_currentSlope2 = d.slope2;
        p->m_currentPotentialSlope = d.potentialSlope;
        p->m_objectSnappedTo = d.snappedTo;
        p->m_lastActivatedPortal = d.lastPortal;
    }
    void secRestoreJb(const secsolve::DashState& d, PlayerObject* who = nullptr) {
        auto* p = who ? who : m_player1;
        if (!secsolve::g_jbKeep || !p || d.jumpBuf < 0) return;
        p->m_jumpBuffered = d.jumpBuf != 0;
    }

    PlayerObject* secP2() {
        return (secsolve::g_p2Extras && m_player2 && m_player2 != m_player1) ? m_player2 : nullptr;
    }
    // Player 2 while a dual section has it (secsolve::g_p2Key), else null.
    PlayerObject* secDualP2() {
        return (m_gameState.m_isDualMode && m_player2 && m_player2 != m_player1) ? m_player2
                                                                                 : nullptr;
    }
    float secDualY2() {
        auto* q = secDualP2();
        return q ? (float)psnap::physPosition(q, this).y : 0.f;
    }
    void secPushP2() {
        auto* q = secP2();
        secsolve::g_dash2.emplace_back();
        if (q) secCaptureDash(secsolve::g_dash2.back(), q);
        secsolve::g_vy2.push_back(q ? q->m_yVelocity : 0.0);
        secsolve::g_accel2.push_back(q ? q->m_accelerationOrSpeed : 0.0);
        secsolve::g_pad2.push_back(q && q->m_touchedPad ? 1 : 0);
    }
    void secRestoreP2(size_t ni, bool vyToo) {
        auto* q = secP2();
        if (!q || ni >= secsolve::g_vy2.size()) return;
        secRestoreDash(secsolve::g_dash2[ni], q);
        secRestoreJb(secsolve::g_dash2[ni], q);
        if (vyToo) q->m_yVelocity = secsolve::g_vy2[ni];
        q->m_accelerationOrSpeed = secsolve::g_accel2[ni];
        q->m_touchedPad = secsolve::g_pad2[ni] != 0;
    }

    void secArmHold() {
        auto arm = [](PlayerObject* p) {
            if (!p) return;
            p->m_holdingButtons[1] = true;
            if (secsolve::g_jumpBuf) p->m_jumpBuffered = true;
        };
        arm(m_player1);
        arm(m_player2);
        secsolve::g_held = 1;
    }

    void secRestoreFrom(PlayLayer* pl, CheckpointObject* cp, int heldAfter = 0) {
    const SecRestTimer restTimer;
        clearCheckpointsUnsectioned(pl);
        {
            const auto s0 = std::chrono::steady_clock::now();
            pl->storeCheckpoint(cp);
            g_secStoreMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - s0).count();
        }
        g_restorePending = true;
        {
            const auto r0 = std::chrono::steady_clock::now();
            pl->resetLevel();
            g_secResetMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - r0).count();
        }
        g_restorePending = false;
        pl->m_queuedButtons.clear();
        g_injecting = true;
        this->handleButton(false, 1, true);
        g_injecting = false;
        secsolve::g_held = 0;
        if (heldAfter) secArmHold();
        if (secsolve::g_headDash && cp == g_ckpt) {
            secRestoreDash(secsolve::g_ckptDash);
            if (auto* q = secP2()) secRestoreDash(secsolve::g_ckptDash2, q);
        }
        // cfg secjbkeep: back at the head, each body's m_jumpBuffered as the plain flight had it
        if (secsolve::g_jbKeep && cp == g_ckpt) {
            if (m_player1 && secsolve::g_headJb[0] >= 0)
                m_player1->m_jumpBuffered = secsolve::g_headJb[0] != 0;
            if (m_player2 && m_player2 != m_player1 && secsolve::g_headJb[1] >= 0)
                m_player2->m_jumpBuffered = secsolve::g_headJb[1] != 0;
        }
        if (secsolve::g_forceP1Size >= 0 && cp == g_ckpt && m_player1
            && (m_player1->m_vehicleSize < 0.9f) != (secsolve::g_forceP1Size == 1))
            m_player1->togglePlayerScale(secsolve::g_forceP1Size == 1, true);
    }

    void secStep(int down, float) {
        secsolve::g_feed = down;
        GJBaseGameLayer::update((float)secsolve::g_dt);
    }

    void secStepBegin(PlayLayer* pl) {
        static_assert(offsetof(PlayLayer, m_levelEndAnimationStarted) == 0x3570,
                      "the byte 0x3aba00 raises and pauseGame tests first");
        if (!pl || !pl->m_levelEndAnimationStarted) return;
        pl->m_levelEndAnimationStarted = false;
        *(reinterpret_cast<uint8_t*>(pl) + 0x3878) = 0;   // checkForEnd's latch (2.2081)
    }

    CheckpointObject* markCheckpointAtPhys(PlayLayer* pl, bool createOnly = false) {
        PlayerObject* ps[2] = {m_player1, m_player2};
        cocos2d::CCPoint drawn[2];
        bool moved[2] = {false, false};
        for (int i = 0; i < 2; ++i) {
            if (!ps[i]) continue;
            const cocos2d::CCPoint at = psnap::physPosition(ps[i], this);
            drawn[i] = ps[i]->getPosition();
            if (at.x != drawn[i].x || at.y != drawn[i].y) {
                ps[i]->setPosition(at);
                moved[i] = true;
            }
        }
        CheckpointObject* cp = createOnly ? pl->createCheckpoint() : pl->markCheckpoint();
        for (int i = 0; i < 2; ++i)
            if (moved[i]) ps[i]->setPosition(drawn[i]);
        return cp;
    }

    // ---- cfg cpflight: loop flights from a checkpoint (mod/cp_flight.hpp) ----

    // The game events our own checkpoints raise, dropped (see cpflight::g_inTake).
    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if ((cpflight::g_inTake && event == GJGameEvent::Checkpoint)
            || (cpflight::g_starting && event == GJGameEvent::CheckpointRespawn)
            || (takeover::g_restoring && event == GJGameEvent::CheckpointRespawn)
            || (seekcp::g_restoring && event == GJGameEvent::CheckpointRespawn)) {
            ++cpflight::g_eventsDropped;
            return;
        }
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }

    void cpAfterUpdate() {
        if (!itermap::g_objWatch.empty() && m_objects && g_started && !g_sessionOver) {
            static const void* s_layer = nullptr;
            static std::vector<std::pair<GameObject*, int>> s_objs;   // object, last (disabled | in-section<<1)
            if (s_layer != this) {
                s_layer = this;
                s_objs.clear();
                for (auto* o : CCArrayExt<GameObject*>(m_objects))
                    if (o && std::find(itermap::g_objWatch.begin(), itermap::g_objWatch.end(), o->m_uniqueID)
                                 != itermap::g_objWatch.end())
                        s_objs.emplace_back(o, -1);
            }
            for (auto& [o, last] : s_objs) {
                const auto* ob = reinterpret_cast<const uint8_t*>(o);
                const int b305 = ob[0x305], b28e = ob[0x28e];
                const int outer = *reinterpret_cast<const int*>(ob + 0x278);
                const int inner = *reinterpret_cast<const int*>(ob + 0x27c);
                const int slot = *reinterpret_cast<const int*>(ob + 0x270);
                int liveCount = -1;
                {
                    const auto* lb = reinterpret_cast<const uint8_t*>(this);
                    const long long xs = (*reinterpret_cast<const long long*>(lb + 0x35b8)
                                          - *reinterpret_cast<const long long*>(lb + 0x35b0)) >> 3;
                    const long long cv = *reinterpret_cast<const long long*>(lb + 0x3658);
                    if (outer >= 0 && inner >= 0 && outer < xs && cv) {
                        const long long vp = *reinterpret_cast<const long long*>(cv + (long long)outer * 8);
                        if (vp) {
                            const long long d0 = *reinterpret_cast<const long long*>(vp);
                            const long long d1 = *reinterpret_cast<const long long*>(vp + 8);
                            if (d0 && inner < (d1 - d0) / 4) liveCount = *reinterpret_cast<const int*>(d0 + (long long)inner * 4);
                        }
                    }
                }
                const int now = (o->m_isGroupDisabled ? 1 : 0) | (o->m_outerSectionIndex >= 0 ? 2 : 0)
                                | (b305 ? 4 : 0) | (b28e ? 8 : 0) | ((slot < liveCount ? 1 : 0) << 4);
                if (now == last) continue;
                last = now;
                writeResult("objwatch: t=" + std::to_string((long long)g_tick) + " uid=" + std::to_string(o->m_uniqueID)
                            + " disabled=" + std::to_string(now & 1) + " sec=" + std::to_string(o->m_outerSectionIndex)
                            + " b305=" + std::to_string(b305) + " b28e=" + std::to_string(b28e)
                            + " sec=(" + std::to_string(outer) + "," + std::to_string(inner) + ") slot=" + std::to_string(slot)
                            + " live=" + std::to_string(liveCount)
                            + " run=" + (seekcp::g_from < 0 ? std::string("head") : "cp@" + std::to_string(seekcp::g_from)));
            }
        }        // cfg batchlog (print only): the ticks each update call ran, (end of the last call, now]
        {
            static long long s_prevEnd = -1;
            if (itermap::g_batchFrom >= 0 && g_started && !g_sessionOver && g_tick >= itermap::g_batchFrom
                && s_prevEnd <= itermap::g_batchTo) {
                char b[200];
                snprintf(b, sizeof(b), "batch: update ran t=%lld..%lld extraDelta=%.17g run=%s%lld", s_prevEnd + 1,
                         (long long)g_tick, m_extraDelta, seekcp::g_from < 0 ? "head" : "cp@",
                         seekcp::g_from < 0 ? 0LL : seekcp::g_from);
                writeResult(b);
            }
            s_prevEnd = g_tick;
        }
        if (g_cfg.cpFlightLayerDiff && m_player1 && cpflight::g_diffStep < cpflight::kDiffSteps) {
            auto* pl = PlayLayer::get();
            const auto* lb = reinterpret_cast<const uint8_t*>(pl);
            const auto* pb = reinterpret_cast<const uint8_t*>(m_player1);
            if (pl && cpflight::g_diffCollect) {
                auto& r = *cpflight::g_diffCollect;
                r.afterLayer.emplace_back(lb, lb + sizeof(PlayLayer));
                r.afterPlayer.emplace_back(pb, pb + sizeof(PlayerObject));
                r.afterTick.push_back(g_tick);
                r.afterTweens.push_back(cpflight::tweensLine(this));
                if (++cpflight::g_diffStep >= cpflight::kDiffSteps) cpflight::g_diffCollect.reset();
            } else if (pl && cpflight::g_diffCompare) {
                auto& r = *cpflight::g_diffCompare;
                const size_t k = (size_t)cpflight::g_diffStep;
                if (k < r.afterLayer.size() && r.afterTick[k] == g_tick) {
                    cpflight::layerDiff(r.afterLayer[k], pl, g_tick);
                    std::vector<uint8_t> tmp = r.afterPlayer[k];
                    writeResult("cpflight: playerdiff follows (offsets from player 1)");
                    cpflight::layerDiff(tmp, m_player1, g_tick);
                    const std::string tw = cpflight::tweensLine(this);
                    if (k < r.afterTweens.size() && tw != r.afterTweens[k])
                        writeResult("cpflight: tweens t=" + std::to_string(g_tick) + " flight:" + r.afterTweens[k]
                                    + " | restored:" + tw);
                    else
                        writeResult("cpflight: tweens t=" + std::to_string(g_tick) + " same:" + tw);
                }
                if (++cpflight::g_diffStep >= cpflight::kDiffSteps) cpflight::g_diffCompare.reset();
            }
        }
        if (cpflight::g_objAfter > 0) {
            --cpflight::g_objAfter;
            cpflight::objLine(this, "after-update", g_tick);
        }
        if (cpflight::g_next >= 0 && g_tick >= cpflight::g_next)
            if (auto* pl = PlayLayer::get(); pl && static_cast<GJBaseGameLayer*>(pl) == this) cpTake(pl);
        seekCpAfterUpdate();
        cpflight::nodeWatch(this, g_tick, nullptr);
        if (cpflight::g_controlFlying && !cpflight::g_controlAtT.have && cpflight::g_probeCap.from >= 0
            && g_tick >= cpflight::g_probeCap.from)
            if (auto* pl = PlayLayer::get(); pl && static_cast<GJBaseGameLayer*>(pl) == this)
                cpflight::g_controlAtT = cpflight::captureCarryFull(pl, g_tick);
        if (cpflight::g_probeFlying && cpflight::g_probeSet
            && g_tick > cpflight::g_probeSet->end + cpflight::kProbeOverrun) {
            writeResult(cpflight::probeCapture(g_tick, m_player1 ? m_player1->getPositionX() : -1.f,
                                               "outlived its source"));
            g_paused = true;
        } else if (cpflight::g_controlFlying && g_tick > cpflight::g_probeCap.end + cpflight::kProbeOverrun) {
            writeResult(cpflight::controlEnd(g_tick, m_player1 ? m_player1->getPositionX() : -1.f,
                                             "outlived the probe"));
            g_paused = true;
        }
    }

    std::vector<GameObject*>& followSectObjects() {
        static const void* layerOf = nullptr;
        static const void* arrayOf = nullptr;
        static size_t countOf = 0;
        static std::vector<GameObject*> objs;
        const size_t n = m_objects ? (size_t)m_objects->count() : 0;
        if (layerOf == this && arrayOf == m_objects && countOf == n) return objs;
        layerOf = this;
        arrayOf = m_objects;
        countOf = n;
        objs.clear();
        if (!m_objects) return objs;
        std::unordered_set<int> groups;
        for (auto* o : CCArrayExt<GameObject*>(m_objects))
            if (o && o->m_objectID == 1814)
                if (auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(o); e && e->m_targetGroupID > 0)
                    groups.insert(e->m_targetGroupID);
        if (groups.empty()) return objs;
        for (auto* o : CCArrayExt<GameObject*>(m_objects)) {
            if (!o || o->m_groupCount <= 0 || !o->m_groups) continue;
            for (int i = 0; i < std::min((int)o->m_groupCount, 10); ++i)
                if (groups.count((*o->m_groups)[i])) {
                    objs.push_back(o);
                    break;
                }
        }
        return objs;
    }
    static int& sectionCellX(GameObject* o) { return *reinterpret_cast<int*>(reinterpret_cast<char*>(o) + 0x278); }
    static int& sectionCellY(GameObject* o) { return *reinterpret_cast<int*>(reinterpret_cast<char*>(o) + 0x27c); }

    // The state GD's checkpoint does not carry, taken where the physics stands.
    void cpCaptureState(PlayLayer* pl, cpflight::Rec& r) {
        psnap::captureRaw(m_player1, this, r.player);
        secCaptureDash(r.dash);
        if (m_player2) secCaptureDash(r.dash2, m_player2);
        psnap::captureTouch(m_player1, r.touch, this);
        r.damageVerified = pl->m_damageVerified;
        {
            auto hb = m_player1->m_holdingButtons.find(1);
            r.held = (hb != m_player1->m_holdingButtons.end() && hb->second) ? 1 : 0;
        }
        r.oobLatch = (int)(unsigned char)reinterpret_cast<const char*>(m_player1)[kOobLatchOff];
        r.nextInput = g_nextInput;
        r.nextToggle = g_nextToggle;
        {
            auto* base = reinterpret_cast<unsigned char*>(geode::base::get());
            r.seedTrig = *reinterpret_cast<long long*>(base + gdoff::kSeedTriggerRva);
            r.seedVarIndex = *reinterpret_cast<long long*>(base + gdoff::kSeedVarIndexRva);
            r.seedVarTable = *reinterpret_cast<long long*>(base + gdoff::kSeedVarTableRva);
            r.seedKept = *reinterpret_cast<long long*>(reinterpret_cast<char*>(this)
                                                       + gdoff::kLayerRandomSeed);
        }
        cpflight::captureCamera(this, r.camera);
        cpflight::captureArea(this, r.area);
        r.followCells.clear();
        if (g_cfg.cpFollowSection)
            for (GameObject* o : followSectObjects())
                r.followCells.push_back({o, sectionCellX(o), sectionCellY(o)});
    }

    bool cpMark(PlayLayer* pl, cpflight::Rec& r) {
        cpflight::g_inTake = true;
        CheckpointObject* cp = markCheckpointAtPhys(pl);
        cpflight::g_inTake = false;
        if (!cp) return false;
        cp->retain();
        r.cp = cp;
        r.layer = pl;
        r.objects = pl->m_objects ? (size_t)pl->m_objects->count() : 0;
        clearCheckpointsUnsectioned(pl);
        return true;
    }

    void cpRestoreState(PlayLayer* pl, const cpflight::Rec& r, bool diag) {
        clearCheckpointsUnsectioned(pl);
        pl->m_queuedButtons.clear();
        if (r.held) {
            m_player1->m_holdingButtons[1] = true;
            if (m_player2) m_player2->m_holdingButtons[1] = true;
        }
        psnap::restore(m_player1, this, r.player.data());
        secRestoreDash(r.dash);
        if (m_player2) secRestoreDash(r.dash2, m_player2);
        psnap::restoreTouch(m_player1, r.touch, this);
        m_player1->m_objectSnappedTo = r.touch.objectSnappedTo;
        m_player1->m_collidedObject = r.touch.collidedObject;
        if (m_player2 && r.touch.second) {
            m_player2->m_objectSnappedTo = r.touch.second->objectSnappedTo;
            m_player2->m_collidedObject = r.touch.second->collidedObject;
        }
        pl->m_damageVerified = r.damageVerified;
        reinterpret_cast<char*>(m_player1)[kOobLatchOff] = (char)r.oobLatch;
        {
            auto* base = reinterpret_cast<unsigned char*>(geode::base::get());
            *reinterpret_cast<long long*>(base + gdoff::kSeedTriggerRva) = r.seedTrig;
            *reinterpret_cast<long long*>(base + gdoff::kSeedVarIndexRva) = r.seedVarIndex;
            *reinterpret_cast<long long*>(base + gdoff::kSeedVarTableRva) = r.seedVarTable;
            *reinterpret_cast<long long*>(reinterpret_cast<char*>(this) + gdoff::kLayerRandomSeed) =
                r.seedKept;
        }
        cpflight::restoreCamera(this, r.camera);
        if (diag) cpflight::objLine(this, "reset", r.tick);
        cpflight::restoreArea(this, r.area);
        if (itermap::g_groupCheck) groupToggleDiff(r);
        if (g_cfg.cpVisAfter) pl->updateVisibility(0.f);
        if (g_cfg.cpFollowSection && !r.followCells.empty()) {
            const float kx = *reinterpret_cast<const float*>(reinterpret_cast<const char*>(this) + 0x36a0);
            const float ky = *reinterpret_cast<const float*>(reinterpret_cast<const char*>(this) + 0x36a4);
            int moved = 0, already = 0, outside = 0, notStart = 0;
            if (kx > 0.f && ky > 0.f) {
                for (const auto& fc : r.followCells) {
                    GameObject* o = fc.obj;
                    if (!o || fc.sx < 0 || sectionCellX(o) < 0) { ++outside; continue; }
                    {
                        auto cellOf = [](double p, float k) {
                            return p <= 0.0 ? 0 : (int)((double)k * std::min(p, 1.0e7));
                        };
                        if (cellOf(o->m_startPosition.x, kx) != fc.sx || cellOf(o->m_startPosition.y, ky) != fc.sy)
                            ++notStart;
                    }
                    if (sectionCellX(o) == fc.sx && sectionCellY(o) == fc.sy) { ++already; continue; }
                    const double px = o->m_positionX, py = o->m_positionY;
                    o->m_positionX = ((double)fc.sx + 0.5) / (double)kx;
                    o->m_positionY = ((double)fc.sy + 0.5) / (double)ky;
                    this->updateObjectSection(o);
                    o->m_positionX = px;
                    o->m_positionY = py;
                    ++moved;
                }
            }
            char b[256];
            snprintf(b, sizeof(b), "cpfollowsection: the restore at t=%lld put %d of %zu Follow Player Y "
                     "object(s) back in their checkpoint cells (%d already there, %d outside any section; "
                     "%d checkpoint cell(s) are not the load-time one)",
                     r.tick, moved, r.followCells.size(), already, outside, notStart);
            writeResult(b);
        }
    }

    void groupToggleDiff(const cpflight::Rec& r) {
        auto it = seekcp::g_toggles.find(&r);
        if (it == seekcp::g_toggles.end() || !m_objects) {
            writeResult("groupcheck: no toggle record for the checkpoint at t=" + std::to_string(r.tick));
            return;
        }
        const auto& rec = it->second;
        size_t i = 0;
        int flagDiff = 0, secDiff = 0;
        std::string s;
        for (auto* o : CCArrayExt<GameObject*>(m_objects)) {
            if (i >= rec.size()) break;
            const uint8_t was = rec[i++];
            if (!o) continue;
            const uint8_t now = (uint8_t)((o->m_isGroupDisabled ? 1 : 0) | (o->m_outerSectionIndex >= 0 ? 2 : 0));
            if (now == was) continue;
            if ((now ^ was) & 1) ++flagDiff;
            if ((now ^ was) & 2) ++secDiff;
            if (flagDiff + secDiff <= 16)
                s += " " + std::to_string(o->m_uniqueID) + "(" + std::to_string(was & 1) + std::to_string((was >> 1) & 1)
                     + "->" + std::to_string(now & 1) + std::to_string((now >> 1) & 1) + ")";
        }
        writeResult("groupcheck: after the restore at t=" + std::to_string(r.tick) + " toggle differs on "
                    + std::to_string(flagDiff) + ", section on " + std::to_string(secDiff)
                    + " object(s) [uid(disabled,in-section) at the checkpoint -> now]:" + s);
    }

    void groupSectionCheck(const char* when, long long t) {
        if (!m_objects) return;
        int offIn = 0, onOut = 0;
        std::string offUids, onUids;
        for (auto* o : CCArrayExt<GameObject*>(m_objects)) {
            if (!o) continue;
            const bool in = o->m_outerSectionIndex >= 0;
            if (o->m_isGroupDisabled && in) {
                if (++offIn <= 8) offUids += " " + std::to_string(o->m_uniqueID);
            } else if (!o->m_isGroupDisabled && !in) {
                if (++onOut <= 8) onUids += " " + std::to_string(o->m_uniqueID);
            }
        }
        writeResult("groupcheck: " + std::string(when) + " t=" + std::to_string(t) + " run="
                    + (seekcp::g_from < 0 ? std::string("head") : "cp@" + std::to_string(seekcp::g_from))
                    + " off-but-in-a-section=" + std::to_string(offIn) + offUids
                    + " | on-but-in-none=" + std::to_string(onOut) + onUids);
    }

    // ---- the human takeover (mod/takeover.hpp) ----

    bool takeoverMark(PlayLayer* pl, cpflight::Rec& r) {
        cpflight::g_inTake = true;
        CheckpointObject* cp = markCheckpointAtPhys(pl, true);
        cpflight::g_inTake = false;
        if (!cp) return false;
        cp->retain();
        r.cp = cp;
        r.layer = pl;
        r.objects = pl->m_objects ? (size_t)pl->m_objects->count() : 0;
        return true;
    }

#ifdef GEODE_IS_WINDOWS
    static_assert(offsetof(PlayerObject, m_pendingCheckpoint) == 0x890, "PlayerObject::m_pendingCheckpoint");
    static_assert(offsetof(LevelSettingsObject, m_twoPlayerMode) == 0x154, "LevelSettingsObject::m_twoPlayerMode");
#endif

    void takeoverAfterUpdate() {
        if (secsolve::g_active) return;
        const bool start = takeover::approaching();
        if (!start && !(takeover::personDriving() && !takeover::g_waiting)) return;
        auto* pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || !m_player1) return;
        if (pl->m_playerDied || m_player1->m_isDead) {
            if (!start) {   // whatever the attempt that died had asked for goes with it
                takeover::g_cpWanted = false;
                takeover::dropPending();
            }
            return;
        }
        if (start) {
            const bool atTick = g_cfg.takeoverAt >= 0 && g_tick >= g_cfg.takeoverAt;
            const bool atX = g_cfg.takeoverX > 0.f && m_player1->getPositionX() >= g_cfg.takeoverX;
            if (atTick || atX) takeoverTake(pl, takeover::TakeKind::Start);
            return;
        }
        // The game stored its pending checkpoint: the strict one taken when it appeared goes in.
        if (takeover::g_commitPending) takeover::commitPending();
        // The game stored one at once (Z, a checkpoint in the level): a strict one here.
        if (takeover::g_cpWanted) {
            takeover::g_cpWanted = false;
            takeoverTake(pl, takeover::TakeKind::Now);
        }
        const void* pend = m_player1->m_pendingCheckpoint;
        if (pend != takeover::g_pendingSrc) {
            takeover::dropPending();
            if (pend) {
                takeoverTake(pl, takeover::TakeKind::Pending);
                takeover::g_pendingSrc = pend;
            }
        }
    }

    void takeoverTake(PlayLayer* pl, takeover::TakeKind kind) {
        const bool start = kind == takeover::TakeKind::Start;
        const long long T = g_tick;
        takeover::Cp c;
        c.rec = std::make_shared<cpflight::Rec>();
        c.rec->tick = T;
        cpCaptureState(pl, *c.rec);
        if (!takeoverMark(pl, *c.rec)) {
            if (start) {
                takeover::g_done = true;
                takeover::line("refused: GD gave no checkpoint at t=" + std::to_string(T)
                               + " - the mode is off for this session");
            } else {
                takeover::line("no checkpoint: GD gave none at t=" + std::to_string(T));
            }
            return;
        }
        if (start) {
            c.before.assign(g_cfg.inputs.begin(), g_cfg.inputs.begin()
                            + (std::ptrdiff_t)std::min(g_nextInput, g_cfg.inputs.size()));
        } else {
            for (const InputCmd& in : takeover::attemptPlan())
                if (in.step < T) c.before.push_back(in);
        }
        if (c.rec->held) c.before.push_back({(int)T, false});
        c.tick = T;
        const auto pos = m_player1->getPosition();
        c.x = pos.x;
        c.y = pos.y;
        c.vy = (float)m_player1->m_yVelocity;
        c.dumpAttempt = g_attempt;
        c.parent = takeover::g_from;
        c.source = start ? "start" : kind == takeover::TakeKind::Now ? "game" : "auto";
        c.gdSrc = kind == takeover::TakeKind::Now ? takeover::g_wantedSrc
                : kind == takeover::TakeKind::Pending ? (const void*)m_player1->m_pendingCheckpoint
                                                      : nullptr;
        if (kind == takeover::TakeKind::Now) {
            takeover::addCp(std::move(c));
            return;
        }
        if (kind == takeover::TakeKind::Pending) {
            takeover::g_pending = std::move(c);
            takeover::g_hasPending = true;
            return;
        }
        g_cfg.inputs = c.before;
        g_cfg.toggles.resize(std::min(g_nextToggle, g_cfg.toggles.size()));
        if (g_cfg.fastdt > 0 && !g_realtimeOverride) {
            g_realtimeOverride = true;
            takeover::line("warning: the session ran the fast loop - realtime from here, and the "
                           "screen stays as it was");
        }
        takeover::begin(std::move(c));
        if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    }

    void takeoverRestore(PlayLayer* pl) {
        takeover::g_restorePending = false;
        takeover::g_cpWanted = false;
        takeover::dropPending();
        if (!takeover::personDriving() || takeover::g_cps.empty()) return;
        const takeover::Cp& c = takeover::g_cps.back();
        if (!c.rec || !c.rec->cp) return;
        const cpflight::Rec& r = *c.rec;
        takeover::g_restoring = true;
        clearCheckpointsUnsectioned(pl);
        pl->storeCheckpoint(r.cp);
        g_restorePending = true;
        pl->resetLevel();
        g_restorePending = false;
        takeover::g_restoring = false;
        cpRestoreState(pl, r, false);
        g_cfg.inputs = c.before;
        g_nextToggle = r.nextToggle;
        g_nextInput = 0;
        while (g_nextInput < g_cfg.inputs.size() && g_cfg.inputs[g_nextInput].step < r.tick)
            ++g_nextInput;
        g_tick = r.tick;
        const auto pos = m_player1->getPosition();
        takeover::restored(c, pos.x, pos.y, (float)m_player1->m_yVelocity);
    }

    // ---- cfg seekcp: the seek bar's checkpoints (mod/seek_cp.hpp) ----

    void latchLine() {
        static long long s_doneFor = -2;   // the run (attempt, seekcp::g_from) it was printed for
        const long long at = itermap::g_latchAt;
        if (at <= 0 || !m_player1 || !m_objects || !g_started || g_sessionOver) return;
        const long long run = (long long)g_attempt * 1000000 + seekcp::g_from;
        if (g_tick < at || s_doneFor == run) return;
        s_doneFor = run;
        const float px = m_player1->getPositionX(), py = m_player1->getPositionY();
        int set = 0;
        std::string nearby;
        for (auto* o : CCArrayExt<GameObject*>(m_objects)) {
            if (!o) continue;
            if (o->m_shouldUseOuterOb) ++set;
            if (std::fabs((float)o->m_positionX - px) > 300.f || std::fabs((float)o->m_positionY - py) > 300.f) continue;
            if (o->getRotation() == 0.f && !o->m_shouldUseOuterOb) continue;
            char b[96];
            snprintf(b, sizeof(b), " %d/%d:L%d r%.1f", o->m_uniqueID, o->m_objectID,
                     o->m_shouldUseOuterOb ? 1 : 0, (double)o->getRotation());
            nearby += b;
        }
        char h[200];
        snprintf(h, sizeof(h), "latch: t=%lld run=%s%lld player=(%.3f,%.3f) latched=%d near(rotated or latched):",
                 (long long)g_tick, seekcp::g_from < 0 ? "head" : "cp@", seekcp::g_from < 0 ? 0LL : seekcp::g_from,
                 (double)px, (double)py, set);
        writeResult(std::string(h) + nearby);
    }

    void regionLine() {
        static long long s_doneFor = -2;
        const long long at = itermap::g_regionAt;
        if (at <= 0 || !m_player1 || !m_objects || !g_started || g_sessionOver) return;
        const long long run = (long long)g_attempt * 1000000 + seekcp::g_from;
        if (g_tick < at || s_doneFor == run) return;
        s_doneFor = run;
        const float* bx = itermap::g_regionBox;
        char h[200];
        snprintf(h, sizeof(h), "region: t=%lld run=%s%lld player=(%.3f,%.3f) vy=%.3f box=(%.0f,%.0f)-(%.0f,%.0f)",
                 (long long)g_tick, seekcp::g_from < 0 ? "head" : "cp@", seekcp::g_from < 0 ? 0LL : seekcp::g_from,
                 (double)m_player1->getPositionX(), (double)m_player1->getPositionY(),
                 (double)m_player1->m_yVelocity, (double)bx[0], (double)bx[1], (double)bx[2], (double)bx[3]);
        writeResult(h);
        groupSectionCheck("region", (long long)g_tick);
        for (auto* o : CCArrayExt<GameObject*>(m_objects)) {
            if (!o) continue;
            const double x = o->m_positionX, y = o->m_positionY;
            if (x < bx[0] || x > bx[2] || y < bx[1] || y > bx[3]) continue;
            const auto& r = o->m_objectRect;
            char b[260];
            snprintf(b, sizeof(b), "region:   uid=%d id=%d type=%d pos=(%.3f,%.3f) rect=(%.2f,%.2f %.2fx%.2f) "
                     "gdis=%d gdisT=%d dis=%d dis2=%d inv=%d hide=%d vis=%d rot=%.1f sec=%d",
                     o->m_uniqueID, o->m_objectID, (int)o->m_objectType, x, y, (double)r.origin.x, (double)r.origin.y,
                     (double)r.size.width, (double)r.size.height, (int)o->m_isGroupDisabled,
                     (int)o->m_isGroupDisabledTemp, (int)o->m_isDisabled, (int)o->m_isDisabled2,
                     (int)o->m_isInvisible, (int)o->m_isHide, (int)o->isVisible(), (double)o->getRotation(),
                     (int)o->m_outerSectionIndex);
            writeResult(b);
        }
    }

    void actCensus() {
        if (!itermap::g_actCensus || !g_started || g_sessionOver || !m_player1) return;
        auto& c = itermap::g_actCensusState;
        bool any = false;
        for (PlayerObject* p : {m_player1, m_player2}) {
            if (!p || (p == m_player2 && !m_gameState.m_isDualMode)) continue;
            cocos2d::CCRect q = p->getObjectRect();
            q.origin.x -= 60.f; q.origin.y -= 60.f; q.size.width += 120.f; q.size.height += 120.f;
            for (int kind = 0; kind < 2; ++kind) {
                cocos2d::CCArray* a = kind == 0 ? staticObjectsInRect(q, true) : damagingObjectsInRect(q, true);
                if (!a) continue;
                for (auto* o : CCArrayExt<GameObject*>(a)) {
                    if (!o || !reinterpret_cast<const uint8_t*>(o)[0x28e]) continue;
                    any = true;
                    auto& e = c.objs[o->m_uniqueID];
                    if (e.first < 0) {
                        e.first = g_tick;
                        e.id = o->m_objectID;
                        e.kind = kind;
                        if (c.objs.size() <= 40)
                            writeResult("actcensus: t=" + std::to_string((long long)g_tick) + " uid=" + std::to_string(o->m_uniqueID)
                                        + " id=" + std::to_string(o->m_objectID) + (kind == 0 ? " solid" : " hazard")
                                        + " is switched off and activated near the player");
                    }
                    e.last = g_tick;
                    ++e.boundaries;
                }
            }
        }
        if (any) ++c.boundaries;
        ++c.checked;
    }

    // After every update call of a plain replay: a checkpoint when one is due.
    void seekCpAfterUpdate() {
        actCensus();
        latchLine();
        regionLine();
        if (seekcp::g_every <= 0 || seekcp::g_off) return;
        if (!g_started || g_sessionOver || g_cfg.dpSolve || g_serveMode || secsolve::g_on
            || secsolve::g_active || takeover::armed())
            return;
        auto* pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || !m_player1) return;
        if (pl->m_isPracticeMode || pl->m_playerDied || m_player1->m_isDead || m_player1->m_isLocked) return;
        seekcp::validate(pl, pl->m_objects ? (size_t)pl->m_objects->count() : 0);
        if (seekcp::g_step <= 0) seekcp::g_step = seekcp::g_every;
        if (!seekcp::due(g_tick)) return;
        if (seekcp::g_memBefore < 0) seekcp::g_memBefore = privateMB();
        const auto t0 = std::chrono::steady_clock::now();
        auto r = std::make_shared<cpflight::Rec>();
        r->tick = g_tick;
        cpCaptureState(pl, *r);
        if (!cpMark(pl, *r)) {
            writeResult("seekcp: markCheckpoint returned 0 at t=" + std::to_string((long long)g_tick)
                        + " - off for this session");
            seekcp::g_off = true;
            return;
        }
        if (itermap::g_groupCheck) {
            std::vector<uint8_t> tg;
            tg.reserve(m_objects ? m_objects->count() : 0);
            for (auto* o : CCArrayExt<GameObject*>(m_objects))
                tg.push_back(o ? (uint8_t)((o->m_isGroupDisabled ? 1 : 0) | (o->m_outerSectionIndex >= 0 ? 2 : 0)) : 0);
            seekcp::g_toggles[r.get()] = std::move(tg);
            groupSectionCheck("checkpoint taken", (long long)g_tick);
        }
        seekcp::add(std::move(r));
        ++seekcp::g_taken;
        seekcp::g_takeMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        seekcp::g_memAfter = privateMB();
    }

    long long seekBlindCut(long long target) {
        if (seekcp::g_every <= 0 || seekcp::g_off) return -1;
        const long long step = seekcp::g_step > 0 ? seekcp::g_step : seekcp::g_every;
        return (target / step) * step;
    }

    void endSeekBlind(long long cut) {
        itermap::g_seekBlindNow = false;
        itermap::g_seekBlindDone = true;
        itermap::g_seekBlindMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - itermap::g_seekT0).count();
        itermap::g_seekRestart = true;
        char b[200];
        snprintf(b, sizeof(b), "seekblind: screen off up to t=%lld (cut %lld%s) in %.0f ms; the rest from "
                 "the checkpoint at or before t=%lld", (long long)g_tick, cut,
                 (m_player1 && m_player1->m_isDead) ? ", died first" : "", itermap::g_seekBlindMs,
                 (long long)itermap::g_seekTick);
        writeResult(b);
    }

    bool seekCpRestore(PlayLayer* pl, long long target) {
        if (seekcp::g_every <= 0 || seekcp::g_off || !pl || !m_player1) return false;
        seekcp::validate(pl, pl->m_objects ? (size_t)pl->m_objects->count() : 0);
        long long want = target;
        if (itermap::g_seekFromCp >= 0) { want = std::min(want, itermap::g_seekFromCp); itermap::g_seekFromCp = -1; }
        auto r = seekcp::atOrBefore(want);
        if (!r || !r->cp) return false;
        const auto t0 = std::chrono::steady_clock::now();
        seekcp::g_restoring = true;
        clearCheckpointsUnsectioned(pl);
        pl->storeCheckpoint(r->cp);
        g_restorePending = true;
        pl->resetLevel();
        g_restorePending = false;
        seekcp::g_restoring = false;
        cpRestoreState(pl, *r, false);
        g_nextInput = r->nextInput;
        g_nextToggle = r->nextToggle;
        g_tick = r->tick;
        itermap::g_nowTick = r->tick;
        seekcp::g_from = r->tick;
        seekcp::g_partSaid = false;
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        ++seekcp::g_restores;
        seekcp::g_restoreMs += ms;
        char b[200];
        snprintf(b, sizeof(b), "seekcp: restored the checkpoint at t=%lld for a seek to t=%lld in %.1f ms",
                 r->tick, target, ms);
        writeResult(b);
        return true;
    }

    void cpTake(PlayLayer* pl) {
        const long long S = g_cfg.cpFlight;
        const long long T = g_tick;
        if (!m_player1 || !cpflight::g_live || cpflight::g_live->attempt != g_attempt || S <= 0) {
            cpflight::g_next = -1;
            return;
        }
        if (pl->m_playerDied || m_player1->m_isDead) return;
        // A tick without an anchor row cannot be checked after a restore: the next slot instead.
        if (T < 1 || (size_t)T >= anchors::g_live.size() || !anchors::g_live[(size_t)T].valid) {
            cpflight::g_next = (T / S + 1) * S;
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto r = std::make_shared<cpflight::Rec>();
        r->tick = T;
        cpCaptureState(pl, *r);
        if (g_cfg.cpFlightLayerDiff)
            r->layerBytes.assign(reinterpret_cast<const uint8_t*>(pl),
                                 reinterpret_cast<const uint8_t*>(pl) + sizeof(PlayLayer));
        cpflight::objLine(this, "take", T);
        if (g_cfg.cpFlightObj >= 0) cpflight::g_objAfter = 3;
        cpflight::captureBooks(*r);
        if (!cpMark(pl, *r)) {
            writeResult("cpflight: markCheckpoint returned 0 at t=" + std::to_string(T)
                        + " - no more checkpoints this attempt");
            cpflight::g_next = -1;
            return;
        }
        if (g_cfg.cpFlightLayerDiff) {
            cpflight::g_diffCollect = r;
            cpflight::g_diffCompare.reset();
            cpflight::g_diffStep = 0;
        }
        cpflight::add(std::move(r));
        cpflight::g_takeMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
    }

    void cpStart(PlayLayer* pl) {
        cpflight::Choice c = std::move(cpflight::g_pending);
        cpflight::g_pending = cpflight::Choice{};
        if (!pl) return;
        if (!c.rec || !c.set || !m_player1) {
            if (!c.probe) pl->resetLevel();
            return;
        }
        const cpflight::Rec& r = *c.rec;
        if (c.probe && cpflight::firstDifference(c.set->plan, g_cfg.inputs) != LLONG_MAX) {
            writeResult("cpflight: probe dropped - the installed plan is not the one that died");
            return;
        }
        // The carry once more, at the reset itself (it was judged at the install or the queueing).
        if (const int d = cpflight::sigDiff(c.set->startSig, cpflight::carrySig(this)); d != 0) {
            if (c.probe) {
                ++cpflight::g_probeSkips;
                return;
            }
            cpflight::countCarryMiss(d);
            pl->resetLevel();
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        clearCheckpointsUnsectioned(pl);
        pl->storeCheckpoint(r.cp);
        cpflight::g_starting = &c;
        g_restorePending = true;
        pl->resetLevel();
        g_restorePending = false;
        cpflight::g_starting = nullptr;
        cpRestoreState(pl, r, true);
        cpflight::objLine(this, "restored", r.tick);
        cpflight::nodeWatch(this, r.tick, "restored");
        if (g_cfg.cpFlightObj >= 0) cpflight::g_objAfter = 3;
        g_nextInput = r.nextInput;
        g_nextToggle = r.nextToggle;
        g_tick = r.tick;
        if (g_cfg.cpFlightLayerDiff) cpflight::layerDiff(r.layerBytes, pl, r.tick);
        const AnchorRow& row = c.set->rows[(size_t)r.tick + 1];
        const auto pos = m_player1->getPosition();
        const float vy = (float)m_player1->m_yVelocity;
        char b[320];
        if (pos.x != row.x || pos.y != row.y) {
            snprintf(b, sizeof(b), "cpflight: the restore at t=%lld came back at x=%.4f y=%.4f vy=%.4f, "
                     "not row t+1's x=%.4f y=%.4f vy=%.4f - %s", r.tick, (double)pos.x, (double)pos.y,
                     (double)vy, (double)row.x, (double)row.y, (double)row.vy,
                     c.probe ? "no probe" : "flying the plan from the head");
            writeResult(b);
            ++cpflight::g_refused;
            if (!c.probe) {
                cpflight::g_forceHead = true;
                pl->resetLevel();
            } else {
                g_paused = true;
            }
            return;
        }
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        cpflight::g_startMs += ms;
        if (g_cfg.cpFlightLayerDiff) {
            cpflight::g_diffCompare = c.rec;
            cpflight::g_diffCollect.reset();
            cpflight::g_diffStep = 0;
        }
        if (c.probe) {
            cpflight::g_probeFlying = true;
            cpflight::g_ended = false;   // counted as a probe flight
            cpflight::g_probeSet = c.set;
            cpflight::g_probeRec = c.rec;
            cpflight::g_probeAtT = cpflight::captureCarryFull(pl, r.tick);   // for the control's comparison
        } else {
            ++cpflight::g_starts;
            cpflight::g_ticksSkipped += r.tick;
        }
        snprintf(b, sizeof(b), "cpflight: %s attempt %d from the checkpoint at t=%lld x=%.1f (taken by "
                 "attempt %d%s), restored in %.1f ms", c.probe ? "probe" : "flight", g_attempt, r.tick,
                 (double)row.x, c.set->attempt,
                 c.probe ? (", which died at t=" + std::to_string(c.set->end)).c_str() : "", ms);
        writeResult(b);
    }

    void runSnapCompare(PlayLayer* pl) {
        snapSweep(pl, secsolve::g_snapCmp, true);
        endSession("psnap");
    }

    static GJEffectManager* EM(GJBaseGameLayer* l) {
        return l ? l->m_effectManager : nullptr;
    }

    static bool isWorldDefiningPortal(int id) {
        switch (id) {
            case 12: case 13: case 47: case 111: case 660:
            case 745: case 1331: case 1933:            // mode
            case 10: case 11:                          // gravity
            case 99: case 101:                         // size
            case 286: case 287:                        // dual
            case 45: case 46:                          // mirror
            case 747: case 2902:                       // teleport
                return true;
            default:
                return false;
        }
    }

    void wsnapTriggerSet(std::vector<GameObject*>& out, double x0, double x1, bool anyTrigger = false) {
        if (!this->m_objects) return;
        auto mover = [anyTrigger](int id) {
            if (anyTrigger) return id != 1268;
            return id == 901 || id == 1346 || id == 1347 || id == 1814 || id == 2067 || id == 1049;
        };
        auto take = [&](EffectGameObject* e, std::set<int>& into) {
            into.insert(e->m_targetGroupID);
            if (anyTrigger && e->m_centerGroupID > 0) into.insert(e->m_centerGroupID);
        };
        std::set<int> groups(secsolve::g_wsnapGroups.begin(), secsolve::g_wsnapGroups.end());
        if (groups.empty()) {
            std::set<int> spawned;
            for (auto* o : CCArrayExt<GameObject*>(this->m_objects)) {
                if (!o) continue;
                const double ox = o->getPositionX();
                if (ox < x0 || ox > x1) continue;
                auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(o);
                if (!e || e->m_targetGroupID <= 0) continue;
                if (mover(o->m_objectID)) take(e, groups);
                else if (o->m_objectID == 1268) spawned.insert(e->m_targetGroupID);
            }
            for (int g : spawned)
                if (auto* arr = this->getGroup(g))
                    for (auto* o : CCArrayExt<GameObject*>(arr))
                        if (auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(o))
                            if (e->m_targetGroupID > 0 && mover(o->m_objectID))
                                take(e, groups);
        }
        std::unordered_set<GameObject*> seen(out.begin(), out.end());
        for (int g : groups)
            if (auto* arr = this->getGroup(g))
                for (auto* o : CCArrayExt<GameObject*>(arr))
                    if (o && seen.insert(o).second) out.push_back(o);
    }

    int movingObjectsInSection(PlayLayer* pl, int n,
                               std::vector<GameObject*>* out = nullptr,
                               int* portalsOut = nullptr, bool movedOnly = false,
                               double* winLo = nullptr, double* winHi = nullptr,
                               bool physOnly = false, bool toHorizon = false) {
        if (out) out->clear();
        auto* arr = this->m_objects;
        const int nobj = arr ? arr->count() : 0;
        if (!nobj) return 0;
        const float dt = g_cfg.fastdt;
        int held = 0;
        for (const auto& in : g_cfg.inputs)
            if (in.step <= g_ckptTick) held = in.down ? 1 : 0;
        secRestore(pl);
        for (int i = 0; i < 2; ++i) secStep(held, dt);   // the frozen ticks
        std::vector<float> px((size_t)nobj), py((size_t)nobj);
        for (int i = 0; i < nobj; ++i) {
            auto* o = static_cast<GameObject*>(arr->objectAtIndex((unsigned)i));
            px[(size_t)i] = o->getPositionX();
            py[(size_t)i] = o->getPositionY();
        }
        const bool savedNoKill = secsolve::g_noKill;
        secsolve::g_noKill = true;
        double wx0 = m_player1 ? m_player1->getPositionX() : 0.0, wx1 = wx0;
        double vMax = 0.0;   // the fastest x step the probe saw (secwsnapset=5)
        std::vector<uint8_t> moving((size_t)nobj, 0);
        const bool anyChange = secsolve::g_wsnapAuto && secsolve::g_wsnapAny && out;
        constexpr int kPoseEvery = 10;
        static const std::vector<psnap::Mem> poseMems = [] {
            static const char* const kNames[] = {
                "m_positionXOffset", "m_positionYOffset", "m_rotationXOffset", "m_rotationYOffset",
                "m_scaleXOffset", "m_scaleYOffset", "m_customScaleX", "m_customScaleY",
                "m_isRotationAligned"};
            std::vector<psnap::Mem> r;
            for (const auto& m : psnap::goScalars())
                for (const char* n : kNames)
                    if (std::strcmp(m.name, n) == 0) { r.push_back(m); break; }
            return r;
        }();
        size_t poseBytes = 7 * sizeof(float);
        for (const auto& m : poseMems) poseBytes += m.size;
        std::vector<uint8_t> changed(anyChange ? (size_t)nobj : 0, 0);
        std::vector<uint8_t> pose0(anyChange ? (size_t)nobj * poseBytes : 0);
        auto poseOf = [&](GameObject* o, uint8_t* d) {
            const float f[7] = {o->getPositionX(), o->getPositionY(), o->getRotationX(),
                                o->getRotationY(), o->getScaleX(), o->getScaleY(),
                                o->isVisible() ? 1.f : 0.f};
            std::memcpy(d, f, sizeof f);
            d += sizeof f;
            for (const auto& m : poseMems) {
                std::memcpy(d, (const uint8_t*)o + m.off, m.size);
                d += m.size;
            }
        };
        auto poseCheck = [&]() {
            std::vector<uint8_t> now(poseBytes);
            for (int i = 0; i < nobj; ++i) {
                if (changed[(size_t)i]) continue;
                auto* o = static_cast<GameObject*>(arr->objectAtIndex((unsigned)i));
                poseOf(o, now.data());
                const uint8_t* b = pose0.data() + (size_t)i * poseBytes;
                const float* fa = (const float*)b;
                const float* fb = (const float*)now.data();
                bool diff = false;
                for (int k = 0; k < 7 && !diff; ++k) diff = std::fabs(fa[k] - fb[k]) > 1e-4f;
                if (!diff)
                    diff = std::memcmp(b + 7 * sizeof(float), now.data() + 7 * sizeof(float),
                                       poseBytes - 7 * sizeof(float)) != 0;
                if (diff) changed[(size_t)i] = 1;
            }
        };
        for (int pat = 0; pat < 3; ++pat) {
            secRestore(pl);
            for (int i = 0; i < 2; ++i) secStep(held, dt);   // the frozen ticks
            for (int i = 0; i < nobj; ++i) {
                auto* o = static_cast<GameObject*>(arr->objectAtIndex((unsigned)i));
                px[(size_t)i] = o->getPositionX();
                py[(size_t)i] = o->getPositionY();
                if (anyChange) poseOf(o, pose0.data() + (size_t)i * poseBytes);
            }
            double xPrev = m_player1 ? m_player1->getPositionX() : 0.0;
            for (int i = 0; i < n; ++i) {
                const int in = pat == 0 ? 0 : (pat == 1 ? 1 : (i & 1));
                secStep(in, dt);
                if (!m_player1) break;
                const double x = m_player1->getPositionX();
                wx0 = std::min(wx0, x); wx1 = std::max(wx1, x);
                vMax = std::max(vMax, x - xPrev);
                xPrev = x;
                if (anyChange && i % kPoseEvery == kPoseEvery - 1) poseCheck();
            }
            if (anyChange) poseCheck();
            for (int i = 0; i < nobj; ++i) {
                auto* o = static_cast<GameObject*>(arr->objectAtIndex((unsigned)i));
                if (std::fabs(o->getPositionX() - px[(size_t)i]) > 1e-4
                    || std::fabs(o->getPositionY() - py[(size_t)i]) > 1e-4)
                    moving[(size_t)i] = 1;
            }
        }
        secsolve::g_noKill = savedNoKill;
        if (toHorizon && secsolve::g_horizon > n && vMax > 0.0) {
            const double ext = (double)(secsolve::g_horizon - n) * vMax;
            wx1 += ext;
            char eb[160];
            snprintf(eb, sizeof(eb), "secsolve: wsnap reach extended %.0f px past the probe (%d of %d ticks, "
                     "%.2f px a tick at most) (secwsnapset=5)", ext, n, secsolve::g_horizon, vMax);
            writeResult(eb);
        }
        wx0 -= 900.0; wx1 += 900.0;
        if (winLo) *winLo = wx0;
        if (winHi) *winHi = wx1;
        int moved = 0;
        if (portalsOut) *portalsOut = 0;
        for (int i = 0; i < nobj; ++i) {
            if (moving[(size_t)i]) ++moved;
            auto* o = static_cast<GameObject*>(arr->objectAtIndex((unsigned)i));
            const bool grouped = o->m_groupCount > 0 || o->m_hasGroupParent
                                 || o->m_hasAreaParent;
            const double ox = o->getPositionX();
            const bool inWindow = ox >= wx0 && ox <= wx1;
            if (portalsOut && isWorldDefiningPortal(o->m_objectID)
                && moving[(size_t)i])
                ++*portalsOut;
            if (!out) continue;
            const bool posed = anyChange && changed[(size_t)i];
            const bool reach = grouped && inWindow && !(physOnly && (int)o->m_objectType == 7);
            if (moving[(size_t)i] || posed || (!movedOnly && reach))
                out->push_back(o);
        }
        if (anyChange) {
            size_t nChanged = 0;
            for (uint8_t c : changed) nChanged += c;
            char cb[160];
            snprintf(cb, sizeof(cb), "secsolve: wsnap probe %zu objects - %d moved end to start, %zu changed "
                     "pose at a sample (cfg secwsnapany)", out->size(), moved, nChanged);
            writeResult(cb);
        }
        return moved;
    }

    void runWorldDiff(PlayLayer* pl) {
        using namespace secsolve;
        const float dt = g_cfg.fastdt;
        const int N = g_worldDiff;
        char b[256];
        auto* arr = this->m_objects;
        const int nobj = arr ? arr->count() : 0;
        const auto& gos = psnap::goScalars();
        const auto& gbs = psnap::gbScalars();
        const size_t NOBJM = gos.size() + 3;
        const size_t NCNT_ = 21;                 // must match cnts below
        const size_t NM = NOBJM + gbs.size() + NCNT_;
        struct Cnt { const char* name; std::function<long long()> get; };
        const std::vector<Cnt> cnts = {
            {"m_objects", [&] { return (long long)(this->m_objects ? this->m_objects->count() : -1); }},
            {"m_collisionBlocks", [&] { return (long long)(this->m_collisionBlocks ? this->m_collisionBlocks->count() : -1); }},
            {"m_spawnObjectsArray", [&] { return (long long)(this->m_spawnObjectsArray ? this->m_spawnObjectsArray->count() : -1); }},
            {"m_disabledObjects", [&] { return (long long)this->m_disabledObjects.size(); }},
            {"m_areaObjects", [&] { return (long long)this->m_areaObjects.size(); }},
            {"m_processedAreaObjects", [&] { return (long long)this->m_processedAreaObjects.size(); }},
            {"m_visibleObjects", [&] { return (long long)this->m_visibleObjects.size(); }},
            {"m_visibleObjects2", [&] { return (long long)this->m_visibleObjects2.size(); }},
            {"m_groups", [&] { return (long long)this->m_groups.size(); }},
            {"m_staticGroups", [&] { return (long long)this->m_staticGroups.size(); }},
            {"m_sections", [&] { return (long long)this->m_sections.size(); }},
            {"em.pulseVec", [&] { return EM(this) ? (long long)EM(this)->m_pulseEffectVector.size() : -1; }},
            {"em.pulseMap", [&] { return EM(this) ? (long long)EM(this)->m_pulseEffectMap.size() : -1; }},
            {"em.opacityMap", [&] { return EM(this) ? (long long)EM(this)->m_opacityEffectMap.size() : -1; }},
            {"em.touchToggle", [&] { return EM(this) ? (long long)EM(this)->m_unkVector1e0.size() : -1; }},
            {"em.countTrig", [&] { return EM(this) ? (long long)EM(this)->m_countTriggerActions.size() : -1; }},
            {"em.collisionTrig", [&] { return EM(this) ? (long long)EM(this)->m_unkVector230.size() : -1; }},
            {"em.toggleTrig", [&] { return EM(this) ? (long long)EM(this)->m_unkVector248.size() : -1; }},
            {"em.itemCount", [&] { return EM(this) ? (long long)EM(this)->m_itemCountMap.size() : -1; }},
            {"em.timerItem", [&] { return EM(this) ? (long long)EM(this)->m_timerItemMap.size() : -1; }},
            {"em.timerTrig", [&] { return EM(this) ? (long long)EM(this)->m_unkMap3f8.size() : -1; }},
        };
        const size_t NCNT = cnts.size();
        auto sig = [&](std::vector<uint64_t>& out) {
            out.assign(NM, 1469598103934665603ull);
            for (size_t k = 0; k < gbs.size(); ++k) {
                uint64_t h = 1469598103934665603ull;
                const uint8_t* p = (const uint8_t*)this + gbs[k].off;
                for (size_t j = 0; j < gbs[k].size; ++j) {
                    h ^= p[j]; h *= 1099511628211ull;
                }
                out[NOBJM + k] = h;
            }
            for (size_t k = 0; k < NCNT; ++k)
                out[NOBJM + gbs.size() + k] = (uint64_t)cnts[k].get();
            for (int i = 0; i < nobj; ++i) {
                auto* o = static_cast<GameObject*>(
                    arr->objectAtIndex((unsigned)i));
                auto mix = [](uint64_t& h, const void* p, size_t n) {
                    const uint8_t* b = (const uint8_t*)p;
                    for (size_t j = 0; j < n; ++j) {
                        h ^= b[j]; h *= 1099511628211ull;
                    }
                };
                const float px = o->getPositionX(), py = o->getPositionY(),
                            pr = o->getRotation();
                mix(out[0], &px, 4); mix(out[1], &py, 4); mix(out[2], &pr, 4);
                for (size_t k = 0; k < gos.size(); ++k)
                    mix(out[k + 3], (const uint8_t*)o + gos[k].off, gos[k].size);
            }
        };
        auto nameOf = [&](size_t k) -> const char* {
            if (k == 0) return "<x>";
            if (k == 1) return "<y>";
            if (k == 2) return "<rot>";
            if (k < NOBJM) return gos[k - 3].name;
            if (k < NOBJM + gbs.size()) return gbs[k - NOBJM].name;
            return cnts[k - NOBJM - gbs.size()].name;
        };
        auto sideOf = [&](size_t k) {
            return k < NOBJM ? "obj"
                 : k < NOBJM + gbs.size() ? "LAYER" : "COUNT";
        };
        snprintf(b, sizeof(b), "world: objects=%d scalarsPerObj=%zu N=%d",
                 nobj, psnap::goScalars().size(), N);
        writeResult(b);
        if (!nobj) { endSession("world"); return; }

        int held = 0;
        for (const auto& in : g_cfg.inputs)
            if (in.step <= g_ckptTick) held = in.down ? 1 : 0;
        secsolve::setSearching(true);
        int freeze = 0;
        std::vector<uint64_t> base, again, after;
        std::vector<uint8_t> snapBytes;

        // (1) The world right after a checkpoint restore
        secRestore(pl);
        {
            const double xf = m_player1 ? m_player1->getPositionX() : 0.0;
            for (int i = 0; i < 8; ++i) {
                secStep(held, dt);
                if (m_player1 && m_player1->getPositionX() != xf) break;
                ++freeze;
            }
        }
        secRestore(pl);
        for (int i = 0; i < freeze; ++i) secStep(held, dt);
        sig(base);
        psnap::capture(m_player1, this, snapBytes);

        for (int i = 0; i < N; ++i) secStep(held, dt);
        secRestore(pl);
        for (int i = 0; i < freeze; ++i) secStep(held, dt);
        sig(again);
        std::vector<uint8_t> stable(NM, 1);
        size_t nStable = 0;
        for (size_t k = 0; k < NM; ++k) {
            stable[k] = (again[k] == base[k]) ? 1 : 0;
            nStable += stable[k];
        }
        snprintf(b, sizeof(b),
                 "world: CP->run%d->CP stable=%zu / %zu members "
                 "(the rest move on the restore itself, so they are not compared)",
                 N, nStable, NM);
        writeResult(b);
        for (size_t k = 0; k < NM; ++k)
            if (!stable[k]) {
                snprintf(b, sizeof(b), "world:   unstable across CP [%s] %s",
                         sideOf(k), nameOf(k));
                writeResult(b);
            }
        if (!stable[0] || !stable[1] || !stable[2]) {
            std::vector<float> px((size_t)nobj), py((size_t)nobj);
            for (int i = 0; i < nobj; ++i) {
                auto* o = static_cast<GameObject*>(arr->objectAtIndex((unsigned)i));
                px[(size_t)i] = o->getPositionX();
                py[(size_t)i] = o->getPositionY();
            }
            secRestore(pl);
            for (int i = 0; i < freeze; ++i) secStep(held, dt);
            int moved = 0; double worst = 0.0; int worstIdx = -1;
            for (int i = 0; i < nobj; ++i) {
                auto* o = static_cast<GameObject*>(arr->objectAtIndex((unsigned)i));
                const double d = std::fabs(o->getPositionX() - px[(size_t)i])
                               + std::fabs(o->getPositionY() - py[(size_t)i]);
                if (d > 1e-4) {
                    ++moved;
                    if (d > worst) { worst = d; worstIdx = i; }
                }
            }
            snprintf(b, sizeof(b),
                     "world: objects at a different position after two CP restores"
                     " = %d / %d (worst %.2fpx, obj[%d])",
                     moved, nobj, worst, worstIdx);
            writeResult(b);
        }

        // (3) Run N ticks, then restore only the player with psnap = what the search does
        for (int i = 0; i < N; ++i) secStep(held, dt);
        psnap::restore(m_player1, this, snapBytes.data());
        g_injecting = true;
        this->handleButton(false, 1, true);
        g_injecting = false;
        g_held = 0;
        sig(after);
        size_t nDiff = 0, shown = 0;
        for (size_t k = 0; k < NM; ++k) {
            if (!stable[k] || after[k] == base[k]) continue;
            ++nDiff;
            if (shown < 40) {
                snprintf(b, sizeof(b), "world:   psnap differs [%s] %s",
                         sideOf(k), nameOf(k));
                writeResult(b);
                ++shown;
            }
        }
        snprintf(b, sizeof(b),
                 "world: CP->run%d->psnap differs=%zu / %zu stable members",
                 N, nDiff, nStable);
        writeResult(b);
        secsolve::setSearching(false);
        endSession("world");
    }

    bool snapSweep(PlayLayer* pl, int N, bool verbose) {
        using namespace secsolve;
        const float dt = g_cfg.fastdt;
        char b[256];
        if (verbose) {
            snprintf(b, sizeof(b),
                     "psnap: sizeof(PlayerObject)=%zu scalars=%zu copyBytes=%zu",
                     sizeof(PlayerObject), psnap::scalarCount(),
                     psnap::maskedBytes());
            writeResult(b);
        }
        // The held state at the section head (the value of pattern "keep")
        int held0 = 0;
        for (const auto& in : g_cfg.inputs)
            if (in.step <= g_ckptTick) held0 = in.down ? 1 : 0;

        struct Pat { const char* name; int period; int base; };
        // period 0 = constant (keep emitting base) / period k = toggle every k ticks
        const Pat PATS[] = {
            {"keep",    0, held0}, {"hold", 0, 1}, {"release", 0, 0},
            {"alt1",    1, 0},     {"alt2", 2, 0}, {"alt4",    4, 0},
            {"alt8",    8, 0},     {"alt16", 16, 0},
        };
        auto inputAt = [](const Pat& p, int i) -> int {
            if (p.period <= 0) return p.base;
            return ((i / p.period) & 1) ^ p.base;
        };
        const int DRIFTS_FULL[] = {0, 1, 2, 4, 8, 16, 32, 64, 128, 512, 2000, 10000};
        const int DRIFTS_QUICK[] = {0, 1, 8, 64, 512, 5000};
        const int* DRIFTS = verbose ? DRIFTS_FULL : DRIFTS_QUICK;
        const size_t NDRIFT = verbose ? sizeof(DRIFTS_FULL) / sizeof(int)
                                      : sizeof(DRIFTS_QUICK) / sizeof(int);
        bool allOk = true;

        const bool wasActive = g_active;
        secsolve::setSearching(true);
        std::vector<double> refY, refV;
        std::vector<uint8_t> refFlags;   // mode|grounded<<4|mini<<5|flip<<6
        std::vector<uint8_t> snapBytes;
        double sx = 0.0, sy = 0.0;
        int freeze = 0;
        auto flagsOf = [&](PlayerObject* p) -> uint8_t {
            return (uint8_t)((int)modeIdx(p)
                             | ((p->m_isOnGround ? 1 : 0) << 4)
                             | ((p->m_vehicleSize < 0.9f ? 1 : 0) << 5)
                             | ((p->m_isUpsideDown ? 1 : 0) << 6));
        };
        const Pat* pat = &PATS[0];

        auto refPass = [&](int n) -> int {
            secRestore(pl);
            if (freeze == 0) {
                const double xf = m_player1 ? m_player1->getPositionX() : 0.0;
                for (int i = 0; i < 8; ++i) {
                    secStep(inputAt(*pat, 0), dt);
                    if (m_player1 && m_player1->getPositionX() != xf) break;
                    ++freeze;
                }
                secRestore(pl);
            }
            for (int i = 0; i < freeze; ++i) secStep(inputAt(*pat, 0), dt);
            if (!m_player1) return -2;
            psnap::capture(m_player1, this, snapBytes);
            sx = m_player1->getPositionX();
            sy = m_player1->getPositionY();
            refY.assign((size_t)n, 0.0);
            refV.assign((size_t)n, 0.0);
            refFlags.assign((size_t)n, 0);
            for (int i = 0; i < n; ++i) {
                secStep(inputAt(*pat, i), dt);
                if (!m_player1) return -2;
                refY[(size_t)i] = m_player1->getPositionY();
                refV[(size_t)i] = (double)m_player1->m_yVelocity;
                refFlags[(size_t)i] = flagsOf(m_player1);
                if (m_player1->m_isDead) return i;
            }
            return -1;
        };

        auto rearmButton = [&]() {
            g_injecting = true;
            this->handleButton(false, 1, true);
            g_injecting = false;
            g_held = 0;
        };

        for (const Pat& P : PATS) {
            pat = &P;
            int n = N;
            int refDied = refPass(n);
            if (refDied >= 0) {
                n = refDied - 2;
                if (n < 4) {
                    snprintf(b, sizeof(b),
                             "psnap: pat=%s SKIP - the reference dies after "
                             "%d tick", P.name, refDied);
                    writeResult(b);
                    continue;
                }
                refDied = refPass(n);
            }
            if (refDied != -1) {
                snprintf(b, sizeof(b), "psnap: pat=%s SKIP - unstable reference",
                         P.name);
                writeResult(b);
                continue;
            }

            for (size_t di = 0; di < NDRIFT; ++di) {
                const int D = DRIFTS[di];
                secRestore(pl);
                for (int i = 0; i < freeze; ++i) secStep(inputAt(P, 0), dt);
                for (int d = 0; d < D; ++d) {
                    psnap::restore(m_player1, this, snapBytes.data());
                    rearmButton();
                    secStep(inputAt(P, 0), dt);
                    if (!m_player1 || m_player1->m_isDead) break;
                }
                const auto t0 = std::chrono::steady_clock::now();
                psnap::restore(m_player1, this, snapBytes.data());
                const double us = std::chrono::duration<double, std::micro>(
                    std::chrono::steady_clock::now() - t0).count();
                rearmButton();
                int firstBad = -1, died = -1, flagBad = -1;
                double maxDy = 0.0, maxDv = 0.0;
                for (int i = 0; i < n; ++i) {
                    secStep(inputAt(P, i), dt);
                    if (!m_player1) break;
                    if (m_player1->m_isDead) { died = i; break; }
                    const double dy = m_player1->getPositionY() - refY[(size_t)i];
                    const double dv = (double)m_player1->m_yVelocity - refV[(size_t)i];
                    if (std::fabs(dy) > std::fabs(maxDy)) maxDy = dy;
                    if (std::fabs(dv) > std::fabs(maxDv)) maxDv = dv;
                    if (firstBad < 0 && (std::fabs(dy) > 1e-4 || std::fabs(dv) > 1e-4))
                        firstBad = i;
                    if (flagBad < 0 && flagsOf(m_player1) != refFlags[(size_t)i])
                        flagBad = i;
                }
                const bool bad = (firstBad >= 0 || flagBad >= 0 || died >= 0);
                if (verbose || bad) {
                    snprintf(b, sizeof(b),
                             "psnap: pat=%-7s drift=%-5d n=%-4d restoreUs=%5.1f "
                             "firstDiff=%-4d flagDiff=%-4d maxDy=%9.4f "
                             "maxDvy=%8.4f died=%d",
                             P.name, D, n, us, firstBad, flagBad, maxDy, maxDv,
                             died);
                    writeResult(b);
                }
                if (bad) {
                    allOk = false;
                    break;   // this pattern split; larger drifts are not examined
                }
            }
            for (int withRearm = 0; withRearm < 2; ++withRearm) {
                secRestore(pl);
                for (int i = 0; i < freeze; ++i) secStep(inputAt(P, 0), dt);
                std::vector<uint8_t> s;
                int firstBad = -1, died = -1;
                double maxDy = 0.0, maxDv = 0.0;
                for (int i = 0; i < n; ++i) {
                    if (!m_player1) break;
                    psnap::capture(m_player1, this, s);
                    psnap::restore(m_player1, this, s.data());
                    if (withRearm) rearmButton();
                    secStep(inputAt(P, i), dt);
                    if (!m_player1) break;
                    if (m_player1->m_isDead) { died = i; break; }
                    const double dy = m_player1->getPositionY() - refY[(size_t)i];
                    const double dv = (double)m_player1->m_yVelocity - refV[(size_t)i];
                    if (std::fabs(dy) > std::fabs(maxDy)) maxDy = dy;
                    if (std::fabs(dv) > std::fabs(maxDv)) maxDv = dv;
                    if (firstBad < 0 && (std::fabs(dy) > 1e-4 || std::fabs(dv) > 1e-4))
                        firstBad = i;
                }
                const bool bad = (firstBad >= 0 || died >= 0);
                if (verbose || bad) {
                    snprintf(b, sizeof(b),
                             "psnap: pat=%-7s CHAIN rearm=%d n=%-4d "
                             "firstDiff=%-4d maxDy=%9.4f maxDvy=%8.4f died=%d",
                             P.name, withRearm, n, firstBad, maxDy, maxDv, died);
                    writeResult(b);
                }
                if (bad) allOk = false;
            }
            for (int k : {1, 2, 4, 8, 16, 32, 64, 128}) {
                if (k >= n) break;
                secRestore(pl);
                for (int i = 0; i < freeze; ++i) secStep(inputAt(P, 0), dt);
                std::vector<uint8_t> back;
                bool ok = true;
                for (int i = 0; i < n; ++i) {
                    if (i == n - k && m_player1) psnap::capture(m_player1, this, back);
                    secStep(inputAt(P, i), dt);
                    if (!m_player1 || m_player1->m_isDead) { ok = false; break; }
                }
                if (!ok || back.empty()) continue;
                // Here the world is at tick n and so is the player. Rewind x by k ticks.
                psnap::restore(m_player1, this, back.data());
                rearmButton();
                for (int j = n - k; j < n; ++j) secStep(inputAt(P, j), dt);
                if (!m_player1) break;
                const double dy = m_player1->getPositionY() - refY[(size_t)n - 1];
                const double dv = (double)m_player1->m_yVelocity - refV[(size_t)n - 1];
                const bool bad = (std::fabs(dy) > 1e-4 || std::fabs(dv) > 1e-4
                                  || m_player1->m_isDead);
                if (verbose || bad) {
                    snprintf(b, sizeof(b),
                             "psnap: pat=%-7s REWIND k=%-4d n=%-4d dy=%9.4f "
                             "dvy=%8.4f dead=%d",
                             P.name, k, n, dy, dv, m_player1->m_isDead ? 1 : 0);
                    writeResult(b);
                }
                if (bad) { allOk = false; break; }
            }
            for (int k : {1, 4, 16, 64}) {
                if (k >= n) break;
                secRestore(pl);
                for (int i = 0; i < freeze; ++i) secStep(inputAt(P, 0), dt);
                std::vector<uint8_t> deep;
                bool ok = true;
                for (int i = 0; i < n - k; ++i) {
                    secStep(inputAt(P, i), dt);
                    if (!m_player1 || m_player1->m_isDead) { ok = false; break; }
                }
                if (!ok) continue;
                psnap::capture(m_player1, this, deep);
                // This reproduces needWake: return only the world to the section head
                secRestore(pl);
                for (int i = 0; i < freeze; ++i) secStep(inputAt(P, 0), dt);
                psnap::restore(m_player1, this, deep.data());
                rearmButton();
                for (int j = n - k; j < n; ++j) secStep(inputAt(P, j), dt);
                if (!m_player1) break;
                const double dy = m_player1->getPositionY() - refY[(size_t)n - 1];
                const double dv = (double)m_player1->m_yVelocity - refV[(size_t)n - 1];
                const bool bad = (std::fabs(dy) > 1e-4 || std::fabs(dv) > 1e-4
                                  || m_player1->m_isDead);
                if (verbose || bad) {
                    snprintf(b, sizeof(b),
                             "psnap: pat=%-7s WAKE   k=%-4d n=%-4d dy=%9.4f "
                             "dvy=%8.4f dead=%d",
                             P.name, k, n, dy, dv, m_player1->m_isDead ? 1 : 0);
                    writeResult(b);
                }
                if (bad) { allOk = false; break; }
            }
            if (!allOk && !verbose) break;   // verdict only: settled at the first split
        }
        secsolve::setSearching(wasActive);
        return allOk;
    }

    void runSectionVerify(PlayLayer* pl) {
        using namespace secsolve;
        const float dt = g_cfg.fastdt;
        const long long base = g_ckptTick - 3;   // margin on both sides so secoff can act
        // seq[j] = pressed state of the plain run at tick base+j
        std::vector<uint8_t> seq((size_t)g_horizon + 12, 0);
        int held = 0;
        size_t idx = 0;
        for (long long t = 0; t <= base + (long long)seq.size(); ++t) {
            while (idx < g_cfg.inputs.size() && g_cfg.inputs[idx].step <= t) {
                held = g_cfg.inputs[idx].down ? 1 : 0;
                ++idx;
            }
            if (t >= base && (size_t)(t - base) < seq.size())
                seq[(size_t)(t - base)] = (uint8_t)held;
        }
        secsolve::setSearching(true);
        const size_t at0 = (size_t)(g_ckptTick + 1 - base);   // = 4
        secRestore(pl);
        int freeze = 0;
        {
            const double xf = m_player1 ? m_player1->getPositionX() : 0.0;
            for (int i = 0; i < 8; ++i) {
                secStep(seq[at0], dt);
                if (m_player1 && m_player1->getPositionX() != xf) break;
                ++freeze;
            }
        }
        secRestore(pl);
        for (int i = 0; i < freeze; ++i) secStep(seq[at0], dt);
        const double x0 = m_player1 ? m_player1->getPositionX() : -1;
        const double y0 = m_player1 ? m_player1->getPositionY() : -1;
        int diedAt = -1;
        for (int i = 0; i < g_horizon; ++i) {
            const size_t si = (size_t)std::max(
                0, std::min((int)seq.size() - 1, (int)at0 + 1 + i + g_off));
            secStep(seq[si], dt);
            if ((g_log || i < 12) && m_player1) {
                char sb[192];
                snprintf(sb, sizeof(sb),
                         "secverify_step: i=%d in=%d x=%.4f y=%.4f vy=%.4f "
                         "mode=%s dead=%d",
                         i, (int)seq[si], m_player1->getPositionX(),
                         m_player1->getPositionY(),
                         (double)m_player1->m_yVelocity, modeStr(m_player1),
                         m_player1->m_isDead ? 1 : 0);
                writeResult(sb);
            }
            if (m_player1 && m_player1->m_isDead) { diedAt = i; break; }
        }
        const double x1 = m_player1 ? m_player1->getPositionX() : -1;
        const double y1 = m_player1 ? m_player1->getPositionY() : -1;
        secsolve::setSearching(false);
        char b[256];
        snprintf(b, sizeof(b),
                 "secverify: restoreX=%.3f restoreY=%.3f -> x=%.3f y=%.3f "
                 "diedAt=%d horizon=%d freeze=%d endTick=%lld",
                 x0, y0, x1, y1, diedAt, g_horizon, freeze,
                 (long long)(g_ckptTick + 1 + (diedAt < 0 ? g_horizon : diedAt)));
        writeResult(b);
        endSession("secverify");
    }

    secsolve::SecTask runSectionSolve(PlayLayer* pl) {
        using namespace secsolve;
        if (g_memAt >= 0) {
            char mb[220];
            for (auto& m : psnap::scalars()) {
                if ((long long)(m.off + m.size) <= g_memAt - 48) continue;
                if ((long long)m.off >= g_memAt + 48) continue;
                snprintf(mb, sizeof(mb), "secmem: off=%zu size=%zu %s",
                         m.off, m.size, m.name);
                writeResult(mb);
            }
        }
        const float dt = g_cfg.fastdt;
        const auto t0 = std::chrono::steady_clock::now();
        const long long savedTick = g_tick;
        const size_t savedInput = g_nextInput;

        g_snapOn = false;
        g_worldOn = false;
        g_movSet.clear();
        // cfg secsnapem / secsnapemfrom: whether this search's snapshots carry the effect manager
        psnap::g_snapEMActive = psnap::g_snapEM && (long long)g_ckptTick >= psnap::g_snapEMFrom;
        if (g_snapMode == 1) {
            g_snapOn = (psnap::scalarCount() > 0);
        } else if (g_snapMode == 3) {
            // Forced. Skip the sweep and run with wsnap (for diagnosis)
            g_snapOn = (psnap::scalarCount() > 0);
            if (g_snapOn) {
                movingObjectsInSection(pl, std::min(400, g_horizon), &g_movSet);
                g_worldOn = !g_movSet.empty();
            }
        } else if (g_snapMode == 4) {
            g_snapOn = (psnap::scalarCount() > 0);
            if (g_snapOn && this->m_objects) {
                for (unsigned i = 0; i < this->m_objects->count(); ++i)
                    g_movSet.push_back(static_cast<GameObject*>(
                        this->m_objects->objectAtIndex(i)));
                g_worldOn = true;
            }
        } else if (g_snapMode == 2) {
            const auto q0 = std::chrono::steady_clock::now();
            const bool gated = g_verifyEvery <= 0;
            g_snapOn = gated ? snapSweep(pl, std::min(400, g_horizon), false)
                             : (psnap::scalarCount() > 0);
            int moved = -1;
            int movPortals = 0;
            g_wsnapHeld = 0;
            if (g_snapOn && gated) {
                moved = movingObjectsInSection(pl, std::min(400, g_horizon),
                                               &g_movSet, &movPortals);
                g_movSet.clear();
                if (moved > 0) g_snapOn = false;
            } else if (g_snapOn) {
                double wLo = 0.0, wHi = 0.0;
                const bool byTrig = g_wsnapSet == 2 || g_wsnapSet == 3 || !g_wsnapGroups.empty();
                const int mv = movingObjectsInSection(pl, std::min(400, g_horizon),
                                                      g_wsnapAuto ? &g_movSet : nullptr,
                                                      &movPortals, g_wsnapSet == 1 || byTrig,
                                                      &wLo, &wHi, g_wsnapSet == 4 || g_wsnapSet == 5,
                                                      g_wsnapSet == 5);
                if (g_wsnapAuto && byTrig && movPortals == 0)
                    wsnapTriggerSet(g_movSet, wLo, wHi, g_wsnapSet == 3);
                if (movPortals > 0) {
                    g_movSet.clear();
                    g_snapOn = false;
                    if (g_cap > 120) {
                        char cb[120];
                        snprintf(cb, sizeof(cb),
                                 "secsolve: fell back to CP, so cap %zu -> 120", g_cap);
                        writeResult(cb);
                        g_cap = 120;
                    }
                } else if (g_wsnapAuto) {
                    const bool lieSkip = g_lieSkipPsnap && g_wsnapGate && !g_wsnapForce && g_secRung
                                         && liedWindow(secsolve::g_startTick);
                    const bool hold = g_wsnapGate && !g_wsnapForce && !lieSkip && g_secRung
                                      && !g_movSet.empty() && g_movSet.size() <= g_maxMoving;
                    if (!hold) moved = mv;
                    char wb[240];
                    if (hold) {
                        g_wsnapHeld = g_movSet.size();
                        snprintf(wb, sizeof(wb), "secsolve: wsnap set %zu objects (secwsnapset=%d) - held "
                                 "back (cfg secwsnapgate): psnap alone first, the set only if that finds "
                                 "nothing", g_movSet.size(), g_wsnapSet);
                        g_movSet.clear();
                    } else if (!g_movSet.empty() && g_movSet.size() <= g_maxMoving) {
                        g_worldOn = true;
                        if (lieSkip)
                            snprintf(wb, sizeof(wb), "secsolve: wsnap set %zu objects (secwsnapset=%d) - "
                                     "put back from the start (cfg seclieskippsnap): this window's plain "
                                     "snapshot search parted from the game before", g_movSet.size(), g_wsnapSet);
                        else
                            snprintf(wb, sizeof(wb), "secsolve: wsnap set %zu objects (secwsnapset=%d) - "
                                     "put back with the player", g_movSet.size(), g_wsnapSet);
                    } else if (g_movSet.empty()) {
                        snprintf(wb, sizeof(wb), "secsolve: wsnap set empty (secwsnapset=%d) - "
                                 "the search runs on psnap alone", g_wsnapSet);
                    } else {
                        snprintf(wb, sizeof(wb), "secsolve: wsnap set %zu objects is over secmaxmov %zu "
                                 "(secwsnapset=%d) - NOT put back, the search runs on psnap alone",
                                 g_movSet.size(), g_maxMoving, g_wsnapSet);
                        g_movSet.clear();
                    }
                    writeResult(wb);
                }
            }
            const double qms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - q0).count();
            char qb[256];
            snprintf(qb, sizeof(qb),
                     "secsolve: snap qualify %s (%.0f ms, movingObjects=%d, "
                     "movingPortals=%d, verifyEvery=%d) - branching with %s",
                     g_snapOn ? "PASS" : "FAIL", qms, moved, movPortals,
                     g_verifyEvery,
                     !g_snapOn ? "CheckpointObject"
                               : (moved > 0 ? "psnap + a checked correction"
                                            : "psnap (3KB/1.4us)"));
            writeResult(qb);
        }
        const bool cpRefused = !g_snapOn && g_secRung && !g_cfg.secCpFallback;
        if (cpRefused)
            writeResult("secsolve: not searched - the snapshot cannot branch in this window and "
                        "cfg seccpfallback=0 keeps the loop's searches off checkpoints");
        g_maxYAuto = 0.0;
        if (g_maxY <= 0.0 && this->m_objects && m_player1) {
            const double x0 = m_player1->getPositionX();
            const double x1 = x0 + (double)g_horizon * 5.5 + 300.0;
            double top = 0.0;
            for (unsigned i = 0; i < this->m_objects->count(); ++i) {
                auto* o = static_cast<GameObject*>(
                    this->m_objects->objectAtIndex(i));
                const int ty = (int)o->m_objectType;
                if (ty == 7 || ty == 20) continue;      // decoration / trigger
                const double ox = o->getPositionX();
                if (ox < x0 - 300.0 || ox > x1) continue;
                top = std::max(top, (double)o->getPositionY());
            }
            if (top > 0.0) g_maxYAuto = top + 600.0;
        }
        const double killAboveY = g_maxY > 0.0 ? g_maxY : g_maxYAuto;

        secsolve::setSearching(true);
        g_noKill = g_killOverride < 0 ? g_snapOn : (g_killOverride == 1);
        g_died = false;
        g_nodes.clear();
        releaseAnchors();
        secsolve::g_keepDropped = 0;   // cfg seckeepsize's count, this search
        g_nodes.push_back({-1, (uint8_t)(g_headHeld ? 1 : 0), 0, 0.f, 0.f, 0.f, 0,
                           (uint8_t)(m_player1 && m_player1->m_vehicleSize < 0.9f ? 1 : 0),
                           (uint8_t)(secDualP2() ? 1 : 0), secDualY2()});
        g_dash.assign(1, secsolve::DashState{});  // section head (input unused)
        g_vy.assign(1, m_player1 ? m_player1->m_yVelocity : 0.0);
        g_accel.assign(1, m_player1 ? m_player1->m_accelerationOrSpeed : 0.0);
        g_pad.assign(1, m_player1 && m_player1->m_touchedPad ? 1 : 0);
        secsolve::g_dash2.clear();
        secsolve::g_vy2.clear();
        secsolve::g_accel2.clear();
        secsolve::g_pad2.clear();
        secPushP2();                      // its dash is retaken below, as player 1's is
        if (secsolve::g_headAccel && !secsolve::g_headPlain.empty()) {
            const size_t one = sizeof(PlayerObject) + psnap::kExtra;
            const uint8_t* hp = secsolve::g_headPlain.data();
            std::memcpy(&g_accel[0], hp + offsetof(PlayerObject, m_accelerationOrSpeed), sizeof(double));
            g_pad[0] = hp[offsetof(PlayerObject, m_touchedPad)] ? 1 : 0;
            const size_t p2at = one + psnap::layerBytes();
            if (secP2() && !secsolve::g_accel2.empty() && secsolve::g_headPlain.size() >= p2at + one) {
                std::memcpy(&secsolve::g_accel2[0],
                            hp + p2at + offsetof(PlayerObject, m_accelerationOrSpeed), sizeof(double));
                secsolve::g_pad2[0] = hp[p2at + offsetof(PlayerObject, m_touchedPad)] ? 1 : 0;
            }
        }
        g_spine = g_spineOn ? 0 : -1;
        g_spineNext = -1;

        int freeze = 0;
        {
            secRestoreFrom(pl, g_ckpt, g_headHeld);
            const double x0 = m_player1 ? m_player1->getPositionX() : 0.0;
            const double y0 = m_player1 ? m_player1->getPositionY() : 0.0;
            for (int i = 0; i < 8; ++i) {
                secStep(g_headHeld, dt);
                if (m_player1 && (m_player1->getPositionX() != x0
                                  || m_player1->getPositionY() != y0)) break;
                ++freeze;
            }
        }
        secRestoreFrom(pl, g_ckpt, g_headHeld);
        for (int f = 0; f < freeze; ++f) secStep(g_headHeld, dt);
        secCaptureDash(g_dash[0]);
        if (auto* q = secP2()) secCaptureDash(secsolve::g_dash2[0], q);
        // Baseline for the counter delta (Node::cnt). Taken once in the section-head world.
        secsolve::g_cntBase = secsolve::countSum(this);

        g_cps.assign(1, g_ckpt);          // nodes[0]'s checkpoint is the section checkpoint itself
        std::vector<std::vector<uint8_t>> snaps;
        std::unordered_map<int, std::vector<uint8_t>> anchorSnaps;
        int cpChecks[5] = {0, 0, 0, 0, 0};
        std::vector<uint8_t> cpLoad, cpAfter;
        auto cpTakeLoad = [&]() {
            if (psnap::g_cpCheck) psnap::captureRaw(m_player1, this, cpLoad);
        };
        auto cpReport = [&](int site, const char* name, long long at,
                            const std::vector<uint8_t>& origin) {
            if (!psnap::g_cpCheck || origin.empty() || cpLoad.empty() || cpChecks[site] >= 8) return;
            ++cpChecks[site];
            psnap::captureRaw(m_player1, this, cpAfter);
            writeResult(psnap::cpCheckLine(name, at, origin, cpLoad, cpAfter, secsolve::g_cpPlayer));
        };
        std::vector<psnap::EMSnap> pulses;   // the effect manager per node (psnap::EMSnap)
        std::vector<std::unique_ptr<GJGameState>> states;
        std::vector<std::unique_ptr<GJGameState>> statePool;
        auto newState = [&statePool]() {
            if (statePool.empty()) return std::make_unique<GJGameState>();
            std::unique_ptr<GJGameState> s = std::move(statePool.back());
            statePool.pop_back();
            return s;
        };
        // The player-side "already touched" bookkeeping (orbs/pads/slopes)
        std::vector<std::unique_ptr<psnap::Touch>> touches;
        std::vector<std::vector<uint8_t>> acts;
        std::vector<uint64_t> actSigs(1, 0);
        std::unordered_set<uint64_t> actSigSeen;
        long long originChecks = 0, originUsed = 0, originLost = 0;
        int actSelfCheck = -1;
        std::vector<std::vector<uint8_t>> worlds;
        const int snapFreeze = g_snapOn ? 0 : freeze;
        const bool keepSnaps = g_snapOn || g_overlay != 0;
        if (keepSnaps) {
            secRestoreFrom(pl, g_ckpt, g_headHeld);
            for (int i = 0; i < freeze; ++i) secStep(g_headHeld, dt);
            cpTakeLoad();
            // The head's player is the plain flight's, not what the load left (g_cpPlayer).
            if (secsolve::g_cpPlayer && !secsolve::g_headPlain.empty())
                psnap::restore(m_player1, this, secsolve::g_headPlain.data());
            if (secsolve::g_forceP1Size >= 0 && m_player1
                && (m_player1->m_vehicleSize < 0.9f) != (secsolve::g_forceP1Size == 1)) {
                m_player1->togglePlayerScale(secsolve::g_forceP1Size == 1, true);
                writeResult(std::string("secsolve: player 1 forced to ")
                            + (secsolve::g_forceP1Size == 1 ? "mini" : "normal")
                            + " size at the head (cfg secforcesize) - a probe, not a route");
            }
            cpReport(0, "head", g_ckptTick, secsolve::g_headPlain);
            snaps.resize(1);
            pulses.resize(1);
            states.resize(1);
            touches.resize(1);
            acts.resize(1);
            states[0] = std::make_unique<GJGameState>();
            touches[0] = std::make_unique<psnap::Touch>();
            psnap::capture(m_player1, this, snaps[0]);
            psnap::captureEM(this, pulses[0]);
            psnap::captureState(this, *states[0]);
            psnap::captureTouch(m_player1, *touches[0], this);
            {
                const double hx = m_player1 ? m_player1->getPositionX() : 0.0;
                psnap::buildActWindow(this, hx - 300.0, hx + 4.0 * (double)g_horizon + 300.0);
                actSigs[0] = psnap::captureActSig(acts[0]);
                actSigSeen.insert(actSigs[0]);
                if (m_player1)
                    actSelfCheck = psnap::actKeySelfCheck(
                        acts[0], keyOf(m_player1->getPositionY(), m_player1->m_yVelocity,
                                       (int)modeIdx(m_player1),
                                       m_player1->m_vehicleSize < 0.9f ? 1 : 0,
                                       m_player1->m_isUpsideDown ? 1 : 0, g_headHeld,
                                       hx, m_player1->m_isDashing ? 1 : 0, 0));
            }
            if (g_worldOn) {
                worlds.resize(1);
                psnap::captureWorld(g_movSet, worlds[0]);
            }
        }
        // cfg secbound: the level's box, from where every object stands at the section head.
        g_boundSkips = 0;
        g_shaderSkips = 0;
        if (g_shaderSig) writeResult("shadersig: search start " + shaderSig(this));
        g_frames = 0;
        g_frameGapMs = 0.0;
        g_overlayMs = 0.0;
        g_outsideMs = 0.0;
        g_sliceEnded = false;
        if (g_boundOn && this->m_objects) {
            double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
            for (unsigned i = 0; i < this->m_objects->count(); ++i) {
                auto* o = static_cast<GameObject*>(this->m_objects->objectAtIndex(i));
                if (!o) continue;
                x0 = std::min(x0, o->m_positionX); x1 = std::max(x1, o->m_positionX);
                y0 = std::min(y0, o->m_positionY); y1 = std::max(y1, o->m_positionY);
            }
            g_boxX0 = x0 - kBoundMargin; g_boxX1 = x1 + kBoundMargin;
            g_boxY0 = y0 - kBoundMargin; g_boxY1 = y1 + kBoundMargin;
        }
        struct CoinBook {
            std::vector<long long> pickup = solver::g_coinPickupTick, gd = solver::g_coinGdTick;
            std::vector<uint8_t> said = solver::g_coinLiveSaid;
            std::map<int, int> items = solver::g_itemCounts;
            bool missFired = solver::g_coinMissFired;
            float prevX = solver::g_prevTickX;
            bool armed = true;   // cfg secbookfirst: put back before the reset, not at the end
            void restore() {
                solver::g_coinPickupTick = pickup; solver::g_coinGdTick = gd;
                solver::g_coinLiveSaid = said; solver::g_itemCounts = items;
                solver::g_coinMissFired = missFired; solver::g_prevTickX = prevX;
            }
            ~CoinBook() {
                if (armed) restore();
            }
        } coinBook;
        const bool coinsOn = g_secCoins && g_cfg.coinRoute && !solver::g_coins.empty()
                             && solver::g_coins.size() <= 32 && m_player1 && this->m_objects;
        std::vector<GameObject*> coinObj;
        std::vector<uint8_t> coinFinal;   // passing coin c forward ends its chance (set below)
        std::vector<uint32_t> coinBits(1, 0u);
        long long coinMissBranches = 0;
        long long coinLessLeaves = 0;     // cfg dpseccoinrung: leaves without the owed coin
        // The gameplay frame at the head, and the rotations run by then (see `forward` below).
        int coinFrame = -1;
        long long coinRotAt = 0;
        if (coinsOn) {
            coinObj.assign(solver::g_coins.size(), nullptr);
            for (unsigned i = 0; i < this->m_objects->count(); ++i) {
                auto* o = static_cast<GameObject*>(this->m_objects->objectAtIndex(i));
                if (!o) continue;
                for (size_t c = 0; c < solver::g_coins.size(); ++c)
                    if (solver::g_coins[c].uid == o->m_uniqueID) coinObj[c] = o;
            }
            const float hx = m_player1->getPositionX();
            const AnchorRow* headRow = anchors::row(secsolve::g_startTick);
            const long long headT = secsolve::g_startTick;
            const auto byHead = [headT](const std::vector<long long>& v, size_t c) {
                return c < v.size() && v[c] >= 0 && v[c] <= headT;
            };
            int lateTicks = 0;
            coinFinal.assign(coinObj.size(), 1);
            uint32_t head = 0;
            for (size_t c = 0; c < coinObj.size(); ++c) {
                if (std::find(solver::g_coinNoMiss.begin(), solver::g_coinNoMiss.end(),
                              solver::g_coins[c].uid) != solver::g_coinNoMiss.end())
                    coinFinal[c] = 0;
                if ((int)c == secsolve::g_rungCoin) coinFinal[c] = 1;
                const bool taken = (headRow && c < 8 && ((headRow->coins >> c) & 1))
                                   || byHead(solver::g_coinGdTick, c)
                                   || byHead(solver::g_coinPickupTick, c);
                if (!taken && ((c < solver::g_coinGdTick.size() && solver::g_coinGdTick[c] > headT)
                               || (c < solver::g_coinPickupTick.size()
                                   && solver::g_coinPickupTick[c] > headT)))
                    ++lateTicks;
                const bool moveAhead = c < solver::g_coinMoveX.size()
                                       && solver::g_coinMoveX[c] > -1e8f
                                       && hx <= solver::g_coinMoveX[c];
                bool passed = coinObj[c] && coinFinal[c] && !moveAhead
                              && !m_player1->m_isGoingLeft && !m_player1->m_isSideways
                              && hx > coinObj[c]->getObjectRect().getMaxX() + 15.f;
                if (passed && secsolve::g_rungCoinOff && (int)c == secsolve::g_rungCoin
                    && coinObj[c]->m_isGroupDisabled)
                    passed = false;
                if (!coinObj[c] || taken || passed) head |= 1u << c;
            }
            coinBits[0] = head;
            coinFrame = headRow ? headRow->gframe : g_gameFrame;
            coinRotAt = g_rotGameplayCalls;
            int left = 0;
            for (size_t c = 0; c < coinObj.size(); ++c)
                if (!(head & (1u << c))) ++left;
            char cb[256];
            snprintf(cb, sizeof(cb), "secsolve: coins - %zu in the level, %d still to take past the "
                     "head (x=%.0f); the search takes them itself (GD collects none in practice "
                     "mode); frame %d camera %.1f; %d coin(s) touched only after the head",
                     coinObj.size(), left, (double)hx, coinFrame,
                     (double)this->m_gameState.m_cameraAngle, lateTicks);
            writeResult(cb);
        }
        long long rephases = 0;
        bool needWake = false;   // previous branch died -> wake the level before next expansion
        auto rephase = [&]() {
            if (!g_snapOn) return;
            secRestoreFrom(pl, g_ckpt, g_headHeld);
            for (int i = 0; i < freeze; ++i) secStep(g_headHeld, dt);
            ++rephases;
        };
        std::vector<int> cur{0}, nxt;
        std::unordered_set<long long> seen;
        long long restores = 0, steps = 0, cpMade = 0, cpPeak = 0;
        int foundLeaf = -1;
        double foundX = 0.0;
        int foundDepth = 0;
        int deepest = 0;
        const char* stopWhy = "none";
        secsolve::g_nonFinite = 0;   // objects handed to re-bucketing at a non-finite position
        // Results of the leaf cross-check (filled by evalLeaf below). Used in the report.
        g_leafVerified = false;
        g_leafDeadAt = -1;
        double verifyX = 0.0, verifyY = 0.0, verifyVy = 0.0;
        int splitAt = -1;
        double splitNodeX = 0, splitRepX = 0, splitNodeY = 0, splitRepY = 0;
        double splitNodeVy = 0, splitRepVy = 0;
        int graceOk = -1, graceDeadAt = -1;
        long long doomedLeaves = 0;      // number of exits discarded as dead ends
        long long unverLeaves = 0;       // ...and, in a rung, as not reproduced by the replay
        double maxVerifyDrift = 0.0;     // size of psnap's lie (max diff in the cross-check pass)

        auto releaseCp = [&](int ni) {
            if (ni <= 0) return;                       // the section checkpoint is never released
            if (keepSnaps) {
                if ((size_t)ni < snaps.size())
                    std::vector<uint8_t>().swap(snaps[(size_t)ni]);
                if ((size_t)ni < pulses.size()) {
                    psnap::EMSnap tmp;
                    std::swap(pulses[(size_t)ni], tmp);
                }
                if ((size_t)ni < states.size() && states[(size_t)ni])
                    statePool.push_back(std::move(states[(size_t)ni]));
                if ((size_t)ni < touches.size()) touches[(size_t)ni].reset();
                if ((size_t)ni < worlds.size())
                    std::vector<uint8_t>().swap(worlds[(size_t)ni]);
                if ((size_t)ni < acts.size() && !g_anchors.count(ni))
                    std::vector<uint8_t>().swap(acts[(size_t)ni]);
                if (g_snapOn) return;                  // the psnap path holds no checkpoint
            }
            if ((size_t)ni >= g_cps.size()) return;
            if (g_cps[(size_t)ni]) { g_cps[(size_t)ni]->release(); g_cps[(size_t)ni] = nullptr; }
        };

        // Is the state controllable (used for exit survivability)
        auto controllable = [](PlayerObject* p) {
            if (!p) return false;
            const int md = modeIdx(p);
            // ship / UFO / wave / swing accept input even in the air
            if (md == 1 || md == 3 || md == 4 || md == 7) return true;
            return p->m_isOnGround != 0;
        };
        auto flyMode = [](PlayerObject* p) {
            const int md = p ? modeIdx(p) : -1;
            return md == 1 || md == 3 || md == 4 || md == 7;
        };
        auto dashing = [this]() {
            if (m_player1 && m_player1->m_isDashing) return true;
            return m_gameState.m_isDualMode && m_player2 && m_player2 != m_player1
                   && m_player2->m_isDashing;
        };

        size_t leafSeqN = 0, leafSeqCap = 0, leafPathN = 0, leafPathCap = 0;
        auto evalLeaf = [&](int leaf) -> int {
            g_leafVerified = false;
            g_leafDeadAt = -1;
            splitAt = -1;
            graceOk = -1;
            graceDeadAt = -1;
            verifyX = verifyY = verifyVy = 0.0;
            std::vector<uint8_t> seq;
            std::vector<int> path;
            for (int i = leaf; i > 0; i = g_nodes[(size_t)i].parent) {
                seq.push_back(g_nodes[(size_t)i].in);
                path.push_back(i);
            }
            std::reverse(seq.begin(), seq.end());
            std::reverse(path.begin(), path.end());
            leafSeqN = seq.size(); leafSeqCap = seq.capacity();
            leafPathN = path.size(); leafPathCap = path.capacity();
            secRestoreFrom(pl, g_ckpt, g_headHeld);
            for (int f = 0; f < freeze; ++f) secStep(g_headHeld, dt);
            cpTakeLoad();
            if (secsolve::g_cpPlayer && keepSnaps && !snaps.empty() && !snaps[0].empty())
                psnap::restore(m_player1, this, snaps[0].data());
            cpReport(1, "leaf", (long long)seq.size(), secsolve::g_headPlain);
            bool dead = false;
            for (size_t i = 0; i < seq.size(); ++i) {
                g_died = false;
                secStepBegin(pl);
                secStep((int)seq[i], dt); ++steps;
                if (!m_player1 || m_player1->m_isDead || g_died) {
                    dead = true; g_leafDeadAt = (int)i + 1; break;
                }
                if (splitAt < 0) {
                    const auto& nd = g_nodes[(size_t)path[i]];
                    // The physics position, as the node recorded it (psnap::physPosition).
                    const cocos2d::CCPoint at = psnap::physPosition(m_player1, this);
                    const double rx = at.x;
                    const double ry = at.y;
                    const double rv = (double)m_player1->m_yVelocity;
                    if (std::fabs(rx - (double)nd.x) > 0.02
                        || std::fabs(ry - (double)nd.y) > 0.02
                        || std::fabs(rv - (double)nd.vy) > 0.02) {
                        splitAt = (int)i + 1;
                        splitNodeX = nd.x; splitRepX = rx;
                        splitNodeY = nd.y; splitRepY = ry;
                        splitNodeVy = nd.vy; splitRepVy = rv;
                    }
                }
            }
            if (dead || !m_player1) return 1;
            {
                const cocos2d::CCPoint at = psnap::physPosition(m_player1, this);
                verifyX = at.x;
                verifyY = at.y;
            }
            verifyVy = (double)m_player1->m_yVelocity;
            if (!reachedGoal(verifyX, verifyY, (int)seq.size())) return 1;
            g_leafVerified = true;
            if (g_grace <= 0) return 0;
            static const int kGracePeriods[] = {0, -1, 2, 3, 4, 6, 8, 12, 20, 40};
            CheckpointObject* lcp = markCheckpointAtPhys(pl);
            if (!lcp) return 0;
            lcp->retain();
            // ...and the player at the exit, for the loads of lcp below (g_cpPlayer).
            std::vector<uint8_t> lcpSnap, lcpOrigin;
            if (secsolve::g_cpPlayer && keepSnaps) psnap::captureRaw(m_player1, this, lcpSnap);
            if (psnap::g_cpCheck) psnap::captureRaw(m_player1, this, lcpOrigin);
            graceOk = 0;
            for (size_t pi = 0;
                 pi < sizeof(kGracePeriods) / sizeof(int) && graceOk == 0; ++pi) {
                const int per = kGracePeriods[pi];
                if (pi) {
                    secRestoreFrom(pl, lcp, 0); ++restores;
                    for (int f = 0; f < freeze; ++f) secStep(0, dt);
                    cpTakeLoad();
                    if (!lcpSnap.empty()) psnap::restore(m_player1, this, lcpSnap.data());
                    cpReport(2, "exit", (long long)pi, lcpOrigin);
                }
                bool gdead = false;
                for (int i = 0; i < g_grace; ++i) {
                    const int in = per == 0 ? 0 : per < 0 ? 1
                                 : ((i % per) == 0 ? 1 : 0);
                    g_died = false;
                    secStep(in, dt); ++steps;
                    if (!m_player1 || m_player1->m_isDead || g_died) {
                        gdead = true;
                        if (pi == 0) graceDeadAt = i + 1;
                        break;
                    }
                    if ((controllable(m_player1) || (secsolve::g_graceDash && dashing()))
                        && (g_graceFly <= 0 || !flyMode(m_player1) || i + 1 >= g_graceFly)) {
                        graceOk = 1;
                        break;
                    }
                }
                if (!gdead && graceOk == 0) graceOk = 1;
            }
            if (graceOk == 1 && g_leafLife > 0) {
                int life = 0, lifeAt = -1;
                for (size_t pi = 0; pi < sizeof(kGracePeriods) / sizeof(int); ++pi) {
                    const int per = kGracePeriods[pi];
                    secRestoreFrom(pl, lcp, 0); ++restores;
                    for (int f = 0; f < freeze; ++f) secStep(0, dt);
                    if (!lcpSnap.empty()) psnap::restore(m_player1, this, lcpSnap.data());
                    int i = 0;
                    for (; i < g_leafLife; ++i) {
                        const int in = per == 0 ? 0 : per < 0 ? 1 : ((i % per) == 0 ? 1 : 0);
                        g_died = false;
                        secStep(in, dt); ++steps;
                        if (!m_player1 || m_player1->m_isDead || g_died) break;
                    }
                    if (i > life) { life = i; lifeAt = per; }
                }
                char lb2[96];
                snprintf(lb2, sizeof(lb2), "secleaf: depth=%zu life=%d/%d line=%d",
                         seq.size(), life, g_leafLife, lifeAt);
                writeResult(lb2);
            }
            lcp->release();
            return graceOk == 0 ? 2 : 0;
        };

        int layDead = 0, layDup = 0, layCap = 0;
        long long capTotal = 0;   // nodes the cap dropped over the whole search (all layers)
        double layMaxX = 0.0, layMinY = 0.0, layMaxY = 0.0, layMinX = 0.0;
        double deadMaxX = 0.0;
        auto spinePlanHeld = [this](int depth) {
            const long long tHere = g_ckptTick + depth + g_spineOff;
            int held = 0;
            for (const auto& in : g_cfg.inputs) {
                if (in.step > tHere) break;
                held = in.down ? 1 : 0;
            }
            return held;
        };
        const bool kLimits = !Mod::get()->getSettingValue<bool>("go-crazy") && dpsolve::g_kLimitWall < 0;
        const int kPulse = (int)Mod::get()->getSettingValue<int64_t>("human-min-pulse");
        const int64_t kCps = Mod::get()->getSettingValue<int64_t>("max-cps");
        const int kGap = kCps > 0 ? (int)((240 + kCps - 1) / kCps) : 0;
        long long kHeadChange = -100000, kHeadPress = -100000;   // the flown plan's, up to the head
        {
            bool down = false;
            for (const auto& in : g_cfg.inputs) {
                if (in.step > g_ckptTick) break;
                if (in.down == down) continue;
                down = in.down;
                kHeadChange = in.step;
                if (down) kHeadPress = in.step;
            }
        }
        auto kHumanBlocks = [&](int ni, int branch, int depth) {
            const long long tChild = g_ckptTick + depth;
            if (!kLimits || tChild < dpsolve::g_kHumanFrom) return false;
            const int level = g_nodes[(size_t)ni].in;
            if (branch == level) return false;
            // Ticks the parent's level has lasted, and the last press, walking back to the head.
            long long lastChange = kHeadChange, lastPress = kHeadPress;
            long long t = tChild - 1;
            for (int n = ni; n >= 0 && tChild - t <= std::max(kPulse, kGap); --t) {   // older: no matter
                const int p = g_nodes[(size_t)n].parent;
                if (p >= 0 && g_nodes[(size_t)p].in != g_nodes[(size_t)n].in) {
                    if (lastChange < t) lastChange = t;
                    if (g_nodes[(size_t)n].in && lastPress < t) lastPress = t;
                }
                n = p;
            }
            if (kPulse > 1 && tChild - lastChange < kPulse) return true;
            return branch == 1 && kGap > 1 && tChild - lastPress < kGap;
        };
        secsolve::g_psnapLied = false;
        secsolve::g_spineLostT = -1;
        auto spineLied = [&](int depth) {
            if (!g_snapOn || secsolve::g_spineUntil <= 0 || g_spine < 0 || secsolve::g_psnapLied)
                return;
            if (g_spineNext >= 0 && std::find(nxt.begin(), nxt.end(), g_spineNext) != nxt.end())
                return;
            const long long t = (long long)g_ckptTick + 1 + depth;
            if (t >= secsolve::g_spineUntil - secsolve::kSpineSlack) return;
            secsolve::g_psnapLied = true;
            secsolve::g_spineLostT = t;
        };
        auto giveUp = [&](const char* kind, const char* what) {
            char eb[320];
            snprintf(eb, sizeof(eb), "secsolve: gave up the search - an exception at depth %d (step %d, node %d): %s",
                     (int)postmortem::g_secPhaseDepth, (int)postmortem::g_secPhase,
                     (int)postmortem::g_secPhaseNode, what);
            writeResult(eb);
            char sb[480];
            snprintf(sb, sizeof(sb), "secsolve: exception kind=%s stage=%s depth=%d node=%d size/capacity "
                     "cur=%zu/%zu nxt=%zu/%zu nodes=%zu/%zu snaps=%zu/%zu acts=%zu/%zu path=%zu/%zu "
                     "seq=%zu/%zu", kind, postmortem::secPhaseName((int)postmortem::g_secPhase),
                     (int)postmortem::g_secPhaseDepth, (int)postmortem::g_secPhaseNode, cur.size(),
                     cur.capacity(), nxt.size(), nxt.capacity(), g_nodes.size(), g_nodes.capacity(),
                     snaps.size(), snaps.capacity(), acts.size(), acts.capacity(), leafPathN,
                     leafPathCap, leafSeqN, leafSeqCap);
            writeResult(sb);
            for (int ni : nxt) releaseCp(ni);
            nxt.clear();
            foundLeaf = -1;
            stopWhy = "exception";
        };
        try {
        if (cpRefused) stopWhy = "no-cp";
        for (int depth = 1; depth <= g_horizon && foundLeaf < 0 && !cpRefused; ++depth) {
            nxt.clear();
            seen.clear();
            layDead = layDup = layCap = 0;
            layMaxX = deadMaxX = 0.0;
            layMinY = 1e9; layMaxY = -1e9; layMinX = 1e9;
            g_depth = depth;
            postmortem::g_secPhaseDepth = depth;
            postmortem::g_secPhase = 1;
            const double elapsedS = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            if (g_deadlineSec > 0.0 && depth > 1) {
                if (elapsedS >= g_deadlineSec) { stopWhy = "deadline"; break; }
                if (depth >= 40 && depth % 20 == 0) {
                    const double perLayer = elapsedS / (double)(depth - 1);
                    if (elapsedS + perLayer * (double)(g_horizon - depth + 1)
                        > g_deadlineSec) {
                        stopWhy = "projected";
                        break;
                    }
                }
            }
            const double layerT0 = elapsedS;
            const long long layerR0 = restores;
            const double layerRestMs0 = g_secRestMs;
            const double layerStoreMs0 = g_secStoreMs;
            const double layerResetMs0 = g_secResetMs;
            const double layerSnapMs0 = g_secSnapMs;
            const double layerStepMs0 = g_secStepMs;
            if (g_leafMid && g_targetDepth > 0 && depth >= g_targetDepth && cur.size() > 2) {
                std::vector<int> byY(cur);
                std::stable_sort(byY.begin(), byY.end(), [&](int a, int b) {
                    return g_nodes[(size_t)a].y < g_nodes[(size_t)b].y;
                });
                const size_t mid = byY.size() / 2;
                cur.clear();
                cur.push_back(byY[mid]);
                cur.push_back(byY[0]);
                for (size_t k = 1; cur.size() < byY.size(); ++k) {
                    if (mid + k < byY.size()) cur.push_back(byY[mid + k]);
                    if (k <= mid && mid - k > 0) cur.push_back(byY[mid - k]);
                }
            }
            if (coinsOn && g_targetDepth > 0 && depth >= g_targetDepth && cur.size() > 1) {
                auto pc = [&](int n) {
                    uint32_t b = (size_t)n < coinBits.size() ? coinBits[(size_t)n] : 0u;
                    int k = 0;
                    for (; b; b &= b - 1) ++k;
                    return k;
                };
                std::stable_sort(cur.begin(), cur.end(), [&](int a, int b) { return pc(a) > pc(b); });
            }
            for (int ni : cur) {
                if (foundLeaf >= 0) break;
                if (g_sliceMs > 0 && sliceExpired()) co_await std::suspend_always{};
                postmortem::g_secPhaseNode = ni;
                postmortem::g_secPhase = 2;
                CheckpointObject* base = nullptr;
                if (!g_snapOn) {
                    base = (size_t)ni < g_cps.size() ? g_cps[(size_t)ni] : nullptr;
                    if (!base) continue;
                } else if ((size_t)ni >= snaps.size() || snaps[(size_t)ni].empty()) {
                    continue;
                }
                for (int branch = 0; branch < 2; ++branch) {
                    if (kHumanBlocks(ni, branch, depth)) continue;   // human limits
                    g_died = false;          // pick up this step's death verdict
                    if (g_snapOn) {
                        const auto snapT0 = std::chrono::steady_clock::now();   // print only
                        if (g_rephase > 0 && restores > 0
                            && restores % g_rephase == 0) rephase();
                        postmortem::g_secPhase = 10;
                        if (g_worldOn && (size_t)ni < worlds.size())
                            psnap::restoreWorld(this, g_movSet, worlds[(size_t)ni]);
                        postmortem::g_secPhase = 11;
                        psnap::restore(m_player1, this, snaps[(size_t)ni].data());
                        postmortem::g_secPhase = 12;
                        psnap::restoreEM(this, pulses[(size_t)ni]);
                        postmortem::g_secPhase = 13;
                        if (states[(size_t)ni]) psnap::restoreState(this, *states[(size_t)ni]);
                        postmortem::g_secPhase = 14;
                        if (touches[(size_t)ni])
                            psnap::restoreTouch(m_player1, *touches[(size_t)ni], this);
                        postmortem::g_secPhase = 15;
                        if ((size_t)ni < acts.size()) psnap::restoreAct(acts[(size_t)ni]);
                        postmortem::g_secPhase = 16;
                        // The wave trail is not in the snapshot (see g_trailClear)
                        if (secsolve::g_trailClear) {
                            for (PlayerObject* p : {m_player1, m_player2})
                                if (p && p->m_waveTrail && p->m_waveTrail->m_pointArray)
                                    p->m_waveTrail->m_pointArray->removeAllObjects();
                        }
                        g_injecting = true;
                        this->handleButton(false, 1, true);
                        g_injecting = false;
                        g_held = 0;
                        if (g_nodes[(size_t)ni].in) secArmHold();
                        if ((size_t)ni < g_dash.size()) {
                            secRestoreDash(g_dash[(size_t)ni]);
                            secRestoreJb(g_dash[(size_t)ni]);
                        }
                        if (m_player1 && (size_t)ni < g_accel.size()) {
                            m_player1->m_accelerationOrSpeed = g_accel[(size_t)ni];
                            m_player1->m_touchedPad = g_pad[(size_t)ni] != 0;
                        }
                        // ...both bodies (see g_dash2).
                        secRestoreP2((size_t)ni, false);
                        if (secsolve::g_visRefresh) {
                            this->preUpdateVisibility(0.f);
                            this->updateVisibility(0.f);
                        }
                        ++restores;
                        g_secSnapMs += std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - snapT0).count();
                    } else {
                        secRestoreFrom(pl, base, g_nodes[(size_t)ni].in);
                        ++restores;
                        // Dash is not in the checkpoint. Restore it here
                        if ((size_t)ni < g_dash.size()) {
                            secRestoreDash(g_dash[(size_t)ni]);
                            secRestoreJb(g_dash[(size_t)ni]);
                        }
                        if ((size_t)ni < g_vy.size() && m_player1)
                            m_player1->m_yVelocity = g_vy[(size_t)ni];
                        if (m_player1 && (size_t)ni < g_accel.size()) {
                            m_player1->m_accelerationOrSpeed = g_accel[(size_t)ni];
                            m_player1->m_touchedPad = g_pad[(size_t)ni] != 0;
                        }
                        // ...both bodies (see g_dash2).
                        secRestoreP2((size_t)ni, true);
                        for (int f = 0; f < snapFreeze; ++f) {
                            secStep(g_nodes[(size_t)ni].in, dt); ++steps;
                        }
                        if (g_overlay && (size_t)ni < snaps.size()
                            && !snaps[(size_t)ni].empty()) {
                            if (g_overlay & 1)
                                psnap::restore(m_player1, this,
                                               snaps[(size_t)ni].data());
                            if (g_overlay & 2) psnap::restoreEM(this, pulses[(size_t)ni]);
                            if ((g_overlay & 4) && states[(size_t)ni])
                                psnap::restoreState(this, *states[(size_t)ni]);
                            if ((g_overlay & 8) && touches[(size_t)ni])
                                psnap::restoreTouch(m_player1, *touches[(size_t)ni], this);
                        }
                    }
                    if (secsolve::g_colLog && m_player1 && restores % 2000 == 1) {
                        auto cnt = [](cocos2d::CCDictionary* d) {
                            return d ? (int)d->count() : -1;
                        };
                        char cb[200];
                        snprintf(cb, sizeof(cb),
                                 "seccollog: d=%d top=%d bottom=%d left=%d right=%d "
                                 "snapped=%s collided=%s",
                                 depth, cnt(m_player1->m_collisionLogTop),
                                 cnt(m_player1->m_collisionLogBottom),
                                 cnt(m_player1->m_collisionLogLeft),
                                 cnt(m_player1->m_collisionLogRight),
                                 m_player1->m_objectSnappedTo ? "yes" : "no",
                                 m_player1->m_collidedObject ? "yes" : "no");
                        writeResult(cb);
                    }
                    if (g_winShift) {
                        this->m_leftSectionIndex += g_winShift;
                        this->m_rightSectionIndex += g_winShift;
                    }
                    g_stepSpine = g_spineOn && ni == g_spine
                                  && branch == spinePlanHeld(depth);
                    g_stepDepth = depth;
                    {
                        const auto stepT0 = std::chrono::steady_clock::now();   // print only
                        postmortem::g_secPhase = 20;
                        secStepBegin(pl);
                        postmortem::g_secPhase = 21;
                        secStep(branch, dt); ++steps;
                        postmortem::g_secPhase = 22;
                        if (secsolve::g_nonFinite > 0) {
                            char nb[240];
                            snprintf(nb, sizeof(nb), "object uid %d (id %d) went to position (%g, %g) on "
                                     "the step from node %d, branch %d", secsolve::g_nonFiniteUid,
                                     secsolve::g_nonFiniteId, secsolve::g_nonFiniteX, secsolve::g_nonFiniteY,
                                     ni, branch);
                            throw std::runtime_error(nb);
                        }
                        // cfg secthrow: the containment's own test (Config::secThrowKind).
                        if (g_cfg.secThrowKind != 0 && depth == g_cfg.secThrowDepth && !nxt.empty()
                            && secsolve::g_secThrowsDone < (g_cfg.secThrowKind == 3 ? 2 : 1)) {
                            const bool alloc = g_cfg.secThrowKind == 2
                                               || (g_cfg.secThrowKind == 3 && secsolve::g_secThrowsDone == 1);
                            ++secsolve::g_secThrowsDone;
                            if (alloc) throw std::bad_alloc();
                            throw std::length_error("cfg secthrow: a length_error on purpose");
                        }
                        g_secStepMs += std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - stepT0).count();
                    }
                    g_stepSpine = false;
                    auto* p = m_player1;
                    if (p && killAboveY > 0.0
                        && (double)p->getPositionY() > killAboveY) {
                        ++layDead;
                        deadMaxX = std::max(deadMaxX, (double)p->getPositionX());
                        needWake = true;
                        continue;
                    }
                    if (!p || g_died || p->m_isDead) {
                        ++layDead;
                        if (p) deadMaxX = std::max(deadMaxX, (double)p->getPositionX());
                        needWake = true;   // only meaningful on the checkpoint path
                        continue;
                    }
                    const cocos2d::CCPoint at = psnap::physPosition(p, this);
                    const double px = at.x;
                    const double py = at.y;
                    const double pv = (double)p->m_yVelocity;
                    // cfg seccoins: the coins this step touched, and a coin left behind.
                    uint32_t bitsHere = coinsOn ? coinBits[(size_t)ni] : 0u;
                    if (coinsOn) {
                        const cocos2d::CCRect pr = p->getObjectRect();
                        const bool forward = !p->m_isGoingLeft && !p->m_isSideways
                                             && coinFrame == 0
                                             && g_rotGameplayCalls == coinRotAt;
                        bool missed = false;
                        for (size_t c = 0; c < coinObj.size(); ++c) {
                            const uint32_t bit = 1u << c;
                            if (bitsHere & bit) continue;
                            const cocos2d::CCRect cr = coinObj[c]->getObjectRect();
                            const bool past = forward && px > cr.getMaxX() + 15.f && coinFinal[c]
                                              && !(c < solver::g_coinMoveX.size()
                                                   && solver::g_coinMoveX[c] > -1e8f
                                                   && px <= solver::g_coinMoveX[c]);
                            // not there now: passing it is no miss
                            if (coinObj[c]->m_isGroupDisabled) {
                                if (past && !(secsolve::g_rungCoinOff
                                              && (int)c == secsolve::g_rungCoin))
                                    bitsHere |= bit;
                                continue;
                            }
                            if (pr.intersectsRect(cr)) { bitsHere |= bit; continue; }
                            if (past) missed = true;
                        }
                        if (missed) {
                            ++layDead;
                            ++coinMissBranches;
                            deadMaxX = std::max(deadMaxX, px);
                            needWake = true;
                            continue;
                        }
                    }
                    if (!secsolve::g_secBands.empty()) {
                        bool banned = false;
                        for (const auto& db : secsolve::g_secBands)
                            if (px >= db.x0 && px <= db.x1
                                && py >= db.y0 && py <= db.y1
                                && (db.mode < 0 || modeIdx(p) != db.mode)) {
                                banned = true;
                                break;
                            }
                        if (banned) {
                            ++layDead;
                            deadMaxX = std::max(deadMaxX, px);
                            needWake = true;
                            continue;
                        }
                    }
                    layMaxX = std::max(layMaxX, px);
                    layMinX = std::min(layMinX, px);
                    layMinY = std::min(layMinY, py);
                    layMaxY = std::max(layMaxY, py);
                    if (reachedGoal(px, py, depth)) {
                        if (g_maxDoomed > 0 && doomedLeaves >= g_maxDoomed)
                            continue;
                        if (coinsOn && secsolve::g_rungCoin >= 0
                            && !(bitsHere & (1u << secsolve::g_rungCoin))) {
                            ++coinLessLeaves;
                            continue;
                        }
                        g_nodes.push_back({ni, (uint8_t)branch,
                                           (uint8_t)(p->m_isDashing ? 1 : 0),
                                           (float)py, (float)pv, (float)px,
                                           (uint16_t)secsolve::cntNow(this),
                                           (uint8_t)(p->m_vehicleSize < 0.9f ? 1 : 0),
                                           (uint8_t)(secDualP2() ? 1 : 0), secDualY2()});
                        g_dash.emplace_back();
                        secCaptureDash(g_dash.back());
                        g_vy.push_back(p ? p->m_yVelocity : 0.0);
                        g_accel.push_back(p ? p->m_accelerationOrSpeed : 0.0);
                        g_pad.push_back(p && p->m_touchedPad ? 1 : 0);
                        secPushP2();
                        coinBits.push_back(bitsHere);
                        g_cps.push_back(nullptr);
                        if (keepSnaps) {
                            snaps.emplace_back(); pulses.emplace_back();
                            states.emplace_back(); touches.emplace_back();
                            acts.emplace_back();
                        }
                        actSigs.push_back(0);
                        if (g_worldOn) worlds.emplace_back();
                        const int cand = (int)g_nodes.size() - 1;
                        const int st = evalLeaf(cand);
                        if (st == 1 && g_secRung) {
                            ++unverLeaves;
                            needWake = true;
                            continue;
                        }
                        if (st != 2) {           // SOLVED or UNVERIFIED stops the search
                            foundLeaf = cand;
                            foundX = st == 0 ? verifyX : px;
                            foundDepth = depth;
                            break;
                        }
                        ++doomedLeaves;
                        needWake = true;
                        // Keep the last dead-end exit for the report (foundLeaf stays unset)
                        foundX = verifyX;
                        continue;
                    }
                    const int cntHere = secsolve::cntNow(this);
                    std::vector<uint8_t> actHere;
                    uint64_t actSigHere = 0;
                    postmortem::g_secPhase = 30;
                    if (g_snapOn && psnap::g_snapAct) actSigHere = psnap::captureActSig(actHere);
                    postmortem::g_secPhase = 31;
                    long long p2Here = 0;
                    if (secsolve::g_p2Key) {
                        if (auto* q = secDualP2())
                            p2Here = keyOf(secDualY2(), q->m_yVelocity, (int)modeIdx(q),
                                           q->m_vehicleSize < 0.9f ? 1 : 0,
                                           q->m_isUpsideDown ? 1 : 0, 0, 0.0,
                                           q->m_isDashing ? 1 : 0, 0);
                    }
                    const long long k = keyOf(py, pv, (int)modeIdx(p),
                                              p->m_vehicleSize < 0.9f ? 1 : 0,
                                              p->m_isUpsideDown ? 1 : 0, branch,
                                              px, p->m_isDashing ? 1 : 0,
                                              cntHere)
                                        ^ (long long)((unsigned long long)bitsHere
                                                      * 0x9E3779B97F4A7C15ULL)
                                        ^ (psnap::g_actKey
                                               ? (long long)(actSigHere * psnap::kActKeyMul)
                                               : 0LL)
                                        ^ (long long)((unsigned long long)p2Here
                                                      * 0xD6E8FEB86659FD93ULL);
                    const bool isSpine =
                        g_spineOn && ni == g_spine && branch == spinePlanHeld(depth);
                    if (!seen.insert(k).second && !isSpine) { ++layDup; continue; }
                    if (g_snapOn) {
                        g_nodes.push_back({ni, (uint8_t)branch,
                                           (uint8_t)(p->m_isDashing ? 1 : 0),
                                           (float)py, (float)pv, (float)px,
                                           (uint16_t)cntHere,
                                           (uint8_t)(p->m_vehicleSize < 0.9f ? 1 : 0),
                                           (uint8_t)(secDualP2() ? 1 : 0), secDualY2()});
                        g_dash.emplace_back();
                        secCaptureDash(g_dash.back());
                        g_vy.push_back(p ? p->m_yVelocity : 0.0);
                        g_accel.push_back(p ? p->m_accelerationOrSpeed : 0.0);
                        g_pad.push_back(p && p->m_touchedPad ? 1 : 0);
                        secPushP2();
                        coinBits.push_back(bitsHere);
                        g_cps.push_back(nullptr);
                        snaps.emplace_back();
                        pulses.emplace_back();
                        states.push_back(newState());
                        touches.push_back(std::make_unique<psnap::Touch>());
                        acts.emplace_back(std::move(actHere));
                        actSigs.push_back(actSigHere);
                        actSigSeen.insert(actSigHere);
                        postmortem::g_secPhase = 40;
                        psnap::capture(p, this, snaps.back());
                        postmortem::g_secPhase = 41;
                        psnap::captureEM(this, pulses.back());
                        postmortem::g_secPhase = 42;
                        psnap::captureState(this, *states.back());
                        postmortem::g_secPhase = 43;
                        psnap::captureTouch(p, *touches.back(), this);
                        postmortem::g_secPhase = 44;
                        if (g_worldOn) {
                            worlds.emplace_back();
                            psnap::captureWorld(g_movSet, worlds.back());
                        }
                        postmortem::g_secPhase = 45;
                        ++cpMade;
                    } else {
                        CheckpointObject* cp = markCheckpointAtPhys(pl);
                        if (!cp) continue;             // if it cannot be made, drop that branch
                        cp->retain(); ++cpMade;
                        g_nodes.push_back({ni, (uint8_t)branch,
                                           (uint8_t)(p->m_isDashing ? 1 : 0),
                                           (float)py, (float)pv, (float)px,
                                           (uint16_t)cntHere,
                                           (uint8_t)(p->m_vehicleSize < 0.9f ? 1 : 0),
                                           (uint8_t)(secDualP2() ? 1 : 0), secDualY2()});
                        g_dash.emplace_back();
                        secCaptureDash(g_dash.back());
                        g_vy.push_back(p ? p->m_yVelocity : 0.0);
                        g_accel.push_back(p ? p->m_accelerationOrSpeed : 0.0);
                        g_pad.push_back(p && p->m_touchedPad ? 1 : 0);
                        secPushP2();
                        coinBits.push_back(bitsHere);
                        actSigs.push_back(0);
                        g_cps.push_back(cp);
                        // For the stacking check. Taken at the same instant as the checkpoint
                        if (g_overlay) {
                            snaps.emplace_back(); pulses.emplace_back();
                            states.push_back(newState());
                            touches.push_back(std::make_unique<psnap::Touch>());
                            acts.emplace_back();
                            psnap::capture(p, this, snaps.back());
                            psnap::captureEM(this, pulses.back());
                            psnap::captureState(this, *states.back());
                            psnap::captureTouch(p, *touches.back(), this);
                            psnap::captureAct(acts.back());
                        }
                    }
                    nxt.push_back((int)g_nodes.size() - 1);
                    if (isSpine) g_spineNext = nxt.back();
                    cpPeak = std::max(cpPeak, (long long)(cur.size() + nxt.size()));
                }
            }
            postmortem::g_secPhase = 50;
            postmortem::g_secPhaseNode = -1;
            if (secsolve::g_keepP1Size >= 0) {
                size_t w = 0;
                for (int ni : nxt) {
                    if ((int)g_nodes[(size_t)ni].mini == secsolve::g_keepP1Size) nxt[w++] = ni;
                    else { releaseCp(ni); ++secsolve::g_keepDropped; }
                }
                nxt.resize(w);
            }
            int spineHeld = -1;
            if (g_spineOn && g_spineNext >= 0) {
                auto it = std::find(nxt.begin(), nxt.end(), g_spineNext);
                if (it != nxt.end()) { spineHeld = *it; nxt.erase(it); }
            }
            bool capByX = false;
            // The order the cap reads a family in: y, then vy.
            auto yThenVy = [&](int a, int b) {
                const auto& na = g_nodes[(size_t)a];
                const auto& nb = g_nodes[(size_t)b];
                if (na.y != nb.y) return na.y < nb.y;
                return na.vy < nb.vy;
            };
            auto pickCover = [&](const std::vector<int>& v, size_t q) {
                std::vector<int> out;
                if (q == 0) return out;
                if (v.size() <= q) { out = v; return out; }
                if (q == 1) { out.push_back(v[v.size() / 2]); return out; }
                if (q == 2) { out.push_back(v.front()); out.push_back(v.back()); return out; }
                float ylo = 1e30f, yhi = -1e30f, vlo = 1e30f, vhi = -1e30f;
                float y2lo = 1e30f, y2hi = -1e30f;
                bool withY2 = false;   // player 2's y as a further axis (cfg secp2key)
                for (int ni : v) {
                    const auto& n = g_nodes[(size_t)ni];
                    ylo = std::min(ylo, n.y); yhi = std::max(yhi, n.y);
                    vlo = std::min(vlo, n.vy); vhi = std::max(vhi, n.vy);
                    if (secsolve::g_p2Key && n.dual) {
                        withY2 = true;
                        y2lo = std::min(y2lo, n.y2); y2hi = std::max(y2hi, n.y2);
                    }
                }
                const double ys = std::max(1e-6, (double)(yhi - ylo));
                const double vs = std::max(1e-6, (double)(vhi - vlo));
                const double y2s = std::max(1e-6, (double)(y2hi - y2lo));
                auto cellOf = [&](int ni, long long k) {
                    const auto& n = g_nodes[(size_t)ni];
                    const long long cy =
                        std::min(k - 1, (long long)((n.y - ylo) / ys * (double)k));
                    const long long cv =
                        std::min(k - 1, (long long)((n.vy - vlo) / vs * (double)k));
                    long long c = cy * (k + 1) + cv;
                    if (withY2)
                        c = c * (k + 1)
                            + (n.dual ? std::min(k - 1, (long long)((n.y2 - y2lo) / y2s
                                                                     * (double)k))
                                      : k);
                    return c * 2 + (n.in ? 1 : 0);
                };
                auto occupied = [&](long long k) {
                    std::unordered_set<long long> c;
                    for (int ni : v) c.insert(cellOf(ni, k));
                    return c.size();
                };
                long long lo = 1, hi = 8192;
                while (lo < hi) {
                    const long long mid = (lo + hi + 1) / 2;
                    if (occupied(mid) + 2 <= q) lo = mid; else hi = mid - 1;
                }
                std::unordered_set<long long> cells;
                std::unordered_set<int> in;
                out.reserve(q);
                out.push_back(v.front()); in.insert(v.front());
                out.push_back(v.back()); in.insert(v.back());
                for (int ni : v) {
                    if (out.size() >= q) break;
                    if (!cells.insert(cellOf(ni, lo)).second) continue;
                    if (in.insert(ni).second) out.push_back(ni);
                }
                if (out.size() < q) {
                    std::vector<int> rest;
                    for (int ni : v) if (!in.count(ni)) rest.push_back(ni);
                    const size_t need = q - out.size();
                    for (size_t i = 0; i < need && i < rest.size(); ++i) {
                        const size_t idx = need > 1
                            ? (size_t)((double)i * (double)(rest.size() - 1)
                                       / (double)(need - 1) + 0.5)
                            : rest.size() / 2;
                        if (in.insert(rest[idx]).second) out.push_back(rest[idx]);
                    }
                }
                return out;
            };
            // The cap proper: `list` down to `cap` nodes, the others released.
            auto pruneTo = [&](std::vector<int>& list, size_t cap) {
                if (list.size() > cap) {
                    std::unordered_map<long long, std::vector<int>> buckets;
                    for (int ni : list) {
                        const auto& nd = g_nodes[(size_t)ni];
                        const long long b = (long long)std::llround(nd.x / 16.0)
                                            + (nd.dash ? 1000003LL : 0LL)
                                            + (long long)nd.cnt * 2000003LL
                                            + (coinsOn ? (long long)coinBits[(size_t)ni] * 3000017LL
                                                       : 0LL)
                                            + (psnap::g_actKey && (size_t)ni < actSigs.size()
                                                   ? (long long)(actSigs[(size_t)ni] % 1000003ull)
                                                         * 5000011LL
                                                   : 0LL);
                        buckets[b].push_back(ni);
                    }
                    if (buckets.size() > 1) {
                        for (auto& kv : buckets) {
                            std::sort(kv.second.begin(), kv.second.end(),
                                      [&](int a, int b2) {
                                          const auto& na = g_nodes[(size_t)a];
                                          const auto& nb = g_nodes[(size_t)b2];
                                          if (na.y != nb.y) return na.y < nb.y;
                                          return na.vy < nb.vy;
                                      });
                        }
                        std::vector<long long> keys;
                        keys.reserve(buckets.size());
                        for (auto& kv : buckets) keys.push_back(kv.first);
                        std::sort(keys.begin(), keys.end());
                        std::vector<int> keepv;
                        keepv.reserve(cap);
                        std::unordered_set<int> kept;
                        // The share per bucket. Even a small family always gets one.
                        const size_t per = std::max<size_t>(1, cap / keys.size());
                        for (long long k : keys) {
                            auto& v = buckets[k];
                            const size_t take = std::min(per, v.size());
                            if (secsolve::g_cover) {
                                for (int ni : pickCover(v, take))
                                    if (keepv.size() < cap && kept.insert(ni).second)
                                        keepv.push_back(ni);
                                continue;
                            }
                            for (size_t i = 0; i < take && keepv.size() < cap; ++i) {
                                const size_t idx = take > 1
                                    ? (size_t)((double)i * (double)(v.size() - 1)
                                               / (double)(take - 1) + 0.5)
                                    : v.size() / 2;
                                if (kept.insert(v[idx]).second) keepv.push_back(v[idx]);
                            }
                        }
                        // Fill the remainder in y order (raises the band's resolution)
                        if (keepv.size() < cap) {
                            std::vector<int> rest;
                            for (int ni : list) if (!kept.count(ni)) rest.push_back(ni);
                            std::sort(rest.begin(), rest.end(), [&](int a, int b2) {
                                return g_nodes[(size_t)a].y < g_nodes[(size_t)b2].y;
                            });
                            const size_t need = cap - keepv.size();
                            if (secsolve::g_cover) {
                                std::sort(rest.begin(), rest.end(), yThenVy);
                                for (int ni : pickCover(rest, need))
                                    if (kept.insert(ni).second) keepv.push_back(ni);
                            }
                            for (size_t i = 0; !secsolve::g_cover && i < need && i < rest.size();
                                 ++i) {
                                const size_t idx = need > 1
                                    ? (size_t)((double)i * (double)(rest.size() - 1)
                                               / (double)(need - 1) + 0.5)
                                    : rest.size() / 2;
                                if (kept.insert(rest[idx]).second)
                                    keepv.push_back(rest[idx]);
                            }
                        }
                        for (int ni : list)
                            if (!kept.count(ni)) { releaseCp(ni); ++layCap; ++capTotal; }
                        list.swap(keepv);
                        capByX = true;
                    }
                }
                if (list.size() > cap) {
                    double ylo = 1e18, yhi = -1e18, xlo = 1e18, xhi = -1e18;
                    for (int ni : list) {
                        const auto& n = g_nodes[(size_t)ni];
                        ylo = std::min(ylo, (double)n.y); yhi = std::max(yhi, (double)n.y);
                        xlo = std::min(xlo, (double)n.x); xhi = std::max(xhi, (double)n.x);
                    }
                    capByX = (xhi - xlo) > (yhi - ylo);
                    std::sort(list.begin(), list.end(), [&](int a, int b) {
                        const auto& na = g_nodes[(size_t)a];
                        const auto& nb = g_nodes[(size_t)b];
                        if (capByX) {
                            if (na.x != nb.x) return na.x < nb.x;
                            if (na.y != nb.y) return na.y < nb.y;
                            return na.vy < nb.vy;
                        }
                        if (na.y != nb.y) return na.y < nb.y;
                        return na.vy < nb.vy;
                    });
                    const size_t n = list.size();
                    std::vector<int> keepv;
                    keepv.reserve(cap);
                    std::unordered_set<int> kept;
                    const bool cover = secsolve::g_cover && !capByX;
                    if (cover)
                        for (int ni : pickCover(list, cap))
                            if (kept.insert(ni).second) keepv.push_back(ni);
                    for (size_t i = 0; !cover && i < cap; ++i) {
                        const size_t idx = cap > 1
                            ? (size_t)((double)i * (double)(n - 1) / (double)(cap - 1) + 0.5)
                            : n / 2;
                        if (kept.insert(list[idx]).second) keepv.push_back(list[idx]);
                    }
                    for (int ni : list)
                        if (!kept.count(ni)) { releaseCp(ni); ++layCap; ++capTotal; }
                    list.swap(keepv);
                }
            };
            if (secsolve::g_sizeFam && nxt.size() > g_cap) {
                std::vector<int> fam[2];
                for (int ni : nxt) fam[g_nodes[(size_t)ni].mini ? 1 : 0].push_back(ni);
                if (!fam[0].empty() && !fam[1].empty()) {
                    const size_t q0 = std::min(fam[0].size(),
                                               std::max(g_cap / 2,
                                                        g_cap - std::min(g_cap, fam[1].size())));
                    const size_t q1 = std::min(fam[1].size(), g_cap - q0);
                    pruneTo(fam[0], q0);
                    pruneTo(fam[1], q1);
                    nxt.swap(fam[0]);
                    nxt.insert(nxt.end(), fam[1].begin(), fam[1].end());
                }
            }
            pruneTo(nxt, g_cap);
            // ...and the spine goes back in, whatever the pruning decided.
            if (spineHeld >= 0) nxt.push_back(spineHeld);
            postmortem::g_secPhase = 55;
            if (g_snapOn && g_verifyEvery > 0 && !nxt.empty()
                && depth % g_verifyEvery == 0) {
                postmortem::g_secPhase = 60;
                std::vector<int> ok;
                ok.reserve(nxt.size());
                int vDead = 0, vFixed = 0, vAnchored = 0;
                double vMaxDy = 0.0;
                std::vector<uint8_t> seq;
                std::vector<int> anc((size_t)nxt.size(), -1);
                std::unordered_map<int, CheckpointObject*> newAnchors;
                std::unordered_map<int, std::vector<uint8_t>> newAnchorSnaps;
                if (g_anchor) {
                    for (size_t k = 0; k < nxt.size(); ++k) {
                        int a = nxt[k];
                        for (int s = 0; s < g_verifyEvery && a > 0; ++s)
                            a = g_nodes[(size_t)a].parent;
                        if (a > 0 && g_anchors.count(a)) anc[k] = a;
                    }
                }
                struct DriftAt { int node = -1, depth = -1; double dy = 0.0, yR = 0.0,
                                 yP = 0.0, vyR = 0.0, vyP = 0.0, xR = 0.0; int mode = -1,
                                 up = -1; float size = 0.f; };
                DriftAt worstDrift;
                std::vector<int> path;
                for (size_t k = 0; k < nxt.size(); ++k) {
                    const int ni = nxt[k];
                    const int a = g_anchor ? anc[k] : -1;
                    seq.clear();
                    path.clear();
                    for (int i = ni; i > 0 && i != a; i = g_nodes[(size_t)i].parent) {
                        seq.push_back(g_nodes[(size_t)i].in);
                        path.push_back(i);
                    }
                    std::reverse(seq.begin(), seq.end());
                    std::reverse(path.begin(), path.end());
                    DriftAt here;
                    auto originCheck = [&](int origin) {
                        if (!psnap::g_snapAct || origin < 0 || (size_t)origin >= acts.size())
                            return;
                        const auto ul = psnap::originUsedLost(acts[(size_t)origin]);
                        ++originChecks;
                        originUsed += ul.first;
                        originLost += ul.second;
                    };
                    if (a > 0) {
                        secRestoreFrom(pl, g_anchors[a], g_nodes[(size_t)a].in);
                        if (psnap::g_snapAct && (size_t)a < acts.size())
                            psnap::restoreAct(acts[(size_t)a]);
                        originCheck(a);
                        ++restores; ++vAnchored;
                        if ((size_t)a < g_dash.size()) secRestoreJb(g_dash[(size_t)a]);
                        if (auto* q = secP2(); q && (size_t)a < secsolve::g_dash2.size())
                            secRestoreJb(secsolve::g_dash2[(size_t)a], q);
                        for (int f = 0; f < freeze; ++f) {
                            secStep(g_nodes[(size_t)a].in, dt); ++steps;
                        }
                        cpTakeLoad();
                        // ...and the origin's own player (g_cpPlayer).
                        const auto it = anchorSnaps.find(a);
                        if (secsolve::g_cpPlayer && it != anchorSnaps.end() && !it->second.empty())
                            psnap::restore(m_player1, this, it->second.data());
                        if (it != anchorSnaps.end())
                            cpReport(4, "checkanchor", (long long)depth, it->second);
                    } else {
                        secRestoreFrom(pl, g_ckpt, g_headHeld); ++restores;
                        if (psnap::g_snapAct && !acts.empty()) psnap::restoreAct(acts[0]);
                        originCheck(0);
                        for (int f = 0; f < freeze; ++f) {
                            secStep(g_headHeld, dt); ++steps;
                        }
                        cpTakeLoad();
                        if (secsolve::g_cpPlayer && !snaps.empty() && !snaps[0].empty())
                            psnap::restore(m_player1, this, snaps[0].data());
                        cpReport(3, "checkhead", (long long)depth, secsolve::g_headPlain);
                    }
                    bool dead = false;
                    for (size_t s = 0; s < seq.size(); ++s) {
                        g_died = false;
                        secStep((int)seq[s], dt); ++steps;
                        if (!m_player1 || m_player1->m_isDead || g_died) {
                            dead = true; break;
                        }
                        if (g_cfg.secDriftWhere && here.node < 0) {
                            const auto& an = g_nodes[(size_t)path[s]];
                            const double yR = m_player1->getPositionY();
                            if (std::fabs(yR - (double)an.y) > 0.5) {
                                here.node = path[s];
                                here.depth = depth - (int)(seq.size() - 1 - s);
                                here.yR = yR; here.yP = an.y;
                                here.vyR = m_player1->m_yVelocity; here.vyP = an.vy;
                                here.xR = m_player1->getPositionX();
                                here.mode = (int)modeIdx(m_player1);
                                here.up = m_player1->m_isUpsideDown ? 1 : 0;
                                here.size = m_player1->m_vehicleSize;
                            }
                        }
                    }
                    if (dead) { ++vDead; releaseCp(ni); continue; }
                    auto* vp = m_player1;
                    const cocos2d::CCPoint rat = psnap::physPosition(vp, this);
                    const double rx = rat.x;
                    const double ry = rat.y;
                    const double rv = (double)vp->m_yVelocity;
                    const double dx = std::fabs(rx - (double)g_nodes[(size_t)ni].x);
                    const double dy = std::fabs(ry - (double)g_nodes[(size_t)ni].y);
                    const double dv = std::fabs(rv - (double)g_nodes[(size_t)ni].vy);
                    const double dmax = std::max(dx, std::max(dy, dv));
                    vMaxDy = std::max(vMaxDy, dmax);
                    if (here.node >= 0 && dmax > worstDrift.dy) {
                        worstDrift = here;
                        worstDrift.dy = dmax;
                    }
                    if (dx > g_verifyTol || dy > g_verifyTol || dv > g_verifyTol) ++vFixed;
                    g_nodes[(size_t)ni].x = (float)rx;
                    g_nodes[(size_t)ni].y = (float)ry;
                    g_nodes[(size_t)ni].vy = (float)rv;
                    psnap::capture(vp, this, snaps[(size_t)ni]);
                    psnap::captureEM(this, pulses[(size_t)ni]);
                    if (!states[(size_t)ni]) states[(size_t)ni] = newState();
                    if (!touches[(size_t)ni]) touches[(size_t)ni] = std::make_unique<psnap::Touch>();
                    psnap::captureState(this, *states[(size_t)ni]);
                    psnap::captureTouch(vp, *touches[(size_t)ni], this);
                    if ((size_t)ni < acts.size() && psnap::g_snapAct) {
                        const uint64_t sig = psnap::captureActSig(acts[(size_t)ni]);
                        if ((size_t)ni < actSigs.size()) actSigs[(size_t)ni] = sig;
                        actSigSeen.insert(sig);
                    }
                    if (g_worldOn)
                        psnap::captureWorld(g_movSet, worlds[(size_t)ni]);
                    if (g_anchor) {
                        if (CheckpointObject* ac = markCheckpointAtPhys(pl)) {
                            ac->retain();
                            newAnchors[ni] = ac;
                            if (secsolve::g_cpPlayer || psnap::g_cpCheck)
                                newAnchorSnaps[ni] = snaps[(size_t)ni];
                        }
                    }
                    ok.push_back(ni);
                }
                for (auto& kv : g_anchors) {
                    if (kv.second) kv.second->release();
                    const int an = kv.first;
                    if (!newAnchors.count(an) && (size_t)an < acts.size()
                        && (size_t)an < snaps.size() && snaps[(size_t)an].empty())
                        std::vector<uint8_t>().swap(acts[(size_t)an]);
                }
                g_anchors.swap(newAnchors);
                anchorSnaps.swap(newAnchorSnaps);
                maxVerifyDrift = std::max(maxVerifyDrift, vMaxDy);
                char vb[240];
                snprintf(vb, sizeof(vb),
                         "secfix: d=%d checked=%zu dead=%d drift=%d maxDiff=%.4f "
                         "kept=%zu anchored=%d",
                         depth, nxt.size(), vDead, vFixed, vMaxDy, ok.size(),
                         vAnchored);
                writeResult(vb);
                if (g_cfg.secDriftWhere && worstDrift.node >= 0) {
                    char db[320];
                    snprintf(db, sizeof(db),
                             "secdrift: d=%d worst=%.3f firstAt d=%d t=%lld x=%.2f "
                             "yReal=%.3f yPsnap=%.3f vyReal=%.3f vyPsnap=%.3f mode=%d up=%d "
                             "size=%.2f",
                             depth, worstDrift.dy, worstDrift.depth,
                             (long long)(g_ckptTick + 1 + worstDrift.depth), worstDrift.xR,
                             worstDrift.yR, worstDrift.yP, worstDrift.vyR, worstDrift.vyP,
                             worstDrift.mode, worstDrift.up, worstDrift.size);
                    writeResult(db);
                }
                nxt.swap(ok);
                if (nxt.empty()) { deepest = depth; spineLied(depth); break; }
            }
            deepest = depth;
            spineLied(depth);
            if (g_log) {
                uint64_t fp = 1469598103934665603ull;
                for (int ni : nxt) {
                    const auto& nd = g_nodes[(size_t)ni];
                    const uint8_t* b = (const uint8_t*)&nd.y;
                    for (size_t j = 0; j < sizeof(float) * 2; ++j) {
                        fp ^= b[j]; fp *= 1099511628211ull;
                    }
                    fp ^= nd.in; fp *= 1099511628211ull;
                }
                int cntLo = 65535, cntHi = 0;
                for (int ni : nxt) {
                    const int c = (int)g_nodes[(size_t)ni].cnt;
                    cntLo = std::min(cntLo, c);
                    cntHi = std::max(cntHi, c);
                }
                if (cntLo > cntHi) { cntLo = 0; cntHi = 0; }
                const double layerMs = (std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count()
                    - layerT0 * 1000.0);
                size_t secLive = 0, secPhys = 0;
                postmortem::g_secPhase = 70;
                sectionCensus(pl, secLive, secPhys);
                char lb[768];
                snprintf(lb, sizeof(lb),
                         "seclayer: d=%d parents=%zu keep=%zu dead=%d dup=%d "
                         "capped=%d x=%.1f y=%.1f..%.1f deadX=%.1f fp=%016llx "
                         "xr=%.1f..%.1f axis=%c cnt=%d..%d spine=%d spineY=%.1f "
                         "lms=%.0f lrest=%lld lrestms=%.0f nodes=%zu dedup=%zu "
                         "wsMB=%zu actN=%d secLive=%zu secPhys=%zu "
                         "lstorems=%.0f lresetms=%.0f",
                         depth, cur.size(), nxt.size(), layDead, layDup, layCap,
                         layMaxX, layMinY > 1e8 ? 0.0 : layMinY,
                         layMaxY < -1e8 ? 0.0 : layMaxY, deadMaxX,
                         (unsigned long long)fp,
                         layMinX > 1e8 ? 0.0 : layMinX, layMaxX,
                         capByX ? 'x' : 'y', cntLo, cntHi, g_spineNext,
                           g_spineNext >= 0 ? (double)g_nodes[(size_t)g_spineNext].y : -1.0,
                         layerMs, restores - layerR0,
                         g_secRestMs - layerRestMs0, g_nodes.size(), seen.size(),
                         layerWsMB(), runningActionsOn(m_player1),
                         secLive, secPhys,
                         g_secStoreMs - layerStoreMs0,
                         g_secResetMs - layerResetMs0);
                {
                    char sx[112];
                    const auto* wt = m_player1 ? m_player1->m_waveTrail : nullptr;
                    snprintf(sx, sizeof(sx), " lsnapms=%.0f lstepms=%.0f trail=%d",
                             g_secSnapMs - layerSnapMs0, g_secStepMs - layerStepMs0,
                             wt && wt->m_pointArray ? (int)wt->m_pointArray->count() : -1);
                    strncat(lb, sx, sizeof(lb) - strlen(lb) - 1);
                }
                // ...and, under cfg secsizefam, how many of the kept nodes are normal size.
                if (secsolve::g_sizeFam) {
                    int normal = 0;
                    for (int ni : nxt) normal += g_nodes[(size_t)ni].mini ? 0 : 1;
                    char sf[40];
                    snprintf(sf, sizeof(sf), " normal=%d", normal);
                    strncat(lb, sf, sizeof(lb) - strlen(lb) - 1);
                }
                {
                    long long kids2 = 0, kids3 = 0;
                    if (auto* ch = this->getChildren()) {
                        for (unsigned i = 0; i < ch->count(); ++i) {
                            auto* c = static_cast<cocos2d::CCNode*>(ch->objectAtIndex(i));
                            if (!c) continue;
                            kids2 += c->getChildrenCount();
                            if (auto* gc = c->getChildren())
                                for (unsigned j = 0; j < gc->count(); ++j)
                                    if (auto* g = static_cast<cocos2d::CCNode*>(gc->objectAtIndex(j)))
                                        kids3 += g->getChildrenCount();
                        }
                    }
                    char sn[128];
                    snprintf(sn, sizeof(sn), " plActs=%d plKids=%u kids2=%lld kids3=%lld",
                             runningActionsOn(this), this->getChildrenCount(), kids2, kids3);
                    strncat(lb, sn, sizeof(lb) - strlen(lb) - 1);
                    auto kidsAt = [&](size_t off) -> long long {
                        auto* n = *reinterpret_cast<cocos2d::CCNode* const*>(
                            reinterpret_cast<const char*>(this) + off);
                        return n ? (long long)n->getChildrenCount() : -1;
                    };
                    snprintf(sn, sizeof(sn), " shd=%lld,%lld,%lld,%lld,%lld,%lld",
                             kidsAt(gdoff::kLayerObjectParent),
                             kidsAt(gdoff::kLayerAboveShaderParent),
                             kidsAt(gdoff::kLayerObjectLayer),
                             kidsAt(gdoff::kLayerInShaderObjectLayer),
                             kidsAt(gdoff::kLayerAboveShaderObjectLayer),
                             kidsAt(gdoff::kLayerShaderLayer));
                    strncat(lb, sn, sizeof(lb) - strlen(lb) - 1);
                    {
                        long long sortKids = -1, sortGrand = 0, sortMax = 0, dirty = 0;
                        // A CCArray, not a node: resetLevel reads its ccArray at +0x38.
                        auto* ch = *reinterpret_cast<cocos2d::CCArray* const*>(
                            reinterpret_cast<const char*>(this) + gdoff::kLayerBatchNodes);
                        if (ch) {
                            sortKids = ch->count();
                            for (unsigned i = 0; i < ch->count(); ++i) {
                                auto* c = static_cast<cocos2d::CCNode*>(ch->objectAtIndex(i));
                                if (!c) continue;
                                const long long k = c->getChildrenCount();
                                sortGrand += k;
                                sortMax = std::max(sortMax, k);
                                if (c->m_bReorderChildDirty) ++dirty;
                            }
                        }
                        snprintf(sn, sizeof(sn), " sort=%lld,%lld,%lld,%lld", sortKids, sortGrand,
                                 sortMax, dirty);
                        strncat(lb, sn, sizeof(lb) - strlen(lb) - 1);
                    }
                }
                writeResult(lb);
                if (g_driftLog && this->m_objects) {
                    double farD = 0.0, fdx = 0.0, fdy = 0.0;   // (`far` is a Windows macro)
                    int fuid = -1, fid = -1, n1000 = 0;
                    for (unsigned i = 0; i < this->m_objects->count(); ++i) {
                        auto* o = static_cast<GameObject*>(this->m_objects->objectAtIndex(i));
                        if (!o) continue;
                        const double dx = o->m_positionX - (double)o->m_startPosition.x;
                        const double dy = o->m_positionY - (double)o->m_startPosition.y;
                        const double dd = std::fabs(dx) + std::fabs(dy);
                        if (dd > 1000.0) ++n1000;
                        if (dd > farD) {
                            farD = dd; fdx = dx; fdy = dy;
                            fuid = o->m_uniqueID; fid = o->m_objectID;
                        }
                    }
                    char db[160];
                    snprintf(db, sizeof(db), "secdrift: d=%d far=%.0f uid=%d id=%d dx=%.0f dy=%.0f "
                             "over1000=%d", depth, farD, fuid, fid, fdx, fdy, n1000);
                    writeResult(db);
                }
            }
            for (int ni : cur) releaseCp(ni);          // the previous layer is no longer needed
            if (nxt.empty()) break;
            g_spine = g_spineNext;
            g_spineNext = -1;
            cur.swap(nxt);
            g_frontierNow = cur.size();
            if (g_sliceMs > 0) {
                char hb[96];
                snprintf(hb, sizeof(hb), "secsolve: layer %d/%d, frontier %zu",
                         depth, g_horizon, cur.size());
                g_hudPhase = hb;
                if (sliceExpired()) co_await std::suspend_always{};
            }
        }
        } catch (const std::length_error& e) {
            giveUp("length_error", e.what());
        } catch (const std::bad_alloc& e) {
            giveUp("bad_alloc", e.what());
        } catch (const std::exception& e) {
            giveUp("other", e.what());
        }
        postmortem::g_secPhase = 0;
        for (int ni : cur) releaseCp(ni);
        releaseAnchors();

        secsolve::setSearching(false);
        g_noKill = false;
        g_tick = savedTick;
        g_nextInput = savedInput;
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        char b[704];
        snprintf(b, sizeof(b),
                 "secsolve: %s prim=%s movSet=%zu depth=%d/%d frontier=%zu restores=%lld "
                 "steps=%lld cps=%lld cpPeak=%lld freeze=%d ms=%.0f "
                 "rephases=%lld maskShrinks=%d ckptTick=%lld inputBase=%lld targetX=%.1f "
                 "foundX=%.1f foundTick=%lld foundY=%.3f foundVy=%.3f "
                 "targetY=%.1f targetYDir=%d targetDepth=%d grace=%d/%d "
                 "doomedLeaves=%lld killAboveY=%.0f psnapDrift=%.2f stop=%s",
                 foundLeaf < 0 ? (doomedLeaves > 0 ? "DOOMED" : "EXHAUSTED")
                               : (g_leafVerified ? "SOLVED" : "UNVERIFIED"),
                 !g_snapOn ? "cp" : (g_worldOn ? "psnap+wsnap" : "psnap"),
                 g_worldOn ? g_movSet.size() : (size_t)0, deepest, g_horizon,
                 cur.size(), restores, steps, cpMade, cpPeak, freeze, ms,
                 rephases, psnap::g_shrinks, (long long)g_ckptTick,
                 (long long)g_ckptTick, g_targetX,
                 g_leafVerified ? verifyX : foundX,
                 foundLeaf >= 0 ? (long long)(g_ckptTick + 1 + foundDepth) : -1LL,
                 g_leafVerified ? verifyY
                                : (foundLeaf >= 0
                                   ? (double)g_nodes[(size_t)foundLeaf].y : 0.0),
                 g_leafVerified ? verifyVy
                                : (foundLeaf >= 0
                                   ? (double)g_nodes[(size_t)foundLeaf].vy : 0.0),
                 g_targetY, g_targetYDir, g_targetDepth, graceOk, graceDeadAt,
                 doomedLeaves, killAboveY, maxVerifyDrift, stopWhy);
        writeResult(b);
        if (g_snapOn) {
            char ab[320];
            snprintf(ab, sizeof(ab),
                     "secsnapact: on=%d key=%d selfcheck=%d objs=%zu bytesPerNode=%zu nodes=%zu "
                     "cpPeak=%lld captures=%lld restores=%lld capUs=%.0f restUs=%.0f "
                     "p1Objs=%d p2Objs=%d sigs=%zu decos=%zu decoUsed=%d decoEver=%d",
                     psnap::g_snapAct ? 1 : 0, psnap::g_actKey ? 1 : 0, actSelfCheck,
                     psnap::g_actObjs.size(), psnap::g_actObjs.size(),
                     acts.size(), cpPeak, psnap::g_actCaptures, psnap::g_actRestores,
                     psnap::g_actCapUs, psnap::g_actRestUs, psnap::actSeenCount(1),
                     psnap::actSeenCount(2), actSigSeen.size(), psnap::g_actDecos.size(),
                     psnap::actDecosUsed(), psnap::actDecosEver());
            writeResult(ab);
            snprintf(ab, sizeof(ab), "secorigin: checks=%lld used=%lld lost=%lld", originChecks,
                     originUsed, originLost);
            writeResult(ab);
        }
        if (coinsOn) {
            const uint32_t lb = foundLeaf >= 0 ? coinBits[(size_t)foundLeaf] : 0u;
            int took = 0;
            for (size_t c = 0; c < coinObj.size(); ++c)
                if ((lb & (1u << c)) && !(coinBits[0] & (1u << c))) ++took;
            char kb[200];
            snprintf(kb, sizeof(kb), "secsolve: coins - %lld branches left a coin behind; the "
                     "leaf took %d (bits %u -> %u); owed coin %d, %lld leaves without it",
                     coinMissBranches, took, coinBits[0], lb, secsolve::g_rungCoin, coinLessLeaves);
            writeResult(kb);
        }
        if (secsolve::g_psnapLied) {
            char lb[256];
            snprintf(lb, sizeof(lb), "secsolve: the plan's own branch died at t=%lld on the player "
                     "snapshot, where the game flew it to t=%lld - the snapshot parts from the game in "
                     "this window (secsolve::g_spineUntil)%s",
                     secsolve::g_spineLostT, secsolve::g_spineUntil,
                     foundLeaf >= 0 ? "; what it found was replayed from the head" : "");
            writeResult(lb);
        }
        if (secsolve::g_keepP1Size >= 0)
            writeResult("secsolve: kept player 1 " + std::string(secsolve::g_keepP1Size ? "mini" : "normal")
                        + " - " + std::to_string(secsolve::g_keepDropped)
                        + " branches that changed its size dropped (cfg seckeepsize)");
        if (g_boundOn) {
            char bb[200];
            snprintf(bb, sizeof(bb), "secsolve: bound x %.0f..%.0f y %.0f..%.0f - %lld section "
                     "updates skipped outside it", g_boxX0, g_boxX1, g_boxY0, g_boxY1,
                     g_boundSkips);
            writeResult(bb);
        }
        if (g_shaderSkip) {
            char hb[96];
            snprintf(hb, sizeof(hb), "secsolve: shader layer left alone %lld times",
                     g_shaderSkips);
            writeResult(hb);
        }
        {
            char fb[160];
            snprintf(fb, sizeof(fb), "secsolve: frames=%lld between slices %.0f ms (outside the update %.0f ms, overlays %.0f ms)",
                     g_frames, g_frameGapMs, g_outsideMs, g_overlayMs);
            writeResult(fb);
        }
        if (g_shaderSig) {
            writeResult("shadersig: search end " + shaderSig(this));
            g_shaderSigPending = true;
        }
        {   // print only: the per-node vectors' own footprint at the end of the search (every
            size_t live = 0, actBytes = 0;
            for (const auto& s : states) live += s ? 1 : 0;
            for (const auto& v : acts) actBytes += v.capacity();
            const double elemsMB =
                (double)(states.capacity() * sizeof(states[0]) + touches.capacity() * sizeof(touches[0])
                         + pulses.capacity() * sizeof(psnap::EMSnap)
                         + (snaps.capacity() + acts.capacity() + worlds.capacity())
                               * sizeof(std::vector<uint8_t>)) / (1024.0 * 1024.0);
            size_t curMB = 0, peakMB = 0;
            procMemMB(curMB, peakMB);
            char mb[256];
            snprintf(mb, sizeof(mb), "secmem: nodes=%zu liveStates=%zu elemsMB=%.0f actMB=%.0f "
                     "procMB=%zu peakMB=%zu", g_nodes.size(), live, elemsMB,
                     (double)actBytes / (1024.0 * 1024.0), curMB, peakMB);
            writeResult(mb);
        }
        if (foundLeaf >= 0) {
            char vb[280];
            snprintf(vb, sizeof(vb),
                     "secsolve_verify: %s deadAt=%d x=%.1f y=%.3f vy=%.3f "
                     "dy=%.4f dvy=%.4f dx=%.4f",
                     g_leafVerified ? "OK" : "FAIL", g_leafDeadAt, verifyX,
                     verifyY, verifyVy,
                     verifyY - (double)g_nodes[(size_t)foundLeaf].y,
                     verifyVy - (double)g_nodes[(size_t)foundLeaf].vy,
                     verifyX - (double)g_nodes[(size_t)foundLeaf].x);
            writeResult(vb);
            char sb[280];
            snprintf(sb, sizeof(sb),
                     "secsolve_split: depth=%d/%d x %.3f->%.3f y %.3f->%.3f "
                     "vy %.3f->%.3f",
                     splitAt, foundDepth, splitNodeX, splitRepX,
                     splitNodeY, splitRepY, splitNodeVy, splitRepVy);
            writeResult(sb);
        }
        if (foundLeaf >= 0)
            writeResult("secsolve_inputs: " + inputsOf(foundLeaf));
        if (g_secRung) {
            g_secRung = false;
            // cfg dpsecstate: the probe's answer is a verdict, not a plan (repair.hpp stateFire).
            const int stateKind = g_secState;
            g_secState = 0;
            if (stateKind) {
                secsolve::g_forceP1Size = -1;
                secsolve::g_keepP1Size = -1;
            }
            secsolve::g_on = false;
            secsolve::g_rungCoin = -1;
            if (unverLeaves > 0)
                writeResult("secrung: passed over " + std::to_string(unverLeaves)
                            + " leaf/leaves the plain replay did not reproduce");
            g_cfg.practiceAt = g_secSavePractice;
            g_cfg.checkpointAt = g_secSaveCkpt;
            g_cfg.fastloops = g_secSaveFastloops;
            g_cfg.maxAttempts = g_secSaveMaxAttempts;
            secsolve::g_targetDepth = g_secSaveTargetDepth;
            if (foundLeaf >= 0 && stateKind != 1) {
                const std::vector<uint8_t> seq = secsolve::inputVecOf(foundLeaf);
                const long long base = (long long)g_ckptTick;
                std::vector<InputCmd> plan;
                uint8_t held = 0;
                for (const InputCmd& c : g_cfg.inputs)
                    if ((long long)c.step < base) { plan.push_back(c); held = c.down ? 1 : 0; }
                for (size_t i = 0; i < seq.size(); ++i)
                    if (seq[i] != held) {
                        plan.push_back(InputCmd{(int)(base + (long long)i), seq[i] != 0});
                        held = seq[i];
                    }
                if (g_cfg.dpSecReuse && stateKind != 2) {
                    dpsolve::g_rjKeepTarget = true;
                    writeResult("secrung: the rest of the plan stays the rejoin target of the "
                                "search after the splice");
                }
                g_cfg.inputs = plan;
                dpsolve::g_plan = plan;
                dpsolve::g_anchorT = base;
                dpsolve::g_anchorX = anchors::row(base) ? anchors::row(base)->x : 0.f;
                // The splice reaches its target, not the end, and joins nothing.
                dpsolve::g_lastTailSolved = false;
                dpsolve::g_planClaimsGoal = false;
                dpsolve::g_rjTailJoined = false;
                {
                    std::error_code ec;
                    std::filesystem::remove(dpsolve::g_planPath + ".trace.csv", ec);
                    std::filesystem::remove(dpsolve::g_tailPath + ".trace.csv", ec);
                }
                g_secPin = std::max(base + (long long)seq.size(), g_secPinWall);
                {   // the run counts a flight of this plan as the section search's (dprun via=secsolve)
                    std::vector<std::pair<long long, int>> se;
                    for (const InputCmd& c : plan) se.emplace_back((long long)c.step, c.down ? 1 : 0);
                    dpbridge::runNoteSplice(se, base);
                }
                if (stateKind == 2)   // cfg dpsecstate: the splice is the way on now
                    dpsolve::stateAdopt(plan, base + (long long)seq.size(),
                                        g_nodes[(size_t)foundLeaf].x);
                char rb[224];
                snprintf(rb, sizeof(rb),
                         "secrung: spliced %zu ticks from t=%lld (%zu inputs now), pinned at "
                         "t=%lld - the loop carries on",
                         seq.size(), base, plan.size(), (long long)g_secPin);
                writeResult(rb);
            } else {
                dpsolve::g_plan = g_cfg.inputs;
                writeResult(stateKind == 1
                                ? std::string("secrung: the probe ") + (foundLeaf >= 0 ? "crossed" : "did not cross")
                                      + " - a verdict, nothing is spliced; the loop carries on from the deepest plan"
                                : std::string("secrung: the search found nothing - the loop carries on from the "
                                              "deepest plan"));
            }
            const bool ownTiers = stateKind == 1 || stateKind == 2;
            const long long snapReach = (long long)g_ckptTick + deepest;
            const bool reachedDeath = g_cfg.secRedoReach && secsolve::g_spineUntil > 0
                                      && snapReach >= secsolve::g_spineUntil - secsolve::kSpineSlack;
            if (secsolve::g_lieSkipPsnap && secsolve::g_psnapLied && secsolve::g_wsnapHeld > 0
                && foundLeaf < 0 && !secsolve::liedWindow(secsolve::g_startTick)) {
                secsolve::g_liedWindows.push_back(secsolve::g_startTick);
                writeResult("secrung: window t=" + std::to_string(secsolve::g_startTick)
                            + " marked (cfg seclieskippsnap): its later asks start with the wsnap set");
            }
            const bool redoWsnap = secsolve::g_wsnapHeld > 0 && foundLeaf < 0 && stateKind == 0
                                   && g_secLastReq.start >= 0;
            const bool redoCp = !redoWsnap && secsolve::g_psnapLied && foundLeaf < 0 && stateKind == 0
                                && g_secLastReq.start >= 0 && !reachedDeath && g_cfg.secCpFallback;
            if (redoWsnap) {
                // (no checkpoint re-ask line: the wsnap re-ask comes first)
            } else if (secsolve::g_psnapLied && foundLeaf < 0 && stateKind == 0 && reachedDeath)
                writeResult("secrung: not asked again on checkpoints - the snapshot search lived to t="
                            + std::to_string(snapReach) + " against the plan's verified death at t="
                            + std::to_string(secsolve::g_spineUntil) + " (cfg secredoreach)");
            else if (secsolve::g_psnapLied && foundLeaf < 0 && stateKind == 0 && !g_cfg.secCpFallback)
                writeResult("secrung: not asked again on checkpoints (cfg seccpfallback=0) - the loop "
                            "carries on from the deepest plan");
            if (redoCp || redoWsnap) {
                g_secReqStart = g_secLastReq.start;
                g_secReqTarget = g_secLastReq.target;
                g_secReqDepth = g_secLastReq.depth;
                g_secReqHorizon = g_secLastReq.horizon;
                g_secReqCap = g_secLastReq.cap;
                // a coin wall's rung is never checked on checkpoints; the wsnap re-ask keeps its coin
                g_secReqCoin = redoWsnap ? g_secLastReq.coin : -1;
                g_secReqRung = true;
                g_secReqForceCp = redoCp;
                g_secReqForceWsnap = redoWsnap;
                if (redoWsnap) g_secReqSpineUntil = secsolve::g_spineUntil;
                g_secReqPending = true;
                if (redoCp)
                    writeResult("secrung: asking the same window again on checkpoints");
                else
                    writeResult("secrung: asking the same window again with its wsnap set of "
                                + std::to_string(secsolve::g_wsnapHeld)
                                + " objects (cfg secwsnapgate) - the plain snapshot search found nothing");
            } else {
                const bool cpRedo = g_secCpRedo;
                dpsolve::autoRungDone(g_ckptTick, steps, secsolve::g_snapOn,
                                      ownTiers ? true : foundLeaf >= 0,
                                      !ownTiers && !cpRedo && capTotal > 0
                                          && (g_secReqCap <= 0
                                              || secsolve::g_cap >= (size_t)g_secReqCap));
                if (ownTiers) dpsolve::stateRungDone(stateKind, foundLeaf >= 0);
            }
            g_secCpRedo = false;
            secsolve::g_wsnapForce = false;
            secsolve::g_wsnapHeld = 0;
            if (g_ckpt) { g_ckpt->release(); g_ckpt = nullptr; }
            g_ckptTick = -1;
            g_headHeld = 0;
            g_practiceOn = false;
            dpsolve::g_stop = false;     // the loop may spawn solves again
            g_paused = redoCp || redoWsnap;   // ...unless a redo is queued: held until it is taken
            g_forceCleanStart = true;    // drop the search's practice mode and checkpoints
            g_hudPhase = "secrung: replaying the spliced plan";
            if (secsolve::g_bookFirst) {
                coinBook.restore();
                coinBook.armed = false;
            }
            if (auto* pl = PlayLayer::get()) pl->resetLevel();
            // ...and only then the screen and the visibility pass (secRenderHold).
            secRenderRelease();
            co_return;                   // the search runs as a coroutine (see the slices)
        }
        endSession("secsolve");
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        ++g_pcCalls;   // sole direct indicator that physics runs (goes to the heartbeat)
        if (!g_prelude.empty() && g_started && !g_sessionOver && !g_preludeResetPending
            && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            const size_t pidx = (size_t)(g_attempt > 0 ? g_attempt - 1 : 0);
            if (pidx < g_prelude.size() && g_prelude[pidx].until >= 0 && g_tick >= g_prelude[pidx].until
                && m_player1 && !m_player1->m_isDead) {
                g_preludeResetPending = true;
                g_preludePendingAttempt = g_attempt;
                writeResult("sequence: prelude attempt " + std::to_string(g_attempt) + " restarts at tick " + std::to_string((long long)g_tick));
            }
        }
        if (!g_attemptFed && !secsolve::g_active
            && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            g_attemptFed = true;
            g_nextInputAtStart = g_nextInput;
        }
        if (secsolve::g_active) {
            if (secsolve::g_feed != secsolve::g_held) {
                g_injecting = true;
                this->handleButton(secsolve::g_feed != 0, 1, true);
                g_injecting = false;
                secsolve::g_held = secsolve::g_feed;
            }
        } else if (g_started && !g_sessionOver
                   && static_cast<GJBaseGameLayer*>(PlayLayer::get()) == this) {
            while (g_nextInput < g_cfg.inputs.size()
                   && g_cfg.inputs[g_nextInput].step <= g_tick) {
                auto& in = g_cfg.inputs[g_nextInput];
                ev("INJECT_handleButton", in.step, in.down);
                g_injecting = true;
                this->handleButton(in.down, 1 /*jump*/, true /*player1*/);
                g_injecting = false;
                if (in.down) orbtrace::g_lastPress = g_tick;
                ++solver::g_injThisAttempt;
                ++g_nextInput;
            }
            while (g_nextToggle < g_cfg.toggles.size()
                   && g_cfg.toggles[g_nextToggle].step <= g_tick) {
                auto& tg = g_cfg.toggles[g_nextToggle];
                ev("INJECT_toggle", tg.step);
                auto* p = m_player1;
                if (p) {
                    if (tg.mode == "ship") p->toggleFlyMode(true, true);
                    else if (tg.mode == "ball") p->toggleRollMode(true, true);
                    else if (tg.mode == "ufo") p->toggleBirdMode(true, true);
                    else if (tg.mode == "wave") p->toggleDartMode(true, true);
                    else if (tg.mode == "robot") p->toggleRobotMode(true, true);
                    else if (tg.mode == "spider") p->toggleSpiderMode(true, true);
                    else if (tg.mode == "swing") p->toggleSwingMode(true, true);
                    else if (tg.mode == "cube") {
                        p->toggleFlyMode(false, true);
                    }
                }
                ++g_nextToggle;
            }
        }
        ev("processCommands", dt, isHalfTick, isLastTick);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (static_cast<GJBaseGameLayer*>(PlayLayer::get()) != this) return;
        ++g_tick;
        const bool inSearch = secsolve::g_active;
        const long long tEff = inSearch ? g_ckptTick + g_stepDepth : g_tick;
        const bool roboLine = g_cfg.roboDbg0 >= 0 && m_player1
                              && (inSearch ? g_stepSpine : true);
        if (roboLine && tEff >= g_cfg.roboDbg0 && tEff <= g_cfg.roboDbg1) {
            const auto* lb = reinterpret_cast<const uint8_t*>(this);
            char rb[384];
            snprintf(rb, sizeof(rb),
                     "robodbg: t=%lld sec=%d d=%d y=%.4f vy=%.4f accel=%.6f "
                     "pad=%d ground=%d held=%d x=%.4f spd=%.4f left=%d side=%d plat=%d "
                     "flip=%d mirror=%.5f dt=%.6f",
                     (long long)tEff, inSearch ? 1 : 0,
                     inSearch ? g_stepDepth : -1,
                     m_player1->getPositionY(), m_player1->m_yVelocity,
                     m_player1->m_accelerationOrSpeed,
                     m_player1->m_touchedPad ? 1 : 0,
                     m_player1->m_isOnGround ? 1 : 0, secsolve::g_held,
                     m_player1->getPositionX(), m_player1->m_playerSpeed,
                     m_player1->m_isGoingLeft ? 1 : 0, m_player1->m_isSideways ? 1 : 0,
                     m_player1->m_isPlatformer ? 1 : 0, lb[gdoff::kLayerUnkBool10] ? 1 : 0,
                     *reinterpret_cast<const float*>(lb + gdoff::kLayerLevelFlipping), (double)dt);
            writeResult(rb);
        }
        if (g_cfg.snapVerify > 0 && m_player1) {
            const SnapState now{(long long)g_tick,
                                m_player1->getPositionX(),
                                m_player1->getPositionY(),
                                m_player1->m_yVelocity};
            if (g_snapProbe) g_snapProbe->push_back(now);
            else if (!g_snapVerified)
                for (auto& s : g_snaps)
                    if (g_tick > s.tick && g_tick <= s.tick + g_cfg.snapVerify)
                        s.head.push_back(now);
        }
        if (grouptrace::g_on && g_started && !g_sessionOver && !secsolve::g_active)
            grouptrace::tick(g_tick);
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver && m_player1) {
            static int lastMode = -1;
            static float lastSize = -1.f;
            const int md = m_player1->m_isShip ? 1 : m_player1->m_isBall ? 2
                         : m_player1->m_isBird ? 3 : m_player1->m_isDart ? 4
                         : m_player1->m_isRobot ? 5 : m_player1->m_isSpider ? 6
                         : m_player1->m_isSwing ? 7 : 0;
            if (md != lastMode || m_player1->m_vehicleSize != lastSize) {
                lastMode = md; lastSize = m_player1->m_vehicleSize;
                CCRect pr = m_player1->getObjectRect();
                char b[192];
                snprintf(b, sizeof(b),
                    "pbox: t=%lld mode=%d size=%.3f rect=(%.3f,%.3f,%.3f,%.3f)",
                    (long long)g_tick, md, m_player1->m_vehicleSize,
                    pr.origin.x, pr.origin.y, pr.size.width, pr.size.height);
                writeResult(b);
            }
        }
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver && m_player1
            && g_tick >= g_cfg.hbFrom
            && (g_cfg.hbTo == 0 || g_tick <= g_cfg.hbTo)) {
            const CCRect pr = m_player1->getObjectRect();
            CCRect q = pr;
            q.origin.x -= 30.f; q.origin.y -= 30.f;
            q.size.width += 60.f; q.size.height += 60.f;
            CCArray* a = this->damagingObjectsInRect(q, true);
            {
                char h[160];
                snprintf(h, sizeof(h),
                    "dmgn: t=%lld n=%d q=(%.1f,%.1f,%.1f,%.1f)",
                    (long long)g_tick, a ? (int)a->count() : -1,
                    q.origin.x, q.origin.y, q.size.width, q.size.height);
                writeResult(h);
            }
            if (CCArray* s = this->staticObjectsInRect(q, true)) {
                for (auto* o : CCArrayExt<GameObject*>(s)) {
                    if (!o) continue;
                    const CCRect r = o->getObjectRect();
                    char b[224];
                    snprintf(b, sizeof(b),
                        "stat: t=%lld uid=%d id=%d type=%d rect=(%.3f,%.3f,%.3f,%.3f)",
                        (long long)g_tick, (int)o->m_uniqueID, (int)o->m_objectID,
                        (int)o->m_objectType, r.origin.x, r.origin.y,
                        r.size.width, r.size.height);
                    writeResult(b);
                }
            }
            if (a) {
                for (auto* o : CCArrayExt<GameObject*>(a)) {
                    if (!o) continue;
                    const CCRect r = o->getObjectRect();
                    char b[224];
                    snprintf(b, sizeof(b),
                        "dmg: t=%lld uid=%d id=%d type=%d rect=(%.3f,%.3f,%.3f,%.3f)"
                        " prect=(%.3f,%.3f,%.3f,%.3f)",
                        (long long)g_tick, (int)o->m_uniqueID, (int)o->m_objectID,
                        (int)o->m_objectType, r.origin.x, r.origin.y,
                        r.size.width, r.size.height,
                        pr.origin.x, pr.origin.y, pr.size.width, pr.size.height);
                    writeResult(b);
                }
            }
        }
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver && m_player1) {
            static int pobbLines = 0;
            if (++pobbLines <= 40000) {
                if (OBB2D* ob = m_player1->getOrientedBox()) {
                    const auto& c = ob->m_corners;
                    const char* pb = reinterpret_cast<const char*>(m_player1);
                    const int f1 = (int)(unsigned char)pb[gdoff::kPlayerUpsideDown];
                    const int f2 = (int)(unsigned char)pb[gdoff::kPlayerGoingLeft];
                    const int f3 = (int)(unsigned char)pb[gdoff::kPlayerSideways];
                    float rmul = 0.f, rspd = 0.f;
                    std::memcpy(&rmul, pb + gdoff::kPlayerGravityMod, sizeof(float));
                    std::memcpy(&rspd, pb + gdoff::kPlayerRotationSpeed, sizeof(float));
                    char b[512];
                    snprintf(b, sizeof(b),
                        "pobb: t=%lld size=%.3f rot=%.3f "
                        "c=(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f)(%.3f,%.3f) "
                        "f=%d,%d,%d mul=%.4f rspd=%.4f",
                        (long long)g_tick, m_player1->m_vehicleSize,
                        m_player1->getRotation(),
                        c[0].x, c[0].y, c[1].x, c[1].y,
                        c[2].x, c[2].y, c[3].x, c[3].y,
                        f1, f2, f3, rmul, rspd);
                    writeResult(b);
                }
            }
        }
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver && m_player1
            && g_tick >= g_cfg.hbFrom
            && (g_cfg.hbTo == 0 || g_tick <= g_cfg.hbTo)) {
            const char* pb = reinterpret_cast<const char*>(m_player1);
            // 0x600-0xa20 in chunks of 0x80 bytes (384 characters per line)
            for (int base = (int)gdoff::kPlayerFlagsDumpBegin;
                 base < (int)gdoff::kPlayerFlagsDumpEnd; base += 0x80) {
                char b[600];
                int n = snprintf(b, sizeof(b), "pflags: t=%lld o=%03x",
                                 (long long)g_tick, base);
                for (int off = base; off < base + 0x80
                                     && n < (int)sizeof(b) - 8; ++off)
                    n += snprintf(b + n, sizeof(b) - n, " %02x",
                                  (unsigned)(unsigned char)pb[off]);
                writeResult(b);
            }
        }
        if (g_cfg.standTrace && g_started && !g_sessionOver && m_player1) {
            static int lastSnap = -2, lastSlope = -2, lastTgt = -2;
            auto* p = m_player1;
            GameObject* sn = p->m_objectSnappedTo;
            GameObject* sl = static_cast<GameObject*>(p->m_currentSlope);
            GameObject* tg =
                *reinterpret_cast<GameObject**>((char*)p + gdoff::kPlayerCurrentSlope2);
            const double tgLo =
                *reinterpret_cast<double*>((char*)p + gdoff::kPlayerCollidedBottomMaxY);
            const double tgHi =
                *reinterpret_cast<double*>((char*)p + gdoff::kPlayerCollidedLeftMaxX);
            const int snId = sn ? sn->m_uniqueID : -1;
            const int slId = sl ? sl->m_uniqueID : -1;
            const int tgId = tg ? tg->m_uniqueID : -1;
            if (snId != lastSnap || slId != lastSlope || tgId != lastTgt) {
                lastSnap = snId; lastSlope = slId; lastTgt = tgId;
                char b[352];
                auto pos = [](GameObject* o, char* dst, size_t n) {
                    if (!o) { snprintf(dst, n, "-"); return; }
                    auto q = o->getPosition();
                    auto r = o->getObjectRect();
                    snprintf(dst, n, "uid%d id%d type%d @%.2f,%.2f %.2fx%.2f",
                             o->m_uniqueID, o->m_objectID, (int)o->m_objectType,
                             q.x, q.y, r.size.width, r.size.height);
                };
                char sb[160], lb[160], tb[160];
                pos(sn, sb, sizeof(sb));
                pos(sl, lb, sizeof(lb));
                pos(tg, tb, sizeof(tb));
                char b2[560];
                snprintf(b2, sizeof(b2),
                         "stand: t=%lld y=%.3f onG=%d snap=[%s] slope=[%s] "
                         "tgt=[%s] lo=%.3f hi=%.3f",
                         (long long)g_tick, m_player1->getPositionY(),
                         (int)m_player1->m_isOnGround, sb, lb, tb, tgLo, tgHi);
                (void)b;
                writeResult(b2);
            }
        }
        if (g_started && !g_sessionOver && m_player1) {
            speedgate::build(this);
            speedgate::check(m_player1, g_tick);   // detect and report only (no side effects)
        }
        if (g_syncProbe && g_started && !g_sessionOver && m_player1) {
            static float s_lastSpd = -1.f;
            float spd = (float)m_player1->m_playerSpeed;
            if (spd != s_lastSpd) {
                s_lastSpd = spd;
                char sp[160];
                snprintf(sp, sizeof(sp), "speed: %.2f at tick=%lld x=%.0f y=%.0f",
                    spd, (long long)g_tick, m_player1->getPositionX(),
                    m_player1->getPositionY());
                writeResult(sp);
            }
        }
        if (g_started && !g_sessionOver && m_player1)
            deadstat::tick(m_player1->m_isDead);
        if (solver::g_gateTrace && !solver::g_gateObjs.empty()
            && g_tick >= solver::g_gateT0 && g_tick <= solver::g_gateT1
            && (solver::g_gateStride <= 1 || (g_tick % solver::g_gateStride) == 0)) {
            if (!solver::g_gateHdrDone) {
                solver::g_gateHdrDone = true;
                for (auto* o : solver::g_gateObjs) {
                    if (!o) continue;
                    char hb[160];
                    auto r = o->getObjectRect();
                    snprintf(hb, sizeof(hb),
                        "gateobj: id=%d uid=%d x=%.2f y=%.2f rect=%.2f,%.2f,%.2f,%.2f",
                        o->m_objectID, o->m_uniqueID,
                        o->getPositionX(), o->getPositionY(),
                        r.origin.x, r.origin.y, r.size.width, r.size.height);
                    writeResult(hb);
                }
            }
            std::string s = "gate: tick=" + std::to_string(g_tick);
            if (m_player1) {
                char pb[224];
                // Emit the real hitbox and GD's own collision record (lcol)
                auto pr = m_player1->getObjectRect();
                auto* g = m_player1->m_maybeLastGroundObject;
                snprintf(pb, sizeof(pb),
                    " px=%.1f py=%.1f prect=%.2f,%.2f,%.2f,%.2f ground=%d@%.0f,%.0f"
                    " lcol=%d,%d,%d,%d lk=%d%d",
                    m_player1->getPositionX(), m_player1->getPositionY(),
                    pr.origin.x, pr.origin.y, pr.size.width, pr.size.height,
                    g ? g->m_objectID : -1,
                    g ? g->getPositionX() : 0.f, g ? g->getPositionY() : 0.f,
                    m_player1->m_lastCollisionBottom, m_player1->m_lastCollisionTop,
                    m_player1->m_lastCollisionLeft, m_player1->m_lastCollisionRight,
                    (int)m_player1->m_isLocked, (int)m_player1->m_controlsDisabled);
                s += pb;
            }
            for (auto* o : solver::g_gateObjs) {
                if (!o) continue;
                char b[96];
                snprintf(b, sizeof(b), " [%d %.0f,%.0f %d%d%d%d%d%d%d]",
                    o->m_objectID, o->getPositionX(), o->getPositionY(),
                    (int)o->m_isDisabled, (int)o->m_isHide, (int)o->isVisible(),
                    (int)o->m_isGroupDisabled, (int)o->m_isGroupDisabledTemp,
                    (int)o->m_hasExtendedCollision, (int)o->m_isPassable);
                s += b;
            }
            writeResult(s);
        }
        if (solver::g_colProbe && m_player1 && !g_sessionOver) {
            if (g_tick == solver::g_cpT && !solver::g_cpInjected
                && !m_player1->m_isDead) {
                solver::g_cpInjected = true;
                const double y = solver::g_cpY0
                    + (double)(g_attempt - 1) * solver::g_cpYStep;
                m_player1->setPositionY((float)y);
                m_player1->m_yVelocity = solver::g_cpVy;
                solver::g_cpEndTick = g_tick + solver::g_cpN;
                char ib[128];
                snprintf(ib, sizeof(ib), "cprobe: inject a=%d t=%lld x=%.2f y=%.3f vy=%.3f",
                    g_attempt, (long long)g_tick, m_player1->getPositionX(),
                    y, solver::g_cpVy);
                writeResult(ib);
            }
            if (solver::g_cpInjected && g_tick > solver::g_cpT
                && g_tick <= solver::g_cpEndTick) {
                char cb[176];
                snprintf(cb, sizeof(cb),
                    "cprobe: a=%d t=%lld x=%.2f y=%.3f vy=%.3f dead=%d lcol=%d,%d,%d,%d",
                    g_attempt, (long long)g_tick,
                    m_player1->getPositionX(), m_player1->getPositionY(),
                    (double)m_player1->m_yVelocity, (int)m_player1->m_isDead,
                    m_player1->m_lastCollisionBottom, m_player1->m_lastCollisionTop,
                    m_player1->m_lastCollisionLeft, m_player1->m_lastCollisionRight);
                writeResult(cb);
                if (g_tick == solver::g_cpEndTick && !m_player1->m_isDead) {
                    // Observation done. If still alive, kill and advance to the next attempt
                    if (auto* pl = PlayLayer::get())
                        pl->destroyPlayer(m_player1, m_player1);
                }
            }
        }
        if (m_player1 && !g_sessionOver && !m_player1->m_isDead) {
            while (g_injectNext < g_injects.size()
                   && g_injects[g_injectNext].tick < g_tick) ++g_injectNext;
            while (g_injectNext < g_injects.size()
                   && g_injects[g_injectNext].tick == g_tick) {
                auto& s = g_injects[g_injectNext++];
                if (s.hasX)  m_player1->setPositionX((float)s.x);
                if (s.hasY)  m_player1->setPositionY((float)s.y);
                if (s.hasVy) m_player1->m_yVelocity = s.vy;
                char ib[192];
                snprintf(ib, sizeof(ib),
                    "inject: t=%lld x=%.3f y=%.3f vy=%.3f mode=%s",
                    (long long)g_tick, m_player1->getPositionX(),
                    m_player1->getPositionY(), (double)m_player1->m_yVelocity,
                    modeStr(m_player1));
                writeResult(ib);
            }
            if (g_stopAt >= 0 && g_tick == g_stopAt && !g_stopFired) {
                char sb[256];
                snprintf(sb, sizeof(sb),
                    "stop: attempt=%d tick=%lld x=%.3f y=%.3f yvel=%.3f mode=%s "
                    "onGround=%d vsize=%.3f",
                    g_attempt, (long long)g_tick, m_player1->getPositionX(),
                    m_player1->getPositionY(), (double)m_player1->m_yVelocity,
                    modeStr(m_player1), (int)m_player1->m_isOnGround,
                    (double)m_player1->m_vehicleSize);
                writeResult(sb);
                g_stopFired = true;   // never twice per attempt (the directive itself is kept)
                if (auto* pl = PlayLayer::get())
                    pl->destroyPlayer(m_player1, m_player1);
            }
        }
        if (solver::ckpttrace::g_on && g_started && !g_sessionOver && m_player1) {
            int tryNow = m_player1->m_shouldTryPlacingCheckpoint ? 1 : 0;
            int toNow  = m_player1->m_checkpointTimeout ? 1 : 0;
            if (tryNow != solver::ckpttrace::g_lastTry
                || toNow != solver::ckpttrace::g_lastTimeout) {
                if (++solver::ckpttrace::g_flagLines > solver::ckpttrace::kFlagCap) {
                    if (solver::ckpttrace::g_flagLines == solver::ckpttrace::kFlagCap + 1)
                        writeResult("ckpt: flag   (capped at "
                            + std::to_string(solver::ckpttrace::kFlagCap)
                            + " lines; further transitions not logged)");
                    solver::ckpttrace::g_lastTry = tryNow;
                    solver::ckpttrace::g_lastTimeout = toNow;
                } else {
                char cb[224];
                snprintf(cb, sizeof(cb),
                    "ckpt: flag   tick=%lld x=%.2f try=%d->%d timeout=%d->%d "
                    "lastCkptT=%.6f totalT=%.6f",
                    (long long)g_tick, m_player1->getPositionX(),
                    solver::ckpttrace::g_lastTry, tryNow,
                    solver::ckpttrace::g_lastTimeout, toNow,
                    m_player1->m_lastCheckpointTime, m_player1->m_totalTime);
                writeResult(cb);
                solver::ckpttrace::g_lastTry = tryNow;
                solver::ckpttrace::g_lastTimeout = toNow;
                }
            }
        }
        if (g_started && !g_sessionOver && m_player1) {
            if ((long long)solver::g_log.size() <= g_tick)
                solver::g_log.resize(g_tick + 1);
            bool dual = m_gameState.m_isDualMode;
            if (dual && !solver::g_dualSeen) {
                solver::g_dualSeen = true;
                writeResult("dual: entered at tick=" + std::to_string((long long)g_tick)
                    + " x=" + std::to_string((int)m_player1->getPositionX())
                    + " p2=" + (m_player2 ? "yes" : "NO"));
            }
            solver::g_log[g_tick] = {
                m_player1->getPositionX(),
                m_player1->getPositionY(),
                (uint8_t)(m_player1->m_isOnGround ? 1 : 0),
                solver::modeOf(m_player1),
                (uint8_t)(m_player1->m_isOnGround2 ? 1 : 0),
                (float)m_player1->m_yVelocity,
                (uint8_t)(dual ? 1 : 0),
                (uint8_t)((dual && m_player2 && m_player2->m_isOnGround) ? 1 : 0)
            };
        }
        if (g_started && !g_sessionOver && m_player1) {
            static auto s_last = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - s_last).count() >= 10) {
                s_last = now;
                writeResult("replay: t=" + std::to_string(g_tick)
                    + " x=" + std::to_string(m_player1->getPositionX())
                    + " y=" + std::to_string(m_player1->getPositionY()));
            }
        }
        if (g_started && !g_sessionOver && m_player1 && !m_player1->m_isDead
            && !g_cfg.noDeath && !secsolve::g_active) {
            static float s_stallX = 0.f, s_stallY = 0.f;
            static long long s_stallSince = -1;
            if (m_player1->m_isLocked && m_player1->getPositionX() > 500.f
                && g_endzoneLockStart.time_since_epoch().count() == 0)
                g_endzoneLockStart = std::chrono::steady_clock::now();
            const double lockWall =
                (m_player1->m_isLocked
                 && g_endzoneLockStart.time_since_epoch().count() != 0)
                    ? std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - g_endzoneLockStart).count()
                    : -1.0;
            const bool endzoneGrace = lockWall >= 0.0 && lockWall < ENDZONE_KILL_AFTER_SEC;
            const bool endzoneHung = lockWall >= ENDZONE_KILL_AFTER_SEC;
            static int s_overFired = -1;   // once per attempt -- this runs per TICK, and
                                           // unlatched it wrote 1,800 lines per frame
            if (g_tick > 40000 && s_overFired != g_attempt && !endzoneGrace) {
                s_overFired = g_attempt;
                char ob2[160];
                snprintf(ob2, sizeof(ob2), "stall: attempt=%d has run %lld ticks without "
                         "ending - forcing it over", g_attempt, (long long)g_tick);
                writeResult(ob2);
                if (auto* pl = PlayLayer::get()) {
                    pl->destroyPlayer(m_player1, m_player1);
                    g_stallResetPending = true;
                }
            }
            const float px = m_player1->getPositionX();
            const float py = m_player1->getPositionY();
            if (s_stallSince < 0 || px != s_stallX || py != s_stallY
                || g_tick < s_stallSince) {
                s_stallX = px; s_stallY = py; s_stallSince = g_tick;
            } else if ((g_tick - s_stallSince >= 3000 && !endzoneGrace)
                       || (endzoneHung && g_tick - s_stallSince >= 300)) {
                char sb[256];
                snprintf(sb, sizeof(sb),
                    "stall: attempt=%d the player has not moved since t=%lld "
                    "(x=%.3f y=%.3f) - ending the attempt",
                    g_attempt, (long long)s_stallSince, (double)px, (double)py);
                writeResult(sb);
                s_stallSince = -1;
                if (auto* pl = PlayLayer::get()) {
                    pl->destroyPlayer(m_player1, m_player1);
                    g_stallResetPending = true;
                }
            }
        }
        if (g_cfg.endTrace && g_started && !g_sessionOver && m_player1) {
            static long long s_lastTick = -1;
            static int s_prev = -1, s_lines = 0;
            if (g_tick <= s_lastTick) { s_prev = -1; s_lines = 0; } // the attempt changed
            s_lastTick = g_tick;
            auto* pl = PlayLayer::get();
            int lk = m_player1->m_isLocked ? 1 : 0;
            int co = m_player1->m_controlsDisabled ? 1 : 0;
            int cp = (pl && pl->m_hasCompletedLevel) ? 1 : 0;
            int dd = m_player1->m_isDead ? 1 : 0;
            int st = lk | (co << 1) | (cp << 2) | (dd << 3);
            if (st != s_prev && ++s_lines <= 40) {
                writeResult("endtrace: t=" + std::to_string(g_tick)
                    + " x=" + std::to_string(m_player1->getPositionX())
                    + " y=" + std::to_string(m_player1->getPositionY())
                    + " vy=" + std::to_string((float)m_player1->m_yVelocity)
                    + " locked=" + std::to_string(lk)
                    + " ctrlOff=" + std::to_string(co)
                    + " completed=" + std::to_string(cp)
                    + " dead=" + std::to_string(dd)
                    + " mode=" + modeStr(m_player1));
            }
            s_prev = st;
        }
        if (g_started && !g_sessionOver && g_cfg.coinMode && m_player1) {
            float px = m_player1->getPositionX();
            float py = m_player1->getPositionY();
            float dxTick = px - solver::g_prevTickX;
            bool completed = false;
            if (auto* pl = PlayLayer::get()) completed = pl->m_hasCompletedLevel;
            bool uncontrolled = m_player1->m_isLocked || m_player1->m_controlsDisabled;
            bool normalMotion = solver::g_prevTickX > -1e8f
                && dxTick > 0.2f && dxTick < 5.0f && !completed && !uncontrolled;
            solver::g_prevTickX = px;
            if (g_cfg.routePrereq && route::g_built && !secsolve::g_active && !secsolve::g_on)
                route::sample(g_tick);
            if (solver::g_coinPosLog.size() == solver::g_coins.size()
                && solver::g_coinObjs.size() == solver::g_coins.size())
                for (size_t i = 0; i < solver::g_coins.size(); ++i) {
                    GameObject* o = solver::g_coinObjs[i];
                    if (!o) continue;
                    const auto lr = o->getObjectRect();
                    const float lx = lr.origin.x + lr.size.width * 0.5f;
                    const float ly = lr.origin.y + lr.size.height * 0.5f;
                    const uint8_t on = o->m_isGroupDisabled ? 0 : 1;
                    auto& lg = solver::g_coinPosLog[i];
                    if (lg.empty() || lg.back().x != lx || lg.back().y != ly || lg.back().on != on)
                        lg.push_back({(long long)g_tick, lx, ly, on});
                }
            if (solver::g_coinLiveSaid.size() != solver::g_coins.size())
                solver::g_coinLiveSaid.assign(solver::g_coins.size(), 0);
            for (size_t i = 0; i < solver::g_coins.size(); ++i) {
                const bool watch = g_cfg.coinWatch && (g_tick % 1200) == 0;
                if (solver::g_coinLiveSaid[i] && !watch) continue;
                if (!watch && std::abs(px - solver::g_coins[i].x) > 80.f) continue;
                solver::g_coinLiveSaid[i] = 1;
                if (auto* arr = this->m_objects)
                    for (auto* o : CCArrayExt<GameObject*>(arr)) {
                        if (!o || o->m_uniqueID != solver::g_coins[i].uid) continue;
                        const auto lp = o->getPosition();
                        const auto lr = o->getObjectRect();
                        char lb[208];
                        snprintf(lb, sizeof(lb),
                                 "coinlive: t=%lld uid=%d load=(%.1f,%.1f) pos=(%.2f,%.2f) "
                                 "rect=(%.2f,%.2f) %.1fx%.1f disabled=%d",
                                 (long long)g_tick, solver::g_coins[i].uid,
                                 solver::g_coins[i].x, solver::g_coins[i].y, lp.x, lp.y,
                                 lr.origin.x + lr.size.width * 0.5f,
                                 lr.origin.y + lr.size.height * 0.5f,
                                 lr.size.width, lr.size.height,
                                 o->m_isGroupDisabled ? 1 : 0);
                        writeResult(lb);
                        break;
                    }
            }
            for (size_t i = 0; i < solver::g_coins.size()
                 && normalMotion && i < solver::g_coinPickupTick.size(); ++i) {
                if (solver::g_coinPickupTick[i] >= 0) continue;
                if (std::abs(px - solver::g_coins[i].x) <= solver::COIN_RADIUS
                    && std::abs(py - solver::g_coins[i].y) <= solver::COIN_RADIUS)
                    solver::g_coinPickupTick[i] = g_tick;
            }
        }
        if (g_started && !g_sessionOver && g_cfg.coinRoute && m_player1 && !m_player1->m_isDead
            && !solver::g_coinMissFired && !solver::g_hasRotGameplay
            && !dpsolve::g_recordAttempt && !dpsolve::g_running.load()
            && !secsolve::g_active && !secsolve::g_on
            && dpsolve::g_planClaimsGoal) {
            bool completed = false;
            if (auto* pl = PlayLayer::get()) completed = pl->m_hasCompletedLevel;
            const float px = m_player1->getPositionX();
            for (size_t i = 0; !completed && i < solver::g_coins.size()
                               && i < solver::g_coinGdTick.size(); ++i) {
                if (solver::g_coinGdTick[i] >= 0) continue;
                const auto& c = solver::g_coins[i];
                if (px <= c.x + c.hw + 15.f) continue;
                if (g_cfg.coinMissRev
                    && std::find(solver::g_coinNoMiss.begin(), solver::g_coinNoMiss.end(), c.uid)
                           != solver::g_coinNoMiss.end())
                    continue;
                if (g_cfg.coinMissMove && i < solver::g_coinMoveX.size()
                    && solver::g_coinMoveX[i] > -1e8f
                    && (px <= solver::g_coinMoveX[i]
                        || std::find(solver::g_coinNoMiss.begin(), solver::g_coinNoMiss.end(),
                                     c.uid) != solver::g_coinNoMiss.end()))
                    continue;
                char mb[192];
                snprintf(mb, sizeof(mb), "coinmiss: t=%lld x=%.2f y=%.2f coin=%zu uid=%d at "
                         "(%.1f,%.1f)", (long long)g_tick, px, m_player1->getPositionY(), i,
                         c.uid, c.x, c.y);
                writeResult(mb);
                solver::g_coinMissFired = true;   // once per attempt, however GD answers
                solver::g_coinMissIdx = (int)i;
                if (g_cfg.dpSolve) dpsolve::g_coinMissPending = true;   // only onDeath reads it
                if (auto* pl = PlayLayer::get()) pl->destroyPlayer(m_player1, m_player1);
                break;
            }
        }
        if (g_started && !g_sessionOver && g_cfg.coinRoute && m_player1 && !m_player1->m_isDead
            && !solver::g_coinMissFired && !solver::g_coinGates.empty()
            && !dpsolve::g_recordAttempt && !dpsolve::g_running.load() && !secsolve::g_active
            && !secsolve::g_on && dpsolve::g_planClaimsGoal) {
            const int ch =
                *reinterpret_cast<int const*>(reinterpret_cast<char const*>(this)
                                               + gdoff::kLayerCurrentChannel);
            const double wx = m_player1->getPositionX(), wy = m_player1->getPositionY();
            for (const solver::CoinGate& g : solver::g_coinGates) {
                if (ch != g.chan) continue;
                auto passed = [&](double px, double py) {
                    return g.dir == 1 ? wy >= py
                         : g.dir == 2 ? wy <= py
                         : g.dir == 3 ? wx <= px
                                      : wx >= px;
                };
                bool got = false;
                for (size_t i = 0; i < solver::g_coins.size() && i < solver::g_coinGdTick.size(); ++i)
                    if (solver::g_coins[i].uid == g.uid && solver::g_coinGdTick[i] >= 0) got = true;
                if (got) continue;
                const auto it = solver::g_itemCounts.find(g.item);
                const int have = it == solver::g_itemCounts.end() ? 0 : it->second;
                const bool shortAtShut = passed(g.x, g.y) && have < g.need;
                const bool pastCoin = g.miss && passed(g.mx, g.my);
                if (!shortAtShut && !pastCoin) continue;
                char mb[256];
                snprintf(mb, sizeof(mb), "coingatemiss: t=%lld x=%.2f y=%.2f coin uid=%d: %s"
                         " (item %d is %d of %d; chan %d, shut (%.0f,%.0f), far edge (%.0f,%.0f))",
                         (long long)g_tick, wx, wy, g.uid,
                         shortAtShut ? "window shut short" : "passed the coin",
                         g.item, have, g.need, g.chan, g.x, g.y, g.mx, g.my);
                writeResult(mb);
                solver::g_coinMissFired = true;   // once per attempt, as the miss above
                if (g_cfg.dpSolve) dpsolve::g_coinMissPending = true;
                if (auto* pl = PlayLayer::get()) pl->destroyPlayer(m_player1, m_player1);
                break;
            }
        }
        if (g_started && !g_sessionOver && g_cfg.dpSolve && m_player1 && !secsolve::g_active)
            anchors::record(this, g_tick);
        if (g_started && !g_sessionOver && !g_areaUids.empty() && this->m_objects
            && g_tick >= g_areaT0 && g_tick <= g_areaT1) {
            if (g_areaArr != this->m_objects || g_tick == g_areaT0
                || g_areaObjs.size() != g_areaUids.size()) {
                g_areaArr = this->m_objects;
                g_areaObjs.assign(g_areaUids.size(), nullptr);
                for (unsigned i = 0; i < g_areaArr->count(); ++i) {
                    auto* o = static_cast<GameObject*>(g_areaArr->objectAtIndex(i));
                    if (!o) continue;
                    for (size_t k = 0; k < g_areaUids.size(); ++k)
                        if (o->m_uniqueID == g_areaUids[k]) g_areaObjs[k] = o;
                }
            }
            for (size_t k = 0; k < g_areaObjs.size(); ++k) {
                if (!g_areaObjs[k]) continue;
                const auto r = g_areaObjs[k]->getObjectRect();
                const auto pos = g_areaObjs[k]->getPosition();
                char ab[160];
                snprintf(ab, sizeof(ab), "area: t=%lld uid=%d cx=%.4f cy=%.4f px=%.4f py=%.4f",
                         (long long)g_tick, g_areaUids[k],
                         r.origin.x + r.size.width * 0.5, r.origin.y + r.size.height * 0.5,
                         pos.x, pos.y);
                writeResult(ab);
            }
        }
        // cfg `slopetrace`: the slope-ride bytes at this same instant (see g_slopeT0).
        if (g_started && !g_sessionOver) anchors::slopeTrace(this, g_tick);
        if (g_started && !g_sessionOver && g_cfg.dpSolve && g_cfg.dpCheck)
            dpsolve::ckTick(g_tick);
        if (g_started && !g_sessionOver && m_player1 && !secsolve::g_active) {
            itermap::trackX(g_tick, m_player1->getPositionX());
            seekcp::noteX(g_tick, m_player1->getPositionX(), m_player1->getPositionY());   // cfg seekcp: head run kept, restored checked
        }
        if (g_started && !g_sessionOver) {
            const float mp = *reinterpret_cast<float const*>(
                reinterpret_cast<char const*>(this) + gdoff::kLayerMaxGameplayY);
            static float mpLast = -1.f;
            if (std::fabs(mp - mpLast) > 0.5f) {
                mpLast = mp;
                writeResult("maxplayy: t=" + std::to_string(g_tick)
                            + " v=" + std::to_string(mp));
            }
        }
        if (g_started && !g_sessionOver && !secsolve::g_active
            && !g_cfg.noTrace && g_dump.is_open() && m_player1) {
            auto* p = m_player1;
            auto pos = p->getPosition();
            g_dump << g_frame << ',' << g_attempt << ',' << g_tick
                   << ',' << pos.x << ',' << pos.y
                   << ',' << p->m_yVelocity << ',' << p->getRotation()
                   << ',' << modeStr(p) << ',' << p->m_isUpsideDown
                   << ',' << p->m_isOnGround << ',' << p->m_isOnGround2
                   << ',' << p->m_isDead << ',' << p->m_playerSpeed
                   << ',' << p->m_gravityMod << ',' << p->m_platformerXVelocity
                   // vsize: the size portal (1.0=normal 0.6=mini). Needed for re-anchoring
                   << ',' << p->m_vehicleSize
                   << ',' << (this->m_groundLayer
                                  ? this->m_groundLayer->getPositionY() : -1.f)
                   << ',' << (this->m_groundLayer2
                                  ? this->m_groundLayer2->getPositionY() : -1.f)
                   << ',' << (m_gameState.m_isDualMode ? 1 : 0)
                   << ',' << (m_player2 ? m_player2->getPositionY() : -1.f)
                   << ',' << (m_player2 ? m_player2->m_yVelocity : 0.f)
                   << ',' << (m_player2 ? (int)m_player2->m_isUpsideDown : 0)
                   << ',' << (m_player2 ? (int)m_player2->m_isOnGround : 0)
                   << ',' << (m_player2 ? (int)m_player2->m_isDead : 0)
                   << ',' << this->getMinPortalY()
                   << ',' << this->getMaxPortalY()
                   << ',' << (p->m_objectSnappedTo
                                  ? p->m_objectSnappedTo->m_uniqueID : -1)
                   << ',' << p->m_snapDistance
                   << ',' << *reinterpret_cast<const float*>(
                                 reinterpret_cast<const char*>(this) + gdoff::kLayerCameraZoom)
                   << ',' << (int)g_gameFrame
                   << ',' << (p->m_controlsDisabled ? 1 : 0)
                   << ',' << (this->m_objectLayer
                                  ? -this->m_objectLayer->getPositionX() : 0.f)
                   << ',' << (this->m_objectLayer
                                  ? -this->m_objectLayer->getPositionY() : 0.f)
                   << ',' << (m_player2 ? (int)m_player2->m_isOnGround2 : 0)
                   << ',' << (m_player2 ? modeStr(m_player2) : "-")
                   << ',' << (m_player2 ? m_player2->m_vehicleSize : 0.f)
                   << ',' << (m_player2 ? m_player2->getPositionX() : -1.f)
                   << ',' << [this]() {
                          static_assert(sizeof(gd::unordered_map<int, int>)
                                        == gdoff::kLayerSpawnChannel1 - gdoff::kLayerSpawnChannel0);
                          static_assert(sizeof(gd::unordered_map<int, bool>)
                                        == gdoff::kLayerSpawnChannel1 - gdoff::kLayerSpawnChannel0);
                          auto const* base = reinterpret_cast<char const*>(this);
                          const int ch =
                              *reinterpret_cast<int const*>(base + gdoff::kLayerCurrentChannel);
                          auto const& idx = *reinterpret_cast<gd::unordered_map<int, int> const*>(
                              base + gdoff::kLayerSpawnChannel0);
                          auto const& rev = *reinterpret_cast<gd::unordered_map<int, bool> const*>(
                              base + gdoff::kLayerSpawnChannel1);
                          auto const i = idx.find(ch);
                          auto const r = rev.find(ch);
                          return std::to_string(ch) + ','
                              + std::to_string(i == idx.end() ? -1 : i->second) + ','
                              + std::to_string(r == rev.end() ? -1 : (int)r->second);
                      }()
                   << ',' << [this]() -> std::string {
                          if (g_cfg.dpWatchFired.empty()) return "-";
                          static void const* layer = nullptr;
                          static long long attempt = -1;
                          static std::vector<std::pair<int, GameObject*>> objs;
                          if (layer != this || attempt != (long long)g_attempt
                              || objs.size() != g_cfg.dpWatchFired.size()) {
                              layer = this;
                              attempt = (long long)g_attempt;
                              objs.clear();
                              for (int uid : g_cfg.dpWatchFired) {
                                  GameObject* found = nullptr;
                                  if (m_objects)
                                      for (auto* o : CCArrayExt<GameObject*>(m_objects))
                                          if (o && o->m_uniqueID == uid) { found = o; break; }
                                  objs.push_back({uid, found});
                              }
                          }
                          std::string s;
                          for (auto const& [uid, o] : objs) {
                              if (!s.empty()) s += '|';
                              s += std::to_string(uid) + ':' + std::to_string(
                                  o ? (int)*(reinterpret_cast<uint8_t const*>(o) + gdoff::kObjGroupDisabled) : -1);
                          }
                          return s;
                      }()
                   << ',' << *reinterpret_cast<const float*>(
                                 reinterpret_cast<const char*>(this) + gdoff::kLayerUnkPoint17Y)
                   << ',' << (int)*(reinterpret_cast<const uint8_t*>(this) + gdoff::kLayerUnkBool1)
                   << ',' << *reinterpret_cast<const float*>(
                                 reinterpret_cast<const char*>(this) + gdoff::kLayerCameraOffsetY)
                   << ',' << (int)*(reinterpret_cast<const uint8_t*>(this) + gdoff::kLayerFreeMode)
                   << ',' << (int)*(reinterpret_cast<const uint8_t*>(this) + gdoff::kLayerDualMode)
                   << ',' << (m_player2 ? m_player2->getRotation() : 0.f)
                   << ',' << ((m_player2 && m_player2->m_controlsDisabled) ? 1 : 0)
                   << '\n';
        }
    }

    void processQueuedButtons(float dt, bool clearInputQueue) {
        ev("processQueuedButtons", dt, clearInputQueue);
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (object) {
            ++touchfire::g_calls;
            if (player == m_player1) {
                auto& m = touchfire::g_first;
                if (m.find(object->m_uniqueID) == m.end()) m[object->m_uniqueID] = (int)g_tick;
            } else {
                ++touchfire::g_p2;
            }
        }
        if (g_trigTrace && g_started && !g_sessionOver && object
            && player == m_player1) {
            static int s_logs = 0;
            if (++s_logs <= 60) {
                auto p = object->getPosition();
                writeResult("trigger: id=" + std::to_string(object->m_objectID)
                    + " x=" + std::to_string((int)p.x)
                    + " y=" + std::to_string((int)p.y)
                    + " tick=" + std::to_string((long long)g_tick)
                    + " attempt=" + std::to_string(g_attempt));
            }
        }
        if (g_cfg.hitboxTrace && g_started && !g_sessionOver && object
            && player == m_player1) {
            static int lines = 0;
            if (++lines <= 2000) {
                const auto orct = object->getObjectRect();
                const auto prct = player->getObjectRect();
                char b[256];
                snprintf(b, sizeof(b),
                         "ptt: t=%lld uid=%d id=%d orect=(%.3f,%.3f,%.3f,%.3f) "
                         "prect=(%.3f,%.3f,%.3f,%.3f)",
                         (long long)g_tick, object->m_uniqueID,
                         object->m_objectID,
                         orct.origin.x, orct.origin.y,
                         orct.size.width, orct.size.height,
                         prct.origin.x, prct.origin.y,
                         prct.size.width, prct.size.height);
                writeResult(b);
            }
        }
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_cfg.blockInput && g_started && !g_sessionOver && !g_injecting
            && !takeover::personDriving()) {
            ev("handleButton_BLOCKED", down, button, isPlayer1);
            return;
        }
        ev("handleButton", down, button, isPlayer1);
        if (!g_injecting && takeover::personDriving()) takeover::noteInput(down, button, isPlayer1);
        if (button == 1 && isPlayer1) g_btnDown = down;
        // cfg batchlog (print only): the tick each press and release reaches GD at
        if (itermap::g_batchFrom >= 0 && g_tick >= itermap::g_batchFrom && g_tick <= itermap::g_batchTo
            && g_started && !g_sessionOver)
            writeResult("batch: button t=" + std::to_string((long long)g_tick) + " down=" + std::to_string(down ? 1 : 0)
                        + " btn=" + std::to_string(button) + " p1=" + std::to_string(isPlayer1 ? 1 : 0)
                        + " run=" + (seekcp::g_from < 0 ? std::string("head") : "cp@" + std::to_string(seekcp::g_from)));
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    int checkCollisions(PlayerObject* player, float dt, bool ignoreDamage) {
        bool isP1 = (player == m_player1);
        if (isP1) ev("checkCollisions", dt);
        return GJBaseGameLayer::checkCollisions(player, dt, ignoreDamage);
    }

    void removeObjectFromSection(GameObject* object) {
        GJBaseGameLayer::removeObjectFromSection(object);
        static_assert(offsetof(GameObject, m_outerSectionIndex) == gdoff::kObjOuterSectionIndex,
                      "the index removeObjectFromSection tests on entry");
        if (secsolve::g_on && object) object->m_outerSectionIndex = -1;
    }

    void updateObjectSection(GameObject* object) {
        if (secsolve::g_active && object
            && !(std::isfinite(object->m_positionX) && std::isfinite(object->m_positionY)
                 && std::fabs(object->m_positionX) < secsolve::kPosLimit
                 && std::fabs(object->m_positionY) < secsolve::kPosLimit)) {
            if (secsolve::g_nonFinite++ == 0) {
                secsolve::g_nonFiniteUid = object->m_uniqueID;
                secsolve::g_nonFiniteId = object->m_objectID;
                secsolve::g_nonFiniteX = object->m_positionX;
                secsolve::g_nonFiniteY = object->m_positionY;
#ifdef GEODE_IS_WINDOWS
                // who asked for the re-bucketing (the caller in the game is the frame after this one)
                postmortem::emit("secnan: an object at a non-finite position handed to re-bucketing");
                postmortem::writeBacktrace("secnan:");
#endif
            }
            return;
        }
        if (secsolve::g_active && secsolve::g_boundOn && object
            && (object->m_positionX < secsolve::g_boxX0 || object->m_positionX > secsolve::g_boxX1
                || object->m_positionY < secsolve::g_boxY0
                || object->m_positionY > secsolve::g_boxY1)) {
            ++secsolve::g_boundSkips;
            return;
        }
        GJBaseGameLayer::updateObjectSection(object);
    }

    void updateShaderLayer(float dt) {
        if (secsolve::g_active && secsolve::g_shaderSkip) {
            ++secsolve::g_shaderSkips;
            return;
        }
        GJBaseGameLayer::updateShaderLayer(dt);
    }
};
