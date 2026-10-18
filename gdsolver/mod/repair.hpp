#pragma once
#include <chrono>
#include <ctime>         // memCensus's wall clock
#include <mutex>         // the checkpoint death handover (g_ckMx)
#include <map>           // cfgDiffLine
#include <set>
#include <sstream>
#include <thread>        // the solver worker
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h>   // _mm_getcsr: see the fpenv line in logSolverArgs
#endif
#include "dp/band_rows.hpp"   // dp_band.txt's rows, shared with dptest (addWorldArgs)
#include "mod/session.hpp"

namespace kp { void onLimitsDropped(const std::string& where); }

namespace p1 {
struct AnchorRow {
    bool valid = false;
    float x = 0.f, y = 0.f, vy = 0.f;
    int mode = 0;            // modeIdx() -- the ordering leveldp uses
    int onGround = 0, onGround2 = 0;
    int flip = 0;            // m_isUpsideDown
    int mini = 0;            // vehicle size < 0.9 (a size portal has been passed)
    int dual = 0;
    float y2 = 0.f, v2 = 0.f;
    float x2 = 0.f;
    int f2 = 0, g2 = 0, g2b = 0;
    int m2 = -1, mini2 = -1;
    int dashing = 0;
    float dashSlope = 0.f;   // dy per px of x, from the ring's own rotation (0 for an upright one)
    int dashing2 = 0;
    int dashRing2 = -1;      // the uid of the ring p2 dashes from, -1 for none
    float rot = 0.f;
    float rot2 = 0.f;        // ...and the second body's (the dump's p2rot; cfg dpanchorrot2)
    float speed = 0.f;       // GD's own speed multiplier, so the model need not re-derive it
    int gframe = 0;          // rotated gameplay (0/1/2/3 = 0/90/180/270)
    int snapUid = -1;        // m_objectSnappedTo's uid, and how far it snapped
    float snapDist = 0.f;
    float pmin = 0.f, pmax = 0.f;   // GD's flight band (getMinPortalY / getMaxPortalY)
    int bandCam = -1;
    int freeMode = 0;
    int boost = 0;
    int ctrlOff = 0;
    int ctrlOff2 = -1;
    int b985 = 0, b986 = 0, b985_2 = -1, b986_2 = -1;
    int coins = 0;
    int item1 = -1, cnt1 = 0, item2 = -1, cnt2 = 0;
    int onSlope = 0, slopeUnder = 0, slopeUid = -1;
    double slopeStart = 0.0, totalTime = 0.0;
    double spiderStamp = 0.0;
};

inline float g_maxPlayYLive = 0.f;

inline AnchorRow g_spawnRow;

namespace anchors {
inline std::vector<AnchorRow> g_live;
inline std::vector<AnchorRow> g_dead;
inline std::vector<AnchorRow> g_deepest;
inline std::vector<std::vector<solver::CoinPos>> g_deepestCoinPos;   // see keepAsDeepest
inline const std::vector<AnchorRow>* g_src = &g_dead;

struct Seeds {
    std::unordered_map<int, int> pads;                // padseed::g_first
    std::vector<std::pair<int, int>> rings;           // ringseed::g_fired
    std::unordered_map<int, int> touch;               // touchseed::g_first
    std::unordered_map<int, int> portal, portal2;     // portalseed::g_first / g_first2
    std::unordered_map<int, int> touchFire;           // touchfire::g_first (--rectouch)
    std::vector<std::pair<int, int>> rings2;          // ringseed::g_fired2 (p2's rings, --spentorb2)
};
inline Seeds g_seedsDead;
inline Seeds g_seedsDeepest;
inline const Seeds& seeds() { return g_src == &g_deepest ? g_seedsDeepest : g_seedsDead; }
inline long long g_deadSerial = -1;
inline long long g_deepestSerial = -1;
inline long long serial() { return g_src == &g_deepest ? g_deepestSerial : g_deadSerial; }
struct ProvLink { long long from; long long src; };
inline std::unordered_map<long long, ProvLink> g_provChain;
// The attempt whose world attempt `s` holds at tick t.
inline long long provOrigin(long long s, long long t) {
    for (int guard = 0; guard < 256; ++guard) {
        const auto it = g_provChain.find(s);
        if (it == g_provChain.end() || t > it->second.from) return s;
        s = it->second.src;
    }
    return s;
}

inline void onAttemptStart() { g_live.clear(); }
inline void reset() {
    g_live.clear();
    g_dead.clear();
    g_deepest.clear();
    g_deepestCoinPos.clear();
    g_src = &g_dead;
    g_seedsDead = Seeds{};
    g_seedsDeepest = Seeds{};
    g_deadSerial = -1;
    g_deepestSerial = -1;
    g_provChain.clear();
}
// The bytes the row buffers hold allocated (capacities), for releaseSolveMemory's line.
inline size_t heldBytes() {
    size_t n = (g_live.capacity() + g_dead.capacity() + g_deepest.capacity()) * sizeof(AnchorRow);
    for (const auto& v : g_deepestCoinPos) n += v.capacity() * sizeof(solver::CoinPos);
    return n + g_deepestCoinPos.capacity() * sizeof(std::vector<solver::CoinPos>);
}
inline void release() {
    reset();
    { decltype(g_live) e; g_live.swap(e); }
    { decltype(g_dead) e; g_dead.swap(e); }
    { decltype(g_deepest) e; g_deepest.swap(e); }
    { decltype(g_deepestCoinPos) e; g_deepestCoinPos.swap(e); }
    { decltype(g_provChain) e; g_provChain.swap(e); }
}

// Hand the finished attempt to the solver side.
inline void bank() {
    g_dead.swap(g_live);
    g_live.clear();
    g_seedsDead = Seeds{padseed::g_first, ringseed::g_fired, touchseed::g_first,
                        portalseed::g_first, portalseed::g_first2, touchfire::g_first, ringseed::g_fired2};
    g_deadSerial = g_attemptSerial;   // still the ending attempt's: the next one counts it up
}

inline void keepAsDeepest() {
    g_deepest = g_dead;
    g_deepestCoinPos = solver::g_coinPosLog;
    route::g_onTickDeepest = route::g_onTick;   // cfg routeprereq: its switches, same attempt
    g_seedsDeepest = g_seedsDead;
    g_deepestSerial = g_deadSerial;
}

inline void ladderOn(bool useDeepest) { g_src = useDeepest ? &g_deepest : &g_dead; }

inline const AnchorRow* row(long long t) {
    const std::vector<AnchorRow>& v = *g_src;
    if (t < 0 || (size_t)t >= v.size()) return nullptr;
    const AnchorRow& r = v[(size_t)t];
    return r.valid ? &r : nullptr;
}

// How many ticks the current source holds (for walks over the whole recording).
inline long long depth() { return (long long)g_src->size(); }

inline void slopeTrace(GJBaseGameLayer* l, long long t) {
    if (!l || !l->m_player1 || t < g_slopeT0 || t > g_slopeT1) return;
    auto const* pb = reinterpret_cast<uint8_t const*>(l->m_player1);
    auto* ramp = *reinterpret_cast<GameObject* const*>(pb + gdoff::kPlayerCurrentSlope);
    const double st = *reinterpret_cast<double const*>(pb + gdoff::kPlayerSlopeStartTime);
    const double tt = *reinterpret_cast<double const*>(pb + gdoff::kPlayerTotalTime);
    char b[256];
    snprintf(b, sizeof(b),
             "slprec: att=%d t=%lld onslp=%d under=%d uid=%d slpst=%.6f ttime=%.6f age=%.3f "
             "og=%d y=%.4f vy=%.4f",
             g_attempt, t, pb[gdoff::kPlayerOnSlope] ? 1 : 0, pb[gdoff::kPlayerUpsideDownSlope] ? 1 : 0,
             ramp ? ramp->m_uniqueID : -1, st, tt,
             (tt - st) * 240.0, l->m_player1->m_isOnGround ? 1 : 0,
             (double)l->m_player1->getPositionY(), (double)l->m_player1->m_yVelocity);
    writeResult(b);
}

inline void record(GJBaseGameLayer* l, long long t) {
    if (!l || !l->m_player1 || t < 0) return;
    if (t > 400000) return;    // a runaway attempt must not eat memory instead of ending
    if ((size_t)t >= g_live.size()) g_live.resize((size_t)t + 1);
    AnchorRow& r = g_live[(size_t)t];
    auto* p = l->m_player1;
    const auto pos = p->getPosition();
    r.valid = true;
    r.x = pos.x;
    r.y = pos.y;
    r.vy = (float)p->m_yVelocity;
    r.mode = modeIdx(p);
    r.onGround = p->m_isOnGround ? 1 : 0;
    r.onGround2 = p->m_isOnGround2 ? 1 : 0;
    r.flip = p->m_isUpsideDown ? 1 : 0;
    r.mini = (p->m_vehicleSize < 0.9f) ? 1 : 0;
    r.dual = l->m_gameState.m_isDualMode ? 1 : 0;
    r.y2 = (r.dual && l->m_player2) ? l->m_player2->getPositionY() : 0.f;
    r.x2 = (r.dual && l->m_player2) ? l->m_player2->getPositionX() : 0.f;
    r.v2 = (r.dual && l->m_player2) ? (float)l->m_player2->m_yVelocity : 0.f;
    r.f2 = (r.dual && l->m_player2 && l->m_player2->m_isUpsideDown) ? 1 : 0;
    r.g2 = (r.dual && l->m_player2 && l->m_player2->m_isOnGround) ? 1 : 0;
    r.g2b = (r.dual && l->m_player2 && l->m_player2->m_isOnGround2) ? 1 : 0;
    r.m2 = (r.dual && l->m_player2) ? modeIdx(l->m_player2) : -1;
    r.mini2 = (r.dual && l->m_player2)
                  ? ((l->m_player2->m_vehicleSize < 0.9f) ? 1 : 0) : -1;
    r.dashing = p->m_isDashing ? 1 : 0;
    r.dashSlope = 0.f;
    if (r.dashing && p->m_dashRing) {
        const double rot = reinterpret_cast<GameObject*>(p->m_dashRing)->getRotation();
        r.dashSlope = (float)std::tan(-rot * 3.14159265358979 / 180.0);
    }
    if (r.dual && l->m_player2 && l->m_player2->m_isDashing) {
        r.dashing2 = 1;
        r.dashRing2 = l->m_player2->m_dashRing
                          ? reinterpret_cast<GameObject*>(l->m_player2->m_dashRing)->m_uniqueID
                          : -1;
    }
    r.rot = p->getRotation();   // the dump's `rot` column, from the same call
    r.rot2 = (r.dual && l->m_player2) ? l->m_player2->getRotation() : 0.f;   // `p2rot`
    r.speed = (float)p->m_playerSpeed;
    r.gframe = g_gameFrame;
    r.snapUid = p->m_objectSnappedTo ? p->m_objectSnappedTo->m_uniqueID : -1;
    r.snapDist = p->m_snapDistance;
    r.pmin = l->getMinPortalY();
    r.pmax = l->getMaxPortalY();
    r.bandCam = (*reinterpret_cast<const float*>(reinterpret_cast<const char*>(l)
                                                 + gdoff::kLayerUnkPoint17Y) != 0.f
                 && !*(reinterpret_cast<uint8_t const*>(l) + gdoff::kLayerUnkBool1)) ? 1 : 0;
    r.freeMode = *(reinterpret_cast<uint8_t const*>(l) + gdoff::kLayerFreeMode) ? 1 : 0;
    r.boost = *(reinterpret_cast<uint8_t const*>(p) + gdoff::kPlayerAccelerating) ? 1 : 0;
    r.ctrlOff = p->m_controlsDisabled ? 1 : 0;
    r.ctrlOff2 = (r.dual && l->m_player2) ? (l->m_player2->m_controlsDisabled ? 1 : 0) : -1;
    r.b985 = *(reinterpret_cast<uint8_t const*>(p) + gdoff::kPlayerJumpBuffered) ? 1 : 0;
    r.b986 = *(reinterpret_cast<uint8_t const*>(p) + gdoff::kPlayerStateRingJump) ? 1 : 0;
    if (r.dual && l->m_player2) {
        auto const* q = reinterpret_cast<uint8_t const*>(l->m_player2);
        r.b985_2 = q[gdoff::kPlayerJumpBuffered] ? 1 : 0;
        r.b986_2 = q[gdoff::kPlayerStateRingJump] ? 1 : 0;
    } else {
        r.b985_2 = r.b986_2 = -1;
    }
    // The slope ride (see AnchorRow::onSlope). Raw offsets, like the press latch.
    {
        auto const* pb = reinterpret_cast<uint8_t const*>(p);
        r.onSlope = pb[gdoff::kPlayerOnSlope] ? 1 : 0;
        r.slopeUnder = pb[gdoff::kPlayerUpsideDownSlope] ? 1 : 0;
        auto* ramp = *reinterpret_cast<GameObject* const*>(pb + gdoff::kPlayerCurrentSlope);
        r.slopeUid = ramp ? ramp->m_uniqueID : -1;
        r.slopeStart = *reinterpret_cast<double const*>(pb + gdoff::kPlayerSlopeStartTime);
        r.totalTime = *reinterpret_cast<double const*>(pb + gdoff::kPlayerTotalTime);
        r.spiderStamp = *reinterpret_cast<double const*>(pb + gdoff::kPlayerLastSpiderFlipTime);
    }
    g_maxPlayYLive = *reinterpret_cast<float const*>(
        reinterpret_cast<char const*>(l) + gdoff::kLayerMaxGameplayY);
    r.coins = 0;
    r.item1 = r.item2 = -1;
    r.cnt1 = r.cnt2 = 0;
    if (g_cfg.coinRoute || g_cfg.worldGates) {   // (the counters are the gates' too)
        if (g_cfg.coinRoute)
            for (size_t i = 0; i < solver::g_coinGdTick.size() && i < 8; ++i)
                if (solver::g_coinGdTick[i] >= 0 && solver::g_coinGdTick[i] <= t)
                    r.coins |= 1 << i;
        int n = 0;
        for (const auto& kv : solver::g_itemCounts) {
            if (n == 0) { r.item1 = kv.first; r.cnt1 = kv.second; }
            else if (n == 1) { r.item2 = kv.first; r.cnt2 = kv.second; }
            else break;
            ++n;
        }
    }
    if (t == 1 && !g_spawnRow.valid) g_spawnRow = r;   // see g_spawnRow
}
}  // namespace anchors
}  // namespace p1
#include "mod/cp_flight.hpp"   // follows the anchors' buffers (cfg cpflight)
#include "mod/seek_cp.hpp"     // the seek bar's checkpoints (cfg seekcp), on cp_flight's records
namespace p1 {
struct RotObj { int uid; double cx, cy; };
inline std::vector<RotObj> g_rotObjs;   // parsed from the level csv at session start

struct TouchTog { int uid; double cx, cy, hw, hh; };
inline std::vector<TouchTog> g_touchToggles;
// Every touch-triggered object (touch=1, any id), for --touchentered.
inline std::vector<TouchTog> g_touchBoxes;
// cfg dpseedpickups: every pickup (objrects type 30), for --touchseed beside the touch Toggles.
inline std::vector<TouchTog> g_touchPickups;

inline void loadRotObjs(const std::string& csv) {
    g_rotObjs.clear();
    g_touchToggles.clear();
    g_touchBoxes.clear();
    g_touchPickups.clear();
    std::istringstream in(csv);
    std::string line;
    std::getline(in, line);   // header: id,type,cx,cy,...,uid,...
    while (std::getline(in, line)) {
        const bool rot = line.rfind("2900,", 0) == 0;
        const bool tog = line.rfind("1049,", 0) == 0;
        {
            // Every touch-triggered object: split once, read cx/cy/w/h/uid/touch.
            double f[44] = {};
            int col = 0;
            size_t p = 0;
            while (p <= line.size() && col < 44) {
                size_t q = line.find(',', p);
                if (q == std::string::npos) q = line.size();
                f[col] = std::atof(line.substr(p, q - p).c_str());
                p = q + 1;
                ++col;
            }
            if (col == 44 && (int)f[43] == 1 && (int)f[7] > 0)
                g_touchBoxes.push_back({(int)f[7], f[2], f[3], f[4] * 0.5, f[5] * 0.5});
            if (col == 44 && (int)f[1] == 30 && (int)f[7] > 0)
                g_touchPickups.push_back({(int)f[7], f[2], f[3], f[4] * 0.5, f[5] * 0.5});
        }
        if (!rot && !tog) continue;
        RotObj o{};
        double w = 0.0, h = 0.0;
        int touch = 0;
        int col = 0;
        size_t p = 0;
        while (p <= line.size() && col < 44) {
            size_t q = line.find(',', p);
            if (q == std::string::npos) q = line.size();
            const std::string f = line.substr(p, q - p);
            if (col == 2) o.cx = std::atof(f.c_str());
            else if (col == 3) o.cy = std::atof(f.c_str());
            else if (col == 4) w = std::atof(f.c_str());
            else if (col == 5) h = std::atof(f.c_str());
            else if (col == 7) o.uid = std::atoi(f.c_str());
            else if (col == 43) touch = std::atoi(f.c_str());
            p = q + 1;
            ++col;
        }
        if (o.uid <= 0) continue;
        if (rot) g_rotObjs.push_back(o);
        else if (touch == 1) g_touchToggles.push_back({o.uid, o.cx, o.cy, w * 0.5, h * 0.5});
    }
}

inline std::string touchEnteredArg(long long t0) {
    std::string out;
    for (const TouchTog& T : g_touchBoxes) {
        for (long long t = 1; t <= t0; ++t) {
            const AnchorRow* r = anchors::row(t);
            if (!r) continue;
            const double half = (r->mode == 4) ? (r->mini ? 2.0 : 5.0)
                              : (r->mode == 6) ? (r->mini ? 8.1 : 13.5)
                                               : (r->mini ? 9.0 : 15.0);
            if (std::fabs((double)r->x - T.cx) < T.hw + half
                && std::fabs((double)r->y - T.cy) < T.hh + half) {
                if (!out.empty()) out += ",";
                out += std::to_string(T.uid);
                out += ":" + std::to_string(t);
                break;
            }
        }
    }
    return out.empty() ? std::string("-") : out;
}

inline bool solverSlamBox() {
    bool on = true;
    for (const auto& a : g_cfg.dpArgs) {
        if (a == "--slambox") on = true;
        else if (a == "--no-slambox") on = false;
    }
    return on;
}
inline bool solverFormulaRec() {
    bool on = true;
    for (const auto& a : g_cfg.dpArgs) {
        if (a == "--formularec") on = true;
        else if (a == "--no-formularec") on = false;
    }
    return on;
}

inline std::string recTouchArg() {
    std::vector<std::pair<int, int>> out;
    for (const auto& kv : anchors::seeds().touchFire) {
        int date = -1;
        for (const TouchTog& T : g_touchBoxes) {
            if (T.uid != kv.first) continue;
            const long long n = anchors::depth();
            for (long long t = 1; t < n && date < 0; ++t) {
                const AnchorRow* r = anchors::row(t);
                if (!r) continue;
                const double half = (r->mode == 4) ? (r->mini ? 2.0 : 5.0)
                                  : (r->mode == 6) ? (r->mini ? 8.1 : 13.5)
                                                   : (r->mini ? 9.0 : 15.0);
                if (std::fabs((double)r->x - T.cx) < T.hw + half
                    && std::fabs((double)r->y - T.cy) < T.hh + half)
                    date = (int)t;
            }
            break;
        }
        out.emplace_back(kv.first, date);
    }
    std::sort(out.begin(), out.end());
    std::string s;
    for (const auto& e : out)
        s += (s.empty() ? "" : ",") + std::to_string(e.first) + ":" + std::to_string(e.second);
    return s.empty() ? std::string("-") : s;
}

inline std::string spentPadArg(long long t0) {
    std::vector<int> uids;
    for (const auto& kv : anchors::seeds().pads)
        if (kv.second < t0) uids.push_back(kv.first);
    std::sort(uids.begin(), uids.end());
    std::string out;
    for (const int u : uids) out += (out.empty() ? "" : ",") + std::to_string(u);
    return out;
}

inline std::string spentOrbArg(long long t0) {
    std::string out;
    for (const auto& f : anchors::seeds().rings)
        if (f.second < t0) out += (out.empty() ? "" : ",") + std::to_string(f.first);
    return out;
}
inline bool solverSpentP2() {
    bool on = true;
    for (const auto& a : g_cfg.dpArgs) {
        if (a == "--spentp2") on = true;
        else if (a == "--no-spentp2") on = false;
    }
    return on;
}
inline std::string spentOrb2Arg(long long t0) {
    std::string out;
    for (const auto& f : anchors::seeds().rings2)
        if (f.second < t0) out += (out.empty() ? "" : ",") + std::to_string(f.first);
    return out;
}

inline std::string xtrackArg(long long t0) {
    const char* src = (anchors::g_src == &anchors::g_deepest) ? "deep" : "dead";
    const std::string path = std::string(DATA_DIR) + "/dp_xtrack_" + src + "_"
                             + std::to_string(t0) + ".txt";
    std::string body = "tick,x\n";
    long long n = 0;
    char b[64];
    for (long long t = 1; t <= t0; ++t) {
        const AnchorRow* r = anchors::row(t);
        if (!r) continue;
        snprintf(b, sizeof(b), "%lld,%.4f\n", t, (double)r->x);
        body += b;
        ++n;
    }
    if (n == 0) return "";
    const std::string tmp = path + ".tmp" + std::to_string(
        (unsigned long long)std::hash<std::thread::id>{}(std::this_thread::get_id()));
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f << body;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return std::filesystem::exists(path) ? path : "";
    }
    return path;
}

inline std::string touchSeedArg(long long t0) {
    std::string out;
    std::vector<const TouchTog*> list;
    for (const TouchTog& T : g_touchToggles) list.push_back(&T);
    if (g_cfg.dpSeedPickups)
        for (const TouchTog& T : g_touchPickups) list.push_back(&T);
    for (const TouchTog* TP : list) {
        const TouchTog& T = *TP;
        for (long long t = 1; t <= t0; ++t) {
            const AnchorRow* r = anchors::row(t);
            if (!r || r->gframe != 0) continue;
            const AnchorRow* pr = anchors::row(t - 1);
            const double half = (r->mode == 4) ? (r->mini ? 2.0 : 5.0)
                              : (r->mode == 6) ? (r->mini ? 8.1 : 13.5)
                                               : (r->mini ? 9.0 : 15.0);
            const bool inX = std::fabs((double)r->x - T.cx) < T.hw + half;
            const bool inY = std::fabs((double)r->y - T.cy) < T.hh + half
                             || (!g_cfg.dpTouchSeedNow && pr
                                 && std::fabs((double)pr->y - T.cy) < T.hh + half);
            if (inX && inY) {
                if (!out.empty()) out += ",";
                out += std::to_string(T.uid) + ":" + std::to_string(t);
                break;
            }
        }
    }
    return out;
}

inline std::string spentRotArg(long long t0) {
    if (g_rotObjs.empty()) return "";
    struct Pt { int uid; double cx, cy; };
    std::vector<Pt> pts;
    {
        const std::string path = std::string(DATA_DIR) + "/rotgameplay.txt";
        std::ifstream f(path);
        if (!f) {
            log::warn("spentrot: {} missing -- no 2900 can be witnessed, none marked", path);
            return "";
        }
        std::string line;
        std::getline(f, line);   // uid,id,cx,cy,chan,ord,sord,sordd,spx,target,chanChanged,swarm,chanOnly,swch
        while (std::getline(f, line)) {
            std::vector<std::string> c;
            std::stringstream ss(line);
            for (std::string v; std::getline(ss, v, ',');) c.push_back(v);
            if (c.size() < 14 || c[1] != "2900" || c[12] != "0") continue;
            pts.push_back({std::atoi(c[0].c_str()), std::atof(c[2].c_str()),
                           std::atof(c[3].c_str())});
        }
    }
    std::set<int> spent;
    int pf = -1;
    for (long long t = 1; t <= t0; ++t) {
        const AnchorRow* r = anchors::row(t);
        if (!r) continue;
        if (pf >= 0 && r->gframe != pf)
            for (const Pt& p : pts) {
                const double d = (pf & 1) ? std::fabs(p.cy - (double)r->y)
                                          : std::fabs(p.cx - (double)r->x);
                if (d <= 2.0) spent.insert(p.uid);
            }
        pf = r->gframe;
    }
    std::string s;
    for (int uid : spent) {
        if (!s.empty()) s += ",";
        s += std::to_string(uid);
    }
    return s;
}

