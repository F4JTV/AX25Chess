#!/usr/bin/env python3
"""
Magasin des parties en cours : plusieurs parties de front, reprise,
suppression, migration de l'ancien fichier unique, et non-regression du
defaut d'origine (la question du demarrage revenait a chaque lancement).
"""

import json
import os
import shutil
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
# Les assertions portant sur des libelles doivent etre deterministes.
os.environ["AX25CHESS_LANG"] = "fr"
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import ax25chess.games as games_mod                        # noqa: E402

SANDBOX = Path(tempfile.mkdtemp(prefix="ax25chess-tests-"))
games_mod.STATE_DIR = SANDBOX
games_mod.GAMES_DIR = SANDBOX / "parties"
games_mod.LEGACY_FILE = SANDBOX / "partie_en_cours.json"

from PyQt6.QtWidgets import QApplication, QMessageBox                # noqa: E402

from ax25chess.chess_rules import WHITE, name_to_sq                  # noqa: E402
from ax25chess.game_manager import GameManagerDialog                 # noqa: E402
from ax25chess.games import GameStore                                # noqa: E402
from ax25chess.protocol import GameSession                           # noqa: E402


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    return ok


def silence_dialogs() -> None:
    """Les boites modales bloqueraient un test sans interaction."""
    QMessageBox.question = staticmethod(
        lambda *a, **k: QMessageBox.StandardButton.Yes)
    QMessageBox.warning = staticmethod(lambda *a, **k: QMessageBox.StandardButton.Ok)
    QMessageBox.information = staticmethod(
        lambda *a, **k: QMessageBox.StandardButton.Ok)


def store_tests(results: list) -> GameStore:
    store = GameStore(games_mod.GAMES_DIR)

    # migration depuis l'ancien fichier unique
    games_mod.LEGACY_FILE.parent.mkdir(parents=True, exist_ok=True)
    games_mod.LEGACY_FILE.write_text(json.dumps({
        "gid": "AAAA", "call": "N0CALL", "peer": "N0CALL-2", "color": "W",
        "moves": ["WP5>29", "BP5>37"], "nonce": 1, "seq": 3}))
    migrated = store.list()
    results.append(check("ancien fichier unique repris",
                         len(migrated) == 1 and migrated[0].gid == "AAAA"))
    results.append(check("ancien fichier supprime apres migration",
                         not games_mod.LEGACY_FILE.exists()))

    # plusieurs parties de front
    store.save("BBBB", "N0CALL", "N0CALL-3", "B", ["WP5>29"])
    time.sleep(0.02)
    store.save("CCCC", "N0CALL", "N0CALL-4", "W", ["WP5>29", "BP5>37", "WN2>22"])
    time.sleep(0.02)
    store.save("BBBB", "N0CALL", "N0CALL-3", "B", ["WP5>29", "BP4>44"])
    listed = store.list()
    results.append(check("trois parties menees de front", len(listed) == 3))
    results.append(check("mise a jour sans doublon",
                         sum(1 for g in listed if g.gid == "BBBB") == 1))
    results.append(check("tri de la plus recente a la plus ancienne",
                         [g.gid for g in listed] == ["BBBB", "CCCC", "AAAA"]))
    results.append(check("trait calcule depuis le nombre de demi-coups",
                         next(g for g in listed if g.gid == "AAAA").my_turn
                         and not next(g for g in listed if g.gid == "CCCC").my_turn))
    results.append(check("aucun fichier temporaire residuel",
                         not list(games_mod.GAMES_DIR.glob("*.tmp"))))

    store.delete("BBBB", "N0CALL-3")
    results.append(check("suppression effective",
                         [g.gid for g in store.list()] == ["CCCC", "AAAA"]))
    return store


def window_tests(results: list, store: GameStore) -> None:
    from ax25chess.main_window import MainWindow

    win = MainWindow()
    win.store = store

    # une partie jouee localement doit s'enregistrer toute seule
    session = GameSession("N0CALL", "N0CALL-2", win)
    session.my_color = WHITE
    session.state = GameSession.PLAYING
    session.gid = "3F1A"
    win.session = session
    for uid, dest in (("WP5", "e4"), ("BP3", "c5"), ("WN2", "f3")):
        move = session.board.find_move(uid, name_to_sq(dest))
        move.san_text = session.board.san(move)
        session.board.push(move)
        win.on_move_applied(move, False)
    results.append(check("partie en cours enregistree automatiquement",
                         store.find("3F1A", "N0CALL-2") is not None))
    results.append(check("compteur affiche sur le bouton",
                         win.btn_games.text() == f"Parties en cours ({store.count()})"))

    dialog = GameManagerDialog(store, current_gid="3F1A", parent=win)
    results.append(check("gestionnaire liste toutes les parties",
                         dialog.table.rowCount() == store.count()))
    rows = [dialog.table.item(r, 0).text() for r in range(dialog.table.rowCount())]
    results.append(check("partie courante signalee dans la liste",
                         any("en cours" in r for r in rows)))

    target = next(g for g in dialog.games if g.gid == "CCCC")
    silence_dialogs()
    win._resume_game(target)
    results.append(check("reprise d'une autre partie",
                         win.session.gid == "CCCC"
                         and win.session.peer_call == "N0CALL-4"))
    results.append(check("historique rejoue integralement",
                         len(win.session.board.moves) == 3
                         and win.table.rowCount() == 3))
    results.append(check("indicatifs restaures dans l'onglet RADIO",
                         win.ed_peer.text() == "N0CALL-4"))

    # historique corrompu : refus propre, sans planter
    store.save("DEAD", "N0CALL", "N0CALL-5", "W", ["WP5>29", "WP5>29"])
    corrupt = store.find("DEAD", "N0CALL-5")
    before = win.session.gid
    win._resume_game(corrupt)
    results.append(check("historique incoherent refuse sans casse",
                         win.session.gid == before))
    store.delete("DEAD", "N0CALL-5")

    # le defaut signale : refuser puis relancer ne doit plus rien redemander
    win.close()
    win2 = MainWindow()
    win2.store = store
    win2._announce_saved_games()
    results.append(check("aucune question imposee au demarrage",
                         win2.btn_games.isEnabled() and store.count() > 0))
    win2.close()


def main() -> int:
    app = QApplication([])
    results: list[bool] = []
    try:
        store = store_tests(results)
        window_tests(results, store)
    finally:
        shutil.rmtree(SANDBOX, ignore_errors=True)
    ok = all(results)
    print("\nTOUS LES TESTS PASSENT" if ok else "\nDES TESTS ONT ECHOUE")
    app.quit()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
