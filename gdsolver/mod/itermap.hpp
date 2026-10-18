#pragma once
#include <chrono>
#include <map>
#include <mutex>

#include "mod/config.hpp"

namespace p1 {
namespace itermap {
inline std::mutex g_mu;

// ---- what a round leaves behind ----

enum Kind {
    KindDeeper = 0,   // deeper than anything before -- progress, back-off reset
    KindFollow = 1,   // a regression on a tail the model had solved; followed anyway
    KindForced = 2,   // ...on the route a forced mode-portal crossing opened
    KindRewind = 3,   // no improvement: rewound to the deepest plan, backed off further
    KindWedge  = 4,   // wedged / void: credited where it stopped moving, never ranks
    KindCount  = 5,
};

inline const char* kindName(int k) {
    switch (k) {
        case KindDeeper: return "deeper";
        case KindFollow: return "followed";
        case KindForced: return "forced";
        case KindWedge:  return "wedged";
        default:         return "rewound";
    }
}

struct Death {
    int iter = 0;
    long long tick = 0;
    float x = 0.f, y = 0.f;
    int kind = KindRewind;
    long long anchorT = -1;   // the anchor the plan that died was spliced at (-1 = the first plan)
    float anchorX = 0.f;
    int backoff = 0;
    int killerId = -1;        // GD's own verdict (destroyPlayer's argument), not a guess
    int killerUid = -1;
};

struct Fixup {
    int iter = 0;
    long long tick = 0;
    float x = 0.f, y = 0.f;
    int kill = 0;             // a fixup that records "GD ends the run at this state"
    int sameDeath = 0;
};

struct Veto {
    int iter = 0;
    float x0 = 0.f, x1 = 0.f;
};

struct Path {
    int iter = 0;
    int kind = KindRewind;
    std::vector<float> xy;    // flat pairs; every kPathStep ticks
};
constexpr int kPathStep = 8;
constexpr size_t kPathMaxPts = 4000;

inline std::vector<Death> g_deaths;
inline std::vector<Fixup> g_fixups;
inline std::vector<Veto>  g_vetoes;
inline std::vector<Path>  g_paths;
inline int g_rounds = 0;        // rounds the run took (the last round recorded, or from the file)
inline bool g_cleared = false;  // whether the run that produced this map went on to clear
inline int g_mapLevel = -1;     // the level the loaded map belongs to (-1 = nothing loaded)
inline int g_loadTried = -1;

inline int g_generation = 0;

inline int g_mapPref = -1;

inline bool mapWanted() { return g_mapPref >= 0 ? g_mapPref != 0 : solveSession(); }

inline bool barVisible() {
    return g_hudOn && !g_overlayHidden && botDriving() && !showingSolve();
}
inline bool mapVisible() {
    return mapWanted() && g_hudOn && !g_overlayHidden && botDriving();
}
inline bool stripVisible() { return barVisible() || mapVisible(); }

inline long long g_seekTick = -1;
inline bool g_seekRestart = false;
inline bool g_seekRepause = false;
inline long long g_seekStepTo = -1;
constexpr long long kStepUndershoot = 16;   // comfortably more than one batch
constexpr float kSeekLead = 240.f;
inline bool seeking() { return g_seekTick >= 0 || g_seekRestart; }
inline std::chrono::steady_clock::time_point g_seekT0{};
inline long long g_seekFrames = 0, g_seekBatches = 0, g_seekVisCalls = 0;
inline double g_seekVisMs = 0.0, g_seekDrawMs = 0.0;
inline int g_seekVisEvery = 1;
// Set by the update hook around each batch: this batch's visibility pass may be skipped.
inline bool g_seekVisSkip = false;
inline bool g_seekBlind = true;
inline bool g_seekBlindNow = false;
inline bool g_seekBlindDone = false;
inline double g_seekBlindMs = 0.0;
// cfg `profsample` (mod/profsample.hpp). Kept here because the cfg reader comes before that file.
inline bool g_profSample = false;
inline bool g_sectionHookLazy = true;
inline long long g_seekCpEvery = 600;
inline long long g_seekCpFrom = 0, g_seekCpTo = -1;
inline bool g_seekCpKeep = false;
inline long long g_seekFromCp = -1;
inline long long g_latchAt = 0;   // cfg latchat (print only, hooks_gamelayer latchLine)
inline bool g_headCheck = false;  // cfg headcheck (print only, seek_cp.hpp)
inline long long g_batchFrom = -1, g_batchTo = -1;   // cfg batchlog=from:to (print only, hooks_gamelayer)
inline long long g_pauseAtTick = 0;
inline std::vector<int> g_objWatch;   // cfg objwatch (print only, hooks_gamelayer cpAfterUpdate)
inline bool g_groupCheck = false;
inline bool g_actCensus = false;
struct ActCensusObj { long long first = -1, last = -1, boundaries = 0; int id = 0, kind = 0; };
struct ActCensus { std::map<int, ActCensusObj> objs; long long boundaries = 0, checked = 0; };
inline ActCensus g_actCensusState;
inline std::string actCensusLine() {
    const auto& c = g_actCensusState;
    std::string s = "actcensus: summary - boundaries with a switched-off activated object near a player "
        + std::to_string(c.boundaries) + " of " + std::to_string(c.checked) + ", objects " + std::to_string(c.objs.size()) + ":";
    int n = 0;
    for (const auto& [uid, e] : c.objs) {
        if (++n > 30) { s += " ..."; break; }
        s += " " + std::to_string(uid) + "/" + std::to_string(e.id) + (e.kind ? "h" : "s") + "@" + std::to_string(e.first)
             + "-" + std::to_string(e.last) + "x" + std::to_string(e.boundaries);
    }
    return s;
}   // cfg groupcheck (print only, hooks_gamelayer groupSectionCheck)   // cfg pauseattick (hooks_gamelayer, next to pauseatx)
inline long long g_regionAt = 0;   // cfg regionat / regionbox (print only, hooks_gamelayer regionLine)
inline float g_regionBox[4] = {0.f, 0.f, 0.f, 0.f};
inline bool (*g_cpAhead)(long long now, long long target) = nullptr;   // a checkpoint to jump ahead to
inline std::string (*g_cpLine)() = nullptr;                            // its counts, after each seek

inline long long g_seekBarTick = 0;
inline bool g_seekForward = true;

inline std::vector<float> g_xHist;

inline long long g_nowTick = 0;
inline long long g_barTicksSeen = 0;

inline void trackX(long long tick, float x) {
    g_nowTick = tick;
    if (tick > g_barTicksSeen) g_barTicksSeen = tick;
    if (tick < 0 || tick > 400000) return;    // the same runaway cap the anchors use
    if ((size_t)tick + 1 > g_xHist.size()) g_xHist.resize((size_t)tick + 1, -1.f);
    g_xHist[(size_t)tick] = x;
}

inline void onAttemptStart() { g_xHist.clear(); g_nowTick = 0; }

inline long long barTicks() {
    long long t = g_barTicksSeen;
    if (!g_cfg.inputs.empty() && (long long)g_cfg.inputs.back().step > t)
        t = (long long)g_cfg.inputs.back().step;
    if (t < 240) t = 240;
    return t + 480;
}

// The x the run was at on that tick, or -1 if it has not been there.
inline float xAtTick(long long tick) {
    if (tick < 0 || (size_t)tick >= g_xHist.size()) return -1.f;
    return g_xHist[(size_t)tick];
}

inline bool g_dragging = false;
inline float g_dragFrac = 0.f;      // 0..1 along the bar
constexpr int kSeekLoopsMax = 400;
constexpr float kSeekPxPerLoop = 16.f;

inline float g_stripX = 0.f, g_stripY = 0.f, g_stripW = 0.f, g_stripH = 0.f;
inline long long g_barTicks = 0;   // the tick span the bar was drawn against
inline float g_labelW = 0.f, g_labelH = 0.f;

inline void clearLocked() {
    g_deaths.clear();
    g_fixups.clear();
    g_vetoes.clear();
    g_paths.clear();
    g_rounds = 0;
    g_cleared = false;
    g_mapLevel = -1;
    g_loadTried = -1;
    g_seekTick = -1;
    g_seekRestart = false;
    g_seekRepause = false;
    g_seekStepTo = -1;
    g_dragging = false;
    g_barTicksSeen = 0;
    ++g_generation;
}

inline void clear() {
    std::lock_guard<std::mutex> lk(g_mu);
    clearLocked();
}

// What the map holds allocated (capacities), for the memory census (cfg memcensus).
inline size_t heldBytes() {
    std::lock_guard<std::mutex> lk(g_mu);
    size_t n = g_deaths.capacity() * sizeof(Death) + g_fixups.capacity() * sizeof(Fixup)
               + g_vetoes.capacity() * sizeof(Veto) + g_paths.capacity() * sizeof(Path);
    for (const Path& p : g_paths) n += p.xy.capacity() * sizeof(float);
    return n;
}

inline long long g_killTick = -1;
inline int g_killId = -1, g_killUid = -1;
inline float g_killY = 0.f;

inline void latchKiller(long long tick, float py, int objId, int objUid) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_killTick = tick;
    g_killY = py;
    g_killId = objId;
    g_killUid = objUid;
}

inline void addDeath(int iter, long long tick, float x, float y, int kind,
                     long long anchorT, float anchorX, int backoff) {
    std::lock_guard<std::mutex> lk(g_mu);
    Death d;
    d.iter = iter;
    d.tick = tick;
    d.x = x;
    d.y = (g_killTick == tick && g_killY != 0.f) ? g_killY : y;
    d.kind = kind;
    d.anchorT = anchorT;
    d.anchorX = anchorX;
    d.backoff = backoff;
    if (g_killTick == tick) { d.killerId = g_killId; d.killerUid = g_killUid; }
    g_deaths.push_back(d);
    if (iter > g_rounds) g_rounds = iter;
    ++g_generation;
}

inline void addFixup(int iter, long long tick, float x, float y, bool kill, bool sameDeath = false) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_fixups.push_back(Fixup{iter, tick, x, y, kill ? 1 : 0, sameDeath ? 1 : 0});
    ++g_generation;
}

