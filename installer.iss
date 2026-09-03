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
; Detection de la langue de l'interface Windows ; la boite de choix
; n'apparait que si elle ne donne rien.
ShowLanguageDialog=auto
LanguageDetectionMethod=uilanguage
Compression=lzma2/max
SolidCompression=yes
AllowNoIcons=yes
CloseApplications=yes
RestartApplications=no

[Languages]
; L'anglais est place en premier : c'est la langue de repli quand la detection
; echoue. Sur une machine francaise, LanguageDetectionMethod=uilanguage
; selectionne le francais tout seul et aucune boite de dialogue n'apparait.
;
; La langue retenue ici est memorisee et reutilisee par le desinstallateur :
; UsePreviousLanguage vaut yes par defaut, il n'y a rien de plus a faire.
Name: "english"; MessagesFile: "compiler:Default.isl"; \
    LicenseFile: "LICENSE.txt"; InfoBeforeFile: "INSTALL-NOTES.en.txt"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"; \
    LicenseFile: "LICENSE.txt"; InfoBeforeFile: "INSTALL-NOTES.fr.txt"

[Types]
#if HasDirewolf
Name: "full";   Description: "{cm:TypeFullBoth}"
#else
Name: "full";   Description: "{cm:TypeFullPlain}"
#endif
#if HasDirewolf
; Ce type n'a de sens que s'il y a quelque chose a ne pas installer. Le
; declarer sans Direwolf donnerait un choix « compact » identique au choix
; « complet », et surtout, le referencer dans [Components] alors qu'il
; n'existe pas fait echouer la compilation avec « unknown type ».
Name: "compact"; Description: "{cm:TypeCompact}"
#endif
Name: "custom"; Description: "{cm:TypeCustom}"; Flags: iscustom

[Components]
#if HasDirewolf
Name: "main"; Description: "AX25Chess"; Types: full compact custom; Flags: fixed
Name: "direwolf"; Description: "{cm:CompDirewolf}"; Types: full custom
#else
Name: "main"; Description: "AX25Chess"; Types: full custom; Flags: fixed
#endif

[CustomMessages]
; Le prefixe reprend le Name declare dans [Languages]. Une chaine sans
; prefixe servirait a toutes les langues ; ici tout est traduit.
english.TypeFullBoth=AX25Chess and Direwolf
french.TypeFullBoth=AX25Chess et Direwolf
english.TypeFullPlain=Full installation
french.TypeFullPlain=Installation complete
english.TypeCompact=AX25Chess only
french.TypeCompact=AX25Chess seul
english.TypeCustom=Custom installation
french.TypeCustom=Installation personnalisee
english.CompDirewolf=Direwolf software modem (needed unless you already have one)
french.CompDirewolf=Direwolf, modem logiciel (indispensable sauf si vous en avez deja un)

english.DirewolfPageTitle=Direwolf location
french.DirewolfPageTitle=Emplacement de Direwolf
english.DirewolfPageSubTitle=Where should Direwolf be installed?
french.DirewolfPageSubTitle=Ou Direwolf doit-il etre installe ?
english.DirewolfPageText=Direwolf is a separate program. It is installed next to AX25Chess rather than inside it, so that it can be used on its own or with other software.%n%nSelect a folder, then click Next.
french.DirewolfPageText=Direwolf est un programme distinct. Il est installe a cote d'AX25Chess plutot qu'a l'interieur, afin de pouvoir servir seul ou avec d'autres logiciels.%n%nChoisissez un dossier, puis cliquez sur Suivant.
english.DirewolfExists=This folder already contains a Direwolf installation:%n%n    %1%n%nContinuing will overwrite it. Choose another folder if you want to keep it.%n%nOverwrite?
french.DirewolfExists=Ce dossier contient deja une installation de Direwolf :%n%n    %1%n%nPoursuivre l'ecrasera. Choisissez un autre dossier si vous souhaitez la conserver.%n%nEcraser ?
english.RemoveDirewolf=Direwolf was installed with AX25Chess, in:%n%n    %1%n%nRemove it completely, including any configuration file and log it has written there?%n%nChoose No to keep Direwolf and use it with another program.
french.RemoveDirewolf=Direwolf a ete installe avec AX25Chess, dans :%n%n    %1%n%nLe supprimer completement, y compris les fichiers de configuration et de journal qu'il y aurait ecrits ?%n%nRepondez Non pour conserver Direwolf et l'employer avec un autre programme.
english.DirewolfKept=Direwolf was left in place:%n%n    %1
french.DirewolfKept=Direwolf a ete conserve dans :%n%n    %1

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; \
    GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Components: main; \
    Flags: ignoreversion recursesubdirs createallsubdirs
