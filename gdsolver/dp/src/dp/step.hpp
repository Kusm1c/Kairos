#pragma once
#include "dp/slopes.hpp"

namespace dp {
inline thread_local int g_vyWriter = 0;      // __LINE__ of the last write this tick
inline thread_local int g_vyWrites = 0;      // how many writes this tick -- see below
inline long long g_vyWatchT = -1;   // --vywriter <t>: report only this tick
#define VYSET(x) (::dp::g_vyWriter = __LINE__, ++::dp::g_vyWrites, (x))

inline thread_local int g_yWriter = 0;
inline thread_local int g_yWrites = 0;
inline thread_local float g_yIn = 0.f;
inline thread_local float g_vyIn = 0.f;
inline thread_local int g_seatGateSeen = 0;      // times the gate was reached this step
inline thread_local int g_seatImpulsedOff = -1;  // -1 = the gate was not reached
inline thread_local int g_seatTappedOff = -1;
inline thread_local int g_seatOnSlope = -1;
inline thread_local int g_seatTook = -1;         // did it take the exemption (and skip vy)?
inline thread_local int g_impulseSite = 0;
inline thread_local int g_impTrail[8] = {};
inline thread_local int g_impCount = 0;
#define IMPULSE() (::dp::g_impTrail[::dp::g_impCount & 7] = __LINE__, ++::dp::g_impCount, ::dp::g_impulseSite = __LINE__, true)
#define YSET(x) (::dp::g_yWriter = __LINE__, ++::dp::g_yWrites, (x))
inline thread_local int g_vpTrail[8] = {};
inline thread_local int g_vpWrites = 0;
inline long long g_vyWatchT2 = -1;  // --vywriter2 <t>: a second (control) tick
#define VPSET(x) (::dp::g_vpTrail[::dp::g_vpWrites & 7] = __LINE__, ++::dp::g_vpWrites, (x))

struct CoinLive {
    double cx, cy, hw, hh;
    int bit;
};

struct StepCtx {
    double x, xPrev;
    float dxF;         // this layer's per-tick x advance (see advanceX)
    long long t;
    // (the flying band lives in State now -- see State::bandFloor)
    const std::vector<const Obj*>* near;
    const std::vector<const Obj*>* ports;
    const std::vector<const Obj*>* pads;
    const std::vector<const Obj*>* orbs;
    const std::vector<const Obj*>* slopes;
    const std::vector<const Obj*>* speeds;
    const gdapprox::ShipParams* SP;
    const gdapprox::ShipParams* SPmini;
    const gdapprox::UfoParams* UP;
    const gdapprox::UfoParams* UPmini;
    const std::vector<std::pair<const TouchTrig*, TouchMask>>* trigs = nullptr;
    const std::vector<CoinLive>* coins = nullptr;
    const SlopeVetoIndex* veto = nullptr;
    const std::vector<const Obj*>* nearSolid = nullptr;
    const std::vector<FollowRef>* fol = nullptr;
};

inline bool vetoOf(const StepCtx& K, const Obj* const& o, double px, double py,
                   double pHalfW, double pHalfH, bool faceIsTop, double prevX,
                   double prevY, int curSlopeUid, bool wasOnSlope = false) {
    const SlopeVetoIndex* V = K.veto;
    if (V && K.near && V->near == K.near && V->slopes == K.slopes) {
        const uintptr_t a = (uintptr_t)&o, b = (uintptr_t)K.near->data();
        const size_t i = (size_t)(a - b) / sizeof(const Obj*);
        if (a >= b && i < K.near->size() && (a - b) % sizeof(const Obj*) == 0) {
            if (V->off[i] == V->off[i + 1]) return false;
            const Obj* const* rs = V->ramps.data();
            return slopeVetoesSolidIn(o, rs + V->off[i], rs + V->off[i + 1], px, py,
                                      pHalfW, pHalfH, faceIsTop, prevX, prevY,
                                      curSlopeUid, false, nullptr, wasOnSlope);
        }
    }
    return slopeVetoesSolid(o, K.slopes, px, py, pHalfW, pHalfH, faceIsTop, prevX,
                            prevY, curSlopeUid, false, nullptr, wasOnSlope);
}

inline thread_local const char* g_deadWhy = "";
inline thread_local const Obj* g_deadObj = nullptr;
inline std::atomic<long long> g_envKills{0};
inline thread_local uint8_t g_nearOrb = 0;
inline thread_local double g_dashVy = 0.0;
inline thread_local uint8_t g_dashVySet = 0;
inline thread_local const char* g_clampWhy = "";
inline thread_local int g_clampUid = -1;
inline thread_local float g_clampCx = 0.f, g_clampCy = 0.f;
#define CLAMP0(why) do { g_clampWhy = (why); } while (0)
#define CLAMP0O(why, ob) do { \
    g_clampWhy = (why); g_clampUid = (ob)->uid; \
    g_clampCx = (float)(ob)->cx; g_clampCy = (float)(ob)->cy; } while (0)
inline thread_local float g_deadCx = 0.f, g_deadCy = 0.f;
enum : uint8_t { kDeathPhysics = 0, kDeathRule = 1, kDeathPrune = 2 };
inline thread_local uint8_t g_deadKind = kDeathPhysics;
#define DIE(why, obj) DIE_AS(kDeathPhysics, why, obj)
#define DIE_AS(kind, why, obj) do { dead = true; g_deadKind = (kind); g_deadWhy = (why); \
    if (g_inRecon) noteReconDeath(why); \
    const Obj* dobj_ = (obj); g_deadObj = dobj_; \
    if (dobj_) { g_deadCx = (float)dobj_->cx; g_deadCy = (float)dobj_->cy; \
        if (dobj_->uid >= kEnvTwinUidBase) g_envKills.fetch_add(1, std::memory_order_relaxed); } \
    else { g_deadCx = g_deadCy = 0.f; } \
    } while (0)

inline const gdapprox::ShipParams& shipParamsFor(float dxF, bool mini,
                                                 const StepCtx& K) {
    static const gdapprox::ShipParams kSlow =
        gdapprox::ShipParams::normal().withSpeed(0.7);
    static const gdapprox::ShipParams kSlowMini =
        gdapprox::ShipParams::mini().withSpeed(0.7);
    static const gdapprox::ShipParams kFast =
        gdapprox::ShipParams::normal().withSpeed(1.1);
    static const gdapprox::ShipParams kFastMini =
        gdapprox::ShipParams::mini().withSpeed(1.1);
    static const gdapprox::ShipParams kFaster =
        gdapprox::ShipParams::normal().withSpeed(1.3);
    static const gdapprox::ShipParams kFasterMini =
        gdapprox::ShipParams::mini().withSpeed(1.3);
    if (g_rawValues) {
        auto raw = [](gdapprox::ShipParams p, bool m) {
            const double G0 = 0.958199024 * 0.225, chi = m ? 0.85 : 1.0;
            p.holdStrong = G0 * 0.5 / chi;
            p.holdWeak = G0 * 0.4 / chi;
            p.releaseStrong = -G0 * 1.2 * 0.4 / chi;
            p.releaseWeak = -G0 * 0.8 * 0.4 / chi;
            return p;
        };
        static const gdapprox::ShipParams rS = raw(kSlow, false), rSm = raw(kSlowMini, true),
            rN = raw(gdapprox::ShipParams::normal(), false),
            rNm = raw(gdapprox::ShipParams::mini(), true), rF = raw(kFast, false),
            rFm = raw(kFastMini, true), rX = raw(kFaster, false), rXm = raw(kFasterMini, true);
        if (dxF < 1.2f) return mini ? rSm : rS;
        if (dxF >= 1.45f && dxF < 1.8f) return mini ? rFm : rF;
        if (dxF >= 1.8f) return mini ? rXm : rX;
        return mini ? rNm : rN;
    }
    // 0.7's per-tick advance is 251.16/240 = 1.0465; the next one up is 1.29825
    if (dxF < 1.2f) return mini ? kSlowMini : kSlow;
    if (dxF >= 1.45f && dxF < 1.8f) return mini ? kFastMini : kFast;
    if (dxF >= 1.8f) return mini ? kFasterMini : kFaster;   // 1.950 / 2.400
    return mini ? *K.SPmini : *K.SP;
}
inline const gdapprox::UfoParams& ufoParamsFor(float dxF, bool mini,
                                               const StepCtx& K) {
    static const gdapprox::UfoParams kUfoSlow =
        gdapprox::UfoParams::normal().withSpeed(0.7);
    static const gdapprox::UfoParams kUfoSlowMini =
        gdapprox::UfoParams::mini().withSpeed(0.7);
    static const gdapprox::UfoParams kUfoFast =
        gdapprox::UfoParams::normal().withSpeed(1.1);
    static const gdapprox::UfoParams kUfoFastMini =
        gdapprox::UfoParams::mini().withSpeed(1.1);
    static const gdapprox::UfoParams kUfoFaster =
        gdapprox::UfoParams::normal().withSpeed(1.3);
    static const gdapprox::UfoParams kUfoFasterMini =
        gdapprox::UfoParams::mini().withSpeed(1.3);
    if (g_rawValues) {
        auto raw = [](gdapprox::UfoParams p, bool m) {
            const double G0 = 0.958199024 * 0.225, chi = m ? 0.85 : 1.0;
            p.gravityWeak = -G0 * 0.4 / chi;
            p.gravityStrong = -G0 * 0.6 / chi;
            return p;
        };
        static const gdapprox::UfoParams rS = raw(kUfoSlow, false),
            rSm = raw(kUfoSlowMini, true), rF = raw(kUfoFast, false),
            rFm = raw(kUfoFastMini, true), rX = raw(kUfoFaster, false),
            rXm = raw(kUfoFasterMini, true), rN = raw(gdapprox::UfoParams::normal(), false),
            rNm = raw(gdapprox::UfoParams::mini(), true);
        if (dxF < 1.2f) return mini ? rSm : rS;
        if (dxF >= 1.45f && dxF < 1.8f) return mini ? rFm : rF;
        if (dxF >= 1.8f) return mini ? rXm : rX;
        return mini ? rNm : rN;
    }
    if (dxF < 1.2f) return mini ? kUfoSlowMini : kUfoSlow;
    if (dxF >= 1.45f && dxF < 1.8f) return mini ? kUfoFastMini : kUfoFast;
    if (dxF >= 1.8f) return mini ? kUfoFasterMini : kUfoFaster;
    return mini ? *K.UPmini : *K.UP;
}
inline const gdapprox::ShipParams& shipParamsG(float dxF, bool mini, const StepCtx& K,
                                               double gm) {
    const gdapprox::ShipParams& base = shipParamsFor(dxF, mini, K);
    if (gm == 1.0) return base;
    thread_local gdapprox::ShipParams sc;
    sc = base;
    const double G0 = 0.958199024 * 0.225, chi = mini ? 0.85 : 1.0;
    sc.releaseStrong = -G0 * 1.2 * 0.4 / chi * gm;
    sc.releaseWeak = -G0 * 0.8 * 0.4 / chi * gm;
    return sc;
}
inline const gdapprox::UfoParams& ufoParamsG(float dxF, bool mini, const StepCtx& K, double gm) {
    const gdapprox::UfoParams& base = ufoParamsFor(dxF, mini, K);
    if (gm == 1.0) return base;
    thread_local gdapprox::UfoParams sc;
    sc = base;
    const double G0 = 0.958199024 * 0.225, chi = mini ? 0.85 : 1.0;
    sc.gravityWeak = -G0 * 0.4 / chi * gm;
    sc.gravityStrong = -G0 * 0.6 / chi * gm;
    return sc;
}
inline const gdapprox::ShipParams& shipParamsWarp(const gdapprox::ShipParams& p, double t) {
    if (t == 1.0) return p;
    thread_local gdapprox::ShipParams sc;
    sc = p;
    sc.holdStrong *= t;
    sc.holdWeak *= t;
    sc.releaseStrong *= t;
    sc.releaseWeak *= t;
    return sc;
}
inline const gdapprox::UfoParams& ufoParamsWarp(const gdapprox::UfoParams& p, double t) {
    if (t == 1.0) return p;
    thread_local gdapprox::UfoParams sc;
    sc = p;
    sc.gravityWeak *= t;
    sc.gravityStrong *= t;
    return sc;
}
// ...and the swing's gravity (G0 x 0.4, mini G0 x 0.6: kSwingG / kSwingGMini unrounded)
inline double swingGm(bool mini, double gm) {
    if (gm == 1.0) return swingG(mini);
    return 0.958199024 * 0.225 * (mini ? 0.6 : 0.4) * gm;
}

inline bool spiderTargetGd(const StepCtx& K, double x, double fromY, bool flip,
                           double pHalf, double searchHalfX, double& outY,
                           bool worldFloor, bool* outHaz, bool rev, bool bandWall,
                           double bandLo, double bandHi, double hazStrip = 4.0,
                           bool* outSolid = nullptr, bool atFromOk = false) {
    const double fallbackAhead = atFromOk ? -0.5 : 0.5;
    const double gs = flip ? -1.0 : 1.0;
    const auto hazHalfX = [&](const Obj* o) {
        return o->type != 0 && o->radius > 0.0 ? o->radius : o->hw;
    };
    const auto hazHalfY = [&](const Obj* o) {
        return o->type != 0 && o->radius > 0.0 ? o->radius : o->hh;
    };
    const auto inReach = [&](const Obj* o) {
        const double hy = hazHalfY(o);
        if (flip ? o->cy - hy > fromY - pHalf + 2.0 : o->cy + hy < fromY + pHalf - 2.0)
            return false;
        return !(bandWall && (flip ? o->cy + hy < bandLo : o->cy - hy > bandHi));
    };
    constexpr int kN = 64;
    const Obj* strip[kN];
    const Obj* wide[kN];
    int nStrip = 0, nWide = 0;
    const Obj* haz = nullptr;
    const double s0 = rev ? x - pHalf - searchHalfX : x;
    const double s1 = rev ? x - pHalf : x + searchHalfX;
    const double w0 = x - pHalf, w1 = x + pHalf + 4.0;
    for (const Obj* o : *K.near) {
        if (!inReach(o)) continue;
        const double lo = o->cx - o->hw, hi = o->cx + o->hw;
        if (o->type != 0) {
            const double hx = hazHalfX(o);
            if (o->cx + hx < x - hazStrip || o->cx - hx > x + hazStrip) continue;
            if (!haz || (o->cy - haz->cy) * gs < 0.0) haz = o;   // nearest first
            continue;
        }
        if (!(hi < s0 || lo > s1) && nStrip < kN) strip[nStrip++] = o;
        if (!(hi < w0 || lo > w1) && nWide < kN) wide[nWide++] = o;
    }
    if (g_spiderSlopeLine && K.slopes)
        for (const Obj* o : *K.slopes) {
            if (!inReach(o)) continue;
            const double lo = o->cx - o->hw, hi = o->cx + o->hw;
            if (!(hi < s0 || lo > s1) && nStrip < kN) strip[nStrip++] = o;
            if (!(hi < w0 || lo > w1) && nWide < kN) wide[nWide++] = o;
        }
    // step 2 over one list, then the fallback faces
    bool pickedSolid = false;   // the last pick's answer came from an object's face
    const auto pick = [&](const Obj* const* L, int n, double& y) {
        bool f = false;
        pickedSolid = false;
        for (int i = 0; i < n; ++i) {
            const Obj* o = L[i];
            double faceY = flip ? o->cy + o->hh : o->cy - o->hh;
            // --spiderslopeline (constants.hpp): a ramp facing the search is met at its line
            if (g_spiderSlopeLine && o->slope && o->hw > 0.0) {
                const bool ceilLike = o->slopeDir < 7 && ((0x6au >> o->slopeDir) & 1u);
                if (ceilLike != flip)
                    faceY = o->sy0 + (o->sy1 - o->sy0) * (x - (o->cx - o->hw)) / (2.0 * o->hw);
            }
            const double rest = faceY - gs * pHalf;
            if (g_slopeDbg)
                std::printf("spidertgt x=%.3f from=%.3f flip=%d uid=%d slope=%d dir=%d face=%.3f rest=%.3f\n",
                            x, fromY, (int)flip, o->uid, (int)o->slope, (int)o->slopeDir, faceY, rest);
            if ((rest - fromY) * gs < -10.0) continue;
            if (f && (rest - fromY) * gs >= (y - fromY) * gs) continue;
            if (rev ? o->cx + o->hw < x : o->cx - o->hw > x) {
                bool blocked = false;
                for (int j = 0; j < n && !blocked; ++j) {
                    const Obj* b = L[j];
                    if (b == o) continue;
                    blocked = !(x + pHalf < b->cx - b->hw || b->cx + b->hw < x - pHalf
                                || rest + pHalf < b->cy - b->hh + 2.0
                                || b->cy + b->hh - 2.0 < rest - pHalf);
                }
                if (blocked) continue;
            }
            y = rest;
            f = true;
        }
        pickedSolid = f;
        if (!f && flip && worldFloor && !bandWall) {
            const double rest = kGroundY + pHalf;
            if (fromY - rest > fallbackAhead) { y = rest; f = true; }
        }
        if (!f && bandWall) {
            const double rest = flip ? bandLo + pHalf : bandHi - pHalf;
            if (flip ? fromY - rest > fallbackAhead : rest - fromY > fallbackAhead) { y = rest; f = true; }
            else if (g_spiderBandBack) { y = rest; f = true; }
        }
        return f;
    };
    const auto hazNearer = [&](bool f, double y) {
        return haz && (!f || (haz->cy - y) * gs < 0.0);
    };
    double y = 0.0;
    bool f = false, onHaz = false, solid = false;
    if (nStrip == 0 && haz) {
        if (nWide == 0) onHaz = true;
        else {
            f = pick(wide, nWide, y);
            solid = pickedSolid;
            onHaz = hazNearer(f, y);
        }
    } else {
        f = pick(strip, nStrip, y);
        solid = pickedSolid;
        if (hazNearer(f, y)) {
            double y2 = 0.0;
            const bool f2 = nWide ? pick(wide, nWide, y2) : false;
            if (nWide == 0 || hazNearer(f2, y2)) onHaz = true;
            else { y = y2; f = f2; solid = pickedSolid; }
        }
    }
    if (onHaz) { y = haz->cy; f = true; solid = false; }
    if (outHaz) *outHaz = f && onHaz;
    if (outSolid) *outSolid = f && solid;
    outY = y;
    return f;
}

inline bool spiderTargetY(const StepCtx& K, double x, double fromY, bool flip,
                          bool mini, double pHalf, double& outY,
                          bool worldFloor = true, bool* outHaz = nullptr,
                          int frame = 0, bool rev = false, bool bandWall = false,
                          double bandLo = 0.0, double bandHi = 1e9,
                          bool* outSolid = nullptr, bool atFromOk = false) {
    const double gs = flip ? -1.0 : 1.0;
    const double searchHalfX = mini ? kSpiderSearchHalfXMini : kSpiderSearchHalfX;
    // frame 0 at normal size: GD's own search, step by step (above)
    if (frame % 2 == 0 && !mini)
        return spiderTargetGd(K, x, fromY, flip, pHalf, searchHalfX, outY, worldFloor,
                              outHaz, rev, bandWall, bandLo, bandHi, 4.0, outSolid, atFromOk);
    if (frame % 2 == 0 && mini)
        return spiderTargetGd(K, x, fromY, flip, pHalf, pHalf + 1.0, outY, worldFloor,
                              outHaz, rev, bandWall, bandLo, bandHi, 4.0 * 0.6, outSolid,
                              atFromOk);
    double bestY = 0.0;
    bool found = false, bestHaz = false, bestSolid = false;
    for (const Obj* o : *K.near) {
        const bool haz = (o->type != 0);
        if (haz && o->radius > 0.0) continue;   // saws are circles, not faces
        const double halfX = haz ? pHalf : searchHalfX;
        if (frame % 2 != 0) {
            const double xF = x + (double)K.dxF;
            if (haz) {
                if (o->cx + o->hw < xF - 4.0 || o->cx - o->hw > xF + 4.0)
                    continue;
            } else {
                if (o->cx + o->hw < xF + pHalf
                    || o->cx - o->hw > xF + 2.0 * pHalf + 1.0)
                    continue;
            }
        } else if (true) {
            if (!mini && !rev) {   // GD's srect / drect
                if (haz) {
                    if (o->cx + o->hw < x - 4.0 || o->cx - o->hw > x + 4.0) continue;
                } else {
                    if (o->cx + o->hw < x || o->cx - o->hw > x + searchHalfX) continue;
                }
            } else if (std::fabs(x - o->cx) > o->hw + halfX) {
                continue;
            }
        } else {
            double bLo = x - 150.0, bHi = x + 150.0;
            if (startBandUsable()) {
                const double u0 = frameU(frame, 0.0, g_startBandFloor);
                const double u1 = frameU(frame, 0.0, g_startBandCeil);
                bLo = std::min(u0, u1); bHi = std::max(u0, u1);
            }
            if (o->cx + o->hw < bLo || o->cx - o->hw > bHi) continue;
        }
        const double face = flip ? (o->cy + o->hh) : (o->cy - o->hh);
        const double rest = haz ? o->cy : (face - gs * pHalf);
        if ((rest - fromY) * gs <= 0.5) continue;   // not ahead of us
        if (!found || (rest - fromY) * gs < (bestY - fromY) * gs) {
            bestY = rest;
            bestHaz = haz;
            bestSolid = !haz;
            found = true;
        }
    }
    if (outHaz) *outHaz = found && bestHaz;
    if (flip && worldFloor) {
        const double rest = kGroundY + pHalf;
        if (fromY - rest > 0.5 && (!found || rest > bestY)) {
            bestY = rest;
            bestSolid = false;
            found = true;
        }
    }
    if (bandWall && frame == 0) {
        const double rest = flip ? bandLo + pHalf : bandHi - pHalf;
        const bool ahead = flip ? (fromY - rest > 0.5) : (rest - fromY > 0.5);
        const bool beyond = found && (flip ? bestY < rest : bestY > rest);
        if (ahead && (!found || beyond)) {
            bestY = rest;
            bestSolid = false;
            found = true;
            if (outHaz) *outHaz = false;
        }
    }
    if (outSolid) *outSolid = found && bestSolid;
    outY = bestY;
    return found;
}

inline void seekOtherFrames(const Level* cur, int t) {
    if (g_rotTrig.empty()) return;
    for (int f = 0; f < 4; ++f) {
        Level* lv = levelOfFrame(f);
        if (lv && lv != cur) lv->dyn.seek(t);
    }
}

inline double portalBandY(const Obj* p) {
    if (!g_portalStartY) return p->cy;
    const auto at = reinterpret_cast<std::uintptr_t>(p);
    for (int f = 0; f < 4; ++f) {
        const Level* lv = levelOfFrame(f);
        if (!lv || lv->dyn.objs.empty()) continue;
        const Dynamics& D = lv->dyn;
        const auto lo = reinterpret_cast<std::uintptr_t>(D.objs.data());
        const auto hi = reinterpret_cast<std::uintptr_t>(D.objs.data() + D.objs.size());
        if (at < lo || at >= hi) continue;
        const size_t i = (size_t)(p - D.objs.data());
        return i < D.baseCy.size() ? (double)D.baseCy[i] : p->cy;
    }
    return p->cy;
}

inline int applyRotation(State& c, double uPrev, double dxUsed, long long t,
                         int input = 0, bool wasGrounded = false,
                         double sPrevY = 0.0, bool havePrevY = false) {
    if (g_rotTrig.empty()) return -1;
    const int f0 = (int)c.frame;
    const int rev0 = (int)c.rev;   // reverse travel is a 180-degree heading = true frame +2
    int nf = -1;
    int nflip = -1;
    const double vColl = (input == 1 && c.mode == 6 && wasGrounded && havePrevY)
        ? sPrevY : (double)c.y;
    bool qFired = false;
    float qModY = 1.0f;
    uint8_t qOvr = 0;
    uint8_t qNoSlide = 0;
    if (g_rotQueue && !g_rotQ.empty()) {
        double wx, wy;
        fromFrame(f0, (double)c.xAbs, (double)c.y, wx, wy);
        for (int guard = 0; guard < 64; ++guard) {
            const int ch = (int)c.rotChan & 15;
            const int beg = g_rotQBeg[(size_t)ch], end = g_rotQEnd[(size_t)ch];
            if (end <= beg) break;                       // no such channel
            const int done = popCount32(c.rotSpent & g_rotQChanMask[(size_t)ch]);
            const int idx = beg + done;
            if (idx >= end) break;                       // (A) consumed to the end
            const RotQEntry& e = g_rotQ[(size_t)idx];
            const bool rev = ((c.rotRev >> ch) & 1u) != 0;
            const bool vertical = (f0 & 1) != 0;
            const double ref = vertical ? wy : wx;
            const double p = vertical ? e.py : e.px;
            if (!rev) { if (ref < p) break; }
            else      { if (p < ref) break; }
            if (idx < 32) c.rotSpent |= (uint32_t)1 << idx;
            if (g_rotQToggle && (size_t)idx < g_rotQOff.size()
                && popCount32((uint32_t)c.trig.word(0) & g_rotQOff[(size_t)idx])
                       > popCount32((uint32_t)c.trig.word(0) & g_rotQOn[(size_t)idx]))
                continue;
            if (e.id == 2900 && e.swarm && e.swch >= 0 && e.swch <= 15) {
                c.rotChan = (uint8_t)e.swch;
                const int gd = (e.rotIdx >= 0)
                    ? g_rotTrig[(size_t)e.rotIdx].gndDir : 0;
                if ((unsigned)(gd - 2) < 2u) c.rotRev |= (uint16_t)(1u << e.swch);
                else c.rotRev &= (uint16_t)~(1u << e.swch);
            }
            // ...and the rotation half, which a channel-only trigger skips.
            if (!e.chanOnly && e.rotIdx >= 0) {
                const RotTrig& r = g_rotTrig[(size_t)e.rotIdx];
                if (r.setRev >= 0) c.rev = (uint8_t)r.setRev;
                nf = r.frame;
                nflip = r.setFlip;
                qModY = r.vmodY;
                qOvr = r.ovrVel;
                qNoSlide = r.dontSlide;
                qFired = true;
                if (g_rotWatchLo >= 0) g_rotWatchUid = r.uid;   // --rotwatch, print only
                break;
            }
        }
    }
    const bool qUsed = (g_rotQueue && !g_rotQ.empty());
    const RotTrig* best = nullptr;   // the matching trigger nearest in the perpendicular axis
    double bestDv = 1e18;
    float rotModY = 1.0f;          // vy multiplier of the adopted trigger (RotTrig::vmodY)
    uint8_t rotOvr = 0;
    uint8_t rotNoSlide = 0;        // --rotboost: the adopted trigger's Dont Slide (RotTrig::dontSlide)
    bool toggleRev = false;
    for (const RotTrig& r : g_rotTrig) {
        if (qUsed) break;   // the queue above already decided
        if (g_rotQToggle) {
            const size_t k = (size_t)(&r - g_rotTrig.data());
            if (k < g_rotTrigOff.size()
                && popCount32((uint32_t)c.trig.word(0) & g_rotTrigOff[k])
                   > popCount32((uint32_t)c.trig.word(0) & g_rotTrigOn[k]))
                continue;
        }
        const bool same = (r.setRev < 0) ? (r.frame == f0)
                                         : (r.frame == f0
                                            && r.setRev == (int)c.rev);
        const int shot = same ? r.revT : r.firedT;
        if (shot >= 0 && shot != (int)t) continue;
        const double ru = frameU(f0, r.cx, r.cy);
        const bool crossed = c.rev
            ? (uPrev > ru && (double)c.xAbs <= ru)
            : (uPrev < ru && (double)c.xAbs >= ru);
        if (!crossed) continue;
        double wxP, wyP;
        fromFrame(f0, (double)c.xAbs, vColl, wxP, wyP);
        if (r.cx > wxP + 30.0) continue;
        const double dv = std::fabs(vColl - frameV(f0, r.cx, r.cy));
        if (dv > kRotPerpWin) continue;
        if (same) { if (r.setRev < 0) toggleRev = true; continue; }
        if (dv < bestDv) {
            bestDv = dv;
            best = &r;
            if (g_rotWatchLo >= 0) g_rotWatchUid = r.uid;   // --rotwatch, print only
            if (r.setRev >= 0) c.rev = (uint8_t)r.setRev;   // absolute value
            nf = r.frame;
            nflip = r.setFlip;
            rotModY = r.vmodY;
            rotOvr = r.ovrVel;
            rotNoSlide = r.dontSlide;
        }
    }
    if (qUsed) {
        if (!qFired) return -1;
        rotModY = qModY;
        rotOvr = qOvr;
        rotNoSlide = qNoSlide;
    } else if (toggleRev) {
        c.rev = (uint8_t)!c.rev;
    }
    if (nf < 0) return -1;
    if (nf == f0) {
        if (g_slopeDbg)
            std::printf("rot2900 t=%lld site=samefr uid=%d nf=%d f0=%d "
                        "nflip=%d flip=%d->%d vy=%.4f\n",
                        (long long)t, best ? best->uid : -1, nf, f0, nflip,
                        (int)c.flip, (nflip >= 0 ? nflip : (int)c.flip),
                        (double)c.vy);
        if (nflip >= 0 && (uint8_t)nflip != c.flip)
            VYSET(c.vy) = (float)((double)c.vy * 0.5);
        if (nflip >= 0) c.flip = (uint8_t)nflip;
        return nf;
    }
    const bool reTap = (input == 1 && c.mode == 6 && wasGrounded);
    if (reTap && havePrevY) YSET(c.y) = (float)sPrevY;
    double X, Y;
    fromFrame(f0, (double)c.xAbs, (double)c.y, X, Y);
    double nu, nv;
    toFrame(nf, X, Y, nu, nv);            // no origin correction is needed
    c.xAbs = (float)nu;
    YSET(c.y) = (float)nv;
    const int ef0 = (f0 + 2 * rev0) & 3;
    const int enf = (nf + 2 * (int)c.rev) & 3;
    if (((ef0 ^ enf) & 1) != 0) {
        double vyGd = (double)rotModY * std::fabs(dxUsed) / 0.25;
        if (rotOvr) vyGd = (double)rotModY;   // absolute assignment (no example in lv22)
        VYSET(c.vy) = (float)(vyGd * (enf == 3 ? -1.0 : 1.0));
        if (g_rotBoost) c.boost = rotNoSlide ? 0 : 1;
    }
    if ((uint8_t)nf != c.frame) c.frameChg = 1;
    c.frame = (uint8_t)nf;
    if (g_slopeDbg)
        std::printf("rot2900 t=%lld site=newframe uid=%d nf=%d f0=%d "
                    "nflip=%d flip=%d->%d vy=%.4f\n",
                    (long long)t, best ? best->uid : -1, nf, f0, nflip,
                    (int)c.flip, (nflip >= 0 ? nflip : (int)c.flip),
                    (double)c.vy);
    if (nflip >= 0) c.flip = (uint8_t)nflip;
    c.grounded = 0;
    c.snapObj = nullptr;
    c.snapDist = 0.f;
    c.onSlope = 0;
    c.slopeM = 0.f;
    c.usedOrb = nullptr;
    c.usedOrbOld = -1;
    for (int i = 0; i < 3; ++i) c.usedOrbHist[i] = -1;
    for (int i = 0; i < 4; ++i) c.touchRing[i] = -1;
    c.touchRingT = 0;
    c.bandFloor = 0.f;
    c.bandCeil = 1e9f;
    c.bandRefY = 0.f;
    if (reTap) {
        Level* nlv = levelOfFrame(nf);
        if (nlv) {
            const double pH = c.mini ? kSpiderHalfMini : kSpiderHalf;
            // the new frame's near list around the landing column
            std::vector<const Obj*> nr;
            const double lo = (double)c.xAbs - 40.0, hi = (double)c.xAbs + 40.0;
            auto lb = std::lower_bound(
                nlv->objs.begin(), nlv->objs.end(), lo - 80.0,
                [](const Obj& o, double v) { return o.cx < v; });
            for (auto it = lb; it != nlv->objs.end() && it->cx <= hi + 80.0; ++it)
                nr.push_back(&*it);
            nlv->dyn.collect(Dynamics::NEAR, lo, hi, nr);
            StepCtx NK{};
            NK.near = &nr;
            NK.t = t;
            double bestY = 0.0;
            bool hazTgt = false;
            if (spiderTargetY(NK, (double)c.xAbs, (double)c.y, c.flip != 0,
                              c.mini != 0, pH, bestY, nf == 0, &hazTgt, nf,
                              c.rev != 0)
                && !hazTgt) {
                const double gs = c.flip ? -1.0 : 1.0;
                YSET(c.y) = (float)bestY;
                c.flip = c.flip ? 0 : 1;
                VYSET(c.vy) = (float)(1.0 * gs);   // the teleport's own +-1.000
                c.spiderJumpT = 0;   // GD's +0x820 (State::spiderJumpT)
                c.grounded = 1;
                c.snapObj = nullptr;
            }
        }
    }
    return nf;
}

inline bool portalWasInsideAtPrev(const Obj& p, double xPrev, double yPrev,
                                  double half, double pRot) {
    return std::fabs(xPrev - p.cx) <= p.hw + half
           && std::fabs(yPrev - p.cy) - (p.hh + half) < 0.0
           && (!p.oriented || orientedHit(p, xPrev, yPrev, half, pRot));
}

inline bool portalOnFrameRotTrigger(const Obj& p, int frame) {
    if (frame == 0 || g_rotTrig.empty()) return false;
    double wpx, wpy;
    fromFrame(frame, p.cx, p.cy, wpx, wpy);
    for (const RotTrig& r : g_rotTrig)
        if (r.frame == frame && std::fabs(r.cx - wpx) <= 60.0
            && std::fabs(r.cy - wpy) <= 60.0)
            return true;
    return false;
}

inline bool isModePortalType(int type) {
    return type == 5 || type == 6 || type == 16 || type == 19 || type == 26 || type == 27
        || type == 33 || type == 41 || type == 17 || type == 18;
}
inline int modePortalTarget(int type) {
    switch (type) {
        case 6: return 0;    // cube
        case 5: return 1;    // ship
        case 16: return 2;   // ball
        case 19: return 3;   // UFO
        case 26: return 4;   // wave
        case 27: return 5;   // robot
        case 33: return 6;   // spider
        case 41: return 7;   // swing
        default: return -1;
    }
}
inline bool flyEarlyRingType(int type) {
    return type == 11 || type == 12 || type == 13 || type == 29 || type == 35 || type == 32
        || (g_flyDashRing && (type == 37 || type == 38));   // --flydashring (speed.hpp)
}
// Was `uid` fired before usedOrbOld? (speed.hpp, the note that was --ringonce's)
inline bool ringFiredBefore(const State& s, int uid) {
    if (uid < 0) return false;
    for (int i = 0; i < 3; ++i)
        if (s.usedOrbHist[i] == uid) return true;
    return false;
}
inline void noteRingFired(State& c, const Obj* ob) {
    if (g_histStatOn && ob) {
        ++g_hist.orbFired;
        if (std::find(g_histFired.begin(), g_histFired.end(), ob->uid) != g_histFired.end())
            ++g_hist.orbRefire;
        else
            g_histFired.push_back(ob->uid);
        if (c.usedOrb && c.usedOrb != ob && c.usedOrbOld >= 0 && c.usedOrbHist[2] >= 0)
            ++g_hist.orbDrop;
    }
    if (c.usedOrb && c.usedOrb != ob) {
        if (c.usedOrbOld >= 0) {
            c.usedOrbHist[2] = c.usedOrbHist[1];
            c.usedOrbHist[1] = c.usedOrbHist[0];
            c.usedOrbHist[0] = c.usedOrbOld;
        }
        c.usedOrbOld = c.usedOrb->uid;
    }
    c.usedOrb = ob;
}
inline void notePressRingFired(State& c, int uid) {
    if (uid < 0 || c.usedOrbOld == uid || ringFiredBefore(c, uid)) return;
    if (c.usedOrbOld >= 0) {
        c.usedOrbHist[2] = c.usedOrbHist[1];
        c.usedOrbHist[1] = c.usedOrbHist[0];
        c.usedOrbHist[0] = c.usedOrbOld;
    }
    c.usedOrbOld = uid;
}
inline bool flyRingContact(const Obj* ob, double px, double py, double ph, double pRot = 0.0) {
    // --ringrotbox (speed.hpp): a rotated ring against the player's rotated box, all four axes
    if (g_ringRotBox && ob->oriented && pRot != 0.0) return orientedHit(*ob, px, py, ph, pRot);
    if (std::fabs(px - ob->cx) >= ob->hw + ph || std::fabs(py - ob->cy) >= ob->hh + ph)
        return false;
    if (ob->oriented) {
        const double rx = px - ob->cx, ry = py - ob->cy;
        // onto the ring's own axes at -rot (see the late ring loop)
        const double lx = rx * ob->rc + ry * ob->rs;
        const double ly = -rx * ob->rs + ry * ob->rc;
        const double oph = ph * (std::fabs(ob->rc) + std::fabs(ob->rs));
        if (std::fabs(lx) >= ob->ohw + oph || std::fabs(ly) >= ob->ohh + oph) return false;
    }
    return true;
}
inline bool gdCollidesAfter(const Obj& o, double cx, double cy, int uid) {
    const int ox = (int)std::floor(o.cx * 0.01), px = (int)std::floor(cx * 0.01);
    if (ox != px) return ox > px;
    const int oy = (int)std::floor(o.cy * 0.01), py = (int)std::floor(cy * 0.01);
    if (oy != py) return oy > py;
    return o.uid > uid;
}
inline int gdSectionCell(double v) { return v <= 0.0 ? 0 : (int)(v * 0.01); }
inline bool gdPassAfter(const Obj& o, const Obj& ref) {
    const int ox = gdSectionCell(o.cx), rx = gdSectionCell(ref.cx);
    if (ox != rx) return ox > rx;
    if (g_dynamicLevelHeight) {
        const int oy = gdSectionCell(o.cy), ry = gdSectionCell(ref.cy);
        if (oy != ry) return oy > ry;
    }
    return o.uid > ref.uid;
}
inline bool gdRectReReadBetween(const Obj& p, double mcx, double mcy, int muid,
                                const std::vector<const Obj*>* ramps, double px, double py,
                                double half) {
    auto cellY = [](double v) { return g_dynamicLevelHeight ? gdSectionCell(v) : 0; };
    const int cx = gdSectionCell(p.cx), cy = cellY(p.cy);
    const int mx = gdSectionCell(mcx), my = cellY(mcy);
    if (cx != mx) return cx > mx;
    if (cy != my) return cy > my;
    if (p.uid <= muid || !ramps) return false;
    for (const Obj* q : *ramps) {
        if (q->uid <= muid || q->uid >= p.uid) continue;
        if (gdSectionCell(q->cx) != cx || cellY(q->cy) != cy) continue;
        if (std::fabs(px - q->cx) <= 2.0 * q->hw + half && std::fabs(py - q->cy) <= 2.0 * q->hh + half)
            return true;
    }
    return false;
}
// ...and the ramp pass's record of it: the last ramp whose turn of the pass moved the body.
struct PortSeatWatch {
    const float& y;
    float y0;
    const Obj* sp;
    const Obj*& out;
    PortSeatWatch(const float& yRef, const Obj* ramp, const Obj*& mover)
        : y(yRef), y0(yRef), sp(ramp), out(mover) {}
    ~PortSeatWatch() { if (y != y0) out = sp; }
};
inline bool gdTeleAfter(double ox, double oy, int ouid, double tcx, double tcy, int tuid,
                        double px, double py) {
    const bool ySplit = g_dynamicLevelHeight != 0;
    auto cy = [&](double v) { return ySplit ? gdSectionCell(v) : 0; };
    const int ocx = gdSectionCell(ox), ocy = cy(oy);
    if (std::abs(ocx - gdSectionCell(px)) > 1 || std::abs(ocy - cy(py)) > 1) return false;
    const int tx = gdSectionCell(tcx), ty = cy(tcy);
    if (ocx != tx) return ocx > tx;
    if (ocy != ty) return ocy > ty;
    return ouid > tuid;
}
inline void solidOrderInto(const std::vector<const Obj*>& near, std::vector<const Obj*>& out) {
    out.assign(near.begin(), near.end());
    std::stable_sort(out.begin(), out.end(),
                     [](const Obj* a, const Obj* b) { return a->uid > b->uid; });
}
inline const std::vector<const Obj*>& solidWalk(const StepCtx& K, std::vector<const Obj*>& buf) {
    if (K.nearSolid) return *K.nearSolid;
    solidOrderInto(*K.near, buf);
    return buf;
}
inline void noteSolidVisit(const StepCtx& K, const Obj* o, double x, double y, double pHalf) {
    if (g_solidOrdDbgT0 < 0 || K.t < g_solidOrdDbgT0 || K.t > g_solidOrdDbgT1 || o->type != 0) return;
    if (std::fabs(x - o->cx) < o->hw + pHalf && std::fabs(y - o->cy) < o->hh + pHalf)
        std::printf("solidord t=%lld uid=%d\n", (long long)K.t, o->uid);
}
inline bool footFaceContact(const Obj& o, const State& c, double pHalf) {
    if (!(c.mode == 0 || c.mode == 1 || c.mode == 2)) return false;
    const double gs = c.flip ? -1.0 : 1.0;
    const double tol = (c.mode == 1) ? kShipLandTol : kLandTol;
    const double face = c.flip ? (o.cy - o.hh) : (o.cy + o.hh);
    const double pen = (face - ((double)c.y - gs * pHalf)) * gs;
    if (pen >= 0.0 && pen <= tol) return true;
    if (c.mode != 2) return false;
    const double under = c.flip ? (o.cy + o.hh) : (o.cy - o.hh);
    const double headPen = (((double)c.y + gs * pHalf) - under) * gs;
    return headPen >= 0.0 && headPen <= tol;
}
inline bool flipGraceSeat(const Obj& o, const State& c, const State& s, bool tapAfterCollide,
                          double pHalf) {
    if (o.dynObj || s.frame != 0) return false;
    if (!(c.mode == 0 || c.mode == 2 || c.mode == 5 || c.mode == 6)) return false;
    const int age = (gdUpOf(c) != gdUpOf(s) && !tapAfterCollide) ? 0 : (int)s.flipT + 1;
    if (age >= kFlipGraceTicks) return false;
    const double kReach = 10.0;
    if (!c.flip)
        return o.cy - o.hh >= (double)c.y - pHalf + kReach
               || o.cy - o.hh >= (double)s.y - pHalf + kReach;
    return o.cy + o.hh <= (double)c.y + pHalf - kReach
           || o.cy + o.hh <= (double)s.y + pHalf - kReach;
}
inline void seatOnNearFace(const Obj& o, State& c, double pHalf) {
    YSET(c.y) = (float)(c.flip ? o.cy + o.hh + pHalf : o.cy - o.hh - pHalf);
    VYSET(c.vy) = 0.f;
    c.grounded = 1;
}
// --ringorder (speed.hpp): a ring's place in m_touchedRings (State::touchRing), 4 when absent.
inline int touchRank(const State& s, int uid) {
    for (int i = 0; i < 4; ++i)
        if (s.touchRing[i] == uid && uid >= 0) return i;
    return 4;
}
inline void updateTouchRings(State& c, const State& s, const StepCtx& K, double px, double py,
                             double ph) {
    int32_t a[4] = {-1, -1, -1, -1};
    int n = 0;
    for (int i = 0; i < 4; ++i)
        if (s.touchRing[i] >= 0 && ((s.touchRingT >> i) & 1)) a[n++] = s.touchRing[i];
    int32_t fresh[8];
    int nf = 0, nfAll = 0;
    uint8_t touched = 0;
    for (const Obj* ob : *K.orbs) {
        if (ob->uid < 0 || !flyRingContact(ob, px, py, ph, (double)c.rot)) continue;
        int at = -1;
        for (int i = 0; i < n; ++i)
            if (a[i] == ob->uid) at = i;
        if (at >= 0) touched |= (uint8_t)(1u << at);
        else {
            ++nfAll;
            if (nf < 8) fresh[nf++] = ob->uid;
        }
    }
    std::sort(fresh, fresh + nf);
    const int nKept = n;
    for (int j = 0; j < nf && n < 4; ++j) {
        touched |= (uint8_t)(1u << n);
        a[n++] = fresh[j];
    }
    if (g_histStatOn) {   // --histstat (speed.hpp)
        g_hist.touchMax = std::max(g_hist.touchMax, n);
        g_hist.touchDrop += nfAll - (n - nKept);
    }
    for (int i = 0; i < 4; ++i) c.touchRing[i] = a[i];
    c.touchRingT = touched;
}
inline const Obj* flyEarlyRing(const State& s, const StepCtx& K, double stepDx, bool foot = false) {
    (void)stepDx;
    const Obj* pick = nullptr;
    int pickRank = 4;
    for (const Obj* ob : *K.orbs) {
        if (!flyEarlyRingType(ob->type) && (foot || (ob->type != 46 && !(g_flySpiderRing && ob->type == 43))))
            continue;
        const bool reuse = g_multiOrb && ob->multiAct;   // --multiorb (the late loop's note)
        if (!reuse && (ob == s.usedOrb || (ob->uid >= 0 && ob->uid == s.usedOrbOld))) continue;
        if (!reuse && ringFiredBefore(s, ob->uid)) continue;
        // the array pushButton walks is the previous tick's end state's (State::touchRing)
        const int rk = touchRank(s, ob->uid);
        if (g_flyDashRing && (ob->type == 37 || ob->type == 38)
            && (rk >= 4 || !((s.touchRingT >> rk) & 1)))
            continue;
        if (foot && (rk >= 4 || !((s.touchRingT >> rk) & 1))) continue;
        if (rk < pickRank) { pick = ob; pickRank = rk; }
    }
    return pick;
}
inline float tpClosestDirection(float a, float half) {   // GJBaseGameLayer 0x22e8b0
    if (a < -half) return std::ceil(std::floor(std::fabs(a) / half) * 0.5f) * (half + half) + a;
    if (half < a) return a - std::ceil(std::floor(std::fabs(a) / half) * 0.5f) * (half + half);
    return a;
}
inline float tpForceVy(const TpForce& t, float vyIn, uint8_t& boost) {
    if (t.fm & 4) {
        // PlayerObject::redirectPlayerForce (0x39fc60), in its float arithmetic
        const float tgt = t.ang * 0.017453292f;
        float vx = 0.f, vy = vyIn;
        const float d = tpClosestDirection(tgt - std::atan2(vy, vx), 3.1415927f);
        if (d != 0.f) {
            const float cs = std::cos(d), sn = std::sin(d);
            const float nx = vx * cs - vy * sn;
            vy = vy * cs + vx * sn;
            vx = nx;
        }
        vx *= t.rmod;
        vy *= t.rmod;
        const float len = std::sqrt(vx * vx + vy * vy);
        if (t.rmax > 0.f && len > t.rmax) {
            vy *= t.rmax / len;
        } else if (t.rmin > 0.f && len < t.rmin) {
            if (len != 0.f) vy *= t.rmin / len;
            else vy = std::sin(tgt) * t.rmin;
        }
        boost = 1;
        return vy;
    }
    if (t.fm & 1) {
        const bool add = (t.fm & 2) != 0;
        if (t.f == 0.f && !add) {
            boost = 0;
            return 0.f;
        }
        // ccpForAngle, normalised, times the strength (0x2104e1..0x210574)
        const float a = t.ang * 0.017453292f;
        const float px = std::cos(a), py = std::sin(a);
        const float len = std::sqrt(px * px + py * py);
        if (!(len > 0.f)) return vyIn;
        const float fy = py * (t.f / len);
        boost = 1;
        return add ? (float)((double)fy + (double)vyIn) : fy;
    }
    return vyIn;
}
inline float dashSlopeOf(double rot, bool flipX, int frame) {
    if (!g_dashClamp || frame != 0) return (float)std::tan(-rot * 3.14159265358979 / 180.0);
    float a = std::fmod(-((float)rot + (flipX ? 180.f : 0.f)), 360.f);
    if (a > 180.f) a -= 360.f;
    else if (a < -180.f) a += 360.f;
    if (std::fabs(a) > 90.f) {
        a = (a > 0.f ? 180.f : -180.f) - a;
        if (a > 180.f) a -= 360.f;
        else if (a < -180.f) a += 360.f;
    }
    if (a > 70.f) a = 70.f;
    else if (a < -70.f) a = -70.f;
    const float r = a * 0.017453292f;
    return std::sin(r) / std::fabs(std::cos(r));
}
inline float dashSlopeOf(const Obj* ob, int frame) {
    return dashSlopeOf(ob->rot, ob->flipX != 0, frame);
}
inline float dashSlopeClamped(float slope, int frame) {
    if (!g_dashClamp || frame != 0) return slope;
    const float lim = std::sin(70.f * 0.017453292f) / std::cos(70.f * 0.017453292f);
    return slope > lim ? lim : slope < -lim ? -lim : slope;
}
inline void applyFlyEarlyRing(State& c, const Obj* ob, float useDx) {
    if (ob->type == 46) {
        if (!(ob->tpKeep & 2)) c.y = (float)ob->tpEy;
        if (!(ob->tpKeep & 1)) c.xAbs = (float)ob->tpEx;
        int wantUp = -1;
        if (ob->tpGrav == 1) wantUp = 0;
        else if (ob->tpGrav == 2) wantUp = 1;
        else if (ob->tpGrav == 3) wantUp = c.flip ? 0 : 1;
        if (wantUp >= 0 && c.flip != (uint8_t)wantUp) {
            c.flip = (uint8_t)wantUp;
            c.vy = c.vy * 0.5f;
            c.grounded = 0;
        }
        c.snapObj = nullptr;
        c.snapDist = 0.f;
        c.onSlope = 0;
        c.slopeM = 0.f;
        noteRingFired(c, ob);
        c.ringHold = 1;
        c.pressSpent = 1;
        c.action = 1;
        c.jumpBuf = 0;
        return;
    }
    if (ob->rev) c.rev = c.rev ? 0 : 1;
    const double ms = (c.mini ? kMiniImpulse : 1.0) * ringScaleFor(useDx);
    if (ob->type == 37 || ob->type == 38) {
        // --flydashring (speed.hpp): the late loop's dash branch, on the state the next tick runs from
        c.dashing = 1;
        c.dashSlope = dashSlopeOf(ob, (int)c.frame);   // --dashclamp
        c.vy = 0.f;
        if (ob->type == 38) c.flip = c.flip ? 0 : 1;
    } else if (ob->type == 13) {
        const double gv = g_rawValues ? rawQ(kCubeJump * 0.8 * ms) * 0.5 : kOrbGravity * ms;
        c.flip = c.flip ? 0 : 1;
        c.vy = (float)(gv * (c.flip ? 1.0 : -1.0));
    } else if (ob->type == 32) {
        const double dv = (c.mode == 3) ? kRingDropUfo : kRingDropFly;
        if ((double)c.vy * (c.flip ? -1.0 : 1.0) >= 2.0) c.pNoTerm = 1;
        c.vy = (float)(c.flip ? dv : -dv);
        c.boost = 1;
    } else if (ob->type == 35 || ob->type == 29) {
        const bool green = (ob->type == 29);
        const double r = green ? ((c.mode == 1) ? kRingGreenShip : kRingGreen)
                         : (c.mode == 1) ? (c.mini ? kRingRedShipMini : kRingRedShip)
                                         : (c.mini ? kRingRedUfoMini : kRingRedUfo);
        if (green) c.flip = c.flip ? 0 : 1;
        const double ov = g_rawValues ? rawQ(kOrbYellow * r * ms) : kOrbYellow * r * ms;
        c.vy = (float)(c.flip ? -ov : ov);
        if (!green && true) c.boost = 1;
    } else {
        const double ratio = (ob->type == 12) ? pinkRingRatio(c.mode) : 1.0;
        const double ov = g_rawValues ? rawQ(kCubeJump * ratio * ms) : kOrbYellow * ratio * ms;
        c.vy = (float)(c.flip ? -ov : ov);
    }
    c.grounded = 0;
    c.rHover = 0;
    if (ob->type != 32) c.a1cLatch = (uint8_t)(c.a1cLatch | 1u);
    noteRingFired(c, ob);
    c.ringHold = 1;
    c.pressSpent = 1;
    c.action = 1;
    c.jumpBuf = 0;
}
inline bool applyFlySpiderRing(State& c, const Obj* ob, const StepCtx& K) {
    if (c.frame != 0) return false;
    const bool sfl = g_spiderOrbFace ? objFacingDown(*ob) : (c.flip != 0);
    double tgt = 0.0;
    if (!spiderTargetY(K, (double)c.xAbs, (double)c.y, sfl, c.mini != 0, playerHalf(c.mode, c.mini != 0), tgt,
                       true, nullptr, 0, c.rev != 0))
        return false;
    c.y = (float)tgt;
    c.flip = sfl ? 0 : 1;
    c.vy = (float)(sfl ? -1.0 : 1.0);
    c.spiderJumpT = 0;
    c.snapObj = nullptr;
    c.snapDist = 0.f;
    c.onSlope = 0;
    c.slopeM = 0.f;
    c.grounded = 0;
    c.rHover = 0;
    c.a1cLatch = (uint8_t)(c.a1cLatch | 1u);   // --a1clatch: every ring but the drop ring
    noteRingFired(c, ob);
    c.ringHold = 1;
    c.pressSpent = 1;
    c.action = 1;
    c.jumpBuf = 0;
    return true;
}

inline bool a1cFallingBugged(double vy, bool flip, bool dual, bool fixedArm, double g2) {
    if (fixedArm) return flip ? (vy > -g2) : (vy < g2);
    return flip ? (vy > g2) : (vy < (dual ? -g2 : g2));
}

inline thread_local int t_followNested = 0;
inline thread_local int t_followHalf2 = 0;
inline thread_local int t_ringLinkLate = 0;
inline thread_local int t_p2RingFresh = -1;

inline bool bandTrackOtherRoute(const State& s, long long t, double fl, double ce, long long age) {
    if (!g_bandTrackSeen || age <= kBandOwnNear) return false;
    const double ownF = (double)s.bandFloor, ownC = (double)s.bandCeil;
    if (std::fabs(fl - ownF) <= 1.0 && std::fabs(ce - ownC) <= 1.0) return false;
    {
        BandKind sk = BandKind::Unknown;
        double sf = 0.0, sc = 0.0;
        long long sa = 0;
        int src = -1;
        if (!bandTrackKindAt(t, sk, sf, sc, sa, &src) || src != 0) return false;
    }
    BandKind pk = BandKind::Unknown;
    double pf = 0.0, pc = 0.0;
    long long pa = 0;
    if (!bandTrackKindAt(t - age - 1, pk, pf, pc, pa) || pk != BandKind::Interval) return false;
    return std::fabs(pf - ownF) <= 1.0 && std::fabs(pc - ownC) <= 1.0;
}

inline bool touchOwnOn(const TouchTrig& T, long long t);
inline bool pressLatTwo(uint8_t mode) { return mode == 1 || mode == 3; }
inline bool pressFreshFires(const TouchTrig& T, const State& par, long long t) {
    if (!g_pressFresh || !T.press || T.offSwitch || T.spawnRing) return false;
    if (!pressLatTwo(par.mode)) return false;
    double bx = T.cx, by = T.cy;
    if (T.followIdx >= 0 && g_touchFollowObjs
        && (size_t)T.followIdx < g_touchFollowObjs->size()) {
        const Obj& fo = (*g_touchFollowObjs)[(size_t)T.followIdx];
        bx += (double)fo.cx - T.followX0;
        by += (double)fo.cy - T.followY0;
    }
    const double h0 = playerHalf(par.mode, par.mini != 0);
    if (!(std::fabs((double)par.xAbs - bx) < T.hw + h0 && std::fabs((double)par.y - by) < T.hh + h0))
        return false;
    return !(g_touchOwnOff && !T.ownTog.empty() && !touchOwnOn(T, t));
}

inline State stepOne(const State& s, int input, const StepCtx& K, bool& dead,
                     bool* linkFlip = nullptr) {
    t_holdHeld = (g_holdTap && g_heldAll && (s.frame & 3) == 0) ? (int8_t)(s.held ? 1 : 0) : (int8_t)-1;
    t_holdX = (double)s.xAbs;
    if (((g_followYOn && (!g_followYs.empty() || !g_followSecObjs.empty())) || !g_bigSecObjs.empty()
         || !g_bigSecDyn.empty())
        && !t_followNested) {
        float fNew[kFollowYSlots] = {s.followF[0], s.followF[1]};
        double fac[kFollowYSlots] = {0.0, 0.0}, facPrev[kFollowYSlots] = {0.0, 0.0};
        int placeCmd[kFollowYSlots] = {-1, -1};   // the trigger whose objects a slot places
        uint8_t okNew = s.followOk;
        for (int sl = 0; sl < kFollowYSlots; ++sl) {
            const int own = followOwner(sl, K.x);
            if (own < 0) continue;
            const FollowY& f = g_followYs[(size_t)own];
            const uint8_t bit = (uint8_t)(1u << sl);
            if (K.xPrev < f.cx) {   // the crossing tick: primed, not moved
                fNew[sl] = (float)f.y0ref;
                okNew = (uint8_t)(okNew | bit);
                continue;
            }
            if (!(s.followOk & bit)) continue;
            const long long n = f.tAct >= 0 ? K.t - f.tAct : -1;
            if (n < 0 || (double)n <= f.durTicks + 1.0) {
                const float p1y = t_followHalf2 ? s.y2 : s.y;
                float d = (p1y + (float)f.offset - s.followF[sl]) * (float)f.k;
                if (f.maxStep > 0.0)
                    d = std::max(-(float)f.maxStep, std::min((float)f.maxStep, d));
                fNew[sl] = (float)((double)s.followF[sl] + (double)d);
            }
            placeCmd[sl] = own;
            if (n >= 0) {
                fac[sl] = std::pow(1.0 - f.k, (double)std::min<long long>(n, 1000000));
                facPrev[sl] = std::pow(1.0 - f.k, (double)std::min<long long>(n - 1, 1000000));
            }
        }
        const StepCtx* KK = &K;
        StepCtx K2;
        thread_local std::vector<const Obj*> wv[6];
        thread_local std::vector<Obj> copies;
        thread_local std::vector<std::pair<const Obj*, const Obj*>> back;   // copy -> level's
        copies.clear();
        back.clear();
        const bool secAll = g_followSection && g_dynamicLevelHeight;
        if (K.fol && (placeCmd[0] >= 0 || placeCmd[1] >= 0 || secAll || g_bigSection)) {
            const std::vector<const Obj*>* src[6] = {K.near, K.ports, K.pads, K.orbs, K.slopes,
                                                     K.speeds};
            bool used[6] = {false, false, false, false, false, false};
            copies.reserve(K.fol->size());   // the pointers into it must not move
            for (const FollowRef& r : *K.fol) {
                if (!src[r.win]) continue;
                const bool bigDyn = r.cmd == kBigSecDynCmd;
                const bool big = r.cmd == kBigSecCmd || bigDyn;
                const int sl = (r.cmd == kFollowSecCmd || big) ? -1 : g_followYs[r.cmd].slot;
                const bool placed = sl >= 0 && placeCmd[sl] == (int)r.cmd;
                const Obj* orig = (*src[r.win])[r.idx];
                const double yFiled = bigDyn ? orig->cy : r.y0;
                const bool farCell = big
                    ? (std::abs(gdSectionCell(orig->cx) - gdSectionCell(K.x)) > 1
                       || (g_dynamicLevelHeight
                           && std::abs(gdSectionCell(yFiled) - gdSectionCell((double)s.y)) > 1))
                    : secAll
                    && (std::abs(gdSectionCell(orig->cx) - gdSectionCell(K.x)) > 1
                        || std::abs(gdSectionCell(r.y0) - gdSectionCell((double)s.y)) > 1);
                if (g_dynDbg == orig->uid)
                    std::printf("followsec t=%lld uid=%d win=%d placed=%d far=%d cell(%d,%d) player(%d,%d) "
                                "y0=%.3f cy=%.3f\n", (long long)K.t, orig->uid, (int)r.win, placed ? 1 : 0,
                                farCell ? 1 : 0, gdSectionCell(orig->cx), gdSectionCell(r.y0),
                                gdSectionCell(K.x), gdSectionCell((double)s.y), r.y0, orig->cy);
                if (!placed && !farCell) continue;
                if (!used[r.win]) {
                    wv[r.win] = *src[r.win];
                    used[r.win] = true;
                }
                copies.push_back(*orig);
                Obj& o = copies.back();
                if (placed) {
                    const FollowY& f = g_followYs[r.cmd];
                    const double y = (double)fNew[sl] + (r.y0 - f.y0ref) * fac[sl];
                    const double yPrev = (double)s.followF[sl] + (r.y0 - f.y0ref) * facPrev[sl];
                    const double dy = y - o.cy;
                    o.cy = y;
                    o.dcy = y - yPrev;
                    if (o.slope) {
                        o.sy0 += dy;
                        o.sy1 += dy;
                    }
                    if (o.type == 28) o.tpY += dy;
                }
                if (farCell) {
                    const double away = 1.0e7;
                    o.cy += away;
                    o.dcy = 0.0;
                    if (o.slope) {
                        o.sy0 += away;
                        o.sy1 += away;
                    }
                    if (o.type == 28) o.tpY += away;
                }
                wv[r.win][r.idx] = &o;
                back.push_back({&o, orig});
            }
            if (!back.empty()) {
                K2 = K;
                if (used[0]) K2.near = &wv[0];
                if (used[1]) K2.ports = &wv[1];
                if (used[2]) K2.pads = &wv[2];
                if (used[3]) K2.orbs = &wv[3];
                if (used[4]) K2.slopes = &wv[4];
                if (used[5]) K2.speeds = &wv[5];
                // built over the level's own lists: the loops sort and walk the copies themselves
                if (used[0]) K2.nearSolid = nullptr;
                if (used[0] || used[4]) K2.veto = nullptr;
                K2.fol = nullptr;
                KK = &K2;
            }
        }
        t_followNested = 1;
        State n = stepOne(s, input, *KK, dead, linkFlip);
        t_followNested = 0;
        for (const auto& cb : back) {
            const auto fix = [&](const Obj*& p) { if (p == cb.first) p = cb.second; };
            fix(n.snapObj);
            fix(n.usedOrb);
            fix(n.snapObj2);
            fix(n.usedOrb2);
            for (int i = 0; i < 4; ++i) {
                fix(n.usedPad[i]);
                fix(n.usedPad2[i]);
            }
        }
        n.followF[0] = fNew[0];
        n.followF[1] = fNew[1];
        n.followOk = okNew;
        return n;
    }
    // --bandholdend (bands.hpp): whether this worldline keeps the recording's last band row
    if (g_bandHoldEnd) g_bandHoldNow = !(s.bandBranch & kBandPastEnd);
    if (!g_ballTapNested && s.mode == 2 && input && !s.action
        && s.onSlope && s.rideLanded && !ctrlBodyInputOffAt(K.t, t_followHalf2 != 0)) {
        // the per-tick diagnostics as the caller left them, put back when the re-run is dropped
        const char* const clampWhy0 = g_clampWhy;
        const int clampUid0 = g_clampUid;
        const float clampCx0 = g_clampCx, clampCy0 = g_clampCy;
        const char* const deadWhy0 = g_deadWhy;
        const Obj* const deadObj0 = g_deadObj;
        const uint8_t deadKind0 = g_deadKind;
        const float deadCx0 = g_deadCx, deadCy0 = g_deadCy;
        const uint8_t nearOrb0 = g_nearOrb, dashVySet0 = g_dashVySet;
        const double dashVy0 = g_dashVy;
        const bool preBtnSet0 = g_preBtnSet;
        const double preBtnY0 = g_preBtnY;
        const int tcBranch0 = g_tcBranch;
        g_ballTapNested = 1;
        g_ballTapLaunch = 0;
        bool deadN = false, flipN = false;
        State n = stepOne(s, input, K, deadN, linkFlip ? &flipN : nullptr);
        const bool launched = g_ballTapLaunch != 0;
        g_ballTapNested = 0;
        g_ballTapLaunch = 0;
        if (launched) {
            dead = deadN;
            if (flipN) *linkFlip = true;
            return n;
        }
        g_clampWhy = clampWhy0;
        g_clampUid = clampUid0;
        g_clampCx = clampCx0;
        g_clampCy = clampCy0;
        g_deadWhy = deadWhy0;
        g_deadObj = deadObj0;
        g_deadKind = deadKind0;
        g_deadCx = deadCx0;
        g_deadCy = deadCy0;
        g_nearOrb = nearOrb0;
        g_dashVySet = dashVySet0;
        g_dashVy = dashVy0;
        g_preBtnSet = preBtnSet0;
        g_preBtnY = preBtnY0;
        g_tcBranch = tcBranch0;
    }
    // ...and inside that re-run, every site that taps the ball on a fresh press stands down.
    const bool freshTapHeld = g_ballTapNested != 0;
    g_vyWriter = 0;
    g_vyWrites = 0;
    g_yWriter = 0;
    g_yWrites = 0;
    g_yIn = s.y;
    g_vyIn = s.vy;
    g_vpWrites = 0;
    g_impCount = 0;
    g_impulseSite = 0;
    g_ballTapped = false;   // --repelland: set again below if this step's ball taps
    if ((unsigned)s.frame < 4)
        ++g_frameRevReach[g_threadSlot & (kThreadSlots - 1)].n[s.frame][s.rev ? 1 : 0];
    State c = s;
    c.tpSkip = 0;   // one tick of life: set again only by this tick's teleport
    // The spider's teleport clock ages a tick; a teleport this tick sets it back to 0.
    c.spiderJumpT = (uint8_t)std::min<int>(spiderJumpGraceTicks(), (int)s.spiderJumpT + 1);
    c.ogLinger = 0; // --upsidecoyote: re-earned every tick by the cube branch
    for (int i = 0; i < 3; ++i) c.portSeen[i] = -1;   // --portalonce: this tick's overlaps
    if (!s.grounded) c.groundUid = -1;
    for (const GpHandoff& h : g_gpHandoff)
        if (K.xPrev < h.x && K.x >= h.x) {
            c.portalLatch &= ~GravLatch::bit(h.bit);
            c.portalLatch2 &= ~GravLatch::bit(h.bit);
        }
    if (ctrlBodyInputOffAt(K.t, t_followHalf2 != 0)) input = 0;
    // --ctrltouch: ...and a touched Options trigger's, from the tick after the touch (State::ctrlT)
    if (g_ctrlTouch && s.ctrlT) input = 0;
    if (!g_flyRingNested && (s.mode == 1 || s.mode == 3) && input
        && !s.jumpBuf && !s.pressSpent && K.orbs) {
        const float ringDx = (s.dx > 0.f) ? s.dx : K.dxF;
        const double stepDx = (double)ringDx * timeWarpAt((double)s.xAbs) * (s.rev ? -1.0 : 1.0);
        if (const Obj* ring = flyEarlyRing(s, K, stepDx)) {
            State s2 = s;
            // --flyspiderring: a spider orb with no surface to go to stays with the late loop
            const bool took = (ring->type == 43) ? applyFlySpiderRing(s2, ring, K)
                                                 : (applyFlyEarlyRing(s2, ring, ringDx), true);
            if (took) {
                g_flyRingNested = 1;
                State r = stepOne(s2, input, K, dead, linkFlip);
                g_flyRingNested = 0;
                if (s.dual && linkFlip && s2.flip != s.flip)
                    *linkFlip = true;
                return r;
            }
        }
    }
    c.jumpBuf = (uint8_t)(input ? 1 : 0);
    const Obj* earlyRingTook = nullptr;
    if (g_earlyRing && input && !s.jumpBuf && !s.pressSpent && K.orbs
        && (s.mode == 0 || s.mode == 2 || s.mode == 5 || s.mode == 6 || s.mode == 7))
        earlyRingTook = flyEarlyRing(s, K, 0.0, true);
    bool spawnRingJumpNow = false;   // --spawnringjump: set where a spawn ring takes the press
    int spawnRingUid = -1;           // ...and which ring that was (--spawnringon)
    bool ringStakedNow = false;
    float preRingStep = 0.f;
    uint8_t preRingNeg = 0;
    // --heldall (speed.hpp): the button level every mode came in with, not only the flying ones
    if (g_heldAll) c.held = (uint8_t)input;
    // --holdlatch: a release, or a fresh press (pushButton sets +0x985), makes the hold live again
    if (!input || !s.action) c.holdDead = 0;
    c.pFlap = 0;   // 1-tick lifetime (see the note on State::pFlap)
    c.lawSv = 0.f;   // same (State::lawSv)
    c.tpArrive = 0;  // same (State::tpArrive)
    // --portalspidertap: the handed-over press lives while the button stays down
    c.pSpiderTap = (uint8_t)((s.pSpiderTap && input) ? 1 : 0);
    c.pExitVy = 0.f;  // same (State::pExitVy, r93)
    c.pNoTerm = 0;    // same (State::pNoTerm, r102)
    const float useDx = (s.dx > 0.f) ? s.dx : K.dxF;
    c.dx = useDx;
    // --bandfloorunstick: fly/bandcarry put a grounded ship back on the band's floor this tick.
    bool bandFloorHeld = false;
    const double tScale = timeWarpAt((double)s.xAbs);
    const double gMod = (g_gravTrig && !g_gravTrigs.empty()) ? gravModAt((double)s.xAbs) : 1.0;
    auto gStepMag = [&](double atOne, bool ballLike, bool robot) -> double {
        if (gMod == 1.0) return atOne;
        return qVy(std::fabs(rawGravStep(ballLike, robot, (double)useDx)) * gMod);
    };
    if (g_staticCamCol) c.bandAnim = bandAnimStep(s.bandAnim);
    float yFreeBeforeSlope = 0.f;
    // --portseatorder: the last ramp of this tick's ride pass that moved the body (PortSeatWatch).
    const Obj* portSeatMover = nullptr;
    bool flyHazRecordOnly = false;
    const Obj* flyHazPending = nullptr;
    // --flyhaztele (speed.hpp): the held hit that stands, decided after the portal pass
    const Obj* flyHazTeleHeld = nullptr;
    float yFree = s.y;
    bool gravHoldOver = false;
    bool rodeFlipped = false;
    int gravFiredUid = -1;
    double ySeatSp = 0.0;
    int seatUidSp = -1;
    bool ceilPressedNow = false;
    double ceilPressM = 0.0;
    uint8_t ceilPressMode = 0;   // --ceilreleasemode: c.mode at the press site
    int ceilPressUid = -1;       // --portalpressorder: the ramp that pressed, by uid
    c.xAbs = advanceX(s.xAbs,
                      (float)(useDx * tScale * (s.rev ? -1.0 : 1.0)));
    const double x = (double)c.xAbs, xPrev = (double)s.xAbs;
    const Obj* tpOrbFired = nullptr;
    const Obj* tpPortalXFired = nullptr;   // --tpportalx: the portal that moves x this tick
    uint8_t tpOrbUndo = 0;
    if (g_lockBox >= 0 && c.trig.test(g_lockBox)
        && (double)((long long)K.t - (long long)c.fireB[g_lockBox] - 1)
               <= g_lockTicks)
        c.lockOff = s.lockOff + (float)(x - xPrev);
    if (g_staticCamCol && !g_staticCams.empty()) {
        const double lo = std::min(xPrev, x), hi = std::max(xPrev, x);
        for (const StaticCam& sc : g_staticCams) {
            if (sc.cx <= lo || sc.cx > hi) continue;
            if (sc.exit_) c.bandBranch &= (uint8_t)~1;   // Exit Static
            else          c.bandBranch = (uint8_t)((c.bandBranch | 1) & ~2);
        }
    }
    double modX = xPrev, modY = (double)s.y;
    if (s.frame & 3) fromFrame((int)s.frame, xPrev, (double)s.y, modX, modY);
    const bool slideTouchStart =
        !g_slideBoxes.empty()
        && slideBoxTouch(modX, modY, playerHalf(s.mode, s.mini != 0),
                         (int)K.t + 1);   // --slidelive: the row the solids have this tick
    c.slideT = slideTouchStart
                   ? (g_slideEnd ? 1 : 0)   // --slideend: the last tick's end touch
                   : (uint8_t)std::min<int>(kArmTicks, (int)s.slideT + 1);
    const bool waveRampArm = g_waveRamp && slideTouchStart;
    const double gdSign = gdUpOf(s) ? -1.0 : 1.0;
    if (!g_deadBands.empty()) {
        double dbX = x, dbY = (double)s.y;
        if (s.frame & 3) fromFrame((int)s.frame, x, (double)s.y, dbX, dbY);
        for (const auto& db : g_deadBands) {
            if (dbX < db.x0 || dbX > db.x1 || dbY < db.y0 || dbY > db.y1)
                continue;
            const bool hit = (db.mode == -1)
                || (db.mode >= 0 && (int)s.mode != db.mode)
                || (db.mode <= -2 && (int)s.mode == -2 - db.mode);
            if (hit) {
                dead = true; g_deadWhy = "deadband"; g_deadObj = nullptr;
                g_deadKind = kDeathPhysics;
                g_deadCx = g_deadCy = 0.f;
                return c;
            }
        }
    }
    if (g_maxPlayY < 1e17) {
        double wy = (double)s.y;
        if (s.frame & 3) {
            double wx;
            fromFrame((int)s.frame, x, (double)s.y, wx, wy);
        }
        if (wy > g_maxPlayY) {
            dead = true; g_deadWhy = "maxplayy"; g_deadObj = nullptr;
            g_deadKind = kDeathPhysics;                     // GD destroys it too (above)
            g_deadCx = g_deadCy = 0.f;                      // see the deadband site
            return c;
        }
    }
    if (g_offBoardMargin > 0.0) {
        double fl = 0.0, ce = 0.0;
        if (bandTrackRowAt(K.t, fl, ce)) {
            double wy = (double)s.y;
            if (s.frame & 3) {
                double wx;
                fromFrame((int)s.frame, x, (double)s.y, wx, wy);
            }
            if (wy > ce + g_offBoardMargin || wy < fl - g_offBoardMargin) {
                dead = true; g_deadWhy = "offboard"; g_deadObj = nullptr;
                g_deadKind = kDeathPrune;                   // the loop's bound, not the game's
                g_deadCx = g_deadCy = 0.f;                  // see the deadband site
                return c;
            }
        }
    }
    const double pHalf = playerHalf(s.mode, c.mini != 0);
    int hazOnceState = -1;          // -1 not asked yet, 0 no, 1 yes
    bool hazOnceDeferred = false;   // a mode branch's hazard test found a hit and left it
    auto hazOnceSkip = [&]() -> bool {
        if (!g_hazPortalOnce || s.frame != 0) return false;
        if (hazOnceState >= 0) return hazOnceState != 0;
        hazOnceState = 0;
        const double pHp = (s.mode == 4 && c.mini) ? kWaveContactHalfMini : pHalf;
        for (const Obj* p : *K.ports) {
            const int m = modePortalTarget(p->type);
            if (m < 0 || m == (int)s.mode || playerHalf((uint8_t)m, c.mini != 0) == pHalf) continue;
            bool seen = false;
            for (int i = 0; i < 3; ++i) seen = seen || (p->uid >= 0 && s.portSeen[i] == p->uid);
            if (seen) continue;
            if (p->oriented && !orientedHit(*p, x, (double)c.y, pHp, (double)s.rot)) continue;
            if (std::fabs(x - p->cx) <= p->hw + pHp
                && std::fabs((double)yFree - p->cy) - (p->hh + pHp) <= 0.0) {
                hazOnceState = 1;
                if (g_slopeDbg)
                    std::printf("hazonce t=%lld deferred: mode portal uid=%d type=%d x=%.2f yFree=%.3f\n",
                                (long long)K.t, p->uid, (int)p->type, x, (double)yFree);
                break;
            }
        }
        return hazOnceState != 0;
    };
    const double pInner = (g_spiderInner && s.mode == 6 && !c.mini) ? kSpiderInner : kCubeInner;
    dead = false;
    bool cubeContact = false;
    bool rotWrite = false;
    bool rotPadSpin = false;
    bool rotBoostNow = false;
    bool rotReleaseNow = false;
    uint8_t rotPadFlip = 0;
    uint8_t rotWriteFlip = 0;
    bool flyEntryStake = false;
    uint8_t flyEntryFlip = 0, flyEntryMini = 0;
    bool rideOn = false;
    double rideFace = 0.0;
    double rideDy = 0.0;
    double rideGap = 0.0;   // how far the chosen face is from the foot
    bool landedThisTick = false;
    double landFaceBest = 0.0;
    float preLandY = 0.f;
    const Obj* offRampRide = nullptr;
    float offRampY = 0.f, offRampVy = 0.f;
    bool cubeLandedThisTick = false;
    bool stepLandedThisTick = false;
    // --rampslablift (constants.hpp): a climbing slab put the body on its top this tick
    bool rampSlabLiftNow = false;
    // --slopeunchanged (constants.hpp): the ramp a seat (the ride's or --slopelaw's) used this tick
    int suSeatUid = -1;
    float suYPre = 0.f;    // --apexstick: y as the ramp pass found it (yBeforeSlopes)
    bool suYPreSet = false;
    bool pinnedOnBlock = false;
    float prePinY = 0.f;
    bool teleportedThisTick = false;
    int teleUid = -1;
    double teleCx = 0.0, teleCy = 0.0;   // --tporder (gdTeleAfter)
    float teleFromY = 0.f, teleFromYFree = 0.f;
    // ...and where it landed (the portal seat's previous edge under --tponce, see there).
    double teleLandY = 0.0;
    bool spiderWarpedThisTick = false;
    bool spiderFreshTp = false;
    bool spiderBandCeil = false;
    auto releasePin = [&](bool undoSnap) {
        if (pinnedOnBlock) {
            YSET(c.y) = prePinY;
            pinnedOnBlock = false;
        } else if (landedThisTick) {
            YSET(c.y) = preLandY;
            landedThisTick = false;
            if (undoSnap) cubeLandedThisTick = false;
        }
    };
    bool impulsedThisTick = false;
    bool cubeGroundJumpNow = false;
    float vyPreGroundJump = 0.0f;
    bool a1cNow = (s.a1cLatch & 1) != 0;
    double a1cVyUpd = 0.0;
    bool a1cUpUpd = (s.flip != 0);   // the polarity the fall test reads (a swing's flip lands first)
    bool a1cNoFall = false, a1cEarlySet = false, a1cJumpLate = false, a1cLate = false;
    auto a1cJump = [&]() {
        if (input && !s.action) {
            a1cJumpLate = true;
        } else {
            a1cNoFall = true;
            a1cEarlySet = true;
        }
    };
    int pressAction = 0;
    bool portalPressRedo = false;
    bool landTapRing = false;
    uint8_t landTapFlip0 = 0;
    // --padsection: the size portal this tick fired, for GD's collision order against the pads
    bool sizeFiredThisTick = false;
    double sizeFiredCx = 0.0, sizeFiredCy = 0.0;
    int sizeFiredUid = -1;
    uint8_t sizePreMini = s.mini;
    // --padafterportal: ...and the first mode portal that changed the mode this tick
    bool modeFiredThisTick = false;
    double modeFiredCx = 0.0, modeFiredCy = 0.0;
    int modeFiredUid = -1;
    bool lawSeatedNow = false;
    const bool lawWasOn = s.seatT > 0 && !s.onSlope && !s.flip
                          && s.frame == 0
                          && (s.mode == 0 || s.mode == 2 || s.mode == 5 || s.mode == 6
                              || (waveRampArm && s.mode == 4
                                  && !s.mini));
    bool lawRepeatDrop = false;
    double lawDropM = 0.0;
    double c6Now = 0.0;
    const bool wasGrounded = (s.grounded != 0);
    bool ballFlippedThisTick = false;
    bool thinSupHaz = false;
    if (s.mode == 4) {
        const double wHalf = (c.mini ? kWaveHalfMini : kWaveHalf);
        const double wHazHalf = (c.mini ? kWaveHazHalfMini : kWaveHalf);
        const double waveSlope = c.mini ? 2.0 : 1.0;
        const double dyDir = (s.held ? 1.0 : -1.0) * (s.flip ? -1.0 : 1.0);
        const double dy = dyDir * useDx * waveSlope;
        YSET(c.y) = (float)((double)s.y + dy);
        yFree = c.y;
        VYSET(c.vy) = (float)(dyDir * useDx * 4.0);
        a1cVyUpd = (double)c.vy;   // --a1clatch (the fall test, 0x38cb26)
        c.held = (uint8_t)input;
        c.grounded = 0;
        {
            const double target = kRadToDeg * std::atan(waveSlope)
                                * (dy < 0.0 ? 1.0 : -1.0);
            const double k = c.mini ? kWaveRotKMini : kWaveRotK;
            c.rot = (float)((double)s.rot + k * (target - (double)s.rot));
        }
        if (g_waveDash) {
            if (s.dashing && s.action
                && (g_dashStopBoxes.empty() || !dashStopAt(modX, modY, pHalf))) {
                c.dashing = 1;
                YSET(c.y) = (float)((double)s.y + (double)s.dashSlope * useDx);
                yFree = c.y;
                VYSET(c.vy) = 0.f;
                a1cVyUpd = 0.0;
                c.rot = (float)(-kRadToDeg * std::atan((double)s.dashSlope));
            } else {
                c.dashing = 0;
            }
        }
        const double wClamp = (c.mini ? kWaveClampMini : kWaveClamp);
        double btF = (double)s.bandFloor, btC = (double)s.bandCeil;
        if (s.frame == 0 && !(g_bandOwnTrack && (s.bandBranch & kBandOwn))) {
            BandKind bk = BandKind::Unknown;
            double f_ = btF, c_ = btC;
            long long bage = 0;
            if (bandTrackKindAt((long long)K.t + 1, bk, f_, c_, bage)
                && bk == BandKind::Interval
                && (double)s.y >= f_ - 1.0 && (double)s.y <= c_ + 1.0
                && !bandTrackOtherRoute(s, (long long)K.t + 1, f_, c_, bage)) {
                btF = f_; btC = c_;
            }
        }
        // --flyfree: the Free Mode byte clamps no mode to the band (bands.hpp, flyBandFree).
        if (s.frame == 0 && flyBandFree(s.bandBranch)) { btF = 0.0; btC = 1e9; }
        const double bFloor = std::max(btF, kGroundY);
        const double yMaxW = btC - wClamp;
        if (g_bandDbg && (double)c.y > yMaxW - 8.0)
            std::printf("banddbg t=%lld x=%.1f y=%.2f band=[%.0f,%.0f] yMaxW=%.1f\n",
                        (long long)K.t, x, (double)c.y, (double)s.bandFloor,
                        (double)s.bandCeil, yMaxW);
        if ((double)c.y > yMaxW) {
            YSET(c.y) = (float)yMaxW; VYSET(c.vy) = 0; c.grounded = 1; CLAMP0("wave/ceil");
        }
        if ((double)c.y < bFloor + wClamp) {
            YSET(c.y) = (float)(bFloor + wClamp); VYSET(c.vy) = 0; c.grounded = 1;
            CLAMP0("wave/floor");
        }
        if (c.y > g_yBound) DIE_AS(kDeathPrune, "wave/out-of-play", nullptr);
        double waveSeat = -1e18;
        double waveSeatDn = 1e18;   // --slidelive: an upside-down wave's seat under a face
        if (g_slideEnd && !g_slideBoxes.empty()) {
            double ex = x, ey = (double)c.y;
            if (s.frame & 3) fromFrame((int)s.frame, x, (double)c.y, ex, ey);
            if (slideBoxTouch(ex, ey, wHalf, (int)K.t + 1)) c.slideT = 0;
        }
        const bool slideArmed = c.slideT < kArmTicks;
        const bool waveRampOn = waveRampArm && !c.mini && !c.flip && s.frame == 0;
        const int waveCurSlope = (s.onSlope || lawWasOn) ? (int)c.slopeUidNow : -1;
        auto waveStage1 = [&](const Obj* o, double px, double py, double half) {
            if (g_waveObbAabb && o->obbOk && o->radius <= 0.0)
                return std::fabs(px - o->cx) <= o->hw + half + kHazMargin
                    && std::fabs(py - o->cy) <= o->hh + half + kHazMargin;
            return hazardHit(o, px, py, half, kHazMargin, kSawMargin, 4, c.mini);
        };
        for (const Obj* o : *K.near) {
            if (dead) break;
            if (g_hazDbgUid == o->uid) {
                const bool g1 = waveStage1(o, x, (double)c.y, wHazHalf);
                const bool g2 = (!o->obbOk || s.mode != 4
                                 || obbOverlap(*o, x, (double)c.y, wHazHalf,
                                               (double)s.rot));
                const double ddx = x - o->cx, ddy = (double)c.y - o->cy;
                const double hh_ = wHazHalf + kHazMargin;
                const double rr = hh_ * (std::fabs(o->bc) + std::fabs(o->bs));
                const double lx_ = ddx * o->bc + ddy * o->bs;
                const double ly_ = -ddx * o->bs + ddy * o->bc;
                const double ex_ = o->bhw * std::fabs(o->bc) + o->bhh * std::fabs(o->bs);
                const double ey_ = o->bhw * std::fabs(o->bs) + o->bhh * std::fabs(o->bc);
                std::printf("wavehazgate t=%lld uid=%d stage1=%d stage2=%d slope=%d "
                            "dx=%.3f dy=%.3f bc=%.4f bs=%.4f r=%.3f lx=%.3f/%.3f "
                            "ly=%.3f/%.3f ex=%.3f ey=%.3f\n",
                            (long long)K.t, o->uid, g1 ? 1 : 0, g2 ? 1 : 0,
                            (int)o->slope, ddx, ddy, o->bc, o->bs, rr,
                            lx_, o->bhw + rr, ly_, o->bhh + rr, ex_, ey_);
            }
            if (g_hazDbgUid == o->uid)
                std::printf("wavehaz t=%lld uid=%d type=%d cx=%.3f cy=%.3f hw=%.3f "
                            "hh=%.3f obbOk=%d bhw=%.3f bhh=%.3f r=%.3f "
                            "x=%.3f xPrev=%.3f y=%.4f yPrev=%.4f rot=%.4f "
                            "half=%.2f mini=%d mode=%d frame=%d\n",
                            (long long)K.t, o->uid, (int)o->type, o->cx, o->cy,
                            o->hw, o->hh, (int)o->obbOk, o->bhw, o->bhh,
                            o->radius, x, xPrev, (double)c.y, (double)s.y,
                            (double)s.rot, wHazHalf, (int)c.mini, (int)c.mode,
                            (int)s.frame);
            if (slideArmed && (!c.mini || g_slideMini) && o->type == 0 && !o->slope
                && !o->oriented) {
                // --slidemini (constants.hpp): the mini wave is pushed out with its 6x6 rect's half
                const double sHalf = c.mini ? kWaveHazHalfMini : wHalf;
                const double top = o->cy + o->hh;
                if (g_slideLive) {
                    const double bot = o->cy - o->hh;
                    const double reach = kShipLandTol + std::fabs((double)o->dcy) / 0.25;
                    if (std::fabs(x - o->cx) <= o->hw + sHalf) {
                        const bool topSide = (double)c.y >= o->cy && (double)c.y <= top + sHalf
                            && ((double)s.y - sHalf) - top >= -reach;
                        const bool rampVeto = topSide && waveRampOn
                            && slopeVetoesSolid(o, K.slopes, x, (double)c.y, sHalf, sHalf,
                                                true, x, (double)c.y, waveCurSlope,
                                                false, nullptr,
                                                s.onSlope != 0 || lawWasOn);
                        if (topSide && !rampVeto) {
                            waveSeat = std::max(waveSeat, top + sHalf);
                            if (waveRampOn) continue;
                        } else if ((double)c.y < o->cy && (double)c.y >= bot - sHalf
                                 && bot - ((double)s.y + sHalf) >= -reach)
                            waveSeatDn = std::min(waveSeatDn, bot - sHalf);
                        if (rampVeto) continue;
                    }
                } else if (std::fabs(x - o->cx) <= o->hw + sHalf
                    && (double)c.y >= o->cy && (double)c.y <= top + sHalf)
                    waveSeat = std::max(waveSeat, top + sHalf);
            }
            // The sweep below is the rest of this object's turn; see outOfSweepReach.
            if (outOfSweepReach(o, xPrev, (double)s.y, x, (double)c.y,
                                std::max(wHazHalf, kWaveKillHalf)))
                continue;
            for (int si = 0; si <= kSubSteps && !dead; ++si) {
                const double f = si / (double)kSubSteps;
                const double sx = xPrev + (x - xPrev) * f;
                const double sy = (double)s.y + ((double)c.y - (double)s.y) * f;
                if (o->type == 0) {
                    if (g_waveOneway && o->oneway) continue;
                    if (std::fabs(sy - o->cy) <= o->hh + kWaveKillHalf
                        && std::fabs(sx - o->cx) <= o->hw + kWaveKillHalf)
                        DIE("wave/solid", o);
                } else if (g_waveHazEndpoint && si < kSubSteps) {
                } else if (waveStage1(o, sx, sy, wHazHalf)
                           && (!o->obbOk || s.mode != 4
                               || obbOverlap(*o, sx, sy, wHazHalf, (double)s.rot))) {
                    if (hazOnceSkip()) hazOnceDeferred = true;   // --hazportalonce
                    else DIE("wave/hazard", o);
                }
            }
        }
        // ...and the push-out, once every object has had its say (see waveSeat).
        if (!dead && waveSeat > -1e17) {
            YSET(c.y) = (float)waveSeat;
            VYSET(c.vy) = 0;
            c.grounded = 1;
            CLAMP0("wave/slide");
        } else if (!dead && waveSeatDn < 1e17) {
            YSET(c.y) = (float)waveSeatDn;
            VYSET(c.vy) = 0;
            c.grounded = 1;
            CLAMP0("wave/slide-under");
        }
        if (!dead && g_glitchWave > 0.0) {
            const double gw = g_glitchWave;
            const double yEnd = (double)c.y;
            for (const Obj* o : *K.near) {
                if (dead) break;
                if (outOfSweepReach(o, x, yEnd, x, yEnd, std::max(wHazHalf, kWaveKillHalf) + gw))
                    continue;
                if (o->type == 0) {
                    if (o->slope || o->oriented) continue;
                    if (g_waveOneway && o->oneway) continue;
                    if (waveSeat > -1e17 && std::fabs(yEnd - waveSeat) < 1e-3
                        && yEnd >= o->cy + o->hh)
                        continue;
                    if (std::fabs(yEnd - o->cy) <= o->hh + kWaveKillHalf + gw
                        && std::fabs(x - o->cx) <= o->hw + kWaveKillHalf + gw)
                        DIE_AS(kDeathRule, "glitch/wave-hug", o);
                } else if ((g_waveObbAabb && o->obbOk && o->radius <= 0.0
                                ? waveStage1(o, x, yEnd, wHazHalf + gw)
                                : hazardHit(o, x, yEnd, wHazHalf + gw, kHazMargin,
                                            kSawMargin + gw, 4, c.mini))
                           && (!o->obbOk || s.mode != 4
                               || obbOverlap(*o, x, yEnd, wHazHalf + gw, (double)s.rot))) {
                    DIE_AS(kDeathRule, "glitch/wave-hug", o);
                }
            }
        }
        if (K.slopes) {
            const double wsR = (c.mini ? kWaveSlopeDxRMini : kWaveSlopeDxR);
            const double wsL = (c.mini ? kWaveSlopeDxLMini : kWaveSlopeDxL);
            const double wsY = (c.mini ? kWaveSlopeDyMini : kWaveSlopeDy);
            for (const Obj* sp : *K.slopes) {
                if (dead) break;
                if (waveRampOn && !sp->slopeHazard) continue;
                const double sx0 = sp->cx - sp->hw, sx1 = sp->cx + sp->hw;
                if (x + wHalf <= sx0 || x - wHalf >= sx1) continue;
                const double sm = (sp->sy1 - sp->sy0) / (sx1 - sx0);
                const bool ceilRamp = slopeIsCeiling(sp->slopeDir);
                const bool toRight = ceilRamp ? (sm < 0.0) : (sm > 0.0);
                const double sxRaw = x + (toRight ? wsR : -wsL);
                const double sxc = std::min(std::max(sxRaw, sx0), sx1);
                const double surf = sp->sy0 + sm * (sxc - sx0);
                if (sxRaw != sxc
                    && std::fabs(surf - (ceilRamp ? sp->cy + sp->hh : sp->cy - sp->hh)) < 1e-6)
                    continue;
                if (ceilRamp) {
                    if ((double)c.y > surf - wsY
                        && (double)c.y - wsY < sp->cy + sp->hh)
                        DIE("wave/slope", sp);
                } else {
                    if ((double)c.y < surf + wsY
                        && (double)c.y + wsY > sp->cy - sp->hh)
                        DIE("wave/slope", sp);
                }
            }
        }
    } else if (s.mode != 1 && s.mode != 3 && s.mode != 7) {
        const bool isBall = (s.mode == 2);
        const bool isRobot = (s.mode == 5);
        const bool isSpider = (s.mode == 6);
        double bandCT = (double)s.bandCeil, bandFT = (double)s.bandFloor;
        bool bandFromRec = false;   // --dualzoomband: the recorded band is GD's own value already
        const bool spiderBand = g_spiderBand && isSpider;
        bool spiderBandMoved = false;
        if (s.frame == 0 && !(g_bandOwnTrack && (s.bandBranch & kBandOwn))) {
            // Same reading as the wave side: interval or nothing.
            BandKind bk = BandKind::Unknown;
            double f_ = bandFT, c_ = bandCT;
            long long bage = 0;
            const long long bandRow = spiderBand ? (long long)K.t - 1 : (long long)K.t + 1;
            if (bandTrackKindAt(bandRow, bk, f_, c_, bage)
                && bk == BandKind::Interval
                && (double)s.y >= f_ - 1.0 && (double)s.y <= c_ + 1.0
                && !bandTrackOtherRoute(s, bandRow, f_, c_, bage)) {
                bandFT = f_; bandCT = c_;
                bandFromRec = true;
                double f2 = f_, c2 = c_;
                if (spiderBand && bandTrackKindAt(bandRow - 1, bk, f2, c2, bage)
                    && bk == BandKind::Interval)
                    spiderBandMoved = (f2 != f_ || c2 != c_);
            }
        }
        bool dzbMoved = false;
        if (g_dualZoomBand && !bandFromRec && s.dual && (s.mode == 0 || s.mode == 5)
            && bandCT < 1e8 && bandCT > bandFT) {
            const double f0 = bandFT, c0 = bandCT, mid = 0.5 * (f0 + c0);
            // the band `back` rows before this one, from the animation byte of that row
            auto dzbBand = [&](int back, uint8_t anim, double& f, double& cc) {
                const double cs = g_zoomTrigs.empty()
                    ? 1.0 : camScaleAt(x - back * (double)useDx, useDx, (long long)K.t - back);
                double h = c0 - f0;
                if (g_staticCamCol && (c.bandBranch & 3) == 1)
                    h = kBandRetracted - (kBandRetracted - h) * bandProgress(anim, kBandAnimInDual);
                f = f0; cc = c0;
                if (cs > 0.0 && (cs != 1.0 || h != c0 - f0)) {
                    f = mid - 0.5 * h / cs;
                    cc = mid + 0.5 * h / cs;
                }
            };
            auto animBack = [](uint8_t a) {
                const int e = a & 0x7f;
                return (uint8_t)((a & 0x80) | (e >= 127 ? 127 : std::max(0, e - 1)));
            };
            double f1, c1, f2, c2;
            dzbBand(1, s.bandAnim, f1, c1);
            dzbBand(2, animBack(s.bandAnim), f2, c2);
            bandFT = f1;
            bandCT = c1;
            dzbMoved = (f1 != f2 || c1 != c2);
            if (g_bandDbg)
                std::printf("dzbdbg t=%lld x=%.2f band(t-1)=[%.3f,%.3f] moved=%d anim=%d\n",
                            (long long)K.t, x, f1, c1, (int)dzbMoved, (int)s.bandAnim);
        }
        const double bandC = bandCT;
        const bool bandUsable =
            bandC < 1e8
            && bandIsWall(s.bandBranch,
                          std::fabs(bandC - std::round(bandC / 30.0) * 30.0) < 0.01
                          && !bandTrackIsCamera());
        const bool dualCubeBand = s.dual && (s.mode == 0 || s.mode == 5);
        const double ceilHere = std::min(
            playerCeilAt(x),
            ((isBall || isSpider || (dualCubeBand && s.flip)) && bandUsable) ? bandC : 1e9);
        const bool ceilIsBand = (isBall || isSpider || (dualCubeBand && s.flip)) && bandUsable
            && bandC <= playerCeilAt(x);
        spiderBandCeil = spiderBand && ceilIsBand;
        const bool tpSkipBand = s.tpSkip != 0;
        const CubePhys cph = cubePhysFor(useDx);
        const double gAcc =
            gMod != 1.0 ? rawGravStep(isBall || isSpider, isRobot, (double)useDx) * gMod
            : g_rawValues ? rawGravStep(isBall || isSpider, isRobot, (double)useDx)   // speed.hpp
            : isBall     ? kBallG
            : isRobot  ? ((useDx > 1.78) ? -0.195 : -0.194)
            : isSpider ? kBallG
                       : cph.g;
        const double gTerm = isBall ? kBallTerm : kCubeTerm;
        const double gsign = s.flip ? -1.0 : 1.0;
        const double vp = (double)s.vy * gsign;
        auto faceOf = [&](const Obj* o) {
            return s.flip ? (o->cy - o->hh) : (o->cy + o->hh);
        };
        bool groundedNow = s.grounded != 0;
        if (groundedNow
            && (s.flip || s.frame != 0 || s.y > (kGroundY + pHalf) + 0.5)) {
            bool sup = false;
            bool seamTied = false, seamHold = false;
            int32_t seamLow = -1;
            // --ridehold: the highest face (along gravity) among the supports that did not move
            bool haveStill = false;
            double stillFace = 0.0;
            if (g_supDbg)
                for (const Obj* o : *K.near)
                    if (o->type == 0 && std::fabs(x - o->cx) < 60.0)
                        std::printf("supdbg t=%lld uid=%d cx=%.2f cy=%.2f hw=%.2f hh=%.2f "
                                    "face=%.3f foot=%.3f gap=%.4f xok=%d dcy=%.4f\n",
                                    (long long)K.t, o->uid, o->cx, o->cy, o->hw, o->hh,
                                    faceOf(o), (double)s.y - gsign * pHalf,
                                    std::fabs((double)s.y - gsign * pHalf - faceOf(o)),
                                    std::fabs(x - o->cx) <= o->hw + pHalf + kContactEps ? 1 : 0,
                                    (double)o->dcy);
            for (const Obj* o : *K.near)
                if (o->type == 0
                    && std::fabs(x - o->cx) <= o->hw + pHalf + kContactEps
                    && std::fabs((double)s.y - gsign * pHalf - faceOf(o))
                           < kSupportTol + std::fabs(o->dcy)
                    && !(g_rideBound && gsign * o->dcy < 0.0
                         && ((double)s.y - gsign * pHalf - faceOf(o)) * gsign >= 5.0 * 0.25)
                    && !(lawWasOn
                         && vetoOf(K, o, x, (double)s.y, pHalf, pHalf, gsign > 0, xPrev,
                                   (double)s.y, s.slopeUidNow, true))) {
                    sup = true;
                    if (gsign * o->dcy >= 0.0) seamHold = true;
                    if (o->uid == s.groundUid
                        && std::fabs(x - o->cx) < o->hw + pHalf)
                        seamTied = true;
                    if (seamLow < 0 || o->uid < seamLow) seamLow = o->uid;
                    if (o->dcy != 0.0) {
                        const double foot = (double)s.y - gsign * pHalf;
                        const double gap = std::fabs(foot - faceOf(o));
                        if (!rideOn || gap < rideGap) {
                            rideOn = true;
                            rideGap = gap;
                            rideFace = faceOf(o);
                            rideDy = o->dcy;
                        }
                    } else if (!haveStill || (faceOf(o) - stillFace) * gsign > 0.0) {
                        haveStill = true;
                        stillFace = faceOf(o);
                    }
                }
            if (g_rideHold && rideOn && haveStill && (stillFace - rideFace) * gsign > 0.0)
                rideOn = false;
            if (sup && (s.frame & 1) && s.groundUid >= 0 && !seamTied
                && !seamHold) {
                sup = false;
                rideOn = false;
                c.groundUid = -1;
                if (g_slopeDbg)
                    std::printf("stickseam t=%lld x=%.2f y=%.2f tied=%d\n",
                                (long long)K.t, x, (double)s.y,
                                (int)s.groundUid);
            } else {
                c.groundUid = (seamTied && !seamHold) ? s.groundUid : seamLow;
            }
            if (g_slopeDbg) {
                std::printf("supp t=%lld x=%.2f y=%.2f flip=%d sup=%d uids=",
                            (long long)K.t, x, (double)s.y, (int)s.flip,
                            sup ? 1 : 0);
                for (const Obj* o : *K.near)
                    if (o->type == 0
                        && std::fabs(x - o->cx) <= o->hw + pHalf + kContactEps
                        && std::fabs((double)s.y - gsign * pHalf - faceOf(o))
                               < kSupportTol + std::fabs(o->dcy))
                        std::printf("%d(%.1f,%.1f) ", o->uid, o->cx, o->cy);
                std::printf("\n");
            }
            if (!sup && s.flip
                && std::fabs((double)s.y + pHalf - ceilHere) < 0.6
                && !(spiderBand && spiderBandMoved)
                && !(dzbMoved && ceilIsBand))   // --dualzoomband: as the spider's, a moving band
                sup = true;
            // --dualcubeband: ...and so does the band's floor under an upright dual cube.
            if (!sup && dualCubeBand && !s.flip && bandFT > kGroundY + 0.01 && bandFT < 1e8
                && bandIsWall(s.bandBranch,
                              std::fabs(bandFT - std::round(bandFT / 30.0) * 30.0) < 0.01)
                && std::fabs((double)s.y - pHalf - bandFT) < 0.6
                && !dzbMoved)
                sup = true;
            if (!sup && s.onSlope && K.slopes) {
                for (const Obj* sp : *K.slopes) {
                    if (s.flip && !slopeIsCeiling(sp->slopeDir)) continue;
                    const double sx0 = sp->cx - sp->hw, sx1 = sp->cx + sp->hw;
                    const double m = (sp->sy1 - sp->sy0) / (sx1 - sx0);
                    if (m == 0.0) continue;
                    const double off = (m > 0) ? slopeXOffset(m, pHalf)
                                               : -slopeXOffset(m, pHalf);
                    const double xr0 = x + (s.flip ? -off : off);
                    double loB = sx0, hiB = sx1 + 1.5;
                    double extUsed = 0.0;
                    int extSide = 0;          // 0 none, -1 widened loB, +1 widened hiB
                    if (!s.flip && s.mode == 0) {
                        const bool revS = ((double)K.dxF < 0.0 || s.rev != 0);
                        const double extC = pHalf * std::sqrt(1.0 + m * m)
                                            / std::fabs(m);
                        if (!revS && m < -0.01) { hiB = sx1 + extC; extUsed = extC; extSide = 1; }
                        else if (revS && m > 0.01) { loB = sx0 - extC; extUsed = extC; extSide = -1; }
                    }
                    if (xr0 < loB || xr0 > hiB) {
                        if (g_slopeDbg)
                            std::printf("slprej t=%lld x=%.2f uid=%d m=%.4f xr0=%.3f "
                                        "loB=%.3f hiB=%.3f side=%s off=%.3f ext=%.3f "
                                        "extside=%d onslope=%d ceil=%d flip=%d mode=%d\n",
                                        (long long)K.t, x, sp->uid, m, xr0, loB, hiB,
                                        (xr0 < loB) ? "lo" : "hi", off, extUsed, extSide,
                                        (int)s.onSlope, slopeIsCeiling(sp->slopeDir) ? 1 : 0,
                                        (int)s.flip, (int)s.mode);
                        continue;
                    }
                    sup = true;
                    break;
                }
            }
            if (!sup && s.frame == 0 && K.slopes) {
                const bool goingLeftT = (double)K.dxF < 0.0 || s.rev != 0;
                const bool fbT = s.mode == 1 || s.mode == 2 || s.mode == 3 || s.mode == 7;
                for (const Obj* sp : *K.slopes) {
                    if (sp->uid == s.slopeUidNow) continue;
                    if (sp->sy1 == sp->sy0) continue;
                    const double sx0 = sp->cx - sp->hw, sx1 = sp->cx + sp->hw;
                    const uint8_t dT = sp->slopeDir;
                    const bool wallLeftT = dT == 2 || dT == 3 || dT == 4 || dT == 6;
                    const bool topT = dT == 1 || dT == 3 || dT == 5 || dT == 6;
                    double insetT = 0.0;
                    bool wallT = false;
                    double wx0T = 0.0, wx1T = 0.0;
                    if (wallLeftT && sx0 > x) {
                        insetT = s.rev ? 1.0 : 0.0;
                        wallT = true; wx0T = sx0; wx1T = sx0 + 1.0;
                    } else if (!wallLeftT && goingLeftT && x > sx1) {
                        insetT = s.rev ? 0.0 : 1.0;
                        wallT = true; wx0T = sx1 - 1.0; wx1T = sx1;
                    }
                    const double sMinYT = sp->cy - sp->hh, sMaxYT = sp->cy + sp->hh;
                    if (wallT && !(wx0T > x + pHalf || x - pHalf > wx1T)) {
                        const bool onW = s.flip
                            ? std::fabs((double)s.y + pHalf - (sMinYT + insetT)) < kSupportTol
                            : std::fabs((double)s.y - pHalf - (sMaxYT - insetT)) < kSupportTol;
                        if (onW) { sup = true; break; }
                    }
                    if (sx0 + insetT > x + pHalf || x - pHalf > sx1 - insetT) continue;
                    const bool onT = topT
                        ? ((!s.flip || fbT)
                           && std::fabs((double)s.y - pHalf - sMaxYT) < kSupportTol)
                        : (s.flip
                           && std::fabs((double)s.y + pHalf - sMinYT) < kSupportTol);
                    if (onT) { sup = true; thinSupHaz = true; break; }
                }
            }
            if (!sup && isSpider && s.frame == 0
                && bandFT > kGroundY + 0.01 && bandCT < 1e8 && bandFT < bandCT
                && bandIsWall(s.bandBranch, false)
                && !(spiderBand && spiderBandMoved)) {   // --spiderband: as the ceiling above
                const double faceB = s.flip ? bandCT : bandFT;
                const double footB = s.flip ? (double)s.y + pHalf : (double)s.y - pHalf;
                if (std::fabs(footB - faceB) < kSupportTol) sup = true;
            }
            if (!sup && g_stickFallback && s.frame == 0 && K.near) {
                constexpr double kStickTol = 5.0 * 0.25;
                const double footNow = (double)s.y - gsign * pHalf;
                const double footAfter = footNow + kYScale * qVy(gAcc) * gsign;
                bool oldRide = false;
                for (const Obj* o : *K.near)
                    if (o->type == 0 && gsign * o->dcy < 0.0
                        && std::fabs(xPrev - o->cx) <= o->hw + pHalf + kContactEps
                        && std::fabs(footNow - faceOf(o)) < kSupportTol + std::fabs(o->dcy)
                        && (footAfter - faceOf(o)) * gsign < kStickTol) {
                        oldRide = true;
                        break;
                    }
                const Obj* fb = nullptr;
                if (oldRide)
                    for (const Obj* o : *K.near) {
                        if (o->type != 0 || gsign * o->dcy >= 0.0
                            || std::fabs(x - o->cx) > o->hw + pHalf)
                            continue;
                        const double gap = (footAfter - faceOf(o)) * gsign;
                        if (gap <= 0.0 || gap > kStickTol) continue;
                        if (!fb || (faceOf(o) - faceOf(fb)) * gsign > 0.0) fb = o;
                    }
                if (fb) {
                    sup = true;
                    rideOn = true;
                    rideFace = faceOf(fb);
                    rideDy = fb->dcy;
                    rideGap = std::fabs(footNow - rideFace);
                    if (g_slopeDbg)
                        std::printf("stickfb t=%lld x=%.2f y=%.3f uid=%d face=%.3f\n",
                                    (long long)K.t, x, (double)s.y, fb->uid, rideFace);
                }
            }
            if (!sup && g_slopeStick && s.stickSlope >= 0 && s.frame == 0 && !s.flip && s.mode == 0
                && !s.dual)
                sup = true;
            // --ridec6 (constants.hpp): the moving face the rider stands on writes c6 as the flyers' do
            if (g_rideC6 && sup && rideOn && s.frame == 0) c6Now = rideDy / 0.25;
            groundedNow = sup;
        }
        const double pHalfE = (c.mode == 4 && c.mini) ? kWaveContactHalfMini
                                                      : pHalf;
        double pRotE = (double)s.rot;
        if (g_noSatRotRaw && s.mode == 0)
            pRotE += (s.rotNeg ? 1.0 : -1.0) * (s.mini ? 2.25 : 1.7307692);
        bool gravPortalThisTick = false;
        const Obj* gravPortalP = nullptr;
        for (const Obj* p : *K.ports) {
            if (!isGravPortalType(p->type)) continue;
            const uint8_t wantFlip = (p->type == 42) ? (uint8_t)!gdUpOf(s) : (p->type == 3) ? 1 : 0;
            if (gdUpOf(s) == wantFlip) continue;
            if (portalOnFrameRotTrigger(*p, (int)c.frame)) continue;
            if ((s.frameChg || c.dual)
                && portalWasInsideAtPrev(*p, xPrev, (double)s.y, pHalfE, pRotE))
                continue;
            if (p->oriented
                && !orientedHit(*p, x, (double)s.y, pHalfE, pRotE)) continue;
            if (std::fabs(x - p->cx) <= p->hw + pHalfE
                && std::fabs((double)s.y - p->cy) < p->hh + pHalfE) {
                gravPortalThisTick = true;
                gravPortalP = p;
                break;
            }
        }
        if (g_slopeDbg && gravPortalThisTick)
            std::printf("gravpred t=%lld uid=%d type=%d mode=%d frame=%d "
                        "flip=%d dual=%d sup=%d ride=%d in=%d\n",
                        (long long)K.t, gravPortalP->uid,
                        (int)gravPortalP->type, (int)c.mode, (int)c.frame,
                        (int)s.flip, (int)c.dual, groundedNow ? 1 : 0,
                        rideOn ? 1 : 0, input ? 1 : 0);
        if (gravPortalThisTick && isBall) {
            if (g_slopeDbg)
                std::printf("gravflip t=%lld x=%.2f y=%.3f flip=%d sup=%d "
                            "ride=%d in=%d act=%d\n",
                            (long long)K.t, x, (double)s.y, (int)s.flip,
                            groundedNow ? 1 : 0, rideOn ? 1 : 0, input ? 1 : 0,
                            (int)s.action);
            groundedNow = false;
            rideOn = false;
        }
        const bool ogLingerGate =
            !g_fixGravityBug
            && ((g_upsideCoyote && (s.mode == 0 || s.mode == 2)) || (g_spiderCoyote && isSpider))
            && s.flip && s.frame == 0 && !s.dual
            && !groundedNow && !gravPortalThisTick
            && (s.grounded || s.ogLinger)
            && !((double)s.vy > gdapprox::accelSwitchVyForSpeed(speedMulForDx(useDx)));
        bool ogLingerJump = false;
        cubeContact = groundedNow;
        bool ringTookPress = false;
        if (input && !s.action && K.trigs && (s.mode == 0 || s.mode == 2)) {
            const double half = playerHalf(s.mode, s.mini != 0);
            for (const auto& tb : *K.trigs) {
                const TouchTrig* T = tb.first;
                if (!T->spawnRing || (s.trig & tb.second)) continue;
                // --spawnringon (speed.hpp): ...nor one the recording has off on the row before this tick's
                if (g_spawnRingOn && !pressRingOnAt(T->uid, (long long)K.t - 1)) continue;
                if (std::fabs(x - T->cx) < T->hw + half
                    && std::fabs((double)s.y - T->cy) < T->hh + half) {
                    ringTookPress = true;
                    c.trig |= tb.second;
                    c.trigT = (int32_t)K.t;
                    const int b = touchBitIndex(tb.second);
                    if (g_touchFireT[b] < 0) g_touchFireT[b] = (int)K.t;
                    c.fireB[b] = (uint16_t)K.t;
                    if (g_touchCensus)   // (the box fires markTouched prints are "tcensus: ... hit=")
                        std::printf("tcensus: t=%lld box=%d uid=%d press-took x=%.3f y=%.3f\n", (long long)K.t, b,
                                    T->uid, x, (double)s.y);
                    spawnRingUid = T->uid;
                    break;
                }
            }
            if (ringTookPress) {
                c.pressSpent = 1;
                c.ringHold = 1;
                c.jumpBuf = 0;
                c.holdDead = 1;
                spawnRingJumpNow = (s.mode == 0);   // --spawnringjump (the end of this function)
            }
        }
        if (g_ringPressEat && !ringTookPress && input && !s.action && (s.mode == 0 || s.mode == 2)) {
            // (--spawnringon: "on" from the row before this tick's, where the press's rings were)
            const int pr = pressRingHolding(x, (double)s.y, playerHalf(s.mode, s.mini != 0), (long long)K.t + 1,
                                            g_spawnRingOn ? (long long)K.t - 1 : (long long)K.t + 1,
                                            s.usedOrbOld, s.usedOrbHist);
            if (pr >= 0) {
                ringTookPress = true;
                c.pressSpent = 1;
                c.ringHold = 1;
                c.jumpBuf = 0;
                c.holdDead = 1;
                const PressRing& r = g_pressRings[(size_t)pr];
                if (!(g_multiOrb && r.multiAct)) notePressRingFired(c, r.uid);
            }
        }
        if (earlyRingTook) ringTookPress = true;
        // the ceiling pin is re-earned every tick (see "cube/ceilstop")
        c.ceilPin = 0;
        pinnedOnBlock =
            groundedNow && (s.flip || (double)s.y > (kGroundY + pHalf) + 0.5);
        prePinY = (float)((double)s.y + kYScale * qVy(gAcc) * gsign);
        double vpNew = vp;
        bool ballFlipped = false;
        bool spiderSeatPress = false;
        if (spiderBand && !groundedNow && !ogLingerGate && input && !s.action
            && !gravPortalThisTick && s.frame == 0) {
            const double yN = (double)s.y
                + kYScale * qVy(std::max(vp + gAcc * tScale, gTerm)) * gsign * tScale;
            if (s.flip)
                spiderSeatPress = ceilIsBand && yN + pHalf >= ceilHere;
            else
                spiderSeatPress = bandFT > kGroundY + 0.01 && bandFT < 1e8
                    && bandIsWall(s.bandBranch,
                                  std::fabs(bandFT - std::round(bandFT / 30.0) * 30.0) < 0.01)
                    && yN - pHalf <= bandFT;
            if (spiderSeatPress && K.orbs) {
                const double seatY = s.flip ? ceilHere - pHalf : bandFT + pHalf;
                for (const Obj* ob : *K.orbs)
                    if ((std::fabs(xPrev - ob->cx) < ob->hw + pHalf
                         && std::fabs((double)s.y - ob->cy) < ob->hh + pHalf)
                        || (std::fabs(x - ob->cx) < ob->hw + pHalf
                            && std::fabs(seatY - ob->cy) < ob->hh + pHalf)) {
                        spiderSeatPress = false;
                        break;
                    }
            }
        }
        bool spiderTpOrbFirst = false;
        if (g_teleportOrb && isSpider && input && !s.action && K.orbs)
            for (const Obj* ob : *K.orbs)
                if (ob->type == 46 && ob != s.usedOrb && !ringFiredBefore(s, ob->uid)
                    && std::fabs((double)s.y - ob->cy) < ob->hh + pHalf
                    && (std::fabs(xPrev - ob->cx) < ob->hw + pHalf
                        || std::fabs(x - ob->cx) < ob->hw + pHalf)) {
                    spiderTpOrbFirst = true;
                    break;
                }
        const bool spiderOg = groundedNow || (isSpider && ogLingerGate) || spiderSeatPress
                              || (isSpider && s.tpArrive && s.grounded);
        bool spiderRingFirst = false;
        if (g_spiderRingFirst && spiderOg && !spiderTpOrbFirst && isSpider && input && !s.action
            && K.orbs)
            for (const Obj* ob : *K.orbs)
                if (ob->type != 46 && ob != s.usedOrb && !ringFiredBefore(s, ob->uid)
                    && std::fabs((double)s.y - ob->cy) < ob->hh + pHalf
                    && (std::fabs(xPrev - ob->cx) < ob->hw + pHalf
                        || std::fabs(x - ob->cx) < ob->hw + pHalf)) {
                    spiderRingFirst = true;
                    break;
                }
        if (earlyRingTook && isSpider && spiderOg) spiderRingFirst = true;
        if (isSpider && input && !s.action && !spiderOg
            && !(g_spiderSpend && ringTookPress))   // --spiderspend: a ring took this press
            c.pSpiderTap = 1;
        bool spiderPadFirst = false;
        if (!spiderTpOrbFirst && !spiderRingFirst && spiderOg && input && !s.action
            && !gravPortalThisTick && isSpider
            && (g_spiderPressLate || g_spiderPadFirst) && !s.dual && !g_inPressProbe) {
            bool probeDead = false;
            g_inPressProbe = true;
            g_probePadFired = false;
            const State pc = stepOne(s, 0, K, probeDead, nullptr);
            g_inPressProbe = false;
            if (g_spiderPressLate && probeDead && g_deadKind == kDeathPhysics) {
                dead = true;   // g_deadWhy / g_deadObj are the probe's
                return pc;
            }
            spiderPadFirst = g_spiderPadFirst && !probeDead && g_probePadFired;
        }
        if (spiderTpOrbFirst || spiderRingFirst) {
            // the orb takes this press: no tap, and none of the jumps below
        } else if (spiderPadFirst) {
            // the pad pass below launches it; the press is an air press, kept while held
            c.pSpiderTap = 1;
        } else if (spiderOg && input && (!s.action || s.pSpiderTap) && !gravPortalThisTick
            && isSpider) {
            c.pSpiderTap = 0;   // --portalspidertap: the handed-over press is spent here
            double bestY = 0.0;
            bool hazTgt = false;
            double tapFT = bandFT, tapCT = bandCT;
            if (spiderBand && !s.action && s.frame == 0) {
                BandKind bk = BandKind::Unknown;
                double f_ = tapFT, c_ = tapCT;
                long long bage = 0;
                if (bandTrackKindAt((long long)K.t, bk, f_, c_, bage) && bk == BandKind::Interval
                    && (double)s.y >= f_ - 1.0 && (double)s.y <= c_ + 1.0
                    && !bandTrackOtherRoute(s, (long long)K.t, f_, c_, bage)) {
                    tapFT = f_; tapCT = c_;
                }
            }
            const bool tapBandWall = (tapFT > kGroundY + 0.01
                                      || (g_spiderBandGround && tapFT > kGroundY - 0.01))
                                     && tapCT < 1e8
                                     && tapFT < tapCT
                                     && bandIsWall(s.bandBranch, false);
            const double tpFromY = !spiderSeatPress ? (double)s.y
                                   : s.flip ? ceilHere - pHalf : bandFT + pHalf;
            const bool found = spiderTargetY(K, x, tpFromY, s.flip != 0,
                                             c.mini != 0, pHalf, bestY,
                                             s.frame == 0, &hazTgt,
                                             (int)s.frame, s.rev != 0,
                                             tapBandWall, tapFT, tapCT);
            if (!found) DIE_AS(kDeathPrune, "spider/no-target", nullptr);
            spiderFreshTp = spiderBand && !s.action && s.frame == 0 && !s.flip && tapBandWall
                            && std::fabs(bestY + pHalf - tapCT) < 0.01;
            if (hazTgt) DIE("spider/tp-hazard", nullptr);
            impulsedThisTick = IMPULSE();
            spiderWarpedThisTick = true;
            if (!(input && !s.action)) a1cNoFall = true;
            c.spiderJumpT = 0;   // GD's +0x820 (State::spiderJumpT)
            if (g_spiderSpend) c.pressSpent = 1;   // --spiderspend (constants.hpp)
            if (g_touchCensus) g_tcBranch |= 1;
            pinnedOnBlock = false;
            g_preBtnSet = true; g_preBtnY = (double)c.y;
            YSET(c.y) = (float)bestY;
            yFree = c.y;
            c.flip = s.flip ? 0 : 1;
            c.grounded = 0;
            c.snapObj = nullptr;
            VPSET(vpNew) = 1.0;
            if (s.onSlope && (double)s.slopeM != 0.0) {
                const double mTravW =
                    (double)s.slopeM
                    * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                const bool upW = s.flip ? (mTravW < 0.0) : (mTravW > 0.0);
                if (upW)
                    c.pExitVy = (float)rawQ((s.flip ? -1.0 : 1.0)   // speed.hpp
                                        * slopeExitVy(
                                              std::fabs((double)s.slopeM),
                                              c.mode, useDx, c.mini != 0)
                                        * slopeRampFactor((int)s.slopeT + 1));
            }
        } else if ((groundedNow || (s.tpArrive && s.grounded))
                   && (input || ((s.mode == 0 || s.mode == 2) && s.jumpBuf))
                   && !gravPortalThisTick && !ringTookPress
                   && !(g_dashGround && s.dashing && s.action)   // --dashground: still dashing
                   && !(freshTapHeld && input && !s.action)   // --balltapexit's re-run
                   && (!s.action
                       || (s.mode == 0 && !s.holdDead)
                       || (s.mode == 5 && !s.ringHold)
                       || (s.mode == 2 && s.jumpBuf && !s.holdDead))) {
            impulsedThisTick = IMPULSE();
            if (c.mode == 0 && !isBall && !isSpider) {   // --jumpafterland
                cubeGroundJumpNow = true;
                vyPreGroundJump = c.vy;
            }
            if (!isSpider) a1cJump();   // --a1clatch: updateJump's jump block (0x38bbda)
            if (input && !s.action && (s.mode == 0 || s.mode == 2) && !isSpider)
                pressAction = isBall ? 2 : 1;
            c.pressSpent = 1;
            if (isBall) {
                c.flip = c.flip ? 0 : 1;
                c.rotStep = (float)ballRotRate(c.mini != 0, true, useDx);
                c.rotNeg = (uint8_t)!((c.flip != 0) ^ (c.rev != 0));
                if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 1u);
                const double mTravB =
                    (double)s.slopeM
                    * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                const bool bUphill =
                    s.onSlope
                    && (s.flip ? (mTravB < 0.0) : (mTravB > 0.0));
                const double bBonus =
                    bUphill ? kBallSlopeExitBonus
                                  * slopeExitVy(std::fabs((double)s.slopeM),
                                                2, useDx, c.mini != 0)
                            : 0.0;
                const double bTap =
                    ballFlipFor(useDx) * (c.mini ? kMiniImpulse : 1.0)
                    + bBonus;
                VYSET(c.vy) = (float)(-bTap * (c.flip ? -1.0 : 1.0));
                if (!(input && !s.action))
                    YSET(c.y) = (float)((double)s.y + (double)c.vy * kYScale);
                if (g_slopeDbg)
                    std::printf("balltap t=%lld frame=%d flip=%d->%d mini=%d "
                                "dx=%.4f base=%.4f bonus=%.4f bTap=%.4f "
                                "vy=%.4f uphill=%d slopeM=%.3f\n",
                                (long long)K.t, (int)c.frame, (int)s.flip,
                                (int)c.flip, (int)c.mini, (double)useDx,
                                ballFlipFor(useDx), bBonus, bTap,
                                (double)c.vy, bUphill ? 1 : 0,
                                (double)s.slopeM);
                ballFlipped = true;
                ballFlippedThisTick = true;
            } else {
                VPSET(vpNew) = c.mini ? (cph.jump * (kCubeJumpMini / kCubeJump))
                               : cph.jump;
                if (isRobot) {
                    VPSET(vpNew) *= kRobotJumpScale;
                    c.rHover = rePushNoHoverAt(K.t)
                                   ? (uint8_t)0
                                   : (uint8_t)kRobotHoverTicks;
                }
                const double mTravJ =
                    (double)s.slopeM
                    * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                if (s.onSlope
                    && (s.flip ? (mTravJ < 0.0) : (mTravJ > 0.0))) {
                    const double bonus =
                        kSlopeJumpBonus
                        * slopeExitVy(std::fabs((double)s.slopeM), 0,
                                      useDx, c.mini != 0)
                        * slopeRampFactor((int)s.slopeT + 2);
                    VPSET(vpNew) += std::min(bonus,
                                      kSlopeJumpBonusCap * std::fabs(vpNew));
                }
                else if (g_slopeStick && !s.onSlope && s.stickSlope >= 0 && c.mode == 0 && !s.flip
                         && K.slopes) {
                    const Obj* st = nullptr;
                    for (const Obj* q : *K.slopes)
                        if (q->uid == s.stickSlope) { st = q; break; }
                    if (st && st->hw > 0.0) {
                        const double mSt = (st->sy1 - st->sy0) / (2.0 * st->hw);
                        const double mTravSt = mSt * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                        if (mTravSt > 0.0) {
                            const double bonus = kSlopeJumpBonus
                                * slopeExitVy(std::fabs(mSt), 0, useDx, c.mini != 0)
                                * slopeRampFactor(kSlopeStickRideTicks);
                            VPSET(vpNew) += std::min(bonus, kSlopeJumpBonusCap * std::fabs(vpNew));
                        }
                    }
                }
                else if (s.onSlope && c.mode == 0) {
                    YSET(c.y) = (float)((double)c.y
                                  + (double)s.slopeM
                                        * std::fabs((double)useDx)
                                        * (((double)K.dxF < 0.0
                                            || s.rev != 0) ? -1.0 : 1.0));
                    yFree = c.y;
                }
            }
            c.grounded = 0;
            if (s.action && s.mode == 0) {
                YSET(c.y) = (float)((double)s.y + kYScale * vpNew * gsign * tScale);
                yFree = c.y;
            }
        } else if (groundedNow) {
            VPSET(vpNew) = 0;
            c.grounded = 1;
            c.rHover = 0;
            // --dashground (constants.hpp): a dash outlives the landing while the button is held
            if (g_dashGround) c.dashing = (uint8_t)((s.dashing && s.action) ? 1 : 0);
            yFree = prePinY;
        } else {
            c.grounded = 0;
            if (s.dashing && s.action
                && (g_dashStopBoxes.empty()
                    || !dashStopAt(modX, modY, pHalf))) {
                c.dashing = 1;
                c.rHover = 0;
                VPSET(vpNew) = 0.0;
                YSET(c.y) = (float)((double)s.y + (double)s.dashSlope * useDx);
                yFree = c.y;
            } else if (isRobot && s.rHover && s.action) {
                VPSET(vpNew) = vp;
                a1cVyUpd = vp * gsign;   // --a1clatch: before the force, as updateJump leaves it
                if (forcePushOn())
                    VPSET(vpNew) += forceBoxAcc(modX, modY, pHalf,
                                         forceUnitFor(s.mode, useDx), (long long)K.t) * gdSign;
                c.rHover = (uint8_t)(s.rHover - 1);
                YSET(c.y) = (float)((double)s.y + kYScale * vpNew * gsign * tScale);
                yFree = c.y;
            } else {
                c.dashing = 0;
                double acc = gAcc;
                double forceAcc = 0.0;
                if (!g_forceFields.empty())
                    forceAcc += forceFieldAcc(modX, modY, pHalf) * gdSign;
                if (forcePushOn())
                    forceAcc += forceBoxAcc(modX, modY, pHalf,
                                            forceUnitFor(s.mode, useDx), (long long)K.t) * gdSign;
                VPSET(vpNew) = s.pNoTerm ? qVy(vp + acc * tScale)
                                  : qVy(std::max(vp + acc * tScale, gTerm));
                a1cVyUpd = qVy(vp + acc * tScale) * gsign;
                VPSET(vpNew) += forceAcc * tScale;
                if (isRobot) c.rHover = 0;
                YSET(c.y) = (float)((double)s.y + kYScale * vpNew * gsign * tScale);
                yFree = c.y;
                if (ogLingerGate && !isSpider   // the spider's press is its teleport, above
                    && input && !s.action && !ringTookPress && !freshTapHeld) {
                    if (isBall) {
                        c.flip = c.flip ? 0 : 1;
                        c.rotStep = (float)ballRotRate(c.mini != 0, true, useDx);
                        c.rotNeg = (uint8_t)!((c.flip != 0) ^ (c.rev != 0));
                        if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 1u);   // flipGravity
                        const double bTap = ballFlipFor(useDx) * (c.mini ? kMiniImpulse : 1.0);
                        VYSET(c.vy) = (float)(-bTap * (c.flip ? -1.0 : 1.0));
                        ballFlipped = true;
                        ballFlippedThisTick = true;
                    } else {
                        VPSET(vpNew) = c.mini ? (cph.jump * (kCubeJumpMini / kCubeJump))
                                              : cph.jump;
                    }
                    impulsedThisTick = IMPULSE();
                    a1cJump();   // --a1clatch: the jump block, from pushButton
                    c.pressSpent = 1;
                    ogLingerJump = true;
                }
            }
        }
        if (!ballFlipped) VYSET(c.vy) = (float)(vpNew * gsign);
        c.ogLinger = (ogLingerGate && !ogLingerJump && !impulsedThisTick) ? 1 : 0;
        double fgX = modX, fgY = modY;
        int fgRow = (int)K.t;
        if (g_fgPost) {
            fgX = x;
            fgY = (double)c.y;
            if (s.frame & 3) fromFrame((int)s.frame, x, (double)c.y, fgX, fgY);
            fgRow = (int)K.t + 1;
        }
        c.fgArm = (!g_flipHeadBoxes.empty()
                   && flipHeadArmsRide(fgX, fgY, pHalf, fgRow,
                                       s.fgArm == (uint8_t)kArmTicks))
                      ? (uint8_t)kArmTicks
                      : (uint8_t)(s.fgArm ? s.fgArm - 1 : 0);
        double armX = modX, armY = modY;
        int armRow = (int)K.t;
        if (g_armPost) {
            armX = x;
            armY = (double)c.y;
            if (s.frame & 3) fromFrame((int)s.frame, x, (double)c.y, armX, armY);
            armRow = (int)K.t + 1;
        }
        c.armT = (!g_armBoxes.empty()
                  && armBoxTouchRide(armX, armY, pHalf, armRow, s.armT == 0))
                     ? 0
                     : (uint8_t)std::min<int>(kArmTicks, (int)s.armT + 1);
        const double pCeil = ceilHere;
        bool groundedInvCeil = false;
        if (g_ceilStrict && ceilIsBand && c.flip && (double)c.y + pHalf == pCeil && c.vy == 0.0f)
            VYSET(c.vy) = 0.0f;
        if (c.flip && ((g_ceilStrict && ceilIsBand) ? (double)c.y + pHalf > pCeil
                                                     : (double)c.y + pHalf >= pCeil)
            && !(tpSkipBand && ceilIsBand)
            && !(dualCubeBand && c.vy < 0.0f)
            && !(spiderFreshTp && ceilIsBand)) {
            YSET(c.y) = (float)(pCeil - pHalf);
            if (!(spiderBand && ceilIsBand && spiderBandMoved) && !(dzbMoved && ceilIsBand))
                VYSET(c.vy) = 0;
            CLAMP0("ground/flipceil");
            c.grounded = 1;
            groundedInvCeil = true;
            if (isBall && input && !s.action && !freshTapHeld
                && !gravPortalThisTick && !ringTookPress
                && !(impulsedThisTick)) {
                impulsedThisTick = IMPULSE();
                a1cJump();   // --a1clatch: the jump block, from pushButton
                ballFlippedThisTick = true;
                if (g_ringTapLink) { landTapRing = true; landTapFlip0 = c.flip; }
                c.flip = 0;
                VYSET(c.vy) = (float)(-ballFlipFor(useDx)
                               * (c.mini ? kMiniImpulse : 1.0));
                c.grounded = 0;
            }
            if (c.mode == 0 && input && !s.action
                && !impulsedThisTick && !ringTookPress && !gravPortalThisTick) {
                impulsedThisTick = IMPULSE();
                a1cJump();   // --a1clatch
                const CubePhys jph = cubePhysFor(useDx);
                VYSET(c.vy) = (float)(-jph.jump
                               * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0));
                c.grounded = 0;
            }
            if (c.mode == 5 && dzbMoved && ceilIsBand && input && (!s.action || !s.ringHold)
                && !impulsedThisTick && !ringTookPress && !gravPortalThisTick) {
                impulsedThisTick = IMPULSE();
                a1cJump();   // --a1clatch
                const CubePhys jph = cubePhysFor(useDx);
                VYSET(c.vy) = (float)(-jph.jump
                               * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0)
                               * kRobotJumpScale);
                c.rHover = (uint8_t)kRobotHoverTicks;
                c.grounded = 0;
            }
        }
        else if (!c.flip && (double)c.y + pHalf >= pCeil
                 && (c.vy > 0 || ceilIsBand)
                 && !(tpSkipBand && ceilIsBand)) {
            YSET(c.y) = (float)(pCeil - pHalf);
            if (c.vy > 0) VYSET(c.vy) = 0;
            CLAMP0("ground/ceilblock");
        }
        const double bandF = bandFT;   // r81: recorded band first (see the ceilHere note)
        const bool bandFUsable =
            bandF > kGroundY + 0.01 && bandF < 1e8
            && bandIsWall(s.bandBranch,
                          std::fabs(bandF - std::round(bandF / 30.0) * 30.0) < 0.01);
        const double floorHere =
            ((isBall || isSpider || (dualCubeBand && !c.flip)) && bandFUsable) ? bandF : kGroundY;
        const bool embeddedOnGround = c.grounded && !c.flip
                                      && (c.mode == 0 || c.mode == 2)
                                      && c.y < floorHere + pHalf - 0.01;
        if (c.frame == 0 && (!c.grounded || embeddedOnGround) && !spiderWarpedThisTick
            && c.y <= floorHere + pHalf
            && c.vy <= 0
            && !(tpSkipBand && (isBall || isSpider) && bandFUsable)) {
            if (c.mode == 0 && c.flip) {
                DIE("ground/floor-inverted-cube", nullptr);
                return c;
            }
            YSET(c.y) = (float)(floorHere + pHalf);
            if (!((spiderBand && spiderBandMoved) || dzbMoved) || !bandFUsable || floorHere != bandF)
                VYSET(c.vy) = 0;
            CLAMP0("ground/floor");
            if (!c.flip) c.grounded = 1;
            if (c.mode == 5 && !c.flip && input
                && (!s.action || (dzbMoved && bandFUsable && floorHere == bandF && !s.ringHold))
                && !impulsedThisTick && !ringTookPress) {
                impulsedThisTick = IMPULSE();
                a1cJump();   // --a1clatch
                const CubePhys jph = cubePhysFor(useDx);
                VYSET(c.vy) = (float)(jph.jump
                               * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0)
                               * kRobotJumpScale);
                c.rHover = (uint8_t)kRobotHoverTicks;
                c.grounded = 0;
            }
            // ...and the CUBE the same, now measured (speed.hpp, the ground cube jump)
            if (c.mode == 0 && !c.flip && input && !s.action
                && !impulsedThisTick && !ringTookPress) {
                impulsedThisTick = IMPULSE();
                a1cJump();   // --a1clatch
                const CubePhys jph = cubePhysFor(useDx);
                VYSET(c.vy) = (float)(jph.jump
                               * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0));
                c.grounded = 0;
            }
            if (isBall && !c.flip && input && !s.action && !impulsedThisTick
                && !gravPortalThisTick && !ringTookPress && !freshTapHeld) {
                impulsedThisTick = IMPULSE();
                a1cJump();   // --a1clatch
                ballFlippedThisTick = true;
                if (g_ringTapLink) { landTapRing = true; landTapFlip0 = c.flip; }
                c.flip = 1;
                VYSET(c.vy) = (float)(ballFlipFor(useDx)
                               * (c.mini ? kMiniImpulse : 1.0));
                c.grounded = 0;
            }
        }
        auto sweepCubeHazard = [&](const Obj* o, bool hazDbg) {
                    const bool warped = spiderWarpedThisTick;
                    const int subLo = warped ? kSubSteps : 0;
                    for (int si = subLo; si <= kSubSteps && !dead; ++si) {
                        const double f = si / (double)kSubSteps;
                        if (g_hazEndpoint && si < kSubSteps) continue;   // --hazendpoint: endpoint only
                        const double sx = xPrev + (x - xPrev) * f;
                        const double sy = warped
                            ? (double)s.y
                            : (double)s.y + ((double)c.y - (double)s.y) * f;
                        const bool hzHit = hazardStage1(o, sx, sy, pHalf, kHazMargin,
                                                        sawMarginFor(s.mode),
                                                        s.mode, c.mini, g_obbAll);
                        if (hazDbg)
                            std::printf("hazdbg t=%lld uid=%d si=%d sx=%.6f "
                                        "sy=%.6f pHalf=%.2f marg=%.3f saw=%.3f "
                                        "mode=%d mini=%d radius=%.3f obbOk=%d "
                                        "|dx|=%.6f |dy|=%.6f limx=%.3f limy=%.3f "
                                        "hit=%d at=%p cy=%.3f dyn=%d\n",
                                        (long long)K.t, o->uid, si, sx, sy,
                                        pHalf, kHazMargin,
                                        sawMarginFor(s.mode), (int)s.mode,
                                        (int)c.mini, o->radius,
                                        o->obbOk ? 1 : 0,
                                        std::fabs(sx - o->cx),
                                        std::fabs(sy - o->cy),
                                        o->hw + pHalf + kHazMargin,
                                        o->hh + pHalf + kHazMargin,
                                        hzHit ? 1 : 0, (const void*)o, o->cy,
                                        (int)o->dynObj);
                        if (hazDbg)
                            std::printf("hazdbg t=%lld uid=%d si=%d obbAll=%d "
                                        "obbOk=%d sat=%d bhw=%.3f bhh=%.3f "
                                        "bc=%.4f bs=%.4f rot=%.4f\n",
                                        (long long)K.t, o->uid, si,
                                        g_obbAll ? 1 : 0, o->obbOk ? 1 : 0,
                                        (o->obbOk && obbOverlap(*o, sx, sy, pHalf,
                                                                (double)s.rot)) ? 1 : 0,
                                        o->bhw, o->bhh, o->bc, o->bs,
                                        (double)s.rot);
                        if (hzHit
                            && (!g_obbAll || !o->obbOk
                                || obbOverlap(*o, sx, sy, pHalf, (double)s.rot))) {
                            if (hazOnceSkip()) hazOnceDeferred = true;   // --hazportalonce
                            else DIE("cube/hazard", o);
                        }
                    }
        };
        std::vector<const Obj*> solidOrd;   // --solidorder
        for (const Obj* const& o : solidWalk(K, solidOrd)) {
            noteSolidVisit(K, o, x, (double)c.y, pHalf);   // --solidorddbg
            const bool hazDbg = (g_hazDbgUid >= 0 && o->uid == g_hazDbgUid);
            if (hazDbg)
                std::printf("hazdbg t=%lld uid=%d type=%d reach=loop x=%.6f "
                            "cx=%.3f hw=%.3f hh=%.3f pHalf=%.2f eps=%.3f "
                            "dx=%.6f lim=%.3f\n",
                            (long long)K.t, o->uid, (int)o->type, x, o->cx,
                            o->hw, o->hh, pHalf, kContactEps,
                            std::fabs(x - o->cx), o->hw + pHalf + kContactEps);
            // inclusive: touching edges count as overlapping (see kSupportReach)
            if (std::fabs(x - o->cx) > o->hw + pHalf + kContactEps) {
                if (hazDbg) std::printf("hazdbg t=%lld uid=%d dropped=xwindow\n",
                                        (long long)K.t, o->uid);
                continue;
            }
            if (hazDbg)
                std::printf("hazdbg t=%lld uid=%d passed=xwindow type!=0 is %d, "
                            "K.t>14 is %d\n", (long long)K.t, o->uid,
                            (o->type != 0) ? 1 : 0, (K.t > 14) ? 1 : 0);
            if (o->type != 0) {  // hazard (sub-tick sampled, planning margin)
                continue;
            }
            const double face = s.flip ? (o->cy - o->hh) : (o->cy + o->hh);
            const double prevFoot = (double)s.y - gsign * pHalf;
            const double newFoot = (double)c.y - gsign * pHalf;
            const double vpNow = (double)c.vy * gsign;
            if (g_rampSlabLift && s.onSlope && o->dcy * gsign > 0.0 && vpNow <= 0.0
                && ((double)c.y - face) * gsign > 0.0 && (newFoot - face) * gsign < 0.0
                && (face - newFoot) * gsign <= kLandTol + std::fabs((double)o->dcy) / 0.25) {
                if (g_slopeDbg)
                    std::printf("rampslablift t=%lld uid=%d face=%.3f dcy=%.4f y %.3f->%.3f\n",
                                (long long)K.t, o->uid, face, o->dcy, (double)c.y,
                                face + gsign * pHalf);
                YSET(c.y) = (float)(face + gsign * pHalf);
                VYSET(c.vy) = (float)((double)o->dcy / 0.25);
                c.grounded = 0;
                rampSlabLiftNow = true;
                pinnedOnBlock = false;
                CLAMP0O("solid/rampslablift", o);
                continue;
            }
            if (vetoOf(K, o, x, (double)c.y, pHalf, pHalf,
                       gsign > 0, xPrev, (double)s.y, c.slopeUidNow, lawWasOn))
                continue;
            double landTol = c.onSlope ? 0.001 : kLandTol;
            if (g_slopeDbg && c.onSlope)
                std::printf("landtol0001 t=%lld uid=%d\n",
                            (long long)K.t, o->uid);
            bool stepCandidate = false;
            if (c.onSlope && s.onSlope && !c.flip && c.mode != 6
                && (double)s.slopeM != 0.0) {
                const double mR = (double)s.slopeM;
                const double xoffR = slopeXOffset(mR, pHalf);
                const double rR = xPrev + (mR > 0 ? xoffR : -xoffR);
                const double footR = (double)s.y - pHalf;
                const double nearX = std::min(std::max(rR, o->cx - o->hw),
                                              o->cx + o->hw);
                const double lineAt = footR + mR * (nearX - rR);
                if (lineAt - face >= kStepDepth) {
                    landTol = kLandTol;
                    stepCandidate = true;
                }
            }
            if (g_dynDbg >= 0 && o->uid == g_dynDbg)
                std::printf("land? t=%lld uid=%d frame=%d flip=%d gs=%.0f "
                            "cy=%.3f hh=%.3f face=%.3f prevFoot=%.3f "
                            "newFoot=%.3f vpNow=%.3f -> %s\n",
                            (long long)K.t, o->uid, (int)c.frame, (int)s.flip,
                            gsign, o->cy, o->hh, face, prevFoot, newFoot, vpNow,
                            (vpNow <= 0 && (prevFoot - face) * gsign >= -landTol
                             && (newFoot - face) * gsign <= 0) ? "LAND" : "no");
            double ballCornerM = 0.0;
            if (c.mode == 2 && !c.flip && vpNow <= 0 && K.slopes
                && (newFoot - face) * gsign <= 0
                && (prevFoot - face) * gsign >= -landTol) {
                for (const Obj* spc : *K.slopes) {
                    if (spc->slopeHazard) continue;
                    if (slopeIsCeiling(spc->slopeDir)) continue;
                    const double sx0 = spc->cx - spc->hw;
                    const double sx1 = spc->cx + spc->hw;
                    const double sm = (spc->sy1 - spc->sy0) / (sx1 - sx0);
                    if (sm == 0.0) continue;
                    const double cxE = (sm > 0) ? sx1 : sx0;
                    const double cyE = (sm > 0) ? spc->sy1 : spc->sy0;
                    if (std::fabs(cyE - face) > 0.01) continue;
                    const bool past = (sm > 0)
                        ? (x > cxE && x - pHalf <= cxE + kContactEps)
                        : (x < cxE && x + pHalf >= cxE - kContactEps);
                    if (!past) continue;
                    ballCornerM = std::fabs(sm);
                    break;
                }
            }
            if (ballCornerM != 0.0 && (face - newFoot) * gsign < 1.0)
                continue;
            const double dHangL = (prevFoot - face) * gsign;
            if (cubeGroundJumpNow && c.mode == 0 && !c.onSlope
                && (double)vyPreGroundJump * gsign <= 0.0
                && dHangL < 0.0 && dHangL >= -landTol && (newFoot - face) * gsign < 0.0) {
                if (!landedThisTick || (face - landFaceBest) * gsign > 0.0) {
                    landFaceBest = face;
                    YSET(c.y) = (float)(face + gsign * pHalf);
                    CLAMP0O("solid/jumpafterland", o);
                }
                continue;
            }
            if (g_cubeMpush && o->dcy * gsign > 0.0 && !c.grounded && !s.onSlope && vpNow > 0.0
                && dHangL < 0.0 && ((double)c.y - face) * gsign > 0.0
                && (newFoot - face) * gsign < 0.0) {
                const double reach = kLandTol + std::fabs((double)o->dcy) / 0.25;
                if ((newFoot - face) * gsign >= -reach || dHangL >= -reach) {
                    if (g_slopeDbg)
                        std::printf("cubempush t=%lld uid=%d face=%.3f dcy=%.4f reach=%.3f "
                                    "y %.3f->%.3f\n", (long long)K.t, o->uid, face, o->dcy, reach,
                                    (double)c.y, face + gsign * pHalf);
                    YSET(c.y) = (float)(face + gsign * pHalf);
                    const double surfVp = (double)o->dcy * gsign / 0.25;
                    if (surfVp > vpNow) VYSET(c.vy) = (float)(surfVp * gsign);
                    if (!g_cubeMpushNoGround && dHangL >= -kLandTol) c.grounded = 1;
                    continue;
                }
            }
            if (vpNow <= 0 && dHangL >= -landTol
                && (dHangL >= 0.0 || !groundedInvCeil)
                && (newFoot - face) * gsign <= 0) {
                if (!landedThisTick) preLandY = c.y;
                // r94: only the face highest along gravity writes y (measured, see decl.)
                const bool higherFace =
                    !landedThisTick || (face - landFaceBest) * gsign > 0.0;
                landedThisTick = true;
                cubeLandedThisTick = true;
                // r84: moved onto a step mid-ride (stops the slope loop below)
                if (stepCandidate && dHangL < -0.001)
                    stepLandedThisTick = true;
                if (higherFace) {
                    landFaceBest = face;
                    YSET(c.y) = (float)(face + gsign * pHalf);
                }
                VYSET(c.vy) = 0; CLAMP0O("solid/land", o);
                c.grounded = 1;
                if (isBall && input && !s.action && !ballFlippedThisTick
                    && !gravPortalThisTick && !ringTookPress && !freshTapHeld) {
                    impulsedThisTick = IMPULSE();
                    a1cJump();   // --a1clatch
                    ballFlippedThisTick = true;
                    if (g_ringTapLink) { landTapRing = true; landTapFlip0 = c.flip; }
                    c.flip = c.flip ? 0 : 1;
                    const double cornerBonus = (ballCornerM != 0.0)
                        ? kBallSlopeTapBonus
                              * (slopeExitVy(ballCornerM, 2, useDx,
                                             c.mini != 0)
                                 / slopeExitVy(0.5, 2, useDx, c.mini != 0))
                        : 0.0;
                    VYSET(c.vy) = (float)(-(ballFlipFor(useDx) + cornerBonus)
                                   * (c.mini ? kMiniImpulse : 1.0)
                                   * (c.flip ? -1.0 : 1.0));
                    c.grounded = 0;
                }
                else if ((c.mode == 0 || c.mode == 5) && input && !s.action
                         && !impulsedThisTick && !ringTookPress) {
                    impulsedThisTick = IMPULSE();
                    a1cJump();   // --a1clatch
                    const CubePhys jph = cubePhysFor(useDx);
                    double vj = jph.jump
                                * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0);
                    if (c.mode == 5) {
                        vj *= kRobotJumpScale;
                        c.rHover = (uint8_t)kRobotHoverTicks;
                    }
                    VYSET(c.vy) = (float)(vj * (c.flip ? -1.0 : 1.0));
                    c.grounded = 0;
                }
            } else if (c.mode == 2 && !s.flip && vpNow >= 0.0
                       && (o->cy - o->hh) - ((double)s.y + pHalf) >= -kLandTol
                       && ((double)c.y + pHalf) - (o->cy - o->hh) > 0.0) {
                if (vetoOf(K, o, x, (double)c.y, pHalf, pHalf, false,
                           xPrev, (double)s.y, c.slopeUidNow, lawWasOn))
                    continue;
                YSET(c.y) = (float)((o->cy - o->hh) - pHalf);
                VYSET(c.vy) = 0; CLAMP0O("ball/ceil", o);
                c.grounded = 1;
                continue;
            } else if (c.mode == 2 && s.flip && c.flip && vpNow >= 0.0
                       && ((double)s.y - pHalf) - (o->cy + o->hh) >= -kLandTol
                       && (o->cy + o->hh) - ((double)c.y - pHalf) > 0.0) {
                if (vetoOf(K, o, x, (double)c.y, pHalf, pHalf, true,
                           xPrev, (double)s.y, c.slopeUidNow, lawWasOn))
                    continue;
                YSET(c.y) = (float)((o->cy + o->hh) + pHalf);
                VYSET(c.vy) = 0; CLAMP0O("ball/ceilflip", o);
                continue;
            } else if (!o->oneway) {
                if (g_cubeMpush && (c.mode == 0 || c.mode == 5) && o->dcy * gsign < 0.0
                    && !(g_mPushUpright && s.flip)   // --mpushupright (constants.hpp)
                    && ((double)s.vy * gsign > 0.0
                        || (g_headPushHold && (double)s.vy * gsign >= 0.0))) {
                    const double under = s.flip ? (o->cy + o->hh) : (o->cy - o->hh);
                    const double headIn = ((double)c.y + gsign * pHalf - under) * gsign;
                    const double prevHeadIn = ((double)s.y + gsign * pHalf - under) * gsign;
                    const double reach = kLandTol + std::fabs((double)o->dcy) / 0.25;
                    if (headIn > 0.0 && ((double)c.y - under) * gsign < 0.0
                        && (headIn <= reach || prevHeadIn <= reach)) {
                        if (g_slopeDbg)
                            std::printf("cubempush-head t=%lld uid=%d under=%.3f dcy=%.4f y %.3f->%.3f\n",
                                        (long long)K.t, o->uid, under, o->dcy, (double)c.y,
                                        under - gsign * pHalf);
                        YSET(c.y) = (float)(under - gsign * pHalf);
                        if (g_headPushHold) {
                            const double vr = (double)c.vy * gsign;
                            VYSET(c.vy) = (float)((vr < 0.0 ? std::min(vr, -1.0) : 0.0) * gsign);
                            if (c.mode == 5) c.rHover = (uint8_t)std::min<int>(c.rHover, 1);
                        } else {
                            VYSET(c.vy) = 0;
                        }
                        continue;
                    }
                }
                if (c.mode == 2 && !s.flip && vpNow > 0.001) {
                    const double headB = o->cy - o->hh;
                    const double yPenB = ((double)c.y + pHalf) - headB;
                    const bool xEntered =
                        std::fabs(x - o->cx) <= o->hw + pHalf
                        && std::fabs(xPrev - o->cx) > o->hw + pHalf;
                    if (xEntered && yPenB > 0.0 && yPenB < pHalf * 0.5) {
                        YSET(c.y) = (float)(headB - pHalf);
                        VYSET(c.vy) = 0; CLAMP0O("ball/headbonk", o);
                        c.grounded = 0;
                        continue;
                    }
                }
                if (c.mode == 0 || c.mode == 5) {
                    const double head = s.flip ? (o->cy + o->hh)
                                               : (o->cy - o->hh);
                    const double prevHead = (double)s.y + gsign * pHalf;
                    const double newHead = (double)c.y + gsign * pHalf;
                    const double xPenC = (o->hw + pHalf) - std::fabs(x - o->cx);
                    const bool held = s.ceilPin != 0
                                      && std::fabs(prevHead - head) <= 1.0;
                    const bool dashUp = g_dashHead && c.dashing && s.dashing
                                        && (double)s.dashSlope * gsign > 0.0;
                    const bool acquireBase = (vpNow > 0.001 || dashUp)
                                             && (prevHead - head) * gsign <= 0.001
                                             && (newHead - head) * gsign >= 0;
                    const double yPenC = (newHead - head) * gsign;
                    const uint8_t fgArmNow = g_fgPost ? c.fgArm : s.fgArm;
                    const bool acquireFlip = acquireBase && fgArmNow
                                             && yPenC <= xPenC;
                    const bool bonkWho = c.armT < kArmTicks;
                    const bool bonk = bonkWho
                                      && acquireBase
                                      && (!s.flip || (g_flipBonk && (g_armPost || c.armT == 0)))
                                      && !fgArmNow && yPenC <= xPenC;
                    if (g_crushHead && c.mode == 0 && !s.flip && o->dynObj && o->dcy < 0.0
                        && bonkWho && !acquireBase && !held && !fgArmNow && xPenC > 0.0
                        && yPenC >= 0.0 && yPenC <= xPenC
                        && prevHead - (head - o->dcy) <= 0.001) {
                        YSET(c.y) = (float)(head - pHalf);
                        if (c.vy > -1.0f) VYSET(c.vy) = -1.0f;
                        c.grounded = 0;
                        c.ceilPin = 0;
                        CLAMP0O("cube/crushhead", o);
                        continue;
                    }
                    if ((std::fabs(x - o->cx) <= o->hw + pInner
                         && (held || acquireFlip || bonk))
                        || (g_bonkOuter && bonk && !held && xPenC > 0.0)) {
                        if (acquireFlip) {
                            c.flip = (uint8_t)!s.flip;
                            const double g2 = c.flip ? -1.0 : 1.0;
                            YSET(c.y) = (float)(head + g2 * pHalf);
                            VYSET(c.vy) = 0; CLAMP0O("cube/fliphead", o);
                            c.grounded = 1;
                            c.ceilPin = 0;
                            a1cEarlySet = true;   // --a1clatch: didHitHead (0x393c7f)
                            // every gravity flip negates the cube's spin
                            c.rotNeg = c.flip;
                            continue;
                        }
                        // plain bonk: stops but does not grab (note at the declaration above)
                        if (bonk && !held) {
                            YSET(c.y) = (float)(head - gsign * pHalf);
                            VYSET(c.vy) = 0; CLAMP0O("cube/headbonk", o);
                            c.grounded = 0;
                            c.ceilPin = 0;
                            if (g_bonkOuter) c.rHover = (uint8_t)std::min<int>(c.rHover, 1);
                            continue;
                        }
                        YSET(c.y) = (float)(head - gsign * pHalf);
                        VYSET(c.vy) = 0; CLAMP0O("cube/ceilstop", o);
                        c.grounded = 1;
                        c.ceilPin = 1;
                        continue;
                    }
                }
                const bool separating =
                    ((double)c.y - o->cy) * (double)c.vy > 0.0
                    && std::fabs(xPrev - o->cx) < o->hw + pInner;
                const bool faceC = footFaceContact(*o, c, pHalf);
                const bool graceC = flipGraceSeat(*o, c, s, ballFlippedThisTick && input && !s.action, pHalf);   // --flipgrace (speed.hpp)
                for (int si = 0; si <= kSubSteps && !dead && !separating && !faceC; ++si) {
                    const double f = si / (double)kSubSteps;
                    const double sx = xPrev + (x - xPrev) * f;
                    const double sy = (double)s.y + ((double)c.y - (double)s.y) * f;
                    if (std::fabs(sy - o->cy) < o->hh + pInner + kHazMargin
                        && std::fabs(sx - o->cx) < o->hw + pInner + kHazMargin) {
                        if (graceC) {
                            // --graceendonly (constants.hpp): only where the tick ends inside
                            if (g_graceEndOnly
                                && !(std::fabs((double)c.y - o->cy) < o->hh + pInner + kHazMargin
                                     && std::fabs(x - o->cx) < o->hw + pInner + kHazMargin))
                                break;
                            seatOnNearFace(*o, c, pHalf);
                            CLAMP0O("cube/flipgrace", o);
                            break;
                        }
                        if (c.mode == 6 && c.frame == 0 && c.spiderJumpT < spiderJumpGraceTicks()
                            && o->cx + o->hw < sx + pInner)
                            break;
                        DIE("cube/solid-side", o);
                    }
                }
            }
        }
        if (K.t > 14 && !thinSupHaz) {
            for (const Obj* o : *K.near) {
                if (dead) break;
                if (o->type == 0) continue;
                const bool hazDbg = (g_hazDbgUid >= 0 && o->uid == g_hazDbgUid);
                const double reachX = (o->radius > o->hw) ? o->radius : o->hw;
                if (std::fabs(x - o->cx) > reachX + pHalf + kContactEps) {
                    if (hazDbg) std::printf("hazdbg t=%lld uid=%d dropped=xwindow\n",
                                            (long long)K.t, o->uid);
                    continue;
                }
                sweepCubeHazard(o, hazDbg);
            }
        }
    } else {
        double gsign = s.flip ? -1.0 : 1.0;
        double btFlyF = (double)s.bandFloor, btFlyC = (double)s.bandCeil;
        bool btFlyOn = false;
        if (s.frame == 0 && !(g_bandOwnTrack && (s.bandBranch & kBandOwn))) {
            BandKind bk = BandKind::Unknown, bkF = BandKind::Unknown;
            double f_ = btFlyF, c_ = btFlyC;
            long long bage = 0;
            int bsrc = -1;   // --bandsrc: the row's source (bands.hpp BandTrackRow::src)
            if (bandTrackKindAt((long long)K.t + 1, bk, f_, c_, bage, &bsrc)
                && bk == BandKind::Interval
                && ((g_bandSrc && bsrc == 0)
                    || ((double)s.y >= f_ - 1.0 && (double)s.y <= c_ + 1.0))
                && !bandTrackOtherRoute(s, (long long)K.t + 1, f_, c_, bage)) {
                btFlyF = f_; btFlyC = c_; btFlyOn = true;
                double ff = f_, fc = c_;
                long long fage = 0;
                if (bandTrackKindAt((long long)K.t - 1, bkF, ff, fc, fage)
                    && bkF == BandKind::Interval)
                    btFlyF = ff;
            }
        }
        const bool flyFreeNow = s.frame == 0 && flyBandFree(s.bandBranch);
        if (flyFreeNow) { btFlyF = 0.0; btFlyC = 1e9; btFlyOn = false; }
        const double floorY = std::max(btFlyF, kGroundY);
        bool groundedNow = s.grounded != 0;
        bool flyRide = false;
        double flyRideFace = 0.0, flyRideGap = 0.0, flyRideDcy = 0.0;
        int flyRideUid = -1;              // --dcydbg only: names the surface
        if (g_supDbg)
            std::printf("supdbg fly t=%lld grounded=%d y=%.3f floorY=%.3f pHalf=%.2f scan=%d\n",
                        (long long)K.t, (int)groundedNow, (double)s.y, floorY, pHalf,
                        (groundedNow && (s.flip || s.y > (floorY + pHalf) + 0.5)) ? 1 : 0);
        if (g_supDbg)
            for (const Obj* o : *K.near)
                if (o->type == 0 && std::fabs(x - o->cx) < 60.0)
                    std::printf("supdbg fly t=%lld uid=%d cx=%.2f cy=%.2f hw=%.2f hh=%.2f face=%.3f "
                                "foot=%.3f xok=%d dcy=%.4f\n",
                                (long long)K.t, o->uid, o->cx, o->cy, o->hw, o->hh,
                                s.flip ? (o->cy - o->hh) : (o->cy + o->hh),
                                (double)s.y - gsign * pHalf,
                                std::fabs(x - o->cx) <= o->hw + pHalf + kContactEps ? 1 : 0,
                                (double)o->dcy);
        if (groundedNow && (s.flip || s.y > (floorY + pHalf) + 0.5)) {
            bool sup = false;
            for (const Obj* o : *K.near) {
                if (o->type != 0) continue;
                const double face = s.flip ? (o->cy - o->hh) : (o->cy + o->hh);
                const double gap =
                    std::fabs((double)s.y - gsign * pHalf - face);
                if (std::fabs(x - o->cx) <= o->hw + pHalf + kContactEps
                    && gap < 0.6 + std::fabs(o->dcy)) {
                    if (g_stickFallback && gsign * o->dcy < 0.0
                        && ((double)s.y - gsign * pHalf - face) * gsign >= 5.0 * 0.25)
                        continue;
                    sup = true;
                    if (o->dcy != 0.0
                        && (!flyRide
                            || face * gsign > flyRideFace * gsign)) {
                        flyRide = true;
                        flyRideGap = gap;
                        flyRideFace = face;
                        flyRideDcy = (double)o->dcy;
                        flyRideUid = o->uid;
                    }
                }
            }
            groundedNow = sup;
        }
        if (groundedNow && s.onSlope && !s.flip
            && s.mode == 1 && c.mode == 1 && s.vy != 0.0f && !flyRide)
            groundedNow = false;
        const bool ridingCarry =
            (flyRide || (s.onSlope && s.grounded && s.flip)) && s.vy != 0.0f;
        const bool bandCarry = c.mode == 1 && btFlyOn && s.grounded
                               && !s.flip && s.vy != 0.0f
                               && (double)s.y <= floorY + pHalf + 1e-3;
        c.held = (uint8_t)input;
        const bool isUfo = (c.mode == 3);
        const bool isSwing = (c.mode == 7);
        const bool act = (isUfo || isSwing)
            ? ((input && !s.action) || (isUfo && s.pFlap != 0 && input))
            : (input != 0 && !c.holdDead);   // the ship reads +0x985 (speed.hpp, the hold latch)
        if (isUfo && act) impulsedThisTick = IMPULSE();

        const bool swingLift =
            isSwing && forcePushOn()
            && forceBoxAcc(modX, modY, pHalf,
                              forceUnitFor(s.mode, useDx), (long long)K.t) * gdSign > kSwingG;
        const bool headBandFresh = g_headBandFresh && K.t == 2 && K.xPrev <= 0.0;
        const bool swingSeatEdge = g_swingSeatPress && isSwing && act && flyRide && !s.rHover;
        if (groundedNow && (!act || swingSeatEdge) && !(isSwing && s.rHover) && !swingLift
            && !ridingCarry && !bandCarry && !headBandFresh) {
            VYSET(c.vy) = 0;
            c.grounded = 1;
            if (swingSeatEdge) c.rHover = 1;   // the flip lands on the next tick, from rest
            if (flyRide) {
                YSET(c.y) = (float)(flyRideFace + gsign * pHalf);
                if (g_seatC6) c6Now = flyRideDcy / 0.25;
            }
            if (!pinnedOnBlock
                && (s.flip || (double)s.y > (floorY + pHalf) + 0.5)) {
                const bool thrFlipRest = s.dual ? true
                                       : (g_fixGravThr && g_fixGravityBug) ? false   // --fixgravthr
                                                                           : (s.flip != 0);
                const double vpRest = qVy(
                    isSwing ? -(gMod != 1.0 ? swingGm(false, gMod) : kSwingG)
                    : isUfo ? gdapprox::UfoModel::stepVy(
                                0.0, false, ufoParamsG(useDx, c.mini, K, gMod),
                                thrFlipRest)
                            : gdapprox::ShipModel::stepVy(
                                0.0, false, shipParamsG(useDx, c.mini, K, gMod),
                                thrFlipRest));
                pinnedOnBlock = true;
                prePinY = (float)((double)c.y + kYScale * vpRest * gsign);
                yFree = prePinY;
            }
        } else if (isSwing) {
            c.grounded = 0;
            double vpS = groundedNow ? 0.0 : (double)s.vy * gsign;
            double gsS = gsign;
            {
                const double bandChi = (c.mini != 0) ? 0.85 : 1.0;
                if (vpS > -6.4 / bandChi && vpS < 8.0 / bandChi) c.boost = 0;
            }
            if (s.rHover) {                    // apply the previous tick's edge now
                c.rHover = 0;
                c.flip = s.flip ? 0 : 1;
                gsS = c.flip ? -1.0 : 1.0;
                vpS = -kSwingFlipDamp * vpS;   // vp in the new frame (sign mapped too)
            }
            if (act) c.rHover = 1;             // takes effect on the next tick (measured)
            const bool ceilTakeoff = s.rHover && groundedNow && s.flip;
            if (ceilTakeoff) {
                vpS = -1.0;                    // no gravity step on this tick
                VYSET(c.vy) = (float)(qVy(vpS) * gsS);
                a1cVyUpd = (double)c.vy;       // --a1clatch (the fall test, 0x38cb26)
                a1cUpUpd = (c.flip != 0);
                YSET(c.y) = s.y;
                yFree = c.y;
            } else {
                vpS -= swingGm(c.mini != 0, gMod) * tScale;
            }
            if (!ceilTakeoff) {
                double vyW = qVy(vpS) * gsS;
                if (!c.boost) {
                    if (vyW < -kSwingTerm)      { vyW = -kSwingTerm; vpS = vyW * gsS; }
                    else if (vyW > kSwingTerm)  { vyW = kSwingTerm;  vpS = vyW * gsS; }
                }
                if (forcePushOn()) {
                    const double fb =
                        forceBoxAcc(modX, modY, pHalf,
                                    forceUnitFor(s.mode, useDx), (long long)K.t);
                    if (fb != 0.0) { vyW = qVy(vyW + fb); vpS = vyW * gsS; }
                }
                VYSET(c.vy) = (float)vyW;
                a1cVyUpd = vyW;   // --a1clatch (the fall test, 0x38cb26)
                a1cUpUpd = (c.flip != 0);
                YSET(c.y) = (float)((double)s.y + kYScale * (double)c.vy * tScale);
                yFree = c.y;
            }
        } else {
            c.grounded = 0;
            const double vp = (groundedNow && !ridingCarry && !bandCarry)
                                  ? 0.0 : (double)s.vy * gsign;
            if (boostLatchMode(c.mode)) {
                const double bandChi = (c.mini != 0) ? 0.85 : 1.0;
                const double vpIn = (double)s.vy * gsign;
                if (vpIn > -6.4 / bandChi && vpIn < 8.0 / bandChi) c.boost = 0;
            }
            const bool boostNow = (c.boost != 0);
            const bool thrFlip = s.dual ? true
                               : (g_fixGravThr && g_fixGravityBug) ? false
                                                                   : (s.flip != 0);
            const double fbAcc = !forcePushOn()
                ? 0.0 : forceBoxAcc(modX, modY, pHalf,
                                     forceUnitFor(s.mode, useDx), (long long)K.t) * gdSign;
            double lawSvIn = (isUfo && !s.onSlope && fbAcc == 0.0)
                                 ? (double)s.lawSv : 0.0;
            const bool rideFlap = isUfo && act && s.onSlope && fbAcc == 0.0
                                  && !c.flip
                                  && (double)s.slopeM
                                         * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0)
                                     > 0.0;
            if (rideFlap)
                lawSvIn = slopeExitVy(std::fabs((double)s.slopeM), 3, useDx, c.mini != 0);
            const double flyT = (g_warpFly && (c.mode != 3 || g_warpFlyUfo))
                                    ? (g_warpSlowOnly ? std::min(tScale, 1.0) : tScale)   // --warpslowonly
                                    : 1.0;
            double vpNew = qVy(
                (isUfo ? gdapprox::UfoModel::stepVy(
                            vp, act, ufoParamsWarp(ufoParamsG(useDx, c.mini, K, gMod), flyT),
                            thrFlip, boostNow, lawSvIn)
                       : gdapprox::ShipModel::stepVy(
                            vp, act, shipParamsWarp(shipParamsG(useDx, c.mini, K, gMod), flyT),
                            thrFlip, boostNow))
                + fbAcc);
            if (!rideFlap && isUfo && act && s.onSlope && !c.mini
                && fbAcc == 0.0)
                vpNew = kUfoRampFlap;
            VYSET(c.vy) = (float)(vpNew * gsign);
            a1cVyUpd = (double)c.vy;   // --a1clatch (the fall test, 0x38cb26)
            if (flyT == 1.0)
                YSET(c.y) = (float)((double)s.y + kYScale * (double)c.vy);
            else
                YSET(c.y) = (float)((double)s.y + kYScale * (double)c.vy * flyT);
            yFree = c.y;
            if (g_flyDashRing) {
                if (s.dashing && input
                    && (g_dashStopBoxes.empty() || !dashStopAt(modX, modY, pHalf))) {
                    c.dashing = 1;
                    VYSET(c.vy) = 0.f;
                    a1cVyUpd = 0.0;
                    YSET(c.y) = (float)((double)s.y + (double)s.dashSlope * useDx);
                    yFree = c.y;
                } else {
                    c.dashing = 0;
                }
            }
            if (s.grounded && !pinnedOnBlock
                && (s.flip || (double)s.y > (kGroundY + pHalf) + 0.5)) {
                pinnedOnBlock = true;
                prePinY = c.y;
            }
            if (groundedNow && flyRide) {
                const double newFoot = (double)c.y - gsign * pHalf;
                const bool caught = (newFoot - flyRideFace) * gsign <= 0.0;
                const double floorVelD = flyRideDcy / 0.25;
                if (g_dcyDbg)
                    std::printf("dcystamp site=flyride t=%lld uid=%d dcy=%.17g "
                                "stamp=%.17g vy=%.9f took=%d caught=%d "
                                "foot=%.9f face=%.9f\n",
                                (long long)K.t, flyRideUid, flyRideDcy,
                                floorVelD, (double)c.vy,
                                (caught && (double)c.vy * gsign
                                 < floorVelD * gsign) ? 1 : 0,
                                caught ? 1 : 0, newFoot, flyRideFace);
                if (caught) {
                    YSET(c.y) = (float)(flyRideFace + gsign * pHalf);
                    c.grounded = 1;
                    const double floorVel = flyRideDcy / 0.25;
                    if (g_seatC6) {
                        c6Now = floorVel;
                        if (!((double)c.vy * gsign > 0.0 && !s.onSlope
                              && std::fabs(floorVel) <= 5.0)) {
                            VYSET(c.vy) = 0;
                        } else if ((double)c.vy * gsign < floorVel * gsign) {
                            VYSET(c.vy) = (float)floorVel;
                        }
                    } else if ((double)c.vy * gsign < floorVel * gsign)
                        VYSET(c.vy) = (float)floorVel;
                }
            }
        }
        if (c.frame == 0
            && !c.grounded && c.y <= floorY + pHalf && c.vy <= 0) {
            YSET(c.y) = (float)(floorY + pHalf);
            if (!headBandFresh) { VYSET(c.vy) = 0; CLAMP0("fly/bandfloor"); }
            if (!c.flip) c.grounded = 1;
        } else if (c.frame == 0 && btFlyOn && !c.flip && s.grounded
                   && c.y < floorY + pHalf) {
            YSET(c.y) = (float)(floorY + pHalf);
            c.grounded = 1; CLAMP0("fly/bandcarry");
            bandFloorHeld = true;
        }
        double bandCeilNow = flyFreeNow ? 1e9 : btFlyOn ? btFlyC : (double)s.bandCeil;
        if (!btFlyOn && !g_zoomTrigs.empty() && bandCeilNow < 1e8) {
            const double cs = camScaleAt(x, useDx, (long long)K.t);
            if (cs > 0.0) {
                double numer =
                    (g_freeModeCol && (double)s.bandCeil > (double)s.bandFloor
                     && (double)s.bandCeil < 1e8)
                        ? (double)s.bandCeil - (double)s.bandFloor
                        : kBandBase;
                if ((c.bandBranch & 3) == 1)
                    numer = kBandRetracted
                          - (kBandRetracted - numer) * bandProgress(c.bandAnim);
                if (!g_freeModeCol && g_bandK <= 0.0 && startBandUsable())
                    g_bandK = (g_startBandCeil - g_startBandFloor) * cs
                              / kBandBase;
                const double k = (g_bandK > 0.0) ? g_bandK : 1.0;
                bandCeilNow = (double)s.bandFloor + numer * k / cs;
            }
        }
        const double yMax = bandCeilNow - pHalf;
        if (g_bandDbg && c.mode == 4 && (double)c.y > yMax - 6.0)
            std::printf("banddbg t=%lld x=%.1f y=%.2f band=[%.0f,%.0f] yMax=%.1f\n",
                        (long long)K.t, x, (double)c.y, (double)s.bandFloor,
                        (double)s.bandCeil, yMax);
        bool bandCeilPulled = false;   // --flyclampreach: the clamp moved an upright body this tick
        const bool swingCeilLand = g_swingBandCeil && c.mode == 7 && c.flip
                                   && (s.bandBranch & kBandFreeKnown) && !(s.bandBranch & kBandFree);
        // --swingheadceil: the upright swing meets the same top head first (speed.hpp).
        const bool swingCeilHead = g_swingHeadCeil && c.mode == 7 && !c.flip
                                   && (s.bandBranch & kBandFreeKnown) && !(s.bandBranch & kBandFree);
        if (c.frame == 0 && (c.mode != 7 || swingCeilLand || swingCeilHead) && (double)c.y > yMax) {
            const bool restingOnTop = swingCeilLand && (double)s.y >= yMax - 0.01;
            YSET(c.y) = (float)yMax;
            if (g_swingCeilMoved && (swingCeilLand || swingCeilHead)) {
                bool topMoved = false;
                if (btFlyOn && swingCeilLand) {
                    BandKind pk = BandKind::Unknown, qk = BandKind::Unknown;
                    double pf = 0.0, pc = 0.0, qf = 0.0, qc = 0.0;
                    long long page = 0, qage = 0;
                    topMoved = bandTrackKindAt((long long)K.t, pk, pf, pc, page)
                               && bandTrackKindAt((long long)K.t - 1, qk, qf, qc, qage)
                               && pk == BandKind::Interval && qk == BandKind::Interval && pc != qc;
                }
                if (c.vy > 0) {
                    if (topMoved) {
                        double v = (double)c.vy;
                        const double iv = (double)(int)v;
                        if (v != iv) v = std::round((v - iv) * 1000.0) / 1000.0 + iv;
                        VYSET(c.vy) = (float)v;
                    } else {
                        VYSET(c.vy) = 0;
                    }
                }
            } else if (!restingOnTop) VYSET(c.vy) = 0;
            CLAMP0("fly/bandceil");
            bandCeilPulled = !c.flip;
        }
        if (c.frame == 0
            && (c.y < (floorY + pHalf) - 40 || c.y > g_yBound))
            DIE_AS(kDeathPrune, "fly/out-of-play", nullptr);
        auto sweepFlyHazard = [&](const Obj* o) {
                const bool hazDbg = (g_hazDbgUid >= 0 && o->uid == g_hazDbgUid);
                if (outOfSweepReach(o, xPrev, (double)s.y, x, (double)c.y, pHalf)) {
                    if (hazDbg) std::printf("hazdbg fly t=%lld uid=%d dropped=sweepreach\n",
                                            (long long)K.t, o->uid);
                    return;
                }
                for (int si = 0; si <= kSubSteps && !dead; ++si) {
                    const double f = si / (double)kSubSteps;
                    if (g_hazEndpoint && si < kSubSteps) continue;   // --hazendpoint: endpoint only
                    const double sx = xPrev + (x - xPrev) * f;
                    const double sy = (double)s.y + ((double)c.y - (double)s.y) * f;
                    if (hazDbg)
                        std::printf("hazdbg fly t=%lld uid=%d si=%d sx=%.3f sy=%.3f pHalf=%.2f "
                                    "prot=%.3f cx=%.3f cy=%.3f hw=%.3f hh=%.3f obbOk=%d bc=%.6f "
                                    "bs=%.6f bhw=%.3f bhh=%.3f hit=%d obb=%d\n",
                                    (long long)K.t, o->uid, si, sx, sy, pHalf, (double)s.rot,
                                    o->cx, o->cy, o->hw, o->hh, o->obbOk ? 1 : 0, o->bc, o->bs,
                                    o->bhw, o->bhh,
                                    hazardStage1(o, sx, sy, pHalf, kHazMargin,
                                                 sawMarginFor(s.mode), s.mode, c.mini, g_obbAll) ? 1 : 0,
                                    (o->obbOk && obbOverlap(*o, sx, sy, pHalf, (double)s.rot))
                                        ? 1 : 0);
                    if (hazardStage1(o, sx, sy, pHalf, kHazMargin,
                                     sawMarginFor(s.mode), s.mode, c.mini, g_obbAll)
                        && (!g_obbAll || !o->obbOk
                            || obbOverlap(*o, sx, sy, pHalf, (double)s.rot))) {
                        if (hazOnceSkip()) hazOnceDeferred = true;   // --hazportalonce
                        else if (flyHazRecordOnly) { if (!flyHazPending) flyHazPending = o; }   // --flyhazdefer
                        else DIE("fly/hazard", o);
                    }
                }
        };
        std::vector<const Obj*> solidOrd;   // --solidorder
        bool ceilPushedThisTick = false;    // --flyreland
        const double gsignTick = gsign;
        if (g_flipCollide && c.flip != s.flip) gsign = c.flip ? -1.0 : 1.0;
        for (const Obj* const& o : solidWalk(K, solidOrd)) {
            if (dead) break;
            noteSolidVisit(K, o, x, (double)c.y, pHalf);   // --solidorddbg
            if (o->type != 0) {
                continue;
            }
            const double face = c.flip ? (o->cy - o->hh) : (o->cy + o->hh);
            const double head = c.flip ? (o->cy + o->hh) : (o->cy - o->hh);
            const double vpNow = (double)c.vy * gsign;
            const double prevFootP = ((double)s.y - gsign * pHalf - face) * gsign;
            const double newFootP = ((double)c.y - gsign * pHalf - face) * gsign;
            const double prevHeadP = ((double)s.y + gsign * pHalf - head) * gsign;
            const double newHeadP = ((double)c.y + gsign * pHalf - head) * gsign;
            const bool xOver = std::fabs(x - o->cx) <= o->hw + pHalf;
            const double xPen = (o->hw + pHalf) - std::fabs(x - o->cx);
            const bool deepIn = xPen > kSolidResolveX * pHalf;
            const bool overtaking = ((double)o->dcy * gsign) > vpNow * kYScale;
            const bool clampReach = g_flyClampReach && bandCeilPulled;
            const double mpushTol =
                kShipLandTol + std::fabs((double)o->dcy) / 0.25;
            const double farFace = o->cy - gsign * o->hh;
            const bool sideTop = g_mpushSide
                && std::fabs((double)c.y - o->cy) < o->hh + pHalf
                && ((double)c.y - gsign * pHalf + gsign * mpushTol - farFace) * gsign > 0.0
                && ((double)s.y - gsign * pHalf + gsign * mpushTol - farFace) * gsign > 0.0;
            const bool mpushGeom = o->dcy * gsign > 0.0 && xOver && prevFootP < 0.0 && newFootP < 0.0
                && (((!c.grounded || clampReach)
                     && (((double)c.y - face) * gsign > 0.0 || (clampReach && newHeadP > 0.0)))
                    || sideTop);
            const bool mpushReach =
                newFootP >= -mpushTol || prevFootP >= -mpushTol;
            if (g_slopeDbg && mpushGeom && !mpushReach)
                std::printf("mpush-out-of-reach t=%lld uid=%d face=%.3f "
                            "dcy=%.4f tol=%.3f prevFootP=%.3f newFootP=%.3f\n",
                            (long long)K.t, o->uid, face, o->dcy, mpushTol,
                            prevFootP, newFootP);
            if (mpushGeom && mpushReach) {
                if (g_slopeDbg)
                    std::printf("mpush t=%lld uid=%d ocy=%.3f ohh=%.3f face=%.3f "
                                "dcy=%.4f pHalf=%.3f y %.3f->%.3f\n",
                                (long long)K.t, o->uid, o->cy, o->hh, face, o->dcy,
                                pHalf, (double)c.y, face + gsign * pHalf);
                YSET(c.y) = (float)(face + gsign * pHalf);
                const double surfVp = (double)o->dcy * gsign / 0.25;
                c6Now = (double)o->dcy / 0.25;
                const double vr = (double)c.vy * gsign;
                const bool seatArm = vr > 0.0 && !s.onSlope
                    && std::fabs(surfVp) <= 5.0;
                VYSET(c.vy) = (float)(seatArm ? std::max(vr, surfVp) * gsign
                                              : 0.0);
                CLAMP0O("fly/mpush", o);
                continue;
            }
            const bool slopeTolArm = g_slopeLandTol && s.onSlope && (double)s.slopeM != 0.0;
            const double shipLandTol = slopeTolArm
                ? kShipLandTol + pHalf * (std::sqrt(1.0 + (double)s.slopeM * (double)s.slopeM) - 1.0)
                : s.onSlope ? 0.001 : kShipLandTol;
            const double gsL = c.flip ? -1.0 : 1.0;
            const bool vetoFootL = vetoOf(K, o, x, (double)c.y,
                                          pHalf, pHalf, gsL > 0, xPrev,
                                          (double)s.y, c.slopeUidNow, lawWasOn);
            const bool vetoHeadL =
                vetoOf(K, o, x, (double)c.y, pHalf, pHalf,
                       gsL < 0, xPrev, (double)s.y, c.slopeUidNow, lawWasOn);
            if (vetoFootL && vetoHeadL)
                continue;
            const double prevFootL = ((double)s.y - gsL * pHalf - face) * gsL;
            const double newFootL = ((double)c.y - gsL * pHalf - face) * gsL;
            const double vpL = (double)c.vy * gsL;
            if (g_dynDbg >= 0 && o->uid == g_dynDbg)
                std::printf("flyland? t=%lld uid=%d pHalf=%.2f face=%.3f "
                            "gsL=%.0f grounded=%d vpL=%.3f over=%d xOver=%d "
                            "prevFoot=%.3f (tol %.2f) newFoot=%.3f -> %s\n",
                            (long long)K.t, o->uid, pHalf, face, gsL,
                            (int)c.grounded, vpL, (int)overtaking, (int)xOver,
                            prevFootL, shipLandTol, newFootL,
                            ((!c.grounded || ceilPushedThisTick || slopeTolArm)
                             && (vpL <= 0 || overtaking || slopeTolArm) && xOver
                             && prevFootL >= -shipLandTol && newFootL <= 0)
                                ? "LAND" : "no");
            if ((!c.grounded || ceilPushedThisTick || slopeTolArm)
                && (vpL <= 0 || overtaking || slopeTolArm) && xOver && !vetoFootL
                && prevFootL >= -shipLandTol && newFootL <= 0) {
                if (!landedThisTick) preLandY = c.y;
                landedThisTick = true;
                const float yBeforeLand = c.y;
                YSET(c.y) = (float)(face + gsL * pHalf);
                if (slopeTolArm && K.slopes) {
                    const Obj* last = nullptr;
                    for (const Obj* R : *K.slopes)
                        if ((!last || gdPassAfter(*R, *last))
                            && slopeWouldAcquire(R, x, (double)yBeforeLand, pHalf,
                                                 R->uid == s.slopeUidNow))
                            last = R;
                    if (last) offRampRide = last;
                }
                const bool seatC6 = g_seatC6 && o->dcy != 0.0;
                if (seatC6) c6Now = (double)o->dcy / 0.25;
                if (vpL <= 0) {
                    VYSET(c.vy) = 0; CLAMP0O("fly/land", o);
                } else if (slopeTolArm) {
                    VYSET(c.vy) = 0; CLAMP0O("fly/land-offramp", o);   // m_wasOnSlope: hitGround's arm
                } else if (seatC6
                           && !(!s.onSlope && std::fabs((double)o->dcy / 0.25) <= 5.0)) {
                    VYSET(c.vy) = 0; CLAMP0O("fly/land", o);   // --seatc6: the hitGround arm
                } else {
                    const double fv = (double)o->dcy / 0.25;
                    if (g_dcyDbg)
                        std::printf("dcystamp site=floorcatch t=%lld uid=%d "
                                    "dcy=%.17g stamp=%.17g vy=%.9f took=%d\n",
                                    (long long)K.t, o->uid, o->dcy, fv,
                                    (double)c.vy,
                                    (double)c.vy * gsL < fv * gsL ? 1 : 0);
                    if ((double)c.vy * gsL < fv * gsL)
                        VYSET(c.vy) = (float)fv;
                }
                c.grounded = 1;
                if (offRampRide) { offRampY = c.y; offRampVy = c.vy; }
            } else if ((vpNow >= 0 || (prevHeadP > 0 && deepIn && !overtaking)
                        || (gsign * o->dcy < 0.0
                            && prevHeadP > 0))
                       && xOver
                       && prevHeadP <= kShipLandTol && newHeadP >= 0
                       && !(s.ceilT > 0)
                       && !vetoHeadL) {
                if (g_slopeDbg)
                    std::printf("ceilride t=%lld uid=%d x=%.3f ocy=%.3f "
                                "ohh=%.3f head=%.3f dcy=%.4f vp=%.4f "
                                "prevH=%.4f newH=%.4f xpen=%.3f pHalf=%.2f "
                                "mini=%d y %.4f->%.4f\n",
                                (long long)K.t, o->uid, x, o->cy, o->hh, head,
                                o->dcy, vpNow * kYScale, prevHeadP, newHeadP,
                                xPen, pHalf, (int)c.mini, (double)c.y,
                                head - gsign * pHalf);
                YSET(c.y) = (float)(head - gsign * pHalf);
                const bool flyMode = (c.mode == 1 || c.mode == 3
                                      || c.mode == 4 || c.mode == 7);
                if (g_dcyDbg)
                    std::printf("dcystamp site=ceilpin t=%lld uid=%d dcy=%.17g "
                                "stamp=%.17g vy=%.9f took=%d\n",
                                (long long)K.t, o->uid, o->dcy,
                                (double)o->dcy / 0.25, (double)c.vy,
                                (flyMode && o->dcy != 0.0) ? 1 : 0);
                const bool faceWithGrav = gsign * o->dcy < 0.0;
                if (flyMode && faceWithGrav) {
                    const double vr = gsign * (double)c.vy;
                    VYSET(c.vy) = (float)(vr < 0.0 ? gsign * std::min(vr, -1.0)
                                                   : 0.0);
                } else if (flyMode && o->dcy != 0.0)
                    VYSET(c.vy) = (float)((double)o->dcy / 0.25);
                else
                    VYSET(c.vy) = 0;
                CLAMP0O("fly/ceilride", o);
                ceilPushedThisTick = true;
            } else if (c.mode == 7 && !deepIn && xOver
                       && std::fabs(xPrev - o->cx) > o->hw + pHalf
                       && std::fabs((double)c.y - o->cy) < o->hh + pHalf
                       && std::min(std::fabs(o->cy + o->hh + pHalf - (double)c.y),
                                   std::fabs(o->cy - o->hh - pHalf - (double)c.y))
                              < 6.0) {
                const double above = o->cy + o->hh + pHalf;
                const double below = o->cy - o->hh - pHalf;
                const double yNow  = (double)c.y;
                const double tgt = (std::fabs(above - yNow) <= std::fabs(below - yNow))
                                   ? above : below;
                if ((tgt - yNow) * (double)c.vy < 0.0) {
                    for (int si = 0; si <= kSubSteps && !dead; ++si) {
                        const double f = si / (double)kSubSteps;
                        const double sx = xPrev + (x - xPrev) * f;
                        const double sy = (double)s.y + ((double)c.y - (double)s.y) * f;
                        if (std::fabs(sy - o->cy) < o->hh + pInner + kHazMargin
                            && std::fabs(sx - o->cx) < o->hw + pInner + kHazMargin)
                            DIE("fly/solid-side", o);
                    }
                    continue;
                }
                YSET(c.y) = (float)tgt;
                CLAMP0O("fly/swingpush", o);
            } else if (!o->oneway) {
                const bool faceContact = footFaceContact(*o, c, pHalf);
                for (int si = kSubSteps; si <= kSubSteps && !dead && !faceContact; ++si) {
                    const double f = si / (double)kSubSteps;
                    const double sx = xPrev + (x - xPrev) * f;
                    const double sy = (double)s.y + ((double)c.y - (double)s.y) * f;
                    if (std::fabs(sy - o->cy) < o->hh + pInner + kHazMargin
                        && std::fabs(sx - o->cx) < o->hw + pInner + kHazMargin)
                        DIE("fly/solid-side", o);
                }
            }
        }
        gsign = gsignTick;   // --flipcollide: the rest of the branch keeps the tick's opening sign
        {
            // --flyhazdefer (speed.hpp): a ship's or UFO's hit here waits for the ramp pass
            flyHazRecordOnly = g_flyHazDefer && (c.mode == 1 || c.mode == 3);
            for (const Obj* o : *K.near) {
                if (dead) break;
                if (o->type == 0) continue;
                sweepFlyHazard(o);
            }
            flyHazRecordOnly = false;
        }
    }
    {
        if (!a1cNoFall && a1cNow) {
            const bool fixedArm = g_fixGravityBug != 0 || s.frame != 0 || s.mode == 7;
            if (a1cFallingBugged(a1cVyUpd, a1cUpUpd, s.dual != 0, fixedArm,
                                 gdapprox::accelSwitchVyForSpeed(speedMulForDx(useDx))))
                a1cNow = false;
        }
        if (a1cEarlySet) a1cNow = true;
    }
    if (g_prePortalUid >= 0 && K.ports) {
        for (const Obj* p : *K.ports) {
            if (p->uid != g_prePortalUid) continue;
            const double pHalfPre = (c.mode == 4 && c.mini) ? kWaveContactHalfMini : pHalf;
            const uint8_t wantUp = (p->type == 3) ? 1 : 0;
            const bool overlaps = std::fabs(x - p->cx) <= p->hw + pHalfPre
                                  && std::fabs((double)yFree - p->cy) <= p->hh + pHalfPre;
            if (overlaps && gdUpOf(c) != wantUp && p->gpBit >= 0
                && !s.portalLatch.test(p->gpBit)) {
                c.portalLatch.set(p->gpBit);
                VYSET(c.vy) = (float)((double)c.vy * kGravPortalScale);
                c.flip = (c.frame == 3) ? (uint8_t)!wantUp : wantUp;
            }
            break;
        }
    }
    {
        const bool waveRide = waveRampArm && c.mode == 4 && !c.mini
                              && !c.flip && c.frame == 0;
        const double pH = (c.mode == 6)
            ? (c.mini ? kSpiderHalfMini : kSpiderHalf)
            : waveRide ? kWaveHalf
            : (c.mini ? kMiniHalf : kCubeHalf);
        c.onSlope = 0;
        bool rampWindowHere = false;
        bool rampEndHere = false;
        bool rampOkHere = false;
        // --movingwallrise: a moved ramp wall's top seated the body this tick (the launch below)
        bool movWallTopNow = false;
        yFreeBeforeSlope = c.y;
        if (K.slopes) {
            if (g_slopeDbg) {
                std::printf("slopeset t=%lld x=%.2f n=%zu uids=", (long long)K.t,
                            x, K.slopes->size());
                for (const Obj* sp : *K.slopes) std::printf("%d ", sp->uid);
                std::printf("\n");
            }
            bool ceilContNow = false;
            if (!c.flip && !s.flip && c.mode == s.mode
                && (c.mode == 1 || c.mode == 3 || c.mode == 4 || c.mode == 7)) {
                for (const Obj* sp : *K.slopes) {
                    if (!slopeIsCeiling(sp->slopeDir) || sp->slopeHazard) continue;
                    const double x0C = sp->cx - sp->hw, x1C = sp->cx + sp->hw;
                    if (x1C <= x0C || (x >= x0C && x <= x1C)) continue;
                    if (std::fabs(x - sp->cx) > 2.0 * sp->hw + pH
                        || std::fabs((double)c.y - sp->cy) > 2.0 * sp->hh + pH)
                        continue;
                    // Seated on this ramp last tick: s.y is its seat at xPrev.
                    if (std::fabs((double)s.y
                                  - slopeSeatTarget(sp, xPrev, pH, true, 0.0))
                        > 0.001)
                        continue;
                    const double mC = (sp->sy1 - sp->sy0) / (x1C - x0C);
                    const double tC = sp->sy0 + mC * (x - x0C)
                        - pH / std::cos(std::atan(std::fabs(mC)));
                    if (tC < sp->cy - sp->hh - pH || tC > sp->cy + sp->hh)
                        continue;
                    if (tC == (double)s.y) continue;
                    bool collinearHeld = false;
                    for (const Obj* o2 : *K.slopes) {
                        if (o2 == sp || !slopeIsCeiling(o2->slopeDir)
                            || o2->slopeHazard)
                            continue;
                        if (x < o2->cx - o2->hw || x > o2->cx + o2->hw) continue;
                        if (std::fabs(slopeSeatTarget(o2, x, pH, true, 0.0) - tC)
                            <= 0.001) {
                            collinearHeld = true;
                            break;
                        }
                    }
                    if (collinearHeld) continue;
                    const double tolC =
                        (input && !slopeBVar22(false, sp->slopeDir)) ? 2.0 : 0.0;
                    if (!((double)c.y > tC - tolC)) continue;
                    if (g_slopeDbg)
                        std::printf("ceilcont t=%lld x=%.3f uid=%d y=%.6f -> %.6f "
                                    "tol=%.0f\n", (long long)K.t, x, sp->uid,
                                    (double)c.y, tC, tolC);
                    YSET(c.y) = (float)tC;
                    if ((double)c.vy > 0.0) VYSET(c.vy) = 0;
                    if (slopeNudgeMode(c.mode)) {
                        const float vN = slopeNudge(c.vy, c.flip != 0, sp->slopeDir, c.mode,
                                                    input);
                        if (vN != c.vy) VYSET(c.vy) = vN;
                    }
                    c.grounded = 0;
                    CLAMP0O("slope/ceilcont", sp);
                    ceilContNow = true;
                    const bool revC2 = ((double)K.dxF < 0.0 || s.rev != 0);
                    if ((revC2 ? -mC : mC) < 0.0) {
                        ceilPressedNow = true;
                        ceilPressM = mC; ceilPressMode = c.mode; ceilPressUid = sp->uid;
                    }
                    break;
                }
            }
            const bool impulsedOffSlope = impulsedThisTick && s.onSlope;
            const bool tappedOffSlope = ballFlippedThisTick && s.onSlope;
            const bool stickToSlope =
                s.onSlope && !impulsedThisTick && !stepLandedThisTick
                && s.grounded;
            bool slopeTookInside = false;
            bool pushOutSeat = false;
            double pushOutTop = -1e18;
            bool lawSeat = false;
            double lawTop = 0.0;
            int lawUid = -1;
            bool lawUnder = false;
            double lawM = 0.0;   // --ufolawflap: the seat's ramp slope (world frame)
            const float yBeforeSlopes = c.y;
            suYPre = c.y;
            suYPreSet = true;
            const uint8_t gBeforeSlopes = c.grounded;
            const Obj* topVetoCur = nullptr;
            bool topVetoClimb = false;
            if (s.onSlope && c.frame == 0) {
                for (const Obj* q : *K.slopes)
                    if (q->uid == s.slopeUidNow) { topVetoCur = q; break; }
                const double mTravV = (double)s.slopeM
                    * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                topVetoClimb = c.flip ? (mTravV < 0.0) : (mTravV > 0.0);
            }
            bool movWallSet = false;
            float movWallY = 0.f, movWallVy = 0.f;
            for (const Obj* sp : *K.slopes) {
                PortSeatWatch portSeatWatch(c.y, sp, portSeatMover);   // --portseatorder
                // the wave does not ride anything -- it only ever dies (but see --waveramp)
                if (c.mode == 4 && !waveRide) break;
                const double x0 = sp->cx - sp->hw, x1 = sp->cx + sp->hw;
                const double m = (sp->sy1 - sp->sy0) / (x1 - x0);
                if (m == 0.0) continue;
                if (c.frame == 0 && sp->uid != c.slopeUidNow) {
                    const uint8_t dP = sp->slopeDir;
                    const bool wallLeftP = dP == 2 || dP == 3 || dP == 4 || dP == 6;
                    const bool topP = dP == 1 || dP == 3 || dP == 5 || dP == 6;
                    const bool goingLeftP = (double)K.dxF < 0.0 || s.rev != 0;
                    const double sMinY = sp->cy - sp->hh, sMaxY = sp->cy + sp->hh;
                    const double pyP = (double)c.y;
                    bool hitP = wallLeftP ? (x0 > x) : (goingLeftP && x > x1);
                    if (topP ? (pyP >= sMaxY) : (pyP <= sMinY + 1.0)) hitP = true;
                    const bool meetP = !(x0 > x + pH || x - pH > x1
                                         || sMinY > pyP + pH || pyP - pH > sMaxY);
                    if (hitP) {
                        double wx0 = 0, wx1 = 0, wy0 = 0, wy1 = 0;
                        bool wall = false;
                        double inset = 0.0;
                        if (wallLeftP && x0 > x) {
                            inset = s.rev ? 1.0 : 0.0;
                            wall = true; wx0 = x0; wx1 = x0 + 1.0;
                            wy0 = sMinY + inset; wy1 = sMaxY - inset;
                        } else if (!wallLeftP && goingLeftP && x > x1) {
                            inset = s.rev ? 0.0 : 1.0;
                            wall = true; wx0 = x1 - 1.0; wx1 = x1;
                            wy0 = sMinY + inset; wy1 = sMaxY - inset;
                        }
                        const double prX = x;    // the player's rect as preSlopeCollision took it
                        const double prY = (double)c.y;
                        auto thinSolid = [&](double rx0, double rx1, double ry0, double ry1,
                                             bool ownWall) {
                            if (rx0 > prX + pH || prX - pH > rx1
                                || ry0 > prY + pH || prY - pH > ry1) return;
                            const bool flyM = c.mode == 1 || c.mode == 3 || c.mode == 7 || waveRide;
                            const bool fbM = flyM || c.mode == 2;
                            const double tol = flyM ? 6.0 : 10.0;
                            const double yy = (double)c.y;
                            const double bot = yy - pH, top = yy + pH;
                            const double pbot = (double)s.y - pH, ptop = (double)s.y + pH;
                            const bool flipped = c.flip != 0;
                            const bool wasOn = s.onSlope != 0;
                            auto thinLandJump = [&]() {
                                if (!g_thinLandJump || !c.grounded) return;
                                if (!((c.mode == 0 || c.mode == 5) && input && !s.action
                                      && !impulsedThisTick && !c.pressSpent && c.flip == s.flip))
                                    return;
                                impulsedThisTick = IMPULSE();
                                a1cJump();   // --a1clatch
                                double vj = cubePhysFor(useDx).jump
                                            * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0);
                                if (c.mode == 5) {
                                    vj *= kRobotJumpScale;
                                    c.rHover = (uint8_t)kRobotHoverTicks;
                                }
                                VYSET(c.vy) = (float)(vj * (c.flip ? -1.0 : 1.0));
                                c.grounded = 0;
                            };
                            auto vetoed = [&](bool faceIsTop) {
                                Obj tr{};
                                tr.type = 0;
                                tr.cx = (rx0 + rx1) / 2.0; tr.cy = (ry0 + ry1) / 2.0;
                                tr.hw = (rx1 - rx0) / 2.0; tr.hh = (ry1 - ry0) / 2.0;
                                if (g_stripVetoOrder && !ownWall && K.slopes) {
                                    if (!flipped && !s.grounded) {
                                        const bool ceilO = slopeIsCeiling(sp->slopeDir);
                                        const double sx0 = sp->cx - sp->hw, sx1 = sp->cx + sp->hw;
                                        const double lineO = (sx1 > sx0)
                                            ? sp->sy0 + (sp->sy1 - sp->sy0) * (x - sx0) / (sx1 - sx0)
                                            : 0.0;
                                        if (sx1 > sx0 && slopeWouldAcquire(sp, x, yy, pH, false)
                                            && (ceilO ? yy < lineO : yy > lineO))
                                            return true;
                                    }
                                    std::vector<const Obj*> mapped;
                                    for (const Obj* q : *K.slopes)
                                        if (q->uid < sp->uid || q->uid == c.slopeUidNow)
                                            mapped.push_back(q);
                                    return slopeVetoesSolidIn(&tr, mapped.data(),
                                                              mapped.data() + mapped.size(), x, yy,
                                                              pH, pH, faceIsTop, xPrev, (double)s.y,
                                                              c.slopeUidNow, true, nullptr);
                                }
                                return slopeVetoesSolid(&tr, K.slopes, x, yy, pH, pH, faceIsTop,
                                                        xPrev, (double)s.y, c.slopeUidNow, true,
                                                        ownWall ? sp : nullptr);
                            };
                            if ((!flipped || fbM) && (bot + tol >= ry1 || pbot + tol >= ry1)) {
                                if (g_movingWallHead && flipped && c.mode != 3 && (double)c.vy > 0.0
                                    && sp->dynObj && sp->dcy > 0.0
                                    && (ry1 <= top - tol || ry1 <= ptop - tol)) {
                                    YSET(c.y) = (float)(ry1 + pH);
                                    VYSET(c.vy) = std::max(c.vy, 1.0f);
                                    c.grounded = 0;
                                    CLAMP0O("slope/thin-head-moving", sp);
                                    return;
                                }
                                const bool riseSeat = g_movingWallRise && !flipped && sp->dynObj
                                                      && sp->dcy > 0.0 && (double)c.vy > 0.0 && !wasOn;
                                if (((double)c.vy <= 0.0 || wasOn) && !vetoed(true)) {
                                    YSET(c.y) = (float)(ry1 + pH);
                                    VYSET(c.vy) = 0.f;
                                    c.grounded = 1;
                                    CLAMP0O("slope/thin-top", sp);
                                    thinLandJump();
                                } else if (g_movingWallRise && !flipped && sp->dynObj && sp->dcy != 0.0
                                           && ((double)c.vy <= 0.0 || wasOn || riseSeat)) {
                                    YSET(c.y) = (float)(ry1 + pH);
                                    VYSET(c.vy) = std::max(c.vy, (float)(sp->dcy / 0.25));
                                    c.grounded = 0;
                                    movWallTopNow = true;
                                    CLAMP0O("slope/thin-top-moving", sp);
                                } else if (g_movingWallSeat && !flipped && sp->dynObj && sp->dcy != 0.0
                                           && ((double)c.vy <= 0.0 || wasOn)) {
                                    YSET(c.y) = (float)(ry1 + pH);
                                    VYSET(c.vy) = (float)(sp->dcy / 0.25);
                                    c.grounded = 0;
                                    CLAMP0O("slope/thin-top-moving", sp);
                                }
                                return;
                            }
                            if ((flipped || fbM) && (ry0 >= top - tol || ry0 >= ptop - tol)) {
                                if (((double)c.vy >= 0.0 || wasOn) && !vetoed(false)) {
                                    YSET(c.y) = (float)(ry0 - pH);
                                    VYSET(c.vy) = 0.f;
                                    if (flipped) c.grounded = 1;
                                    CLAMP0O("slope/thin-bottom", sp);
                                    if (flipped) thinLandJump();
                                } else if (g_movingWallSeat && flipped && sp->dynObj && sp->dcy != 0.0
                                           && ((double)c.vy >= 0.0 || wasOn)) {
                                    YSET(c.y) = (float)(ry0 - pH);
                                    VYSET(c.vy) = (float)(sp->dcy / 0.25);
                                    c.grounded = 0;
                                    CLAMP0O("slope/thin-bottom-moving", sp);
                                    movWallSet = true;
                                    movWallY = c.y;
                                    movWallVy = c.vy;
                                }
                                return;
                            }
                            const double ih = 0.3 * pH;
                            if (!(rx0 > x + ih || x - ih > rx1
                                  || ry0 > yy + ih || yy - ih > ry1))
                                DIE("slope/thin-crush", sp);
                        };
                        if (wall) thinSolid(wx0, wx1, wy0, wy1, true);
                        if (!dead && (topP ? ((double)c.y >= sMaxY) : ((double)c.y <= sMinY + 1.0))) {
                            if (topP) thinSolid(x0 + inset, x1 - inset, sMaxY - 1.0, sMaxY, false);
                            else thinSolid(x0 + inset, x1 - inset, sMinY, sMinY + 1.0, false);
                        }
                        continue;
                    }
                    if (!meetP) continue;
                }
                if (sp->slopeHazard) continue;
                if (topVetoCur && topVetoClimb) {   // --slopetopveto, see above the loop
                    const auto topDir = [](uint8_t d) { return d == 1 || d == 3 || d == 5 || d == 6; };
                    const bool upV = sp->slopeDir == 0 || sp->slopeDir == 3
                                  || sp->slopeDir == 6 || sp->slopeDir == 7;
                    const bool leftV = (double)K.dxF < 0.0 || s.rev != 0;
                    const bool bandV = ((!upV) != leftV) != (c.flip != 0);
                    const bool curTop = topDir(topVetoCur->slopeDir);
                    if (bandV && curTop == topDir(sp->slopeDir) && curTop == (c.flip != 0))
                        continue;
                }
                const bool ceilRampSide = slopeIsCeiling(sp->slopeDir);
                const bool flipForRide =
                    tappedOffSlope ? (s.flip != 0) : (c.flip != 0);
                bool ridesTop = !flipForRide;
                if (flipForRide && !ceilRampSide) {
                    // came from ON or ABOVE the upright resting height?
                    const double ru = xPrev + (m > 0 ? slopeXOffset(m, pH)
                                                     : -slopeXOffset(m, pH));
                    const double topPrevUp =
                        sp->sy0 + m * (std::min(std::max(ru, x0), x1) - x0) + pH;
                    ridesTop = ((double)s.y - topPrevUp) >= -kLandTol;
                }
                const double gs = ridesTop ? 1.0 : -1.0;
                const double seatOff = slopeSeatOff(sp, K.slopes,
                                                    s.onSlope ? (int)s.slopeUidNow : -1,
                                                    s.onSlope != 0, c.mini != 0);
                struct Smp { double xr; bool ok; bool inside; };
                auto rideSeatMoves = [&](double cx) {
                    if (!(s.onSlope && c.frame == 0 && (int)s.slopeUidNow == sp->uid
                          && cx == x))
                        return false;
                    const float seatR =
                        (float)slopeSeatTarget(sp, cx, pH, false, seatOff);
                    return seatR != s.y;
                };
                auto sampleAt = [&](double cx) {
                    if (ridesTop) {
                        const double r = cx + (m > 0 ? slopeXOffset(m, pH)
                                                     : -slopeXOffset(m, pH));
                        {
                            const bool revE =
                                ((double)K.dxF < 0.0 || s.rev != 0);
                            const double mTravS = revE ? -m : m;
                            if (mTravS < -0.01 && stickToSlope && s.onSlope
                                && !c.flip && c.mode == 0) {
                                double extC =
                                    pH * std::sqrt(1.0 + m * m)
                                    / std::fabs(m);
                                bool flushFlat = false;
                                if (std::fabs((revE ? (double)sp->sy0
                                                    : (double)sp->sy1)
                                              - kGroundY) <= 0.5) {
                                    extC = slopeXOffset(m, pH)
                                         + kStickGap / std::fabs(m);
                                    flushFlat = true;
                                }
                                if (K.near && !flushFlat) {
                                    const double xEndF = revE ? x0 : x1;
                                    const double yEndF = revE
                                        ? (double)sp->sy0 : (double)sp->sy1;
                                    const double probeX =
                                        xEndF + (revE ? -1.0 : 1.0);
                                    for (const Obj* nb : *K.near) {
                                        if (nb->type != 0) continue;
                                        if (std::fabs((nb->cy + nb->hh)
                                                      - yEndF) > 0.5)
                                            continue;
                                        if (nb->cx - nb->hw <= probeX
                                            && probeX <= nb->cx + nb->hw) {
                                            extC = slopeXOffset(m, pH)
                                                 + kStickGap / std::fabs(m);
                                            flushFlat = true;
                                            break;
                                        }
                                    }
                                }
                                bool okE = revE
                                    ? (r <= x1 && cx >= x0 - extC)
                                    : (r >= x0 && cx <= x1 + extC);
                                if (!okE && !flushFlat
                                    && (revE ? r <= x1 : r >= x0)
                                    && rideSeatMoves(cx))
                                    okE = true;
                                return Smp{r, okE, r >= x0 && r <= x1};
                            }
                        }
                        const bool rev = ((double)K.dxF < 0.0 || s.rev != 0);
                        const double rideGrace = std::fabs(useDx);
                        bool ok = rev
                            ? (r >= x0 - rideGrace && r <= x1)
                            : (r >= x0 && r <= x1 + rideGrace);
                        if (!ok && rideSeatMoves(cx)) ok = true;
                        const double mTravHi = rev ? -m : m;
                        if (!ok && c.grounded && !s.grounded
                            && mTravHi < 0.0) {
                            const double capHi =
                                std::max((double)sp->sy0, (double)sp->sy1) + pH;
                            if ((double)s.y >= capHi - 0.02
                                && std::fabs((double)c.y - capHi) <= 0.6
                                && cx + pH >= x0 && cx - pH <= x1)
                                ok = true;
                        }
                        if (!ok && stickToSlope && s.grounded && K.near
                            && (rev ? -m : m) < -0.01) {
                            const double xEndF = rev ? x0 : x1;
                            const double yEndF = rev
                                ? (double)sp->sy0 : (double)sp->sy1;
                            const double probeX = xEndF + (rev ? -1.0 : 1.0);
                            for (const Obj* nb : *K.near) {
                                if (nb->type != 0) continue;
                                if (std::fabs((nb->cy + nb->hh) - yEndF)
                                    > 0.5)
                                    continue;
                                if (nb->cx - nb->hw <= probeX
                                    && probeX <= nb->cx + nb->hw) {
                                    const double cornB =
                                        slopeXOffset(m, pH)
                                        + kStickGap / std::fabs(m);
                                    ok = rev ? (cx >= x0 - cornB)
                                             : (cx <= x1 + cornB);
                                    break;
                                }
                            }
                        }
                        if (g_slopeDbg)
                            std::printf("slopeform t=%lld uid=%d mode=%d m=%.3f "
                                        "cx=%.3f x0=%.1f x1=%.1f r=%.3f "
                                        "stick=%d ok=%d  [rot-branch]\n",
                                        (long long)K.t, sp->uid, (int)c.mode, m,
                                        cx, x0, x1, r, stickToSlope ? 1 : 0,
                                        ok ? 1 : 0);
                        return Smp{std::min(std::max(r, x0), x1), ok,
                                   r >= x0 && r <= x1};
                    }
                    const double a = std::min(std::max(cx - pH, x0), x1);
                    const double b = std::min(std::max(cx + pH, x0), x1);
                    const double sa = sp->sy0 + m * (a - x0);
                    const double sb = sp->sy0 + m * (b - x0);
                    const bool centre = (c.mode == 2
                                         || (c.mode == 0 && s.dual));
                    const bool contactPt = (c.mode == 0 || c.mode == 3
                                            || c.mode == 5 || c.mode == 6
                                            || c.mode == 7);
                    const double rcH =
                        cx + (m > 0 ? -slopeXOffset(m, pH)
                                    : slopeXOffset(m, pH));
                    const bool revT7 = ((double)K.dxF < 0.0 || s.rev != 0);
                    const double cornG = std::fabs((double)useDx);
                    const bool ok = centre
                        ? (cx >= x0 && cx <= x1)
                        : contactPt
                        ? ((c.mode == 7)
                           ? (revT7 ? (rcH >= x0 - cornG && rcH <= x1)
                                    : (rcH >= x0 && rcH <= x1 + cornG))
                           : (rcH >= x0 && rcH <= x1))
                        : (cx + pH >= x0 && cx - pH <= x1);
                    if (g_slopeDbg)
                        std::printf("slopeform t=%lld uid=%d mode=%d m=%.3f "
                                    "cx=%.3f x0=%.1f x1=%.1f pH=%.1f "
                                    "centre=%d contactPt=%d rcH=%.3f "
                                    "cornG=%.3f ok=%d\n",
                                    (long long)K.t, sp->uid, (int)c.mode, m, cx,
                                    x0, x1, pH, centre ? 1 : 0,
                                    contactPt ? 1 : 0, rcH, cornG, ok ? 1 : 0);
                    if (c.mode == 7)
                        return Smp{std::min(std::max(rcH, x0), x1), ok, ok};
                    if (c.mode == 2) {
                        const bool revB = ((double)K.dxF < 0.0 || s.rev != 0);
                        const double xoffB = slopeXOffset(m, pH);
                        const double graceB = cornG;
                        double okLo = x0, okHi = x1;
                        if (!revB && m < 0) okHi = x1 - xoffB + graceB;
                        else if (revB && m > 0) okLo = x0 + xoffB - 1.5;
                        if (g_slopeDbg)
                            std::printf("slopeok t=%lld uid=%d m=%.3f cx=%.3f "
                                        "x0=%.1f x1=%.1f xoff=%.3f rev=%d "
                                        "okLo=%.3f okHi=%.3f ok=%d\n",
                                        (long long)K.t, sp->uid, m, cx, x0, x1,
                                        xoffB, revB ? 1 : 0, okLo, okHi,
                                        (cx >= okLo && cx <= okHi) ? 1 : 0);
                        return Smp{std::min(std::max(cx, x0), x1),
                                   cx >= okLo && cx <= okHi,
                                   cx >= x0 && cx <= x1};
                    }
                    if (c.mode == 1 && (m > 0 ? (cx > x1) : (cx < x0))) {
                        const double rel = pH - slopeXOffset(m, pH);
                        const bool okC = (m > 0) ? (cx <= x1 + rel)
                                                 : (cx >= x0 - rel);
                        return Smp{(m > 0) ? x1 : x0, okC, okC};
                    }
                    if (c.mode == 1) {
                        const bool revH =
                            ((double)K.dxF < 0.0 || s.rev != 0);
                        const double xoffH = slopeXOffset(m, pH);
                        const double cornGS = std::fabs((double)useDx);
                        if (!revH && m < 0 && cx + xoffH > x1 + cornGS)
                            return Smp{x1, false, false};
                        if (revH && m > 0 && cx - xoffH < x0 - cornGS)
                            return Smp{x0, false, false};
                        if (!revH && m > 0 && cx - xoffH < x0)
                            return Smp{x0, false, false};
                        if (revH && m < 0 && cx + xoffH > x1)
                            return Smp{x1, false, false};
                    }
                    if ((c.mode == 0 || c.mode == 5) && c.frame == 0
                        && s.onSlope && (int)s.slopeUidNow == sp->uid && cx == x) {
                        const float seatH = (float)slopeSeatTarget(sp, cx, pH, true, seatOff);
                        if (seatH == s.y) return Smp{(m < 0) ? x1 : x0, false, false};
                    }
                    return Smp{(sa <= sb) ? a : b, ok, ok};
                };
                const auto smp = sampleAt(x);
                const bool ceilRamp = ceilRampSide;
                auto ceilLimSeat = [&](double cx, bool gdSeat) {
                    const double r = cx + (m > 0 ? -slopeXOffset(m, pH)
                                                 : slopeXOffset(m, pH));
                    const double rc = std::min(std::max(r, x0), x1);
                    const double flat =
                        gdSeat ? slopeSeatTarget(sp, cx, pH, true, seatOff)
                               : sp->sy0 + m * (rc - x0) - pH;
                    const bool wallSideL = c.mode == 1
                        && ((sp->slopeDir == 2 || sp->slopeDir == 3 || sp->slopeDir == 4
                             || sp->slopeDir == 6)
                                ? cx < x0
                                : (((double)K.dxF < 0.0 || s.rev != 0) && cx > x1));
                    if (c.mode == 1 && (r > x1 || r < x0)) {
                        for (const Obj* nb : *K.slopes) {
                            if (nb == sp || nb->slopeHazard) continue;
                            const double nx0 = nb->cx - nb->hw;
                            const double nx1 = nb->cx + nb->hw;
                            if (nx1 <= nx0) continue;
                            const double nm = (nb->sy1 - nb->sy0)
                                            / (nx1 - nx0);
                            if (nm == 0.0) continue;
                            const bool nbCeil =
                                (c.mode == 1 || c.mode == 7 || c.mode == 0)
                                ? slopeIsCeiling(nb->slopeDir)
                                : (nb->slopeDir == 1 || nb->slopeDir == 3);
                            if (!nbCeil) continue;
                            if (r > x1 && std::abs(nx0 - x1) <= 0.5
                                && std::abs((double)nb->sy0
                                            - (double)sp->sy1) <= 0.5) {
                                const double rr = std::min(r, nx1);
                                const double ext = nb->sy0
                                    + nm * (rr - nx0) - pH;
                                return (nm * m > 0.0 || wallSideL) ? ext
                                                                   : std::min(ext, flat);
                            }
                            if (r < x0 && std::abs(nx1 - x0) <= 0.5
                                && std::abs((double)nb->sy1
                                            - (double)sp->sy0) <= 0.5) {
                                const double rr = std::max(r, nx0);
                                const double ext = nb->sy0
                                    + nm * (rr - nx0) - pH;
                                return (nm * m > 0.0 || wallSideL) ? ext
                                                                   : std::min(ext, flat);
                            }
                        }
                    }
                    return flat;
                };
                auto ceilLimAt = [&](double cx) {
                    return ceilLimSeat(cx, true);
                };
                if (!c.flip && ceilRamp && !sp->slopeHazard
                    && (double)c.y + pH <= sp->cy + sp->hh + 0.001) {
                    const double rcU = x + (m > 0 ? -slopeXOffset(m, pH)
                                                  : slopeXOffset(m, pH));
                    const double limU = ceilLimAt(x);
                    const bool inYU = ((double)c.y + pH >= sp->cy - sp->hh)
                                   && ((double)c.y - pH <= sp->cy + sp->hh);
                    if (g_slopeDbg)
                        std::printf("upceil t=%lld x=%.2f uid=%d y=%.3f "
                                    "lim=%.3f rc=%.2f x0=%.1f x1=%.1f inY=%d\n",
                                    (long long)K.t, x, sp->uid, (double)c.y,
                                    limU, rcU, x0, x1, inYU ? 1 : 0);
                    if ((c.mode == 0 || c.mode == 5 || c.mode == 6)
                        && !s.onSlope && s.ceilT == 0) {
                        const bool graceC =
                            (((c.mode != s.mode) ? 0 : (int)s.modeT + 1) < kFlipGraceTicks)
                            || (((gdUpOf(c) != gdUpOf(s)) ? 0 : (int)s.flipT + 1)
                                < kFlipGraceTicks);
                        const uint8_t dC = sp->slopeDir;
                        const bool leftFaceC = (dC == 2 || dC == 3 || dC == 4 || dC == 6);
                        const bool probeC = leftFaceC
                            ? (x < sp->cx - sp->hw)
                            : (((double)K.dxF < 0.0 || s.rev != 0) && sp->cx + sp->hw < x);
                        if (!graceC && !probeC
                            && slopeWouldAcquire(sp, x, (double)c.y, pH, false)) {
                            const double tgtC = slopeSeatTarget(sp, x, pH, true, seatOff);
                            if (g_slopeDbg)
                                std::printf("undercrush: t=%lld p%d uid=%d y=%.4f seat=%.4f\n",
                                            (long long)K.t, g_halfNow + 1, sp->uid,
                                            (double)c.y, tgtC);
                            if ((double)c.y - 2.0 > tgtC) DIE("slope/crush", sp);
                            else c.grounded = 0;
                            continue;
                        }
                    }
                    const bool revU = ((double)K.dxF < 0.0 || s.rev != 0);
                    const bool contU =
                        (c.mode == 1 || c.mode == 3 || c.mode == 6
                         || c.mode == 7 || (c.mode == 2))
                        && s.ceilT > 0 && (revU ? -m : m) < 0.0
                        && (!revU ? (rcU > x1 && x <= x1 + pH)
                                  : (rcU < x0 && x >= x0 - pH));
                    const uint8_t dG = sp->slopeDir;
                    const bool leftFaceG =
                        (dG == 2 || dG == 3 || dG == 4 || dG == 6);
                    const bool goingLeftG =
                        ((double)K.dxF < 0.0 || s.rev != 0);
                    const bool probeG = leftFaceG ? (x < sp->cx - sp->hw)
                                                  : (goingLeftG
                                                     && sp->cx + sp->hw < x);
                    const bool wasOnSlopeG = s.onSlope || s.ceilT > 0;
                    const double overlapG =
                        ((double)c.y + pH) - (sp->cy - sp->hh);
                    const bool cubeGraceU =
                        (c.mode == 0 || c.mode == 5 || c.mode == 6)
                        && !probeG
                        && std::fabs(x - sp->cx) < sp->hw + pH
                        && (wasOnSlopeG || overlapG > 1.0)
                        && (((c.mode != s.mode) ? 0 : (int)s.modeT + 1)
                                < kFlipGraceTicks
                            || ((gdUpOf(c) != gdUpOf(s)) ? 0 : (int)s.flipT + 1)
                                < kFlipGraceTicks);
                    const bool freshEndU =
                        (c.mode == 1 || c.mode == 3 || c.mode == 6 || c.mode == 7)
                        && s.ceilT == 0 && (revU ? -m : m) < 0.0
                        && (!revU ? (rcU > x1 && x <= x1 + pH)
                                  : (rcU < x0 && x >= x0 - pH));
                    const bool winU = (rcU >= x0 && rcU <= x1) || contU
                                      || cubeGraceU || freshEndU;
                    const bool flightU = (c.mode == 1 || c.mode == 3
                                          || c.mode == 4 || c.mode == 7);
                    const bool seatedPrevU =
                        std::fabs((double)s.y - ceilLimAt(xPrev)) <= 0.001;
                    const bool upU = sp->slopeDir == 0 || sp->slopeDir == 3
                                  || sp->slopeDir == 6 || sp->slopeDir == 7;
                    const bool band22U = ((!upU) != revU) != (c.flip != 0);
                    const double tolU =
                        (flightU && input && !c.onSlope
                         && !ceilContNow && !band22U)
                            ? (seatedPrevU ? 2.0 : 1.0)
                            : 0.0;
                    // --slopeinset: the fresh-contact inset (see the ceiling press above).
                    const bool insetRefusedU = !wasOnSlopeG && overlapG <= 1.0;
                    const bool repeatDroppedU = wasOnSlopeG && (float)limU == s.y;
                    if (inYU && winU && !insetRefusedU && !repeatDroppedU
                        && (double)c.y > limU - tolU) {
                        if ((c.mode == 0 || c.mode == 5 || c.mode == 6)
                            && !cubeGraceU && (double)c.y > limU) {
                            if ((double)c.y - 2.0 > limU) DIE("slope/crush", sp);
                            else c.grounded = 0;
                            continue;
                        }
                        if (g_bvar9Kill && c.mode == 3 && s.onSlope && c.frame == 0
                            && sp->uid != (int)s.slopeUidNow) {
                            const Obj* ride = nullptr;
                            for (const Obj* q : *K.slopes)
                                if (q->uid == (int)s.slopeUidNow) { ride = q; break; }
                            if (ride && !slopeIsCeiling(ride->slopeDir)) {
                                DIE("slope/bvar9", sp);
                                continue;
                            }
                        }
                        const bool seatU = ((double)c.y <= limU);
                        YSET(c.y) = (float)limU;
                        if (!seatU && (double)c.vy > 0.0)
                            VYSET(c.vy) = 0;
                        if (cubeGraceU && (double)c.vy > -2.0) {
                            VYSET(c.vy) = -2.0f;           // 0x390276's min
                            CLAMP0O("slope/cubegrace", sp);
                        }
                        if (seatU) {
                            if ((double)c.vy > 0.0) VYSET(c.vy) = 0;
                            c.grounded = 0;
                        }
                        if ((revU ? -m : m) < 0.0 && !cubeGraceU) {
                            ceilPressedNow = true;
                            ceilPressM = m; ceilPressMode = c.mode; ceilPressUid = sp->uid;
                        }
                        const double dirU =
                            (m * (double)useDx >= 0.0) ? 1.0 : -1.0;
                        const double limUPrev = ceilLimAt(xPrev);
                        const bool freshU =
                            std::fabs((double)s.y - limUPrev) > 0.001;
                        const bool holdRelU = s.held && !input;
                        if (slopeNudgeMode(c.mode)) {
                            const float vN = slopeNudge(c.vy, c.flip != 0,
                                                        sp->slopeDir, c.mode,
                                                        input);
                            if (vN != c.vy) {
                                VYSET(c.vy) = vN;
                                CLAMP0O("slope/upceil", sp);
                            }
                        } else {
                            const double vClampU = (c.mode == 6) ? 0.0 : -2.0;
                            if (!(c.mode == 2)
                                && dirU < 0.0 && (freshU || holdRelU)
                                && (double)c.vy > vClampU) {
                                VYSET(c.vy) = (float)vClampU;
                                CLAMP0O("slope/upceil", sp);
                            }
                        }
                    }
                }
                if (c.flip && ceilRamp && !sp->slopeHazard && c.mode != 7) {
                    const double rc = x + (m > 0 ? -slopeXOffset(m, pH)
                                                 : slopeXOffset(m, pH));
                    const double lim = ceilLimAt(x);
                    const bool inY = ((double)c.y + pH >= sp->cy - sp->hh)
                                  && ((double)c.y - pH <= sp->cy + sp->hh);
                    if (g_slopeDbg)
                        std::printf("ceilpush t=%lld x=%.3f uid=%d y=%.3f "
                                    "vy=%.3f lim=%.3f rc=%.2f x0=%.1f x1=%.1f "
                                    "inY=%d sOn=%d sG=%d cOn=%d\n",
                                    (long long)K.t, x, sp->uid, (double)c.y,
                                    (double)c.vy, lim, rc, x0, x1,
                                    inY ? 1 : 0, (int)s.onSlope,
                                    (int)s.grounded, (int)c.onSlope);
                    const double headC = (double)c.y + pH * std::sqrt(1.0 + m * m)
                                       + 4.0;
                    const bool shipCont = c.mode == 1
                                          && !(input != 0 && !c.holdDead);
                    const bool contRide =
                        (c.mode == 0 || shipCont) && c.frame == 0
                        && s.onSlope && s.grounded && !impulsedThisTick
                        && std::fabs(x - sp->cx) <= 2.0 * sp->hw + pH
                        && std::fabs((double)c.y - sp->cy) <= 2.0 * sp->hh + pH
                        && !((double)(sp->cy - sp->hh) > headC);
                    double limSeat = lim;
                    if (contRide && c.mode == 1 && c.flip && m > 0.0
                        && (double)K.dxF > 0.0 && s.rev == 0) {
                        const double corner = sp->sy0;
                        bool prevSame = false;
                        for (const Obj* nb : *K.slopes) {
                            if (nb == sp || nb->slopeHazard) continue;
                            const double nx0 = nb->cx - nb->hw, nx1 = nb->cx + nb->hw;
                            if (nx1 <= nx0) continue;
                            const double nm = (nb->sy1 - nb->sy0) / (nx1 - nx0);
                            if (nm > 0.0 && std::abs(nx1 - x0) <= 0.5
                                && std::abs((double)nb->sy1 - corner) <= 0.5
                                && slopeIsCeiling(nb->slopeDir)) { prevSame = true; break; }
                        }
                        if (prevSame && lim > corner && (double)s.y < corner - 1e-6)
                            limSeat = corner;
                    }
                    const bool seatRepeats = contRide && (float)limSeat == s.y;
                    const bool shipC = c.mode == 1;
                    const bool graceRiderOnly = shipC || (g_ceilGraceCube && c.mode == 0);
                    const double exitGrace = (graceRiderOnly && !s.onSlope)
                                                 ? 0.0 : std::fabs((double)useDx);
                    const bool freshRectC = !shipC || s.onSlope || contRide
                        || ((double)yFreeBeforeSlope + pH >= sp->cy - sp->hh + 1.0
                            && (double)yFreeBeforeSlope - pH <= sp->cy + sp->hh - 1.0);
                    const bool reachC = rc >= x0
                        || (!s.onSlope && x + pH >= x0
                            && (double)yFreeBeforeSlope + pH >= sp->cy - sp->hh + 1.0
                            && (double)yFreeBeforeSlope - pH <= sp->cy + sp->hh - 1.0)
                        || contRide
                        || (s.onSlope && s.slopeUidNow == sp->uid
                            && !(c.onSlope && c.slopeUidNow != sp->uid));
                    if (inY && freshRectC && reachC && !seatRepeats
                        && (rc <= x1 + exitGrace || contRide)) {
                        const bool cubeRide =
                            (c.mode == 0 || (g_flipCeilBall && c.mode == 2)) && c.frame == 0
                            && s.onSlope && s.grounded && !impulsedThisTick;
                        const bool cubeLand =
                            c.mode == 0 && c.frame == 0
                            && (double)c.vy > 0.0 && !impulsedThisTick;
                        const bool hangSnap =
                            (c.mode == 1 || cubeRide || cubeLand || contRide)
                            && ((double)c.vy > 0.0
                                || (s.onSlope && s.grounded))
                            && ((((double)K.dxF < 0.0 || s.rev != 0) ? -m : m)
                                > 0.01)
                            && ((((double)K.dxF < 0.0 || s.rev != 0))
                                    ? (rc <= x1 - 0.8) : (rc >= x0 + 0.8))
                            && (double)c.y <= limSeat
                            && (contRide ? limSeat - (double)c.y < 4.0
                                         : limSeat - (double)c.y <= 1.0);
                        if (g_flipLandDbgLeft.load(std::memory_order_relaxed) > 0
                            && c.mode == 0 && c.frame == 0
                            && !s.dual && !cubeRide && (double)c.vy > 0.0
                            && ((double)c.y > lim || hangSnap)
                            && g_flipLandDbgLeft.fetch_sub(1) > 0)
                            std::printf("flipcl t=%lld x=%.3f uid=%d y=%.3f lim=%.3f "
                                        "vy=%.3f snap=%d sOn=%d sG=%d\n",
                                        (long long)K.t, x, sp->uid, (double)c.y, lim,
                                        (double)c.vy, (double)c.y > lim ? 0 : 1,
                                        (int)s.onSlope, (int)s.grounded);
                        if ((double)c.y > limSeat || hangSnap) {
                            YSET(c.y) = (float)limSeat;
                            const bool contH =
                                s.onSlope && c.slopeM == (float)m;
                            bool flyHangRide = false;
                            const bool nudgeHere =
                                slopeNudgeMode(c.mode);
                            if (nudgeHere
                                && slopeSkipsHitGround(c.flip != 0,
                                                       sp->slopeDir, c.mode,
                                                       input)) {
                                VYSET(c.vy) = slopeNudge(c.vy, c.flip != 0,
                                                  sp->slopeDir, c.mode, input);
                                flyHangRide = true;
                            } else if (nudgeHere && contH) {
                                VYSET(c.vy) = 0.f;       // V1, hitGround @0x39c164
                                flyHangRide = true;
                            } else if (nudgeHere) {
                                if ((double)c.vy * (c.flip ? -1.0 : 1.0)
                                    <= kSlopeLandV) {
                                    VYSET(c.vy) = 0.f;
                                    flyHangRide = true;
                                }
                            } else if (c.mode == 1 && input) {
                                const double dirH =
                                    (m * (double)useDx >= 0.0) ? 1.0 : -1.0;
                                if (!(contH && s.vy != 0.0f))
                                    VYSET(c.vy) = (float)(2.0 * dirH);
                                flyHangRide = true;
                            } else if ((c.mode == 1 || c.mode == 3)
                                       && !input && contH) {
                                VYSET(c.vy) = 0.f;
                                flyHangRide = true;
                            }
                            if (!flyHangRide
                                && ((c.mode == 0 && s.dual)
                                    || (c.mode == 0 && c.frame == 0)
                                    || ((c.mode == 2 || c.mode == 1
                                         || c.mode == 3)
                                        && c.frame == 0))
                                && (double)c.vy > 0.0) VYSET(c.vy) = 0.f;
                            if ((cubeRide || contRide) && (double)c.vy >= 0.0)
                                VYSET(c.vy) = 0.f;
                            if (!impulsedThisTick) {
                                const bool awayFastH =
                                    (double)c.vy * (c.flip ? -1.0 : 1.0) > kSlopeLandV;
                                if (!awayFastH) {
                                c.grounded = 1;
                                c.rideLanded = 1;
                                }
                                c.onSlope = 1;
                                c.slopeM = (float)m;
                                // the ramp the ride began on (State::slopeUid0)
                                c.slopeUid0 =
                                    s.onSlope ? s.slopeUid0 : sp->uid;
                                c.slopeUidNow = sp->uid;
                                suSeatUid = sp->uid;   // --slopeunchanged
                            }
                        }
                        continue;
                    }
                }
                if (!c.flip && ceilRamp) {
                    const double rc = x + (m > 0 ? -slopeXOffset(m, pH)
                                                 : slopeXOffset(m, pH));
                    const bool inSpan = (rc >= x0 && rc <= x1 + 1.5);
                    auto cornerContinued = [&](void) {
                        const double edge = (m < 0) ? x1 : x0;
                        const double edgeY = (m < 0) ? sp->sy1 : sp->sy0;
                        for (const Obj* nb : *K.slopes) {
                            if (nb == sp || nb->slopeHazard) continue;
                            const double nx0 = nb->cx - nb->hw;
                            const double nx1 = nb->cx + nb->hw;
                            if (nx1 <= nx0) continue;
                            const double nm = (nb->sy1 - nb->sy0) / (nx1 - nx0);
                            if (nm == 0.0) continue;
                            if (!slopeIsCeiling(nb->slopeDir)) continue;
                            const bool joins = (m < 0)
                                ? (std::abs(nx0 - edge) <= 0.5
                                   && std::abs((double)nb->sy0 - edgeY) <= 0.5)
                                : (std::abs(nx1 - edge) <= 0.5
                                   && std::abs((double)nb->sy1 - edgeY) <= 0.5);
                            if (joins && (nm > 0.0) == (m > 0.0)) return true;
                        }
                        if (K.near) {
                            for (const Obj* nb : *K.near) {
                                if (nb->type != 0) continue;
                                const double face = nb->cy - nb->hh;
                                if (std::fabs(face - edgeY) > 0.5) continue;
                                const bool touches = (m < 0)
                                    ? (std::fabs((nb->cx - nb->hw) - edge) <= 0.5)
                                    : (std::fabs((nb->cx + nb->hw) - edge) <= 0.5);
                                if (touches) return true;
                            }
                        }
                        return false;
                    };
                    const bool wallSideC =
                        (sp->slopeDir == 2 || sp->slopeDir == 3 || sp->slopeDir == 4
                         || sp->slopeDir == 6)
                            ? (double)x < x0
                            : (((double)K.dxF < 0.0 || s.rev != 0) && (double)x > x1);
                    const bool cornerReg = (c.mode == 1) && !inSpan && !wallSideC
                        && (m < 0 ? (rc > x1 && (double)x - pH <= x1 + 0.5)
                                  : (rc < x0
                                     && (double)x + pH >= x0 - 0.5))
                        && !cornerContinued();
                    const bool ceilOk = (c.mode == 1)
                        ? (inSpan || cornerReg) : smp.ok;
                    const bool rcInSpan = (rc >= x0 && rc <= x1);
                    const double lim = ceilLimAt(x);
                    const double limPrev = ceilLimAt(xPrev);
                    const bool pressGate = cornerReg
                        ? ((double)s.y >= limPrev - 0.05
                           && (double)s.y <= limPrev + 2.0)
                        : ((double)s.y <= limPrev + 0.05);
                    const bool insetRefusedC =
                        !(s.onSlope || s.ceilT > 0)
                        && ((double)c.y + pH <= sp->cy - sp->hh + 1.0);
                    // --upceilrepeat: the seat-repeat drop (see the upceil clamp below).
                    const bool repeatDroppedC =
                        (s.onSlope || s.ceilT > 0) && (float)lim == s.y;
                    if (ceilOk && pressGate && !insetRefusedC && !repeatDroppedC) {
                        if ((double)c.y > lim
                            && (c.mode == 1 || rcInSpan)) {   // r42: see the rcInSpan note
                            YSET(c.y) = (float)lim;
                            if (m * (double)useDx < 0.0) {
                                ceilPressedNow = true;
                                ceilPressM = m; ceilPressMode = c.mode; ceilPressUid = sp->uid;
                            }
                            if ((double)c.vy > 0.0) {
                                if (slopeNudgeMode(c.mode)) {
                                    VYSET(c.vy) = slopeNudge(0.f, c.flip != 0,
                                                      sp->slopeDir, c.mode,
                                                      input);
                                } else {
                                    const double dir =
                                        (m * (double)useDx >= 0.0) ? 1.0 : -1.0;
                                    VYSET(c.vy) = (c.mode == 1 && dir < 0.0)
                                               ? (float)(2.0 * dir) : 0.f;
                                }
                                CLAMP0("slope/ceillim");
                            } else if (slopeNudgeMode(c.mode)) {
                                const float vN = slopeNudge(c.vy, c.flip != 0, sp->slopeDir,
                                                            c.mode, input);
                                if (vN != c.vy) {
                                    VYSET(c.vy) = vN;
                                    CLAMP0("slope/ceillim");
                                }
                            }
                        } else if (cornerReg && !ceilPressedNow
                                              && (double)s.vy >= -2.05
                                              && (double)s.vy <= 0.05) {
                            VYSET(c.vy) = -2.762f;
                        }
                        continue;
                    }
                }
                if (ridesTop && !c.flip && c.mode == 0 && s.grounded
                    && !s.onSlope) {
                    const bool revC = ((double)K.dxF < 0.0 || s.rev != 0);
                    const double mTravC = revC ? -m : m;
                    const double cxE = revC ? x1 : x0;
                    const double cyE = sp->sy0 + m * (cxE - x0);
                    const bool edgeIn = revC
                        ? (x > cxE && x - pH <= cxE + kContactEps)
                        : (x < cxE && x + pH >= cxE - kContactEps);
                    if (mTravC < -0.01 && edgeIn) {
                        const double d = (cyE + pH) - (double)c.y;
                        if (d > 0.0005 && d <= 1.0) {
                            if (g_slopeDbg)
                                std::printf("slopedbg t=%lld x=%.2f uid=%d "
                                            "cornerstand y=%.3f (d=%.3f)\n",
                                            (long long)K.t, x, sp->uid,
                                            cyE + pH, d);
                            YSET(c.y) = (float)(cyE + pH);
                            VYSET(c.vy) = 0; c.grounded = 1;
                            continue;
                        }
                    }
                }
                if (g_rampEndClick && ceilRamp && !ridesTop && s.onSlope && c.flip) {
                    if (smp.ok) rampOkHere = true;
                    else if (x >= x0 && x <= x1) rampEndHere = true;
                }
                if (g_slopeDbg && !smp.ok)
                    std::printf("slopewin t=%lld x=%.2f uid=%d m=%.3f "
                                "x0=%.1f x1=%.1f xr=%.2f ridesTop=%d ceil=%d "
                                "REJECTED-BY-WINDOW\n",
                                (long long)K.t, x, sp->uid, m, x0, x1, smp.xr,
                                ridesTop ? 1 : 0, ceilRamp ? 1 : 0);
                const double sgnPO =
                    ((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0;
                const double xHiPO = (m > 0) ? x1 : x0;
                bool okHere = smp.ok;
                bool seamHere = false;   // the concave-seam switch below took this ramp
                if (!okHere && s.onSlope && ridesTop && !ceilRamp
                    && !sp->slopeHazard && c.frame == 0
                    && (int)s.slopeUidNow != sp->uid) {
                    const Obj* cur = nullptr;
                    for (const Obj* q : *K.slopes)
                        if (q->uid == (int)s.slopeUidNow) { cur = q; break; }
                    if (cur && !slopeIsCeiling(cur->slopeDir) && !cur->slopeHazard) {
                        const bool revS = ((double)K.dxF < 0.0 || s.rev != 0);
                        const double cx0 = cur->cx - cur->hw, cx1 = cur->cx + cur->hw;
                        const double xs = revS ? cx0 : cx1;           // the seam's x
                        const double xb = revS ? x1 : x0;             // the next ramp's near end
                        const double yCur = cur->sy0 + (cur->sy1 - cur->sy0) * (xs - cx0)
                                                           / (cx1 - cx0);
                        const double yNext = sp->sy0 + m * (xb - x0);
                        if (std::fabs(xs - xb) < 0.5 && std::fabs(yCur - yNext) < 0.5
                            && slopeSeatTarget(sp, x, pH, false, 0.0)
                                   > slopeSeatTarget(cur, x, pH, false, 0.0)) {
                            okHere = true;
                            seamHere = true;
                            if (g_slopeDbg)
                                std::printf("slopeseam t=%lld p%d from uid=%d (seat %.4f) to "
                                            "uid=%d (seat %.4f) y=%.4f\n", (long long)K.t,
                                            g_halfNow + 1, cur->uid,
                                            slopeSeatTarget(cur, x, pH, false, 0.0), sp->uid,
                                            slopeSeatTarget(sp, x, pH, false, 0.0), (double)c.y);
                        }
                    }
                }
                if (g_slopeDbg)
                    std::printf("pogate t=%lld uid=%d flip=%d y=%.6f notok=%d ridesTop=%d "
                                "notceil=%d notonslope=%d past=%d inx0=%d inx1=%d "
                                "x=%.3f xHiPO=%.1f sgnPO=%.0f x0=%.1f x1=%.1f pH=%.2f\n",
                                (long long)K.t, sp->uid, c.flip ? 1 : 0, (double)c.y,
                                !okHere ? 1 : 0, ridesTop ? 1 : 0,
                                !ceilRamp ? 1 : 0, !s.onSlope ? 1 : 0,
                                ((x - xHiPO) * sgnPO > 0.0) ? 1 : 0,
                                (x + pH > x0) ? 1 : 0, (x - pH < x1) ? 1 : 0,
                                x, xHiPO, sgnPO, x0, x1, pH);
                if (g_ceilPush && !okHere && ceilRamp && !s.onSlope && std::fabs(m) <= 1.0) {
                    const double lineCP = sp->sy0 + m * (x - x0);
                    const double seatCP = lineCP - pH * std::sqrt(1.0 + m * m);
                    const bool boxX = (x + pH > x0) && (x - pH < x1);
                    const bool boxY = ((double)c.y + pH > sp->cy - sp->hh)
                                      && ((double)c.y - pH < sp->cy + sp->hh);
                    if (boxX && boxY && (double)c.y > seatCP && (double)c.y < lineCP) {
                        const double yWas = (double)c.y, vyWas = (double)c.vy;
                        YSET(c.y) = (float)seatCP;
                        if (c.mode == 7 && (double)c.vy > -2.0) VYSET(c.vy) = -2.0f;
                        if (g_slopeDbg)
                            std::printf("ceilpush: t=%lld p%d uid=%d mode=%d m=%.3f "
                                        "x=%.3f y=%.4f->%.4f vy=%.3f->%.3f "
                                        "seatTarget=%.4f\n",
                                        (long long)K.t, g_halfNow + 1, sp->uid,
                                        (int)c.mode, m, x, yWas, seatCP, vyWas,
                                        (double)c.vy,
                                        slopeSeatTarget(sp, x, pH, true, seatOff));
                    }
                }
                bool apexReacq = false;
                if (g_apexReacq && !okHere && s.onSlope && ridesTop && !ceilRamp
                    && !sp->slopeHazard && c.frame == 0 && (int)s.slopeUidNow != sp->uid
                    && (x - xHiPO) * sgnPO > 0.0) {
                    const Obj* cur = nullptr;
                    for (const Obj* q : *K.slopes)
                        if (q->uid == (int)s.slopeUidNow) { cur = q; break; }
                    if (cur && !slopeIsCeiling(cur->slopeDir) && !cur->slopeHazard) {
                        const double cx0 = cur->cx - cur->hw, cx1 = cur->cx + cur->hw;
                        const double xn = sgnPO < 0.0 ? cx1 : cx0;   // the ridden ramp's near end
                        const double yn = sgnPO < 0.0 ? cur->sy1 : cur->sy0;
                        const double yHi = sp->sy0 + m * (xHiPO - x0);
                        apexReacq = std::fabs(xn - xHiPO) < 0.5 && std::fabs(yn - yHi) < 0.5;
                    }
                }
                if (!okHere && (!s.onSlope || apexReacq)) {
                    const double oMinSL = sp->cy - sp->hh;
                    const double oMaxSL = sp->cy + sp->hh;
                    const double lineSL = sp->sy0 + m * (x - x0);
                    const bool preVeto = ceilRamp ? ((double)c.y >= oMaxSL)
                                                  : ((double)c.y <= oMinSL + 1.0);
                    const uint8_t dSL = sp->slopeDir;
                    const bool leftFace =
                        (dSL == 2 || dSL == 3 || dSL == 4 || dSL == 6);
                    const bool goingLeftSL =
                        ((double)K.dxF < 0.0 || s.rev != 0);
                    const bool vertProbe =
                        leftFace ? (x < sp->cx - sp->hw)
                                 : (goingLeftSL && sp->cx + sp->hw < x);
                    const double lineSide = sp->sy0 + m * (std::clamp(x, x0, x1) - x0);
                    const bool freeSide =
                        ceilRamp ? ((double)c.y < lineSide)
                                 : ((double)c.y > lineSide);
                    const bool contSL = (lawWasOn || apexReacq) && !ceilRamp;
                    bool acqSL = slopeWouldAcquire(sp, x, (double)c.y, pH, contSL);
                    if (contSL && !acqSL && seatOff == 0.0
                        && slopeBVar22(false, sp->slopeDir) && !(a1cNow)) {
                        // the helper's reach test for a continuing contact, then the band
                        const double cosB = std::cos(std::atan(std::fabs(m)));
                        const bool reachB = x + pH >= x0 && x - pH <= x1
                            && !((double)c.y - pH / cosB - 4.0 > oMaxSL);
                        if (reachB && slopeSeatTarget(sp, x, pH, false, 0.0) + 4.0
                                          > (double)c.y)
                            acqSL = true;
                    }
                    if (!contSL && !acqSL && !ceilRamp && c.flip == 0 && seatOff == 0.0
                        && !c.onSlope && (slopeBVar22(false, sp->slopeDir) != goingLeftSL)
                        && c.mode == 1 && !(input != 0 && !c.holdDead)) {
                        const bool reachF = x + pH >= x0 && x - pH <= x1
                            && (double)c.y + pH >= oMinSL + 1.0
                            && (double)c.y - pH <= oMaxSL - 1.0;
                        if (reachF && slopeSeatTarget(sp, x, pH, false, 0.0) + 1.0
                                          > (double)c.y)
                            acqSL = true;
                    }
                    const bool dropSL =
                        contSL && !preVeto && !vertProbe && freeSide && acqSL
                        && (float)slopeSeatTarget(sp, x, pH, false, seatOff) == s.y;
                    if (dropSL) {
                        lawRepeatDrop = true;
                        lawDropM = m;
                    }
                    if (!preVeto && !vertProbe && freeSide && acqSL && !dropSL) {
                        const double tgtSL =
                            slopeSeatTarget(sp, x, pH, ceilRamp, seatOff);
                        if (g_slopeDbg)
                            std::printf("slopelaw: t=%lld p%d uid=%d mode=%d "
                                        "m=%.3f x=%.3f y=%.4f->%.4f line=%.4f "
                                        "isTop=%d\n",
                                        (long long)K.t, g_halfNow + 1, sp->uid,
                                        (int)c.mode, m, x, (double)c.y, tgtSL,
                                        lineSL, ceilRamp ? 1 : 0);
                        lawSeat = true;
                        lawTop = tgtSL;
                        lawUid = sp->uid;
                        lawUnder = slopeUnderside(c.flip != 0, sp->slopeDir);
                        lawM = m;
                    }
                }
                if (!okHere && ridesTop && !ceilRamp && !s.onSlope
                    && (x - xHiPO) * sgnPO > 0.0
                    && x + pH > x0 && x - pH < x1) {
                    const double lineHere =
                        sp->sy0 + m * (std::min(std::max(x, x0), x1) - x0);
                    const double yPO = landedThisTick ? (double)preLandY
                                                      : (double)c.y;
                    const double pLo = std::max(yPO - pH,
                                                (double)(sp->cy - sp->hh));
                    const double pHi = std::min(yPO + pH, lineHere);
                    if (pLo < pHi - 1.0) {
                        okHere = true;
                        double best = -1e18;
                        for (const Obj* nb : *K.slopes) {
                            if (nb->slopeHazard) continue;
                            const bool nceil = (c.mode == 1 || c.mode == 7
                                                || c.mode == 0)
                                ? slopeIsCeiling(nb->slopeDir)
                                : (nb->slopeDir == 1 || nb->slopeDir == 3);
                            if (nceil) continue;
                            const double a0 = nb->cx - nb->hw;
                            const double a1 = nb->cx + nb->hw;
                            if (a1 <= a0) continue;
                            if (x + pH <= a0 || x - pH >= a1) continue;
                            const double nm = (nb->sy1 - nb->sy0) / (a1 - a0);
                            if (nm == 0.0) continue;
                            const double sfc = nb->sy0
                                + nm * (std::min(std::max(x, a0), a1) - a0);
                            // a face sticking out above the box is not a ride surface
                            if (sfc > (double)c.y + pH) continue;
                            if (sfc > best) best = sfc;
                        }
                        if (best > -1e17) {
                            pushOutSeat = true;
                            pushOutTop = best + pH;
                        }
                        if (g_slopeDbg)
                            std::printf("slopepush t=%lld x=%.2f uid=%d m=%.3f "
                                        "line=%.3f y=%.3f ridge=%.3f\n",
                                        (long long)K.t, x, sp->uid, m,
                                        lineHere, (double)c.y, pushOutTop);
                    }
                }
                if (!okHere) continue;
                const double hangOff =
                    (c.mode == 2 && gs < 0.0)
                    ? (pH + std::fabs(m) * slopeXOffset(m, pH))
                    : pH;
                double top;
                const char* topSrc = "seatTarget";
                top = slopeSeatTarget(sp, x, pH, gs < 0.0, seatOff);
                if (pushOutSeat && !c.flip) {
                    const double beforePush = top;
                    top = std::max(top, pushOutTop);
                    if (top != beforePush) topSrc = "pushOutSeat";
                }
                if (c.onSlope && c.frame == 0) {
                    const bool upK = sp->slopeDir == 0 || sp->slopeDir == 3
                                  || sp->slopeDir == 6 || sp->slopeDir == 7;
                    const bool leftK = (double)K.dxF < 0.0 || s.rev != 0;
                    const bool fallsAway = ((!upK) != leftK) != (c.flip != 0);
                    const float topF = (float)top;
                    if (fallsAway && !(gs > 0.0 ? topF > c.y : topF < c.y)) continue;
                }
                {
                    const double sgnT =
                        ((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0;
                    const double mTravOld = (double)s.slopeM * sgnT;
                    const double mTravNew = m * sgnT;
                    if (s.onSlope && ridesTop && !c.flip
                        && mTravOld > 0.01 && mTravNew > 0.0
                        && mTravNew < mTravOld - 0.01) {
                        if (g_slopeDbg)
                            std::printf("rampwin: t=%lld uid=%d id=%d "
                                        "dropped=convex-seam mTravOld=%.3f "
                                        "mTravNew=%.3f\n",
                                        (long long)K.t, sp ? sp->uid : -1,
                                        sp ? sp->id : -1, mTravOld, mTravNew);
                        continue;
                    }
                    if (g_seatSpeed && s.onSlope && s.seatSpd && ridesTop && !c.flip
                        && s.seatSpd != speedCodeForDx(useDx)) {
                        const double rOld = mTravOld * (double)dxForSpeedCode(s.seatSpd);
                        const double rNew = mTravNew * std::fabs((double)useDx);
                        if (rOld > 0.0 && rNew > 0.0 && rNew < rOld) {
                            if (g_slopeDbg)
                                std::printf("rampwin: t=%lld uid=%d id=%d "
                                            "dropped=seat-speed rOld=%.4f rNew=%.4f\n",
                                            (long long)K.t, sp ? sp->uid : -1,
                                            sp ? sp->id : -1, rOld, rNew);
                            continue;
                        }
                    }
                }
                if (!ridesTop && s.onSlope && s.slopeUidNow >= 0
                    && s.slopeUidNow != sp->uid
                    && std::fabs((double)s.slopeM - m) < 0.01) {
                    const double xoffH = slopeXOffset(m, pH);
                    const double rcH2 = x + ((m > 0) ? -xoffH : xoffH);
                    bool prevHolds = false;
                    for (const Obj* pv : *K.slopes) {
                        if (pv->uid != s.slopeUidNow) continue;
                        const double px0 = pv->cx - pv->hw;
                        const double px1 = pv->cx + pv->hw;
                        const bool adj = std::fabs(px1 - x0) <= 0.5
                                      || std::fabs(x1 - px0) <= 0.5;
                        if (adj && rcH2 >= px0 && rcH2 <= px1)
                            prevHolds = true;
                        break;
                    }
                    if (prevHolds) {
                        if (g_slopeDbg)
                            std::printf("rampwin: t=%lld uid=%d id=%d "
                                        "dropped=prev-holds slopeUidNow=%d\n",
                                        (long long)K.t, sp ? sp->uid : -1,
                                        sp ? sp->id : -1, (int)s.slopeUidNow);
                        continue;
                    }
                }
                if (g_slopeDbg) {
                    const double tol = 3.0 * std::fabs((double)useDx);
                    const double gap = std::fabs(top - (double)s.y);
                    std::printf("rampwin: t=%lld uid=%d id=%d inside=%d "
                                "top=%.4f y=%.4f gap=%.4f tol=%.4f useDx=%.4f "
                                "pH=%.2f m=%.3f gs=%.1f seatOff=%.4f seat=%s "
                                "topSrc=%s xr=%.4f hangOff=%.4f sy0=%.1f "
                                "x0=%.1f verdict=%d\n",
                                (long long)K.t, sp ? sp->uid : -1,
                                sp ? sp->id : -1, smp.inside ? 1 : 0,
                                top, (double)s.y, gap, tol, (double)useDx,
                                pH, m, gs, seatOff,
                                "secant",
                                topSrc, smp.xr, hangOff, (double)sp->sy0, x0,
                                (smp.inside && gap <= tol) ? 1 : 0);
                }
                bool hangRanOff = false;
                if (c.mode == 1 && !ridesTop) {
                    const bool revW = ((double)K.dxF < 0.0 || s.rev != 0);
                    const double xoffW = slopeXOffset(m, pH);
                    hangRanOff = (!revW && m < 0 && x + xoffW > x1)
                              || (revW && m > 0 && x - xoffW < x0);
                }
                if (smp.inside && std::fabs(top - (double)s.y)
                                      <= 3.0 * std::fabs((double)useDx)
                    && !hangRanOff)
                    rampWindowHere = true;
                const double prevTop = slopeSeatTarget(sp, xPrev, pH, gs < 0.0, seatOff);
                const double sLo = std::max((double)c.y - pH, sp->cy - sp->hh);
                const double sHi = std::min((double)c.y + pH,
                                            sp->sy0 + m * (std::min(std::max(x, x0), x1) - x0));
                const bool insideSolid = ridesTop && !ceilRamp
                    && (x + pH > x0 && x - pH < x1) && (sLo < sHi - 0.001);
                if (g_slopeDbg)
                    std::printf("slopecand t=%lld x=%.2f uid=%d m=%.3f "
                                "sy=%.3f prevTop=%.3f top=%.3f y=%.3f sy0=%.3f "
                                "gs=%.0f inside=%d ridesTop=%d ceil=%d "
                                "reject=%d\n",
                                (long long)K.t, x, sp->uid, m, (double)s.y,
                                prevTop, top, (double)c.y, sp->sy0, gs,
                                insideSolid ? 1 : 0, ridesTop ? 1 : 0,
                                ceilRamp ? 1 : 0,
                                (!insideSolid
                                 && ((double)s.y - prevTop) * gs < -kLandTol)
                                    ? 1 : 0);
                if (!insideSolid && ((double)s.y - prevTop) * gs < -kLandTol) {
                    if (c.mode == 0 && okHere && !s.onSlope) {
                        const double oMinFB = sp->cy - sp->hh;
                        const double oMaxFB = sp->cy + sp->hh;
                        const double lineFB = sp->sy0 + m * (x - x0);
                        const bool preVetoFB = ceilRamp
                            ? ((double)c.y >= oMaxFB)
                            : ((double)c.y <= oMinFB + 1.0);
                        const uint8_t dFB = sp->slopeDir;
                        const bool leftFaceFB =
                            (dFB == 2 || dFB == 3 || dFB == 4 || dFB == 6);
                        const bool goingLeftFB =
                            ((double)K.dxF < 0.0 || s.rev != 0);
                        const bool vertProbeFB =
                            leftFaceFB ? (x < sp->cx - sp->hw)
                                       : (goingLeftFB && sp->cx + sp->hw < x);
                        const bool freeSideFB =
                            ceilRamp ? ((double)c.y < lineFB)
                                     : ((double)c.y > lineFB);
                        if (!preVetoFB && !vertProbeFB && freeSideFB
                            && slopeWouldAcquire(sp, x, (double)c.y, pH, false)) {
                            lawSeat = true;
                            lawTop = slopeSeatTarget(sp, x, pH, ceilRamp, seatOff);
                            lawUid = sp->uid;
                            lawUnder = slopeUnderside(c.flip != 0, sp->slopeDir);
                            lawM = m;
                            if (g_slopeDbg)
                                std::printf("slopelaw: t=%lld p%d uid=%d (from "
                                            "below) y=%.4f->%.4f\n",
                                            (long long)K.t, g_halfNow + 1,
                                            sp->uid, (double)c.y, lawTop);
                        }
                    }
                    continue;
                }
                const bool ufoRising =
                    (c.mode == 3) && (double)c.vy * gs > 0.0;
                const bool uphillU = sp->slopeDir == 0 || sp->slopeDir == 3
                                  || sp->slopeDir == 6 || sp->slopeDir == 7;
                const bool goingLeftU = (double)K.dxF < 0.0 || s.rev != 0;
                const bool bandU = ((!uphillU) != goingLeftU) != (c.flip != 0);
                const double ufoAllow = (c.frame == 0 && !bandU)
                                            ? 0.001
                                            : (s.onSlope ? 2.0 : 1.0);
                const bool shipBand = c.mode == 1 && c.frame == 0 && !s.onSlope
                    && c.flip == 0 && bandU && seatOff == 0.0
                    && !(input != 0 && !c.holdDead);
                const double landAllow = waveRide ? 0.0
                    : (ridesTop && (c.mode == 1 || c.mode == 3) && !ufoRising)
                        ? (c.mode == 1 ? (shipBand ? 1.0 : 0.001) : ufoAllow)
                        : 0.001;
                const bool shipLeaving =
                    (c.mode == 1 || waveRide) && (double)c.vy * gs > 0.0
                    && ((double)c.y - top) * gs > landAllow;
                bool apexFar = false;
                if (g_apexStick && stickToSlope && c.mode == 1 && c.flip && c.frame == 0
                    && ridesTop && !ceilRampSide && (int)s.slopeUidNow != sp->uid) {
                    const double sgnA = ((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0;
                    for (const Obj* q : *K.slopes) {
                        if (q->uid != (int)s.slopeUidNow) continue;
                        if (slopeIsCeiling(q->slopeDir) || q->hw <= 0.0) break;
                        const double qx0 = q->cx - q->hw, qx1 = q->cx + q->hw;
                        const double qm = (q->sy1 - q->sy0) / (qx1 - qx0);
                        const double qHi = (qm > 0.0) == (sgnA > 0.0) ? (sgnA > 0.0 ? qx1 : qx0) : -1e18;
                        const double xn = sgnA > 0.0 ? x0 : x1;
                        const double yn = sgnA > 0.0 ? sp->sy0 : sp->sy1;
                        const double yq = sgnA > 0.0 ? q->sy1 : q->sy0;
                        apexFar = qHi > -1e17 && std::fabs(xn - qHi) < 0.5 && std::fabs(yn - yq) < 0.5
                                  && (x - qHi) * sgnA > 0.0;
                        break;
                    }
                }
                const bool stickHere =
                    stickToSlope && !(bandFloorHeld)
                    && std::fabs(top - (double)s.y) <= (apexFar ? 1.0 : kStickGap)
                    && !(ufoRising && m > 0.0
                         && ((double)c.y - top) * gs > landAllow)
                    && !shipLeaving;
                if (slopeTookInside && !smp.inside && !seamHere) continue;
                const double rRaw = x + (m > 0 ? slopeXOffset(m, pH)
                                               : -slopeXOffset(m, pH));
                const bool revT = ((double)K.dxF < 0.0 || s.rev != 0);
                const bool seatTolOk =
                    ridesTop && !c.flip && (c.mode == 0 || c.mode == 2)
                    && !s.grounded && (double)c.vy * gs <= 0.0
                    && (revT ? -m : m) < -0.01
                    && (revT ? (rRaw <= x1 - 0.8) : (rRaw >= x0 + 0.8))
                    && smp.inside
                    && ((double)c.y - top) * gs <= 1.0;
                const bool seatFromPreLand =
                    landedThisTick && !stepLandedThisTick && ridesTop && !c.flip
                    && (c.mode == 0 || c.mode == 6 || c.mode == 3) && !s.dual
                    && ((double)preLandY - top) * gs <= landAllow;
                if (s.grounded && !s.onSlope && !insideSolid
                    && ((double)c.y - top) * gs > 0.001) {
                    bool flatSup = false;
                    const double footG = (double)c.y - gs * pH;
                    for (const Obj* fo : *K.near) {
                        if (fo->type != 0) continue;
                        if (std::fabs(x - fo->cx) > fo->hw + pH + kContactEps)
                            continue;
                        if (lawWasOn
                            && vetoOf(K, fo, x, (double)c.y, pH, pH, gs > 0.0, xPrev,
                                      (double)s.y, c.slopeUidNow, true))
                            continue;
                        const double ff = (gs > 0.0) ? (fo->cy + fo->hh)
                                                     : (fo->cy - fo->hh);
                        if (std::fabs(footG - ff) <= 0.6) {
                            flatSup = true;
                            break;
                        }
                    }
                    if (flatSup) continue;
                }
                bool freshRectOk = true;
                if (!s.onSlope && !lawWasOn) {
                    const double ry0 = sp->cy - sp->hh;
                    const double ry1 = sp->cy + sp->hh;
                    const double py = (double)yFreeBeforeSlope;
                    // closed-interval overlap, as cocos CCRect::intersectsRect
                    freshRectOk = (x + pH >= x0) && (x - pH <= x1)
                               && (py + pH >= ry0 + 1.0)
                               && (py - pH <= ry1 - 1.0);
                    if (!freshRectOk && g_slopeDbg)
                        std::printf("sloperect t=%lld x=%.2f uid=%d yfree=%.3f "
                                    "prect=[%.3f,%.3f] inset=[%.1f,%.1f] "
                                    "alt=%d NO-FRESH-CONTACT\n",
                                    (long long)K.t, x, sp->uid, py,
                                    py - pH, py + pH, ry0 + 1.0, ry1 - 1.0,
                                    (seatTolOk || seatFromPreLand || stickHere
                                     || insideSolid) ? 1 : 0);
                }
                const bool lawBandOk =
                    lawWasOn && ridesTop && !c.flip && seatOff == 0.0
                    && slopeBVar22(false, sp->slopeDir) && !(a1cNow)
                    && ((double)c.y - top) * gs <= 4.0;
                const bool lawRideDrop = lawWasOn && ridesTop && !c.flip
                                         && (float)top == s.y;
                if (lawRideDrop && freshRectOk
                    && (((double)c.y - top) * gs <= landAllow || lawBandOk || seatTolOk
                        || seatFromPreLand || stickHere || insideSolid)) {
                    lawRepeatDrop = true;
                    lawDropM = m;
                }
                if (c.mode == 2 && impulsedThisTick && s.onSlope && !s.flip
                    && ridesTop && sp->uid == s.slopeUidNow && freshRectOk && c.frame == 0
                    && m * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0) < -0.01
                    && (double)c.y > top && (double)c.y - top <= kStickGap) {
                    if (g_slopeDbg)
                        std::printf("balltapstep: t=%lld uid=%d y %.4f -> %.4f\n",
                                    (long long)K.t, sp->uid, (double)c.y, top);
                    YSET(c.y) = (float)top;
                    continue;
                }
                if (freshRectOk && !lawRideDrop
                    && (((double)c.y - top) * gs <= landAllow || lawBandOk || seatTolOk
                        || seatFromPreLand || stickHere || insideSolid)) {
                    const double rTopHere = sp->cy + sp->hh;
                    bool rTopIsSeam = false;
                    {
                        const bool revT = ((double)K.dxF < 0.0 || s.rev != 0);
                        const double entryEdge = revT ? x1 : x0;
                        for (const Obj* nb : *K.slopes) {
                            if (nb == sp || nb->slopeHazard) continue;
                            const double nx0 = nb->cx - nb->hw;
                            const double nx1 = nb->cx + nb->hw;
                            if (nx1 <= nx0) continue;
                            const double nedge = revT ? nx0 : nx1;
                            const double nlo = revT ? (double)nb->sy0
                                                    : (double)nb->sy1;
                            if (std::fabs(nedge - entryEdge) <= 0.5
                                && std::fabs(nlo - rTopHere) <= 0.5) {
                                rTopIsSeam = true;
                                break;
                            }
                        }
                    }
                    if (ridesTop && !c.flip && (c.mode == 0 || c.mode == 2)
                        && rTopIsSeam && s.onSlope && s.slopeUid0 >= 0
                        && (c.mode == 2 || sp->uid != s.slopeUid0)) {
                        const double rTop = rTopHere;
                        if ((double)s.y > rTop + 0.0005 && top < rTop - 1e-9) {
                            if (g_slopeDbg)
                                std::printf("slopedbg t=%lld x=%.2f uid=%d "
                                            "recttop=%.3f (top=%.3f)\n",
                                            (long long)K.t, x, sp->uid,
                                            rTop, top);
                            YSET(c.y) = (float)rTop;
                            VYSET(c.vy) = 0; c.grounded = 1;
                            c.rideLanded = 1;   // as above: grounded is the landing
                            c.onSlope = 1; c.slopeM = (float)m;
                            c.slopeUid0 = s.onSlope ? s.slopeUid0 : sp->uid;
                    c.slopeUidNow = sp->uid;
                            suSeatUid = sp->uid;   // --slopeunchanged
                            continue;
                        }
                    }
                    if (flipForRide && ridesTop && !ceilRampSide
                        && c.mode == 0 && !s.onSlope && c.frame == 0
                        && !((((c.mode != s.mode) ? 0 : (int)s.modeT + 1) < kFlipGraceTicks)
                             || (((gdUpOf(c) != gdUpOf(s)) ? 0 : (int)s.flipT + 1)
                                 < kFlipGraceTicks))) {
                        const double penF = top - (double)c.y;
                        if (g_slopeDbg)
                            std::printf("flipcubecrush: t=%lld p%d uid=%d y=%.4f seat=%.4f "
                                        "pen=%.4f\n", (long long)K.t, g_halfNow + 1, sp->uid,
                                        (double)c.y, top, penF);
                        if (penF > 2.0) DIE("slope/crush", sp);
                        continue;
                    }
                    if (smp.inside) slopeTookInside = true;
                    if (g_slopeDbg)
                        std::printf("slopedbg t=%lld x=%.2f y=%.3f yfree=%.3f "
                                    "pH=%.1f "
                                    "uid=%d m=%.3f top=%.3f gap=%.3f allow=%.3f "
                                    "why=%s\n",
                                    (long long)K.t, x, (double)c.y,
                                    (double)yFreeBeforeSlope, pH, sp->uid,
                                    m, top, ((double)c.y - top) * gs, landAllow,
                                    (((double)c.y - top) * gs <= landAllow)
                                        ? "land"
                                        : (seatTolOk
                                               ? "seat1"
                                               : (stickHere ? "stick"
                                                            : "inside")));
                    if (ridesTop && !c.flip && K.near && s.onSlope
                        && sp->uid != s.slopeUidNow) {
                        bool blockedByFlat = false;
                        for (const Obj* nb : *K.near) {
                            if (nb->type != 0 || nb->oneway) continue;
                            if (std::fabs(x - nb->cx) > nb->hw) continue;
                            const double face = nb->cy + nb->hh;
                            if (face > (double)c.y + pHalf) continue;
                            if (face + pHalf > top + 0.001) {
                                blockedByFlat = true;
                                break;
                            }
                        }
                        if (blockedByFlat) {
                            if (g_slopeDbg)
                                std::printf("slopeblock t=%lld x=%.2f y=%.3f "
                                            "uid=%d top=%.3f - a flat solid is "
                                            "holding it up, no seat\n",
                                            (long long)K.t, x, (double)c.y,
                                            sp->uid, top);
                            continue;
                        }
                    }
                    if (rampSlabLiftNow) continue;
                    if (g_slopeDbg)
                        std::printf("slopeseat t=%lld x=%.2f uid=%d y %.3f -> "
                                    "%.3f\n",
                                    (long long)K.t, x, sp->uid, (double)c.y, top);
                    YSET(c.y) = (float)top;
                    ++g_seatGateSeen;
                    g_seatImpulsedOff = impulsedOffSlope ? 1 : 0;
                    g_seatTappedOff = tappedOffSlope ? 1 : 0;
                    g_seatOnSlope = s.onSlope ? 1 : 0;
                    g_seatTook = (impulsedOffSlope || tappedOffSlope) ? 1 : 0;
                    if (impulsedOffSlope || tappedOffSlope) {
                        const double sgnT2 =
                            ((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0;
                        if (ridesTop && !c.flip && c.mode == 0
                            && m * sgnT2 < -0.01)
                            YSET(c.y) = (float)top;
                        continue;
                    }
                    const float vyPreRide = c.vy;
                    if (slopeNudgeMode(c.mode)) {
                        const double gsN = c.flip ? -1.0 : 1.0;
                        const bool heldN = waveRide ? (s.held != 0) : (input != 0);
                        if (slopeUnderside(c.flip != 0, sp->slopeDir)) {
                            if (gsN * (double)c.vy > 0.0) VYSET(c.vy) = 0.f;   // V3
                            VYSET(c.vy) = slopeNudge(c.vy, c.flip != 0, sp->slopeDir,
                                              c.mode, heldN);
                        } else if (slopeSkipsHitGround(c.flip != 0,
                                                       sp->slopeDir, c.mode,
                                                       heldN)) {
                            VYSET(c.vy) = slopeNudge(c.vy, c.flip != 0, sp->slopeDir,
                                              c.mode, heldN);
                        } else {
                            VYSET(c.vy) = 0.f;                                 // V1
                        }
                    } else if (c.mode == 1 && input) {
                        const double dir = ((m * (double)useDx >= 0.0) ? 1.0 : -1.0)
                                         * (c.flip ? -1.0 : 1.0);
                        VYSET(c.vy) = (s.onSlope && c.slopeM == (float)m
                                && s.vy != 0.0f)
                                   ? (float)qVy((double)s.vy + kShipRampG * dir)
                                   : (float)(2.0 * dir);
                    } else if (c.mode == 7) {
                        const double dir = (m * (double)useDx >= 0.0) ? 1.0 : -1.0;
                        const double gRide = swingGm(c.mini != 0, gMod);
                        const bool walkIn0 =
                            (s.onSlope && c.slopeM == (float)m)
                                ? (s.vy == 0.0f)
                                : (s.grounded
                                   || (forcePushOn()
                                       && forceBoxAcc(modX, modY, pHalf,
                                                      forceUnitFor(s.mode, useDx), (long long)K.t)
                                                  * (s.flip ? -1.0 : 1.0)
                                              > kSwingG));
                        VYSET(c.vy) = walkIn0
                                   ? 0.0f
                                   : (s.onSlope && c.slopeM == (float)m)
                                   ? (float)qVy((double)s.vy + gRide * dir)
                                   : (float)(2.0 * dir);
                    } else if (slopeUnderside(c.flip != 0, sp->slopeDir)) {
                        if ((c.flip ? -1.0 : 1.0) * (double)c.vy > 0.0) VYSET(c.vy) = 0.f;
                    } else if (!(flipForRide && ridesTop)) {
                        VYSET(c.vy) = 0;
                    }
                    const double svRide =
                        (double)vyPreRide * (c.flip ? -1.0 : 1.0);
                    const bool rideLands = svRide <= kSlopeLandV;
                    const bool undoneBySide = slopeUnderside(c.flip != 0, sp->slopeDir);
                    if (!rideLands && !undoneBySide)
                        VYSET(c.vy) = vyPreRide;               // 0x3907dd's restore
                    if (rideLands && !(flipForRide && ridesTop)) c.grounded = 1;
                    else if (rideLands) rodeFlipped = true;
                    c.rideLanded = (uint8_t)(rideLands || (s.onSlope && s.rideLanded));
                    c.onSlope = 1;
                    c.slopeM = (float)m;
                    c.slopeUid0 = s.onSlope ? s.slopeUid0 : sp->uid;
                    c.slopeUidNow = sp->uid;
                    suSeatUid = sp->uid;   // --slopeunchanged
                }
            }
            if (movWallSet) {
                YSET(c.y) = movWallY;
                VYSET(c.vy) = movWallVy;
            }
            if (offRampRide && !c.onSlope) {
                YSET(c.y) = offRampY;
                VYSET(c.vy) = offRampVy;
                c.grounded = 1;
                c.onSlope = 1;
                const Obj* R = offRampRide;
                c.slopeM = (float)((R->sy1 - R->sy0) / (2.0 * R->hw));
                c.slopeUid0 = s.onSlope ? s.slopeUid0 : R->uid;
                c.slopeUidNow = R->uid;
                c.rideLanded = (uint8_t)(s.onSlope && s.rideLanded);
                if (g_slopeDbg)
                    std::printf("offrampride t=%lld uid=%d y=%.4f vy=%.4f m=%.3f\n", (long long)K.t,
                                R->uid, (double)c.y, (double)c.vy, (double)c.slopeM);
            }
            const bool crushLaw =
                lawSeat && lawUnder
                && (c.mode == 0 || c.mode == 5 || c.mode == 6)
                && c.y == yBeforeSlopes && c.grounded == gBeforeSlopes
                && !((((c.mode != s.mode) ? 0 : (int)s.modeT + 1) < kFlipGraceTicks)
                     || (((gdUpOf(c) != gdUpOf(s)) ? 0 : (int)s.flipT + 1)
                         < kFlipGraceTicks));
            if (crushLaw) {
                const double penL = c.flip ? (lawTop - (double)c.y) : ((double)c.y - lawTop);
                if (g_slopeDbg)
                    std::printf("undercrush: t=%lld p%d uid=%d y=%.4f seat=%.4f pen=%.4f\n",
                                (long long)K.t, g_halfNow + 1, lawUid, (double)c.y, lawTop,
                                penL);
                if (penL > 2.0) {
                    const Obj* lc = nullptr;
                    for (const Obj* q : *K.slopes)
                        if (q->uid == lawUid) { lc = q; break; }
                    DIE("slope/crush", lc);
                } else {
                    c.grounded = 0;
                }
            }
            const bool lawStack = lawSeat && lawUnder && !crushLaw
                && ceilPressedNow && c.y != yBeforeSlopes && c.grounded == gBeforeSlopes
                && (c.mode == 1 || c.mode == 3 || c.mode == 4 || c.mode == 7)
                && (c.flip ? lawTop > (double)c.y : lawTop < (double)c.y);
            if (lawSeat && !crushLaw
                && ((c.y == yBeforeSlopes && c.grounded == gBeforeSlopes) || lawStack)) {
                if (g_slopeDbg)
                    std::printf("slopelawapply: t=%lld p%d uid=%d y=%.4f->%.4f\n",
                                (long long)K.t, g_halfNow + 1, lawUid,
                                (double)c.y, lawTop);
                YSET(c.y) = (float)lawTop;
                suSeatUid = lawUid;   // --slopeunchanged
                const double gsSL = c.flip ? -1.0 : 1.0;
                if (lawUnder) {
                    if (gsSL * (double)c.vy > 0.0) VYSET(c.vy) = 0.0f;
                    if (c.frame == 0 && !input
                        && (c.mode == 1 || c.mode == 3 || c.mode == 4 || c.mode == 7)) {
                        const Obj* lo = nullptr;
                        for (const Obj* q : *K.slopes)
                            if (q->uid == lawUid) { lo = q; break; }
                        if (lo) {
                            const bool upL = lo->slopeDir == 0 || lo->slopeDir == 3
                                          || lo->slopeDir == 6 || lo->slopeDir == 7;
                            const bool leftL = (double)K.dxF < 0.0 || s.rev != 0;
                            const bool b22 = ((!upL) != leftL) != (c.flip != 0);
                            if (b22 && gsSL * (double)c.vy > -2.0)
                                VYSET(c.vy) = (float)(-2.0 * gsSL);
                        }
                    }
                    if (c.frame == 0 && !c.flip
                        && (c.mode == 1 || c.mode == 3 || c.mode == 6 || c.mode == 7
                            || (c.mode == 2))) {
                        const Obj* lr = nullptr;
                        for (const Obj* q : *K.slopes)
                            if (q->uid == lawUid) { lr = q; break; }
                        if (lr) {
                            const double mR = (lr->sy1 - lr->sy0) / (2.0 * lr->hw);
                            const bool revR = (double)K.dxF < 0.0 || s.rev != 0;
                            if ((revR ? -mR : mR) < 0.0) {
                                ceilPressedNow = true;
                                ceilPressM = mR;
                                ceilPressMode = c.mode;
                                ceilPressUid = lr->uid;
                            }
                        }
                    }
                } else if (gsSL * (double)c.vy <= kSlopeLandV) {
                    VYSET(c.vy) = 0.0f;
                    c.grounded = 1;
                }
                if (!lawUnder) lawSeatedNow = true;
                if (!lawUnder && !s.dual && c.mode == 3 && !c.flip) {
                    const double mTravL =
                        lawM * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                    if (mTravL > 0.0)
                        c.lawSv = (float)slopeExitVy(std::fabs(lawM), c.mode, useDx,
                                                     c.mini != 0);
                }
                if (!lawUnder && !s.dual && c.mode == 1 && c.frame == 0) {
                    const double mTravL =
                        lawM * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                    if (c.flip ? mTravL < 0.0 : mTravL > 0.0)
                        c.lawSv = (float)((c.flip ? -1.0 : 1.0)
                                          * slopeExitVy(std::fabs(lawM), 1, useDx,
                                                        c.mini != 0));
                }
            }
            if (g_slopeStick && !dead && c.mode == 0 && !c.flip && c.frame == 0 && !s.dual) {
                int seatNow = -1;
                if (lawSeatedNow && c.y == (float)lawTop) seatNow = lawUid;
                else if (c.onSlope && c.grounded) seatNow = c.slopeUidNow;
                const int cand = seatNow >= 0 ? seatNow : s.stickSlope;
                c.stickSlope = seatNow;
                const Obj* st = nullptr;
                if (cand >= 0)
                    for (const Obj* q : *K.slopes)
                        if (q->uid == cand) { st = q; break; }
                if (st && !slopeIsCeiling(st->slopeDir) && (double)st->dcy < 0.0) {
                    const double pHs = playerHalf(c.mode, c.mini != 0);
                    const double sx0 = st->cx - st->hw, sx1 = st->cx + st->hw;
                    const bool rectX = x + pHs > sx0 && x - pHs < sx1;
                    const bool rectY = (double)c.y - pHs < st->cy + st->hh - 1.0
                                       && (double)c.y + pHs > st->cy - st->hh + 1.0;
                    const double tgt = slopeSeatTarget(st, x, pHs, false, 0.0);
                    const double tol = std::min(std::fabs((double)st->dcy), 1.25);
                    if (rectX && rectY && (double)c.y < tgt + tol) {
                        if (g_slopeDbg)
                            std::printf("slopestick: t=%lld uid=%d y=%.4f->%.4f dcy=%.4f tol=%.4f\n",
                                        (long long)K.t, st->uid, (double)c.y, tgt,
                                        (double)st->dcy, tol);
                        YSET(c.y) = (float)tgt;
                        if ((double)c.vy <= kSlopeLandV) {
                            VYSET(c.vy) = 0.0f;
                            c.grounded = 1;
                        }
                        c.stickSlope = st->uid;
                    }
                }
            }
            if (c.onSlope && c.y != yBeforeSlopes) {
                ySeatSp = (double)c.y;
                seatUidSp = c.slopeUidNow;
            }
            if (flyHazPending) {
                if (!dead && c.y == yBeforeSlopes) {
                    if (g_flyHazTele) flyHazTeleHeld = flyHazPending;   // --flyhaztele
                    else DIE("fly/hazard", flyHazPending);
                }
                flyHazPending = nullptr;
            }
            if (!dead && (c.mode == 1 || c.mode == 3) && c.y != yBeforeSlopes) {
                for (const Obj* o : *K.near) {
                    if (dead) break;
                    if (o->type == 0) continue;
                    if (hazardStage1(o, x, (double)c.y, pHalf, kHazMargin,
                                     sawMarginFor(s.mode), s.mode, c.mini, g_obbAll)
                        && (!g_obbAll || !o->obbOk
                            || obbOverlap(*o, x, (double)c.y, pHalf, (double)s.rot)))
                        DIE("fly/hazard-afterslope", o);
                }
            }
            if (thinSupHaz && !dead && K.t > 14) {
                for (const Obj* o : *K.near) {
                    if (dead) break;
                    if (o->type == 0) continue;
                    // --radreach: the circle's own reach (the cube walk's note)
                    if (std::fabs(x - o->cx)
                        > ((o->radius > o->hw) ? o->radius : o->hw) + pHalf
                              + kContactEps)
                        continue;
                    const bool warpedT = spiderWarpedThisTick;   // as the walk treats a warp
                    for (int si = warpedT ? kSubSteps : 0; si <= kSubSteps && !dead; ++si) {
                        if (g_hazEndpoint && si < kSubSteps) continue;
                        const double f = si / (double)kSubSteps;
                        const double sx = xPrev + (x - xPrev) * f;
                        const double sy = warpedT
                            ? (double)s.y : (double)s.y + ((double)c.y - (double)s.y) * f;
                        if (hazardStage1(o, sx, sy, pHalf, kHazMargin, sawMarginFor(s.mode),
                                         s.mode, c.mini, g_obbAll)
                            && (!g_obbAll || !o->obbOk
                                || obbOverlap(*o, sx, sy, pHalf, (double)s.rot))) {
                            if (hazOnceSkip()) hazOnceDeferred = true;   // --hazportalonce
                            else DIE("cube/hazard", o);
                        }
                    }
                }
            }
            const double ph = (c.mode == 4 && c.mini)
                ? kWaveHazHalfMini : playerHalf(c.mode, c.mini != 0);
            const double inner = (c.mode == 4) ? kWaveKillHalf : kCubeInner;
            const bool plainKillsHere = (c.mode == 4) && !c.onSlope
                && !(waveRampArm && !c.mini && !c.flip && c.frame == 0);
            const bool shipCapK = c.mode == 1 || c.mode == 3 || c.mode == 2;
            for (const Obj* sp : *K.slopes) {
                if (!sp->slopeHazard && !plainKillsHere) continue;
                const double sx0 = sp->cx - sp->hw, sx1 = sp->cx + sp->hw;
                const double sm = (sp->sy1 - sp->sy0) / (sx1 - sx0);
                const bool inWin = (c.mode == 4 || shipCapK)
                    ? (x >= sx0 - ph && x <= sx1 + ph)
                    : (x + pH > sx0 && x - pH < sx1);
                if (inWin) {
                    const double surf = sp->sy0 + sm * (x - sx0);
                    const double lineLo = std::min(sp->sy0, sp->sy1);
                    const double lineHi = std::max(sp->sy0, sp->sy1);
                    const double off = ph * std::sqrt(1.0 + sm * sm);
                    const bool ceilSlope = slopeIsCeiling(sp->slopeDir);
                    const double y0 = (double)c.y;
                    bool hit;
                    if (c.mode == 4 || c.mode == 1 || c.mode == 3 || c.mode == 2) {
                        const double offW = (c.mode == 4 && c.flip
                                             && ceilSlope)
                            ? kWaveFlipSlopeKillD * std::sqrt(1.0 + sm * sm)
                            : off;
                        const double objMinY = sp->cy - sp->hh, objMaxY = sp->cy + sp->hh;
                        const double innerS = std::min(kCubeInner, ph - kFlyThinTol);
                        const bool flatInX = x >= sx0 - kCubeInner && x <= sx1 + kCubeInner;
                        const uint8_t dirK = sp->slopeDir;
                        const bool wallLeftK = dirK == 2 || dirK == 3 || dirK == 4 || dirK == 6;
                        const bool leftK = (double)K.dxF < 0.0 || s.rev != 0;
                        if (shipCapK && (wallLeftK ? x < sx0 : (leftK && x > sx1))) {
                            const double wallX = wallLeftK ? sx0 : sx1;
                            if (std::fabs(x - wallX) <= kCubeInner
                                && y0 > objMinY - innerS && y0 < objMaxY + innerS)
                                DIE("slope/spiked", sp);
                            continue;
                        }
                        if (g_waveSlopeWall && c.mode == 4 && !sp->slopeHazard
                            && (wallLeftK ? x < sx0 : (leftK && x > sx1))) {
                            const double wallX = wallLeftK ? sx0 : sx1;
                            if (std::fabs(x - wallX) <= kWaveKillHalf
                                && y0 > objMinY - kWaveKillHalf && y0 < objMaxY + kWaveKillHalf)
                                DIE("slope/inside", sp);
                            continue;
                        }
                        const bool waveCapK = c.mode == 4 && !c.flip;
                        const bool wasSlopeK = s.onSlope || s.ceilT > 0;
                        const bool isSlopeK = c.onSlope || ceilPressedNow || ceilContNow;
                        const bool capK = shipCapK || waveCapK;
                        const double insetK = (capK && wasSlopeK) ? 0.0 : 1.0;
                        const bool revK = (double)K.dxF < 0.0 || s.rev != 0;
                        const double headTol =
                            (slopeUnderside(c.flip != 0, sp->slopeDir)
                             && slopeNudgeMode(c.mode) && input
                             && slopeBVar22(c.flip != 0, sp->slopeDir) == revK && !isSlopeK)
                                ? (wasSlopeK ? 2.0 : 1.0)
                                : 0.0;
                        const double gateLo = objMinY - ph + insetK;
                        const double thrLo = std::max(surf - offW, objMinY - ph) - headTol;
                        const double lo = ceilSlope
                            ? (capK ? std::max(thrLo, gateLo)          // sloped, capped, gated
                                    : std::max(surf - offW, lineLo) - headTol)
                            : shipCapK ? (flatInX ? objMinY - innerS : objMinY + 1.0)
                                       : sp->cy - sp->hh - inner;        // flat
                        const bool heldNoDownK = c.mode == 1 && input;
                        const double downDy = ((c.mode == 4 || shipCapK) && !c.flip
                                               && !ceilSlope && sm < 0.0 && !heldNoDownK)
                            ? kWaveSlopeDescendDy : 0.0;
                        const double capHi = capK ? objMaxY + ph - insetK : lineHi;
                        // (capHi is the floor ramp's gate for the two capped outlines.)
                        const double thrHi = std::min(surf + off, objMaxY + ph) + downDy + headTol;
                        const double hi = ceilSlope
                            ? (shipCapK ? (flatInX ? objMaxY + innerS : objMaxY)
                                        : sp->cy + sp->hh + inner)         // flat
                            : capK ? std::min(thrHi, capHi)            // sloped, capped, gated
                                   : std::min(surf + off + downDy, capHi) + headTol;
                        const bool strictLo = capK && ceilSlope && thrLo >= gateLo;
                        const bool strictHi = capK && !ceilSlope && thrHi <= capHi;
                        hit = (strictLo ? y0 > lo : y0 >= lo) && (strictHi ? y0 < hi : y0 <= hi);
                    } else {
                        const double sxc = std::min(std::max(x, sx0), sx1);
                        const double sf = sp->sy0 + sm * (sxc - sx0);
                        const double lo = ceilSlope
                            ? std::max(y0 - pH, sf)
                            : std::max(y0 - pH, sp->cy - sp->hh);
                        const double hi = ceilSlope
                            ? std::min(y0 + pH, sp->cy + sp->hh)
                            : std::min(y0 + pH, sf);
                        hit = (lo < hi - 0.001);
                    }
                    if (hit)
                        DIE(sp->slopeHazard ? "slope/spiked" : "slope/inside", sp);
                }
            }
        }
        // --flyhazdefer: ...and a held hit that no ramp pass resolved (no ramps in the window) stands
        if (flyHazPending) {
            if (!dead) {
                if (g_flyHazTele) flyHazTeleHeld = flyHazPending;   // --flyhaztele
                else DIE("fly/hazard", flyHazPending);
            }
            flyHazPending = nullptr;
        }
        const double mTravRide = (double)c.slopeM
                               * (((double)K.dxF < 0.0 || s.rev != 0)
                                      ? -1.0 : 1.0);
        if (c.onSlope && !c.flip && K.near && mTravRide < 0.0) {
            double bestFace = -1e18;
            for (const Obj* nb : *K.near) {
                if (nb->type != 0 || nb->oneway) continue;
                if (std::fabs(x - nb->cx) > nb->hw) continue;   // centre over it
                const double face = nb->cy + nb->hh;
                if (face + pHalf <= (double)c.y + 0.001) continue;
                if (face > (double)c.y + pHalf) continue;   // a wall is a different story
                if (face > bestFace) bestFace = face;
            }
            if (bestFace > -1e17) YSET(c.y) = (float)(bestFace + pHalf);
        }
        if (K.near) {
            const double preY = (double)yFreeBeforeSlope;
            const double seatY = (double)c.y;
            if (seatY != preY) {
                const bool up = seatY > preY;
                double lim = seatY;
                for (const Obj* const& o : *K.near) {
                    if (o->type != 0 || o->oneway) continue;
                    if (std::fabs(x - o->cx) > o->hw + pHalf) continue;
                    const double f = up ? (o->cy - o->hh - pHalf)
                                        : (o->cy + o->hh + pHalf);
                    if (up ? !(preY <= f && seatY > f)
                           : !(preY >= f && seatY < f)) continue;
                    if (vetoOf(K, o, x, seatY, pHalf, pHalf,
                               !up, xPrev, (double)s.y, c.slopeUidNow, lawWasOn))
                        continue;
                    if (up ? (f < lim) : (f > lim)) lim = f;
                }
                if (lim != seatY) YSET(c.y) = (float)lim;
            }
        }
        const double mTravel = (double)s.slopeM
                             * (((double)K.dxF < 0.0 || s.rev != 0)
                                    ? -1.0 : 1.0);
        const bool rodeCeil = (s.grounded != 0);
        const bool uphillExit = (c.flip && rodeCeil) ? (mTravel < 0.0)
                                                     : (mTravel > 0.0);
        const bool a1cHoldsLaunch = a1cNow
                                    && (c.mode == 0 || c.mode == 5 || c.mode == 6);
        const bool uphillLaunch = !c.onSlope && s.onSlope && uphillExit
            && (!rampWindowHere || (g_movingWallRise && movWallTopNow))
            && s.rideLanded
            && !(impulsedThisTick
                 && (c.mode == 3
                     || (c.flip && !(g_rampEndClick && c.mode == 0 && input && !s.action
                                     && rampEndHere && !rampOkHere))))
            && !a1cHoldsLaunch;
        const float exitDx = (g_seatSpeed && s.seatSpd) ? dxForSpeedCode(s.seatSpd) : useDx;
        const float exitVy = uphillLaunch
            ? (float)rawQ(((c.flip && rodeCeil) ? -1.0 : 1.0)   // speed.hpp
                          * slopeExitVy(std::fabs((double)s.slopeM), c.mode,
                                        exitDx, c.mini != 0)
                          * slopeRampFactor((int)s.slopeT + 1))
            : 0.0f;
        const bool freshPress = input && !s.action;
        const bool headJump = g_exitJump && impulsedThisTick && cubeGroundJumpNow && s.onSlope
                              && s.grounded && !s.flip && !c.flip;
        const bool exitLoses = uphillLaunch
            && !(impulsedThisTick && freshPress && !headJump)
            && !(exitVy > 0.0f ? exitVy > c.vy : exitVy < c.vy);
        if (uphillLaunch && !exitLoses) {
            if (freshTapHeld && c.mode == 2 && !ballFlippedThisTick) g_ballTapLaunch = 1;
            VYSET(c.vy) = exitVy;
            c.grounded = 0;
            if (g_lingerLaunch) c.ogLinger = 0;
            a1cLate = true;   // --a1clatch: boostPlayer raises +0xa1c (0x39feee)
            c.boost = 1;
            if (c.mode == 0) rotBoostNow = true;
            releasePin(false);
        }
        else if (!uphillLaunch && !c.onSlope && s.onSlope && !rampWindowHere
                 && s.rideLanded
                 && !impulsedThisTick
                 && !ceilPressedNow
                 && !(a1cNow)
                 && !(g_waveRamp && c.mode == 4 && s.held)) {
            VYSET(c.vy) = (float)(mTravel * std::fabs((double)K.dxF) / 0.25);
            if (g_slopeRelDbg)
                std::printf("sloperel: t=%lld mode=%d mini=%d slopeM=%.4f "
                            "mtrav=%.6f dxF=%.6f vy=%.6f\n",
                            (long long)K.t, (int)c.mode, (int)c.mini,
                            (double)c.slopeM, mTravel, (double)K.dxF,
                            (double)c.vy);
            c.grounded = 0;
            if (c.mode == 0) rotReleaseNow = true;
            c.boost = 1;
            if (!pinnedOnBlock) releasePin(false);
            if (K.near) {
                const double gsR = c.flip ? -1.0 : 1.0;
                const double reachR = 5.0 * 0.25;
                const Obj* stick = nullptr;
                double stickGap = 1e9;
                for (const Obj* o : *K.near) {
                    if (o->type != 0 || gsR * o->dcy >= 0.0) continue;
                    if (std::fabs(x - o->cx) >= o->hw + pHalf) continue;
                    const double face = o->cy + gsR * o->hh;
                    const double gap = ((double)c.y - gsR * pHalf - face) * gsR;
                    if (gap > -0.5 && gap < reachR && gap < stickGap) {
                        stick = o;
                        stickGap = gap;
                    }
                }
                if (stick) {
                    YSET(c.y) = (float)(stick->cy + gsR * (stick->hh + pHalf));
                    VYSET(c.vy) = 0;
                    c.grounded = 1;
                    CLAMP0O("slope/stickrelease", stick);
                }
            }
        }
        // --seatspeed (constants.hpp): the speed this tick's seat ran at, for the next tick's test
        if (g_seatSpeed) c.seatSpd = c.onSlope ? speedCodeForDx(useDx) : 0;
        if (s.mode == 1 && c.mode == 1 && s.lawSv != 0.0f && !s.onSlope
            && !c.onSlope && !lawSeatedNow && !rampWindowHere && !impulsedThisTick
            && c.frame == 0) {
            const float evL = (float)rawQ((double)s.lawSv * slopeRampFactor((int)s.seatT));
            if (evL > 0.0f ? evL > c.vy : evL < c.vy) {
                VYSET(c.vy) = evL;
                c.grounded = 0;
                a1cLate = true;   // --a1clatch: boostPlayer raises +0xa1c
                c.boost = 1;
            }
        }
        if (lawRepeatDrop && !c.onSlope && !lawSeatedNow && s.grounded && !rampWindowHere
            && !ceilPressedNow && !impulsedThisTick && !(a1cNow)
            && !(g_waveRamp && c.mode == 4 && s.held)) {   // --waveramp: as the release above
            const double mTravL =
                lawDropM * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
            if (mTravL < 0.0) {
                VYSET(c.vy) = (float)(mTravL * std::fabs((double)K.dxF) / 0.25);
                c.grounded = 0;
            } else if (mTravL > 0.0 && !c.flip) {
                const float exitL = (float)rawQ(
                    slopeExitVy(std::fabs(lawDropM), c.mode, useDx, c.mini != 0)
                    * slopeRampFactor(std::max<int>(1, (int)s.seatT)));
                if (exitL > c.vy) {
                    VYSET(c.vy) = exitL;
                    c.grounded = 0;
                    a1cLate = true;   // --a1clatch: boostPlayer raises +0xa1c (0x39feee)
                }
            }
        }
        const double prev = (double)s.prevC6q / 1000.0;
        const bool flipArm = g_mpushFlip && c.flip;
        if (flipArm) {
            if (prev != 0.0 && std::fabs(prev - c6Now) > 0.5 && prev < -5.0 && prev < c6Now) {
                const double p = std::max(prev, -20.0);
                if (p <= (double)c.vy) {
                    VYSET(c.vy) = (float)p;
                    c.grounded = 0;
                    a1cLate = true;   // --a1clatch: boostPlayer (0x38ec5c)
                    if (g_seatC6) c.boost = 1;
                }
            }
        } else if (prev != 0.0 && std::fabs(prev - c6Now) > 0.5 && prev > 5.0
            && prev > c6Now) {
            const double p = std::min(prev, 20.0);
            if (p >= (double)c.vy) {
                VYSET(c.vy) = (float)p;
                c.grounded = 0;
                a1cLate = true;   // --a1clatch: boostPlayer (0x38ec5c)
                if (g_seatC6) c.boost = 1;
            }
        }
        c.prevC6q = (int16_t)std::lround(
            std::max(-32.0, std::min(32.0, c6Now)) * 1000.0);
        const bool rampSeam = (s.grounded != 0 && s.slopeT > 0);
        const bool sameRide = (s.slopeT > 0
                               && ((c.slopeUid0 >= 0 && c.slopeUid0 == s.slopeUid0)
                                   || (c.slopeUidNow >= 0 && c.slopeUidNow == s.slopeUidNow)));
        c.slopeT = c.onSlope
            ? (uint8_t)((s.onSlope || rampSeam || sameRide)
                            ? std::min<int>(24, (int)s.slopeT + 1) : 0)
            : (uint8_t)(rampSeam ? s.slopeT : 0);
        if (c.onSlope) {
            if (!(s.onSlope || rampSeam || sameRide) && s.seatT > 0)
                c.slopeT = (uint8_t)std::min<int>(24, (int)s.seatT);
            c.seatT = 0;
        } else {
            c.seatT = lawSeatedNow
                ? (uint8_t)std::min<int>(24, (int)s.seatT + 1) : 0;
        }
        (void)0;
    }
    if (c.frame == 0) {
        // --goalspare: above the bound but alive to the goal (speed.hpp)
        if ((c.y > g_yBound
             && !reachesGoalUnderMaxPlayY(x, (double)c.y, (double)c.vy, (double)c.dx, c.rev != 0))
            || c.y < (kGroundY + pHalf) - 40)
            DIE_AS(kDeathPrune, "out-of-play", nullptr);
    } else if (std::fabs((double)c.y) > g_yBoundTurned) {
        DIE_AS(kDeathPrune, "out-of-play", nullptr);
    }
    if (!dead && c.mode != 1 && c.mode != 4 && !(c.mode == 3) && c.flip
        && !c.grounded && !spiderFreshTp
        && !(c.mode == 6 && ((g_spiderCoyote && c.ogLinger) || spiderBandCeil))
        && (double)c.y - pHalf > topAheadAtF((int)c.frame, x)
        // --escdyn: ...nor while a moving object ahead can still be above it (search_key.hpp)
        && !(g_escDyn && c.frame == 0 && (double)c.y - pHalf <= topAheadDynAt(x))) {
        bool spareEsc = false;
        double escDist = 0.0;
        if (c.frame != 0 && g_rotQueue && !g_rotQ.empty()) {
            const int ch = (int)c.rotChan & 15;
            const int beg = g_rotQBeg[(size_t)ch], end = g_rotQEnd[(size_t)ch];
            const int done = popCount32(c.rotSpent & g_rotQChanMask[(size_t)ch]);
            double wx, wy;
            fromFrame((int)s.frame, (double)s.xAbs, (double)s.y, wx, wy);
            const bool rev = ((c.rotRev >> ch) & 1u) != 0;
            const bool vertical = ((int)c.frame & 1) != 0;
            const double ref = vertical ? wy : wx;
            for (int idx = beg + done; idx < end; ++idx) {
                const RotQEntry& e = g_rotQ[(size_t)idx];
                if (!(e.id == 2900 && !e.chanOnly && e.rotIdx >= 0)) continue;
                const double p = vertical ? e.py : e.px;
                if (rev ? (p < ref) : (ref < p)) {
                    spareEsc = true;
                    escDist = std::fabs(p - ref);
                }
                break;   // the first ROTATING entry decides, fired or not
            }
            if (spareEsc && g_slopeDbg)
                std::printf("escspare: t=%lld frame=%d x=%.3f y-pH=%.3f "
                            "dist=%.3f ticks=%.1f\n",
                            (long long)K.t, (int)c.frame, x, (double)c.y - pHalf,
                            escDist,
                            escDist / std::max(1e-6, std::fabs((double)K.dxF)));
        }
        // --goalspare: it never comes down, but it does not have to (speed.hpp)
        if (!spareEsc && c.frame == 0
            && reachesGoalUnderMaxPlayY(x, (double)c.y, (double)c.vy, (double)c.dx, c.rev != 0)) {
            spareEsc = true;
            if (g_slopeDbg)
                std::printf("goalspare: t=%lld x=%.3f y=%.3f vy=%.3f goal=%.1f\n",
                            (long long)K.t, x, (double)c.y, (double)c.vy, g_goalX);
        }
        if (!spareEsc) {
            if (g_slopeDbg) {
                long long bi = -1, bn = 0;
                topAheadIndex((int)c.frame, x, bi, bn);
                std::printf("escapee: t=%lld frame=%d x=%.3f y-pH=%.3f "
                            "topAhead=%.3f i=%lld size=%lld\n",
                            (long long)K.t, (int)c.frame, x, (double)c.y - pHalf,
                            topAheadAtF((int)c.frame, x), bi, bn);
            }
            DIE_AS(kDeathPrune, "escapee-prune", nullptr);
        }
    }
    const double pHalfPortal =
        (c.mode == 4 && c.mini) ? kWaveContactHalfMini : pHalf;
    const double pHalfSize = pHalfPortal;
    double pHalfLive = pHalfPortal;
    const double yColl = (c.grounded && !s.grounded) ? (double)s.y
                                                     : (double)c.y;
    int p2SoloUid = -1;
    if (g_p2Solo && g_p2SoloHalf && c.dual) {
        for (const Obj* p : *K.ports) {
            if (p->type != 24 || p->oriented) continue;
            if (std::fabs(x - p->cx) > p->hw + pHalfPortal) continue;
            if (std::fabs((double)yFree - p->cy) > p->hh + pHalfPortal) continue;
            if (p2SoloUid < 0 || p->uid < p2SoloUid) p2SoloUid = p->uid;
        }
    }
    auto runPortalPass = [&](int pport) {
    for (const Obj* p : *K.ports) {
        if (spiderWarpedThisTick) break;
        if ((p->type == 28) != (pport == 0)) continue;
        if (p->pendOff) {
            bool fires = false;
            if (input && !s.action && K.trigs)
                for (const auto& tb : *K.trigs)
                    if (touchBitIndex(tb.second) == p->pendBox) {
                        fires = !(s.trig & tb.second) && pressFreshFires(*tb.first, s, K.t);
                        break;
                    }
            if (!fires) continue;
        }
        if (p2SoloUid >= 0 && p->uid > p2SoloUid) continue;   // --p2solo (above)
        // --portalslopeorder: the nested step dealt with this one ahead of the ramps.
        if (g_prePortalUid >= 0 && p->uid == g_prePortalUid) continue;
        // --tporder: a larger uid the pass does not reach after the teleport is next tick's.
        const bool teleAfterP = teleportedThisTick
            && gdTeleAfter(p->cx, p->cy, p->uid, teleCx, teleCy, teleUid, x, teleFromY);
        if (g_tpOrder && pport == 1 && teleportedThisTick && p->uid > teleUid && !teleAfterP)
            continue;
        const bool oldPosHere = pport == 1 && teleportedThisTick && p->uid < teleUid
                                && !(g_tpOrder && teleAfterP);
        struct OldPosGuard {
            State& st; float& yf; bool on; float y, f;
            ~OldPosGuard() { if (on) { st.y = y; yf = f; } }
        } oldPosGuard{c, yFree, oldPosHere, c.y, yFree};
        if (oldPosHere) { c.y = teleFromY; yFree = teleFromYFree; }
        double pHalfP =
            p->oriented ? pHalfLive
                        : ((p->type == 17 || p->type == 18) ? pHalfSize
                                                            : pHalfPortal);
        if (g_portReRead && modeFiredThisTick && s.frame == 0 && !p->oriented && p->uid != modeFiredUid
            && gdRectReReadBetween(*p, modeFiredCx, modeFiredCy, modeFiredUid, K.slopes, x,
                                   (double)yFree, pHalfPortal)) {
            const bool rectMini = sizeFiredThisTick ? sizePreMini != 0 : c.mini != 0;
            pHalfP = (c.mode == 4 && rectMini) ? kWaveContactHalfMini : playerHalf(c.mode, rectMini);
        }
        if (g_hazDbgUid == p->uid)
            std::printf("portdbg t=%lld uid=%d type=%d x=%.3f xPrev=%.3f y=%.4f yFree=%.4f "
                        "sy=%.4f mode=%d mini=%d pHalfP=%.2f |dx|=%.3f lim=%.3f "
                        "|dy|=%.3f limy=%.3f latch=%d\n",
                        (long long)K.t, p->uid, (int)p->type, x, xPrev, (double)c.y,
                        (double)yFree, (double)s.y, (int)c.mode, (int)c.mini, pHalfP,
                        std::fabs(x - p->cx), p->hw + pHalfP, std::fabs((double)yFree - p->cy),
                        p->hh + pHalfP,
                        (p->gpBit >= 0 && s.portalLatch.test(p->gpBit)) ? 1 : 0);
        if (std::fabs(x - p->cx) > p->hw + pHalfP) continue;
        double pRotHere = (double)s.rot;
        if (g_noSatRotRaw && s.mode == 0)
            pRotHere += (s.rotNeg ? 1.0 : -1.0) * (s.mini ? 2.25 : 1.7307692);
        if (g_slopeDbg && p->oriented
            && !orientedHit(*p, x, (double)c.y, pHalfP, pRotHere))
        {
            const double thO = std::atan2(-p->rs, p->rc);
            const double a1 = -thO;
            const double a2 = -pRotHere * 3.14159265358979 / 180.0;
            const double ddx = x - p->cx, ddy = (double)c.y - p->cy;
            const double c1 = std::cos(a1), s1 = std::sin(a1);
            const double c2 = std::cos(a2), s2 = std::sin(a2);
            const double axv[4] = { c1, -s1, c2, -s2 };
            const double ayv[4] = { s1,  c1, s2,  c2 };
            double marg[4];
            for (int i = 0; i < 4; ++i) {
                const double ux = axv[i], uy = ayv[i];
                const double r1 = p->ohw * std::fabs(c1 * ux + s1 * uy)
                                + p->ohh * std::fabs(-s1 * ux + c1 * uy);
                const double r2 = pHalfP
                                  * (std::fabs(c2 * ux + s2 * uy)
                                     + std::fabs(-s2 * ux + c2 * uy));
                marg[i] = (r1 + r2) - std::fabs(ddx * ux + ddy * uy);
            }
            std::printf("portobb t=%lld uid=%d type=%d x=%.2f y=%.2f "
                        "pcx=%.2f pcy=%.2f ohw=%.2f ohh=%.2f pHalf=%.1f "
                        "pRot=%.3f thO=%.3f marg=%.3f/%.3f/%.3f/%.3f "
                        "REJECTED-BY-OBB\n",
                        (long long)K.t, p->uid, (int)p->type, x, (double)c.y,
                        p->cx, p->cy, p->ohw, p->ohh, pHalfP, pRotHere,
                        thO * 180.0 / 3.14159265358979,
                        marg[0], marg[1], marg[2], marg[3]);
        }
        if (p->oriented
            && !orientedHit(*p, x, (double)c.y, pHalfP, pRotHere))
            continue;
        if (portalOnFrameRotTrigger(*p, (int)c.frame)) continue;
        const bool bornLastTick = s.portSeen[0] == -2;
        const bool tpLatched = g_tpLatchExact && p->type == 28 && p->gpBit >= 0;
        const bool spawnStep = g_tpSpawn && K.t <= 2;
        const bool tpMovesX = g_tpPortalX && p->type == 28 && (p->tpKeep & 4) && c.frame == 0
                              && s.frame == 0 && !c.dual && !s.dual && !c.rev && !s.rev;
        if (tpMovesX && K.t <= 1) continue;
        const bool tpSeenGate = g_tpSeen && p->type == 28 && !tpLatched && p->uid >= 0;
        if (!tpSeenGate && p->type == 28 && !bornLastTick && !tpLatched && !spawnStep
            && portalWasInsideAtPrev(*p, xPrev, (double)s.y, pHalfP, pRotHere))
            continue;
        if (p->type == 28 && p->gpBit >= 0 && s.portalLatch.test(p->gpBit))
            continue;
        const double tpTarg = (g_tpKeepY && p->type == 28 && (p->tpKeep & 2)) ? (double)c.y
                              : (p->type == 28 && p->id != 747
                                 && (p->tpEx != 0.0 || p->tpEy != 0.0))
                                  ? p->tpEy : p->tpY;
        const bool changes =
            (p->type == 23 || p->type == 24)
                ? (c.dual != (uint8_t)(p->type == 23 ? 1 : 0))
            : (p->type == 28) ? (std::fabs((double)c.y - tpTarg) > 0.01 || tpMovesX
                                 || (g_tpPush && p->tpForce.fm != 0)
                                 || (p->tpGrav == 3)
                                 || (p->tpGrav == 1 && gdUpOf(c) != 0)
                                 || (p->tpGrav == 2 && gdUpOf(c) == 0))
            : (p->type == 3)  ? (gdUpOf(c) != 1)
            : (p->type == 4)  ? (gdUpOf(c) != 0)
            : (p->type == 17) ? (c.mini != 0)
            : (p->type == 18) ? (c.mini != 1)
                              : (c.mode != ((p->type == 5)    ? 1
                                            : (p->type == 16) ? 2
                                            : (p->type == 19) ? 3
                                            : (p->type == 26) ? 4
                                                              : 0));
        double yPort = ((p->type == 3 || p->type == 4))
                           ? (double)c.y
                           : (double)yFree;
        if (g_portSeatOrder && portSeatMover)
            yPort = gdPassAfter(*p, *portSeatMover) ? (double)c.y : (double)yFree;
        const double gapY = std::fabs(yPort - p->cy) - (p->hh + pHalfP);
        if (g_slopeDbg)
            std::printf("portgate half=%d t=%lld uid=%d type=%d changes=%d gapY=%.3f "
                        "y=%.3f pcy=%.3f phh=%.2f pHalf=%.1f flip=%d frame=%d "
                        "prot=%.3f orient=%d\n",
                        g_halfNow, (long long)K.t, p->uid, (int)p->type,
                        changes ? 1 : 0,
                        gapY, (double)c.y, p->cy, p->hh, pHalfP,
                        (int)c.flip, (int)c.frame, pRotHere,
                        p->oriented ? 1 : 0);
        if (gapY > 0.0) {
            const bool glitchDodge = g_glitchPortal > g_portalDodgeMin
                                     && gapY >= g_portalDodgeMin;
            if (changes && gapY < std::max(g_portalDodgeMin, g_glitchPortal)) {
                const bool stillClosing =
                    s.frame != c.frame
                    || std::fabs(yPort - p->cy)
                           < std::fabs((double)s.y - p->cy);
                if (!stillClosing) {
                    if (glitchDodge) DIE_AS(kDeathRule, "glitch/portal-dodge", p);
                    else DIE_AS(kDeathRule, "portal/hairline-dodge", p);
                }
            }
            if (g_portSeenX && p->uid >= 0
                && (isModePortalType(p->type) || (isGravPortalType(p->type) && p->gpBit < 0))) {
                bool seenPrev = false;
                for (int i = 0; i < 3; ++i) seenPrev = seenPrev || s.portSeen[i] == p->uid;
                if (seenPrev)
                    for (int i = 0; i < 3; ++i)
                        if (c.portSeen[i] < 0 || c.portSeen[i] == p->uid) {
                            c.portSeen[i] = p->uid;
                            break;
                        }
            }
            continue;
        }
        if (isGravPortalType(p->type) && p->gpBit >= 0) {
            const bool spent = s.portalLatch.test(p->gpBit);
            c.portalLatch.set(p->gpBit);
            if (g_slopeDbg)
                std::printf("gplatch half=%d t=%lld uid=%d bit=%d spent=%d "
                            "mask=%s\n",
                            g_halfNow, (long long)K.t, p->uid, (int)p->gpBit,
                            spent ? 1 : 0, gravLatchHex(s.portalLatch).c_str());
            if (spent) continue;
        }
        if ((isModePortalType(p->type)
             || (isGravPortalType(p->type) && p->gpBit < 0)
             || tpSeenGate)
            && p->uid >= 0) {
            bool seen = false;
            for (int i = 0; i < 3; ++i) seen = seen || s.portSeen[i] == p->uid;
            if (tpSeenGate && (bornLastTick || spawnStep)) seen = false;
            bool placed = false;
            for (int i = 0; i < 3; ++i)
                if (c.portSeen[i] < 0 || c.portSeen[i] == p->uid) {
                    c.portSeen[i] = p->uid;
                    placed = true;
                    break;
                }
            if (g_histStatOn) {   // --histstat (speed.hpp)
                if (!placed) ++g_hist.portFull;
                int k = 0;
                for (int i = 0; i < 3; ++i) k += c.portSeen[i] >= 0;
                g_hist.portMax = std::max(g_hist.portMax, k);
            }
            if (seen) continue;
        }
        if ((p->type == 3 || p->type == 4) && s.frameChg
            && s.frame == c.frame
            && portalWasInsideAtPrev(*p, xPrev, (double)s.y, pHalfP,
                                     pRotHere)) {
            if (g_slopeDbg)
                std::printf("r52hold t=%lld uid=%d type=%d frame=%d\n",
                            (long long)K.t, p->uid, (int)p->type, (int)c.frame);
            gravHoldOver = true;
            continue;
        }
        if (p->type == 28) {
            // --tplatch: teleportPlayer marks the portal whether or not it moves anything.
            if (p->gpBit >= 0) c.portalLatch.set(p->gpBit);
            if (!changes) continue;   // already at the target
            const double vIn = (double)c.vy;   // for `telefire` below
            teleportedThisTick = true;
            c.tpSkip = 1;   // State::tpSkip
            if (g_touchCensus) g_tcBranch |= 4;
            teleUid = p->uid;      // uid-order gate (measured, see teleUid's decl.)
            teleCx = p->cx;        // --tporder: where the pass met the teleport
            teleCy = p->cy;
            teleFromY = c.y;       // --teleoldpos
            teleFromYFree = yFree;
            YSET(c.y) = (float)tpTarg;   // exit half for 2902, closed tpY for 747
            if (tpMovesX) tpPortalXFired = p;   // --tpportalx: x at the end of the tick
            teleLandY = (double)c.y;
            yFree = c.y;
            c.grounded = 0;
            c.snapObj = nullptr;
            c.snapDist = 0.f;
            c.onSlope = 0;
            c.slopeM = 0.f;
            int wantUp = -1;
            if (p->tpGrav == 1) wantUp = 0;
            else if (p->tpGrav == 2) wantUp = 1;
            else if (p->tpGrav == 3) wantUp = gdUpOf(c) ? 0 : 1;
            const bool gravChanged =
                (wantUp >= 0 && gdUpOf(c) != (uint8_t)wantUp);
            if (gravChanged)
                c.flip = (c.frame == 3) ? (uint8_t)!wantUp : (uint8_t)wantUp;
            if (p->id == 747) {
                if (p->tpGrav == 1 && !(g_tpPush && g_tpForceCol)) VYSET(c.vy) = 18.0f;
                else if (s.grounded
                         && (c.mode == 0 || c.mode == 5)) {
                    const bool onFloorT = !s.flip && s.frame == 0
                        && (double)s.y <= (kGroundY + pHalf) + 0.5;
                    if (!onFloorT) {
                        const double gT = gStepMag(std::fabs(cubePhysFor(useDx).g)
                                                       * (c.mode == 5 ? kRobotGScale : 1.0),
                                                   false, c.mode == 5);
                        const double vT = (s.flip ? 1.0 : -1.0) * gT;
                        VYSET(c.vy) = (float)(gravChanged ? vT * 0.5 : vT);
                    }
                }
            } else {
                double vTp = (double)c.vy;
                if (s.grounded
                    && (c.mode == 0 || c.mode == 5)) {
                    const bool onFloor = !s.flip && s.frame == 0
                        && (double)s.y <= (kGroundY + pHalf) + 0.5;
                    if (!onFloor)
                        vTp = (s.flip ? 1.0 : -1.0)
                            * gStepMag(std::fabs(cubePhysFor(useDx).g)
                                           * (c.mode == 5 ? kRobotGScale : 1.0),
                                       false, c.mode == 5);
                }
                VYSET(c.vy) = (float)(gravChanged ? vTp * 0.5 : vTp);
            }
            if (g_tpPush && p->tpForce.fm != 0 && c.frame == 0)
                VYSET(c.vy) = tpForceVy(p->tpForce, c.vy, c.boost);
            if (g_slopeDbg)
                std::printf("telefire t=%lld uid=%d id=%d type=%d tpg=%d "
                            "vIn=%.4f vOut=%.4f y=%.3f gravChanged=%d\n",
                            (long long)K.t, p->uid, p->id, p->type, p->tpGrav,
                            vIn, (double)c.vy, (double)c.y, gravChanged ? 1 : 0);
            continue;
        }
        {
            if (g_slopeDbg)
                std::printf("portfire t=%lld frame=%d x=%.2f y=%.2f uid=%d "
                            "type=%d pcx=%.2f pcy=%.2f\n",
                            (long long)K.t, (int)c.frame, x, (double)c.y,
                            p->uid, (int)p->type, p->cx, p->cy);
            uint8_t wantFlip = (p->type == 3) ? 1 : 0;   // a toggle (42) is set below, at gdUpPortal
            const uint8_t wantMode = (p->type == 5)    ? 1
                                     : (p->type == 16) ? 2
                                     : (p->type == 19) ? 3   // UFO
                                     : (p->type == 26) ? 4   // wave
                                     : (p->type == 27) ? 5   // robot
                                     : (p->type == 33) ? 6   // spider
                                     : (p->type == 41) ? 7   // swing
                                                       : 0;
            const uint8_t wantMini = (p->type == 18) ? 1 : 0;
            const bool isGrav = isGravPortalType(p->type);
            const bool isSize = (p->type == 17 || p->type == 18);
            if (p->type == 23 || p->type == 24) {
                const uint8_t wantDual = (p->type == 23) ? 1 : 0;
                if (c.dual == wantDual) continue;
                if (!wantDual && g_p2Solo && g_p2SoloHalf) g_p2SoloFired = true;   // --p2solo
                if (wantDual) {
                    const bool bornUnlinked = dualUnlinkedAt((int)s.frame, (double)s.xAbs, (double)s.y);
                    c.y2 = yFree;
                    c.vy2 = bornUnlinked ? c.vy : -c.vy;
                    c.mode2 = c.mode;
                    c.mini2 = c.mini;
                    if (g_dualSlide) c.slideT2 = c.slideT;   // --dualslide: born with p1's arm
                    c.ceilT2 = 0;
                    c.ceilM42 = 0;
                    c.flip2 = bornUnlinked ? c.flip : (uint8_t)(c.flip ? 0 : 1);
                    c.grounded2 = 0;
                    c.ringHold2 = 0;
                    c.pressSpent2 = 0;
                    c.onSlope2 = 0;
                    c.slopeM2 = 0.f;
                    c.snapObj2 = nullptr;
                    c.snapDist2 = 0.f;
                    c.usedOrb2 = nullptr;
                    c.usedOrbOld2 = -1;
                    for (int i = 0; i < 3; ++i) c.usedOrbHist2[i] = -1;
                    c.spdFired2[0] = c.spdFired2[1] = -1;
                    c.spdMore2[0] = c.spdMore2[1] = -1;   // --spdsameonce
                    c.hitG2 = 0;   // --repela0c: a new body has not touched the ground
                    c.a1cLatch = (uint8_t)(c.a1cLatch & ~2u);   // --a1clatch: nor jumped
                    {  // --rot2: born with the first body's angle and spin
                        c.rot2 = c.rot;
                        c.rotStep2 = c.rotStep;
                        c.rotNeg2 = c.rotNeg;
                    }
                    c.holdDead2 = 0;
                    if (g_dualBornPress) {
                        c.pressSpent2 = c.pressSpent;
                        c.holdDead2 = c.holdDead;
                    }
                    { c.flipT2 = 0; c.modeT2 = 0; }
                    for (int i = 0; i < 3; ++i) c.portSeen2[i] = -1;
                    c.portSeen2[0] = -2;
                    for (int i = 0; i < 4; ++i) c.touchRing2[i] = -1;
                    c.touchRingT2 = 0;
                    for (int i = 0; i < 4; ++i) c.usedPad2[i] = nullptr;
                    { c.dashing2 = 0; c.dashSlope2 = 0.f; }   // --dualdash: a new body is not dashing
                    c.rHover2 = 0;   // --dualhover: nor hovering
                    c.dx2 = 0.f;   // --p2xoff: a new body is born at player 1's x
                }
                c.dual = wantDual;
                if (wantDual) {
                    if (g_bandHoldEnd && g_bandTrackEnd >= 0 && (long long)K.t > g_bandTrackEnd)
                        c.bandBranch |= kBandPastEnd;   // --bandholdend (as below)
                    if (g_dualZoomBand && g_staticCamCol && (c.mode == 0 || c.mode == 5))
                        c.bandAnim = bandAnimStep(bandAnimStart(true));
                    c.bandRefY = (float)portalBandY(p);
                    const FlyBand nb = bandFor(portalBandY(p), bandHeightDual(c.mode));
                    c.bandFloor = (float)nb.floorY;
                    c.bandCeil = (float)nb.ceilY;
                    if (g_shipCeilSet) c.bandCeil = (float)(g_shipCeil + kCubeHalf);
                    if (g_ufoCeil > 0.0) c.bandCeil = (float)g_ufoCeil;
                    if (g_flyFloor > 0.0) c.bandFloor = (float)g_flyFloor;
                    if (g_bandOwnTrack)   // --bandowntrack (bands.hpp kBandOwn)
                        c.bandBranch = (uint8_t)((c.bandBranch & ~kBandOwn)
                            | (bandTrackSawBand((long long)K.t, c.bandFloor, c.bandCeil) ? 0 : kBandOwn));
                }
                continue;
            }
            {
                const bool isMode = !isGrav && !isSize;
                const bool inDual = isMode && c.dual != 0;
                if (isMode && g_bandHoldEnd && g_bandTrackEnd >= 0
                    && (long long)K.t > g_bandTrackEnd)
                    c.bandBranch |= kBandPastEnd;
                if (isMode && g_freeModeCol)
                    c.bandBranch = (uint8_t)((c.bandBranch & ~kBandFree)
                                             | kBandFreeKnown
                                             | (p->freeMode ? kBandFree : 0));
                const int otherMode = (g_dualOtherMode >= 0) ? g_dualOtherMode : (int)s.mode2;
                const double H = inDual ? (g_dualBand ? std::max(bandHeightDual(wantMode),
                                                                  bandHeightDual(otherMode))
                                                      : bandHeightDual(wantMode))
                                        : bandHeightFor(p->type);
                const double refY = inDual ? (double)c.bandRefY : portalBandY(p);
                if (g_staticCamCol && isMode
                    && (H <= 0.0 || (g_freeModeCol && p->freeMode))) {
                    c.bandAnim = bandAnimStart(false);
                    if (c.bandBranch & 2) c.bandBranch &= (uint8_t)~1;
                    g_bandPortalWrote = true;   // --dualband (stepBoth)
                }
                if (H > 0.0 && !(g_freeModeCol && p->freeMode)) {
                    g_bandPortalWrote = true;   // --dualband (stepBoth)
                    if (g_staticCamCol) {
                        c.bandAnim = bandAnimStart(true);
                        c.bandBranch |= 3;
                    }
                    if (!inDual) c.bandRefY = (float)portalBandY(p);
                    const FlyBand nb = bandFor(refY, H);
                    c.bandFloor = (float)nb.floorY;
                    c.bandCeil = (float)nb.ceilY;
                    if (g_shipCeilSet) c.bandCeil = (float)(g_shipCeil + kCubeHalf);
                    if (g_ufoCeil > 0.0) c.bandCeil = (float)g_ufoCeil;
                    if (g_flyFloor > 0.0) c.bandFloor = (float)g_flyFloor;
                    if (g_bandOwnTrack)   // --bandowntrack (bands.hpp kBandOwn)
                        c.bandBranch = (uint8_t)((c.bandBranch & ~kBandOwn)
                            | (bandTrackSawBand((long long)K.t, c.bandFloor, c.bandCeil) ? 0 : kBandOwn));
                }
            }
            const uint8_t gdUpPortal =
                (isGrav && ballFlippedThisTick)
                    ? ((c.frame == 3) ? (uint8_t)!s.flip : (uint8_t)s.flip)
                    : gdUpOf(c);
            // --gravtoggle: a toggle portal asks for the gravity the body does not have.
            if (p->type == 42) wantFlip = (uint8_t)!gdUpPortal;
            const bool wasInBoxPrev =
                portalWasInsideAtPrev(*p, xPrev, (double)s.y, pHalfP, pRotHere);
            if (isGrav && c.dual && wasInBoxPrev
                && !(g_dualBornPortal && s.portSeen[0] == -2)) {
                if (g_slopeDbg)
                    std::printf("dualskip kind=wasinbox half=%d t=%lld uid=%d\n",
                                g_halfNow, (long long)K.t, p->uid);
                continue;
            }
            if (isGrav ? (gdUpPortal == wantFlip)
                : isSize ? (c.mini == wantMini)
                         : (c.mode == wantMode)) continue;
            if (g_portalPress && pressAction && !isGrav && !isSize && wantMode != c.mode
                && (wantMode == 0 || wantMode == 1 || wantMode == 2 || wantMode == 4
                    || (wantMode == 3 && c.mode == 0))) {
                c.flip = s.flip;
                VYSET(c.vy) = 0.f;
                c.grounded = 1;
                if (pressAction == 2) { c.rotStep = s.rotStep; c.rotNeg = s.rotNeg; }
                if (pressAction == 2) ballFlippedThisTick = false;
                c.pressSpent = s.pressSpent;
                portalPressRedo = true;
                a1cJumpLate = false;   // --a1clatch: redone below by the new mode, if at all
            }
            const bool halves = !isSize;
            double vAtPortal = (double)c.vy;
            if (isGrav && s.grounded && c.mode == 0) {
                const bool onFloor = !s.flip && s.frame == 0
                    && (double)s.y <= (kGroundY + pHalf) + 0.5;
                if (!onFloor)
                    vAtPortal = (s.flip ? 1.0 : -1.0)
                              * gStepMag(std::fabs(cubePhysFor(useDx).g), false, false);
                if (!onFloor && !s.flip && g_portalUnpin && pinnedOnBlock) releasePin(false);
            }
            const bool srcFly =
                ((c.mode == 1 || c.mode == 3 || c.mode == 4 || c.mode == 7)
                 && !(c.mode == 7 && s.onSlope));
            const bool dstFly =
                (wantMode == 1 || wantMode == 3 || wantMode == 4
                 || wantMode == 7);
            const int flyEnds = (srcFly ? 1 : 0) + (dstFly ? 1 : 0);
            const double portalScale =
                isGrav             ? kGravPortalScale
                : (flyEnds == 2)   ? 0.25
                : (flyEnds == 1)   ? kGravPortalScale
                                   : 1.0;
            if (g_slopeDbg)
                std::printf("porthalve t=%lld uid=%d type=%d halves=%d "
                            "isGrav=%d flyEnds=%d scale=%.4f vAt=%.4f "
                            "vy=%.4f -> %.4f\n",
                            (long long)K.t, p->uid, (int)p->type,
                            halves ? 1 : 0, isGrav ? 1 : 0, flyEnds,
                            portalScale, vAtPortal, (double)c.vy,
                            halves ? vAtPortal * portalScale : (double)c.vy);
            if (halves) VYSET(c.vy) = (float)(vAtPortal * portalScale);
            if (isGrav) {
                const uint8_t flipBefore = c.flip;
                c.flip = (c.frame == 3) ? (uint8_t)!wantFlip : wantFlip;
                if (c.flip != flipBefore && !p->oriented) gravFiredUid = p->uid;
                if (c.mode == 2 && c.flip != flipBefore) {
                    c.rotStep = (float)ballRotRate(c.mini != 0, true, useDx);
                    c.rotNeg = (uint8_t)!((c.flip != 0) ^ (c.rev != 0));
                    // --ballscaleair: ...after lowering +0x728 (before 0x39a409)
                    if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 1u);
                }
                if (linkFlip && c.flip != flipBefore) *linkFlip = true;
                const double bandCG = (double)s.bandCeil;
                const bool bandUG = bandCG < 1e8
                    && std::fabs(bandCG - std::round(bandCG / 30.0) * 30.0)
                           < 0.01;
                const double invCeilG = std::min(
                    playerCeilAt(x),
                    (c.mode == 6 && bandUG) ? bandCG : 1e9);
                if (flipBefore && !c.flip && s.grounded
                    && std::fabs((double)c.vy) < 1e-6
                    && (c.mode == 5 || c.mode == 6)
                    && !(std::fabs((double)s.y + pHalf - invCeilG) < 0.6)) {
                    const double gMag = gStepMag(
                        (c.mode == 6) ? -kBallG
                        : (((double)useDx > 1.78) ? 0.195 : 0.194),
                        c.mode == 6, c.mode == 5);
                    VYSET(c.vy) = (float)(0.5 * gMag);
                    YSET(c.y) = (float)((double)c.y + 0.225 * gMag);
                }
                if (flipBefore && !c.flip && s.grounded && c.mode == 0) {
                    const double gMagC = gStepMag(std::fabs(cubePhysFor(useDx).g), false, false);
                    if (std::fabs((double)c.vy - 0.5 * gMagC) < 0.002)
                        YSET(c.y) = (float)((double)c.y + 0.225 * gMagC);
                }
                c.grounded = 0;
            } else if (isSize) {
                const double oldHalf = c.mini ? kMiniHalf : kCubeHalf;
                const double newHalf = wantMini ? kMiniHalf : kCubeHalf;
                if (!sizeFiredThisTick) {   // --padsection
                    sizeFiredThisTick = true;
                    sizeFiredCx = p->cx;
                    sizeFiredCy = p->cy;
                    sizeFiredUid = p->uid;
                    sizePreMini = c.mini;
                }
                const uint8_t miniBeforeThis = c.mini;
                c.mini = wantMini;
                if (c.mode == 2 && miniBeforeThis != c.mini
                    && (std::fabs(c.rotStep
                                  - (float)ballRotRate(miniBeforeThis != 0, false, useDx)) < 1e-3f
                        || (g_ballScaleAir && (c.ball728 & 3u) != 1u
                            && std::fabs(c.rotStep
                                         - (float)ballRotRate(miniBeforeThis != 0, true, useDx))
                                   < 1e-3f))) {
                    c.rotStep = (float)ballRotRate(c.mini != 0, false, useDx);
                    c.rotNeg = (uint8_t)((c.flip != 0) ^ (c.rev != 0) ^ (c.frame != 0));
                    if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 2u);
                }
                if (!g_portOrder)
                    pHalfLive = (c.mode == 4 && c.mini)
                                    ? kWaveContactHalfMini
                                    : playerHalf(c.mode, c.mini != 0);
                const bool growOnGround = (c.mode == 0 || c.mode == 2)
                                          && !c.flip
                                          && std::fabs((double)c.y - oldHalf - kGroundY) < 0.01;
                // Grow-away (speed.hpp): a body moving away from its floor is not re-seated
                const bool awayNoLift = (double)c.vy * (c.flip ? -1.0 : 1.0) > 0.0;
                int growSupports = 0;
                if (newHalf > oldHalf && s.grounded) {
                    const double foot = (double)s.y + (c.flip ? oldHalf : -oldHalf);
                    for (const Obj* o : *K.near) {
                        if (o->type != 0 || o->dynObj) continue;
                        const double face = c.flip ? o->cy - o->hh : o->cy + o->hh;
                        if (std::fabs(face - foot) < 0.01
                            && std::fabs((double)x - o->cx) < o->hw + oldHalf)
                            ++growSupports;
                    }
                }
                const bool growLag = growSupports < 2;
                const bool growPress = pressAction && s.grounded
                                       && newHalf > oldHalf && (c.mode == 0 || c.mode == 2);
                if (growPress && c.mode == 0) {
                    if (!growLag)
                        YSET(c.y) = (float)((double)s.y
                                      + (c.flip ? -1.0 : 1.0) * (newHalf - oldHalf));
                    const double j = cubePhysFor(useDx).jump
                                     * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0);
                    VYSET(c.vy) = (float)(c.flip ? -j : j);
                } else if (growPress) {
                    const double bTap = ballFlipFor(useDx) * (c.mini ? kMiniImpulse : 1.0);
                    VYSET(c.vy) = (float)(-bTap * (c.flip ? -1.0 : 1.0));
                } else if (pressAction == 1 && s.grounded
                           && newHalf < oldHalf && c.mode == 0) {
                    const double j = cubePhysFor(useDx).jump * (kCubeJumpMini / kCubeJump);
                    VYSET(c.vy) = (float)(c.flip ? -j : j);
                } else if (s.grounded && c.grounded && newHalf > oldHalf && !growOnGround
                           && !awayNoLift && !growLag)
                    YSET(c.y) = (float)((double)c.y
                                  + (c.flip ? -1.0 : 1.0) * (newHalf - oldHalf));
            } else {
                const uint8_t oldMode = c.mode;
                c.mode = wantMode;
                if (!modeFiredThisTick && wantMode != oldMode) {   // --padafterportal
                    modeFiredThisTick = true;
                    modeFiredCx = p->cx;
                    modeFiredCy = p->cy;
                    modeFiredUid = p->uid;
                }
                if (g_wavePortalFree && wantMode == 4 && oldMode == 0 && s.grounded && c.grounded
                    && !s.dual && !teleportedThisTick) {
                    VYSET(c.vy) = (float)(0.5 * ((double)yFree - (double)s.y) / (kYScale * tScale));
                    YSET(c.y) = yFree;
                    c.grounded = 0;
                }
                if (wantMode == 2 && oldMode != 2) c.rotStep = 0.f;
                // --ballscaleair: ...and the same toggle, into the ball or out of it, lowers +0x728
                if (g_ballScaleAir && (wantMode == 2) != (oldMode == 2))
                    c.ball728 = (uint8_t)((c.ball728 & ~3u) | 1u);
                if (wantMode == 2 && (oldMode == 1 || oldMode == 3)
                    && c.onSlope && c.slopeUidNow >= 0 && c.slopeUidNow < p->uid)
                    c.grounded = 0;
                if (portalPressRedo) {
                    portalPressRedo = false;
                    // --a1clatch: the new mode's jump or tap is the jump block's, from pushButton
                    if (wantMode == 0 || wantMode == 2) a1cJumpLate = true;
                    if (wantMode == 0) {
                        const double j = cubePhysFor(useDx).jump
                                         * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0);
                        VYSET(c.vy) = (float)(c.flip ? -j : j);
                        c.grounded = 0;
                        c.pressSpent = 1;
                    } else if (wantMode == 2) {
                        c.flip = c.flip ? 0 : 1;
                        c.rotStep = (float)ballRotRate(c.mini != 0, true, useDx);
                        c.rotNeg = (uint8_t)!((c.flip != 0) ^ (c.rev != 0));
                        if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 1u);   // flipGravity
                        const double bTap = ballFlipFor(useDx) * (c.mini ? kMiniImpulse : 1.0);
                        VYSET(c.vy) = (float)(-bTap * (c.flip ? -1.0 : 1.0));
                        c.grounded = 0;
                        c.pressSpent = 1;
                        if (input) c.holdDead = 1;
                    } else if (wantMode == 3) {
                        impulsedThisTick = false;
                    }
                }
                const bool wroteOnEntry = (c.mode != 0 && c.mode != 2);
                if (wroteOnEntry) {
                    c.rot = 0.f;
                } else if (oldMode == 1 || oldMode == 3 || oldMode == 4) {
                    c.rot = 0.f;                      // their falling edge: 0
                } else if (oldMode == 2 || oldMode == 7) {
                    c.rot = c.flip ? 180.f : 0.f;     // ball / swing falling
                }
                if (g_rotLatch && c.mode == 0
                    && (oldMode == 1 || oldMode == 3 || oldMode == 4 || oldMode == 7)) {
                    flyEntryStake = true;
                    flyEntryFlip = c.flip;
                    flyEntryMini = c.mini;
                } else if (c.mode != 0) {
                    flyEntryStake = false;
                }
                {
                    if (c.mode == 0 && (oldMode == 5 || oldMode == 6 || (g_rotLatch && oldMode == 2)))
                        c.rotStep = -2.f;
                    else if (c.rotStep < 0.f) c.rotStep = 0.f;
                }
                pHalfLive = (c.mode == 4 && c.mini)
                                ? kWaveContactHalfMini
                                : playerHalf(c.mode, c.mini != 0);
                if (c.mode == 1) c.grounded = 0;
                if (oldMode == 7 && wantMode != 7 && s.onSlope) {
                    const double mTravP = (double)s.slopeM
                        * (((double)K.dxF < 0.0 || s.rev != 0) ? -1.0 : 1.0);
                    VYSET(c.vy) = (float)rawQ((mTravP >= 0.0 ? 1.0 : -1.0)   // speed.hpp
                                   * slopeExitVy(std::fabs((double)s.slopeM),
                                                 2, useDx, c.mini != 0)
                                   * slopeRampFactor((int)s.slopeT + 1));
                    c.grounded = 0;
                    c.onSlope = 0;
                    YSET(c.y) = yFreeBeforeSlope;
                }
                else if (oldMode != wantMode && s.ceilT > 0 && !s.onSlope
                         && !(ceilPressedNow && ceilPressUid >= 0 && p->uid > ceilPressUid))
                    YSET(c.y) = yFreeBeforeSlope;
                {
                    const double oh = playerHalf(oldMode, c.mini != 0);
                    const double nh = playerHalf(c.mode, c.mini != 0);
                    const double ohG = (oldMode == 4)
                        ? (c.mini ? kWaveClampMini : kWaveClamp) : oh;
                    if (c.grounded && nh > ohG)
                        YSET(c.y) = (float)((double)c.y
                                      + (c.flip ? -1.0 : 1.0) * (nh - ohG));
                    else if (!c.grounded && !c.flip && nh > oh && K.slopes) {
                        for (const Obj* sp : *K.slopes) {
                            if (sp->uid < p->uid) continue;
                            if (sp->slopeHazard) continue;
                            if (slopeIsCeiling(sp->slopeDir)) continue;
                            const double sx0 = sp->cx - sp->hw;
                            const double sx1 = sp->cx + sp->hw;
                            if (x < sx0 || x > sx1) continue;
                            const double sm =
                                (sp->sy1 - sp->sy0) / (sx1 - sx0);
                            const double line = sp->sy0 + sm * (x - sx0);
                            const double r = x + (sm > 0
                                ? slopeXOffset(sm, nh)
                                : -slopeXOffset(sm, nh));
                            const double seat =
                                sp->sy0
                                + sm * (std::min(std::max(r, sx0), sx1) - sx0)
                                + nh;
                            if ((double)c.y < line) continue;  // centre below the line
                            if ((double)c.y >= seat) continue; // not touching
                            if (g_slopeDbg)
                                std::printf("rampseat t=%lld uid=%d portal=%d "
                                            "y=%.4f seat=%.4f nh=%.2f oh=%.2f\n",
                                            (long long)K.t, sp->uid, p->uid,
                                            (double)c.y, seat, nh, oh);
                            YSET(c.y) = (float)seat;
                            VYSET(c.vy) = 0.f;
                            c.grounded = 1;
                            break;
                        }
                    }
                const bool flySlack = (c.mode == 1 || c.mode == 3
                                       || c.mode == 4 || c.mode == 7);
                const double seatSlack = flySlack ? kShipLandTol : kLandTol;
                if (!c.grounded && K.near
                    && (double)c.vy * (c.flip ? -1.0 : 1.0) <= 0.0) {
                    const double gs = c.flip ? -1.0 : 1.0;
                    const Obj* best = nullptr;
                    double bestFace = 0.0;
                    for (const Obj* o : *K.near) {
                        if (o->type != 0 || o->slope || o->oneway
                            || o->oriented)
                            continue;
                        if (std::fabs(x - o->cx) > o->hw + nh + kContactEps)
                            continue;
                        const double face =
                            c.flip ? (o->cy - o->hh) : (o->cy + o->hh);
                        const double yPrevSeat =
                            (teleportedThisTick) ? teleLandY : (double)s.y;
                        const double prevFoot =
                            (yPrevSeat - gs * nh - face) * gs;
                        const double curFoot =
                            ((double)c.y - gs * nh - face) * gs;
                        if (prevFoot < -seatSlack || curFoot > 0.0) continue;
                        if (!best || (face - bestFace) * gs > 0.0) {
                            best = o;
                            bestFace = face;
                        }
                    }
                    if (best) {
                        if (g_slopeDbg)
                            std::printf("portalseat t=%lld uid=%d portal=%d "
                                        "old=%d new=%d y=%.4f -> %.4f "
                                        "vy=%.4f gs=%.0f flip=%d\n",
                                        (long long)K.t, best->uid, p->uid,
                                        (int)oldMode, (int)c.mode,
                                        (double)c.y, bestFace + gs * nh,
                                        (double)c.vy, gs, (int)c.flip);
                        YSET(c.y) = (float)(bestFace + gs * nh);
                        VYSET(c.vy) = 0.f;
                        c.grounded = 1;
                    }
                    }
                }   // oh / nh scope
                if (impulsedThisTick && oldMode != c.mode) {
                    const double sgn = c.flip ? -1.0 : 1.0;
                    const double ms = c.mini ? kMiniImpulse : 1.0;
                    if ((c.mode == 0 || c.mode == 5) && !c.grounded) {
                    } else if (c.mode == 0 || c.mode == 5) {  // -> cube / robot: a jump
                        const CubePhys jph = cubePhysFor(useDx);
                        VYSET(c.vy) = (float)(jph.jump * (c.mini ? (kCubeJumpMini / kCubeJump) : 1.0)
                                       * (c.mode == 5 ? kRobotJumpScale : 1.0)
                                       * sgn);
                        c.grounded = 0;
                        // the hover budget belongs to the jump, not to the mode
                        if (c.mode == 5) c.rHover = (uint8_t)kRobotHoverTicks;
                    } else if (c.mode == 3) {   // -> UFO: a flap
                        const double fp =
                            (c.mini ? gdapprox::UfoParams::mini()
                                    : gdapprox::UfoParams::normal()).flapPostVy();
                        if ((double)c.vy * sgn < fp) VYSET(c.vy) = (float)(fp * sgn);
                        c.grounded = 0;
                    }
                }
                {
                    if (!impulsedThisTick && c.mode == 3 && input
                        && ((oldMode == 1
                             && (!s.action || !s.pressSpent))
                            || (oldMode == 4
                                && !s.pressSpent)
                            || (oldMode == 0 && !s.action)
                            || (g_ballPortalFlap && oldMode == 2 && !s.pressSpent)))
                        c.pFlap = 1;
                    if (c.mode == 6 && oldMode != 6 && input
                        && (oldMode == 0 || oldMode == 1 || oldMode == 3 || oldMode == 4)) {
                        const bool carriesGround = s.grounded && (oldMode == 0 || oldMode == 4);
                        if (!s.action && carriesGround) {
                            double tY = 0.0;
                            bool tHaz = false;
                            const double sh = playerHalf(6, c.mini != 0);
                            const bool tFound =
                                spiderTargetY(K, x, (double)c.y, c.flip != 0, c.mini != 0, sh, tY,
                                              c.frame == 0, &tHaz, (int)c.frame, c.rev != 0);
                            if (!tFound) DIE_AS(kDeathPrune, "spider/no-target", nullptr);
                            if (tHaz) DIE("spider/tp-hazard", nullptr);
                            YSET(c.y) = (float)tY;
                            VYSET(c.vy) = (float)(c.flip ? -1.0 : 1.0);
                            c.flip = c.flip ? 0 : 1;
                            c.grounded = 0;
                            c.snapObj = nullptr;
                            c.spiderJumpT = 0;
                            c.pSpiderTap = 0;
                        } else {
                            c.pSpiderTap = 1;
                        }
                    }
                }
            }
        }
    }
    };  // runPortalPass
    runPortalPass(0);                              // teleports only
    if (!teleportedThisTick) runPortalPass(1);     // no jump -> as before
    if (flyHazTeleHeld && !dead) {
        if (!teleportedThisTick) {
            DIE("fly/hazard", flyHazTeleHeld);
        } else {
            for (const Obj* o : *K.near) {
                if (dead) break;
                if (o->type == 0) continue;
                if (hazardHit(o, x, (double)c.y, pHalf, kHazMargin, sawMarginFor(c.mode), c.mode, c.mini)
                    && (!g_obbAll || !o->obbOk
                        || obbOverlap(*o, x, (double)c.y, pHalf, (double)c.rot)))
                    DIE("fly/hazard-tele", o);
            }
        }
    }
    if (!dead && s.frame == 0
        && (s.mode == 0 || s.mode == 2 || s.mode == 5 || s.mode == 6)
        && (c.mode == 1 || c.mode == 3 || c.mode == 4 || c.mode == 7)) {
        const double pH = playerHalf(c.mode, c.mini != 0);
        const double gs = c.flip ? -1.0 : 1.0;
        for (const Obj* o : *K.near) {
            if (o->type != 0 || o->slope || o->oneway || o->oriented) continue;
            if (std::fabs(x - o->cx) >= o->hw + pH) continue;
            const double head = c.flip ? (o->cy + o->hh) : (o->cy - o->hh);
            const double prevHeadP = ((double)s.y + gs * pH - head) * gs;
            const double newHeadP = ((double)c.y + gs * pH - head) * gs;
            if ((double)c.vy * gs >= 0.0 && prevHeadP <= kShipLandTol && newHeadP >= 0.0) {
                YSET(c.y) = (float)(head - gs * pH);
                VYSET(c.vy) = 0;
                CLAMP0O("portal/ceil", o);
                break;
            }
        }
    }
    if ((g_hazAfterPortal || hazOnceDeferred) && !dead && s.frame == 0 && c.mode != s.mode) {
        // --togglescale (speed.hpp): ...unless the mode's toggler ran too -- it applies the new size at once
        const uint8_t boxMini = (g_toggleScale && c.mini != s.mini
                                 && (toggleScalesNow(s.mode) || toggleScalesNow(c.mode))) ? c.mini : s.mini;
        const double pHn = playerHalf(c.mode, boxMini != 0);
        for (const Obj* o : *K.near) {
            if (dead) break;
            if (o->type == 0) continue;
            if (g_hazDbgUid >= 0 && o->uid == g_hazDbgUid)
                std::printf("hazdbg newbox t=%lld uid=%d x=%.4f y=%.4f pH=%.2f mode %d->%d rot=%.4f cx=%.3f cy=%.3f "
                            "bc=%.5f bs=%.5f bhw=%.3f bhh=%.3f hit=%d obb=%d\n",
                            (long long)K.t, o->uid, x, (double)c.y, pHn, (int)s.mode, (int)c.mode, (double)c.rot,
                            o->cx, o->cy, o->bc, o->bs, o->bhw, o->bhh,
                            hazardHit(o, x, (double)c.y, pHn, kHazMargin, sawMarginFor(c.mode), c.mode, boxMini)
                                ? 1 : 0,
                            (o->obbOk && obbOverlap(*o, x, (double)c.y, pHn, (double)c.rot)) ? 1 : 0);
            if (hazardHit(o, x, (double)c.y, pHn, kHazMargin, sawMarginFor(c.mode), c.mode, boxMini)
                && (!g_obbAll || !o->obbOk
                    || obbOverlap(*o, x, (double)c.y, pHn, (double)c.rot)))
                DIE("portal/hazard-newbox", o);
        }
    } else if (hazOnceDeferred && !dead && c.mode == s.mode) {
        for (const Obj* o : *K.near) {
            if (dead) break;
            if (o->type == 0) continue;
            if (hazardHit(o, x, (double)c.y, pHalf, kHazMargin, sawMarginFor(s.mode), s.mode, s.mini)
                && (!g_obbAll || !o->obbOk
                    || obbOverlap(*o, x, (double)c.y, pHalf, (double)s.rot)))
                DIE("portal/hazard-oldbox", o);
        }
    }
    if (hazOnceDeferred && g_slopeDbg)
        std::printf("hazonce t=%lld after the portal pass: mode %d -> %d, %s\n", (long long)K.t,
                    (int)s.mode, (int)c.mode, dead ? g_deadWhy : "alive");
    bool gravPadFlipped = false;
    const bool landedBeforePads = landedThisTick || pinnedOnBlock;
    const bool cubeLandedBeforePads = cubeLandedThisTick;
    const float yBeforePads = c.y;
    const uint8_t flipBeforePads = c.flip;
    // --padundoflip (speed.hpp): the same, with a ball's tap that a pad undoes taken back out
    uint8_t flipBeforePadsGd = flipBeforePads;
    bool padFiredNow = false;
    bool spiderPadFired = false;
    std::vector<const Obj*> padsByUid(K.pads->begin(), K.pads->end());
    std::stable_sort(padsByUid.begin(), padsByUid.end(),
                     [](const Obj* a, const Obj* b) { return a->uid < b->uid; });
    for (const Obj* pd : padsByUid) {
        if (spiderWarpedThisTick) break;
        double padY = (double)c.y;
        if (teleportedThisTick
            && !(g_tpOrder ? gdTeleAfter(pd->cx, pd->cy, pd->uid, teleCx, teleCy, teleUid, x, teleFromY)
                           : pd->uid > teleUid)) {
            if (!g_padAfterPortal || pd->type == 44 || s.frame != 0) continue;
            padY = (double)teleFromY;
        }
        double padHalf = pHalf;
        uint8_t padMini = c.mini;
        if (sizeFiredThisTick) {
            padMini = gdCollidesAfter(*pd, sizeFiredCx, sizeFiredCy, sizeFiredUid) ? c.mini
                                                                                  : sizePreMini;
            padHalf = playerHalf(s.mode, padMini != 0);
        }
        uint8_t padMode = s.mode;
        if (g_padAfterPortal && modeFiredThisTick && s.frame == 0
            && gdCollidesAfter(*pd, modeFiredCx, modeFiredCy, modeFiredUid)) {
            padMode = c.mode;
            padHalf = playerHalf(c.mode, padMini != 0);
        }
        if (padMode == 4 && padMini) padHalf = kWaveHazHalfMini;
        // --padoldbox (speed.hpp): the contact on a size portal's tick is the box from before it
        double padHitHalf = padHalf;
        if (g_padOldBox && sizeFiredThisTick)
            padHitHalf = (padMode == 4 && sizePreMini) ? kWaveHazHalfMini
                                                       : playerHalf(padMode, sizePreMini != 0);
        if (std::fabs(x - pd->cx) < pd->hw + pHalf + kPadReach + 30.0
            && std::fabs((double)c.y - pd->cy) < pd->hh + pHalf + 30.0)
            g_nearOrb = 1;
        if (g_padTouch) {
            if (std::fabs(x - pd->cx) > pd->hw + padHitHalf + kPadReach) continue;
            if (std::fabs(padY - pd->cy) > pd->hh + padHitHalf) continue;
        } else {
        if (std::fabs(x - pd->cx) >= pd->hw + padHitHalf + kPadReach) continue;
        if (std::fabs(padY - pd->cy) >= pd->hh + padHitHalf) continue;
        }
        double pRotPad = (double)s.rot;
        if (g_noSatRotRaw && s.mode == 0)
            pRotPad += (s.rotNeg ? 1.0 : -1.0) * (s.mini ? 2.25 : 1.7307692);
        if (pd->type == 8) {
            if (g_noPadPlayerRot) pRotPad = 0.0;
            else if (!padPlayerRotMode(padMode)) pRotPad = 0.0;
        }
        if (pd->oriented && !orientedHit(*pd, x, padY, padHitHalf, pRotPad))
            continue;
        if (pd->type == 10) {
            const bool upNow = ballFlippedThisTick ? (s.flip != 0)
                                                   : (c.flip != 0);
            if (upNow != objFacingDown(*pd)) continue;
        }
        if (pd->type == 44 && s.frame % 2 != 0) continue;
        if (g_spentPadHold && pd->spentRun) continue;   // --spentpadhold (frames.hpp)
        // the pad wins over the tap: undo the flip before applying it
        if (ballFlippedThisTick) {
            c.flip = s.flip;
            ballFlippedThisTick = false;
            if (g_padUndoFlip) flipBeforePadsGd = s.flip;
        }
        bool already = false;
        int slot = -1;
        for (int i = 0; i < 4; ++i) {
            if (c.usedPad[i] == pd) { already = true; break; }
            if (!c.usedPad[i] && slot < 0) slot = i;
        }
        if (already) continue;   // already used, still inside it
        if (g_padRefill && slot >= 0 && pd->type != 10 && pd->type != 44
            && std::fabs(xPrev - pd->cx) < pd->hw + padHalf + kPadReach
            && std::fabs((double)s.y - pd->cy) < pd->hh + padHalf
            && (!pd->oriented || orientedHit(*pd, xPrev, (double)s.y, padHalf, pRotPad))) {
            bool cluster = false;
            for (int i = 0; i < 4 && !cluster; ++i) {
                const Obj* up = c.usedPad[i];
                cluster = up && std::fabs(up->cx - pd->cx) <= up->hw + pd->hw + 2.0 * padHalf
                          && std::fabs(up->cy - pd->cy) <= up->hh + pd->hh + 2.0 * padHalf;
            }
            if (cluster) {
                c.usedPad[slot] = pd;
                continue;
            }
        }
        if (slot < 0) {
            const bool insideAtPrev =
                std::fabs(xPrev - pd->cx) < pd->hw + padHalf + kPadReach
                && std::fabs((double)s.y - pd->cy) < pd->hh + padHalf
                && (!pd->oriented || orientedHit(*pd, xPrev, (double)s.y, padHalf, pRotPad));
            if (insideAtPrev) {
                if (g_histStatOn) ++g_hist.padFull;   // --histstat (speed.hpp)
                continue;
            }
        } else {
            c.usedPad[slot] = pd;
        }
        padFiredNow = true;
        if (g_inPressProbe && pd->type != 44) g_probePadFired = true;   // --spiderpadfirst
        // --a1clatch: propellPlayer raises +0xa1c (0x39f86e). A spider pad never calls it.
        if (pd->type != 44) a1cLate = true;
        if (g_histStatOn) {
            int k = 0;
            for (int i = 0; i < 4; ++i) k += c.usedPad[i] != nullptr;
            g_hist.padMax = std::max(g_hist.padMax, k);
            if (std::find(g_histPads.begin(), g_histPads.end(), pd->uid) != g_histPads.end())
                ++g_hist.padRefire;
            else
                g_histPads.push_back(pd->uid);
        }
        if (pressAction) {
            c.pressSpent = s.pressSpent;
            pressAction = 0;
        }
        if (pd->rev) c.rev = c.rev ? 0 : 1;
        if (pd->type == 44) {
            const bool fdown = objFacingDown(*pd);
            if ((c.flip != 0) != fdown) c.flip = fdown ? 1 : 0;
            double padBandF = (double)c.bandFloor, padBandC = (double)c.bandCeil;
            if (s.frame == 0 && !(g_bandOwnTrack && (c.bandBranch & kBandOwn))) {
                BandKind bk = BandKind::Unknown;
                double f_ = padBandF, c_ = padBandC;
                long long bage = 0;
                if (bandTrackKindAt((long long)K.t + 1, bk, f_, c_, bage)
                    && bk == BandKind::Interval
                    && (double)c.y >= f_ - 1.0 && (double)c.y <= c_ + 1.0
                    && !bandTrackOtherRoute(c, (long long)K.t + 1, f_, c_, bage)) {
                    padBandF = f_;
                    padBandC = c_;
                }
            }
            const bool padBandWall = padBandF > kGroundY + 0.01 && padBandC < 1e8
                                     && padBandF < padBandC
                                     && bandIsWall(c.bandBranch, false);
            double tgt = 0.0;
            bool tgtHaz = false, tgtSolid = false;
            if (!spiderTargetY(K, x, (double)c.y, c.flip != 0, padMini != 0, padHalf, tgt, true,
                               &tgtHaz, (int)s.frame, c.rev != 0, padBandWall, padBandF, padBandC,
                               &tgtSolid, true)) {
                DIE("spiderpad/no-target", pd);   // (DIE flags the death; nothing to place)
                break;
            }
            // ...a hazard is landed on and kills there, as the tap's search does
            if (tgtHaz) DIE("spiderpad/tp-hazard", pd);
            const double gs = c.flip ? -1.0 : 1.0;
            // the landing before the pads belongs to the surface the body has left
            pinnedOnBlock = false;
            landedThisTick = false;
            YSET(c.y) = (float)tgt;
            yFree = c.y;
            c.flip = c.flip ? 0 : 1;
            if (tgtSolid && c.mode != 4) {
                VYSET(c.vy) = 0;
                c.grounded = 1;
                landedThisTick = true;
            } else {
                VYSET(c.vy) = (float)gs;
                c.grounded = 0;
            }
            c.spiderJumpT = 0;   // GD's +0x820 (State::spiderJumpT)
            c.snapObj = nullptr;
            c.snapDist = 0.f;
            c.onSlope = 0;
            c.slopeM = 0.f;
            if (c.mode == 0) c.rot = c.flip ? 180.f : 0.f;
            c.rHover = 0;
            spiderWarpedThisTick = true;
            spiderPadFired = true;
            break;   // the pads after it were judged at the old position
        }
        rotWrite = true;
        rotWriteFlip = c.flip;
        rotPadSpin = true;
        rotPadFlip = c.flip;
        if (pd->type == 10) {
            const bool padPost = c.mode == 2 || (g_bluePadPost && (c.mode == 6 || c.mode == 7));
            const double bv = (padPost ? kPadBlueBall : kPadBlueVy)
                              * (c.mini ? kMiniImpulse : 1.0);
            {
                c.flip = c.flip ? 0 : 1;
                gravPadFlipped = true;
            }
            VYSET(c.vy) = (float)(c.flip ? bv : -bv);
            if (g_dualPadFlip && linkFlip) *linkFlip = true;
        } else {
            const bool ballish = (c.mode == 2 || c.mode == 6);
            const double pBase = (pd->type == 9)  ? kPadPink
                               : (pd->type == 34) ? kPadRed
                                                  : kPadYellow;
            double pv = (ballish ? ((pd->type == 9)
                                          ? kPadPinkBall
                                          : pBase * kPadYellowBall / kPadYellow)
                                       : pBase)
                              * (padMini ? kMiniImpulse : 1.0);
            // --padtable (speed.hpp): GD's own strengths, per pad and mode.
            if (g_padTable) pv = padTableVy((int)pd->type, c.mode, padMini != 0);
            VYSET(c.vy) = (float)(c.flip ? -pv : pv);
            if (c.mode == 2) {
                c.rotStep = (float)ballRotRate(c.mini != 0, false, useDx);
                c.rotNeg = (uint8_t)((c.flip != 0) ^ (c.rev != 0) ^ (c.frame != 0));
                if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 2u);   // runBallRotation
            }
            if (pd->type == 34 && boostLatchMode(c.mode)) c.boost = 1;
            c.boost = (pd->type == 34) ? 1 : 0;
            c.onSlope = 0;
            c.slopeT = 0;
        }
        c.grounded = 0;
        c.rHover = 0;
        releasePin(true);
    }
    if (padFiredNow && !spiderPadFired && landedBeforePads && c.flip == flipBeforePadsGd
        && (double)c.vy * (c.flip ? -1.0 : 1.0) <= 0.0) {
        YSET(c.y) = yBeforePads;
        VYSET(c.vy) = 0;
        c.grounded = 1;
        landedThisTick = true;
        cubeLandedThisTick = cubeLandedBeforePads;
    }
    if (!dead && !spiderPadFired && s.frame == 0 && c.flip != flipBeforePadsGd
        && (c.mode == 0 || c.mode == 2 || c.mode == 5 || c.mode == 6)) {
        const double gs = c.flip ? -1.0 : 1.0;
        const double foot = (double)c.y - gs * pHalf;
        const double prevFoot = (double)s.y - gs * pHalf;
        if ((double)c.vy * gs <= 0.0) {
            for (const Obj* o : *K.near) {
                if (o->type != 0 || o->slope || o->oriented || o->dynObj) continue;
                if (std::fabs(x - o->cx) >= o->hw + pHalf) continue;
                const double face = o->cy + gs * o->hh;
                const double pen = (face - foot) * gs;
                if (pen <= 0.0) continue;   // not past the new floor's face
                if (pen > kLandTol && (face - prevFoot) * gs > kLandTol) continue;
                YSET(c.y) = (float)(face + gs * pHalf);
                VYSET(c.vy) = 0;
                c.grounded = 1;
                landedThisTick = true;
                CLAMP0O("pad/flipland", o);
                break;
            }
        }
    }
    if (g_flyPadHead && !dead && padFiredNow && !spiderPadFired && !gravPadFlipped && s.frame == 0
        && !modeFiredThisTick && !sizeFiredThisTick && (c.mode == 1 || c.mode == 3 || c.mode == 7)) {
        const double gs = c.flip ? -1.0 : 1.0;
        if ((double)c.vy * gs > 0.0) {
            const double head = (double)c.y + gs * pHalf;
            const double prevHead = (double)s.y + gs * pHalf;
            for (const Obj* o : *K.near) {
                if (o->type != 0 || o->slope || o->oriented || o->oneway || (o->dynObj && o->dcy != 0.0))
                    continue;
                if (std::fabs(x - o->cx) >= o->hw + pHalf) continue;
                const double face = o->cy - gs * o->hh;
                const double pen = (head - face) * gs;
                if (pen <= 0.0) continue;   // the head has not crossed this face
                if (pen > kShipLandTol && (prevHead - face) * gs > kShipLandTol) continue;
                YSET(c.y) = (float)(face - gs * pHalf);
                VYSET(c.vy) = 0;
                CLAMP0O("pad/flyhead", o);
                break;
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        const Obj* up = c.usedPad[i];
        if (up && std::fabs(x - up->cx) >= up->hw + pHalf + kPadReach)
            c.usedPad[i] = nullptr;
    }
    updateTouchRings(c, s, K, x, (double)c.y, pHalf);
    {
        const Obj* used = c.usedOrb;
        bool stillTouching = false;
        if (g_slopeDbg)
            std::printf("orbdbg t=%lld input=%d ringHold=%d used=%d\n",
                        (long long)K.t, (int)input, (int)s.ringHold,
                        used ? used->uid : -1);
        const bool ringFly = (c.mode == 1 || c.mode == 3
                              || c.mode == 4 || c.mode == 7);
        const bool ringGate = ringFly ? (input && !s.jumpBuf)
                                      : (input != 0 || s.jumpBuf);
        if (!input) {
            c.ringHold = 0;      // released: the next press may ring again
            c.pressSpent = 0;    // ...and GD's releaseButton clears +0x986 with it
        }
        if (ringGate && !s.pressSpent) {
            const Obj* pick = nullptr;
            int pickRank = 4;          // `pick`'s place in m_touchedRings
            for (const Obj* ob : *K.orbs) {
                if (spiderWarpedThisTick) break;
                // a ship's or UFO's press had its one chance at these a tick ago (the fly rings)
                if (!s.dual && (s.mode == 1 || s.mode == 3)
                    && (flyEarlyRingType(ob->type) || ob->type == 46))
                    continue;
                // the landing site follows uid order (measured, teleUid's decl.)
                if (teleportedThisTick                                   // --tporder
                    && !(g_tpOrder ? gdTeleAfter(ob->cx, ob->cy, ob->uid, teleCx, teleCy, teleUid, x,
                                                 teleFromY)
                                   : ob->uid > teleUid))
                    continue;
                //// the landing site counts from the next tick (type 28)
                const double odxNow = std::fabs(x - ob->cx);
                const double odyNow = std::fabs((double)c.y - ob->cy);
                const double odxPre = std::fabs(xPrev - ob->cx);
                const double odyPre = std::fabs((double)s.y - ob->cy);
                const bool nowOnly = ob->oriented;
                const bool rrgPre = g_ringRotGround && ob->oriented && c.mode != 4;
                const bool usePre = rrgPre
                                 || (!nowOnly
                                     && ((odxPre <= odxNow && odyPre <= odyNow)
                                         || (odyPre + odxPre < odyNow + odxNow)));
                const double odx = usePre ? odxPre : odxNow;
                const double ody = usePre ? odyPre : odyNow;
                if (odx < ob->hw + pHalf + 30.0 && ody < ob->hh + pHalf + 30.0)
                    g_nearOrb = 1;
                // --waveringmini (speed.hpp): the mini wave's object rect is 6 x 6, not 4 x 4
                const double ringRectHalf =
                    (g_waveRingMini && c.mode == 4 && c.mini) ? kWaveHazHalfMini : pHalf;
                // Ring-either (speed.hpp): either position in the box is a contact
                if (!nowOnly) {
                    if (!(odxPre < ob->hw + ringRectHalf && odyPre < ob->hh + ringRectHalf)
                        && !(odxNow < ob->hw + ringRectHalf && odyNow < ob->hh + ringRectHalf))
                        continue;
                } else if (odx >= ob->hw + ringRectHalf || ody >= ob->hh + ringRectHalf) continue;
                if (ob->oriented) {
                    const double relx = (usePre ? xPrev : x) - ob->cx;
                    const double rely = (usePre ? (double)s.y : (double)c.y)
                                        - ob->cy;
                    const double lx = relx * ob->rc + rely * ob->rs;
                    const double ly = -relx * ob->rs + rely * ob->rc;
                    const double ringHalf =
                        (g_waveRingBox && c.mode == 4 && !c.mini) ? kWaveRingHalf
                        : (g_waveRingMiniBox && c.mode == 4 && c.mini) ? kWaveRingHalfMini
                                                                         : pHalf;
                    const double pRotRing = usePre ? (double)s.rot : (double)c.rot;
                    if (g_ringPobb && c.mode != 4 && pRotRing != 0.0) {
                        if (!orientedHit(*ob, usePre ? xPrev : x,
                                         usePre ? (double)s.y : (double)c.y, ringHalf, pRotRing))
                            continue;
                    } else {
                    const double ph = ringHalf * (std::fabs(ob->rc)
                                                  + std::fabs(ob->rs));
                    if (g_ringRotGround && s.rot != 0.0f && c.mode != 4) {
                        if (!orientedHit(*ob, relx + ob->cx, rely + ob->cy, ringHalf,
                                         (double)s.rot))
                            continue;
                    } else if (std::fabs(lx) >= ob->ohw + ph
                               || std::fabs(ly) >= ob->ohh + ph)
                        continue;
                    }
                }
                if (ob->type == 43) {
                    double tgt = 0.0;
                    // --spiderorbface (constants.hpp): the search starts from the orb's facing
                    const bool sfl = (g_spiderOrbFace && c.frame == 0) ? objFacingDown(*ob)
                                                                        : (c.flip != 0);
                    if (!spiderTargetY(K, x, (double)c.y, sfl,
                                       c.mini != 0, pHalf, tgt, c.frame == 0,
                                       nullptr, (int)c.frame, c.rev != 0))
                        continue;
                }
                const bool reuse = g_multiOrb && ob->multiAct;
                if (ob == used && !reuse) { stillTouching = true; continue; }
                if (!reuse && ob->uid >= 0 && ob->uid == c.usedOrbOld) continue;   // fired before `used`
                if (!reuse && ringFiredBefore(c, ob->uid)) continue;
                // the first in m_touchedRings wins, then the lowest uid
                const int rk = touchRank(c, ob->uid);
                if (!pick || rk < pickRank || (rk == pickRank && ob->uid < pick->uid)) {
                    pick = ob;
                    pickRank = rk;
                }
            }
            uint8_t flipAtRing = c.flip;
            auto applyOrb = [&](const Obj* ob) {
                if (ob->type == 46 && pressAction && !portalPressRedo) tpOrbUndo = (uint8_t)pressAction;
                if (pressAction && !portalPressRedo) {
                    c.flip = s.flip;
                    if (pressAction == 2) {
                        c.rotStep = s.rotStep;
                        c.rotNeg = s.rotNeg;
                        ballFlippedThisTick = false;
                    }
                    pressAction = 0;
                    a1cJumpLate = false;   // --a1clatch: nor did its jump block
                }
                if (landTapRing && !portalPressRedo) {
                    c.flip = landTapFlip0;
                    ballFlippedThisTick = false;
                    landTapRing = false;
                }
                flipAtRing = c.flip;
                if (ob->rev) c.rev = c.rev ? 0 : 1;
                rotWrite = true;   // rings/orbs too (the rotWrite note)
                rotWriteFlip = c.flip;   // default: gravity from before the branch
                // ring impulses scale with the section's speed (see ringScaleFor)
                const double ms = (c.mini ? kMiniImpulse : 1.0) * ringScaleFor(useDx);
                if (ob->type == 46) {
                    tpOrbFired = ob;
                } else if (ob->type == 13) {
                    const double gpost = (c.mode == 6) ? kRingSpiderPost : 1.0;
                    const double gv = g_rawValues
                        ? rawQ(kCubeJump * 0.8 * ms) * 0.5
                              * ((c.mode == 2) ? kRingSpiderPost : gpost)
                        : ((c.mode == 2) ? kOrbGravityBall : kOrbGravity) * ms * gpost;
                    c.flip = c.flip ? 0 : 1;
                    VYSET(c.vy) = (float)(gv * (c.flip ? 1.0 : -1.0));
                } else if (ob->type == 37 || ob->type == 38) {
                    c.dashing = 1;
                    c.dashSlope = dashSlopeOf(ob, (int)c.frame);   // --dashclamp
                    // --wavedash (speed.hpp): the wave's sprite turns to the line on this tick
                    if (g_waveDash && c.mode == 4)
                        c.rot = (float)(-kRadToDeg * std::atan((double)c.dashSlope));
                    g_dashVy = (double)c.vy * (ob->type == 38 ? 0.5 : 1.0);
                    g_dashVySet = 1;
                    VYSET(c.vy) = 0.f;
                    c.grounded = 0;
                    if (ob->type == 38) c.flip = c.flip ? 0 : 1;
                } else if (ob->type == 32) {
                    const double dv = (c.mode == 6)   ? kRingDropSpider
                                      : (c.mode == 3) ? kRingDropUfo
                                      : (c.mode == 1 || c.mode == 4
                                         || c.mode == 7) ? kRingDropFly
                                                         : kRingDrop;
                    const bool dropNotFalling = [&] {
                        if (!(c.mode == 0 || c.mode == 2 || c.mode == 5
                                             || c.mode == 6))
                            return (double)c.vy * (c.flip ? -1.0 : 1.0) >= 2.0;
                        const double g2 = gdapprox::accelSwitchVyForSpeed(speedMulForDx(useDx));
                        const bool fixedArm = g_fixGravityBug != 0 || c.frame != 0;
                        const double v = (double)c.vy;
                        const bool falling = !c.flip ? (v < g2) : fixedArm ? (v > -g2) : (v > g2);
                        return !falling;
                    }();
                    if (dropNotFalling)
                        c.pNoTerm = 1;
                    VYSET(c.vy) = (float)(c.flip ? dv : -dv);
                    if (!g_dropGround) c.grounded = 0;
                    else if (cubeGroundJumpNow) c.grounded = 1;
                    c.boost = 1;
                } else if (ob->type == 43) {
                    double tgt = 0.0;
                    const bool sfl = (g_spiderOrbFace && c.frame == 0) ? objFacingDown(*ob)
                                                                        : (c.flip != 0);
                    const double gs = sfl ? -1.0 : 1.0;
                    // the candidate loop already refused a target-less one
                    if (spiderTargetY(K, x, (double)c.y, sfl,
                                      c.mini != 0, pHalf, tgt, c.frame == 0,
                                      nullptr, (int)c.frame, c.rev != 0)) {
                        YSET(c.y) = (float)tgt;
                        c.flip = sfl ? 0 : 1;
                        VYSET(c.vy) = (float)gs;
                        c.spiderJumpT = 0;   // GD's +0x820 (State::spiderJumpT)
                        if (g_touchCensus) g_tcBranch |= 2;
                        // whatever held us up is a level away now
                        c.snapObj = nullptr;
                        c.snapDist = 0.f;
                        c.onSlope = 0;
                        c.slopeM = 0.f;
                    }
                } else if (ob->type == 35 || ob->type == 29) {
                    const bool green = (ob->type == 29);
                    double r;
                    if (green) {
                        r = (c.mode == 1) ? kRingGreenShip : kRingGreen;
                    } else {
                        switch (c.mode) {
                            case 1: r = c.mini ? kRingRedShipMini : kRingRedShip; break;
                            case 3: r = c.mini ? kRingRedUfoMini : kRingRedUfo; break;
                            case 2: r = kRingRedBall; break;
                            case 5: r = kRingRedRobot; break;
                            case 6: r = kRingRedSpider; break;
                            default: r = kRingRedCube; break;
                        }
                    }
                    const double post = (c.mode == 2)   ? kRingBallPost
                                        : (c.mode == 6) ? kRingSpiderPost
                                        : (c.mode == 7) ? kRingSwingPost
                                                        : 1.0;
                    if (green) { c.flip = c.flip ? 0 : 1; rotWriteFlip = c.flip; }
                    const double ov = g_rawValues ? rawQ(kOrbYellow * r * ms) * post
                                                  : kOrbYellow * r * post * ms;
                    VYSET(c.vy) = (float)(c.flip ? -ov : ov);
                    if (!green && true) c.boost = 1;
                } else {
                    const bool pink = (ob->type == 12);
                    const double post =
                        (c.mode == 6) ? kRingSpiderPost
                        : (g_orbSwing && c.mode == 7) ? kRingSwingPost
                                                      : 1.0;
                    const double robotR =
                        (!pink && c.mode == 5) ? kRingRobotDefault : 1.0;
                    double ov =
                        ((c.mode == 2) ? (pink ? kOrbPinkBall : kOrbYellowBall)
                         : (pink && (c.mode == 1 || c.mode == 3))
                             ? kOrbYellow * pinkRingRatio(c.mode)
                             : (pink ? kOrbPink : kOrbYellow))
                        * ms * post * robotR;
                    if (g_rawValues) {
                        const double ratio = pink ? pinkRingRatio(c.mode) : 1.0;
                        ov = rawQ(kCubeJump * ratio * ms * robotR)
                             * ((c.mode == 2) ? kRingSpiderPost : 1.0) * post;
                    }
                    VYSET(c.vy) = (float)(c.flip ? -ov : ov);
                }
                const bool tpOrb = (ob->type == 46);
                if (!tpOrb && !(g_dropGround && ob->type == 32)) c.grounded = 0;   // --dropground (above)
                if (!tpOrb) c.rHover = 0;
                if (ob->type != 13 && !tpOrb && !(input && !s.jumpBuf))
                    releasePin(true);
                if (ob->type != 32 && ob->type != 37 && ob->type != 38 && ob->type != 43 && !tpOrb)
                    a1cLate = true;
                noteRingFired(c, ob);
                if (input) {
                    c.ringHold = 1;
                    c.pressSpent = 1;   // GD's ringJump spends the press (see the field)
                    if (g_spiderSpend) c.pSpiderTap = 0;   // --spiderspend: and m_jumpBuffered
                    if (g_ringSpendFlap) c.pFlap = 0;
                }
                if (c.mode == 2 && !tpOrb) {
                    if (!ringStakedNow) {
                        ringStakedNow = true;
                        preRingStep = c.rotStep;
                        preRingNeg = c.rotNeg;
                    }
                    c.rotStep = (float)ballRotRate(c.mini != 0, true, useDx);
                    c.rotNeg = (uint8_t)!((c.flip != 0) ^ (c.rev != 0));
                }
                if (!tpOrb && !(g_dropGround && ob->type == 32 && c.mode != 2 && c.mode != 7))
                    c.jumpBuf = 0;
                if (input
                    && ((flyEarlyRingType(ob->type) && ob->type != 32
                         && (c.mode == 2 || c.mode == 6 || c.mode == 7))
                        || (ob->type == 32 && (c.mode == 2 || c.mode == 7))
                        || ob->type == 43))   // the spider orb runs spiderTestJump
                    c.holdDead = 1;
                used = ob;
                stillTouching = true;
            };
            const uint8_t flipBeforeOrb = c.flip;
            if (earlyRingTook) pick = earlyRingTook;
            if (pick) applyOrb(pick);
            if (g_ballScaleAir && pick && c.mode == 2 && c.flip != flipBeforeOrb)
                c.ball728 = (uint8_t)((c.ball728 & ~3u) | 1u);
            if (pick && t_followHalf2 && input && !s.jumpBuf) t_p2RingFresh = pick->type;   // --rtlp2ring
            const uint8_t flipRef = g_ringTapLink ? flipAtRing : flipBeforeOrb;
            if (linkFlip && pick && c.flip != flipRef) {
                if (g_ringTapLink && !t_followHalf2 && input && !s.jumpBuf) t_ringLinkLate = 1;
                else *linkFlip = true;
            }
        }
        if (teleportedThisTick) runPortalPass(1);
        (void)stillTouching;
    }
    if (c.mode == 0) {
        c.rotNeg = s.rotNeg;
        const uint8_t rotSignNow = s.rotNeg;
        if (rotWrite) c.rotNeg = rotWriteFlip;
        else if (s.grounded && !c.grounded) c.rotNeg = c.flip;
        if (flyEntryStake && !rotWrite) c.rotNeg = flyEntryFlip;
        bool boostSpin = false;
        if (rotBoostNow) {
            c.rotNeg = c.flip ? 0 : 1;
            c.rotBoost = 1;
            boostSpin = true;
        } else if (rotWrite || c.grounded || c.onSlope || (s.grounded && !c.grounded)
                   || s.mode != 0) {
            c.rotBoost = 0;
        }
        const bool relSpin = rotReleaseNow && !boostSpin && !rotPadSpin && !c.grounded;
        const bool rampPress = s.onSlope && !c.onSlope && input && !s.action
                               && !rotPadSpin && !boostSpin && !relSpin;
        const bool spinNow =
            rotPadSpin || boostSpin || relSpin
            || (!c.grounded && !s.grounded && !cubeLandedThisTick);
        bool entryIdle = false, fallStake = false;
        if (c.rotStep < 0.f) {
            if (rotWrite || rotPadSpin || boostSpin || relSpin || c.grounded || s.grounded
                || cubeLandedThisTick || c.onSlope || rampPress) {
                c.rotStep = 0.f;
            } else if (c.rotStep < -1.5f) {
                c.rotStep = -1.f;
                entryIdle = true;
            } else if (c.flip ? c.vy > 0.25f : c.vy < -0.25f) {
                c.rotStep = 0.f;
                c.rotNeg = c.flip;
                fallStake = true;
            } else {
                entryIdle = true;
            }
        }
        const uint8_t spinSign =
            rotPadSpin ? rotPadFlip
                       : (boostSpin || relSpin || fallStake) ? c.rotNeg
                       : flyEntryStake ? flyEntryFlip : rotSignNow;
        bool rateMini = c.mini != 0;
        if (g_rotLatch && c.rotStep >= 0.f) {
            float latch = c.rotStep;
            if (flyEntryStake)
                latch = flyEntryMini ? 2.f : 1.f;
            else if (fallStake)   // updateJump's stake, before this tick's portals (lv20 t=10,699: +2.25)
                latch = s.mini ? 2.f : 1.f;
            else if (rotWrite || rotPadSpin || boostSpin || relSpin || c.grounded
                     || s.grounded || cubeLandedThisTick || c.onSlope || rampPress || s.mode != 0)
                latch = 0.f;   // staked again at this tick's size (or on the ground, which eases)
            else if (latch == 0.f && s.mini != c.mini)
                latch = s.mini ? 2.f : 1.f;   // a size portal: the old stake holds
            if (latch > 0.f) rateMini = latch > 1.5f;
            c.rotStep = (latch > 0.f && rateMini != (c.mini != 0)) ? latch : 0.f;
        }
        if (c.frame == 0 && !rotPadSpin && !boostSpin && !relSpin
            && (c.onSlope || rampPress)) {
            const double rampM = c.onSlope ? (double)c.slopeM : (double)s.slopeM;
            const double ang = -std::atan(rampM) * (180.0 / 3.14159265358979);
            const double w = 0.04375 * speedMulForDx(useDx);
            for (int k = 0; k < 2; ++k) {
                const float ref = (float)((int)c.rot % 360);
                float lo = (float)ang, hi = (float)ang;
                if (hi <= ref) {
                    for (; hi < ref; hi += 90.0f) lo = hi;
                } else {
                    do { lo = hi; hi = lo - 90.0f; } while (ref < hi);
                }
                const double tgt = (std::fabs(ref - lo) <= std::fabs(ref - hi)) ? lo : hi;
                // the half-angle slerp: the shortest arc, then 2 * atan2 of the half angle
                const double hd = std::remainder((tgt - (double)c.rot) * 0.5, 180.0);
                c.rot = (float)(2.0 * std::remainder((double)c.rot * 0.5 + w * hd, 360.0));
            }
        } else if (entryIdle) {
            // --cubeentryspin: no spin staked yet, so the angle holds
        } else if (spinNow && !c.grounded) {
            const double rate = ((c.rotBoost && !rotPadSpin)
                                     ? (rateMini ? 1.125 : 0.8653846)
                                     : (rateMini ? 2.25 : 1.7307692))
                                * (g_warpRot ? tScale : 1.0);
            c.rot = (float)((double)c.rot + (spinSign ? -rate : rate));
        } else {
            const double tgt = std::round((double)c.rot / 90.0) * 90.0;
            const double rate = 0.13125 * speedMulForDx(useDx);
            c.rot = (float)((double)c.rot + rate * (tgt - (double)c.rot));
        }
    } else if (c.mode == 1) {
        if (c.onSlope && c.frame == 0) {
            const double ang = -std::atan((double)c.slopeM) * (180.0 / 3.14159265358979);
            const double w = 0.021875 * speedMulForDx(useDx);
            for (int k = 0; k < 2; ++k) {
                const double hd = std::remainder((ang - (double)c.rot) * 0.5, 180.0);
                c.rot = (float)(2.0 * std::remainder((double)c.rot * 0.5 + w * hd, 360.0));
            }
        } else {
            const double ddx = x - xPrev;
            const double ddy = (double)c.y - (double)s.y;
            if (ddx != 0.0 || ddy != 0.0) {
                const double tgt =
                    -std::atan2(ddy, ddx) * 180.0 / 3.14159265358979;
                c.rot = (float)((double)c.rot + 0.0375 * (tgt - (double)c.rot));
            }
        }
    } else if (c.mode == 3) {
        const double ddx = x - xPrev;
        const double ddy = (double)c.y - (double)s.y;
        if (ddx != 0.0 || ddy != 0.0) {
            const double raw = std::atan2(-ddy, ddx);
            double m = -0.4 * raw;
            if (c.rev) m = -m;
            const bool gdUp = gdUpOf(c) != 0;
            m = ((c.rev != 0) == gdUp) ? std::max(-0.1, m) : std::min(0.1, m);
            const double tgt = m * 57.29578;
            c.rot = (float)((double)c.rot
                            + 0.0175 * std::remainder(tgt - (double)c.rot, 360.0));
        }
    } else if (c.mode == 2) {
        if (c.grounded) {
            c.rotStep = (float)ballRotRate(c.mini != 0, false, useDx);
            c.rotNeg = (uint8_t)((c.flip != 0) ^ (c.rev != 0)
                                 ^ (c.frame != 0));
            if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 2u);   // runBallRotation
        }
        const bool ringLag = ringStakedNow && !c.grounded;
        const bool tapLag = g_ballTapLag && !ringLag && ballFlippedThisTick
                            && !c.grounded && s.mode == 2;
        const float stepNow = ringLag ? preRingStep : tapLag ? s.rotStep : c.rotStep;
        const uint8_t negNow = ringLag ? preRingNeg : tapLag ? s.rotNeg : c.rotNeg;
        if (stepNow != 0.f)
            c.rot = (float)((double)c.rot
                            + (negNow ? -1.0 : 1.0) * (double)stepNow);
    }
    const bool tapRide = ballFlippedThisTick && !c.grounded;
    const bool jumpRide = impulsedThisTick && !c.grounded && c.flip == s.flip;
    if (rideOn && s.grounded && c.y == s.y && !s.onSlope && !c.onSlope
        && (tapRide || jumpRide || (c.grounded && c.flip == s.flip))) {
        YSET(c.y) = (float)(rideFace + (s.flip ? -1.0 : 1.0) * pHalf);
        (void)rideDy;
    }
    if (c.mode == 0 && (c.grounded || cubeContact || cubeLandedThisTick)
        && !s.onSlope && !c.onSlope) {
        const double gs = c.flip ? -1.0 : 1.0;
        const double foot = (double)c.y - gs * pHalf;
        const double blockLen = gs * 30.0;   // GD: flipMod() * 30
        const StairParams sp =
            stairParamsFor((double)useDx, c.mini ? kMiniScale : 1.0);
        const double xBase = x;
        double xs = xBase;
        auto snapSec = [](double v) { return (long long)std::floor(v / 100.0); };
        auto snapBefore = [&](const Obj* a, const Obj* b) {
            const long long ax = snapSec(a->cx), bx = snapSec(b->cx);
            if (ax != bx) return ax > bx;
            const long long ay = snapSec(a->cy), by = snapSec(b->cy);
            if (ay != by) return ay > by;
            return a->uid > b->uid;
        };
        const Obj* touch[8];
        int nTouch = 0;
        for (const Obj* o : *K.near) {
            if (o->type != 0) continue;
            if (std::fabs(xBase - o->cx) > o->hw + pHalf + kContactEps) continue;
            const double face = c.flip ? (o->cy - o->hh) : (o->cy + o->hh);
            if (std::fabs(foot - face) > 0.6) continue;
            if (nTouch == 8) break;
            int j = nTouch++;
            for (; j > 0 && snapBefore(o, touch[j - 1]); --j) touch[j] = touch[j - 1];
            touch[j] = o;
        }
        for (int ti = 0; ti < nTouch; ++ti) {
            const Obj* o = touch[ti];
            const double xIn = xs;
            const double sdIn = (double)c.snapDist;
            const Obj* prevObj = c.snapObj;
            const bool onewayPair =
                c.snapObj != nullptr && c.snapObj->oneway;
            if (c.snapObj != nullptr && c.snapObj != o && !onewayPair) {
                const double dx = o->cx - c.snapObj->cx;
                const double dy = o->cy - c.snapObj->cy;
                if ((std::fabs(dx - sp.little) <= sp.threshold
                     && std::fabs(dy - blockLen) <= sp.threshold)
                    || (std::fabs(dx - sp.down) <= sp.threshold
                        && std::fabs(dy + blockLen) <= sp.threshold)
                    || (std::fabs(dx - sp.big) <= sp.threshold
                        && std::fabs(dy - 2 * blockLen) <= sp.threshold)) {
                    double d = (o->cx + (double)c.snapDist) - xs;
                    d = std::max(-sp.threshold, std::min(sp.threshold, d));
                    c.xAbs = (float)(xs + d);
                    xs = (double)c.xAbs;
                }
            }
            c.snapObj = o;
            c.snapDist = (float)(xs - o->cx);
            if (g_snapOut) {
                char b[256];
                snprintf(b, sizeof(b),
                    "snap: t=%lld y=%.3f prev=(%.2f,%.2f) obj=(%.2f,%.2f) "
                    "dx=%.2f dy=%.2f x=%.4f->%.4f d=%+.4f sd=%.4f->%.4f",
                    (long long)K.t, (double)c.y,
                    prevObj ? prevObj->cx : 0.0, prevObj ? prevObj->cy : 0.0,
                    o->cx, o->cy,
                    prevObj ? o->cx - prevObj->cx : 0.0,
                    prevObj ? o->cy - prevObj->cy : 0.0,
                    xIn, xs, xs - xIn, sdIn, (double)c.snapDist);
                *g_snapOut << b << "\n";
            }
        }
    }
    if (g_spdDbg)
        std::printf("spddbg t=%lld x=%.3f dx=%.5f window=%zu\n",
                    (long long)K.t, x, (double)c.dx,
                    K.speeds ? K.speeds->size() : (size_t)0);
    if (K.speeds) {
        for (const Obj* sp : *K.speeds) {
            const double pHalfSp = pHalfPortal;
            if (g_spdDbg)
                std::printf("spddbg t=%lld x=%.3f y=%.3f uid=%d id=%d "
                            "cx=%.2f cy=%.2f hw=%.2f hh=%.2f ori=%d obb=%d "
                            "ohw=%.3f ohh=%.3f rc=%.5f rs=%.5f "
                            "pHalf=%.2f |dx|=%.2f need<%.2f g1=%s\n",
                            (long long)K.t, x, (double)c.y, (int)sp->uid,
                            (int)sp->id, sp->cx, sp->cy, sp->hw, sp->hh,
                            (int)sp->oriented, (int)sp->obbOk,
                            sp->ohw, sp->ohh, sp->rc, sp->rs, pHalfSp,
                            std::fabs(x - sp->cx), sp->hw + pHalfSp,
                            (std::fabs(x - sp->cx) >= sp->hw + pHalfSp) ? "REJ"
                                                                        : "pass");
            if (std::fabs(x - sp->cx) >= sp->hw + pHalfSp) continue;
            if (sp->uid >= 0
                && (s.spdFired[0] == sp->uid || s.spdFired[1] == sp->uid
                    // --spdsameonce: the memory is four deep (State::spdMore)
                    || (g_spdSameOnce && (s.spdMore[0] == sp->uid || s.spdMore[1] == sp->uid))))
                continue;
            // --tporder: past a teleport, whether the pass reaches this one after it (gdTeleAfter).
            const bool teleAfterSp = teleportedThisTick
                && (g_tpOrder ? gdTeleAfter(sp->cx, sp->cy, sp->uid, teleCx, teleCy, teleUid, x, teleFromY)
                              : sp->uid > teleUid);
            if (g_tpOrder && teleportedThisTick && sp->uid > teleUid && !teleAfterSp)
                continue;
            const double ySp = teleAfterSp
                                   ? (double)c.y
                               : (seatUidSp >= 0 && sp->uid > seatUidSp)
                                   ? ySeatSp
                                   : yColl;
            if (sp->oriented) {
                double pRotSp = (double)s.rot;
                if (g_noSatRotRaw && s.mode == 0)
                    pRotSp += (s.rotNeg ? 1.0 : -1.0)
                              * (s.mini ? 2.25 : 1.7307692);
                const bool hit = orientedHit(*sp, x, (double)c.y, pHalfSp,
                                             pRotSp);
                if (g_spdDbg)
                    std::printf("spddbg   g2 uid=%d pRot=%.3f hit=%d%s\n",
                                (int)sp->uid, pRotSp, (int)hit,
                                hit ? "" : "  <- REJECTED HERE");
                if (!hit) continue;
            }
            const double gapY =
                std::fabs(ySp - sp->cy) - (sp->hh + pHalfSp);
            if (g_spdDbg)
                std::printf("spddbg   g3 uid=%d ySp=%.3f cy=%.2f hh+pHalf=%.2f "
                            "gapY=%.3f %s\n",
                            (int)sp->uid, ySp, sp->cy, sp->hh + pHalf, gapY,
                            gapY >= 0.0 ? "<- REJECTED HERE"
                                        : "-> FIRE (dx from the next tick)");
            if (gapY >= 0.0) {
                if (g_speedDodgeMin > 0.0 && gapY < g_speedDodgeMin
                    && std::fabs(c.dx - dxForSpeedId(sp->id)) > 1e-6)
                    DIE_AS(kDeathRule, "speed/hairline-dodge", sp);
                if (!dead && g_glitchPortal > 0.0 && gapY < g_glitchPortal
                    && std::fabs(c.dx - dxForSpeedId(sp->id)) > 1e-6
                    && s.frame == c.frame
                    && !(std::fabs(ySp - sp->cy) < std::fabs((double)s.y - sp->cy)))
                    DIE_AS(kDeathRule, "glitch/speed-dodge", sp);
                continue;
            }
            c.dx = dxForSpeedId(sp->id);
            if (c.mode == 2) {
                c.rotStep = (float)ballRotRate(c.mini != 0, false, c.dx);
                c.rotNeg = (uint8_t)((c.flip != 0) ^ (c.rev != 0) ^ (c.frame != 0));
                if (g_ballScaleAir) c.ball728 = (uint8_t)((c.ball728 & ~3u) | 2u);   // updateTimeMod
            }
            // --spdonce: marked whether or not the speed changed (GD marks on activation).
            if (sp->uid >= 0 && c.spdFired[0] != sp->uid) {
                if (g_spdSameOnce) {
                    int32_t* m[4] = {&c.spdFired[0], &c.spdFired[1], &c.spdMore[0], &c.spdMore[1]};
                    const auto insideNow = [&](int uid) {
                        for (const Obj* o : *K.speeds)
                            if (o->uid == uid)
                                return !o->oriented && std::fabs(x - o->cx) < o->hw + pHalfSp
                                       && std::fabs(ySp - o->cy) < o->hh + pHalfSp;
                        return false;
                    };
                    int drop = -1;
                    for (int k = 0; k < 4 && drop < 0; ++k)
                        if (*m[k] == sp->uid || *m[k] < 0) drop = k;
                    for (int k = 3; k >= 0 && drop < 0; --k)
                        if (!insideNow(*m[k])) drop = k;
                    if (drop < 0) drop = 3;
                    for (int k = drop; k > 0; --k) *m[k] = *m[k - 1];
                    *m[0] = sp->uid;
                } else {
                    c.spdFired[1] = c.spdFired[0];
                    c.spdFired[0] = sp->uid;
                }
            }
        }
    }
    if (rodeFlipped && c.onSlope && !c.flip && c.flip != s.flip) {
        VYSET(c.vy) = 0;
        c.grounded = 1;
    }
    if (ceilPressedNow) {
        c.ceilT = (uint8_t)std::min<int>((int)s.ceilT + 1, 200);
        if (s.onSlope && s.ceilT == 0)
            c.ceilT = (uint8_t)std::min<int>(
                std::max<int>((int)c.ceilT, (int)s.slopeT + 1), 200);
        c.ceilMode = ceilPressMode;
        c.ceilM4 = (uint8_t)std::min<int>(
            255, (int)std::lround(std::fabs(ceilPressM) * 4.0));
    } else if (s.ceilT > 0) {
        if (c.grounded && !s.grounded) {
        } else {
            // --ceilreleasemode: gate and value on the mode of the press.
            const uint8_t relMode = s.ceilMode;
            if ((relMode == 1 || relMode == 3 || relMode == 6 || relMode == 7 || relMode == 2)
                && !(a1cNow && (c.mode == 0 || c.mode == 5 || c.mode == 6))) {
                const double exU = rawQ(slopeExitVy((double)s.ceilM4 / 4.0, relMode,   // speed.hpp
                                                    useDx, c.mini != 0)
                                        * slopeRampFactor((int)s.ceilT));
                if ((double)c.vy > -exU) {
                    VYSET(c.vy) = (float)-exU;
                    CLAMP0("ceil/release");
                    a1cLate = true;   // --a1clatch: boostPlayer (0x39feee)
                    c.boost = 1;
                }
            }
            c.ceilT = 0;
            c.ceilM4 = 0;
        }
    }
    if (s.pExitVy != 0.f) {
        VYSET(c.vy) = s.pExitVy;
        c.grounded = 0;
    }
    c.flipT = (gdUpOf(c) != gdUpOf(s))
                  ? 0
                  : (uint8_t)std::min<int>(kFlipGraceTicks, (int)s.flipT + 1);
    // --cubeceilgrace: the mode-switch clock, kept like flipT above.
    c.modeT = (c.mode != s.mode)
                  ? 0
                  : (uint8_t)std::min<int>(kFlipGraceTicks, (int)s.modeT + 1);
    const double crushHalf = (c.mode == 4) ? kWaveKillHalf
                           : (g_spiderInner && c.mode == 6 && !c.mini) ? kSpiderInner
                                                                       : kCrushHalf;
    if (!dead && s.frame == 0) {
        const double cyF = (double)c.y;
        for (const Obj* o : *K.near)
            if (o->type == 0 && !o->slope && !o->oneway && !o->oriented
                && std::fabs(x - o->cx) < o->hw + crushHalf + 1e-6
                && std::fabs(cyF - o->cy) < o->hh + crushHalf + 1e-6
                && !footFaceContact(*o, c, pHalf)) {   // --faceclass (speed.hpp)
                if (flipGraceSeat(*o, c, s, ballFlippedThisTick && input && !s.action, pHalf)) {   // --flipgrace (speed.hpp)
                    seatOnNearFace(*o, c, pHalf);
                    CLAMP0O("crush/flipgrace", o);
                    continue;
                }
                if (g_spiderCrushGrace && c.mode == 6 && c.frame == 0
                    && c.spiderJumpT < spiderJumpGraceTicks() && o->cx + o->hw < x + crushHalf)
                    continue;
                DIE("crush", o);
                break;
            }
    }
    if (!dead && g_glitchEmbed > 0.0 && s.frame == 0
        && (c.mode == 1 || c.mode == 3 || c.mode == 7)) {
        const double cyF = (double)c.y;
        for (const Obj* o : *K.near) {
            if (o->type != 0 || o->slope || o->oneway || o->oriented) continue;
            const double ox = o->hw + pHalf - std::fabs(x - o->cx);
            const double oy = o->hh + pHalf - std::fabs(cyF - o->cy);
            if (ox > g_glitchEmbed && oy > g_glitchEmbed) {
                DIE_AS(kDeathRule, "glitch/embed", o);
                break;
            }
        }
    }
    if (ballFlippedThisTick) c.jumpBuf = 0;
    g_ballTapped = ballFlippedThisTick;   // --repelland (fixup.hpp stepBoth)
    {
        uint8_t at = s.hitG;
        if (padFiredNow) at = 0;
        if (s.onSlope && !c.onSlope && !c.grounded) at = 0;
        if (c.grounded || ballFlippedThisTick) at = 1;
        g_hitGRepel = at;
        c.hitG = ballFlippedThisTick ? 0 : at;
    }
    if ((ballFlippedThisTick || spiderWarpedThisTick) && input) c.holdDead = 1;
    if (g_prePortalUid < 0 && gravFiredUid >= 0 && !s.dual && !c.dual
        && (s.mode == 1 || s.mode == 3 || s.mode == 4 || s.mode == 7)
        && c.onSlope && c.slopeUidNow > gravFiredUid) {
        g_prePortalUid = gravFiredUid;
        bool deadRe = false;
        State r = stepOne(s, input, K, deadRe, linkFlip);
        g_prePortalUid = -1;
        dead = deadRe;
        return r;
    }
    if (s.mode == 4 && c.mode == 4 && s.frame == 0 && c.frame == 0
        && !s.rev && !s.onSlope && !c.onSlope && x - xPrev > 0.0) {
        const double target = kRadToDeg * std::atan2(-((double)c.y - (double)s.y), x - xPrev);
        const double k = c.mini ? kWaveRotKMini : kWaveRotK;
        c.rot = (float)((double)s.rot + k * (target - (double)s.rot));
    }
    c.frameChg = (gravHoldOver || c.frame != s.frame) ? 1 : 0;
    bool spawnRingJumpGiven = spawnRingJumpNow;
    if (spawnRingJumpGiven && g_spawnRingOn) {
        const int32_t h[3] = {s.usedOrbOld, s.usedOrbHist[0], s.usedOrbHist[1]};
        spawnRingJumpGiven = s.grounded
            && pressRingHolding((double)c.xAbs, (double)c.y, playerHalf(c.mode, c.mini != 0), (long long)K.t + 1,
                                (long long)K.t, spawnRingUid, h) < 0;
    }
    if (spawnRingJumpGiven && !dead && c.mode == 0) {
        const double vRing = kOrbYellow * ringScaleFor(useDx) * (c.mini ? kMiniImpulse : 1.0);
        const double gMag = gStepMag(std::fabs(cubePhysFor(useDx).g), false, false);
        VYSET(c.vy) = (float)((c.flip ? -1.0 : 1.0) * (vRing + gMag));
        c.grounded = 0;
    }
    {
        const bool up = a1cNow || a1cJumpLate || a1cLate || c.frame != s.frame;
        c.a1cLatch = (uint8_t)((c.a1cLatch & ~1u) | (up ? 1u : 0u));
    }
    if (g_ctrlTouch && !g_ctrlTouchBoxes.empty() && !dead && c.frame == 0 && !c.dual && !s.dual) {
        const double ph = (c.mode == 4 && c.mini) ? kWaveContactHalfMini
                                                  : playerHalf(c.mode, c.mini != 0);
        for (const CtrlTouchBox& b : g_ctrlTouchBoxes)
            if (!(b.holdOff && input) && std::fabs((double)c.xAbs - b.cx) <= b.hw + ph
                && std::fabs((double)yFree - b.cy) <= b.hh + ph)
                c.ctrlT = b.on;
    }
    if (tpPortalXFired && !dead) c.xAbs = (float)tpPortalXFired->tpEx;
    if (tpOrbFired && !dead) {
        const Obj* ob = tpOrbFired;
        if (tpOrbUndo) {
            VYSET(c.vy) = s.vy;
            c.grounded = s.grounded;
            if (c.mode == 0 || c.mode == 2) c.jumpBuf = 1;
        }
        if (!tpOrbUndo && input && !s.action && s.grounded && (c.mode == 5 || c.mode == 7)) {
            VYSET(c.vy) = s.vy;
            c.grounded = s.grounded;
            c.flip = s.flip;
            c.rHover = s.rHover;
        }
        // ...and the spider's press, which the orb took before the tap (spiderTpOrbFirst)
        if (c.mode == 6 && input) c.pSpiderTap = 1;
        if (c.grounded) c.tpArrive = 1;
        if (!(ob->tpKeep & 2)) YSET(c.y) = (float)ob->tpEy;
        if (!(ob->tpKeep & 1)) c.xAbs = (float)ob->tpEx;
        int wantUp = -1;
        if (ob->tpGrav == 1) wantUp = 0;
        else if (ob->tpGrav == 2) wantUp = 1;
        else if (ob->tpGrav == 3) wantUp = gdUpOf(c) ? 0 : 1;
        if (wantUp >= 0 && gdUpOf(c) != (uint8_t)wantUp) {
            c.flip = (uint8_t)wantUp;
            VYSET(c.vy) = c.vy * 0.5f;
            c.grounded = 0;   // the rig's forced and toggled units arrive with onGround 0
        }
        c.snapObj = nullptr;
        c.snapDist = 0.f;
        c.onSlope = 0;
        c.slopeM = 0.f;
    }
    const bool suNoSeat = suSeatUid < 0 && s.onSlope && s.slopeUidNow >= 0;
    if (g_slopeUnchanged && !dead && (suSeatUid >= 0 || suNoSeat) && c.mode == 1 && c.flip
        && c.frame == 0 && !c.dual && K.slopes) {
        const int suUid = suSeatUid >= 0 ? suSeatUid : s.slopeUidNow;
        const Obj* su = nullptr;
        for (const Obj* q : *K.slopes)
            if (q->uid == suUid) { su = q; break; }
        if (su && !slopeIsCeiling(su->slopeDir) && su->hw > 0.0) {
            const double pHsu = playerHalf(c.mode, c.mini != 0);
            const double tNow = slopeSeatTarget(su, (double)c.xAbs, pHsu, false, 0.0);
            const Obj* suPrev = su;
            if (g_apexReacq && s.onSlope && (int)s.slopeUidNow != su->uid)
                for (const Obj* q : *K.slopes)
                    if (q->uid == (int)s.slopeUidNow && !slopeIsCeiling(q->slopeDir) && q->hw > 0.0) {
                        suPrev = q;
                        break;
                    }
            const double tPrev = slopeSeatTarget(suPrev, (double)s.xAbs, pHsu, false, 0.0);
            if ((double)c.y < tNow - 0.5) {
                const bool unchanged = s.onSlope && std::fabs(tNow - tPrev) < 1e-9;
                if (unchanged) {
                    const double mSu = (su->sy1 - su->sy0) / (2.0 * su->hw);
                    if (g_apexStick && suSeatUid >= 0 && suYPreSet) YSET(c.y) = suYPre;
                    c.onSlope = 0;
                    VYSET(c.vy) = (float)rawQ(slopeExitVy(std::fabs(mSu), 1, useDx, c.mini != 0)
                                              * slopeRampFactor(1));
                    c.grounded = 0;
                } else if (suSeatUid >= 0) {
                    VYSET(c.vy) = 0.f;
                    c.grounded = 1;
                    c.onSlope = 1;
                    c.slopeUidNow = su->uid;
                }
                if (g_slopeDbg && (unchanged || suSeatUid >= 0))
                    std::printf("slopeunchanged t=%lld uid=%d y=%.4f seat=%.4f prev=%.4f %s%s vy=%.4f\n",
                                (long long)K.t, su->uid, (double)c.y, tNow, tPrev,
                                unchanged ? "release" : "land", suSeatUid < 0 ? " (no seat)" : "",
                                (double)c.vy);
            }
        }
    }
    if (g_teleportTrig && !g_tpTrigs.empty() && !dead
        && ((!s.dual && !c.dual) || (g_p2xOff && s.dual && c.dual && g_halfNow == 0))
        && s.frame == 0 && c.frame == 0 && !s.rev && !c.rev) {
        const double xp = (double)s.xAbs, xn = (double)c.xAbs;
        for (const TpTrig& t : g_tpTrigs) {
            if (t.cx > xn) break;
            if (t.cx <= xp) continue;
            if (!t.ignY) YSET(c.y) = (float)t.ty;
            if (!t.ignX) c.xAbs = (float)t.tx;
            int wantUp = -1;
            if (t.grav == 1) wantUp = 0;
            else if (t.grav == 2) wantUp = 1;
            else if (t.grav == 3) wantUp = gdUpOf(c) ? 0 : 1;
            if (wantUp >= 0 && gdUpOf(c) != (uint8_t)wantUp) {
                c.flip = (uint8_t)wantUp;
                VYSET(c.vy) = c.vy * 0.5f;
            }
            if (g_tpPush && t.force.fm != 0) VYSET(c.vy) = tpForceVy(t.force, c.vy, c.boost);
            c.grounded = 0;
            c.snapObj = nullptr;
            c.snapDist = 0.f;
            c.onSlope = 0;
            c.slopeM = 0.f;
        }
    }
    return c;
}

inline void swapHalves(State& s) {
    std::swap(s.y, s.y2);
    std::swap(s.vy, s.vy2);
    std::swap(s.slopeM, s.slopeM2);
    std::swap(s.snapDist, s.snapDist2);
    std::swap(s.grounded, s.grounded2);
    std::swap(s.flip, s.flip2);
    std::swap(s.ringHold, s.ringHold2);
    std::swap(s.pressSpent, s.pressSpent2);
    if (g_dualSlide) std::swap(s.slideT, s.slideT2);   // --dualslide: per-body dart slide arm
    if (g_dualHover) std::swap(s.rHover, s.rHover2);   // --dualhover: per-body robot hover budget
    {
        std::swap(s.dashing, s.dashing2);
        std::swap(s.dashSlope, s.dashSlope2);
    }
    std::swap(s.boost, s.boost2);
    std::swap(s.onSlope, s.onSlope2);
    std::swap(s.slopeT, s.slopeT2);
    std::swap(s.rideLanded, s.rideLanded2);
    std::swap(s.seatT, s.seatT2);
    std::swap(s.mode, s.mode2);
    std::swap(s.mini, s.mini2);
    std::swap(s.slopeUid0, s.slopeUid02);
    std::swap(s.slopeUidNow, s.slopeUidNow2);
    std::swap(s.ceilT, s.ceilT2);
    std::swap(s.ceilM4, s.ceilM42);
    std::swap(s.snapObj, s.snapObj2);
    std::swap(s.usedOrb, s.usedOrb2);
    std::swap(s.usedOrbOld, s.usedOrbOld2);
    for (int i = 0; i < 3; ++i) std::swap(s.usedOrbHist[i], s.usedOrbHist2[i]);
    for (int i = 0; i < 2; ++i) std::swap(s.spdFired[i], s.spdFired2[i]);
    for (int i = 0; i < 2; ++i) std::swap(s.spdMore[i], s.spdMore2[i]);   // --spdsameonce
    std::swap(s.holdDead, s.holdDead2);
    for (int i = 0; i < 3; ++i) std::swap(s.portSeen[i], s.portSeen2[i]);
    for (int i = 0; i < 4; ++i) std::swap(s.touchRing[i], s.touchRing2[i]);
    std::swap(s.touchRingT, s.touchRingT2);
    std::swap(s.portalLatch, s.portalLatch2);
    for (int i = 0; i < 4; ++i) std::swap(s.usedPad[i], s.usedPad2[i]);
    std::swap(s.hitG, s.hitG2);   // --repela0c
    // --a1clatch: the two bodies' +0xa1c are bits 0 and 1 of one byte
    s.a1cLatch = (uint8_t)(((s.a1cLatch & 1u) << 1) | ((s.a1cLatch >> 1) & 1u));
    // --ballscaleair: the two bodies' +0x728 are bits 0-1 and 2-3 of one byte
    s.ball728 = (uint8_t)(((s.ball728 & 3u) << 2) | ((s.ball728 >> 2) & 3u));
    {                 // --rot2
        std::swap(s.rot, s.rot2);
        std::swap(s.rotStep, s.rotStep2);
        std::swap(s.rotNeg, s.rotNeg2);
    }
    {        // --dualflipclock
        std::swap(s.flipT, s.flipT2);
        std::swap(s.modeT, s.modeT2);
    }
}

inline bool touchOwnOn(const TouchTrig& T, long long t) {
    int lastT = -1;
    double lastX = -1e18;
    uint8_t on = 1;
    for (const TouchTrig::OwnTog& e : T.ownTog) {
        const int ft = g_autoTrig[(size_t)e.autoIdx].fireT;
        if (ft < 0 || ft >= t) continue;
        if (ft > lastT || (ft == lastT && e.cx >= lastX)) {
            lastT = ft;
            lastX = e.cx;
            on = e.on;
        }
    }
    return on != 0;
}
inline bool squareMeetsTurnedBox(double px, double py, double h, double cx, double cy, double hw, double hh,
                                 double rotDeg) {
    const double th = -rotDeg * 3.14159265358979 / 180.0;
    const double co = std::cos(th), si = std::sin(th);
    const double dx = px - cx, dy = py - cy;
    const double axes[4][2] = {{1.0, 0.0}, {0.0, 1.0}, {co, si}, {-si, co}};
    for (const auto& ax : axes) {
        const double d = std::fabs(dx * ax[0] + dy * ax[1]);
        const double rp = h * (std::fabs(ax[0]) + std::fabs(ax[1]));
        const double rq = hw * std::fabs(co * ax[0] + si * ax[1]) + hh * std::fabs(-si * ax[0] + co * ax[1]);
        if (d > rp + rq) return false;
    }
    return true;
}
inline void markTouched(State& c, const StepCtx& K, double preY, bool btnHeld,
                        bool btnFresh = true, const State* par = nullptr, bool btnNow = false) {
    if (!K.trigs || K.trigs->empty()) return;
    const double half = playerHalf(c.mode, c.mini != 0);
    for (const auto& tb : *K.trigs) {
        if (c.trig & tb.second) continue;
        const TouchTrig* T = tb.first;
        if (T->count >= 0 || T->tap) continue;
        const bool freshFire = btnFresh && par && pressFreshFires(*T, *par, K.t);
        long long fireAt = freshFire ? K.t - 1 : K.t;
        const bool held = (g_pressFresh && !T->spawnRing)
                              ? (pressLatTwo(c.mode) ? (btnNow && !g_flyHeldBox) : (btnNow || btnHeld))
                              : btnHeld;
        if (T->press && !T->offSwitch && !held && !freshFire) continue;
        if (g_spawnRingOn && T->spawnRing
            && (!btnNow || c.pressSpent || !pressRingOnAt(T->uid, (long long)K.t - 1)))
            continue;
        bool offFresh = false;
        if (T->offSwitch && T->press) {
            if (!btnFresh || !par) continue;
            const double h0 = playerHalf(par->mode, par->mini != 0);
            if (!(std::fabs((double)par->xAbs - T->cx) < T->hw + h0
                  && std::fabs((double)par->y - T->cy) < T->hh + h0))
                continue;
            if (g_boxOffOwn && pressLatTwo(par->mode)) {
                offFresh = true;
                fireAt = K.t - 1;
            }
        }
        if (g_p2Touch && c.dual) {
            const double half2 = playerHalf(c.mode2, c.mini2 != 0);
            // --p2xoff: at the second body's own x
            const double x2 = (double)c.xAbs + ((g_p2xOff && c.frame == 0) ? (double)c.dx2 : 0.0);
            const bool p2in = std::fabs(x2 - T->cx) < T->hw + half2
                              && std::fabs((double)c.y2 - T->cy) < T->hh + half2;
            const bool p1in = std::fabs((double)c.xAbs - T->cx) < T->hw + half
                              && (std::fabs((double)c.y - T->cy) < T->hh + half
                                  || std::fabs(preY - T->cy) < T->hh + half);
            if (p2in)
                std::printf("p2touch: t=%lld box=0x%llx p1=%d p2=1\n",
                            (long long)K.t, (unsigned long long)tb.second.word(0),
                            p1in ? 1 : 0);
        }
        double bx = T->cx, by = T->cy;
        if (T->followIdx >= 0 && g_touchFollowObjs
            && (size_t)T->followIdx < g_touchFollowObjs->size()) {
            const Obj& fo = (*g_touchFollowObjs)[(size_t)T->followIdx];
            bx += (double)fo.cx - T->followX0;
            by += (double)fo.cy - T->followY0;
        }
        const DynSample* rb = nullptr;
        if (g_boxArm && T->recBox && c.frame == 0) {
            const std::vector<DynSample>& sm = *T->recBox;
            size_t lo = 0, hi = sm.size();
            while (lo + 1 < hi) {
                const size_t md = (lo + hi) / 2;
                if (sm[md].t <= K.t) lo = md; else hi = md;
            }
            if (sm[lo].t <= K.t) rb = &sm[lo];
        }
        bool p2In = false;
        if (g_boxArm && T->press && !T->offSwitch && c.dual) {
            const double half2 = playerHalf(c.mode2, c.mini2 != 0);
            const double x2 = (double)c.xAbs + ((g_p2xOff && c.frame == 0) ? (double)c.dx2 : 0.0);
            p2In = rb ? squareMeetsTurnedBox(x2, (double)c.y2, half2, rb->cx, rb->cy, T->hw, T->hh, rb->rot)
                      : std::fabs(x2 - bx) < T->hw + half2 && std::fabs((double)c.y2 - by) < T->hh + half2;
        }
        const bool p1In = rb
            ? (squareMeetsTurnedBox((double)c.xAbs, (double)c.y, half, rb->cx, rb->cy, T->hw, T->hh, rb->rot)
               || squareMeetsTurnedBox((double)c.xAbs, preY, half, rb->cx, rb->cy, T->hw, T->hh, rb->rot))
            : (std::fabs((double)c.xAbs - bx) < T->hw + half
               && (std::fabs((double)c.y - by) < T->hh + half || std::fabs(preY - by) < T->hh + half));
        if (freshFire || offFresh || p2In || p1In) {
            // --touchoff: a trigger in a group a Toggle has switched off is not touched.
            if (g_touchOwnOff && !T->ownTog.empty() && !touchOwnOn(*T, K.t)) continue;
            c.trig |= tb.second;
            c.trigT = (int32_t)fireAt;
            {
                const int b = touchBitIndex(tb.second);
                if (g_touchFireT[b] < 0) g_touchFireT[b] = (int)fireAt;
                c.fireB[b] = (uint16_t)fireAt;
                if (g_touchCensus) {
                    const bool yHit = std::fabs((double)c.y - T->cy) < T->hh + half;
                    std::printf("tcensus: t=%lld box=%d uid=%d hit=%s mode=%d mini=%d "
                                "x=%.3f y=%.3f preY=%.3f vy=%.3f br=%d\n",
                                (long long)K.t, b, T->uid,
                                (freshFire || offFresh) ? "fresh" : yHit ? "y" : "preY",
                                (int)c.mode, (int)c.mini, (double)c.xAbs, (double)c.y,
                                preY, (double)c.vy, g_tcBranchP1);
                }
            }
            if (g_slopeDbg)
                std::printf("trigfire t=%lld box(%.1f,%.1f) %.0fx%.0f "
                            "player(%.1f,%.1f) half=%.1f mask=0x%llx\n",
                            (long long)K.t, T->cx, T->cy, 2 * T->hw, 2 * T->hh,
                            (double)c.xAbs, (double)c.y, half,
                            (unsigned long long)c.trig.word(0));
        }
    }
}
}  // namespace dp