inline size_t searchFixupsLocked() {
    size_t n = 0;
    for (const Fixup& x : g_fixups) n += x.sameDeath ? 1 : 0;
    return n;
}

inline void addVeto(int iter, float x0, float x1) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_vetoes.push_back(Veto{iter, x0, x1});
    ++g_generation;
}

inline void beginPath(int iter, int kind) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_paths.push_back(Path{iter, kind, {}});
}
inline void addPathPoint(float x, float y) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_paths.empty()) return;
    std::vector<float>& v = g_paths.back().xy;
    if (v.size() >= kPathMaxPts * 2) return;
    v.push_back(x);
    v.push_back(y);
}
inline void endPath() {
    std::lock_guard<std::mutex> lk(g_mu);
    // A tail of one point is a dot the death mark already draws.
    if (!g_paths.empty() && g_paths.back().xy.size() < 4) g_paths.pop_back();
    ++g_generation;
}

// ---- the file ----

inline std::string pathFor(int levelId) {
    char name[64];
    snprintf(name, sizeof(name),
             g_cfg.coinFiles ? "/itermap_lv%d_coins.txt" : "/itermap_lv%d.txt", levelId);
    return std::string(DATA_DIR) + name;
}

inline bool emptyLocked() { return g_deaths.empty() && g_fixups.empty() && g_paths.empty(); }

