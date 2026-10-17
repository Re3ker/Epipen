#include <windows.h>
#include <algorithm>
#include <string>

namespace {
LRESULT CALLBACK ProbeProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT r{}; GetClientRect(hwnd, &r);
        HBRUSH bg = CreateSolidBrush(RGB(245, 245, 245));
        FillRect(dc, &r, bg); DeleteObject(bg);
        const auto tick = GetTickCount64() / 16;
        const int x = static_cast<int>(tick % (std::max(1L, r.right - 100)));
        HBRUSH ball = CreateSolidBrush(RGB(50, 100, 170));
        HGDIOBJ oldBrush = SelectObject(dc, ball);
        RECT circle{ x, 120, x + 70, 190 };
        Ellipse(dc, circle.left, circle.top, circle.right, circle.bottom);
        SelectObject(dc, oldBrush); DeleteObject(ball);
        SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
        std::wstring text = L"Epipen Click-through-Probe  |  Zähler: " + std::to_wstring(tick);
        TextOutW(dc, 24, 24, text.c_str(), static_cast<int>(text.size()));
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_TIMER) { InvalidateRect(hwnd, nullptr, FALSE); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    WNDCLASSW wc{}; wc.hInstance = instance; wc.lpfnWndProc = ProbeProc; wc.lpszClassName = L"EpipenProbe"; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Epipen Hintergrundprobe", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 780, 480, nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, show); SetTimer(hwnd, 1, 16, nullptr);
    MSG msg{}; while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return static_cast<int>(msg.wParam);
}
