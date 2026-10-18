#pragma once
// The notifications the mod puts on screen, and the watchdog that takes them back off.
#include "mod/trace_io.hpp"

using namespace p1;

namespace notify {
inline bool g_fix = true;
inline Ref<Notification> g_cur;
inline std::chrono::steady_clock::time_point g_shownAt;
inline bool g_curSeenShowing = false;  // its own clock starts when it reaches the head, not when
                                       // it was created: a queued one has not begun
inline float g_secs = 0.f;
inline int g_stage = 0;             // how far the watchdog has escalated on the current one
inline long long g_forced = 0;      // how many needed forcing (reported at session end)

inline Ref<Notification> g_retiring;
inline std::chrono::steady_clock::time_point g_retiredAt;
inline int g_retireStage = 0;

inline double aliveSec() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - g_shownAt).count();
}
inline double retireSec() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - g_retiredAt).count();
}

inline void retire(Notification* n) {
    if (!n) return;
    n->resumeSchedulerAndActions();
    n->hide();
    g_retiring = n;
    g_retiredAt = std::chrono::steady_clock::now();
    g_retireStage = 0;
}

inline void clear() {
    if (g_cur) {
        if (g_cur->isShowing()) {
            retire(g_cur);        // only the head can be showing, so nothing is displaced here
        } else {
            g_cur->cancel();      // still queued: this takes it out of the queue
        }
    }
    g_cur = nullptr;
    g_stage = 0;
    g_curSeenShowing = false;
}

// The raising itself, with no gate: only the self-test below calls it directly.
inline void showAlways(const std::string& text, NotificationIcon icon, float seconds) {
    clear();                       // never stack ours; the queue is what wedges
    auto* n = Notification::create(text, icon, seconds);
    if (!n) return;
    g_cur = n;
    g_secs = seconds;
    g_shownAt = std::chrono::steady_clock::now();
    g_stage = 0;
    g_curSeenShowing = false;
    n->show();
}

inline void show(const std::string& text, NotificationIcon icon, float seconds) {
    if (g_cfg.quitWhenDone) return;
    showAlways(text, icon, seconds);
}

// Once per frame, from the same place audio::sync runs.
inline void watchdog() {
    if (!g_fix) return;
    if (g_retiring) {
        if (!g_retiring->isShowing()) {
            g_retiring = nullptr;          // it popped; the queue has moved on
            g_retireStage = 0;
        } else if (g_retireStage == 0 && retireSec() > 3.0) {
            g_retireStage = 1;
            g_retiring->resumeSchedulerAndActions();
            g_retiring->hide();            // re-issues the whole chain, pop included
            ++g_forced;
            writeResult("notify: a hidden notification never popped the queue ("
                        + std::to_string((int)retireSec()) + "s) - re-issued its hide");
        } else if (g_retireStage == 1 && retireSec() > 8.0) {
            g_retireStage = 2;
            writeResult("notify: the re-issued hide did not take either - removing the node "
                        "directly (Geode's queue may stay wedged until the scene changes)");
            g_retiring->removeFromParent();
            g_retiring = nullptr;
        }
    }
    if (!g_cur) return;
    if (!g_cur->isShowing()) {
        if (g_stage > 0 || (!g_retiring && aliveSec() > (double)g_secs + 2.0)) {
            g_cur = nullptr;
            g_stage = 0;
        }
        return;
    }
    if (!g_curSeenShowing) {
        g_curSeenShowing = true;
        g_shownAt = std::chrono::steady_clock::now();
    }
    const double over = aliveSec() - ((double)g_secs + 0.8);
    if (over < 2.0) return;
    if (g_stage == 0) {
        g_stage = 1;
        g_cur->resumeSchedulerAndActions();
        g_cur->hide();
        ++g_forced;
        writeResult("notify: a notification outlived its time by "
                    + std::to_string((int)over) + "s - resumed its actions and forced the hide");
        return;
    }
    if (over < 5.0) return;
    writeResult("notify: the forced hide did not take - removing the node directly "
                "(Geode's notification queue may be wedged until the scene changes)");
    g_cur->removeFromParent();
    g_cur = nullptr;
    g_stage = 0;
}