inline bool save(int levelId, bool cleared) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (emptyLocked()) return false;
    g_cleared = cleared;
    const std::string path = pathFor(levelId);
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << "# gdsolver iteration map\n";
    f << "level=" << levelId << "\n";
    f << "rounds=" << g_rounds << "\n";
    f << "cleared=" << (cleared ? 1 : 0) << "\n";
    for (const Death& d : g_deaths)
        f << "death=" << d.iter << ',' << d.tick << ',' << d.x << ',' << d.y << ','
          << d.kind << ',' << d.anchorT << ',' << d.anchorX << ',' << d.backoff << ','
          << d.killerId << ',' << d.killerUid << "\n";
    for (const Fixup& x : g_fixups)
        f << "fixup=" << x.iter << ',' << x.tick << ',' << x.x << ',' << x.y << ','
          << x.kill << ',' << x.sameDeath << "\n";
    for (const Veto& v : g_vetoes)
        f << "veto=" << v.iter << ',' << v.x0 << ',' << v.x1 << "\n";
    for (const Path& p : g_paths) {
        if (p.xy.size() < 4) continue;
        f << "path=" << p.iter << ',' << p.kind;
        for (float v : p.xy) f << ',' << v;
        f << "\n";
    }
    g_mapLevel = levelId;
    return true;
}

inline bool load(int levelId) {
    std::ifstream f(pathFor(levelId));
    if (!f) return false;
    std::lock_guard<std::mutex> lk(g_mu);
    clearLocked();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const char* v = line.c_str() + eq + 1;
        if (key == "rounds") { g_rounds = std::atoi(v); }
        else if (key == "cleared") { g_cleared = (*v == '1'); }
        else if (key == "death") {
            Death d;
            double x = 0, y = 0, ax = 0;
            long long tick = 0, at = -1;
            if (sscanf(v, "%d,%lld,%lf,%lf,%d,%lld,%lf,%d,%d,%d", &d.iter, &tick, &x, &y,
                       &d.kind, &at, &ax, &d.backoff, &d.killerId, &d.killerUid) >= 5) {
                d.tick = tick; d.x = (float)x; d.y = (float)y;
                d.anchorT = at; d.anchorX = (float)ax;
                g_deaths.push_back(d);
                if (d.iter > g_rounds) g_rounds = d.iter;
            }
        } else if (key == "fixup") {
            Fixup x;
            double fx = 0, fy = 0;
            long long tick = 0;
            // The sixth field (sameDeath) is absent from maps written before it existed: 0.
            if (sscanf(v, "%d,%lld,%lf,%lf,%d,%d", &x.iter, &tick, &fx, &fy, &x.kill,
                       &x.sameDeath) >= 4) {
                x.tick = tick; x.x = (float)fx; x.y = (float)fy;
                g_fixups.push_back(x);
            }
        } else if (key == "path") {
            Path p;
            const char* q = v;
            char* end = nullptr;
            p.iter = (int)std::strtol(q, &end, 10);
            if (end == q || *end != ',') continue;
            q = end + 1;
            p.kind = (int)std::strtol(q, &end, 10);
            if (end == q) continue;
            q = end;
            while (*q == ',') {
                const double d = std::strtod(q + 1, &end);
                if (end == q + 1) break;
                p.xy.push_back((float)d);
                q = end;
            }
            if (p.xy.size() >= 4) {
                p.xy.resize(p.xy.size() & ~size_t(1));   // pairs only
                g_paths.push_back(std::move(p));
            }
        } else if (key == "veto") {
            Veto vt;
            double a = 0, b = 0;
            if (sscanf(v, "%d,%lf,%lf", &vt.iter, &a, &b) == 3) {
                vt.x0 = (float)a; vt.x1 = (float)b;
                g_vetoes.push_back(vt);
            }
        }
    }
    g_mapLevel = levelId;
    ++g_generation;
    return !emptyLocked();
}

inline void ensureLoaded(int levelId) {
    if (levelId <= 0 || g_loadTried == levelId) return;
    if (g_cfg.dpSolve && !g_dpShowSolution) {
        g_loadTried = levelId;
        return;
    }
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (!emptyLocked()) {                                  // this run owns the map
            g_loadTried = levelId;
            return;
        }
    }
    load(levelId);
    g_loadTried = levelId;
}

constexpr float kBucket = 30.f;

struct Hot {
    float x0 = 0.f;      // the bucket's left edge in level coordinates
    int deaths = 0;
    int fixups = 0;
    int searchFixups = 0;   // of `fixups`, the ones a search sent (Fixup::sameDeath)
    int firstIter = 0, lastIter = 0;
    float yLo = 0.f, yHi = 0.f;
    int dominant = KindRewind;   // the kind most of this bucket's deaths were scored as
    int byKind[KindCount] = {0, 0, 0, 0, 0};
};

inline std::vector<Hot> g_hot;
inline int g_hotGen = -1;
inline int g_hotMax = 0;         // the busiest bucket's death count (the scale for every bar)

// The caller holds g_mu.
inline void rebuildHotLocked() {
    if (g_hotGen == g_generation) return;
    g_hotGen = g_generation;
    g_hot.clear();
    g_hotMax = 0;
    std::map<int, Hot> by;
    for (const Death& d : g_deaths) {
        const int b = (int)std::floor(d.x / kBucket);
        auto it = by.find(b);
        if (it == by.end()) {
            Hot h;
            h.x0 = b * kBucket;
            h.firstIter = h.lastIter = d.iter;
            h.yLo = h.yHi = d.y;
            it = by.emplace(b, h).first;
        }
        Hot& h = it->second;
        ++h.deaths;
        if (d.kind >= 0 && d.kind < KindCount) ++h.byKind[d.kind];
        if (d.iter < h.firstIter) h.firstIter = d.iter;
        if (d.iter > h.lastIter) h.lastIter = d.iter;
        if (d.y < h.yLo) h.yLo = d.y;
        if (d.y > h.yHi) h.yHi = d.y;
    }
    for (const Fixup& x : g_fixups) {
        const int b = (int)std::floor(x.x / kBucket);
        auto it = by.find(b);
        if (it == by.end()) {
            Hot h;
            h.x0 = b * kBucket;
            h.firstIter = h.lastIter = x.iter;
            h.yLo = h.yHi = x.y;
            it = by.emplace(b, h).first;
        }
        ++it->second.fixups;
        if (x.sameDeath) ++it->second.searchFixups;
    }
    g_hot.reserve(by.size());
    for (auto& kv : by) {
        Hot& h = kv.second;
        for (int k = 0; k < KindCount; ++k)
            if (h.byKind[k] > h.byKind[h.dominant]) h.dominant = k;
        g_hot.push_back(h);
        if (h.deaths > g_hotMax) g_hotMax = h.deaths;
    }
}

