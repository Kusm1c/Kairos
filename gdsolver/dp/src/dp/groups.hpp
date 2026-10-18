#pragma once
#include "dp/stairs.hpp"

namespace dp {
using GroupTimeline = std::unordered_map<int, std::vector<DynSample>>;

inline bool readFileBytes(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const std::streamoff n = in.tellg();
    if (n < 0) return false;
    in.seekg(0, std::ios::beg);
    out.resize((size_t)n);
    if (n > 0 && !in.read(&out[0], n)) return false;
    return true;
}

inline int envTwinUid(int uid) { return kEnvTwinUidBase + uid; }
inline std::unordered_map<int, uint8_t> g_groupInit;

namespace groupscan {
inline bool blank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
inline bool digit(char c) { return c >= '0' && c <= '9'; }

inline bool scanInt(const char*& p, const char* e, int& out) {
    const char* q = p;
    while (q < e && blank(*q)) ++q;
    bool neg = false;
    if (q < e && (*q == '+' || *q == '-')) neg = *q++ == '-';
    if (q >= e || !digit(*q)) return false;
    long long v = 0;
    for (; q < e && digit(*q); ++q)
        if (v < 100000000000LL) v = v * 10 + (*q - '0');
    out = (int)(neg ? -v : v);
    p = q;
    return true;
}

inline bool scanFloat(const char*& p, const char* e, float& out) {
    static const double kPow10[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8};
    const char* q = p;
    while (q < e && blank(*q)) ++q;
    const char* s = q;
    bool neg = false;
    if (q < e && (*q == '+' || *q == '-')) neg = *q++ == '-';
    unsigned long long m = 0;
    int nInt = 0, nFrac = 0;
    for (; q < e && digit(*q); ++q, ++nInt)
        if (nInt < 18) m = m * 10 + (unsigned long long)(*q - '0');
    if (q < e && *q == '.') {
        ++q;
        for (; q < e && digit(*q); ++q, ++nFrac)
            if (nInt + nFrac < 18) m = m * 10 + (unsigned long long)(*q - '0');
    }
    const bool letterNext = q < e && ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'));
    if (nInt + nFrac > 0 && nInt + nFrac <= 15 && nFrac <= 8 && !letterNext) {
        const double d = (double)m / kPow10[nFrac];
        out = (float)(neg ? -d : d);
        p = q;
        return true;
    }
    // strtof skips blanks itself, and from the line's end that would read the next line's number.
    if (s >= e) return false;
    char* end = nullptr;
    const float v = std::strtof(s, &end);
    if (end == s) return false;
    out = v;
    p = end;
    return true;
}

// A literal in the format: must be the very next character.
inline bool scanChar(const char*& p, const char* e, char c) {
    if (p >= e || *p != c) return false;
    ++p;
    return true;
}
}  // namespace groupscan

inline GroupTimeline parseGroupTimeline(const std::string& bytes, const std::string& path,
                                        long long* endOut = nullptr,
                                        std::unordered_map<int, uint8_t>* initOut = nullptr) {
    using namespace groupscan;
    if (endOut) *endOut = -1;
    if (initOut) initOut->clear();
    GroupTimeline g;
    const char* p = bytes.data();
    const char* const end = p + bytes.size();
    // Lines as getline on a text-mode stream gives them: split at '\n', a '\r' before it dropped.
    auto nextLine = [&](const char*& b, const char*& e) {
        if (p >= end) return false;
        b = p;
        const char* nl = static_cast<const char*>(std::memchr(p, '\n', (size_t)(end - p)));
        e = nl ? nl : end;
        p = nl ? nl + 1 : end;
        if (nl && e > b && e[-1] == '\r') --e;
        return true;
    };
    const char* b = nullptr;
    const char* e = nullptr;
    nextLine(b, e);   // header
    long long rows = 0;
    while (nextLine(b, e)) {
        int t = 0, uid = 0, on = 1, env = 0;
        float cx = 0, cy = 0, w = 0, h = 0, rot = 0;
        const size_t len = (size_t)(e - b);
        if (len >= 4 && !std::memcmp(b, "end,", 4)) {
            if (endOut) *endOut = std::atoll(std::string(b + 4, e).c_str());
            continue;
        }
        if (len >= 5 && !std::memcmp(b, "init,", 5)) {
            const char* q = b + 5;
            int u = 0, o = 1;
            if (initOut && scanInt(q, e, u) && scanChar(q, e, ',') && scanInt(q, e, o))
                (*initOut)[u] = o ? 1 : 0;
            continue;
        }
        const char* q = b;
        int n = 0;
        if (scanInt(q, e, t) && ++n && scanChar(q, e, ',') && scanInt(q, e, uid) && ++n
            && scanChar(q, e, ',') && scanFloat(q, e, cx) && ++n
            && scanChar(q, e, ',') && scanFloat(q, e, cy) && ++n
            && scanChar(q, e, ',') && scanFloat(q, e, w) && ++n
            && scanChar(q, e, ',') && scanFloat(q, e, h) && ++n
            && scanChar(q, e, ',') && scanInt(q, e, on) && ++n
            && scanChar(q, e, ',') && scanFloat(q, e, rot) && ++n
            && scanChar(q, e, ',') && scanInt(q, e, env))
            ++n;
        if (n < 6) continue;
        g[uid].push_back({t, cx, cy, w * 0.5f, h * 0.5f,
                          (uint8_t)(on ? 1 : 0), rot, (uint8_t)(env ? 1 : 0)});
        ++rows;
    }
    for (auto& kv : g)
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const DynSample& a, const DynSample& b2) { return a.t < b2.t; });
    std::printf("groups: %lld samples for %zu objects (%s)\n", rows, g.size(),
                path.c_str());
    return g;
}

