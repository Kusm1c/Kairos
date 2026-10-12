#include "Macro.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>

namespace kpsim::macro {

namespace {

bool heldIn(const Holds& holds, Tick t) {
    auto it = std::upper_bound(holds.begin(), holds.end(), t,
                               [](Tick v, const Hold& h) { return v < h.press; });
    if (it == holds.begin()) return false;
    --it;
    return t >= it->press && t < it->release;
}

void normalizeHolds(Holds& holds) {
    Holds in;
    for (const auto& h : holds)
        if (h.release > h.press) in.push_back(h);
    std::sort(in.begin(), in.end(), [](const Hold& a, const Hold& b) { return a.press < b.press; });
    holds.clear();
    for (const auto& h : in) {
        if (!holds.empty() && h.press <= holds.back().release)
            holds.back().release = std::max(holds.back().release, h.release);
        else
            holds.push_back(h);
    }
}

bool validHolds(const Holds& holds) {
    for (size_t i = 0; i < holds.size(); ++i) {
        if (holds[i].press < 1 || holds[i].release <= holds[i].press) return false;
        if (i && holds[i].press <= holds[i - 1].release) return false;
    }
    return true;
}

} // namespace

bool Plan::heldAt(Tick t, int p) const { return heldIn(channel(p), t); }

Tick Plan::lastEdge() const {
    Tick t = 0;
    for (const Holds* c : {&holds, &holds2})
        for (const auto& h : *c) t = std::max(t, h.release == kNever ? h.press : h.release);
    return t;
}

void Plan::normalize() {
    normalizeHolds(holds);
    normalizeHolds(holds2);
}

bool Plan::valid() const { return validHolds(holds) && validHolds(holds2); }

uint64_t Plan::fingerprint() const {
    uint64_t h = 1469598103934665603ull;
    for (const Holds* c : {&holds, &holds2}) {
        for (const auto& x : *c) {
            h = (h ^ x.press) * 1099511628211ull;
            h = (h ^ x.release) * 1099511628211ull;
        }
        h = (h ^ 0xffu) * 1099511628211ull;
    }
    return h;
}

namespace {

struct Event {
    uint64_t frame;
    bool down;
    bool player2;
    uint8_t button;
};

struct Reader {
    const uint8_t* d;
    size_t n, pos = 0;
    size_t left() const { return n > pos ? n - pos : 0; }
    bool raw(void* out, size_t k) {
        if (left() < k) return false;
        std::memcpy(out, d + pos, k);
        pos += k;
        return true;
    }
    bool var(uint64_t& v) {   // LEB128
        v = 0;
        for (int i = 0; i < 10; ++i) {
            if (pos >= n) return false;
            uint8_t b = d[pos++];
            v |= uint64_t(b & 0x7f) << (7 * i);
            if (!(b & 0x80)) return true;
        }
        return false;
    }
    bool str(std::string& s) {
        uint64_t len;
        if (!var(len) || len > 0xffff || left() < len) return false;
        s.assign(reinterpret_cast<const char*>(d + pos), (size_t)len);
        pos += (size_t)len;
        return true;
    }
    template <class T> bool be(T& v) {   // big-endian float/double
        uint8_t b[sizeof(T)];
        if (!raw(b, sizeof(T))) return false;
        std::reverse(b, b + sizeof(T));
        std::memcpy(&v, b, sizeof(T));
        return true;
    }
    bool skip(uint64_t k) {
        if (left() < k) return false;
        pos += (size_t)k;
        return true;
    }
};

bool parseGdr2(const std::vector<uint8_t>& bytes, Macro& m, std::vector<Event>& ev, std::string& err) {
    Reader r{bytes.data(), bytes.size()};
    char magic[3];
    if (!r.raw(magic, 3) || std::memcmp(magic, "GDR", 3) != 0) { err = "not a .gdr2 file"; return false; }
    uint64_t version, u;
    std::string inputTag, s;
    float duration;
    double fps;
    if (!r.var(version) || !r.str(inputTag) || !r.str(s) || !r.str(s) || !r.be(duration) || !r.var(u) ||
        !r.be(fps) || !r.var(u) || !r.var(u)) { err = "bad .gdr2 header"; return false; }
    uint64_t ldm, platformer, botVersion, levelId;
    if (!r.var(ldm) || !r.var(platformer) || !r.str(m.bot) || !r.var(botVersion) || !r.var(levelId) ||
        !r.str(m.levelName)) { err = "bad .gdr2 header"; return false; }
    m.framerate = fps;
    m.platformer = platformer != 0;
    m.levelId = (uint32_t)levelId;
    uint64_t ext;
    if (!r.var(ext) || !r.skip(ext)) { err = "bad .gdr2 extension"; return false; }
    uint64_t deaths;
    if (!r.var(deaths)) { err = "bad .gdr2 deaths"; return false; }
    for (uint64_t i = 0; i < deaths; ++i)
        if (!r.var(u)) { err = "bad .gdr2 deaths"; return false; }
    m.deaths = (size_t)deaths;
    uint64_t count, p1Count;
    if (!r.var(count) || !r.var(p1Count) || p1Count > count) { err = "bad .gdr2 input count"; return false; }
    uint64_t frame = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t packed;
        if (!r.var(packed)) { err = "truncated .gdr2 inputs"; return false; }
        Event e{};
        e.down = packed & 1;
        uint64_t delta;
        if (m.platformer) { e.button = (packed >> 1) & 3; delta = packed >> 3; }
        else { e.button = 1; delta = packed >> 1; }
        if (delta > std::numeric_limits<uint64_t>::max() - frame) { err = "bad .gdr2 frame"; return false; }
        e.frame = frame + delta;
        e.player2 = i >= p1Count;
        if (!inputTag.empty()) {
            uint64_t k;
            if (!r.var(k) || !r.skip(k)) { err = "bad .gdr2 input extension"; return false; }
        }
        ev.push_back(e);
        frame = (i + 1 == p1Count) ? 0 : e.frame;
    }
    return true;
}

// MessagePack subset used by .gdr.
struct Pack {
    const uint8_t* d;
    size_t n, pos = 0;
    bool u8(uint8_t& v) { if (pos >= n) return false; v = d[pos++]; return true; }
    bool peek(uint8_t& v) const { if (pos >= n) return false; v = d[pos]; return true; }
    bool be(uint64_t& v, int k) { v = 0; for (int i = 0; i < k; ++i) { if (pos >= n) return false; v = (v << 8) | d[pos++]; } return true; }
    bool skipN(uint64_t k) { if (pos + k > n) return false; pos += (size_t)k; return true; }
    bool str(std::string& s) {
        uint8_t b; uint64_t len;
        if (!u8(b)) return false;
        if (b >= 0xa0 && b <= 0xbf) len = b & 0x1f;
        else if (b == 0xd9) { if (!be(len, 1)) return false; }
        else if (b == 0xda) { if (!be(len, 2)) return false; }
        else if (b == 0xdb) { if (!be(len, 4)) return false; }
        else return false;
        if (pos + len > n) return false;
        s.assign(reinterpret_cast<const char*>(d + pos), (size_t)len);
        pos += (size_t)len;
        return true;
    }
    bool map(size_t& c) {
        uint8_t b; uint64_t v;
        if (!u8(b)) return false;
        if (b >= 0x80 && b <= 0x8f) { c = b & 0x0f; return true; }
        if (b == 0xde) { if (!be(v, 2)) return false; c = (size_t)v; return true; }
        if (b == 0xdf) { if (!be(v, 4)) return false; c = (size_t)v; return true; }
        return false;
    }
    bool arr(size_t& c) {
        uint8_t b; uint64_t v;
        if (!u8(b)) return false;
        if (b >= 0x90 && b <= 0x9f) { c = b & 0x0f; return true; }
        if (b == 0xdc) { if (!be(v, 2)) return false; c = (size_t)v; return true; }
        if (b == 0xdd) { if (!be(v, 4)) return false; c = (size_t)v; return true; }
        return false;
    }
    bool num(double& v) {
        uint8_t b;
        if (!u8(b)) return false;
        uint64_t u;
        if (b <= 0x7f) { v = b; return true; }
        if (b >= 0xe0) { v = (int8_t)b; return true; }
        switch (b) {
            case 0xcc: if (!be(u, 1)) return false; v = (double)u; return true;
            case 0xcd: if (!be(u, 2)) return false; v = (double)u; return true;
            case 0xce: if (!be(u, 4)) return false; v = (double)u; return true;
            case 0xcf: if (!be(u, 8)) return false; v = (double)u; return true;
            case 0xd0: if (!be(u, 1)) return false; v = (int8_t)u; return true;
            case 0xd1: if (!be(u, 2)) return false; v = (int16_t)u; return true;
            case 0xd2: if (!be(u, 4)) return false; v = (int32_t)u; return true;
            case 0xd3: if (!be(u, 8)) return false; v = (double)(int64_t)u; return true;
            case 0xca: { if (!be(u, 4)) return false; uint32_t t = (uint32_t)u; float f; std::memcpy(&f, &t, 4); v = f; return true; }
            case 0xcb: { if (!be(u, 8)) return false; std::memcpy(&v, &u, 8); return true; }
            case 0xc2: v = 0; return true;
            case 0xc3: v = 1; return true;
            default: return false;
        }
    }
    bool skip() {
        uint8_t b;
        if (!u8(b)) return false;
        if (b <= 0x7f || b >= 0xe0) return true;
        if (b >= 0x80 && b <= 0x8f) { for (size_t i = 0; i < size_t(b & 0x0f) * 2; ++i) if (!skip()) return false; return true; }
        if (b >= 0x90 && b <= 0x9f) { for (size_t i = 0; i < size_t(b & 0x0f); ++i) if (!skip()) return false; return true; }
        if (b >= 0xa0 && b <= 0xbf) return skipN(b & 0x1f);
        uint64_t l;
        switch (b) {
            case 0xc0: case 0xc2: case 0xc3: return true;
            case 0xcc: case 0xd0: return skipN(1);
            case 0xcd: case 0xd1: return skipN(2);
            case 0xce: case 0xd2: case 0xca: return skipN(4);
            case 0xcf: case 0xd3: case 0xcb: return skipN(8);
            case 0xd9: case 0xc4: if (!be(l, 1)) return false; return skipN(l);
            case 0xda: case 0xc5: if (!be(l, 2)) return false; return skipN(l);
            case 0xdb: case 0xc6: if (!be(l, 4)) return false; return skipN(l);
            case 0xdc: { if (!be(l, 2)) return false; for (uint64_t i = 0; i < l; ++i) if (!skip()) return false; return true; }
            case 0xdd: { if (!be(l, 4)) return false; for (uint64_t i = 0; i < l; ++i) if (!skip()) return false; return true; }
            case 0xde: { if (!be(l, 2)) return false; for (uint64_t i = 0; i < l * 2; ++i) if (!skip()) return false; return true; }
            case 0xdf: { if (!be(l, 4)) return false; for (uint64_t i = 0; i < l * 2; ++i) if (!skip()) return false; return true; }
            case 0xd4: return skipN(2);
            case 0xd5: return skipN(3);
            case 0xd6: return skipN(5);
            case 0xd7: return skipN(9);
            case 0xd8: return skipN(17);
            case 0xc7: if (!be(l, 1)) return false; return skipN(l + 1);
            case 0xc8: if (!be(l, 2)) return false; return skipN(l + 1);
            case 0xc9: if (!be(l, 4)) return false; return skipN(l + 1);
            default: return false;
        }
    }
};

bool parseGdr(const std::vector<uint8_t>& bytes, Macro& m, std::vector<Event>& ev, std::string& err) {
    Pack r{bytes.data(), bytes.size()};
    size_t top;
    if (!r.map(top)) { err = "not a .gdr/.gdr2 file"; return false; }
    for (size_t i = 0; i < top; ++i) {
        std::string key;
        if (!r.str(key)) { err = "bad .gdr key"; return false; }
        double v;
        if (key == "framerate" || key == "fps") {
            if (!r.num(v)) { err = "bad .gdr framerate"; return false; }
            if (v > 0) m.framerate = v;
        } else if (key == "platformer") {
            if (!r.num(v)) { err = "bad .gdr platformer"; return false; }
            m.platformer = v != 0;
        } else if (key == "bot") {
            size_t c;
            if (!r.map(c)) { err = "bad .gdr bot"; return false; }
            for (size_t j = 0; j < c; ++j) {
                std::string k;
                if (!r.str(k)) { err = "bad .gdr bot"; return false; }
                if (k == "name") { if (!r.str(m.bot)) { err = "bad .gdr bot"; return false; } }
                else if (!r.skip()) { err = "bad .gdr bot"; return false; }
            }
        } else if (key == "level") {
            size_t c;
            if (!r.map(c)) { err = "bad .gdr level"; return false; }
            for (size_t j = 0; j < c; ++j) {
                std::string k;
                if (!r.str(k)) { err = "bad .gdr level"; return false; }
                if (k == "id") { if (!r.num(v)) { err = "bad .gdr level"; return false; } m.levelId = (uint32_t)v; }
                else if (k == "name") { if (!r.str(m.levelName)) { err = "bad .gdr level"; return false; } }
                else if (!r.skip()) { err = "bad .gdr level"; return false; }
            }
        } else if (key == "deaths") {
            size_t c;
            if (!r.arr(c)) { err = "bad .gdr deaths"; return false; }
            for (size_t j = 0; j < c; ++j) if (!r.skip()) { err = "bad .gdr deaths"; return false; }
            m.deaths = c;
        } else if (key == "inputs") {
            size_t c;
            if (!r.arr(c)) { err = "bad .gdr inputs"; return false; }
            for (size_t j = 0; j < c; ++j) {
                size_t mc;
                if (!r.map(mc)) { err = "bad .gdr input"; return false; }
                Event e{0, false, false, 1};
                for (size_t k = 0; k < mc; ++k) {
                    std::string ik;
                    if (!r.str(ik)) { err = "bad .gdr input"; return false; }
                    if (ik == "frame") { if (!r.num(v)) { err = "bad .gdr input"; return false; } e.frame = (uint64_t)v; }
                    else if (ik == "btn" || ik == "button") { if (!r.num(v)) { err = "bad .gdr input"; return false; } e.button = v == 0 ? 1 : (uint8_t)v; }
                    else if (ik == "down" || ik == "holding") { if (!r.num(v)) { err = "bad .gdr input"; return false; } e.down = v != 0; }
                    else if (ik == "2p" || ik == "p2" || ik == "player2") { if (!r.num(v)) { err = "bad .gdr input"; return false; } e.player2 = v != 0; }
                    else if (!r.skip()) { err = "bad .gdr input"; return false; }
                }
                ev.push_back(e);
            }
        } else if (!r.skip()) {
            err = "bad .gdr field " + key;
            return false;
        }
    }
    return true;
}

} // namespace

