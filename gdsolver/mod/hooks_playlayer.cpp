// PlayLayer hook: attempt boundaries, checkpoints, death / completion, cleanup.
#include "mod/playlayer_helpers.hpp"
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>

using namespace p1;

namespace kp { bool engineResetting(); }

static void leaveLevelCleanup(const char* why, const char* censusWhy) {
    seekcp::resetSession();
    if (g_started) {
        if (!g_sessionOver) {
            log::info("session ended by {}", censusWhy);
            endSession(why);
        }
        if (g_solveMemHeld) dpsolve::releaseSolveMemory(censusWhy);
        resetSessionState();
        dpsolve::memCensus(censusWhy);   // cfg memcensus (print only), before g_cfg is reset
        g_cfg = Config{};
    } else {
        dpsolve::memCensus(censusWhy);   // ...and when the player leaves a level they played
    }
    audio::neutral();
    notify::clear();
}

static void persistWorldApply(GJEffectManager* em) {
    static int s_logs = 0;
    std::string was, now;
    for (auto& kv : em->m_persistentItemCountMap)
        was += std::to_string(kv.first) + ":" + std::to_string(kv.second) + " ";
    em->m_persistentItemCountMap.clear();
    em->m_persistentTimerItemSet.clear();
    if (g_cfg.persistWorld > 0) {
        const std::string& s = g_cfg.persistItems;
        size_t p = 0;
        while (p < s.size()) {
            size_t q = s.find(',', p);
            if (q == std::string::npos) q = s.size();
            const std::string one = s.substr(p, q - p);
            const size_t c = one.find(':');
            if (c != std::string::npos) {
                const int id = std::atoi(one.substr(0, c).c_str());
                const int v = std::atoi(one.substr(c + 1).c_str());
                em->m_persistentItemCountMap[id] = v;
                now += std::to_string(id) + ":" + std::to_string(v) + " ";
            }
            p = q + 1;
        }
    }
    if (s_logs < 6) {
        ++s_logs;
        writeResult("persistworld: k=" + std::to_string(g_cfg.persistWorld) + " attempt "
                    + std::to_string(g_attempt) + ": kept items {" + was + "} -> {" + now + "}"
                    + (g_cfg.persistWorld > 0 && g_cfg.persistItems.empty()
                           ? " (no persistitems: the empty table, as k=0)" : ""));
    }
}

class $modify(PlayLayer) {
    CheckpointObject* markCheckpoint() {
        auto* ck = PlayLayer::markCheckpoint();
        if (solver::ckpttrace::g_on) noteCkpt("mark", m_player1, (void*)ck, -1);
        return ck;
    }
    CheckpointObject* createCheckpoint() {
        auto* ck = PlayLayer::createCheckpoint();
        if (solver::ckpttrace::g_on) noteCkpt("create", m_player1, (void*)ck, -1);
        return ck;
    }
    void showCompleteEffect() {
        writeResult("endscreen: showCompleteEffect");
        PlayLayer::showCompleteEffect();
    }
    void showCompleteText() {
        writeResult("endscreen: showCompleteText");
        PlayLayer::showCompleteText();
    }
    void showEndLayer() {
        writeResult("endscreen: showEndLayer (results screen)");
        PlayLayer::showEndLayer();
    }
    void updateVisibility(float dt) {
        if (g_started && !g_sessionOver && g_cfg.fastdt > 0
            && g_cfg.skipRender && !g_realtimeOverride) return;
        if (g_endzoneBurn) return;   // burning through the end-zone effects: stop visibility too
        // A section solve's search: off, as in every worker's search (secRenderHold).
        if (g_secRenderWas >= 0) return;
        if (g_visRefresh && g_visRefreshOn) {
            g_visRefresh = false;
            m_leftSectionIndex = m_rightSectionIndex = 0;
            m_bottomSectionIndex = m_topSectionIndex = 0;
        }
        if (g_cfg.cpVisAfter && (seekcp::g_restoring || cpflight::g_starting || takeover::g_restoring)) return;
        // A seek's batch that is not the frame's last (cfg seekvisevery, itermap.hpp).
        if (itermap::g_seekVisSkip) return;
        if (itermap::seeking()) {
            const auto t0 = std::chrono::steady_clock::now();
            this->safeUpdateVisibility(dt);
            ++itermap::g_seekVisCalls;
            itermap::g_seekVisMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            return;
        }
        this->safeUpdateVisibility(dt);
    }

