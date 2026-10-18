#include "Jobs.hpp"
#include "ModGuard.hpp"
#include "ProgressPanel.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>

#include <algorithm>
#include <filesystem>

using namespace geode::prelude;

namespace kp {
namespace {
struct Watch {
    bool driving = false;
    Ref<GJGameLevel> level;
    bool had = false;
    std::filesystem::file_time_type before{};
    std::string limitsDropped;   // kp::onLimitsDropped: where the solver went on without the limits
} g;

void offer(GJGameLevel* level, const std::string& limitsDropped) {
    JobRequest req;
    req.kind = JobKind::Optimize;
    std::string err;
    auto path = solverSolutionPath(level);
    if (!kpsim::macro::readSolverPlan(path, req.plan, err) || !req.plan.presses()) return;
    req.merged = req.plan;
    req.source = path.filename().string();
    req.fitLimits = limitsDropped;
    Ref<GJGameLevel> keep = level;
    if (!limitsDropped.empty() || Mod::get()->getSettingValue<bool>("auto-optimize")) {
        Loader::get()->queueInMainThread([keep, req]() mutable { startJob(keep, std::move(req)); });
        return;
    }
    // Shortest press or gap of the raw plan: why it is worth optimizing.
    unsigned shortest = ~0u;
    for (int ch = 0; ch < 2; ++ch) {
        const auto& h = req.plan.channel(ch);
        for (size_t i = 0; i < h.size(); ++i) {
            if (h[i].release != kpsim::macro::kNever) shortest = std::min(shortest, h[i].release - h[i].press);
            if (i) shortest = std::min(shortest, h[i].press - h[i - 1].release);
        }
    }
    std::string text = fmt::format("The solver found a solution: {} clicks, shortest press or gap {} ticks.\n"
                                   "Optimize it now? Fewer clicks, human timings, checked by the game.",
                                   req.plan.presses(), shortest == ~0u ? 0u : shortest);
    // A route found where the limits do not reach (the section solver ignores them).
    if (const auto breach = limitBreach(req.plan); !breach.empty())
        text += "\n<cy>Beyond your limits: " + breach + "</c>";
    Loader::get()->queueInMainThread([keep, req, text]() mutable {
        createQuickPopup("Kairos", text, "Later", "Optimize", [keep, req](FLAlertLayer*, bool go) mutable {
            if (go) startJob(keep, std::move(req));
        });
    });
}

// During a Solve, the Kairos panel in place of the solver's own text: how far, what now, Stop.
Ref<ProgressPanel> g_panel;
float g_panelClock = 0;

void panel(float dt) {
    const SolveStatus st = solveStatus();
    auto* pl = PlayLayer::get();
    auto* scene = pl ? pl->getParent() : nullptr;
    if (!st.solving || !scene) {
        if (g_panel) g_panel->removeFromParent();
        g_panel = nullptr;
        return;
    }
    if (!g_panel || g_panel->getParent() != scene) {
        if (g_panel) g_panel->removeFromParent();
        g_panel = ProgressPanel::create(fmt::format("Solve  -  {}", std::string(pl->m_level->m_levelName)));
        g_panel->onStop([] { Loader::get()->queueInMainThread([] { stopSolve(); }); });
        scene->addChild(g_panel, 2);
        g_panelClock = 1.f;
    }
    if ((g_panelClock += dt) < 0.25f) return;
    g_panelClock = 0;
    std::string status = st.phase.empty() ? std::string("starting") : st.phase;
    if (st.iteration) status = fmt::format("round {}: {}", st.iteration, status);
    g_panel->set(status, st.level, st.seconds,
                 st.search >= 0 ? fmt::format("search {:.0f}%", st.search * 100.0) : std::string());
}

void poll(float dt) {
    panel(dt);
    const bool driving = solverDriving();
    if (driving && !g.level) {
        if (auto* pl = PlayLayer::get(); pl && pl->m_level) {
            g.level = pl->m_level;
            std::error_code ec;
            auto path = solverSolutionPath(pl->m_level);
            g.had = std::filesystem::exists(path, ec);
            if (g.had) g.before = std::filesystem::last_write_time(path, ec);
        }
    }
    if (!driving && g.driving && g.level) {
        std::error_code ec;
        auto path = solverSolutionPath(g.level);
        if (std::filesystem::exists(path, ec) && (!g.had || std::filesystem::last_write_time(path, ec) > g.before)) {
            saveModelWorld((int)g.level->m_levelID, modelDir(g.level));
            offer(g.level, g.limitsDropped);
        }
        g.limitsDropped.clear();
    }
    if (!driving) g.level = nullptr;
    g.driving = driving;
}
} // namespace

void onLimitsDropped(const std::string& where) {
    if (g.limitsDropped.empty()) g.limitsDropped = where;
}
} // namespace kp

class $modify(KairosSolveWatch, CCScheduler) {
    void update(float dt) {
        kp::poll(dt);
        CCScheduler::update(dt);
    }
};
