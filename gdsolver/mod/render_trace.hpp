#pragma once
// Render-suppression predicate and modifier-counter tracing.
#include "mod/hud.hpp"

using namespace p1;

inline bool renderSuppressed() {
    if (g_renderOff) return true;      // F8
    return g_started && !g_sessionOver && g_cfg.skipRender && !g_realtimeOverride;
}

inline void traceModifierCounters(PlayerObject* p) {
    if (!g_cfg.hitboxTrace || !g_started || g_sessionOver) return;
    const char* pb = reinterpret_cast<const char*>(p);
    int v[5];
    memcpy(&v[0], pb + gdoff::kPlayerStateNoAutoJump, 4);
    memcpy(&v[1], pb + gdoff::kPlayerStateDartSlide, 4);
    memcpy(&v[2], pb + gdoff::kPlayerStateHitHead, 4);
    memcpy(&v[3], pb + gdoff::kPlayerStateFlipGravity, 4);
    memcpy(&v[4], pb + gdoff::kPlayerStateForce, 4);
    static int prev[5] = {0, 0, 0, 0, 0};
    bool up = false;
    for (int i = 0; i < 5; ++i) {
        if (v[i] > 0 && v[i] > prev[i]) up = true;
        if (v[i] <= 0 && prev[i] > 0) up = true;
    }
    for (int i = 0; i < 5; ++i) prev[i] = v[i];
    const bool inWindow = g_tick >= g_cfg.hbFrom
                          && (g_cfg.hbTo <= 0 || g_tick <= g_cfg.hbTo);
    if (!up && !inWindow) return;
    static int lines = 0;
    if (++lines > 400) return;
    char b[192];
    snprintf(b, sizeof(b),
             "mod: t=%lld x=%.3f y=%.3f noAuto=%d dart=%d hitHead=%d "
             "flipGrav=%d force=%d",
             (long long)g_tick, p->getPositionX(), p->getPositionY(),
             v[0], v[1], v[2], v[3], v[4]);
    writeResult(b);
}
