// GameManager / FMODAudioEngine / AppDelegate / GameStatsManager hooks: achievement and stat blocking, music, focus.
#include "mod/playlayer_helpers.hpp"

using namespace p1;

inline bool statBlockActive() { return botDriving(); }

class $modify(GJGameLevel) {
    void savePercentage(int percent, bool isPracticeMode, int clicks, int attempts,
                        bool isChkValid) {
        if (progressBlockActive()) { ++g_blockedProgress; return; }
        GJGameLevel::savePercentage(percent, isPracticeMode, clicks, attempts, isChkValid);
    }
    void saveNewScore(int value, int type, int ticks, int clicks, int coins,
                      gd::string inputs, bool save) {
        if (progressBlockActive()) { ++g_blockedProgress; return; }
        GJGameLevel::saveNewScore(value, type, ticks, clicks, coins, inputs, save);
    }
};

class $modify(GameManager) {
    // The percentage GD records for the level (and reports for the leaderboard).
    void reportPercentageForLevel(int levelID, int percentage, bool isPlatformer) {
        if (progressBlockActive()) { ++g_blockedProgress; return; }
        GameManager::reportPercentageForLevel(levelID, percentage, isPlatformer);
    }
    void reportAchievementWithID(char const* key, int percent, bool dontNotify) {
        if (statBlockActive()) {
            ++g_blockedAch;
            static std::set<std::string> s_seen;
            if (key && s_seen.insert(key).second)
                log::info("achievement blocked during solve session: {}", key);
            return;
        }
        GameManager::reportAchievementWithID(key, percent, dontNotify);
    }
};

class $modify(FMODAudioEngine) {
    static void notedSound(const char* what, const gd::string& path) {
        if (!solveSession() || g_dpShowSolution) return;
        if (++g_soundWhileSolving <= 5)
            writeResult(std::string("audio: ") + what + " played while solving: "
                        + std::string(path.c_str())
                        + " (screen=" + (renderingOn() ? "on" : "off")
                        + " fast=" + (fastModeActive() ? "1" : "0") + ")");
    }
    static void notedBlocked(const char* what, const gd::string& path) {
        if (!solveSession() || g_dpShowSolution) return;
        if (++g_soundBlockedWhileSolving <= 5)
            writeResult(std::string("audio: ") + what + " REFUSED while solving: "
                        + std::string(path.c_str()));
    }
    void playMusic(gd::string path, bool shouldLoop, float fadeInTime, int channel) {
        if (audio::silent()) { notedBlocked("music", path); return; }
        if (audio::effectsMuted()) {
            audio::holdMusic(path, shouldLoop, fadeInTime, channel);
            return;
        }
        notedSound("music", path);
        FMODAudioEngine::playMusic(path, shouldLoop, fadeInTime, channel);
    }
    void loadAndPlayMusic(gd::string path, unsigned int time, int musicID) {
        if (audio::silent() || audio::effectsMuted()) { notedBlocked("loadAndPlayMusic", path); return; }
        notedSound("loadAndPlayMusic", path);
        FMODAudioEngine::loadAndPlayMusic(path, time, musicID);
    }
    void queueStartMusic(gd::string path, float pitch, float unk, float volume, bool loop,
                         int start, int end, int fadeIn, int fadeOut, int musicID, bool p10,
                         int channelID, bool noPrepare, bool dontReset) {
        if (audio::silent() || audio::effectsMuted()) { notedBlocked("queueStartMusic", path); return; }
        notedSound("queueStartMusic", path);
        FMODAudioEngine::queueStartMusic(path, pitch, unk, volume, loop, start, end, fadeIn,
                                         fadeOut, musicID, p10, channelID, noPrepare, dontReset);
    }
    int queuePlayEffect(gd::string path, float speed, float unk, float volume, float pitch,
                        bool fft, bool reverb, int start, int end, int fadeIn, int fadeOut,
                        bool loop, int effectID, bool override, int uniqueID, float minInterval,
                        int group) {
        if (audio::silent() || audio::effectsMuted()) { notedBlocked("queuePlayEffect", path); return 0; }
        notedSound("queuePlayEffect", path);
        return FMODAudioEngine::queuePlayEffect(path, speed, unk, volume, pitch, fft, reverb,
                                                start, end, fadeIn, fadeOut, loop, effectID,
                                                override, uniqueID, minInterval, group);
    }
    void setMusicTimeMS(unsigned int time, bool dontWait, int musicID) {
        if (secsolve::g_active) { ++g_secSongSeeksSkipped; return; }
        FMODAudioEngine::setMusicTimeMS(time, dontWait, musicID);
    }
    int playEffect(gd::string path, float speed, float unknown, float volume) {
        if (audio::silent() || audio::effectsMuted()) { notedBlocked("effect", path); return 0; }
        notedSound("effect", path);
        return FMODAudioEngine::playEffect(path, speed, unknown, volume);
    }
    int playEffectAdvanced(gd::string path, float speed, float unknown, float volume,
                           float pitch, bool fft, bool reverb, int startMillis, int endMillis,
                           int fadeIn, int fadeOut, bool loopEnabled, int effectID,
                           bool override, bool noPreload, int channelID, int uniqueID,
                           float minInterval, int sfxGroup) {
        if (audio::silent() || audio::effectsMuted()) {
            notedBlocked("effectAdv", path);
            return 0;
        }
        notedSound("effectAdv", path);
        return FMODAudioEngine::playEffectAdvanced(path, speed, unknown, volume, pitch, fft,
            reverb, startMillis, endMillis, fadeIn, fadeOut, loopEnabled, effectID,
            override, noPreload, channelID, uniqueID, minInterval, sfxGroup);
    }
};

