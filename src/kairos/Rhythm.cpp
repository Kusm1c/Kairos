#include "Rhythm.hpp"
#include "UiText.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>

using namespace geode::prelude;
using kpsim::macro::Plan;
using kpsim::search::WindowReport;

namespace kp {
namespace {
constexpr double kSearch = 60.0;        // ticks a click looks around for its note
constexpr double kUnknownWindow = 3.0;  // judging window without measured frame windows
constexpr int kFreeRelease = 6;         // a release with this much room both ways is not judged
constexpr double kMsPerTick = 1000.0 / 240.0;

enum class Grade { None, Perfect, Good, Early, Late, Missed };

struct Note {
    int channel = 0;
    bool press = true;
    double tick = 0;
    double holdEnd = 0;   // press notes: where the hold ends
    double early = kUnknownWindow, late = kUnknownWindow;
    bool known = false;   // frame windows measured
    int width = 0;        // ticks
    bool limited = false; // a side stopped by the cap or a neighbour: at least `width`
    CCNode* marker = nullptr;
    bool judged = false;
    bool counts = true;   // judged at all (wide releases are not)
    Grade grade = Grade::None;
    double dev = 0;
};

ccColor4F rgba(int r, int g, int b, float a) { return {r / 255.f, g / 255.f, b / 255.f, a}; }

// Same scale as the stats menu: red frame-perfect to blue roomy.
ccColor4F widthColor(double width, float a) {
    if (width <= 1) return rgba(255, 70, 70, a);
    if (width <= 3) return rgba(255, 150, 60, a);
    if (width <= 6) return rgba(255, 220, 80, a);
    if (width <= 12) return rgba(170, 255, 110, a);
    return rgba(90, 220, 255, a);
}

ccColor3B gradeColor(Grade g) {
    switch (g) {
        case Grade::Perfect: return {120, 240, 255};
        case Grade::Good: return {140, 255, 120};
        case Grade::Early: case Grade::Late: return {255, 170, 70};
        case Grade::Missed: return {255, 80, 80};
        default: return {200, 200, 200};
    }
}

const char* gradeText(Grade g) {
    switch (g) {
        case Grade::Perfect: return "PERFECT";
        case Grade::Good: return "GOOD";
        case Grade::Early: return "EARLY";
        case Grade::Late: return "LATE";
        case Grade::Missed: return "MISS";
        default: return "";
    }
}

class RhythmOverlay;
RhythmOverlay* g_overlay = nullptr;

class RhythmOverlay : public CCNode {
public:
    static RhythmOverlay* create(PlayLayer* layer, const Plan& plan, const WindowReport& w, bool demo, bool follow) {
        auto* o = new RhythmOverlay();
        if (o->init(layer, plan, w, demo, follow)) {
            o->autorelease();
            return o;
        }
        delete o;
        return nullptr;
    }

    ~RhythmOverlay() override {
        if (g_overlay == this) g_overlay = nullptr;
        if (m_markerRoot) m_markerRoot->removeFromParent();
    }

    PlayLayer* layer() const { return m_layer; }

    void onStep(bool half) {
        if (!m_layer->m_playerDied) m_clock += half ? 0.5 : 1.0;
    }
    void onReset() {
        auto* cps = m_layer->m_checkpointArray;
        CheckpointObject* last = cps && cps->count() ? static_cast<CheckpointObject*>(cps->lastObject()) : nullptr;
        auto it = last ? m_cpClock.find(last) : m_cpClock.end();
        m_clock = it != m_cpClock.end() ? it->second : last ? m_layer->m_gameState.m_levelTime * 240.0 : 0.0;
    }
    void onCheckpoint(CheckpointObject* cp) { m_cpClock[cp] = m_clock; }

    // A real (or, in a demo, injected) jump click.
    void onClick(int channel, bool down) {
        const double t = now();
        Note* best = nullptr;
        for (auto& n : m_notes) {
            if (n.channel != channel || n.press != down || n.judged || !n.counts) continue;
            double d = std::abs(t - n.tick);
            if (d <= kSearch && (!best || d < std::abs(t - best->tick))) best = &n;
        }
        if (!best) {
            if (down) ++m_extra;
            return;
        }
        judge(*best, t - best->tick);
    }