inline const Hot* hottestLocked() {
    rebuildHotLocked();
    const Hot* best = nullptr;
    for (const Hot& h : g_hot)
        if (!best || h.deaths > best->deaths) best = &h;
    return best;
}

// The bucket the player is standing in right now (or nullptr). Same contract.
inline const Hot* atLocked(float px) {
    rebuildHotLocked();
    for (const Hot& h : g_hot)
        if (px >= h.x0 && px < h.x0 + kBucket) return &h;
    return nullptr;
}

// ---- drawing ----

constexpr int WORLD_TAG = 0x51D60;   // the CCDrawNode parented to the object layer
constexpr int STRIP_TAG = 0x51D61;   // ...and the one in the bottom-right corner
constexpr int LABEL_TAG = 0x51D62;   // the summary text that goes with it
constexpr int TOUCH_TAG = 0x51D63;   // the layer that turns a click on the strip into a seek
constexpr int COUNT_TAG = 0x51D64;   // holds the per-column death counts, in level coordinates
constexpr int COVER_TAG = 0x51D65;   // the blackout drawn over the level while seeking

// Colours, in one place so the legend and the marks cannot drift apart.
inline cocos2d::ccColor4F kindColor(int k, float a) {
    switch (k) {
        case KindDeeper: return {0.30f, 0.95f, 0.45f, a};   // progress
        case KindFollow: return {0.35f, 0.80f, 1.00f, a};   // followed a solved branch
        case KindForced: return {0.70f, 0.55f, 1.00f, a};   // the forced portal route
        case KindWedge:  return {1.00f, 0.45f, 0.95f, a};   // wedged, never ranks
        default:         return {1.00f, 0.30f, 0.25f, a};   // rewound: the loop is stuck here
    }
}

inline cocos2d::CCDrawNode* drawNode(cocos2d::CCNode* parent, int tag, const std::string& id,
                                     int z) {
    using namespace cocos2d;
    if (!parent) return nullptr;
    auto* n = static_cast<CCDrawNode*>(parent->getChildByTag(tag));
    if (!n) {
        n = CCDrawNode::create();
        if (!n) return nullptr;
        n->setTag(tag);
        n->setID(id);
        n->setZOrder(z);
        parent->addChild(n);
    }
    return n;
}

