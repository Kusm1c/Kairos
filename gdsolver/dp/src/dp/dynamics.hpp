#pragma once
#include "dp/frames.hpp"

namespace dp {
struct DynSample { int t; float cx, cy, hw, hh; uint8_t on; float rot = 0.f; uint8_t env = 0; };
inline std::vector<std::vector<int>> g_touchStops;

struct TrigCtl {          // one object a trigger ends up moving
    int uid;
    float dx, dy;         // total offset once the move has finished
    double durTicks;
    int ease;             // GD's EasingType, raw (see gdEase)
    double erate;         // its rate (period, for the elastic family)
    double lockTicks;
    double lockYTicks;
    int mover = 0;
    uint8_t tmode = 0;
    float tdx = 0.f, tdy = 0.f;
    int spawnLag = 0;
    uint8_t orbit = 0;
};
inline bool g_spawnLag = true;
inline bool g_anchorMove = true;
inline std::unordered_map<int, double> g_spawnableX;
inline int spawnLagTicks(double seconds) {
    if (seconds <= 0.0) return 0;
    return std::max(0, (int)std::ceil(seconds * 240.0 - 1e-9) - 1);
}
inline TouchMask g_recPhase{};
inline double gdEase(int kind, double rate, double u) {
    u = u < 0.0 ? 0.0 : (u > 1.0 ? 1.0 : u);
    const double p = (rate > 0.0) ? rate : 2.0;
    const double kPi = 3.14159265358979;
    auto bounceOut = [](double x) {
        if (x < 1.0 / 2.75) return 7.5625 * x * x;
        if (x < 2.0 / 2.75) { x -= 1.5 / 2.75;   return 7.5625 * x * x + 0.75; }
        if (x < 2.5 / 2.75) { x -= 2.25 / 2.75;  return 7.5625 * x * x + 0.9375; }
        x -= 2.625 / 2.75;
        return 7.5625 * x * x + 0.984375;
    };
    auto elasticOut = [&](double x, double period) {
        if (x <= 0.0 || x >= 1.0) return x;
        return std::pow(2.0, -10.0 * x)
             * std::sin((x - period / 4.0) * kPi * 2.0 / period) + 1.0;
    };
    auto elasticIn = [&](double x, double period) {
        if (x <= 0.0 || x >= 1.0) return x;
        const double y = x - 1.0;
        return -std::pow(2.0, 10.0 * y)
             * std::sin((y - period / 4.0) * kPi * 2.0 / period);
    };
    switch (kind) {
        case 0: return u;                                     // None (linear)
        case 1: {                                             // EaseInOut
            const double t = 2.0 * u;
            return t < 1.0 ? 0.5 * std::pow(t, p)
                           : 1.0 - 0.5 * std::pow(2.0 - t, p);
        }
        case 2: return std::pow(u, p);                        // EaseIn
        case 3: return std::pow(u, 1.0 / p);                  // EaseOut
        case 4: return u < 0.5 ? elasticIn(2.0 * u, p) * 0.5
                               : elasticOut(2.0 * u - 1.0, p) * 0.5 + 0.5;
        case 5: return elasticIn(u, p);
        case 6: return elasticOut(u, p);
        case 7: return u < 0.5 ? (1.0 - bounceOut(1.0 - 2.0 * u)) * 0.5
                               : bounceOut(2.0 * u - 1.0) * 0.5 + 0.5;
        case 8: return 1.0 - bounceOut(1.0 - u);
        case 9: return bounceOut(u);
        case 10: return u < 0.5
                     ? 0.5 * std::pow(2.0, 10.0 * (2.0 * u - 1.0))
                     : 0.5 * (2.0 - std::pow(2.0, -10.0 * (2.0 * u - 1.0)));
        case 11: return (g_expInZero && u == 0.0) ? 0.0 : std::pow(2.0, 10.0 * (u - 1.0)) - 0.001;
        case 12: return u >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * u);
        case 13: return -0.5 * (std::cos(kPi * u) - 1.0);
        case 14: return 1.0 - std::cos(u * kPi * 0.5);
        case 15: return std::sin(u * kPi * 0.5);
        case 16: {
            const double o = 1.70158 * 1.525;
            double x = 2.0 * u;
            if (x < 1.0) return 0.5 * (x * x * ((o + 1.0) * x - o));
            x -= 2.0;
            return 0.5 * (x * x * ((o + 1.0) * x + o) + 2.0);
        }
        case 17: { const double o = 1.70158; return u * u * ((o + 1.0) * u - o); }
        case 18: {
            const double o = 1.70158, x = u - 1.0;
            return x * x * ((o + 1.0) * x + o) + 1.0;
        }
        default: return u;
    }
}
inline bool g_instantLag = true;
inline double instantE(int te, int f) { return (g_instantLag && te <= f) ? 0.0 : 1.0; }
inline long long zoomRowsSince(size_t k, double x, double dx, long long t) {
    const long long est = (long long)std::floor((x - g_zoomTrigs[k].cx) / dx + 1e-6);
    if (t < 0 || k >= g_zoomCrossN) return est;
    long long tc = g_zoomCrossT[k].load(std::memory_order_relaxed);
    if (tc == kZoomCrossUnknown) {
        long long want = t - est;
        // the earliest wins if two threads see it on different layers
        while (!g_zoomCrossT[k].compare_exchange_weak(tc, want, std::memory_order_relaxed)) {
            if (tc != kZoomCrossUnknown && tc <= want) break;
        }
        tc = g_zoomCrossT[k].load(std::memory_order_relaxed);
    }
    return t - tc;
}
inline double zoomValueAfter(const ZoomTrig& z, double from, long long d) {
    if (z.durTicks <= 0.0) return d >= 0 ? z.target : from;
    const double e = (double)(d + 1);
    if (e <= 0.0) return from;
    if (e >= z.durTicks) return z.target;
    return from + (z.target - from) * gdEase(z.ease, z.rate, e / z.durTicks);
}
inline double camScaleChain(double x, double dx, long long t) {
    size_t m = 0;   // the zooms crossed by x
    while (m < g_zoomTrigs.size() && g_zoomTrigs[m].cx <= x) ++m;
    if (m == 0) return 1.0;
    thread_local std::vector<long long> n;
    n.clear();
    size_t k = m;
    while (k > 0) {
        --k;
        n.push_back(zoomRowsSince(k, x, dx, t));
        if (n.size() >= 2) {
            const ZoomTrig& z = g_zoomTrigs[k];
            const long long dStart = n[n.size() - 1] - n[n.size() - 2] - 1;   // rows into z when the next started
            if (z.durTicks <= 0.0 ? dStart >= 0 : (double)(dStart + 1) >= z.durTicks) break;
        }
    }
    double from = 1.0;
    for (size_t j = 0; j < n.size(); ++j) {
        const size_t idx = k + j;
        const long long nj = n[n.size() - 1 - j];
        if (j + 1 < n.size()) {
            const long long nNext = n[n.size() - 2 - j];
            from = zoomValueAfter(g_zoomTrigs[idx], from, nj - nNext - 1);
        } else {
            from = zoomValueAfter(g_zoomTrigs[idx], from, nj);
        }
    }
    return from;
}
inline double camScaleAt(double x, double dx, long long t) {
    double cur = 1.0;
    if (dx <= 0.0) dx = 1.0;
    if (g_zoomChain) return camScaleChain(x, dx, t);
    for (const auto& z : g_zoomTrigs) {
        if (z.cx > x) break;
        if (z.durTicks <= 0.0) { cur = z.target; continue; }
        const double u = (x - z.cx) / (dx * z.durTicks);
        cur = (u >= 1.0) ? z.target
                         : cur + (z.target - cur) * gdEase(z.ease, z.rate, u);
    }
    return cur;
}

inline int recordLag(double dx, double dy, double durTicks, int ease,
                     double erate) {
    if (durTicks <= 0.0 || (std::fabs(dx) < 0.5 && std::fabs(dy) < 0.5))
        return 0;
    for (int n = 1; n <= 240; ++n) {
        const double e = gdEase(ease, erate, (double)n / durTicks);
        if (std::fabs(dx * e) >= 0.05 || std::fabs(dy * e) >= 0.05) return n;
    }
    return 1;
}
inline bool g_recLagUp = true;
inline bool g_recLagAbs = true;
inline double recLagCurve(double full, double e) { return g_recLagAbs ? std::fabs(full * e) : full * e; }
inline bool g_offAfter = true;
// The on/off to use at tick `tt` for row `k` of a recording (the row in force at tt).
inline uint8_t recOnAt(const std::vector<DynSample>& sm, size_t k, int tt) {
    if (g_offAfter && sm[k].on == 0 && sm[k].t == tt && k > 0 && sm[k - 1].on) return 1;
    return sm[k].on;
}
inline int recLagUpN(int bestN, int lag, double full, double moved, double dur, int ease,
                     double erate) {
    if (!g_recLagUp || bestN != lag || dur <= 0.0) return bestN;
    const double eL = std::fabs(recLagCurve(full, gdEase(ease, erate, (double)lag / dur)) - moved);
    const double eU = std::fabs(recLagCurve(full, gdEase(ease, erate, (double)(lag + 1) / dur)) - moved);
    return (eL > 0.001 && eU < 0.5 * eL) ? lag + 1 : bestN;
}
inline bool g_rotLag = true;
inline int g_rotLagN = 0;   // objects the flag gave a lag (the CLI prints it)
inline int rotRecordLag(double totalDeg, double durTicks, int ease, double erate) {
    if (durTicks <= 0.0 || std::fabs(totalDeg) < 0.05) return 0;
    for (int n = 1; n <= 240; ++n)
        if (std::fabs(totalDeg * gdEase(ease, erate, (double)n / durTicks)) >= 0.05) return n;
    return 1;
}
struct AutoTrig {
    int uid = 0;              // reporting only
    double cx = 0;            // player x that fires it
    std::vector<TrigCtl> ctl;
    int fireT = -1;
    double fireX = 0;
    int fireSpreadT = 0;
    double lockLastX = 0;
    int delay = 1;
    // --anchormove: the root is a Toggle, whose on/off the recording reads at the crossing.
    uint8_t toggleRoot = 0;
    struct Tog {
        int uid;
        uint8_t on;
        double delay;
        int grp = 0;   // the group the Toggle switches (read by --tapdisarm, Dynamics::disarmEv)
    };
    std::vector<Tog> tog;
    std::vector<int> stopMoves;
    double rotTotal = 0.0, rotDurT = 0.0, rotErate = 2.0;
    int rotEase = 0;
    uint8_t rotLock = 0, rotAngleOk = 0;
    uint8_t offInit = 0;
};
inline std::vector<AutoTrig> g_autoTrig;
inline bool g_autoStop = true;
inline bool g_autoSpread = false;
struct AutoSpreadOpen { size_t i; long long leadT; size_t behind, of; };
inline std::vector<AutoSpreadOpen> g_autoSpreadOpen;
inline bool g_autoOwn = true;
// Per g_autoTrig entry: how many ticks after its fire its movers can still be moving.
inline std::vector<double> g_autoRun;
struct AutoOwnGuard {
    std::vector<std::pair<size_t, int>> saved;
    AutoOwnGuard() = default;
    AutoOwnGuard(const AutoOwnGuard&) = delete;
    AutoOwnGuard& operator=(const AutoOwnGuard&) = delete;
    ~AutoOwnGuard() {
        for (const auto& p : saved) g_autoTrig[p.first].fireT = p.second;
    }
};
inline void autoOwnApply(AutoOwnGuard& G, double gx, double gdx, long long t) {
    if (!(gdx > 0.0)) return;
    if (g_autoRun.size() != g_autoTrig.size()) {
        g_autoRun.assign(g_autoTrig.size(), 0.0);
        for (size_t i = 0; i < g_autoTrig.size(); ++i)
            for (const TrigCtl& c : g_autoTrig[i].ctl)
                g_autoRun[i] = std::max(g_autoRun[i],
                                        c.durTicks + (double)c.spawnLag + c.lockTicks);
    }
    for (size_t i = 0; i < g_autoTrig.size(); ++i) {
        AutoTrig& A = g_autoTrig[i];
        if (A.fireT < 0) continue;
        int own = -1;
        if (gx >= A.cx) {
            const long long k = (long long)std::floor((gx - A.cx) / gdx);
            own = std::min((int)(t - 1 - k) + A.delay, A.fireT + A.fireSpreadT);
            if (own <= A.fireT) continue;                          // not behind the leader
            if ((double)(t - own) > g_autoRun[i] + 2.0) continue;  // its movers are done for it too
        }
        if (g_trigDbgT >= 0 && t == g_trigDbgT)   // --trigdbg <tick>: what this group's placement reads
            std::printf("autoown t=%lld gx=%.1f gdx=%.3f trig=%zu cx=%.1f fireT %d -> %d spread=%d run=%.0f\n",
                        t, gx, gdx, i, A.cx, A.fireT, own, A.fireSpreadT, g_autoRun[i]);
        G.saved.push_back({i, A.fireT});
        A.fireT = own;
    }
}
inline bool g_rotNoSpawn = true;
inline std::vector<std::vector<int>> g_autoStopBy;
inline bool g_dynObbRot = true;
inline bool g_rotObb = true;
inline bool g_rotObbLatch = true;
inline int autoStopT(int k) {
    int st = 1 << 30;
    if (k < 0 || (size_t)k >= g_autoStopBy.size()) return st;
    for (const int s : g_autoStopBy[(size_t)k]) {
        const int f = g_autoTrig[(size_t)s].fireT;
        if (f >= 0) st = std::min(st, f);
    }
    return st;
}
struct AutoDisarm {
    int box;
    uint8_t on;
    double delay;
};
inline std::vector<std::vector<AutoDisarm>> g_autoDisarm;
inline bool autoDisarmed(int k, TouchMask mask, const uint16_t* fireB) {
    const bool offInit = g_boxArm && k >= 0 && (size_t)k < g_autoTrig.size() && g_autoTrig[(size_t)k].offInit;
    if (!fireB || k < 0 || (size_t)k >= g_autoDisarm.size() || g_autoDisarm[(size_t)k].empty()) return offInit;
    const AutoTrig& A = g_autoTrig[(size_t)k];
    if (A.fireT < 0) return false;
    const double cross = (double)(A.fireT - A.delay);
    double best = -1e300;
    uint8_t on = offInit ? 0 : 1;
    for (const AutoDisarm& d : g_autoDisarm[(size_t)k]) {
        if (d.box < 0 || d.box >= kTouchBits || !(mask & touchBit(d.box))) continue;
        const double te = (double)fireB[d.box] + d.delay;
        if (te > cross || te < best) continue;
        best = te;
        on = d.on;
    }
    return on == 0;
}
inline int autoFireT(int k, TouchMask mask, const uint16_t* fireB) {
    const AutoTrig& A = g_autoTrig[(size_t)k];
    if (A.fireT < 0 || !A.offInit || !g_boxArm) return A.fireT;
    return autoDisarmed(k, mask, fireB) ? -1 : A.fireT;
}

