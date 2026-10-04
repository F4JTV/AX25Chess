; ============================================================================
;  AX25Chess - Inno Setup script (bilingual English / French)
;
;  Saved as UTF-8 with a byte-order mark: without it Inno Setup may read the
;  file in the system code page, and the French accents come out garbled.
;
;  Built by build_all.bat, which stages everything in dist\ first:
;      ISCC.exe /DAppVersion=x.y.z AX25Chess.iss
;
;  The version is passed by build_all.bat (/DAppVersion=x.y.z), which reads
;  it from CMakeLists.txt. The value below only serves when this file is
;  compiled by hand, and is then wrong: go through build_all.bat.
; ============================================================================

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#define AppName "AX25Chess"
#define AppPublisher "AX25Chess Project"
#define AppExeName "ax25chess.exe"

[Setup]
; The AppId of the Python version 1.x: this installer upgrades it in place.
; Never the one of another program (AX25Chat has its own): the same AppId
; would make one installer take the other for an older version of itself.
AppId={{4E1B7A62-9C35-4D80-A7F1-2B6E0D9C3A54}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
UninstallDisplayIcon={app}\{#AppExeName}
OutputDir=output
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile=..\assets\ax25chess.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
LicenseFile=..\LICENSE.txt

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french";  MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Everything windeployqt and build_all.bat gathered: the program, the Qt
; runtime and its QML modules.
Source: "dist\*"; DestDir: "{app}"; Excludes: "vc_redist.x64.exe"; Flags: ignoreversion recursesubdirs createallsubdirs
; The Visual C++ runtime, unpacked to a temporary folder, run, then deleted.
#ifexist "dist\vc_redist.x64.exe"
Source: "dist\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
#endif

[InstallDelete]
; What the Python version 1.x left in the same folder: its Python runtime.
; The Dire Wolf it may have installed beside AX25Chess is a program of its
; own and is not touched; this version no longer needs it.
Type: filesandordirs; Name: "{app}\_internal"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; A runtime already present, of the same or a newer version, makes it return
; at once with a non-zero code, which Inno Setup ignores.
#ifexist "dist\vc_redist.x64.exe"
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "{cm:InstallingRuntime}"; Flags: waituntilterminated
#endif
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent

[CustomMessages]
english.InstallingRuntime=Installing the Microsoft Visual C++ runtime...
french.InstallingRuntime=Installation du runtime Microsoft Visual C++...

[Code]
// The user's settings (config.json, direwolf.conf) live in
// %LOCALAPPDATA%\AX25Chess and the saved games in %APPDATA%\AX25Chess;
// the uninstaller leaves both alone, so a reinstall keeps them.
