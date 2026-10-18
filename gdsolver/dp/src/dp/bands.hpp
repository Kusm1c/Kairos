#pragma once
#include "dp/progress.hpp"   // which includes constants.hpp -- the chain's next link

namespace dp {
constexpr double kBandFly = 300.0;    // ship / UFO / wave / swing
constexpr double kBandBall = 240.0;
constexpr double kBandOther = 270.0;  // spider updates with this; cube/robot do not
// 0 = this portal does not touch the band
inline double bandHeightFor(int portalType) {
    switch (portalType) {
        case 5: case 19: case 26: case 41: return kBandFly;
        case 16: return kBandBall;
        case 33: return kBandOther;       // spider
        default: return 0.0;
    }
}
inline double bandHeightDual(int mode) {
    return (mode == 1 || mode == 3 || mode == 4) ? kBandFly : kBandOther;
}
enum class BandKind : uint8_t {
    Interval,       // fl < ce: a real band
    Degenerate,     // fl == ce: the band collapsed (NOT "no band")
    NoBand,         // the writer knows there is none here
    Unknown,        // the writer could not read it (a missing or zero field)
};
inline BandKind g_startBandKind = BandKind::Unknown;
inline double g_startBandFloor = 0.0, g_startBandCeil = 0.0;
inline bool startBandUsable() {
    return g_startBandKind == BandKind::Interval;
}
struct BandTrackRow { int t; BandKind kind; float fl, ce; int8_t src = -1; };
inline std::vector<BandTrackRow> g_bandTrack;
inline std::vector<size_t> g_bandTrackIntervals;
struct BandSrcRow { int t; int8_t src; };
inline std::vector<BandSrcRow> g_bandSrcRows;
inline long long g_bandTrackEnd = -1;
inline bool g_bandHoldEnd = true;
inline thread_local bool g_bandHoldNow = false;
constexpr uint8_t kBandPastEnd = 32;
inline bool bandTrackPastEnd(long long t) {
    return g_bandTrackEnd >= 0 && t > g_bandTrackEnd && !(g_bandHoldEnd && g_bandHoldNow);
}
inline bool bandTrackRowAt(long long t, double& fl, double& ce) {
    if (g_bandTrackIntervals.empty()) return false;
    if (bandTrackPastEnd(t)) return false;
    size_t lo = 0, hi = g_bandTrackIntervals.size();
    while (lo + 1 < hi) {
        const size_t m = (lo + hi) / 2;
        if (g_bandTrack[g_bandTrackIntervals[m]].t <= t) lo = m; else hi = m;
    }
    const BandTrackRow& r = g_bandTrack[g_bandTrackIntervals[lo]];
    if ((long long)r.t > t) return false;
    fl = (double)r.fl;
    ce = (double)r.ce;
    return true;
}
inline bool bandTrackKindAt(long long t, BandKind& kind, double& fl, double& ce,
                            long long& age, int* src = nullptr) {
    if (g_bandTrack.empty()) return false;
    if (bandTrackPastEnd(t)) return false;
    size_t lo = 0, hi = g_bandTrack.size();
    while (lo + 1 < hi) {
        const size_t m = (lo + hi) / 2;
        if (g_bandTrack[m].t <= t) lo = m; else hi = m;
    }
    if ((long long)g_bandTrack[lo].t > t) return false;
    kind = g_bandTrack[lo].kind;
    fl = (double)g_bandTrack[lo].fl;
    ce = (double)g_bandTrack[lo].ce;
    age = t - (long long)g_bandTrack[lo].t;
    if (src) {
        *src = g_bandTrack[lo].src;
        // ...unless the source changed since that row (an S row at or before t, after the row).
        size_t a = 0, b = g_bandSrcRows.size();
        while (a < b) {
            const size_t m = (a + b) / 2;
            if ((long long)g_bandSrcRows[m].t <= t) a = m + 1; else b = m;
        }
        if (a > 0 && g_bandSrcRows[a - 1].t >= g_bandTrack[lo].t) *src = g_bandSrcRows[a - 1].src;
    }
    return true;
}
inline bool g_bandSrc = true;
constexpr uint8_t kBandOwn = 16;
constexpr int kBandOwnNear = 3;   // GD's band changes on the portal's own tick
inline bool g_bandOwnTrack = false;
inline bool g_bandTrackSeen = true;
inline bool bandTrackAt(long long t, double& fl, double& ce) {
    return bandTrackRowAt(t + 1, fl, ce);
}
inline bool bandTrackFloorAt(long long t, double& fl, double& ce) {
    return bandTrackRowAt(t - 1, fl, ce);
}
inline std::atomic<int> g_bandTrackCam{-1};
inline bool bandTrackIsCamera() {
    const int cam = g_bandTrackCam.load(std::memory_order_relaxed);
    if (cam >= 0) return cam != 0;
    const size_t n = g_bandTrackIntervals.size();
    size_t off = 0;
    int verdict = 0;                     // too short to judge (n < 8); as before
    if (n >= 8) {
        for (const size_t i : g_bandTrackIntervals) {
            const double c = (double)g_bandTrack[i].ce;
            if (std::fabs(c - std::round(c / 30.0) * 30.0) >= 0.01) ++off;
        }
        verdict = (off * 2 > n) ? 1 : 0;
    }
    int unset = -1;
    if (g_bandTrackCam.compare_exchange_strong(unset, verdict, std::memory_order_relaxed)
        && n >= 8)
        std::printf("bandtrack: %zu interval rows, %zu off-grid -> %s\n",
                    n, off,
                    verdict ? "CAMERA-driven (no physical ceiling)"
                            : "portal-set (the ceiling is a wall)");
    return verdict != 0;
}
inline bool loadBandTrack(const char* path) {
    std::FILE* bf = std::fopen(path, "r");
    if (!bf) {
        std::fprintf(stderr, "bandtrack: cannot open %s\n", path);
        return false;
    }
    size_t kn[4] = {0, 0, 0, 0};
    size_t skipped = 0, nSrc = 0;
    char line[256];
    while (std::fgets(line, sizeof line, bf)) {
        BandTrackRow r{};
        char k = 0;
        float a = 0, b = 0;
        // a fifth field is the band's source (BandTrackRow::src); absent = -1
        int src = -1;
        const int nf = std::sscanf(line, "%d,%c,%f,%f,%d", &r.t, &k, &a, &b, &src);
        if (nf >= 3 && k == 'S') {
            const int s = (int)a;
            g_bandSrcRows.push_back({r.t, (int8_t)((s == 0 || s == 1) ? s : -1)});
            ++nSrc;
            continue;
        }
        if (nf >= 2
            && (k == 'I' || k == 'D' || k == 'N' || k == 'U')) {
            r.src = (int8_t)((nf >= 5 && (src == 0 || src == 1)) ? src : -1);
            r.fl = a;
            r.ce = (k == 'D') ? a : b;
            r.kind = k == 'I' ? BandKind::Interval
                   : k == 'D' ? BandKind::Degenerate
                   : k == 'N' ? BandKind::NoBand
                              : BandKind::Unknown;
            // An interval that is not one is not silently kept: the invariant is part of the type.
            if (r.kind == BandKind::Interval && !(r.ce > r.fl))
                r.kind = BandKind::Unknown;
        } else if (std::sscanf(line, "%d,%f,%f", &r.t, &r.fl, &r.ce) == 3) {
            r.kind = r.ce > r.fl  ? BandKind::Interval
                   : r.ce == r.fl ? BandKind::Degenerate
                                  : BandKind::Unknown;
            if (r.ce < r.fl)
                std::fprintf(stderr, "bandtrack: t=%d has ceil %g below floor %g\n", r.t,
                             (double)r.ce, (double)r.fl);
        } else {
            ++skipped;
            continue;
        }
        if (r.kind == BandKind::Interval)
            g_bandTrackIntervals.push_back(g_bandTrack.size());
        ++kn[(size_t)r.kind];
        g_bandTrack.push_back(r);
    }
    std::fclose(bf);
    g_bandTrackCam = -1;   // the verdict belongs to THIS track
    if (nSrc)
        std::printf("bandtrack: %zu rows (I %zu D %zu N %zu U %zu) unreadable %zu, source changes %zu (%s)\n",
                    g_bandTrack.size(), kn[0], kn[1], kn[2], kn[3], skipped, nSrc, path);
    else
        std::printf("bandtrack: %zu rows (I %zu D %zu N %zu U %zu) unreadable %zu (%s)\n",
                    g_bandTrack.size(), kn[0], kn[1], kn[2], kn[3], skipped, path);
    return true;
}
inline bool bandTrackSawBand(long long t, double fl, double ce) {
    if (g_bandTrack.empty()) return false;
    if (bandTrackIsCamera()) return true;
    size_t lo = 0, hi = g_bandTrack.size();
    while (lo < hi) {
        const size_t m = (lo + hi) / 2;
        if ((long long)g_bandTrack[m].t < t - kBandOwnNear) lo = m + 1; else hi = m;
    }
    for (size_t i = lo; i < g_bandTrack.size() && (long long)g_bandTrack[i].t <= t + kBandOwnNear;
         ++i) {
        const BandTrackRow& r = g_bandTrack[i];
        if (r.kind == BandKind::Interval && std::fabs((double)r.fl - fl) <= 1.0
            && std::fabs((double)r.ce - ce) <= 1.0)
            return true;
    }
    double f0 = 0.0, c0 = 0.0;
    return bandTrackRowAt(t, f0, c0) && std::fabs(f0 - fl) <= 1.0 && std::fabs(c0 - ce) <= 1.0;
}
inline bool g_portalStartY = true;
inline double g_bandK = 0.0;
inline bool g_slopeDbg = false;  // --slopedbg: one line per ramp acquisition
inline bool g_slopeRelDbg = false;
inline thread_local int g_threadSlot = 0;
inline constexpr int kThreadSlots = 64;
struct alignas(64) FrameRevRow { long long n[4][2]; };
inline FrameRevRow g_frameRevReach[kThreadSlots] = {};
inline long long frameRevReach(int f, int r) {
    long long sum = 0;
    for (const FrameRevRow& row : g_frameRevReach) sum += row.n[f][r];
    return sum;
}
inline long long g_fxWatchT = -1;
inline char g_fxWhy[1600] = "";
inline bool g_dcyDbg = false;
inline bool g_supDbg = false;
inline bool g_bandDbg = false;   // --banddbg: one line per wave tick near the band ceiling
inline bool g_spdDbg = false;
inline bool g_shipCeilSet = false;
inline bool g_freeModeCol = false;
constexpr uint8_t kBandFreeKnown = 4;
constexpr uint8_t kBandFree = 8;
// (--bandowntrack's bit is kBandOwn, declared with the track above.)
inline bool bandIsWall(uint8_t branch, bool legacyGridTest) {
    if (branch & kBandFreeKnown) return (branch & kBandFree) == 0;
    return legacyGridTest;
}
inline bool g_flyFree = true;
inline bool g_dualZoomBand = true;
inline bool flyBandFree(uint8_t branch) {
    return g_flyFree && (branch & kBandFreeKnown) && (branch & kBandFree);
}
inline bool g_trigGateCol = false;
struct StaticCam { double cx; uint8_t exit_; };
inline std::vector<StaticCam> g_staticCams;   // sorted by cx at load
inline bool g_staticCamCol = false;
constexpr int kBandAnimIn = 119, kBandAnimOut = 95;
constexpr double kBandRetracted = 322.0;   // winSize.height + 2
inline uint8_t bandAnimStart(bool animateIn) {
    return (uint8_t)(animateIn ? 0x80 : 0x00);
}
inline uint8_t bandAnimStep(uint8_t a) {
    const int e = a & 0x7f;
    return (uint8_t)((a & 0x80) | (e >= 127 ? 127 : e + 1));
}
constexpr int kBandAnimInDual = 96;
inline double bandProgress(uint8_t a, int inTicks = kBandAnimIn) {
    const bool in = (a & 0x80) != 0;
    const int e = a & 0x7f;
    if (e >= (in ? inTicks : kBandAnimOut)) return in ? 1.0 : 0.0;
    // ease-in-out, the exponent GD's tween carries
    const double u = (double)e / (in ? inTicks : kBandAnimOut);
    const double rate = in ? 2.0 : 1.5;
    const double t = 2.0 * u;
    const double e01 = t < 1.0 ? 0.5 * std::pow(t, rate)
                               : 1.0 - 0.5 * std::pow(2.0 - t, rate);
    return in ? e01 : 1.0 - e01;
}
struct FlyBand { double floorY = 0.0, ceilY = 1e9; };
inline FlyBand bandFor(double cy, double H) {
    FlyBand b;
    b.floorY = std::max(90.0, std::floor((cy - H * 0.5) / 30.0) * 30.0);
    b.ceilY = b.floorY + H;
    return b;
}
struct CeilBand {
    double y;
    double x0, x1;   // inclusive; -inf..+inf for a bare value
};
inline std::vector<CeilBand> g_playerCeils;
inline double playerCeilAt(double x) {
    double best = 1e9;
    for (const CeilBand& b : g_playerCeils)
        if (x >= b.x0 && x <= b.x1 && b.y < best) best = b.y;
    return best;
}
constexpr double kSupportReach = 15.0;
constexpr double kContactEps = 0.0;
}  // namespace dp
