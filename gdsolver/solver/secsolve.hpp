#pragma once

namespace secsolve {
inline bool g_on = false;
inline long long g_startTick = -1;   // create the checkpoint at this tick and search from it
inline double g_targetX = 0.0;       // success when crossed alive (<=0 disables)
inline int g_horizon = 300;          // section length (ticks)
inline size_t g_cap = 100;           // states kept per layer
inline bool g_cover = true;
inline bool g_sizeFam = true;
inline bool g_p2Key = true;
inline int g_forceP1Size = -1;
inline int g_keepP1Size = -1;
inline long long g_keepDropped = 0;   // branches dropped for it, this search
inline double g_targetY = 0.0;       // cfg `sectargety`
inline int g_targetYDir = 0;         // cfg `sectargetydir`: +1 = y>=target / -1 = y<=target
                                     // / 0 = disabled
inline int g_targetDepth = 0;        // cfg `sectargetdepth`: success after surviving this
inline double g_yq = 0.25;           // dedupe y granularity
inline double g_vq = 0.1;            // dedupe vy granularity
inline double g_xq = 0.25;
inline bool g_done = false;          // once per session
// said once: "secsolve=1 but no checkpoint, so nothing ran"
inline bool g_warnedNoCkpt = false;

inline bool g_spineOn = true;        // cfg `secspine=0` turns it off for A/B
inline int g_spine = -1;             // node index of the spine at this layer
inline int g_spineNext = -1;         // ...and the child that continues it
inline long long g_spineUntil = -1;  // the tick the spine should live to, -1 = not known
constexpr long long kSpineSlack = 3; // ...less this many ticks before it counts
inline bool g_psnapLied = false;     // this search's spine died early on the snapshot
inline long long g_spineLostT = -1;  // ...at this tick
inline int g_spineOff = -1;   // MEASURED: -1 tracks all 60 layers to 0.0499 px;
inline double g_deadlineSec = 240.0;
inline bool g_verify = false;        // cfg `secverify=1`: no search, only check
inline bool g_log = false;
inline double g_dt = 1.0 / 240.0;
inline int g_off = 0;
inline int g_snapCmp = 0;
inline int g_snapReps = 3;
inline int g_snapMode = 0;
inline int g_rephase = 5000;
inline bool g_trailClear = true;
inline bool g_snapOn = false;        // is psnap actually used in this section
inline bool g_worldOn = false;
inline size_t g_maxMoving = 4000;    // cfg `secmaxmov`: beyond this fall back
                                     // to the checkpoint
inline std::vector<GameObject*> g_movSet;   // decided once at the section start
inline bool g_wsnapAuto = true;
inline int g_wsnapSet = 5;
inline bool g_wsnapAny = true;
inline bool g_wsnapGate = true;
inline bool g_wsnapForce = false;    // the rung in flight is that re-ask: the set goes back in
inline size_t g_wsnapHeld = 0;       // the size of the set this search held back (0: none)
inline bool g_lieSkipPsnap = false;
inline std::vector<long long> g_liedWindows;   // the starts (g_startTick) of the windows caught so
inline bool liedWindow(long long start) {
    for (long long s : g_liedWindows)
        if (s == start) return true;
    return false;
}
// cfg `secwsnapgroups=a,b,...` (diagnosis): the set is exactly these groups' objects.
inline std::vector<int> g_wsnapGroups;
inline int g_worldDiff = 0;
inline bool g_noKill = false;   // set only by the search proper (not by the sweep)
inline int g_killOverride = -1;
inline bool g_killLog = false;
inline int g_depth = 0;          // the layer currently being expanded (for logging)
inline bool g_visRefresh = false;
inline int g_overlay = 0;
inline long long g_memAt = -1;
inline int g_winShift = 0;
inline bool g_colLog = false;
inline int g_verifyEvery = 0;
inline double g_verifyTol = 1e-4;
inline bool g_anchor = true;
inline bool g_leafVerified = false;
inline int g_leafDeadAt = -1;
inline int g_grace = 600;
inline int g_graceFly = 0;
inline bool g_graceDash = true;
inline bool g_leafMid = true;
inline int g_leafLife = 0;
inline bool g_driftLog = false;
inline bool g_secCoins = true;
inline int g_rungCoin = -1;
inline bool g_rungCoinOff = true;
inline bool g_bookFirst = true;
inline bool g_boundOn = true;
constexpr double kBoundMargin = 3000.0;
inline double g_boxX0 = 0.0, g_boxX1 = 0.0, g_boxY0 = 0.0, g_boxY1 = 0.0;
inline long long g_boundSkips = 0;
constexpr double kPosLimit = 1e8;
inline long long g_nonFinite = 0;
inline int g_nonFiniteUid = -1, g_nonFiniteId = -1;
inline double g_nonFiniteX = 0.0, g_nonFiniteY = 0.0;
// cfg secthrow: the exceptions thrown on purpose so far in this process (Config::secThrowKind).
inline int g_secThrowsDone = 0;
inline bool g_shaderSkip = true;
inline long long g_shaderSkips = 0;

inline bool g_shaderSig = false;
inline bool g_shaderSigPending = false;   // a search ended; the next real reset prints
inline std::string shaderSig(GJBaseGameLayer* l) {
    if (!l) return "no layer";
    auto kind = [l](cocos2d::CCNode* p) -> char {
        if (!p) return '0';
        if (p == l->m_inShaderParent) return 'I';
        if (p == l->m_aboveShaderParent) return 'A';
        if (p == l->m_objectLayer) return 'O';
        if (p == l->m_inShaderObjectLayer) return 'i';
        if (p == l->m_aboveShaderObjectLayer) return 'a';
        if (p == l) return 'L';
        return '?';
    };
    std::string seq;
    size_t in = 0, above = 0, other = 0;
    if (l->m_batchNodes) {
        for (auto* n : geode::cocos::CCArrayExt<cocos2d::CCNode*>(l->m_batchNodes)) {
            const char k = kind(n ? n->getParent() : nullptr);
            seq += k;
            seq += std::to_string(n ? n->getZOrder() : 0);
            seq += ',';
            if (k == 'I' || k == 'i') ++in;
            else if (k == 'A' || k == 'a') ++above;
            else ++other;
        }
    }
    std::string layers;
    for (cocos2d::CCNode* n : {static_cast<cocos2d::CCNode*>(l->m_objectLayer),
                               static_cast<cocos2d::CCNode*>(l->m_inShaderObjectLayer),
                               static_cast<cocos2d::CCNode*>(l->m_aboveShaderObjectLayer),
                               l->m_inShaderParent, l->m_aboveShaderParent})
        layers += kind(n ? n->getParent() : nullptr);
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char ch : seq + "|" + layers) { h ^= ch; h *= 1099511628211ULL; }
    char b[160];
    snprintf(b, sizeof(b), "batches=%zu in=%zu above=%zu other=%zu layers=%s shader=%s sig=%08x",
             l->m_batchNodes ? (size_t)l->m_batchNodes->count() : (size_t)0, in, above, other,
             layers.c_str(), (l->m_shaderLayer && l->m_shaderLayer->isVisible()) ? "on" : "off",
             (unsigned)(h & 0xffffffffULL));
    return b;
}
inline long long g_maxDoomed = 500;
inline double g_maxY = 0.0;
inline double g_maxYAuto = 0.0;   // computed once at the section start
inline bool g_died = false;     // did a death verdict come in the previous step

