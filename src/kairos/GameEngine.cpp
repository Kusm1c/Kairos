#include "GameEngine.hpp"
#include "Fiber.hpp"

#include <Geode/modify/FMODAudioEngine.hpp>
#include <Geode/modify/GJEffectManager.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/GameStatsManager.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <chrono>

using namespace geode::prelude;
using kpsim::search::Outcome;

namespace kp {
GameEngine* GameEngine::s_active = nullptr;

bool engineResetting() { return GameEngine::active() && GameEngine::active()->ownReset(); }
bool engineStepping() { return GameEngine::active() && !GameEngine::active()->frozen(); }

namespace {
constexpr Tick kChunk = 240;
constexpr Tick kFirstChunk = 8;   // runs that may stop early start small

// GD's random generators (2.2081). Their values in a fresh game.
constexpr uintptr_t kSeedTrigger = 0x6c2e90;    // Random triggers, reseeded by resetLevel
constexpr uintptr_t kSeedVarIndex = 0x6c2ee0;   // drawn by resetObject
constexpr uintptr_t kSeedVarTable = 0x6c2ef8;
constexpr int64_t kFreshVarIndex = 2531011, kFreshVarTable = 0;
constexpr size_t kLayerSeed = 0x32e0, kLayerSeedCopy = 0x32e8;
static_assert(offsetof(GJBaseGameLayer, m_useReplay) == 0x3190, "layout differs from 2.2081");

int64_t& global(uintptr_t rva) { return *reinterpret_cast<int64_t*>(geode::base::get() + rva); }
int64_t& field(PlayLayer* l, size_t off) { return *reinterpret_cast<int64_t*>(reinterpret_cast<uint8_t*>(l) + off); }

uint64_t mix(uint64_t a, uint64_t b) { return (a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2))); }
} // namespace

GameEngine::GameEngine(PlayLayer* layer) : m_layer(layer) {
    s_active = this;
    m_twoPlayer = layer->m_levelSettings && layer->m_levelSettings->m_twoPlayerMode;
    // handleButton swaps the buttons when "flip 2-player controls" is on.
    m_flip = !layer->m_isPlatformer && !layer->m_useReplay && GameManager::get()->getGameVariable("0010");
}

GameEngine::~GameEngine() {
    m_saves.clear();
    if (s_active == this) s_active = nullptr;
}

void GameEngine::checkCancel() {
    if (m_cancel) throw kpsim::search::Cancelled();
    if (m_invalid) throw kpsim::search::EngineError("the level was restarted during the search");
    if (m_deathBlocked)
        throw kpsim::search::EngineError("a death was cancelled by another mod (noclip?). Turn it off and retry.");
}

void GameEngine::resetLayer() {
    // Same object variances as on the attempt that took the snapshot.
    global(kSeedVarIndex) = kFreshVarIndex;
    global(kSeedVarTable) = kFreshVarTable;
    m_ownReset = true;
    m_layer->resetLevel();
    m_ownReset = false;
    m_stop = Outcome::Reached;
    m_held[0] = m_held[1] = false;
}

void GameEngine::reset() {
    FiberTask::yieldIfDue();
    checkCancel();
    m_layer->m_checkpointArray->removeAllObjects();
    resetLayer();
    global(kSeedTrigger) = 0;
    field(m_layer, kLayerSeed) = 0;
    field(m_layer, kLayerSeedCopy) = 0;
    m_tick = 0;
}

int GameEngine::save() {
    if (m_held[0] || m_held[1] || m_stop != Outcome::Reached) return -1;
    auto t0 = std::chrono::steady_clock::now();
    CheckpointObject* cp = m_layer->createCheckpoint();
    if (!cp) return -1;
    Save s;
    s.checkpoint = cp;
    s.tick = m_tick;
    for (int p = 0; p < 2; ++p) copyPlayer(player(p), s.players[p]);
    copyLayer(m_layer, s.layer);
    s.seeds[0] = global(kSeedTrigger);
    s.seeds[1] = global(kSeedVarIndex);
    s.seeds[2] = global(kSeedVarTable);
    s.seeds[3] = field(m_layer, kLayerSeed);
    s.seeds[4] = field(m_layer, kLayerSeedCopy);
    int id = m_nextId++;
    m_saves.emplace(id, std::move(s));
    ++m_stats.saves;
    m_stats.saveMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return id;
}

