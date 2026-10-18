#include "mod/playlayer_helpers.hpp"

using namespace p1;

// ---- Auto-enter ----
class $modify(P1MenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        static bool s_notified = false;
        if (!s_notified) {
            s_notified = true;
            log::info("gdsolver: loaded. While the bot drives -- solving or replaying -- "
                      "achievements, statistics, coins and the level's own record are all "
                      "blocked. Your own attempts, with the bot idle, record as usual.");
        }
        static bool s_keeperMade = false;
        if (!s_keeperMade) {
            s_keeperMade = true;
            auto* suiteKeeper = new SuiteKeeper();
            CCDirector::sharedDirector()->getScheduler()->scheduleSelector(
                schedule_selector(SuiteKeeper::tick), suiteKeeper, 0.1f, false);
        }
        static bool s_ranOnce = false;
        if (!s_ranOnce) {
            s_ranOnce = true;
            resolveDataDir(); // must be settled BEFORE the first file access
            postmortem::install(); // leave at least one line of crash cause (after DATA_DIR)
            loadConfig();
            if (g_cfg.enabled) {
                openFiles();
                stallwatch::start();
                updateWindowTitle();
                writeResult("session_start", true);
                dpsolve::memCensus("session start");   // cfg memcensus (print only)
                writeResult(dpConfigLine());   // the solver configuration, one line
                writeResult(g_cfg.researchCapture
                    ? "session: research capture on (per-attempt band/groups/recorder-input copies are kept)"
                    : "session: research capture off (per-attempt band/groups/recorder-input copies are not kept)");
                if (g_serveMode) {
                    std::vector<InputCmd> plan;
                    loadPlanExtras(std::string(DATA_DIR) + "/plan_in.txt");
                    if (loadInputsFile(std::string(DATA_DIR) + "/plan_in.txt", plan)) {
                        std::sort(plan.begin(), plan.end(),
                                  [](const InputCmd& a, const InputCmd& b) { return a.step < b.step; });
                        g_cfg.inputs = std::move(plan);
                        writeResult("serve: initial plan "
                            + std::to_string(g_cfg.inputs.size()) + " inputs");
                    }
                }
                if (g_cfg.fps > 0) {
                    auto* gm = GameManager::sharedState();
                    gm->m_vsyncEnabled = false;
                    gm->m_customFPSTarget = (float)g_cfg.fps;
                    gm->updateCustomFPS();
#ifdef GEODE_IS_WINDOWS
                    using SwapIntervalFn = BOOL(WINAPI*)(int);
                    auto ogl = GetModuleHandleA("opengl32.dll");
                    auto getProc = (PROC(WINAPI*)(LPCSTR))GetProcAddress(ogl, "wglGetProcAddress");
                    if (getProc) {
                        auto swapInterval = (SwapIntervalFn)getProc("wglSwapIntervalEXT");
                        if (swapInterval) {
                            swapInterval(0);
                            log::info("phase1: vsync disabled via wglSwapIntervalEXT");
                        }
                    }
#endif
                    log::info("phase1: forced custom FPS to {}", g_cfg.fps);
                }
                this->runAction(CCSequence::create(
                    CCDelayTime::create(g_cfg.delaySec),
                    CCCallFunc::create(this, callfunc_selector(P1MenuLayer::onAutoEnter)),
                    nullptr
                ));
            }
        }
        return true;
    }

    void onAutoEnter() {
        if (suite::active()) {
            writeResult("suite: level=" + std::to_string(suite::current())
                        + " (1/" + std::to_string(suite::g_levels.size()) + ")");
            // Where each level comes from, checked for the whole list before the first one.
            if (!suiteLevelSource()) {
                writeResult("suite: done " + std::to_string(suite::g_levels.size()) + " levels");
                if (g_cfg.quitWhenDone)
                    Loader::get()->queueInMainThread([] { utils::game::exit(false); });
                return;
            }
        }
        enterConfiguredLevel();
    }
};
