@echo off
setlocal EnableExtensions
rem Check the PE subsystem version and imported DLLs/functions of the built
rem binaries.  Run this on the Windows build host, not on the XP target.

set "EXE=%~1"
if "%EXE%"=="" set "EXE=%~dp0..\build-win-xp\app\win32\Release\msxstudio.exe"
if not exist "%EXE%" (
  echo [NG] File not found: %EXE%
  exit /b 1
)

echo === XP binary check ===
echo File: %EXE%

set "VSPATH="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%V in (`call "%VSWHERE%" -latest -version "[15.0,18.0)" -products * -property installationPath`) do set "VSPATH=%%V"
)

set "DUMPBIN="
if defined VSPATH (
  for /d %%D in ("%VSPATH%\VC\Tools\MSVC\14.*") do if exist "%%D\bin\Hostx86\x86\dumpbin.exe" set "DUMPBIN=%%D\bin\Hostx86\x86\dumpbin.exe"
)
if not defined DUMPBIN (
  for /f "delims=" %%D in ('where dumpbin.exe 2^>nul') do set "DUMPBIN=%%D"
)
if not defined DUMPBIN (
  echo [NG] dumpbin.exe was not found. Run this from a Visual Studio developer prompt.
  exit /b 1
)

set "MSX_EXE=%EXE%"
echo.
echo --- Subsystem (PE optional header) ---
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes($env:MSX_EXE); $p=[BitConverter]::ToInt32($b,0x3C); $maj=[BitConverter]::ToUInt16($b,$p+0x48); $min=[BitConverter]::ToUInt16($b,$p+0x4A); Write-Host ('Subsystem version: {0}.{1}' -f $maj,$min)" 2>nul
if errorlevel 1 echo [WARN] PowerShell could not read the PE header.

echo.
echo --- Dependent DLLs ---
"%DUMPBIN%" /dependents "%EXE%" > "%TEMP%\msx_xp_deps.txt"
type "%TEMP%\msx_xp_deps.txt"

echo.
echo --- Suspicious runtime / Vista+ imports ---
"%DUMPBIN%" /imports "%EXE%" > "%TEMP%\msx_xp_imports.txt"
findstr /i /c:"ucrtbase.dll" /c:"api-ms-win-crt" /c:"vcruntime140" /c:"msvcp140" /c:"CompareStringEx" /c:"CompareStringOrdinal" /c:"InitializeCriticalSectionEx" /c:"InitializeSRWLock" /c:"AcquireSRWLock" /c:"SleepConditionVariable" /c:"CreateEventEx" /c:"GetTickCount64" /c:"GetSystemTimePreciseAsFileTime" /c:"GetFileInformationByHandleEx" /c:"CancelIoEx" /c:"FlsAlloc" /c:"GetFinalPathNameByHandle" /c:"GetLocaleInfoEx" /c:"LCMapStringEx" /c:"GetUserDefaultLocaleName" /c:"EnumSystemLocalesEx" /c:"SetThreadDescription" /c:"GetDpiForWindow" /c:"SetProcessDpiAwareness" "%TEMP%\msx_xp_imports.txt"
if errorlevel 1 (
  echo none found.
  set "RESULT=0"
) else (
  echo [NG] The imports above are not available on Windows XP.
  echo      Do not treat this binary as XP-compatible.
  set "RESULT=1"
)

if exist "%TEMP%\msx_xp_deps.txt" del "%TEMP%\msx_xp_deps.txt" >nul 2>nul
if exist "%TEMP%\msx_xp_imports.txt" del "%TEMP%\msx_xp_imports.txt" >nul 2>nul
exit /b %RESULT%
