#pragma once
// Audio control and fast-mode predicates shared by the hooks.
#include "mod/repair.hpp"

using namespace p1;

// Whether fast mode is actually in effect (accounts for the F8 realtime override)
inline bool fastModeActive() {
    return g_cfg.fastdt > 0 && !g_realtimeOverride;
}

// Declared in session_hotkeys.hpp (namespace p1), where probe:: is not in scope yet
namespace p1 {
inline void clearManualPause() {
    probe::g_pause = false;
    probe::g_step = 0;
}
inline bool manualPaused() { return probe::g_pause; }
inline bool manualStepPending() { return probe::g_step > 0; }
}  // namespace p1

namespace audio {
// Above this notch the song is paused rather than played (a 8x/16x chipmunk is not music)
constexpr float kPitchMax = 4.f;

inline bool  g_silenced = false;     // the "solving / music=mute" stop has been issued
inline bool  g_musicPaused = false;  // WE paused it (never fight a pause GD itself made)
inline bool  g_pausedForSpeed = false; // ...and it was the speed, not a game pause, that did it
inline float g_pitch = 1.f;          // the pitch currently applied to the music group
inline bool  g_reasserted = false;

inline bool effectsMuted() { return itermap::seeking(); }

inline bool silent() {
    if (g_cfg.music == "mute") return true;      // what the driver's workers pass
    if (g_secSearching) return true;
    if (g_dpShowSolution) return false;          // the showing is a replay in every sense
    if (g_cfg.dpSolve) return !renderingOn() || fastModeActive();
    return solvingNow();
}

inline unsigned songPosMs() { return (unsigned)((double)g_tick * (1000.0 / 240.0)); }

inline FMOD::ChannelGroup* musicGroup() {
    auto* fe = FMODAudioEngine::sharedEngine();
    return fe ? fe->m_backgroundMusicChannel : nullptr;
}

inline bool pausedIs(FMOD::ChannelGroup* grp, bool want) {
    bool cur = !want;
    grp->getPaused(&cur);
    return cur == want;
}
inline bool pitchIs(FMOD::ChannelGroup* grp, float want) {
    float cur = want + 1.f;
    grp->getPitch(&cur);
    return cur == want;
}

inline float g_savedVolume = -1.f;   // <0 = we have not taken it

inline float g_savedMusicVol = -1.f;
inline float g_savedSfxVol = -1.f;

inline void hushVolumes() {
    auto* fe = FMODAudioEngine::sharedEngine();
    if (!fe) return;
    if (g_savedMusicVol < 0.f) g_savedMusicVol = fe->getBackgroundMusicVolume();
    if (g_savedSfxVol < 0.f) g_savedSfxVol = fe->getEffectsVolume();
    fe->setBackgroundMusicVolume(0.f);
    fe->setEffectsVolume(0.f);
}

inline void restoreVolumes() {
    if (g_savedMusicVol < 0.f && g_savedSfxVol < 0.f) return;
    if (auto* fe = FMODAudioEngine::sharedEngine()) {
        if (g_savedMusicVol >= 0.f) fe->setBackgroundMusicVolume(g_savedMusicVol);
        if (g_savedSfxVol >= 0.f) fe->setEffectsVolume(g_savedSfxVol);
    }
    g_savedMusicVol = -1.f;
    g_savedSfxVol = -1.f;
}

inline bool g_musicHeld = false;
inline gd::string g_heldPath;
inline bool g_heldLoop = false;
inline float g_heldFade = 0.f;
inline int g_heldChannel = 0;

inline void holdMusic(const gd::string& path, bool loop, float fade, int channel) {
    g_musicHeld = true;
    g_heldPath = path;
    g_heldLoop = loop;
    g_heldFade = fade;
    g_heldChannel = channel;
}

inline void muteForSeek() {
    auto* grp = musicGroup();
    if (!grp) return;
    if (g_savedVolume < 0.f) {
        float v = 1.f;
        grp->getVolume(&v);
        g_savedVolume = v;
    }
    grp->setVolume(0.f);
}

inline void unmuteAfterSeek() {
    if (g_savedVolume < 0.f) return;
    if (auto* grp = musicGroup()) grp->setVolume(g_savedVolume);
    g_savedVolume = -1.f;
}

inline void hushNow() {
    auto* grp = musicGroup();
    if (!grp) return;
    if (g_savedVolume < 0.f) {
        float v = 1.f;
        grp->getVolume(&v);
        g_savedVolume = v;
    }
    grp->setVolume(0.f);
    grp->setPaused(true);
    g_musicPaused = true;
    g_pausedForSpeed = true;    // the arrival must put it back where the game ended up
}

inline void neutral() {
    unmuteAfterSeek();   // ...and a leftover mute would follow them in silence
    restoreVolumes();    // ...as would a volume the mod took to zero and never gave back
    g_musicHeld = false;
    if (auto* grp = musicGroup()) {
        if (g_pitch != 1.f) grp->setPitch(1.f);
        if (g_musicPaused) grp->setPaused(false);
    }
    g_pitch = 1.f;
    g_musicPaused = false;
    g_pausedForSpeed = false;
    g_silenced = false;
}

inline void sync() {
    auto* fe = FMODAudioEngine::sharedEngine();
    if (!fe) return;
    if (!g_started || g_sessionOver) { neutral(); return; }
    if (silent()) {
        if (!g_silenced) {
            g_silenced = true;
            g_musicPaused = false;
            g_pitch = 1.f;
            fe->stopAllMusic(false);
            fe->stopAllEffects();
            log::info("audio: silent ({})", solvingNow() ? "solving"
                                          : (g_cfg.dpSolve ? "solve session" : "music=mute"));
        }
        hushVolumes();
        ++g_silenceReasserts;
        return;
    }
    restoreVolumes();   // out of silence: give the levels back before anything else runs
    g_silenced = false;
    if (itermap::seeking()) muteForSeek();
    auto* grp = fe->m_backgroundMusicChannel;
    if (!grp) return;
    const bool gameStopped = g_paused || probe::g_pause;
    if (itermap::seeking()) {
        muteForSeek();              // re-fetches the group, so a replaced one is caught
        grp->setPaused(true);
        if (!g_musicPaused) {
            g_musicPaused = true;
            g_pausedForSpeed = !gameStopped;   // the arrival must re-seek it
            log::info("audio: music held for the seek");
        }
        return;
    }
    const bool tooFast = g_watchSpeed > kPitchMax;
    const bool wantPaused = gameStopped || tooFast;
    if (wantPaused != g_musicPaused) {
        g_musicPaused = wantPaused;
        g_reasserted = false;
        if (wantPaused) {
            grp->setPaused(true);
            g_pausedForSpeed = !gameStopped;
            log::info("audio: music paused ({})",
                      gameStopped ? "the game is stopped"
                                  : (itermap::seeking() ? "seeking"
                                                        : "above the pitch ceiling"));
        } else if (g_pausedForSpeed) {
            g_pausedForSpeed = false;
            fe->setMusicTimeMS(songPosMs(), false, 0);
            grp->setPaused(false);
            log::info("audio: music resumed, seeked to {}ms (tick {})",
                      songPosMs(), (long long)g_tick);
        } else {
            grp->setPaused(false);
            log::info("audio: music resumed");
        }
    } else if (wantPaused && !pausedIs(grp, true)) {
        grp->setPaused(true);
        if (!g_reasserted) {
            g_reasserted = true;
            log::info("audio: the song came back unpaused (GD restarted it) - pause re-asserted");
        }
    }
    if (!wantPaused && !pitchIs(grp, g_watchSpeed)) {
        if (g_watchSpeed != g_pitch) {
            g_reasserted = false;     // a new intent, not a re-assert
            log::info("audio: pitch {:g}x (follows the spectating speed)", (double)g_watchSpeed);
        } else if (!g_reasserted) {
            g_reasserted = true;
            log::info("audio: the song came back at pitch 1 (GD restarted it) - {:g}x re-applied",
                      (double)g_watchSpeed);
        }
        g_pitch = g_watchSpeed;
        grp->setPitch(g_watchSpeed);
    }
    if (!itermap::seeking() && g_musicHeld) {
        g_musicHeld = false;
        fe->playMusic(g_heldPath, g_heldLoop, g_heldFade, g_heldChannel);
        fe->setMusicTimeMS(songPosMs(), false, 0);
        g_musicPaused = false;
        g_pausedForSpeed = false;
        log::info("audio: the held song started at {}ms (tick {})",
                  songPosMs(), (long long)g_tick);
    }
    if (!itermap::seeking()) unmuteAfterSeek();
}
}  // namespace audio
