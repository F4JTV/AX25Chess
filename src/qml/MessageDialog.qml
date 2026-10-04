// MessageDialog.qml - a message, or a question with yes and no.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    property string body: ""
    property bool question: false
    property var yesAction: null
    property var noAction: null

    function ask(title, text, isQuestion, yes, no) {
        dialog.title = title
        dialog.body = text
        dialog.question = isQuestion
        dialog.yesAction = yes || null
        dialog.noAction = no || null
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(window.width - 32, 480)
    modal: true
    closePolicy: question ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    contentItem: Label {
        text: dialog.body
        wrapMode: Text.Wrap
    }
    footer: DialogButtonBox {
        AccentButton {
            text: dialog.question ? qsTr("Yes") : qsTr("OK")
                        DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
        }
        Button {
            visible: dialog.question
            text: qsTr("No")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
    onAccepted: if (yesAction) yesAction()
    onRejected: if (noAction) noAction()
}
