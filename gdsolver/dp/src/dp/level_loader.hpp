#pragma once
#include <cstdarg>
#include <limits>
#include <map>
#include "dp/triggers.hpp"

namespace dp {
struct ObbBox { double hw, hh, c, s; };
inline std::unordered_map<int, ObbBox> loadObb(const std::string& path) {
    std::unordered_map<int, ObbBox> m;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "obb: cannot open %s\n", path.c_str());
        return m;
    }
    std::string line;
    std::getline(in, line);   // header
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string f[14];
        int n = 0;
        for (; n < 14 && std::getline(ss, f[n], ','); ++n) {}
        if (n < 14) continue;
        const int uid = std::atoi(f[0].c_str());
        double px[4], py[4];
        for (int i = 0; i < 4; ++i) {
            px[i] = std::atof(f[6 + 2 * i].c_str());
            py[i] = std::atof(f[7 + 2 * i].c_str());
        }
        if (n >= 15 && std::atoi(f[14].c_str()) == 0) continue;
        const double e1x = px[1] - px[0], e1y = py[1] - py[0];
        const double e2x = px[2] - px[1], e2y = py[2] - py[1];
        const double l1 = std::hypot(e1x, e1y), l2 = std::hypot(e2x, e2y);
        if (l1 < 1e-6 || l2 < 1e-6) continue;
        m[uid] = ObbBox{l1 * 0.5, l2 * 0.5, e1x / l1, e1y / l1};
    }
    std::printf("obb: %zu turned boxes from %s\n", m.size(), path.c_str());
    return m;
}
inline std::unordered_map<int, ObbBox> g_obb;

inline constexpr int kObjFields = 64;

inline void splitCommaFields(const std::string& line, std::string* f, int n) {
    size_t pos = 0;
    for (int i = 0; i < n && pos < line.size(); ++i) {
        const size_t c = line.find(',', pos);
        if (c == std::string::npos) {
            f[i].assign(line, pos, std::string::npos);
            break;
        }
        f[i].assign(line, pos, c - pos);
        pos = c + 1;
    }
}

inline std::string* g_loadPrinted = nullptr;
inline void loadPrintf(const char* fmt, ...) {
    va_list ap, again;
    va_start(ap, fmt);
    va_copy(again, ap);
    char buf[1024];
    const int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    std::string big;
    const char* s = buf;
    if (n >= (int)sizeof buf) {
        big.resize((size_t)n + 1);
        std::vsnprintf(big.data(), big.size(), fmt, again);
        big.resize((size_t)n);
        s = big.c_str();
    }
    va_end(again);
    if (n <= 0) return;
    std::fwrite(s, 1, (size_t)n, stdout);
    if (g_loadPrinted) g_loadPrinted->append(s, (size_t)n);
}

inline bool isAnticheatSpikeRow(const std::string& row, int maxUid) {
    std::stringstream ss(row);
    std::string f;
    std::vector<std::string> v;
    while (v.size() < 17 && std::getline(ss, f, ',')) v.push_back(f);
    return v.size() == 17 && v[0] == "8" && v[2] == "0" && v[3] == "105" && v[4] == "6"
           && v[5] == "12" && v[15] == "30" && v[16] == "30" && std::atoi(v[7].c_str()) == maxUid;
}
inline bool g_glitchDeco = false;   // --glitchdeco (the mode's other switches: thread_pool.hpp)
inline int addHazardLookalikes(std::vector<std::string>& rows, int& maxUid) {
    struct Row { std::vector<std::string> f; double cx = 0, cy = 0; int rot = 0; };
    std::vector<Row> rs;
    rs.reserve(rows.size());
    for (size_t i = 1; i < rows.size(); ++i) {
        Row r;
        std::stringstream ss(rows[i]);
        std::string f;
        while (std::getline(ss, f, ',')) r.f.push_back(f);
        while (!r.f.empty() && !r.f.back().empty()
               && (r.f.back().back() == '\r' || r.f.back().back() == '\n'))
            r.f.back().pop_back();
        if (r.f.size() < 10) { r.f.clear(); rs.push_back(r); continue; }
        r.cx = std::atof(r.f[2].c_str());
        r.cy = std::atof(r.f[3].c_str());
        r.rot = ((int)std::lround(std::atof(r.f[9].c_str())) % 360 + 360) % 360;
        rs.push_back(std::move(r));
    }
    auto key = [](double x, double y) {
        return ((long long)std::llround(x) << 32) ^ (long long)(std::llround(y) & 0xffffffff);
    };
    std::unordered_map<long long, std::vector<size_t>> haz;
    for (size_t i = 0; i < rs.size(); ++i)
        if (!rs[i].f.empty() && rs[i].f[1] == "2") haz[key(rs[i].cx, rs[i].cy)].push_back(i);
    auto onHazard = [&](const Row& d) -> const Row* {
        auto it = haz.find(key(d.cx, d.cy));
        if (it == haz.end()) return nullptr;
        for (size_t hi : it->second) {
            const Row& h = rs[hi];
            if (std::fabs(h.cx - d.cx) < 0.5 && std::fabs(h.cy - d.cy) < 0.5 && h.rot == d.rot)
                return &h;
        }
        return nullptr;
    };
    std::map<std::string, int> total;
    std::map<std::string, std::map<std::string, int>> pairedWith;
    for (const Row& d : rs) {
        if (d.f.empty() || d.f[1] != "7") continue;
        ++total[d.f[0]];
        if (const Row* h = onHazard(d)) ++pairedWith[d.f[0]][h->f[0]];
    }
    std::map<std::string, std::string> lookOf;   // deco id -> hazard id
    for (const auto& kv : pairedWith) {
        const auto best = std::max_element(kv.second.begin(), kv.second.end(),
                                           [](const auto& a, const auto& b) {
                                               return a.second < b.second;
                                           });
        if (best->second >= 3 && best->second >= 0.9 * total[kv.first])
            lookOf[kv.first] = best->first;
    }
    // One template row per (hazard id, rotation): the w/h columns are the rotated box's.
    std::map<std::pair<std::string, int>, const Row*> tmpl;
    for (const Row& h : rs)
        if (!h.f.empty() && h.f[1] == "2") tmpl.emplace(std::make_pair(h.f[0], h.rot), &h);
    int added = 0;
    std::vector<std::string> extra;
    for (const Row& d : rs) {
        if (d.f.empty() || d.f[1] != "7" || d.f[6] != "0") continue;
        const auto lk = lookOf.find(d.f[0]);
        if (lk == lookOf.end() || onHazard(d)) continue;
        const auto t = tmpl.find(std::make_pair(lk->second, d.rot));
        if (t == tmpl.end()) continue;
        std::vector<std::string> f = t->second->f;
        f[2] = d.f[2];
        f[3] = d.f[3];
        f[6] = "0";
        f[7] = std::to_string(++maxUid);
        std::string line;
        for (size_t k = 0; k < f.size(); ++k) line += (k ? "," : "") + f[k];
        extra.push_back(line);
        ++added;
    }
    for (std::string& e : extra) rows.push_back(std::move(e));
    if (added) {
        std::string ids;
        for (const auto& kv : lookOf) ids += " " + kv.first + "~" + kv.second;
        std::printf("glitchdeco: %d hazard look-alike(s) with no hazard under them are hazards "
                    "here (deco~hazard:%s)\n", added, ids.c_str());
    }
    return added;
}

