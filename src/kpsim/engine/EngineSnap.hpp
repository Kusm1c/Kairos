#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace kpsim::engine {
using unsigned_char = unsigned char;
using unsigned_int  = unsigned int;

struct EngineSnap {
#define SNAP_SCALAR(T, n, off) T n{};
#define SNAP_POINT(n, off)     float n##_x{}, n##_y{};
#define SNAP_OBJ(n, off)       int32_t n{};
#define SNAP_SEED(n, off)      int32_t n{};
#include "SnapFields.inc"
#undef SNAP_SCALAR
#undef SNAP_POINT
#undef SNAP_OBJ
#undef SNAP_SEED
};

template <class F>
inline void forEachField(const EngineSnap& s, F&& f) {
#define SNAP_SCALAR(T, n, off) f(#n, (double)s.n);
#define SNAP_POINT(n, off)     f(#n "_x", (double)s.n##_x); f(#n "_y", (double)s.n##_y);
#define SNAP_OBJ(n, off)       f(#n, (double)s.n);
#define SNAP_SEED(n, off)      f(#n, (double)s.n);
#include "SnapFields.inc"
#undef SNAP_SCALAR
#undef SNAP_POINT
#undef SNAP_OBJ
#undef SNAP_SEED
}

struct FieldDiff { std::string field; double expected, got; };
inline std::vector<FieldDiff> diff(const EngineSnap& real, const EngineSnap& ours) {
    std::vector<double> a, b;
    std::vector<std::string> names;
    forEachField(real, [&](const char* n, double v) { names.emplace_back(n); a.push_back(v); });
    forEachField(ours, [&](const char*, double v) { b.push_back(v); });
    std::vector<FieldDiff> out;
    for (size_t i = 0; i < a.size(); ++i)
        if (!(a[i] == b[i]) && !(a[i] != a[i] && b[i] != b[i]))   // NaN == NaN counts as equal
            out.push_back({names[i], a[i], b[i]});
    return out;
}

inline uint32_t layoutHash() {
    uint32_t h = 2166136261u;                                      // FNV-1a
    auto mix = [&](const char* s) { for (; *s; ++s) { h ^= (uint8_t)*s; h *= 16777619u; } };
#define SNAP_SCALAR(T, n, off) mix(#T); mix(#n);
#define SNAP_POINT(n, off)     mix("pt"); mix(#n);
#define SNAP_OBJ(n, off)       mix("obj"); mix(#n);
#define SNAP_SEED(n, off)      mix("seed"); mix(#n);
#include "SnapFields.inc"
#undef SNAP_SCALAR
#undef SNAP_POINT
#undef SNAP_OBJ
#undef SNAP_SEED
    return h;
}

inline void write(std::FILE* f, const EngineSnap& s) {
#define SNAP_SCALAR(T, n, off) std::fwrite(&s.n, sizeof(s.n), 1, f);
#define SNAP_POINT(n, off)     std::fwrite(&s.n##_x, 4, 1, f); std::fwrite(&s.n##_y, 4, 1, f);
#define SNAP_OBJ(n, off)       std::fwrite(&s.n, 4, 1, f);
#define SNAP_SEED(n, off)      std::fwrite(&s.n, 4, 1, f);
#include "SnapFields.inc"
#undef SNAP_SCALAR
#undef SNAP_POINT
#undef SNAP_OBJ
#undef SNAP_SEED
}

inline bool read(std::FILE* f, EngineSnap& s) {
    bool ok = true;
#define SNAP_SCALAR(T, n, off) ok = ok && std::fread(&s.n, sizeof(s.n), 1, f) == 1;
#define SNAP_POINT(n, off)     ok = ok && std::fread(&s.n##_x, 4, 1, f) == 1 && std::fread(&s.n##_y, 4, 1, f) == 1;
#define SNAP_OBJ(n, off)       ok = ok && std::fread(&s.n, 4, 1, f) == 1;
#define SNAP_SEED(n, off)      ok = ok && std::fread(&s.n, 4, 1, f) == 1;
#include "SnapFields.inc"
#undef SNAP_SCALAR
#undef SNAP_POINT
#undef SNAP_OBJ
#undef SNAP_SEED
    return ok;
}
} // namespace kpsim::engine