struct OnEvent {
    int box = -1;
    int at = -1;
    uint8_t on = 1;       // 1 = the object comes on, 0 = it goes off
    double delay = 0.0;   // ticks after the source fires (the chain's spawn delays)
    int autoIdx = -1;
    int grp = -1;         // the group the switch acts on (--tapdisarm's resolveOnGroups; -1 = not known)
};

inline double offAfterLag(const OnEvent& e) {
    return (g_offAfter && e.autoIdx >= 0 && e.on == 0) ? 1.0 : 0.0;
}
inline bool resolveOn(bool init, const OnEvent* ev, size_t n, TouchMask mask,
                      const uint16_t* fireB, int t) {
    bool on = init;
    double best = -1e300;
    for (size_t k = 0; k < n; ++k) {
        const OnEvent& e = ev[k];
        double f;
        if (e.box >= 0) {
            if (e.box >= kTouchBits || !fireB || !(mask & touchBit(e.box))) continue;
            f = fireB[e.box];
        } else if (e.autoIdx >= 0) {
            if ((size_t)e.autoIdx >= g_autoTrig.size()) continue;
            const int ft = g_autoTrig[(size_t)e.autoIdx].fireT;
            if (ft < 0) continue;
            if (autoDisarmed(e.autoIdx, mask, fireB)) continue;   // --tapdisarm
            f = ft;
        } else {
            if (e.at < 0) continue;
            f = e.at;
        }
        const double te = f + e.delay + offAfterLag(e);   // --offafter
        if (te > t || te < best) continue;
        best = te;
        on = e.on != 0;
    }
    return on;
}
// --recendtail: the same, from `init` as it stood at tick `after` -- only the switches that land after it.
inline bool resolveOnAfter(bool init, int after, const OnEvent* ev, size_t n, TouchMask mask,
                           const uint16_t* fireB, int t) {
    bool on = init;
    double best = -1e300;
    for (size_t k = 0; k < n; ++k) {
        const OnEvent& e = ev[k];
        double f;
        if (e.box >= 0) {
            if (e.box >= kTouchBits || !fireB || !(mask & touchBit(e.box))) continue;
            f = fireB[e.box];
        } else if (e.autoIdx >= 0) {
            if ((size_t)e.autoIdx >= g_autoTrig.size()) continue;
            const int ft = g_autoTrig[(size_t)e.autoIdx].fireT;
            if (ft < 0) continue;
            if (autoDisarmed(e.autoIdx, mask, fireB)) continue;   // --tapdisarm
            f = ft;
        } else {
            if (e.at < 0) continue;
            f = e.at;
        }
        const double te = f + e.delay + offAfterLag(e);   // --offafter
        if (te <= (double)after || te > t || te < best) continue;
        best = te;
        on = e.on != 0;
    }
    return on;
}

inline bool resolveOnGroups(const OnEvent* ev, size_t n, TouchMask mask, const uint16_t* fireB, int t) {
    struct G { int grp; double best; uint8_t on; };
    G gs[16];
    size_t ng = 0;
    for (size_t k = 0; k < n; ++k) {
        const OnEvent& e = ev[k];
        double f;
        if (e.box >= 0) {
            if (e.box >= kTouchBits || !fireB || !(mask & touchBit(e.box))) continue;
            f = fireB[e.box];
        } else if (e.autoIdx >= 0) {
            if ((size_t)e.autoIdx >= g_autoTrig.size()) continue;
            const int ft = g_autoTrig[(size_t)e.autoIdx].fireT;
            if (ft < 0) continue;
            if (autoDisarmed(e.autoIdx, mask, fireB)) continue;
            f = ft;
        } else {
            if (e.at < 0) continue;
            f = e.at;
        }
        const double te = f + e.delay + offAfterLag(e);   // --offafter
        if (te > t) continue;
        const int key = e.grp >= 0 ? e.grp : -2 - (int)k;
        size_t j = 0;
        while (j < ng && gs[j].grp != key) ++j;
        if (j == ng) {
            if (ng == 16) continue;   // more groups than any object seen; the rest are left out
            gs[ng++] = {key, -1e300, 1};
        }
        if (te < gs[j].best) continue;   // a tie goes to the later in the list, as in resolveOn
        gs[j].best = te;
        gs[j].on = e.on;
    }
    for (size_t j = 0; j < ng; ++j)
        if (!gs[j].on) return false;
    return true;
}
inline std::unordered_map<int, std::vector<OnEvent>> autoOnEvents(
        const std::vector<AutoTrig>& autos) {
    std::unordered_map<int, std::vector<OnEvent>> out;
    for (size_t a = 0; a < autos.size(); ++a)
        for (const AutoTrig::Tog& g : autos[a].tog) {
            OnEvent ev;
            ev.autoIdx = (int)a;
            ev.on = g.on;
            ev.delay = g.delay;
            ev.grp = g.grp;
            out[g.uid].push_back(ev);
        }
    return out;
}
inline float g_touchBoxU[kTouchBits] = {};
inline bool g_keyCensus = false;
inline std::array<std::atomic<long long>, kTouchBits> g_keyCount{};
inline std::array<int, kTouchBits> g_touchFireT = [] {
    std::array<int, kTouchBits> a{};
    a.fill(-1);
    return a;
}();
inline bool g_formulaRec = true;
inline bool g_recProvGiven = false;
inline long long g_recProvRec = -1, g_recProvAnchor = -1, g_recProvReach = -1;
inline bool g_formulaRecTouch = true;
inline bool formulaRecProvOk(long long t) {
    return g_recProvGiven && g_recProvRec >= 0 && g_recProvRec == g_recProvAnchor && t <= g_recProvReach;
}
inline std::array<int, kTouchBits> g_touchRecEntry = [] {
    std::array<int, kTouchBits> a{};
    a.fill(-1);
    return a;
}();
inline std::atomic<long long> g_formulaRecHit{0}, g_formulaRecMiss{0};
inline std::unordered_set<int> g_rotated;
struct RotSpec {
    int trigUid = 0;       // the Rotate, for reporting and for its fire tick
    int centreUid = 0;     // the single object of the centre group
    double total = 0.0;    // t360*360 + deg -- the WHOLE angle
    double durT = 0.0;     // ticks
    int ease = 0;
    double erate = 2.0;
    int lockrot = 0;       // does the object's own facing follow the orbit?
    // Resolved at load, once the objects are placed:
    int anchor = -1;       // g_autoTrig index of the Rotate (-1 = not autonomous)
    int centreIdx = -1;    // index into dyn of the centre object
    float relX = 0.f, relY = 0.f;   // entry position MINUS the centre's entry
};
inline std::unordered_map<int, RotSpec> g_rotSpec;   // uid -> how it turns
inline bool g_rotTail = true;
struct RotTailPart {
    int trigUid = 0;      // the autonomous Rotate (its g_autoTrig entry gives the fire tick)
    int centreUid = 0;    // the single object of its centre group; 0 = each object turns about itself
    double total = 0.0, durT = 0.0, erate = 2.0;
    int ease = 0;
    uint8_t lock = 0;     // lock rotation: the object orbits but keeps its facing
    double trigCx = 0.0;  // the Rotate's x, for the crossing order
};
inline std::unordered_map<int, std::vector<RotTailPart>> g_rotTailParts;   // turned uid -> its Rotates
inline int g_rotTailN = 0;   // objects the loader armed (the CLI prints it)
inline bool g_recEndTail = true;
inline bool g_recEndAuto = true;
inline bool g_areaMove = true;
inline constexpr double kAreaArmTol = 1.0;
struct AreaAct {
    int uid = -1, id = 0, target = 0, special = 0, dirType = 0, eInType = 0, eInBuf = -3;
    double cx = 0, len = 0, off = 0, modFront = 1, modBack = 1, deadzone = 0, moveX = 0, moveY = 0, eInRate = 2;
    bool xy = false, inbound = false;
    double moveDist = 0, angle = 0, scaleX = 1, scaleY = 1, rot = 0;
};
inline std::vector<AreaAct> g_areas;
inline double areaEase(const AreaAct& a, double u) {
    if (a.eInBuf == -1 || (a.eInBuf == -3 && a.eInType == 0)) return u;
    if (a.eInBuf == -2) return gdEase(a.eInType, a.eInRate, u);
    const float uf = (float)u;
    const int k = (int)(uf * 100.0f);
    if (k >= 100) return gdEase(a.eInType, a.eInRate, 1.0);
    const double lo = gdEase(a.eInType, a.eInRate, k / 100.0), hi = gdEase(a.eInType, a.eInRate, (k + 1) / 100.0);
    return (double)(uf - (float)k * 0.01f) * (hi - lo) * 100.0 + lo;
}
inline float areaValue(const AreaAct& a, double bx, double px) {
    const float s0 = (float)(bx - (px + a.off));
    const float s = s0 * (0.f <= s0 ? (float)a.modBack : (float)a.modFront);
    const float x = s / (float)(int)a.len;
    const float dz = (float)a.deadzone;
    float r = dz == 0.f ? x : (x - dz) / (1.f - dz);
    r = r > 1.f ? 1.f : (r < 0.f ? 0.f : r);
    return a.inbound ? 1.f - r : r;
}
inline bool areaDisp(const AreaAct& a, double bx, double px, double& dx, double& dy) {
    if (a.id != 3006) return false;
    const float u = areaValue(a, bx, px);
    if (!(u < 1.f)) return false;
    const double k = 1.0 - areaEase(a, (double)u);
    if (a.xy) {
        dx = k * a.moveX;
        dy = k * a.moveY;
    } else {   // processAreaMoveGroupAction: kk * m along ccpForAngle((angle - 90) deg)
        const float ang = ((float)a.angle - 90.f) * 0.017453292f;
        const float t = (float)k * (float)a.moveDist;
        dx = (double)(t * std::cos(ang));
        dy = (double)(t * std::sin(ang));
    }
    return true;
}
inline bool areaScaleRot(const AreaAct& a, double bx, double px, double& fx, double& fy, double& dr) {
    if (a.id != 3008 && a.id != 3007) return false;
    const float u = areaValue(a, bx, px);
    if (!(u < 1.f)) return false;
    const double k = 1.0 - areaEase(a, (double)u);
    if (a.id == 3008) {
        fx = (a.scaleX - 1.0) * k + 1.0;
        fy = (a.scaleY - 1.0) * k + 1.0;
    } else {
        dr = k * a.rot;
    }
    return true;
}
struct AreaPose {
    double cx = 0, cy = 0, hw = 0, hh = 0, rot = 0;
    bool sized = false;
};
inline AreaPose areaPoseOf(const std::vector<int>& acts, double bx, double by, double ha, double hb, double r0,
                           double px) {
    AreaPose p;
    p.cx = bx;
    p.cy = by;
    double sx = 1.0, sy = 1.0, dr = 0.0;
    for (const int k : acts) {
        const AreaAct& a = g_areas[(size_t)k];
        if (a.id == 3007 || a.id == 3008) p.sized = true;
        if (px < a.cx) continue;   // not crossed yet
        double dx = 0.0, dy = 0.0, fx = 1.0, fy = 1.0, r = 0.0;
        if (areaDisp(a, bx, px, dx, dy)) { p.cx += dx; p.cy += dy; }
        if (areaScaleRot(a, bx, px, fx, fy, r)) { sx *= fx; sy *= fy; dr += r; }
    }
    p.rot = r0 + dr;
    if (p.sized) {
        const double th = p.rot * (3.14159265358979 / 180.0);
        const double c = std::fabs(std::cos(th)), s = std::fabs(std::sin(th));
        const double w = ha * sx, h = hb * sy;
        p.hw = w * c + h * s;
        p.hh = w * s + h * c;
    }
    return p;
}
inline int g_recEndTailN = 0, g_recEndTailTried = 0, g_recEndTailOnN = 0;   // printed only
inline constexpr int kRecEndTailTicks = 4000;     // simulated past an object's last row; held after
inline constexpr int kRecEndTailCheck = 120;      // the rows before the last one that must agree
inline constexpr double kRecEndTailTol = 0.5, kRecEndTailRotTol = 0.2, kRecEndTailOff = 4.0;
inline bool g_rotCompute = false;
inline bool g_rotCheck = false;
inline size_t g_rotSeen = 0, g_rotRouted = 0;
inline bool g_rotSplit = true;

