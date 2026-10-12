#include "Jobs.hpp"
#include "CalmScreen.hpp"
#include "Fiber.hpp"
#include "GameEngine.hpp"
#include "ModGuard.hpp"
#include "ProgressPanel.hpp"
#include "Rhythm.hpp"
#include "kpsim/search/FrameWindows.hpp"
#include "kpsim/search/Optimizer.hpp"
#include "kpsim/search/Pathfinder.hpp"
#include "kpsim/search/WindowFile.hpp"
#include "StatsPopup.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>

#include <chrono>
#include <fstream>
#include <sstream>

using namespace geode::prelude;
using kpsim::macro::Plan;
using kpsim::macro::Tick;
using kpsim::search::Outcome;

namespace kp {

kpsim::search::InputRules humanRules() {
    kpsim::search::InputRules r;
    if (Mod::get()->getSettingValue<bool>("go-crazy")) return r;
    r.minPress = r.minRelease = (Tick)Mod::get()->getSettingValue<int64_t>("human-min-pulse");
    if (const int64_t cps = Mod::get()->getSettingValue<int64_t>("max-cps"); cps > 0)
        r.minInterval = (Tick)((240 + cps - 1) / cps);
    return r;
}

std::string limitBreach(const kpsim::macro::Plan& plan) {
    const auto r = humanRules();
    if (!r.minPress && !r.minRelease && !r.minInterval) return {};
    for (int ch = 0; ch < 2; ++ch) {
        const auto& h = plan.channel(ch);
        for (size_t i = 0; i < h.size(); ++i) {
            std::vector<std::string> need;
            if (h[i].release != kpsim::macro::kNever && h[i].release - h[i].press < r.minPress)
                need.push_back(fmt::format("a {}-tick press", h[i].release - h[i].press));
            if (i && h[i].press - h[i - 1].release < r.minRelease)
                need.push_back(fmt::format("a {}-tick gap", h[i].press - h[i - 1].release));
            if (i && r.minInterval && h[i].press - h[i - 1].press < r.minInterval) {
                const double d = h[i].press - h[i - 1].press;
                need.push_back(fmt::format("presses {:.2f} s apart ({:.0f} CPS)", d / 240.0, 240.0 / d));
            }
            if (need.empty()) continue;
            std::string what;
            for (size_t k = 0; k < need.size(); ++k)
                what += (k ? (k + 1 == need.size() ? " and " : ", ") : "") + need[k];
            const int64_t cps = Mod::get()->getSettingValue<int64_t>("max-cps");
            return fmt::format("At {:.2f} s{} it needs {}. Your limits: {}-tick presses and gaps{}.",
                               h[i].press / 240.0, ch ? " (player 2)" : "", what, r.minPress,
                               cps > 0 ? fmt::format(", {} CPS", cps) : std::string());
        }
    }
    return {};
}

namespace {

struct Pending {
    bool active = false;
    int levelId = 0;
    JobRequest request;
} g_pending;

std::string safeName(std::string s) {
    for (auto& c : s)
        if (std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos) c = '_';
    return s.empty() ? "level" : s;
}

std::string fileStem(GJGameLevel* level) {
    return safeName(std::string(level->m_levelName)) + "-" + std::to_string((int)level->m_levelID);
}

double ticksToMs(int ticks) { return ticks * 1000.0 / 240.0; }

// Replays a plan visibly at normal speed.
struct Watch {
    bool on = false;
    Plan plan;
    Tick tick = 0;
    bool held[2] = {};
    bool flip = false;
    bool injecting = false;
    GJBaseGameLayer* layer = nullptr;
} g_watch;

bool twoPlayer(PlayLayer* pl) { return pl->m_levelSettings && pl->m_levelSettings->m_twoPlayerMode; }

// 1-player levels: both buttons act on player 1.
Plan planFor(PlayLayer* pl, const JobRequest& r) { return twoPlayer(pl) ? r.plan : r.merged; }

void startWatch(PlayLayer* pl, Plan plan) {
    g_watch = Watch{};
    g_watch.on = true;
    g_watch.plan = std::move(plan);
    g_watch.layer = pl;
    g_watch.flip = !pl->m_isPlatformer && GameManager::get()->getGameVariable("0010");
    pl->m_isTestMode = true;
    pl->m_clickBetweenSteps = false;
    pl->m_clickOnSteps = true;
    updateModGuard();
    pl->resetLevel();
}

const char* outcomeName(Outcome o) {
    return o == Outcome::Finished ? "finished" : o == Outcome::Died ? "died" : "alive";
}

std::string windowsReport(GJGameLevel* level, const JobRequest& req, const kpsim::search::WindowReport& r) {
    std::string out = fmt::format("Frame windows - {} ({})\nmacro: {}\nbase run: {} at tick {}\n\n",
                                  std::string(level->m_levelName), (int)level->m_levelID, req.source,
                                  outcomeName(r.base.outcome), r.base.tick);
    const bool two = !r.removable[1].empty() ||
                     std::any_of(r.edges.begin(), r.edges.end(), [](auto& w) { return w.player == 1; });
    auto name = [&](int player, size_t hold) {
        return fmt::format("{}#{}", two ? (player ? "P2 " : "P1 ") : "", hold + 1);
    };
    out += "click     edge     tick   earlier  later  window  (ms)\n";
    int perfect = 0;
    for (const auto& w : r.edges) {
        int width = w.early + w.late + 1;
        perfect += width == 1;
        out += fmt::format("{:<9} {:<8} {:>6}  {:>6}{} {:>5}{} {:>6}  {:>6.1f}\n", name(w.player, w.hold),
                           w.press ? "press" : "release", w.tick, w.early, w.earlyLimited ? "+" : " ", w.late,
                           w.lateLimited ? "+" : " ", width, ticksToMs(width));
    }
    int removable = 0;
    for (auto& v : r.removable)
        for (bool b : v) removable += b;
    const auto st = kpsim::search::windowStats(r, 1.0);
    out += fmt::format("\n{} edges, frame-perfect at 60 / 120 / 240 FPS: {:.0f} / {:.0f} / {}, {} clicks removable, "
                       "{} trials ({} cut short)\n",
                       r.edges.size(), st.perfectAt[0], st.perfectAt[1], perfect, removable, r.trials, r.pruned);
    out += "+ = limited by a neighbour edge or the search range, not by a death\n";
    out += fmt::format("Windows count the next inputs re-timed by about the same shift ({} sides needed it)\n",
                       r.followed);
    if (st.precision > 0)
        out += fmt::format("Precision for a 24 h clear: {:.1f} sigma/s (nandl.pages.dev base formula)\n", st.precision);
    for (int p = 0; p < 2; ++p)
        for (size_t i = 0; i < r.removable[p].size(); ++i)
            if (r.removable[p][i]) out += fmt::format("click {} is not needed\n", name(p, i));
    return out;
}

class JobRunner : public CCNode {
public:
    static JobRunner* create(PlayLayer* layer, JobRequest request) {
        auto* r = new JobRunner();
        if (r->init(layer, std::move(request))) {
            r->autorelease();
            return r;
        }
        delete r;
        return nullptr;
    }