bool GameEngine::restore(int id) {
    FiberTask::yieldIfDue();   // long: the frame first if it is due
    checkCancel();
    auto it = m_saves.find(id);
    if (it == m_saves.end()) return false;
    const Save& s = it->second;
    auto t0 = std::chrono::steady_clock::now();
    // resetLevel reloads the last checkpoint of the array.
    auto* array = m_layer->m_checkpointArray;
    array->removeAllObjects();
    array->addObject(s.checkpoint);
    resetLayer();
    array->removeAllObjects();
    auto t1 = std::chrono::steady_clock::now();
    m_stats.resetMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    // What the checkpoint does not bring back.
    for (int p = 0; p < 2; ++p) pastePlayer(player(p), s.players[p]);
    pasteGameState(m_layer, s.layer);
    m_stats.pasteMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    global(kSeedTrigger) = s.seeds[0];
    global(kSeedVarIndex) = s.seeds[1];
    global(kSeedVarTable) = s.seeds[2];
    field(m_layer, kLayerSeed) = s.seeds[3];
    field(m_layer, kLayerSeedCopy) = s.seeds[4];
    m_tick = s.tick;
    m_lastRestoreDiff = layerDiff(m_layer, s.layer);
    ++m_stats.restores;
    m_stats.restoreMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return true;
}

void GameEngine::restoreMismatch(Tick snapshot, Tick diverged) {
    if (++m_stats.mismatches <= 8)
        log::warn("[kp] snapshot @{} replays differently from tick {}; layer fields not restored: {}", snapshot,
                  diverged, m_lastRestoreDiff.empty() ? "none" : m_lastRestoreDiff);
}

void GameEngine::drop(int id) { m_saves.erase(id); }

void GameEngine::advance(Tick n) {
    float warp = std::min(m_layer->m_gameState.m_timeWarp, 1.f);
    if (warp <= 0.f) warp = 1.f;
    if (!m_layer->m_started) throw kpsim::search::EngineError("the level has not started");
    m_layer->m_resumeTimer = 0;   // would skip this update's physics
    m_layer->m_extraDelta = 0.0;  // dt remainder would drift the step count
    m_expected = m_tick + n;
    m_seen = 0;
    m_inUpdate = true;
    auto t0 = std::chrono::steady_clock::now();
    static_cast<GJBaseGameLayer*>(m_layer)->update((float)n * warp / 240.f);
    m_stats.updateMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ++m_stats.updates;
    m_inUpdate = false;
    if (m_stop == Outcome::Reached) m_layer->checkForEnd();   // end on the chunk's last step
    if (m_seen != n && !(m_stop != Outcome::Reached && m_seen < n))
        throw kpsim::search::EngineError(fmt::format(
            "the game ran {} physics steps instead of {} (TPS bypass, speedhack or time warp?)", m_seen, n));
}

RunResult GameEngine::run(const Plan& plan, Tick until, const Trace* trace) {
    checkCancel();
    struct Scope {
        GameEngine& e;
        ~Scope() { e.m_plan = nullptr; e.m_trace = nullptr; }
    } scope{*this};
    m_plan = &plan;
    m_trace = trace;
    // The game kept stepping after an early stop: restore first.
    if (m_stop == Outcome::Converged || m_stop == Outcome::Diverged)
        throw kpsim::search::EngineError("internal: run after an early stop");
    const bool mayStop = trace && (trace->known || trace->expect || trace->nearKnown || trace->looseKnown);
    Tick chunk = m_stepsPerUpdate ? m_stepsPerUpdate : mayStop ? kFirstChunk : kChunk;
    while (m_stop == Outcome::Reached && m_tick < until) {
        advance(std::min(until - m_tick, chunk));
        if (!m_stepsPerUpdate) chunk = std::min(chunk * 2, kChunk);
        FiberTask::yieldIfDue();
        checkCancel();
    }
    RunResult r{m_stop, m_stop == Outcome::Reached ? m_tick : m_stopTick, m_stopEnd, m_deathPlayer};
    return r;
}

