param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere.exe wurde nicht gefunden. Visual Studio Build Tools müssen installiert sein.' }
$vsPath = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsPath) { throw 'Visual-Studio-C++-Buildwerkzeuge wurden nicht gefunden; es wird nichts automatisch installiert.' }
$launchVs = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
if (-not (Test-Path -LiteralPath $launchVs) -or -not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath $ninja)) { throw 'Visual Studio enthält nicht die benötigten PowerShell-, CMake-, Ninja- oder C++-Buildwerkzeuge.' }
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$launchEscaped = $launchVs.Replace("'", "''")
$vsEscaped = $vsPath.Replace("'", "''")
$cmakeEscaped = $cmake.Replace("'", "''")
$ninjaEscaped = $ninja.Replace("'", "''")
$repoEscaped = $repo.Replace("'", "''")
$inner = ". '$launchEscaped' -VsInstallationPath '$vsEscaped' -Arch amd64 -HostArch amd64; `$env:EPIPEN_NINJA = '$ninjaEscaped'; Set-Location '$repoEscaped'; if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC cl.exe ist nach dem Laden der Entwicklershell nicht verfügbar.' }; & '$cmakeEscaped' --preset '$Configuration'; if (`$LASTEXITCODE -ne 0) { exit `$LASTEXITCODE }; & '$cmakeEscaped' --build --preset '$Configuration' --parallel; exit `$LASTEXITCODE"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $inner
exit $LASTEXITCODE
