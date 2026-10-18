#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace grouptrace {
// cfg gtrollcheck: FNV-1a of a whole file (0 when it cannot be read).
inline unsigned long long fileFnv(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0;
    unsigned long long h = 1469598103934665603ULL;
    std::vector<char> buf(1 << 20);
    while (f.read(buf.data(), (std::streamsize)buf.size()) || f.gcount()) {
        const std::streamsize got = f.gcount();
        for (std::streamsize i = 0; i < got; ++i) {
            h ^= (unsigned char)buf[(size_t)i];
            h *= 1099511628211ULL;
        }
    }
    return h;
}

inline bool g_on = false;      // cfg `grouptrace=1`
inline long long g_stride = 1; // cfg `groupstride=<n>` (subsampling)
inline bool g_all = false;

struct Tracked {
    GameObject* obj = nullptr;
    int uid = -1;
    // The last rect written out. Reference for writing only changes above the threshold
    float cx = 0, cy = 0, w = 0, h = 0;
    float rot = 0;
    int on = -1;   // negation of GameObject::m_isGroupDisabled (toggle)
    int env = 0;
    bool emitted = false;
};

inline std::vector<Tracked> g_objs;
inline GJBaseGameLayer* g_owner = nullptr;
inline std::ofstream g_out;
inline long long g_rows = 0;
inline long long g_lastTick = -1;   // last tick a row was written at (= depth of the recording)

struct Roll {
    long long rows = 0;
    long long depth = -1;
};

inline Roll g_lastRoll;

constexpr float kEps = 0.05f;

inline void reset() {
    g_objs.clear();
    areaenv::clear();   // keyed by GameObject*, which die with the level
    g_owner = nullptr;
    g_rows = 0;
    g_lastTick = -1;
    g_lastRoll = Roll{};
    if (g_out.is_open()) g_out.close();
}

inline bool owns(GJBaseGameLayer* l) { return l && g_owner == l; }

inline void build(GJBaseGameLayer* l) {
    reset();
    areaenv::resetTallies();
    if (!g_on || !l || !l->m_objects) return;
    g_owner = l;
    for (auto* obj : CCArrayExt<GameObject*>(l->m_objects)) {
        if (!obj) continue;
        const int t7 = (int)obj->m_objectType;
        if ((t7 == 22 || t7 == 31) && !g_cfg.coinMode && !g_cfg.coinRoute)
            continue;
        if (!g_all) {
            if (obj->m_groupCount <= 0) continue;
            if (t7 == 7) continue;
        }
        if (t7 == 20) {
            const int oid = (int)obj->m_objectID;
            const bool wanted = oid == 200 || oid == 201 || oid == 202
                                || oid == 203 || oid == 1334 || oid == 2900;
            if (!wanted) continue;
        }
        Tracked t;
        t.obj = obj;
        t.uid = obj->m_uniqueID;
        g_objs.push_back(t);
    }
    g_out.open(std::string(DATA_DIR) + "/grouptrace.txt", std::ios::trunc);
    if (g_out.is_open()) g_out << "tick,uid,cx,cy,w,h,on,rot\n";
    log::info("grouptrace: tracking {} grouped objects", g_objs.size());
}