uint64_t GameEngine::stateHash() {
    kpsim::engine::EngineSnap a, b;
    captureEngineSnap(m_layer->m_player1, a);
    captureEngineSnap(m_layer->m_player2, b);
    return mix(physicsHash(a), physicsHash(b));
}

float GameEngine::playerX() { return m_layer->m_player1 ? m_layer->m_player1->getPositionX() : 0.f; }
float GameEngine::playerY(int player) {
    auto* p = player ? m_layer->m_player2 : m_layer->m_player1;
    return p ? p->getPositionY() : 0.f;
}

int GameEngine::mode(int player) {
    auto* p = player ? m_layer->m_player2 : m_layer->m_player1;
    if (!p) return -1;
    using namespace kpsim::search;
    return p->m_isShip ? kShip : p->m_isBird ? kUfo : p->m_isBall ? kBall : p->m_isDart ? kWave
         : p->m_isRobot ? kRobot : p->m_isSpider ? kSpider : p->m_isSwing ? kSwing : kCube;
}

int GameEngine::onGround(int player) {
    auto* p = player ? m_layer->m_player2 : m_layer->m_player1;
    return p ? (p->m_isOnGround ? 1 : 0) : -1;
}

void GameEngine::onStep(GJBaseGameLayer* layer) {
    ++m_seen;
    if (m_stop != Outcome::Reached || m_tick >= m_expected) return;
    m_layer->checkForEnd();   // exact end: state after step m_tick
    if (m_stop != Outcome::Reached) return;
    observe();
    if (m_stop != Outcome::Reached) return;
    if (forbidden && m_tick)
        if (auto* p = forbiddenPlayer ? m_layer->m_player2 : m_layer->m_player1;
            p && forbidden(m_layer->m_player1->getPositionX(), p->getPositionY())) {
            m_stop = Outcome::Died;
            m_stopTick = m_tick;
            m_deathPlayer = forbiddenPlayer;
            return;
        }
    ++m_tick;
    ++m_stats.steps;
    for (int p = 0; p < 2; ++p) {
        bool want = m_plan && m_plan->heldAt(m_tick, p);
        if (want == m_held[p]) continue;
        m_injecting = true;
        layer->handleButton(want, 1, (p == 0) != m_flip);
        m_injecting = false;
        m_held[p] = want;
    }
}