inline GroupTimeline loadGroupTimeline(const std::string& path,
                                       long long* endOut = nullptr,
                                       std::unordered_map<int, uint8_t>* initOut = nullptr) {
    std::string bytes;
    if (!readFileBytes(path, bytes)) {
        if (endOut) *endOut = -1;
        if (initOut) initOut->clear();
        std::fprintf(stderr, "groups: cannot open %s\n", path.c_str());
        return GroupTimeline{};
    }
    return parseGroupTimeline(bytes, path, endOut, initOut);
}

inline long long g_groupsEndT = -1;

constexpr int kBootAlignWin = 60;      // ticks before the seam that are compared
constexpr int kBootAlignMaxShift = 240;
constexpr double kBootAlignMinTravel = 3.0;   // px inside the window
constexpr double kBootAlignTol = 0.05;        // px, mean |dcx| + |dcy|

inline int bootAlignShift(const std::vector<DynSample>& live,
                          const std::vector<DynSample>& boot, int cover,
                          bool* moving = nullptr, double* bestErrOut = nullptr,
                          int* nFit = nullptr) {
    std::vector<const DynSample*> win;
    for (const DynSample& s : live)
        if (s.t > cover - kBootAlignWin && s.t <= cover) win.push_back(&s);
    if (win.size() < 10 || boot.empty()) return 0;
    double travel = 0.0;
    for (const DynSample* s : win)
        travel = std::max(travel, std::fabs((double)s->cx - win.front()->cx)
                                      + std::fabs((double)s->cy - win.front()->cy));
    if (travel < kBootAlignMinTravel) return 0;
    if (moving) *moving = true;
    auto at = [&](int t) -> const DynSample* {
        const auto it = std::lower_bound(
            boot.begin(), boot.end(), t,
            [](const DynSample& a, int v) { return a.t < v; });
        return (it != boot.end() && it->t == t) ? &*it : nullptr;
    };
    int best = 0;
    double bestErr = 1e18;
    for (int k = 0; k <= 2 * kBootAlignMaxShift; ++k) {
        const int s = (k & 1) ? (k + 1) / 2 : -(k / 2);
        double err = 0.0;
        size_t n = 0;
        for (const DynSample* w : win) {
            const DynSample* b = at(w->t + s);
            if (!b) continue;
            err += std::fabs((double)b->cx - w->cx) + std::fabs((double)b->cy - w->cy);
            ++n;
        }
        if (n * 5 < win.size() * 4) continue;    // at least 80% of the window
        err /= (double)n;
        if (err < kBootAlignTol && nFit) ++*nFit;
        if (err < bestErr - 1e-9) { bestErr = err; best = s; }
    }
    if (bestErrOut) *bestErrOut = bestErr;
    return bestErr < kBootAlignTol ? best : 0;
}

inline bool g_groupHoldDeath = false;

inline void overlayGroupTimeline(GroupTimeline& base, const GroupTimeline& over,
                                 bool deathEnd = false, long long overEnd = -1,
                                 std::string* report = nullptr) {
    int cover = -1;
    for (const auto& kv : over)
        if (!kv.second.empty()) cover = std::max(cover, kv.second.back().t);
    if (cover < 0) return;
    if (overEnd > cover) cover = (int)overEnd;
    int nRetimed = 0, sLo = 0, sHi = 0;
    int nMoving = 0, nNoFit = 0, nMulti = 0, mostFits = 0, nHeld = 0;
    double worstJoin = 0.0;
    for (const auto& kv : over) {
        std::vector<DynSample> merged = kv.second;
        auto it = base.find(kv.first);
        if (it != base.end()) {
            bool moving = false;
            double bestErr = 0.0;
            int nFit = 0;
            const int s = bootAlignShift(kv.second, it->second, cover, &moving, &bestErr,
                                         &nFit);
            if (moving) {
                ++nMoving;
                if (nFit == 0) ++nNoFit;
                else worstJoin = std::max(worstJoin, bestErr);
                if (nFit > 1) ++nMulti;
                mostFits = std::max(mostFits, nFit);
            }
            if (s != 0) {
                sLo = nRetimed ? std::min(sLo, s) : s;
                sHi = nRetimed ? std::max(sHi, s) : s;
                ++nRetimed;
            }
            const int hold = (deathEnd && s == 0) ? 1 : 0;
            if (hold > 0) ++nHeld;
            for (const DynSample& b : it->second) {
                DynSample c = b;
                c.t -= s;
                if (c.t > cover + hold) merged.push_back(c);
            }
        }
        std::sort(merged.begin(), merged.end(),
                  [](const DynSample& a, const DynSample& b) { return a.t < b.t; });
        base[kv.first] = std::move(merged);
    }
    char b[512];
    std::string lines;
    std::snprintf(b, sizeof b, "groups: overlay covers t<=%d\n", cover);
    lines += b;
    std::snprintf(b, sizeof b, "groups: the bootstrap joined %d moving object(s) at a shift "
                  "of %d..%d ticks; %d moving at the seam, %d with no fitting "
                  "shift, worst fitting error %.4f px, %d with more than one "
                  "fitting shift (at most %d)\n", nRetimed, sLo, sHi, nMoving,
                  nNoFit, worstJoin, nMulti, mostFits);
    lines += b;
    if (deathEnd) {
        std::snprintf(b, sizeof b, "groups: --groupholddeath: the last recording ended on a "
                      "death at t=%d; %d object(s) hold its last row one tick\n", cover, nHeld);
        lines += b;
    }
    if (report) *report += lines;
    else std::fputs(lines.c_str(), stdout);
}
}  // namespace dp
