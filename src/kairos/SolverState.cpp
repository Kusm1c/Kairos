// The only Kairos file that reads the solver's own state (src/mod).
#include "mod/config.hpp"

#include "ModGuard.hpp"

namespace kp {

bool solverDriving() { return p1::g_started && !p1::g_sessionOver; }

} // namespace kp
