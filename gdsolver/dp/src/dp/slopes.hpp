#pragma once
#include "dp/search_key.hpp"

namespace dp {
inline double slopeXOffset(double m, double pHalf) {
    return pHalf * std::tan(std::atan(std::fabs(m)) * 0.5);
}
inline bool slopeIsCeiling(uint8_t dir) {
    return dir == 1 || dir == 3 || dir == 5 || dir == 6;
}
inline bool slopeIsUphill(uint8_t dir) {
    return dir == 0 || dir == 3 || dir == 6 || dir == 7;
}

inline bool slopeUnderside(bool flip, uint8_t dir) {
    return flip != slopeIsCeiling(dir);
}

inline bool slopeBVar22(bool flip, uint8_t dir) {
    return (!slopeIsUphill(dir)) != flip;
}

inline bool slopeNudgeMode(int mode) {
    return mode == 1 || mode == 3 || mode == 4 || mode == 7;
}

inline bool boostLatchMode(int mode) {
    return mode == 1 || mode == 3 || mode == 7;
}

inline bool slopeSkipsHitGround(bool flip, uint8_t dir, int mode, bool held) {
    return held && slopeNudgeMode(mode) && !slopeUnderside(flip, dir)
        && !slopeBVar22(flip, dir);
}

inline float slopeNudge(float vy, bool flip, uint8_t dir, int mode, bool held) {
    if (!slopeNudgeMode(mode)) return vy;
    const double gs = flip ? -1.0 : 1.0;
    const double v = (double)vy;
    if (slopeUnderside(flip, dir)) {
        if (!held && slopeBVar22(flip, dir) && gs * v > -2.0)
            return (float)(-gs * 2.0);
    } else {
        if (held && !slopeBVar22(flip, dir) && gs * v < 2.0)
            return (float)(gs * 2.0);
    }
    return vy;
}
inline bool slopeWouldAcquire(const Obj* R, double px, double py, double pHalf,
                              bool continuing) {
    const double rx0 = R->cx - R->hw, rx1 = R->cx + R->hw;
    const double ry0 = R->cy - R->hh, ry1 = R->cy + R->hh;
    if (rx1 <= rx0) return false;
    if (!(px + pHalf >= rx0 && px - pHalf <= rx1)) return false;
    const double m = (R->sy1 - R->sy0) / (rx1 - rx0);
    const double cosT = std::cos(std::atan(std::fabs(m)));
    if (continuing) {
        if (py - pHalf / cosT - 4.0 > ry1) return false;
    } else if (!(py + pHalf >= ry0 + 1.0 && py - pHalf <= ry1 - 1.0)) {
        return false;
    }
    const double half = pHalf / cosT;
    const double line = R->sy0 + (R->sy1 - R->sy0) * (px - rx0) / (rx1 - rx0);
    if (!slopeIsCeiling(R->slopeDir)) {
        const double t = std::max(std::min(line + half, ry1 + pHalf), ry0);
        return t > py;
    }
    const double t = std::min(std::max(line - half, ry0 - pHalf), ry1);
    return t < py;
}
inline double slopeSeatTarget(const Obj* R, double px, double pHalf,
                              bool ceiling, double off) {
    const double x0 = R->cx - R->hw, x1 = R->cx + R->hw;
    const double m = (R->sy1 - R->sy0) / (x1 - x0);
    const double half = pHalf / std::cos(std::atan(std::fabs(m)));
    const double line = R->sy0 + m * (px - x0);   // NOT clamped to [x0,x1]
    const double oMin = R->cy - R->hh, oMax = R->cy + R->hh;
    if (!ceiling)
        return std::max(std::min(line + half - off, oMax + pHalf - off), oMin);
    return std::min(std::max(line - half + off, oMin - pHalf + off), oMax);
}
inline double slopeSeatOff(const Obj* R, const std::vector<const Obj*>* slopes,
                           int prevUid, bool wasOnSlope, bool mini) {
    if (!wasOnSlope || !slopes || prevUid < 0 || prevUid == R->uid) return 0.0;
    for (const Obj* pv : *slopes) {
        if (pv->uid != prevUid) continue;
        if (slopeIsCeiling(pv->slopeDir) == slopeIsCeiling(R->slopeDir))
            return 0.0;
        return mini ? 12.0 : 20.0;
    }
    return 0.0;
}

inline bool rampMayVetoSolid(const Obj* R, const Obj* o) {
    if (!(R->hw >= 0.0 && R->hh >= 0.0)) return true;
    const double sx0 = o->cx - o->hw, sx1 = o->cx + o->hw;
    const double sy0 = o->cy - o->hh, sy1 = o->cy + o->hh;
    const double jw = R->hw * 1.2, jh = R->hh * 1.2;
    return R->cx - jw <= sx1 && R->cx + jw >= sx0
        && R->cy - jh <= sy1 && R->cy + jh >= sy0;
}

inline bool slopeVetoesSolidIn(const Obj* o, const Obj* const* rb,
                               const Obj* const* re,
                               double px, double py, double pHalfW, double pHalfH,
                               bool faceIsTop, double prevX, double prevY,
                               int curSlopeUid, bool noPartner = false,
                               const Obj* notInMap = nullptr, bool wasOnSlope = false) {
    if (o->type != 0) return false;
    const double sx0 = o->cx - o->hw, sx1 = o->cx + o->hw;
    const double sy0 = o->cy - o->hh, sy1 = o->cy + o->hh;
    for (const Obj* const* rp = rb; rp != re; ++rp) {
        const Obj* R = *rp;
        if (R == notInMap) continue;
        const bool isTop = slopeIsCeiling(R->slopeDir);
        const bool uphill = slopeIsUphill(R->slopeDir);
        if (faceIsTop ? (isTop && R->uid != curSlopeUid)
                      : (!isTop && R->uid != curSlopeUid)) continue;
        if (!rampMayVetoSolid(R, o)) continue;
        const bool turned = std::fabs(std::fmod(std::fabs(R->rot), 180.0) - 90.0) < 45.0;
        const double iw = R->hw * (turned ? 1.1 : 1.2);
        const double ih = R->hh * (turned ? 1.2 : 1.1);
        const double ix0 = R->cx - iw, ix1 = R->cx + iw;
        const double iy0 = R->cy - ih, iy1 = R->cy + ih;
        if (!(ix0 <= sx1 && ix1 >= sx0 && iy0 <= sy1 && iy1 >= sy0)) continue;
        if (!(ix0 <= px + pHalfW && ix1 >= px - pHalfW
              && iy0 <= py + pHalfH && iy1 >= py - pHalfH)) continue;
        const bool cont = (R->uid == curSlopeUid);
        if (!(noPartner || cont || wasOnSlope
              || slopeWouldAcquire(R, px, py, pHalfH, cont)
              || slopeWouldAcquire(R, prevX, prevY, pHalfH, cont))) {
            const double rx0 = R->cx - R->hw, rx1 = R->cx + R->hw;
            if (!(rx0 < o->cx && o->cx < rx1)) continue;
        }
        const double xEdge = (uphill == isTop) ? sx1 : sx0;
        const double rx0 = R->cx - R->hw, rx1 = R->cx + R->hw;
        if (rx1 == rx0) continue;
        const double line = R->sy0 + (R->sy1 - R->sy0) * (xEdge - rx0) / (rx1 - rx0);
        if (faceIsTop ? (sy1 - line <= 2.0) : (line - 2.0 <= sy0)) return true;
    }
    return false;
}
inline bool slopeVetoesSolid(const Obj* o, const std::vector<const Obj*>* slopes,
                             double px, double py, double pHalfW, double pHalfH,
                             bool faceIsTop, double prevX, double prevY,
                             int curSlopeUid, bool noPartner = false,
                             const Obj* notInMap = nullptr, bool wasOnSlope = false) {
    if (!slopes) return false;
    return slopeVetoesSolidIn(o, slopes->data(), slopes->data() + slopes->size(),
                              px, py, pHalfW, pHalfH, faceIsTop, prevX, prevY,
                              curSlopeUid, noPartner, notInMap, wasOnSlope);
}

struct SlopeVetoIndex {
    const std::vector<const Obj*>* near = nullptr;     // the lists it was built
    const std::vector<const Obj*>* slopes = nullptr;   // from
    std::vector<uint32_t> off;
    std::vector<const Obj*> ramps;
    void build(const std::vector<const Obj*>& nr, const std::vector<const Obj*>& sl) {
        near = &nr;
        slopes = &sl;
        off.assign(nr.size() + 1, 0);
        ramps.clear();
        for (size_t i = 0; i < nr.size(); ++i) {
            off[i] = (uint32_t)ramps.size();
            const Obj* o = nr[i];
            if (o->type != 0) continue;
            for (const Obj* R : sl)
                if (rampMayVetoSolid(R, o)) ramps.push_back(R);
        }
        off[nr.size()] = (uint32_t)ramps.size();
    }
};
inline double slopeExitVy(double m, uint8_t mode, float dxF, bool mini) {
    const double a = std::fabs(m);
    const double cubeExit = (double)(1.4f * ((float)a * (4.0f * std::fabs(dxF)))
                                     * std::min(1.1f, 0.8f / std::atan((float)a)));
    if (mode == 2) {
        return cubeExit * 0.75;
    }
    if (mode == 7) {
        return cubeExit * 0.75;
    }
    if (mode == 1 || mode == 3 || mode == 4) return cubeExit * 0.75;
    return cubeExit;
}

inline double slopeRampFactor(int rideTicks) {
    const double diff = rideTicks / 240.0;
    if (diff >= 0.1) return 1.0;
    return std::max(0.4, diff * 10.0);
}

inline double playerHalf(uint8_t mode, bool mini) {
    return (mode == 4) ? (mini ? kWaveHalfMini : kWaveHalf)
         : (mode == 6) ? (mini ? kSpiderHalfMini : kSpiderHalf)
                       : (mini ? kMiniHalf : kCubeHalf);
}
}  // namespace dp
