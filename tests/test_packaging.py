#!/usr/bin/env python3
"""
Empaquetage Windows : validation statique de installer.iss, et simulation
d'une installation gelee avec Direwolf embarque.

La simulation reproduit ce que PyInstaller pose en mode « un dossier » :
    AX25Chess\\
    +-- AX25Chess.exe        (sys.executable)
    +-- _internal\\           (sys._MEIPASS)
    +-- direwolf\\direwolf.exe
"""

import ast
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
# Les assertions portant sur des libelles doivent etre deterministes.
os.environ["AX25CHESS_LANG"] = "fr"
ROOT = Path(__file__).resolve().parents[1]

FAKE_PORT = 8021

FAKE_DIREWOLF = '''#!/usr/bin/env python3
import socket, sys, threading, time
if "--version" in sys.argv:
    print("Dire Wolf version 1.8")
    sys.exit(0)
print("Dire Wolf version 1.8 (banc de test AX25Chess)", flush=True)
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", %d))
srv.listen(4)
print("Ready to accept KISS TCP client application 0 on port %d ...", flush=True)
clients = []
def loop():
    while True:
        conn, _ = srv.accept()
        clients.append(conn)
threading.Thread(target=loop, daemon=True).start()
while True:
    time.sleep(1)
''' % (FAKE_PORT, FAKE_PORT)

results: list[bool] = []


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    results.append(bool(ok))
    return bool(ok)


# ---------------------------------------------------------------- installer

def resolve_preprocessor(text: str, has_direwolf: bool) -> str:
    """Resout #if HasDirewolf / #else / #endif pour une valeur donnee.

    Reproduit ce que fait le preprocesseur d'Inno Setup sur la seule
    condition employee par ce script, afin de controler chacune des deux
    variantes reellement compilables.
    """
    out, stack = [], []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("#if "):
            stack.append(has_direwolf if "HasDirewolf" in stripped else True)
            continue
        if stripped == "#else":
            if stack:
                stack[-1] = not stack[-1]
            continue
        if stripped == "#endif":
            if stack:
                stack.pop()
            continue
        if all(stack):
            out.append(line)
    return "\n".join(out)


def section(text: str, name: str) -> str:
    """Contenu d'une section [Nom], commentaires retires."""
    match = re.search(rf"^\[{name}\]\s*$(.*?)(?=^\[|\Z)", text,
                      re.M | re.S)
    if not match:
        return ""
    return "\n".join(line for line in match.group(1).splitlines()
                      if not line.strip().startswith(";"))


