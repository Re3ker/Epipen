#pragma once
#include "common.h"
#include <vector>

std::vector<MonitorInfo> EnumerateMonitors();
bool CheckMonitorHdr(const std::wstring& deviceName, bool& known, bool& enabled);
