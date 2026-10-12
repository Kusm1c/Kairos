#pragma once

namespace kp {

// The solver (src/mod) is solving or replaying a plan.
bool solverDriving();

// Mods that change input timing or the physics step (Click Between Frames) are paused,
// hooks and patches, while anything drives the game; they come back as they were after.
// Polled every frame; also safe to call directly.
void updateModGuard();

} // namespace kp
