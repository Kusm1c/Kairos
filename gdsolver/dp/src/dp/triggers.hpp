#pragma once
#include "dp/groups.hpp"

namespace dp {
struct TouchTrig {
    double cx, cy, hw, hh;          // the box the player has to enter
    std::vector<TrigCtl> ctl;
    int uid = -1;
    int id = 0;
    int togOn = -1;
    std::vector<int> togOnUids;
    std::vector<int> spawnsRoots;
    TouchMask armBy{};
    struct TogEv {
        int uid;
        uint8_t on;        // 1 = the Toggle activates the group, 0 = deactivates
        double delay;      // ticks after this box fires (the chain's spawn delays)
        int grp = 0;       // the group the Toggle switches (read by --tapdisarm, Dynamics::disarmEv)
    };
    std::vector<TogEv> togEv;
    std::vector<TogEv> trigTog;
    bool activator = false;
    bool press = false;
    bool spawnRing = false;
    bool offSwitch = false;
    int item = 0;
    int count = -1;
    int cmode = 0;
    bool tap = false;
    double tapCloseX = 1e18;
    int tapChan = 0, tapCloseChan = 0;
    double tapCloseY = 0.0;
    bool tapCloseTouch = false;
    double tapCloseHw = 0.0, tapCloseHh = 0.0;
    int tapCloseUid = -1;
    bool reachesCoin = false;
    std::vector<int> stops;
    double stopDelay = 0.0;
    int followIdx = -1;
    double followX0 = 0.0, followY0 = 0.0;
    const std::vector<DynSample>* recBox = nullptr;
    struct OwnTog {
        int uid;
        double cx;
        uint8_t on;
        int autoIdx;
    };
    std::vector<OwnTog> ownTog;
};
inline std::unordered_set<int> g_coinUids;
inline const std::vector<Obj>* g_touchFollowObjs = nullptr;
inline std::unordered_map<int, int> g_itemGiver;
inline int g_fbForceBox = -1;
inline int g_fbForceTick = 0;
// --replayon (diagnostic, --replay only): walk past the first death (cli.hpp).
inline bool g_replayOn = false;
struct Collectible {
    double cx, cy, hw, hh;
    int item = 0;
    int uid = -1;
};
inline std::vector<Collectible> g_collect;
inline std::vector<TouchTrig> g_touch;
struct PressWin {
    double lo, hi;
    int bit;
};
inline std::vector<PressWin> g_pressWin;
inline TouchMask g_actMask{};
inline constexpr bool kDefAirPress = false;
inline bool g_airPress = kDefAirPress;
inline bool g_refAdopt = false;
inline bool g_refAdoptMem = false;
inline long long g_refAdoptT = -1;       // the first ADOPT's tick, -1 = none
inline std::string g_refAdoptFields;     // every field any ADOPT had to overlook
inline std::unordered_map<int, std::vector<OnEvent>> boxOnEvents(
        const std::vector<TouchTrig>& touch) {
    std::unordered_map<int, std::vector<OnEvent>> out;
    for (size_t b = 0; b < touch.size() && b < (size_t)kTouchBits; ++b) {
        const TouchTrig& T = touch[b];
        if (T.tap || T.count >= 0) continue;
        for (const TouchTrig::TogEv& e : T.togEv) {
            OnEvent ev;
            ev.box = (int)b;
            ev.on = e.on;
            ev.delay = e.delay;
            out[e.uid].push_back(ev);
        }
    }
    return out;
}
inline std::vector<int> g_touchMoveTicks;
// fireB's invariant counters (--firebcheck). See the check at the key site.
inline bool g_fireBCheck = false;
inline unsigned long long g_fireBNoTick = 0, g_fireBNoBit = 0, g_fireBTooEarly = 0;
inline void buildTouchMoveTicks() {
    g_touchMoveTicks.assign(g_touch.size(), 0);
    g_touchStops.assign(g_touch.size(), {});   // --movetarget (see the field)
    for (size_t b = 0; b < g_touch.size(); ++b) g_touchStops[b] = g_touch[b].stops;
    for (size_t b = 0; b < g_touch.size(); ++b) {
        double d = 0.0;
        for (const TrigCtl& c : g_touch[b].ctl)
            d = std::max(d, c.durTicks + (double)c.spawnLag);
        for (const TrigCtl& c : g_touch[b].ctl) d = std::max(d, c.lockTicks);
        for (const TouchTrig::TogEv& e : g_touch[b].togEv) d = std::max(d, e.delay);
        if (d <= 0.0) continue;             // nothing moves: contributes nothing
        g_touchMoveTicks[b] = (int)std::ceil(d) + 5 + 4;
    }
}
inline std::array<std::vector<TouchTrig>, 4> g_touchFrame;
inline const std::vector<TouchTrig>& touchFor(int f) {
    f &= 3;
    if (f == 0 || g_touch.empty()) return g_touch;
    auto& v = g_touchFrame[(size_t)f];
    if (v.empty()) {
        v = g_touch;
        for (TouchTrig& T : v) {
            T.ctl.clear();
            double u, w;
            toFrame(f, T.cx, T.cy, u, w);
            T.cx = u; T.cy = w;
            if (f & 1) std::swap(T.hw, T.hh);
        }
    }
    return v;
}
// which bits have already been announced (diagnostic only)
inline TouchMask g_trigReported{};
struct BandRow {
    long long t; int alive; float ylo, yhi, x;
    int capdrop = 0, merged = 0;
    std::string cls;
};
inline std::string g_bandPath;
inline std::vector<BandRow> g_bands;
inline TouchMask g_needTrig{};
inline bool g_needUnseen = false;
inline TouchMask g_needSkip{};
inline std::vector<int> g_needTrigUids;
inline bool g_obbAll = true;

struct TrigRow {
    int uid, id, target, center, touch, spawn;
    double cx, cy, w, h, dur, ox, oy;
    int ease = 0; double erate = 2.0;
    int lockx = 0, locky = 0;
    double grav = 1.0; int gravmod = 0;
    double deg = 0.0;
    int ord = 0, chan = 0, sord = 0, sordd = 0;
    int t360 = 0;
    int lockrot = 0;
    bool hasAngle = false;
    int togon = -1;
    int item = 0, item2 = 0, count = -1, actgrp = -1, cmode = -1;
    int thold = 0, ttog = 0;
    bool cmpCols = false;
    int i1mode = 0, i2mode = 0, res1 = 0, res2 = 0, res3 = -1;
    double mod1 = 0.0, mod2 = 0.0;
    std::vector<std::pair<int, int>> remap;
    int mvtgt = 0, mvaxis = 0, tmodctr = 0;
    // A Spawn's delay, the 27th column, in seconds (0 on dumps that predate it).
    double sdelay = 0.0;
    int sponly = 0;
    bool fyCols = false;
    double fySpeed = 0.0, fyDelay = 0.0, fyMax = 0.0;
    int fyOffset = 0;
};

inline bool g_spawnRemap = true;

inline bool g_touchVisual = true;
inline bool visualOnly(int id) { return id == 1006 || id == 1007; }

// Apply a remap to a group id; unmapped ids pass through.
inline int remapGroup(const std::vector<std::pair<int, int>>& rm, int g) {
    for (const auto& p : rm)
        if (p.first == g) return p.second;
    return g;
}

inline std::vector<std::pair<int, int>> composeRemap(
        const std::vector<std::pair<int, int>>& own,
        const std::vector<std::pair<int, int>>& inherited) {
    if (own.empty()) return inherited;
    std::vector<std::pair<int, int>> out;
    for (const auto& p : own) out.push_back({p.first, remapGroup(inherited, p.second)});
    for (const auto& p : inherited) {
        bool shadowed = false;
        for (const auto& q : own) shadowed |= (q.first == p.first);
        if (!shadowed) out.push_back(p);
    }
    return out;
}
inline bool loadTrigRows(const std::string& path,
                         std::unordered_map<int, TrigRow>& trig) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);   // header
    while (std::getline(in, line)) {
        TrigRow r{};
        const int n = std::sscanf(
            line.c_str(),
            "%d,%d,%lf,%lf,%lf,%lf,%d,%d,%d,%d,%lf,%lf,%lf,%d,%lf,%d,%d,"
            "%lf,%d,%lf,%d,%d,%d,%d,%d,%d",
            &r.uid, &r.id, &r.cx, &r.cy, &r.w, &r.h, &r.target, &r.center,
            &r.touch, &r.spawn, &r.dur, &r.ox, &r.oy, &r.ease, &r.erate,
            &r.lockx, &r.locky,
            &r.grav, &r.gravmod, &r.deg, &r.ord, &r.chan, &r.sord, &r.sordd,
            &r.t360, &r.lockrot);
        if (n < 13) continue;
        if (n < 15) { r.ease = 0; r.erate = 2.0; }
        r.hasAngle = (n >= 26);
        {   // togon, the 35th column: found by position past the 26 parsed above,
            size_t p = 0;
            int commas = 0;
            while (commas < 34 && (p = line.find(',', p)) != std::string::npos) {
                ++p;
                ++commas;
            }
            if (commas == 34 && p < line.size()) r.togon = std::atoi(line.c_str() + p);
            {
                auto field = [&](int k) -> int {
                    size_t q = 0;
                    for (int c = 0; c < k; ++c) {
                        q = line.find(',', q);
                        if (q == std::string::npos) return 0;
                        ++q;
                    }
                    return std::atoi(line.c_str() + q);
                };
                r.mvtgt = field(27);
                r.mvaxis = field(28);
                r.tmodctr = field(29);
                auto dfield = [&](int k) -> double {
                    size_t q = 0;
                    for (int c = 0; c < k; ++c) {
                        q = line.find(',', q);
                        if (q == std::string::npos) return 0.0;
                        ++q;
                    }
                    return std::atof(line.c_str() + q);
                };
                r.sdelay = dfield(26);
            }
            if (commas == 34 && (p = line.find(',', p)) != std::string::npos) {
                std::stringstream es(line.substr(p + 1));
                std::string ent;
                while (std::getline(es, ent, ';')) {
                    int a = 0, b = 0, c = 0, d = 0;
                    if (std::sscanf(ent.c_str(), "%d:%d:%d:%d", &a, &b, &c, &d) == 4
                        && a != 0 && c != 0)
                        r.remap.push_back({a, c});
                }
            }
        }
        {   // The item columns, the 37th on (item,item2,count,subcount,actgrp,
            size_t q = 0;
            int cm = 0;
            while (cm < 36 && (q = line.find(',', q)) != std::string::npos) {
                ++q;
                ++cm;
            }
            if (cm == 36 && q < line.size()) {
                int it = 0, it2 = 0, cnt = -1, sub = 0, act = -1,
                    th = 0, tt = 0, td = 0, cmd = -1;
                int m1 = 0, m2 = 0, tm = 0, r1 = 0, r2 = 0, r3 = -1;
                double md1 = 0.0, md2 = 0.0;
                const int got = std::sscanf(
                    line.c_str() + q,
                    "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lf,%lf,%d,%d,%d",
                    &it, &it2, &cnt, &sub, &act, &th, &tt, &td, &cmd,
                    &m1, &m2, &tm, &md1, &md2, &r1, &r2, &r3);
                if (got >= 5) {
                    r.item = it;
                    r.item2 = it2;
                    r.count = cnt;
                    r.actgrp = act;
                    r.cmode = cmd;
                }
                if (got >= 7) {
                    r.thold = th;
                    r.ttog = tt;
                }
                {
                    size_t s = 0;
                    int cs = 0;
                    while (cs < 58 && (s = line.find(',', s)) != std::string::npos) {
                        ++s;
                        ++cs;
                    }
                    if (cs == 58 && s < line.size()) r.sponly = std::atoi(line.c_str() + s);
                    // fyspd,fydel,fyoff,fymax: the four columns after sponly.
                    if (cs == 58 && (s = line.find(',', s)) != std::string::npos) {
                        double sp = 0.0, de = 0.0, mx = 0.0;
                        int of = 0;
                        if (std::sscanf(line.c_str() + s + 1, "%lf,%lf,%d,%lf", &sp, &de, &of,
                                        &mx) == 4) {
                            r.fyCols = true;
                            r.fySpeed = sp;
                            r.fyDelay = de;
                            r.fyOffset = of;
                            r.fyMax = mx;
                        }
                    }
                }
                if (got >= 17) {
                    r.cmpCols = true;
                    r.i1mode = m1;
                    r.i2mode = m2;
                    r.mod1 = md1;
                    r.mod2 = md2;
                    r.res1 = r1;
                    r.res2 = r2;
                    r.res3 = r3;
                }
            }
        }
        trig[r.uid] = r;
    }
    return true;
}