def installer_tests() -> None:
    iss = ROOT / "installer.iss"
    if not iss.is_file():
        check("installer.iss present", False)
        return
    text = iss.read_text(encoding="utf-8")

    sections = re.findall(r"^\[(\w+)\]", text, re.M)
    known = {"Setup", "Languages", "Tasks", "Files", "Icons", "Run",
             "UninstallDelete", "Code", "Dirs", "Registry", "INI", "Messages",
             "CustomMessages", "Types", "Components", "UninstallRun",
             "InstallDelete"}
    check("sections toutes connues", not set(sections) - known)
    check("[Setup] declare une seule fois", sections.count("Setup") == 1)

    # Inno Setup lit toute ligne dont le premier caractere non blanc est un
    # crochet comme un en-tete de section, y compris au milieu d'un bloc
    # Pascal. Une continuation d'appel commencant par un tableau d'arguments
    # a ainsi produit un « Invalid section tag » en pleine section [Code].
    headers = {i for i, line in enumerate(text.split("\n"))
               if line.startswith("[") and line.rstrip().endswith("]")}
    stray = [(i + 1, line.strip()) for i, line in enumerate(text.split("\n"))
             if line.strip().startswith("[") and i not in headers]
    check("aucune ligne ne commence par un crochet hors en-tete de section",
          not stray)
    for number, line in stray[:5]:
        print(f"      ligne {number} : {line[:60]}")

    # Un commentaire Pascal { ... } se termine au premier } rencontre : y
    # ecrire une constante {code:...} ou {app} le referme trop tot et le
    # reste du commentaire devient du code.
    code_block = text[text.index("[Code]"):]
    depth_issue = re.findall(r"\{[^{}]*\{[^{}]*\}[^{}]*\}", code_block)
    check("aucun commentaire Pascal contenant une constante entre accolades",
          not depth_issue)
    for bad in depth_issue[:3]:
        print("      ", bad[:70].replace("\n", " "))

    # Meme piege pour la forme parenthesee : un delimiteur cite dans la prose
    # referme le commentaire au milieu.
    nested_paren = [c for c in re.findall(r"\(\*.*?\*\)", code_block, re.S)
                    if "(*" in c[2:]]
    check("aucun commentaire parenthese contenant un delimiteur",
          not nested_paren)
    for bad in nested_paren[:3]:
        print("      ", bad[:70].replace("\n", " "))

    n_if = len(re.findall(r"^\s*#if\b", text, re.M))
    n_end = len(re.findall(r"^\s*#endif\b", text, re.M))
    check(f"directives #if/#endif equilibrees ({n_if})", n_if == n_end and n_if > 0)

    check("AppId au format GUID",
          bool(re.search(r"^AppId=\{\{[0-9A-Fa-f-]{36}\}", text, re.M)))

    # Le fichier se compile en DEUX variantes selon la presence du dossier
    # direwolf. Un controle global ne prouve rien : il verrait un type
    # declare dans une branche et employe dans l'autre. C'est exactement ce
    # qui a laisse passer un « Types: full compact custom » alors que le type
    # compact n'existait que si Direwolf etait empaquete.
    for has_direwolf in (True, False):
        variant = resolve_preprocessor(text, has_direwolf)
        label = "avec Direwolf" if has_direwolf else "sans Direwolf"

        types = section(variant, "Types")
        components = section(variant, "Components")
        declared_types = set(re.findall(r'^Name:\s*"(\w+)"', types, re.M))
        declared_components = set(re.findall(r'^Name:\s*"(\w+)"', components, re.M))

        used_types = {w for grp in re.findall(r"Types:\s*([\w ]+)", variant)
                      for w in grp.split()}
        used_components = {w for grp in re.findall(r"Components:\s*([\w ]+)", variant)
                           for w in grp.split()}

        missing_types = used_types - declared_types
        check(f"{label} : tout type employe est declare", not missing_types)
        if missing_types:
            print("      types inconnus :", sorted(missing_types))

        missing_components = used_components - declared_components
        check(f"{label} : tout composant employe est declare",
              not missing_components)
        if missing_components:
            print("      composants inconnus :", sorted(missing_components))

        check(f"{label} : un type personnalise existe",
              any("iscustom" in line for line in types.splitlines()))
        check(f"{label} : le composant principal est fixe",
              'Name: "main"' in components and "fixed" in components)
        check(f"{label} : composant direwolf coherent",
              ("direwolf" in declared_components) == has_direwolf)

    # Direwolf est un programme a part entiere : dossier voisin, jamais sous
    # {app}, et surtout jamais dans _internal que [InstallDelete] efface a
    # chaque mise a jour.
    check("Direwolf installe dans un dossier voisin",
          'DestDir: "{code:GetDirewolfDir}"' in text)
    check("plus aucune installation sous {app}",
          r'DestDir: "{app}\direwolf"' not in text)
    check("emplacement choisi par l'operateur",
          "CreateInputDirPage" in text and "GetDirewolfDir" in text)
    check("ecrasement d'un Direwolf existant signale",
          "DirewolfExists" in text and "NextButtonClick" in text)
    check("page masquee si le composant n'est pas retenu",
          "ShouldSkipPage" in text and "WizardIsComponentSelected" in text)
    check("emplacement memorise dans le registre",
          "DirewolfDir" in section(text, "Registry"))
    check("desinstallateur relit l'emplacement",
          "RegQueryStringValue" in text)
    check("_internal efface a la mise a jour",
          r'Name: "{app}\_internal"' in text)
    check("_internal seul dans [InstallDelete]",
          "direwolf" not in text.split("[InstallDelete]")[1].split("[")[0])

    check("notice GPL non optionnelle pour le binaire",
          "DIREWOLF-NOTICE.txt" in text)

    # --- bilinguisme de l'installateur --------------------------------
    languages = section(text, "Languages")
    declared_languages = set(re.findall(r'^Name:\s*"(\w+)"', languages, re.M))
    check("installateur bilingue anglais/francais",
          {"english", "french"} <= declared_languages)
    check("langue detectee depuis l'interface Windows",
          "LanguageDetectionMethod=uilanguage" in text)
    check("boite de choix seulement si la detection echoue",
          "ShowLanguageDialog=auto" in text)
    check("notes d'installation declarees par langue",
          text.count("InfoBeforeFile:") == len(declared_languages))
    check("aucun InfoBeforeFile global residuel",
          "InfoBeforeFile=" not in text)

    messages = section(text, "CustomMessages")
    defined = {}
    for line in messages.splitlines():
        match = re.match(r"^(\w+)\.(\w+)=", line.strip())
        if match:
            defined.setdefault(match.group(2), set()).add(match.group(1))
    # Deux formes coexistent : {cm:Nom} dans les sections, et
    # CustomMessage('Nom') dans le code Pascal. Ne regarder que la premiere
    # ferait passer tous les messages du desinstallateur pour orphelins.
    used = set(re.findall(r"\{cm:(\w+)", text))
    used |= set(re.findall(r"CustomMessage\(\s*'(\w+)'\s*\)", text))
    # LaunchProgram, CreateDesktopIcon et UninstallProgram viennent des
    # fichiers de messages fournis par Inno Setup, pas de ce script.
    builtin = {"LaunchProgram", "CreateDesktopIcon", "AdditionalIcons",
               "UninstallProgram"}
    custom_used = used - builtin
    check("tout message personnalise employe est defini",
          not (custom_used - set(defined)))
    incomplete = sorted(name for name in custom_used
                        if defined.get(name, set()) != declared_languages)
    check("chaque message personnalise existe dans les deux langues",
          not incomplete)
    for name in incomplete[:5]:
        print("      incomplet :", name, sorted(defined.get(name, [])))

    orphan = sorted(set(defined) - custom_used)
    check("aucun message personnalise inutilise", not orphan)
    for name in orphan[:5]:
        print("      orphelin :", name)

    # --- desinstallation de Direwolf ----------------------------------
    check("Direwolf survit a la desinstallation ordinaire",
          "uninsneveruninstall" in text)
    check("le sort de Direwolf est decide par une question",
          "CurUninstallStepChanged" in text and "RemoveDirewolf" in text)
    check("suppression recursive du dossier Direwolf",
          "DelTree" in text)
    check("aucun effacement automatique du dossier Direwolf",
          "direwolf" not in section(text, "UninstallDelete").lower())

    # Les commentaires Pascal sont retires avant le comptage, et l'on
    # compte « end » et non « end; » : celui qui precede un else n'a pas de
    # point-virgule, et l'ancienne version signalait a tort un desequilibre.
    code = re.sub(r"\{[^}]*\}", " ", text[text.index("[Code]"):])
    begins = len(re.findall(r"\bbegin\b", code))
    ends = len(re.findall(r"\bend\b", code))
    check(f"bloc [Code] equilibre (begin={begins}, end={ends})", begins == ends)

    for name in ("LICENSE.txt", "INSTALL-NOTES.en.txt", "INSTALL-NOTES.fr.txt",
                 "DIREWOLF-NOTICE.txt",
                 "version.iss", "assets/ax25chess.ico"):
        check(f"fichier requis present : {name}", (ROOT / name).exists())


