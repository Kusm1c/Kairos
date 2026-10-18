#pragma once
#include "dp/level_loader.hpp"

namespace dp {
// objects overlapping an x window, via the sorted array
struct XSlice {
    const std::vector<Obj>& v;
    size_t lo = 0;
    double maxHw = 0.0;
    // --widescan (constants.hpp): the objects forRange's break can step over, by index
    std::vector<size_t> wide;
    double wideMaxHw = 0.0;
    explicit XSlice(const std::vector<Obj>& objs) : v(objs) {
        for (const Obj& o : objs) maxHw = std::max(maxHw, o.hw);
        for (size_t i = 0; i < objs.size(); ++i)
            if (objs[i].hw >= kWideScanHw) {
                wide.push_back(i);
                wideMaxHw = std::max(wideMaxHw, objs[i].hw);
            }
    }
    void seekTo(double x0) {
        const double lim = x0 - maxHw - 40.0;
        size_t a = 0, b = v.size();
        while (a < b) {
            const size_t m = (a + b) / 2;
            if (v[m].cx < lim) a = m + 1; else b = m;
        }
        lo = a;
    }
    template <class F>
    void forRange(double x0, double x1, F&& f) {
        const bool ws = g_wideScan;
        if (lo > 0 && v[lo - 1].cx + (ws ? kWideScanHw : v[lo - 1].hw) + 40 >= x0) {
            if (ws) {
                const double lim = x0 - kWideScanHw - 40.0;
                size_t a = 0, b = v.size();
                while (a < b) {
                    const size_t m = (a + b) / 2;
                    if (v[m].cx < lim) a = m + 1; else b = m;
                }
                lo = a;
            } else {
                seekTo(x0);
            }
        }
        while (lo < v.size() && v[lo].cx + v[lo].hw + 40 < x0) ++lo;
        size_t i = lo;
        for (; i < v.size(); ++i) {
            if (v[i].cx - v[i].hw > x1 + 40) break;
            f(v[i]);
        }
        if (!ws) return;
        // the narrow ones just past the break (a wide one comes from `wide` below)
        for (size_t j = i + 1; j < v.size() && v[j].cx - kWideScanHw <= x1 + 40; ++j)
            if (v[j].hw < kWideScanHw && v[j].cx - v[j].hw <= x1 + 40) f(v[j]);
        if (wide.empty()) return;
        // the wide ones the cursor has passed...
        const auto mid = std::lower_bound(wide.begin(), wide.end(), lo);
        for (auto it = mid; it != wide.begin();) {
            --it;
            const Obj& o = v[*it];
            if (o.cx + wideMaxHw + 40 < x0) break;
            if (o.cx + o.hw + 40 >= x0 && o.cx - o.hw <= x1 + 40) f(o);
        }
        // ...and the ones past the break
        for (auto it = std::upper_bound(wide.begin(), wide.end(), i); it != wide.end(); ++it) {
            const Obj& o = v[*it];
            if (o.cx - wideMaxHw > x1 + 40) break;
            if (o.cx - o.hw <= x1 + 40 && o.cx + o.hw + 40 >= x0) f(o);
        }
    }
};

struct State {
    float y, vy;      // vy is WORLD frame (dump convention): y += 0.225 * vy
    uint8_t mode;     // 0 cube, 1 ship
    uint8_t held;     // ship: input level; cube: unused (jump is an event)
    uint8_t grounded; // cube only
    uint8_t flip;     // gravity flipped (upsideDown): all vertical signs mirror
    float xAbs;       // absolute x, float-accumulated like GD (see advanceX)
    const Obj* snapObj;  // GD m_objectSnappedTo: last solid stood on (null = none)
    float snapDist;      // GD m_snapDistance: x - snapObj->cx at the last contact
    float dx2 = 0.f;
    const Obj* usedOrb;  // GD m_touchedRings: orb already used in this contact
    uint8_t ringHold;
    uint8_t pressSpent;
    uint8_t onSlope;
    uint8_t slopeT = 0;
    uint8_t rideLanded = 0;
    uint8_t seatT = 0;
    int16_t prevC6q = 0;
    float slopeM;
    float lawSv = 0.f;
    float followF[kFollowYSlots] = {0.f, 0.f};
    uint8_t followOk = 0;
    uint8_t tpArrive = 0;
    uint8_t ctrlT = 0;
    uint8_t seatSpd = 0;
    uint8_t flipT = 255;
    uint8_t armT = 255;
    uint8_t modeT = 255;
    uint8_t slideT = 255;
    uint8_t slideT2 = 255;
    uint8_t spiderJumpT = 255;
    float bandFloor = 0.f;
    float bandCeil = 1e9f;
    uint8_t bandAnim = 0;
    uint8_t bandBranch = 0;
    float bandRefY = 0.f;
    float dx;
    TouchMask trig{};
    int32_t trigT = -1;
    GravLatch portalLatch{};
    GravLatch portalLatch2{};
    uint16_t fireB[kTouchBits] = {};
    float lockOff = 0.f;
    uint32_t rotSpent = 0;
    uint8_t rotChan = 0;
    uint8_t tpSkip = 0;
    uint16_t rotRev = 0;
    uint8_t pFlap = 0;
    uint8_t pSpiderTap = 0;
    uint8_t pNoTerm = 0;
    uint8_t boost = 0;
    uint8_t ceilMode = 0;
    float pExitVy = 0.f;
    int32_t slopeUid0 = -1;
    int32_t slopeUidNow = -1;
    // Both take the same input and either one dying ends the run.
    uint8_t dual;
    // the second body had nothing within reach this tick (see stepBoth)
    uint8_t freeHalf = 0;
    uint8_t seatT2 = 0;
    float y2, vy2, slopeM2, snapDist2;
    uint8_t grounded2, flip2, ringHold2, onSlope2;
    uint8_t pressSpent2 = 0;   // second body's pressSpent (see pressSpent)
    uint8_t slopeT2 = 0;   // second body's ride counter (see slopeT)
    uint8_t rideLanded2 = 0;  // second body's landing record (see rideLanded)
    uint8_t boost2 = 0;
    uint8_t mode2 = 0, ceilT2 = 0, ceilM42 = 0;
    uint8_t mini2 = 0;
    int32_t slopeUid02 = -1, slopeUidNow2 = -1;
    const Obj* snapObj2;
    const Obj* usedOrb2;
    const Obj* usedPad2[4];
    const Obj* usedPad[4];
    uint8_t mini;        // size portal 18 = mini (half 9), 17 = back to normal
    uint8_t rHover = 0;
    uint8_t dashing = 0;
    float dashSlope = 0.f;   // dy per px of x (0 for a rot=0 ring)
    uint8_t frame = 0;
    uint8_t frameChg = 0;
    uint8_t ceilT = 0;
    uint8_t ceilM4 = 0;
    float rot = 0.f;
    uint8_t rotNeg = 0;
    uint8_t ceilPin = 0;
    uint8_t fgArm = 0;
    uint8_t coins = 0;
    uint8_t taps = 0;
    uint16_t items = 0;