inline Level loadLevelFrom(std::istream& inRaw, const GroupTimeline* gt = nullptr,
                const std::vector<TouchTrig>* tt = nullptr,
                const std::vector<AutoTrig>* at = nullptr) {
    std::istringstream filtered;
    std::unordered_map<int, std::pair<double, double>> rotCentrePos;
    {
        std::string all, row;
        std::vector<std::string> rows;
        while (std::getline(inRaw, row)) rows.push_back(row);
        int maxUid = -1;
        for (size_t i = 1; i < rows.size(); ++i) {
            std::stringstream ss(rows[i]);
            std::string f;
            for (int k = 0; k < 8 && std::getline(ss, f, ','); ++k)
                if (k == 7) maxUid = std::max(maxUid, std::atoi(f.c_str()));
        }
        if ((g_rotTail || g_recEndTail) && !g_rotTailParts.empty()) {
            std::unordered_set<int> centres;
            for (const auto& kv : g_rotTailParts)
                for (const RotTailPart& p : kv.second) centres.insert(p.centreUid);
            for (size_t i = 1; i < rows.size(); ++i) {
                std::stringstream ss(rows[i]);
                std::string f;
                double rcx = 0.0, rcy = 0.0;
                for (int k = 0; k < 8 && std::getline(ss, f, ','); ++k) {
                    if (k == 2) rcx = std::atof(f.c_str());
                    else if (k == 3) rcy = std::atof(f.c_str());
                    else if (k == 7 && centres.count(std::atoi(f.c_str())))
                        rotCentrePos[std::atoi(f.c_str())] = {rcx, rcy};
                }
            }
        }
        const int realMaxUid = maxUid;
        if (g_glitchDeco && !rows.empty()) addHazardLookalikes(rows, maxUid);
        maxUid = realMaxUid;
        for (size_t i = 0; i < rows.size(); ++i)
            if (i == 0 || !isAnticheatSpikeRow(rows[i], maxUid)) all += rows[i] + "\n";
        filtered.str(all);
    }
    std::istream& in = filtered;
    Level L;
    GroupTimeline envSplit;
    std::unordered_map<int, int> envTwin;   // uid -> its twin's uid
    const GroupTimeline* const gtAsRecorded = gt;
    if (gt) {
        std::vector<int> withEnv;
        for (const auto& kv : *gt)
            for (const DynSample& s : kv.second)
                if (s.env) { withEnv.push_back(kv.first); break; }
        if (!withEnv.empty()) {
            envSplit = *gt;
            for (int uid : withEnv) {
                std::vector<DynSample> twin;
                {
                    std::vector<DynSample>& own = envSplit[uid];
                    twin.reserve(own.size());
                    for (DynSample& s : own) {
                        DynSample h = s;
                        h.on = s.env ? s.on : 0;
                        h.rot = 0.f;   // the box is axis-aligned already
                        h.env = 0;
                        twin.push_back(h);
                        if (s.env) s.on = 0;
                    }
                }
                envSplit[envTwinUid(uid)] = std::move(twin);   // after `own` is done with
                envTwin[uid] = envTwinUid(uid);
            }
        }
    }
    int envTwins = 0, envOther = 0;
    g_forceFields.clear();
    g_forceCircles = 0;
    g_pressRings.clear();
    g_forceBoxes.clear();
    g_flipHeadBoxes.clear();
    g_dashStopBoxes.clear();
    g_ctrlTouchBoxes.clear();
    g_timeWarps.clear();
    g_tpTrigs.clear();
    g_gravTrigs.clear();
    g_zoomTrigs.clear();
    g_staticCams.clear();
    g_unlinkTrigs.clear();
    std::string line;
    // header: id,type,cx,cy,w,h,groups,uid,radius,rot[,sy0,sy1,shz]
    std::getline(in, line);
    int colFree = -1, colTouch = -1, colSpawn = -1, colChan = -1;
    int colAxis = -1, colExStat = -1, colRev = -1, colNoCol = -1, colMultiAct = -1;
    int colGMod = -1, colGPl = -1, colDontSlide = -1;
    int colTpFm = -1, colTpF = -1, colTpAng = -1, colTpRMod = -1, colTpRMin = -1, colTpRMax = -1;
    int colOptUdg = -1;
    int colExtCol = -1;   // --bigsection: Extended Collision (Obj::extCol)
    {
        std::stringstream hs(line);
        std::string name;
        for (int i = 0; std::getline(hs, name, ','); ++i) {
            while (!name.empty()
                   && (name.back() == '\r' || name.back() == '\n'))
                name.pop_back();
            if (name == "free") colFree = i;
            else if (name == "touch") colTouch = i;
            else if (name == "spawn") colSpawn = i;
            else if (name == "chan") colChan = i;
            else if (name == "axis") colAxis = i;
            else if (name == "exstat") colExStat = i;
            else if (name == "rev") colRev = i;
            else if (name == "nocol") colNoCol = i;
            else if (name == "multiact") colMultiAct = i;
            else if (name == "gmod") colGMod = i;
            else if (name == "gpl") colGPl = i;
            else if (name == "dontslide") colDontSlide = i;
            else if (name == "tpfm") colTpFm = i;
            else if (name == "tpf") colTpF = i;
            else if (name == "tpang") colTpAng = i;
            else if (name == "tprmod") colTpRMod = i;
            else if (name == "tprmin") colTpRMin = i;
            else if (name == "tprmax") colTpRMax = i;
            else if (name == "optudg") colOptUdg = i;
            else if (name == "extcol") colExtCol = i;
        }
    }
    if (colOptUdg >= kObjFields) colOptUdg = -1;
    if (colExtCol >= kObjFields) colExtCol = -1;
    if (g_bigSection && colExtCol < 0)
        loadPrintf("objrects: --bigsection asked, but the dump predates the extcol column -- an object with "
                   "Extended Collision cannot be told from one GD files in a section cell (re-dump to read it)\n");
    if (g_dualUnlink && colOptUdg < 0)
        loadPrintf("objrects: --dualunlink asked, but the dump predates the optudg column -- no "
                   "Options trigger is read as unlinking the dual's gravity (re-dump to read it)\n");
    const bool tpForceCols = colTpFm >= 0 && colTpFm < kObjFields && colTpF >= 0
                             && colTpF < kObjFields && colTpAng >= 0 && colTpAng < kObjFields
                             && colTpRMod >= 0 && colTpRMod < kObjFields && colTpRMin >= 0
                             && colTpRMin < kObjFields && colTpRMax >= 0 && colTpRMax < kObjFields;
    // --tppush: a teleport's velocity, from the tp* force columns of its row (modifiers.hpp TpForce)
    auto tpForceOf = [&](const std::string* f) {
        TpForce t;
        if (!g_tpPush || !tpForceCols) return t;
        auto num = [&](int c) { return f[c].empty() ? 0.f : (float)std::atof(f[c].c_str()); };
        t.fm = (uint8_t)(f[colTpFm].empty() ? 0 : std::atoi(f[colTpFm].c_str()));
        t.f = num(colTpF);
        t.ang = num(colTpAng);
        t.rmod = num(colTpRMod);
        t.rmin = num(colTpRMin);
        t.rmax = num(colTpRMax);
        return t;
    };
    g_tpForceCol = tpForceCols;
    std::vector<int> tpActUids;
    bool tpLevelTurns = false, tpLevelDual = false;
    if (g_tpPush && !tpForceCols)
        loadPrintf("objrects: --tppush asked, but the dump predates the tpfm columns -- no "
                   "teleport's force is read (re-dump to read them)\n");
    g_formulaDriven = 0;
    g_recLeadsN = 0;
    g_freeModeCol = (colFree >= 0);
    g_trigGateCol = (colTouch >= 0 && colSpawn >= 0);
    g_staticCamCol = (colAxis >= 0 && colExStat >= 0);
    if (!g_freeModeCol || !g_trigGateCol || !g_staticCamCol)
        loadPrintf("objrects: dump predates a column this build reads --"
                   "%s%s%s (re-dump to enable them)\n",
                   g_freeModeCol ? "" : " free",
                   g_trigGateCol ? "" : " touch/spawn",
                   g_staticCamCol ? "" : " axis/exstat");
    if (g_multiOrb && colMultiAct < 0)
        loadPrintf("objrects: --multiorb asked, but the dump predates the multiact column -- "
                   "no ring is read as multi-activate (re-dump to enable it)\n");
    if (g_rotBoost && colDontSlide < 0)
        loadPrintf("objrects: --rotboost asked, but the dump predates the dontslide column -- "
                   "every 2900 is read as Dont Slide off (re-dump to read it)\n");
    struct TrigOf {
        TouchMask mask{}; float dx = 0, dy = 0; double dur = 0;
        int ease = 0; double erate = 2.0;
        int aAnchor = -1; float adx = 0, ady = 0; double adur = 0;
        int aease = 0; double aerate = 2.0;
        double alock = 0, alockY = 0; int alockTrig = -1; int alockN = 0;
        double tlock = 0; int tlockBox = -1; int tlockN = 0;
        float andx = 0, andy = 0; double andur = 0;
        int aAnchorMove = -1;   // --anchormove: the earliest-crossed controller that moves it
        double aMinCx = 1e18;   // --recleads: the smallest cx over its autonomous controllers
        int anease = 0; double anerate = 2.0;
        int tlag = -1;
        float tldx = 0, tldy = 0; double tldur = 0;
        int tlease = 0; double tlerate = 2.0;
        std::vector<Dynamics::AutoPart> parts;   // one per moving controller
        std::vector<Dynamics::AutoPart> tparts;  // touch, one per BOX (trig=bit)
    };
    std::unordered_map<int, TrigOf> trigOf;
    std::vector<long long> effReach(tt ? tt->size() : 0, 0);
    if (tt) {
        for (size_t b = 0; b < tt->size(); ++b)
            for (const TrigCtl& c : (*tt)[b].ctl) {
                ++effReach[b];
                TrigOf& e = trigOf[c.uid];
                if (c.lockTicks > 0.0) {
                    ++e.tlockN;
                    if (e.tlockBox < 0) { e.tlockBox = (int)b; e.tlock = c.lockTicks; }
                }
                e.mask |= touchBit((int)b);
                if (std::hypot((double)c.dx, (double)c.dy) > 0.5
                    && c.durTicks > 0.0) {
                    const int lg = recordLag(c.dx, c.dy, c.durTicks, c.ease,
                                             c.erate);
                    if (e.tlag < 0 || lg < e.tlag) {
                        e.tlag = lg;
                        e.tldx = c.dx; e.tldy = c.dy; e.tldur = c.durTicks;
                        e.tlease = c.ease; e.tlerate = c.erate;
                    }
                }
                e.dx += c.dx;
                e.dy += c.dy;
                if (c.durTicks >= e.dur) { e.ease = c.ease; e.erate = c.erate; }
                e.dur = std::max(e.dur, c.durTicks);
                if (c.tmode) {
                    Dynamics::AutoPart tp{(int)b, 0.f, 0.f, c.durTicks, c.ease,
                                          c.erate};
                    tp.mover = c.mover;
                    tp.tmode = c.tmode;
                    tp.tdx = c.tdx;
                    tp.tdy = c.tdy;
                    tp.lag = c.spawnLag;   // --spawnlag
                    e.tparts.push_back(tp);
                } else if (std::fabs(c.dx) > 0.001f || std::fabs(c.dy) > 0.001f) {
                    Dynamics::AutoPart* found = nullptr;
                    for (auto& p : e.tparts)
                        if (p.trig == (int)b && !p.tmode
                            && p.mover == c.mover) {
                            found = &p; break;
                        }
                    if (found) {
                        found->dx += c.dx;
                        found->dy += c.dy;
                        if (c.durTicks >= found->dur) {
                            found->ease = c.ease; found->erate = c.erate;
                        }
                        found->dur = std::max(found->dur, c.durTicks);
                        found->lag = std::min(found->lag, c.spawnLag);
                    } else {
                        e.tparts.push_back({(int)b, c.dx, c.dy, c.durTicks,
                                            c.ease, c.erate});
                        e.tparts.back().mover = c.mover;
                        e.tparts.back().lag = c.spawnLag;
                    }
                }
            }
    }
    if (g_trigEffect && tt) {
        long long nBoxes = 0, nMoveNoop = 0, nNoopLive = 0;
        for (size_t b = 0; b < tt->size(); ++b) {
            long long uids = 0, moved = 0, tmodeUids = 0, recGate = 0;
            double maxNet = 0.0, maxDur = 0.0;
            for (const auto& kv : trigOf) {
                const TrigOf& e = kv.second;
                if (!(e.mask & touchBit((int)b))) continue;
                ++uids;
                bool tm = false;
                for (const auto& p : e.tparts) {
                    if (p.trig != (int)b) continue;
                    if (p.tmode) tm = true;
                    const double n = std::max(std::fabs((double)p.dx),
                                              std::fabs((double)p.dy));
                    if (n > 0.001) { ++moved; maxNet = std::max(maxNet, n); }
                    maxDur = std::max(maxDur, (double)p.dur);
                }
                if (tm) ++tmodeUids;
                const bool autoMoves = e.adx != 0.f || e.ady != 0.f;
                const bool noopTouch = e.dx == 0.f && e.dy == 0.f && e.dur == 0.0
                                       && e.aAnchor >= 0 && autoMoves;
                const auto rit = gt ? gt->find(kv.first) : GroupTimeline::const_iterator();
                const bool recChanges = gt && rit != gt->end() && rit->second.size() > 1;
                if (!noopTouch && recChanges) ++recGate;
            }
            std::unordered_set<int> lockSet;
            double maxLock = 0.0;
            for (const TrigCtl& c : (*tt)[b].ctl)
                if (c.lockTicks > 0.0) {
                    lockSet.insert(c.uid);
                    maxLock = std::max(maxLock, c.lockTicks);
                }
            const long long lockUids = (long long)lockSet.size();
            const int togOn = (*tt)[b].togOn;
            const size_t stops = (*tt)[b].stops.size();
            const bool moveNoop = uids && !moved && !tmodeUids;
            const bool live = moved || tmodeUids || lockUids || togOn >= 0
                              || stops || recGate;
            ++nBoxes;
            if (moveNoop) { ++nMoveNoop; if (live) ++nNoopLive; }
            loadPrintf("trigeffect: box %zu uid %d reaches=%lld uids=%lld "
                       "movedUids=%lld maxNet=%.3f maxDur=%.1f tmodeUids=%lld "
                       "lockUids=%lld maxLock=%.1f togOn=%d stops=%zu "
                       "recGate=%lld live=%d%s\n",
                       b, (*tt)[b].uid, effReach[b], uids, moved, maxNet,
                       maxDur, tmodeUids, lockUids, maxLock, togOn, stops,
                       recGate, live ? 1 : 0, moveNoop ? "  MOVE-NOOP" : "");
        }
        loadPrintf("trigeffect: %lld boxes, MOVE-NOOP %lld, of which live %lld\n",
                   nBoxes, nMoveNoop, nNoopLive);
        std::fflush(stdout);
    }
    if (at) {
        for (size_t b = 0; b < at->size(); ++b)
            for (const TrigCtl& c : (*at)[b].ctl) {
                TrigOf& e = trigOf[c.uid];
                if (g_dynDbg == c.uid)
                    loadPrintf("autoctl: uid=%d trig=%zu cx=%.1f root=%d dx=%.2f dy=%.2f dur=%.1f "
                               "lock=%.1f lockY=%.1f spawnLag=%d\n", c.uid, b, (*at)[b].cx,
                               (*at)[b].uid, (double)c.dx, (double)c.dy, c.durTicks, c.lockTicks,
                               c.lockYTicks, c.spawnLag);
                // anchor on the earliest-crossed controller (see Dynamics)
                if (e.aAnchor < 0 || (*at)[b].cx < (*at)[(size_t)e.aAnchor].cx)
                    e.aAnchor = (int)b;
                e.aMinCx = std::min(e.aMinCx, (*at)[b].cx);
                if ((std::fabs(c.dx) > 0.001f || std::fabs(c.dy) > 0.001f || c.tmode != 0
                     || c.lockTicks > 0.0 || c.lockYTicks > 0.0 || c.orbit || (*at)[b].toggleRoot)
                    && (e.aAnchorMove < 0 || (*at)[b].cx < (*at)[(size_t)e.aAnchorMove].cx))
                    e.aAnchorMove = (int)b;
                const float cdx = (g_lockNoDx && c.lockTicks > 0.0) ? 0.f : c.dx;
                e.adx += cdx;
                e.ady += c.dy;
                if (c.durTicks >= e.adur) { e.aease = c.ease; e.aerate = c.erate; }
                e.adur = std::max(e.adur, c.durTicks);
                e.alockY = std::max(e.alockY, c.lockYTicks);
                if (c.lockTicks > 0.0) {
                    ++e.alockN;              // two locks on one object: unmodelled
                    if (e.alockTrig < 0) { e.alockTrig = (int)b; e.alock = c.lockTicks; }
                }
                if (std::fabs(cdx) > 0.001f || std::fabs(c.dy) > 0.001f)
                    e.parts.push_back({(int)b, cdx, c.dy, c.durTicks,
                                       c.ease, c.erate});
            }
        for (auto& kv : trigOf) {
            TrigOf& e = kv.second;
            if (e.aAnchor < 0 || e.aAnchorMove < 0 || e.aAnchorMove == e.aAnchor) continue;
            if (g_shiftStat)
                loadPrintf("anchormove: uid=%d anchor=%d (x %.1f) moving=%d (x %.1f) applied=%d\n", kv.first,
                           e.aAnchor, (*at)[(size_t)e.aAnchor].cx, e.aAnchorMove,
                           (*at)[(size_t)e.aAnchorMove].cx, g_anchorMove ? 1 : 0);
            if (g_anchorMove) e.aAnchor = e.aAnchorMove;
        }
        for (auto& kv : trigOf) {
            TrigOf& e = kv.second;
            if (e.aAnchor < 0) continue;
            for (const TrigCtl& c : (*at)[(size_t)e.aAnchor].ctl) {
                if (c.uid != kv.first) continue;
                e.andx += c.dx;
                e.andy += c.dy;
                if (c.durTicks >= e.andur) {
                    e.anease = c.ease; e.anerate = c.erate;
                }
                e.andur = std::max(e.andur, c.durTicks);
            }
        }
    }
    auto rideWindow = [&](const Obj& ob, const std::vector<ModRow>& live, bool& rides,
                          int& t0r, int& t1r, const char* kind) {
        rides = false;
        t0r = t1r = -1;
        const auto tit = (ob.uid >= 0) ? trigOf.find(ob.uid) : trigOf.end();
        if (tit == trigOf.end() || tit->second.alockTrig < 0) return;
        const TrigOf& e = tit->second;
        if (e.alock <= 0.0 || e.alockY <= 0.0) {
            loadPrintf("ridebox: %s uid %d is locked to the player on one axis only "
                       "(x %.1f, y %.1f ticks) - read as before\n",
                       kind, ob.uid, e.alock, e.alockY);
            return;
        }
        rides = true;
        float px = (float)ob.cx, py = (float)ob.cy;
        for (const ModRow& r : live) {
            const bool moved = std::fabs(r.cx - px) > 1e-3f || std::fabs(r.cy - py) > 1e-3f;
            if (moved && r.t > 1) {
                if (t0r < 0) t0r = r.t;
                t1r = r.t;
            }
            px = r.cx;
            py = r.cy;
        }
        const bool open = t1r >= 0 && g_groupsEndT >= 0 && t1r >= g_groupsEndT - 1;
        if (open) t1r = std::numeric_limits<int>::max();
        if (t0r < 0)
            loadPrintf("ridebox: %s uid %d rides the player (lock %.1f ticks) - the "
                       "recording has no ride, read at its load-time place\n",
                       kind, ob.uid, e.alock);
        else
            loadPrintf("ridebox: %s uid %d rides the player (lock %.1f ticks) - window "
                       "t=%d..%s from the recording (end %lld)\n",
                       kind, ob.uid, e.alock, t0r,
                       open ? "open" : std::to_string(t1r).c_str(), g_groupsEndT);
    };
    int lockXN = 0;   // --movelockx: objects whose x offsets it dropped
    auto emit = [&](uint8_t bucket, const Obj& o) {
        const auto tit = (o.uid >= 0) ? trigOf.find(o.uid) : trigOf.end();
        if (g_moveLockX && !g_platformerMode && tit != trigOf.end() && objLocksX(o.id)) {
            TrigOf& e = tit->second;
            bool had = e.dx != 0.f || e.adx != 0.f || e.alock > 0.0 || e.tlock > 0.0;
            e.dx = e.adx = e.andx = e.tldx = 0.f;
            e.alock = e.tlock = 0.0;
            e.alockTrig = e.tlockBox = -1;
            auto strip = [&had](std::vector<Dynamics::AutoPart>& v) {
                for (auto& p : v) {
                    if (p.tmode == 1 || p.tmode == 2) had = true;
                    p.dx = 0.f;
                    p.tdx = 0.f;
                    if (p.tmode == 1) p.tmode = 3;
                }
                v.erase(std::remove_if(v.begin(), v.end(),
                                       [](const Dynamics::AutoPart& p) {
                                           return p.tmode == 2 || (!p.tmode && p.dy == 0.f);
                                       }),
                        v.end());
            };
            strip(e.parts);
            strip(e.tparts);
            if (had) {
                ++lockXN;
                if (g_dynDbg == o.uid)
                    loadPrintf("movelockx: uid=%d id=%d moves only in y\n", o.uid, o.id);
            }
        }
        const bool autoMoves = (tit != trigOf.end())
                               && (tit->second.adx != 0.f || tit->second.ady != 0.f);
        const bool noopTouch = (tit != trigOf.end()) && tit->second.mask.any()
                               && tit->second.dx == 0.f && tit->second.dy == 0.f
                               && tit->second.dur == 0.0
                               && tit->second.aAnchor >= 0 && autoMoves;
        const bool controlled = (tit != trigOf.end()) && tit->second.mask.any()
                                && !noopTouch;
        bool recLeads = false;
        if (g_recLeads && tit != trigOf.end() && !controlled && tit->second.aAnchor >= 0
            && !g_xTrack.empty() && gt && o.uid >= 0) {
            const auto ri = gt->find(o.uid);
            if (ri != gt->end() && ri->second.size() > 1) {
                const std::vector<DynSample>& sm = ri->second;
                int first = -1;
                for (const DynSample& s : sm)
                    if (std::fabs(s.cx - sm[0].cx) > 0.5f || std::fabs(s.cy - sm[0].cy) > 0.5f) {
                        first = s.t;
                        break;
                    }
                if (first >= g_xTrack.front().first && first <= g_xTrack.back().first) {
                    double xAt = g_xTrack.front().second;
                    for (const auto& r : g_xTrack) {
                        if (r.first > first) break;
                        xAt = r.second;
                    }
                    recLeads = xAt < tit->second.aMinCx;
                    if (recLeads) {
                        ++g_recLeadsN;
                        if (g_dynDbg == o.uid)
                            loadPrintf("recleads: uid=%d first change t=%d at x=%.1f, before the "
                                       "nearest controller x=%.1f\n", o.uid, first, xAt,
                                       tit->second.aMinCx);
                    }
                }
            }
        }
        const bool autoCtl = (tit != trigOf.end()) && !controlled
                             && tit->second.aAnchor >= 0 && !recLeads;
        const bool touchMoves = (tit != trigOf.end())
                                && (tit->second.dx != 0.f || tit->second.dy != 0.f);
        const bool formulaDriven = controlled && autoMoves && touchMoves
                                   && tit->second.aAnchor >= 0;
        if (formulaDriven) ++g_formulaDriven;
        if (g_dynDbg >= 0 && tit != trigOf.end())
            loadPrintf("ctl: uid=%d mask=0x%llx dx=%.2f dy=%.2f dur=%.3f "
                       "aAnchor=%d adx=%.2f ady=%.2f adur=%.3f "
                       "autoMoves=%d noopTouch=%d controlled=%d autoCtl=%d\n",
                       o.uid, (unsigned long long)tit->second.mask.word(0),
                       tit->second.dx, tit->second.dy,
                       tit->second.dur, tit->second.aAnchor, tit->second.adx,
                       tit->second.ady, tit->second.adur, autoMoves ? 1 : 0,
                       noopTouch ? 1 : 0, controlled ? 1 : 0, autoCtl ? 1 : 0);
        const auto it = (gt && o.uid >= 0) ? gt->find(o.uid) : GroupTimeline::const_iterator();
        const bool timed = gt && o.uid >= 0 && it != gt->end() && !it->second.empty();
        bool rotComputable = false;
        if (g_rotCompute && o.uid >= 0) {
            const auto rs = g_rotSpec.find(o.uid);
            if (rs != g_rotSpec.end()) {
                ++g_rotSeen;
                if (gt) {
                    const auto ct = gt->find(rs->second.centreUid);
                    rotComputable = (ct != gt->end() && !ct->second.empty());
                }
                if (rotComputable) ++g_rotRouted;
            }
        }
        if (!controlled && !autoCtl && !timed && !rotComputable) {
            switch (bucket) {
                case Dynamics::NEAR:  L.objs.push_back(o); break;
                case Dynamics::PORT:  L.portals.push_back(o); break;
                case Dynamics::PAD:   L.pads.push_back(o); break;
                case Dynamics::ORB:   L.orbs.push_back(o); break;
                case Dynamics::SPEED: L.speeds.push_back(o); break;
                case Dynamics::COIN:  break;
                default:              L.slopes.push_back(o); break;
            }
            return;
        }
        L.dyn.objs.push_back(o);
        L.dyn.objs.back().dynObj = 1;
        {
            std::vector<DynSample> sm =
                timed ? it->second
                      : std::vector<DynSample>{{0, (float)o.cx, (float)o.cy,
                                                (float)o.hw, (float)o.hh, 1}};
            for (size_t k = 1; k < sm.size(); ++k)
                if (sm[k].t - sm[k - 1].t > 30000) { sm.resize(k); break; }
            if (o.type == 2 && g_dynHazPad > 0.0) {
                for (DynSample& ds : sm) {
                    ds.hw += (float)g_dynHazPad;
                    ds.hh += (float)g_dynHazPad;
                }
                Obj& dob = L.dyn.objs.back();
                dob.bhw += g_dynHazPad;
                dob.bhh += g_dynHazPad;
                if (dob.radius > 0.0) dob.radius += g_dynHazPad;
            }
            L.dyn.samples.push_back(std::move(sm));
        }
        L.dyn.cur.push_back(0);
        L.dyn.bucket.push_back(bucket);
        L.dyn.baseCy.push_back((float)o.cy);
        L.dyn.baseSy0.push_back((float)o.sy0);
        L.dyn.baseSy1.push_back((float)o.sy1);
        L.dyn.on.push_back(1);
        L.dyn.prevCy.push_back((float)o.cy);
        L.dyn.trigMask.push_back(controlled ? tit->second.mask : TouchMask{});
        L.dyn.trigDx.push_back(controlled ? tit->second.dx : 0.f);
        L.dyn.trigDy.push_back(controlled ? tit->second.dy : 0.f);
        L.dyn.trigDur.push_back(controlled ? tit->second.dur : 0.0);
        L.dyn.trigEase.push_back(controlled ? tit->second.ease : 0);
        L.dyn.trigErate.push_back(controlled ? tit->second.erate : 2.0);
        bool boxParts = false;
        if (g_boxParts && controlled && !formulaDriven && !autoMoves) {
            int moving = 0, box = -1;
            bool oneBox = true;
            for (const auto& p : tit->second.tparts) {
                if (p.tmode || (p.dx == 0.f && p.dy == 0.f)) continue;
                ++moving;
                const int tb = p.trig & (kTouchBits - 1);
                if (box < 0) box = tb;
                else if (tb != box) oneBox = false;
            }
            boxParts = moving >= 2 && oneBox;
        }
        if (boxParts && g_dynDbg == o.uid)
            loadPrintf("boxparts: uid=%d takes the per-part formula (%zu parts)\n", o.uid,
                       tit->second.tparts.size());
        L.dyn.formula.push_back(formulaDriven ? 1 : boxParts ? 2 : 0);
        // --formulanolock: a formula-driven object its autonomous chain also locks to the player
        L.dyn.formulaLock.push_back((formulaDriven && (tit->second.alock > 0.0 || tit->second.alockY > 0.0)) ? 1 : 0);
        L.dyn.autoAnchor.push_back((autoCtl || formulaDriven) ? tit->second.aAnchor : -1);
        L.dyn.recSelfFire.push_back((controlled && !touchMoves && autoMoves
                                     && tit->second.aAnchor >= 0) ? 1u : 0u);
        L.dyn.autoReach.push_back(((tit != trigOf.end()
                                    && (tit->second.aAnchor >= 0 || autoMoves))
                                   || g_rotated.count(o.uid)) ? 1u : 0u);
        L.dyn.autoDx.push_back((autoCtl || formulaDriven) ? tit->second.adx : 0.f);
        L.dyn.autoDy.push_back((autoCtl || formulaDriven) ? tit->second.ady : 0.f);
        L.dyn.autoDur.push_back((autoCtl || formulaDriven) ? tit->second.adur : 0.0);
        L.dyn.autoEase.push_back((autoCtl || formulaDriven) ? tit->second.aease : 0);
        L.dyn.autoErate.push_back((autoCtl || formulaDriven) ? tit->second.aerate : 2.0);
        L.dyn.autoLock.push_back(autoCtl ? tit->second.alock : 0.0);
        L.dyn.autoLockTrig.push_back(autoCtl ? tit->second.alockTrig : -1);
        L.dyn.touchLock.push_back(tit != trigOf.end() ? tit->second.tlock : 0.0);
        if (tit != trigOf.end() && tit->second.tlockBox >= 0) {
            if (g_lockBox < 0) {
                g_lockBox = tit->second.tlockBox;
                g_lockTicks = tit->second.tlock;
            } else if (g_lockBox != tit->second.tlockBox) {
                loadPrintf("triggers: WARNING second touch lock box %d (using "
                           "%d) -- objects on it will not follow the player\n",
                           tit->second.tlockBox, g_lockBox);
            }
        }
        L.dyn.autoParts.push_back(autoCtl ? tit->second.parts
                                          : std::vector<Dynamics::AutoPart>{});
        // --moverextend: every Move the object's groups listen to, controlled or not (the tail rule matches it to the rows).
        L.dyn.partsAny.push_back(tit != trigOf.end() ? tit->second.parts : std::vector<Dynamics::AutoPart>{});
        L.dyn.touchParts.push_back(controlled ? tit->second.tparts
                                              : std::vector<Dynamics::AutoPart>{});
        L.dyn.rotSplit.push_back(0);   // set by the g_rotSplit stage below
        int recFire = -1;
        if (controlled || autoCtl) {
            const auto& sm = L.dyn.samples.back();
            for (size_t k = 1; k < sm.size(); ++k)
                if (std::fabs(sm[k].cx - sm[0].cx) > 0.05f
                    || std::fabs(sm[k].cy - sm[0].cy) > 0.05f
                    || std::fabs(sm[k].hw - sm[0].hw) > 0.05f
                    || std::fabs(sm[k].hh - sm[0].hh) > 0.05f
                    || sm[k].on != sm[0].on) { recFire = sm[k].t; break; }
        }
        L.dyn.trigRecFire.push_back(recFire);
        {
            int rx = -1;
            if (g_recAnchorX && autoCtl && recFire >= 0 && at && tit->second.aAnchor >= 0
                && (size_t)tit->second.aAnchor < at->size() && !g_xTrack.empty()) {
                const AutoTrig& A = (*at)[(size_t)tit->second.aAnchor];
                int cross = -1;
                for (const auto& r : g_xTrack)
                    if (r.second >= A.cx) { cross = r.first; break; }
                if (cross >= 0 && recFire < cross) {
                    rx = cross + A.delay;
                    ++g_recAnchorXN;
                }
            }
            L.dyn.recAnchorX.push_back(rx);
        }
        {
            uint8_t un = 0;
            if (autoCtl && recFire >= 0 && at && tit->second.aAnchor >= 0
                && (size_t)tit->second.aAnchor < at->size()
                && tit->second.andx == 0.f && tit->second.andy == 0.f
                && tit->second.andur == 0.0 && !L.dyn.samples.back().empty()) {
                for (const AutoTrig::Tog& g : (*at)[(size_t)tit->second.aAnchor].tog)
                    if (g.uid == o.uid) {
                        un = ((int)L.dyn.samples.back()[0].on == (int)g.on) ? 1 : 0;
                        break;
                    }
            }
            L.dyn.anchorUnrec.push_back(un);
        }
        {
            uint8_t ra = 0;
            if (controlled) {
                TouchMask upB{}, downB{};
                for (const auto& p : tit->second.tparts) {
                    const int tb = p.trig & (kTouchBits - 1);
                    if (p.dy > 0.01f) upB |= touchBit(tb);
                    if (p.dy < -0.01f) downB |= touchBit(tb);
                }
                ra = ((upB & ~downB) && (downB & ~upB)) ? 1 : 0;
            }
            L.dyn.recAuto.push_back(ra);
        }
        const bool touchLag = controlled && !autoCtl
            && tit != trigOf.end() && tit->second.tlag > 0;
        int lag = autoCtl
            ? recordLag(tit->second.andx, tit->second.andy, tit->second.andur,
                        tit->second.anease, tit->second.anerate)
            : (touchLag ? tit->second.tlag : 0);
        if (g_rotLag && autoCtl && lag == 0 && at && tit->second.aAnchor >= 0
            && (size_t)tit->second.aAnchor < at->size()) {
            const AutoTrig& A = (*at)[(size_t)tit->second.aAnchor];
            if (A.rotDurT > 0.0 && A.rotAngleOk && !A.rotLock) {
                int rl = rotRecordLag(A.rotTotal, A.rotDurT, A.rotEase, A.rotErate);
                const auto& sm = L.dyn.samples.back();
                if (rl > 1 && recFire >= 0 && !sm.empty()) {
                    double turned = -1.0;
                    for (const DynSample& q : sm)
                        if (q.t == recFire) {
                            turned = std::fmod(std::fabs((double)q.rot - (double)sm[0].rot), 360.0);
                            turned = std::min(turned, 360.0 - turned);   // 359.8 -> 0.4 is 0.6
                            break;
                        }
                    if (turned >= 0.0) {
                        int bestN = rl;
                        double bestErr = 1e18;
                        for (int n = 1; n <= rl; ++n) {
                            const double err = std::fabs(
                                std::fabs(A.rotTotal * gdEase(A.rotEase, A.rotErate, (double)n / A.rotDurT))
                                - turned);
                            if (err < bestErr) { bestErr = err; bestN = n; }
                        }
                        rl = bestN;
                    }
                }
                if (rl > 0) {
                    lag = rl;
                    ++g_rotLagN;
                }
            }
        }
        if ((autoCtl || touchLag) && lag > 1 && recFire >= 0) {
            const auto& sm = L.dyn.samples.back();
            const double full = autoCtl
                ? std::hypot((double)tit->second.andx, (double)tit->second.andy)
                : std::hypot((double)tit->second.tldx, (double)tit->second.tldy);
            const double dur = autoCtl ? tit->second.andur : tit->second.tldur;
            const int easeL = autoCtl ? tit->second.anease : tit->second.tlease;
            const double erateL =
                autoCtl ? tit->second.anerate : tit->second.tlerate;
            if (full > 0.5 && dur > 0.0) {
                double moved = 0.0;
                for (const DynSample& q : sm)
                    if (q.t == recFire) {
                        moved = std::hypot((double)q.cx - (double)sm[0].cx,
                                           (double)q.cy - (double)sm[0].cy);
                        break;
                    }
                int bestN = lag;
                double bestErr = 1e18;
                for (int n = 1; n <= lag; ++n) {
                    const double e = gdEase(easeL, erateL, (double)n / dur);
                    const double err = std::fabs(recLagCurve(full, e) - moved);   // --reclagabs
                    if (err < bestErr) { bestErr = err; bestN = n; }
                }
                lag = recLagUpN(bestN, lag, full, moved, dur, easeL, erateL);   // --reclagup
            }
        }
        L.dyn.recLag.push_back(lag);
        int aLag = lag;
        if (formulaDriven && recFire >= 0) {
            const TrigOf& e = tit->second;
            aLag = recordLag(e.andx, e.andy, e.andur, e.anease, e.anerate);
            const double full = std::hypot((double)e.andx, (double)e.andy);
            if (aLag > 1 && full > 0.5 && e.andur > 0.0) {
                const auto& sm = L.dyn.samples.back();
                double moved = 0.0;
                for (const DynSample& q : sm)
                    if (q.t == recFire) {
                        moved = std::hypot((double)q.cx - (double)sm[0].cx,
                                           (double)q.cy - (double)sm[0].cy);
                        break;
                    }
                int bestN = aLag;
                double bestErr = 1e18;
                for (int n = 1; n <= aLag; ++n) {
                    const double ev = gdEase(e.anease, e.anerate, (double)n / e.andur);
                    const double err = std::fabs(recLagCurve(full, ev) - moved);   // --reclagabs
                    if (err < bestErr) { bestErr = err; bestN = n; }
                }
                aLag = recLagUpN(bestN, aLag, full, moved, e.andur, e.anease,
                                 e.anerate);   // --reclagup
            }
            if (g_lagFitDbg && aLag != lag) {
                const auto& sm = L.dyn.samples.back();
                auto resid = [&](float fx, float fy, double dur, int ease, double rate,
                                 int start) {
                    double worst = 0.0;
                    int seen = 0;
                    for (size_t k = 1; k < sm.size() && seen < 3; ++k) {
                        if (sm[k].t < recFire) continue;
                        const double u = dur > 0.0
                            ? std::min(1.0, std::max(0.0, (double)(sm[k].t - start) / dur))
                            : 1.0;
                        const double ev = gdEase(ease, rate, u);
                        const double ex = (double)sm[k].cx - (double)sm[0].cx - fx * ev;
                        const double ey = (double)sm[k].cy - (double)sm[0].cy - fy * ev;
                        worst = std::max(worst, std::hypot(ex, ey));
                        ++seen;
                    }
                    return worst;
                };
                const double ra = resid(e.andx, e.andy, e.andur, e.anease, e.anerate,
                                        recFire - aLag);
                const double rt = resid(e.tldx, e.tldy, e.tldur, e.tlease, e.tlerate,
                                        recFire - lag);
                loadPrintf("lagfit: uid=%d autoAnchor=%d recFire=%d recLag=%d autoLag=%d "
                           "resid auto=%.4f touch=%.4f -> %s\n",
                           o.uid, e.aAnchor, recFire, lag, aLag, ra, rt,
                           ra < rt ? "auto" : "touch");
            }
        }
        L.dyn.autoLag.push_back(aLag);
        uint8_t closed = 0;
        if (autoCtl && tit->second.alockN == 1 && tit->second.alockY <= 0.0) {
            closed = 1;
        } else if (autoCtl && tit->second.alockN == 0
            && tit->second.alockY <= 0.0
            && !tit->second.parts.empty()) {
            closed = 1;
            if (recFire >= 0 && tit->second.parts.size() <= 1) {
                const auto& sm = L.dyn.samples.back();
                const double bx = sm[0].cx, by = sm[0].cy;
                const int t0r = recFire - lag;
                for (const DynSample& s : sm) {
                    const double e = gdEase(tit->second.aease, tit->second.aerate,
                                            (double)(s.t - t0r) / tit->second.adur);
                    if (std::fabs((double)s.cx - (bx + tit->second.adx * e)) > 0.5
                        || std::fabs((double)s.cy - (by + tit->second.ady * e)) > 0.5) {
                        closed = 0;
                        break;
                    }
                }
            }
        }
        if (closed && g_rotated.count(o.uid)) closed = 0;
        L.dyn.autoClosed.push_back(closed);
        if (controlled) L.dyn.anyTrig = true;
        if (autoCtl) L.dyn.anyAuto = true;
        if (timed)
            for (const DynSample& s : it->second) {
                L.maxX = std::max(L.maxX, (double)s.cx);
                if (bucket == Dynamics::NEAR)
                    L.maxY = std::max(L.maxY, (double)s.cy + (double)s.hh);
            }
        if (controlled && bucket == Dynamics::NEAR)
            L.maxY = std::max(L.maxY,
                              (double)o.cy + (double)tit->second.dy + (double)o.hh);
        if (autoCtl && bucket == Dynamics::NEAR)
            L.maxY = std::max(L.maxY,
                              (double)o.cy + (double)tit->second.ady + (double)o.hh);
    };
    while (std::getline(in, line)) {
        std::string f[kObjFields];
        splitCommaFields(line, f, kObjFields);
        if (f[5].empty()) continue;
        const int type = std::atoi(f[1].c_str());
        Obj o{std::atof(f[2].c_str()), std::atof(f[3].c_str()),
              std::atof(f[4].c_str()) / 2, std::atof(f[5].c_str()) / 2,
              (uint8_t)type, std::atoi(f[0].c_str()),
              f[7].empty() ? -1 : std::atoi(f[7].c_str()),
              f[8].empty() ? 0.0 : std::atof(f[8].c_str())};
        if (colExtCol >= 0 && f[colExtCol] == "1") o.extCol = 1;   // --bigsection (Obj::extCol)
        if (o.radius > 0.0) {
            const double w0 = f[15].empty() ? 0.0 : std::atof(f[15].c_str());
            const double h0 = f[16].empty() ? 0.0 : std::atof(f[16].c_str());
            const double rotHere = f[9].empty() ? 0.0 : std::atof(f[9].c_str());
            const long q = std::lround(rotHere / 90.0);
            const bool quarter = std::fabs(rotHere - 90.0 * (double)q) < 0.01;
            if (!quarter) {
                if (w0 > 1.0) o.radius *= (std::atof(f[4].c_str()) / w0);
            } else {
                const bool turned = (((q % 4) + 4) % 4) & 1;
                const double uw = turned ? h0 : w0;
                const double uh = turned ? w0 : h0;
                const double sx = uw > 1.0
                                      ? std::atof(f[4].c_str()) / uw : 1.0;
                const double sy = uh > 1.0
                                      ? std::atof(f[5].c_str()) / uh : 1.0;
                if (sx != 1.0 || sy != 1.0)
                    o.radius *= (sx > sy ? sx : sy);
            }
            o.hw = o.hh = o.radius;
        }
        o.rot = f[9].empty() ? 0.0 : std::atof(f[9].c_str());
        o.flipY = (uint8_t)(f[31] == "1" ? 1 : 0);
        o.flipX = (uint8_t)(f[30] == "1" ? 1 : 0);
        o.freeMode = (uint8_t)((colFree >= 0 && f[colFree] == "1") ? 1 : 0);
        o.rev = (uint8_t)((colRev >= 0 && colRev < kObjFields
                           && std::atoi(f[colRev].c_str()) == 1) ? 1 : 0);
        o.multiAct = (uint8_t)((colMultiAct >= 0 && colMultiAct < kObjFields
                                && std::atoi(f[colMultiAct].c_str()) == 1) ? 1 : 0);
        const double w0 = f[15].empty() ? 0.0 : std::atof(f[15].c_str());
        const double h0 = f[16].empty() ? 0.0 : std::atof(f[16].c_str());
        if (type == 28) {
            o.tpY = f[17].empty() ? 0.0 : std::atof(f[17].c_str());
            o.tpGrav = f[18].empty() ? 0 : (uint8_t)std::atoi(f[18].c_str());
            o.tpEx = f[34].empty() ? 0.0 : std::atof(f[34].c_str());
            o.tpEy = f[35].empty() ? 0.0 : std::atof(f[35].c_str());
            const bool igx = (!f[19].empty() && std::atoi(f[19].c_str()));
            const bool igy = (!f[20].empty() && std::atoi(f[20].c_str()));
            bool movesX = false;
            if (o.id != 747 && (o.tpEx != 0.0 || o.tpEy != 0.0)
                && !igx && std::fabs(o.tpEx - o.cx) > 0.5) {
                movesX = g_tpPortalX && o.tpEx > o.cx;
                if (movesX)
                    loadPrintf("teleport: uid %d at (%.0f,%.0f) moves x to %.0f\n", o.uid, o.cx, o.cy,
                               o.tpEx);
                else
                    loadPrintf("teleport: uid %d at (%.0f,%.0f) moves x to %.0f "
                               "- NOT modelled\n", o.uid, o.cx, o.cy, o.tpEx);
            }
            // --tpkeepy: the portal pass reads the keep (Obj::tpKeep)
            if (igy && g_tpKeepY) {
                o.tpKeep = (uint8_t)((igx ? 1 : 0) | 2);
                loadPrintf("teleport: uid %d at (%.0f,%.0f) keeps y\n", o.uid, o.cx, o.cy);
            } else if (igy) {
                loadPrintf("teleport: uid %d at (%.0f,%.0f) uses ignoreY "
                           "- NOT modelled\n", o.uid, o.cx, o.cy);
            }
            if (movesX) o.tpKeep = (uint8_t)(o.tpKeep | 4);
            o.tpForce = tpForceOf(f);
            if (o.tpForce.fm || (o.tpKeep & 2)) tpActUids.push_back(o.uid);
            if (o.tpForce.fm)
                loadPrintf("teleport: uid %d at (%.0f,%.0f) %s %.3g along %.3g deg%s\n", o.uid,
                           o.cx, o.cy, (o.tpForce.fm & 4) ? "redirects, x" : "pushes",
                           (o.tpForce.fm & 4) ? o.tpForce.rmod : o.tpForce.f, o.tpForce.ang,
                           (o.tpForce.fm & 4) ? "" : (o.tpForce.fm & 2) ? " (adds)" : "");
            if (o.id != 747 && o.tpEx == 0.0 && o.tpEy == 0.0)
                loadPrintf("teleport: uid %d id %d at (%.0f,%.0f) has NO exit "
                           "columns (old objrects dump) - target unreliable, "
                           "refresh objrects\n", o.uid, o.id, o.cx, o.cy);
            if (o.tpGrav)
                loadPrintf("teleport: uid %d at (%.0f,%.0f) sets gravity mode %d\n",
                           o.uid, o.cx, o.cy, (int)o.tpGrav);
        }
        if (type == 46 && g_teleportOrb) {
            o.tpGrav = f[18].empty() ? 0 : (uint8_t)std::atoi(f[18].c_str());
            o.tpEx = f[34].empty() ? 0.0 : std::atof(f[34].c_str());
            o.tpEy = f[35].empty() ? 0.0 : std::atof(f[35].c_str());
            o.tpKeep = (uint8_t)(((!f[19].empty() && std::atoi(f[19].c_str())) ? 1 : 0)
                                 | ((!f[20].empty() && std::atoi(f[20].c_str())) ? 2 : 0));
            // The orb's force is not measured (calib_teleportorb has none): it stays out.
            if (tpForceOf(f).fm)
                loadPrintf("teleportorb: uid %d at (%.0f,%.0f) sets a velocity - NOT modelled\n",
                           o.uid, o.cx, o.cy);
            loadPrintf("teleportorb: uid %d at (%.0f,%.0f) -> (%.1f, %.1f)%s%s grav %d%s\n", o.uid,
                       o.cx, o.cy, o.tpEx, o.tpEy, (o.tpKeep & 1) ? " keepX" : "",
                       (o.tpKeep & 2) ? " keepY" : "", (int)o.tpGrav,
                       (o.tpEx == 0.0 && o.tpEy == 0.0) ? " - NO target in the dump" : "");
        }
        if (o.id == 2900) tpLevelTurns = true;
        if (type == 23) tpLevelDual = true;
        if (o.id == 2899 && colOptUdg >= 0 && !f[colOptUdg].empty()) {
            const int udg = std::atoi(f[colOptUdg].c_str());
            if (udg != 0) {
                const bool queued = !(g_trigGateCol && (f[colTouch] == "1" || f[colSpawn] == "1"));
                if (queued) g_unlinkTrigs.push_back({o.cx, (int8_t)(udg == 1 ? 1 : -1)});
                if (g_dualUnlink)
                    loadPrintf("dualunlink: uid %d at x=%.0f %s the dual's gravity%s\n", o.uid, o.cx,
                               udg == 1 ? "unlinks" : "links",
                               queued ? "" : " - touch/spawn-triggered, IGNORED");
            }
        }
        if (g_ctrlTouch && o.id == 2899 && g_trigGateCol && f[colTouch] == "1" && !f[28].empty()) {
            const int p1 = std::atoi(f[28].c_str());
            if (p1 != 0) {
                g_ctrlTouchBoxes.push_back({o.cx, o.cy, o.hw, o.hh, o.uid, (uint8_t)(p1 == 1 ? 1 : 0)});
                loadPrintf("ctrltouch: uid %d at (%.0f,%.0f) %.1fx%.1f %s player 1's controls\n", o.uid,
                           o.cx, o.cy, o.hw * 2.0, o.hh * 2.0, p1 == 1 ? "disables" : "enables");
            }
        }
        if (o.radius == 0.0 && !o.slope && w0 > 1.0 && h0 > 1.0) {
            const double m = std::fabs(std::fmod(o.rot, 90.0));
            if (m > 0.5 && m < 89.5) {
                const double th = o.rot * 3.14159265358979 / 180.0;
                {
                    const double ccL = std::fabs(std::cos(th));
                    const double ssL = std::fabs(std::sin(th));
                    const double denomL = (w0 * 0.5) * ccL + (h0 * 0.5) * ssL;
                    double kSc = (denomL > 1.0) ? o.hw / denomL : 1.0;
                    if (std::fabs(kSc - 1.0) < 0.01) kSc = 1.0;
                    const double W = w0 * kSc, H = h0 * kSc;
                    if (W > 1.0 && H > 1.0) {
                        o.oriented = 1;
                        o.ohw = W * 0.5; o.ohh = H * 0.5;
                        const double thc = -th;
                        o.rc = std::cos(thc); o.rs = std::sin(thc);
                    }
                }
            }
        }
        L.maxX = std::max(L.maxX, o.cx);
        if (type == 21) { o.type = 0; o.oneway = 1; }
        if (type == 22 || type == 31) {
            L.coins.push_back(o);
            if (g_coinRoute) emit(Dynamics::COIN, o);
            continue;
        }
        if ((type == 2 || type == 47) && o.radius == 0.0 && o.uid >= 0) {
            const auto it = g_obb.find(o.uid);
            if (it != g_obb.end()) {
                o.obbOk = 1;
                o.bhw = it->second.hw; o.bhh = it->second.hh;
                o.bc = it->second.c;   o.bs = it->second.s;
            } else if (g_rotObb && g_rotated.count(o.uid)) {
                o.obbOk = 1;
                o.bhw = o.hw; o.bhh = o.hh;
                o.bc = 1.0;   o.bs = 0.0;
            }
        }
        // --ringpresseat (speed.hpp): every toggle ring, with its recorded rows; the flag decides at run time
        if (o.id == 1594) {
            PressRing pr{o.cx, o.cy, o.hw, o.hh, o.uid, o.multiAct, {}, {}, {}, {}};
            if (gt && o.uid >= 0) {
                const auto fit = gt->find(o.uid);
                if (fit != gt->end())
                    for (const auto& r : fit->second) {
                        pr.liveT.push_back(r.t);
                        pr.liveX.push_back(r.cx);
                        pr.liveY.push_back(r.cy);
                        pr.liveOn.push_back(r.on);
                    }
            }
            g_pressRings.push_back(std::move(pr));
        }
        if (o.type == 0 || type == 2 || type == 47) {
            L.maxY = std::max(L.maxY, o.cy + o.hh);
            bool noCollide =
                (colNoCol >= 0 && colNoCol < kObjFields && !f[colNoCol].empty())
                    ? std::atoi(f[colNoCol].c_str()) != 0
                    : o.id == kNoCollideId;
            if (noCollide && g_noColOneWay && o.type == 0 && gt && o.uid >= 0 && gt->find(o.uid) != gt->end())
                { o.oneway = 1; noCollide = false; }
            if (noCollide) continue;
            const auto tw = envTwin.empty() ? envTwin.end() : envTwin.find(o.uid);
            if (tw == envTwin.end()) {
                emit(Dynamics::NEAR, o);
            } else {
                gt = &envSplit;
                emit(Dynamics::NEAR, o);
                Obj h = o;
                h.type = 2;
                h.uid = tw->second;
                h.radius = 0.0;
                h.oneway = 0;
                h.obbOk = 0;
                h.oriented = 0;
                h.slope = 0;
                h.slopeHazard = 0;
                emit(Dynamics::NEAR, h);
                gt = gtAsRecorded;
                ++envTwins;
            }
        }
        else if (type == 3 || type == 4 || type == 5 || type == 6 || type == 16
                 || type == 17 || type == 18 || type == 19
                 || type == 26
                 || type == 27
        // 33 = SPIDER portal (lv21 x=10,695 and lv22 x=975 are the first two).
                 || type == 33
                 || type == 41
                 || type == 28
                 || (g_gravToggle && type == 42)
                 || type == 23 || type == 24) {
            if (o.freeMode && bandHeightFor(o.type) > 0.0)
                loadPrintf("freemode: portal uid %d id %d at (%.0f,%.0f) "
                           "carries Free Mode - band stays as it was "
                           "(would have written H=%.0f)\n",
                           o.uid, o.id, o.cx, o.cy, bandHeightFor(o.type));
            emit(Dynamics::PORT, o);   // 16 = ball
        }
        else if (type == 8 || type == 9 || type == 10 || type == 34)
            emit(Dynamics::PAD, o);
        else if (type == 44 && g_spiderPad)
            emit(Dynamics::PAD, o);
        else if (type == 11 || type == 12 || type == 13 || type == 29
                 || type == 32 || type == 35 || type == 37 || type == 38
                 || type == 43 || (g_teleportOrb && type == 46 && (o.tpEx != 0.0 || o.tpEy != 0.0))) {
            const double om = std::fabs(std::fmod(o.rot, 90.0));
            if (om > 0.5 && om < 89.5 && w0 > 1.0 && h0 > 1.0) {
                const double th = o.rot * 3.14159265358979 / 180.0;
                const double cc = std::fabs(std::cos(th));
                const double ssn = std::fabs(std::sin(th));
                const double denom = (w0 * 0.5) * cc + (h0 * 0.5) * ssn;
                if (denom > 1.0) {
                    const double k = o.hw / denom;
                    o.oriented = 1;
                    o.ohw = k * w0 * 0.5;
                    o.ohh = k * h0 * 0.5;
                    const double thc = -th;
                    o.rc = std::cos(thc);
                    o.rs = std::sin(thc);
                }
            }
            emit(Dynamics::ORB, o);  // 13 = gravity
        }
        else if (type == 20 && isSpeedId(o.id)) emit(Dynamics::SPEED, o);
        else if (type == 20 && o.id == 2900) {
            const int gd = f[27].empty() ? 0 : std::atoi(f[27].c_str());
            int fr = ((int)std::lround(o.rot / 90.0)) & 3;
            int rv = -1;   // -1 = column absent (old export); follow rot
            switch (gd) {
                case 4: fr = 0; rv = 0; break;
                case 3: fr = 0; rv = 1; break;
                case 2: fr = 1; rv = 0; break;
                case 1: fr = 3; rv = 0; break;
                default: break;
            }
            static const int kUpFor[4] = {1, 4, 2, 3};
            const int mv = f[26].empty() ? 0 : std::atoi(f[26].c_str());
            RotTrig rt{o.cx, o.cy, fr};
            rt.setRev = rv;
            if (mv >= 1 && mv <= 4) rt.setFlip = (mv == kUpFor[fr & 3]) ? 1 : 0;
            rt.uid = o.uid;
            const int ev = f[37].empty() ? 0 : std::atoi(f[37].c_str());
            if (ev)
                rt.vmodY = f[39].empty() ? 0.0f : (float)std::atof(f[39].c_str());
            rt.ovrVel = f[40].empty() ? 0 : (uint8_t)std::atoi(f[40].c_str());
            rt.dontSlide = (colDontSlide >= 0 && colDontSlide < kObjFields
                            && !f[colDontSlide].empty())
                               ? (uint8_t)std::atoi(f[colDontSlide].c_str()) : 0;
            rt.rawRot = o.rot;
            rt.flipX = (uint8_t)(f[30].empty() ? 0 : std::atoi(f[30].c_str()));
            rt.gndDir = gd;
            g_rotTrig.push_back(rt);
        }
        else if (type == 25 && !f[10].empty()) {
            o.slope = 1;
            o.sy0 = std::atof(f[10].c_str());
            o.sy1 = std::atof(f[11].c_str());
            o.slopeHazard = f[12].empty() ? 0 : (uint8_t)std::atoi(f[12].c_str());
            o.slopeDir = f[13].empty() ? 0 : (uint8_t)std::atoi(f[13].c_str());
            if (g_nanSlope && (!std::isfinite(o.sy0) || !std::isfinite(o.sy1) || !(o.hw > 0.0))) {
                ++g_nanSlopeDropped;
                continue;
            }
            emit(Dynamics::SLOPE, o);
        }
        else if (o.id == 1913) {
            const double zm = f[22].empty() ? 0.0 : std::atof(f[22].c_str());
            const char* dropped = nullptr;
            if (g_trigGateCol && (f[colTouch] == "1" || f[colSpawn] == "1"))
                dropped = "touch/spawn-triggered";
            else if (colChan >= 0 && !f[colChan].empty()
                     && std::atoi(f[colChan].c_str()) != 0)
                dropped = "on a rotate channel";
            if (dropped)
                loadPrintf("zoom: uid %d at x=%.0f is %s - never fires on an "
                           "x crossing, IGNORED\n", o.uid, o.cx, dropped);
            if (zm > 0.0 && !dropped) {
                g_zoomTrigs.push_back({o.cx, zm,
                                       (f[23].empty() ? 0.0
                                          : std::atof(f[23].c_str())) * 240.0,
                                       f[25].empty() ? 2.0
                                          : std::atof(f[25].c_str()),
                                       f[24].empty() ? 0
                                          : std::atoi(f[24].c_str())});
                loadPrintf("zoom: uid %d at x=%.0f -> %.6f over %.2fs\n",
                           o.uid, o.cx, zm,
                           f[23].empty() ? 0.0 : std::atof(f[23].c_str()));
            }
        }
        else if (o.id == 1914 && g_staticCamCol) {
            const int axis = f[colAxis].empty() ? 0
                                                : std::atoi(f[colAxis].c_str());
            const uint8_t ex = (uint8_t)(f[colExStat] == "1" ? 1 : 0);
            bool queued = !(g_trigGateCol
                            && (f[colTouch] == "1" || f[colSpawn] == "1"));
            if (ex && colChan >= 0 && !f[colChan].empty()
                && std::atoi(f[colChan].c_str()) != 0)
                queued = false;
            if (axis != 1 && queued) {
                g_staticCams.push_back({o.cx, ex});
                loadPrintf("staticcam: uid %d at x=%.0f axis=%d %s\n",
                           o.uid, o.cx, axis, ex ? "EXIT" : "on");
            }
        }
        // TIME WARP (id 1935): the `tw` column carries m_timeWarpTimeMod.
        else if (o.id == 1935) {
            const double tw = f[21].empty() ? 0.0 : std::atof(f[21].c_str());
            if (tw <= 0.0) {
                loadPrintf("timewarp: uid %d at x=%.0f has NO tw column - "
                           "objrects is older than the 2026-08-14 dump, "
                           "IGNORED\n", o.uid, o.cx);
            } else if (g_warpQueue && g_trigGateCol
                       && (f[colTouch] == "1" || f[colSpawn] == "1")) {
                // --warpqueue (modifiers.hpp)
                loadPrintf("timewarp: uid %d at x=%.0f is touch/spawn-triggered, not on "
                           "the crossing queue - NOT modelled\n", o.uid, o.cx);
            } else {
                g_timeWarps.push_back({o.cx, tw});
                loadPrintf("timewarp: uid %d at x=%.0f mod=%.4f\n",
                           o.uid, o.cx, tw);
            }
        }
        else if (o.id == 3022 && g_teleportTrig) {
            const double tx = f[34].empty() ? 0.0 : std::atof(f[34].c_str());
            const double ty = f[35].empty() ? 0.0 : std::atof(f[35].c_str());
            const uint8_t igx = (uint8_t)(!f[19].empty() && std::atoi(f[19].c_str()) ? 1 : 0);
            const uint8_t igy = (uint8_t)(!f[20].empty() && std::atoi(f[20].c_str()) ? 1 : 0);
            const uint8_t grav = (uint8_t)(f[18].empty() ? 0 : std::atoi(f[18].c_str()));
            const char* dropped = nullptr;
            if (g_trigGateCol && (f[colTouch] == "1" || f[colSpawn] == "1"))
                dropped = "touch/spawn-triggered, not on the crossing queue";
            else if (tx == 0.0 && ty == 0.0)
                dropped = "without a target in the dump (no tpex/tpey)";
            else if (!igx && std::fabs(tx - o.cx) > 0.5 && !(g_tpSideways && tx > o.cx))
                dropped = g_tpSideways ? "moving x backwards" : "moving x sideways";
            if (dropped) {
                loadPrintf("teleporttrig: uid %d at x=%.0f is %s - NOT modelled\n",
                           o.uid, o.cx, dropped);
            } else {
                g_tpTrigs.push_back({o.cx, tx, ty, igx, igy, grav, o.uid, tpForceOf(f)});
                loadPrintf("teleporttrig: uid %d at x=%.0f -> (%.1f, %.1f)%s%s grav %d\n",
                           o.uid, o.cx, tx, ty, igx ? " keepX" : "", igy ? " keepY" : "",
                           (int)grav);
            }
        }
        else if (o.id == 2066 && g_gravTrig) {
            const char* dropped = nullptr;
            const double gm = (colGMod >= 0 && !f[colGMod].empty())
                                  ? std::atof(f[colGMod].c_str()) : 0.0;
            const int gpl = (colGPl >= 0 && !f[colGPl].empty())
                                ? std::atoi(f[colGPl].c_str()) : 0;
            if (colGMod < 0)
                dropped = "in a dump without the gmod column";
            else if (g_trigGateCol && (f[colTouch] == "1" || f[colSpawn] == "1"))
                dropped = "touch/spawn-triggered, not on the crossing queue";
            else if (gpl & 2)
                dropped = "for player 2 only";
            if (dropped) {
                loadPrintf("gravtrig: uid %d at x=%.0f is %s - NOT modelled\n",
                           o.uid, o.cx, dropped);
            } else {
                g_gravTrigs.push_back({o.cx, gm, o.uid});
                loadPrintf("gravtrig: uid %d at x=%.0f gravity x%.3f%s\n", o.uid, o.cx, gm,
                           (gpl & 4) ? " (the player that crosses it)" : "");
            }
        }
        else if (o.id == 2866) {
            FlipHeadBox fh{o.cx, o.cy, o.hw, o.hh, o.uid, {}};
            if (gt && o.uid >= 0) {
                const auto fit = gt->find(o.uid);
                if (fit != gt->end()) {
                    fh.live.reserve(fit->second.size());
                    for (const auto& r : fit->second)
                        fh.live.push_back({r.t, r.cx, r.cy});
                }
            }
            const size_t nrows = fh.live.size();
            rideWindow(o, fh.live, fh.rides, fh.rideT0, fh.rideT1, "fliphead");
            g_flipHeadBoxes.push_back(std::move(fh));
            loadPrintf("fliphead: uid %d at (%.0f,%.0f) %.1fx%.1f "
                       "(%zu recorded rows)\n",
                       o.uid, o.cx, o.cy, o.hw * 2.0, o.hh * 2.0, nrows);
        }
        else if (o.id == 1829) {
            g_dashStopBoxes.push_back({o.cx, o.cy, o.hw, o.hh});
            loadPrintf("dashstop: uid %d at (%.0f,%.0f) %.1fx%.1f\n",
                       o.uid, o.cx, o.cy, o.hw * 2.0, o.hh * 2.0);
        }
        else if (o.id == 1859) {
            ArmBox ab{o.cx, o.cy, o.hw, o.hh, o.uid, {}};
            if (gt && o.uid >= 0) {
                const auto fit = gt->find(o.uid);
                if (fit != gt->end()) {
                    ab.live.reserve(fit->second.size());
                    for (const auto& r : fit->second)
                        ab.live.push_back({r.t, r.cx, r.cy});
                }
            }
            const size_t nrows = ab.live.size();
            rideWindow(o, ab.live, ab.rides, ab.rideT0, ab.rideT1, "ceilarm");
            g_armBoxes.push_back(std::move(ab));
            loadPrintf("ceilarm: uid %d at (%.0f,%.0f) %.1fx%.1f "
                       "(%zu recorded rows)\n",
                       o.uid, o.cx, o.cy, o.hw * 2.0, o.hh * 2.0, nrows);
        }
        else if (o.id == 1755) {
            SlideBox sb{o.cx, o.cy, o.hw, o.hh, o.uid, {}};
            if (gt && o.uid >= 0) {
                const auto fit = gt->find(o.uid);
                if (fit != gt->end()) {
                    sb.live.reserve(fit->second.size());
                    for (const auto& r : fit->second)
                        sb.live.push_back({r.t, r.cx, r.cy});
                }
            }
            loadPrintf("dartslide: uid %d at (%.0f,%.0f) %.1fx%.1f (%zu recorded rows)\n",
                       o.uid, o.cx, o.cy, o.hw * 2.0, o.hh * 2.0, sb.live.size());
            g_slideBoxes.push_back(std::move(sb));
        }
        else if (o.id == 3645) {
            double rm = std::fmod(std::fabs(o.rot), 360.0);
            if (rm > 180.0) rm = 360.0 - rm;
            if (std::fabs(rm - 90.0) < 45.0)
                loadPrintf("forcefield: uid %d at (%.0f,%.0f) rot %.0f is "
                           "SIDEWAYS - not measured, object IGNORED\n",
                           o.uid, o.cx, o.cy, o.rot);
            else {
                ForceField ff{o.cx, o.cy, o.radius, (rm >= 90.0) ? -1 : +1};
                ff.uid = o.uid;
                ff.force = f[41].empty() ? 0.0 : std::atof(f[41].c_str());   // --forcecircle
                ff.dirY = (o.flipY ? -1.0 : 1.0) * std::cos(o.rot * 3.14159265358979 / 180.0);
                if (gt && o.uid >= 0) {
                    const auto fit = gt->find(o.uid);
                    if (fit != gt->end()) {
                        for (const auto& r : fit->second) {
                            ff.liveT.push_back((int)r.t);
                            ff.liveX.push_back((float)r.cx);
                            ff.liveY.push_back((float)r.cy);
                            ff.liveOn.push_back(r.on);
                        }
                    }
                }
                g_forceFields.push_back(ff);
                loadPrintf("forcefield: uid %d at (%.0f,%.0f) R=%.1f push %s\n",
                           o.uid, o.cx, o.cy, o.radius,
                           (rm >= 90.0) ? "down" : "up (UNMEASURED mirror)");
            }
        }
        else if (o.id == 2069) {
            double force = 0.0;
            if (f[41].empty()) {
                force = kFF2069 * kForceGDiv / std::fabs(kCubeG);
                loadPrintf("forcebox: uid %d has NO force column (old objrects "
                           "dump) - refresh objrects\n", o.uid);
            } else {
                force = std::atof(f[41].c_str());
            }
            g_forceBoxes.push_back({o.cx, o.cy, o.hw, o.hh, force, o.uid, 0});
            {
                const double dirY = (o.flipY ? -1.0 : 1.0)
                                    * std::cos(o.rot * 3.14159265358979 / 180.0);
                g_forceBoxes.back().dirY = dirY;
                if (std::fabs(dirY) < 0.999)
                    loadPrintf("forcebox: uid %d is turned %.1f deg - only the y share %.3f "
                               "of its push is modelled\n", o.uid, o.rot, dirY);
                else if (dirY < 0.0)
                    loadPrintf("forcebox: uid %d points down (rot %.1f, flipY %d)\n",
                               o.uid, o.rot, (int)o.flipY);
            }
            loadPrintf("forcebox: uid %d at (%.0f,%.0f) %gx%g m_force=%.3f "
                       "(cube %.3f / robot %.3f / swing %.3f)\n",
                       o.uid, o.cx, o.cy, 2 * o.hw, 2 * o.hh, force,
                       qVy(force * forceUnitFor(0, 1.29825f).v),
                       qVy(force * forceUnitFor(5, 1.29825f).v),
                       qVy(force * forceUnitFor(7, 1.29825f).v));
        }
    }
    if (!envTwin.empty()) {
        envOther = (int)envTwin.size() - envTwins;
        loadPrintf("groups: %d objects placed by GD's random numbers -> hazard twins%s\n",
                   envTwins,
                   envOther ? " (and some that are not solids or hazards: rows kept as "
                               "recorded, NOT treated as deadly)" : "");
    }
    auto byX = [](const Obj& a, const Obj& b) { return a.cx < b.cx; };
    std::sort(L.objs.begin(), L.objs.end(), byX);
    std::sort(L.portals.begin(), L.portals.end(), byX);
    {
        int nGrav = 0, nGravAll = 0;
        for (Obj& p : L.portals)
            if (p.type == 3 || p.type == 4 || (g_gravToggle && p.type == 42)) {
                ++nGravAll;
                if (nGrav < kGravPortalBits) p.gpBit = (int8_t)nGrav++;
            }
        if (nGravAll > kGravPortalBits) {
            constexpr double kGpShareGap = 120.0;
            bool anyRev = false;
            for (const Obj& o : L.objs) anyRev = anyRev || o.rev;
            for (const Obj& o : L.portals) anyRev = anyRev || o.rev;
            for (const Obj& o : L.orbs) anyRev = anyRev || o.rev;
            for (const Obj& o : L.pads) anyRev = anyRev || o.rev;
            std::vector<Obj*> grav;
            for (Obj& p : L.portals)
                if (p.type == 3 || p.type == 4 || (g_gravToggle && p.type == 42)) grav.push_back(&p);
            bool ok = !anyRev;
            for (size_t j = kGravPortalBits; ok && j < grav.size(); ++j) {
                const Obj& a = *grav[j - kGravPortalBits];
                const Obj& b = *grav[j];
                const double lo = a.cx + a.hw, hi = b.cx - b.hw;
                if (hi - lo < kGpShareGap) ok = false;
            }
            if (ok) {
                for (size_t j = kGravPortalBits; j < grav.size(); ++j) {
                    const Obj& a = *grav[j - kGravPortalBits];
                    Obj& b = *grav[j];
                    b.gpBit = a.gpBit;
                    L.gpHandoff.push_back(GpHandoff{((a.cx + a.hw) + (b.cx - b.hw)) / 2.0,
                                                    (int)a.gpBit});
                }
            } else {
                L.unsupported = "gravity portals: " + std::to_string(nGravAll)
                                + " (limit " + std::to_string(kGravPortalBits)
                                + (anyRev ? ", reversal" : ", too close to share bits") + ")";
            }
        }
        if (nGravAll <= kGravPortalBits) {
            int nTp = 0, nTpAll = 0;
            const int first = nGrav;
            for (Obj& p : L.portals)
                if (p.type == 28) {
                    ++nTpAll;
                    if (nGrav < kGravPortalBits) { p.gpBit = (int8_t)nGrav++; ++nTp; }
                }
            for (size_t i = 0; i < L.dyn.objs.size(); ++i)
                if (L.dyn.bucket[i] == Dynamics::PORT && L.dyn.objs[i].type == 28) {
                    ++nTpAll;
                    if (nGrav < kGravPortalBits) { L.dyn.objs[i].gpBit = (int8_t)nGrav++; ++nTp; }
                }
            loadPrintf("tplatch: %d of %d teleport portals latched (bits %d..%d)\n", nTp, nTpAll,
                       first, nGrav - 1);
        }
        if (g_dynGpLatch && nGravAll <= kGravPortalBits) {
            for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                Obj& p = L.dyn.objs[i];
                if (L.dyn.bucket[i] == Dynamics::PORT
                    && (p.type == 3 || p.type == 4 || (g_gravToggle && p.type == 42))
                    && nGrav < kGravPortalBits) {
                    p.gpBit = (int8_t)nGrav++;
                    if (g_slopeDbg)
                        loadPrintf("gpbit bit=%d uid=%d type=%d cx=%.1f (dyn)\n",
                                   (int)p.gpBit, p.uid, (int)p.type, p.cx);
                }
            }
        }
        if (g_slopeDbg)
            for (const Obj& p : L.portals)
                if (p.gpBit >= 0)
                    loadPrintf("gpbit bit=%d uid=%d type=%d cx=%.1f\n",
                               (int)p.gpBit, p.uid, (int)p.type, p.cx);
    }
    std::sort(L.pads.begin(), L.pads.end(), byX);
    std::sort(L.orbs.begin(), L.orbs.end(), byX);
    std::sort(L.speeds.begin(), L.speeds.end(), byX);
    std::sort(L.slopes.begin(), L.slopes.end(), byX);
    std::sort(L.coins.begin(), L.coins.end(), [](const Obj& a, const Obj& b) {
        return a.cx < b.cx || (a.cx == b.cx && a.cy < b.cy);
    });
    L.coinDyn.assign(L.coins.size(), -1);
    for (size_t ci = 0; ci < L.coins.size(); ++ci)
        for (size_t di = 0; di < L.dyn.objs.size(); ++di)
            if (L.dyn.objs[di].uid == L.coins[ci].uid && L.coins[ci].uid >= 0) {
                L.coinDyn[ci] = (int)di;
                break;
            }
    std::sort(g_timeWarps.begin(), g_timeWarps.end(),
              [](const TimeWarp& a, const TimeWarp& b) { return a.cx < b.cx; });
    std::sort(g_tpTrigs.begin(), g_tpTrigs.end(),
              [](const TpTrig& a, const TpTrig& b) { return a.cx < b.cx; });
    if (!tpActUids.empty() && (tpLevelTurns || tpLevelDual))
        loadPrintf("teleport: %zu portal(s) push or keep y (uid %d first) in a level that %s%s%s - "
                   "NOT modelled in a turned frame, not measured for a dual's second body\n",
                   tpActUids.size(), tpActUids.front(), tpLevelTurns ? "turns its gameplay" : "",
                   (tpLevelTurns && tpLevelDual) ? " and " : "", tpLevelDual ? "goes dual" : "");
    if (!g_tpTrigs.empty() && (tpLevelTurns || tpLevelDual))
        loadPrintf("teleporttrig: %zu trigger(s) (uid %d first) in a level that %s%s%s - NOT "
                   "modelled in a dual, a turned frame or moving backwards\n",
                   g_tpTrigs.size(), g_tpTrigs.front().uid, tpLevelTurns ? "turns its gameplay" : "",
                   (tpLevelTurns && tpLevelDual) ? " and " : "", tpLevelDual ? "goes dual" : "");
    std::sort(g_gravTrigs.begin(), g_gravTrigs.end(),
              [](const GravTrig& a, const GravTrig& b) { return a.cx < b.cx; });
    std::sort(g_staticCams.begin(), g_staticCams.end(),
              [](const StaticCam& a, const StaticCam& b) { return a.cx < b.cx; });
    std::stable_sort(g_unlinkTrigs.begin(), g_unlinkTrigs.end(),
                     [](const UnlinkTrig& a, const UnlinkTrig& b) { return a.cx < b.cx; });
    std::sort(g_zoomTrigs.begin(), g_zoomTrigs.end(),
              [](const ZoomTrig& a, const ZoomTrig& b) { return a.cx < b.cx; });
    zoomCrossReset(g_zoomTrigs.size());   // --zoomchain: no crossing seen yet
    L.dyn.everRot.assign(L.dyn.size(), 0);
    for (size_t i = 0; i < L.dyn.size(); ++i) {
        const double placed = L.dyn.objs[i].rot;
        for (const DynSample& s : L.dyn.samples[i]) {
            double d = std::fmod(std::fabs((double)s.rot - placed), 360.0);
            if (d > 180.0) d = 360.0 - d;
            if (d > 0.001) { L.dyn.everRot[i] = 1; break; }
        }
        if (g_rotObb && L.dyn.objs[i].obbOk && g_rotated.count(L.dyn.objs[i].uid)
            && g_obb.find(L.dyn.objs[i].uid) == g_obb.end()) {
            L.dyn.everRot[i] = 1;
            if (g_rotObbLatch) {
                if (L.dyn.obbLatchT.size() != L.dyn.size()) L.dyn.obbLatchT.assign(L.dyn.size(), -1);
                int first = std::numeric_limits<int>::max();
                for (const DynSample& s : L.dyn.samples[i]) {
                    double d = std::fmod(std::fabs((double)s.rot - placed), 360.0);
                    if (d > 180.0) d = 360.0 - d;
                    if (d > 0.001) { first = s.t; break; }
                }
                L.dyn.obbLatchT[i] = first;
                L.dyn.objs[i].obbOk = 0;   // the first seek sets it from the tick
            }
        }
    }
    {
        size_t n = 0;
        for (uint8_t v : L.dyn.everRot) n += (v != 0);
        if (n) loadPrintf("dynamics: %zu of %zu moving objects are rotated by "
                          "a trigger\n", n, L.dyn.size());
        size_t nl = 0, never = 0;
        for (const int v : L.dyn.obbLatchT) {
            nl += (v >= 0);
            never += (v == std::numeric_limits<int>::max());
        }
        if (nl) loadPrintf("rotobblatch: %zu hazard(s) take their turned box only once turned (%zu not turned in the "
                           "recording)\n", nl, never);
    }
    {
        std::unordered_map<int, size_t> idx;
        for (size_t i = 0; i < L.dyn.size(); ++i) idx[L.dyn.objs[i].uid] = i;
        size_t armed = 0;
        for (auto& kv : g_rotSpec) {
            RotSpec& R = kv.second;
            const auto io = idx.find(kv.first), ic = idx.find(R.centreUid);
            if (io == idx.end() || ic == idx.end()) continue;
            if (L.dyn.samples[ic->second].empty()) continue;
            for (size_t a = 0; a < g_autoTrig.size(); ++a)
                if (g_autoTrig[a].uid == R.trigUid) { R.anchor = (int)a; break; }
            if (R.anchor < 0) continue;          // touch-fired: stage 2
            R.centreIdx = (int)ic->second;
            R.relX = L.dyn.samples[io->second][0].cx
                   - L.dyn.samples[ic->second][0].cx;
            R.relY = L.dyn.samples[io->second][0].cy
                   - L.dyn.samples[ic->second][0].cy;
            ++armed;
        }
        if (g_rotCompute && !g_rotSpec.empty())
            loadPrintf("rotplace: %zu of %zu orbits armed (autonomous rotate "
                       "+ recorded centre); %zu spec'd objects reached the "
                       "router, %zu were routed into dyn\n",
                       armed, g_rotSpec.size(), g_rotSeen, g_rotRouted);
    }
    g_rotTailN = 0;
    if (g_rotTail && !g_rotTailParts.empty()) {
        std::unordered_map<int, size_t> idx;
        for (size_t i = 0; i < L.dyn.size(); ++i) idx[L.dyn.objs[i].uid] = i;
        L.dyn.rotTail.assign(L.dyn.size(), {});
        L.dyn.rotBoxW.assign(L.dyn.size(), 0.f);
        L.dyn.rotBoxH.assign(L.dyn.size(), 0.f);
        L.dyn.rotTailMaxT = -1;
        for (const auto& sm : L.dyn.samples)
            if (!sm.empty()) L.dyn.rotTailMaxT = std::max(L.dyn.rotTailMaxT, sm.back().t);
        for (size_t i = 0; i < L.dyn.size(); ++i) {
            const auto it = g_rotTailParts.find(L.dyn.objs[i].uid);
            if (it == g_rotTailParts.end()) continue;
            if (i < L.dyn.autoParts.size() && !L.dyn.autoParts[i].empty()) continue;
            const auto& sm = L.dyn.samples[i];
            if (sm.empty()) continue;
            std::vector<Dynamics::RotTail> parts;
            bool ok = true;
            for (const RotTailPart& p : it->second) {
                int a = -1;
                for (size_t k = 0; k < g_autoTrig.size(); ++k)
                    if (g_autoTrig[k].uid == p.trigUid) { a = (int)k; break; }
                double ccx = 0.0, ccy = 0.0;
                const auto ic = idx.find(p.centreUid);
                if (p.centreUid == 0) {
                    // turns about its own position: nothing to find
                } else if (ic != idx.end()) {
                    const auto& cs = L.dyn.samples[ic->second];
                    if (cs.size() > 1) { ok = false; break; }
                    ccx = cs.empty() ? (double)L.dyn.objs[ic->second].cx : (double)cs[0].cx;
                    ccy = cs.empty() ? (double)L.dyn.objs[ic->second].cy : (double)cs[0].cy;
                } else {
                    const auto rp = rotCentrePos.find(p.centreUid);
                    if (rp == rotCentrePos.end()) { ok = false; break; }
                    ccx = rp->second.first;
                    ccy = rp->second.second;
                }
                if (a < 0) { ok = false; break; }
                parts.push_back({a, ccx, ccy, p.total, p.durT, p.erate, p.ease, p.lock,
                             (uint8_t)(p.centreUid == 0 ? 1 : 0)});
            }
            if (!ok || parts.empty()) continue;
            double bestDet = 0.3;
            for (const DynSample* q : {&sm.front(), &sm.back()}) {
                const double r = (double)q->rot * (3.14159265358979 / 180.0);
                const double c = std::fabs(std::cos(r)), s = std::fabs(std::sin(r));
                const double det = c * c - s * s;
                if (std::fabs(det) <= bestDet) continue;
                const double W = 2.0 * q->hw, H = 2.0 * q->hh;
                const double w = (W * c - H * s) / det, h = (H * c - W * s) / det;
                if (w <= 0.0 || h <= 0.0) continue;
                bestDet = std::fabs(det);
                L.dyn.rotBoxW[i] = (float)w;
                L.dyn.rotBoxH[i] = (float)h;
            }
            L.dyn.rotTail[i] = std::move(parts);
            ++g_rotTailN;
        }
    }
    if (g_rotSplit && !g_rotSpec.empty()) {
        size_t done = 0, refusedFit = 0, refusedNoAuto = 0;
        double worstFit = 0.0;
        std::unordered_map<int, size_t> idx;
        for (size_t i = 0; i < L.dyn.size(); ++i) idx[L.dyn.objs[i].uid] = i;
        for (const auto& kv : g_rotSpec) {
            const RotSpec& R = kv.second;
            if (R.anchor < 0 || R.centreIdx < 0) continue;
            const auto io = idx.find(kv.first);
            if (io == idx.end()) continue;
            const size_t i = io->second, ci = (size_t)R.centreIdx;
            if (L.dyn.samples[i].size() < 2 || L.dyn.samples[ci].empty()) continue;
            // the translation has to have somewhere analytic to come from
            if (L.dyn.autoParts[i].empty()) { ++refusedNoAuto; continue; }
            const double ex = L.dyn.samples[ci][0].cx, ey = L.dyn.samples[ci][0].cy;
            const int lo = L.dyn.samples[i][0].t - (int)R.durT - 5;
            const int hi = L.dyn.samples[i].back().t + 5;
            int t0 = L.dyn.samples[i][0].t;
            {
                const std::vector<DynSample>& smp = L.dyn.samples[i];
                const std::vector<DynSample>& csm = L.dyn.samples[ci];
                const int dMin = smp.front().t - hi, dMax = smp.back().t - lo;
                std::vector<double> gxT((size_t)(dMax - dMin + 1)), gyT(gxT.size());
                for (int d = dMin; d <= dMax; ++d) {
                    const double th = -R.total
                        * gdEase(R.ease, R.erate, (double)d / R.durT)
                        * 3.14159265358979 / 180.0;
                    const double c = std::cos(th), sn = std::sin(th);
                    gxT[(size_t)(d - dMin)] = ex + c * R.relX - sn * R.relY;
                    gyT[(size_t)(d - dMin)] = ey + sn * R.relX + c * R.relY;
                }
                std::vector<size_t> cAt(smp.size());
                {
                    size_t ck = 0;
                    for (size_t k = 0; k < smp.size(); ++k) {
                        while (ck + 1 < csm.size() && csm[ck + 1].t <= smp[k].t) ++ck;
                        cAt[k] = ck;
                    }
                }
                double bestE = 1e18;
                for (int cand = lo; cand <= hi; ++cand) {
                    double w = 0.0;
                    for (size_t k = 0; k < smp.size(); ++k) {
                        const DynSample& s = smp[k];
                        const size_t di = (size_t)(s.t - cand - dMin);
                        const double gx = gxT[di], gy = gyT[di];
                        const size_t ck = cAt[k];
                        w = std::max(w, std::hypot(
                            (double)s.cx - (gx + csm[ck].cx - ex),
                            (double)s.cy - (gy + csm[ck].cy - ey)));
                        if (w >= bestE) break;
                    }
                    if (w < bestE) { bestE = w; t0 = cand; }
                }
            }
            // centre position by tick, held between recorded rows
            size_t cj = 0;
            auto centreAt = [&](int t) {
                while (cj + 1 < L.dyn.samples[ci].size()
                       && L.dyn.samples[ci][cj + 1].t <= t) ++cj;
                while (cj > 0 && L.dyn.samples[ci][cj].t > t) --cj;
                return std::pair<double, double>(L.dyn.samples[ci][cj].cx,
                                                 L.dyn.samples[ci][cj].cy);
            };
            std::vector<DynSample> gen = L.dyn.samples[i];
            double worst = 0.0;
            cj = 0;
            for (DynSample& s : gen) {
                const double th = -R.total
                    * gdEase(R.ease, R.erate, (double)(s.t - t0) / R.durT)
                    * 3.14159265358979 / 180.0;
                const double c = std::cos(th), sn = std::sin(th);
                const double ox = (double)R.relX, oy = (double)R.relY;
                const double gx = ex + c * ox - sn * oy;
                const double gy = ey + sn * ox + c * oy;
                const auto cp = centreAt(s.t);
                worst = std::max(worst,
                    std::hypot((double)s.cx - (gx + cp.first - ex),
                               (double)s.cy - (gy + cp.second - ey)));
                s.cx = (float)gx;
                s.cy = (float)gy;
            }
            if (worst > 0.1) { ++refusedFit; continue; }
            worstFit = std::max(worstFit, worst);
            L.dyn.samples[i] = std::move(gen);
            if (i < L.dyn.rotSplit.size()) L.dyn.rotSplit[i] = 1;
            ++done;
        }
        if (done || refusedFit || refusedNoAuto)
            loadPrintf("rotsplit: %zu objects moved onto a computed orbit "
                       "(worst fit %.4f px), %zu refused on fit, %zu with no "
                       "analytic translation\n",
                       done, worstFit, refusedFit, refusedNoAuto);
    }
    if (!g_rotSpec.empty()) {
        size_t done = 0, rows = 0, refusedFit = 0, refusedCentre = 0;
        std::unordered_map<int, size_t> idx;
        for (size_t i = 0; i < L.dyn.size(); ++i) idx[L.dyn.objs[i].uid] = i;
        const double kPi = 3.14159265358979;
        for (const auto& kv : g_rotSpec) {
            const RotSpec& R = kv.second;
            if (R.anchor < 0 || R.centreIdx < 0 || R.durT <= 0.0) continue;
            const auto io = idx.find(kv.first);
            if (io == idx.end()) continue;
            const size_t i = io->second, ci = (size_t)R.centreIdx;
            std::vector<DynSample>& sm = L.dyn.samples[i];
            if (sm.size() < 2 || L.dyn.samples[ci].empty()) continue;
            const double ex = L.dyn.samples[ci][0].cx, ey = L.dyn.samples[ci][0].cy;
            const bool split = i < L.dyn.rotSplit.size() && L.dyn.rotSplit[i];
            if (!split) {
                bool still = true;
                for (const DynSample& c : L.dyn.samples[ci])
                    if (std::fabs((double)c.cx - ex) >= kRecEps
                        || std::fabs((double)c.cy - ey) >= kRecEps) {
                        still = false;
                        break;
                    }
                if (!still) { ++refusedCentre; continue; }
            }
            auto orbit = [&](double tick, double t0, double& gx, double& gy) {
                const double th = -R.total * gdEase(R.ease, R.erate, (tick - t0) / R.durT)
                                  * kPi / 180.0;
                const double c = std::cos(th), sn = std::sin(th);
                gx = ex + c * R.relX - sn * R.relY;
                gy = ey + sn * R.relX + c * R.relY;
            };
            size_t k1 = 1;
            while (k1 < sm.size() && std::fabs((double)sm[k1].cx - (double)sm[0].cx) < kRecEps
                   && std::fabs((double)sm[k1].cy - (double)sm[0].cy) < kRecEps)
                ++k1;
            if (k1 == sm.size()) continue;
            const int fm = sm[k1].t;
            if (sm.back().t >= fm + (int)std::ceil(R.durT) + 5) continue;
            auto fitErr = [&](int cand, double bound) {
                double w = 0.0;
                for (const DynSample& s : sm) {
                    double gx, gy;
                    orbit((double)s.t, (double)cand, gx, gy);
                    w = std::max(w, std::hypot((double)s.cx - gx, (double)s.cy - gy));
                    if (w > bound) break;
                }
                return w;
            };
            const int lo = fm - (int)R.durT - 5, hi = fm + 1;
            const double seedE = fitErr(fm - 1, 1e18);
            int t0 = sm[0].t;
            double bestE = 1e18;
            for (int cand = lo; cand <= hi; ++cand) {
                const double w = fitErr(cand, std::min(bestE, seedE));
                if (w < bestE) { bestE = w; t0 = cand; }
            }
            if (bestE > 0.1) { ++refusedFit; continue; }
            const int tEnd = t0 + (int)std::ceil(R.durT);
            const DynSample last = sm.back();
            if (last.t >= tEnd) continue;
            double uhw = -1.0, uhh = -1.0;
            {
                const double r0 = std::fmod(std::fabs((double)sm[0].rot), 180.0);
                if (r0 < 0.001 || r0 > 179.999) { uhw = sm[0].hw; uhh = sm[0].hh; }
                else if (std::fabs(r0 - 90.0) < 0.001) { uhw = sm[0].hh; uhh = sm[0].hw; }
            }
            const double eLast = gdEase(R.ease, R.erate, ((double)last.t - t0) / R.durT);
            for (int t = last.t + 1; t <= tEnd; ++t) {
                DynSample s = last;
                s.t = t;
                double gx, gy;
                orbit((double)t, (double)t0, gx, gy);
                s.cx = (float)gx;
                s.cy = (float)gy;
                if (!R.lockrot) {
                    const double e = gdEase(R.ease, R.erate, ((double)t - t0) / R.durT);
                    s.rot = (float)((double)last.rot + R.total * (e - eLast));
                }
                if (uhw > 0.0 && uhh > 0.0) {
                    const double a = (double)s.rot * kPi / 180.0;
                    const double c = std::fabs(std::cos(a)), sn = std::fabs(std::sin(a));
                    s.hw = (float)(uhw * c + uhh * sn);
                    s.hh = (float)(uhw * sn + uhh * c);
                }
                sm.push_back(s);
                ++rows;
            }
            ++done;
        }
        loadPrintf("rotextend: %zu turned objects carried past their recordings (%zu rows), %zu "
                   "refused on fit, %zu whose centre moved\n",
                   done, rows, refusedFit, refusedCentre);
    }
    if (g_rotCheck && !g_rotSpec.empty()) {
        std::unordered_map<int, size_t> idx;
        for (size_t i = 0; i < L.dyn.size(); ++i) idx[L.dyn.objs[i].uid] = i;
        size_t checked = 0, noCentre = 0, noRec = 0, pass = 0;
        double worstAll = 0.0;
        int worstUid = 0;
        for (const auto& kv : g_rotSpec) {
            const auto io = idx.find(kv.first);
            const auto ic = idx.find(kv.second.centreUid);
            if (io == idx.end() || L.dyn.samples[io->second].size() < 2) {
                ++noRec;
                continue;
            }
            if (ic == idx.end() || L.dyn.samples[ic->second].empty()) {
                ++noCentre;
                continue;
            }
            const auto& so = L.dyn.samples[io->second];
            const auto& sc = L.dyn.samples[ic->second];
            auto centreAt = [&](int t) {
                const DynSample* best = &sc.front();
                for (const DynSample& s : sc) {
                    if (s.t > t) break;
                    best = &s;
                }
                return std::pair<double, double>((double)best->cx, (double)best->cy);
            };
            // first motion of the object = its fire tick, to within recordLag
            int t0 = so.front().t;
            for (size_t k = 1; k < so.size(); ++k)
                if (std::fabs((double)so[k].cx - (double)so[0].cx) > 1e-4
                    || std::fabs((double)so[k].cy - (double)so[0].cy) > 1e-4) {
                    t0 = so[k].t - 1;
                    break;
                }
            const RotSpec& R = kv.second;
            float x = so.front().cx, y = so.front().cy;
            double worst = 0.0;
            size_t si = 0;
            for (int t = t0 + 1; t <= so.back().t; ++t) {
                const auto cp = centreAt(t - 1), cn = centreAt(t);
                const double th0 = -R.total * gdEase(R.ease, R.erate,
                                                     (double)(t - 1 - t0) / R.durT);
                const double th1 = -R.total * gdEase(R.ease, R.erate,
                                                     (double)(t - t0) / R.durT);
                rotStep(x, y, cp.first, cp.second, cn.first, cn.second,
                        (th1 - th0) * 3.14159265358979 / 180.0);
                while (si < so.size() && so[si].t < t) ++si;
                if (si < so.size() && so[si].t == t) {
                    const double d = std::hypot((double)x - (double)so[si].cx,
                                                (double)y - (double)so[si].cy);
                    worst = std::max(worst, d);
                }
            }
            ++checked;
            if (worst <= 0.1) ++pass;
            if (worst > worstAll) { worstAll = worst; worstUid = kv.first; }
        }
        loadPrintf("rotcheck: %zu checked, %zu within 0.1px, worst %.4f px "
                   "(uid %d); %zu unrecorded, %zu centre not recorded\n",
                   checked, pass, worstAll, worstUid, noRec, noCentre);
    }
    if (g_formulaDriven) {
        int nLagDiff = 0, lagLo = 0, lagHi = 0;
        for (size_t i = 0; i < L.dyn.formula.size(); ++i) {
            if (!L.dyn.formula[i] || L.dyn.trigRecFire[i] < 0) continue;
            const int d = L.dyn.autoLag[i] - L.dyn.recLag[i];
            if (d == 0) continue;
            if (nLagDiff == 0 || d < lagLo) lagLo = d;
            if (nLagDiff == 0 || d > lagHi) lagHi = d;
            ++nLagDiff;
        }
        loadPrintf("dynamics: %d formula-driven (a touch AND an autonomous "
                   "controller both reach them and both move); %d recorded "
                   "with an autonomous lag other than the touch lag (%+d..%+d)\n",
                   g_formulaDriven, nLagDiff, lagLo, lagHi);
    }
    if (g_recLeadsN)
        loadPrintf("recleads: %d autonomous objects read as their recording (it moves before the "
                   "player reaches any controller the walk found)\n", g_recLeadsN);
    if (lockXN)
        loadPrintf("movelockx: %d objects a Move reaches in x are locked in x (speed portals and "
                   "triggers)\n", lockXN);
    if (!L.dyn.objs.empty()) {
        L.dyn.boxRecEntry.assign((size_t)kTouchBits, -1);
        std::vector<int> latest((size_t)kTouchBits, -1), n((size_t)kTouchBits, 0);
        std::vector<int> first((size_t)kTouchBits, -1);   // uid giving the entry
        for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
            if (L.dyn.trigRecFire[i] < 0 || L.dyn.autoAnchor[i] >= 0 || L.dyn.recAuto[i]
                || L.dyn.autoReach[i])
                continue;
            int box = -1, bits = 0;
            for (int b = 0; b < kTouchBits; ++b)
                if (L.dyn.trigMask[i].test(b)) { box = b; ++bits; }
            if (bits != 1) continue;
            const int e = L.dyn.trigRecFire[i] - kTouchRetimeLat
                          - std::max(0, L.dyn.recLag[i] - 1);
            int& cur = L.dyn.boxRecEntry[(size_t)box];
            if (cur < 0 || e < cur) { cur = e; first[(size_t)box] = L.dyn.objs[i].uid; }
            latest[(size_t)box] = std::max(latest[(size_t)box], e);
            ++n[(size_t)box];
        }
        // Named where it changes something: a box whose objects do not all start together.
        for (int b = 0; b < kTouchBits; ++b)
            if (n[(size_t)b] > 0 && latest[(size_t)b] > L.dyn.boxRecEntry[(size_t)b])
                loadPrintf("touchretimebox: box %d entry t=%d (uid %d), %d object(s), the "
                           "last starting %d ticks after\n", b,
                           L.dyn.boxRecEntry[(size_t)b], first[(size_t)b], n[(size_t)b],
                           latest[(size_t)b] - L.dyn.boxRecEntry[(size_t)b]);
    }
    if (tt && !L.dyn.objs.empty()) {
        std::unordered_set<int> switched;   // uids some activator switches
        std::unordered_map<int, std::vector<uint8_t>> offBy;   // --boxoff: uid -> off boxes
        for (size_t b = 0; b < tt->size() && b < (size_t)kTouchBits; ++b) {
            if (!(*tt)[b].activator) continue;
            for (const TouchTrig::TogEv& e : (*tt)[b].togEv) {
                if ((*tt)[b].offSwitch) offBy[e.uid].push_back((uint8_t)b);
                else switched.insert(e.uid);
            }
        }
        if (!offBy.empty()) {
            L.dyn.offBox.assign(L.dyn.objs.size(), {});
            size_t n = 0;
            for (size_t i = 0; i < L.dyn.objs.size(); ++i)
                if (const auto o = offBy.find(L.dyn.objs[i].uid); o != offBy.end()) {
                    L.dyn.offBox[i] = o->second;
                    ++n;
                }
            loadPrintf("boxoff: %zu object(s) keep their recording and go out when their box "
                       "fires\n", n);
        }
        if (!switched.empty()) {
            const auto boxEv = boxOnEvents(*tt);
            const auto autoEv = at ? autoOnEvents(*at)
                                   : std::unordered_map<int, std::vector<OnEvent>>{};
            L.dyn.onEv.assign(L.dyn.objs.size(), {});
            L.dyn.onInit.assign(L.dyn.objs.size(), 1);
            for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                const int u = L.dyn.objs[i].uid;
                if (!switched.count(u)) continue;
                const auto s0 = g_groupInit.find(u);
                if (s0 == g_groupInit.end()) {
                    loadPrintf("activators: uid %d stays on its recording - no reset snapshot "
                               "for it (a recording made before the snapshot existed)\n", u);
                    continue;
                }
                std::vector<OnEvent>& ev = L.dyn.onEv[i];
                if (const auto a = autoEv.find(u); a != autoEv.end())
                    ev.insert(ev.end(), a->second.begin(), a->second.end());
                if (const auto b = boxEv.find(u); b != boxEv.end())
                    ev.insert(ev.end(), b->second.begin(), b->second.end());
                L.dyn.onInit[i] = s0->second;
                loadPrintf("activators: uid %d gets %zu switch(es), on=%d at t=0 from the reset "
                           "snapshot\n", u, ev.size(), (int)L.dyn.onInit[i]);
                if (g_pressFresh && L.dyn.objs[i].type == 28)
                    for (const OnEvent& e : ev)
                        if (e.box >= 0 && e.on && e.delay == 0.0 && (size_t)e.box < tt->size()
                            && (*tt)[(size_t)e.box].press && !(*tt)[(size_t)e.box].offSwitch) {
                            L.dyn.objs[i].pendBox = (int16_t)e.box;
                            loadPrintf("pressfresh: teleport uid %d is live on the press that "
                                       "fires box %d\n", u, e.box);
                            break;
                        }
            }
        }
    }
    if (g_tapDisarm && at && !L.dyn.objs.empty() && g_autoDisarm.size() == at->size()) {
        std::unordered_set<int> hit;
        for (size_t k = 0; k < at->size(); ++k)
            if (!g_autoDisarm[k].empty())
                for (const AutoTrig::Tog& g : (*at)[k].tog) hit.insert(g.uid);
        if (!hit.empty()) {
            const auto autoEv = autoOnEvents(*at);
            // Every box's switches, the Taps' and Counts' too (boxOnEvents leaves those out), with their groups.
            std::unordered_map<int, std::vector<OnEvent>> boxEv;
            if (tt)
                for (size_t b = 0; b < tt->size() && b < (size_t)kTouchBits; ++b)
                    for (const TouchTrig::TogEv& e : (*tt)[b].togEv) {
                        OnEvent o;
                        o.box = (int)b;
                        o.on = e.on;
                        o.delay = e.delay;
                        o.grp = e.grp;
                        boxEv[e.uid].push_back(o);
                    }
            L.dyn.disarmEv.assign(L.dyn.objs.size(), {});
            L.dyn.disarmInit.assign(L.dyn.objs.size(), 1);
            size_t n = 0, noSnap = 0;
            for (size_t i = 0; i < L.dyn.objs.size(); ++i) {
                const int u = L.dyn.objs[i].uid;
                if (!hit.count(u)) continue;
                const auto s0 = g_groupInit.find(u);
                if (s0 == g_groupInit.end()) {   // no snapshot: stays on its recording (see --activators)
                    ++noSnap;
                    continue;
                }
                std::vector<OnEvent>& ev = L.dyn.disarmEv[i];
                if (const auto a = autoEv.find(u); a != autoEv.end())
                    ev.insert(ev.end(), a->second.begin(), a->second.end());
                if (const auto b = boxEv.find(u); b != boxEv.end())
                    ev.insert(ev.end(), b->second.begin(), b->second.end());
                L.dyn.disarmInit[i] = s0->second;
                ++n;
                if (g_dynDbg == u || g_dynDbg == -6) {
                    std::string s;
                    for (const OnEvent& o : ev) {
                        char b[96];
                        if (o.box >= 0)
                            std::snprintf(b, sizeof b, " box%d:g%d:%d+%.0f", o.box, o.grp, (int)o.on, o.delay);
                        else std::snprintf(b, sizeof b, " x%.0f:g%d:%d+%.0f", (*at)[(size_t)o.autoIdx].cx, o.grp,
                                           (int)o.on, o.delay);
                        s += b;
                    }
                    loadPrintf("tapdisarm: uid %d on=%d at the reset, switches%s\n", u, (int)L.dyn.disarmInit[i],
                               s.c_str());
                }
            }
            loadPrintf("tapdisarm: %zu object(s) take their switches where a box has switched one off (%zu "
                       "without a reset snapshot stay on their recording)\n", n, noSnap);
        }
    }
    g_recEndTailN = g_recEndTailTried = g_recEndTailOnN = 0;
    if (g_recEndTail && !L.dyn.objs.empty()) {
        Dynamics& D = L.dyn;
        const size_t N = D.size();
        D.rigTail.assign(N, {});
        D.tailOnEv.assign(N, {});
        D.tailOnBox.assign(N, 0);
        D.tailRecEnd = -1;
        for (const auto& sm : D.samples)
            if (!sm.empty()) D.tailRecEnd = std::max(D.tailRecEnd, sm.back().t);
        {
            auto ev = at ? autoOnEvents(*at) : std::unordered_map<int, std::vector<OnEvent>>{};
            if (tt)
                for (auto& kv : boxOnEvents(*tt))
                    ev[kv.first].insert(ev[kv.first].end(), kv.second.begin(), kv.second.end());
            for (size_t i = 0; i < N; ++i) {
                if (D.samples[i].empty()) continue;
                const auto e = ev.find(D.objs[i].uid);
                if (e == ev.end() || e->second.empty()) continue;
                D.tailOnEv[i] = e->second;
                for (const OnEvent& o : e->second) D.tailOnBox[i] |= (o.box >= 0) ? 1 : 0;
                ++g_recEndTailOnN;
                // --dyndbg <uid> (or -6, every one): the switches it takes past the recording's end
                if (g_dynDbg == D.objs[i].uid || g_dynDbg == -6) {
                    std::string s;
                    for (const OnEvent& o : e->second) {
                        char b[96];
                        if (o.box >= 0) std::snprintf(b, sizeof b, " box%d:%d+%.0f", o.box, (int)o.on, o.delay);
                        else if (o.autoIdx >= 0 && at && (size_t)o.autoIdx < at->size())
                            std::snprintf(b, sizeof b, " x%.0f:%d+%.0f", (*at)[(size_t)o.autoIdx].cx, (int)o.on,
                                          o.delay);
                        else std::snprintf(b, sizeof b, " t%d:%d+%.0f", o.at, (int)o.on, o.delay);
                        s += b;
                    }
                    loadPrintf("recendtail: uid %d at x %.0f, last row t=%d on=%d, recording to t=%d, switches%s\n",
                               D.objs[i].uid, (double)D.samples[i][0].cx, D.samples[i].back().t,
                               (int)D.samples[i].back().on, D.tailRecEnd, s.c_str());
                }
            }
        }
        if (at && !g_xTrack.empty()) {
            const std::vector<AutoTrig>& A = *at;
            std::unordered_map<int, size_t> dynIdx;
            for (size_t i = 0; i < N; ++i) dynIdx[D.objs[i].uid] = i;
            struct Mv { int s; int t0; double dur, dx, dy, erate; int ease; int stopT; };
            struct Lk { int s; int t0, t1; };
            std::unordered_map<int, std::vector<Mv>> mvOf;
            std::unordered_map<int, std::vector<Lk>> lkOf;
            std::unordered_set<int> bad;   // a target Move reaches it: not simulated
            std::unordered_map<int, size_t> trigIdx;
            for (size_t k = 0; k < A.size(); ++k) {
                trigIdx[A[k].uid] = k;
                const int cross = xRefCross(A[k].cx);
                if (cross < 0) continue;
                int stopT = 1 << 30;
                if (g_autoStop && k < g_autoStopBy.size())
                    for (const int s : g_autoStopBy[k])
                        if (s >= 0 && (size_t)s < A.size()) {
                            const int c2 = xRefCross(A[(size_t)s].cx);
                            if (c2 >= 0) stopT = std::min(stopT, c2);
                        }
                for (const TrigCtl& c : A[k].ctl) {
                    if (c.orbit) continue;
                    if (c.tmode) { bad.insert(c.uid); continue; }
                    if (c.lockTicks > 0.0)
                        lkOf[c.uid].push_back({-1, cross + c.spawnLag,
                                               std::min(cross + c.spawnLag + (int)c.lockTicks, stopT)});
                    if (c.dx != 0.f || c.dy != 0.f)
                        mvOf[c.uid].push_back({-1, cross + A[k].delay + c.spawnLag, c.durTicks, (double)c.dx,
                                               (double)c.dy, c.erate, c.ease, stopT});
                }
            }
            // The candidates, and the uids their orbits need (the centres, and theirs).
            std::vector<int> S;
            std::unordered_map<int, int> sIdx;
            auto addS = [&](int u) {
                if (sIdx.count(u)) return;
                sIdx[u] = (int)S.size();
                S.push_back(u);
            };
            std::vector<size_t> cand;
            for (size_t i = 0; i < N; ++i) {
                const int u = D.objs[i].uid;
                if (g_dynDbg == u)   // --dyndbg: why it is or is not a rig candidate
                    loadPrintf("recendtail: uid %d rows=%zu bad=%d rotSplit=%d recAuto=%d formula=%d reached=%d "
                               "lock=%d rot=%d\n", u, D.samples[i].size(), (int)bad.count(u),
                               (int)(i < D.rotSplit.size() && D.rotSplit[i]),
                               (int)(i < D.recAuto.size() && D.recAuto[i]),
                               (int)(i < D.formula.size() && D.formula[i]),
                               (int)((i < D.trigMask.size() && D.trigMask[i].any())
                                     || (i < D.autoAnchor.size() && D.autoAnchor[i] >= 0)),
                               (int)lkOf.count(u), (int)g_rotTailParts.count(u));
                if (D.samples[i].empty() || u < 0 || bad.count(u)) continue;
                if (i < D.rotSplit.size() && D.rotSplit[i]) continue;
                if (i < D.recAuto.size() && D.recAuto[i] && !g_recEndAuto) continue;
                if (i < D.formula.size() && D.formula[i]) continue;
                const bool reached = (i < D.trigMask.size() && D.trigMask[i].any())
                                     || (i < D.autoAnchor.size() && D.autoAnchor[i] >= 0);
                if (!reached) continue;
                if (!lkOf.count(u) && !g_rotTailParts.count(u)) continue;
                cand.push_back(i);
                addS(u);
            }
            g_recEndTailTried = (int)cand.size();
            for (size_t q = 0; q < S.size(); ++q) {
                const auto rp = g_rotTailParts.find(S[q]);
                if (rp == g_rotTailParts.end()) continue;
                for (const RotTailPart& p : rp->second)
                    if (p.centreUid != 0) addS(p.centreUid);
            }
            // reset positions: a recorded object's first row, a centre's own row in the dump
            std::vector<double> px0(S.size(), 0.0), py0(S.size(), 0.0), pr0(S.size(), 0.0);
            std::vector<uint8_t> known(S.size(), 0);
            for (size_t q = 0; q < S.size(); ++q) {
                if (bad.count(S[q])) continue;
                const auto di = dynIdx.find(S[q]);
                if (di != dynIdx.end() && !D.samples[di->second].empty()) {
                    const DynSample& s0 = D.samples[di->second][0];
                    px0[q] = s0.cx; py0[q] = s0.cy; pr0[q] = s0.rot;
                    known[q] = 1;
                } else if (const auto rc = rotCentrePos.find(S[q]); rc != rotCentrePos.end()) {
                    px0[q] = rc->second.first; py0[q] = rc->second.second;
                    known[q] = 1;
                }
            }
            // A uid is simulated when it and every centre it turns about (and theirs) are.
            std::vector<int8_t> ok(S.size(), -1);
            std::function<bool(int, int)> okOf = [&](int q, int depth) -> bool {
                if (ok[(size_t)q] >= 0) return ok[(size_t)q] != 0;
                if (depth > 8 || !known[(size_t)q]) { ok[(size_t)q] = 0; return false; }
                bool r = true;
                if (const auto rp = g_rotTailParts.find(S[(size_t)q]); rp != g_rotTailParts.end())
                    for (const RotTailPart& p : rp->second) {
                        if (!trigIdx.count(p.trigUid)) { r = false; break; }
                        if (p.centreUid != 0 && !okOf(sIdx[p.centreUid], depth + 1)) { r = false; break; }
                    }
                ok[(size_t)q] = r ? 1 : 0;
                return r;
            };
            for (size_t q = 0; q < S.size(); ++q) okOf((int)q, 0);
            // the acts, over the simulated uids
            std::vector<Mv> mvs;
            std::vector<Lk> lks;
            struct Rt { int t0; double dur, total, erate; int ease; int c; double cx; int trigUid;
                        std::vector<std::pair<int, uint8_t>> mem; };
            std::unordered_map<int, Rt> rts;   // by Rotate uid
            for (size_t q = 0; q < S.size(); ++q) {
                if (ok[q] != 1) continue;
                if (const auto m = mvOf.find(S[q]); m != mvOf.end())
                    for (Mv v : m->second) { v.s = (int)q; mvs.push_back(v); }
                if (const auto l = lkOf.find(S[q]); l != lkOf.end())
                    for (Lk v : l->second) { v.s = (int)q; lks.push_back(v); }
                if (const auto rp = g_rotTailParts.find(S[q]); rp != g_rotTailParts.end())
                    for (const RotTailPart& p : rp->second) {
                        const AutoTrig& T = A[trigIdx[p.trigUid]];
                        const int cross = xRefCross(T.cx);
                        if (cross < 0) continue;
                        auto it = rts.find(p.trigUid);
                        if (it == rts.end())
                            it = rts.emplace(p.trigUid, Rt{cross + T.delay, p.durT, p.total, p.erate, p.ease,
                                                           p.centreUid ? sIdx[p.centreUid] : -1, p.trigCx,
                                                           p.trigUid, {}}).first;
                        it->second.mem.push_back({(int)q, p.lock});
                    }
            }
            std::vector<Rt> rot;
            for (auto& kv : rts) rot.push_back(std::move(kv.second));
            std::sort(rot.begin(), rot.end(), [](const Rt& a, const Rt& b) {
                return a.cx != b.cx ? a.cx < b.cx : a.trigUid < b.trigUid;
            });
            // what each candidate is held against: its rows over the last kRecEndTailCheck ticks
            const int globalEnd = D.tailRecEnd;
            struct Chk { size_t i; int q; int tEnd; size_t row; double offX = 0, offY = 0, offR = 0;
                         double worst = 0, worstR = 0, gap = 0; double ex = 0, ey = 0, er = 0; bool have = false; };
            std::vector<Chk> chk;
            int tMax = 0;
            for (const size_t i : cand) {
                const int q = sIdx[D.objs[i].uid];
                if (ok[(size_t)q] != 1) continue;
                const auto& sm = D.samples[i];
                Chk c{i, q, sm.back().t, 0};
                while (c.row < sm.size() && sm[c.row].t < c.tEnd - kRecEndTailCheck) ++c.row;
                chk.push_back(c);
                D.rigTail[i].t0 = c.tEnd;
                D.rigTail[i].x.reserve(kRecEndTailTicks + 1);
                tMax = std::max(tMax, c.tEnd + kRecEndTailTicks);
                for (const Lk& l : lks)
                    if (l.s == q) {
                        D.rigTail[i].lockA = std::min(D.rigTail[i].lockA, l.t0);
                        D.rigTail[i].lockB = std::max(D.rigTail[i].lockB, l.t1);
                    }
            }
            std::vector<double> P(S.size() * 2), R(S.size());
            for (size_t q = 0; q < S.size(); ++q) { P[2 * q] = px0[q]; P[2 * q + 1] = py0[q]; R[q] = pr0[q]; }
            std::vector<uint8_t> noTurn(S.size(), 0);
            if (g_recEndAuto)
                for (size_t q = 0; q < S.size(); ++q) {
                    const auto di = dynIdx.find(S[q]);
                    if (di != dynIdx.end() && di->second < D.bucket.size()
                        && D.bucket[di->second] == Dynamics::SLOPE)
                        noTurn[q] = 1;
                }
            auto eAt = [](int tk, int t0, double dur, int ease, double erate) {
                if (tk < t0) return 0.0;
                return dur > 0.0 ? gdEase(ease, erate, std::min(1.0, (double)(tk - t0) / dur)) : 1.0;
            };
            // the acts by start, swept into an active list
            std::sort(mvs.begin(), mvs.end(), [](const Mv& a, const Mv& b) { return a.t0 < b.t0; });
            std::sort(lks.begin(), lks.end(), [](const Lk& a, const Lk& b) { return a.t0 < b.t0; });
            std::vector<size_t> actMv, actLk;
            size_t nextMv = 0, nextLk = 0;
            double xPrev = xRefAt(1);
            for (int t = 2; t <= tMax && !chk.empty(); ++t) {
                const double xNow = xRefAt(t);
                while (nextMv < mvs.size() && mvs[nextMv].t0 <= t) actMv.push_back(nextMv++);
                while (nextLk < lks.size() && lks[nextLk].t0 < t) actLk.push_back(nextLk++);
                for (size_t a = 0; a < actMv.size();) {
                    const Mv& v = mvs[actMv[a]];
                    const int te = std::min(t, v.stopT), tp = std::min(t - 1, v.stopT);
                    const double de = eAt(te, v.t0, v.dur, v.ease, v.erate) - eAt(tp, v.t0, v.dur, v.ease, v.erate);
                    P[2 * (size_t)v.s] += v.dx * de;
                    P[2 * (size_t)v.s + 1] += v.dy * de;
                    if (t >= v.stopT || (double)(t - v.t0) >= v.dur) { actMv[a] = actMv.back(); actMv.pop_back(); }
                    else ++a;
                }
                for (size_t a = 0; a < actLk.size();) {
                    const Lk& l = lks[actLk[a]];
                    if (t <= l.t1) P[2 * (size_t)l.s] += xNow - xPrev;
                    if (t >= l.t1) { actLk[a] = actLk.back(); actLk.pop_back(); }
                    else ++a;
                }
                for (const Rt& r : rot) {
                    if (t < r.t0 || (r.dur > 0.0 ? (double)(t - 1 - r.t0) >= r.dur : t != r.t0)) continue;
                    const double d = r.total * (eAt(t, r.t0, r.dur, r.ease, r.erate)
                                                - eAt(t - 1, r.t0, r.dur, r.ease, r.erate));
                    if (d == 0.0) continue;
                    const double th = -d * (3.14159265358979 / 180.0);
                    const double cs = std::cos(th), sn = std::sin(th);
                    for (const auto& m : r.mem) {
                        const size_t q = (size_t)m.first;
                        if (r.c >= 0 && (size_t)r.c != q) {
                            const double ccx = P[2 * (size_t)r.c], ccy = P[2 * (size_t)r.c + 1];
                            const double rx = P[2 * q] - ccx, ry = P[2 * q + 1] - ccy;
                            P[2 * q] = ccx + cs * rx - sn * ry;
                            P[2 * q + 1] = ccy + sn * rx + cs * ry;
                        }
                        if (!m.second && !noTurn[q]) R[q] += d;
                    }
                }
                xPrev = xNow;
                for (Chk& c : chk) {
                    const auto& sm = D.samples[c.i];
                    const double sx = P[2 * (size_t)c.q], sy = P[2 * (size_t)c.q + 1], sr = R[(size_t)c.q];
                    while (c.row < sm.size() && sm[c.row].t < t) ++c.row;
                    if (c.row < sm.size() && sm[c.row].t == t) {
                        const double ex = sm[c.row].cx - sx, ey = sm[c.row].cy - sy, er = sm[c.row].rot - sr;
                        if (!c.have) { c.ex = ex; c.ey = ey; c.er = er; c.have = true; }
                        c.worst = std::max({c.worst, std::fabs(ex - c.ex), std::fabs(ey - c.ey)});
                        c.worstR = std::max(c.worstR, std::fabs(er - c.er));
                        if (t == c.tEnd) { c.offX = ex; c.offY = ey; c.offR = er; }
                    }
                    if (t >= c.tEnd) {
                        Dynamics::RigTail& T = D.rigTail[c.i];
                        if (T.x.size() <= (size_t)kRecEndTailTicks) {
                            T.x.push_back((float)(sx + c.offX));
                            T.y.push_back((float)(sy + c.offY));
                            T.r.push_back((float)(sr + c.offR));
                        }
                        if (t <= globalEnd && !T.x.empty())
                            c.gap = std::max({c.gap, std::fabs((double)T.x.back() - (double)T.x.front()),
                                              std::fabs((double)T.y.back() - (double)T.y.front())});
                    }
                }
            }
            for (const Chk& c : chk) {
                Dynamics::RigTail& T = D.rigTail[c.i];
                const bool armed = c.have && T.x.size() > 1 && c.worst <= kRecEndTailTol
                                   && c.worstR <= kRecEndTailRotTol
                                   && std::hypot(c.offX, c.offY) <= kRecEndTailOff
                                   && c.gap <= kRecEndTailTol;
                if (g_dynDbg == D.objs[c.i].uid)
                    loadPrintf("recendtail: uid %d last row t=%d off=(%.3f,%.3f,%.3f) spread=%.3f/%.3fdeg "
                               "gap=%.3f lock=[%d,%d] -> %s\n", D.objs[c.i].uid, c.tEnd, c.offX, c.offY, c.offR,
                               c.worst, c.worstR, c.gap, T.lockA, T.lockB, armed ? "armed" : "not armed");
                if (!armed) { T = Dynamics::RigTail{}; continue; }
                ++g_recEndTailN;
            }
        }
    }
    if (g_tapDisarm && !L.dyn.objs.empty()) {
        Dynamics& D = L.dyn;
        auto disarmable = [](const OnEvent& o) {
            return o.autoIdx >= 0 && (size_t)o.autoIdx < g_autoDisarm.size()
                   && !g_autoDisarm[(size_t)o.autoIdx].empty();
        };
        D.disarmDep.assign(D.size(), 0);
        size_t n = 0, noBox = 0;   // noBox: no box reaches them, so they were placed once per tick before
        for (size_t i = 0; i < D.size(); ++i) {
            bool dep = i < D.disarmEv.size() && !D.disarmEv[i].empty();
            if (!dep && i < D.tailOnEv.size())
                dep = std::any_of(D.tailOnEv[i].begin(), D.tailOnEv[i].end(), disarmable);
            D.disarmDep[i] = dep ? 1 : 0;
            n += dep ? 1 : 0;
            if (dep && !(i < D.trigMask.size() && D.trigMask[i].any())) {
                ++noBox;
                if (g_dynDbg == -6) loadPrintf("tapdisarm: uid %d is reached by no box\n", D.objs[i].uid);
            }
        }
        if (n)
            loadPrintf("tapdisarm: %zu object(s) are placed for every group (their on/off reads a box), %zu of them "
                       "reached by no box\n", n, noBox);
    }
    return L;
}

