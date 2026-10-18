#pragma once
#include "dp/reset.hpp"
#include "dp/clearance.hpp"
#include "dp/refwatch.hpp"
#include "dp/search_census.hpp"
#include "dp/model.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace dp {
// --coins: a counter a coin needs (see where coinReq is filled in cliMainOnce).
struct CoinCountReq {
    int item;      // which counter
    int need;      // ...has to reach this
    int coinIdx;   // the coin it decides (skip once that one is collected)
};

// --coins: the touch bits that, once fired, put a coin out of reach (see coinGate in cliMainOnce).
struct CoinOnGate {
    int coinIdx;
    std::vector<std::pair<TouchMask, double>> ons;  // bit, x past which it is gone
};

struct Slots { int hi = -1; int lo = -1; };
struct CellMap {
    std::vector<uint64_t> keys;
    std::vector<Slots> vals;
    std::vector<uint32_t> touched;
    size_t mask = 0;
    void ensure(size_t want) {
        size_t n = 1024;
        while (n < want * 4) n <<= 1;   // keep the load factor under 1/4
        if (n <= keys.size()) return;
        keys.assign(n, 0);
        vals.assign(n, Slots{});
        mask = n - 1;
    }
    void clear() {
        for (uint32_t i : touched) keys[i] = 0;
        touched.clear();
    }
    // same names the memory report used when this was an unordered_map
    size_t size() const { return touched.size(); }
    size_t bucket_count() const { return keys.size(); }
    Slots& at(uint64_t k) {
        size_t i = (size_t)(k * 0x9E3779B97F4A7C15ull) & mask;
        for (;;) {
            if (keys[i] == k) return vals[i];
            if (keys[i] == 0) {
                keys[i] = k;
                vals[i] = Slots{};
                touched.push_back((uint32_t)i);
                return vals[i];
            }
            i = (i + 1) & mask;
        }
    }
};

struct KeyRec {
    uint64_t key;
    uint32_t aOrd;          // ordinal that allocated the key's first slot
    uint32_t bOrd;          // ordinal of the first hi/lo split (kNone: none)
    uint32_t hiIdx, loIdx;  // child index of the current hi / lo rep
    float hiVy, loVy;
    uint8_t bWasHi;
};
struct ShardMap {           // CellMap's open addressing, key -> rec index
    std::vector<uint64_t> keys;
    std::vector<uint32_t> vals;
    std::vector<uint32_t> touched;
    std::vector<KeyRec> recs;
    uint32_t goalOrd = kNone;
    uint32_t goalTight = 0xffffffffu;   // route score (Kairos: routeScore, 32 bits); see the goal pick below
    size_t mask = 0;
    void ensure(size_t want) {
        size_t n = 1024;
        while (n < want * 4) n <<= 1;
        if (n <= keys.size()) return;
        keys.assign(n, 0);
        vals.assign(n, kNone);
        mask = n - 1;
    }
    void clear() {
        for (uint32_t i : touched) keys[i] = 0;
        touched.clear();
        recs.clear();
        goalOrd = kNone;
        goalTight = 0xffffffffu;
    }
    void grow() {
        const size_t n = keys.size() ? keys.size() * 2 : 1024;
        std::vector<uint64_t> ok;
        ok.swap(keys);
        std::vector<uint32_t> ov;
        ov.swap(vals);
        keys.assign(n, 0);
        vals.assign(n, (uint32_t)kNone);
        mask = n - 1;
        touched.clear();
        for (size_t i = 0; i < ok.size(); ++i) {
            if (!ok[i]) continue;
            size_t j = (size_t)(ok[i] * 0x9E3779B97F4A7C15ull) & mask;
            while (keys[j]) j = (j + 1) & mask;
            keys[j] = ok[i];
            vals[j] = ov[i];
            touched.push_back((uint32_t)j);
        }
    }
    uint32_t& at(uint64_t k) {
        if ((touched.size() + 1) * 4 > keys.size()) grow();
        size_t i = (size_t)(k * 0x9E3779B97F4A7C15ull) & mask;
        for (;;) {
            if (keys[i] == k) return vals[i];
            if (keys[i] == 0) {
                keys[i] = k;
                vals[i] = kNone;
                touched.push_back((uint32_t)i);
                return vals[i];
            }
            i = (i + 1) & mask;
        }
    }
};

struct Child {
    State s{};
    uint64_t key = 0;         // modelKeyOf(s) ^ rules.keyBits(s), computed on the worker thread
    uint8_t dead = 0;
    uint8_t valid = 0;
    const char* why = "";     // --dbg only
    const Obj* obj = nullptr; // --dbg only
    int rotUid = -1;          // --rotwatch only: the 2900 stepKid applied
    int8_t rotF0 = -1, rotNf = -1;
};

struct PlanEdge {
    long long press;   // the tick the button changes state on
    int level;         // ...and to what
};

inline std::vector<PlanEdge> planEdges(const std::vector<uint8_t>& lvl,
                                       const std::vector<uint8_t>& modeAt, long long t0,
                                       int prevHeld, uint8_t initMode) {
    std::vector<PlanEdge> out;
    int prev = prevHeld;
    for (size_t i = 0; i < lvl.size(); ++i) {
        if (lvl[i] == prev) continue;
        const long long effect = t0 + (long long)i + 1;
        auto latOf = [](uint8_t m) { return (m == 1 || m == 3) ? 2 : 1; };
        auto modeAtTick = [&](long long j) -> uint8_t {
            if (j < 0) return initMode;
            if ((size_t)j >= modeAt.size()) return modeAt.empty() ? initMode : modeAt.back();
            return modeAt[(size_t)j];
        };
        const int lat0 = latOf(modeAtTick((long long)i - 1));
        const int lat = latOf(modeAtTick((long long)i - lat0));
        out.push_back(PlanEdge{effect - lat, (int)lvl[i]});
        prev = lvl[i];
    }
    return out;
}

inline std::atomic<long long> g_ladderBestT{-1};
inline std::atomic<bool> g_ladderStop{false};

template <class C>
struct CurrentRules {
    double goalX;      // the level's end (cliMainOnce: L.maxX + 60)
    bool coinOn;       // --coins, on a level whose coins fit the mask
    uint32_t coinAll;  // ...and the mask of all of them
    const C& c;

    bool reached(const State& s) const {
        return s.frame == 0 && (double)s.xAbs >= goalX
               && (!coinOn || s.coins == (uint8_t)coinAll);
    }
    uint32_t routeScore(const State& s) const {
        return g_fewPress ? ((uint32_t)s.inputs << 16 | s.tight) : (uint32_t)s.tight;
    }
    bool prefer(uint32_t cand, uint32_t held) const { return cand < held; }
    double score(const State& s) const {
        return (coinOn ? 1e7 * popCount32((uint32_t)s.coins) : 0.0) + progressOf(s).x;
    }
    const State& pick(const std::vector<State>& v) const {
        size_t best = 0;
        if (coinOn) {
            int most = -1;
            for (size_t i = 0; i < v.size(); ++i) {
                const int c = popCount32((uint32_t)v[i].coins);
                if (c > most) { most = c; best = i; }
            }
        }
        return v[best];
    }

    bool bandClass() const { return coinOn; }
    uint64_t winCell(const State& s) const {
        auto& L = c.L;
        auto& coinPruneOk = c.coinPruneOk;
        if (!coinOn || g_coinWinY <= 0.0 || s.frame != 0) return 0;
        for (size_t ci = 0; ci < L.coins.size() && ci < 8; ++ci) {
            if (!coinPruneOk[ci] || ((s.coins >> ci) & 1u)) continue;
            const double cx = L.coins[ci].cx;
            if ((double)s.xAbs < cx - g_coinWinLen || (double)s.xAbs > cx + L.coins[ci].hw)
                continue;
            const int64_t yb = (int64_t)std::floor((double)s.y / g_coinWinY);
            const int64_t vb = (int64_t)std::floor((double)s.vy / g_coinWinVy);
            return (((uint64_t)(ci + 1) << 40) ^ ((uint64_t)(yb & 0xfffff) << 16)
                    ^ (uint64_t)(vb & 0xffff)) * 0xBF58476D1CE4E5B9ull;
        }
        return 0;
    }
    bool rejoinOk(const State* rjS) const {
        auto& tapGive = c.tapGive;
        auto& tapGateBits = c.tapGateBits;
        auto& gatedMask = c.gatedMask;
        const bool rjWindow = rjS && (rjS->taps & 0x80) && !(rjS->taps & 0x40)
                              && !(rjS->trig & tapGateBits);
        const bool rjOwed = rjS && gatedMask && (rjS->trig & tapGateBits)
                            && (rjS->coins & gatedMask) != gatedMask;
        return !coinOn || !tapGive.bit || !(rjWindow || rjOwed);
    }

    uint64_t keyBits(const State& s) const { return coinKeyBits(s); }
    uint64_t classBits(const State& s) const { return (uint64_t)s.coins << 40; }
    void init(State& s) const {
        if (!coinOn) return;
        s.coins = (uint8_t)((uint32_t)g_coinMaskSeed & coinAll);
        if (s.coins)
            std::printf("coins: starting with 0x%x already collected\n", (unsigned)s.coins);
        const uint8_t skip = (uint8_t)((uint32_t)g_coinSkip & coinAll);
        if (skip) {
            s.coins |= skip;
            std::printf("coins: 0x%x not a goal this run (--coinskip)\n", (unsigned)skip);
        }
    }
    bool needsAfterStep() const { return coinOn; }

    void afterStep(const State& s, Child& kid, int input, const StepCtx& K, int gframe, long long t,
                   bool& dead) const {
        static const std::vector<CoinLive> kNoCoins;
        const std::vector<CoinLive>& coinLive = K.coins ? *K.coins : kNoCoins;
        auto& L = c.L;
        auto& passedOn = c.passedOn;
        auto& tapGive = c.tapGive;
        auto& tapGateBits = c.tapGateBits;
        auto& gatedMask = c.gatedMask;
        auto& tapMissX = c.tapMissX;
        auto& tapMissY = c.tapMissY;
        auto& coinGate = c.coinGate;
        auto& tapSole = c.tapSole;
        auto& coinEnable = c.coinEnable;
        auto& coinReq = c.coinReq;
        auto& coinPruneOk = c.coinPruneOk;
        if (coinOn && !dead && kid.s.frame == gframe) {
            const double ph =
                hazardHalfFor(kid.s.mode, kid.s.mini != 0);
            const double px = (double)kid.s.xAbs, py = (double)kid.s.y;
            for (const CoinLive& lc : coinLive) {
                const uint8_t bit = (uint8_t)(1u << lc.bit);
                if (kid.s.coins & bit) continue;
                if (g_coinDbgT >= 0 && gframe == 0 && px > g_coinProbeMaxX)
                    g_coinProbeMaxX = px;
                if (g_coinDbgT >= 0 && gframe == 0
                    && std::fabs(px - lc.cx) <= lc.hw + ph) {
                    ++g_coinSeen[lc.bit];
                    const double dy = std::fabs(py - lc.cy);
                    double& best = g_coinNearDy[lc.bit];
                    if (best < 0.0 || dy < best) {
                        best = dy;
                        g_coinNearX[lc.bit] = px;
                        g_coinNearY[lc.bit] = py;
                        g_coinNearT[lc.bit] = (long long)t;
                    }
                }
                const double mx = std::min(kCoinMargin, (lc.hw + ph) * 0.25);
                const double my = std::min(kCoinMargin, (lc.hh + ph) * 0.25);
                if (std::fabs(px - lc.cx) <= lc.hw + ph - mx
                    && std::fabs(py - lc.cy) <= lc.hh + ph - my) {
                    if (g_coinDbgT >= 0 && !g_coinSaid[lc.bit]) {
                        g_coinSaid[lc.bit] = 1;
                        std::printf("coincollect: t=%lld coin=%d player=(%.3f,%.3f)"
                                    " vy=%.4f mode=%d mini=%d frame=%d"
                                    " live=(%.3f,%.3f) |dx|=%.3f |dy|=%.3f"
                                    " bound=(%.3f,%.3f)\n",
                                    (long long)t, lc.bit, px, py,
                                    (double)kid.s.vy, (int)kid.s.mode,
                                    (int)kid.s.mini, gframe, lc.cx, lc.cy,
                                    std::fabs(px - lc.cx), std::fabs(py - lc.cy),
                                    lc.hw + ph - mx, lc.hh + ph - my);
                    }
                    kid.s.coins |= bit;
                }
            }
            if (!dead && gatedMask
                && ((kid.s.trig & tapGateBits) || (kid.s.taps & 0x40))) {
                double wx = 0.0, wy = 0.0;
                fromFrame(gframe, (double)kid.s.xAbs, (double)kid.s.y, wx, wy);
                for (size_t ci = 0; ci < L.coins.size(); ++ci) {
                    if (!((gatedMask >> ci) & 1) || (kid.s.coins & (uint8_t)(1u << ci)))
                        continue;
                    if (passedOn(kid.s, gframe, tapGive.closeChan, wx, wy,
                                 tapMissX[ci], tapMissY[ci])) {
                        dead = true;
                        break;
                    }
                }
            }
            for (size_t gi = 0; gframe == 0 && gi < coinGate.size(); ++gi) {
                const CoinOnGate& G = coinGate[gi];
                if (kid.s.coins & (uint8_t)(1u << G.coinIdx)) continue;
                bool liveNow = false;
                for (const CoinLive& lc : coinLive)
                    if (lc.bit == G.coinIdx) { liveNow = true; break; }
                if (liveNow) continue;
                bool canStill = false;
                for (const auto& on : G.ons)
                    if ((kid.s.trig & on.first) || px <= on.second) {
                        canStill = true;
                        break;
                    }
                if (!canStill) { dead = true; break; }
            }
            if (!dead && tapSole && (kid.s.taps & 0x40)
                && !(kid.s.trig & tapGateBits))
                for (size_t ci = 0; ci < L.coins.size(); ++ci) {
                    if (!coinEnable[ci] || (coinEnable[ci] & ~tapGateBits)) continue;
                    if (kid.s.coins & (uint8_t)(1u << ci)) continue;
                    dead = true;
                    break;
                }
            for (size_t ri = 0; gframe == 0 && ri < coinReq.size(); ++ri) {
                const CoinCountReq& R = coinReq[ri];
                if (kid.s.coins & (uint8_t)(1u << R.coinIdx)) continue;
                int have = 0, ahead = 0;
                for (const auto& ib : g_itemBase)
                    if (ib.first == R.item) have += ib.second;
                for (size_t ii = 0; ii < g_collect.size() && ii < 16; ++ii) {
                    if (g_collect[ii].item != R.item) continue;
                    if (kid.s.items & (uint16_t)(1u << ii)) ++have;
                    else if (g_collect[ii].cx > px) ++ahead;
                }
                if (have + ahead < R.need) { dead = true; break; }
            }
            for (size_t ci = 0; !dead && gframe == 0 && ci < L.coins.size(); ++ci) {
                if (!coinPruneOk[ci]) continue;
                if (kid.s.coins & (uint8_t)(1u << ci)) continue;
                const Obj& oc = L.coins[ci];
                if (px > oc.cx + oc.hw + ph) { dead = true; break; }
            }
        }
    }
};

struct SearchResult {
    enum Kind : uint8_t { Solved, Partial, NoPlan, Declined };
    Kind kind = NoPlan;
    long long t = -1;
    double x = -1.0;
    State goal{};
    bool deterministic = true;
    std::vector<uint8_t> inputs;
    // NoPlan: the first states (up to 12) of the last layer the frontier had, for the FAILED lines.
    std::vector<State> lastLayer;
};

