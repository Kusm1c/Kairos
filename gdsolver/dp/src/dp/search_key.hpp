#pragma once
#include "dp/thread_pool.hpp"

namespace dp {
inline std::vector<double> g_topAhead;   // per 30 px bucket
inline double g_bucketX0 = 0.0;
inline double topAheadAt(double x) {
    if (g_topAhead.empty()) return 1e9;
    long long i = (long long)((x - g_bucketX0) / 30.0);
    if (i < 0) i = 0;
    if (i >= (long long)g_topAhead.size()) return -1e9;
    return g_topAhead[(size_t)i];
}
inline bool g_escDyn = true;
inline std::vector<double> g_topAheadDyn;   // per 30 px bucket, on g_topAhead's grid
inline double topAheadDynAt(double x) {
    if (g_topAheadDyn.empty()) return -1e18;
    long long i = (long long)((x - g_bucketX0) / 30.0);
    if (i < 0) i = 0;
    if (i >= (long long)g_topAheadDyn.size()) return -1e18;
    return g_topAheadDyn[(size_t)i];
}
inline double topAheadAtF(int f, double x) {
    if ((f & 3) == 0) return topAheadAt(x);
    const auto& slot = g_frameLv[(size_t)(f & 3)];
    if (!slot || slot->topAhead.empty()) return 1e9;
    long long i = (long long)((x - slot->bucketX0) / 30.0);
    if (i < 0) i = 0;
    if (i >= (long long)slot->topAhead.size()) return -1e9;
    return slot->topAhead[(size_t)i];
}
inline void topAheadIndex(int f, double x, long long& i, long long& n) {
    const std::vector<double>* t = nullptr;
    double x0 = 0.0;
    if ((f & 3) == 0) {
        t = &g_topAhead;
        x0 = g_bucketX0;
    } else {
        const auto& slot = g_frameLv[(size_t)(f & 3)];
        if (slot) { t = &slot->topAhead; x0 = slot->bucketX0; }
    }
    n = t ? (long long)t->size() : 0;
    i = (t && n) ? std::max(0LL, (long long)((x - x0) / 30.0)) : -1;
}
inline double g_shipYq = 0.5;   // multiplier: 0.5 -> 2px bins
inline double g_shipVq = 2.5;   // multiplier: 2.5 -> 0.4 bins
inline double g_cubeYq = 2.0;
inline double g_cubeVq = 10.0;
inline double g_dualFreeQ = 0.125;
inline constexpr bool kDefHeldKeyRead = true;
inline bool g_heldKeyRead = kDefHeldKeyRead;
inline constexpr double kDefKeyXq = 4.0;
inline double g_keyXq = kDefKeyXq;
inline double g_keyXqFly = kDefKeyXq;

inline uint8_t gdUpOf(const State& s) {
    return (s.frame == 3) ? (uint8_t)!s.flip : s.flip;
}

inline bool nearPressBox(const State& s) {
    if (g_pressWin.empty() || s.frame != 0) return false;
    const double x = (double)s.xAbs;
    for (const PressWin& w : g_pressWin)
        if (x >= w.lo && x <= w.hi && !s.trig.test(w.bit)) return true;
    return false;
}
inline uint64_t pressKey(const State& s) {
    if (g_pressWin.empty() || s.frame != 0) return 0;
    const double x = (double)s.xAbs;
    uint64_t k = 0;
    for (const PressWin& w : g_pressWin) {
        if (x < w.lo || x > w.hi) continue;
        if (s.trig.test(w.bit)) k ^= 0x5851F42D4C957F2Dull * (uint64_t)(w.bit + 1);
        else if (s.action) k ^= 0xE7037ED1A0B428DBull * (uint64_t)(w.bit + 1);
    }
    return k;
}

inline bool bandClamps(const State& s) {
    auto m = [](int k) { return k == 1 || k == 2 || k == 3 || k == 4 || k == 6 || k == 7; };
    return m(s.mode) || (s.dual && m(s.mode2));
}
inline uint64_t coinKeyBits(const State& s) {
    return s.coins ? ((uint64_t)s.coins * 0x2545F4914F6CDD1Dull) : 0;
}
inline uint64_t modelKeyOf(const State& s, long long t) {
    const bool flying = (s.mode == 1 || s.mode == 3);
    const double ys = flying ? g_shipYq : g_cubeYq;
    const double vs = flying ? g_shipVq : g_cubeVq;
    const int32_t yq = (int32_t)std::lround(s.y * ys);
    const int32_t vq = (int32_t)std::lround(s.vy * vs);
    const bool flyX = (s.mode == 1 || s.mode == 3 || s.mode == 4 || s.mode == 7);
    const int32_t xq = (int32_t)std::lround(s.xAbs * (flyX ? g_keyXqFly : g_keyXq));
    uint64_t k = ((uint64_t)(uint32_t)yq << 32) ^ ((uint64_t)(uint32_t)xq << 18)
           ^ ((uint32_t)vq << 6)
           ^ ((uint64_t)s.mini << 5)
           ^ ((uint64_t)s.ringHold << 4)
           ^ ((s.pressSpent != s.ringHold)
                  ? ((uint64_t)s.pressSpent * 0x9E3779B97F4A7C15ull) : 0)
           ^ (s.dual ? ((uint64_t)(uint32_t)(int32_t)std::lround(
                            s.y2 * (s.freeHalf ? ys * g_dualFreeQ : ys)) << 40)
                     : 0)
           ^ (s.dual ? ((uint64_t)(uint32_t)(int32_t)std::lround(
                            s.vy2 * (s.freeHalf ? vs * g_dualFreeQ : vs)) << 24)
                     : 0)
           ^ ((uint64_t)s.dual << 7) ^ ((uint64_t)s.flip2 << 6)
           ^ ((uint64_t)s.mode << 3) ^ ((uint64_t)s.flip << 2)
           ^ ((s.dual && s.mode2 != s.mode)
                  ? ((uint64_t)s.mode2 * 0xFF51AFD7ED558CCDull) : 0)
           ^ ((s.dual && s.mini2 != s.mini)
                  ? ((uint64_t)s.mini2 * 0xC4CEB9FE1A85EC53ull) : 0)
           ^ (bandClamps(s) ? ((uint64_t)(uint32_t)(int32_t)std::lround(s.bandFloor) << 46)
                            : 0)
           ^ (((s.bandBranch & 3) == 1 && bandClamps(s))
                  ? ((uint64_t)(uint32_t)(int32_t)std::lround(
                        kBandRetracted
                        - (kBandRetracted - ((double)s.bandCeil
                                             - (double)s.bandFloor))
                          * bandProgress(s.bandAnim))
                     * 0x9E3779B97F4A7C15ull)
                  : 0)
           ^ ((uint64_t)s.rHover << 56)
           ^ ((uint64_t)s.dashing << 54)
           ^ ((uint64_t)((s.rHover || s.dashing) ? s.action : 0) << 55)
           // --dualdash: ...and the second body's dash, which its own step now keeps (0 off)
           ^ ((s.dual && s.dashing2)
                  ? (0x5B2E7A1D93C4F086ull ^ ((uint64_t)s.action << 31)) : 0)
           ^ ((s.dual && s.dx2 != 0.f)
                  ? ((uint64_t)(uint32_t)(int32_t)std::lround((double)s.dx2 * 4.0)
                     * 0xD6E8FEB86659FD93ull)
                  : 0)
           ^ pressKey(s)
           ^ ((g_airPress && s.mode == 0 && s.jumpBuf) ? 0x6C62272E07BB0142ull : 0)
           ^ (s.onSlope ? ((uint64_t)s.slopeT << 47) : 0)
           ^ ((s.dual && s.onSlope2) ? ((uint64_t)s.slopeT2 << 12) : 0)
           ^ ((s.onSlope && s.rideLanded) ? 0xD1B54A32D192ED03ull : 0)
           ^ ((s.dual && s.onSlope2 && s.rideLanded2)
                  ? 0xA24BAED4963EE407ull : 0)
           ^ (s.pFlap ? 0x9E3779B97F4A7C15ull : 0)
           // ...and the spider's handed-over press (State::pSpiderTap), same reason
           ^ (s.pSpiderTap ? 0xC2B2AE3D27D4EB4Full : 0)
           ^ (s.pNoTerm ? 0x94D049BB133111EBull : 0)
           ^ (s.boost ? 0xA0761D6478BD642Full : 0)
           ^ ((s.dual && s.boost2) ? 0x7B7D159C79E2A32Full : 0)
           ^ ((s.a1cLatch & 1u) ? 0x6A09E667F3BCC909ull : 0)
           ^ ((s.dual && (s.a1cLatch & 2u)) ? 0xBB67AE8584CAA73Bull : 0)
           ^ ((uint64_t)(s.ball728 & 3u) * 0xA54FF53A5F1D36F1ull)
           ^ (s.dual ? (uint64_t)((s.ball728 >> 2) & 3u) * 0x510E527FADE682D1ull : 0)
           ^ ((s.seatT > 0) ? 0x3C6EF372FE94F82Bull : 0)
           ^ (s.pExitVy != 0.f
                  ? (uint64_t)(int64_t)((double)s.pExitVy * 1000.0)
                        * 0xD6E8FEB86659FD93ull
                  : 0)
           ^ (s.onSlope ? ((uint64_t)(uint32_t)s.slopeUid0 * 0x9E3779B1ull)
                        : 0)
           ^ (s.onSlope ? ((uint64_t)(uint32_t)s.slopeUidNow * 0x85EBCA77ull)
                        : 0)
           ^ ((uint64_t)s.fgArm * 0xBF58476D1CE4E5B9ull)
           ^ (s.ogLinger ? 0x3C6EF372FE94F82Bull : 0)
           ^ (s.holdDead ? 0x1F83D9ABFB41BD6Bull : 0)
           ^ ((s.armT < kArmTicks) ? 0xFF51AFD7ED558CCDull : 0)
           ^ ((s.armT == 0) ? 0x2545F4914F6CDD1Dull : 0)
           ^ ((s.spiderJumpT < spiderJumpGraceTicks())
                  ? ((uint64_t)(s.spiderJumpT + 1) * 0x94D049BB133111EBull) : 0)
           ^ (s.items ? ((uint64_t)popCount32(s.items) * 0xD1B54A32D192ED03ull) : 0)
           // ...and the counting tap's presses, a number for the same reason.
           ^ (s.taps ? ((uint64_t)s.taps * 0x9FB21C651E98DF25ull) : 0)
           ^ (s.frameChg ? 0x27D4EB2F165667C5ull : 0)
           ^ (s.ceilT ? (0x165667B19E3779F9ull
                         * (uint64_t)(s.ceilT | ((uint32_t)s.ceilM4 << 8)))
                      : 0)
           // --heldkeyread: only where the body's next tick reads or writes it (see above)
           ^ ((!g_heldKeyRead || s.dual || s.mode == 1 || s.mode == 3 || s.mode == 4
               || s.mode == 7)
                  ? ((uint64_t)s.held << 1) : 0)
           ^ s.grounded;
    if (s.portalLatch.word(0))
        k ^= s.portalLatch.word(0) * 0xFF51AFD7ED558CCDull;
    if (s.portalLatch2.word(0))
        k ^= s.portalLatch2.word(0) * 0xC4CEB9FE1A85EC53ull;
    for (int w = 1; w < GravLatch::kWords; ++w) {
        const unsigned r = (unsigned)(7 * w);
        if (const uint64_t v = s.portalLatch.word(w))
            k ^= ((v * 0xFF51AFD7ED558CCDull) << r) | ((v * 0xFF51AFD7ED558CCDull) >> (64 - r));
        if (const uint64_t v = s.portalLatch2.word(w))
            k ^= ((v * 0xC4CEB9FE1A85EC53ull) << r) | ((v * 0xC4CEB9FE1A85EC53ull) >> (64 - r));
    }
    if (s.trig) {
        const size_t n = std::min<size_t>(g_touchMoveTicks.size(),
                                          (size_t)kTouchBits);
        for (size_t b = 0; b < n; ++b) {
            if (!s.trig.test(b)) continue;
            const long long moving = g_touchMoveTicks[b];
            if (moving <= 0) continue;
            if (t - (long long)s.fireB[b] >= moving) continue;   // at rest
            if (g_keyCensus) ++g_keyCount[b];
            k ^= ((uint64_t)(s.fireB[b] >> 2) * 0x9E3779B97F4A7C15ull)
                 ^ ((uint64_t)(b + 1) * 0xBF58476D1CE4E5B9ull);
        }
    }
    if (s.ctrlT) k ^= 0x2545F4914F6CDD1Dull;
    if (s.action && nearCtrlHoldBox((double)s.xAbs)) k ^= 0x9FB21C651E98DF25ull;
    return k;
}
inline uint64_t keyOf(const State& s, long long t) { return modelKeyOf(s, t) ^ coinKeyBits(s); }

inline bool g_groupFire = false;
inline bool g_groupXSplit = true;
inline int g_searchMethod = 0;
inline int g_beamWidth = 2000;
inline int g_mcRollouts = 300;
inline int g_mcWindow = 1000;
inline int g_mcToggles = 30;
inline long long g_mcSteps = 30000000;
inline double g_mcSafetyS = 600.0;
inline long long g_mcStall = 200000;
inline long long g_mcCall = -1;
inline long long g_searchSeed = 0;
inline bool g_witnessAuto = false;
inline bool g_rolloutFiresAuto = false;
constexpr double kGroupWinMargin = 80.0;    // the widest of the group's window margins (speeds)
inline long long g_groupXSplitLayers = 0;   // layers where some group split (printed at the end)
inline int g_groupXSplitMax = 0;            // the most parts one group split into
inline uint64_t groupFireSig(const State& s, long long t) {
    if (!g_groupFire || !s.trig) return 0;
    uint64_t k = 0;
    const size_t n = std::min<size_t>(g_touchMoveTicks.size(), (size_t)kTouchBits);
    for (size_t b = 0; b < n; ++b) {
        if (!s.trig.test(b)) continue;
        const long long moving = g_touchMoveTicks[b];
        if (moving <= 0 || t - (long long)s.fireB[b] >= moving) continue;
        uint64_t h = ((uint64_t)(b + 1) << 32) ^ (uint64_t)s.fireB[b];
        h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
        h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
        k ^= h ^ (h >> 31);
    }
    return k;
}
}  // namespace dp
