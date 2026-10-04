// SectionTitle.qml - the heading of a settings section, with an accent bar.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    property alias text: t.text
    Layout.fillWidth: true
    Layout.topMargin: 8
    spacing: 8
    Rectangle { width: 4; height: 20; radius: 2; color: window.theme.accent }
    Label {
        id: t
        font.pixelSize: 16
        font.bold: true
        color: window.theme.ink
        Layout.fillWidth: true
    }
}
