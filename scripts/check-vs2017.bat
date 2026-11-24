
@echo off
setlocal EnableExtensions
echo === VS2017 / v141_xp preflight ===

where cmake >nul 2>nul
if errorlevel 1 goto :no_cmake

for /f "tokens=3" %%V in ('cmake --version ^| findstr /b /c:"cmake version"') do echo CMake version: %%V

if defined VCTargetsPath (
    echo [WARN] VCTargetsPath is set:
    set VCTargetsPath
    set "VCTargetsPath="
)

set "VSPATH="
set "VSVER="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" goto :scan

for /f "usebackq delims=" %%V in (`call "%VSWHERE%" -latest -version "[15.0,16.0)" -products * -property installationPath`) do set "VSPATH=%%V"

for /f "usebackq delims=" %%V in (`call "%VSWHERE%" -latest -version "[15.0,16.0)" -products * -property installationVersion`) do set "VSVER=%%V"

if not defined VSPATH goto :scan
goto :check

:scan
for /d %%D in ("%ProgramFiles(x86)%\Microsoft Visual Studio\2017\*") do (
    if exist "%%D\Common7\IDE\devenv.exe" set "VSPATH=%%D"
)

if not defined VSPATH goto :no_vs

:check
echo VS2017 path: %VSPATH%
if defined VSVER echo VS2017 version: %VSVER%

set "XPDIR=%VSPATH%\Common7\IDE\VC\VCTargets\Platforms\Win32\PlatformToolsets\v141_xp"

if not exist "%XPDIR%\Toolset.props" goto :no_xp
if not exist "%XPDIR%\Toolset.targets" goto :no_xp

echo [OK] v141_xp toolset: %XPDIR%

set "V141FOUND="

for /d %%D in ("%VSPATH%\VC\Tools\MSVC\14.1*") do (
    if exist "%%D\bin\Hostx86\x86\cl.exe" set "V141FOUND=%%D"
)

if not defined V141FOUND goto :no_v141
echo [OK] v141 compiler: %V141FOUND%

set "SDK71=%ProgramFiles(x86)%\Microsoft SDKs\Windows\v7.1A"

if not exist "%SDK71%\Lib\kernel32.lib" goto :no_sdk71
echo [OK] Windows 7.1A SDK: %SDK71%

echo Installed Windows 10 SDKs:
for /d %%D in ("%ProgramFiles(x86)%\Windows Kits\10\Include\10.*") do echo   %%~nxD

echo.
echo CMake generator: Visual Studio 15 2017
echo Architecture: Win32
echo Toolset: v141_xp
echo Generator instance:
if defined VSVER (
    echo "%VSPATH%,version=%VSVER%"
) else (
    echo "%VSPATH%"
)

echo.
echo Preflight OK.
exit /b 0

:no_cmake
echo [NG] cmake.exe was not found in PATH.
exit /b 1

:no_vs
echo [NG] Visual Studio 2017 was not found.
exit /b 1

:no_xp
echo [NG] v141_xp toolset files were not found:
echo      %XPDIR%
echo      Install C++ Windows XP Support for VS 2017.
exit /b 1

:no_v141
echo [NG] v141 compiler was not found.
echo      Install the MSVC v141 x86/x64 toolset.
exit /b 1

:no_sdk71
echo [NG] Windows 7.1A SDK was not found:
echo      %SDK71%
echo      Check the Windows XP support component.
exit /b 1