# PyInstaller evalue version_info.txt avec ces classes dans son espace de
# noms. Les reproduire ici verifie le fichier exactement comme lui, sans
# dependre de win32api, indisponible hors Windows.
class _VersionNode:
    def __init__(self, *args, **kwargs):
        self.args, self.kwargs = args, kwargs


class _StringStruct:
    def __init__(self, name, val):
        self.name, self.val = name, val


def read_version_fields(path: Path) -> dict:
    namespace = {
        "VSVersionInfo": _VersionNode, "FixedFileInfo": _VersionNode,
        "StringFileInfo": type("StringFileInfo", (_VersionNode,), {}),
        "VarFileInfo": _VersionNode, "VarStruct": _VersionNode,
        "StringTable": _VersionNode, "StringStruct": _StringStruct,
    }
    info = eval(path.read_text(encoding="utf-8"), namespace)
    fields = {}
    kids = info.kwargs.get("kids", [])
    for kid in kids:
        if type(kid).__name__ != "StringFileInfo":
            continue
        for table in (kid.kwargs.get("kids") or kid.args[0]):
            for item in (table.kwargs.get("kids") or table.args[1]):
                fields[item.name] = item.val
    return fields


def version_tests() -> None:
    """version_info.txt est du Python : les apostrophes doivent survivre.

    « Jeu d'echecs » terminait le litteral et rendait le fichier
    insyntaxique ; l'erreur ne se serait manifestee qu'au milieu du build.
    """
    import importlib.util
    import subprocess

    subprocess.run([sys.executable, "make_version.py"], cwd=ROOT,
                   capture_output=True, check=False)
    info = ROOT / "version_info.txt"
    if not check("version_info.txt genere", info.is_file()):
        return

    try:
        ast.parse(info.read_text(encoding="utf-8"))
        parsed = True
    except SyntaxError:
        parsed = False
    check("version_info.txt syntaxiquement valide", parsed)
    if not parsed:
        return

    fields = read_version_fields(info)
    check("evaluable comme le fait PyInstaller", bool(fields))
    check("apostrophe preservee dans FileDescription",
          fields.get("FileDescription") == "Jeu d'echecs par radio packet AX.25")

    source = (ROOT / "ax25chess" / "__init__.py").read_text(encoding="utf-8")
    match = re.search(r"^__version__\s*=\s*[\"']([^\"']+)", source, re.M)
    version = match.group(1) if match else ""
    check("version alignee sur ax25chess/__init__.py",
          bool(version) and fields.get("ProductVersion") == version)
    iss = (ROOT / "version.iss").read_text(encoding="utf-8")
    check("version.iss aligne sur la meme source",
          bool(version) and f'"{version}"' in iss)

    # Une valeur hostile ne doit pas davantage casser le fichier.
    spec_mod = importlib.util.spec_from_file_location(
        "make_version", ROOT / "make_version.py")
    module = importlib.util.module_from_spec(spec_mod)
    spec_mod.loader.exec_module(module)
    nasty = module.VERSION_INFO.format(
        a=1, b=0, c=0,
        version=module.py_literal("1.0.0"),
        name=module.py_literal('Nom "cite" et \\ antislash'),
        filename=module.py_literal("x.exe"),
        publisher=module.py_literal("O'Brien & Co"),
        description=module.py_literal("guillemet \" et apostrophe '"),
        copyright=module.py_literal("(C) 2026"))
    try:
        ast.parse(nasty)
        nasty_ok = True
    except SyntaxError:
        nasty_ok = False
    check("guillemets, apostrophes et antislashs tous cites", nasty_ok)


