#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

enum class AppState { Stopped, Starting, Running, Quiet, Faulted, Stopping };

struct FilterSettings {
    int blackFloorSrgb = 16;
    int whiteCeilingSrgb = 220;
    bool temporalEnabled = true;
    float maxChannelChangePerSecond = 1.0f;
};

struct MonitorSelection { std::wstring deviceName; };
struct AppSettings {
    int version = 1;
    MonitorSelection monitor;
    FilterSettings filter;
};

struct MonitorInfo {
    std::wstring deviceName;
    std::wstring description;
    std::wstring adapterName;
    RECT bounds{};
    bool primary = false;
    bool hdrStatusKnown = false;
    bool advancedColorEnabled = false;
    bool featureLevel11 = false;
    int width() const { return bounds.right - bounds.left; }
    int height() const { return bounds.bottom - bounds.top; }
};

struct FrameMetadata {
    uint64_t sequence = 0;
    int64_t captureTime100ns = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct RuntimeStats {
    double captureFps = 0;
    double presentFps = 0;
    double captureToSubmitMsP50 = 0;
    double captureToSubmitMsP95 = 0;
    double gpuFilterMsP50 = 0;
    double gpuFilterMsP95 = 0;
    uint64_t supersededCaptureFrames = 0;
};

struct AppError {
    std::wstring category;
    std::wstring message;
    std::wstring operation;
    HRESULT hr = S_OK;
};

inline bool ValidateSettings(const FilterSettings& s) {
    return s.blackFloorSrgb >= 0 && s.blackFloorSrgb <= 254 &&
           s.whiteCeilingSrgb >= 1 && s.whiteCeilingSrgb <= 255 &&
           s.blackFloorSrgb < s.whiteCeilingSrgb &&
           s.maxChannelChangePerSecond >= 0.1f && s.maxChannelChangePerSecond <= 10.0f;
}
