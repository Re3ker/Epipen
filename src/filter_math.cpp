#include "filter_math.h"
#include <algorithm>
#include <cmath>

float SrgbToLinear(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
}
float LinearToSrgb(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}
float StaticFilter(float inputSrgb, int blackFloorSrgb, int whiteCeilingSrgb) {
    const float floor = SrgbToLinear(static_cast<float>(blackFloorSrgb) / 255.0f);
    const float ceiling = SrgbToLinear(static_cast<float>(whiteCeilingSrgb) / 255.0f);
    return floor + (ceiling - floor) * SrgbToLinear(inputSrgb);
}
float DynamicStep(float previous, float target, float rate, float dt) {
    const float clampedDt = std::clamp(dt, 0.0f, 1.0f / 30.0f);
    const float delta = target - previous;
    const float magnitude = std::abs(delta);
    const float factor = magnitude < 1e-6f ? 1.0f : std::min(1.0f, rate * clampedDt / magnitude);
    return previous + factor * delta;
}
bool ValidateFilterSettings(const FilterSettings& settings) { return ValidateSettings(settings); }
