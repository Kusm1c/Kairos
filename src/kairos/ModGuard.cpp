#include "ModGuard.hpp"
#include "GameEngine.hpp"
#include "Jobs.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>

#include <string_view>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace geode::prelude;

namespace kp {
namespace {

// Functions whose hooks can change the physics a bot plays: the step and its delta (speedhack,
// TPS bypass, Click Between Frames), inputs, deaths (noclip), collisions, the player itself.
constexpr std::string_view kPhysics[] = {
    "GJBaseGameLayer::update", "GJBaseGameLayer::getModifiedDelta", "GJBaseGameLayer::processCommands",
    "GJBaseGameLayer::handleButton", "GJBaseGameLayer::queueButton", "GJBaseGameLayer::checkCollisions",
    "GJBaseGameLayer::collisionCheckObjects", "GJBaseGameLayer::destroyPlayer", "GJBaseGameLayer::updateCamera",
    "PlayLayer::update", "PlayLayer::postUpdate", "PlayLayer::destroyPlayer", "PlayLayer::checkForEnd",
};
// ...and what a bot run calls thousands of times or must not report: resets and checkpoints (slow
// in other mods, and practice fixes change what a restore brings back), completions (records).
constexpr std::string_view kRun[] = {
    "PlayLayer::resetLevel", "PlayLayer::resetLevelFromStart", "PlayLayer::fullReset", "PlayLayer::delayedResetLevel",
    "PlayLayer::delayedFullReset", "PlayLayer::createCheckpoint", "PlayLayer::storeCheckpoint",
    "PlayLayer::loadFromCheckpoint", "PlayLayer::loadLastCheckpoint", "PlayLayer::markCheckpoint",
    "PlayLayer::removeCheckpoint", "PlayLayer::removeAllCheckpoints", "PlayLayer::levelComplete",
    "PlayLayer::playEndAnimationToPos", "PlayLayer::showCompleteText", "PlayLayer::showCompleteEffect",
    "GJBaseGameLayer::resetLevelVariables", "GJBaseGameLayer::resetPlayer",
};
constexpr std::string_view kPrefixes[] = {"PlayerObject::", "CheckpointObject::"};

bool physicsHook(std::string_view name) {
    for (auto p : kPrefixes)
        if (name.substr(0, p.size()) == p) return true;
    for (auto n : kPhysics)
        if (n == name) return true;
    for (auto n : kRun)
        if (n == name) return true;
    return false;
}

// The game's own module: a patch there may change what it computes.
bool inGame(uintptr_t addr) {
    static const auto [lo, hi] = [] {
        const uintptr_t base = geode::base::get();
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        return std::pair{base, base + nt->OptionalHeader.SizeOfImage};
    }();
    return addr >= lo && addr < hi;
}

struct Paused {
    std::vector<Hook*> hooks;
    std::vector<Patch*> patches;
    bool on = false;
} g_paused;

bool driving() { return solverDriving() || GameEngine::active() || watchActive(); }

void pause() {
    for (Mod* m : Loader::get()->getAllMods()) {
        if (!m || m == Mod::get() || !m->isLoaded() || m->isInternal()) continue;
        size_t hooks = 0, patches = 0;
        for (Hook* h : m->getHooks())
            if (h->isEnabled() && physicsHook(h->getDisplayName()) && h->disable().isOk()) {
                g_paused.hooks.push_back(h);
                ++hooks;
            }
        for (Patch* p : m->getPatches())
            if (p->isEnabled() && inGame(p->getAddress()) && p->disable().isOk()) {
                g_paused.patches.push_back(p);
                ++patches;
            }
        if (hooks || patches)
            log::info("[kairos] {} paused while a bot drives: {} hooks, {} game patches", m->getID(), hooks, patches);
    }
}

void resume() {
    for (Hook* h : g_paused.hooks) (void)h->enable();
    for (Patch* p : g_paused.patches) (void)p->enable();
    if (!g_paused.hooks.empty() || !g_paused.patches.empty()) log::info("[kairos] paused mods resumed");
    g_paused.hooks.clear();
    g_paused.patches.clear();
}

} // namespace

void updateModGuard() {
    const bool want = driving();
    if (want == g_paused.on) return;
    if (want) pause();
    else resume();
    g_paused.on = want;
}

} // namespace kp

class $modify(KairosGuardScheduler, CCScheduler) {
    static void onModify(auto& self) {
        // Before the frame runs anything, so no paused hook is mid-call.
        (void)self.setHookPriority("cocos2d::CCScheduler::update", geode::Priority::First);
    }
    void update(float dt) {
        kp::updateModGuard();
        CCScheduler::update(dt);
    }
};
