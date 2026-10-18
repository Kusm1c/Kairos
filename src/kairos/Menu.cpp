#include "Jobs.hpp"
#include "StatsPopup.hpp"
#include "UiText.hpp"
#include <functional>
#include <optional>
#include "kpsim/search/WindowFile.hpp"

#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/file.hpp>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#pragma comment(lib, "comdlg32.lib")

using namespace geode::prelude;

namespace {
bool checkLevel(GJGameLevel* level) {
    if (!level || std::string(level->m_levelString).empty()) {
        FLAlertLayer::create("Kairos", "Download the level first.", "OK")->show();
        return false;
    }
    return true;
}

bool loadPlan(const std::filesystem::path& path, kp::JobRequest& req, bool quiet = false) {
    kpsim::macro::Macro m;
    std::string err;
    auto fail = [&](const std::string& why) {
        if (!quiet) FLAlertLayer::create("Kairos", why, "OK")->show();
        return false;
    };
    if (!kpsim::macro::readMacroFile(path, m, err)) return fail("Cannot read the macro: " + err);
    if (m.platformer) return fail("Platformer macros are not supported.");
    if (std::abs(m.framerate - 240.0) > 1e-6)
        return fail(fmt::format("This macro runs at {} TPS; only 240 TPS macros replay exactly.", m.framerate));
    if (m.merged.holds.empty()) return fail("The macro has no input.");
    req.plan = std::move(m.plan);
    req.merged = std::move(m.merged);
    return true;
}

// Native dialog: geode's async picker crashes this MSVC version.
std::optional<std::filesystem::path> pickFile() {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = L"Macros (*.gdr2, *.gdr)\0*.gdr2;*.gdr\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buf);
}

void pickMacro(GJGameLevel* level, kp::JobKind kind, bool follow = false) {
    auto path = pickFile();
    if (!path) return;
    kp::JobRequest req;
    req.kind = kind;
    req.follow = follow;
    req.source = path->filename().string();
    if (!loadPlan(*path, req)) return;
    req.windows = kp::windowsFor(level, req.plan);
    kp::startJob(level, std::move(req));
}

// The newest of the solver's solution and the saved .gdr2.
bool loadSolution(GJGameLevel* level, kp::JobRequest& req, bool quiet = false) {
    auto solver = kp::solverSolutionPath(level);
    auto saved = kp::solutionPath(level);
    std::error_code ec;
    bool useSaved = !std::filesystem::exists(solver) ||
                    (std::filesystem::exists(saved) &&
                     std::filesystem::last_write_time(saved, ec) > std::filesystem::last_write_time(solver, ec));
    if (useSaved) {
        req.source = kp::solutionPath(level).filename().string();
        return loadPlan(kp::solutionPath(level), req, quiet);
    }
    std::string err;
    if (!kpsim::macro::readSolverPlan(solver, req.plan, err)) {
        if (!quiet) FLAlertLayer::create("Kairos", "Cannot read the solution: " + err, "OK")->show();
        return false;
    }
    req.merged = req.plan;
    req.source = solver.filename().string();
    return true;
}

bool hasSolution(GJGameLevel* level) {
    return std::filesystem::exists(kp::solverSolutionPath(level)) || std::filesystem::exists(kp::solutionPath(level));
}

// Two lines about the level's solution: how much input, then how human and how tight.
struct SolutionInfo {
    std::string inputs, timing;
    ccColor3B color;   // the second line: yellow while Optimize has work to do
};
SolutionInfo solutionInfo(GJGameLevel* level) {
    kp::JobRequest req;
    if (!loadSolution(level, req, true)) return {"The solution cannot be read.", "", {255, 120, 90}};
    const auto& plan = req.merged.presses() ? req.merged : req.plan;
    unsigned shortest = ~0u, interval = ~0u;
    for (int ch = 0; ch < 2; ++ch) {
        const auto& h = plan.channel(ch);
        for (size_t i = 0; i < h.size(); ++i) {
            if (h[i].release != kpsim::macro::kNever) shortest = std::min(shortest, h[i].release - h[i].press);
            if (i) shortest = std::min(shortest, h[i].press - h[i - 1].release);
            if (i) interval = std::min(interval, h[i].press - h[i - 1].press);
        }
    }
    SolutionInfo info;
    info.inputs = fmt::format("{} clicks  |  {} inputs", plan.presses(), plan.inputs());
    if (interval != ~0u && interval) info.inputs += fmt::format("  |  up to {:.0f} CPS", 240.0 / interval);
    info.timing = fmt::format("shortest press/gap {} ticks", shortest == ~0u ? 0u : shortest);
    auto windows = kp::windowsFor(level, req.plan);
    if (!windows.edges.empty()) {
        auto st = kpsim::search::windowStats(windows, (double)Mod::get()->getSettingValue<int64_t>("human-sigma-ms"));
        info.timing += fmt::format("  |  narrowest window {}  |  human {:.0f}%", st.narrowest, st.humanChance * 100.0);
    }
    const bool raw = !kp::humanRules().allows(plan);
    if (raw) info.timing += "  |  -> Optimize";
    info.color = raw ? ccColor3B{255, 210, 90} : ccColor3B{140, 255, 140};
    return info;
}

// The plan of the level that the last frame windows were measured on, if it is still there.
std::optional<kpsim::macro::Plan> windowsPlan(GJGameLevel* level) {
    kp::JobRequest req;
    if (!loadSolution(level, req, true)) return std::nullopt;
    if (kp::windowsFor(level, req.plan).edges.empty()) return std::nullopt;
    return req.plan;
}

// Two windows: a macro (played by a human, the best base), and the bot's solution and Solve.
class KairosPopup : public geode::Popup {
protected:
    Ref<GJGameLevel> m_level;
    std::function<bool()> m_solve;   // opens the solver's mode menu
    bool m_bot = false;

