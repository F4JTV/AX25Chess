// Field.qml - a label and its control, on one row; the control may wrap
// below the label on a narrow screen.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridLayout {
    property alias label: l.text
    default property alias content: holder.data
    Layout.fillWidth: true
    columns: width > 420 ? 2 : 1
    columnSpacing: 12
    rowSpacing: 2
    Label {
        id: l
        Layout.preferredWidth: parent.columns === 2 ? 170 : -1
        Layout.maximumWidth: parent.columns === 2 ? 170 : 100000
        wrapMode: Text.Wrap
        color: window.theme.muted
    }
    Item {
        id: holder
        Layout.fillWidth: true
        implicitHeight: childrenRect.height
    }
}
