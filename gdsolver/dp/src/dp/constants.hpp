#pragma once
#include "dp/modifiers.hpp"

namespace dp {
constexpr double kFloorY = 105.0;   // cube rest height on the ground line
constexpr double kGroundY = kFloorY - 15.0;   // 90.0
constexpr double kCubeHalf = 15.0;  // outer box (landing)
constexpr double kMiniHalf = 9.0;
constexpr double kMiniScale = 0.6;
constexpr double kCubeJumpMini = 8.944;
constexpr double kMiniImpulse = 0.800;
constexpr double kCubeInner = 4.5;  // side/ceiling kill box
constexpr double kFlyThinTol = 6.0;
constexpr double kRobotGScale = 0.9;
constexpr double kRobotJumpScale = 0.5;
constexpr int kRobotHoverTicks = 67;
inline bool g_inRecon = false;      // set after "reconstructing plan"
inline long long g_dieRecon = 0;    // DIEs while g_inRecon
inline std::vector<std::pair<const char*, long long>> g_dieReconWhy;
inline long long g_resimDead = 0;
inline bool g_vetoPhys = false;
inline bool g_resimLadder = true;
constexpr int kResimLadderMax = 4;                        // searches per call
constexpr long long kResimLadderRungs[] = {6, 24, 96, 240};   // ticks before the walk's death
inline bool g_resimLadderPartial = false;
inline bool g_resimLadderHold = false;
inline constexpr int kNoCollideId = 1910;
inline bool g_verdictInfo = false;
inline float g_resimPX = 0.f;   // the player's world x at the witness's first death
inline const char* g_resimWhy = nullptr;
inline long long g_trigDbgT = -1;
inline long long g_coinDbgT = -1;
inline unsigned char g_coinSaid[8] = {0, 0, 0, 0, 0, 0, 0, 0};
// ...and one per pickup, for the same reason (g_collect is capped at 16).
inline unsigned char g_itemSaid[16] = {};
inline double g_coinNearDy[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
inline double g_coinNearX[8] = {};
inline double g_coinNearY[8] = {};
inline long long g_coinNearT[8] = {};
inline long long g_coinSeen[8] = {};
inline double g_coinProbeMaxX = -1e18;
inline long long g_gfireGroups = 0;   // groups placed
inline long long g_gfireSpread = 0;   // ...of which the members disagree on trigT
inline long long g_gfireSum = 0;      // sum of (max-min) over those
inline int g_gfireMax = 0;            // widest spread seen, in ticks
inline int g_resimUid = -1;
inline float g_resimObjX = 0.f, g_resimObjY = 0.f;
inline TouchMask g_resimTrig{};  // the walk's own trigger mask at that tick
inline int g_resimFrame = -1;
inline long long g_resimTicks = 0;
inline int g_resimFirst = -1;
inline int g_resimLast = -1;
inline void noteReconDeath(const char* why) {
    ++g_dieRecon;
    for (auto& e : g_dieReconWhy)
        if (e.first == why || (e.first && why && !std::strcmp(e.first, why))) {
            ++e.second;
            return;
        }
    g_dieReconWhy.emplace_back(why, 1LL);
}
inline int g_hazDbgUid = -1;        // --hazdbg
inline bool g_waveObbAabb = true;
inline bool g_waveHazEndpoint = true;
inline bool g_waveOneway = true;
inline bool g_tpLatchExact = false;
inline bool g_tpSeen = true;
inline bool g_dynGpLatch = true;
inline bool g_tpSpawn = true;
inline bool g_padTouch = true;
inline bool g_padRefill = true;
inline bool g_spiderOrbFace = true;
inline bool g_dashGround = true;
inline bool g_spiderPad = true;
inline bool g_gravToggle = true;
inline bool isGravPortalType(int type) {
    return type == 3 || type == 4 || (g_gravToggle && type == 42);
}
inline bool g_teleportTrig = true;
inline bool g_tpSideways = true;
inline bool g_tpPortalX = true;
inline bool g_wideScan = true;
constexpr double kWideScanHw = 40.0;
inline bool g_p2xOff = true;
// --startp2dx X: the anchor's dx2 (GD's p2x - x), read only under --p2xoff and only for a dual anchor.
inline double g_startP2dx = 0.0;
inline bool g_p2Solo = true;
inline bool g_dualEndRot = true;
inline bool g_p1SoloEnd = true;
inline bool g_gravTrig = true;
inline bool g_followYOn = true;
inline bool g_teleportOrb = true;
inline bool g_tpPush = true;
inline bool g_tpForceCol = false;
inline bool g_tpKeepY = true;
inline bool g_tpOrder = true;
inline bool g_boxParts = true;
// --moverextend (default on since 2026-10): a mover whose last three rows are consecutive ticks of one constant velocity and end at the recording's own last tick (the attempt's death) goes on at that velocity past it (dynamics.hpp prepareExtend / seek). --rotextend (rotation, load time) and --slideparts (eased autonomous moves) are the other two tails; this one reads the rows only.
inline bool g_moverExtend = true;
// --moverextendvel (default off): the FALLBACK hypothesis of the same tail, terminal velocity with no cap and no Move data behind it. --moverextend is the Move-data form: it needs one of the object's own linear Moves with exactly that step and goes on only for the Move's remaining duration.
inline bool g_moverExtendVel = false;
inline bool g_recEndExtend = true;
inline constexpr int kRecEndExtendTicks = 2;
inline bool g_recEndResid = true;
inline bool g_flipCollide = true;
inline bool g_mpushFlip = true;
inline bool g_swingBandCeil = true;
inline bool g_slopeStick = true;
inline constexpr int kSlopeStickRideTicks = 24;
inline bool g_hazRectGate = true;
inline bool g_ctlAutoTail = true;
inline bool g_headBandFresh = true;
inline bool g_fixGravThr = true;
inline bool g_expInZero = true;
inline bool g_touchOwnOff = true;
inline bool g_tapDisarm = true;
inline bool g_boxArm = true;
inline bool g_nanSlope = true;
inline int g_nanSlopeDropped = 0;   // how many the loader left out (printed after the load)
inline bool g_flipBonk = true;
inline bool g_multiOrb = true;
inline bool g_spiderBand = true;
inline bool g_spiderCoyote = true;
inline bool g_spiderSpend = true;
inline bool g_spiderPressLate = true;
inline bool g_spiderPadFirst = true;
inline bool g_spiderBandBack = true;
inline bool g_spiderSlopeLine = true;
inline bool g_spiderBandGround = true;
inline bool g_noColOneWay = true;
inline bool g_slamDedup = true;
inline bool g_slamBox = true;
inline bool g_holdDedup = true;
inline bool g_spiderInner = true;
constexpr double kSpiderInner = 4.05;
inline bool g_rotBoost = true;
inline bool g_rotSpeed = true;
inline bool g_bonkOuter = true;
inline bool g_spiderCrushGrace = true;
inline thread_local bool g_inPressProbe = false;
inline thread_local bool g_probePadFired = false;   // --spiderpadfirst: a pad fired inside the probe
inline bool g_rideHold = true;
inline bool g_stickFallback = true;
inline bool g_rideBound = true;
inline bool g_graceEndOnly = true;
inline bool g_ballScaleAir = true;
inline bool g_slopeUnchanged = true;
inline bool g_thinLandJump = true;
inline bool g_apexReacq = true;
inline bool g_followSection = true;
inline bool g_bigSection = true;
inline bool g_crushHead = true;
inline bool g_dashHead = true;
inline bool g_portOrder = true;
inline bool g_slideMini = true;
inline bool g_apexStick = true;
inline bool g_ceilStrict = true;
inline int g_halfNow = 0;

constexpr double kUfoRampFlap = 8.0;

// (--no-padobb, the raw player rotation for a rotated pad, is gone since the flag clean-up.)

inline bool g_noSatRotRaw = false;      // --no-satrotraw

inline bool g_noPadPlayerRot = false;   // --no-padplayerrot

inline bool padPlayerRotMode(uint8_t mode) {
    return mode == 0 || mode == 1 || mode == 4;   // cube / ship / wave
}

inline bool sameModeFlags(uint8_t a, uint8_t b) {
    static const uint8_t kCmp[6] = {1, 2, 3, 5, 6, 7};  // ship ball ufo robot spider swing
    for (int i = 0; i < 6; ++i)
        if ((a == kCmp[i]) != (b == kCmp[i])) return false;
    return true;
}
inline bool g_ringTapLink = true;
inline bool g_rtlP2Ring = true;
constexpr double kSlopeLandV = 5.0;
constexpr double kSlopeJumpBonus = 0.25;
constexpr double kSlopeJumpBonusCap = 0.4;
constexpr double kSpiderGScale = 0.6;
constexpr double kSpiderHalf = 13.5;
constexpr double kSpiderHalfMini = 8.1;   // 13.5 * 0.6, UNVERIFIED
constexpr double kSpiderSearchHalfX = 14.5;
constexpr double kSpiderSearchHalfXMini = 8.7;   // 14.5 * 0.6, UNVERIFIED
constexpr double kWaveClamp = 10.0;
constexpr double kWaveClampMini = 6.0;
constexpr int kFlipGraceTicks = 25;
// Ages (State::spiderJumpT) for which a spider is spared a solid's side kill: now - jump < 0.04 s.
constexpr int kSpiderJumpGraceTicks = 10;
inline bool g_spiderGraceAge = true;
inline int spiderJumpGraceTicks() {
    return g_spiderGraceAge ? kSpiderJumpGraceTicks + 1 : kSpiderJumpGraceTicks;
}
constexpr int kArmTicks = 2;
constexpr double kWaveHalf = 5.0;
constexpr double kWaveHalfMini = 2.0;
constexpr double kWaveHazHalfMini = 3.0;
constexpr double kWaveContactHalfMini = 3.0;
constexpr double kMiniContactHalf = 7.5;
constexpr double kWaveKillHalf = 1.5;
constexpr double kWaveRotK = 0.0625;
constexpr double kWaveRotKMini = 0.10;
constexpr double kRadToDeg = 57.29577951308232;
constexpr double kWaveSlopeDxR = 3.072;
constexpr double kWaveSlopeDxL = 4.068;
constexpr double kWaveSlopeDy = 4.0;
constexpr double kWaveSlopeMiniScale = 0.700;
constexpr double kWaveSlopeDxRMini = kWaveSlopeDxR * kWaveSlopeMiniScale;
constexpr double kWaveSlopeDxLMini = kWaveSlopeDxL * kWaveSlopeMiniScale;
constexpr double kWaveSlopeDyMini = kWaveSlopeDy * kWaveSlopeMiniScale;
constexpr double kShipHalf = 15.0;  // ship outer box (any contact excludes)
constexpr double kHazHalf = 15.0;
constexpr int kSubSteps = 8;
constexpr double kHazMargin = 0.0;
inline bool g_hazAfterPortal = true;
inline bool g_hazPortalOnce = true;
inline bool g_padAfterPortal = true;
inline bool g_portReRead = true;
inline bool g_wavePortalFree = true;
inline bool g_movingWallSeat = true;
inline bool g_movingWallHead = true;
inline bool g_movingWallRise = true;
inline bool g_dualBornPortal = true;
inline bool g_moveLockX = true;
inline bool objLocksX(int id) {
    switch (id) {
    case 22: case 23: case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
    case 32: case 33: case 34: case 55: case 56: case 57: case 58: case 59: case 105:
    case 200: case 201: case 202: case 203: case 744: case 899: case 900: case 901: case 915:
    case 1006: case 1007: case 1049: case 1268: case 1334: case 1346: case 1347: case 1616:
    case 1812: case 1814: case 1815: case 1912: case 1913: case 1914: case 1915: case 1916:
    case 1917: case 1934: case 1935: case 2015: case 2062: case 2067: case 2068: case 2900:
    case 2901: case 2925: case 2999: case 3023: case 3024: case 3029: case 3030: case 3031:
    case 3602: case 3603: case 3604: case 3605: case 3606: case 3607: case 3608: case 3609:
    case 3612: case 3613: case 3640: case 3660: case 3661: case 3662:
        return true;
    default:
        return id >= 3006 && id <= 3021;
    }
}
inline double kLandTol = 10.0;
inline bool g_cubeMpush = true;
// --cubempushnoground (default on since 2026-10): cubempush does not set onGround on the push tick (step.hpp).
inline bool g_cubeMpushNoGround = true;
inline bool g_rampSlabLift = true;
inline bool g_headPushHold = true;
inline bool g_mPushUpright = true;
inline bool g_armPost = true;
inline bool g_fgPost = true;
inline bool g_rampEndClick = true;
inline bool g_exitJump = true;
inline bool g_seatSpeed = true;
inline bool g_dropGround = true;
inline bool g_seatC6 = true;
inline bool g_rideC6 = true;
inline bool g_orbSwing = true;
inline bool g_flyClampReach = true;
inline double kStickGap = 4.034;
inline double kRotPerpWin = 150.0;
inline double kCrushHalf = 4.5;
inline double kStepDepth = 3.15;
constexpr double kShipLandTol = 6.0;
constexpr double kSolidResolveX = 1.75;
inline bool g_coinRoute = false;
inline bool g_worldGates = false;
// Are the item gates part of the world this call? (--coins or --worldgates)
inline bool gatesModelled() { return g_coinRoute || g_worldGates; }
inline double kCoinMargin = 0.0;
inline std::atomic<int> g_countSaid[kTouchBits] = {};
// --coinmask: the collected set an anchored (--start) search begins with.
inline int g_coinMaskSeed = 0;
inline int g_coinSkip = 0;
inline std::vector<std::pair<int, int>> g_itemBase;
inline double g_shipCeil = 375.0;
constexpr double kFlyBandBelow = 165.0;
constexpr double kFlyBandAbove = 135.0;
inline double g_flyFloor = 0.0;
inline double g_ufoCeil = 0.0;
}  // namespace dp
