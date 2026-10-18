#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace levelwarn {
enum : uint32_t {
    kSidewaysTeleport = 1u << 0,
    kOrbPush = 1u << 1,
    kTeleportSeveralExits = 1u << 2,
    kTeleportTrigger = 1u << 3,
    kGravityTrigger = 1u << 4,
    kGravityPortals = 1u << 5,
    kAttemptDependent = 1u << 6,
    kPlatformer = 1u << 7,
    kTwoPlayer = 1u << 8,
    kFollowNotRun = 1u << 9,
    kFollowSlots = 1u << 10,
};

// The phrase each bit is shown as, in bit order.
inline const char* const kUnmodelledNames[] = {
    "teleports that move you sideways",
    "teleport orbs that push you",
    "teleports with several exits",
    "teleport triggers set off by touch or a spawn, or keeping an offset",
    "gravity triggers set off by touch or a spawn, or for player 2",
    "too many gravity portals",
    "triggers that count attempts",
    "platformer mode",
    "two-player mode",
    "delayed, touch- or spawn-started follow triggers",
    "more than two follow triggers at once",
};

inline constexpr int kNewer[] = {
    371, 372, 709, 745, 747, 901, 1022, 1049, 1268, 1330, 1331, 1332,
    1333, 1338, 1339, 1341, 1342, 1344, 1345, 1346, 1347, 1594, 1595, 1611, 1616, 1704, 1717, 1718,
    1723, 1724, 1743, 1744, 1745, 1746, 1747, 1748, 1749, 1750, 1751, 1755, 1811, 1812, 1813, 1814,
    1815, 1816, 1817, 1829, 1859, 1906, 1907, 1912, 1913, 1914, 1916, 1917, 1931, 1932, 1933, 1935,
    2015, 2062, 2066, 2067, 2068, 2069, 2866, 2899, 2900, 2901, 2902, 2925, 2926, 3004, 3005, 3006,
    3007, 3008, 3011, 3012, 3013, 3016, 3017, 3018, 3019, 3022, 3023, 3024, 3027, 3033, 3600, 3604,
    3607, 3609, 3614, 3615, 3617, 3618, 3619, 3620, 3640, 3641, 3643, 3645, 3655, 3660, 3661};

inline bool isNewer(int id) {
    return std::binary_search(std::begin(kNewer), std::end(kNewer), id);
}

// one row of the menu's Level check (src/kairos/LevelCheckPopup), most severe first.
enum class Severity { Bad, Caution, Info, Fine };
struct Note {
    Severity severity;
    std::string title;    // a few words: the menu's status line shows it
    std::string detail;   // where and why
};

struct Findings {
    uint32_t unmodelled = 0;   // the bits above
    bool newer = false;        // an object in kNewer
    std::unordered_map<int, int> newerCount;   // how many of each kNewer object, for the log
    std::vector<Note> notes;   // the Follow Player Y found (the other bits: bitNotes)
};

inline const char* const kUnmodelledTitles[] = {
    "Sideways teleports", "Pushing teleport orbs", "Teleports with several exits",
    "Touch or spawn teleport triggers", "Touch or spawn gravity triggers", "Too many gravity portals",
    "Attempt-counting triggers", "Platformer mode", "Two-player mode", "Follow Player Y",
    "Too many Follow Player Y",
};
inline const char* const kUnmodelledDetails[] = {
    "x is the solver's clock: an exit elsewhere in x is a jump it cannot take.",
    "They set the player's speed, which the solver reads on portals only.",
    "The game picks one exit at random; the solver takes the first.",
    "Set off by touch or by a spawn, or keeping the player's offset.",
    "Set off by touch or by a spawn, or acting on player 2.",
    "More than the 128 the solver can tell apart.",
    "The level changes with the attempt count.",
    "Platformer levels are not solved.",
    "Only player 1's button is pressed.",
    "",
    "More than two running at once.",
};
inline std::vector<Note> bitNotes(uint32_t bits) {
    std::vector<Note> v;
    for (size_t i = 0; i < sizeof(kUnmodelledTitles) / sizeof(kUnmodelledTitles[0]); ++i)
        if ((bits & (1u << i)) && (1u << i) != kFollowNotRun)
            v.push_back({Severity::Bad, kUnmodelledTitles[i], kUnmodelledDetails[i]});
    return v;
}

// The key's value in one object's "k,v,k,v" run, or empty.
inline std::string_view field(std::string_view kv, std::string_view key) {
    size_t p = 0;
    while (p < kv.size()) {
        const size_t c1 = kv.find(',', p);
        if (c1 == std::string_view::npos) break;
        const size_t c2 = kv.find(',', c1 + 1);
        const size_t e = (c2 == std::string_view::npos) ? kv.size() : c2;
        if (kv.substr(p, c1 - p) == key) return kv.substr(c1 + 1, e - c1 - 1);
        if (c2 == std::string_view::npos) break;
        p = c2 + 1;
    }
    return {};
}

