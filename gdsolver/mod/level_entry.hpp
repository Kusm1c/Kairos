#pragma once
#include "mod/helpers_game.hpp"
#include "mod/level_warnings.hpp"

using namespace p1;

inline int levelHeaderTwoPlayer(std::string s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return -1;
    s = s.substr(b);
    if (s.rfind("H4sI", 0) == 0) s = std::string(cocos2d::ZipUtils::decompressString(s, false, 0));
    if (s.empty()) return -1;
    const std::string_view all(s);
    return levelwarn::field(all.substr(0, all.find(';')), "kA10") == "1" ? 1 : 0;
}

inline bool suiteLevelSource() {
    g_cfg.levelFile.clear();
    const std::string& dir = suite::g_levelDir;
    if (dir.empty()) return true;
    auto fileOf = [&](int id) { return dir + "/" + std::to_string(id) + ".lvl"; };
    if (!suite::g_levelDirChecked) {
        suite::g_levelDirChecked = true;
        for (size_t i = 1; i < suite::g_levels.size(); ++i)
            if (suite::g_levels[i] <= suite::g_levels[i - 1]) {
                writeResult("suite: refused - with leveldir the levels must be strictly ascending (a "
                            "fixed order, no duplicates); " + std::to_string(suite::g_levels[i])
                            + " follows " + std::to_string(suite::g_levels[i - 1]));
                return false;
            }
        std::string missing;
        for (int id : suite::g_levels) {
            if (id >= 1 && id <= 22) continue;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(fileOf(id), ec))
                missing += (missing.empty() ? "" : ",") + std::to_string(id);
        }
        if (!missing.empty()) {
            writeResult("suite: refused - no level file in leveldir for " + missing
                        + " (nothing else is read for those IDs)");
            return false;
        }
    }
    const int id = suite::current();
    if (id < 1 || id > 22) g_cfg.levelFile = fileOf(id);
    return true;
}

// FNV-1a of a level's bytes, for the `levelsource:` line: which level a run actually played.
inline std::string levelContentSig(const std::string& bytes) {
    uint64_t h = 1469598103934665603ULL;
    for (char c : bytes) {
        h ^= (uint8_t)c;
        h *= 1099511628211ULL;
    }
    char b[24];
    snprintf(b, sizeof(b), "%016llx", (unsigned long long)h);
    return b;
}

inline bool enterConfiguredLevel() {
    if (g_started) return false;
    g_started = true;
    if (solver::g_totalAttempts == 0)
        solver::g_solveStart = std::chrono::steady_clock::now();
    // A suite level is the suite's current ID and nothing else (see suiteLevelSource).
    if (suite::active() && g_cfg.levelId != suite::current()) {
        writeResult("suite: refused - about to enter level " + std::to_string(g_cfg.levelId)
                    + " where the suite is at " + std::to_string(suite::current()));
        return false;
    }
    // Main levels are 1-22; anything else is a saved / online-cached custom level
    GJGameLevel* level = nullptr;
    auto* glm = GameLevelManager::sharedState();
    std::string srcKind, srcBytes;   // for the `levelsource:` line below
    if (!g_cfg.levelFile.empty()) {
        std::ifstream lf(g_cfg.levelFile, std::ios::binary);
        if (!lf.is_open()) {
            log::error("phase1: cannot open levelfile {}", g_cfg.levelFile);
            writeResult("error: levelfile not found");
            return false;
        }
        std::string raw((std::istreambuf_iterator<char>(lf)),
                        std::istreambuf_iterator<char>());
        level = GJGameLevel::create();
        level->m_levelName = "gdsolver calib";
        level->m_levelID = g_cfg.levelId;
        level->m_levelType = g_cfg.levelFileMain ? GJLevelType::Main : GJLevelType::Editor;
        level->m_levelString = cocos2d::ZipUtils::compressString(raw, false, 0);
        log::info("phase1: built the level from levelfile {} ({} chars)",
                  g_cfg.levelFile, raw.size());
        srcKind = "file";
        srcBytes = raw;
    } else if (g_cfg.levelId >= 1 && g_cfg.levelId <= 22) {
        level = glm->getMainLevel(g_cfg.levelId, false);
        srcKind = "main";
    } else {
        level = glm->getSavedLevel(g_cfg.levelId);
        if (!level && glm->m_onlineLevels)
            level = static_cast<GJGameLevel*>(
                glm->m_onlineLevels->objectForKey(std::to_string(g_cfg.levelId)));
        srcKind = "saved";
    }
    if (!level) {
        log::error("phase1: level {} not found (main or saved)", g_cfg.levelId);
        writeResult("error: level not found");
        return false;
    }
    if (srcKind != "file") srcBytes = std::string(level->m_levelString);
    writeResult("levelsource: id=" + std::to_string(g_cfg.levelId) + " kind=" + srcKind
                + " sig=" + levelContentSig(srcBytes) + " bytes=" + std::to_string(srcBytes.size()));
    writeResult(fmt::format(
        "levelinfo: id={} type={} levelVersion={} gameVersion={} "
        "twoPlayer={} listTwoPlayer={} objCount={}",
        g_cfg.levelId, (int)level->m_levelType, level->m_levelVersion,
        level->m_gameVersion,
        levelHeaderTwoPlayer(srcKind == "main"
                                 ? std::string(LocalLevelManager::sharedState()->getMainLevelString(
                                       g_cfg.levelId))
                                 : srcBytes),
        (int)level->m_twoPlayerMode, (int)level->m_objectCount).c_str());
    log::info("phase1: entering level {}", g_cfg.levelId);
    g_forceCleanStart = true;
    auto* scene = PlayLayer::scene(level, false, false);
    CCDirector::sharedDirector()->replaceScene(CCTransitionFade::create(0.5f, scene));
    return true;
}

