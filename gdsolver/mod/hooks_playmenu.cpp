// The play menu: the popup a level's play button opens, where the session's mode is chosen.
#include "mod/playlayer_helpers.hpp"
#include "mod/level_warnings.hpp"
#include "kairos/LevelCheckPopup.hpp"   // the menu's notes behind (i)
#include "kairos/UiText.hpp"
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/LevelPage.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <cstddef>
#include <string_view>

using namespace p1;

namespace playmenu {
// Set while Start hands the press back, so the hook it goes through lets it pass.
inline bool g_passing = false;

inline bool wanted() {
    return false;
}

struct LevelFacts {
    bool platformer = false;
    int coins = 0;
    bool random = false;   // uses the game's random numbers (isRandomObject)
    uint32_t unmodelled = 0;
    bool newer = false;
    bool official = false;   // a main level (GJLevelType::Main), where the yellow note is not shown
    std::string newerIds = "none";   // which objects from 2.0 on, for the log (newerSummary)
    bool attempts = false;
    std::vector<levelwarn::Note> notes;   // the Follow Player Y found (scan)
};

inline bool collidesInGame(int id) {
    static std::unordered_map<int, bool> known;
    if (auto it = known.find(id); it != known.end()) return it->second;
    bool hits = true;
    if (auto* o = GameObject::createWithKey(id))
        hits = o->m_objectType != GameObjectType::Decoration && o->m_objectType != GameObjectType::Modifier;
    known[id] = hits;
    return hits;
}

// The key's value in a "k,v,k,v" run, or empty.
inline std::string_view valueOf(std::string_view kv, std::string_view key) {
    size_t p = 0;
    while (p < kv.size()) {
        const size_t c1 = kv.find(',', p);
        if (c1 == std::string_view::npos) break;
        const size_t c2 = kv.find(',', c1 + 1);
        const size_t e = (c2 == std::string_view::npos) ? kv.size() : c2;
        if (kv.substr(p, c1 - p) == key) return kv.substr(c1 + 1, e - c1 - 1);
        if (c2 == std::string_view::npos) break;
        p = c2 + 1;
    }
    return {};
}

inline bool isRandomObject(std::string_view obj, std::string_view id) {
    if (id == "1912" || id == "2068") return true;
    int n = 0;
    for (char ch : id) {
        if (ch < '0' || ch > '9') return false;
        n = n * 10 + (ch - '0');
    }
    if (n < 3006 || n > 3021 || n == 3016) return false;
    for (std::string_view key : {"223", "221", "253", "219", "232", "238", "240", "234", "236",
                                 "285"}) {
        const auto v = valueOf(obj, key);
        if (!v.empty() && v != "0" && v != "-99") return true;
    }
    return false;
}

inline LevelFacts readLevel(GJGameLevel* level, bool mainLevel) {
    LevelFacts f;
    f.official = level->m_levelType == GJLevelType::Main;
    std::string s = mainLevel
        ? std::string(LocalLevelManager::sharedState()->getMainLevelString(
              level->m_levelID.value()))
        : std::string(level->m_levelString);
    const size_t b = s.find_first_not_of(" \t\r\n");
    const size_t e = s.find_last_not_of(" \t\r\n");
    s = (b == std::string::npos) ? std::string() : s.substr(b, e - b + 1);
    if (s.rfind("H4sI", 0) == 0)    // base64 of a gzip header: the stored, compressed form
        s = std::string(cocos2d::ZipUtils::decompressString(s, false, 0));
    if (s.empty()) {
        // No string to read (not expected on a level that can start): fall back to the fields.
        f.platformer = level->isPlatformer();
        f.coins = level->m_coins;
        return f;
    }
    const std::string_view all(s);
    const size_t head = all.find(';');
    f.platformer = valueOf(all.substr(0, head), "kA22") == "1";
    for (size_t pos = head; pos != std::string_view::npos;) {
        const size_t end = all.find(';', pos + 1);
        const auto obj = all.substr(pos + 1, end == std::string_view::npos
                                                 ? std::string_view::npos : end - pos - 1);
        const auto id = valueOf(obj, "1");
        if (id == "142" || id == "1329") ++f.coins;
        if (!f.random && isRandomObject(obj, id)) f.random = true;
        pos = end;
    }
    const auto w = levelwarn::scan(all, f.platformer, collidesInGame);   // types asked of the game
    f.notes = w.notes;
    f.unmodelled = w.unmodelled;
    f.newer = w.newer;
    f.newerIds = levelwarn::newerSummary(w);
#ifdef GEODE_IS_WINDOWS
    if (g_attemptOne && (f.unmodelled & levelwarn::kAttemptDependent)) {
        f.unmodelled &= ~levelwarn::kAttemptDependent;
        f.attempts = true;
    }
#endif
    return f;
}

inline const char* describe(int mode, bool coins) {
    switch (mode) {
        case UI_MODE_REPLAY:
            return coins ? "Play back the stored coin solution for this level."
                         : "Play back the stored solution for this level.";
        case UI_MODE_SOLVE:
            return coins ? "Solve for the end and every coin here and now,\n"
                           "then play the solution through."
                         : "Solve the level here and now,\nthen play the solution through.";
        default:
            return "Play it yourself. The mod stays idle,\nand your progress records as usual.";
    }
}

inline const cocos2d::ccColor3B kWhite{255, 255, 255};
inline const cocos2d::ccColor3B kDim{110, 110, 110};
inline const cocos2d::ccColor3B kRefused{255, 140, 140};
inline const cocos2d::ccColor3B kWarnRed{255, 100, 100};
inline const cocos2d::ccColor3B kWarnYellow{255, 215, 70};
inline const cocos2d::ccColor3B kInfoBlue{160, 205, 255};

// What each search choice is, under its name in Options.
inline const char* describeSearch(int s) {
    switch (s) {
        case UI_SEARCH_BEAM:
            return "Experimental: keeps the best states each tick.\nMore likely not to be solved.";
        case UI_SEARCH_MONTECARLO:
            return "Experimental: random playouts.\nMore likely not to be solved.";
        default:
            return "The reachability DP (the default).";
    }
}

class Options : public geode::Popup {
public:
    static Options* create(bool glitch, int search, std::function<void(bool, int)> onChange) {
        auto* r = new Options();
        if (r->init(glitch, search, std::move(onChange))) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }

protected:
    bool m_glitch = false;
    int m_search = UI_SEARCH_DP;
    std::function<void(bool, int)> m_onChange;
    CCMenuItemToggler* m_glitchToggle = nullptr;
    cocos2d::CCLabelBMFont* m_searchValue = nullptr;
    cocos2d::CCLabelBMFont* m_searchDesc = nullptr;

