#!/usr/bin/env python3
"""
make_version.py - Derive les fichiers de version depuis ax25chess/__init__.py.

Une seule source de verite pour le numero de version : la barre de titre, les
proprietes du fichier .exe et l'installateur ne peuvent donc pas se
contredire.

Produit :
    version_info.txt   ressource de version de l'executable (PyInstaller)
    version.iss        #define AppVersion, inclus par installer.iss

On n'utilise pas GetVersionNumbersString() cote Inno Setup pour lire la
version dans l'exe : cette fonction renvoie quatre nombres, et l'installateur
se serait appele AX25Chess-1.0.0.0-setup.exe.
"""

from __future__ import annotations

import ast
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

VERSION_INFO = '''\
VSVersionInfo(
  ffi=FixedFileInfo(
    filevers=({a}, {b}, {c}, 0),
    prodvers=({a}, {b}, {c}, 0),
    mask=0x3f,
    flags=0x0,
    OS=0x40004,
    fileType=0x1,
    subtype=0x0,
    date=(0, 0)
  ),
  kids=[
    StringFileInfo([
      StringTable(
        '040C04B0',
        [StringStruct('CompanyName', {publisher}),
         StringStruct('FileDescription', {description}),
         StringStruct('FileVersion', {version}),
         StringStruct('InternalName', {name}),
         StringStruct('LegalCopyright', {copyright}),
         StringStruct('OriginalFilename', {filename}),
         StringStruct('ProductName', {name}),
         StringStruct('ProductVersion', {version})])
    ]),
    VarFileInfo([VarStruct('Translation', [1036, 1200])])
  ]
)
'''

def py_literal(value: str) -> str:
    """Cite une valeur pour l'inserer dans le fichier de version.

    version_info.txt est du Python evalue par PyInstaller : une apostrophe
    dans une valeur — « Jeu d'echecs » — terminait le litteral et rendait le
    fichier insyntaxique. On laisse donc repr() faire la citation plutot que
    de poser les guillemets a la main dans le gabarit.
    """
    return repr(str(value))


NAME = "AX25Chess"
PUBLISHER = "F4JTV"
DESCRIPTION = "Jeu d'echecs par radio packet AX.25"
COPYRIGHT = "Copyright (C) 2026 F4JTV"


def read_version() -> str:
    path = os.path.join(HERE, "ax25chess", "__init__.py")
    with open(path, encoding="utf-8") as handle:
        match = re.search(r'^__version__\s*=\s*["\']([^"\']+)["\']',
                          handle.read(), re.M)
    if not match:
        sys.exit(f"__version__ introuvable dans {path}")
    return match.group(1)


def main() -> int:
    version = read_version()
    parts = [int(p) for p in re.findall(r"\d+", version)][:3]
    while len(parts) < 3:
        parts.append(0)
    a, b, c = parts

    text = VERSION_INFO.format(
        a=a, b=b, c=c,
        version=py_literal(version),
        name=py_literal(NAME),
        filename=py_literal(NAME + ".exe"),
        publisher=py_literal(PUBLISHER),
        description=py_literal(DESCRIPTION),
        copyright=py_literal(COPYRIGHT),
    )

    # Verification avant ecriture : PyInstaller evalue ce fichier, et une
    # erreur de syntaxe ne se manifesterait qu'au milieu du build.
    try:
        ast.parse(text)
    except SyntaxError as exc:
        sys.exit(f"version_info.txt genere invalide (ligne {exc.lineno}) : "
                 f"{exc.msg}")

    info = os.path.join(HERE, "version_info.txt")
    with open(info, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)

    iss = os.path.join(HERE, "version.iss")
    with open(iss, "w", encoding="utf-8", newline="") as handle:
        handle.write(f'#define AppVersion "{version}"\n')

    print(f"version : {version}")
    print(f"ecrit   : {info}")
    print(f"ecrit   : {iss}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