inline std::string g_levelCsv;

inline bool loadLevelSettings(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string ln;
    while (std::getline(in, ln)) {
        const size_t eq = ln.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = ln.substr(0, eq);
        const int v = std::atoi(ln.c_str() + eq + 1);
        if (k == "fixRadiusCollision")      g_fixRadiusCollision = v;
        else if (k == "fixGravityBug")      g_fixGravityBug = v;
        else if (k == "fixNegativeScale")   g_fixNegativeScale = v;
        else if (k == "fixRobotJump")       g_fixRobotJump = v;
        else if (k == "dynamicLevelHeight") g_dynamicLevelHeight = v;
        else if (k == "platformerMode")     g_platformerMode = v;
        else if (k == "endPortalX")         g_endPortalX = std::atof(ln.c_str() + eq + 1);   // --goalgd
        else if (k == "endPortalFlipX")     g_endPortalFlipX = v;
    }
    return true;
}

inline std::string settingsPathBeside(const std::string& objrectsPath) {
    const size_t at = objrectsPath.rfind("objrects");
    if (at == std::string::npos) return std::string();
    return objrectsPath.substr(0, at) + "levelsettings"
           + objrectsPath.substr(at + 8);
}
inline std::string rotQPathBeside(const std::string& objrectsPath) {
    const size_t at = objrectsPath.rfind("objrects");
    if (at == std::string::npos) return std::string();
    return objrectsPath.substr(0, at) + "rotgameplay"
           + objrectsPath.substr(at + 8);
}
inline std::string forceIdsPathBeside(const std::string& objrectsPath) {
    const size_t at = objrectsPath.rfind("objrects");
    if (at == std::string::npos) return std::string();
    const std::string p = objrectsPath.substr(0, at) + "forceblocks"
                          + objrectsPath.substr(at + 8);
    std::ifstream probe(p);
    return probe ? p : std::string();
}

