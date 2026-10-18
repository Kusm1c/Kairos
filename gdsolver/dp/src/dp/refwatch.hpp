#pragma once
#include <cstring>
#include <map>

#include "dp/frames.hpp"
#include "dp/state.hpp"

namespace dp {
struct RefRow {
    double y = 0.0, vy = 0.0, x = 0.0;
    int mode = -1;
    int act = -1;
    int flip = -1;
    int frame = -1;
    int dead = -1;   // the witness walk's own kill on this tick (1/0); -1 = no such column
    uint64_t key = 0;
    bool haveKey = false;
    bool have = false;
};

inline bool g_refWatch = false;
inline std::map<long long, RefRow> g_refRows;
inline double g_refEps = 0.01;
inline bool g_traceState = false;
inline double g_refDriftVy = 0.0;
inline double g_refDriftY = 0.0;
inline int g_refParent = -1;      // index into `cur` of the reference's parent
inline int g_refKidFate[2] = {-1, -1};   // -1 unknown, 0 not expanded, 1 died, 2 alive
inline const char* g_refKidWhy[2] = {"", ""};
inline uint64_t g_refKidKey[2] = {0, 0};
inline State g_refKidState[2]{};
inline long long g_refLostAt = -1;

inline bool g_rjOn = false;
inline std::map<long long, RefRow> g_rjRows;
inline long long g_rjAfter = -1;
inline long long g_rjFirst[3] = {-1, -1, -1};   // exact, near2, near8
inline int g_rjMode[3] = {-1, -1, -1};
inline long long g_rjLayers = 0, g_rjLayersNear[3] = {0, 0, 0};
inline uint32_t g_rjOldNode = 0xffffffffu;   // arena node of the old path at the death tick
inline bool g_rjUse = false;
inline long long g_rejoinProbe = 5;
inline thread_local bool t_rejoinRetry = false;   // the retry's own call (not reset per invocation)
inline bool g_rjFull = false;   // --rejoinfull: an exact rejoin also needs the dedupe key to match
inline long long g_rjJoinT = -1;

// Read the model's own trace of the reference replay.
inline bool loadTraceInto(const char* path, std::map<long long, RefRow>& rows);
inline bool loadRefTrace(const char* path) { return loadTraceInto(path, g_refRows); }
inline bool loadTraceInto(const char* path, std::map<long long, RefRow>& rows) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    if (!std::getline(f, line)) return false;   // header
    std::vector<std::string> cols;
    {
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) cols.push_back(c);
    }
    auto col = [&](const char* name) -> int {
        for (size_t i = 0; i < cols.size(); ++i)
            if (cols[i] == name) return (int)i;
        return -1;
    };
    const int cT = col("tick"), cX = col("x"), cY = col("y"),
              cV = col("vy"), cM = col("mode"), cA = col("act"),
              cF = col("flip"), cFr = col("frame"), cD = col("dead"), cK = col("key");
    // --tracestate's columns, when the trace has them: the state's own vy and flip
    const int cSv = col("svy"), cSf = col("sflip");
    if (cT < 0 || cY < 0 || cV < 0) return false;
    while (std::getline(f, line)) {
        std::vector<std::string> v;
        std::stringstream ss(line);
        std::string c;
        while (std::getline(ss, c, ',')) v.push_back(c);
        if ((int)v.size() <= cT) continue;
        RefRow r;
        r.have = true;
        r.y = std::atof(v[(size_t)cY].c_str());
        r.vy = std::atof(v[(size_t)cV].c_str());
        r.x = cX >= 0 && (int)v.size() > cX ? std::atof(v[(size_t)cX].c_str()) : 0.0;
        r.mode = cM >= 0 && (int)v.size() > cM ? std::atoi(v[(size_t)cM].c_str()) : -1;
        r.act = cA >= 0 && (int)v.size() > cA ? std::atoi(v[(size_t)cA].c_str()) : -1;
        r.flip = cF >= 0 && (int)v.size() > cF ? std::atoi(v[(size_t)cF].c_str()) : -1;
        r.frame = cFr >= 0 && (int)v.size() > cFr ? std::atoi(v[(size_t)cFr].c_str()) : -1;
        r.dead = cD >= 0 && (int)v.size() > cD ? std::atoi(v[(size_t)cD].c_str()) : -1;
        if (cSv >= 0 && (int)v.size() > cSv) r.vy = std::atof(v[(size_t)cSv].c_str());
        if (cSf >= 0 && (int)v.size() > cSf) r.flip = std::atoi(v[(size_t)cSf].c_str());
        if (cK >= 0 && (int)v.size() > cK && !v[(size_t)cK].empty()) {
            r.key = std::strtoull(v[(size_t)cK].c_str(), nullptr, 10);
            r.haveKey = true;
        }
        rows[std::atoll(v[(size_t)cT].c_str())] = r;
    }
    return !rows.empty();
}