inline bool isOn(std::string_view v) { return !v.empty() && v != "0"; }
inline int toInt(std::string_view v) { return v.empty() ? 0 : std::atoi(std::string(v).c_str()); }
inline double toNum(std::string_view v) { return v.empty() ? 0.0 : std::atof(std::string(v).c_str()); }

template <class F>
inline void forEachObject(std::string_view all, F&& f) {
    size_t pos = all.find(';');
    while (pos != std::string_view::npos) {
        const size_t end = all.find(';', pos + 1);
        const auto obj = all.substr(pos + 1, end == std::string_view::npos
                                                 ? std::string_view::npos : end - pos - 1);
        if (!obj.empty()) f(obj);
        pos = end;
    }
}

// A gravity portal as the dp loader sees it: its x and the half width of its box.
struct GravPortal {
    double x;
    double hw;
};

inline double gravPortalHalfWidth(std::string_view obj, int id) {
    auto scale = [&](std::string_view axisKey) {
        double s = 0.0;
        bool given = false;
        for (std::string_view k : {std::string_view("32"), axisKey}) {
            const auto v = field(obj, k);
            if (v.empty()) continue;
            s = std::max(s, std::fabs(toNum(v)));
            given = true;
        }
        return given ? s : 1.0;
    };
    const bool toggle = id == 2926;
    const double w = (toggle ? 23.75 : 25.0) * scale("128"), h = (toggle ? 75.5 : 75.0) * scale("129");
    double c = 0.0, s = 0.0;
    bool turned = false;
    for (std::string_view k : {"6", "131", "132"}) {
        const auto v = field(obj, k);
        if (v.empty()) continue;
        const double a = toNum(v) * 3.14159265358979 / 180.0;
        c = std::max(c, std::fabs(std::cos(a)));
        s = std::max(s, std::fabs(std::sin(a)));
        turned = true;
    }
    if (!turned) c = 1.0;
    return 0.5 * (w * c + h * s);
}

inline bool gravityPortalsRefused(std::vector<GravPortal> g, bool reversal) {
    constexpr size_t kBits = 128;       // dp: kGravPortalBits
    constexpr double kGap = 120.0;      // dp: kGpShareGap
    constexpr double kSlack = 2.0;
    if (g.size() <= kBits) return false;
    if (reversal) return true;
    std::sort(g.begin(), g.end(), [](const GravPortal& a, const GravPortal& b) { return a.x < b.x; });
    const size_t n = g.size();
    auto firstAt = [&](double x) {   // the first portal whose x is at least `x`
        return (size_t)(std::lower_bound(g.begin(), g.end(), x,
                                         [](const GravPortal& p, double v) { return p.x < v; })
                        - g.begin());
    };
    // hiMin[k]: the leftmost box start among portals k.. in x order.
    std::vector<double> hiMin(n + 1, std::numeric_limits<double>::infinity());
    for (size_t k = n; k-- > 0;) hiMin[k] = std::min(hiMin[k + 1], g[k].x - g[k].hw);
    for (const GravPortal& a : g) {
        const size_t lo = firstAt(a.x - kSlack);
        if (lo + kBits >= n) break;
        const size_t from = firstAt(g[lo + kBits].x - kSlack);
        if (hiMin[from] - (a.x + a.hw) < kGap + kSlack) return true;
    }
    return false;
}

inline double speedDx(int id) {
    switch (id) {
        case 200: return 251.16 / 240.0;
        case 202: return 1.6142578;
        case 203: return 468.0 / 240.0;
        case 1334: return 576.0 / 240.0;
        default: return 1.29825044;
    }
}

struct FollowRun {
    double x;
    double dur;   // seconds
};
inline bool followSlotsShort(std::vector<FollowRun> runs, double dxMax) {
    constexpr int kSlots = 2;   // dp: kFollowYSlots
    std::sort(runs.begin(), runs.end(),
              [](const FollowRun& a, const FollowRun& b) { return a.x < b.x; });
    double freeAt[kSlots];
    for (double& v : freeAt) v = -std::numeric_limits<double>::infinity();
    for (const FollowRun& r : runs) {
        int slot = -1;
        for (int s = 0; s < kSlots; ++s)
            if (freeAt[s] < r.x && (slot < 0 || freeAt[s] < freeAt[slot])) slot = s;
        if (slot < 0) return true;
        freeAt[slot] = r.x + r.dur * 240.0 * dxMax;
    }
    return false;
}

