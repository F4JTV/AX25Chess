// GamesDialog.qml - the games started and not finished, saved after every
// half-move: resume one, or delete it.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(window.width - 24, 640)
    height: Math.min(window.height - 48, 520)
    modal: true
    title: qsTr("Saved games")
    onAboutToShow: app.refreshGames()

    function resume(row) {
        var go = function() {
            var why = app.resumeGame(row)
            if (why !== "") window.inform(qsTr("Saved games"), why)
            else dialog.close()
        }
        if (app.resumeNeedsConfirmation(row)) {
            window.confirm(qsTr("Saved games"),
                           qsTr("A game against %1 is in progress. It stays saved, but will be closed here. Continue?").arg(app.peer),
                           go)
        } else {
            go()
        }
    }

    contentItem: ColumnLayout {
        spacing: 8
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: window.theme.muted
            text: app.savedCount === 0
                  ? qsTr("No saved games. Games are stored after every half-move and removed as soon as they end.")
                  : app.waitingCount > 0
                    ? qsTr("%1 game(s), %2 waiting for your move.").arg(app.savedCount).arg(app.waitingCount)
                    : qsTr("%1 game(s).").arg(app.savedCount)
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: app.games
            ScrollBar.vertical: ScrollBar {}
            delegate: Card {
                width: list.width - 4
                padding: 8
                RowLayout {
                    anchors.fill: parent
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            text: model.peer + "  -  " + model.gid + (model.current ? "  " + qsTr("(open)") : "")
                            font.family: app.monoFamily
                            font.bold: true
                            color: model.current ? window.theme.accent : window.theme.ink
                        }
                        Label {
                            text: model.colour + "  -  " + model.progress + "  -  " + model.age
                            font.pixelSize: 12
                            color: window.theme.muted
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Label {
                            text: model.myTurn ? qsTr("your move") : qsTr("your correspondent's move")
                            font.pixelSize: 12
                            color: model.myTurn ? window.theme.ok : window.theme.muted
                        }
                    }
                    AccentButton {
                        text: qsTr("Resume")
                                                enabled: !model.current
                        onClicked: dialog.resume(index)
                    }
                    Button {
                        text: qsTr("Delete")
                        flat: true
                        Material.foreground: window.theme.alert
                        onClicked: {
                            var row = index
                            window.confirm(qsTr("Delete"),
                                           qsTr("Permanently delete game %1 against %2?").arg(model.gid).arg(model.peer),
                                           function() { app.deleteGame(row) })
                        }
                    }
                }
            }
        }
    }
    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Close
    }
}
