"""
theme.py - Palettes sombre et claire.

Les couleurs sont des objets QColor partages, mutes sur place au changement de
theme. Tout le code de dessin garde donc ses references habituelles et n'a
rien a savoir du theme courant : il suffit de redemander un rafraichissement.

Le theme clair n'est pas l'inverse du sombre. En plein soleil, l'ecran perd du
contraste et les nuances moyennes disparaissent les premieres : les gris de
texte secondaire sont donc nettement plus fonces qu'un simple miroir ne le
donnerait, et l'accent ambre du cadran VFO, tres lisible sur fond sombre,
tombe a un rapport de 2:1 sur du blanc. Il est remplace par un ambre brule qui
tient au-dessus de 4,5:1.

Toutes les paires texte/fond des palettes sont verifiees par
tests/test_theme.py.
"""

from __future__ import annotations

from typing import Callable

from PyQt6.QtGui import QColor

THEMES = {"dark": "Sombre", "light": "Clair"}
DEFAULT_THEME = "dark"

# --------------------------------------------------------------------------
# Palettes
# --------------------------------------------------------------------------

PALETTES: dict[str, dict[str, str]] = {
    # Chassis d'appareil de mesure, retro-eclairage ambre de cadran VFO.
    "dark": {
        "chassis":    "#131A21",
        "panel":      "#1B242D",
        "line":       "#2C3944",
        "ink":        "#DCE3E8",
        "muted":      "#7C8C99",
        "amber":      "#E8A33D",
        "ok":         "#5FB47E",
        "alert":      "#D9534F",
        "light_sq":   "#E4DCC9",
        "dark_sq":    "#5C7C6F",
        "piece_w":    "#F7F3EA",
        "piece_b":    "#1C2430",
        "dead_w":     "#E8E2D5",
        "dead_b":     "#61748A",
        "dead_w_edge": "#00000070",
        "dead_b_edge": "#101820",
        "piece_edge": "#0000005A",
        "badge":      "#131A21D7",
        "badge_ink":  "#E8A33D",
        "tray":       "#1B242D",
        "on_primary": "#131A21",
    },
    # Papier sous une lumiere forte : fonds tres clairs, encre presque noire,
    # traits reellement visibles plutot que suggeres.
    "light": {
        "chassis":    "#F5F2EA",
        "panel":      "#FFFFFF",
        "line":       "#8F8778",
        "ink":        "#14191E",
        "muted":      "#4E555C",
        "amber":      "#8A4F04",
        "ok":         "#1F6B41",
        "alert":      "#A3211B",
        "light_sq":   "#F2EAD5",
        "dark_sq":    "#5A8271",
        "piece_w":    "#FFFFFF",
        "piece_b":    "#101820",
        "dead_w":     "#FFFFFF",
        "dead_b":     "#1B2A38",
        "dead_w_edge": "#242A30",
        "dead_b_edge": "#00000060",
        # Plateau des prises en ton moyen : sur un fond clair, une piece
        # blanche s'y perdrait completement (1,3:1 mesure), et c'est justement
        # l'information que ce bandeau doit porter.
        "piece_edge": "#000000A0",
        # Pastille sombre a encre claire : sur un fond clair, une pastille
        # claire se confondrait avec les cases ivoire et l'identifiant de la
        # piece deviendrait illisible, alors que c'est l'information que ce
        # projet met en avant.
        "badge":      "#212832F0",
        "badge_ink":  "#F6C87A",
        "tray":       "#8C949B",
        "on_primary": "#FFFFFF",
    },
}

# --------------------------------------------------------------------------
# Couleurs partagees
# --------------------------------------------------------------------------
# Ces objets sont mutes sur place : toute reference existante suit le theme.

C_CHASSIS = QColor()
C_PANEL = QColor()
C_LINE = QColor()
C_INK = QColor()
C_MUTED = QColor()
C_AMBER = QColor()
C_OK = QColor()
C_ALERT = QColor()
C_LIGHT_SQ = QColor()
C_DARK_SQ = QColor()
C_PIECE_W = QColor()
C_PIECE_B = QColor()
C_DEAD_W = QColor()
C_DEAD_B = QColor()
C_DEAD_W_EDGE = QColor()
C_DEAD_B_EDGE = QColor()
C_PIECE_EDGE = QColor()
C_BADGE = QColor()
C_BADGE_INK = QColor()
C_TRAY = QColor()
C_ON_PRIMARY = QColor()

_BINDINGS = {
    "chassis": C_CHASSIS, "panel": C_PANEL, "line": C_LINE, "ink": C_INK,
    "muted": C_MUTED, "amber": C_AMBER, "ok": C_OK, "alert": C_ALERT,
    "light_sq": C_LIGHT_SQ, "dark_sq": C_DARK_SQ, "piece_w": C_PIECE_W,
    "piece_b": C_PIECE_B, "dead_w": C_DEAD_W, "dead_b": C_DEAD_B,
    "dead_w_edge": C_DEAD_W_EDGE, "dead_b_edge": C_DEAD_B_EDGE,
    "piece_edge": C_PIECE_EDGE, "badge": C_BADGE, "badge_ink": C_BADGE_INK,
    "tray": C_TRAY, "on_primary": C_ON_PRIMARY,
}

_current = ""
_listeners: list[Callable[[str], None]] = []


def _parse(value: str) -> QColor:
    """Accepte #RRGGBB et #RRGGBBAA."""
    if len(value) == 9:
        color = QColor(value[:7])
        color.setAlpha(int(value[7:], 16))
        return color
    return QColor(value)


def apply_theme(name: str) -> None:
    global _current
    name = name if name in PALETTES else DEFAULT_THEME
    if name == _current:
        return
    _current = name
    palette = PALETTES[name]
    for key, color in _BINDINGS.items():
        color.setRgba(_parse(palette[key]).rgba())
    for listener in list(_listeners):
        listener(name)


def current_theme() -> str:
    return _current or DEFAULT_THEME


def is_light() -> bool:
    return current_theme() == "light"


def on_theme_changed(callback: Callable[[str], None]) -> None:
    if callback not in _listeners:
        _listeners.append(callback)


def alpha(color: QColor, value: int) -> QColor:
    """Copie teintee, sans toucher a la couleur partagee."""
    out = QColor(color)
    out.setAlpha(value)
    return out


# --------------------------------------------------------------------------
# Reglages dependant du theme
# --------------------------------------------------------------------------

def last_move_alpha() -> int:
    """Voile du dernier coup.

    Plus soutenu en clair : le meme voile pose sur une case ivoire se voit
    beaucoup moins que sur une case sombre.
    """
    return 110 if is_light() else 74


def disabled_ink() -> QColor:
    """Encre d'un controle desactive : lisible, mais nettement en retrait."""
    base = QColor(C_MUTED)
    return base.darker(135) if is_light() else base.darker(150)


def primary_hover() -> QColor:
    """Survol du bouton principal.

    Eclaircir fonctionne sur un accent clair, pas sur un ambre brule pose sur
    du blanc : en theme clair on fonce, sinon le bouton s'effacerait du fond.
    """
    return C_AMBER.lighter(125) if not is_light() else C_AMBER.darker(125)


def hint_alpha() -> int:
    """Opacite des pastilles de destination legale."""
    return 220 if is_light() else 190


def square_number_alpha() -> int:
    """Opacite des numeros de case, renforcee en plein soleil."""
    return 140 if is_light() else 100


apply_theme(DEFAULT_THEME)
