#pragma once
// Session configuration (autorun.cfg keys), data root, shared session state.
#include "mod/prelude.hpp"
#include <memory>

namespace p1 {
inline std::string g_dataDirStorage;
inline const char* DATA_DIR = "";
inline bool g_dataDirResolved = false;

inline void resolveDataDir() {
    if (g_dataDirResolved) return;
    g_dataDirResolved = true;
    g_dataDirStorage = Mod::get()->getSaveDir().lexically_normal().generic_string();
    if (auto arg = Mod::get()->getLaunchArgument("data-root"); arg && !arg->empty()) {
        std::filesystem::path requested(*arg);
        // Relative paths depend on the working directory and are dangerous, so reject them
        if (requested.is_absolute()) {
            g_dataDirStorage = requested.lexically_normal().generic_string();
        } else {
            log::error("gdsolver: ignoring non-absolute data-root '{}'", *arg);
        }
    }
    DATA_DIR = g_dataDirStorage.c_str();
    std::error_code ec;
    std::filesystem::create_directories(g_dataDirStorage, ec);
    log::info("gdsolver: data root = {}", DATA_DIR);
}

inline std::string workerTag() {
    std::string d = g_dataDirStorage;
    auto p = d.find("worker-");
    if (p == std::string::npos) return "local";
    auto e = d.find_first_of("/\\", p);
    return d.substr(p, e == std::string::npos ? std::string::npos : e - p);
}

inline void updateWindowTitle();   // the definition comes after the g_cfg / g_started declarations

inline bool g_psnapPlayerWritten = false;

struct InputCmd { int step; bool down; };
struct ToggleCmd { int step; std::string mode; };

constexpr double kGlitchPortalDefault = 15.0;
constexpr double kGlitchWaveDefault = 5.0;
constexpr double kGlitchEmbedDefault = 14.0;

struct Config {
    bool enabled = false;
    int levelId = 1;
    std::string levelFile;
    bool levelFileMain = false;
    int maxAttempts = 1;
    float delaySec = 2.f;
    bool quitWhenDone = true;
    int fps = 0;        // 0=leave unchanged. For verification C: force the frame rate
    int cbs = -1;       // -1=leave unchanged. 0/1= force m_clickBetweenSteps
    int cos = -1;       // -1=leave unchanged. 0/1= force m_clickOnSteps
    float framedt = 0;  // >0: pin the dt passed to BGL_update to this value (fake frame rate)
    bool blockInput = false; // block real input (other than injection) during replay
    bool progressBlock = true;
    float fastdt = 0;   // >0: fast mode. dt passed per update call (e.g. 1.0 = 240 ticks)
    int fastloops = 1;  // in fast mode, number of update calls per rendered frame
    double frameBudgetMs = 0.0;
    bool skipRender = false; // skip rendering of the game layer in fast mode
    bool noTrace = false;    // stop trace/dump writes (for speed-first runs)
    bool dpSelfTest = false;
    bool dpSolve = false;
    int dpHorizon = 0;
    int dpStepHorizon = 3000;
    long long dpSeed = 0;
    std::string dpSearch = "dp";
    bool dpFastVetoAll = false;
    bool dpRejoinFull = false;
    bool dpOffBoardKill = false;
    bool offBoardTp = true;
    bool offBoardFree = true;
    bool offBoardBand = true;
    bool anchorBoardArm = true;
    int dpMaxIters = 200;
    int dpTopStop = 0;
    int dpCapTiers = 1;
    int dpSecTierFirst = 1;
    int dpSecMaxBack = 0;
    bool coinMissRev = false;
    bool coinMissMove = true;    // ON BY DEFAULT with the SubZero coin set (0 = off)
    bool groupsRetime = true;    // ON BY DEFAULT with the SubZero coin set (0 = off)
    bool coinMissPost = true;    // ON BY DEFAULT with the SubZero coin set (0 = off)
    bool coinMissEarly = true;
    bool coinOverDepth = true;   // ON BY DEFAULT with the SubZero coin set (0 = off)
    bool coinApproachOff = true; // ON BY DEFAULT with the SubZero coin set (0 = off)
    bool coinApproachReach = true;
    bool routePrereq = true;
    int routePrereqAfter = 10;
    bool dpSecAuto = true;
    int dpSecStall = 0;     // cfg `dpsecstall`: rounds spent at the deepest wall (the same wall
                            // until the deepest death moves kAutoCreep ticks, repair.hpp)
    int dpSecNoRec = 0;     // cfg `dpsecnorec`: deaths there in a row the recorder wrote nothing for
    int dpSecMargin = 54;   // cfg `dpsecmargin`: how long past the death (ticks) the rung's
    int dpSecCap = 100;     // cfg `dpseccap`: the search's frontier cap
    int dpSecCapTiers = 2;  // cfg `dpseccaptiers`: a window that found nothing, again at twice
    bool dpSecState = true;
    bool dpSecChain = true;
    bool secRedoReach = true;
    bool secCpFallback = true;
    int secThrowKind = 0;    // 0 off, 1 length_error, 2 bad_alloc, 3 both
    int secThrowDepth = 0;
    int dpSecChainSpan = 30;
    int dpSecPinBack = 60;
    int dpLearnResets = 1;
    bool secDriftWhere = false;
    double dpSecStatePrice = 1.75;
    double dpSecCallPrice = 950000.0;
    bool dpSecSolved = false;
    bool dpSecRecAway = true;
    bool dpSecReuse = true;
    bool dpSecRent = true;
    int dpSecRungPrior = 15;
    double dpSecWallBudget = 0.0;
    bool dpUnseenFirst = false;
    bool dpSeedPickups = true;
    bool obbNoLatch = true;
    bool dpSecCoinRung = true;   // ON BY DEFAULT with the SubZero coin set (0 = off)
    bool dpEndTrigClear = true;
    bool slice = true;
    int sliceMin = -1;   // -1 = kSliceMinDefault
    int sliceAddBacks = 2;
    bool sliceGiveUpCopy = true;
    bool sliceCount = false;
    bool sliceNoPosition = false;
    bool sliceNoTouch = true;
    bool sliceTriggers = true;
    bool sliceUids = true;
    bool sliceKeepGroups = true;
    bool sliceFirstRef = true;
    std::string dpSeedPlan;
    int dpShow = -1;
    bool dpFixups = true;
    bool dpWorld = true;
    bool dpGroups = true;
    bool needPinRelease = true;
    std::string dpCoinWin = "60,2,700";
    double glitchPortal = 0.0;
    double glitchWave = 0.0;
    double glitchEmbed = 0.0;   // cfg `glitchembed=<px>`: dp's g_glitchEmbed
    bool glitchDeco = false;    // cfg `glitchdeco=1`: dp's --glitchdeco (hazard look-alikes)
    bool dpBandTrack = true;
    int dpRotSeed = 4;
    bool dpRotSeedAnchor = true;
    bool rotSeedPre = true;
    bool dpSnapshot = false;
    bool researchCapture = false;
    bool dpCheck = false;
    bool dpCheckObs = false;
    bool dpCheckFirst = false;
    int dpContentHorizon = 2;
    int dpCapLadder = 125;
    int dpPlainBeside = -1;
    int dpSigCheck = 0;
    int gtRollCheck = 0;
    int dpInputGrid = 2;
    bool dpPhaseProf = false;
    bool memCensus = false;
    std::vector<int> dpWatchFired;
    bool dpSpentPad = true;
    bool dpXTrack = true;
    bool dpFineRetry = true;
    bool dpTouchSeedNow = true;
    bool dpFingerprint = true;
    std::vector<std::string> dpArgs;
    bool dumpEarly = false;  // diagnostic: write tick<=600 to dump
    bool endTrace = false;
    // Record orb firing points (press tick / fire tick / offset from the orb centre)
    bool orbTrace = false;
    float orbTraceX = 0; // >0: record only orbs near this x (±300)
    bool subRingSpent = false;
    bool padTrace = false;
    bool touchPayload = false;
    bool portalPayload = true;
    bool portalPayloadLt = true;
    bool dpAnchorRotStep = true;
    bool dpAnchorRot2 = true;
    bool histRide = true;
    bool snapTrace = false;
    bool snapNodeSync = true;
    bool snapNodeLog = false;   // cfg snapnodelog=1: print a snap whose object's node is off its real position
    bool singleReset = true;
    bool resetLog = false;
    bool resetDump = false;
    std::vector<int> resetDumpUids;
    bool headBandFresh = true;
    int persistWorld = -1;
    // cfg persistitems=id:v,id:v (with persistworld=k > 0): the kept items of that world.
    std::string persistItems;
    bool voidBand = true;
    bool recProvChain = false;
    bool cpFollowSection = true;
    bool hitboxTrace = false;   // record the hitboxes GD actually uses (cfg hitboxtrace=1)
    bool killerSite = false;
    bool fieldProbe = false;
    int watchUid = 0;
    long long hbFrom = 0, hbTo = 0;   // hbTo=0 means no upper bound
    bool standTrace = false;    // cfg standtrace=1
    bool noDeath = false;       // swallow deaths (cfg nodeath=1, observation only)
    bool antiCheatPass = true;
    // Rollback verification (method B: practice-mode checkpoints)
    int practiceAt = -1;     // turn practice mode ON at this tick
    int checkpointAt = -1;   // create a checkpoint at this tick
    int restoreAt = -1;      // restore at this tick (once only)
    std::vector<int> snapAt;
    int snapVerify = 0;
    bool snapPlayer = true;
    int cpFlight = 600;
    bool cpFlightProbe = false;
    bool cpVisAfter = false;
    int cpActivation = 0;   // 1 = the flag only, 2 = through GD's transitions + the list (crashes the draw)
    int cpFlightObj = -1;
    int cpFlightProbeAt = -1;
    bool cpFlightLayerDiff = false;
    // cfg `cpflightcarrylog=1` (print only): each attempt's carry signature (`cpflight: carry` lines).
    bool cpFlightCarryLog = false;
    std::string cpFlightNodeWatch = "-";
    long long roboDbg0 = -1, roboDbg1 = -1;
    int restoreLoop = 0;
    bool restoreLoopKeep = false;
    int restoreLoopHold = 0;
    bool restoreLoopCycle = false;
    bool restoreLoopClearQ = false;
    bool restoreLoopDrain = false;
    int restoreLoopHeapMB = 0;
    int restoreLoopDrainAt = 0;
    bool oobTest = false;
    double vyTest = 0.0;
    bool vyTestOn = false;
    bool coinFiles = false;
    bool coinMode = false;   // enable our own coin-pickup detection (for coin verification
    bool coinRoute = false;
    bool worldGates = false;
    bool coinWatch = false;
    bool areaEnv = false;
    // Music handling. continuous=keep the song playing / mute=silent / normal=untouched
    std::string music = "continuous";
    long long takeoverAt = -1;
    float takeoverX = 0.f;
    double takeoverWait = 1.0;
    std::vector<InputCmd> inputs;
    std::vector<ToggleCmd> toggles; // for verification D: switch mode at the given tick (calls
                                    // the same function a portal does)
};

inline Config g_cfg;
inline bool g_started = false;
inline bool g_uiSession = false;

namespace suite {
inline std::vector<int> g_levels;   // in the order they will be solved
inline size_t g_at = 0;             // index of the one running now
inline bool g_advance = false;
inline int g_waited = 0;            // resident-poll ticks since the session ended
inline int g_settle = 0;            // ...and ticks since the scene became enterable

inline bool active() { return !g_levels.empty(); }
inline bool hasNext() { return active() && g_at + 1 < g_levels.size(); }
inline int current() { return g_at < g_levels.size() ? g_levels[g_at] : 0; }

inline std::string g_levelDir;
inline bool g_levelDirChecked = false;
}  // namespace suite
inline bool g_forceCleanStart = false;
inline bool botDriving() { return g_started; }

inline bool progressBlockActive() { return botDriving() && g_cfg.progressBlock; }

inline long long g_blockedAch = 0, g_blockedStat = 0, g_blockedCoin = 0;
inline long long g_blockedProgress = 0;

struct LevelProgress {
    int attempts = -1, jumps = -1, clicks = -1;
    int normalPercent = -1, practicePercent = -1;
    int newNormalPercent2 = -1, orbCompletion = -1;
    bool valid = false;
};

inline LevelProgress sampleProgress(GJGameLevel* lv) {
    LevelProgress p;
    if (!lv) return p;
    p.attempts = lv->m_attempts.value();
    p.jumps = lv->m_jumps.value();
    p.clicks = lv->m_clicks.value();
    p.normalPercent = lv->m_normalPercent.value();
    p.practicePercent = lv->m_practicePercent;
    p.newNormalPercent2 = lv->m_newNormalPercent2.value();
    p.orbCompletion = lv->m_orbCompletion.value();
    p.valid = true;
    return p;
}

inline std::string progressDiff(const LevelProgress& a, const LevelProgress& b) {
    if (!a.valid || !b.valid) return "unavailable";
    std::string s;
    auto one = [&](const char* name, int x, int y) {
        if (x != y) s += std::string(s.empty() ? "" : " ") + name + "="
                       + std::to_string(x) + "->" + std::to_string(y);
    };
    one("attempts", a.attempts, b.attempts);
    one("jumps", a.jumps, b.jumps);
    one("clicks", a.clicks, b.clicks);
    one("normal%", a.normalPercent, b.normalPercent);
    one("practice%", a.practicePercent, b.practicePercent);
    one("newNormal%2", a.newNormalPercent2, b.newNormalPercent2);
    one("orbs", a.orbCompletion, b.orbCompletion);
    return s.empty() ? "none" : s;
}

inline LevelProgress g_progressAtStart;
inline GJGameLevel* g_progressLevel = nullptr;
inline long long g_progressRestores = 0;

inline void restoreProgress() {
    if (!progressBlockActive()) return;
    if (!g_progressLevel || !g_progressAtStart.valid) return;
    const LevelProgress now = sampleProgress(g_progressLevel);
    if (!now.valid) return;
    const LevelProgress& w = g_progressAtStart;
    if (now.attempts == w.attempts && now.jumps == w.jumps && now.clicks == w.clicks
        && now.normalPercent == w.normalPercent && now.practicePercent == w.practicePercent
        && now.newNormalPercent2 == w.newNormalPercent2
        && now.orbCompletion == w.orbCompletion)
        return;
    ++g_progressRestores;
    g_progressLevel->m_attempts = w.attempts;
    g_progressLevel->m_jumps = w.jumps;
    g_progressLevel->m_clicks = w.clicks;
    g_progressLevel->m_normalPercent = w.normalPercent;
    g_progressLevel->m_practicePercent = w.practicePercent;
    g_progressLevel->m_newNormalPercent2 = w.newNormalPercent2;
    g_progressLevel->m_orbCompletion = w.orbCompletion;
}
inline long long g_bgBlocked = 0, g_resignBlocked = 0;
inline int g_attempt = 0;
inline long long g_attemptSerial = 0;
inline size_t g_nextInput = 0;
inline size_t g_nextInputAtStart = 0;
inline bool g_attemptFed = false;
inline long long g_frame = 0;
inline int g_traceLines = 0;
inline int g_finishedAttempts = 0;
inline bool g_sessionOver = false;
inline size_t g_nextToggle = 0;
inline bool g_injecting = false;
inline bool g_btnDown = false;
inline std::chrono::steady_clock::time_point g_attemptStart;

namespace cpflight { struct Rec; }
namespace takeover {
inline bool armed() { return g_cfg.takeoverAt >= 0 || g_cfg.takeoverX > 0.f; }
inline bool g_live = false;            // a person has the input (from the take to the mode's end)
inline bool g_done = false;            // the mode has ended in this session (it does not re-arm)
inline bool g_restoring = false;       // the reset that loads the checkpoint is running
inline bool g_restorePending = false;  // a reset GD asked for, to be a restore at the frame boundary
inline bool g_waiting = false;         // the stop before the person's run (cfg takeoverwait)
inline std::chrono::steady_clock::time_point g_waitUntil{};
struct Cp {
    std::shared_ptr<cpflight::Rec> rec;
    std::vector<InputCmd> before;
    long long tick = -1;
    float x = 0.f, y = 0.f, vy = 0.f;
    int n = 0;             // 1 = the start point's, which is never removed
    int dumpAttempt = 0;   // the session attempt that took it (dump.csv's attempt column)
    int parent = 0;        // n of the checkpoint that attempt started from (0: the plan's flight)
    const char* source = "";
    const void* gdSrc = nullptr;
};
enum class TakeKind { Start, Now, Pending };
inline std::vector<Cp> g_cps;          // the stack; [0] is the start point's
inline int g_from = 0;                 // n of the checkpoint the attempt in progress started from
inline int g_nextN = 0;                // the next checkpoint's n
inline bool g_cpWanted = false;        // the game stored one of its own at once (Z, a checkpoint
inline const void* g_wantedSrc = nullptr;   // ...the one it stored (compared, never dereferenced)
inline Cp g_pending;
inline bool g_hasPending = false;
inline const void* g_pendingSrc = nullptr;   // the game's pending checkpoint g_pending was taken for
inline bool g_commitPending = false;         // ...which the game has just stored
inline long long g_T = -1;             // the start point's tick
inline std::vector<InputCmd> g_human;  // this attempt's own presses (jump, player 1)
inline int g_other = 0;                // presses the plan format cannot carry (other buttons, 2P)
inline int g_attemptNo = 0;            // the mode's attempts: 1 is the one that took the checkpoint
inline int g_deaths = 0, g_restores = 0, g_missed = 0;
inline std::string g_dir;              // this session's folder for the records ("" until the take)
inline bool g_written = false;         // the attempt in progress has been written out
inline std::string g_endWhy;           // why the mode ended (the HUD says so)
// Still flying the plan towards the point (armed, not yet taken, not over).
inline bool approaching() { return armed() && !g_live && !g_done && g_started && !g_sessionOver; }
// A person, not the plan, is pressing now (the input block is lifted for them).
inline bool personDriving() { return g_live && g_started && !g_sessionOver; }
// GD asked for a reset while a person plays: it becomes a restore of the checkpoint.
inline bool takesReset() { return g_live && !g_restoring && g_started && !g_sessionOver; }
inline bool stopsBatch() { return g_live && g_waiting; }
inline void resetState() {
    g_live = g_done = g_restoring = g_restorePending = g_waiting = g_cpWanted = false;
    g_T = -1;
    g_cps.clear();
    g_pending = Cp{};
    g_hasPending = g_commitPending = false;
    g_pendingSrc = nullptr;
    g_wantedSrc = nullptr;
    g_from = g_nextN = 0;
    g_human.clear();
    g_other = g_attemptNo = g_deaths = g_restores = g_missed = 0;
    g_dir.clear();
    g_written = false;
    g_endWhy.clear();
}
// Defined in mod/takeover.hpp (the key is polled in session_hotkeys.hpp, ahead of it).
inline void endByKey();
}  // namespace takeover

// Live commands (data/cmd.txt): pause / resume / step N
inline bool g_paused = false;
inline bool g_serveMode = false;
// Raised while the in-process solve is running (see dpsolve::start / poll)
inline bool g_dpSolving = false;
inline bool g_secSearching = false;
inline bool solvingNow() { return botDriving() && (g_serveMode || g_dpSolving || g_secSearching); }
inline bool solveSession() { return botDriving() && (g_serveMode || g_cfg.dpSolve); }
inline bool g_dpShowSolution = false;
inline bool showingSolve() { return solveSession() && !g_dpShowSolution; }
inline bool g_serveWait = false;
inline bool g_serveReset = false;  // reset requested by rerun (executed on the physics-loop side)
inline int g_hudIter = 0;
// fixup usage (n/cap). The driver passes it as `fixups=n/cap` in hud.txt
inline int g_hudFixups = 0, g_hudFixupCap = 0;
inline float g_hudVerifiedX = 0.f, g_hudAnchorX = 0.f;
inline long long g_hudAnchorT = 0;
inline std::string g_hudPhase;
inline float g_clearMargin = 400.f;
// Auto-pause once the player's x passes this (cfg `pauseatx`). 0=disabled. For filming
inline float g_pauseAtX = 0.f;
inline bool g_pauseAtXFired = false;
inline bool g_stallResetPending = false;
inline int g_lastBatchCalls = 0;
inline bool g_hudOn = true;
inline bool g_renderOff = false;
inline bool g_overlayHidden = false;
inline bool solvingNow();
constexpr float WATCH_SPEEDS[] = { 0.25f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f };
constexpr int WATCH_SPEED_N = (int)(sizeof(WATCH_SPEEDS) / sizeof(WATCH_SPEEDS[0]));
constexpr int WATCH_SPEED_1X = 2;               // WATCH_SPEEDS[2] == 1.0
constexpr float WATCH_SPEED_MAX = WATCH_SPEEDS[WATCH_SPEED_N - 1];
inline int g_watchIdx = WATCH_SPEED_1X;
inline float g_watchSpeed = 1.f;
// Move one notch (dir = +1 faster, -1 slower). The ends just saturate; no wrap-around
inline void watchSpeedStep(int dir) {
    int n = g_watchIdx + dir;
    if (n < 0) n = 0;
    if (n > WATCH_SPEED_N - 1) n = WATCH_SPEED_N - 1;
    g_watchIdx = n;
    g_watchSpeed = WATCH_SPEEDS[n];
}
inline void watchSpeedSet(int idx) {
    if (idx < 0) idx = 0;
    if (idx > WATCH_SPEED_N - 1) idx = WATCH_SPEED_N - 1;
    g_watchIdx = idx;
    g_watchSpeed = WATCH_SPEEDS[idx];
}
inline float g_slowmo = 1.f;
inline bool g_visRefresh = false;
inline bool g_visRefreshOn = false;
inline bool g_visResetPending = false;
inline bool g_endzoneBurn = false;
inline std::chrono::steady_clock::time_point g_endzoneStart;
inline bool g_endzoneBurnOn = true;
// Wall-clock time at which the end-zone lock began (not locked = default-constructed zero).
inline std::chrono::steady_clock::time_point g_endzoneLockStart;
constexpr double ENDZONE_BURN_AFTER_SEC = 3.0;
constexpr double ENDZONE_KILL_AFTER_SEC = 10.0;
inline bool g_syncProbe = false;
// Record switch (touch trigger) contacts (cfg `trigtrace=1`)
inline bool g_trigTrace = false;
inline std::vector<std::string> g_cfgRemoved;
inline long long g_pressT0 = 0, g_pressT1 = -1;
inline long long g_stickT0 = 0, g_stickT1 = -1;
inline bool g_inPostCollP1 = false;
inline bool g_rngFresh = false;
inline bool g_rngSeedSet = false;
inline long long g_rngSeedEE0 = 0, g_rngSeedEF8 = 0;
inline bool g_rngFix = true;
constexpr long long kRngFixEE0 = 2531011, kRngFixEF8 = 0;
constexpr long long kRngFixE90 = 0;
constexpr size_t kRandomSeedOff = gdoff::kLayerRandomSeed;   // GJBaseGameLayer::m_randomSeed
inline long long g_rngDrawnInReset = 0;     // resets whose trigger seed was drawn before rngfix
inline void rngFixAfterReset(void* layer) {
    if (!botDriving() || !g_rngFix || !layer) return;
    auto* base = reinterpret_cast<unsigned char*>(geode::base::get());
    auto& seed = *reinterpret_cast<long long*>(base + gdoff::kSeedTriggerRva);
    auto& kept = *reinterpret_cast<long long*>(reinterpret_cast<char*>(layer) + kRandomSeedOff);
    if (seed != kept) ++g_rngDrawnInReset;
    seed = kRngFixE90;
    kept = kRngFixE90;
}
inline void rngFixApply() {
    if (!botDriving() || !(g_rngFix || g_rngSeedSet)) return;
    auto* base = reinterpret_cast<unsigned char*>(geode::base::get());
    *reinterpret_cast<long long*>(base + gdoff::kSeedVarIndexRva) =
        g_rngSeedSet ? g_rngSeedEE0 : kRngFixEE0;
    *reinterpret_cast<long long*>(base + gdoff::kSeedVarTableRva) =
        g_rngSeedSet ? g_rngSeedEF8 : kRngFixEF8;
}
inline bool g_attemptOne = false;
inline long long g_attemptCount = -1;
inline long long g_attemptReadsPinned = 0;     // attempt-count reads answered as on attempt 1 (or as k)
inline long long g_attemptReadsSeen = 0;       // attempt-count reads this session (first 20 printed)
inline long long g_persistTransfersSkipped = 0;   // kept-item copies not made
inline bool attemptPinActive() { return botDriving() && g_attemptOne; }
inline long long g_areaT0 = 0, g_areaT1 = -1;
inline std::vector<int> g_areaUids;
inline std::vector<GameObject*> g_areaObjs;
inline cocos2d::CCArray* g_areaArr = nullptr;
inline long long g_slopeT0 = 0, g_slopeT1 = -1;
inline int g_lagMs = 0;
inline long long g_lagAtTick = -1;
inline bool g_lagFired = false;
// Counters for isolating "it froze" (is rendering being skipped, or is the update not advancing)
inline long long g_visitSkips = 0;   // times the game-layer draw was skipped
inline long long g_visitDraws = 0;   // times it actually drew
inline long long g_pcCalls = 0;      // times processCommands was called (the physics entry point)
inline const void* g_resetMarkLayer = nullptr;
inline long long g_resetMarkPc = -1;
inline long long g_resetsSkipped = 0;
// Whether to stop all actions when toggling with F8 (cfg `watchpurge`). For A/B
inline bool g_watchPurge = true;
inline bool g_endPurge = false;
inline bool g_deathFx = false;
inline long long g_deathFxSkipped = 0;  // times not created (so that "zero" is visible)
inline long long g_soundWhileSolving = 0;
inline long long g_soundBlockedWhileSolving = 0;
// Song seeks skipped while a section search ran (the FMODAudioEngine hook in hooks_system.cpp).
inline long long g_secSongSeeksSkipped = 0;
// Times the silence had to be imposed again on something already sounding (audio::sync).
inline long long g_silenceReasserts = 0;
inline double g_retryAfterSec = 0.0;
inline bool g_retryDone = false;
inline int g_stepTicks = 0;

inline void writeResult(const std::string& text, bool truncate = false);

struct ActionManagerTick : cocos2d::CCActionManager {
    using cocos2d::CCActionManager::update;
};

inline void notifyActionsPurged();

inline void purgeDanglingActions() {
    if (auto* d = cocos2d::CCDirector::sharedDirector())
        if (auto* am = d->getActionManager()) am->removeAllActions();
    notifyActionsPurged();
}
}  // namespace p1

#include "mod/phase.hpp"   // the loop's named phases; needs writeResult's declaration above