namespace dpsolve {
constexpr int kBackOff = 6;
// The rungs the ladder walks when the shallow anchor is doomed. Same list as the driver's.
constexpr int kRungs[] = {24, 96, 240, 600, 1500, 3600, 7200, 14400, 28800};
constexpr long long kCap = 2000;
constexpr double kYq = 0.25, kVq = 1.0;
inline std::string solverThreads() {
    int n = (int)Mod::get()->getSettingValue<int64_t>("solver-threads");
    if (n <= 0) n = std::clamp((int)std::thread::hardware_concurrency() / 2, 2, 16);
    return std::to_string(n);
}
constexpr long long kFineCap = 50000;
constexpr double kFineYq = 2.0, kFineVq = 10.0;
constexpr long long kFineLead = 200;
constexpr int kFineTriesPerLadder = 4;
constexpr long long kCapTiers[] = {16000, 40000};
constexpr double kTierYq = 0.5, kTierVq = 2.5;
inline int g_capTier = 0;    // 0 = the base setting
inline int g_topTierIter = 0;  // the round the ladder reached its last tier (cfg dptopstop)

// ---- the job in flight ----
inline std::atomic<bool> g_running{false};   // a solve has been started and not yet collected
inline std::atomic<bool> g_finished{false};  // the worker thread has set g_rc
inline std::atomic<int> g_generation{0};
inline int g_resultGeneration = -1;          // set by the worker before g_finished, like g_rc
inline bool jobInFlight() { return g_running.load() && !g_finished.load(); }
inline GJBaseGameLayer* g_pendingLayer = nullptr;
inline int g_rc = -1;
inline std::string g_planPath;
inline std::string g_tailPath;
inline std::chrono::steady_clock::time_point g_t0;
inline std::string g_csv;          // the level, built once at session start
inline std::string g_csvSig;
inline std::string bytesSig(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (const char c : s) {
        h ^= (uint8_t)c;
        h *= 1099511628211ULL;
    }
    char out[48];
    snprintf(out, sizeof(out), "%zu/%08x", s.size(), (unsigned)(h & 0xffffffffULL));
    return out;
}
inline int g_horizon = 0;
inline bool g_argsLogged = false;  // the `solver args:` line has been printed for THIS session
inline std::string g_argsLast;     // ...and what it said, so a CHANGE gets its own line

inline bool g_ckFlying = false;          // an attempt flying a checkpoint is in the air
inline bool g_ckInstalled = false;       // ...and g_cfg.inputs is that flight, not g_plan
inline unsigned long long g_ckCall = 0;  // the solve call that published it
inline size_t g_ckIndex = 0;             // ...its position in that call's order
inline long long g_ckT0 = -1;            // the anchor it was searched from
inline long long g_ckEnd = -1;           // the last tick its inputs cover
inline std::vector<InputCmd> g_ckPlan;   // exactly what GD was handed
inline std::vector<InputCmd> g_ckBase;
inline size_t g_ckFlights = 0, g_ckPassed = 0, g_ckDeaths = 0;   // per job, for the summary line
struct CkDeath {
    bool ready = false;
    unsigned long long call = 0;
    size_t index = 0;
    long long t0 = 0, end = 0, deathT = 0;
    float deathX = 0.f;
    std::vector<InputCmd> plan;
    std::vector<AnchorRow> rows;
};
inline std::mutex g_ckMx;                // guards g_ckDeath and g_ckDeaf
inline CkDeath g_ckDeath;
inline std::set<long long> g_ckDeaf;
struct CkObs {
    bool have = false;
    size_t index = 0;
    long long t0 = -1, end = -1, deathT = -1;
    std::vector<InputCmd> plan;   // exactly what GD was handed for that flight
    double atSec = 0.0;           // seconds into the job when the death was seen (print only)
    bool compared = false, same = false;
};
inline CkObs g_ckObs;
inline size_t g_ckObsSkipped = 0;   // checkpoints passed unflown after the first death
inline bool g_ckInner = false;
// ---- what the loop knows between iterations ----
inline int g_iter = 0;
inline std::vector<InputCmd> g_plan;      // the plan currently installed
inline std::vector<InputCmd> g_best;      // the deepest plan GD has verified
inline std::vector<InputCmd> g_flownPlan;
inline long long g_bestDeath = -1;        // ...and how far it got
inline int g_curBackoff = kBackOff;
inline bool g_lastTailSolved = false;     // the tail spliced last time reached the end
inline bool g_planClaimsGoal = false;
inline int g_followSolved = 0;            // regressions followed on a solved branch
inline long long g_followPeak = -1;
inline int g_followForced = 0;            // ...and on the forced (portal) route -- see the grace
inline long long g_lastDeath = -1;
inline float g_lastDeathX = 0.f;      // ...and where (the void-attempt repeat scoring)
inline bool g_coinMissPending = false;
inline bool g_lastDeathCoinMiss = false;
inline bool g_coinMissPostPending = false;
inline int g_postMissCoin = -1;
inline int g_routeNeedUid = -1;
inline int g_routeNeedDeath = -1;   // the box the death being scored was ranked at (-1 = none)
inline uint32_t g_routeEngaged = 0;
inline int g_routeRounds[8] = {0, 0, 0, 0, 0, 0, 0, 0};
inline bool routeEngaged(size_t ci) {
    return g_cfg.routePrereqAfter <= 0 || (ci < 32 && ((g_routeEngaged >> ci) & 1));
}
inline long long g_postMissCrossT = -1;
inline long long firstFinalCrossing(const std::vector<AnchorRow>& rows,
                                    const std::vector<solver::CoinPos>& track, long long upto) {
    if (track.empty()) return -1;
    const solver::CoinPos fin = track.back();
    if (!fin.on) return -1;
    size_t k = 0;
    float px = 0.f;
    bool have = false;
    for (size_t i = 0; i < rows.size() && (long long)i < upto; ++i) {
        while (k + 1 < track.size() && track[k + 1].t <= (long long)i) ++k;
        const AnchorRow& r = rows[i];
        if (!r.valid) { have = false; continue; }
        const bool atFinal = track[k].t <= (long long)i && track[k].on && track[k].x == fin.x
                             && track[k].y == fin.y;
        if (have && atFinal && (px - fin.x) * (r.x - fin.x) <= 0.f && px != r.x) return (long long)i;
        px = r.x;
        have = true;
    }
    return -1;
}
inline int g_postCoins = 0;
inline long long g_postCoinT[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
inline long long g_lastDeathPhys = -1;
inline double g_coinMarginNow = 0.0;
constexpr double kCoinMarginStep = 2.0, kCoinMarginMax = 8.0;
inline std::map<long long, int> g_deathRuns;
constexpr int kPhantomAfter = 4;          // hits at one site before the box is dropped
constexpr double kPhantomDx = 4.0;        // veto box half-width (px)
constexpr double kPhantomDy = 10.0;       // ...and half-height
constexpr int kPhantomMaxBands = 64;
inline std::map<long long, int> g_phantomHits;    // site (x/8) -> deaths of a SOLVED tail
inline std::map<long long, int> g_phantomScale;
constexpr int kPhantomScaleMax = 2;
inline std::vector<std::string> g_phantomBands;   // --deadband strings, in the order found
// cfg voidband (checkVoidRun): the x spans of objects reaching below kVoidTop, read from g_csv once a session
inline std::vector<std::pair<float, float>> g_voidLow;
inline bool g_voidLowRead = false;
// ...and the last run it closed: the death tick, and where the run began (-1 = none this round)
inline long long g_voidDt = -1, g_voidT0 = -1;
inline float g_voidX0 = 0.f;
inline void voidDeeper(const char* how);
inline bool g_phantomLifted = false;      // released once; never veto again after that
inline int g_killVetoIter = -1;
inline bool g_stop = false;               // the loop has given up; do not start another job
// What the worker thread produced (read on the main thread after g_finished)
inline bool g_haveNewPlan = false;
inline long long g_anchorT = -1;
inline bool g_phaseLabelsLive = false;
inline float g_anchorX = 0.f;
inline bool g_showRequest = false;
enum RecordKind { RecNone = 0, RecBootstrap = 1, RecDeep = 2 };
inline int g_recordKind = RecNone;
inline bool g_recordRequest = false;
inline bool g_deepActive = false;
inline bool g_recordAttempt = false;
inline long long g_deepDoneAt = -1;   // the depth the last one was taken at
inline long long g_deepStartTick = 0;
inline std::string g_groupsBootPath;
// Defined below, next to the deep record it shares its machinery with; start() needs it first.
inline bool startBootstrapRecord();
// Defined below, next to beginFirstAttempt; spawn()'s first solve needs it first.
inline void addFirstStart(std::vector<std::string>& a);
inline bool g_coldRestarted = false;  // the prefix has been thrown away once
inline int g_escalations = 0;         // how many times the ladder has been given another option

inline std::string planFnv(const std::vector<InputCmd>& p);
inline std::string fileSig(const std::string& path);
inline std::unordered_map<std::string, std::string> g_sigKnown;
inline bool copyRecording(const std::string& src, const std::string& dst);

inline std::string num(double v) {
    char b[40];
    snprintf(b, sizeof(b), "%.9g", v);
    return b;
}

inline std::string g_fixupPath;      // declared here; the recorder below fills it

inline std::unordered_set<long long> g_spentAnchors;
inline long long g_learnResetWall = -1;
inline int g_learnResets = 0;
inline unsigned long long g_needTrigDropped = 0;   // wide: see SolveOutcome::resimTrig
inline unsigned long long g_needTrigSuspect = 0;   // wide: see SolveOutcome::resimTrig
inline int g_stallRuns = 0;          // iterations since the run last got deeper
inline long long g_kLimitWall = -1;     // the wall the round without them is for, -1 = none
inline long long g_kLimitTested = -1;   // the last wall tested, once each
inline long long g_kHumanFrom = 0;      // the limits hold from this tick on

// dp --startages: ticks since the plan's last change and last press before t0, as of t0 - 1.
inline std::string kStartAges(const std::vector<InputCmd>& plan, long long t0) {
    long long edge = -1000, press = -1000;
    bool down = false;
    for (const InputCmd& c : plan) {
        if (c.step >= t0) break;
        if (c.down == down) continue;
        down = c.down;
        edge = c.step;
        if (down) press = c.step;
    }
    return std::to_string(std::min(254LL, t0 - 1 - edge)) + "," + std::to_string(std::min(254LL, t0 - 1 - press));
}
inline bool g_needUnseen = false;    // go and touch doors whose effect has not been seen
inline int g_horizonFull = 0;        // a plan that covers the level
inline int g_horizonNow = 0;         // ...and what is being asked for right now
inline bool g_triedWholeLevel = false;   // this wall's one whole-level escalation (step mode)
// cfg dpfastveto: the plan the last round flew and the tick it died on.
inline std::string g_prevFlownFnv;
inline bool g_roundRepeated = false;
inline int g_wallRepeats = 0;   // cfg dpadaptivehorizon: stops in a row at one wall
inline long long g_prevFlownDeath = -1;
inline std::set<std::pair<std::string, long long>> g_flownDeaths;

constexpr long long kAutoCreep = 30;
inline float g_bestDeathX = 0.f;         // where the deepest death was
inline long long g_autoWall = -1;        // the deepest death the counts below began at
inline int g_autoWallRounds = 0;         // rounds since then (the dpsecstall signal)
inline long long g_autoHead = -1;        // earliest real fixup recorded for a death at the wall
inline int g_autoNoRec = 0;              // deaths at the wall in a row the recorder wrote nothing for
inline int g_autoRecNow = 0;             // real records in the recorder run in progress
inline long long g_autoTriedWall = -1;   // the wall the last rung was fired at
inline int g_autoTries = 0;              // ...and how many it has had (autoWindow's levels)
inline long long g_autoLastT0 = -1;      // ...and where the last of them began
inline int g_autoCapTier = 0;
inline bool g_autoLastDrawn = false;
inline bool g_autoLastFound = true;
inline bool g_autoLastCapBound = false;
inline bool g_autoPinOut = false;       // runLadder: every rung past the pin is spent
inline int g_autoChain = 0;              // walls in a row right behind the pin (dpsecchain)
inline bool g_autoRecordThenRung = false;
inline std::string g_autoRecordWhy;
inline double workDpState() { return g_cfg.dpSecStatePrice; }
inline double workDpCall() { return g_cfg.dpSecCallPrice; }
constexpr double kWorkRecTick = 1500.0;    // per model tick the fixup recorder replays (926-4605)
constexpr double kWorkReplayTick = 80.0;   // per tick GD flies (47-128)
constexpr double kWorkSecStep = 300.0;     // per section-search step on psnap (253-387)
constexpr double kWorkSecStepCp = 1100.0;  // ...and on the checkpoint path (140 s / 129,581)
inline double g_autoWallWork = 0.0;        // the rent paid at the wall
inline double g_autoRoundWork = 0.0;       // the round in progress: its job's searches and
                                           // recorder (worker thread), then its flight (onDeath)
inline double g_autoRungWork = 0.0;        // what this run's rungs have cost
inline int g_autoRungN = 0;
inline double g_secEpisodeWork = 0.0;
inline long long g_secEpisodeReach = -1;   // the furthest wall / pin the episode's rungs reached
inline int g_secEpisodeRungs = 0;
inline double autoRungEstimate() {
    return g_autoRungN > 0 ? g_autoRungWork / g_autoRungN : 1e6 * (double)g_cfg.dpSecRungPrior;
}

inline long long g_coinWall = -1;
inline int g_coinWallCoin = -1;
inline std::vector<InputCmd> g_coinWallPlan;
inline long long rungWall() {
    return (g_cfg.dpSecCoinRung && g_cfg.coinRoute && g_coinWall >= 0) ? g_coinWall : g_bestDeath;
}

inline bool autoSync() {
    const long long wall = rungWall();
    if (g_autoWall < 0 || wall < g_autoWall || wall >= g_autoWall + kAutoCreep) {
        g_autoWall = wall;
        g_autoWallRounds = 0;
        g_autoWallWork = 0.0;
        g_autoHead = -1;
        g_autoNoRec = 0;
        return true;
    }
    return false;
}

inline void autoRungDone(long long headTick, long long steps, bool onSnap, bool found,
                         bool capBound) {
    const double work = kWorkReplayTick * (double)std::max(0LL, headTick)
                        + (onSnap ? kWorkSecStep : kWorkSecStepCp) * (double)steps;
    g_autoRungWork += work;
    ++g_autoRungN;
    g_secEpisodeWork += work;   // cfg dpsecwallbudget
    ++g_secEpisodeRungs;
    g_autoLastFound = found;
    g_autoLastCapBound = capBound;
    g_autoRoundWork = 0.0;
    g_autoWallWork = 0.0;
    g_autoWallRounds = 0;
}

inline bool secRungPrefixFailed(long long died, float x, const char* how) {
    if (!g_secRung || !secsolve::g_on || g_ckpt) return false;
    if (g_secRungFailAttempt == (long long)g_attempt) return false;   // this attempt is counted
    g_secRungFailAttempt = (long long)g_attempt;
    ++g_secRungPrefixFails;
    char b[256];
    snprintf(b, sizeof(b), "secrung: the prefix %s at t=%lld x=%.1f, before the head t=%d (try %d of "
             "%d)", how, died, (double)x, g_cfg.checkpointAt, g_secRungPrefixFails,
             kSecRungPrefixTries);
    writeResult(b);
    if (g_secRungPrefixFails < kSecRungPrefixTries) return false;
    const int head = g_cfg.checkpointAt;
    g_secRung = false;
    if (g_secState) {   // cfg dpsecstate: the size is the rung's, not the next one's
        secsolve::g_forceP1Size = -1;
        secsolve::g_keepP1Size = -1;
        g_secState = 0;
    }
    secsolve::g_on = false;
    secsolve::g_rungCoin = -1;
    g_cfg.practiceAt = g_secSavePractice;
    g_cfg.checkpointAt = g_secSaveCkpt;
    g_cfg.fastloops = g_secSaveFastloops;
    g_cfg.maxAttempts = g_secSaveMaxAttempts;
    secsolve::g_targetDepth = g_secSaveTargetDepth;
    g_plan = g_cfg.inputs;          // the deepest plan the handoff installed (as when nothing is found)
    autoRungDone(std::max(0LL, died), 0, false, true, false);
    g_ckptTick = -1;
    g_headHeld = 0;
    g_practiceOn = false;
    g_stop = false;                 // the loop may spawn solves again
    g_paused = false;
    g_forceCleanStart = true;       // drop practice mode and checkpoints at the next reset
    g_hudPhase = "secrung: abandoned - the prefix does not reach the head";
    snprintf(b, sizeof(b), "secrung: abandoned - the prefix did not reach the head t=%d in %d tries; "
             "the loop carries on from the deepest plan", head, kSecRungPrefixTries);
    writeResult(b);
    secRenderRelease();
    return true;
}

constexpr int kStallToUnseen = 2;
constexpr int kStallToShorten = 3;
constexpr int kStallToCap = 5;
constexpr int kHorizonShort = 3000;
inline int horizonDefault() { return g_cfg.dpStepHorizon > 0 ? g_cfg.dpStepHorizon : g_horizonFull; }
inline int horizonShort() { return g_cfg.dpStepHorizon > 0 ? g_cfg.dpStepHorizon : kHorizonShort; }

inline std::vector<float> g_newGimmickX;     // sorted x of every such object in the level
inline bool g_horizonFullAsked = false;      // the whole level was asked for on purpose
inline double g_startX = 0.0;                // where the first solve starts
constexpr double kContentBack = 300.0;       // an object this far behind the anchor still counts
constexpr double kContentPxPerTick = 1.0;    // the slowest the player goes (speed 0.7: 1.01 px)
inline bool isGeometryTrigger(int id) {
    switch (id) {
        case 901: case 1346: case 1347: case 1814:     // move, rotate, follow, follow player y
        case 1049: case 1268: case 1616:               // toggle, spawn, stop
        case 1815: case 1595: case 1611: case 1811:    // collision, touch, count, instant count
        case 1817: case 1912: case 2066: case 2068:    // pickup, random, gravity, adv. random
        case 2900: case 1935: case 1917: case 1932:    // rotate gameplay, timewarp, reverse,
        case 1934:                                     // player control, song
            return true;
        default:
            return id >= 3006 && id <= 3033;           // area and keyframe triggers
    }
}
inline bool isNewGimmick(int id, int type) {
    if (type == 7 || id < 286) return false;
    switch (type) {
        case 25:                                       // slope
        case 23: case 24: case 26: case 27: case 33: case 41:   // dual/solo, wave, robot,
                                                                 // spider, swing portals
        case 28:                                       // teleport
        case 29: case 32: case 34: case 35: case 36:   // green, drop, red pad, red ring, custom
        case 37: case 38: case 43: case 44: case 46:   // dash, spider orb/pad, teleport orb
        case 39: case 40: case 45:                     // collision, special, area objects
            return true;
        case 20:
            return isGeometryTrigger(id);
        default:
            return false;                              // re-skins, collectibles, the look
    }
}
inline int horizonFor(double x0) {
    if (g_cfg.dpContentHorizon <= 0 || g_horizonNow != g_horizonFull || g_horizonFullAsked
        || std::isnan(x0))
        return g_horizonNow;
    if (g_cfg.dpContentHorizon == 2 && g_iter > 0) return g_horizonNow;
    auto it = std::lower_bound(g_newGimmickX.begin(), g_newGimmickX.end(),
                               (float)(x0 - kContentBack));
    if (it == g_newGimmickX.end()) return g_horizonFull;
    const double dx = std::max(0.0, (double)*it - x0);
    const long long h = (long long)(dx / kContentPxPerTick) + horizonShort();
    return (int)std::min<long long>(h, g_horizonFull);
}

constexpr float kOffBoard = 1200.f;

constexpr int kOffBoardTicks = 2;
constexpr float kOffBoardTpDy = 100.f;
inline long long offBoardTick(long long upTo, float& xAt) {
    bool armed = false;
    long long outT = -1;
    float outX = 0.f;
    bool haveY = false;
    float lastY = 0.f;
    bool haveBand = false;
    float lastLo = 0.f, lastHi = 0.f;
    bool lastFree = false;
    for (long long t = 1; t <= upTo; ++t) {
        const AnchorRow* r = anchors::row(t);
        if (!r) continue;
        if (g_cfg.offBoardTp && haveY && std::fabs(r->y - lastY) > kOffBoardTpDy) {
            armed = false;
            outT = -1;
        }
        lastY = r->y;
        haveY = true;
        if (g_cfg.offBoardBand) {
            const bool bandOk = r->pmax > r->pmin;
            if (haveBand && bandOk && !r->freeMode && !lastFree
                && (std::fabs(r->pmin - lastLo) > kOffBoardTpDy
                    || std::fabs(r->pmax - lastHi) > kOffBoardTpDy)) {
                armed = false;
                outT = -1;
            }
            haveBand = bandOk;
            lastLo = r->pmin;
            lastHi = r->pmax;
            lastFree = r->freeMode != 0;
        }
        if (r->pmax <= r->pmin) { outT = -1; continue; }
        if (g_cfg.offBoardFree && r->freeMode) { outT = -1; continue; }
        if (!armed) {
            if (r->y < r->pmin || r->y > r->pmax) continue;
            armed = true;
        }
        if (r->y < r->pmin - kOffBoard || r->y > r->pmax + kOffBoard) {
            if (outT < 0) { outT = t; outX = r->x; }
            if (t - outT + 1 >= kOffBoardTicks) {
                xAt = outX;
                return outT;
            }
        } else {
            outT = -1;
        }
    }
    return -1;
}

inline std::string g_groupsPath;
inline long long g_groupsDepth = -1;
inline long long g_groupsSerial = -1;
inline std::string recProvArg() {
    const long long rec = g_groupsSerial, anc = anchors::serial();
    if (g_cfg.recProvChain && rec >= 0 && anc >= 0 && rec != anc && g_groupsDepth >= 0) {
        std::vector<long long> ticks;
        for (long long s : {rec, anc})
            for (int guard = 0; guard < 256; ++guard) {
                const auto it = anchors::g_provChain.find(s);
                if (it == anchors::g_provChain.end()) break;
                ticks.push_back(it->second.from);
                s = it->second.src;
            }
        std::sort(ticks.begin(), ticks.end());
        long long agree = -1;   // the last tick of [0, agree] on which the two worlds are one
        long long lo = 0;
        for (const long long T : ticks) {
            if (T < lo) continue;
            if (anchors::provOrigin(rec, T) != anchors::provOrigin(anc, T)) break;
            agree = T;
            lo = T + 1;
        }
        if (agree == (ticks.empty() ? -1 : ticks.back()) && agree >= 0
            && anchors::provOrigin(rec, agree + 1) == anchors::provOrigin(anc, agree + 1))
            agree = g_groupsDepth;
        if (agree >= 0) {
            const long long reach = std::min(agree, g_groupsDepth);
            return std::to_string(rec) + "," + std::to_string(rec) + "," + std::to_string(reach);
        }
    }
    return std::to_string(rec) + "," + std::to_string(anc) + "," + std::to_string(g_groupsDepth);
}
inline std::string g_groupsCopiedSig;
inline std::map<std::string, std::string> g_fixinGroupsBySig;
inline bool g_fixinPending = false;
// The groups signature the died plan was solved against (the [inputs] line), for the manifest.
inline std::string g_solveGroupsSig;
inline std::vector<float> g_groupsX;
inline std::vector<float> g_groupsDeepX;
inline std::vector<float> g_groupsBootX;
inline int g_retimeLeftOut = 0;   // the layers addWorldArgs last left out (1 boot, 2 deep), logged on change
constexpr float kRetimeTolX = 2.f;
inline long long clockPart(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t t = 0; t < n; ++t)
        if (!std::isnan(a[t]) && !std::isnan(b[t]) && std::fabs(a[t] - b[t]) > kRetimeTolX)
            return (long long)t;
    return -1;
}
inline std::string g_groupsDeepPath;

inline bool rotQueueRequested() {
    for (const std::string& s : g_cfg.dpArgs)
        if (s == "--rotqueue") return true;
    return false;
}

struct RotQE {
    int ch = 0, uid = 0;
    double px = 0.0, py = 0.0;
    int swarm = 0, swch = -1, chanOnly = 0, gnddir = -1, id = 0;
};
inline std::vector<RotQE> g_rotQOrder;   // this level's queue in dp's order; empty = not read yet
inline std::set<int> g_rotSurvivors;     // level F's witnesses from earlier attempts

inline void rotSeedLevelReset() {
    g_rotQOrder.clear();
    g_rotSurvivors.clear();
}

inline bool rotQOrderReady() {
    if (!g_rotQOrder.empty()) return true;
    std::stringstream ss(dpbridge::outcome().rotQOrder);
    for (std::string item; std::getline(ss, item, ';');) {
        RotQE e;
        if (std::sscanf(item.c_str(), "%d,%d,%lf,%lf,%d,%d,%d,%d,%d", &e.ch, &e.uid, &e.px,
                        &e.py, &e.swarm, &e.swch, &e.chanOnly, &e.gnddir, &e.id) == 9
            && e.ch >= 0 && e.ch <= 15)
            g_rotQOrder.push_back(e);
    }
    return !g_rotQOrder.empty();
}

namespace rotseed {
inline bool visible(const RotQE& e) { return e.id == 2900 && !e.chanOnly; }

// GD's own firing test (checkSpawnObjects): the player's axis, the channel's direction.
inline bool passed(int gframe, double x, double y, const RotQE& e, bool rev) {
    const bool vertical = (gframe & 1) != 0;
    const double ref = vertical ? y : x;
    const double p = vertical ? e.py : e.px;
    return rev ? (ref <= p) : (p <= ref);
}

inline bool near2(const RotQE& e, const AnchorRow& r) {
    return std::min(std::fabs(e.px - (double)r.x), std::fabs(e.py - (double)r.y)) <= 2.0;
}

inline std::vector<std::vector<const RotQE*>> byChannel() {
    std::vector<std::vector<const RotQE*>> v(16);
    for (const RotQE& e : g_rotQOrder) v[(size_t)e.ch].push_back(&e);
    return v;
}

struct Chain {
    std::string fail;          // non-empty: the walk stopped here
    bool underivable = false;  // ...because the recording contradicts the chain
    int chan = 0;
    bool rev[16] = {};
    int ptr[16] = {};
    std::vector<int> spent;
    std::string prov;
    std::vector<std::pair<long long, int>> skipped;   // (tick of the visible fire, uid)
    std::vector<std::pair<long long, int>> timeline;  // (tick, active channel from then on)
};

inline Chain walk(const std::vector<AnchorRow>& v, long long tEnd,
                  const std::vector<std::vector<const RotQE*>>& q) {
    Chain w;
    w.timeline.push_back({0, 0});
    int pg = -1;
    for (long long t = 0; t < (long long)v.size() && t <= tEnd; ++t) {
        const AnchorRow& r = v[(size_t)t];
        if (!r.valid) continue;
        if (pg >= 0 && r.gframe != pg) {
            const std::vector<const RotQE*>& lst = q[(size_t)w.chan];
            int k = w.ptr[w.chan];
            std::vector<int> sk;
            while (k < (int)lst.size() && !visible(*lst[(size_t)k])) {
                if (lst[(size_t)k]->swarm) {
                    w.fail = "channel " + std::to_string(w.chan) + " switched by unseen uid "
                             + std::to_string(lst[(size_t)k]->uid) + " before t="
                             + std::to_string(t);
                    return w;
                }
                sk.push_back(lst[(size_t)k]->uid);
                ++k;
            }
            if (k >= (int)lst.size() || !near2(*lst[(size_t)k], r)) {
                w.fail = "gframe change at t=" + std::to_string(t) + " is not channel "
                         + std::to_string(w.chan) + "'s next visible entry";
                w.underivable = true;
                return w;
            }
            const RotQE& e = *lst[(size_t)k];
            for (int u : sk) {
                w.skipped.push_back({t, u});
                w.spent.push_back(u);
            }
            w.spent.push_back(e.uid);
            w.prov += (w.prov.empty() ? "" : ",") + std::to_string(t) + ":" + std::to_string(e.uid);
            w.ptr[w.chan] = k + 1;
            if (e.swarm && e.swch >= 0 && e.swch <= 15) {
                w.chan = e.swch;
                w.rev[e.swch] = (unsigned)(e.gnddir - 2) < 2u;
                w.timeline.push_back({t, w.chan});
            }
        }
        pg = r.gframe;
    }
    return w;
}

struct Crossing { long long t; int uid; bool ctrlChanged; };

inline std::vector<Crossing> crossings2899(const std::vector<AnchorRow>& v) {
    std::vector<long long> chg;
    const AnchorRow* prev = nullptr;
    for (long long t = 0; t < (long long)v.size(); ++t) {
        const AnchorRow& r = v[(size_t)t];
        if (!r.valid) continue;
        if (prev && r.ctrlOff != prev->ctrlOff) chg.push_back(t);
        prev = &r;
    }
    std::vector<Crossing> out;
    prev = nullptr;
    for (long long t = 0; t < (long long)v.size(); ++t) {
        const AnchorRow& r = v[(size_t)t];
        if (!r.valid) continue;
        if (prev) {
            const bool vertical = (prev->gframe & 1) != 0;
            const double c0 = vertical ? prev->y : prev->x;
            const double c1 = vertical ? r.y : r.x;
            for (const RotQE& e : g_rotQOrder) {
                if (e.id != 2899) continue;
                const double p = vertical ? e.py : e.px;
                if (c0 != c1 && (c0 - p) * (c1 - p) <= 0.0) {
                    bool changed = false;
                    for (long long c : chg)
                        if (c >= t - 1 && c <= t + 1) { changed = true; break; }
                    out.push_back({t, e.uid, changed});
                }
            }
        }
        prev = &r;
    }
    return out;
}

struct Seed { std::string cls, why, prov, seed; };

// Whether the travel coordinate turns back between (t-1 -> t) and (t -> t+1).
inline bool reverses(const std::vector<AnchorRow>& v, long long t, int gframe) {
    if (t < 1 || t + 1 >= (long long)v.size()) return false;
    const AnchorRow& a = v[(size_t)(t - 1)];
    const AnchorRow& b = v[(size_t)t];
    const AnchorRow& c = v[(size_t)(t + 1)];
    if (!a.valid || !b.valid || !c.valid) return false;
    const bool vertical = (gframe & 1) != 0;
    const double d0 = vertical ? (double)b.y - (double)a.y : (double)b.x - (double)a.x;
    const double d1 = vertical ? (double)c.y - (double)b.y : (double)c.x - (double)b.x;
    return d0 * d1 < 0.0;
}

inline bool g_preButton = false;
inline Seed seedSim(const std::vector<AnchorRow>& v, long long t0) {
    Seed s;
    const auto q = byChannel();
    int chan = 0;
    bool rev[16] = {};
    int ptr[16] = {};
    std::vector<int> spent;
    int pg = -1;
    for (long long t = 0; t < (long long)v.size() && t <= t0; ++t) {
        const AnchorRow& r = v[(size_t)t];
        if (!r.valid) continue;
        const bool acted = pg >= 0 && (r.gframe != pg || reverses(v, t, r.gframe));
        auto walk = [&](double px, double py) {
            int axis = pg < 0 ? r.gframe : pg;
            int turned = 0;
            for (int guard = 0; guard < 64; ++guard) {
                const std::vector<const RotQE*>& lst = q[(size_t)chan];
                if (ptr[chan] >= (int)lst.size()) break;
                const RotQE& e = *lst[(size_t)ptr[chan]];
                if (!passed(axis, px, py, e, rev[chan])) break;
                ++ptr[chan];
                spent.push_back(e.uid);
                if (visible(e) && !(acted && turned == 0)) continue;   // switched off
                if (e.id == 2900 && e.swarm == 1 && e.swch >= 0 && e.swch <= 15) {
                    chan = e.swch;
                    rev[chan] = (unsigned)(e.gnddir - 2) < 2u;
                }
                if (visible(e)) {
                    ++turned;
                    axis = r.gframe;   // the rest of this tick's walk tests the new axis
                    s.prov += (s.prov.empty() ? "" : ",") + std::to_string(t) + ":"
                              + std::to_string(e.uid);
                }
            }
            return turned;
        };
        int turned = walk(r.x, r.y);
        if (g_preButton && pg >= 0 && r.gframe != pg && turned == 0 && t >= 2
            && v[(size_t)(t - 1)].valid && v[(size_t)(t - 2)].valid) {
            const AnchorRow& a = v[(size_t)(t - 2)];
            const AnchorRow& b = v[(size_t)(t - 1)];
            turned = walk(2.0 * b.x - a.x, 2.0 * b.y - a.y);
            if (turned) s.prov += "(pre)";
        }
        if (pg >= 0 && r.gframe != pg && turned == 0) {
            s.cls = "underivable";
            s.why = "S t=" + std::to_string(t) + " the recording turns and the walk fired nothing";
            return s;
        }
        pg = r.gframe;
    }
    unsigned mask = 0;
    for (int c = 0; c <= 15; ++c) if (rev[c]) mask |= 1u << c;
    char head[32];
    std::snprintf(head, sizeof head, "%d,%x", chan, mask);
    s.seed = head;
    for (int u : spent) s.seed += "," + std::to_string(u);
    s.cls = "exact";
    return s;
}

inline Seed seedFor(const std::vector<AnchorRow>& v, long long t0, int level) {
    Seed s;
    if (!rotQOrderReady()) {
        s.cls = "unavailable";
        s.why = "dp has not handed back the queue order";
        return s;
    }
    if (level >= 4) return seedSim(v, t0);
    const auto q = byChannel();
    const Chain w = walk(v, t0, q);
    s.prov = w.prov;
    if (!w.fail.empty()) {
        s.cls = w.underivable ? "underivable" : "ambiguous";
        s.why = w.fail;
        return s;
    }
    const AnchorRow* last = nullptr;
    for (long long t = std::min<long long>(t0, (long long)v.size() - 1); t >= 0; --t)
        if (v[(size_t)t].valid) { last = &v[(size_t)t]; break; }
    if (!last) {
        s.cls = "underivable";
        s.why = "no recorded row at or before t0";
        return s;
    }
    const std::vector<const RotQE*>& act = q[(size_t)w.chan];
    for (int k = w.ptr[w.chan]; k < (int)act.size(); ++k)
        if (passed(last->gframe, last->x, last->y, *act[(size_t)k], w.rev[w.chan])) {
            s.cls = "ambiguous";
            s.why = "C2 active channel " + std::to_string(w.chan) + " uid "
                    + std::to_string(act[(size_t)k]->uid) + " passed";
            return s;
        }
    const std::vector<Crossing> cr = level >= 2 ? crossings2899(v) : std::vector<Crossing>{};
    auto activeAt = [&](long long t) {
        int c = 0;
        for (const auto& p : w.timeline) if (p.first <= t) c = p.second;
        return c;
    };
    for (const auto& sk : w.skipped) {
        bool seen = false;
        for (const Crossing& c : cr)
            if (c.uid == sk.second && c.t <= sk.first && c.ctrlChanged) { seen = true; break; }
        if (!seen) {
            s.cls = "ambiguous";
            s.why = "C1 uid " + std::to_string(sk.second) + " passed over unseen before t="
                    + std::to_string(sk.first);
            return s;
        }
    }
    for (int c = 0; c <= 15; ++c) {
        if (c == w.chan) continue;
        const std::vector<const RotQE*>& lst = q[(size_t)c];
        for (int k = w.ptr[c]; k < (int)lst.size(); ++k) {
            const RotQE& e = *lst[(size_t)k];
            if (!passed(last->gframe, last->x, last->y, e, w.rev[c])) continue;
            bool ok = false;
            if (visible(e)) {   // A: it fires visibly later in this same attempt
                int pg = -1;
                for (long long t = 0; t < (long long)v.size() && !ok; ++t) {
                    const AnchorRow& r = v[(size_t)t];
                    if (!r.valid) continue;
                    if (t > t0 && pg >= 0 && r.gframe != pg && near2(e, r)) ok = true;
                    pg = r.gframe;
                }
            }
            if (!ok && level >= 2 && e.id == 2899)   // E: crossed while inactive, not consumed
                for (const Crossing& x : cr)
                    if (x.uid == e.uid && x.t <= t0 && !x.ctrlChanged && activeAt(x.t) != c) {
                        ok = true;
                        break;
                    }
            if (!ok && level >= 3 && g_rotSurvivors.count(e.uid)) ok = true;   // F
            if (!ok) {
                s.cls = "ambiguous";
                s.why = "C2 channel " + std::to_string(c) + " uid " + std::to_string(e.uid)
                        + " passed, no witness";
                return s;
            }
        }
    }
    unsigned rev = 0;
    for (int c = 0; c <= 15; ++c) if (w.rev[c]) rev |= 1u << c;
    char head[32];
    std::snprintf(head, sizeof head, "%d,%x", w.chan, rev);
    s.seed = head;
    for (int u : w.spent) s.seed += "," + std::to_string(u);
    s.cls = "exact";
    return s;
}

inline void fold(const std::vector<AnchorRow>& v) {
    if (!rotQOrderReady()) {
        writeResult("dpsolve:   [rotseed] fold skipped - dp has not handed back the queue order");
        return;
    }
    const auto q = byChannel();
    int chan = 0;
    bool rev[16] = {};
    int ptr[16] = {};
    std::map<int, long long> first;
    std::string added;
    int pg = -1;
    for (long long t = 0; t < (long long)v.size(); ++t) {
        const AnchorRow& r = v[(size_t)t];
        if (!r.valid) continue;
        if (pg >= 0 && r.gframe != pg) {
            const std::vector<const RotQE*>& lst = q[(size_t)chan];
            int k = ptr[chan];
            while (k < (int)lst.size() && !visible(*lst[(size_t)k]) && !lst[(size_t)k]->swarm) ++k;
            if (k >= (int)lst.size() || !visible(*lst[(size_t)k]) || !near2(*lst[(size_t)k], r)) break;
            const RotQE& e = *lst[(size_t)k];
            ptr[chan] = k + 1;
            if (first.count(e.uid) && g_rotSurvivors.insert(e.uid).second)
                added += (added.empty() ? "" : ",") + std::to_string(e.uid);
            if (e.swarm && e.swch >= 0 && e.swch <= 15) {
                chan = e.swch;
                rev[chan] = (unsigned)(e.gnddir - 2) < 2u;
            }
        }
        pg = r.gframe;
        for (int c = 0; c <= 15; ++c) {
            if (c == chan) continue;
            const std::vector<const RotQE*>& lst = q[(size_t)c];
            for (int k = ptr[c]; k < (int)lst.size(); ++k)
                if (!first.count(lst[(size_t)k]->uid)
                    && passed(r.gframe, r.x, r.y, *lst[(size_t)k], rev[c]))
                    first[lst[(size_t)k]->uid] = t;
        }
    }
    writeResult("dpsolve:   [rotseed] fold rows=" + std::to_string(v.size()) + " added="
                + (added.empty() ? std::string("-") : added) + " survivors="
                + std::to_string(g_rotSurvivors.size()));
}
}  // namespace rotseed

inline bool rotSeedArgs(long long t0, std::vector<std::string>& a, const char* site) {
    if (g_cfg.dpRotSeed <= 0 || t0 <= 0) return false;
    const std::vector<AnchorRow>& v = *anchors::g_src;
    rotseed::g_preButton = g_cfg.rotSeedPre;
    const rotseed::Seed s = rotseed::seedFor(v, t0, g_cfg.dpRotSeed);
    const AnchorRow* r0 = anchors::row(t0);
    const bool withheld = std::string(site) == "anchor" && !g_cfg.dpRotSeedAnchor;
    const char* queue = s.cls != "exact" ? "none" : withheld ? "withheld" : "given";
    char head[256];
    std::snprintf(head, sizeof head, "rotseed: site=%s t0=%lld level=%c class=%s rows=%zu x0=%.3f y0=%.3f",
                  site, t0, " AEFS"[g_cfg.dpRotSeed], s.cls.c_str(), v.size(),
                  r0 ? (double)r0->x : -1.0, r0 ? (double)r0->y : -1.0);
    writeResult(std::string(head) + " why=" + (s.why.empty() ? "-" : s.why)
                + " provenance=" + (s.prov.empty() ? "-" : s.prov)
                + " seed=" + (s.seed.empty() ? "-" : s.seed) + " queue=" + queue);
    if (s.cls != "exact" || withheld) return false;
    a.push_back("--rotqueue");
    a.push_back("--startrotq");
    a.push_back(s.seed);
    return true;
}

// ...and dp's own account of that seed, after the call: how many of its uids bound.
inline void logRotSeedRead(long long t0, const char* site) {
    const dpbridge::SolveOutcome o = dpbridge::outcome();
    writeResult(std::string("rotseed-read: site=") + site + " t0=" + std::to_string(t0)
                + " startrotq spent=" + std::to_string(o.startRotHit) + "/"
                + std::to_string(o.startRotGiven)
                + (o.startRotMiss.empty() ? "" : " NOT IN THE QUEUE: " + o.startRotMiss));
}

inline void addWorldArgs(std::vector<std::string>& a);

inline void addWorldArgs(std::vector<std::string>& a) {
    std::error_code ec;
    long long bootPart = -1, deepPart = -1;
    if (g_cfg.groupsRetime) {
        bootPart = clockPart(g_groupsBootX, g_groupsX);
        deepPart = clockPart(g_groupsDeepX, g_groupsX);
        const int out = (bootPart >= 0 ? 1 : 0) | (deepPart >= 0 ? 2 : 0);
        if (out != g_retimeLeftOut) {
            g_retimeLeftOut = out;
            char b[224];
            snprintf(b, sizeof(b), "dpsolve:   moving geometry: layers on another clock than the "
                     "live record - bootstrap %s (t=%lld), deep %s (t=%lld)",
                     bootPart >= 0 ? "left out" : "kept", bootPart,
                     deepPart >= 0 ? "left out" : "kept", deepPart);
            writeResult(b);
        }
    }
    if (g_cfg.dpGroups && !g_groupsBootPath.empty() && bootPart < 0
        && std::filesystem::exists(g_groupsBootPath, ec)) {
        a.push_back("--groups");
        a.push_back(g_groupsBootPath);
    }
    if (g_cfg.dpGroups && !g_groupsDeepPath.empty() && deepPart < 0
        && std::filesystem::exists(g_groupsDeepPath, ec)) {
        a.push_back("--groups");
        a.push_back(g_groupsDeepPath);
    }
    if (g_cfg.dpGroups && !g_groupsPath.empty()
        && std::filesystem::exists(g_groupsPath, ec)) {
        a.push_back("--groups");
        a.push_back(g_groupsPath);
        a.push_back("--groupholddeath");
    }
    {
        const std::string bp = std::string(DATA_DIR) + "/dp_band.txt";
        std::ofstream bf(bp, std::ios::trunc);
        auto bandMode = [](const AnchorRow* a) {
            auto m = [](int k) { return k == 1 || k == 2 || k == 3 || k == 4 || k == 6 || k == 7; };
            return a && (m(a->mode) || m(a->m2));
        };
        std::vector<dp::BandSample> samples;
        const long long n = anchors::depth();
        for (long long t = 1; t < n; ++t) {
            const AnchorRow* r = anchors::row(t);
            if (!r) continue;
            dp::BandSample s;
            s.t = t;
            s.written = bandMode(r) || bandMode(anchors::row(t - 1)) || bandMode(anchors::row(t + 1));
            s.pmin = r->pmin;
            s.pmax = r->pmax;
            s.cam = r->bandCam;
            samples.push_back(s);
        }
        long long rows = 0;
        bf << dp::bandRows(samples, &rows);
        bf.close();
        if (rows > 0 && g_cfg.dpBandTrack) {
            a.push_back("--bandtrack");
            a.push_back(bp);
            a.push_back("--bandtrackend");
            a.push_back(std::to_string(n - 1));
        }
    }
    const long long n = anchors::depth();
    auto windows = [&](bool second) {
        std::string wins;
        long long t0 = -1, last = -1;
        for (long long t = 1; t < n; ++t) {
            const AnchorRow* r = anchors::row(t);
            if (!r) continue;
            last = t;
            const bool off = second ? r->ctrlOff2 == 1 : r->ctrlOff != 0;
            if (off && t0 < 0) t0 = t;
            if (!off && t0 >= 0) {
                wins += (wins.empty() ? "" : ",") + std::to_string(t0) + ":"
                        + std::to_string(t - 1);
                t0 = -1;
            }
        }
        if (t0 >= 0)
            wins += (wins.empty() ? "" : ",") + std::to_string(t0) + ":"
                    + std::to_string(last);
        return wins;
    };
    const std::string wins = windows(false), wins2 = windows(true);
    if (!wins.empty()) {
        a.push_back("--ctrlwin");
        a.push_back(wins);
    }
    if (!wins2.empty()) {
        a.push_back("--ctrlwin2");
        a.push_back(wins2);
    }
    const std::string lset = std::string(DATA_DIR) + "/levelsettings.txt";
    if (std::filesystem::exists(lset, ec)) {
        a.push_back("--levelsettings"); a.push_back(lset);
    }
    if (!g_cfg.dpWorld) return;
    const std::string trig = std::string(DATA_DIR) + "/triggers.txt";
    const std::string grp = std::string(DATA_DIR) + "/objgroups.txt";
    const std::string obb = std::string(DATA_DIR) + "/obb.txt";
    if (std::filesystem::exists(trig, ec) && std::filesystem::exists(grp, ec)) {
        a.push_back("--triggers"); a.push_back(trig);
        a.push_back("--objgroups"); a.push_back(grp);
        if (g_needUnseen) a.push_back("--needtrig-unseen");
        for (int b = 0; b < 64; ++b)
            if (g_needTrigDropped & (1ull << b)) {
                a.push_back("--needtrig-skip");
                a.push_back(std::to_string(b));
            }
        if (g_cfg.routePrereq && g_routeNeedUid >= 0) {
            a.push_back("--needtrig-uid");
            a.push_back(std::to_string(g_routeNeedUid));
        }
    }
    if (std::filesystem::exists(obb, ec)) { a.push_back("--obb"); a.push_back(obb); }
    const std::string fids = std::string(DATA_DIR) + "/forceblocks.txt";
    if (std::filesystem::exists(fids, ec)) { a.push_back("--forceids"); a.push_back(fids); }
    // The area triggers (solver.hpp writes areas.txt at session start); read by dp only under --areamove.
    const std::string areas = std::string(DATA_DIR) + "/areas.txt";
    if (std::filesystem::exists(areas, ec)) { a.push_back("--areas"); a.push_back(areas); }
    if (rotQueueRequested() || g_cfg.dpRotSeed > 0) {
        const std::string rotq = std::string(DATA_DIR) + "/rotgameplay.txt";
        if (std::filesystem::exists(rotq, ec)) {
            a.push_back("--rotgameplay"); a.push_back(rotq);
        }
    }
    if (g_maxPlayYLive > 0.f) {
        a.push_back("--maxplayy");
        a.push_back(num(g_maxPlayYLive));
    }
}

inline std::string snapFile(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return "";
    std::string sig = fileSig(path);
    for (char& c : sig) if (c == '/') c = '_';
    const std::filesystem::path src(path);
    const std::string name = "snaps/" + src.stem().string() + "_" + sig + src.extension().string();
    const std::filesystem::path dst = std::filesystem::path(DATA_DIR) / name;
    std::filesystem::create_directories(dst.parent_path(), ec);
    if (!std::filesystem::exists(dst, ec))
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::skip_existing, ec);
    return ec ? "" : name;
}

