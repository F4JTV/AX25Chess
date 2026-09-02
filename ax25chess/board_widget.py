"""
board_widget.py - Echiquier 2D dessine au QPainter.

Parti pris graphique : l'echiquier est traite comme un panneau de brassage
d'atelier radio. Chaque case porte discretement son numero reseau (1 a 64)
et chaque piece porte son identifiant unique estampille comme un numero de
serie sur un materiel. Ce sont exactement les deux grandeurs qui partent sur
l'air : l'interface montre le protocole plutot que de le cacher.
"""

from __future__ import annotations

from typing import Optional

from PyQt6.QtCore import QPointF, QRectF, Qt, pyqtSignal
from PyQt6.QtGui import (QColor, QFont, QFontDatabase, QFontMetrics, QPainter,
                         QPainterPath, QPen, QRadialGradient)
from PyQt6.QtWidgets import (QApplication, QDialog, QHBoxLayout, QLabel,
                             QPushButton, QVBoxLayout, QWidget)

from .i18n import tr
from .chess_rules import (BLACK, FILES, WHITE, Board, file_of, make_sq,
                          rank_of, sq_number)

# Les couleurs viennent de theme.py et sont muettes sur place au changement
# de theme : le code de dessin ci-dessous n'a donc rien a savoir du theme
# courant. Elles sont reexportees pour les modules qui les importent d'ici.
from .theme import (C_ALERT, C_AMBER, C_BADGE, C_BADGE_INK, C_CHASSIS,
                    C_DARK_SQ, C_DEAD_B,
                    C_DEAD_B_EDGE, C_DEAD_W, C_DEAD_W_EDGE, C_INK, C_LIGHT_SQ,
                    C_LINE, C_MUTED, C_OK, C_ON_PRIMARY, C_PANEL, C_PIECE_B,
                    C_PIECE_EDGE, C_PIECE_W, C_TRAY, alpha, hint_alpha,
                    last_move_alpha, square_number_alpha)

# glyphes pleins pour les deux couleurs : rendu homogene quelle que soit la fonte
SOLID = {"K": "\u265A", "Q": "\u265B", "R": "\u265C",
         "B": "\u265D", "N": "\u265E", "P": "\u265F"}

PIECE_VALUES = {"P": 1, "N": 3, "B": 3, "R": 5, "Q": 9, "K": 0}

_CHESS_FONT_CANDIDATES = ["DejaVu Sans", "Segoe UI Symbol", "Noto Sans Symbols 2",
                          "FreeSerif", "Arial Unicode MS", "Symbola",
                          "Apple Symbols"]

_MONO_CANDIDATES = ["DejaVu Sans Mono", "Consolas", "Liberation Mono",
                    "Menlo", "Cascadia Mono", "Courier New"]

_cached_chess_font: Optional[str] = None
_cached_mono_font: Optional[str] = None


def _has_chess_glyphs(family: str) -> bool:
    """Verifie que la police contient reellement les pieces d'echecs.

    Indispensable : QPainterPath.addText() ne fait aucune substitution de
    glyphe, contrairement au dessin de texte ordinaire. Une police sans ces
    caracteres ne produirait pas un carre blanc mais un chemin VIDE, donc des
    pieces purement invisibles.
    """
    metrics = QFontMetrics(QFont(family, 24))
    return all(metrics.inFont(glyph) for glyph in SOLID.values())


def pick_chess_font() -> str:
    """Police portant les pieces d'echecs, verifiee glyphe par glyphe."""
    global _cached_chess_font
    if _cached_chess_font is not None:
        return _cached_chess_font

    families = set(QFontDatabase.families())
    for name in _CHESS_FONT_CANDIDATES:
        if name in families and _has_chess_glyphs(name):
            _cached_chess_font = name
            return name

    # Aucun candidat connu : on balaie ce qui est installe plutot que de
    # rendre un echiquier vide.
    for name in sorted(families):
        if _has_chess_glyphs(name):
            _cached_chess_font = name
            return name

    _cached_chess_font = QApplication.font().family()
    return _cached_chess_font


def mono_family() -> str:
    """Police a chasse fixe reellement disponible.

    « monospace » est un alias fontconfig : il se resout sous Linux, pas sous
    Windows, ou Qt retomberait sur une police proportionnelle et ruinerait
    l'alignement de tout l'affichage technique.
    """
    global _cached_mono_font
    if _cached_mono_font is not None:
        return _cached_mono_font

    families = set(QFontDatabase.families())
    for name in _MONO_CANDIDATES:
        if name in families:
            _cached_mono_font = name
            return name
    _cached_mono_font = QFontDatabase.systemFont(
        QFontDatabase.SystemFont.FixedFont).family()
    return _cached_mono_font