    static constexpr float kW = 300.f, kH = 230.f;

    bool init(bool glitch, int search, std::function<void(bool, int)> onChange) {
        using namespace cocos2d;
        if (!geode::Popup::init(kW, kH)) return false;
        this->setID("play-menu-options"_spr);
        m_glitch = glitch;
        m_search = std::clamp(search, 0, UI_SEARCH_COUNT - 1);
        m_onChange = std::move(onChange);
        this->setTitle("Solve options");

        // Avoid glitches, the checkbox and its label centred as a pair (as the menu's rows are).
        m_glitchToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(Options::onGlitch), .6f);
        m_glitchToggle->toggle(m_glitch);
        m_glitchToggle->setID("glitch-toggle"_spr);
        auto* glitchLabel = CCLabelBMFont::create("Avoid glitches", "bigFont.fnt");
        glitchLabel->setScale(.45f);
        glitchLabel->setAnchorPoint({0.f, .5f});
        glitchLabel->setID("glitch-label"_spr);
        const float boxW = 18.f, gap = 6.f;
        const float left = -(boxW + gap + glitchLabel->getScaledContentWidth()) / 2.f;
        m_buttonMenu->addChildAtPosition(m_glitchToggle, Anchor::Top, ccp(left + boxW / 2.f, -58.f));
        m_mainLayer->addChildAtPosition(glitchLabel, Anchor::Top, ccp(left + boxW + gap, -58.f));

