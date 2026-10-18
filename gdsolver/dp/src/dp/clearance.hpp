#pragma once
#include "dp/step.hpp"

namespace dp {
inline bool g_clearProbe = false;

constexpr double kTightPx = 4.0;
inline double g_tightPx = kTightPx;

constexpr double kClearLimit = 64.0;

inline double hazardHalfFor(int mode, int mini) {
    if (mode == 4) return mini ? kWaveHazHalfMini : kWaveHalf;
    return playerHalf((uint8_t)mode, mini != 0);
}

inline float solidClearance(const std::vector<const Obj*>& near, double px,
                            double py, double half, long long* skipped) {
    const double pl = px - half, pr = px + half;
    const double pb = py - half, pt = py + half;
    double best = kClearLimit, worst = 0.0;
    bool embedded = false;
    for (const Obj* o : near) {
        if (o->type != 0) continue;
        if (o->slope) { if (skipped) ++*skipped; continue; }
        const double l = o->cx - o->hw, r = o->cx + o->hw;
        if (r <= pl || l >= pr) continue;              // no x overlap: not in the way
        const double b = o->cy - o->hh, tt = o->cy + o->hh;
        if (tt <= pb) {                                 // below the player
            best = std::min(best, pb - tt);
        } else if (b >= pt) {                           // above it
            if (o->oneway) continue;                    // does not block from below
            best = std::min(best, b - pt);
        } else {                                        // overlapping in y
            if (o->oneway) continue;                    // walks straight through
            embedded = true;
            worst = std::max(worst, std::min(tt - pb, pt - b));
        }
    }
    return (float)(embedded ? -worst : best);
}

inline float hazardClearance(const std::vector<const Obj*>& near, double px,
                             double py, double half, int mode, int mini) {
    auto hit = [&](const Obj* o, double y) {
        return hazardHit(o, px, y, half, kHazMargin, sawMarginFor((uint8_t)mode),
                         mode, mini);
    };
    double best = kClearLimit;
    for (const Obj* o : near) {
        if (o->type != 2 && o->type != 47) continue;
        if (hit(o, py)) return 0.0f;     // already touching (a sub-step killed it)
        const double d = o->cy - py;
        if (std::fabs(d) > kClearLimit || !hit(o, o->cy)) continue;
        double lo = 0.0, hi = d;         // lo = miss (py), hi = hit (its centre)
        for (int i = 0; i < 12; ++i) {   // -> |d|/4096, well under 0.02 px
            const double mid = 0.5 * (lo + hi);
            (hit(o, py + mid) ? hi : lo) = mid;
        }
        best = std::min(best, std::fabs(lo));
    }
    return (float)best;
}

inline bool tightHere(const std::vector<const Obj*>& near, double px, double py,
                      double half, double eps, bool hazardsOnly = false) {   // hazardsOnly
    const double pl = px - half, pr = px + half;
    const double pb = py - half, pt = py + half;
    for (const Obj* o : near) {
        if (o->type == 0) {
            if (o->slope || o->oneway || hazardsOnly) continue;
            if (o->cx + o->hw <= pl || o->cx - o->hw >= pr) continue;
            const double b = o->cy - o->hh, t = o->cy + o->hh;
            if (t <= pb) { if (pb - t < eps) return true; }
            else if (b >= pt) { if (b - pt < eps) return true; }
            else return true;                       // already inside one
        } else if (o->type == 2 || o->type == 47) {
            if (o->radius > 0.0) {
                const double dx = px - o->cx, dy = py - o->cy;
                const double r = o->radius + half + eps;
                if (dx * dx + dy * dy < r * r) return true;
            } else if (std::fabs(px - o->cx) < o->hw + half + eps
                       && std::fabs(py - o->cy) < o->hh + half + eps) {
                return true;
            }
        }
    }
    return false;
}

inline bool roomierRoute(uint16_t cand, uint16_t held) { return cand < held; }

// One live child, as the probe sees it.
struct ClearSample {
    uint64_t key;
    float vy;
    float solid;
    float haz;
    uint8_t mode;
    uint8_t goal;    // 1 = this child reached goalX
};

struct ClearAcc {
    struct Mode {
        std::vector<float> spreadSolid, spreadHaz;   // M1
        std::vector<float> lostSolid, lostHaz;       // M2: room the merge threw away
        long long cells = 0, cellsMulti = 0, kids = 0;
        std::vector<float> rawSolid, rawHaz;
        long long rawSeen = 0;
    };
    Mode per[8];
    long long slopesSkipped = 0;
    long long framesSkipped = 0;      // rotated frames, not measured
    std::vector<int> goalCount;       // M3: goal-reachers per layer that had any
    std::vector<float> goalSpread;    // M3: their solid-clearance spread