inline constexpr const char* kFileArgs[] = {
    "--groups", "--fixups", "--bandtrack", "--rejoinwatch", "--replay",
    "--triggers", "--objgroups", "--obb", "--levelsettings", "--rotgameplay",
    "--forceids", "--xtrack", "--items", "--areas"};
inline bool isFileArg(const std::string& s) {
    for (const char* f : kFileArgs)
        if (s == f) return true;
    return false;
}

inline void adoptCoinGates() {
    if (!g_cfg.coinRoute) return;
    std::vector<solver::CoinGate> v;
    std::stringstream ss(dpbridge::outcome().coinGates);
    for (std::string item; std::getline(ss, item, ';');) {
        solver::CoinGate g;
        int miss = 0;
        if (std::sscanf(item.c_str(), "%d,%d,%lf,%lf,%d,%d,%d,%d,%lf,%lf", &g.uid, &g.chan,
                        &g.x, &g.y, &g.dir, &g.item, &g.need, &miss, &g.mx, &g.my) == 10) {
            g.miss = miss != 0;
            v.push_back(g);
        }
    }
    solver::g_coinGates.swap(v);
    // ...and the coins the search does not call missed when passed (cfg coinmissrev).
    std::vector<int> nm;
    std::stringstream sn(dpbridge::outcome().coinNoPrune);
    for (std::string item; std::getline(sn, item, ';');)
        if (!item.empty()) nm.push_back(std::atoi(item.c_str()));
    solver::g_coinNoMiss.swap(nm);
}

inline void logTrigWindow(const char* tag) {
    const dpbridge::SolveOutcome o = dpbridge::outcome();
    if (o.trigWinTouch < 0 && o.trigTotal == 0) return;   // no boxes: say nothing
    char cov[128] = "";
    if (o.trigDroppedAhead > 0 && o.deepX > o.trigMaxKeptX)
        std::snprintf(cov, sizeof cov,
                      "  UNCOVERED past x=%.0f (%lld relevant boxes AHEAD not "
                      "loaded, deepest x=%.0f)",
                      o.trigMaxKeptX, o.trigDroppedAhead, o.deepX);
    char b[448];
    std::snprintf(b, sizeof b,
                  "dpsolve:   [trigwin] %s win=%d total=%lld relevant=%lld "
                  "kept=%lld dropBehind=%lld dropAhead=%lld maxKeptX=%.0f "
                  "map=%016llx%s",
                  tag, o.trigWinTouch, o.trigTotal, o.trigRelevantN, o.trigKept,
                  o.trigDroppedBehind, o.trigDroppedAhead, o.trigMaxKeptX,
                  o.trigMapSig, cov);
    writeResult(b);
}

inline int g_plainBesideMode = 0;
inline int timedSolve(const std::string& csv, const std::vector<std::string>& a,
                      const char* kind) {
    const auto c0 = std::chrono::steady_clock::now();
    static constexpr const char* kStart = "starting the search (loading inputs)";
    const bool isResim = std::strcmp(kind, "resim") == 0;
    phase::Scope ph(isResim ? "replaying the plan through the model (resim)" : kStart, g_anchorT, g_iter);
    struct StartWatch {
        std::atomic<bool> stop{false};
        std::thread th;
        explicit StartWatch(bool on, phase::Id id) {
            if (!on) return;
            th = std::thread([this, id] {
                while (!stop.load(std::memory_order_acquire)) {
                    if (dpbridge::progress().running) { phase::endId(id); return; }
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            });
        }
        ~StartWatch() {
            stop.store(true, std::memory_order_release);
            if (th.joinable()) th.join();
        }
    } startWatch(!isResim, ph.id());
    dpbridge::plainBeside(g_plainBesideMode);   // cfg dpplainbeside (config.hpp) or the menu
    {   // the signatures of the recordings this call reads, for dp's groups cache (g_sigKnown)
        std::unordered_map<std::string, std::string> hints;
        for (size_t i = 0; i + 1 < a.size(); ++i)
            if (isFileArg(a[i])) {
                const auto k = g_sigKnown.find(a[i + 1]);
                if (k != g_sigKnown.end()) hints[a[i + 1]] = k->second;
            }
        dpbridge::setFileSigHints(hints);
    }
    std::vector<std::string> aa;
    const std::vector<std::string>* av = &a;
    if (g_cfg.dpSigCheck) {   // cfg dpsigcheck: dp checks its signature hits against the bytes too
        aa = a;
        aa.push_back("--groupsigcheck");
        aa.push_back("--levelcachecheck");   // ...and its replay level cache against the level it stored
        av = &aa;
    }
    const int rc = dpbridge::solveInProcess(csv, *av);
    dpbridge::setFileSigHints({});
    {   // the cap ladder's account of the call, for a change to the ladder to be checked against
        const std::string lad = dpbridge::outcome().ladder;
        if (!lad.empty()) writeResult(std::string("dpsolve:   [ladder] ") + kind + " " + lad);
    }
    if (g_plainBesideMode == 2) {
        const std::string c = dpbridge::besideCheckTaken();
        if (!c.empty()) writeResult(std::string("dpsolve:   [beside] ") + kind + ": " + c);
    }
    if (g_cfg.dpPhaseProf) {
        const std::vector<double> m = dpbridge::prepMarks();
        auto at = [&](size_t k) { return k < m.size() ? m[k] : -1.0; };
        char b[224];
        snprintf(b, sizeof(b), "dpcall: %s %.3f s (prep: args %.3f groups %.3f objpos %.3f "
                 "touch %.3f level %.3f tables %.3f)", kind,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - c0).count(),
                 at(0), at(1), at(5), at(2), at(3), at(4));
        writeResult(b);
    }
    return rc;
}

inline void logSolverArgs(const std::vector<std::string>& a) {
    std::string line = "dpsolve: solver args:";
    for (const std::string& s : a) line += " " + s;
    if (!g_argsLogged || line != g_argsLast) {
        g_argsLogged = true;
        g_argsLast = line;
        writeResult("dpsolve:   [call] att=" + std::to_string(g_attempt));
        writeResult(line);
    }
    char fb[64];
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    std::snprintf(fb, sizeof fb, "dpsolve: fpenv mxcsr=0x%04x",
                  (unsigned)_mm_getcsr());
#elif defined(__aarch64__)
    unsigned long long fpcr = 0;   // AArch64's flush-to-zero and rounding mode
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    std::snprintf(fb, sizeof fb, "dpsolve: fpenv fpcr=0x%08llx", fpcr);
#endif
    writeResult(fb);
    std::string sig = "dpsolve: input sig";
    for (size_t i = 0; i + 1 < a.size(); ++i)
        if (isFileArg(a[i]))
            sig += " " + a[i].substr(2) + "=" + fileSig(a[i + 1]);
    // ...and the level table the solver gets in memory (g_csvSig)
    sig += " level=" + (g_csvSig.empty() ? std::string("-") : g_csvSig);
    writeResult(sig);
    if (g_cfg.dpSnapshot) {
        std::string snap = "dpsolve: input snap";
        for (size_t i = 0; i + 1 < a.size(); ++i)
            if (isFileArg(a[i])) {
                const std::string n = snapFile(a[i + 1]);
                snap += " " + a[i].substr(2) + "=" + (n.empty() ? "-" : n);
            }
        std::string lvl = "-";
        if (!g_csvSig.empty()) {
            std::string tag = g_csvSig;
            for (char& c : tag) if (c == '/') c = '_';
            const std::string name = "snaps/objrects_" + tag + ".txt";
            const std::filesystem::path dst = std::filesystem::path(DATA_DIR) / name;
            std::error_code ec;
            std::filesystem::create_directories(dst.parent_path(), ec);
            if (std::filesystem::exists(dst, ec)) {
                lvl = name;
            } else {
                std::ofstream f(dst, std::ios::binary | std::ios::trunc);
                f.write(g_csv.data(), (std::streamsize)g_csv.size());
                f.close();
                if (f) lvl = name;
                else std::filesystem::remove(dst, ec);
            }
        }
        snap += " level=" + lvl;
        writeResult(snap);
    }
}

inline long long g_rjAfterTick = -1;   // cfg dprejoinwatch: the death the trace ends in
inline bool g_rjTailJoined = false;
inline bool g_rjOldIsJoin = false;
inline bool g_rjKeepTarget = false;
inline std::string rejoinTracePath() { return std::string(DATA_DIR) + "/dp_rejoin.trace.csv"; }
inline std::vector<std::string> baseArgs(const std::string& out,
                                         double x0 = std::numeric_limits<double>::quiet_NaN()) {
    const bool tiered = g_capTier > 0;
    std::vector<std::string> a{"--out", out,
                               "--horizon", std::to_string(horizonFor(x0)),
                               "--cap", std::to_string(tiered ? kCapTiers[g_capTier - 1] : kCap),
                               "--shipyq", num(tiered ? kTierYq : kYq),
                               "--shipvq", num(tiered ? kTierVq : kVq),
                               "--threads", solverThreads()};
    if (g_cfg.dpPhaseProf) a.push_back("--phaseprof");   // print only (see timedSolve)
    a.push_back("--groupfire");
    // cfg dpsearch (config.hpp): another search than the layer DP answers these calls.
    if (g_cfg.dpSearch != "dp") {
        a.push_back("--search");
        a.push_back(g_cfg.dpSearch);
    }
    if (g_cfg.dpCapLadder > 0 && !tiered) {
        a.push_back("--capladder");
        a.push_back(std::to_string(g_cfg.dpCapLadder));
    }
    std::error_code ec;
    if (!g_fixupPath.empty() && std::filesystem::exists(g_fixupPath, ec)) {
        a.push_back("--fixups");
        a.push_back(g_fixupPath);
    }
    for (const std::string& band : g_phantomBands) {
        a.push_back("--deadband");
        a.push_back(band);
    }
    if (g_cfg.glitchPortal > 0.0) {
        a.push_back("--glitchportal");
        a.push_back(num(g_cfg.glitchPortal));
    }
    if (g_cfg.glitchWave > 0.0) {
        a.push_back("--glitchwave");
        a.push_back(num(g_cfg.glitchWave));
    }
    if (g_cfg.glitchEmbed > 0.0) {
        a.push_back("--glitchembed");
        a.push_back(num(g_cfg.glitchEmbed));
    }
    if (g_cfg.glitchDeco) a.push_back("--glitchdeco");
    addWorldArgs(a);
    if (g_rjAfterTick >= 0
        && std::filesystem::exists(rejoinTracePath(), ec)) {
        a.push_back("--rejoinwatch");
        a.push_back(rejoinTracePath());
        a.push_back("--rejoinafter");
        a.push_back(std::to_string(g_rjAfterTick));
        a.push_back("--rejoinuse");
        if (g_cfg.dpRejoinFull) a.push_back("--rejoinfull");
    }
    if (g_cfg.dpOffBoardKill) {
        a.push_back("--offboard");
        a.push_back(num(kOffBoard));
    }
    if (g_cfg.coinRoute) a.push_back("--coins");
    if (g_cfg.coinRoute && !g_cfg.dpCoinWin.empty()) {
        a.push_back("--coinwin");
        a.push_back(g_cfg.dpCoinWin);
    }
    if (g_cfg.worldGates) a.push_back("--worldgates");
    if (g_cfg.coinRoute || g_cfg.worldGates) {
        const std::string ip = std::string(DATA_DIR) + "/items.txt";
        if (std::filesystem::exists(ip, ec)) {
            a.push_back("--items");
            a.push_back(ip);
        }
    }
    if (g_cfg.coinRoute) {
        if (g_coinMarginNow > 0.0) {
            a.push_back("--coinmargin");
            a.push_back(num(g_coinMarginNow));
        }
    }
    // cfg dpinputgrid: before dparg, so a dparg=--inputgrid still has the last word.
    if (g_cfg.dpInputGrid > 1) {
        a.push_back("--inputgrid");
        a.push_back(std::to_string(g_cfg.dpInputGrid));
    }
    if (!Mod::get()->getSettingValue<bool>("go-crazy") && g_kLimitWall < 0) {
        if (g_kHumanFrom > 0) {
            a.push_back("--humanfrom");
            a.push_back(std::to_string(g_kHumanFrom));
        }
        a.push_back("--minpulse");
        a.push_back(std::to_string(Mod::get()->getSettingValue<int64_t>("human-min-pulse")));
        // ...and no faster clicking than "max-cps" (0 = no limit).
        if (const int64_t cps = Mod::get()->getSettingValue<int64_t>("max-cps"); cps > 0) {
            a.push_back("--minpressgap");
            a.push_back(std::to_string((240 + cps - 1) / cps));
        }
    }
    // which of the routes reaching the end the search keeps ("solve-goal").
    if (const auto goal = Mod::get()->getSettingValue<std::string>("solve-goal"); goal == "Fewest inputs") {
        a.push_back("--fewpress");
    } else {   // roomy routes: wider frame windows
        a.push_back("--roomy");
        a.push_back("--tightpx");
        a.push_back(goal == "Most lenient windows" ? "16" : "8");
    }
    for (const std::string& s : g_cfg.dpArgs) a.push_back(s);
    if (g_capTier > 0 || g_coldRestarted) {
        bool grid = g_cfg.dpInputGrid > 1;
        for (const std::string& s : g_cfg.dpArgs) grid = grid || s == "--inputgrid";
        if (grid) {
            a.push_back("--gridmap");
            a.push_back("-1e9:1");
        }
    }
    return a;
}

inline int groundedOf(const AnchorRow& r) {
    const bool flying = (r.mode == 1 || r.mode == 3 || r.mode == 4);   // ship / ufo / wave
    if (flying)
        return (r.onGround && r.onGround2 && std::fabs(r.vy) < 0.01f) ? 1 : 0;
    return (r.onGround || r.vy == 0.f) ? 1 : 0;
}

inline int groundedOf2(const AnchorRow& r) {
    if (!r.dual) return 0;
    const bool flying = (r.mode == 1 || r.mode == 3 || r.mode == 4);
    if (flying)
        return (r.g2 && r.g2b && std::fabs(r.v2) < 0.01f) ? 1 : 0;
    return (r.g2 || r.v2 == 0.f) ? 1 : 0;
}

constexpr int kRobotHoverTicks = 67;

inline int robotHoverLeft(long long t0) {
    const AnchorRow* cur = anchors::row(t0);
    if (!cur || cur->mode != 5 || cur->onGround) return 0;   // grounded = GD has dropped it
    const float vp = cur->flip ? -cur->vy : cur->vy;
    if (vp < 0.f) return 0;
    long long j = t0;
    for (int i = 0; i <= kRobotHoverTicks; ++i) {
        const AnchorRow* prev = anchors::row(j - 1);
        if (!prev || prev->onGround || prev->vy != cur->vy) break;
        --j;
    }
    if (j == t0) {
        const AnchorRow* next = anchors::row(t0 + 1);
        const bool startsHere = next && !next->onGround && next->vy == cur->vy;
        return startsHere ? kRobotHoverTicks : 0;
    }
    const long long left = kRobotHoverTicks - (t0 - j);
    return left > 0 ? (int)left : 0;
}

inline int heldBefore(const std::vector<InputCmd>& plan, long long t0) {
    int held = 0;
    for (const InputCmd& c : plan) {
        if (c.step >= t0) break;
        held = c.down ? 1 : 0;
    }
    return held;
}

inline int swingPendingAt(const std::vector<InputCmd>& plan, long long t0) {
    int prev = 0, last = 0;
    long long lastStep = -1;
    for (const InputCmd& c : plan) {
        if (c.step >= t0) break;
        prev = last;
        last = c.down ? 1 : 0;
        lastStep = c.step;
    }
    return (lastStep == t0 - 1 && last == 1 && prev == 0) ? 1 : 0;
}

inline void addStartP2dx(std::vector<std::string>& a, const AnchorRow* r) {
    if (!r || !r->dual) return;
    const float d = r->x2 - r->x;
    if (std::fabs(d) <= 0.01f) return;
    a.push_back("--startp2dx");
    a.push_back(num(d));
}

inline std::string startArg(long long t0, const AnchorRow& r, int held, int swingPending = 0) {
    int gf = r.gframe, rv = 0, flip = r.flip;
    if (gf == 2) { gf = 0; rv = 1; }
    else if (gf == 3) { flip = 1 - flip; }
    if (rv == 0) {
        const AnchorRow* prev = anchors::row(t0 - 1);
        if (prev) {
            const double d = (r.gframe % 2 == 1) ? (r.y - prev->y) : (r.x - prev->x);
            const double fwd = (r.gframe == 0 || r.gframe == 3) ? 1.0 : -1.0;
            if (d * fwd < -1e-6) rv = 1;
        }
    }
    std::string s = std::to_string(t0);
    s += "," + num(r.x) + "," + num(r.y) + "," + num(r.vy);
    s += "," + std::to_string(r.mode) + "," + std::to_string(groundedOf(r));
    s += "," + std::to_string(held) + "," + std::to_string(flip)
       + "," + std::to_string(r.mini) + "," + std::to_string(r.dual);
    s += "," + num(r.y2) + "," + num(r.v2) + "," + std::to_string(r.f2)
       + "," + std::to_string(groundedOf2(r)) + "," + num(r.speed);
    const int hover = r.dashing ? 0
                    : (r.mode == 7 && swingPending) ? 1 : robotHoverLeft(t0);
    s += "," + std::to_string(hover) + "," + std::to_string(r.dashing)
       + "," + num(r.dashSlope);
    s += "," + std::to_string(r.snapUid) + "," + num(r.snapDist);
    s += "," + std::to_string(gf) + "," + std::to_string(rv);
    int rotNeg = 0;
    if (const AnchorRow* prev = anchors::row(t0 - 1))
        if (r.rot - prev->rot < 0.f) rotNeg = 1;
    s += "," + num(r.rot) + "," + std::to_string(rotNeg);
    // 25th: the velocity-limit exemption (GD's byte 0x952 -> State::boost).
    s += "," + std::to_string(r.boost);
    s += "," + std::to_string(r.m2) + "," + std::to_string(r.mini2);
    if (g_cfg.dpAnchorRotStep || (g_cfg.dpAnchorRot2 && r.dual)) {
        double step = 0.0;
        if (g_cfg.dpAnchorRotStep)
            if (const AnchorRow* prev = anchors::row(t0 - 1))
                step = std::fabs(std::remainder((double)r.rot - (double)prev->rot, 360.0));
        s += ",-1,-1," + num(step);
    }
    if (g_cfg.dpAnchorRot2 && r.dual) {
        double d2 = 0.0;
        if (const AnchorRow* prev = anchors::row(t0 - 1))
            if (prev->dual) d2 = std::remainder((double)r.rot2 - (double)prev->rot2, 360.0);
        s += "," + num(r.rot2) + "," + std::to_string(d2 < 0.0 ? 1 : 0) + "," + num(std::fabs(d2));
    }
    return s;
}

inline std::string anchorPayload(long long t0) {
    std::string body;
    for (const auto& kv : anchors::seeds().touch) {
        if (kv.second > t0) continue;
        if (!body.empty()) body += ",";
        body += std::to_string(kv.first) + ":" + std::to_string(kv.second);
    }
    if (body.empty()) return std::string();
    return "owns=touch;touch=" + body;
}

inline std::string portalPayload(long long t0) {
    const bool lt = g_cfg.portalPayloadLt;
    auto spent = [&](const std::unordered_map<int, int>& seeds) {
        std::vector<int> uids;
        for (const auto& kv : seeds)
            if (lt ? kv.second < t0 : kv.second <= t0) uids.push_back(kv.first);
        std::sort(uids.begin(), uids.end());
        std::string out;
        for (const int u : uids) out += (out.empty() ? "" : ",") + std::to_string(u);
        return out;
    };
    const std::string p1 = spent(anchors::seeds().portal);
    const std::string p2 = spent(anchors::seeds().portal2);
    if (p1.empty() && p2.empty()) return std::string();
    return "owns=portal;portal=" + p1 + ";portal2=" + p2;
}

inline std::string histPayload(long long t0) {
    const AnchorRow* r = anchors::row(t0);
    if (!r || !r->valid) return std::string();
    const int ps = (r->b985 && !r->b986) ? 1 : 0;
    const int ps2 = (r->b985_2 > 0 && r->b986_2 == 0) ? 1 : 0;
    const double since = r->totalTime - r->spiderStamp;
    const long long age = (since >= 0.0) ? std::llround(since * 240.0) : -1;
    std::string s = std::string("owns=hist;hist=") + (g_cfg.histRide ? "6|13" : "3|4")
         + "|pressSpent:" + std::to_string(ps)
         + ",pressSpent2:" + std::to_string(ps2)
         + ",freeMode:" + std::to_string(r->freeMode)
         + ",spiderJumpT:" + std::to_string(std::min<long long>(age, 255));
    if (!g_cfg.histRide) return s;
    long long slopeAge = -1;
    int landed = -1;
    if (r->onSlope) {
        const double rs = r->totalTime - r->slopeStart;
        slopeAge = std::clamp<long long>(std::llround(rs * 240.0), 0, 255);
        landed = 0;
        for (long long t = t0; t >= 0; --t) {
            const AnchorRow* q = anchors::row(t);
            if (!q || !q->onSlope || q->slopeStart != r->slopeStart) break;
            if (groundedOf(*q)) { landed = 1; break; }
        }
    }
    return s + ",slopeOn:" + std::to_string(r->onSlope)
         + ",slopeUnder:" + std::to_string(r->slopeUnder)
         + ",slopeUid:" + std::to_string(r->slopeUid)
         + ",slopeAge:" + std::to_string(slopeAge)
         + ",slopeLanded:" + std::to_string(landed)
         + ",dashing2:" + std::to_string(r->dashing2)
         + ",dashRing2:" + std::to_string(r->dashRing2)
         + ",jumpBuf:" + std::to_string(r->b985)
         + ",jumpBuf2:" + std::to_string(r->b985_2);
}

inline std::string anchorPayloadAll(long long t0) {
    std::string out;
    auto add = [&](const std::string& s) {
        if (s.empty()) return;
        out += (out.empty() ? "" : ";") + s;
    };
    add(g_cfg.touchPayload ? anchorPayload(t0) : std::string());
    add(g_cfg.portalPayload ? portalPayload(t0) : std::string());
    add(histPayload(t0));
    return out;
}

constexpr long long kFixupWindow = 400;
constexpr double kDivergeEps = 0.3;
constexpr double kNoopEps = 0.05;
constexpr double kForkX = 5.0;
constexpr int kFixupPasses = 4;    // a multi-tick divergence needs one record per tick
constexpr int kFixupCap = 2000;

inline std::string g_fixupNoopPath;  // records that turned out to change nothing (dedupe marks)
inline int g_fixupCount = 0;
inline int g_fixupNoop = 0;

// One row of the model's own trace: tick,x,y,vy,mode,grounded,dual,y2,vy2,flip2,act
struct TraceRow {
    bool valid = false;
    double x = 0, y = 0, vy = 0, y2 = 0, vy2 = 0;
    int mode = 0, grounded = 0, dual = 0, act = -1;
    int flip = -1, mini = -1;
    double dx = 0.0, slopem = 0.0, bandf = 0.0, bandc = 0.0;
    int onslope = -1, slopet = -1, nearorb = -1;
    std::string clamp, clampuid;
    int frame = -1;
    int fxblk = -1;
    bool classOk = false;
};

inline std::string g_traceClassMissing;

inline bool loadTrace(const std::string& path, std::map<long long, TraceRow>& out) {
    std::ifstream f(path);
    if (!f) return false;
    out.clear();
    auto split = [](const std::string& s) {
        std::vector<std::string> v;
        size_t p = 0;
        while (true) {
            size_t q = s.find(',', p);
            v.push_back(s.substr(p, q == std::string::npos ? q : q - p));
            if (q == std::string::npos) break;
            p = q + 1;
        }
        return v;
    };
    std::string line;
    if (!std::getline(f, line)) return false;
    if (!line.empty() && (unsigned char)line[0] == 0xEF) line.erase(0, 3);
    std::map<std::string, size_t> col;
    {
        const std::vector<std::string> h = split(line);
        for (size_t i = 0; i < h.size(); ++i) col[h[i]] = i;
    }
    static const char* kRepair[] = {
        "tick", "x", "y", "vy", "mode", "grounded", "dual", "y2", "vy2", "act",
    };
    static const char* kClass[] = {
        "flip", "mini", "dx", "onslope", "slopem", "slopet", "nearorb",
        "clamp", "clampuid", "bandf", "bandc",
    };
    std::string missing;
    for (const char* n : kRepair)
        if (!col.count(n)) missing += (missing.empty() ? "" : ",") + std::string(n);
    if (!missing.empty()) {
        char msg[320];
        snprintf(msg, sizeof(msg),
                 "dpsolve:   [fixup] REFUSED %s - the trace has %zu columns and is "
                 "missing what a RECORD needs: %s.",
                 path.c_str(), col.size(), missing.c_str());
        writeResult(msg);
        return false;
    }
    g_traceClassMissing.clear();
    for (const char* n : kClass)
        if (!col.count(n))
            g_traceClassMissing += (g_traceClassMissing.empty() ? "" : ",")
                                   + std::string(n);
    if (!g_traceClassMissing.empty()) {
        char msg[320];
        snprintf(msg, sizeof(msg),
                 "dpsolve:   [fixup] %s cannot be classified - missing: %s. "
                 "Repair continues; the family is reported as unavailable.",
                 path.c_str(), g_traceClassMissing.c_str());
        writeResult(msg);
    }
    size_t maxRepair = 0, maxClass = 0;
    for (const char* n : kRepair) {
        auto it = col.find(n);
        if (it != col.end()) maxRepair = std::max(maxRepair, it->second);
    }
    for (const char* n : kClass) {
        auto it = col.find(n);
        if (it != col.end()) maxClass = std::max(maxClass, it->second);
    }
    auto num = [&](const std::vector<std::string>& c, const char* n,
                   double def = 0.0) {
        auto it = col.find(n);
        if (it == col.end() || it->second >= c.size()) return def;
        return std::atof(c[it->second].c_str());
    };
    auto integer = [&](const std::vector<std::string>& c, const char* n,
                       int def = 0) {
        auto it = col.find(n);
        if (it == col.end() || it->second >= c.size()) return def;
        return std::atoi(c[it->second].c_str());
    };
    auto text = [&](const std::vector<std::string>& c, const char* n) {
        auto it = col.find(n);
        if (it == col.end() || it->second >= c.size()) return std::string();
        return c[it->second];
    };
    while (std::getline(f, line)) {
        if (line.empty() || !isdigit((unsigned char)line[0])) continue;
        const std::vector<std::string> c = split(line);
        if (c.size() <= maxRepair) continue;
        TraceRow r;
        r.valid = true;
        r.classOk = (c.size() > maxClass);
        const long long t = (long long)num(c, "tick");
        r.x = num(c, "x");
        r.y = num(c, "y");
        r.vy = num(c, "vy");
        r.mode = integer(c, "mode");
        r.grounded = integer(c, "grounded");
        r.dual = integer(c, "dual");
        r.y2 = num(c, "y2");
        r.vy2 = num(c, "vy2");
        const std::string a = text(c, "act");
        r.act = (a == "0" || a == "1") ? std::atoi(a.c_str()) : -1;
        r.mini = integer(c, "mini", -1);
        r.flip = integer(c, "flip", -1);
        // ...and the family's remaining inputs.
        r.dx = num(c, "dx");
        r.onslope = integer(c, "onslope");
        r.slopem = num(c, "slopem");
        r.slopet = integer(c, "slopet");
        r.nearorb = integer(c, "nearorb");
        r.clamp = text(c, "clamp");
        r.clampuid = text(c, "clampuid");
        r.frame = integer(c, "frame", -1);
        r.fxblk = integer(c, "fxblk", -1);
        r.bandf = num(c, "bandf");
        r.bandc = num(c, "bandc");
        out[t] = r;
    }
    return !out.empty();
}

struct FixupKey {
    double x = 0, y = 0, vy = 0, y2 = 0, vy2 = 0;
    int in = 0, mode = 0, mini = 0, flip = 0, g = 0, kill = 0, dual = 0;
};

inline bool parseFixupKey(const std::string& line, FixupKey& k,
                          double* dyOut = nullptr, double* dvOut = nullptr) {
    double dy, dvy;
    int g2;
    if (std::sscanf(line.c_str(),
                    "x=%lf,in=%d,mode=%d,mini=%d,flip=%d,g=%d,"
                    "y=%lf,vy=%lf,dy=%lf,dvy=%lf,g2=%d,kill=%d",
                    &k.x, &k.in, &k.mode, &k.mini, &k.flip, &k.g, &k.y, &k.vy,
                    &dy, &dvy, &g2, &k.kill) < 12)
        return false;
    if (dyOut) *dyOut = dy;
    if (dvOut) *dvOut = dvy;
    const size_t p = line.find(",dual2=");
    if (p != std::string::npos) {
        double d1, d2;
        int gg;
        if (std::sscanf(line.c_str() + p, ",dual2=%lf,%lf,%lf,%lf,%d",
                        &k.y2, &k.vy2, &d1, &d2, &gg) == 5)
            k.dual = 1;
    }
    return true;
}

inline bool sameTransition(const FixupKey& a, const FixupKey& b) {
    if (a.in != b.in || a.kill != b.kill || a.mode != b.mode || a.mini != b.mini
        || a.flip != b.flip || a.g != b.g || a.dual != b.dual)
        return false;
    if (std::fabs(a.x - b.x) > 1.2 || std::fabs(a.y - b.y) > 4.0
        || std::fabs(a.vy - b.vy) > 1.0)
        return false;
    if (a.dual && (std::fabs(a.y2 - b.y2) > 4.0 || std::fabs(a.vy2 - b.vy2) > 1.0))
        return false;
    return true;
}

inline bool fixupOnFile(const std::string& path, const FixupKey& k,
                        double dyG, double dvG) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        FixupKey o;
        double dy = 0, dvy = 0;
        if (parseFixupKey(line, o, &dy, &dvy) && sameTransition(o, k)
            && std::fabs(dy - dyG) < 0.2 && std::fabs(dvy - dvG) < 0.2)
            return true;
    }
    return false;
}

enum FixupWrite {
    FixupReal,       // a record that changes what the model does
    FixupMark,       // ...and one that only says "looked at"
    FixupCapped,     // the run has recorded all it is allowed to
    FixupNoTrace,    // the model's own resim has no row on one side of the transition
    FixupNoRow,      // ...and neither does GD's recording
    FixupHalfPair,   // a dual, with only one of the two bodies observed
    FixupNoInput,    // the resim did not write down which way the button was
    FixupOnFile,     // an existing record already covers this state
    FixupIoError,
    FixupBlockedDyn,
};
inline const char* fixupWhy(int w) {
    switch (w) {
        case FixupReal:     return "it went on file";
        case FixupMark:     return "it was already right, so only a mark went on file";
        case FixupCapped:   return "the record budget is spent";
        case FixupNoTrace:  return "the resim has no row for one side of it";
        case FixupNoRow:    return "the game's recording has no row for one side of it";
        case FixupHalfPair: return "a dual with only one body observed";
        case FixupNoInput:  return "the resim did not record the input";
        case FixupOnFile:   return "a record already covers this state";
        case FixupBlockedDyn:
            return "a record matches but the solver applies none next to moving geometry";
        default:            return "it could not be written";
    }
}

