#!/usr/bin/env python3
"""
Fermeture rapide de l'application.

Le defaut : `terminate()` poste un WM_CLOSE qu'une application console ignore
toujours sous Windows, donc l'attente de trois secondes qui suivait etait
perdue d'avance et systematique. S'y ajoutaient un `waitForFinished(1000)`
apres le `kill()`, et un `taskkill` attendu jusqu'a cinq secondes.

Le banc utilise un faux Direwolf qui IGNORE SIGTERM : c'est l'equivalent
Unix d'une application console ignorant le WM_CLOSE. Sans cela le processus
mourrait poliment et le defaut resterait invisible.
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

FAKE_PORT = 8041
BUDGET_MS = 900          # au-dela, la fermeture se voit a l'oeil nu

STUBBORN_DIREWOLF = '''#!/usr/bin/env python3
"""Faux Direwolf qui ignore l'arret courtois, comme une console Windows."""
import signal, socket, threading, time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", %d))
srv.listen(4)
clients = []
def loop():
    while True:
        conn, _ = srv.accept()
        clients.append(conn)
threading.Thread(target=loop, daemon=True).start()
while True:
    time.sleep(1)
''' % FAKE_PORT

results: list[bool] = []


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    results.append(bool(ok))
    return bool(ok)


def make_fake(directory: Path) -> str:
    exe = directory / "direwolf"
    exe.write_text(STUBBORN_DIREWOLF)
    exe.chmod(0o755)
    return str(exe)


def alive(pid: int) -> bool:
    import subprocess
    out = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)],
                         capture_output=True, text=True).stdout.strip()
    return bool(out) and not out.startswith("Z")


def main() -> int:
    from PyQt6.QtCore import QSettings, QTimer
    from PyQt6.QtWidgets import QApplication

    import ax25chess.games as games_mod
    sandbox = Path(tempfile.mkdtemp(prefix="ax25chess-shutdown-"))
    games_mod.STATE_DIR = sandbox
    games_mod.GAMES_DIR = sandbox / "parties"
    games_mod.LEGACY_FILE = sandbox / "absent.json"

    app = QApplication([])
    # Sans cela, fermer la fenetre mesuree terminerait la boucle d'evenements
    # et les phases suivantes ne s'executeraient jamais.
    app.setQuitOnLastWindowClosed(False)
    QSettings("F4JTV", "AX25Chess").clear()
    exe = make_fake(sandbox)

    from ax25chess.main_window import MainWindow

    win = MainWindow()
    win.ed_exe.setText(exe)
    win.ed_conf.setText("")
    win.sp_port.setValue(FAKE_PORT)
    win.chk_launch.setChecked(True)
    win.chk_autoconnect.setChecked(True)
    win.chk_console.setChecked(False)
    win.show()
    win._startup_done = False
    win._startup()

    state: dict = {}

    def phase_capture():
        check("Direwolf lance en mode capture", win.direwolf.running)
        check("liaison etablie", win.link.online)
        pid = win.direwolf.pid
        started = time.perf_counter()
        win.close()
        elapsed = (time.perf_counter() - started) * 1000
        print(f"    fermeture en mode capture : {elapsed:.0f} ms")
        check(f"fermeture sous {BUDGET_MS} ms en mode capture",
              elapsed < BUDGET_MS)
        time.sleep(0.4)
        check("Direwolf reellement arrete", not alive(pid))
        QTimer.singleShot(300, phase_console)

    def phase_console():
        from ax25chess.direwolf import DirewolfProcess
        proc = DirewolfProcess()
        proc.start(exe, host="127.0.0.1", port=FAKE_PORT,
                   detached=True, probe_port=False)
        state["proc"] = proc
        state["pid"] = proc.pid
        QTimer.singleShot(1200, phase_console_stop)

    def phase_console_stop():
        proc, pid = state["proc"], state["pid"]
        check("Direwolf lance en console separee", proc.running)
        started = time.perf_counter()
        proc.stop()
        elapsed = (time.perf_counter() - started) * 1000
        print(f"    arret en console separee  : {elapsed:.0f} ms")
        check(f"arret sous {BUDGET_MS} ms en console separee",
              elapsed < BUDGET_MS)
        time.sleep(0.4)
        check("processus console reellement arrete", not alive(pid))

        # Une fermeture sans Direwolf doit etre quasi instantanee.
        second = MainWindow()
        second.show()
        started = time.perf_counter()
        second.close()
        elapsed = (time.perf_counter() - started) * 1000
        print(f"    fermeture sans Direwolf   : {elapsed:.0f} ms")
        check("fermeture sans Direwolf sous 200 ms", elapsed < 200)

        shutil.rmtree(sandbox, ignore_errors=True)
        print("\nTOUS LES TESTS PASSENT" if all(results)
              else "\nDES TESTS ONT ECHOUE")
        app.exit(0 if all(results) else 1)

    QTimer.singleShot(4000, phase_capture)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
