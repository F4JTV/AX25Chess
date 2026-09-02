"""
chess_rules.py - Moteur de regles d'echecs complet, sans dependance externe.

Particularite : chaque piece porte un IDENTIFIANT UNIQUE stable (UID) qui ne
change JAMAIS de toute la partie, meme apres promotion. C'est cet UID qui est
transmis sur l'air en AX.25, accompagne du numero de case de destination.

Numerotation des cases
----------------------
Index interne : 0..63,  index = (rang-1) * 8 + colonne,  a1 = 0, h8 = 63
Numero reseau : 1..64,  numero = index + 1               a1 = 1, h8 = 64

    +----+----+----+----+----+----+----+----+
  8 | 57 | 58 | 59 | 60 | 61 | 62 | 63 | 64 |
  7 | 49 | 50 | 51 | 52 | 53 | 54 | 55 | 56 |
  ...
  1 |  1 |  2 |  3 |  4 |  5 |  6 |  7 |  8 |
    +----+----+----+----+----+----+----+----+
       a    b    c    d    e    f    g    h

Identifiants de pieces (32 UID uniques)
---------------------------------------
    WR1 WN1 WB1 WQ1 WK1 WB2 WN2 WR2   (rangee 1)
    WP1 .. WP8                        (rangee 2)
    BP1 .. BP8                        (rangee 7)
    BR1 BN1 BB1 BQ1 BK1 BB2 BN2 BR2   (rangee 8)

Une promotion conserve l'UID : WP5 promu dame reste "WP5" mais son type
devient 'Q'. L'unicite et la tracabilite sont donc absolues.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .i18n import tr
from typing import Optional

WHITE = "W"
BLACK = "B"

PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING = "P", "N", "B", "R", "Q", "K"

FILES = "abcdefgh"

KNIGHT_DELTAS = [(1, 2), (2, 1), (2, -1), (1, -2), (-1, -2), (-2, -1), (-2, 1), (-1, 2)]
KING_DELTAS = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]
BISHOP_DIRS = [(1, 1), (1, -1), (-1, 1), (-1, -1)]
ROOK_DIRS = [(1, 0), (-1, 0), (0, 1), (0, -1)]

PIECE_NAMES_FR = {
    PAWN: "Pion", KNIGHT: "Cavalier", BISHOP: "Fou",
    ROOK: "Tour", QUEEN: "Dame", KING: "Roi",
}

UNICODE_GLYPHS = {
    (WHITE, KING): "\u2654", (WHITE, QUEEN): "\u2655", (WHITE, ROOK): "\u2656",
    (WHITE, BISHOP): "\u2657", (WHITE, KNIGHT): "\u2658", (WHITE, PAWN): "\u2659",
    (BLACK, KING): "\u265A", (BLACK, QUEEN): "\u265B", (BLACK, ROOK): "\u265C",
    (BLACK, BISHOP): "\u265D", (BLACK, KNIGHT): "\u265E", (BLACK, PAWN): "\u265F",
}


# --------------------------------------------------------------------------
# Utilitaires de cases
# --------------------------------------------------------------------------

def rank_of(sq: int) -> int:
    return sq >> 3


def file_of(sq: int) -> int:
    return sq & 7


def make_sq(file: int, rank: int) -> int:
    return rank * 8 + file


def on_board(file: int, rank: int) -> bool:
    return 0 <= file < 8 and 0 <= rank < 8


def sq_name(sq: int) -> str:
    """Notation algebrique : 0 -> 'a1'."""
    return FILES[file_of(sq)] + str(rank_of(sq) + 1)


def sq_number(sq: int) -> int:
    """Numero de case unique transmis sur l'air : 1..64."""
    return sq + 1


def number_to_sq(num: int) -> int:
    if not 1 <= num <= 64:
        raise ValueError(f"numero de case hors bornes: {num}")
    return num - 1


def name_to_sq(name: str) -> int:
    name = name.strip().lower()
    if len(name) != 2 or name[0] not in FILES or not name[1].isdigit():
        raise ValueError(f"case invalide: {name}")
    return make_sq(FILES.index(name[0]), int(name[1]) - 1)