inline void fillRect(cocos2d::CCDrawNode* n, float x0, float y0, float x1, float y1,
                     const cocos2d::ccColor4F& c) {
    using namespace cocos2d;
    CCPoint v[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    n->drawPolygon(v, 4, c, 0.f, ccColor4F{0.f, 0.f, 0.f, 0.f});
}

inline void drawWorld(cocos2d::CCNode* objectLayer) {
    using namespace cocos2d;
    if (!objectLayer) return;
    auto* n = drawNode(objectLayer, WORLD_TAG, "itermap-world"_spr, 1 << 20);
    if (!n) return;
    auto* counts = objectLayer->getChildByTag(COUNT_TAG);
    if (!counts) {
        counts = CCNode::create();
        if (!counts) return;
        counts->setTag(COUNT_TAG);
        counts->setID("itermap-counts"_spr);
        counts->setZOrder((1 << 20) + 1);
        objectLayer->addChild(counts);
    }
    n->setVisible(mapVisible());
    counts->setVisible(mapVisible());
    if (!mapVisible()) return;
    std::lock_guard<std::mutex> lk(g_mu);
    static int s_gen = -1;
    static CCNode* s_parent = nullptr;
    if (s_gen == g_generation && s_parent == objectLayer) return;
    s_gen = g_generation;
    s_parent = objectLayer;
    rebuildHotLocked();
    n->clear();
    counts->removeAllChildrenWithCleanup(true);
    // 1. the veto boxes, underneath everything: stretches the loop ruled out as routes
    for (const Veto& v : g_vetoes)
        fillRect(n, v.x0, -300.f, v.x1, 3200.f, ccColor4F{0.55f, 0.15f, 0.65f, 0.10f});
    for (const Hot& h : g_hot) {
        if (h.deaths <= 0) continue;
        const float f = g_hotMax > 0 ? (float)h.deaths / (float)g_hotMax : 0.f;
        const float lo = h.yLo - 200.f, hi = h.yHi + 200.f;
        fillRect(n, h.x0, lo, h.x0 + kBucket, hi,
                 ccColor4F{1.00f, 0.25f, 0.20f, 0.05f + 0.25f * f});
    }
    const float ta = std::max(0.05f, std::min(0.20f, 2.0f / std::max(1.f, (float)g_paths.size())));
    for (const Path& p : g_paths) {
        const ccColor4F c = kindColor(p.kind, ta);
        for (size_t i = 2; i + 1 < p.xy.size(); i += 2)
            n->drawSegment({p.xy[i - 2], p.xy[i - 1]}, {p.xy[i], p.xy[i + 1]}, 0.8f, c);
    }
    //    ...and a tick at each anchor, which is where its tail begins.
    for (const Death& d : g_deaths) {
        if (d.anchorT < 0 || d.anchorX <= 0.f) continue;
        n->drawSegment({d.anchorX, d.y - 16.f}, {d.anchorX, d.y + 16.f}, 1.2f,
                       ccColor4F{0.35f, 0.60f, 1.00f, 0.45f});
    }
    for (const Fixup& x : g_fixups) {
        const ccColor4F c = x.sameDeath ? ccColor4F{0.70f, 0.70f, 0.70f, 0.70f}
                            : x.kill    ? ccColor4F{1.00f, 0.35f, 0.85f, 0.85f}
                                        : ccColor4F{1.00f, 0.80f, 0.20f, 0.75f};
        n->drawDot({x.x, x.y}, 3.5f, c);
    }
    // 5. the deaths themselves, on top, coloured by how the round was scored
    for (const Death& d : g_deaths) {
        const ccColor4F c = kindColor(d.kind, 0.90f);
        n->drawSegment({d.x, d.y - 26.f}, {d.x, d.y + 26.f}, 1.4f, kindColor(d.kind, 0.45f));
        n->drawDot({d.x, d.y}, 5.f, c);
    }
    for (const Hot& h : g_hot) {
        if (h.deaths <= 0) continue;
        char cb[16];
        snprintf(cb, sizeof(cb), "%d", h.deaths);
        auto* lbl = CCLabelBMFont::create(cb, "bigFont.fnt");
        if (!lbl) continue;
        lbl->setAnchorPoint({0.5f, 0.f});
        lbl->setScale(0.45f);
        lbl->setPosition({h.x0 + kBucket * 0.5f, h.yHi + 44.f});
        const ccColor4F c = kindColor(h.dominant, 1.f);
        lbl->setColor({(GLubyte)(c.r * 255.f), (GLubyte)(c.g * 255.f), (GLubyte)(c.b * 255.f)});
        lbl->setOpacity(235);
        counts->addChild(lbl);
    }
}

inline void logScreenMetrics(cocos2d::CCNode* parent) {
    using namespace cocos2d;
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    auto* d = CCDirector::sharedDirector();
    const CCSize win = d->getWinSize();
    const CCSize vis = d->getVisibleSize();
    const CCPoint vo = d->getVisibleOrigin();
    CCSize frame{0.f, 0.f};
    float sx = 0.f, sy = 0.f;
    if (auto* v = CCEGLView::sharedOpenGLView()) {
        frame = v->getFrameSize();
        sx = v->getScaleX();
        sy = v->getScaleY();
    }
    CCPoint bl{0.f, 0.f}, tr{0.f, 0.f};
    float pscale = 0.f;
    if (parent) {
        bl = parent->convertToNodeSpace({0.f, 0.f});
        tr = parent->convertToNodeSpace({win.width, win.height});
        pscale = parent->getScale();
    }
    float cw = 0.f, ch = 0.f;
#ifdef GEODE_IS_WINDOWS
    RECT rc{};
    if (HWND h = GetActiveWindow())
        if (GetClientRect(h, &rc)) {
            cw = (float)(rc.right - rc.left);
            ch = (float)(rc.bottom - rc.top);
        }
#endif
    char b[448];
    snprintf(b, sizeof(b),
             "itermap: win=%.1fx%.1f visOrigin=%.1f,%.1f visSize=%.1fx%.1f frame=%.1fx%.1f "
             "viewScale=%.3f,%.3f parentScale=%.3f nodeRect=%.1f,%.1f..%.1f,%.1f "
             "client=%.0fx%.0f",
             win.width, win.height, vo.x, vo.y, vis.width, vis.height,
             frame.width, frame.height, sx, sy, pscale, bl.x, bl.y, tr.x, tr.y, cw, ch);
    writeResult(b);
}

inline cocos2d::CCRect stripBox() {
    using namespace cocos2d;
    const CCSize v = CCDirector::sharedDirector()->getWinSize();
    const float m = v.width * 0.015f;              // margin off both edges
    const float w = v.width * 0.46f;
    const float h = v.height * 0.055f;
    return CCRect(v.width - w - m, m, w, h);
}

inline void drawStrip(cocos2d::CCNode* parent) {
    using namespace cocos2d;
    auto* n = drawNode(parent, STRIP_TAG, "itermap-strip"_spr, 1 << 20);
    if (!n) return;
    const bool on = stripVisible();
    n->setVisible(on);
    if (!on) { g_stripW = 0.f; return; }   // not drawn = not clickable
    const CCRect box = stripBox();
    const float kStripW = box.size.width, kStripH = box.size.height;
    g_stripX = box.origin.x; g_stripY = box.origin.y;
    g_stripW = kStripW;      g_stripH = kStripH;
    g_barTicks = barTicks();
    std::lock_guard<std::mutex> lk(g_mu);
    rebuildHotLocked();
    n->setPosition({box.origin.x, box.origin.y});
    n->clear();
    const float thin = kStripW * 0.004f;
    const float pad = kStripH * 0.07f;
    if (g_labelH > 0.f) {
        const float lp = kStripH * 0.25f;
        fillRect(n, kStripW - g_labelW - lp, kStripH,
                 kStripW + lp * 0.5f, kStripH + kStripH * 0.25f + g_labelH + lp,
                 ccColor4F{0.f, 0.f, 0.f, 0.78f});
    }
    // the trough
    fillRect(n, 0.f, 0.f, kStripW, kStripH, ccColor4F{0.f, 0.f, 0.f, 0.78f});
    fillRect(n, 0.f, 0.f, kStripW, pad * 0.4f, ccColor4F{1.f, 1.f, 1.f, 0.25f});
    const float st = kStripW / (float)std::max(1LL, g_barTicks);   // ticks -> bar
    if (mapVisible()) {
        const long long tb = std::max(1LL, g_barTicks / 200);
        std::map<long long, std::pair<int, int>> col;   // bucket -> (deaths, dominant kind)
        std::map<long long, int[KindCount]> kinds;
        for (const Death& d : g_deaths) {
            const long long b = d.tick / tb;
            ++col[b].first;
            if (d.kind >= 0 && d.kind < KindCount) ++kinds[b][d.kind];
        }
        int most = 0;
        for (auto& kv : col) if (kv.second.first > most) most = kv.second.first;
        for (auto& kv : col) {
            int dom = KindRewind;
            auto it = kinds.find(kv.first);
            if (it != kinds.end())
                for (int k = 0; k < KindCount; ++k)
                    if (it->second[k] > it->second[dom]) dom = k;
            const float f = most > 0 ? (float)kv.second.first / (float)most : 0.f;
            const float x0 = (float)(kv.first * tb) * st;
            fillRect(n, x0, pad, x0 + std::max(thin, (float)tb * st),
                     pad + (kStripH - pad * 3.f) * f, kindColor(dom, 0.85f));
        }
        for (const Fixup& x : g_fixups) {
            const float fx = (float)x.tick * st;
            fillRect(n, fx, kStripH - pad, fx + thin, kStripH,
                     x.sameDeath ? ccColor4F{0.70f, 0.70f, 0.70f, 0.9f}
                     : x.kill    ? ccColor4F{1.00f, 0.35f, 0.85f, 0.9f}
                                 : ccColor4F{1.00f, 0.80f, 0.20f, 0.9f});
        }
    }
    if (g_dragging) {
        const float gx = g_dragFrac * kStripW;
        fillRect(n, gx - thin, -pad * 2.f, gx + thin, kStripH + pad * 2.f,
                 ccColor4F{1.00f, 0.75f, 0.15f, 0.95f});
    }
    const long long headT = seeking() ? g_seekBarTick : g_nowTick;
    const float hx = std::min(kStripW, std::max(0.f, (float)headT * st));
    fillRect(n, hx - thin * 0.5f, -pad, hx + thin * 0.5f, kStripH + pad,
             ccColor4F{1.f, 1.f, 1.f, 0.95f});
}

inline void drawCover(cocos2d::CCNode* parent, bool forward) {
    using namespace cocos2d;
    auto* n = drawNode(parent, COVER_TAG, "seek-cover"_spr, (1 << 20) - 1);
    if (!n) return;
    const bool on = (seeking() || g_dragging) && barVisible();
    n->setVisible(on);
    if (!on) return;
    const CCSize v = CCDirector::sharedDirector()->getWinSize();
    n->setPosition({0.f, 0.f});
    n->clear();
    fillRect(n, 0.f, 0.f, v.width, v.height, ccColor4F{0.f, 0.f, 0.f, 1.f});
    // Three chevrons, centred, pointing the way the level is about to move.
    const float s = v.height * 0.055f;                 // one chevron's half-height
    const float cy = v.height * 0.5f;
    const float dir = forward ? 1.f : -1.f;
    for (int i = 0; i < 3; ++i) {
        const float cx = v.width * 0.5f + dir * (float)(i - 1) * s * 1.5f;
        CCPoint tri[3] = {{cx - dir * s * 0.55f, cy + s},
                          {cx + dir * s * 0.55f, cy},
                          {cx - dir * s * 0.55f, cy - s}};
        n->drawPolygon(tri, 3, ccColor4F{1.f, 1.f, 1.f, 0.30f + 0.22f * (float)i},
                       0.f, ccColor4F{0.f, 0.f, 0.f, 0.f});
    }
}

inline void summary(char* out, size_t cap, float px, float levelLen) {
    if (cap == 0) return;
    std::lock_guard<std::mutex> lk(g_mu);
    const long long total = std::max(1LL, g_barTicks);
    int n = snprintf(out, cap, "t=%lld / %lld  (%.0f%%)   x=%.0f\n",
                     (long long)g_nowTick, total,
                     100.0 * (double)g_nowTick / (double)total, (double)px);
    if (n < 0 || (size_t)n >= cap) return;
    if (!mapWanted()) return;       // the bar alone says where you are and stops there
    if (emptyLocked()) {
        snprintf(out + n, cap - n,
                 "ITERATION MAP  nothing recorded for this level\n"
                 "  (solve it once, or build a map from a log with py/itermap_from_log.py)");
        return;
    }
    rebuildHotLocked();
    const Hot* top = hottestLocked();
    char worst[96] = "";
    if (top && g_rounds > 0)
        snprintf(worst, sizeof(worst), "   worst x=%.0f (%d %s, %.0f%%)",
                 (double)(top->x0 + kBucket * 0.5f), top->deaths, kindName(top->dominant),
                 100.0 * top->deaths / (double)std::max(1, (int)g_deaths.size()));
    const char* state = (g_mapLevel < 0) ? "   [in progress]"
                                         : (g_cleared ? "" : "   [did not clear]");
    const char* tails = (!g_deaths.empty() && g_paths.empty())
                        ? "   [no tails in this map - rebuilt from a log]" : "";
    const size_t searchFx = searchFixupsLocked();
    n += snprintf(out + n, cap - n, "ITERATION MAP  %d rounds  %zu deaths  %zu fixups%s%s%s\n",
                  g_rounds, g_deaths.size(), g_fixups.size(), worst, state, tails);
    if (n < 0 || (size_t)n >= cap) return;
    if (!g_fixups.empty()) {
        n += snprintf(out + n, cap - n,
                      "  fixups: %zu where the model was wrong, %zu a search sent (same death)\n",
                      g_fixups.size() - searchFx, searchFx);
        if (n < 0 || (size_t)n >= cap) return;
    }
    const Hot* here = atLocked(px);
    if (here) {
        n += snprintf(out + n, cap - n,
                      "  here x=%.0f: %d death%s (%s), %d fixup%s (%d search)  (rounds %d-%d)\n",
                      (double)px, here->deaths, here->deaths == 1 ? "" : "s",
                      kindName(here->dominant),
                      here->fixups, here->fixups == 1 ? "" : "s", here->searchFixups,
                      here->firstIter, here->lastIter);
    } else {
        n += snprintf(out + n, cap - n,
                      "  here x=%.0f: clear on the first plan   (%.0f%% of the level)\n",
                      (double)px, levelLen > 1.f ? 100.0 * px / levelLen : 0.0);
    }
    if (n < 0 || (size_t)n >= cap) return;
    snprintf(out + n, cap - n,
             "  deaths: green deeper / cyan followed / violet forced / red rewound / pink wedged\n"
             "  fixups: amber model / magenta kill / grey search (same death), purple veto\n"
             "  %s",
             barVisible() ? "click the strip to seek" : "live: the solve is writing this");
}

inline void drawLabel(cocos2d::CCNode* parent, float px, float levelLen) {
    using namespace cocos2d;
    auto* lbl = static_cast<CCLabelBMFont*>(parent->getChildByTag(LABEL_TAG));
    if (!stripVisible()) {
        if (lbl) lbl->setVisible(false);
        g_labelW = g_labelH = 0.f;      // ...so the strip stops painting a panel for it
        return;
    }
    if (!lbl) {
        lbl = CCLabelBMFont::create("", "chatFont.fnt");
        if (!lbl) return;
        lbl->setTag(LABEL_TAG);
        lbl->setID("itermap-readout"_spr);
        lbl->setAnchorPoint({1.f, 0.f});
        lbl->setScale(0.5f);
        lbl->setZOrder((1 << 20) + 2);
        lbl->setColor({255, 255, 255});
        lbl->setOpacity(255);
        parent->addChild(lbl);
    }
    lbl->setVisible(true);
    const CCRect box = stripBox();
    lbl->setPosition({box.origin.x + box.size.width,
                      box.origin.y + box.size.height + box.size.height * 0.25f});
    g_labelW = lbl->getContentSize().width * lbl->getScale();
    g_labelH = lbl->getContentSize().height * lbl->getScale();
    // setString rebuilds every glyph sprite, so it is throttled the same way the session HUD is.
    static auto s_last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last).count() < 120
        && *lbl->getString())
        return;
    s_last = now;
    char b[512];
    summary(b, sizeof(b), px, levelLen);
    lbl->setString(b);
}

