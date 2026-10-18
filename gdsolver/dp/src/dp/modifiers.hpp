#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include "dp/speed.hpp"

namespace dp {
struct DeadBand { double x0, x1; int mode; double y0, y1; };
inline std::vector<DeadBand> g_deadBands;
constexpr double kFF3645Max = 0.270;
constexpr double kFF2069 = 0.159;
constexpr double kForceGDiv = 0.9601;
constexpr double kForceUnitShip = 0.1058;
constexpr double kForceUnitUfo = 0.1305;
struct ForceUnit { double v; };
inline ForceUnit forceUnitFor(uint8_t mode, float /*dxF*/) {
    const double g = std::fabs(cubePhysFor(1.29825f).g);
    switch (mode) {
        case 0: return {g / kForceGDiv};                  // cube
        case 1: return {kForceUnitShip};
        case 2: return {g * 0.6 / kForceGDiv};            // ball
        case 3: return {kForceUnitUfo};
        case 4: return {0.0};                             // wave: measured zero
        case 5: return {g * 0.9 / kForceGDiv};            // robot
        case 6: return {g * 0.6 / kForceGDiv};            // spider
        case 7: return {g * 0.4 / kForceGDiv};            // swing
        default: return {0.0};
    }
}
struct ForceBox {
    double cx, cy, hw, hh;
    double force;   // m_force
    int uid = -1;
    int fid = 0;    // m_forceID (property 530); 0 = none, set by --forceids
    double dirY = 1.0;
};
inline std::vector<ForceBox> g_forceBoxes;
inline std::string g_forceIdsPath;
struct ForceField {
    double cx, cy, R;
    int dir;                // -1 = push world-down
    int uid = -1;
    double force = 0.0;     // m_force (objrects' `force` column)
    double dirY = 1.0;      // the y share of its push, as --forceboxdir reads a 2069
    bool constLaw = false;  // forceblocks.txt: neither target mode nor range (--forcecircle)
    int fid = 0;            // its force ID (forceblocks.txt), one push per ID per tick
    // the recording's rows for this uid (--forcelive): tick, centre x, centre y, on
    std::vector<int> liveT;
    std::vector<float> liveX, liveY;
    std::vector<uint8_t> liveOn;
};
inline std::vector<ForceField> g_forceFields;
inline int g_forceCircles = 0;   // the fields with constLaw (applyForceIds)
// --forcecircle (speed.hpp): GD's circle contact for a 3645, behind the collision sections' gate.
inline bool forceCircleTouch(const ForceField& ff, double fx, double fy, double x, double y,
                             double pHalf) {
    auto cell = [](double v) { return v <= 0.0 ? 0 : (int)(v * 0.01); };   // gdSectionCell
    if (std::abs(cell(fx) - cell(x)) > 1) return false;
    if (g_dynamicLevelHeight && std::abs(cell(fy) - cell(y)) > 1) return false;
    const double dx = x - fx, dy = y - fy;
    if (g_fixRadiusCollision) return dx * dx + dy * dy <= (pHalf + ff.R) * (pHalf + ff.R);
    if (std::fabs(dx) <= pHalf && std::fabs(dy) <= pHalf) return true;   // its centre inside the player's rect
    for (int k = 0; k < 4; ++k) {
        const double cx = (k & 1) ? x + pHalf : x - pHalf, cy = (k & 2) ? y + pHalf : y - pHalf;
        if ((cx - fx) * (cx - fx) + (cy - fy) * (cy - fy) < ff.R * ff.R) return true;
    }
    return false;
}
inline bool forceFieldAt(const ForceField& ff, long long t, double& fx, double& fy) {
    fx = ff.cx;
    fy = ff.cy;
    if (!g_forceLive || t < 0 || ff.liveT.empty()) return true;
    auto rowAt = [&](long long tt) {
        size_t lo = 0, hi = ff.liveT.size();
        while (lo < hi) {
            const size_t m = lo + (hi - lo) / 2;
            if ((long long)ff.liveT[m] <= tt) lo = m + 1; else hi = m;
        }
        return lo;   // one past the row in force at tt (0: none yet)
    };
    const size_t lp = rowAt(t);
    if (lp > 0) {
        fx = (double)ff.liveX[lp - 1];
        fy = (double)ff.liveY[lp - 1];
    }
    const size_t lo = rowAt(t - 1);
    return lo == 0 || ff.liveOn[lo - 1] != 0;
}
inline double forceCircleSum(double x, double y, double pHalf, long long t = -1) {
    double f = 0.0;
    int seen[8];
    int ns = 0;
    for (const auto& ff : g_forceFields) {
        if (!ff.constLaw) continue;
        double fx, fy;
        if (!forceFieldAt(ff, t, fx, fy)) continue;   // switched off on that row
        if (!forceCircleTouch(ff, fx, fy, x, y, pHalf)) continue;
        if (ff.fid > 0) {
            bool dup = false;
            for (int k = 0; k < ns; ++k)
                if (seen[k] == ff.fid) { dup = true; break; }
            if (dup) continue;
            if (ns < 8) seen[ns++] = ff.fid;
        }
        f += ff.force * ff.dirY;
    }
    return f;
}
inline double forceBoxSum(double x, double y, double pHalf) {
    double f = 0.0;
    int seen[8];
    int ns = 0;
    for (const auto& fb : g_forceBoxes)
        if (std::fabs(x - fb.cx) <= fb.hw + pHalf
            && std::fabs(y - fb.cy) <= fb.hh + pHalf) {
            if (fb.fid > 0) {
                bool dup = false;
                for (int k = 0; k < ns; ++k)
                    if (seen[k] == fb.fid) { dup = true; break; }
                if (dup) continue;
                if (ns < 8) seen[ns++] = fb.fid;
            }
            f += fb.force * fb.dirY;
        }
    return f;
}
inline void applyForceIds(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::printf("forceids: cannot read %s\n", path.c_str());
        return;
    }
    std::string line;
    std::getline(in, line);
    std::vector<std::string> hdr;
    {
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) hdr.push_back(c);
    }
    int iu = -1, ifd = -1, irel = -1, irng = -1;
    for (int k = 0; k < (int)hdr.size(); ++k) {
        if (hdr[k] == "uid") iu = k;
        if (hdr[k] == "forceid") ifd = k;
        if (hdr[k] == "relative") irel = k;
        if (hdr[k] == "range") irng = k;
    }
    if (iu < 0 || ifd < 0) {
        std::printf("forceids: %s has no uid/forceid column\n", path.c_str());
        return;
    }
    int set = 0;
    while (std::getline(in, line)) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) f.push_back(c);
        if ((int)f.size() <= std::max(iu, ifd)) continue;
        const int uid = std::atoi(f[iu].c_str());
        const int fid = std::atoi(f[ifd].c_str());
        for (auto& fb : g_forceBoxes)
            if (fb.uid == uid) { fb.fid = fid; if (fid > 0) ++set; }
        // --forcecircle: a 3645 with neither column set pushes at a constant m_force
        if (irel >= 0 && irng >= 0 && (int)f.size() > std::max(irel, irng))
            for (auto& ff : g_forceFields)
                if (ff.uid == uid) {
                    ff.fid = fid;
                    ff.constLaw = std::atof(f[irel].c_str()) == 0.0 && std::atof(f[irng].c_str()) == 0.0;
                    if (g_forceCircle)
                        std::printf("forcecircle: uid %d R=%.3f m_force=%.3f dirY=%.3f %s\n", uid, ff.R, ff.force,
                                    ff.dirY, ff.constLaw ? "constant push" : "range/target - left to the field");
                }
    }
    g_forceCircles = 0;
    for (const auto& ff : g_forceFields) g_forceCircles += ff.constLaw ? 1 : 0;
    std::printf("forceids: %d box(es) carry a force ID\n", set);
}
inline double forceBoxAcc(double x, double y, double pHalf, ForceUnit unit, long long t = -1) {
    return qVy((forceBoxSum(x, y, pHalf)
                + (g_forceCircle ? forceCircleSum(x, y, pHalf, t >= 0 ? t - 1 : -1) : 0.0)) * unit.v);
}
// Is there a push to look up at all: the force boxes, and under --forcecircle the constant 3645s.
inline bool forcePushOn() { return !g_forceBoxes.empty() || (g_forceCircle && g_forceCircles > 0); }
inline double forceFieldAcc(double x, double y, double pHalf) {
    double a = 0.0;
    for (const auto& ff : g_forceFields) {
        if (g_forceCircle && ff.constLaw) continue;   // forceBoxAcc pushes it (forceCircleSum)
        const double dx = x - ff.cx, dy = y - ff.cy;
        const double rr = ff.R + pHalf;
        if (dx * dx + dy * dy > rr * rr) continue;
        double frac = (ff.dir < 0) ? (y - (ff.cy - ff.R)) / (2.0 * ff.R)
                                   : ((ff.cy + ff.R) - y) / (2.0 * ff.R);
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;
        a += (double)ff.dir * kFF3645Max * frac;
    }
    return a;
}
struct TimeWarp { double cx, mod; };
inline std::vector<TimeWarp> g_timeWarps;   // sorted by cx at load
inline double timeWarpAt(double x) {
    double m = 1.0;
    for (const auto& w : g_timeWarps) {
        if (w.cx > x) break;
        m = w.mod;
    }
    return m;
}
inline bool g_warpQueue = true;
inline bool g_warpRot = true;
inline bool g_warpFly = true;
inline bool g_warpFlyUfo = true;
inline bool g_warpSlowOnly = true;
struct TpForce {
    uint8_t fm = 0;
    float f = 0.f, ang = 0.f, rmod = 0.f, rmin = 0.f, rmax = 0.f;
};
struct TpTrig {
    double cx, tx, ty;
    uint8_t ignX, ignY, grav;
    int uid;
    TpForce force{};
};
inline std::vector<TpTrig> g_tpTrigs;   // sorted by cx at load
struct GravTrig {
    double cx, mod;
    int uid;
};
inline std::vector<GravTrig> g_gravTrigs;   // sorted by cx at load
inline double gravModAt(double x) {
    double m = 1.0;
    for (const auto& g : g_gravTrigs) {
        if (g.cx > x) break;
        m = g.mod;
    }
    return m;
}
constexpr int kFollowYSlots = 2;
struct FollowY {
    double cx;            // fires on the tick whose move crosses it
    int uid, group;
    double k, offset, maxStep;
    double durTicks;      // the command's duration in ticks (dur * 240)
    double endX;          // the x its run is over by, at the level's fastest speed
    int slot;             // which State::followF it lives in
    double y0ref;         // the reference object's load-time y: F's start
    int refUid;
    long long tAct = -1;  // the crossing tick, once a walk has seen it (model.hpp prepareGroup)
    std::vector<std::pair<int, double>> objs;
};
inline std::vector<FollowY> g_followYs;   // sorted by cx
// The trigger that holds `slot` at x: the last one in that slot at or behind x, -1 for none.
inline int followOwner(int slot, double x) {
    int o = -1;
    for (size_t i = 0; i < g_followYs.size(); ++i) {
        if (g_followYs[i].cx > x) break;
        if (g_followYs[i].slot == slot) o = (int)i;
    }
    return o;
}
struct FollowRef {
    uint8_t win;
    uint32_t idx;
    uint16_t cmd;
    double y0;
};
inline std::vector<std::pair<int, double>> g_followSecObjs;
constexpr uint16_t kFollowSecCmd = 0xFFFF;
inline std::vector<std::pair<int, double>> g_bigSecObjs;
constexpr uint16_t kBigSecCmd = 0xFFFE;
inline std::vector<std::pair<int, double>> g_bigSecDyn;
constexpr uint16_t kBigSecDynCmd = 0xFFFD;
constexpr double kBigSecHalf = 70.0;
constexpr double kBandBase = 270.0;
struct ZoomTrig { double cx, target, durTicks, rate; int ease; };
inline std::vector<ZoomTrig> g_zoomTrigs;   // sorted by cx at load
// t: the tick of the row at x, for --zoomchain (-1 = not known: the reading below)
inline double camScaleAt(double x, double dx, long long t = -1);     // defined below gdEase
inline bool g_zoomChain = true;
inline std::unique_ptr<std::atomic<long long>[]> g_zoomCrossT;   // per g_zoomTrigs entry
inline size_t g_zoomCrossN = 0;
constexpr long long kZoomCrossUnknown = -(1LL << 62);
inline void zoomCrossReset(size_t n) {
    g_zoomCrossT.reset(n ? new std::atomic<long long>[n] : nullptr);
    g_zoomCrossN = n;
    for (size_t i = 0; i < n; ++i) g_zoomCrossT[i].store(kZoomCrossUnknown, std::memory_order_relaxed);
}
struct ModRow { int t; float cx, cy; };
struct FlipHeadBox {
    double cx, cy, hw, hh;      // LOAD-TIME position
    int uid = -1;
    std::vector<ModRow> live;
    bool rides = false;
    int rideT0 = -1, rideT1 = -1;
};
inline std::vector<FlipHeadBox> g_flipHeadBoxes;
inline bool g_fgArmLive = false;
inline void flipHeadAt(const FlipHeadBox& b, int t, double& cx, double& cy) {
    cx = b.cx;
    cy = b.cy;
    if (b.live.empty()) return;
    size_t lo = 0, hi = b.live.size();
    while (lo < hi) {
        const size_t m = lo + (hi - lo) / 2;
        if (b.live[m].t <= t) lo = m + 1; else hi = m;
    }
    if (lo == 0) return;
    cx = (double)b.live[lo - 1].cx;
    cy = (double)b.live[lo - 1].cy;
}
inline bool flipHeadArms(double x, double y, double pHalf, int t) {
    for (const auto& b : g_flipHeadBoxes) {
        double bx = b.cx, by = b.cy;
        if (g_fgArmLive) flipHeadAt(b, t, bx, by);
        if (std::fabs(x - bx) <= b.hw + pHalf
            && std::fabs(y - by) <= b.hh + pHalf)
            return true;
    }
    return false;
}
template <class Box> inline int rideStage(const Box& b, int t) {
    if (!b.rides || b.rideT0 < 0) return 0;
    if (t < b.rideT0) return 0;
    return t <= b.rideT1 ? 1 : 2;
}
inline bool flipHeadArmsRide(double x, double y, double pHalf, int t, bool freshPrev) {
    for (const auto& b : g_flipHeadBoxes) {
        const int st = rideStage(b, t);
        if (st == 1) {
            if (freshPrev) return true;
            continue;
        }
        if (st == 2) continue;
        double bx = b.cx, by = b.cy;
        if (!b.rides) flipHeadAt(b, t, bx, by);   // a box that does not ride: as --fgarmlive
        if (std::fabs(x - bx) <= b.hw + pHalf
            && std::fabs(y - by) <= b.hh + pHalf)
            return true;
    }
    return false;
}
struct ArmBox {
    double cx, cy, hw, hh;      // LOAD-TIME position
    int uid = -1;
    std::vector<ModRow> live;   // the recording's rows for this uid (see FlipHeadBox)
    bool rides = false;         // --ridebox, as FlipHeadBox
    int rideT0 = -1, rideT1 = -1;
};
inline std::vector<ArmBox> g_armBoxes;
inline void armBoxAt(const ArmBox& b, int t, double& bx, double& by) {
    bx = b.cx;
    by = b.cy;
    if (b.live.empty()) return;
    size_t lo = 0, hi = b.live.size();
    while (lo < hi) {
        const size_t m = lo + (hi - lo) / 2;
        if (b.live[m].t <= t) lo = m + 1; else hi = m;
    }
    if (lo > 0) {
        bx = (double)b.live[lo - 1].cx;
        by = (double)b.live[lo - 1].cy;
    }
}
inline bool armBoxTouch(double x, double y, double pHalf, int t) {
    for (const auto& b : g_armBoxes) {
        double bx, by;
        armBoxAt(b, t, bx, by);
        if (std::fabs(x - bx) <= b.hw + pHalf
            && std::fabs(y - by) <= b.hh + pHalf)
            return true;
    }
    return false;
}
inline bool armBoxTouchRide(double x, double y, double pHalf, int t, bool freshPrev) {
    for (const auto& b : g_armBoxes) {
        const int st = rideStage(b, t);
        if (st == 1) {
            if (freshPrev) return true;
            continue;
        }
        if (st == 2) continue;
        double bx = b.cx, by = b.cy;
        if (!b.rides) armBoxAt(b, t, bx, by);
        if (std::fabs(x - bx) <= b.hw + pHalf
            && std::fabs(y - by) <= b.hh + pHalf)
            return true;
    }
    return false;
}
struct SlideBox {
    double cx, cy, hw, hh;      // LOAD-TIME position
    int uid = -1;
    std::vector<ModRow> live;   // the recording's rows for this uid (--slidelive)
};
inline std::vector<SlideBox> g_slideBoxes;
inline bool g_slideLive = true;
inline bool g_slideEnd = true;
inline bool g_waveRamp = true;
inline bool g_waveSlopeWall = true;
// --ringpresseat (speed.hpp): every toggle ring (1594) in the level, with its recorded rows.
struct PressRing {
    double cx, cy, hw, hh;      // LOAD-TIME box
    int uid = -1;
    uint8_t multiAct = 0;       // Obj::multiAct: GD never marks it used (--multiorb)
    std::vector<int> liveT;     // the recording's rows: tick, centre, on
    std::vector<float> liveX, liveY;
    std::vector<uint8_t> liveOn;
};
inline std::vector<PressRing> g_pressRings;
inline bool pressRingFired(int uid, int32_t old, const int32_t* hist) {
    if (uid < 0) return false;
    if (old == uid) return true;
    for (int k = 0; k < 3; ++k)
        if (hist[k] == uid) return true;
    return false;
}
// The ring's last recorded row at or before t, or -1.
inline long pressRingRow(const PressRing& r, long long t) {
    size_t lo = 0, hi = r.liveT.size();
    while (lo < hi) {
        const size_t m = lo + (hi - lo) / 2;
        if ((long long)r.liveT[m] <= t) lo = m + 1; else hi = m;
    }
    return (long)lo - 1;
}
inline int pressRingHolding(double x, double y, double pHalf, long long t, long long onT, int32_t old,
                            const int32_t* hist) {
    for (size_t i = 0; i < g_pressRings.size(); ++i) {
        const auto& r = g_pressRings[i];
        if (pressRingFired(r.uid, old, hist)) continue;
        double rx = r.cx, ry = r.cy;
        bool on = true;
        if (!r.liveT.empty()) {
            const long k = pressRingRow(r, t);
            if (k >= 0) {
                rx = (double)r.liveX[(size_t)k];
                ry = (double)r.liveY[(size_t)k];
            }
            const long ko = onT == t ? k : pressRingRow(r, onT);
            if (ko >= 0) on = r.liveOn[(size_t)ko] != 0;
        }
        if (on && std::fabs(x - rx) < r.hw + pHalf && std::fabs(y - ry) < r.hh + pHalf) return (int)i;
    }
    return -1;
}
inline bool pressRingOnAt(int uid, long long t) {
    if (uid < 0) return true;
    for (const auto& r : g_pressRings) {
        if (r.uid != uid) continue;
        if (r.liveT.empty()) return true;
        const long k = pressRingRow(r, t);
        return k < 0 || r.liveOn[(size_t)k] != 0;
    }
    return true;
}
inline bool slideBoxTouch(double x, double y, double pHalf, int t = -1) {
    for (const auto& b : g_slideBoxes) {
        double bx = b.cx, by = b.cy;
        if (g_slideLive && t >= 0 && !b.live.empty()) {
            size_t lo = 0, hi = b.live.size();
            while (lo < hi) {
                const size_t m = lo + (hi - lo) / 2;
                if (b.live[m].t <= t) lo = m + 1; else hi = m;
            }
            if (lo > 0) {
                bx = (double)b.live[lo - 1].cx;
                by = (double)b.live[lo - 1].cy;
            }
        }
        if (std::fabs(x - bx) <= b.hw + pHalf
            && std::fabs(y - by) <= b.hh + pHalf)
            return true;
    }
    return false;
}
struct DashStopBox { double cx, cy, hw, hh; };
inline std::vector<DashStopBox> g_dashStopBoxes;
inline bool g_ctrlTouch = true;
struct CtrlTouchBox { double cx, cy, hw, hh; int uid; uint8_t on; uint8_t holdOff = 0; };
inline std::vector<CtrlTouchBox> g_ctrlTouchBoxes;
inline bool nearCtrlHoldBox(double x) {
    if (!g_ctrlTouch) return false;
    for (const CtrlTouchBox& b : g_ctrlTouchBoxes)
        if (b.holdOff && x >= b.cx - b.hw - 30.0 && x <= b.cx + b.hw + 15.0) return true;
    return false;
}
inline bool dashStopAt(double x, double y, double pHalf) {
    for (const auto& b : g_dashStopBoxes)
        if (std::fabs(x - b.cx) <= b.hw + pHalf
            && std::fabs(y - b.cy) <= b.hh + pHalf)
            return true;
    return false;
}
}  // namespace dp
