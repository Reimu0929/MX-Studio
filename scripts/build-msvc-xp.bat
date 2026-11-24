@echo off
setlocal
rem Build the Win32 app and CLI with the MSVC v141_xp toolset.
rem Requirements: Visual Studio 2017 with the "C++ Windows XP Support for
rem VS 2017 (v141_xp)" component, and CMake 3.16 or newer.
rem
rem v141_xp belongs to the VS2017 toolset.  VS2019/2022 can also use it only
rem when the VS2017 XP toolset is installed; in that case set the generator:
rem   set MSX_VS_GENERATOR=Visual Studio 16 2019
rem   set MSX_VS_GENERATOR=Visual Studio 17 2022
rem
rem The project intentionally uses a static CRT and the 5.01 subsystem.

if "%MSX_VS_GENERATOR%"=="" set MSX_VS_GENERATOR=Visual Studio 15 2017
echo Generator: %MSX_VS_GENERATOR%

set ROOT=%~dp0..
set BUILD=%ROOT%\build-win-xp

if defined VCTargetsPath (
  echo NOTE: VCTargetsPath was set; clearing it for this build.
  set "VCTargetsPath="
)

if /i "%MSX_VS_GENERATOR%"=="Visual Studio 15 2017" (
  if /i not "%MSX_SKIP_PRECHECK%"=="1" (
    call "%~dp0check-vs2017.bat"
    if errorlevel 1 goto :fail
  )
) else (
  echo Preflight: skipped for a non-VS2017 generator.
)

rem Let CMake select the Windows SDK automatically.
rem v141_xp uses the Windows 7.1A SDK for XP targeting.
rem Allow an explicit SDK override when necessary.

set "PLATFORM=Win32"
set "SDK_ARG="

if defined MSX_WINDOWS_SDK (
    set "PLATFORM=Win32,version=%MSX_WINDOWS_SDK%"
    echo SDK: %MSX_WINDOWS_SDK%
)

echo Platform: %PLATFORM%
rem Remove a cache left behind by a failed compiler detection, otherwise CMake
rem may not retry with the newly selected SDK.
if exist "%BUILD%\CMakeCache.txt" (
  findstr /b /c:"CMAKE_CXX_COMPILER:FILEPATH=CMAKE_CXX_COMPILER-NOTFOUND" "%BUILD%\CMakeCache.txt" >nul 2>nul
  if not errorlevel 1 (
    echo Removing build directory with a failed compiler cache...
    rmdir /s /q "%BUILD%"
  )
)

rem Reusing a build directory with a different generator is rejected by CMake.
rem Remove the stale cache automatically when the generator changed.
if not exist "%BUILD%\CMakeCache.txt" goto :configure
set "CACHED_GEN="
for /f "tokens=1,* delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" "%BUILD%\CMakeCache.txt"') do set "CACHED_GEN=%%B"
if /i "%CACHED_GEN%"=="%MSX_VS_GENERATOR%" goto :configure
echo.
echo Existing CMake cache uses : "%CACHED_GEN%"
echo Requested generator     : "%MSX_VS_GENERATOR%"
echo Removing stale build directory: "%BUILD%"
rmdir /s /q "%BUILD%"
if exist "%BUILD%" (
  echo Could not remove the build directory. Delete it manually and re-run.
  goto :fail
)

:configure
cmake -S "%ROOT%" -B "%BUILD%" ^
  -G "%MSX_VS_GENERATOR%" -A "%PLATFORM%" -T v141_xp %SDK_ARG% ^
  -DMSX_BUILD_WIN32=ON -DMSX_BUILD_TOOLS=ON -DMSX_BUILD_TESTS=OFF
if errorlevel 1 goto :fail

cmake --build "%BUILD%" --config Release
if errorlevel 1 goto :fail

echo.
echo Built:
echo   %BUILD%\app\win32\Release\msxstudio.exe
echo   %BUILD%\tools\msxtool\Release\msxtool.exe
exit /b 0

:fail
echo Build failed.
echo See: %BUILD%\CMakeFiles\CMakeConfigureLog.yaml
exit /b 1