    struct B {
        const char* text;
        const char* bg;
        SEL_MenuHandler sel;
    };

    bool init(GJGameLevel* level, std::function<bool()> solve, bool bot) {
        m_level = level;
        m_solve = std::move(solve);
        m_bot = bot;
        if (!Popup::init(380.f, bot ? 230.f : 200.f)) return false;
        this->setTitle(bot ? "Bot solution" : "Kairos");
        auto size = m_mainLayer->getContentSize();
        auto menu = CCMenu::create();
        menu->setPosition(0, 0);
        m_mainLayer->addChild(menu);
        auto row = [&](float y, float scale, const std::vector<B>& bs) {
            const float step = (size.width - 30.f) / (float)bs.size();
            float x = 15.f + step / 2.f;
            for (const auto& b : bs) {
                auto item = CCMenuItemSpriteExtra::create(ButtonSprite::create(b.text, "bigFont.fnt", b.bg, scale), this,
                                                          b.sel);
                item->setPosition(x, y);
                menu->addChild(item);
                x += step;
            }
        };
        auto line = [&](const std::string& text, float y, ccColor3B color) {
            auto* l = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
            kp::fitLabel(l, size.width - 30.f, 0.55f);
            l->setColor(color);
            l->setPosition(size.width / 2.f, y);
            m_mainLayer->addChild(l);
        };
        const float top = size.height - 46.f;
        if (!bot) {
            line("Your macro (.gdr2 / .gdr), played by a human: the best base", top, {140, 255, 140});
            row(top - 30.f, 0.5f, {{"Rhythm", "GJ_button_01.png", menu_selector(KairosPopup::onRhythmMacro)},
                                   {"Windows", "GJ_button_01.png", menu_selector(KairosPopup::onWindows)},
                                   {"Watch", "GJ_button_01.png", menu_selector(KairosPopup::onWatch)}});
            std::vector<B> more{{"Bot solution", "GJ_button_04.png", menu_selector(KairosPopup::onBot)},
                                {"Folder", "GJ_button_05.png", menu_selector(KairosPopup::onFolder)}};
            if (std::filesystem::exists(kp::statsPath(level)))
                more.push_back({"Windows stats", "GJ_button_05.png", menu_selector(KairosPopup::onStats)});
            row(26.f, 0.4f, more);
            return true;
        }
        if (hasSolution(level)) {
            const auto info = solutionInfo(level);
            line(info.inputs, top, {235, 235, 235});
            if (!info.timing.empty()) line(info.timing, top - 12.f, info.color);
            row(top - 38.f, 0.42f, {{"Rhythm", "GJ_button_02.png", menu_selector(KairosPopup::onRhythmSolution)},
                                    {"Optimize", "GJ_button_02.png", menu_selector(KairosPopup::onOptimize)},
                                    {"Windows", "GJ_button_02.png", menu_selector(KairosPopup::onSolutionWindows)}});
            row(top - 70.f, 0.42f, {{"Watch", "GJ_button_02.png", menu_selector(KairosPopup::onSolutionWatch)},
                                    {"Export .gdr2", "GJ_button_02.png", menu_selector(KairosPopup::onExport)}});
        } else {
            line("No solution yet: Solve finds one.", top, {200, 200, 200});
        }
        std::vector<B> bots;
        if (m_solve) bots.push_back({"Solve", "GJ_button_01.png", menu_selector(KairosPopup::onSolve)});
        if (level->m_twoPlayerMode)
            bots.push_back({"Find path (2P)", "GJ_button_01.png", menu_selector(KairosPopup::onFindPath)});
        if (!bots.empty()) row(28.f, 0.5f, bots);
        return true;
    }

