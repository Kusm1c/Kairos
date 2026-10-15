// After a Solve that wrote a solution: optimize it (setting "auto-optimize") or offer to. After one
// the loop stopped as impossible within the human limits: say where and why.
#include "Jobs.hpp"
#include "LimitsPopup.hpp"
#include "ModGuard.hpp"

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
    std::string limitStop;   // kp::onLimitStop's reason, shown once the session is over
} g;

void offer(GJGameLevel* level) {
    JobRequest req;
    req.kind = JobKind::Optimize;
    std::string err;
    auto path = solverSolutionPath(level);
    if (!kpsim::macro::readSolverPlan(path, req.plan, err) || !req.plan.presses()) return;
    req.merged = req.plan;
    req.source = path.filename().string();
    Ref<GJGameLevel> keep = level;
    if (Mod::get()->getSettingValue<bool>("auto-optimize")) {
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

void poll() {
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
        if (!g.limitStop.empty()) {
            const std::string name = g.level->m_levelName, why = g.limitStop;
            Loader::get()->queueInMainThread([name, why] { showLimitsPopup(name, why); });
        } else {
            std::error_code ec;
            auto path = solverSolutionPath(g.level);
            if (std::filesystem::exists(path, ec) &&
                (!g.had || std::filesystem::last_write_time(path, ec) > g.before)) {
                saveModelWorld((int)g.level->m_levelID, modelDir(g.level));
                offer(g.level);
            }
        }
        g.limitStop.clear();
    }
    if (!driving) g.level = nullptr;
    g.driving = driving;
}

} // namespace

void onLimitStop(const std::string& why) { g.limitStop = why; }

} // namespace kp

class $modify(KairosSolveWatch, CCScheduler) {
    void update(float dt) {
        kp::poll();
        CCScheduler::update(dt);
    }
};
