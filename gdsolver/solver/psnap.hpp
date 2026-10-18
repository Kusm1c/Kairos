#pragma once

namespace psnap {
struct Mem { const char* name; size_t off; size_t size; };

inline const std::vector<Mem>& scalars() {
    static std::vector<Mem> tbl = [] {
        std::vector<Mem> m;
#define PO_SCALAR(n) m.push_back({#n, offsetof(PlayerObject, n), sizeof(PlayerObject::n)});
#define GO_SCALAR(n) m.push_back({#n, offsetof(GameObject, n), sizeof(GameObject::n)});
#define PO_OPAQUE(n)
#define GO_OPAQUE(n)
#define GB_SCALAR(n)
#define GB_OPAQUE(n)
#define EM_SCALAR(n)
#define EM_OPAQUE(n)
#include "../po_members.inc"
#undef PO_SCALAR
#undef GO_SCALAR
#undef PO_OPAQUE
#undef GO_OPAQUE
#undef GB_SCALAR
#undef GB_OPAQUE
#undef EM_SCALAR
#undef EM_OPAQUE
        std::sort(m.begin(), m.end(),
                  [](const Mem& a, const Mem& b) { return a.off < b.off; });
        return m;
    }();
    return tbl;
}

inline const char* const PRESERVE[] = {
    "m_dashFireSprite", "m_particleSystems", "m_ghostTrail",
    "m_iconSprite", "m_iconSpriteSecondary", "m_iconSpriteWhitener",
    "m_vehicleSprite", "m_vehicleSpriteSecondary", "m_vehicleSpriteWhitener",
    "m_dashSpritesContainer", "m_regularTrail", "m_waveTrail",
    "m_robotSprite", "m_spiderSprite", "m_maybeSpriteRelated",
    "m_playerGroundParticles", "m_trailingParticles", "m_shipClickParticles",
    "m_vehicleGroundParticles", "m_ufoClickParticles", "m_robotBurstParticles",
    "m_dashParticles", "m_swingBurstParticles1", "m_swingBurstParticles2",
    "m_landParticles0", "m_landParticles1",
    "m_touchedRings", "m_ringRelatedSet", "m_touchingRings",
    "m_rotateObjectsRelated", "m_potentialSlopeMap",
    "m_playerFollowFloats", "m_jumpPadRelated", "m_holdingButtons",
    "m_currentRobotAnimation",
    "m_collisionLogTop", "m_collisionLogBottom",
    "m_collisionLogLeft", "m_collisionLogRight",
    "m_unk958",
};

inline size_t scalarCount() { return scalars().size(); }

inline bool g_keepSnapPos = false;
inline size_t g_maskLo = 0;
inline size_t g_maskHi = (size_t)-1;
inline size_t g_maskExLo = 0;
inline size_t g_maskExHi = 0;
// cfg `secskipextras=1`: skip writing back CCNode position/rotation and m_position
inline bool g_skipExtras = false;

inline bool looksPointer(uint64_t v) {
    // Win64 user-mode space. 8-byte aligned. Excludes 0 and small integers.
    return v >= 0x10000ull && v < 0x7FFFFFFFFFFFull && (v & 7) == 0;
}

inline std::vector<uint8_t>& mask() {
    static std::vector<uint8_t> m = [] {
        std::vector<uint8_t> v(sizeof(PlayerObject), 0);
        for (auto& mem : scalars()) {
            if (mem.off < 0x20) continue;                   // CCObject header
            if (mem.off + mem.size > sizeof(PlayerObject)) continue;
            std::fill(v.begin() + (ptrdiff_t)mem.off,
                      v.begin() + (ptrdiff_t)(mem.off + mem.size), (uint8_t)1);
        }
        return v;
    }();
    return m;
}

inline int g_shrinks = 0;
inline void observe(PlayerObject* p) {
    auto& m = mask();
    for (size_t o = 0x20; o + 8 <= m.size(); o += 8) {
        bool all = true;
        for (size_t j = 0; j < 8; ++j) if (!m[o + j]) { all = false; break; }
        if (!all) continue;
        uint64_t v;
        std::memcpy(&v, (const uint8_t*)p + o, 8);
        if (looksPointer(v)) ++g_shrinks;
    }
}

inline const std::vector<Mem>& goScalars() {
    static std::vector<Mem> tbl = [] {
        std::vector<Mem> m;
#define PO_SCALAR(n)
#define PO_OPAQUE(n)
#define GO_SCALAR(n) m.push_back({#n, offsetof(GameObject, n), sizeof(GameObject::n)});
#define GO_OPAQUE(n)
#define GB_SCALAR(n)
#define GB_OPAQUE(n)
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
        return m;
    }();
    return tbl;
}

inline const std::vector<Mem>& gbScalars() {
    static std::vector<Mem> tbl = [] {
        std::vector<Mem> m;
#define PO_SCALAR(n)
#define PO_OPAQUE(n)
#define GO_SCALAR(n)
#define GO_OPAQUE(n)
#define GB_SCALAR(n) m.push_back({#n, offsetof(GJBaseGameLayer, n), sizeof(GJBaseGameLayer::n)});
#define GB_OPAQUE(n)
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
        return m;
    }();
    return tbl;
}

inline uint64_t objSig(GameObject* o) {
    uint64_t h = 1469598103934665603ull;                    // FNV-1a
    auto mix = [&h](const void* p, size_t n) {
        const uint8_t* b = (const uint8_t*)p;
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    const float pos[3] = {o->getPositionX(), o->getPositionY(),
                          o->getRotation()};
    mix(pos, sizeof(pos));
    for (auto& m : goScalars()) mix((const uint8_t*)o + m.off, m.size);
    return h;
}

inline size_t maskedBytes() {
    size_t n = 0;
    for (uint8_t b : mask()) n += b;
    return n;
}

struct ObjXf { float x, y, rotX, rotY, scaleX, scaleY; uint8_t visible; };

inline bool isSectionIndex(const char* n) {
    return std::strcmp(n, "m_someOtherIndex") == 0
        || std::strcmp(n, "m_innerSectionIndex") == 0
        || std::strcmp(n, "m_outerSectionIndex") == 0
        || std::strcmp(n, "m_middleSectionIndex") == 0;
}

inline const std::vector<Mem>& goWorldList() {
    static std::vector<Mem> v = [] {
        std::vector<Mem> r;
        for (auto& m : goScalars()) if (!isSectionIndex(m.name)) r.push_back(m);
        return r;
    }();
    return v;
}

inline size_t goStride() {
    static size_t n = [] {
        size_t s = sizeof(ObjXf);
        for (auto& m : goWorldList()) s += m.size;
        return s;
    }();
    return n;
}

inline void captureWorld(const std::vector<GameObject*>& set,
                         std::vector<uint8_t>& out) {
    out.resize(set.size() * goStride());
    uint8_t* d = out.data();
    for (auto* o : set) {
        const ObjXf xf{o->getPositionX(), o->getPositionY(),
                       o->getRotationX(), o->getRotationY(),
                       o->getScaleX(), o->getScaleY(),
                       (uint8_t)(o->isVisible() ? 1 : 0)};
        std::memcpy(d, &xf, sizeof(xf)); d += sizeof(xf);
        for (auto& m : goWorldList()) {
            std::memcpy(d, (const uint8_t*)o + m.off, m.size); d += m.size;
        }
    }
}

inline void restoreWorld(GJBaseGameLayer* l,
                         const std::vector<GameObject*>& set,
                         const std::vector<uint8_t>& in) {
    if (in.size() != set.size() * goStride()) return;
    const uint8_t* s = in.data();
    for (auto* o : set) {
        ObjXf xf;
        std::memcpy(&xf, s, sizeof(xf)); s += sizeof(xf);
        o->setPosition({xf.x, xf.y});
        o->setRotationX(xf.rotX);
        o->setRotationY(xf.rotY);
        o->setScaleX(xf.scaleX);
        o->setScaleY(xf.scaleY);
        o->setVisible(xf.visible != 0);
        for (auto& m : goWorldList()) {
            std::memcpy((uint8_t*)o + m.off, s, m.size); s += m.size;
        }
        // Re-bucket. Restoring the position alone does NOT put it into collision.
        if (l) l->updateObjectSection(o);
    }
}

constexpr size_t kExtra = 3 * sizeof(float);   // x, y, rotation

inline const char* const LAYER_RESTORE[] = {
    "m_blending", "m_areaObjectsUpdated", "m_extraDelta", "m_audioPaused",
};

inline int g_layerInput = 0;
inline const char* const LAYER_INPUT[] = {
    "m_currentStep", "m_clicks", "m_queuedRecordedButtonsSize",
};

inline const std::vector<Mem>& layerRestoreList() {
    static std::vector<Mem> lists[8];
    static bool built[8] = {};
    const int bits = g_layerInput & 7;
    if (!built[bits]) {
        std::vector<Mem> r;
        for (auto& m : gbScalars()) {
            bool take = false;
            for (auto* n : LAYER_RESTORE)
                if (std::strcmp(m.name, n) == 0) { take = true; break; }
            for (int k = 0; k < 3 && !take; ++k)
                if ((bits & (1 << k)) && std::strcmp(m.name, LAYER_INPUT[k]) == 0) take = true;
            if (take) r.push_back(m);
        }
        lists[bits] = std::move(r);
        built[bits] = true;
    }
    return lists[bits];
}

inline size_t layerBytes() {
    size_t n = 0;
    for (auto& m : layerRestoreList()) n += m.size;
    return n;
}

struct Touch {
    gd::unordered_set<int> touchedRings;
    gd::unordered_set<int> ringRelatedSet;
    gd::map<int, bool> jumpPadRelated;
    gd::unordered_map<int, GameObject*> potentialSlopeMap;
    GameObject* objectSnappedTo = nullptr;
    GameObject* collidedObject = nullptr;
    std::vector<cocos2d::CCObject*> touchingRings;
    bool haveTouching = false;
    // Player 2's, when the snapshot had one (see partner below).
    std::shared_ptr<Touch> second;
};

// cfg `secsnapobj=1`: also carry the two pointers above
inline bool g_snapCollideObj = false;
inline bool g_snapRing = false;

inline PlayerObject* partner(PlayerObject* p, GJBaseGameLayer* l) {
    return (l && p && p == l->m_player1 && l->m_player2 && l->m_player2 != p) ? l->m_player2
                                                                            : nullptr;
}

inline void captureTouchOne(PlayerObject* p, Touch& out) {
    out.touchedRings = p->m_touchedRings;
    out.ringRelatedSet = p->m_ringRelatedSet;
    out.jumpPadRelated = p->m_jumpPadRelated;
    out.potentialSlopeMap = p->m_potentialSlopeMap;
    out.objectSnappedTo = p->m_objectSnappedTo;
    out.collidedObject = p->m_collidedObject;
    out.touchingRings.clear();
    out.haveTouching = g_snapRing && p->m_touchingRings;
    if (out.haveTouching)
        for (unsigned i = 0; i < p->m_touchingRings->count(); ++i)
            out.touchingRings.push_back(p->m_touchingRings->objectAtIndex(i));
}

inline void restoreTouchOne(PlayerObject* p, const Touch& in) {
    p->m_touchedRings = in.touchedRings;
    p->m_ringRelatedSet = in.ringRelatedSet;
    p->m_jumpPadRelated = in.jumpPadRelated;
    p->m_potentialSlopeMap = in.potentialSlopeMap;
    if (g_snapCollideObj) {
        p->m_objectSnappedTo = in.objectSnappedTo;
        p->m_collidedObject = in.collidedObject;
    }
    if (g_snapRing && in.haveTouching && p->m_touchingRings) {
        p->m_touchingRings->removeAllObjects();
        for (auto* o : in.touchingRings) p->m_touchingRings->addObject(o);
    }
}

inline void captureTouch(PlayerObject* p, Touch& out, GJBaseGameLayer* l = nullptr) {
    if (!p) return;
    captureTouchOne(p, out);
    out.second.reset();
    if (auto* p2 = partner(p, l)) {
        out.second = std::make_shared<Touch>();
        captureTouchOne(p2, *out.second);
    }
}

inline void restoreTouch(PlayerObject* p, const Touch& in, GJBaseGameLayer* l = nullptr) {
    if (!p) return;
    restoreTouchOne(p, in);
    if (auto* p2 = partner(p, l); p2 && in.second) restoreTouchOne(p2, *in.second);
}

#ifdef GEODE_IS_WINDOWS
template <class M>
inline void syncOrderedMap(M& dst, const M& src) {
    auto d = dst.begin();
    auto s = src.begin();
    const auto less = dst.key_comp();
    while (s != src.end()) {
        if (d == dst.end() || less(s->first, d->first)) {
            d = std::next(dst.emplace_hint(d, *s));
            ++s;
        } else if (less(d->first, s->first)) {
            d = dst.erase(d);
        } else {
            d->second = s->second;   // a vector keeps its capacity; a scalar is a scalar
            ++d;
            ++s;
        }
    }
    while (d != dst.end()) d = dst.erase(d);
}

inline void assignState(GJGameState& dst, GJGameState& src) {
    if (&dst == &src) return;
    static decltype(dst.m_activatedObjectIDs) d0, s0;
    static decltype(dst.m_unkMapPairGJGameEventIntVectorEventTriggerInstance) d1, s1;
    static decltype(dst.m_unkMapPairGJGameEventIntInt) d2, s2;
    static decltype(dst.m_proximityVolumeRelated) d3, s3;
    d0.swap(dst.m_activatedObjectIDs);
    s0.swap(src.m_activatedObjectIDs);
    d1.swap(dst.m_unkMapPairGJGameEventIntVectorEventTriggerInstance);
    s1.swap(src.m_unkMapPairGJGameEventIntVectorEventTriggerInstance);
    d2.swap(dst.m_unkMapPairGJGameEventIntInt);
    s2.swap(src.m_unkMapPairGJGameEventIntInt);
    d3.swap(dst.m_proximityVolumeRelated);
    s3.swap(src.m_proximityVolumeRelated);
    dst = src;   // every other member; the four maps are empty on both sides here
    dst.m_activatedObjectIDs.swap(d0);
    src.m_activatedObjectIDs.swap(s0);
    dst.m_unkMapPairGJGameEventIntVectorEventTriggerInstance.swap(d1);
    src.m_unkMapPairGJGameEventIntVectorEventTriggerInstance.swap(s1);
    dst.m_unkMapPairGJGameEventIntInt.swap(d2);
    src.m_unkMapPairGJGameEventIntInt.swap(s2);
    dst.m_proximityVolumeRelated.swap(d3);
    src.m_proximityVolumeRelated.swap(s3);
    syncOrderedMap(dst.m_activatedObjectIDs, src.m_activatedObjectIDs);
    syncOrderedMap(dst.m_unkMapPairGJGameEventIntVectorEventTriggerInstance,
                   src.m_unkMapPairGJGameEventIntVectorEventTriggerInstance);
    syncOrderedMap(dst.m_unkMapPairGJGameEventIntInt, src.m_unkMapPairGJGameEventIntInt);
    syncOrderedMap(dst.m_proximityVolumeRelated, src.m_proximityVolumeRelated);
}
#else
inline void assignState(GJGameState& dst, GJGameState& src) {
    if (&dst == &src) return;
    dst = src;
}
#endif

inline void captureState(GJBaseGameLayer* l, GJGameState& out) {
    if (l) assignState(out, l->m_gameState);
}

inline void restoreState(GJBaseGameLayer* l, GJGameState& in) {
    if (l) assignState(l->m_gameState, in);
}

inline bool g_snapAct = true;
inline std::vector<EnhancedGameObject*> g_actObjs;
inline std::vector<EnhancedGameObject*> g_actDecos;
inline std::vector<uint8_t> g_decoSeen;   // per g_actDecos: either flag ever seen set
inline long long g_actCaptures = 0, g_actRestores = 0;
inline double g_actCapUs = 0.0, g_actRestUs = 0.0;
inline std::vector<uint8_t> g_actSeen;

inline void buildActWindow(GJBaseGameLayer* l, double x0, double x1) {
    g_actObjs.clear();
    g_actDecos.clear();
    g_actSeen.clear();
    g_actCaptures = g_actRestores = 0;
    g_actCapUs = g_actRestUs = 0.0;
    if (!g_snapAct || !l || !l->m_objects) return;
    for (unsigned i = 0; i < l->m_objects->count(); ++i) {
        auto* o = static_cast<GameObject*>(l->m_objects->objectAtIndex(i));
        if (!o) continue;
        const double x = o->getPositionX();
        if (x < x0 || x > x1) continue;
        auto* e = typeinfo_cast<EnhancedGameObject*>(o);
        if (!e) continue;
        (o->m_objectType == GameObjectType::Decoration ? g_actDecos : g_actObjs).push_back(e);
    }
    g_actSeen.assign(g_actObjs.size(), 0);
    g_decoSeen.assign(g_actDecos.size(), 0);
}

// OR the decorations' flags into g_decoSeen (called on every capture and restore).
inline void noteDecos() {
    for (size_t i = 0; i < g_actDecos.size(); ++i) {
        const auto* e = g_actDecos[i];
        if (e->m_activatedByPlayer1 || e->m_activatedByPlayer2) g_decoSeen[i] = 1;
    }
}

inline int actDecosUsed() {
    int n = 0;
    for (const auto* e : g_actDecos)
        n += (e->m_activatedByPlayer1 || e->m_activatedByPlayer2) ? 1 : 0;
    return n;
}

// ...and the ones seen with a flag at any capture or restore of the search.
inline int actDecosEver() {
    noteDecos();
    int n = 0;
    for (uint8_t s : g_decoSeen) n += s;
    return n;
}

inline uint64_t actSignature(const std::vector<uint8_t>& v) {
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : v) { h ^= b; h *= 1099511628211ull; }
    return h;
}

inline uint64_t captureActSig(std::vector<uint8_t>& out) {
    const auto t0 = std::chrono::steady_clock::now();
    out.resize(g_actObjs.size());
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < g_actObjs.size(); ++i) {
        const auto* e = g_actObjs[i];
        out[i] = (uint8_t)((e->m_activatedByPlayer1 ? 1 : 0) | (e->m_activatedByPlayer2 ? 2 : 0));
        g_actSeen[i] |= out[i];
        h ^= out[i];
        h *= 1099511628211ull;
    }
    noteDecos();
    ++g_actCaptures;
    g_actCapUs += std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - t0).count();
    return h;
}

