"""
direwolf.py - Demarrage et surveillance du processus Direwolf.

Deux difficultes propres a Windows ont dicte la conception de ce module.

1. La console est muette quand on la capture.
   Le CRT Microsoft bascule `stdout` en tampon de bloc (4 Ko) des lors qu'il
   n'ecrit plus vers une vraie console mais vers un tuyau. Direwolf n'emet pas
   4 Ko a son demarrage : ses lignes restent donc dans le tampon et n'arrivent
   jamais. Rien ne permet de forcer le vidage depuis l'exterieur sous Windows
   (pas d'equivalent a `stdbuf`). La reponse est de laisser Direwolf tourner
   dans sa propre fenetre console, ou tout s'affiche normalement.

   QProcess ne sait pas le faire, et deux tentatives l'ont prouve. Qt pose
   CREATE_NO_WINDOW sur les processus qu'il lance ; ajouter
   CREATE_NEW_CONSOLE via setCreateProcessArgumentsModifier ne l'annule pas,
   et startDetached() n'aide pas davantage, sa structure de demarrage
   demandant encore une fenetre cachee. Dans les deux cas Direwolf tourne,
   apparait dans le gestionnaire des taches, et n'affiche rien.

   Ce chemin n'utilise donc pas QProcess du tout. Il utilise subprocess.Popen
   avec creationflags=CREATE_NEW_CONSOLE et AUCUNE redirection des flux
   standards, ce qui est la methode documentee par Microsoft pour donner sa
   propre console a un processus fils. Ne rien rediriger est aussi important
   que le drapeau : rediriger stdout ou stderr envoie la sortie dans un tuyau
   au lieu de la nouvelle console, et la fenetre s'ouvrirait vide.

   Popen conserve en outre un handle, donc poll() dit reellement si Direwolf
   vit encore — ce qu'un simple PID memorise ne permet pas. Fermer sa fenetre
   console est donc remarque. L'arret passe par taskkill /PID <pid> /T /F :
   une application console ne traite pas le WM_CLOSE qu'un arret courtois
   posterait, et /T emporte la console avec elle.

2. On ne peut donc pas se fier a la console pour savoir si Direwolf est pret.
   La disponibilite se lit sur le port KISS, pas sur la console.

   Reste a savoir COMMENT on l'interroge, et ce detail compte. Une socket
   jetable fonctionne, mais du point de vue de Direwolf c'est un client KISS
   a part entiere : il journalise « Attached to KISS TCP client application N »
   puis « has gone away », et elle occupe un des trois emplacements
   qu'autorise MAX_NET_CLIENTS dans kiss_frame.h.

   Quand l'appelant va de toute facon ouvrir une vraie liaison KISS — ce que
   fait toujours l'application — il passe probe_port=False et appelle
   note_ready() des que cette liaison aboutit. Le signal de disponibilite est
   alors la connexion qu'on voulait etablir de toute maniere, et Direwolf ne
   voit qu'un seul client. La sonde ne subsiste que pour les appelants sans
   liaison propre, et pour verifier qu'un TNC ne tourne pas deja avant d'en
   lancer un second.
"""

from __future__ import annotations

import os
import re
import shutil
import signal
import socket
import sys
import time
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QObject, QProcess, QTimer, pyqtSignal

from .i18n import tr
from .resources import bundled_direwolf

IS_WINDOWS = sys.platform.startswith("win")

# Drapeau Win32 : donner au processus fils sa propre fenetre console.
CREATE_NEW_CONSOLE = 0x00000010

# Employe pour les utilitaires appeles en arriere-plan : sans lui, taskkill
# ferait apparaitre une fenetre de console noire le temps de son execution,
# l'application etant elle-meme sans console.
CREATE_NO_WINDOW = 0x08000000

# Delai laisse a un arret courtois, la ou il a un sens. Sous Windows il n'en a
# aucun pour une application console : terminate() y poste un WM_CLOSE que
# Direwolf ignore, et attendre trois secondes que rien ne se passe est le prix
# que payait la fermeture de l'application.
GRACE_MS = 300

# lignes annoncant que le serveur KISS TCP est operationnel
READY_PATTERNS = [
    re.compile(r"accept.*KISS.*(TCP|client)", re.I),
    re.compile(r"KISS TCP.*port", re.I),
    re.compile(r"Ready to accept", re.I),
]