    uint32_t parent;  // node arena index
    uint8_t action;   // input level THIS tick (for plan reconstruction)
    uint8_t latLock = 0;
    uint8_t rev = 0;
    uint16_t tight = 0;
    uint8_t jumpBuf = 0;
    float rotStep = 0.f;
    int32_t groundUid = -1;
    int32_t usedOrbOld = -1, usedOrbOld2 = -1;
    uint8_t ogLinger = 0;
    uint8_t holdDead = 0, holdDead2 = 0;
    int32_t usedOrbHist[3] = {-1, -1, -1}, usedOrbHist2[3] = {-1, -1, -1};
    int32_t portSeen[3] = {-1, -1, -1}, portSeen2[3] = {-1, -1, -1};
    int32_t touchRing[4] = {-1, -1, -1, -1}, touchRing2[4] = {-1, -1, -1, -1};
    uint8_t touchRingT = 0, touchRingT2 = 0;
    uint8_t edgeAge = 255;
    uint8_t rotBoost = 0;
    int32_t spdFired[2] = {-1, -1}, spdFired2[2] = {-1, -1};
    uint8_t hitG = 0, hitG2 = 0;
    uint8_t rotNeg2 = 0;
    uint8_t a1cLatch = 0;
    uint8_t dashing2 = 0;
    uint8_t flipT2 = 255, modeT2 = 255;
    uint8_t rHover2 = 0;
    uint8_t ball728 = 0;
    float rot2 = 0.f, rotStep2 = 0.f;
    float dashSlope2 = 0.f;
    int32_t stickSlope = -1;
    int32_t spdMore[2] = {-1, -1}, spdMore2[2] = {-1, -1};
    uint8_t pressAge = 255;
    uint8_t heldSeed = 0;
    uint16_t inputs = 0;
    uint8_t pressPad[4] = {};
};

constexpr size_t kStateBytes =
    (560u +(size_t)(kTouchBits - 32) * sizeof(uint16_t)
          + (sizeof(TouchMask) - sizeof(uint32_t))
          + 2u * (sizeof(GravLatch) - sizeof(Bits<128>)) + 7u) / 8u * 8u;
static_assert(sizeof(State) == kStateBytes,
              "State changed size. If the new field ACCUMULATES over ticks, "
              "seed it in the --start anchor scan, print it in --seeddump, and "
              "run oneoff/py/seedcheck.py to zero before updating this. (This "
              "says nothing about globals -- those belong in reset.hpp.)");

// arena entry for witness reconstruction, packed: bit31 = action, rest parent
struct Node {
    uint32_t packed;
    uint32_t parent() const { return packed & 0x7fffffffu; }
    uint8_t action() const { return (uint8_t)(packed >> 31); }
};
enum : uint32_t { kNone = 0xffffffffu };
inline int g_threads = 4;
}  // namespace dp
