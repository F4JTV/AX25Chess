"""
resources.py - Localisation des fichiers, en developpement comme en mode gele.

PyInstaller en mode « un dossier » place les ressources dans un sous-dossier
`_internal`, tandis que l'executable et tout ce que l'installateur depose a
cote se trouvent un niveau au-dessus. Les deux racines ne se confondent donc
pas, et Direwolf appartient a la seconde : `_internal` est reecrit a chaque
montee de version, un programme tiers n'a rien a y faire.

    AX25Chess\\
    +-- AX25Chess.exe          <- application_dir()
    +-- _internal\\             <- resource_root()
    +-- direwolf\\
        +-- direwolf.exe       <- bundled_direwolf()
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


def bundled_direwolf() -> Optional[str]:
    """Chemin d'un Direwolf livre avec l'application, ou None.

    Deux racines sont explorees. `application_dir()` est la reponse normale ;
    le parent de la racine des ressources couvre le cas ou `sys.executable`
    ne serait pas ce qu'il devrait etre. Cela coute deux appels systeme et
    supprime toute une famille de pannes liees a la disposition des fichiers.
    """
    name = "direwolf.exe" if sys.platform.startswith("win") else "direwolf"
    roots = [application_dir()]
    if is_frozen():
        roots.append(os.path.dirname(resource_root()))

    seen = set()
    for root in roots:
        if not root or root in seen:
            continue
        seen.add(root)
        for folder in ("direwolf", os.path.join("direwolf", "bin")):
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
