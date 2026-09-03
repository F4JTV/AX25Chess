#!/usr/bin/env python3
"""
Sequence de demarrage automatique : lancement de Direwolf, detection du port
KISS et ouverture de la liaison, avec un faux Direwolf en guise de banc.
"""

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
# Les assertions portant sur des libelles doivent etre deterministes.
os.environ["AX25CHESS_LANG"] = "fr"
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from PyQt6.QtCore import QSettings, QTimer          # noqa: E402
from PyQt6.QtWidgets import QApplication           # noqa: E402

FAKE_PORT = 8011

FAKE_DIREWOLF = '''#!/usr/bin/env python3
"""Faux Direwolf : imite la console et ouvre un serveur KISS TCP."""
import socket, threading, time
print("Dire Wolf version 1.8 (banc de test AX25Chess)", flush=True)
print("Audio device for both receive and transmit: plughw:1,0 (channel 0)", flush=True)
time.sleep(0.5)
srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", %d))
srv.listen(4)
print("Ready to accept KISS TCP client application 0 on port %d ...", flush=True)
clients = []          # on garde les sockets, sinon elles sont refermees aussitot
def loop():
    while True:
        conn, _ = srv.accept()
        clients.append(conn)
        print("Connected to KISS TCP client application 0 ...", flush=True)
threading.Thread(target=loop, daemon=True).start()
while True:
    time.sleep(1)
''' % (FAKE_PORT, FAKE_PORT)


def make_fake() -> str:
    path = Path(tempfile.mkdtemp()) / "direwolf"
    path.write_text(FAKE_DIREWOLF)
    path.chmod(0o755)
    return str(path)


def configure(exe: str, launch: bool) -> None:
    QSettings("AX25Chess", "AX25Chess").clear()
    s = QSettings("AX25Chess", "AX25Chess")
    s.setValue("dw_exe", exe)
    s.setValue("dw_launch", launch)
    s.setValue("autoconnect", True)
    s.setValue("port", FAKE_PORT)
    s.setValue("call", "N0CALL")
    s.setValue("peer", "N0CALL-2")
    s.sync()