    void onLevelEnd() {
        if (m_demo || m_summaryShown) return;
        m_summaryShown = true;
        std::string text = summary();
        this->runAction(CCSequence::create(CCDelayTime::create(1.2f), CallFuncExt::create([text] {
            FLAlertLayer::create(nullptr, "Rhythm results", text, "OK", nullptr, 420.f)->show();
        }), nullptr));
    }

private:
    bool init(PlayLayer* layer, const Plan& plan, const WindowReport& w, bool demo, bool follow) {
        if (!CCNode::init()) return false;
        m_layer = layer;
        m_demo = demo;
        m_follow = follow;
        m_pxPerTick = (float)Mod::get()->getSettingValue<int64_t>("rhythm-speed") / 240.f;
        buildNotes(plan, w);
        if (Mod::get()->getSettingValue<bool>("window-markers") && layer->m_objectLayer) {
            m_markerRoot = CCNode::create();
            layer->m_objectLayer->addChild(m_markerRoot, 1000);
        }

        const auto win = CCDirector::get()->getWinSize();
        m_lanes = plan.holds2.empty() ? 1 : 2;
        m_laneW = 20.f;
        m_trackX = 6.f;
        m_trackY = 44.f;
        m_trackH = win.height - m_trackY - 52.f;
        m_hitY = m_trackY + 36.f;

        m_draw = CCDrawNode::create();
        this->addChild(m_draw);

        m_grade = label("bigFont.fnt", 0.4f, {m_trackX, 30.f});
        m_offset = label("chatFont.fnt", 0.45f, {m_trackX, 14.f});
        m_hud = label("chatFont.fnt", 0.45f, {m_trackX, m_trackY + m_trackH + 14.f});
        m_hint = label("chatFont.fnt", 0.45f, {m_trackX, m_trackY + m_trackH + 30.f});
        setText(m_hint, m_known ? (demo     ? "Demo: the macro plays itself"
                                   : follow ? "The macro plays: click with the notes"
                                            : "Click with the notes")
                                : "No frame windows: judged on +-3 ticks");
        m_hint->setOpacity(170);
        m_hint->runAction(CCSequence::create(CCDelayTime::create(5.f), CCFadeOut::create(1.f), nullptr));
        this->scheduleUpdate();
        refreshHud();
        return true;
    }

    CCLabelBMFont* label(const char* font, float scale, CCPoint pos) {
        auto* l = CCLabelBMFont::create("", font);
        l->setScale(scale);
        l->setAnchorPoint({0.f, 0.5f});
        l->setPosition(pos);
        this->addChild(l, 2);
        return l;
    }

    // Kept clear of the player: never wider than kTextW.
    static constexpr float kTextW = 130.f;
    void setText(CCLabelBMFont* l, const std::string& s) {
        l->setString(s.c_str());
        fitLabel(l, kTextW, 0.45f);
    }

    void buildNotes(const Plan& plan, const WindowReport& w) {
        m_known = !w.edges.empty();
        for (int ch = 0; ch < 2; ++ch) {
            const auto& holds = plan.channel(ch);
            for (size_t i = 0; i < holds.size(); ++i) {
                const auto& h = holds[i];
                const double end = h.release == kpsim::macro::kNever ? h.press + 240.0 * 3600 : (double)h.release;
                Note press{ch, true, (double)h.press, end};
                Note release{ch, false, end, end};
                for (const auto& e : w.edges) {
                    if (e.player != ch || e.hold != i) continue;
                    Note& n = e.press ? press : release;
                    n.known = true;
                    n.width = e.early + e.late + 1;
                    n.limited = e.earlyLimited || e.lateLimited;
                    n.early = e.early + (e.earlyLimited ? 0.5 : 0.0);
                    n.late = e.late + (e.lateLimited ? 0.5 : 0.0);
                }
                release.counts = h.release != kpsim::macro::kNever && release.known &&
                                 (release.early < kFreeRelease || release.late < kFreeRelease);
                m_notes.push_back(press);
                if (h.release != kpsim::macro::kNever) m_notes.push_back(release);
            }
        }
        std::sort(m_notes.begin(), m_notes.end(), [](const Note& a, const Note& b) { return a.tick < b.tick; });
        for (const auto& n : m_notes) m_total += n.counts;
    }

    double now() const { return m_clock; }

    void judge(Note& n, double dev) {
        n.judged = true;
        n.dev = dev;
        const double side = dev < 0 ? n.early : n.late;
        const double inner = std::max(0.5, std::min(n.early, n.late) / 3.0);
        if (std::abs(dev) <= inner) n.grade = Grade::Perfect;
        else if (std::abs(dev) <= side + 0.5) n.grade = Grade::Good;
        else n.grade = dev < 0 ? Grade::Early : Grade::Late;
        record(n);
    }

