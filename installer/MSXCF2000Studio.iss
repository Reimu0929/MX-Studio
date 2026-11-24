; Inno Setup 5.6.1 (Unicode) script.
; Build the app first, then run scripts\build-installer.bat.
; This installs per-user (no administrator rights) and never deletes the
; user's .bas / .cas / .wav files on update or uninstall.

#define AppName "MSX CF-2000 Studio"
#define AppVersion "0.1.0"
#define AppPublisher "MSX CF-2000 project"
#define AppExeName "msxstudio.exe"

[Setup]
AppId={{5B0E6D7A-3C9C-4D4F-9E07-7E0C2C7A62A1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={localappdata}\MSXCF2000Studio
DefaultGroupName={#AppName}
AllowNoIcons=yes
PrivilegesRequired=lowest
OutputDir=..\dist
OutputBaseFilename=msxcf2000studio-{#AppVersion}-setup
Compression=lzma
SolidCompression=yes
UninstallDisplayIcon={app}\{#AppExeName}
VersionInfoVersion=0.1.0.0
VersionInfoDescription=MSX CF-2000 Studio installer
VersionInfoProductName={#AppName}
SetupLogging=yes
DisableProgramGroupPage=no

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[Tasks]
Name: "desktopicon"; Description: "デスクトップにショートカットを作成する"; GroupDescription: "追加タスク:"; Flags: unchecked

[Files]
Source: "..\dist\msxstudio.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\msxtool.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\docs\offline-guide.txt"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\dist\docs\analysis.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\dist\docs\formats.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\dist\docs\verification.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\dist\docs\MSX-CF-2000-technical-spec.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\dist\examples\HELLO.bas"; DestDir: "{app}\examples"; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\ドキュメント（オフラインガイド）"; Filename: "{app}\docs\offline-guide.txt"
Name: "{group}\アンインストール {#AppName}"; Filename: "{uninstallexe}"
Name: "{userdesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{#AppName} を起動する"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; Only files that the installer itself creates are removed.  User sources,
; CAS images and WAV recordings are deliberately outside {app}.
Type: filesandordirs; Name: "{app}\docs"
Type: dirifempty; Name: "{app}"
