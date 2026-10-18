#pragma once
#include "dp/fixup.hpp"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace dp {
struct FrameSlices {
    Level* lv = nullptr;
    std::unique_ptr<XSlice> near, port, pad, orb, slope, speed;
    double lastLo = -1e18;
};

// (Re)build one frame's slices over that frame's level, cursors at the start.
inline void bindFrameSlices(FrameSlices& fs, Level& lv) {
    fs.lv = &lv;
    fs.near = std::make_unique<XSlice>(lv.objs);
    fs.port = std::make_unique<XSlice>(lv.portals);
    fs.pad = std::make_unique<XSlice>(lv.pads);
    fs.orb = std::make_unique<XSlice>(lv.orbs);
    fs.slope = std::make_unique<XSlice>(lv.slopes);
    fs.speed = std::make_unique<XSlice>(lv.speeds);
}

struct ModelWorld {
    Level& L;
    const gdapprox::ShipParams* SP;
    const gdapprox::ShipParams* SPmini;
    const gdapprox::UfoParams* UP;
    const gdapprox::UfoParams* UPmini;
    bool coinOn;
    const std::vector<TouchMask>* coinEnable;
    const std::vector<uint8_t>* coinAnyFrame;
    std::array<FrameSlices, 4> fsl;
    SlopeVetoIndex vetoIdx;       // rebuilt per group, storage reused
    using GatesFn = void (*)(void* ctx, State& s, uint8_t prevAction, int input, const StepCtx& K,
                             int gframe, double px, double py, double ph, long long t);
    GatesFn gates = nullptr;
    void* gatesCtx = nullptr;
    // The counting tap's bit and the boxes of the gate it feeds (Model::pressMatters).
    TouchMask tapBit{}, tapGateBits{};

    FrameSlices& slicesFor(int f, double wLo) {
        FrameSlices& fs = fsl[(size_t)(f & 3)];
        if (!fs.lv) {
            bindFrameSlices(fs, frameLevel(L, f));
        } else if (wLo + 1e-6 < fs.lastLo) {
            fs.near->seekTo(wLo - 40);
            fs.port->seekTo(wLo - 60);
            fs.pad->seekTo(wLo - 40);
            fs.orb->seekTo(wLo - 50);
            fs.slope->seekTo(wLo - 60);
            fs.speed->seekTo(wLo - 80);
        }
        fs.lastLo = wLo;
        return fs;
    }
};

struct Progress {
    double x;
    int frame;
    int rev;
};
inline Progress progressOf(const State& s) { return Progress{(double)s.xAbs, (int)s.frame, (int)s.rev}; }

struct GroupKey {
    float dx;
    TouchMask trig;
    uint8_t frame;
    uint8_t rev;
    uint64_t fsig;
    bool operator==(const GroupKey& o) const {
        return dx == o.dx && trig == o.trig && frame == o.frame && rev == o.rev && fsig == o.fsig;
    }
};
inline GroupKey groupKeyOf(const State& s, float fallbackDx, uint64_t fsig) {
    return GroupKey{(s.dx > 0.f) ? s.dx : fallbackDx, s.trig, s.frame, s.rev, fsig};
}
inline bool inGroup(const GroupKey& k, const State& s, float fallbackDx) {
    return ((s.dx > 0.f) ? s.dx : fallbackDx) == k.dx && s.trig == k.trig && s.frame == k.frame
           && s.rev == k.rev;
}

inline float p2Dx(const State& s) { return (g_p2xOff && s.dual && s.frame == 0) ? s.dx2 : 0.f; }
inline void p2Widen(double& wLo, double& wHi, float dLo, float dHi) {
    if (dLo < 0.f) wLo += (double)dLo;
    if (dHi > 0.f) wHi += (double)dHi;
}

struct GroupOpts {
    bool coins = true;     // --coins: the coins where they are now (only while ModelWorld::coinOn)
    bool veto = true;      // the group's SlopeVetoIndex over near and the slopes
    bool trigDbg = true;   // --trigdbg's per-group line
};

