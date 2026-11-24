@echo off
setlocal
rem Stage files into dist\ and run Inno Setup 5.6.1 (Unicode).
rem Build the app first with scripts\build-msvc-xp.bat.

set ROOT=%~dp0..
set BUILD=%ROOT%\build-win-xp
set DIST=%ROOT%\dist

if not exist "%BUILD%\app\win32\Release\msxstudio.exe" (
  echo msxstudio.exe not found. Run scripts\build-msvc-xp.bat first.
  exit /b 1
)

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%"
mkdir "%DIST%\docs"
mkdir "%DIST%\examples"

copy /y "%BUILD%\app\win32\Release\msxstudio.exe" "%DIST%" >nul
copy /y "%BUILD%\tools\msxtool\Release\msxtool.exe" "%DIST%" >nul
copy /y "%ROOT%\README.md" "%DIST%" >nul
copy /y "%ROOT%\THIRD_PARTY_NOTICES.txt" "%DIST%" >nul
copy /y "%ROOT%\docs\analysis.md" "%DIST%\docs" >nul
copy /y "%ROOT%\docs\formats.md" "%DIST%\docs" >nul
copy /y "%ROOT%\docs\verification.md" "%DIST%\docs" >nul
copy /y "%ROOT%\docs\MSX-CF-2000-technical-spec.md" "%DIST%\docs" >nul
copy /y "%ROOT%\docs\offline-guide.txt" "%DIST%\docs" >nul
copy /y "%ROOT%\examples\HELLO.bas" "%DIST%\examples" >nul

if "%ISCC%"=="" set ISCC=C:\Program Files (x86)\Inno Setup 5\ISCC.exe
if not exist "%ISCC%" set ISCC=C:\Program Files\Inno Setup 5\ISCC.exe
if not exist "%ISCC%" (
  echo ISCC.exe not found. Set ISCC to the Inno Setup 5.6.1 compiler path.
  exit /b 1
)

"%ISCC%" "%ROOT%\installer\MSXCF2000Studio.iss"
if errorlevel 1 exit /b 1
echo Installer written to %DIST%.
exit /b 0
