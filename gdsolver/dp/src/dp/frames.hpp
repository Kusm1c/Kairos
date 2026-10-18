#pragma once
#include "dp/object.hpp"
#include <atomic>

namespace dp {
struct RotTrig {
    double cx, cy;     // WORLD position of the trigger
    int frame;         // rot/90 mod 4
    int firedT = -1;
    int revT = -1;
    int setFlip = -1;
    int setRev = -1;
    int uid = -1;
    float vmodY = 1.0f;
    uint8_t ovrVel = 0;
    uint8_t dontSlide = 0;
    double rawRot = 0.0;
    uint8_t flipX = 0;
    int gndDir = 0;
};
inline std::vector<RotTrig> g_rotTrig;
inline long long g_rotWatchLo = -1, g_rotWatchHi = -1;
inline thread_local int g_rotWatchUid = -1;
inline long long g_qfoldLo = -1, g_qfoldHi = -1;
inline bool g_rotQToggle = false;
inline std::vector<uint32_t> g_rotQOff, g_rotQOn, g_rotTrigOff, g_rotTrigOn;
inline std::string g_touchSeedArg;
inline bool g_touchCensus = false;
inline thread_local int g_tcBranch = 0;
inline thread_local int g_tcBranchP1 = 0;
inline bool g_ceilPush = false;
inline bool g_dualBand = true;
inline thread_local bool g_bandPortalWrote = false;
inline thread_local int g_dualOtherMode = -1;
inline thread_local bool g_p2SoloHalf = false;
inline thread_local bool g_p2SoloFired = false;
inline bool g_dualSlide = true;
inline bool g_dualHover = true;
inline bool g_rotLatch = true;
inline bool g_stripVetoOrder = false;
inline bool g_ringSpendFlap = true;
inline bool g_ballPortalFlap = true;
inline bool g_bvar9Kill = true;
inline bool g_spiderRingFirst = true;
inline bool g_flipCeilBall = true;
inline std::atomic<int> g_flipLandDbgLeft{0};
constexpr double kWaveSlopeDescendDy = 1.0;
constexpr double kWaveFlipSlopeKillD = 5.845;
inline thread_local bool g_preBtnSet = false;
inline thread_local double g_preBtnY = 0.0;
inline thread_local bool g_ballTapped = false;
inline thread_local uint8_t g_hitGRepel = 0;

struct RotQEntry {
    int uid = -1;
    int id = 0;
    int chan = 0;     // m_channelValue: the bucket this object lives in
    int ord = 0;      // m_ordValue: the first sort key, stronger than position
    double px = 0.0, py = 0.0;   // the firing point: the LOAD position, frozen
    int swarm = 0;    // m_changeChannel: only these switch the active channel
    int swch = 0;     // m_targetChannelID
    int chanOnly = 0;
    int rotIdx = -1;  // index into g_rotTrig
};
inline std::vector<RotQEntry> g_rotQ;      // grouped by channel, sorted in it
inline std::array<int, 16> g_rotQBeg{};
inline std::array<int, 16> g_rotQEnd{};
inline std::array<uint32_t, 16> g_rotQChanMask{};
inline bool g_rotQueue = false;
inline int g_startRotChan = -1;
inline unsigned g_startRotRev = 0;
inline std::vector<int> g_startRotSpent;
inline int g_seedDump = -1;
inline int g_seedEvery = 0;
inline bool g_p2Touch = false;

inline std::string g_anchorState;
inline const char* const kAnchorKeys[] = {"owns", "touch", "portal", "portal2",
                                          "hist"};
inline bool g_ownsHist = false;
inline const char* const kHistNames[] = {"pressSpent", "pressSpent2"};
inline std::array<const char*, 13> histNamesFor(int version) {
    if (version == 6)
        return {"pressSpent", "pressSpent2", "freeMode", "spiderJumpT", "slopeOn",
                "slopeUnder", "slopeUid", "slopeAge", "slopeLanded", "dashing2", "dashRing2",
                "jumpBuf", "jumpBuf2"};
    if (version == 5)
        return {"pressSpent", "pressSpent2", "freeMode", "spiderJumpT", "slopeOn",
                "slopeUnder", "slopeUid", "slopeAge", "slopeLanded", "dashing2", "dashRing2",
                nullptr, nullptr};
    if (version == 4)
        return {"pressSpent", "pressSpent2", "freeMode", "spiderJumpT", "slopeOn",
                "slopeUnder", "slopeUid", "slopeAge", "slopeLanded", nullptr, nullptr,
                nullptr, nullptr};
    if (version == 3)
        return {"pressSpent", "pressSpent2", "freeMode", "spiderJumpT", nullptr,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    if (version == 2)
        return {"pressSpent", "pressSpent2", "freeMode", nullptr, nullptr,
                nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    return {kHistNames[0], kHistNames[1], nullptr, nullptr, nullptr,
            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
}
inline bool g_anchorRide = true;
inline bool g_ownsTouch = false;
inline bool g_ownsPortal = false;
inline bool g_seedPartialOk = false;
inline std::string g_seedPartial;   // what was missing, for the outcome line

inline bool parseAnchorState(const std::string& s,
                             std::vector<std::pair<std::string, std::string>>& out,
                             std::string& unknown) {
    size_t i = 0;
    while (i < s.size()) {
        size_t semi = s.find(';', i);
        if (semi == std::string::npos) semi = s.size();
        const std::string tok = s.substr(i, semi - i);
        i = semi + 1;
        if (tok.empty()) continue;
        const size_t eq = tok.find('=');
        if (eq == std::string::npos) { unknown = tok; return false; }
        const std::string k = tok.substr(0, eq);
        bool known = false;
        for (const char* kk : kAnchorKeys) if (k == kk) { known = true; break; }
        if (!known) { unknown = k; return false; }
        out.emplace_back(k, tok.substr(eq + 1));
    }
    return true;
}
// `uid:tick,uid:tick` -> pairs. Uids, not bit indices: see g_anchorState.
inline std::vector<std::pair<int, int>> parseTouchPayload(const std::string& v) {
    std::vector<std::pair<int, int>> out;
    size_t i = 0;
    while (i < v.size()) {
        size_t comma = v.find(',', i);
        if (comma == std::string::npos) comma = v.size();
        const std::string tok = v.substr(i, comma - i);
        i = comma + 1;
        const size_t colon = tok.find(':');
        if (colon == std::string::npos) continue;
        out.emplace_back(std::atoi(tok.substr(0, colon).c_str()),
                         std::atoi(tok.substr(colon + 1).c_str()));
    }
    return out;
}
inline std::vector<int> parseUidList(const std::string& v) {
    std::vector<int> out;
    size_t i = 0;
    while (i < v.size()) {
        size_t comma = v.find(',', i);
        if (comma == std::string::npos) comma = v.size();
        const std::string tok = v.substr(i, comma - i);
        i = comma + 1;
        if (!tok.empty()) out.push_back(std::atoi(tok.c_str()));
    }
    return out;
}
// std::popcount is C++20 and this tree builds as C++17.
inline int popCount32(uint32_t v) {
    int n = 0;
    while (v) { v &= v - 1; ++n; }
    return n;
}
inline int g_rotQChans = 0;                // how many channels actually appear

inline int rotQDirection(double rot, bool flipX) {
    const int r = ((int)rot) % 360;
    if (r == 0)                  return flipX ? 3 : 4;   // x desc : x asc
    if (r == 180 || r == -180)   return flipX ? 4 : 3;
    if (r == 90 || r == -270)    return flipX ? 1 : 2;   // y asc : y desc
    if (r == 270 || r == -90)    return flipX ? 2 : 1;
    return 4;
}

// The signed coordinate a bucket is ordered along.
inline double rotQAxis(int dir, double px, double py) {
    switch (dir) {
        case 1:  return  py;    // y ascending
        case 2:  return -py;    // y descending
        case 3:  return -px;    // x descending
        default: return  px;    // x ascending
    }
}

inline void buildRotQueue() {
    g_rotQBeg.fill(0);
    g_rotQEnd.fill(0);
    g_rotQChans = 0;
    if (g_rotQ.empty()) return;
    std::array<int, 17> dir{};
    dir.fill(0);
    std::vector<const RotQEntry*> byUid;
    byUid.reserve(g_rotQ.size());
    for (const RotQEntry& e : g_rotQ) byUid.push_back(&e);
    std::sort(byUid.begin(), byUid.end(),
              [](const RotQEntry* a, const RotQEntry* b) { return a->uid < b->uid; });
    for (const RotQEntry* e : byUid) {
        if (!e->swarm || e->swch < 0 || e->swch > 15) continue;
        if (dir[e->swch]) continue;                       // first one wins
        if (e->rotIdx < 0 || (size_t)e->rotIdx >= g_rotTrig.size()) continue;
        const RotTrig& rt = g_rotTrig[(size_t)e->rotIdx];
        dir[e->swch] = rotQDirection(rt.rawRot, rt.flipX != 0);
    }
    // Pass 2 + 3: bucket by channel, then sort inside each bucket.
    std::stable_sort(g_rotQ.begin(), g_rotQ.end(),
                     [](const RotQEntry& a, const RotQEntry& b) {
                         return a.chan < b.chan;
                     });
    size_t i = 0;
    while (i < g_rotQ.size()) {
        size_t j = i;
        const int ch = g_rotQ[i].chan;
        while (j < g_rotQ.size() && g_rotQ[j].chan == ch) ++j;
        const int d = (ch >= 0 && ch <= 15 && dir[ch]) ? dir[ch] : 4;
        std::sort(g_rotQ.begin() + (long long)i, g_rotQ.begin() + (long long)j,
                  [d](const RotQEntry& a, const RotQEntry& b) {
                      if (a.ord != b.ord) return a.ord < b.ord;
                      const int ka = (int)((float)a.ord
                                           + (float)rotQAxis(d, a.px, a.py));
                      const int kb = (int)((float)b.ord
                                           + (float)rotQAxis(d, b.px, b.py));
                      if (ka != kb) return ka < kb;
                      return a.uid < b.uid;
                  });
        if (ch >= 0 && ch <= 15) {
            g_rotQBeg[(size_t)ch] = (int)i;
            g_rotQEnd[(size_t)ch] = (int)j;
            uint32_t m = 0;
            for (size_t k = i; k < j && k < 32; ++k) m |= (uint32_t)1 << k;
            g_rotQChanMask[(size_t)ch] = m;
            ++g_rotQChans;
        }
        i = j;
    }
}

inline bool loadRotQueue(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::unordered_map<int, int> byUid;
    for (size_t k = 0; k < g_rotTrig.size(); ++k)
        byUid[g_rotTrig[k].uid] = (int)k;
    std::string line;
    std::getline(in, line);                       // header
    g_rotQ.clear();
    while (std::getline(in, line)) {
        RotQEntry e{};
        int sord = 0, sordd = 0, spx = 0, target = 0, chanChanged = 0;
        const int n = std::sscanf(
            line.c_str(), "%d,%d,%lf,%lf,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            &e.uid, &e.id, &e.px, &e.py, &e.chan, &e.ord, &sord, &sordd, &spx,
            &target, &chanChanged, &e.swarm, &e.chanOnly, &e.swch);
        static const std::string kAbsent = ",-,-,-";
        const bool absent = line.size() >= kAbsent.size()
                            && line.compare(line.size() - kAbsent.size(),
                                            kAbsent.size(), kAbsent) == 0;
        if (n == 11 && absent) {
            e.swarm = 0;
            e.chanOnly = 0;
            e.swch = -1;
        } else if (n < 14) {
            continue;
        }
        const auto it = byUid.find(e.uid);
        e.rotIdx = (it == byUid.end()) ? -1 : it->second;
        g_rotQ.push_back(e);
    }
    buildRotQueue();
    return true;
}
inline std::vector<int> g_spentRot;
inline std::vector<int> g_spentPad;
inline bool g_spentPadSeed = true;
inline bool g_spentPadHold = true;

inline std::vector<std::pair<long long, long long>> g_ctrlWin;

inline bool ctrlOffAt(long long t) {
    for (const auto& w : g_ctrlWin)
        if (t >= w.first && t <= w.second) return true;
    return false;
}
inline bool g_ctrlWinLag = true;
inline bool ctrlInputOffAt(long long t, bool heldBefore) {
    (void)heldBefore;
    if (!ctrlOffAt(t)) return false;
    return !g_ctrlWinLag || ctrlOffAt(t - 1);
}
inline std::vector<std::pair<long long, long long>> g_ctrlWin2;
inline bool g_ctrlWinP2 = true;
inline bool ctrlOff2At(long long t) {
    for (const auto& w : g_ctrlWin2)
        if (t >= w.first && t <= w.second) return true;
    return false;
}
inline bool ctrlBodyInputOffAt(long long t, bool secondHalf) {
    if (!(g_ctrlWinP2 && secondHalf)) return !g_ctrlWin.empty() && ctrlInputOffAt(t, false);
    if (!ctrlOff2At(t)) return false;
    return !g_ctrlWinLag || ctrlOff2At(t - 1);
}
inline std::vector<long long> g_winRePushJump;
inline bool rePushNoHoverAt(long long t) {
    for (long long v : g_winRePushJump)
        if (t == v) return true;
    return false;
}

inline void toFrame(int f, double X, double Y, double& u, double& v) {
    switch (f & 3) {
        case 0:  u =  X; v =  Y; break;
        case 1:  u = -Y; v =  X; break;   // rot 90:  travel -Y
        case 2:  u = -X; v = -Y; break;   // rot 180: travel -X (the reverse runs)
        default: u =  Y; v = -X; break;   // rot 270 (= -90): travel +Y
    }
}
inline void fromFrame(int f, double u, double v, double& X, double& Y) {
    switch (f & 3) {
        case 0:  X =  u; Y =  v; break;
        case 1:  X =  v; Y = -u; break;
        case 2:  X = -u; Y = -v; break;
        default: X = -v; Y =  u; break;
    }
}
inline bool g_dualUnlink = true;
struct UnlinkTrig {
    double cx;   // world x
    int8_t v;    // 1 unlinks, -1 links again
};
inline std::vector<UnlinkTrig> g_unlinkTrigs;   // sorted by cx at load
// Whether the pair is unlinked during a tick that starts at (u, v) in `frame`.
inline bool dualUnlinkedAt(int frame, double u, double v) {
    if (!g_dualUnlink || g_unlinkTrigs.empty()) return false;
    double X = u, Y = v;
    if (frame & 3) fromFrame(frame, u, v, X, Y);
    int8_t st = -1;
    for (const UnlinkTrig& t : g_unlinkTrigs) {
        if (t.cx > X) break;
        st = t.v;
    }
    return st == 1;
}
// The travel coordinate of a world point in this frame (what `cx` becomes).
inline double frameU(int f, double X, double Y) {
    double u, v;
    toFrame(f, X, Y, u, v);
    return u;
}
inline double frameV(int f, double X, double Y) {
    double u, v;
    toFrame(f, X, Y, u, v);
    return v;
}

inline void turnObj(Obj& o, int f) {
    if ((f & 3) == 0) return;
    const double cx = o.cx, cy = o.cy;
    double u, v;
    toFrame(f, cx, cy, u, v);
    // the slope's two surface points, in world, before the box moves
    const double sxL = cx - o.hw, sxR = cx + o.hw;
    const double syL = o.sy0, syR = o.sy1;
    o.cx = u; o.cy = v;
    if (f & 1) std::swap(o.hw, o.hh);
    o.rot += 90.0 * (f & 3);
    if (o.slope) {
        double uL, vL, uR, vR;
        toFrame(f, sxL, syL, uL, vL);
        toFrame(f, sxR, syR, uR, vR);
        if (uL <= uR) { o.sy0 = vL; o.sy1 = vR; }
        else          { o.sy0 = vR; o.sy1 = vL; }
    }
    if (o.tpY != 0.0) {
        double tu, tv;
        toFrame(f, cx, o.tpY, tu, tv);
        o.tpY = tv;
    }
    if (o.tpEx != 0.0 || o.tpEy != 0.0) {
        double eu, ev;
        toFrame(f, o.tpEx, o.tpEy, eu, ev);
        o.tpEx = eu; o.tpEy = ev;
    }
}
}  // namespace dp