class PromotionDialog(QDialog):
    """Choix de la piece de promotion."""

    def __init__(self, color: str, font_family: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(tr("Promotion"))
        self.choice: Optional[str] = None
        self.setStyleSheet(
            f"QDialog{{background:{C_PANEL.name()};}}"
            f"QLabel{{color:{C_INK.name()};font-size:12px;}}")
        lay = QVBoxLayout(self)
        lay.addWidget(QLabel(tr("Choisissez la piece promue :")))
        row = QHBoxLayout()
        ink = C_PIECE_W if color == WHITE else C_PIECE_B
        for kind in ("Q", "R", "B", "N"):
            btn = QPushButton(SOLID[kind])
            btn.setFont(QFont(font_family, 26))
            btn.setFixedSize(58, 58)
            btn.setStyleSheet(
                f"QPushButton{{background:{C_LIGHT_SQ.name()};color:{ink.name()};"
                f"border:1px solid {C_LINE.name()};border-radius:6px;}}"
                f"QPushButton:hover{{background:{C_AMBER.name()};}}")
            btn.clicked.connect(lambda _, k=kind: self._pick(k))
            row.addWidget(btn)
        lay.addLayout(row)

    def _pick(self, kind: str) -> None:
        self.choice = kind
        self.accept()


class BoardWidget(QWidget):
    """Echiquier interactif. Selection par clic puis clic sur la destination."""

    move_requested = pyqtSignal(str, int, str)   # uid, case (0..63), promo|""
    square_hovered = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.board = Board()
        self.orientation = WHITE           # couleur en bas
        self.interactive = False
        self.show_uids = True
        self.show_numbers = True
        self.selected: Optional[int] = None
        self.targets: dict[int, list] = {}
        self.last_move: Optional[tuple[int, int]] = None
        self.hover: Optional[int] = None
        self.font_family = pick_chess_font()
        self.setMinimumSize(420, 420)
        self.setMouseTracking(True)

    # -- geometrie ----------------------------------------------------------

    def _metrics(self):
        margin = max(18, int(min(self.width(), self.height()) * 0.045))
        side = min(self.width(), self.height()) - 2 * margin
        side -= side % 8
        ox = (self.width() - side) // 2
        oy = (self.height() - side) // 2
        return ox, oy, side // 8, margin

    def _square_rect(self, sq: int) -> QRectF:
        ox, oy, cell, _ = self._metrics()
        f, r = file_of(sq), rank_of(sq)
        col = f if self.orientation == WHITE else 7 - f
        row = 7 - r if self.orientation == WHITE else r
        return QRectF(ox + col * cell, oy + row * cell, cell, cell)

    def _square_at(self, x: float, y: float) -> Optional[int]:
        ox, oy, cell, _ = self._metrics()
        col = int((x - ox) // cell)
        row = int((y - oy) // cell)
        if not (0 <= col < 8 and 0 <= row < 8):
            return None
        f = col if self.orientation == WHITE else 7 - col
        r = 7 - row if self.orientation == WHITE else row
        return make_sq(f, r)

    # -- etat ---------------------------------------------------------------

    def set_board(self, board: Board) -> None:
        self.board = board
        self.selected = None
        self.targets = {}
        self.update()

    def set_last_move(self, frm: int, to: int) -> None:
        self.last_move = (frm, to)
        self.update()

    def clear_selection(self) -> None:
        self.selected = None
        self.targets = {}
        self.update()

    # -- interaction --------------------------------------------------------

    def mouseMoveEvent(self, ev) -> None:
        sq = self._square_at(ev.position().x(), ev.position().y())
        if sq != self.hover:
            self.hover = sq
            if sq is not None:
                self.square_hovered.emit(sq)
            self.update()

    def leaveEvent(self, ev) -> None:
        self.hover = None
        self.update()

    def mousePressEvent(self, ev) -> None:
        if not self.interactive or ev.button() != Qt.MouseButton.LeftButton:
            return
        sq = self._square_at(ev.position().x(), ev.position().y())
        if sq is None:
            return

        if self.selected is not None and sq in self.targets:
            moves = self.targets[sq]
            promo = ""
            if any(m.promo for m in moves):
                piece = self.board.piece_at(self.selected)
                dlg = PromotionDialog(piece.color, self.font_family, self)
                if dlg.exec() != QDialog.DialogCode.Accepted or not dlg.choice:
                    return
                promo = dlg.choice
            uid = self.board.squares[self.selected]
            self.clear_selection()
            self.move_requested.emit(uid, sq, promo)
            return

        piece = self.board.piece_at(sq)
        if piece and piece.color == self.board.turn:
            self.selected = sq
            self.targets = {}
            for m in self.board.legal_moves_from(sq):
                self.targets.setdefault(m.to, []).append(m)
        else:
            self.clear_selection()
        self.update()

    # -- rendu --------------------------------------------------------------

    def paintEvent(self, ev) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), C_CHASSIS)

        ox, oy, cell, margin = self._metrics()
        side = cell * 8

        # liseré du panneau
        p.setPen(QPen(C_LINE, 1))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawRoundedRect(QRectF(ox - 7, oy - 7, side + 14, side + 14), 4, 4)

        check_sq = -1
        if self.board.in_check():
            check_sq = self.board.king_square(self.board.turn)

        for sq in range(64):
            r = self._square_rect(sq)
            light = (file_of(sq) + rank_of(sq)) % 2 == 1
            p.fillRect(r, C_LIGHT_SQ if light else C_DARK_SQ)

            if self.last_move and sq in self.last_move:
                p.fillRect(r, alpha(C_AMBER, last_move_alpha()))

            if sq == check_sq:
                g = QRadialGradient(r.center(), r.width() * 0.62)
                g.setColorAt(0.0, alpha(C_ALERT, 205))
                g.setColorAt(1.0, alpha(C_ALERT, 0))
                p.fillRect(r, g)

            if sq == self.selected:
                p.setPen(QPen(C_AMBER, max(2.0, cell * 0.045)))
                p.setBrush(Qt.BrushStyle.NoBrush)
                p.drawRect(r.adjusted(1.5, 1.5, -1.5, -1.5))

            if self.show_numbers:
                shade = square_number_alpha()
                p.setPen(QColor(0, 0, 0, shade) if light
                         else QColor(255, 255, 255, min(255, shade + 10)))
                f = QFont(mono_family(), max(6, int(cell * 0.13)))
                p.setFont(f)
                p.drawText(r.adjusted(0, 2, -4, 0),
                           Qt.AlignmentFlag.AlignTop | Qt.AlignmentFlag.AlignRight,
                           str(sq_number(sq)))

        # pieces
        for sq in range(64):
            piece = self.board.piece_at(sq)
            if piece is None:
                continue
            r = self._square_rect(sq)
            self._draw_piece(p, r, piece, cell)

        # destinations legales, dessinees par-dessus les pieces
        for sq, moves in self.targets.items():
            r = self._square_rect(sq)
            capture = any(m.captured_uid for m in moves)
            p.setBrush(Qt.BrushStyle.NoBrush)
            if capture:
                p.setPen(QPen(alpha(C_AMBER, 235), max(2.5, cell * 0.06)))
                p.drawEllipse(r.center(), cell * 0.42, cell * 0.42)
            else:
                p.setPen(Qt.PenStyle.NoPen)
                p.setBrush(alpha(C_AMBER, hint_alpha()))
                p.drawEllipse(r.center(), cell * 0.13, cell * 0.13)

        if self.hover is not None and self.interactive:
            p.setPen(QPen(alpha(C_AMBER, 150), 1.5))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRect(self._square_rect(self.hover).adjusted(1, 1, -1, -1))

        # coordonnees
        p.setPen(C_MUTED)
        p.setFont(QFont(mono_family(), max(7, int(cell * 0.2))))
        for i in range(8):
            f = i if self.orientation == WHITE else 7 - i
            r = 7 - i if self.orientation == WHITE else i
            p.drawText(QRectF(ox + i * cell, oy + side + 4, cell, margin - 4),
                       Qt.AlignmentFlag.AlignCenter, FILES[f])
            p.drawText(QRectF(ox - margin, oy + i * cell, margin - 6, cell),
                       Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignRight,
                       str(r + 1))
        p.end()

    def _draw_piece(self, p: QPainter, r: QRectF, piece, cell: int) -> None:
        glyph = SOLID[piece.kind]
        font = QFont(self.font_family, int(cell * 0.62))
        path = QPainterPath()
        fm_off = QPointF(r.center().x(), r.center().y() + cell * 0.30)
        path.addText(fm_off, font, glyph)
        br = path.boundingRect()
        path.translate(r.center().x() - br.center().x(),
                       r.center().y() - br.center().y())

        # En plein soleil, c'est ce trait qui detache une piece blanche d'une
        # case claire : le theme clair l'epaissit et le fonce.
        p.setPen(QPen(C_PIECE_EDGE, max(1.0, cell * 0.035)))
        p.setBrush(C_PIECE_W if piece.color == WHITE else C_PIECE_B)
        p.drawPath(path)

        if self.show_uids:
            bw, bh = cell * 0.46, cell * 0.20
            badge = QRectF(r.left() + cell * 0.05, r.bottom() - bh - cell * 0.05, bw, bh)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(C_BADGE)
            p.drawRoundedRect(badge, 3, 3)
            p.setPen(C_BADGE_INK)
            p.setFont(QFont(mono_family(), max(6, int(cell * 0.125)),
                            QFont.Weight.DemiBold))
            p.drawText(badge, Qt.AlignmentFlag.AlignCenter, piece.uid)


class CapturesBar(QWidget):
    """Bandeau des pieces capturees, avec leur identifiant.

    Les pieces y sont posees sur un plateau legerement plus clair que le
    chassis, et dessinees avec une encre dediee : sur fond sombre, l'encre
    normale des Noirs serait indiscernable du fond.
    """

    def __init__(self, color: str, parent=None):
        super().__init__(parent)
        self.color = color
        self.board: Optional[Board] = None
        self.font_family = pick_chess_font()
        self.setFixedHeight(50)
        side = tr("blanches") if color == WHITE else tr("noires")
        winner = tr("Noirs") if color == WHITE else tr("Blancs")
        self.setToolTip(tr("Pieces {side} capturees, avec leur identifiant. L'ecart affiche "
                           "a droite est l'avantage materiel des {winner}.",
                        side=side, winner=winner))

    def set_board(self, board: Board) -> None:
        self.board = board
        self.update()

    def _material_edge(self) -> int:
        """Avantage materiel du camp qui a pris ces pieces."""
        if self.board is None:
            return 0
        mine = sum(PIECE_VALUES.get(p.kind, 0)
                   for p in self.board.captured(self.color))
        other = BLACK if self.color == WHITE else WHITE
        theirs = sum(PIECE_VALUES.get(p.kind, 0)
                     for p in self.board.captured(other))
        return mine - theirs

    def paintEvent(self, ev) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.fillRect(self.rect(), C_CHASSIS)
        if self.board is None:
            p.end()
            return

        taken = self.board.captured(self.color)
        label = tr("PRISES BLANCHES") if self.color == WHITE else tr("PRISES NOIRES")
        p.setPen(C_MUTED)
        p.setFont(QFont(mono_family(), 7))
        p.drawText(QRectF(6, 5, 150, 12),
                   Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
                   f"{label}  ({len(taken)})")
        if not taken:
            p.end()
            return

        step = 32.0
        width = len(taken) * step
        x = max(126.0, (self.width() - width) / 2)

        # plateau : sans lui, les pieces noires se fondraient dans le chassis
        tray = QRectF(x - 8, 3, width + 16, self.height() - 6)
        p.setPen(QPen(C_LINE, 1))
        p.setBrush(C_TRAY)
        p.drawRoundedRect(tray, 5, 5)

        ink = C_DEAD_W if self.color == WHITE else C_DEAD_B
        outline = C_DEAD_W_EDGE if self.color == WHITE else C_DEAD_B_EDGE
        for piece in taken:
            path = QPainterPath()
            path.addText(QPointF(x, 28), QFont(self.font_family, 18),
                         SOLID[piece.kind])
            p.setPen(QPen(outline, 1.2))
            p.setBrush(ink)
            p.drawPath(path)
            p.setPen(C_MUTED)
            p.setFont(QFont(mono_family(), 7))
            p.drawText(QRectF(x - 3, 32, 32, 12),
                       Qt.AlignmentFlag.AlignLeft, piece.uid)
            x += step

        edge = self._material_edge()
        if edge > 0:
            p.setPen(C_AMBER)
            p.setFont(QFont(mono_family(), 9, QFont.Weight.DemiBold))
            p.drawText(QRectF(tray.right() + 8, 3, 46, self.height() - 6),
                       Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
                       f"+{edge}")
        p.end()
