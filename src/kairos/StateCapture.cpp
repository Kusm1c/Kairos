#include "StateCapture.hpp"

#include "kpsim/engine/PlayerEngine.hpp"

#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

using namespace geode::prelude;
using kpsim::engine::EngineSnap;

namespace kp {

void captureEngineSnap(PlayerObject* p, EngineSnap& s) {
#define SNAP_SCALAR(T, n, off) s.n = p->n;
#define SNAP_POINT(n, off)     s.n##_x = p->n.x; s.n##_y = p->n.y;
#define SNAP_OBJ(n, off)       s.n = p->n ? p->n->m_uniqueID : 0;
#define SNAP_SEED(n, off)      s.n = p->n.value();
#define SNAP_NODEPOS(n)        { const auto pt = p->getPosition(); s.n##_x = pt.x; s.n##_y = pt.y; }
#include "kpsim/engine/SnapFields.inc"
#undef SNAP_SCALAR
#undef SNAP_POINT
#undef SNAP_OBJ
#undef SNAP_SEED
#undef SNAP_NODEPOS
}

bool isVolatileField(const char* name) {
    static const std::string_view kSkip[] = {
        "m_totalTime",                    // accumulates across attempts
        "m_position_x", "m_position_y",   // set after each game update only
        "m_audioScale",                   // music level
    };
    for (auto s : kSkip)
        if (s == name) return true;
    for (const char* const* p = kpsim::engine::kNonPhysicsFields; *p; ++p)
        if (std::strcmp(*p, name) == 0) return true;
    return false;
}

uint64_t physicsHash(const EngineSnap& s, double grid) {
    static const std::vector<bool> skip = [] {
        std::vector<bool> v;
        kpsim::engine::forEachField(EngineSnap{}, [&](const char* name, double) { v.push_back(isVolatileField(name)); });
        return v;
    }();
    uint64_t h = 1469598103934665603ull;
    size_t i = 0;
    kpsim::engine::forEachField(s, [&](const char*, double v) {
        if (skip[i++]) return;
        if (grid > 0) v = std::round(v / grid);
        if (v == 0.0) v = 0.0;   // -0 == +0
        uint64_t bits;
        std::memcpy(&bits, &v, sizeof bits);
        h = (h ^ bits) * 1099511628211ull;
    });
    return h;
}

std::string physicsDiff(const EngineSnap& expected, const EngineSnap& got) {
    std::string out;
    int shown = 0;
    for (const auto& d : kpsim::engine::diff(expected, got)) {
        if (isVolatileField(d.field.c_str())) continue;
        if (shown++ >= 4) break;
        out += fmt::format("{} {:.9g}->{:.9g} ", d.field, d.expected, d.got);
    }
    return out;
}

#define COPY_FIELD(dst, src, n) std::memcpy((void*)&(dst)->n, (const void*)&(src)->n, sizeof((dst)->n))

void copyPlayer(PlayerObject* p, PlayerCopy& out) {
    auto* b = reinterpret_cast<const uint8_t*>(p);
    out.bytes.assign(b, b + sizeof(PlayerObject));
#define PLAYER_BOX(n) out.boxes.n = p->n;
#include "PodFields.inc"
#undef PLAYER_BOX
    out.position = p->getPosition();
    out.rotationX = p->getRotationX();
    out.rotationY = p->getRotationY();
    out.scaleX = p->getScaleX();
    out.scaleY = p->getScaleY();
}

void pastePlayer(PlayerObject* p, const PlayerCopy& in) {
    // Node setters first: they may touch fields the copy then puts back.
    p->setPosition(in.position);
    p->setRotationX(in.rotationX);
    p->setRotationY(in.rotationY);
    p->setScaleX(in.scaleX);
    p->setScaleY(in.scaleY);
    auto* src = reinterpret_cast<const PlayerObject*>(in.bytes.data());
#define PLAYER_POD(n) COPY_FIELD(p, src, n);
#define PLAYER_REF(n) COPY_FIELD(p, src, n);
#include "PodFields.inc"
#undef PLAYER_POD
#undef PLAYER_REF
#define PLAYER_BOX(n) p->n = in.boxes.n;
#include "PodFields.inc"
#undef PLAYER_BOX
}

void copyLayer(PlayLayer* layer, LayerCopy& out) {
    auto* b = reinterpret_cast<const uint8_t*>(layer);
    out.bytes.assign(b, b + sizeof(PlayLayer));
}

void pasteGameState(PlayLayer* layer, const LayerCopy& in) {
    auto* src = &reinterpret_cast<const PlayLayer*>(in.bytes.data())->m_gameState;
    auto* dst = &layer->m_gameState;
#define STATE_POD(n) COPY_FIELD(dst, src, n);
#define STATE_REF(n) COPY_FIELD(dst, src, n);
#include "PodFields.inc"
#undef STATE_POD
#undef STATE_REF
}

std::string layerDiff(PlayLayer* layer, const LayerCopy& in) {
    static const std::string_view kExpected[] = {
        "m_attempts", "m_savedAttempts", "m_jumps", "m_clicks", "m_attemptTime", "m_currentTime", "m_timePlayed",
        "m_timestamp", "m_resumeTimer", "m_extraDelta", "m_loadingProgress", "m_lastAttemptPercent",
        "m_pauseTime", "m_pauseDelta", "m_bestAttemptTime", "m_uncommittedJumps", "m_hasJumped",
    };
    auto* src = reinterpret_cast<const PlayLayer*>(in.bytes.data());
    std::string out;
    auto check = [&](const char* name, const void* a, const void* b, size_t n) {
        if (std::memcmp(a, b, n) == 0) return;
        for (auto e : kExpected)
            if (e == name) return;
        out += name;
        out += ' ';
    };
#define LAYER_POD(n) check(#n, &layer->n, &src->n, sizeof(layer->n));
#include "PodFields.inc"
#undef LAYER_POD
    return out;
}

} // namespace kp