    void safeUpdateVisibility(float dt) {
#ifdef GEODE_IS_WINDOWS
        __try {
            PlayLayer::updateVisibility(dt);
        } __except (visAvNote(GetExceptionInformation())) {
            logVisibilityCrashSwallowed();
        }
#else
        // No SEH on Android: the pass runs unguarded, and a fault in it ends the game.
        PlayLayer::updateVisibility(dt);
#endif
    }
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        dpsolve::g_phaseLabelsLive = false;   // until start() has reset this level's labels (phase.hpp)
        const bool armed = level && g_uiArmedLevel == level;
        if (g_uiArmedLevel && !armed)
            log::info("play menu: armed for another level, entering {} unarmed",
                      level ? level->m_levelID.value() : -1);
        g_uiArmedLevel = nullptr;   // one entry per Start, whatever is decided below
        bool want = (!g_started || g_sessionOver) && g_uiMode > 0 && armed;
        if (want) uiConfigureSession(level->m_levelID.value());
        if (levelslice::g_swapInit) {
            levelslice::g_swapInit = false;
        } else {
            g_progressLevel = level;
            g_progressAtStart = sampleProgress(level);
        }
        if (g_rngFresh) {
            static bool saved = false;
            static long long seeds[3];
            static const uintptr_t kSeedRva[3] = {gdoff::kSeedTriggerRva, gdoff::kSeedVarIndexRva,
                                                  gdoff::kSeedVarTableRva};
            auto* base = reinterpret_cast<unsigned char*>(geode::base::get());
            for (int k = 0; k < 3; ++k) {
                auto* p = reinterpret_cast<long long*>(base + kSeedRva[k]);
                if (!saved) seeds[k] = *p;
                else *p = seeds[k];
            }
            char b[160];
            snprintf(b, sizeof(b), "rngfresh: %s e90=%lld ee0=%lld ef8=%lld",
                     saved ? "restored" : "saved", seeds[0], seeds[1], seeds[2]);
            writeResult(b);
            saved = true;
        }
        rngFixApply();
        if (botDriving() && (g_rngFix || g_rngSeedSet)) {
            char b[128];
            snprintf(b, sizeof(b), "rngfix: GD's random seeds fixed on every attempt (ee0=%lld ef8=%lld%s)",
                     g_rngSeedSet ? g_rngSeedEE0 : kRngFixEE0, g_rngSeedSet ? g_rngSeedEF8 : kRngFixEF8,
                     g_rngSeedSet ? ", from cfg rngseed" : "");
            writeResult(b);
        }
        if (g_swapTiming) {
            const auto t0 = std::chrono::steady_clock::now();
            const bool ok = PlayLayer::init(level, useReplay, dontCreateObjects);
            const auto now = std::chrono::steady_clock::now();
            char b[192];
            snprintf(b, sizeof(b), "swaplevel: layer built in %lld ms, %lld ms since the command "
                     "(objects=%d)",
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(now - t0).count(),
                     (long long)std::chrono::duration_cast<std::chrono::milliseconds>(now - g_swapT0).count(),
                     level ? (int)level->m_objectCount : -1);
            writeResult(b);
            g_swapTiming = false;
            return ok;
        }
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void onQuit() {
        leaveLevelCleanup("level_exit", "level exit");
        PlayLayer::onQuit();
    }