struct GroupEnv {
    double x = 0.0, xPrev = 0.0;
    float dxF = 0.f;
    long long t = 0;
    std::vector<const Obj*> near, ports, pads, orbs, slps, spds;
    std::vector<std::pair<const TouchTrig*, TouchMask>> trigs;
    std::vector<CoinLive> coinLive;   // --coins, filled below the windows
    std::vector<const Obj*> nearSolid;   // --solidorder: near in GD's solid order, once per group
    std::vector<FollowRef> fol;          // --followy: the follow groups' objects in the windows
    const ModelWorld* w = nullptr;
    GroupOpts opts;

    StepCtx ctx() const {
        StepCtx c{x, xPrev, dxF, t, &near, &ports, &pads, &orbs, &slps, &spds,
                  w->SP, w->SPmini, w->UP, w->UPmini, &trigs,
                  opts.coins ? &coinLive : nullptr, opts.veto ? &w->vetoIdx : nullptr,
                  &nearSolid};
        c.fol = fol.empty() ? nullptr : &fol;
        return c;
    }
};

inline void prepareGroup(ModelWorld& w, FrameSlices& FS, GroupEnv& e, int gframe,
                         const TouchMask& gtrig, int gFire, const uint16_t* gFireB, float lockAt,
                         double x, double xPrev, float dxUsed, double wLo, double wHi,
                         long long t, const GroupOpts& opts = GroupOpts{}) {
    Level& L = w.L;
    e.w = &w;
    e.opts = opts;
    e.x = x;
    e.xPrev = xPrev;
    e.dxF = dxUsed;
    e.t = t;
    Level& LG = *FS.lv;
    LG.dyn.seek((int)t);
    seekOtherFrames(&LG, (int)t);   // --rotpretap only; serial here, before the workers
    LG.dyn.areaPxPrev = xPrev;   // --areamove: GD's area pass reads P1 before the step
    LG.dyn.applyTriggers(gtrig, gFire, gFireB, lockAt, (int)t, x);
    std::vector<const Obj*>& near = e.near;
    FS.near->forRange(wLo - 40, wHi + 40, [&](const Obj& o) { near.push_back(&o); });
    LG.dyn.collect(Dynamics::NEAR, wLo - 40, wHi + 40, near);
    if (opts.trigDbg && g_trigDbgT >= 0 && (long long)t == g_trigDbgT) {
        const Obj* seen = nullptr;
        if (g_hazDbgUid >= 0)
            for (const Obj* o : near)
                if (o->uid == g_hazDbgUid) { seen = o; break; }
        std::printf("trigdbg t=%lld gtrig=0x%08x gFire=%d lockOff=%.3f "
                    "win=[%.1f,%.1f] n=%zu uid=%d %s gFireB=",
                    (long long)t, (unsigned)gtrig.word(0), gFire,
                    (double)lockAt, wLo, wHi, near.size(),
                    g_hazDbgUid,
                    seen ? "" : "NOT-IN-WINDOW");
        for (int b = 0; b < kTouchBits; ++b)
            if (gtrig.test(b)) std::printf("%d:%u,", b, (unsigned)gFireB[b]);
        std::printf("\n");
        if (seen)
            std::printf("trigdbg t=%lld uid=%d at (%.3f,%.3f) hw=%.3f hh=%.3f\n",
                        (long long)t, seen->uid, seen->cx, seen->cy,
                        seen->hw, seen->hh);
    }
    FS.port->forRange(wLo - 60, wHi + 60, [&](const Obj& o) { e.ports.push_back(&o); });
    LG.dyn.collect(Dynamics::PORT, wLo - 60, wHi + 60, e.ports);
    // --portorder (constants.hpp): the collision pass's order, cell by cell and by uid in a cell
    if (g_portOrder)
        std::stable_sort(e.ports.begin(), e.ports.end(), [](const Obj* a, const Obj* b) {
            const int ax = gdSectionCell(a->cx), bx = gdSectionCell(b->cx);
            if (ax != bx) return ax < bx;
            const int ay = g_dynamicLevelHeight ? gdSectionCell(a->cy) : 0;
            const int by = g_dynamicLevelHeight ? gdSectionCell(b->cy) : 0;
            if (ay != by) return ay < by;
            return a->uid < b->uid;
        });
    FS.pad->forRange(wLo - 40, wHi + 40, [&](const Obj& o) { e.pads.push_back(&o); });
    LG.dyn.collect(Dynamics::PAD, wLo - 40, wHi + 40, e.pads);
    FS.orb->forRange(wLo - 50, wHi + 50, [&](const Obj& o) { e.orbs.push_back(&o); });
    LG.dyn.collect(Dynamics::ORB, wLo - 50, wHi + 50, e.orbs);
    FS.slope->forRange(wLo - 60, wHi + 60, [&](const Obj& o) { e.slps.push_back(&o); });
    LG.dyn.collect(Dynamics::SLOPE, wLo - 60, wHi + 60, e.slps);
    FS.speed->forRange(wLo - 80, wHi + 80, [&](const Obj& o) { e.spds.push_back(&o); });
    LG.dyn.collect(Dynamics::SPEED, wLo - 80, wHi + 80, e.spds);
    const std::vector<TouchTrig>& tfg = touchFor(gframe);
    for (size_t b = 0; b < tfg.size(); ++b) {
        const TouchTrig& T = tfg[b];
        if (gtrig & touchBit(b)) continue;   // already fired
        const bool standing =
            ((T.id == 1611 || T.id == 3620) && T.count >= 0 && !T.tap) || (g_tapDisarm && T.tap);
        const bool anywhere = T.id == 3620 && T.count >= 0;
        if (!anywhere && !standing && T.cx + T.hw < wLo - 40) continue;
        if (!anywhere && T.cx - T.hw > wHi + 40) continue;
        e.trigs.push_back({&T, touchBit(b)});
    }
    std::vector<CoinLive>& coinLive = e.coinLive;
    const std::vector<TouchMask>& coinEnable = *w.coinEnable;
    const std::vector<uint8_t>& coinAnyFrame = *w.coinAnyFrame;
    const bool coinOn = opts.coins && w.coinOn;
    if (coinOn && gframe != 0)
        for (size_t ci = 0; ci < L.coins.size(); ++ci) {
            if (!coinAnyFrame[ci] || !(gtrig & coinEnable[ci])) continue;
            const Obj& c = L.coins[ci];
            double u = 0.0, v = 0.0;
            toFrame(gframe, c.cx, c.cy, u, v);
            const bool odd = (gframe & 1) != 0;
            coinLive.push_back({u, v, odd ? c.hh : c.hw, odd ? c.hw : c.hh, (int)ci});
        }
    if (coinOn && gframe == 0) {
        for (size_t ci = 0; ci < L.coins.size(); ++ci) {
            const int di = ci < L.coinDyn.size() ? L.coinDyn[ci] : -1;
            if (coinEnable[ci]) {
                if (gtrig & coinEnable[ci]) {
                    const Obj& c = di < 0 ? L.coins[ci] : LG.dyn.objs[(size_t)di];
                    coinLive.push_back({c.cx, c.cy, c.hw, c.hh, (int)ci});
                }
                continue;
            }
            if (di < 0) {
                const Obj& c = L.coins[ci];
                coinLive.push_back({c.cx, c.cy, c.hw, c.hh, (int)ci});
            } else if (LG.dyn.on[(size_t)di]) {
                const Obj& c = LG.dyn.objs[(size_t)di];
                coinLive.push_back({c.cx, c.cy, c.hw, c.hh, (int)ci});
            }
        }
        if (g_coinDbgT >= 0 && (long long)t == g_coinDbgT) {
            for (const CoinLive& C : coinLive)
                std::printf("coindbg: t=%lld gtrig=0x%08llx coin=%d live=(%.3f,%.3f)"
                            " box %.1fx%.1f load=(%.3f,%.3f)\n",
                            (long long)t, (unsigned long long)gtrig.word(0),
                            C.bit, C.cx, C.cy,
                            C.hw * 2, C.hh * 2,
                            L.coins[(size_t)C.bit].cx, L.coins[(size_t)C.bit].cy);
            for (size_t ci = 0; ci < L.coins.size(); ++ci) {
                bool live = false;
                for (const CoinLive& C : coinLive)
                    if ((size_t)C.bit == ci) live = true;
                if (!live)
                    std::printf("coindbg: t=%lld gtrig=0x%08llx coin=%zu OFF"
                                " (its group is switched off)\n",
                                (long long)t, (unsigned long long)gtrig.word(0), ci);
            }
        }
    }
    e.fol.clear();
    const bool folOn = g_followYOn && (!g_followYs.empty() || !g_followSecObjs.empty());
    if (folOn || !g_bigSecObjs.empty() || !g_bigSecDyn.empty()) {
        if (folOn)
            for (FollowY& f : g_followYs)
                if (xPrev < f.cx && f.cx <= x) f.tAct = t;
        const std::vector<const Obj*>* wins[6] = {&near, &e.ports, &e.pads, &e.orbs, &e.slps,
                                                  &e.spds};
        for (uint8_t wi = 0; wi < 6; ++wi)
            for (uint32_t i = 0; i < (uint32_t)wins[wi]->size(); ++i) {
                const int u = (*wins[wi])[i]->uid;
                if (folOn) {
                    for (uint16_t c = 0; c < (uint16_t)g_followYs.size(); ++c)
                        for (const auto& m : g_followYs[c].objs)
                            if (m.first == u) e.fol.push_back({wi, i, c, m.second});
                    for (const auto& m : g_followSecObjs)   // --followsection: placed by nobody
                        if (m.first == u) e.fol.push_back({wi, i, kFollowSecCmd, m.second});
                }
                for (const auto& m : g_bigSecObjs)   // --bigsection: placed by nobody either
                    if (m.first == u) e.fol.push_back({wi, i, kBigSecCmd, m.second});
                for (const auto& m : g_bigSecDyn)    // ...and the grouped ones
                    if (m.first == u) e.fol.push_back({wi, i, kBigSecDynCmd, m.second});
            }
    }
    if (opts.veto) w.vetoIdx.build(near, e.slps);
    solidOrderInto(near, e.nearSolid);
}