inline void captureAct(std::vector<uint8_t>& out) { (void)captureActSig(out); }

// cfg `secactkey=0`: keep carrying the flags but leave them out of the key and the buckets.
inline bool g_actKey = true;
inline constexpr uint64_t kActKeyMul = 0xC2B2AE3D27D4EB4Full;

inline int actKeySelfCheck(const std::vector<uint8_t>& base, long long playerKey) {
    if (base.empty()) return -1;
    std::vector<uint8_t> a = base, b = base;
    a.front() ^= 1;
    b.back() ^= 2;
    const uint64_t s0 = actSignature(base), sa = actSignature(a), sb = actSignature(b);
    const long long k0 = playerKey ^ (long long)(s0 * kActKeyMul);
    const long long ka = playerKey ^ (long long)(sa * kActKeyMul);
    const long long kb = playerKey ^ (long long)(sb * kActKeyMul);
    const bool ok = s0 != sa && s0 != sb && (base.size() == 1 || sa != sb)
                    && k0 != ka && k0 != kb;
    return ok ? 1 : 0;
}

inline void restoreAct(const std::vector<uint8_t>& in) {
    if (in.size() != g_actObjs.size()) return;
    const auto t0 = std::chrono::steady_clock::now();
    noteDecos();   // what the step before this restore left on them
    for (size_t i = 0; i < g_actObjs.size(); ++i) {
        g_actObjs[i]->m_activatedByPlayer1 = (in[i] & 1) != 0;
        g_actObjs[i]->m_activatedByPlayer2 = (in[i] & 2) != 0;
    }
    ++g_actRestores;
    g_actRestUs += std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - t0).count();
}

