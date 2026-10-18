#pragma once
#include "kpsim/search/FrameWindows.hpp"

#include <filesystem>
#include <functional>
#include <string>

namespace kp {

using CheckEdge = std::function<void(const kpsim::search::EdgeTests&)>;

// Frame window stats of a macro: summary, histogram, tightest clicks first.
// `practice`, when set, adds a button that starts the rhythm practice of this macro; `check`, a
// Check button by each edge whose tries were kept.
void showWindowStats(const kpsim::search::WindowReport& r, const std::string& macro,
                     std::function<void()> practice = {}, CheckEdge check = {});
// From a file written by writeWindowFile; false if it cannot be read.
bool showWindowStatsFile(const std::filesystem::path& path, std::function<void()> practice = {},
                         CheckEdge check = {});

} // namespace kp