        // The search: its name between two arrows, as the game's own resolution choice is.
        auto* searchTitle = CCLabelBMFont::create("Search", "goldFont.fnt");
        searchTitle->setScale(.7f);
        searchTitle->setID("search-title"_spr);
        m_mainLayer->addChildAtPosition(searchTitle, Anchor::Top, ccp(0, -94.f));
        m_searchValue = CCLabelBMFont::create(uiSearchName(m_search), "bigFont.fnt");
        m_searchValue->setID("search-value"_spr);
        m_mainLayer->addChildAtPosition(m_searchValue, Anchor::Top, ccp(0, -122.f));
        for (int dir : {-1, 1}) {
            auto* spr = CCSprite::createWithSpriteFrameName(dir < 0 ? "edit_leftBtn_001.png"
                                                                    : "edit_rightBtn_001.png");
            spr->setScale(.9f);
            auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(Options::onSearch));
            btn->setTag(dir);
            btn->setID(dir < 0 ? "search-left"_spr : "search-right"_spr);
            m_buttonMenu->addChildAtPosition(btn, Anchor::Top, ccp(95.f * dir, -122.f));
        }
        m_searchDesc = CCLabelBMFont::create("", "chatFont.fnt", 260.f, kCCTextAlignmentCenter);
        m_searchDesc->setScale(.7f);
        m_searchDesc->setID("search-description"_spr);
        m_mainLayer->addChildAtPosition(m_searchDesc, Anchor::Top, ccp(0, -156.f));

        auto* okSpr = ButtonSprite::create("OK", "goldFont.fnt", "GJ_button_01.png", .8f);
        auto* ok = CCMenuItemSpriteExtra::create(okSpr, this, menu_selector(Options::onOk));
        ok->setID("ok-button"_spr);
        m_buttonMenu->addChildAtPosition(ok, Anchor::Bottom, ccp(0, 26.f));

        refresh();
        return true;
    }

    void refresh() {
        m_searchValue->setString(uiSearchName(m_search));
        geode::cocos::limitNodeWidth(m_searchValue, 150.f, .6f, .3f);
        m_searchDesc->setString(describeSearch(m_search));
        m_searchDesc->setColor(m_search == UI_SEARCH_DP ? kWhite : kWarnRed);
        if (m_onChange) m_onChange(m_glitch, m_search);
    }

    // CCMenuItemToggler calls its target before it flips its own state (see the menu's onCoins).
    void onGlitch(CCObject*) {
        m_glitch = !m_glitchToggle->isToggled();
        refresh();
    }

    void onSearch(CCObject* sender) {
        const int dir = static_cast<cocos2d::CCNode*>(sender)->getTag();
        m_search = (m_search + dir + UI_SEARCH_COUNT) % UI_SEARCH_COUNT;
        refresh();
    }

    void onOk(CCObject*) { this->onClose(nullptr); }

    void keyDown(cocos2d::enumKeyCodes key, double p1) override {
        if (key == cocos2d::enumKeyCodes::KEY_Left || key == cocos2d::enumKeyCodes::KEY_Right) {
            m_search = (m_search + (key == cocos2d::enumKeyCodes::KEY_Left ? -1 : 1)
                        + UI_SEARCH_COUNT) % UI_SEARCH_COUNT;
            refresh();
            return;
        }
        if (key == cocos2d::enumKeyCodes::KEY_Enter) { onOk(nullptr); return; }
        geode::Popup::keyDown(key, p1);
    }
};

class Popup : public geode::Popup {
public:
    static Popup* create(GJGameLevel* level, LevelFacts facts, std::function<void()> play) {
        auto* r = new Popup();
        if (r->init(level, facts, std::move(play))) { r->autorelease(); return r; }
        delete r;
        return nullptr;
    }

protected:
    Ref<GJGameLevel> m_level;
    LevelFacts m_facts;
    std::function<void()> m_play;
    int m_mode = UI_MODE_NORMAL;
    bool m_coins = false;
    bool m_started = false;
    ButtonSprite* m_modeSpr[UI_MODE_COUNT] = {};
    cocos2d::CCLabelBMFont* m_desc = nullptr;
    CCMenuItemToggler* m_coinToggle = nullptr;
    cocos2d::CCLabelBMFont* m_coinLabel = nullptr;
    // The Options popup's two choices (Solve only), and its button.
    bool m_glitch = false;
    int m_search = UI_SEARCH_DP;
    CCMenuItemSpriteExtra* m_optBtn = nullptr;
    cocos2d::CCLabelBMFont* m_note = nullptr;
    cocos2d::CCDrawNode* m_statusDot = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    CCMenuItemSpriteExtra* m_infoBtn = nullptr;
    std::vector<levelwarn::Note> m_rows;
    int m_levelId = 0;
    ButtonSprite* m_startSpr = nullptr;
    CCMenuItemSpriteExtra* m_startBtn = nullptr;