// ---- clicking the strip ----

inline bool g_seekRevive = false;

inline void dismissEndScreen() {
    using namespace cocos2d;
    auto* pl = PlayLayer::get();
    if (!pl) return;
    CCNode* hosts[2] = {pl, pl->getParent()};
    for (CCNode* host : hosts) {
        if (!host) continue;
        CCArray* kids = host->getChildren();
        if (!kids) continue;
        // Collected first: removing while walking the array is what invalidates it.
        std::vector<CCNode*> doomed;
        for (unsigned i = 0; i < kids->count(); ++i) {
            auto* n = static_cast<CCNode*>(kids->objectAtIndex(i));
            if (geode::cast::typeinfo_cast<EndLevelLayer*>(n)) doomed.push_back(n);
        }
        for (CCNode* n : doomed) n->removeFromParentAndCleanup(true);
        if (!doomed.empty())
            writeResult("itermap: removed " + std::to_string(doomed.size())
                        + " end-of-level screen(s) before restarting");
    }
}

// Seek to a physics tick. The one entry point: the bar and the keys both ask in ticks now.
inline void seekToTick(long long targetTick) {
    if (targetTick < 0) targetTick = 0;
    g_seekTick = targetTick;
    g_seekForward = (targetTick >= g_nowTick);
    g_seekBarTick = targetTick;
    g_seekRestart = (targetTick <= g_nowTick) || (g_cpAhead && g_cpAhead(g_nowTick, targetTick));
    if (g_sessionOver) g_seekRevive = true;
    g_seekT0 = std::chrono::steady_clock::now();
    g_seekFrames = g_seekBatches = g_seekVisCalls = 0;
    g_seekVisMs = g_seekDrawMs = 0.0;
    g_seekBlindNow = g_seekBlindDone = false;
    g_seekBlindMs = 0.0;
    char b[192];
    snprintf(b, sizeof(b), "itermap: seek to t=%lld from t=%lld (%s%s; speed stays %gx)",
             (long long)targetTick, (long long)g_nowTick,
             g_seekRestart ? "restarting the level" : "fast-forwarding",
             g_seekRevive ? ", reviving the replay" : "", (double)g_watchSpeed);
    writeResult(b);
}

