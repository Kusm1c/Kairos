#pragma once

namespace solver {
struct TickInfo {
    float x;
    float y;
    uint8_t grounded;
    uint8_t mode; // 0=cube 1=ship 2=ball 3=ufo 4=wave 5=robot 6=spider 7=swing
    uint8_t grounded2; // m_isOnGround2 (the checkpoint does not restore it, so it is
                       // filled in from here on restore)
    float yvel;        // for boundary selection: m_isOnGround can be set even while falling
    uint8_t dual = 0;      // m_gameState.m_isDualMode
    uint8_t p2Grounded = 0;
};

struct Poi { float x; float y; int id = 0; bool dash = false; int uid = -1; float hw = 0.f; };
inline std::vector<Poi> g_pois;
inline bool g_poisBuilt = false;
// Latch to report dual entry only once (diagnostics)
inline bool g_dualSeen = false;
inline bool g_deathBooked = false;
inline std::vector<Poi> g_coins;
inline std::vector<long long> g_coinPickupTick; // -1=not picked up
constexpr float COIN_RADIUS = 20.f;
inline std::vector<long long> g_coinGdTick;
inline size_t coinsThisAttempt() {
    size_t got = 0;
    for (size_t i = 0; i < g_coins.size(); ++i)
        if ((i < g_coinGdTick.size() && g_coinGdTick[i] >= 0)
            || (i < g_coinPickupTick.size() && g_coinPickupTick[i] >= 0))
            ++got;
    return got;
}
inline size_t g_coinBest = 0;
inline int g_coinGdUnmatched = 0;
inline bool g_coinMissFired = false;
// ...and which coin that was (index into g_coins; -1 = a coin gate's cut, or none).
inline int g_coinMissIdx = -1;
inline std::vector<uint8_t> g_coinLiveSaid;
inline bool g_hasRotGameplay = false;
inline std::map<int, int> g_itemCounts;
struct CoinGate {
    int uid = 0, chan = 0, dir = 4, item = 0, need = 0;
    double x = 0.0, y = 0.0;
    bool miss = false;
    double mx = 0.0, my = 0.0;
};
inline std::vector<CoinGate> g_coinGates;
inline std::vector<int> g_coinNoMiss;
inline std::vector<float> g_coinMoveX;
inline int g_endTriggers = 0;
inline long long g_endTriggerFiredTick = -1;
struct CoinPos {
    long long t;
    float x, y;
    uint8_t on;
};
inline std::vector<std::vector<CoinPos>> g_coinPosLog;
inline std::vector<GameObject*> g_coinObjs;
inline int g_coinLogLines = 0;
// Hazard/solid geometry (sorted by x). Used by the clearance table (clearance.hpp)
struct Obj { float x; float y; int id; };
inline std::vector<Obj> g_hazards;
inline std::vector<Obj> g_solids;

inline bool g_gateTrace = false;
inline float g_gateX0 = 0.f, g_gateX1 = 0.f;
inline long long g_gateT0 = 0, g_gateT1 = 0;
inline long long g_gateStride = 1;   // record every how many ticks
inline std::vector<GameObject*> g_gateObjs;
inline bool g_gateHdrDone = false;

inline bool g_colProbe = false;
inline long long g_cpT = 0;
inline double g_cpY0 = 0, g_cpYStep = 0, g_cpVy = 0;
inline int g_cpN = 30;
inline long long g_cpEndTick = -1;
inline bool g_cpInjected = false;   // injected in this attempt already? (false at attempt start)

inline std::vector<TickInfo> g_log;       // per-tick recording of the current attempt

inline float g_levelMaxX = 0;
inline float g_goalX = 0;

inline float g_prevTickX = -1e9f;

inline bool plausibleDeathX(float x) {
    if (!std::isfinite(x)) return false;
    if (g_levelMaxX > 0 && x > g_levelMaxX + 60.f) return false;
    return true;
}
inline long long g_totalAttempts = 0;
// Wall time spent in resetLevel (fixed cost per attempt. Used for resets= in stall.txt)
inline long long g_resetNanos = 0;
inline long long g_resetCalls = 0;
inline std::chrono::steady_clock::time_point g_solveStart;

inline long long g_restoreTickDbg = -1;

inline uint8_t modeOf(PlayerObject* p) {
    if (p->m_isShip) return 1;
    if (p->m_isBall) return 2;
    if (p->m_isBird) return 3;
    if (p->m_isDart) return 4;
    if (p->m_isRobot) return 5;
    if (p->m_isSpider) return 6;
    if (p->m_isSwing) return 7;
    return 0;
}

// Is the plan input holding at tick T
inline bool holdingAt(const std::vector<InputCmd>& plan, long long t) {
    bool h = false;
    for (auto& c : plan) {
        if (c.step > t) break;
        h = c.down;
    }
    return h;
}

inline float forceOf(GameObject* obj) {
    auto* fb = geode::cast::typeinfo_cast<ForceBlockGameObject*>(obj);
    return fb ? fb->m_force : 0.f;
}

inline void writeObjRects(std::ostream& rf, GJBaseGameLayer* l) {
    rf << "id,type,cx,cy,w,h,groups,uid,radius,rot,sy0,sy1,shz,sdir,sup,w0,h0,"
          "tpy,tpg,tpix,tpiy,tw,zoom,zdur,zease,zrate,mvdir,gnddir,"
          "optp1,optp2,flipx,flipy,nofx,notouch,tpex,tpey,dis,"
          "editvel,vmodx,vmody,ovrvel,force,free,touch,spawn,chan,axis,exstat,rev,"
          "nocol,multiact,nomultiact,gmod,gpl,dontslide,"
          "tpfm,tpf,tpang,tprmod,tprmin,tprmax,"
          "optudg,"
          "extcol\n";
    PlayLayer* const pl = typeinfo_cast<PlayLayer*>(l);
    GameObject* const anticheat = pl ? pl->m_anticheatSpike : nullptr;
    for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!obj || obj == anticheat) continue;
        auto r = obj->getObjectRect();
        double sy0 = 0.0, sy1 = 0.0;
        int shz = 0, sdir = 0, sup = 0;
        if ((int)obj->m_objectType == 25) {
            sy0 = obj->slopeYPos(r.origin.x);
            sy1 = obj->slopeYPos(r.origin.x + r.size.width);
            shz = obj->m_slopeIsHazard ? 1 : 0;
            sdir = obj->m_slopeDirection;
            sup = obj->m_slopeUphill ? 1 : 0;
        }
        double tpy = 0.0, tpex = 0.0, tpey = 0.0;
        int tpg = 0, tpix = 0, tpiy = 0;
        int tpfm = 0;
        float tpf = 0.f, tpang = 0.f, tprmod = 0.f, tprmin = 0.f, tprmax = 0.f;
        if ((int)obj->m_objectType == 28 || obj->m_objectID == 3022
            || ((int)obj->m_objectType == 46
                && geode::cast::typeinfo_cast<TeleportPortalObject*>(obj))) {
            auto* tp = static_cast<TeleportPortalObject*>(obj);
            tpy = obj->getRealPosition().y + tp->m_teleportYOffset;
            tpg = tp->m_gravityMode;
            tpix = tp->m_ignoreX ? 1 : 0;
            tpiy = tp->m_ignoreY ? 1 : 0;
            GameObject* exitObj = nullptr;
            if (tp->m_orangePortal) {
                exitObj = tp->m_orangePortal;
                auto ep = tp->m_orangePortal->getRealPosition();
                tpex = ep.x;
                tpey = ep.y;
            } else if (tp->m_targetGroupID > 0) {
                auto* grp = l->getGroup(tp->m_targetGroupID);
                const int n = grp ? (int)grp->count() : 0;
                if (n >= 1) {
                    auto* ex = static_cast<GameObject*>(grp->objectAtIndex(0));
                    exitObj = ex;
                    auto ep = ex->getRealPosition();
                    tpex = ep.x;
                    tpey = ep.y;
                }
                if (n != 1)
                    log::info("solver: teleport uid {} target group {} has {} "
                              "members (random exit - NOT deterministic)",
                              obj->m_uniqueID, tp->m_targetGroupID, n);
            }
            tpfm = (tp->m_staticForceEnabled ? 1 : 0) | (tp->m_staticForceAdditive ? 2 : 0)
                   | (tp->m_redirectForceEnabled ? 4 : 0);
            tpf = tp->m_staticForce;
            tprmod = tp->m_redirectForceMod;
            tprmin = tp->m_redirectForceMin;
            tprmax = tp->m_redirectForceMax;
            if (exitObj) {
                const int eid = exitObj->m_objectID;
                const bool portalLike = eid == 38 || eid == 747 || eid == 749 || eid == 2064
                                        || eid == 2902;
                const float a = (portalLike ? 180.f : 90.f) - exitObj->getRotation();
                tpang = (exitObj->m_isFlipX ? 180.f : 0.f) + a;
            } else {
                tpang = (obj->m_isFlipX ? 180.f : 0.f) - obj->getRotation();
            }
        }
        double tw = 0.0;
        if (obj->m_objectID == 1935)
            tw = static_cast<EffectGameObject*>(obj)->m_timeWarpTimeMod;
        double zm = 0.0, zdur = 0.0, zrate = 0.0;
        int zease = 0;
        if (obj->m_objectID == 1913) {
            auto* e = static_cast<EffectGameObject*>(obj);
            zm = e->m_zoomValue;
            zdur = e->m_duration;
            zease = (int)e->m_easingType;
            zrate = e->m_easingRate;
        }
        int mvdir = 0, gnddir = 0;
        int editvel = 0, ovrvel = 0, dontslide = 0;
        float vmodx = 0.f, vmody = 0.f;
        if (obj->m_objectID == 2900) {
            auto* rg = static_cast<RotateGameplayGameObject*>(obj);
            mvdir = rg->m_moveDirection;
            gnddir = rg->m_groundDirection;
            editvel = rg->m_editVelocity ? 1 : 0;
            ovrvel = rg->m_overrideVelocity ? 1 : 0;
            dontslide = rg->m_dontSlide ? 1 : 0;
            vmodx = rg->m_velocityModX;
            vmody = rg->m_velocityModY;
        }
        const int flx = obj->m_isFlipX ? 1 : 0;
        const int fly = obj->m_isFlipY ? 1 : 0;
        const int nofx = obj->m_hasNoEffects ? 1 : 0;
        const int notch = obj->m_isNoTouch ? 1 : 0;
        int optp1 = 0, optp2 = 0, optudg = 0;
        if (obj->m_objectID == 2899) {
            auto* go = static_cast<GameOptionsTrigger*>(obj);
            optp1 = (int)go->m_disableP1Controls;
            optp2 = (int)go->m_disableP2Controls;
            optudg = (int)go->m_unlinkDualGravity;
        }
        int touch = 0, spawn = 0, chan = 0, axis = 0, exstat = 0, freem = 0, rev = 0;
        if (auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(obj)) {
            rev = e->m_isReverse ? 1 : 0;
            touch = e->m_isTouchTriggered ? 1 : 0;
            spawn = e->m_isSpawnTriggered ? 1 : 0;
            chan = e->m_channelValue;
            axis = (int)e->m_moveTargetMode;
            freem = e->m_cameraIsFreeMode ? 1 : 0;
        }
        if (auto* c = geode::cast::typeinfo_cast<CameraTriggerGameObject*>(obj))
            exstat = c->m_exitStatic ? 1 : 0;
        int multiAct = 0, noMultiAct = 0;
        if (auto* eg = geode::cast::typeinfo_cast<EnhancedGameObject*>(obj)) {
            multiAct = eg->m_isMultiActivate ? 1 : 0;
            noMultiAct = eg->m_isNoMultiActivate ? 1 : 0;
        }
        double gmod = 0.0;
        int gpl = 0;
        if (obj->m_objectID == 2066) {
            auto* e = static_cast<EffectGameObject*>(obj);
            gmod = e->m_gravityValue;
            gpl = (e->m_targetPlayer1 ? 1 : 0) | (e->m_targetPlayer2 ? 2 : 0)
                  | (e->m_followCPP ? 4 : 0);
        }
        rf << obj->m_objectID << "," << (int)obj->m_objectType << ","
           << (r.origin.x + r.size.width * 0.5f) << ","
           << (r.origin.y + r.size.height * 0.5f) << ","
           << r.size.width << "," << r.size.height << ","
           << (int)obj->m_groupCount << "," << obj->m_uniqueID << ","
           << obj->m_objectRadius << "," << obj->getRotation() << ","
           << sy0 << "," << sy1 << "," << shz << "," << sdir << "," << sup
           << "," << obj->m_width << "," << obj->m_height
           << "," << tpy << "," << tpg << "," << tpix << "," << tpiy
           << "," << tw
           << "," << zm << "," << zdur << "," << zease << "," << zrate
           << "," << mvdir << "," << gnddir
           << "," << optp1 << "," << optp2
           << "," << flx << "," << fly << "," << nofx << "," << notch
           << "," << tpex << "," << tpey
           << "," << (obj->m_isDisabled ? 1 : 0)
           << "," << editvel << "," << vmodx << "," << vmody << "," << ovrvel
           << "," << forceOf(obj)
           << "," << freem << "," << touch << "," << spawn << "," << chan
           << "," << axis << "," << exstat << "," << rev
           << "," << (int)(unsigned char)reinterpret_cast<const char*>(obj)[gdoff::kObjPassable]
           << "," << multiAct << "," << noMultiAct
           << "," << gmod << "," << gpl
           << "," << dontslide
           << "," << tpfm << "," << tpf << "," << tpang
           << "," << tprmod << "," << tprmin << "," << tprmax
           << "," << optudg
           << "," << (obj->m_hasExtendedCollision ? 1 : 0)
           << "\n";
    }
}

