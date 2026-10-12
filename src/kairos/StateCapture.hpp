#pragma once
#include "kpsim/engine/EngineSnap.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kp {

void captureEngineSnap(PlayerObject* player, kpsim::engine::EngineSnap& out);

// Not reproducible between attempts, or only a cache.
bool isVolatileField(const char* name);

// Hash of the reproducible physics fields.
uint64_t physicsHash(const kpsim::engine::EngineSnap& s);

// First differing reproducible fields, empty when equal.
std::string physicsDiff(const kpsim::engine::EngineSnap& expected, const kpsim::engine::EngineSnap& got);

// Everything a checkpoint does not give back on a player.
struct PlayerCopy {
    struct Boxes {
#define PLAYER_BOX(n) decltype(PlayerObject::n) n;
#include "PodFields.inc"
#undef PLAYER_BOX
    } boxes;
    std::vector<uint8_t> bytes;   // the object, for its plain fields
    cocos2d::CCPoint position;
    float rotationX = 0, rotationY = 0, scaleX = 1, scaleY = 1;
};
void copyPlayer(PlayerObject* p, PlayerCopy& out);
void pastePlayer(PlayerObject* p, const PlayerCopy& in);

// The layer's bytes: game state (camera, ...) to paste, the rest to compare.
struct LayerCopy {
    std::vector<uint8_t> bytes;
};
void copyLayer(PlayLayer* layer, LayerCopy& out);
void pasteGameState(PlayLayer* layer, const LayerCopy& in);
// Plain layer fields that differ from the copy.
std::string layerDiff(PlayLayer* layer, const LayerCopy& in);

} // namespace kp
