#!/usr/bin/env python3
"""
Fins de partie declenchees par une trame recue.

Ces chemins sont ceux qui traversent le plus de couches d'un coup : trame
recue -> protocol -> rappel de l'interface -> boite de dialogue -> reponse
renvoyee au protocole. Une proposition de nulle y a plante en exploitation,
sur un TypeError leve seulement au moment de formater le message.

Les boites modales sont neutralisees : ce banc verifie le chemin de code, pas
le rendu.
"""

import os
import shutil
import sys
import tempfile
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ["AX25CHESS_LANG"] = "fr"
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

results: list[bool] = []


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    results.append(bool(ok))
    return bool(ok)


def main() -> int:
    from PyQt6.QtCore import QSettings, QTimer
    from PyQt6.QtWidgets import QApplication, QMessageBox

    import ax25chess.games as games_mod
    sandbox = Path(tempfile.mkdtemp(prefix="ax25chess-endgame-"))
    games_mod.STATE_DIR = sandbox
    games_mod.GAMES_DIR = sandbox / "parties"
    games_mod.LEGACY_FILE = sandbox / "absent.json"

    app = QApplication([])
    app.setQuitOnLastWindowClosed(False)
    QSettings("AX25Chess", "AX25Chess").clear()

    QMessageBox.question = staticmethod(
        lambda *a, **k: QMessageBox.StandardButton.Yes)
    QMessageBox.information = staticmethod(
        lambda *a, **k: QMessageBox.StandardButton.Ok)
    QMessageBox.warning = staticmethod(
        lambda *a, **k: QMessageBox.StandardButton.Ok)

    from ax25chess.chess_rules import WHITE, name_to_sq
    from ax25chess.main_window import MainWindow
    from ax25chess.protocol import Frame, GameSession

    def fresh_session(win) -> GameSession:
        session = GameSession("N0CALL", "N0CALL-2", win)
        session.my_color = WHITE
        session.state = GameSession.PLAYING
        session.gid = "3F1A"
        win.session = session
        return session

    win = MainWindow()
    win.link.send_info = lambda *a, **k: True      # pas de radio sur ce banc

    scenarios = []

    # --- 1. proposition de nulle acceptee ------------------------------
    session = fresh_session(win)
    session.feed(Frame("3F1A", "N0CALL-2", "N0CALL", 1, "DRWO", "").encode(),
                 "N0CALL-2", now=0.0)
    check("proposition de nulle enregistree", session.draw_offered_by_peer)
    check("aucune boite ouverte dans le rappel reseau",
          session.state == GameSession.PLAYING)
    scenarios.append(("nulle", session))

    def after_draw():
        check("nulle conclue apres le traitement differe",
              session.state == GameSession.OVER
              and session.result == "Nulle par accord mutuel")

        # --- 2. abandon recu ------------------------------------------
        other = fresh_session(win)
        other.feed(Frame("3F1A", "N0CALL-2", "N0CALL", 2, "RSGN", "").encode(),
                   "N0CALL-2", now=0.0)
        check("abandon du correspondant traite",
              other.state == GameSession.OVER and "abandonne" in (other.result or ""))

        # --- 3. mat recu ----------------------------------------------
        mate = fresh_session(win)
        for uid, dest in (("WP6", "f3"), ("BP5", "e5"), ("WP7", "g4")):
            move = mate.board.find_move(uid, name_to_sq(dest))
            move.san_text = mate.board.san(move)
            mate.board.push(move)
        peer = GameSession("N0CALL-2", "N0CALL", win)
        peer.board = mate.board.__class__()
        for uid, dest in (("WP6", "f3"), ("BP5", "e5"), ("WP7", "g4")):
            peer.board.push(peer.board.find_move(uid, name_to_sq(dest)))
        peer.my_color = "B"
        peer.state = GameSession.PLAYING
        peer.gid = "3F1A"
        peer.seq = 5

        sent = []
        win.link.send_info = lambda *a, **k: True
        original = win.on_send
        win.on_send = lambda frame: sent.append(frame)
        peer.play_local("BQ1", name_to_sq("h4"), now=0.0)
        win.on_send = original
        check("le mat produit bien une trame", bool(sent))
        if sent:
            mate.feed(sent[-1].encode(), "N0CALL-2", now=0.0)
            check("mat recu et annonce sans erreur",
                  mate.state == GameSession.OVER
                  and "mat" in (mate.result or "").lower())

        QTimer.singleShot(200, finish)

    def finish():
        win.close()
        shutil.rmtree(sandbox, ignore_errors=True)
        print("\nTOUS LES TESTS PASSENT" if all(results)
              else "\nDES TESTS ONT ECHOUE")
        app.exit(0 if all(results) else 1)

    QTimer.singleShot(200, after_draw)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