    ~JobRunner() override {
        if (m_panel) m_panel->removeFromParent();
    }

    void cancelNow() {
        if (!m_task || m_task->done()) return;
        m_engine->cancel();
        for (int i = 0; i < 1000 && !m_task->done(); ++i) m_task->resume(50.0);
        finish(true);
    }

private:
    bool init(PlayLayer* layer, JobRequest request) {
        if (!CCNode::init()) return false;
        m_layer = layer;
        m_request = std::move(request);
        // On the scene, above the cover that hides the level (CalmScreen); placed once there is one.
        m_panel = ProgressPanel::create(fmt::format("{}  -  {}", jobTitle(), std::string(layer->m_level->m_levelName)));
        m_panel->onStop([this] {
            if (m_engine) m_engine->cancel();
        });
        this->scheduleUpdate();
        return true;
    }

    const char* jobTitle() const {
        switch (m_request.kind) {
            case JobKind::FindPath: return "Find path";
            case JobKind::Optimize: return "Optimize";
            case JobKind::Windows: return "Frame windows";
            default: return "Kairos";
        }
    }

    std::string refuseReason() {
        auto* s = m_layer->m_levelSettings;
        if (s && s->m_platformerMode) return "Platformer levels are not supported.";
        if (m_layer->m_isPracticeMode) return "Leave practice mode first.";
        return {};
    }