struct LayerWork {
    std::vector<State> cur, nxt;
    CellMap seen;
    std::vector<ShardMap> shards;   // parallel dedupe (KeyRec, ShardMap above)
    std::vector<uint64_t> kidKeys;  // compact copy of kids[i].key (8 B strides
                                    // instead of pulling the whole Child in)
    std::vector<uint8_t> kidFlag;   // 0 = not expanded, 1 = dead, 2 = alive
    std::vector<uint8_t> evKind;
    std::vector<uint32_t> evRec;
    std::vector<Child> kids;      // reused every layer
    std::vector<ClearSample> clearKids;   // --clearprobe only, reused too
    std::vector<size_t> gidx;     // this speed group's indices into `cur`
    std::vector<uint8_t> kidMoot;   // --twinaudit: per parent of the group, predicted twin
    // next arena size that triggers a GC (doubles after each compaction)
    size_t gcNext = g_gcNodes > 0 ? g_gcNodes : (size_t)-1;
    long long nBornPrev = 0, nDiedPrev = 0;   // last layer's expansion counters
    size_t prevAlive = 0;
    // The frontier starts at the root state -- the call's init, after every amendment.
    explicit LayerWork(const State& root) { cur.push_back(root); }
};

struct SearchArena {
    std::vector<Node> arena;
    std::vector<uint8_t> arenaMode;
    explicit SearchArena(bool checkWanted) {
        arena.push_back(Node{0});
        if (checkWanted) arenaMode.push_back(0);   // the root, which no state owns
    }
};

inline void planFromArena(const std::vector<Node>& arena, const State& goal,
                          std::vector<uint8_t>& inputs) {
    inputs.clear();
    for (uint32_t i = goal.parent; i != 0; i = arena[i].parent()) inputs.push_back(arena[i].action());
    std::reverse(inputs.begin(), inputs.end());
}

struct LadderPrefixSnap {
    long long T = -1;
    std::vector<State> cur;
    std::vector<uint64_t> objRefs;   // the frontier's Obj pointers, 12 per state (Model::encodeObj)
    size_t arenaSize = 0, arenaModeSize = 0;
    size_t gcNext = 0;
    long long nBornPrev = 0, nDiedPrev = 0;
    size_t prevAlive = 0;
    long long bestT = 0;
    double bestX = 0;
    State goalState{};
    bool solved = false;
    size_t maxAlive = 0;
    long long capDropped = 0, capHits = 0, cellCuts = 0, cellSpared = 0, cellTwinsKept = 0;
    SearchCensus census;
    TwinStats twin;
    long long workStates = 0;
    std::vector<AutoTrig> autoTrig;
    std::vector<RotTrig> rotTrig;
    std::vector<std::vector<double>> carry;   // per frame 0..3, empty when not bound
    std::vector<uint8_t> carryOn;
    long long gfireGroups = 0, gfireSpread = 0, gfireSum = 0;
    int gfireMax = 0;
    int groupXSplitMax = 0;
    long long rjJoinT = -1;
    uint32_t rjOldNode = 0xffffffffu;
    size_t layerXSize = 0;
};
struct LadderPrefixCache {
    bool active = false;        // this call is a ladder attempt under --ladderprefix (cliMain)
    long long t0 = -1;          // the anchor the saves were made from
    std::vector<double> layerX; // the leader x each layer t0+1.. started with (the map's argument)
    std::vector<LadderPrefixSnap> snaps;
    std::vector<Node> arena;    // the last attempt's arena (append-only between its saves)
    std::vector<uint8_t> arenaMode;
    bool usable = false;        // the last attempt ended normally, so its saves can be resumed
    int resume = -1;            // index into snaps to start this attempt from (cliMain), -1 = t0
    long long resumedFrom = -1; // what this call started from, for the account
};
inline LadderPrefixCache g_lpc;            // --ladderprefix / --ladderprefixcheck are read by cliMain
inline int g_ladderPrefixEvery = 64;       // save every N layers
inline size_t g_ladderPrefixMaxSnaps = 64; // and keep at most this many (oldest dropped)

