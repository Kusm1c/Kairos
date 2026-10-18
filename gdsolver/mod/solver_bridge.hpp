#pragma once
// Includes the in-process solver modules (solver/*.hpp) and the manual pause/step keys.
#include "mod/commands.hpp"

#include "mod/dp_bridge.hpp"

namespace p1 {
#include "solver/solver.hpp"

#include "solver/areaenv.hpp"
#include "solver/grouptrace.hpp"
#include "solver/clearance.hpp"
#include "solver/psnap.hpp"
#include "solver/secsolve.hpp"
#include "solver/route.hpp"

inline void dpSelfTest(GJBaseGameLayer* l) {
    if (!l) return;
    std::ostringstream oss;
    solver::writeObjRects(oss, l);
    const std::string csv = oss.str();

    std::ifstream f(std::string(DATA_DIR) + "/objrects.txt");
    const std::string disk((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
    const char* verdict = disk.empty() ? "NO DUMP ON DISK"
                        : (disk == csv ? "same as objrects.txt" : "DIFFERS from objrects.txt");
    writeResult("dpselftest: in-memory csv " + std::to_string(csv.size()) + " bytes, disk "
                + std::to_string(disk.size()) + " bytes - " + verdict);

    const dpbridge::LevelStats s = dpbridge::statsFromCsv(csv);
    char b[256];
    snprintf(b, sizeof(b),
             "dpselftest: level: %zu colliders, %zu portals, %zu pads, %zu orbs, "
             "%zu moving, maxX=%.0f",
             s.objs, s.portals, s.pads, s.orbs, s.moving, s.maxX);
    writeResult(b);
    writeResult("dpselftest: " + dpbridge::coreVersion());
}

namespace hitbox {
inline int g_mode = 0;   // 0 = off, 1 = boxes over the level, 2 = boxes only
inline std::vector<cocos2d::CCNode*> g_hidden;   // what mode 2 hid, so it can be put back

inline void clearHidden() {
    for (cocos2d::CCNode* n : g_hidden) if (n) n->release();
    g_hidden.clear();
}

inline void showArt(bool visible) {
    if (visible) {
        for (cocos2d::CCNode* n : g_hidden) if (n) n->setVisible(true);
        clearHidden();
        return;
    }
    auto* pl = PlayLayer::get();
    if (!pl || !pl->m_objectLayer) return;
    auto* kids = pl->m_objectLayer->getChildren();
    if (!kids) return;
    for (int i = 0; i < (int)kids->count(); ++i) {
        auto* n = static_cast<cocos2d::CCNode*>(kids->objectAtIndex(i));
        if (!n || n == pl->m_debugDrawNode || !n->isVisible()) continue;
        n->setVisible(false);
        n->retain();
        g_hidden.push_back(n);
    }
}

inline bool apply() {
    auto* pl = PlayLayer::get();
    if (!pl) return false;
    pl->m_isDebugDrawEnabled = (g_mode > 0);
    if (pl->m_debugDrawNode) pl->m_debugDrawNode->setVisible(g_mode > 0);
    showArt(g_mode < 2);
    return true;
}

inline void cycle() {
    const int prev = g_mode;
    g_mode = (prev + 1) % 3;
    if (!apply()) { g_mode = prev; return; }
    log::info("hotkey: F6 -> hitboxes {}",
              g_mode == 0 ? "off" : (g_mode == 1 ? "on" : "only"));
}

inline void onLevelReset() {
    clearHidden();
    if (g_mode > 0) apply();
}

inline void perFrame(GJBaseGameLayer* l) {
    if (g_mode <= 0 || !l) return;
    l->m_isDebugDrawEnabled = true;
    l->updateDebugDraw();
    if (l->m_debugDrawNode) l->m_debugDrawNode->setVisible(true);
}
}  // namespace hitbox

#include "solver/probe.hpp"

inline void handleProbeRequest(GJBaseGameLayer* layer) {
    if (!g_probeRequest || !layer || !layer->m_player1) return;
    auto* p = layer->m_player1;
    int req = g_probeRequest;
    g_probeRequest = 0;
    switch (req) {
        case 2: // pause/resume
            probe::g_pause = !probe::g_pause;
            g_paused = false;
            probe::trace(probe::g_pause ? "pause" : "resume", p, g_tick);
            log::info("probe: {}", probe::g_pause ? "PAUSED" : "RESUMED");
            break;
        case 3: // 1 substep
            probe::g_step = 1;
            probe::trace("step1", p, g_tick);
            break;
        case 4: // 10 substeps
            probe::g_step = 10;
            probe::trace("step10", p, g_tick);
            break;
        case 6: // hitboxes: off -> on -> only
            hitbox::cycle();
            break;
        default: break;
    }
}
}  // namespace p1
