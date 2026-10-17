#pragma once
#include "common.h"

float SrgbToLinear(float value);
float LinearToSrgb(float value);
float StaticFilter(float inputSrgb, int blackFloorSrgb, int whiteCeilingSrgb);
float DynamicStep(float previous, float target, float rate, float dt);
bool ValidateFilterSettings(const FilterSettings& settings);