template <class E, class Rules>
inline int searchLayers(E& e, const Rules& rules, SearchResult& out) {
    auto& t0 = e.in.t0;
    auto& dbgLayers = e.in.dbgLayers;
    auto& init = e.in.init;
    auto& horizon = e.in.horizon;
    auto& planCut = e.in.planCut;
    auto& goalX = e.in.goalX;
    auto& tEnd = e.in.tEnd;
    LayerWork work(init);   // the frontier starts at the root
    auto& cur = work.cur;
    auto& nxt = work.nxt;
    SearchArena store(e.in.checkWanted);   // this method's states
    auto& arena = store.arena;
    auto& checkWanted = e.in.checkWanted;
    auto& arenaMode = store.arenaMode;
    auto& seen = work.seen;
    auto& shards = work.shards;
    auto& kidKeys = work.kidKeys;
    auto& kidFlag = work.kidFlag;
    auto& evKind = work.evKind;
    auto& evRec = work.evRec;
    auto& model = e.in.model;
    auto& kids = work.kids;
    auto& clearKids = work.clearKids;
    auto& gidx = work.gidx;
    auto& kParallelMin = e.in.kParallelMin;
    auto& pool = e.in.pool;
    auto& bestT = e.outcome.bestT;
    auto& bestX = e.outcome.bestX;
    auto& goalState = e.outcome.goalState;
    auto& solved = e.outcome.solved;
    auto& maxAlive = e.outcome.maxAlive;
    auto& capDropped = e.outcome.capDropped;
    auto& capHits = e.outcome.capHits;
    auto& cellCuts = e.outcome.cellCuts;
    auto& cellSpared = e.outcome.cellSpared;
    auto& cellTwinsKept = e.outcome.cellTwinsKept;
    auto& census = e.outcome.census;
    auto& twin = e.outcome.twin;
    auto& kidMoot = work.kidMoot;
    auto& gcNext = work.gcNext;
    auto& nBornPrev = work.nBornPrev;
    auto& nDiedPrev = work.nDiedPrev;
    auto& prevAlive = work.prevAlive;
    auto& curDxF = e.in.curDxF;
    auto& ppMark = e.in.ppMark;
    const bool lpOn = g_lpc.active && !checkWanted && !g_refWatch && g_searchCensus == 0
                      && g_bandPath.empty();
    long long tStart = t0 + 1;
    g_lpc.resumedFrom = -1;
    const long long lpWork0 = g_outcome.workStates;   // what the call had before this loop
    if (lpOn && g_lpc.usable && g_lpc.t0 == t0 && g_lpc.resume >= 0
        && (size_t)g_lpc.resume < g_lpc.snaps.size()) {
        LadderPrefixSnap S = g_lpc.snaps[(size_t)g_lpc.resume];
        cur = S.cur;
        for (size_t i = 0; i < cur.size(); ++i) {   // the pointers into THIS attempt's level
            const uint64_t* r = &S.objRefs[i * 12];
            State& s = cur[i];
            s.snapObj = model.decodeObj(r[0]);
            s.usedOrb = model.decodeObj(r[1]);
            s.snapObj2 = model.decodeObj(r[2]);
            s.usedOrb2 = model.decodeObj(r[3]);
            for (int k = 0; k < 4; ++k) s.usedPad[k] = model.decodeObj(r[4 + k]);
            for (int k = 0; k < 4; ++k) s.usedPad2[k] = model.decodeObj(r[8 + k]);
        }
        g_lpc.arena.resize(S.arenaSize);
        arena.swap(g_lpc.arena);
        g_lpc.arenaMode.resize(S.arenaModeSize);
        arenaMode.swap(g_lpc.arenaMode);
        work.gcNext = S.gcNext;
        nBornPrev = S.nBornPrev;
        nDiedPrev = S.nDiedPrev;
        prevAlive = S.prevAlive;
        bestT = S.bestT;
        bestX = S.bestX;
        goalState = S.goalState;
        solved = S.solved;
        maxAlive = S.maxAlive;
        capDropped = S.capDropped;
        capHits = S.capHits;
        cellCuts = S.cellCuts;
        cellSpared = S.cellSpared;
        cellTwinsKept = S.cellTwinsKept;
        census = S.census;
        twin = S.twin;
        g_outcome.workStates = lpWork0 + S.workStates;   // the loop's own work up to T
        g_autoTrig = S.autoTrig;
        g_rotTrig = S.rotTrig;
        for (int f = 0; f < 4; ++f)
            if (S.carryOn[(size_t)f]) model.loadWorld(f, S.carry[(size_t)f], S.T);
        g_gfireGroups = S.gfireGroups;
        g_gfireSpread = S.gfireSpread;
        g_gfireSum = S.gfireSum;
        g_gfireMax = S.gfireMax;
        g_groupXSplitMax = S.groupXSplitMax;
        g_rjJoinT = S.rjJoinT;
        g_rjOldNode = S.rjOldNode;
        g_lpc.layerX.resize(S.layerXSize);
        g_lpc.snaps.resize((size_t)g_lpc.resume + 1);   // later saves belong to the old attempt
        tStart = S.T + 1;
        g_lpc.resumedFrom = S.T;
        std::printf("ladderprefix: attempt resumes after t=%lld (%lld layers not searched again)\n",
                    S.T, S.T - t0);
    } else if (lpOn) {
        g_lpc.layerX.clear();
        g_lpc.snaps.clear();
    }
    g_lpc.usable = false;
    g_lpc.arena.clear();
    g_lpc.arenaMode.clear();
    g_lpc.t0 = t0;
    bool lpBound = capHits > 0;   // save only once this attempt's cap has bound (a call that never binds never climbs)
    for (long long t = tStart; t <= tEnd && !solved; ++t) {
        if (lpOn) g_lpc.layerX.push_back(bestX);   // the x this layer's capAtX / gridAtX read
        ppMark(6);
        nxt.clear();
        if (g_refWatch && g_refLostAt < 0) {
            if (t == t0 + 1) {
                g_refParent = cur.empty() ? -1 : 0;
            }
            if (g_refParent < 0 && t == t0 + 1) {
                std::printf("refwatch: the reference is not in the FIRST "
                            "frontier (t=%lld) -- wrong trace or wrong anchor, "
                            "not a search result\n", t - 1);
                g_refWatch = false;
            }
            if (t == t0 + 1)
                std::printf("refwatch: armed rows=%zu t0=%lld parent=%d "
                            "frontier=%zu\n",
                            g_refRows.size(), t0, g_refParent, cur.size());
            for (int a = 0; a < 2; ++a) {
                g_refKidFate[a] = -1;
                g_refKidWhy[a] = "";
                g_refKidKey[a] = 0;
            }
        }

        model.fireAutoTriggers(cur, t);

        struct GKey : GroupKey {
            uint16_t xc;
            bool operator==(const GKey& o) const {
                return GroupKey::operator==(o) && xc == o.xc;
            }
        };
        std::vector<uint64_t> curSig(g_groupFire ? cur.size() : 0);
        for (size_t i = 0; i < curSig.size(); ++i) curSig[i] = groupFireSig(cur[i], t);
        auto sigOf = [&](size_t i) -> uint64_t { return curSig.empty() ? 0 : curSig[i]; };
        std::vector<uint16_t> curXc;
        bool xsplitHere = g_groupXSplit && cur.size() > 1;
        for (size_t i = 0; xsplitHere && i < cur.size(); ++i)
            if (model.progress(cur[i]).frame != 0 || model.progress(cur[i]).rev != 0) xsplitHere = false;
        if (xsplitHere) {
            curXc.assign(cur.size(), 0);
            std::vector<GKey> bases;
            std::vector<std::vector<size_t>> members;
            for (size_t i = 0; i < cur.size(); ++i) {
                const State& s = cur[i];
                const GKey b{model.groupKey(s, curDxF, sigOf(i)), 0};
                const size_t j = (size_t)(std::find(bases.begin(), bases.end(), b) - bases.begin());
                if (j == bases.size()) {
                    bases.push_back(b);
                    members.emplace_back();
                }
                members[j].push_back(i);
            }
            bool split = false;
            for (size_t j = 0; j < members.size(); ++j) {
                std::vector<size_t>& m = members[j];
                const double gap = 2.0 * kGroupWinMargin + std::fabs((double)bases[j].dx);
                std::sort(m.begin(), m.end(), [&](size_t a, size_t b) {
                    const double xa = model.progress(cur[a]).x, xb = model.progress(cur[b]).x;
                    return xa < xb || (xa == xb && a < b);
                });
                uint16_t c = 0;
                for (size_t k = 1; k < m.size(); ++k) {
                    if (model.progress(cur[m[k]]).x - model.progress(cur[m[k - 1]]).x > gap
                        && c < 0xFFFF)
                        ++c;
                    curXc[m[k]] = c;
                }
                if (c > 0) split = true;
                if ((int)c + 1 > g_groupXSplitMax) g_groupXSplitMax = (int)c + 1;
            }
            if (split) ++g_groupXSplitLayers;
        }
        auto xcOf = [&](size_t i) -> uint16_t { return curXc.empty() ? 0 : curXc[i]; };
        std::vector<GKey> groupDx;
        for (size_t i = 0; i < cur.size(); ++i) {
            const State& s = cur[i];
            const GKey k{model.groupKey(s, curDxF, sigOf(i)), xcOf(i)};
            if (std::find(groupDx.begin(), groupDx.end(), k) == groupDx.end())
                groupDx.push_back(k);
        }

        auto emit = [&](State s, const State& from, uint8_t action, uint64_t k) {
            auto finish = [&](State& stored) {
                arena.push_back(Node{from.parent | ((uint32_t)action << 31)});
                if (checkWanted) arenaMode.push_back(s.mode);
                s.parent = (uint32_t)(arena.size() - 1);
                s.action = action;
                stored = s;
                if (rules.reached(s) && (!solved || rules.prefer(rules.routeScore(s), rules.routeScore(goalState)))) {
                    solved = true;
                    goalState = s;
                }
            };
            Slots& sl = seen.at(k);
            if (sl.hi < 0) {
                nxt.push_back(State{});
                sl.hi = sl.lo = (int)nxt.size() - 1;
                finish(nxt.back());
            } else if (s.vy > nxt[(size_t)sl.hi].vy + 1e-9) {
                if (sl.hi == sl.lo) {
                    nxt.push_back(State{});
                    sl.hi = (int)nxt.size() - 1;
                }
                finish(nxt[(size_t)sl.hi]);
            } else if (s.vy < nxt[(size_t)sl.lo].vy - 1e-9) {
                if (sl.hi == sl.lo) {
                    nxt.push_back(State{});
                    sl.lo = (int)nxt.size() - 1;
                }
                finish(nxt[(size_t)sl.lo]);
            } else if (dp::slotTieBreak()) {
                const int slot = s.vy >= nxt[(size_t)sl.hi].vy - 1e-9 ? sl.hi
                                 : s.vy <= nxt[(size_t)sl.lo].vy + 1e-9 ? sl.lo : -1;
                if (slot >= 0 && dp::slotScore(s) < dp::slotScore(nxt[(size_t)slot])) finish(nxt[(size_t)slot]);
            }
        };

        long long nBorn = 0, nDied = 0;
        long long qfLost = 0, qfKeys = 0;
        auto qfoldAccount = [&](size_t before) {
            auto qs = [](const State& s) {
                return ((uint64_t)s.rotSpent << 32) | ((uint64_t)s.rotChan << 16)
                       | (uint64_t)s.rotRev;
            };
            auto add = [](std::vector<uint64_t>& v, uint64_t q) {
                if (std::find(v.begin(), v.end(), q) == v.end()) v.push_back(q);
            };
            std::unordered_map<uint64_t, std::vector<uint64_t>> alive, kept;
            for (size_t i = 0; i < kids.size(); ++i)
                if (kidFlag[i] == 2) add(alive[kidKeys[i]], qs(kids[i].s));
            for (size_t j = before; j < nxt.size(); ++j)
                add(kept[modelKeyOf(nxt[j], (long long)t) ^ rules.keyBits(nxt[j])], qs(nxt[j]));
            for (const auto& [k, a] : alive) {
                const auto it = kept.find(k);
                long long lost = 0;
                for (uint64_t q : a)
                    if (it == kept.end()
                        || std::find(it->second.begin(), it->second.end(), q)
                               == it->second.end())
                        ++lost;
                if (lost) { qfLost += lost; ++qfKeys; }
            }
        };
        const bool qfHere = (g_qfoldLo >= 0 && t >= g_qfoldLo && t <= g_qfoldHi);
        for (const auto& gkey : groupDx) {
        const float gdx = gkey.dx;
        const TouchMask gtrig = gkey.trig;
        const int gframe = (int)gkey.frame;
        const int grev = (int)gkey.rev;
        double gLo = 1e18, gHi = -1e18;
        int gFire = -1;   // latest tick any of them fired a touch trigger
        int gFireMin = 0x7fffffff;   // ...and the earliest, for the pass count only
        uint16_t gFireB[kTouchBits] = {};
        float gLockOff = 1e30f;
        for (size_t ci = 0; ci < cur.size(); ++ci) {
            const State& s = cur[ci];
            if (!model.inGroup(gkey, s, curDxF) || sigOf(ci) != gkey.fsig || xcOf(ci) != gkey.xc)
                continue;
            gLo = std::min(gLo, (double)s.xAbs);
            gHi = std::max(gHi, (double)s.xAbs);
            gFire = std::max(gFire, (int)s.trigT);
            gFireMin = std::min(gFireMin, (int)s.trigT);
            // gFireB is uint16_t[kTouchBits] -- fold all of it, not the first 32.
            for (int b = 0; b < kTouchBits; ++b)
                if (s.fireB[b] > gFireB[b]) gFireB[b] = s.fireB[b];
            if (s.lockOff < gLockOff) gLockOff = s.lockOff;
        }
        if (gLockOff > 1e29f) gLockOff = 0.f;   // no member (group is empty)
        float gD2Lo = 0.f, gD2Hi = 0.f;
        if (g_p2xOff)
            for (size_t ci = 0; ci < cur.size(); ++ci) {
                const State& s = cur[ci];
                if (!model.inGroup(gkey, s, curDxF) || sigOf(ci) != gkey.fsig || xcOf(ci) != gkey.xc)
                    continue;
                const float d2 = p2Dx(s);
                gD2Lo = std::min(gD2Lo, d2);
                gD2Hi = std::max(gD2Hi, d2);
            }
        if (gFire >= 0) {
            ++g_gfireGroups;
            if (gFireMin < gFire) {
                ++g_gfireSpread;
                const int d = gFire - gFireMin;
                g_gfireSum += d;
                if (d > g_gfireMax) g_gfireMax = d;
            }
        }
        if (g_needTrig) {
            bool missed = false;
            for (size_t b = 0; b < g_touch.size() && !missed; ++b)
                if ((g_needTrig & ~gtrig) & touchBit(b))
                    missed = gLo > g_touch[b].cx + g_touch[b].hw + 40.0;
            if (missed) continue;
        }
        const double xPrev = gHi;                    // nominal, reports only
        const double sdx = grev ? -(double)gdx : (double)gdx;
        const double x = (grev ? gLo : gHi) + sdx;   // ditto
        const float dxUsed = (float)sdx;
        const double wLo = grev ? (gLo + sdx) : gLo;
        const double wHi = grev ? gHi : (gHi + sdx);
        // Gameplay rotation triggers this group's advance reaches (model.hpp fireRotTriggers).
        model.fireRotTriggers(gframe, grev, gLo, gHi, wLo, wHi, t);
        AutoOwnGuard autoOwn;
        if (g_autoOwn && gframe == 0 && !grev) autoOwnApply(autoOwn, gHi, (double)gdx, t);
        GroupEnv env;
        double pLo = wLo, pHi = wHi;
        p2Widen(pLo, pHi, gD2Lo, gD2Hi);   // --p2xoff
        model.prepare(env, gframe, gtrig, gFire, gFireB, gLockOff + (float)sdx, x, xPrev, dxUsed,
                      pLo, pHi, t);
        const std::vector<const Obj*>& near = env.near;
        const std::vector<const Obj*>& ports = env.ports;
        const std::vector<const Obj*>& pads = env.pads;
        const std::vector<const Obj*>& orbs = env.orbs;
        const std::vector<const Obj*>& slps = env.slps;
        const std::vector<std::pair<const TouchTrig*, TouchMask>>& trigs = env.trigs;
        seen.ensure(cur.size() * 2 + 64);
        seen.clear();   // per GROUP, not per layer (see the note above)
        (void)x; (void)xPrev;
        const StepCtx K = env.ctx();
        ppMark(0);
        gidx.clear();
        for (size_t i = 0; i < cur.size(); ++i)
            if (((cur[i].dx > 0.f) ? cur[i].dx : curDxF) == gdx
                && cur[i].trig == gtrig && sigOf(i) == gkey.fsig
                && (curXc.empty()
                    || ((((int)cur[i].frame == gframe && (int)cur[i].rev == grev))
                            ? curXc[i] == gkey.xc
                            : gkey.xc == 0)))
                gidx.push_back(i);
        kids.assign(gidx.size() * 2, Child{});
        kidKeys.assign(gidx.size() * 2, 0);
        kidFlag.assign(gidx.size() * 2, 0);
        const bool dbgHere = (dbgLayers > 0 && t - t0 <= dbgLayers);
        const bool orbsEmpty = orbs.empty();
        bool groupPressTrig = false;
        for (const auto& tb : trigs)
            if (tb.first->tap || tb.first->press) { groupPressTrig = true; break; }
        const bool twinOn = !dbgHere && !g_refWatch
                            && !g_clearProbe && !qfHere && g_coinDbgT < 0 && !g_airPress
                            && false   // --latgap was always on, and the twin skip never ran with it
                            && !(g_rotWatchLo >= 0 && t >= g_rotWatchLo && t <= g_rotWatchHi);
        kidMoot.assign(twinOn ? gidx.size() : 0, 0);
        auto pressMoot = [&](const State& s) -> bool {
            if (s.dual || s.grounded || s.onSlope) return false;
            if (s.mode != 0 && s.mode != 2 && s.mode != 5 && s.mode != 6) return false;
            if ((int)s.frame != gframe || (int)s.rev != grev) return false;
            if (s.ringHold || s.pressSpent || s.rHover || s.dashing || s.ogLinger || s.holdDead)
                return false;
            if ((int)s.flipT + 1 < kFlipGraceTicks) return false;
            if (nearPressBox(s)) return false;
            if (model.pressMatters(s)) return false;
            for (const std::vector<Fixup>* fv : {&g_fixupKills, &g_fixupDeltas}) {
                const float lo = s.xAbs - 8.f;
                const auto it = std::lower_bound(
                    fv->begin(), fv->end(), lo,
                    [](const Fixup& f, float xx) { return f.x < xx; });
                if (it != fv->end() && it->x <= s.xAbs + 8.f) return false;
            }
            const double px = (double)s.xAbs, py = (double)s.y;
            const double rx = 60.0 + 2.0 * std::fabs((double)K.dxF);
            const double ry = 60.0 + std::fabs((double)s.vy);
            auto within = [&](const std::vector<const Obj*>& v) {
                for (const Obj* o : v)
                    if (std::fabs(o->cx - px) <= o->hw + rx && std::fabs(o->cy - py) <= o->hh + ry)
                        return true;
                return false;
            };
            for (const Obj* o : pads)
                if (o->type == 10 && std::fabs(o->cx - px) <= o->hw + rx
                    && std::fabs(o->cy - py) <= o->hh + ry)
                    return false;
            return !within(orbs) && !within(ports) && !within(slps);
        };
        const int gridNow = gridAtX(bestX);
        auto stepKidAt = [&](size_t i, bool mayTwin) {
            const State& s = cur[gidx[i >> 1]];
            const int input = (int)(i & 1);
            // May the button change on this tick (model.hpp edgeLocked)?
            const bool edgeLocked = model.edgeLocked(s, gridNow, K.t);
            if (edgeLocked && input != dp::heldLevel(s)) return;   // heldLevel
            if (input == 1 && s.mode == 0 && !s.grounded && orbsEmpty
                && !s.dashing && !(s.dual && s.dashing2)   // --dualdash
                && !nearPressBox(s) && !nearCtrlHoldBox((double)s.xAbs) && !g_airPress
                && !nearHoldTap((double)s.xAbs)   // --holdtap (object.hpp)
                && !(edgeLocked && dp::heldLevel(s) == 1))   // heldLevel
                return;
            // --latgap: no edge on a tick GD cannot put one (model.hpp inputAllowed)
            if (!model.inputAllowed(s, input, gridNow, K.t))
                return;
            if (input == 1 && mayTwin && twinOn && pressMoot(s)) {
                if (!g_twinAudit) {
                    kidFlag[i] = 3;
                    return;
                }
                kidMoot[i >> 1] = 1;
            }
            Child& kid = kids[i];
            bool dead = false;
            RotNote rot;
            kid.s = model.advance(s, input, K, dead, &rot);
            if (rot.set) {   // --rotwatch: print only, read in the serial pass below
                kid.rotUid = rot.uid;
                kid.rotF0 = rot.f0;
                kid.rotNf = rot.nf;
            }
            model.noteInput(s, kid.s, input);
            // ---- --coins: collect, then kill whoever left one behind (rules.afterStep) ----
            model.fireGates(kid.s, s.action, input, K, gframe, t, dead);
            rules.afterStep(s, kid, input, K, gframe, t, dead);
            if (!dead && kid.s.tight < 0xffff
                && tightHere(near, (double)kid.s.xAbs, (double)kid.s.y,
                             g_roomy ? hazardHalfFor(kid.s.mode, kid.s.mini) : playerHalf(kid.s.mode, kid.s.mini != 0),
                             g_tightPx, g_roomy))   // --tightpx, --roomy
                ++kid.s.tight;
            kid.dead = dead ? 1 : 0;
            kid.valid = 1;
            if (!dead) kid.key = modelKeyOf(kid.s, (long long)t) ^ rules.keyBits(kid.s);
            if (!dead && g_fireBCheck) {
                for (int b = 0; b < kTouchBits; ++b) {
                    const bool bit = kid.s.trig.test(b);
                    const bool has = kid.s.fireB[b] != 0;
                    if (bit && !has) ++g_fireBNoTick;
                    if (!bit && has) ++g_fireBNoBit;
                    if (bit && has && g_touchFireT[b] >= 0
                        && (int)kid.s.fireB[b] < g_touchFireT[b]
                        && !init.trig.test(b))
                        ++g_fireBTooEarly;
                }
            }
            kidKeys[i] = kid.key;
            kidFlag[i] = dead ? 1 : 2;
            if (dbgHere || g_refWatch) { kid.why = g_deadWhy; kid.obj = g_deadObj; }
        };
        auto stepKid = [&](size_t i) { stepKidAt(i, true); };
        if (pool && !dbgHere && kids.size() >= kParallelMin)
            pool->parallelFor(kids.size(), stepKid);
        else
            for (size_t i = 0; i < kids.size(); ++i) stepKid(i);
        if (twinOn) {
            auto airborne = [&](size_t a) {
                const Child& k = kids[a];
                return kidFlag[a] == 2 && !k.s.grounded && !k.s.onSlope;
            };
            std::vector<size_t> redo;   // pending pressed slots whose sibling did not qualify
            for (size_t j = 0; j < gidx.size(); ++j) {
                const size_t a = 2 * j, b = a + 1;
                const State& p = cur[gidx[j]];
                const int m = p.mode < 8 ? p.mode : 0;
                auto pressedFrom = [&](const Child& rel) {
                    Child k = rel;
                    k.s.action = 1;
                    k.s.heldSeed = 0;
                    if (g_minPulse > 1)
                        k.s.edgeAge = (dp::heldLevel(p) != 1)   // heldLevel
                                          ? 0 : (uint8_t)std::min(254, (int)p.edgeAge + 1);
                    if (g_minPressGap > 1)   // --minpressgap
                        k.s.pressAge = (dp::heldLevel(p) != 1)
                                           ? 0 : (uint8_t)std::min(254, (int)p.pressAge + 1);
                    if (g_fewPress)   // --fewpress
                        k.s.inputs = (uint16_t)std::min(0xffff, (int)p.inputs + (p.action != 1 ? 1 : 0));
                    if (g_heldAll) {
                        k.s.held = 1;
                        k.s.jumpBuf = 1;
                        k.key = modelKeyOf(k.s, (long long)t) ^ rules.keyBits(k.s);
                    }
                    return k;
                };
                if (g_twinAudit) {
                    if (!kidMoot[j] || !airborne(a)) continue;
                    ++twin.predicted;
                    ++twin.byMode[m];
                    const int fa = kidFlag[a], fb = kidFlag[b];
                    bool ok = fa == fb;
                    if (ok && fa == 2) {
                        if (g_heldAll) {
                            const Child pk = pressedFrom(kids[a]);
                            ok = pk.key == kidKeys[b] && pk.s.vy == kids[b].s.vy
                                 && pk.s.y == kids[b].s.y && pk.s.trig == kids[b].s.trig
                                 && pk.s.held == kids[b].s.held
                                 && pk.s.jumpBuf == kids[b].s.jumpBuf;
                        } else {
                            ok = kidKeys[a] == kidKeys[b] && kids[a].s.vy == kids[b].s.vy
                                 && kids[a].s.trig == kids[b].s.trig;
                        }
                    }
                    if (!ok)
                        twin.violation(t, p, fa, fb, kids[a].s, kids[b].s, kidKeys[a],
                                       kidKeys[b]);
                } else if (kidFlag[b] == 3) {
                    if (!airborne(a)) {
                        kidFlag[b] = 0;
                        redo.push_back(b);
                        continue;
                    }
                    kids[b] = pressedFrom(kids[a]);
                    kidKeys[b] = g_heldAll ? kids[b].key : kidKeys[a];
                    kidFlag[b] = kidFlag[a];
                    ++twin.skipped;
                    ++twin.byMode[m];
                }
            }
            if (!redo.empty()) {
                auto stepRedo = [&](size_t k) { stepKidAt(redo[k], false); };
                if (pool && redo.size() >= kParallelMin)
                    pool->parallelFor(redo.size(), stepRedo);
                else
                    for (size_t k = 0; k < redo.size(); ++k) stepRedo(k);
            }
        }
        ppMark(1);
        // --searchcensus: did the two inputs of each parent lead anywhere different? Print only.
        if (g_searchCensus > 0)
            for (size_t j = 0; j < gidx.size(); ++j)
                census.siblings(cur[gidx[j]], kidFlag[2 * j], kidFlag[2 * j + 1], kids[2 * j].s,
                                kids[2 * j + 1].s, kidKeys[2 * j], kidKeys[2 * j + 1],
                                (long long)t);
        if (g_rotWatchLo >= 0 && t >= g_rotWatchLo && t <= g_rotWatchHi) {
            struct RW { int uid, f0, nf; size_t n; };
            std::vector<RW> rws;
            size_t nk = 0;
            for (const Child& k : kids) {
                if (!k.valid) continue;
                ++nk;
                if (k.rotNf < 0) continue;
                bool found = false;
                for (RW& r : rws)
                    if (r.uid == k.rotUid && r.f0 == k.rotF0 && r.nf == k.rotNf) { ++r.n; found = true; break; }
                if (!found) rws.push_back(RW{k.rotUid, (int)k.rotF0, (int)k.rotNf, 1});
            }
            for (const RW& r : rws)
                std::printf("rotwatch: t=%lld gdx=%.4f uid=%d f0=%d nf=%d kids=%zu/%zu\n",
                            (long long)t, (double)gdx, r.uid, r.f0, r.nf, r.n, nk);
        }

        if (g_refWatch && g_refParent >= 0) {
            for (size_t i = 0; i < kids.size(); ++i) {
                if ((int)gidx[i >> 1] != g_refParent) continue;
                const int a = (int)(i & 1);
                if (g_refKidFate[a] == 2) continue;   // already alive somewhere
                g_refKidFate[a] = (int)kidFlag[i];
                g_refKidWhy[a] = kids[i].why ? kids[i].why : "";
                g_refKidKey[a] = kidKeys[i];
                g_refKidState[a] = kids[i].s;
            }
        }

        if (g_clearProbe) {
            clearKids.clear();
            for (size_t i = 0; i < kids.size(); ++i) {
                if (kidFlag[i] != 2) continue;   // 1 = died, 0 = never expanded
                const State& c = kids[i].s;
                if (c.frame != 0) { ++g_clear.framesSkipped; continue; }
                const double px = (double)c.xAbs, py = (double)c.y;
                ClearSample cs;
                cs.key = kidKeys[i];
                cs.vy = c.vy;
                cs.solid = solidClearance(near, px, py,
                                          playerHalf(c.mode, c.mini != 0),
                                          &g_clear.slopesSkipped);
                cs.haz = hazardClearance(near, px, py,
                                         hazardHalfFor(c.mode, c.mini),
                                         c.mode, c.mini);
                cs.mode = c.mode;
                cs.goal = (uint8_t)(c.frame == 0 && px >= goalX ? 1 : 0);
                clearKids.push_back(cs);
            }
            g_clear.addLayer(clearKids);
        }

        const size_t qfBefore = nxt.size();   // --qfoldwatch: this group's survivors start here
        const bool parDedupe = pool && !dbgHere && g_threads > 1
                               && kids.size() >= 4096;
        if (parDedupe) {
            const size_t nsh = (size_t)g_threads;
            if (shards.size() < nsh) shards.resize(nsh);
            if (evKind.size() < kids.size()) {
                evKind.resize(kids.size(), 0);
                evRec.resize(kids.size(), 0);
            }
            pool->parallelTasks(nsh, [&](size_t si) {
                ShardMap& sm = shards[si];
                sm.clear();
                sm.ensure(kids.size() / nsh + 64);
                for (uint32_t i = 0; i < (uint32_t)kids.size(); ++i) {
                    if (kidFlag[i] != 2) continue;
                    const uint64_t k = kidKeys[i];
                    if ((size_t)((k * 0x9E3779B97F4A7C15ull) >> 32) % nsh != si)
                        continue;
                    const float vy = kids[i].s.vy;
                    uint32_t& ri = sm.at(k);
                    bool accepted = false;
                    if (ri == kNone) {
                        ri = (uint32_t)sm.recs.size();
                        sm.recs.push_back(KeyRec{k, i, kNone, i, i, vy, vy, 0});
                        evKind[i] = (uint8_t)(1 + si);
                        evRec[i] = ri;
                        accepted = true;
                    } else {
                        KeyRec& r = sm.recs[ri];
                        if ((double)vy > (double)r.hiVy + 1e-9) {
                            if (r.bOrd == kNone) {
                                r.bOrd = i;
                                r.bWasHi = 1;
                                evKind[i] = (uint8_t)(1 + si);
                                evRec[i] = ri;
                            }
                            r.hiIdx = i;
                            r.hiVy = vy;
                            accepted = true;
                        } else if ((double)vy < (double)r.loVy - 1e-9) {
                            if (r.bOrd == kNone) {
                                r.bOrd = i;
                                r.bWasHi = 0;
                                evKind[i] = (uint8_t)(1 + si);
                                evRec[i] = ri;
                            }
                            r.loIdx = i;
                            r.loVy = vy;
                            accepted = true;
                        } else if (dp::slotTieBreak()) {
                            // --fewpress / --roomy, emit()'s same-vy rule. One slot so far: both reps.
                            const uint32_t in = dp::slotScore(kids[i].s);
                            if ((double)vy >= (double)r.hiVy - 1e-9) {
                                if (in < dp::slotScore(kids[r.hiIdx].s)) {
                                    if (r.bOrd == kNone) { r.loIdx = i; r.loVy = vy; }
                                    r.hiIdx = i;
                                    r.hiVy = vy;
                                    accepted = true;
                                }
                            } else if ((double)vy <= (double)r.loVy + 1e-9) {
                                if (in < dp::slotScore(kids[r.loIdx].s)) {
                                    if (r.bOrd == kNone) { r.hiIdx = i; r.hiVy = vy; }
                                    r.loIdx = i;
                                    r.loVy = vy;
                                    accepted = true;
                                }
                            }
                        }
                    }
                    if (accepted && rules.reached(kids[i].s)
                        && (sm.goalOrd == kNone
                            || rules.prefer(rules.routeScore(kids[i].s), sm.goalTight))) {
                        sm.goalOrd = i;
                        sm.goalTight = rules.routeScore(kids[i].s);
                    }
                }
            });
            // counters (compact array, trivial serial pass)
            for (size_t i = 0; i < kids.size(); ++i) {
                if (!kidFlag[i]) continue;
                ++nBorn;
                if (kidFlag[i] == 1) ++nDied;
            }
            // materialise nxt in the serial loop's slot order
            for (uint32_t i = 0; i < (uint32_t)kids.size(); ++i) {
                if (!evKind[i]) continue;
                ShardMap& sm = shards[(size_t)evKind[i] - 1];
                const KeyRec& r = sm.recs[evRec[i]];
                evKind[i] = 0;   // leave the array clean for the next group
                uint32_t src;
                if (r.bOrd == kNone) src = r.hiIdx;   // never split: hi == lo
                else if (i == r.aOrd) src = r.bWasHi ? r.loIdx : r.hiIdx;
                else src = r.bWasHi ? r.hiIdx : r.loIdx;
                const State& from = cur[gidx[src >> 1]];
                const uint8_t action = (uint8_t)(src & 1);
                arena.push_back(Node{from.parent | ((uint32_t)action << 31)});
                if (checkWanted) arenaMode.push_back(kids[src].s.mode);
                nxt.push_back(kids[src].s);
                nxt.back().parent = (uint32_t)(arena.size() - 1);
                nxt.back().action = action;
            }
            {
                uint32_t g = kNone;
                uint32_t gt = 0xffffffffu;   // routeScore, 32 bits
                for (size_t si = 0; si < nsh; ++si) {
                    const uint32_t o = shards[si].goalOrd;
                    if (o == kNone) continue;
                    const uint32_t ot = shards[si].goalTight;
                    if (g == kNone || ot < gt || (ot == gt && o < g)) {
                        g = o;
                        gt = ot;
                    }
                }
                if (g != kNone && (!solved || rules.prefer(gt, rules.routeScore(goalState)))) {
                    const State& from = cur[gidx[g >> 1]];
                    const uint8_t action = (uint8_t)(g & 1);
                    arena.push_back(
                        Node{from.parent | ((uint32_t)action << 31)});
                    if (checkWanted) arenaMode.push_back(kids[g].s.mode);
                    goalState = kids[g].s;
                    goalState.parent = (uint32_t)(arena.size() - 1);
                    goalState.action = action;
                    solved = true;
                }
            }
            if (qfHere) qfoldAccount(qfBefore);
            continue;   // next speed group -- skips the serial phase 2
        }
        // ---- phase 2: dedupe and record (serial, in order) ------------------
        for (size_t i = 0; i < kids.size(); ++i) {
            const Child& kid = kids[i];
            if (!kid.valid) continue;
            const State& s = cur[gidx[i >> 1]];
            const int input = (int)(i & 1);
            const State& c = kid.s;
            const bool dead = kid.dead != 0;
            ++nBorn;
            if (dead) ++nDied;
            {
                if (dbgHere) {
                    char why[128] = "";
                    if (dead) {
                        if (kid.obj)
                            std::snprintf(why, sizeof why,
                                          " %s id=%d type=%d (%.0f,%.0f) %gx%g",
                                          kid.why, kid.obj->id,
                                          (int)kid.obj->type, kid.obj->cx,
                                          kid.obj->cy, kid.obj->hw * 2,
                                          kid.obj->hh * 2);
                        else
                            std::snprintf(why, sizeof why, " %s", kid.why);
                    }
                    char two[160] = "";
                    if (s.dual)
                        std::snprintf(two, sizeof two,
                                      " | p2 (%.3f,%.3f)->(%.3f,%.3f) flip2=%d "
                                      "g2=%d free=%d",
                                      (double)s.y2, (double)s.vy2,
                                      (double)c.y2, (double)c.vy2,
                                      (int)c.flip2, (int)c.grounded2,
                                      (int)c.freeHalf);
                    std::printf("dbg t=%lld in=%d: (%.3f,%.3f)->(%.3f,%.3f) "
                                "x=%.2f mode=%d flip=%d g=%d held=%d dead=%d%s%s\n",
                                t, input, (double)s.y, (double)s.vy, (double)c.y,
                                (double)c.vy, (double)c.xAbs, (int)c.mode,
                                (int)c.flip, (int)c.grounded, (int)s.held,
                                (int)dead, why, two);
                }
                if (!dead) emit(c, s, (uint8_t)input, kid.key);
            }
        }
        if (qfHere) qfoldAccount(qfBefore);
        }   // speed group
        if (!g_bandPath.empty()) {
            float ylo = 1e9f, yhi = -1e9f;
            double xr = 0;
            struct Cls { uint32_t k; int n; float lo, hi; };
            std::vector<Cls> cs;
            for (const State& s : nxt) {
                ylo = std::min(ylo, s.y);
                yhi = std::max(yhi, s.y);
                xr = std::max(xr, (double)s.xAbs);
                const uint32_t k = ((uint32_t)s.mode << 24)
                                   ^ ((uint32_t)s.mini << 20)
                                   ^ ((uint32_t)s.dual << 16)
                                   ^ (uint32_t)std::lround(s.dx * 1000.0);
                size_t j = 0;
                for (; j < cs.size(); ++j) if (cs[j].k == k) break;
                if (j == cs.size()) cs.push_back({k, 0, 1e9f, -1e9f});
                cs[j].n++;
                cs[j].lo = std::min(cs[j].lo, s.y);
                cs[j].hi = std::max(cs[j].hi, s.y);
            }
            if (!nxt.empty()) {
                BandRow b;
                b.t = t; b.alive = (int)nxt.size();
                b.ylo = ylo; b.yhi = yhi; b.x = (float)xr;
                b.merged = (int)std::max<long long>(
                    0, nBorn - nDied - (long long)nxt.size());
                char buf[64];
                for (const Cls& c : cs) {
                    std::snprintf(buf, sizeof buf, "%u.%u.%u.%u:%d:%.0f:%.0f",
                                  c.k >> 24, (c.k >> 20) & 0xf,
                                  (c.k >> 16) & 0xf, c.k & 0xffff, c.n,
                                  (double)c.lo, (double)c.hi);
                    if (!b.cls.empty()) b.cls += ';';
                    b.cls += buf;
                }
                g_bands.push_back(std::move(b));
            }
        }
        if (!g_touch.empty()) {
            TouchMask m{};
            for (const State& s : nxt) m |= s.trig;
            if (m & ~g_trigReported) {
                for (size_t b = 0; b < g_touch.size(); ++b)
                    if ((m & ~g_trigReported) & touchBit(b))
                        std::printf("triggers: box (%.0f,%.0f) entered at "
                                    "t=%lld\n", g_touch[b].cx, g_touch[b].cy, t);
                g_trigReported |= m;
            }
        }
        ppMark(2);
        bool refPreCap = false;
        State refCarried{};     // the tracked state, captured BEFORE the cap
        if (g_refWatch && g_refLostAt < 0) {
            const char* gate = nullptr;
            char detail[256];
            detail[0] = '\0';
            const auto it = g_refRows.find(t);
            int want = -1;
            if (it != g_refRows.end() && it->second.act >= 0
                && it->second.act < 2 && g_refKidFate[it->second.act] >= 1) {
                want = it->second.act;
            } else {
                for (int a = 0; a < 2 && it != g_refRows.end(); ++a)
                    if (g_refKidFate[a] >= 1 && refMatches(g_refKidState[a],
                                                           it->second))
                        want = a;
            }
            if (g_refParent < 0) {
                gate = "parent-gone";
            } else if (it == g_refRows.end()) {
                gate = "trace-ended";
            } else if (want < 0) {
                gate = "cannot-reproduce";
                snprintf(detail, sizeof(detail),
                         " ref y=%.4f vy=%.4f | kid0 y=%.4f vy=%.4f f=%d"
                         " | kid1 y=%.4f vy=%.4f f=%d",
                         it->second.y, it->second.vy,
                         (double)g_refKidState[0].y, (double)g_refKidState[0].vy,
                         g_refKidFate[0],
                         (double)g_refKidState[1].y, (double)g_refKidState[1].vy,
                         g_refKidFate[1]);
            } else if (g_refKidFate[want] == 2
                       && !refMatches(g_refKidState[want], it->second)) {
                gate = "cannot-reproduce";
                snprintf(detail, sizeof(detail),
                         " act=%d ref y=%.4f vy=%.4f flip=%d fr=%d | kid y=%.4f "
                         "vy=%.4f flip=%d fr=%d", want, it->second.y,
                         it->second.vy, it->second.flip, it->second.frame,
                         (double)g_refKidState[want].y,
                         (double)g_refKidState[want].vy,
                         (int)g_refKidState[want].flip,
                         (int)g_refKidState[want].frame);
            } else if (g_refKidFate[want] == 1) {
                gate = "kill";
                snprintf(detail, sizeof(detail), " rule=%s action=%d",
                         *g_refKidWhy[want] ? g_refKidWhy[want] : "(unnamed)",
                         want);
            } else {
                const int idx = refFindExact(nxt, g_refKidState[want]);
                if (idx >= 0) {
                    g_refParent = idx;      // carried, untouched
                    refPreCap = true;
                } else {
                    gate = "dedupe";
                    const int alt = refNearestInCell(nxt, g_refKidKey[want],
                                                     g_refKidState[want],
                                                     [t, &rules](const State& st) {
                                                         return modelKeyOf(st, (long long)t) ^ rules.keyBits(st);
                                                     });
                    int n = 0;
                    for (const State& s : nxt)
                        if ((modelKeyOf(s, (long long)t) ^ rules.keyBits(s)) == g_refKidKey[want]) ++n;
                    bool adopted = false;
                    for (size_t si = 0; si < nxt.size() && !adopted; ++si) {
                        if ((modelKeyOf(nxt[si], (long long)t) ^ rules.keyBits(nxt[si])) != g_refKidKey[want]) continue;
                        State a = nxt[si], b = g_refKidState[want];
                        // Which of the overlooked fields actually differ, by name.
                        std::string diff;
                        if (a.action != b.action) diff += " action";
                        if (std::memcmp(a.fireB, b.fireB, sizeof(a.fireB)) != 0) diff += " fireB";
                        if (a.trigT != b.trigT) diff += " trigT";
                        if (a.tight != b.tight) diff += " tight";
                        if (a.rot != b.rot || a.rotNeg != b.rotNeg || a.rotStep != b.rotStep
                            || a.rotBoost != b.rotBoost)
                            diff += " rot";
                        if (a.groundUid != b.groundUid) diff += " groundUid";
                        a.parent = b.parent = 0;
                        a.action = b.action = 0;
                        std::memcpy(a.fireB, b.fireB, sizeof(a.fireB));
                        a.trigT = b.trigT;
                        a.tight = b.tight;
                        const bool rotDiff = a.rot != b.rot || a.rotNeg != b.rotNeg
                                             || a.rotStep != b.rotStep || a.rotBoost != b.rotBoost;
                        a.rot = b.rot; a.rotNeg = b.rotNeg; a.rotStep = b.rotStep;
                        a.rotBoost = b.rotBoost;
                        // ...and --stickseam's tie (groundUid), which the key leaves out.
                        const bool tieDiff = a.groundUid != b.groundUid;
                        a.groundUid = b.groundUid;
                        (void)rotDiff; (void)tieDiff;
                        if (g_refAdoptMem) {
                            if (a.jumpBuf != b.jumpBuf) diff += " jumpBuf";
                            if (a.usedOrb != b.usedOrb || a.usedOrb2 != b.usedOrb2
                                || a.usedOrbOld != b.usedOrbOld || a.usedOrbOld2 != b.usedOrbOld2
                                || std::memcmp(a.usedOrbHist, b.usedOrbHist, sizeof(a.usedOrbHist)) != 0
                                || std::memcmp(a.usedOrbHist2, b.usedOrbHist2, sizeof(a.usedOrbHist2)) != 0)
                                diff += " usedOrb";
                            a.jumpBuf = b.jumpBuf;
                            a.usedOrb = b.usedOrb; a.usedOrb2 = b.usedOrb2;
                            a.usedOrbOld = b.usedOrbOld; a.usedOrbOld2 = b.usedOrbOld2;
                            std::memcpy(a.usedOrbHist, b.usedOrbHist, sizeof(a.usedOrbHist));
                            std::memcpy(a.usedOrbHist2, b.usedOrbHist2, sizeof(a.usedOrbHist2));
                        }
                        if (std::memcmp(&a, &b, sizeof(State)) == 0 && !g_refAdopt) {
                            std::printf("refwatch: merge t=%lld the survivor differs only in:%s "
                                        "(--refadopt would carry on with it)\n", t, diff.c_str());
                        } else if (std::memcmp(&a, &b, sizeof(State)) == 0) {
                            std::printf("refwatch: ADOPT t=%lld a SUBSTITUTE from here, differing in:%s\n",
                                        t, diff.c_str());
                            if (g_refAdoptT < 0) g_refAdoptT = t;
                            for (const char* fld : {"action", "fireB", "trigT", "tight", "rot", "groundUid",
                                                    "jumpBuf", "usedOrb"})
                                if (diff.find(std::string(" ") + fld) != std::string::npos
                                    && g_refAdoptFields.find(fld) == std::string::npos)
                                    g_refAdoptFields += std::string(" ") + fld;
                            g_refParent = (int)si;
                            refPreCap = true;
                            gate = nullptr;
                            adopted = true;
                        } else {
                            const unsigned char* pa = reinterpret_cast<const unsigned char*>(&a);
                            const unsigned char* pb = reinterpret_cast<const unsigned char*>(&b);
                            std::printf("refwatch: cell-mate %zu trig 0x%llx/0x%llx differs at:", si,
                                        (unsigned long long)nxt[si].trig.word(0),
                                        (unsigned long long)g_refKidState[want].trig.word(0));
                            int nd = 0;
                            for (size_t o = 0; o < sizeof(State) && nd < 16; ++o)
                                if (pa[o] != pb[o]) { std::printf(" %zu", o); ++nd; }
                            std::printf("\n");
                        }
                    }
                    if (adopted) {
                    } else if (alt < 0) {
                        snprintf(detail, sizeof(detail), " cell-gone");
                    } else {
                        g_refDriftY = std::fabs((double)nxt[(size_t)alt].y
                                                - (double)g_refKidState[want].y);
                        g_refDriftVy = std::fabs((double)nxt[(size_t)alt].vy
                                                 - (double)g_refKidState[want].vy);
                        snprintf(detail, sizeof(detail),
                                 " cell=%d action=%d ref y=%.4f vy=%.4f "
                                 "kept y=%.4f vy=%.4f", n, want,
                                 (double)g_refKidState[want].y,
                                 (double)g_refKidState[want].vy,
                                 (double)nxt[(size_t)alt].y,
                                 (double)nxt[(size_t)alt].vy);
                    }
                }
            }
            if (refPreCap) refCarried = nxt[(size_t)g_refParent];
            if (gate) {
                std::printf("refwatch: LOST t=%lld gate=%s%s "
                            "dy=%.3f dvy=%.3f alive=%zu\n",
                            t, gate, detail, g_refDriftY,
                            g_refDriftVy, nxt.size());
                g_refLostAt = t;
            }
        }
        maxAlive = std::max(maxAlive, nxt.size());
        const bool censusHere = g_searchCensus > 0 && ((t - t0) % g_searchCensus) == 0;
        if (g_searchCensus > 0) census.layer(nxt, (long long)t, curDxF, nBorn, nDied, censusHere);
        // --capmap: this layer's cap, from the leader x of the layer before it.
        const size_t capNow = capAtX(bestX);
        g_outcome.workStates += (long long)std::min(nxt.size(), capNow);
        const size_t qfPre = nxt.size();   // --qfoldwatch: the frontier before the alive cap
        std::vector<uint32_t> cellPool;
        std::vector<uint32_t> cellOf;
        bool overCap = nxt.size() > capNow;
        if (overCap && g_heldCellCap && g_heldAll) {
            const size_t n = nxt.size();
            std::vector<std::pair<uint64_t, uint32_t>> ck(n);
            auto keyAt = [&](size_t i) {
                State s = nxt[i];
                s.held = 0;
                s.jumpBuf = 0;
                ck[i] = {modelKeyOf(s, (long long)t) ^ rules.keyBits(s), (uint32_t)i};
            };
            if (pool && n >= 512) pool->parallelFor(n, keyAt);
            else for (size_t i = 0; i < n; ++i) keyAt(i);
            std::sort(ck.begin(), ck.end());
            cellOf.resize(n);
            for (size_t j = 0; j < n; ++j) {
                const uint32_t rep = (j > 0 && ck[j].first == ck[j - 1].first)
                                         ? cellOf[ck[j - 1].second] : ck[j].second;
                cellOf[ck[j].second] = rep;
                if (rep == ck[j].second) cellPool.push_back(rep);
            }
            std::sort(cellPool.begin(), cellPool.end());
            overCap = cellPool.size() > capNow;
            if (overCap) ++cellCuts;
            else ++cellSpared;
        }
        const bool cellCap = !cellOf.empty();
        if (overCap) {
            ++capHits;
            if (!cellCap) capDropped += (long long)(nxt.size() - capNow);
            if (!g_bandPath.empty() && !g_bands.empty() && g_bands.back().t == t)
                g_bands.back().capdrop = (int)((cellCap ? cellPool.size() : nxt.size()) - capNow);
            const bool bandClass = rules.bandClass();
            auto coinWinCell = [&rules](const State& s) -> uint64_t { return rules.winCell(s); };
            auto classOf = [bandClass, &coinWinCell, &rules](const State& s) {
                return coinWinCell(s) ^ ((uint64_t)s.mode << 32) ^ ((uint64_t)s.mini << 24)
                       ^ ((uint64_t)s.flip << 16) ^ ((uint64_t)s.dual << 8)
                       // the route's own class (CurrentRules::classBits: the coins taken)
                       ^ rules.classBits(s)
                       ^ ((bandClass && bandClamps(s)) ? ((uint64_t)(uint32_t)(int32_t)
                              std::lround((double)s.bandCeil) << 48) : 0)
                       ^ (bandClass ? (s.trig & g_actMask).word(0) * 0x94D049BB133111EBull
                                    : 0)
                       ^ (uint64_t)(uint32_t)std::lround(s.dx * 1000.0);
            };
            std::vector<uint64_t> cls;
            std::vector<std::vector<uint32_t>> byCls;
            const size_t popN = cellCap ? cellPool.size() : nxt.size();
            for (size_t p = 0; p < popN; ++p) {
                const uint32_t i = cellCap ? cellPool[p] : (uint32_t)p;
                const uint64_t k = classOf(nxt[i]);
                size_t j = 0;
                for (; j < cls.size(); ++j) if (cls[j] == k) break;
                if (j == cls.size()) { cls.push_back(k); byCls.push_back({}); }
                byCls[j].push_back(i);
            }
            std::vector<size_t> ord(byCls.size());
            for (size_t i = 0; i < ord.size(); ++i) ord[i] = i;
            std::stable_sort(ord.begin(), ord.end(), [&](size_t a, size_t b) {
                return byCls[a].size() < byCls[b].size();
            });
            std::vector<uint32_t> keepIdx;
            keepIdx.reserve(capNow);
            size_t left = capNow;
            for (size_t n = 0; n < ord.size(); ++n) {
                const std::vector<uint32_t>& v = byCls[ord[n]];
                const size_t fair = left / (ord.size() - n);
                const size_t take = std::min(v.size(), fair);
                if (take == v.size()) {
                    keepIdx.insert(keepIdx.end(), v.begin(), v.end());
                } else if (g_capEnvelope && take >= 3) {
                    // --capenvelope: the class's two extremes, then the stride over the rest.
                    auto lower = [&](uint32_t a, uint32_t b) {
                        if (nxt[a].y != nxt[b].y) return nxt[a].y < nxt[b].y;
                        if (nxt[a].vy != nxt[b].vy) return nxt[a].vy < nxt[b].vy;
                        return a < b;
                    };
                    size_t lo = 0, hi = 0;
                    for (size_t k = 1; k < v.size(); ++k) {
                        if (lower(v[k], v[lo])) lo = k;
                        if (lower(v[hi], v[k])) hi = k;
                    }
                    keepIdx.push_back(v[lo]);
                    keepIdx.push_back(v[hi]);
                    std::vector<uint32_t> rest;
                    rest.reserve(v.size() - 2);
                    for (size_t k = 0; k < v.size(); ++k)
                        if (k != lo && k != hi) rest.push_back(v[k]);
                    const double stride = (double)rest.size() / (double)(take - 2);
                    for (double i = 0; (size_t)i < rest.size(); i += stride)
                        keepIdx.push_back(rest[(size_t)i]);
                } else if (take > 0) {
                    const double stride = (double)v.size() / (double)take;
                    for (double i = 0; (size_t)i < v.size() && take > 0; i += stride)
                        keepIdx.push_back(v[(size_t)i]);
                }
                left -= std::min(left, take);
            }
            // --heldcellcap: every state of a sampled cell comes back with it, in layer order.
            auto withTwins = [&](std::vector<uint32_t>& idx) {
                if (!cellCap) return;
                std::vector<uint8_t> pick(nxt.size(), 0);
                for (uint32_t i : idx) pick[i] = 1;
                idx.clear();
                if (g_heldCellTwins <= 0) {
                    for (uint32_t i = 0; i < (uint32_t)nxt.size(); ++i)
                        if (pick[cellOf[i]]) idx.push_back(i);
                    return;
                }
                std::vector<uint8_t> kept(nxt.size(), 0);
                std::vector<int> nOf(nxt.size(), 0);
                for (int pass = 0; pass < 3; ++pass)
                    for (uint32_t i = 0; i < (uint32_t)nxt.size(); ++i) {
                        const uint32_t r = cellOf[i];
                        if (!pick[r] || kept[i] || nOf[r] >= g_heldCellTwins) continue;
                        if (pass == 0 && i != r) continue;
                        if (pass == 1 && nxt[i].held == nxt[r].held) continue;
                        kept[i] = 1;
                        ++nOf[r];
                        idx.push_back(i);
                    }
                std::sort(idx.begin(), idx.end());
            };
            const size_t cellsKept = keepIdx.size();
            withTwins(keepIdx);
            if (cellCap) {
                capDropped += (long long)(nxt.size() - keepIdx.size());
                cellTwinsKept += (long long)(keepIdx.size() - cellsKept);
            }
            std::sort(keepIdx.begin(), keepIdx.end());
            if (g_searchCensus > 0) census.cap(nxt.size(), capNow, nDied);
            if (censusHere) {
                std::vector<uint32_t> altIdx;
                size_t leftA = capNow;
                for (size_t n = 0; n < ord.size(); ++n) {
                    std::vector<uint32_t> v = byCls[ord[n]];
                    std::sort(v.begin(), v.end(), [&](uint32_t a, uint32_t b) {
                        if (nxt[a].y != nxt[b].y) return nxt[a].y < nxt[b].y;
                        if (nxt[a].vy != nxt[b].vy) return nxt[a].vy < nxt[b].vy;
                        return a < b;
                    });
                    const size_t fair = leftA / (ord.size() - n);
                    const size_t take = std::min(v.size(), fair);
                    if (take == v.size()) {
                        altIdx.insert(altIdx.end(), v.begin(), v.end());
                    } else if (take > 0) {
                        const double stride = (double)v.size() / (double)take;
                        for (double i = 0; (size_t)i < v.size() && take > 0; i += stride)
                            altIdx.push_back(v[(size_t)i]);
                    }
                    leftA -= std::min(leftA, take);
                }
                withTwins(altIdx);
                census.coverage(nxt, keepIdx, altIdx, curDxF);
            }
            std::vector<State> kept;
            kept.reserve(keepIdx.size());
            for (uint32_t i : keepIdx) kept.push_back(nxt[i]);
            nxt.swap(kept);
            if (g_refWatch && g_refLostAt < 0 && refPreCap) {
                const int idx = refFindExact(nxt, refCarried);
                if (idx < 0) {
                    std::printf("refwatch: LOST t=%lld gate=cap alive=%zu "
                                "cap=%zu\n",
                                t, nxt.size(), capNow);
                    g_refLostAt = t;
                } else {
                    g_refParent = idx;    // the cap reordered the layer
                }
            }
        }
        // --heldcellcap keeps more states than the cap counts: charge what the layer really keeps.
        if (cellCap) g_outcome.workStates += (long long)nxt.size() - (long long)std::min(qfPre, capNow);
        if (g_searchCensus > 0) census.kept(nxt);
        if (g_clearProbe && solved) measureGoalDiversity(nxt, arena, goalX);
        if (g_rjOn) {
            if ((long long)t == g_rjAfter) {
                const auto rr = g_rjRows.find((long long)t);
                const int oi = rr == g_rjRows.end() ? -1 : refFind(nxt, rr->second);
                g_rjOldNode = oi >= 0 ? nxt[(size_t)oi].parent : 0xffffffffu;
                std::printf("rejoin: the old path is %s the frontier at its death tick %lld\n",
                            oi >= 0 ? "in" : "NOT in", (long long)t);
            }
            const long long tt = (long long)t;
            const int ji = rejoinLayer(nxt, tt, [&](const State& s) {
                if (g_rjOldNode == 0xffffffffu) return false;
                uint32_t n = s.parent;
                for (long long k = tt; k > g_rjAfter; --k) n = arena[n].parent();
                return n == g_rjOldNode;
            }, [&](const State& s, const RefRow& r) {
                return !g_rjFull || (r.haveKey && (modelKeyOf(s, tt) ^ rules.keyBits(s)) == r.key);
            });
            const bool rjGoesOn = !g_rjRows.empty() && g_rjRows.rbegin()->first - tt >= kRjMinTail;
            const State* rjS = ji >= 0 ? &nxt[(size_t)ji] : nullptr;
            const bool rjCoins = rules.rejoinOk(rjS);
            if (g_rjUse && ji >= 0 && !solved && rjGoesOn && rjCoins) {
                solved = true;
                goalState = nxt[(size_t)ji];
                planCut = 0;
                g_rjJoinT = tt;
                g_outcome.rejoinT = tt;
                bestT = tt;
                std::printf("rejoin: JOINED the old plan at t=%lld (%lld ticks past its death), "
                            "stopping the search here\n", tt, tt - g_rjAfter);
                break;
            }
        }
        if (qfHere)
            std::printf("qfold: t=%lld frontier=%zu kept=%zu lostq=%lld keys=%lld\n",
                        (long long)t, qfPre, nxt.size(), qfLost, qfKeys);
        prevAlive = cur.size();
        cur.swap(nxt);
        nBornPrev = nBorn;
        nDiedPrev = nDied;
        double x = 0.0;
        for (const State& s : cur) x = std::max(x, (double)s.xAbs);
        if (!cur.empty()) { bestT = t; bestX = x; }
        if (cur.empty()) break;
        if (horizon > 0 && !solved && t - t0 >= horizon * 2) {
            solved = true;
            g_outcome.horizonCut = true;   // survived, did not reach the end (SearchOutcome)
            goalState = rules.pick(cur);
            planCut = t0 + horizon;
            break;
        }
        ppMark(3);
        if (g_gcNodes > 0 && !solved && arena.size() >= gcNext) {
            std::vector<bool> mark(arena.size(), false);
            mark[0] = true;
            auto markFrom = [&](const std::vector<State>& v) {
                for (const State& s : v)
                    for (uint32_t i = s.parent; i != 0 && !mark[i];
                         i = arena[i].parent())
                        mark[i] = true;
            };
            markFrom(cur);
            markFrom(nxt);   // the PARTIAL path reads nxt after the loop
            size_t keep = 0;
            for (size_t i = 0; i < arena.size(); ++i) keep += mark[i];
            std::vector<uint32_t> remap(arena.size(), 0);
            std::vector<Node> na;
            na.reserve(keep);
            std::vector<uint8_t> nm;
            if (checkWanted) nm.reserve(keep);
            for (size_t i = 0; i < arena.size(); ++i) {
                if (!mark[i]) continue;
                remap[i] = (uint32_t)na.size();
                na.push_back(Node{remap[arena[i].parent()]
                                  | ((uint32_t)arena[i].action() << 31)});
                if (checkWanted) nm.push_back(arenaMode[i]);
            }
            const size_t before = arena.size();
            arena.swap(na);
            na = std::vector<Node>();   // release the old allocation NOW
            if (checkWanted) {
                arenaMode.swap(nm);
                nm = std::vector<uint8_t>();
            }
            for (State& s : cur) s.parent = remap[s.parent];
            for (State& s : nxt) s.parent = remap[s.parent];
            if (g_rjOldNode != 0xffffffffu)
                g_rjOldNode = mark[g_rjOldNode] ? remap[g_rjOldNode] : 0xffffffffu;
            gcNext = std::max(g_gcNodes, arena.size() * 2);
            if (lpOn) g_lpc.snaps.clear();   // --ladderprefix: their arena sizes refer to the old numbering
            std::printf("gc t=%lld arena %zu -> %zu nodes (%.1f%% live)\n", t,
                        before, arena.size(),
                        100.0 * (double)arena.size() / (double)before);
            std::fflush(stdout);
        }
        ppMark(4);
        // ---- --ladderprefix: save the loop's state after this layer ----
        if (lpOn && !solved) {
            if (!lpBound && capHits > 0) lpBound = true;
            if (lpBound && g_ladderPrefixEvery > 0 && ((t - t0) % g_ladderPrefixEvery) == 0) {
                LadderPrefixSnap S;
                S.T = t;
                S.cur = cur;
                bool refsOk = true;
                S.objRefs.resize(cur.size() * 12);
                for (size_t i = 0; i < cur.size(); ++i) {
                    const State& s = cur[i];
                    uint64_t* r = &S.objRefs[i * 12];
                    r[0] = model.encodeObj(s.snapObj, refsOk);
                    r[1] = model.encodeObj(s.usedOrb, refsOk);
                    r[2] = model.encodeObj(s.snapObj2, refsOk);
                    r[3] = model.encodeObj(s.usedOrb2, refsOk);
                    for (int k = 0; k < 4; ++k) r[4 + k] = model.encodeObj(s.usedPad[k], refsOk);
                    for (int k = 0; k < 4; ++k) r[8 + k] = model.encodeObj(s.usedPad2[k], refsOk);
                }
                S.arenaSize = arena.size();
                S.arenaModeSize = arenaMode.size();
                S.gcNext = work.gcNext;
                S.nBornPrev = nBornPrev;
                S.nDiedPrev = nDiedPrev;
                S.prevAlive = prevAlive;
                S.bestT = bestT;
                S.bestX = bestX;
                S.goalState = goalState;
                S.solved = solved;
                S.maxAlive = maxAlive;
                S.capDropped = capDropped;
                S.capHits = capHits;
                S.cellCuts = cellCuts;
                S.cellSpared = cellSpared;
                S.cellTwinsKept = cellTwinsKept;
                S.census = census;
                S.twin = twin;
                S.workStates = g_outcome.workStates - lpWork0;
                S.autoTrig = g_autoTrig;
                S.rotTrig = g_rotTrig;
                S.carry.assign(4, {});
                S.carryOn.assign(4, 0);
                for (int f = 0; f < 4; ++f)
                    if (model.frameBound(f)) {
                        model.saveWorld(f, S.carry[(size_t)f]);
                        S.carryOn[(size_t)f] = 1;
                    }
                S.gfireGroups = g_gfireGroups;
                S.gfireSpread = g_gfireSpread;
                S.gfireSum = g_gfireSum;
                S.gfireMax = g_gfireMax;
                S.groupXSplitMax = g_groupXSplitMax;
                S.rjJoinT = g_rjJoinT;
                S.rjOldNode = g_rjOldNode;
                S.layerXSize = g_lpc.layerX.size();
                if (refsOk) {   // a pointer outside the level's lists: this layer is not saved
                    if (g_lpc.snaps.size() >= g_ladderPrefixMaxSnaps) g_lpc.snaps.erase(g_lpc.snaps.begin());
                    g_lpc.snaps.push_back(std::move(S));
                }
            }
        }
        if (g_memLimitMiB > 0 && !solved && (t & 127) == 0 && t - t0 > 2) {
            const size_t est = arena.capacity() * sizeof(Node) + arenaMode.capacity()
                               + (cur.capacity() + nxt.capacity()) * sizeof(State)
                               + seen.bucket_count() * 8 + seen.size() * 48;
            if (est > g_memLimitMiB * (size_t)1048576) {
                solved = true;
                goalState = rules.pick(cur);
                planCut = t0 + (t - t0) / 2;
                std::printf("MEMORY_LIMIT: est=%zuMiB arena=%zu alive=%zu "
                            "t=%lld x=%.0f -> plan cut at t=%lld "
                            "(half the searched depth)\n",
                            est >> 20, arena.size(), cur.size(), t, x,
                            planCut);
                break;
            }
        }
        if (checkWanted && !solved && !cur.empty() && isCheckpointLayer(t - t0)) {
            std::vector<uint8_t> plvl, pmode;
            plvl.reserve((size_t)(t - t0));
            pmode.reserve((size_t)(t - t0));
            for (uint32_t i = cur.front().parent; i != 0; i = arena[i].parent()) {
                plvl.push_back(arena[i].action());
                pmode.push_back(arenaMode[i]);
            }
            std::reverse(plvl.begin(), plvl.end());
            std::reverse(pmode.begin(), pmode.end());
            SearchCheckpoints::Point p;
            p.t0 = t0;
            p.tick = t0 + (long long)plvl.size();
            for (const PlanEdge& e :
                 planEdges(plvl, pmode, t0, (int)init.held, init.mode))
                p.edges.emplace_back(e.press, e.level);
            g_check.publish(std::move(p));
        }
        if (g_check.cancel.load(std::memory_order_relaxed)) {
            std::printf("CANCELLED: at t=%lld (search abandoned by the caller)\n", t);
            std::fflush(stdout);
            g_outcome.verdict = VerdictCancelled;
            g_outcome.cancelT = t;
            g_outcome.deepT = bestT;
            g_outcome.deepX = bestX;
            return 3;    // distinct from FAILED (1) and from --replay's unreadable plan (2)
        }
        if (g_ladderStop.load(std::memory_order_relaxed)) {
            std::printf("STOPPED: at t=%lld (the ladder took the plain search beside it)\n", t);
            std::fflush(stdout);
            g_outcome.verdict = VerdictCancelled;
            g_outcome.cancelT = t;
            g_outcome.deepT = bestT;
            g_outcome.deepX = bestX;
            return 3;
        }
        ppMark(5);
        g_progress.layer(t, x, cur.size());
        const bool crashed = prevAlive >= 8 && cur.size() * 4 <= prevAlive;
        if ((t % 500) == 0 || crashed
            || (cur.size() <= 64 && (t % 10) == 0)) {
            double ylo = 1e9, yhi = -1e9;
            int nm[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            int nMini = 0;
            for (const State& s : cur) {
                ylo = std::min(ylo, (double)s.y);
                yhi = std::max(yhi, (double)s.y);
                if (s.mode < 8) ++nm[s.mode];
                if (s.mini) ++nMini;
            }
            double xlo = 1e18;
            std::vector<float> spds;
            for (const State& s : cur) {
                xlo = std::min(xlo, model.progress(s).x);
                const float d = (s.dx > 0.f) ? s.dx : curDxF;
                if (std::find(spds.begin(), spds.end(), d) == spds.end())
                    spds.push_back(d);
            }
            std::sort(spds.begin(), spds.end());
            char sp[64] = "";
            for (size_t i = 0, o = 0; i < spds.size() && o + 8 < sizeof sp; ++i)
                o += (size_t)std::snprintf(sp + o, sizeof sp - o, "%s%.2f",
                                           i ? "/" : "", (double)spds[i]);
            std::printf("t=%lld x=%.0f alive=%zu arena=%zu y=[%.0f,%.0f] "
                        "cbswrpg=%d/%d/%d/%d/%d/%d/%d/%d mini=%d dx=%s xlo=%.0f "
                        "born=%lld died=%lld merged=%lld\n",
                        t, x, cur.size(), arena.size(),
                        cur.empty() ? 0.0 : ylo, cur.empty() ? 0.0 : yhi,
                        nm[0], nm[1], nm[2], nm[3], nm[4], nm[5], nm[6], nm[7],
                        nMini, sp,
                        cur.empty() ? 0.0 : xlo, nBornPrev, nDiedPrev,
                        nBornPrev - nDiedPrev - (long long)cur.size());
            if (g_memStat) {
                std::vector<bool> mark(arena.size(), false);
                size_t reach = 0;
                for (const State& s : cur)
                    for (uint32_t i = s.parent; i != 0 && !mark[i];
                         i = arena[i].parent()) {
                        mark[i] = true;
                        ++reach;
                    }
                size_t nDual = 0, nMirror = 0;
                std::unordered_set<uint64_t> p1All, p1Mir, axes;
                for (const State& s : cur) {
                    if (!s.dual) continue;
                    ++nDual;
                    const bool flying = (s.mode == 1 || s.mode == 3);
                    const double ys = flying ? g_shipYq : g_cubeYq;
                    const double vs = flying ? g_shipVq : g_cubeVq;
                    const uint64_t c1 =
                        ((uint64_t)(uint32_t)(int32_t)std::lround(s.y * ys) << 32)
                        ^ (uint32_t)(int32_t)std::lround(s.vy * vs);
                    p1All.insert(c1);
                    // exact mirror: opposite gravity, opposite vy, both airborne
                    if (s.flip2 != s.flip && !s.grounded && !s.grounded2
                        && std::fabs((double)s.vy + (double)s.vy2) < 0.05) {
                        ++nMirror;
                        p1Mir.insert(c1);
                        // the split axis, 0.5 px bins: y2 = 2*axis - y
                        axes.insert((uint64_t)(int64_t)std::llround(
                            ((double)s.y + (double)s.y2)));
                    }
                }
                const double MiB = 1024.0 * 1024.0;
                std::printf(
                    "memstat t=%lld arena=%zu (%.0f MiB) reach=%zu (%.1f%%) "
                    "state=%zuB cur=%zu/%zu nxtcap=%zu seen=%zu/%zu\n",
                    t, arena.size(),
                    (double)(arena.size() * sizeof(Node)) / MiB, reach,
                    arena.empty() ? 0.0 : 100.0 * (double)reach / (double)arena.size(),
                    sizeof(State), cur.size(), cur.capacity(), nxt.capacity(),
                    seen.size(), seen.bucket_count());
                if (nDual)
                    std::printf(
                        "memstat-dual t=%lld dual=%zu p1cells=%zu product=%.1f "
                        "mirror=%zu (%.0f%%) p1mir=%zu axes=%zu\n",
                        t, nDual, p1All.size(),
                        (double)nDual / (double)p1All.size(), nMirror,
                        100.0 * (double)nMirror / (double)nDual, p1Mir.size(),
                        axes.size());
            }
            std::fflush(stdout);
        }
    }
    if (solved) {
        out.kind = SearchResult::Solved;
        out.goal = goalState;
    } else if (horizon > 0 && !nxt.empty()) {
        out.kind = SearchResult::Partial;
        out.goal = rules.pick(nxt);
    } else {
        out.kind = SearchResult::NoPlan;
        // after the final swap the previous layer's states sit in nxt
        out.lastLayer.assign(nxt.begin(), nxt.begin() + (std::ptrdiff_t)std::min<size_t>(nxt.size(), 12));
    }
    out.t = bestT;
    out.x = bestX;
    if (out.kind != SearchResult::NoPlan) planFromArena(arena, out.goal, out.inputs);
    if (lpOn) {   // --ladderprefix: the next attempt may start from this one's saves
        g_lpc.arena.swap(arena);
        g_lpc.arenaMode.swap(arenaMode);
        g_lpc.usable = true;
    }
    return -1;
}

template <class E, class Rules>
inline int searchBeam(E& e, const Rules& rules, SearchResult& out) {
    auto& t0 = e.in.t0;
    auto& horizon = e.in.horizon;
    auto& planCut = e.in.planCut;
    auto& tEnd = e.in.tEnd;
    LayerWork work(e.in.init);   // the frontier starts at the root
    auto& cur = work.cur;
    auto& nxt = work.nxt;
    SearchArena store(e.in.checkWanted);   // this method's states
    auto& arena = store.arena;
    auto& checkWanted = e.in.checkWanted;
    auto& arenaMode = store.arenaMode;
    auto& model = e.in.model;
    auto& bestT = e.outcome.bestT;
    auto& bestX = e.outcome.bestX;
    auto& goalState = e.outcome.goalState;
    auto& solved = e.outcome.solved;
    auto& maxAlive = e.outcome.maxAlive;
    auto& capHits = e.outcome.capHits;
    auto& capDropped = e.outcome.capDropped;
    auto& prevAlive = work.prevAlive;
    auto& curDxF = e.in.curDxF;
    const size_t B = (size_t)std::max(1, g_beamWidth);
    std::printf("search: beam, width %zu\n", B);
    struct Cand {
        State s;          // the child, its parent the PARENT's arena node until it is kept
        uint8_t action;
        double score;
    };
    std::vector<Cand> cand;
    std::vector<size_t> order, rank;
    std::unordered_set<uint64_t> seenKeys;
    // keep one candidate: its arena node, and the state that now owns it
    auto keep = [&](const Cand& c) -> State {
        arena.push_back(Node{c.s.parent | ((uint32_t)c.action << 31)});
        if (checkWanted) arenaMode.push_back(c.s.mode);
        State k = c.s;
        k.parent = (uint32_t)(arena.size() - 1);
        k.action = c.action;
        return k;
    };
    for (long long t = t0 + 1; t <= tEnd && !solved; ++t) {
        nxt.clear();
        cand.clear();
        seenKeys.clear();
        model.fireAutoTriggers(cur, t);
        order.resize(cur.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const Progress pa = model.progress(cur[a]), pb = model.progress(cur[b]);
            return pa.frame != pb.frame ? pa.frame < pb.frame : pa.x < pb.x;
        });
        long long goalAt = -1;   // index into cand of the goal found this tick
        for (size_t oi : order) {
            const State& s = cur[oi];
            const int frame = (int)s.frame;
            const bool rev = s.rev != 0;
            // a group of one: its window is its own advance, its doors and lock its own
            const double sdx = rev ? -(double)((s.dx > 0.f) ? s.dx : curDxF)
                                   : (double)((s.dx > 0.f) ? s.dx : curDxF);
            const float dxUsed = (float)sdx;
            const double xPrev = (double)s.xAbs;
            const double x = (double)advanceX(s.xAbs, dxUsed);
            const double wLo = std::min(xPrev, x), wHi = std::max(xPrev, x);
            model.fireRotTriggers(frame, rev, xPrev, xPrev, wLo, wHi, t);
            GroupEnv env;
            double pLo = wLo, pHi = wHi;
            p2Widen(pLo, pHi, p2Dx(s), p2Dx(s));   // --p2xoff
            model.prepare(env, frame, s.trig, (int)s.trigT, s.fireB, s.lockOff + (float)sdx, x, xPrev,
                          dxUsed, pLo, pHi, t, GroupOpts{true, true, false});
            const StepCtx K = env.ctx();
            for (int input = 0; input < 2; ++input) {
                if (!model.inputAllowed(s, input, 1, t)) continue;
                Child kid;
                bool dead = false;
                kid.s = model.advance(s, input, K, dead, nullptr);
                model.noteInput(s, kid.s, input);
                model.fireGates(kid.s, s.action, input, K, frame, t, dead);
                rules.afterStep(s, kid, input, K, frame, t, dead);
                if (dead) continue;
                if (!seenKeys.insert(modelKeyOf(kid.s, t) ^ rules.keyBits(kid.s)).second) continue;
                kid.s.parent = s.parent;
                cand.push_back(Cand{kid.s, (uint8_t)input, rules.score(kid.s)});
                if (rules.reached(kid.s)
                    && (goalAt < 0 ? !solved || rules.prefer(rules.routeScore(kid.s), rules.routeScore(goalState))
                                   : rules.prefer(rules.routeScore(kid.s), rules.routeScore(cand[(size_t)goalAt].s))))
                    goalAt = (long long)cand.size() - 1;
            }
        }
        if (goalAt >= 0) {
            goalState = keep(cand[(size_t)goalAt]);
            solved = true;
        }
        // the B best survive, ties in stepping order
        rank.resize(cand.size());
        for (size_t i = 0; i < rank.size(); ++i) rank[i] = i;
        std::stable_sort(rank.begin(), rank.end(),
                         [&](size_t a, size_t b) { return cand[a].score > cand[b].score; });
        if (cand.size() > maxAlive) maxAlive = cand.size();
        if (cand.size() > B) {
            ++capHits;
            capDropped += (long long)(cand.size() - B);
            rank.resize(B);
        }
        std::sort(rank.begin(), rank.end());   // the frontier in stepping order
        for (size_t i : rank) nxt.push_back(keep(cand[i]));
        prevAlive = cur.size();
        cur.swap(nxt);
        double xl = 0.0;
        for (const State& s : cur) xl = std::max(xl, model.progress(s).x);
        if (!cur.empty()) { bestT = t; bestX = xl; }
        if (cur.empty()) break;
        // the caller's horizon, as searchLayers keeps it: search on to twice the horizon, then cut
        if (horizon > 0 && !solved && t - t0 >= horizon * 2) {
            solved = true;
            g_outcome.horizonCut = true;
            goalState = rules.pick(cur);
            planCut = t0 + horizon;
            break;
        }
        if (g_check.cancel.load(std::memory_order_relaxed)
            || g_ladderStop.load(std::memory_order_relaxed)) {
            std::printf("CANCELLED: at t=%lld (beam search abandoned by the caller)\n", t);
            std::fflush(stdout);
            g_outcome.verdict = VerdictCancelled;
            g_outcome.cancelT = t;
            g_outcome.deepT = bestT;
            g_outcome.deepX = bestX;
            return 3;
        }
    }
    if (solved) {
        out.kind = SearchResult::Solved;
        out.goal = goalState;
    } else if (horizon > 0 && !nxt.empty()) {
        out.kind = SearchResult::Partial;
        out.goal = rules.pick(nxt);
    } else {
        out.kind = SearchResult::NoPlan;
        // after the final swap the previous layer's states sit in nxt
        out.lastLayer.assign(nxt.begin(), nxt.begin() + (std::ptrdiff_t)std::min<size_t>(nxt.size(), 12));
    }
    out.t = bestT;
    out.x = bestX;
    if (out.kind != SearchResult::NoPlan) planFromArena(arena, out.goal, out.inputs);
    return -1;
}