struct LoadLevelWrites {
    decltype(g_forceFields) forceFields;
    decltype(g_forceBoxes) forceBoxes;
    decltype(g_flipHeadBoxes) flipHeadBoxes;
    decltype(g_dashStopBoxes) dashStopBoxes;
    decltype(g_ctrlTouchBoxes) ctrlTouchBoxes;
    decltype(g_timeWarps) timeWarps;
    decltype(g_tpTrigs) tpTrigs;
    decltype(g_gravTrigs) gravTrigs;
    decltype(g_zoomTrigs) zoomTrigs;
    decltype(g_staticCams) staticCams;
    decltype(g_unlinkTrigs) unlinkTrigs;
    decltype(g_armBoxes) armBoxes;
    decltype(g_slideBoxes) slideBoxes;
    decltype(g_rotTrig) rotTrig;
    decltype(g_rotSpec) rotSpec;
    decltype(g_formulaDriven) formulaDriven{};
    decltype(g_freeModeCol) freeModeCol{};
    decltype(g_trigGateCol) trigGateCol{};
    decltype(g_staticCamCol) staticCamCol{};
    decltype(g_lockBox) lockBox{};
    decltype(g_lockTicks) lockTicks{};
    decltype(g_rotSeen) rotSeen{};
    decltype(g_rotRouted) rotRouted{};
    void take() {
        forceFields = g_forceFields;
        forceBoxes = g_forceBoxes;
        flipHeadBoxes = g_flipHeadBoxes;
        dashStopBoxes = g_dashStopBoxes;
        ctrlTouchBoxes = g_ctrlTouchBoxes;
        timeWarps = g_timeWarps;
        tpTrigs = g_tpTrigs;
        gravTrigs = g_gravTrigs;
        zoomTrigs = g_zoomTrigs;
        staticCams = g_staticCams;
        unlinkTrigs = g_unlinkTrigs;
        armBoxes = g_armBoxes;
        slideBoxes = g_slideBoxes;
        rotTrig = g_rotTrig;
        rotSpec = g_rotSpec;
        formulaDriven = g_formulaDriven;
        freeModeCol = g_freeModeCol;
        trigGateCol = g_trigGateCol;
        staticCamCol = g_staticCamCol;
        lockBox = g_lockBox;
        lockTicks = g_lockTicks;
        rotSeen = g_rotSeen;
        rotRouted = g_rotRouted;
    }
    void put() const {
        g_forceFields = forceFields;
        g_forceBoxes = forceBoxes;
        g_flipHeadBoxes = flipHeadBoxes;
        g_dashStopBoxes = dashStopBoxes;
        g_ctrlTouchBoxes = ctrlTouchBoxes;
        g_timeWarps = timeWarps;
        g_tpTrigs = tpTrigs;
        g_gravTrigs = gravTrigs;
        g_zoomTrigs = zoomTrigs;
        g_staticCams = staticCams;
        g_unlinkTrigs = unlinkTrigs;
        g_armBoxes = armBoxes;
        g_slideBoxes = slideBoxes;
        g_rotTrig = rotTrig;
        g_rotSpec = rotSpec;
        g_formulaDriven = formulaDriven;
        g_freeModeCol = freeModeCol;
        g_trigGateCol = trigGateCol;
        g_staticCamCol = staticCamCol;
        g_lockBox = lockBox;
        g_lockTicks = lockTicks;
        g_rotSeen = rotSeen;
        g_rotRouted = rotRouted;
    }
};

