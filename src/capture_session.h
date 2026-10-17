#pragma once
#include "common.h"
#include <functional>

struct CaptureResult {
    bool stopped = false;
    bool ready = false;
    std::wstring error;
    HRESULT hr = S_OK;
    uint64_t capturedFrames = 0;
    uint64_t presentedFrames = 0;
    uint64_t supersededFrames = 0;
};

CaptureResult RunCaptureSession(HWND outputWindow, const MonitorInfo& monitor,
                                HANDLE stopEvent, HANDLE settingsChangedEvent,
                                const FilterSettings& initialSettings,
                                const std::function<FilterSettings()>& getSettings,
                                const std::function<void()>& onFirstPresent);
