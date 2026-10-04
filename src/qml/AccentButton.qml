// AccentButton.qml - the main action of a panel, in the theme's accent.
// Drawn here rather than through Material's "highlighted" or background
// attached property: those pick colours of their own that do not follow
// the four themes (and the label came out dark on dark).
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Button {
    id: control
    font.bold: true
    Material.elevation: window.elev(2)
    contentItem: Text {
        text: control.text
        font: control.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        fontSizeMode: Text.HorizontalFit
        minimumPixelSize: 10
        color: control.enabled ? window.theme.onAccent : window.theme.muted
    }
    background: Rectangle {
        implicitWidth: 64
        implicitHeight: 40
        radius: 4
        color: !control.enabled ? window.theme.line
             : control.down ? Qt.darker(window.theme.accent, 1.2)
             : control.hovered ? Qt.lighter(window.theme.accent, 1.1) : window.theme.accent
    }
}