inline std::pair<long long, long long> originUsedLost(const std::vector<uint8_t>& want) {
    if (want.size() != g_actObjs.size()) return {0, 0};
    long long used = 0, lost = 0;
    for (size_t i = 0; i < g_actObjs.size(); ++i) {
        if (!want[i]) continue;
        ++used;
        const uint8_t now = (uint8_t)((g_actObjs[i]->m_activatedByPlayer1 ? 1 : 0)
                                      | (g_actObjs[i]->m_activatedByPlayer2 ? 2 : 0));
        if (want[i] & ~now) ++lost;
    }
    return {used, lost};
}

// Objects ever seen activated by player 1 / player 2 in a captured node.
inline int actSeenCount(uint8_t bit) {
    int n = 0;
    for (uint8_t s : g_actSeen) n += (s & bit) ? 1 : 0;
    return n;
}

inline bool g_snapEM = true;
inline long long g_snapEMFrom = 0;
inline bool g_snapEMActive = false;

struct EMSnap {
    gd::vector<PulseEffectAction> pulses;
    std::shared_ptr<EffectManagerState> full;   // cfg secsnapem only
};

inline void captureEM(GJBaseGameLayer* l, EMSnap& out) {
    auto* em = l ? l->m_effectManager : nullptr;
    if (!em) {
        out.pulses.clear();
        out.full.reset();
        return;
    }
    out.pulses = em->m_pulseEffectVector;
    if (g_snapEMActive) {
        out.full = std::make_shared<EffectManagerState>();   // fresh: saveToState may append
        em->saveToState(*out.full);
    } else {
        out.full.reset();
    }
}

