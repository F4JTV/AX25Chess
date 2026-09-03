"""
resources.py - Localisation des fichiers, en developpement comme en mode gele.

PyInstaller en mode « un dossier » place les ressources dans un sous-dossier
`_internal`, tandis que l'executable et tout ce que l'installateur depose a
cote se trouvent un niveau au-dessus. Les deux racines ne se confondent donc
pas, et Direwolf appartient a la seconde : `_internal` est reecrit a chaque
montee de version, un programme tiers n'a rien a y faire.

    Program Files\\
    +-- AX25Chess\\
    |   +-- AX25Chess.exe      <- application_dir()
    |   +-- _internal\\         <- resource_root()
    +-- Direwolf\\
        +-- direwolf.exe       <- bundled_direwolf()

Direwolf est installe A COTE d'AX25Chess et non dedans : c'est un programme a
part entiere, qui doit pouvoir servir seul ou avec d'autres logiciels. Son
emplacement etant choisi par l'operateur pendant l'installation, il est note
dans le registre ; les dossiers voisins habituels restent explores en repli,
ce qui couvre un build portable ou une installation faite a la main.
"""

from __future__ import annotations

import os
import sys
from typing import Optional


def is_frozen() -> bool:
    """Vrai si l'application tourne depuis un build PyInstaller."""
    return getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS")


def resource_root() -> str:
    """Racine des ressources embarquees (`_internal` en mode gele)."""
    if is_frozen():
        return sys._MEIPASS                       # type: ignore[attr-defined]
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def application_dir() -> str:
    """Dossier ou vit l'application elle-meme.

    Distinct de resource_root() : c'est ici que l'installateur depose ce qui
    accompagne l'executable.
    """
    if is_frozen():
        return os.path.dirname(os.path.abspath(sys.executable))
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _registered_direwolf_dir() -> Optional[str]:
    """Dossier note par l'installateur, sous Windows uniquement."""
    if not sys.platform.startswith("win"):
        return None
    try:
        import winreg
    except ImportError:
        return None
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        try:
            with winreg.OpenKey(hive, r"Software\AX25Chess") as key:
                value, _ = winreg.QueryValueEx(key, "DirewolfDir")
        except OSError:
            continue
        if value and os.path.isdir(value):
            return value
    return None


def bundled_direwolf() -> Optional[str]:
    """Chemin d'un Direwolf livre avec l'application, ou None.

    Ordre de recherche, du plus sur au plus general :

    1. le dossier note dans le registre par l'installateur, seul endroit qui
       connaisse un emplacement choisi par l'operateur ;
    2. le dossier voisin de l'application, disposition posee par defaut ;
    3. un sous-dossier `direwolf`, qui couvre un build portable et les
       installations anterieures a ce changement.
    """
    name = "direwolf.exe" if sys.platform.startswith("win") else "direwolf"

    registered = _registered_direwolf_dir()
    if registered:
        candidate = os.path.join(registered, name)
        if os.path.isfile(candidate):
            return candidate

    app = application_dir()
    roots = [app, os.path.dirname(app)]
    if is_frozen():
        roots.append(os.path.dirname(resource_root()))

    folders = ("Direwolf", "direwolf", os.path.join("direwolf", "bin"))
    seen = set()
    for root in roots:
        if not root or root in seen:
            continue
        seen.add(root)
        for folder in folders:
            candidate = os.path.join(root, folder, name)
            if os.path.isfile(candidate):
                return candidate
    return None


def bundled_direwolf_dir() -> Optional[str]:
    """Dossier du Direwolf embarque, utile pour retrouver sa documentation."""
    exe = bundled_direwolf()
    return os.path.dirname(exe) if exe else None


def icon_path() -> Optional[str]:
    """Icone de l'application, si elle est presente."""
    for name in ("ax25chess.ico", "ax25chess.png"):
        candidate = os.path.join(resource_root(), "assets", name)
        if os.path.isfile(candidate):
            return candidate
    return None
