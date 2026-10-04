// LogPage.qml - the protocol log (frames, retransmissions, game events) or
// the modem's own console.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: page
    property string which: "frames"
    // On a phone one page shows either log, with a switch.
    property bool selectable: false
    readonly property var model: page.which === "modem" ? app.modemLog : app.frames

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            visible: page.selectable
            Layout.fillWidth: true
            Button {
                text: qsTr("Frames")
                flat: page.which !== "frames"
                highlighted: page.which === "frames"
                onClicked: page.which = "frames"
            }
            Button {
                text: qsTr("Modem")
                flat: page.which !== "modem"
                highlighted: page.which === "modem"
                onClicked: page.which = "modem"
            }
            Item { Layout.fillWidth: true }
        }

        Label {
            visible: page.which === "modem"
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: app.modemDescription !== "" ? app.modemDescription + "  -  " + app.channelText : app.channelText
            font.family: app.monoFamily
            font.pixelSize: 12
            color: window.theme.muted
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 6
            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: page.model
                property bool following: true
                onMovementEnded: following = atYEnd
                onCountChanged: if (following) Qt.callLater(positionViewAtEnd)
                onModelChanged: { following = true; Qt.callLater(positionViewAtEnd) }
                ScrollBar.vertical: ScrollBar {}
                delegate: Text {
                    width: list.width - 14
                    wrapMode: Text.WrapAnywhere
                    text: "<span style='color:" + window.theme.muted + "'>" + model.time + "</span> " + htmlEscape(model.text)
                    textFormat: Text.StyledText
                    font.family: app.monoFamily
                    font.pixelSize: 12
                    color: window.levelColour(model.level)
                    function htmlEscape(s) { return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label {
                id: note
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                color: window.theme.muted
                font.pixelSize: 12
            }
            Button {
                text: window.android ? qsTr("Copy") : qsTr("Export")
                onClicked: note.text = app.exportLog(page.which)
            }
            Button {
                text: qsTr("Clear")
                onClicked: page.model.clear()
            }
        }
    }
}
