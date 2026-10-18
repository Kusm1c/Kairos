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
    // Play it slow: from handFrom the player's own clicks, recorded into `played`.
    bool hand = false, twoPlayers = false;
    Tick handFrom = 0;
    Plan played;
    bool playedHeld[2] = {};
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

class DrawPad : public CCLayer {
public:
    using Line = std::vector<std::pair<float, float>>;
    struct Drawing {
        Line line;
        std::vector<Line> walls;
    };
    static DrawPad* create(PlayLayer* layer, std::function<void(Drawing)> go, std::function<void()> cancel) {
        auto* p = new DrawPad();
        if (p->init(layer, std::move(go), std::move(cancel))) {
            p->autorelease();
            return p;
        }
        delete p;
        return nullptr;
    }

    // The pad off and the level running again.
    void close() {
        CCTouchDispatcher::get()->removeDelegate(this);
        CCDirector::get()->getScheduler()->setTimeScale(1.f);
        if (m_ink) m_ink->removeFromParent();
        m_ink = nullptr;
        this->removeFromParent();
    }

private:
    enum Button { kGo, kTool, kClear, kCancel };

    bool init(PlayLayer* layer, std::function<void(Drawing)> go, std::function<void()> cancel) {
        if (!CCLayer::init()) return false;
        m_layer = layer;
        m_go = std::move(go);
        m_cancel = std::move(cancel);
        const auto win = CCDirector::get()->getWinSize();
        auto button = [&](const char* text, const char* bg, float x) {
            auto* spr = ButtonSprite::create(text, "goldFont.fnt", bg, 0.8f);
            spr->setScale(0.6f);
            spr->setPosition({x, 26.f});
            this->addChild(spr);
            m_buttons.push_back(spr);
            return spr;
        };
        button("Find route", "GJ_button_01.png", win.width / 2 + 165.f);
        m_tool = button("Draw: line", "GJ_button_02.png", win.width / 2 + 55.f);
        button("Clear", "GJ_button_04.png", win.width / 2 - 55.f);
        button("Cancel", "GJ_button_06.png", win.width / 2 - 165.f);
        m_ink = CCDrawNode::create();
        layer->m_objectLayer->addChild(m_ink, 1000);
        CCDirector::get()->getScheduler()->setTimeScale(0.f);   // the level frozen while drawing
        CCTouchDispatcher::get()->addTargetedDelegate(this, -600, true);
        return true;
    }

    int hit(CCPoint at) const {
        for (size_t i = 0; i < m_buttons.size(); ++i)
            if (m_buttons[i]->boundingBox().containsPoint(at)) return (int)i;
        return -1;
    }

    // The line goes on from where it stopped; each wall is its own stroke.
    void add(CCTouch* t, bool stroke) {
        if (m_walling && stroke) m_drawing.walls.emplace_back();
        Line& l = m_walling ? m_drawing.walls.back() : m_drawing.line;
        const CCPoint w = m_layer->m_objectLayer->convertToNodeSpace(t->getLocation());
        if (!l.empty()) {
            const CCPoint last{l.back().first, l.back().second};
            if (ccpDistance(last, w) < 4.f) return;
            m_ink->drawSegment(last, w, 2.5f, m_walling ? ccc4f(1.f, 0.3f, 0.25f, 0.95f) : ccc4f(0.3f, 1.f, 1.f, 0.9f));
        }
        l.push_back({w.x, w.y});
    }

    bool drawn() const {
        if (m_drawing.line.size() >= 2) return true;
        for (const auto& w : m_drawing.walls)
            if (w.size() >= 2) return true;
        return false;
    }

