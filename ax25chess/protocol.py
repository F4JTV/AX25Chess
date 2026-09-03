"""
protocol.py - Protocole applicatif "CHS-1" transporte par trames AX.25 UI.

Le canal radio est lent, semi-duplex et non fiable : le protocole est donc
concu en "stop-and-wait" (une seule trame fiable en vol a la fois), avec
acquittement explicite, retransmission temporisee et detection immediate de
desynchronisation par empreinte de position.

Format de trame (texte ASCII, lisible dans un moniteur AGWPE / Direwolf)

    CHS1|<gid>|<src>|<dst>|<seq>|<TYPE>|<payload>|<crc>

    gid      identifiant de partie, 4 hex
    src      indicatif de l'expediteur          <- exigence : l'indicatif voyage
    dst      indicatif du destinataire
    seq      compteur de trames de l'expediteur, 0..9999
    TYPE     HELLO ACPT MOVE ACK  SREQ SYNC RSGN DRWO DRWA DRWD CHAT PING PONG
    payload  champs separes par ';'
    crc      CRC16-CCITT (4 hex) de tout ce qui precede

Charge utile d'un coup (le coeur du sujet)

    MOVE|<UID>;<case_depart>;<case_arrivee>;<promo>;<ply>;<empreinte>

    exemple :  CHS1|3F1A|N0CALL|N0CALL-2|7|MOVE|WP5;13;29;-;0;A34F|725A
               -> l'operateur N0CALL deplace la piece WP5 (pion e2)
                  de la case 13 vers la case 29 (e2-e4), demi-coup 0,
                  empreinte de la position resultante A34F

L'empreinte est le CRC16 de la position obtenue APRES le coup. Le receveur
joue le coup de son cote et compare : toute divergence est detectee au
demi-coup pres, pas dix coups plus tard.
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field
from typing import Callable, Optional

from .i18n import tr
from .chess_rules import (BLACK, WHITE, Board, Move, number_to_sq, sq_number)

PROTO = "CHS1"
FIELD_SEP = "|"
SUB_SEP = ";"

# types de trames
T_HELLO, T_ACPT, T_MOVE, T_ACK = "HELLO", "ACPT", "MOVE", "ACK"
T_SREQ, T_SYNC, T_RSGN = "SREQ", "SYNC", "RSGN"
T_DRWO, T_DRWA, T_DRWD = "DRWO", "DRWA", "DRWD"
T_CHAT, T_PING, T_PONG = "CHAT", "PING", "PONG"

RELIABLE = {T_HELLO, T_ACPT, T_MOVE, T_RSGN, T_DRWO, T_DRWA, T_DRWD, T_CHAT}

# temporisations par defaut, calibrees pour du 1200 bauds VHF
RETRY_SECONDS = 14.0
RETRY_JITTER = 3.0
MAX_ATTEMPTS = 6
SYNC_CHUNK_MOVES = 16


# --------------------------------------------------------------------------
# CRC16-CCITT
# --------------------------------------------------------------------------

def crc16(data: bytes | str) -> int:
    if isinstance(data, str):
        data = data.encode("ascii", "replace")
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def position_hash(board: Board) -> str:
    """Empreinte 4 hex de la position courante (avec les compteurs)."""
    key = f"{board.position_key()}|{board.halfmove}|{len(board.moves)}"
    return f"{crc16(key):04X}"


# --------------------------------------------------------------------------
# Trames
# --------------------------------------------------------------------------

@dataclass
class Frame:
    gid: str
    src: str
    dst: str
    seq: int
    type: str
    payload: str = ""

    def body(self) -> str:
        return FIELD_SEP.join([PROTO, self.gid, self.src, self.dst,
                               str(self.seq), self.type, self.payload])

    def encode(self) -> bytes:
        body = self.body()
        return f"{body}{FIELD_SEP}{crc16(body):04X}".encode("ascii", "replace")

    def text(self) -> str:
        return self.encode().decode("ascii", "replace")

    @property
    def fields(self) -> list[str]:
        return self.payload.split(SUB_SEP) if self.payload else []


def parse_frame(data: bytes | str) -> Optional[Frame]:
    """Decode une trame CHS1. Retourne None si invalide ou CRC faux."""
    if isinstance(data, bytes):
        try:
            data = data.decode("ascii", "strict")
        except UnicodeDecodeError:
            return None
    data = data.strip()
    if not data.startswith(PROTO + FIELD_SEP):
        return None
    parts = data.split(FIELD_SEP)
    if len(parts) < 8:
        return None
    crc_txt = parts[-1]
    body = FIELD_SEP.join(parts[:-1])
    if f"{crc16(body):04X}" != crc_txt.upper():
        return None
    payload = FIELD_SEP.join(parts[6:-1])   # tolere un '|' dans un CHAT
    try:
        seq = int(parts[4])
    except ValueError:
        return None
    return Frame(gid=parts[1], src=parts[2], dst=parts[3], seq=seq,
                 type=parts[5], payload=payload)


def encode_move(m: Move, ply: int, poshash: str) -> str:
    return SUB_SEP.join([m.uid, str(sq_number(m.frm)), str(sq_number(m.to)),
                         m.promo or "-", str(ply), poshash])


def compact_move(m: Move) -> str:
    """Forme ultra-courte pour l'historique de resynchronisation."""
    return f"{m.uid}>{sq_number(m.to)}" + (f"={m.promo}" if m.promo else "")