inline void fireRotTriggers(int gframe, bool grev, double gLo, double gHi, double wLo,
                            double wHi, long long t) {
    for (RotTrig& r : g_rotTrig) {
        const bool same = (r.frame == gframe);
        if ((same ? r.revT : r.firedT) >= 0) continue;
        const double ru = frameU(gframe, r.cx, r.cy);
        if (grev ? (wLo <= ru && ru < gHi) : (gLo < ru && wHi >= ru)) {
            if (same) r.revT = (int)t; else r.firedT = (int)t;
        }
    }
}

struct RotNote {
    bool set = false;
    int uid = -1;
    int8_t f0 = -1, nf = -1;
};

inline State advanceState(const State& s, int input, const StepCtx& K, bool& dead, RotNote* rot) {
    State c = stepBoth(s, input, K, dead);
    if (!dead && !g_rotTrig.empty()) {
        const bool rw = rot && g_rotWatchLo >= 0 && K.t >= g_rotWatchLo
                        && K.t <= g_rotWatchHi;
        if (rw) g_rotWatchUid = -1;
        const int f0R = (int)c.frame;
        const int nfR = applyRotation(c, (double)s.xAbs, (double)K.dxF, K.t,
                                      input, s.grounded != 0, (double)s.y, true);
        if (rw && nfR >= 0) {   // --rotwatch: print only, read in the serial pass
            rot->set = true;
            rot->uid = g_rotWatchUid;
            rot->f0 = (int8_t)f0R;
            rot->nf = (int8_t)nfR;
        }
    }
    return c;
}