inline Roll roll(long long endTick = -1) {
    Roll r;
    if (!g_on || g_objs.empty()) { g_lastRoll = r; return r; }
    for (auto& t : g_objs) t.emitted = false;
    if (g_out.is_open()) {
        g_out.close();
        const std::string cur = std::string(DATA_DIR) + "/grouptrace.txt";
        if (g_rows > 0) {
            const std::string last =
                std::string(DATA_DIR) + "/grouptrace_last.txt";
            const std::string tmp = last + ".tmp";
            if (endTick >= 0) {
                std::ofstream app(cur, std::ios::binary | std::ios::app);
                app << "end," << endTick << "\n";
            }
            // cfg gtrollcheck (debug): the copy's bytes, hashed before the rename, against _last after
            unsigned long long checkSig = 0;
            if (g_cfg.gtRollCheck) checkSig = fileFnv(cur);
            std::error_code ec;
            std::filesystem::rename(cur, last, ec);
            if (ec) {
                static bool s_said = false;
                if (!s_said) {
                    s_said = true;
                    log::warn("grouptrace: renaming the recording onto grouptrace_last.txt failed ({}) "
                              "- copying it instead from now on when that happens", ec.message());
                }
                // The copy, from the recording as it now stands (trailer included).
                {
                    std::ifstream src(cur, std::ios::binary);
                    std::ofstream dst(tmp, std::ios::binary | std::ios::trunc);
                    dst << src.rdbuf();
                    dst.flush();
                }
                ec.clear();
                std::filesystem::rename(tmp, last, ec);
            }
            if (g_cfg.gtRollCheck && !ec) {
                const unsigned long long got = fileFnv(last);
                log::info("gtrollcheck: {} (copy {:016x} last {:016x})",
                          got == checkSig ? "same bytes" : "MISMATCH", checkSig, got);
            }
            if (ec) {
                // if rename fails, keep the previous recording (do not throw)
                std::filesystem::remove(tmp, ec);
                log::warn("grouptrace: rename failed, keeping the previous "
                          "grouptrace_last.txt");
            } else {
                r.rows = g_rows;
                r.depth = g_lastTick;
            }
        }
        g_out.open(cur, std::ios::trunc);
        if (g_out.is_open()) g_out << "tick,uid,cx,cy,w,h,on,rot\n";
    }
    g_rows = 0;
    g_lastTick = -1;
    g_lastRoll = r;
    return r;
}

inline void restart() {
    if (!g_on || g_objs.empty()) return;
    for (auto& t : g_objs) t.emitted = false;
    if (g_out.is_open()) {
        g_out.close();
        g_out.open(std::string(DATA_DIR) + "/grouptrace.txt", std::ios::trunc);
        if (g_out.is_open()) g_out << "tick,uid,cx,cy,w,h,on,rot\n";
    }
    g_rows = 0;
    g_lastTick = -1;
}

inline void snapshotInit() {
    if (!g_on || g_objs.empty() || !g_out.is_open()) return;
    for (const auto& tr : g_objs) {
        if (!tr.obj) continue;
        const int on = (tr.obj->m_isGroupDisabled || tr.obj->m_isGroupDisabledTemp) ? 0 : 1;
        g_out << "init," << tr.uid << "," << on << "\n";
    }
}

inline void tick(long long t) {
    if (!g_on || g_objs.empty() || !g_out.is_open()) return;
    if (g_stride > 1 && (t % g_stride) != 0) return;
    for (auto& tr : g_objs) {
        if (!tr.obj) continue;
        auto r = tr.obj->getObjectRect();
        const float cx = r.origin.x + r.size.width * 0.5f;
        const float cy = r.origin.y + r.size.height * 0.5f;
        const float w = r.size.width, h = r.size.height;
        const float rot = tr.obj->getRotation();
        const int on = (tr.obj->m_isGroupDisabled
                        || tr.obj->m_isGroupDisabledTemp) ? 0 : 1;
        float ex = cx, ey = cy, ew = w, eh = h;
        const int env = areaenv::envelope(tr.obj, ex, ey, ew, eh) ? 1 : 0;
        if (tr.emitted && on == tr.on && env == tr.env && std::fabs(ex - tr.cx) < kEps
            && std::fabs(ey - tr.cy) < kEps && std::fabs(ew - tr.w) < kEps
            && std::fabs(eh - tr.h) < kEps && std::fabs(rot - tr.rot) < kEps)
            continue;
        tr.cx = ex; tr.cy = ey; tr.w = ew; tr.h = eh; tr.rot = rot; tr.on = on; tr.env = env;
        tr.emitted = true;
        char b[160];
        if (env) {
            snprintf(b, sizeof(b), "%lld,%d,%.3f,%.3f,%.3f,%.3f,%d,%.3f,1\n", t, tr.uid,
                     ex, ey, ew, eh, on, rot);
            ++areaenv::g_rows;
        } else {
            snprintf(b, sizeof(b), "%lld,%d,%.3f,%.3f,%.3f,%.3f,%d,%.3f\n", t, tr.uid,
                     cx, cy, w, h, on, rot);
        }
        g_out << b;
        ++g_rows;
        g_lastTick = t;
    }
    if ((t % 600) == 0) g_out.flush();
}
} // namespace grouptrace