def parse_compact(txt: str) -> tuple[str, int, Optional[str]]:
    promo = None
    if "=" in txt:
        txt, promo = txt.split("=", 1)
    uid, num = txt.split(">", 1)
    return uid, int(num), promo or None


# --------------------------------------------------------------------------
# Evenements remontes a l'interface
# --------------------------------------------------------------------------

@dataclass
class Outbound:
    frame: Frame
    attempts: int = 0
    next_try: float = 0.0
    reliable: bool = True


class SessionListener:
    """A surcharger (ou a brancher sur des signaux Qt)."""

    def on_send(self, frame: Frame) -> None: ...
    def on_log(self, level: str, text: str) -> None: ...
    def on_state(self) -> None: ...
    def on_move_applied(self, move: Move, by_peer: bool) -> None: ...
    def on_chat(self, who: str, text: str) -> None: ...
    def on_game_over(self, code: str, text: str) -> None: ...
    def on_draw_offer(self) -> None: ...


# --------------------------------------------------------------------------
# Session de jeu
# --------------------------------------------------------------------------

class GameSession:
    """Machine a etats de la partie, independante du transport et de l'IHM.

    Le transport appelle `feed(info_bytes, src)`; une horloge appelle
    `tick(now)` toutes les secondes pour gerer les retransmissions.
    """

    IDLE, HANDSHAKE, PLAYING, OVER = "idle", "handshake", "playing", "over"

    def __init__(self, my_call: str, peer_call: str,
                 listener: SessionListener, rng: random.Random | None = None):
        self.my_call = my_call.upper()
        self.peer_call = peer_call.upper()
        self.listener = listener
        self.rng = rng or random.Random()

        self.gid = "0000"
        self.state = self.IDLE
        self.my_color: Optional[str] = None
        self.board = Board()
        self.seq = 0
        self.my_nonce = 0
        self.peer_nonce: Optional[int] = None

        self.pending: Optional[Outbound] = None
        self.queue: list[Outbound] = []
        self.seen: dict[int, str] = {}          # seq recu -> type (anti-doublon)
        self.result: Optional[str] = None
        self.draw_offered_by_peer = False
        self.draw_offered_by_me = False
        self.sync_parts: dict[int, str] = {}
        self.sync_total = 0
        self.last_rx_ok = True

    # -- helpers ------------------------------------------------------------

    def _next_seq(self) -> int:
        self.seq = (self.seq + 1) % 10000
        return self.seq

    def _mk(self, type_: str, payload: str = "") -> Frame:
        return Frame(self.gid, self.my_call, self.peer_call,
                     self._next_seq(), type_, payload)

    def _send(self, type_: str, payload: str = "", reliable: Optional[bool] = None,
              now: float = 0.0) -> Frame:
        f = self._mk(type_, payload)
        rel = RELIABLE.__contains__(type_) if reliable is None else reliable
        ob = Outbound(f, reliable=rel)
        if rel:
            if self.pending is None:
                self.pending = ob
                self._transmit(ob, now)
            else:
                self.queue.append(ob)
        else:
            self.listener.on_send(f)
        return f

    def _transmit(self, ob: Outbound, now: float) -> None:
        ob.attempts += 1
        ob.next_try = now + RETRY_SECONDS + self.rng.uniform(0, RETRY_JITTER)
        self.listener.on_send(ob.frame)
        if ob.attempts > 1:
            self.listener.on_log("warn",
                                 tr("Retransmission {type} seq={seq} (tentative {n}/{total})",
                                    type=ob.frame.type, seq=ob.frame.seq,
                                    n=ob.attempts, total=MAX_ATTEMPTS))

    def _pump(self, now: float) -> None:
        if self.pending is None and self.queue:
            self.pending = self.queue.pop(0)
            self._transmit(self.pending, now)

    def _ack_done(self, seq: int, now: float) -> None:
        if self.pending and self.pending.frame.seq == seq:
            self.pending = None
            self._pump(now)

    # -- api publique -------------------------------------------------------

    def invite(self, now: float = 0.0) -> None:
        """Lance une partie : envoie HELLO avec un nonce aleatoire."""
        self.gid = f"{self.rng.getrandbits(16):04X}"
        self.my_nonce = self.rng.getrandbits(32)
        self.state = self.HANDSHAKE
        self.board = Board()
        self.my_color = None
        self._send(T_HELLO, SUB_SEP.join([f"{self.my_nonce:08X}", "1"]), now=now)
        self.listener.on_log("info", tr("Invitation envoyee (partie {gid})", gid=self.gid))
        self.listener.on_state()

    def my_turn(self) -> bool:
        return (self.state == self.PLAYING and self.my_color is not None
                and self.board.turn == self.my_color and self.pending is None)

    def play_local(self, uid: str, to_sq: int, promo: Optional[str] = None,
                   now: float = 0.0) -> bool:
        """Joue un coup local et le transmet. Retourne False si illegal."""
        if self.state != self.PLAYING or self.board.turn != self.my_color:
            self.listener.on_log("err", tr("Ce n'est pas votre trait."))
            return False
        if self.pending is not None:
            self.listener.on_log("err", tr("Coup precedent non acquitte, patientez."))
            return False
        m = self.board.find_move(uid, to_sq, promo)
        if m is None:
            self.listener.on_log("err",
                                 tr("Coup illegal : {uid} vers la case {case}", uid=uid,
                                 case=sq_number(to_sq)))
            return False
        ply = len(self.board.moves)
        san = self.board.san(m)
        m.san_text = san
        self.board.push(m)
        h = position_hash(self.board)
        self._send(T_MOVE, encode_move(m, ply, h), now=now)
        self.listener.on_log("tx",
                             tr("{call} joue {san}  [{uid} -> case {case}]", call=self.my_call,
                                san=san, uid=uid, case=sq_number(to_sq)))
        self.listener.on_move_applied(m, by_peer=False)
        self._check_end()
        self.listener.on_state()
        return True

    def resign(self, now: float = 0.0) -> None:
        if self.state != self.PLAYING:
            return
        self._send(T_RSGN, now=now)
        self._finish("resign", tr("{call} abandonne", call=self.my_call))

    def offer_draw(self, now: float = 0.0) -> None:
        if self.state == self.PLAYING and not self.draw_offered_by_me:
            self.draw_offered_by_me = True
            self._send(T_DRWO, now=now)
            self.listener.on_log("info", tr("Proposition de nulle envoyee"))

    def answer_draw(self, accept: bool, now: float = 0.0) -> None:
        if not self.draw_offered_by_peer:
            return
        self.draw_offered_by_peer = False
        self._send(T_DRWA if accept else T_DRWD, now=now)
        if accept:
            self._finish("draw", tr("Nulle par accord mutuel"))
        else:
            self.listener.on_log("info", tr("Proposition de nulle declinee"))
        self.listener.on_state()

    def send_chat(self, text: str, now: float = 0.0) -> None:
        self._send(T_CHAT, text[:180].replace(FIELD_SEP, "/"), now=now)
        self.listener.on_chat(self.my_call, text)

    def request_sync(self, now: float = 0.0) -> None:
        self.sync_parts.clear()
        self.sync_total = 0
        self._send(T_SREQ, str(len(self.board.moves)), reliable=False, now=now)
        self.listener.on_log("info", tr("Demande de resynchronisation envoyee"))

    # -- horloge ------------------------------------------------------------

    def tick(self, now: float) -> None:
        ob = self.pending
        if ob is None:
            self._pump(now)
            return
        if now < ob.next_try:
            return
        if ob.attempts >= MAX_ATTEMPTS:
            self.listener.on_log("err",
                                 tr("Aucun acquittement pour {type} apres {n} tentatives - "
                                    "liaison perdue ?", type=ob.frame.type,
                                    n=MAX_ATTEMPTS))
            ob.attempts = 0
            ob.next_try = now + RETRY_SECONDS * 4
            return
        self._transmit(ob, now)

    # -- reception ----------------------------------------------------------

    def feed(self, info: bytes, src_from_ax25: str = "", now: float = 0.0) -> None:
        f = parse_frame(info)
        if f is None:
            self.last_rx_ok = False
            self.listener.on_log("warn", tr("Trame ignoree (CRC ou format invalide)"))
            return
        if f.dst.upper() not in (self.my_call, "ALL", "CQ"):
            return
        if f.src.upper() != self.peer_call:
            self.listener.on_log("warn",
                                 tr("Trame recue de {src}, correspondant attendu {peer} - ignoree",
                                    src=f.src, peer=self.peer_call))
            return
        if self.state in (self.PLAYING, self.HANDSHAKE) and f.gid != self.gid \
                and f.type not in (T_HELLO,):
            self.listener.on_log("warn", tr("Trame d'une autre partie ({gid}) ignoree", gid=f.gid))
            return

        self.last_rx_ok = True
        self.listener.on_log("rx", f"< {f.text()}")

        # anti-doublon : on reacquitte sans rejouer
        if f.type in RELIABLE and self.seen.get(f.seq) == f.type:
            self._send(T_ACK, SUB_SEP.join([str(f.seq), position_hash(self.board)]),
                       reliable=False, now=now)
            return
        if f.type in RELIABLE:
            self.seen[f.seq] = f.type
            if len(self.seen) > 200:
                for k in sorted(self.seen)[:100]:
                    self.seen.pop(k, None)

        handler = getattr(self, f"_rx_{f.type.lower()}", None)
        if handler is None:
            self.listener.on_log("warn", tr("Type de trame inconnu : {type}", type=f.type))
            return
        handler(f, now)
        self.listener.on_state()

    def _ack(self, f: Frame, now: float) -> None:
        self._send(T_ACK, SUB_SEP.join([str(f.seq), position_hash(self.board)]),
                   reliable=False, now=now)

    # -- handlers -----------------------------------------------------------

    def _rx_hello(self, f: Frame, now: float) -> None:
        fields = f.fields
        if not fields:
            return
        self.peer_nonce = int(fields[0], 16)
        self.gid = f.gid
        if self.my_nonce == 0:
            self.my_nonce = self.rng.getrandbits(32)
        self.board = Board()
        self.state = self.HANDSHAKE
        self._assign_colors()
        self._ack(f, now)
        self._send(T_ACPT, SUB_SEP.join([f"{self.my_nonce:08X}", self.my_color]),
                   now=now)
        self.state = self.PLAYING
        self.listener.on_log("info",
                             tr("Partie {gid} acceptee - vous jouez les {colour}", gid=self.gid,
                                colour=tr("Blancs") if self.my_color == WHITE
                                else tr("Noirs")))

    def _rx_acpt(self, f: Frame, now: float) -> None:
        fields = f.fields
        if not fields:
            return
        self.peer_nonce = int(fields[0], 16)
        self._assign_colors()
        peer_claim = fields[1] if len(fields) > 1 else None
        if peer_claim and peer_claim == self.my_color:
            self.listener.on_log("err",
                                 tr("Conflit d'attribution des couleurs - relancez l'invitation"))
            self._ack(f, now)
            return
        self._ack(f, now)
        self.state = self.PLAYING
        self.listener.on_log("info",
                             tr("Partie {gid} engagee - vous jouez les {colour}", gid=self.gid,
                                colour=tr("Blancs") if self.my_color == WHITE
                                else tr("Noirs")))

    def _assign_colors(self) -> None:
        """Attribution deterministe : le plus grand nonce prend les Blancs.
        Egalite improbable tranchee par l'ordre alphabetique des indicatifs."""
        if self.peer_nonce is None:
            return
        if self.my_nonce > self.peer_nonce:
            self.my_color = WHITE
        elif self.my_nonce < self.peer_nonce:
            self.my_color = BLACK
        else:
            self.my_color = WHITE if self.my_call < self.peer_call else BLACK

    def _rx_move(self, f: Frame, now: float) -> None:
        fields = f.fields
        if len(fields) < 6:
            return
        uid, frm_num, to_num, promo, ply_txt, their_hash = fields[:6]
        promo = None if promo == "-" else promo
        try:
            to_sq = number_to_sq(int(to_num))
            frm_sq = number_to_sq(int(frm_num))
            ply = int(ply_txt)
        except ValueError:
            self.listener.on_log("err", tr("Coup recu illisible"))
            return

        if self.state != self.PLAYING:
            self.listener.on_log("warn", tr("Coup recu hors partie - ignore"))
            return

        expected = len(self.board.moves)
        if ply < expected:
            self.listener.on_log("info", tr("Coup {ply} deja connu - reacquitte", ply=ply))
            self._ack(f, now)
            return
        if ply > expected:
            self.listener.on_log("err",
                                 tr("Trou dans la sequence (recu {got}, attendu {want})", got=ply,
                                 want=expected))
            self.request_sync(now)
            return
        if self.board.turn == self.my_color:
            self.listener.on_log("err", tr("Coup recu alors que c'est votre trait - desync"))
            self.request_sync(now)
            return

        m = self.board.find_move(uid, to_sq, promo)
        if m is None or m.frm != frm_sq:
            self.listener.on_log(
                "err", tr("Coup illegal recu : {uid} case {case} - resynchronisation",
                          uid=uid, case=to_num))
            self.request_sync(now)
            return

        san = self.board.san(m)
        m.san_text = san
        self.board.push(m)
        mine = position_hash(self.board)
        if mine != their_hash.upper():
            self.board.pop()
            self.listener.on_log(
                "err", tr("Empreinte divergente ({mine} != {theirs}) - resync",
                          mine=mine, theirs=their_hash))
            self.request_sync(now)
            return

        self._ack(f, now)
        self.listener.on_log("rx",
                             tr("{src} joue {san}  [{uid} -> case {case}]", src=f.src, san=san,
                                uid=uid, case=to_num))
        self.listener.on_move_applied(m, by_peer=True)
        self._check_end()

    def _rx_ack(self, f: Frame, now: float) -> None:
        fields = f.fields
        if not fields:
            return
        try:
            acked = int(fields[0])
        except ValueError:
            return
        self._ack_done(acked, now)
        if len(fields) > 1 and self.state == self.PLAYING:
            theirs = fields[1].upper()
            mine = position_hash(self.board)
            if theirs != mine and self.board.turn != self.my_color:
                self.listener.on_log("warn",
                                     tr("Empreinte du correspondant differente ({theirs} != {mine})",
                                        theirs=theirs, mine=mine))

    def _rx_sreq(self, f: Frame, now: float) -> None:
        self.listener.on_log("info", tr("Le correspondant demande une resynchronisation"))
        moves = [compact_move(m) for m in self.board.moves]
        chunks = [moves[i:i + SYNC_CHUNK_MOVES]
                  for i in range(0, len(moves), SYNC_CHUNK_MOVES)] or [[]]
        total = len(chunks)
        for i, ch in enumerate(chunks, 1):
            payload = SUB_SEP.join([f"{i}/{total}", str(len(moves)),
                                    ",".join(ch), self.gid])
            self._send(T_SYNC, payload, reliable=False, now=now)

    def _rx_sync(self, f: Frame, now: float) -> None:
        fields = f.fields
        if len(fields) < 3:
            return
        part_txt, total_txt = fields[0].split("/")
        part, total = int(part_txt), int(total_txt)
        self.sync_total = total
        self.sync_parts[part] = fields[2]
        if len(self.sync_parts) < total:
            self.listener.on_log("info",
                                 tr("Resynchronisation {done}/{total}", done=len(self.sync_parts),
                                 total=total))
            return

        moves_txt = ",".join(self.sync_parts[i] for i in range(1, total + 1))
        tokens = [t for t in moves_txt.split(",") if t]
        rebuilt = Board()
        for tok in tokens:
            try:
                uid, num, promo = parse_compact(tok)
                m = rebuilt.find_move(uid, number_to_sq(num), promo)
            except Exception:
                m = None
            if m is None:
                self.listener.on_log("err",
                                     tr("Historique recu invalide au coup '{token}' - "
                                        "resynchronisation impossible", token=tok))
                self.sync_parts.clear()
                return
            rebuilt.push(m)

        # l'historique local doit etre un prefixe de celui recu
        local = [compact_move(m) for m in self.board.moves]
        if local != tokens[:len(local)]:
            self.listener.on_log("err", tr(
                "Divergence irreconciliable des historiques - "
                "il faut relancer une partie"))
            self.sync_parts.clear()
            return

        self.board = rebuilt
        self.sync_parts.clear()
        self.listener.on_log("info",
                             tr("Resynchronisation reussie sur {plies} demi-coups "
                                "(empreinte {hash})", plies=len(tokens),
                                hash=position_hash(self.board)))
        self._check_end()

    def _rx_rsgn(self, f: Frame, now: float) -> None:
        self._ack(f, now)
        self._finish("resign", tr("{src} abandonne - vous gagnez", src=f.src))

    def _rx_drwo(self, f: Frame, now: float) -> None:
        self._ack(f, now)
        self.draw_offered_by_peer = True
        self.listener.on_draw_offer()

    def _rx_drwa(self, f: Frame, now: float) -> None:
        self._ack(f, now)
        self._finish("draw", tr("Nulle par accord mutuel"))

    def _rx_drwd(self, f: Frame, now: float) -> None:
        self._ack(f, now)
        self.draw_offered_by_me = False
        self.listener.on_log("info", tr("Proposition de nulle refusee"))

    def _rx_chat(self, f: Frame, now: float) -> None:
        self._ack(f, now)
        self.listener.on_chat(f.src, f.payload)

    def _rx_ping(self, f: Frame, now: float) -> None:
        self._send(T_PONG, f.payload, reliable=False, now=now)

    def _rx_pong(self, f: Frame, now: float) -> None:
        self.listener.on_log("info", tr("Reponse balise recue"))

    # -- fin de partie ------------------------------------------------------

    def _check_end(self) -> None:
        code, label = self.board.status()
        if code != "playing":
            self._finish(code, label)

    def _finish(self, code: str, label: str) -> None:
        self.state = self.OVER
        self.result = label
        self.listener.on_game_over(code, label)
        self.listener.on_state()