bool readMacro(const std::vector<uint8_t>& bytes, Macro& out, std::string& err) {
    out = Macro{};
    std::vector<Event> ev;
    bool ok = bytes.size() >= 3 && std::memcmp(bytes.data(), "GDR", 3) == 0 ? parseGdr2(bytes, out, ev, err)
                                                                            : parseGdr(bytes, out, ev, err);
    if (!ok) return false;
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
    // Down/up events in order, last one wins.
    auto fold = [&](Holds& out, auto&& take) {
        bool held = false;
        Tick start = 0;
        for (const auto& e : ev) {
            if (e.button != 1 || !take(e)) continue;
            if (e.down && !held) { held = true; start = (Tick)e.frame; }
            else if (!e.down && held) { held = false; if (e.frame > start) out.push_back({start, (Tick)e.frame}); }
        }
        if (held) out.push_back({start, kNever});
    };
    for (const auto& e : ev)
        if (e.frame >= kNever) { err = "frame out of range"; return false; }
    fold(out.plan.holds, [](const Event& e) { return !e.player2; });
    fold(out.plan.holds2, [](const Event& e) { return e.player2; });
    fold(out.merged.holds, [](const Event&) { return true; });
    out.player2Inputs = !out.plan.holds2.empty();
    for (Plan* p : {&out.plan, &out.merged}) {
        p->normalize();
        for (Holds* c : {&p->holds, &p->holds2})
            for (auto& h : *c)
                if (h.press == 0) h.press = 1;
    }
    return true;
}