    // The latest grade of every note: the run that got furthest, replayed parts included once.
    struct Tally {
        int perfect = 0, good = 0, off = 0, judged = 0, clicked = 0;
        double sum = 0, sumSq = 0;
    };
    Tally tally() const {
        Tally t;
        for (const auto& n : m_notes) {
            if (!n.counts || !n.judged) continue;
            ++t.judged;
            if (n.grade == Grade::Perfect) ++t.perfect;
            else if (n.grade == Grade::Good) ++t.good;
            else ++t.off;
            if (n.grade != Grade::Missed) {
                ++t.clicked;
                t.sum += n.dev;
                t.sumSq += n.dev * n.dev;
            }
        }
        return t;
    }

    void record(const Note& n) {
        const bool hit = n.grade == Grade::Perfect || n.grade == Grade::Good;
        m_combo = hit ? m_combo + 1 : 0;
        m_bestCombo = std::max(m_bestCombo, m_combo);
        if (n.grade != Grade::Missed) {
            m_recent.push_back({n.dev, n.early, n.late, n.grade});
            if (m_recent.size() > 14) m_recent.pop_front();
        }
        m_grade->setString(gradeText(n.grade));
        m_grade->setColor(gradeColor(n.grade));
        m_grade->stopAllActions();
        m_grade->setOpacity(255);
        m_grade->setScale(0.55f);
        m_grade->runAction(CCSpawn::create(CCEaseBackOut::create(CCScaleTo::create(0.18f, 0.45f)),
                                           CCSequence::create(CCDelayTime::create(0.45f), CCFadeOut::create(0.35f),
                                                              nullptr),
                                           nullptr));
        if (n.grade != Grade::Missed) {
            setText(m_offset, fmt::format("{:+.0f} ms ({:+.0f}t)  -{:.0f}/+{:.0f}", n.dev * kMsPerTick, n.dev,
                                          n.early, n.late));
            m_offset->setColor(gradeColor(n.grade));
        }
        refreshHud();
    }

    void refreshHud() {
        const Tally t = tally();
        const double acc = t.judged ? (t.perfect + 0.6 * t.good) * 100.0 / t.judged : 100.0;
        const double mean = t.clicked ? t.sum / t.clicked * kMsPerTick : 0.0;
        setText(m_hud, fmt::format("{}x  {:.1f}%  {:+.1f} ms  {}/{}", m_combo, acc, mean, t.judged, m_total));
    }

    void update(float) override {
        const double t = now();
        // Respawn or restart: what lies ahead is played again.
        if (t < m_lastNow - 0.5) {
            for (auto& n : m_notes)
                if (n.tick >= t - 0.5) {
                    n.judged = false;
                    n.grade = Grade::None;
                    if (n.marker) n.marker->removeFromParent();
                    n.marker = nullptr;
                }
            m_nextMark = 0;
            m_combo = 0;
            if (t < 1.0) {
                ++m_attempts;
                m_summaryShown = false;
            }
            refreshHud();
        }
        m_lastNow = t;
        // Notes nobody clicked.
        for (auto& n : m_notes) {
            if (n.judged || !n.counts || t <= n.tick + n.late + 6.0) continue;
            n.judged = true;
            n.grade = Grade::Missed;
            record(n);
        }
        placeMarkers(t);
        draw(t);
    }

    // By the player where each note's tick is reached: its window in frames.
    void placeMarkers(double t) {
        if (!m_markerRoot) return;
        for (; m_nextMark < m_notes.size() && m_notes[m_nextMark].tick <= t; ++m_nextMark) {
            Note& n = m_notes[m_nextMark];
            if (!n.known || n.marker) continue;
            auto* player = n.channel ? m_layer->m_player2 : m_layer->m_player1;
            if (!player) continue;
            const float r = n.press ? 8.f : 6.f;
            auto* node = CCNode::create();
            auto* dot = CCDrawNode::create();
            // drawDot is square here.
            auto disc = [&](float rad, ccColor4F c) {
                CCPoint v[24];
                for (int i = 0; i < 24; ++i)
                    v[i] = ccp(rad * std::cos(i * 0.2617994f), rad * std::sin(i * 0.2617994f));
                dot->drawPolygon(v, 24, c, 0.f, c);
            };
            disc(r + 1.f, rgba(0, 0, 0, 0.6f));
            disc(r, widthColor(n.width, n.press ? 0.95f : 0.75f));
            node->addChild(dot);
            auto* l = CCLabelBMFont::create(fmt::format("{}{}", n.width, n.limited ? "+" : "").c_str(), "bigFont.fnt");
            fitLabel(l, r * 1.6f, 0.3f, 0.1f);
            node->addChild(l);
            const auto pos = player->getPosition();
            node->setPosition({pos.x, pos.y + (n.press ? 18.f : -18.f)});
            m_markerRoot->addChild(node);
            n.marker = node;
        }
    }