template <class Pre, class Post>
inline State rollout(ModelWorld& w, const State& start, long long t0,
                     const std::vector<uint8_t>& lvl, float fallbackDx, Pre&& pre, Post&& post,
                     int bindFrame = -1) {
    FrameSlices fs;
    bindFrameSlices(fs, frameLevel(w.L, bindFrame >= 0 ? bindFrame : (int)start.frame));
    State s = start;
    for (size_t i = 0; i < lvl.size(); ++i) {
        const long long t = t0 + (long long)i + 1;
        // Under reverse (State::rev) x decreases, with the same sign as the search's windows.
        const float dxUsed = ((s.dx > 0.f) ? s.dx : fallbackDx) * (s.rev ? -1.f : 1.f);
        const double xPrev = (double)s.xAbs;
        const double x = (double)advanceX(s.xAbs, dxUsed);
        if (g_rolloutFiresAuto)
            for (AutoTrig& A : g_autoTrig) {
                if (A.fireT >= 0) continue;
                if (x < A.cx) break;   // sorted by cx
                A.fireT = (int)t + A.delay;
                A.fireX = A.lockLastX = x;
                std::printf("witnessauto: uid %d x=%.0f crossed at t=%lld, effect t=%d\n", A.uid,
                            A.cx, t, A.fireT);
            }
        GroupEnv env;
        double pLo = x, pHi = x;
        p2Widen(pLo, pHi, p2Dx(s), p2Dx(s));   // --p2xoff
        prepareGroup(w, fs, env, (int)s.frame, s.trig, (int)s.trigT, s.fireB,
                     s.lockOff + (float)(x - (double)s.xAbs), x, xPrev, dxUsed, pLo, pHi, t,
                     GroupOpts{false, false, false});
        const StepCtx K = env.ctx();
        bool dead = false;
        const int frame0 = (int)s.frame;
        State c = advanceState(s, (int)lvl[i], K, dead, nullptr);
        if (!dead && !g_rotTrig.empty() && (int)c.frame != frame0)
            bindFrameSlices(fs, frameLevel(w.L, (int)c.frame));
        pre(i, t, (const State&)s, c, dead, K);
        c.action = (uint8_t)lvl[i];
        s = c;
        post(i, t, (const State&)s, dead);
    }
    return s;
}