inline bool g_test = false;
inline int g_testStage = 0;
inline std::chrono::steady_clock::time_point g_testT0;

inline void selfTest() {
    if (!g_test) return;
    const double el = (g_testStage == 0) ? 0.0
        : std::chrono::duration<double>(std::chrono::steady_clock::now() - g_testT0).count();
    if (g_testStage == 0) {
        g_testStage = 1;
        g_testT0 = std::chrono::steady_clock::now();
        showAlways("gdsolver: notification self-test", NotificationIcon::Info, 3.f);
        writeResult("notifytest: shown (3s), purging under it in 1s");
        return;
    }
    if (g_testStage == 1 && el >= 1.0) {
        g_testStage = 2;
        // Exactly what the render toggle does (session_hotkeys renderTogglePress).
        purgeDanglingActions();
        writeResult(std::string("notifytest: purged - still showing=")
                    + (g_cur && g_cur->isShowing() ? "yes" : "no")
                    + " rearmed=" + std::to_string(g_forced));
        return;
    }
    if (g_testStage == 2 && el >= 9.0) {
        g_testStage = 3;
        const bool stuck = g_cur && g_cur->isShowing();
        writeResult(std::string("notifytest: ") + (stuck ? "FAIL - still on screen "
                                                           "9s after a purge"
                                                         : "PASS - gone after the purge")
                    + " (rearmed=" + std::to_string(g_forced) + ")");
    }
    if (g_testStage == 3 && el >= 11.0) {
        g_testStage = 4;
        g_testT0 = std::chrono::steady_clock::now();
        showAlways("gdsolver: notification self-test A", NotificationIcon::Info, 3.f);
        writeResult("notifytest2: A shown (3s)");
        return;
    }
    if (g_testStage == 4 && el >= 0.6) {
        g_testStage = 5;
        // A is still up, so show() retires it and B goes into the queue behind A's fade-out...
        showAlways("gdsolver: notification self-test B", NotificationIcon::Info, 3.f);
        purgeDanglingActions();   // ...and the purge lands on that fade-out, pop and all
        writeResult(std::string("notifytest2: B queued, purged under A - A=")
                    + (g_retiring && g_retiring->isShowing() ? "showing" : "gone")
                    + " forced=" + std::to_string(g_forced));
        return;
    }
    if (g_testStage == 5 && el >= 12.0) {
        g_testStage = 6;
        const bool stuckA = g_retiring && g_retiring->isShowing();
        const bool stuckB = g_cur && g_cur->isShowing();
        writeResult(std::string("notifytest2: ")
                    + ((stuckA || stuckB) ? "FAIL - still on screen 12s after the purge"
                                          : "PASS - both gone, queue released")
                    + " (A=" + (stuckA ? "stuck" : "gone")
                    + " B=" + (stuckB ? "stuck" : "gone")
                    + " forced=" + std::to_string(g_forced) + ")");
    }
}
}  // namespace notify

namespace p1 {
inline void notifyActionsPurged() {
    if (!notify::g_fix) return;
    if (notify::g_retiring) {
        if (notify::g_retiring->isShowing()) {
            notify::g_retiring->resumeSchedulerAndActions();
            notify::g_retiring->hide();
            notify::g_retiredAt = std::chrono::steady_clock::now();
            notify::g_retireStage = 0;
            ++notify::g_forced;
        } else {
            notify::g_retiring = nullptr;
        }
    }
    if (!notify::g_cur || !notify::g_cur->isShowing()) return;
    const double left = (double)notify::g_secs - notify::aliveSec();
    notify::g_cur->setTime((float)std::max(0.4, left));
    ++notify::g_forced;
}
}  // namespace p1
