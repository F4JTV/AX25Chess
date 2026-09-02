#!/usr/bin/env python3
"""
Direwolf ne doit voir qu'UN seul client KISS.

MAX_NET_CLIENTS vaut 3 dans kiss_frame.h : une socket de sondage jetable
consomme un tiers des emplacements disponibles, et Direwolf la journalise en
« Attached to KISS TCP client application N » puis « has gone away ».

Le faux Direwolf de ce banc est deliberement MUET : il n'ecrit rien sur sa
console et compte les connexions dans un fichier annexe. C'est la fidelite au
comportement Windows qui compte ici — quand la banniere arrive, la
disponibilite est detectee tot et le defaut ne se manifeste pas. Un banc
bavard passerait avec le bug present.
"""

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

FAKE_PORT = 8031

# Muet sur stdout, comme Direwolf derriere un tuyau sous Windows.
MUTE_DIREWOLF = '''#!/usr/bin/env python3
import socket, sys, threading, time
port, journal = %d, %r
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", port))
srv.listen(4)
clients, lock = [], threading.Lock()

def note(line):
    with lock:
        with open(journal, "a") as handle:
            handle.write(line + "\\n")

def handle(conn, index):
    note("ATTACHED %%d" %% index)
    try:
        while conn.recv(4096):
            pass
    except OSError:
        pass
    note("GONE %%d" %% index)

def loop():
    index = 0
    while True:
        conn, _ = srv.accept()
        clients.append(conn)
        threading.Thread(target=handle, args=(conn, index), daemon=True).start()
        index += 1

threading.Thread(target=loop, daemon=True).start()
while True:
    time.sleep(1)
'''


def check(label: str, ok: bool, results: list) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    results.append(bool(ok))
    return bool(ok)


def build_fake(directory: Path) -> tuple[str, Path]:
    journal = directory / "clients.log"
    exe = directory / "direwolf"
    exe.write_text(MUTE_DIREWOLF % (FAKE_PORT, str(journal)))
    exe.chmod(0o755)
    return str(exe), journal


def main() -> int:
    from PyQt6.QtCore import QSettings, QTimer
    from PyQt6.QtWidgets import QApplication

    import ax25chess.games as games_mod
    sandbox = Path(tempfile.mkdtemp(prefix="ax25chess-oneclient-"))
    games_mod.STATE_DIR = sandbox
    games_mod.GAMES_DIR = sandbox / "parties"
    games_mod.LEGACY_FILE = sandbox / "absent.json"

    app = QApplication([])
    QSettings("F4JTV", "AX25Chess").clear()

    exe, journal = build_fake(sandbox)
    results: list[bool] = []

    from ax25chess.main_window import MainWindow
    win = MainWindow()
    win.ed_exe.setText(exe)
    win.ed_conf.setText("")
    win.sp_port.setValue(FAKE_PORT)
    win.ed_call.setText("F4JTV")
    win.ed_peer.setText("F1ABC")
    win.chk_launch.setChecked(True)
    win.chk_autoconnect.setChecked(True)
    win.chk_console.setChecked(False)
    win._startup_done = False
    win._startup()

    def finish():
        lines = journal.read_text().splitlines() if journal.exists() else []
        attached = [l for l in lines if l.startswith("ATTACHED")]
        gone = [l for l in lines if l.startswith("GONE")]

        print("--- ce que Direwolf a vu ---")
        for line in lines:
            print("   ", line)
        print()

        check(f"une seule attache client ({len(attached)})",
              len(attached) == 1, results)
        check(f"aucun client parti ({len(gone)})", len(gone) == 0, results)
        check("liaison KISS etablie", win.link.online, results)
        check("Direwolf marque comme pret", win.direwolf.is_ready, results)
        check("aucune ligne de console recue (fidelite Windows)",
              "ATTACHED" not in win.dw_console.toPlainText(), results)

        win.link.close()
        win.direwolf.stop()
        win.close()
        shutil.rmtree(sandbox, ignore_errors=True)
        print("\nTOUS LES TESTS PASSENT" if all(results)
              else "\nDES TESTS ONT ECHOUE")
        app.exit(0 if all(results) else 1)

    QTimer.singleShot(6000, finish)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