inline bool& g_active = g_secSearching;
inline void (*g_onActive)() = nullptr;
inline void setSearching(bool v) {
    g_active = v;
    if (g_onActive) g_onActive();
}
inline int g_feed = 0;              // button state given at this tick (0/1)
inline int g_held = 0;               // currently pressing? (for handleButton deltas)
inline bool g_jumpBuf = true;
inline bool g_jbKeep = true;
inline int8_t g_headJb[2] = {-1, -1};   // m_jumpBuffered of p1 / p2 in the plain flight, -1 = none

struct DashState {
    bool on = false;
    GameObject* ring = nullptr;
    double x = 0, y = 0, angle = 0, startTime = 0;
    // Below: pointers whose omission changes behaviour on restore (12 of them)
    GameObject* lastGround = nullptr;
    GameObject* maybeLastGround = nullptr;
    GameObject* preLastGround = nullptr;
    GameObject* collided = nullptr;
    GameObject* collideLeft = nullptr;
    GameObject* collideRight = nullptr;
    GameObject* slope = nullptr;
    GameObject* slope2 = nullptr;
    GameObject* potentialSlope = nullptr;
    GameObject* snappedTo = nullptr;
    GameObject* lastPortal = nullptr;
    int8_t jumpBuf = -1;   // m_jumpBuffered at the capture (cfg secjbkeep), -1 = not taken
};

