#include "monitor_catalog.h"
#include <dxgi1_6.h>
#include <vector>

namespace {
BOOL CALLBACK EnumMonitorProc(HMONITOR hmonitor, HDC, LPRECT, LPARAM data) {
    auto& monitors = *reinterpret_cast<std::vector<MonitorInfo>*>(data);
    MONITORINFOEXW mi{}; mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hmonitor, &mi)) return TRUE;
    MonitorInfo info{};
    info.deviceName = mi.szDevice;
    info.bounds = mi.rcMonitor;
    info.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    DEVMODEW mode{}; mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &mode)) {
        info.description = std::to_wstring(mode.dmPelsWidth) + L" × " + std::to_wstring(mode.dmPelsHeight);
    } else {
        info.description = std::to_wstring(info.width()) + L" × " + std::to_wstring(info.height());
    }
    bool known = false, enabled = false;
    CheckMonitorHdr(info.deviceName, known, enabled);
    info.hdrStatusKnown = known; info.advancedColorEnabled = enabled;
    monitors.push_back(std::move(info));
    return TRUE;
}

bool FindPathForSource(const std::wstring& deviceName, DISPLAYCONFIG_PATH_INFO& found) {
    UINT32 pathCount = 0, modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    LONG result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
    if (result != ERROR_SUCCESS) return false;
    paths.resize(pathCount);
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS && deviceName == source.viewGdiDeviceName) {
            found = path; return true;
        }
    }
    return false;
}
}

bool CheckMonitorHdr(const std::wstring& deviceName, bool& known, bool& enabled) {
    known = false; enabled = false;
    DISPLAYCONFIG_PATH_INFO path{};
    if (!FindPathForSource(deviceName, path)) return false;
    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO info{};
    info.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
    info.header.size = sizeof(info);
    info.header.adapterId = path.targetInfo.adapterId;
    info.header.id = path.targetInfo.id;
    if (DisplayConfigGetDeviceInfo(&info.header) != ERROR_SUCCESS) return false;
    known = true;
    enabled = info.advancedColorEnabled != 0;
    return true;
}

std::vector<MonitorInfo> EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;
    EnumDisplayMonitors(nullptr, nullptr, EnumMonitorProc, reinterpret_cast<LPARAM>(&monitors));
    return monitors;
}