inline Findings scan(std::string_view all, bool platformer, const std::function<bool(int)>& collides = {}) {
    Findings r;
    if (platformer) r.unmodelled |= kPlatformer;
    const std::string_view head = all.substr(0, all.find(';'));
    if (field(head, "kA10") == "1") r.unmodelled |= kTwoPlayer;
    struct Portal { double x; int group; bool keepX; };
    std::vector<Portal> portals;
    std::vector<GravPortal> gravity;
    bool reversal = false;
    std::vector<FollowRun> follows;
    // every 1814 with a target, decided after the pass by what its group moves.
    struct Follow {
        double x;
        int group;
        std::string why;   // empty: one the model runs (x-started, no delay)
        bool runs;         // a speed above 0
        double dur;
    };
    std::vector<Follow> f1814;
    double dxMax = speedDx(201);
    {
        static constexpr int kStartSpeed[] = {201, 200, 202, 203, 1334};   // header kA4 0..4
        const int s = toInt(field(head, "kA4"));
        if (s >= 0 && s < 5) dxMax = std::max(dxMax, speedDx(kStartSpeed[s]));
    }
    forEachObject(all, [&](std::string_view obj) {
        const int id = toInt(field(obj, "1"));
        if (isNewer(id)) {
            r.newer = true;
            ++r.newerCount[id];
        }
        if (!reversal && isOn(field(obj, "117")) && !isOn(field(obj, "121"))) reversal = true;
        switch (id) {
            case 10: case 11: case 2926:
                if (!isOn(field(obj, "121")))
                    gravity.push_back({toNum(field(obj, "2")), gravPortalHalfWidth(obj, id)});
                break;
            case 747: case 2902:
                // a force (345 / 347) and ignoreY (353) are modelled (dp --tppush, --tpkeepy)
                if (id == 2902) {
                    const int g = toInt(field(obj, "51"));
                    if (g > 0) portals.push_back({toNum(field(obj, "2")), g, isOn(field(obj, "352"))});
                }
                break;
            case 3027: {
                if (isOn(field(obj, "345")) || isOn(field(obj, "347"))) r.unmodelled |= kOrbPush;
                const int g = toInt(field(obj, "51"));
                if (g > 0) portals.push_back({toNum(field(obj, "2")), g, true});
                break;
            }
            case 3022: {
                if (isOn(field(obj, "11")) || isOn(field(obj, "62")) || isOn(field(obj, "351")))
                    r.unmodelled |= kTeleportTrigger;
                const int g = toInt(field(obj, "51"));
                if (g > 0) portals.push_back({toNum(field(obj, "2")), g, isOn(field(obj, "352"))});
                break;
            }
            case 200: case 201: case 202: case 203: case 1334:
                dxMax = std::max(dxMax, speedDx(id));
                break;
            case 1814: {
                const int g = toInt(field(obj, "51"));
                if (g <= 0) break;
                std::string why;
                if (isOn(field(obj, "91"))) {
                    char b[48];
                    std::snprintf(b, sizeof b, "delayed %.2f s", toNum(field(obj, "91")));
                    why = b;
                } else if (isOn(field(obj, "11"))) {
                    why = "started by touch";
                } else if (isOn(field(obj, "62"))) {
                    why = "started by a spawn";
                }
                const auto speed = field(obj, "90");
                const auto dur = field(obj, "10");
                f1814.push_back({toNum(field(obj, "2")), g, why, speed.empty() || toNum(speed) > 0.0,
                                 dur.empty() ? std::numeric_limits<double>::infinity() : toNum(dur)});
                break;
            }
            case 2066:
                if (isOn(field(obj, "11")) || isOn(field(obj, "62")) || isOn(field(obj, "200")))
                    r.unmodelled |= kGravityTrigger;
                break;
            case 3641: r.unmodelled |= kAttemptDependent; break;
            case 3619: case 3620:
                if (field(obj, "476") == "5" || field(obj, "477") == "5")
                    r.unmodelled |= kAttemptDependent;
                break;
            default: break;
        }
    });
    if (!f1814.empty()) {
        std::unordered_map<int, std::vector<int>> carries;   // group -> the groups it moves in turn
        std::unordered_set<int> touchable;                   // groups holding something that collides
        auto reachFrom = [&](int group) {
            std::vector<int> todo{group};
            std::unordered_set<int> seen{group};
            while (!todo.empty()) {
                const int g = todo.back();
                todo.pop_back();
                if (auto it = carries.find(g); it != carries.end())
                    for (int n : it->second)
                        if (seen.insert(n).second) todo.push_back(n);
            }
            return seen;
        };
        if (collides) {
            forEachObject(all, [&](std::string_view obj) {
                const int ref = toInt(field(obj, "71")), tgt = toInt(field(obj, "51"));
                if (ref > 0 && tgt > 0 && ref != tgt) carries[ref].push_back(tgt);
            });
            std::unordered_set<int> reach;
            for (const auto& f : f1814)
                for (int g : reachFrom(f.group)) reach.insert(g);
            forEachObject(all, [&](std::string_view obj) {
                const auto groups = field(obj, "57");
                if (groups.empty() || isOn(field(obj, "121"))) return;
                bool asked = false, hits = false;
                size_t p = 0;
                while (p <= groups.size()) {
                    const size_t d = groups.find('.', p);
                    const int g = toInt(groups.substr(p, d == std::string_view::npos ? std::string_view::npos : d - p));
                    if (reach.count(g) && !touchable.count(g)) {
                        if (!asked) {
                            hits = collides(toInt(field(obj, "1")));
                            asked = true;
                        }
                        if (hits) touchable.insert(g);
                    }
                    if (d == std::string_view::npos) break;
                    p = d + 1;
                }
            });
        }
        auto matters = [&](int group) {
            if (!collides) return true;
            for (int g : reachFrom(group))
                if (touchable.count(g)) return true;
            return false;
        };
        for (const auto& f : f1814) {
            const bool m = matters(f.group);
            if (!f.why.empty()) {
                char where[64];
                std::snprintf(where, sizeof where, "x %.0f, group %d: ", f.x, f.group);
                if (m) {
                    r.unmodelled |= kFollowNotRun;
                    r.notes.push_back({Severity::Bad, "Follow Player Y (" + f.why + ")",
                                       where + f.why + ". The solver cannot run it, and it moves "
                                                       "something the player can touch."});
                } else {
                    r.notes.push_back({Severity::Fine, "Follow Player Y (" + f.why + ")",
                                       where + f.why + ", but it moves nothing the player can "
                                                       "touch: no effect on the run."});
                }
            } else if (f.runs && m) {
                follows.push_back({f.x, f.dur});
            }
        }
    }
    if (gravityPortalsRefused(std::move(gravity), reversal)) r.unmodelled |= kGravityPortals;
    if (followSlotsShort(std::move(follows), dxMax)) r.unmodelled |= kFollowSlots;
    if (portals.empty()) return r;
    // The exits: every object in a portal's target group (key 57, '.'-separated).
    struct Exits { int n = 0; double x = 0.0; };
    std::unordered_map<int, Exits> exits;
    for (const auto& p : portals) exits.emplace(p.group, Exits{});
    forEachObject(all, [&](std::string_view obj) {
        const auto groups = field(obj, "57");
        size_t p = 0;
        while (!groups.empty() && p <= groups.size()) {
            const size_t d = groups.find('.', p);
            const auto one = groups.substr(p, d == std::string_view::npos ? std::string_view::npos
                                                                          : d - p);
            if (auto it = exits.find(toInt(one)); it != exits.end() && it->second.n++ == 0)
                it->second.x = toNum(field(obj, "2"));
            if (d == std::string_view::npos) break;
            p = d + 1;
        }
    });
    for (const auto& p : portals) {
        const Exits& e = exits[p.group];
        if (e.n > 1) r.unmodelled |= kTeleportSeveralExits;
        else if (e.n == 1 && !p.keepX && std::fabs(e.x - p.x) > 0.5) r.unmodelled |= kSidewaysTeleport;
    }
    return r;
}