    float yOf(double tick, double t) const { return m_hitY + (float)((tick - t) * m_pxPerTick); }
    float top() const { return m_trackY + m_trackH; }

    // Clipped to the track's height.
    void rect(float x0, float y0, float x1, float y1, ccColor4F fill) {
        if (y1 < y0) std::swap(y0, y1);
        y0 = std::max(y0, m_trackY);
        y1 = std::min(y1, top());
        if (y1 <= y0 || x1 <= x0) return;
        CCPoint v[4] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        m_draw->drawPolygon(v, 4, fill, 0.f, fill);
    }

    void draw(double t) {
        m_draw->clear();
        const float right = m_trackX + m_lanes * m_laneW;
        // Panel and lanes.
        rect(m_trackX - 2.f, m_trackY, right + 2.f, top(), rgba(12, 14, 24, 0.72f));
        for (int l = 1; l < m_lanes; ++l)
            rect(m_trackX + l * m_laneW - 0.5f, m_trackY, m_trackX + l * m_laneW + 0.5f, top(),
                 rgba(255, 255, 255, 0.12f));

        const double from = t - (m_hitY - m_trackY) / m_pxPerTick - 1.0;
        const double to = t + (top() - m_hitY) / m_pxPerTick + 1.0;
        for (const auto& n : m_notes) {
            if (n.press && n.holdEnd < from) continue;
            if (!n.press && n.tick < from) continue;
            if (n.tick > to) break;
            const int lane = m_lanes == 2 ? n.channel : 0;
            const float x0 = m_trackX + lane * m_laneW + 3.f, x1 = x0 + m_laneW - 6.f;
            const float y = yOf(n.tick, t);
            if (n.press) {   // the hold, then its press
                const ccColor4F bar = n.channel ? rgba(255, 170, 80, 0.30f) : rgba(80, 200, 255, 0.30f);
                rect(x0 + 4.f, y, x1 - 4.f, yOf(n.holdEnd, t), bar);
            }
            if (n.counts) {
                const double width = n.early + n.late + 1.0;
                rect(x0, yOf(n.tick - n.early - 0.5, t), x1, yOf(n.tick + n.late + 0.5, t),
                     n.known ? widthColor(width, n.judged ? 0.18f : 0.42f) : rgba(180, 180, 200, 0.25f));
            }
            ccColor4F mark = rgba(255, 255, 255, n.counts ? 0.95f : 0.4f);
            if (n.judged) {
                auto c = gradeColor(n.grade);
                mark = rgba(c.r, c.g, c.b, 0.95f);
            }
            const float h = n.press ? 2.f : 1.f;
            rect(n.press ? x0 - 1.f : x0 + 3.f, y - h, n.press ? x1 + 1.f : x1 - 3.f, y + h, mark);
        }
        // Hit line.
        rect(m_trackX - 2.f, m_hitY - 3.f, right + 2.f, m_hitY + 3.f, rgba(255, 255, 255, 0.18f));
        rect(m_trackX - 2.f, m_hitY - 1.f, right + 2.f, m_hitY + 1.f, rgba(255, 255, 255, 0.95f));
        drawErrorBar(right + 6.f);
    }

    // Beside the hit line: the last window and the latest offsets in it (early above).
    void drawErrorBar(float x) {
        if (m_recent.empty()) return;
        const float cy = m_hitY, px = 3.f;
        const auto& last = m_recent.back();
        rect(x + 2.f, cy - (float)(last.late + 0.5) * px, x + 6.f, cy + (float)(last.early + 0.5) * px,
             widthColor(last.early + last.late + 1.0, 0.55f));
        rect(x - 1.f, cy - 0.75f, x + 9.f, cy + 0.75f, rgba(255, 255, 255, 0.9f));
        float a = 0.25f;
        for (const auto& r : m_recent) {
            a = std::min(1.f, a + 0.06f);
            auto c = gradeColor(r.grade);
            const float y = cy - (float)r.dev * px;
            rect(x - 2.f, y - 1.f, x + 10.f, y + 1.f, rgba(c.r, c.g, c.b, a));
        }
    }

