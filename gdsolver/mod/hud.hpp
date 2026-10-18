#pragma once
// On-screen HUD that stays visible while rendering is skipped.
#include "mod/level_slice.hpp"
#include "mod/takeover.hpp"         // the human takeover's line under the badge

using namespace p1;

#include "mod/touch_controls.hpp"   // Android: the on-screen buttons that stand in for the keys
#include "mod/progress_bars.hpp"    // the solve session's bars, drawn as GD's own level bar

constexpr int HUD_TAG = 0x51D50;
constexpr int KEYS_TAG = 0x51D52;
constexpr int BADGE_TAG = 0x51D53;
constexpr int ITERMAP_TAG = 0x51D54;
constexpr int TAKEOVER_TAG = 0x51D55;

struct OverlayCursor { float y; };

inline cocos2d::CCLabelBMFont* overlayLabel(cocos2d::CCNode* parent, int tag,
                                            const std::string& id, const char* font,
                                            float scale, bool visible, OverlayCursor& cur) {
    using namespace cocos2d;
    auto* lbl = static_cast<CCLabelBMFont*>(parent->getChildByTag(tag));
    if (!visible) {
        if (lbl) lbl->setVisible(false);
        return nullptr;
    }
    if (!lbl) {
        lbl = CCLabelBMFont::create("", font);
        if (!lbl) return nullptr;
        lbl->setTag(tag);
        lbl->setID(id);
        lbl->setAnchorPoint({0.f, 1.f});
        lbl->setScale(scale);
        lbl->setZOrder(1 << 20);
        parent->addChild(lbl);
    }
    lbl->setVisible(true);
    lbl->setPositionX(10.f);
    lbl->setPositionY(cur.y);
    return lbl;
}

// Move the cursor past a label overlayLabel placed and the caller has filled.
inline void overlayAdvance(cocos2d::CCLabelBMFont* lbl, OverlayCursor& cur) {
    if (lbl) cur.y -= lbl->getContentSize().height * lbl->getScaleY() + 3.f;
}

inline void speedText(char* out, size_t n) {
    const bool fastLoop = g_started && !g_sessionOver && g_cfg.fastdt > 0 && !g_realtimeOverride;
    char slow[24] = "";
    if (g_slowmo > 1.f) snprintf(slow, sizeof(slow), "  slowmo 1/%.0f", (double)g_slowmo);
    if (fastLoop)
        snprintf(out, n, "FAST (%d loops/frame)%s", g_cfg.fastloops, slow);
    else
        snprintf(out, n, "%gx%s", (double)g_watchSpeed, slow);
}

inline void fillKeysHud(cocos2d::CCLabelBMFont* lbl) {
    if (!lbl) return;
    static auto s_last = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - s_last).count() < 200
        && *lbl->getString())
        return;
    s_last = now;
    char spd[64];
    speedText(spd, sizeof(spd));
    const bool showRender = solveSession();
    char renderPart[32] = "";
    char renderLine[40] = "";
    if (showRender) {
        snprintf(renderPart, sizeof(renderPart), "        RENDER %s",
                 renderingOn() ? "on" : "off");
        snprintf(renderLine, sizeof(renderLine), "%s   render on / off\n",
                 keyName(Key::Render).c_str());
    }
    const char* stopPart = probe::g_pause ? "  [PAUSED]"
                         : !g_paused      ? ""
                         : solvingNow()   ? "  [HELD - SEARCHING]"
                                          : "  [PAUSED]";
#ifndef GEODE_IS_WINDOWS
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "SPEED  %s%s%s\n< >  %s\nMAP  %s",
                 spd, stopPart, renderPart,
                 showingSolve()                ? "(disabled while solving)"
                 : (g_paused || probe::g_pause) ? "step back / forward (10 substeps)"
                                                : "seek back / forward (hold to accelerate)",
                 itermap::mapWanted() ? "on" : "off");
        lbl->setString(buf);
        return;
    }