inline void restoreEM(GJBaseGameLayer* l, const EMSnap& in) {
    auto* em = l ? l->m_effectManager : nullptr;
    if (!em) return;
    if (g_snapEMActive && in.full) {
        EffectManagerState copy = *in.full;
        em->loadFromState(copy);
    }
    em->m_pulseEffectVector = in.pulses;
}

static_assert(offsetof(PlayerObject, m_position) == gdoff::kPlayerPosition,
              "PlayerObject::m_position moved");
inline cocos2d::CCPoint physPosition(PlayerObject* p, GJBaseGameLayer* l) {
    if (p && l) {
        const auto* lb = reinterpret_cast<const uint8_t*>(l);
        const float t = *reinterpret_cast<const float*>(lb + gdoff::kLayerLevelFlipping);
        const bool stored = reinterpret_cast<const uint8_t*>(p)[gdoff::kPlayerLocked] == 0;
        if (t > 0.f && t < 1.f && stored) return p->m_position;
    }
    return p ? p->getPosition() : cocos2d::CCPoint{};
}

// One player's bytes and its node extras (x, y, rotation), at dst.
inline void capturePlayer(PlayerObject* p, uint8_t* dst, GJBaseGameLayer* l = nullptr) {
    std::memcpy(dst, (const void*)p, sizeof(PlayerObject));
    const cocos2d::CCPoint at = physPosition(p, l);
    const float ex[3] = {at.x, at.y, p->getRotation()};
    std::memcpy(dst + sizeof(PlayerObject), ex, kExtra);
}

