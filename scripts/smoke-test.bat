@echo off
setlocal
rem Run the built Windows CLI through the BASIC -> CAS -> WAV -> CAS -> BASIC
rem round trip.  This exercises the MSVC v141_xp build on the build host.
set "ROOT=%~dp0.."
set "TOOL=%ROOT%\build-win-xp\tools\msxtool\Release\msxtool.exe"
if not exist "%TOOL%" (
  echo [NG] msxtool.exe not found: %TOOL%
  echo      Build with scripts\build-msvc-xp.bat first.
  exit /b 1
)
set "WORK=%TEMP%\msx-smoke"
if exist "%WORK%" rmdir /s /q "%WORK%"
mkdir "%WORK%"
if errorlevel 1 (
  echo [NG] Cannot create %WORK%
  exit /b 1
)

echo === msxtool selftest ===
"%TOOL%" selftest
if errorlevel 1 goto :fail

echo === validate examples\HELLO.bas ===
"%TOOL%" validate "%ROOT%\examples\HELLO.bas"
if errorlevel 1 goto :fail

echo === bas2cas / cas2wav / wav2cas / cas2bas ===
"%TOOL%" bas2cas "%ROOT%\examples\HELLO.bas" -o "%WORK%\HELLO.cas" --name HELLO --force
if errorlevel 1 goto :fail
"%TOOL%" cas2wav "%WORK%\HELLO.cas" -o "%WORK%\HELLO.wav" --baud 2400 --volume 60 --force
if errorlevel 1 goto :fail
"%TOOL%" wav2cas "%WORK%\HELLO.wav" -o "%WORK%\HELLO2.cas" --force
if errorlevel 1 goto :fail
"%TOOL%" cas2bas "%WORK%\HELLO2.cas" -o "%WORK%\HELLO2.bas" --force
if errorlevel 1 goto :fail

echo === normalize / decode normalized copy ===
"%TOOL%" normalize "%WORK%\HELLO.wav" -o "%WORK%\HELLO.normalized.wav" --force
if errorlevel 1 goto :fail
"%TOOL%" wav2cas "%WORK%\HELLO.normalized.wav" -o "%WORK%\HELLO_NORM.cas" --force
if errorlevel 1 goto :fail

echo === token image comparison ===
"%TOOL%" tokenize "%ROOT%\examples\HELLO.bas" -o "%WORK%\A.bin" --force
if errorlevel 1 goto :fail
"%TOOL%" tokenize "%WORK%\HELLO2.bas" -o "%WORK%\B.bin" --force
if errorlevel 1 goto :fail
fc /b "%WORK%\A.bin" "%WORK%\B.bin" >nul
if errorlevel 1 (
  echo [NG] Token images differ.
  goto :fail
)

echo.
echo Smoke test OK.
echo Work files: %WORK%
exit /b 0

:fail
echo.
echo Smoke test FAILED.
exit /b 1