    std::string summary() const {
        const Tally t = tally();
        const int judged = t.judged;
        const double mean = t.clicked ? t.sum / t.clicked : 0.0;
        const double sd = t.clicked > 1 ? std::sqrt(std::max(0.0, t.sumSq / t.clicked - mean * mean)) : 0.0;
        std::string s = fmt::format(
            "Inside the windows: <cg>{}</c> / {} ({:.1f}%)\nPerfect {}   Good {}   Off {}   Extra clicks {}\n"
            "Mean offset {:+.1f} ms ({})   Spread {:.1f} ms   Best combo {}   Attempts {}\n",
            t.perfect + t.good, judged, judged ? (t.perfect + t.good) * 100.0 / judged : 0.0, t.perfect, t.good,
            t.off, m_extra, mean * kMsPerTick, mean < -0.25 ? "early" : mean > 0.25 ? "late" : "centred",
            sd * kMsPerTick, m_bestCombo, m_attempts);
        std::vector<Note> worst;
        for (const auto& n : m_notes)
            if (n.counts && n.judged && n.grade != Grade::Perfect && n.grade != Grade::Good) worst.push_back(n);
        if (!worst.empty()) {
            s += "\nOff the window at:";
            for (size_t i = 0; i < worst.size() && i < 5; ++i)
                s += fmt::format("  {:.2f}s {}", worst[i].tick / 240.0, worst[i].press ? "press" : "release");
            s += "\n";
        }
        s += "\n<cy>Each window was measured with every other click perfect: inside all of them is a strong "
             "guide, not a guarantee.</c>";
        return s;
    }

    struct Recent {
        double dev, early, late;
        Grade grade;
    };

    PlayLayer* m_layer = nullptr;
    bool m_demo = false, m_follow = false, m_known = false, m_summaryShown = false;

public:
    bool following() const { return m_follow; }

private:
    std::vector<Note> m_notes;
    CCDrawNode* m_draw = nullptr;
    CCLabelBMFont *m_grade = nullptr, *m_offset = nullptr, *m_hud = nullptr, *m_hint = nullptr;
    float m_pxPerTick = 1.5f, m_laneW = 20.f, m_trackX = 0, m_trackY = 0, m_trackH = 0, m_hitY = 0;
    int m_lanes = 1;
    double m_lastNow = 0;
    double m_clock = 0;
    std::unordered_map<CheckpointObject*, double> m_cpClock;
    int m_total = 0, m_extra = 0, m_combo = 0, m_bestCombo = 0, m_attempts = 1;
    std::deque<Recent> m_recent;
    Ref<CCNode> m_markerRoot;   // in the level's object layer
    size_t m_nextMark = 0;
};
} // namespace

void startRhythm(PlayLayer* layer, const Plan& plan, const WindowReport& windows, bool demo, bool follow) {
    if (auto* o = RhythmOverlay::create(layer, plan, windows, demo, follow)) {
        g_overlay = o;
        layer->addChild(o, 10001);
    }
}

bool rhythmActive(GJBaseGameLayer* layer) { return g_overlay && static_cast<GJBaseGameLayer*>(g_overlay->layer()) == layer; }

void rhythmFollowClick(GJBaseGameLayer* layer, int channel, bool down) {
    if (rhythmActive(layer) && g_overlay->following()) g_overlay->onClick(channel, down);
}
} // namespace kp

class $modify(KairosRhythmLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // The clock steps before anything injects this step's inputs (Watch).
        (void)self.setHookPriority("GJBaseGameLayer::processCommands", geode::Priority::First);
    }
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (kp::rhythmActive(this)) kp::g_overlay->onStep(isHalfTick);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
    void handleButton(bool down, int button, bool isPlayer1) {
        if (button == 1 && kp::rhythmActive(this) && !kp::g_overlay->following()) {   // follow: Watch hands them over
            // handleButton swaps the buttons itself when "flip 2-player controls" is on.
            bool flip = !m_isPlatformer && GameManager::get()->getGameVariable("0010");
            bool two = m_levelSettings && m_levelSettings->m_twoPlayerMode;
            kp::g_overlay->onClick(two && (isPlayer1 == flip) ? 1 : 0, down);
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};

class $modify(KairosRhythmPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        if (kp::rhythmActive(this)) kp::g_overlay->onReset();
    }
    void storeCheckpoint(CheckpointObject* cp) {
        PlayLayer::storeCheckpoint(cp);
        if (kp::rhythmActive(this)) kp::g_overlay->onCheckpoint(cp);
    }
    void levelComplete() {
        if (kp::rhythmActive(this)) kp::g_overlay->onLevelEnd();
        PlayLayer::levelComplete();
    }
};
