#pragma once
#include "common.h"
#include <filesystem>

std::filesystem::path SettingsPath();
bool LoadSettings(AppSettings& settings, std::wstring& warning);
bool SaveSettings(const AppSettings& settings, std::wstring& error);