    static constexpr float kW = 340.f, kH = 260.f;
    static constexpr float kModeY = -88.f, kDescY = -128.f, kCoinY = -164.f;   // from the top edge
    static constexpr float kNoteY = 64.f, kStartY = 32.f;   // from the bottom edge
    static constexpr float kNoteRow = 14.f;
    static constexpr float kStatusY = kNoteY + kNoteRow + 4.f;   // the status line

    static std::string modVersion() { return Mod::get()->getVersion().toVString(); }

    std::string olderSolve(bool coins) const {
        const std::string path = uiSolutionPath(m_levelId, coins);
        std::vector<InputCmd> plan;
        if (!loadInputsFile(path, plan, true)) return {};
        const std::string v = readSolvedWith(path);
        if (v == modVersion()) return {};
        return v.empty() ? std::string("an earlier version") : v;
    }

    std::vector<levelwarn::Note> currentNotes(bool solved, const std::string& older) const {
        using levelwarn::Severity;
        std::vector<levelwarn::Note> v;
        if (!solved) {
            v = m_facts.notes;
            for (auto& n : levelwarn::bitNotes(m_facts.unmodelled)) v.push_back(n);
            if (m_facts.newer && !m_facts.official)
                v.push_back({Severity::Caution, "Objects from 2.0 on",
                             "The solver is still being matched to the game on these: slower, and it "
                             "may not be solved. Found: " + m_facts.newerIds + "."});
            if (m_facts.attempts)
                v.push_back({Severity::Caution, "Changes from attempt to attempt",
                             "The bot takes it as its first attempt, so a level that opens only after "
                             "dying will not be solved."});
        }
        if (!older.empty())
            v.push_back({Severity::Caution, "Solved with " + older,
                         "Solving it again with " + modVersion() + " may not succeed."});
        if (m_mode == UI_MODE_SOLVE && m_glitch)
            v.push_back({Severity::Caution, "Avoid glitches is on",
                         "Experimental: the level is more likely not to be solved."});
        if (m_mode == UI_MODE_SOLVE && m_search != UI_SEARCH_DP)
            v.push_back({Severity::Bad, std::string("Search: ") + uiSearchName(m_search),
                         "Experimental: the level is more likely not to be solved."});
        if (m_facts.random)
            v.push_back({Severity::Info, "Random numbers fixed",
                         "Fixed while the bot drives, so a replay outside the mod may go differently."});
        if (m_mode == UI_MODE_SOLVE && !Mod::get()->getSettingValue<bool>("go-crazy")) {
            const int64_t pulse = Mod::get()->getSettingValue<int64_t>("human-min-pulse");
            const int64_t cps = Mod::get()->getSettingValue<int64_t>("max-cps");
            v.push_back({Severity::Info, "Human limits on",
                         "Presses and gaps of at least " + std::to_string(pulse) + " ticks"
                             + (cps > 0 ? ", at most " + std::to_string(cps) + " clicks a second" : std::string())
                             + ". Where only faster inputs pass, Solve goes on and Optimize brings the solution back within them."});
        }
        return v;
    }

    void updateStatus(bool bot) {
        using levelwarn::Severity;
        const bool solve = m_mode == UI_MODE_SOLVE;
        Severity worst = Severity::Fine;
        for (const auto& n : m_rows) worst = std::min(worst, n.severity);
        std::string text;
        if (worst == Severity::Bad || worst == Severity::Caution) {
            const auto first = std::find_if(m_rows.begin(), m_rows.end(),
                                            [&](const auto& n) { return n.severity == worst; });
            const auto same = std::count_if(m_rows.begin(), m_rows.end(),
                                            [&](const auto& n) { return n.severity == worst; });
            text = std::string(worst == Severity::Bad ? "Not modelled: " : "Partly modelled: ") + first->title
                   + (same > 1 ? "  +" + std::to_string(same - 1) : "");
        } else if (solve) {
            text = "Fully modelled";
            worst = Severity::Fine;
        } else if (worst == Severity::Info) {
            text = std::find_if(m_rows.begin(), m_rows.end(),
                                [](const auto& n) { return n.severity == Severity::Info; })->title;
        }
        const bool show = bot && !text.empty();
        m_statusDot->setVisible(show);
        m_statusLabel->setVisible(show);
        m_infoBtn->setVisible(show && !m_rows.empty());
        m_infoBtn->setEnabled(show && !m_rows.empty());
        if (!show) return;
        m_statusLabel->setString(text.c_str());
        kp::fitLabel(m_statusLabel, kW - 90.f, .62f, .5f);
        const auto c = kp::severityColor(worst);
        m_statusLabel->setColor(c);
        m_statusDot->clear();
        m_statusDot->drawDot({0.f, 0.f}, 3.5f, {c.r / 255.f, c.g / 255.f, c.b / 255.f, 1.f});
        const float dotW = 7.f, gap = 6.f, btnW = m_rows.empty() ? 0.f : 16.f;
        const float labelW = m_statusLabel->getScaledContentWidth();
        const float total = dotW + gap + labelW + (btnW > 0.f ? gap + btnW : 0.f);
        const float left = -total / 2.f;
        m_statusDot->updateAnchoredPosition(Anchor::Bottom, ccp(left + dotW / 2.f, kStatusY));
        m_statusLabel->updateAnchoredPosition(Anchor::Bottom, ccp(left + dotW + gap, kStatusY));
        m_infoBtn->updateAnchoredPosition(Anchor::Bottom,
                                          ccp(left + dotW + gap + labelW + gap + btnW / 2.f, kStatusY));
    }