# --------------------------------------------------------------------------
# Pieces et coups
# --------------------------------------------------------------------------

@dataclass
class Piece:
    uid: str            # identifiant unique, stable toute la partie
    color: str          # 'W' ou 'B'
    kind: str           # 'P','N','B','R','Q','K' (peut changer a la promotion)
    square: int         # 0..63, ou -1 si capturee
    born_kind: str      # type d'origine (utile pour l'affichage/journal)

    @property
    def alive(self) -> bool:
        return self.square >= 0

    @property
    def glyph(self) -> str:
        return UNICODE_GLYPHS[(self.color, self.kind)]


@dataclass
class Move:
    uid: str
    frm: int
    to: int
    promo: Optional[str] = None      # 'Q','R','B','N'
    captured_uid: Optional[str] = None
    captured_sq: int = -1
    is_ep: bool = False
    castle: Optional[str] = None     # 'K' (petit roque) ou 'Q' (grand roque)
    rook_uid: Optional[str] = None
    rook_frm: int = -1
    rook_to: int = -1
    san_text: str = ""

    def key(self) -> tuple:
        return (self.uid, self.to, self.promo)

    def __str__(self) -> str:
        s = f"{self.uid}:{sq_number(self.frm)}->{sq_number(self.to)}"
        if self.promo:
            s += f"={self.promo}"
        return s


@dataclass
class _Undo:
    move: Move
    castling: set = field(default_factory=set)
    ep: Optional[int] = None
    halfmove: int = 0
    fullmove: int = 1
    prev_kind: str = PAWN


# --------------------------------------------------------------------------
# Echiquier
# --------------------------------------------------------------------------