FATAL_PATTERNS = [
    re.compile(r"Could not open audio device", re.I),
    re.compile(r"Pointless to continue", re.I),
    re.compile(r"Config file.*not found", re.I),
    re.compile(r"Couldn't open config file", re.I),
]

WINDOWS_GUESSES = [
    r"C:\Program Files\Direwolf\direwolf.exe",
    r"C:\Program Files (x86)\Direwolf\direwolf.exe",
    r"C:\Direwolf\direwolf.exe",
]


def find_direwolf() -> str:
    """Cherche un executable Direwolf plausible sur la machine.

    Une copie livree avec l'application l'emporte sur une trouvee dans le
    PATH : c'est celle dont la version a ete essayee avec ce build, et
    preferer le PATH utiliserait silencieusement ce que l'operateur a pu
    installer par ailleurs.
    """
    bundled = bundled_direwolf()
    if bundled:
        return bundled

    found = shutil.which("direwolf") or shutil.which("direwolf.exe")
    if found:
        return found
    if IS_WINDOWS:
        for guess in WINDOWS_GUESSES:
            if Path(guess).exists():
                return guess
    for guess in ("/usr/local/bin/direwolf", "/usr/bin/direwolf",
                  "/opt/direwolf/direwolf"):
        if Path(guess).exists():
            return guess
    return ""


def build_arguments(config: str = "", console_mode: bool = False,
                    extra_args: Optional[list[str]] = None) -> list[str]:
    """Arguments passes a Direwolf, dans l'ordre exact du lancement.

    `console_mode` a True retire `-t 0`. Ce drapeau supprime les couleurs
    ANSI : utile quand la sortie est capturee dans le journal, mais c'est du
    gachis dans une vraie console ou la couleur est justement souhaitable.
    """
    args: list[str] = [] if console_mode else ["-t", "0"]
    if config.strip():
        args += ["-c", config.strip()]
    args += list(extra_args or [])
    return args


def format_command(executable: str, args: list[str]) -> str:
    """Ligne de commande exacte, guillemets compris pour les chemins a espaces.

    Affichee dans l'interface : si la fenetre n'apparait pas, cette ligne dit
    tout de suite si le probleme vient de la commande ou du lancement, et elle
    peut etre collee telle quelle dans une invite de commandes.
    """
    def quote(part: str) -> str:
        return f'"{part}"' if " " in part else part
    return " ".join([quote(executable)] + [quote(a) for a in args])


def port_is_open(host: str, port: int, timeout: float = 0.4) -> bool:
    """Teste si un serveur KISS ecoute deja sur ce port.

    Sert a deux choses : ne pas lancer une seconde instance de Direwolf (deux
    processus se disputeraient la carte son), et savoir quand celui qu'on vient
    de lancer est reellement operationnel.
    """
    try:
        with socket.create_connection((host, int(port)), timeout=timeout):
            return True
    except OSError:
        return False


STARTER_CONFIG = """\
# direwolf.conf ecrit par AX25Chess.
# Reference complete : le guide utilisateur livre avec Direwolf.
#
# Editez au minimum le peripherique audio et la commande d'emission avant
# de passer sur l'air.

# --- peripherique audio ----------------------------------------------------
# Listez les peripheriques disponibles avec :  direwolf -p
ADEVICE  {adevice}
CHANNEL  0

# --- station ---------------------------------------------------------------
MYCALL   {callsign}

# 1200 bauds AFSK est le standard du packet VHF.
MODEM    1200

# --- commande d'emission ---------------------------------------------------
# Choisissez UNE de ces lignes selon votre interface. Le VOX ajoute un delai
# de retombee qui entre en collision avec la station suivante : une vraie
# ligne PTT est nettement preferable en packet.
{ptt}

# Marges d'emission, a allonger si le TX monte lentement en puissance.
TXDELAY  30
TXTAIL   10

# --- interface KISS, indispensable a AX25Chess -----------------------------
# Sans cette ligne, Direwolf demarre mais rien ne peut s'y connecter.
KISSPORT {kissport}
"""


def user_config_path(base_dir: Optional[str] = None) -> str:
    """Emplacement d'un direwolf.conf dont nous sommes proprietaires.

    Jamais a cote de l'executable : une installation machine atterrit sous
    Program Files, qui n'est pas inscriptible. Direwolf ne pourrait rien y
    ecrire et l'operateur ne pourrait pas l'editer sans elever ses droits.
    """
    if base_dir is None:
        from .games import STATE_DIR
        base_dir = str(STATE_DIR)
    return os.path.join(base_dir, "direwolf.conf")


