#pragma once
#include <memory>

namespace p1 {
namespace cpflight {
struct CamMem { size_t off, size; };
inline const std::vector<CamMem>& gameStateScalars() {
#define CPF_GS(m) CamMem{offsetof(GJGameState, m), sizeof(GJGameState::m)}
    static const std::vector<CamMem> v = {
        CPF_GS(m_cameraZoom), CPF_GS(m_targetCameraZoom), CPF_GS(m_cameraOffset),
        CPF_GS(m_unkPoint1), CPF_GS(m_unkPoint2), CPF_GS(m_unkPoint3), CPF_GS(m_unkPoint4),
        CPF_GS(m_unkPoint5), CPF_GS(m_unkPoint6), CPF_GS(m_unkPoint7), CPF_GS(m_unkPoint8),
        CPF_GS(m_unkPoint9), CPF_GS(m_unkPoint10), CPF_GS(m_unkPoint11), CPF_GS(m_unkPoint12),
        CPF_GS(m_unkPoint13), CPF_GS(m_unkPoint14), CPF_GS(m_unkPoint15), CPF_GS(m_unkPoint16),
        CPF_GS(m_unkPoint17), CPF_GS(m_unkPoint18), CPF_GS(m_unkPoint19), CPF_GS(m_unkPoint20),
        CPF_GS(m_unkPoint21), CPF_GS(m_unkPoint22), CPF_GS(m_unkPoint23), CPF_GS(m_unkPoint24),
        CPF_GS(m_unkPoint25), CPF_GS(m_unkPoint26), CPF_GS(m_unkPoint27), CPF_GS(m_unkPoint28),
        CPF_GS(m_unkPoint29), CPF_GS(m_unkBool1), CPF_GS(m_unkInt1), CPF_GS(m_unkBool2),
        CPF_GS(m_unkInt2), CPF_GS(m_unkBool3), CPF_GS(m_unkPoint30), CPF_GS(m_middleGroundOffsetY),
        CPF_GS(m_unkInt3), CPF_GS(m_unkInt4), CPF_GS(m_unkBool4), CPF_GS(m_unkBool5),
        CPF_GS(m_unkFloat2), CPF_GS(m_unkFloat3), CPF_GS(m_unkInt5), CPF_GS(m_unkInt6),
        CPF_GS(m_unkInt7), CPF_GS(m_unkInt8), CPF_GS(m_unkInt9), CPF_GS(m_unkInt10),
        CPF_GS(m_unkInt11), CPF_GS(m_unkFloat4), CPF_GS(m_unkUint1), CPF_GS(m_portalY),
        CPF_GS(m_unkBool6), CPF_GS(m_gravityRelated), CPF_GS(m_unkInt12), CPF_GS(m_unkInt13),
        CPF_GS(m_unkInt14), CPF_GS(m_unkInt15), CPF_GS(m_unkBool7), CPF_GS(m_isFreeMode),
        CPF_GS(m_unkBool9), CPF_GS(m_unkFloat5), CPF_GS(m_unkFloat6), CPF_GS(m_unkFloat7),
        CPF_GS(m_unkFloat8), CPF_GS(m_cameraAngle), CPF_GS(m_targetCameraAngle),
        CPF_GS(m_playerStreakBlend), CPF_GS(m_timeWarp), CPF_GS(m_queuedTimeWarp),
        CPF_GS(m_timeWarpRelated), CPF_GS(m_currentChannel), CPF_GS(m_rotateChannel),
        CPF_GS(m_totalTime), CPF_GS(m_levelTime), CPF_GS(m_unkDouble3), CPF_GS(m_commandIndex),
        CPF_GS(m_unkUint3), CPF_GS(m_currentProgress), CPF_GS(m_unkUint4), CPF_GS(m_unkUint5),
        CPF_GS(m_unkUint6), CPF_GS(m_unkUint7), CPF_GS(m_unkUint8),
        // two objects of the same layer, which a restore of the loop never re-creates
        CPF_GS(m_lastActivatedPortal1), CPF_GS(m_lastActivatedPortal2),
        CPF_GS(m_cameraPosition), CPF_GS(m_unkBool10), CPF_GS(m_levelFlipping), CPF_GS(m_unkBool11),
        CPF_GS(m_unkBool12), CPF_GS(m_isDualMode), CPF_GS(m_unkFloat9), CPF_GS(m_cameraEdgeValue0),
        CPF_GS(m_cameraEdgeValue1), CPF_GS(m_cameraEdgeValue2), CPF_GS(m_cameraEdgeValue3),
        CPF_GS(m_unkUint10), CPF_GS(m_unkUint11), CPF_GS(m_unkUint12), CPF_GS(m_cameraStepDiff),
        CPF_GS(m_unkFloat10), CPF_GS(m_timeModRelated), CPF_GS(m_timeModRelated2),
        CPF_GS(m_unkUint13), CPF_GS(m_unkPoint32), CPF_GS(m_cameraPosition2), CPF_GS(m_unkBool20),
        CPF_GS(m_unkBool21), CPF_GS(m_unkBool22), CPF_GS(m_unkUint14), CPF_GS(m_unkBool26),
        CPF_GS(m_cameraShakeEnabled), CPF_GS(m_cameraShakeDuration), CPF_GS(m_cameraShakeStrength),
        CPF_GS(m_cameraShakeInterval), CPF_GS(m_lastShakeTime), CPF_GS(m_unkPoint34),
        CPF_GS(m_dualRelated), CPF_GS(m_unkBool27), CPF_GS(m_unkBool28), CPF_GS(m_unkBool29),
        CPF_GS(m_unkUint17), CPF_GS(m_unkBool30), CPF_GS(m_background), CPF_GS(m_ground),
        CPF_GS(m_middleground), CPF_GS(m_unkBool31), CPF_GS(m_points), CPF_GS(m_unkBool32),
        CPF_GS(m_pauseCounter), CPF_GS(m_pauseBufferTimer),
    };
#undef CPF_GS
    return v;
}

struct LayerMem { size_t off, size; };
inline const std::vector<LayerMem>& layerScalars() {
#define CPF_LM(m) LayerMem{offsetof(PlayLayer, m), sizeof(PlayLayer::m)}
    static const std::vector<LayerMem> v = {
        CPF_LM(m_resumeTimer), CPF_LM(m_currentStep), CPF_LM(m_timePlayed), CPF_LM(m_tickIndex),
        CPF_LM(m_clickIndex), CPF_LM(m_timestamp), CPF_LM(m_songTriggerInterval), CPF_LM(m_attemptTime),
        CPF_LM(m_currentTime), CPF_LM(m_cameraFlip), CPF_LM(m_cameraWidthOffset),
        CPF_LM(m_cameraHeightOffset), CPF_LM(m_freezeStartCamera), CPF_LM(m_cameraUnzoomedHeightOffset),
        CPF_LM(m_targetCameraHeightOffset), CPF_LM(m_calculateTargetHeightOffset),
        CPF_LM(m_staticCameraShake), CPF_LM(m_skipCameraShake), CPF_LM(m_cameraWidth),
        CPF_LM(m_cameraHeight), CPF_LM(m_cameraUnzoomedX), CPF_LM(m_halfCameraWidth),
    };
#undef CPF_LM
    return v;
}

struct Camera {
    std::vector<uint8_t> gs;                    // gameStateScalars(), in order
    std::vector<uint8_t> layer;                 // layerScalars(), in order
    decltype(GJGameState::m_tweenActions) tweens;
    decltype(GJGameState::m_unkMapPairGJGameEventIntVectorEventTriggerInstance) events;
    decltype(GJGameState::m_unkMapPairGJGameEventIntInt) eventCounts;
    struct Node { bool have = false; cocos2d::CCPoint pos; float sx = 1.f, sy = 1.f, rot = 0.f; };
    Node objectLayer, ground1, ground2, middleground, background;
};

inline void captureNode(cocos2d::CCNode* n, Camera::Node& out) {
    out.have = n != nullptr;
    if (!n) return;
    out.pos = n->getPosition();
    out.sx = n->getScaleX();
    out.sy = n->getScaleY();
    out.rot = n->getRotation();
}

inline void restoreNode(cocos2d::CCNode* n, const Camera::Node& in) {
    if (!n || !in.have) return;
    n->setPosition(in.pos);
    n->setScaleX(in.sx);
    n->setScaleY(in.sy);
    n->setRotation(in.rot);
}

inline void captureCamera(GJBaseGameLayer* l, Camera& c) {
    const auto* base = reinterpret_cast<const uint8_t*>(&l->m_gameState);
    c.gs.clear();
    for (const CamMem& m : gameStateScalars()) c.gs.insert(c.gs.end(), base + m.off, base + m.off + m.size);
    const auto* lb = reinterpret_cast<const uint8_t*>(static_cast<PlayLayer*>(l));
    c.layer.clear();
    for (const LayerMem& m : layerScalars()) c.layer.insert(c.layer.end(), lb + m.off, lb + m.off + m.size);
    c.tweens = l->m_gameState.m_tweenActions;
    c.events = l->m_gameState.m_unkMapPairGJGameEventIntVectorEventTriggerInstance;
    c.eventCounts = l->m_gameState.m_unkMapPairGJGameEventIntInt;
    captureNode(l->m_objectLayer, c.objectLayer);
    captureNode(l->m_groundLayer, c.ground1);
    captureNode(l->m_groundLayer2, c.ground2);
    captureNode(l->m_middleground, c.middleground);
    captureNode(l->m_background, c.background);
}

inline void restoreCamera(GJBaseGameLayer* l, const Camera& c) {
    auto* base = reinterpret_cast<uint8_t*>(&l->m_gameState);
    size_t o = 0;
    for (const CamMem& m : gameStateScalars()) {
        if (o + m.size > c.gs.size()) break;
        std::memcpy(base + m.off, c.gs.data() + o, m.size);
        o += m.size;
    }
    auto* lb = reinterpret_cast<uint8_t*>(static_cast<PlayLayer*>(l));
    o = 0;
    for (const LayerMem& m : layerScalars()) {
        if (o + m.size > c.layer.size()) break;
        std::memcpy(lb + m.off, c.layer.data() + o, m.size);
        o += m.size;
    }
    l->m_gameState.m_tweenActions = c.tweens;
    l->m_gameState.m_unkMapPairGJGameEventIntVectorEventTriggerInstance = c.events;
    l->m_gameState.m_unkMapPairGJGameEventIntInt = c.eventCounts;
    restoreNode(l->m_objectLayer, c.objectLayer);
    restoreNode(l->m_groundLayer, c.ground1);
    restoreNode(l->m_groundLayer2, c.ground2);
    restoreNode(l->m_middleground, c.middleground);
    restoreNode(l->m_background, c.background);
}

struct ObjOffsets {
    GameObject* obj = nullptr;
    float v[10] = {};
    double px = 0.0, py = 0.0;
    float sx = 1.f, sy = 1.f;
    int stamp = 0;
    bool skip = false;
};

static_assert(offsetof(GameObject, m_unk2C0) == offsetof(GameObject, m_positionXOffset) + 9 * sizeof(float),
              "the area offsets are ten consecutive floats");

struct StaleNode {
    GameObject* obj = nullptr;
    cocos2d::CCPoint pos;
};

inline bool isEnhanced(GameObject* o) {
    return o->m_classType == GameObjectClassType::Effect || o->m_classType == GameObjectClassType::Animated
           || o->m_classType == GameObjectClassType::Enhanced;
}

struct AreaState {
    std::vector<StaleNode> staleNodes;
    std::vector<ObjOffsets> objs;
    std::vector<short> variance;               // m_objects order
    std::vector<uint8_t> latch;                // m_objects order
    std::vector<uint8_t> activated;
    std::vector<GameObject*> area, processed;  // the lists' first `count` entries
    bool haveActivation = false;
    std::vector<uint8_t> activatedFlag;
    std::vector<GameObject*> activeObjects;
    int activeCount = 0, activeIndex = 0;
    int areaCount = 0, areaIndex = 0, processedCount = 0, processedIndex = 0;
};

inline void captureArea(GJBaseGameLayer* l, AreaState& a) {
    a = AreaState{};
    if (!l || !l->m_objects) return;
    const int stamp = (int)l->m_gameState.m_commandIndex;
    a.variance.reserve(l->m_objects->count());
    a.latch.reserve(l->m_objects->count());
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        a.variance.push_back(o ? o->m_varianceIndex : (short)0);
        a.latch.push_back(o && o->m_shouldUseOuterOb ? 1 : 0);
        a.activated.push_back(0xff);
        if (!o) continue;
        if (isEnhanced(o)) {
            auto* e = static_cast<EnhancedGameObject*>(o);
            a.activated.back() = (uint8_t)((e->m_activated ? 1 : 0) | (e->m_activatedByPlayer1 ? 2 : 0)
                                           | (e->m_activatedByPlayer2 ? 4 : 0));
        }
        const cocos2d::CCPoint node = o->getPosition();
        if (node.x != (float)o->m_positionX || node.y != (float)o->m_positionY)
            a.staleNodes.push_back(StaleNode{o, node});
        const float* f = &o->m_positionXOffset;
        bool any = o->m_unk4C8 == stamp;
        for (int i = 0; i < 10 && !any; ++i) any = f[i] != 0.f;
        if (!any) continue;
        ObjOffsets e;
        e.obj = o;
        std::memcpy(e.v, f, sizeof(e.v));
        e.px = o->m_positionX;
        e.py = o->m_positionY;
        e.sx = o->m_scaleX;
        e.sy = o->m_scaleY;
        e.stamp = o->m_unk4C8;
        e.skip = o->m_unk508;
        a.objs.push_back(e);
    }
    if (g_cfg.cpActivation) {
        a.haveActivation = true;
        a.activatedFlag.reserve(l->m_objects->count());
        for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) a.activatedFlag.push_back(o && o->m_isActivated ? 1 : 0);
        a.activeObjects = l->m_activeObjects;
        a.activeCount = l->m_activeObjectsCount;
        a.activeIndex = l->m_activeObjectsIndex;
    }
    a.areaCount = l->m_areaObjectsCount;
    a.areaIndex = l->m_areaObjectsIndex;
    a.processedCount = l->m_processedAreaObjectsCount;
    a.processedIndex = l->m_processedAreaObjectsIndex;
    for (int k = 0; k < a.areaCount && (size_t)k < l->m_areaObjects.size(); ++k)
        a.area.push_back(l->m_areaObjects[k]);
    for (int k = 0; k < a.processedCount && (size_t)k < l->m_processedAreaObjects.size(); ++k)
        a.processed.push_back(l->m_processedAreaObjects[k]);
}

