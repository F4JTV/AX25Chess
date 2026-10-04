// CapturesBar.qml - the pieces of one colour that were taken, with their
// identifiers, on a tray (without it, black pieces would vanish into a dark
// chassis), and the material edge of the side that took them.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: bar
    property string colour: "W"
    readonly property var pieces: { app.boardRevision; return app.captured(colour) }
    readonly property int edge: { app.boardRevision; return app.materialEdge(colour) }
    implicitHeight: 46
    clip: true

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        spacing: 8
        Label {
            text: (bar.colour === "W" ? qsTr("White taken") : qsTr("Black taken")) + "  (" + bar.pieces.length + ")"
            font.family: app.monoFamily
            font.pixelSize: 10
            color: window.theme.muted
            Layout.preferredWidth: 120
            elide: Text.ElideRight
        }
        Rectangle {
            visible: bar.pieces.length > 0
            Layout.fillHeight: true
            Layout.topMargin: 3
            Layout.bottomMargin: 3
            Layout.preferredWidth: row.width + 12
            Layout.maximumWidth: bar.width - 190
            radius: 5
            color: window.theme.tray
            border.color: window.theme.line
            clip: true
            Row {
                id: row
                anchors.verticalCenter: parent.verticalCenter
                x: 6
                spacing: 2
                Repeater {
                    model: bar.pieces
                    Column {
                        width: 30
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.glyph
                            font.family: app.pieceFontFamily()
                            font.pixelSize: 20
                            color: bar.colour === "W" ? window.theme.deadWhite : window.theme.deadBlack
                            style: Text.Outline
                            styleColor: window.theme.deadEdge
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.uid
                            font.family: app.monoFamily
                            font.pixelSize: 8
                            color: window.theme.muted
                        }
                    }
                }
            }
        }
        Label {
            visible: bar.edge > 0
            text: "+" + bar.edge
            font.family: app.monoFamily
            font.pixelSize: 12
            font.bold: true
            color: window.theme.accent
        }
        Item { Layout.fillWidth: true }
    }
}
