#pragma once
#include "FrameWindows.hpp"

#include <string>

namespace kpsim::search {

struct WindowStats {
    size_t edges = 0;
    size_t framePerfect = 0;   // window of 1 tick
    size_t removable = 0;
    int narrowest = 0;         // ticks
    double medianWidth = 0;    // ticks
    size_t histogram[6] = {};  // widths 1, 2-3, 4-6, 7-12, 13-24, 25+
    double humanChance = 0;    // every edge hit with a normal timing error of sigma
};

// Bucket of a window width in WindowStats::histogram.
int widthBucket(int width);
extern const char* const kWidthBuckets[6];

WindowStats windowStats(const WindowReport& r, double sigmaMs);

// Plain text, one record per line, for the stats menu.
// `plan` is the fingerprint of the plan the windows were measured on.
std::string writeWindowFile(const WindowReport& r, const std::string& macro, uint64_t plan);
bool readWindowFile(const std::string& text, WindowReport& r, std::string& macro, uint64_t& plan);

} // namespace kpsim::search
