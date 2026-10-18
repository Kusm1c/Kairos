#pragma once
#include "dp/reset.hpp"
#include "dp/clearance.hpp"
#include "dp/refwatch.hpp"
#include "dp/search_census.hpp"
#include "dp/model.hpp"
#include "dp/search_dp.hpp"
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h>   // _mm_getcsr: see the fpenv line at the top of cliMain
#endif

namespace dp {
// (PlanEdge, planEdges: search_dp.hpp)

inline double g_prepMark[6] = {-1, -1, -1, -1, -1, -1};

struct WalkCheckpoint {
    size_t i = 0;                        // lvl[0..i) walked: `s` is the state after tick t0 + i
    State s{};
    uint64_t objRefs[12] = {};           // s's Obj pointers (Model::encodeObj), as --ladderprefix
    std::vector<double> carry;           // frame 0's moving geometry (Dynamics::saveCarry); a walk
                                         // through gameplay rotation is not taken up at all
    std::vector<double> autoLockLastX;   // the one autonomous-trigger field the walk writes
    // the walk's own accumulators, which the lines after it and g_outcome read
    long long resimDead = 0, resimTicks = 0, dieRecon = 0;
    int resimFirst = -1, resimLast = -1, resimUid = -1, resimFrame = -1;
    const char* resimWhy = nullptr;
    float resimPX = 0.f, resimObjX = 0.f, resimObjY = 0.f;
    TouchMask resimTrig{};
    std::vector<std::pair<const char*, long long>> dieReconWhy;
    const char* deadWhy = "";
    uint64_t deadObjRef = 0;
    float deadCx = 0.f, deadCy = 0.f;
    size_t traceLen = 0;                // bytes of the trace rows through tick t0 + i
    uint64_t sticky = 0;                 // frame 0's turnedBox latch here (Dynamics::foldSticky)
    long long fixupHitsWalk = 0;         // g_fixupHits the walk added up to here (the search's
    bool preBtnSeen = false;
    double preBtnY = 0.0;
    int bandTrackCam = -1;
};
struct WalkResumeCache {
    bool active = false;                 // --walkresume, inside a ladder (cliMain) and only there
    long long t0 = -1;
    uint64_t worldSig = 0;               // the world the kept walk started from
    std::vector<uint8_t> lvl;            // the plan it walked
    std::string trace;                   // its trace rows (the header is written apart)
    std::vector<uint8_t> modeAt;
    std::vector<WalkCheckpoint> cps;
};
inline WalkResumeCache g_wrc;
inline int g_walkResumeEvery = 256;
inline bool g_walkResumeCheck = false;
inline uint64_t walkFnv(uint64_t h, const void* p, size_t n) {
    const unsigned char* b = static_cast<const unsigned char*>(p);
    for (size_t k = 0; k < n; ++k) { h ^= b[k]; h *= 1099511628211ull; }
    return h;
}
inline uint64_t walkWorldSig(const Model& model, long long t0, const State& init, float rDxF, bool& ok) {
    uint64_t h = 1469598103934665603ull;
    h = walkFnv(h, &t0, sizeof t0);
    h = walkFnv(h, &rDxF, sizeof rDxF);
    State z = init;
    uint64_t refs[12];
    refs[0] = model.encodeObj(z.snapObj, ok);
    refs[1] = model.encodeObj(z.usedOrb, ok);
    refs[2] = model.encodeObj(z.snapObj2, ok);
    refs[3] = model.encodeObj(z.usedOrb2, ok);
    for (int k = 0; k < 4; ++k) refs[4 + k] = model.encodeObj(z.usedPad[k], ok);
    for (int k = 0; k < 4; ++k) refs[8 + k] = model.encodeObj(z.usedPad2[k], ok);
    z.snapObj = z.usedOrb = z.snapObj2 = z.usedOrb2 = nullptr;
    for (int k = 0; k < 4; ++k) z.usedPad[k] = z.usedPad2[k] = nullptr;
    h = walkFnv(h, &z, sizeof z);
    h = walkFnv(h, refs, sizeof refs);
    for (const AutoTrig& A : g_autoTrig) {
        h = walkFnv(h, &A.fireT, sizeof A.fireT);
        h = walkFnv(h, &A.fireX, sizeof A.fireX);
        h = walkFnv(h, &A.fireSpreadT, sizeof A.fireSpreadT);
        h = walkFnv(h, &A.lockLastX, sizeof A.lockLastX);
    }
    for (const RotTrig& R : g_rotTrig) {
        h = walkFnv(h, &R.firedT, sizeof R.firedT);
        h = walkFnv(h, &R.revT, sizeof R.revT);
    }
    h = walkFnv(h, &g_rjJoinT, sizeof g_rjJoinT);
    const int bandCam = g_bandTrackCam.load();
    h = walkFnv(h, &bandCam, sizeof bandCam);   // computed yet or not (bands.hpp)
    for (int f = 0; f < 4; ++f) {
        const uint8_t on = model.frameBound(f) ? 1 : 0;
        h = walkFnv(h, &on, 1);
        if (on) model.levelOf(f).dyn.foldSticky(h);
    }
    return h;
}

struct LevelTextCache {
    std::string text;
    bool haveTypes = false, havePos = false, haveSpawnX = false;
    std::unordered_map<int, int> types;
    std::unordered_map<int, std::pair<double, double>> pos;
    std::unordered_map<int, double> spawnX;
};
// (namespace scope, not a function's static, so releaseCrossCallCaches below can empty it)
inline LevelTextCache g_levelTextCache;
inline LevelTextCache& levelTextFor(const std::string& text) {
    LevelTextCache& c = g_levelTextCache;
    if (c.text.size() != text.size() || c.text != text) {
        c = LevelTextCache{};
        c.text = text;
    }
    return c;
}
struct GroupFileParse {
    std::string path, bytes;
    std::string sig;      // the caller's signature of these bytes (g_fileSigHint), "" when none
    GroupTimeline one;
    long long end = -1;
    std::unordered_map<int, uint8_t> init;
    std::string line;     // the line its load printed
};
struct GroupLayersCache {
    bool valid = false;
    std::vector<GroupFileParse> files;   // the last call's files, in its order
    bool holdDeath = false;
    GroupTimeline merged;
    std::unordered_map<int, uint8_t> init;
    std::string report;   // the lines the loads and overlays printed, in order
    long long endT = -1;  // the largest `end,<tick>` among the files (g_groupsEndT)
};
inline std::unordered_map<std::string, std::string> g_fileSigHint;
inline bool g_groupSigCheck = false;
inline unsigned long long g_groupLayersGen = 0;
inline GroupLayersCache g_groupLayersCache;   // (namespace scope: releaseCrossCallCaches empties it)
inline const GroupTimeline& groupLayersFor(const std::vector<std::string>& paths) {
    GroupLayersCache& c = g_groupLayersCache;
    static const GroupTimeline kNone;
    g_groupInit.clear();
    g_groupsEndT = -1;
    if (paths.empty()) return kNone;
    std::vector<std::string> hint(paths.size());
    for (size_t i = 0; i < paths.size(); ++i) {
        const auto h = g_fileSigHint.find(paths[i]);
        if (h != g_fileSigHint.end()) hint[i] = h->second;
    }
    bool sigHit = c.valid && c.holdDeath == g_groupHoldDeath && c.files.size() == paths.size();
    for (size_t i = 0; i < paths.size() && sigHit; ++i)
        sigHit = !hint[i].empty() && c.files[i].path == paths[i] && c.files[i].sig == hint[i];
    if (sigHit && !g_groupSigCheck) {
        std::fputs(c.report.c_str(), stdout);
        g_groupInit = c.init;
        g_groupsEndT = c.endT;
        return c.merged;
    }
    auto cachedBySig = [&](size_t i) -> GroupFileParse* {
        if (hint[i].empty() || g_groupSigCheck) return nullptr;
        for (GroupFileParse& old : c.files)
            if (old.path == paths[i] && old.sig == hint[i]) return &old;
        return nullptr;
    };
    std::vector<std::string> bytes(paths.size());
    std::vector<uint8_t> bySig(paths.size(), 0);
    bool readable = true;
    for (size_t i = 0; i < paths.size() && readable; ++i) {
        if (cachedBySig(i)) { bySig[i] = 1; continue; }
        readable = readFileBytes(paths[i], bytes[i]);
    }
    auto sameFiles = [&]() {
        if (c.files.size() != paths.size()) return false;
        for (size_t i = 0; i < paths.size(); ++i) {
            if (c.files[i].path != paths[i]) return false;
            if (bySig[i] ? c.files[i].sig != hint[i] : c.files[i].bytes != bytes[i]) return false;
        }
        return true;
    };
    const bool byteHit = readable && c.valid && c.holdDeath == g_groupHoldDeath && sameFiles();
    if (sigHit)
        std::printf("groupsigcheck: %s (%s)\n",
                    byteHit ? "signature hit, bytes agree" : "MISMATCH - a signature hit whose bytes differ",
                    paths[0].c_str());
    if (byteHit) {
        for (size_t i = 0; i < paths.size(); ++i)
            if (!hint[i].empty()) c.files[i].sig = hint[i];
        std::fputs(c.report.c_str(), stdout);
        g_groupInit = c.init;
        g_groupsEndT = c.endT;
        return c.merged;
    }
    // A miss: the loop this replaces, printing as it goes -- each file's line, then its overlay's.
    ++g_groupLayersGen;
    std::vector<GroupFileParse> files(paths.size());
    GroupTimeline gt;
    std::string report;
    for (size_t gi = 0; gi < paths.size(); ++gi) {
        GroupFileParse& f = files[gi];
        GroupFileParse* kept = nullptr;
        if (readable && bySig[gi]) {
            kept = cachedBySig(gi);
        } else if (readable) {
            for (GroupFileParse& old : c.files)
                if (old.path == paths[gi] && !old.bytes.empty() && old.bytes == bytes[gi]) {
                    kept = &old;
                    break;
                }
        }
        if (kept) {   // the same bytes as last time: the same parse, and the line it printed
            f = std::move(*kept);
            f.sig = hint[gi];
            kept->path.clear();   // taken; a path given twice parses its second copy anew
            std::fputs(f.line.c_str(), stdout);
        } else {
            f.path = paths[gi];
            f.sig = hint[gi];
            if (readable) {
                f.bytes = std::move(bytes[gi]);
                f.one = parseGroupTimeline(f.bytes, paths[gi], &f.end, &f.init);
            } else {  // an unreadable file says so on stderr and loads as nothing
                f.one = loadGroupTimeline(paths[gi], &f.end, &f.init);
            }
            long long rows = 0;
            for (const auto& kv : f.one) rows += (long long)kv.second.size();
            char b[768];
            std::snprintf(b, sizeof b, "groups: %lld samples for %zu objects (%s)\n", rows,
                          f.one.size(), paths[gi].c_str());
            f.line = b;
        }
        if (!f.sig.empty() && !g_groupSigCheck) std::string().swap(f.bytes);
        report += f.line;
        if (g_groupInit.empty()) g_groupInit = f.init;
        if (gi == 0) {
            gt = f.one;
        } else {
            std::string ov;
            overlayGroupTimeline(gt, f.one, g_groupHoldDeath && gi + 1 == files.size(), f.end,
                                 &ov);
            std::fputs(ov.c_str(), stdout);
            report += ov;
        }
    }
    long long endT = -1;
    for (const GroupFileParse& f : files) endT = std::max(endT, f.end);
    g_groupsEndT = endT;
    c = GroupLayersCache{};
    c.merged = std::move(gt);
    c.endT = endT;
    if (readable) {   // an unreadable file is not a state worth remembering
        c.valid = true;
        c.files = std::move(files);
        c.holdDeath = g_groupHoldDeath;
        c.init = g_groupInit;
        c.report = std::move(report);
    }
    return c.merged;
}

struct LadderLevelCache {
    unsigned long long ladder = 0;   // the ladder it was built in; 0 = empty
    std::vector<std::string> args;   // that attempt's arguments without --capmap / --gridmap
    unsigned long long groupsGen = 0;
    Level level;
    LoadLevelWrites writes;
    std::string printed;
};
inline unsigned long long g_ladder = 0;   // the ladder in progress (cliMain); 0 = none
inline bool g_declineToLadder = false;
inline bool g_declinedToLadder = false;
// (g_ladderBestT, g_ladderStop: search_dp.hpp)
inline LadderLevelCache g_ladderLevel;
inline bool g_levelCacheCheck = false;
struct LevelHash {
    uint64_t h = 1469598103934665603ULL;
    void bytes(const void* p, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    }
};
template <class T> struct LvhAlwaysFalse : std::false_type {};
template <class T> inline void lvh(LevelHash& H, const T& v) {
    static_assert(std::is_trivially_copyable<T>::value || LvhAlwaysFalse<T>::value,
                  "levelHash: a member type that is neither plain data nor a vector/string");
    H.bytes(&v, sizeof v);
}
inline void lvh(LevelHash& H, const std::string& s) {
    const size_t n = s.size();
    H.bytes(&n, sizeof n);
    H.bytes(s.data(), n);
}
template <class T> inline void lvh(LevelHash& H, const std::vector<T>& v) {
    const size_t n = v.size();
    H.bytes(&n, sizeof n);
    if constexpr (std::is_trivially_copyable<T>::value) {
        if (n) H.bytes(v.data(), n * sizeof(T));
    } else {
        for (const T& x : v) lvh(H, x);
    }
}
inline void levelHashDyn(LevelHash& H, const Dynamics& d) {
    lvh(H, d.objs);
    lvh(H, d.samples);
    lvh(H, d.everRot);
    lvh(H, d.obbBc0);
    lvh(H, d.obbBs0);
    lvh(H, d.shiftSaid);
    lvh(H, d.cur);
    lvh(H, d.bucket);
    lvh(H, d.formula);
    lvh(H, d.prevCy);
    lvh(H, d.lastStep1);
    lvh(H, d.baseCy);
    lvh(H, d.baseSy0);
    lvh(H, d.baseSy1);
    lvh(H, d.on);
    lvh(H, d.onEv);
    lvh(H, d.onInit);
    lvh(H, d.offBox);
    lvh(H, d.trigMask);
    lvh(H, d.recAuto);
    lvh(H, d.trigDx);
    lvh(H, d.trigDy);
    lvh(H, d.trigDur);
    lvh(H, d.trigEase);
    lvh(H, d.trigErate);
    lvh(H, d.trigRecFire);
    lvh(H, d.boxRecEntry);
    lvh(H, d.recLag);
    lvh(H, d.autoLag);
    lvh(H, d.autoClosed);
    lvh(H, d.autoAnchor);
    lvh(H, d.anchorUnrec);
    if (g_recAnchorX) lvh(H, d.recAnchorX);   // only under the flag: the off arm hashes as before
    if (g_areaMove) {                           // the same
        lvh(H, d.areaOf);
        lvh(H, d.areaBaseX);
        lvh(H, d.areaBaseY);
        lvh(H, d.areaBaseHa);
        lvh(H, d.areaBaseHb);
        lvh(H, d.areaBaseRot);
        lvh(H, d.areaArmed);
    }
    if (g_tapDisarm) {                          // the same
        lvh(H, d.disarmEv);
        lvh(H, d.disarmInit);
    }
    if (g_boxArm) lvh(H, d.armDep);             // the same
    if (g_rotObbLatch) lvh(H, d.obbLatchT);     // the same
    if (g_recEndTail) {                         // the same
        lvh(H, d.tailOnEv);
        lvh(H, d.tailOnBox);
        lvh(H, d.tailRecEnd);
        for (const Dynamics::RigTail& r : d.rigTail) {
            lvh(H, r.t0); lvh(H, r.lockA); lvh(H, r.lockB);
            lvh(H, r.x); lvh(H, r.y); lvh(H, r.r);
        }
    }
    lvh(H, d.recSelfFire);
    lvh(H, d.autoReach);
    lvh(H, d.autoDx);
    lvh(H, d.autoDy);
    lvh(H, d.autoDur);
    lvh(H, d.autoEase);
    lvh(H, d.autoErate);
    lvh(H, d.autoParts);
    lvh(H, d.rotSplit);
    lvh(H, d.touchParts);
    lvh(H, d.autoLock);
    lvh(H, d.autoLockTrig);
    lvh(H, d.touchLock);
    lvh(H, d.anyTrig);
    lvh(H, d.anyAuto);
    lvh(H, d.lastT);
    lvh(H, d.seekEpoch);
    lvh(H, d.pureEpoch);
    lvh(H, d.pureT);
    lvh(H, d.pureFire);
    lvh(H, d.byBucket);
    lvh(H, d.anyUidM1);
    lvh(H, d.nNonPure);
}
inline uint64_t levelHashOf(const Level& L) {
    LevelHash H;
    lvh(H, L.objs);
    lvh(H, L.portals);
    lvh(H, L.pads);
    lvh(H, L.orbs);
    lvh(H, L.speeds);
    lvh(H, L.slopes);
    lvh(H, L.coins);
    lvh(H, L.coinDyn);
    levelHashDyn(H, L.dyn);
    lvh(H, L.topAhead);
    lvh(H, L.bucketX0);
    lvh(H, L.maxX);
    lvh(H, L.maxY);
    lvh(H, L.unsupported);
    lvh(H, L.gpHandoff);
    return H.h;
}
struct ReplayLevelCache {
    bool valid = false;
    std::vector<std::string> args;
    unsigned long long groupsGen = 0;
    std::string csv;
    std::vector<std::pair<std::string, std::string>> files;   // path, bytes
    Level level;
    LoadLevelWrites writes;
    std::string printed;
    uint64_t hash = 0;   // --levelcachecheck: levelHashOf(level) when stored
};
inline ReplayLevelCache g_replayLevel;

template <class T> inline void dropHeld(T& x) {
    T gone = std::move(x);   // destroyed at the end of this scope, its buffers with it
    x = T{};
}
inline void releaseCrossCallCaches() {
    dropHeld(g_levelTextCache);
    dropHeld(g_groupLayersCache);
    dropHeld(g_trigRowsCache);
    dropHeld(g_groupMembersCache);
    dropHeld(g_replayLevel);
    dropHeld(g_fileSigHint);
    dropHeld(g_levelCsv);   // the caller's, cleared after every call but its capacity kept
}
inline size_t crossCallCacheBytes() {
    auto tl = [](const GroupTimeline& g) {
        size_t n = 0;
        for (const auto& kv : g) n += kv.second.capacity() * sizeof(DynSample);
        return n;
    };
    size_t n = g_levelTextCache.text.capacity() + g_levelCsv.capacity() + tl(g_groupLayersCache.merged)
               + g_groupLayersCache.report.capacity() + g_trigRowsCache.bytes.capacity()
               + g_groupMembersCache.bytes.capacity() + g_replayLevel.csv.capacity()
               + g_replayLevel.printed.capacity();
    for (const GroupFileParse& f : g_groupLayersCache.files) n += f.bytes.capacity() + tl(f.one);
    for (const auto& f : g_replayLevel.files) n += f.second.capacity();
    for (const auto& kv : g_groupMembersCache.byGroup) n += kv.second.capacity() * sizeof(int);
    return n;
}
inline size_t crossCallGroupBytes() {
    auto tl = [](const GroupTimeline& g) {
        size_t n = 0;
        for (const auto& kv : g) n += kv.second.capacity() * sizeof(DynSample);
        return n;
    };
    size_t n = tl(g_groupLayersCache.merged);
    for (const GroupFileParse& f : g_groupLayersCache.files) n += f.bytes.capacity() + tl(f.one);
    return n;
}

inline std::vector<std::string> ladderArgs(int argc, char** argv) {
    std::vector<std::string> a;
    for (int i = 0; i < argc; ++i) {
        if (i + 1 < argc
            && (!std::strcmp(argv[i], "--capmap") || !std::strcmp(argv[i], "--gridmap")
                || !std::strcmp(argv[i], "--search"))) {
            ++i;
            continue;
        }
        a.push_back(argv[i]);
    }
    return a;
}

inline int cliMainOnce(int argc, char** argv) {
    const auto cliT0 = std::chrono::steady_clock::now();
    for (double& m : g_prepMark) m = -1;
    g_groupSigCheck = false;   // --groupsigcheck is per call (set again by the argument loop below)
    g_levelCacheCheck = false;   // --levelcachecheck likewise
    g_walkResumeCheck = false;   // --walkresumecheck likewise
    auto prepMark = [&](int k) {
        g_prepMark[k] = std::chrono::duration<double>(std::chrono::steady_clock::now() - cliT0)
                            .count();
    };
    g_outcome.reset();
    resetInvocationState();
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    std::printf("fpenv: mxcsr=0x%04x\n", (unsigned)_mm_getcsr());
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--mxcsr")) {
            _mm_setcsr((unsigned)std::strtoul(argv[i + 1], nullptr, 0));
            std::printf("fpenv: mxcsr set to 0x%04x\n", (unsigned)_mm_getcsr());
        }
#elif defined(__aarch64__)
    // AArch64 keeps flush-to-zero and the rounding mode in FPCR. Read only: --mxcsr is x86's.
    {
        unsigned long long fpcr = 0;
        __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
        std::printf("fpenv: fpcr=0x%08llx\n", fpcr);
    }
#endif
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--eval-padgate")) continue;
        if (i + 1 >= argc) {
            std::printf("--eval-padgate needs a case file\n");
            return 2;
        }
        std::FILE* f = std::fopen(argv[i + 1], "r");
        if (!f) {
            std::printf("--eval-padgate: cannot read %s\n", argv[i + 1]);
            return 2;
        }
        char buf[512];
        while (std::fgets(buf, sizeof buf, f)) {
            std::string line(buf);
            bool blank = true;
            for (char& ch : line) {
                if (ch == ',') ch = ' ';
                else if (!std::isspace((unsigned char)ch)) blank = false;
            }
            if (blank) continue;
            std::istringstream is(line);
            double v[13];
            int n = 0;
            while (n < 13 && (is >> v[n])) ++n;
            if (n < 13) { std::printf("hit=?\n"); continue; }
            Obj o{};
            o.cx = v[0];  o.cy = v[1];  o.hw = v[2];  o.hh = v[3];
            o.ohw = v[4]; o.ohh = v[5]; o.rc = v[6];  o.rs = v[7];
            o.oriented = (v[8] != 0.0);
            std::printf("hit=%d\n",
                        orientedHit(o, v[9], v[10], v[11], v[12]) ? 1 : 0);
        }
        std::fclose(f);
        return 0;
    }
    if (argc < 2) {
        std::printf("usage: leveldp <objrects.csv> [--out plan.txt]\n");
        return 2;
    }
    {
        static const char* const kRemoved[][3] = {
        {"--no-miniwave", "dbd106d", "5c44e5d"},
        {"--no-ringmode", "dbd106d", "5c44e5d"},
        {"--no-pressspent", "dbd106d", "5c44e5d"},
        {"--no-portalseat", "dbd106d", "5c44e5d"},
        {"--no-forceorder", "dbd106d", "5c44e5d"},
        {"--no-rot2900halve", "dbd106d", "5c44e5d"},
        {"--no-slopeland5", "dbd106d", "5c44e5d"},
        {"--no-portallatch", "dbd106d", "5c44e5d"},
        {"--no-r52gravhold", "dbd106d", "5c44e5d"},
        {"--no-ceilseat", "dbd106d", "5c44e5d"},
        {"--no-slopeveto", "dbd106d", "b43e150"},
        {"--no-slopefreshrect", "dbd106d", "5c44e5d"},
        {"--no-slopenudge", "dbd106d", "5c44e5d"},
        {"--no-mpushreach", "dbd106d", "5c44e5d"},
        {"--no-boostlatch", "dbd106d", "b43e150"},
        {"--no-ringfirsttouch", "dbd106d", "5c44e5d"},
        {"--no-padobb", "dbd106d", "5c44e5d"},
        {"--no-padspinpre", "dbd106d", "5c44e5d"},
        {"--no-dualflip", "dbd106d", "b43e150"},
        {"--nofreeside", "dbd106d", "5c44e5d"},
        {"--no-ufolandtol", "dbd106d", "5c44e5d"},
        {"--no-uforampflap", "dbd106d", "5c44e5d"},
        {"--no-ridelandlaunch", "dbd106d", "5c44e5d"},
        {"--no-ballcorng", "dbd106d", "5c44e5d"},
        {"--no-rampfirst", "dbd106d", "5c44e5d"},
        {"--nohangladder", "dbd106d", "5c44e5d"},
        {"--nocrush", "dbd106d", "5c44e5d"},
        {"--noformula", "dbd106d", "b43e150"},
        {"--ballceilpos", "4c5b7c7", "56a7a83"},
        {"--no-ballceilpos", "4c5b7c7", "56a7a83"},
        {"--ceilcont", "4c5b7c7", "56a7a83"},
        {"--no-ceilcont", "4c5b7c7", "56a7a83"},
        {"--ceillimrelease", "4c5b7c7", "56a7a83"},
        {"--no-ceillimrelease", "4c5b7c7", "56a7a83"},
        {"--ceilpinmin", "4c5b7c7", "56a7a83"},
        {"--no-ceilpinmin", "4c5b7c7", "56a7a83"},
        {"--ceilreleasemode", "4c5b7c7", "56a7a83"},
        {"--no-ceilreleasemode", "4c5b7c7", "56a7a83"},
        {"--ceilveto", "4c5b7c7", "56a7a83"},
        {"--no-ceilveto", "4c5b7c7", "56a7a83"},
        {"--csvtypes", "4c5b7c7", "ac5506d"},
        {"--no-csvtypes", "4c5b7c7", "ac5506d"},
        {"--cubeceilgrace", "4c5b7c7", "56a7a83"},
        {"--no-cubeceilgrace", "4c5b7c7", "56a7a83"},
        {"--dropnocollide", "4c5b7c7", "bf6e11d"},
        {"--dualcouple", "4c5b7c7", "bf6e11d"},
        {"--no-dualcouple", "4c5b7c7", "bf6e11d"},
        {"--no-dyninterp", "4c5b7c7", "bf6e11d"},
        {"--escrotahead", "4c5b7c7", "56a7a83"},
        {"--hangland", "4c5b7c7", "56a7a83"},
        {"--no-hangland", "4c5b7c7", "56a7a83"},
        {"--hazaftersolid", "4c5b7c7", "56a7a83"},
        {"--no-hazaftersolid", "4c5b7c7", "56a7a83"},
        {"--killslopeside", "4c5b7c7", "56a7a83"},
        {"--no-killslopeside", "4c5b7c7", "56a7a83"},
        {"--lockanchor", "4c5b7c7", "ac5506d"},
        {"--no-lockanchor", "4c5b7c7", "ac5506d"},
        {"--movetarget", "4c5b7c7", "ac5506d"},
        {"--no-movetarget", "4c5b7c7", "ac5506d"},
        {"--mpushlaunch", "4c5b7c7", "56a7a83"},
        {"--no-mpushlaunch", "4c5b7c7", "56a7a83"},
        {"--mpushsign", "4c5b7c7", "56a7a83"},
        {"--no-mpushsign", "4c5b7c7", "56a7a83"},
        {"--oriringnow", "4c5b7c7", "56a7a83"},
        {"--no-oriringnow", "4c5b7c7", "56a7a83"},
        {"--oriented", "4c5b7c7", "bf6e11d"},
        {"--no-oriented", "4c5b7c7", "bf6e11d"},
        {"--recinterp", "4c5b7c7", "bf6e11d"},
        {"--no-recinterp", "4c5b7c7", "bf6e11d"},
        {"--releaseceilpress", "4c5b7c7", "56a7a83"},
        {"--no-releaseceilpress", "4c5b7c7", "56a7a83"},
        {"--norevtoggle", "4c5b7c7", "56a7a83"},
        {"--rotpretap", "4c5b7c7", "56a7a83"},
        {"--no-rotpretap", "4c5b7c7", "56a7a83"},
        {"--shipheldflap", "4c5b7c7", "56a7a83"},
        {"--no-shipheldflap", "4c5b7c7", "56a7a83"},
        {"--shipslopekill", "4c5b7c7", "56a7a83"},
        {"--no-shipslopekill", "4c5b7c7", "56a7a83"},
        {"--slopelaw", "4c5b7c7", "56a7a83"},
        {"--no-slopelaw", "4c5b7c7", "56a7a83"},
        {"--spiderstrip", "4c5b7c7", "56a7a83"},
        {"--no-spiderstrip", "4c5b7c7", "56a7a83"},
        {"--stickground", "4c5b7c7", "56a7a83"},
        {"--no-stickground", "4c5b7c7", "56a7a83"},
        {"--stickrelease", "4c5b7c7", "56a7a83"},
        {"--no-stickrelease", "4c5b7c7", "56a7a83"},
        {"--stickseam", "4c5b7c7", "56a7a83"},
        {"--no-stickseam", "4c5b7c7", "56a7a83"},
        {"--touchprey=", "4c5b7c7", "bf6e11d"},
        {"--touchretime", "4c5b7c7", "bf6e11d"},
        {"--no-touchretime", "4c5b7c7", "bf6e11d"},
        {"--touchretimelag", "4c5b7c7", "bf6e11d"},
        {"--no-touchretimelag", "4c5b7c7", "bf6e11d"},
        {"--tpbandskip", "4c5b7c7", "56a7a83"},
        {"--no-tpbandskip", "4c5b7c7", "56a7a83"},
        {"--tpgroundvy", "4c5b7c7", "56a7a83"},
        {"--no-tpgroundvy", "4c5b7c7", "56a7a83"},
        {"--trigclosed", "4c5b7c7", "ac5506d"},
        {"--no-trigclosed", "4c5b7c7", "ac5506d"},
        {"--trigrelevant", "4c5b7c7", "ac5506d"},
        {"--no-trigrelevant", "4c5b7c7", "ac5506d"},
        {"--upceilv3", "4c5b7c7", "56a7a83"},
        {"--no-upceilv3", "4c5b7c7", "56a7a83"},
        {"--waveflipkill", "4c5b7c7", "56a7a83"},
        {"--no-waveflipkill", "4c5b7c7", "56a7a83"},
        {"--waveslopeside", "4c5b7c7", "56a7a83"},
        {"--no-waveslopeside", "4c5b7c7", "56a7a83"},
        {"--wavespentgate", "4c5b7c7", "56a7a83"},
        {"--no-wavespentgate", "4c5b7c7", "56a7a83"},
        {"--witnessframe", "4c5b7c7", "ac5506d"},
        // the 0.2.0 clean-up
        {"--automixlag", "a234c00", "edc8b6b"},
        {"--no-automixlag", "a234c00", "edc8b6b"},
        {"--trigwinworld", "a234c00", "edc8b6b"},
        {"--no-trigwinworld", "a234c00", "edc8b6b"},
        {"--bootretime", "a234c00", "edc8b6b"},
        {"--no-bootretime", "a234c00", "edc8b6b"},
        // the 0.4.0 clean-up
        {"--flyhazaftersolid", "4f75a6f", "d008ee2"},
        {"--no-flyhazaftersolid", "4f75a6f", "d008ee2"},
        {"--latgap", "4f75a6f", "d008ee2"},
        {"--no-latgap", "4f75a6f", "d008ee2"},
        {"--forceunit1x", "4f75a6f", "d008ee2"},
        {"--no-forceunit1x", "4f75a6f", "d008ee2"},
        {"--swingpushtol", "4f75a6f", "d008ee2"},
        {"--no-swingpushtol", "4f75a6f", "d008ee2"},
        {"--trigwinsel", "4f75a6f", "d008ee2"},
        {"--no-trigwinsel", "4f75a6f", "d008ee2"},
        {"--bandcarryvy", "4f75a6f", "d008ee2"},
        {"--no-bandcarryvy", "4f75a6f", "d008ee2"},
        {"--swingpushflight", "4f75a6f", "d008ee2"},
        {"--no-swingpushflight", "4f75a6f", "d008ee2"},
        {"--shrinkpress", "4f75a6f", "d008ee2"},
        {"--no-shrinkpress", "4f75a6f", "d008ee2"},
        {"--repellpolarity", "4f75a6f", "d008ee2"},
        {"--no-repellpolarity", "4f75a6f", "d008ee2"},
        {"--gravportalseat", "4f75a6f", "d008ee2"},
        {"--no-gravportalseat", "4f75a6f", "d008ee2"},
        {"--dualspeedp2", "4f75a6f", "d008ee2"},
        {"--no-dualspeedp2", "4f75a6f", "d008ee2"},
        {"--wavepadhalf", "4f75a6f", "d008ee2"},
        {"--no-wavepadhalf", "4f75a6f", "d008ee2"},
        {"--dualremirror", "4f75a6f", "d008ee2"},
        {"--no-dualremirror", "4f75a6f", "d008ee2"},
        {"--waverotactual", "4f75a6f", "d008ee2"},
        {"--no-waverotactual", "4f75a6f", "d008ee2"},
        {"--wavegrowclamp", "4f75a6f", "d008ee2"},
        {"--no-wavegrowclamp", "4f75a6f", "d008ee2"},
        {"--portalspidertap", "4f75a6f", "d008ee2"},
        {"--no-portalspidertap", "4f75a6f", "d008ee2"},
        {"--spiderairbuffer", "4f75a6f", "d008ee2"},
        {"--no-spiderairbuffer", "4f75a6f", "d008ee2"},
        {"--ceiltaponce", "4f75a6f", "d008ee2"},
        {"--no-ceiltaponce", "4f75a6f", "d008ee2"},
        {"--dualcubeband", "4f75a6f", "d008ee2"},
        {"--no-dualcubeband", "4f75a6f", "d008ee2"},
        {"--tponce", "4f75a6f", "d008ee2"},
        {"--no-tponce", "4f75a6f", "d008ee2"},
        {"--tplatch", "4f75a6f", "d008ee2"},
        {"--no-tplatch", "4f75a6f", "d008ee2"},
        {"--padentry", "4f75a6f", "d008ee2"},
        {"--no-padentry", "4f75a6f", "d008ee2"},
        {"--forceboxdir", "4f75a6f", "d008ee2"},
        {"--no-forceboxdir", "4f75a6f", "d008ee2"},
        {"--escufo", "4f75a6f", "d008ee2"},
        {"--no-escufo", "4f75a6f", "d008ee2"},
        {"--spentdyn", "4f75a6f", "d008ee2"},
        {"--no-spentdyn", "4f75a6f", "d008ee2"},
        {"--radreach", "4f75a6f", "d008ee2"},
        {"--no-radreach", "4f75a6f", "d008ee2"},
        {"--uforot", "4f75a6f", "d008ee2"},
        {"--no-uforot", "4f75a6f", "d008ee2"},
        {"--spdonce", "4f75a6f", "d008ee2"},
        {"--no-spdonce", "4f75a6f", "d008ee2"},
        {"--dualorbflip", "4f75a6f", "d008ee2"},
        {"--no-dualorbflip", "4f75a6f", "d008ee2"},
        {"--orbspawn", "4f75a6f", "d008ee2"},
        {"--no-orbspawn", "4f75a6f", "d008ee2"},
        {"--spawnringjump", "4f75a6f", "d008ee2"},
        {"--no-spawnringjump", "4f75a6f", "d008ee2"},
        {"--ballstake", "4f75a6f", "d008ee2"},
        {"--no-ballstake", "4f75a6f", "d008ee2"},
        {"--boostcarry", "4f75a6f", "d008ee2"},
        {"--no-boostcarry", "4f75a6f", "d008ee2"},
        {"--dualflyring", "4f75a6f", "d008ee2"},
        {"--no-dualflyring", "4f75a6f", "d008ee2"},
        {"--dyngravseen", "4f75a6f", "d008ee2"},
        {"--no-dyngravseen", "4f75a6f", "d008ee2"},
        {"--dropfall", "4f75a6f", "d008ee2"},
        {"--no-dropfall", "4f75a6f", "d008ee2"},
        {"--anchornoop", "4f75a6f", "d008ee2"},
        {"--no-anchornoop", "4f75a6f", "d008ee2"},
        {"--movenospawn", "4f75a6f", "d008ee2"},
        {"--no-movenospawn", "4f75a6f", "d008ee2"},
        {"--repelland", "4f75a6f", "d008ee2"},
        {"--no-repelland", "4f75a6f", "d008ee2"},
        {"--repela0c", "4f75a6f", "d008ee2"},
        {"--no-repela0c", "4f75a6f", "d008ee2"},
        {"--spentaction", "4f75a6f", "d008ee2"},
        {"--no-spentaction", "4f75a6f", "d008ee2"},
        {"--rot2", "4f75a6f", "d008ee2"},
        {"--no-rot2", "4f75a6f", "d008ee2"},
        {"--rotport", "4f75a6f", "d008ee2"},
        {"--no-rotport", "4f75a6f", "d008ee2"},
        {"--a1clatch", "4f75a6f", "d008ee2"},
        {"--no-a1clatch", "4f75a6f", "d008ee2"},
        {"--ridebox", "4f75a6f", "d008ee2"},
        {"--no-ridebox", "4f75a6f", "d008ee2"},
        {"--spiderminigd", "4f75a6f", "d008ee2"},
        {"--no-spiderminigd", "4f75a6f", "d008ee2"},
        {"--tapclosebox", "4f75a6f", "d008ee2"},
        {"--no-tapclosebox", "4f75a6f", "d008ee2"},
        {"--cubeentryspin", "4f75a6f", "9b56145"},
        {"--no-cubeentryspin", "4f75a6f", "9b56145"},
        {"--ringrotsign", "4f75a6f", "36b534d"},
        {"--no-ringrotsign", "4f75a6f", "36b534d"},
        {"--dualdash", "4f75a6f", "36b534d"},
        {"--no-dualdash", "4f75a6f", "36b534d"},
        {"--ceilcubejump", "4f75a6f", "ae8ea0c"},
        {"--no-ceilcubejump", "4f75a6f", "ae8ea0c"},
        {"--ceilcontv4", "4f75a6f", "ae8ea0c"},
        {"--no-ceilcontv4", "4f75a6f", "ae8ea0c"},
        {"--shipceiltol4", "4f75a6f", "ae8ea0c"},
        {"--no-shipceiltol4", "4f75a6f", "ae8ea0c"},
        {"--teleoldpos", "4f75a6f", "27ac5dd"},
        {"--no-teleoldpos", "4f75a6f", "27ac5dd"},
        {"--ballungroundramp", "4f75a6f", "84e607a"},
        {"--no-ballungroundramp", "4f75a6f", "84e607a"},
        {"--contseatclamp", "4f75a6f", "b42218a"},
        {"--no-contseatclamp", "4f75a6f", "b42218a"},
        {"--bandfloorunstick", "4f75a6f", "b42218a"},
        {"--no-bandfloorunstick", "4f75a6f", "b42218a"},
        {"--ceilgracerider", "4f75a6f", "18c39b9"},
        {"--no-ceilgracerider", "4f75a6f", "18c39b9"},
        {"--lawuplaunch", "4f75a6f", "3290750"},
        {"--no-lawuplaunch", "4f75a6f", "3290750"},
        {"--dualseatt", "4f75a6f", "3290750"},
        {"--no-dualseatt", "4f75a6f", "3290750"},
        {"--ceilfreshrect", "4f75a6f", "0acc458"},
        {"--no-ceilfreshrect", "4f75a6f", "0acc458"},
        {"--ceilpushreach", "4f75a6f", "7d29ad8"},
        {"--no-ceilpushreach", "4f75a6f", "7d29ad8"},
        {"--stripownacq", "4f75a6f", "44b8d97"},
        {"--no-stripownacq", "4f75a6f", "44b8d97"},
        {"--ladderback", "4f75a6f", "0bae408"},
        {"--no-ladderback", "4f75a6f", "0bae408"},
        {"--ladderenv", "4f75a6f", "0bae408"},
        {"--no-ladderenv", "4f75a6f", "0bae408"},
        {"--upceilfreshend", "4f75a6f", "b5da1ca"},
        {"--no-upceilfreshend", "4f75a6f", "b5da1ca"},
        {"--uforideflap", "4f75a6f", "f20dab1"},
        {"--no-uforideflap", "4f75a6f", "f20dab1"},
        {"--lawunderstack", "4f75a6f", "9ef38b3"},
        {"--no-lawunderstack", "4f75a6f", "9ef38b3"},
        {"--ridenoblockrest", "4f75a6f", "9ef38b3"},
        {"--no-ridenoblockrest", "4f75a6f", "9ef38b3"},
        {"--groundflushflat", "4f75a6f", "ed0535a"},
        {"--no-groundflushflat", "4f75a6f", "ed0535a"},
        {"--lawfreeclamp", "4f75a6f", "96af63d"},
        {"--no-lawfreeclamp", "4f75a6f", "96af63d"},
        {"--lawseatlaunch", "4f75a6f", "96af63d"},
        {"--no-lawseatlaunch", "4f75a6f", "96af63d"},
        {"--flipcubecrush", "4f75a6f", "e20e0b5"},
        {"--no-flipcubecrush", "4f75a6f", "e20e0b5"},
        {"--balltapstep", "4f75a6f", "520fe36"},
        {"--no-balltapstep", "4f75a6f", "520fe36"},
        {"--ballunderv3", "4f75a6f", "7f045e5"},
        {"--no-ballunderv3", "4f75a6f", "7f045e5"},
        {"--jumpafterland", "4f75a6f", "8a1bb9d"},
        {"--no-jumpafterland", "4f75a6f", "8a1bb9d"},
        {"--dualflipclock", "4f75a6f", "e6ed808"},
        {"--no-dualflipclock", "4f75a6f", "e6ed808"},
        {"--spiderbandsup", "4f75a6f", "0889198"},
        {"--no-spiderbandsup", "4f75a6f", "0889198"},
        {"--ceilrideslope", "4f75a6f", "556e131"},
        {"--no-ceilrideslope", "4f75a6f", "556e131"},
        {"--lawseatonslope", "4f75a6f", "556e131"},
        {"--no-lawseatonslope", "4f75a6f", "556e131"},
        {"--flyhazafterslope", "4f75a6f", "556e131"},
        {"--no-flyhazafterslope", "4f75a6f", "556e131"},
        {"--slopevelexact", "4f75a6f", "556e131"},
        {"--no-slopevelexact", "4f75a6f", "556e131"},
        {"--flipceilland", "4f75a6f", "556e131"},
        {"--no-flipceilland", "4f75a6f", "556e131"},
        {"--portalslopeorder", "4f75a6f", "556e131"},
        {"--no-portalslopeorder", "4f75a6f", "556e131"},
        {"--flipceilride", "4f75a6f", "556e131"},
        {"--no-flipceilride", "4f75a6f", "556e131"},
        {"--ceilreleaseball", "4f75a6f", "556e131"},
        {"--no-ceilreleaseball", "4f75a6f", "556e131"},
        {"--seattoldual", "4f75a6f", "556e131"},
        {"--no-seattoldual", "4f75a6f", "556e131"},
        {"--relkeepblock", "4f75a6f", "556e131"},
        {"--no-relkeepblock", "4f75a6f", "556e131"},
        {"--lawfrombelow", "4f75a6f", "556e131"},
        {"--no-lawfrombelow", "4f75a6f", "556e131"},
        {"--rampjumptick", "4f75a6f", "556e131"},
        {"--no-rampjumptick", "4f75a6f", "556e131"},
        {"--ufobanddir", "4f75a6f", "556e131"},
        {"--no-ufobanddir", "4f75a6f", "556e131"},
        {"--slopetopveto", "4f75a6f", "556e131"},
        {"--no-slopetopveto", "4f75a6f", "556e131"},
        {"--undernudge", "4f75a6f", "556e131"},
        {"--no-undernudge", "4f75a6f", "556e131"},
        {"--seatkeepfirst", "4f75a6f", "556e131"},
        {"--no-seatkeepfirst", "4f75a6f", "556e131"},
        {"--upceilband", "4f75a6f", "556e131"},
        {"--no-upceilband", "4f75a6f", "556e131"},
        {"--lawunderrelease", "4f75a6f", "556e131"},
        {"--no-lawunderrelease", "4f75a6f", "556e131"},
        {"--speedafterseat", "4f75a6f", "556e131"},
        {"--no-speedafterseat", "4f75a6f", "556e131"},
        {"--rideseatrepeat", "4f75a6f", "556e131"},
        {"--no-rideseatrepeat", "4f75a6f", "556e131"},
        {"--slopeinset", "4f75a6f", "556e131"},
        {"--no-slopeinset", "4f75a6f", "556e131"},
        {"--exitbeatsvy", "4f75a6f", "556e131"},
        {"--no-exitbeatsvy", "4f75a6f", "556e131"},
        {"--undersidev3", "4f75a6f", "556e131"},
        {"--no-undersidev3", "4f75a6f", "556e131"},
        {"--upceilrepeat", "4f75a6f", "556e131"},
        {"--no-upceilrepeat", "4f75a6f", "556e131"},
        {"--ballceilveto", "4f75a6f", "556e131"},
        {"--no-ballceilveto", "4f75a6f", "556e131"},
        {"--padclearsride", "4f75a6f", "556e131"},
        {"--no-padclearsride", "4f75a6f", "556e131"},
        {"--ceillimv4", "4f75a6f", "556e131"},
        {"--no-ceillimv4", "4f75a6f", "556e131"},
        {"--waveslopelowend", "4f75a6f", "556e131"},
        {"--no-waveslopelowend", "4f75a6f", "556e131"},
        {"--shipslopecap", "4f75a6f", "556e131"},
        {"--no-shipslopecap", "4f75a6f", "556e131"},
        {"--waveslopecap", "4f75a6f", "556e131"},
        {"--no-waveslopecap", "4f75a6f", "556e131"},
        {"--hangrunoff", "4f75a6f", "556e131"},
        {"--no-hangrunoff", "4f75a6f", "556e131"},
        {"--hangseatrepeat", "4f75a6f", "556e131"},
        {"--no-hangseatrepeat", "4f75a6f", "556e131"},
        {"--sloperot", "4f75a6f", "556e131"},
        {"--no-sloperot", "4f75a6f", "556e131"},
        {"--portalpressorder", "4f75a6f", "556e131"},
        {"--no-portalpressorder", "4f75a6f", "556e131"},
        {"--undercrush", "4f75a6f", "556e131"},
        {"--no-undercrush", "4f75a6f", "556e131"},
        {"--ufoslopekill", "4f75a6f", "556e131"},
        {"--no-ufoslopekill", "4f75a6f", "556e131"},
        {"--balltapexit", "4f75a6f", "556e131"},
        {"--no-balltapexit", "4f75a6f", "556e131"},
        {"--extseatrepeat", "4f75a6f", "556e131"},
        {"--no-extseatrepeat", "4f75a6f", "556e131"},
        {"--flipceilend", "4f75a6f", "556e131"},
        {"--no-flipceilend", "4f75a6f", "556e131"},
        {"--preslopegate", "4f75a6f", "556e131"},
        {"--no-preslopegate", "4f75a6f", "556e131"},
        {"--preslopesolid", "4f75a6f", "556e131"},
        {"--no-preslopesolid", "4f75a6f", "556e131"},
        {"--thinsupport", "4f75a6f", "556e131"},
        {"--no-thinsupport", "4f75a6f", "556e131"},
        {"--spikebottom", "4f75a6f", "556e131"},
        {"--no-spikebottom", "4f75a6f", "556e131"},
        {"--ufolawflap", "4f75a6f", "556e131"},
        {"--no-ufolawflap", "4f75a6f", "556e131"},
        {"--ceilwallnopush", "4f75a6f", "556e131"},
        {"--no-ceilwallnopush", "4f75a6f", "556e131"},
        {"--heldtolhead", "4f75a6f", "556e131"},
        {"--no-heldtolhead", "4f75a6f", "556e131"},
        {"--heldnodown", "4f75a6f", "556e131"},
        {"--no-heldnodown", "4f75a6f", "556e131"},
        {"--capinsetgate", "4f75a6f", "556e131"},
        {"--no-capinsetgate", "4f75a6f", "556e131"},
        {"--ballslopekill", "4f75a6f", "556e131"},
        {"--no-ballslopekill", "4f75a6f", "556e131"},
        {"--lawcontact", "4f75a6f", "556e131"},
        {"--no-lawcontact", "4f75a6f", "556e131"},
        {"--activators", "4f75a6f", "13ae1aa"},
        {"--no-activators", "4f75a6f", "13ae1aa"},
        {"--coinpick", "4f75a6f", "ae28409"},
        {"--no-coinpick", "4f75a6f", "ae28409"},
        {"--hanglandgate", "4f75a6f", "6cd733b"},
        {"--no-hanglandgate", "4f75a6f", "6cd733b"},
        {"--itemsnoblock", "4f75a6f", "9c116a0"},
        {"--no-itemsnoblock", "4f75a6f", "9c116a0"},
        {"--no-offmoves", "4f75a6f", "12e2d6e"},
        {"--no-slopeseat", "4f75a6f", "6ced25c"},
        {"--spawnroots", "4f75a6f", "65e1dad"},
        {"--no-spawnroots", "4f75a6f", "65e1dad"},
        {"--touchretimebox", "4f75a6f", "111fb0e"},
        {"--no-touchretimebox", "4f75a6f", "111fb0e"},
        {"--twinskip", "4f75a6f", "5c2a526"},
        {"--no-twinskip", "4f75a6f", "5c2a526"},
        {"--walkgates", "4f75a6f", "0597ec8"},
        {"--no-walkgates", "4f75a6f", "0597ec8"},
        {"--rotextend", "4f75a6f", "d008ee2"},
        {"--slideparts", "4f75a6f", "d008ee2"},
        {"--ceilpin", "1cfc049c", "a6dd210f"},
        {"--groupholdend", "1cfc049c", "390be274"},
        {"--hazaabb", "1cfc049c", "fca8fa9e"},
        {"--ladderx0", "1cfc049c", "05d4e224"},
        {"--old-latency", "1cfc049c", "a6dd210f"},
        {"--old-slope", "1cfc049c", "a6dd210f"},
        {"--pinminnoswing", "1cfc049c", "ceaafa89"},
        {"--rotlast", "1cfc049c", "a6dd210f"},
        {"--touch-from-anchor", "1cfc049c", "a6dd210f"},
        };
        for (int i = 1; i < argc; ++i)
            for (const auto& r : kRemoved) {
                const size_t n = std::strlen(r[0]);
                const bool hit = r[0][n - 1] == '='
                    ? !std::strncmp(argv[i], r[0], n) : !std::strcmp(argv[i], r[0]);
                if (hit) {
                    std::printf("leveldp: %s was removed in %s; the behaviour is fixed "
                                "(since %s)\n", argv[i], r[1], r[2]);
                    std::fprintf(stderr, "leveldp: %s was removed in %s; the behaviour is "
                                 "fixed (since %s)\n", argv[i], r[1], r[2]);
                    return 2;
                }
            }
    }
    std::string outPath = "leveldp_plan.txt";
    std::string replayPath;
    long long t0 = 0;
    long long startSnapUid = -1;
    double startSnapDist = 0.0;
    int startFrame = 0;   // --start's 21st field (gframe); gates the pad seeding
    double x0 = -kDx;  // so that x(1) = 0
    double x0World = -1e18;
    int dbgLayers = 0;
    std::string snapLogPath;
    std::vector<std::string> groupsPaths;
    std::string trigPath, grpPath, obbPath, setPath, rotQPath, itemPath;
    std::string areasPath;   // --areas (with --areamove)
    double endPortalArg = 0.0;   // --endportalx (with --goalgd)
    // y, vy, mode, held, grounded, flip (everything after that is zeroed)
    State init{(float)kFloorY, 0.f, 0, 0, 1, 0};
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--spawnremap")) g_spawnRemap = true;
        if (!std::strcmp(argv[i], "--no-spawnremap")) g_spawnRemap = false;
        // --touchvisual: a Pulse or an Alpha ends a chain walk (triggers.hpp)
        if (!std::strcmp(argv[i], "--touchvisual")) g_touchVisual = true;
        if (!std::strcmp(argv[i], "--no-touchvisual")) g_touchVisual = false;
        // --recleads: an autonomous object whose recording moves first is read as it (dynamics.hpp)
        if (!std::strcmp(argv[i], "--recleads")) g_recLeads = true;
        if (!std::strcmp(argv[i], "--no-recleads")) g_recLeads = false;
        if (!std::strcmp(argv[i], "--waveobbaabb")) g_waveObbAabb = true;
        if (!std::strcmp(argv[i], "--wavehazendpoint")) g_waveHazEndpoint = true;
        if (!std::strcmp(argv[i], "--waveoneway")) g_waveOneway = true;
        if (!std::strcmp(argv[i], "--tplatchexact")) g_tpLatchExact = true;
        if (!std::strcmp(argv[i], "--tpseen")) g_tpSeen = true;
        if (!std::strcmp(argv[i], "--no-tpseen")) g_tpSeen = false;
        if (!std::strcmp(argv[i], "--dyngplatch")) g_dynGpLatch = true;
        if (!std::strcmp(argv[i], "--no-dyngplatch")) g_dynGpLatch = false;
        if (!std::strcmp(argv[i], "--tpspawn")) g_tpSpawn = true;
        if (!std::strcmp(argv[i], "--padtouch")) g_padTouch = true;
        // --padrefill: a pad the body was already inside is taken as fired (constants.hpp)
        if (!std::strcmp(argv[i], "--padrefill")) g_padRefill = true;
        if (!std::strcmp(argv[i], "--no-padrefill")) g_padRefill = false;
        // --spiderorbface: a spider orb jumps from its own facing's gravity (constants.hpp)
        if (!std::strcmp(argv[i], "--spiderorbface")) g_spiderOrbFace = true;
        if (!std::strcmp(argv[i], "--no-spiderorbface")) g_spiderOrbFace = false;
        // --dashground: a dash lasts through a landing while the button is held (constants.hpp)
        if (!std::strcmp(argv[i], "--dashground")) g_dashGround = true;
        if (!std::strcmp(argv[i], "--no-dashground")) g_dashGround = false;
        if (!std::strcmp(argv[i], "--fixgravthr")) g_fixGravThr = true;
        if (!std::strcmp(argv[i], "--expinzero")) g_expInZero = true;
        if (!std::strcmp(argv[i], "--no-expinzero")) g_expInZero = false;
        if (!std::strcmp(argv[i], "--spiderpad")) g_spiderPad = true;
        if (!std::strcmp(argv[i], "--gravtoggle")) g_gravToggle = true;
        if (!std::strcmp(argv[i], "--touchoff")) g_touchOwnOff = true;
        if (!std::strcmp(argv[i], "--no-touchoff")) g_touchOwnOff = false;
        if (!std::strcmp(argv[i], "--areamove")) g_areaMove = true;
        if (!std::strcmp(argv[i], "--no-areamove")) g_areaMove = false;
        if (!std::strcmp(argv[i], "--recendauto")) g_recEndAuto = true;
        if (!std::strcmp(argv[i], "--no-recendauto")) g_recEndAuto = false;
        if (!std::strcmp(argv[i], "--tapdisarm")) g_tapDisarm = true;
        if (!std::strcmp(argv[i], "--no-tapdisarm")) g_tapDisarm = false;
        if (!std::strcmp(argv[i], "--boxarm")) g_boxArm = true;
        if (!std::strcmp(argv[i], "--no-boxarm")) g_boxArm = false;
        if (!std::strcmp(argv[i], "--spentp2")) g_spentP2 = true;
        if (!std::strcmp(argv[i], "--no-spentp2")) g_spentP2 = false;
        if (!std::strcmp(argv[i], "--nanslope")) g_nanSlope = true;
        if (!std::strcmp(argv[i], "--no-nanslope")) g_nanSlope = false;
        if (!std::strcmp(argv[i], "--flipbonk")) g_flipBonk = true;
        if (!std::strcmp(argv[i], "--no-flipbonk")) g_flipBonk = false;
        if (!std::strcmp(argv[i], "--multiorb")) g_multiOrb = true;
        if (!std::strcmp(argv[i], "--no-multiorb")) g_multiOrb = false;
        if (!std::strcmp(argv[i], "--spiderband")) g_spiderBand = true;
        if (!std::strcmp(argv[i], "--no-spiderband")) g_spiderBand = false;
        if (!std::strcmp(argv[i], "--spidercoyote")) g_spiderCoyote = true;
        if (!std::strcmp(argv[i], "--no-spidercoyote")) g_spiderCoyote = false;
        if (!std::strcmp(argv[i], "--spiderspend")) g_spiderSpend = true;
        if (!std::strcmp(argv[i], "--no-spiderspend")) g_spiderSpend = false;
        if (!std::strcmp(argv[i], "--ridehold")) g_rideHold = true;
        if (!std::strcmp(argv[i], "--no-ridehold")) g_rideHold = false;
        if (!std::strcmp(argv[i], "--stickfallback")) g_stickFallback = true;
        if (!std::strcmp(argv[i], "--no-stickfallback")) g_stickFallback = false;
        if (!std::strcmp(argv[i], "--graceendonly")) g_graceEndOnly = true;
        if (!std::strcmp(argv[i], "--no-graceendonly")) g_graceEndOnly = false;
        if (!std::strcmp(argv[i], "--ballscaleair")) g_ballScaleAir = true;
        if (!std::strcmp(argv[i], "--no-ballscaleair")) g_ballScaleAir = false;
        if (!std::strcmp(argv[i], "--slopeunchanged")) g_slopeUnchanged = true;
        if (!std::strcmp(argv[i], "--no-slopeunchanged")) g_slopeUnchanged = false;
        if (!std::strcmp(argv[i], "--thinlandjump")) g_thinLandJump = true;
        if (!std::strcmp(argv[i], "--no-thinlandjump")) g_thinLandJump = false;
        if (!std::strcmp(argv[i], "--apexreacq")) g_apexReacq = true;
        if (!std::strcmp(argv[i], "--no-apexreacq")) g_apexReacq = false;
        if (!std::strcmp(argv[i], "--followsection")) g_followSection = true;
        if (!std::strcmp(argv[i], "--no-followsection")) g_followSection = false;
        if (!std::strcmp(argv[i], "--bigsection")) g_bigSection = true;
        if (!std::strcmp(argv[i], "--no-bigsection")) g_bigSection = false;
        if (!std::strcmp(argv[i], "--portorder")) g_portOrder = true;
        if (!std::strcmp(argv[i], "--no-portorder")) g_portOrder = false;
        if (!std::strcmp(argv[i], "--slidemini")) g_slideMini = true;
        if (!std::strcmp(argv[i], "--no-slidemini")) g_slideMini = false;
        if (!std::strcmp(argv[i], "--apexstick")) g_apexStick = true;
        if (!std::strcmp(argv[i], "--no-apexstick")) g_apexStick = false;
        if (!std::strcmp(argv[i], "--ceilstrict")) g_ceilStrict = true;
        if (!std::strcmp(argv[i], "--no-ceilstrict")) g_ceilStrict = false;
        if (!std::strcmp(argv[i], "--spiderpresslate")) g_spiderPressLate = true;
        if (!std::strcmp(argv[i], "--no-spiderpresslate")) g_spiderPressLate = false;
        if (!std::strcmp(argv[i], "--spiderpadfirst")) g_spiderPadFirst = true;
        if (!std::strcmp(argv[i], "--no-spiderpadfirst")) g_spiderPadFirst = false;
        if (!std::strcmp(argv[i], "--spiderbandback")) g_spiderBandBack = true;
        if (!std::strcmp(argv[i], "--no-spiderbandback")) g_spiderBandBack = false;
        // --spiderslopeline: the spider's teleport meets a ramp facing it at its line (constants.hpp)
        if (!std::strcmp(argv[i], "--spiderslopeline")) g_spiderSlopeLine = true;
        if (!std::strcmp(argv[i], "--no-spiderslopeline")) g_spiderSlopeLine = false;
        if (!std::strcmp(argv[i], "--spiderinner")) g_spiderInner = true;
        if (!std::strcmp(argv[i], "--no-spiderinner")) g_spiderInner = false;
        if (!std::strcmp(argv[i], "--rotboost")) g_rotBoost = true;
        if (!std::strcmp(argv[i], "--no-rotboost")) g_rotBoost = false;
        if (!std::strcmp(argv[i], "--rotspeed")) g_rotSpeed = true;
        if (!std::strcmp(argv[i], "--no-rotspeed")) g_rotSpeed = false;
        if (!std::strcmp(argv[i], "--bonkouter")) g_bonkOuter = true;
        if (!std::strcmp(argv[i], "--no-bonkouter")) g_bonkOuter = false;
        if (!std::strcmp(argv[i], "--flyfree")) g_flyFree = true;
        if (!std::strcmp(argv[i], "--no-flyfree")) g_flyFree = false;
        if (!std::strcmp(argv[i], "--dualzoomband")) g_dualZoomBand = true;
        if (!std::strcmp(argv[i], "--no-dualzoomband")) g_dualZoomBand = false;
        if (!std::strcmp(argv[i], "--zoomchain")) g_zoomChain = true;
        if (!std::strcmp(argv[i], "--no-zoomchain")) g_zoomChain = false;
        if (!std::strcmp(argv[i], "--lingerlaunch")) g_lingerLaunch = true;
        if (!std::strcmp(argv[i], "--no-lingerlaunch")) g_lingerLaunch = false;
        if (!std::strcmp(argv[i], "--warpfly")) g_warpFly = true;
        if (!std::strcmp(argv[i], "--no-warpfly")) g_warpFly = false;
        if (!std::strcmp(argv[i], "--warpflyufo")) g_warpFlyUfo = true;
        if (!std::strcmp(argv[i], "--warpslowonly")) g_warpSlowOnly = true;
        if (!std::strcmp(argv[i], "--no-warpslowonly")) g_warpSlowOnly = false;
        if (!std::strcmp(argv[i], "--no-warpflyufo")) g_warpFlyUfo = false;
        // --warpqueue: a touch- or spawn-triggered time warp is left out (modifiers.hpp)
        if (!std::strcmp(argv[i], "--warpqueue")) g_warpQueue = true;
        if (!std::strcmp(argv[i], "--no-warpqueue")) g_warpQueue = false;
        if (!std::strcmp(argv[i], "--warprot")) g_warpRot = true;
        if (!std::strcmp(argv[i], "--no-warprot")) g_warpRot = false;
        // --ctrltouch: a touch-triggered Options trigger's controls switch is fired by the state (modifiers.hpp)
        if (!std::strcmp(argv[i], "--ctrltouch")) g_ctrlTouch = true;
        if (!std::strcmp(argv[i], "--no-ctrltouch")) g_ctrlTouch = false;
        if (!std::strcmp(argv[i], "--ctrlwinlag")) g_ctrlWinLag = true;
        if (!std::strcmp(argv[i], "--no-ctrlwinlag")) g_ctrlWinLag = false;
        if (!std::strcmp(argv[i], "--ctrlwinp2")) g_ctrlWinP2 = true;
        if (!std::strcmp(argv[i], "--no-ctrlwinp2")) g_ctrlWinP2 = false;
        if (!std::strcmp(argv[i], "--recstall")) g_recStall = true;
        if (!std::strcmp(argv[i], "--no-recstall")) g_recStall = false;
        if (!std::strcmp(argv[i], "--portseenx")) g_portSeenX = true;
        if (!std::strcmp(argv[i], "--no-portseenx")) g_portSeenX = false;
        // --spdsameonce: a second speed portal of one speed on a tick is not marked (speed.hpp)
        if (!std::strcmp(argv[i], "--spdsameonce")) g_spdSameOnce = true;
        if (!std::strcmp(argv[i], "--no-spdsameonce")) g_spdSameOnce = false;
        if (!std::strcmp(argv[i], "--p1soloend")) g_p1SoloEnd = true;
        if (!std::strcmp(argv[i], "--no-p1soloend")) g_p1SoloEnd = false;
        if (!std::strcmp(argv[i], "--flydashring")) g_flyDashRing = true;
        if (!std::strcmp(argv[i], "--no-flydashring")) g_flyDashRing = false;
        if (!std::strcmp(argv[i], "--flyspiderring")) g_flySpiderRing = true;
        if (!std::strcmp(argv[i], "--no-flyspiderring")) g_flySpiderRing = false;
        if (!std::strcmp(argv[i], "--flypadhead")) g_flyPadHead = true;
        if (!std::strcmp(argv[i], "--no-flypadhead")) g_flyPadHead = false;
        if (!std::strcmp(argv[i], "--earlyring")) g_earlyRing = true;
        if (!std::strcmp(argv[i], "--no-earlyring")) g_earlyRing = false;
        if (!std::strcmp(argv[i], "--dashclamp")) g_dashClamp = true;
        if (!std::strcmp(argv[i], "--no-dashclamp")) g_dashClamp = false;
        if (!std::strcmp(argv[i], "--mpushside")) g_mpushSide = true;
        if (!std::strcmp(argv[i], "--no-mpushside")) g_mpushSide = false;
        if (!std::strcmp(argv[i], "--bluepadpost")) g_bluePadPost = true;
        if (!std::strcmp(argv[i], "--no-bluepadpost")) g_bluePadPost = false;
        if (!std::strcmp(argv[i], "--ringrotbox")) g_ringRotBox = true;
        if (!std::strcmp(argv[i], "--no-ringrotbox")) g_ringRotBox = false;
        if (!std::strcmp(argv[i], "--balltaplag")) g_ballTapLag = true;
        if (!std::strcmp(argv[i], "--no-balltaplag")) g_ballTapLag = false;
        if (!std::strcmp(argv[i], "--ringrotground")) g_ringRotGround = true;
        if (!std::strcmp(argv[i], "--no-ringrotground")) g_ringRotGround = false;
        if (!std::strcmp(argv[i], "--holdseed")) g_holdSeed = true;
        if (!std::strcmp(argv[i], "--no-holdseed")) g_holdSeed = false;
        if (!std::strcmp(argv[i], "--portseatorder")) g_portSeatOrder = true;
        if (!std::strcmp(argv[i], "--no-portseatorder")) g_portSeatOrder = false;
        if (!std::strcmp(argv[i], "--flyhazdefer")) g_flyHazDefer = true;
        if (!std::strcmp(argv[i], "--no-flyhazdefer")) g_flyHazDefer = false;
        if (!std::strcmp(argv[i], "--dualbornpress")) g_dualBornPress = true;
        if (!std::strcmp(argv[i], "--no-dualbornpress")) g_dualBornPress = false;
        if (!std::strcmp(argv[i], "--dualpadflip")) g_dualPadFlip = true;
        if (!std::strcmp(argv[i], "--no-dualpadflip")) g_dualPadFlip = false;
        if (!std::strcmp(argv[i], "--dualunlink")) g_dualUnlink = true;
        if (!std::strcmp(argv[i], "--no-dualunlink")) g_dualUnlink = false;
        if (!std::strcmp(argv[i], "--padoldbox")) g_padOldBox = true;
        if (!std::strcmp(argv[i], "--no-padoldbox")) g_padOldBox = false;
        if (!std::strcmp(argv[i], "--crushhead")) g_crushHead = true;
        if (!std::strcmp(argv[i], "--no-crushhead")) g_crushHead = false;
        if (!std::strcmp(argv[i], "--dashhead")) g_dashHead = true;
        if (!std::strcmp(argv[i], "--no-dashhead")) g_dashHead = false;
        if (!std::strcmp(argv[i], "--sametick")) g_sameTick = true;
        if (!std::strcmp(argv[i], "--no-sametick")) g_sameTick = false;
        if (!std::strcmp(argv[i], "--togglescale")) g_toggleScale = true;
        if (!std::strcmp(argv[i], "--no-togglescale")) g_toggleScale = false;
        if (!std::strcmp(argv[i], "--holdtap")) g_holdTap = true;
        if (!std::strcmp(argv[i], "--no-holdtap")) g_holdTap = false;
        if (!std::strcmp(argv[i], "--ridebound")) g_rideBound = true;
        if (!std::strcmp(argv[i], "--no-ridebound")) g_rideBound = false;
        if (!std::strcmp(argv[i], "--instantlag")) g_instantLag = true;
        if (!std::strcmp(argv[i], "--no-instantlag")) g_instantLag = false;
        if (!std::strcmp(argv[i], "--forcecircle")) g_forceCircle = true;
        if (!std::strcmp(argv[i], "--no-forcecircle")) g_forceCircle = false;
        if (!std::strcmp(argv[i], "--forcelive")) g_forceLive = true;
        if (!std::strcmp(argv[i], "--no-forcelive")) g_forceLive = false;
        if (!std::strcmp(argv[i], "--ringpresseat")) g_ringPressEat = true;
        if (!std::strcmp(argv[i], "--no-ringpresseat")) g_ringPressEat = false;
        if (!std::strcmp(argv[i], "--spawnringon")) g_spawnRingOn = true;
        if (!std::strcmp(argv[i], "--no-spawnringon")) g_spawnRingOn = false;
        if (!std::strcmp(argv[i], "--offafter")) g_offAfter = true;
        if (!std::strcmp(argv[i], "--no-offafter")) g_offAfter = false;
        if (!std::strcmp(argv[i], "--acthazoff")) g_actHazOff = true;
        if (!std::strcmp(argv[i], "--no-acthazoff")) g_actHazOff = false;
        if (!std::strcmp(argv[i], "--formulanolock")) g_formulaNoLock = true;
        if (!std::strcmp(argv[i], "--no-formulanolock")) g_formulaNoLock = false;
        if (!std::strcmp(argv[i], "--swingseatpress")) g_swingSeatPress = true;
        if (!std::strcmp(argv[i], "--no-swingseatpress")) g_swingSeatPress = false;
        if (!std::strcmp(argv[i], "--swingheadceil")) g_swingHeadCeil = true;
        if (!std::strcmp(argv[i], "--no-swingheadceil")) g_swingHeadCeil = false;
        if (!std::strcmp(argv[i], "--swingceilmoved")) g_swingCeilMoved = true;
        if (!std::strcmp(argv[i], "--no-swingceilmoved")) g_swingCeilMoved = false;
        if (!std::strcmp(argv[i], "--movingwallrise")) g_movingWallRise = true;
        if (!std::strcmp(argv[i], "--no-movingwallrise")) g_movingWallRise = false;
        if (!std::strcmp(argv[i], "--flyhaztele")) g_flyHazTele = true;
        if (!std::strcmp(argv[i], "--no-flyhaztele")) g_flyHazTele = false;
        if (!std::strcmp(argv[i], "--slopelandtol")) g_slopeLandTol = true;
        if (!std::strcmp(argv[i], "--no-slopelandtol")) g_slopeLandTol = false;
        if (!std::strcmp(argv[i], "--ringpobb")) g_ringPobb = true;
        if (!std::strcmp(argv[i], "--no-ringpobb")) g_ringPobb = false;
        if (!std::strcmp(argv[i], "--waveringbox")) g_waveRingBox = true;
        if (!std::strcmp(argv[i], "--no-waveringbox")) g_waveRingBox = false;
        if (!std::strcmp(argv[i], "--wavedash")) g_waveDash = true;
        if (!std::strcmp(argv[i], "--no-wavedash")) g_waveDash = false;
        if (!std::strcmp(argv[i], "--waveringmini")) g_waveRingMini = true;
        if (!std::strcmp(argv[i], "--no-waveringmini")) g_waveRingMini = false;
        if (!std::strcmp(argv[i], "--waveringminibox")) g_waveRingMiniBox = true;
        if (!std::strcmp(argv[i], "--no-waveringminibox")) g_waveRingMiniBox = false;
        if (!std::strcmp(argv[i], "--spidercrushgrace")) g_spiderCrushGrace = true;
        if (!std::strcmp(argv[i], "--no-spidercrushgrace")) g_spiderCrushGrace = false;
        if (!std::strcmp(argv[i], "--spidergraceage")) g_spiderGraceAge = true;
        if (!std::strcmp(argv[i], "--no-spidergraceage")) g_spiderGraceAge = false;
        if (!std::strcmp(argv[i], "--bandholdend")) g_bandHoldEnd = true;
        if (!std::strcmp(argv[i], "--no-bandholdend")) g_bandHoldEnd = false;
        if (!std::strcmp(argv[i], "--anchorentry")) g_anchorEntryBase = true;
        if (!std::strcmp(argv[i], "--no-anchorentry")) g_anchorEntryBase = false;
        if (!std::strcmp(argv[i], "--teleporttrig")) g_teleportTrig = true;
        if (!std::strcmp(argv[i], "--no-teleporttrig")) g_teleportTrig = false;
        // --tpsideways: a teleport trigger whose target is ahead in x is loaded too (constants.hpp)
        if (!std::strcmp(argv[i], "--tpsideways")) g_tpSideways = true;
        if (!std::strcmp(argv[i], "--no-tpsideways")) g_tpSideways = false;
        // --tpportalx: a 2902-family portal whose exit is ahead in x moves x too (constants.hpp)
        if (!std::strcmp(argv[i], "--tpportalx")) g_tpPortalX = true;
        if (!std::strcmp(argv[i], "--no-tpportalx")) g_tpPortalX = false;
        // --widescan: an object wider than the x-window scan's margin is not stepped over (constants.hpp)
        if (!std::strcmp(argv[i], "--widescan")) g_wideScan = true;
        if (!std::strcmp(argv[i], "--no-widescan")) g_wideScan = false;
        // --padundoflip: a ball's tap a pad undoes is undone for the landing passes too (speed.hpp)
        if (!std::strcmp(argv[i], "--padundoflip")) g_padUndoFlip = true;
        if (!std::strcmp(argv[i], "--no-padundoflip")) g_padUndoFlip = false;
        // --p2xoff: the second dual body keeps its own x offset (constants.hpp); --startp2dx seeds it
        if (!std::strcmp(argv[i], "--p2xoff")) g_p2xOff = true;
        if (!std::strcmp(argv[i], "--no-p2xoff")) g_p2xOff = false;
        if (!std::strcmp(argv[i], "--startp2dx") && i + 1 < argc) g_startP2dx = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--p2solo")) g_p2Solo = true;
        if (!std::strcmp(argv[i], "--no-p2solo")) g_p2Solo = false;
        // --dualendrot: a wave that ends the dual as the second body hands over its angle (constants.hpp)
        if (!std::strcmp(argv[i], "--dualendrot")) g_dualEndRot = true;
        if (!std::strcmp(argv[i], "--no-dualendrot")) g_dualEndRot = false;
        if (!std::strcmp(argv[i], "--autostop")) g_autoStop = true;
        if (!std::strcmp(argv[i], "--no-autostop")) g_autoStop = false;
        // --rotnospawn: a Rotate's walk does not enter its triggers' targets (dynamics.hpp)
        if (!std::strcmp(argv[i], "--rotnospawn")) g_rotNoSpawn = true;
        if (!std::strcmp(argv[i], "--no-rotnospawn")) g_rotNoSpawn = false;
        // --formularec: a formula-driven object reads its recording while its boxes agree (dynamics.hpp)
        if (!std::strcmp(argv[i], "--formularec")) g_formulaRec = true;
        if (!std::strcmp(argv[i], "--no-formularec")) g_formulaRec = false;
        if (!std::strcmp(argv[i], "--formularectouch")) g_formulaRecTouch = true;
        if (!std::strcmp(argv[i], "--no-formularectouch")) g_formulaRecTouch = false;
        // --recprov rec,anchor,reach: whose attempt the recording and the anchor's rows are (dynamics.hpp)
        if (!std::strcmp(argv[i], "--recprov") && i + 1 < argc) {
            long long a = -1, b = -1, c = -1;
            if (std::sscanf(argv[i + 1], "%lld,%lld,%lld", &a, &b, &c) == 3) {
                g_recProvGiven = true;
                g_recProvRec = a;
                g_recProvAnchor = b;
                g_recProvReach = c;
            }
        }
        if (!std::strcmp(argv[i], "--dynobbrot")) g_dynObbRot = true;
        if (!std::strcmp(argv[i], "--no-dynobbrot")) g_dynObbRot = false;
        if (!std::strcmp(argv[i], "--reclagup")) g_recLagUp = true;
        if (!std::strcmp(argv[i], "--no-reclagup")) g_recLagUp = false;
        if (!std::strcmp(argv[i], "--reclagabs")) g_recLagAbs = true;
        if (!std::strcmp(argv[i], "--no-reclagabs")) g_recLagAbs = false;
        if (!std::strcmp(argv[i], "--ceilgracecube")) g_ceilGraceCube = true;
        if (!std::strcmp(argv[i], "--no-ceilgracecube")) g_ceilGraceCube = false;
        if (!std::strcmp(argv[i], "--gravtrig")) g_gravTrig = true;
        if (!std::strcmp(argv[i], "--no-gravtrig")) g_gravTrig = false;
        if (!std::strcmp(argv[i], "--followy")) g_followYOn = true;
        if (!std::strcmp(argv[i], "--no-followy")) g_followYOn = false;
        if (!std::strcmp(argv[i], "--teleportorb")) g_teleportOrb = true;
        if (!std::strcmp(argv[i], "--no-teleportorb")) g_teleportOrb = false;
        if (!std::strcmp(argv[i], "--tppush")) g_tpPush = true;
        if (!std::strcmp(argv[i], "--no-tppush")) g_tpPush = false;
        if (!std::strcmp(argv[i], "--tpkeepy")) g_tpKeepY = true;
        if (!std::strcmp(argv[i], "--no-tpkeepy")) g_tpKeepY = false;
        if (!std::strcmp(argv[i], "--nocolone")) g_noColOneWay = true;
        if (!std::strcmp(argv[i], "--no-nocolone")) g_noColOneWay = false;
        if (!std::strcmp(argv[i], "--tporder")) g_tpOrder = true;
        if (!std::strcmp(argv[i], "--no-tporder")) g_tpOrder = false;
        if (!std::strcmp(argv[i], "--slamdedup")) g_slamDedup = true;
        if (!std::strcmp(argv[i], "--no-slamdedup")) g_slamDedup = false;
        // --slambox: a slam step is skipped by the recorded attempt's own box (constants.hpp)
        if (!std::strcmp(argv[i], "--slambox")) g_slamBox = true;
        if (!std::strcmp(argv[i], "--no-slambox")) g_slamBox = false;
        // --rectouch uid:tick,... ("-" = none): the boxes GD fired in the recorded attempt (dynamics.hpp)
        if (!std::strcmp(argv[i], "--rectouch") && i + 1 < argc) {
            g_recTouchGiven = true;
            g_recTouch.clear();
            const std::string v = argv[i + 1];
            size_t p = 0;
            while (p < v.size()) {
                size_t q = v.find(',', p);
                if (q == std::string::npos) q = v.size();
                const std::string tok = v.substr(p, q - p);
                const int u = std::atoi(tok.c_str());
                const size_t c = tok.find(':');
                if (u > 0) g_recTouch[u] = (c != std::string::npos) ? std::atoi(tok.c_str() + c + 1) : -1;
                p = q + 1;
            }
        }
        if (!std::strcmp(argv[i], "--holddedup")) g_holdDedup = true;
        if (!std::strcmp(argv[i], "--no-holddedup")) g_holdDedup = false;
        if (!std::strcmp(argv[i], "--boxparts")) g_boxParts = true;
        if (!std::strcmp(argv[i], "--moverextend")) g_moverExtend = true;
        if (!std::strcmp(argv[i], "--no-moverextend")) g_moverExtend = false;
        if (!std::strcmp(argv[i], "--moverextendvel")) g_moverExtendVel = true;
        if (!std::strcmp(argv[i], "--flipcollide")) g_flipCollide = true;
        if (!std::strcmp(argv[i], "--no-flipcollide")) g_flipCollide = false;
        if (!std::strcmp(argv[i], "--mpushflip")) g_mpushFlip = true;
        if (!std::strcmp(argv[i], "--no-mpushflip")) g_mpushFlip = false;
        if (!std::strcmp(argv[i], "--swingbandceil")) g_swingBandCeil = true;
        if (!std::strcmp(argv[i], "--no-swingbandceil")) g_swingBandCeil = false;
        if (!std::strcmp(argv[i], "--slopestick")) g_slopeStick = true;
        if (!std::strcmp(argv[i], "--no-slopestick")) g_slopeStick = false;
        if (!std::strcmp(argv[i], "--recendextend")) g_recEndExtend = true;
        if (!std::strcmp(argv[i], "--no-recendextend")) g_recEndExtend = false;
        if (!std::strcmp(argv[i], "--recendresid")) g_recEndResid = true;
        if (!std::strcmp(argv[i], "--no-recendresid")) g_recEndResid = false;
        if (!std::strcmp(argv[i], "--hazrectgate")) g_hazRectGate = true;
        if (!std::strcmp(argv[i], "--no-hazrectgate")) g_hazRectGate = false;
        if (!std::strcmp(argv[i], "--recanchorx")) g_recAnchorX = true;
        if (!std::strcmp(argv[i], "--no-recanchorx")) g_recAnchorX = false;
        if (!std::strcmp(argv[i], "--rotlag")) g_rotLag = true;
        if (!std::strcmp(argv[i], "--no-rotlag")) g_rotLag = false;
        if (!std::strcmp(argv[i], "--lockstop")) g_lockStop = true;
        if (!std::strcmp(argv[i], "--no-lockstop")) g_lockStop = false;
        if (!std::strcmp(argv[i], "--rottail")) g_rotTail = true;
        if (!std::strcmp(argv[i], "--no-rottail")) g_rotTail = false;
        if (!std::strcmp(argv[i], "--recendtail")) g_recEndTail = true;
        if (!std::strcmp(argv[i], "--no-recendtail")) g_recEndTail = false;
        if (!std::strcmp(argv[i], "--rotobb")) g_rotObb = true;
        if (!std::strcmp(argv[i], "--no-rotobb")) g_rotObb = false;
        if (!std::strcmp(argv[i], "--rotobblatch")) g_rotObbLatch = true;
        if (!std::strcmp(argv[i], "--no-rotobblatch")) g_rotObbLatch = false;
        if (!std::strcmp(argv[i], "--ctlautotail")) g_ctlAutoTail = true;
        if (!std::strcmp(argv[i], "--no-ctlautotail")) g_ctlAutoTail = false;
        if (!std::strcmp(argv[i], "--headbandfresh")) g_headBandFresh = true;
        if (!std::strcmp(argv[i], "--no-headbandfresh")) g_headBandFresh = false;
        if (!std::strcmp(argv[i], "--no-boxparts")) g_boxParts = false;
        if (!std::strcmp(argv[i], "--slidelive")) g_slideLive = true;
        if (!std::strcmp(argv[i], "--no-slidelive")) g_slideLive = false;
        if (!std::strcmp(argv[i], "--slideend")) g_slideEnd = true;
        if (!std::strcmp(argv[i], "--no-slideend")) g_slideEnd = false;
        if (!std::strcmp(argv[i], "--waveramp")) g_waveRamp = true;
        if (!std::strcmp(argv[i], "--no-waveramp")) g_waveRamp = false;
        if (!std::strcmp(argv[i], "--waveslopewall")) g_waveSlopeWall = true;
        if (!std::strcmp(argv[i], "--no-waveslopewall")) g_waveSlopeWall = false;
        if (!std::strcmp(argv[i], "--lockx")) g_lockX = true;
        if (!std::strcmp(argv[i], "--no-lockx")) g_lockX = false;
        if (!std::strcmp(argv[i], "--anchormove")) g_anchorMove = true;
        if (!std::strcmp(argv[i], "--no-anchormove")) g_anchorMove = false;
        if (!std::strcmp(argv[i], "--spiderbandground")) g_spiderBandGround = true;
        if (!std::strcmp(argv[i], "--no-spiderbandground")) g_spiderBandGround = false;
        // ...on by default since 2026-10; these are the off arms
        if (!std::strcmp(argv[i], "--no-spiderpad")) g_spiderPad = false;
        if (!std::strcmp(argv[i], "--no-waveobbaabb")) g_waveObbAabb = false;
        if (!std::strcmp(argv[i], "--no-wavehazendpoint")) g_waveHazEndpoint = false;
        if (!std::strcmp(argv[i], "--no-waveoneway")) g_waveOneway = false;
        if (!std::strcmp(argv[i], "--no-tpspawn")) g_tpSpawn = false;
        if (!std::strcmp(argv[i], "--no-padtouch")) g_padTouch = false;
        if (!std::strcmp(argv[i], "--no-fixgravthr")) g_fixGravThr = false;
        if (!std::strcmp(argv[i], "--no-gravtoggle")) g_gravToggle = false;
        if (!std::strcmp(argv[i], "--no-portalstarty")) g_portalStartY = false;
        if (!std::strcmp(argv[i], "--no-boxoff")) g_boxOff = false;
        if (!std::strcmp(argv[i], "--bandowntrack")) g_bandOwnTrack = true;
        if (!std::strcmp(argv[i], "--bandtrackseen")) g_bandTrackSeen = true;
        if (!std::strcmp(argv[i], "--no-bandtrackseen")) g_bandTrackSeen = false;
        // --bandsrc: a recorded portal band holds a flyer from either side (bands.hpp)
        if (!std::strcmp(argv[i], "--bandsrc")) g_bandSrc = true;
        if (!std::strcmp(argv[i], "--no-bandsrc")) g_bandSrc = false;
        if (!std::strcmp(argv[i], "--boxoff")) g_boxOff = true;
        // --boxoffown: an off box's objects keep the recording's "gone" only once it fired (dynamics.hpp)
        if (!std::strcmp(argv[i], "--boxoffown")) g_boxOffOwn = true;
        if (!std::strcmp(argv[i], "--no-boxoffown")) g_boxOffOwn = false;
        // --ringtaplink: a ring's gravity turn reaches the partner past a same-tick tap (constants.hpp)
        if (!std::strcmp(argv[i], "--ringtaplink")) g_ringTapLink = true;
        if (!std::strcmp(argv[i], "--no-ringtaplink")) g_ringTapLink = false;
        if (!std::strcmp(argv[i], "--rtlp2ring")) g_rtlP2Ring = true;
        if (!std::strcmp(argv[i], "--no-rtlp2ring")) g_rtlP2Ring = false;
        if (!std::strcmp(argv[i], "--acttele")) g_actTele = true;
        if (!std::strcmp(argv[i], "--no-acttele")) g_actTele = false;
        if (!std::strcmp(argv[i], "--pressfresh")) g_pressFresh = true;
        if (!std::strcmp(argv[i], "--no-pressfresh")) g_pressFresh = false;
        // --flyheldbox: a ship or UFO holding into a toggle block does not fire it (triggers.hpp)
        if (!std::strcmp(argv[i], "--flyheldbox")) g_flyHeldBox = true;
        if (!std::strcmp(argv[i], "--no-flyheldbox")) g_flyHeldBox = false;
        if (!std::strcmp(argv[i], "--portalstarty")) g_portalStartY = true;
        if (!std::strcmp(argv[i], "--cubempush")) g_cubeMpush = true;
        if (!std::strcmp(argv[i], "--cubempushnoground")) g_cubeMpushNoGround = true;
        if (!std::strcmp(argv[i], "--no-cubempushnoground")) g_cubeMpushNoGround = false;
        // --rampslablift: a face climbing into a ramp rider puts it on top at its speed (constants.hpp)
        if (!std::strcmp(argv[i], "--rampslablift")) g_rampSlabLift = true;
        if (!std::strcmp(argv[i], "--no-rampslablift")) g_rampSlabLift = false;
        // --headpushhold: cubempush's head side holds a body at vy 0 under a face that keeps coming down
        if (!std::strcmp(argv[i], "--headpushhold")) g_headPushHold = true;
        if (!std::strcmp(argv[i], "--no-headpushhold")) g_headPushHold = false;
        if (!std::strcmp(argv[i], "--mpushupright")) g_mPushUpright = true;
        if (!std::strcmp(argv[i], "--no-mpushupright")) g_mPushUpright = false;
        if (!std::strcmp(argv[i], "--hazafterportal")) g_hazAfterPortal = true;
        if (!std::strcmp(argv[i], "--no-hazafterportal")) g_hazAfterPortal = false;
        if (!std::strcmp(argv[i], "--hazportalonce")) g_hazPortalOnce = true;
        if (!std::strcmp(argv[i], "--no-hazportalonce")) g_hazPortalOnce = false;
        if (!std::strcmp(argv[i], "--padafterportal")) g_padAfterPortal = true;
        if (!std::strcmp(argv[i], "--no-padafterportal")) g_padAfterPortal = false;
        if (!std::strcmp(argv[i], "--portreread")) g_portReRead = true;
        if (!std::strcmp(argv[i], "--no-portreread")) g_portReRead = false;
        if (!std::strcmp(argv[i], "--movingwallseat")) g_movingWallSeat = true;
        if (!std::strcmp(argv[i], "--no-movingwallseat")) g_movingWallSeat = false;
        if (!std::strcmp(argv[i], "--movingwallhead")) g_movingWallHead = true;
        if (!std::strcmp(argv[i], "--no-movingwallhead")) g_movingWallHead = false;
        if (!std::strcmp(argv[i], "--dualbornportal")) g_dualBornPortal = true;
        if (!std::strcmp(argv[i], "--no-dualbornportal")) g_dualBornPortal = false;
        if (!std::strcmp(argv[i], "--movelockx")) g_moveLockX = true;
        if (!std::strcmp(argv[i], "--no-movelockx")) g_moveLockX = false;
        if (!std::strcmp(argv[i], "--waveportalfree")) g_wavePortalFree = true;
        if (!std::strcmp(argv[i], "--no-waveportalfree")) g_wavePortalFree = false;
        if (!std::strcmp(argv[i], "--sawexactwave")) g_sawExactWave = true;
        if (!std::strcmp(argv[i], "--no-sawexactwave")) g_sawExactWave = false;
        if (!std::strcmp(argv[i], "--sawexactufo")) g_sawExactUfo = true;
        if (!std::strcmp(argv[i], "--no-sawexactufo")) g_sawExactUfo = false;
        if (!std::strcmp(argv[i], "--sawexactcube")) g_sawExactCube = true;
        if (!std::strcmp(argv[i], "--no-sawexactcube")) g_sawExactCube = false;
        // --armpost: the 1859 arm reads the post-move box (constants.hpp)
        if (!std::strcmp(argv[i], "--armpost")) g_armPost = true;
        if (!std::strcmp(argv[i], "--no-armpost")) g_armPost = false;
        // --fgpost: the 2866 arm the same way, and read on the touch tick (constants.hpp)
        if (!std::strcmp(argv[i], "--fgpost")) g_fgPost = true;
        if (!std::strcmp(argv[i], "--no-fgpost")) g_fgPost = false;
        if (!std::strcmp(argv[i], "--rampendclick")) g_rampEndClick = true;
        if (!std::strcmp(argv[i], "--no-rampendclick")) g_rampEndClick = false;
        // --exitjump: a cube that rode the ramp last tick jumps on the exit tick (constants.hpp)
        if (!std::strcmp(argv[i], "--exitjump")) g_exitJump = true;
        if (!std::strcmp(argv[i], "--no-exitjump")) g_exitJump = false;
        if (!std::strcmp(argv[i], "--seatspeed")) g_seatSpeed = true;
        if (!std::strcmp(argv[i], "--no-seatspeed")) g_seatSpeed = false;
        if (!std::strcmp(argv[i], "--no-cubempush")) g_cubeMpush = false;
        if (!std::strcmp(argv[i], "--dropground")) g_dropGround = true;
        if (!std::strcmp(argv[i], "--no-dropground")) g_dropGround = false;   // on by default
        // --seatc6: a flyer on a moved top face writes c6 and is launched from it (constants.hpp)
        if (!std::strcmp(argv[i], "--seatc6")) g_seatC6 = true;
        if (!std::strcmp(argv[i], "--no-seatc6")) g_seatC6 = false;
        // --ridec6: a ground-mode rider on a moved face writes c6 too (constants.hpp)
        if (!std::strcmp(argv[i], "--ridec6")) g_rideC6 = true;
        if (!std::strcmp(argv[i], "--no-ridec6")) g_rideC6 = false;
        // --orbswing: the yellow / pink orb's swing x0.6 (constants.hpp)
        if (!std::strcmp(argv[i], "--orbswing")) g_orbSwing = true;
        if (!std::strcmp(argv[i], "--no-orbswing")) g_orbSwing = false;
        // --flyclampreach: a body the band ceiling pulled down is offered to a rising face again (constants.hpp)
        if (!std::strcmp(argv[i], "--flyclampreach")) g_flyClampReach = true;
        if (!std::strcmp(argv[i], "--no-flyclampreach")) g_flyClampReach = false;
        // --escdyn: escapee-prune also reads the moving objects' tops (search_key.hpp)
        if (!std::strcmp(argv[i], "--escdyn")) g_escDyn = true;
        if (!std::strcmp(argv[i], "--no-escdyn")) g_escDyn = false;
        // --goalspare: keep a body above the level that reaches the goal first (speed.hpp)
        if (!std::strcmp(argv[i], "--goalspare")) g_goalSpare = 1;
        if (!std::strcmp(argv[i], "--no-goalspare")) g_goalSpare = 0;   // on by default
        if (!std::strcmp(argv[i], "--goalgd")) g_goalGd = true;
        if (!std::strcmp(argv[i], "--no-goalgd")) g_goalGd = false;
        if (!std::strcmp(argv[i], "--upsidecoyote")) g_upsideCoyote = 1;
        if (!std::strcmp(argv[i], "--no-upsidecoyote")) g_upsideCoyote = 0;
        if (!std::strcmp(argv[i], "--rawvalues")) g_rawValues = 1;
        if (!std::strcmp(argv[i], "--no-rawvalues")) g_rawValues = 0;
        // --portalunpin: a gravity portal off a block keeps the tick's step in y (speed.hpp)
        if (!std::strcmp(argv[i], "--portalunpin")) g_portalUnpin = 1;   // on by default
        if (!std::strcmp(argv[i], "--no-portalunpin")) g_portalUnpin = 0;
        // --hazendpoint: the cube's and flying body's hazard test at the tick's end only (speed.hpp)
        if (!std::strcmp(argv[i], "--hazendpoint")) g_hazEndpoint = 1;   // on by default
        if (!std::strcmp(argv[i], "--no-hazendpoint")) g_hazEndpoint = 0;
        // --portalpress: a press on a mode portal's tick belongs to the new mode (speed.hpp)
        if (!std::strcmp(argv[i], "--portalpress")) g_portalPress = 1;   // on by default
        if (!std::strcmp(argv[i], "--no-portalpress")) g_portalPress = 0;
        // --padtable: pad launches from GD's per-mode table (speed.hpp)
        if (!std::strcmp(argv[i], "--padtable")) g_padTable = 1;
        if (!std::strcmp(argv[i], "--no-padtable")) g_padTable = 0;
        if (!std::strcmp(argv[i], "--ringonce") || !std::strcmp(argv[i], "--portalonce")
            || !std::strcmp(argv[i], "--ringorder") || !std::strcmp(argv[i], "--holdlatch")
            || !std::strcmp(argv[i], "--ringfirst") || !std::strcmp(argv[i], "--padfirst")
            || !std::strcmp(argv[i], "--portalflap") || !std::strcmp(argv[i], "--ringpresspin")
            || !std::strcmp(argv[i], "--ballbuffer") || !std::strcmp(argv[i], "--ringbuffer")
            || !std::strcmp(argv[i], "--groundcubejump")
            || !std::strcmp(argv[i], "--gravlandtap") || !std::strcmp(argv[i], "--gravpadchain")
            || !std::strcmp(argv[i], "--onewayceil") || !std::strcmp(argv[i], "--padsection")
            || !std::strcmp(argv[i], "--solidorder") || !std::strcmp(argv[i], "--ballceil")
            || !std::strcmp(argv[i], "--ballceilflip") || !std::strcmp(argv[i], "--snaponeway")
            || !std::strcmp(argv[i], "--flyreland") || !std::strcmp(argv[i], "--padthenland")
            || !std::strcmp(argv[i], "--faceclass") || !std::strcmp(argv[i], "--portalceil")
            || !std::strcmp(argv[i], "--flipgrace") || !std::strcmp(argv[i], "--padflipland")
            || !std::strcmp(argv[i], "--flyrings") || !std::strcmp(argv[i], "--sawexact")
            || !std::strcmp(argv[i], "--snaptable") || !std::strcmp(argv[i], "--sawexactship")
            || !std::strcmp(argv[i], "--groundgrow") || !std::strcmp(argv[i], "--ringeither")
            || !std::strcmp(argv[i], "--growaway") || !std::strcmp(argv[i], "--sizepress")
            || !std::strcmp(argv[i], "--ballgrowlag")) continue;
        // --heldall: State::held tracks the button in every mode (speed.hpp)
        if (!std::strcmp(argv[i], "--heldall")) g_heldAll = 1;
        if (!std::strcmp(argv[i], "--no-heldall")) g_heldAll = 0;
        // --heldcellcap: under --heldall the alive cap counts cells, not button twins (speed.hpp)
        if (!std::strcmp(argv[i], "--heldcellcap")) g_heldCellCap = 1;
        // --heldcelltwins N: ...and keeps at most N states of a sampled cell (speed.hpp)
        if (i + 1 < argc && !std::strcmp(argv[i], "--heldcelltwins"))
            g_heldCellTwins = std::atoi(argv[++i]);
        if (!std::strcmp(argv[i], "--fgarmlive")) g_fgArmLive = true;
        if (!std::strcmp(argv[i], "--trigdump")) g_trigDump = true;
        if (!std::strcmp(argv[i], "--stopdump")) g_stopDump = true;   // print only
        if (!std::strcmp(argv[i], "--trigeffect")) g_trigEffect = true;  // print only
        if (!std::strcmp(argv[i], "--obb-all")) g_obbAll = true;
        if (!std::strcmp(argv[i], "--no-obb-all")) g_obbAll = false;
        if (!std::strcmp(argv[i], "--memstat")) g_memStat = true;
        // --phaseprof: wall time per phase of the layer loop, printed once at the end. Print only.
        if (!std::strcmp(argv[i], "--phaseprof")) g_phaseProf = true;
        if (!std::strcmp(argv[i], "--groupsigcheck")) g_groupSigCheck = true;
        if (!std::strcmp(argv[i], "--levelcachecheck")) g_levelCacheCheck = true;   // debug, ReplayLevelCache   // debug, groupLayersFor (cleared below each call)
        if (!std::strcmp(argv[i], "--firebcheck")) g_fireBCheck = true;
        if (!std::strcmp(argv[i], "--groupfire")) g_groupFire = true;
        if (!std::strcmp(argv[i], "--groupxsplit")) g_groupXSplit = true;
        if (!std::strcmp(argv[i], "--no-groupxsplit")) g_groupXSplit = false;
        // --witnessauto (search_key.hpp): the witness fires the autonomous triggers itself.
        if (!std::strcmp(argv[i], "--witnessauto")) g_witnessAuto = true;
        if (!std::strcmp(argv[i], "--no-witnessauto")) g_witnessAuto = false;
        if (!std::strcmp(argv[i], "--rotqueue")) g_rotQueue = true;
        if (!std::strcmp(argv[i], "--startrotq")) {
            const char* p = argv[i + 1];
            g_startRotChan = std::atoi(p);
            const char* c = std::strchr(p, ',');
            if (c) {
                g_startRotRev = (unsigned)std::strtoul(c + 1, nullptr, 16);
                p = std::strchr(c + 1, ',');
                while (p) {
                    g_startRotSpent.push_back(std::atoi(p + 1));
                    p = std::strchr(p + 1, ',');
                }
            }
        }
        // --seeddump <t>: the accumulated-field line, for the seeding check.
        if (!std::strcmp(argv[i], "--seeddump")) g_seedDump = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--seedevery")) g_seedEvery = std::atoi(argv[i + 1]);
        // --p2touch: count touch boxes the second player enters (diagnostic).
        if (!std::strcmp(argv[i], "--p2touch")) g_p2Touch = true;
        if (!std::strcmp(argv[i], "--no-spentpad")) g_spentPadSeed = false;
        if (!std::strcmp(argv[i], "--spentpadhold")) g_spentPadHold = true;
        if (!std::strcmp(argv[i], "--no-spentpadhold")) g_spentPadHold = false;
        // --anchor-state / --seed-partial-ok: the anchor payload (frames.hpp).
        if (!std::strcmp(argv[i], "--anchor-state")) g_anchorState = argv[i + 1];
        if (!std::strcmp(argv[i], "--touchseed") && i + 1 < argc) g_touchSeedArg = argv[i + 1];
        // --xtrack <file>: GD's x per tick behind the anchor (g_xTrack).
        if (!std::strcmp(argv[i], "--xtrack") && i + 1 < argc) {
            g_xTrack.clear();
            std::ifstream xf(argv[i + 1]);
            std::string ln;
            int ct = 0, cx = 1;
            bool first = true;
            while (std::getline(xf, ln)) {
                std::vector<std::string> f;
                size_t p = 0;
                while (true) {
                    const size_t q = ln.find(',', p);
                    f.push_back(ln.substr(p, q == std::string::npos ? std::string::npos : q - p));
                    if (q == std::string::npos) break;
                    p = q + 1;
                }
                if (first) {
                    first = false;
                    bool hdr = false;
                    for (size_t k = 0; k < f.size(); ++k) {
                        if (f[k] == "tick") { ct = (int)k; hdr = true; }
                        if (f[k] == "x") { cx = (int)k; hdr = true; }
                    }
                    if (hdr) continue;
                }
                if ((int)f.size() <= std::max(ct, cx)) continue;
                g_xTrack.push_back({std::atoi(f[ct].c_str()), std::atof(f[cx].c_str())});
            }
            std::printf("xtrack: %zu rows from %s\n", g_xTrack.size(), argv[i + 1]);
        }
        if (!std::strcmp(argv[i], "--touchentered") && i + 1 < argc) {
            g_touchEnteredGiven = true;
            g_touchEntered.clear();
            g_touchEnteredT.clear();
            const std::string v = argv[i + 1];
            size_t p = 0;
            while (p < v.size()) {
                size_t q = v.find(',', p);
                if (q == std::string::npos) q = v.size();
                const std::string tok = v.substr(p, q - p);
                const int u = std::atoi(tok.c_str());
                if (u > 0) {
                    g_touchEntered.insert(u);
                    const size_t c = tok.find(':');
                    if (c != std::string::npos)
                        g_touchEnteredT[u] = std::atoi(tok.c_str() + c + 1);
                }
                p = q + 1;
            }
        }
        if (!std::strcmp(argv[i], "--seed-partial-ok")) g_seedPartialOk = true;
        if (!std::strcmp(argv[i], "--shiftstat")) g_shiftStat = true;
        if (!std::strcmp(argv[i], "--clearprobe")) g_clearProbe = true;
        if (!std::strcmp(argv[i], "--refwatch") && i + 1 < argc) {
            if (!loadRefTrace(argv[i + 1]))
                std::printf("refwatch: cannot read %s\n", argv[i + 1]);
            else {
                g_refWatch = true;
                std::printf("refwatch: %zu reference rows\n", g_refRows.size());
            }
        }
        // --rejoinwatch <trace.csv> / --rejoinafter <tick>: print only (refwatch.hpp).
        if (!std::strcmp(argv[i], "--rejoinwatch") && i + 1 < argc) {
            if (!loadTraceInto(argv[i + 1], g_rjRows))
                std::printf("rejoin: cannot read %s\n", argv[i + 1]);
            else
                g_rjOn = true;
        }
        if (!std::strcmp(argv[i], "--rejoinuse")) g_rjUse = true;
        if (!std::strcmp(argv[i], "--rejoinprobe") && i + 1 < argc) g_rejoinProbe = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--no-rejoinprobe")) g_rejoinProbe = 0;
        if (!std::strcmp(argv[i], "--rejoinfull")) g_rjFull = true;
        if (!std::strcmp(argv[i], "--rejoinafter") && i + 1 < argc)
            g_rjAfter = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--refeps") && i + 1 < argc)
            g_refEps = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--no-padplayerrot")) g_noPadPlayerRot = true;
        if (!std::strcmp(argv[i], "--no-satrotraw")) g_noSatRotRaw = true;
        if (!std::strcmp(argv[i], "--rotqtoggle")) g_rotQToggle = true;
        if (!std::strcmp(argv[i], "--touchwinahead")) g_touchWinAhead = true;
        if (!std::strcmp(argv[i], "--no-touchwinahead")) g_touchWinAhead = false;
        if (!std::strcmp(argv[i], "--spawnlag")) g_spawnLag = true;   // dynamics.hpp
        if (!std::strcmp(argv[i], "--no-spawnlag")) g_spawnLag = false;
        if (!std::strcmp(argv[i], "--touchcensus")) g_touchCensus = true;   // value-less, same reason
        if (!std::strcmp(argv[i], "--lagfit")) g_lagFitDbg = true;   // print only
        if (!std::strcmp(argv[i], "--keycensus")) g_keyCensus = true;
        if (!std::strcmp(argv[i], "--searchcensus")) g_searchCensus = 16;
        if (!std::strcmp(argv[i], "--autospread")) g_autoSpread = true;
        // --autoown: each group is placed by its own crossing of the autonomous triggers (dynamics.hpp)
        if (!std::strcmp(argv[i], "--autoown")) g_autoOwn = true;
        if (!std::strcmp(argv[i], "--no-autoown")) g_autoOwn = false;
        // --twinaudit (search_census.hpp): value-less.
        if (!std::strcmp(argv[i], "--twinaudit")) g_twinAudit = true;
        if (!std::strcmp(argv[i], "--fewpress")) g_fewPress = true;
        if (!std::strcmp(argv[i], "--roomy")) g_roomy = true;
        if (!std::strcmp(argv[i], "--heldkeyread")) g_heldKeyRead = true;   // value-less
        if (!std::strcmp(argv[i], "--no-heldkeyread")) g_heldKeyRead = false;
        if (!std::strcmp(argv[i], "--trigraw")) g_trigRaw = true;
        if (!std::strcmp(argv[i], "--groupholddeath")) g_groupHoldDeath = true;
        if (!std::strcmp(argv[i], "--ceilpush")) g_ceilPush = true;   // value-less, same reason
        if (!std::strcmp(argv[i], "--dualband")) g_dualBand = true;   // value-less too
        if (!std::strcmp(argv[i], "--no-dualband")) g_dualBand = false;
        if (!std::strcmp(argv[i], "--dualslide")) g_dualSlide = true;   // value-less too
        if (!std::strcmp(argv[i], "--no-dualslide")) g_dualSlide = false;
        if (!std::strcmp(argv[i], "--dualhover")) g_dualHover = true;
        if (!std::strcmp(argv[i], "--no-dualhover")) g_dualHover = false;
        if (!std::strcmp(argv[i], "--rotlatch")) g_rotLatch = true;
        if (!std::strcmp(argv[i], "--no-rotlatch")) g_rotLatch = false;
        if (!std::strcmp(argv[i], "--anchorride")) g_anchorRide = true;   // on by default
        if (!std::strcmp(argv[i], "--no-anchorride")) g_anchorRide = false;
        if (i + 1 < argc && !std::strcmp(argv[i], "--flipcldbg"))
            g_flipLandDbgLeft = std::atoi(argv[++i]);   // print only
        if (!std::strcmp(argv[i], "--bonkarm")) {}
        if (!std::strcmp(argv[i], "--vetophys")) g_vetoPhys = true;   // value-less, same reason
        if (!std::strcmp(argv[i], "--resimladder")) g_resimLadder = true;
        if (!std::strcmp(argv[i], "--no-resimladder")) g_resimLadder = false;
        if (!std::strcmp(argv[i], "--resimladderpartial")) g_resimLadderPartial = true;
        if (!std::strcmp(argv[i], "--no-resimladderpartial")) g_resimLadderPartial = false;
        if (!std::strcmp(argv[i], "--verdictinfo")) g_verdictInfo = true;   // value-less, same reason
        if (!std::strcmp(argv[i], "--needtrig-unseen")) g_needUnseen = true;
        if (!std::strcmp(argv[i], "--airpress")) g_airPress = true;   // value-less (g_airPress)
        if (!std::strcmp(argv[i], "--refadopt")) g_refAdopt = true;   // value-less (g_refAdopt)
        if (!std::strcmp(argv[i], "--refadoptmem")) g_refAdopt = g_refAdoptMem = true;   // value-less
        if (!std::strcmp(argv[i], "--tracestate")) g_traceState = true;   // value-less (refwatch.hpp)
        // --walkresume is the ladder's (cliMain reads it into g_wrc); --walkresumecheck is per call.
        if (!std::strcmp(argv[i], "--walkresumecheck")) g_walkResumeCheck = true;   // value-less
        if (!std::strcmp(argv[i], "--no-airpress")) g_airPress = false;
        // --replayon (diagnostic): the replay walks past its first death (g_replayOn).
        if (!std::strcmp(argv[i], "--replayon")) g_replayOn = true;
        if (!std::strcmp(argv[i], "--banddbg")) g_bandDbg = true;
        if (!std::strcmp(argv[i], "--slopedbg")) g_slopeDbg = true;
            if (!std::strcmp(argv[i], "--slopereldbg")) g_slopeRelDbg = true;
            if (!std::strcmp(argv[i], "--vywriter") && i + 1 < argc) g_vyWatchT = std::atoll(argv[++i]);
            if (!std::strcmp(argv[i], "--vywriter2") && i + 1 < argc) g_vyWatchT2 = std::atoll(argv[++i]);
            if (!std::strcmp(argv[i], "--fxwatch") && i + 1 < argc) g_fxWatchT = std::atoll(argv[++i]);
            if (!std::strcmp(argv[i], "--rotwatch") && i + 1 < argc
                && std::sscanf(argv[++i], "%lld,%lld", &g_rotWatchLo, &g_rotWatchHi) != 2)
                g_rotWatchLo = g_rotWatchHi = -1;
            if (!std::strcmp(argv[i], "--qfoldwatch") && i + 1 < argc
                && std::sscanf(argv[++i], "%lld,%lld", &g_qfoldLo, &g_qfoldHi) != 2)
                g_qfoldLo = g_qfoldHi = -1;
        if (!std::strcmp(argv[i], "--dcydbg")) g_dcyDbg = true;
        if (!std::strcmp(argv[i], "--supdbg")) g_supDbg = true;
        if (!std::strcmp(argv[i], "--spddbg")) g_spdDbg = true;
        if (!std::strcmp(argv[i], "--rotcheck")) g_rotCheck = true;
        if (!std::strcmp(argv[i], "--no-rotcompute")) g_rotCompute = false;
        if (!std::strcmp(argv[i], "--no-rotsplit")) g_rotSplit = false;
        if (!std::strcmp(argv[i], "--stripvetoorder")) g_stripVetoOrder = true;
        if (!std::strcmp(argv[i], "--no-stripvetoorder")) g_stripVetoOrder = false;
        // --ringspendflap: a ring that takes the press takes the portal's buffered flap too (frames.hpp)
        if (!std::strcmp(argv[i], "--ringspendflap")) g_ringSpendFlap = true;
        if (!std::strcmp(argv[i], "--no-ringspendflap")) g_ringSpendFlap = false;
        // --ballportalflap: a ball's unspent press flaps the UFO a portal makes of it (frames.hpp)
        if (!std::strcmp(argv[i], "--ballportalflap")) g_ballPortalFlap = true;
        if (!std::strcmp(argv[i], "--no-ballportalflap")) g_ballPortalFlap = false;
        // --bvar9kill: a UFO meeting the other side's ramp after a ride dies on it (frames.hpp)
        if (!std::strcmp(argv[i], "--bvar9kill")) g_bvar9Kill = true;
        if (!std::strcmp(argv[i], "--no-bvar9kill")) g_bvar9Kill = false;
        // --spiderringfirst: a grounded spider's press inside any ring goes to the ring (frames.hpp)
        if (!std::strcmp(argv[i], "--spiderringfirst")) g_spiderRingFirst = true;
        if (!std::strcmp(argv[i], "--no-spiderringfirst")) g_spiderRingFirst = false;
        // --flipceilball: a flipped ball rides a ceiling ramp's line on as the cube does (frames.hpp)
        if (!std::strcmp(argv[i], "--flipceilball")) g_flipCeilBall = true;
        if (!std::strcmp(argv[i], "--no-flipceilball")) g_flipCeilBall = false;
        // --locknodx: a lockToPlayerX Move's x offset is not added to the object (dynamics.hpp)
        if (!std::strcmp(argv[i], "--locknodx")) g_lockNoDx = true;
        if (!std::strcmp(argv[i], "--no-locknodx")) g_lockNoDx = false;
        if (!std::strcmp(argv[i], "--ladderovercap")) g_ladderOverCap = true;
        if (!std::strcmp(argv[i], "--no-ladderovercap")) g_ladderOverCap = false;
        // --capenvelope: a thinned cap class keeps its lowest and highest state (g_capEnvelope).
        if (!std::strcmp(argv[i], "--capenvelope")) g_capEnvelope = true;
        if (!std::strcmp(argv[i], "--coins")) g_coinRoute = true;
        // --worldgates: the item gates are part of the world without --coins too (g_worldGates).
        if (!std::strcmp(argv[i], "--worldgates")) g_worldGates = true;
    }
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--threads")) g_threads = std::atoi(argv[i + 1]);
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--dodgemin"))
            g_portalDodgeMin = std::atof(argv[i + 1]);
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--speeddodge"))
            g_speedDodgeMin = std::atof(argv[i + 1]);
    // --glitchportal <px> / --glitchwave <px>: the glitch-avoid mode (thread_pool.hpp)
    for (int i = 2; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--glitchportal")) g_glitchPortal = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--glitchwave")) g_glitchWave = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--glitchembed")) g_glitchEmbed = std::atof(argv[i + 1]);
    }
    for (int i = 2; i < argc; ++i)
        if (!std::strcmp(argv[i], "--glitchdeco")) g_glitchDeco = true;
    // --coinwin <yBin>,<vyBin>,<len> (thread_pool.hpp). Anything but three positive numbers is off.
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--coinwin")
            && (std::sscanf(argv[i + 1], "%lf,%lf,%lf", &g_coinWinY, &g_coinWinVy, &g_coinWinLen) != 3
                || !(g_coinWinY > 0.0 && g_coinWinVy > 0.0 && g_coinWinLen > 0.0)))
            g_coinWinY = g_coinWinVy = g_coinWinLen = 0.0;
    if (g_coinWinY > 0.0)
        std::printf("coinwin: cap cells y/%.1f vy/%.2f over %.0f px before each coin still lacked\n",
                    g_coinWinY, g_coinWinVy, g_coinWinLen);
    for (int i = 2; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--out")) outPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--dbg")) dbgLayers = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--cap")) g_aliveCap = (size_t)std::atoll(argv[i + 1]);
        // --search dp|beam, --beam B (search_key.hpp): which search answers, and the beam's width.
        if (!std::strcmp(argv[i], "--search")) {
            if (!std::strcmp(argv[i + 1], "beam")) g_searchMethod = 1;
            else if (!std::strcmp(argv[i + 1], "montecarlo")) g_searchMethod = 2;
            else if (!std::strcmp(argv[i + 1], "dp")) g_searchMethod = 0;
            else std::printf("search: unknown method '%s', using dp\n", argv[i + 1]);
        }
        if (!std::strcmp(argv[i], "--beam")) g_beamWidth = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--mcrollouts")) g_mcRollouts = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--mcwindow")) g_mcWindow = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--mctoggles")) g_mcToggles = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--mcsteps")) g_mcSteps = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--mcsafety")) g_mcSafetyS = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--mcstall")) g_mcStall = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--mccall")) g_mcCall = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--seed")) g_searchSeed = std::atoll(argv[i + 1]);
        // --capmap x0:c0,x1:c1,... (thread_pool.hpp). Entries must be in x order.
        if (!std::strcmp(argv[i], "--capmap")) {
            g_capMap.clear();
            for (const char* p = argv[i + 1]; *p;) {
                char* e = nullptr;
                const double x = std::strtod(p, &e);
                if (e == p || *e != ':') break;
                p = e + 1;
                const long long c = std::strtoll(p, &e, 10);
                if (e == p || c <= 0) break;
                g_capMap.emplace_back(x, (size_t)c);
                p = (*e == ',') ? e + 1 : e;
            }
        }
        if (!std::strcmp(argv[i], "--gridmap")) {
            g_gridMap.clear();
            for (const char* p = argv[i + 1]; *p;) {
                char* e = nullptr;
                const double x = std::strtod(p, &e);
                if (e == p || *e != ':') break;
                p = e + 1;
                const long k = std::strtol(p, &e, 10);
                if (e == p || k < 0) break;
                g_gridMap.emplace_back(x, (int)k);
                p = (*e == ',') ? e + 1 : e;
            }
        }
        if (!std::strcmp(argv[i], "--coinmask")) g_coinMaskSeed = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--coinskip")) g_coinSkip = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--hazdbg")) g_hazDbgUid = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--trigdbg")) g_trigDbgT = std::atoll(argv[i + 1]);
        // --fbforce <box>:<tick>: see g_fbForceBox (triggers.hpp).
        if (!std::strcmp(argv[i], "--fbforce")) {
            const char* v = argv[i + 1];
            g_fbForceBox = std::atoi(v);
            const char* c = std::strchr(v, ':');
            g_fbForceTick = c ? std::atoi(c + 1) : 0;
        }
        if (!std::strcmp(argv[i], "--coindbg")) g_coinDbgT = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--gcnodes")) g_gcNodes = (size_t)std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--memlimit")) g_memLimitMiB = (size_t)std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--shipyq")) g_shipYq = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--keyxq")) g_keyXq = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--keyxqfly")) g_keyXqFly = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--minpulse")) g_minPulse = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--minpressgap")) g_minPressGap = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--startages"))
            std::sscanf(argv[i + 1], "%d,%d", &g_startEdgeAge, &g_startPressAge);
        if (!std::strcmp(argv[i], "--humanfrom")) g_humanFrom = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--tightpx")) g_tightPx = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--inputgrid")) g_inputGrid = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--shipvq")) g_shipVq = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--snaplog")) snapLogPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--groups")) groupsPaths.push_back(argv[i + 1]);
        if (!std::strcmp(argv[i], "--supporttol"))
            kSupportTol = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--landtol"))
            kLandTol = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--stickgap"))
            kStickGap = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--rotperp"))
            kRotPerpWin = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--stepdepth"))
            kStepDepth = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--ctrlwin") || !std::strcmp(argv[i], "--ctrlwin2")) {
            auto& wins = argv[i][9] ? g_ctrlWin2 : g_ctrlWin;
            for (const char* p = argv[i + 1]; *p; ) {
                long long a0 = std::atoll(p);
                const char* col = std::strchr(p, ':');
                long long a1 = col ? std::atoll(col + 1) : a0;
                wins.push_back({a0, a1});
                const char* c = std::strchr(p, ',');
                if (!c) break;
                p = c + 1;
            }
        }
        if (!std::strcmp(argv[i], "--spentrot")) {
            for (const char* p = argv[i + 1]; *p; ) {
                g_spentRot.push_back(std::atoi(p));
                const char* c = std::strchr(p, ',');
                if (!c) break;
                p = c + 1;
            }
        }
        if (!std::strcmp(argv[i], "--spentpad")) {
            for (const char* p = argv[i + 1]; *p; ) {
                g_spentPad.push_back(std::atoi(p));
                const char* c = std::strchr(p, ',');
                if (!c) break;
                p = c + 1;
            }
        }
        // --solidorddbg t0:t1: print the solids the collision loops visit, in order (speed.hpp)
        if (!std::strcmp(argv[i], "--solidorddbg")) {
            g_solidOrdDbgT0 = std::atoll(argv[i + 1]);
            const char* c = std::strchr(argv[i + 1], ':');
            g_solidOrdDbgT1 = c ? std::atoll(c + 1) : g_solidOrdDbgT0;
        }
        // --spentorb uid,uid,...: rings already fired before the anchor, oldest first (speed.hpp)
        if (!std::strcmp(argv[i], "--spentorb")) {
            for (const char* p = argv[i + 1]; *p; ) {
                g_spentOrb.push_back(std::atoi(p));
                const char* c = std::strchr(p, ',');
                if (!c) break;
                p = c + 1;
            }
        }
        // --spentorb2 uid,uid,...: the second body's (read under --spentp2, speed.hpp)
        if (!std::strcmp(argv[i], "--spentorb2")) {
            for (const char* p = argv[i + 1]; *p; ) {
                g_spentOrb2.push_back(std::atoi(p));
                const char* c = std::strchr(p, ',');
                if (!c) break;
                p = c + 1;
            }
        }
        if (!std::strcmp(argv[i], "--dyndbg")) g_dynDbg = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--triggers")) trigPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--objgroups")) grpPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--items")) itemPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--coinmargin")) kCoinMargin = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--itembase")) {
            g_itemBase.clear();
            std::stringstream ss(argv[i + 1]);
            std::string ent;
            while (std::getline(ss, ent, ',')) {
                int it = 0, n = 0;
                if (std::sscanf(ent.c_str(), "%d:%d", &it, &n) == 2 && it != 0)
                    g_itemBase.push_back({it, n});
            }
        }
        if (!std::strcmp(argv[i], "--rotgameplay")) rotQPath = argv[i + 1];
        // --areas <path>: the MOD's dump of the area triggers (areas.txt), read under --areamove.
        if (!std::strcmp(argv[i], "--areas")) areasPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--levelsettings")) setPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--endportalx")) endPortalArg = std::atof(argv[i + 1]);   // --goalgd
        if (!std::strcmp(argv[i], "--obb")) obbPath = argv[i + 1];
        // --forceids <file>: force block IDs (see g_forceIdsPath)
        if (!std::strcmp(argv[i], "--forceids")) g_forceIdsPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--bandtrack")) loadBandTrack(argv[i + 1]);
        // --bandtrackend <t>: where the recording behind --bandtrack stops (bands.hpp)
        if (!std::strcmp(argv[i], "--bandtrackend")) g_bandTrackEnd = std::atoll(argv[i + 1]);
        if (!std::strcmp(argv[i], "--startband")) {
            double f = 0, c = 0;
            if (std::sscanf(argv[i + 1], "%lf,%lf", &f, &c) != 2) {
                g_startBandKind = BandKind::Unknown;
                std::fprintf(stderr, "startband: `%s` is not a readable f,c pair\n",
                             argv[i + 1]);
            } else if (c > f) {
                g_startBandKind = BandKind::Interval;
                g_startBandFloor = f;
                g_startBandCeil = c;
            } else if (c == f) {
                g_startBandKind = BandKind::Degenerate;
                g_startBandFloor = g_startBandCeil = f;
                std::fprintf(stderr, "startband: `%s` is a collapsed band\n",
                             argv[i + 1]);
            } else {
                g_startBandKind = BandKind::Unknown;
                std::fprintf(stderr, "startband: `%s` has the ceiling below the "
                             "floor\n", argv[i + 1]);
            }
        }
        // --needtrig <n>: repeatable. See g_needTrig.
        if (!std::strcmp(argv[i], "--needtrig"))
            g_needTrig |= touchBit(std::atoi(argv[i + 1]));
        // --needtrig-uid <uid>: repeatable. See g_needTrigUids.
        if (!std::strcmp(argv[i], "--needtrig-uid"))
            g_needTrigUids.push_back(std::atoi(argv[i + 1]));
        if (!std::strcmp(argv[i], "--needtrig-unseen")) g_needUnseen = true;
        // --needtrig-skip <n>: repeatable. See g_needSkip.
        if (!std::strcmp(argv[i], "--needtrig-skip"))
            g_needSkip |= touchBit(std::atoi(argv[i + 1]));
        if (!std::strcmp(argv[i], "--obb-all")) g_obbAll = true;
        if (!std::strcmp(argv[i], "--no-obb-all")) g_obbAll = false;
        if (!std::strcmp(argv[i], "--bands")) g_bandPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--replay")) replayPath = argv[i + 1];
        if (!std::strcmp(argv[i], "--fixups")) {
            std::ifstream ff(argv[i + 1]);
            std::string ln;
            while (std::getline(ff, ln)) {
                if (ln.size() >= 3 && (unsigned char)ln[0] == 0xEF &&
                    (unsigned char)ln[1] == 0xBB && (unsigned char)ln[2] == 0xBF)
                    ln.erase(0, 3);
                Fixup f{};
                double x, y, vy, dy, dvy;
                int in, mode, mini, flip, g, g2, kill = 0;
                const int n = std::sscanf(
                    ln.c_str(),
                    "x=%lf,in=%d,mode=%d,mini=%d,flip=%d,g=%d,"
                    "y=%lf,vy=%lf,dy=%lf,dvy=%lf,g2=%d,kill=%d",
                    &x, &in, &mode, &mini, &flip, &g, &y, &vy, &dy, &dvy,
                    &g2, &kill);
                if (n < 11) continue;
                f.x = (float)x; f.y = (float)y; f.vy = (float)vy;
                f.dy = (float)dy; f.dvy = (float)dvy;
                f.in = (uint8_t)in; f.mode = (uint8_t)mode;
                f.mini = (uint8_t)mini; f.flip = (uint8_t)flip;
                f.g = (uint8_t)g; f.gAfter = (uint8_t)g2;
                f.kill = (uint8_t)(kill != 0);
                if (const char* d = std::strstr(ln.c_str(), ",dual2=")) {
                    double y2, vy2, dy2, dvy2;
                    int g2b;
                    if (std::sscanf(d, ",dual2=%lf,%lf,%lf,%lf,%d",
                                    &y2, &vy2, &dy2, &dvy2, &g2b) == 5) {
                        f.dual = 1;
                        f.y2 = (float)y2; f.vy2 = (float)vy2;
                        f.dy2 = (float)dy2; f.dvy2 = (float)dvy2;
                        f.gAfter2 = (uint8_t)g2b;
                    }
                }
                f.ord = (int)g_fixups.size();
                g_fixups.push_back(f);
            }
            for (const Fixup& fx : g_fixups)
                (fx.kill ? g_fixupKills : g_fixupDeltas).push_back(fx);
            auto byX = [](const Fixup& a, const Fixup& b) { return a.x < b.x; };
            std::stable_sort(g_fixupKills.begin(), g_fixupKills.end(), byX);
            std::stable_sort(g_fixupDeltas.begin(), g_fixupDeltas.end(), byX);
            if (g_fixupConflictFilter) {
                std::vector<char> drop(g_fixupDeltas.size(), 0);
                for (size_t a = 0; a < g_fixupDeltas.size(); ++a)
                    for (size_t b = a + 1;
                         b < g_fixupDeltas.size()
                         && g_fixupDeltas[b].x <= g_fixupDeltas[a].x + 2.4f;
                         ++b) {
                        const Fixup &p = g_fixupDeltas[a], &q = g_fixupDeltas[b];
                        if (p.in != q.in || p.mode != q.mode || p.mini != q.mini
                            || p.flip != q.flip || p.g != q.g || p.dual != q.dual)
                            continue;
                        if (std::fabs(p.y - q.y) > 8.0f
                            || std::fabs(p.vy - q.vy) > 2.0f)
                            continue;
                        if (std::fabs(p.dy - q.dy) > 0.5f
                            || std::fabs(p.dvy - q.dvy) > 0.5f)
                            drop[p.ord < q.ord ? a : b] = 1;
                    }
                size_t w = 0, gone = 0;
                for (size_t a = 0; a < g_fixupDeltas.size(); ++a) {
                    if (drop[a]) { ++gone; continue; }
                    g_fixupDeltas[w++] = g_fixupDeltas[a];
                }
                g_fixupDeltas.resize(w);
                if (gone)
                    std::printf("fixups: %zu contradictory delta records "
                                "dropped\n", gone);
            }
            if (!g_fixups.empty())
                std::printf("fixups: %zu transition overrides loaded\n",
                            g_fixups.size());
        }
        if (!std::strcmp(argv[i], "--dynhazpad")) g_dynHazPad = std::atof(argv[i + 1]);
        // --histstat <file>: the fixed-length histories' fill after a replay (speed.hpp)
        if (!std::strcmp(argv[i], "--histstat")) {
            g_histStatOn = true;
            g_histStatPath = argv[i + 1];
        }
        if (!std::strcmp(argv[i], "--maxplayy")) g_maxPlayY = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--offboard") && i + 1 < argc)
            g_offBoardMargin = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--shiftdbg")) g_shiftDbgUid = std::atoi(argv[i + 1]);
        if (!std::strcmp(argv[i], "--deadband")) {
            double a0 = 0, a1 = 0, b0 = -1e18, b1 = 1e18; int md = -1;
            const int n = std::sscanf(argv[i + 1], "%lf,%lf,%d,%lf,%lf",
                                      &a0, &a1, &md, &b0, &b1);
            if (n >= 2 && a1 > a0)
                g_deadBands.push_back({a0, a1, n >= 3 ? md : -1,
                                       n >= 4 ? b0 : -1e18,
                                       n >= 5 ? b1 : 1e18});
        }
        if (!std::strcmp(argv[i], "--cubeyq")) g_cubeYq = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--cubevq")) g_cubeVq = std::atof(argv[i + 1]);
        if (!std::strcmp(argv[i], "--start")) {
            double a[33] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                            0, -1, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, 0, -9999, 0, 0};
            std::sscanf(argv[i + 1],
                        "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,"
                        "%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,"
                        "%lf,%lf,%lf,%lf,%lf,%lf,%lf",
                        &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7],
                        &a[8], &a[9], &a[10], &a[11], &a[12], &a[13], &a[14],
                        &a[15], &a[16], &a[17], &a[18], &a[19], &a[20], &a[21],
                        &a[22], &a[23], &a[24], &a[25], &a[26], &a[27], &a[28],
                        &a[29], &a[30], &a[31], &a[32]);
            const uint8_t startRev = (uint8_t)((int)a[21] & 1);
            startFrame = (int)a[20] & 3;
            startSnapUid = (long long)a[18];
            startSnapDist = a[19];
            g_startSpeedMul = a[14];
            t0 = (long long)a[0];
            x0 = a[1];
            x0World = a[1];
            init = State{(float)a[2], (float)a[3], (uint8_t)a[4], (uint8_t)a[6],
                         (uint8_t)a[5], (uint8_t)a[7]};
            init.mini = (uint8_t)a[8];
            // Ticks since the last gravity flip; absent or negative = no grace.
            init.flipT = (a[27] < 0.0)
                             ? (uint8_t)kFlipGraceTicks
                             : (uint8_t)std::min<int>(kFlipGraceTicks,
                                                      (int)a[27]);
            init.armT = (a[28] < 0.0)
                            ? (uint8_t)kArmTicks
                            : (uint8_t)std::min<int>(kArmTicks, (int)a[28]);
            init.rev = startRev;
            // (after the rebuild, same as rev -- see the NOTE above)
            init.boost = ((int)a[24] != 0) ? 1 : 0;
            if (((int)a[20] & 3) != 0) {
                const int sf = (int)a[20] & 3;
                double u, v;
                toFrame(sf, x0, a[2], u, v);
                x0 = u;
                init.y = (float)v;
                init.vy = (float)(a[3] * ((sf & 2) ? -1.0 : 1.0));
                init.frame = (uint8_t)sf;
                for (RotTrig& r : g_rotTrig)
                    if (r.frame == sf) r.firedT = (int)t0;
            }
            init.rHover = (uint8_t)a[15];   // see the 16th-field note above
            init.dashing = (uint8_t)a[16];
            // --dashclamp (speed.hpp): the mod's tan(-rot), held inside GD's +-70 line
            init.dashSlope = dashSlopeClamped((float)a[17], (int)init.frame);
            if (a[15] > 0 || a[16] > 0) init.action = (uint8_t)a[6];
            g_startHoldField = (int)a[6];   // --holdseed reads it with the hist payload
            init.rot = (float)a[22];
            init.rotNeg = (uint8_t)(a[23] != 0.0 ? 1 : 0);
            if ((int)a[4] == 2) init.rotStep = (float)std::fabs(a[29]);
            if ((int)a[4] == 0 && (int)a[5] == 0
                && std::fabs(std::fabs(a[29]) - (a[8] != 0.0 ? 1.125 : 0.8653846)) < 2e-3)
                init.rotBoost = 1;
            init.hitG = (uint8_t)(a[5] != 0.0 ? 1 : 0);
            init.dual = (uint8_t)a[9];
            if (init.dual) {
                init.y2 = (float)a[10];
                init.vy2 = (float)a[11];
                init.flip2 = (uint8_t)a[12];
                init.grounded2 = (uint8_t)a[13];
                init.hitG2 = init.grounded2 ? 1 : 0;
                init.mode2 = (a[25] >= 0.0) ? (uint8_t)a[25] : init.mode;
                init.mini2 = (a[26] >= 0.0) ? (uint8_t)(a[26] != 0.0)
                                            : init.mini;
                {
                    const bool told = a[30] > -9998.0;
                    init.rot2 = told ? (float)a[30] : init.rot;
                    init.rotNeg2 = told ? (uint8_t)(a[31] != 0.0 ? 1 : 0) : init.rotNeg;
                    init.rotStep2 = (init.mode2 == 2)
                                        ? (told ? (float)std::fabs(a[32]) : init.rotStep) : 0.f;
                }
            }
        }
    }
    init.xAbs = (float)x0;
    // --p2xoff: the second body's x offset at the anchor (--startp2dx; GD's p2x - x). Frame 0 only.
    if (g_p2xOff && init.dual && init.frame == 0) init.dx2 = (float)g_startP2dx;

    long long horizon = 0;
    long long planCut = 0;   // >0: emit only the plan up to this tick
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "--horizon")) horizon = std::atoll(argv[i + 1]);

    prepMark(0);
    // Read only from here on: the level loader takes it by const pointer, and the rest look up.
    const GroupTimeline& gt = groupLayersFor(groupsPaths);   // ...and sets g_groupInit
    prepMark(1);
    if (!trigPath.empty() && !grpPath.empty()) {
        g_touchRetimeFrom = t0;   // --touchretime re-times only boxes entered from here on
        if (g_coinRoute) {
            g_coinUids = coinUids(argv[1]);
            std::printf("coins: %zu coin uids for the trigger window\n",
                        g_coinUids.size());
        }
        if (gatesModelled() && !itemPath.empty()) {   // the pickups feed the gates
            loadCollectibles(itemPath);
            if (!g_collect.empty())
                std::printf("items: %zu pickups\n", g_collect.size());
            std::printf("items: %zu item givers\n", g_itemGiver.size());
        }
        std::unordered_map<int, int> objTypesOwn;
        const std::unordered_map<int, int>* objTypesP = &objTypesOwn;
        if (!g_levelCsv.empty()) {
            LevelTextCache& lc = levelTextFor(g_levelCsv);
            if (!lc.haveTypes) {
                std::istringstream cs(g_levelCsv);
                lc.types = loadObjTypesFrom(cs);
                lc.haveTypes = true;
            }
            objTypesP = &lc.types;
        } else {
            objTypesOwn = loadObjTypes(argv[1]);
        }
        const std::unordered_map<int, int>& objTypes = *objTypesP;
        bool winTouch = false;   // the window is opened by the auto-window gate below (and --touchwinahead)
        const bool rotWin = startFrame != 0 && x0World > -1e17;
        const double xGate = rotWin ? x0World : x0;
        if (!winTouch && xGate > -1e17) {
            const std::vector<TouchTrig> plain =
                loadTouchTriggers(trigPath, grpPath, -1e18,
                                  &objTypes);
            double lastBox = -1e18;
            for (const TouchTrig& e : plain)
                if (e.count < 0 && !e.tap) lastBox = std::max(lastBox, e.cx);
            if ((int)plain.size() >= kTouchBits && !plain.empty()
                && lastBox < xGate - 200.0) {
                winTouch = true;
                std::printf("triggers: the plain %d all lie behind the anchor "
                            "(last %.0f < %.0f) - windowing from the anchor\n",
                            (int)plain.size(), lastBox, xGate);
            } else if (g_touchWinAhead && g_trigDroppedAhead > 0) {
                const double dxReach = std::max((double)kDxF, g_startSpeedMul > 0.0
                                                ? (double)dxForSpeedMul(g_startSpeedMul) : 0.0);
                const double reach = (double)(horizon > 0 ? horizon : 3000) * dxReach;
                if (g_trigFirstAheadX < xGate + reach) {
                    winTouch = true;
                    std::printf("triggers: the plain %d drop a box ahead at x=%.0f within "
                                "the anchor's reach (%.0f + %.0f) - windowing from the "
                                "anchor\n", (int)plain.size(), g_trigFirstAheadX, xGate,
                                reach);
                }
            }
        }
        std::unordered_map<int, std::pair<double, double>> objPosOwn;
        const std::unordered_map<int, std::pair<double, double>>* objPosP = &objPosOwn;
        if (!g_levelCsv.empty()) {
            LevelTextCache& lc = levelTextFor(g_levelCsv);
            if (!lc.havePos) {
                std::istringstream cs(g_levelCsv);
                lc.pos = loadObjPos(cs);
                lc.havePos = true;
            }
            objPosP = &lc.pos;
        } else {
            std::ifstream fs(argv[1]);
            if (fs) objPosOwn = loadObjPos(fs);
        }
        const std::unordered_map<int, std::pair<double, double>>& objPos = *objPosP;
        prepMark(5);
        std::printf("movetarget: %zu object positions from %s\n", objPos.size(),
                    g_levelCsv.empty() ? "the dump" : "the in-process level table");
        if (rotWin && winTouch) g_trigWinNear = x0World;
        // --spawnlag: the spawn-triggered objects' x, from the same table as the positions.
        if (g_spawnLag) {
            if (!g_levelCsv.empty()) {
                LevelTextCache& lc = levelTextFor(g_levelCsv);
                if (!lc.haveSpawnX) {
                    std::istringstream cs(g_levelCsv);
                    lc.spawnX = loadObjSpawnX(cs);
                    lc.haveSpawnX = true;
                }
                g_spawnableX = lc.spawnX;
            } else {
                std::ifstream fs(argv[1]);
                if (fs) g_spawnableX = loadObjSpawnX(fs);
            }
            std::printf("spawnlag: %zu spawn-triggered objects\n", g_spawnableX.size());
        }
        g_touch = loadTouchTriggers(trigPath, grpPath, winTouch ? x0 : -1e18,
                                    &objTypes,
                                    &objPos);
        if (g_spawnLag)   // which boxes it acts on, and by how much
            for (size_t b = 0; b < g_touch.size(); ++b) {
                int n = 0, lo = 1 << 30, hi = 0;
                for (const TrigCtl& c : g_touch[b].ctl)
                    if (c.spawnLag > 0) {
                        ++n;
                        lo = std::min(lo, c.spawnLag);
                        hi = std::max(hi, c.spawnLag);
                    }
                if (n > 0)
                    std::printf("spawnlag: box %zu uid %d (x=%.0f) delays %d of %zu objects "
                                "by %d..%d ticks\n", b, g_touch[b].uid, g_touch[b].cx, n,
                                g_touch[b].ctl.size(), lo, hi);
            }
        g_trigWinNear = -1e18;
        g_outcome.trigWinTouch = g_touch.empty() ? -1 : (winTouch ? 1 : 0);
        g_outcome.trigTotal = (long long)g_trigTotal;
        g_outcome.trigRelevantN = (long long)g_trigRelevantN;
        g_outcome.trigKept = (long long)g_trigKept;
        g_outcome.trigDroppedRelevant = (long long)g_trigDroppedRelevant;
        g_outcome.trigDroppedBehind = (long long)g_trigDroppedBehind;
        g_outcome.trigDroppedAhead = (long long)g_trigDroppedAhead;
        g_outcome.trigMaxKeptX = g_trigMaxKeptX;
        {
            unsigned long long h = 1469598103934665603ull;
            for (const TouchTrig& t : g_touch) {
                unsigned long long u = (unsigned long long)(unsigned)t.uid;
                for (int k = 0; k < 8; ++k) {
                    h ^= (u >> (k * 8)) & 0xffull;
                    h *= 1099511628211ull;
                }
            }
            g_outcome.trigMapSig = g_touch.empty() ? 0ull : h;
        }
        buildTouchMoveTicks();   // the dedupe key's "is this box still moving"
        g_pressWin.clear();
        g_actMask = TouchMask{};
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
            if (g_touch[b].press)
                g_pressWin.push_back({g_touch[b].cx - g_touch[b].hw - 35.0,
                                      g_touch[b].cx + g_touch[b].hw + 15.0, (int)b});
            if (g_touch[b].activator) g_actMask |= touchBit((int)b);
        }
        g_autoTrig = loadAutoTriggers(trigPath, grpPath);
        // --autostop: for every entry, the Stop entries that name its root Move.
        g_autoStopBy.assign(g_autoStop ? g_autoTrig.size() : 0, {});
        if (g_autoStop) {
            std::unordered_map<int, int> autoIdx;
            for (size_t a = 0; a < g_autoTrig.size(); ++a)
                if (g_autoTrig[a].stopMoves.empty()) autoIdx[g_autoTrig[a].uid] = (int)a;
            for (size_t s = 0; s < g_autoTrig.size(); ++s)
                for (const int mu : g_autoTrig[s].stopMoves) {
                    const auto f = autoIdx.find(mu);
                    if (f != autoIdx.end()) g_autoStopBy[(size_t)f->second].push_back((int)s);
                }
        }
        g_autoDisarm.assign((g_tapDisarm || g_boxArm) ? g_autoTrig.size() : 0, {});
        if (g_tapDisarm || g_boxArm) {
            std::unordered_map<int, std::vector<int>> autoIdx;
            for (size_t a = 0; a < g_autoTrig.size(); ++a) autoIdx[g_autoTrig[a].uid].push_back((int)a);
            size_t nTied = 0;
            for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b)
                for (const TouchTrig::TogEv& e : g_touch[b].trigTog) {
                    const auto f = autoIdx.find(e.uid);
                    if (f == autoIdx.end()) continue;
                    for (const int a : f->second) {
                        g_autoDisarm[(size_t)a].push_back({(int)b, e.on, e.delay});
                        ++nTied;
                        std::printf("tapdisarm: box %zu (uid %d) switches %s the x-crossing trigger uid %d "
                                    "(x=%.0f) +%.0f\n", b, g_touch[b].uid, e.on ? "on" : "off", e.uid,
                                    g_autoTrig[(size_t)a].cx, e.delay);
                    }
                }
            std::printf("tapdisarm: %zu switch(es) of x-crossing triggers by boxes\n", nTied);
        }
        if (g_touchOwnOff) {
            std::unordered_map<int, int> autoIdx;
            for (size_t a = 0; a < g_autoTrig.size(); ++a) autoIdx[g_autoTrig[a].uid] = (int)a;
            for (TouchTrig& tb : g_touch) {
                if (tb.ownTog.empty()) continue;
                bool all = true;
                for (TouchTrig::OwnTog& e : tb.ownTog) {
                    const auto f = autoIdx.find(e.uid);
                    e.autoIdx = f == autoIdx.end() ? -1 : f->second;
                    if (e.autoIdx < 0) all = false;
                }
                std::printf("touchoff: box uid %d at (%.0f,%.0f) -", tb.uid, tb.cx, tb.cy);
                for (const TouchTrig::OwnTog& e : tb.ownTog)
                    std::printf(" %s at x=%.0f (Toggle uid %d)", e.on ? "on" : "off", e.cx, e.uid);
                std::printf("%s\n", all ? "" : " - a Toggle has no crossing tick, left as it was");
                if (!all) tb.ownTog.clear();
            }
        }
    }
    prepMark(2);
    if (!obbPath.empty()) g_obb = loadObb(obbPath);   // before loadLevel reads it
    if (setPath.empty()) setPath = settingsPathBeside(argv[1]);
    if (!setPath.empty() && loadLevelSettings(setPath) && g_fixRadiusCollision)
        std::printf("levelsettings: fixRadiusCollision=1 - circular hazards "
                    "use centre distance (branch A) in this level\n");
    if (endPortalArg > 0.0) g_endPortalX = endPortalArg;
    Level L;
    {
        std::vector<std::string> la;
        if (g_ladder) la = ladderArgs(argc, argv);
        bool isReplay = false;
        for (int i = 2; i < argc && !isReplay; ++i) isReplay = !std::strcmp(argv[i], "--replay");
        isReplay = isReplay && !g_ladder;
        if (!isReplay) g_replayLevel = ReplayLevelCache{};
        std::vector<std::string> ra;
        std::vector<std::pair<std::string, std::string>> rf;
        bool replayKeyed = false;
        if (isReplay) {
            replayKeyed = true;
            for (int i = 0; i < argc; ++i) {
                const bool fixVal = i > 0 && !std::strcmp(argv[i - 1], "--fixups");
                ra.push_back(fixVal ? std::string() : std::string(argv[i]));
                if (fixVal || i < 2) continue;
                const std::string a = argv[i];
                if (a.empty() || a[0] == '-') continue;
                if (std::find(groupsPaths.begin(), groupsPaths.end(), a) != groupsPaths.end())
                    continue;   // the timeline, keyed by g_groupLayersGen
                std::string bytes;
                if (readFileBytes(a, bytes)) rf.emplace_back(a, std::move(bytes));
            }
        }
        if (g_ladder && g_ladderLevel.ladder == g_ladder
            && g_ladderLevel.groupsGen == g_groupLayersGen && g_ladderLevel.args == la) {
            L = g_ladderLevel.level;
            g_ladderLevel.writes.put();
            std::fputs(g_ladderLevel.printed.c_str(), stdout);
        } else if (replayKeyed && g_replayLevel.valid && g_replayLevel.args == ra
                   && g_replayLevel.groupsGen == g_groupLayersGen && g_replayLevel.csv == g_levelCsv
                   && g_replayLevel.files == rf) {
            L = g_replayLevel.level;
            g_replayLevel.writes.put();
            std::fputs(g_replayLevel.printed.c_str(), stdout);
            if (g_levelCacheCheck) {
                const uint64_t hc = levelHashOf(g_replayLevel.level), hl = levelHashOf(L);
                std::printf("levelcachecheck: %s (stored %016llx cached %016llx copy %016llx)\n",
                            (hc == g_replayLevel.hash && hl == g_replayLevel.hash) ? "reuse ok" : "MISMATCH",
                            (unsigned long long)g_replayLevel.hash, (unsigned long long)hc,
                            (unsigned long long)hl);
            }
        } else {
            std::string printed;
            if (g_ladder || replayKeyed) g_loadPrinted = &printed;
            L = loadLevel(argv[1], groupsPaths.empty() ? nullptr : &gt,
                          g_touch.empty() ? nullptr : &g_touch,
                          g_autoTrig.empty() ? nullptr : &g_autoTrig);
            g_loadPrinted = nullptr;
            if (replayKeyed) {
                g_replayLevel = ReplayLevelCache{};
                g_replayLevel.valid = true;
                g_replayLevel.args = std::move(ra);
                g_replayLevel.groupsGen = g_groupLayersGen;
                g_replayLevel.csv = g_levelCsv;
                g_replayLevel.files = std::move(rf);
                g_replayLevel.level = L;
                g_replayLevel.writes.take();
                g_replayLevel.printed = printed;
                if (g_levelCacheCheck) g_replayLevel.hash = levelHashOf(g_replayLevel.level);
            }
            if (g_ladder) {
                g_ladderLevel.ladder = g_ladder;
                g_ladderLevel.args = std::move(la);
                g_ladderLevel.groupsGen = g_groupLayersGen;
                g_ladderLevel.level = L;
                g_ladderLevel.writes.take();
                g_ladderLevel.printed = std::move(printed);
            }
        }
    }
    prepMark(3);
    if (!L.unsupported.empty()) {
        std::printf("unsupported: %s\n", L.unsupported.c_str());
        std::fprintf(stderr, "level unsupported: %s\n", L.unsupported.c_str());
        g_outcome.unsupported = L.unsupported;
        return 2;
    }
    g_baseLv = &L;   // see levelOfFrame / applyRotation's re-tap
    g_areas.clear();
    L.dyn.areaOf.clear();
    L.dyn.areaArmed.clear();
    if (g_areaMove && areasPath.empty() && argc > 1) {
        std::string dir = argv[1];
        const size_t sl = dir.find_last_of("/\\");
        dir = sl == std::string::npos ? std::string(".") : dir.substr(0, sl);
        std::vector<std::string> cand{dir + "/areas.txt"};
        const size_t sl2 = dir.find_last_of("/\\");
        if (sl2 != std::string::npos && dir.substr(sl2 + 1) == "snaps") cand.push_back(dir.substr(0, sl2) + "/areas.txt");
        for (const std::string& c : cand)
            if (std::ifstream(c)) {
                areasPath = c;
                std::printf("areamove: no --areas, read %s\n", c.c_str());
                break;
            }
    }
    // Said, not silent: a kit collected before the mod wrote areas.txt has none, and the flag then does nothing.
    if (g_areaMove && (areasPath.empty() || grpPath.empty()))
        std::printf("areamove: nothing taken - no %s\n", areasPath.empty() ? "--areas file (areas.txt)"
                                                                          : "--objgroups file");
    else if (g_areaMove && !std::ifstream(areasPath))
        std::printf("areamove: nothing taken - cannot read %s\n", areasPath.c_str());
    if (g_areaMove && !areasPath.empty() && !grpPath.empty() && !L.dyn.objs.empty()) {
        std::ifstream af(areasPath);
        std::string line;
        std::vector<std::string> head;
        size_t nRead = 0;
        if (af && std::getline(af, line)) {
            std::stringstream hs(line);
            for (std::string c; std::getline(hs, c, ',');) head.push_back(c);
        }
        auto col = [&](const std::vector<std::string>& f, const char* name, double dflt) {
            for (size_t k = 0; k < head.size() && k < f.size(); ++k)
                if (head[k] == name) return f[k].empty() ? dflt : std::atof(f[k].c_str());
            return dflt;
        };
        while (af && std::getline(af, line)) {
            std::vector<std::string> f;
            std::stringstream ls(line);
            for (std::string c; std::getline(ls, c, ',');) f.push_back(c);
            ++nRead;
            AreaAct a;
            a.uid = (int)col(f, "uid", -1);
            a.id = (int)col(f, "id", 0);
            a.cx = col(f, "cx", 0);
            a.target = (int)col(f, "target", 0);
            a.special = (int)col(f, "special", 0);
            a.dirType = (int)col(f, "dirType", 0);
            a.xy = col(f, "xy", 0) != 0;
            a.inbound = col(f, "inbound", 0) != 0;
            a.len = col(f, "len", 0);
            a.off = col(f, "off", 0);
            a.modFront = col(f, "modFront", 1);
            a.modBack = col(f, "modBack", 1);
            if (col(f, "twoDir", 0) != 0) a.modFront = -std::fabs(a.modFront);   // processAreaEffects, +0x7f4
            a.deadzone = col(f, "deadzone", 0);
            a.moveX = col(f, "moveX", 0);
            a.moveY = col(f, "moveY", 0);
            a.eInType = (int)col(f, "eInType", 0);
            a.eInRate = col(f, "eInRate", 2);
            a.eInBuf = (int)col(f, "eInBuf", -3);
            a.moveDist = col(f, "moveDist", 0);
            a.angle = col(f, "angle", 0);
            a.scaleX = col(f, "scaleX", 1);
            a.scaleY = col(f, "scaleY", 1);
            a.rot = col(f, "rot", 0);
            const bool varies = col(f, "lenV", 0) != 0 || col(f, "offV", 0) != 0 || col(f, "offYV", 0) != 0
                                || col(f, "moveXV", 0) != 0 || col(f, "moveYV", 0) != 0
                                || col(f, "moveDistV", 0) != 0 || col(f, "angleV", 0) != 0
                                || col(f, "scaleXV", 0) != 0 || col(f, "scaleYV", 0) != 0 || col(f, "rotV", 0) != 0;
            const bool shape = (a.id == 3006 || a.id == 3007 || a.id == 3008) && a.special == -1 && a.dirType == 1
                               && !varies && col(f, "relative", 0) == 0 && col(f, "eOut", 0) == 0 && a.len > 0;
            if (shape) g_areas.push_back(a);
        }
        const auto* members = groupMembersCached(grpPath);
        Dynamics& D = L.dyn;
        const size_t N = D.objs.size();
        D.areaOf.assign(N, {});
        D.areaArmed.assign(N, 0);
        D.areaBaseX.assign(N, 0.0);
        D.areaBaseY.assign(N, 0.0);
        D.areaBaseHa.assign(N, 0.0);
        D.areaBaseHb.assign(N, 0.0);
        D.areaBaseRot.assign(N, 0.0);
        std::unordered_map<int, size_t> dynIdx;
        for (size_t i = 0; i < N; ++i) dynIdx[D.objs[i].uid] = i;
        if (members)
            for (size_t k = 0; k < g_areas.size(); ++k) {
                const auto gi = members->find(g_areas[k].target);
                if (gi == members->end()) continue;
                for (const int u : gi->second)
                    if (const auto di = dynIdx.find(u); di != dynIdx.end()) D.areaOf[di->second].push_back((int)k);
            }
        size_t nCand = 0, nArmed = 0;
        double worstArmed = 0.0;
        for (size_t i = 0; i < N; ++i) {
            if (D.areaOf[i].empty() || D.samples[i].size() < 2 || g_xTrack.empty()) continue;
            ++nCand;
            const auto& sm = D.samples[i];
            const double bx = sm[0].cx, by = sm[0].cy, r0 = sm[0].rot;
            bool before = true;   // the base row comes before every action runs
            for (const int k : D.areaOf[i]) before = before && xRefAt(std::max(1, sm[0].t - 1)) < g_areas[(size_t)k].cx;
            // the unturned half sizes from the base row's rect: only at a right angle
            const double q = std::fmod(std::fabs(r0), 180.0);
            const bool right = q < 1e-3 || std::fabs(q - 90.0) < 1e-3 || std::fabs(q - 180.0) < 1e-3;
            const bool swap = std::fabs(q - 90.0) < 1e-3;
            const double ha = swap ? sm[0].hh : sm[0].hw, hb = swap ? sm[0].hw : sm[0].hh;
            double worst = before ? 0.0 : 1e9;
            for (size_t r = 1; r < sm.size() && worst <= kAreaArmTol; ++r) {
                const double prx = xRefAt(std::max(1, sm[r].t - 1));
                const AreaPose p = areaPoseOf(D.areaOf[i], bx, by, ha, hb, r0, prx);
                if (p.sized && !right) { worst = 1e9; break; }
                worst = std::max({worst, std::fabs(p.cx - (double)sm[r].cx), std::fabs(p.cy - (double)sm[r].cy)});
                if (p.sized)
                    worst = std::max({worst, std::fabs(p.hw - (double)sm[r].hw), std::fabs(p.hh - (double)sm[r].hh)});
            }
            if (g_dynDbg == D.objs[i].uid || g_dynDbg == -6)
                std::printf("areamove: uid %d, %zu action(s), base (%.3f,%.3f), worst %.4f px over %zu rows -> %s\n",
                            D.objs[i].uid, D.areaOf[i].size(), bx, by, worst, sm.size(),
                            worst <= kAreaArmTol ? "armed" : "not armed");
            if (worst <= kAreaArmTol) {
                D.areaArmed[i] = 1;
                D.areaBaseX[i] = bx;
                D.areaBaseY[i] = by;
                D.areaBaseHa[i] = ha;
                D.areaBaseHb[i] = hb;
                D.areaBaseRot[i] = r0;
                worstArmed = std::max(worstArmed, worst);
                ++nArmed;
                D.anyAuto = true;
            }
        }
        std::printf("areamove: %zu area trigger(s) read, %zu of the covered shape; %zu of %zu object(s) armed "
                    "(worst %.4f px)%s\n", nRead, g_areas.size(), nArmed, nCand, worstArmed,
                    g_xTrack.empty() ? " - no --xtrack, nothing armed" : "");
    }
    if (g_nanSlope && g_nanSlopeDropped)   // --nanslope (constants.hpp)
        std::printf("nanslope: %d ramp(s) with no line (scale 0, sy0/sy1 nan) left out of the level\n",
                    g_nanSlopeDropped);
    L.dyn.armDep.clear();
    if (g_boxArm) {
        Dynamics& D = L.dyn;
        const size_t N = D.objs.size();
        D.armDep.assign(N, 0);
        auto offInit = [](int k) {
            return k >= 0 && (size_t)k < g_autoTrig.size() && g_autoTrig[(size_t)k].offInit != 0;
        };
        size_t nDep = 0;
        for (size_t i = 0; i < N; ++i) {
            bool dep = (i < D.autoAnchor.size() && offInit(D.autoAnchor[i]))
                       || (i < D.autoLockTrig.size() && offInit(D.autoLockTrig[i]));
            if (i < D.autoParts.size())
                for (const Dynamics::AutoPart& p : D.autoParts[i]) dep = dep || offInit(p.trig);
            if (i < D.partsAny.size())
                for (const Dynamics::AutoPart& p : D.partsAny[i]) dep = dep || offInit(p.trig);
            if (dep) {
                D.armDep[i] = 1;
                ++nDep;
            }
        }
        std::printf("boxarm: %zu moving object(s) take a Move that only a box switches back on\n", nDep);
    }
    if (g_ctrlTouch && !g_ctrlTouchBoxes.empty() && !trigPath.empty() && !grpPath.empty()) {
        const auto* rows = trigRowsCached(trigPath);
        const auto* mem = groupMembersCached(grpPath);
        if (rows && mem)
            for (const auto& kv : *rows) {
                const TrigRow& T = kv.second;
                if (T.id != 1595 || T.thold != 1 || T.ttog != 2 || T.touch || T.spawn) continue;
                const auto g = mem->find(T.target);
                if (g == mem->end()) continue;
                for (CtrlTouchBox& b : g_ctrlTouchBoxes)
                    if (!b.holdOff && T.cx < b.cx
                        && std::find(g->second.begin(), g->second.end(), b.uid) != g->second.end()) {
                        b.holdOff = 1;
                        std::printf("ctrltouch: uid %d is off while the button is held (Touch uid %d "
                                    "at x=%.0f)\n", b.uid, T.uid, T.cx);
                    }
            }
    }
    if (g_holdTap && !trigPath.empty() && !grpPath.empty() && !L.dyn.objs.empty()) {
        if (!g_heldAll)
            std::printf("holdtap: nothing taken - needs --heldall (the button in every mode)\n");
        const auto* rows = trigRowsCached(trigPath);
        const auto* mem = groupMembersCached(grpPath);
        if (g_heldAll && rows && mem)
            for (const auto& kv : *rows) {
                const TrigRow& T = kv.second;
                if (T.id != 1595 || T.thold != 1 || T.ttog != 2 || T.touch || T.spawn) continue;
                const auto g = mem->find(T.target);
                if (g == mem->end()) continue;
                int nHaz = 0, nOther = 0;
                double lo = 1e18, hi = -1e18;
                for (Obj& o : L.dyn.objs) {
                    if (std::find(g->second.begin(), g->second.end(), o.uid) == g->second.end()) continue;
                    if (o.type != 2 && o.type != 47) { ++nOther; continue; }   // hazards only
                    if (o.holdTapX <= 0.f || (float)T.cx < o.holdTapX) o.holdTapX = (float)T.cx;
                    lo = std::min(lo, o.cx - o.hw);
                    hi = std::max(hi, o.cx + o.hw);
                    ++nHaz;
                }
                if (nHaz) g_holdTapSpans.push_back({lo, hi});
                std::printf("holdtap: Touch uid %d at x=%.0f (group %d) switches %d moving hazard(s) off while "
                            "the button is held (x %.0f..%.0f)%s\n", T.uid, T.cx, T.target, nHaz, lo, hi,
                            nOther ? " - its other moving objects are left to the recording" : "");
            }
    }
    if (g_ctrlTouch && !g_ctrlTouchBoxes.empty() && !g_ctrlWin.empty()) {
        const double dxA = g_startSpeedMul > 0.0 ? (double)dxForSpeedMul(g_startSpeedMul) : (double)kDxF;
        std::vector<std::pair<long long, long long>> kept;
        for (const auto& w : g_ctrlWin) {
            const CtrlTouchBox* hit = nullptr;
            if (w.first > t0) {
                const double xw = x0 + (double)(w.first - t0) * dxA;
                for (const CtrlTouchBox& b : g_ctrlTouchBoxes)
                    if (b.on && std::fabs(xw - b.cx) <= b.hw + 15.0 + dxA) { hit = &b; break; }
            }
            if (hit)
                std::printf("ctrltouch: window %lld:%lld dropped (x ~%.1f at its start is over uid %d)\n",
                            w.first, w.second, x0 + (double)(w.first - t0) * dxA, hit->uid);
            else
                kept.push_back(w);
        }
        g_ctrlWin.swap(kept);
    }
    g_gpHandoff = L.gpHandoff;   // shared gravity-portal bits (prelude.hpp); empty on most levels
    if (!g_gpHandoff.empty())
        std::printf("gravity portals: %zu bits shared past %d, handed on at x=%.1f..%.1f\n",
                    g_gpHandoff.size(), kGravPortalBits, g_gpHandoff.front().x,
                    g_gpHandoff.back().x);
    if (!g_touch.empty() && !trigPath.empty() && !grpPath.empty() && !L.dyn.objs.empty()) {
        std::unordered_map<int, TrigRow> rows;
        if (loadTrigRows(trigPath, rows)) {
            std::unordered_set<int> moved;   // groups some Move displaces
            std::unordered_set<int> rigid;   // groups a whole-group translation acts on
            std::unordered_set<int> bent;    // groups a position-dependent transform acts on
            for (const auto& kv : rows) {
                const TrigRow& r = kv.second;
                if (r.target <= 0) continue;
                if (r.id == 901 && (r.ox != 0.0 || r.oy != 0.0)) moved.insert(r.target);
                if (r.id == 901 || r.id == 1347 || r.id == 1814) rigid.insert(r.target);
                if (r.id == 1346 || r.id == 2067 || (r.id >= 3006 && r.id <= 3008)
                    || (r.id >= 3011 && r.id <= 3013) || r.id == 3016)
                    bent.insert(r.target);
            }
            std::unordered_map<int, std::vector<int>> movedOf;   // uid -> its moved groups
            std::unordered_map<int, std::vector<int>> rigidOf;   // uid -> its translated groups
            std::unordered_set<int> bentUid;
            {
                std::ifstream in(grpPath);
                std::string line;
                std::getline(in, line);   // header
                while (std::getline(in, line)) {
                    std::stringstream ss(line);
                    int uid = 0, g = 0;
                    if (!(ss >> uid)) continue;
                    while (ss >> g) {
                        if (moved.count(g)) movedOf[uid].push_back(g);
                        if (rigid.count(g)) rigidOf[uid].push_back(g);
                        if (bent.count(g)) bentUid.insert(uid);
                    }
                }
                for (auto& kv : movedOf) std::sort(kv.second.begin(), kv.second.end());
                for (auto& kv : rigidOf) std::sort(kv.second.begin(), kv.second.end());
            }
            const auto rigidSig = [&](int uid) {
                const auto it = rigidOf.find(uid);
                return it == rigidOf.end() ? std::vector<int>{} : it->second;
            };
            int nFollow = 0, nRefused = 0;
            for (TouchTrig& T : g_touch) {
                const auto want = movedOf.find(T.uid);
                if (want == movedOf.end() || want->second.empty()) continue;
                const std::vector<int> wantRigid = rigidSig(T.uid);
                int best = -1, nSameMoves = 0;
                double bestD = 1e18;
                for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                    const int u = L.dyn.objs[i].uid;
                    const auto have = movedOf.find(u);
                    if (have == movedOf.end() || have->second != want->second) continue;
                    ++nSameMoves;
                    if (bentUid.count(T.uid) || bentUid.count(u) || rigidSig(u) != wantRigid)
                        continue;
                    const double d = std::fabs(L.dyn.objs[i].cx - T.cx)
                                     + std::fabs(L.dyn.objs[i].cy - T.cy);
                    if (d < bestD) { bestD = d; best = (int)i; }
                }
                if (best < 0) {
                    if (nSameMoves) {
                        ++nRefused;
                        std::printf("touchfollow: box uid %d NOT followed: %s\n", T.uid,
                                    bentUid.count(T.uid)
                                        ? "a Rotate/Scale/Area/Advanced Follow moves its "
                                          "group, by an amount that depends on position"
                                        : "no recorded object shares all its transforms");
                    }
                    continue;
                }
                // load-time position: seek() has not run yet
                T.followIdx = best;
                T.followX0 = L.dyn.objs[(size_t)best].cx;
                T.followY0 = L.dyn.objs[(size_t)best].cy;
                ++nFollow;
                std::printf("touchfollow: box uid %d follows dynamic uid %d (%zu moved "
                            "group(s), %.0f px away)\n", T.uid,
                            L.dyn.objs[(size_t)best].uid, want->second.size(), bestD);
            }
            if (nFollow || nRefused)
                std::printf("touchfollow: %d box(es) follow, %d refused\n", nFollow, nRefused);
            if (nFollow) g_touchFollowObjs = &L.dyn.objs;
        }
    }
    if (g_boxArm) {
        size_t nRec = 0;
        for (TouchTrig& T : g_touch) {
            T.recBox = nullptr;
            if (!T.press || T.offSwitch) continue;
            bool arms = false;
            for (const TouchTrig::TogEv& e : T.trigTog) arms = arms || e.on;
            if (!arms) continue;
            const auto it = gt.find(T.uid);
            if (it == gt.end() || it->second.size() < 2) continue;
            T.recBox = &it->second;
            ++nRec;
        }
        std::printf("boxarm: %zu toggle block(s) tested where the recording has them\n", nRec);
    }
    g_followYs.clear();
    g_followSecObjs.clear();
    if (g_followYOn && !trigPath.empty() && !grpPath.empty()) {
        std::unordered_map<int, TrigRow> rows;
        if (loadTrigRows(trigPath, rows)) {
            std::vector<const TrigRow*> fy;
            for (const auto& kv : rows)
                if (kv.second.id == 1814 && kv.second.target > 0) fy.push_back(&kv.second);
            std::sort(fy.begin(), fy.end(),
                      [](const TrigRow* a, const TrigRow* b) { return a->cx < b->cx; });
            std::unordered_map<int, std::vector<int>> members;   // group -> uids
            {
                std::ifstream in(grpPath);
                std::string line;
                std::getline(in, line);   // header
                while (std::getline(in, line)) {
                    std::stringstream ss(line);
                    int uid = 0, g = 0;
                    if (!(ss >> uid)) continue;
                    while (ss >> g) members[g].push_back(uid);
                }
            }
            std::unordered_map<int, double> y0Of;   // uid -> load-time y, from every bucket
            for (const std::vector<Obj>* v : {&L.objs, &L.portals, &L.pads, &L.orbs, &L.speeds,
                                              &L.slopes, &L.dyn.objs})
                for (const Obj& o : *v) y0Of.emplace(o.uid, (double)o.cy);
            // the fastest x advance the level can have: where a run is over by, at the latest
            double dxMax = (double)kDxF;
            for (const Obj& o : L.speeds) dxMax = std::max(dxMax, (double)dxForSpeedId(o.id));
            if (g_startSpeedMul > 0.0)
                dxMax = std::max(dxMax, (double)dxForSpeedMul(g_startSpeedMul));
            double slotFree[kFollowYSlots];
            for (double& v : slotFree) v = -1e300;
            for (const TrigRow* r : fy) {
                const char* why = nullptr;
                int slot = -1;
                for (int sl = 0; sl < kFollowYSlots; ++sl)
                    if (slotFree[sl] < r->cx && (slot < 0 || slotFree[sl] < slotFree[slot]))
                        slot = sl;
                if (!r->fyCols) why = "from a dump without the follow columns";
                else if (r->touch || r->spawn) why = "touch- or spawn-triggered";
                else if (r->fyDelay > 0.0) why = "delayed (a ring of past y a state cannot carry)";
                else if (r->fySpeed <= 0.0) why = "at speed 0";
                else if (slot < 0) why = "one more than a state's slots while the others run";
                FollowY f;
                f.slot = slot;
                f.endX = r->cx + r->dur * 240.0 * dxMax;
                f.cx = r->cx;
                f.uid = r->uid;
                f.group = r->target;
                f.k = std::min(std::max(0.25 * r->fySpeed, 0.0), 1.0);
                f.offset = (double)r->fyOffset;
                f.maxStep = r->fyMax > 0.0 ? 0.25 * r->fyMax : 0.0;
                f.durTicks = r->dur * 240.0;
                f.refUid = -1;
                const auto mit = members.find(r->target);
                if (!why && mit != members.end())
                    for (int u : mit->second) {
                        const auto yit = y0Of.find(u);
                        if (yit == y0Of.end()) continue;   // a decoration or a trigger
                        if (f.refUid < 0) {
                            f.refUid = u;
                            f.y0ref = yit->second;
                        }
                        f.objs.push_back({u, yit->second});
                    }
                if (!why && f.objs.empty()) why = "its group holds nothing the model collides with";
                if (why) {
                    std::printf("followy: uid %d at x=%.0f (group %d) NOT modelled: %s\n",
                                r->uid, r->cx, r->target, why);
                    // --followsection: not placed, but bucketed all the same (modifiers.hpp)
                    if (g_followSection && mit != members.end()) {
                        size_t n0 = g_followSecObjs.size();
                        for (int u : mit->second) {
                            const auto yit = y0Of.find(u);
                            if (yit != y0Of.end()) g_followSecObjs.push_back({u, yit->second});
                        }
                        std::printf("followsection: group %d's %zu object(s) are bucketed at their "
                                    "load-time cells\n", r->target, g_followSecObjs.size() - n0);
                    }
                    continue;
                }
                std::printf("followy: uid %d at x=%.0f slot %d: group %d (%zu object(s), ref uid "
                            "%d at y=%.1f), k %.4f, offset %+.0f, max step %.3f, %.0f ticks "
                            "(over by x=%.0f)\n",
                            r->uid, r->cx, slot, f.group, f.objs.size(), f.refUid, f.y0ref, f.k,
                            f.offset, f.maxStep, f.durTicks, f.endX);
                slotFree[slot] = f.endX;
                g_followYs.push_back(std::move(f));
            }
        }
    }
    g_bigSecObjs.clear();
    g_bigSecDyn.clear();
    if (g_bigSection) {
        std::unordered_set<int> followed;
        for (const FollowY& f : g_followYs)
            for (const auto& m : f.objs) followed.insert(m.first);
        for (const auto& m : g_followSecObjs) followed.insert(m.first);
        for (const std::vector<Obj>* v : {&L.objs, &L.portals, &L.pads, &L.orbs, &L.speeds, &L.slopes})
            for (const Obj& o : *v)
                if ((o.hw > kBigSecHalf || o.hh > kBigSecHalf) && !o.extCol && !followed.count(o.uid))
                    g_bigSecObjs.push_back({o.uid, (double)o.cy});
        for (const Obj& o : L.dyn.objs)
            if ((o.hw > kBigSecHalf || o.hh > kBigSecHalf || o.radius > kBigSecHalf) && !o.extCol
                && !followed.count(o.uid))
                g_bigSecDyn.push_back({o.uid, (double)o.cy});
        std::printf("bigsection: %zu static and %zu grouped collider(s) are met only from their own "
                    "section cell:", g_bigSecObjs.size(), g_bigSecDyn.size());
        for (size_t i = 0; i < g_bigSecObjs.size() && i < 24; ++i)
            std::printf(" %d", g_bigSecObjs[i].first);
        if (!g_bigSecDyn.empty()) std::printf(" | grouped:");
        for (size_t i = 0; i < g_bigSecDyn.size() && i < 24; ++i)
            std::printf(" %d", g_bigSecDyn[i].first);
        std::printf("\n");
    }
    if (!g_followYs.empty()) {
        if (t0 > 0) L.dyn.seek((int)t0);
        for (int sl = 0; sl < kFollowYSlots; ++sl) {
            const int own = followOwner(sl, x0World);   // ahead of x0: primed on its crossing
            if (own < 0) continue;
            FollowY& f = g_followYs[(size_t)own];
            const uint8_t bit = (uint8_t)(1u << sl);
            if (t0 <= 0) {
                f.tAct = t0;
                init.followF[sl] = (float)f.y0ref;
                init.followOk = (uint8_t)(init.followOk | bit);
                continue;
            }
            for (const Obj& o : L.dyn.objs)
                if (o.uid == f.refUid) {
                    init.followF[sl] = (float)o.cy;
                    init.followOk = (uint8_t)(init.followOk | bit);
                    break;
                }
            if (f.tAct < 0) {
                const double dxA = g_startSpeedMul > 0.0 ? (double)dxForSpeedMul(g_startSpeedMul)
                                                         : (double)kDxF;
                f.tAct = t0 - (long long)std::floor((x0World - f.cx) / dxA);
            }
            std::printf("followy: uid %d (slot %d) at the anchor t=%lld: %s, crossed at t=%lld\n",
                        f.uid, sl, t0,
                        (init.followOk & bit) ? "seeded from the recording" : "no recorded row - "
                        "the group stays where the recording puts it", f.tAct);
        }
    }
    if (g_forceIdsPath.empty()) g_forceIdsPath = forceIdsPathBeside(argv[1]);
    if (!g_forceIdsPath.empty()) applyForceIds(g_forceIdsPath);
    for (const RotTrig& r : g_rotTrig) (void)frameLevel(L, r.frame);
    if (!g_spentRot.empty()) {
        int hit = 0;
        for (int su : g_spentRot)
            for (RotTrig& r : g_rotTrig)
                if (r.uid == su && r.firedT < 0) { r.firedT = 0; ++hit; }
        std::printf("spentrot: %d/%zu triggers pre-fired\n",
                    hit, g_spentRot.size());
    }
    if (startSnapUid >= 0) {
        for (const Obj& o : L.objs)
            if (o.uid == (int)startSnapUid) { init.snapObj = &o; break; }
        if (!init.snapObj)
            for (const Obj& o : L.dyn.objs)
                if (o.uid == (int)startSnapUid) { init.snapObj = &o; break; }
        if (init.snapObj) init.snapDist = (float)startSnapDist;
        else std::printf("start: snap uid %lld not found in the level\n",
                         startSnapUid);
    }
    if (t0 > 0 && startFrame == 0) {
        const double sHalf = playerHalf(init.mode, init.mini != 0);
        int slot = 0;
        for (const Obj& o : L.pads) {
            if (slot >= 4) break;
            if (std::fabs(x0 - o.cx) >= o.hw + sHalf + kPadReach) continue;
            if (std::fabs((double)init.y - o.cy) >= o.hh + sHalf) continue;
            double sRotPad = (double)init.rot;
            if (o.type == 8) {
                if (g_noPadPlayerRot) sRotPad = 0.0;
                else if (!padPlayerRotMode(init.mode)) sRotPad = 0.0;
            }
            if (o.oriented
                && !orientedHit(o, x0, (double)init.y, sHalf, sRotPad))
                continue;
            init.usedPad[slot++] = &o;
        }
        if (g_spentPadSeed && !g_spentPad.empty()) {
            if (g_spentPadHold)   // --spentpadhold (frames.hpp): all of them, for the whole run --
                for (int su : g_spentPad) {   // a pad carried by moving geometry as well
                    for (Obj& o : L.pads)
                        if (o.uid == su && !o.multiAct) o.spentRun = 1;
                    for (Obj& o : L.dyn.objs)
                        if (o.uid == su && !o.multiAct) o.spentRun = 1;
                }
            int seeded = 0, gone = 0, unknown = 0, overflow = 0, carried = 0;
            for (int su : g_spentPad) {
                const Obj* pad = nullptr;
                for (const Obj& o : L.pads)
                    if (o.uid == su) { pad = &o; break; }
                if (!pad) {
                    bool moving = false;
                    for (const Obj& o : L.dyn.objs)
                        if (o.uid == su) { moving = true; break; }
                    (moving ? carried : unknown) += 1;
                    continue;
                }
                if (std::fabs(x0 - pad->cx) >= pad->hw + sHalf + kPadReach) {
                    ++gone;      // released before t0 anyway -- see the release
                    continue;
                }
                bool have = false;
                for (int i = 0; i < slot; ++i)
                    if (init.usedPad[i] == pad) { have = true; break; }
                if (have) continue;             // the overlap rule got it first
                if (slot >= 4) { ++overflow; continue; }
                init.usedPad[slot++] = pad;
                ++seeded;
            }
            std::printf("spentpad: %d/%zu pads pre-fired (%d already outside "
                        "the x window, %d carried by moving geometry, %d not a "
                        "pad in this level)\n",
                        seeded, g_spentPad.size(), gone, carried, unknown);
            if (overflow)
                std::printf("spentpad: %d DROPPED - all four usedPad slots are "
                            "taken at t0\n", overflow);
        }
        auto pressRingSpent = [](int su) {
            if (!g_ringPressEat) return false;
            for (const PressRing& r : g_pressRings)
                if (r.uid == su) return !(g_multiOrb && r.multiAct);
            return false;
        };
        if (!g_spentOrb.empty()) {
            int seeded = 0, unknown = 0, carried = 0, pressed = 0;
            for (int su : g_spentOrb) {
                const Obj* ring = nullptr;
                for (const Obj& o : L.orbs)
                    if (o.uid == su) { ring = &o; break; }
                if (!ring)
                    for (size_t i = 0; i < L.dyn.objs.size(); ++i)
                        if (L.dyn.objs[i].uid == su && L.dyn.bucket[i] == Dynamics::ORB) {
                            ring = &L.dyn.objs[i];
                            ++carried;
                            break;
                        }
                if (!ring && pressRingSpent(su)) {
                    notePressRingFired(init, su);
                    ++pressed;
                    continue;
                }
                if (!ring) { ++unknown; continue; }
                noteRingFired(init, ring);
                ++seeded;
            }
            std::printf("spentorb: %d/%zu rings pre-fired (%d not a ring in this level)\n",
                        seeded, g_spentOrb.size(), unknown);
            std::printf("spentorb: %d of them carried by moving geometry\n", carried);
            if (pressed) std::printf("spentorb: %d toggle rings pre-fired (--ringpresseat)\n", pressed);
        }
        if (g_spentP2 && !g_spentOrb2.empty()) {
            int seeded2 = 0, unknown2 = 0, pressed2 = 0;
            auto swapRings = [](State& s) {
                std::swap(s.usedOrb, s.usedOrb2);
                std::swap(s.usedOrbOld, s.usedOrbOld2);
                for (int i = 0; i < 3; ++i) std::swap(s.usedOrbHist[i], s.usedOrbHist2[i]);
            };
            for (int su : g_spentOrb2) {
                const Obj* ring = nullptr;
                for (const Obj& o : L.orbs)
                    if (o.uid == su) { ring = &o; break; }
                if (!ring)
                    for (size_t i = 0; i < L.dyn.objs.size(); ++i)
                        if (L.dyn.objs[i].uid == su && L.dyn.bucket[i] == Dynamics::ORB) {
                            ring = &L.dyn.objs[i];
                            break;
                        }
                if (!ring && pressRingSpent(su)) {
                    swapRings(init);
                    notePressRingFired(init, su);
                    swapRings(init);
                    ++pressed2;
                    continue;
                }
                if (!ring) { ++unknown2; continue; }
                swapRings(init);
                noteRingFired(init, ring);
                swapRings(init);
                ++seeded2;
            }
            std::printf("spentorb2: %d/%zu of the second body's rings pre-fired (%d not a ring in this level)\n",
                        seeded2, g_spentOrb2.size(), unknown2);
            if (pressed2) std::printf("spentorb2: %d toggle rings pre-fired (--ringpresseat)\n", pressed2);
        }
    }
    std::vector<std::pair<int, int>> payloadTouch;
    std::vector<int> payloadPortal, payloadPortal2;
    std::vector<std::pair<std::string, int>> payloadHist;
    int histVersion = 1;
    bool havePayload = false;
    if (!g_anchorState.empty()) {
        std::vector<std::pair<std::string, std::string>> kv;
        std::string unknown;
        if (!parseAnchorState(g_anchorState, kv, unknown)) {
            std::printf("seed payload rejected: unknown key '%s'\n"
                        "  this exe knows:", unknown.c_str());
            for (const char* kk : kAnchorKeys) std::printf(" %s", kk);
            std::printf("\n  a payload naming something else was written by a "
                        "build that carries what this one would drop\n");
            return 2;
        }
        bool sawTouchKey = false, sawPortalKey = false, sawPortal2Key = false;
        bool sawHistKey = false;
        for (const auto& p : kv) {
            if (p.first == "owns") {
                if (p.second.find("touch") != std::string::npos)
                    g_ownsTouch = true;
                if (p.second.find("portal") != std::string::npos)
                    g_ownsPortal = true;
                if (p.second.find("hist") != std::string::npos)
                    g_ownsHist = true;
            } else if (p.first == "hist") {
                // version|count|name:value,... -- see kHistNames.
                const std::string& v = p.second;
                const size_t b1 = v.find('|');
                const size_t b2 = (b1 == std::string::npos) ? b1
                                                            : v.find('|', b1 + 1);
                if (b2 == std::string::npos
                    || (v.substr(0, b1) != "1" && v.substr(0, b1) != "2"
                        && v.substr(0, b1) != "3" && v.substr(0, b1) != "4"
                        && v.substr(0, b1) != "5" && v.substr(0, b1) != "6")) {
                    std::printf("seed payload rejected: hist '%s' is not version "
                                "1 to 6 (version|count|name:value,...)\n", v.c_str());
                    return 2;
                }
                histVersion = std::atoi(v.substr(0, b1).c_str());
                const int want = std::atoi(v.substr(b1 + 1, b2 - b1 - 1).c_str());
                int got = 0;
                size_t i = b2 + 1;
                while (i < v.size()) {
                    size_t c = v.find(',', i);
                    if (c == std::string::npos) c = v.size();
                    const std::string nv = v.substr(i, c - i);
                    i = c + 1;
                    if (nv.empty()) continue;
                    const size_t colon = nv.find(':');
                    const std::string name = nv.substr(0, colon);
                    bool known = false;
                    for (const char* hn : histNamesFor(histVersion))
                        if (hn && name == hn) known = true;
                    if (colon == std::string::npos || !known) {
                        std::printf("seed payload rejected: hist name '%s' is "
                                    "not one version %d knows:", name.c_str(),
                                    histVersion);
                        for (const char* hn : histNamesFor(histVersion))
                            if (hn) std::printf(" %s", hn);
                        std::printf("\n");
                        return 2;
                    }
                    payloadHist.emplace_back(name,
                                             std::atoi(nv.substr(colon + 1).c_str()));
                    ++got;
                }
                if (got != want) {
                    std::printf("seed payload rejected: hist says %d values and "
                                "carries %d\n", want, got);
                    return 2;
                }
                sawHistKey = true;
            } else if (p.first == "touch") {
                payloadTouch = parseTouchPayload(p.second);
                sawTouchKey = true;
            } else if (p.first == "portal") {
                payloadPortal = parseUidList(p.second);
                sawPortalKey = true;
            } else if (p.first == "portal2") {
                payloadPortal2 = parseUidList(p.second);
                sawPortal2Key = true;
            }
        }
        havePayload = g_ownsTouch;
        std::string missing;
        if (g_ownsTouch && !sawTouchKey) missing = "touch";
        if (g_ownsPortal && !sawPortalKey)
            missing += (missing.empty() ? "" : " ") + std::string("portal");
        if (g_ownsPortal && !sawPortal2Key)
            missing += (missing.empty() ? "" : " ") + std::string("portal2");
        if (g_ownsHist) {
            if (!sawHistKey)
                missing += (missing.empty() ? "" : " ") + std::string("hist");
            for (const char* hn : histNamesFor(histVersion)) {
                if (!hn) continue;
                bool have = false;
                for (const auto& h : payloadHist) if (h.first == hn) have = true;
                if (sawHistKey && !have)
                    missing += (missing.empty() ? "" : " ") + std::string(hn);
            }
        }
        if (!missing.empty()) {
            if (!g_seedPartialOk) {
                std::printf("seed payload incomplete: missing %s\n"
                            "  a payload REPLACES the recording-derived seed, "
                            "so an omitted key is set by nobody\n"
                            "  this exe wants:", missing.c_str());
                for (const char* kk : kAnchorKeys) std::printf(" %s", kk);
                std::printf("\n  payload carried:");
                for (const auto& p : kv) std::printf(" %s", p.first.c_str());
                std::printf("\n  (the shorter list is the older build; pass "
                            "--seed-partial-ok to run anyway)\n");
                return 2;
            }
            g_seedPartial = missing;
        }
    }
    if (t0 > 0 && havePayload) {
        int set = 0, unmapped = 0;
        for (const auto& ut : payloadTouch) {
            int bit = -1;
            for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b)
                if (g_touch[b].uid == ut.first) { bit = (int)b; break; }
            if (bit < 0) { ++unmapped; continue; }
            init.trig |= touchBit(bit);
            init.fireB[bit] = (uint16_t)std::max(0, ut.second);
            ++set;
        }
        std::printf("seed payload: %d boxes set, %d named but not in this "
                    "build's window\n", set, unmapped);
        std::printf("seed: touch=payload lock=recording\n");
        std::printf("seed payload: values are the state at t0; a first-tick "
                    "difference near one dx means the payload was written at "
                    "t0+1\n");
    }
    if (t0 > 0 && !g_touchSeedArg.empty()) {
        int set = 0, unmapped = 0;
        for (const auto& ut : parseTouchPayload(g_touchSeedArg)) {
            int bit = -1;
            for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b)
                if (g_touch[b].uid == ut.first) { bit = (int)b; break; }
            if (bit < 0) { ++unmapped; continue; }
            init.trig |= touchBit(bit);
            init.fireB[bit] = (uint16_t)std::max(0, ut.second);
            ++set;
        }
        std::printf("seed touchseed: %d boxes set, %d named but not in this "
                    "build's window\n", set, unmapped);
    }
    if (t0 > 0 && g_ownsPortal) {
        int set = 0, unmapped = 0;
        for (int half = 0; half < 2; ++half) {
            const std::vector<int>& ids = half ? payloadPortal2 : payloadPortal;
            GravLatch& mask = half ? init.portalLatch2 : init.portalLatch;
            for (int uid : ids) {
                int bit = -1;
                double px = 0.0;
                for (const Obj& p : L.portals)
                    if (p.uid == uid && p.gpBit >= 0) { bit = p.gpBit; px = p.cx; break; }
                // --tplatch: a teleport in dyn holds a bit too.
                if (bit < 0)
                    for (const Obj& p : L.dyn.objs)
                        if (p.uid == uid && p.gpBit >= 0) { bit = p.gpBit; px = p.cx; break; }
                if (bit < 0) { ++unmapped; continue; }
                // a shared bit already handed on past this portal belongs to the later one
                bool handedOn = false;
                for (const GpHandoff& h : g_gpHandoff)
                    handedOn = handedOn || (h.bit == bit && px < h.x && (double)init.xAbs >= h.x);
                if (handedOn) continue;
                mask.set(bit);
                ++set;
            }
        }
        std::printf("seed payload: %d gravity portals spent (%s/%s), %d "
                    "named but not a gravity portal in this level\n",
                    set, gravLatchHex(init.portalLatch).c_str(),
                    gravLatchHex(init.portalLatch2).c_str(), unmapped);
        std::printf("seed: portal=payload\n");
    }
    // owns=hist: the per-body history values --start does not carry.
    if (t0 > 0 && g_ownsHist) {
        for (const auto& h : payloadHist) {
            if (h.first == "pressSpent") init.pressSpent = (uint8_t)(h.second != 0);
            else if (h.first == "pressSpent2")
                init.pressSpent2 = (uint8_t)(h.second != 0);
            else if (h.first == "freeMode" && g_freeModeCol)
                init.bandBranch = (uint8_t)((init.bandBranch & ~kBandFree)
                                            | kBandFreeKnown
                                            | (h.second != 0 ? kBandFree : 0));
            else if (h.first == "spiderJumpT" && h.second >= 0)
                init.spiderJumpT = (uint8_t)std::min(h.second, spiderJumpGraceTicks());
        }
        int dash2 = 0, dashRing2 = -1;
        for (const auto& h : payloadHist) {
            if (h.first == "dashing2") dash2 = h.second;
            else if (h.first == "dashRing2") dashRing2 = h.second;
        }
        if (init.dual && dash2 != 0) {
            const Obj* ring = nullptr;
            for (const Obj& o : L.orbs)
                if (o.uid == dashRing2) { ring = &o; break; }
            if (!ring)
                for (size_t i = 0; i < L.dyn.objs.size(); ++i)
                    if (L.dyn.objs[i].uid == dashRing2 && L.dyn.bucket[i] == Dynamics::ORB) {
                        ring = &L.dyn.objs[i];
                        break;
                    }
            init.dashing2 = 1;
            init.dashSlope2 = ring ? dashSlopeOf(ring, (int)init.frame) : 0.f;   // --dashclamp
            std::printf("seed: dashing2=1 ring uid %d%s slope %.6f\n", dashRing2,
                        ring ? "" : " NOT a ring in this level (slope 0)",
                        (double)init.dashSlope2);
        }
        if (init.pressSpent && (init.mode == 3 || init.mode == 7))
            init.action = 1;
        if (g_holdSeed && histVersion >= 6 && g_startHoldField > 0) {
            int jb = -1, jb2 = -1;
            for (const auto& h : payloadHist) {
                if (h.first == "jumpBuf") jb = h.second;
                else if (h.first == "jumpBuf2") jb2 = h.second;
            }
            if (jb == 0) init.holdDead = 1;
            if (init.dual && jb2 == 0) init.holdDead2 = 1;
            if (init.holdDead || init.holdDead2) init.action = 1;
            std::printf("seed: holdseed jumpBuf=%d jumpBuf2=%d -> holdDead=%d holdDead2=%d\n", jb,
                        jb2, (int)init.holdDead, (int)init.holdDead2);
        }
        std::printf("seed: hist=payload v%d pressSpent=%d pressSpent2=%d "
                    "freeMode=%s spiderJumpT=%d", histVersion, (int)init.pressSpent,
                    (int)init.pressSpent2,
                    !(init.bandBranch & kBandFreeKnown) ? "unknown"
                    : (init.bandBranch & kBandFree) ? "1" : "0",
                    (int)init.spiderJumpT);
        if (histVersion >= 4)
            for (const auto& h : payloadHist)
                if (h.first.rfind("slope", 0) == 0)
                    std::printf(" %s=%d", h.first.c_str(), h.second);
        std::printf("\n");
    }
    // --startages, the plan's button history behind the anchor (search_census.hpp).
    if (t0 > 0) {
        if (g_minPulse > 1 && g_startEdgeAge >= 0) init.edgeAge = (uint8_t)std::min(254, g_startEdgeAge);
        if (g_minPressGap > 1 && g_startPressAge >= 0) init.pressAge = (uint8_t)std::min(254, g_startPressAge);
        if ((g_startEdgeAge >= 0 || g_startPressAge >= 0) && init.held && !init.action) init.heldSeed = 1;
    }
    if (t0 <= 0 && g_freeModeCol) init.bandBranch |= kBandFreeKnown;
    if (t0 > 0 && !g_touch.empty()) {
        if (!havePayload)
            std::printf("seed: touch=recording lock=recording\n");
        int nCtl = 0, nNoRec = 0, nNoOff = 0, nNotEntered = 0, nEnteredRedated = 0;
        for (size_t b = 0; b < g_touch.size(); ++b) {
            const bool mayOpen = !g_touchEnteredGiven
                                 || g_touchEntered.count(g_touch[b].uid) != 0;
            if (!mayOpen) ++nNotEntered;
            bool keepRecFire = false;
            if (g_seedDump >= 0 || g_seedEvery > 0) {
                int passes = 0, movedNoOff = 0, offNoMove = 0, noRec = 0;
                for (const TrigCtl& c : g_touch[b].ctl) {
                    const auto itc = gt.find(c.uid);
                    if (itc == gt.end() || itc->second.empty()) { ++noRec; continue; }
                    const DynSample& f = itc->second.front();
                    double mv = 0.0;
                    for (const DynSample& s : itc->second)
                        mv = std::max(mv, std::hypot((double)s.cx - (double)f.cx,
                                                     (double)s.cy - (double)f.cy));
                    const bool hasOff = std::hypot((double)c.dx, (double)c.dy) > 0.5;
                    if (hasOff && mv > 0.5) ++passes;
                    else if (!hasOff && mv > 0.5) ++movedNoOff;
                    else if (hasOff) ++offNoMove;
                }
                const int win = (b < g_touchMoveTicks.size())
                                ? g_touchMoveTicks[b] : -1;
                std::printf("boxctl: box=%zu ctl=%zu seedable=%d "
                            "movedButNoTableOffset=%d offsetButNoMotion=%d "
                            "noRecording=%d window=%d%s\n",
                            b, g_touch[b].ctl.size(), passes, movedNoOff,
                            offNoMove, noRec, win,
                            (passes == 0 && movedNoOff > 0)
                                ? "  <- MISSED: recorded motion, no table offset"
                                : "");
            }
            for (const TrigCtl& c : g_touch[b].ctl) {
                ++nCtl;
                const auto it = gt.find(c.uid);
                const bool dbg = (g_dynDbg >= 0 && c.uid == g_dynDbg);
                if (it == gt.end() || it->second.empty()) {
                    ++nNoRec;
                    if (dbg)
                        std::printf("triggers: dyndbg uid=%d box=%zu -- no "
                                    "recording\n", c.uid, b);
                    continue;
                }
                if (std::hypot((double)c.dx, (double)c.dy) <= 0.5) ++nNoOff;
                if (dbg)
                    std::printf("triggers: dyndbg uid=%d box=%zu off=(%.1f,%.1f) "
                                "samples=%zu t[%d..%d]\n", c.uid, b,
                                (double)c.dx, (double)c.dy, it->second.size(),
                                it->second.front().t, it->second.back().t);
                long long entryT = -1;
                const DynSample* basePtr = &it->second.front();
                if (g_anchorEntryBase) {
                    const auto et = g_touchEnteredT.find(g_touch[b].uid);
                    if (et != g_touchEnteredT.end() && et->second > 0
                        && (long long)et->second <= t0) {
                        entryT = et->second;
                        const DynSample* nextPtr = nullptr;
                        for (const DynSample& s : it->second) {
                            if ((long long)s.t <= entryT) basePtr = &s;
                            else if (!nextPtr) nextPtr = &s;
                        }
                        if (nextPtr && basePtr != &it->second.front()
                            && entryT - (long long)basePtr->t <= 3
                            && (long long)nextPtr->t - entryT <= 3) {
                            entryT = -1;
                            basePtr = &it->second.front();
                        }
                    }
                }
                const DynSample& s0 = *basePtr;
                const DynSample* at = &s0;
                for (const DynSample& s : it->second)
                    if ((long long)s.t <= t0) at = &s;
                const double moved = std::hypot((double)at->cx - (double)s0.cx,
                                                (double)at->cy - (double)s0.cy);
                const double full = std::hypot((double)c.dx, (double)c.dy);
                if (c.lockTicks > 0.0
                    && std::hypot((double)c.dx, (double)c.dy) <= 0.5) {
                    const DynSample& f0 = it->second.front();
                    const DynSample* atL = &f0;
                    for (const DynSample& s : it->second)
                        if ((long long)s.t <= t0) atL = &s;
                    init.lockOff = (float)((double)atL->cx - (double)f0.cx);
                    if (dbg)
                        std::printf("triggers: dyndbg uid=%d box=%zu lock seed "
                                    "%.3f (recorded cx %.3f - base %.3f)\n",
                                    c.uid, b, (double)init.lockOff,
                                    (double)atL->cx, (double)f0.cx);
                }
                const int lag = recordLag((double)c.dx, (double)c.dy,
                                          c.durTicks, c.ease, c.erate);
                const bool useX = std::fabs((double)c.dx) > 0.5;
                const bool useY = std::fabs((double)c.dy) > 0.5;
                long long recFire = -1;
                for (const DynSample& s : it->second) {
                    if (entryT >= 0 && (long long)s.t <= entryT) continue;   // --anchorentry
                    const double ddx = (double)s.cx - (double)s0.cx;
                    const double ddy = (double)s.cy - (double)s0.cy;
                    const bool moved =
                        (useX || useY)
                            ? ((useX && std::fabs(ddx) > 0.05)
                               || (useY && std::fabs(ddy) > 0.05))
                            : (std::hypot(ddx, ddy) > 0.05);
                    if (moved) {
                        recFire = (long long)s.t - lag - 1;
                        recFire -= c.spawnLag;
                        if (g_seedDump >= 0 || g_seedEvery > 0)
                            std::printf("seedscan: box=%zu uid=%d firstRow=%d "
                                        "lag=%d recFire=%lld dx=%.2f dy=%.2f "
                                        "dur=%.3f ease=%d\n",
                                        b, c.uid, s.t, lag, recFire,
                                        (double)c.dx, (double)c.dy, c.durTicks,
                                        c.ease);
                        break;
                    }
                }
                if (havePayload) continue;
                if (!mayOpen) continue;
                if (full > 0.5 && moved > full * 0.5) {
                    init.trig |= touchBit(b);
                    keepRecFire = entryT >= 0 && recFire >= 0;   // --anchorentry
                    // far enough back that the move counts as finished
                    init.trigT = (int32_t)(t0 - (long long)std::llround(c.durTicks));
                    init.fireB[b] = (uint16_t)std::max(
                        0LL, recFire >= 0 ? recFire
                                          : t0 - (long long)std::llround(c.durTicks));
                    break;
                }
                if (full > 0.5 && moved > 0.5) {
                    init.trig |= touchBit(b);
                    keepRecFire = entryT >= 0 && recFire >= 0;   // --anchorentry
                    g_recPhase |= touchBit(b);
                    init.fireB[b] = (uint16_t)std::max(
                        0LL, recFire >= 0
                                 ? recFire
                                 : t0 - (long long)std::llround(
                                            (moved / full) * c.durTicks));
                    break;
                }
                if (full <= 0.5 && recFire >= 0 && recFire <= t0) {
                    init.trig |= touchBit(b);
                    keepRecFire = entryT >= 0;   // --anchorentry
                    init.fireB[b] = (uint16_t)std::max(0LL, recFire);
                    break;
                }
            }
            if (!havePayload && (init.trig & touchBit(b)) && !keepRecFire) {
                const auto et = g_touchEnteredT.find(g_touch[b].uid);
                if (et != g_touchEnteredT.end() && et->second > 0
                    && (long long)et->second <= t0) {
                    if (init.fireB[b] != (uint16_t)et->second) ++nEnteredRedated;
                    init.fireB[b] = (uint16_t)et->second;
                }
            }
        }
        if (!havePayload && g_touchEnteredGiven) {
            TouchMask arms{};
            for (const TouchTrig& R : g_touch)
                if (R.armBy) arms |= R.armBy;
            for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
                const TouchMask bit = touchBit((int)b);
                if (!(arms & bit) || (init.trig & bit)) continue;
                const auto et = g_touchEnteredT.find(g_touch[b].uid);
                if (et == g_touchEnteredT.end() || et->second <= 0
                    || (long long)et->second > t0)
                    continue;
                init.trig |= bit;
                init.fireB[b] = (uint16_t)et->second;
                init.trigT = std::max(init.trigT, (int32_t)et->second);
                std::printf("triggers: --tapclosebox: box %zu uid %d arms a spawned root and "
                            "was entered at t=%d <= t0, so the anchor starts with it set\n",
                            b, g_touch[b].uid, et->second);
            }
        }
        if (!havePayload && g_boxArm && g_recProvGiven && g_recProvRec == g_recProvAnchor && !g_xTrack.empty()) {
            const double xP1 = xRefAt((int)t0);
            const double dx2 = init.dual ? (double)init.dx2 : 0.0;
            size_t nSet = 0;
            for (size_t k = 0; k < g_autoTrig.size(); ++k) {
                const AutoTrig& A = g_autoTrig[k];
                if (!A.offInit) continue;
                const int cross = xRefCross(A.cx);
                if (cross < 1) continue;
                int fired = -1;
                for (size_t i = 0; i < L.dyn.objs.size() && i < L.dyn.autoParts.size() && fired < 0; ++i) {
                    const Dynamics::AutoPart* mine = nullptr;
                    for (const Dynamics::AutoPart& p : L.dyn.autoParts[i])
                        if (p.trig == (int)k) mine = &p;
                    if (!mine || std::fabs(mine->dx) > 0.001f) continue;
                    const int w0 = cross - 1, w1 = cross + (int)std::ceil(mine->dur) + 3;
                    if ((long long)w1 > g_recProvReach) continue;
                    bool overlap = false;
                    for (const Dynamics::AutoPart& p : L.dyn.autoParts[i]) {
                        if (&p == mine || std::fabs(p.dy) < 0.001f) continue;
                        const int c2 = xRefCross(g_autoTrig[(size_t)p.trig].cx);
                        if (c2 >= 0 && c2 <= w1 && c2 + (int)std::ceil(p.dur) + 3 >= w0) overlap = true;
                    }
                    if (overlap) continue;
                    const std::vector<DynSample>& sm = L.dyn.samples[i];
                    if (sm.size() < 2 || sm.front().t > w0 || sm.back().t < w1) continue;
                    auto cyAt = [&](int t) {
                        size_t lo = 0, hi = sm.size();
                        while (lo + 1 < hi) {
                            const size_t md = (lo + hi) / 2;
                            if (sm[md].t <= t) lo = md; else hi = md;
                        }
                        return (double)sm[lo].cy;
                    };
                    const double d = cyAt(w1) - cyAt(w0);
                    if (std::fabs(d - (double)mine->dy) < 1.0) fired = 1;
                    else if (std::fabs(d) < 1.0) fired = 0;
                }
                if (fired != 1) continue;
                for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
                    const TouchTrig& B = g_touch[b];
                    bool arms = false;
                    for (const TouchTrig::TogEv& e : B.trigTog) arms = arms || (e.on && e.uid == A.uid);
                    if (!arms) continue;
                    if (init.trig & touchBit((int)b)) break;   // already set: this Move has its block
                    const double reach = B.cx + B.hw;
                    if (!(reach < xP1 || (dx2 > 0.0 && reach < xP1 + dx2))) continue;   // no body past it yet
                    const int at = std::max(0, std::min(xRefCross(B.cx - B.hw - dx2), cross - 1));
                    init.trig |= touchBit((int)b);
                    init.fireB[b] = (uint16_t)at;
                    init.trigT = std::max(init.trigT, (int32_t)at);
                    ++nSet;
                    std::printf("boxarm: box %zu uid %d starts set at t0=%lld - the recording fired the Move uid %d "
                                "on its crossing t=%d; dated t=%d\n", b, B.uid, t0, A.uid, cross, at);
                    break;
                }
            }
            std::printf("boxarm: anchor t0=%lld, %zu block(s) set from the recording\n", t0, nSet);
        }
        std::printf("triggers: anchor scan t0=%lld: %d controlled objects, "
                    "%d with no recording, %d with no offset\n",
                    t0, nCtl, nNoRec, nNoOff);
        if (g_touchEnteredGiven)
            std::printf("triggers: --touchentered names %zu boxes (%zu with an entry "
                        "tick, %d opened boxes re-dated by it); %d of %zu boxes "
                        "may not open from the recording\n",
                        g_touchEntered.size(), g_touchEnteredT.size(), nEnteredRedated,
                        nNotEntered, g_touch.size());
        if (init.trig)
            std::printf("triggers: anchor starts with mask 0x%llx (already open "
                        "in the recording)\n",
                        (unsigned long long)init.trig.word(0));
        if (g_recPhase)
            std::printf("triggers: mask 0x%llx was still MOVING at the anchor -- "
                        "replayed from the recording's own phase\n",
                        (unsigned long long)g_recPhase.word(0));
    }
    if (!g_autoTrig.empty() && x0 > 0.0) {
        double dxA = kDxF;
        {
            size_t si = 0;
            auto gate = [&](const Obj& o) { return o.cx - o.hw - kCubeHalf; };
            while (si < L.speeds.size() && gate(L.speeds[si]) <= x0) {
                dxA = dxForSpeedId(L.speeds[si].id);
                ++si;
            }
        }
        bool canReverse = false;
        for (const std::vector<Obj>* v : {&L.orbs, &L.pads, &L.dyn.objs})
            for (const Obj& o : *v) canReverse = canReverse || o.rev != 0;
        const auto sharedPast = [&](int rf) {
            return canReverse && rf >= 0 && (long long)rf <= (long long)t0;
        };
        int behind = 0, fromX = 0, fromRec = 0, xTracked = 0;
        for (size_t b = 0; b < g_autoTrig.size(); ++b) {
            AutoTrig& A = g_autoTrig[b];
            int rf = -1;
            for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                if (L.dyn.autoAnchor[i] != (int)b || L.dyn.trigRecFire[i] < 0)
                    continue;
                // --anchornoop: its first recorded change is not this trigger's effect.
                if (i < L.dyn.anchorUnrec.size() && L.dyn.anchorUnrec[i])
                    continue;
                const int tr = L.dyn.trigRecFire[i] - L.dyn.autoLag[i];
                if (rf < 0 || tr < rf) rf = tr;
            }
            if (A.cx > x0) {
                if ((g_trigRaw || sharedPast(rf)) && rf >= 0) {
                    A.fireT = rf;
                    A.fireX = A.lockLastX = A.cx;
                    if (g_dynDbg >= 0)
                        std::printf("anchortrig(ahead) uid=%d cx=%.1f rf=%d "
                                    "-> fireT=%d\n", A.uid, A.cx, rf, A.fireT);
                    continue;
                }
                if (canReverse && A.fireT < 0) {
                    for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                        if (L.dyn.autoParts[i].size() != 1
                            || L.dyn.autoParts[i][0].trig != (int)b
                            || L.dyn.autoParts[i][0].dur > 0.0
                            || L.dyn.samples[i].size() < 2)
                            continue;
                        const Dynamics::AutoPart& p = L.dyn.autoParts[i][0];
                        const std::vector<DynSample>& sm = L.dyn.samples[i];
                        const double bx = sm[0].cx, by = sm[0].cy;
                        for (const DynSample& s : sm) {
                            if ((long long)s.t > t0) break;
                            if (std::fabs((s.cx - bx) - p.dx) > 1.0
                                || std::fabs((s.cy - by) - p.dy) > 1.0)
                                continue;
                            A.fireT = s.t;
                            A.fireX = A.lockLastX = A.cx;
                            ++fromRec;
                            std::printf("anchortrig: ahead trigger uid=%d cx=%.1f "
                                        "was unresolved -> fireT=%d from uid%d's "
                                        "recording (dur=%.2f d=(%.1f,%.1f))\n",
                                        A.uid, A.cx, A.fireT, L.dyn.objs[i].uid,
                                        p.dur, (double)p.dx, (double)p.dy);
                            break;
                        }
                        if (A.fireT >= 0) break;
                    }
                }
                continue;
            }
            long long est = t0 - (long long)std::floor(
                (x0 - A.cx) / (dxA > 0.0 ? dxA : (double)kDxF)) + A.delay;
            // --xtrack: the crossing read off GD's own x, when the track reaches it.
            if (!g_xTrack.empty())
                for (const auto& r : g_xTrack)
                    if (r.second >= A.cx) {
                        est = (long long)r.first + A.delay;
                        ++xTracked;
                        break;
                    }
            const int estT = (int)std::max(0LL, est);
            if (rf >= 0 && (g_trigRaw || sharedPast(rf)
                            || std::llabs((long long)rf - (long long)estT) <= 60)) {
                A.fireT = rf;
            } else {
                A.fireT = estT;
                if (rf >= 0) ++fromX;
            }
            A.fireX = A.lockLastX = A.cx;
            for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                if (L.dyn.autoLockTrig[i] != (int)b
                    || L.dyn.samples[i].size() < 2)
                    continue;
                bool xFree = true;
                for (const auto& p : L.dyn.autoParts[i])
                    xFree = xFree && std::fabs(p.dx) <= 0.001f;
                if (!xFree) continue;
                const auto& sm = L.dyn.samples[i];
                const DynSample* at = &sm.front();
                for (const DynSample& s : sm)
                    if ((long long)s.t <= t0) at = &s;
                const double D = (double)at->cx - (double)sm.front().cx;
                A.fireX = x0 - D;
                A.lockLastX = x0;
                break;
            }
            if (g_dynDbg >= 0)
                std::printf("anchortrig uid=%d cx=%.1f rf=%d estT=%d -> fireT=%d "
                            "(x0=%.2f dxA=%.5f t0=%lld)\n",
                            A.uid, A.cx, rf, estT, A.fireT, x0, dxA, (long long)t0);
            ++behind;
        }
        if (behind || fromRec)
            std::printf("autotrig: %d behind the anchor (%d re-dated from x, "
                        "the recording disagreed by >60 ticks); %d ahead of it "
                        "resolved from a recording\n", behind, fromX, fromRec);
        if (!g_xTrack.empty())
            std::printf("autotrig: %d of the %d behind the anchor dated by GD's x (--xtrack, "
                        "%zu rows)\n", xTracked, behind, g_xTrack.size());
    }
    if (g_recAnchorX)
        std::printf("recanchorx: %d objects re-paired on the recording run's crossing of their anchor%s\n",
                    g_recAnchorXN, g_xTrack.empty() ? " (no --xtrack: none decided)" : "");
    if (g_rotLag)
        std::printf("rotlag: %d objects anchored on a Rotate given the rotation's threshold lag\n", g_rotLagN);
    if (g_rotTail)
        std::printf("rottail: %d turned objects go on turning past the recording's end\n", g_rotTailN);
    if (g_recEndTail)
        std::printf("recendtail: %d of %d rig objects go on past their last row as the simulated rig%s; %d "
                    "objects' switches go on past it\n", g_recEndTailN, g_recEndTailTried,
                    g_xTrack.empty() ? " (no --xtrack: none simulated)" : "", g_recEndTailOnN);
    if (g_formulaRec) {
        g_touchRecEntry.fill(-1);
        int nRec = 0;
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
            const auto et = g_touchEnteredT.find(g_touch[b].uid);
            if (et != g_touchEnteredT.end() && et->second > 0 && (long long)et->second <= t0) {
                g_touchRecEntry[b] = et->second;
                ++nRec;
            } else if (g_formulaRecTouch && g_recProvGiven) {   // --formularectouch
                const auto rt = g_recTouch.find(g_touch[b].uid);
                if (rt != g_recTouch.end() && rt->second > 0 && (long long)rt->second <= g_recProvReach) {
                    g_touchRecEntry[b] = rt->second;
                    ++nRec;
                }
            }
        }
        size_t nForm = 0;
        for (size_t i = 0; i < L.dyn.objs.size(); ++i)
            if (i < L.dyn.formula.size() && L.dyn.formula[i]) ++nForm;
        std::printf("formularec: %zu formula-driven objects, %d boxes with the attempt's entry tick\n",
                    nForm, nRec);
        if (!g_recProvGiven)
            std::printf("formularec: no --recprov, so the recording's attempt is unknown and it is not read\n");
        else
            std::printf("formularec: recording of attempt %lld, anchor rows of attempt %lld, reached t=%lld -> %s\n",
                        g_recProvRec, g_recProvAnchor, g_recProvReach,
                        (g_recProvRec >= 0 && g_recProvRec == g_recProvAnchor) ? "read up to that tick"
                                                                                : "another attempt, not read");
    }
    if (g_slamBox) {
        g_recTouchBit.fill(-1);
        int nRec = 0;
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
            const auto rt = g_recTouch.find(g_touch[b].uid);
            if (rt != g_recTouch.end() && rt->second > 0) {
                g_recTouchBit[b] = rt->second;
                ++nRec;
            }
        }
        if (g_recTouchGiven)
            std::printf("slambox: --rectouch names %zu boxes, %d of them loaded with an entry tick\n",
                        g_recTouch.size(), nRec);
        else
            std::printf("slambox: no --rectouch on this call, the slam steps follow --slamdedup (%s)\n",
                        g_slamDedup ? "on" : "off");
    }
    if (g_dynDbg >= 0) {
        bool found = false;
        for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
            if (L.dyn.objs[i].uid != g_dynDbg) continue;
            found = true;
            const auto& sm = L.dyn.samples[i];
            std::printf("dyndbg: uid=%d IN DYN  samples=%zu", g_dynDbg, sm.size());
            if (!sm.empty())
                std::printf("  first(t=%d cy=%.3f)  last(t=%d cy=%.3f)",
                            sm.front().t, sm.front().cy, sm.back().t, sm.back().cy);
            std::printf("\n  closed=%d  trigMask=0x%llx recFire=%d (lag %d -> "
                        "true %d; autoLag %d)  autoAnchor=%d autoD=(%.1f,%.1f) "
                        "autoDur=%.2f ease=%d/%.2f\n",
                        (int)L.dyn.autoClosed[i],
                        (unsigned long long)L.dyn.trigMask[i].word(0),
                        L.dyn.trigRecFire[i], L.dyn.recLag[i],
                        L.dyn.trigRecFire[i] >= 0
                            ? L.dyn.trigRecFire[i] - L.dyn.recLag[i] : -1,
                        L.dyn.autoLag[i],
                        L.dyn.autoAnchor[i], L.dyn.autoDx[i], L.dyn.autoDy[i],
                        L.dyn.autoDur[i], L.dyn.autoEase[i], L.dyn.autoErate[i]);
            const int aA = L.dyn.autoAnchor[i];
            if (aA >= 0)
                std::printf("  anchor trigger: uid=%d cx=%.1f fireT=%d delay=%d\n",
                            g_autoTrig[(size_t)aA].uid, g_autoTrig[(size_t)aA].cx,
                            g_autoTrig[(size_t)aA].fireT,
                            g_autoTrig[(size_t)aA].delay);
        }
        if (!found)
            std::printf("dyndbg: uid=%d is NOT in dyn (static grid)\n", g_dynDbg);
    }
    if (g_needUnseen) {
        for (size_t b = 0; b < g_touch.size(); ++b) {
            bool seenIt = false;
            for (size_t i = 0; i < L.dyn.objs.size() && !seenIt; ++i)
                if ((L.dyn.trigMask[i] & touchBit(b)) && L.dyn.trigRecFire[i] >= 0)
                    seenIt = true;
            // ...and one the anchor already entered is settled, not unknown.
            if (!seenIt && !(init.trig & touchBit(b))) {
                const bool passed = g_touch[b].cx + g_touch[b].hw + 40.0 < x0;
                const bool skip = (g_needSkip & touchBit(b)).any();
                std::printf("needtrig: box=%zu cx=%.0f cy=%.0f hw=%.0f "
                            "passed=%d skipped=%d\n",
                            b, g_touch[b].cx, g_touch[b].cy, g_touch[b].hw,
                            passed ? 1 : 0, skip ? 1 : 0);
                if (skip) continue;
                g_needTrig |= touchBit(b);
                // Same facts, for a caller with no stdout to read (dp/progress.hpp)
                g_outcome.needTrigMask |= touchBit(b);
                if (passed) g_outcome.needTrigPassed |= touchBit(b);
            }
        }
    }
    for (const int u : g_needTrigUids) {
        int bit = -1;
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b)
            if (g_touch[b].uid == u) { bit = (int)b; break; }
        if (bit < 0) {
            std::printf("needtrig-uid: uid %d is not a box of this call's window - not required\n", u);
            continue;
        }
        const TouchTrig& T = g_touch[(size_t)bit];
        if (init.trig & touchBit(bit)) {
            std::printf("needtrig-uid: uid %d (box %d) is already entered at the anchor\n", u, bit);
            continue;
        }
        if (T.cx + T.hw + 40.0 < x0) {
            std::printf("needtrig-uid: uid %d (box %d, cx=%.0f) is behind the anchor x=%.0f - not "
                        "required\n", u, bit, T.cx, x0);
            continue;
        }
        g_needTrig |= touchBit(bit);
        std::printf("needtrig-uid: uid %d = box %d cx=%.0f cy=%.0f hw=%.0f - required\n", u, bit,
                    T.cx, T.cy, T.hw);
    }
    if (rotQPath.empty()) rotQPath = rotQPathBeside(argv[1]);
    if (!rotQPath.empty() && !g_rotTrig.empty() && !loadRotQueue(rotQPath))
        std::printf("rotq: could not read %s\n", rotQPath.c_str());
    if (g_rotQ.size() > 32) {
        std::printf("rotq: %zu entries, but State::rotSpent is a 32-bit cursor "
                    "- entries 33+ would be consumed without being recorded. "
                    "Refusing the queue on this level.\n", g_rotQ.size());
        g_rotQueue = false;
        g_rotQ.clear();
    }
    if (!g_rotQ.empty())
        for (int ch = 0; ch <= 15; ++ch)
            for (int k = g_rotQBeg[(size_t)ch]; k < g_rotQEnd[(size_t)ch]; ++k) {
                const RotQEntry& e = g_rotQ[(size_t)k];
                char b[192];
                std::snprintf(b, sizeof b, "%s%d,%d,%.4f,%.4f,%d,%d,%d,%d,%d",
                              g_outcome.rotQOrder.empty() ? "" : ";", ch, e.uid,
                              (double)e.px, (double)e.py, (int)e.swarm, (int)e.swch,
                              (int)e.chanOnly,
                              e.rotIdx >= 0 ? g_rotTrig[(size_t)e.rotIdx].gndDir : -1,
                              (int)e.id);
                g_outcome.rotQOrder += b;
            }
    g_rotQOff.assign(g_rotQ.size(), 0);
    g_rotQOn.assign(g_rotQ.size(), 0);
    g_rotTrigOff.assign(g_rotTrig.size(), 0);
    g_rotTrigOn.assign(g_rotTrig.size(), 0);
    if (g_rotQToggle) {
        auto maskFor = [](int uid, int want) {
            uint32_t m = 0;
            for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
                if (g_touch[b].togOn != want) continue;
                for (const TrigCtl& ct : g_touch[b].ctl)
                    if (ct.uid == uid) { m |= (uint32_t)touchBit((int)b).word(0); break; }
            }
            return m;
        };
        for (size_t k = 0; k < g_rotQ.size(); ++k) {
            g_rotQOff[k] = maskFor(g_rotQ[k].uid, 0);
            g_rotQOn[k] = maskFor(g_rotQ[k].uid, 1);
            if (g_rotQOff[k] | g_rotQOn[k])
                std::printf("rotqtoggle: queue uid %d off=0x%x on=0x%x\n",
                            g_rotQ[k].uid, g_rotQOff[k], g_rotQOn[k]);
        }
        for (size_t k = 0; k < g_rotTrig.size(); ++k) {
            g_rotTrigOff[k] = maskFor(g_rotTrig[k].uid, 0);
            g_rotTrigOn[k] = maskFor(g_rotTrig[k].uid, 1);
        }
    }
    if (g_startRotChan >= 0) {
        if (g_rotQ.empty()) {
            std::printf("startrotq: no rotation queue on this level - the seed "
                        "has nothing to bind to and is ignored\n");
            g_outcome.startRotHit = 0;
            g_outcome.startRotGiven = (int)g_startRotSpent.size();
            g_outcome.startRotMiss = "(no queue)";
        } else {
            init.rotChan = (uint8_t)g_startRotChan;
            init.rotRev = (uint16_t)g_startRotRev;
            init.rotSpent = 0;
            int hit = 0;
            std::string miss;
            for (int su : g_startRotSpent) {
                bool found = false;
                for (size_t q = 0; q < g_rotQ.size() && q < 32; ++q)
                    if (g_rotQ[q].uid == su) {
                        init.rotSpent |= (uint32_t)1 << q;
                        found = true;
                        ++hit;
                        break;
                    }
                if (!found) {
                    miss += miss.empty() ? "" : ",";
                    miss += std::to_string(su);
                }
            }
            std::printf("startrotq: chan=%d rev=0x%04x spent=%d/%zu of %zu "
                        "queued%s%s\n",
                        (int)init.rotChan, (unsigned)init.rotRev, hit,
                        g_startRotSpent.size(), g_rotQ.size(),
                        miss.empty() ? "" : "  NOT IN THE QUEUE: ",
                        miss.c_str());
            g_outcome.startRotHit = hit;
            g_outcome.startRotGiven = (int)g_startRotSpent.size();
            g_outcome.startRotMiss = miss;
        }
    }
    std::printf("level: %zu colliders, %zu portals, %zu pads, %zu orbs, "
                "%zu moving, maxX=%.0f\n",
                L.objs.size(), L.portals.size(), L.pads.size(), L.orbs.size(),
                L.dyn.size(), L.maxX);
    if (!g_rotTrig.empty()) {
        std::printf("rotation: %zu gameplay-rotation objects (id 2900)\n",
                    g_rotTrig.size());
        for (const RotTrig& r : g_rotTrig)
            std::printf("  rot: (%.0f,%.0f) frame=%d travel=%s\n",
                        r.cx, r.cy, r.frame,
                        r.frame == 0 ? "+X" : r.frame == 1 ? "-Y"
                        : r.frame == 2 ? "-X" : "+Y");
    }
    if (!g_rotQ.empty()) {
        std::printf("rotq: %zu queued objects in %d channels\n",
                    g_rotQ.size(), g_rotQChans);
        for (int ch = 0; ch <= 15; ++ch) {
            if (g_rotQEnd[(size_t)ch] <= g_rotQBeg[(size_t)ch]) continue;
            int d = 4;
            for (const RotQEntry& e : g_rotQ)
                if (e.swarm && e.swch == ch && e.rotIdx >= 0) {
                    d = rotQDirection(g_rotTrig[(size_t)e.rotIdx].rawRot,
                                      g_rotTrig[(size_t)e.rotIdx].flipX != 0);
                    break;
                }
            std::printf("  rotq ch=%d dir=%s n=%d\n", ch,
                        d == 1 ? "y+" : d == 2 ? "y-" : d == 3 ? "x-" : "x+",
                        g_rotQEnd[(size_t)ch] - g_rotQBeg[(size_t)ch]);
            for (int k = g_rotQBeg[(size_t)ch]; k < g_rotQEnd[(size_t)ch]; ++k) {
                const RotQEntry& e = g_rotQ[(size_t)k];
                std::printf("    [%d] uid=%d ord=%d (%.0f,%.0f) swarm=%d "
                            "swch=%d chanOnly=%d gnddir=%d rev=%d\n",
                            k - g_rotQBeg[(size_t)ch], e.uid, e.ord, e.px, e.py,
                            e.swarm, e.swch, e.chanOnly,
                            e.rotIdx >= 0 ? g_rotTrig[(size_t)e.rotIdx].gndDir : -1,
                            e.rotIdx >= 0
                                ? ((unsigned)(g_rotTrig[(size_t)e.rotIdx].gndDir - 2) < 2u)
                                : 0);
            }
        }
    }
    if (L.dyn.anyAuto) {
        size_t autoN = 0, closedN = 0, closedRec = 0, lockN = 0;
        for (size_t i = 0; i < L.dyn.size(); ++i) {
            if (L.dyn.autoAnchor[i] < 0) continue;
            ++autoN;
            if (L.dyn.autoLock[i] > 0.0) ++lockN;
            if (!L.dyn.autoClosed[i]) continue;
            ++closedN;
            if (L.dyn.trigRecFire[i] >= 0) ++closedRec;
        }
        std::printf("autotrig: %zu/%zu moved objects match the closed form "
                    "(%zu of them checked against a recording, %zu follow the "
                    "player)%s\n",
                    closedN, autoN, closedRec, lockN,
                    ", --trigclosed ON");
    }
    auto rideAtAnchor = [&](uint8_t grounded, uint8_t flip, float y,
                            uint8_t& onSlope, float& slopeM, uint8_t& slopeT,
                            uint8_t& rideLanded) {
        if (!grounded) return;
        const double pH = init.mini ? kMiniHalf : kCubeHalf;
        for (const Obj& sp : L.slopes) {
            const double sx0 = sp.cx - sp.hw, sx1 = sp.cx + sp.hw;
            const double m = (sp.sy1 - sp.sy0) / (sx1 - sx0);
            if (m == 0.0) continue;
            const double mEff = flip ? -m : m;
            const double xr0 =
                x0 + (mEff > 0 ? slopeXOffset(m, pH) : -slopeXOffset(m, pH));
            if (xr0 < sx0 || xr0 > sx1 + 1.5) continue;
            const double top =
                sp.sy0 + m * (std::min(xr0, sx1) - sx0) + (flip ? -pH : pH);
            if (std::fabs((double)y - top) > 0.6) continue;
            onSlope = 1;
            slopeM = (float)m;
            slopeT = 24;
            rideLanded = 1;
            break;
        }
    };
    rideAtAnchor(init.grounded, init.flip, init.y,
                 init.onSlope, init.slopeM, init.slopeT, init.rideLanded);
    if (init.dual)
        rideAtAnchor(init.grounded2, init.flip2, init.y2,
                     init.onSlope2, init.slopeM2, init.slopeT2,
                     init.rideLanded2);
    if (init.onSlope) init.rideLanded = 1;
    if (init.dual && init.onSlope2) init.rideLanded2 = 1;
    if (g_anchorRide && t0 > 0) {
        constexpr int kRideAgeLag = 1;
        auto histVal = [&](const char* name) {
            for (const auto& h : payloadHist) if (h.first == name) return h.second;
            return -1;
        };
        const int on = histVal("slopeOn"), under = histVal("slopeUnder");
        const int uid = histVal("slopeUid"), age = histVal("slopeAge");
        const int landed = histVal("slopeLanded");
        const Obj* ramp = nullptr;
        if (uid >= 0) {
            for (const Obj& sp : L.slopes)
                if (sp.uid == uid) { ramp = &sp; break; }
            if (!ramp)
                for (const Obj& o : L.dyn.objs)
                    if (o.slope && o.uid == uid) { ramp = &o; break; }
        }
        const double m = (ramp && ramp->hw > 0.0)
            ? (ramp->sy1 - ramp->sy0) / (2.0 * ramp->hw) : 0.0;
        const char* skip = nullptr;
        if (!g_ownsHist || histVersion < 4) skip = "no version-4 hist payload";
        else if (on != 1) skip = on == 0 ? "off a ramp (slopeOn=0)" : "slopeOn not said";
        else if (under < 0 || age < 0) skip = "slopeUnder / slopeAge not said";
        else if (!ramp) skip = "the uid is not a ramp in this level";
        else if (m == 0.0) skip = "the ramp is flat";
        else if (init.frame != 0) skip = "a rotated frame";
        else if (under == 0 && landed < 0) skip = "slopeLanded not said";
        else if (under == 0 && init.mode == 4) skip = "a floor ride on a wave";
        if (skip) {
            std::printf("anchorride: nothing seeded, %s (slopeOn=%d slopeUnder=%d "
                        "slopeUid=%d slopeAge=%d slopeLanded=%d)\n",
                        skip, on, under, uid, age, landed);
        } else {
            const int A = std::max(0, age - kRideAgeLag);
            const int wasOn = init.onSlope, wasT = init.slopeT;
            const char* regime = nullptr;
            if (under == 1) {
                regime = "underside";
                init.onSlope = 0; init.slopeT = 0; init.rideLanded = 0; init.slopeM = 0.f;
                init.seatT = 0;
                init.ceilT = (uint8_t)std::min(200, A + 1);
                init.ceilM4 = (uint8_t)std::min<long>(255, std::lround(std::fabs(m) * 4.0));
                init.ceilMode = init.mode;
            } else if (landed == 1) {
                regime = "floor-landed";
                init.onSlope = 1;
                init.slopeT = (uint8_t)std::min(24, A);
                init.rideLanded = 1;
                init.slopeM = (float)m;
                init.slopeUid0 = uid;
                init.slopeUidNow = uid;
                init.seatT = 0;
                init.ceilT = 0; init.ceilM4 = 0;
            } else {
                regime = "floor-seat";
                init.onSlope = 0; init.slopeT = 0; init.rideLanded = 0; init.slopeM = 0.f;
                init.seatT = (uint8_t)std::min(24, A + 1);
                init.ceilT = 0; init.ceilM4 = 0;
            }
            std::printf("anchorride: %s uid=%d m=%.4f ceilRamp=%d flip=%d mode=%d "
                        "slopeAge=%d A=%d slopeLanded=%d -> onSlope=%d slopeT=%d "
                        "rideLanded=%d slopeM=%.4f seatT=%d ceilT=%d ceilM4=%d ceilMode=%d "
                        "(the geometric guess had onSlope=%d slopeT=%d)\n",
                        regime, uid, m, slopeIsCeiling(ramp->slopeDir) ? 1 : 0,
                        (int)init.flip, (int)init.mode, age, A, landed,
                        (int)init.onSlope, (int)init.slopeT, (int)init.rideLanded,
                        (double)init.slopeM, (int)init.seatT, (int)init.ceilT,
                        (int)init.ceilM4, (int)init.ceilMode, wasOn, wasT);
        }
    }
    if (t0 > 0) {
        const int t1 = (int)t0 + 1;
        const double half = playerHalf(init.mode, init.mini != 0);
        auto over = [&](double bx, double by, double hw, double hh) {
            return init.frame == 0 && std::fabs(x0World - bx) <= hw + half
                   && std::fabs((double)init.y - by) <= hh + half;
        };
        for (const FlipHeadBox& b : g_flipHeadBoxes) {
            if (rideStage(b, t1) != 1) continue;
            double bx, by;
            flipHeadAt(b, t1, bx, by);
            const bool on = over(bx, by, b.hw, b.hh);
            if (on) init.fgArm = (uint8_t)kArmTicks;
            std::printf("ridebox: anchor t=%lld inside fliphead uid %d's ride - %s "
                        "(box %.2f,%.2f, frame %d)\n", t0, b.uid,
                        on ? "riding with it, armed" : "not overlapping it, unarmed", bx, by,
                        (int)init.frame);
        }
        for (const ArmBox& b : g_armBoxes) {
            if (rideStage(b, t1) != 1) continue;
            double bx, by;
            armBoxAt(b, t1, bx, by);
            const bool on = over(bx, by, b.hw, b.hh);
            if (on && init.armT >= (uint8_t)kArmTicks) init.armT = 0;
            std::printf("ridebox: anchor t=%lld inside ceilarm uid %d's ride - %s "
                        "(box %.2f,%.2f, frame %d, armT %d)\n", t0, b.uid,
                        on ? "riding with it, armed" : "not overlapping it, unarmed", bx, by,
                        (int)init.frame, (int)init.armT);
        }
    }
    double goalX = L.maxX + 60.0;
    // --goalgd (speed.hpp): GD's own end, the end portal's spawn x
    if (g_goalGd && g_endPortalX > 0.0 && !g_endPortalFlipX) {
        const double zoom = camScaleAt(g_endPortalX - 300.0, (double)kDx, -1);
        const double gdGoal = g_endPortalX - 300.0 / (zoom > 0.0 ? zoom : 1.0);
        std::printf("goalgd: end portal x %.2f, zoom %.3f -> goal %.2f (L.maxX + 60 = %.2f)\n",
                    g_endPortalX, zoom, gdGoal, goalX);
        goalX = gdGoal;
    }
    g_goalX = goalX;   // --goalspare (speed.hpp)
    const uint32_t coinAll = g_coinRoute && !L.coins.empty()
        ? (uint32_t)((1u << L.coins.size()) - 1u) : 0u;
    if (g_coinRoute) {
        if (L.coins.size() > 8) {
            std::printf("coins: %zu is more than the 8 the mask holds -- "
                        "routing disabled\n", L.coins.size());
        } else if (L.coins.empty()) {
            std::printf("coins: none in this level\n");
        } else {
            std::printf("coins: %zu to collect, mask 0x%x\n",
                        L.coins.size(), coinAll);
            for (size_t i = 0; i < L.coins.size(); ++i)
                std::printf("  coin %zu: (%.0f,%.0f) box %.1fx%.1f\n", i,
                            L.coins[i].cx, L.coins[i].cy,
                            L.coins[i].hw * 2, L.coins[i].hh * 2);
        }
    }
    const bool coinOn = coinAll != 0 && L.coins.size() <= 8;
    const bool gatesOn = coinOn || g_worldGates;
    std::vector<CoinCountReq> coinReq;
    if (coinOn) {
        for (const TouchTrig& T : g_touch) {
            if (T.count < 0 || T.cmode != 2) continue;   // only `n < C` bites
            for (const TrigCtl& c : T.ctl) {
                int ci = -1;
                for (size_t i = 0; i < L.coins.size(); ++i)
                    if (L.coins[i].uid == c.uid) { ci = (int)i; break; }
                if (ci < 0) continue;
                const double reach = L.coins[(size_t)ci].hh + 15.0;
                if (std::fabs((double)c.dy) <= reach) continue;   // not decisive
                coinReq.push_back({T.item, T.count, ci});
                std::printf("coins: coin %d needs item %d to reach %d before it"
                            " (uid %d moves it %.0f px, reach %.0f)\n",
                            ci, T.item, T.count, c.uid, (double)c.dy, reach);
            }
        }
    }
    std::unordered_map<int, TouchMask> itemBits;   // item -> bits that give it
    std::vector<std::vector<std::pair<int, int>>> bitItemGive(g_touch.size());
    struct TapGive {
        TouchMask bit{};
        int item = 0, per = 0;
        double armX = 0.0, closeX = 1e18;
        int armChan = 0, closeChan = 0;
        double armY = 0.0, closeY = 0.0;
        TouchMask armBy{};
        bool closeBox = false;
        double closeHw = 0.0, closeHh = 0.0;
        int closeUid = -1;
    };
    TapGive tapGive;   // bit 0 = none
    if (gatesOn && !g_itemGiver.empty()) {
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
            std::vector<std::pair<int, int>>& v = bitItemGive[b];
            for (const TrigCtl& c : g_touch[b].ctl) {
                const auto gi = g_itemGiver.find(c.uid);
                if (gi == g_itemGiver.end()) continue;
                bool isPickup = false;
                for (const auto& col : g_collect)
                    if (col.uid == c.uid) { isPickup = true; break; }
                if (isPickup) continue;
                bool seen = false;
                for (auto& p : v)
                    if (p.first == gi->second) { ++p.second; seen = true; break; }
                if (!seen) v.push_back({gi->second, 1});
            }
            if (g_touch[b].tap && v.size() == 1
                && (g_touch[b].tapCloseX < 1e17 || (g_touch[b].armBy))) {
                if (!tapGive.bit) {
                    tapGive.bit = touchBit((int)b);
                    tapGive.armBy = g_touch[b].armBy;
                    tapGive.item = v[0].first;
                    tapGive.per = v[0].second;
                    tapGive.armX = g_touch[b].cx;
                    tapGive.armY = g_touch[b].cy;
                    tapGive.armChan = g_touch[b].tapChan;
                    tapGive.closeX = g_touch[b].tapCloseX;
                    tapGive.closeY = g_touch[b].tapCloseY;
                    tapGive.closeChan = g_touch[b].tapCloseChan;
                    tapGive.closeBox = g_touch[b].tapCloseTouch
                                       && g_touch[b].tapCloseX < 1e17;
                    tapGive.closeHw = g_touch[b].tapCloseHw;
                    tapGive.closeHh = g_touch[b].tapCloseHh;
                    tapGive.closeUid = g_touch[b].tapCloseUid;
                    std::printf("items: tap %zu uid %d gives item %d x%d PER PRESS,"
                                " open from (%.0f,%.0f) on channel %d to the Stop at"
                                " (%.0f,%.0f) on channel %d\n", b, g_touch[b].uid,
                                tapGive.item, tapGive.per, tapGive.armX, tapGive.armY,
                                tapGive.armChan, tapGive.closeX, tapGive.closeY,
                                tapGive.closeChan);
                    if (tapGive.closeBox)
                        std::printf("items: --tapclosebox: the Stop uid %d is a touch box "
                                    "%.1fx%.1f, and shuts the window on the first tick the "
                                    "player's rect overlaps it\n", tapGive.closeUid,
                                    2.0 * tapGive.closeHw, 2.0 * tapGive.closeHh);
                    v.clear();
                    continue;
                }
                std::printf("items: tap %zu uid %d also counts presses -- only one"
                            " is held per call, so it gives once\n", b,
                            g_touch[b].uid);
            }
            for (const auto& p : v) itemBits[p.first] |= touchBit((int)b);
            if (!v.empty()) {
                std::printf("items: box %zu uid %d (x=%.0f) gives", b,
                            g_touch[b].uid, g_touch[b].cx);
                for (const auto& p : v)
                    std::printf(" item %d x%d", p.first, p.second);
                std::printf("\n");
            }
        }
    }
    if (tapGive.closeBox && t0 > 0 && g_touchEnteredGiven) {
        const auto et = g_touchEnteredT.find(tapGive.closeUid);
        if (et != g_touchEnteredT.end() && et->second > 0 && (long long)et->second <= t0) {
            init.taps = 0x40;
            std::printf("items: --tapclosebox: the Stop uid %d was entered at t=%d <= t0, so "
                        "the window starts shut\n", tapGive.closeUid, et->second);
        }
    }
    auto chainItems = [&itemBits, &bitItemGive](TouchMask trig, int item) -> int {
        const auto mi = itemBits.find(item);
        if (mi == itemBits.end() || !(trig & mi->second)) return 0;
        int add = 0;
        for (size_t b = 0; b < bitItemGive.size(); ++b) {
            if (!(trig & touchBit((int)b))) continue;
            for (const auto& p : bitItemGive[b])
                if (p.first == item) add += p.second;
        }
        return add;
    };
    TouchMask tapGateBits{};
    std::vector<TouchMask> coinEnable(L.coins.size(), TouchMask{});
    if (gatesOn)
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
            const TouchTrig& T = g_touch[b];
            if (T.count < 0 || T.cmode != 3) continue;
            if (tapGive.bit && T.item == tapGive.item) tapGateBits |= touchBit((int)b);
            if (!coinOn) continue;   // (the coins it switches on are the route's)
            for (const TrigCtl& c : T.ctl)
                for (size_t ci = 0; ci < L.coins.size(); ++ci)
                    if (L.coins[ci].uid == c.uid && !(coinEnable[ci] & touchBit((int)b))) {
                        coinEnable[ci] |= touchBit((int)b);
                        std::printf("coins: coin %zu is switched on by Item Compare uid %d"
                                    " (item %d at least %d)\n",
                                    ci, T.uid, T.item, T.count);
                    }
        }
    if (coinOn)
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
            if (g_touch[b].tap) continue;
            if (g_touch[b].count >= 0 && !(g_touch[b].armBy)) continue;
            for (const int u : g_touch[b].togOnUids)
                for (size_t ci = 0; ci < L.coins.size(); ++ci)
                    if (L.coins[ci].uid == u
                        && !(coinEnable[ci] & touchBit((int)b))) {
                        coinEnable[ci] |= touchBit((int)b);
                        std::printf("coins: coin %zu is switched on by the Toggle in"
                                    " touch box uid %d's chain\n", ci, g_touch[b].uid);
                    }
        }
    std::vector<uint8_t> coinAnyFrame(L.coins.size(), 0);
    for (size_t ci = 0; ci < L.coins.size(); ++ci) {
        if (!coinEnable[ci]) continue;
        bool moved = false;
        for (size_t b = 0; b < g_touch.size() && !moved; ++b)
            for (const TrigCtl& c : g_touch[b].ctl)
                if (c.uid == L.coins[ci].uid
                    && (c.dx != 0.f || c.dy != 0.f || c.durTicks != 0.0
                        || c.lockTicks != 0.0 || c.lockYTicks != 0.0 || c.tmode != 0)) {
                    moved = true;
                    break;
                }
        if (moved) continue;
        coinAnyFrame[ci] = 1;
        std::printf("coins: coin %zu never moves, so it is collected in any frame\n", ci);
    }
    bool haveCmpGate = false;
    for (const TouchTrig& T : g_touch)
        if (T.count >= 0 && T.cmode == 3) haveCmpGate = true;
    bool tapDisarmBox = false;
    if (g_tapDisarm && !gatesOn)
        for (const TouchTrig& T : g_touch)
            if (T.tap) tapDisarmBox = true;
    bool tapSole = tapGive.bit.any() && !itemBits.count(tapGive.item);
    for (const auto& col : g_collect)
        if (col.item == tapGive.item) tapSole = false;
    int chanDir[16];
    for (int ch = 0; ch < 16; ++ch) {
        chanDir[ch] = 4;
        for (const RotQEntry& e : g_rotQ)
            if (e.swarm && e.swch == ch && e.rotIdx >= 0) {
                chanDir[ch] = rotQDirection(g_rotTrig[(size_t)e.rotIdx].rawRot,
                                            g_rotTrig[(size_t)e.rotIdx].flipX != 0);
                break;
            }
    }
    const bool tapQueue = g_rotQueue && !g_rotQ.empty();
    auto reachedOn = [&chanDir](int ch, double wx, double wy, double px, double py) {
        switch (chanDir[ch & 15]) {
            case 1: return wy >= py;
            case 2: return wy <= py;
            case 3: return wx <= px;
            default: return wx >= px;
        }
    };
    uint8_t gatedMask = 0;
    std::vector<double> tapMissX(L.coins.size(), 0.0), tapMissY(L.coins.size(), 0.0);
    if (tapGive.bit)
        for (size_t ci = 0; ci < L.coins.size(); ++ci) {
            if (!(coinEnable[ci] & tapGateBits) || ((g_coinSkip >> ci) & 1)) continue;
            const Obj& c = L.coins[ci];
            const int d = chanDir[tapGive.closeChan & 15];
            // the coin has to be ahead of the shut point on that run
            if (!reachedOn(tapGive.closeChan, c.cx, c.cy, tapGive.closeX, tapGive.closeY))
                continue;
            gatedMask |= (uint8_t)(1u << ci);
            const double m = 15.0;   // the largest half any mode has, as the mod's miss
            tapMissX[ci] = d == 3 ? c.cx - c.hw - m : (d == 1 || d == 2) ? c.cx : c.cx + c.hw + m;
            tapMissY[ci] = d == 1 ? c.cy + c.hh + m : d == 2 ? c.cy - c.hh - m : c.cy;
        }
    if (tapGive.bit && tapSole)
        for (size_t ci = 0; ci < L.coins.size(); ++ci) {
            const TouchMask gates = coinEnable[ci] & tapGateBits;
            if (!gates || ((g_coinSkip >> ci) & 1)) continue;
            int need = 1 << 30;
            for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b)
                if (gates & touchBit((int)b)) need = std::min(need, g_touch[b].count);
            const bool miss = ((gatedMask >> ci) & 1) != 0;
            char e[224];
            std::snprintf(e, sizeof e, "%s%d,%d,%.3f,%.3f,%d,%d,%d,%d,%.3f,%.3f",
                          g_outcome.coinGates.empty() ? "" : ";", L.coins[ci].uid,
                          tapGive.closeChan, tapGive.closeX, tapGive.closeY,
                          chanDir[tapGive.closeChan & 15], tapGive.item, need,
                          miss ? 1 : 0, tapMissX[ci], tapMissY[ci]);
            g_outcome.coinGates += e;
        }
    auto passedOn = [&](const State& st, int frame, int ch, double wx, double wy,
                        double px, double py) {
        if (!tapQueue) return ch == 0 && frame == 0 && wx > px;
        if ((st.rotChan & 15) == (ch & 15) && reachedOn(ch, wx, wy, px, py)) return true;
        const int beg = g_rotQBeg[(size_t)(ch & 15)], end = g_rotQEnd[(size_t)(ch & 15)];
        for (int k = beg; k < end && k < 32; ++k)
            if (((st.rotSpent >> k) & 1u)
                && reachedOn(ch, g_rotQ[(size_t)k].px, g_rotQ[(size_t)k].py, px, py))
                return true;
        return false;
    };
    auto itemGates = [&](State& ks, uint8_t prevAction, int input, const StepCtx& K,
                         int gframe, double px, double py, double ph, long long t) {
        if ((gframe == 0
             && (!g_collect.empty() || !itemBits.empty() || tapGive.bit || tapDisarmBox))
            || (gframe != 0 && haveCmpGate)) {
            for (size_t ii = 0; gframe == 0 && ii < g_collect.size() && ii < 16; ++ii) {
                const uint16_t ibit = (uint16_t)(1u << ii);
                if (ks.items & ibit) continue;
                const Collectible& C = g_collect[ii];
                if (std::fabs(px - C.cx) <= C.hw + ph - kCoinMargin
                    && std::fabs(py - C.cy) <= C.hh + ph - kCoinMargin) {
                    if (g_coinDbgT >= 0 && !g_itemSaid[ii]) {
                        g_itemSaid[ii] = 1;
                        std::printf("itemcollect: t=%lld item=%d uid=%d"
                                    " player=(%.3f,%.3f) obj=(%.3f,%.3f)"
                                    " |dx|=%.3f |dy|=%.3f bound=(%.3f,%.3f)"
                                    " mode=%d mini=%d\n",
                                    (long long)t, C.item, C.uid, px, py,
                                    C.cx, C.cy, std::fabs(px - C.cx),
                                    std::fabs(py - C.cy),
                                    C.hw + ph - kCoinMargin,
                                    C.hh + ph - kCoinMargin,
                                    (int)ks.mode, (int)ks.mini);
                    }
                    ks.items |= ibit;
                }
            }
            if (K.trigs)
                for (const auto& tb : *K.trigs) {
                    const TouchTrig* T = tb.first;
                    if (T->count < 0 || (ks.trig & tb.second)) continue;
                    if (T->tap) continue;   // fired below, by the press
                    // --spawnroots: a Count a box spawns listens only once that box fired.
                    if (T->armBy && !(ks.trig & T->armBy)) continue;
                    if (gframe != 0 && T->cmode != 3) continue;
                    int n = 0;
                    for (const auto& ib : g_itemBase)
                        if (ib.first == T->item) n += ib.second;
                    const int nBase = n;   // before anything this run gave
                    for (size_t ii = 0; ii < g_collect.size() && ii < 16; ++ii)
                        if ((ks.items & (uint16_t)(1u << ii))
                            && g_collect[ii].item == T->item)
                            ++n;
                    n += chainItems(ks.trig, T->item);
                    // ...and the counting tap's presses (tapGive).
                    if (tapGive.bit && T->item == tapGive.item)
                        n += (int)(ks.taps & 0x3f) * tapGive.per;
                    const bool hit = T->cmode == 3 ? (n >= T->count)
                                   : T->cmode == 1 ? (n > T->count)
                                   : T->cmode == 2 ? (n < T->count)
                                   : (nBase <= T->count && T->count <= n);
                    if (g_coinDbgT >= 0 && T->count >= 0 && !T->tap) {
                        const int bi = touchBitIndex(tb.second);
                        const int key = std::min(n, 60) + 1;
                        if (bi >= 0 && bi < kTouchBits
                            && g_countSaid[bi].load() < key) {
                            g_countSaid[bi].store(key);
                            std::printf("countgate: t=%lld bit=%d uid=%d id=%d "
                                        "item=%d cmode=%d need=%d n=%d base=%d "
                                        "px=%.1f hit=%d\n",
                                        (long long)t, bi, T->uid, T->id,
                                        T->item, T->cmode, T->count, n, nBase,
                                        px, hit ? 1 : 0);
                        }
                    }
                    if (!hit) continue;
                    if (T->id == 1811 && px < T->cx) continue;
                    ks.trig |= tb.second;
                    ks.trigT = (int32_t)t;
                    const int b = touchBitIndex(tb.second);
                    if (g_touchFireT[b] < 0) g_touchFireT[b] = (int)t;
                    ks.fireB[b] = (uint16_t)t;
                }
        }
        if (K.trigs && input && !prevAction && gframe == 0)
            for (const auto& tb : *K.trigs) {
                const TouchTrig* T = tb.first;
                if (!T->tap || (ks.trig & tb.second)) continue;
                // --spawnroots: a Tap a box spawns is armed by that box, not by its x.
                if (T->armBy ? !(ks.trig & T->armBy) : (px <= T->cx)) continue;
                ks.trig |= tb.second;
                ks.trigT = (int32_t)t;
                const int b = touchBitIndex(tb.second);
                if (g_touchFireT[b] < 0) g_touchFireT[b] = (int)t;
                ks.fireB[b] = (uint16_t)t;
            }
        if (tapGive.bit) {
            if (ks.trig & tapGateBits) {
                ks.taps = 0;
            } else {
                double wx = 0.0, wy = 0.0;
                fromFrame(gframe, (double)ks.xAbs, (double)ks.y, wx, wy);
                uint8_t tp = ks.taps;
                const bool armed =
                    (tp & 0x80) != 0
                    || (tapGive.armBy ? (bool)(ks.trig & tapGive.armBy)
                                      : passedOn(ks, gframe, tapGive.armChan, wx, wy,
                                                 tapGive.armX, tapGive.armY));
                bool boxShut = false;
                if (tapGive.closeBox && armed && gframe == 0) {
                    const double half = playerHalf(ks.mode, ks.mini != 0);
                    const double preY = (!ks.dual && g_preBtnSet) ? g_preBtnY : wy;
                    boxShut = std::fabs(wx - tapGive.closeX) <= tapGive.closeHw + half
                              && (std::fabs(wy - tapGive.closeY) <= tapGive.closeHh + half
                                  || std::fabs(preY - tapGive.closeY) <= tapGive.closeHh + half);
                }
                const bool shut =
                    (tp & 0x40) != 0 || boxShut
                    || (!tapGive.closeBox && armed && tapGive.closeX < 1e17
                        && (tapGive.armBy ? (gframe == 0 && wx > tapGive.closeX)
                                          : passedOn(ks, gframe, tapGive.closeChan, wx, wy,
                                                     tapGive.closeX, tapGive.closeY)));
                if (shut) {
                    tp = 0x40;
                } else if (armed) {
                    tp |= 0x80;
                    if (input && !prevAction && (tp & 0x3f) < 0x3f) ++tp;
                }
                ks.taps = tp;
            }
        }
    };
    const auto revPast = [&](double cx) {
        for (const std::vector<Obj>* v : {&L.orbs, &L.pads, &L.dyn.objs})
            for (const Obj& o : *v)
                if (o.rev != 0 && o.cx > cx) return true;
        return false;
    };
    std::vector<uint8_t> coinPruneOk(L.coins.size(), 0);
    if (coinOn) {
        for (size_t ci = 0; ci < L.coins.size(); ++ci) {
            const double past = L.coins[ci].cx + L.coins[ci].hw + 40.0;
            if (revPast(L.coins[ci].cx)) {
                std::printf("coins: coin %zu at x=%.0f -- something past it turns "
                            "the player round, so passing it says nothing (no "
                            "miss prune)\n", ci, L.coins[ci].cx);
                // ...and the mod is told, so its own attempt cut agrees (Outcome).
                g_outcome.coinNoPrune += (g_outcome.coinNoPrune.empty() ? "" : ";")
                                         + std::to_string(L.coins[ci].uid);
                continue;
            }
            if (g_rotTrig.empty()) { coinPruneOk[ci] = 1; continue; }
            size_t idx = g_rotTrig.size();
            for (size_t j = 0; j < g_rotTrig.size(); ++j)
                if (g_rotTrig[j].cx > L.coins[ci].cx) { idx = j; break; }
            double minRe = 1e18;
            for (size_t j = idx; j < g_rotTrig.size(); ++j)
                if (g_rotTrig[j].frame == 0)
                    minRe = std::min(minRe, g_rotTrig[j].cx);
            coinPruneOk[ci] = (minRe > past) ? (uint8_t)1 : (uint8_t)0;
            if (!coinPruneOk[ci])
                g_outcome.coinNoPrune += (g_outcome.coinNoPrune.empty() ? "" : ";")
                                         + std::to_string(L.coins[ci].uid);
            if (!g_rotTrig.empty())
                std::printf("coins: coin %zu at x=%.0f -- the frame returns to 0"
                            " next at x=%.0f, %s\n", ci, L.coins[ci].cx,
                            minRe > 1e17 ? -1.0 : minRe,
                            coinPruneOk[ci] ? "so passing it is final"
                                            : "which is BEHIND it, so passing it "
                                              "says nothing (no miss prune)");
        }
    }
    // (struct CoinOnGate: search_dp.hpp)
    std::vector<CoinOnGate> coinGate;
    if (coinOn) {
        for (size_t ci = 0; ci < L.coins.size(); ++ci) {
            if (!coinPruneOk[ci]) continue;
            CoinOnGate g{(int)ci, {}};
            for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b) {
                if (g_touch[b].togOn != 1) continue;
                bool hits = false;
                for (const TrigCtl& c : g_touch[b].ctl)
                    if (c.uid == L.coins[ci].uid) { hits = true; break; }
                if (!hits) continue;
                g.ons.push_back({touchBit((int)b),
                                 g_touch[b].cx + g_touch[b].hw + 15.0});
            }
            if (g.ons.empty()) continue;
            coinGate.push_back(std::move(g));
            std::printf("coins: coin %zu is switched on by %zu box(es), the last"
                        " at x=%.0f\n", ci, coinGate.back().ons.size(),
                        coinGate.back().ons.back().second);
        }
    }
    if (coinOn && !g_rotTrig.empty()) {
        size_t n = 0;
        for (uint8_t ok : coinPruneOk) n += ok ? 1 : 0;
        std::printf("coins: the level turns the frame -- %zu of %zu coins can "
                    "still be called missed once passed\n", n, coinPruneOk.size());
    }
    g_yBound = std::max(700.0, L.maxY + 120.0);
    g_yBoundTurned = std::max(L.maxX, L.maxY) + 400.0;
    std::printf("  yBound=%.0f (level maxY=%.0f)\n", g_yBound, L.maxY);
    {   // suffix max of surface tops per 30 px bucket (see g_topAhead)
        double lo = 1e18, hi = -1e18;
        auto scan = [&](const std::vector<Obj>& v) {
            for (const Obj& o : v) {
                lo = std::min(lo, o.cx - o.hw);
                hi = std::max(hi, o.cx + o.hw);
            }
        };
        scan(L.objs); scan(L.pads); scan(L.orbs); scan(L.portals);
        if (lo < hi) {
            g_bucketX0 = lo;
            const size_t n = (size_t)((hi - lo) / 30.0) + 2;
            g_topAhead.assign(n, -1e18);
            auto put = [&](const std::vector<Obj>& v) {
                for (const Obj& o : v) {
                    long long i = (long long)((o.cx - lo) / 30.0);
                    if (i < 0) i = 0;
                    if (i >= (long long)n) i = (long long)n - 1;
                    g_topAhead[(size_t)i] =
                        std::max(g_topAhead[(size_t)i], o.cy + o.hh);
                }
            };
            put(L.objs); put(L.pads); put(L.orbs); put(L.portals);
            for (size_t i = n - 1; i-- > 0;)
                g_topAhead[i] = std::max(g_topAhead[i], g_topAhead[i + 1]);
            if (g_escDyn && !L.dyn.objs.empty()) {
                g_topAheadDyn.assign(n, -1e18);
                auto upOf = [](const std::vector<Dynamics::AutoPart>& v) {
                    double s = 0.0;
                    for (const Dynamics::AutoPart& p : v) s += std::max(0.0, (double)p.dy);
                    return s;
                };
                for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                    const Obj& o = L.dyn.objs[i];
                    double up = 0.0, s1 = 0.0, s2 = 0.0;
                    if (i < L.dyn.autoParts.size()) s1 += upOf(L.dyn.autoParts[i]);
                    if (i < L.dyn.touchParts.size()) s1 += upOf(L.dyn.touchParts[i]);
                    if (i < L.dyn.partsAny.size()) s2 = upOf(L.dyn.partsAny[i]);
                    up = std::max(s1, s2);
                    double s3 = 0.0;
                    if (i < L.dyn.autoDy.size()) s3 += std::max(0.0, (double)L.dyn.autoDy[i]);
                    if (i < L.dyn.trigDy.size()) s3 += std::max(0.0, (double)L.dyn.trigDy[i]);
                    up = std::max(up, s3);
                    double top = o.cy + o.hh + up, cxMin = o.cx;
                    if (i < L.dyn.baseCy.size())
                        top = std::max(top, (double)L.dyn.baseCy[i] + o.hh + up);
                    if (i < L.dyn.samples.size())
                        for (const DynSample& sm : L.dyn.samples[i]) {
                            top = std::max(top, (double)sm.cy + (double)sm.hh);
                            cxMin = std::min(cxMin, (double)sm.cx);
                        }
                    long long b = (long long)((cxMin - lo) / 30.0);
                    if (b < 0) b = 0;
                    if (b >= (long long)n) b = (long long)n - 1;
                    g_topAheadDyn[(size_t)b] = std::max(g_topAheadDyn[(size_t)b], top);
                }
                for (size_t i = n - 1; i-- > 0;)
                    g_topAheadDyn[i] = std::max(g_topAheadDyn[i], g_topAheadDyn[i + 1]);
                std::printf("escdyn: %zu moving objects on the escapee table\n", L.dyn.objs.size());
            }
        }
    }
    long long tEnd = (long long)std::ceil(std::max(goalX, L.maxX + 60.0) / kDx) + 2;
    if (t0 > 0) tEnd = std::max(tEnd, t0 + 8000);

    const gdapprox::ShipParams SP = gdapprox::ShipParams::normal();
    const gdapprox::ShipParams SPmini = gdapprox::ShipParams::mini();
    const gdapprox::UfoParams UP = gdapprox::UfoParams::normal();
    const gdapprox::UfoParams UPmini = gdapprox::UfoParams::mini();

    const bool checkWanted = g_check.enabled.load(std::memory_order_relaxed);
    struct CheckGuard {
        bool on;
        ~CheckGuard() { if (on) g_check.reset(); }
    } checkGuard{checkWanted};
    if (checkWanted) g_check.reset();

    ModelWorld world{L, &SP, &SPmini, &UP, &UPmini, coinOn, &coinEnable, &coinAnyFrame, {}, {}};
    if (gatesOn || tapDisarmBox) {
        world.gatesCtx = (void*)&itemGates;
        world.gates = [](void* ctx, State& ks, uint8_t prevAction, int input, const StepCtx& K,
                         int gframe, double px, double py, double ph, long long t) {
            (*static_cast<decltype(itemGates)*>(ctx))(ks, prevAction, input, K, gframe, px, py, ph, t);
        };
    }
    world.tapBit = tapGive.bit;
    world.tapGateBits = tapGateBits;
    // ...and the model as a search sees it (model.hpp Model): the world and its plugs.
    const Model model{world};
    const size_t kParallelMin = 256;
    std::unique_ptr<ThreadPool> pool;
    if (g_threads > 1) pool.reset(new ThreadPool(g_threads - 1));  // + this one
    struct DynParLoan {
        ~DynParLoan() { g_dynParTasks = nullptr; }
    } dynParLoan;
    if (pool)
        g_dynParTasks = [&pool](size_t n, const std::function<void(size_t)>& task) {
            pool->parallelTasks(n, task);
        };
    long long bestT = 0;
    double bestX = 0;
    State goalState{};
    bool solved = false;
    size_t maxAlive = 0;
    long long capHits = 0, capDropped = 0;
    long long cellSpared = 0, cellCuts = 0, cellTwinsKept = 0;
    SearchCensus census;   // --searchcensus only
    TwinStats twin;        // --twinskip / --twinaudit
    {
        FlyBand seed;
        double seedRefY = 0.0;
        bool seedDual = false;
        int seedMode = (int)init.mode;
        for (const Obj& p : L.portals) {
            if (p.cx > x0) break;
            if (p.type == 23 || p.type == 24) {
                seedDual = (p.type == 23);
                if (seedDual) {
                    seedRefY = p.cy;
                    seed = bandFor(seedRefY, bandHeightDual(seedMode));
                }
                continue;
            }
            const double modeH = bandHeightFor(p.type);
            const bool isModeP = modeH > 0.0 || p.type == 6 || p.type == 27;
            if (isModeP) {
                seedMode = (p.type == 5)    ? 1
                           : (p.type == 16) ? 2
                           : (p.type == 19) ? 3
                           : (p.type == 26) ? 4
                                            : 0;
            }
            if (seedDual && isModeP) {
                seed = bandFor(seedRefY, bandHeightDual(seedMode));
            } else if (modeH > 0.0) {
                seedRefY = p.cy;
                seed = bandFor(p.cy, modeH);
            }
        }
        init.bandRefY = (float)seedRefY;
        if (startBandUsable()) {
            const bool inside = (double)init.y >= g_startBandFloor - 1.0
                                && (double)init.y <= g_startBandCeil + 1.0;
            if (inside) {
                seed.floorY = g_startBandFloor;
                seed.ceilY = g_startBandCeil;
            } else {
                std::printf("startband: anchor y=%.2f is OUTSIDE [%.1f, %.1f]"
                            " - ignoring it (stale pmin/pmax)\n",
                            (double)init.y, g_startBandFloor, g_startBandCeil);
                seed.floorY = kGroundY;
                seed.ceilY = 1e9;
            }
        }
        if (g_shipCeilSet) seed.ceilY = g_shipCeil + kCubeHalf;
        if (g_ufoCeil > 0.0) seed.ceilY = g_ufoCeil;
        if (g_flyFloor > 0.0) seed.floorY = g_flyFloor;
        init.bandFloor = (float)seed.floorY;
        init.bandCeil = (float)seed.ceilY;
        if (g_bandOwnTrack && startBandUsable() && !g_bandTrack.empty() && !bandTrackIsCamera()
            && seed.floorY == g_startBandFloor && seed.ceilY == g_startBandCeil) {
            double fl = 0.0, ce = 0.0;
            if (bandTrackRowAt(t0, fl, ce)
                && (std::fabs(fl - g_startBandFloor) > 1.0 || std::fabs(ce - g_startBandCeil) > 1.0)) {
                init.bandBranch |= kBandOwn;
                std::printf("bandowntrack: the anchor's band [%.1f, %.1f] is not the recording's "
                            "[%.1f, %.1f] at t=%lld - its own from the start\n",
                            g_startBandFloor, g_startBandCeil, fl, ce, t0);
            }
        }
        if (startBandUsable())
            std::printf("startband: the seed will start in [%.1f, %.1f]\n",
                        seed.floorY, seed.ceilY);
        if (g_dualBand && init.dual) {
            double lo = -1e18, hi = 1e18;
            int used = 0;
            auto take = [&](double fl, double ce) -> bool {
                const double H = ce - fl;
                if (std::fabs(H - kBandFly) > 0.01 && std::fabs(H - kBandOther) > 0.01)
                    return true;   // not a dual height: no information, keep walking
                if (fl <= 90.0 + 0.01) {
                    const double b = 90.0 + H * 0.5 + 30.0;
                    if (b <= lo) return false;
                    hi = std::min(hi, b);
                    ++used;
                    return true;
                }
                if (std::fabs(fl - std::round(fl / 30.0) * 30.0) > 0.01) return true;
                const double a = fl + H * 0.5, b = a + 30.0;
                if (b <= lo || a >= hi) return false;
                lo = std::max(lo, a);
                hi = std::min(hi, b);
                ++used;
                return true;
            };
            bool open = true;
            if (startBandUsable()) open = take(g_startBandFloor, g_startBandCeil);
            for (size_t k = g_bandTrackIntervals.size(); open && k-- > 0;) {
                const BandTrackRow& r = g_bandTrack[g_bandTrackIntervals[k]];
                if ((long long)r.t > t0) continue;
                open = take((double)r.fl, (double)r.ce);
            }
            if (used > 0 && lo > -1e17 && hi < 1e17) {
                double pick = 0.0;
                const char* how = nullptr;
                if (seedRefY >= lo && seedRefY < hi) {
                    pick = seedRefY;
                    how = "the portal replay";
                } else {
                    double bestX = -1e18;
                    auto consider = [&](const Obj& p) {
                        if (p.type != 23 || p.cx > x0 || p.cx <= bestX) return;
                        if (p.cy >= lo && p.cy < hi) {
                            bestX = p.cx;
                            pick = p.cy;
                            how = "a dual portal";
                        }
                    };
                    for (const Obj& p : L.portals) consider(p);
                    for (size_t k = 0; k < L.dyn.objs.size() && k < L.dyn.bucket.size(); ++k)
                        if (L.dyn.bucket[k] == Dynamics::PORT) consider(L.dyn.objs[k]);
                    if (!how) {
                        pick = (lo + hi) * 0.5;
                        how = "the rows' midpoint";
                    }
                }
                init.bandRefY = (float)pick;
                std::printf("dualband: the anchor is inside a dual - reference y %.1f (%s) from %d"
                            " band row(s), in [%.1f, %.1f) (the portal replay gave %.1f)\n",
                            (double)init.bandRefY, how, used, lo, hi, seedRefY);
            } else {
                std::printf("dualband: the anchor is inside a dual but no band row bounds the"
                            " reference y - left at %.1f\n", seedRefY);
            }
        }
    }
    size_t ceilIdx = 0;

    float curDxF = kDxF;
    size_t spdIdx = 0;
    auto speedGate = [&](const Obj& o) { return o.cx - o.hw - kCubeHalf; };
    while (spdIdx < L.speeds.size() && speedGate(L.speeds[spdIdx]) <= x0) {
        curDxF = dxForSpeedId(L.speeds[spdIdx].id);
        ++spdIdx;
    }
    if (g_startSpeedMul > 0.0) {
        const float measured = dxForSpeedMul(g_startSpeedMul);
        if (measured != curDxF)
            std::printf("start speed: replay-by-x says %.5f, GD says %.5f "
                        "(mul %.3f) - using GD\n",
                        (double)curDxF, (double)measured, g_startSpeedMul);
        curDxF = measured;
    }
    init.dx = curDxF;

    prepMark(4);
    if (!replayPath.empty()) {
        struct Edge { long long press; int v; };
        std::vector<Edge> edges;
        {
            std::ifstream pf(replayPath);
            if (!pf) {
                std::printf("REPLAY: cannot open %s\n", replayPath.c_str());
                return 2;
            }
            std::string ln;
            while (std::getline(pf, ln)) {
                if (ln.size() >= 3 && (unsigned char)ln[0] == 0xEF &&
                    (unsigned char)ln[1] == 0xBB && (unsigned char)ln[2] == 0xBF)
                    ln.erase(0, 3);
                long long p; int v;
                if (std::sscanf(ln.c_str(), "input=%lld,%d", &p, &v) == 2)
                    edges.push_back(Edge{p, v});
            }
        }
        std::stable_sort(edges.begin(), edges.end(),
                         [](const Edge& a, const Edge& b) { return a.press < b.press; });
        if (!g_ctrlWin.empty()) {
            const size_t nOrig = edges.size();
            for (const auto& w : g_ctrlWin) {
                int heldBefore = 0, heldAfter = 0;
                for (size_t k = 0; k < nOrig; ++k) {
                    if (edges[k].press < w.second) heldBefore = edges[k].v;
                    if (edges[k].press <= w.second) heldAfter = edges[k].v;
                    else break;
                }
                if (heldBefore) {
                    edges.push_back(Edge{w.second, 1});
                    if (!heldAfter) {
                        edges.push_back(Edge{w.second + 1, 0});
                        g_winRePushJump.push_back(w.second + 1);
                    }
                    std::printf("REPLAY: ctrl re-push at t=%lld (held into "
                                "window end)\n", w.second);
                }
            }
            std::stable_sort(edges.begin(), edges.end(),
                             [](const Edge& a, const Edge& b) { return a.press < b.press; });
        }
        std::printf("REPLAY: %zu edges from %s\n", edges.size(), replayPath.c_str());
        g_hist = HistStat{};   // --histstat: this replay's counts only
        g_histFired.clear();
        g_histPads.clear();
        auto latOf = [](uint8_t m) { return (m == 1 || m == 3) ? 2 : 1; };
        size_t eIdx = 0;
        std::vector<std::pair<long long, int>> fx;  // (effect tick, level)
        std::set<long long> refreshEff, tapEff;
        int pressLevel = 0;
        auto scheduleTick = [&](size_t from, size_t to, long long eff, uint8_t mode,
                                std::vector<std::pair<long long, int>>& out) {
            const int before = pressLevel;
            bool fresh = false;
            int lvl = before;
            for (size_t k = from; k < to; ++k) {
                if (edges[k].v != 0 && lvl == 0) fresh = true;
                lvl = edges[k].v;
            }
            pressLevel = lvl;
            if (!g_sameTick || to - from < 2 || !fresh) {
                for (size_t k = from; k < to; ++k) out.push_back({eff, edges[k].v});
                return;
            }
            if (lvl == 0) {
                out.push_back({eff, 1});
                out.push_back({eff + 1, 0});
                tapEff.insert(eff);
                if (before != 0) refreshEff.insert(eff);
            } else {
                out.push_back({eff, lvl});
                if (lvl != 0 && before != 0) refreshEff.insert(eff);
            }
        };
        int preLevel = 0;
        bool preRise = false;   // the rising edge landed exactly ON t0
        while (eIdx < edges.size() && edges[eIdx].press <= t0) {
            const long long eff = edges[eIdx].press + latOf(init.mode);
            size_t to = eIdx + 1;
            while (to < edges.size() && edges[to].press == edges[eIdx].press) ++to;
            std::vector<std::pair<long long, int>> tick;
            scheduleTick(eIdx, to, eff, init.mode, tick);
            for (const auto& e : tick) {
                if (e.first <= t0) {
                    preRise = (e.first == t0) && e.second != 0 && preLevel == 0;
                    preLevel = e.second;
                } else {
                    fx.push_back(e);
                }
            }
            eIdx = to;
        }
        init.action = (uint8_t)preLevel;
        if (init.pressSpent && (init.mode == 3 || init.mode == 7))
            init.action = 1;
        if (init.mode == 7 && preRise) init.rHover = 1;
        std::ofstream tr(outPath + ".trace.csv");
        tr.precision(10);
        tr << "tick,x,y,vy,mode,grounded,dual,y2,vy2,flip2,act"
              ",onslope,slopem,slopet,bandf,bandc,mini,held,dx,nearorb,clamp"
              ",clampuid,clampcx,clampcy,flip,frame,rot,rotneg"
              ",grounded2,onslope2,slopem2,slopet2,mode2,ceilt,ceilt2,mini2"
              ",fxblk"
           << (g_traceState ? ",svy,sflip" : "")
           // --p2xoff: the second body's x offset (State::dx2), under the flag only
           << (g_p2xOff ? ",dx2\n" : "\n");
        std::ofstream sn;
        if (!snapLogPath.empty()) {
            sn.open(snapLogPath);
            g_snapOut = &sn;
        }
        State s = init;
        bool sawSeedTick = false;
        FrameSlices rfs;
        bindFrameSlices(rfs, frameLevel(L, (int)init.frame));
        int pendingFrame = -1;
        const float rDxF = curDxF;
        int curIn = preLevel;   // (see the pre-anchor edge split above)
        size_t fxIdx = 0;
        long long diedT = -1;
        double diedX = 0;
        long long lastT = t0;
        for (long long t = t0 + 1; t <= tEnd; ++t) {
            lastT = t;
            while (fxIdx < fx.size() && fx[fxIdx].first <= t)
                curIn = fx[fxIdx++].second;
            const float rDxUsed = ((s.dx > 0.f) ? s.dx : rDxF)
                                  * (s.rev ? -1.f : 1.f);
            const double xPrevR = (double)s.xAbs;
            const double x = (double)advanceX(s.xAbs, rDxUsed);
            if (g_dynDbg == -2 && t == t0 + 1)
                std::printf("revdbg init.rev=%d (at replay start)\n", (int)init.rev);
            if (g_dynDbg == -2 && t <= t0 + 3)
                std::printf("revdbg t=%lld s.rev=%d dx=%.4f xPrev=%.3f x=%.3f\n",
                            t, (int)s.rev, rDxUsed, xPrevR, x);
            for (AutoTrig& A : g_autoTrig) {
                if (A.fireT >= 0) continue;
                if (x < A.cx) break;   // sorted by cx
                A.fireT = (int)t + A.delay;
                A.fireX = A.lockLastX = x;
                std::printf("autotrig: uid %d x=%.0f crossed at t=%lld, "
                            "effect t=%d\n", A.uid, A.cx, t, A.fireT);
            }
            {
                RotTrig* rBest = nullptr;
                bool bestSame = false;
                double bestDvR = 1e18;
                for (RotTrig& r : g_rotTrig) {
                    const bool same = (r.setRev < 0)
                        ? (r.frame == (int)s.frame)
                        : (r.frame == (int)s.frame
                           && r.setRev == (int)s.rev);
                    if ((same ? r.revT : r.firedT) >= 0) continue;  // one shot
                    const double ru = frameU((int)s.frame, r.cx, r.cy);
                    const bool cross = s.rev ? (xPrevR > ru && x <= ru)
                                             : (xPrevR < ru && x >= ru);
                    const double dvR =
                        std::fabs((double)s.y
                                  - frameV((int)s.frame, r.cx, r.cy));
                    if (!cross || dvR > kRotPerpWin) continue;
                    if (dvR < bestDvR) {
                        bestDvR = dvR; rBest = &r; bestSame = same;
                    }
                }
                if (rBest) {
                    pendingFrame = rBest->frame;
                    if (!bestSame) rBest->firedT = (int)t;
                    else rBest->revT = (int)t;
                }
            }
            uint16_t fbForced[kTouchBits];
            const uint16_t* fbUse = s.fireB;
            if (g_fbForceBox >= 0 && g_fbForceBox < kTouchBits && s.trig.test(g_fbForceBox)) {
                std::copy(s.fireB, s.fireB + kTouchBits, fbForced);
                fbForced[g_fbForceBox] = (uint16_t)g_fbForceTick;
                fbUse = fbForced;
            }
            GroupEnv env;
            double pLo = x, pHi = x;
            p2Widen(pLo, pHi, p2Dx(s), p2Dx(s));   // --p2xoff
            prepareGroup(world, rfs, env, (int)s.frame, s.trig, (int)s.trigT, fbUse,
                         s.lockOff + (float)(x - xPrevR), x, xPrevR, rDxUsed, pLo, pHi, t,
                         GroupOpts{false, false, false});
            const std::vector<std::pair<const TouchTrig*, TouchMask>>& rt = env.trigs;
            if (g_slopeDbg && s.frame != 0) {
                std::printf("trigcand t=%lld frame=%d x=%.3f y=%.3f "
                            "boxes=%zu cand=%zu:",
                            (long long)t, (int)s.frame, x, (double)s.y,
                            touchFor((int)s.frame).size(), rt.size());
                for (const auto& tb : rt)
                    std::printf(" (%.1f,%.1f %.0fx%.0f du=%.2f dv=%.2f)",
                                tb.first->cx, tb.first->cy,
                                2 * tb.first->hw, 2 * tb.first->hh,
                                std::fabs(x - tb.first->cx),
                                std::fabs((double)s.y - tb.first->cy));
                std::printf("\n");
            }
            const StepCtx K = env.ctx();
            bool rdead = false;
            g_nearOrb = 0;
            g_dashVySet = 0;
            g_clampWhy = "";
            g_clampUid = -1;
            g_clampCx = g_clampCy = 0.f;
            g_deadWhy = "";
            g_deadObj = nullptr;
            g_deadCx = g_deadCy = 0.f;
            const bool sPrevGrounded = (s.grounded != 0);   // applyRotation's re-tap
            const double sPrevY = (double)s.y;              // ...and its pre-tap y
            const int wgFrame = (int)s.frame;               // --walkgates (below)
            // --sametick: a release then a press on one tick while held -- the step sees a fresh press
            if (g_sameTick && curIn != 0 && refreshEff.count(t)) s.action = 0;
            if (g_sameTick && curIn != 0 && tapEff.count(t)) {
                bool deadTry = false;
                const State tryC = stepBoth(s, 1, K, deadTry);
                const bool ring = (tryC.ringHold && !s.ringHold)
                                  || (tryC.usedOrb != nullptr && tryC.usedOrb != s.usedOrb);
                const bool groundJump = s.grounded && sameTickTapMode(s.mode)
                                        && ((tryC.pressSpent && !s.pressSpent)
                                            || (s.mode == 2 && tryC.flip != s.flip));
                if (!ring && !groundJump) curIn = 0;
                // the trial's diagnostics are not this tick's
                g_nearOrb = 0;
                g_dashVySet = 0;
                g_clampWhy = "";
                g_clampUid = -1;
                g_clampCx = g_clampCy = 0.f;
                g_deadWhy = "";
                g_deadObj = nullptr;
                g_deadCx = g_deadCy = 0.f;
            }
            const uint8_t wgPrev = s.action;
            State c = stepBoth(s, (uint8_t)curIn, K, rdead);
            c.action = (uint8_t)curIn;
            s = c;
            if ((g_seedDump >= 0 && t == (long long)g_seedDump)
                || (g_seedEvery > 0 && t % (long long)g_seedEvery == 0)) {
                std::printf("seed: t=%lld sizeof=%zu trig=0x%llx trigT=%d "
                            "lockOff=%.4f rotSpent=0x%x rotChan=%d "
                            "rotRev=0x%x rotStep=%.6f rotNeg=%d rotBoost=%d "
                            "ringHold=%d pressSpent=%d portalLatch=%s/%s "
                            "groundUid=%d usedOrbOld=%d usedOrbHist=%d|%d|%d portSeen=%d|%d|%d "
                            "touchRing=%d|%d|%d|%d/%x taps=%d fireB=",
                            t, sizeof(State), (unsigned long long)s.trig.word(0),
                            (int)s.trigT,
                            (double)s.lockOff, s.rotSpent, (int)s.rotChan,
                            (unsigned)s.rotRev, (double)s.rotStep,
                            (int)s.rotNeg, (int)s.rotBoost, (int)s.ringHold,
                            (int)s.pressSpent, gravLatchHex(s.portalLatch).c_str(),
                            gravLatchHex(s.portalLatch2).c_str(),
                            (int)s.groundUid, (int)s.usedOrbOld, (int)s.usedOrbHist[0],
                            (int)s.usedOrbHist[1], (int)s.usedOrbHist[2], (int)s.portSeen[0],
                            (int)s.portSeen[1], (int)s.portSeen[2], (int)s.touchRing[0],
                            (int)s.touchRing[1], (int)s.touchRing[2], (int)s.touchRing[3],
                            (unsigned)s.touchRingT, (int)s.taps);
                for (int b = 0; b < kTouchBits; ++b)
                    if (s.fireB[b]) std::printf("%d:%u,", b, s.fireB[b]);
                std::printf("\n");
                std::printf("seed: t=%lld spdFired=%d|%d spdFired2=%d|%d\n", t,
                                (int)s.spdFired[0], (int)s.spdFired[1],
                                (int)s.spdFired2[0], (int)s.spdFired2[1]);
                // --spdsameonce: the memory's two more places (State::spdMore)
                if (g_spdSameOnce)
                    std::printf("seed: t=%lld spdMore=%d|%d spdMore2=%d|%d\n", t, (int)s.spdMore[0],
                                (int)s.spdMore[1], (int)s.spdMore2[0], (int)s.spdMore2[1]);
                std::printf("seed: t=%lld a1cLatch=%d\n", t, (int)s.a1cLatch);
                // +0x728 (State::ball728, --ballscaleair): printed, not seeded, for the same reason
                if (g_ballScaleAir) std::printf("seed: t=%lld ball728=%d\n", t, (int)s.ball728);
                // --p2xoff (State::dx2): seeded at an anchor from --startp2dx
                if (g_p2xOff) std::printf("seed: t=%lld dx2=%.4f\n", t, (double)s.dx2);
                // --followy (State::followF): seeded from the recording at an anchor
                if (!g_followYs.empty())
                    std::printf("seed: t=%lld followOk=%d followF=%.3f|%.3f\n", t,
                                (int)s.followOk, (double)s.followF[0], (double)s.followF[1]);
                if (!g_rotQ.empty()) {
                    char head[64];
                    std::snprintf(head, sizeof head, "%d,%04x",
                                  (int)s.rotChan, (unsigned)s.rotRev);
                    std::string arg(head);
                    for (size_t q = 0; q < g_rotQ.size() && q < 32; ++q)
                        if ((s.rotSpent >> q) & 1u)
                            arg += "," + std::to_string(g_rotQ[q].uid);
                    std::printf("seedrotq: t=%lld --startrotq %s\n",
                                t, arg.c_str());
                    g_outcome.seedRotQ = arg;
                }
                sawSeedTick = true;
            }
            if (pendingFrame >= 0) {
                double wx0, wy0;
                fromFrame((int)s.frame, (double)s.xAbs, (double)s.y, wx0, wy0);
                const int rev0dbg = (int)s.rev;
                const int f0dbg = (int)s.frame;
                const int nf = applyRotation(s, xPrevR, (double)rDxUsed, t,
                                             curIn, sPrevGrounded,
                                             sPrevY, true);
                if ((nf >= 0 && nf != f0dbg) || (int)s.rev != rev0dbg) {
                    if (nf >= 0 && nf != f0dbg)
                        bindFrameSlices(rfs, frameLevel(L, nf));
                    std::printf("rotation: frame -> %d at t=%lld "
                                "world=(%.1f,%.1f) u=%.3f v=%.3f vy=%.3f "
                                "f0=%d rev0=%d -> rev=%d flip=%d\n",
                                (nf >= 0 ? nf : f0dbg), t, wx0, wy0,
                                (double)s.xAbs, (double)s.y,
                                (double)s.vy, f0dbg, rev0dbg,
                                (int)s.rev, (int)s.flip);
                }
                pendingFrame = -1;
            }
            model.fireGates(s, wgPrev, curIn, K, wgFrame, t, rdead);
            double wX, wY;
            fromFrame((int)s.frame, (double)s.xAbs, (double)s.y, wX, wY);
            double rowVy = (double)s.vy;
            int rowFlip = (int)s.flip;
            if ((s.mode == 1 || s.mode == 3) && !s.dual && !s.jumpBuf
                && !s.pressSpent) {
                int nextIn = curIn;
                for (size_t j = fxIdx; j < fx.size() && fx[j].first <= t + 1; ++j)
                    nextIn = fx[j].second;
                if (!g_ctrlWin.empty() && ctrlInputOffAt(t + 1, curIn != 0)) nextIn = 0;
                const float ringDx = (s.dx > 0.f) ? s.dx : K.dxF;
                const Obj* ring = nextIn
                    ? flyEarlyRing(s, K, (double)ringDx * timeWarpAt((double)s.xAbs)
                                             * (s.rev ? -1.0 : 1.0))
                    : nullptr;
                if (ring) {
                    State d = s;
                    const bool hs = g_histStatOn;   // a look ahead for the row, not a fire
                    g_histStatOn = false;
                    applyFlyEarlyRing(d, ring, ringDx);
                    g_histStatOn = hs;
                    rowVy = (double)d.vy;
                    rowFlip = (int)d.flip;
                }
            }
            const double vyGd = (g_dashVySet ? g_dashVy : rowVy)
                                * (s.frame == 3 ? -1.0 : 1.0);
            if ((g_vyWatchT >= 0 && t == g_vyWatchT)
                || (g_vyWatchT2 >= 0 && t == g_vyWatchT2)) {
                char vpt[96] = "-";
                const int nvp = g_vpWrites < 8 ? g_vpWrites : 8;
                for (int k = 0, o = 0; k < nvp; ++k)
                    o += std::snprintf(vpt + o, sizeof vpt - o, k ? ",%d" : "%d",
                                       g_vpTrail[(g_vpWrites - nvp + k) & 7]);
                char impt[96] = "-";
                const int nimp = g_impCount < 8 ? g_impCount : 8;
                for (int k = 0, o = 0; k < nimp; ++k)
                    o += std::snprintf(impt + o, sizeof impt - o, k ? ",%d" : "%d",
                                       g_impTrail[(g_impCount - nimp + k) & 7]);
                std::printf("vywriter: t=%lld writes=%d lastline=%d ywrites=%d ylastline=%d vyIn=%.6f vy=%.6f yIn=%.4f y=%.4f seatgate=%d impulsedOff=%d tappedOff=%d sOnSlope=%d took=%d impsite=%d "
                            "impcount=%d imptrail=%s vpwrites=%d vptrail=%s traceVy=%.6f frame=%d mode=%d\n",
                            (long long)t, g_vyWrites, g_vyWriter, g_yWrites, g_yWriter, (double)g_vyIn, (double)s.vy, (double)g_yIn, (double)s.y, g_seatGateSeen, g_seatImpulsedOff, g_seatTappedOff, g_seatOnSlope, g_seatTook, g_impulseSite,
                            g_impCount, impt, g_vpWrites, vpt, vyGd, (int)s.frame, (int)s.mode);
            }
            if (g_fxWatchT >= 0 && t == g_fxWatchT)
                std::printf("fxwhy: t=%lld %s\n", (long long)t, g_fxWhy);
            tr << t << ',' << wX << ',' << wY << ',' << vyGd << ','
               << (int)s.mode << ',' << (int)s.grounded << ',' << (int)s.dual
               << ',' << s.y2 << ',' << s.vy2 << ',' << (int)s.flip2 << ','
               << curIn
               << ',' << (int)s.onSlope << ',' << s.slopeM << ','
               << (int)s.slopeT << ',' << s.bandFloor << ',' << s.bandCeil
               << ',' << (int)s.mini << ',' << (int)s.held << ',' << s.dx
               << ',' << (int)g_nearOrb
               << ',' << (*g_clampWhy ? g_clampWhy : "-")
               << ',' << g_clampUid << ',' << g_clampCx << ',' << g_clampCy
               << ',' << rowFlip << ',' << (int)s.frame
               << ',' << s.rot << ',' << (int)s.rotNeg
               << ',' << (int)s.grounded2 << ',' << (int)s.onSlope2
               << ',' << s.slopeM2 << ',' << (int)s.slopeT2
               << ',' << (int)s.mode2 << ',' << (int)s.ceilT
               << ',' << (int)s.ceilT2 << ',' << (int)s.mini2
               << ',' << g_fxBlockedUid;
            if (g_traceState)
                tr << ',' << (double)s.vy * (s.frame == 3 ? -1.0 : 1.0) << ',' << (int)s.flip;
            if (g_p2xOff) tr << ',' << s.dx2;
            tr << "\n";
            if (rdead) {
                if (g_replayOn) {
                    if (diedT < 0) { diedT = t; diedX = (double)s.xAbs; }
                } else {
                    diedT = t;
                    diedX = (double)s.xAbs;
                    break;
                }
            }
            if ((double)s.xAbs >= goalX) break;
            if (eIdx < edges.size() && edges[eIdx].press == t) {
                size_t to = eIdx + 1;
                while (to < edges.size() && edges[to].press == t) ++to;
                scheduleTick(eIdx, to, t + latOf(s.mode), s.mode, fx);
                eIdx = to;
                // a tap's release lands a tick after its press: keep the effects in tick order
                if (g_sameTick)
                    std::stable_sort(fx.begin() + (long long)fxIdx, fx.end(),
                                     [](const auto& a, const auto& b) { return a.first < b.first; });
            }
        }
        g_outcome.replayDiedT = diedT;
        if (g_histStatOn) {
            if (FILE* hf = std::fopen(g_histStatPath.c_str(), "a")) {
                std::fprintf(hf,
                             "histstat out=%s x=%.1f died=%lld touchMax=%d touchDrop=%d "
                             "portMax=%d portFull=%d padMax=%d padFull=%d orbFired=%d "
                             "orbDrop=%d orbRefire=%d padRefire=%d\n",
                             outPath.c_str(), (double)s.xAbs, diedT, g_hist.touchMax,
                             g_hist.touchDrop, g_hist.portMax, g_hist.portFull, g_hist.padMax,
                             g_hist.padFull, g_hist.orbFired, g_hist.orbDrop, g_hist.orbRefire,
                             g_hist.padRefire);
                std::fclose(hf);
            }
        }
        if (diedT >= 0) {
            std::printf("REPLAY: model DIED at t=%lld x=%.1f (y=%.2f vy=%.3f "
                        "mode=%d grounded=%d dual=%d y2=%.2f vy2=%.3f held=%d)\n",
                        diedT, diedX, (double)s.y, (double)s.vy, (int)s.mode,
                        (int)s.grounded, (int)s.dual, (double)s.y2,
                        (double)s.vy2, (int)curIn);
            if (*g_deadWhy) {
                std::printf("REPLAY: cause=%s", g_deadWhy);
                if (g_deadObj)
                    std::printf(" obj=uid%d id%d %gx%g @%.2f,%.2f (now %.2f,%.2f)",
                                g_deadObj->uid, g_deadObj->id,
                                2 * g_deadObj->hw, 2 * g_deadObj->hh,
                                (double)g_deadCx, (double)g_deadCy,
                                g_deadObj->cx, g_deadObj->cy);
                if (g_deadObj) {
                    const char* home = "?";
                    auto in = [&](const std::vector<Obj>& v) {
                        return !v.empty() && g_deadObj >= v.data()
                               && g_deadObj < v.data() + v.size();
                    };
                    if (in(L.objs)) home = "L.objs";
                    else if (in(L.dyn.objs)) home = "L.dyn";
                    else if (in(L.slopes)) home = "L.slopes";
                    else for (int fi = 1; fi < 4; ++fi)
                        if (g_frameLv[fi]) {
                            if (in(g_frameLv[fi]->objs)) { home = "frame.objs"; break; }
                            if (in(g_frameLv[fi]->dyn.objs)) { home = "frame.dyn"; break; }
                            if (in(g_frameLv[fi]->slopes)) { home = "frame.slopes"; break; }
                        }
                    std::printf(" from=%s", home);
                }
                std::printf("\n");
            }
        }
        else
            std::printf("REPLAY: SURVIVED to t=%lld x=%.1f (goal %.1f)\n",
                        lastT, (double)s.xAbs, goalX);
        if (g_seedDump >= 0 && !sawSeedTick)
            std::printf("seedrotq: t=%d NOT REACHED - this walk ended at "
                        "t=%lld, so there is no state to seed from\n",
                        g_seedDump, lastT);
        std::printf("trace -> %s.trace.csv\n", outPath.c_str());
        if (g_formulaRec)
            std::printf("formularec: %lld formula-driven placements read the recording, %lld took "
                        "the formula (a box the state and the attempt disagree on)\n",
                        g_formulaRecHit.load(), g_formulaRecMiss.load());
        g_snapOut = nullptr;
        return 0;
    }
    struct CoinTables {
        decltype(L)& L;
        decltype(passedOn)& passedOn;
        decltype(tapGive)& tapGive;
        decltype(tapGateBits)& tapGateBits;
        decltype(gatedMask)& gatedMask;
        decltype(tapMissX)& tapMissX;
        decltype(tapMissY)& tapMissY;
        decltype(coinGate)& coinGate;
        decltype(tapSole)& tapSole;
        decltype(coinEnable)& coinEnable;
        decltype(coinReq)& coinReq;
        decltype(coinPruneOk)& coinPruneOk;
    };
    const CoinTables coinTables{L, passedOn, tapGive, tapGateBits, gatedMask, tapMissX,
                                tapMissY, coinGate, tapSole, coinEnable, coinReq, coinPruneOk};
    const CurrentRules<CoinTables> rules{goalX, coinOn, coinAll, coinTables};
    rules.init(init);   // the route's part of the root state (--coinmask, --coinskip)
    g_progress.begin(t0, horizon > 0 ? std::min(tEnd, t0 + 2 * horizon) : tEnd);
    struct ProgressGuard { ~ProgressGuard() { g_progress.end(); } } progressGuard;
    double pp[7] = {0, 0, 0, 0, 0, 0, 0};
    auto ppLast = std::chrono::steady_clock::now();
    auto ppMark = [&](int b) {
        if (!g_phaseProf) return;
        const auto n = std::chrono::steady_clock::now();
        pp[b] += std::chrono::duration<double>(n - ppLast).count();
        ppLast = n;
    };
    const auto ppStart = std::chrono::steady_clock::now();
    struct SearchIn {
        decltype(t0)& t0;
        decltype(dbgLayers)& dbgLayers;
        decltype(init)& init;
        decltype(horizon)& horizon;
        decltype(planCut)& planCut;
        decltype(goalX)& goalX;
        decltype(tEnd)& tEnd;
        decltype(curDxF)& curDxF;
        decltype(model)& model;
        decltype(pool)& pool;
        decltype(kParallelMin)& kParallelMin;
        decltype(ppMark)& ppMark;
        decltype(checkWanted)& checkWanted;
    };
    struct SearchOutcome {
        decltype(bestT)& bestT;
        decltype(bestX)& bestX;
        decltype(goalState)& goalState;
        decltype(solved)& solved;
        decltype(maxAlive)& maxAlive;
        decltype(capDropped)& capDropped;
        decltype(capHits)& capHits;
        decltype(cellCuts)& cellCuts;
        decltype(cellSpared)& cellSpared;
        decltype(cellTwinsKept)& cellTwinsKept;
        decltype(census)& census;
        decltype(twin)& twin;
    };
    struct SearchEnv {
        SearchIn in;
        SearchOutcome outcome;
    };
    // The autonomous triggers' fire state before the search, for --witnessauto's witness walk.
    struct AutoFire { int fireT; double fireX, lockLastX; };
    std::vector<AutoFire> autoBeforeSearch;
    for (const AutoTrig& A : g_autoTrig) autoBeforeSearch.push_back({A.fireT, A.fireX, A.lockLastX});
    struct RlAuto { int fireT; double fireX; int fireSpreadT; double lockLastX; };
    struct RlRot { int firedT, revT; };
    auto rlSaveTrig = [](std::vector<RlAuto>& a, std::vector<RlRot>& r) {
        a.clear();
        r.clear();
        for (const AutoTrig& A : g_autoTrig) a.push_back({A.fireT, A.fireX, A.fireSpreadT, A.lockLastX});
        for (const RotTrig& R : g_rotTrig) r.push_back({R.firedT, R.revT});
    };
    std::vector<RlAuto> rlAutoBefore;
    std::vector<RlRot> rlRotBefore;
    if (g_resimLadder) rlSaveTrig(rlAutoBefore, rlRotBefore);
    SearchResult found;
    {
        SearchEnv se{
            {t0, dbgLayers, init, horizon, planCut, goalX, tEnd, curDxF, model, pool, kParallelMin,
             ppMark, checkWanted},
            {bestT, bestX, goalState, solved, maxAlive, capDropped, capHits, cellCuts, cellSpared,
             cellTwinsKept, census, twin}};
        ++g_run.calls;
        int rc = g_searchMethod == 1   ? searchBeam(se, rules, found)
                 : g_searchMethod == 2 ? searchMontecarlo(se, rules, found)
                                       : searchLayers(se, rules, found);
        if (rc < 0 && found.kind == SearchResult::Declined && g_declineToLadder) {
            std::printf("search: declined -- the layer DP answers this call under the cap ladder\n");
            g_declinedToLadder = true;
            return 0;
        }
        if (rc < 0 && found.kind == SearchResult::Declined) {
            std::printf("search: declined -- the layer DP answers this call\n");
            found = SearchResult{};
            rc = searchLayers(se, rules, found);
        }
        if (!found.deterministic) ++g_run.nondet;
        if (rc >= 0) return rc;
    }
    if (g_fireBCheck)
        std::printf("firebcheck: bit-without-tick=%llu tick-without-bit=%llu too-early=%llu"
                    " (both must be 0)\n", g_fireBNoTick, g_fireBNoBit, g_fireBTooEarly);
    std::printf("capstat: maxAlive=%zu capHits=%lld dropped=%lld cap=%zu\n",
                maxAlive, capHits, capDropped, g_aliveCap);
    if (g_groupXSplit) {
        std::printf("groupxsplit: gap>2*%.0f+|dx| layers-split=%lld max-parts=%d\n",
                    kGroupWinMargin, g_groupXSplitLayers, g_groupXSplitMax);
        g_groupXSplitLayers = 0;
        g_groupXSplitMax = 0;
    }
    if (g_heldCellCap && g_heldAll)
        std::printf("cellcap: spared=%lld cut=%lld twinsKept=%lld (layers over the cap in states "
                    "whose cells fit / layers cut by cells / twins kept past the cap)\n",
                    cellSpared, cellCuts, cellTwinsKept);
    if (g_searchCensus > 0) census.print();
    twin.print();
    if (g_coinDbgT >= 0 && coinOn)
        for (size_t i = 0; i < L.coins.size() && i < 8; ++i) {
            if (g_coinNearDy[i] < 0.0) {
                std::printf("coinnear: coin=%zu at (%.0f,%.0f) NEVER ENTERED "
                            "(seen=%lld, the coin test reached x=%.0f, this "
                            "coin's x bound starts at %.0f)%s\n",
                            i, L.coins[i].cx, L.coins[i].cy, g_coinSeen[i],
                            g_coinProbeMaxX, L.coins[i].cx - L.coins[i].hw - 15.0,
                            (g_coinProbeMaxX < L.coins[i].cx - L.coins[i].hw - 15.0)
                                ? "  <-- NOT YET: the search never got this far"
                                : "  <-- a real zero: the search passed it and no "
                                  "state was inside the x bound");
                continue;
            }
            std::printf("coinnear: coin=%zu at (%.0f,%.0f) closest |dy|=%.3f at "
                        "t=%lld player=(%.3f,%.3f)  yBound=%.1f  seen=%lld%s\n",
                        i, L.coins[i].cx, L.coins[i].cy, g_coinNearDy[i],
                        g_coinNearT[i], g_coinNearX[i], g_coinNearY[i],
                        L.coins[i].hh + 15.0, g_coinSeen[i],
                        (g_coinNearDy[i] <= L.coins[i].hh + 5.0) ? "  <-- inside" : "");
        }
    if (g_keyCensus) {
        long long tot = 0;
        for (int b = 0; b < kTouchBits; ++b) tot += g_keyCount[b].load();
        std::printf("keycensus: total=%lld over %zu boxes\n", tot, g_touch.size());
        for (size_t b = 0; b < g_touch.size() && b < (size_t)kTouchBits; ++b)
            std::printf("keycensus: box=%zu uid=%d cx=%.0f moveTicks=%lld splits=%lld\n",
                        b, g_touch[b].uid, g_touch[b].cx,
                        (long long)(b < g_touchMoveTicks.size() ? g_touchMoveTicks[b] : -1),
                        (long long)g_keyCount[b].load());
    }
    clearReport();
    g_outcome.capHits = capHits;
    if (!g_fixups.empty()) {
        std::printf("fixups: %lld transitions overridden this call\n",
                    g_fixupHits);
        const FixupCensusRow fc = fixupCensus();
        for (int f = 0; f < 4; ++f)
            for (int r = 0; r < 2; ++r)
                if (frameRevReach(f, r) || fc.call[f][r] || fc.hit[f][r])
                    std::printf("framerev: f%d rev%d steps=%lld lookups=%lld hits=%lld\n",
                                f, r, frameRevReach(f, r), fc.call[f][r], fc.hit[f][r]);
        const auto perFrame = [](const long long (&n)[4][2], int f) { return n[f][0] + n[f][1]; };
        std::printf("fixupcall:  f0=%lld f1=%lld f2=%lld f3=%lld rotated=%lld",
                    perFrame(fc.call, 0), perFrame(fc.call, 1),
                    perFrame(fc.call, 2), perFrame(fc.call, 3), fc.callRot);
        for (int i = 0, n = g_fixupCallRotSeen.load(); i < n; ++i)
            std::printf(" x=%.1f", g_fixupCallRotX[i]);
        std::printf("\n");
        std::printf("fixupframe: f0=%lld f1=%lld f2=%lld f3=%lld rotated=%lld",
                    perFrame(fc.hit, 0), perFrame(fc.hit, 1),
                    perFrame(fc.hit, 2), perFrame(fc.hit, 3), fc.hitRot);
        for (int i = 0, n = g_fixupRotSeen.load(); i < n; ++i)
            std::printf(" x=%.1f", g_fixupRotX[i]);
        std::printf("\n");
    }
    if (g_refWatch)
        std::printf("refwatch: %s%s%s%s\n",
                    g_refLostAt < 0 ? "CARRIED to the end"
                                    : "lost (see the line above)",
                    // Past an ADOPT the carrier is a substitute, not the reference.
                    g_refAdoptT >= 0 ? " -- a SUBSTITUTE lineage since t=" : "",
                    g_refAdoptT >= 0 ? std::to_string(g_refAdoptT).c_str() : "",
                    g_refAdoptT >= 0 ? (", overlooked:" + g_refAdoptFields).c_str() : "");
    if (g_phaseProf) {
        ppMark(6);
        const double tot = std::chrono::duration<double>(std::chrono::steady_clock::now() - ppStart).count();
        std::printf("phaseprof: layers=%lld total=%.2fs setup=%.2f step=%.2f dedupe=%.2f cap=%.2f "
                    "gc=%.2f mem=%.2f rest=%.2f prep=%.2f\n", bestT - t0, tot, pp[0], pp[1], pp[2],
                    pp[3], pp[4], pp[5], pp[6],
                    std::chrono::duration<double>(ppStart - cliT0).count());
    }
    if (g_rjOn)
        std::printf("rejoin: t0=%lld after=%lld reached=%lld layers=%lld exact=%lld(m%d,n%lld) "
                    "near2=%lld(m%d,n%lld) near8=%lld(m%d,n%lld)\n",
                    t0, g_rjAfter, bestT, g_rjLayers,
                    g_rjFirst[0], g_rjMode[0], g_rjLayersNear[0],
                    g_rjFirst[1], g_rjMode[1], g_rjLayersNear[1],
                    g_rjFirst[2], g_rjMode[2], g_rjLayersNear[2]);
    if (checkWanted) {
        while (g_check.enabled.load(std::memory_order_acquire)
               && !g_check.cancel.load(std::memory_order_acquire)
               && g_check.judged.load(std::memory_order_acquire) < g_check.published())
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (g_check.cancel.load(std::memory_order_acquire)) {
            std::printf("CANCELLED: after the search, waiting on a checkpoint (search abandoned "
                        "by the caller)\n");
            std::fflush(stdout);
            g_outcome.verdict = VerdictCancelled;
            g_outcome.cancelT = bestT;
            g_outcome.deepT = bestT;
            g_outcome.deepX = bestX;
            return 3;
        }
    }
    if (found.kind == SearchResult::Partial) {
        solved = true;
        goalState = found.goal;
        planCut = 0;   // no truncation: the driver wants to reach the wall
        std::printf("PARTIAL: frontier died at t=%lld x=%.1f, emitting the "
                    "deepest branch\n", found.t, found.x);
        g_outcome.verdict = VerdictPartial;
        g_outcome.deepT = bestT;
        g_outcome.deepX = bestX;
    }
    auto coinWallAt = [&](double x) -> int {
        if (!coinOn) return -1;
        for (size_t ci = 0; ci < L.coins.size() && ci < coinPruneOk.size(); ++ci) {
            if (!coinPruneOk[ci]) continue;
            const double edge = (double)L.coins[ci].cx + (double)L.coins[ci].hw;
            if (x >= edge - 5.0 && x <= edge + 40.0) {
                std::printf("coinwall: the frontier died at coin %zu's miss prune (x %.1f, far "
                            "edge %.1f)\n", ci, x, edge);
                return (int)ci;
            }
        }
        return -1;
    };
    if (g_outcome.deepX >= 0.0 && coinOn) {
        const int cw = coinWallAt(bestX);
        if (cw >= 0) g_outcome.coinWall = cw;
    }
    if (found.kind == SearchResult::NoPlan) {
        std::printf("FAILED: frontier died at t=%lld x=%.1f\n", found.t, found.x);
        g_outcome.verdict = VerdictFailed;
        g_outcome.deepT = bestT;
        g_outcome.deepX = bestX;
        // the last layer the frontier had, as the method handed it back (SearchResult::lastLayer)
        const std::vector<State>& prev = found.lastLayer;
        for (size_t i = 0; i < prev.size() && i < 12; ++i)
            std::printf("  prev[%zu]: y=%.2f vy=%.2f mode=%d g=%d held=%d\n", i,
                        (double)prev[i].y, (double)prev[i].vy, (int)prev[i].mode,
                        (int)prev[i].grounded, (int)prev[i].held);
        return 1;
    }
    std::printf("SOLVED at x=%.0f, reconstructing plan\n", (double)goalState.y * 0 + goalX);
    g_inRecon = true;
    if (g_outcome.verdict != VerdictPartial) g_outcome.verdict = VerdictSolved;

    // the per-tick input level, as the search method handed it back (SearchResult::inputs)
    std::vector<uint8_t> lvl = std::move(found.inputs);
    if (planCut > 0 && (long long)lvl.size() > planCut - t0)
        lvl.resize((size_t)std::max<long long>(0, planCut - t0));
    if (g_rjJoinT >= 0 && (long long)lvl.size() == g_rjJoinT - t0) {
        size_t added = 0;
        for (long long k = g_rjJoinT + 1;; ++k) {
            const auto it = g_rjRows.find(k);
            if (it == g_rjRows.end() || it->second.act < 0) break;
            lvl.push_back((uint8_t)it->second.act);
            ++added;
        }
        std::printf("rejoin: the plan carries on with %zu ticks of the old plan's inputs\n", added);
    } else if (g_rjJoinT >= 0) {
        std::printf("rejoin: the prefix is %zu ticks, not %lld - not joined\n", lvl.size(),
                    g_rjJoinT - t0);
    }
    std::vector<uint8_t> modeAt(lvl.size(), 0);  // filled by the resim below

    std::vector<State> rlStates;
    std::vector<double> rlLockX;   // each autonomous trigger's lockLastX after each tick (the walk writes it)
    uint8_t rlKind0 = kDeathPhysics;
    std::vector<RlAuto> rlAutoPlan;   // the triggers as the search behind the current plan left them
    std::vector<RlRot> rlRotPlan;
    if (g_resimLadder) rlSaveTrig(rlAutoPlan, rlRotPlan);

    auto witnessWalk = [&]() {
        std::ofstream tr(outPath + ".trace.csv");
        tr.precision(10);
        tr << "tick,x,y,vy,mode,grounded,dual,y2,vy2,flip2,act,flip,frame"
              ",rotspent,rotchan,rotrev,dead,key\n";
        std::ofstream sn;
        if (!snapLogPath.empty()) {
            sn.open(snapLogPath);
            g_snapOut = &sn;
        }
        State s = init;
        FlyBand rBand;
        size_t rIdx = 0;
        float rDxF = kDxF;
        size_t rSpd = 0;
        auto rGate = [&](const Obj& o) { return o.cx - o.hw - kCubeHalf; };
        while (rSpd < L.speeds.size() && rGate(L.speeds[rSpd]) <= x0) {
            rDxF = dxForSpeedId(L.speeds[rSpd].id);
            ++rSpd;
        }
        if (g_startSpeedMul > 0.0) rDxF = dxForSpeedMul(g_startSpeedMul);
        if (g_witnessAuto) {
            for (size_t ai = 0; ai < g_autoTrig.size() && ai < autoBeforeSearch.size(); ++ai) {
                g_autoTrig[ai].fireT = autoBeforeSearch[ai].fireT;
                g_autoTrig[ai].fireX = autoBeforeSearch[ai].fireX;
                g_autoTrig[ai].lockLastX = autoBeforeSearch[ai].lockLastX;
            }
            g_rolloutFiresAuto = true;
        }
        struct RolloutAutoOff {
            ~RolloutAutoOff() { g_rolloutFiresAuto = false; }
        } rolloutAutoOff;
        const bool wrOn = g_wrc.active && !g_witnessAuto && snapLogPath.empty() && g_dynDbg == -1
                          && g_rjJoinT < 0 && !g_refWatch && g_rotTrig.empty() && !g_resimLadder;
        std::ostringstream trBuf;   // the rows, when they are kept or fingerprinted
        trBuf.precision(10);
        std::ostream& trOut = (wrOn || g_walkResumeCheck) ? static_cast<std::ostream&>(trBuf)
                                                          : static_cast<std::ostream&>(tr);
        bool wrSigOk = true;
        const uint64_t wrSig = wrOn ? walkWorldSig(model, t0, init, rDxF, wrSigOk) : 0;
        size_t wrFrom = 0;
        const long long wrFixupHits0 = g_fixupHits;   // what the search left; the walk adds to it
        bool wrPreBtnSeen = false;    // has the walk written g_preBtnY, and the last it wrote
        double wrPreBtnY = 0.0;
        std::vector<WalkCheckpoint> wrKeep;   // this walk's checkpoints, for the next attempt
        // why this walk was or was not taken up (--walkresumecheck prints it)
        const char* wrWhy = !wrOn ? "not taken up here"
                            : !wrSigOk ? "a start-state pointer encodeObj cannot name"
                            : g_wrc.t0 != t0 ? "no earlier walk in this ladder"
                            : g_wrc.worldSig != wrSig ? "the world it starts from differs"
                            : "no checkpoint in the shared prefix";
        size_t wrCommon = 0;
        if (wrOn && wrSigOk && g_wrc.t0 == t0 && g_wrc.worldSig == wrSig) {
            size_t common = 0;
            while (common < lvl.size() && common < g_wrc.lvl.size() && lvl[common] == g_wrc.lvl[common])
                ++common;
            wrCommon = common;
            uint64_t stickyNow = 1469598103934665603ull;
            model.levelOf(0).dyn.foldSticky(stickyNow, false);
            const WalkCheckpoint* cp = nullptr;
            for (const WalkCheckpoint& k : g_wrc.cps)
                if (k.i <= common && k.i < lvl.size() && k.sticky == stickyNow && (!cp || k.i > cp->i))
                    cp = &k;
            if (cp) {
                wrWhy = "taken up";
                s = cp->s;
                s.snapObj = model.decodeObj(cp->objRefs[0]);
                s.usedOrb = model.decodeObj(cp->objRefs[1]);
                s.snapObj2 = model.decodeObj(cp->objRefs[2]);
                s.usedOrb2 = model.decodeObj(cp->objRefs[3]);
                for (int k = 0; k < 4; ++k) s.usedPad[k] = model.decodeObj(cp->objRefs[4 + k]);
                for (int k = 0; k < 4; ++k) s.usedPad2[k] = model.decodeObj(cp->objRefs[8 + k]);
                for (size_t a = 0; a < g_autoTrig.size() && a < cp->autoLockLastX.size(); ++a)
                    g_autoTrig[a].lockLastX = cp->autoLockLastX[a];
                model.loadWorld(0, cp->carry, t0 + (long long)cp->i);
                model.levelOf(0).dyn.resetCursors();
                g_resimDead = cp->resimDead;
                g_resimTicks = cp->resimTicks;
                g_dieRecon = cp->dieRecon;
                g_resimFirst = cp->resimFirst;
                g_resimLast = cp->resimLast;
                g_resimUid = cp->resimUid;
                g_resimFrame = cp->resimFrame;
                g_resimWhy = cp->resimWhy;
                g_resimPX = cp->resimPX;
                g_resimObjX = cp->resimObjX;
                g_resimObjY = cp->resimObjY;
                g_resimTrig = cp->resimTrig;
                g_dieReconWhy = cp->dieReconWhy;
                if (cp->dieRecon > 0) {
                    g_deadWhy = cp->deadWhy;
                    g_deadObj = model.decodeObj(cp->deadObjRef);
                    g_deadCx = cp->deadCx;
                    g_deadCy = cp->deadCy;
                }
                g_fixupHits = wrFixupHits0 + cp->fixupHitsWalk;
                if (cp->preBtnSeen) {
                    g_preBtnY = cp->preBtnY;
                    wrPreBtnSeen = true;
                    wrPreBtnY = cp->preBtnY;
                }
                // first used inside the part taken up: use it now, which prints its line
                if (cp->bandTrackCam >= 0 && g_bandTrackCam < 0) (void)bandTrackIsCamera();
                for (size_t k = 0; k < cp->i && k < modeAt.size(); ++k) modeAt[k] = g_wrc.modeAt[k];
                trBuf.write(g_wrc.trace.data(), (std::streamsize)cp->traceLen);
                wrFrom = cp->i;
                for (const WalkCheckpoint& k : g_wrc.cps)
                    if (k.i <= cp->i) wrKeep.push_back(k);
            }
        }
        auto wrPre =
            [&](size_t i, long long t, const State& prev, State& c, bool rdead, const StepCtx& K) {
                // this tick's step wrote it (stepBoth clears the flag); tracked for the check as well
                if ((wrOn || g_walkResumeCheck) && g_preBtnSet) {
                    wrPreBtnSeen = true;
                    wrPreBtnY = g_preBtnY;
                }
                model.fireGates(c, prev.action, (int)lvl[i], K, (int)prev.frame, t, rdead);
                if (rdead) {
                    if (g_resimDead == 0) {
                        g_resimFirst = (int)t;
                        g_resimWhy = g_deadWhy;
                        rlKind0 = g_deadKind;   // --resimladder: physics, rule or prune
                        {
                            double wxR, wyR;
                            fromFrame((int)c.frame, (double)c.xAbs, (double)c.y, wxR, wyR);
                            g_resimPX = (float)wxR;
                        }
                        g_resimUid = g_deadObj ? g_deadObj->uid : -1;
                        g_resimObjX = g_deadCx;
                        g_resimObjY = g_deadCy;
                        g_resimTrig = prev.trig;
                        // ...and the frame those coordinates are expressed in.
                        g_resimFrame = (int)c.frame;
                    }
                    g_resimLast = (int)t;
                    ++g_resimDead;
                }
                ++g_resimTicks;
            };
        auto wrPost = [&](size_t i, long long t, const State& s, bool rdead) {
                modeAt[i] = s.mode;
                if (g_resimLadder) {   // the state after tick t0 + i + 1
                    if (rlStates.size() <= i) rlStates.resize(i + 1);
                    rlStates[i] = s;
                    const size_t nA = g_autoTrig.size();
                    if (rlLockX.size() < (i + 1) * nA) rlLockX.resize((i + 1) * nA);
                    for (size_t a = 0; a < nA; ++a) rlLockX[i * nA + a] = g_autoTrig[a].lockLastX;
                }
                trOut << t << ',' << (double)s.xAbs << ',' << s.y << ',' << s.vy << ','
                   << (int)s.mode << ',' << (int)s.grounded << ',' << (int)s.dual
                   << ',' << s.y2 << ',' << s.vy2 << ',' << (int)s.flip2 << ','
                   << (int)lvl[i] << ',' << (int)s.flip << ',' << (int)s.frame
                   << ',' << s.rotSpent << ',' << (int)s.rotChan
                   << ',' << (unsigned)s.rotRev << ',' << (rdead ? 1 : 0)
                   << ',' << (unsigned long long)keyOf(s, (long long)t) << "\n";
                if (g_rjJoinT >= 0 && (long long)t > g_rjJoinT && g_outcome.rejoinBadT < 0) {
                    const auto ot = g_rjRows.find((long long)t);
                    if (ot != g_rjRows.end()) {
                        const RefRow& o = ot->second;
                        const char* why = nullptr;
                        if ((o.mode >= 0 && (int)s.mode != o.mode) || (o.flip >= 0 && (int)s.flip != o.flip)
                            || (o.frame >= 0 && (int)s.frame != o.frame))
                            why = "mode/flip/frame";
                        else if (std::fabs((double)s.y - o.y) > g_refEps
                                 || std::fabs((double)s.vy - o.vy) > g_refEps)
                            why = "y/vy";
                        else if (rdead && o.dead == 0)
                            why = "a kill the old walk did not have";
                        if (why) {
                            g_outcome.rejoinBadT = (long long)t;
                            g_outcome.rejoinBadWhy = why;
                            std::printf("rejoin: the joined walk leaves the old one at t=%lld (%s)\n",
                                        (long long)t, why);
                        }
                    }
                }
                if (wrOn && wrSigOk && (i + 1) % (size_t)g_walkResumeEvery == 0 && i + 1 > wrFrom) {
                    WalkCheckpoint k;
                    k.i = i + 1;
                    k.s = s;
                    bool ok = true;
                    k.objRefs[0] = model.encodeObj(s.snapObj, ok);
                    k.objRefs[1] = model.encodeObj(s.usedOrb, ok);
                    k.objRefs[2] = model.encodeObj(s.snapObj2, ok);
                    k.objRefs[3] = model.encodeObj(s.usedOrb2, ok);
                    for (int q = 0; q < 4; ++q) k.objRefs[4 + q] = model.encodeObj(s.usedPad[q], ok);
                    for (int q = 0; q < 4; ++q) k.objRefs[8 + q] = model.encodeObj(s.usedPad2[q], ok);
                    model.saveWorld(0, k.carry);
                    k.autoLockLastX.reserve(g_autoTrig.size());
                    for (const AutoTrig& A : g_autoTrig) k.autoLockLastX.push_back(A.lockLastX);
                    k.resimDead = g_resimDead;
                    k.resimTicks = g_resimTicks;
                    k.dieRecon = g_dieRecon;
                    k.resimFirst = g_resimFirst;
                    k.resimLast = g_resimLast;
                    k.resimUid = g_resimUid;
                    k.resimFrame = g_resimFrame;
                    k.resimWhy = g_resimWhy;
                    k.resimPX = g_resimPX;
                    k.resimObjX = g_resimObjX;
                    k.resimObjY = g_resimObjY;
                    k.resimTrig = g_resimTrig;
                    k.dieReconWhy = g_dieReconWhy;
                    k.deadWhy = g_deadWhy;
                    k.deadObjRef = model.encodeObj(g_deadObj, ok);
                    k.deadCx = g_deadCx;
                    k.deadCy = g_deadCy;
                    k.fixupHitsWalk = g_fixupHits - wrFixupHits0;
                    k.preBtnSeen = wrPreBtnSeen;
                    k.preBtnY = wrPreBtnY;
                    k.bandTrackCam = g_bandTrackCam;
                    k.traceLen = (size_t)trBuf.tellp();
                    k.sticky = 1469598103934665603ull;
                    model.levelOf(0).dyn.foldSticky(k.sticky, false);
                    if (ok) wrKeep.push_back(std::move(k));
                }
            };
        if (wrFrom > 0) {
            const std::vector<uint8_t> tail(lvl.begin() + (std::ptrdiff_t)wrFrom, lvl.end());
            s = model.rollout(s, t0 + (long long)wrFrom, tail, rDxF,
                [&](size_t i, long long t, const State& prev, State& c, bool rdead, const StepCtx& K) {
                    wrPre(i + wrFrom, t, prev, c, rdead, K);
                },
                [&](size_t i, long long t, const State& st, bool rdead) { wrPost(i + wrFrom, t, st, rdead); });
        } else {
            s = model.rollout(s, t0, lvl, rDxF, wrPre, wrPost);
        }
        if (wrOn) {   // this walk is what the ladder's next attempt can take up
            if (wrSigOk) {
                g_wrc.t0 = t0;
                g_wrc.worldSig = wrSig;
                g_wrc.lvl = lvl;
                g_wrc.trace = trBuf.str();
                g_wrc.modeAt = modeAt;
                g_wrc.cps = std::move(wrKeep);
            } else {
                g_wrc.t0 = -1;
                g_wrc.cps.clear();
            }
        }
        if (wrOn || g_walkResumeCheck) {
            const std::string rows = trBuf.str();
            tr << rows;
            if (g_walkResumeCheck) {
                std::string prof;
                {
                    const std::string all = defaultsProfile();
                    size_t p = 0;
                    while (p < all.size()) {
                        size_t q = all.find(' ', p);
                        if (q == std::string::npos) q = all.size();
                        const std::string tok = all.substr(p, q - p);
                        if (tok.rfind("g_preBtnY=", 0) == 0 && !wrPreBtnSeen)
                            prof += "g_preBtnY=(not written by the walk) ";
                        else if (tok.rfind("g_fixupHits=", 0) != 0 && tok.rfind("g_seatGateSeen=", 0) != 0)
                            prof += tok + ' ';
                        p = q + 1;
                    }
                }
                uint64_t hp = walkFnv(1469598103934665603ull, prof.data(), prof.size());
                {   // the globals themselves, one file per walk of the process, for a diff when
                    // the two runs' fingerprints part
                    static int s_walkN = 0;
                    std::ofstream pf(outPath + ".walkprof" + std::to_string(++s_walkN) + ".txt");
                    pf << prof;
                }
                uint64_t ht = walkFnv(1469598103934665603ull, rows.data(), rows.size());
                uint64_t hm = walkFnv(1469598103934665603ull, modeAt.data(), modeAt.size());
                std::printf("walkstate: t0=%lld ticks=%zu trace=%016llx modeAt=%016llx globals=%016llx\n",
                            t0, lvl.size(), (unsigned long long)ht, (unsigned long long)hm,
                            (unsigned long long)hp);
                std::printf("walkresume: %s (%s; plan prefix shared with the kept walk %zu), took %zu of "
                            "%zu ticks from an earlier attempt, kept %zu checkpoints; in a ladder %d, "
                            "cache %s\n",
                            wrOn ? "on" : "off", wrWhy, wrCommon, wrFrom, lvl.size(),
                            wrOn ? g_wrc.cps.size() : (size_t)0,
                            g_ladder ? 1 : 0,
                            (!g_ladder && (!g_wrc.cps.empty() || g_wrc.active)) ? "LEFT OVER" : "ok");
            }
        }
        g_snapOut = nullptr;
    };
    auto resimLadder = [&]() {
        if (g_resimDead == 0 || g_resimFirst <= t0 || rlKind0 != kDeathPhysics) return;
        if (g_searchMethod != 0) return;
        if (!g_resimLadderPartial && g_outcome.verdict != VerdictSolved) return;
        if (g_resimLadderHold && g_outcome.verdict == VerdictPartial) {
            std::printf("resimladder: the walk dies at t=%d; a PARTIAL in a cap-ladder attempt is left "
                        "to the ladder\n", g_resimFirst);
            return;
        }
        if (!(g_outcome.verdict == VerdictSolved
              || (g_outcome.verdict == VerdictPartial
                  && (long long)g_resimFirst + kResimLadderRungs[0] < g_outcome.deepT)))
            return;
        struct Walked {
            std::vector<uint8_t> lvl, modeAt;
            std::string trace;
            long long dead = 0, ticks = 0, dieRecon = 0, planCut = 0;
            int first = -1, last = -1, uid = -1, frame = -1;
            const char* why = nullptr;
            float px = 0.f, ox = 0.f, oy = 0.f;
            TouchMask trig{};
            std::vector<std::pair<const char*, long long>> dieReconWhy;
            long long rjJoinT = -1, rejoinT = -1, rejoinBadT = -1;
            const char* rejoinBadWhy = nullptr;
        };
        auto keep = [&](Walked& w) {
            w.lvl = lvl;
            w.modeAt = modeAt;
            std::ifstream in(outPath + ".trace.csv", std::ios::binary);
            w.trace.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            w.dead = g_resimDead; w.ticks = g_resimTicks; w.dieRecon = g_dieRecon; w.planCut = planCut;
            w.first = g_resimFirst; w.last = g_resimLast; w.uid = g_resimUid; w.frame = g_resimFrame;
            w.why = g_resimWhy; w.px = g_resimPX; w.ox = g_resimObjX; w.oy = g_resimObjY;
            w.trig = g_resimTrig; w.dieReconWhy = g_dieReconWhy;
            w.rjJoinT = g_rjJoinT; w.rejoinT = g_outcome.rejoinT;
            w.rejoinBadT = g_outcome.rejoinBadT; w.rejoinBadWhy = g_outcome.rejoinBadWhy;
        };
        auto putBack = [&](const Walked& w) {
            lvl = w.lvl;
            modeAt = w.modeAt;
            { std::ofstream o(outPath + ".trace.csv", std::ios::binary); o << w.trace; }
            g_resimDead = w.dead; g_resimTicks = w.ticks; g_dieRecon = w.dieRecon; planCut = w.planCut;
            g_resimFirst = w.first; g_resimLast = w.last; g_resimUid = w.uid; g_resimFrame = w.frame;
            g_resimWhy = w.why; g_resimPX = w.px; g_resimObjX = w.ox; g_resimObjY = w.oy;
            g_resimTrig = w.trig; g_dieReconWhy = w.dieReconWhy;
            g_rjJoinT = w.rjJoinT; g_outcome.rejoinT = w.rejoinT;
            g_outcome.rejoinBadT = w.rejoinBadT; g_outcome.rejoinBadWhy = w.rejoinBadWhy;
        };
        long long rlCut = 0, rlDeepT = -1;
        double rlDeepX = -1.0;
        bool rlSolved = false, rlPartial = false, rlHorizon = false, rlCancelled = false;
        auto researchFrom = [&](long long tr, std::vector<uint8_t>& out) -> bool {
            rlSolved = rlPartial = rlHorizon = rlCancelled = false;
            rlDeepT = -1;
            const size_t ip = (size_t)(tr - t0);
            const size_t nA = g_autoTrig.size();
            if (ip == 0 || ip > rlStates.size() || ip > lvl.size() || rlLockX.size() < ip * nA) return false;
            const State sTr = rlStates[ip - 1];
            // the triggers: as the search behind this plan fired them, less every firing past tr
            for (size_t a = 0; a < nA && a < rlAutoPlan.size() && a < rlAutoBefore.size(); ++a) {
                AutoTrig& A = g_autoTrig[a];
                const RlAuto& p = rlAutoPlan[a];
                const bool kept = rlAutoBefore[a].fireT >= 0
                                  || (p.fireT >= 0 && (long long)p.fireT - A.delay <= tr);
                const RlAuto& u = kept ? p : rlAutoBefore[a];
                A.fireT = u.fireT;
                A.fireX = u.fireX;
                A.fireSpreadT = u.fireSpreadT;
                A.lockLastX = rlLockX[(ip - 1) * nA + a];
            }
            for (size_t r = 0; r < g_rotTrig.size() && r < rlRotPlan.size() && r < rlRotBefore.size(); ++r) {
                RotTrig& R = g_rotTrig[r];
                R.firedT = (rlRotPlan[r].firedT >= 0 && (long long)rlRotPlan[r].firedT <= tr)
                               ? rlRotPlan[r].firedT : rlRotBefore[r].firedT;
                R.revT = (rlRotPlan[r].revT >= 0 && (long long)rlRotPlan[r].revT <= tr)
                             ? rlRotPlan[r].revT : rlRotBefore[r].revT;
            }
            for (int f = 0; f < 4; ++f)
                if (model.frameBound(f)) model.forgetWorld(f);
            const long long t0s = t0, cutS = planCut;
            const State initS = init;
            const float dxS = curDxF;
            const bool rjOnS = g_rjOn, rjUseS = g_rjUse;
            // ...and the first search's outcome, which the lines after the walk still read
            const long long bestTS = bestT, capHitsS = capHits, capDroppedS = capDropped;
            const double bestXS = bestX;
            const bool solvedS = solved;
            const State goalS = goalState;
            const size_t maxAliveS = maxAlive;
            const long long cellCutsS = cellCuts, cellSparedS = cellSpared, cellTwinsS = cellTwinsKept;
            const auto outS = g_outcome;   // (auto: cliMainOnce has a SearchOutcome of its own)
            g_outcome.horizonCut = false;
            t0 = tr;
            init = sTr;
            planCut = 0;
            if (sTr.dx > 0.f) curDxF = sTr.dx;
            bestT = tr;
            bestX = (double)sTr.xAbs;
            solved = false;
            goalState = State{};
            maxAlive = 0;
            capHits = capDropped = 0;
            cellCuts = cellSpared = cellTwinsKept = 0;
            g_rjOn = g_rjUse = false;   // a join would need the splice the first search's has
            g_inRecon = false;
            const bool noCheck = false;   // the caller's checkpoints belong to the first search's t0
            SearchEnv se2{{t0, dbgLayers, init, horizon, planCut, goalX, tEnd, curDxF, model, pool,
                           kParallelMin, ppMark, noCheck},
                          {bestT, bestX, goalState, solved, maxAlive, capDropped, capHits, cellCuts,
                           cellSpared, cellTwinsKept, census, twin}};
            ++g_run.calls;
            SearchResult f2;
            const int rc = searchLayers(se2, rules, f2);
            g_inRecon = true;
            rlSolved = f2.kind == SearchResult::Solved;
            rlPartial = f2.kind == SearchResult::Partial;
            rlHorizon = g_outcome.horizonCut;
            rlCancelled = g_outcome.verdict == VerdictCancelled;
            rlDeepT = bestT;
            rlDeepX = bestX;
            rlCut = rlSolved ? planCut : 0;   // a PARTIAL is not cut: it is there to reach the wall
            t0 = t0s;
            init = initS;
            planCut = cutS;
            curDxF = dxS;
            g_rjOn = rjOnS;
            g_rjUse = rjUseS;
            bestT = bestTS;
            bestX = bestXS;
            solved = solvedS;
            goalState = goalS;
            maxAlive = maxAliveS;
            capHits = capHitsS;
            capDropped = capDroppedS;
            cellCuts = cellCutsS;
            cellSpared = cellSparedS;
            cellTwinsKept = cellTwinsS;
            g_outcome = outS;
            if (rc >= 0 || rlCancelled || f2.inputs.empty()) return false;
            if (!rlSolved
                && (f2.kind != SearchResult::Partial || g_resimLadderHold || !g_resimLadderPartial))
                return false;
            out.assign(lvl.begin(), lvl.begin() + (std::ptrdiff_t)ip);
            std::vector<uint8_t> tail = std::move(f2.inputs);
            if (rlCut > 0 && (long long)tail.size() > rlCut - tr) tail.resize((size_t)(rlCut - tr));
            out.insert(out.end(), tail.begin(), tail.end());
            return true;
        };
        Walked best;
        keep(best);
        const int first0 = best.first;
        long long deathAt = best.first;
        int tries = 0;
        size_t rung = 0;
        bool bestOnDisk = true;   // the trace and the globals are best's
        std::vector<RlAuto> candAuto;
        std::vector<RlRot> candRot;
        const size_t nRungs = sizeof(kResimLadderRungs) / sizeof(kResimLadderRungs[0]);
        while (tries < kResimLadderMax && rung < nRungs) {
            const long long tr = deathAt - kResimLadderRungs[rung];
            if (tr <= t0) break;
            ++tries;
            std::vector<uint8_t> cand;
            if (!researchFrom(tr, cand)) {
                const bool past = rlPartial && !g_resimLadderPartial
                                  && rlDeepT > deathAt + kResimLadderRungs[nRungs - 1];
                char why[96];
                if (rlCancelled)
                    std::snprintf(why, sizeof(why), "cancelled");
                else if (rlPartial)
                    std::snprintf(why, sizeof(why), "PARTIAL to t=%lld%s", rlDeepT,
                                  past ? ", past the death - no rung further back" : "");
                else
                    std::snprintf(why, sizeof(why), "no plan");
                std::printf("resimladder: from t=%lld (%lld before the walk's death at %lld): %s\n",
                            tr, kResimLadderRungs[rung], deathAt, why);
                if (rlCancelled || past) break;
                ++rung;
                continue;
            }
            rlSaveTrig(candAuto, candRot);
            lvl = std::move(cand);
            modeAt.assign(lvl.size(), 0);
            rlStates.clear();
            rlLockX.clear();
            rlKind0 = kDeathPhysics;
            g_resimDead = 0; g_resimTicks = 0; g_dieRecon = 0; g_dieReconWhy.clear();
            g_resimFirst = -1; g_resimLast = -1; g_resimUid = -1; g_resimFrame = -1;
            g_resimWhy = nullptr; g_resimPX = 0.f; g_resimObjX = 0.f; g_resimObjY = 0.f;
            g_resimTrig = TouchMask{};
            g_rjJoinT = -1;   // a plain plan: no old walk to retrace
            g_outcome.rejoinT = -1;
            g_outcome.rejoinBadT = -1;
            g_outcome.rejoinBadWhy = nullptr;
            const long long cutS = planCut;
            planCut = rlCut;
            witnessWalk();
            bestOnDisk = false;
            const bool clean = g_resimDead == 0;
            const long long reach = clean ? t0 + (long long)lvl.size() : (long long)g_resimFirst;
            const long long same = (clean && !rlSolved) ? kResimLadderRungs[0] : 0;
            const bool later = (clean || rlKind0 == kDeathPhysics) && reach > best.first + same;
            char what[64] = "";
            if (!rlSolved) std::snprintf(what, sizeof(what), "PARTIAL to t=%lld, ", rlDeepT);
            std::printf("resimladder: from t=%lld (%lld before the walk's death at %lld): %s%zu ticks, "
                        "walk %s%s\n", tr, kResimLadderRungs[rung], deathAt, what, lvl.size(),
                        clean ? "clean" : ("dies at t=" + std::to_string(g_resimFirst)).c_str(),
                        !later ? " - not later" : clean ? " - kept" : " - later, kept");
            if (later) {
                keep(best);
                bestOnDisk = true;
                rlAutoPlan = candAuto;
                rlRotPlan = candRot;
                // the call's verdict is the kept plan's search's
                if (rlSolved) {
                    g_outcome.verdict = VerdictSolved;
                    g_outcome.horizonCut = rlHorizon;
                    g_outcome.deepT = -1;
                    g_outcome.deepX = -1.0;
                    g_outcome.coinWall = -1;
                } else {
                    g_outcome.verdict = VerdictPartial;
                    g_outcome.horizonCut = false;
                    g_outcome.deepT = rlDeepT;
                    g_outcome.deepX = rlDeepX;
                    g_outcome.coinWall = coinWallAt(rlDeepX);
                }
                if (clean) break;
                deathAt = g_resimFirst;
                rung = 0;
            } else {
                planCut = cutS;
                ++rung;
            }
        }
        if (!bestOnDisk) putBack(best);
        std::printf("resimladder: %d search(es); the walk died at t=%d, now %s\n", tries, first0,
                    best.dead == 0 ? "clean" : ("at t=" + std::to_string(best.first)).c_str());
    };
    witnessWalk();
    if (g_resimLadder) resimLadder();

    if (!g_bandPath.empty()) {
        std::ofstream bo(g_bandPath);
        bo << "tick,alive,ylo,yhi,x,capdrop,merged,classes\n";
        for (const BandRow& b : g_bands)
            bo << b.t << ',' << b.alive << ',' << b.ylo << ',' << b.yhi << ','
               << b.x << ',' << b.capdrop << ',' << b.merged << ',' << b.cls
               << "\n";
        std::printf("bands: %zu layers -> %s\n", g_bands.size(),
                    g_bandPath.c_str());
    }
    if (g_rejoinProbe > 0 && g_rjJoinT >= 0 && g_resimDead > 0 && g_resimFirst >= 0
        && (long long)g_resimFirst >= g_rjJoinT
        && (long long)g_resimFirst - g_rjJoinT <= g_rejoinProbe && !t_rejoinRetry) {
        std::printf("rejoin: the joined walk dies at t=%d, %lld ticks past the join -- searching "
                    "again without the join\n", g_resimFirst, (long long)g_resimFirst - g_rjJoinT);
        std::fflush(stdout);
        std::vector<char*> p;
        for (int i = 0; i < argc; ++i)
            if (std::strcmp(argv[i], "--rejoinuse")) p.push_back(argv[i]);
        struct Retry {
            Retry() { t_rejoinRetry = true; }
            ~Retry() { t_rejoinRetry = false; }
        } retry;
        return cliMainOnce((int)p.size(), p.data());
    }
    std::ofstream out(outPath);
    g_outcome.planWritten = true;   // ...and the trace above with it (cliMain's step 5 keeps both)
    int edges = 0;
    {
        const std::vector<PlanEdge> pe =
            planEdges(lvl, modeAt, t0, (int)init.held, init.mode);
        for (const PlanEdge& e : pe) {
            out << "input=" << e.press << ',' << e.level << "\n";
            ++edges;
        }
        g_run.lastT0 = t0;
        g_run.lastEdges.clear();
        for (const PlanEdge& e : pe) g_run.lastEdges.emplace_back(e.press, e.level);
    }
    std::printf("recondie: n=%lld inRecon=%d last=%s\n",
                g_dieRecon, g_inRecon ? 1 : 0,
                (g_dieRecon && g_deadWhy) ? g_deadWhy : "-");
    for (const auto& e : g_dieReconWhy)
        std::printf("recondiewhy: %s=%lld\n", e.first ? e.first : "?", e.second);
    const long long rDead = g_resimDead, rOf = g_resimTicks;
    const int rFirst = g_resimFirst, rLast = g_resimLast;
    const char* rWhy = g_resimWhy;
    if (g_vetoPhys && rDead > 0 && g_outcome.verdict == VerdictSolved && rWhy) {
        static const char* const kPhys[] = {"cube/hazard", "wave/hazard", "fly/hazard",
                                            "spider/tp-hazard", "cube/solid-side",
                                            "fly/solid-side", "crush"};
        bool phys = false;
        for (const char* p : kPhys)
            if (!std::strcmp(rWhy, p)) phys = true;
        if (phys) {
            g_outcome.verdict = VerdictPartial;
            g_outcome.deepT = rFirst;
            g_outcome.deepX = g_resimPX;
            std::printf("vetophys: SOLVED -> PARTIAL at t=%d x=%.1f (witness %s, start frame %d)\n",
                        rFirst, (double)g_resimPX, rWhy, (int)init.frame);
        }
    }
    std::printf("gfirestat: groups=%lld spread=%lld sum=%lld max=%d\n",
                g_gfireGroups, g_gfireSpread, g_gfireSum, g_gfireMax);
    std::printf("resimdie: dead=%lld of=%lld first=%d last=%d contig=%d why=%s\n",
                rDead, rOf, rFirst, rLast,
                (rDead > 0 && (long long)(rLast - rFirst + 1) == rDead) ? 1 : 0,
                rWhy ? rWhy : "-");
    if (g_resimDead > 0)
        std::printf("resimwho: uid=%d obj=(%.1f,%.1f) frame=%d trig=0x%08llx\n",
                    g_resimUid, g_resimObjX, g_resimObjY, g_resimFrame,
                    (unsigned long long)g_resimTrig.word(0));
    if (g_verdictInfo && g_outcome.verdict == VerdictSolved) {
        const int vUid   = (rDead > 0) ? g_resimUid   : -1;
        const int vFrame = (rDead > 0) ? g_resimFrame : -1;
        std::printf("vinfo: verdict=SOLVED dead=%lld of=%lld first=%d last=%d "
                    "why=%s uid=%d objframe=%d startframe=%d\n",
                    rDead, rOf, rFirst, rLast,
                    (rDead > 0 && rWhy) ? rWhy : "-",
                    vUid, vFrame, (int)init.frame);
    }
    std::fflush(stdout);
    if (rOf > 0) {
        g_outcome.resimDead = rDead;
        g_outcome.resimFirst = rFirst;
        g_outcome.resimWhy = rWhy;   // a string literal: static, and dp is
        g_outcome.resimUid = g_resimUid;
        g_outcome.resimObjX = g_resimObjX;
        g_outcome.resimObjY = g_resimObjY;
        g_outcome.resimTrig = g_resimTrig;
        g_outcome.resimFrame = g_resimFrame;
    }
    std::printf("plan: %d edges, %zu ticks -> %s\n", edges, lvl.size(),
                outPath.c_str());
    return 0;
}

inline constexpr int kLadderMaxAttempts = 8;
inline constexpr double kLadderLead = 600.0;
inline constexpr double kLadderPast = 300.0;

struct PlainElsewhere {
    bool (*start)(const std::vector<std::string>& argv) = nullptr;
    int (*take)() = nullptr;
    // The ladder ended without it: stop it and throw it away.
    void (*drop)() = nullptr;
    void (*check)(int rc) = nullptr;
    bool (*ready)(long long bestT) = nullptr;
};
inline PlainElsewhere g_plainElsewhere;

inline int cliMain(int argc, char** argv) {
    long long c0 = 0;
    std::vector<std::string> base;
    for (int i = 0; i < argc; ++i) {
        if (i + 1 < argc && !std::strcmp(argv[i], "--capladder")) {
            c0 = std::atoll(argv[++i]);
            continue;
        }
        // the ladder writes its own maps; a caller's would be overwritten by them anyway
        if (i + 1 < argc && !std::strcmp(argv[i], "--capmap")) { ++i; continue; }
        if (i + 1 < argc && !std::strcmp(argv[i], "--gridmap")) { ++i; continue; }
        base.push_back(argv[i]);
    }
    if (c0 <= 0) return cliMainOnce(argc, argv);
    bool lpLadder = true, lpCheck = false;
    for (const std::string& s : base) {
        if (s == "--ladderprefix") lpLadder = true;
        if (s == "--no-ladderprefix") lpLadder = false;
        if (s == "--ladderprefixcheck") lpCheck = true;
    }
    if (lpCheck) lpLadder = true;
    g_lpc = LadderPrefixCache{};
    bool wrLadder = false, wrCheck = false;
    for (const std::string& s : base) {
        if (s == "--walkresume") wrLadder = true;
        if (s == "--walkresumecheck") wrCheck = true;
    }
    if (wrCheck)
        std::printf("walkresume: at the ladder's entry the cache holds %zu checkpoints (%s)\n",
                    g_wrc.cps.size(), g_wrc.cps.empty() && !g_wrc.active ? "empty" : "LEFT OVER");
    g_wrc = WalkResumeCache{};
    g_wrc.active = wrLadder;
    // The attempts share one level (LadderLevelCache), for this ladder only.
    static unsigned long long s_ladders = 0;
    g_ladder = ++s_ladders;
    g_ladderBestT.store(-1);
    g_ladderStop.store(false);
    struct EndLadder {
        ~EndLadder() {
            g_ladder = 0;
            g_ladderLevel = LadderLevelCache{};
            g_lpc = LadderPrefixCache{};   // --ladderprefix: the saves and the kept arena
            g_wrc = WalkResumeCache{};     // --walkresume: the kept walk, for this ladder only
            g_ladderBestT.store(-1);
            g_ladderStop.store(false);
        }
    } endLadder;
    {
        int method = 0;
        for (size_t i = 0; i + 1 < base.size(); ++i)
            if (base[i] == "--search") method = base[i + 1] == "dp" ? 0 : 1;
        if (method) {
            std::vector<char*> p;
            for (std::string& s : base) p.push_back(s.data());
            struct Clear {
                ~Clear() { g_declineToLadder = false; }
            } clear;
            g_declineToLadder = true;
            g_declinedToLadder = false;
            const int rc = cliMainOnce((int)p.size(), p.data());
            g_declineToLadder = false;
            if (!g_declinedToLadder) return rc;
            base.push_back("--search");
            base.push_back("dp");
        }
    }
    long long fullCap = -1;
    const std::string noGrid = "-1e9:1";
    struct Elsewhere {
        bool on = false, taken = false;
        ~Elsewhere() {
            if (on && !taken && g_plainElsewhere.drop) g_plainElsewhere.drop();
        }
    } elsewhere;
    if (g_plainElsewhere.start && g_plainElsewhere.take && g_plainElsewhere.drop) {
        std::vector<std::string> a = base;
        a.push_back("--gridmap");
        a.push_back(noGrid);
        elsewhere.on = g_plainElsewhere.start(a);
    }

    // --ladderback's envelope step: the attempts run with --capenvelope while this is set.
    bool envOn = false;
    uint32_t envCoinTried = 0;
    bool envCoinPending = false;
    long long envCoinT = -1;
    auto run = [&](const std::string* map, const std::string* grid) {
        std::vector<std::string> a = base;
        if (envOn) a.push_back("--capenvelope");
        if (map) {
            a.push_back("--capmap");
            a.push_back(*map);
        }
        if (grid) {
            a.push_back("--gridmap");
            a.push_back(*grid);
        }
        std::vector<char*> p;
        p.reserve(a.size());
        for (std::string& s : a) p.push_back(s.data());
        return cliMainOnce((int)p.size(), p.data());
    };

    struct Window {
        double wall, lead;
        long long cap;
        bool stepped = false, lastBack = false;
        double lastWx = 0.0;
        long long lastT = -1;   // the tick the frontier died on after the window's last change
        bool backFailed = false, capFailed = false;
        int over = 0;   // --ladderovercap steps taken (cap above --cap)
    };
    std::vector<Window> wins;
    auto mapOf = [&]() {
        std::vector<double> edges;
        for (const Window& w : wins) {
            edges.push_back(w.wall - w.lead);
            edges.push_back(w.wall + kLadderPast);
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        char buf[64];
        std::snprintf(buf, sizeof(buf), "-1e9:%lld", c0);
        std::string m = buf;
        for (double e : edges) {
            long long c = c0;
            for (const Window& w : wins)
                if (e >= w.wall - w.lead && e < w.wall + kLadderPast) c = std::max(c, w.cap);
            std::snprintf(buf, sizeof(buf), ",%.1f:%lld", e, c);
            m += buf;
        }
        return m;
    };

    auto frontierDied = [] {
        const int v = g_outcome.verdict;
        return (v == VerdictPartial || v == VerdictFailed) && g_outcome.deepX >= 0.0
               && g_outcome.capHits >= 0;
    };
    std::string outPath;
    for (size_t i = 0; i + 1 < base.size(); ++i)
        if (base[i] == "--out") outPath = base[i + 1];
    auto planHash = [&]() -> std::string {
        std::string bytes;
        if (outPath.empty() || !readFileBytes(outPath, bytes)) return "none";
        uint64_t h = 1469598103934665603ULL;
        for (char c : bytes) {
            h ^= (uint8_t)c;
            h *= 1099511628211ULL;
        }
        char b[24];
        std::snprintf(b, sizeof b, "%016llx", (unsigned long long)h);
        return b;
    };
    int nAttempts = 0, nIdle = 0;
    std::string plainHash = "-";
    const char* via = "attempt";
    std::string rungs;
    auto rungNote = [&](const char* tag, double ms, long long cap, long long hits, long long deepT,
                        long long work) {
        char r[112];
        if (tag[0]) std::snprintf(r, sizeof r, "%s%s%.0f", rungs.empty() ? "" : ",", tag, ms);
        else std::snprintf(r, sizeof r, "%s%.0f/%lld/%lld/%lld/%lld", rungs.empty() ? "" : ",", ms, cap, hits, deepT, work);
        rungs += r;
    };
    struct Account {
        std::function<void()> f;
        ~Account() { f(); }
    } account{[&] {
        char b[960];
        const std::string chosen = planHash();
        std::snprintf(b, sizeof b, "attempts=%d idle=%d plain=%s chosen=%s via=%s rungs=%s", nAttempts,
                      nIdle, plainHash.c_str(), chosen.c_str(), via, rungs.empty() ? "-" : rungs.c_str());
        g_outcome.ladder = b;
        std::printf("capladder: result %s\n", b);
        std::fflush(stdout);
    }};
    // Step 5: the first attempt whose frontier died deepest, and the map it ran with.
    long long bestT = -1;
    int bestAttempt = 0;
    std::string bestMap;
    bool bestEnv = false;   // ...and whether it ran with --ladderback's envelope step
    const bool keep = !outPath.empty() && !g_check.enabled.load(std::memory_order_acquire);
    const std::string keptPlan = outPath + ".best";
    const std::string keptTrace = outPath + ".best.trace.csv";
    bool kept = false;
    int keptRc = 0;
    SearchOutcome keptOutcome;
    auto copyFile = [](const std::string& from, const std::string& to) {
        std::string bytes;
        if (!readFileBytes(from, bytes)) return false;
        std::ofstream o(to, std::ios::binary | std::ios::trunc);
        o.write(bytes.data(), (std::streamsize)bytes.size());
        return (bool)o;
    };
    struct DropKept {
        std::string a, b;
        ~DropKept() {
            std::remove(a.c_str());
            std::remove(b.c_str());
        }
    } dropKept{keptPlan, keptTrace};
    // The gamble (PlainElsewhere::ready): the plain search beside the ladder, taken now.
    auto gambleTakes = [&](int attempt) {
        if (!elsewhere.on || !g_plainElsewhere.ready || !g_plainElsewhere.ready(bestT))
            return false;
        std::printf("capladder: attempt=%d took the plain search beside it (the gamble; the "
                    "ladder had died at t=%lld at best)\n", attempt, bestT);
        std::fflush(stdout);
        elsewhere.taken = true;
        return true;
    };
    // --ladderprefix: the map the last attempt ran with and whether it ran with the envelope.
    std::string lpPrevMap;
    bool lpPrevEnv = false;
    auto capOfMap = [](const std::string& map, double x) {
        long long c = (long long)g_aliveCap;
        size_t q = 0;
        while (q < map.size()) {
            size_t e = map.find(',', q);
            if (e == std::string::npos) e = map.size();
            const std::string one = map.substr(q, e - q);
            const size_t k = one.find(':');
            if (k != std::string::npos && x >= std::atof(one.substr(0, k).c_str()))
                c = std::atoll(one.substr(k + 1).c_str());
            q = e + 1;
        }
        return c;
    };
    auto runAttempt = [&](const std::string& m) {
        g_lpc.resume = -1;
        if (lpLadder && !lpPrevMap.empty() && lpPrevEnv == envOn && g_lpc.usable) {
            long long tStar = g_lpc.t0 + 1 + (long long)g_lpc.layerX.size();
            for (size_t i = 0; i < g_lpc.layerX.size(); ++i)
                if (capOfMap(lpPrevMap, g_lpc.layerX[i]) != capOfMap(m, g_lpc.layerX[i])) {
                    tStar = g_lpc.t0 + 1 + (long long)i;
                    break;
                }
            for (int i = (int)g_lpc.snaps.size() - 1; i >= 0; --i)
                if (g_lpc.snaps[(size_t)i].T <= tStar - 1) { g_lpc.resume = i; break; }
        }
        g_lpc.active = lpLadder;
        lpPrevMap = m;
        lpPrevEnv = envOn;
        if (!(lpCheck && g_lpc.resume >= 0)) {
            const int rc = run(&m, nullptr);
            g_lpc.active = false;
            return rc;
        }
        // --ladderprefixcheck: the resumed attempt, its plan, trace and outcome kept aside...
        const int rcA = run(&m, nullptr);
        const long long fromT = g_lpc.resumedFrom;
        std::string planA, traceA;
        readFileBytes(outPath, planA);
        readFileBytes(outPath + ".trace.csv", traceA);
        const SearchOutcome oA = g_outcome;
        // ...then the same attempt from t0 (its saves replace the resumed one's), which is kept
        g_lpc.resume = -1;
        const int rcB = run(&m, nullptr);
        g_lpc.active = false;
        std::string planB, traceB;
        readFileBytes(outPath, planB);
        readFileBytes(outPath + ".trace.csv", traceB);
        const SearchOutcome& oB = g_outcome;
        std::string diff;
        if (rcA != rcB) diff += " rc";
        if (planA != planB) diff += " plan";
        if (traceA != traceB) diff += " trace";
        if (oA.verdict != oB.verdict) diff += " verdict";
        if (oA.deepT != oB.deepT) diff += " deepT";
        if (oA.deepX != oB.deepX) diff += " deepX";
        if (oA.capHits != oB.capHits) diff += " capHits";
        if (oA.workStates != oB.workStates) diff += " workStates";
        if (oA.planWritten != oB.planWritten) diff += " planWritten";
        std::printf("ladderprefixcheck: %s (resumed after t=%lld)%s\n", diff.empty() ? "same" : "MISMATCH",
                    fromT, diff.c_str());
        std::fflush(stdout);
        return rcB;
    };
    for (int attempt = 1;; ++attempt) {
        const std::string m = mapOf();
        long long maxCap = c0;
        for (const Window& w : wins) maxCap = std::max(maxCap, w.cap);
        const auto rungT0 = std::chrono::steady_clock::now();
        // (--resimladder holds: this attempt's verdict is the ladder's to read)
        const int rc = [&] {
            struct Hold {
                Hold() { g_resimLadderHold = true; }
                ~Hold() { g_resimLadderHold = false; }
            } hold;
            return runAttempt(m);
        }();
        const double rungMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rungT0).count();
        const int v = g_outcome.verdict;
        const double wx = g_outcome.deepX;
        rungNote("", rungMs, maxCap, g_outcome.capHits, g_outcome.deepT, g_outcome.workStates);
        std::printf("capladder: attempt=%d map=%s verdict=%d deepX=%.1f rc=%d ms=%.0f cap=%lld capHits=%lld\n",
                    attempt, m.c_str(), v, wx, rc, rungMs, maxCap, (long long)g_outcome.capHits);
        std::fflush(stdout);
        ++nAttempts;
        // The gamble's taking of the plain search (below): its plan is the answer.
        auto takeGamble = [&] {
            const int grc = g_plainElsewhere.take();
            plainHash = planHash();
            via = "gamble";
            return grc;
        };
        if (g_ladderStop.load()) {
            std::printf("capladder: attempt=%d stopped; took the plain search beside it (the "
                        "gamble; the ladder had died at t=%lld at best)\n", attempt, bestT);
            std::fflush(stdout);
            elsewhere.taken = true;
            return takeGamble();
        }
        if (g_outcome.capHits == 0) ++nIdle;
        if (fullCap < 0) fullCap = (long long)g_aliveCap;
        const bool died = frontierDied();
        if (!died || c0 >= fullCap) return rc;
        if (g_outcome.deepT > bestT) {
            bestT = g_outcome.deepT;
            bestAttempt = attempt;
            bestMap = m;
            bestEnv = envOn;
            kept = keep && g_outcome.planWritten && copyFile(outPath, keptPlan)
                   && copyFile(outPath + ".trace.csv", keptTrace);
            if (kept) {
                keptRc = rc;
                keptOutcome = g_outcome;
            }
            g_ladderBestT.store(bestT);
        }
        if (gambleTakes(attempt)) return takeGamble();
        Window* hit = nullptr;
        for (Window& w : wins)
            if (wx >= w.wall - w.lead && wx < w.wall + kLadderPast) hit = &w;
        bool plain = attempt >= kLadderMaxAttempts || g_outcome.capHits == 0;
        if (!plain) {
            if (envCoinPending) {
                envCoinPending = false;
                if (g_outcome.deepT <= envCoinT) envOn = false;
            } else if (!envOn && g_outcome.coinWall >= 0 && g_outcome.coinWall < 32
                       && !((envCoinTried >> g_outcome.coinWall) & 1u)) {
                envCoinTried |= 1u << g_outcome.coinWall;
                envOn = true;
                envCoinPending = true;
                envCoinT = g_outcome.deepT;
                continue;
            }
        }
        if (hit && g_ladderOverCap && hit->over > 0) {
            if (hit->over < 2) {
                hit->cap = fullCap * 4;
                ++hit->over;
                std::printf("capladder: attempt=%d window at x %.1f still inside; its cap to %lld"
                            " (--ladderovercap)\n", attempt + 1, hit->wall, hit->cap);
            } else {
                plain = true;
            }
            hit->lastT = std::max(hit->lastT, g_outcome.deepT);
        } else if (hit) {
            const bool canBack = hit->wall - hit->lead > 0.0;
            const bool canCap = hit->cap < fullCap;
            bool back = true;
            if (hit->stepped) {
                const bool moved = g_outcome.deepT > hit->lastT;
                if (moved) {
                    hit->backFailed = hit->capFailed = false;
                } else if (hit->lastBack) {
                    hit->backFailed = true;
                } else {
                    hit->capFailed = true;
                }
                back = moved ? hit->lastBack : !hit->lastBack;
            }
            if (back && !canBack) back = false;
            if (!back && !canCap) back = canBack;
            if ((!canBack && !canCap) || (hit->backFailed && hit->capFailed)) {
                if (g_ladderOverCap && hit->over < 2) {
                    // --ladderovercap: the window alone above --cap (x2, then x4)
                    hit->cap = fullCap * (hit->over == 0 ? 2 : 4);
                    ++hit->over;
                    hit->backFailed = hit->capFailed = false;
                    std::printf("capladder: attempt=%d window at x %.1f stuck at --cap; its cap to %lld"
                                " (--ladderovercap)\n", attempt + 1, hit->wall, hit->cap);
                } else {
                    plain = true;
                }
            } else if (back) {
                hit->lead *= 2;
            } else {
                hit->cap = std::min(hit->cap * 4, fullCap);
            }
            hit->stepped = true;
            hit->lastBack = back;
            hit->lastWx = wx;
            hit->lastT = g_outcome.deepT;
        } else if (hit && hit->cap >= fullCap) {
            plain = true;
        } else if (hit) {
            hit->cap = std::min(hit->cap * 4, fullCap);
            hit->lead *= 2;
        } else {
            wins.push_back({wx, kLadderLead, std::min(c0 * 4, fullCap), false, false, 0.0, -1, false,
                            false});
        }
        if (plain) {
            envOn = false;   // the plain search is the caller's own arguments
            std::printf("capladder: attempt=%d plain search at --cap %lld%s\n", attempt + 1,
                        fullCap, elsewhere.on ? " (started with the ladder)" : "");
            std::fflush(stdout);
            int prc;
            const auto plainT0 = std::chrono::steady_clock::now();
            if (elsewhere.on && !g_plainElsewhere.check) {
                elsewhere.taken = true;
                prc = g_plainElsewhere.take();
            } else {
                prc = run(nullptr, &noGrid);
                if (elsewhere.on) {
                    elsewhere.taken = true;
                    g_plainElsewhere.check(prc);
                }
            }
            rungNote("P", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - plainT0).count(), 0, 0, 0, 0);
            plainHash = planHash();
            via = "plain";
            // The other side may have raised it as it finished; step 5's search must not see it.
            g_ladderStop.store(false);
            const long long plainT = g_outcome.deepT;
            if (!frontierDied() || plainT >= bestT) return prc;
            via = "again";
            // (a kept PARTIAL was held from --resimladder; searched again, it is not)
            if (kept && !(g_resimLadder && g_resimLadderPartial && keptOutcome.verdict == VerdictPartial)) {
                std::remove(outPath.c_str());
                std::remove((outPath + ".trace.csv").c_str());
                if (copyFile(keptPlan, outPath) && copyFile(keptTrace, outPath + ".trace.csv")) {
                    g_outcome = keptOutcome;
                    std::printf("capladder: the plain search died at t=%lld, before attempt %d's"
                                " t=%lld - attempt %d's own result is the ladder's (kept)\n",
                                plainT, bestAttempt, bestT, bestAttempt);
                    std::fflush(stdout);
                    return keptRc;
                }
            }
            std::printf("capladder: the plain search died at t=%lld, before attempt %d's t=%lld"
                        " - searching attempt %d again for the result\n",
                        plainT, bestAttempt, bestT, bestAttempt);
            std::fflush(stdout);
            envOn = bestEnv;
            const auto againT0 = std::chrono::steady_clock::now();
            const int brc = run(&bestMap, nullptr);
            rungNote("A", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - againT0).count(), 0, 0, 0, 0);
            std::printf("capladder: attempt=%d again map=%s verdict=%d deepT=%lld (was %lld)"
                        " deepX=%.1f rc=%d\n", bestAttempt, bestMap.c_str(), g_outcome.verdict,
                        g_outcome.deepT, bestT, g_outcome.deepX, brc);
            std::fflush(stdout);
            return brc;
        }
    }
}
}  // namespace dp
