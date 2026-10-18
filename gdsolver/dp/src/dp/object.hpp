#pragma once
#include "dp/bands.hpp"

namespace dp {
constexpr int kEnvTwinUidBase = 100000000;

struct Obj {
    double cx, cy, hw, hh;
    uint8_t type;  // 0 solid, 2/47 hazard, 4 gravity portal, 5 ship, 6 cube,
                   // 8 yellow pad, 10 blue pad
    int id = 0;    // object id (10/11 = normal/reverse gravity portal)
    int uid = -1;
    double radius = 0.0;
    uint8_t oneway = 0;
    int8_t gpBit = -1;
    uint8_t dynObj = 0;
    uint8_t extCol = 0;
    uint8_t slope = 0;
    uint8_t slopeHazard = 0;   // m_slopeIsHazard (spiked slope)
    uint8_t slopeDir = 0;
    double sy0 = 0.0, sy1 = 0.0;
    double rot = 0.0;
    uint8_t oriented = 0;      // 1 = use the turned box, not the bound
    double ohw = 0, ohh = 0;   // half sizes of the real (unturned) box
    double rc = 1, rs = 0;     // cos/sin of rot
    uint8_t obbOk = 0;
    double bhw = 0, bhh = 0;   // half sizes along the box's own axes
    double bc = 1, bs = 0;     // its long axis as a unit vector (bc, bs)
    double dcy = 0.0;
    double tpY = 0.0;
    uint8_t tpGrav = 0;
    uint8_t tpKeep = 0;
    // The velocity a teleport portal sets after the move (--tppush; objrects tpfm..tprmax).
    TpForce tpForce;
    double tpEx = 0.0, tpEy = 0.0;
    uint8_t flipY = 0;
    uint8_t flipX = 0;
    uint8_t freeMode = 0;
    uint8_t rev = 0;
    uint8_t multiAct = 0;
    uint8_t spentRun = 0;
    int16_t pendBox = -1;
    uint8_t pendOff = 0;
    float holdTapX = 0.f;
    uint8_t recOn = 1;
};

inline bool objFacingDown(const Obj& o) {
    const int r = (int)o.rot;
    const bool f = (o.flipY != 0);
    if (r % 90 == 0) return (std::abs(r) == 180) ? !f : f;
    const bool in = (r >= 91 && r <= 269) || (r >= -269 && r <= -91);
    return f ? !in : in;
}

inline bool obbSat(double cx1, double cy1, double hw1, double hh1, double a1,
                   double cx2, double cy2, double hw2, double hh2, double a2) {
    const double dx = cx2 - cx1, dy = cy2 - cy1;
    const double c1 = std::cos(a1), s1 = std::sin(a1);
    const double c2 = std::cos(a2), s2 = std::sin(a2);
    const double ax[4] = { c1, -s1, c2, -s2 };
    const double ay[4] = { s1,  c1, s2,  c2 };
    for (int i = 0; i < 4; ++i) {
        const double ux = ax[i], uy = ay[i];
        const double r1 = hw1 * std::fabs(c1 * ux + s1 * uy)
                        + hh1 * std::fabs(-s1 * ux + c1 * uy);
        const double r2 = hw2 * std::fabs(c2 * ux + s2 * uy)
                        + hh2 * std::fabs(-s2 * ux + c2 * uy);
        if (std::fabs(dx * ux + dy * uy) > r1 + r2) return false;
    }
    return true;
}
inline bool orientedHit(const Obj& o, double px, double py, double half,
                        double pRot = 0.0) {
    if (!o.oriented)
        return std::fabs(px - o.cx) <= o.hw + half
            && std::fabs(py - o.cy) < o.hh + half;
    if (pRot != 0.0) {
        const double thO = std::atan2(-o.rs, o.rc);  // rc/rs hold cos/sin(-th)
        return obbSat(o.cx, o.cy, o.ohw, o.ohh, -thO,
                      px, py, half, half,
                      -pRot * 3.14159265358979 / 180.0);
    }
    const double dx = px - o.cx, dy = py - o.cy;
    // the player's box projected onto the object's two axes
    const double r = half * (std::fabs(o.rc) + std::fabs(o.rs));
    const double lx = dx * o.rc + dy * o.rs;
    const double ly = -dx * o.rs + dy * o.rc;
    if (std::fabs(lx) > o.ohw + r || std::fabs(ly) > o.ohh + r) return false;
    // ...and the object's box projected onto the world axes
    const double ex = o.ohw * std::fabs(o.rc) + o.ohh * std::fabs(o.rs);
    const double ey = o.ohw * std::fabs(o.rs) + o.ohh * std::fabs(o.rc);
    return std::fabs(dx) <= ex + half && std::fabs(dy) < ey + half;
}

constexpr double kSawMargin = 0.05;
inline bool g_sawExactWave = true;
inline bool g_sawExactUfo = true;
inline bool g_sawExactCube = true;
inline double sawMarginFor(uint8_t mode) {
    if (mode == 5) return 0.10;
    if (mode == 2 || mode == 1) return 0.0;
    if (mode == 4 && g_sawExactWave) return 0.0;
    if (mode == 3 && g_sawExactUfo) return 0.0;
    if (mode == 0 && g_sawExactCube) return 0.0;
    return kSawMargin;
}

inline bool obbOverlap(const Obj& o, double px, double py, double ph,
                       double pdeg) {
    const double th = -pdeg * (1.0 / kRadToDeg);   // cocos rotation is CW +
    const double pc = std::cos(th), ps = std::sin(th);
    const double dx = px - o.cx, dy = py - o.cy;
    // the object's axes
    const double lx = dx * o.bc + dy * o.bs;
    const double ly = -dx * o.bs + dy * o.bc;
    const double rOnObj = ph * (std::fabs(pc * o.bc + ps * o.bs)
                              + std::fabs(-ps * o.bc + pc * o.bs));
    if (std::fabs(lx) >= o.bhw + rOnObj || std::fabs(ly) >= o.bhh + rOnObj)
        return false;
    // the player's axes
    const double mx = dx * pc + dy * ps;
    const double my = -dx * ps + dy * pc;
    const double rOnPlrX = o.bhw * std::fabs(o.bc * pc + o.bs * ps)
                         + o.bhh * std::fabs(-o.bs * pc + o.bc * ps);
    const double rOnPlrY = o.bhw * std::fabs(-o.bc * ps + o.bs * pc)
                         + o.bhh * std::fabs(o.bs * ps + o.bc * pc);
    return std::fabs(mx) < ph + rOnPlrX && std::fabs(my) < ph + rOnPlrY;
}

inline double sawRectHalfB(int mode, int mini) {
    if (mode < 0) return -1.0;
    const double s = mini ? 0.6 : 1.0;
    if (mode == 4) return 5.0 * s;   // wave
    if (mode == 6) return 13.5 * s;  // spider
    return 15.0 * s;                 // cube/ship/ball/ufo/robot/swing
}

inline bool outOfSweepReach(const Obj* o, double x0, double y0, double x1,
                            double y1, double half) {
    double ex = std::max(o->hw, o->radius), ey = std::max(o->hh, o->radius);
    if (o->obbOk) {
        ex = std::max(ex, o->bhw * std::fabs(o->bc) + o->bhh * std::fabs(o->bs));
        ey = std::max(ey, o->bhw * std::fabs(o->bs) + o->bhh * std::fabs(o->bc));
    }
    const double reach = std::max(half, 15.0) + 2.0;
    return o->cx + ex + reach < std::min(x0, x1) || o->cx - ex - reach > std::max(x0, x1)
        || o->cy + ey + reach < std::min(y0, y1) || o->cy - ey - reach > std::max(y0, y1);
}

inline bool g_holdTap = true;
inline thread_local int8_t t_holdHeld = -1;   // the stepped state's button on the previous tick; -1: none
inline thread_local double t_holdX = 0.0;     // ...and its x at the start of the tick
inline bool holdTapGone(const Obj* o) {
    if (o->holdTapX <= 0.f || t_holdHeld < 0) return false;
    if (t_holdX > (double)o->holdTapX) return t_holdHeld != 0;
    return o->recOn == 0;   // before the trigger is crossed, as recorded
}
inline std::vector<std::pair<double, double>> g_holdTapSpans;
inline bool nearHoldTap(double x) {
    if (!g_holdTap) return false;
    for (const auto& sp : g_holdTapSpans)
        if (x >= sp.first - 30.0 && x <= sp.second + 15.0) return true;
    return false;
}

inline bool hazardHit(const Obj* o, double px, double py, double half,
                      double margin, double sawAdd = kSawMargin,
                      int mode = -1, int mini = 0) {
    if (g_holdTap && holdTapGone(o)) return false;
    if (o->radius > 0.0) {
        const double dx = px - o->cx, dy = py - o->cy;
        const double hRect = sawRectHalfB(mode, mini);   // -1 when unmeasured
        if (hRect >= 0.0 && !g_fixRadiusCollision) {
            const double r = o->radius + margin + sawMarginFor((uint8_t)mode);
            const double ex = std::fabs(dx) - hRect, ey = std::fabs(dy) - hRect;
            if (ex < 0.0 && ey < 0.0) return true;      // centre inside rect
            return ex * ex + ey * ey < r * r;           // nearest corner
        }
        const double hA = hRect >= 0.0 ? hRect : half;
        const double r = hA + o->radius + margin
                       + (hRect >= 0.0 ? kSawMargin : sawAdd);
        return dx * dx + dy * dy < r * r;
    }
    if (o->obbOk) {
        const double dx = px - o->cx, dy = py - o->cy;
        const double h = half + margin;
        const double r = h * (std::fabs(o->bc) + std::fabs(o->bs));
        const double lx = dx * o->bc + dy * o->bs;
        const double ly = -dx * o->bs + dy * o->bc;
        if (std::fabs(lx) >= o->bhw + r || std::fabs(ly) >= o->bhh + r)
            return false;
        // ...and the box projected onto the world axes
        const double ex = o->bhw * std::fabs(o->bc) + o->bhh * std::fabs(o->bs);
        const double ey = o->bhw * std::fabs(o->bs) + o->bhh * std::fabs(o->bc);
        return std::fabs(dx) < ex + h && std::fabs(dy) < ey + h;
    }
    return std::fabs(px - o->cx) <= o->hw + half + margin
        && std::fabs(py - o->cy) <= o->hh + half + margin;
}
inline bool hazardStage1(const Obj* o, double px, double py, double half, double margin, double sawAdd,
                         int mode, int mini, bool boxFollows) {
    if (g_hazRectGate && boxFollows && o->obbOk && o->radius <= 0.0)
        return std::fabs(px - o->cx) <= o->hw + half + margin
            && std::fabs(py - o->cy) <= o->hh + half + margin;
    return hazardHit(o, px, py, half, margin, sawAdd, mode, mini);
}
}  // namespace dp
