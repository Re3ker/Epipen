#include "diagnostics.h"
#include "settings_store.h"
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <chrono>

namespace {
std::mutex g_logMutex;
std::wstring StateName(AppState s) {
    switch (s) {
    case AppState::Stopped: return L"Stopped"; case AppState::Starting: return L"Starting";
    case AppState::Running: return L"Running"; case AppState::Quiet: return L"Quiet";
    case AppState::Faulted: return L"Faulted"; case AppState::Stopping: return L"Stopping";
    }
    return L"Unknown";
}
}
void WriteLog(const std::wstring& message, HRESULT hr) {
    std::lock_guard lock(g_logMutex);
    auto settings = SettingsPath(); if (settings.empty()) return;
    auto dir = settings.parent_path() / L"logs";
    std::error_code ec; std::filesystem::create_directories(dir, ec); if (ec) return;
    auto file = dir / L"epipen.log";
    constexpr uintmax_t maxBytes = 1024 * 1024;
    if (std::filesystem::exists(file, ec) && std::filesystem::file_size(file, ec) >= maxBytes) {
        std::filesystem::remove(dir / L"epipen.2.log", ec);
        std::filesystem::rename(dir / L"epipen.1.log", dir / L"epipen.2.log", ec);
        std::filesystem::rename(file, dir / L"epipen.1.log", ec);
    }
    std::wofstream out(file, std::ios::app);
    if (!out) return;
    SYSTEMTIME now{}; GetLocalTime(&now);
    out << now.wYear << L'-' << now.wMonth << L'-' << now.wDay << L' ' << now.wHour << L':' << now.wMinute << L':' << now.wSecond
        << L" | " << message;
    if (FAILED(hr)) out << L" | HRESULT=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec;
    out << L'\n';
}
void LogState(AppState state, const std::wstring& detail) { WriteLog(L"state=" + StateName(state) + (detail.empty() ? L"" : L" | " + detail)); }