inline void captureRaw(PlayerObject* p, GJBaseGameLayer* l, std::vector<uint8_t>& out) {
    PlayerObject* p2 = partner(p, l);
    const size_t one = sizeof(PlayerObject) + kExtra;
    out.resize(one + layerBytes() + (p2 ? one : 0));
    capturePlayer(p, out.data(), l);
    size_t o = one;
    for (auto& m : layerRestoreList()) {
        std::memcpy(out.data() + o, (const uint8_t*)l + m.off, m.size);
        o += m.size;
    }
    if (p2) capturePlayer(p2, out.data() + o, l);
}

inline void capture(PlayerObject* p, GJBaseGameLayer* l,
                    std::vector<uint8_t>& out) {
    observe(p);
    if (PlayerObject* p2 = partner(p, l)) observe(p2);
    captureRaw(p, l, out);
}

inline bool g_cpCheck = false;
inline const char* const kCpCheckP1[] = {"m_stateRingJump"};
inline const char* const kCpCheckP2[] = {"m_padRingRelated", "m_stateJumpBuffered", "m_wasRobotJump",
                                         "m_blackOrbRelated", "m_yVelocityBeforeSlope",
                                         "m_slopeStartTime"};

inline const Mem* scalarNamed(const char* n) {
    for (const auto& m : scalars())
        if (std::strcmp(m.name, n) == 0) return &m;
    return nullptr;
}

