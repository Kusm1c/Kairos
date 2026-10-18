#include "Jobs.hpp"
#include "LimitsPopup.hpp"
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
#include "UiText.hpp"
#include "kpsim/search/Timeline.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/MDPopup.hpp>
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
    std::chrono::steady_clock::time_point at;   // a request the level did not open for soon is stale
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

// Check: a narrow edge's tries, each fast-forwarded unseen to a second before the edge, then
// played at normal speed with what it is and what the measure found.
class CheckViewer;
CheckViewer* g_check = nullptr;

class CheckViewer : public CCNode {
public:
    static CheckViewer* create(PlayLayer* layer, kpsim::search::EdgeTests tests) {
        auto* v = new CheckViewer();
        if (v->init(layer, std::move(tests))) {
            v->autorelease();
            return v;
        }
        delete v;
        return nullptr;
    }

    // Leaves the level as it was (attempts, buttons).
    void stop() {
        if (m_ui) m_ui->removeFromParent();
        m_ui = nullptr;
        m_layer->m_level->m_attempts = m_savedAttempts;
        m_layer->m_level->m_jumps = m_savedJumps;
        g_watch = Watch{};
        if (g_check == this) g_check = nullptr;
        updateModGuard();
    }

private:
    enum class State { Starting, Playing, Pausing, Done };

    bool init(PlayLayer* layer, kpsim::search::EdgeTests tests) {
        if (!CCNode::init()) return false;
        m_layer = layer;
        m_tests = std::move(tests);
        if (!Mod::get()->getSettingValue<bool>("window-check-tests") && m_tests.tests.size() > 1)
            m_tests.tests.resize(1);
        m_savedAttempts = layer->m_level->m_attempts.value();
        m_savedJumps = layer->m_level->m_jumps.value();
        const auto& w = m_tests.edge;
        m_from = w.tick > 240 ? w.tick - 240 : 1;
        this->scheduleUpdate();
        return true;
    }

    std::string edgeName() const {
        const auto& w = m_tests.edge;
        return fmt::format("{}#{} {}", w.player ? "P2 " : "", w.hold + 1, w.press ? "press" : "release");
    }

    // Inputs of `p` other than the edge itself that differ from the plan as measured.
    int retimed(const Plan& p) const {
        const Plan& base = m_tests.tests.front().plan;
        int n = 0;
        for (int ch = 0; ch < 2; ++ch) {
            const auto& a = base.channel(ch);
            const auto& b = p.channel(ch);
            for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
                n += (a[i].press != b[i].press) + (a[i].release != b[i].release);
        }
        return std::max(0, n - 1);
    }

    std::string describe(size_t i) const {
        const auto& w = m_tests.edge;
        const auto& x = m_tests.tests[i];
        std::string head = fmt::format("{} at {:.2f} s - ", edgeName(), w.tick / 240.0);
        if (x.shift == 0)
            return head + fmt::format("as measured (window {} ticks: -{} / +{})", w.early + w.late + 1, w.early, w.late);
        const int k = std::abs(x.shift);
        std::string what = fmt::format("{} tick{} {}", k, k > 1 ? "s" : "", x.shift < 0 ? "earlier" : "later");
        const int n = retimed(x.plan);
        if (x.passed)
            return head + what + ": passes" + (n ? fmt::format(" (next {} input{} re-timed)", n, n > 1 ? "s" : "") : "");
        const int64_t after = (int64_t)x.death - ((int64_t)w.tick + x.shift);
        return head + what + fmt::format(": dies {} ticks after", after) +
               (n ? fmt::format(" (best of the next {} input{} re-timed)", n, n > 1 ? "s" : "") : "");
    }

