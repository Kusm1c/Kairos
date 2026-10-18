#pragma once
#include "dp/step.hpp"

namespace dp {
struct Fixup {
    float x, y, vy, dy, dvy;
    uint8_t in, mode, mini, flip, g, gAfter;
    uint8_t kill = 0;
    // The second body, when this was measured on a pair.
    uint8_t dual = 0, gAfter2 = 255;
    float y2 = 0, vy2 = 0, dy2 = 0, dvy2 = 0;
    int ord = 0;
};
inline bool g_fixupConflictFilter = false;
inline std::vector<Fixup> g_fixups;
inline std::vector<Fixup> g_fixupKills, g_fixupDeltas;
inline long long g_fixupHits = 0;
inline float g_fixupRotX[8] = {0, 0, 0, 0, 0, 0, 0, 0};
inline std::atomic<int> g_fixupRotSeen{0};
struct alignas(64) FixupCensusRow {
    long long call[4][2];
    long long hit[4][2];
    long long callRot, hitRot;
};
inline FixupCensusRow g_fixupCensus[kThreadSlots] = {};
inline float g_fixupCallRotX[8] = {0, 0, 0, 0, 0, 0, 0, 0};
inline std::atomic<int> g_fixupCallRotSeen{0};   // x slots taken, as g_fixupRotSeen
inline FixupCensusRow fixupCensus() {
    FixupCensusRow t{};
    for (const FixupCensusRow& r : g_fixupCensus) {
        for (int f = 0; f < 4; ++f)
            for (int v = 0; v < 2; ++v) {
                t.call[f][v] += r.call[f][v];
                t.hit[f][v] += r.hit[f][v];
            }
        t.callRot += r.callRot;
        t.hitRot += r.hitRot;
    }
    return t;
}
inline bool fixupMatches(const Fixup& f, const State& s, int input) {
    if ((int)f.in != input || f.mode != s.mode || f.mini != s.mini
        || f.flip != s.flip || f.g != s.grounded)
        return false;
    if (f.dual != s.dual) return false;
    if (std::fabs((double)s.xAbs - (double)f.x) > 1.2) return false;
    if (std::fabs((double)s.y - (double)f.y) > 4.0) return false;
    if (std::fabs((double)s.vy - (double)f.vy) > 1.0) return false;
    if (f.dual) {
        if (std::fabs((double)s.y2 - (double)f.y2) > 4.0) return false;
        if (std::fabs((double)s.vy2 - (double)f.vy2) > 1.0) return false;
    }
    return true;
}
inline const Fixup* findFixup(const std::vector<Fixup>& v, const State& s,
                              int input) {
    const float lo = s.xAbs - 1.2f;
    auto it = std::lower_bound(
        v.begin(), v.end(), lo,
        [](const Fixup& f, float x) { return f.x < x; });
    const Fixup* best = nullptr;
    double bestD = 1e18;
    for (; it != v.end() && it->x <= s.xAbs + 1.2f; ++it)
        if (fixupMatches(*it, s, input)) {
            const double d =
                std::fabs((double)s.xAbs - (double)it->x) / 1.2
                + std::fabs((double)s.y - (double)it->y) / 4.0
                + std::fabs((double)s.vy - (double)it->vy) / 1.0;
            if (d < bestD) { bestD = d; best = &*it; }
        }
    return best;
}
inline const Obj* nearDynObjectPtr(const State& s, const StepCtx& K) {
    for (const Obj* o : *K.near)
        if (o->dynObj
            && std::fabs((double)s.xAbs - o->cx) < o->hw + 40.0
            && std::fabs((double)s.y - o->cy) < o->hh + 40.0)
            return o;
    return nullptr;
}
inline bool nearDynObject(const State& s, const StepCtx& K) {
    return nearDynObjectPtr(s, K) != nullptr;
}
inline thread_local int g_fxBlockedUid = -1;
inline void takeRotX(std::atomic<int>& taken, float (&xs)[8], float x) {
    int n = taken.load(std::memory_order_relaxed);
    while (n < 8 && !taken.compare_exchange_weak(n, n + 1, std::memory_order_relaxed)) {}
    if (n < 8) xs[n] = x;
}

inline void noteFixupFrame(const State& s) {
    const unsigned f = (unsigned)s.frame;
    FixupCensusRow& row = g_fixupCensus[g_threadSlot & (kThreadSlots - 1)];
    if (f < 4) ++row.hit[f][s.rev ? 1 : 0];
    if (f != 0) {
        ++row.hitRot;
        takeRotX(g_fixupRotSeen, g_fixupRotX, s.xAbs);
    }
}

inline void noteFixupCall(const State& s) {
    const unsigned f = (unsigned)s.frame;
    FixupCensusRow& row = g_fixupCensus[g_threadSlot & (kThreadSlots - 1)];
    if (f < 4) ++row.call[f][s.rev ? 1 : 0];
    if (f != 0) {
        ++row.callRot;
        takeRotX(g_fixupCallRotSeen, g_fixupCallRotX, s.xAbs);
    }
}

inline void applyFixup(const State& s, int input, State& c, bool& dead,
                       bool deltasToo = true) {
    noteFixupCall(s);
    if (const Fixup* f = findFixup(g_fixupKills, s, input)) {
        (void)f;
        dead = true;
        noteFixupFrame(s);
        ++g_fixupHits;
        return;
    }
    if (!deltasToo) return;
    if (const Fixup* f = findFixup(g_fixupDeltas, s, input)) {
        YSET(c.y) = s.y + f->dy;
        VYSET(c.vy) = s.vy + f->dvy;
        if (f->gAfter != 255) c.grounded = f->gAfter;
        if (f->dual) {
            c.y2 = s.y2 + f->dy2;
            c.vy2 = s.vy2 + f->dvy2;
            if (f->gAfter2 != 255) c.grounded2 = f->gAfter2;
        }
        dead = false;
        noteFixupFrame(s);
        ++g_fixupHits;
        return;
    }
}

inline void fxDescribe(const State& s, int input, const StepCtx& K,
                       const State& c, long long hits0) {
    const int cap = (int)sizeof g_fxWhy;
    int o = 0;
    auto clampO = [&]() { if (o >= cap) o = cap - 1; };
    o += std::snprintf(g_fxWhy + o, cap - o,
                       "Kt=%lld xAbs=%.4f y=%.4f vy=%.4f in=%d mode=%d mini=%d flip=%d "
                       "g=%d dual=%d frame=%d records=%zu",
                       (long long)K.t, (double)s.xAbs, (double)s.y, (double)s.vy, input,
                       (int)s.mode, (int)s.mini, (int)s.flip, (int)s.grounded,
                       (int)s.dual, (int)s.frame, g_fixups.size());
    clampO();
    const Obj* dyn = nullptr;
    if (K.near)
        for (const Obj* ob : *K.near)
            if (ob->dynObj && std::fabs((double)s.xAbs - ob->cx) < ob->hw + 40.0
                && std::fabs((double)s.y - ob->cy) < ob->hh + 40.0) { dyn = ob; break; }
    o += std::snprintf(g_fxWhy + o, cap - o, " nearDyn=%d", dyn ? 1 : 0);
    clampO();
    if (dyn) {
        o += std::snprintf(g_fxWhy + o, cap - o, " dynUid=%d |dx|=%.1f<%.1f |dy|=%.1f<%.1f",
                           dyn->uid, std::fabs((double)s.xAbs - dyn->cx), dyn->hw + 40.0,
                           std::fabs((double)s.y - dyn->cy), dyn->hh + 40.0);
        clampO();
    }
    o += std::snprintf(g_fxWhy + o, cap - o, " fired=%lld vyOut=%.4f",
                       g_fixupHits - hits0, (double)c.vy);
    clampO();
    for (int kd = 0; kd < 2; ++kd) {
        const std::vector<Fixup>& v = kd ? g_fixupDeltas : g_fixupKills;
        auto it = std::lower_bound(v.begin(), v.end(), s.xAbs - 1.2f,
                                   [](const Fixup& f, float x) { return f.x < x; });
        for (; it != v.end() && it->x <= s.xAbs + 1.2f && o < cap - 200; ++it) {
            const Fixup& f = *it;
            o += std::snprintf(g_fxWhy + o, cap - o,
                               " | %s x=%.4f y=%.4f vy=%.4f key in%d mode%d mini%d flip%d g%d dual%d"
                               " |dx|=%.3f |dy|=%.3f |dvy|=%.3f matches=%d",
                               kd ? "delta" : "kill", (double)f.x, (double)f.y, (double)f.vy,
                               (int)f.in == input, f.mode == s.mode, f.mini == s.mini,
                               f.flip == s.flip, f.g == s.grounded, f.dual == s.dual,
                               std::fabs((double)s.xAbs - (double)f.x),
                               std::fabs((double)s.y - (double)f.y),
                               std::fabs((double)s.vy - (double)f.vy),
                               fixupMatches(f, s, input) ? 1 : 0);
            clampO();
        }
    }
}

inline double touchPreY(const State& s, const State& c, bool set, double y) {
    return set ? y : (double)c.y;
}

inline State stepBoth(const State& s, int input, const StepCtx& K, bool& dead) {
    bool d1 = false;
    g_halfNow = 0;
    g_fxBlockedUid = -1;
    // Out-parameter, not a global: phase 1 steps the layer in parallel.
    bool p1FlippedGravity = false;
    if (g_touchCensus) g_tcBranch = 0;
    g_preBtnSet = false;
    t_ringLinkLate = 0;   // --ringtaplink
    State c = stepOne(s, input, K, d1, &p1FlippedGravity);
    const bool p1RingLate = t_ringLinkLate != 0;
    t_ringLinkLate = 0;
    if (g_touchCensus) g_tcBranchP1 = g_tcBranch;
    // p1's pre-button y, saved before p2's stepOne can overwrite it
    const bool preBtnSet = g_preBtnSet;
    const double preBtnY = g_preBtnY;
    const bool p1Tapped = g_ballTapped;   // --repelland, likewise
    const uint8_t p1HitG = g_hitGRepel;   // --repela0c, likewise
    if (!s.dual || (g_p1SoloEnd && !c.dual)) {
        dead = d1;
        markTouched(c, K, touchPreY(s, c, preBtnSet, preBtnY), s.action != 0,
                    input != 0 && s.action == 0, &s, input != 0);
        const long long fxHits0 = g_fixupHits;
        if (!g_fixups.empty()) {
            const Obj* dyn = nearDynObjectPtr(s, K);
            if (!dyn)
                applyFixup(s, input, c, dead);
            else if (findFixup(g_fixupKills, s, input) || findFixup(g_fixupDeltas, s, input))
                g_fxBlockedUid = dyn->uid;   // matched, and the gate kept it back
        }
        if (g_fxWatchT >= 0 && K.t == g_fxWatchT) fxDescribe(s, input, K, c, fxHits0);
        return c;
    }
    State sb = s;
    swapHalves(sb);
    sb.grounded2 = c.grounded;
    sb.flip2 = c.flip;
    const bool unlinked = dualUnlinkedAt((int)s.frame, (double)s.xAbs, (double)s.y);
    if (p1FlippedGravity && s.dual && !unlinked
        && sameModeFlags(s.mode, s.mode2)) {
        const uint8_t want = (uint8_t)!c.flip;
        if (sb.flip != want) {
            sb.flip = want;
            sb.vy = (float)((double)sb.vy * 0.5);
            // --ballscaleair: the partner call is a flipGravity, which lowers a ball's +0x728
            if (g_ballScaleAir && sb.mode == 2) sb.ball728 = (uint8_t)((sb.ball728 & ~3u) | 1u);
        }
    }
    const uint8_t ballF1Own = c.flip;
    bool d2 = false;
    bool p2FlippedGravity = false;
    g_halfNow = 1;
    // --dualband: the second half's portal band reads p1's finished mode, and says if it wrote
    g_bandPortalWrote = false;
    if (g_dualBand) g_dualOtherMode = (int)c.mode;
    g_p2SoloHalf = g_p2Solo;   // --p2solo
    g_p2SoloFired = false;
    t_followHalf2 = 1;   // --followy: player 1's y is sb's y2
    t_p2RingFresh = -1;  // --rtlp2ring
    const bool p2x = g_p2xOff && s.frame == 0;
    if (p2x && s.dx2 != 0.f) sb.xAbs = (float)((double)sb.xAbs + (double)s.dx2);
    State cb = stepOne(sb, input, K, d2, &p2FlippedGravity);
    if (p2x) {
        const double moved = ((double)cb.xAbs - (double)sb.xAbs) - ((double)c.xAbs - (double)s.xAbs);
        c.dx2 = (std::fabs(moved) > 0.5) ? (float)((double)s.dx2 + moved) : s.dx2;
    }
    t_followHalf2 = 0;
    const int p2RingFresh = t_p2RingFresh;
    t_p2RingFresh = -1;
    g_p2SoloHalf = false;
    const bool p2Solo = g_p2SoloFired;
    g_dualOtherMode = -1;
    const bool p2WroteBand = g_bandPortalWrote;
    g_halfNow = 0;
    const bool p2Tapped = g_ballTapped;   // --repelland
    const uint8_t p2HitG = g_hitGRepel;   // --repela0c
    swapHalves(cb);
    const uint8_t ballF2Own = cb.flip2;   // --ballscaleair (see ballF1Own)
    c.y2 = cb.y2;            c.vy2 = cb.vy2;
    c.mode2 = cb.mode2;      c.mini2 = cb.mini2;
    c.ceilT2 = cb.ceilT2;    c.ceilM42 = cb.ceilM42;
    c.slopeM2 = cb.slopeM2;  c.snapDist2 = cb.snapDist2;
    c.grounded2 = cb.grounded2; c.flip2 = cb.flip2;
    c.ringHold2 = cb.ringHold2; c.onSlope2 = cb.onSlope2;
    c.pressSpent2 = cb.pressSpent2;
    c.slopeT2 = cb.slopeT2;
    c.rideLanded2 = cb.rideLanded2;
    c.seatT2 = cb.seatT2;   // --dualseatt
    c.boost2 = cb.boost2;
    if (g_dualSlide) c.slideT2 = cb.slideT2;
    // --dualhover: ...and its robot hover budget (State::rHover2)
    if (g_dualHover) c.rHover2 = cb.rHover2;
    // --dualdash: ...and its dash (State::dashing2 / dashSlope2), for the same reason
    { c.dashing2 = cb.dashing2; c.dashSlope2 = cb.dashSlope2; }
    c.slopeUid02 = cb.slopeUid02;  c.slopeUidNow2 = cb.slopeUidNow2;
    c.snapObj2 = cb.snapObj2;   c.usedOrb2 = cb.usedOrb2;
    c.usedOrbOld2 = cb.usedOrbOld2;
    for (int i = 0; i < 3; ++i) c.usedOrbHist2[i] = cb.usedOrbHist2[i];
    for (int i = 0; i < 2; ++i) c.spdFired2[i] = cb.spdFired2[i];
    for (int i = 0; i < 2; ++i) c.spdMore2[i] = cb.spdMore2[i];   // --spdsameonce
    c.hitG2 = cb.hitG2;   // --repela0c
    // --a1clatch: the second body's +0xa1c is bit 1 (0 in both halves when the flag is off)
    c.a1cLatch = (uint8_t)((c.a1cLatch & 1u) | (cb.a1cLatch & 2u));
    // --ballscaleair: the second body's +0x728 is bits 2-3 (0 in both halves when the flag is off)
    c.ball728 = (uint8_t)((c.ball728 & 3u) | (cb.ball728 & 12u));
    {         // --rot2
        c.rot2 = cb.rot2;
        c.rotStep2 = cb.rotStep2;
        c.rotNeg2 = cb.rotNeg2;
    }
    c.holdDead2 = cb.holdDead2;
    { c.flipT2 = cb.flipT2; c.modeT2 = cb.modeT2; }   // --dualflipclock
    for (int i = 0; i < 3; ++i) c.portSeen2[i] = cb.portSeen2[i];
    for (int i = 0; i < 4; ++i) c.touchRing2[i] = cb.touchRing2[i];
    c.touchRingT2 = cb.touchRingT2;
    c.portalLatch2 = cb.portalLatch2;
    for (int i = 0; i < 4; ++i) c.usedPad2[i] = cb.usedPad2[i];
    if (s.dual && cb.dx != sb.dx) c.dx = cb.dx;
    if (g_dualBand && s.dual && p2WroteBand) {
        c.bandFloor = cb.bandFloor;
        c.bandCeil = cb.bandCeil;
        c.bandRefY = cb.bandRefY;
        c.bandBranch = cb.bandBranch;
        c.bandAnim = cb.bandAnim;
    }
    if (p2FlippedGravity && s.dual && !unlinked
        && sameModeFlags(g_dualBornPortal ? c.mode : s.mode, s.mode2)) {
        const uint8_t want = (uint8_t)!c.flip2;
        if (c.flip != want) {
            c.flip = want;
            c.vy = (float)((double)c.vy * 0.5);
        }
    }
    if (p1RingLate && s.dual && !unlinked && sameModeFlags(c.mode, c.mode2)) {
        const uint8_t want = (uint8_t)!c.flip;
        if (c.flip2 != want) {
            c.flip2 = want;
            if (g_rtlP2Ring && (p2RingFresh == 11 || p2RingFresh == 12 || p2RingFresh == 35
                                || p2RingFresh == 32))
                c.vy2 = -c.vy2;
            else c.vy2 = (float)((double)c.vy2 * 0.5);
        }
    }
    const bool p1IsToucher = c.mode != s.mode
                             && c.mode2 == s.mode2 && s.mode2 == c.mode;
    if (unlinked) {
        // --dualunlink: playerWillSwitchMode skips the re-mirror when the layer's +0x860 is up
    } else if (p1IsToucher) {
        const uint8_t want = c.flip2 ? 0 : 1;
        if (c.flip != want) {
            c.flip = want;
            c.vy *= 0.5f;
        }
    } else if (c.mode2 != s.mode2 && c.mode2 == c.mode) {
        const uint8_t want = c.flip ? 0 : 1;
        if (c.flip2 != want) {
            c.flip2 = want;
            c.vy2 *= 0.5f;
        }
    }
    if (s.dual && !d1 && !d2 && c.mode == 2 && c.mode2 == 2
        && (p1HitG || p2HitG)) {
        const uint8_t f1 = p1Tapped ? (uint8_t)!c.flip : c.flip;
        const uint8_t f2 = p2Tapped ? (uint8_t)!c.flip2 : c.flip2;
        const double halves = playerHalf(2, c.mini != 0) + playerHalf(2, c.mini2 != 0);
        if (f1 == f2 && std::fabs((double)c.y - (double)c.y2) < halves + 5.0) {
            if (p1HitG) {
                c.flip2 = (uint8_t)!f2;
                c.vy2 = c.flip2 ? 2.0f : -2.0f;
                c.grounded2 = 0;
                if (p2Tapped) {
                    c.pressSpent2 = sb.pressSpent;
                    c.holdDead2 = sb.holdDead;
                    c.hitG2 = p2HitG;
                }
            } else {
                c.flip = (uint8_t)!f1;
                c.vy = c.flip ? 2.0f : -2.0f;
                c.grounded = 0;
                if (p1Tapped) {
                    c.pressSpent = s.pressSpent;
                    c.holdDead = s.holdDead;
                    c.hitG = p1HitG;
                    c.jumpBuf = (uint8_t)(input ? 1 : 0);
                }
            }
        }
    }
    if (p2Solo && s.dual) {
        c.y = cb.y2;
        c.vy = cb.vy2;
        c.flip = cb.flip2;
        c.mode = cb.mode2;
        c.mini = cb.mini2;
        c.pressSpent = cb.pressSpent2;
        // --dualendrot (constants.hpp): ...but a wave takes the second body's angle, as it eased this tick
        if (g_dualEndRot && cb.mode2 == 4) c.rot = cb.rot2;
        c.dual = 0;
        c.onSlope = 0;
        c.slopeM = 0.f;
        c.snapObj = nullptr;
        c.snapDist = 0.f;
        // --p2xoff: ...the position includes x, which is the second body's own once they ran apart
        if (g_p2xOff && s.frame == 0 && s.dx2 != 0.f) c.xAbs = cb.xAbs;
    }
    if (g_p2xOff && !c.dual) c.dx2 = 0.f;   // --p2xoff: the dual ended, the offset with it
    // --ballscaleair: a ball whose gravity a partner call or the repel turned after its own step
    if (g_ballScaleAir && !p2Solo) {
        if (c.mode == 2 && c.flip != ballF1Own) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 1u);
        if (c.dual && c.mode2 == 2 && c.flip2 != ballF2Own)
            c.ball728 = (uint8_t)((c.ball728 & ~12u) | 4u);
    }
    c.freeHalf = 1;
    if (K.near) {
        for (const Obj* o : *K.near) {
            if (std::fabs((double)c.y2 - o->cy) < o->hh + 45.0) { c.freeHalf = 0; break; }
        }
    }
    if (c.freeHalf && K.slopes) {
        for (const Obj* sp : *K.slopes) {
            if (std::fabs((double)c.y2 - sp->cy) < sp->hh + 45.0) { c.freeHalf = 0; break; }
        }
    }
    dead = d1 || d2;
    markTouched(c, K, touchPreY(s, c, preBtnSet, preBtnY), s.action != 0,
                    input != 0 && s.action == 0, &s, input != 0);
    if (!g_fixups.empty()) applyFixup(s, input, c, dead);
    return c;
}
}  // namespace dp