struct TrigRowsCache {
    bool valid = false;
    std::string path, bytes;
    std::unordered_map<int, TrigRow> rows;
};
// (namespace scope, not a function's static, so cli.hpp's releaseCrossCallCaches can empty it)
inline TrigRowsCache g_trigRowsCache;
inline const std::unordered_map<int, TrigRow>* trigRowsCached(const std::string& path) {
    TrigRowsCache& c = g_trigRowsCache;
    std::string bytes;
    if (!readFileBytes(path, bytes)) return nullptr;
    if (!(c.valid && c.path == path && c.bytes == bytes)) {
        c = TrigRowsCache{};
        if (!loadTrigRows(path, c.rows)) return nullptr;
        c.valid = true;
        c.path = path;
        c.bytes = std::move(bytes);
    }
    return &c.rows;
}
struct GroupMembersCache {
    bool valid = false;
    std::string path, bytes;
    std::unordered_map<int, std::vector<int>> byGroup;
};
inline GroupMembersCache g_groupMembersCache;   // (namespace scope, as g_trigRowsCache)
inline const std::unordered_map<int, std::vector<int>>* groupMembersCached(
        const std::string& path) {
    GroupMembersCache& c = g_groupMembersCache;
    std::string bytes;
    if (!readFileBytes(path, bytes)) return nullptr;
    if (!(c.valid && c.path == path && c.bytes == bytes)) {
        c = GroupMembersCache{};
        std::ifstream in(path);
        if (!in) return nullptr;
        std::string line;
        std::getline(in, line);   // header
        while (std::getline(in, line)) {
            std::stringstream ss(line);
            int uid = 0;
            if (!(ss >> uid)) continue;
            int g = 0;
            while (ss >> g) c.byGroup[g].push_back(uid);
        }
        c.valid = true;
        c.path = path;
        c.bytes = std::move(bytes);
    }
    return &c.byGroup;
}

template <class Fn>
inline void forTogglesHolding(int uid, const std::unordered_map<int, TrigRow>& trig,
                              const std::unordered_map<int, std::vector<int>>& byGroup, Fn&& fn) {
    for (const auto& kv : trig) {
        const TrigRow& X = kv.second;
        if (X.id != 1049 || X.togon < 0 || X.target == 0 || X.uid == uid) continue;
        const auto gi = byGroup.find(X.target);
        if (gi == byGroup.end()
            || std::find(gi->second.begin(), gi->second.end(), uid) == gi->second.end())
            continue;
        fn(X);
    }
}
inline bool offAtCrossing(const TrigRow& T, const std::unordered_map<int, TrigRow>& trig,
                          const std::unordered_map<int, std::vector<int>>& byGroup,
                          bool* spawnOn = nullptr) {
    const TrigRow* last = nullptr;
    bool sOn = false;
    forTogglesHolding(T.uid, trig, byGroup, [&](const TrigRow& X) {
        if (X.spawn) {
            if (X.togon == 1) sOn = true;
            return;
        }
        if (X.touch || X.cx >= T.cx) return;
        if (!last || X.cx > last->cx || (X.cx == last->cx && X.uid > last->uid)) last = &X;
    });
    if (spawnOn) *spawnOn = sOn;
    return last && last->togon == 0;
}
inline bool boxArmsTrigger(int uid, const std::unordered_map<int, TrigRow>& trig,
                           const std::unordered_map<int, std::vector<int>>& byGroup) {
    for (const auto& kv : trig) {
        const TrigRow& B = kv.second;
        if (B.id != 1594 || B.actgrp != 1 || B.spawn || B.target == 0) continue;
        const auto gi = byGroup.find(B.target);
        if (gi != byGroup.end() && std::find(gi->second.begin(), gi->second.end(), uid) != gi->second.end())
            return true;
    }
    return false;
}