inline void refWorldOf(const State& s, double& wy, double& wvy) {
    wy = (double)s.y;
    wvy = (double)s.vy;
    if (s.frame & 3) {
        double wx = 0.0;
        fromFrame((int)s.frame, (double)s.xAbs, (double)s.y, wx, wy);
        if (s.frame == 3) wvy = -wvy;
    }
}

inline bool refMatches(const State& s, const RefRow& r) {
    if (!r.have) return false;
    if (r.mode >= 0 && (int)s.mode != r.mode) return false;
    if (r.flip >= 0 && (int)s.flip != r.flip) return false;
    if (r.frame >= 0 && (int)s.frame != r.frame) return false;
    double wy = (double)s.y, wvy = (double)s.vy;
    if (r.frame < 0) refWorldOf(s, wy, wvy);
    return std::fabs(wy - r.y) <= g_refEps
        && std::fabs(wvy - r.vy) <= g_refEps;
}

constexpr long long kRjWindow = 3000;
constexpr int kRjWalks = 4;
constexpr long long kRjMinTail = 600;   // --rejoinuse: ticks the old plan must go on past a join
// Returns the index in `v` of the first exact rejoin found on this layer, -1 if none.
template <class FromOld, class SameKey>
inline int rejoinLayer(const std::vector<State>& v, long long t, FromOld fromOld, SameKey sameKey) {
    if (!g_rjOn || t <= g_rjAfter || t > g_rjAfter + kRjWindow) return -1;
    const auto it = g_rjRows.find(t);
    if (it == g_rjRows.end()) return -1;
    int exactIdx = -1;
    const RefRow& r = it->second;
    ++g_rjLayers;
    int walks[3] = {0, 0, 0};
    bool hit[3] = {false, false, false};
    for (size_t si = 0; si < v.size(); ++si) {
        const State& s = v[si];
        if (r.mode >= 0 && (int)s.mode != r.mode) continue;
        if (r.flip >= 0 && (int)s.flip != r.flip) continue;
        if (r.frame >= 0 && (int)s.frame != r.frame) continue;
        double wy = (double)s.y, wvy = (double)s.vy;
        if (r.frame < 0) refWorldOf(s, wy, wvy);
        const double dy = std::fabs(wy - r.y), dv = std::fabs(wvy - r.vy);
        const bool in[3] = {dy <= g_refEps && dv <= g_refEps && sameKey(s, r),
                            dy <= 2.0 && dv <= 0.5, dy <= 8.0 && dv <= 2.0};
        int k = 0;
        while (k < 3 && !in[k]) ++k;          // the tightest tier this state is in
        if (k == 3 || hit[k] || walks[k] >= kRjWalks) continue;
        ++walks[k];
        if (fromOld(s)) continue;
        for (int j = k; j < 3; ++j) hit[j] = true;
        if (hit[0]) { exactIdx = (int)si; break; }
    }
    for (int k = 0; k < 3; ++k)
        if (hit[k]) {
            ++g_rjLayersNear[k];
            if (g_rjFirst[k] < 0) { g_rjFirst[k] = t; g_rjMode[k] = r.mode; }
        }
    return exactIdx;
}

inline int refFind(const std::vector<State>& v, const RefRow& r) {
    for (size_t i = 0; i < v.size(); ++i)
        if (refMatches(v[i], r)) return (int)i;
    return -1;
}

inline bool refSameState(const State& a, const State& b) {
    State x = a, y = b;
    x.parent = y.parent = 0;
    x.action = y.action = 0;
    return std::memcmp(&x, &y, sizeof(State)) == 0;
}

inline int refFindExact(const std::vector<State>& v, const State& s) {
    for (size_t i = 0; i < v.size(); ++i)
        if (refSameState(v[i], s)) return (int)i;
    return -1;
}

template <class KeyFn>
inline int refNearestInCell(const std::vector<State>& v, uint64_t key,
                            const State& want, KeyFn keyFn) {
    int best = -1;
    double bd = 1e18;
    for (size_t i = 0; i < v.size(); ++i) {
        if (keyFn(v[i]) != key) continue;
        const double d = std::fabs((double)v[i].y - (double)want.y)
                       + std::fabs((double)v[i].vy - (double)want.vy);
        if (d < bd) { bd = d; best = (int)i; }
    }
    return best;
}
}  // namespace dp