inline void restoreArea(GJBaseGameLayer* l, const AreaState& a) {
    if (!l || !l->m_objects) return;
    size_t i = 0;
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        if (i >= a.variance.size()) break;
        if (o) {
            o->m_varianceIndex = a.variance[i];
            if (i < a.activated.size() && a.activated[i] != 0xff && isEnhanced(o)) {
                auto* e = static_cast<EnhancedGameObject*>(o);
                e->m_activated = (a.activated[i] & 1) != 0;
                e->m_activatedByPlayer1 = (a.activated[i] & 2) != 0;
                e->m_activatedByPlayer2 = (a.activated[i] & 4) != 0;
            }
            const bool latch = i < a.latch.size() && a.latch[i];
            if (o->m_shouldUseOuterOb != latch) {
                o->m_shouldUseOuterOb = latch;
                o->m_isObjectRectDirty = true;
                o->m_isOrientedBoxDirty = true;
            }
        }
        ++i;
    }
    for (const ObjOffsets& e : a.objs) {
        GameObject* o = e.obj;
        std::memcpy(&o->m_positionXOffset, e.v, sizeof(e.v));
        o->m_positionX = e.px;
        o->m_positionY = e.py;
        o->m_scaleX = e.sx;
        o->m_scaleY = e.sy;
        o->m_unk4C8 = e.stamp;
        o->m_unk508 = e.skip;
        o->m_isObjectRectDirty = true;
        o->m_isOrientedBoxDirty = true;
        l->updateObjectSection(o);
    }
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!o) continue;
        const cocos2d::CCPoint real((float)o->m_positionX, (float)o->m_positionY);
        const cocos2d::CCPoint node = o->getPosition();
        if (node.x != real.x || node.y != real.y) o->cocos2d::CCNode::setPosition(real);
    }
    for (const StaleNode& s : a.staleNodes) s.obj->cocos2d::CCNode::setPosition(s.pos);
    for (size_t k = 0; k < a.area.size() && k < l->m_areaObjects.size(); ++k)
        l->m_areaObjects[k] = a.area[k];
    for (size_t k = 0; k < a.processed.size() && k < l->m_processedAreaObjects.size(); ++k)
        l->m_processedAreaObjects[k] = a.processed[k];
    l->m_areaObjectsCount = (int)std::min(a.area.size(), l->m_areaObjects.size());
    l->m_areaObjectsIndex = a.areaIndex;
    l->m_processedAreaObjectsCount = (int)std::min(a.processed.size(), l->m_processedAreaObjects.size());
    l->m_processedAreaObjectsIndex = a.processedIndex;
    if (a.haveActivation && g_cfg.cpActivation) {
        size_t k = 0;
        for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
            if (k >= a.activatedFlag.size()) break;
            if (o) {
                const bool want = a.activatedFlag[k] != 0;
                if (g_cfg.cpActivation >= 2) {
                    if (o->m_isActivated && !want) o->deactivateObject(false);
                    else if (!o->m_isActivated && want) o->activateObject();
                }
                o->m_isActivated = want;
            }
            ++k;
        }
        if (g_cfg.cpActivation >= 2) {
            l->m_activeObjects = a.activeObjects;
            l->m_activeObjectsCount = a.activeCount;
            l->m_activeObjectsIndex = a.activeIndex;
        }
    }
}