inline int writeFixup(long long t, int kill, const std::map<long long, TraceRow>& m,
                      bool revive = false, bool sameDeath = false) {
    if (g_fixupCount >= kFixupCap) return FixupCapped;
    auto mPrev = m.find(t - 1), mCur = m.find(t);
    if (mPrev == m.end() || mCur == m.end()) return FixupNoTrace;
    const AnchorRow* gPrev = anchors::row(t - 1);
    const AnchorRow* gCur = anchors::row(t);
    if (!gPrev || (!gCur && kill == 0)) return FixupNoRow;
    const bool dual = (mPrev->second.dual != 0);
    if (dual && !(gPrev->dual && mCur->second.dual && (!gCur || gCur->dual))) return FixupHalfPair;
    const int act = mCur->second.act;
    if (act < 0) return FixupNoInput;
    const double dyG = gCur ? gCur->y - gPrev->y : 0.0;
    const double dvG = gCur ? gCur->vy - gPrev->vy : 0.0;
    const bool flying = (mPrev->second.mode == 1 || mPrev->second.mode == 3
                         || mPrev->second.mode == 4);
    const int g2 = (flying || !gCur) ? 255 : (gCur->onGround ? 1 : 0);
    const double eDy = dyG - (mCur->second.y - mPrev->second.y);
    const double eDvy = dvG - (mCur->second.vy - mPrev->second.vy);
    const double eDy2 = dual ? (gCur ? gCur->y2 - gPrev->y2 : 0.0)
                                   - (mCur->second.y2 - mPrev->second.y2) : 0.0;
    const double eDvy2 = dual ? (gCur ? gCur->v2 - gPrev->v2 : 0.0)
                                   - (mCur->second.vy2 - mPrev->second.vy2) : 0.0;
    const bool noop = (kill == 0 && !revive && std::fabs(eDy) < kNoopEps
                       && std::fabs(eDvy) < kNoopEps
                       && std::fabs(eDy2) < kNoopEps
                       && std::fabs(eDvy2) < kNoopEps);
    const int keyFlip = mPrev->second.flip >= 0 ? mPrev->second.flip : gPrev->flip;
    const int keyMini = mPrev->second.mini >= 0 ? mPrev->second.mini : gPrev->mini;
    FixupKey key;
    key.x = mPrev->second.x;
    key.y = mPrev->second.y;
    key.vy = mPrev->second.vy;
    key.in = act;
    key.mode = mPrev->second.mode;
    key.mini = keyMini;
    key.flip = keyFlip;
    key.g = mPrev->second.grounded;
    key.kill = kill;
    key.dual = dual ? 1 : 0;
    key.y2 = mPrev->second.y2;
    key.vy2 = mPrev->second.vy2;
    if (fixupOnFile(g_fixupPath, key, dyG, dvG)
        || (!revive && fixupOnFile(g_fixupNoopPath, key, dyG, dvG)))
        return mCur->second.fxblk >= 0 ? FixupBlockedDyn : FixupOnFile;
    char dual2[160] = "";
    if (dual) {
        snprintf(dual2, sizeof(dual2), ",dual2=%.4f,%.4f,%.4f,%.4f,%d",
                 mPrev->second.y2, mPrev->second.vy2,
                 gCur ? gCur->y2 - gPrev->y2 : 0.0, gCur ? gCur->v2 - gPrev->v2 : 0.0,
                 (flying || !gCur) ? 255 : (gCur->g2 ? 1 : 0));
    }
    char line[700];
    snprintf(line, sizeof(line),
             "x=%.4f,in=%d,mode=%d,mini=%d,flip=%d,g=%d,y=%.4f,vy=%.4f,"
             "dy=%.4f,dvy=%.4f,g2=%d,kill=%d,edy=%.4f,edvy=%.4f%s",
             mPrev->second.x, act, mPrev->second.mode, keyMini, keyFlip,
             mPrev->second.grounded, mPrev->second.y, mPrev->second.vy,
             dyG, dvG, g2, kill, eDy, eDvy, dual2);
    {
        std::ofstream f(noop ? g_fixupNoopPath : g_fixupPath, std::ios::app);
        if (!f) return FixupIoError;
        f << line << "\n";
    }
    char b[800];
    char e2[48] = "";
    if (dual) snprintf(e2, sizeof(e2), " edy2=%.3f edvy2=%.3f", eDy2, eDvy2);
    if (noop) {
        ++g_fixupNoop;
        snprintf(b, sizeof(b), "dpsolve:   [fixup] t=%lld already right (edy=%.3f edvy=%.3f%s) "
                 "- marked only (%d)", t, eDy, eDvy, e2, g_fixupNoop);
    } else {
        ++g_fixupCount;
        itermap::addFixup(g_iter, t, (float)mPrev->second.x, (float)mPrev->second.y, kill != 0,
                          sameDeath);
        char gd[192] = "";
        snprintf(gd, sizeof(gd), " gdg=%d gdm=%d gdgo=%d gdmo=%d"
                 " gdsnap=%d gdsnapd=%.2f mframe=%d gdframe=%d gdslp=%d/%d/%d",
                 gPrev->onGround, gPrev->mode,
                 gCur ? gCur->onGround : -1, gCur ? gCur->mode : -1,
                 gCur ? gCur->snapUid : -1, gCur ? gCur->snapDist : 0.f,
                 mPrev->second.frame, gPrev->gframe,
                 gPrev->onSlope, gCur ? gCur->onSlope : -1, gCur ? gCur->slopeUid : -1);
        char md[224] = "";
        const bool rowsClassifiable =
            mPrev->second.classOk && (mCur == m.end() || mCur->second.classOk);
        if (!g_traceClassMissing.empty() || !rowsClassifiable) {
            snprintf(md, sizeof(md), " mfam=unavailable(%s)",
                     !g_traceClassMissing.empty() ? g_traceClassMissing.c_str()
                                                  : "row short of them");
        } else {
            const TraceRow& p = mPrev->second;
            const TraceRow* nx = (mCur != m.end()) ? &mCur->second : nullptr;
            snprintf(md, sizeof(md),
                     " mg=%d my=%.3f mmini=%d mdx=%.4f mslope=%d/%.4f/%d"
                     " mclamp=%s mcuid=%s morb=%d mband=%.3f/%.3f",
                     p.grounded, p.y, p.mini, p.dx, p.onslope, p.slopem, p.slopet,
                     nx && !nx->clamp.empty() ? nx->clamp.c_str() : "-",
                     nx && !nx->clampuid.empty() ? nx->clampuid.c_str() : "-",
                     nx ? nx->nearorb : -1, p.bandf, p.bandc);
        }
        snprintf(b, sizeof(b), "dpsolve:   [fixup] t=%lld x=%.1f mode=%d in=%d "
                 "dy=%.3f dvy=%.3f kill=%d edy=%.3f edvy=%.3f%s%s%s%s%s (%d total)",
                 t, mPrev->second.x, mPrev->second.mode, act, dyG, dvG, kill,
                 eDy, eDvy, e2, gd, md, revive ? " revive" : "",
                 sameDeath ? " same-death" : "", g_fixupCount);
    }
    writeResult(b);
    const bool physAgrees =
        std::fabs(eDy) < kNoopEps && std::fabs(eDvy) < kNoopEps
        && (!dual || (std::fabs(eDy2) < kNoopEps && std::fabs(eDvy2) < kNoopEps));
    if (!noop && kill != 0 && physAgrees && g_lastTailSolved && !g_phantomLifted
        && g_killVetoIter != g_iter && !g_ckInner) {
        g_killVetoIter = g_iter;
        const long long site = (long long)std::floor((double)g_lastDeathX / 8.0);
        const int n = ++g_phantomHits[site];
        char kb[224];
        snprintf(kb, sizeof(kb), "dpsolve:   [veto] t=%lld is kill-only (the "
                 "physics agrees) - counting it twice at x=%.0f (%d/%d)",
                 t, (double)g_lastDeathX, n, kPhantomAfter);
        writeResult(kb);
    }
    return noop ? FixupMark : FixupReal;
}

inline long long g_verifyDivT = -1;   // the earliest record of the death in hand (runVerified)
inline void autoNoteRecord(long long t, long long deathTick) {
    if (g_verifyDivT < 0 || t < g_verifyDivT) g_verifyDivT = t;
    ++g_autoRecNow;
    if (rungWall() >= 0 && deathTick >= rungWall() - kAutoCreep
        && (g_autoHead < 0 || t < g_autoHead))
        g_autoHead = t;
}

