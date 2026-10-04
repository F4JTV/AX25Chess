// WideButton.qml - a button as wide as its column, its label wrapped rather
// than cut or sticking out (see WrapSwitch.qml for why).
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Button {
    id: control
    // The label's colour; the board panel's Resign sets the alert red.
    property color textColor: control.flat ? window.theme.accent : window.theme.ink
    Layout.fillWidth: true
    contentItem: Text {
        text: control.text
        font: control.font
        // Whole words only; a word longer than the button shrinks the type
        // rather than breaking in the middle ("ABANDONNE / R").
        wrapMode: Text.WordWrap
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 10
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.enabled ? control.textColor : window.theme.muted
    }
}