struct GtState {
    float cx = 0, cy = 0, w = 0, h = 0, rot = 0;
    int on = -1, env = 0;
    bool emitted = false;
};

// One checkpoint and what it does not carry.
struct Rec {
    long long tick = -1;
    CheckpointObject* cp = nullptr;             // retained, out of GD's checkpoint list
    const void* layer = nullptr;
    size_t objects = 0;
    std::vector<uint8_t> player;
    secsolve::DashState dash, dash2;
    psnap::Touch touch;
    bool damageVerified = true;
    int held = 0;
    int oobLatch = 0;
    // the input cursors as the flight had them
    size_t nextInput = 0, nextToggle = 0;
    // GD's random seeds: the trigger seed, the layer's copy of it, and the two rngfix pins
    long long seedTrig = 0, seedKept = 0, seedVarIndex = 0, seedVarTable = 0;
    Camera camera;
    AreaState area;
    struct FollowCell { GameObject* obj; int sx, sy; };
    std::vector<FollowCell> followCells;
    std::vector<uint8_t> layerBytes;   // cfg cpflightlayerdiff only
    std::vector<std::vector<uint8_t>> afterLayer, afterPlayer;
    std::vector<long long> afterTick;
    std::vector<std::string> afterTweens;
    int gameFrame = 0;
    // the loop's per-attempt books at T
    std::vector<long long> coinGd, coinPickup;
    int coinGdUnmatched = 0;
    bool coinMissFired = false;
    int coinMissIdx = -1;
    long long endTrigger = -1;
    std::vector<size_t> coinPosLen;
    std::map<int, int> itemCounts;
    std::vector<uint8_t> coinLiveSaid;
    std::vector<long long> routeOn;
    std::vector<uint8_t> routeSeenOff;
    long long gtBytes = -1, gtRows = 0, gtLastTick = -1;
    std::vector<GtState> gt;
    std::unordered_map<GameObject*, areaenv::Entry> envEntries;
    std::unordered_map<GameObject*, areaenv::Shape> envShapes;
    Rec() = default;
    Rec(const Rec&) = delete;
    Rec& operator=(const Rec&) = delete;
    ~Rec() { if (cp) cp->release(); }
};

// The attempt's moving-geometry recording as it was rolled, kept while a set refers to it.
struct GtFile {
    std::string path;
    ~GtFile() {
        std::error_code ec;
        if (!path.empty()) std::filesystem::remove(path, ec);
    }
};

struct BandCarry {
    uint32_t centre = 0, height = 0;
    bool operator==(const BandCarry& o) const { return centre == o.centre && height == o.height; }
};

inline BandCarry bandCarry(GJBaseGameLayer* l) {
    BandCarry c;
    std::memcpy(&c.centre, &l->m_gameState.m_unkInt14, 4);
    std::memcpy(&c.height, &l->m_gameState.m_unkInt11, 4);
    return c;
}

inline uint64_t fnvAdd(uint64_t h, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

// One stale node: the object's uid and its node position's float bits.
struct NodeEntry {
    int uid = 0;
    uint32_t x = 0, y = 0;
    bool operator<(const NodeEntry& o) const { return uid != o.uid ? uid < o.uid : (x != o.x ? x < o.x : y < o.y); }
    bool operator==(const NodeEntry& o) const { return uid == o.uid && x == o.x && y == o.y; }
};

inline std::vector<NodeEntry> staleNodeSet(GJBaseGameLayer* l, bool afterReset) {
    std::vector<NodeEntry> v;
    if (!l || !l->m_objects) return v;
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!o) continue;
        const cocos2d::CCPoint node = o->getPosition();
        const float rx = afterReset ? o->m_startPosition.x : (float)o->m_positionX;
        const float ry = afterReset ? o->m_startPosition.y : (float)o->m_positionY;
        if (node.x == rx && node.y == ry) continue;
        NodeEntry e;
        e.uid = o->m_uniqueID;
        std::memcpy(&e.x, &node.x, 4);
        std::memcpy(&e.y, &node.y, 4);
        v.push_back(e);
    }
    std::sort(v.begin(), v.end());
    return v;
}

inline uint64_t nodesHash(const std::vector<NodeEntry>& v) {
    uint64_t h = 1469598103934665603ULL;
    for (const NodeEntry& e : v) h = fnvAdd(h, &e, sizeof(e));
    return h;
}

struct CarrySig {
    BandCarry band;
    uint64_t latch = 0, area = 0, player = 0, nodes = 0;
    std::vector<NodeEntry> nodeSet;   // kept to say which nodes a refusal was about
    bool have = false;
};

inline int sigDiff(const CarrySig& a, const CarrySig& b) {
    if (!a.have || !b.have) return 31;
    return (a.band == b.band ? 0 : 1) | (a.latch == b.latch ? 0 : 2) | (a.area == b.area ? 0 : 4)
           | (a.player == b.player ? 0 : 8) | (a.nodes == b.nodes ? 0 : 16);
}

// The nodes two signatures differ in, for the refusal's line: "uid 254 (x1,y1 / x2,y2)" and so on.
inline std::string nodesDiff(const CarrySig& a, const CarrySig& b, size_t most = 4) {
    std::map<int, std::pair<const NodeEntry*, const NodeEntry*>> by;
    for (const NodeEntry& e : a.nodeSet) by[e.uid].first = &e;
    for (const NodeEntry& e : b.nodeSet) by[e.uid].second = &e;
    std::string out;
    size_t n = 0, shown = 0;
    auto pos = [](const NodeEntry* e) {
        if (!e) return std::string("at its place");
        float x, y;
        std::memcpy(&x, &e->x, 4);
        std::memcpy(&y, &e->y, 4);
        char b[64];
        snprintf(b, sizeof(b), "%.3f,%.3f", (double)x, (double)y);
        return std::string(b);
    };
    for (const auto& [uid, p] : by) {
        if (p.first && p.second && *p.first == *p.second) continue;
        ++n;
        if (shown++ < most) out += " uid " + std::to_string(uid) + " (" + pos(p.first) + " / " + pos(p.second) + ")";
    }
    return std::to_string(n) + " node(s):" + out;
}

inline CarrySig carrySig(GJBaseGameLayer* l) {
    CarrySig s;
    if (!l) return s;
    s.have = true;
    s.band = bandCarry(l);
    uint64_t h = 1469598103934665603ULL;
    if (l->m_objects) {
        uint32_t i = 0;
        for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
            if (o && o->m_shouldUseOuterOb) h = fnvAdd(h, &i, sizeof(i));
            ++i;
        }
    }
    s.latch = h;
    h = 1469598103934665603ULL;
    const int n = l->m_processedAreaObjectsCount;
    h = fnvAdd(h, &n, sizeof(n));
    for (int k = 0; k < n && (size_t)k < l->m_processedAreaObjects.size(); ++k) {
        GameObject* o = l->m_processedAreaObjects[k];
        const int uid = o ? o->m_uniqueID : -1;
        h = fnvAdd(h, &uid, sizeof(uid));
    }
    s.area = h;
    h = 1469598103934665603ULL;
    for (PlayerObject* p : {l->m_player1, l->m_player2}) {
        if (!p) continue;
        const int uid = p->m_objectSnappedTo ? p->m_objectSnappedTo->m_uniqueID : -1;
        const double sd = p->m_snapDistance;
        const uint8_t g2 = p->m_isOnGround2 ? 1 : 0;
        h = fnvAdd(h, &uid, sizeof(uid));
        h = fnvAdd(h, &sd, sizeof(sd));
        h = fnvAdd(h, &g2, sizeof(g2));
    }
    s.player = h;
    s.nodeSet = staleNodeSet(l, true);
    s.nodes = nodesHash(s.nodeSet);
    return s;
}