    void onLevelCheck(CCObject*) { kp::showLevelCheck(std::string(m_level->m_levelName), m_rows); }

    bool init(GJGameLevel* level, LevelFacts facts, std::function<void()> play) {
        using namespace cocos2d;
        m_facts = facts;
        m_levelId = level->m_levelID.value();
        if (!geode::Popup::init(kW, kH + kNoteRow)) return false;   // one height
        // ...and the stored solutions the version line is decided from.
        auto solution = [this](bool coins) -> std::string {
            const std::string path = uiSolutionPath(m_levelId, coins);
            std::vector<InputCmd> plan;
            if (!loadInputsFile(path, plan, true)) return "none";
            const std::string v = readSolvedWith(path);
            return v.empty() ? std::string("stored, version not recorded") : "stored by " + v;
        };
        log::info("play menu: lv{} solutions: plain {}, coins {} (this is {})", m_levelId,
                  solution(false), solution(true), modVersion());
        this->setID("play-menu"_spr);
        m_level = level;
        m_facts = facts;
        m_play = std::move(play);
        const int id = level->m_levelID.value();
        auto stored = [id](bool coins) {
            std::vector<InputCmd> plan;
            return loadInputsFile(uiSolutionPath(id, coins), plan, true);
        };
        if (m_facts.coins > 0 && stored(true)) {
            m_mode = UI_MODE_REPLAY;
            m_coins = true;
        } else if (stored(false)) {
            m_mode = UI_MODE_REPLAY;
            m_coins = false;
        } else {
            m_mode = UI_MODE_SOLVE;
            m_coins = g_uiCoins && m_facts.coins > 0;
        }

        this->setTitle("GD Solver");
        auto* name = CCLabelBMFont::create(level->m_levelName.c_str(), "bigFont.fnt");
        geode::cocos::limitNodeWidth(name, 280.f, .45f, .1f);
        name->setColor({200, 200, 255});
        name->setID("level-name"_spr);
        m_mainLayer->addChildAtPosition(name, Anchor::Top, ccp(0, -46));

        const char* ids[UI_MODE_COUNT] = {"normal-button"_spr, "replay-button"_spr,
                                          "solve-button"_spr};
        for (int m = 0; m < UI_MODE_COUNT; ++m) {
            m_modeSpr[m] = ButtonSprite::create(uiModeName(m), 76, true, "bigFont.fnt",
                                                "GJ_button_04.png", 30.f, .6f);
            auto* btn = CCMenuItemSpriteExtra::create(m_modeSpr[m], this,
                                                      menu_selector(Popup::onMode));
            btn->setTag(m);
            btn->setID(ids[m]);
            m_buttonMenu->addChildAtPosition(btn, Anchor::Top,
                                             ccp(-100.f + 100.f * m, kModeY));
        }

        m_desc = CCLabelBMFont::create("", "chatFont.fnt", 300.f, kCCTextAlignmentCenter);
        m_desc->setScale(.75f);
        m_desc->setID("description"_spr);
        m_mainLayer->addChildAtPosition(m_desc, Anchor::Top, ccp(0, kDescY));

        // The checkbox and its label, centred as a pair: the label is one of two lengths.
        const bool haveCoins = m_facts.coins > 0;
        m_coinToggle = CCMenuItemToggler::createWithStandardSprites(
            this, menu_selector(Popup::onCoins), .6f);
        m_coinToggle->toggle(m_coins);
        m_coinToggle->setID("coins-toggle"_spr);
        m_coinLabel = CCLabelBMFont::create(haveCoins ? "Coins" : "Coins: none in this level",
                                            "bigFont.fnt");
        m_coinLabel->setScale(haveCoins ? .45f : .35f);
        m_coinLabel->setAnchorPoint({0.f, .5f});
        m_coinLabel->setID("coins-label"_spr);
        const float boxW = 18.f, gap = 6.f;
        const float left = -(boxW + gap + m_coinLabel->getScaledContentWidth()) / 2.f;
        m_buttonMenu->addChildAtPosition(m_coinToggle, Anchor::Top,
                                         ccp(left + boxW / 2.f, kCoinY));
        m_mainLayer->addChildAtPosition(m_coinLabel, Anchor::Top,
                                        ccp(left + boxW + gap, kCoinY));
        if (!haveCoins) {
            // Refused, and shown to be: the box greys out and the label says why.
            m_coinToggle->setEnabled(false);
            for (auto* b : {m_coinToggle->m_offButton, m_coinToggle->m_onButton})
                if (auto* spr = geode::cast::typeinfo_cast<CCSprite*>(b->getNormalImage()))
                    spr->setColor(kDim);
            m_coinLabel->setColor(kDim);
        }
        m_glitch = Mod::get()->getSavedValue<bool>("play-glitch", false);
        m_search = std::clamp(Mod::get()->getSavedValue<int>("play-search", UI_SEARCH_DP), 0,
                              UI_SEARCH_COUNT - 1);
        {
            auto* spr = ButtonSprite::create("Options", "goldFont.fnt", "GJ_button_04.png", .55f);
            m_optBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(Popup::onOptions));
            m_optBtn->setID("options-button"_spr);
            m_buttonMenu->addChildAtPosition(m_optBtn, Anchor::TopRight, ccp(-46.f, -22.f));
        }

