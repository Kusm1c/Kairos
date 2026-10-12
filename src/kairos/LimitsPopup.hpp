#pragma once
#include <string>

namespace kp {

// "Impossible with your limits": where the level needs faster inputs than the human limits allow,
// with a way to the settings.
void showLimitsPopup(const std::string& levelName, const std::string& why);

} // namespace kp