def build_script_tests() -> None:
    """Le script de construction ne doit pas confondre un mode et un chemin.

    « .\\build_windows.cmd full » liait `full` a -InnoSetupPath, parametre
    positionnel par defaut en PowerShell. Le script croyait recevoir un chemin,
    sautait toute la recherche d'Inno Setup et echouait sur un « ISCC.exe
    introuvable » sans rapport avec la cause.
    """
    ps1 = ROOT / "build_windows.ps1"
    if not check("build_windows.ps1 present", ps1.is_file()):
        return
    text = ps1.read_text(encoding="utf-8")

    check("accolades equilibrees", text.count("{") == text.count("}"))
    check("parentheses equilibrees", text.count("(") == text.count(")"))

    check("le mode est un parametre positionnel declare",
          "Position = 0" in text and "$Mode" in text)
    check("les modes acceptes sont valides par ValidateSet",
          bool(re.search(r'ValidateSet\("full",\s*"app",\s*"installer",\s*"clean"\)',
                         text)))
    check("filet de securite si un mode se lie a -InnoSetupPath",
          "knownModes" in text and "$InnoSetupPath = \"\"" in text)
    check("-InnoSetupPath verifie avant usage",
          "Resolve-Iscc $InnoSetupPath" in text)
    check("message d'erreur citant la valeur fautive",
          "ne designe ni" in text)

    # La recherche ne doit dependre d'aucun numero de version code en dur :
    # Inno Setup 7 existe en 32 et en 64 bits, donc sous les deux « Program
    # Files », et peut aussi etre installe par utilisateur.
    check("recherche dans le PATH", 'Get-Command "ISCC.exe"' in text)
    check("recherche par la base de registre",
          "InstallLocation" in text and "Uninstall" in text)
    check("recherche par l'association des fichiers .iss",
          "InnoSetupScriptFile" in text)
    check("recherche generique toutes versions",
          '"Inno Setup*"' in text)
    check("installation par utilisateur couverte", "LOCALAPPDATA" in text)
    check("aucun chemin fige sur une version",
          "Inno Setup 7\\ISCC.exe" not in text
          and "Inno Setup 6\\ISCC.exe" not in text)
    check("emplacements consultes affiches en cas d'echec",
          "Emplacements consultes" in text)

    cmd = ROOT / "build_windows.cmd"
    if check("build_windows.cmd present", cmd.is_file()):
        cmd_text = cmd.read_text(encoding="utf-8")
        check("le lanceur transmet ses arguments", "%*" in cmd_text)
        check("le lanceur documente les modes", "installer" in cmd_text)