inline void endSeek(const char* why) {
    if (!seeking()) return;
    char b[320];
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_seekT0).count();
    snprintf(b, sizeof(b),
             "itermap: seek ended (%s) at t=%lld, back to %gx (%.0f ms, %lld frames, %lld batches, "
             "visibility %lld calls %.0f ms, draw %.0f ms)",
             why, (long long)g_nowTick, (double)g_watchSpeed, ms, g_seekFrames, g_seekBatches,
             g_seekVisCalls, g_seekVisMs, g_seekDrawMs);
    g_seekTick = -1;
    g_seekRestart = false;
    g_seekBlindNow = false;
    writeResult(b);
    if (g_seekBlindDone) {
        snprintf(b, sizeof(b), "itermap: the seek ran screen off for its first %.0f ms (cfg seekblind)",
                 g_seekBlindMs);
        writeResult(b);
    }
    if (g_cpLine && g_seekCpEvery > 0) writeResult(g_cpLine());
}

inline int seekLoops(float dt) {
    if (g_seekRestart || g_seekTick < 0) return 0;
    float ticksPerLoop = dt * 240.f;
    if (!(ticksPerLoop > 0.5f)) ticksPerLoop = 1.f;    // also catches NaN
    const long long remT = g_seekTick - g_nowTick;
    if (remT <= 0) return 0;
    const int n = (int)((float)remT / ticksPerLoop);
    return n < 1 ? 0 : (n > kSeekLoopsMax ? kSeekLoopsMax : n);
}

