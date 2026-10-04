// ConfEditor.qml - the modem's direwolf.conf, to read (generated) or to edit
// (the operator's own file).
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    property string path: ""
    property bool editable: false
    property alias text: area.text

    function showText(titleText, body) {
        title = titleText
        path = ""
        editable = false
        area.text = body
        open()
    }
    function editFile(filePath) {
        title = filePath
        path = filePath
        editable = true
        area.text = app.readConfigFile(filePath)
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(window.width - 24, 760)
    height: Math.min(window.height - 48, 620)
    modal: true

    contentItem: ScrollView {
        clip: true
        TextArea {
            id: area
            readOnly: !dialog.editable
            font.family: app.monoFamily
            font.pixelSize: 13
            wrapMode: TextEdit.NoWrap
            selectByMouse: true
        }
    }
    footer: DialogButtonBox {
        AccentButton {
            visible: dialog.editable
            text: qsTr("Save")
            onClicked: {
                var why = app.writeConfigFile(dialog.path, area.text)
                if (why !== "") window.inform(qsTr("Modem configuration"), why)
                else dialog.close()
            }
        }
        Button {
            text: qsTr("Close")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
        }
    }
}