#endif
    char buf[768];
    snprintf(buf, sizeof(buf),
        "SPEED  %s%s%s\n"
        "%s   hide this overlay%s\n"
        "%s   pause / resume\n"
        "%s / %s   step 1 / 10 substeps (hold to repeat)\n"
        "%s"
        "%s   hitboxes: off / on / only\n"
        "%s   replay from the start\n"
        "%s   quit to the level screen\n"
        "%s  iteration map: %s\n"
        "%s / %s   %s\n"
        "%s / %s   speed up / down   -   or drag the bar",
        spd, stopPart, renderPart,
        keyName(Key::Overlay).c_str(),
        showingSolve() ? "  (disabled while solving)" : "",
        keyName(Key::Pause).c_str(),
        keyName(Key::Step1).c_str(), keyName(Key::Step10).c_str(),
        renderLine,
        keyName(Key::Hitboxes).c_str(),
        keyName(Key::Replay).c_str(),
        keyName(Key::Quit).c_str(),
        keyName(Key::Itermap).c_str(), itermap::mapWanted() ? "on" : "off",
        keyName(Key::SeekBack).c_str(), keyName(Key::SeekForward).c_str(),
        showingSolve()                ? "(disabled while solving)"
        : (g_paused || probe::g_pause) ? "step back / forward (10 substeps)"
                                       : "seek back / forward (hold to accelerate)",
        keyName(Key::Faster).c_str(), keyName(Key::Slower).c_str());
    lbl->setString(buf);
}

inline void fillBotBadge(cocos2d::CCLabelBMFont* lbl) {
    if (!lbl) return;
    static int s_wasMode = -1;
    static const std::string s_version = geode::Mod::get()->getVersion().toVString();
    const int mode = showingSolve() ? 1 : takeover::personDriving() ? 2 : 0;
    if (!*lbl->getString() || mode != s_wasMode) {
        s_wasMode = mode;
        const std::string text = "GDSOLVER BOT " + s_version
                               + (mode == 1 ? " - SOLVING" : mode == 2 ? " - HUMAN INPUT" : " - REPLAY");
        lbl->setString(text.c_str());
    }
}

constexpr float kBadgeScale = 0.28f;
constexpr float kBadgeMinScale = 0.15f;
constexpr float kGdBarHalfWidth = 105.f;
inline void fitBotBadge(cocos2d::CCLabelBMFont* lbl) {
    using namespace cocos2d;
    if (!lbl) return;
    float s = kBadgeScale;
    const float w = lbl->getContentSize().width;
    if (w > 0.f) {
        const float barLeft = CCDirector::sharedDirector()->getWinSize().width * 0.5f - kGdBarHalfWidth;
        const float room = barLeft - 6.f - lbl->getPositionX();
        if (room < w * s) s = std::max(kBadgeMinScale, room / w);
    }
    if (lbl->getScale() != s) lbl->setScale(s);
}

constexpr char BAR_FILL = ';';
constexpr char BAR_TRACK = '.';
constexpr int BAR_CELLS = 40;

inline double barFraction(double done, double total) {
    if (!(total > 0.0)) return 0.0;
    const double f = done / total;
    return (f < 0.0) ? 0.0 : (f > 1.0 ? 1.0 : f);
}

// Spell one bar's cells into `out`, which needs BAR_CELLS + 1 bytes. `f` is already clamped.
inline void barCells(char* out, double f) {
    const int fill = (int)(f * BAR_CELLS + 0.5);
    for (int i = 0; i < BAR_CELLS; ++i) out[i] = (i < fill) ? BAR_FILL : BAR_TRACK;
    out[BAR_CELLS] = 0;
}

inline int barLine(char* out, size_t cap, const char* label,
                   double done, double total, const char* detail) {
    if (cap == 0) return 0;
    const double f = barFraction(done, total);
    const int n = snprintf(out, cap, "%s %5.1f%%   %s\n", label, f * 100.0, detail);
    if (n < 0) return 0;
    return (n < (int)cap) ? n : (int)cap - 1;
}