inline bool loadCollectibles(const std::string& path) {
    g_collect.clear();
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);   // header
    while (std::getline(in, line)) {
        int uid = 0, id = 0, item = 0, points = 0, pickup = 0, toggle = 0, sub = 0;
        double cx = 0, cy = 0, w = 0, h = 0;
        if (std::sscanf(line.c_str(), "%d,%d,%lf,%lf,%lf,%lf,%d,%d,%d,%d,%d",
                        &uid, &id, &cx, &cy, &w, &h, &item, &points, &pickup,
                        &toggle, &sub) < 9)
            continue;
        if (item != 0 && id != 1816 && id != 1615) g_itemGiver[uid] = item;
        if (!pickup || item == 0) continue;
        g_collect.push_back({cx, cy, w * 0.5, h * 0.5, item, uid});
    }
    std::sort(g_collect.begin(), g_collect.end(),
              [](const Collectible& a, const Collectible& b) {
                  return a.cx < b.cx || (a.cx == b.cx && a.cy < b.cy);
              });
    return true;
}

inline bool g_touchWinAhead = true;
inline bool g_trigDump = false;
inline bool g_trigEffect = false;
inline bool g_stopDump = false;
inline double g_trigWinNear = -1e18;
inline size_t g_trigTotal = 0, g_trigRelevantN = 0, g_trigKept = 0,
              g_trigDroppedRelevant = 0;
inline size_t g_trigDroppedBehind = 0, g_trigDroppedAhead = 0;
inline double g_trigMaxKeptX = 0.0;
inline double g_trigFirstAheadX = 1e300;