    void reset() {
        for (Mode& m : per) m = Mode{};
        slopesSkipped = framesSkipped = 0;
        goalCount.clear();
        goalSpread.clear();
    }
    // (g_goalDiv is reset alongside this -- see resetInvocationState)

    void addLayer(std::vector<ClearSample>& s) {
        if (s.empty()) return;
        std::stable_sort(s.begin(), s.end(),
                         [](const ClearSample& a, const ClearSample& b) {
                             return a.key < b.key;
                         });
        int nGoal = 0;
        float gLo = 1e18f, gHi = -1e18f;
        for (size_t i = 0; i < s.size();) {
            size_t j = i;
            while (j < s.size() && s[j].key == s[i].key) ++j;
            Mode& m = per[s[i].mode & 7];
            ++m.cells;
            m.kids += (long long)(j - i);
            for (size_t k = i; k < j; ++k)
                if ((m.rawSeen++ & 63) == 0) {      // every 64th, to bound memory
                    m.rawSolid.push_back(s[k].solid);
                    m.rawHaz.push_back(s[k].haz);
                }
            for (size_t k = i; k < j; ++k)
                if (s[k].goal) {
                    ++nGoal;
                    gLo = std::min(gLo, s[k].solid);
                    gHi = std::max(gHi, s[k].solid);
                }
            if (j - i >= 2) {
                ++m.cellsMulti;
                size_t hi = i, lo = i;
                for (size_t k = i + 1; k < j; ++k) {
                    if (s[k].vy > s[hi].vy) hi = k;
                    if (s[k].vy < s[lo].vy) lo = k;
                }
                float bs = -1e18f, bh = -1e18f, ws = -1e18f, wh = -1e18f;
                for (size_t k = i; k < j; ++k) {
                    bs = std::max(bs, s[k].solid);
                    bh = std::max(bh, s[k].haz);
                }
                ws = std::max(s[hi].solid, s[lo].solid);
                wh = std::max(s[hi].haz, s[lo].haz);
                float ls = 1e18f, lh = 1e18f;
                for (size_t k = i; k < j; ++k) {
                    ls = std::min(ls, s[k].solid);
                    lh = std::min(lh, s[k].haz);
                }
                m.spreadSolid.push_back(bs - ls);
                m.spreadHaz.push_back(bh - lh);
                m.lostSolid.push_back(bs - ws);   // 0 when a survivor was roomiest
                m.lostHaz.push_back(bh - wh);
            }
            i = j;
        }
        if (nGoal > 0) {
            goalCount.push_back(nGoal);
            goalSpread.push_back(gHi - gLo);
        }
    }
};

inline ClearAcc g_clear;

struct GoalDiversity {
    long long candidates = 0;
    long long sampled = 0;
    long long convergeDepth = -1;   // ticks back before ALL sampled agree
    long long walked = 0;           // how far back the walk went
    int tightFirst = -1, tightBest = -1, tightWorst = -1;
};

inline GoalDiversity g_goalDiv;

inline void measureGoalDiversity(const std::vector<State>& nxt,
                                 const std::vector<Node>& arena, double goalX) {
    if (!g_clearProbe || g_goalDiv.candidates) return;
    std::vector<uint32_t> chain;
    for (const State& s : nxt)
        if (s.frame == 0 && (double)s.xAbs >= goalX) {
            if (g_goalDiv.tightFirst < 0) g_goalDiv.tightFirst = (int)s.tight;
            if (g_goalDiv.tightBest < 0 || (int)s.tight < g_goalDiv.tightBest)
                g_goalDiv.tightBest = (int)s.tight;
            if ((int)s.tight > g_goalDiv.tightWorst)
                g_goalDiv.tightWorst = (int)s.tight;
            chain.push_back(s.parent);
        }
    g_goalDiv.candidates = (long long)chain.size();
    if (chain.size() < 2) return;
    const size_t kMax = 256;
    if (chain.size() > kMax) {
        const size_t step = chain.size() / kMax;
        std::vector<uint32_t> s;
        for (size_t i = 0; i < chain.size() && s.size() < kMax; i += step)
            s.push_back(chain[i]);
        chain.swap(s);
    }
    g_goalDiv.sampled = (long long)chain.size();
    for (long long d = 0; d < 200000; ++d) {
        bool same = true;
        for (size_t i = 1; i < chain.size() && same; ++i)
            same = (chain[i] == chain[0]);
        if (same) { g_goalDiv.convergeDepth = d; g_goalDiv.walked = d; return; }
        bool moved = false;
        for (uint32_t& c : chain)
            if (c) { c = arena[c].parent(); moved = true; }
        g_goalDiv.walked = d + 1;
        if (!moved) return;          // all at the root and still not equal
    }
}

inline void clearReport() {
    if (g_goalDiv.candidates) {
        const int span = g_goalDiv.tightWorst - g_goalDiv.tightBest;
        const int saved = g_goalDiv.tightFirst - g_goalDiv.tightBest;
        std::printf("clearscore: goal candidates=%lld tightpx=%.2f | tight "
                    "ordinal=%d best=%d worst=%d | saved %d of a %d span "
                    "(%.0f%% of what was on offer)\n",
                    g_goalDiv.candidates, kTightPx, g_goalDiv.tightFirst,
                    g_goalDiv.tightBest, g_goalDiv.tightWorst, saved, span,
                    span > 0 ? 100.0 * saved / span : 0.0);
    }
    if (!g_clearProbe) return;
    auto q = [](std::vector<float>& v, double p) {
        if (v.empty()) return -1.0f;
        std::sort(v.begin(), v.end());
        return v[std::min(v.size() - 1, (size_t)(v.size() * p))];
    };
    auto frac = [](const std::vector<float>& v, float c) {
        if (v.empty()) return -1.0;
        long long n = 0;
        for (float z : v) if (z > c) ++n;
        return 100.0 * (double)n / (double)v.size();
    };
    static const char* kName[8] = {"cube", "ship", "ball", "ufo",
                                   "wave", "robot", "spider", "swing"};
    std::printf("clearprobe: slopes skipped %lld, rotated frames skipped %lld\n",
                g_clear.slopesSkipped, g_clear.framesSkipped);
    std::printf("clearprobe: %-6s %10s | raw solid p10/med/p90 | raw haz p10/med/p90\n",
                "mode", "kids");
    for (int i = 0; i < 8; ++i) {
        ClearAcc::Mode& m = g_clear.per[i];
        if (!m.cells) continue;
        std::printf("clearprobe: %-6s %10lld |  %6.2f %6.2f %6.2f |  %6.2f %6.2f %6.2f\n",
                    kName[i], m.kids,
                    q(m.rawSolid, 0.1), q(m.rawSolid, 0.5), q(m.rawSolid, 0.9),
                    q(m.rawHaz, 0.1), q(m.rawHaz, 0.5), q(m.rawHaz, 0.9));
    }
    std::printf("clearprobe: %-6s %9s %6s | %-22s | %s\n", "mode",
                "multi", "%multi", "M1 spread p90 >1px% (s|h)",
                "M2 lost p90 >1px% (s|h)");
    for (int i = 0; i < 8; ++i) {
        ClearAcc::Mode& m = g_clear.per[i];
        if (!m.cells) continue;
        std::printf(
            "clearprobe: %-6s %9lld %5.1f%% | %5.2f %5.1f%% %5.2f %5.1f%% | "
            "%5.2f %5.1f%% %5.2f %5.1f%%\n",
            kName[i], m.cellsMulti,
            m.cells ? 100.0 * (double)m.cellsMulti / (double)m.cells : 0.0,
            q(m.spreadSolid, 0.9), frac(m.spreadSolid, 1.0f),
            q(m.spreadHaz, 0.9), frac(m.spreadHaz, 1.0f),
            q(m.lostSolid, 0.9), frac(m.lostSolid, 1.0f),
            q(m.lostHaz, 0.9), frac(m.lostHaz, 1.0f));
    }
    if (g_goalDiv.candidates) {
        std::printf("clearprobe: M3 goal candidates=%lld sampled=%lld "
                    "converge at %lld ticks back (walked %lld)\n",
                    g_goalDiv.candidates, g_goalDiv.sampled,
                    g_goalDiv.convergeDepth, g_goalDiv.walked);
    }
    if (g_clear.goalCount.empty()) {
        std::printf("clearprobe: M3 no layer reached goalX\n");
    } else {
        long long tot = 0, mx = 0;
        for (int c : g_clear.goalCount) { tot += c; mx = std::max(mx, (long long)c); }
        std::printf("clearprobe: M3 goal-reaching layers=%zu, states total=%lld, "
                    "max in one layer=%lld, solid-clearance spread med=%.2f\n",
                    g_clear.goalCount.size(), tot, mx,
                    q(g_clear.goalSpread, 0.5));
    }
}
}  // namespace dp