// Seen at the head of the next step, the same point in every run.
void GameEngine::observe() {
    if (!m_trace || m_tick == 0) return;
    kpsim::engine::EngineSnap a, b;
    captureEngineSnap(m_layer->m_player1, a);
    captureEngineSnap(m_layer->m_player2, b);
    const uint64_t h = mix(physicsHash(a), physicsHash(b));
    if (auto* rec = m_trace->record) {
        if (rec->size() < m_tick) rec->resize(m_tick, 0);
        (*rec)[m_tick - 1] = h;
    }
    if (auto* exp = m_trace->expect; exp && m_tick <= exp->size()) {
        uint64_t want = (*exp)[m_tick - 1];
        if (want && want != h) {
            m_stop = Outcome::Diverged;
            m_stopTick = m_tick;
            return;
        }
    }
    if (auto* known = m_trace->known; known && m_tick >= m_trace->knownFrom) {
        auto it = known->find(kpsim::search::stateKey(m_tick, h));
        if (it != known->end()) {
            m_stop = Outcome::Converged;
            m_stopTick = m_tick;
            m_stopEnd = it->second;
            return;
        }
    }
    if (m_trace->nearRecord || m_trace->nearKnown) {
        const uint64_t n = mix(physicsHash(a, kpsim::search::kNearGrid), physicsHash(b, kpsim::search::kNearGrid));
        if (auto* rec = m_trace->nearRecord) {
            if (rec->size() < m_tick) rec->resize(m_tick, 0);
            (*rec)[m_tick - 1] = n;
        }
        if (auto* nearKnown = m_trace->nearKnown; nearKnown && m_tick >= m_trace->knownFrom) {
            auto it = nearKnown->find(kpsim::search::stateKey(m_tick, n));
            if (it != nearKnown->end()) {
                m_stop = Outcome::Converged;
                m_stopTick = m_tick;
                m_stopEnd = it->second;
                return;
            }
        }
    }
    if (!m_trace->looseRecord && !m_trace->looseKnown) return;
    const int md = mode(0);
    auto* p1 = m_layer->m_player1;
    if (!p1 || !kpsim::search::flyingMode(md)) return;
    const auto cell = kpsim::search::looseCell(md, p1->m_isUpsideDown, p1->getPositionY(), p1->m_yVelocity);
    if (auto* rec = m_trace->looseRecord) {
        if (rec->size() < m_tick) rec->resize(m_tick, 0);
        (*rec)[m_tick - 1] = kpsim::search::looseKey(cell);
    }
    Tick end = 0;
    if (auto* lk = m_trace->looseKnown; lk && m_tick >= m_trace->knownFrom &&
                                        kpsim::search::looseFind(*lk, m_tick, cell, end)) {
        m_stop = Outcome::Converged;
        m_stopTick = m_tick;
        m_stopEnd = end;
    }
}

void GameEngine::onDeath(PlayerObject* p) {
    if (m_stop != Outcome::Reached) return;
    m_stop = Outcome::Died;
    m_stopTick = m_tick;
    m_deathPlayer = p == m_layer->m_player2 ? 1 : 0;
}

void GameEngine::onDeathBlocked() { m_deathBlocked = true; }

void GameEngine::onFinish() {
    if (m_stop != Outcome::Reached) return;
    m_stop = Outcome::Finished;
    m_stopTick = m_tick;
}
} // namespace kp