def portability_tests() -> None:
    """Portabilite Windows / Linux du code applicatif."""
    src = {name: (ROOT / "ax25chess" / f"{name}.py").read_text(encoding="utf-8")
           for name in ("direwolf", "board_widget", "main_window",
                        "game_manager", "resources", "net_link", "games",
                        "protocol", "chess_rules", "ax25_kiss")}

    def code_lines(text: str) -> list[tuple[int, str]]:
        """Lignes de code seules : la docstring de module decrit justement ces
        mecanismes, et une recherche naive y trouverait de fausses occurrences."""
        body = text.split('"""', 2)[-1] if text.count('"""') >= 2 else text
        offset = text.count("\n", 0, len(text) - len(body))
        out = []
        for i, line in enumerate(body.splitlines(), start=offset + 1):
            stripped = line.strip()
            if stripped and not stripped.startswith("#"):
                out.append((i, line))
        return out

    def guarded_by_platform(text: str, needle: str) -> bool:
        lines = code_lines(text)
        for index, (_, line) in enumerate(lines):
            if needle not in line:
                continue
            window = "\n".join(l for _, l in lines[max(0, index - 25):index + 1])
            if "IS_WINDOWS" not in window:
                return False
        return True

    for needle in ("taskkill", "os.kill", "CREATE_NEW_CONSOLE"):
        check(f"{needle} garde par une verification de plateforme",
              guarded_by_platform(src["direwolf"], needle))

    check("taskkill n'affiche pas de console",
          "creationflags=CREATE_NO_WINDOW" in src["direwolf"])

    # « monospace » est un alias fontconfig : il ne se resout pas sous Windows.
    for name, text in src.items():
        check(f"{name}.py sans QFont(\"monospace\")",
              'QFont("monospace"' not in text)
    check("la feuille de style injecte une famille reelle",
          'build_style().replace(' in src["main_window"]
          and "mono_family()" in src["main_window"])

    # QPainterPath.addText ne substitue pas les glyphes manquants.
    check("la police des pieces est verifiee glyphe par glyphe",
          "inFont" in src["board_widget"]
          and "_has_chess_glyphs" in src["board_widget"])

    # Aucun separateur de chemin code en dur dans la logique.
    for name, text in src.items():
        bad = re.findall(r'["\'][A-Za-z0-9_.-]+\\\\[A-Za-z0-9_.-]+["\']', text)
        bad = [b for b in bad if "Program Files" not in b and "\\\\n" not in b]
        check(f"{name}.py sans separateur Windows fige", not bad)

    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    from PyQt6.QtCore import QSettings
    from PyQt6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication([])
    QSettings("AX25Chess", "AX25Chess").clear()
    from ax25chess.board_widget import SOLID, mono_family, pick_chess_font
    from PyQt6.QtGui import QFont, QFontMetrics

    family = pick_chess_font()
    metrics = QFontMetrics(QFont(family, 24))
    check(f"police des pieces resolue ({family}) avec tous les glyphes",
          all(metrics.inFont(glyph) for glyph in SOLID.values()))
    mono = mono_family()
    check(f"police fixe resolue en famille reelle ({mono})",
          bool(mono) and mono.lower() != "monospace")


def spec_tests() -> None:
    spec = (ROOT / "AX25Chess.spec").read_text(encoding="utf-8")
    check("UPX desactive (compresser les DLL Qt fait planter)",
          "upx=False" in spec and "upx=True" not in spec)
    check("QtNetwork declare en import cache", "PyQt6.QtNetwork" in spec)
    check("build en un dossier (COLLECT present)", "COLLECT(" in spec)


# ------------------------------------------------------- installation gelee