inline void keepRecorderInputs(const std::vector<std::string>& a, long long t0, long long deathTick) {
    std::error_code ec;
    const std::string dir = std::string(DATA_DIR) + "/";
    std::string planSig = "-", bandSig = "-", bandCopy = "-", groupsLine;
    std::string fixSig = "-", fixCopy = "-";
    int k = 0;
    for (size_t i = 0; i + 1 < a.size(); ++i) {
        if (a[i] == "--replay") {
            planSig = fileSig(a[i + 1]);
        } else if (a[i] == "--fixups") {
            fixSig = fileSig(a[i + 1]);
            char fp[256];
            snprintf(fp, sizeof(fp), "dp_fixin_fixups_it%d_t%lld.txt", g_iter, deathTick);
            std::filesystem::copy_file(a[i + 1], dir + fp,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            fixCopy = ec ? std::string("copy-failed") : std::string(fp);
        } else if (a[i] == "--bandtrack") {
            bandSig = fileSig(a[i + 1]);
            char bp[256];
            snprintf(bp, sizeof(bp), "dp_fixin_band_it%d_t%lld.txt", g_iter, deathTick);
            std::filesystem::copy_file(a[i + 1], dir + bp,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            bandCopy = ec ? std::string("copy-failed") : std::string(bp);
        } else if (a[i] == "--groups") {
            const std::string gs = fileSig(a[i + 1]);
            std::string copy;
            auto it = g_fixinGroupsBySig.find(gs);
            if (it != g_fixinGroupsBySig.end()) {
                copy = it->second;
            } else {
                char gp[256];
                snprintf(gp, sizeof(gp), "dp_fixin_groups_it%d_t%lld_%d.txt", g_iter, deathTick, k);
                std::filesystem::copy_file(a[i + 1], dir + gp,
                                           std::filesystem::copy_options::overwrite_existing, ec);
                copy = ec ? std::string("copy-failed") : std::string(gp);
                if (!ec) g_fixinGroupsBySig[gs] = copy;
                if (!ec && gs != "-") g_sigKnown[dir + gp] = gs;   // a byte copy of a file whose sig is gs
            }
            groupsLine += (groupsLine.empty() ? "" : ";") + gs + "=" + copy;
            ++k;
        }
    }
    char mp[256];
    snprintf(mp, sizeof(mp), "dp_attempt_it%d_t%lld.txt", g_iter, deathTick);
    char solveBand[256];
    snprintf(solveBand, sizeof(solveBand), "%sdp_band_it%d_t%lld.txt", dir.c_str(), g_iter, deathTick);
    {
        std::ofstream mf(dir + mp, std::ios::trunc);
        mf << "iteration " << g_iter << "\n"
           << "death_tick " << deathTick << "\n"
           << "plan " << planSig << "\n"
           << "solve_band " << fileSig(solveBand) << "\n"
           << "solve_groups " << (g_solveGroupsSig.empty() ? "-" : g_solveGroupsSig) << "\n"
           << "fixup_band " << bandSig << " " << bandCopy << "\n"
           << "fixup_groups " << (groupsLine.empty() ? "-" : groupsLine) << "\n"
           << "fixup_records " << fixSig << " " << fixCopy << "\n"
           << "coverage " << t0 << " " << deathTick << "\n";
    }
    char b[384];
    snprintf(b, sizeof(b), "dpsolve:   [fixin] it=%d band=%s groups=%s fixups=%s coverage=%lld..%lld",
             g_iter, bandSig.c_str(), groupsLine.empty() ? "-" : groupsLine.c_str(),
             fixSig.c_str(), t0, deathTick);
    writeResult(b);
}

inline int fixupPass(long long t0, const std::string& startArgStr, const std::string& band,
                     long long deathTick) {
    const std::string base = std::string(DATA_DIR) + "/dp_fixup";
    std::vector<std::string> a{"--replay", g_planPath, "--start", startArgStr,
                               "--out", base,
                               "--cap", std::to_string(kCap),
                               "--shipyq", num(kYq), "--shipvq", num(kVq),
                               "--threads", solverThreads()};
    addStartP2dx(a, anchors::row(t0));   // dp --p2xoff
    if (!band.empty()) { a.push_back("--startband"); a.push_back(band); }
    {   // ...and the touch triggers GD had already set off by t0, and the
        const std::string ap = anchorPayloadAll(t0);
        if (!ap.empty()) { a.push_back("--anchor-state"); a.push_back(ap); }
        a.push_back(g_cfg.histRide ? "--anchorride" : "--no-anchorride");
    }
    bool rotQ = false;
    {   // the resim must not fire 2900s the recorded run already consumed either --
        const std::string sr = spentRotArg(t0);
        if (!sr.empty()) { a.push_back("--spentrot"); a.push_back(sr); }
        if (!g_rotObjs.empty()) a.push_back("--trigraw");
        rotQ = rotSeedArgs(t0, a, "resim");
        {
            a.push_back("--rotqtoggle");
            const std::string ts = touchSeedArg(t0);
            if (!ts.empty()) { a.push_back("--touchseed"); a.push_back(ts); }
        }
        // ...and which touch boxes the attempt had reached.
        {
            a.push_back("--touchentered");
            a.push_back(touchEnteredArg(t0));
        }
        // ...and, under dparg --slambox, the boxes GD fired in that attempt (recTouchArg).
        if (solverSlamBox()) {
            a.push_back("--rectouch");
            a.push_back(recTouchArg());
        }
        // ...and, under dparg --formularec, whose attempt the recording and the rows are (recProvArg).
        if (solverFormulaRec()) {
            a.push_back("--recprov");
            a.push_back(recProvArg());
        }
        // ...and, under cfg dpspentpad, the pads it had already fired (spentPadArg).
        if (g_cfg.dpSpentPad) {
            const std::string sp = spentPadArg(t0);
            if (!sp.empty()) { a.push_back("--spentpad"); a.push_back(sp); }
        }
        // ...and the rings (spentOrbArg), and p2's under dp --spentp2 (spentOrb2Arg).
        {
            const std::string so = spentOrbArg(t0);
            if (!so.empty()) { a.push_back("--spentorb"); a.push_back(so); }
            if (solverSpentP2()) {
                const std::string so2 = spentOrb2Arg(t0);
                if (!so2.empty()) { a.push_back("--spentorb2"); a.push_back(so2); }
            }
        }
        // ...and, under cfg dpxtrack, GD's x up to t0 (xtrackArg).
        if (g_cfg.dpXTrack) {
            const std::string xt = xtrackArg(t0);
            if (!xt.empty()) { a.push_back("--xtrack"); a.push_back(xt); }
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(g_fixupPath, ec)) {
        a.push_back("--fixups");
        a.push_back(g_fixupPath);
    }
    addWorldArgs(a);
    if (g_cfg.coinRoute || g_cfg.worldGates) {
        a.push_back(g_cfg.coinRoute ? "--coins" : "--worldgates");
        const std::string ip = std::string(DATA_DIR) + "/items.txt";
        if (std::filesystem::exists(ip, ec)) {
            a.push_back("--items");
            a.push_back(ip);
        }
    }
    for (const std::string& s : g_cfg.dpArgs) a.push_back(s);
    std::filesystem::remove(base + ".trace.csv", ec);
    if (g_fixinPending) {   // the first pass for this death: keep what it reads
        g_fixinPending = false;
        keepRecorderInputs(a, t0, deathTick);
    }
    logSolverArgs(a);
    timedSolve(g_csv, a, "resim");
    logTrigWindow("resim");
    if (rotQ) logRotSeedRead(t0, "resim");
    const long long modelDied = dpbridge::outcome().replayDiedT;
    std::map<long long, TraceRow> m;
    if (!loadTrace(base + ".trace.csv", m)) {
        writeResult("dpsolve:   [fixup] the resim wrote no trace - skipping");
        return 0;
    }
    if (m.find(t0) == m.end()) {
        if (const AnchorRow* a = anchors::row(t0)) {
            TraceRow r;
            r.valid = true;
            r.x = a->x;
            r.y = a->y;
            r.vy = a->vy;
            r.mode = a->mode;
            r.grounded = groundedOf(*a);
            r.dual = a->dual;
            r.y2 = a->y2;
            r.vy2 = a->v2;
            r.act = -1;    // no transition ENDS at t0, so no input is attributed to it
            r.mini = a->mini;
            r.flip = (a->gframe == 3) ? 1 - a->flip : a->flip;
            m[t0] = r;
        }
    }
    int made = 0;     // anything went on file, marks included -- drives the pass loop
    int real = 0;     // ...and of those, the ones that change what the model does
    int skipped = 0;
    long long lastCommon = -1;
    for (auto it = m.begin(); it != m.end(); ++it) {
        const long long t = it->first;
        if (t > deathTick) break;
        const AnchorRow* gr = anchors::row(t);
        if (!gr) continue;
        lastCommon = t;
        const double dy = it->second.y - gr->y;
        const double dv = it->second.vy - gr->vy;
        const bool pair = (it->second.dual != 0 && gr->dual != 0);
        const double dy2 = pair ? it->second.y2 - gr->y2 : 0.0;
        const double dv2 = pair ? it->second.vy2 - gr->v2 : 0.0;
        if (std::fabs(dy) <= kDivergeEps && std::fabs(dv) <= kDivergeEps
            && std::fabs(dy2) <= kDivergeEps && std::fabs(dv2) <= kDivergeEps) continue;
        if (std::fabs(it->second.x - gr->x) > kForkX) {
            char b[200];
            snprintf(b, sizeof(b), "dpsolve:   [fixup] t=%lld is an x fork (%.1f px) - "
                     "not a transition a delta can carry", t, it->second.x - gr->x);
            writeResult(b);
            return made;
        }
        const int w = writeFixup(t, 0, m);
        if (w == FixupBlockedDyn && g_roundRepeated) {
            char b[360];
            snprintf(b, sizeof(b), "dpsolve:   [fixup] t=%lld x=%.1f y=%.1f differs (dy=%.3f "
                     "dvy=%.3f), a record matches, but the solver applies none next to moving "
                     "uid %d - a model wall, not a record; nothing past it is recorded (plan %s)",
                     t, it->second.x, it->second.y, dy, dv, it->second.fxblk,
                     g_prevFlownFnv.empty() ? "-" : g_prevFlownFnv.c_str());
            writeResult(b);
            break;
        }
        if (w == FixupReal || w == FixupMark) {
            ++made;
            if (w == FixupReal) ++real, autoNoteRecord(t, deathTick);
            break;
        }
        if (++skipped <= 3) {
            char b[240];
            snprintf(b, sizeof(b), "dpsolve:   [fixup] t=%lld differs (dy=%.3f dvy=%.3f) but %s "
                     "- looking further", t, dy, dv, fixupWhy(w));
            writeResult(b);
        }
    }
    bool agreedAtDeath = false;
    double gapY = -1.0, gapVy = -1.0;   // how far apart they were there, for the report
    double gapY2 = -1.0, gapVy2 = -1.0;
    {
        auto mp = m.find(deathTick - 1);
        const AnchorRow* gp = anchors::row(deathTick - 1);
        if (mp != m.end() && gp) {
            gapY = std::fabs(mp->second.y - gp->y);
            gapVy = std::fabs(mp->second.vy - gp->vy);
            agreedAtDeath = gapY <= kDivergeEps && gapVy <= kDivergeEps;
            if (mp->second.dual != 0 && gp->dual != 0) {
                gapY2 = std::fabs(mp->second.y2 - gp->y2);
                gapVy2 = std::fabs(mp->second.vy2 - gp->v2);
                if (agreedAtDeath)
                    agreedAtDeath = gapY2 <= kDivergeEps && gapVy2 <= kDivergeEps;
            }
        }
    }
    int verdictWhy = -1;
    if (real == 0) {
        if (modelDied >= 0 && modelDied < deathTick && anchors::row(modelDied + 1)) {
            // The model killed a run GD carried on: the delta record revives it
            verdictWhy = writeFixup(modelDied, 0, m, /*revive=*/true);
            if (verdictWhy == FixupReal) ++real, ++made, autoNoteRecord(modelDied, deathTick);
        } else if ((modelDied < 0 || modelDied > deathTick) && agreedAtDeath
                   && anchors::row(deathTick - 1) && m.find(deathTick) != m.end()) {
            if (!g_lastDeathCoinMiss) {
                verdictWhy = writeFixup(deathTick, 1, m, /*revive=*/false,
                                        /*sameDeath=*/modelDied == deathTick + 1);
                if (verdictWhy == FixupReal) ++real, ++made, autoNoteRecord(deathTick, deathTick);
            }
        }
    }
    if (real == 0) {
        char b[280];
        char p2[64] = "";
        if (gapY2 >= 0.0) snprintf(p2, sizeof(p2), " (p2 %.2f/%.2f)", gapY2, gapVy2);
        snprintf(b, sizeof(b), "dpsolve:   [fixup] nothing recordable: GD died t=%lld, model died "
                 "t=%lld, apart there by %.2f/%.2f%s, compared to t=%lld, marks %d, skipped %d, "
                 "trace %lld..%lld",
                 deathTick, modelDied, gapY, gapVy, p2, lastCommon, made, skipped,
                 m.empty() ? -1 : m.begin()->first, m.empty() ? -1 : m.rbegin()->first);
        writeResult(b);
        const long long vt = (verdictWhy >= 0 && modelDied >= 0 && modelDied < deathTick)
                                 ? modelDied : deathTick;
        auto vp = m.find(vt - 1);
        char c[320];
        if (verdictWhy < 0)
            snprintf(c, sizeof(c), "dpsolve:   [fixup] no verdict record was attempted: "
                     "model died %s, agreed=%d, GD row at model death+1=%d, GD row at t-1=%d, "
                     "model row at t=%d",
                     modelDied < 0 ? "not at all"
                                   : modelDied < deathTick ? "first"
                                   : modelDied > deathTick ? "later" : "on the same tick",
                     agreedAtDeath ? 1 : 0,
                     (modelDied >= 0 && anchors::row(modelDied + 1)) ? 1 : 0,
                     anchors::row(deathTick - 1) ? 1 : 0,
                     m.find(deathTick) != m.end() ? 1 : 0);
        else if (vp == m.end())
            snprintf(c, sizeof(c), "dpsolve:   [fixup] the verdict record at t=%lld was refused: "
                     "%s", vt, fixupWhy(verdictWhy));
        else
            snprintf(c, sizeof(c), "dpsolve:   [fixup] the verdict record at t=%lld was refused: "
                     "%s (key x=%.4f y=%.4f vy=%.4f in=%d mode=%d mini=%d flip=%d g=%d dual=%d)",
                     vt, fixupWhy(verdictWhy), vp->second.x, vp->second.y, vp->second.vy,
                     m.count(vt) ? m.at(vt).act : -1, vp->second.mode, vp->second.mini,
                     vp->second.flip, vp->second.grounded, vp->second.dual);
        writeResult(c);
    }
    return made;
}

inline void recordFixups(long long deathTick) {
    g_verifyDivT = -1;   // this death's own records only
    if (!g_cfg.dpFixups || deathTick < 2 || g_fixupCount >= kFixupCap) return;
    phase::Scope ph("recording what the model got wrong (fixup recorder)", deathTick, g_iter);
    const std::vector<AnchorRow>* saved = anchors::g_src;
    anchors::ladderOn(false);
    struct Restore {
        const std::vector<AnchorRow>* s;
        ~Restore() { anchors::g_src = s; }
    } restore{saved};
    long long t0 = deathTick - kFixupWindow;
    if (t0 < 1) t0 = 1;
    if (g_secPin > 0 && g_secPin < deathTick && t0 < g_secPin) t0 = g_secPin;
    while (t0 < deathTick && !anchors::row(t0)) ++t0;
    const AnchorRow* r = anchors::row(t0);
    if (!r) {
        writeResult("dpsolve:   [fixup] no recorded state near the anchor - skipping");
        return;
    }
    const std::string arg = startArg(t0, *r, heldBefore(g_plan, t0),
                                     swingPendingAt(g_plan, t0));
    std::string band;
    if (r->pmax > r->pmin) band = num(r->pmin) + "," + num(r->pmax);
    const auto fxT0 = std::chrono::steady_clock::now();
    // cfg dpsecauto: the counts belong to one wall; a deeper one starts them again.
    autoSync();
    g_autoRecNow = 0;
    int passes = 0;
    g_fixinPending = g_cfg.researchCapture;
    for (int pass = 1; pass <= kFixupPasses; ++pass) {
        ++passes;
        if (fixupPass(t0, arg, band, deathTick) == 0) break;
    }
    if (rungWall() >= 0 && deathTick >= rungWall() - kAutoCreep)
        g_autoNoRec = (g_autoRecNow == 0) ? g_autoNoRec + 1 : 0;
    // cfg dpsecrent: the recorder's work, counted -- each pass replays the plan from t0 to the death.
    g_autoRoundWork += kWorkRecTick * (double)passes * (double)std::max(0LL, deathTick - t0);
    {
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - fxT0).count();
        char b[160];
        snprintf(b, sizeof(b), "dpsolve:   [fixup] recorder: %d pass(es) in %.0f ms "
                 "(anchor t=%lld, death t=%lld)", passes, ms, t0, deathTick);
        writeResult(b);
    }
}

struct AutoWindow {
    long long t0 = -1;
    long long depth = 0;
    long long horizon = 0;
    std::string from;
    bool pastMax = false;   // cfg dpsecmaxback: the next window would begin further back than that
};
constexpr long long kAutoFirstBack = 200;
constexpr long long kAutoDoubleUpTo = 800;   // doubling up to here, then this much more per try
inline long long autoBack(int level) {
    long long back = kAutoFirstBack;
    for (int i = 0; i < level; ++i)
        back = back < kAutoDoubleUpTo ? back * 2 : back + kAutoDoubleUpTo;
    return back;
}
inline bool autoWindow(AutoWindow& w, int level) {
    const long long wall = rungWall();
    if (wall < 0 || level < 0) return false;
    long long t0 = wall - autoBack(level);
    w.from = std::to_string(autoBack(level)) + " ticks before the death";
    if (g_autoHead > 0 && g_autoHead - 20 < t0) {
        t0 = g_autoHead - 20;
        w.from = "the head of the fixups";
    }
    if (level > 0 && g_autoLastT0 > 0 && t0 >= g_autoLastT0) {
        t0 = g_autoLastT0 - (autoBack(level) - autoBack(level - 1));
        w.from = std::to_string(autoBack(level) - autoBack(level - 1))
                 + " ticks before the last window";
    }
    if (level == 0) {
        if (t0 < wall - kFixupWindow) t0 = wall - kFixupWindow;
        if (g_secPin > t0 && g_secPin <= wall - 100) {
            t0 = g_secPin;
            w.from = "the pin";
        } else if (g_cfg.dpSecPinBack >= 0 && g_secPin > 0 && g_secPin < wall
                   && g_secPin - g_cfg.dpSecPinBack > t0) {
            t0 = g_secPin - g_cfg.dpSecPinBack;
            w.from = "just before the pin";
        }
    }
    if (t0 < 1) {
        t0 = 1;
        w.from = "the start of the level";
    }
    if (g_cfg.dpSecMaxBack > 0 && wall - t0 > g_cfg.dpSecMaxBack) {
        w.pastMax = true;
        return false;
    }
    if (t0 >= wall - 50) return false;
    w.t0 = t0;
    long long margin = g_cfg.dpSecMargin;
    if (g_cfg.dpSecChain && level == 0 && g_autoChain > 0) margin <<= std::min(g_autoChain, 3);
    w.depth = (wall - t0) + margin;
    w.horizon = w.depth + 50;
    return true;
}

inline AutoWindow g_autoLastWin;          // the last window drawn (a cap retry searches it again)
inline bool autoCapRetry() {
    const bool sameWall = g_autoTriedWall >= 0 && rungWall() >= g_autoTriedWall
                          && rungWall() < g_autoTriedWall + kAutoCreep;
    return g_cfg.dpSecCapTiers > 0 && sameWall && g_autoLastDrawn && !g_autoLastFound
           && g_autoLastCapBound && g_autoCapTier < g_cfg.dpSecCapTiers;
}

inline int autoLevel() {
    const bool sameWall = g_autoTriedWall >= 0 && rungWall() >= g_autoTriedWall
                          && rungWall() < g_autoTriedWall + kAutoCreep;
    if (!sameWall) return 0;
    if (autoCapRetry()) return g_autoTries - 1;
    return (g_autoLastT0 >= 0 && g_autoLastT0 <= 1) ? -1 : g_autoTries;
}
inline bool autoTried() { return autoLevel() < 0; }

constexpr long long kStateReach = 4000;   // how far before the first window a size change counts
constexpr long long kStatePad = 100;      // a kept window begins this far before the change
constexpr long long kStatePast = 200;     // ...and ends this far after it
inline long long g_swWall = -1;   // the wall the fields below belong to (g_autoTriedWall)
inline int g_swPhase = 0;         // 0 not asked, 1 probe in flight, 2 kept windows, 3 done
inline long long g_swEvent = -1;  // the tick player 1's size last changed before the first window
inline int g_swSize = -1;         // the size the probe forces and the windows keep (0/1)
inline int g_swTier = 0;          // kept windows fired
inline AutoWindow g_swWin;        // the wall's first window, which the probe searches again
inline int g_reqSize = -1;          // the size the run keeps (0/1), -1 = none
inline long long g_reqFrom = -1;    // ...from the kept window's head
inline long long g_reqUntil = -1;   // ...until it is past this tick (the wall, plus the margin)

inline void stateQueue(const AutoWindow& w, int cap, int kind, int size) {
    g_secReqStart = w.t0;
    g_secReqTarget = 0.0;
    g_secReqDepth = w.depth;
    g_secReqHorizon = w.horizon;
    g_secReqCap = cap;
    g_secReqRung = true;
    g_secReqCoin = -1;
    g_secReqState = kind;
    g_secReqSize = size;
    g_secReqPending = true;
    g_paused = true;
    if (secsolve::g_snapMode == 0 && secsolve::g_verifyEvery == 0) {   // as autoFire
        secsolve::g_snapMode = 2;
        secsolve::g_verifyEvery = 20;
    }
}

inline bool stateReqActive() {
    if (g_reqSize < 0) return false;
    if (g_bestDeath > g_reqUntil) {
        char b[160];
        snprintf(b, sizeof(b), "dpsolve:   [secstate] past t=%lld - player 1's size is free again",
                 g_reqUntil);
        writeResult(b);
        g_reqSize = -1;
        g_reqFrom = g_reqUntil = -1;
        return false;
    }
    return true;
}

inline bool stateFire(const char* why) {
    if (g_swWall != g_autoTriedWall) {
        g_swWall = g_autoTriedWall;
        g_swPhase = 0;
        g_swTier = 0;
        g_swEvent = -1;
        g_swSize = -1;
    }
    const long long wall = rungWall();
    char b[320];
    if (g_swPhase == 0) {
        g_swPhase = 3;   // unless a size change turns up below
        if (stateReqActive()) {
            snprintf(b, sizeof(b), "dpsolve:   [secstate] the run keeps player 1 %s until past t=%lld "
                     "- this wall's windows keep it too, and the size is not questioned",
                     g_reqSize ? "mini" : "normal", g_reqUntil);
            writeResult(b);
            return false;
        }
        g_swWin = g_autoLastWin;
        const std::vector<AnchorRow>& v = anchors::g_deepest;
        auto at = [&](long long t) -> const AnchorRow* {
            return (t >= 0 && (size_t)t < v.size() && v[(size_t)t].valid) ? &v[(size_t)t] : nullptr;
        };
        const AnchorRow* head = at(g_swWin.t0);
        if (!head) return false;
        const long long lo = std::max(1LL, g_swWin.t0 - kStateReach);
        for (long long t = g_swWin.t0; t > lo; --t) {
            const AnchorRow* r = at(t);
            const AnchorRow* p = at(t - 1);
            if (r && p && r->mini != p->mini) { g_swEvent = t; break; }
        }
        if (g_swEvent < 0) return false;
        g_swSize = head->mini ? 0 : 1;
        g_swPhase = 1;
        stateQueue(g_swWin, g_cfg.dpSecCap, 1, g_swSize);
        snprintf(b, sizeof(b), "dpsolve:   [secstate] %s at the wall t=%lld: player 1 turned %s at "
                 "t=%lld, %lld ticks before the first window - that window again from t=%lld at %s "
                 "size, to ask whether the size is what stops it (a probe; nothing is spliced)",
                 why, wall, head->mini ? "mini" : "normal", g_swEvent, g_swWin.t0 - g_swEvent,
                 g_swWin.t0, g_swSize ? "mini" : "normal");
        writeResult(b);
        return true;
    }
    if (g_swPhase == 2) {
        if (g_swTier > g_cfg.dpSecCapTiers) {
            g_swPhase = 3;
            writeResult("dpsolve:   [secstate] the windows that keep the size found nothing - the "
                        "ladder carries on");
            return false;
        }
        AutoWindow w;
        w.t0 = std::max(1LL, g_swEvent - kStatePad);
        w.depth = std::min(g_swEvent + kStatePast, wall + g_cfg.dpSecMargin) - w.t0;
        w.horizon = w.depth + 50;
        w.from = "before the size change";
        const int cap = g_cfg.dpSecCap << g_swTier;
        ++g_swTier;
        stateQueue(w, cap, 2, g_swSize);
        snprintf(b, sizeof(b), "dpsolve:   [secstate] %s at the wall t=%lld - a section solve from "
                 "t=%lld (%lld ticks before player 1's size changed at t=%lld) that keeps it %s, "
                 "alive to t=%lld (depth %lld), cap %d; the model plans on from there",
                 why, wall, w.t0, g_swEvent - w.t0, g_swEvent, g_swSize ? "mini" : "normal",
                 w.t0 + w.depth, w.depth, cap);
        writeResult(b);
        return true;
    }
    return false;
}

inline void stateAdopt(const std::vector<InputCmd>& plan, long long end, float x) {
    char b[200];
    snprintf(b, sizeof(b), "dpsolve:   [secstate] the kept window's splice replaces the deepest plan: "
             "the depth to beat goes %lld -> %lld", g_bestDeath, end);
    writeResult(b);
    g_best = plan;
    g_bestDeath = end;
    g_bestDeathX = x;
    g_spentAnchors.clear();
    // ...and the size is kept until the run is past the wall the probe asked about.
    g_reqSize = g_swSize;
    g_reqFrom = std::max(1LL, g_swEvent - kStatePad);
    g_reqUntil = g_swWall + g_cfg.dpSecMargin;
}

// A rung of cfg dpsecstate is back.
inline void stateRungDone(int kind, bool found) {
    if (kind == 1) {
        g_swPhase = found ? 2 : 3;
        writeResult(found ? "dpsolve:   [secstate] the probe crossed the wall at the other size - "
                            "the next windows begin before the size change and keep it"
                          : "dpsolve:   [secstate] the probe did not cross at the other size "
                            "either - the ladder carries on");
    } else if (kind == 2 && found) {
        g_swPhase = 3;   // spliced; a wall past it starts over
    }
}

inline bool autoFire(const char* why) {
    const bool coinRung = g_cfg.dpSecCoinRung && g_cfg.coinRoute && g_coinWall >= 0
                          && !g_coinWallPlan.empty();
    if (!g_cfg.dpSecAuto || g_secReqPending || (g_bestDeath < 0 && !coinRung) || rungWall() < 0)
        return false;
    {
        const bool pinOut = std::strcmp(why, "every rung past the pin is spent") == 0;
        const long long reach = std::max(g_secEpisodeReach, g_secPin);
        if (g_secEpisodeReach < 0 || (!pinOut && rungWall() > reach + kAutoCreep)) {
            g_secEpisodeWork = 0.0;
            g_secEpisodeRungs = 0;
        }
        g_secEpisodeReach = std::max(reach, rungWall());
        if (g_cfg.dpSecWallBudget > 0.0 && g_secEpisodeWork >= g_cfg.dpSecWallBudget * 1e6) {
            char eb[256];
            snprintf(eb, sizeof(eb), "dpsolve:   [secauto] the section-solve budget of this wall is spent "
                     "(%.1f of %.1f s-equiv over %d rungs since t=%lld) - the fixup loop carries on "
                     "(cfg dpsecwallbudget)", g_secEpisodeWork / 1e6, g_cfg.dpSecWallBudget,
                     g_secEpisodeRungs, g_secEpisodeReach);
            writeResult(eb);
            return false;
        }
    }
    const bool retry = autoCapRetry();   // cfg dpseccaptiers: the last window again, larger
    const int level = autoLevel();
    if (level < 0) return false;
    // cfg dpsecstate: before the wall's second window, whether player 1's size is what stops it.
    if (g_cfg.dpSecState && !retry && level >= 1 && !coinRung && stateFire(why)) return true;
    if (!retry && g_cfg.dpSecCapTiers > 0 && level > 0 && g_autoLastDrawn && !g_autoLastFound
        && !g_autoLastCapBound && g_autoCapTier < g_cfg.dpSecCapTiers)
        writeResult("dpsolve:   [secauto] the last window's cap never bound (nothing capped, or "
                    "the checkpoint path's clamp) - a larger cap would search it the same way; "
                    "the next window instead (cfg dpseccaptiers)");
    if (level == 0 && !retry)
        g_autoChain = (g_secPin > 0 && rungWall() > g_secPin
                       && rungWall() - g_secPin <= (long long)g_cfg.dpSecChainSpan)
                          ? g_autoChain + 1 : 0;
    if (level == 0 && !retry) {
        g_autoTriedWall = rungWall();
        g_autoLastT0 = -1;
    }
    g_autoTries = level + 1;   // spent whether or not a window can be drawn
    g_autoCapTier = retry ? g_autoCapTier + 1 : 0;
    AutoWindow w;
    char b[320];
    if (retry) {
        w = g_autoLastWin;
    } else if (!autoWindow(w, level)) {
        g_autoLastDrawn = false;
        if (rungWall() - autoBack(level) < 1 || w.pastMax) g_autoLastT0 = 1;
        if (w.pastMax)
            snprintf(b, sizeof(b), "dpsolve:   [secauto] %s at t=%lld, but the next window would "
                     "begin more than %d ticks back (cfg dpsecmaxback) - this wall's windows are "
                     "spent", why, rungWall(), g_cfg.dpSecMaxBack);
        else
            snprintf(b, sizeof(b), "dpsolve:   [secauto] %s at t=%lld, but no window fits before "
                     "the death - the loop carries on", why, g_bestDeath);
        writeResult(b);
        return false;
    }
    g_autoLastT0 = w.t0;
    g_autoLastWin = w;
    g_autoLastDrawn = true;
    const int cap = g_cfg.dpSecCap << g_autoCapTier;
    g_secReqStart = w.t0;
    g_secReqTarget = 0.0;            // no x condition (see AutoWindow)
    g_secReqDepth = w.depth;
    g_secReqHorizon = w.horizon;
    g_secReqCap = cap;
    g_secReqRung = true;
    g_secReqCoin = coinRung ? g_coinWallCoin : -1;
    // cfg dpsecstate: a window under the run's size requirement keeps the size (a rung of kind 3).
    const bool keepReq = g_cfg.dpSecState && !coinRung && stateReqActive() && w.t0 >= g_reqFrom;
    if (keepReq) {
        g_secReqState = 3;
        g_secReqSize = g_reqSize;
    }
    g_secReqSpineUntil = (!coinRung && !keepReq) ? g_bestDeath : -1;
    g_secReqPending = true;
    g_paused = true;   // hold the level still until the handoff takes it
    if (secsolve::g_snapMode == 0 && secsolve::g_verifyEvery == 0) {
        secsolve::g_snapMode = 2;
        secsolve::g_verifyEvery = 20;
    }
    snprintf(b, sizeof(b), "dpsolve:   [secauto] %s at the wall t=%lld x=%.1f (rounds %d, "
             "no-record %d, head %lld, try %d) - a section solve from t=%lld (%s), alive to "
             "t=%lld (depth %lld), horizon %lld cap %d", why, rungWall(),
             (double)(rungWall() == g_bestDeath ? g_bestDeathX : 0.f),
             g_autoWallRounds, g_autoNoRec, g_autoHead, level + 1, w.t0, w.from.c_str(),
             w.t0 + w.depth, w.depth, w.horizon, cap);
    writeResult(b);
    if (retry) {
        snprintf(b, sizeof(b), "dpsolve:   [secauto] the same window again (cfg dpseccaptiers): at "
                 "cap %d it found nothing", g_cfg.dpSecCap << (g_autoCapTier - 1));
        writeResult(b);
    }
    if (keepReq) {
        snprintf(b, sizeof(b), "dpsolve:   [secstate] ...keeping player 1 %s (the run needs it until "
                 "past t=%lld)", g_reqSize ? "mini" : "normal", g_reqUntil);
        writeResult(b);
    }
    if (g_cfg.coinRoute) {
        auto coinsAt = [](const std::vector<AnchorRow>& v, long long t) -> int {
            for (long long k = std::min<long long>(t, (long long)v.size() - 1); k >= 0; --k)
                if (v[(size_t)k].valid) return v[(size_t)k].coins;
            return -1;
        };
        const int bestCoins = coinsAt(anchors::g_deepest, g_bestDeath - 1);
        const int ladCoins = coinsAt(anchors::g_dead, g_lastDeathPhys - 1);
        int next = -1;
        for (size_t i = 0; i < solver::g_coins.size() && i < 8; ++i)
            if (ladCoins < 0 || !((ladCoins >> i) & 1)) { next = (int)i; break; }
        snprintf(b, sizeof(b), "dpsolve:   [secauto] coins: the rung's plan (deepest, died t=%lld) "
                 "had 0x%x; the ladder's plan died t=%lld (ranked t=%lld) with 0x%x; first coin it "
                 "lacks: %d", g_bestDeath, bestCoins < 0 ? 0 : bestCoins, g_lastDeathPhys,
                 g_lastDeath, ladCoins < 0 ? 0 : ladCoins, next);
        writeResult(b);
    }
    if (g_cfg.dpSecChain && g_autoChain > 0)
        writeResult("dpsolve:   [secauto] wall " + std::to_string(g_autoChain)
                    + " in a row right behind the pin - margin "
                    + std::to_string((w.t0 + w.depth) - rungWall()) + " ticks");
    return true;
}

inline bool ckLearn(int& learnt) {
    learnt = 0;
    CkDeath d;
    {
        std::lock_guard<std::mutex> g(g_ckMx);
        if (!g_ckDeath.ready) return false;
        d = std::move(g_ckDeath);
        g_ckDeath = CkDeath{};
    }
    dpbridge::cancelSearch(false);
    const int before = g_fixupCount;
    writeInputsFile(g_planPath, d.plan);
    std::vector<InputCmd> ownPlan;
    ownPlan.swap(g_plan);
    g_plan = d.plan;
    anchors::g_dead.swap(d.rows);
    g_ckInner = true;
    recordFixups(d.deathT);
    g_ckInner = false;
    anchors::g_dead.swap(d.rows);
    g_plan.swap(ownPlan);
    learnt = g_fixupCount - before;
    char b[256];
    snprintf(b, sizeof(b), "dpsolve:   [check] checkpoint #%zu (t0=%lld..%lld) died at t=%lld "
             "x=%.1f: %d new record(s) - solving it again; this death is passed from now on",
             d.index, d.t0, d.end, d.deathT, (double)d.deathX, learnt);
    writeResult(b);
    {
        std::lock_guard<std::mutex> g(g_ckMx);
        g_ckDeaf.insert(d.deathT);
    }
    return true;
}

constexpr int kCkRetriesPerRung = 64;

struct ModePortal {
    double x;
    int mode;
};
inline std::vector<ModePortal> g_modePortals;
inline bool g_triedMissedPortal = false;
inline std::string g_forcePortalBand;
inline long long g_forceAnchorT = -1;

inline void loadModePortals(const std::string& csv) {
    g_modePortals.clear();
    std::istringstream in(csv);
    std::string line;
    std::getline(in, line);   // header
    while (std::getline(in, line)) {
        const size_t p1 = line.find(',');
        if (p1 == std::string::npos) continue;
        const size_t p2 = line.find(',', p1 + 1);
        if (p2 == std::string::npos) continue;
        int mode = -1;
        switch (std::atoi(line.c_str() + p1 + 1)) {
            case 5:  mode = 1; break;   // ship
            case 16: mode = 2; break;   // ball
            case 19: mode = 3; break;   // UFO
            case 26: mode = 4; break;   // wave
            case 27: mode = 5; break;   // robot
            case 33: mode = 6; break;   // spider
            case 41: mode = 7; break;   // swing
            default: break;
        }
        if (mode < 0) continue;
        g_modePortals.push_back({std::atof(line.c_str() + p2 + 1), mode});
    }
    std::sort(g_modePortals.begin(), g_modePortals.end(),
              [](const ModePortal& a, const ModePortal& b) { return a.x < b.x; });
}

constexpr long long kPortalSettle = 30;

inline double g_missedPortalX = 0.0;
inline int g_missedPortalMode = -1;

inline long long missedPortalTick() {
    if (g_modePortals.empty() || !anchors::g_src) return -1;
    const long long end = (long long)anchors::g_src->size();
    size_t idx = 0;
    long long best = -1, crossT = -1;
    int wantMode = -1;
    double crossX = 0.0;
    for (long long t = 1; t < end; ++t) {
        const AnchorRow* r = anchors::row(t);
        if (!r) continue;
        if (crossT >= 0 && t >= crossT + kPortalSettle) {
            if (r->mode != wantMode) {
                best = crossT;
                g_missedPortalX = crossX;
                g_missedPortalMode = wantMode;
            }
            crossT = -1;
        }
        while (idx < g_modePortals.size() && (double)r->x >= g_modePortals[idx].x) {
            crossT = t;
            wantMode = g_modePortals[idx].mode;
            crossX = g_modePortals[idx].x;
            ++idx;
        }
    }
    return best;
}

inline bool runLadder(long long dt) {
    std::vector<int> rungs;
    size_t nForced = 0;
    if (g_forceAnchorT > 0 && g_forceAnchorT < dt - kBackOff) {
        rungs.push_back((int)(dt - g_forceAnchorT));
        g_spentAnchors.erase(g_forceAnchorT);   // it has never been tried for THIS reason
        nForced = 1;
    }
    g_forceAnchorT = -1;
    // One-shot: the slit belongs to this hint's rung alone.
    const std::string forceBand = g_forcePortalBand;
    g_forcePortalBand.clear();
    rungs.push_back(g_curBackoff);
    for (int r : kRungs) if (r >= g_curBackoff) rungs.push_back(r);
    if (g_secPin > 0 && dt - g_secPin >= 2) {
        const int pb = (int)(dt - g_secPin);
        auto at = std::lower_bound(rungs.begin() + (std::ptrdiff_t)nForced, rungs.end(), pb);
        if (at == rungs.end() || *at != pb) rungs.insert(at, pb);
    }

    std::vector<InputCmd> tail;
    long long chosenT = -1;
    int chosenBackoff = 0;
    bool solvedTail = false;
    bool tailClaimsGoal = false;   // ...and SOLVED by reaching the end, not by outliving the horizon
    size_t ckRung = (size_t)-1;   // the rung the checkpoint retries below are counting
    int ckRetries = 0;
    int fineTries = 0;            // cfg dpfineretry: fine retries asked, this ladder
    int pinReleases = 0;          // cfg needpinrelease: boxes released behind a pin, this ladder
    for (size_t rungIdx = 0; rungIdx < rungs.size(); ++rungIdx) {
        const int bo = rungs[rungIdx];
        const long long t0 = dt - bo;
        if (t0 < 2) {
            writeResult("dpsolve:   ladder exhausted: backoff " + std::to_string(bo)
                        + " reaches before the start of the run");
            break;
        }
        const bool restartScale = (dt > 200 && t0 < dt / 2);
        if (g_spentAnchors.count(t0)) {
            writeResult("dpsolve:   anchor t=" + std::to_string(t0)
                        + " has already been tried and led nowhere - skipping this rung");
            continue;
        }
        if (g_secPin > 0 && t0 < g_secPin) {
            writeResult("dpsolve:   anchor t=" + std::to_string(t0)
                        + " is before the section solve pinned at t="
                        + std::to_string(g_secPin) + " - skipping this rung");
            continue;
        }
        const AnchorRow* r = anchors::row(t0);
        if (!r) {
            writeResult("dpsolve:   no recorded state for t=" + std::to_string(t0)
                        + " - skipping this rung");
            continue;
        }
        float offX = 0.f;
        const bool offBoard = g_cfg.anchorBoardArm
            ? (offBoardTick(t0, offX) >= 0)
            : (r->pmax > r->pmin && !(r->y >= r->pmin - 1200.f && r->y <= r->pmax + 1200.f));
        if (offBoard) {
            writeResult("dpsolve:   anchor t=" + std::to_string(t0) + " y="
                        + std::to_string((int)r->y) + " is off the board (band "
                        + std::to_string((int)r->pmin) + ".."
                        + std::to_string((int)r->pmax) + ") - skipping this rung");
            continue;
        }
        const int held = heldBefore(g_plan, t0);
        const std::string arg = startArg(t0, *r, held,
                                         swingPendingAt(g_plan, t0));
        std::vector<std::string> a = baseArgs(g_tailPath, (double)r->x);
        if (rungIdx < nForced && !forceBand.empty()) {
            a.push_back("--deadband");
            a.push_back(forceBand);
        }
        bool rotQ = false;
        {   // 2900s the recorded run consumed before this anchor (see spentRotArg)
            const std::string sr = spentRotArg(t0);
            if (!sr.empty()) { a.push_back("--spentrot"); a.push_back(sr); }
            if (!g_rotObjs.empty()) a.push_back("--trigraw");
            // ...and, under cfg dprotseed, the queue, exactly as the fixup resim does.
            rotQ = rotSeedArgs(t0, a, "anchor");
            {
                a.push_back("--rotqtoggle");
                const std::string ts = touchSeedArg(t0);
                if (!ts.empty()) { a.push_back("--touchseed"); a.push_back(ts); }
            }
            // ...and which touch boxes the attempt had reached.
            {
                a.push_back("--touchentered");
                a.push_back(touchEnteredArg(t0));
            }
            // ...and, under dparg --slambox, the boxes GD fired in that attempt, as the resim does.
            if (solverSlamBox()) {
                a.push_back("--rectouch");
                a.push_back(recTouchArg());
            }
            // ...and, under dparg --formularec, the recording's and the rows' attempts, as the resim does.
            if (solverFormulaRec()) {
                a.push_back("--recprov");
                a.push_back(recProvArg());
            }
            // ...and, under cfg dpspentpad, the pads it had already fired, as the resim does.
            if (g_cfg.dpSpentPad) {
                const std::string sp = spentPadArg(t0);
                if (!sp.empty()) { a.push_back("--spentpad"); a.push_back(sp); }
            }
            // ...and the rings (p2's under dp --spentp2).
            {
                const std::string so = spentOrbArg(t0);
                if (!so.empty()) { a.push_back("--spentorb"); a.push_back(so); }
                if (solverSpentP2()) {
                    const std::string so2 = spentOrb2Arg(t0);
                    if (!so2.empty()) { a.push_back("--spentorb2"); a.push_back(so2); }
                }
            }
            // ...and, under cfg dpxtrack, GD's x up to t0, as the resim does.
            if (g_cfg.dpXTrack) {
                const std::string xt = xtrackArg(t0);
                if (!xt.empty()) { a.push_back("--xtrack"); a.push_back(xt); }
            }
        }
        a.push_back("--start");
        a.push_back(arg);
        a.push_back("--startages");   // the human limits across the seam
        a.push_back(kStartAges(g_plan, t0));
        addStartP2dx(a, r);   // dp --p2xoff
        // ...and the coins GD had already credited by t0 (AnchorRow::coins).
        const std::string coinMask = g_cfg.coinRoute ? std::to_string(r->coins) : "";
        if (!coinMask.empty()) { a.push_back("--coinmask"); a.push_back(coinMask); }
        std::string itemBase;
        if ((g_cfg.coinRoute || g_cfg.worldGates) && r->item1 >= 0) {
            itemBase = std::to_string(r->item1) + ":" + std::to_string(r->cnt1);
            if (r->item2 >= 0)
                itemBase += "," + std::to_string(r->item2) + ":"
                          + std::to_string(r->cnt2);
            a.push_back("--itembase");
            a.push_back(itemBase);
        }
        const std::string ap = anchorPayloadAll(t0);
        if (!ap.empty()) { a.push_back("--anchor-state"); a.push_back(ap); }
        a.push_back(g_cfg.histRide ? "--anchorride" : "--no-anchorride");   // as in fixupPass
        std::string band;
        if (r->pmax > r->pmin) {
            band = num(r->pmin) + "," + num(r->pmax);
            a.push_back("--startband");
            a.push_back(band);
        }
        writeResult("dpsolve:   [anchor] att=" + std::to_string(g_attempt)
                    + " --start " + arg
                    + (band.empty() ? "  (band guessed from x)" : "  --startband " + band)
                    + (ap.empty() ? "" : "  --anchor-state " + ap)
                    + (coinMask.empty() ? "" : "  --coinmask " + coinMask)
                    + (itemBase.empty() ? "" : "  --itembase " + itemBase));
        std::error_code ec;
        std::filesystem::remove(g_tailPath, ec);   // a stale tail must not read as this call's
        logSolverArgs(a);
        const int rc = timedSolve(g_csv, a, "anchor");
        logTrigWindow("anchor");
        adoptCoinGates();
        if (rotQ) logRotSeedRead(t0, "anchor");
        if (g_cfg.dpSnapshot) {   // the plan this anchored search emitted (see snapFile)
            const std::string n = snapFile(g_tailPath);
            writeResult("dpsolve: output snap t0=" + std::to_string(t0) + " tail="
                        + (n.empty() ? std::string("-") : n + " " + fileSig(g_tailPath)));
        }
        dpbridge::SolveOutcome o = dpbridge::outcome();   // the fine retry below may replace it
        g_autoRoundWork += workDpCall() + workDpState() * (double)o.workStates;
        if (g_cfg.dpSecRent)
            writeResult("dpsolve:   [work] t0=" + std::to_string(t0) + " states="
                        + std::to_string(o.workStates));
        if (o.verdict == dpbridge::OutcomeCancelled) {
            int learnt = 0;
            if (!ckLearn(learnt)) {
                writeResult("dpsolve:   [check] the search was cancelled with no refuted "
                            "checkpoint waiting (the session is ending) - leaving the job");
                return false;
            }
            if (ckRung != rungIdx) { ckRung = rungIdx; ckRetries = 0; }
            if (++ckRetries <= kCkRetriesPerRung) {
                --rungIdx;     // size_t: wraps at 0 and the ++ in the for brings it back
                continue;
            }
            writeResult("dpsolve:   [check] this rung was cancelled "
                        + std::to_string(kCkRetriesPerRung)
                        + " times - treating it as doomed and backing off");
            continue;
        }
        g_rjTailJoined = o.rejoinT >= 0;
        if (o.rejoinT >= 0) {
            char jb[224];
            if (o.rejoinBadT >= 0)
                snprintf(jb, sizeof(jb), "dpsolve:   [rejoin] joined the plan that last died at "
                         "t=%lld (its walk leaves the old one at t=%lld: %s)", o.rejoinT,
                         o.rejoinBadT, o.rejoinBadWhy ? o.rejoinBadWhy : "?");
            else
                snprintf(jb, sizeof(jb), "dpsolve:   [rejoin] joined the plan that last died at "
                         "t=%lld", o.rejoinT);
            writeResult(jb);
        }        std::vector<InputCmd> cand;
        loadInputsFile(g_tailPath, cand, true);
        const bool haveFile = std::filesystem::exists(g_tailPath, ec);
        const bool forcedRung = (rungIdx < nForced);
        bool usable = haveFile   // the fine retry below may replace it
            && (o.verdict == dpbridge::OutcomeSolved
                || (forcedRung && o.verdict == dpbridge::OutcomePartial
                    && o.deepT > 0 && !cand.empty())
                || (!restartScale && o.verdict == dpbridge::OutcomePartial
                    && o.deepT > dt)
                || (restartScale && o.verdict == dpbridge::OutcomePartial
                    && o.deepT > 0 && o.deepX > (double)g_hudVerifiedX + 40.0));
        char b[448];
        char deep[64] = "";
        if (o.deepT >= 0) snprintf(deep, sizeof(deep), " t=%lld x=%.0f", o.deepT, o.deepX);
        char rd[128] = "";
        if (o.resimDead > 0)
            snprintf(rd, sizeof(rd),
                     " resimdie=%lld@%lld/%s who=%d@(%.1f,%.1f)f%d/0x%08llx",
                     o.resimDead, o.resimFirst, o.resimWhy ? o.resimWhy : "?",
                     o.resimUid, o.resimObjX, o.resimObjY, o.resimFrame,
                     o.resimTrig);
        else if (o.resimDead == 0)
            snprintf(rd, sizeof(rd), " resimdie=0");
        else
            snprintf(rd, sizeof(rd), " resimdie=?");
        const std::string tailFp = cand.empty() ? std::string("0/-")
                                                : planFnv(cand);
        snprintf(b, sizeof(b),
                 "dpsolve:   [%s%s] rc=%d inputs=%zu capHits=%lld tail=%s%s%s",
                 o.verdict == dpbridge::OutcomeSolved ? "SOLVED"
                     : (o.verdict == dpbridge::OutcomePartial ? "PARTIAL" : "FAILED"),
                 deep, rc, cand.size(), o.capHits, tailFp.c_str(), rd,
                 usable ? "" : (!haveFile ? " - no tail written"
                              : (restartScale ? " - doomed, and this far back only a complete "
                                                "route is worth the prefix"
                                              : " - doomed, backing off")));
        writeResult(b);
        const unsigned long long stuckBoxes =
            o.needTrigMask & o.needTrigPassed & ~g_needTrigDropped;
        if (g_cfg.needPinRelease && !usable && stuckBoxes && o.deepT <= 0 && g_secPin > 0
            && g_secPin < t0 && pinReleases < 8) {
            ++pinReleases;
            g_needTrigDropped |= stuckBoxes;
            snprintf(b, sizeof(b), "dpsolve:   the frontier was empty before tick 1 on a box this "
                     "anchor has already passed (mask 0x%llx), and the pin at t=%lld keeps every "
                     "rung past it - released, asking this rung again", stuckBoxes,
                     (long long)g_secPin);
            writeResult(b);
            --rungIdx;     // size_t: wraps at 0 and the ++ in the for brings it back
            continue;
        }
        if (!usable && stuckBoxes && o.deepT <= 0 && !(g_needTrigSuspect & stuckBoxes)) {
            g_needTrigSuspect |= stuckBoxes;
            snprintf(b, sizeof(b), "dpsolve:   the frontier was empty before tick 1: the search "
                     "was told to enter a box this anchor has already passed (mask 0x%llx) - "
                     "noted, released only if the run stops getting anywhere", stuckBoxes);
            writeResult(b);
        }
        const unsigned long long aheadBoxes =
            o.needTrigMask & ~o.needTrigPassed & ~g_needTrigDropped & ~g_needTrigSuspect;
        if (!usable && aheadBoxes && o.deepT > 0) {
            g_needTrigSuspect |= aheadBoxes;
            snprintf(b, sizeof(b), "dpsolve:   a doomed rung was still being asked for boxes "
                     "ahead of it (mask 0x%llx) - noted, released only if the run stops getting "
                     "anywhere", aheadBoxes);
            writeResult(b);
        }
        if (!usable && g_cfg.dpFineRetry && haveFile && o.verdict == dpbridge::OutcomePartial
            && o.capHits == 0 && o.deepT > t0 && fineTries < kFineTriesPerLadder) {
            const AnchorRow* dr = anchors::row(dt);
            if (!dr) dr = anchors::row(dt - 1);
            const bool flying = dr && (dr->mode == 1 || dr->mode == 3);   // ship, UFO (modeIdx)
            if (flying) {
                ++fineTries;
                const long long h = (dt - t0) + kFineLead;
                std::vector<std::string> fa = a;
                for (const std::string& s : {std::string("--cap"), std::to_string(kFineCap),
                                             std::string("--shipyq"), num(kFineYq),
                                             std::string("--shipvq"), num(kFineVq),
                                             std::string("--inputgrid"), std::string("1"),
                                             std::string("--capladder"), std::string("0"),
                                             std::string("--horizon"), std::to_string(h)})
                    fa.push_back(s);   // dp reads the last occurrence of each
                std::filesystem::remove(g_tailPath, ec);
                const int frc = timedSolve(g_csv, fa, "fine");
                const dpbridge::SolveOutcome of = dpbridge::outcome();
                std::vector<InputCmd> fc;
                loadInputsFile(g_tailPath, fc, true);
                const bool fHave = std::filesystem::exists(g_tailPath, ec);
                const bool fUsable = fHave
                    && (of.verdict == dpbridge::OutcomeSolved
                        || (of.verdict == dpbridge::OutcomePartial && of.deepT > dt + 20));
                char fb[256];
                snprintf(fb, sizeof(fb),
                         "dpsolve:   [fine] t0=%lld horizon=%lld rc=%d %s t=%lld x=%.0f "
                         "capHits=%lld inputs=%zu%s", t0, h, frc,
                         of.verdict == dpbridge::OutcomeSolved
                             ? (of.horizonCut ? "SOLVED(horizon)" : "SOLVED")
                             : (of.verdict == dpbridge::OutcomePartial ? "PARTIAL" : "FAILED"),
                         of.deepT, of.deepX, of.capHits, fc.size(),
                         fUsable ? " - taken" : " - doomed too");
                writeResult(fb);
                if (fUsable) {
                    o = of;
                    cand = std::move(fc);
                    usable = true;
                }
            }
        }
        if (!usable) continue;
        tail = std::move(cand);
        chosenT = t0;
        chosenBackoff = bo;
        {
            char tp[512];
            snprintf(tp, sizeof(tp), "%s/dp_tail_it%d_a%lld.txt", DATA_DIR,
                     g_iter, (long long)t0);
            writeInputsFile(tp, tail);
        }
        solvedTail = (o.verdict == dpbridge::OutcomeSolved);
        tailClaimsGoal = solvedTail && !o.horizonCut;
        if (rungIdx < nForced) g_followForced = 6;
        break;
    }
    if (chosenT < 0) {
        if (g_secPin > 0) {
            if (g_cfg.dpSecAuto && dt > g_secPin && !autoTried()) {
                writeResult("dpsolve:   every rung past the pin at t=" + std::to_string(g_secPin)
                            + " is spent - the pin stays, this wall goes to a section solve");
                g_autoPinOut = true;
            } else {
                writeResult("dpsolve:   every rung is before the pin at t="
                            + std::to_string(g_secPin) + " - dropping the pin");
                g_secPin = -1;
            }
        }
        return false;
    }

    std::vector<InputCmd> next;
    for (const InputCmd& c : g_plan) {
        if (c.step >= chosenT) break;
        next.push_back(c);
    }
    const size_t nPrefix = next.size();
    next.insert(next.end(), tail.begin(), tail.end());
    g_plan.swap(next);
    g_curBackoff = chosenBackoff;
    g_lastTailSolved = solvedTail;
    g_planClaimsGoal = tailClaimsGoal;
    g_anchorT = chosenT;
    g_anchorX = anchors::row(chosenT) ? anchors::row(chosenT)->x : 0.f;
    char b[224];
    snprintf(b, sizeof(b), "dpsolve:   re-anchored at t=%lld (backoff %d): %zu kept + %zu new "
             "= %zu inputs", chosenT, chosenBackoff, nPrefix, tail.size(), g_plan.size());
    writeResult(b);
    return true;
}

inline std::string planFnv(const std::vector<InputCmd>& p) {
    uint64_t h = 1469598103934665603ULL;
    for (const InputCmd& c : p) {
        const uint64_t v = (uint64_t)c.step * 2u + (uint64_t)(c.down ? 1 : 0);
        for (int i = 0; i < 8; ++i) {
            h ^= (uint8_t)(v >> (i * 8));
            h *= 1099511628211ULL;
        }
    }
    char out[48];
    snprintf(out, sizeof(out), "%zu/%08x", p.size(),
             (unsigned)(h & 0xffffffffULL));
    return out;
}

enum JobKind { JobFirstSolve = 0, JobLadder = 1, JobSeedPlan = 2 };

inline void reapOrphanedJob(int gen) {
    if (!g_running.load() || !g_finished.load() || g_resultGeneration != gen) return;   // taken already
    const bool live = g_started && !g_sessionOver;
    if (live && gen == g_generation.load()) return;
    g_running = false;
    g_finished = false;
    writeResult("dpsolve: a job finished after its session ended - its result dropped (gen "
                + std::to_string(gen) + ", now " + std::to_string(g_generation.load()) + ")");
    if (!live) releaseSolveMemory("job end");
    else dpbridge::releaseCaches();
    memCensus("job reaped");
}

inline void spawn(int kind, long long arg, const char* phase) {
    g_running = true;
    g_finished = false;
    g_rc = -1;
    g_haveNewPlan = false;
    g_ckFlying = false;
    g_ckInstalled = false;
    g_ckCall = 0;
    g_ckIndex = 0;
    g_ckT0 = -1;
    g_ckEnd = -1;
    g_ckPlan.clear();
    g_ckBase = g_plan;
    g_ckFlights = g_ckPassed = g_ckDeaths = 0;
    g_ckObs = CkObs{};
    g_ckObsSkipped = 0;
    {
        std::lock_guard<std::mutex> g(g_ckMx);
        g_ckDeath = CkDeath{};
        g_ckDeaf.clear();
    }
    dpbridge::cancelSearch(false);    g_t0 = std::chrono::steady_clock::now();
    g_dpSolving = true;    // the badge and the session HUD say SOLVING from here
    g_paused = true;
    g_hudPhase = phase;
    const int gen = g_generation.load();   // this session's generation, read on the main thread
    std::thread([kind, arg, gen]() {
        bool ok = false;
        try {
            if (kind == JobFirstSolve) {
                bool cancelledOut = false;
                bool fromFlight = false;
                for (int tries = 0;; ++tries) {
                    std::error_code ec;
                    std::filesystem::remove(g_planPath, ec);
                    // From the start of the level (cfg dpcontenthorizon reads where that is).
                    std::vector<std::string> a0 = baseArgs(g_planPath, g_startX);
                    addFirstStart(a0);   // ...or from GD's own tick 1 (prepareFirstStart)
                    logSolverArgs(a0);
                    g_rc = timedSolve(g_csv, a0, "first");
                    g_autoRoundWork += workDpCall()
                                       + workDpState() * (double)dpbridge::outcome().workStates;
                    logTrigWindow("head");
                    adoptCoinGates();
                    if (dpbridge::outcome().verdict != dpbridge::OutcomeCancelled) break;
                    // cfg dpcheckfirst: the flight that stopped this solve IS the first plan.
                    if (g_cfg.dpCheckFirst) {
                        CkDeath d;
                        {
                            std::lock_guard<std::mutex> g(g_ckMx);
                            if (g_ckDeath.ready) {
                                d = std::move(g_ckDeath);
                                g_ckDeath = CkDeath{};
                            }
                        }
                        dpbridge::cancelSearch(false);
                        if (d.ready) {
                            g_plan = d.plan;
                            writeInputsFile(g_planPath, g_plan);
                            fromFlight = true;
                            char fb[240];
                            snprintf(fb, sizeof(fb), "dpsolve:   [checkfirst] the first solve "
                                     "stopped at its flight's death t=%lld (checkpoint #%zu, "
                                     "t0=%lld..%lld) - that flight is the first plan", d.deathT,
                                     d.index, d.t0, d.end);
                            writeResult(fb);
                            break;
                        }
                    }
                    int learnt = 0;
                    if (!ckLearn(learnt) || tries + 1 >= kCkRetriesPerRung) {
                        writeResult("dpsolve:   [check] the first solve was cancelled and is not "
                                    "being retried");
                        cancelledOut = true;
                        break;
                    }
                }
                if (!fromFlight && !cancelledOut) {
                    loadInputsFile(g_planPath, g_plan);
                    std::error_code fe;
                    ok = std::filesystem::exists(g_planPath, fe);
                } else {
                    ok = fromFlight;
                }
                g_planClaimsGoal = !fromFlight && ok
                                   && dpbridge::outcome().verdict == dpbridge::OutcomeSolved
                                   && !dpbridge::outcome().horizonCut;
            } else if (kind == JobSeedPlan) {
                g_rc = 0;
                ok = loadInputsFile(g_cfg.dpSeedPlan, g_plan, true);
                g_planClaimsGoal = ok;
            } else {
                const bool flown = g_lastDeath >= 0;
                writeInputsFile(g_planPath, flown ? g_flownPlan : g_plan);
                {
                    char fb[128];
                    snprintf(fb, sizeof(fb), "dpsolve:   [resim] it=%d plan=%s",
                             g_iter,
                             planFnv(flown ? g_flownPlan : g_plan).c_str());
                    writeResult(fb);
                }
                const int fixBefore = g_fixupCount;
                recordFixups(g_lastDeathPhys >= 0 ? g_lastDeathPhys : g_lastDeath);
                {
                    std::vector<std::pair<long long, int>> fe;
                    for (const InputCmd& c : (flown ? g_flownPlan : g_plan))
                        fe.emplace_back((long long)c.step, c.down ? 1 : 0);
                    writeResult(dpbridge::runVerified(
                        fe, g_lastDeathPhys >= 0 ? g_lastDeathPhys : g_lastDeath,
                        (double)g_lastDeathX, false, g_verifyDivT));
                }
                const bool rankedAway = g_lastDeathPhys >= 0 && g_lastDeathPhys != g_lastDeath;
                if (g_fixupCount > fixBefore && rankedAway)
                    writeResult("dpsolve:   the model learnt something past the coin this death was "
                                "ranked at - the ladder keeps its rung");
                const bool wallCapped = g_cfg.dpLearnResets >= 0
                                        && g_learnResetWall == g_bestDeath
                                        && g_learnResets >= g_cfg.dpLearnResets;
                if (g_fixupCount > fixBefore && !rankedAway && wallCapped) {
                    char lb[200];
                    snprintf(lb, sizeof(lb), "dpsolve:   the model learnt something again at the "
                             "wall t=%lld (%d reset%s there already) - the ladder keeps its rung",
                             g_bestDeath, g_learnResets, g_learnResets == 1 ? "" : "s");
                    writeResult(lb);
                }
                if (g_fixupCount > fixBefore && !rankedAway && !wallCapped) {
                    if (g_learnResetWall != g_bestDeath) g_learnResets = 0;
                    g_learnResetWall = g_bestDeath;
                    ++g_learnResets;
                    g_spentAnchors.clear();
                    g_curBackoff = kBackOff;
                    writeResult("dpsolve:   the model learnt something - every anchor is worth "
                                "another look, starting from the shallowest");
                }
                AutoWindow aw;
                if (g_autoRecordThenRung && autoWindow(aw, autoLevel())) {
                    ok = false;
                } else {
                    g_autoRecordThenRung = false;
                    ok = runLadder(arg);
                }
            }
        } catch (...) {
            ok = false;    // never let an exception cross back into the game's frame
        }
        g_haveNewPlan = ok;
        g_resultGeneration = gen;   // set before g_finished, same ordering convention as g_rc
        g_finished = true;
        // ...and if its session has ended meanwhile, nothing will take the result (reapOrphanedJob)
        Loader::get()->queueInMainThread([gen] { reapOrphanedJob(gen); });
    }).detach();
}

inline bool spawnIsModelDefault(const AnchorRow& r) {
    return std::fabs(r.x) < 0.01f && std::fabs(r.y - 105.f) < 0.01f && r.mode == 0
        && r.flip == 0 && r.mini == 0 && r.dual == 0 && r.gframe == 0
        && std::fabs(r.speed - 0.9f) < 1e-3f;   // 0.9 = GD's 1x
}

inline std::string g_firstStart, g_firstBand;

inline void prepareFirstStart() {
    g_firstStart.clear();
    g_firstBand.clear();
    const AnchorRow& r = g_spawnRow;
    if (!r.valid || spawnIsModelDefault(r)) return;
    g_firstStart = startArg(1, r, 0);
    if (r.pmax > r.pmin) g_firstBand = num(r.pmin) + "," + num(r.pmax);
    char b[400];
    snprintf(b, sizeof(b), "dpsolve:   [spawn] GD starts the player at x=%.3f y=%.3f mode=%d "
             "mini=%d flip=%d dual=%d speed=%.3f frame=%d, not where the model does (a 1x cube "
             "at 0,105) - the first solve starts from GD's tick 1: --start %s%s%s",
             (double)r.x, (double)r.y, r.mode, r.mini, r.flip, r.dual, (double)r.speed, r.gframe,
             g_firstStart.c_str(), g_firstBand.empty() ? "" : " --startband ",
             g_firstBand.c_str());
    writeResult(b);
}

inline void addFirstStart(std::vector<std::string>& a) {
    if (g_firstStart.empty()) return;
    a.push_back("--start");
    a.push_back(g_firstStart);
    if (!g_firstBand.empty()) {
        a.push_back("--startband");
        a.push_back(g_firstBand);
    }
}

inline void beginFirstAttempt() {
    prepareFirstStart();
    if (!g_cfg.dpSeedPlan.empty()) {
        writeResult("dpsolve: seeding the plan from " + g_cfg.dpSeedPlan
                    + " instead of solving for it - the game still verifies it for real");
        spawn(JobSeedPlan, 0, "verifying a seeded plan before searching from where it lands");
    } else {
        spawn(JobFirstSolve, 0, "the level is held still until the plan is ready");
    }
}

inline bool refuseIfPlatformer(GJBaseGameLayer* l) {
    bool plat = l->m_isPlatformer;
    if (auto* pl = PlayLayer::get())
        if (pl->m_level && pl->m_level->isPlatformer()) plat = true;
    if (!plat) return false;
    if (g_sessionOver) return true;   // one ending per session
    const char* note =
        "gdsolver: this is a PLATFORMER level - not solving it";
    writeResult("dpsolve: refusing - a platformer has steering and no forced "
                "scroll, so \"the input at tick t\" does not describe the run. "
                "The solver is not wrong here, it does not apply. Nothing was "
                "searched and no plan was produced.");
    notify::show(note, NotificationIcon::Error, 6.f);
    g_paused = true;
    endSession("platformer_unsupported");
    return true;
}

inline std::string cfgDiffLine(const std::string& defs, const std::string& now) {
    auto split = [](const std::string& s) {
        std::vector<std::pair<std::string, std::string>> v;
        std::istringstream in(s);
        for (std::string tok; in >> tok;) {
            const size_t eq = tok.find('=');
            v.emplace_back(tok.substr(0, eq), eq == std::string::npos ? "" : tok.substr(eq + 1));
        }
        return v;
    };
    std::map<std::string, std::string> d;
    for (auto& kv : split(defs)) d.insert(std::move(kv));
    std::string out;
    for (const auto& [k, v] : split(now)) {
        const auto it = d.find(k);
        if (it != d.end() && it->second == v) continue;
        if (!out.empty()) out += ' ';
        out += k + "=" + v + "(" + (it == d.end() ? std::string("?") : it->second) + ")";
    }
    return out.empty() ? "-" : out;
}

inline void start(GJBaseGameLayer* l) {
    if (!l) return;
    if (refuseIfPlatformer(l)) return;
    if (!g_cfgRemoved.empty()) {
        for (const auto& m : g_cfgRemoved) writeResult("cfg: " + m);
        writeResult("dpsolve: refused - autorun.cfg names " + std::to_string(g_cfgRemoved.size())
                    + " removed key(s)");
        g_paused = true;
        endSession("removed_cfg");
        return;
    }
    ++g_generation;
    if (g_running.load()) {
        g_pendingLayer = l;
        g_paused = true;
        writeResult("dpsolve: start queued behind a job still running");
        return;
    }
    g_pendingLayer = nullptr;
    if (levelslice::maybeSlice(l)) return;
    std::vector<InputCmd> carried;
    if (levelslice::takeLight(carried)) {
        std::sort(carried.begin(), carried.end(),
                  [](const InputCmd& a, const InputCmd& c) { return a.step < c.step; });
        g_plan = std::move(carried);
        g_cfg.inputs = g_plan;
        g_paused = false;
        writeResult("dpsolve: " + std::string(levelslice::g_phase == levelslice::Verifying
                                                  ? "check flight on the level itself"
                                                  : levelslice::g_phase == levelslice::RefFlight
                                                        ? "first-attempt flight on a fresh copy"
                                                        : "back on the copy, where the loop was")
                    + " (" + std::to_string(g_plan.size()) + " inputs, round "
                    + std::to_string(g_iter) + ")");
        return;
    }
    const bool carry = levelslice::takeCarry(carried);
    if (!carry) captureSecsolveSettings();
    if (!carry) {
        std::string why;
        if (!dpbridge::runBegin(g_cfg.levelId, (unsigned long long)g_cfg.dpSeed, why)) {
            writeResult("dprun: refused - " + why);
            g_paused = true;
            endSession("run_carryover");
            return;
        }
        writeResult("dprun: begin level=" + std::to_string(g_cfg.levelId) + " seed="
                    + std::to_string(g_cfg.dpSeed));
    }
    std::ostringstream oss;
    solver::writeObjRects(oss, l);
    g_csv = oss.str();
    g_csvSig = bytesSig(g_csv);
    {
        const dpbridge::LevelStats st = dpbridge::statsFromCsv(g_csv);
        if (!st.unsupported.empty()) {
            writeResult("dpsolve: unsupported level - " + st.unsupported
                        + ". Nothing was searched and no plan was produced.");
            notify::show("gdsolver: unsupported level - " + st.unsupported,
                         NotificationIcon::Error, 6.f);
            g_paused = true;
            endSession("unsupported_level");
            return;
        }
    }
    loadModePortals(g_csv);    // where the mode portals are; see missedPortalTick
    loadRotObjs(g_csv);        // ...and the 2900s, for --spentrot (see spentRotArg)
    rotSeedLevelReset();       // ...and cfg dprotseed's queue order and witnesses
    if (g_cfg.dpRotSeed > 0 && rotQueueRequested()) {
        writeResult("dpsolve: cfg dprotseed REFUSED - cfg dparg=--rotqueue already runs the "
                    "queue on every call; dprotseed is off for this session");
        g_cfg.dpRotSeed = 0;
    }
    g_triedMissedPortal = false;
    g_forceAnchorT = -1;
    dpbridge::checkSubscribe(g_cfg.dpCheck);
    dpbridge::cancelSearch(false);
    const int menuMode = Mod::get()->getSettingValue<bool>("solve-gamble") ? 3 : 0;
    g_plainBesideMode = g_cfg.dpPlainBeside >= 0 ? g_cfg.dpPlainBeside : menuMode;
    if (g_plainBesideMode == 3)
        writeResult("dpsolve: solving under the gamble (dpplainbeside=3): the plain search is "
                    "taken as soon as it will do, so this run is not reproducible");
    g_planPath = std::string(DATA_DIR) + "/dp_plan.txt";
    g_tailPath = std::string(DATA_DIR) + "/dp_tail.txt";
    g_fixupPath = std::string(DATA_DIR) + "/dp_fixups.txt";
    g_fixupNoopPath = std::string(DATA_DIR) + "/dp_fixups_noop.txt";
    g_groupsPath = std::string(DATA_DIR) + "/dp_groups.txt";
    g_groupsDeepPath = std::string(DATA_DIR) + "/dp_groups_deep.txt";
    g_groupsBootPath = std::string(DATA_DIR) + "/dp_groups_boot.txt";
    if (!carry) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(DATA_DIR, ec)) {
            const std::string n = e.path().filename().string();
            if (n.rfind("dp_died_it", 0) == 0 || n.rfind("dp_tail_it", 0) == 0
                || n.rfind("dp_band_it", 0) == 0 || n.rfind("dp_groups_it", 0) == 0
                || n.rfind("dp_fixin_", 0) == 0 || n.rfind("dp_attempt_it", 0) == 0)
                std::filesystem::remove(e.path(), ec);
        }
    }
    {
        std::error_code ec;
        // A carried run keeps its fixups: they are keyed by the player's state, not by an object.
        if (!carry) {
            std::filesystem::remove(g_fixupPath, ec);
            std::filesystem::remove(g_fixupNoopPath, ec);
        }
        std::filesystem::remove(g_groupsPath, ec);   // the recordings are this run's, too
        std::filesystem::remove(g_groupsDeepPath, ec);
        std::filesystem::remove(g_groupsBootPath, ec);
    g_sigKnown.clear();   // the recordings it described are gone
        std::filesystem::remove(std::string(DATA_DIR) + "/grouptrace_last.txt", ec);
    }
    if (!carry) {
        g_fixupCount = 0;
        g_fixupNoop = 0;
    }
    g_groupsDepth = -1;
    g_groupsSerial = -1;
    g_groupsCopiedSig.clear();
    g_fixinGroupsBySig.clear();
    g_fixinPending = false;
    g_solveGroupsSig.clear();
    g_groupsX.clear();
    g_coinWall = -1;
    g_coinWallCoin = -1;
    g_coinWallPlan.clear();
    g_groupsDeepX.clear();
    g_groupsBootX.clear();
    g_retimeLeftOut = 0;
    g_deepActive = false;
    g_recordAttempt = false;
    g_deepDoneAt = -1;
    g_recordKind = RecNone;
    g_recordRequest = false;
    g_coldRestarted = false;
    g_escalations = 0;
    g_capTier = 0;
    g_topTierIter = 0;
    g_needTrigDropped = 0;
    g_needTrigSuspect = 0;
    g_spentAnchors.clear();
    g_learnResetWall = -1;
    g_learnResets = 0;
    if (!carry) {
        g_phantomHits.clear();
        g_phantomScale.clear();
        g_phantomBands.clear();
        g_phantomLifted = false;
    }
    g_voidLowRead = false;   // cfg voidband: this session's level
    g_voidLow.clear();
    g_voidDt = -1;
    g_voidT0 = -1;
    g_voidX0 = 0.f;
    g_killVetoIter = -1;
    g_forcePortalBand.clear();
    g_cfg.noDeath = false;
    g_horizonFull = g_cfg.dpHorizon;
    if (g_horizonFull <= 0) {
        g_horizonFull = (int)(solver::g_levelMaxX) + 2000;
        if (g_horizonFull < 3000) g_horizonFull = 3000;
    }
    g_horizon = g_horizonFull;     // the bound the no-death pass is allowed
    // Adaptive mode opens with the whole level: nothing is known yet about where the model is wrong.
    g_horizonNow = g_horizonFull;
    // cfg dpcontenthorizon: ...unless the level says where it is likely to be (horizonFor).
    g_horizonFullAsked = false;
    g_newGimmickX.clear();
    g_startX = 0.0;
    if (l && l->m_player1) g_startX = l->m_player1->getPositionX();
    if (l && l->m_objects) {
        for (unsigned i = 0; i < l->m_objects->count(); ++i) {
            auto* o = static_cast<GameObject*>(l->m_objects->objectAtIndex(i));
            if (o && isNewGimmick(o->m_objectID, (int)o->m_objectType))
                g_newGimmickX.push_back((float)o->m_positionX);
        }
        std::sort(g_newGimmickX.begin(), g_newGimmickX.end());
    }
    if (g_cfg.dpContentHorizon) {
        char hb[200];
        snprintf(hb, sizeof(hb), "dpsolve:   [content] %zu objects newer than 1.7 (first at x=%.0f) "
                 "- the first solve plans %d ticks of %d", g_newGimmickX.size(),
                 g_newGimmickX.empty() ? -1.0 : (double)g_newGimmickX.front(),
                 horizonFor(g_startX), g_horizonFull);
        writeResult(hb);
    }
    g_wallRepeats = 0;
    g_triedWholeLevel = false;
    g_prevFlownFnv.clear();
    g_prevFlownDeath = -1;
    g_roundRepeated = false;
    g_flownDeaths.clear();
    g_rjAfterTick = -1;
    g_rjTailJoined = false;
    g_rjOldIsJoin = false;
    g_rjKeepTarget = false;
    {
        std::error_code ec;
        std::filesystem::remove(rejoinTracePath(), ec);
    }
    g_stallRuns = 0;
    g_kLimitWall = g_kLimitTested = -1;
    g_kHumanFrom = 0;
    g_needUnseen = false;
    if (!carry) g_iter = 0;
    g_plan.clear();
    g_best.clear();
    g_flownPlan.clear();
    g_bestDeath = -1;
    g_bestDeathX = 0.f;
    g_secPin = -1;
    g_secPinWall = -1;
    g_secRung = false;
    g_autoWall = -1;
    g_autoWallRounds = 0;
    g_autoWallWork = 0.0;
    g_autoRoundWork = 0.0;
    g_secEpisodeWork = 0.0;
    g_secEpisodeReach = -1;
    g_secEpisodeRungs = 0;
    g_autoRungWork = 0.0;
    g_autoRungN = 0;
    g_autoHead = -1;
    g_autoNoRec = 0;
    g_autoRecNow = 0;
    g_autoTriedWall = -1;
    g_autoTries = 0;
    g_autoLastT0 = -1;
    g_autoCapTier = 0;
    g_autoLastDrawn = false;
    g_autoLastWin = AutoWindow{};   // ...and the window drawn (stateFire reads it at a wall's second try)
    g_autoLastFound = true;
    g_autoLastCapBound = false;
    g_autoPinOut = false;
    g_autoChain = 0;
    g_autoRecordThenRung = false;
    g_autoRecordWhy.clear();
    g_swWall = -1;           // cfg dpsecstate
    g_swPhase = 0;
    g_swEvent = -1;
    g_swSize = -1;
    g_swTier = 0;
    g_swWin = AutoWindow{};
    g_reqSize = -1;
    g_reqFrom = -1;
    g_reqUntil = -1;
    g_lastDeath = -1;
    g_coinMissPending = false;
    g_lastDeathCoinMiss = false;
    g_coinMissPostPending = false;
    g_postMissCoin = -1;
    g_postCoins = 0;
    for (long long& t : g_postCoinT) t = -1;
    g_routeNeedUid = -1;     // cfg routeprereq: a run's walls are its own
    g_routeNeedDeath = -1;
    g_routeEngaged = 0;
    for (int& n : g_routeRounds) n = 0;
    route::g_onTickDeepest.assign(route::g_prereq.size(), -1);
    g_lastDeathPhys = -1;
    if (!carry) g_coinMarginNow = 0.0;   // what the game refused so far (a carried run keeps it)
    g_planClaimsGoal = false;
    g_deathRuns.clear();
    g_curBackoff = kBackOff;
    g_lastTailSolved = false;
    g_followSolved = 0;
    g_followPeak = -1;
    g_followForced = 0;
    g_anchorT = -1;
    g_phaseLabelsLive = true;   // g_iter was set above (carry or 0) and g_anchorT now: the labels are this level's
    g_lastDeathX = 0.f;   // inert while g_lastDeath gates its only reader, but it is the same
                          // per-run pair and the two must not drift apart
    g_stop = false;
    g_showRequest = false;
    g_dpShowSolution = false;
    g_psnapPlayerWritten = false;   // tidyPlayerModes: per solve
    g_maxPlayYLive = 0.f;
    g_argsLogged = false;
    g_argsLast.clear();   // ...or level N+1's first solve compares against level N's
    anchors::reset();
    cpflight::reset();          // ...and the checkpoints kept with them (cfg cpflight)
    g_solveMemHeld = true;      // ...and let go of again when the solving is over (releaseSolveMemory)
    if (!carry) cpflight::resetCounts();   // the session's tally goes on across a carried run
    g_cpFlightSummary = &cpflight::summary;   // with the flag off too: its counts are then 0
    g_spawnRow = AnchorRow{};   // ...and so is the spawn: level N's must not start level N+1
    writeResult("dpsolve: " + std::string(carry ? "resume" : "start") + " level="
                + std::to_string(g_cfg.levelId)
                + " csv=" + std::to_string(g_csv.size()) + " bytes horizon="
                + std::to_string(g_horizon));
    {
        const std::string eff = effcfg::modCfg();
        writeResult("cfgeff: " + eff);
        writeResult("cfgdiff: " + (g_cfgDefaults.empty()
                                       ? std::string("(no autorun.cfg was read, no defaults to compare)")
                                       : cfgDiffLine(g_cfgDefaults, eff)));
        writeResult("dpdefaults: " + dpbridge::defaultsProfile());
    }
    if (carry) {
        std::sort(carried.begin(), carried.end(),
                  [](const InputCmd& a, const InputCmd& c) { return a.step < c.step; });
        g_plan = std::move(carried);
        g_cfg.inputs = g_plan;
        g_paused = false;
        writeResult("dpsolve: resume - flying the carried plan (" + std::to_string(g_plan.size())
                    + " inputs) after " + std::to_string(g_iter) + " rounds, "
                    + std::to_string(g_fixupCount) + " fixups kept");
        return;
    }
    if (!startBootstrapRecord())
        beginFirstAttempt();
}