inline bool autoTrigMoves(const AutoTrig& A) {
    for (const TrigCtl& c : A.ctl)
        if (std::fabs(c.dx) > 0.001f || std::fabs(c.dy) > 0.001f || c.lockTicks > 0.0
            || c.tmode != 0 || c.orbit)
            return true;
    return false;
}
inline void autoSpreadClose(const std::vector<State>& frontier, long long t) {
    for (size_t k = 0; k < g_autoSpreadOpen.size();) {
        const AutoSpreadOpen& o = g_autoSpreadOpen[k];
        const AutoTrig& A = g_autoTrig[o.i];
        size_t behind = 0;
        for (const State& s : frontier)
            if (progressOf(s).x < A.cx) ++behind;
        if (behind) { ++k; continue; }
        std::printf("autospread: uid=%d cx=%.0f moves=%d lead=%lld last=%lld spread=%lld "
                    "behind=%zu of=%zu\n", A.uid, A.cx, autoTrigMoves(A) ? 1 : 0, o.leadT, t - 1,
                    t - 1 - o.leadT, o.behind, o.of);
        g_autoSpreadOpen.erase(g_autoSpreadOpen.begin() + (std::ptrdiff_t)k);
    }
}
inline void fireAutoTriggers(const std::vector<State>& frontier, long long t) {
    if (g_autoTrig.empty()) return;
    if (g_autoSpread && !g_autoSpreadOpen.empty()) autoSpreadClose(frontier, t);
    double xLead = -1e18;
    bool haveLead = false;
    for (AutoTrig& A : g_autoTrig) {
        if (A.fireT >= 0) continue;
        if (!haveLead) {
            for (const State& s : frontier)
                if (progressOf(s).x > xLead) xLead = progressOf(s).x;
            haveLead = true;
        }
        if (xLead < A.cx) break;
        A.fireT = (int)(t - 1) + A.delay;
        A.fireX = A.lockLastX = xLead;
        // --autoown: the most ticks a state still behind it needs to reach it at its own speed (dynamics.hpp).
        A.fireSpreadT = 0;
        if (g_autoOwn)
            for (const State& s : frontier) {
                if ((int)s.frame != 0 || s.rev) continue;
                const double sx = progressOf(s).x;
                if (sx < A.cx && s.dx > 0.f)
                    A.fireSpreadT = std::max(A.fireSpreadT, (int)std::ceil((A.cx - sx) / (double)s.dx) + 1);
            }
        std::printf("autotrig: uid %d x=%.0f crossed at t=%lld, "
                    "effect t=%d\n",
                    A.uid, A.cx, t - 1, A.fireT);
        if (g_autoSpread) {
            size_t behind = 0;
            for (const State& s : frontier)
                if (progressOf(s).x < A.cx) ++behind;
            if (behind)
                g_autoSpreadOpen.push_back({(size_t)(&A - g_autoTrig.data()), t - 1, behind,
                                            frontier.size()});
        }
    }
}

