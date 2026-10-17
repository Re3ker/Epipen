#include "common.h"
#include "monitor_catalog.h"
#include "settings_store.h"
#include "capture_session.h"
#include "diagnostics.h"

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "wtsapi32.lib")

namespace {
constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_RENDER_READY = WM_APP + 2;
constexpr UINT WM_RENDER_ENDED = WM_APP + 3;
constexpr UINT ID_TRAY = 1;
constexpr int ID_MONITOR = 100;
constexpr int ID_STATUS = 101;
constexpr int ID_START = 102;
constexpr int ID_QUIET = 103;
constexpr int ID_RESUME = 104;
constexpr int ID_STOP = 105;
constexpr int ID_BLACK = 106;
constexpr int ID_WHITE = 107;
constexpr int ID_TEMPORAL = 108;
constexpr int ID_RATE = 109;
constexpr int ID_RATE_VALUE = 110;
constexpr int ID_NOTE = 111;
constexpr int ID_BLACK_VALUE = 112;
constexpr int ID_WHITE_VALUE = 113;
constexpr UINT HOTKEY_QUIET = 1;
constexpr UINT HOTKEY_SETTINGS = 2;
constexpr UINT HOTKEY_STOP = 3;

enum MenuAction { MenuSettings = 1, MenuQuiet, MenuResume, MenuStop, MenuExit };

class Application {
public:
    HINSTANCE instance{};
    HWND settingsWindow{};
    HWND outputWindow{};
    HWND curtainWindow{};
    std::wstring overlayMonitor;
    HWND monitorCombo{};
    HWND statusLabel{};
    HWND blackSlider{};
    HWND whiteSlider{};
    HWND blackValue{};
    HWND whiteValue{};
    HWND temporalCheck{};
    HWND rateSlider{};
    HWND rateValue{};
    HWND noteLabel{};
    std::vector<MonitorInfo> monitors;
    AppSettings settings{};
    AppState state = AppState::Stopped;
    std::wstring errorMessage;
    bool hotkeysReady = false;
    bool trayInstalled = false;
    bool closingExplained = false;
    bool exitAfterStop = false;
    AppState stopDestination = AppState::Stopped;
    HANDLE stopEvent{};
    HANDLE settingsChangedEvent{};
    std::mutex filterSettingsMutex;
    std::thread renderThread;
    NOTIFYICONDATAW tray{};

    bool Initialize(HINSTANCE hInstance);
    void Shutdown();
    void RefreshMonitors();
    const MonitorInfo* SelectedMonitor();
    bool EnsureOverlays(const MonitorInfo& monitor);
    void Start();
    void StopTo(AppState destination);
    void SetState(AppState next, const std::wstring& detail = L"");
    void SetCover(bool visible);
    void ShowSettings();
    void InstallTray();
    void RemoveTray();
    void HandleTray(LPARAM event);
    void UpdateSettingsFromControls(bool useRateText = false);
    void SaveCurrentSettings();
    FilterSettings SnapshotFilterSettings();
};

Application g_app;

void SetFont(HWND control) { SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE); }
HWND MakeControl(const wchar_t* klass, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND hwnd = CreateWindowExW(0, klass, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        g_app.settingsWindow, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g_app.instance, nullptr);
    if (hwnd) SetFont(hwnd);
    return hwnd;
}
void Label(const wchar_t* text, int x, int y, int w, int h) { MakeControl(L"STATIC", text, 0, x, y, w, h, 0); }