Source: "LICENSE.txt"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "README.md";    DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "README.fr.md"; DestDir: "{app}"; Components: main; Flags: ignoreversion skipifsourcedoesntexist
Source: "INSTALL-NOTES.en.txt"; DestDir: "{app}"; Components: main; Flags: ignoreversion
Source: "INSTALL-NOTES.fr.txt"; DestDir: "{app}"; Components: main; Flags: ignoreversion

#if HasDirewolf
; Direwolf va a cote de l'executable et non dans _internal, que PyInstaller
; reecrit a chaque montee de version. L'application le cherche a cet endroit.
; uninsneveruninstall est ce qui donne un sens au choix propose a la
; desinstallation : sans ce drapeau, l'executable de Direwolf serait retire
; de toute facon et repondre « non » laisserait un dossier inutilisable.
; La suppression est donc entierement pilotee par CurUninstallStepChanged.
Source: "{#DirewolfDir}\*"; DestDir: "{code:GetDirewolfDir}"; Components: direwolf; \
    Flags: ignoreversion recursesubdirs createallsubdirs uninsneveruninstall
; L'article 3 de la GPL-2.0 exige que les sources, ou une offre de les
; fournir, accompagnent toute distribution binaire. Cette notice ne doit pas
; etre optionnelle.
Source: "DIREWOLF-NOTICE.txt"; DestDir: "{code:GetDirewolfDir}"; Components: direwolf; \
    Flags: ignoreversion uninsneveruninstall
#endif

[InstallDelete]
; Seul le dossier d'execution d'AX25Chess est efface a la mise a jour : un
; Direwolf que l'operateur a configure, et peut-etre edite, est laisse
; tranquille. C'est aussi pourquoi Direwolf n'est pas dans _internal.
Type: filesandordirs; Name: "{app}\_internal"