inline void rotStep(float& x, float& y, double cxPrev, double cyPrev,
                    double cxNow, double cyNow, double dTheta) {
    const double c = std::cos(dTheta), s = std::sin(dTheta);
    const double rx = (double)x - cxPrev, ry = (double)y - cyPrev;
    x = (float)(cxPrev + c * rx - s * ry + (cxNow - cxPrev));
    y = (float)(cyPrev + s * rx + c * ry + (cyNow - cyPrev));
}
inline bool g_trigRaw = false;
inline std::vector<std::pair<int, double>> g_xTrack;
inline double xRefAt(int t) {
    const auto& v = g_xTrack;
    if (v.empty()) return 0.0;
    const size_t n = v.size();
    if (t <= v.front().first) {
        if (n < 2 || v[1].first <= v[0].first) return v[0].second;
        return v[0].second - (v[1].second - v[0].second) / (v[1].first - v[0].first) * (v[0].first - t);
    }
    if (t >= v.back().first) {
        if (n < 2 || v[n - 1].first <= v[n - 2].first) return v[n - 1].second;
        return v[n - 1].second
               + (v[n - 1].second - v[n - 2].second) / (v[n - 1].first - v[n - 2].first) * (t - v[n - 1].first);
    }
    const auto it = std::lower_bound(v.begin(), v.end(), t,
                                     [](const std::pair<int, double>& a, int b) { return a.first < b; });
    if (it->first == t) return it->second;
    const auto& a = *(it - 1);
    return a.second + (it->second - a.second) * (double)(t - a.first) / (double)(it->first - a.first);
}
// ...and the first tick its x reaches `cx` (-1 = never, the track standing still at its end).
inline int xRefCross(double cx) {
    const auto& v = g_xTrack;
    if (v.empty()) return -1;
    for (size_t k = 0; k < v.size(); ++k)
        if (v[k].second >= cx) {
            if (k == 0) return v[0].first;
            // between two rows: the first whole tick at or past cx
            const auto& a = v[k - 1];
            for (int t = a.first + 1; t <= v[k].first; ++t)
                if (xRefAt(t) >= cx) return t;
            return v[k].first;
        }
    const size_t n = v.size();
    if (n < 2) return -1;
    const double step = (v[n - 1].second - v[n - 2].second) / (double)(v[n - 1].first - v[n - 2].first);
    if (!(step > 0.0)) return -1;
    return v[n - 1].first + (int)std::ceil((cx - v[n - 1].second) / step - 1e-9);
}
inline bool g_recLeads = true;
inline int g_recLeadsN = 0;   // objects it re-routed in the last load (printed only)
inline bool g_recAnchorX = true;
inline int g_recAnchorXN = 0;   // objects it re-paired in the last load (printed only)
inline bool g_lagFitDbg = false;
inline bool g_touchEnteredGiven = false;
inline std::unordered_set<int> g_touchEntered;
inline std::unordered_map<int, int> g_touchEnteredT;
inline bool g_recTouchGiven = false;
inline std::unordered_map<int, int> g_recTouch;
inline bool g_boxOffOwn = true;
inline std::array<int, kTouchBits> g_recTouchBit = [] {
    std::array<int, kTouchBits> a{};
    a.fill(-1);
    return a;
}();
inline bool slamBoxRecorded(const std::array<int, kTouchBits>& rec, int b, int F) {
    return b >= 0 && b < kTouchBits && F >= 0 && rec[(size_t)b] >= 0 && rec[(size_t)b] == F;
}
inline bool g_anchorEntryBase = true;
inline int g_dynDbg = -1;
inline thread_local std::function<void(size_t, const std::function<void(size_t)>&)>
    g_dynParTasks;
inline int g_formulaDriven = 0;
inline long long g_touchRetimeFrom = 0;
inline constexpr int kRecInterpGap = 4;
inline constexpr double kRecEps = 0.05;
inline bool g_recStall = true;
// Whether the gap between sm[c] and sm[c+1] is such a stop (see g_recStall).
inline bool recStallGap(const std::vector<DynSample>& sm, size_t c) {
    if (!g_recStall || c == 0 || c + 2 >= sm.size()) return false;
    const DynSample &p = sm[c - 1], &s = sm[c], &n = sm[c + 1], &q = sm[c + 2];
    const int g = n.t - s.t;
    if (g < 2 || s.t - p.t != 1 || q.t - n.t != 1) return false;
    const double ch[3][4] = {{p.cx, s.cx, n.cx, q.cx}, {p.cy, s.cy, n.cy, q.cy},
                             {p.rot, s.rot, n.rot, q.rot}};
    for (const auto& v : ch) {
        const double before = std::fabs(v[1] - v[0]), after = std::fabs(v[3] - v[2]);
        if (before < kRecEps || after < kRecEps) continue;
        return std::fabs(v[2] - v[1]) < (g - 0.5) * 0.5 * (before + after);
    }
    return false;
}
inline constexpr int kTouchRetimeLat = 2;
inline int g_lockBox = -1;
inline double g_lockTicks = 0.0;
inline bool g_lockX = true;
inline bool g_lockNoDx = true;
inline bool g_lockStop = true;
inline int touchStopTick(const TouchMask& mask, const uint16_t* fireB, int moverUid) {
    int st = 1 << 30;
    if (moverUid == 0 || !fireB) return st;
    const int nb = std::min((int)g_touchStops.size(), kTouchBits);
    for (int b = 0; b < nb; ++b) {
        if (!(mask & touchBit(b))) continue;
        for (const int s : g_touchStops[(size_t)b])
            if (s == moverUid) st = std::min(st, (int)fireB[b] + (g_spawnLag ? 0 : 1));
    }
    return st;
}
inline bool g_shiftStat = false;
struct Dynamics {
    std::vector<Obj> objs;
    std::vector<std::vector<DynSample>> samples;  // parallel to objs
    std::vector<uint8_t> everRot;
    std::vector<double> obbBc0, obbBs0;
    std::vector<int> obbLatchT;
    void obbLatch(size_t i, int t, double rotDeg) {
        if (i >= obbLatchT.size() || obbLatchT[i] < 0) return;
        double d = std::fmod(std::fabs(rotDeg - objs[i].rot), 360.0);
        if (d > 180.0) d = 360.0 - d;
        objs[i].obbOk = (t >= obbLatchT[i] || d > 0.001) ? 1 : 0;
    }
    std::vector<uint8_t> shiftSaid;
    std::vector<size_t> cur;                      // per-object cursor
    enum Bucket : uint8_t { NEAR, PORT, PAD, ORB, SPEED, SLOPE, COIN };
    std::vector<uint8_t> bucket;
    std::vector<uint8_t> formula;
    // --formulanolock (speed.hpp): a formula-driven object whose autonomous chain locks it to the player
    std::vector<uint8_t> formulaLock;
    std::vector<float> prevCy;
    bool lastStep1 = false;
    std::vector<float> baseCy, baseSy0, baseSy1;
    std::vector<uint8_t> on;   // toggled in? (0 = the object is not there)
    std::vector<std::vector<OnEvent>> onEv;
    std::vector<uint8_t> onInit;
    std::vector<std::vector<OnEvent>> disarmEv;
    std::vector<uint8_t> disarmInit;
    std::vector<uint8_t> disarmDep;
    std::vector<std::vector<int>> areaOf;
    std::vector<double> areaBaseX, areaBaseY;
    // ...and its unturned half sizes and angle at the base (for an Area Scale / Rotate)
    std::vector<double> areaBaseHa, areaBaseHb, areaBaseRot;
    std::vector<uint8_t> areaArmed;
    double areaPxPrev = 0.0;
    template <class T, class H, class R>
    bool areaPlace(size_t i, T& cx, T& cy, H* hw = nullptr, H* hh = nullptr, R* rotv = nullptr) const {
        if (!g_areaMove || i >= areaArmed.size() || !areaArmed[i]) return false;
        const AreaPose p = areaPoseOf(areaOf[i], areaBaseX[i], areaBaseY[i], areaBaseHa[i], areaBaseHb[i],
                                      areaBaseRot[i], areaPxPrev);
        cx = (T)p.cx;
        cy = (T)p.cy;
        if (p.sized) {
            if (hw) *hw = (H)p.hw;
            if (hh) *hh = (H)p.hh;
            if (rotv) *rotv = (R)p.rot;
        }
        if (g_dynDbg == objs[i].uid)
            std::printf("areamove: uid=%d px(prev)=%.3f -> (%.3f,%.3f) half (%.3f,%.3f) rot %.2f\n", objs[i].uid,
                        areaPxPrev, p.cx, p.cy, p.hw, p.hh, p.rot);
        return true;
    }
    std::vector<std::vector<uint8_t>> offBox;
    std::vector<TouchMask> trigMask;
    std::vector<uint8_t> recAuto;
    std::vector<uint8_t> extOk;
    std::vector<float> extVx, extVy;
    std::vector<int> extCap;
    void prepareExtend() {
        extOk.assign(objs.size(), 0);
        extVx.assign(objs.size(), 0.f);
        extVy.assign(objs.size(), 0.f);
        extCap.assign(objs.size(), 0);
        int maxT = -1;
        for (const auto& sm : samples)
            if (!sm.empty()) maxT = std::max(maxT, sm.back().t);
        for (size_t i = 0; i < samples.size(); ++i) {
            const auto& sm = samples[i];
            if (sm.size() < 3 || sm.back().t != maxT) continue;
            if (i < autoParts.size() && !autoParts[i].empty()) continue;
            const DynSample& a = sm[sm.size() - 3];
            const DynSample& b = sm[sm.size() - 2];
            const DynSample& c = sm.back();
            if (b.t != c.t - 1 || a.t != b.t - 1) continue;
            const float v1x = c.cx - b.cx, v1y = c.cy - b.cy, v2x = b.cx - a.cx, v2y = b.cy - a.cy;
            if (std::fabs(v1x - v2x) > 0.02f || std::fabs(v1y - v2y) > 0.02f) continue;
            if (std::fabs(v1x) < 0.05f && std::fabs(v1y) < 0.05f) continue;
            // the run of consecutive ticks at this velocity that ends at the last row
            int run = 2;
            bool reachedFirstRow = true;   // the run goes back to the series' first row
            for (size_t k = sm.size() - 3; k > 0; --k) {
                if (sm[k].t != sm[k - 1].t + 1
                    || std::fabs((sm[k].cx - sm[k - 1].cx) - v1x) > 0.02f
                    || std::fabs((sm[k].cy - sm[k - 1].cy) - v1y) > 0.02f) { reachedFirstRow = false; break; }
                ++run;
            }
            const bool startUnknown = reachedFirstRow;
            int cap = -1;   // -1 = no Move found
            if (g_moverExtend && i < partsAny.size()) {
                for (const AutoPart& p : partsAny[i]) {
                    if (p.ease != 0 || p.dur <= 0.0) continue;
                    if (std::fabs(p.dx / p.dur - v1x) > 0.02 || std::fabs(p.dy / p.dur - v1y) > 0.02) continue;
                    cap = std::max(cap, std::max(0, (int)std::lround(p.dur) - run));
                }
            }
            if (startUnknown && cap >= 0) cap = -1;   // the data form needs the Move's start
            if (cap < 0 && g_moverExtendVel) cap = 1 << 28;
            if (cap <= 0) continue;
            extOk[i] = 1; extVx[i] = v1x; extVy[i] = v1y; extCap[i] = cap;
        }
    }
    std::vector<uint8_t> recOk;
    std::vector<float> recVx, recVy, recVr, recVhw, recVhh;
    void prepareRecEnd() {
        const size_t n = objs.size();
        recOk.assign(n, 0);
        recVx.assign(n, 0.f); recVy.assign(n, 0.f); recVr.assign(n, 0.f);
        recVhw.assign(n, 0.f); recVhh.assign(n, 0.f);
        int maxT = -1;
        for (const auto& sm : samples)
            if (!sm.empty()) maxT = std::max(maxT, sm.back().t);
        for (size_t i = 0; i < samples.size() && i < n; ++i) {
            const auto& sm = samples[i];
            if (sm.size() < 3 || sm.back().t != maxT) continue;
            const DynSample& a = sm[sm.size() - 3];
            const DynSample& b = sm[sm.size() - 2];
            const DynSample& c = sm.back();
            if (b.t != c.t - 1 || a.t != b.t - 1) continue;
            const float vx = c.cx - b.cx, vy = c.cy - b.cy, vr = c.rot - b.rot;
            if (std::fabs(vx - (b.cx - a.cx)) > 0.02f || std::fabs(vy - (b.cy - a.cy)) > 0.02f
                || std::fabs(vr - (b.rot - a.rot)) > 0.01f)
                continue;
            if (std::fabs(vx) < 0.05f && std::fabs(vy) < 0.05f && std::fabs(vr) < 0.005f) continue;
            recOk[i] = 1; recVx[i] = vx; recVy[i] = vy; recVr[i] = vr;
            recVhw[i] = c.hw - b.hw; recVhh[i] = c.hh - b.hh;
        }
    }
    // The ticks (1..kRecEndExtendTicks) object i goes on past its last row at recording tick tr, 0 when it holds.
    int recEndTicks(size_t i, int tr) const {
        if (!g_recEndExtend || i >= recOk.size() || !recOk[i]) return 0;
        const auto& sm = samples[i];
        if (sm.empty() || tr <= sm.back().t) return 0;
        return std::min(tr - sm.back().t, kRecEndExtendTicks);
    }
    struct RotTail { int a; double cx, cy, total, durT, erate; int ease; uint8_t lock, own; };   // own: no centre
    std::vector<std::vector<RotTail>> rotTail;
    std::vector<float> rotBoxW, rotBoxH;
    int rotTailMaxT = -1;
    struct RigTail { int t0 = -1; int lockA = 1 << 30, lockB = -1; std::vector<float> x, y, r; };
    std::vector<RigTail> rigTail;
    std::vector<std::vector<OnEvent>> tailOnEv;
    std::vector<uint8_t> tailOnBox;
    int tailRecEnd = -1;   // the recording's last tick over every object (the switches start after it)
    bool rotTailFrom(size_t i, int tl, int t, double& cx, double& cy, float& rot) const {
        bool moved = false;
        for (const RotTail& r : rotTail[i]) {
            const int F = g_autoTrig[(size_t)r.a].fireT;
            if (F < 0) continue;
            auto theta = [&](int tau) -> double {
                if (tau <= F) return 0.0;
                const double u = r.durT > 0.0 ? std::min(1.0, (double)(tau - F) / r.durT) : 1.0;
                return r.total * gdEase(r.ease, r.erate, u);
            };
            const double d = theta(t) - theta(tl);
            if (d == 0.0) continue;
            // GD's angle runs clockwise; in the y-up frame that is a turn by -d.
            if (!r.own) {
                const double a = -d * (3.14159265358979 / 180.0);
                const double c = std::cos(a), s = std::sin(a);
                const double rx = cx - r.cx, ry = cy - r.cy;
                cx = r.cx + c * rx - s * ry;
                cy = r.cy + s * rx + c * ry;
            }
            if (!r.lock) rot += (float)d;
            moved = true;
        }
        return moved;
    }
    // The turned-box axis at angle rotDeg (the --dynobbrot rule, for a placement that moved the angle).
    void recEndAxis(size_t i, double rotDeg) {
        // --rotobblatch: a placement that turns it past the recording latches it (and the latch stays)
        if (i < obbLatchT.size() && obbLatchT[i] >= 0 && !objs[i].obbOk) {
            double d = std::fmod(std::fabs(rotDeg - objs[i].rot), 360.0);
            if (d > 180.0) d = 360.0 - d;
            if (d > 0.001) objs[i].obbOk = 1;
        }
        if (!g_dynObbRot || !objs[i].obbOk || i >= everRot.size() || !everRot[i] || i >= obbBc0.size()) return;
        const double a = -(rotDeg - objs[i].rot) * (3.14159265358979 / 180.0);
        const double ca = std::cos(a), sa = std::sin(a);
        objs[i].bc = obbBc0[i] * ca - obbBs0[i] * sa;
        objs[i].bs = obbBc0[i] * sa + obbBs0[i] * ca;
    }    std::vector<float> trigDx, trigDy;
    std::vector<double> trigDur;
    std::vector<int> trigEase;
    std::vector<double> trigErate;
    std::vector<int> trigRecFire;
    std::vector<int> boxRecEntry;
    std::vector<int> recLag;
    std::vector<int> autoLag;
    std::vector<uint8_t> autoClosed;
    std::vector<int> autoAnchor;
    std::vector<uint8_t> anchorUnrec;
    std::vector<int> recAnchorX;
    std::vector<uint8_t> recSelfFire;
    std::vector<uint8_t> autoReach;
    std::vector<float> autoDx, autoDy;   // summed final offset (ease fallback)
    std::vector<double> autoDur;
    std::vector<int> autoEase;
    std::vector<double> autoErate;
    struct AutoPart {
        int trig; float dx, dy; double dur; int ease; double erate;
        // --movetarget only: see TrigCtl's fields of the same names.
        int mover = 0;
        uint8_t tmode = 0;
        float tdx = 0.f, tdy = 0.f;
        // --spawnlag: TrigCtl::spawnLag, the smallest over the paths merged into this part.
        int lag = 0;
    };
    std::vector<std::vector<AutoPart>> autoParts;
    std::vector<std::vector<AutoPart>> partsAny;   // --moverextend: every Move the object's groups listen to (loader)
    std::vector<uint8_t> rotSplit;
    std::vector<std::vector<AutoPart>> touchParts;
    std::vector<double> autoLock;
    std::vector<int> autoLockTrig;
    std::vector<int> lockMover;
    void prepareLockMover() {
        lockMover.assign(objs.size(), 0);
        for (size_t i = 0; i < objs.size() && i < autoLockTrig.size(); ++i) {
            const int k = autoLockTrig[i];
            if (k < 0 || (size_t)k >= g_autoTrig.size()) continue;
            const AutoTrig& A = g_autoTrig[(size_t)k];
            int mv = A.uid;
            for (const TrigCtl& c : A.ctl)
                if (c.uid == objs[i].uid && c.lockTicks > 0.0 && c.mover != 0) { mv = c.mover; break; }
            lockMover[i] = mv;
        }
    }
    std::vector<double> touchLock;
    bool autoLockXOnly(size_t i) const {
        if (i >= autoParts.size()) return true;
        for (const AutoPart& p : autoParts[i])
            if (std::fabs(p.dx) > 0.001f) return false;
        return true;
    }
    bool anyTrig = false;
    bool anyAuto = false;
    int lastT = -1;

