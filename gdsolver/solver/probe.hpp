#pragma once
namespace probe {
inline bool g_pause = false;
inline int g_step = 0;   // number of substeps to advance on the next frame

inline void reset() {
    g_pause = false;
    g_step = 0;
}

// Log the state as one line (for pinpointing causes)
inline void trace(const char* tag, PlayerObject* p, long long tick) {
    if (!p) return;
    writeResult(std::string("PROBE[") + tag + "] tick=" + std::to_string(tick)
        + " x=" + std::to_string(p->getPositionX())
        + " y=" + std::to_string(p->getPositionY())
        + " vy=" + std::to_string((float)p->m_yVelocity)
        + " g=" + std::to_string(p->m_isOnGround)
        + " g2=" + std::to_string(p->m_isOnGround2)
        + " dead=" + std::to_string(p->m_isDead)
        + " up=" + std::to_string(p->m_isUpsideDown)
        + " locked=" + std::to_string(p->m_isLocked)
        + " ctrlOff=" + std::to_string(p->m_controlsDisabled)
        + " mpos=" + std::to_string(p->m_position.y)
        + " gnd=" + std::to_string(
            PlayLayer::get() && PlayLayer::get()->m_groundLayer
                ? PlayLayer::get()->m_groundLayer->getPositionY() : -1.f)
        + " mode=" + modeStr(p));
}
} // namespace probe