struct Set {
    int attempt = -1;
    long long serial = -1;                       // g_attemptSerial (anchors::g_provChain)
    std::vector<InputCmd> plan;
    std::vector<std::shared_ptr<Rec>> recs;      // tick order
    long long from = -1;                         // restored from a checkpoint at this tick (-1: head)
    // banked
    long long end = -1;
    float endX = 0.f;
    std::vector<AnchorRow> rows;
    anchors::Seeds seeds;
    std::vector<std::vector<solver::CoinPos>> coinPos;
    std::vector<long long> coinGd, coinPickup, routeOn;
    std::map<int, int> itemCounts;
    long long endTrigger = -1;
    std::shared_ptr<GtFile> gt;
    // what the attempt inherited from the one before it (CarrySig), as it stood before its reset
    CarrySig startSig;
};

inline std::shared_ptr<Set> g_live, g_dead, g_deepest;
inline long long g_next = -1;        // the tick the live attempt's next checkpoint is due at (-1: none)
// This attempt's start: -1 from the head, else the checkpoint tick it was restored at.
inline long long g_from = -1;
struct Choice {
    std::shared_ptr<Set> set;
    std::shared_ptr<Rec> rec;
    bool probe = false;
};
inline Choice g_pending;
inline const Choice* g_starting = nullptr;
// The next attempt flies from the head: a clear from a checkpoint is confirmed that way.
inline bool g_forceHead = false;
inline bool g_headPending = false;   // ...and the reset that starts it, at a frame boundary
// The probe (cfg cpflightprobe): queued at a death, flown while the solve runs, compared at its end.
inline Choice g_probeQueued;
inline bool g_probeFlying = false;
inline bool g_controlPending = false, g_controlStarting = false, g_controlFlying = false;
inline std::shared_ptr<Set> g_probeSet;
inline std::shared_ptr<Rec> g_probeRec;
constexpr long long kProbeMin = 60;      // the least a probe flies before the head flight's death
constexpr long long kProbeOverrun = 240; // a probe that outlives the head's death by this is ended
// Counts for the session's summary line.
inline long long g_taken = 0, g_starts = 0, g_ticksSkipped = 0, g_probes = 0, g_probesEqual = 0;
inline long long g_refused = 0;
inline double g_takeMs = 0.0, g_startMs = 0.0;
inline long long g_invalid = 0;
inline long long g_carryFallbacks = 0, g_carryPart[5] = {0, 0, 0, 0, 0}, g_probeSkips = 0;
inline void countCarryMiss(int parts) {
    ++g_carryFallbacks;
    for (int k = 0; k < 5; ++k)
        if (parts & (1 << k)) ++g_carryPart[k];
}
inline long long g_nodeDrift = 0, g_nodeChecks = 0, g_nodePremise = 0;
struct FlightCount { long long n = 0, ticks = 0; double ms = 0.0; };
inline FlightCount g_headFlights, g_cpFlights, g_probeFlights;
inline std::chrono::steady_clock::time_point g_attemptT0;
inline bool g_ended = true;   // this attempt's end is counted
inline int g_gtSerial = 0;

inline int g_objAfter = 0;   // update calls left to print after a restore

inline void objLine(GJBaseGameLayer* l, const char* when, long long t) {
    if (g_cfg.cpFlightObj < 0 || !l || !l->m_objects) return;
    GameObject* o = nullptr;
    for (auto* x : CCArrayExt<GameObject*>(l->m_objects))
        if (x && x->m_uniqueID == g_cfg.cpFlightObj) { o = x; break; }
    if (!o) { writeResult(std::string("cpflight: obj ") + when + " uid not found"); return; }
    const auto r = o->getObjectRect();
    const float* f = &o->m_positionXOffset;
    char b[760];
    snprintf(b, sizeof(b), "cpflight: obj %s t=%lld uid=%d pos=(%.3f,%.3f) real=(%.4f,%.4f) "
             "off=(%g,%g,%g,%g,%g,%g,%g,%g,%g,%g) stamp=%d 4c0=%d 4c4=%d 4cc=%d skip=%d dirty=%d "
             "rect=(%.3f,%.3f %.3fx%.3f) disabled=%d/%d circle=(r %.4f wh %.4f,%.4f scale %.4f,%.4f "
             "start %.4f,%.4f soff %.4f,%.4f node %.4f,%.4f)", when, t, g_cfg.cpFlightObj,
             o->getPositionX(), o->getPositionY(), o->m_positionX, o->m_positionY, f[0], f[1], f[2], f[3],
             f[4], f[5], f[6], f[7], f[8], f[9], o->m_unk4C8, o->m_unk4C0, o->m_unk4C4, o->m_unk4CC,
             (int)o->m_unk508, (int)o->m_isObjectRectDirty, r.origin.x, r.origin.y, r.size.width,
             r.size.height, (int)o->m_isGroupDisabled, (int)o->m_isGroupDisabledTemp,
             (double)o->m_objectRadius, (double)o->m_width, (double)o->m_height, (double)o->m_scaleX,
             (double)o->m_scaleY, (double)o->m_startScaleX, (double)o->m_startScaleY,
             (double)o->m_scaleXOffset, (double)o->m_scaleYOffset, (double)o->getScaleX(),
             (double)o->getScaleY());
    writeResult(b);
}

inline void layerDiff(const std::vector<uint8_t>& before, const void* now, long long t) {
    if (before.empty()) return;
    const auto* b = before.data();
    const auto* a = static_cast<const uint8_t*>(now);
    std::string s;
    int ranges = 0;
    size_t i = 0;
    const size_t n = before.size();
    while (i < n && ranges < 60) {
        if (a[i] == b[i]) { ++i; continue; }
        size_t j = i;
        while (j < n && (a[j] != b[j] || (j + 1 < n && a[j + 1] != b[j + 1]) || (j + 2 < n && a[j + 2] != b[j + 2])))
            ++j;
        const size_t lo = i & ~(size_t)3, hi = (j + 3) & ~(size_t)3;
        char r[96];
        float fb = 0, fa = 0;
        std::memcpy(&fb, b + lo, 4);
        std::memcpy(&fa, a + lo, 4);
        snprintf(r, sizeof(r), " 0x%zx+%zu(%g->%g)", lo, hi - lo, (double)fb, (double)fa);
        s += r;
        ++ranges;
        i = hi;
    }
    writeResult("cpflight: layerdiff t=" + std::to_string(t) + " ranges=" + std::to_string(ranges) + s);
}

inline std::string tweensLine(GJBaseGameLayer* l) {
    std::vector<std::pair<int, std::string>> v;
    for (const auto& kv : l->m_gameState.m_tweenActions) {
        const GJValueTween& t = kv.second;
        char b[200];
        snprintf(b, sizeof(b), " k%d(%.6g->%.6g dur=%.6g dt=%.6g cur=%.6g ease=%d/%.3g fin=%d dis=%d uid=%d ctl=%d)",
                 kv.first, (double)t.m_fromValue, (double)t.m_toValue, (double)t.m_duration,
                 (double)t.m_deltaTime, (double)t.m_currentValue, t.m_easingType, (double)t.m_easingRate,
                 (int)t.m_finished, (int)t.m_disabled, t.m_uniqueID, t.m_controlID);
        v.emplace_back(kv.first, b);
    }
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string s;
    for (const auto& e : v) s += e.second;
    return s;
}

inline bool g_inTake = false;
inline long long g_eventsDropped = 0;

constexpr int kDiffSteps = 4;
inline std::shared_ptr<struct Rec> g_diffCollect;   // the flight's checkpoint, collecting what follows it
inline std::shared_ptr<struct Rec> g_diffCompare;   // a restored flight's checkpoint, compared against
inline int g_diffStep = 0;

inline bool on() { return g_cfg.cpFlight > 0; }
inline bool probeMode() { return on() && g_cfg.cpFlightProbe; }

inline const void* g_watchLayer = nullptr;
inline std::vector<std::pair<GameObject*, cocos2d::CCPoint>> g_watch;
inline void nodeWatch(GJBaseGameLayer* l, long long tick, const char* when) {
    if (g_cfg.cpFlightNodeWatch.empty() || g_cfg.cpFlightNodeWatch == "-" || !l || !l->m_objects) return;
    if (g_watchLayer != l) {
        g_watchLayer = l;
        g_watch.clear();
        std::set<int> want;
        std::stringstream ss(g_cfg.cpFlightNodeWatch);
        for (std::string t; std::getline(ss, t, ',');)
            if (!t.empty()) want.insert(std::atoi(t.c_str()));
        for (auto* o : CCArrayExt<GameObject*>(l->m_objects))
            if (o && want.count(o->m_uniqueID)) g_watch.emplace_back(o, cocos2d::CCPoint(-1e9f, -1e9f));
    }
    for (auto& [o, last] : g_watch) {
        const cocos2d::CCPoint node = o->getPosition();
        if (!when && node.x == last.x && node.y == last.y) continue;
        last = node;
        char b[240];
        snprintf(b, sizeof(b), "cpflight: node uid=%d t=%lld %s node=%.4f,%.4f real=%.4f,%.4f start=%.4f,%.4f",
                 o->m_uniqueID, tick, when ? when : "moved", (double)node.x, (double)node.y, o->m_positionX,
                 o->m_positionY, (double)o->m_startPosition.x, (double)o->m_startPosition.y);
        writeResult(b);
    }
}