inline std::string fileSigScan(const std::string& path);
inline std::string fileSig(const std::string& path) {
    const auto known = g_sigKnown.find(path);
    if (known != g_sigKnown.end()) {
        if (g_cfg.dpSigCheck) {   // cfg dpsigcheck (debug): the scan as well, and say if they differ
            const std::string scan = fileSigScan(path);
            if (scan != known->second) {
                writeResult("dpsolve:   [sigcheck] MISMATCH " + path + " known=" + known->second +
                            " scan=" + scan);
                return scan;
            }
        }
        return known->second;
    }
    return fileSigScan(path);
}
inline std::string fileSigScan(const std::string& path) {
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return "-";
    std::ifstream f(path, std::ios::binary);
    if (!f) return "-";
    uint64_t h = 1469598103934665603ULL;
    size_t n = 0;
    char buf[65536];
    while (f.read(buf, sizeof(buf)) || f.gcount()) {
        const size_t got = (size_t)f.gcount();
        n += got;
        for (size_t i = 0; i < got; ++i) {
            h ^= (uint8_t)buf[i];
            h *= 1099511628211ULL;
        }
    }
    char out[48];
    snprintf(out, sizeof(out), "%zu/%08x", n, (unsigned)(h & 0xffffffffULL));
    return out;
}
inline bool copyRecording(const std::string& src, const std::string& dst) {
    g_sigKnown.erase(dst);
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    uint64_t h = 1469598103934665603ULL;
    size_t n = 0;
    std::vector<char> buf(1 << 20);
    while (in.read(buf.data(), (std::streamsize)buf.size()) || in.gcount()) {
        const size_t got = (size_t)in.gcount();
        n += got;
        for (size_t i = 0; i < got; ++i) {
            h ^= (uint8_t)buf[i];
            h *= 1099511628211ULL;
        }
        out.write(buf.data(), (std::streamsize)got);
        if (!out) return false;
    }
    out.close();
    if (!out) return false;
    char sig[48];
    snprintf(sig, sizeof(sig), "%zu/%08x", n, (unsigned)(h & 0xffffffffULL));
    g_sigKnown[dst] = sig;
    return true;
}

inline void logFingerprint(long long dt, double deathX) {
    if (!g_cfg.dpFingerprint) return;
    const std::string ph = planFnv(g_plan);
    // The band the ATTEMPT started in, not the one it died in (see the note above).
    const std::vector<AnchorRow>* savedSrc = anchors::g_src;
    anchors::ladderOn(false);
    const AnchorRow* r1 = anchors::row(1);
    anchors::g_src = savedSrc;
    char band[48] = "-";
    if (r1) snprintf(band, sizeof(band), "%.1f,%.1f", (double)r1->pmin, (double)r1->pmax);
    char b[384];
    snprintf(b, sizeof(b),
             "dpsolve:   [fp] it=%d plan=%s att=%d t=%lld x=%.3f band1=%s "
             "fix=%d/%s horizon=%d backoff=%d live=%s deep=%s",
             g_iter, ph.c_str(), g_attempt,
             dt, deathX, band, g_fixupCount, fileSig(g_fixupPath).c_str(),
             g_horizonNow, g_curBackoff, fileSig(g_groupsPath).c_str(),
             fileSig(g_groupsDeepPath).c_str());
    writeResult(b);
    memCensus("round");   // cfg memcensus (print only)
    {
        char pp[512];
        snprintf(pp, sizeof(pp), "%s/dp_died_it%d_t%lld.txt", DATA_DIR,
                 g_iter, dt);
        writeInputsFile(pp, g_plan);
    }
    {
        std::error_code ec;
        const std::string bandSrc = std::string(DATA_DIR) + "/dp_band.txt";
        if (g_cfg.researchCapture) {
            char bp[512];
            snprintf(bp, sizeof(bp), "%s/dp_band_it%d_t%lld.txt", DATA_DIR, g_iter, dt);
            if (std::filesystem::exists(bandSrc, ec))
                std::filesystem::copy_file(bandSrc, bp,
                                           std::filesystem::copy_options::overwrite_existing, ec);
        }
        const std::string gs = fileSig(g_groupsPath);
        g_solveGroupsSig = gs;   // for the attempt's manifest (keepRecorderInputs)
        if (g_cfg.researchCapture && !g_groupsPath.empty()
            && std::filesystem::exists(g_groupsPath, ec) && gs != g_groupsCopiedSig) {
            char gp[512];
            snprintf(gp, sizeof(gp), "%s/dp_groups_it%d_t%lld.txt", DATA_DIR, g_iter, dt);
            std::filesystem::copy_file(g_groupsPath, gp,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (!ec) g_groupsCopiedSig = gs;
        }
        char b2[256];
        snprintf(b2, sizeof(b2), "dpsolve:   [inputs] it=%d band=%s groups=%s capture=%s", g_iter,
                 fileSig(bandSrc).c_str(), gs.c_str(), g_cfg.researchCapture ? "on" : "off");
        writeResult(b2);
    }
}

inline void releaseSolveMemory(const char* where) {
    if (jobInFlight()) {
        writeResult(std::string("dpsolve: memory kept at ") + where + " - a search is still running");
        return;
    }
    g_solveMemHeld = false;
    auto mb = [](size_t n) { return (double)n / (1024.0 * 1024.0); };
    const int before = postmortem::privateMB();
    const size_t a0 = anchors::heldBytes(), s0 = secsolve::nodeTableBytes(), c0 = dpbridge::cacheBytes();
    anchors::release();
    cpflight::reset();
    const bool sec = !secsolve::inFlight();
    if (sec) secsolve::freeNodeTables();
    const bool core = dpbridge::releaseCaches();
    char b[320];
    snprintf(b, sizeof(b), "dpsolve: memory released at %s - private %d -> %d MB; held anchor rows %.1f -> %.1f, "
             "section tables %.1f -> %.1f%s, solver caches %.1f -> %.1f MB%s", where, before,
             postmortem::privateMB(), mb(a0), mb(anchors::heldBytes()), mb(s0), mb(secsolve::nodeTableBytes()),
             sec ? "" : " (kept: in flight)", mb(c0), mb(dpbridge::cacheBytes()),
             core ? "" : " (kept: a search beside the ladder)");
    writeResult(b);
}

inline void memCensus(const char* where) {
    if (!g_cfg.memCensus) return;
    auto mb = [](size_t n) { return (double)n / (1024.0 * 1024.0); };
    const cpflight::Held cf = cpflight::held();
    size_t seekBytes = 0;
    for (const auto& kv : seekcp::g_cps) if (kv.second) seekBytes += cpflight::recBytes(*kv.second);
    int gdcp = -1;
    if (auto* pl = PlayLayer::get())
        if (pl->m_checkpointArray) gdcp = (int)pl->m_checkpointArray->count();
    char dp[32] = "busy", dpg[32] = "busy";
    if (!jobInFlight()) {
        snprintf(dp, sizeof(dp), "%.1f", mb(dpbridge::cacheBytes()));
        snprintf(dpg, sizeof(dpg), "%.1f", mb(dpbridge::groupCacheBytes()));
    }
    const std::time_t now = std::time(nullptr);
    char clock[16] = "?";
    if (const std::tm* tm = std::localtime(&now)) std::strftime(clock, sizeof(clock), "%H:%M:%S", tm);
    char b[400];
    snprintf(b, sizeof(b),
             "memcensus: %s %s priv=%d ws=%d it=%d | anchors=%.1f cpf=%d/%zu/%.1f seekcp=%zu/%.1f sec=%.1f "
             "map=%.1f log=%.1f csv=%.1f slice=%.1f gt=%.1f dp=%s dpgroups=%s gdcp=%d",
             where, clock, postmortem::privateMB(), postmortem::workingSetMB(), g_iter,
             mb(anchors::heldBytes()), cf.sets, cf.recs, mb(cf.bytes), seekcp::g_cps.size(), mb(seekBytes),
             mb(secsolve::nodeTableBytes()), mb(itermap::heldBytes()),
             mb(solver::g_log.capacity() * sizeof(solver::TickInfo)), mb(g_csv.capacity()),
             mb(levelslice::heldBytes()), mb(grouptrace::g_objs.capacity() * sizeof(grouptrace::Tracked)), dp, dpg,
             gdcp);
    writeResult(b);
    log::info("{}", b);
}

inline void giveUp(const char* reason, const char* sessionWhy) {
    if (g_sessionOver) return;   // one ending per session
    // Giving up on a slice's copy is the copy's verdict, not the level's (level_slice.hpp).
    if (levelslice::onGiveUp(reason)) return;
    double lvl = 0.0;
    if (auto* pl = PlayLayer::get()) lvl = pl->m_levelLength;
    const double pct = (lvl > 1.0) ? (double)g_hudVerifiedX / lvl * 100.0 : 0.0;
    char note[224];
    snprintf(note, sizeof(note),
             "gdsolver: could not solve lv%d - stopped at %.0f%% (x %.0f) after %d round%s",
             g_cfg.levelId, pct, (double)g_hudVerifiedX, g_iter, g_iter == 1 ? "" : "s");
    writeResult(std::string("dpsolve: giving up - ") + reason + " | " + note);
    if (itermap::save(g_cfg.levelId, false))
        writeResult("dpsolve: itermap saved -> " + itermap::pathFor(g_cfg.levelId));
    notify::show(note, NotificationIcon::Error, 6.f);
    g_paused = true;
    endSession(sessionWhy);
}

inline void harvestGroups(bool fresh = true) {
    if (!grouptrace::g_on || g_groupsPath.empty()) return;
    const grouptrace::Roll r = grouptrace::g_lastRoll;
    if (r.rows <= 0) return;
    const std::vector<AnchorRow>& v = anchors::g_dead;
    long long partT = -1;
    float xKept = 0.f, xNow = 0.f;
    if (r.depth <= g_groupsDepth) {
        if (!g_cfg.groupsRetime || !fresh || g_groupsX.empty()) return;
        const size_t n = std::min({v.size(), g_groupsX.size(), (size_t)std::max(0LL, r.depth)});
        for (size_t t = 0; t < n && partT < 0; ++t)
            if (v[t].valid && !std::isnan(g_groupsX[t])
                && std::fabs(v[t].x - g_groupsX[t]) > kRetimeTolX) {
                partT = (long long)t;
                xKept = g_groupsX[t];
                xNow = v[t].x;
            }
        if (partT < 0) return;
    }
    if (!copyRecording(std::string(DATA_DIR) + "/grouptrace_last.txt", g_groupsPath)) return;
    g_groupsDepth = r.depth;
    g_groupsSerial = g_attemptSerial;   // grouptrace_last is the attempt that just ended
    if (g_cfg.groupsRetime) {
        g_groupsX.assign(v.size(), std::numeric_limits<float>::quiet_NaN());
        if (fresh)
            for (size_t t = 0; t < v.size(); ++t)
                if (v[t].valid) g_groupsX[t] = v[t].x;
    }
    char b[256];
    if (partT >= 0)
        snprintf(b, sizeof(b), "dpsolve:   moving geometry: kept this run's record (%lld rows, "
                 "depth t=%lld) over a deeper one on another clock - they part at t=%lld "
                 "(x %.1f there, %.1f here)", r.rows, r.depth, partT, xKept, xNow);
    else
        snprintf(b, sizeof(b), "dpsolve:   moving geometry: kept this run's record "
                 "(%lld rows, depth t=%lld)", r.rows, r.depth);
    writeResult(b);
}

inline void checkPhantom(long long dt, double deathX, bool wedged = false,
                         bool force = false) {
    if ((!g_lastTailSolved && !wedged) || g_phantomLifted) return;
    const long long site = (long long)std::floor(deathX / 8.0);
    const int n = (g_phantomHits[site] += (force ? kPhantomAfter : 1));
    if (n < kPhantomAfter) return;
    if ((int)g_phantomBands.size() >= kPhantomMaxBands) return;
    const std::vector<AnchorRow>* savedSrc = anchors::g_src;
    anchors::ladderOn(false);
    const AnchorRow* r = anchors::row(dt);   // AnchorRow is p1's, the accessor is anchors'
    anchors::g_src = savedSrc;
    if (!r) return;
    // Counting starts again toward the NEXT box at this site (see the dedupe below).
    g_phantomHits[site] = 0;
    char band[96];
    const long long scaleKey = site * 16 + r->mode;
    const int scale = g_phantomScale[scaleKey];
    const double pdx = kPhantomDx * (double)(1 << scale);
    const double pdy = kPhantomDy * (double)(1 << scale);
    snprintf(band, sizeof(band), "%.1f,%.1f,%d,%.1f,%.1f",
             (double)r->x - pdx, (double)r->x + pdx,
             -2 - r->mode,
             (double)r->y - pdy, (double)r->y + pdy);
    for (const std::string& prev : g_phantomBands)
        if (prev == band) {
            if (g_phantomScale[scaleKey] < kPhantomScaleMax)
                ++g_phantomScale[scaleKey];
            return;
        }
    g_phantomBands.emplace_back(band);
    itermap::addVeto(g_iter, (float)((double)r->x - pdx), (float)((double)r->x + pdx));
    char b[224];
    snprintf(b, sizeof(b), "dpsolve:   [veto] a SOLVED tail died in the game %d times at t=%lld "
             "(x=%.0f) - dropping the box: --deadband %s [%d]",
             n, dt, (double)r->x, band, (int)g_phantomBands.size());
    writeResult(b);
    g_triedMissedPortal = false;
}

constexpr double kVoidLow = 200.0;
constexpr double kVoidTop = 300.0;
constexpr long long kVoidTicks = 1200;
// ...sorted by x; g_voidLow and g_voidLowRead are declared with g_phantomBands
inline bool voidLowIn(double x0, double x1) {
    if (!g_voidLowRead) {
        g_voidLowRead = true;
        g_voidLow.clear();
        std::istringstream f(g_csv);
        std::string line;
        int cType = -1, cCx = -1, cCy = -1, cW = -1, cH = -1;
        if (std::getline(f, line)) {
            std::stringstream hs(line);
            std::string col;
            for (int i = 0; std::getline(hs, col, ','); ++i) {
                if (col == "type") cType = i;
                else if (col == "cx") cCx = i;
                else if (col == "cy") cCy = i;
                else if (col == "w") cW = i;
                else if (col == "h") cH = i;
            }
        }
        if (cType < 0 || cCx < 0 || cCy < 0 || cW < 0 || cH < 0) return true;   // unreadable: never fire
        while (std::getline(f, line)) {
            std::vector<std::string> v;
            std::stringstream ls(line);
            std::string cell;
            while (std::getline(ls, cell, ',')) v.push_back(cell);
            const int need = std::max({cType, cCx, cCy, cW, cH});
            if ((int)v.size() <= need || v[cType] == "7" || v[cType] == "20") continue;
            const double cx = std::atof(v[cCx].c_str()), cy = std::atof(v[cCy].c_str());
            const double w = std::atof(v[cW].c_str()), h = std::atof(v[cH].c_str());
            if (cy - h / 2 < kVoidTop) g_voidLow.emplace_back((float)(cx - w / 2), (float)(cx + w / 2));
        }
        std::sort(g_voidLow.begin(), g_voidLow.end());
    }
    for (const auto& s : g_voidLow) {
        if (s.first > x1) break;
        if (s.second >= x0) return true;
    }
    return false;
}
inline void checkVoidRun(long long dt, double deathX) {
    if (!g_cfg.voidBand) return;
    if (solver::g_killUid < 0 || std::llabs(solver::g_killTick - dt) > 1
        || std::fabs((double)solver::g_killX - deathX) > 100.0 || solver::g_killY >= kVoidTop)
        return;
    const std::vector<AnchorRow>* savedSrc = anchors::g_src;
    anchors::ladderOn(false);
    long long t = dt;
    double x0 = deathX;
    if (!anchors::row(t)) --t;   // the death tick itself may have no row
    for (; t >= 1; --t) {
        const AnchorRow* r = anchors::row(t);
        if (!r || !r->valid || !(r->y > 0.f && r->y < kVoidLow)) break;
        x0 = r->x;
    }
    anchors::g_src = savedSrc;
    const long long t0 = t + 1, n = dt - t;
    if (n < kVoidTicks || voidLowIn(x0, deathX - 40.0)) return;
    char band[96];
    snprintf(band, sizeof(band), "%.1f,%.1f,-1,-1000.0,%.1f", x0, deathX + 1.0, kVoidTop);
    for (const std::string& prev : g_phantomBands)
        if (prev == band) return;
    g_phantomBands.emplace_back(band);
    char b[256];
    snprintf(b, sizeof(b), "dpsolve:   [voidband] the death at t=%lld (x=%.0f) ends %lld ticks over empty "
             "floor from t=%lld (x=%.0f), killer uid %d - closing it: --deadband %s",
             dt, deathX, n, t0, x0, solver::g_killUid, band);
    writeResult(b);
    g_voidDt = dt;
    g_voidT0 = t0;
    g_voidX0 = (float)x0;
    if (g_bestDeath >= dt - 1 && t0 < g_bestDeath) voidDeeper("was");
}
// cfg voidband: the depth to beat goes back to where the closed run began.
inline void voidDeeper(const char* how) {
    char b[200];
    snprintf(b, sizeof(b), "dpsolve:   [voidband] the deepest plan %s that run - the depth to beat goes "
             "%lld -> %lld", how, g_bestDeath, g_voidT0);
    writeResult(b);
    g_bestDeath = g_voidT0;
    g_bestDeathX = g_voidX0;
}

inline bool liftPhantomVetoes(const char* where) {
    if (g_phantomBands.empty() || g_phantomLifted) return false;
    char b[192];
    snprintf(b, sizeof(b), "dpsolve:   [veto] out of anchors at %s - giving all %d boxes back for "
             "one last attempt (no more vetoes after this)", where, (int)g_phantomBands.size());
    writeResult(b);
    g_phantomBands.clear();
    g_phantomLifted = true;
    return true;
}

inline void ckTick(long long t) {
    if (!g_ckFlying) return;
    if (dpbridge::checkCall() != g_ckCall) {
        // Cannot happen while dp waits for its judgements; said out loud if it ever does.
        g_ckFlying = false;
        g_paused = true;
        writeResult("dpsolve:   [check] the call that published this checkpoint has returned - "
                    "dropping the flight");
        return;
    }
    if (t < g_ckEnd) return;
    g_ckFlying = false;
    g_paused = true;
    ++g_ckPassed;
    if (!dpbridge::passCheckpoint(g_ckCall, g_ckIndex))
        writeResult("dpsolve:   [check] a passed checkpoint was refused by the search");
}

inline void ckOnDeath(long long dt, float deathX) {
    if (!g_cfg.dpCheck || !g_ckFlying) return;
    g_ckFlying = false;
    g_paused = true;
    char b[256];
    if (dpbridge::checkCall() != g_ckCall) {
        writeResult("dpsolve:   [check] died on a checkpoint whose call has returned - ignored");
        return;
    }
    bool deaf = false;
    {
        std::lock_guard<std::mutex> g(g_ckMx);
        deaf = g_ckDeaf.count(dt) != 0;
    }
    if (dt >= g_ckEnd || deaf) {
        snprintf(b, sizeof(b), "dpsolve:   [check] checkpoint #%zu (t0=%lld..%lld) died at t=%lld "
                 "- passed (%s)", g_ckIndex, g_ckT0, g_ckEnd, dt,
                 deaf ? "this job already learnt from this death" : "its inputs had run out");
        writeResult(b);
        ++g_ckPassed;
        if (!dpbridge::passCheckpoint(g_ckCall, g_ckIndex))
            writeResult("dpsolve:   [check] a passed checkpoint was refused by the search");
        return;
    }
    ++g_ckDeaths;
    if (g_cfg.dpCheckObs) {
        if (!g_ckObs.have) {
            g_ckObs.have = true;
            g_ckObs.index = g_ckIndex;
            g_ckObs.t0 = g_ckT0;
            g_ckObs.end = g_ckEnd;
            g_ckObs.deathT = dt;
            g_ckObs.plan = g_ckPlan;
            g_ckObs.atSec = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - g_t0).count();
            snprintf(b, sizeof(b), "dpsolve:   [checkobs] checkpoint #%zu (t0=%lld..%lld) died at "
                     "t=%lld x=%.1f, %.1f s into the job - kept; the search goes on", g_ckIndex,
                     g_ckT0, g_ckEnd, dt, (double)deathX, g_ckObs.atSec);
            writeResult(b);
        }
        if (!dpbridge::passCheckpoint(g_ckCall, g_ckIndex))
            writeResult("dpsolve:   [check] a passed checkpoint was refused by the search");
        return;
    }
    {
        std::lock_guard<std::mutex> g(g_ckMx);
        g_ckDeath = CkDeath{};
        g_ckDeath.ready = true;
        g_ckDeath.call = g_ckCall;
        g_ckDeath.index = g_ckIndex;
        g_ckDeath.t0 = g_ckT0;
        g_ckDeath.end = g_ckEnd;
        g_ckDeath.deathT = dt;
        g_ckDeath.deathX = deathX;
        g_ckDeath.plan = g_ckPlan;
        g_ckDeath.rows.swap(anchors::g_live);
    }
    dpbridge::cancelSearch(true);
}