    size_t size() const { return objs.size(); }

    uint64_t seekEpoch = 0;              // bumped by every seek() that rewrites
    uint64_t pureEpoch = ~0ull;          // what the pure objects were placed for:
    int pureT = -1;                      //   seek epoch, tick,
    std::vector<int> pureFire;           //   and every AutoTrig::fireT
    bool isPure(size_t i) const {
        return !trigMask[i]
            && !(i < onEv.size() && !onEv[i].empty())   // --activators: depends on the group
            && !(i < formula.size() && formula[i])
            && !(i < autoLockTrig.size() && autoLockTrig[i] >= 0)
            // --recendtail: the lock reads the group's x, a touch switch its boxes
            && !(i < rigTail.size() && rigTail[i].lockB >= 0)
            && !(i < tailOnBox.size() && tailOnBox[i])
            // --areamove: placed from the group's x
            && !(i < areaArmed.size() && areaArmed[i])
            // --tapdisarm: the on/off reads autoDisarmed, which asks the group's boxes
            && !(i < disarmDep.size() && disarmDep[i])
            // --boxarm: a Move of it fires only where the group's boxes switched it back on
            && !(i < armDep.size() && armDep[i]);
    }
    std::vector<uint8_t> armDep;
    std::vector<std::vector<uint32_t>> byBucket;
    size_t byBucketN = (size_t)-1;
    bool anyUidM1 = false;   // an object --dyndbg's default of -1 would name
    size_t nNonPure = 0;     // objects a same-tick applyTriggers still places
    void rebuildBuckets() {
        byBucket.assign(8, {});
        for (size_t i = 0; i < objs.size() && i < bucket.size(); ++i)
            if (bucket[i] < byBucket.size())
                byBucket[bucket[i]].push_back((uint32_t)i);
        anyUidM1 = false;
        for (const Obj& o : objs) anyUidM1 |= (o.uid == -1);
        nNonPure = 0;
        for (size_t i = 0; i < objs.size(); ++i) nNonPure += !isPure(i);
        byBucketN = objs.size();
    }
    static constexpr size_t kParMin = 512, kParRun = 64;
    template <class F> void forRuns(bool par, size_t work, F&& body) {
        const size_t n = objs.size();
        if (!par || !g_dynParTasks || work < kParMin) { body((size_t)0, n); return; }
        g_dynParTasks((n + kParRun - 1) / kParRun, [&](size_t k) {
            body(k * kParRun, std::min(n, (k + 1) * kParRun));
        });
    }