def write_starter_config(path: str, callsign: str = "NOCALL",
                         kiss_port: int = 8001) -> str:
    """Ecrit une configuration minimale, sans jamais ecraser l'existante."""
    if os.path.isfile(path):
        return path
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    text = STARTER_CONFIG.format(
        adevice="0" if IS_WINDOWS else "plughw:1,0",
        callsign=(callsign or "NOCALL").upper(),
        kissport=int(kiss_port),
        ptt=("#PTT      COM3 RTS" if IS_WINDOWS
             else "#PTT      /dev/ttyUSB0 RTS"),
    )
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write(text)
    return path


class DirewolfProcess(QObject):
    """Enveloppe QProcess : demarrage, console, detection de disponibilite."""

    output = pyqtSignal(str)          # une ligne de console
    started = pyqtSignal()
    ready = pyqtSignal()              # port KISS operationnel
    stopped = pyqtSignal(int)         # code de retour
    failed = pyqtSignal(str)

    PROBE_MS = 500                    # cadence de sondage du port KISS
    GIVE_UP_MS = 60000                # au-dela, on previent l'operateur
    SILENCE_MS = 5000                 # au-dela, on explique la console vide

    def __init__(self, parent=None):
        super().__init__(parent)
        self.proc = QProcess(self)
        self.proc.readyReadStandardOutput.connect(self._on_output)
        self.proc.started.connect(self._on_started)
        self.proc.finished.connect(self._on_finished)
        self.proc.errorOccurred.connect(self._on_error)

        self._is_ready = False
        self._buffer = ""
        self._got_output = False
        self._detached_console = False
        self._popen = None                 # subprocess.Popen en mode console
        self._probe_enabled = True

        # Popen n'emet aucun signal : on interroge poll() pour savoir si la
        # fenetre console a ete fermee a la main.
        self._watch = QTimer(self)
        self._watch.setInterval(2000)
        self._watch.timeout.connect(self._watch_console)
        self._elapsed = 0
        self._host = "127.0.0.1"
        self._port = 8001

        self._probe = QTimer(self)
        self._probe.setInterval(self.PROBE_MS)
        self._probe.timeout.connect(self._probe_port)

        self._silence = QTimer(self)
        self._silence.setSingleShot(True)
        self._silence.timeout.connect(self._explain_silence)

    # -- etat ---------------------------------------------------------------

    @property
    def running(self) -> bool:
        if self._popen is not None:
            return self._popen.poll() is None
        return self.proc.state() != QProcess.ProcessState.NotRunning

    @property
    def detached(self) -> bool:
        """Vrai si Direwolf tourne dans sa propre fenetre console."""
        return self._popen is not None

    @property
    def pid(self) -> Optional[int]:
        if self._popen is not None:
            return self._popen.pid
        pid = self.proc.processId()
        return int(pid) if pid else None

    @property
    def is_ready(self) -> bool:
        return self._is_ready

    # -- controle -----------------------------------------------------------

    def start(self, executable: str, config: str = "",
              host: str = "127.0.0.1", port: int = 8001,
              separate_console: bool = False,
              extra_args: Optional[list[str]] = None,
              detached: Optional[bool] = None,
              probe_port: bool = True) -> bool:
        """Lance Direwolf.

        `detached` force le mode fenetre separee ; par defaut il est deduit de
        `separate_console` et de la plateforme. Le parametre explicite sert aux
        tests, qui doivent pouvoir exercer ce chemin ailleurs que sous Windows.

        `probe_port` a False supprime le sondage periodique : l'appelant
        s'engage alors a signaler la disponibilite par note_ready() quand sa
        propre liaison KISS aboutit. C'est le mode normal de l'application,
        qui evite d'ouvrir une connexion parasite chez Direwolf.
        """
        if self.running:
            self.failed.emit(tr("Direwolf est deja en cours d'execution"))
            return False

        exe = Path(executable).expanduser()
        if not exe.exists():
            self.failed.emit(tr("Executable introuvable : {exe}", exe=exe))
            return False

        workdir = exe.parent
        cfg = (config or "").strip()
        if cfg:
            conf = Path(cfg).expanduser()
            if not conf.exists():
                self.failed.emit(tr("Fichier de configuration introuvable : {conf}", conf=conf))
                return False
            cfg = str(conf)
            workdir = conf.parent

        console_mode = (detached if detached is not None
                        else (separate_console and IS_WINDOWS))
        args = build_arguments(cfg, console_mode=console_mode,
                               extra_args=extra_args)

        self._is_ready = False
        self._buffer = ""
        self._got_output = False
        self._elapsed = 0
        self._popen = None
        self._probe_enabled = bool(probe_port)
        self._host, self._port = host, int(port)

        self._detached_console = console_mode

        self.output.emit(format_command(str(exe), args))
        if console_mode:
            return self._start_console(exe, args, workdir)

        self.proc.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.proc.setWorkingDirectory(str(workdir))
        self.proc.start(str(exe), args)
        return True

    def _start_console(self, exe: Path, args: list[str], workdir: Path) -> bool:
        """Lance Direwolf dans sa propre fenetre console.

        Deliberement pas QProcess : voyez la note en tete de module. Aucune
        redirection des flux standards, sans quoi la sortie partirait dans un
        tuyau au lieu de la nouvelle console et la fenetre s'ouvrirait vide.
        """
        import subprocess

        kwargs: dict = {}
        if IS_WINDOWS:
            kwargs["creationflags"] = CREATE_NEW_CONSOLE
        else:
            # Pas de console a ouvrir sous Unix, mais on detache quand meme le
            # processus de notre groupe pour que le comportement d'arret soit
            # comparable et que ce chemin reste testable hors Windows.
            kwargs["start_new_session"] = True

        try:
            popen = subprocess.Popen(
                [str(exe)] + list(args),
                cwd=str(workdir),
                # Aucun stdout/stderr/stdin ici : c'est essentiel.
                **kwargs)
        except OSError as exc:
            self.failed.emit(tr(
                "Direwolf n'a pas pu etre lance dans une fenetre separee : "
                "{exc}. Decochez l'option pour revenir a la console integree.",
                exc=exc))
            return False

        self._popen = popen
        self.output.emit(
            tr("[AX25Chess] Direwolf tourne dans sa propre fenetre console "
               "(PID {pid}). Sa sortie s'affiche la-bas, pas ici.",
               pid=popen.pid))
        self._watch.start()
        if self._probe_enabled:
            self._probe.start()
        self.started.emit()
        return True

    def _watch_console(self) -> None:
        """Detecte la fermeture de la fenetre console par l'operateur."""
        if self._popen is None:
            self._watch.stop()
            return
        code = self._popen.poll()
        if code is None:
            return
        self._watch.stop()
        self._probe.stop()
        self._popen = None
        self._is_ready = False
        self.stopped.emit(int(code))

    def stop(self, wait_ms: int = 200) -> None:
        """Arrete Direwolf sans faire attendre l'interface.

        Les attentes sont volontairement courtes : cette methode est appelee
        depuis closeEvent, sur le fil de l'interface, et chaque milliseconde
        passee ici est une milliseconde ou la fenetre reste a l'ecran apres
        que l'operateur a demande a quitter.
        """
        self._probe.stop()
        self._silence.stop()

        if self._popen is not None:
            self._stop_console()
            return

        if not self.running:
            return

        if IS_WINDOWS:
            # Inutile de demander poliment : une application console ne traite
            # pas le WM_CLOSE que terminate() posterait, et l'attente serait
            # perdue d'avance.
            self.proc.kill()
        else:
            self.proc.terminate()
            if not self.proc.waitForFinished(GRACE_MS):
                self.proc.kill()
        self.proc.waitForFinished(wait_ms)

    def _stop_console(self) -> None:
        """Arrete un Direwolf lance dans sa propre console."""
        import subprocess

        popen, self._popen = self._popen, None
        self._watch.stop()
        self._is_ready = False
        if popen is None:
            return

        if popen.poll() is not None:
            # Deja mort : la fenetre a ete fermee a la main. Ne pas journaliser
            # une fausse erreur pour un processus qui n'existe plus.
            self.stopped.emit(int(popen.returncode or 0))
            return

        pid = popen.pid
        if IS_WINDOWS:
            # /F car une application console ne traite pas le WM_CLOSE qu'un
            # arret courtois posterait ; /T pour emporter la console avec.
            #
            # Lance sans attendre sa fin : taskkill est un processus
            # independant qui fera son travail meme si nous avons deja quitte.
            # L'attendre ne ferait que retarder la fermeture de la fenetre.
            try:
                subprocess.Popen(["taskkill", "/PID", str(pid), "/T", "/F"],
                                 stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL,
                                 creationflags=CREATE_NO_WINDOW)
            except OSError:
                pass
        else:
            popen.terminate()
            deadline = time.monotonic() + GRACE_MS / 1000.0
            while popen.poll() is None and time.monotonic() < deadline:
                time.sleep(0.02)
            if popen.poll() is None:
                try:
                    popen.kill()
                except OSError:
                    pass
        self.output.emit(tr("[AX25Chess] Direwolf arrete (PID {pid}).", pid=pid))
        self.stopped.emit(0)

    # -- evenements ---------------------------------------------------------

    def _on_started(self) -> None:
        if self._probe_enabled:
            self._probe.start()
        if not self._detached_console:
            self._silence.start(self.SILENCE_MS)
        self.started.emit()

    def _on_output(self) -> None:
        raw = bytes(self.proc.readAllStandardOutput())
        if not raw:
            return
        self._got_output = True
        self._silence.stop()
        self._buffer += raw.decode("utf-8", "replace").replace("\r", "")
        while "\n" in self._buffer:
            line, self._buffer = self._buffer.split("\n", 1)
            line = line.rstrip()
            if not line:
                continue
            self.output.emit(line)
            if any(p.search(line) for p in FATAL_PATTERNS):
                self.failed.emit(line)
            if not self._is_ready and any(p.search(line) for p in READY_PATTERNS):
                self._mark_ready()

    def note_ready(self) -> None:
        """Signale de l'exterieur que le port KISS repond.

        Appele par l'application quand sa vraie liaison KISS aboutit, ce qui
        evite d'ouvrir une socket de sondage supplementaire.
        """
        self._mark_ready()

    def _mark_ready(self) -> None:
        if self._is_ready:
            return
        self._is_ready = True
        self._probe.stop()
        self.ready.emit()

    def _probe_port(self) -> None:
        """Sonde le port KISS : seule preuve fiable que Direwolf est pret."""
        self._elapsed += self.PROBE_MS
        if not self.running:
            self._probe.stop()
            return
        if self._detached_console and self._elapsed < self.PROBE_MS * 2:
            return          # laisser a la fenetre le temps de s'ouvrir
        if port_is_open(self._host, self._port):
            self.output.emit(tr("[AX25Chess] Port KISS {host}:{port} ouvert, Direwolf est pret.",
                                host=self._host, port=self._port))
            self._mark_ready()
            return
        if self._elapsed >= self.GIVE_UP_MS:
            self._probe.stop()
            self.failed.emit(
                tr("Direwolf tourne mais rien n'ecoute sur {host}:{port} apres "
                   "{delay} s. Verifiez la ligne `KISSPORT` de direwolf.conf.",
                   host=self._host, port=self._port,
                   delay=self.GIVE_UP_MS // 1000))

    def _explain_silence(self) -> None:
        """La console reste vide : on explique plutot que de laisser perplexe."""
        if self._got_output or not self.running or self._detached_console:
            return
        if IS_WINDOWS:
            self.output.emit(
                tr("[AX25Chess] Aucune sortie recue de Direwolf. Sous Windows, "
                "stdout passe en tampon de bloc quand il est capture : les "
                "lignes restent bloquees tant que 4 Ko ne sont pas accumules."))
            self.output.emit(
                tr("[AX25Chess] Cochez « Fenetre console Direwolf separee » dans "
                "l'onglet RADIO pour voir la console reelle. Cela ne gene en "
                "rien la liaison KISS, qui est detectee par sondage du port."))
        else:
            self.output.emit(
                tr("[AX25Chess] Aucune sortie recue de Direwolf pour l'instant."))

    def _on_finished(self, code: int, status) -> None:
        self._probe.stop()
        self._silence.stop()
        self._is_ready = False
        if self._buffer.strip():
            self.output.emit(self._buffer.strip())
            self._buffer = ""
        self.stopped.emit(code)

    def _on_error(self, err) -> None:
        self._probe.stop()
        self._silence.stop()
        messages = {
            QProcess.ProcessError.FailedToStart:
                tr("Direwolf n'a pas pu demarrer (chemin ou droits d'execution)"),
            QProcess.ProcessError.Crashed: tr("Direwolf s'est arrete anormalement"),
        }
        self.failed.emit(messages.get(err, self.proc.errorString()))