struct Node {
    int parent;      // -1 = the section start
    uint8_t in;      // input given at this tick
    uint8_t dash;    // dashing? (held so the cap does not squash the kinds)
    float y;
    float vy;
    float x;
    uint16_t cnt;
    uint8_t mini;    // player 1 mini? (a family of the cap under cfg secsizefam)
    uint8_t dual;    // in a dual section? (then y2 is player 2's y; see g_p2Key)
    float y2;
};
inline std::vector<Node> g_nodes;
inline bool g_cpPlayer = true;
inline std::vector<uint8_t> g_headPlain;
inline std::vector<DashState> g_dash;
inline std::vector<double> g_vy;
inline std::vector<double> g_accel;
inline std::vector<uint8_t> g_pad;
inline bool g_headAccel = true;
inline bool g_p2Extras = true;
inline std::vector<DashState> g_dash2;
inline std::vector<double> g_vy2;
inline std::vector<double> g_accel2;
inline std::vector<uint8_t> g_pad2;
inline DashState g_ckptDash;
inline bool g_headDash = true;
inline DashState g_ckptDash2;
inline std::vector<CheckpointObject*> g_cps;
inline std::unordered_map<int, CheckpointObject*> g_anchors;

inline void releaseAnchors() {
    for (auto& kv : g_anchors) if (kv.second) kv.second->release();
    g_anchors.clear();
}

inline size_t nodeTableBytes() {
    return g_cps.capacity() * sizeof(CheckpointObject*) + g_nodes.capacity() * sizeof(Node)
           + (g_dash.capacity() + g_dash2.capacity()) * sizeof(DashState)
           + (g_vy.capacity() + g_accel.capacity() + g_vy2.capacity() + g_accel2.capacity()) * sizeof(double)
           + g_pad.capacity() + g_pad2.capacity();
}

inline void freeNodeTables() {
    for (size_t i = 1; i < g_cps.size(); ++i)
        if (g_cps[i]) g_cps[i]->release();
    { decltype(g_cps) e; g_cps.swap(e); }
    { decltype(g_nodes) e; g_nodes.swap(e); }
    { decltype(g_dash) e; g_dash.swap(e); }
    { decltype(g_dash2) e; g_dash2.swap(e); }
    { decltype(g_vy) e; g_vy.swap(e); }
    { decltype(g_accel) e; g_accel.swap(e); }
    { decltype(g_pad) e; g_pad.swap(e); }
    { decltype(g_vy2) e; g_vy2.swap(e); }
    { decltype(g_accel2) e; g_accel2.swap(e); }
    { decltype(g_pad2) e; g_pad2.swap(e); }
}

struct SecDeadBand { double x0, x1; int mode; double y0, y1; };
inline std::vector<SecDeadBand> g_secBands;

inline long long g_cntBase = 0;
inline long long countSum(GJBaseGameLayer* l) {
    if (!l) return 0;
    long long s = 0;
    if (auto* em = l->m_effectManager)
        for (auto& kv : em->m_itemCountMap) s += (long long)kv.second;
    s += (long long)l->m_disabledObjects.size() * 131LL;
    return s;
}
inline int cntNow(GJBaseGameLayer* l) {
    long long d = countSum(l) - g_cntBase;
    if (d < 0) d = -d;
    if (d > 65535) d = 65535;
    return (int)d;
}

inline int g_sliceMs = 50;
inline std::chrono::steady_clock::time_point g_sliceStart;
inline long long g_frames = 0;
inline double g_frameGapMs = 0.0, g_overlayMs = 0.0;
inline double g_outsideMs = 0.0;   // ...of which outside the game layer's update (the frame)
inline std::chrono::steady_clock::time_point g_sliceEnd;
inline bool g_sliceEnded = false;