    void start() {
        if (auto why = refuseReason(); !why.empty()) {
            m_finished = true;
            m_panel->removeFromParent();
            FLAlertLayer::create("Kairos", why, "OK")->show();
            return;
        }
        m_savedAttempts = m_layer->m_level->m_attempts.value();
        m_savedJumps = m_layer->m_level->m_jumps.value();
        m_savedTestMode = m_layer->m_isTestMode;
        m_savedCBS = m_layer->m_clickBetweenSteps;
        m_savedCOS = m_layer->m_clickOnSteps;
        m_layer->m_isTestMode = true;
        m_layer->m_clickBetweenSteps = false;
        m_layer->m_clickOnSteps = true;
        setLayersVisible(false);
        m_request.plan = planFor(m_layer, m_request);
        m_engine = std::make_unique<GameEngine>(m_layer);
        updateModGuard();
        m_started = std::chrono::steady_clock::now();
        m_limitMinutes = (double)Mod::get()->getSettingValue<int64_t>("time-limit-minutes");
        m_task = std::make_unique<FiberTask>([this] { body(); });
    }

    void body() {
        if (m_request.kind == JobKind::FindPath) {
            kpsim::search::PathOptions o;
            o.rules = humanRules();
            kpsim::search::findPath(*m_engine, o, m_path);
        } else if (m_request.kind == JobKind::Optimize) {
            const auto goal = Mod::get()->getSettingValue<std::string>("solve-goal");
            auto o = kpsim::search::optimizeFor(goal == "Fewest inputs" ? kpsim::search::SolveGoal::FewestInputs
                                                : goal == "Most lenient windows"
                                                    ? kpsim::search::SolveGoal::LenientWindows
                                                    : kpsim::search::SolveGoal::Balanced);
            o.maxShift = (int)Mod::get()->getSettingValue<int64_t>("window-max-shift");
            o.rules = humanRules();
            kpsim::search::optimizePlan(*m_engine, m_request.plan, o, m_opt);
        } else {
            kpsim::search::WindowOptions o;
            o.maxShift = (int)Mod::get()->getSettingValue<int64_t>("window-max-shift");
            o.strict = Mod::get()->getSettingValue<bool>("windows-strict");
            kpsim::search::findWindows(*m_engine, m_request.plan, o, m_windows);
        }
    }

    void update(float) override {
        if (m_panel && !m_panel->getParent() && !m_finished)
            if (auto* scene = m_layer->getParent()) scene->addChild(m_panel, kPanelZ);
        if (!m_begun) {
            if (!m_layer->m_started) return;   // intro still running
            m_begun = true;
            start();
            return;
        }
        if (!m_task || m_task->done() || m_layer->m_isPaused) return;
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
        if (m_limitMinutes > 0 && elapsed > m_limitMinutes * 60.0) m_engine->cancel();
        // About 60 frames a second: with the level hidden a frame costs little.
        auto t0 = std::chrono::steady_clock::now();
        const bool ended = m_task->resume(16.0);
        m_workMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (ended) {
            finish(false);
            return;
        }
        const auto& st = m_engine->stats();
        m_panel->set(m_engine->statusText(), m_engine->progressValue(), elapsed,
                     fmt::format("{:.0f}k steps/s   {} restores ({:.1f} ms)", elapsed > 0 ? st.steps / elapsed / 1000.0 : 0.0,
                                 st.restores, st.restores ? st.restoreMs / st.restores : 0.0));
    }

    void setLayersVisible(bool v) {
        if (m_layer->m_objectLayer) m_layer->m_objectLayer->setVisible(v);
        if (m_layer->m_inShaderObjectLayer) m_layer->m_inShaderObjectLayer->setVisible(v);
        if (m_layer->m_aboveShaderObjectLayer) m_layer->m_aboveShaderObjectLayer->setVisible(v);
    }

    void finish(bool cancelled) {
        std::string error;
        if (auto e = m_task->error()) {
            try {
                std::rethrow_exception(e);
            } catch (const kpsim::search::Cancelled&) {
                cancelled = true;
            } catch (const std::exception& ex) {
                error = ex.what();
            } catch (...) {
                error = "unknown error";
            }
        }
        EngineStats stats = m_engine->stats();
        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_started).count();
        logSummary(stats, elapsed, cancelled, error);
        m_engine.reset();   // engine off before touching the level
        updateModGuard();
        m_layer->m_level->m_attempts = m_savedAttempts;
        m_layer->m_level->m_jumps = m_savedJumps;
        m_layer->m_isTestMode = m_savedTestMode;
        m_layer->m_clickBetweenSteps = m_savedCBS;
        m_layer->m_clickOnSteps = m_savedCOS;
        m_layer->m_checkpointArray->removeAllObjects();
        setLayersVisible(true);
        m_finished = true;
        m_panel->removeFromParent();
        if (!m_quitting) {
            if (!m_layer->m_isPaused) m_layer->resetLevel();
            report(cancelled, error, stats, elapsed);
        }
        m_task.reset();
    }