    void applyTriggers(TouchMask mask, int fireHi, const uint16_t* fireB,
                       float lockOff, int t, double px = 0.0) {
        if (!anyTrig && !anyAuto) return;
        // (serial caller: the per-object loops below are the parallel part)
        if (g_lockStop && lockMover.size() != objs.size()) prepareLockMover();
        const bool quiet = g_dynDbg == -1 && g_shiftDbgUid < 0 && !g_shiftStat
                           && !(g_rotCompute && !g_rotSpec.empty());
        bool samePure = quiet && pureEpoch == seekEpoch && pureT == t
                        && pureFire.size() == g_autoTrig.size();
        for (size_t k = 0; samePure && k < g_autoTrig.size(); ++k)
            samePure = (pureFire[k] == g_autoTrig[k].fireT);
        if (!samePure) {
            pureEpoch = seekEpoch;
            pureT = t;
            pureFire.resize(g_autoTrig.size());
            for (size_t k = 0; k < g_autoTrig.size(); ++k)
                pureFire[k] = g_autoTrig[k].fireT;
        }
        const bool par = quiet && !anyUidM1 && byBucketN == objs.size();
        auto place = [&](size_t b, size_t e, bool lockPass) {
        for (size_t i = b; i < e; ++i) {
            if (samePure && isPure(i)) continue;
            if (par && lockPass != (i < autoLockTrig.size() && autoLockTrig[i] >= 0))
                continue;
            const TouchMask m = trigMask[i];
            if (i < formula.size() && formula[i]
                && !(formula[i] == 2 && trigRecFire[i] >= 0)
                && !samples[i].empty()) {
                const long long lockReach = (g_recProvGiven && g_recProvRec >= 0) ? (long long)g_recProvReach
                                                                                  : (long long)samples[i].back().t;
                const bool lockObj = g_formulaNoLock && i < formulaLock.size() && formulaLock[i];
                const bool lockRead = lockObj && t <= lockReach;
                if ((g_formulaRec && formulaRecProvOk(t)) || lockRead) {
                    bool agree = true;
                    for (int b = 0; b < kTouchBits && agree; ++b) {
                        if (!(m & touchBit(b))) continue;
                        const int rec = g_touchRecEntry[(size_t)b];
                        const bool st = fireB && (mask & touchBit(b));
                        if (st) agree = rec >= 0 && (int)fireB[b] == rec;
                        else agree = !(rec >= 0 && (g_formulaRecTouch ? rec < t : rec <= t));
                    }
                    if (agree) {
                        const auto& sm = samples[i];
                        auto recAt = [&](int tt, double& rx, double& ry) {
                            size_t lo = 0, hi = sm.size();
                            while (lo + 1 < hi) {
                                const size_t md = (lo + hi) / 2;
                                if (sm[md].t <= tt) lo = md; else hi = md;
                            }
                            const DynSample& s = sm[lo];
                            rx = s.cx; ry = s.cy;
                            if (s.t < tt && lo + 1 < sm.size() && sm[lo + 1].t - s.t <= kRecInterpGap
                                && !recStallGap(sm, lo)) {
                                const DynSample& n = sm[lo + 1];
                                const double u = (double)(tt - s.t) / (double)(n.t - s.t);
                                rx = (double)s.cx + u * ((double)n.cx - (double)s.cx);
                                ry = (double)s.cy + u * ((double)n.cy - (double)s.cy);
                            }
                        };
                        double rx = 0.0, ry = 0.0, px = 0.0, py = 0.0;
                        const int lag = lockObj ? 1 : 0;
                        recAt(t - lag, rx, ry);
                        recAt(t - 1 - lag, px, py);
                        objs[i].cx = (float)rx;
                        objs[i].cy = (float)ry;
                        objs[i].dcy = ry - py;
                        if (g_dynDbg == objs[i].uid || g_dynDbg == -3)
                            std::printf("frec: uid=%d t=%d cx=%.3f cy=%.4f dcy=%+.4f\n",
                                        objs[i].uid, t, rx, ry, ry - py);
                        g_formulaRecHit.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    g_formulaRecMiss.fetch_add(1, std::memory_order_relaxed);
                }
                const DynSample& s0 = samples[i][0];
                int fb = -1;
                if (fireB)
                    for (int b = 0; b < kTouchBits; ++b)
                        if ((m & mask) & touchBit(b))
                            fb = std::max(fb, (int)fireB[b]);
                const int aa0 = autoAnchor[i];
                const int ft0 = (aa0 >= 0 && (size_t)aa0 < g_autoTrig.size())
                                ? autoFireT(aa0, mask, fireB) : -1;   // --boxarm
                auto stopTick = [&](int mover) -> int {
                    int st = 1 << 30;
                    if (mover == 0 || !fireB) return st;
                    const int nb = std::min((int)g_touchStops.size(), kTouchBits);
                    for (int b = 0; b < nb; ++b) {
                        if (!(mask & touchBit(b))) continue;
                        for (const int s : g_touchStops[(size_t)b])
                            if (s == mover)
                                st = std::min(st, (int)fireB[b] + (g_spawnLag ? 0 : 1));
                    }
                    return st;
                };
                auto plainAt = [&](int tt, double& ox, double& oy) {
                    ox = 0.0; oy = 0.0;
                    if (fireB)
                        for (const AutoPart& p : touchParts[i]) {
                            if (p.tmode) continue;
                            if (!((m & mask) & touchBit(p.trig))) continue;
                            // --spawnlag: a queued Spawn on the way starts it later
                            const int f = (int)fireB[p.trig] + (g_spawnLag ? p.lag : 0);
                            const int te = std::min(tt, stopTick(p.mover));
                            if (te <= f) continue;
                            const double e = p.dur > 0.0
                                ? gdEase(p.ease, p.erate,
                                         (double)(te - (f + 1)) / p.dur)
                                : 1.0;
                            ox += p.dx * e; oy += p.dy * e;
                        }
                    if (ft0 >= 0) {
                        int te = std::min(tt, stopTick(g_autoTrig[(size_t)aa0].uid));
                        if (g_autoStop) te = std::min(te, autoStopT(aa0));   // --autostop
                        if (te >= ft0) {
                            const double e = autoDur[i] > 0.0
                                ? gdEase(autoEase[i], autoErate[i],
                                         (double)(te - ft0) / autoDur[i])
                                : instantE(te, ft0);   // --instantlag
                            ox += autoDx[i] * e; oy += autoDy[i] * e;
                        }
                    }
                };
                auto targetAt = [&](int tt, double& ox, double& oy) {
                    plainAt(tt, ox, oy);
                    if (!fireB) return;
                    struct TP { int f; const AutoPart* p; double ddx, ddy; };
                    TP tp[8];
                    int n = 0;
                    for (const AutoPart& p : touchParts[i])
                        if (p.tmode && ((m & mask) & touchBit(p.trig)) && n < 8)
                            tp[n++] = {(int)fireB[p.trig] + (g_spawnLag ? p.lag : 0), &p,
                                       0.0, 0.0};
                    std::sort(tp, tp + n, [](const TP& a, const TP& b) { return a.f < b.f; });
                    auto contrib = [&](const TP& q, int at, double& cx, double& cy) {
                        cx = 0.0; cy = 0.0;
                        if (at <= q.f || q.p->dur <= 0.0) return;
                        const double e = gdEase(q.p->ease, q.p->erate,
                                                (double)(at - (q.f + 1)) / q.p->dur);
                        cx = q.ddx * e; cy = q.ddy * e;
                    };
                    for (int k = 0; k < n; ++k) {
                        const int s = tp[k].f + (g_spawnLag ? 0 : 1);
                        double bx = 0.0, by = 0.0;
                        plainAt(s, bx, by);
                        for (int j = 0; j < k; ++j) {
                            double cx, cy;
                            contrib(tp[j], s, cx, cy);
                            bx += cx; by += cy;
                        }
                        const int md = tp[k].p->tmode;   // 1 both, 2 x only, 3 y only
                        tp[k].ddx = (md == 3) ? 0.0 : (double)tp[k].p->tdx - bx;
                        tp[k].ddy = (md == 2) ? 0.0 : (double)tp[k].p->tdy - by;
                    }
                    for (int k = 0; k < n; ++k) {
                        double cx, cy;
                        contrib(tp[k], tt, cx, cy);
                        ox += cx; oy += cy;
                    }
                };
                const bool perBox = !touchParts[i].empty();
                auto offsetAt = [&](int tt, double& ox, double& oy) {
                    if (perBox) { targetAt(tt, ox, oy); return; }
                    ox = 0.0; oy = 0.0;
                    if (fb >= 0 && tt > fb) {
                        const double e = trigDur[i] > 0.0
                            ? gdEase(trigEase[i], trigErate[i],
                                     (double)(tt - (fb + 1)) / trigDur[i])
                            : 1.0;
                        ox += trigDx[i] * e; oy += trigDy[i] * e;
                    }
                    if (ft0 >= 0 && tt >= ft0) {
                        const int te = g_autoStop ? std::min(tt, autoStopT(aa0)) : tt;
                        const double e = autoDur[i] > 0.0
                            ? gdEase(autoEase[i], autoErate[i],
                                     (double)(te - ft0) / autoDur[i])
                            : instantE(te, ft0);   // --instantlag
                        if (te >= ft0) { ox += autoDx[i] * e; oy += autoDy[i] * e; }
                    }
                };
                double fx = 0.0, fy = 0.0, px1 = 0.0, py1 = 0.0;
                offsetAt(t, fx, fy);
                offsetAt(t - 1, px1, py1);
                if (g_dynDbg == objs[i].uid)
                    std::printf("formdbg t=%d uid=%d perBox=%d fb=%d ft0=%d "
                                "autoDur=%.2f autoDy=%.1f trigDur=%.2f fy=%.3f "
                                "base=%.3f -> %.3f\n",
                                t, objs[i].uid, perBox ? 1 : 0, fb, ft0,
                                autoDur[i], autoDy[i], trigDur[i], fy,
                                (double)s0.cy, (double)s0.cy + fy);
                const double ncy = (double)s0.cy + fy;
                objs[i].dcy = fy - py1;
                const bool locked = (g_lockBox >= 0 && i < touchLock.size()
                                     && touchLock[i] > 0.0
                                     && mask.test(g_lockBox));
                if (locked) objs[i].cx = (double)s0.cx + fx + (double)lockOff;
                else if (formula[i] == 2) objs[i].cx = (double)s0.cx + fx;
                objs[i].cy = ncy;
                if (formula[i] == 2 && objs[i].slope) {
                    const double dy = ncy - (double)baseCy[i];
                    objs[i].sy0 = (double)baseSy0[i] + dy;
                    objs[i].sy1 = (double)baseSy1[i] + dy;
                }
                if (objs[i].uid == g_dynDbg || g_dynDbg == -3)
                    std::printf("fdrv: uid=%d t=%d box=%d autoFireT=%d "
                                "base=%.3f lock=%d off=%.3f cx=%.3f "
                                "fy=%+.4f dcy=%+.4f -> cy=%.4f\n",
                                objs[i].uid, t, fb, ft0, (double)s0.cy,
                                locked ? 1 : 0, (double)lockOff, objs[i].cx,
                                fy, fy - py1, ncy);
                continue;
            }
            const int aA = m ? -1 : autoAnchor[i];   // touch control wins
            if (!m && aA < 0) {
                double ahw = objs[i].hw, ahh = objs[i].hh, arot = std::nan("");
                if (areaPlace(i, objs[i].cx, objs[i].cy, &ahw, &ahh, &arot)) {
                    if (!std::isnan(arot)) {   // a scale or rotate acts on it
                        objs[i].hw = ahw;
                        objs[i].hh = ahh;
                        recEndAxis(i, arot);
                    }
                    continue;
                }
            }
            if ((!m && aA < 0) || samples[i].empty()) continue;
            const auto& sm = samples[i];
            const DynSample& s0 = sm[0];
            double cx = s0.cx, cy = s0.cy, hw = s0.hw, hh = s0.hh;
            uint8_t onv = s0.on;
            float rotv = s0.rot;   // GD's own getRotation (see turnedBox)
            bool partsTail = false;
            double tailCx = 0.0, tailCy = 0.0, tailLastDx = 0.0, tailLastDy = 0.0;
            bool rigTailed = false;   // --recendtail placed it past the last row (the tails below stand down)
            bool fired; int anchor, recAnchor; float fdx, fdy;
            double fdur; int lat, fease; double ferate;
            int shiftUsed = 0;
            if (m) {
                fired = (mask & m).any();
                anchor = fireHi; recAnchor = trigRecFire[i];
                fdx = trigDx[i]; fdy = trigDy[i];
                fdur = trigDur[i]; lat = 5;
                fease = trigEase[i]; ferate = trigErate[i];
                if (trigRecFire[i] >= 0) {
                    if (g_recPhase & m) fired = true;
                    if (autoAnchor[i] >= 0
                        || (i < recSelfFire.size() && recSelfFire[i]
                            && t >= trigRecFire[i])
                        || (i < recAuto.size() && recAuto[i]))
                        fired = true;
                    anchor = recAnchor; lat = 0;
                    // --touchretime (see the flag): this state's own entry.
                    if (fireB && autoAnchor[i] < 0
                        && !(i < recAuto.size() && recAuto[i])
                        && !(i < autoReach.size() && autoReach[i])
                        && !(g_recPhase & m)) {
                        int fb = -1, fbBox = -1;
                        for (int b = 0; b < kTouchBits; ++b)
                            if (((m & mask) & touchBit(b)) && (int)fireB[b] > fb) {
                                fb = (int)fireB[b];
                                fbBox = b;
                            }
                        const int boxEntry =
                            (fbBox >= 0 && (size_t)fbBox < boxRecEntry.size())
                                ? boxRecEntry[(size_t)fbBox] : -1;
                        if (fb >= 0 && (long long)fb >= g_touchRetimeFrom)
                            anchor = (boxEntry >= 0)
                                ? fb + (trigRecFire[i] - boxEntry)
                                : fb + kTouchRetimeLat
                                      + ((i < recLag.size())
                                             ? std::max(0, recLag[i] - 1) : 0);
                    }
                }
            } else {
                const int f = autoFireT(aA, mask, fireB);   // --boxarm
                fired = (f >= 0 && f <= t);
                anchor = f;
                recAnchor = trigRecFire[i]
                    - (i < autoLag.size() ? autoLag[i] : recLag[i]);
                if (f >= 0 && i < anchorUnrec.size() && anchorUnrec[i])
                    recAnchor = f;
                // --recanchorx: the recording's first change was not the anchor's.
                else if (g_recAnchorX && i < recAnchorX.size() && recAnchorX[i] >= 0)
                    recAnchor = recAnchorX[i];
                fdx = autoDx[i]; fdy = autoDy[i];
                fdur = autoDur[i]; lat = 0;
                fease = autoEase[i]; ferate = autoErate[i];
            }
            if (g_shiftStat && fired) {
                if (shiftSaid.size() != objs.size())
                    shiftSaid.assign(objs.size(), 0);
                if (!(shiftSaid[i] & 4)) {
                    shiftSaid[i] |= 4;
                    std::printf("shiftres: uid=%d bucket=%d branch=%s anchor=%d "
                                "recFire=%d recLag=%d shift=%d ease=%d "
                                "rate=%.3f dur=%.3f dx=%.3f dy=%.3f t=%d\n",
                                objs[i].uid, (int)bucket[i],
                                m ? "touch" : "auto", anchor, trigRecFire[i],
                                (i < recLag.size() ? recLag[i] : -1),
                                (anchor >= 0 ? anchor - recAnchor : 0),
                                fease, ferate, fdur, (double)fdx, (double)fdy,
                                t);
                }
            }
            if (fired) {
                bool nearBox = false;
                if (m && (mask & m)) {
                    const TouchMask touched = mask & m;
                    for (int b = 0; b < kTouchBits; ++b)
                        if (touched.test(b)
                            && std::fabs((double)g_touchBoxU[b]
                                         - (double)objs[i].cx) < 400.0) {
                            nearBox = true;
                            break;
                        }
                }
                const bool recAutoObj = m && i < recAuto.size()
                                        && recAuto[i] != 0;
                const bool ownTouch = m && (mask & m) && nearBox
                    && trigRecFire[i] >= 0
                    && !recAutoObj && autoAnchor[i] >= 0
                    && fireHi > recAnchor + 8;
                if (trigRecFire[i] >= 0 && !autoClosed[i]) {
                    const int shift = (anchor >= 0) ? (anchor - recAnchor) : 0;
                    shiftUsed = shift;
                    int tt = t - shift;
                    if (ownTouch && tt > fireHi) {
                        const int hold = 60;
                        tt = (t - shift > fireHi + hold) ? (tt - hold) : fireHi;
                    } else if (recAutoObj && (mask & m)) {
                        int delay = 0;
                        for (const AutoPart& p : touchParts[i]) {
                            if (p.dy <= 0.f) continue;
                            if (!mask.test(p.trig)) continue;
                            const int F = g_touchFireT[p.trig & (kTouchBits - 1)];
                            if (F < 0 || t <= F) continue;
                            if (std::fabs((double)g_touchBoxU[p.trig & (kTouchBits - 1)]
                                          - (double)objs[i].cx) >= 400.0)
                                continue;
                            if (g_dynDbg == objs[i].uid)
                                std::printf("holddbg t=%d uid=%d box=%d F=%d recAnchor=%d recFire=%d dy=%.3f add=%d\n",
                                            t, objs[i].uid, p.trig, F, recAnchor, trigRecFire[i], (double)p.dy,
                                            std::min(60, t - F));
                            if (g_holdDedup && trigRecFire[i] >= 0 && std::abs(F - recAnchor) <= 16)
                                continue;
                            delay += std::min(60, t - F);
                        }
                        tt -= delay;
                    }
                    if (g_shiftDbgUid >= 0 && objs[i].uid == g_shiftDbgUid
                        && !g_shiftDbgDone) {
                        g_shiftDbgDone = true;
                        std::printf("shiftdbg: uid=%d anchor=%d recAnchor=%d "
                                    "shift=%d t=%d tt=%d touch=%d\n",
                                    objs[i].uid, anchor, recAnchor, shift, t,
                                    tt, m ? 1 : 0);
                    }
                    size_t lo = 0, hi = sm.size();
                    while (lo + 1 < hi) {
                        const size_t mid = (lo + hi) / 2;
                        if (sm[mid].t <= tt) lo = mid; else hi = mid;
                    }
                    const DynSample& s = sm[lo];
                    cx = s.cx; cy = s.cy; hw = s.hw; hh = s.hh; onv = recOnAt(sm, lo, tt);   // --offafter
                    rotv = s.rot;
                    if ((g_moverExtend || g_moverExtendVel) && i < extOk.size() && extOk[i] && lo + 1 == sm.size() && tt > s.t) {
                        const float k = (float)std::min(tt - s.t, extCap[i]);
                        cx = s.cx + extVx[i] * k;
                        cy = s.cy + extVy[i] * k;
                    }
                    const bool leadingGap = m
                                            && trigRecFire[i] >= 0
                                            && tt < trigRecFire[i];
                    if (lo + 1 < sm.size() && !leadingGap && !recStallGap(sm, lo)) {
                        const DynSample& n = sm[lo + 1];
                        const int span = n.t - s.t;
                        if (span > 0 && tt > s.t) {
                            const double u = (double)(tt - s.t) / (double)span;
                            const auto lerp = [u](float a, float b) {
                                const double d = (double)b - (double)a;
                                return std::fabs(d) < 2.0 * kRecEps
                                           ? (float)((double)a + d * u) : a;
                            };
                            cx = lerp(s.cx, n.cx);
                            cy = lerp(s.cy, n.cy);
                            rotv = lerp(s.rot, n.rot);
                        }
                    }
                    double bdx = fdx, bdy = fdy, bdur = fdur;
                    int bease = fease; double berate = ferate;
                    if (recAutoObj) {
                        bdur = 0.0;
                        const double dirY = (double)s.cy - (double)s0.cy;
                        float best = 0.f;
                        for (const AutoPart& p : touchParts[i])
                            if ((double)p.dy * dirY > 0.0
                                && std::fabs(p.dy) > best) {
                                best = std::fabs(p.dy);
                                bdx = p.dx; bdy = p.dy; bdur = p.dur;
                                bease = p.ease; berate = p.erate;
                            }
                    }
                    bool partsDone = false;
                    const bool ctlTail = g_ctlAutoTail && m && !(mask & m).any() && fdx == 0.f
                                         && fdy == 0.f && autoParts[i].empty() && i < partsAny.size()
                                         && !partsAny[i].empty();
                    if (lo + 1 == sm.size() && tt > s.t && !recAutoObj
                        && (autoParts[i].size() >= 2 || ctlTail)
                        && !(i < rotSplit.size() && rotSplit[i])) {
                        const int tEnd = s.t + (t - tt);   // the last row on this state's clock
                        if (ctlTail) { cx = s.cx; cy = s.cy; }
                        const bool fromLast = (double)s.cx == cx && (double)s.cy == cy;   // --recendresid
                        for (const AutoPart& p : (ctlTail ? partsAny[i] : autoParts[i])) {
                            const int f = autoFireT(p.trig, mask, fireB);   // --boxarm
                            if (f < 0) continue;
                            auto eAt = [&](int tk) {
                                if (tk < f) return 0.0;
                                return p.dur > 0.0
                                    ? gdEase(p.ease, p.erate, (double)(tk - f) / p.dur)
                                    : instantE(tk, f);   // --instantlag
                            };
                            const double de = eAt(t) - eAt(tEnd);
                            cx += p.dx * de;
                            cy += p.dy * de;
                            if (g_recEndResid) {
                                const double dl = eAt(tEnd) - eAt(tEnd - 1);
                                tailLastDx += p.dx * dl;
                                tailLastDy += p.dy * dl;
                            }
                        }
                        partsDone = true;
                        if (g_recEndResid && fromLast) { partsTail = true; tailCx = cx; tailCy = cy; }
                    }
                    if (!partsDone && lo + 1 == sm.size() && tt > s.t && bdur > 0) {
                        const double full = std::hypot((double)bdx, (double)bdy);
                        const double done = std::hypot((double)s.cx - (double)s0.cx,
                                                       (double)s.cy - (double)s0.cy);
                        if (full > 0.5 && done < full - 0.5) {
                            auto ease = [&](double u) {
                                return gdEase(bease, berate, u);
                            };
                            const double uEnd =
                                (double)(s.t - recAnchor) / bdur;
                            const double uNow =
                                (double)(tt - recAnchor) / bdur;
                            const double eEnd = ease(uEnd);
                            if (eEnd < 0.999) {
                                const double de =
                                    (ease(uNow) - eEnd) / (1.0 - eEnd);
                                cx += ((double)s0.cx + (double)bdx - (double)s.cx) * de;
                                cy += ((double)s0.cy + (double)bdy - (double)s.cy) * de;
                            }
                        }
                    }
                    if (g_recEndTail && i < rigTail.size() && !rigTail[i].x.empty()
                        && lo + 1 == sm.size() && tt > s.t && rigTail[i].t0 == s.t) {
                        const RigTail& R = rigTail[i];
                        const size_t k = std::min((size_t)(tt - R.t0), R.x.size() - 1);
                        cx = R.x[k]; cy = R.y[k]; rotv = R.r[k];
                        if (tt >= R.lockA && tt <= R.lockB) cx += px - xRefAt(tt);
                        rigTailed = true;
                        partsTail = false;
                        recEndAxis(i, (double)rotv);
                        if (objs[i].obbOk) {
                            hw = objs[i].bhw * std::fabs(objs[i].bc) + objs[i].bhh * std::fabs(objs[i].bs);
                            hh = objs[i].bhw * std::fabs(objs[i].bs) + objs[i].bhh * std::fabs(objs[i].bc);
                        }
                    }
                    if (ownTouch) {
                        double u = 1.0;
                        if (fdur > 0) u = (double)(t - fireHi - 5) / fdur;
                        if (u > 0.0) {
                            const double e = gdEase(fease, ferate, u);
                            cx += fdx * e;
                            cy += fdy * e;
                        }
                    } else if (recAutoObj && (mask & m)) {
                        int fireMin = 1 << 30;
                        int nHit = 0;   // boxes the state touched that move this object (two on the same tick are both fireMin)
                        for (const AutoPart& q : touchParts[i]) {
                            if (!mask.test(q.trig)) continue;
                            const int Fq = g_touchFireT[q.trig & (kTouchBits - 1)];
                            if (Fq >= 0) { fireMin = std::min(fireMin, Fq); ++nHit; }
                        }
                        for (const AutoPart& p : touchParts[i]) {
                            if (p.dy >= 0.f) continue;
                            if (!mask.test(p.trig)) continue;
                            const int F = g_touchFireT[p.trig & (kTouchBits - 1)];
                            if (F < 0 || t < F + 5) continue;
                            if (std::fabs((double)g_touchBoxU[p.trig & (kTouchBits - 1)]
                                          - (double)objs[i].cx) >= 400.0)
                                continue;
                            const int pb = p.trig & (kTouchBits - 1);
                            const bool dup = (g_slamBox && g_recTouchGiven)
                                ? slamBoxRecorded(g_recTouchBit, pb, F)
                                : (g_slamDedup && trigRecFire[i] >= 0 && nHit == 1 && F == fireMin && std::abs(F - recAnchor) <= 8);
                            if (g_dynDbg == objs[i].uid)
                                std::printf("slamdbg t=%d uid=%d box=%d F=%d recAnchor=%d recFire=%d rec=%d dy=%.3f dedup=%d\n",
                                            t, objs[i].uid, p.trig, F, recAnchor, trigRecFire[i],
                                            g_recTouchBit[(size_t)pb], (double)p.dy, dup ? 1 : 0);
                            if (dup) continue;
                            cx += p.dx;
                            cy += p.dy;
                        }
                    }
                    if (i < rotSplit.size() && rotSplit[i]) {
                        for (const AutoPart& p : autoParts[i]) {
                            const int f = autoFireT(p.trig, mask, fireB);   // --boxarm
                            if (f < 0 || t < f) continue;
                            // --autostop: a halted Move holds where its Stop left it
                            const int te = g_autoStop ? std::min(t, autoStopT(p.trig)) : t;
                            if (te < f) continue;
                            const double e = (p.dur > 0.0)
                                ? gdEase(p.ease, p.erate, (double)(te - f) / p.dur)
                                : instantE(te, f);   // --instantlag
                            cx += p.dx * e;
                            cy += p.dy * e;
                        }
                    }
                    if (g_lockX && !rigTailed && aA >= 0 && autoLockTrig[i] >= 0 && autoLockXOnly(i)
                        && autoFireT(autoLockTrig[i], mask, fireB) >= 0) {   // --boxarm
                        AutoTrig& A = g_autoTrig[(size_t)autoLockTrig[i]];
                        const int lockT0 = A.fireT - A.delay;
                        int lockEnd = lockT0 + (int)autoLock[i];
                        if (g_autoStop) lockEnd = std::min(lockEnd, autoStopT(autoLockTrig[i]));
                        if (g_lockStop) lockEnd = std::min(lockEnd, touchStopTick(mask, fireB, i < lockMover.size() ? lockMover[i] : A.uid));
                        cx = s0.cx;
                        if (t >= lockT0 && t <= lockEnd) {
                            A.lockLastX = px;
                            cx += px - A.fireX;
                        } else if (t > lockEnd) {
                            cx += A.lockLastX - A.fireX;
                        }
                        if (g_dynDbg == objs[i].uid && (t % 40 == 0))
                            std::printf("lockxdbg t=%d uid=%d lockT0=%d lockEnd=%d fireX=%.3f "
                                        "px=%.3f cx=%.3f\n", t, objs[i].uid, lockT0, lockEnd,
                                        A.fireX, px, cx);
                    }
                    if (g_dynDbg == objs[i].uid && (t % 40 == 0))
                        std::printf("armdbg t=%d uid=%d arm=recording split=%d "
                                    "parts=%zu cx=%.3f cy=%.3f\n",
                                    t, objs[i].uid,
                                    (int)(i < rotSplit.size() ? rotSplit[i] : 0),
                                    autoParts[i].size(), cx, cy);
                } else {
                    if (aA >= 0 && autoLockTrig[i] >= 0
                        && autoFireT(autoLockTrig[i], mask, fireB) >= 0) {   // --boxarm
                        AutoTrig& A = g_autoTrig[(size_t)autoLockTrig[i]];
                        const int lockT0 = A.fireT - A.delay;
                        int lockEnd = lockT0 + (int)autoLock[i];
                        if (g_autoStop) lockEnd = std::min(lockEnd, autoStopT(autoLockTrig[i]));
                        // --lockstop: ...and so does one in a touch chain this world fired.
                        if (g_lockStop) lockEnd = std::min(lockEnd, touchStopTick(mask, fireB, i < lockMover.size() ? lockMover[i] : A.uid));
                        if (t >= lockT0 && t <= lockEnd) {
                            A.lockLastX = px;
                            cx += px - A.fireX;
                        } else if (t > lockEnd) {
                            cx += A.lockLastX - A.fireX;
                        }
                    }
                    if (trigRecFire[i] >= 0 && sm.size() > 1) {
                        const int shift = (anchor >= 0) ? (anchor - recAnchor) : 0;
                        const int tt = t - shift;
                        size_t lo = 0, hi = sm.size();
                        while (lo + 1 < hi) {
                            const size_t mid = (lo + hi) / 2;
                            if (sm[mid].t <= tt) lo = mid; else hi = mid;
                        }
                        hw = sm[lo].hw; hh = sm[lo].hh; onv = recOnAt(sm, lo, tt);   // --offafter
                        rotv = sm[lo].rot;
                    }
                    if (recAutoObj) {
                        for (const AutoPart& p : touchParts[i]) {
                            if (!mask.test(p.trig)) continue;
                            const int F = g_touchFireT[p.trig & (kTouchBits - 1)];
                            if (F < 0) continue;
                            double u = 1.0;
                            if (p.dur > 0) u = (double)(t - F - 5) / p.dur;
                            if (u <= 0.0) continue;
                            const double e = gdEase(p.ease, p.erate, u);
                            cx += p.dx * e;
                            cy += p.dy * e;
                        }
                    } else if (m || autoParts[i].empty()) {
                        double u = 1.0;
                        if (anchor >= 0 && fdur > 0)
                            u = (double)(t - anchor - lat) / fdur;
                        const double e = gdEase(fease, ferate, u);
                        cx += fdx * e;
                        cy += fdy * e;
                    } else {
                        // Superposition: every controller on its own clock.
                        if (g_dynDbg == objs[i].uid && (t % 40 == 0))
                            for (const AutoPart& p : autoParts[i])
                                std::printf("armdbg   part t=%d uid=%d trig=%d "
                                            "fireT=%d d=(%.1f,%.1f) dur=%.2f\n",
                                            t, objs[i].uid, p.trig,
                                            g_autoTrig[(size_t)p.trig].fireT,
                                            (double)p.dx, (double)p.dy, p.dur);
                        for (const AutoPart& p : autoParts[i]) {
                            const int f = autoFireT(p.trig, mask, fireB);   // --boxarm
                            if (f < 0 || t < f) continue;
                            // --autostop: a halted Move holds where its Stop left it
                            const int te = g_autoStop ? std::min(t, autoStopT(p.trig)) : t;
                            if (te < f) continue;
                            const double e = (p.dur > 0.0)
                                ? gdEase(p.ease, p.erate, (double)(te - f) / p.dur)
                                : instantE(te, f);   // --instantlag
                            cx += p.dx * e;
                            cy += p.dy * e;
                        }
                    }
                    if (g_dynDbg == objs[i].uid && (t % 40 == 0))
                        std::printf("armdbg t=%d uid=%d branch=%s m=%u "
                                    "parts=%zu recAutoObj=%d anchor=%d "
                                    "cy=%.3f\n", t, objs[i].uid,
                                    recAutoObj ? "recAuto"
                                        : (m || autoParts[i].empty())
                                            ? "collapsed" : "superpos",
                                    (unsigned)m.word(0), autoParts[i].size(),
                                    (int)recAutoObj, anchor, cy);
                    if (g_dynDbg == objs[i].uid && t == 11720)
                        for (const AutoPart& p : autoParts[i])
                            std::printf("armdbg   part trig=%d uid=%d cx=%.1f "
                                        "fireT=%d d=(%.2f,%.2f) dur=%.1f\n",
                                        p.trig, g_autoTrig[(size_t)p.trig].uid,
                                        g_autoTrig[(size_t)p.trig].cx,
                                        g_autoTrig[(size_t)p.trig].fireT,
                                        p.dx, p.dy, p.dur);
                }
            }
            if (g_rotCompute && !g_rotSpec.empty() && !rigTailed) {
                const auto rs = g_rotSpec.find(objs[i].uid);
                if (rs != g_rotSpec.end() && rs->second.anchor >= 0
                    && rs->second.centreIdx >= 0) {
                    const RotSpec& R = rs->second;
                    const int f = g_autoTrig[(size_t)R.anchor].fireT;
                    if (f >= 0 && t >= f) {
                        const size_t ci = (size_t)R.centreIdx;
                        double ccx, ccy;
                        if (samples[ci].size() > 1) {
                            ccx = objs[ci].cx;
                            ccy = objs[ci].cy;
                        } else {
                            ccx = samples[ci][0].cx;
                            ccy = samples[ci][0].cy;
                            for (const AutoPart& p : autoParts[ci]) {
                                const int cf = autoFireT(p.trig, mask, fireB);   // --boxarm
                                if (cf < 0 || t < cf) continue;
                                const double e = (p.dur > 0.0)
                                    ? gdEase(p.ease, p.erate,
                                             (double)(t - cf) / p.dur)
                                    : instantE(t, cf);   // --instantlag
                                ccx += p.dx * e;
                                ccy += p.dy * e;
                            }
                        }
                        const double th = -R.total
                            * gdEase(R.ease, R.erate, (double)(t - f) / R.durT)
                            * 3.14159265358979 / 180.0;
                        const double c = std::cos(th), s = std::sin(th);
                        const double rx = cx - ccx, ry = cy - ccy;
                        cx = ccx + c * rx - s * ry;
                        cy = ccy + s * rx + c * ry;
                    }
                }
            }
            if (g_shiftDbgUid >= 0 && objs[i].uid == g_shiftDbgUid
                && t % 20 == 0)
                std::printf("dynpos: uid=%d t=%d cx=%.2f cy=%.2f on=%d\n",
                            objs[i].uid, t, cx, cy, (int)onv);
            bool rotTailed = rigTailed;   // --recendtail: already turned on, nothing below adds to it
            if (g_rotTail && !rigTailed && i < rotTail.size() && !rotTail[i].empty()) {
                const DynSample& L = sm.back();
                if ((double)L.cx == cx && (double)L.cy == cy && t - shiftUsed > rotTailMaxT
                    && rotTailFrom(i, rotTailMaxT + shiftUsed, t, cx, cy, rotv)) {
                    rotTailed = true;
                    if (rotBoxW[i] > 0.f) {
                        const double th = (double)rotv * (3.14159265358979 / 180.0);
                        const double c = std::fabs(std::cos(th)), s = std::fabs(std::sin(th));
                        hw = 0.5 * ((double)rotBoxW[i] * c + (double)rotBoxH[i] * s);
                        hh = 0.5 * ((double)rotBoxW[i] * s + (double)rotBoxH[i] * c);
                    }
                    recEndAxis(i, (double)rotv);
                }
            }
            if (g_recEndExtend && !rotTailed) {
                const DynSample& L = sm.back();
                const bool resid = partsTail && cx == tailCx && cy == tailCy
                                   && (t - shiftUsed) - L.t <= kRecEndExtendTicks;
                if (((double)L.cx == cx && (double)L.cy == cy) || resid)
                    if (const int k = recEndTicks(i, t - shiftUsed)) {
                        const double rdx = resid ? tailLastDx : 0.0, rdy = resid ? tailLastDy : 0.0;
                        cx += ((double)recVx[i] - rdx) * k; cy += ((double)recVy[i] - rdy) * k;
                        hw += (double)recVhw[i] * k; hh += (double)recVhh[i] * k;
                        rotv += recVr[i] * (float)k;
                        recEndAxis(i, (double)rotv);
                    }
            }
            {
                double ax = 0.0, ay = 0.0, ahw = hw, ahh = hh, arot = std::nan("");
                if (areaPlace(i, ax, ay, &ahw, &ahh, &arot)) {
                    cx = ax;
                    cy = ay;
                    if (!std::isnan(arot)) {
                        hw = ahw;
                        hh = ahh;
                        rotv = (float)arot;
                        recEndAxis(i, arot);
                    }
                }
            }
            objs[i].cx = cx; objs[i].cy = cy; objs[i].hw = hw; objs[i].hh = hh;
            if (lastStep1 && i < prevCy.size())
                objs[i].dcy = (double)cy - (double)prevCy[i];
            bool offBoxHeld = false;
            if (g_boxOffOwn && i < offBox.size() && !offBox[i].empty()) {
                offBoxHeld = true;
                if (fireB)
                    for (const uint8_t b : offBox[i])
                        if ((mask & touchBit((int)b)) && (int)fireB[b] <= t) {
                            offBoxHeld = false;
                            break;
                        }
            }
            if (!offBoxHeld && cur[i] < sm.size() && sm[cur[i]].t <= t
                && recOnAt(sm, cur[i], t - shiftUsed) == 0)   // --offafter: the row's own clock
                onv = 0;
            {
                bool noMove = fdx == 0.f && fdy == 0.f;
                for (const AutoPart& p : autoParts[i])
                    if (p.dx != 0.f || p.dy != 0.f) { noMove = false; break; }
                if (!m && noMove
                    && (bucket[i] == PORT || bucket[i] == SPEED)
                    && cur[i] < sm.size() && sm[cur[i]].t <= t) {
                    uint8_t v = sm[cur[i]].on;
                    if (v && sm[cur[i]].t == t && cur[i] > 0
                        && !sm[cur[i] - 1].on)
                        v = 0;
                    onv = v;
                }
            }
            if (g_recEndTail && i < tailOnEv.size() && !tailOnEv[i].empty()) {
                const int after = std::max(sm.back().t, tailRecEnd) + shiftUsed;
                if (t > after)
                    onv = resolveOnAfter(sm.back().on != 0, after, tailOnEv[i].data(), tailOnEv[i].size(),
                                         mask, fireB, t) ? 1 : 0;
            }
            // --activators: this object's on/off is its switches', not the recording's.
            if (i < onEv.size() && !onEv[i].empty())
                onv = resolveOn(onInit[i] != 0, onEv[i].data(), onEv[i].size(), mask, fireB, t)
                          ? 1 : 0;
            if (i < disarmEv.size() && !disarmEv[i].empty() && fireB) {
                bool any = false;
                for (const OnEvent& e : disarmEv[i])
                    if (e.autoIdx >= 0 && (size_t)e.autoIdx < g_autoTrig.size()
                        && g_autoTrig[(size_t)e.autoIdx].fireT >= 0
                        && (double)g_autoTrig[(size_t)e.autoIdx].fireT + e.delay <= (double)t
                        && autoDisarmed(e.autoIdx, mask, fireB)) {
                        any = true;
                        break;
                    }
                if (any)
                    onv = (disarmInit[i] != 0
                               ? resolveOnGroups(disarmEv[i].data(), disarmEv[i].size(), mask, fireB, t)
                               : resolveOn(false, disarmEv[i].data(), disarmEv[i].size(), mask, fireB, t))
                              ? 1 : 0;
                if (any && g_dynDbg == objs[i].uid) {   // --dyndbg: which switches it took, which were stopped
                    std::printf("tapdisarm t=%d uid=%d init=%d ->%d:", t, objs[i].uid, (int)disarmInit[i], (int)onv);
                    for (const OnEvent& e : disarmEv[i]) {
                        if (e.autoIdx >= 0 && (size_t)e.autoIdx < g_autoTrig.size()) {
                            const AutoTrig& A = g_autoTrig[(size_t)e.autoIdx];
                            if (A.fireT < 0 || A.fireT > t) continue;
                            std::printf(" u%d@%d:%d%s", A.uid, A.fireT, (int)e.on,
                                        autoDisarmed(e.autoIdx, mask, fireB) ? "X" : "");
                            for (const AutoDisarm& d : g_autoDisarm[(size_t)e.autoIdx])
                                if (d.box >= 0 && d.box < kTouchBits && (mask & touchBit(d.box)))
                                    std::printf("(b%d@%d:%d)", d.box, (int)fireB[d.box], (int)d.on);
                        } else if (e.box >= 0 && (mask & touchBit(e.box)))
                            std::printf(" box%d@%d:%d", e.box, (int)fireB[e.box], (int)e.on);
                    }
                    std::printf("\n");
                }
            }
            // --boxoff: out once a box that switches it off has fired in this group's world.
            if (i < offBox.size() && fireB)
                for (const uint8_t b : offBox[i])
                    if ((mask & touchBit((int)b)) && (int)fireB[b] <= t) { onv = 0; break; }
            on[i] = onv;
            objs[i].recOn = onv;   // --holdtap (object.hpp)
            if (objs[i].pendBox >= 0)   // --pressfresh (Obj::pendOff)
                objs[i].pendOff = !onv && !(mask & touchBit(objs[i].pendBox));
            if (g_dynDbg == objs[i].uid)
                std::printf("dyntrig t=%d uid=%d fired=%d m=%08x cx=%.3f "
                            "cy=%.3f hw=%.1f hh=%.1f on=%d\n",
                            t, objs[i].uid, (int)fired, (unsigned)m.word(0), cx, cy,
                            hw, hh, (int)onv);
            if (g_shiftStat) {
                if (shiftSaid.size() != objs.size())
                    shiftSaid.assign(objs.size(), 0);
                if (!(shiftSaid[i] & 1)) {
                    shiftSaid[i] |= 1;
                    std::printf("shiftstat: uid=%d bucket=%d t=%d\n",
                                objs[i].uid, (int)bucket[i], t);
                }
                if (shiftUsed != 0 && !(shiftSaid[i] & 2)) {
                    shiftSaid[i] |= 2;
                    std::printf("shiftnz: uid=%d bucket=%d shift=%d t=%d "
                                "anchor=%d recAnchor=%d\n",
                                objs[i].uid, (int)bucket[i], shiftUsed, t,
                                anchor, recAnchor);
                }
            }
            turnedBox(objs[i], bucket[i], (double)rotv,
                      i < everRot.size() && everRot[i] != 0);
            // a moving SLOPE carries its surface line (same rule as seek())
            if (objs[i].slope) {
                const double dy = cy - (double)baseCy[i];
                objs[i].sy0 = (double)baseSy0[i] + dy;
                objs[i].sy1 = (double)baseSy1[i] + dy;
            }
        }
        };
        forRuns(par, samePure ? nNonPure : objs.size(),
                [&](size_t b, size_t e) { place(b, e, false); });
        if (par) place(0, objs.size(), true);
    }

    void forgetPlacement() { lastT = 1 << 30; }   // later than any tick: the next seek goes back
    void saveCarry(std::vector<double>& cy) const {
        cy.resize(objs.size());
        for (size_t i = 0; i < objs.size(); ++i) cy[i] = objs[i].cy;
    }
    void loadCarry(const std::vector<double>& cy, int tPrev) {
        for (size_t i = 0; i < objs.size() && i < cy.size(); ++i) objs[i].cy = cy[i];
        lastT = tPrev;
    }
    void resetCursors() { std::fill(cur.begin(), cur.end(), (size_t)0); }
    void foldSticky(uint64_t& h, bool withLastT = true) const {
        auto mix = [&h](const void* p, size_t n) {
            const unsigned char* b = static_cast<const unsigned char*>(p);
            for (size_t k = 0; k < n; ++k) { h ^= b[k]; h *= 1099511628211ull; }
        };
        for (const Obj& o : objs) {
            mix(&o.oriented, sizeof o.oriented);
            mix(&o.ohw, sizeof o.ohw); mix(&o.ohh, sizeof o.ohh);
            mix(&o.rc, sizeof o.rc); mix(&o.rs, sizeof o.rs);
        }
        if (withLastT) mix(&lastT, sizeof lastT);
    }
    void seek(int t) {
        if (objs.empty() || t == lastT) return;
        if ((g_moverExtend || g_moverExtendVel) && extOk.size() != objs.size()) prepareExtend();
        if (g_recEndExtend && recOk.size() != objs.size()) prepareRecEnd();
        ++seekEpoch;
        if (byBucketN != objs.size()) rebuildBuckets();
        const bool back = (t < lastT);
        const bool step1 = !back && (t == lastT + 1);
        lastStep1 = step1;
        lastT = t;
        if (g_dynObbRot && obbBc0.size() != objs.size()) {
            obbBc0.resize(objs.size()); obbBs0.resize(objs.size());
            for (size_t i = 0; i < objs.size(); ++i) { obbBc0[i] = objs[i].bc; obbBs0[i] = objs[i].bs; }
        }
        forRuns(g_dynDbg == -1 && !anyUidM1, objs.size(), [&](size_t b, size_t e) {
        for (size_t i = b; i < e; ++i) {
            const auto& sm = samples[i];
            if (sm.empty()) continue;
            size_t c = back ? 0 : cur[i];
            while (c + 1 < sm.size() && sm[c + 1].t <= t) ++c;
            cur[i] = c;
            const DynSample& s = sm[c];
            if (i < prevCy.size())
                prevCy[i] = step1 ? objs[i].cy : (float)s.cy;
            objs[i].dcy =
                step1 ? ((double)s.cy - (double)objs[i].cy)
                      : (c > 0 && sm[c].t == t && sm[c - 1].t == t - 1)
                            ? ((double)sm[c].cy - (double)sm[c - 1].cy)
                            : 0.0;
            objs[i].cx = s.cx; objs[i].cy = s.cy;
            if (s.t < t && c + 1 < sm.size()
                && sm[c + 1].t - s.t <= kRecInterpGap
                && !recStallGap(sm, c)) {   // --recstall
                const DynSample& n = sm[c + 1];
                const double u = (double)(t - s.t) / (double)(n.t - s.t);
                objs[i].cx = (float)((double)s.cx + u * ((double)n.cx - (double)s.cx));
                objs[i].cy = (float)((double)s.cy + u * ((double)n.cy - (double)s.cy));
                if (step1 && i < prevCy.size())
                    objs[i].dcy = (double)objs[i].cy - (double)prevCy[i];
            }
            const bool mvExt = (g_moverExtend || g_moverExtendVel) && i < extOk.size() && extOk[i]
                               && c + 1 == sm.size() && t > s.t;
            if (mvExt) {
                const float k = (float)std::min(t - s.t, extCap[i]);
                objs[i].cx = s.cx + extVx[i] * k;
                objs[i].cy = s.cy + extVy[i] * k;
                if (step1 && i < prevCy.size())
                    objs[i].dcy = (double)objs[i].cy - (double)prevCy[i];
            }
            float hwNow = s.hw, hhNow = s.hh;
            double rotNow = (double)s.rot;
            // --recendextend: past the last row by the last steady step, where --moverextend did not place it.
            if (!mvExt && c + 1 == sm.size())
                if (const int k = recEndTicks(i, t)) {
                    objs[i].cx = s.cx + recVx[i] * (float)k;
                    objs[i].cy = s.cy + recVy[i] * (float)k;
                    hwNow += recVhw[i] * (float)k; hhNow += recVhh[i] * (float)k;
                    rotNow += (double)recVr[i] * k;
                    if (step1 && i < prevCy.size())
                        objs[i].dcy = (double)objs[i].cy - (double)prevCy[i];
                }
            objs[i].hw = hwNow; objs[i].hh = hhNow;
            on[i] = recOnAt(sm, c, t);   // --offafter
            objs[i].recOn = s.on;   // --holdtap (object.hpp)
            objs[i].pendOff = 0;   // --pressfresh: applyTriggers decides it per group
            turnedBox(objs[i], bucket[i], rotNow,
                      i < everRot.size() && everRot[i] != 0);
            obbLatch(i, t, rotNow);   // --rotobblatch
            if (g_dynObbRot && objs[i].obbOk && i < everRot.size() && everRot[i]
                && i < obbBc0.size()) {
                const double a = -(rotNow - objs[i].rot) * (3.14159265358979 / 180.0);
                const double ca = std::cos(a), sa = std::sin(a);
                objs[i].bc = obbBc0[i] * ca - obbBs0[i] * sa;
                objs[i].bs = obbBc0[i] * sa + obbBs0[i] * ca;
            }
            if (objs[i].slope) {
                const double dy = (double)objs[i].cy - (double)baseCy[i];
                objs[i].sy0 = (double)baseSy0[i] + dy;
                objs[i].sy1 = (double)baseSy1[i] + dy;
            }
            if (g_dynDbg == objs[i].uid)
                std::printf("dynseek t=%d uid=%d cur=%zu/%zu cy=%.3f "
                            "(sample t=%d) at=%p\n",
                            t, objs[i].uid, c, sm.size(), (double)objs[i].cy,
                            s.t, (const void*)&objs[i]);
        }
        });
    }
    static void turnedBox(Obj& o, uint8_t bucketOf, double rotDeg,
                          bool everRot = false) {
        if (bucketOf != PORT && !(g_rotSpeed && bucketOf == SPEED)) return;
        o.rot = rotDeg;
        if (o.oriented) {
            const double th2 = rotDeg * 3.14159265358979 / 180.0;
            o.rc = std::cos(-th2); o.rs = std::sin(-th2);
            return;
        }
        const double om = std::fabs(std::fmod(rotDeg, 90.0));
        if (om <= 0.5 && everRot && o.hw > 0.5 && o.hh > 0.5) {
            o.oriented = 1;
            o.ohw = o.hw; o.ohh = o.hh;   // axis aligned: the bound IS the box
            if (((long long)std::llround(rotDeg / 90.0)) & 1)
                std::swap(o.ohw, o.ohh);
            const double th0 = rotDeg * 3.14159265358979 / 180.0;
            o.rc = std::cos(-th0); o.rs = std::sin(-th0);
            return;
        }
        if (om <= 0.5 || om >= 89.5) return;   // axis aligned: bound == shape
        const double th = rotDeg * 3.14159265358979 / 180.0;
        const double c = std::fabs(std::cos(th)), s = std::fabs(std::sin(th));
        const double det = c * c - s * s;      // cos 2t
        if (std::fabs(det) < 0.1) return;      // near 45 deg: not invertible
        const double W = ((double)o.hw * c - (double)o.hh * s) / det;
        const double H = ((double)o.hh * c - (double)o.hw * s) / det;
        if (W <= 0.5 || H <= 0.5) return;
        o.oriented = 1;
        o.ohw = W; o.ohh = H;
        o.rc = std::cos(-th); o.rs = std::sin(-th);
    }
    void turn(int f) {
        if ((f & 3) == 0) return;
        auto rotVec = [&](float& dx, float& dy) {
            double u, v;
            toFrame(f, (double)dx, (double)dy, u, v);
            dx = (float)u; dy = (float)v;
        };
        for (size_t i = 0; i < objs.size(); ++i) {
            for (DynSample& s : samples[i]) {
                double u, v;
                toFrame(f, (double)s.cx, (double)s.cy, u, v);
                s.cx = (float)u; s.cy = (float)v;
                if (f & 1) std::swap(s.hw, s.hh);
            }
            turnObj(objs[i], f);
            baseCy[i] = objs[i].cy;
            baseSy0[i] = (float)objs[i].sy0;
            baseSy1[i] = (float)objs[i].sy1;
            rotVec(trigDx[i], trigDy[i]);
            rotVec(autoDx[i], autoDy[i]);
            for (AutoPart& p : autoParts[i]) rotVec(p.dx, p.dy);
            for (AutoPart& p : touchParts[i]) {
                rotVec(p.dx, p.dy);
                rotVec(p.tdx, p.tdy);
                if ((f & 1) && (p.tmode == 2 || p.tmode == 3))
                    p.tmode = (uint8_t)(5 - p.tmode);
            }
        }
        std::fill(cur.begin(), cur.end(), (size_t)0);
        lastT = -1;
        ++seekEpoch;
    }

    void collect(uint8_t b, double x0, double x1,
                 std::vector<const Obj*>& out) const {
        if (g_dynDbg < 0 && byBucketN == objs.size() && b < byBucket.size()) {
            for (const uint32_t i : byBucket[b]) {
                // --pressfresh (Obj::pendOff); --holdtap: the state decides (object.hpp holdTapGone)
                if (!on[i] && !objs[i].pendOff && !(g_holdTap && objs[i].holdTapX > 0.f)) continue;
                const Obj& o = objs[i];
                if (o.cx + o.hw < x0 || o.cx - o.hw > x1) continue;
                out.push_back(&o);
            }
            return;
        }
        for (size_t i = 0; i < objs.size(); ++i) {
            const Obj& o = objs[i];
            if (g_dynDbg >= 0 && o.uid == g_dynDbg)
                std::printf("dyncollect uid=%d b=%d/%d on=%d cx=%.3f hw=%.3f "
                            "win=[%.1f,%.1f] -> %s\n",
                            o.uid, (int)bucket[i], (int)b, (int)on[i], o.cx,
                            o.hw, x0, x1,
                            (bucket[i] != b || !on[i])          ? "SKIP(bucket/on)"
                            : (o.cx + o.hw < x0 || o.cx - o.hw > x1)
                                ? "SKIP(window)" : "in");
            if (bucket[i] != b || (!on[i] && !o.pendOff && !(g_holdTap && o.holdTapX > 0.f))) continue;
            if (o.cx + o.hw < x0 || o.cx - o.hw > x1) continue;
            out.push_back(&o);
        }
    }
};
}  // namespace dp