inline bool g_ckAbandonJob = false;
inline bool ckClearedDuringJob() {
    if (!g_cfg.dpCheck || !g_ckFlying) return false;
    g_ckFlying = false;
    g_ckAbandonJob = true;
    char b[192];
    snprintf(b, sizeof(b), "dpsolve:   [check] checkpoint #%zu (t0=%lld..%lld) cleared the level - "
             "abandoning the search in flight", g_ckIndex, g_ckT0, g_ckEnd);
    writeResult(b);
    dpbridge::cancelSearch(true);
    return true;
}

inline void ckConsider() {
    if (!g_cfg.dpCheck || g_ckFlying) return;
    if (!g_running.load() || g_finished.load()) return;
    if (g_stop || g_sessionOver || !g_started || g_deepActive || g_dpShowSolution) return;
    auto* pl = PlayLayer::get();
    if (!pl) return;
    dpbridge::SolveCheckpoint cp;
    if (g_cfg.dpCheckObs && g_ckObs.have) {
        while (dpbridge::nextCheckpoint(cp)) {
            ++g_ckObsSkipped;
            if (!dpbridge::passCheckpoint(cp.call, cp.index))
                writeResult("dpsolve:   [check] a passed checkpoint was refused by the search");
        }
        return;
    }
    if (!dpbridge::nextCheckpoint(cp)) return;
    std::vector<InputCmd> plan;
    for (const InputCmd& c : g_ckBase) {
        if (c.step >= cp.t0) break;
        plan.push_back(c);
    }
    for (const std::pair<long long, int>& e : cp.edges)
        plan.push_back(InputCmd{(int)e.first, e.second != 0});
    std::sort(plan.begin(), plan.end(),
              [](const InputCmd& a, const InputCmd& c) { return a.step < c.step; });
    g_ckCall = cp.call;
    g_ckIndex = cp.index;
    g_ckT0 = cp.t0;
    g_ckEnd = cp.tick;
    g_ckPlan = plan;
    g_ckFlying = true;
    g_ckInstalled = true;
    ++g_ckFlights;
    g_cfg.inputs = plan;
    g_paused = false;
    pl->resetLevel();
}

inline void ckObsCompare(double jobSec) {
    char b[360];
    if (!g_ckObs.have) {
        snprintf(b, sizeof(b), "dpsolve:   [checkobs] job %.1f s: no flight died (%zu flown)",
                 jobSec, g_ckFlights);
        writeResult(b);
        return;
    }
    const long long T = g_ckObs.deathT;
    auto upTo = [](const std::vector<InputCmd>& p, long long last) {
        std::vector<std::pair<int, int>> v;
        for (const InputCmd& c : p)
            if (c.step <= last) v.push_back({c.step, c.down ? 1 : 0});
        std::sort(v.begin(), v.end());
        return v;
    };
    const auto fl = upTo(g_ckObs.plan, T), pl = upTo(g_plan, T);
    const bool sameLe = fl == pl;
    const bool sameLt = upTo(g_ckObs.plan, T - 1) == upTo(g_plan, T - 1);
    long long firstDiff = -1;
    if (!sameLe) {
        size_t i = 0;
        while (i < fl.size() && i < pl.size() && fl[i] == pl[i]) ++i;
        const long long a = i < fl.size() ? fl[i].first : LLONG_MAX;
        const long long c = i < pl.size() ? pl[i].first : LLONG_MAX;
        firstDiff = std::min(a, c);
    }
    g_ckObs.compared = true;
    g_ckObs.same = sameLt;
    snprintf(b, sizeof(b), "dpsolve:   [checkobs] job %.1f s; flight #%zu (t0=%lld) died %.1f s in at "
             "t=%lld; the job's plan %s the flight's inputs up to it (through t-1: %s, first "
             "difference t=%lld; %zu flown, %zu passed unflown)", jobSec, g_ckObs.index,
             g_ckObs.t0, g_ckObs.atSec, T, sameLe ? "shares" : "does not share",
             sameLt ? "same" : "different", firstDiff, g_ckFlights, g_ckObsSkipped);
    writeResult(b);
}

inline void mapTail(const std::vector<AnchorRow>& rows, int iter, int kind, long long p0,
                    long long p1) {
    if (p0 < 0) p0 = 0;
    if (p1 > 400000) p1 = 400000;
    if (p1 >= (long long)rows.size()) p1 = (long long)rows.size() - 1;
    if (p1 <= p0) return;
    const long long room = (long long)itermap::kPathMaxPts - 2;
    const long long step = std::max<long long>(itermap::kPathStep, (p1 - p0 + room - 1) / room);
    itermap::beginPath(iter, kind);
    long long last = -1;
    for (long long k = p0; k <= p1; k += step)
        if (rows[(size_t)k].valid) {
            itermap::addPathPoint(rows[(size_t)k].x, rows[(size_t)k].y);
            last = k;
        }
    for (long long k = p1; k > last; --k)
        if (rows[(size_t)k].valid) {
            itermap::addPathPoint(rows[(size_t)k].x, rows[(size_t)k].y);
            break;
        }
    itermap::endPath();
}

inline void mapClear(long long t, bool byFlight) {
    mapTail(anchors::g_live, g_iter + 1, itermap::KindDeeper, byFlight ? g_ckT0 : g_anchorT, t);
}

inline bool coinApproach(const std::vector<AnchorRow>& rows,
                         const std::vector<std::vector<solver::CoinPos>>& track, size_t ci,
                         long long upto, long long& t, float& x, double& d) {
    const auto& c = solver::g_coins[ci];
    const std::vector<solver::CoinPos>* lg =
        (ci < track.size() && !track[ci].empty()) ? &track[ci] : nullptr;
    t = -1;
    d = 1e18;
    long long tOff = -1;
    float xOff = 0.f;
    double dOff = 1e18;
    const double reach = (c.hw > 0.f ? (double)c.hw : 20.0) + 15.0;
    long long tReach = -1, lastIn = -2;
    float xReach = 0.f;
    double dReach = 1e18;
    size_t k = 0;   // the track row in force at tick i
    for (size_t i = 0; i < rows.size() && (long long)i < upto; ++i) {
        double cx = c.x, cy = c.y;
        bool off = false;
        if (lg) {
            while (k + 1 < lg->size() && (*lg)[k + 1].t <= (long long)i) ++k;
            if ((*lg)[k].t > (long long)i) continue;
            off = !(*lg)[k].on;
            if (off && !g_cfg.coinApproachOff) continue;
            cx = (*lg)[k].x;
            cy = (*lg)[k].y;
        }
        const AnchorRow& r = rows[i];
        if (!r.valid) continue;
        const double e = std::max(std::fabs((double)r.x - cx), std::fabs((double)r.y - cy));
        if (off) {
            if (e <= dOff) { dOff = e; tOff = (long long)i; xOff = r.x; }
            if (g_cfg.coinApproachReach && e <= reach) {
                if ((long long)i > lastIn + 1 || e <= dReach) {   // a new pass, or closer in this one
                    dReach = e; tReach = (long long)i; xReach = r.x;
                }
                lastIn = (long long)i;
            }
        } else if (e <= d) {
            d = e; t = (long long)i; x = r.x;
        }
    }
    if (g_cfg.coinApproachOff && tOff > t) { t = tOff; x = xOff; d = dOff; }
    if (g_cfg.coinApproachReach && tReach > t) { t = tReach; x = xReach; d = dReach; }
    return t > 0;
}

inline bool coinApproachRoute(const std::vector<AnchorRow>& rows,
                              const std::vector<std::vector<solver::CoinPos>>& track,
                              const std::vector<long long>& onTick, size_t ci, long long upto,
                              long long& t, float& x, double& d, int& need) {
    need = -1;
    const bool ok = coinApproach(rows, track, ci, upto, t, x, d);
    if (!g_cfg.routePrereq || !route::g_built || !routeEngaged(ci)) return ok;
    const std::vector<size_t> miss = route::unmet((int)ci, onTick, ok ? t : -1);
    if (miss.empty()) return ok;
    long long bt = -1;
    float bx = 0.f;
    double bd = 1e18;
    int bu = -1;
    for (const size_t k : miss) {
        const route::Prereq& p = route::g_prereq[k];
        long long kt = -1;
        float kx = 0.f;
        double kd = 1e18;
        for (size_t i = 0; i < rows.size() && (long long)i < upto; ++i) {
            const AnchorRow& r = rows[i];
            if (!r.valid) continue;
            const double ex = std::max(std::fabs((double)r.x - p.bx) - p.bhw, 0.0);
            const double ey = std::max(std::fabs((double)r.y - p.by) - p.bhh, 0.0);
            const double e = std::max(ex, ey);
            if (e < kd) { kd = e; kt = (long long)i; kx = r.x; }
        }
        // The earliest box the route has not opened is the first thing to change.
        if (kt > 0 && (bt < 0 || kt < bt)) { bt = kt; bx = kx; bd = kd; bu = p.boxUid; }
    }
    if (bt <= 0) return ok;
    t = bt;
    x = bx;
    d = bd;
    need = bu;
    return true;
}

inline bool routeEngage(size_t ci, const char* why) {
    if (ci >= 32 || routeEngaged(ci)) return false;
    g_routeEngaged |= 1u << ci;
    writeResult("dpsolve:   [route] coin " + std::to_string(ci) + ": " + why
                + " - its prerequisites rank from here (cfg routeprereqafter)");
    if (g_bestDeath < 0) return false;
    int had = 0;
    for (auto it = anchors::g_deepest.rbegin(); it != anchors::g_deepest.rend(); ++it)
        if (it->valid) { had = it->coins; break; }
    if (ci < 8 && (had & (1 << ci))) return false;
    long long t0;
    float x0 = 0.f;
    double d0;
    int need = -1;
    if (!coinApproachRoute(anchors::g_deepest, anchors::g_deepestCoinPos, route::g_onTickDeepest, ci,
                           g_bestDeath + 1, t0, x0, d0, need)
        || need < 0 || t0 >= g_bestDeath)
        return false;
    char b[224];
    snprintf(b, sizeof(b), "dpsolve:   [route] the deepest plan never opened box uid %d - its wall "
             "moves to the box: best %lld -> %lld", need, g_bestDeath, t0);
    writeResult(b);
    g_bestDeath = t0;
    g_bestDeathX = x0;
    g_routeNeedUid = need;
    return true;
}

inline bool fileCoinMissPost(long long& dt, float& deathX) {
    if (!g_cfg.coinMissPost) return false;
    long long bestT = -1;
    float bestX = 0.f;
    double bestD = 0.0;
    size_t bestC = 0;
    int bestNeed = -1;
    for (size_t i = 0; i < solver::g_coins.size() && i < solver::g_coinGdTick.size(); ++i) {
        if (solver::g_coinGdTick[i] >= 0) continue;
        long long ct;
        float cx = 0.f;
        double cd;
        int need = -1;
        if (coinApproachRoute(anchors::g_live, solver::g_coinPosLog, route::g_onTick, i, dt, ct, cx,
                              cd, need)
            && (bestT < 0 || ct < bestT)) {
            bestT = ct;
            bestX = cx;
            bestD = cd;
            bestC = i;
            bestNeed = need;
        }
    }
    if (bestT <= 0) return false;
    g_routeNeedDeath = bestNeed;
    g_postCoins |= 1 << bestC;
    if (bestC < 8 && (g_postCoinT[bestC] < 0 || bestT < g_postCoinT[bestC]))
        g_postCoinT[bestC] = bestT;
    const long long wasBest = g_bestDeath;
    if (g_bestDeath >= 0) {
        int had = 0;
        for (auto it = anchors::g_deepest.rbegin(); it != anchors::g_deepest.rend(); ++it)
            if (it->valid) { had = it->coins; break; }
        long long it0;
        float ix = 0.f;
        double id;
        int ineed = -1;
        if (!(had & (1 << bestC))
            && coinApproachRoute(anchors::g_deepest, anchors::g_deepestCoinPos, route::g_onTickDeepest,
                                 bestC, g_bestDeath + 1, it0, ix, id, ineed)
            && it0 < g_bestDeath) {
            g_bestDeath = it0;
            // ...and when that rank is a box, the wall the ladder goes back to is the box.
            if (ineed >= 0) g_routeNeedUid = ineed;
        }
    }
    char b[288];
    if (bestNeed >= 0)
        snprintf(b, sizeof(b), "dpsolve:   [route] coinmisspost: coin %zu (uid %d) missed, and the box "
                 "that has to open it first (uid %d) was not - filed at the closest approach to the "
                 "box t=%lld x=%.0f, %.1f px away, not at t=%lld (cfg routeprereq)", bestC,
                 solver::g_coins[bestC].uid, bestNeed, bestT, (double)bestX, bestD, dt);
    else
        snprintf(b, sizeof(b), "dpsolve:   [coin] coinmisspost: coin %zu (uid %d) missed - closest "
                 "approach t=%lld x=%.0f, %.1f px away; filed there, not at t=%lld", bestC,
                 solver::g_coins[bestC].uid, bestT, (double)bestX, bestD, dt);
    writeResult(b);
    if (g_bestDeath != wasBest) {
        snprintf(b, sizeof(b), "dpsolve:   [coin] the deepest plan never had it either - ranked at "
                 "its own closest approach: best %lld -> %lld", wasBest, g_bestDeath);
        writeResult(b);
    }
    dt = bestT;
    deathX = bestX;
    g_coinMissPostPending = true;
    g_postMissCoin = (int)bestC;
    g_postMissCrossT = bestC < solver::g_coinPosLog.size()
                           ? firstFinalCrossing(anchors::g_live, solver::g_coinPosLog[bestC], dt) : -1;
    return true;
}

inline bool scoreByPostCoins(long long& dt, float& deathX) {
    if (!g_cfg.coinMissPost || !g_postCoins) return false;
    long long bestT = -1;
    float bestX = 0.f;
    size_t bestC = 0;
    int bestNeed = -1;
    for (size_t i = 0; i < solver::g_coins.size() && i < solver::g_coinGdTick.size(); ++i) {
        if (!(g_postCoins & (1 << i)) || solver::g_coinGdTick[i] >= 0) continue;
        if (i < 8 && dt <= g_postCoinT[i]) continue;   // has not reached the coin yet
        long long ct;
        float cx = 0.f;
        double cd;
        int need = -1;
        if (coinApproachRoute(anchors::g_live, solver::g_coinPosLog, route::g_onTick, i, dt, ct, cx,
                              cd, need)
            && (bestT < 0 || ct < bestT)) {
            bestT = ct;
            bestX = cx;
            bestC = i;
            bestNeed = need;
        }
    }
    if (bestT <= 0 || bestT >= dt) return false;
    g_routeNeedDeath = bestNeed;
    char b[256];
    if (bestNeed >= 0)
        snprintf(b, sizeof(b), "dpsolve:   [route] died at t=%lld without coin %zu, and without the "
                 "box that opens it (uid %d) - ranked at its closest approach to the box t=%lld "
                 "x=%.0f (cfg routeprereq)", dt, bestC, bestNeed, bestT, (double)bestX);
    else
        snprintf(b, sizeof(b), "dpsolve:   [coin] died at t=%lld without coin %zu (a clear has missed "
                 "it) - ranked at its closest approach t=%lld x=%.0f", dt, bestC, bestT,
                 (double)bestX);
    writeResult(b);
    dt = bestT;
    deathX = bestX;
    return true;
}

inline void fileCoinMissEarly(long long dt) {
    if (!g_cfg.coinMissEarly || !g_cfg.coinMissPost || !g_cfg.routePrereq || !route::g_built
        || solver::g_hasRotGameplay)
        return;
    for (size_t i = 0; i < solver::g_coins.size() && i < solver::g_coinGdTick.size() && i < 8; ++i) {
        if (solver::g_coinGdTick[i] >= 0 || (g_postCoins & (1 << i))) continue;
        long long passT = -1;
        int boxUid = -1;
        for (const size_t k : route::unmet((int)i, route::g_onTick, -1)) {
            const route::Prereq& p = route::g_prereq[k];
            const float edge = p.bx + p.bhw + 15.f;   // the player's largest half, as the cut uses
            for (size_t r = 0; r < anchors::g_live.size() && (long long)r < dt; ++r) {
                const AnchorRow& row = anchors::g_live[r];
                if (row.valid && row.x > edge) {
                    if (passT < 0 || (long long)r < passT) { passT = (long long)r; boxUid = p.boxUid; }
                    break;
                }
            }
        }
        if (passT < 0) continue;
        g_postCoins |= 1 << i;
        if (g_postCoinT[i] < 0 || passT < g_postCoinT[i]) g_postCoinT[i] = passT;
        char b[224];
        snprintf(b, sizeof(b), "dpsolve:   [route] coinmissearly: coin %zu (uid %d) cannot come on any "
                 "more - its box (uid %d) was passed unopened at t=%lld; ranked as a missed coin from "
                 "here (cfg coinmissearly)", i, solver::g_coins[i].uid, boxUid, passT);
        writeResult(b);
        routeEngage(i, "its box was passed unopened");
        if (g_bestDeath < 0) continue;
        int had = 0;
        for (auto it = anchors::g_deepest.rbegin(); it != anchors::g_deepest.rend(); ++it)
            if (it->valid) { had = it->coins; break; }
        if (had & (1 << i)) continue;
        long long it0;
        float ix = 0.f;
        double id;
        int ineed = -1;
        if (coinApproachRoute(anchors::g_deepest, anchors::g_deepestCoinPos, route::g_onTickDeepest, i,
                              g_bestDeath + 1, it0, ix, id, ineed)
            && it0 < g_bestDeath) {
            snprintf(b, sizeof(b), "dpsolve:   [coin] the deepest plan never had coin %zu either - "
                     "ranked at its own closest approach: best %lld -> %lld", i, g_bestDeath, it0);
            writeResult(b);
            g_bestDeath = it0;
            if (ineed >= 0) g_routeNeedUid = ineed;
        }
    }
}

inline bool kLimitsOn(int& pulse, int& every) {
    if (Mod::get()->getSettingValue<bool>("go-crazy")) return false;
    pulse = (int)Mod::get()->getSettingValue<int64_t>("human-min-pulse");
    const int64_t cps = Mod::get()->getSettingValue<int64_t>("max-cps");
    every = cps > 0 ? (int)((240 + cps - 1) / cps) : 0;
    return pulse > 1 || every > 1;
}

inline std::string kLimitBreach(long long from, long long to, int pulse, int every) {
    int press = std::numeric_limits<int>::max(), hole = press, apart = press;
    long long lastDown = -1, lastUp = -1;
    for (const auto& c : g_plan) {
        if (c.step > to) break;
        if (c.down) {
            if (c.step >= from && lastUp >= 0) hole = std::min(hole, (int)(c.step - lastUp));
            if (c.step >= from && lastDown >= 0) apart = std::min(apart, (int)(c.step - lastDown));
            lastDown = c.step;
        } else {
            if (c.step >= from && lastDown >= 0) press = std::min(press, (int)(c.step - lastDown));
            lastUp = c.step;
        }
    }
    std::vector<std::string> need;
    char b[96];
    if (press < pulse) need.push_back(std::to_string(press) + "-tick presses");
    if (hole < pulse) need.push_back(std::to_string(hole) + "-tick gaps");
    if (every > 1 && apart < every) {
        snprintf(b, sizeof(b), "presses %.2f s apart (%.0f CPS)", apart / 240.0, 240.0 / apart);
        need.push_back(b);
    }
    if (need.empty()) return {};
    std::string s = "it needed ";
    for (size_t i = 0; i < need.size(); ++i) s += (i ? (i + 1 == need.size() ? " and " : ", ") : "") + need[i];
    s += ". Your limits: " + std::to_string(pulse) + "-tick presses and gaps";
    if (every > 1) s += ", " + std::to_string(Mod::get()->getSettingValue<int64_t>("max-cps")) + " CPS";
    return s + ".";
}

inline void onDeath(long long dt, float deathX) {
    // Consumed first, whichever way this returns, so a coin miss cannot leak into a later death.
    const bool coinMissPost = g_coinMissPostPending;
    g_coinMissPostPending = false;
    const bool coinMiss = g_coinMissPending || coinMissPost;
    g_coinMissPending = false;
    const bool flownClaimedGoal = g_planClaimsGoal;
    if (!g_cfg.dpSolve || g_dpShowSolution || g_stop || g_sessionOver) return;
    // A session whose start is still queued (see start()) has no rounds yet.
    if (g_pendingLayer) return;
    if (cpflight::g_probeFlying) {
        writeResult(cpflight::probeCapture(dt, deathX, "died"));
        g_paused = true;
        return;
    }
    if (cpflight::g_controlFlying) {
        writeResult(cpflight::controlEnd(dt, deathX, "died"));
        g_paused = true;
        return;
    }
    if (g_running.load()) {
        ckOnDeath(dt, deathX);
        return;
    }
    if (levelslice::onVerifyDeath(dt, deathX)) return;
    // ...and a wall the copy has held for a while is checked on the level itself, once.
    if (levelslice::maybeCheckWall()) return;
    // cfg dpcheckobs: where the job's plan died, next to where its search's flight had.
    if (g_cfg.dpCheckObs && g_ckObs.compared) {
        g_ckObs.compared = false;
        char ob[200];
        snprintf(ob, sizeof(ob), "dpsolve:   [checkobs] the job's plan died at t=%lld; its flight "
                 "at t=%lld - %s (inputs through t-1 %s)", dt, g_ckObs.deathT,
                 dt == g_ckObs.deathT ? "the same tick" : "a different tick",
                 g_ckObs.same ? "same" : "different");
        writeResult(ob);
    }
    const long long physDt = dt;
    const float physX = deathX;
    const int filedNeed = g_routeNeedDeath;
    g_routeNeedDeath = -1;
    if (g_cfg.routePrereq && g_cfg.routePrereqAfter > 0 && route::g_built)
        for (size_t i = 0; i < solver::g_coins.size() && i < 8; ++i) {
            if (!(g_postCoins & (1 << i)) || routeEngaged(i)) continue;
            if (i < solver::g_coinGdTick.size() && solver::g_coinGdTick[i] >= 0) continue;
            if (++g_routeRounds[i] < g_cfg.routePrereqAfter) continue;
            bool has = false;
            for (const route::Prereq& p : route::g_prereq) has = has || p.coin == (int)i;
            if (!has) continue;
            routeEngage(i, ("still missing " + std::to_string(g_routeRounds[i])
                            + " rounds after its filing").c_str());
        }
    if (!coinMiss) fileCoinMissEarly(dt);
    const bool postScored = !coinMiss && scoreByPostCoins(dt, deathX);
    const int deathNeed = coinMissPost ? filedNeed : (postScored ? g_routeNeedDeath : -1);
    g_routeNeedDeath = -1;
    const bool voidAttempt =
        anchors::g_live.size() < 50 && anchors::g_dead.size() >= 500
        && dt < 600 && g_lastDeath > 3000;
    if (voidAttempt) {
        char vb[192];
        snprintf(vb, sizeof(vb), "dpsolve:   void attempt (t=%lld, %zu rows recorded) - "
                 "scored as a repeat of the last real death (t=%lld)",
                 dt, (size_t)anchors::g_live.size(), (long long)g_lastDeath);
        writeResult(vb);
        anchors::g_live.clear();
        cpflight::g_live.reset();
        dt = g_lastDeath;
        deathX = g_lastDeathX;
    } else {
        anchors::bank();
        // cfg cpflight: this attempt's checkpoints and books go with its rows (before any fold).
        if (cpflight::on()) {
            cpflight::bank(physDt, physX,
                           grouptrace::g_lastRoll.rows > 0 && grouptrace::g_lastRoll.depth >= 0);
            cpflight::queueProbe();   // cfg cpflightprobe: flown while the solve this death starts runs
        }
        if (g_cfg.dpRotSeed == 3) rotseed::fold(anchors::g_dead);
    }
    harvestGroups(!voidAttempt);   // rollGroupTrace has already committed this run's recording
    ++g_iter;
    {
        const std::vector<AnchorRow>* savedSrcOb = anchors::g_src;
        anchors::ladderOn(false);
        float offX = 0.f;
        const long long offT = offBoardTick(dt, offX);
        anchors::g_src = savedSrcOb;
        if (offT > 0 && offT < dt) {
            char ob[224];
            snprintf(ob, sizeof(ob), "dpsolve:   left the playfield at t=%lld x=%.0f - crediting "
                     "the death there, not at t=%lld x=%.0f", offT, (double)offX, dt,
                     (double)deathX);
            writeResult(ob);
            dt = offT;
            deathX = offX;
        }
    }
    bool wasWedged = false;
    {
        const std::vector<AnchorRow>* savedSrc = anchors::g_src;
        anchors::ladderOn(false);
        long long d0 = std::min(dt, 400000LL);
        while (d0 > 1 && !anchors::row(d0)) --d0;
        const AnchorRow* rd = anchors::row(d0);
        if (rd) {
            long long k = d0;
            while (k > 1) {
                const AnchorRow* rk = anchors::row(k - 1);
                if (!rk || std::fabs(rk->x - rd->x) > 0.5f
                    || std::fabs(rk->y - rd->y) > 0.5f)
                    break;
                --k;
            }
            if (dt - k >= 900) {
                char wb[224];
                snprintf(wb, sizeof(wb), "dpsolve:   wedged since t=%lld (x=%.0f, %lld still "
                         "ticks) - crediting the death there, not at t=%lld", k,
                         (double)rd->x, dt - k, dt);
                writeResult(wb);
                dt = k;
                deathX = rd->x;
                wasWedged = true;
            }
        }
        anchors::g_src = savedSrc;
    }
    if (dt > 30000) wasWedged = true;
    g_hudIter = g_iter;
    if (deathX > g_hudVerifiedX) g_hudVerifiedX = deathX;
    char b[256];
    snprintf(b, sizeof(b), "dpsolve: iter %d: death t=%lld x=%.1f (best t=%lld)",
             g_iter, dt, (double)deathX, g_bestDeath);
    writeResult(b);
    g_flownPlan = g_plan;
    logFingerprint(dt, deathX);
    std::error_code ec;
    const std::string pt = g_planPath + ".trace.csv", tt = g_tailPath + ".trace.csv";
    const bool hp = std::filesystem::exists(pt, ec), ht = std::filesystem::exists(tt, ec);
    std::string src;
    if (hp && ht)
        src = std::filesystem::last_write_time(tt, ec) > std::filesystem::last_write_time(pt, ec)
              ? tt : pt;
    else if (hp) src = pt;
    else if (ht) src = tt;
    // cfg dpsecreuse: a splice has no walk of its own; the target it inherited stays.
    const bool keepTarget = src.empty() && g_rjKeepTarget && g_rjAfterTick >= 0;
    g_rjKeepTarget = false;
    if (keepTarget) {
        // the file and the tick it ends in are the ones the rung was fired with
    } else if (!src.empty()
        && std::filesystem::copy_file(src, rejoinTracePath(),
                                      std::filesystem::copy_options::overwrite_existing, ec))
        g_rjAfterTick = dt;
    else
        g_rjAfterTick = -1;
    if (coinMissPost || postScored) {
        g_rjAfterTick = -1;
        writeResult("dpsolve:   [rejoin] off for this death - it is ranked at a missed coin, and "
                    "the old path past it is the one that missed it");
    }
    writeResult("dpsolve:   [rejoin] trace <- "
                + std::string(keepTarget ? "kept (the splice's source plan)"
                              : src.empty() ? "none" : (src == tt ? "tail" : "plan"))
                + " " + (g_rjAfterTick >= 0 ? fileSig(rejoinTracePath()) : std::string("-")));
    if (!keepTarget)
        g_rjOldIsJoin = g_rjTailJoined;   // the plan that just died: was its tail a join?
    if (g_rjOldIsJoin)
        writeResult("dpsolve:   [rejoin] the plan that died was itself a join - the next "
                    "searches do not join it");
    const int sameDeaths = ++g_deathRuns[dt / 4];
    bool repeatSame = false;
    {
        const std::string fnv = planFnv(g_plan);
        repeatSame = fnv == g_prevFlownFnv && dt == g_prevFlownDeath;
        g_prevFlownFnv = fnv;
        g_prevFlownDeath = dt;
        if (g_cfg.dpFastVetoAll && !repeatSame && !g_flownDeaths.emplace(fnv, dt).second) {
            repeatSame = true;
            writeResult("dpsolve:   [veto] this plan already died on this tick in an earlier round - "
                        "a deterministic repeat, not new evidence");
        } else if (repeatSame) {
            writeResult("dpsolve:   [veto] the same plan died on the same tick as the round before - "
                        "a deterministic repeat, not new evidence");
        }
        g_roundRepeated = repeatSame;
    }
    if (coinMiss) {
        if (!coinMissPost && g_coinMarginNow < kCoinMarginMax) {
            g_coinMarginNow = std::min(kCoinMarginMax,
                                       g_coinMarginNow + kCoinMarginStep);
            char cb[176];
            snprintf(cb, sizeof(cb), "dpsolve:   [coin] the game refused a coin the plan claimed "
                     "- asking for %.0f px inside it from here on", g_coinMarginNow);
            writeResult(cb);
        }
        writeResult(coinMissPost
                        ? "dpsolve:   [coin] a clear short of a coin, filed at the closest approach"
                          " to it - a death for the ladder, not for the kill fixups or the veto"
                        : "dpsolve:   [coin] ended at a missed coin - a death for the ladder and the "
                          "fixups, but not for the phantom veto");
    }
    else {
        checkPhantom(postScored ? physDt : dt, postScored ? (double)physX : (double)deathX,
                     wasWedged || sameDeaths >= 8,
                     sameDeaths >= 8 || repeatSame);
        checkVoidRun(dt, (double)deathX);   // cfg voidband
    }
    if (wasWedged && !g_triedMissedPortal) {
        anchors::ladderOn(false);
        const long long mt = missedPortalTick();
        if (mt > 0) {
            g_triedMissedPortal = true;
            g_forceAnchorT = mt - kPortalSettle;
            char fb[96];
            snprintf(fb, sizeof(fb), "%.1f,%.1f,%d,-1e18,1e18",
                     g_missedPortalX + 45.0, g_missedPortalX + 75.0, g_missedPortalMode);
            g_forcePortalBand = fb;
            char mb[256];
            snprintf(mb, sizeof(mb), "dpsolve:   the wedge names it - the run crossed a mode "
                     "portal at t=%lld in the wrong mode; going back there and forcing the "
                     "crossing (--deadband %s)", mt, fb);
            writeResult(mb);
        }
    }
    if (g_iter > g_cfg.dpMaxIters) {
        writeResult("dpsolve: iteration budget exhausted (" + std::to_string(g_cfg.dpMaxIters)
                    + ") - stopping");
        g_stop = true;
        g_hudPhase = "gave up: out of iterations";
        giveUp("it ran out of repair rounds", "dpsolve_budget");
        return;
    }
    int coinOver = -1;
    if (g_cfg.coinOverDepth && g_cfg.coinMissPost && g_postCoins && g_bestDeath >= 0
        && !wasWedged && !voidAttempt && dt <= g_bestDeath) {
        int had = 0;
        for (auto it = anchors::g_deepest.rbegin(); it != anchors::g_deepest.rend(); ++it)
            if (it->valid) { had = it->coins; break; }
        int got = 0;
        for (size_t i = 0; i < solver::g_coins.size() && i < solver::g_coinGdTick.size() && i < 8; ++i)
            if (solver::g_coinGdTick[i] >= 0) got |= 1 << i;
        const int more = got & ~had & g_postCoins;
        if ((got & had) == had && more)
            for (int i = 0; i < 8; ++i)
                if ((more >> i) & 1) { coinOver = i; break; }
        if (coinOver >= 0) {
            char cb[224];
            snprintf(cb, sizeof(cb), "dpsolve:   [coin] took coin %d (GD t=%lld), which the deepest "
                     "plan (0x%x, ranked t=%lld) never had - this plan (0x%x, died t=%lld) replaces it "
                     "(cfg coinoverdepth)", coinOver, (long long)solver::g_coinGdTick[(size_t)coinOver],
                     had, g_bestDeath, got, dt);
            writeResult(cb);
        }
    }
    const bool deeper = (dt > g_bestDeath || coinOver >= 0) && !wasWedged;
    if (g_cfg.dpTopStop > 0 && g_capTier == (int)(sizeof(kCapTiers) / sizeof(kCapTiers[0]))
        && !deeper && g_iter - g_topTierIter >= g_cfg.dpTopStop) {
        writeResult("dpsolve: " + std::to_string(g_iter - g_topTierIter) + " rounds at "
                    + std::to_string(kCapTiers[g_capTier - 1]) + " states without getting deeper"
                    + " (best " + std::to_string(g_bestDeath) + ") - stopping (heuristic)");
        g_stop = true;
        g_hudPhase = "heuristic-stalled: no progress at the largest search";
        giveUp("heuristic-stalled: no deeper at the largest search (dptopstop)",
               "dpsolve_topstop");
        return;
    }
    long long useT = dt;
    int outcome = itermap::KindRewind;
    if (deeper) {
        if (g_kLimitWall >= 0) {
            const long long wall = g_kLimitWall;
            g_kLimitWall = -1;
            int pulse = 0, every = 0;
            const std::string need = kLimitsOn(pulse, every) ? kLimitBreach(wall - 480, dt, pulse, every)
                                                             : std::string();
            if (!need.empty()) {
                char where[96];
                snprintf(where, sizeof(where), "x %.0f (%.2f s)", (double)g_bestDeathX, wall / 240.0);
                g_kHumanFrom = std::max(g_kHumanFrom, wall + 480);
                kp::onLimitsDropped(where);
                writeResult("dpsolve:   [kairos] past " + std::string(where) + " only without the human limits ("
                            + need + "): off up to t=" + std::to_string(g_kHumanFrom)
                            + ", Optimize fits that stretch to them after");
            } else {
                writeResult("dpsolve:   [kairos] past the wall without the human limits, with inputs that keep "
                            "them - not the limits; they are back on");
            }
        }
        g_stallRuns = 0;
        if (g_capTier != 0) {
            writeResult("dpsolve:   deeper - back to the cheap search");
            g_capTier = 0;
        }
        if (g_horizonNow != horizonDefault()) {
            if (horizonDefault() == g_horizonFull)
                snprintf(b, sizeof(b), "dpsolve:   deeper at t=%lld - back to planning the whole "
                         "level (%d ticks)", dt, g_horizonFull);
            else
                snprintf(b, sizeof(b), "dpsolve:   deeper at t=%lld - back to planning %d ticks "
                         "at a time", dt, horizonDefault());
            writeResult(b);
            g_horizonNow = horizonDefault();
        }
        g_triedWholeLevel = false;   // a new wall gets its own whole-level try (step mode)
        g_bestDeath = dt;
        g_bestDeathX = deathX;
        if (g_voidDt == dt) voidDeeper("is");   // cfg voidband: this death closed its run
        // cfg routeprereq: the new wall's box, or none (a wall that is the coin's own, or no coin's).
        if (g_cfg.routePrereq && g_routeNeedUid != deathNeed) {
            snprintf(b, sizeof(b), deathNeed >= 0
                         ? "dpsolve:   [route] the wall is the box uid %d - searches that start before "
                           "it have to enter it"
                         : "dpsolve:   [route] the wall no longer waits on a box (was uid %d)",
                     deathNeed >= 0 ? deathNeed : g_routeNeedUid);
            writeResult(b);
        }
        g_routeNeedUid = deathNeed;
        g_best = g_plan;
        g_spentAnchors.clear();     // a different wall; nothing is known about its anchors
        g_triedMissedPortal = false;
        g_escalations = 0;
        anchors::keepAsDeepest();   // the plan and the trajectory that produced it, together
        cpflight::keepAsDeepest();  // ...and its checkpoints (cfg cpflight)
        anchors::ladderOn(false);
        g_curBackoff = kBackOff;
        g_followSolved = 0;
        g_followPeak = -1;     // a new wall: nothing has been followed on it yet
        g_followForced = 0;    // the route made progress; the normal flow owns it now
        outcome = itermap::KindDeeper;
    } else if (g_lastTailSolved
               && (dt > std::max(g_lastDeath, g_followPeak) || g_followSolved < 4)) {
        const long long ref = std::max(g_lastDeath, g_followPeak);
        outcome = itermap::KindFollow;
        if (dt > g_lastDeath && dt <= ref) {
            snprintf(b, sizeof(b), "dpsolve:   t=%lld is above the last death (t=%lld) but not "
                     "above this wall's follow peak (t=%lld) - a repeat, not progress", dt,
                     g_lastDeath, ref);
            writeResult(b);
        }
        g_followSolved = (dt > ref) ? 0 : g_followSolved + 1;
        g_followPeak = std::max(g_followPeak, dt);
        snprintf(b, sizeof(b), "dpsolve:   regression to t=%lld on a solved branch - following "
                 "it (%d/4, best %lld)", dt, g_followSolved, g_bestDeath);
        writeResult(b);
        anchors::ladderOn(false);
        g_curBackoff = kBackOff;
    } else if (g_followForced > 0 && (dt > g_lastDeath || g_followForced > 1)) {
        outcome = itermap::KindForced;
        if (dt <= g_lastDeath) --g_followForced;
        snprintf(b, sizeof(b), "dpsolve:   %s t=%lld on the forced route - "
                 "following it (%d left, best %lld)",
                 dt > g_lastDeath ? "progress to" : "regression to",
                 dt, g_followForced, g_bestDeath);
        writeResult(b);
        anchors::ladderOn(false);
        g_curBackoff = kBackOff;
    } else {
        if (g_anchorT > 0) g_spentAnchors.insert(g_anchorT);
        if (g_bestDeath >= 0) g_plan = g_best;   // an empty best is still the best (autoFire)
        anchors::ladderOn(true);
        g_curBackoff = std::min(g_curBackoff * 4, 3600);
        useT = g_bestDeath;
        snprintf(b, sizeof(b), "dpsolve:   no improvement (best %lld) - rewound to the deepest "
                 "plan (t=%lld), backoff %d", g_bestDeath, g_bestDeath, g_curBackoff);
        writeResult(b);
        ++g_stallRuns;
        {
            int pulse = 0, every = 0;
            if (g_kLimitWall >= 0) {
                g_kLimitWall = -1;
                writeResult("dpsolve:   [kairos] the round without the human limits got no deeper - "
                            "not the limits; they are back on");
            } else if (g_stallRuns >= 2 && g_kLimitTested != g_bestDeath && kLimitsOn(pulse, every)) {
                g_kLimitWall = g_kLimitTested = g_bestDeath;
                snprintf(b, sizeof(b), "dpsolve:   [kairos] stuck at t=%lld with the human limits on - "
                         "one round without them", g_bestDeath);
                writeResult(b);
            }
        }
        if (g_stallRuns >= kStallToUnseen && !g_needUnseen && g_cfg.dpWorld) {
            g_needUnseen = true;
            writeResult("dpsolve:   stuck - from here the search must also enter the doors "
                        "whose effect it has not seen");
        }
        if (g_stallRuns >= kStallToShorten && g_horizonNow == g_horizonFull
            && horizonShort() != g_horizonFull) {
            g_horizonNow = horizonShort();
            snprintf(b, sizeof(b), "dpsolve:   %d rounds without progress - planning %d ticks "
                     "at a time so the game checks each step", g_stallRuns, horizonShort());
            writeResult(b);
        }
    }
    if (wasWedged) outcome = itermap::KindWedge;
    {
        const std::vector<AnchorRow>* savedSrcIm = anchors::g_src;
        anchors::ladderOn(false);
        float dy = 0.f;
        for (long long k = std::min(dt, 400000LL); k > 0 && k > dt - 8; --k)
            if (const AnchorRow* r = anchors::row(k)) { dy = r->y; break; }
        mapTail(*anchors::g_src, g_iter, outcome, g_anchorT, dt);
        anchors::g_src = savedSrcIm;
        itermap::addDeath(g_iter, dt, deathX, dy, outcome, g_anchorT, g_anchorX, g_curBackoff);
    }
    if (g_cfg.dpStepHorizon > 0) {
        const long long ran = dt - std::max<long long>(0, g_anchorT);
        const bool sameWall = (outcome != itermap::KindDeeper) && g_lastDeath >= 0
                              && std::llabs(dt - g_lastDeath) <= 8;
        g_wallRepeats = sameWall ? g_wallRepeats + 1 : 0;
        const int want = (ran < g_cfg.dpStepHorizon && g_wallRepeats < 2) ? g_cfg.dpStepHorizon
                                                                            : g_horizonFull;
        // cfg dpcontenthorizon: the second stop at one wall asks for the whole level on purpose.
        g_horizonFullAsked = g_wallRepeats >= 2;
        if (want != g_horizonNow) {
            snprintf(b, sizeof(b), "dpsolve:   the game ended the plan %lld ticks past its anchor "
                     "- planning %d ticks next", ran, want);
            writeResult(b);
            g_horizonNow = want;
        }
    }
    g_lastDeath = dt;
    g_lastDeathX = deathX;
    g_lastDeathPhys = postScored ? physDt : dt;
    g_lastDeathCoinMiss = coinMiss;
    if (g_cfg.dpSecCoinRung && g_cfg.coinRoute) {
        if (g_coinWallCoin >= 0 && (size_t)g_coinWallCoin < solver::g_coinGdTick.size()
            && solver::g_coinGdTick[(size_t)g_coinWallCoin] >= 0) {
            writeResult("dpsolve:   [secauto] coin " + std::to_string(g_coinWallCoin)
                        + " taken - its wall (t=" + std::to_string(g_coinWall) + ") is down");
            g_coinWall = -1;
            g_coinWallCoin = -1;
            g_coinWallPlan.clear();
        }
        const int wallCoin = solver::g_coinMissIdx >= 0 ? solver::g_coinMissIdx
                             : (coinMissPost ? g_postMissCoin : -1);
        const long long wallT = (solver::g_coinMissIdx < 0 && coinMissPost && g_postMissCrossT > 0)
                                    ? g_postMissCrossT : physDt;
        if (coinMiss && wallCoin >= 0 && deathNeed < 0) {
            if (g_coinWallCoin != wallCoin || std::llabs(g_coinWall - wallT) > kAutoCreep)
                writeResult("dpsolve:   [secauto] coin wall: coin " + std::to_string(wallCoin)
                            + (solver::g_coinMissIdx >= 0 ? " cut" : " missed (clear refused)")
                            + " at t=" + std::to_string(wallT)
                            + (wallT != physDt ? " (its first crossing; ranked at t="
                                                     + std::to_string(physDt) + ")" : ""));
            g_coinWall = wallT;
            g_coinWallCoin = wallCoin;
            g_coinWallPlan = g_plan;
        } else if (coinMiss && wallCoin >= 0 && deathNeed >= 0 && g_coinWall >= 0) {
            writeResult("dpsolve:   [route] coin wall (coin " + std::to_string(g_coinWallCoin)
                        + " at t=" + std::to_string(g_coinWall) + ") set aside - the wall is the box"
                        " uid " + std::to_string(deathNeed));
            g_coinWall = -1;
            g_coinWallCoin = -1;
            g_coinWallPlan.clear();
        }
    }
    if (g_cfg.dpSecAuto) {
        const double roundWork = g_autoRoundWork + kWorkReplayTick * (double)std::max(0LL, dt);
        g_autoRoundWork = 0.0;
        // The round that FOUND the wall is not rent paid at it -- it got there.
        if (!autoSync()) g_autoWallWork += roundWork;
        ++g_autoWallRounds;   // a round that did not move the wall (autoSync restarts the count)
        char why[128] = "";
        if (g_cfg.dpSecNoRec > 0 && g_autoNoRec >= g_cfg.dpSecNoRec)
            snprintf(why, sizeof(why), "the recorder wrote nothing for %d deaths", g_autoNoRec);
        else if (g_cfg.dpSecStall > 0 && g_autoWallRounds >= g_cfg.dpSecStall)
            snprintf(why, sizeof(why), "%d rounds", g_autoWallRounds);
        else if (g_cfg.dpSecRent && g_autoWallWork >= autoRungEstimate())
            snprintf(why, sizeof(why), "%.1f s-equiv of work spent (a rung is ~%.1f, %d measured)",
                     g_autoWallWork / 1e6, autoRungEstimate() / 1e6, g_autoRungN);
        if (why[0] && g_cfg.dpUnseenFirst && g_cfg.dpWorld && !g_needUnseen && !g_secReqPending) {
            g_needUnseen = true;
            writeResult(std::string("dpsolve:   [secauto] ") + why + " - the doors the search has "
                        "not entered first, then the rung (cfg dpunseenfirst)");
            why[0] = 0;
        }
        if (why[0] && g_cfg.dpSecRecAway && !g_secReqPending && g_lastDeathPhys >= 0
            && g_lastDeathPhys + kAutoCreep < rungWall()) {
            g_autoRecordThenRung = true;
            g_autoRecordWhy = why;
            writeResult(std::string("dpsolve:   [secauto] ") + why + " at the wall t="
                        + std::to_string(rungWall()) + ", but this round died at t="
                        + std::to_string(g_lastDeathPhys)
                        + " - recording it first, then the rung (cfg dpsecrecaway)");
            why[0] = 0;
        }
        if (why[0] && autoFire(why)) return;
        if (g_cfg.dpSecSolved && flownClaimedGoal && !coinMiss && !g_secReqPending
            && dt >= g_bestDeath - kAutoCreep && !autoTried()) {
            g_autoRecordThenRung = true;
            g_autoRecordWhy.clear();
            writeResult("dpsolve:   [secauto] a plan that reached the end died at t="
                        + std::to_string(dt) + " - recording, then a section solve");
        }
    }
    spawn(JobLadder, useT, "learning where the model was wrong, then re-solving");
}

