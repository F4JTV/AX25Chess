// PromotionDialog.qml - the piece a pawn becomes.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Dialog {
    id: dialog
    property string uid: ""
    property int square: -1
    property bool white: true

    function ask(pieceUid, toSquare, isWhite) {
        uid = pieceUid
        square = toSquare
        white = isWhite
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    title: qsTr("Promotion")
    width: 4 * 64 + 3 * 10 + 48

    contentItem: RowLayout {
        spacing: 10
        Repeater {
            model: ["Q", "R", "B", "N"]
            Button {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64
                Material.background: window.theme.lightSquare
                contentItem: Text {
                    text: app.pieceGlyph(modelData)
                    font.family: app.pieceFontFamily()
                    font.pixelSize: 40
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: dialog.white ? window.theme.pieceWhite : window.theme.pieceBlack
                    style: Text.Outline
                    styleColor: window.theme.light ? "#404040" : "#000000"
                }
                onClicked: {
                    dialog.close()
                    app.playMove(dialog.uid, dialog.square, modelData)
                }
            }
        }
    }
    footer: DialogButtonBox {
        standardButtons: DialogButtonBox.Cancel
    }
}