inline std::unordered_map<int, int> loadObjTypesFrom(std::istream& in);
inline std::unordered_map<int, int> loadObjTypes(const std::string& objPath) {
    std::ifstream in(objPath);
    if (!in) return {};
    return loadObjTypesFrom(in);
}
inline std::unordered_map<int, int> loadObjTypesFrom(std::istream& in) {
    std::unordered_map<int, int> out;
    std::string line;
    if (!std::getline(in, line)) return out;
    int iu = -1, it = -1, n = 0;
    {
        std::stringstream hs(line);
        std::string c;
        while (std::getline(hs, c, ',')) {
            if (c == "uid") iu = n;
            else if (c == "type") it = n;
            ++n;
        }
    }
    if (iu < 0 || it < 0) return out;
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string c;
        int i = 0, uid = -1, ty = -1;
        while (std::getline(ss, c, ',')) {
            if (i == iu) uid = std::atoi(c.c_str());
            else if (i == it) ty = std::atoi(c.c_str());
            ++i;
        }
        if (uid >= 0 && ty >= 0) out[uid] = ty;
    }
    return out;
}
inline std::unordered_map<int, std::pair<double, double>> loadObjPos(std::istream& in) {
    std::unordered_map<int, std::pair<double, double>> out;
    std::string line;
    if (!std::getline(in, line)) return out;
    int iu = -1, ix = -1, iy = -1, n = 0;
    {
        std::stringstream hs(line);
        std::string c;
        while (std::getline(hs, c, ',')) {
            if (c == "uid") iu = n;
            else if (c == "cx") ix = n;
            else if (c == "cy") iy = n;
            ++n;
        }
    }
    if (iu < 0 || ix < 0 || iy < 0) return out;
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string c;
        int i = 0, uid = -1;
        double x = 0.0, y = 0.0;
        while (std::getline(ss, c, ',')) {
            if (i == iu) uid = std::atoi(c.c_str());
            else if (i == ix) x = std::atof(c.c_str());
            else if (i == iy) y = std::atof(c.c_str());
            ++i;
        }
        if (uid >= 0) out[uid] = {x, y};
    }
    return out;
}
inline std::unordered_map<int, double> loadObjSpawnX(std::istream& in) {
    std::unordered_map<int, double> out;
    std::string line;
    if (!std::getline(in, line)) return out;
    int iu = -1, ix = -1, is = -1, n = 0;
    {
        std::stringstream hs(line);
        std::string c;
        while (std::getline(hs, c, ',')) {
            if (c == "uid") iu = n;
            else if (c == "cx") ix = n;
            else if (c == "spawn") is = n;
            ++n;
        }
    }
    if (iu < 0 || ix < 0 || is < 0) return out;
    while (std::getline(in, line)) {
        std::stringstream ss(line);
        std::string c;
        int i = 0, uid = -1, sp = 0;
        double x = 0.0;
        while (std::getline(ss, c, ',')) {
            if (i == iu) uid = std::atoi(c.c_str());
            else if (i == ix) x = std::atof(c.c_str());
            else if (i == is) sp = std::atoi(c.c_str());
            ++i;
        }
        if (uid >= 0 && sp == 1) out[uid] = x;
    }
    return out;
}
inline bool collidableType(int t) {
    return t == 0 || t == 2 || t == 21 || t == 47;
}
inline bool activatorTargetSafe(int t) {
    return t == 0 || t == 21 || t == 22 || t == 31 || t == 11 || t == 12 || t == 13
        || t == 29 || t == 32 || t == 35 || t == 37 || t == 38 || t == 43 || t == 7;
}
inline bool g_actTele = true;
inline bool g_pressFresh = true;
inline bool g_flyHeldBox = true;
inline bool g_boxOff = true;
inline int itemCompareGate(const TrigRow& T) {
    if (T.id != 3620 || !T.cmpCols) return 0;
    if (T.i1mode != 1 || T.i2mode > 0 || T.res3 != 2 || T.mod1 != 1.0) return 0;
    if (T.item <= 0 || T.mod2 < 1.0 || T.mod2 != std::floor(T.mod2)) return 0;
    return (int)T.mod2;
}
inline bool xCrossingRow(const TrigRow& T) {
    return !T.spawn && !T.touch && T.id != 1595 && T.id != 1594 && T.id != 1611 && T.id != 1811 && T.id != 3620;
}
inline std::vector<TouchTrig> loadTouchTriggers(const std::string& trigPath,
                                                const std::string& grpPath,
                                                double fromX = -1e18,
                                                const std::unordered_map<int, int>*
                                                    objTypes = nullptr,
                                                const std::unordered_map<int,
                                                    std::pair<double, double>>*
                                                    objPos = nullptr) {
    std::vector<TouchTrig> out;
    size_t nIrrelevant = 0;                             // --trigrelevant
    size_t nGateCosmetic = 0;                           // --worldgates (see below)
    const std::unordered_map<int, TrigRow>* trigP = trigRowsCached(trigPath);
    if (!trigP) {
        std::fprintf(stderr, "triggers: cannot open %s\n", trigPath.c_str());
        return out;
    }
    const std::unordered_map<int, TrigRow>& trig = *trigP;   // uid -> row
    const std::unordered_map<int, std::vector<int>>* membersP = groupMembersCached(grpPath);
    if (!membersP) {
        std::fprintf(stderr, "objgroups: cannot open %s\n", grpPath.c_str());
        return out;
    }
    std::unordered_map<int, std::vector<int>> byGroup = *membersP;
    for (const auto& kv : trig) {
        const TrigRow& T = kv.second;
        const bool isCount = (T.id == 1611 || T.id == 1811) && T.count >= 0
                             && gatesModelled();
        const bool isTapDisarm = (T.id == 1595) && !gatesModelled() && g_tapDisarm && !T.touch && !T.spawn;
        const bool isTap = ((T.id == 1595) && gatesModelled()) || isTapDisarm;
        const int cmpNeed = gatesModelled() ? itemCompareGate(T) : 0;
        const bool isCmp = cmpNeed > 0;
        bool isActivator = false, isPress = false, isSpawnRing = false;
        if (objTypes && T.id == 1594 && T.sponly == 1
            && T.target != 0 && !T.spawn) {
            const auto gi = byGroup.find(T.target);
            int bad = -1, badType = 0, nTog = 0;
            if (gi != byGroup.end())
                for (const int ou : gi->second) {
                    const auto t2 = trig.find(ou);
                    if (t2 == trig.end()) continue;   // its own objects are inert (decoration)
                    const TrigRow& S = t2->second;
                    if (!S.spawn || (S.id != 1049 && S.id != 1616)) { bad = ou; badType = -1; break; }
                    if (S.id != 1049) continue;
                    ++nTog;
                    const auto tg = byGroup.find(S.target);
                    if (tg == byGroup.end()) continue;
                    for (const int su : tg->second) {
                        if (trig.count(su)) continue;
                        const auto ot = objTypes->find(su);
                        const int t = ot == objTypes->end() ? -1 : ot->second;
                        // --acthazoff (speed.hpp): a hazard the Toggle switches OFF
                        if (g_actHazOff && t == 2 && S.togon == 0) continue;
                        if (!activatorTargetSafe(t) && t != 8 && t != 9 && t != 10 && t != 34) {
                            bad = su;
                            badType = t;
                            break;
                        }
                    }
                    if (bad >= 0) break;
                }
            if (gi == byGroup.end() || nTog == 0) {
                std::printf("activators: spawn ring uid %d left out - group %d spawns no "
                            "Toggle\n", T.uid, T.target);
            } else if (bad >= 0 && badType == -1) {
                std::printf("activators: spawn ring uid %d left out - group %d holds trigger "
                            "uid %d, not a spawned Toggle or Stop\n", T.uid, T.target, bad);
            } else if (bad >= 0) {
                std::printf("activators: spawn ring uid %d left out - a Toggle it spawns reaches "
                            "uid %d of type %d\n", T.uid, bad, badType);
            } else {
                isActivator = true;
                isPress = true;
                isSpawnRing = true;
                std::printf("activators: spawn ring uid %d at (%.0f,%.0f) spawns group %d "
                            "(%d Toggle(s))\n", T.uid, T.cx, T.cy, T.target, nTog);
            }
        }
        std::vector<int> armUids;
        std::vector<int> armSkip;   // ...and the unmodelled trigger objects such a block's group holds besides
        if (!isSpawnRing && objTypes && T.actgrp == 1 && T.target != 0
            && !T.spawn) {
            const auto ty = objTypes->find(T.uid);
            const bool pickup = !T.touch && ty != objTypes->end() && ty->second == 30;
            const bool press = T.id == 1594;
            if (pickup || press) {
                const auto gi = byGroup.find(T.target);
                int bad = -1, badType = 0;
                if (gi != byGroup.end())
                    for (const int ou : gi->second) {
                        if (g_boxArm && press && trig.count(ou)) {
                            const TrigRow& M = trig.at(ou);
                            bool spawnOn = false;
                            if (M.id == 901 && xCrossingRow(M) && M.target != 0
                                && offAtCrossing(M, trig, byGroup, &spawnOn) && !spawnOn) {
                                armUids.push_back(ou);
                                continue;
                            }
                        }
                        if (trig.count(ou)) { bad = ou; badType = -1; break; }
                        const auto ot = objTypes->find(ou);
                        const int t = ot == objTypes->end() ? -1 : ot->second;
                        if (g_boxArm && press && t == 20) {
                            armSkip.push_back(ou);
                            continue;
                        }
                        if (!activatorTargetSafe(t) && !(g_actTele && press && t == 28)) {
                            bad = ou;
                            badType = t;
                            break;
                        }
                    }
                // (only for a block that does switch Moves back on: without them the old refusal stands)
                if (bad < 0 && armUids.empty() && !armSkip.empty()) {
                    bad = armSkip.front();
                    badType = 20;
                }
                if (gi == byGroup.end()) {
                    std::printf("activators: uid %d (id %d) left out - group %d is empty\n",
                                T.uid, T.id, T.target);
                } else if (bad >= 0 && badType == -1) {
                    std::printf("activators: uid %d (id %d) left out - group %d holds the "
                                "trigger uid %d, a chain this does not follow\n",
                                T.uid, T.id, T.target, bad);
                } else if (bad >= 0) {
                    std::printf("activators: uid %d (id %d) left out - group %d holds uid %d "
                                "of type %d, not something to stand on, collect or press\n",
                                T.uid, T.id, T.target, bad, badType);
                } else {
                    isActivator = true;
                    isPress = press;
                    if (!armUids.empty()) {
                        std::printf("boxarm: uid %d (id %d, x %.0f) switches group %d back on, %zu Move(s):",
                                    T.uid, T.id, T.cx, T.target, armUids.size());
                        for (const int u : armUids) std::printf(" %d", u);
                        std::printf("\n");
                    }
                }
            }
        }
        if (!isActivator) {
            armUids.clear();
            armSkip.clear();
        }
        bool isOffBox = false;
        if (g_boxOff && !isSpawnRing && !isActivator && objTypes && T.target != 0 && !T.spawn
            && ((T.id == 1594 && T.actgrp == 0) || (T.id == 1049 && T.touch && T.togon == 0))) {
            const auto gi = byGroup.find(T.target);
            int bad = -1, badType = 0;
            if (gi != byGroup.end())
                for (const int ou : gi->second) {
                    if (trig.count(ou)) { bad = ou; badType = -1; break; }
                    const auto ot = objTypes->find(ou);
                    const int t = ot == objTypes->end() ? -1 : ot->second;
                    if (!activatorTargetSafe(t) && t != 2) { bad = ou; badType = t; break; }
                }
            if (gi == byGroup.end()) {
                std::printf("boxoff: uid %d (id %d) left out - group %d is empty\n",
                            T.uid, T.id, T.target);
            } else if (bad >= 0) {
                std::printf("boxoff: uid %d (id %d) left out - group %d holds uid %d of type %d\n",
                            T.uid, T.id, T.target, bad, badType);
            } else {
                isActivator = true;
                isPress = T.id == 1594;
                isOffBox = true;
                std::printf("boxoff: uid %d (id %d) at (%.0f,%.0f) switches group %d off "
                            "(%zu objects)\n", T.uid, T.id, T.cx, T.cy, T.target,
                            gi->second.size());
            }
        }
        if ((!T.touch && !isCount && !isTap && !isCmp && !isActivator) || T.target == 0)
            continue;
        if (g_touchVisual && visualOnly(T.id)) continue;   // --touchvisual: a tint is no box
        TouchTrig tt{T.cx, T.cy, T.w * 0.5, T.h * 0.5, {}};
        tt.uid = T.uid;   // so an outside payload can name the box (see the field)
        tt.id = T.id;
        tt.togOn = (T.id == 1049) ? T.togon : -1;   // --rotqtoggle (see the field)
        if (isCount) {
            tt.item = T.item;
            tt.count = T.count;
            tt.cmode = T.cmode > 0 ? T.cmode : 0;
        }
        if (isCmp) {
            tt.item = T.item;
            tt.count = cmpNeed;
            tt.cmode = 3;
        }
        tt.tap = isTap;
        if (g_touchOwnOff && T.touch) {
            bool chained = false;
            forTogglesHolding(T.uid, trig, byGroup, [&](const TrigRow& X) {
                if (X.spawn || X.touch) {
                    chained = true;
                    return;
                }
                tt.ownTog.push_back({X.uid, X.cx, (uint8_t)(X.togon == 1), -1});
            });
            if (chained) tt.ownTog.clear();
            std::sort(tt.ownTog.begin(), tt.ownTog.end(),
                      [](const TouchTrig::OwnTog& a, const TouchTrig::OwnTog& b) {
                          return a.cx < b.cx || (a.cx == b.cx && a.uid < b.uid);
                      });
        }
        if (T.id == 1049 && T.togon >= 0) {
            const auto gi = byGroup.find(T.target);
            if (gi != byGroup.end())
                for (const int ou : gi->second)
                    if (!trig.count(ou))
                        tt.togEv.push_back({ou, (uint8_t)(T.togon == 1), 0.0, T.target});
                    else if (g_tapDisarm && xCrossingRow(trig.at(ou)))
                        tt.trigTog.push_back({ou, (uint8_t)(T.togon == 1), 0.0});
        }
        tt.activator = isActivator;
        tt.press = isPress;
        tt.spawnRing = isSpawnRing;
        tt.offSwitch = isOffBox;
        if (isActivator && !isSpawnRing && !(isOffBox && T.id == 1049))
            for (const int ou : byGroup[T.target]) {
                if (!armUids.empty() && (std::find(armUids.begin(), armUids.end(), ou) != armUids.end()
                                         || std::find(armSkip.begin(), armSkip.end(), ou) != armSkip.end()))
                    continue;
                tt.togEv.push_back({ou, (uint8_t)(isOffBox ? 0 : 1), 0.0, T.target});
            }
        for (const int u : armUids) tt.trigTog.push_back({u, 1, 0.0});
        struct Item {
            int group; float dx, dy; double dur; int ease; double erate;
            double lock, lockY;
            // the remap the triggers in `group` act under (--spawnremap)
            std::vector<std::pair<int, int>> remap;
            // --movetarget only (see TrigCtl::mover / tmode / tdx / tdy)
            int mover = 0;
            uint8_t tmode = 0;
            float tdx = 0.f, tdy = 0.f;
            double sdelay = 0.0;
            bool root = false;   // the box's own target group
            double fireS = 0.0;
            bool ordered = false;
            bool togHop = false;
        };
        const bool rootTurns = (T.deg != 0.0 || T.t360 != 0);
        const bool rootMoves = (T.ox != 0.0 || T.oy != 0.0 || rootTurns);
        size_t nInert = 0;
        size_t nTogHopSkip = 0;   // --tapdisarm: spawn-fired triggers in a Toggle's group, no longer followed
        double maxDur = 0.0, maxLock = 0.0, maxOff = 0.0;
        std::vector<Item> stack{{T.target, (float)T.ox, (float)T.oy,
                                 rootMoves ? T.dur * 240.0 : 0.0,
                                 rootMoves ? T.ease : 0,
                                 rootMoves ? T.erate : 2.0,
                                 T.lockx ? T.dur * 240.0 : 0.0,
                                 T.locky ? T.dur * 240.0 : 0.0,
                                 g_spawnRemap ? T.remap
                                              : std::vector<std::pair<int, int>>{}}};
        stack[0].mover = rootMoves ? T.uid : 0;
        stack[0].root = true;
        stack[0].togHop = g_tapDisarm && T.id == 1049;
        if (g_spawnLag && T.id == 1268) {
            stack[0].fireS = T.sdelay;
            stack[0].ordered = T.sordd != 0;
        }
        int guard = 0;
        while (!stack.empty() && guard++ < 4096) {
            const Item it = stack.back();
            stack.pop_back();
            const auto g = byGroup.find(it.group);
            if (g == byGroup.end()) continue;
            double xFirst = 1e18;
            if (g_spawnLag && it.ordered)
                for (const int ou : g->second) {
                    const auto sx = g_spawnableX.find(ou);
                    if (sx != g_spawnableX.end()) xFirst = std::min(xFirst, sx->second);
                    const auto tr = trig.find(ou);
                    if (tr != trig.end() && tr->second.spawn)
                        xFirst = std::min(xFirst, tr->second.cx);
                }
            for (const int uid : g->second) {
                const auto t2 = trig.find(uid);
                if (t2 != trig.end()) {
                    if (it.togHop && t2->second.spawn) {
                        ++nTogHopSkip;
                        continue;
                    }
                    if (t2->second.target == 0) continue;
                    bool reOn = false;
                    if (it.root && T.id == 1049 && T.togon == 1 && T.touch
                        && !t2->second.spawn && !t2->second.touch && t2->second.id == 901
                        && t2->second.cx > T.cx && objPos) {
                        bool spawnOn = false;
                        if (offAtCrossing(t2->second, trig, byGroup, &spawnOn) && !spawnOn) {
                            const auto mg = byGroup.find(t2->second.target);
                            reOn = mg != byGroup.end() && !mg->second.empty();
                            if (reOn)
                                for (const int ou : mg->second) {
                                    const auto pi = objPos->find(ou);
                                    if (pi != objPos->end() && pi->second.first < t2->second.cx) {
                                        reOn = false;
                                        break;
                                    }
                                }
                            std::printf("triggers: box uid %d switches the Move uid %d (x=%.0f) "
                                        "back on - %s\n", T.uid, t2->second.uid, t2->second.cx,
                                        reOn ? "the box moves its group"
                                             : "left out, its group is not all past its x");
                        }
                    }
                    if (!t2->second.spawn && !reOn) continue;
                    if (g_touchVisual && visualOnly(t2->second.id)) continue;   // --touchvisual
                    // --spawnlag: when this trigger fires (see Item::fireS)
                    const double fireT2 = it.fireS
                        + ((g_spawnLag && it.ordered && xFirst < 1e17)
                               ? std::max(0.0, (t2->second.cx - xFirst) / 311.5801086425781)
                               : 0.0);
                    if (gatesModelled() && itemCompareGate(t2->second) > 0) continue;
                    if (gatesModelled()
                        && ((((t2->second.id == 1611 || t2->second.id == 1811)
                              && t2->second.count >= 0))
                            || t2->second.id == 1595)) {
                        tt.spawnsRoots.push_back(t2->second.uid);
                        continue;
                    }
                    if (t2->second.id == 1616) {
                        const int sg = remapGroup(it.remap, t2->second.target);
                        const auto sgi = byGroup.find(sg);
                        if (sgi != byGroup.end())
                            for (const int su : sgi->second)
                                if (trig.count(su)) tt.stops.push_back(su);
                        continue;
                    }
                    if (objPos && t2->second.id == 901
                        && t2->second.mvtgt == 1) {
                        auto posOfGroup = [&](int grp, double& x, double& y) {
                            const auto gi = byGroup.find(grp);
                            if (gi == byGroup.end()) return false;
                            for (const int ou : gi->second) {
                                const auto pi = objPos->find(ou);
                                if (pi != objPos->end()) {
                                    x = pi->second.first; y = pi->second.second;
                                    return true;
                                }
                            }
                            return false;
                        };
                        double tx = 0, ty = 0, rx = 0, ry = 0;
                        if (posOfGroup(remapGroup(it.remap, t2->second.center), tx, ty)
                            && posOfGroup(remapGroup(it.remap, t2->second.tmodctr),
                                          rx, ry)) {
                            Item nx{remapGroup(it.remap, t2->second.target),
                                    it.dx, it.dy, t2->second.dur * 240.0,
                                    t2->second.ease, t2->second.erate,
                                    it.lock, it.lockY, it.remap};
                            nx.mover = t2->second.uid;
                            nx.tmode = (uint8_t)(1 + std::clamp(t2->second.mvaxis, 0, 2));
                            nx.tdx = (float)(tx - rx);
                            nx.tdy = (float)(ty - ry);
                            nx.fireS = fireT2;   // --spawnlag
                            stack.push_back(std::move(nx));
                            continue;
                        }
                    }
                    if (t2->second.id == 1049 && t2->second.togon >= 0) {
                        const int tg = remapGroup(it.remap, t2->second.target);
                        const auto gi = byGroup.find(tg);
                        const double dl = it.sdelay + t2->second.sdelay * 240.0;
                        if (gi != byGroup.end())
                            for (const int ou : gi->second)
                                if (!trig.count(ou)) {
                                    tt.togEv.push_back({ou,
                                        (uint8_t)(t2->second.togon == 1), dl, tg});
                                    if (t2->second.togon == 1)
                                        tt.togOnUids.push_back(ou);
                                } else if (g_tapDisarm && xCrossingRow(trig.at(ou))) {
                                    tt.trigTog.push_back({ou,
                                        (uint8_t)(t2->second.togon == 1), dl});
                                }
                    }
                    const bool turns = (t2->second.deg != 0.0
                                        || t2->second.t360 != 0);
                    const bool moves = (t2->second.ox != 0.0
                                        || t2->second.oy != 0.0 || turns);
                    const double d2 = t2->second.dur * 240.0;
                    const bool longer = moves && d2 >= it.dur;
                    stack.push_back({remapGroup(it.remap, t2->second.target),
                                     it.dx + (float)t2->second.ox,
                                     it.dy + (float)t2->second.oy,
                                     longer ? d2 : it.dur,
                                     longer ? t2->second.ease : it.ease,
                                     longer ? t2->second.erate : it.erate,
                                     std::max(it.lock, t2->second.lockx
                                         ? t2->second.dur * 240.0 : 0.0),
                                     std::max(it.lockY, t2->second.locky
                                         ? t2->second.dur * 240.0 : 0.0),
                                     g_spawnRemap
                                         ? composeRemap(t2->second.remap, it.remap)
                                         : std::vector<std::pair<int, int>>{}});
                    stack.back().mover = moves ? t2->second.uid : it.mover;
                    stack.back().tmode = it.tmode;
                    stack.back().tdx = it.tdx;
                    stack.back().tdy = it.tdy;
                    // ...and the spawn delays down to here, for togEv only.
                    stack.back().sdelay = it.sdelay + t2->second.sdelay * 240.0;
                    stack.back().togHop = g_tapDisarm && t2->second.id == 1049;
                    if (g_spawnLag) {
                        const bool sp = t2->second.id == 1268;
                        stack.back().fireS = fireT2 + (sp ? t2->second.sdelay : 0.0);
                        stack.back().ordered = sp && t2->second.sordd != 0;
                    }
                } else {
                    const double off = std::max(std::fabs((double)it.dx),
                                                std::fabs((double)it.dy));
                    if (off == 0.0 && it.dur == 0.0 && it.lock == 0.0
                        && it.lockY == 0.0)
                        ++nInert;
                    maxOff = std::max(maxOff, off);
                    maxDur = std::max(maxDur, it.dur);
                    maxLock = std::max(maxLock, std::max(it.lock, it.lockY));
                    tt.ctl.push_back({uid, it.dx, it.dy, it.dur, it.ease,
                                      it.erate, it.lock, it.lockY});
                    tt.ctl.back().mover = it.mover;
                    tt.ctl.back().tmode = it.tmode;
                    tt.ctl.back().tdx = it.tdx;
                    tt.ctl.back().tdy = it.tdy;
                    if (g_spawnLag) tt.ctl.back().spawnLag = spawnLagTicks(it.fireS);
                }
            }
        }
        if (isTap && !(T.thold == 0 && T.ttog == 1)) tt.trigTog.clear();
        const bool trigOff = std::any_of(tt.trigTog.begin(), tt.trigTog.end(),
                                         [](const TouchTrig::TogEv& e) { return e.on == 0; });
        if (isTapDisarm) {
            if (!trigOff) continue;
            tt.ctl.clear();
            tt.togOnUids.clear();
            tt.spawnsRoots.clear();
            tt.stops.clear();
        }
        if ((isCount || isTap || isCmp) && !T.touch && !g_coinUids.empty() && !g_worldGates) {
            for (const TrigCtl& c : tt.ctl)
                if (g_coinUids.count(c.uid)) { tt.reachesCoin = true; break; }
            bool feedsItem = false;
            if (isTap && !tt.reachesCoin)
                for (const TrigCtl& c : tt.ctl)
                    if (g_itemGiver.count(c.uid)) { feedsItem = true; break; }
            if (!tt.reachesCoin && !feedsItem) continue;
            if (feedsItem)
                for (const auto& sv : trig) {
                    const TrigRow& S = sv.second;
                    if (S.id != 1616 || S.spawn || S.cx <= T.cx)
                        continue;
                    const auto sg = byGroup.find(S.target);
                    if (sg == byGroup.end()) continue;
                    bool halts = false;
                    for (const int u : sg->second) {
                        if (u == T.uid) { halts = true; break; }
                        for (const TrigCtl& c : tt.ctl)
                            if (c.uid == u && g_itemGiver.count(u)) { halts = true; break; }
                        if (halts) break;
                    }
                    if (halts && S.cx < tt.tapCloseX) {
                        tt.tapCloseX = S.cx;
                        tt.tapCloseY = S.cy;
                        tt.tapCloseChan = S.chan;
                        tt.tapCloseTouch = S.touch != 0;
                        tt.tapCloseHw = S.w * 0.5;
                        tt.tapCloseHh = S.h * 0.5;
                        tt.tapCloseUid = S.uid;
                    }
                }
            if (isTap) tt.tapChan = T.chan;
        }
        if (g_tapDisarm && (nTogHopSkip || !tt.trigTog.empty())) {
            std::printf("tapdisarm: box uid %d (id %d, x %.0f) switches %zu trigger(s)", tt.uid, tt.id,
                        tt.cx, tt.trigTog.size());
            for (const TouchTrig::TogEv& e : tt.trigTog)
                std::printf(" %d:%s+%.0f", e.uid, e.on ? "on" : "off", e.delay);
            std::printf("; %zu spawn-fired trigger(s) in a Toggle's group no longer followed\n", nTogHopSkip);
        }
        int hard = objTypes ? 0 : -1;
        if (objTypes)
            for (const TrigCtl& c : tt.ctl) {
                const auto t3 = objTypes->find(c.uid);
                if (t3 != objTypes->end() && collidableType(t3->second))
                    ++hard;
            }
        if (g_trigDump && !tt.ctl.empty()) {
            std::printf("trigdump: uid=%d id=%d cx=%.0f cy=%.0f ctl=%zu "
                        "inert=%zu hard=%d maxoff=%.2f maxdur=%.1f "
                        "maxlock=%.1f uids=",
                        tt.uid, tt.id, tt.cx, tt.cy, tt.ctl.size(), nInert,
                        hard, maxOff, maxDur, maxLock);
            for (size_t i = 0; i < tt.ctl.size(); ++i)
                std::printf("%s%d", i ? "," : "", tt.ctl[i].uid);
            std::printf("\n");
        }
        if (objTypes && !tt.ctl.empty() && !isCount && !isTap && !isCmp && !isActivator) {
            const bool moves = nInert < tt.ctl.size();
            if (!moves && hard <= 0 && tt.togOn < 0 && !trigOff) {
                ++nIrrelevant;
                continue;
            }
        }
        if (g_worldGates && !g_coinRoute && (isCount || isTap || isCmp) && objTypes
            && !tt.ctl.empty() && hard <= 0 && tt.togOn < 0 && !trigOff) {
            ++nGateCosmetic;
            continue;
        }
        if (!tt.ctl.empty() || trigOff || !armUids.empty()) out.push_back(std::move(tt));
    }
    if (nGateCosmetic)
        std::printf("triggers: --worldgates left out %zu gate(s) whose chain reaches nothing "
                    "collidable and toggles nothing\n", nGateCosmetic);
    if (objTypes)
        std::printf("triggers: --trigrelevant dropped %zu box(es) that move "
                    "nothing, reach nothing collidable and toggle nothing; "
                    "%zu left to choose from\n", nIrrelevant, out.size());
    g_trigTotal = out.size() + nIrrelevant;
    g_trigRelevantN = out.size();
    g_trigDroppedBehind = 0;
    g_trigDroppedAhead = 0;
    g_trigFirstAheadX = 1e300;
    std::sort(out.begin(), out.end(),
              [](const TouchTrig& a, const TouchTrig& b) { return a.cx < b.cx; });
    std::vector<int> wantedItems;
    for (const TouchTrig& t : out)
        if (t.count >= 0 && t.item != 0) {
            bool reachesCoin = false;
            for (const TrigCtl& c : t.ctl)
                if (g_coinUids.count(c.uid)) { reachesCoin = true; break; }
            if (reachesCoin) wantedItems.push_back(t.item);
        }
    auto feedsACoin = [&](const TouchTrig& t) {
        if (wantedItems.empty() || g_itemGiver.empty()) return false;
        for (const TrigCtl& c : t.ctl) {
            auto it = g_itemGiver.find(c.uid);
            if (it == g_itemGiver.end()) continue;
            for (int w : wantedItems) if (w == it->second) return true;
        }
        return false;
    };
    if (!g_worldGates)
        out.erase(std::remove_if(out.begin(), out.end(),
                                 [&](const TouchTrig& t) {
                                     // (--tapdisarm: not one kept for the triggers it switches off)
                                     const bool off = std::any_of(
                                         t.trigTog.begin(), t.trigTog.end(),
                                         [](const TouchTrig::TogEv& e) { return e.on == 0; });
                                     return t.tap && !t.reachesCoin && !feedsACoin(t) && !off;
                                 }),
                  out.end());
    std::vector<TouchTrig> counts;
    if ((int)out.size() > kTouchBits) {
        std::vector<TouchTrig> boxes;
        for (TouchTrig& t : out) {
            if (t.count >= 0 || t.tap || feedsACoin(t))
                counts.push_back(std::move(t));
            else boxes.push_back(std::move(t));
        }
        out.swap(boxes);
    }
    if ((int)(out.size() + counts.size()) > kTouchBits) {
        const size_t keep = (size_t)kTouchBits
                          - std::min<size_t>(counts.size(), (size_t)kTouchBits - 1);
        size_t first = 0;
        if (g_trigWinNear > -1e17) {
            double best = 1e300;
            for (size_t f = 0; f + (size_t)kTouchBits <= out.size(); ++f) {
                const double reach = std::max(
                    std::fabs(out[f].cx - g_trigWinNear),
                    std::fabs(out[f + (size_t)kTouchBits - 1].cx - g_trigWinNear));
                if (reach < best) { best = reach; first = f; }
            }
            std::printf("triggers: the rotated-frame window keeps the %d boxes nearest world "
                        "x=%.0f (within %.0f px)\n", kTouchBits, g_trigWinNear, best);
        } else if (fromX > -1e17) {
            const double lo = fromX - 200.0;
            while (first < out.size() && out[first].cx < lo) ++first;
            if (g_touchWinAhead && first > 0 && first < out.size()) {
                const size_t was = first;
                while (first > 0) {
                    std::unordered_set<int> moved;
                    for (const TrigCtl& c : out[first].ctl) moved.insert(c.uid);
                    bool shares = false;
                    for (const TrigCtl& c : out[first - 1].ctl)
                        if (moved.count(c.uid)) { shares = true; break; }
                    if (!shares) break;
                    --first;
                }
                if (first != was)
                    std::printf("triggers: --touchwinahead starts the window %zu box(es) back, at "
                                "x=%.0f, so it does not cut a run that moves the same objects\n",
                                was - first, out[first].cx);
            }
            if (out.size() - first < keep) first = out.size() - keep;
        }
        const size_t total = out.size();
        if (first + keep < total) g_trigFirstAheadX = out[first + keep].cx;
        out = std::vector<TouchTrig>(out.begin() + (long long)first,
                                     out.begin() + (long long)(first + keep));
        // The two sides, published and not only printed -- see the globals.
        g_trigDroppedBehind = first;
        g_trigDroppedAhead = total - first - keep;
        std::printf("triggers: %zu touch triggers, keeping %zu from x=%.0f "
                    "(dropped %zu behind, %zu ahead)\n",
                    total, keep, out.front().cx,
                    g_trigDroppedBehind, g_trigDroppedAhead);
    }
    // ...and back in, at the end, so the boxes keep the numbering they had.
    for (TouchTrig& t : counts) out.push_back(std::move(t));
    g_trigKept = out.size();
    g_trigDroppedRelevant = g_trigRelevantN - g_trigKept;
    g_trigMaxKeptX = out.empty() ? 0.0 : out.back().cx;
    std::printf("triggers: coverage total=%zu relevant=%zu kept=%zu "
                "droppedRelevant=%zu droppedBehind=%zu droppedAhead=%zu "
                "maxKeptX=%.0f%s\n",
                g_trigTotal, g_trigRelevantN, g_trigKept,
                g_trigDroppedRelevant, g_trigDroppedBehind, g_trigDroppedAhead,
                g_trigMaxKeptX,
                g_trigDroppedAhead ? "  UNCOVERED"
                                   : (g_trigDroppedBehind ? "  behind-only" : ""));
    for (size_t b = 0; b < out.size(); ++b) {
        if (out[b].count >= 0 && out[b].cmode == 3)
            std::printf("triggers: bit %zu uid %d ITEM COMPARE item %d at least %d,"
                        " moves %zu objects\n",
                        b, out[b].uid, out[b].item, out[b].count, out[b].ctl.size());
        else if (out[b].count >= 0)
            std::printf("triggers: bit %zu uid %d COUNT item %d %s %d, moves %zu objects\n",
                        b, out[b].uid, out[b].item,
                        out[b].cmode == 1 ? ">=" : out[b].cmode == 2 ? "<=" : "==",
                        out[b].count, out[b].ctl.size());
        else if (out[b].tap && out[b].tapCloseX < 1e17)
            std::printf("triggers: box %zu uid %d TAP at x=%.0f chan %d, counts presses"
                        " until the Stop at x=%.0f chan %d, moves %zu objects\n",
                        b, out[b].uid, out[b].cx, out[b].tapChan, out[b].tapCloseX,
                        out[b].tapCloseChan, out[b].ctl.size());
        else
            std::printf("triggers: box %zu uid %d (%.0f,%.0f) %.0fx%.0f moves %zu objects\n",
                        b, out[b].uid, out[b].cx, out[b].cy, out[b].hw * 2,
                        out[b].hh * 2, out[b].ctl.size());
    }
    // Each spawned root is armed by the box(es) whose chain spawns it.
    for (size_t b = 0; b < out.size() && b < (size_t)kTouchBits; ++b)
            for (const int u : out[b].spawnsRoots)
                for (size_t r = 0; r < out.size() && r < (size_t)kTouchBits; ++r)
                    if (out[r].uid == u) {
                        out[r].armBy |= touchBit((int)b);
                        std::printf("triggers: bit %zu uid %d is armed by box %zu uid %d "
                                    "(spawned by its chain)\n", r, u, b, out[b].uid);
                    }
    // the ownTouch proximity gate's table (see g_touchBoxU)
    for (size_t b = 0; b < (size_t)kTouchBits && b < out.size(); ++b)
        g_touchBoxU[b] = (float)out[b].cx;
    return out;
}

