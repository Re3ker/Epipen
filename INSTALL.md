# Build and installation guide

Epipen is built from source on Windows. There is currently no installer and no packaged release. The application runs locally and does not need network access or extra runtime frameworks.

## Requirements

Use a 64-bit Windows 11 desktop session. The selected display must run in SDR mode; Epipen refuses to start on HDR or Advanced Color displays.

Install Visual Studio 2026 Community, Professional, Enterprise, or the matching Build Tools edition with these components:

- Desktop development with C++
- MSVC x64/x86 build tools
- CMake tools for Windows
- Ninja
- Windows 11 SDK version `10.0.26100.0`, including `fxc.exe`

The build scripts locate Visual Studio through `vswhere.exe` and use Visual Studio's bundled CMake and Ninja. No global `PATH` changes are required.

At runtime, the target graphics adapter needs Direct3D Feature Level 11.0 or later. Windows Graphics Capture and DirectComposition must be available in the logged-in desktop session. Remote Desktop, protected video, exclusive fullscreen applications, the secure desktop, HDR, and multi-monitor filtering are outside the supported scope.

## Get the source

Clone the repository or copy its source directory to a writable local folder. Open PowerShell in the project root, the folder containing `CMakeLists.txt`.

If PowerShell blocks local scripts in the current session, enable them only for that session:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
```

## Build

Build a debug version:

```powershell
.\scripts\build.ps1 -Configuration Debug
```

Build the optimized version:

```powershell
.\scripts\build.ps1 -Configuration Release
```

The outputs are created here:

| Build | Application | Background probe |
|---|---|---|
| Debug | `build\Debug\epipen.exe` | `build\Debug\epipen_background.exe` |
| Release | `build\Release\epipen.exe` | `build\Release\epipen_background.exe` |

The build also compiles the HLSL shaders into generated headers. Do not copy the generated headers manually; they are regenerated during every relevant build.

## Run automated tests

Run tests after building:

```powershell
.\scripts\test.ps1 -Configuration Debug
.\scripts\test.ps1 -Configuration Release
```

The tests include CPU checks for color and temporal-filter mathematics and GPU output checks through the Windows WARP renderer. They do not verify the DirectComposition path, monitor capture exclusion, click-through behavior, or the target GPU.

## Start Epipen

Start the Release executable from PowerShell or Explorer:

```powershell
.\build\Release\epipen.exe
```

Choose an SDR monitor, set the brightness bounds and optional temporal limit, then select **Filter starten**. A dark safety curtain remains visible until the first filtered frame is presented.

Keyboard shortcuts:

| Shortcut | Action |
|---|---|
| `Ctrl+Alt+F9` | Show the input-blocking quiet screen |
| `Ctrl+Alt+F10` | Open settings |
| `Ctrl+Alt+F12` | Stop filtering and release the desktop |

To check the live path without flashing content, start `epipen_background.exe` first. It displays a slowly moving disk and a counter. Follow the manual steps in [docs/acceptance.md](docs/acceptance.md) and record the results for the target GPU and monitor.

## Local files

Settings are stored at `%LOCALAPPDATA%\Epipen\settings.ini`. Operational logs are stored in `%LOCALAPPDATA%\Epipen\logs`. Delete `settings.ini` while Epipen is stopped to restore default settings.

## Troubleshooting

| Symptom | What to check |
|---|---|
| `vswhere.exe` or C++ tools are not found | Install Visual Studio or Build Tools with Desktop development with C++. |
| `fxc.exe was not found` | Add Windows SDK `10.0.26100.0` through Visual Studio Installer. |
| CMake configuration fails | Confirm the project is in a writable local folder and rerun the supplied build script. |
| Epipen refuses to start | Select an SDR monitor; disable HDR/Advanced Color for the selected display. |
| The quiet screen stays visible | Check `%LOCALAPPDATA%\Epipen\logs\epipen.log` for the startup error, then verify that the monitor is connected and the app was rebuilt after source changes. |
| Click-through or capture exclusion fails | This is a platform and driver dependent acceptance failure. Stop filtering, record the environment in `docs/acceptance.md`, and do not treat the display as filtered. |
