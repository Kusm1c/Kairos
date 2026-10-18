#pragma once
// Death statistics, developer hotkeys (F8 realtime override, probe requests).
#include "mod/fxcensus.hpp"

namespace p1 {
namespace deadstat {
inline long long g_total = 0;      // total physics ticks advanced during the session
inline long long g_dead = 0;       // of which, ticks where the player was dead
inline long long g_streak = 0;     // current run of consecutive dead ticks
inline long long g_maxStreak = 0;  // longest
inline long long g_episodes = 0;   // number of death episodes (= effective attempt count)
inline void tick(bool dead) {
    ++g_total;
    if (dead) {
        ++g_dead;
        if (++g_streak > g_maxStreak) g_maxStreak = g_streak;
    } else {
        if (g_streak > 0) ++g_episodes;
        g_streak = 0;
    }
}
}

inline int g_probeRequest = 0;

#include "solver/sweep.hpp"

inline void endSession(const std::string& why);
inline void clearManualPause();
inline bool manualPaused();
inline bool manualStepPending();

enum class Key {
    Overlay, Pause, Step1, Step10, Render, Hitboxes, Replay, Quit, Itermap,
    Faster, Slower, SeekBack, SeekForward, TakeoverEnd, Count
};

inline bool g_keyDown[(size_t)Key::Count] = {};

// In the order of the enum.
inline const char* const kKeySettings[(size_t)Key::Count] = {
    "key-overlay", "key-pause", "key-step1", "key-step10", "key-render", "key-hitboxes",
    "key-replay", "key-quit", "key-itermap", "key-faster", "key-slower",
    "key-seek-back", "key-seek-forward", "key-takeover-end"
};

inline bool keyHeld(Key k) { return g_keyDown[(size_t)k]; }

inline std::string keyName(Key k) {
    auto binds = Mod::get()->getSettingValue<std::vector<Keybind>>(kKeySettings[(size_t)k]);
    return binds.empty() ? "(unbound)" : binds.front().toString();
}

inline void ensureHotkeyListeners() {
    static bool s_installed = false;
    if (s_installed) return;
    s_installed = true;
    for (size_t i = 0; i < (size_t)Key::Count; ++i) {
        listenForKeybindSettingPresses(kKeySettings[i],
            [i](Keybind const&, bool down, bool, double) { g_keyDown[i] = down; });
    }
}

inline void pollProbeHotkeys() {
    ensureHotkeyListeners();
    static bool s_f1Down = false;
    bool f1 = keyHeld(Key::Overlay);
    if (f1 && !s_f1Down) {
        if (solvingNow()) {
            log::info("hotkey: F1 ignored (solving)");
        } else {
            g_overlayHidden = !g_overlayHidden;
            log::info("hotkey: F1 -> overlay {}", g_overlayHidden ? "hidden" : "shown");
        }
    }
    s_f1Down = f1;
    // F2: pause/resume (for manual experiments; also works outside a session)
    static bool s_f2Down = false;
    bool f2 = keyHeld(Key::Pause);
    if (f2 && !s_f2Down) g_probeRequest = 2;
    s_f2Down = f2;
    auto stepKey = [](Key k, int req, bool& down,
                      std::chrono::steady_clock::time_point& since,
                      std::chrono::steady_clock::time_point& last) {
        constexpr auto kRepeatAfter = std::chrono::milliseconds(500);
        constexpr auto kRepeatEvery = std::chrono::milliseconds(100);
        const bool now = keyHeld(k);
        const auto t = std::chrono::steady_clock::now();
        if (now && !down) {            // the press itself
            g_probeRequest = req;
            since = t;
            last = t;
        } else if (now && t - since >= kRepeatAfter && t - last >= kRepeatEvery) {
            g_probeRequest = req;      // ...and every 100ms once it has been held for 500ms
            last = t;
        }
        down = now;
    };
    static bool s_f3Down = false, s_f4Down = false;
    static std::chrono::steady_clock::time_point s_f3Since, s_f3Last, s_f4Since, s_f4Last;
    stepKey(Key::Step1, 3, s_f3Down, s_f3Since, s_f3Last);
    stepKey(Key::Step10, 4, s_f4Down, s_f4Since, s_f4Last);
    static bool s_f6Down = false;
    bool f6 = keyHeld(Key::Hitboxes);
    if (f6 && !s_f6Down) g_probeRequest = 6;
    s_f6Down = f6;
    static bool s_f10Down = false;
    bool f10 = keyHeld(Key::Itermap);
    if (f10 && !s_f10Down) {
        int lv = g_cfg.levelId;
        if (lv <= 0)
            if (auto* pl = PlayLayer::get())
                if (pl->m_level) lv = pl->m_level->m_levelID.value();
        itermap::toggle(lv);
        log::info("hotkey: F10 -> iteration map {}", itermap::mapWanted() ? "on" : "off");
    }
    s_f10Down = f10;
}

inline bool renderingOn() {
    if (g_renderOff) return false;
    return !(g_started && !g_sessionOver && g_cfg.skipRender && !g_realtimeOverride);
}

inline int g_secRenderWas = -1;          // g_renderOff when the hold began (0/1)
inline bool g_secScreenWasOn = false;    // ...and whether anything was being drawn then
inline void secRenderHold() {
    if (g_secRenderWas >= 0) return;
    g_secScreenWasOn = renderingOn();
    g_secRenderWas = g_renderOff ? 1 : 0;
    g_renderOff = true;
    log::info("secsolve: screen and visibility off until the section solve ends (screen was {})",
              g_secScreenWasOn ? "on" : "off");
}
inline void secRenderRelease() {
    if (g_secRenderWas < 0) return;
    g_renderOff = g_secRenderWas != 0;
    g_secRenderWas = -1;
    if (g_secScreenWasOn) g_visResetPending = true;
    log::info("secsolve: hold lifted after the reset (screen {})",
              g_secScreenWasOn ? "back after one more reset" : "stays as it was");
}

inline void renderTogglePress() {
        const bool hasFast = g_cfg.fastdt > 0;
        if (g_secRenderWas >= 0) {
            // A section solve holds the screen off (secRenderHold); the key waits for its end.
            log::info("hotkey: F5 ignored - a section solve is running, the screen comes back "
                      "when it ends");
            return;
        }
        if (renderingOn()) {
            g_renderOff = true;
            if (hasFast) g_realtimeOverride = false;
        } else {
            g_renderOff = false;
            if (hasFast && !g_realtimeOverride && !g_visResetPending) {
                g_visResetPending = true;
                g_visRefresh = true;        // old method (only effective with visrefresh=1)
                if (g_watchPurge) purgeDanglingActions();
            }
        }
        log::info("hotkey: F5 -> render {}", renderingOn() ? "ON" : "OFF");
        writeResult(fxCensus("render-toggle"));
}

inline void pollHotkeys() {
    ensureHotkeyListeners();
    static bool s_f5Down = false, s_f9Down = false;
    bool f5 = keyHeld(Key::Render);
    if (f5 && !s_f5Down && solveSession()) renderTogglePress();
    s_f5Down = f5;
    static bool s_tkDown = false;
    const bool tk = keyHeld(Key::TakeoverEnd);
    if (tk && !s_tkDown && takeover::armed()) takeover::endByKey();
    s_tkDown = tk;
    const bool takeoverOwnsLevel = takeover::armed();
    bool f9 = keyHeld(Key::Quit);
    if (f9 && !s_f9Down) {
        log::info("hotkey: F9 -> quit to the level screen");
        if (!g_sessionOver) {
            endSession("user_quit_hotkey");   // ends with GD's onQuit (see endSession)
        } else {
            Loader::get()->queueInMainThread([] {
                if (auto* pl = PlayLayer::get()) pl->onQuit();
            });
        }
    }
    s_f9Down = f9;
    static bool s_f7Down = false;
    bool f7 = keyHeld(Key::Replay);
    if (f7 && !s_f7Down && !takeoverOwnsLevel) {
        if (auto* pl = PlayLayer::get()) {
            if (g_sessionOver && !g_serveMode) {
                g_sessionOver = false;
                g_finishedAttempts = 0;
                writeResult("session_resume: F7 restarted the replay after the session ended");
            }
            ++g_cfg.maxAttempts;
            g_pauseAtXFired = false;   // stop short of the wall again on the next lap
            g_paused = false;          // release it so it does not start out stuck paused
            clearManualPause();        // ...and F2's, so the replay does not start frozen
            itermap::dismissEndScreen();
            log::info("hotkey: F7 -> replay from the start");
            pl->resetLevel();
        }
    }
    s_f7Down = f7;
    static bool s_upDown = false, s_downDown = false;
    const bool kUp   = keyHeld(Key::Faster);
    const bool kDown = keyHeld(Key::Slower);
    int step = 0;
    if (kUp && !s_upDown) step = +1;
    if (kDown && !s_downDown) step = -1;
    s_upDown = kUp; s_downDown = kDown;
    if (step != 0 && !takeoverOwnsLevel) {
        watchSpeedStep(step);
        log::info("hotkey: {} -> {:g}x", step > 0 ? "faster" : "slower", g_watchSpeed);
    }
    auto transportKey = [](Key k, int dir, bool& down, int& repeats,
                           std::chrono::steady_clock::time_point& since,
                           std::chrono::steady_clock::time_point& last) {
        const bool stopped = g_paused || manualPaused();
        // Stopped: F3/F4's timing exactly, because that is the key this becomes.
        const auto repeatAfter = std::chrono::milliseconds(stopped ? 500 : 400);
        const auto repeatEvery = std::chrono::milliseconds(stopped ? 100 : 140);
        const bool now = keyHeld(k);
        const auto t = std::chrono::steady_clock::now();
        const bool press = now && !down;
        const bool repeat = now && !press && t - since >= repeatAfter
                            && t - last >= repeatEvery;
        down = now;
        if (!press && !repeat) return;
        if (showingSolve() || takeover::armed()) return;
        if (press) { repeats = 0; since = t; }
        last = t;
        if (stopped) {
            if (itermap::seeking() || manualStepPending()) return;
            if (dir > 0) g_probeRequest = 4;   // ...which IS F4
            else         itermap::stepBack(10);
            return;
        }
        if (dir < 0 && itermap::g_seekRestart) return;
        itermap::seekBy(dir * itermap::seekStep(press ? 0 : ++repeats));
    };
    static bool s_rightDown = false, s_leftDown = false;
    static int s_rightRep = 0, s_leftRep = 0;
    static std::chrono::steady_clock::time_point s_rSince, s_rLast, s_lSince, s_lLast;
    transportKey(Key::SeekForward, +1, s_rightDown, s_rightRep, s_rSince, s_rLast);
    transportKey(Key::SeekBack,  -1, s_leftDown,  s_leftRep,  s_lSince, s_lLast);
}

inline void handleProbeRequest(GJBaseGameLayer* layer);

// Rollback-verification state
}  // namespace p1