// Everything let go: the session starts, or the layer the checkpoints belong to is replaced.
inline void reset() {
    g_live.reset();
    g_dead.reset();
    g_deepest.reset();
    g_pending = Choice{};
    g_probeQueued = Choice{};
    g_probeSet.reset();
    g_probeRec.reset();
    g_probeFlying = false;
    g_controlPending = g_controlStarting = g_controlFlying = false;
    g_starting = nullptr;
    g_forceHead = false;
    g_headPending = false;
    g_next = -1;
    g_from = -1;
    g_watchLayer = nullptr;
    g_watch.clear();
}

struct Held { int sets = 0; size_t recs = 0; size_t bytes = 0; };
inline size_t recBytes(const Rec& r) {
    const AreaState& a = r.area;
    size_t n = r.player.capacity() + a.staleNodes.capacity() * sizeof(StaleNode)
               + a.objs.capacity() * sizeof(ObjOffsets) + a.variance.capacity() * sizeof(short)
               + a.latch.capacity() + a.activated.capacity() + a.activatedFlag.capacity()
               + (a.area.capacity() + a.processed.capacity() + a.activeObjects.capacity()) * sizeof(GameObject*)
               + r.followCells.capacity() * sizeof(Rec::FollowCell) + r.layerBytes.capacity()
               + r.gt.capacity() * sizeof(GtState)
               + r.envEntries.size() * sizeof(std::pair<GameObject* const, areaenv::Entry>)
               + r.envShapes.size() * sizeof(std::pair<GameObject* const, areaenv::Shape>);
    for (const auto& v : r.afterLayer) n += v.capacity();
    for (const auto& v : r.afterPlayer) n += v.capacity();
    return n;
}
inline Held held() {
    Held h;
    const Set* seen[3] = {nullptr, nullptr, nullptr};
    int k = 0;
    for (const std::shared_ptr<Set>* p : {&g_live, &g_dead, &g_deepest}) {
        const Set* s = p->get();
        if (!s || s == seen[0] || s == seen[1]) continue;
        seen[k++] = s;
        ++h.sets;
        h.recs += s->recs.size();
        h.bytes += s->rows.capacity() * sizeof(AnchorRow) + s->plan.capacity() * sizeof(InputCmd);
        for (const auto& r : s->recs) if (r) h.bytes += recBytes(*r);
    }
    return h;
}

inline void resetCounts() {
    g_taken = g_starts = g_ticksSkipped = g_probes = g_probesEqual = g_refused = g_invalid = 0;
    g_takeMs = g_startMs = 0.0;
    g_headFlights = g_cpFlights = g_probeFlights = FlightCount{};
    g_eventsDropped = 0;
    g_carryFallbacks = g_probeSkips = g_nodeDrift = g_nodeChecks = g_nodePremise = 0;
    for (auto& n : g_carryPart) n = 0;
}

// An attempt ended at `end` (the death or completion line). Counted once per attempt, by its kind.
inline void noteEnd(long long end) {
    if (g_ended) return;
    g_ended = true;
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_attemptT0).count();
    FlightCount& c = (g_probeFlying || g_controlFlying) ? g_probeFlights
                                                        : (g_from >= 0 ? g_cpFlights : g_headFlights);
    ++c.n;
    c.ticks += std::max<long long>(0, end - std::max<long long>(g_from, 0));
    c.ms += ms;
}

inline long long firstDifference(const std::vector<InputCmd>& a, const std::vector<InputCmd>& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i)
        if (a[i].step != b[i].step || a[i].down != b[i].down)
            return std::min<long long>(a[i].step, b[i].step);
    if (a.size() > n) return a[n].step;
    if (b.size() > n) return b[n].step;
    return LLONG_MAX;
}

inline size_t cursorAt(const std::vector<InputCmd>& plan, long long t) {
    size_t n = 0;
    while (n < plan.size() && (long long)plan[n].step < t) ++n;
    return n;
}

inline void onAttemptStart(int attempt, bool takes) {
    g_from = -1;
    g_next = -1;
    g_live.reset();
    g_attemptT0 = std::chrono::steady_clock::now();
    g_ended = !takes;   // the loop's own flights are counted (a probe's start sets it again)
    if (!on()) return;
    auto s = std::make_shared<Set>();
    s->attempt = attempt;
    s->serial = g_attemptSerial;
    s->plan = g_cfg.inputs;
    s->startSig = carrySig(PlayLayer::get());
    if (g_cfg.cpFlightCarryLog) {   // print only: the signature this attempt starts with
        const CarrySig& c = s->startSig;
        std::string uids;
        for (size_t k = 0; k < c.nodeSet.size() && k < 12; ++k) uids += " " + std::to_string(c.nodeSet[k].uid);
        char b[400];
        snprintf(b, sizeof(b), "cpflight: carry att=%d band=%08x,%08x latch=%016llx area=%016llx players=%016llx "
                 "nodes=%zu/%016llx:%s%s", attempt, c.band.centre, c.band.height, (unsigned long long)c.latch,
                 (unsigned long long)c.area, (unsigned long long)c.player, c.nodeSet.size(),
                 (unsigned long long)c.nodes, uids.c_str(), c.nodeSet.size() > 12 ? " ..." : "");
        writeResult(b);
    }
    if (g_starting && g_starting->set && g_starting->rec) {
        const long long T = g_starting->rec->tick;
        g_from = T;
        s->from = T;
        // ...and so is the world up to T (cfg recprovchain reads it in recProvArg)
        if (g_starting->set->serial >= 0)
            anchors::g_provChain[g_attemptSerial] = anchors::ProvLink{T, g_starting->set->serial};
        // ...and the checkpoints behind T are this attempt's too: its prefix IS that attempt's.
        for (const auto& r : g_starting->set->recs)
            if (r->tick <= T) s->recs.push_back(r);
    }
    if (takes && !g_cfg.inputs.empty()) {
        const long long S = g_cfg.cpFlight;
        const long long base = std::max<long long>(g_from, 0);
        g_next = (base / S + 1) * S;
    }
    g_live = std::move(s);
}

// The books at the checkpoint (the player-side capture is the layer's).
inline void captureBooks(Rec& r) {
    r.coinGd = solver::g_coinGdTick;
    r.coinPickup = solver::g_coinPickupTick;
    r.coinGdUnmatched = solver::g_coinGdUnmatched;
    r.coinMissFired = solver::g_coinMissFired;
    r.coinMissIdx = solver::g_coinMissIdx;
    r.endTrigger = solver::g_endTriggerFiredTick;
    r.coinPosLen.clear();
    for (const auto& lg : solver::g_coinPosLog) r.coinPosLen.push_back(lg.size());
    r.itemCounts = solver::g_itemCounts;
    r.coinLiveSaid = solver::g_coinLiveSaid;
    r.routeOn = route::g_onTick;
    r.routeSeenOff = route::g_seenOff;
    r.gameFrame = g_gameFrame;
    r.envEntries = areaenv::g_env;
    r.envShapes = areaenv::g_shape;
    r.gtBytes = -1;
    r.gt.clear();
    if (grouptrace::g_on && !grouptrace::g_objs.empty() && grouptrace::g_out.is_open()) {
        grouptrace::g_out.flush();
        r.gtBytes = (long long)grouptrace::g_out.tellp();
        r.gtRows = grouptrace::g_rows;
        r.gtLastTick = grouptrace::g_lastTick;
        r.gt.reserve(grouptrace::g_objs.size());
        for (const auto& t : grouptrace::g_objs)
            r.gt.push_back(GtState{t.cx, t.cy, t.w, t.h, t.rot, t.on, t.env, t.emitted});
    }
}

inline void add(std::shared_ptr<Rec> r) {
    if (!g_live) return;
    const long long S = g_cfg.cpFlight;
    g_next = (r->tick / S + 1) * S;
    g_live->recs.push_back(std::move(r));
    ++g_taken;
}

