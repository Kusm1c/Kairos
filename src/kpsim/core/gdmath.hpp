#pragma once

namespace kpsim::math {
float  sinf(float x);
float  cosf(float x);
float  tanf(float x);
float  acosf(float x);
float  atanf(float x);
float  atan2f(float y, float x);
double atan2(double y, double x);
float  powf(float x, float y);
float  asinhf(float x);
float  tanhf(float x);

const char* backend();

bool fma3Enabled();

bool setFma3(bool enable);
} // namespace kpsim::math
