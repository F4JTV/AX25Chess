// BoardPanel.qml - the board, the captured pieces above and below it, the
// hint, and the display options.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import AX25Chess.Board

Item {
    id: panel
    property bool compact: false

    function saveDisplay(key, value) {
        var cfg = JSON.parse(JSON.stringify(app.config))
        cfg.game[key] = value
        app.saveConfig(cfg)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: panel.compact ? 4 : 10
        spacing: 4

        CapturesBar {
            Layout.fillWidth: true
            // The side at the top of the board lost these.
            colour: app.boardFlipped ? "W" : "B"
        }

        BoardView {
            id: board
            objectName: "boardView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 200
            Layout.maximumHeight: width
            source: app
            flipped: app.boardFlipped
            showUids: app.config.game ? app.config.game.show_uids : true
            showNumbers: app.config.game ? app.config.game.show_numbers : true
            colors: window.theme
            onMoveChosen: function(uid, square, promotion, white) { window.chooseMove(uid, square, promotion, white) }
            onHovered: function(square) { window.hoverText = board.describeSquare(square) }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: !window.android
                onPressed: function(mouse) { board.pressAt(mouse.x, mouse.y) }
                onPositionChanged: function(mouse) { board.hoverAt(mouse.x, mouse.y) }
                onExited: board.hoverAt(-1, -1)
            }
            Connections {
                target: app
                function onBoardRevisionChanged() { board.update() }
                function onGameChanged() { board.update() }
            }
        }

        CapturesBar {
            Layout.fillWidth: true
            colour: app.boardFlipped ? "B" : "W"
        }

        Label {
            text: app.hint
            Layout.topMargin: 2
            wrapMode: Text.Wrap
            Layout.fillWidth: true
            font.family: app.monoFamily
            font.pixelSize: 13
            color: app.myTurn ? window.theme.ok : window.theme.muted
        }

        // On a phone the main actions sit under the board, in three equal
        // parts whatever the length of their labels (the French ones are
        // longer): no button may widen the column past the screen.
        RowLayout {
            visible: panel.compact
            Layout.fillWidth: true
            spacing: 6
            AccentButton {
                text: qsTr("Invite")
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                enabled: app.gameState !== "playing" && app.gameState !== "handshake"
                onClicked: window.invite()
            }
            WideButton {
                text: qsTr("Draw")
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                enabled: app.gameState === "playing"
                onClicked: app.offerDraw()
            }
            WideButton {
                text: qsTr("Resign")
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                enabled: app.gameState === "playing"
                textColor: window.theme.alert
                onClicked: window.askResign()
            }
        }
        Label {
            visible: panel.compact && app.ackText !== ""
            text: app.ackText
            font.family: app.monoFamily
            font.pixelSize: 11
            color: window.theme.warn
        }

        Item { Layout.fillHeight: true; Layout.fillWidth: true }

        // The display options flow onto a second line when the width runs out.
        Flow {
            Layout.fillWidth: true
            spacing: 4
            CheckBox {
                text: qsTr("Identifiers")
                checked: app.config.game ? app.config.game.show_uids : true
                onToggled: panel.saveDisplay("show_uids", checked)
            }
            CheckBox {
                text: qsTr("Square numbers")
                checked: app.config.game ? app.config.game.show_numbers : true
                onToggled: panel.saveDisplay("show_numbers", checked)
            }
            Button {
                text: qsTr("Flip")
                flat: true
                onClicked: app.flipBoard()
            }
        }
    }
}
