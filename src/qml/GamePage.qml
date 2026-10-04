// GamePage.qml - the game: who plays whom, the situation, the moves, and
// the actions.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        Card {
            Layout.fillWidth: true
            GridLayout {
                anchors.fill: parent
                columns: 4
                columnSpacing: 10
                rowSpacing: 4
                Label { text: qsTr("Game"); color: window.theme.muted }
                Label { text: app.gid || "-"; font.family: app.monoFamily; color: window.theme.accent }
                Label { text: qsTr("Your colour"); color: window.theme.muted }
                Label {
                    text: app.myColor === "W" ? qsTr("White") : app.myColor === "B" ? qsTr("Black") : "-"
                    font.family: app.monoFamily
                }
                Label { text: qsTr("You"); color: window.theme.muted }
                Label { text: app.myCall || "-"; font.family: app.monoFamily }
                Label { text: qsTr("Correspondent"); color: window.theme.muted }
                Label { text: app.peer || "-"; font.family: app.monoFamily }
                Label { text: qsTr("Situation"); color: window.theme.muted }
                Label {
                    text: app.situation
                    font.family: app.monoFamily
                    wrapMode: Text.Wrap
                    Layout.columnSpan: 3
                    Layout.fillWidth: true
                }
            }
        }

        // The moves, newest at the bottom; the list follows the game unless
        // the operator has scrolled up to read.
        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 0
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    Label { text: qsTr("No."); color: window.theme.muted; Layout.preferredWidth: 36; font.pixelSize: 12 }
                    Label { text: qsTr("Side"); color: window.theme.muted; Layout.preferredWidth: 60; font.pixelSize: 12 }
                    Label { text: qsTr("Move"); color: window.theme.muted; Layout.fillWidth: true; font.pixelSize: 12 }
                    Label { text: qsTr("Piece"); color: window.theme.muted; Layout.preferredWidth: 50; font.pixelSize: 12 }
                    Label { text: qsTr("Square"); color: window.theme.muted; Layout.preferredWidth: 70; font.pixelSize: 12 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: window.theme.line }
                ListView {
                    id: moveList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: app.moves
                    property bool following: true
                    onMovementEnded: following = atYEnd
                    onCountChanged: if (following) Qt.callLater(positionViewAtEnd)
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        width: moveList.width - 16
                        x: 8
                        height: 26
                        Label { text: model.number; font.family: app.monoFamily; Layout.preferredWidth: 36; color: window.theme.muted }
                        Label { text: model.side; Layout.preferredWidth: 60 }
                        Label { text: model.san; font.family: app.monoFamily; font.bold: true; Layout.fillWidth: true
                                color: model.byPeer ? window.theme.rx : window.theme.ink }
                        Label { text: model.uid; font.family: app.monoFamily; color: window.theme.accent; Layout.preferredWidth: 50 }
                        Label { text: model.square; font.family: app.monoFamily; Layout.preferredWidth: 70; color: window.theme.muted }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: moveList.count === 0
                        text: qsTr("No move yet")
                        color: window.theme.muted
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            rowSpacing: 4
            AccentButton {
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                text: qsTr("Start a game")
                                Layout.fillWidth: true
                enabled: app.gameState !== "playing" && app.gameState !== "handshake"
                onClicked: window.invite()
            }
            WideButton {
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                text: qsTr("Resynchronise")
                Layout.fillWidth: true
                enabled: app.gameState === "playing"
                onClicked: app.resync()
            }
            WideButton {
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                text: qsTr("Offer a draw")
                Layout.fillWidth: true
                enabled: app.gameState === "playing"
                onClicked: app.offerDraw()
            }
            WideButton {
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                text: qsTr("Resign")
                Layout.fillWidth: true
                enabled: app.gameState === "playing"
                textColor: window.theme.alert
                onClicked: window.askResign()
            }
            WideButton {
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                text: app.savedCount > 0 ? qsTr("Saved games (%1)").arg(app.savedCount) : qsTr("Saved games")
                Layout.fillWidth: true
                enabled: app.savedCount > 0
                onClicked: gamesDialog.open()
            }
            WideButton {
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                text: qsTr("Test beacon")
                Layout.fillWidth: true
                enabled: app.modemState === "running"
                onClicked: app.ping()
            }
        }
    }
}