    void resetLevel() {
        // a restore of the Kairos engine is inside its search (kp::engineResetting, top).
        if (kp::engineResetting()) {
            PlayLayer::resetLevel();
            return;
        }
        if (takeover::takesReset() && this == PlayLayer::get()) {
            takeover::g_restorePending = true;
            return;
        }
        rngFixApply();
        phase::Scope resetPhase("resetting", dpsolve::g_phaseLabelsLive ? dpsolve::g_anchorT : -1,
                                dpsolve::g_phaseLabelsLive ? dpsolve::g_iter : -1);
        // A reset of an old layer (embers awaiting the scene swap) must not touch the books
        if (this != PlayLayer::get()) { PlayLayer::resetLevel(); rngFixAfterReset(this); restoreProgress(); return; }
        if (secsolve::g_active) {
            PlayLayer::resetLevel();
            // cfg singlereset: the loop's next flight resets again straight after the search's last one.
            g_resetMarkLayer = this;
            g_resetMarkPc = g_pcCalls;
            rngFixAfterReset(this); restoreProgress(); return;
        }
        hookdepth::Guard hg(hookdepth::RESET);
        stallwatch::Mark sm(stallwatch::RESET);
        if (g_heapCheckEvery > 0 && g_started && !g_sessionOver) {
            static long long s_lastCheck = 0;
            if (solver::g_totalAttempts - s_lastCheck >= g_heapCheckEvery) {
                s_lastCheck = solver::g_totalAttempts;
                auto t0 = std::chrono::steady_clock::now();
                int heaps = 0; long long blocks = 0;
                bool ok = postmortem::heapOk(&heaps, &blocks);
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                if (!ok) {
                    postmortem::writeRaw("HEAP CORRUPTED (detected by heapcheck)");
                    writeResult("heapcheck: CORRUPT at attempt "
                        + std::to_string(solver::g_totalAttempts)
                        + " tick=" + std::to_string((long long)g_tick));
                    g_heapCheckEvery = 0;   // silent from now on (only the first hit matters)
                } else {
                    static int s_logs = 0;
                    if (++s_logs <= 3 || (solver::g_totalAttempts % (g_heapCheckEvery * 20)) == 0)
                        writeResult("heapcheck: ok at attempt "
                            + std::to_string(solver::g_totalAttempts)
                            + " (" + std::to_string(ms) + "ms, heaps=" + std::to_string(heaps)
                            + " blocks=" + std::to_string(blocks) + ")");
                }
            }
        }
        auto resetT0 = std::chrono::steady_clock::now();
        struct ResetTimer {
            std::chrono::steady_clock::time_point t0;
            ~ResetTimer() {
                solver::g_resetNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - t0).count();
                ++solver::g_resetCalls;
            }
        } resetTimer{resetT0};
        solver::g_deathBooked = false;  // the next run's death is booked afresh
        speedgate::newAttempt();   // portal detection restarts every attempt
        if (g_forceCleanStart && g_started && !g_sessionOver) {
            g_forceCleanStart = false;
            if (m_checkpointArray) {
                while (m_checkpointArray->count() > 0) this->removeCheckpoint(false);
            }
            if (m_isPracticeMode) this->togglePracticeMode(false);
        }
        bool ckptRestore = m_isPracticeMode && m_checkpointArray
            && m_checkpointArray->count() > 0 && g_ckpt;
        solver::g_injThisAttempt = 0;
        solver::g_restoreTickDbg = 0;
        solver::g_prevTickX = -1e9f;
        orbtrace::reset(); // do not carry the previous attempt's contact tick over
                           // (orb pointers are reused)
        if (!ckptRestore) {
            ++g_attempt;
            ++g_attemptSerial;
            solver::g_cpInjected = false;   // the state-injection probe runs once per attempt
            g_injectNext = 0;               // analysis injections are also re-read from the
                                            // start of the attempt
            g_stopFired = false;
            g_nextInput = 0;
            // An attempt sequence (plan_io.hpp): attempt N flies block N, the last one after the prelude.
            g_preludeResetPending = false;   // a restart asked by the attempt that has just ended is moot
            if (!g_prelude.empty() && g_started && !g_sessionOver) {
                const size_t idx = (size_t)(g_attempt > 0 ? g_attempt - 1 : 0);
                g_cfg.inputs = idx < g_prelude.size() ? g_prelude[idx].in : g_finalInputs;
                writeResult("sequence: attempt " + std::to_string(g_attempt) + " flies "
                            + (idx < g_prelude.size() ? "prelude block " + std::to_string(idx + 1) : std::string("the clearing block"))
                            + " (" + std::to_string(g_cfg.inputs.size()) + " inputs)");
            }
            g_attemptFed = false;
            g_nextToggle = 0;
            g_tick = 0;
            g_gameFrame = 0;   // rotation does not carry across attempts
            anchors::onAttemptStart();   // the re-anchor record is per attempt
            padseed::reset();            // ...and so are the pads it seeds (cfg dpspentpad)
            ringseed::reset();           // ...and the rings (dp --spentorb)
            portalseed::reset();
            touchseed::reset();
            touchfire::reset();
            itermap::onAttemptStart();   // ...and so is the seek bar's tick -> x record
            seekcp::onAttemptBegin(this);   // a head run, unless a seek's restore says otherwise after it
            // The HUD's best coin count, before the attempt that got them is cleared.
            solver::g_coinBest = std::max(solver::g_coinBest, solver::coinsThisAttempt());
            solver::g_coinPickupTick.assign(solver::g_coins.size(), -1);
            solver::g_coinGdTick.assign(solver::g_coins.size(), -1);
            solver::g_coinGdUnmatched = 0;
            solver::g_coinMissFired = false;   // the miss request is one per attempt
            solver::g_coinMissIdx = -1;
            solver::g_endTriggerFiredTick = -1;
            for (auto& lg : solver::g_coinPosLog) lg.clear();
            route::onAttemptStart();   // cfg routeprereq: the switches are per attempt too
            solver::g_itemCounts.clear();      // ...and so are GD's item counters
            solver::g_coinLiveSaid.assign(solver::g_coins.size(), 0);
            g_attemptStart = std::chrono::steady_clock::now();
            if (grouptrace::g_on) {
                if (grouptrace::owns(this)) grouptrace::restart();
                else grouptrace::build(this);
            }
            dpsolve::g_recordAttempt = dpsolve::g_deepActive;
            cpflight::g_forceHead = false;
            cpflight::onAttemptStart(g_attempt,
                g_started && g_cfg.dpSolve && !g_dpShowSolution && !secsolve::g_on
                && !dpsolve::g_deepActive && !dpsolve::g_ckFlying
                && levelslice::g_phase != levelslice::Verifying
                && levelslice::g_phase != levelslice::RefFlight
                && !(cpflight::g_starting && cpflight::g_starting->probe)
                && !cpflight::g_controlStarting);
            if (cpflight::g_controlStarting) cpflight::startControl();
            if ((g_cfg.coinMode || clearance::g_on || g_cfg.dpSelfTest || g_cfg.dpSolve)
                && !solver::g_poisBuilt) {
                solver::buildPois(this, g_cfg.obbNoLatch);
                solver::g_poisBuilt = true;
                if (g_cfg.routePrereq && g_cfg.coinRoute)
                    route::build(this, solver::g_coinObjs, [](const route::Prereq& p) {
                        char b[224];
                        snprintf(b, sizeof(b), "route: coin %d needs uid %d (id %d) entered at "
                                 "(%.0f,%.0f) %.0fx%.0f -- it switches on %s uid %d", p.coin,
                                 p.boxUid, p.boxId, (double)p.bx, (double)p.by,
                                 (double)(2 * p.bhw), (double)(2 * p.bhh),
                                 p.gate ? "the gate object" : "the coin itself", p.watchUid);
                        writeResult(b);
                    });
                if (g_cfg.dpSelfTest) dpSelfTest(this);
                // Stage B: solve this level in-process, then replay what comes back
                if (g_cfg.dpSolve) dpsolve::start(this);
                writeResult("pois: " + std::to_string(solver::g_pois.size()) + " orbs, "
                    + std::to_string(solver::g_coins.size()) + " coins");
            }
        }
        if (g_started && !g_sessionOver) {
            if (g_cfg.cbs >= 0) m_clickBetweenSteps = (g_cfg.cbs == 1);
            if (g_cfg.cos >= 0) m_clickOnSteps = (g_cfg.cos == 1);
        }
        ev("resetLevel", m_clickBetweenSteps, ckptRestore);
        // Leftover completion flags on retry are invisible from outside, so record before/after
        if (g_sessionOver || g_retryDone) {
            char rb[192];
            snprintf(rb, sizeof(rb),
                "retry-reset: before completed=%d locked=%d dead=%d tick=%lld",
                m_hasCompletedLevel ? 1 : 0,
                m_player1 ? (m_player1->m_isLocked ? 1 : 0) : -1,
                m_player1 ? (m_player1->m_isDead ? 1 : 0) : -1, (long long)g_tick);
            writeResult(rb);
        }
        // cfg resetlog: what the reset starts from (read-only).
        auto resetLogLine = [this](const char* when, const char* kind) {
            if (!(g_cfg.resetLog && g_started && !g_sessionOver)) return;
            static int n = 0;
            if (++n > 2000) return;
            std::string items;
            int tuples = (int)m_spawnTuples.size(), acts = -1;
            if (m_effectManager) {
                acts = (int)m_effectManager->m_spawnTriggerActions.size();
                int shown = 0;
                for (auto& kv : m_effectManager->m_persistentItemCountMap) {
                    if (++shown > 12) { items += "..."; break; }
                    items += std::to_string(kv.first) + ":" + std::to_string(kv.second) + "/live" + std::to_string(m_effectManager->countForItem(kv.first)) + " ";
                }
            }
            int disabled = 0;
            if (m_objects)
                for (unsigned i = 0; i < m_objects->count(); ++i) {
                    auto* o = static_cast<GameObject*>(m_objects->objectAtIndex(i));
                    if (o && o->m_isGroupDisabled) ++disabled;
                }
            writeResult(std::string("resetlog: ") + when + " " + kind + " attempt=" + std::to_string(g_attempt) + " tick=" + std::to_string((long long)g_tick)
                        + " persistent{" + items + "} tuples=" + std::to_string(tuples) + " spawnActs=" + std::to_string(acts)
                        + " groupDisabledObjs=" + std::to_string(disabled) + " pc=" + std::to_string(g_pcCalls));
        };
        const char* resetKind = ckptRestore ? "ckpt" : (cpflight::g_starting ? "cpstart" : "head");
        resetLogLine("before", resetKind);
        struct ResetLogAfter { std::function<void(const char*, const char*)> f; const char* k; ~ResetLogAfter() { f("after", k); } };
        std::unique_ptr<ResetLogAfter> resetLogAfter;
        if (g_cfg.resetLog) resetLogAfter.reset(new ResetLogAfter{resetLogLine, resetKind});
        if (g_cfg.singleReset && g_started && !g_sessionOver && !ckptRestore && !cpflight::g_starting
            && !takeover::g_restoring && g_resetMarkLayer == this && g_resetMarkPc == g_pcCalls) {
            ++g_resetsSkipped;
            if (g_resetsSkipped <= 5)
                writeResult("singlereset: reset skipped at attempt " + std::to_string(g_attempt) + " (no physics since the last one)");
        } else {
            if (g_cfg.singleReset && g_started && !g_sessionOver && !ckptRestore && !cpflight::g_starting && !takeover::g_restoring && m_effectManager) {
                m_spawnTuples.clear();
                m_effectManager->m_spawnTriggerActions.clear();
            }
            if (g_cfg.persistWorld >= 0 && g_started && !g_sessionOver && !ckptRestore && !cpflight::g_starting
                && !takeover::g_restoring && m_effectManager)
                persistWorldApply(m_effectManager);
            PlayLayer::resetLevel();
            g_resetMarkLayer = this;
            g_resetMarkPc = g_pcCalls;
            if (g_cfg.headBandFresh && !ckptRestore && !cpflight::g_starting && !takeover::g_restoring) {
                char* lb = reinterpret_cast<char*>(static_cast<GJBaseGameLayer*>(this));
                char* gs = reinterpret_cast<char*>(&this->m_gameState);
                static int s_hb = 0;
                if (s_hb++ < 3) {
                    float was = 0.f;
                    std::memcpy(&was, gs + 0x5bc, 4);
                    char b[180];
                    snprintf(b, sizeof(b), "headbandfresh: layer+0x%llx (m_gameState+0x5bc) was %.3f, now 0",
                             (long long)(gs + 0x5bc - lb), (double)was);
                    writeResult(b);
                }
                std::memset(gs + 0x5bc, 0, 4);
            }
            if (g_cfg.resetDump && m_player1 && !ckptRestore && !cpflight::g_starting && !takeover::g_restoring) {
                auto dumpBytes = [&](const char* tag, const void* p, size_t size) {
                    const unsigned char* pb = reinterpret_cast<const unsigned char*>(p);
                    for (size_t base = 0; base < size; base += 0x80) {
                        char b[620];
                        int n = snprintf(b, sizeof(b), "%s attempt=%d o=%04zx", tag, g_attempt, base);
                        for (size_t off = base; off < base + 0x80 && off < size && n < (int)sizeof(b) - 8; ++off)
                            n += snprintf(b + n, sizeof(b) - n, " %02x", (unsigned)pb[off]);
                        writeResult(b);
                    }
                };
                static bool s_names = false;
                if (!s_names) {
                    s_names = true;
                    char b[200];
#define PO_SCALAR(n) snprintf(b, sizeof(b), "rdmem: P %s %zu %zu", #n, offsetof(PlayerObject, n), sizeof(PlayerObject::n)); writeResult(b);
#define PO_OPAQUE(n) PO_SCALAR(n)
#define GO_SCALAR(n) snprintf(b, sizeof(b), "rdmem: O %s %zu %zu", #n, offsetof(GameObject, n), sizeof(GameObject::n)); writeResult(b);
#define GO_OPAQUE(n) GO_SCALAR(n)
#define GB_SCALAR(n) snprintf(b, sizeof(b), "rdmem: L %s %zu %zu", #n, offsetof(GJBaseGameLayer, n), sizeof(GJBaseGameLayer::n)); writeResult(b);
#define GB_OPAQUE(n) GB_SCALAR(n)
#define EM_SCALAR(n)
#define EM_OPAQUE(n)
#include "../po_members.inc"
#undef PO_SCALAR
#undef PO_OPAQUE
#undef GO_SCALAR
#undef GO_OPAQUE
#undef GB_SCALAR
#undef GB_OPAQUE
#undef EM_SCALAR
#undef EM_OPAQUE
                }
                dumpBytes("rdump:", m_player1, sizeof(PlayerObject));
                dumpBytes("rdumpL:", static_cast<GJBaseGameLayer*>(this), sizeof(GJBaseGameLayer));
                if (!g_cfg.resetDumpUids.empty() && m_objects)
                    for (auto* o : CCArrayExt<GameObject*>(m_objects))
                        if (o && std::find(g_cfg.resetDumpUids.begin(), g_cfg.resetDumpUids.end(), o->m_uniqueID)
                                     != g_cfg.resetDumpUids.end()) {
                            const std::string tag = "rdumpO: uid=" + std::to_string(o->m_uniqueID);
                            dumpBytes(tag.c_str(), o, sizeof(GameObject));
                        }
            }
        }
        rngFixAfterReset(this);   // the trigger seed GD just drew from the clock (see g_rngFix)
        if (secsolve::g_shaderSig) {
            static const void* s_sigLayer = nullptr;
            if (s_sigLayer != this) {
                s_sigLayer = this;
                writeResult("shadersig: first reset of level " + std::to_string(g_cfg.levelId)
                            + " " + secsolve::shaderSig(this));
            } else if (secsolve::g_shaderSigPending) {
                writeResult("shadersig: first reset after the search "
                            + secsolve::shaderSig(this));
            }
            secsolve::g_shaderSigPending = false;
        }
        const bool cpStart = !ckptRestore && cpflight::g_starting;
        if (cpStart) cpflight::splice(*cpflight::g_starting);
        else if (!ckptRestore) cpflight::afterHeadReset(this);   // the carry's node prediction, checked
        if (!ckptRestore && !cpStart && grouptrace::g_on) grouptrace::snapshotInit();
        restoreProgress();
        hitbox::onLevelReset();   // the nodes it hid are gone; re-apply against the new scene
        const bool wasEndAnim = m_levelEndAnimationStarted;
        if (wasEndAnim) m_levelEndAnimationStarted = false;
        if (g_sessionOver || g_retryDone || wasEndAnim) {
            char rb[224];
            snprintf(rb, sizeof(rb),
                "retry-reset: after  completed=%d locked=%d dead=%d x=%.1f "
                "endAnimWas=%d (cleared)",
                m_hasCompletedLevel ? 1 : 0,
                m_player1 ? (m_player1->m_isLocked ? 1 : 0) : -1,
                m_player1 ? (m_player1->m_isDead ? 1 : 0) : -1,
                m_player1 ? m_player1->getPositionX() : -1.f,
                wasEndAnim ? 1 : 0);
            writeResult(rb);
        }
        if (!ckptRestore && !cpStart && !takeover::g_restoring && g_started && g_cfg.blockInput) {
            g_injecting = true;
            this->handleButton(false, 1, true);
            g_injecting = false;
        }
        // An attempt sequence's prelude attempts do not count against the cap: it is the clearing attempts' budget.
        if (g_started && g_finishedAttempts >= g_cfg.maxAttempts + (int)g_prelude.size()) {
            endSession("max_attempts");
        }
    }

    void pauseGame(bool unfocused) {
        if (!this->getParent()) {
            writeResult("pause: refused - the layer has no scene to put the pause menu in");
            log::warn("pause: refused - the layer has no scene to put the pause menu in");
            return;
        }
        // (Not while a person plays a takeover: then the game pauses as it would for anyone.)
        if (unfocused && g_started && !g_sessionOver && !takeover::personDriving()) {
            ++g_unfocusPauseBlocked;
            if (g_unfocusPauseBlocked <= 3)
                writeResult("pause: ignored an unfocus-pause (automated session, n="
                    + std::to_string(g_unfocusPauseBlocked) + ")");
            return;
        }
        PlayLayer::pauseGame(unfocused);
    }

    void storeCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::storeCheckpoint(checkpoint);
        // Observation of auto-checkpoint placement only (cfg `ckpttrace=1`)
        if (solver::ckpttrace::g_on)
            noteCkpt("store", m_player1, (void*)checkpoint, -1);
        if (takeover::personDriving() && !takeover::g_restoring && this == PlayLayer::get()) {
            if (takeover::g_hasPending && checkpoint == takeover::g_pendingSrc) {
                takeover::g_commitPending = true;
            } else {
                takeover::g_cpWanted = true;
                takeover::g_wantedSrc = checkpoint;
            }
        }
    }

    void removeCheckpoint(bool first) {
        const void* gone = nullptr;
        if (takeover::personDriving() && this == PlayLayer::get() && m_checkpointArray
            && m_checkpointArray->count() > 0)
            gone = first ? m_checkpointArray->objectAtIndex(0) : m_checkpointArray->lastObject();
        PlayLayer::removeCheckpoint(first);
        if (gone) takeover::gameRemoved(gone);
    }

    void rollGroupTrace() {
        if (!grouptrace::g_on) return;
        if (cpflight::g_probeFlying || cpflight::g_controlFlying) return;
        // g_tick, not the recording's last row: see roll's `endTick`
        auto r = grouptrace::roll(g_tick);
        writeResult("gt_last: attempt=" + std::to_string(g_attempt)
            + " rows=" + std::to_string(r.rows)
            + " depth=" + std::to_string(r.depth)
            + " end=" + std::to_string(g_tick));
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (this != PlayLayer::get()) {
            NoRecordGuard nr(this);
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        // cfg killersite=1: built before the original call, written after it (see below).
        char siteBuf[256];
        bool siteArmed = false;
        const bool anticheat = object && object == m_anticheatSpike;
        if (anticheat) ++solver::g_anticheatCalls;
        if ((g_cfg.hitboxTrace || g_cfg.dpSolve) && g_started && !g_sessionOver
            && player && !anticheat) {
            char kb[240];
            snprintf(kb, sizeof(kb),
                "killer: t=%lld who=%s py=%.3f pvy=%.3f px=%.3f obj=%s uid=%d "
                "id=%d type=%d ox=%.3f oy=%.3f",
                (long long)g_tick,
                player == m_player1 ? "p1" : player == m_player2 ? "p2" : "?",
                player->getPositionY(), (double)player->m_yVelocity,
                player->getPositionX(), object ? "yes" : "NULL",
                object ? (int)object->m_uniqueID : -1,
                object ? (int)object->m_objectID : -1,
                object ? (int)object->m_objectType : -1,
                object ? object->getPositionX() : 0.f,
                object ? object->getPositionY() : 0.f);
            writeResult(kb);
            solver::g_killTick = (long long)g_tick;   // cfg voidband (repair.hpp checkVoidRun)
            solver::g_killUid = object ? (int)object->m_uniqueID : -1;
            solver::g_killX = object ? object->getPositionX() : 0.f;
            solver::g_killY = object ? object->getPositionY() : 0.f;
            if (g_cfg.killerSite && !player->m_isDead && !secsolve::g_noKill
                && !secsolve::g_active && !g_cfg.noDeath) {
#ifdef GEODE_IS_WINDOWS
                static uintptr_t base = 0, size = 0;
                if (!base) {
                    base = (uintptr_t)GetModuleHandleW(nullptr);
                    auto* dos = (IMAGE_DOS_HEADER*)base;
                    auto* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
                    size = nt->OptionalHeader.SizeOfImage;
                }
                char* sb = siteBuf;
                constexpr int kSb = (int)sizeof siteBuf;
                int o = snprintf(sb, kSb, "killsite: t=%lld obj=%s rva=",
                                 (long long)g_tick, object ? "yes" : "NULL");
                int kept = 0;
                ULONG_PTR sp = (ULONG_PTR)&kept;
                MEMORY_BASIC_INFORMATION mbi{};
                if (VirtualQuery((LPCVOID)sp, &mbi, sizeof mbi)) {
                    ULONG_PTR end = (ULONG_PTR)mbi.BaseAddress + mbi.RegionSize;
                    if (end > sp + 0x4000) end = sp + 0x4000;
                    for (ULONG_PTR p = sp;
                         p + 8 <= end && kept < 6 && o < kSb - 20;
                         p += 8) {
                        const ULONG_PTR v = *(ULONG_PTR*)p;
                        if (v < base || v - base >= size) continue;
                        o += snprintf(sb + o, kSb - o, "%s%llx",
                                      kept++ ? "," : "",
                                      (unsigned long long)(v - base));
                    }
                }
                if (!kept) snprintf(sb + o, kSb - o, "(none in module)");
#else
                // The stack scan reads the PE image and Windows' memory map; not on Android.
                snprintf(siteBuf, sizeof siteBuf, "killsite: t=%lld obj=%s rva=(unavailable)",
                         (long long)g_tick, object ? "yes" : "NULL");
#endif
                siteArmed = true;
            }
            if (player == m_player1)
                itermap::latchKiller((long long)g_tick, player->getPositionY(),
                                     object ? (int)object->m_objectID : -1,
                                     object ? (int)object->m_uniqueID : -1);
        }
        if (secsolve::g_active && secsolve::g_killLog && player == m_player1) {
            char kb[220];
            snprintf(kb, sizeof(kb),
                     "seckill: d=%d py=%.3f pvy=%.3f px=%.1f obj=%s uid=%d "
                     "id=%d ox=%.1f oy=%.1f",
                     secsolve::g_depth, player->getPositionY(),
                     (double)player->m_yVelocity, player->getPositionX(),
                     object ? "yes" : "NULL", object ? object->m_uniqueID : -1,
                     object ? object->m_objectID : -1,
                     object ? object->getPositionX() : 0.f,
                     object ? object->getPositionY() : 0.f);
            writeResult(kb);
        }
        if (anticheat && g_cfg.antiCheatPass && (secsolve::g_noKill || g_cfg.noDeath)) {
            if (!(m_player1 && m_player1->m_isLocked) && !m_playerDied) m_damageVerified = true;
            return;
        }
        if (secsolve::g_noKill) {
            if (player == m_player1 || player == m_player2) secsolve::g_died = true;
            return;
        }
        if (secsolve::g_active) {
            NoRecordGuard nr(this);
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        hookdepth::Guard hg(hookdepth::DESTROY);
        stallwatch::Mark sm(stallwatch::DESTROY);
        if (g_cfg.noDeath) return;
        bool wasDead = player->m_isDead;
        char kb[280];
        const bool gateLine = !wasDead && solver::g_gateTrace;
        if (gateLine) {
            const char* who = player == m_player1 ? "p1"
                            : player == m_player2 ? "p2" : "?";
            if (object) {
                auto orr = object->getObjectRect();
                snprintf(kb, sizeof(kb),
                    "killer: tick=%lld who=%s uid=%d id=%d type=%d ox=%.1f oy=%.1f "
                    "rect=%.2f,%.2f,%.2f,%.2f on=%d px=%.1f py=%.1f",
                    (long long)g_tick, who, object->m_uniqueID, object->m_objectID,
                    (int)object->m_objectType,
                    object->getPositionX(), object->getPositionY(),
                    orr.origin.x, orr.origin.y, orr.size.width, orr.size.height,
                    (object->m_isGroupDisabled || object->m_isGroupDisabledTemp)
                        ? 0 : 1,
                    player->getPositionX(), player->getPositionY());
            }
            else
                snprintf(kb, sizeof(kb),
                    "killer: tick=%lld who=%s (no object) px=%.1f py=%.1f",
                    (long long)g_tick, who,
                    player->getPositionX(), player->getPositionY());
        }
        {
            NoRecordGuard nr(this);
            PlayLayer::destroyPlayer(player, object);
        }
        // ...and now the call has said whether it killed anybody.
        if (gateLine && player->m_isDead) writeResult(kb);
        if (siteArmed && !wasDead && player->m_isDead) writeResult(siteBuf);
        if (!wasDead && player->m_isDead) {
            if (player == m_player1) ++solver::g_deathsP1;
            else if (player == m_player2) ++solver::g_deathsP2;
            else ++solver::g_deathsOther;
        }
        restoreProgress();
        bool isP1 = (player == m_player1);
        bool isP2Dual = (player == m_player2 && m_gameState.m_isDualMode);
        if (!wasDead && player->m_isDead && (isP1 || isP2Dual)
            && !solver::g_deathBooked) {
            solver::g_deathBooked = true;
            auto pos = (isP1 || !m_player1) ? player->getPosition()
                                            : m_player1->getPosition();
            if (isP2Dual) {
                static int s_p2logs = 0;
                if (++s_p2logs <= 5)
                    writeResult("dual: booked a p2 death as the run end (tick="
                        + std::to_string((long long)g_tick)
                        + " x=" + std::to_string((int)pos.x) + ")");
            }
            ev("destroyPlayer", pos.x, pos.y);
            clearance::observe(solver::g_log, g_tick);
            if (g_started) {
                ++g_finishedAttempts;
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - g_attemptStart).count();
                double speed = ms > 0 ? (g_tick / 240.0) / (ms / 1000.0) : 0;
                rollGroupTrace();
                flushAll();
                cpflight::noteEnd(g_tick);   // the flight's work, by kind (cpflight summary)
                cpflight::objLine(this, "death", g_tick);   // cfg cpflightobj (print only)
                writeResult("death: attempt=" + std::to_string(g_attempt)
                    + " tick=" + std::to_string(g_tick)
                    + " x=" + std::to_string(pos.x)
                    + " wallMs=" + std::to_string(ms)
                    + " speedX=" + std::to_string(speed));
                if (takeover::personDriving()) takeover::died(g_tick, pos.x);
                else if (takeover::approaching()) takeover::diedBeforePoint(g_tick, pos.x);
                if (g_serveMode) g_serveWait = true;  // stop at the start of the next attempt
                if (g_secRung && secsolve::g_on && !g_ckpt && g_cfg.checkpointAt >= 0
                    && g_tick < g_cfg.checkpointAt)
                    dpsolve::secRungPrefixFailed(g_tick, pos.x, "died");
                if (g_cfg.dpSolve) dpsolve::onDeath(g_tick, pos.x);
            }
        }
    }

    void levelComplete() {
        if (this != PlayLayer::get()) {
            NoRecordGuard nr(this);
            PlayLayer::levelComplete();
            return;
        }
        if (g_started && !g_sessionOver && !m_isPlatformer
            && (secsolve::inFlight() || secsolve::g_active || !m_levelEndAnimationStarted)) {
            const bool during = secsolve::inFlight() || secsolve::g_active;
            writeResult(std::string("complete: refused - ")
                        + (during ? "raised during a section search"
                                  : "the attempt never started the end animation")
                        + " (x=" + std::to_string((int)(m_player1 ? m_player1->getPositionX() : -1.f))
                        + " tick=" + std::to_string(g_tick) + ")");
            return;
        }
        hookdepth::Guard hg(hookdepth::COMPLETE);
        stallwatch::Mark sm(stallwatch::COMPLETE);
        ev("levelComplete");
        if (g_cfg.dpSolve && g_cfg.dpCheck && g_started && g_tick == 0) {
            writeResult("dpsolve:   [check] ignoring a completion raised before the attempt's "
                        "first tick - it belongs to the attempt before");
            return;
        }
        if (g_started && m_player1) {
            float lm = solver::g_levelMaxX;
            if (lm <= 0 && m_objects) {
                for (auto* obj : CCArrayExt<GameObject*>(m_objects))
                    if (obj) lm = std::max(lm, obj->getPositionX());
            }
            static int s_cc = 0;
            if (++s_cc <= 40)
                writeResult("clearcheck: x=" + std::to_string((int)m_player1->getPositionX())
                    + " levelMaxX=" + std::to_string((int)lm)
                    + " goalX=" + std::to_string((int)solver::g_goalX)
                    + " margin=" + std::to_string((int)(lm - m_player1->getPositionX()))
                    + " tick=" + std::to_string(g_tick)
                    + " solve=0");
        }
        if (g_started && g_cfg.coinMode && !solver::g_coins.empty()) {
            size_t got = 0;
            for (auto pu : solver::g_coinPickupTick)
                if (pu >= 0 && pu <= g_tick) ++got;
            std::string s = "coin: level complete with " + std::to_string(got) + "/"
                + std::to_string(solver::g_coins.size())
                + " coins (COARSE r=" + std::to_string((int)solver::COIN_RADIUS)
                + " at the load position -- GD's own count is the coingd: line)"
                + " (pickup ticks:";
            for (auto pu : solver::g_coinPickupTick) s += " " + std::to_string(pu);
            writeResult(s + ")");
            size_t gdGot = 0;
            for (auto pu : solver::g_coinGdTick) if (pu >= 0) ++gdGot;
            std::string g = "coingd: level complete with " + std::to_string(gdGot) + "/"
                + std::to_string(solver::g_coins.size()) + " coins (pickup ticks:";
            for (auto pu : solver::g_coinGdTick) g += " " + std::to_string(pu);
            writeResult(g + ") unmatched=" + std::to_string(solver::g_coinGdUnmatched));
            std::string d;
            long long maxLag = 0;
            for (size_t i = 0; i < solver::g_coins.size(); ++i) {
                const long long ours = i < solver::g_coinPickupTick.size()
                                           ? solver::g_coinPickupTick[i] : -1;
                const long long gd = i < solver::g_coinGdTick.size()
                                         ? solver::g_coinGdTick[i] : -1;
                if ((ours >= 0) != (gd >= 0)) {
                    d += " #" + std::to_string(i)
                       + "(uid" + std::to_string(solver::g_coins[i].uid)
                       + " ours=" + std::to_string(ours)
                       + " gd=" + std::to_string(gd) + ")";
                } else if (ours >= 0) {
                    maxLag = std::max(maxLag, ours > gd ? ours - gd : gd - ours);
                }
            }
            writeResult("coincmp:" + (d.empty() ? std::string(" agree") : d)
                        + " maxlag=" + std::to_string(maxLag));
        }
        const bool takeOver = g_started && dpsolve::takesOverClear();
        if (!takeOver) {
            NoRecordGuard nr(this);
            PlayLayer::levelComplete();
        }
        restoreProgress();
        if (g_started) {
            ++g_finishedAttempts;
            clearance::observe(solver::g_log, (long long)solver::g_log.size(), false);
            if (g_endzoneBurn) {
                double sec = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - g_endzoneStart).count();
                writeResult("endzone: burned in " + std::to_string(sec) + "s");
                g_endzoneBurn = false;
            }
            float cx = m_player1 ? m_player1->getPositionX() : -1.f;
            float maxX = solver::g_levelMaxX;
            if (maxX <= 0 && m_objects) {
                for (auto* obj : CCArrayExt<GameObject*>(m_objects))
                    if (obj) maxX = std::max(maxX, obj->getPositionX());
            }
            float goalX = solver::g_goalX;
            if (goalX <= 0.f && m_endPortal) goalX = m_endPortal->getPositionX();
            rollGroupTrace();
            cpflight::noteEnd(g_tick);
            writeResult("complete: attempt=" + std::to_string(g_attempt)
                + " step=" + std::to_string(m_currentStep)
                + " tick=" + std::to_string(g_tick)
                + " x=" + std::to_string((int)cx)
                + " levelMaxX=" + std::to_string((int)maxX)
                + " goalX=" + std::to_string((int)goalX)
                + " pct=" + std::to_string(this->getCurrentPercent()));
            if (takeover::personDriving()) takeover::cleared(g_tick, cx);
            if (g_cfg.dpSolve && dpsolve::g_recordAttempt) {
                if (dpsolve::g_deepActive) dpsolve::finishDeepRecord("reached the end");
                else writeResult("dpsolve: the no-death pass reached the end after its recorder "
                                 "had already retired - not a clear");
                return;
            }
            if (g_cfg.dpSolve && cpflight::g_probeFlying) {
                writeResult(cpflight::probeCapture(g_tick, cx, "cleared"));
                g_paused = true;
                return;
            }
            if (g_cfg.dpSolve && cpflight::g_controlFlying) {
                writeResult(cpflight::controlEnd(g_tick, cx, "cleared"));
                g_paused = true;
                return;
            }
            const float goal = (goalX > 0.f) ? goalX : maxX;
            const bool endTrigLevel = g_cfg.dpEndTrigClear && solver::g_endTriggerFiredTick >= 0;
            if (g_cfg.dpSolve && !g_dpShowSolution && cx >= 0.f && goal > 1.f
                && levelslice::g_phase != levelslice::Sliced
                && levelslice::g_phase != levelslice::RefFlight
                && (goal - cx) > g_clearMargin
                && this->getCurrentPercent() < 95.0f && !endTrigLevel) {
                char fb[256];
                snprintf(fb, sizeof(fb),
                    "dpsolve: refusing a clear %.0fpx short of the goal (x=%.0f goal=%.0f "
                    "pct=%.2f, tolerance %.0f) - not a solution",
                    (double)(goal - cx), (double)cx, (double)goal,
                    this->getCurrentPercent(), (double)g_clearMargin);
                writeResult(fb);
                dpsolve::onDeath(g_tick, cx);
                return;
            }
            if (g_cfg.dpSolve && !g_dpShowSolution && g_cfg.coinRoute
                && !solver::g_coins.empty()) {
                size_t got = 0;
                for (auto pu : solver::g_coinGdTick) if (pu >= 0) ++got;
                if (got < solver::g_coins.size()) {
                    writeResult("dpsolve: refusing a clear with " + std::to_string(got) + "/"
                                + std::to_string(solver::g_coins.size())
                                + " coins - not an all-coins solution");
                    // cfg coinmisspost: filed at the missed coin instead of at the finish.
                    long long dT = g_tick;
                    float dX = cx;
                    dpsolve::fileCoinMissPost(dT, dX);
                    dpsolve::onDeath(dT, dX);
                    return;
                }
            }
            bool byFlight = false;
            if (g_cfg.dpSolve && g_cfg.dpCheck && !g_dpShowSolution && dpsolve::g_running.load())
                byFlight = dpsolve::ckClearedDuringJob();
            if (g_cfg.dpSolve && !g_dpShowSolution
                && levelslice::onSliceCleared(g_cfg.inputs, g_tick))
                return;
            if (g_cfg.dpSolve && !g_dpShowSolution && cpflight::g_from >= 0) {
                writeResult("cpflight: attempt " + std::to_string(g_attempt)
                            + " cleared from the checkpoint at t=" + std::to_string(cpflight::g_from)
                            + " - flying the plan from the head before it is filed");
                cpflight::g_forceHead = true;
                cpflight::g_headPending = true;
                return;
            }
            if (g_cfg.dpSolve && !g_dpShowSolution) levelslice::onLevelCleared(g_tick);
            if (g_cfg.dpSolve && !g_dpShowSolution && !g_cfg.inputs.empty()) {
                char name[128];
                snprintf(name, sizeof(name),
                         g_cfg.coinFiles ? "%s/solution_lv%d_coins.txt"
                                         : "%s/solution_lv%d_dp.txt",
                         DATA_DIR, g_cfg.levelId);
                if (writeInputsFile(name, g_cfg.inputs)) {
                    // ...and which version solved it, for the play menu (solvedWithPath).
                    writeSolvedWith(name, Mod::get()->getVersion().toVString());
                    writeResult(std::string("dpsolve: solution saved -> ") + name);
                    notify::show("gdsolver: solution saved", NotificationIcon::Success, 3.f);
                }
                dpsolve::mapClear(g_tick, byFlight);
                if (itermap::save(g_cfg.levelId, true))
                    writeResult("dpsolve: itermap saved -> "
                                + itermap::pathFor(g_cfg.levelId));
            }
            if (g_cfg.dpSolve && dpsolve::onCleared()) return;
            endSession("level_complete");
        }
    }
};

class $modify(PauseLayer) {
    void goEdit() {
        leaveLevelCleanup("editor_exit", "editor exit");
        PauseLayer::goEdit();
    }
};

class $modify(EndLevelLayer) {
    void goEdit() {
        leaveLevelCleanup("editor_exit", "editor exit");
        EndLevelLayer::goEdit();
    }
};
