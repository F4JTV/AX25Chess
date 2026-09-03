"""
main_window.py - Interface principale AX25Chess.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path
from typing import Optional

from PyQt6.QtCore import QSettings, Qt, QTimer
from PyQt6.QtGui import QFont
from PyQt6.QtWidgets import (QCheckBox, QComboBox, QFileDialog, QFormLayout,
                             QGridLayout, QGroupBox, QHBoxLayout, QHeaderView,
                             QLabel, QLineEdit, QMainWindow, QMessageBox,
                             QPlainTextEdit, QPushButton, QSpinBox,
                             QSplitter, QTableWidget, QTableWidgetItem,
                             QTabWidget, QVBoxLayout, QWidget)

from .board_widget import (C_ALERT, C_AMBER, C_CHASSIS, C_INK, C_LINE, C_MUTED,
                           C_ON_PRIMARY, C_OK, C_PANEL, BoardWidget,
                           CapturesBar, mono_family)
from .chess_rules import BLACK, WHITE, Board, sq_name, sq_number
from .direwolf import (DirewolfProcess, build_arguments, find_direwolf,
                       format_command, port_is_open, user_config_path,
                       write_starter_config)
from .resources import bundled_direwolf
from .game_manager import GameManagerDialog
from .games import STATE_DIR as STORE_DIR, GameStore
from .theme import (THEMES, apply_theme, current_theme, disabled_ink,
                    primary_hover)
from .i18n import (LANGUAGES, current_language, detect_language,
                   set_language, tr)
from .net_link import KissLink
from .protocol import (GameSession, SessionListener, position_hash)

APP_NAME = "AX25Chess"

# Espace de stockage des reglages. Neutre a dessein : dans un projet publie,
# tous les utilisateurs ecriraient sinon sous le nom de l'auteur.
SETTINGS_ORG = "AX25Chess"

STATE_DIR = STORE_DIR

def build_style() -> str:
    """Feuille de style du theme courant.

    Reconstruite a chaque changement : les couleurs y sont ecrites en
    dur par Qt, une simple mutation des QColor partages ne suffirait
    donc pas a la mettre a jour.
    """
    return f"""
QMainWindow, QWidget {{ background: {C_CHASSIS.name()}; color: {C_INK.name()}; }}
QGroupBox {{
    border: 1px solid {C_LINE.name()}; border-radius: 5px;
    margin-top: 14px; padding-top: 10px; font-size: 11px;
}}
QGroupBox::title {{
    subcontrol-origin: margin; left: 10px; padding: 0 5px;
    color: {C_MUTED.name()}; font-family: monospace;
    letter-spacing: 1px; text-transform: uppercase;
}}
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTableWidget {{
    background: {C_PANEL.name()}; color: {C_INK.name()};
    border: 1px solid {C_LINE.name()}; border-radius: 4px;
    padding: 4px; selection-background-color: {C_AMBER.name()};
    selection-color: {C_ON_PRIMARY.name()};
}}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {{ border: 1px solid {C_AMBER.name()}; }}
QPushButton {{
    background: {C_PANEL.name()}; color: {C_INK.name()};
    border: 1px solid {C_LINE.name()}; border-radius: 4px;
    padding: 6px 12px; font-size: 11px;
}}
QPushButton:hover:enabled {{ border: 1px solid {C_AMBER.name()}; color: {C_AMBER.name()}; }}
QPushButton:disabled {{ color: {disabled_ink().name()}; }}
QPushButton#primary {{ background: {C_AMBER.name()}; color: {C_ON_PRIMARY.name()}; border: none; font-weight: 600; }}
QPushButton#primary:hover {{ background: {primary_hover().name()}; }}
QPushButton#danger:hover {{ border: 1px solid {C_ALERT.name()}; color: {C_ALERT.name()}; }}
QTabWidget::pane {{ border: 1px solid {C_LINE.name()}; border-radius: 5px; top: -1px; }}
QTabBar::tab {{
    background: transparent; color: {C_MUTED.name()};
    padding: 7px 15px; border: 1px solid transparent;
    font-family: monospace; font-size: 11px; letter-spacing: 1px;
}}
QTabBar::tab:selected {{ color: {C_AMBER.name()}; border-bottom: 2px solid {C_AMBER.name()}; }}
QHeaderView::section {{
    background: {C_PANEL.name()}; color: {C_MUTED.name()};
    border: none; border-bottom: 1px solid {C_LINE.name()};
    padding: 5px; font-family: monospace; font-size: 10px;
}}
QTableWidget {{ gridline-color: {C_LINE.name()}; font-family: monospace; font-size: 11px; }}
QCheckBox {{ font-size: 11px; }}
QStatusBar {{ color: {C_MUTED.name()}; font-family: monospace; font-size: 11px; }}
QScrollBar:vertical {{ background: {C_CHASSIS.name()}; width: 10px; }}
QScrollBar::handle:vertical {{ background: {C_LINE.name()}; border-radius: 5px; min-height: 24px; }}
QScrollBar::add-line, QScrollBar::sub-line {{ height: 0; }}
"""


class Led(QLabel):
    """Temoin lumineux facon face avant d'emetteur."""

    def __init__(self, text: str, parent=None):
        super().__init__(text, parent)
        self.setFont(QFont(mono_family(), 10))
        self.set_state("off")

    def set_state(self, state: str) -> None:
        self._state = state
        color = {"on": C_OK, "warn": C_AMBER, "err": C_ALERT}.get(state, C_MUTED)
        self.setStyleSheet(f"color:{color.name()};")

    def refresh(self) -> None:
        """Reapplique la couleur apres un changement de theme."""
        self.set_state(getattr(self, "_state", "off"))