bool readMacroFile(const std::filesystem::path& path, Macro& out, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "cannot open " + path.string(); return false; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return readMacro(bytes, out, err);
}

bool readSolverPlan(const std::filesystem::path& path, Plan& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "cannot open " + path.string(); return false; }
    out = Plan{};
    std::string line;
    bool held[2] = {};
    Tick start[2] = {};
    while (std::getline(in, line)) {
        if (line.rfind("attempt=", 0) == 0) { out = Plan{}; held[0] = held[1] = false; continue; }
        if (line.rfind("input=", 0) != 0) continue;
        unsigned long long step = 0, buttons = 0;
        if (std::sscanf(line.c_str() + 6, "%llu,%llu", &step, &buttons) != 2) { err = "bad line: " + line; return false; }
        // Pressed during the step after `step`: our tick numbering starts at 1.
        const Tick t = (Tick)step + 1;
        for (int p = 0; p < 2; ++p) {
            bool down = (buttons >> p) & 1;
            if (down && !held[p]) { held[p] = true; start[p] = t; }
            else if (!down && held[p]) { held[p] = false; if (t > start[p]) out.channel(p).push_back({start[p], t}); }
        }
    }
    for (int p = 0; p < 2; ++p)
        if (held[p]) out.channel(p).push_back({start[p], kNever});
    out.normalize();
    return true;
}