inline void bank(long long end, float endX, bool rolled) {
    if (!g_live || g_live->attempt != g_attempt) { g_live.reset(); return; }
    Set& s = *g_live;
    s.end = end;
    s.endX = endX;
    s.rows = anchors::g_dead;
    s.seeds = anchors::g_seedsDead;
    s.coinPos = solver::g_coinPosLog;
    s.coinGd = solver::g_coinGdTick;
    s.coinPickup = solver::g_coinPickupTick;
    s.routeOn = route::g_onTick;
    s.itemCounts = solver::g_itemCounts;
    s.endTrigger = solver::g_endTriggerFiredTick;
    if (rolled && !s.recs.empty()) {
        auto f = std::make_shared<GtFile>();
        f->path = std::string(DATA_DIR) + "/cpflight_gt_" + std::to_string(++g_gtSerial) + ".txt";
        std::error_code ec;
        std::filesystem::copy_file(std::string(DATA_DIR) + "/grouptrace_last.txt", f->path,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) s.gt = std::move(f);
        else f->path.clear();
    }
    g_dead = std::move(g_live);
}

inline void keepAsDeepest() { g_deepest = g_dead; }

inline void afterHeadReset(GJBaseGameLayer* l) {
    nodeWatch(l, 0, "reset");
    if (!on() || !g_live || !g_live->startSig.have || !l || !l->m_objects) return;
    ++g_nodeChecks;
    if (staleNodeSet(l, false) != g_live->startSig.nodeSet) ++g_nodeDrift;
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects))
        if (o && ((float)o->m_positionX != o->m_startPosition.x || (float)o->m_positionY != o->m_startPosition.y)) {
            ++g_nodePremise;
            break;
        }
}

inline bool usable(const Set& s, const Rec& r) {
    if (r.tick < 1 || r.player.empty() || !r.cp) return false;
    auto* pl = PlayLayer::get();
    if (!pl || r.layer != static_cast<const void*>(pl) || !pl->m_objects
        || r.objects != (size_t)pl->m_objects->count()) {
        ++g_invalid;
        return false;
    }
    if ((size_t)r.tick + 1 >= s.rows.size() || !s.rows[(size_t)r.tick].valid
        || !s.rows[(size_t)r.tick + 1].valid)
        return false;
    if (grouptrace::g_on && !grouptrace::g_objs.empty()) {
        if (!s.gt || r.gtBytes < 0 || r.gt.size() != grouptrace::g_objs.size()) return false;
    }
    return true;
}

inline Choice choose(const std::vector<InputCmd>& plan, const CarrySig& now, int* carryMiss = nullptr,
                     std::string* why = nullptr) {
    Choice best;
    int miss = 0;
    for (int k = 0; k < 2; ++k) {
        const std::shared_ptr<Set>& s = k == 0 ? g_dead : g_deepest;
        if (!s || s->recs.empty()) continue;
        if (k == 1 && s == g_dead) continue;   // the deepest is the one that just died
        const long long D = firstDifference(s->plan, plan);
        for (auto it = s->recs.rbegin(); it != s->recs.rend(); ++it) {
            const Rec& r = **it;
            if (r.tick >= D) continue;
            if (!usable(*s, r)) continue;
            if (const int d = sigDiff(s->startSig, now)) {
                miss |= d;
                if (why) {
                    *why += " attempt " + std::to_string(s->attempt) + " (checkpoint t=" + std::to_string(r.tick)
                            + "):" + (d & 1 ? " band" : "") + (d & 2 ? " latch" : "") + (d & 4 ? " area" : "")
                            + (d & 8 ? " players" : "") + (d & 16 ? " nodes, " + nodesDiff(s->startSig, now) : "");
                }
                break;
            }
            if (!best.rec || r.tick > best.rec->tick) { best.set = s; best.rec = *it; }
            break;
        }
    }
    if (carryMiss) *carryMiss = best.rec ? 0 : miss;
    return best;
}

inline bool requestStart(const std::vector<InputCmd>& plan) {
    if (!on() || g_cfg.cpFlightProbe) return false;
    if (g_forceHead) return false;
    int miss = 0;
    std::string why;
    Choice c = choose(plan, carrySig(PlayLayer::get()), &miss, &why);
    if (miss) {
        countCarryMiss(miss);
        writeResult("cpflight: from the head - what an attempt inherits now is not what the checkpoint's "
                    "attempt inherited:" + why);
    }
    if (!c.rec) return false;
    // A plan's cursor at T must be the checkpoint's: the same inputs before T, counted the same way.
    if (cursorAt(plan, c.rec->tick) != c.rec->nextInput) {
        ++g_refused;
        return false;
    }
    g_pending = std::move(c);
    return true;
}

inline void splice(const Choice& c) {
    const Set& s = *c.set;
    const Rec& r = *c.rec;
    const long long T = r.tick;
    anchors::g_live.assign(s.rows.begin(), s.rows.begin() + (T + 1));
    padseed::g_first.clear();
    for (const auto& kv : s.seeds.pads) if (kv.second <= T) padseed::g_first.insert(kv);
    ringseed::g_fired.clear();
    for (const auto& e : s.seeds.rings) if (e.second <= T) ringseed::g_fired.push_back(e);
    ringseed::g_fired2.clear();
    for (const auto& e : s.seeds.rings2) if (e.second <= T) ringseed::g_fired2.push_back(e);
    touchseed::g_first.clear();
    for (const auto& kv : s.seeds.touch) if (kv.second <= T) touchseed::g_first.insert(kv);
    portalseed::g_first.clear();
    for (const auto& kv : s.seeds.portal) if (kv.second <= T) portalseed::g_first.insert(kv);
    portalseed::g_first2.clear();
    for (const auto& kv : s.seeds.portal2) if (kv.second <= T) portalseed::g_first2.insert(kv);
    touchfire::g_first.clear();
    for (const auto& kv : s.seeds.touchFire) if (kv.second <= T) touchfire::g_first.insert(kv);
    solver::g_coinGdTick = r.coinGd;
    solver::g_coinPickupTick = r.coinPickup;
    solver::g_coinGdUnmatched = r.coinGdUnmatched;
    solver::g_coinMissFired = r.coinMissFired;
    solver::g_coinMissIdx = r.coinMissIdx;
    solver::g_endTriggerFiredTick = r.endTrigger;
    for (size_t i = 0; i < solver::g_coinPosLog.size(); ++i) {
        auto& lg = solver::g_coinPosLog[i];
        lg.clear();
        if (i < s.coinPos.size() && i < r.coinPosLen.size())
            lg.assign(s.coinPos[i].begin(),
                      s.coinPos[i].begin() + (long long)std::min(r.coinPosLen[i], s.coinPos[i].size()));
    }
    solver::g_itemCounts = r.itemCounts;
    solver::g_coinLiveSaid = r.coinLiveSaid;
    route::g_onTick = r.routeOn;
    route::g_seenOff = r.routeSeenOff;
    g_gameFrame = r.gameFrame;
    areaenv::g_env = r.envEntries;
    areaenv::g_shape = r.envShapes;
    itermap::g_xHist.assign((size_t)T + 1, -1.f);
    for (long long t = 0; t <= T; ++t)
        if (s.rows[(size_t)t].valid) itermap::g_xHist[(size_t)t] = s.rows[(size_t)t].x;
    // The recording: that attempt's file up to T, and the tracked objects' state as it had them.
    if (grouptrace::g_on && !grouptrace::g_objs.empty() && s.gt && r.gtBytes >= 0) {
        const std::string cur = std::string(DATA_DIR) + "/grouptrace.txt";
        if (grouptrace::g_out.is_open()) grouptrace::g_out.close();
        {
            std::ifstream src(s.gt->path, std::ios::binary);
            std::ofstream dst(cur, std::ios::binary | std::ios::trunc);
            std::vector<char> buf(1 << 16);
            long long left = r.gtBytes;
            while (left > 0 && src) {
                const std::streamsize n = (std::streamsize)std::min<long long>(left, (long long)buf.size());
                src.read(buf.data(), n);
                const std::streamsize got = src.gcount();
                if (got <= 0) break;
                dst.write(buf.data(), got);
                left -= got;
            }
        }
        // text mode, as build() and roll() open it, positioned at the end
        grouptrace::g_out.open(cur, std::ios::in | std::ios::out);
        grouptrace::g_out.seekp(0, std::ios::end);
        grouptrace::g_rows = r.gtRows;
        grouptrace::g_lastTick = r.gtLastTick;
        for (size_t i = 0; i < grouptrace::g_objs.size() && i < r.gt.size(); ++i) {
            auto& t = grouptrace::g_objs[i];
            const GtState& g = r.gt[i];
            t.cx = g.cx; t.cy = g.cy; t.w = g.w; t.h = g.h; t.rot = g.rot;
            t.on = g.on; t.env = g.env; t.emitted = g.emitted;
        }
    }
}