    void logSummary(const EngineStats& st, double elapsed, bool cancelled, const std::string& error) {
        GJGameLevel* level = m_layer->m_level;
        const auto fp = kpsim::search::windowStats(m_windows, 1.0);
        std::string what = m_request.kind == JobKind::Optimize
                               ? fmt::format("optimize finished={} verified={} clicks {}->{} inputs {}->{} removed={} "
                                             "merged={} centered={} rejected={} narrowest {}->{} trials={}",
                                             m_opt.finishedBefore, m_opt.verified, m_opt.clicksBefore,
                                             m_opt.plan.presses(), m_opt.inputsBefore, m_opt.plan.inputs(),
                                             m_opt.removed, m_opt.merged, m_opt.centered, m_opt.rejected,
                                             m_opt.narrowestBefore, m_opt.narrowestAfter, m_opt.trials)
                           : m_request.kind == JobKind::FindPath
                               ? fmt::format("path solved={} furthest={} clicks={} trials={} cut={} backtracks={}",
                                             m_path.solved, m_path.furthest, m_path.plan.presses(), m_path.trials,
                                             m_path.pruned, m_path.backtracks)
                               : fmt::format("windows edges={} trials={} cut={} complete={} followed={} "
                                             "frame-perfect 60/120/240 fps={:.0f}/{:.0f}/{} nandl-precision={:.1f}",
                                             m_windows.edges.size(), m_windows.trials, m_windows.pruned,
                                             m_windows.complete, m_windows.followed, fp.perfectAt[0],
                                             fp.perfectAt[1], fp.framePerfect, fp.precision);
        log::info("[kp] {} ({}) {}{}{} | {:.1f}s, {} steps ({:.0f}k/s), {} saves, {} restores ({:.2f} ms avg), "
                  "{} snapshot mismatches",
                  std::string(level->m_levelName), (int)level->m_levelID, what, cancelled ? " cancelled" : "",
                  error.empty() ? "" : " error: " + error, elapsed, st.steps,
                  elapsed > 0 ? st.steps / elapsed / 1000.0 : 0.0, st.saves, st.restores,
                  st.restores ? st.restoreMs / st.restores : 0.0, st.mismatches);
        // Where the time went.
        log::info("[kp] time: restores {:.1f}s, physics {:.1f}s ({:.1f} us/step, {} updates), saves {:.1f}s, "
                  "search {:.1f}s, frames {:.1f}s",
                  st.restoreMs / 1000.0, st.updateMs / 1000.0, st.steps ? st.updateMs * 1000.0 / st.steps : 0.0,
                  st.updates, st.saveMs / 1000.0,
                  std::max(0.0, m_workMs - st.restoreMs - st.updateMs - st.saveMs) / 1000.0,
                  std::max(0.0, elapsed - m_workMs / 1000.0));
    }