inline bool swapLevelNow(const std::string& path) {
    std::ifstream lf(path, std::ios::binary);
    if (!lf.is_open()) {
        writeResult("swaplevel: cannot open " + path);
        return false;
    }
    std::string raw((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
    auto* level = GJGameLevel::create();
    level->m_levelName = "gdsolver swap";
    level->m_levelID = g_cfg.levelId;
    level->m_levelType = g_cfg.levelFileMain ? GJLevelType::Main : GJLevelType::Editor;
    level->m_levelString = cocos2d::ZipUtils::compressString(raw, false, 0);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - g_swapT0).count();
    g_forceCleanStart = true;
    auto* scene = PlayLayer::scene(level, false, false);
    CCDirector::sharedDirector()->replaceScene(scene);
    writeResult("swaplevel: read+compressed " + std::to_string(raw.size()) + " chars in "
                + std::to_string(ms) + " ms; scene replaced");
    return true;
}

class SuiteKeeper : public cocos2d::CCObject {
public:
    static constexpr int kSettleTicks = 10;
    static constexpr int kGiveUpTicks = 600;   // 60 s

    void tick(float) {
        using namespace cocos2d;
        if (!suite::g_advance) return;
        auto* scene = CCDirector::sharedDirector()->getRunningScene();
        const bool clear = PlayLayer::get() == nullptr && scene
                           && !geode::cast::typeinfo_cast<CCTransitionScene*>(scene);
        ++suite::g_waited;
        if (!clear) {
            suite::g_settle = 0;
            if (suite::g_waited < kGiveUpTicks) return;
            suite::g_advance = false;
            writeResult("suite: gave up waiting to leave the level after 60s (playLayer="
                        + std::string(PlayLayer::get() ? "still up" : "gone")
                        + ") - the remaining levels are not run");
            writeResult("suite: done " + std::to_string(suite::g_levels.size()) + " levels");
            if (g_cfg.quitWhenDone)
                Loader::get()->queueInMainThread([] { utils::game::exit(false); });
            return;
        }
        if (++suite::g_settle < kSettleTicks) return;
        suite::g_advance = false;
        suite::g_settle = 0;
        suite::g_waited = 0;
        g_cfg = Config{};
        loadConfig();
        g_cfg.levelId = suite::current();
        openFiles();                      // endSession closed them
        if (!suiteLevelSource()) {
            writeResult("suite: done " + std::to_string(suite::g_levels.size()) + " levels");
            if (g_cfg.quitWhenDone)
                Loader::get()->queueInMainThread([] { utils::game::exit(false); });
            return;
        }
        writeResult("suite: level=" + std::to_string(g_cfg.levelId)
                    + " (" + std::to_string(suite::g_at + 1) + "/"
                    + std::to_string(suite::g_levels.size()) + ")");
        dpsolve::memCensus("session start");
        updateWindowTitle();
        if (enterConfiguredLevel()) return;
        writeResult("suite: level=" + std::to_string(g_cfg.levelId)
                    + " could not be entered - skipped");
        g_started = false;
        if (suite::hasNext()) {
            ++suite::g_at;
            suite::g_advance = true;
            return;
        }
        writeResult("suite: done " + std::to_string(suite::g_levels.size()) + " levels");
        if (g_cfg.quitWhenDone)
            Loader::get()->queueInMainThread([] { utils::game::exit(false); });
    }
};