LRESULT CALLBACK CurtainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_ERASEBKGND) {
        RECT r{}; GetClientRect(hwnd, &r); HBRUSH brush = CreateSolidBrush(RGB(32, 32, 32));
        FillRect(reinterpret_cast<HDC>(wParam), &r, brush); DeleteObject(brush); return 1;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); RECT r{}; GetClientRect(hwnd, &r);
        HBRUSH brush = CreateSolidBrush(RGB(32, 32, 32)); FillRect(dc, &r, brush); DeleteObject(brush); EndPaint(hwnd, &ps); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_NCCREATE:
        g_app.settingsWindow = hwnd;
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_CREATE: {
        Label(L"Monitor", 24, 22, 120, 22);
        g_app.monitorCombo = MakeControl(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, 150, 18, 510, 220, ID_MONITOR);
        g_app.statusLabel = MakeControl(L"STATIC", L"Status: Filter beendet", 0, 24, 64, 660, 24, ID_STATUS);
        MakeControl(L"BUTTON", L"Filter starten", BS_PUSHBUTTON, 24, 104, 156, 32, ID_START);
        MakeControl(L"BUTTON", L"Ruhebild anzeigen", BS_PUSHBUTTON, 190, 104, 150, 32, ID_QUIET);
        MakeControl(L"BUTTON", L"Fortsetzen", BS_PUSHBUTTON, 350, 104, 130, 32, ID_RESUME);
        MakeControl(L"BUTTON", L"Filter beenden – Desktop freigeben", BS_PUSHBUTTON, 490, 104, 205, 32, ID_STOP);
        Label(L"Schwarzuntergrenze (sRGB-Codewert)", 24, 162, 330, 22);
        g_app.blackSlider = MakeControl(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS, 24, 184, 500, 36, ID_BLACK);
        g_app.blackValue = MakeControl(L"STATIC", L"16", 0, 540, 190, 70, 24, ID_BLACK_VALUE);
        SendMessageW(g_app.blackSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 254));
        Label(L"Weißobergrenze (sRGB-Codewert)", 24, 234, 330, 22);
        g_app.whiteSlider = MakeControl(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS, 24, 256, 500, 36, ID_WHITE);
        g_app.whiteValue = MakeControl(L"STATIC", L"220", 0, 540, 262, 70, 24, ID_WHITE_VALUE);
        SendMessageW(g_app.whiteSlider, TBM_SETRANGE, TRUE, MAKELONG(1, 255));
        g_app.temporalCheck = MakeControl(L"BUTTON", L"Zeitliche Begrenzung aktiv", BS_AUTOCHECKBOX, 24, 310, 420, 26, ID_TEMPORAL);
        Label(L"Änderungsrate, linearer Farbwert pro Sekunde (0,1–10,0)", 24, 352, 540, 22);
        g_app.rateSlider = MakeControl(TRACKBAR_CLASSW, L"", TBS_AUTOTICKS, 24, 374, 500, 36, ID_RATE);
        SendMessageW(g_app.rateSlider, TBM_SETRANGE, TRUE, MAKELONG(1, 100));
        g_app.rateValue = MakeControl(L"EDIT", L"1,0", WS_BORDER | ES_AUTOHSCROLL, 540, 377, 100, 26, ID_RATE_VALUE);
        g_app.noteLabel = MakeControl(L"STATIC", L"Die GPU begrenzt Helligkeit und zeitliche Änderungen direkt vor der Vollbildausgabe.", 0, 24, 434, 680, 42, ID_NOTE);
        Label(L"GPU-Filter: Helligkeitsbereich und zeitliche Aenderungsrate", 24, 494, 680, 24);
        Label(L"Epipen ist eine experimentelle Reduktion visueller Reize. Keine Diagnose oder Garantie zur Anfallsverhinderung.", 24, 532, 680, 44);
        g_app.RefreshMonitors();
        SendMessageW(g_app.blackSlider, TBM_SETPOS, TRUE, g_app.settings.filter.blackFloorSrgb);
        SendMessageW(g_app.whiteSlider, TBM_SETPOS, TRUE, g_app.settings.filter.whiteCeilingSrgb);
        SendMessageW(g_app.temporalCheck, BM_SETCHECK, g_app.settings.filter.temporalEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(g_app.rateSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(g_app.settings.filter.maxChannelChangePerSecond * 10.0f));
        g_app.UpdateSettingsFromControls();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_START: g_app.UpdateSettingsFromControls(); g_app.Start(); return 0;
        case ID_QUIET: g_app.StopTo(AppState::Quiet); return 0;
        case ID_RESUME: if (g_app.state == AppState::Quiet || g_app.state == AppState::Faulted) g_app.Start(); return 0;
        case ID_STOP: g_app.StopTo(AppState::Stopped); return 0;
        case ID_MONITOR:
            if (HIWORD(wParam) == CBN_SELCHANGE) { const auto* m = g_app.SelectedMonitor(); if (m) g_app.settings.monitor.deviceName = m->deviceName; g_app.SaveCurrentSettings(); }
            return 0;
        case ID_RATE_VALUE:
            if (HIWORD(wParam) == EN_KILLFOCUS) { g_app.UpdateSettingsFromControls(true); g_app.SaveCurrentSettings(); }
            return 0;
        default: break;
        }
        if (HIWORD(wParam) == BN_CLICKED && (LOWORD(wParam) == ID_TEMPORAL)) { g_app.UpdateSettingsFromControls(); g_app.SaveCurrentSettings(); }
        return 0;
    case WM_HSCROLL:
        g_app.UpdateSettingsFromControls(); g_app.SaveCurrentSettings(); return 0;
    case WM_HOTKEY:
        if (wParam == HOTKEY_QUIET) g_app.StopTo(AppState::Quiet);
        else if (wParam == HOTKEY_SETTINGS) g_app.ShowSettings();
        else if (wParam == HOTKEY_STOP) g_app.StopTo(AppState::Stopped);
        return 0;
    case WM_DISPLAYCHANGE:
        if (g_app.state == AppState::Running || g_app.state == AppState::Starting) {
            g_app.errorMessage = L"Die Monitorauflösung oder Displaykonfiguration hat sich geändert.";
            g_app.StopTo(AppState::Faulted);
        }
        g_app.RefreshMonitors(); return 0;
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK || wParam == WTS_CONSOLE_DISCONNECT) g_app.StopTo(AppState::Quiet);
        return 0;
    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND) g_app.StopTo(AppState::Quiet);
        return TRUE;
    case WM_TRAY: g_app.HandleTray(lParam); return 0;
    case WM_RENDER_READY:
        if (g_app.state == AppState::Starting) {
            ShowWindow(g_app.outputWindow, SW_SHOWNOACTIVATE);
            SetWindowPos(g_app.outputWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ShowWindow(g_app.curtainWindow, SW_HIDE);
            g_app.SetState(AppState::Running, L"GPU-Filter aktiv");
        }
        return 0;
    case WM_RENDER_ENDED: {
        auto* result = reinterpret_cast<CaptureResult*>(lParam);
        if (g_app.renderThread.joinable()) g_app.renderThread.join();
        if (g_app.stopEvent) { CloseHandle(g_app.stopEvent); g_app.stopEvent = nullptr; }
        if (g_app.settingsChangedEvent) { CloseHandle(g_app.settingsChangedEvent); g_app.settingsChangedEvent = nullptr; }
        if (result) {
            const bool stopped = result->stopped;
            const std::wstring reason = result->error;
            const HRESULT hr = result->hr;
            WriteLog(L"Capture-Ende: Frames=" + std::to_wstring(result->capturedFrames) + L", präsentiert=" + std::to_wstring(result->presentedFrames) + L", verworfen=" + std::to_wstring(result->supersededFrames), hr);
            delete result;
            if (!stopped && !reason.empty()) {
                g_app.errorMessage = reason; g_app.SetCover(true); g_app.SetState(AppState::Faulted, reason); g_app.ShowSettings();
                if (FAILED(hr)) WriteLog(reason, hr);
            } else if (g_app.stopDestination == AppState::Quiet) {
                g_app.SetCover(true); g_app.SetState(AppState::Quiet);
            } else if (g_app.stopDestination == AppState::Faulted) {
                g_app.SetCover(true);
                if (g_app.errorMessage.empty()) g_app.errorMessage = L"Die Anzeige- oder Sitzungsumgebung hat sich geändert. Bitte prüfen und ausdrücklich fortsetzen.";
                g_app.SetState(AppState::Faulted, g_app.errorMessage); g_app.ShowSettings();
            } else {
                ShowWindow(g_app.outputWindow, SW_HIDE); ShowWindow(g_app.curtainWindow, SW_HIDE);
                g_app.SetState(AppState::Stopped);
            }
            if (g_app.exitAfterStop) { g_app.exitAfterStop = false; PostQuitMessage(0); }
        }
        return 0;
    }
    case WM_CLOSE:
        if (!g_app.closingExplained) {
            MessageBoxW(hwnd, L"Das Fenster wird in den Infobereich minimiert. Zum Beenden wählen Sie im Tray-Menü „Anwendung beenden“.", L"Epipen", MB_OK | MB_ICONINFORMATION);
            g_app.closingExplained = true;
        }
        ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_DESTROY:
        WTSUnRegisterSessionNotification(hwnd);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool Application::Initialize(HINSTANCE hInstance) {
    instance = hInstance;
    std::wstring warning; LoadSettings(settings, warning);
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{ sizeof(cls) }; cls.hInstance = instance; cls.lpfnWndProc = SettingsProc;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1); cls.lpszClassName = L"EpipenSettingsWindow";
    if (!RegisterClassExW(&cls)) return false;
    WNDCLASSEXW output{ sizeof(output) }; output.hInstance = instance; output.lpfnWndProc = DefWindowProcW;
    output.hCursor = LoadCursorW(nullptr, IDC_ARROW); output.lpszClassName = L"EpipenOutputWindow";
    RegisterClassExW(&output);
    WNDCLASSEXW curtain{ sizeof(curtain) }; curtain.hInstance = instance; curtain.lpfnWndProc = CurtainProc;
    curtain.hCursor = LoadCursorW(nullptr, IDC_ARROW); curtain.lpszClassName = L"EpipenSafetyCurtain";
    RegisterClassExW(&curtain);
    settingsWindow = CreateWindowExW(0, L"EpipenSettingsWindow", L"Epipen – experimentelle Reizreduktion",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 760, 650, nullptr, nullptr, instance, nullptr);
    if (!settingsWindow) return false;
    const bool settingsKey = RegisterHotKey(settingsWindow, HOTKEY_SETTINGS, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F10) != FALSE;
    const bool stopKey = RegisterHotKey(settingsWindow, HOTKEY_STOP, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12) != FALSE;
    const bool quietKey = RegisterHotKey(settingsWindow, HOTKEY_QUIET, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F9) != FALSE;
    hotkeysReady = settingsKey && stopKey && quietKey;
    if (!hotkeysReady) {
        UnregisterHotKey(settingsWindow, HOTKEY_QUIET); UnregisterHotKey(settingsWindow, HOTKEY_SETTINGS); UnregisterHotKey(settingsWindow, HOTKEY_STOP);
        errorMessage = L"Mindestens eine globale Tastenkombination ist belegt. Freigabe: Ctrl+Alt+F9, Einstellungen: Ctrl+Alt+F10, Stoppen: Ctrl+Alt+F12.";
    }
    WTSRegisterSessionNotification(settingsWindow, NOTIFY_FOR_THIS_SESSION);
    InstallTray();
    if (!warning.empty()) { errorMessage = warning; SetWindowTextW(statusLabel, warning.c_str()); }
    ShowWindow(settingsWindow, SW_SHOW); UpdateWindow(settingsWindow);
    return true;
}