class MainWindow(QMainWindow, SessionListener):

    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"{APP_NAME} - echecs par radio packet AX.25")
        self.resize(1240, 800)
        # « monospace » ne se resout que via fontconfig, absent sous Windows :
        # on injecte une famille reellement installee.
        self.settings = QSettings(SETTINGS_ORG, APP_NAME)
        self.link = KissLink(self)
        self.direwolf = DirewolfProcess(self)
        self.store = GameStore()
        self._startup_done = False
        self.session: Optional[GameSession] = None
        self.pending_local_uid: Optional[str] = None

        # La langue doit etre fixee AVANT de construire l'interface : les
        # libelles sont traduits a la creation de chaque widget.
        self._first_run = not self.settings.contains("configured")
        set_language(self.settings.value("language", detect_language()))
        # Meme raison que pour la langue : la feuille de style et les couleurs
        # sont lues a la creation des widgets.
        apply_theme(self.settings.value("theme", "dark"))
        # Apres apply_theme, jamais avant : la feuille de style fige les
        # couleurs en dur, et la construire trop tot laisserait une fenetre
        # sombre autour d'un echiquier clair.
        self._apply_style()

        self._build_ui()
        self._load_settings()
        self._apply_first_run_defaults()
        self._refresh_command()

        self.link.connected.connect(self._on_link_up)
        self.link.disconnected.connect(self._on_link_down)
        self.link.failed.connect(lambda m: self.log("err", m))
        self.link.ax25_received.connect(self._on_ax25)

        self.direwolf.output.connect(self._on_dw_output)
        self.direwolf.started.connect(self._on_dw_started)
        self.direwolf.ready.connect(self._on_dw_ready)
        self.direwolf.stopped.connect(self._on_dw_stopped)
        self.direwolf.failed.connect(self._on_dw_failed)

        self._kiss_timeout = QTimer(self)
        self._kiss_timeout.setSingleShot(True)
        self._kiss_timeout.timeout.connect(self._on_kiss_timeout)

        self.clock = QTimer(self)
        self.clock.setInterval(1000)
        self.clock.timeout.connect(self._tick)
        self.clock.start()

        self.refresh()
        QTimer.singleShot(200, self._startup)
        QTimer.singleShot(400, self._announce_saved_games)

    # ------------------------------------------------------------------ UI

    def _build_ui(self) -> None:
        split = QSplitter(Qt.Orientation.Horizontal)
        split.addWidget(self._build_left())
        split.addWidget(self._build_right())
        split.setStretchFactor(0, 3)
        split.setStretchFactor(1, 2)
        split.setSizes([720, 500])
        self.setCentralWidget(split)

        self.sb_turn = QLabel("-")
        self.sb_hash = QLabel("-")
        self.sb_ack = QLabel("")
        for w in (self.sb_turn, self.sb_hash, self.sb_ack):
            w.setFont(QFont(mono_family(), 10))
        self.statusBar().addWidget(self.sb_turn, 2)
        self.statusBar().addPermanentWidget(self.sb_ack)
        self.statusBar().addPermanentWidget(self.sb_hash)

    def _build_left(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(12, 10, 8, 10)
        lay.setSpacing(6)

        head = QHBoxLayout()
        title = QLabel("AX25CHESS")
        title.setFont(QFont(mono_family(), 15, QFont.Weight.Bold))
        title.setStyleSheet(f"color:{C_AMBER.name()};letter-spacing:4px;")
        self.led_dw = Led(tr("* DIREWOLF"))
        self.led_link = Led(tr("* LIAISON"))
        self.led_turn = Led(tr("* TRAIT"))
        head.addWidget(title)
        head.addStretch(1)
        head.addWidget(self.led_dw)
        head.addSpacing(12)
        head.addWidget(self.led_link)
        head.addSpacing(12)
        head.addWidget(self.led_turn)
        lay.addLayout(head)

        self.cap_black = CapturesBar(BLACK)
        self.cap_white = CapturesBar(WHITE)
        self.board_view = BoardWidget()
        self.board_view.move_requested.connect(self._on_move_requested)
        self.board_view.square_hovered.connect(self._on_hover)

        lay.addWidget(self.cap_black)
        lay.addWidget(self.board_view, 1)
        lay.addWidget(self.cap_white)

        self.hint = QLabel(tr("Configurez la radio puis lancez une invitation."))
        self.hint.setFont(QFont(mono_family(), 10))
        self.hint.setStyleSheet(f"color:{C_MUTED.name()};")
        lay.addWidget(self.hint)

        opts = QHBoxLayout()
        self.chk_uid = QCheckBox(tr("Identifiants des pieces"))
        self.chk_uid.setChecked(True)
        self.chk_num = QCheckBox(tr("Numeros de case"))
        self.chk_num.setChecked(True)
        self.btn_flip = QPushButton(tr("Retourner l'echiquier"))
        self.chk_uid.toggled.connect(lambda v: (setattr(self.board_view, "show_uids", v),
                                                self.board_view.update()))
        self.chk_num.toggled.connect(lambda v: (setattr(self.board_view, "show_numbers", v),
                                                self.board_view.update()))
        self.btn_flip.clicked.connect(self._flip)
        opts.addWidget(self.chk_uid)
        opts.addWidget(self.chk_num)
        opts.addStretch(1)
        opts.addWidget(self.btn_flip)
        lay.addLayout(opts)
        return w

    def _build_right(self) -> QWidget:
        tabs = QTabWidget()
        tabs.addTab(self._tab_game(), tr("PARTIE"))
        tabs.addTab(self._tab_radio(), tr("RADIO"))
        tabs.addTab(self._tab_frames(), tr("TRAMES"))
        tabs.addTab(self._tab_direwolf(), tr("DIREWOLF"))
        tabs.addTab(self._tab_chat(), tr("MESSAGES"))
        self.tabs = tabs
        return tabs

    def _tab_game(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(10, 12, 10, 10)

        box = QGroupBox(tr("Etat de la partie"))
        g = QGridLayout(box)
        self.lbl_gid = QLabel("-")
        self.lbl_color = QLabel("-")
        self.lbl_peer = QLabel("-")
        self.lbl_state = QLabel(tr("hors partie"))
        for i, (k, v) in enumerate(((tr("Partie"), self.lbl_gid),
                                    (tr("Vos couleurs"), self.lbl_color),
                                    (tr("Correspondant"), self.lbl_peer),
                                    (tr("Situation"), self.lbl_state))):
            lab = QLabel(k)
            lab.setStyleSheet(f"color:{C_MUTED.name()};font-size:11px;")
            v.setFont(QFont(mono_family(), 11))
            g.addWidget(lab, i // 2, (i % 2) * 2)
            g.addWidget(v, i // 2, (i % 2) * 2 + 1)
        lay.addWidget(box)

        self.table = QTableWidget(0, 5)
        self.table.setHorizontalHeaderLabels(
            [tr("N°"), tr("Trait"), tr("Coup"), tr("Identifiant"), tr("Case")])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(QTableWidget.SelectionBehavior.SelectRows)
        hh = self.table.horizontalHeader()
        hh.setSectionResizeMode(QHeaderView.ResizeMode.Stretch)
        hh.setSectionResizeMode(0, QHeaderView.ResizeMode.ResizeToContents)
        hh.setSectionResizeMode(1, QHeaderView.ResizeMode.ResizeToContents)
        lay.addWidget(self.table, 1)

        row1 = QHBoxLayout()
        self.btn_invite = QPushButton(tr("Lancer une partie"))
        self.btn_invite.setObjectName("primary")
        self.btn_invite.clicked.connect(self._invite)
        self.btn_sync = QPushButton(tr("Resynchroniser"))
        self.btn_sync.clicked.connect(self._resync)
        row1.addWidget(self.btn_invite, 1)
        row1.addWidget(self.btn_sync)
        lay.addLayout(row1)

        row_games = QHBoxLayout()
        self.btn_games = QPushButton(tr("Parties en cours"))
        self.btn_games.setToolTip(
            "Liste des parties commencees et non terminees. Elles sont "
            "enregistrees apres chaque demi-coup et effacees des qu'une "
            "partie se termine.")
        self.btn_games.clicked.connect(self._open_games)
        row_games.addWidget(self.btn_games, 1)
        lay.addLayout(row_games)

        row2 = QHBoxLayout()
        self.btn_draw = QPushButton(tr("Proposer nulle"))
        self.btn_draw.clicked.connect(self._offer_draw)
        self.btn_resign = QPushButton(tr("Abandonner"))
        self.btn_resign.setObjectName("danger")
        self.btn_resign.clicked.connect(self._resign)
        row2.addWidget(self.btn_draw, 1)
        row2.addWidget(self.btn_resign, 1)
        lay.addLayout(row2)
        return w

    def _tab_radio(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(10, 12, 10, 10)

        box0 = QGroupBox(tr("Programme Direwolf"))
        f0 = QFormLayout(box0)
        self.ed_exe = QLineEdit()
        self.ed_exe.setPlaceholderText("chemin de l'executable direwolf")
        btn_exe = QPushButton(tr("Parcourir..."))
        btn_exe.clicked.connect(self._browse_exe)
        row_exe = QHBoxLayout()
        row_exe.addWidget(self.ed_exe, 1)
        row_exe.addWidget(btn_exe)
        f0.addRow(tr("Executable"), row_exe)

        self.ed_conf = QLineEdit()
        self.ed_conf.setPlaceholderText("direwolf.conf (facultatif)")
        btn_conf = QPushButton(tr("Parcourir..."))
        btn_conf.clicked.connect(self._browse_conf)
        btn_conf_new = QPushButton(tr("Creer"))
        btn_conf_new.setToolTip(
            "Ecrit un direwolf.conf minimal dans votre dossier de reglages, "
            "inscriptible quel que soit l'emplacement d'installation.")
        btn_conf_new.clicked.connect(self._create_conf)
        row_conf = QHBoxLayout()
        row_conf.addWidget(self.ed_conf, 1)
        row_conf.addWidget(btn_conf)
        row_conf.addWidget(btn_conf_new)
        f0.addRow(tr("Configuration"), row_conf)

        self.chk_console = QCheckBox(tr("Fenetre console Direwolf separee"))
        if sys.platform.startswith("win"):
            self.chk_console.setToolTip(
                "Direwolf recoit sa propre fenetre console, ou sa sortie "
                "s'affiche normalement. Sans cette option, Windows met stdout "
                "en tampon de bloc des qu'il est capture et l'onglet DIREWOLF "
                "reste vide.")
        else:
            self.chk_console.setEnabled(False)
            self.chk_console.setToolTip(
                "Option specifique a Windows. Sous Linux, la console de "
                "Direwolf est recopiee correctement dans l'onglet DIREWOLF.")
        self.chk_launch = QCheckBox(tr("Lancer Direwolf au demarrage de l'application"))
        self.chk_autoconnect = QCheckBox(tr("Se connecter automatiquement au port KISS"))
        self.chk_autoconnect.setChecked(True)
        f0.addRow(self.chk_launch)
        f0.addRow(self.chk_autoconnect)
        f0.addRow(self.chk_console)

        self.chk_stop_dw = QCheckBox(tr("Arreter Direwolf en quittant"))
        self.chk_stop_dw.setChecked(True)
        self.chk_stop_dw.setToolTip(
            "Decochez si vous preferez laisser Direwolf tourner apres avoir "
            "ferme AX25Chess, par exemple pour l'utiliser avec un autre "
            "programme.")
        f0.addRow(self.chk_stop_dw)

        self.ed_cmd = QLineEdit()
        self.ed_cmd.setReadOnly(True)
        self.ed_cmd.setToolTip(
            "Ligne de commande exacte qui sera executee. Si la fenetre "
            "n'apparait pas, collez-la telle quelle dans une invite de "
            "commandes pour savoir si le probleme vient de la commande ou "
            "du lancement.")
        f0.addRow(tr("Commande"), self.ed_cmd)

        for widget in (self.ed_exe, self.ed_conf):
            widget.textChanged.connect(self._refresh_command)
        self.chk_console.toggled.connect(self._refresh_command)

        self.btn_dw = QPushButton(tr("Demarrer Direwolf"))
        self.btn_dw.clicked.connect(self._toggle_direwolf)
        f0.addRow(self.btn_dw)
        lay.addWidget(box0)

        box = QGroupBox(tr("Liaison KISS TCP"))
        f = QFormLayout(box)
        self.ed_host = QLineEdit("127.0.0.1")
        self.sp_port = QSpinBox()
        self.sp_port.setRange(1, 65535)
        self.sp_port.setValue(8001)
        f.addRow(tr("Hote"), self.ed_host)
        f.addRow(tr("Port KISS"), self.sp_port)
        lay.addWidget(box)

        box2 = QGroupBox(tr("Station"))
        f2 = QFormLayout(box2)
        self.ed_call = QLineEdit("N0CALL")
        self.ed_peer = QLineEdit("")
        self.ed_path = QLineEdit("")
        self.ed_path.setPlaceholderText(tr("relais separes par une virgule, ex. WIDE1-1"))
        f2.addRow(tr("Votre indicatif"), self.ed_call)
        f2.addRow(tr("Correspondant"), self.ed_peer)
        f2.addRow(tr("Chemin"), self.ed_path)
        lay.addWidget(box2)

        box3 = QGroupBox(tr("Temporisation"))
        f3 = QFormLayout(box3)
        self.cb_theme = QComboBox()
        for code, label in THEMES.items():
            self.cb_theme.addItem(tr(label), code)
        self.cb_theme.setToolTip(tr(
            "Le theme clair est concu pour un ecran en plein soleil : "
            "contrastes renforces et accent plus fonce."))
        self.cb_theme.currentIndexChanged.connect(self._on_theme_changed)
        f3.addRow(tr("Theme"), self.cb_theme)

        self.cb_lang = QComboBox()
        for code, label in LANGUAGES.items():
            self.cb_lang.addItem(label, code)
        self.cb_lang.currentIndexChanged.connect(self._on_language_changed)
        f3.addRow(tr("Langue"), self.cb_lang)

        self.sp_retry = QSpinBox()
        self.sp_retry.setRange(5, 120)
        self.sp_retry.setValue(14)
        self.sp_retry.setSuffix(" s")
        f3.addRow(tr("Delai avant retransmission"), self.sp_retry)
        lay.addWidget(box3)

        row = QHBoxLayout()
        self.btn_connect = QPushButton(tr("Se connecter a Direwolf"))
        self.btn_connect.setObjectName("primary")
        self.btn_connect.clicked.connect(self._toggle_link)
        self.btn_ping = QPushButton(tr("Balise de test"))
        self.btn_ping.clicked.connect(self._ping)
        row.addWidget(self.btn_connect, 1)
        row.addWidget(self.btn_ping)
        lay.addLayout(row)

        note = QLabel(
            "Direwolf doit exposer un port KISS : la ligne `KISSPORT 8001`\n"
            "est obligatoire dans direwolf.conf. Si vous laissez l'application\n"
            "lancer Direwolf, sa console apparait dans l'onglet DIREWOLF.")
        note.setStyleSheet(f"color:{C_MUTED.name()};font-size:11px;")
        lay.addWidget(note)
        lay.addStretch(1)
        return w

    def _tab_frames(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(10, 12, 10, 10)
        self.trace = QPlainTextEdit()
        self.trace.setReadOnly(True)
        self.trace.setMaximumBlockCount(3000)
        self.trace.setFont(QFont(mono_family(), 10))
        lay.addWidget(self.trace, 1)
        row = QHBoxLayout()
        btn_clear = QPushButton(tr("Effacer"))
        btn_clear.clicked.connect(self.trace.clear)
        btn_save = QPushButton(tr("Exporter le journal"))
        btn_save.clicked.connect(self._export_log)
        row.addStretch(1)
        row.addWidget(btn_save)
        row.addWidget(btn_clear)
        lay.addLayout(row)
        return w

    def _tab_direwolf(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(10, 12, 10, 10)
        self.dw_console = QPlainTextEdit()
        self.dw_console.setReadOnly(True)
        self.dw_console.setMaximumBlockCount(2000)
        self.dw_console.setFont(QFont(mono_family(), 10))
        self.dw_console.setPlaceholderText(
            "Console de Direwolf, recopiee ici quand l'application lance "
            "elle-meme le programme.\n\n"
            "Sous Windows, cette recopie est peu fiable : le systeme met "
            "stdout en tampon de bloc des qu'il est capture, et les lignes "
            "n'arrivent que par paquets de 4 Ko. Preferez l'option\n"
            "« Fenetre console Direwolf separee » de l'onglet RADIO.\n\n"
            "Cela n'affecte pas la liaison : la disponibilite du port KISS "
            "est etablie par sondage, pas par lecture de la console.")
        lay.addWidget(self.dw_console, 1)
        row = QHBoxLayout()
        self.btn_dw2 = QPushButton(tr("Demarrer Direwolf"))
        self.btn_dw2.clicked.connect(self._toggle_direwolf)
        btn_clear = QPushButton(tr("Effacer"))
        btn_clear.clicked.connect(self.dw_console.clear)
        row.addWidget(self.btn_dw2, 1)
        row.addWidget(btn_clear)
        lay.addLayout(row)
        return w

    def _tab_chat(self) -> QWidget:
        w = QWidget()
        lay = QVBoxLayout(w)
        lay.setContentsMargins(10, 12, 10, 10)
        self.chat_view = QPlainTextEdit()
        self.chat_view.setReadOnly(True)
        self.chat_view.setFont(QFont(mono_family(), 11))
        lay.addWidget(self.chat_view, 1)
        row = QHBoxLayout()
        self.ed_chat = QLineEdit()
        self.ed_chat.setPlaceholderText(tr("message a votre correspondant"))
        self.ed_chat.returnPressed.connect(self._send_chat)
        btn = QPushButton(tr("Emettre"))
        btn.clicked.connect(self._send_chat)
        row.addWidget(self.ed_chat, 1)
        row.addWidget(btn)
        lay.addLayout(row)
        return w

    # ------------------------------------------------------- configuration

    def _load_settings(self) -> None:
        s = self.settings
        self.ed_host.setText(s.value("host", "127.0.0.1"))
        self.sp_port.setValue(int(s.value("port", 8001)))
        self.ed_call.setText(s.value("call", "N0CALL"))
        self.ed_peer.setText(s.value("peer", ""))
        self.ed_path.setText(s.value("path", ""))
        self.sp_retry.setValue(int(s.value("retry", 14)))
        self.ed_exe.setText(s.value("dw_exe", "") or find_direwolf())
        self.ed_conf.setText(s.value("dw_conf", ""))
        self.chk_launch.setChecked(s.value("dw_launch", False, type=bool))
        self.chk_autoconnect.setChecked(s.value("autoconnect", True, type=bool))
        self.chk_console.setChecked(
            s.value("dw_console", sys.platform.startswith("win"), type=bool))
        self.chk_stop_dw.setChecked(s.value("dw_stop", True, type=bool))
        index = self.cb_lang.findData(current_language())
        if index >= 0:
            self.cb_lang.setCurrentIndex(index)
        index = self.cb_theme.findData(current_theme())
        if index >= 0:
            self.cb_theme.setCurrentIndex(index)

    def _apply_style(self) -> None:
        self.setStyleSheet(build_style().replace(
            "font-family: monospace", f"font-family: '{mono_family()}'"))

    def _on_theme_changed(self) -> None:
        code = self.cb_theme.currentData()
        if not code or code == current_theme():
            return
        apply_theme(code)
        self.settings.setValue("theme", code)
        self._apply_style()
        # Les widgets peints a la main ne suivent pas la feuille de style :
        # il faut leur redemander un rendu.
        for widget in (self.board_view, self.cap_white, self.cap_black):
            widget.update()
        for led in (self.led_link, self.led_turn, self.led_dw):
            led.refresh()
        self.refresh()

    def _on_language_changed(self) -> None:
        code = self.cb_lang.currentData()
        if not code or code == current_language():
            return
        set_language(code)
        self.settings.setValue("language", code)
        self.retranslate()

    def retranslate(self) -> None:
        """Reapplique tous les libelles dans la langue courante.

        Reconstruire la fenetre serait plus simple mais ferait perdre l'etat
        de la partie en cours et le contenu des journaux.
        """
        for index, key in enumerate(("PARTIE", "RADIO", "TRAMES", "DIREWOLF",
                                     "MESSAGES")):
            self.tabs.setTabText(index, tr(key))
        for index in range(self.cb_theme.count()):
            code = self.cb_theme.itemData(index)
            self.cb_theme.setItemText(index, tr(THEMES[code]))
        for widget, text in self._retranslatable():
            widget.setText(tr(text))
        self.table.setHorizontalHeaderLabels(
            [tr("N°"), tr("Trait"), tr("Coup"), tr("Identifiant"), tr("Case")])
        self.led_link.setText(tr("* LIAISON"))
        self.led_turn.setText(tr("* TRAIT"))
        self.led_dw.setText(tr("* DIREWOLF"))
        self._refresh_games_button()
        self._refresh_command()
        self.refresh()

    def _retranslatable(self) -> list:
        """Widgets dont le libelle est fixe et doit suivre la langue."""
        return [
            (self.btn_invite, "Lancer une partie"),
            (self.btn_sync, "Resynchroniser"),
            (self.btn_draw, "Proposer nulle"),
            (self.btn_resign, "Abandonner"),
            (self.btn_flip, "Retourner l'echiquier"),
            (self.chk_uid, "Identifiants des pieces"),
            (self.chk_num, "Numeros de case"),
            (self.chk_launch, "Lancer Direwolf au demarrage de l'application"),
            (self.chk_autoconnect, "Se connecter automatiquement au port KISS"),
            (self.chk_console, "Fenetre console Direwolf separee"),
            (self.chk_stop_dw, "Arreter Direwolf en quittant"),
        ]

    def _refresh_command(self) -> None:
        """Tient a jour l'apercu de la ligne de commande."""
        exe = self.ed_exe.text().strip()
        if not exe:
            self.ed_cmd.setText("")
            return
        console = (self.chk_console.isChecked()
                   and sys.platform.startswith("win"))
        args = build_arguments(self.ed_conf.text().strip(), console_mode=console)
        self.ed_cmd.setText(format_command(exe, args))

    def _apply_first_run_defaults(self) -> None:
        """Configure une installation neuve pour le Direwolf qui l'accompagne.

        Ne s'execute que si aucun reglage n'a jamais ete enregistre : cette
        methode ne peut donc pas ecraser un choix deja fait par l'operateur.
        """
        if not self._first_run:
            return
        bundled = bundled_direwolf()
        if not bundled:
            return

        self.ed_exe.setText(bundled)
        self.chk_launch.setChecked(True)
        self.chk_autoconnect.setChecked(True)

        conf = user_config_path()
        try:
            write_starter_config(conf, self.ed_call.text().strip(),
                                 self.sp_port.value())
            self.ed_conf.setText(conf)
        except OSError as exc:
            self.log("warn", tr("Configuration de depart non ecrite : {exc}", exc=exc))
            return

        self.log("info", tr("Premier lancement : utilisation du Direwolf "
                            "installe avec AX25Chess"))
        self.log("warn", tr("Editez {conf} pour votre peripherique audio et votre commande "
                            "PTT avant d'emettre", conf=conf))

    def _create_conf(self) -> None:
        """Ecrit une configuration de depart la ou l'operateur peut l'editer."""
        path = user_config_path()
        existed = Path(path).is_file()
        try:
            write_starter_config(path, self.ed_call.text().strip(),
                                 self.sp_port.value())
        except OSError as exc:
            QMessageBox.critical(self, APP_NAME,
                                 tr("Ecriture impossible :\n{exc}", exc=exc))
            return
        self.ed_conf.setText(path)
        self._save_settings()
        QMessageBox.information(
            self, APP_NAME,
            (f"Un fichier existant a ete conserve :\n{path}" if existed
             else f"Fichier ecrit :\n{path}") +
            "\n\nEditez-le pour votre peripherique audio et votre commande "
            "PTT. Le numero de peripherique se liste avec « direwolf -p ».")

    def _save_settings(self) -> None:
        s = self.settings
        s.setValue("configured", True)
        s.setValue("host", self.ed_host.text())
        s.setValue("port", self.sp_port.value())
        s.setValue("call", self.ed_call.text().upper())
        s.setValue("peer", self.ed_peer.text().upper())
        s.setValue("path", self.ed_path.text().upper())
        s.setValue("retry", self.sp_retry.value())
        s.setValue("dw_exe", self.ed_exe.text().strip())
        s.setValue("dw_conf", self.ed_conf.text().strip())
        s.setValue("dw_launch", self.chk_launch.isChecked())
        s.setValue("autoconnect", self.chk_autoconnect.isChecked())
        s.setValue("dw_console", self.chk_console.isChecked())
        s.setValue("dw_stop", self.chk_stop_dw.isChecked())
        s.setValue("language", current_language())
        s.setValue("theme", current_theme())

    def _digi_path(self) -> list[str]:
        return [p.strip() for p in self.ed_path.text().split(",") if p.strip()]

    # ------------------------------------------------------------- liaison

    def _startup(self) -> None:
        """Sequence automatique au lancement de l'application.

        Ne s'execute qu'une fois, meme si elle est declenchee a nouveau.
        """
        if self._startup_done:
            return
        self._startup_done = True
        if self.chk_launch.isChecked():
            if not self.ed_exe.text().strip():
                self.log("warn", "Lancement automatique demande mais aucun "
                                 "executable Direwolf n'est renseigne")
            elif self._start_direwolf(silent=True):
                return          # _start_direwolf a deja ouvert la liaison
            # sinon : la raison a deja ete journalisee, on tente la connexion
            # directe au cas ou un Direwolf serait accessible malgre tout
        if self.chk_autoconnect.isChecked():
            self._open_link()

    def _browse_exe(self) -> None:
        start = self.ed_exe.text().strip() or str(Path.home())
        filt = ("Executables (*.exe);;Tous les fichiers (*)"
                if sys.platform.startswith("win") else tr("Tous les fichiers (*)"))
        path, _ = QFileDialog.getOpenFileName(
            self, tr("Executable Direwolf"), start, filt)
        if path:
            self.ed_exe.setText(path)
            self._save_settings()
            self.log("info", f"Executable Direwolf : {path}")

    def _browse_conf(self) -> None:
        start = self.ed_conf.text().strip() or str(Path.home())
        path, _ = QFileDialog.getOpenFileName(
            self, tr("Fichier de configuration Direwolf"), start,
            "Configuration (*.conf);;Tous les fichiers (*)")
        if path:
            self.ed_conf.setText(path)
            self._save_settings()
            self.log("info", f"Configuration Direwolf : {path}")

    def _toggle_direwolf(self) -> None:
        if self.direwolf.running:
            self.link.close()
            self.direwolf.stop()
        else:
            self._start_direwolf()

    def _start_direwolf(self, silent: bool = False) -> bool:
        if self.direwolf.running:
            self.log("info", tr("Direwolf est deja lance par l'application"))
            return True

        self._save_settings()
        exe = self.ed_exe.text().strip()
        if not exe:
            if not silent:
                QMessageBox.warning(self, APP_NAME,
                                    tr("Indiquez le chemin de l'executable Direwolf."))
                self.tabs.setCurrentIndex(1)
            return False

        host, port = self.ed_host.text().strip(), self.sp_port.value()
        if port_is_open(host, port):
            self.log("info", tr("Un serveur KISS repond deja sur {host}:{port} : Direwolf ne "
                                "sera pas relance", host=host, port=port))
            self.led_dw.set_state("warn")
            return False

        self.dw_console.clear()
        # Si nous allons ouvrir une vraie liaison KISS, c'est elle qui servira
        # de signal de disponibilite. Sonder le port en plus ferait apparaitre
        # un second client chez Direwolf, qui n'en accepte que trois.
        will_link = self.chk_autoconnect.isChecked()
        started = self.direwolf.start(
            exe, self.ed_conf.text().strip(), host=host, port=port,
            separate_console=self.chk_console.isChecked(),
            probe_port=not will_link)
        if started and will_link:
            self._open_link()
            self._arm_kiss_timeout()
        return started

    def _arm_kiss_timeout(self) -> None:
        """Previent si le port KISS ne repond jamais, sans rien sonder."""
        self._kiss_timeout.start(60000)

    def _on_kiss_timeout(self) -> None:
        if self.link.online:
            return
        host, port = self.ed_host.text().strip(), self.sp_port.value()
        self.log("err", tr("Direwolf tourne mais rien n'ecoute sur {host}:{port} apres "
                           "60 s. Verifiez la ligne `KISSPORT` de direwolf.conf.",
                           host=host, port=port))

    def _on_dw_output(self, line: str) -> None:
        self.dw_console.appendPlainText(line)

    def _on_dw_started(self) -> None:
        self.led_dw.set_state("warn")
        self._set_dw_buttons(tr("Arreter Direwolf"))
        self.log("info", tr("Direwolf demarre, attente du port KISS..."))

    def _on_dw_ready(self) -> None:
        self.led_dw.set_state("on")
        self.log("info", tr("Direwolf est pret"))
        if self.chk_autoconnect.isChecked():
            self._open_link()

    def _on_dw_stopped(self, code: int) -> None:
        self.led_dw.set_state("off")
        self._set_dw_buttons(tr("Demarrer Direwolf"))
        self.log("warn" if code else "info",
                 tr("Direwolf s'est arrete (code {code})", code=code))

    def _on_dw_failed(self, message: str) -> None:
        self.led_dw.set_state("err")
        self.log("err", tr("Direwolf : {message}", message=message))

    def _set_dw_buttons(self, text: str) -> None:
        for btn in (self.btn_dw, self.btn_dw2):
            btn.setText(text)

    def _open_link(self) -> None:
        """Ouvre la liaison KISS, sans jamais doubler une tentative en cours.

        Un second appel relancerait link.open(), qui abandonne la socket
        precedente : Direwolf verrait un client partir puis un autre arriver,
        exactement le « has gone away » observe.
        """
        self._save_settings()
        if self.link.online or self.link.wanted:
            return
        self.link.open(self.ed_host.text().strip(), self.sp_port.value())
        self.log("info", tr("Connexion a {host}:{port}...", host=self.ed_host.text(),
                         port=self.sp_port.value()))
        self.refresh()

    def _toggle_link(self) -> None:
        if self.link.online or self.link.wanted:
            self.link.close()
            self.led_link.set_state("off")
            self.btn_connect.setText(tr("Se connecter a Direwolf"))
            self.log("info", tr("Liaison KISS fermee"))
            return
        self._open_link()

    def _on_link_up(self) -> None:
        self.led_link.set_state("on")
        self.btn_connect.setText(tr("Se deconnecter"))
        self.log("info", tr("Direwolf connecte"))
        # C'est cette connexion, et non une sonde, qui prouve que le port KISS
        # repond : on en informe le superviseur de processus.
        self.direwolf.note_ready()
        self._kiss_timeout.stop()
        self.refresh()

    def _on_link_down(self) -> None:
        self.led_link.set_state("err")
        self.btn_connect.setText(tr("Se connecter a Direwolf"))
        self.log("warn", tr("Direwolf deconnecte"))
        self.refresh()

    def _on_ax25(self, parsed: dict) -> None:
        if self.session is None:
            self._ensure_session()
        self.session.feed(parsed["info"], parsed["src"], now=time.monotonic())

    def _ping(self) -> None:
        self._ensure_session()
        from .protocol import T_PING
        self.session._send(T_PING, f"{int(time.time()) & 0xFFFF:04X}",
                           reliable=False, now=time.monotonic())

    # -------------------------------------------------------------- partie

    def _ensure_session(self) -> GameSession:
        call = self.ed_call.text().strip().upper()
        peer = self.ed_peer.text().strip().upper()
        if self.session is None or self.session.my_call != call \
                or self.session.peer_call != peer:
            self.session = GameSession(call, peer, self)
        return self.session

    def _invite(self) -> None:
        if not self.ed_peer.text().strip():
            QMessageBox.warning(self, APP_NAME, tr(
                "Indiquez l'indicatif de votre correspondant "
                "dans l'onglet RADIO."))
            self.tabs.setCurrentIndex(1)
            return
        if not self.link.online:
            QMessageBox.warning(self, APP_NAME,
                                tr("Connectez-vous d'abord a Direwolf."))
            self.tabs.setCurrentIndex(1)
            return
        self._save_settings()
        self.session = GameSession(self.ed_call.text().strip().upper(),
                                   self.ed_peer.text().strip().upper(), self)
        self.table.setRowCount(0)
        self.session.invite(now=time.monotonic())

    def _resync(self) -> None:
        if self.session:
            self.session.request_sync(now=time.monotonic())

    def _offer_draw(self) -> None:
        if self.session:
            self.session.offer_draw(now=time.monotonic())

    def _resign(self) -> None:
        if not self.session or self.session.state != GameSession.PLAYING:
            return
        if QMessageBox.question(self, APP_NAME, tr("Confirmez-vous l'abandon ?")) \
                == QMessageBox.StandardButton.Yes:
            self.session.resign(now=time.monotonic())

    def _send_chat(self) -> None:
        text = self.ed_chat.text().strip()
        if text and self.session:
            self.session.send_chat(text, now=time.monotonic())
            self.ed_chat.clear()

    def _flip(self) -> None:
        self.board_view.orientation = (BLACK if self.board_view.orientation == WHITE
                                       else WHITE)
        self.board_view.update()

    def _on_move_requested(self, uid: str, sq: int, promo: str) -> None:
        if not self.session:
            return
        self.session.play_local(uid, sq, promo or None, now=time.monotonic())

    def _on_hover(self, sq: int) -> None:
        if self.session and self.session.state == GameSession.PLAYING:
            piece = self.session.board.piece_at(sq)
            tag = f"  {piece.uid}" if piece else ""
            self.sb_hash.setText(tr("case {num} ({name}){tag}", num=sq_number(sq), name=sq_name(sq), tag=tag))

    def _tick(self) -> None:
        if self.session:
            from . import protocol
            protocol.RETRY_SECONDS = float(self.sp_retry.value())
            self.session.tick(time.monotonic())
            if self.session.pending:
                ob = self.session.pending
                self.sb_ack.setText(
                    tr("attente ACK {type} seq={seq} ({n}/6)", type=ob.frame.type,
                       seq=ob.frame.seq, n=ob.attempts))
            else:
                self.sb_ack.setText("")

    # ------------------------------------------- rappels de la session (SessionListener)

    def on_send(self, frame) -> None:
        ok = self.link.send_info(frame.src, frame.dst, frame.encode(),
                                 self._digi_path())
        self.log("tx" if ok else "err", f"> {frame.text()}")

    def on_log(self, level: str, text: str) -> None:
        self.log(level, text)

    def on_state(self) -> None:
        self.refresh()

    def on_move_applied(self, move, by_peer: bool) -> None:
        ply = len(self.session.board.moves)
        row = self.table.rowCount()
        self.table.insertRow(row)
        vals = [str((ply + 1) // 2), tr("Blancs") if ply % 2 else tr("Noirs"),
                move.san_text or str(move), move.uid,
                f"{sq_number(move.to)} ({sq_name(move.to)})"]
        for c, v in enumerate(vals):
            item = QTableWidgetItem(v)
            if c == 3:
                item.setForeground(C_AMBER)
            self.table.setItem(row, c, item)
        self.table.scrollToBottom()
        self.board_view.set_last_move(move.frm, move.to)
        self._autosave()

    def on_chat(self, who: str, text: str) -> None:
        self.chat_view.appendPlainText(f"{time.strftime('%H:%M')} {who:>9} | {text}")
        if self.tabs.currentIndex() != 3:
            self.tabs.setTabText(3, tr("MESSAGES *"))

    def on_game_over(self, code: str, text: str) -> None:
        # Le rangement se fait tout de suite, l'annonce est differee : voyez
        # _show_later.
        self.log("info", tr("Fin de partie : {text}", text=text))
        if self.session:
            self.store.delete(self.session.gid, self.session.peer_call)
        self._refresh_games_button()
        self._show_later(
            lambda: QMessageBox.information(self, APP_NAME, text))

    def on_draw_offer(self) -> None:
        self._show_later(self._ask_draw)

    def _ask_draw(self) -> None:
        if not self.session or not self.session.draw_offered_by_peer:
            return
        answer = QMessageBox.question(
            self, APP_NAME,
            tr("Votre correspondant propose la nulle. Acceptez-vous ?"))
        self.session.answer_draw(answer == QMessageBox.StandardButton.Yes,
                                 now=time.monotonic())

    @staticmethod
    def _show_later(callback) -> None:
        """Affiche une boite de dialogue apres le rappel reseau en cours.

        Ces rappels arrivent depuis feed(), lui-meme appele par le lecteur de
        socket. Une boite modale y fait tourner une boucle d'evenements : des
        trames peuvent arriver et rentrer une seconde fois dans feed() pendant
        que la question est affichee, sur un etat a demi modifie. Le delai de
        zero milliseconde suffit a rendre la main avant d'ouvrir la fenetre.
        """
        QTimer.singleShot(0, callback)

    # ------------------------------------------------------------ affichage

    def log(self, level: str, text: str) -> None:
        color = {"tx": C_AMBER, "rx": C_OK, "err": C_ALERT,
                 "warn": "#D9A441", "info": C_MUTED}.get(level, C_MUTED)
        name = color if isinstance(color, str) else color.name()
        stamp = time.strftime("%H:%M:%S")
        self.trace.appendHtml(
            f'<span style="color:#5A6875">{stamp}</span> '
            f'<span style="color:{name}">{text}</span>')

    def refresh(self) -> None:
        s = self.session
        self.led_link.set_state("on" if self.link.online
                                else "warn" if self.link.wanted else "off")
        self.btn_connect.setText(tr("Se deconnecter") if self.link.wanted
                                 else tr("Se connecter a Direwolf"))
        if s is None:
            self.board_view.interactive = False
            self.board_view.set_board(Board())
            self.cap_white.set_board(self.board_view.board)
            self.cap_black.set_board(self.board_view.board)
            self.sb_turn.setText(tr("hors partie"))
            self.hint.setText(tr("Configurez la radio puis lancez une invitation."))
            return

        self.board_view.board = s.board
        self.cap_white.set_board(s.board)
        self.cap_black.set_board(s.board)
        if s.my_color:
            self.board_view.orientation = s.my_color

        self.lbl_gid.setText(s.gid)
        self.lbl_peer.setText(s.peer_call or "-")
        self.lbl_color.setText({WHITE: tr("Blancs"), BLACK: tr("Noirs")}.get(s.my_color, "-"))

        code, label = s.board.status()
        self.lbl_state.setText(s.result or label)

        mine = s.my_turn()
        self.board_view.interactive = mine
        self.led_turn.set_state("on" if mine else
                                "warn" if s.state == GameSession.PLAYING else "off")
        self.btn_resign.setEnabled(s.state == GameSession.PLAYING)
        self.btn_draw.setEnabled(s.state == GameSession.PLAYING)
        self.btn_sync.setEnabled(s.state == GameSession.PLAYING)

        if s.state == GameSession.PLAYING:
            trait = tr("Blancs") if s.board.turn == WHITE else tr("Noirs")
            self.sb_turn.setText(
                tr("trait aux {trait} | coup {move} | {plies} demi-coups",
                   trait=trait, move=s.board.fullmove, plies=len(s.board.moves)))
            if s.pending is not None:
                self.hint.setText(tr("Coup emis, en attente de l'acquittement radio."))
            elif mine:
                self.hint.setText(tr("A vous de jouer : cliquez une piece puis sa case."))
            else:
                self.hint.setText(tr("En attente du coup de votre correspondant."))
        elif s.state == GameSession.HANDSHAKE:
            self.sb_turn.setText(tr("negociation des couleurs..."))
            self.hint.setText(tr("Invitation emise, en attente de la reponse."))
        else:
            self.sb_turn.setText(s.result or tr("hors partie"))

        self.sb_hash.setText(tr("empreinte {hash}", hash=position_hash(s.board)))
        self.board_view.update()

    # ------------------------------------------------------- sauvegarde

    def _autosave(self) -> None:
        """Enregistre la partie apres chaque demi-coup."""
        s = self.session
        if not s or s.state != GameSession.PLAYING:
            return
        from .protocol import compact_move
        self.store.save(
            gid=s.gid, my_call=s.my_call, peer_call=s.peer_call,
            color=s.my_color or "W",
            moves=[compact_move(m) for m in s.board.moves],
            nonce=s.my_nonce, peer_nonce=s.peer_nonce, seq=s.seq)
        self._refresh_games_button()

    def _refresh_games_button(self) -> None:
        count = self.store.count()
        self.btn_games.setText(tr("Parties en cours ({count})", count=count) if count
                               else tr("Parties en cours"))
        self.btn_games.setEnabled(count > 0)

    def _announce_saved_games(self) -> None:
        """Signale les parties enregistrees sans rien imposer a l'operateur.

        L'ancienne question posee au demarrage revenait a chaque lancement,
        puisque repondre « non » ne supprimait rien. Ici, l'information est
        simplement portee par le bouton et une ligne de journal.
        """
        self._refresh_games_button()
        games = self.store.list()
        if not games:
            return
        waiting = sum(1 for g in games if g.my_turn)
        detail = tr(", dont {waiting} en attente de votre coup", waiting=waiting) if waiting else ""
        self.log("info", tr("{count} partie(s) en cours{detail} - bouton "
                            "« Parties en cours » de l'onglet PARTIE",
                            count=len(games), detail=detail))

    def _open_games(self) -> None:
        dialog = GameManagerDialog(
            self.store, self.session.gid if self.session else "", self)
        dialog.exec()
        self._refresh_games_button()
        if dialog.chosen is not None:
            self._resume_game(dialog.chosen)

    def _resume_game(self, saved) -> None:
        """Rejoue l'historique enregistre pour restaurer la partie."""
        if self.session and self.session.state == GameSession.PLAYING \
                and self.session.gid != saved.gid:
            if QMessageBox.question(
                    self, APP_NAME,
                    tr("Une partie contre {peer} est en cours. Elle reste enregistree, "
                       "mais sera fermee ici. Continuer ?",
                       peer=self.session.peer_call)) \
                    != QMessageBox.StandardButton.Yes:
                return

        from .chess_rules import number_to_sq
        from .protocol import parse_compact

        s = GameSession(saved.my_call, saved.peer_call, self)
        s.gid = saved.gid
        s.my_color = saved.color
        s.my_nonce = saved.nonce
        s.peer_nonce = saved.peer_nonce
        s.seq = saved.seq

        replayed = []
        for token in saved.moves:
            try:
                uid, num, promo = parse_compact(token)
                m = s.board.find_move(uid, number_to_sq(num), promo)
            except Exception:
                m = None
            if m is None:
                self.log("err", tr("Partie {gid} : historique incoherent au coup « {token} », "
                                   "reprise abandonnee", gid=saved.gid, token=token))
                QMessageBox.warning(
                    self, APP_NAME,
                    tr("L'historique de la partie {gid} est incoherent et ne peut pas "
                       "etre rejoue. Vous pouvez la supprimer depuis le "
                       "gestionnaire de parties.", gid=saved.gid))
                return
            m.san_text = s.board.san(m)
            s.board.push(m)
            replayed.append(m)

        self.session = s
        s.state = GameSession.PLAYING
        self.table.setRowCount(0)
        for m in replayed:
            self.on_move_applied_silent(m, s)

        self.ed_call.setText(saved.my_call)
        self.ed_peer.setText(saved.peer_call)
        self._save_settings()
        trait = tr("a vous de jouer") if s.board.turn == s.my_color \
            else tr("au correspondant de jouer")
        self.log("info", tr("Partie {gid} contre {peer} reprise sur {plies} demi-coups - {trait}",
                            gid=s.gid, peer=s.peer_call, plies=len(replayed),
                            trait=trait))
        self._check_resumed_end(s)
        self.refresh()

    def _check_resumed_end(self, session) -> None:
        """Une partie reprise peut deja etre terminee (mat, pat, nulle)."""
        code, label = session.board.status()
        if code != "playing":
            session.state = GameSession.OVER
            session.result = label
            self.log("info", tr("Cette partie est terminee : {label}", label=label))
            self.store.delete(session.gid, session.peer_call)
            self._refresh_games_button()

    def on_move_applied_silent(self, move, session) -> None:
        row = self.table.rowCount()
        self.table.insertRow(row)
        ply = row + 1
        vals = [str((ply + 1) // 2), tr("Blancs") if ply % 2 else tr("Noirs"),
                move.san_text or str(move), move.uid,
                f"{sq_number(move.to)} ({sq_name(move.to)})"]
        for c, v in enumerate(vals):
            item = QTableWidgetItem(v)
            if c == 3:
                item.setForeground(C_AMBER)
            self.table.setItem(row, c, item)
        self.board_view.set_last_move(move.frm, move.to)

    def _export_log(self) -> None:
        STATE_DIR.mkdir(exist_ok=True)
        path = STATE_DIR / f"trames_{time.strftime('%Y%m%d_%H%M%S')}.txt"
        path.write_text(self.trace.toPlainText())
        self.log("info", tr("Journal enregistre : {path}", path=path))

    def closeEvent(self, ev) -> None:
        """Fermeture immediate du point de vue de l'operateur.

        La fenetre est masquee d'abord : le reste du rangement, si bref
        soit-il, se fait alors sur une application deja disparue de l'ecran.
        Les minuteries sont arretees avant tout, sinon un rappel pourrait
        s'executer sur des objets a demi demontes.
        """
        self.hide()
        for timer in (self.clock, self._kiss_timeout):
            timer.stop()

        self._save_settings()
        self.link.close()
        if self.chk_stop_dw.isChecked() and self.direwolf.running:
            self.direwolf.stop()
        super().closeEvent(ev)
