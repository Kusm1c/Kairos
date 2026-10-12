#pragma once
#include "EngineSnap.hpp"

namespace kpsim::engine {
struct Callees {
    void (*spiderTestJump)(EngineSnap&) = nullptr;   // PlayerObject::spiderTestJump 0x140394340
};

// PlayerObject::setYVelocity 0x140388d10
void setYVelocity(EngineSnap& s, double v);

double quantizeYVelocity(double v);

// PlayerObject::playerIsFallingBugged 0x14039a430 (pure query).
bool playerIsFallingBugged(const EngineSnap& s);

// PlayerObject::flipGravity 0x14039a1d0.
void flipGravity(EngineSnap& s, bool flip, bool noEffects);

// PlayerObject::updateJump 0x14038b900
void updateJump(EngineSnap& s, float dt, const Callees& callees = {});

// PlayerObject::update 0x140388d80
void update(EngineSnap& s, float dt, const Callees& callees = {});

extern const char* const kNonPhysicsFields[];
} // namespace kpsim::engine