namespace {
// Inside one of the engine's restores: the time goes to `field`.
struct RestoreTimer {
    double* field = nullptr;
    std::chrono::steady_clock::time_point t0;
    RestoreTimer(double kp::EngineStats::*f) {
        auto* e = kp::GameEngine::active();
        if (!e || !e->ownReset()) return;
        field = &(e->statsRef().*f);
        t0 = std::chrono::steady_clock::now();
    }
    ~RestoreTimer() {
        if (field) *field += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
};

kp::GameEngine* engineFor(GJBaseGameLayer* layer) {
    auto* e = kp::GameEngine::active();
    return e && static_cast<GJBaseGameLayer*>(e->layer()) == layer ? e : nullptr;
}
} // namespace

class $modify(KpEngineLayer, GJBaseGameLayer) {
    void update(float dt) {
        if (auto* e = engineFor(this); e && e->frozen()) return;   // only our steps
        GJBaseGameLayer::update(dt);
    }
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (auto* e = engineFor(this); e && !e->frozen()) e->onStep(this);
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }
    void handleButton(bool down, int button, bool isPlayer1) {
        if (auto* e = engineFor(this); e && !e->injecting()) return;   // no real input
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
    // Timed inside a restore (EngineStats::varsMs).
    void resetLevelVariables() {
        auto* e = engineFor(this);
        if (!e || !e->ownReset()) return GJBaseGameLayer::resetLevelVariables();
        const auto t0 = std::chrono::steady_clock::now();
        GJBaseGameLayer::resetLevelVariables();
        e->statsRef().varsMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    void pauseAudio() {
        if (auto* e = engineFor(this); e && e->ownReset()) {
            ++e->statsRef().audioSkips;
            return;
        }
        GJBaseGameLayer::pauseAudio();
    }
    void processActivatedAudioTriggers(float levelTime) {
        if (auto* e = engineFor(this); e && e->ownReset()) {
            ++e->statsRef().audioSkips;
            return;
        }
        GJBaseGameLayer::processActivatedAudioTriggers(levelTime);
    }
    // As gdsolver's secshaderskip: visual only, put back by the next real reset.
    void updateShaderLayer(float dt) {
        if (auto* e = engineFor(this); e && e->shaderSkip()) return;
        GJBaseGameLayer::updateShaderLayer(dt);
    }
};

class $modify(KpEnginePlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        // Outermost, to see deaths another mod cancels.
        (void)self.setHookPriority("PlayLayer::destroyPlayer", geode::Priority::First);
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        auto* e = engineFor(this);
        // PlayLayer::destroyPlayer returns early only in these cases.
        bool lethal = e && m_player1 && !m_player1->m_isLocked && !m_playerDied &&
                      !(object && m_anticheatSpike && object->m_uniqueID == m_anticheatSpike->m_uniqueID);
        PlayLayer::destroyPlayer(player, object);
        if (!e) return;
        if (m_playerDied) e->onDeath(player ? player : m_player1);
        else if (lethal) e->onDeathBlocked();
    }
    void delayedResetLevel() {
        if (engineFor(this)) return;
        PlayLayer::delayedResetLevel();
    }
    // Timed inside a restore (EngineStats::loadMs).
    void loadFromCheckpoint(CheckpointObject* cp) {
        auto* e = engineFor(this);
        if (!e || !e->ownReset()) return PlayLayer::loadFromCheckpoint(cp);
        const auto t0 = std::chrono::steady_clock::now();
        if (cp) e->statsRef().savedObjects += cp->m_vectorSavedObjectStateRef.size();
        ++e->statsRef().loads;
        PlayLayer::loadFromCheckpoint(cp);
        e->statsRef().loadMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    void processLoadedMoveActions() {
        RestoreTimer t(&kp::EngineStats::movesMs);
        PlayLayer::processLoadedMoveActions();
    }
    void playEndAnimationToPos(cocos2d::CCPoint position) {
        if (auto* e = engineFor(this)) { e->onFinish(); return; }   // no animation
        PlayLayer::playEndAnimationToPos(position);
    }
    void levelComplete() {
        if (auto* e = engineFor(this)) { e->onFinish(); return; }
        PlayLayer::levelComplete();
    }
    void startMusic() {
        if (engineFor(this)) return;
        PlayLayer::startMusic();
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        if (auto* e = engineFor(this); e && !e->ownReset()) e->invalidate();
    }
};

class $modify(KpEngineEffects, GJEffectManager) {
    void loadFromState(EffectManagerState& state) {
        RestoreTimer t(&kp::EngineStats::effectsMs);
        GJEffectManager::loadFromState(state);
    }
};

class $modify(KpEnginePlayer, PlayerObject) {
    void loadFromCheckpoint(PlayerCheckpoint* cp) {
        RestoreTimer t(&kp::EngineStats::playersMs);
        PlayerObject::loadFromCheckpoint(cp);
    }
};

class $modify(KpEngineAudio, FMODAudioEngine) {
    void loadAudioState(FMODAudioState& state) {
        if (auto* e = kp::GameEngine::active(); e && e->ownReset()) {
            ++e->statsRef().audioSkips;
            return;
        }
        FMODAudioEngine::loadAudioState(state);
    }
    int playEffect(gd::string path, float speed, float p2, float volume) {
        if (kp::GameEngine::active()) return 0;
        return FMODAudioEngine::playEffect(path, speed, p2, volume);
    }
};

class $modify(KpEngineStats, GameStatsManager) {
    void incrementStat(char const* key, int amount) {
        if (kp::GameEngine::active()) return;
        GameStatsManager::incrementStat(key, amount);
    }
};