struct SearchRun {
    bool open = false;
    int level = -1;
    uint64_t seed = 0;
    long long verified = 0;   // flights reported
    long long own = 0;        // ...of which flew the plan this core emitted last
    long long calls = 0;      // search calls so far (the call index a seeded search mixes in)
    long long nondet = 0;     // ...of which stopped other than on their own budget (not reproducible)
    long long lastT0 = -1;    // the plan this core emitted last (cliMainOnce): its anchor...
    std::vector<std::pair<long long, int>> lastEdges;   // ...and its input edges (press, level)
    long long spliceT0 = -1;
    std::vector<std::pair<long long, int>> spliceEdges;
    long long viaSplice = 0;  // flights reported that flew it
};
inline SearchRun g_run;

inline bool runBegin(int level, uint64_t seed, std::string& why) {
    if (g_run.open) {
        why = "a run is still open (level " + std::to_string(g_run.level) + ", "
              + std::to_string(g_run.verified) + " flights reported) at the start of level "
              + std::to_string(level);
        return false;
    }
    g_run = SearchRun{};
    g_run.open = true;
    g_run.level = level;
    g_run.seed = seed;
    return true;
}
inline bool runNotePlanFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::vector<std::pair<long long, int>> e;
    std::string ln;
    while (std::getline(in, ln)) {
        long long press = 0;
        int level = 0;
        if (std::sscanf(ln.c_str(), "input=%lld,%d", &press, &level) == 2) e.emplace_back(press, level);
    }
    g_run.lastEdges = std::move(e);
    return true;
}
inline bool runFlewPlan(const std::vector<std::pair<long long, int>>& flown, long long t0,
                        const std::vector<std::pair<long long, int>>& edges) {
    if (t0 < 0) return false;
    using Edge = std::pair<long long, int>;
    auto byTick = [](const Edge& a, const Edge& b) { return a.first < b.first; };
    std::vector<Edge> fl(flown.begin(), flown.end()), em(edges.begin(), edges.end());
    std::stable_sort(fl.begin(), fl.end(), byTick);
    std::stable_sort(em.begin(), em.end(), byTick);
    long long cut = t0;
    if (!em.empty()) cut = std::min(cut, em.front().first);
    int start = 0;   // the button as the flown plan left it just before the seam
    for (const Edge& e : fl)
        if (e.first < cut) start = e.second;
    auto changes = [&](const std::vector<Edge>& v) {   // only the edges that change the level
        std::vector<Edge> out;
        int lvl = start;
        for (const Edge& e : v) {
            if (e.first < cut || e.second == lvl) continue;
            lvl = e.second;
            out.push_back(e);
        }
        return out;
    };
    return changes(fl) == changes(em);
}
inline bool runOwnPlan(const std::vector<std::pair<long long, int>>& flown) {
    return runFlewPlan(flown, g_run.lastT0, g_run.lastEdges);
}
inline void runNoteSplice(const std::vector<std::pair<long long, int>>& edges, long long t0) {
    if (!g_run.open) return;
    g_run.spliceT0 = t0;
    g_run.spliceEdges = edges;
}
inline std::string runVerified(const std::vector<std::pair<long long, int>>& flown, long long deathT,
                               double x, bool cleared, long long divT) {
    if (!g_run.open) return "dprun: verified with no run open";
    const bool own = runOwnPlan(flown);
    const bool splice = !own && runFlewPlan(flown, g_run.spliceT0, g_run.spliceEdges);
    ++g_run.verified;
    if (own) ++g_run.own;
    if (splice) ++g_run.viaSplice;
    char b[240];
    std::snprintf(b, sizeof b,
                  "dprun: verified %s t=%lld x=%.1f diverged=%lld own=%d (%lld of %lld) nondet=%lld%s",
                  cleared ? "cleared" : "death", deathT, x, divT, own ? 1 : 0, g_run.own,
                  g_run.verified, g_run.nondet, splice ? " via=secsolve" : "");
    return b;
}
inline std::string runEnd() {
    if (!g_run.open) return "dprun: end (no run open)";
    const std::string s = "dprun: end level=" + std::to_string(g_run.level) + " flights="
                          + std::to_string(g_run.verified) + " own=" + std::to_string(g_run.own)
                          + " calls=" + std::to_string(g_run.calls)
                          + " nondet=" + std::to_string(g_run.nondet)
                          + (g_run.viaSplice ? " via-secsolve=" + std::to_string(g_run.viaSplice)
                                             : std::string());
    g_run = SearchRun{};
    return s;
}