    void onBot(CCObject*) {
        Ref<GJGameLevel> level = m_level;
        auto solve = m_solve;
        this->onClose(nullptr);
        if (auto p = create(level, solve, true)) p->show();
    }

    void onSolve(CCObject*) {
        auto solve = m_solve;
        this->onClose(nullptr);
        if (solve && !solve())
            FLAlertLayer::create("Kairos", "The solver cannot open here (a platformer level, or not downloaded yet).",
                                 "OK")->show();
    }

    void onFindPath(CCObject*) {
        if (!checkLevel(m_level)) return;
        auto level = m_level;
        this->onClose(nullptr);
        kp::startJob(level, {kp::JobKind::FindPath});
    }
    void onWindows(CCObject*) {
        if (!checkLevel(m_level)) return;
        auto level = m_level;
        this->onClose(nullptr);
        pickMacro(level, kp::JobKind::Windows);
    }
    void onWatch(CCObject*) {
        if (!checkLevel(m_level)) return;
        auto level = m_level;
        this->onClose(nullptr);
        pickMacro(level, kp::JobKind::Watch);
    }
    void fromSolution(kp::JobKind kind) {
        if (!checkLevel(m_level)) return;
        auto level = m_level;
        kp::JobRequest req;
        req.kind = kind;
        this->onClose(nullptr);
        if (!loadSolution(level, req)) return;
        req.windows = kp::windowsFor(level, req.plan);
        kp::startJob(level, std::move(req));
    }
    void onExport(CCObject*) {
        kp::JobRequest req;
        if (!loadSolution(m_level, req)) return;
        auto path = kp::solutionPath(m_level);
        kpsim::macro::writeFile(path, kpsim::macro::writeGdr2(req.plan, (uint32_t)m_level->m_levelID,
                                                               std::string(m_level->m_levelName)));
        FLAlertLayer::create("Kairos", fmt::format("{} clicks saved to\n{}", req.plan.presses(), path.string()), "OK")
            ->show();
    }
    void onSolutionWindows(CCObject*) {
        kp::JobRequest req;
        if (!loadSolution(m_level, req)) return;
        if (req.plan.presses() <= 400) {
            fromSolution(kp::JobKind::Windows);
            return;
        }
        Ref<GJGameLevel> level = m_level;
        this->onClose(nullptr);
        createQuickPopup("Kairos",
                         fmt::format("This solution has {} clicks: measuring all their windows takes long.\n"
                                     "Optimize it first (fewer clicks, centred timings)?", req.plan.presses()),
                         "Windows", "Optimize", [level, req](FLAlertLayer*, bool optimize) mutable {
                             req.kind = optimize ? kp::JobKind::Optimize : kp::JobKind::Windows;
                             req.windows = kp::windowsFor(level, req.plan);
                             kp::startJob(level, std::move(req));
                         });
    }
    void onOptimize(CCObject*) { fromSolution(kp::JobKind::Optimize); }
    // Rhythm practice: the macro playing while the player clicks along, or the player playing.
    static void chooseRhythm(std::function<void(bool follow)> go) {
        createQuickPopup("Rhythm practice",
                         "<cg>Auto</c>: the macro plays and the game ignores your clicks; they are judged against "
                         "the notes.\n<cy>You play</c>: the game is yours, the notes run beside it.",
                         "Auto", "You play", [go](FLAlertLayer*, bool play) { go(!play); });
    }
    void onRhythmSolution(CCObject*) {
        if (!checkLevel(m_level)) return;
        Ref<GJGameLevel> level = m_level;
        this->onClose(nullptr);
        chooseRhythm([level](bool follow) {
            kp::JobRequest req;
            req.kind = kp::JobKind::Rhythm;
            req.follow = follow;
            if (!loadSolution(level, req)) return;
            req.windows = kp::windowsFor(level, req.plan);
            kp::startJob(level, std::move(req));
        });
    }
    void onRhythmMacro(CCObject*) {
        if (!checkLevel(m_level)) return;
        Ref<GJGameLevel> level = m_level;
        this->onClose(nullptr);
        chooseRhythm([level](bool follow) { pickMacro(level, kp::JobKind::Rhythm, follow); });
    }
    void onSolutionWatch(CCObject*) { fromSolution(kp::JobKind::Watch); }
    void onStats(CCObject*) {
        Ref<GJGameLevel> level = m_level;
        std::function<void()> practice;
        if (windowsPlan(level))
            practice = [level] {
                kp::JobRequest req;
                if (!loadSolution(level, req)) return;
                req.kind = kp::JobKind::Rhythm;
                req.windows = kp::windowsFor(level, req.plan);
                kp::startJob(level, std::move(req));
            };
        if (!kp::showWindowStatsFile(kp::statsPath(m_level), practice,
                                     [level](const kpsim::search::EdgeTests& t) { kp::startCheck(level, t); }))
            FLAlertLayer::create("Kairos", "These frame windows were measured by an older Kairos. Run <cy>Windows</c> again.",
                                 "OK")->show();
    }
    void onFolder(CCObject*) {
        std::error_code ec;
        std::filesystem::create_directories(kp::outputDir(), ec);
        file::openFolder(kp::outputDir());
    }

public:
    static KairosPopup* create(GJGameLevel* level, std::function<bool()> solve = {}, bool bot = false) {
        auto* p = new KairosPopup();
        if (p->init(level, std::move(solve), bot)) {
            p->autorelease();
            return p;
        }
        delete p;
        return nullptr;
    }
};

// Green: a clean solution; yellow: one to optimize; white: none.
CCMenuItemSpriteExtra* menuButton(CCObject* target, SEL_MenuHandler sel, GJGameLevel* level) {
    auto sprite = CCSprite::createWithSpriteFrameName("GJ_plainBtn_001.png");
    auto label = CCLabelBMFont::create("K", "bigFont.fnt");
    label->setScale(0.5f);
    if (level && hasSolution(level)) label->setColor(solutionInfo(level).color);
    label->setPosition(sprite->getContentSize() / 2);
    sprite->addChild(label);
    sprite->setScale(0.85f);
    auto b = CCMenuItemSpriteExtra::create(sprite, target, sel);
    b->setID("menu-button"_spr);
    return b;
}

void place(CCNode* host, CCMenu* preferred, CCMenuItemSpriteExtra* button) {
    if (preferred) {
        preferred->addChild(button);
        preferred->updateLayout();
        return;
    }
    auto win = CCDirector::get()->getWinSize();
    auto menu = CCMenu::create();
    menu->setPosition(CCPointZero);
    button->setPosition({28.f, win.height / 2.f - 40.f});
    menu->addChild(button);
    host->addChild(menu, 200);
}
} // namespace

bool kp::isSolutionPlan(GJGameLevel* level, const kpsim::macro::Plan& plan) {
    kp::JobRequest req;
    if (!loadSolution(level, req, true)) return false;
    return req.plan.fingerprint() == plan.fingerprint() || req.merged.fingerprint() == plan.fingerprint();
}

class $modify(KpLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        place(this, typeinfo_cast<CCMenu*>(this->getChildByID("left-side-menu")),
              menuButton(this, menu_selector(KpLevelInfoLayer::onKairosMenu), level));
        return true;
    }
    void onKairosMenu(CCObject*) {
        Ref<LevelInfoLayer> self = this;
        if (auto p = KairosPopup::create(m_level, [self] { return kp::openSolveMenu(self.data()); })) p->show();
    }
};

class $modify(KpEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        place(this, m_buttonMenu, menuButton(this, menu_selector(KpEditLevelLayer::onKairosMenu), level));
        return true;
    }
    void onKairosMenu(CCObject*) {
        Ref<EditLevelLayer> self = this;
        if (auto p = KairosPopup::create(m_level, [self] { return kp::openSolveMenu(self.data()); })) p->show();
    }
};