class $modify(AppDelegate) {
    void applicationDidEnterBackground() {
        if (g_started && !g_sessionOver) {
            ++g_bgBlocked;
            if (g_bgBlocked <= 3)
                writeResult("focus: ignored applicationDidEnterBackground (n="
                    + std::to_string(g_bgBlocked) + ")");
            return;
        }
        AppDelegate::applicationDidEnterBackground();
    }
    void applicationWillResignActive() {
        if (g_started && !g_sessionOver) {
            ++g_resignBlocked;
            if (g_resignBlocked <= 3)
                writeResult("focus: ignored applicationWillResignActive (n="
                    + std::to_string(g_resignBlocked) + ")");
            return;
        }
        AppDelegate::applicationWillResignActive();
    }
    void trySaveGame(bool p0) {
        audio::restoreVolumes();
        AppDelegate::trySaveGame(p0);
    }
};

class $modify(GameStatsManager) {
    void incrementStat(char const* key, int amount) {
        if (statBlockActive()) {
            ++g_blockedStat;
            static std::set<std::string> s_seen;
            if (key && s_seen.insert(key).second)
                log::info("stat increment blocked during solve session: {} (+{})", key, amount);
            return;
        }
        GameStatsManager::incrementStat(key, amount);
    }
    void storeSecretCoin(char const* key) {
        if (statBlockActive()) { ++g_blockedCoin; return; }
        GameStatsManager::storeSecretCoin(key);
    }
    void storeUserCoin(char const* key) {
        if (statBlockActive()) { ++g_blockedCoin; return; }
        GameStatsManager::storeUserCoin(key);
    }

    // ---- the rewards levelComplete hands out ----
    void completedLevel(GJGameLevel* level) {
        if (progressBlockActive()) { ++g_blockedProgress; return; }
        GameStatsManager::completedLevel(level);
    }
    void awardCurrencyForLevel(GJGameLevel* level) {
        if (progressBlockActive()) { ++g_blockedProgress; return; }
        GameStatsManager::awardCurrencyForLevel(level);
    }
    void awardDiamondsForLevel(GJGameLevel* level) {
        if (progressBlockActive()) { ++g_blockedProgress; return; }
        GameStatsManager::awardDiamondsForLevel(level);
    }
    bool awardSecretKey() {
        if (progressBlockActive()) { ++g_blockedProgress; return false; }
        return GameStatsManager::awardSecretKey();
    }
    void checkCoinAchievement(GJGameLevel* level) {
        if (progressBlockActive()) { ++g_blockedProgress; return; }
        GameStatsManager::checkCoinAchievement(level);
    }
};