inline bool sliceExpired() {
    if (g_sliceMs <= 0) return false;
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - g_sliceStart).count() >= (double)g_sliceMs;
}

struct SecTask {
    struct promise_type {
        SecTask get_return_object() {
            return SecTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() noexcept { return {}; }
        // Kept alive after the body returns so done() can be asked; the driver destroys it.
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
    using handle_t = std::coroutine_handle<promise_type>;
    handle_t h{};

    SecTask() = default;
    explicit SecTask(handle_t hh) : h(hh) {}
    SecTask(const SecTask&) = delete;
    SecTask& operator=(const SecTask&) = delete;
    SecTask(SecTask&& o) noexcept : h(o.h) { o.h = {}; }
    SecTask& operator=(SecTask&& o) noexcept {
        if (this != &o) { destroy(); h = o.h; o.h = {}; }
        return *this;
    }
    ~SecTask() { destroy(); }

    bool valid() const { return (bool)h; }
    bool done() const { return h && h.done(); }
    void resume() { if (h && !h.done()) h.resume(); }
    void destroy() { if (h) { h.destroy(); h = {}; } }
};

// The search in flight (empty when none). Owned by the game layer's update.
inline SecTask g_task;
inline void* g_taskLayer = nullptr;
inline bool inFlight() { return g_task.valid() && !g_task.done(); }
// Frontier size of the last completed layer, for the HUD.
inline size_t g_frontierNow = 0;

inline void reset() {
    g_task.destroy();
    g_taskLayer = nullptr;
    g_frontierNow = 0;
    g_keepDropped = 0;   // cfg seckeepsize's count (a search also starts it at 0)
    g_psnapLied = false;
    g_liedWindows.clear();   // cfg seclieskippsnap: the windows are this layer's
    g_spineLostT = -1;
    g_done = false;
    setSearching(false);
    g_on = false;
    g_noKill = false;
    g_died = false;
    g_rungCoin = -1;
    g_feed = 0;
    g_held = 0;
    freeNodeTables();   // after the task: its frame indexes them
    releaseAnchors();
    g_movSet.clear();
    g_worldOn = false;
    g_cntBase = 0;
}

inline long long keyOf(double y, double vy, int mode, int mini, int flip,
                       int held, double x, int dash = 0, int cnt = 0) {
    const long long qy = (long long)std::llround(y / (g_yq > 0 ? g_yq : 1.0));
    const long long qv = (long long)std::llround(vy / (g_vq > 0 ? g_vq : 1.0));
    const long long qx = g_xq > 0
        ? (long long)std::llround(x / g_xq) : 0LL;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](long long v) {
        const uint8_t* b = (const uint8_t*)&v;
        for (size_t i = 0; i < sizeof(v); ++i) { h ^= b[i]; h *= 1099511628211ull; }
    };
    mix(((((long long)mode * 3 + mini) * 2 + flip) * 2 + held) * 2 + dash);
    mix(qy);
    mix(qv);
    mix(qx);
    mix((long long)cnt);
    return (long long)h;
}

inline bool reachedGoal(double px, double py, int depth) {
    bool any = false;
    if (g_targetX > 0.0) { if (px < g_targetX) return false; any = true; }
    if (g_targetYDir > 0) { if (py < g_targetY) return false; any = true; }
    if (g_targetYDir < 0) { if (py > g_targetY) return false; any = true; }
    if (g_targetDepth > 0) { if (depth < g_targetDepth) return false; any = true; }
    return any;      // with no condition at all, never succeed (a misconfig
                     // would make every branch a success)
}

inline std::vector<uint8_t> inputVecOf(int leaf) {
    std::vector<uint8_t> seq;
    for (int i = leaf; i > 0; i = g_nodes[(size_t)i].parent)
        seq.push_back(g_nodes[(size_t)i].in);
    std::reverse(seq.begin(), seq.end());
    return seq;
}

inline std::string inputsOf(int leaf) {
    const std::vector<uint8_t> seq = inputVecOf(leaf);
    std::string s;
    for (size_t i = 0; i < seq.size(); ++i) {
        if (i) s += ',';
        s += (char)('0' + seq[i]);
    }
    return s;
}
}  // namespace secsolve
