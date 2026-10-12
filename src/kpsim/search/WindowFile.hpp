#pragma once
#include "FrameWindows.hpp"

#include <string>

namespace kpsim::search {

constexpr int kWidthBucketCount = 11;

struct WindowStats {
    size_t edges = 0;
    size_t framePerfect = 0;   // window of 1 tick
    double perfectAt[3] = {};  // frame-perfect at 60, 120, 240 FPS (expected, any frame phase)
    size_t removable = 0;
    int narrowest = 0;         // ticks
    double medianWidth = 0;    // ticks
    size_t histogram[kWidthBucketCount] = {};   // widths 1..10 ticks, then 11+
    double humanChance = 0;    // every edge hit with a normal timing error of sigma
    double precision = 0;      // NaNDL's: sigma/s for a 24 h expected clear (0: nothing to time)
};

// Bucket of a window width in WindowStats::histogram.
int widthBucket(int width);
extern const char* const kWidthBuckets[kWidthBucketCount];

WindowStats windowStats(const WindowReport& r, double sigmaMs);

// Plain text, one record per line, for the stats menu. Older versions do not read.
// `plan` is the fingerprint of the plan the windows were measured on.
std::string writeWindowFile(const WindowReport& r, const std::string& macro, uint64_t plan);
bool readWindowFile(const std::string& text, WindowReport& r, std::string& macro, uint64_t& plan);

} // namespace kpsim::search
