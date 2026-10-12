#include "gdmath.hpp"

#include <cmath>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace kpsim::math {
namespace {
using F1 = float (*)(float);
using F2 = float (*)(float, float);
using D2 = double (*)(double, double);
using FmaSet = int (*)(int);
using FmaGet = int (*)();

struct Table {
    F1 sinf, cosf, tanf, acosf, atanf, asinhf, tanhf;
    F2 atan2f, powf;
    D2 atan2;
    FmaSet setFma3 = nullptr;
    FmaGet getFma3 = nullptr;
    bool ucrt = false;
};

float  libSinf(float x)            { return std::sin(x); }
float  libCosf(float x)            { return std::cos(x); }
float  libTanf(float x)            { return std::tan(x); }
float  libAcosf(float x)           { return std::acos(x); }
float  libAtanf(float x)           { return std::atan(x); }
float  libAsinhf(float x)          { return std::asinh(x); }
float  libTanhf(float x)           { return std::tanh(x); }
float  libAtan2f(float y, float x) { return std::atan2(y, x); }
float  libPowf(float x, float y)   { return std::pow(x, y); }
double libAtan2(double y, double x){ return std::atan2(y, x); }

Table resolve() {
    Table t{libSinf, libCosf, libTanf, libAcosf, libAtanf, libAsinhf, libTanhf,
            libAtan2f, libPowf, libAtan2};
#ifdef _WIN32
    HMODULE crt = LoadLibraryA("ucrtbase.dll");
    if (!crt) return t;
    auto get = [&](const char* name) { return reinterpret_cast<void*>(GetProcAddress(crt, name)); };
    void* p[10] = {get("sinf"), get("cosf"), get("tanf"), get("acosf"), get("atanf"),
                   get("asinhf"), get("tanhf"), get("atan2f"), get("powf"), get("atan2")};
    for (void* f : p)
        if (!f) return t;
    t.sinf   = reinterpret_cast<F1>(p[0]);
    t.cosf   = reinterpret_cast<F1>(p[1]);
    t.tanf   = reinterpret_cast<F1>(p[2]);
    t.acosf  = reinterpret_cast<F1>(p[3]);
    t.atanf  = reinterpret_cast<F1>(p[4]);
    t.asinhf = reinterpret_cast<F1>(p[5]);
    t.tanhf  = reinterpret_cast<F1>(p[6]);
    t.atan2f = reinterpret_cast<F2>(p[7]);
    t.powf   = reinterpret_cast<F2>(p[8]);
    t.atan2  = reinterpret_cast<D2>(p[9]);
    t.setFma3 = reinterpret_cast<FmaSet>(get("_set_FMA3_enable"));
    t.getFma3 = reinterpret_cast<FmaGet>(get("_get_FMA3_enable"));
    t.ucrt = true;
#endif
    return t;
}

const Table& table() {
    static const Table t = resolve();
    return t;
}
} // namespace

float  sinf(float x)             { return table().sinf(x); }
float  cosf(float x)             { return table().cosf(x); }
float  tanf(float x)             { return table().tanf(x); }
float  acosf(float x)            { return table().acosf(x); }
float  atanf(float x)            { return table().atanf(x); }
float  atan2f(float y, float x)  { return table().atan2f(y, x); }
double atan2(double y, double x) { return table().atan2(y, x); }
float  powf(float x, float y)    { return table().powf(x, y); }
float  asinhf(float x)           { return table().asinhf(x); }
float  tanhf(float x)            { return table().tanhf(x); }

const char* backend() { return table().ucrt ? "ucrtbase.dll" : "libm"; }

bool fma3Enabled() {
    const Table& t = table();
    return t.getFma3 && t.getFma3() != 0;
}

bool setFma3(bool enable) {
    const Table& t = table();
    return t.setFma3 && t.setFma3(enable ? 1 : 0) != 0;
}
} // namespace kpsim::math
