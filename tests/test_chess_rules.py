#!/usr/bin/env python3
"""Validation du moteur de regles par comptage de noeuds (perft)."""

import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from ax25chess.chess_rules import Board, perft   # noqa: E402

INITIAL = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

CASES = [
    ("position initiale", INITIAL, [20, 400, 8902, 197281]),
    ("kiwipete",
     "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
     [48, 2039, 97862]),
    ("position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
     [14, 191, 2812, 43238]),
    ("position 4",
     "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
     [6, 264, 9467]),
    ("position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
     [44, 1486, 62379]),
]


def main() -> int:
    failures = 0
    for name, fen, expected in CASES:
        board = Board.from_fen(fen)
        for depth, want in enumerate(expected, 1):
            t0 = time.time()
            got = perft(board, depth)
            ok = got == want
            failures += not ok
            print(f"[{'OK ' if ok else 'ECHEC'}] {name:20s} profondeur {depth} "
                  f"-> {got:>8d} (attendu {want:>8d})  {time.time() - t0:5.2f}s")
    print("\nTOUS LES TESTS PASSENT" if not failures else f"\n{failures} ECHEC(S)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
