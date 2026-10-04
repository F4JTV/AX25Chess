// ChatPage.qml - messages with the correspondent, over the air (CHAT frames,
// acknowledged like moves).
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    property bool active: false
    onActiveChanged: if (active) app.markChatRead()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            padding: 6
            ListView {
                id: chatList
                anchors.fill: parent
                clip: true
                model: app.chat
                spacing: 4
                property bool following: true
                onMovementEnded: following = atYEnd
                onCountChanged: if (following) Qt.callLater(positionViewAtEnd)
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    width: chatList.width - 12
                    spacing: 8
                    Label { text: model.time.substring(0, 5); font.family: app.monoFamily; color: window.theme.muted }
                    Label {
                        text: model.text
                        wrapMode: Text.Wrap
                        Layout.fillWidth: true
                        color: window.levelColour(model.level)
                    }
                }
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 20
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    visible: chatList.count === 0
                    text: qsTr("Messages go to your correspondent, %1 characters at most each.").arg(180)
                    color: window.theme.muted
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: input
                Layout.fillWidth: true
                placeholderText: qsTr("Message to %1").arg(app.peer || qsTr("your correspondent"))
                maximumLength: 180
                onAccepted: send()
                function send() {
                    if (text.trim() === "") return
                    app.sendChat(text)
                    text = ""
                }
            }
            AccentButton {
                text: qsTr("Send")
                                enabled: input.text.trim() !== "" && app.modemState === "running"
                onClicked: input.send()
            }
        }
    }
}