        // The promise the safety gate makes, where the choice is made (see botDriving).
        m_note = CCLabelBMFont::create("The bot drives: nothing is recorded.", "chatFont.fnt");
        m_note->setScale(.6f);
        m_note->setOpacity(150);
        m_note->setID("bot-note"_spr);
        m_mainLayer->addChildAtPosition(m_note, Anchor::Bottom, ccp(0, kNoteY));
        m_statusDot = CCDrawNode::create();
        m_statusDot->setID("status-dot"_spr);
        m_mainLayer->addChildAtPosition(m_statusDot, Anchor::Bottom, ccp(0, kStatusY));
        m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
        m_statusLabel->setAnchorPoint({0.f, .5f});
        m_statusLabel->setID("status-label"_spr);
        m_mainLayer->addChildAtPosition(m_statusLabel, Anchor::Bottom, ccp(0, kStatusY));
        {
            auto* spr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
            spr->setScale(.55f);
            m_infoBtn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(Popup::onLevelCheck));
            m_infoBtn->setID("level-check-button"_spr);
            m_buttonMenu->addChildAtPosition(m_infoBtn, Anchor::Bottom, ccp(0, kStatusY));
        }

        m_startSpr = ButtonSprite::create("Start", "goldFont.fnt", "GJ_button_01.png", .9f);
        m_startBtn = CCMenuItemSpriteExtra::create(m_startSpr, this,
                                                   menu_selector(Popup::onStart));
        m_startBtn->setID("start-button"_spr);
        m_buttonMenu->addChildAtPosition(m_startBtn, Anchor::Bottom, ccp(0, kStartY));

        refresh();
        return true;
    }

    float fitScale() const {
        const float winH = cocos2d::CCDirector::sharedDirector()->getWinSize().height;
        const float h = m_mainLayer->getContentSize().height;
        const float btnH = m_closeBtn ? m_closeBtn->getScaledContentSize().height : 30.f;
        const float over = std::max(0.f, btnH / 2.f - 3.f);   // how far the button rises above
        const float half = h / 2.f + over;                     // centre to the highest point
        const float room = winH / 2.f - 6.f;
        return (half > room && half > 0.f) ? room / half : 1.f;
    }

public:
    void show() override {
        geode::Popup::show();
        const float s = fitScale();
        if (s >= 1.f) return;
        using namespace cocos2d;
        m_mainLayer->stopAllActions();
        m_mainLayer->setScale(0.1f * s);
        m_mainLayer->runAction(CCEaseElasticOut::create(CCScaleTo::create(0.5f, s), 0.6f));
    }