    void buildUi() {
        auto* scene = m_layer->getParent();
        if (!scene || m_ui) return;
        const auto win = CCDirector::get()->getWinSize();
        m_ui = CCNode::create();
        scene->addChild(m_ui, 1 << 21);
        // As GD's own: an "Attempt"-style label, a gold line, the level browser's arrows.
        m_title = CCLabelBMFont::create("", "bigFont.fnt");
        m_title->setPosition({win.width / 2, win.height - 22.f});
        m_ui->addChild(m_title);
        m_label = CCLabelBMFont::create("", "goldFont.fnt");
        m_label->setPosition({win.width / 2, win.height - 46.f});
        m_ui->addChild(m_label);
        auto* menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_ui->addChild(menu);
        auto button = [&](const char* frame, SEL_MenuHandler sel, CCPoint at, bool flip) {
            auto* spr = CCSprite::createWithSpriteFrameName(frame);
            spr->setFlipX(flip);
            spr->setScale(0.8f);
            auto* b = CCMenuItemSpriteExtra::create(spr, this, sel);
            b->setPosition(at);
            menu->addChild(b);
        };
        button("GJ_arrow_03_001.png", menu_selector(CheckViewer::onPrev), {26.f, win.height / 2}, false);
        button("GJ_arrow_03_001.png", menu_selector(CheckViewer::onNext), {win.width - 26.f, win.height / 2}, true);
        button("GJ_replayBtn_001.png", menu_selector(CheckViewer::onAgain), {win.width - 26.f, win.height - 26.f}, false);
    }

    void setText(const std::string& title, const std::string& detail, ccColor3B c) {
        if (!m_label) return;
        const float w = CCDirector::get()->getWinSize().width - 120.f;
        m_title->setString(title.c_str());
        fitLabel(m_title, w, 0.7f);
        m_title->setColor(c);
        m_label->setString(detail.c_str());
        fitLabel(m_label, w, 0.6f);
    }

    // Try i: fast and unseen up to a second before the edge, then watched.
    void play(size_t i) {
        m_index = i;
        const auto& x = m_tests.tests[i];
        g_watch = Watch{};
        try {
            GameEngine e(m_layer);
            e.setShaderSkip(false);   // watched next
            updateModGuard();   // other mods' physics hooks off, as when measured
            e.reset();
            const auto r = e.run(x.plan, m_from);
            if (r.outcome != Outcome::Reached) throw kpsim::search::EngineError("the run ended before the edge");
        } catch (const std::exception& ex) {
            setText("Check stopped", ex.what(), {255, 100, 100});
            m_state = State::Done;
            updateModGuard();
            return;
        }
        g_watch.on = true;
        g_watch.plan = x.plan;
        g_watch.layer = m_layer;
        g_watch.flip = !m_layer->m_isPlatformer && GameManager::get()->getGameVariable("0010");
        g_watch.tick = m_from;
        for (int p = 0; p < 2; ++p) g_watch.held[p] = x.plan.heldAt(m_from, p);
        m_layer->m_isTestMode = true;
        m_layer->m_clickBetweenSteps = false;
        m_layer->m_clickOnSteps = true;
        updateModGuard();
        const auto& w = m_tests.edge;
        Tick last = w.tick;
        if (i) last = std::max<Tick>(last, kpsim::search::lastDifference(m_tests.tests.front().plan, x.plan));
        m_stopAt = x.passed ? last + 180 : x.death + 30;
        setText(fmt::format("Try {}/{}", i + 1, m_tests.tests.size()), describe(i),
                x.shift == 0 ? ccColor3B{255, 255, 255}
                : x.passed   ? ccColor3B{120, 255, 120}
                             : ccColor3B{255, 120, 100});
        m_state = State::Playing;
    }

    void next(size_t i) {
        if (i < m_tests.tests.size()) play(i);
        else {
            m_state = State::Done;
            setText("Done", fmt::format("{}: {} tries", edgeName(), m_tests.tests.size()), {200, 200, 255});
        }
    }

    void onPrev(CCObject*) { if (m_state != State::Starting) play(m_index ? m_index - 1 : 0); }
    void onAgain(CCObject*) { if (m_state != State::Starting) play(m_index); }
    void onNext(CCObject*) { if (m_state != State::Starting) next(m_index + 1); }

