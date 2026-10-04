#!/usr/bin/env python3
"""Generate the interoperability vectors from the Python version 1.0.0.

Run from the root of the Python AX25Chess source tree:

    python3 path/to/make_vectors.py > python_vectors.json

The C++ tests replay the same games and must produce the same SAN, FEN,
fingerprints, compact moves and frame encodings, byte for byte.
"""
import json, sys
sys.path.insert(0, ".")
from ax25chess.chess_rules import Board, name_to_sq, sq_number
from ax25chess.protocol import (Frame, position_hash, encode_move, compact_move, crc16)

GAMES = {
    "scholar": "e2e4 e7e5 f1c4 b8c6 d1h5 g8f6 h5f7",
    "spanish": "e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6 e1g1 f7f6 d2d4 e5d4 f3d4 c6c5 d4e2 d8d1 f1d1",
    "castles_ep": "e2e4 g8f6 e4e5 d7d5 e5d6 e7d6 g1f3 f8e7 f1e2 e8g8 e1g1 b8c6 b1c3 c8f5 d2d3 d8d7 c1e3 a8d8",
    "promotion": "a2a4 b7b5 a4b5 a7a6 b5a6 c8b7 a6b7 b8c6 b7a8q c6b8 a8b8 d8c8 b8c8",
    "underpromo": "h2h4 g7g5 h4g5 h7h6 g5h6 f8g7 h6g7 g8f6 g7h8n f6h5",
    "long_castle": "d2d4 d7d5 b1c3 b8c6 c1f4 c8f5 d1d2 d8d7 e1c1 e8c8 f4h6 g7h6 d2h6 f5g4",
}

def uci_to_move(board, uci):
    frm = name_to_sq(uci[0:2]); to = name_to_sq(uci[2:4])
    promo = uci[4].upper() if len(uci) > 4 else None
    for m in board.legal_moves():
        if m.frm == frm and m.to == to and (promo is None or m.promo == promo):
            if promo is None and m.promo:
                continue
            return m
    raise SystemExit(f"illegal {uci}")

out = {"games": {}, "frames": [], "crc": []}
for name, line in GAMES.items():
    b = Board()
    plies = []
    for i, uci in enumerate(line.split()):
        m = uci_to_move(b, uci)
        san = b.san(m)
        m.san_text = san
        b.push(m)
        h = position_hash(b)
        f = Frame("3F1A", "N0CALL", "N0CALL-2", i + 1, "MOVE", encode_move(m, i, h))
        plies.append({"uci": uci, "uid": m.uid, "to": sq_number(m.to), "promo": m.promo or "",
                      "san": san, "fen": b.fen(), "hash": h, "compact": compact_move(m),
                      "key": b.position_key(), "frame": f.text()})
    out["games"][name] = {"plies": plies, "status": b.status()[0]}

for args in [("3F1A", "N0CALL", "N0CALL-2", 7, "MOVE", "WP5;13;29;-;0;A34F"),
             ("00FF", "F4ABC-7", "F1XYZ", 1, "HELLO", "DEADBEEF;1"),
             ("00FF", "F1XYZ", "F4ABC-7", 9999, "ACPT", "0000002A;B"),
             ("ABCD", "N0CALL", "ALL", 12, "CHAT", "bonjour d\u00e9j\u00e0 vu / test"),
             ("ABCD", "N0CALL", "CQ", 13, "ACK", "12;F88A"),
             ("ABCD", "N0CALL", "N0CALL-2", 14, "SYNC", "1/1;3;WP5>29,BP5>37,WN2>22;ABCD"),
             ("ABCD", "N0CALL", "N0CALL-2", 15, "RSGN", "")]:
    f = Frame(*args)
    out["frames"].append({"gid": f.gid, "src": f.src, "dst": f.dst, "seq": f.seq, "type": f.type,
                          "payload": f.payload, "text": f.text()})
for s in ["", "A", "123456789", "CHS1|3F1A|N0CALL|N0CALL-2|7|MOVE|WP5;13;29;-;0;A34F"]:
    out["crc"].append({"text": s, "crc": crc16(s)})
json.dump(out, sys.stdout, indent=1, ensure_ascii=True)