    bool ccTouchBegan(CCTouch* t, CCEvent*) override {
        m_pressed = hit(t->getLocation());
        if (m_pressed < 0) add(t, true);
        return true;
    }
    void ccTouchMoved(CCTouch* t, CCEvent*) override {
        if (m_pressed < 0) add(t, false);
    }
    void ccTouchEnded(CCTouch* t, CCEvent*) override {
        const int b = m_pressed;
        m_pressed = -1;
        if (b < 0 || hit(t->getLocation()) != b) return;
        if (b == kTool) {
            m_walling = !m_walling;
            m_tool->setString(m_walling ? "Draw: wall" : "Draw: line");
            return;
        }
        if (b == kClear) {
            m_drawing = {};
            m_ink->clear();
            return;
        }
        if (b == kGo && !drawn()) return;
        Ref<DrawPad> keep = this;
        auto go = m_go;
        auto cancel = m_cancel;
        Drawing drawing = m_drawing;
        close();
        if (b == kGo) go(std::move(drawing));
        else cancel();
    }

    PlayLayer* m_layer = nullptr;
    std::function<void(Drawing)> m_go;
    std::function<void()> m_cancel;
    std::vector<CCNode*> m_buttons;   // as Button
    ButtonSprite* m_tool = nullptr;
    CCDrawNode* m_ink = nullptr;
    Drawing m_drawing;
    bool m_walling = false;
    int m_pressed = -1;
};

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
        if (m_pad) m_pad->close();
        m_pad = nullptr;
        CCDirector::get()->getScheduler()->setTimeScale(1.f);
        this->unscheduleUpdate();
        m_state = State::Done;
        if (m_ui) m_ui->removeFromParent();
        m_ui = nullptr;
        m_title = m_label = nullptr;   // went with m_ui
        m_menu = m_handMenu = nullptr;
        m_layer->m_level->m_attempts = m_savedAttempts;
        m_layer->m_level->m_jumps = m_savedJumps;
        g_watch = Watch{};
        if (g_check == this) g_check = nullptr;
        updateModGuard();
    }