    void update(float dt) override {
        if (m_layer->m_isPaused) return;
        if (m_state == State::Starting) {
            if (!m_layer->m_started) return;   // intro
            buildUi();
            play(0);
            return;
        }
        if (m_state == State::Playing) {
            if (m_layer->m_playerDied || g_watch.tick >= m_stopAt) {
                m_state = State::Pausing;
                m_pause = m_layer->m_playerDied ? 1.2f : 0.6f;
            }
        } else if (m_state == State::Pausing) {
            if ((m_pause -= dt) <= 0.f) next(m_index + 1);
        }
    }

    PlayLayer* m_layer = nullptr;
    kpsim::search::EdgeTests m_tests;
    State m_state = State::Starting;
    size_t m_index = 0;
    Tick m_from = 1, m_stopAt = 0;
    float m_pause = 0;
    CCNode* m_ui = nullptr;
    CCLabelBMFont *m_title = nullptr, *m_label = nullptr;
    int m_savedAttempts = 0, m_savedJumps = 0;
};

const char* outcomeName(Outcome o) {
    return o == Outcome::Finished ? "finished" : o == Outcome::Died ? "died" : "alive";
}

// The .gdr2 and the solver's plan (Replay), whose first version stays as .raw.txt.
std::filesystem::path saveSolution(GJGameLevel* level, const Plan& plan) {
    auto path = solutionPath(level);
    kpsim::macro::writeFile(path, kpsim::macro::writeGdr2(plan, (uint32_t)level->m_levelID,
                                                           std::string(level->m_levelName)));
    auto solver = solverSolutionPath(level);
    auto raw = solver;
    raw.replace_extension(".raw.txt");
    std::error_code ec;
    if (std::filesystem::exists(solver) && !std::filesystem::exists(raw)) std::filesystem::copy_file(solver, raw, ec);
    std::ofstream(solver) << kpsim::macro::writeSolverPlan(plan);
    return path;
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
    if (!r.complete)
        out += fmt::format("Stopped after {} of {} edges: the rest was not measured\n", r.edges.size(), r.total);
    if (r.centered && r.complete)
        out += fmt::format("{} inputs moved to the middle of their window before the next was measured{}\n",
                           r.centered, r.verified ? "" : " (the result did not finish from the start)");
    // A death right after the input: truly that tight. Far later: the correction fell short.
    std::string fp;
    for (const auto& w : r.edges)
        for (int side = 0; side < 2; ++side) {
            const int room = side ? w.late : w.early, death = side ? w.lateDeath : w.earlyDeath;
            if (room == 0 && death >= 0)
                fp += fmt::format("  {} {} {}1 tick: died {} ticks later{}\n", name(w.player, w.hold),
                                  w.press ? "press" : "release", side ? "+" : "-", death,
                                  w.flying ? " (in flight: no recovering route found)"
                                  : w.ground == 0 ? " (pressed in the air: buffered)" : "");
        }
    if (!fp.empty()) out += "\nNo room on one side:\n" + fp;
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
        if (solverDriving()) return "A Solve or Replay of the solver is running on this level. Quit it, then start the job again.";
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
            o.maxShift = (int)Mod::get()->getSettingValue<int64_t>("window-cap");
            o.rules = humanRules();
            kpsim::search::optimizePlan(*m_engine, m_request.plan, o, m_opt);
        } else {
            kpsim::search::WindowOptions o;
            o.maxShift = (int)Mod::get()->getSettingValue<int64_t>("window-cap");
            o.strict = Mod::get()->getSettingValue<bool>("windows-strict");
            o.holds = m_request.firstClicks;
            if (o.holds) o.removals = false;
            o.rules = humanRules();
            // Feasibility of model-guided windows: does the solver's model replay this plan?
            double secs = 0;
            std::string err;
            const long long died = m_request.firstClicks ? -3 : modelReplay(modelDir(m_layer->m_level), m_request.plan, secs, err);
            if (died != -3) log::info("[kp] model replay: {} ({:.1f}s)",
                      died == -2 ? err : died < 0 ? std::string("survived") : fmt::format("died at step {}", died), secs);
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
                                             "merged={} centered={} widened={} rejected={} narrowest {}->{} trials={}",
                                             m_opt.finishedBefore, m_opt.verified, m_opt.clicksBefore,
                                             m_opt.plan.presses(), m_opt.inputsBefore, m_opt.plan.inputs(),
                                             m_opt.removed, m_opt.merged, m_opt.centered, m_opt.widened, m_opt.rejected,
                                             m_opt.narrowestBefore, m_opt.narrowestAfter, m_opt.trials)
                           : m_request.kind == JobKind::FindPath
                               ? fmt::format("path solved={} furthest={} clicks={} trials={} cut={} backtracks={}",
                                             m_path.solved, m_path.furthest, m_path.plan.presses(), m_path.trials,
                                             m_path.pruned, m_path.backtracks)
                               : fmt::format("windows edges={} trials={} cut={} complete={} followed={} "
                                             "centered={} verified={} retimed={} predicted={} splices={} "
                                             "proven={} prune-lies={} restore-lies={} moved={} steered={} far={} quick={} "
                                             "frame-perfect 60/120/240 fps={:.0f}/{:.0f}/{} nandl-precision={:.1f}",
                                             m_windows.edges.size(), m_windows.trials, m_windows.pruned,
                                             m_windows.complete, m_windows.followed, m_windows.centered,
                                             m_windows.verified, m_windows.retimed, m_windows.predicted,
                                             m_windows.splices, m_windows.proven, m_windows.pruneLies,
                                             m_windows.restoreLies, m_windows.moved, m_windows.steered, m_windows.farDeaths, m_windows.quick,
                                             fp.perfectAt[0],
                                             fp.perfectAt[1], fp.framePerfect, fp.precision);
        log::info("[kp] {} ({}) {}{}{} | {:.1f}s, {} steps ({:.0f}k/s), {} saves, {} restores ({:.2f} ms avg), "
                  "{} snapshot mismatches",
                  std::string(level->m_levelName), (int)level->m_levelID, what, cancelled ? " cancelled" : "",
                  error.empty() ? "" : " error: " + error, elapsed, st.steps,
                  elapsed > 0 ? st.steps / elapsed / 1000.0 : 0.0, st.saves, st.restores,
                  st.restores ? st.restoreMs / st.restores : 0.0, st.mismatches);
        // Where the time went.
        log::info("[kp] time: restores {:.1f}s (GD resetLevel {:.1f}s: checkpoint load {:.1f}s, variables {:.1f}s; "
                  "paste {:.1f}s), physics {:.1f}s ({:.1f} us/step, {} updates), saves {:.1f}s, search {:.1f}s, "
                  "frames {:.1f}s",
                  st.restoreMs / 1000.0, st.resetMs / 1000.0, st.loadMs / 1000.0, st.varsMs / 1000.0,
                  st.pasteMs / 1000.0, st.updateMs / 1000.0, st.steps ? st.updateMs * 1000.0 / st.steps : 0.0,
                  st.updates, st.saveMs / 1000.0,
                  std::max(0.0, m_workMs - st.restoreMs - st.updateMs - st.saveMs) / 1000.0,
                  std::max(0.0, elapsed - m_workMs / 1000.0));
        if (!m_opt.widenLog.empty()) log::info("[kp] widen:{}", m_opt.widenLog);
        log::info("[kp] checkpoint load: effects {:.1f}s, moves {:.1f}s, players {:.1f}s ({} audio restores "
                  "skipped), rest {:.1f}s; {:.0f} saved objects a load",
                  st.effectsMs / 1000.0, st.movesMs / 1000.0, st.playersMs / 1000.0, st.audioSkips,
                  std::max(0.0, st.loadMs - st.effectsMs - st.movesMs - st.playersMs) / 1000.0,
                  st.loads ? (double)st.savedObjects / st.loads : 0.0);
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
            auto path = saveSolution(level, m_opt.plan);
            Plan plan = m_opt.plan;
            PlayLayer* layer = m_layer;
            auto ticks = [](int w) { return w < 0 ? std::string("comfortable") : fmt::format("{} ticks", w); };
            // Still faster than the human limits somewhere: where first, and what it takes.
            const std::string breach = limitBreach(m_opt.plan);
            // The solver went past a wall without the limits: did the game take a version within them?
            if (!m_request.fitLimits.empty() && !breach.empty()) {
                showLimitsPopup(std::string(level->m_levelName),
                                fmt::format("The solver passed {} only with faster inputs than your limits, and the "
                                            "game refused every slower version Optimize tried. {} The solution is "
                                            "saved anyway.", m_request.fitLimits, breach));
                return;
            }
            if (!m_request.fitLimits.empty())
                note += fmt::format("\n<cg>The solver went past {} without your limits; Optimize brought it back "
                                    "within them.</c>", m_request.fitLimits);
            std::string human = breach.empty() ? std::string()
                                               : fmt::format("\n<cy>Still beyond your limits ({} places). {}</c>",
                                                             m_opt.violations, breach);
            createQuickPopup("Optimized",
                             fmt::format("<cg>Verified</c>: {} -> {} clicks, {} -> {} inputs ({} removed, {} merged, "
                                         "{} made human).\nNarrowest window: {} -> {} ({} timings centered, {} tight ones "
                                         "given room by a zigzag).\nSaved to {}{}{}",
                                         m_opt.clicksBefore, m_opt.plan.presses(), m_opt.inputsBefore,
                                         m_opt.plan.inputs(), m_opt.removed, m_opt.merged, m_opt.humanized,
                                         ticks(m_opt.narrowestBefore), ticks(m_opt.narrowestAfter), m_opt.centered,
                                         m_opt.widened,
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
        } else if (m_request.firstClicks) {
            // Each click in order, to compare with a frame stepper by hand.
            std::string text = fmt::format("Windows in 240 Hz ticks, the macro's clicks in order{}.\n\n",
                                           m_windows.complete ? "" : " (stopped)");
            for (size_t h = 0; h < m_request.firstClicks; ++h) {
                const kpsim::search::EdgeWindow *press = nullptr, *release = nullptr;
                for (const auto& w : m_windows.edges)
                    if (w.player == 0 && w.hold == h) (w.press ? press : release) = &w;
                if (!press) continue;
                auto side = [&](const kpsim::search::EdgeWindow& w) {
                    return fmt::format("**{}** (-{}{} / +{}{})", w.early + w.late + 1, w.early,
                                       w.earlyLimited ? "+" : "", w.late, w.lateLimited ? "+" : "");
                };
                text += fmt::format("{}. **{:.2f} s** - press {}{}{}\n\n", h + 1, press->tick / 240.0, side(*press),
                                    press->ground == 0 ? " *in the air (buffered)*"
                                    : press->movedBy ? fmt::format(" *moved {:+} ticks to a wider window*", press->movedBy)
                                                     : std::string(),
                                    release ? "   release " + side(*release) : std::string());
            }
            if (!Mod::get()->getSettingValue<bool>("window-markers"))
                text += "\nTurn on *Frame window markers* in the settings to see them by each click in Watch.\n";
            text += fmt::format("\n{} trials, {}", m_windows.trials, note);
            // Watch: the clicks replayed with their windows by them (setting "window-markers").
            Plan plan = m_windows.plan.presses() ? m_windows.plan : m_request.plan;
            auto windows = m_windows;
            PlayLayer* layer = m_layer;
            geode::MDPopup::create("First clicks", text, "OK", "Watch", [plan, windows, layer](bool watch) {
                if (!watch) return;
                startWatch(layer, plan);
                startRhythm(layer, plan, windows, true);
            })->show();
        } else {
            auto path = reportPath(level);
            std::string text = windowsReport(level, m_request, m_windows);
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream(path) << text;
            // The centred route replaces the solution once it finished from the start (not a macro's).
            const bool ofSolution = m_request.source == solutionPath(level).filename().string() ||
                                    m_request.source == solverSolutionPath(level).filename().string();
            const bool saved = ofSolution && m_windows.verified && m_windows.centered;
            if (saved) saveSolution(level, m_windows.plan);
            const Plan& measured = saved || !m_windows.centered ? m_windows.plan : m_request.plan;
            std::ofstream(statsPath(level))
                << kpsim::search::writeWindowFile(m_windows, m_request.source, measured.fingerprint());
            if (m_windows.centered && m_windows.complete && ofSolution)
                note += saved ? fmt::format("\n<cg>{} inputs moved to the middle of their window; saved as the "
                                            "solution.</c>", m_windows.centered)
                              : std::string("\n<cr>The centred route did not finish from the start; nothing was "
                                            "saved.</c>");
            const auto st = kpsim::search::windowStats(m_windows, 1.0);
            auto windows = m_windows;
            auto source = m_request.source;
            createQuickPopup("Frame windows",
                             fmt::format("{}{} edges. Frame-perfect at 60 / 120 / 240 FPS: {:.0f} / {:.0f} / {}.\n"
                                         "Base run {} at tick {}.\nReport: {}{}",
                                         m_windows.complete ? ""
                                                            : fmt::format("<cy>Stopped, {} of {} measured:</c> ",
                                                                          m_windows.edges.size(), m_windows.total),
                                         m_windows.edges.size(),
                                         st.perfectAt[0], st.perfectAt[1], st.framePerfect,
                                         outcomeName(m_windows.base.outcome), m_windows.base.tick, path.string(), note),
                             "OK", "Stats", [windows, source, lv = Ref<GJGameLevel>(level)](FLAlertLayer*, bool stats) {
                                 if (stats)
                                     showWindowStats(windows, source, {}, [lv](const kpsim::search::EdgeTests& t) {
                                         startCheck(lv, t);
                                     });
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
std::filesystem::path modelDir(GJGameLevel* level) { return outputDir() / "model" / std::to_string((int)level->m_levelID); }
std::filesystem::path statsPath(GJGameLevel* level) { return outputDir() / "windows" / (fileStem(level) + ".kw"); }
std::filesystem::path reportPath(GJGameLevel* level) { return outputDir() / "windows" / (fileStem(level) + ".txt"); }

void startCheck(GJGameLevel* level, const kpsim::search::EdgeTests& tests) {
    JobRequest req;
    req.kind = JobKind::Check;
    req.check = tests;
    startJob(level, std::move(req));
}

void startJob(GJGameLevel* level, JobRequest request) {
    g_pending.active = true;
    g_pending.levelId = (int)level->m_levelID;
    g_pending.request = std::move(request);
    g_pending.at = std::chrono::steady_clock::now();
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
        g_check = nullptr;
        if (g_pending.active && std::chrono::steady_clock::now() - g_pending.at > std::chrono::seconds(20))
            g_pending.active = false;
        if (g_pending.active && level && (int)level->m_levelID == g_pending.levelId) {
            g_pending.active = false;
            if (g_pending.request.kind == JobKind::Check) {
                if (auto* v = CheckViewer::create(this, std::move(g_pending.request.check))) {
                    g_check = v;
                    this->addChild(v, 10000);
                }
            } else if (g_pending.request.kind == JobKind::Watch) {
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
    void delayedResetLevel() {
        if (g_check) return;   // the viewer resets for the next try
        PlayLayer::delayedResetLevel();
    }
    void onQuit() {
        if (g_check) g_check->stop();
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
