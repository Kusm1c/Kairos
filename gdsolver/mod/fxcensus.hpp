#pragma once
// Census of live effect nodes and sweeping of leaked circle waves (fast-mode hygiene).
#include "mod/itermap.hpp"

namespace p1 {
inline void censusInto(cocos2d::CCNode* n, int depth, std::map<std::string, int>& out) {
    if (!n || depth > 3) return;
    const char* kind = "other";
    // Test the most-derived types first (a CCParticleSystemQuad is also a CCParticleSystem)
    if (geode::cast::typeinfo_cast<PlayerObject*>(n))                      kind = "PlayerObject";
    else if (geode::cast::typeinfo_cast<GameObject*>(n))                   kind = "GameObject";
    else if (geode::cast::typeinfo_cast<cocos2d::CCParticleSystem*>(n))    kind = "Particle";
    else if (geode::cast::typeinfo_cast<CCCircleWave*>(n))                 kind = "CircleWave";
    else if (geode::cast::typeinfo_cast<cocos2d::CCSpriteBatchNode*>(n))   kind = "BatchNode";
    else if (geode::cast::typeinfo_cast<cocos2d::CCLabelBMFont*>(n))       kind = "Label";
    else if (geode::cast::typeinfo_cast<cocos2d::CCSprite*>(n))            kind = "Sprite";
    else if (geode::cast::typeinfo_cast<cocos2d::CCLayer*>(n))             kind = "Layer";
    ++out[kind];
    auto* kids = n->getChildren();
    if (!kids) return;
    for (unsigned i = 0; i < kids->count(); ++i)
        censusInto(static_cast<cocos2d::CCNode*>(kids->objectAtIndex(i)), depth + 1, out);
}

inline std::string fxCensus(const char* tag) {
    std::map<std::string, int> c;
    if (auto* pl = PlayLayer::get()) censusInto(pl, 0, c);
    std::string s = std::string("fxcensus[") + tag + "]";
    for (auto& kv : c) s += " " + kv.first + "=" + std::to_string(kv.second);
    return s;
}

inline bool g_fxSweep = true;
inline long long g_fxSwept = 0;
inline void collectCircleWaves(cocos2d::CCNode* n, int depth,
                               std::vector<CCCircleWave*>& out) {
    if (!n || depth > 3) return;
    auto* kids = n->getChildren();
    if (!kids) return;
    for (unsigned i = 0; i < kids->count(); ++i) {
        auto* k = static_cast<cocos2d::CCNode*>(kids->objectAtIndex(i));
        if (auto* cw = geode::cast::typeinfo_cast<CCCircleWave*>(k)) out.push_back(cw);
        else collectCircleWaves(k, depth + 1, out);
    }
}
inline void sweepCircleWaves() {
    auto* pl = PlayLayer::get();
    if (!pl) return;
    std::vector<CCCircleWave*> waves;
    collectCircleWaves(pl, 0, waves);
    for (auto* cw : waves) {
        cw->retain();
        if (cw->m_delegate) cw->m_delegate->circleWaveWillBeRemoved(cw);
        cw->m_delegate = nullptr;   // cut off further callbacks (the other side may die first)
        cw->removeFromParentAndCleanup(true);
        cw->autorelease();
        ++g_fxSwept;
    }
}

inline long long g_visitAVs = 0;
inline long long g_visAVs = 0;
// Times a re-entrantly called update was passed through (so that "zero" is visible)
inline long long g_reentrantUpdates = 0;
// Times the auto-pause on focus loss was swallowed (the main cause of frozen workers)
inline long long g_unfocusPauseBlocked = 0;
inline void logVisitCrashSwallowed() {
    static int s_count = 0;
    ++g_visitAVs;
    if (++s_count <= 5)
        log::warn("GJBaseGameLayer::visit access violation swallowed ({}), frame skipped",
                  s_count);
}

inline constexpr int UI_MODE_NORMAL = 0;   // the game as it is; the mod only watches
inline constexpr int UI_MODE_REPLAY = 1;   // replay a stored solution file for this level
inline constexpr int UI_MODE_SOLVE = 2;    // solve the level in-process, then replay the result
inline constexpr int UI_MODE_COUNT = 3;

inline const char* uiModeName(int m) {
    switch (m) {
        case UI_MODE_REPLAY: return "Replay";
        case UI_MODE_SOLVE: return "Solve";
        default: return "Normal";
    }
}

inline int g_uiMode = UI_MODE_NORMAL;
inline bool g_uiCoins = false;
inline bool g_uiGlitch = false;
inline constexpr int UI_SEARCH_DP = 0;
inline constexpr int UI_SEARCH_BEAM = 1;
inline constexpr int UI_SEARCH_MONTECARLO = 2;
inline constexpr int UI_SEARCH_COUNT = 3;
inline const char* uiSearchName(int s) {   // as the menu shows it
    switch (s) {
        case UI_SEARCH_BEAM: return "Beam";
        case UI_SEARCH_MONTECARLO: return "Monte Carlo";
        default: return "DP";
    }
}
inline const char* uiSearchCfg(int s) {    // as cfg dpsearch and dp's --search spell it
    switch (s) {
        case UI_SEARCH_BEAM: return "beam";
        case UI_SEARCH_MONTECARLO: return "montecarlo";
        default: return "dp";
    }
}
inline int g_uiSearch = UI_SEARCH_DP;
inline const void* g_uiArmedLevel = nullptr;

// F8: temporarily toggle between fast mode and realtime (spectating hotkey)
inline bool g_realtimeOverride = false;
inline long long g_watchAtTick = 0;
inline float g_watchAtX = 0.f;
inline long long g_watchBackTicks = 0;
inline long long g_watchStartTick = -1;   // tick at which we switched to realtime
inline double g_watchAfterSec = 0.0;
inline double g_watchCycleSec = 0.0;
inline double g_watchCycleLast = -1.0;   // wall-clock second of the last flip
inline long long g_watchFlips = 0;

// Measurement of the "idle spinning" from death to the start of the next attempt
}  // namespace p1
