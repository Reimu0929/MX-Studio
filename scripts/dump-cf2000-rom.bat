@echo off
setlocal EnableDelayedExpansion
rem ---------------------------------------------------------------------------
rem Dump a CF-2000 ROM image from cassette recordings made on the real machine.
rem
rem The CF-2000 ROM is soldered in, so do NOT desolder it.  MSX BASIC's BSAVE
rem can read ROM space directly, so save it in 8 KB chunks:
rem
rem   BSAVE"ROM0",&H0000,&H2000
rem   BSAVE"ROM1",&H2000,&H2000
rem   BSAVE"ROM2",&H4000,&H2000
rem   BSAVE"ROM3",&H6000,&H2000
rem
rem Record each block to a separate WAV on the PC, then run:
rem
rem   scripts\dump-cf2000-rom.bat ROM0.wav ROM1.wav ROM2.wav ROM3.wav
rem
rem The input WAVs are never modified.  A normalized copy (for decoding only)
rem is written under %TEMP%.  Output: cf2000.rom next to the first WAV.
rem ---------------------------------------------------------------------------

set "ROOT=%~dp0.."
set "TOOL=%ROOT%\build-win-xp\tools\msxtool\Release\msxtool.exe"
if not exist "%TOOL%" (
  echo [NG] msxtool.exe not found: %TOOL%
  echo      Build it with scripts\build-msvc-xp.bat first.
  exit /b 1
)

if "%~1"=="" (
  echo Usage: %~nx0 ROM0.wav [ROM1.wav [ROM2.wav [ROM3.wav]]]
  echo.
  echo Save the ROM on the CF-2000 in 8 KB chunks:
  echo    BSAVE"ROM0",^&H0000,^&H2000
  echo    BSAVE"ROM1",^&H2000,^&H2000
  echo    BSAVE"ROM2",^&H4000,^&H2000
  echo    BSAVE"ROM3",^&H6000,^&H2000
  echo Record each block separately and pass the WAVs in address order.
  exit /b 1
)

set "OUTDIR=%~dp1"
set "OUT=%OUTDIR%cf2000.rom"
if exist "%OUT%" (
  echo [NG] Output already exists: %OUT%
  echo      Move or delete it first.  This script never overwrites a ROM image.
  exit /b 1
)

set "WORK=%TEMP%\msx-romdump"
if exist "%WORK%" rmdir /s /q "%WORK%"
mkdir "%WORK%"
if errorlevel 1 (
  echo [NG] Cannot create %WORK%
  exit /b 1
)

set /a N=0
set "SRC="

:next
if "%~1"=="" goto :assemble
set /a N+=1
set "WAV=%~f1"
set "CAS=%WORK%\B!N!.cas"
set "BIN=%WORK%\B!N!.bin"
echo.
echo === block !N!: !WAV! ===
if not exist "!WAV!" (
  echo [NG] WAV not found: !WAV!
  goto :fail
)

rem Normalization is only for decoding; the raw recording stays untouched.
"%TOOL%" normalize "!WAV!" -o "%WORK%\B!N!.norm.wav" --force
if errorlevel 1 (
  echo [warn] Normalization failed; decoding the raw recording instead.
  if exist "%WORK%\B!N!.norm.wav" del /q "%WORK%\B!N!.norm.wav"
  set "USE=!WAV!"
) else (
  set "USE=%WORK%\B!N!.norm.wav"
)

"%TOOL%" wav2cas "!USE!" -o "!CAS!" --force
if errorlevel 1 (
  echo [NG] Decoding failed for block !N!.
  echo      Re-record this block and run the script again.
  goto :fail
)

rem Trailing-byte warnings here are the tape checksum and are harmless.
"%TOOL%" casinfo "!CAS!"

"%TOOL%" cas2bin "!CAS!" -o "!BIN!" --force
if errorlevel 1 (
  echo [NG] No binary block in block !N!.
  goto :fail
)
for %%A in ("!BIN!") do echo    chunk: %%~zA bytes

set "SRC=!SRC!+"!BIN!""
shift
goto :next

:assemble
if !N! lss 1 goto :fail
set "SRC=!SRC:~1!"
copy /b !SRC! "%OUT%" >nul
if errorlevel 1 (
  echo [NG] Cannot concatenate the chunks.
  goto :fail
)

for %%A in ("%OUT%") do set "FINAL=%%~zA"
echo.
echo ROM image: %OUT%  (!FINAL! bytes)
if not "!FINAL!"=="32768" (
  echo [warn] Expected 32768 bytes. Check the block order and the addresses shown by casinfo.
)

echo.
echo SHA1:
certutil -hashfile "%OUT%" SHA1 2>nul
if errorlevel 1 echo [info] certutil -hashfile is unavailable; compare the SHA1 by hand.
echo.
echo Compare this SHA1 with the sha1sum in openMSX's National_CF-2000.xml.
echo Work files: %WORK%
exit /b 0

:fail
echo.
echo ROM dump FAILED. Raw WAVs were not modified.
exit /b 1
