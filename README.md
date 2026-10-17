# Epipen

Epipen is a Windows 11 x64 experiment that reduces abrupt visual changes on one SDR monitor. It is not a medical product, does not diagnose epilepsy, and does not guarantee prevention of seizures.

See [INSTALL.md](INSTALL.md) for prerequisites, building from source, running tests, starting the application, and troubleshooting.

## Current implementation

The live desktop is captured with Windows Graphics Capture and filtered on the GPU before DirectComposition presents it. The static filter maps sRGB brightness limits in linear light. The temporal filter limits the largest per-pixel RGB change to the selected rate. The controls update the render thread while it is running, and settings are written atomically to `%LOCALAPPDATA%\Epipen\settings.ini`.

The GPU shader path and its output pixels are covered by a Windows WARP test, including channel order, odd image dimensions, live setting changes, and temporal rate limiting. This does not replace a manual check on the target GPU and monitor. Click-through, capture exclusion, startup and stop behavior, and performance still need post-fix hardware acceptance; see `docs/acceptance.md`.

The start button keeps a dark safety curtain visible until the first filtered frame is presented. `Ctrl+Alt+F9` returns to the blocking quiet screen, `Ctrl+Alt+F10` opens settings, and `Ctrl+Alt+F12` releases the desktop. A failure leaves the quiet screen visible while the process remains responsive.

## Build

Requirements: Windows 11 x64, Visual Studio C++ x64 tools, Windows SDK 10.0.26100.0, CMake and Ninja included with Visual Studio, and `fxc.exe` from that SDK. The build script locates Visual Studio with `vswhere.exe`; it does not install tools.

From PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build.ps1 -Configuration Debug
.\scripts\build.ps1 -Configuration Release
.\scripts\test.ps1 -Configuration Debug
```

The build creates `build/Debug/epipen.exe`, `build/Debug/epipen_background.exe`, and the GPU regression test. The background probe draws a slowly moving disk and counter for the manual acceptance check. Do not use flashing test patterns.

## Manual acceptance

1. Start `epipen_background.exe` and keep its moving disk and counter visible.
2. Start `epipen.exe`, choose an SDR monitor, and select **Filter starten**.
3. Check that the moving content is visibly filtered, the brightness sliders change the output, and the temporal rate affects transitions.
4. Click, scroll, drag, and type into the background application. Confirm that clicks pass through, the cursor appears once, and Epipen is not captured recursively.
5. Reopen settings with `Ctrl+Alt+F10`. Press `Ctrl+Alt+F9` and confirm the quiet screen blocks mouse input; press `Ctrl+Alt+F12` to reveal the original desktop.

Record the GPU, Windows build, monitor model and mode, and each result in `docs/acceptance.md`. No graphics hooks, virtual displays, or CPU frame copies are used as a fallback.

## Filter contract

Each input channel is decoded from sRGB, mapped into `[blackFloor, whiteCeiling]` in linear light, then encoded back to sRGB for display. With temporal filtering enabled, one shared RGB scale factor limits the largest channel movement per pixel to `rate * clamp(dt, 0, 1/30 s)`. Defaults are 16, 220, temporal enabled, and 1.0 linear color value per second. These are technical starting values without medical validation. The temporal filter can trail moving edges and normal animation.

## Data and logs

Settings are stored in `%LOCALAPPDATA%\Epipen\settings.ini`. The local log directory is `%LOCALAPPDATA%\Epipen\logs`; logs contain operational events and errors, not pixels, screenshots, window titles, or keystrokes. No network service or telemetry is used.
