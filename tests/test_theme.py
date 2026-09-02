#!/usr/bin/env python3
"""
Themes sombre et clair.

Le theme clair vise un ecran en plein soleil : l'ecran y perd du contraste et
les nuances moyennes disparaissent les premieres. Chaque paire texte/fond est
donc mesuree, et pas seulement regardee.

Le rapport de contraste est celui de WCAG 2.1. Seuils retenus :
    4.5:1  texte
    3.0:1  elements graphiques porteurs d'information
"""

import os
import sys
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ["AX25CHESS_LANG"] = "fr"
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

results: list[bool] = []


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    results.append(bool(ok))
    return bool(ok)


def luminance(color: str) -> float:
    value = color.lstrip("#")[:6]
    channels = [int(value[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    channels = [c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
                for c in channels]
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2]


def ratio(a: str, b: str) -> float:
    la, lb = luminance(a), luminance(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


# (avant, arriere, seuil, libelle)
PAIRS = [
    ("ink", "chassis", 4.5, "texte principal"),
    ("ink", "panel", 4.5, "texte sur panneau"),
    ("muted", "chassis", 4.5, "texte secondaire"),
    ("muted", "panel", 4.5, "texte secondaire sur panneau"),
    ("amber", "chassis", 4.5, "accent"),
    ("amber", "panel", 4.5, "accent sur panneau"),
    ("on_primary", "amber", 4.5, "texte du bouton principal"),
    ("ok", "chassis", 4.5, "temoin vert"),
    ("alert", "chassis", 4.0, "temoin rouge"),
    ("badge_ink", "badge", 4.5, "identifiant de piece"),
    ("dead_w", "tray", 3.0, "pieces blanches capturees"),
    ("dead_b", "tray", 3.0, "pieces noires capturees"),
    ("piece_b", "light_sq", 3.0, "piece noire sur case claire"),
    ("piece_b", "dark_sq", 3.0, "piece noire sur case sombre"),
    ("piece_w", "dark_sq", 3.0, "piece blanche sur case sombre"),
    ("light_sq", "dark_sq", 3.0, "cases entre elles"),
]


def main() -> int:
    from ax25chess import theme

    for name in ("dark", "light"):
        palette = theme.PALETTES[name]
        print(f"--- theme {name} ---")
        for front, back, threshold, label in PAIRS:
            value = ratio(palette[front], palette[back])
            check(f"{name}: {label} {value:.2f}:1 (>= {threshold})",
                  value >= threshold)

        check(f"{name}: toutes les clefs presentes",
              set(palette) == set(theme.PALETTES["dark"]))

    # Une piece blanche sur case claire ne se distingue que par son contour :
    # c'est vrai de tout jeu d'echecs, et c'est pourquoi ce contour doit etre
    # nettement plus marque en theme clair.
    dark_edge = theme.PALETTES["dark"]["piece_edge"]
    light_edge = theme.PALETTES["light"]["piece_edge"]
    check("contour des pieces renforce en theme clair",
          int(light_edge[-2:], 16) > int(dark_edge[-2:], 16))

    # Commutation
    theme.apply_theme("dark")
    dark_chassis = theme.C_CHASSIS.name()
    theme.apply_theme("light")
    check("les couleurs partagees suivent le theme",
          theme.C_CHASSIS.name() != dark_chassis)
    check("is_light() concordant", theme.is_light())
    theme.apply_theme("inconnu")
    check("theme inconnu ramene au defaut",
          theme.current_theme() in theme.PALETTES)

    theme.apply_theme("light")
    check("survol du bouton principal distinct du fond",
          theme.primary_hover().name() != theme.C_AMBER.name())
    check("voile du dernier coup renforce en clair",
          theme.last_move_alpha() > 74)

    # Bascule sur la fenetre reelle
    from PyQt6.QtCore import QSettings
    from PyQt6.QtWidgets import QApplication
    app = QApplication.instance() or QApplication([])
    app.setQuitOnLastWindowClosed(False)
    QSettings("F4JTV", "AX25Chess").clear()

    from ax25chess.main_window import MainWindow
    win = MainWindow()
    win.resize(1000, 700)
    win.show()

    def sample() -> tuple:
        # Sans traitement des evenements en attente, grab() rendrait encore
        # l'ancien contenu et la comparaison ne prouverait rien.
        app.processEvents()
        image = win.grab().toImage()
        return (image.pixelColor(4, 4).name(), win.styleSheet())

    theme.apply_theme("dark")
    win.cb_theme.setCurrentIndex(win.cb_theme.findData("dark"))
    dark_sample = sample()
    win.cb_theme.setCurrentIndex(win.cb_theme.findData("light"))
    light_sample = sample()

    print("      echantillon sombre :", dark_sample[0])
    print("      echantillon clair  :", light_sample[0])
    check("le rendu change reellement de theme", dark_sample != light_sample)
    check("fond clair effectivement clair",
          luminance(light_sample[0]) > 0.5)
    check("fond sombre effectivement sombre",
          luminance(dark_sample[0]) < 0.1)

    win.cb_theme.setCurrentIndex(win.cb_theme.findData("dark"))
    check("retour au theme sombre", sample() == dark_sample)

    win.close()
    print("\nTOUS LES TESTS PASSENT" if all(results) else "\nDES TESTS ONT ECHOUE")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
