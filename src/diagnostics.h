#pragma once
#include "common.h"
#include <string>

void WriteLog(const std::wstring& message, HRESULT hr = S_OK);
void LogState(AppState state, const std::wstring& detail = L"");