std::string writeSolverPlan(const Plan& plan) {
    // Every tick where the buttons change, as a bitmask; our tick t is the solver's step t - 1.
    std::vector<std::pair<Tick, int>> edges;
    for (int p = 0; p < 2; ++p)
        for (const auto& h : plan.channel(p)) {
            edges.push_back({h.press, 0});
            if (h.release != kNever) edges.push_back({h.release, 0});
        }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    std::string out;
    int last = 0;
    for (auto& [t, unused] : edges) {
        int buttons = (plan.heldAt(t, 0) ? 1 : 0) | (plan.heldAt(t, 1) ? 2 : 0);
        if (buttons == last) continue;
        out += "input=" + std::to_string((unsigned long long)t - 1) + "," + std::to_string(buttons) + "\n";
        last = buttons;
    }
    return out;
}

std::vector<uint8_t> writeGdr2(const Plan& plan, uint32_t levelId, const std::string& levelName) {
    std::vector<uint8_t> o;
    auto raw = [&](const void* p, size_t k) { auto b = (const uint8_t*)p; o.insert(o.end(), b, b + k); };
    auto var = [&](uint64_t v) { do { uint8_t b = v & 0x7f; v >>= 7; if (v) b |= 0x80; o.push_back(b); } while (v); };
    auto str = [&](const std::string& s) { var(s.size()); raw(s.data(), s.size()); };
    auto be = [&](auto v) { uint8_t b[sizeof v]; std::memcpy(b, &v, sizeof v); std::reverse(b, b + sizeof v); raw(b, sizeof v); };

    std::vector<std::pair<Tick, bool>> ev[2];
    for (int p = 0; p < 2; ++p)
        for (const auto& h : plan.channel(p)) {
            ev[p].push_back({h.press, true});
            if (h.release != kNever) ev[p].push_back({h.release, false});
        }
    raw("GDR", 3);
    var(2);                  // format version
    str("");                 // input tag
    str("");                 // author
    str("");                 // description
    be(0.0f);                // duration
    var(22);                 // game version
    be(240.0);               // framerate
    var(0);                  // seed
    var(0);                  // coins
    var(0);                  // ldm
    var(0);                  // platformer
    str("kusmic.kairos");
    var(1);                  // bot version
    var(levelId);
    str(levelName);
    var(0);                  // extension
    var(0);                  // deaths
    var(ev[0].size() + ev[1].size());
    var(ev[0].size());
    for (const auto& list : ev) {
        Tick prev = 0;
        for (const auto& [t, down] : list) {
            var((uint64_t(t - prev) << 1) | (down ? 1 : 0));
            prev = t;
        }
    }
    return o;
}

bool writeFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    return out.good();
}

} // namespace kpsim::macro