def make_install_tree() -> Path:
    """Reproduit la disposition posee par l'installateur.

        Program Files\\
        +-- AX25Chess\\ AX25Chess.exe et _internal\\
        +-- Direwolf\\  direwolf.exe

    Direwolf est un dossier VOISIN, pas un sous-dossier : c'est cette
    disposition que bundled_direwolf() doit savoir retrouver.
    """
    parent = Path(tempfile.mkdtemp(prefix="ax25chess-install-"))
    root = parent / "AX25Chess"
    internal = root / "_internal"
    internal.mkdir(parents=True)
    shutil.copytree(ROOT / "ax25chess", internal / "ax25chess")
    if (ROOT / "assets").is_dir():
        shutil.copytree(ROOT / "assets", internal / "assets")
    dw = parent / "Direwolf"
    dw.mkdir()
    exe = dw / "direwolf"
    exe.write_text(FAKE_DIREWOLF)
    exe.chmod(0o755)
    (dw / "DIREWOLF-NOTICE.txt").write_text("banc de test")
    return root


def frozen_tests(root: Path) -> None:
    internal = root / "_internal"
    sys.frozen = True                       # type: ignore[attr-defined]
    sys._MEIPASS = str(internal)            # type: ignore[attr-defined]
    sys.executable = str(root / "AX25Chess")
    sys.path.insert(0, str(internal))

    sandbox = Path(tempfile.mkdtemp(prefix="ax25chess-state-"))
    import ax25chess.games as games_mod
    games_mod.STATE_DIR = sandbox
    games_mod.GAMES_DIR = sandbox / "parties"
    games_mod.LEGACY_FILE = sandbox / "absent.json"

    from PyQt6.QtCore import QSettings, QTimer
    from PyQt6.QtWidgets import QApplication

    from ax25chess.direwolf import find_direwolf
    from ax25chess.resources import (application_dir, bundled_direwolf,
                                     icon_path, resource_root)

    app = QApplication.instance() or QApplication([])
    QSettings("AX25Chess", "AX25Chess").clear()

    check("resource_root pointe sur _internal", resource_root() == str(internal))
    check("application_dir pointe sur le dossier de l'executable",
          application_dir() == str(root))
    check("Direwolf detecte dans le dossier voisin",
          bundled_direwolf() == str(root.parent / "Direwolf" / "direwolf"))
    check("copie embarquee preferee au PATH", find_direwolf() == bundled_direwolf())
    check("icone resolue en mode gele", icon_path() is not None)

    from ax25chess.main_window import MainWindow
    win = MainWindow()
    win.sp_port.setValue(FAKE_PORT)

    check("premier lancement detecte", win._first_run)
    check("executable pre-rempli", win.ed_exe.text() == bundled_direwolf())
    check("lancement automatique active", win.chk_launch.isChecked())
    check("connexion automatique activee", win.chk_autoconnect.isChecked())

    conf = win.ed_conf.text()
    check("configuration de depart ecrite", bool(conf) and os.path.isfile(conf))
    check("configuration hors du dossier d'installation",
          bool(conf) and not conf.startswith(str(root)))
    if conf and os.path.isfile(conf):
        check("KISSPORT present dans la configuration",
              "KISSPORT" in Path(conf).read_text())

    steps: list[str] = []
    win.direwolf.started.connect(lambda: steps.append("lance"))
    win.direwolf.ready.connect(lambda: steps.append("pret"))
    win._startup_done = False
    win._startup()

    def finish():
        check("Direwolf embarque demarre", "lance" in steps)
        check("port KISS annonce pret", "pret" in steps)
        check("liaison etablie sans intervention", win.link.online)
        win._save_settings()
        win.close()

        second = MainWindow()
        check("second lancement : plus de premier lancement", not second._first_run)
        second.ed_exe.setText("/chemin/choisi/par/operateur")
        second._save_settings()
        third = MainWindow()
        check("choix de l'operateur jamais ecrase",
              third.ed_exe.text() == "/chemin/choisi/par/operateur")
        second.close()
        third.close()

        shutil.rmtree(sandbox, ignore_errors=True)
        shutil.rmtree(root.parent, ignore_errors=True)
        print("\nTOUS LES TESTS PASSENT" if all(results)
              else "\nDES TESTS ONT ECHOUE")
        app.exit(0 if all(results) else 1)

    QTimer.singleShot(5000, finish)
    sys.exit(app.exec())


def main() -> int:
    version_tests()
    portability_tests()
    build_script_tests()
    installer_tests()
    spec_tests()
    frozen_tests(make_install_tree())
    return 0


if __name__ == "__main__":
    sys.exit(main())
