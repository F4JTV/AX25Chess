"""
net_link.py - Liaison TCP KISS avec Direwolf.

Le socket est non bloquant et pilote par les signaux Qt : pas de thread, donc
pas de section critique a proteger. Direwolf ecoute par defaut sur 8001 pour
le KISS TCP (`KISSPORT 8001` dans direwolf.conf).
"""

from __future__ import annotations

from PyQt6.QtCore import QObject, QTimer, pyqtSignal
from PyQt6.QtNetwork import QAbstractSocket, QTcpSocket

from .ax25_kiss import (AX25Error, KissDecoder, build_ui_frame, kiss_wrap,
                        parse_ui_frame)


class KissLink(QObject):
    connected = pyqtSignal()
    disconnected = pyqtSignal()
    failed = pyqtSignal(str)
    ax25_received = pyqtSignal(dict)     # {'src','dst','path','info'}
    bytes_sent = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.sock = QTcpSocket(self)
        self.decoder = KissDecoder()
        self.host = "127.0.0.1"
        self.port = 8001
        self.auto_reconnect = True
        self._want = False

        self.sock.connected.connect(self._on_connected)
        self.sock.disconnected.connect(self._on_disconnected)
        self.sock.readyRead.connect(self._on_ready)
        self.sock.errorOccurred.connect(self._on_error)

        self._retry = QTimer(self)
        # Assez court pour que la liaison suive le demarrage de Direwolf sans
        # attente perceptible : c'est cette liaison, et non une sonde, qui
        # sert de signal de disponibilite.
        self._retry.setInterval(1500)
        self._retry.timeout.connect(self._try_connect)

    # -- etat ---------------------------------------------------------------

    @property
    def online(self) -> bool:
        return self.sock.state() == QAbstractSocket.SocketState.ConnectedState

    @property
    def wanted(self) -> bool:
        """Vrai si l'operateur a demande la liaison, meme si elle est coupee."""
        return self._want

    def open(self, host: str, port: int) -> None:
        self.host, self.port = host, int(port)
        self._want = True
        self._try_connect()

    def close(self) -> None:
        self._want = False
        self._retry.stop()
        self.sock.abort()

    def _try_connect(self) -> None:
        if self.online or not self._want:
            return
        self.decoder = KissDecoder()
        self.sock.abort()
        self.sock.connectToHost(self.host, self.port)

    # -- evenements socket --------------------------------------------------

    def _on_connected(self) -> None:
        self._retry.stop()
        self.connected.emit()

    def _on_disconnected(self) -> None:
        self.disconnected.emit()
        if self._want and self.auto_reconnect:
            self._retry.start()

    def _on_error(self, err) -> None:
        self.failed.emit(self.sock.errorString())
        if self._want and self.auto_reconnect:
            self._retry.start()

    def _on_ready(self) -> None:
        data = bytes(self.sock.readAll())
        for frame in self.decoder.feed(data):
            parsed = parse_ui_frame(frame)
            if parsed:
                self.ax25_received.emit(parsed)

    # -- emission -----------------------------------------------------------

    def send_info(self, src: str, dst: str, info: bytes,
                  path: list[str] | None = None) -> bool:
        if not self.online:
            self.failed.emit("Direwolf non connecte : trame non emise")
            return False
        try:
            frame = build_ui_frame(src, dst, info, path)
        except AX25Error as exc:
            self.failed.emit(str(exc))
            return False
        payload = kiss_wrap(frame)
        self.sock.write(payload)
        self.sock.flush()
        self.bytes_sent.emit(len(payload))
        return True