void Application::RefreshMonitors() {
    monitors = EnumerateMonitors();
    if (!monitorCombo) return;
    SendMessageW(monitorCombo, CB_RESETCONTENT, 0, 0);
    int selected = -1;
    for (size_t i = 0; i < monitors.size(); ++i) {
        auto& m = monitors[i];
        std::wstring row = m.deviceName + L" — " + m.description + (m.primary ? L" (primär)" : L"") + L" — ";
        row += !m.hdrStatusKnown ? L"HDR-Status nicht prüfbar" : (m.advancedColorEnabled ? L"HDR aktiv" : L"SDR unterstützt");
        const int index = static_cast<int>(SendMessageW(monitorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(row.c_str())));
        SendMessageW(monitorCombo, CB_SETITEMDATA, index, i);
        if (m.deviceName == settings.monitor.deviceName) selected = index;
        if (m.primary && selected < 0) selected = index;
    }
    if (selected < 0 && !monitors.empty()) selected = 0;
    if (selected >= 0) {
        SendMessageW(monitorCombo, CB_SETCURSEL, selected, 0);
        const auto* monitor = SelectedMonitor(); if (monitor) settings.monitor.deviceName = monitor->deviceName;
    }
}

const MonitorInfo* Application::SelectedMonitor() {
    if (!monitorCombo || monitors.empty()) return nullptr;
    const LRESULT index = SendMessageW(monitorCombo, CB_GETCURSEL, 0, 0);
    if (index < 0) return nullptr;
    const LRESULT item = SendMessageW(monitorCombo, CB_GETITEMDATA, index, 0);
    if (item < 0 || static_cast<size_t>(item) >= monitors.size()) return nullptr;
    return &monitors[static_cast<size_t>(item)];
}