inline bool slotTieBreak() { return g_fewPress || g_roomy; }
inline uint32_t slotScore(const State& s) { return g_fewPress ? s.inputs : s.tight; }
// the button's level the human limits count edges from (State::heldSeed).
inline int heldLevel(const State& s) { return (int)(s.action | s.heldSeed); }
inline bool edgeLocked(const State& s, int gridNow, long long t) {
    return (g_minPulse > 1 && t >= g_humanFrom && (int)s.edgeAge + 1 < g_minPulse) ||   // humanFrom
           (gridNow > 1 && (t % gridNow) != 0);
}
inline bool inputAllowed(const State& s, int input, int gridNow, long long t) {
    if (edgeLocked(s, gridNow, t) && input != heldLevel(s)) return false;   // heldLevel
    if (s.latLock && input != (int)s.action) return false;
    // --minpressgap, no press within g_minPressGap ticks of the last one.
    if (g_minPressGap > 1 && t >= g_humanFrom && input == 1 && heldLevel(s) == 0 && (int)s.pressAge + 1 < g_minPressGap)
        return false;
    return true;
}
inline void noteInput(const State& prev, State& c, int input) {
    c.action = (uint8_t)input;
    c.heldSeed = 0;
    if (g_minPulse > 1)
        c.edgeAge = (input != heldLevel(prev)) ? 0 : (uint8_t)std::min(254, (int)prev.edgeAge + 1);
    if (g_minPressGap > 1)   // --minpressgap
        c.pressAge = (input == 1 && heldLevel(prev) != 1) ? 0 : (uint8_t)std::min(254, (int)prev.pressAge + 1);
    if (g_fewPress)   // --fewpress
        c.inputs = (uint16_t)std::min(0xffff, (int)prev.inputs + (input != (int)prev.action ? 1 : 0));
    const auto lat2 = [](uint8_t m) { return m == 1 || m == 3; };
    c.latLock = (lat2(c.mode) && !lat2(prev.mode) && !c.dual) ? 1 : 0;
}

struct Model {
    ModelWorld& w;