MUTE_DIREWOLF = '''#!/usr/bin/env python3
"""Direwolf muet : ouvre le port KISS sans jamais rien ecrire.

Reproduit le comportement observe sous Windows, ou stdout passe en tampon de
bloc des qu'il est capture et ou aucune ligne n'atteint l'application.
"""
import socket, threading, time
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


def make_mute() -> str:
    path = Path(tempfile.mkdtemp()) / "direwolf"
    path.write_text(MUTE_DIREWOLF)
    path.chmod(0o755)
    return str(path)


def process_alive(pid: int) -> bool:
    """Vrai si le PID correspond a un processus reellement actif.

    `ps -p` reussit encore sur un zombie : on filtre donc sur l'etat, sinon un
    processus correctement arrete passerait pour vivant.
    """
    out = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)],
                         capture_output=True, text=True).stdout.strip()
    return bool(out) and not out.startswith("Z")


def check(label: str, ok: bool) -> bool:
    print(f"[{'OK ' if ok else 'ECHEC'}] {label}")
    return ok


def main() -> int:
    app = QApplication([])
    from ax25chess.main_window import MainWindow
    exe = make_fake()
    results = []

    # --- 1. l'application lance Direwolf puis se connecte -----------------
    configure(exe, launch=True)
    win = MainWindow()
    steps: list[str] = []
    win.direwolf.started.connect(lambda: steps.append("lance"))
    win.direwolf.ready.connect(lambda: steps.append("pret"))
    win.link.connected.connect(lambda: steps.append("connecte"))

    def phase1():
        results.append(check("Direwolf lance automatiquement", "lance" in steps))
        results.append(check("port KISS detecte pret", "pret" in steps))
        results.append(check("liaison ouverte sans intervention",
                             "connecte" in steps and win.link.online))
        results.append(check("console Direwolf alimentee",
                             "Ready to accept" in win.dw_console.toPlainText()))
        before = len(win.dw_console.toPlainText().splitlines())
        win._startup()
        win._start_direwolf()
        results.append(check("second appel sans relance ni perte de console",
                             len(win.dw_console.toPlainText().splitlines()) >= before))
        win.close()
        results.append(check("Direwolf arrete a la fermeture",
                             not win.direwolf.running))
        QTimer.singleShot(200, phase2)

    external = {}

    def phase2():
        # --- 2. un Direwolf tourne deja : pas de seconde instance ---------
        external["proc"] = subprocess.Popen([exe], stdout=subprocess.DEVNULL)
        time.sleep(1.5)
        configure(exe, launch=True)
        win2 = MainWindow()
        external["win"] = win2

        def phase2_check():
            results.append(check("aucune seconde instance lancee",
                                 not win2.direwolf.running))
            results.append(check("liaison etablie sur le Direwolf existant",
                                 win2.link.online))
            win2.close()
            QTimer.singleShot(200, phase3)

        QTimer.singleShot(3000, phase2_check)

    def phase3():
        external["proc"].terminate()
        # --- 3. Direwolf muet : la disponibilite vient du sondage du port ---
        configure(make_mute(), launch=True)
        win_mute = MainWindow()
        external["mute"] = win_mute

        def phase3_check():
            results.append(check("Direwolf muet detecte pret par sondage du port",
                                 win_mute.direwolf.is_ready))
            results.append(check("liaison ouverte sans aucune ligne de console",
                                 win_mute.link.online))
            # La disponibilite vient desormais de la liaison KISS elle-meme :
            # aucune socket de sondage ne doit avoir ete ouverte, sans quoi
            # Direwolf verrait un client de trop.
            results.append(check("disponibilite obtenue sans sonde",
                                 win_mute.direwolf.is_ready
                                 and "Port KISS" not in
                                 win_mute.dw_console.toPlainText()))
            win_mute.close()
            QTimer.singleShot(200, phase_detached)

        QTimer.singleShot(3000, phase3_check)

    def phase_detached():
        # --- 4. mode fenetre separee (chemin Windows, force ici) ----------
        from ax25chess.direwolf import DirewolfProcess
        proc = DirewolfProcess()
        lines: list[str] = []
        proc.output.connect(lines.append)
        ready: list[str] = []
        proc.ready.connect(lambda: ready.append("pret"))
        started = proc.start(make_mute(), host="127.0.0.1", port=FAKE_PORT,
                             detached=True)

        def detached_check():
            pid = proc.pid
            results.append(check("lancement en console separee accepte", started))
            results.append(check("handle de processus conserve",
                                 proc._popen is not None
                                 and isinstance(pid, int) and pid > 0))
            results.append(check("liveness lisible par poll()", proc.running))
            results.append(check("port KISS detecte en mode console",
                                 bool(ready)))
            results.append(check("sortie renvoyee vers la fenetre separee",
                                 any("propre fenetre" in l for l in lines)))
            proc.stop()
            time.sleep(0.5)
            results.append(check("processus reellement arrete",
                                 not process_alive(pid)))
            results.append(check("etat mis a jour apres arret", not proc.running))
            QTimer.singleShot(200, phase_closed_window)

        QTimer.singleShot(2500, detached_check)

    def phase_closed_window():
        # L'operateur peut fermer la fenetre console de Direwolf a la main.
        # Popen conserve un handle, donc poll() le remarque ; un simple PID
        # memorise ne l'aurait pas permis.
        import signal as signal_mod
        from ax25chess.direwolf import DirewolfProcess

        idle = Path(tempfile.mkdtemp()) / "direwolf"
        idle.write_text("#!/usr/bin/env python3\nimport time\n"
                        "while True: time.sleep(1)\n")
        idle.chmod(0o755)

        proc = DirewolfProcess()
        stops: list[int] = []
        proc.stopped.connect(stops.append)
        proc.start(str(idle), host="127.0.0.1", port=8099,
                   detached=True, probe_port=False)
        pid = proc.pid
        results.append(check("processus vivant apres lancement", proc.running))
        os.kill(pid, signal_mod.SIGTERM)

        def closed_check():
            results.append(check("fermeture de la fenetre remarquee",
                                 not proc.running and bool(stops)))
            failures: list[str] = []
            proc.failed.connect(failures.append)
            proc.stop()
            results.append(check("aucune fausse erreur sur un processus mort",
                                 not failures))
            QTimer.singleShot(200, phase4)

        QTimer.singleShot(3500, closed_check)

    def phase4():
        # --- 5. executable introuvable : l'application reste utilisable ---
        configure("/chemin/inexistant/direwolf", launch=True)
        win3 = MainWindow()

        def phase3_check():
            journal = win3.trace.toPlainText()
            results.append(check("erreur d'executable signalee a l'operateur",
                                 "introuvable" in journal))
            results.append(check("application toujours fonctionnelle",
                                 win3.isEnabled()))
            win3.close()
            print("\nTOUS LES TESTS PASSENT" if all(results)
                  else "\nDES TESTS ONT ECHOUE")
            app.exit(0 if all(results) else 1)

        QTimer.singleShot(2000, phase3_check)

    QTimer.singleShot(4000, phase1)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