class Board:
    """Etat complet d'une partie, avec suivi des UID."""

    def __init__(self, setup: bool = True):
        self.squares: list[Optional[str]] = [None] * 64   # index -> uid
        self.pieces: dict[str, Piece] = {}
        self.turn: str = WHITE
        self.castling: set[str] = set()                   # 'K','Q','k','q'
        self.ep: Optional[int] = None                     # case de prise en passant
        self.halfmove: int = 0
        self.fullmove: int = 1
        self.stack: list[_Undo] = []
        self.moves: list[Move] = []                       # historique complet
        self.rep: dict[str, int] = {}
        if setup:
            self.set_initial_position()

    # -- installation -------------------------------------------------------

    def _place(self, uid: str, color: str, kind: str, sq: int) -> None:
        self.pieces[uid] = Piece(uid, color, kind, sq, kind)
        self.squares[sq] = uid

    def set_initial_position(self) -> None:
        self.squares = [None] * 64
        self.pieces = {}
        back = [(ROOK, 1), (KNIGHT, 1), (BISHOP, 1), (QUEEN, 1),
                (KING, 1), (BISHOP, 2), (KNIGHT, 2), (ROOK, 2)]
        for f, (kind, idx) in enumerate(back):
            self._place(f"W{kind}{idx}", WHITE, kind, make_sq(f, 0))
            self._place(f"B{kind}{idx}", BLACK, kind, make_sq(f, 7))
        for f in range(8):
            self._place(f"WP{f + 1}", WHITE, PAWN, make_sq(f, 1))
            self._place(f"BP{f + 1}", BLACK, PAWN, make_sq(f, 6))
        self.turn = WHITE
        self.castling = {"K", "Q", "k", "q"}
        self.ep = None
        self.halfmove = 0
        self.fullmove = 1
        self.stack = []
        self.moves = []
        self.rep = {self.position_key(): 1}

    # -- acces --------------------------------------------------------------

    def piece_at(self, sq: int) -> Optional[Piece]:
        uid = self.squares[sq]
        return self.pieces[uid] if uid else None

    def piece(self, uid: str) -> Optional[Piece]:
        return self.pieces.get(uid)

    def king_square(self, color: str) -> int:
        for p in self.pieces.values():
            if p.color == color and p.kind == KING and p.alive:
                return p.square
        return -1

    def captured(self, color: str) -> list[Piece]:
        return [p for p in self.pieces.values() if p.color == color and not p.alive]

    # -- attaques -----------------------------------------------------------

    def is_attacked(self, sq: int, by_color: str) -> bool:
        f0, r0 = file_of(sq), rank_of(sq)

        # pions
        d = -1 if by_color == WHITE else 1     # depuis quelle direction il vient
        for df in (-1, 1):
            f, r = f0 + df, r0 + d
            if on_board(f, r):
                p = self.piece_at(make_sq(f, r))
                if p and p.color == by_color and p.kind == PAWN:
                    return True

        # cavaliers
        for df, dr in KNIGHT_DELTAS:
            f, r = f0 + df, r0 + dr
            if on_board(f, r):
                p = self.piece_at(make_sq(f, r))
                if p and p.color == by_color and p.kind == KNIGHT:
                    return True

        # roi
        for df, dr in KING_DELTAS:
            f, r = f0 + df, r0 + dr
            if on_board(f, r):
                p = self.piece_at(make_sq(f, r))
                if p and p.color == by_color and p.kind == KING:
                    return True

        # glissantes
        for dirs, kinds in ((BISHOP_DIRS, (BISHOP, QUEEN)), (ROOK_DIRS, (ROOK, QUEEN))):
            for df, dr in dirs:
                f, r = f0 + df, r0 + dr
                while on_board(f, r):
                    p = self.piece_at(make_sq(f, r))
                    if p:
                        if p.color == by_color and p.kind in kinds:
                            return True
                        break
                    f += df
                    r += dr
        return False

    def in_check(self, color: Optional[str] = None) -> bool:
        color = color or self.turn
        ks = self.king_square(color)
        return ks >= 0 and self.is_attacked(ks, BLACK if color == WHITE else WHITE)

    # -- generation ---------------------------------------------------------

    def _pseudo_moves(self, color: str) -> list[Move]:
        out: list[Move] = []
        for p in list(self.pieces.values()):
            if not p.alive or p.color != color:
                continue
            f0, r0 = file_of(p.square), rank_of(p.square)

            if p.kind == PAWN:
                step = 1 if color == WHITE else -1
                start_rank = 1 if color == WHITE else 6
                last_rank = 7 if color == WHITE else 0

                r = r0 + step
                if on_board(f0, r) and self.squares[make_sq(f0, r)] is None:
                    self._add_pawn(out, p, make_sq(f0, r), last_rank)
                    r2 = r0 + 2 * step
                    if r0 == start_rank and self.squares[make_sq(f0, r2)] is None:
                        out.append(Move(p.uid, p.square, make_sq(f0, r2)))
                for df in (-1, 1):
                    f, r = f0 + df, r0 + step
                    if not on_board(f, r):
                        continue
                    tsq = make_sq(f, r)
                    tp = self.piece_at(tsq)
                    if tp and tp.color != color:
                        self._add_pawn(out, p, tsq, last_rank, tp.uid, tsq)
                    elif self.ep is not None and tsq == self.ep:
                        cap_sq = make_sq(f, r0)
                        cp = self.piece_at(cap_sq)
                        if cp and cp.color != color and cp.kind == PAWN:
                            out.append(Move(p.uid, p.square, tsq,
                                            captured_uid=cp.uid, captured_sq=cap_sq,
                                            is_ep=True))

            elif p.kind == KNIGHT:
                self._add_steps(out, p, KNIGHT_DELTAS)
            elif p.kind == KING:
                self._add_steps(out, p, KING_DELTAS)
                self._add_castles(out, p)
            else:
                dirs = (BISHOP_DIRS if p.kind == BISHOP else
                        ROOK_DIRS if p.kind == ROOK else BISHOP_DIRS + ROOK_DIRS)
                self._add_slides(out, p, dirs)
        return out

    def _add_pawn(self, out, p, tsq, last_rank, cap_uid=None, cap_sq=-1):
        if rank_of(tsq) == last_rank:
            for promo in (QUEEN, ROOK, BISHOP, KNIGHT):
                out.append(Move(p.uid, p.square, tsq, promo=promo,
                                captured_uid=cap_uid, captured_sq=cap_sq))
        else:
            out.append(Move(p.uid, p.square, tsq,
                            captured_uid=cap_uid, captured_sq=cap_sq))

    def _add_steps(self, out, p, deltas):
        f0, r0 = file_of(p.square), rank_of(p.square)
        for df, dr in deltas:
            f, r = f0 + df, r0 + dr
            if not on_board(f, r):
                continue
            tsq = make_sq(f, r)
            tp = self.piece_at(tsq)
            if tp is None:
                out.append(Move(p.uid, p.square, tsq))
            elif tp.color != p.color:
                out.append(Move(p.uid, p.square, tsq,
                                captured_uid=tp.uid, captured_sq=tsq))

    def _add_slides(self, out, p, dirs):
        f0, r0 = file_of(p.square), rank_of(p.square)
        for df, dr in dirs:
            f, r = f0 + df, r0 + dr
            while on_board(f, r):
                tsq = make_sq(f, r)
                tp = self.piece_at(tsq)
                if tp is None:
                    out.append(Move(p.uid, p.square, tsq))
                else:
                    if tp.color != p.color:
                        out.append(Move(p.uid, p.square, tsq,
                                        captured_uid=tp.uid, captured_sq=tsq))
                    break
                f += df
                r += dr

    def _add_castles(self, out, king):
        color = king.color
        opp = BLACK if color == WHITE else WHITE
        rank = 0 if color == WHITE else 7
        home = make_sq(4, rank)
        if king.square != home:
            return
        if self.is_attacked(home, opp):
            return
        short = "K" if color == WHITE else "k"
        long_ = "Q" if color == WHITE else "q"

        if short in self.castling:
            f_sq, g_sq, h_sq = make_sq(5, rank), make_sq(6, rank), make_sq(7, rank)
            rook = self.piece_at(h_sq)
            if (rook and rook.kind == ROOK and rook.color == color
                    and self.squares[f_sq] is None and self.squares[g_sq] is None
                    and not self.is_attacked(f_sq, opp)
                    and not self.is_attacked(g_sq, opp)):
                out.append(Move(king.uid, home, g_sq, castle="K",
                                rook_uid=rook.uid, rook_frm=h_sq, rook_to=f_sq))

        if long_ in self.castling:
            d_sq, c_sq, b_sq, a_sq = (make_sq(3, rank), make_sq(2, rank),
                                      make_sq(1, rank), make_sq(0, rank))
            rook = self.piece_at(a_sq)
            if (rook and rook.kind == ROOK and rook.color == color
                    and self.squares[d_sq] is None and self.squares[c_sq] is None
                    and self.squares[b_sq] is None
                    and not self.is_attacked(d_sq, opp)
                    and not self.is_attacked(c_sq, opp)):
                out.append(Move(king.uid, home, c_sq, castle="Q",
                                rook_uid=rook.uid, rook_frm=a_sq, rook_to=d_sq))

    def legal_moves(self, color: Optional[str] = None) -> list[Move]:
        color = color or self.turn
        legal = []
        for m in self._pseudo_moves(color):
            self._apply(m)
            if not self.in_check(color):
                legal.append(m)
            self._revert()
        return legal

    def legal_moves_from(self, sq: int) -> list[Move]:
        return [m for m in self.legal_moves() if m.frm == sq]

    def find_move(self, uid: str, to_sq: int, promo: Optional[str] = None) -> Optional[Move]:
        """Retrouve le coup legal correspondant a (UID, case de destination)."""
        cands = [m for m in self.legal_moves() if m.uid == uid and m.to == to_sq]
        if not cands:
            return None
        if promo:
            for m in cands:
                if m.promo == promo:
                    return m
            return None
        return cands[0]

    # -- application --------------------------------------------------------

    def _apply(self, m: Move) -> None:
        p = self.pieces[m.uid]
        undo = _Undo(m, set(self.castling), self.ep, self.halfmove,
                     self.fullmove, p.kind)
        self.stack.append(undo)

        if m.captured_uid:
            cap = self.pieces[m.captured_uid]
            self.squares[cap.square] = None
            cap.square = -1

        self.squares[m.frm] = None
        self.squares[m.to] = p.uid
        p.square = m.to

        if m.promo:
            p.kind = m.promo

        if m.castle:
            rook = self.pieces[m.rook_uid]
            self.squares[m.rook_frm] = None
            self.squares[m.rook_to] = rook.uid
            rook.square = m.rook_to

        # droits de roque
        if p.kind == KING or undo.prev_kind == KING:
            if p.color == WHITE:
                self.castling -= {"K", "Q"}
            else:
                self.castling -= {"k", "q"}
        for sq, right in ((0, "Q"), (7, "K"), (56, "q"), (63, "k")):
            if m.frm == sq or m.to == sq:
                self.castling.discard(right)

        # prise en passant
        self.ep = None
        if undo.prev_kind == PAWN and abs(rank_of(m.to) - rank_of(m.frm)) == 2:
            self.ep = make_sq(file_of(m.frm), (rank_of(m.frm) + rank_of(m.to)) // 2)

        if undo.prev_kind == PAWN or m.captured_uid:
            self.halfmove = 0
        else:
            self.halfmove += 1
        if self.turn == BLACK:
            self.fullmove += 1
        self.turn = BLACK if self.turn == WHITE else WHITE

    def _revert(self) -> None:
        undo = self.stack.pop()
        m = undo.move
        p = self.pieces[m.uid]

        if m.castle:
            rook = self.pieces[m.rook_uid]
            self.squares[m.rook_to] = None
            self.squares[m.rook_frm] = rook.uid
            rook.square = m.rook_frm

        p.kind = undo.prev_kind
        self.squares[m.to] = None
        self.squares[m.frm] = p.uid
        p.square = m.frm

        if m.captured_uid:
            cap = self.pieces[m.captured_uid]
            cap.square = m.captured_sq
            self.squares[m.captured_sq] = cap.uid

        self.castling = undo.castling
        self.ep = undo.ep
        self.halfmove = undo.halfmove
        self.fullmove = undo.fullmove
        self.turn = BLACK if self.turn == WHITE else WHITE

    def push(self, m: Move) -> None:
        """Joue un coup pour de bon (historique + repetitions)."""
        self._apply(m)
        self.moves.append(m)
        key = self.position_key()
        self.rep[key] = self.rep.get(key, 0) + 1

    def pop(self) -> Optional[Move]:
        if not self.moves:
            return None
        key = self.position_key()
        self.rep[key] = max(0, self.rep.get(key, 1) - 1)
        m = self.moves.pop()
        self._revert()
        return m

    # -- etat de la partie --------------------------------------------------

    def position_key(self) -> str:
        """Cle de position (sans compteurs) pour la triple repetition."""
        rows = []
        for sq in range(64):
            uid = self.squares[sq]
            rows.append("." if uid is None else
                        (self.pieces[uid].kind if self.pieces[uid].color == WHITE
                         else self.pieces[uid].kind.lower()))
        return ("".join(rows) + "|" + self.turn + "|"
                + "".join(sorted(self.castling)) + "|"
                + (str(self.ep) if self.ep is not None else "-"))

    def insufficient_material(self) -> bool:
        alive = [p for p in self.pieces.values() if p.alive]
        kinds = sorted(p.kind for p in alive)
        if kinds == [KING, KING]:
            return True
        if len(alive) == 3 and (KNIGHT in kinds or BISHOP in kinds):
            return True
        if len(alive) == 4:
            bishops = [p for p in alive if p.kind == BISHOP]
            if len(bishops) == 2 and bishops[0].color != bishops[1].color:
                c0 = (file_of(bishops[0].square) + rank_of(bishops[0].square)) & 1
                c1 = (file_of(bishops[1].square) + rank_of(bishops[1].square)) & 1
                if c0 == c1:
                    return True
        return False

    def status(self) -> tuple[str, str]:
        """Retourne (code, libelle FR).

        codes : 'playing', 'checkmate', 'stalemate', 'fifty', 'repetition',
                'material'
        """
        if not self.legal_moves():
            if self.in_check():
                return "checkmate", (tr("Echec et mat - les Noirs gagnent")
                                     if self.turn == WHITE
                                     else tr("Echec et mat - les Blancs gagnent"))
            return "stalemate", tr("Pat - partie nulle")
        if self.halfmove >= 100:
            return "fifty", tr("Nulle par la regle des 50 coups")
        if self.rep.get(self.position_key(), 0) >= 3:
            return "repetition", tr("Nulle par triple repetition")
        if self.insufficient_material():
            return "material", tr("Nulle par materiel insuffisant")
        return "playing", tr("Partie en cours")

    # -- FEN (diagnostic / affichage) ---------------------------------------

    def fen(self) -> str:
        rows = []
        for r in range(7, -1, -1):
            empty = 0
            row = ""
            for f in range(8):
                p = self.piece_at(make_sq(f, r))
                if p is None:
                    empty += 1
                else:
                    if empty:
                        row += str(empty)
                        empty = 0
                    row += p.kind if p.color == WHITE else p.kind.lower()
            if empty:
                row += str(empty)
            rows.append(row)
        castle = "".join(c for c in "KQkq" if c in self.castling) or "-"
        ep = sq_name(self.ep) if self.ep is not None else "-"
        return (f"{'/'.join(rows)} {'w' if self.turn == WHITE else 'b'} "
                f"{castle} {ep} {self.halfmove} {self.fullmove}")

    @classmethod
    def from_fen(cls, fen: str) -> "Board":
        """Charge une position. Les UID sont regeneres (usage tests/analyse)."""
        b = cls(setup=False)
        parts = fen.split()
        counters = {}
        rank = 7
        file = 0
        for ch in parts[0]:
            if ch == "/":
                rank -= 1
                file = 0
            elif ch.isdigit():
                file += int(ch)
            else:
                color = WHITE if ch.isupper() else BLACK
                kind = ch.upper()
                k = (color, kind)
                counters[k] = counters.get(k, 0) + 1
                b._place(f"{color}{kind}{counters[k]}", color, kind, make_sq(file, rank))
                file += 1
        b.turn = WHITE if parts[1] == "w" else BLACK
        b.castling = set(parts[2]) if parts[2] != "-" else set()
        b.ep = name_to_sq(parts[3]) if parts[3] != "-" else None
        b.halfmove = int(parts[4]) if len(parts) > 4 else 0
        b.fullmove = int(parts[5]) if len(parts) > 5 else 1
        b.rep = {b.position_key(): 1}
        return b

    # -- notation -----------------------------------------------------------

    def san(self, m: Move) -> str:
        """Notation algebrique abregee, calculee AVANT que le coup soit joue."""
        if m.castle == "K":
            base = "O-O"
        elif m.castle == "Q":
            base = "O-O-O"
        else:
            p = self.pieces[m.uid]
            if p.kind == PAWN:
                base = (FILES[file_of(m.frm)] + "x" if m.captured_uid else "") + sq_name(m.to)
            else:
                disamb = ""
                for other in self.legal_moves(p.color):
                    op = self.pieces[other.uid]
                    if (other.uid != m.uid and other.to == m.to
                            and op.kind == p.kind):
                        if file_of(other.frm) != file_of(m.frm):
                            disamb = FILES[file_of(m.frm)]
                        else:
                            disamb = str(rank_of(m.frm) + 1)
                        break
                base = p.kind + disamb + ("x" if m.captured_uid else "") + sq_name(m.to)
            if m.promo:
                base += "=" + m.promo
        self._apply(m)
        if self.in_check():
            base += "#" if not self.legal_moves() else "+"
        self._revert()
        return base


def perft(board: Board, depth: int) -> int:
    """Compteur de noeuds - sert a valider le moteur."""
    if depth == 0:
        return 1
    total = 0
    for m in board.legal_moves():
        board._apply(m)
        total += perft(board, depth - 1)
        board._revert()
    return total