    void report(bool cancelled, const std::string& error, const EngineStats& st, double elapsed) {
        GJGameLevel* level = m_layer->m_level;
        std::string note = fmt::format("\n{:.0f}s, {:.0f}k steps/s, {} restores ({:.1f} ms).", elapsed,
                                       elapsed > 0 ? st.steps / elapsed / 1000.0 : 0.0, st.restores,
                                       st.restores ? st.restoreMs / st.restores : 0.0);
        if (st.mismatches)
            note += fmt::format("\n<cy>{} snapshots replayed differently and were dropped.</c>", st.mismatches);
        bool trusted = m_request.kind == JobKind::FindPath ? m_path.snapshotsTrusted : m_windows.snapshotsTrusted;
        if (!trusted) note += "\n<cy>Snapshots were not exact here: runs restarted from the level start.</c>";
        if (m_request.kind == JobKind::FindPath && m_path.updateSensitive)
            note += "\n<cr>The path only works with several physics steps per frame.</c>";
        if (!error.empty()) {
            FLAlertLayer::create("Kairos", "<cr>Stopped:</c> " + error, "OK")->show();
            return;
        }
        if (m_request.kind == JobKind::Optimize) {
            if (!m_opt.finishedBefore) {
                FLAlertLayer::create("Kairos", "The solution does not finish the level here." + note, "OK")->show();
                return;
            }
            if (!m_opt.verified) {
                FLAlertLayer::create("Kairos",
                                     "<cr>No simpler plan replayed from the start; nothing was saved.</c>" + note,
                                     "OK")->show();
                return;
            }
            if (m_opt.rejected)
                note += fmt::format("\n<cy>{} changes did not hold up in the full replay and were dropped.</c>",
                                    m_opt.rejected);
            auto path = solutionPath(level);
            kpsim::macro::writeFile(path, kpsim::macro::writeGdr2(m_opt.plan, (uint32_t)level->m_levelID,
                                                                   std::string(level->m_levelName)));
            // The solver's Replay plays it too; its raw plan is kept beside.
            {
                auto solver = solverSolutionPath(level);
                auto raw = solver;
                raw.replace_extension(".raw.txt");
                std::error_code ec;
                if (std::filesystem::exists(solver) && !std::filesystem::exists(raw))
                    std::filesystem::copy_file(solver, raw, ec);
                std::ofstream(solver) << kpsim::macro::writeSolverPlan(m_opt.plan);
            }
            Plan plan = m_opt.plan;
            PlayLayer* layer = m_layer;
            auto ticks = [](int w) { return w < 0 ? std::string("comfortable") : fmt::format("{} ticks", w); };
            // Still faster than the human limits somewhere: where first, and what it takes.
            const std::string breach = limitBreach(m_opt.plan);
            std::string human = breach.empty() ? std::string()
                                               : fmt::format("\n<cy>Still beyond your limits ({} places). {}</c>",
                                                             m_opt.violations, breach);
            createQuickPopup("Optimized",
                             fmt::format("<cg>Verified</c>: {} -> {} clicks, {} -> {} inputs ({} removed, {} merged, "
                                         "{} made human).\nNarrowest window: {} -> {} ({} timings centered).\nSaved to {}{}{}",
                                         m_opt.clicksBefore, m_opt.plan.presses(), m_opt.inputsBefore,
                                         m_opt.plan.inputs(), m_opt.removed, m_opt.merged, m_opt.humanized,
                                         ticks(m_opt.narrowestBefore), ticks(m_opt.narrowestAfter), m_opt.centered,
                                         path.string(), human, note),
                             "OK", "Watch", [plan, layer](FLAlertLayer*, bool watch) { if (watch) startWatch(layer, plan); });
            return;
        }
        if (m_request.kind == JobKind::FindPath) {
            auto path = solutionPath(level);
            if (m_path.plan.presses() || m_path.solved)
                kpsim::macro::writeFile(path, kpsim::macro::writeGdr2(m_path.plan, (uint32_t)level->m_levelID,
                                                                       std::string(level->m_levelName)));
            if (m_path.solved) {
                Plan plan = m_path.plan;
                PlayLayer* layer = m_layer;
                createQuickPopup("Path found",
                                 fmt::format("<cg>Verified</c> from the start: {} clicks, {} trials.\nSaved to {}{}",
                                             m_path.plan.presses(), m_path.trials, path.string(), note),
                                 "OK", "Watch",
                                 [plan, layer](FLAlertLayer*, bool watch) { if (watch) startWatch(layer, plan); });
            } else {
                FLAlertLayer::create("Kairos",
                                     fmt::format("{} at tick {} ({} trials).\nBest attempt saved to {}{}",
                                                 cancelled ? "Stopped" : "No path found", m_path.furthest,
                                                 m_path.trials, path.string(), note),
                                     "OK")->show();
            }
        } else {
            auto path = reportPath(level);
            std::string text = windowsReport(level, m_request, m_windows);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream(path) << text;
            std::ofstream(statsPath(level))
                << kpsim::search::writeWindowFile(m_windows, m_request.source, m_request.plan.fingerprint());
            const auto st = kpsim::search::windowStats(m_windows, 1.0);
            auto windows = m_windows;
            auto source = m_request.source;
            createQuickPopup("Frame windows",
                             fmt::format("{}{} edges. Frame-perfect at 60 / 120 / 240 FPS: {:.0f} / {:.0f} / {}.\n"
                                         "Base run {} at tick {}.\nReport: {}{}",
                                         m_windows.complete ? "" : "<cy>Partial:</c> ", m_windows.edges.size(),
                                         st.perfectAt[0], st.perfectAt[1], st.framePerfect,
                                         outcomeName(m_windows.base.outcome), m_windows.base.tick, path.string(), note),
                             "OK", "Stats", [windows, source](FLAlertLayer*, bool stats) {
                                 if (stats) showWindowStats(windows, source);
                             });
        }
    }

public:
    bool m_quitting = false;

private:
    PlayLayer* m_layer = nullptr;
    JobRequest m_request;
    Ref<ProgressPanel> m_panel;
    bool m_finished = false;
    std::unique_ptr<GameEngine> m_engine;
    std::unique_ptr<FiberTask> m_task;
    kpsim::search::PathResult m_path;
    kpsim::search::OptimizeResult m_opt;
    kpsim::search::WindowReport m_windows;
    std::chrono::steady_clock::time_point m_started;
    double m_limitMinutes = 0, m_workMs = 0;
    bool m_begun = false;
    bool m_savedTestMode = false, m_savedCBS = false, m_savedCOS = false;
    int m_savedAttempts = 0, m_savedJumps = 0;
};

JobRunner* g_runner = nullptr;

} // namespace