// Every bit set, "; "-separated, or "none" -- for the log line the menu writes.
inline std::string describeAll(uint32_t bits) {
    std::string s;
    for (size_t i = 0; i < sizeof(kUnmodelledNames) / sizeof(kUnmodelledNames[0]); ++i)
        if (bits & (1u << i)) s += (s.empty() ? "" : "; ") + std::string(kUnmodelledNames[i]);
    return s.empty() ? "none" : s;
}

inline std::string newerSummary(const Findings& f, size_t most = 12) {
    std::vector<std::pair<int, int>> v(f.newerCount.begin(), f.newerCount.end());
    std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::string s;
    for (size_t i = 0; i < v.size() && i < most; ++i)
        s += (i ? ", " : "") + std::to_string(v[i].first) + " x" + std::to_string(v[i].second);
    if (v.size() > most) s += ", +" + std::to_string(v.size() - most) + " more kinds";
    return s.empty() ? "none" : s;
}

// "a", "a and b", or "a, b and N more" for the bits set, in bit order: the menu gives it one line.
inline std::string describe(uint32_t bits) {
    std::vector<const char*> names;
    for (size_t i = 0; i < sizeof(kUnmodelledNames) / sizeof(kUnmodelledNames[0]); ++i)
        if (bits & (1u << i)) names.push_back(kUnmodelledNames[i]);
    std::string s;
    const size_t shown = names.size() > 2 ? 2 : names.size();
    for (size_t i = 0; i < shown; ++i) {
        if (i) s += (i + 1 == names.size()) ? " and " : ", ";
        s += names[i];
    }
    if (names.size() > shown) s += " and " + std::to_string(names.size() - shown) + " more";
    return s;
}
}  // namespace levelwarn
