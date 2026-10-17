#include "settings_store.h"
#include <shlobj.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <vector>

namespace {
std::wstring ReadString(const std::filesystem::path& path, const wchar_t* section,
                        const wchar_t* key, const wchar_t* fallback) {
    std::array<wchar_t, 512> buffer{};
    GetPrivateProfileStringW(section, key, fallback, buffer.data(),
                             static_cast<DWORD>(buffer.size()), path.c_str());
    return buffer.data();
}

bool WriteUtf16File(const std::filesystem::path& path, const std::wstring& contents) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const WORD bom = 0xFEFF;
    std::vector<BYTE> bytes(sizeof(bom) + contents.size() * sizeof(wchar_t));
    std::memcpy(bytes.data(), &bom, sizeof(bom));
    if (!contents.empty())
        std::memcpy(bytes.data() + sizeof(bom), contents.data(), contents.size() * sizeof(wchar_t));
    DWORD written = 0;
    const bool wrote = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                       written == bytes.size();
    const bool flushed = wrote && FlushFileBuffers(file);
    CloseHandle(file);
    return flushed;
}
}

std::filesystem::path SettingsPath() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &raw))) return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path / L"Epipen" / L"settings.ini";
}

bool LoadSettings(AppSettings& settings, std::wstring& warning) {
    settings = AppSettings{};
    warning.clear();
    const auto path = SettingsPath();
    if (path.empty()) {
        warning = L"The local configuration folder is unavailable; defaults are in use.";
        return false;
    }
    if (!std::filesystem::exists(path)) return true;

    AppSettings loaded{};
    loaded.version = GetPrivateProfileIntW(L"app", L"version", 0, path.c_str());
    loaded.monitor.deviceName = ReadString(path, L"monitor", L"device", L"");
    loaded.filter.blackFloorSrgb = GetPrivateProfileIntW(L"filter", L"black_floor", 16, path.c_str());
    loaded.filter.whiteCeilingSrgb = GetPrivateProfileIntW(L"filter", L"white_ceiling", 220, path.c_str());
    loaded.filter.temporalEnabled = GetPrivateProfileIntW(L"filter", L"temporal_enabled", 1, path.c_str()) != 0;
    const auto rateText = ReadString(path, L"filter", L"change_rate", L"1.0");
    wchar_t* end = nullptr;
    loaded.filter.maxChannelChangePerSecond = wcstof(rateText.c_str(), &end);
    while (end && (*end == L' ' || *end == L'\t' || *end == L'\r' || *end == L'\n')) ++end;
    if (loaded.version != 1 || !ValidateSettings(loaded.filter) || end == rateText.c_str() || !end || *end != L'\0' ||
        !std::isfinite(loaded.filter.maxChannelChangePerSecond)) {
        warning = L"The saved settings are invalid. Safe defaults are in use.";
        return false;
    }
    settings = std::move(loaded);
    return true;
}

bool SaveSettings(const AppSettings& settings, std::wstring& error) {
    error.clear();
    if (!ValidateSettings(settings.filter)) {
        error = L"Invalid filter settings were not saved.";
        return false;
    }
    if (settings.monitor.deviceName.find_first_of(L"\r\n") != std::wstring::npos) {
        error = L"The selected monitor name contains invalid characters.";
        return false;
    }
    const auto path = SettingsPath();
    if (path.empty()) {
        error = L"The local configuration folder is unavailable.";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = L"The configuration folder could not be created.";
        return false;
    }

    wchar_t rate[32]{};
    if (swprintf_s(rate, L"%.6g", static_cast<double>(settings.filter.maxChannelChangePerSecond)) < 0) {
        error = L"The filter rate could not be formatted.";
        return false;
    }
    std::wstring contents = L"[app]\r\nversion=1\r\n\r\n[monitor]\r\ndevice=";
    contents += settings.monitor.deviceName;
    contents += L"\r\n\r\n[filter]\r\nblack_floor=" + std::to_wstring(settings.filter.blackFloorSrgb);
    contents += L"\r\nwhite_ceiling=" + std::to_wstring(settings.filter.whiteCeilingSrgb);
    contents += L"\r\ntemporal_enabled=" + std::wstring(settings.filter.temporalEnabled ? L"1" : L"0");
    contents += L"\r\nchange_rate=";
    contents += rate;
    contents += L"\r\n";

    const std::filesystem::path temp(path.wstring() + L".tmp");
    if (!WriteUtf16File(temp, contents)) {
        error = L"The temporary configuration file could not be written.";
        DeleteFileW(temp.c_str());
        return false;
    }
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = L"The new configuration could not be atomically installed.";
        DeleteFileW(temp.c_str());
        return false;
    }
    return true;
}