inline bool seekArrived(bool dead, float dt) {
    if (g_seekRestart || g_seekTick < 0) return false;
    if (dead) return true;
    const long long slack = (long long)std::max(1.f, dt * 240.f);
    return g_nowTick + slack > g_seekTick;
}

constexpr long long kSeekStepTicks = 5 * 240;
inline long long seekStep(int repeats) {
    long long mult = 1 + repeats / 6;
    if (mult > 6) mult = 6;
    return kSeekStepTicks * mult;
}

inline void seekBy(long long deltaTicks) {
    long long from = (g_seekTick >= 0) ? g_seekTick : g_nowTick;
    if (deltaTicks > 0 && from < g_nowTick) from = g_nowTick;
    g_seekStepTo = -1;
    seekToTick(from + deltaTicks);
}

inline void stepBack(long long ticks) {
    const long long want = g_nowTick - ticks;
    g_seekStepTo = want < 0 ? 0 : want;
    g_seekRepause = true;
    seekToTick(g_seekStepTo - kStepUndershoot);
    char b[224];
    snprintf(b, sizeof(b),
             "itermap: [stepback] from t=%lld by %lld -> want t=%lld, seek t=%lld, restart=%d",
             (long long)g_nowTick, ticks, (long long)g_seekStepTo,
             (long long)g_seekTick, (int)g_seekRestart);
    writeResult(b);
}

// Whether the bar can be driven at all right now.
inline bool barLive() {
    if (!barVisible() || g_stripW <= 0.f || g_barTicks <= 1) return false;
    if (solvingNow()) return false;         // the loop owns the level while it is solving
    auto* pl = PlayLayer::get();
    return pl && pl->m_player1;
}

inline bool onBar(float x, float y) {
    const float slack = g_stripH * 0.9f;
    return x >= g_stripX - slack && x <= g_stripX + g_stripW + slack
        && y >= g_stripY - slack && y <= g_stripY + g_stripH + slack;
}

inline float barFrac(float x) {
    float f = (x - g_stripX) / g_stripW;
    if (f < 0.f) f = 0.f;
    if (f > 1.f) f = 1.f;
    return f;
}

class StripTouch : public cocos2d::CCLayer {
public:
    static StripTouch* create() {
        auto* r = new StripTouch();
        if (r && r->init()) { r->autorelease(); return r; }
        CC_SAFE_DELETE(r);
        return nullptr;
    }
    bool init() {
        if (!CCLayer::init()) return false;
        this->setTouchEnabled(true);
        return true;
    }
    void registerWithTouchDispatcher() override {
        cocos2d::CCDirector::sharedDirector()->getTouchDispatcher()
            ->addTargetedDelegate(this, -0x7000, true);
    }
    bool ccTouchBegan(cocos2d::CCTouch* t, cocos2d::CCEvent*) override {
        if (!t) return false;
        const cocos2d::CCPoint p = t->getLocation();
        const bool hit = onBar(p.x, p.y);
        if (hit && !barLive()) {
            auto* pl = PlayLayer::get();
            char b[192];
            snprintf(b, sizeof(b), "itermap: press on the bar refused - vis=%d w=%.1f len=%.1f "
                     "solving=%d play=%d player=%d",
                     (int)barVisible(), (double)g_stripW, (double)g_barTicks,
                     (int)solvingNow(), (int)(pl != nullptr),
                     (int)(pl && pl->m_player1));
            writeResult(b);
        }
        if (!hit || !barLive()) return false;
        g_dragging = true;
        g_dragFrac = barFrac(p.x);
        return true;
    }
    void ccTouchMoved(cocos2d::CCTouch* t, cocos2d::CCEvent*) override {
        if (!t || !g_dragging) return;
        g_dragFrac = barFrac(t->getLocation().x);
    }
    void ccTouchEnded(cocos2d::CCTouch* t, cocos2d::CCEvent*) override {
        if (!g_dragging) return;
        if (t) g_dragFrac = barFrac(t->getLocation().x);
        g_dragging = false;
        if (!barLive()) return;
        auto* pl = PlayLayer::get();
        seekToTick((long long)(g_dragFrac * (float)g_barTicks));
    }
    void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override {
        g_dragging = false;   // no seek: a cancelled drag asked for nothing
    }
};

// ---- one call from the overlay pass ----
inline void draw(cocos2d::CCNode* parent, PlayLayer* pl) {
    if (!parent) return;
    const float levelLen = pl ? pl->m_levelLength : 0.f;
    const float px = (pl && pl->m_player1) ? pl->m_player1->getPositionX() : 0.f;
    if (mapWanted() && pl && pl->m_level) ensureLoaded(pl->m_level->m_levelID.value());
    if (mapWanted()) logScreenMetrics(parent);
    if (barVisible() && !parent->getChildByTag(TOUCH_TAG)) {
        if (auto* tl = StripTouch::create()) {
            tl->setTag(TOUCH_TAG);
            tl->setID("seek-bar-touch"_spr);
            parent->addChild(tl, (1 << 20) + 1);
        }
    }
    drawCover(parent, g_dragging ? (g_dragFrac * (float)g_barTicks > (float)g_nowTick) : g_seekForward);
    drawLabel(parent, px, levelLen);
    drawStrip(parent);
    drawWorld(pl ? pl->m_objectLayer : nullptr);
}

inline void toggle(int levelId) {
    ensureLoaded(levelId);
    g_mapPref = mapWanted() ? 0 : 1;
    char b[192];
    {
        std::lock_guard<std::mutex> lk(g_mu);
        ++g_generation;   // force a rebuild: the parent is unchanged but the visibility is not
        snprintf(b, sizeof(b), "itermap: %s (level %d, %zu deaths, %zu fixups, %d rounds)",
                 mapWanted() ? "shown" : "hidden", levelId, g_deaths.size(), g_fixups.size(),
                 g_rounds);
    }
    writeResult(b);
}
}  // namespace itermap
}  // namespace p1
