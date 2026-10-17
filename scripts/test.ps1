param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
$launchVs = Join-Path $vsPath 'Common7\Tools\Launch-VsDevShell.ps1'
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) { throw 'ctest.exe wurde nicht in der Visual-Studio-Installation gefunden.' }
$repoEscaped = $repo.Replace("'", "''")
$launchEscaped = $launchVs.Replace("'", "''")
$vsEscaped = $vsPath.Replace("'", "''")
$cmakeEscaped = $cmake.Replace("'", "''")
$ctestEscaped = $ctest.Replace("'", "''")
$inner = ". '$launchEscaped' -VsInstallationPath '$vsEscaped' -Arch amd64 -HostArch amd64; & '$ctestEscaped' --test-dir '$repoEscaped\build\$Configuration' --output-on-failure; exit `$LASTEXITCODE"
& powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $inner
exit $LASTEXITCODE