inline std::vector<AutoTrig> loadAutoTriggers(const std::string& trigPath,
                                              const std::string& grpPath) {
    std::vector<AutoTrig> out;
    const std::unordered_map<int, TrigRow>* trigP = trigRowsCached(trigPath);
    if (!trigP) return out;
    const std::unordered_map<int, TrigRow>& trig = *trigP;   // uid -> row
    const std::unordered_map<int, std::vector<int>>* membersP = groupMembersCached(grpPath);
    if (!membersP) return out;
    const std::unordered_map<int, std::vector<int>>& byGroup = *membersP;   // read only here
    for (const auto& kv : trig) {
        const TrigRow& T = kv.second;
        if (T.touch || T.spawn || T.target == 0) continue;
        if (T.id != 901 && T.id != 1049) continue;
        bool offInit = false;
        if (T.id == 901) {
            bool spawnOn = false;
            if (offAtCrossing(T, trig, byGroup, &spawnOn) && !spawnOn) {
                if (g_boxArm && boxArmsTrigger(T.uid, trig, byGroup)) {
                    offInit = true;
                    std::printf("autotrig: uid %d (move, x=%.0f) is switched off when the player reaches it "
                                "- kept, a toggle block switches it back on (--boxarm)\n", T.uid, T.cx);
                } else {
                    std::printf("autotrig: uid %d (move, x=%.0f) is switched off when the player "
                                "reaches it - it moves nothing on its own\n", T.uid, T.cx);
                    continue;
                }
            }
        }
        AutoTrig at;
        at.offInit = offInit ? 1 : 0;
        at.uid = T.uid;
        at.cx = T.cx;
        at.delay = (T.id == 1049) ? 0 : 1;
        at.toggleRoot = (T.id == 1049) ? 1 : 0;
        // The root's own switch, when the root is a Toggle (see AutoTrig::tog).
        if (T.id == 1049 && T.togon >= 0) {
            const auto gi = byGroup.find(T.target);
            if (gi != byGroup.end())
                for (const int ou : gi->second)
                    if (!trig.count(ou)) at.tog.push_back({ou, (uint8_t)(T.togon == 1), 0.0, T.target});
        }
        struct Item {
            int group; float dx, dy; double dur; int ease; double erate;
            double lock, lockY;
            std::vector<std::pair<int, int>> remap;   // as in the touch walk
            double sdelay = 0.0;   // the spawn delays so far, in ticks (AutoTrig::tog only)
        };
        const bool rootMoves = (T.ox != 0.0 || T.oy != 0.0);
        std::vector<Item> stack{{T.target, (float)T.ox, (float)T.oy,
                                 rootMoves ? T.dur * 240.0 : 0.0,
                                 rootMoves ? T.ease : 0,
                                 rootMoves ? T.erate : 2.0,
                                 T.lockx ? T.dur * 240.0 : 0.0,
                                 T.locky ? T.dur * 240.0 : 0.0}};
        int guard = 0;
        while (!stack.empty() && guard++ < 4096) {
            const Item it = stack.back();
            stack.pop_back();
            const auto g = byGroup.find(it.group);
            if (g == byGroup.end()) continue;
            for (const int uid : g->second) {
                const auto t2 = trig.find(uid);
                if (t2 != trig.end()) {
                    if (t2->second.target == 0) continue;
                    if (!t2->second.spawn) continue;
                    if (T.id == 901) continue;
                    if (g_touchVisual && visualOnly(t2->second.id)) continue;   // --touchvisual
                    const double dl = it.sdelay + t2->second.sdelay * 240.0;
                    // A Toggle spawned in the chain: its switch, after the delays so far.
                    if (t2->second.id == 1049 && t2->second.togon >= 0) {
                        const int tg = remapGroup(it.remap, t2->second.target);
                        const auto gi = byGroup.find(tg);
                        if (gi != byGroup.end())
                            for (const int ou : gi->second)
                                if (!trig.count(ou))
                                    at.tog.push_back({ou, (uint8_t)(t2->second.togon == 1), dl, tg});
                    }
                    const bool moves = (t2->second.ox != 0.0 || t2->second.oy != 0.0);
                    const double d2 = t2->second.dur * 240.0;
                    const bool longer = moves && d2 >= it.dur;
                    stack.push_back({remapGroup(it.remap, t2->second.target),
                                     it.dx + (float)t2->second.ox,
                                     it.dy + (float)t2->second.oy,
                                     longer ? d2 : it.dur,
                                     longer ? t2->second.ease : it.ease,
                                     longer ? t2->second.erate : it.erate,
                                     std::max(it.lock, t2->second.lockx
                                         ? t2->second.dur * 240.0 : 0.0),
                                     std::max(it.lockY, t2->second.locky
                                         ? t2->second.dur * 240.0 : 0.0),
                                     g_spawnRemap
                                         ? composeRemap(t2->second.remap, it.remap)
                                         : std::vector<std::pair<int, int>>{},
                                     dl});
                } else {
                    at.ctl.push_back({uid, it.dx, it.dy, it.dur, it.ease,
                                      it.erate, it.lock, it.lockY});
                }
            }
        }
        bool keepForTouch = false;
        if (g_touchOwnOff && at.ctl.empty() && T.id == 1049 && T.togon >= 0) {
            const auto gi = byGroup.find(T.target);
            if (gi != byGroup.end())
                for (const int ou : gi->second) {
                    const auto t2 = trig.find(ou);
                    if (t2 != trig.end() && t2->second.touch) {
                        keepForTouch = true;
                        break;
                    }
                }
        }
        if (!at.ctl.empty() || keepForTouch) out.push_back(std::move(at));
    }
    g_rotated.clear();
    g_rotSpec.clear();
    g_rotTailParts.clear();
    for (const auto& kv : trig) {
        const TrigRow& T = kv.second;
        if (T.id != 1346 || T.target == 0) continue;
        AutoTrig rot;
        rot.uid = T.uid;
        rot.cx = T.cx;
        rot.delay = 1;            // measured, see above
        int centre = 0;
        if (T.center != 0) {
            const auto cg = byGroup.find(T.center);
            if (cg != byGroup.end() && cg->second.size() == 1)
                centre = cg->second.front();
        }
        RotSpec spec;
        spec.trigUid = T.uid;
        spec.centreUid = centre;
        spec.total = (double)T.t360 * 360.0 + T.deg;
        spec.durT = T.dur * 240.0;
        spec.ease = T.ease;
        spec.erate = T.erate;
        spec.lockrot = T.lockrot;
        rot.rotTotal = spec.total;
        rot.rotDurT = spec.durT;
        rot.rotEase = spec.ease;
        rot.rotErate = spec.erate;
        rot.rotLock = T.lockrot ? 1 : 0;
        rot.rotAngleOk = T.hasAngle ? 1 : 0;
        const bool usable = T.hasAngle && centre != 0 && spec.durT > 0.0;
        const bool autonomous = (!T.touch && !T.spawn);
        std::vector<int> stack{T.target};
        int guard = 0;
        while (!stack.empty() && guard++ < 4096) {
            const int g = stack.back();
            stack.pop_back();
            const auto it = byGroup.find(g);
            if (it == byGroup.end()) continue;
            for (const int uid : it->second) {
                const auto t2 = trig.find(uid);
                if (t2 != trig.end() && t2->second.target != 0) {
                    // --rotnospawn: the Rotate turns this trigger and fires nothing (dynamics.hpp)
                    if (!g_rotNoSpawn) stack.push_back(t2->second.target);
                } else {
                    g_rotated.insert(uid);
                    if (usable && uid != centre) g_rotSpec[uid] = spec;
                    if (autonomous && T.hasAngle && uid != centre && (T.center == 0 || centre != 0))
                        g_rotTailParts[uid].push_back({T.uid, centre, spec.total, spec.durT, spec.erate,
                                                      spec.ease, (uint8_t)(T.lockrot ? 1 : 0), T.cx});
                    if (autonomous) {
                        TrigCtl c{};
                        c.uid = uid;
                        c.orbit = (T.center != 0 && uid != centre) ? 1 : 0;   // any centre group, the target's own included
                        rot.ctl.push_back(c);
                    }
                }
            }
        }
        if (autonomous && !rot.ctl.empty()) out.push_back(std::move(rot));
    }
    for (auto& kv : g_rotTailParts)
        std::stable_sort(kv.second.begin(), kv.second.end(),
                         [](const RotTailPart& a, const RotTailPart& b) {
                             return a.trigCx != b.trigCx ? a.trigCx < b.trigCx : a.trigUid < b.trigUid;
                         });
    if (g_autoStop)
        for (const auto& kv : trig) {
            const TrigRow& T = kv.second;
            if (T.id != 1616 || T.touch || T.spawn || T.target == 0) continue;
            const auto gi = byGroup.find(T.target);
            if (gi == byGroup.end()) continue;
            AutoTrig st;
            st.uid = T.uid;
            st.cx = T.cx;
            st.delay = 0;
            for (const int u : gi->second) {
                const auto t2 = trig.find(u);
                if (t2 != trig.end() && t2->second.id == 901) st.stopMoves.push_back(u);
            }
            std::printf("autostop: Stop uid %d at x=%.0f halts %zu Move(s) of group %d\n", T.uid,
                        T.cx, st.stopMoves.size(), T.target);
            if (!st.stopMoves.empty()) out.push_back(std::move(st));
        }
    std::sort(out.begin(), out.end(),
              [](const AutoTrig& a, const AutoTrig& b) { return a.cx < b.cx; });
    if (!out.empty())
        std::printf("autotrig: %zu autonomous moves (first x=%.0f, last x=%.0f),"
                    " %zu uids turned by a rotate\n",
                    out.size(), out.front().cx, out.back().cx, g_rotated.size());
    if (!g_rotated.empty())
        std::printf("rotspec: %zu of %zu turned uids have a computable orbit "
                    "(centre + whole angle)\n", g_rotSpec.size(),
                    g_rotated.size());
    if (g_stopDump) {
        size_t n = 0;
        for (const auto& kv : trig) {
            const TrigRow& T = kv.second;
            if (T.id != 1616 || T.target == 0) continue;
            ++n;
            const char* how = T.touch ? "touch" : (T.spawn ? "SPAWN (not a root here)"
                                                           : "x-crossing");
            std::printf("stopdump: uid=%d at x=%.0f target=%d fired-by=%s\n",
                        T.uid, T.cx, T.target, how);
            const auto g = byGroup.find(T.target);
            if (g == byGroup.end()) { std::printf("stopdump:   (empty group)\n"); continue; }
            for (const int uid : g->second) {
                const auto t2 = trig.find(uid);
                if (t2 == trig.end()) continue;
                const TrigRow& S = t2->second;
                std::printf("stopdump:   stops uid=%d id=%d -> group %d "
                            "dur=%.3f ox=%.1f oy=%.1f fired-by=%s\n",
                            S.uid, S.id, S.target, S.dur, S.ox, S.oy,
                            S.touch ? "touch" : (S.spawn ? "spawn"
                                                         : "x-crossing"));
            }
        }
        std::printf("stopdump: %zu stop triggers in this level\n", n);
    }
    return out;
}
}  // namespace dp