constexpr long long kDeepRefire = 300;

inline bool levelHasMovingParts() {
    return g_cfg.dpGroups && grouptrace::g_on && !grouptrace::g_objs.empty();
}

inline bool startBootstrapRecord() {
    if (!levelHasMovingParts()) return false;
    g_recordKind = RecBootstrap;
    g_deepActive = true;
    g_cfg.noDeath = true;
    g_cfg.inputs.clear();
    g_recordRequest = true;
    g_hudPhase = "watching the level run itself, to see what moves";
    writeResult("dpsolve: recording what the level does on its own (no inputs, nodeath) "
                "before solving");
    return true;
}

inline bool startDeepRecord() {
    if (!levelHasMovingParts() || g_deepActive) return false;
    if (g_bestDeath < 0) return false;   // an empty best still has a trajectory to record
    if (g_deepDoneAt >= 0 && (g_bestDeath - g_deepDoneAt) < kDeepRefire) return false;
    g_deepDoneAt = g_bestDeath;
    g_recordKind = RecDeep;
    g_deepActive = true;
    g_deepStartTick = 0;
    g_cfg.noDeath = true;
    g_cfg.inputs = g_best;
    g_hudPhase = "recording the moving geometry to the end of the level";
    char b[192];
    snprintf(b, sizeof(b), "dpsolve: no anchor left - re-recording the moving geometry with "
             "nodeath over the deepest plan (t=%lld)", g_bestDeath);
    writeResult(b);
    g_recordRequest = true;
    return true;
}

inline void finishDeepRecord(const char* why) {
    if (!g_deepActive) return;
    const int kind = g_recordKind;
    g_deepActive = false;
    g_recordKind = RecNone;
    g_cfg.noDeath = false;
    const grouptrace::Roll r = grouptrace::g_lastRoll;
    const std::string dst = (kind == RecBootstrap) ? g_groupsBootPath : g_groupsDeepPath;
    const char* what = (kind == RecBootstrap) ? "bootstrap record" : "deep record";
    char b[240];
    if (r.rows >= 100) {
        const bool copied = copyRecording(std::string(DATA_DIR) + "/grouptrace_last.txt", dst);
        snprintf(b, sizeof(b), "dpsolve:   %s (%s): %lld samples to t=%lld - %s",
                 what, why, r.rows, r.depth, copied ? "adopted" : "COPY FAILED");
        if (copied && g_cfg.groupsRetime) {
            std::vector<float>& x = (kind == RecBootstrap) ? g_groupsBootX : g_groupsDeepX;
            x.assign(anchors::g_live.size(), std::numeric_limits<float>::quiet_NaN());
            for (size_t t = 0; t < anchors::g_live.size(); ++t)
                if (anchors::g_live[t].valid) x[t] = anchors::g_live[t].x;
        }
    } else {
        snprintf(b, sizeof(b), "dpsolve:   %s (%s): only %lld samples - discarded",
                 what, why, r.rows);
    }
    writeResult(b);
    if (kind == RecBootstrap) {
        // Now solve (or verify the seed), in a world that is already known to move.
        beginFirstAttempt();
        return;
    }
    g_plan = g_best;
    anchors::ladderOn(true);
    g_curBackoff = kBackOff;
    spawn(JobLadder, g_bestDeath, "re-solving against the recorded geometry");
}

inline bool escalate() {
    ++g_escalations;
    if (g_escalations > 12) return false;    // backstop; the flags below already bound this
    bool routeMoved = false;
    if (g_cfg.routePrereq && route::g_built)
        for (size_t i = 0; i < solver::g_coins.size() && i < 8 && !routeMoved; ++i) {
            if (!(g_postCoins & (1 << i)) || routeEngaged(i)) continue;
            if (i < solver::g_coinGdTick.size() && solver::g_coinGdTick[i] >= 0) continue;
            routeMoved = routeEngage(i, "the ladder found no anchor");
        }
    if (routeMoved) {
        // (the wall moved to the box; asked again below)
    } else if (g_cfg.dpWorld && !g_needUnseen) {
        g_needUnseen = true;
        writeResult("dpsolve:   no anchor - trying again with the doors the search has not "
                    "entered");
    } else if (g_cfg.dpStepHorizon <= 0 && g_horizonNow == g_horizonFull) {
        g_horizonNow = kHorizonShort;
        char b[192];
        snprintf(b, sizeof(b), "dpsolve:   no anchor - trying again in %d-tick steps, so the "
                 "game checks each one", kHorizonShort);
        writeResult(b);
    } else if (g_cfg.dpStepHorizon > 0 && g_horizonNow != g_horizonFull && !g_triedWholeLevel) {
        g_triedWholeLevel = true;
        g_horizonNow = g_horizonFull;
        g_horizonFullAsked = true;   // (cfg dpcontenthorizon: on purpose)
        char b[192];
        snprintf(b, sizeof(b), "dpsolve:   no anchor - trying again planning the whole level "
                 "(%d ticks)", g_horizonFull);
        writeResult(b);
    } else if (g_needTrigSuspect & ~g_needTrigDropped) {
        const unsigned long long rel = g_needTrigSuspect & ~g_needTrigDropped;
        g_needTrigDropped |= rel;
        char b[192];
        snprintf(b, sizeof(b), "dpsolve:   no anchor - releasing the doors it cannot reach "
                 "from here (mask 0x%llx)", rel);
        writeResult(b);
    } else if (startDeepRecord()) {
        return true;    // that one drives the level itself; it will come back through poll
    } else if (!g_triedMissedPortal) {
        g_triedMissedPortal = true;
        const long long mt = missedPortalTick();
        if (mt <= 0) {
            writeResult("dpsolve:   no anchor - the run took every mode portal it crossed, so "
                        "the route is not the thing that is wrong");
        } else {
            g_forceAnchorT = mt - kPortalSettle;
            char fb[96];
            snprintf(fb, sizeof(fb), "%.1f,%.1f,%d,-1e18,1e18",
                     g_missedPortalX + 45.0, g_missedPortalX + 75.0, g_missedPortalMode);
            g_forcePortalBand = fb;
            char b[256];
            snprintf(b, sizeof(b), "dpsolve:   no anchor - it crossed a mode portal at t=%lld "
                     "and came out in the wrong mode; going back there and forcing the "
                     "crossing (--deadband %s)", mt, fb);
            writeResult(b);
        }
    } else if (g_cfg.dpSecTierFirst > 0 && g_capTier < (int)(sizeof(kCapTiers) / sizeof(kCapTiers[0]))
               && autoFire("the ladder found no anchor")) {
        --g_escalations;
        return true;
    } else if (g_cfg.dpCapTiers > 0
               && g_capTier < (int)(sizeof(kCapTiers) / sizeof(kCapTiers[0]))) {
        // (cfg dpcaptiers: off, this is skipped and the restart below runs at the base setting)
        ++g_capTier;
        if (g_capTier == (int)(sizeof(kCapTiers) / sizeof(kCapTiers[0]))) g_topTierIter = g_iter;
        char b[192];
        snprintf(b, sizeof(b), "dpsolve:   no anchor - trying again with room for %lld states "
                 "on a coarser grid", kCapTiers[g_capTier - 1]);
        writeResult(b);
    } else if (!g_coldRestarted) {
        g_coldRestarted = true;
        if (g_cfg.dpCapTiers <= 0)
            writeResult("dpsolve:   (the capacity tiers were skipped - cfg dpcaptiers=0)");
        writeResult("dpsolve:   no anchor - solving the level again from the start, with what "
                    "this run has learnt");
        g_horizonNow = g_horizonFull;   // a whole-level attempt, not a step
        g_horizonFullAsked = true;      // (cfg dpcontenthorizon: on purpose)
        g_curBackoff = kBackOff;
        g_stallRuns = 0;
        anchors::ladderOn(false);
        spawn(JobFirstSolve, 0, "solving again from the start");
        return true;
    } else if (liftPhantomVetoes("no solvable anchor")) {
    } else {
        return false;
    }
    // The setup changed; ask the same wall again.
    g_curBackoff = kBackOff;
    spawn(JobLadder, g_bestDeath >= 0 ? g_bestDeath : g_lastDeath,
          "re-solving with more of the search turned on");
    return true;
}

inline bool wantsShow() {
    if (!g_cfg.dpSolve || g_dpShowSolution) return false;
    return (g_cfg.dpShow >= 0) ? (g_cfg.dpShow == 1) : g_uiSession;
}

inline bool takesOverClear() {
    if (g_cfg.dpSolve && !g_dpShowSolution
        && (cpflight::g_from >= 0 || cpflight::g_probeFlying || cpflight::g_controlFlying))
        return true;
    return g_recordAttempt || g_deepActive || wantsShow()
           || (g_cfg.dpSolve && !g_dpShowSolution
               && (levelslice::g_phase == levelslice::Sliced
                   || levelslice::g_phase == levelslice::RefFlight));
}

inline void tidyPlayerModes() {
    if (!g_psnapPlayerWritten) return;
    auto* pl = PlayLayer::get();
    if (!pl) return;
    for (PlayerObject* p : {pl->m_player1, pl->m_player2}) {
        if (!p) continue;
        p->m_isShip = true;   p->toggleFlyMode(false, true);
        p->m_isBird = true;   p->toggleBirdMode(false, true);
        p->m_isBall = true;   p->toggleRollMode(false, true);
        p->m_isDart = true;   p->toggleDartMode(false, true);
        p->m_isRobot = true;  p->toggleRobotMode(false, true);
        p->m_isSpider = true; p->toggleSpiderMode(false, true);
        p->m_isSwing = true;  p->toggleSwingMode(false, true);
    }
    writeResult("dpsolve: the players' modes tidied before the showing (a snapshot had written "
                "their flags)");
}

inline bool onCleared() {
    // The run is told the flight cleared (dp_bridge runVerified), whatever the show does next.
    {
        std::vector<std::pair<long long, int>> fe;
        for (const InputCmd& c : g_plan) fe.emplace_back((long long)c.step, c.down ? 1 : 0);
        writeResult(dpbridge::runVerified(fe, -1, (double)g_hudVerifiedX, true, -1));
    }
    if (!wantsShow()) return false;
    g_dpShowSolution = true;   // badge -> REPLAY, audio stops being silenced
    g_showRequest = true;
    writeResult("dpsolve: cleared after " + std::to_string(g_iter)
                + " repair rounds - replaying the solution at 1x");
    return true;
}

// Called once per frame. Installs whatever the worker thread produced, at a frame boundary.
inline void poll() {
    if (!g_running.load() && g_pendingLayer) {
        GJBaseGameLayer* l = g_pendingLayer;
        g_pendingLayer = nullptr;
        if (g_started && !g_sessionOver && g_cfg.dpSolve && PlayLayer::get() == l) start(l);
        else
            writeResult(std::string("dpsolve: a queued start was dropped (started=")
                        + (g_started ? "1" : "0") + " over=" + (g_sessionOver ? "1" : "0")
                        + " solve=" + (g_cfg.dpSolve ? "1" : "0") + " sameLayer="
                        + (PlayLayer::get() == l ? "1" : "0") + ")");
    }
    if (g_recordRequest) {
        g_recordRequest = false;
        g_paused = false;
        if (auto* pl = PlayLayer::get()) pl->resetLevel();
        return;
    }
    if (g_deepActive && g_horizon > 0 && g_tick > (long long)g_horizon * 2) {
        finishDeepRecord("did not reach the end");
        return;
    }
    if (g_deepActive) {
        static long long lastMoveTick = 0;
        static double lastMoveX = -1e18;
        double px = 0.0;
        if (auto* pl = PlayLayer::get())
            if (pl->m_player1) px = pl->m_player1->getPositionX();
        if (std::fabs(px - lastMoveX) > 1.0) { lastMoveX = px; lastMoveTick = g_tick; }
        if (px < -50.0) {
            finishDeepRecord("fell off the board");
            return;
        }
        if (g_tick - lastMoveTick > 3600) {
            finishDeepRecord("stopped getting anywhere");
            return;
        }
    }
    if (g_showRequest) {
        g_showRequest = false;
        g_paused = false;
        clearManualPause();          // do not start the showing frozen on someone's F2
        watchSpeedSet(WATCH_SPEED_1X);
        g_hudPhase = "replaying the solution";
        g_cfg.maxAttempts = g_finishedAttempts + 1;   // show it once, then the session ends
        tidyPlayerModes();
        if (!renderingOn()) {
            renderTogglePress();
        } else {
            g_realtimeOverride = true;
            if (auto* pl = PlayLayer::get()) pl->resetLevel();
        }
        notify::show("gdsolver: solved - replaying the solution",
                     NotificationIcon::Success, 4.f);
        return;
    }
    if (g_secReqPending && (!g_started || g_sessionOver)) g_secReqPending = false;
    if (g_secReqPending && g_cfg.dpSolve && !g_dpShowSolution && !g_stop) {
        g_stop = true;
        writeResult("secsolve handoff: the repair loop stops after the solve in flight");
    }
    if (g_secReqPending && g_cfg.dpSolve && !g_dpShowSolution
        && !g_running.load() && !g_deepActive) {
        g_secReqPending = false;
        if (g_bestDeath >= 0 && g_secReqStart >= g_bestDeath) {
            writeResult("secsolve handoff: refused - start t=" + std::to_string(g_secReqStart)
                        + " is past the verified depth t=" + std::to_string(g_bestDeath));
            return;
        }
        g_stop = true;               // no more spawns; onDeath's guard makes deaths inert
        if (g_bestDeath < 0)
            writeResult("secsolve handoff: no verified plan yet - the prefix run is inputless");
        const bool coinRung = g_secReqRung && g_secReqCoin >= 0 && !g_coinWallPlan.empty();
        std::vector<InputCmd>& rungSrc = coinRung ? g_coinWallPlan : g_best;
        std::sort(rungSrc.begin(), rungSrc.end(),
                  [](const InputCmd& a, const InputCmd& c) { return a.step < c.step; });
        g_cfg.inputs = rungSrc;
        {
            const std::string pp = std::string(DATA_DIR) + "/secsolve_prefix.txt";
            if (writeInputsFile(pp, rungSrc))
                writeResult("secsolve handoff: prefix written to " + pp);
        }
        g_secRung = g_secReqRung;
        g_secReqRung = false;
        g_secPinWall = coinRung ? g_coinWall : g_bestDeath;
        // ...and the coin the search has to take (hooks_gamelayer, cfg seccoins).
        secsolve::g_rungCoin = coinRung ? g_secReqCoin : -1;
        g_secReqCoin = -1;
        // ...and, for a rung of cfg dpsecstate, the size it forces (the probe) or keeps.
        g_secState = g_secRung ? g_secReqState : 0;
        if (g_secState == 1) secsolve::g_forceP1Size = g_secReqSize;
        if (g_secState == 2 || g_secState == 3) secsolve::g_keepP1Size = g_secReqSize;
        if (g_secState == 2) g_secPinWall = -1;
        g_secReqState = 0;
        g_secReqSize = -1;
        g_secSavePractice = g_cfg.practiceAt;
        g_secSaveCkpt = g_cfg.checkpointAt;
        g_secSaveFastloops = g_cfg.fastloops;
        g_secSaveMaxAttempts = g_cfg.maxAttempts;
        g_cfg.practiceAt = (int)std::max(1LL, g_secReqStart - 100);
        g_cfg.checkpointAt = (int)g_secReqStart;
        g_cfg.fastloops = 1;
        if (!g_secRung) g_cfg.maxAttempts = g_finishedAttempts + 5;
        // ...and a rung is abandoned instead (secRungAbandon, kSecRungPrefixTries).
        g_secRungPrefixFails = 0;
        g_secRungFailAttempt = -1;
        secsolve::g_on = true;
        secsolve::g_done = false;
        secsolve::g_startTick = g_secReqStart;
        secsolve::g_targetX = g_secReqTarget;
        g_secSaveTargetDepth = secsolve::g_targetDepth;
        if (g_secReqDepth > 0) secsolve::g_targetDepth = (int)g_secReqDepth;
        if (g_secReqHorizon > 0) secsolve::g_horizon = (int)g_secReqHorizon;
        if (g_secReqCap > 0) secsolve::g_cap = (size_t)g_secReqCap;
        g_secLastReq = {g_secReqStart, g_secReqDepth, g_secReqHorizon, g_secReqCap, g_secReqTarget,
                        secsolve::g_rungCoin};
        secsolve::g_spineUntil = g_secRung ? g_secReqSpineUntil : -1;
        g_secReqSpineUntil = -1;
        g_secCpRedo = g_secRung && g_secReqForceCp;
        secsolve::g_wsnapForce = g_secRung && g_secReqForceWsnap;
        if (g_secReqForceWsnap) {
            g_secReqForceWsnap = false;
            writeResult("secrung: this window again with its wsnap set - the plain snapshot search "
                        "found nothing (cfg secwsnapgate)");
        }
        if (g_secReqForceCp) {
            g_secReqForceCp = false;
            secsolve::g_snapMode = 0;      // checkpoints; the next ordinary rung puts psnap back
            secsolve::g_verifyEvery = 0;   // (autoFire, when both are 0)
            writeResult("secrung: this window again on checkpoints - the player snapshot parted "
                        "from the game in it");
        }
        secsolve::g_log = true;      // without seclayer: lines, a capped frontier and a real
                                     // wall cannot be told apart
        char hb[256];
        snprintf(hb, sizeof(hb),
                 "%s handoff: loop %s after iter %d - replaying %zu inputs to "
                 "t=%lld, then solving to x=%.1f depth=%d (horizon=%d cap=%zu)",
                 g_secRung ? "secrung" : "secsolve",
                 g_secRung ? "suspended" : "stopped",
                 g_iter, g_cfg.inputs.size(), (long long)g_secReqStart,
                 secsolve::g_targetX, secsolve::g_targetDepth, secsolve::g_horizon,
                 secsolve::g_cap);
        writeResult(hb);
        g_hudPhase = "secsolve: replaying to the section head";
        g_paused = false;
        secRenderHold();   // the screen stays off until the rung ends (session_hotkeys.hpp)
        if (auto* pl = PlayLayer::get()) pl->resetLevel();
        return;
    }
    ckConsider();
    if (cpflight::g_probeQueued.rec) {
        if (g_running.load() && !g_finished.load() && !g_cfg.dpCheck && !g_stop && !g_sessionOver
            && g_started && !g_deepActive && !g_dpShowSolution && !cpflight::g_probeFlying) {
            cpflight::g_pending = std::move(cpflight::g_probeQueued);
            g_paused = false;
        }
        cpflight::g_probeQueued = cpflight::Choice{};
    }
    if (!g_running.load() || !g_finished.load()) return;
    if (cpflight::g_probeFlying || cpflight::g_pending.probe || cpflight::g_controlPending
        || cpflight::g_controlFlying)
        return;
    g_running = false;
    g_finished = false;
    const bool hadFlight = g_ckInstalled;
    g_ckFlying = false;
    g_ckInstalled = false;
    if (g_cfg.dpCheck) {
        char cb[160];
        snprintf(cb, sizeof(cb), "dpsolve:   [check] this job: %zu flight(s), %zu passed, "
                 "%zu refuted", g_ckFlights, g_ckPassed, g_ckDeaths);
        writeResult(cb);
        if (g_cfg.dpCheckFirst) {
            g_cfg.dpCheck = false;
            dpbridge::checkSubscribe(false);
            writeResult("dpsolve:   [checkfirst] the first solve is over - no more checkpoint "
                        "flights this run");
        }
    }
    if (g_ckAbandonJob) {
        g_ckAbandonJob = false;
        writeResult("dpsolve:   [check] the job was abandoned for a checkpoint clear - its result "
                    "is discarded");
        return;
    }
    if (g_resultGeneration != g_generation.load()
        || !g_started || g_sessionOver || !g_cfg.dpSolve) {
        writeResult("dpsolve: discarded a stale result from a session that already ended (gen "
                    + std::to_string(g_resultGeneration) + " != "
                    + std::to_string(g_generation.load()) + ")");
        return;
    }
    const double sec = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - g_t0).count();
    char b[256];
    char rcs[24] = "";
    if (g_iter == 0) snprintf(rcs, sizeof(rcs), " rc=%d", g_rc);
    snprintf(b, sizeof(b), "dpsolve: done in %.1fs, %zu inputs%s%s",
             sec, g_plan.size(), rcs,
             !g_haveNewPlan ? " (NO PLAN)"
                            : g_plan.empty() ? " (a plan that never presses - flying it)" : "");
    writeResult(b);
    g_dpSolving = false;
    if (!g_haveNewPlan) {
        if (hadFlight) g_cfg.inputs = g_plan;   // never leave a flight installed as the plan
        g_paused = false;      // never leave the game frozen because the solve failed
        if (g_autoPinOut) {
            g_autoPinOut = false;
            if (autoFire("every rung past the pin is spent")) return;
            writeResult("dpsolve:   no section solve for this wall - dropping the pin at t="
                        + std::to_string(g_secPin));
            g_secPin = -1;
        }
        if (g_autoRecordThenRung) {
            g_autoRecordThenRung = false;
            const std::string why = g_autoRecordWhy.empty() ? std::string("a plan that reached the end died")
                                                            : g_autoRecordWhy;
            g_autoRecordWhy.clear();
            if (autoFire(why.c_str())) return;
            if (g_secReqPending) return;   // a command's section solve is queued and takes over
        }
        // Before conceding: everything that changes how the search is set up gets a turn.
        if (g_iter > 0 && escalate()) return;
        if (g_iter > 0 && g_cfg.dpSecAuto && autoFire("no anchor on this prefix can be solved"))
            return;
        g_stop = true;
        const char* why = g_iter == 0 ? "the model cannot leave the start of this level"
                                      : "no anchor on this prefix can be solved";
        g_hudPhase = why;
        giveUp(why, "dpsolve_stuck");
        return;
    }
    std::sort(g_plan.begin(), g_plan.end(),
              [](const InputCmd& a, const InputCmd& c) { return a.step < c.step; });
    g_cfg.inputs = g_plan;
    if (g_cfg.dpCheck && g_cfg.dpCheckObs) ckObsCompare(sec);
    g_paused = false;
    g_hudPhase = g_iter == 0 ? "verifying the first plan in the game"
                             : "verifying the repaired plan in the game";
    g_hudAnchorT = g_anchorT;
    g_hudAnchorX = g_anchorX;
    if (auto* pl = PlayLayer::get())
        if (!cpflight::requestStart(g_cfg.inputs)) pl->resetLevel();
    if (g_iter == 0) {
        char note[192];
        snprintf(note, sizeof(note), "gdsolver: first plan in %.0fs (%zu inputs) - now testing "
                 "it in the game", sec, g_plan.size());
        notify::show(note, NotificationIcon::Info, 4.f);
    }
}
}  // namespace dpsolve
}  // namespace p1