inline uint64_t mcMix(uint64_t z) {   // splitmix64's finaliser
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
template <class E, class Rules>
inline int searchMontecarlo(E& e, const Rules& rules, SearchResult& out) {
    auto& t0 = e.in.t0;
    auto& init = e.in.init;
    auto& tEnd = e.in.tEnd;
    SearchArena store(e.in.checkWanted);   // this method's states
    auto& arena = store.arena;
    auto& checkWanted = e.in.checkWanted;
    auto& arenaMode = store.arenaMode;
    auto& model = e.in.model;
    auto& bestT = e.outcome.bestT;
    auto& bestX = e.outcome.bestX;
    auto& goalState = e.outcome.goalState;
    auto& solved = e.outcome.solved;
    auto& curDxF = e.in.curDxF;
    auto& horizon = e.in.horizon;
    auto& planCut = e.in.planCut;
    const long long hzEnd = horizon > 0 ? t0 + horizon * 2 : -1;
    // (...nor a world with item gates, which its walks never fire: Model::fireGates.)
    if (!g_rotTrig.empty() || !g_ctrlWin.empty() || (g_ctrlWinP2 && !g_ctrlWin2.empty())
        || init.frame != 0 || rules.needsAfterStep() || model.hasGates()) {
        std::printf("search: montecarlo declines (rotation=%zu control-windows=%zu frame=%d "
                    "coins=%d%s)\n", g_rotTrig.size(),
                    g_ctrlWin.size() + (g_ctrlWinP2 ? g_ctrlWin2.size() : 0), (int)init.frame,
                    rules.needsAfterStep() ? 1 : 0,
                    model.hasGates() && !rules.needsAfterStep() ? " gates=1" : "");
        out.kind = SearchResult::Declined;
        return -1;
    }
    const uint64_t seed = g_run.open ? g_run.seed : (uint64_t)g_searchSeed;
    const long long call = g_mcCall >= 0 ? g_mcCall : g_run.calls;
    std::mt19937_64 rng(mcMix(seed ^ mcMix((uint64_t)t0 ^ mcMix((uint64_t)call))));
    std::printf("search: montecarlo seed=%llu t0=%lld call=%lld rollouts=%d window=%d toggles=%d "
                "steps=%lld stall=%lld\n", (unsigned long long)seed, t0, call, g_mcRollouts,
                g_mcWindow, g_mcToggles, g_mcSteps, g_mcStall);
    using Clock = std::chrono::steady_clock;
    const auto mcStart = Clock::now();
    auto elapsedS = [&] { return std::chrono::duration<double>(Clock::now() - mcStart).count(); };
    auto latOf = [](uint8_t m) { return (m == 1 || m == 3) ? 2 : 1; };
    struct McNode {
        State s;
        int curIn = 0;
        int held = 0;
        int nfx = 0;
        long long fxT[4] = {0, 0, 0, 0};
        int fxV[4] = {0, 0, 0, 0};
        bool pressed = false;   // the button changed on this tick
    };
    long long steps = 0;
    auto press = [&](McNode& n, long long t) {
        n.held ^= 1;
        n.pressed = true;
        if (n.nfx < 4) {
            n.fxT[n.nfx] = t + latOf(n.s.mode);
            n.fxV[n.nfx] = n.held;
            ++n.nfx;
        }
    };
    auto stepTick = [&](McNode& n, long long t) -> bool {
        ++steps;
        n.pressed = false;
        int k2 = 0;   // effects due now, in scheduling order (a later press wins a tie)
        for (int k = 0; k < n.nfx; ++k) {
            if (n.fxT[k] <= t) n.curIn = n.fxV[k];
            else { n.fxT[k2] = n.fxT[k]; n.fxV[k2] = n.fxV[k]; ++k2; }
        }
        n.nfx = k2;
        State& s = n.s;
        const float dxU = ((s.dx > 0.f) ? s.dx : curDxF) * (s.rev ? -1.f : 1.f);
        const double xPrev = (double)s.xAbs;
        const double x = (double)advanceX(s.xAbs, dxU);
        for (AutoTrig& A : g_autoTrig) {   // autonomous fires on this walk's own crossing
            if (A.fireT >= 0) continue;
            if (x < A.cx) break;   // sorted by cx
            A.fireT = (int)t + A.delay;
            A.fireX = A.lockLastX = x;
        }
        GroupEnv env;
        double pLo = x, pHi = x;
        p2Widen(pLo, pHi, p2Dx(s), p2Dx(s));   // --p2xoff
        model.prepare(env, (int)s.frame, s.trig, (int)s.trigT, s.fireB,
                      s.lockOff + (float)(x - xPrev), x, xPrev, dxU, pLo, pHi, t,
                      GroupOpts{false, false, false});
        const StepCtx K = env.ctx();
        g_nearOrb = 0;
        g_dashVySet = 0;
        bool dead = false;
        State c = model.advance(s, n.curIn, K, dead, nullptr);
        c.action = (uint8_t)n.curIn;
        s = c;
        return dead;
    };
    auto walk = [&](const McNode& from, long long T, const std::vector<long long>& flips,
                    long long until, bool& goal, std::vector<McNode>* keep) -> long long {
        McNode n = from;
        size_t fi = 0;
        goal = false;
        model.forgetWorld((int)from.s.frame);
        while (fi < flips.size() && flips[fi] == T) { press(n, T); ++fi; }
        for (long long t = T + 1; t <= until; ++t) {
            if (stepTick(n, t)) return t - 1;
            while (fi < flips.size() && flips[fi] == t) { press(n, t); ++fi; }
            if (keep) keep->push_back(n);
            if (rules.reached(n.s) || (hzEnd > 0 && t >= hzEnd)) { goal = true; return t; }
        }
        return until;
    };
    std::vector<McNode> path;   // path[k] = the state after tick t0 + k
    {
        McNode n0;
        n0.s = init;
        n0.curIn = (int)init.held;
        n0.held = (int)init.held;
        path.push_back(n0);
    }
    auto commit = [&](long long T, const std::vector<long long>& fl, long long upto) {
        McNode& head = path.back();
        std::vector<long long> rest;
        for (long long p : fl) {
            if (p == T) press(head, T);
            else rest.push_back(p);
        }
        bool g = false;
        const McNode start = head;
        walk(start, T, rest, upto, g, &path);
    };
    struct AutoInit { int fireT; double fireX, lockLastX; };
    std::vector<AutoInit> autoInit;
    for (const AutoTrig& A : g_autoTrig) autoInit.push_back({A.fireT, A.fireX, A.lockLastX});
    auto resetAuto = [&](long long T) {
        const size_t last = (size_t)(T - t0);   // path[0..last] is committed
        for (size_t i = 0; i < g_autoTrig.size(); ++i) {
            AutoTrig& A = g_autoTrig[i];
            if (autoInit[i].fireT >= 0) {
                A.fireT = autoInit[i].fireT;
                A.fireX = autoInit[i].fireX;
                A.lockLastX = autoInit[i].lockLastX;
                continue;
            }
            size_t a = 1, b = last + 1;   // the first k in [1, last] with x >= cx
            while (a < b) {
                const size_t mid = (a + b) / 2;
                if ((double)path[mid].s.xAbs < A.cx) a = mid + 1; else b = mid;
            }
            if (a <= last) {
                A.fireT = (int)(t0 + (long long)a) + A.delay;
                A.fireX = A.lockLastX = (double)path[a].s.xAbs;
            } else {
                A.fireT = -1;
            }
        }
    };
    constexpr int kMcLanes = 32;
    struct Lane {
        McNode n;
        size_t fi = 0;   // the next flip
        long long lived = 0, steps = 0;
        bool goal = false, done = false;
        int cls = 0;
    };
    struct LaneClass {
        std::vector<AutoInit> autoSt;   // the autonomous triggers as this class's walk left them
        std::vector<double> carry;      // its world after its last tick (Model::saveWorld)
        bool hasCarry = false;
    };
    std::vector<Lane> lanes((size_t)kMcLanes);
    std::vector<std::vector<long long>> laneFlips((size_t)kMcLanes);
    auto saveAuto = [&](std::vector<AutoInit>& v) {
        v.resize(g_autoTrig.size());
        for (size_t i = 0; i < g_autoTrig.size(); ++i)
            v[i] = {g_autoTrig[i].fireT, g_autoTrig[i].fireX, g_autoTrig[i].lockLastX};
    };
    auto loadAuto = [&](const std::vector<AutoInit>& v) {
        for (size_t i = 0; i < g_autoTrig.size() && i < v.size(); ++i) {
            g_autoTrig[i].fireT = v[i].fireT;
            g_autoTrig[i].fireX = v[i].fireX;
            g_autoTrig[i].lockLastX = v[i].lockLastX;
        }
    };
    // What a lane hands Model::prepare on a tick, beyond its state's own fields.
    struct LanePre { float dxU = 0.f, lockAt = 0.f; double xPrev = 0.0, x = 0.0; };
    std::vector<LanePre> pre((size_t)kMcLanes);
    std::vector<LaneClass> cls, nxt;
    std::vector<int> act, rep, prevOf, kids;
    auto runLanes = [&](const McNode& from, long long T, long long until, size_t nl) {
        const int f = (int)from.s.frame;
        cls.assign(1, LaneClass{});
        resetAuto(T);
        saveAuto(cls[0].autoSt);
        for (size_t l = 0; l < nl; ++l) {
            Lane& L = lanes[l];
            L = Lane{};
            L.n = from;
            const std::vector<long long>& fl = laneFlips[l];
            while (L.fi < fl.size() && fl[L.fi] == T) { press(L.n, T); ++L.fi; }
        }
        model.forgetWorld(f);
        int held = -1;
        long long heldT = T;
        auto sameInputs = [&](size_t a, size_t b) {
            const State& sa = lanes[a].n.s;
            const State& sb = lanes[b].n.s;
            return pre[a].x == pre[b].x && pre[a].xPrev == pre[b].xPrev && pre[a].dxU == pre[b].dxU
                   && pre[a].lockAt == pre[b].lockAt && sa.frame == sb.frame && sa.trig == sb.trig
                   && sa.trigT == sb.trigT && !std::memcmp(sa.fireB, sb.fireB, sizeof sa.fireB);
        };
        for (long long t = T + 1; t <= until; ++t) {
            act.clear();
            for (size_t l = 0; l < nl; ++l)
                if (!lanes[l].done) act.push_back((int)l);
            if (act.empty()) break;
            // Each lane's own half of stepTick: the effects due now, and where it goes.
            for (int l : act) {
                Lane& L = lanes[(size_t)l];
                McNode& n = L.n;
                ++L.steps;
                n.pressed = false;
                int k2 = 0;
                for (int k = 0; k < n.nfx; ++k) {
                    if (n.fxT[k] <= t) n.curIn = n.fxV[k];
                    else { n.fxT[k2] = n.fxT[k]; n.fxV[k2] = n.fxV[k]; ++k2; }
                }
                n.nfx = k2;
                const State& s = n.s;
                LanePre& P = pre[(size_t)l];
                P.dxU = ((s.dx > 0.f) ? s.dx : curDxF) * (s.rev ? -1.f : 1.f);
                P.xPrev = (double)s.xAbs;
                P.x = (double)advanceX(s.xAbs, P.dxU);
                P.lockAt = s.lockOff + (float)(P.x - P.xPrev);
            }
            // This tick's classes: a lane joins the first class of its parent with the same inputs.
            nxt.clear();
            rep.clear();
            prevOf.clear();
            for (int l : act) {
                Lane& L = lanes[(size_t)l];
                int c = 0;
                while (c < (int)rep.size()
                       && !(prevOf[(size_t)c] == L.cls && sameInputs((size_t)rep[(size_t)c], (size_t)l)))
                    ++c;
                if (c == (int)rep.size()) {
                    rep.push_back(l);
                    prevOf.push_back(L.cls);
                    nxt.emplace_back();
                }
                L.cls = c;   // (this tick's numbering from here on)
            }
            // A parent with several children has its world saved now, while the model holds it.
            kids.assign(cls.size(), 0);
            for (int p : prevOf) ++kids[(size_t)p];
            if (held >= 0 && heldT == t - 1 && kids[(size_t)held] > 1 && !cls[(size_t)held].hasCarry) {
                model.saveWorld(f, cls[(size_t)held].carry);
                cls[(size_t)held].hasCarry = true;
            }
            for (size_t c = 0; c < rep.size(); ++c) {
                const int p = prevOf[c];
                const Lane& R = lanes[(size_t)rep[c]];
                const LanePre& P = pre[(size_t)rep[c]];
                if (!(held == p && heldT == t - 1)) {
                    if (cls[(size_t)p].hasCarry) model.loadWorld(f, cls[(size_t)p].carry, t - 1);
                    else model.forgetWorld(f);   // class 0 at T
                }
                loadAuto(cls[(size_t)p].autoSt);
                for (AutoTrig& A : g_autoTrig) {   // autonomous fires on this class's own crossing
                    if (A.fireT >= 0) continue;
                    if (P.x < A.cx) break;   // sorted by cx
                    A.fireT = (int)t + A.delay;
                    A.fireX = A.lockLastX = P.x;
                }
                GroupEnv env;
                double pLo = P.x, pHi = P.x;
                p2Widen(pLo, pHi, p2Dx(R.n.s), p2Dx(R.n.s));   // --p2xoff
                model.prepare(env, (int)R.n.s.frame, R.n.s.trig, (int)R.n.s.trigT, R.n.s.fireB,
                              P.lockAt, P.x, P.xPrev, P.dxU, pLo, pHi, t,
                              GroupOpts{false, false, false});
                const StepCtx K = env.ctx();
                saveAuto(nxt[c].autoSt);
                held = (int)c;
                heldT = t;
                if (rep.size() > 1) {
                    model.saveWorld(f, nxt[c].carry);
                    nxt[c].hasCarry = true;
                }
                for (int l : act) {
                    Lane& L = lanes[(size_t)l];
                    if (L.cls != (int)c) continue;
                    g_nearOrb = 0;
                    g_dashVySet = 0;
                    bool dead = false;
                    State s2 = model.advance(L.n.s, L.n.curIn, K, dead, nullptr);
                    s2.action = (uint8_t)L.n.curIn;
                    L.n.s = s2;
                    if (dead) { L.done = true; L.lived = t - 1; continue; }
                    const std::vector<long long>& fl = laneFlips[(size_t)l];
                    while (L.fi < fl.size() && fl[L.fi] == t) { press(L.n, t); ++L.fi; }
                    if (rules.reached(L.n.s) || (hzEnd > 0 && t >= hzEnd)) {
                        L.done = true;
                        L.goal = true;
                        L.lived = t;
                    } else if (t == until) {
                        L.done = true;
                        L.lived = until;
                    }
                }
            }
            cls.swap(nxt);
        }
    };
    long long deepest = t0, back = 5, rounds = 0, rollouts = 0, backoffs = 0;
    // The rollout count when deepest last grew, and the longest run of rollouts without it.
    long long gainAt = 0, maxGap = 0;
    bool reached = false, valve = false, stalled = false;
    std::vector<long long> flips, bestFlips, f2;
    auto stopAsked = [&] {
        return g_check.cancel.load(std::memory_order_relaxed)
               || g_ladderStop.load(std::memory_order_relaxed);
    };
    auto cancelled = [&] {
        const long long t = t0 + (long long)path.size() - 1;
        std::printf("CANCELLED: at t=%lld (montecarlo search abandoned by the caller)\n", t);
        std::fflush(stdout);
        g_outcome.verdict = VerdictCancelled;
        g_outcome.cancelT = t;
        g_outcome.deepT = t;
        g_outcome.deepX = (double)path.back().s.xAbs;
        return 3;
    };
    while (true) {
        if (stopAsked()) return cancelled();
        if (steps >= g_mcSteps) break;
        if (g_mcStall > 0 && rollouts - gainAt >= g_mcStall) { stalled = true; break; }
        if (elapsedS() > g_mcSafetyS) { valve = true; break; }
        ++rounds;
        const long long T = t0 + (long long)path.size() - 1;
        if (T >= tEnd) break;
        const McNode from = path.back();
        long long best = T;
        bool bestGoal = false;
        bestFlips.clear();
        const long long until = std::min(T + (long long)g_mcWindow, tEnd);
        std::uniform_int_distribution<long long> pick(T, until - 1);
        auto draw = [&](std::vector<long long>& out) {
            flips.clear();
            for (int k = 0; k < g_mcToggles; ++k) flips.push_back(pick(rng));
            std::sort(flips.begin(), flips.end());
            out.clear();   // two flips on one tick cancel: keep a tick only if odd
            for (size_t i = 0; i < flips.size();) {
                size_t j = i;
                while (j < flips.size() && flips[j] == flips[i]) ++j;
                if ((j - i) & 1) out.push_back(flips[i]);
                i = j;
            }
        };
        bool roundDone = false;
        for (int r0 = 0; r0 < g_mcRollouts && !roundDone; r0 += kMcLanes) {
            if (r0 > 0 && stopAsked()) return cancelled();
            const int nl = std::min(kMcLanes, g_mcRollouts - r0);
            const std::mt19937_64 rngAt = rng;
            const auto pickAt = pick;
            for (int l = 0; l < nl; ++l) draw(laneFlips[(size_t)l]);
            runLanes(from, T, until, (size_t)nl);
            for (int l = 0; l < nl; ++l) {
                const Lane& L = lanes[(size_t)l];
                ++rollouts;
                steps += L.steps;
                if (L.goal || L.lived > best) {
                    best = L.lived;
                    bestFlips = laneFlips[(size_t)l];
                    bestGoal = L.goal;
                }
                if (bestGoal || best - T > g_mcWindow / 2) {
                    rng = rngAt;
                    pick = pickAt;
                    for (int k = 0; k <= l; ++k) draw(f2);
                    roundDone = true;
                    break;
                }
            }
        }
        if (bestGoal) {
            resetAuto(T);
            commit(T, bestFlips, best);
            reached = true;
            maxGap = std::max(maxGap, rollouts - gainAt);
            break;
        }
        if (best - T >= 3) {
            const long long commitT = T + (best - T) / 3;
            resetAuto(T);
            commit(T, bestFlips, commitT);
            if (commitT > deepest) {
                deepest = commitT;
                back = 5;
                maxGap = std::max(maxGap, rollouts - gainAt);
                gainAt = rollouts;
            }
            continue;
        }
        ++backoffs;   // stuck: back off, further each time, never past the start
        const long long keepT = std::max(t0, T - back);
        path.resize((size_t)(keepT - t0) + 1);
        back += (back > 100) ? 50 : 5;
        if (back > 20000) back = 5;
    }
    const long long lastT = t0 + (long long)path.size() - 1;
    resetAuto(lastT);   // the witness walks the committed path, with its triggers' firing
    if (!reached) maxGap = std::max(maxGap, rollouts - gainAt);
    std::printf("search: montecarlo %s at t=%lld x=%.1f in %.3fs rounds=%lld rollouts=%lld "
                "backoffs=%lld steps=%lld deepest=%lld maxgap=%lld%s\n",
                reached ? "SOLVED" : "GAVE UP", lastT, (double)path.back().s.xAbs, elapsedS(),
                rounds, rollouts, backoffs, steps, deepest, maxGap,
                valve ? " -- stopped by the wall-clock safety valve: NOT deterministic"
                      : stalled ? " -- stalled: no deeper commit in --mcstall rollouts" : "");
    // The committed path into the arena: one node per tick, its action the input in effect.
    uint32_t parent = init.parent;
    for (size_t k = 1; k < path.size(); ++k) {
        arena.push_back(Node{parent | ((uint32_t)(path[k].curIn & 1) << 31)});
        if (checkWanted) arenaMode.push_back(path[k].s.mode);
        parent = (uint32_t)(arena.size() - 1);
        path[k].s.parent = parent;
        path[k].s.action = (uint8_t)path[k].curIn;
    }
    out.deterministic = !valve;
    bestT = lastT;
    bestX = (double)path.back().s.xAbs;
    out.t = bestT;
    out.x = bestX;
    if (reached) {
        solved = true;
        goalState = path.back().s;
        out.kind = SearchResult::Solved;
        out.goal = goalState;
        if (!rules.reached(goalState)) {   // alive at twice the horizon, not at the goal
            g_outcome.horizonCut = true;
            planCut = t0 + horizon;
            std::printf("search: montecarlo alive at t=%lld = t0 + 2 x horizon; the plan is cut "
                        "to t=%lld\n", lastT, planCut);
        }
    } else if (path.size() > 1) {
        out.kind = SearchResult::Partial;
        out.goal = path.back().s;
    } else {
        out.kind = SearchResult::NoPlan;
    }
    if (out.kind != SearchResult::NoPlan) planFromArena(arena, out.goal, out.inputs);
    return -1;
}
}  // namespace dp