protected:

    bool hasSolution() const {
        std::vector<InputCmd> plan;
        return loadInputsFile(uiSolutionPath(m_level->m_levelID.value(), m_coins), plan, true);
    }

    void refresh() {
        for (int m = 0; m < UI_MODE_COUNT; ++m)
            m_modeSpr[m]->updateBGImage(m == m_mode ? "GJ_button_01.png" : "GJ_button_04.png");
        const bool bot = m_mode != UI_MODE_NORMAL;
        m_coinToggle->setVisible(bot);
        m_coinLabel->setVisible(bot);
        m_optBtn->setVisible(m_mode == UI_MODE_SOLVE);
        m_optBtn->setEnabled(m_mode == UI_MODE_SOLVE);
        m_note->setVisible(bot);
        const bool ok = m_mode != UI_MODE_REPLAY || hasSolution();
        const bool solved = hasSolution();
        const std::string older = (m_mode == UI_MODE_SOLVE && solved) ? olderSolve(m_coins)
                                                                      : std::string();
        // the notes for these choices, and the status line they make.
        m_rows = currentNotes(solved, older);
        updateStatus(bot);
        m_desc->setString(ok ? describe(m_mode, m_coins)
                             : (m_coins ? "No stored coin solution for this level yet.\n"
                                          "Solve it with Coins on first."
                                        : "No stored solution for this level yet.\n"
                                          "Solve it first."));
        m_desc->setColor(ok ? kWhite : kRefused);
        m_startBtn->setEnabled(ok);
        m_startSpr->setColor(ok ? kWhite : kDim);
    }

    void onMode(CCObject* sender) {
        m_mode = static_cast<cocos2d::CCNode*>(sender)->getTag();
        refresh();
    }

    void onCoins(CCObject*) {
        m_coins = !m_coinToggle->isToggled();
        refresh();
    }

    // Options over the menu; its choices come back here as they are made.
    void onOptions(CCObject*) {
        if (m_mode != UI_MODE_SOLVE) return;
        Ref<Popup> self = this;
        auto* o = Options::create(m_glitch, m_search, [self](bool glitch, int search) {
            self->m_glitch = glitch;
            self->m_search = search;
            self->refresh();
        });
        if (o) o->show();
    }

    void onStart(CCObject*) {
        if (!m_startBtn->isEnabled()) return;
        g_uiMode = m_mode;
        g_uiCoins = m_coins;
        // Only a Solve asks; the choices themselves are kept for the next one either way.
        g_uiGlitch = m_mode == UI_MODE_SOLVE && m_glitch;
        g_uiSearch = m_mode == UI_MODE_SOLVE ? m_search : UI_SEARCH_DP;
        g_uiArmedLevel = m_level.data();
        Mod::get()->setSavedValue<int>("play-mode", m_mode);
        if (m_facts.coins > 0) Mod::get()->setSavedValue<bool>("play-coins", m_coins);
        if (m_mode == UI_MODE_SOLVE) {
            Mod::get()->setSavedValue<bool>("play-glitch", m_glitch);
            Mod::get()->setSavedValue<int>("play-search", m_search);
        }
        log::info("play menu: {} lv{}{}{}{} ({} coins in the level)", uiModeName(m_mode),
                  m_level->m_levelID.value(),
                  (m_mode != UI_MODE_NORMAL && m_coins) ? " with coins" : "",
                  g_uiGlitch ? " avoiding glitches" : "",
                  g_uiSearch != UI_SEARCH_DP ? std::string(", search ") + uiSearchName(g_uiSearch)
                                             : std::string(),
                  m_facts.coins);
        auto play = std::move(m_play);
        m_started = true;
        this->onClose(nullptr);
        g_passing = true;
        play();
        g_passing = false;
    }

    void onClose(CCObject* sender) override {
        if (!m_started && g_uiArmedLevel == m_level.data()) g_uiArmedLevel = nullptr;
        geode::Popup::onClose(sender);
    }

    void keyDown(cocos2d::enumKeyCodes key, double p1) override {
        if (key == cocos2d::enumKeyCodes::KEY_Enter) { onStart(nullptr); return; }
        if (key >= cocos2d::enumKeyCodes::KEY_One && key <= cocos2d::enumKeyCodes::KEY_Three) {
            m_mode = (int)key - (int)cocos2d::enumKeyCodes::KEY_One;
            refresh();
            return;
        }
        geode::Popup::keyDown(key, p1);
    }
};

