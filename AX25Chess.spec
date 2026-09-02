# -*- mode: python ; coding: utf-8 -*-
"""
Recette PyInstaller pour AX25Chess.

    python make_icon.py
    python make_version.py
    pyinstaller --noconfirm --clean AX25Chess.spec

Le build est en « un dossier » et non en fichier unique, deliberement : un
onefile se decompresse dans un dossier temporaire a chaque lancement, ce qui
coute plusieurs secondes avec Qt, declenche des heuristiques antivirus, et
complique le raisonnement sur le processus fils Direwolf.

UPX est desactive volontairement : compresser les DLL Qt est une cause
classique de plantages difficiles a diagnostiquer.
"""

import os

block_cipher = None

datas = [('README.md', '.'), ('docs', 'docs')]
if os.path.isdir('assets'):
    datas.append(('assets', 'assets'))

a = Analysis(
    ['main.py'],
    pathex=[],
    binaries=[],
    datas=datas,
    hiddenimports=['PyQt6.QtNetwork'],
    hookspath=[],
    runtime_hooks=[],
    # Les modules Qt que l'application ne charge jamais : l'exclusion fait
    # passer le build d'environ 180 Mo a une petite centaine.
    excludes=[
        'tkinter', 'numpy', 'matplotlib', 'PIL',
        'PyQt6.QtWebEngineCore', 'PyQt6.QtWebEngineWidgets', 'PyQt6.QtWebChannel',
        'PyQt6.QtQml', 'PyQt6.QtQuick', 'PyQt6.QtQuick3D', 'PyQt6.QtQuickWidgets',
        'PyQt6.QtMultimedia', 'PyQt6.QtMultimediaWidgets',
        'PyQt6.Qt3DCore', 'PyQt6.Qt3DRender', 'PyQt6.Qt3DExtras',
        'PyQt6.QtCharts', 'PyQt6.QtDataVisualization',
        'PyQt6.QtBluetooth', 'PyQt6.QtNfc', 'PyQt6.QtPositioning',
        'PyQt6.QtSql', 'PyQt6.QtTest', 'PyQt6.QtDesigner', 'PyQt6.QtHelp',
        'PyQt6.QtPdf', 'PyQt6.QtPdfWidgets', 'PyQt6.QtSpatialAudio',
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)
pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='AX25Chess',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=('assets/ax25chess.ico' if os.path.isfile('assets/ax25chess.ico')
          else None),
    version=('version_info.txt' if os.path.isfile('version_info.txt') else None),
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name='AX25Chess',
)
