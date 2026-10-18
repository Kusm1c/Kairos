#pragma once
#include "dp/dynamics.hpp"

namespace dp {
struct Level {
    std::vector<Obj> objs;        // solids + hazards, sorted by cx
    std::vector<Obj> portals;     // mode/gravity portals, sorted by cx
    std::vector<Obj> pads;        // type 8/10, sorted by cx
    std::vector<Obj> orbs;        // type 11, sorted by cx
    std::vector<Obj> speeds;      // speed portals (see dxForSpeedId), by cx
    std::vector<Obj> slopes;      // GameObjectType 25, by cx
    std::vector<Obj> coins;
    std::vector<int> coinDyn;
    Dynamics dyn;                 // objects with a recorded timeline
    std::vector<double> topAhead;
    double bucketX0 = 0.0;
    double maxX = 0;
    double maxY = 0;
    std::string unsupported;
    std::vector<GpHandoff> gpHandoff;
};

inline std::array<std::unique_ptr<Level>, 4> g_frameLv;
inline Level* g_baseLv = nullptr;
inline Level* levelOfFrame(int f) {
    f &= 3;
    return f == 0 ? g_baseLv : g_frameLv[(size_t)f].get();
}

inline Level& frameLevel(Level& L0, int f) {
    f &= 3;
    if (f == 0) return L0;
    auto& slot = g_frameLv[(size_t)f];
    if (!slot) {
        auto p = std::make_unique<Level>();
        auto turnVec = [&](const std::vector<Obj>& src, std::vector<Obj>& dst) {
            dst = src;
            for (Obj& o : dst) turnObj(o, f);
            std::sort(dst.begin(), dst.end(),
                      [](const Obj& a, const Obj& b) { return a.cx < b.cx; });
        };
        turnVec(L0.objs, p->objs);
        turnVec(L0.portals, p->portals);
        turnVec(L0.pads, p->pads);
        turnVec(L0.orbs, p->orbs);
        turnVec(L0.speeds, p->speeds);
        turnVec(L0.slopes, p->slopes);
        p->dyn = L0.dyn;
        p->dyn.turn(f);
        double mx = -1e18, my = -1e18;
        for (const Obj& o : p->objs) {
            mx = std::max(mx, o.cx + o.hw);
            my = std::max(my, o.cy + o.hh);
        }
        p->maxX = mx; p->maxY = my;
        {   // suffix max in this frame (built the same way as frame 0's g_topAhead)
            double lo = 1e18, hi = -1e18;
            for (const Obj& o : p->objs) {
                lo = std::min(lo, o.cx - o.hw);
                hi = std::max(hi, o.cx + o.hw);
            }
            if (lo < hi) {
                p->bucketX0 = lo;
                const size_t n = (size_t)((hi - lo) / 30.0) + 2;
                p->topAhead.assign(n, -1e18);
                for (const Obj& o : p->objs) {
                    long long i = (long long)((o.cx - lo) / 30.0);
                    if (i < 0) i = 0;
                    if (i >= (long long)n) i = (long long)n - 1;
                    p->topAhead[(size_t)i] =
                        std::max(p->topAhead[(size_t)i], o.cy + o.hh);
                }
                for (size_t i = n - 1; i-- > 0;)
                    p->topAhead[i] = std::max(p->topAhead[i], p->topAhead[i + 1]);
            }
        }
        std::printf("rotation: built frame %d (%zu colliders, %zu portals, "
                    "%zu moving)\n",
                    f, p->objs.size(), p->portals.size(), p->dyn.size());
        slot = std::move(p);
    }
    return *slot;
}

constexpr double kPadReach = 0.0;
constexpr double kPadYellow = 16.0;
constexpr double kPadPink = 10.4;
constexpr double kPadRed = 20.0;
constexpr double kPadYellowBall = 9.6;
constexpr double kPadPinkBall = 6.72;
constexpr double kOrbYellow = 11.18;  // yellow orb (id 36): same as a cube jump
constexpr double kOrbPink = 8.05;
constexpr double kOrbPinkBall = 6.0263;
constexpr double kOrbYellowBall = 7.826;
constexpr double kOrbGravity = 4.472;
constexpr double kOrbGravityBall = 3.130;
constexpr double kOrbRadius = 19.5;
constexpr double kOrbRadiusMini = 17.8;
constexpr double kRingRedCube = 1.38;
constexpr double kRingRedShip = 1.00;      // mini ship: 1.40
constexpr double kRingRedShipMini = 1.40;
constexpr double kRingRedUfo = 1.02;       // mini UFO: 1.36
constexpr double kRingRedUfoMini = 1.36;
constexpr double kRingRedBall = 1.34;
constexpr double kRingRedRobot = 1.28;
constexpr double kRingRedSpider = 1.34;
constexpr double kRingGreen = 1.00;
constexpr double kRingGreenShip = 0.70;
constexpr double kRingRobotDefault = 0.9;
constexpr double kRingDrop = 15.0;
constexpr double kRingDropSpider = 16.5;
constexpr double kRingDropFly = 14.0;
constexpr double kRingDropUfo = 11.2;
// The three post-multipliers applied after the velocity is set.
constexpr double kRingBallPost = 0.700;
constexpr double kRingSpiderPost = 0.700;
constexpr double kRingSwingPost = 0.600;
constexpr double kPadBlueVy = 6.4;
constexpr double kPadBlueBall = 3.84;
constexpr double kGravPortalScale = 0.5;
}  // namespace dp