bool watchActive() { return g_watch.on; }

kpsim::search::WindowReport windowsFor(GJGameLevel* level, const Plan& plan) {
    kpsim::search::WindowReport r;
    std::ifstream in(statsPath(level));
    if (!in) return r;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string macro;
    uint64_t measured = 0;
    if (!kpsim::search::readWindowFile(ss.str(), r, macro, measured) || measured != plan.fingerprint()) return {};
    return r;
}

std::filesystem::path outputDir() { return Mod::get()->getSaveDir(); }
std::filesystem::path solutionPath(GJGameLevel* level) { return outputDir() / "solutions" / (fileStem(level) + ".gdr2"); }
std::filesystem::path solverSolutionPath(GJGameLevel* level) {
    return outputDir() / ("solution_lv" + std::to_string((int)level->m_levelID) + "_dp.txt");
}
std::filesystem::path statsPath(GJGameLevel* level) { return outputDir() / "windows" / (fileStem(level) + ".kw"); }
std::filesystem::path reportPath(GJGameLevel* level) { return outputDir() / "windows" / (fileStem(level) + ".txt"); }

void startJob(GJGameLevel* level, JobRequest request) {
    g_pending.active = true;
    g_pending.levelId = (int)level->m_levelID;
    g_pending.request = std::move(request);
    auto* scene = PlayLayer::scene(level, false, false);
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

} // namespace kp

using namespace kp;

class $modify(KpJobPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        g_watch = Watch{};
        g_runner = nullptr;
        if (g_pending.active && level && (int)level->m_levelID == g_pending.levelId) {
            g_pending.active = false;
            if (g_pending.request.kind == JobKind::Watch) {
                Plan plan = planFor(this, g_pending.request);
                startWatch(this, plan);
                startRhythm(this, plan, g_pending.request.windows, true);
            } else if (g_pending.request.kind == JobKind::Rhythm) {
                startRhythm(this, planFor(this, g_pending.request), g_pending.request.windows, false);
            } else if (auto* r = JobRunner::create(this, std::move(g_pending.request))) {
                g_runner = r;
                this->addChild(r, 10000);
            }
        }
        return true;
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        if (g_watch.on && static_cast<GJBaseGameLayer*>(this) == g_watch.layer) { g_watch.tick = 0; g_watch.held[0] = g_watch.held[1] = false; }
    }
    void onQuit() {
        if (g_runner) {
            g_runner->m_quitting = true;
            g_runner->cancelNow();
            g_runner = nullptr;
        }
        g_watch = Watch{};
        PlayLayer::onQuit();
    }
};

class $modify(KpWatchLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (g_watch.on && this == g_watch.layer && !GameEngine::active()) {
            ++g_watch.tick;
            for (int p = 0; p < 2; ++p) {
                bool want = g_watch.plan.heldAt(g_watch.tick, p);
                if (want == g_watch.held[p]) continue;
                g_watch.injecting = true;
                this->handleButton(want, 1, (p == 0) != g_watch.flip);
                g_watch.injecting = false;
                g_watch.held[p] = want;
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
    void handleButton(bool down, int button, bool isPlayer1) {
        if (g_watch.on && this == g_watch.layer && !g_watch.injecting) return;
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