inline void queueProbe() {
    if (!probeMode() || !g_dead || g_dead->end < 0) return;
    if (sigDiff(g_dead->startSig, carrySig(PlayLayer::get())) != 0) {
        ++g_probeSkips;
        return;
    }
    long long upTo = g_dead->end - kProbeMin;
    if (g_cfg.cpFlightProbeAt >= 0) upTo = std::min<long long>(upTo, g_cfg.cpFlightProbeAt);
    for (auto it = g_dead->recs.rbegin(); it != g_dead->recs.rend(); ++it) {
        if ((*it)->tick > upTo) continue;
        if (!usable(*g_dead, **it)) continue;
        g_probeQueued = Choice{g_dead, *it, true};
        return;
    }
}

inline long long clockAge(double now, double stamp, int cap) {
    const double since = now - stamp;
    return since >= 0.0 ? std::min<long long>(std::llround(since * 240.0), cap) : -1;
}
inline const char* rowDiff(const AnchorRow& a, const AnchorRow& b, std::map<std::string, long long>& count,
                           const double* clock0a = nullptr, const double* clock0b = nullptr) {
    const char* first = nullptr;
#define CPF_F(f) if (std::memcmp(&a.f, &b.f, sizeof(a.f)) != 0) { ++count[#f]; if (!first) first = #f; }
#define CPF_V(name, va, vb) if ((va) != (vb)) { ++count[name]; if (!first) first = name; }
    CPF_F(valid) CPF_F(x) CPF_F(y) CPF_F(vy) CPF_F(mode) CPF_F(onGround) CPF_F(onGround2) CPF_F(flip)
    CPF_F(mini) CPF_F(dual) CPF_F(y2) CPF_F(v2) CPF_F(f2) CPF_F(g2) CPF_F(g2b) CPF_F(m2) CPF_F(mini2)
    CPF_F(dashing) CPF_F(dashSlope) CPF_F(dashing2) CPF_F(dashRing2) CPF_F(rot) CPF_F(speed)
    CPF_F(gframe) CPF_F(snapUid)
    CPF_F(snapDist) CPF_F(pmin) CPF_F(pmax) CPF_F(freeMode) CPF_F(boost) CPF_F(ctrlOff) CPF_F(ctrlOff2) CPF_F(b985)
    CPF_F(b986) CPF_F(b985_2) CPF_F(b986_2) CPF_F(coins) CPF_F(item1) CPF_F(cnt1) CPF_F(item2)
    CPF_F(cnt2) CPF_F(onSlope) CPF_F(slopeUnder) CPF_F(slopeUid)
    if (clock0a && clock0b) {
        CPF_V("clockTicks", std::llround((a.totalTime - *clock0a) * 240.0),
              std::llround((b.totalTime - *clock0b) * 240.0))
        CPF_V("spiderAge", clockAge(a.totalTime, a.spiderStamp, 255), clockAge(b.totalTime, b.spiderStamp, 255))
        CPF_V("slopeAge", a.onSlope ? std::max<long long>(clockAge(a.totalTime, a.slopeStart, 255), 0) : -1,
              b.onSlope ? std::max<long long>(clockAge(b.totalTime, b.slopeStart, 255), 0) : -1)
    } else {
        CPF_F(slopeStart) CPF_F(totalTime) CPF_F(spiderStamp)
    }
#undef CPF_V
#undef CPF_F
    return first;
}

inline bool sameCoinPos(const std::vector<std::vector<solver::CoinPos>>& a,
                        const std::vector<std::vector<solver::CoinPos>>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].size() != b[i].size()) return false;
        for (size_t k = 0; k < a[i].size(); ++k)
            if (a[i][k].t != b[i][k].t || a[i][k].x != b[i][k].x || a[i][k].y != b[i][k].y
                || a[i][k].on != b[i][k].on)
                return false;
    }
    return true;
}

struct CarryFull {
    BandCarry band;
    std::vector<uint32_t> latch;   // indexes into m_objects
    std::vector<int> area;         // m_processedAreaObjects' uids, count first
    std::vector<NodeEntry> nodes;
    struct Snap {
        int uid = -1;
        uint64_t sd = 0;
        uint8_t g2 = 0;
        bool operator==(const Snap& o) const { return uid == o.uid && sd == o.sd && g2 == o.g2; }
    } snap[2];
    long long tick = -1;
    bool have = false;
};

inline CarryFull captureCarryFull(GJBaseGameLayer* l, long long tick) {
    CarryFull c;
    if (!l || !l->m_objects) return c;
    c.have = true;
    c.tick = tick;
    c.band = bandCarry(l);
    uint32_t i = 0;
    for (auto* o : CCArrayExt<GameObject*>(l->m_objects)) {
        if (o && o->m_shouldUseOuterOb) c.latch.push_back(i);
        ++i;
    }
    c.area.push_back(l->m_processedAreaObjectsCount);
    for (int k = 0; k < l->m_processedAreaObjectsCount && (size_t)k < l->m_processedAreaObjects.size(); ++k)
        c.area.push_back(l->m_processedAreaObjects[k] ? l->m_processedAreaObjects[k]->m_uniqueID : -1);
    c.nodes = staleNodeSet(l, false);
    PlayerObject* ps[2] = {l->m_player1, l->m_player2};
    for (int k = 0; k < 2; ++k) {
        if (!ps[k]) continue;
        c.snap[k].uid = ps[k]->m_objectSnappedTo ? ps[k]->m_objectSnappedTo->m_uniqueID : -1;
        std::memcpy(&c.snap[k].sd, &ps[k]->m_snapDistance, 8);
        c.snap[k].g2 = ps[k]->m_isOnGround2 ? 1 : 0;
    }
    return c;
}

// "band same | latch same (n) | ..." and whether every part is the same.
inline std::string compareCarryFull(const CarryFull& a, const CarryFull& b, bool* same) {
    if (!a.have || !b.have) {
        *same = false;
        return std::string("not taken (") + (a.have ? "" : "probe ") + (b.have ? "" : "control ") + ")";
    }
    CarrySig sa, sb;
    sa.nodeSet = a.nodes;
    sb.nodeSet = b.nodes;
    const bool band = a.band == b.band, latch = a.latch == b.latch, area = a.area == b.area,
               nodes = a.nodes == b.nodes, snap = a.snap[0] == b.snap[0] && a.snap[1] == b.snap[1],
               tick = a.tick == b.tick;
    *same = band && latch && area && nodes && snap && tick;
    char buf[200];
    snprintf(buf, sizeof(buf), "t=%lld/%lld band %s, latch %s (%zu), area %s (%d), snap %s (uid %d), stale nodes ",
             a.tick, b.tick, band ? "same" : "DIFFER", latch ? "same" : "DIFFER", a.latch.size(),
             area ? "same" : "DIFFER", a.area.empty() ? 0 : a.area[0], snap ? "same" : "DIFFER", a.snap[0].uid);
    return buf + (nodes ? "same (" + std::to_string(a.nodes.size()) + ")" : "DIFFER, " + nodesDiff(sa, sb));
}
inline CarryFull g_probeAtT, g_controlAtT;

// What the probe ended with, kept for the control.
struct ProbeCap {
    int attempt = -1, sourceAttempt = -1;
    long long from = -1, end = -1;
    float endX = 0.f;
    std::string how;
    std::vector<AnchorRow> rows;
    anchors::Seeds seeds;
    std::vector<long long> coinGd, coinPickup, routeOn;
    std::vector<std::vector<solver::CoinPos>> coinPos;
    std::map<int, int> itemCounts;
    long long endTrigger = -1;
    std::string gt;   // the recording, grouptrace.txt as the probe left it (never rolled)
    bool haveGt = false;
    CarrySig sourceSig;    // what the checkpoint's attempt inherited
    int controlCarry = 0;  // sigDiff of it against what the control inherits (0 = the same)
    CarryFull atT;         // the carry state right after the probe's restore
};
inline ProbeCap g_probeCap;

// The control's attempt begins (hooks_playlayer, right after onAttemptStart): counted as a probe flight.
inline void startControl() {
    g_controlFlying = true;
    g_ended = false;
}