inline void fillSessionHud(cocos2d::CCLabelBMFont* hud) {
    using namespace cocos2d;
    if (!hud) return;
    static auto s_lastHud = std::chrono::steady_clock::time_point{};
    auto nowHud = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(nowHud - s_lastHud).count() < 250
        && *hud->getString())
        return;
    s_lastHud = nowHud;
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - solver::g_solveStart).count();
    float len = 0.f;
    if (auto* pl = PlayLayer::get()) len = pl->m_levelLength;
    char coinLine[80] = "";
    if (g_cfg.coinMode) {
        const size_t got = solver::coinsThisAttempt();
        const size_t best = std::max(solver::g_coinBest, got);
        snprintf(coinLine, sizeof(coinLine), "coins %zu/%zu this attempt   best %zu/%zu\n", got,
                 solver::g_coins.size(), best, solver::g_coins.size());
    }
    float px = 0.f;
    if (auto* pl = PlayLayer::get()) if (pl->m_player1) px = pl->m_player1->getPositionX();
    if (showingSolve() && g_cfg.dpSolve) {
        const dpbridge::SolveProgress pr = dpbridge::progress();
        const double lvl = (len > 1.f) ? (double)len : 0.0;
        char b[480];
        int n = snprintf(b, sizeof(b), "iter %d   %s   %llds\n%s",
                         g_hudIter, g_hudPhase.empty() ? "starting" : g_hudPhase.c_str(),
                         (long long)sec, coinLine);
        char detail[192];
        snprintf(detail, sizeof(detail), "best x %.0f / %.0f",
                 (double)g_hudVerifiedX, lvl);
        n += barLine(b + n, sizeof(b) - n, "level ", (double)g_hudVerifiedX, lvl, detail);
        if (pr.running) {
            snprintf(detail, sizeof(detail), "tick %lld / %lld   x %.0f   states %zu",
                     pr.tick, pr.horizon, pr.x, pr.alive);
            n += barLine(b + n, sizeof(b) - n, "search",
                         (double)(pr.tick - pr.from), (double)(pr.horizon - pr.from), detail);
        } else {
            snprintf(b + n, sizeof(b) - n, "now    x %.0f  (%.1f%%)   anchor t=%lld x=%.0f",
                     (double)px, lvl > 0.0 ? px / lvl * 100.0 : 0.0,
                     (long long)g_hudAnchorT, (double)g_hudAnchorX);
        }
        hud->setString(b);
        return;
    }
    const double nowPct = (len > 1.f) ? (px / len * 100.0) : 0.0;
    const double vPct = (len > 1.f && g_hudVerifiedX > 0)
                      ? (g_hudVerifiedX / len * 100.0) : 0.0;
    // Bar of the verified prefix
    char bar[BAR_CELLS + 1];
    barCells(bar, barFraction(vPct, 100.0));
    char buf[512];
    snprintf(buf, sizeof(buf),
        "iter %d   %s\n"
        "%s"
        "[%s] %.1f%%\n"
        "verified  x=%.0f / %.0f\n"
        "anchor    t=%lld  x=%.0f   fixups %d/%d\n"
        "now       x=%.0f  (%.1f%%)   %llds",
        g_hudIter, g_hudPhase.empty() ? "starting" : g_hudPhase.c_str(),
        coinLine,
        bar, vPct,
        (double)g_hudVerifiedX, (double)len,
        (long long)g_hudAnchorT, (double)g_hudAnchorX,
        g_hudFixups, g_hudFixupCap,
        (double)px, nowPct, (long long)sec);
    hud->setString(buf);
}

inline void updateOverlays(cocos2d::CCNode* gameLayer) {
    using namespace cocos2d;
    CCNode* parent = gameLayer ? gameLayer->getParent() : nullptr;
    if (!parent) return;
    OverlayCursor cur{CCDirector::sharedDirector()->getWinSize().height - 4.f};
    // 1. the bot badge -- neither F1 nor cfg hud=0 reaches it (spec 9)
    auto* badge = overlayLabel(parent, BADGE_TAG, "bot-badge"_spr, "bigFont.fnt", kBadgeScale,
                               botDriving(), cur);
    fillBotBadge(badge);
    fitBotBadge(badge);
    overlayAdvance(badge, cur);
    {
        const std::string tk = botDriving() ? takeover::hudText() : std::string();
        auto* lbl = overlayLabel(parent, TAKEOVER_TAG, "takeover-hud"_spr, "chatFont.fnt", 0.6f,
                                 !tk.empty(), cur);
        if (lbl) {
            if (tk != lbl->getString()) lbl->setString(tk.c_str());
            overlayAdvance(lbl, cur);
        }
    }
    const bool show = g_hudOn && !g_overlayHidden;
    auto* session = overlayLabel(parent, HUD_TAG, "session-hud"_spr, "chatFont.fnt", 0.6f,
                                 show && showingSolve(), cur);
    fillSessionHud(session);
    overlayAdvance(session, cur);
    //    ...and its two drawn bars, as the next row of the column (progress_bars.hpp)
    progressbars::updateBars(parent, show, cur.y);
    auto* keys = overlayLabel(parent, KEYS_TAG, "keys-hud"_spr, "chatFont.fnt", 0.5f,
                              show && botDriving(), cur);
    fillKeysHud(keys);
    overlayAdvance(keys, cur);
    itermap::draw(parent, PlayLayer::get());
#ifndef GEODE_IS_WINDOWS
    touchpad::update(parent);
#endif
}