inline std::string cpValue(const uint8_t* p, size_t n) {
    char b[40];
    if (n == 1) snprintf(b, sizeof(b), "%d", (int)p[0]);
    else if (n == 4) { float f; std::memcpy(&f, p, 4); snprintf(b, sizeof(b), "%.6g", (double)f); }
    else if (n == 8) { double d; std::memcpy(&d, p, 8); snprintf(b, sizeof(b), "%.9g", d); }
    else snprintf(b, sizeof(b), "(%zu bytes)", n);
    return b;
}

inline bool cpPlayerPart(const std::vector<uint8_t>* s[3], size_t off, const char* const* names,
                         size_t n, std::string& out, uint32_t h[3]) {
    for (int k = 0; k < 3; ++k)
        if (s[k]->size() < off + sizeof(PlayerObject)) return false;
    for (int k = 0; k < 3; ++k) h[k] = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        const Mem* m = scalarNamed(names[i]);
        if (!m) { out += std::string(" ") + names[i] + "=?"; continue; }
        out += std::string(" ") + names[i] + "=";
        for (int k = 0; k < 3; ++k) {
            const uint8_t* p = s[k]->data() + off + m->off;
            for (size_t j = 0; j < m->size; ++j) { h[k] ^= p[j]; h[k] *= 16777619u; }
            out += (k ? "/" : "") + cpValue(p, m->size);
        }
    }
    return true;
}