private:
    enum class State { Starting, Playing, Pausing, Done, ToDraw, Drawing, Hand, HandDied };

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
        m_menu = menu;
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
        // A route drawn by hand; and at 1 to 3 ticks, the inputs around it may take a wider path.
        const auto& w = m_tests.edge;
        const bool tight = w.early + w.late + 1 <= 3;
        auto wide = [&](const char* text, SEL_MenuHandler sel, float x) {
            auto* spr = ButtonSprite::create(text, "goldFont.fnt", "GJ_button_01.png", 0.8f);
            spr->setScale(0.7f);
            auto* b = CCMenuItemSpriteExtra::create(spr, this, sel);
            b->setPosition({x, 28.f});
            menu->addChild(b);
        };
        const float gap = tight ? 165.f : 90.f;
        wide("Draw a route", menu_selector(CheckViewer::onDraw), win.width / 2 - gap);
        wide("Play it slow", menu_selector(CheckViewer::onPlaySlow), tight ? win.width / 2 : win.width / 2 + gap);
        if (tight) wide("Find better route", menu_selector(CheckViewer::onBetter), win.width / 2 + gap);
        // Play it slow: its own buttons.
        m_handMenu = CCMenu::create();
        m_handMenu->setPosition({0.f, 0.f});
        m_handMenu->setVisible(false);
        m_ui->addChild(m_handMenu);
        auto hand = [&](const char* text, const char* bg, SEL_MenuHandler sel, float x) {
            auto* spr = ButtonSprite::create(text, "goldFont.fnt", bg, 0.8f);
            spr->setScale(0.7f);
            auto* b = CCMenuItemSpriteExtra::create(spr, this, sel);
            b->setPosition({x, 28.f});
            m_handMenu->addChild(b);
        };
        hand("Cancel", "GJ_button_06.png", menu_selector(CheckViewer::onHandCancel), win.width / 2 - 150.f);
        hand("Again", "GJ_button_04.png", menu_selector(CheckViewer::onPlaySlow), win.width / 2);
        hand("Use my run", "GJ_button_01.png", menu_selector(CheckViewer::onUseRun), win.width / 2 + 150.f);
    }

    void onPlaySlow(CCObject*) {
        if (m_state == State::Starting || m_state == State::ToDraw || m_state == State::Drawing) return;
        CCDirector::get()->getScheduler()->setTimeScale(1.f);
        play(0);
        if (m_state != State::Playing) return;
        const Plan& plan = m_tests.tests.front().plan;
        const Tick edge = m_tests.edge.tick;
        Tick from = edge > 30 ? edge - 30 : 1;
        while (from > m_from + 1 && plan.anyHeldAt(from)) --from;
        while (plan.anyHeldAt(from) && from < edge + 240) ++from;   // held all along: after its release
        g_watch.hand = true;
        g_watch.twoPlayers = twoPlayer(m_layer);
        g_watch.handFrom = from;
        g_watch.played = plan;
        for (kpsim::macro::Holds* h : {&g_watch.played.holds, &g_watch.played.holds2})
            while (!h->empty() && h->back().press >= from) h->pop_back();
        g_watch.playedHeld[0] = g_watch.playedHeld[1] = false;
        m_stopAt = std::max(edge, from) + 600;
        if (m_menu) m_menu->setVisible(false);
        if (m_handMenu) m_handMenu->setVisible(true);
        setText("Your turn, at 25 % speed",
                fmt::format("From {:.2f} s, play it yourself; Use my run once through", from / 240.0), {120, 255, 160});
        CCDirector::get()->getScheduler()->setTimeScale(0.25f);
        m_state = State::Hand;
    }

    void onHandCancel(CCObject*) {
        if (m_state != State::Hand && m_state != State::HandDied) return;
        CCDirector::get()->getScheduler()->setTimeScale(1.f);
        if (m_handMenu) m_handMenu->setVisible(false);
        if (m_menu) m_menu->setVisible(true);
        play(0);
    }

    void onUseRun(CCObject*) {
        if (m_state != State::Hand) return;   // alive only
        if (g_watch.tick <= g_watch.handFrom) return;
        Plan played = g_watch.played;
        for (kpsim::macro::Holds* h : {&played.holds, &played.holds2})
            if (!h->empty() && h->back().release == kpsim::macro::kNever) h->back().release = g_watch.tick + 1;
        const Tick until = g_watch.tick;
        auto tests = m_tests;
        Ref<GJGameLevel> level = m_layer->m_level;
        stop();
        startPlayed(level, tests, std::move(played), until);
    }

    // The level shown a moment before the edge, then frozen for the drawing.
    void onDraw(CCObject*) {
        if (m_state == State::Starting || m_state == State::ToDraw || m_state == State::Drawing) return;
        play(0);
        if (m_state != State::Playing) return;
        if (m_menu) m_menu->setVisible(false);
        setText("Draw a route", "A line where the player should go (a rough guide), walls where it must not",
                {120, 220, 255});
        m_state = State::ToDraw;
        m_pause = 0.35f;
    }

    void startDrawing() {
        auto* scene = m_layer->getParent();
        Ref<CheckViewer> self = this;
        m_pad = DrawPad::create(
            m_layer,
            [self](DrawPad::Drawing d) {
                self->m_pad = nullptr;
                auto tests = self->m_tests;
                Ref<GJGameLevel> level = self->m_layer->m_level;
                self->stop();
                startGuide(level, tests, std::move(d.line), std::move(d.walls));
            },
            [self] {
                self->m_pad = nullptr;
                if (self->m_menu) self->m_menu->setVisible(true);
                self->play(0);
            });
        if (!m_pad || !scene) return;
        scene->addChild(m_pad, (1 << 21) + 1);
        m_state = State::Drawing;
    }

    void onBetter(CCObject*) {
        auto tests = m_tests;
        Ref<GJGameLevel> level = m_layer->m_level;
        stop();
        startReroute(level, tests);
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
        } else if (m_state == State::ToDraw) {
            if ((m_pause -= dt) <= 0.f) startDrawing();
        } else if (m_state == State::Hand) {
            if (m_layer->m_playerDied) {
                CCDirector::get()->getScheduler()->setTimeScale(1.f);
                m_state = State::HandDied;
                setText("You died", "Again, or Cancel", {255, 120, 100});
            } else if (g_watch.tick >= m_stopAt) {
                onUseRun(nullptr);   // far enough past the edge
            }
        }
    }

    PlayLayer* m_layer = nullptr;
    kpsim::search::EdgeTests m_tests;
    State m_state = State::Starting;
    size_t m_index = 0;
    Tick m_from = 1, m_stopAt = 0;
    float m_pause = 0;
    CCNode* m_ui = nullptr;
    CCMenu* m_menu = nullptr;
    CCMenu* m_handMenu = nullptr;
    DrawPad* m_pad = nullptr;
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
            case JobKind::Reroute: return "Find better route";
            case JobKind::Guide: return "Route along your line";
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
        } else if (m_request.kind == JobKind::Guide) {
            kpsim::search::GuideOptions o;
            o.points = m_request.guide;
            o.walls = m_request.guideWalls;
            if (m_request.playedUntil) {
                o.played = &m_request.played;
                o.playedUntil = m_request.playedUntil;
            }
            o.player = m_request.check.edge.player;
            o.drawnAt = m_request.check.edge.tick;
            o.rules = humanRules();
            kpsim::search::followGuide(*m_engine, m_request.plan, o, m_guide);
        } else if (m_request.kind == JobKind::Reroute) {
            const auto& w = m_request.check.edge;
            kpsim::search::OptimizeOptions o;
            o.maxShift = (int)Mod::get()->getSettingValue<int64_t>("window-cap");
            o.rules = humanRules();
            o.only = {w.player, w.tick, w.press};
            kpsim::search::optimizePlan(*m_engine, m_request.plan, o, m_opt);
        } else {
            kpsim::search::WindowOptions o;
            o.maxShift = (int)Mod::get()->getSettingValue<int64_t>("window-cap");
            o.strict = Mod::get()->getSettingValue<bool>("windows-strict");
            o.rules = humanRules();
            // Feasibility of model-guided windows: does the solver's model replay this plan?
            double secs = 0;
            std::string err;
            const long long died = modelReplay(modelDir(m_layer->m_level), m_request.plan, secs, err);
            log::info("[kp] model replay: {} ({:.1f}s)",
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
        const auto& edge = m_request.check.edge;
        std::string what = m_request.kind == JobKind::Guide
                               ? fmt::format("guide found={} verified={} differs {}-{} clicks {}->{} trials={}{}",
                                             m_guide.found, m_guide.verified, m_guide.from, m_guide.to,
                                             m_request.plan.presses(), m_guide.plan.presses(), m_guide.trials,
                                             m_guide.error.empty() ? "" : " (" + m_guide.error + ")") +
                                     (m_guide.log.empty() ? "" : "; " + m_guide.log)
                           : m_request.kind == JobKind::Reroute
                               ? fmt::format("better route {}#{} {} at {} found={} verified={} window {}->{} "
                                             "mean {:.2f}->{:.2f} clicks {}->{} routes={} trials={}",
                                             edge.player ? "P2 " : "", edge.hold + 1, edge.press ? "press" : "release",
                                             edge.tick, m_opt.rebatched, m_opt.verified, m_opt.targetBefore,
                                             m_opt.targetAfter, m_opt.batchMean[0], m_opt.batchMean[1],
                                             m_request.plan.presses(), m_opt.plan.presses(), m_opt.routesTried, m_opt.trials)
                           : m_request.kind == JobKind::Optimize
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
        log::info("[kp] checkpoint load: effects {:.1f}s, moves {:.1f}s, players {:.1f}s ({} audio restores "
                  "skipped), rest {:.1f}s; {:.0f} saved objects a load",
                  st.effectsMs / 1000.0, st.movesMs / 1000.0, st.playersMs / 1000.0, st.audioSkips,
                  std::max(0.0, st.loadMs - st.effectsMs - st.movesMs - st.playersMs) / 1000.0,
                  st.loads ? (double)st.savedObjects / st.loads : 0.0);
    }

    void reportGuide(bool cancelled, const std::string& note) {
        const char* title = m_request.playedUntil ? "Your run" : "Route along your line";
        if (!m_guide.verified) {
            FLAlertLayer::create(title,
                                 fmt::format("{}{} ({} tries).{}", cancelled ? "<cy>Stopped.</c> " : "",
                                             m_guide.error.empty() ? "No route along the line was found" : m_guide.error,
                                             m_guide.trials, note),
                                 "OK")->show();
            return;
        }
        GJGameLevel* level = m_layer->m_level;
        std::string saved;
        if (m_request.ofSolution) {
            saved = "\nSaved as the solution to " + saveSolution(level, m_guide.plan).string() + ".";
        } else {   // a macro: the new one beside the solutions
            auto path = outputDir() / "solutions" / (fileStem(level) + "-guided.gdr2");
            kpsim::macro::writeFile(path, kpsim::macro::writeGdr2(m_guide.plan, (uint32_t)level->m_levelID,
                                                                   std::string(level->m_levelName)));
            saved = "\nSaved to " + path.string() + ".";
        }
        saved += " Run <cy>Windows</c> again for its windows.";
        Plan plan = m_guide.plan;
        PlayLayer* layer = m_layer;
        const size_t walls = m_request.guideWalls.size();
        std::string drawn = m_request.playedUntil ? "your run joined back to the macro"
                            : m_request.guide.size() >= 2 ? "along your line" : "";
        if (walls) drawn += fmt::format("{}off your {} wall{}", drawn.empty() ? "" : ", ", walls, walls > 1 ? "s" : "");
        createQuickPopup(title,
                         fmt::format("<cg>Verified</c> from the start, {}: the macro changed from {:.2f} s to {:.2f} s, "
                                     "{} -> {} clicks.{}{}",
                                     drawn, m_guide.from / 240.0, m_guide.to / 240.0, m_request.plan.presses(),
                                     m_guide.plan.presses(), saved, note),
                         "OK", "Watch", [plan, layer](FLAlertLayer*, bool watch) { if (watch) startWatch(layer, plan); });
    }

    void reportReroute(bool cancelled, const std::string& note) {
        GJGameLevel* level = m_layer->m_level;
        const auto& w = m_request.check.edge;
        const std::string name = fmt::format("{}#{} {} at {:.2f} s", w.player ? "P2 " : "", w.hold + 1,
                                             w.press ? "press" : "release", w.tick / 240.0);
        if (!m_opt.finishedBefore || m_opt.targetBefore < 0) {
            FLAlertLayer::create("Kairos", "This plan no longer has " + name + " as measured. Run <cy>Windows</c> again." + note,
                                 "OK")->show();
            return;
        }
        if (!m_opt.rebatched || !m_opt.verified) {
            FLAlertLayer::create("Find better route",
                                 fmt::format("{}No route gives {} more room, re-timing the {} inputs before it or "
                                             "adding a click ({} versions compared). Its window stays {} ticks.{}",
                                             cancelled ? "<cy>Stopped.</c> " : "", name,
                                             kpsim::search::OptimizeOptions{}.batch, m_opt.routesTried,
                                             m_opt.targetBefore, note),
                                 "OK")->show();
            return;
        }
        std::string saved = "\n<cy>A macro's route: not saved as the solution.</c>";
        if (m_request.ofSolution) saved = "\nSaved as the solution to " + saveSolution(level, m_opt.plan).string() +
                                          ". Run <cy>Windows</c> again for all the windows.";
        Plan plan = m_opt.plan;
        PlayLayer* layer = m_layer;
        const int added = (int)m_opt.plan.presses() - (int)m_request.plan.presses();
        createQuickPopup("Better route",
                         fmt::format("<cg>Verified</c> from the start: {} now has {} ticks instead of {}, the inputs "
                                     "around it re-timed{} (mean window {:.1f} -> {:.1f} ticks).{}{}",
                                     name, m_opt.targetAfter, m_opt.targetBefore,
                                     added > 0 ? fmt::format(" and {} click{} added", added, added > 1 ? "s" : "") : "",
                                     m_opt.batchMean[0], m_opt.batchMean[1], saved, note),
                         "OK", "Watch", [plan, layer](FLAlertLayer*, bool watch) { if (watch) startWatch(layer, plan); });
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
        if (m_request.kind == JobKind::Reroute) {
            reportReroute(cancelled, note);
            return;
        }
        if (m_request.kind == JobKind::Guide) {
            reportGuide(cancelled, note);
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
    kpsim::search::GuideResult m_guide;
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

bool checksSolution(GJGameLevel* level, const Plan& plan) {
    if (isSolutionPlan(level, plan)) return true;
    std::ifstream in(statsPath(level));
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    kpsim::search::WindowReport r;
    std::string macro;
    uint64_t measured = 0;
    if (!kpsim::search::readWindowFile(ss.str(), r, macro, measured)) return false;
    return macro == solutionPath(level).filename().string() || macro == solverSolutionPath(level).filename().string();
}

void startCheck(GJGameLevel* level, const kpsim::search::EdgeTests& tests) {
    JobRequest req;
    req.kind = JobKind::Check;
    req.check = tests;
    startJob(level, std::move(req));
}

void startReroute(GJGameLevel* level, const kpsim::search::EdgeTests& tests) {
    if (tests.tests.empty()) return;
    JobRequest req;
    req.kind = JobKind::Reroute;
    req.check = tests;
    req.check.tests.resize(1);
    req.plan = req.merged = tests.tests.front().plan;
    req.ofSolution = checksSolution(level, req.plan);
    startJob(level, std::move(req));
}

void startGuide(GJGameLevel* level, const kpsim::search::EdgeTests& tests, std::vector<std::pair<float, float>> line,
                std::vector<std::vector<std::pair<float, float>>> walls) {
    if (tests.tests.empty()) return;
    JobRequest req;
    req.kind = JobKind::Guide;
    req.check = tests;
    req.check.tests.resize(1);
    req.plan = req.merged = tests.tests.front().plan;
    req.ofSolution = checksSolution(level, req.plan);
    req.guide = std::move(line);
    req.guideWalls = std::move(walls);
    startJob(level, std::move(req));
}

void startPlayed(GJGameLevel* level, const kpsim::search::EdgeTests& tests, Plan played, Tick until) {
    if (tests.tests.empty()) return;
    JobRequest req;
    req.kind = JobKind::Guide;
    req.check = tests;
    req.check.tests.resize(1);
    req.plan = req.merged = tests.tests.front().plan;
    req.ofSolution = checksSolution(level, req.plan);
    req.played = std::move(played);
    req.playedUntil = until;
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
                Plan plan = planFor(this, g_pending.request);
                if (g_pending.request.follow) startWatch(this, plan);
                startRhythm(this, plan, g_pending.request.windows, false, g_pending.request.follow);
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
            for (int p = 0; p < 2 && !(g_watch.hand && g_watch.tick >= g_watch.handFrom); ++p) {
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
        if (g_watch.on && this == g_watch.layer && !g_watch.injecting) {
            if (!g_watch.hand || g_watch.tick < g_watch.handFrom) {
                // Kept from the game; Rhythm in follow mode judges it.
                if (button == 1) {
                    const bool two = m_levelSettings && m_levelSettings->m_twoPlayerMode;
                    kp::rhythmFollowClick(this, two && isPlayer1 == g_watch.flip ? 1 : 0, down);
                }
                return;
            }
            // Played by hand: each click at the step it lands on.
            const int p = g_watch.twoPlayers && isPlayer1 == g_watch.flip ? 1 : 0;
            kpsim::macro::Holds& h = g_watch.played.channel(p);
            const kpsim::macro::Tick t = g_watch.tick;
            if (down && !g_watch.playedHeld[p]) {
                h.push_back({std::max<kpsim::macro::Tick>(t, h.empty() ? 1 : h.back().release + 1), kpsim::macro::kNever});
                g_watch.playedHeld[p] = true;
            } else if (!down && g_watch.playedHeld[p]) {
                h.back().release = std::max<kpsim::macro::Tick>(t, h.back().press + 1);
                g_watch.playedHeld[p] = false;
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
