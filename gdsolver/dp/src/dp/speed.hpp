#pragma once
#include "dp/prelude.hpp"

namespace dp {
inline double kSupportTol = 0.01;
constexpr double kDx = 1.29825;
constexpr float kDxF = 1.29825044f;
inline double g_startSpeedMul = 0.0;
inline float dxForSpeedMul(double mul) {
    if (mul < 0.8) return 251.16f / 240.0f;   // 0.7
    if (mul < 1.0) return 1.29825044f;        // 0.9  normal
    if (mul < 1.2) return 1.6142578f;         // 1.1
    if (mul < 1.45) return 468.0f / 240.0f;   // 1.3
    return 576.0f / 240.0f;                   // 1.6
}
inline double speedMulForDx(double dxF) {
    if (dxF > 2.2) return 1.6;
    if (dxF > 1.78) return 1.3;
    if (dxF > 1.45) return 1.1;
    if (dxF > 1.15) return 0.9;
    return 0.7;
}
// --seatspeed: a speed as one byte (State::seatSpd), 1..5 for 0.7 .. 1.6, 0 for none.
inline uint8_t speedCodeForDx(double dxF) {
    const double mul = speedMulForDx(std::fabs(dxF));
    return (uint8_t)(mul < 0.8 ? 1 : mul < 1.0 ? 2 : mul < 1.2 ? 3 : mul < 1.45 ? 4 : 5);
}
inline float dxForSpeedCode(uint8_t code) {
    static const double kMul[6] = {0.9, 0.7, 0.9, 1.1, 1.3, 1.6};
    return dxForSpeedMul(kMul[code < 6 ? code : 0]);
}
inline float dxForSpeedId(int id) {
    switch (id) {
        case 200: return 251.16f / 240.0f;   // 0.7  slow      UNVERIFIED
        case 201: return kDxF;               // 0.9  normal
        case 202: return 1.6142578f;         // 1.1  measured on lv15
        case 203: return 468.0f / 240.0f;    // 1.3  = 1.95, see the note above
        case 1334: return 576.0f / 240.0f;   // 1.6            UNVERIFIED
        default: return kDxF;
    }
}
inline double ballRotRate(bool mini, bool airborne, double dx) {
    const double S = mini ? 0.8 : 1.0;
    const double V = (dx > 0.0) ? ((double)kDxF / dx) : 1.0;
    const double rate = airborne ? (340.0 / (S * 0.80 * V))
                                 : (120.0 / (S * 0.20 * V));
    return rate / 240.0;
}

struct CubePhys { double jump, g; };
inline CubePhys cubePhysFor(float dxF) {
    if (dxF > 1.78f) return {11.230, -0.216};   // 1.3
    if (dxF > 1.45f) return {11.420, -0.215};   // 1.1
    if (dxF > 1.15f) return {11.180, -0.216};   // 0.9
    return {10.620, -0.212};
}
inline double ringScaleFor(float dxF) { return cubePhysFor(dxF).jump / 11.180; }
inline double ballFlipFor(float dxF) { return 3.354 * ringScaleFor(dxF); }
constexpr double kBallSlopeTapBonus = 0.2796;
constexpr double kBallSlopeExitBonus = 0.075;
inline bool isSpeedId(int id) {
    return id == 200 || id == 201 || id == 202 || id == 203 || id == 1334;
}
constexpr double kYScale = 0.225;
inline double qVy(double v) { return std::round(v * 1000.0) / 1000.0; }
constexpr double kCubeG = -0.216, kCubeJump = 11.18, kCubeTerm = -15.0;
constexpr double kBallG = -0.129;
constexpr double kBallFlip = 3.354;
constexpr double kBallTerm = -15.0;
constexpr double kSwingG = 0.086;
constexpr double kSwingGMini = 0.129;
inline double swingG(bool mini) { return mini ? kSwingGMini : kSwingG; }
constexpr double kShipRampG = 0.086;
constexpr double kSwingTerm = 8.0;
constexpr double kSwingFlipDamp = 0.8;
inline double g_dynHazPad = 0.0;

inline int g_fixRadiusCollision = 0;
inline int g_fixGravityBug = 0;
inline int g_fixNegativeScale = 0;
inline int g_fixRobotJump = 0;
inline int g_dynamicLevelHeight = 0;
inline int g_platformerMode = 0;
inline double g_maxPlayY = 1e18;
inline constexpr int kDefGoalSpare = 1;
inline int g_goalSpare = kDefGoalSpare;
inline double g_goalX = 1e18;   // the search's goal, L.maxX + 60 (cli.hpp)
inline bool g_goalGd = false;
inline double g_endPortalX = 0.0;   // levelsettings endPortalX (0 = not in the dump)
inline int g_endPortalFlipX = 0;    // levelsettings endPortalFlipX
inline bool reachesGoalUnderMaxPlayY(double x, double y, double vy, double dx, bool rev) {
    if (!g_goalSpare || g_maxPlayY >= 1e17 || g_goalX >= 1e17) return false;
    if (rev || dx <= 1e-6) return false;
    if (x >= g_goalX) return true;
    const double n = std::ceil((g_goalX - x) / dx);
    const double rise = std::max(std::max(std::fabs(vy), 15.0) * 0.25, 2.0 * dx);
    return y + n * rise <= g_maxPlayY;
}
inline int g_upsideCoyote = 1;
inline bool g_lingerLaunch = true;
inline bool g_ceilGraceCube = true;
constexpr int kRawValuesDefault = 1;
inline int g_rawValues = kRawValuesDefault;
inline double rawQ(double v) { return g_rawValues ? qVy((double)(float)v) : v; }
constexpr int kPortalUnpinDefault = 1;
inline int g_portalUnpin = kPortalUnpinDefault;
constexpr int kHazEndpointDefault = 1;
inline int g_hazEndpoint = kHazEndpointDefault;
constexpr int kPortalPressDefault = 1;
inline int g_portalPress = kPortalPressDefault;
constexpr int kPadTableDefault = 1;
inline int g_padTable = kPadTableDefault;
inline double padTableVy(int type, int mode, bool mini) {
    const bool ship = mode == 1, ufo = mode == 3, ball = mode == 2, spider = mode == 6;
    float s = 1.0f;
    if (type == 9)
        s = ship ? 0.35f : ufo ? 0.4f : (ball || spider) ? 0.7f : 0.65f;
    else if (type == 34)
        s = ship ? (mini ? 0.95f : 0.63f) : ufo ? (mini ? 0.98f : 0.6f) : 1.25f;
    double v = (double)(s * 16.0f * (mini ? 0.8f : 1.0f));
    if (ball || spider || mode == 7) v *= 0.6000000238418579;
    return v;
}
inline thread_local int g_flyRingNested = 0;
inline bool g_flyDashRing = true;
inline bool g_flySpiderRing = true;
inline bool g_flyPadHead = true;
inline bool g_earlyRing = true;
inline bool g_dashClamp = true;
inline bool g_mpushSide = true;
inline bool g_bluePadPost = true;
inline bool g_ringRotBox = true;
inline bool g_ballTapLag = true;
inline bool g_ringRotGround = true;
inline bool g_slopeLandTol = true;
inline bool g_ringPobb = true;
inline bool g_waveRingBox = true;
constexpr double kWaveRingHalf = 6.0;
inline bool g_waveDash = true;
inline bool g_waveRingMini = true;
inline bool g_waveRingMiniBox = true;
constexpr double kWaveRingHalfMini = kWaveRingHalf * 0.6;
inline thread_local int g_prePortalUid = -1;
inline thread_local int g_ballTapNested = 0;
inline thread_local int g_ballTapLaunch = 0;
inline bool g_portSeenX = true;
inline bool g_spdSameOnce = true;
inline bool g_holdSeed = true;
// The --start hold field (its 7th, held_before(plan, t0)); -1 when no --start was given.
inline int g_startHoldField = -1;
inline bool g_portSeatOrder = true;
inline bool g_flyHazDefer = true;
inline bool g_dualBornPress = true;
inline bool g_dualPadFlip = true;
inline bool g_flyHazTele = true;
inline int g_heldAll = 1;
inline int g_heldCellCap = 0;
inline int g_heldCellTwins = 0;
inline bool g_padOldBox = true;
inline bool g_sameTick = true;
inline bool sameTickTapMode(uint8_t mode) { return mode == 0 || mode == 2 || mode == 5 || mode == 6; }
inline bool g_toggleScale = true;
inline bool toggleScalesNow(uint8_t mode) {   // ship, UFO, wave, robot, spider
    return mode == 1 || mode == 3 || mode == 4 || mode == 5 || mode == 6;
}
inline bool g_forceCircle = true;
inline bool g_forceLive = true;
inline bool g_ringPressEat = true;
inline bool g_spawnRingOn = true;
inline bool g_actHazOff = true;
inline bool g_formulaNoLock = true;
inline bool g_swingSeatPress = true;
inline bool g_swingHeadCeil = true;
inline bool g_swingCeilMoved = true;
inline long long g_solidOrdDbgT0 = -1, g_solidOrdDbgT1 = -1;
inline bool g_padUndoFlip = true;
inline std::vector<int> g_spentOrb;
inline std::vector<int> g_spentOrb2;
inline bool g_spentP2 = true;
inline bool g_histStatOn = false;
inline std::string g_histStatPath;
struct HistStat {
    int touchMax = 0, touchDrop = 0, portMax = 0, portFull = 0, padMax = 0, padFull = 0;
    int orbFired = 0, orbDrop = 0, orbRefire = 0;
    int padRefire = 0;
};
inline thread_local HistStat g_hist;
inline thread_local std::vector<int> g_histFired;
inline thread_local std::vector<int> g_histPads;
inline double pinkRingRatio(int mode) {
    return mode == 2 ? 0.77 : mode == 1 ? 0.37 : mode == 3 ? 0.42 : 0.72;
}
inline double rawGravStep(bool ballLike, bool robot, double dx) {
    const double mul = speedMulForDx(dx);
    const double g = mul < 0.8 ? 0.940199 : mul < 1.0 ? 0.958199024 : mul < 1.2 ? 0.957199 : 0.961199;
    if (ballLike) return -0.958199024 * 0.6 * 0.225;
    return -(robot ? 0.9 : 1.0) * g * 0.225;
}
inline double g_offBoardMargin = 0.0;
inline int g_shiftDbgUid = -1;       // --shiftdbg <uid>
inline bool g_shiftDbgDone = false;
}  // namespace dp