inline std::string readGt() {
    grouptrace::g_out.flush();
    std::ifstream f(std::string(DATA_DIR) + "/grouptrace.txt", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

inline std::string probeCapture(long long end, float endX, const char* how) {
    g_probeFlying = false;
    ProbeCap& p = g_probeCap;
    p = ProbeCap{};
    p.attempt = g_attempt;
    p.sourceAttempt = g_probeSet ? g_probeSet->attempt : -1;
    if (g_probeSet) p.sourceSig = g_probeSet->startSig;
    p.atT = std::move(g_probeAtT);
    g_probeAtT = CarryFull{};
    g_controlAtT = CarryFull{};
    p.from = g_probeRec ? g_probeRec->tick : -1;
    p.end = end;
    p.endX = endX;
    p.how = how;
    p.rows = anchors::g_live;
    p.seeds = anchors::Seeds{padseed::g_first, ringseed::g_fired, touchseed::g_first, portalseed::g_first,
                             portalseed::g_first2, touchfire::g_first, ringseed::g_fired2};
    p.coinGd = solver::g_coinGdTick;
    p.coinPickup = solver::g_coinPickupTick;
    p.routeOn = route::g_onTick;
    p.coinPos = solver::g_coinPosLog;
    p.itemCounts = solver::g_itemCounts;
    p.endTrigger = solver::g_endTriggerFiredTick;
    if (grouptrace::g_on && !grouptrace::g_objs.empty()) {
        p.gt = readGt();
        p.haveGt = true;
    }
    g_probeSet.reset();
    g_probeRec.reset();
    g_controlPending = true;
    char b[240];
    snprintf(b, sizeof(b), "cpflight: probe attempt %d from the checkpoint at t=%lld of attempt %d %s at "
             "t=%lld x=%.3f - its control, the same plan from the head, flies next",
             p.attempt, p.from, p.sourceAttempt, how, end, (double)endX);
    return b;
}

// The control ended: the probe against it. Returns the verdict line.
inline std::string controlEnd(long long end, float endX, const char* how) {
    g_controlFlying = false;
    ++g_probes;
    const ProbeCap& p = g_probeCap;
    const long long last = std::min(end, p.end);
    long long n = 0, same = 0, firstBad = -1;
    const char* badField = nullptr;
    std::map<std::string, long long> fieldCount;
    // each attempt's clock at its first row (see rowDiff)
    double c0p = 0.0, c0c = 0.0;
    bool haveC0p = false, haveC0c = false;
    for (long long t = 1; t <= last && !(haveC0p && haveC0c); ++t) {
        if (!haveC0p && (size_t)t < p.rows.size() && p.rows[(size_t)t].valid) {
            c0p = p.rows[(size_t)t].totalTime;
            haveC0p = true;
        }
        if (!haveC0c && (size_t)t < anchors::g_live.size() && anchors::g_live[(size_t)t].valid) {
            c0c = anchors::g_live[(size_t)t].totalTime;
            haveC0c = true;
        }
    }
    for (long long t = 1; t <= last; ++t) {
        const bool ha = (size_t)t < p.rows.size() && p.rows[(size_t)t].valid;
        const bool hb = (size_t)t < anchors::g_live.size() && anchors::g_live[(size_t)t].valid;
        if (!ha || !hb) {
            if (ha != hb && firstBad < 0) { firstBad = t; badField = "missing row"; }
            continue;
        }
        ++n;
        const char* f = rowDiff(p.rows[(size_t)t], anchors::g_live[(size_t)t], fieldCount, &c0p, &c0c);
        if (!f) ++same;
        else if (firstBad < 0) { firstBad = t; badField = f; }
    }
    std::string fields;
    for (const auto& kv : fieldCount) fields += " " + kv.first + "=" + std::to_string(kv.second);
    const bool deathSame = end == p.end && endX == p.endX;
    const bool seedsSame = padseed::g_first == p.seeds.pads && ringseed::g_fired == p.seeds.rings
                           && touchseed::g_first == p.seeds.touch && portalseed::g_first == p.seeds.portal
                           && portalseed::g_first2 == p.seeds.portal2
                           && touchfire::g_first == p.seeds.touchFire;
    const bool coinsSame = solver::g_coinGdTick == p.coinGd && solver::g_coinPickupTick == p.coinPickup
                           && sameCoinPos(solver::g_coinPosLog, p.coinPos)
                           && solver::g_itemCounts == p.itemCounts && route::g_onTick == p.routeOn
                           && solver::g_endTriggerFiredTick == p.endTrigger;
    std::string gt = "none recorded";
    bool gtSame = true;
    if (p.haveGt) {
        const std::string c = readGt();
        size_t i = 0;
        const size_t m = std::min(c.size(), p.gt.size());
        while (i < m && c[i] == p.gt[i]) ++i;
        gtSame = c.size() == p.gt.size() && i == m;
        char gb[200];
        if (gtSame) {
            snprintf(gb, sizeof(gb), "same (%zu bytes)", c.size());
        } else {
            size_t line = 1;
            for (size_t k = 0; k < i; ++k) line += c[k] == '\n';
            snprintf(gb, sizeof(gb), "DIFFER at byte %zu line %zu (probe %zu bytes, control %zu)", i, line,
                     p.gt.size(), c.size());
            std::error_code ec;
            const std::string stem = std::string(DATA_DIR) + "/cpflight_probe" + std::to_string(p.attempt);
            std::ofstream(stem + "_gt.txt", std::ios::binary) << p.gt;
            std::ofstream(stem + "_control_gt.txt", std::ios::binary) << c;
        }
        gt = gb;
    }
    bool carrySame = false;
    const std::string carry = compareCarryFull(p.atT, g_controlAtT, &carrySame);
    g_controlAtT = CarryFull{};
    const bool all = n > 0 && same == n && firstBad < 0 && deathSame && seedsSame && coinsSame && gtSame
                     && carrySame;
    std::string verdict = all ? "EQUAL" : "MISMATCH";
    if (p.controlCarry != 0) {
        --g_probes;
        ++g_probeSkips;
        verdict = "NOT COMPARED - the control inherits differently (parts " + std::to_string(p.controlCarry)
                  + ")";
    } else if (all) {
        ++g_probesEqual;
    }
    char b[1600];
    snprintf(b, sizeof(b),
             "cpflight: probe att=%d (control att=%d; checkpoint of att %d at t=%lld): the probe %s at t=%lld "
             "x=%.3f, the control %s at t=%lld x=%.3f | rows t=1..%lld equal on all fields %lld/%lld%s%s%s%s%s "
             "(the player clocks %+.4f s apart: compared as ages) | seeds %s | coins %s | groups %s "
             "| carry at the checkpoint (restore / control): %s | %s",
             p.attempt, g_attempt, p.sourceAttempt, p.from, p.how.c_str(), p.end, (double)p.endX, how, end,
             (double)endX, last, same, n, firstBad >= 0 ? " (first difference t=" : "",
             firstBad >= 0 ? (std::to_string(firstBad) + " " + badField).c_str() : "",
             firstBad >= 0 ? ";" : "", fields.c_str(), firstBad >= 0 ? " ticks)" : "", c0c - c0p,
             seedsSame ? "same" : "DIFFER", coinsSame ? "same" : "DIFFER", gt.c_str(), carry.c_str(),
             verdict.c_str());
    g_probeCap = ProbeCap{};
    return b;
}

inline std::string summary() {
    char b[1000];
    snprintf(b, sizeof(b),
             "cpflight: %s%s | head flights %lld, %lld ticks, %.1f s | from a checkpoint %lld, %lld ticks, %.1f s "
             "| %lld ticks not flown again | checkpoints %lld (%.1f ms, %.2f ms each), restores %lld (%.1f ms) "
             "| fallbacks: %lld at the cursor, %lld invalid, %lld on the carry (band %lld, latch %lld, "
             "area %lld, players %lld) | game events of our checkpoints dropped %lld "
             "| probes %lld/%lld equal, %lld skipped on the carry (%lld ticks, %.1f s with their controls)",
             on() ? ("every " + std::to_string(g_cfg.cpFlight) + " ticks").c_str() : "off",
             g_cfg.cpFlightProbe ? " (probe)" : "", g_headFlights.n, g_headFlights.ticks,
             g_headFlights.ms / 1000.0, g_cpFlights.n, g_cpFlights.ticks, g_cpFlights.ms / 1000.0,
             g_ticksSkipped, g_taken, g_takeMs, g_taken ? g_takeMs / (double)g_taken : 0.0, g_starts,
             g_startMs, g_refused, g_invalid, g_carryFallbacks, g_carryPart[0], g_carryPart[1],
             g_carryPart[2], g_carryPart[3], g_eventsDropped, g_probesEqual, g_probes, g_probeSkips,
             g_probeFlights.ticks, g_probeFlights.ms / 1000.0);
    if (on()) {
        char e[300];
        snprintf(e, sizeof(e), " | stale nodes: %lld fallbacks on them; head resets whose stale nodes were not the "
                 "predicted ones %lld of %lld, that left a real position off its start %lld",
                 g_carryPart[4], g_nodeDrift, g_nodeChecks, g_nodePremise);
        return std::string(b) + e;
    }
    return b;
}
}  // namespace cpflight
}  // namespace p1