// The menu for this press, or false to let the press through to the game untouched.
template <class Layer>
bool open(Layer* layer, GJGameLevel* level, CCObject* sender, bool mainLevel) {
    const LevelFacts facts = readLevel(level, mainLevel);
    if (facts.platformer) return false;
    // What the notes were decided from, so a note can be traced back to the objects behind it.
    log::info("play menu: lv{} checked: not modelled: {}; objects from 2.0 on: {}{}; random "
              "numbers: {}; changes by attempt (taken as the first): {}",
              level->m_levelID.value(), levelwarn::describeAll(facts.unmodelled),
              facts.newerIds, facts.official ? " (a main level: yellow not shown)" : "",
              facts.random ? "yes" : "no", facts.attempts ? "yes" : "no");
    Ref<Layer> self = layer;
    Ref<CCObject> from = sender;
    auto* p = Popup::create(level, facts, [self, from] { self->onPlay(from.data()); });
    if (!p) return false;
    p->show();
    return true;
}
}  // namespace playmenu

$on_mod(Loaded) {
    g_uiMode = std::clamp(Mod::get()->getSavedValue<int>("play-mode", UI_MODE_NORMAL),
                          0, UI_MODE_COUNT - 1);
    g_uiCoins = Mod::get()->getSavedValue<bool>("play-coins", false);
    g_uiGlitch = Mod::get()->getSavedValue<bool>("play-glitch", false);
    g_uiSearch = UI_SEARCH_DP;
}

static_assert(offsetof(LevelInfoLayer, m_isBusy) == gdoff::kLevelInfoBusy);
static_assert(offsetof(LevelInfoLayer, m_enterTransitionFinished) == gdoff::kLevelInfoTransitionDone);
static_assert(offsetof(LevelPage, m_isBusy) == gdoff::kLevelPageBusy);
static_assert(offsetof(EditLevelLayer, m_exiting) == gdoff::kEditLevelExiting);
static_assert(offsetof(GJGameLevel, m_requiredCoins) == gdoff::kLevelRequiredCoins);

class $modify(PlayMenuLevelInfoLayer, LevelInfoLayer) {
    void onPlay(CCObject* sender) {
        if (!playmenu::wanted() || m_isBusy || !m_enterTransitionFinished || !m_level
            || this->shouldDownloadLevel()
            || !playmenu::open(this, m_level, sender, false))
            LevelInfoLayer::onPlay(sender);
    }

    void FLAlert_Clicked(FLAlertLayer* alert, bool btn2) {
        const int tag = alert ? alert->getTag() : 0;
        if (tag != 9 && tag != 10) return LevelInfoLayer::FLAlert_Clicked(alert, btn2);
        if (!btn2) {
            if (g_uiArmedLevel == m_level) g_uiArmedLevel = nullptr;
            return LevelInfoLayer::FLAlert_Clicked(alert, btn2);
        }
        playmenu::g_passing = true;
        LevelInfoLayer::FLAlert_Clicked(alert, btn2);
        playmenu::g_passing = false;
    }
};

class $modify(PlayMenuLevelPage, LevelPage) {
    void onPlay(CCObject* sender) {
        const int id = m_level ? m_level->m_levelID.value() : -1;
        const bool locked = m_level && m_level->m_requiredCoins
                                           > GameStatsManager::sharedState()->getStat("8");
        if (!playmenu::wanted() || m_isBusy || id <= 0 || locked
            || !playmenu::open(this, m_level, sender, true))
            LevelPage::onPlay(sender);
    }
};

// A locally saved level. Its onPlay sets m_exiting and leaves; a second press finds it set.
class $modify(PlayMenuEditLevelLayer, EditLevelLayer) {
    void onPlay(CCObject* sender) {
        if (!playmenu::wanted() || m_exiting || !m_level
            || !playmenu::open(this, m_level, sender, false))
            EditLevelLayer::onPlay(sender);
    }
};

namespace kp {
bool openSolveMenu(LevelInfoLayer* layer) {
    return layer && layer->m_level && !layer->shouldDownloadLevel() &&
           playmenu::open(layer, layer->m_level, nullptr, false);
}
bool openSolveMenu(EditLevelLayer* layer) {
    return layer && layer->m_level && playmenu::open(layer, layer->m_level, nullptr, false);
}
} // namespace kp
