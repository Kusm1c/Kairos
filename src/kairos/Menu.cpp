#include "Jobs.hpp"
#include "StatsPopup.hpp"
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

void pickMacro(GJGameLevel* level, kp::JobKind kind) {
    auto path = pickFile();
    if (!path) return;
    kp::JobRequest req;
    req.kind = kind;
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

// One line about the level's solution: how many clicks, how human, how tight.
std::pair<std::string, ccColor3B> solutionInfo(GJGameLevel* level) {
    kp::JobRequest req;
    if (!loadSolution(level, req, true)) return {"The solution cannot be read.", {255, 120, 90}};
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
    std::string text = fmt::format("{} clicks   shortest press/gap {} ticks", plan.presses(),
                                   shortest == ~0u ? 0u : shortest);
    if (interval != ~0u && interval) text += fmt::format("   up to {:.0f} CPS", 240.0 / interval);
    auto windows = kp::windowsFor(level, req.plan);
    if (!windows.edges.empty()) {
        auto st = kpsim::search::windowStats(windows, (double)Mod::get()->getSettingValue<int64_t>("human-sigma-ms"));
        text += fmt::format("   narrowest window {} ticks   human {:.0f}%", st.narrowest, st.humanChance * 100.0);
    }
    if (!kp::humanRules().allows(plan)) return {text + "   -> Optimize", {255, 210, 90}};
    return {text, {140, 255, 140}};
}


// The plan of the level that the last frame windows were measured on, if it is still there.
std::optional<kpsim::macro::Plan> windowsPlan(GJGameLevel* level) {
    kp::JobRequest req;
    if (!loadSolution(level, req, true)) return std::nullopt;
    if (kp::windowsFor(level, req.plan).edges.empty()) return std::nullopt;
    return req.plan;
}

class KairosPopup : public geode::Popup {
protected:
    Ref<GJGameLevel> m_level;

    bool init(GJGameLevel* level) {
        m_level = level;
        if (!Popup::init(380.f, 280.f)) return false;
        this->setTitle("Kairos");
        auto size = m_mainLayer->getContentSize();
        auto menu = CCMenu::create();
        menu->setPosition(0, 0);
        m_mainLayer->addChild(menu);
        // A row of buttons, evenly spread.
        struct B { const char* text; const char* bg; SEL_MenuHandler sel; };
        auto rowOf = [&](float y, float scale, const std::vector<B>& bs) {
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
        auto row = [&](float y, float scale, std::initializer_list<B> bs) { rowOf(y, scale, std::vector<B>(bs)); };
        auto heading = [&](const char* text, float y) {
            auto* l = CCLabelBMFont::create(text, "goldFont.fnt");
            l->setScale(0.5f);
            l->setPosition(size.width / 2.f, y);
            m_mainLayer->addChild(l);
        };
        float y = size.height - 52.f;
        if (hasSolution(level)) {
            auto [text, color] = solutionInfo(level);
            auto* info = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
            info->setScale(0.55f);
            info->limitLabelWidth(size.width - 24.f, 0.55f, 0.2f);
            info->setColor(color);
            info->setPosition(size.width / 2.f, size.height - 40.f);
            m_mainLayer->addChild(info);
            y -= 12.f;
            heading("Solution", y);
            row(y - 24.f, 0.42f, {{"Rhythm", "GJ_button_01.png", menu_selector(KairosPopup::onRhythmSolution)},
                                  {"Optimize", "GJ_button_01.png", menu_selector(KairosPopup::onOptimize)},
                                  {"Windows", "GJ_button_02.png", menu_selector(KairosPopup::onSolutionWindows)}});
            row(y - 56.f, 0.42f, {{"Watch", "GJ_button_02.png", menu_selector(KairosPopup::onSolutionWatch)},
                                  {"Export .gdr2", "GJ_button_02.png", menu_selector(KairosPopup::onExport)}});
            y -= 92.f;
        }
        heading("A macro (.gdr2 / .gdr)", y);
        row(y - 24.f, 0.42f, {{"Rhythm", "GJ_button_01.png", menu_selector(KairosPopup::onRhythmMacro)},
                              {"Windows", "GJ_button_02.png", menu_selector(KairosPopup::onWindows)},
                              {"Watch", "GJ_button_02.png", menu_selector(KairosPopup::onWatch)}});
        // 1-player levels are solved from the Play button; the exact search is for 2 players.
        const bool stats = std::filesystem::exists(kp::statsPath(level));
        std::vector<B> tools;
        if (stats) tools.push_back({"Windows stats", "GJ_button_03.png", menu_selector(KairosPopup::onStats)});
        if (level->m_twoPlayerMode)
            tools.push_back({"Find path (2P)", "GJ_button_05.png", menu_selector(KairosPopup::onFindPath)});
        tools.push_back({"Folder", "GJ_button_05.png", menu_selector(KairosPopup::onFolder)});
        rowOf(30.f, 0.4f, tools);
        return true;
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
    void onRhythmSolution(CCObject*) { fromSolution(kp::JobKind::Rhythm); }
    void onRhythmMacro(CCObject*) {
        if (!checkLevel(m_level)) return;
        auto level = m_level;
        this->onClose(nullptr);
        pickMacro(level, kp::JobKind::Rhythm);
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
        if (!kp::showWindowStatsFile(kp::statsPath(m_level), practice))
            FLAlertLayer::create("Kairos", "Cannot read the last frame windows.", "OK")->show();
    }
    void onFolder(CCObject*) {
        std::error_code ec;
        std::filesystem::create_directories(kp::outputDir(), ec);
        file::openFolder(kp::outputDir());
    }

public:
    static KairosPopup* create(GJGameLevel* level) {
        auto* p = new KairosPopup();
        if (p->init(level)) {
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
    if (level && hasSolution(level)) label->setColor(solutionInfo(level).second);
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

class $modify(KpLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        place(this, typeinfo_cast<CCMenu*>(this->getChildByID("left-side-menu")),
              menuButton(this, menu_selector(KpLevelInfoLayer::onKairosMenu), level));
        return true;
    }
    void onKairosMenu(CCObject*) {
        if (auto p = KairosPopup::create(m_level)) p->show();
    }
};

class $modify(KpEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;
        place(this, m_buttonMenu, menuButton(this, menu_selector(KpEditLevelLayer::onKairosMenu), level));
        return true;
    }
    void onKairosMenu(CCObject*) {
        if (auto p = KairosPopup::create(m_level)) p->show();
    }
};
