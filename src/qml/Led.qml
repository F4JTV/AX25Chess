// Led.qml - a front-panel indicator: off, on, warn, error.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    property string text
    property string level: "off"
    spacing: 5
    readonly property color lit: level === "on" ? window.theme.ok
                               : level === "warn" ? window.theme.warn
                               : level === "error" ? window.theme.alert : window.theme.line
    Rectangle {
        width: 10; height: 10; radius: 5
        color: parent.lit
        border.color: Qt.darker(parent.lit, 1.4)
    }
    Label {
        text: parent.text
        font.family: app.monoFamily
        font.pixelSize: 12
        color: parent.level === "off" ? window.theme.muted : window.theme.ink
    }
}