inline void buildPois(GJBaseGameLayer* l, bool obbNoLatch = false) {
    g_pois.clear();
    g_hazards.clear();
    g_solids.clear();
    g_coins.clear();
    g_hasRotGameplay = false;
    if (!l || !l->m_objects) return;
    for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!obj) continue;
        // Coin extraction (142=secret, 1329=user)
        if (obj->m_objectID == 142 || obj->m_objectID == 1329) {
            auto p = obj->getPosition();
            const float hw = obj->getObjectRect().size.width * 0.5f;
            g_coins.push_back({p.x, p.y, obj->m_objectID, false, obj->m_uniqueID, hw});
            {
                char cb[160];
                snprintf(cb, sizeof(cb),
                         "coinstate: uid=%d id=%d at (%.1f,%.1f) disabled=%d tempdisabled=%d",
                         obj->m_uniqueID, obj->m_objectID, p.x, p.y,
                         obj->m_isGroupDisabled ? 1 : 0,
                         obj->m_isGroupDisabledTemp ? 1 : 0);
                writeResult(cb);
            }
        }
        if (obj->m_objectID == 2900) g_hasRotGameplay = true;   // see the flag
        // Hazard/solid extraction (for the clearance table + diagnostics)
        if (obj->m_objectType == GameObjectType::Hazard) {
            auto p = obj->getPosition();
            g_hazards.push_back({p.x, p.y, obj->m_objectID});
        } else if (obj->m_objectType == GameObjectType::Solid) {
            auto p = obj->getPosition();
            g_solids.push_back({p.x, p.y, obj->m_objectID});
        }
        if (g_gateTrace && obj->m_groupCount > 0
            && (obj->m_objectType == GameObjectType::Solid
                || obj->m_objectType == GameObjectType::Hazard)) {
            auto p = obj->getPosition();
            if (p.x >= g_gateX0 && p.x <= g_gateX1) g_gateObjs.push_back(obj);
        }
        switch (obj->m_objectType) {
            case GameObjectType::YellowJumpRing:
            case GameObjectType::PinkJumpRing:
            case GameObjectType::GravityRing:
            case GameObjectType::GreenRing:
            case GameObjectType::RedJumpRing:
            case GameObjectType::CustomRing:
            case GameObjectType::DashRing:
            case GameObjectType::GravityDashRing:
            case GameObjectType::DropRing:
            case GameObjectType::SpiderOrb:
            case GameObjectType::TeleportOrb: {
                auto p = obj->getPosition();
                bool dash = (obj->m_objectType == GameObjectType::DashRing
                          || obj->m_objectType == GameObjectType::GravityDashRing);
                g_pois.push_back({p.x, p.y, obj->m_objectID, dash});
                break;
            }
            default:
                break;
        }
    }
    std::sort(g_pois.begin(), g_pois.end(),
        [](const Poi& a, const Poi& b) { return a.x < b.x; });
    auto byX = [](const Obj& a, const Obj& b) { return a.x < b.x; };
    std::sort(g_hazards.begin(), g_hazards.end(), byX);
    std::sort(g_solids.begin(), g_solids.end(), byX);
    std::sort(g_coins.begin(), g_coins.end(), [](const Poi& a, const Poi& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    g_coinPickupTick.assign(g_coins.size(), -1);
    g_coinGdTick.assign(g_coins.size(), -1);
    g_coinBest = 0;
    g_coinGdUnmatched = 0;
    // g_coinMoveX: the coins' groups, then every Move (901) that is not spawn-fired and targets one.
    g_coinMoveX.assign(g_coins.size(), -1e9f);
    g_coinObjs.assign(g_coins.size(), nullptr);
    g_coinPosLog.assign(g_coins.size(), {});
    if (!g_coins.empty()) {
        std::vector<std::vector<int>> coinGroups(g_coins.size());
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!obj || (obj->m_objectID != 142 && obj->m_objectID != 1329)) continue;
            for (size_t i = 0; i < g_coins.size(); ++i)
                if (g_coins[i].uid == obj->m_uniqueID) g_coinObjs[i] = obj;
            if (obj->m_groupCount <= 0 || !obj->m_groups) continue;
            for (size_t i = 0; i < g_coins.size(); ++i)
                if (g_coins[i].uid == obj->m_uniqueID) {
                    const int ng = std::min((int)obj->m_groupCount, 10);
                    for (int k = 0; k < ng; ++k) coinGroups[i].push_back((int)(*obj->m_groups)[k]);
                }
        }
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!obj || obj->m_objectID != 901) continue;
            auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(obj);
            if (!e || e->m_targetGroupID <= 0 || e->m_isSpawnTriggered) continue;
            for (size_t i = 0; i < g_coins.size(); ++i)
                for (const int g : coinGroups[i])
                    if (g == e->m_targetGroupID)
                        g_coinMoveX[i] = std::max(g_coinMoveX[i], obj->getPositionX());
        }
        for (size_t i = 0; i < g_coins.size(); ++i)
            if (g_coinMoveX[i] > -1e8f) {
                char mb[128];
                snprintf(mb, sizeof(mb), "coinmove: coin %zu uid=%d is moved by a Move up to x=%.0f",
                         i, g_coins[i].uid, (double)g_coinMoveX[i]);
                writeResult(mb);
            }
    }
    g_levelMaxX = 0;
    g_endTriggers = 0;
    for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!obj) continue;
        g_levelMaxX = std::max(g_levelMaxX, obj->getPositionX());
        if (obj->m_objectID == 3600) ++g_endTriggers;
    }
    if (g_endTriggers > 0) {
        char eb[128];
        snprintf(eb, sizeof(eb), "endtrigger: %d End trigger(s) (id 3600) in the level - GD can "
                 "finish it short of the end portal", g_endTriggers);
        writeResult(eb);
    }
    log::info("solver: levelMaxX={}", g_levelMaxX);
    g_goalX = (l->m_endPortal) ? l->m_endPortal->getPositionX() : 0.f;
    log::info("solver: goalX={} (endPortal), levelMaxX={}", g_goalX, g_levelMaxX);
    log::info("solver: {} orbs, {} hazards, {} solids, {} coins extracted",
        g_pois.size(), g_hazards.size(), g_solids.size(), g_coins.size());
    std::ofstream pf(std::string(DATA_DIR) + "/pois.txt", std::ios::trunc);
    for (auto& p : g_pois) pf << p.x << "," << p.y << "\n";
    // Dump of all objects (objects.txt): id, type, x, y (centre coordinates only)
    std::ofstream of(std::string(DATA_DIR) + "/objects.txt", std::ios::trunc);
    for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!obj) continue;
        auto p = obj->getPosition();
        of << obj->m_objectID << "," << (int)obj->m_objectType
           << "," << p.x << "," << p.y << "\n";
    }
    {
        std::ofstream rf(std::string(DATA_DIR) + "/objrects.txt", std::ios::trunc);
        writeObjRects(rf, l);
    }
    {
        std::ofstream bf(std::string(DATA_DIR) + "/obb.txt", std::ios::trunc);
        bf << "uid,id,type,cx,cy,rot,x0,y0,x1,y1,x2,y2,x3,y3,oob\n";
        long long n = 0;
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!obj) continue;
            const float rot = obj->getRotation();
            const float m = std::fabs(std::fmod(rot, 90.f));
            if (m < 0.5f || m > 89.5f) continue;
            OBB2D* const box0 = obj->m_orientedBox;
            const bool latch0 = obj->m_shouldUseOuterOb;
            const bool boxDirty0 = obj->m_isOrientedBoxDirty;
            const bool rectDirty0 = obj->m_isObjectRectDirty;
            const cocos2d::CCRect rect0 = obj->m_objectRect;
            auto* ob = obj->getOrientedBox();
            if (ob) {
                auto p = obj->getPosition();
                bf << obj->m_uniqueID << "," << obj->m_objectID << ","
                   << (int)obj->m_objectType << "," << p.x << "," << p.y << ","
                   << rot;
                for (int i = 0; i < 4; ++i)
                    bf << "," << ob->m_corners[i].x << "," << ob->m_corners[i].y;
                bf << "," << ((obbNoLatch ? latch0 : obj->m_shouldUseOuterOb) ? 1 : 0) << "\n";
                ++n;
            }
            if (obbNoLatch) {
                obj->m_shouldUseOuterOb = latch0;
                obj->m_isOrientedBoxDirty = boxDirty0;
                obj->m_isObjectRectDirty = rectDirty0;
                obj->m_objectRect = rect0;
                if (!box0 && obj->m_orientedBox) {
                    obj->m_orientedBox->release();
                    obj->m_orientedBox = nullptr;
                }
            }
        }
        log::info("obb: {} rotated objects", n);
    }
    {
        std::ofstream ff(std::string(DATA_DIR) + "/forceblocks.txt",
                         std::ios::trunc);
        ff << "uid,id,type,cx,cy,rot,scalex,scaley,radius,rectw,recth,"
              "force,minforce,maxforce,relative,range,forceid\n";
        long long n = 0;
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            auto* fb = geode::cast::typeinfo_cast<ForceBlockGameObject*>(obj);
            if (!fb) continue;
            const auto p = fb->getPosition();
            const auto r = fb->getObjectRect();
            ff << fb->m_uniqueID << "," << fb->m_objectID << ","
               << (int)fb->m_objectType << "," << p.x << "," << p.y << ","
               << fb->getRotation() << "," << fb->m_scaleX << ","
               << fb->m_scaleY << "," << fb->m_objectRadius << ","
               << r.size.width << "," << r.size.height << ","
               << fb->m_force << "," << fb->m_minForce << ","
               << fb->m_maxForce << "," << (fb->m_relativeForce ? 1 : 0) << ","
               << (fb->m_forceRange ? 1 : 0) << "," << fb->m_forceID << "\n";
            ++n;
        }
        log::info("forceblocks: {} force blocks", n);
    }
    {
        std::ofstream tf(std::string(DATA_DIR) + "/triggers.txt", std::ios::trunc);
        tf << "uid,id,cx,cy,w,h,target,center,touch,spawn,dur,ox,oy,"
              "ease,erate,lockx,locky,grav,gravmod,"
              "deg,ord,chan,sord,sordd,t360,lockrot,"
              "sdelay,mvtgt,mvaxis,tmodctr,dirsnap,dirdist,dynmode,silent,togon,"
              "remap,item,item2,count,subcount,actgrp,thold,ttog,tdual,cmode,"
              "i1mode,i2mode,tgtmode,mod1,mod2,res1,res2,res3,tol,rnd1,rnd2,"
              "sgn1,sgn2,"
              "sponly,fyspd,fydel,fyoff,fymax\n";
        std::ofstream gf(std::string(DATA_DIR) + "/objgroups.txt", std::ios::trunc);
        gf << "uid,groups\n";
        std::ofstream itf(std::string(DATA_DIR) + "/items.txt", std::ios::trunc);
        itf << "uid,id,cx,cy,w,h,item,points,pickup,toggle,subcount\n";
        long long nItem = 0;
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!obj) continue;
            log::info("offsets: GameObject::m_isGroupDisabled=+{:#x} "
                      "m_isGroupDisabledTemp=+{:#x}",
                      (size_t)(reinterpret_cast<char const*>(&obj->m_isGroupDisabled)
                               - reinterpret_cast<char const*>(obj)),
                      (size_t)(reinterpret_cast<char const*>(&obj->m_isGroupDisabledTemp)
                               - reinterpret_cast<char const*>(obj)));
            break;
        }
        long long nTrig = 0, nGrp = 0;
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!obj) continue;
            if (obj->m_groupCount > 0 && obj->m_groups) {
                gf << obj->m_uniqueID;
                const int ng = std::min((int)obj->m_groupCount, 10);
                for (int i = 0; i < ng; ++i) gf << " " << (int)(*obj->m_groups)[i];
                gf << "\n";
                ++nGrp;
            }
            auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(obj);
            if (e && e->m_targetGroupID == 0
                && (e->m_collectibleIsPickupItem || e->m_itemID != 0)) {
                auto ir = obj->getObjectRect();
                itf << obj->m_uniqueID << "," << obj->m_objectID << ","
                    << (ir.origin.x + ir.size.width * 0.5f) << ","
                    << (ir.origin.y + ir.size.height * 0.5f) << ","
                    << ir.size.width << "," << ir.size.height << ","
                    << e->m_itemID << "," << e->m_collectiblePoints << ","
                    << (e->m_collectibleIsPickupItem ? 1 : 0) << ","
                    << (e->m_collectibleIsToggleTrigger ? 1 : 0) << ","
                    << (e->m_subtractCount ? 1 : 0) << "\n";
                ++nItem;
            }
            if (!e || e->m_targetGroupID == 0) continue;
            auto tr = obj->getObjectRect();
            tf << obj->m_uniqueID << "," << obj->m_objectID << ","
               << (tr.origin.x + tr.size.width * 0.5f) << ","
               << (tr.origin.y + tr.size.height * 0.5f) << ","
               << tr.size.width << "," << tr.size.height << ","
               << e->m_targetGroupID << "," << e->m_centerGroupID << ","
               << (e->m_isTouchTriggered ? 1 : 0) << ","
               << (e->m_isSpawnTriggered ? 1 : 0) << ","
               << e->m_duration << "," << e->m_moveOffset.x << ","
               << e->m_moveOffset.y << ","
               << (int)e->m_easingType << "," << e->m_easingRate << ","
               << (e->m_lockToPlayerX ? 1 : 0) << ","
               << (e->m_lockToPlayerY ? 1 : 0) << ","
               << e->m_gravityValue << "," << e->m_gravityMod << ","
               << e->m_rotationDegrees << "," << e->m_ordValue << ","
               << e->m_channelValue << "," << e->m_spawnOrder << ","
               << (e->m_spawnOrdered ? 1 : 0) << ","
               << e->m_times360 << ","
               << (e->m_lockObjectRotation ? 1 : 0) << ","
               << e->m_spawnTriggerDelay << ","
               << (e->m_useMoveTarget ? 1 : 0) << ","
               << (int)e->m_moveTargetMode << ","
               << e->m_targetModCenterID << ","
               << (e->m_isDirectionFollowSnap360 ? 1 : 0) << ","
               << e->m_directionModeDistance << ","
               << (e->m_isDynamicMode ? 1 : 0) << ","
               << (e->m_isSilent ? 1 : 0) << ","
               << (obj->m_objectID == 1049 ? (e->m_activateGroup ? 1 : 0) : -1) << ",";
            {
                std::string rm;
                if (auto* sp = geode::cast::typeinfo_cast<SpawnTriggerGameObject*>(obj)) {
                    for (auto const& c : sp->m_remapObjects) {
                        if (!rm.empty()) rm += ";";
                        rm += std::to_string(c.m_groupID) + ":"
                            + std::to_string(c.m_oldGroupID) + ":"
                            + std::to_string(c.m_chance) + ":"
                            + std::to_string(c.m_unk00c);
                    }
                }
                tf << (rm.empty() ? std::string("-") : rm) << ",";
            }
            {
                int cnt = -1, cmode = -1;
                if (auto* c = geode::cast::typeinfo_cast<CountTriggerGameObject*>(obj)) {
                    cnt = c->m_pickupCount;
                    cmode = c->m_pickupTriggerMode;
                }
                tf << e->m_itemID << "," << e->m_itemID2 << "," << cnt << ","
                   << (e->m_subtractCount ? 1 : 0) << ","
                   << (e->m_activateGroup ? 1 : 0) << ","
                   << (e->m_touchHoldMode ? 1 : 0) << ","
                   << (int)e->m_touchToggleMode << ","
                   << (e->m_isDualMode ? 1 : 0) << "," << cmode;
                if (auto* it = geode::cast::typeinfo_cast<ItemTriggerGameObject*>(obj))
                    tf << "," << it->m_item1Mode << "," << it->m_item2Mode << ","
                       << it->m_targetItemMode << "," << it->m_mod1 << ","
                       << it->m_mod2 << "," << it->m_resultType1 << ","
                       << it->m_resultType2 << "," << it->m_resultType3 << ","
                       << it->m_tolerance << "," << it->m_roundType1 << ","
                       << it->m_roundType2 << "," << it->m_signType1 << ","
                       << it->m_signType2;
                else
                    tf << ",-1,-1,-1,0,0,-1,-1,-1,0,-1,-1,-1,-1";
                auto* ring = geode::cast::typeinfo_cast<RingObject*>(obj);
                tf << "," << ((ring && ring->m_isSpawnOnly) ? 1 : 0) << ","
                   << e->m_followYSpeed << "," << e->m_followYDelay << ","
                   << e->m_followYOffset << "," << e->m_followYMaxSpeed << "\n";
            }
            ++nTrig;
        }
        log::info("triggers: {} triggers with a target, {} grouped objects, "
                  "{} pickups", nTrig, nGrp, nItem);
    }
    {
        std::ofstream rf(std::string(DATA_DIR) + "/rotgameplay.txt",
                         std::ios::trunc);
        rf << "uid,id,cx,cy,chan,ord,sord,sordd,spx,target,"
              "chanChanged,swarm,chanOnly,swch\n";
        long long n = 0;
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!obj || (obj->m_objectID != 2900 && obj->m_objectID != 2899))
                continue;
            auto* e = geode::cast::typeinfo_cast<EffectGameObject*>(obj);
            if (!e) continue;
            auto tr = obj->getObjectRect();
            rf << obj->m_uniqueID << "," << obj->m_objectID << ","
               << (tr.origin.x + tr.size.width * 0.5f) << ","
               << (tr.origin.y + tr.size.height * 0.5f) << ","
               << e->m_channelValue << "," << e->m_ordValue << ","
               << e->m_spawnOrder << "," << (e->m_spawnOrdered ? 1 : 0) << ","
               << e->m_spawnXPosition << "," << e->m_targetGroupID << ","
               << (e->m_channelChanged ? 1 : 0) << ",";
            if (auto* r = geode::cast::typeinfo_cast<RotateGameplayGameObject*>(obj))
                rf << (r->m_changeChannel ? 1 : 0) << ","
                   << (r->m_channelOnly ? 1 : 0) << ","
                   << r->m_targetChannelID << "\n";
            else
                rf << "-,-,-\n";
            ++n;
        }
        if (n) log::info("rotgameplay: {} rotation-gameplay objects", n);
    }
    {
        std::ofstream af(std::string(DATA_DIR) + "/areas.txt", std::ios::trunc);
        af << "uid,id,cx,cy,target,special,raw614,center,dirType,xy,inbound,twoDir,len,lenV,off,offV,offY,offYV,"
              "modFront,modBack,deadzone,moveDist,moveDistV,angle,angleV,moveX,moveXV,moveY,moveYV,relative,"
              "relFade,eInType,eInRate,eInBuf,eOut,eOutType,eOutRate,eOutBuf,dur,scaleX,scaleXV,scaleY,scaleYV,"
              "rot,rotV\n";
        long long n = 0;
        for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
            if (!obj || obj->m_objectID < 3006 || obj->m_objectID > 3010) continue;
            auto* e = geode::cast::typeinfo_cast<EnterEffectObject*>(obj);
            if (!e) continue;
            auto tr = obj->getObjectRect();
            af << obj->m_uniqueID << "," << obj->m_objectID << ","
               << (tr.origin.x + tr.size.width * 0.5f) << "," << (tr.origin.y + tr.size.height * 0.5f) << ","
               << e->m_targetGroupID << "," << e->m_specialTarget << ","
               << *reinterpret_cast<int*>(reinterpret_cast<char*>(e) + 0x614) << "," << e->m_centerGroupID << ","
               << e->m_directionType << "," << (e->m_xyMode ? 1 : 0) << "," << (e->m_inbound ? 1 : 0) << ","
               << (e->m_twoDirections ? 1 : 0) << "," << e->m_length << "," << e->m_lengthVariance << ","
               << e->m_offset << "," << e->m_offsetVariance << "," << e->m_offsetY << "," << e->m_offsetYVariance
               << "," << e->m_modFront << "," << e->m_modBack << "," << e->m_deadzone << "," << e->m_moveDistance
               << "," << e->m_moveDistanceVariance << "," << e->m_moveAngle << "," << e->m_moveAngleVariance << ","
               << e->m_moveX << "," << e->m_moveXVariance << "," << e->m_moveY << "," << e->m_moveYVariance << ","
               << (e->m_relative ? 1 : 0) << "," << e->m_relativeFade << "," << (int)e->m_easingInType << ","
               << e->m_easingInRate << "," << e->m_easingInBuffer << "," << (e->m_easeOutEnabled ? 1 : 0) << ","
               << (int)e->m_easingOutType << "," << e->m_easingOutRate << "," << e->m_easingOutBuffer << ","
               << e->m_duration << "," << e->m_areaScaleX << "," << e->m_areaScaleXVariance << ","
               << e->m_areaScaleY << "," << e->m_areaScaleYVariance << "," << e->m_areaRotation << ","
               << e->m_areaRotationVariance << "\n";
            ++n;
        }
        if (n) log::info("areas: {} area triggers", n);
    }
    if (auto* ls = l->m_levelSettings) {
        std::ofstream sf(std::string(DATA_DIR) + "/levelsettings.txt",
                         std::ios::trunc);
        sf << "fixRadiusCollision=" << (ls->m_fixRadiusCollision ? 1 : 0)
           << "\nfixGravityBug=" << (ls->m_fixGravityBug ? 1 : 0)
           << "\nfixNegativeScale=" << (ls->m_fixNegativeScale ? 1 : 0)
           << "\nfixRobotJump=" << (ls->m_fixRobotJump ? 1 : 0)
           << "\nenableImpulseFix=" << (ls->m_enableImpulseFix ? 1 : 0)
           << "\ndynamicLevelHeight=" << (ls->m_dynamicLevelHeight ? 1 : 0)
           << "\nplatformerMode=" << (ls->m_platformerMode ? 1 : 0)
           << "\nreverseGameplay=" << (ls->m_reverseGameplay ? 1 : 0)
           << "\n";
        sf.precision(9);
        sf << "endPortalX=" << (l->m_endPortal ? l->m_endPortal->getPositionX() : 0.f)
           << "\nendPortalFlipX=" << ((l->m_endPortal && l->m_endPortal->m_flippedX) ? 1 : 0)
           << "\nendTriggers=" << g_endTriggers
           << "\n";
        log::info("levelsettings: fixRadiusCollision={} platformer={} "
                  "reverse={}", (int)ls->m_fixRadiusCollision,
                  (int)ls->m_platformerMode, (int)ls->m_reverseGameplay);
    }
}

// Measures which player a death in a dual section came from (destroyPlayer counts)
inline long long g_deathsP1 = 0, g_deathsP2 = 0, g_deathsOther = 0;
inline long long g_anticheatCalls = 0;
inline long long g_killTick = -1;
inline int g_killUid = -1;
inline float g_killX = 0.f, g_killY = 0.f;

inline long long g_lastDeathTick = 0;
inline long long g_lastGroundGap = 0;
inline long long g_injThisAttempt = 0;   // number of injections applied in this attempt
inline long long g_injAtDeath = 0;       // number of injections at the most recent death

namespace ckpttrace {
inline bool g_on = false;
inline int g_lastTry = -1;      // previous-tick value of m_shouldTryPlacingCheckpoint
                                // (-1=uninitialised)
inline int g_lastTimeout = -1;  // previous-tick value of m_checkpointTimeout
inline long long g_flagLines = 0;        // flag lines emitted (counter against overflow)
constexpr long long kFlagCap = 5000;     // beyond this, flag lines are dropped (the cut-off
                                         // is stated explicitly)
}
} // namespace solver