bool Application::EnsureOverlays(const MonitorInfo& monitor) {
    if (outputWindow && curtainWindow && overlayMonitor == monitor.deviceName) return true;
    const RECT r = monitor.bounds;
    HWND newOutput = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"EpipenOutputWindow", L"Epipen Ausgabe", WS_POPUP, r.left, r.top, monitor.width(), monitor.height(), nullptr, nullptr, instance, nullptr);
    HWND newCurtain = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"EpipenSafetyCurtain", L"Epipen Ruhebild", WS_POPUP, r.left, r.top, monitor.width(), monitor.height(), nullptr, nullptr, instance, nullptr);
    if (!newOutput || !newCurtain) {
        if (newOutput) DestroyWindow(newOutput); if (newCurtain) DestroyWindow(newCurtain);
        errorMessage = L"Die vollflächigen Overlay-Fenster konnten nicht erstellt werden."; return false;
    }
    if (!SetLayeredWindowAttributes(newOutput, 0, 255, LWA_ALPHA)) {
        DestroyWindow(newOutput); DestroyWindow(newCurtain);
        errorMessage = L"Die volle Deckkraft des Ausgabefensters konnte nicht gesetzt werden."; return false;
    }
    if (!SetWindowDisplayAffinity(newOutput, WDA_EXCLUDEFROMCAPTURE) || !SetWindowDisplayAffinity(newCurtain, WDA_EXCLUDEFROMCAPTURE) ||
        !SetWindowDisplayAffinity(settingsWindow, WDA_EXCLUDEFROMCAPTURE)) {
        DestroyWindow(newOutput); DestroyWindow(newCurtain);
        errorMessage = L"Windows konnte nicht bestätigen, dass Epipen-Fenster von der Monitoraufnahme ausgeschlossen werden.";
        return false;
    }
    SetWindowPos(newOutput, HWND_TOPMOST, r.left, r.top, monitor.width(), monitor.height(), SWP_NOACTIVATE | SWP_HIDEWINDOW);
    SetWindowPos(newCurtain, HWND_TOPMOST, r.left, r.top, monitor.width(), monitor.height(), SWP_NOACTIVATE | SWP_HIDEWINDOW);
    HWND oldOutput = outputWindow, oldCurtain = curtainWindow;
    const bool coverWasVisible = oldCurtain && IsWindowVisible(oldCurtain);
    outputWindow = newOutput; curtainWindow = newCurtain; overlayMonitor = monitor.deviceName;
    if (coverWasVisible) {
        ShowWindow(curtainWindow, SW_SHOWNOACTIVATE);
        SetWindowPos(curtainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (oldOutput) DestroyWindow(oldOutput);
    if (oldCurtain) DestroyWindow(oldCurtain);
    return true;
}

void Application::Start() {
    if (state == AppState::Running || state == AppState::Starting || renderThread.joinable()) return;
    const auto* monitor = SelectedMonitor();
    if (!monitor) { errorMessage = L"Es ist kein Monitor verfügbar."; SetState(AppState::Faulted, errorMessage); return; }
    if (!monitor->hdrStatusKnown) { errorMessage = L"Der HDR-Status dieses Monitors konnte nicht zuverlässig bestimmt werden. Der Start wurde verweigert."; SetState(AppState::Faulted, errorMessage); ShowSettings(); return; }
    if (monitor->advancedColorEnabled) { errorMessage = L"Auf diesem Monitor ist HDR/Advanced Color aktiviert. Epipen unterstützt derzeit nur SDR."; SetState(AppState::Faulted, errorMessage); ShowSettings(); return; }
    if (!hotkeysReady) { SetState(AppState::Faulted, errorMessage); ShowSettings(); return; }
    SaveCurrentSettings();
    if (!EnsureOverlays(*monitor)) { SetState(AppState::Faulted, errorMessage); ShowSettings(); return; }
    ShowWindow(curtainWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(curtainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(outputWindow, SW_HIDE);
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent) { errorMessage = L"Das Stoppsignal des Render-Threads konnte nicht erstellt werden."; SetState(AppState::Faulted, errorMessage); return; }
    settingsChangedEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!settingsChangedEvent) {
        CloseHandle(stopEvent); stopEvent = nullptr;
        errorMessage = L"Das Aktualisierungssignal der Filterregler konnte nicht erstellt werden.";
        SetState(AppState::Faulted, errorMessage); return;
    }
    stopDestination = AppState::Stopped;
    SetState(AppState::Starting, L"Sicherheitsfläche sichtbar; Aufnahme und DirectComposition werden eingerichtet.");
    MonitorInfo copy = *monitor;
    const FilterSettings initialSettings = SnapshotFilterSettings();
    renderThread = std::thread([this, copy = std::move(copy), initialSettings] {
        auto result = RunCaptureSession(outputWindow, copy, stopEvent, settingsChangedEvent,
            initialSettings, [this] { return SnapshotFilterSettings(); },
            [this] { PostMessageW(settingsWindow, WM_RENDER_READY, 0, 0); });
        PostMessageW(settingsWindow, WM_RENDER_ENDED, 0, reinterpret_cast<LPARAM>(new CaptureResult(std::move(result))));
    });
}

void Application::StopTo(AppState destination) {
    if (destination == AppState::Quiet || destination == AppState::Faulted) SetCover(true);
    stopDestination = destination;
    if (!renderThread.joinable()) {
        if (destination == AppState::Stopped) { if (outputWindow) ShowWindow(outputWindow, SW_HIDE); if (curtainWindow) ShowWindow(curtainWindow, SW_HIDE); }
        else SetCover(true);
        SetState(destination); return;
    }
    SetState(AppState::Stopping, L"Die Aufnahme wird beendet.");
    if (stopEvent) SetEvent(stopEvent);
}

void Application::SetCover(bool visible) {
    if (!curtainWindow) return;
    if (visible) {
        ShowWindow(curtainWindow, SW_SHOWNOACTIVATE);
        SetWindowPos(curtainWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(outputWindow, SW_HIDE);
    } else ShowWindow(curtainWindow, SW_HIDE);
}

void Application::SetState(AppState next, const std::wstring& detail) {
    state = next; LogState(next, detail);
    if (monitorCombo) {
        const bool monitorLocked = next == AppState::Starting || next == AppState::Running || next == AppState::Stopping;
        EnableWindow(monitorCombo, monitorLocked ? FALSE : TRUE);
    }
    if (!statusLabel) return;
    std::wstring label = L"Status: ";
    switch (next) {
    case AppState::Stopped: label += L"Filter beendet"; break;
    case AppState::Starting: label += L"Startet"; break;
    case AppState::Running: label += L"Filter aktiv"; break;
    case AppState::Quiet: label += L"Ruhebild aktiv"; break;
    case AppState::Faulted: label += L"Fehler – " + errorMessage; break;
    case AppState::Stopping: label += L"Stoppt"; break;
    }
    SetWindowTextW(statusLabel, label.c_str());
}

void Application::ShowSettings() {
    if (!settingsWindow) return;
    ShowWindow(settingsWindow, SW_SHOW); SetWindowPos(settingsWindow, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(settingsWindow);
}

void Application::InstallTray() {
    tray = {}; tray.cbSize = sizeof(tray); tray.hWnd = settingsWindow; tray.uID = ID_TRAY;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; tray.uCallbackMessage = WM_TRAY;
    tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION); wcscpy_s(tray.szTip, L"Epipen – experimenteller Desktopfilter");
    trayInstalled = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    if (trayInstalled) { tray.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &tray); }
}
void Application::RemoveTray() { if (trayInstalled) { Shell_NotifyIconW(NIM_DELETE, &tray); trayInstalled = false; } }
void Application::HandleTray(LPARAM event) {
    POINT p{}; GetCursorPos(&p);
    if (LOWORD(event) == WM_LBUTTONDBLCLK) { ShowSettings(); return; }
    if (LOWORD(event) != WM_RBUTTONUP && LOWORD(event) != WM_CONTEXTMENU) return;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, MenuSettings, L"Einstellungen öffnen");
    AppendMenuW(menu, MF_STRING, MenuQuiet, L"Ruhebild anzeigen");
    AppendMenuW(menu, MF_STRING, MenuResume, L"Fortsetzen");
    AppendMenuW(menu, MF_STRING, MenuStop, L"Filter stoppen");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, MenuExit, L"Anwendung beenden");
    SetForegroundWindow(settingsWindow);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, p.x, p.y, 0, settingsWindow, nullptr);
    DestroyMenu(menu);
    switch (cmd) {
    case MenuSettings: ShowSettings(); break;
    case MenuQuiet: StopTo(AppState::Quiet); break;
    case MenuResume: if (state == AppState::Quiet || state == AppState::Faulted) Start(); break;
    case MenuStop: StopTo(AppState::Stopped); break;
    case MenuExit:
        if (renderThread.joinable()) { exitAfterStop = true; StopTo(AppState::Stopped); }
        else PostQuitMessage(0);
        break;
    }
}