// The CLI's way in: the same parse, reading the dump the mod wrote to disk.
inline Level loadLevel(const std::string& path, const GroupTimeline* gt = nullptr,
                const std::vector<TouchTrig>* tt = nullptr,
                const std::vector<AutoTrig>* at = nullptr) {
    if (!g_levelCsv.empty()) {
        std::istringstream in(g_levelCsv);
        return loadLevelFrom(in, gt, tt, at);
    }
    std::ifstream in(path);
    return loadLevelFrom(in, gt, tt, at);
}

inline std::unordered_set<int> coinUidsFrom(std::istream& in) {
    std::unordered_set<int> out;
    std::string line;
    if (!std::getline(in, line)) return out;   // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string f[8];
        for (int i = 0; i < 8 && std::getline(ss, f[i], ','); ++i) {}
        if (f[1].empty() || f[7].empty()) continue;
        const int type = std::atoi(f[1].c_str());
        if (type == 22 || type == 31) out.insert(std::atoi(f[7].c_str()));
    }
    return out;
}

inline std::unordered_set<int> coinUids(const std::string& path) {
    if (!g_levelCsv.empty()) {
        std::istringstream in(g_levelCsv);
        return coinUidsFrom(in);
    }
    std::ifstream in(path);
    return coinUidsFrom(in);
}
}  // namespace dp