[Registry]
; Le chemin choisi doit survivre a la fin de l'assistant : le
; desinstallateur n'a pas acces aux fonctions {code:} de l'installation, et
; l'application doit pouvoir retrouver Direwolf ou qu'il ait ete mis.
#if HasDirewolf
Root: HKA; Subkey: "Software\{#AppName}"; ValueType: string; \
    ValueName: "DirewolfDir"; ValueData: "{code:GetDirewolfDir}"; \
    Components: direwolf; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\{#AppName}"; Flags: uninsdeletekeyifempty
#endif

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
; Pas de suppression de {app}\direwolf ici : son sort est decide par la
; question posee dans CurUninstallStepChanged, qui s'execute avant.
Type: dirifempty;     Name: "{app}"

[Code]

(* La configuration de depart n'est volontairement pas ecrite ici : lors d'une
   installation machine elle atterrirait sous Program Files, ou l'operateur ne
   pourrait ni l'editer ni Direwolf y ecrire. L'application s'en charge au
   premier lancement, dans un dossier inscriptible. *)

#if HasDirewolf
var
  DirewolfPage: TInputDirWizardPage;

function DefaultDirewolfDir(): String;
var
  Parent: String;
begin
  { Le dossier voisin de celui d'AX25Chess, et non un sous-dossier : Direwolf
    est un programme a part entiere, qui doit pouvoir servir seul ou avec
    d'autres logiciels. }
  Parent := ExtractFileDir(RemoveBackslashUnlessRoot(ExpandConstant('{app}')));
  Result := AddBackslash(Parent) + 'Direwolf';
end;

procedure InitializeWizard();
begin
  DirewolfPage := CreateInputDirPage(wpSelectComponents,
    CustomMessage('DirewolfPageTitle'),
    CustomMessage('DirewolfPageSubTitle'),
    CustomMessage('DirewolfPageText'),
    False, '');
  DirewolfPage.Add('');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  { Inutile de demander ou mettre Direwolf si on ne l'installe pas. }
  Result := (PageID = DirewolfPage.ID) and (not WizardIsComponentSelected('direwolf'));
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  (* La valeur par defaut depend du dossier d'installation, qui n'est connu
     qu'apres la page de destination : elle ne peut donc pas etre posee a la
     creation de la page.

     Ce commentaire emploie la forme parenthesee parce qu'un commentaire
     entre accolades se ferme sur la PREMIERE accolade fermante rencontree :
     y citer une constante Inno Setup le terminerait au milieu, et la suite
     du texte deviendrait du code. *)
  if (CurPageID = DirewolfPage.ID) and (DirewolfPage.Values[0] = '') then
    DirewolfPage.Values[0] := DefaultDirewolfDir();
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Chosen: String;
  Prompt: String;
begin
  Result := True;
  if CurPageID <> DirewolfPage.ID then
    Exit;

  { Le dossier voisin naturel est aussi celui qu'emploie une installation
    ordinaire de Direwolf. Ecraser sans prevenir remplacerait une version que
    l'operateur a peut-etre choisie et configuree. }
  Chosen := DirewolfPage.Values[0];
  if FileExists(AddBackslash(Chosen) + 'direwolf.exe') then
  begin
    { Le tableau d'arguments reste sur cette ligne : Inno Setup lit toute
      ligne commencant par un crochet comme un en-tete de section, y compris
      au milieu d'un bloc Pascal. }
    Prompt := FmtMessage(CustomMessage('DirewolfExists'), [Chosen]);
    Result := MsgBox(Prompt, mbConfirmation, MB_YESNO) = IDYES;
  end;
end;

function GetDirewolfDir(Param: String): String;
begin
  Result := DirewolfPage.Values[0];
  if Result = '' then
    Result := DefaultDirewolfDir();
end;
#endif

function InstalledDirewolfDir(): String;
begin
  (* Chemin retenu a l'installation. Le desinstallateur n'a pas acces aux
     fonctions de rappel employees plus haut : il relit la valeur ecrite dans
     le registre. *)
  Result := '';
  if not RegQueryStringValue(HKA, 'Software\{#AppName}', 'DirewolfDir', Result) then
    Result := '';
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DirewolfDir: String;
  Prompt: String;
begin
  if CurUninstallStep <> usUninstall then
    Exit;

  DirewolfDir := InstalledDirewolfDir();
  if (DirewolfDir = '') or (not DirExists(DirewolfDir)) then
    Exit;

  { Les fichiers de Direwolf portent uninsneveruninstall : ils survivent a la
    desinstallation ordinaire, et c'est cette question qui decide de leur
    sort. Repondre Non laisse donc un Direwolf complet et fonctionnel, pas un
    dossier vide. }
  Prompt := FmtMessage(CustomMessage('RemoveDirewolf'), [DirewolfDir]);
  if MsgBox(Prompt, mbConfirmation, MB_YESNO) = IDYES then
  begin
    DelTree(DirewolfDir, True, True, True);
  end
  else
  begin
    MsgBox(FmtMessage(CustomMessage('DirewolfKept'), [DirewolfDir]),
           mbInformation, MB_OK);
  end;
end;