void Application::UpdateSettingsFromControls(bool useRateText) {
    if (!blackSlider) return;
    const FilterSettings previous = SnapshotFilterSettings();
    FilterSettings next = previous;
    next.blackFloorSrgb = static_cast<int>(SendMessageW(blackSlider, TBM_GETPOS, 0, 0));
    next.whiteCeilingSrgb = static_cast<int>(SendMessageW(whiteSlider, TBM_GETPOS, 0, 0));
    if (next.blackFloorSrgb >= next.whiteCeilingSrgb) {
        if (next.blackFloorSrgb >= previous.whiteCeilingSrgb) next.blackFloorSrgb = std::max(0, previous.whiteCeilingSrgb - 1);
        else next.whiteCeilingSrgb = std::min(255, previous.blackFloorSrgb + 1);
        SendMessageW(blackSlider, TBM_SETPOS, TRUE, next.blackFloorSrgb);
        SendMessageW(whiteSlider, TBM_SETPOS, TRUE, next.whiteCeilingSrgb);
    }
    next.temporalEnabled = SendMessageW(temporalCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (useRateText) {
        wchar_t raw[32]{}; GetWindowTextW(rateValue, raw, ARRAYSIZE(raw));
        for (wchar_t* c = raw; *c; ++c) if (*c == L',') *c = L'.';
        wchar_t* end = nullptr; const float typed = wcstof(raw, &end);
        if (end != raw && *end == L'\0' && std::isfinite(typed) && typed >= 0.1f && typed <= 10.0f) {
            next.maxChannelChangePerSecond = typed;
            SendMessageW(rateSlider, TBM_SETPOS, TRUE, static_cast<LPARAM>(std::lround(typed * 10.0f)));
        }
    } else {
        const int rate = static_cast<int>(SendMessageW(rateSlider, TBM_GETPOS, 0, 0));
        next.maxChannelChangePerSecond = std::clamp(rate / 10.0f, 0.1f, 10.0f);
    }
    bool changed = false;
    if (ValidateSettings(next)) {
        changed = next.blackFloorSrgb != previous.blackFloorSrgb ||
            next.whiteCeilingSrgb != previous.whiteCeilingSrgb ||
            next.temporalEnabled != previous.temporalEnabled ||
            next.maxChannelChangePerSecond != previous.maxChannelChangePerSecond;
        if (changed) {
            std::lock_guard<std::mutex> lock(filterSettingsMutex);
            settings.filter = next;
        }
    }
    if (changed && settingsChangedEvent) SetEvent(settingsChangedEvent);
    const FilterSettings current = SnapshotFilterSettings();
    wchar_t blackText[8]{};
    wchar_t whiteText[8]{};
    swprintf_s(blackText, L"%d", current.blackFloorSrgb);
    swprintf_s(whiteText, L"%d", current.whiteCeilingSrgb);
    SetWindowTextW(blackValue, blackText);
    SetWindowTextW(whiteValue, whiteText);
    wchar_t rateText[16]{}; swprintf_s(rateText, L"%.1f", current.maxChannelChangePerSecond);
    SetWindowTextW(rateValue, rateText);
}

FilterSettings Application::SnapshotFilterSettings() {
    std::lock_guard<std::mutex> lock(filterSettingsMutex);
    return settings.filter;
}
void Application::SaveCurrentSettings() { std::wstring error; if (!SaveSettings(settings, error)) { errorMessage = error; WriteLog(error); } }

void Application::Shutdown() {
    if (renderThread.joinable()) { if (stopEvent) SetEvent(stopEvent); renderThread.join(); }
    if (stopEvent) { CloseHandle(stopEvent); stopEvent = nullptr; }
    if (settingsChangedEvent) { CloseHandle(settingsChangedEvent); settingsChangedEvent = nullptr; }
    RemoveTray();
    if (settingsWindow) { UnregisterHotKey(settingsWindow, HOTKEY_QUIET); UnregisterHotKey(settingsWindow, HOTKEY_SETTINGS); UnregisterHotKey(settingsWindow, HOTKEY_STOP); }
    if (outputWindow) DestroyWindow(outputWindow);
    if (curtainWindow) DestroyWindow(curtainWindow);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        MessageBoxW(nullptr, L"Per-Monitor-DPI-Awareness V2 konnte vor der Fenstererstellung nicht aktiviert werden.", L"Epipen – Startfehler", MB_OK | MB_ICONERROR);
        return 2;
    }
    if (!g_app.Initialize(instance)) return 1;
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    g_app.Shutdown();
    return static_cast<int>(message.wParam);
}
