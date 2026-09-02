"""
game_manager.py - Gestionnaire des parties commencees et non terminees.

Remplace l'ancienne question posee au demarrage, qui revenait a chaque
lancement puisque refuser ne supprimait rien. Ici, rien ne s'impose a
l'operateur : il ouvre la liste quand il le souhaite, reprend la partie de son
choix ou fait le menage.
"""

from __future__ import annotations

from typing import Optional

from PyQt6.QtCore import Qt
from PyQt6.QtGui import QColor, QFont
from PyQt6.QtWidgets import (QAbstractItemView, QDialog, QHBoxLayout,
                             QHeaderView, QLabel, QMessageBox, QPushButton,
                             QTableWidget, QTableWidgetItem, QVBoxLayout)

from .board_widget import (C_ALERT, C_AMBER, C_INK, C_MUTED, C_OK,
                           mono_family)
from .i18n import tr
from .games import GameStore, SavedGame

COLOR_LABEL = {"W": "Blancs", "B": "Noirs"}


class GameManagerDialog(QDialog):
    """Liste les parties en cours et permet d'en reprendre ou d'en supprimer."""

    def __init__(self, store: GameStore, current_gid: str = "", parent=None):
        super().__init__(parent)
        self.store = store
        self.current_gid = current_gid
        self.chosen: Optional[SavedGame] = None
        self.games: list[SavedGame] = []

        self.setWindowTitle(tr("Parties en cours"))
        self.resize(840, 400)
        if parent is not None:
            self.setStyleSheet(parent.styleSheet())

        lay = QVBoxLayout(self)
        lay.setContentsMargins(14, 14, 14, 14)
        lay.setSpacing(10)

        self.intro = QLabel()
        self.intro.setStyleSheet(f"color:{C_MUTED.name()};font-size:11px;")
        self.intro.setWordWrap(True)
        lay.addWidget(self.intro)

        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(
            [tr("Partie"), tr("Correspondant"), tr("Couleurs"), tr("Avancement"),
             tr("Trait"), tr("Activite")])
        self.table.verticalHeader().setVisible(False)
        self.table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.table.doubleClicked.connect(self._resume)
        self.table.itemSelectionChanged.connect(self._update_buttons)
        head = self.table.horizontalHeader()
        # tout au contenu sauf la derniere colonne, qui absorbe la place libre :
        # en mode Stretch generalise, les libelles se retrouvent tronques
        for col in range(5):
            head.setSectionResizeMode(col, QHeaderView.ResizeMode.ResizeToContents)
        head.setSectionResizeMode(5, QHeaderView.ResizeMode.Stretch)
        lay.addWidget(self.table, 1)

        row = QHBoxLayout()
        self.btn_resume = QPushButton(tr("Reprendre cette partie"))
        self.btn_resume.setObjectName("primary")
        self.btn_resume.clicked.connect(self._resume)
        self.btn_delete = QPushButton(tr("Supprimer"))
        self.btn_delete.setObjectName("danger")
        self.btn_delete.clicked.connect(self._delete)
        btn_close = QPushButton(tr("Fermer"))
        btn_close.clicked.connect(self.reject)
        row.addWidget(self.btn_resume, 1)
        row.addWidget(self.btn_delete)
        row.addStretch(1)
        row.addWidget(btn_close)
        lay.addLayout(row)

        self.reload()

    # -- contenu ------------------------------------------------------------

    def reload(self) -> None:
        self.games = self.store.list()
        self.table.setRowCount(0)
        for game in self.games:
            row = self.table.rowCount()
            self.table.insertRow(row)
            is_current = game.gid == self.current_gid

            trait = tr("a vous") if game.my_turn else tr("correspondant")
            values = [
                game.gid + (tr("  (en cours)") if is_current else ""),
                game.peer_call or "-",
                COLOR_LABEL.get(game.color, "-"),
                tr("coup {n} / {plies} demi-coups", n=game.move_number,
                   plies=game.ply_count),
                trait,
                game.age_text(),
            ]
            for col, text in enumerate(values):
                item = QTableWidgetItem(text)
                item.setFont(QFont(mono_family(), 11))
                if col == 4:
                    item.setForeground(C_OK if game.my_turn else C_MUTED)
                elif col == 0 and is_current:
                    item.setForeground(C_AMBER)
                else:
                    item.setForeground(C_INK)
                self.table.setItem(row, col, item)

        if self.games:
            waiting = sum(1 for g in self.games if g.my_turn)
            detail = (tr(", dont {waiting} en attente de votre coup", waiting=waiting)
                      if waiting else "")
            self.intro.setText(
                tr("{count} partie(s) enregistree(s){detail}. Double-cliquez sur "
                   "une ligne pour la reprendre.",
                   count=len(self.games), detail=detail))
        else:
            self.intro.setText(tr(
                "Aucune partie en cours. Les parties sont enregistrees "
                "automatiquement apres chaque demi-coup et effacees des "
                "qu'elles se terminent."))
        if self.table.rowCount():
            self.table.selectRow(0)
        self._update_buttons()

    def _selected(self) -> Optional[SavedGame]:
        row = self.table.currentRow()
        if 0 <= row < len(self.games):
            return self.games[row]
        return None

    def _update_buttons(self) -> None:
        game = self._selected()
        self.btn_delete.setEnabled(game is not None)
        self.btn_resume.setEnabled(game is not None
                                   and game.gid != self.current_gid)

    # -- actions ------------------------------------------------------------

    def _resume(self) -> None:
        game = self._selected()
        if game is None or game.gid == self.current_gid:
            return
        self.chosen = game
        self.accept()

    def _delete(self) -> None:
        game = self._selected()
        if game is None:
            return
        question = tr("Supprimer definitivement la partie {gid} contre {peer} "
                      "({plies} demi-coups) ?",
                      gid=game.gid, peer=game.peer_call, plies=game.ply_count)
        if game.gid == self.current_gid:
            question += "\n\n" + tr("Cette partie est celle en cours.")
        if QMessageBox.question(self, tr("Parties en cours"), question) \
                != QMessageBox.StandardButton.Yes:
            return
        self.store.delete_game(game)
        self.reload()