inline std::string cpCheckLine(const char* site, long long at, const std::vector<uint8_t>& origin,
                               const std::vector<uint8_t>& load, const std::vector<uint8_t>& after,
                               bool writeBack) {
    const std::vector<uint8_t>* s[3] = {&origin, &load, &after};
    std::string p1, p2;
    uint32_t h1[3] = {0, 0, 0}, h2[3] = {0, 0, 0};
    cpPlayerPart(s, 0, kCpCheckP1, sizeof(kCpCheckP1) / sizeof(kCpCheckP1[0]), p1, h1);
    const bool has2 = cpPlayerPart(s, sizeof(PlayerObject) + kExtra + layerBytes(), kCpCheckP2,
                                   sizeof(kCpCheckP2) / sizeof(kCpCheckP2[0]), p2, h2);
    char b[200];
    snprintf(b, sizeof(b), " | hash origin/load/after p1=%08x/%08x/%08x p2=%08x/%08x/%08x | "
             "load=origin:%s after=origin:%s", h1[0], h1[1], h1[2], h2[0], h2[1], h2[2],
             (h1[1] == h1[0] && (!has2 || h2[1] == h2[0])) ? "yes" : "no",
             (h1[2] == h1[0] && (!has2 || h2[2] == h2[0])) ? "yes" : "no");
    return std::string("seccp: site=") + site + " at=" + std::to_string(at) + " writeback="
           + (writeBack ? "1" : "0") + " p1[" + p1 + " ] p2[" + (has2 ? p2 : " none") + " ]" + b;
}

inline void restorePlayer(PlayerObject* p, const uint8_t* bytes) {
    g_psnapPlayerWritten = true;
    const auto& m = mask();
    uint8_t* dst = (uint8_t*)p;
    size_t i = 0;
    const size_t n = m.size();
    while (i < n) {
        if (!m[i] || i < g_maskLo || i >= g_maskHi
            || (i >= g_maskExLo && i < g_maskExHi)) { ++i; continue; }
        size_t j = i;
        while (j < n && m[j] && j < g_maskHi
               && !(j >= g_maskExLo && j < g_maskExHi)) ++j;
        std::memcpy(dst + i, bytes + i, j - i);
        i = j;
    }
    if (!g_skipExtras) {
        float ex[3];
        std::memcpy(ex, bytes + sizeof(PlayerObject), kExtra);
        p->setPosition({ex[0], ex[1]});
        p->setRotation(ex[2]);
    }
}

inline void restore(PlayerObject* p, GJBaseGameLayer* l, const uint8_t* bytes) {
    restorePlayer(p, bytes);
    size_t o = sizeof(PlayerObject) + kExtra;
    for (auto& m : layerRestoreList()) {
        std::memcpy((uint8_t*)l + m.off, bytes + o, m.size);
        o += m.size;
    }
    PlayerObject* p2 = partner(p, l);
    if (p2) {
        restorePlayer(p2, bytes + o);
        if (!g_keepSnapPos && !g_skipExtras) p2->m_position = p2->getPosition();
    }
    if (!g_keepSnapPos && !g_skipExtras) p->m_position = p->getPosition();
}
}  // namespace psnap