    // prepareGroup, with the group's frame slices from the world's cache.
    void prepare(GroupEnv& e, int gframe, const TouchMask& gtrig, int gFire, const uint16_t* gFireB,
                 float lockAt, double x, double xPrev, float dxUsed, double wLo, double wHi,
                 long long t, const GroupOpts& opts = GroupOpts{}) const {
        prepareGroup(w, w.slicesFor(gframe, wLo), e, gframe, gtrig, gFire, gFireB, lockAt, x,
                     xPrev, dxUsed, wLo, wHi, t, opts);
    }
    void forgetWorld(int f) const { levelOf(f).dyn.forgetPlacement(); }
    void saveWorld(int f, std::vector<double>& carry) const { levelOf(f).dyn.saveCarry(carry); }
    // --ladderprefix: frame f's level exists already (levelOf binds it lazily otherwise).
    bool frameBound(int f) const { return w.fsl[(size_t)(f & 3)].lv != nullptr; }
    static void objLists(const Level& lv, const std::vector<Obj>* out[8]) {
        out[0] = &lv.objs; out[1] = &lv.portals; out[2] = &lv.pads; out[3] = &lv.orbs;
        out[4] = &lv.speeds; out[5] = &lv.slopes; out[6] = &lv.coins; out[7] = &lv.dyn.objs;
    }
    uint64_t encodeObj(const Obj* p, bool& ok) const {
        if (!p) return 0;
        for (int f = 0; f < 4; ++f) {
            if (!frameBound(f)) continue;
            const std::vector<Obj>* ls[8];
            objLists(*w.fsl[(size_t)f].lv, ls);
            for (int k = 0; k < 8; ++k) {
                const std::vector<Obj>& v = *ls[k];
                if (!v.empty() && p >= v.data() && p < v.data() + v.size())
                    return 1 + ((uint64_t)f << 60) + ((uint64_t)k << 56) + (uint64_t)(p - v.data());
            }
        }
        ok = false;
        return 0;
    }
    const Obj* decodeObj(uint64_t c) const {
        if (!c) return nullptr;
        c -= 1;
        const int f = (int)((c >> 60) & 3), k = (int)((c >> 56) & 15);
        const size_t i = (size_t)(c & ((1ull << 56) - 1));
        const std::vector<Obj>* ls[8];
        objLists(levelOf(f), ls);
        return &(*ls[k])[i];
    }
    void loadWorld(int f, const std::vector<double>& carry, long long tPrev) const {
        levelOf(f).dyn.loadCarry(carry, (int)tPrev);
    }
    Level& levelOf(int f) const {
        FrameSlices& fs = w.fsl[(size_t)(f & 3)];
        if (!fs.lv) bindFrameSlices(fs, frameLevel(w.L, f));
        return *fs.lv;
    }
    State advance(const State& s, int input, const StepCtx& K, bool& dead, RotNote* rot) const {
        return advanceState(s, input, K, dead, rot);
    }
    void fireGates(State& s, uint8_t prevAction, int input, const StepCtx& K, int gframe,
                   long long t, bool dead) const {
        if (!w.gates || dead || (int)s.frame != gframe) return;
        w.gates(w.gatesCtx, s, prevAction, input, K, gframe, (double)s.xAbs, (double)s.y,
                hazardHalfFor(s.mode, s.mini != 0), t);
    }
    bool hasGates() const { return w.gates != nullptr; }
    bool pressMatters(const State& s) const {
        return w.tapBit && !(s.trig & w.tapGateBits) && !(s.taps & 0x40);
    }
    void fireRotTriggers(int gframe, bool grev, double gLo, double gHi, double wLo, double wHi,
                         long long t) const {
        dp::fireRotTriggers(gframe, grev, gLo, gHi, wLo, wHi, t);
    }
    GroupKey groupKey(const State& s, float fallbackDx, uint64_t fsig) const {
        return groupKeyOf(s, fallbackDx, fsig);
    }
    bool inGroup(const GroupKey& k, const State& s, float fallbackDx) const {
        return dp::inGroup(k, s, fallbackDx);
    }
    Progress progress(const State& s) const { return progressOf(s); }
    void fireAutoTriggers(const std::vector<State>& frontier, long long t) const {
        dp::fireAutoTriggers(frontier, t);
    }
    bool edgeLocked(const State& s, int gridNow, long long t) const {
        return dp::edgeLocked(s, gridNow, t);
    }
    bool inputAllowed(const State& s, int input, int gridNow, long long t) const {
        return dp::inputAllowed(s, input, gridNow, t);
    }
    void noteInput(const State& prev, State& c, int input) const { dp::noteInput(prev, c, input); }
    template <class Pre, class Post>
    State rollout(const State& start, long long t0, const std::vector<uint8_t>& lvl,
                  float fallbackDx, Pre&& pre, Post&& post, int bindFrame = -1) const {
        return dp::rollout(w, start, t0, lvl, fallbackDx, pre, post, bindFrame);
    }
};
}  // namespace dp
