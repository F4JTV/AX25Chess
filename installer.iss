; ===========================================================================
;  Installateur AX25Chess  -  Inno Setup 6.3 ou superieur
;
;  Construire l'application d'abord :
;      python make_icon.py
;      python make_version.py
;      pyinstaller --noconfirm --clean AX25Chess.spec
;
;  Puis compiler ce script :
;      build_windows.ps1 installer
;  ou l'ouvrir dans l'IDE du compilateur et appuyer sur F9. Le chemin exact
;  d'ISCC.exe depend de la version et de l'edition installees : Inno Setup 7
;  existe en 32 et en 64 bits, donc sous « Program Files » comme sous
;  « Program Files (x86) », et il peut aussi etre installe par utilisateur.
;  C'est pourquoi build_windows.ps1 le cherche plutot que de le supposer.
;
;  Sortie : Output\AX25Chess-<version>-setup.exe
;
;  Notes sur Inno Setup 7 :
;    - SetupArchitecture est nouveau en 7. Le mettre a x64 produit un
;      installateur 64 bits et change les valeurs par defaut de
;      ArchitecturesAllowed et ArchitecturesInstallIn64BitMode. Les deux sont
;      declarees explicitement ci-dessous pour que le script reste correct
;      sous Inno Setup 6, ou SetupArchitecture est inconnu et simplement
;      ignore.
;    - x64compatible remplace l'ancien identifiant x64, deprecie en 6.3. Il
;      couvre le x64 et l'ARM64 executant du x64 en emulation, ce dont un
;      build PyInstaller x64 a effectivement besoin.
; ===========================================================================

#define AppName        "AX25Chess"
#define AppPublisher   "F4JTV"
#define AppExeName     "AX25Chess.exe"
#define AppURL         "https://github.com/F4JTV"
#define SourceDir      "dist\AX25Chess"

; Numero de version produit par make_version.py depuis ax25chess/__init__.py.
#include "version.iss"

; Optionnel : un Direwolf a installer avec AX25Chess. Deposez ici le dossier
; telecharge depuis la page des versions de Direwolf, celui qui contient
; direwolf.exe. Quand le dossier est absent, le composant disparait
; simplement de l'installateur et tout le reste se construit a l'identique.
#define DirewolfDir    "direwolf"
#define HasDirewolf    DirExists(AddBackslash(SourcePath) + DirewolfDir)

#if HasDirewolf
  #define TypeFullDesc "AX25Chess et Direwolf"
#else
  #define TypeFullDesc "Installation complete"
#endif

[Setup]
; Un AppId stable est ce qui permet a une mise a jour de remplacer
; l'installation precedente au lieu de s'installer a cote. Cette valeur ne
; doit JAMAIS changer apres une premiere diffusion.
AppId={{4E1B7A62-9C35-4D80-A7F1-2B6E0D9C3A54}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}

; Installateur 64 bits, en accord avec un Python et un PyInstaller 64 bits.
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Installation par utilisateur par defaut, donc sans invite administrateur ;
; l'operateur peut toujours choisir une installation machine sur la premiere
; page.
PrivilegesRequiredOverridesAllowed=dialog
PrivilegesRequired=lowest

OutputDir=Output
OutputBaseFilename={#AppName}-{#AppVersion}-setup
SetupIconFile=assets\ax25chess.ico
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
LicenseFile=LICENSE.txt
InfoBeforeFile=INSTALL-NOTES.txt
AllowNoIcons=yes
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full";   Description: "{#TypeFullDesc}"
#if HasDirewolf
; Ce type n'a de sens que s'il y a quelque chose a ne pas installer. Le
; declarer sans Direwolf donnerait un choix « compact » identique au choix
; « complet », et surtout, le referencer dans [Components] alors qu'il
; n'existe pas fait echouer la compilation avec « unknown type ».
Name: "compact"; Description: "AX25Chess seul"
#endif
Name: "custom"; Description: "Installation personnalisee"; Flags: iscustom

[Components]
#if HasDirewolf
Name: "main"; Description: "AX25Chess"; Types: full compact custom; Flags: fixed
Name: "direwolf"; Description: "Direwolf, modem logiciel (indispensable sauf si vous en avez deja un)"; Types: full custom
#else
Name: "main"; Description: "AX25Chess"; Types: full custom; Flags: fixed
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Components: main; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "LICENSE.txt"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "README.md";   DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "INSTALL-NOTES.txt"; DestDir: "{app}"; Components: main; Flags: ignoreversion

#if HasDirewolf
; Direwolf va a cote de l'executable et non dans _internal, que PyInstaller
; reecrit a chaque montee de version. L'application le cherche a cet endroit.
Source: "{#DirewolfDir}\*"; DestDir: "{app}\direwolf"; Components: direwolf; \
    Flags: ignoreversion recursesubdirs createallsubdirs
; L'article 3 de la GPL-2.0 exige que les sources, ou une offre de les
; fournir, accompagnent toute distribution binaire. Cette notice ne doit pas
; etre optionnelle.
Source: "DIREWOLF-NOTICE.txt"; DestDir: "{app}\direwolf"; Components: direwolf; \
    Flags: ignoreversion
#endif

[InstallDelete]
; Seul le dossier d'execution d'AX25Chess est efface a la mise a jour : un
; Direwolf que l'operateur a configure, et peut-etre edite, est laisse
; tranquille. C'est aussi pourquoi Direwolf n'est pas dans _internal.
Type: filesandordirs; Name: "{app}\_internal"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExeName}"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; PyInstaller et Python laissent des __pycache__ que l'installateur n'a pas
; deposes : sans cela le dossier survit a la desinstallation.
Type: filesandordirs; Name: "{app}\_internal"
Type: dirifempty;     Name: "{app}\direwolf"
Type: dirifempty;     Name: "{app}"

[Code]
function InitializeSetup(): Boolean;
begin
  Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  { Rien a faire pour l'instant : la configuration de depart est ecrite par
    l'application au premier lancement, dans un dossier inscriptible. La
    faire ici la placerait sous Program Files lors d'une installation
    machine, ou l'operateur ne pourrait ni l'editer ni Direwolf y ecrire. }
end;
