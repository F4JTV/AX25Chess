#!/usr/bin/env python3
"""
Partie complete jouee en boucle locale a travers la pile AX.25 + KISS,
sur un canal degrade, puis resynchronisation forcee.
"""

import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ax25chess.ax25_kiss import (KissDecoder, build_ui_frame, kiss_wrap,   # noqa: E402
                                 parse_ui_frame)
from ax25chess.chess_rules import WHITE, name_to_sq                        # noqa: E402
from ax25chess.protocol import (GameSession, SessionListener,              # noqa: E402
                                position_hash)

NOW = [0.0]


class LoopbackListener(SessionListener):
    """Rejoue chaque trame emise a travers AX.25 + KISS vers la station pair."""

    def __init__(self, seed: int):
        self.peer = None
        self.loss = 0.0
        self.rng = random.Random(seed)
        self.lost = 0
        self.retries = 0
        self.logs: list[tuple[str, str]] = []

    def on_send(self, frame):
        ax25 = build_ui_frame(frame.src, frame.dst, frame.encode(), ["WIDE1-1"])
        stream = kiss_wrap(ax25)
        if self.rng.random() < self.loss:
            self.lost += 1
            return
        decoder = KissDecoder()
        for raw in decoder.feed(stream):
            parsed = parse_ui_frame(raw)
            assert parsed, "trame AX.25 illisible"
            self.peer.feed(parsed["info"], parsed["src"], now=NOW[0])

    def on_log(self, level, text):
        if level == "warn" and text.startswith("Retransmission"):
            self.retries += 1
        self.logs.append((level, text))

    def on_state(self): pass
    def on_move_applied(self, move, by_peer): pass
    def on_chat(self, who, text): pass
    def on_game_over(self, code, text): self.logs.append(("end", text))
    def on_draw_offer(self): pass


def settle(a, b, limit=500):
    for _ in range(limit):
        NOW[0] += 1.0
        a.tick(NOW[0])
        b.tick(NOW[0])
        if a.pending is None and b.pending is None:
            return


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    return ok


def main() -> int:
    ok = True

    # --- 1. mat du berger sur canal propre --------------------------------
    la, lb = LoopbackListener(7), LoopbackListener(8)
    a = GameSession("N0CALL", "N0CALL-2", la, rng=random.Random(1))
    b = GameSession("N0CALL-2", "N0CALL", lb, rng=random.Random(2))
    la.peer, lb.peer = b, a
    a.invite(now=NOW[0])

    ok &= check("couleurs attribuees sans conflit",
                a.my_color != b.my_color and None not in (a.my_color, b.my_color))
    ok &= check("identifiant de partie partage", a.gid == b.gid)

    white, black = (a, b) if a.my_color == WHITE else (b, a)
    berger = [(white, "WP5", "e4"), (black, "BP5", "e5"),
              (white, "WB2", "c4"), (black, "BN1", "c6"),
              (white, "WQ1", "h5"), (black, "BN2", "f6"),
              (white, "WQ1", "f7")]
    played = all(s.play_local(uid, name_to_sq(dest), now=NOW[0])
                 for s, uid, dest in berger)
    ok &= check("sept demi-coups transmis et acceptes", played)
    ok &= check("positions identiques", a.board.fen() == b.board.fen())
    ok &= check("empreintes identiques",
                position_hash(a.board) == position_hash(b.board))
    ok &= check("mat detecte des deux cotes",
                a.state == b.state == GameSession.OVER)

    # --- 2. ouverture espagnole avec 35 % de pertes ------------------------
    la, lb = LoopbackListener(11), LoopbackListener(12)
    a = GameSession("N0CALL", "N0CALL-2", la, rng=random.Random(11))
    b = GameSession("N0CALL-2", "N0CALL", lb, rng=random.Random(12))
    la.peer, lb.peer = b, a
    a.invite(now=NOW[0])
    la.loss = lb.loss = 0.35

    white, black = (a, b) if a.my_color == WHITE else (b, a)
    espagnole = [(white, "WP5", "e4"), (black, "BP5", "e5"),
                 (white, "WN2", "f3"), (black, "BN1", "c6"),
                 (white, "WB2", "b5"), (black, "BP1", "a6"),
                 (white, "WB2", "c6"), (black, "BP4", "c6")]
    degraded = True
    for session, uid, dest in espagnole:
        settle(a, b)
        degraded &= session.play_local(uid, name_to_sq(dest), now=NOW[0])
    settle(a, b)
    ok &= check(f"partie complete malgre {la.lost + lb.lost} trames perdues "
                f"et {la.retries + lb.retries} retransmissions", degraded)
    ok &= check("positions identiques apres canal degrade",
                a.board.fen() == b.board.fen())

    # --- 3. resynchronisation apres corruption ----------------------------
    la.loss = lb.loss = 0.0
    b.board.pop()
    b.board.pop()
    ok &= check("desynchronisation bien reelle avant resync",
                a.board.fen() != b.board.fen())
    b.request_sync(now=NOW[0])
    ok &= check("resynchronisation reussie", a.board.fen() == b.board.fen())

    print("\nTOUS LES TESTS PASSENT" if ok else "\nDES TESTS ONT ECHOUE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
