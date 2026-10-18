#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <set>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "models/ship_model.hpp"
#include "models/ufo_model.hpp"
#include "models/ship_params.hpp"

namespace dp {
constexpr int kTouchBits = 32;

template <int N>
struct Bits {
    static_assert(N > 0, "Bits<0>");
    static constexpr int kWords = (N + 63) / 64;
    uint64_t w[kWords];
    void set(int i) { w[i >> 6] |= (uint64_t)1 << (i & 63); }
    bool test(int i) const { return ((w[i >> 6] >> (i & 63)) & 1u) != 0; }
    bool any() const {
        for (int k = 0; k < kWords; ++k)
            if (w[k]) return true;
        return false;
    }
    uint64_t word(int k) const { return w[k]; }
    // The lowest set bit, or -1 when empty.
    int lowest() const {
        for (int k = 0; k < kWords; ++k)
            if (w[k])
                for (int b = 0; b < 64; ++b)
                    if ((w[k] >> b) & 1u) return k * 64 + b;
        return -1;
    }
    // A set with exactly bit i.
    static Bits bit(int i) {
        Bits r{};
        r.set(i);
        return r;
    }
    explicit operator bool() const { return any(); }
    Bits& operator|=(const Bits& o) {
        for (int k = 0; k < kWords; ++k) w[k] |= o.w[k];
        return *this;
    }
    Bits& operator&=(const Bits& o) {
        for (int k = 0; k < kWords; ++k) w[k] &= o.w[k];
        return *this;
    }
    Bits& operator^=(const Bits& o) {
        for (int k = 0; k < kWords; ++k) w[k] ^= o.w[k];
        return *this;
    }
    friend Bits operator|(Bits a, const Bits& b) { return a |= b; }
    friend Bits operator&(Bits a, const Bits& b) { return a &= b; }
    friend Bits operator^(Bits a, const Bits& b) { return a ^= b; }
    friend Bits operator~(Bits a) {
        for (int k = 0; k < kWords; ++k) a.w[k] = ~a.w[k];
        if (N % 64) a.w[kWords - 1] &= ((uint64_t)1 << (N % 64)) - 1;
        return a;
    }
    friend bool operator==(const Bits& a, const Bits& b) {
        for (int k = 0; k < kWords; ++k)
            if (a.w[k] != b.w[k]) return false;
        return true;
    }
    friend bool operator!=(const Bits& a, const Bits& b) { return !(a == b); }
};
static_assert(std::is_trivially_copyable_v<Bits<128>>
                  && std::is_standard_layout_v<Bits<128>>,
              "Bits must stay POD: State is memcpy'd and memcmp'd");

using TouchMask = Bits<kTouchBits>;
static_assert(kTouchBits <= 64, "the mod's boundary masks are one word");
inline TouchMask touchBit(int b) { return TouchMask::bit(b); }
inline int touchBitIndex(const TouchMask& m) {
    const int b = m.lowest();
    return (b < 0 || b > kTouchBits - 1) ? kTouchBits - 1 : b;
}

constexpr int kGravPortalBits = 128;
static_assert(kGravPortalBits - 1 <= 127, "Obj::gpBit is int8_t");
using GravLatch = Bits<kGravPortalBits>;
struct GpHandoff {
    double x;   // where the bit changes hands
    int bit;
};
inline std::vector<GpHandoff> g_gpHandoff;
inline std::string gravLatchHex(const GravLatch& b) {
    int top = GravLatch::kWords - 1;
    while (top > 0 && b.word(top) == 0) --top;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)b.word(top));
    std::string s = buf;
    for (int k = top - 1; k >= 0; --k) {
        std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)b.word(k));
        s += buf;
    }
    return s;
}
}  // namespace dp
