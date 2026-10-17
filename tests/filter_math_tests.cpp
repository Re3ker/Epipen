#include "filter_math.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {
int failures = 0;
void Check(bool condition, const char* name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
void Near(float actual, float expected, float tolerance, const char* name) {
    Check(std::abs(actual - expected) <= tolerance, name);
}
}
int main() {
    Near(SrgbToLinear(0.0f), 0.0f, 1e-7f, "sRGB black");
    Near(SrgbToLinear(1.0f), 1.0f, 1e-6f, "sRGB white");
    Near(SrgbToLinear(0.04045f), 0.0031308f, 2e-6f, "sRGB breakpoint");
    Near(LinearToSrgb(SrgbToLinear(0.5f)), 0.5f, 1e-5f, "sRGB roundtrip");
    Near(StaticFilter(0.0f, 16, 220), SrgbToLinear(16.0f / 255.0f), 1e-7f, "black endpoint");
    Near(StaticFilter(1.0f, 16, 220), SrgbToLinear(220.0f / 255.0f), 1e-6f, "white endpoint");
    float previous = -1.0f;
    for (int i = 0; i <= 255; ++i) {
        const float value = StaticFilter(i / 255.0f, 16, 220);
        Check(value >= previous, "static filter monotonic"); previous = value;
    }
    FilterSettings valid{}; Check(ValidateFilterSettings(valid), "default settings valid");
    auto invalid = valid; invalid.blackFloorSrgb = invalid.whiteCeilingSrgb;
    Check(!ValidateFilterSettings(invalid), "equal bounds invalid");
    invalid = valid; invalid.maxChannelChangePerSecond = 10.1f;
    Check(!ValidateFilterSettings(invalid), "rate max enforced");
    Near(DynamicStep(0.5f, 0.5f, 1.0f, 1.0f / 60.0f), 0.5f, 1e-7f, "stable image");
    Near(DynamicStep(0.0f, 1.0f, 1.0f, 1.0f / 60.0f), 1.0f / 60.0f, 1e-6f, "black to white bounded");
    Near(DynamicStep(1.0f, 0.0f, 1.0f, 1.0f / 60.0f), 1.0f - 1.0f / 60.0f, 1e-6f, "white to black bounded");
    Near(DynamicStep(0.0f, 1.0f, 1.0f, 5.0f), 1.0f / 30.0f, 1e-6f, "large time gap clamped");
    Near(DynamicStep(0.9f, 0.2f, 0.0f, 0.016f), 0.9f, 1e-6f, "zero rate does not move");
    if (failures) return EXIT_FAILURE;
    std::cout << "All filter math checks passed.\n";
    return EXIT_SUCCESS;
}
