// WrapSwitch.qml - a switch whose label wraps instead of widening the page.
//
// In a Qt Quick layout an item that does not fill the width keeps its
// implicit width, and a Switch's implicit width is its whole label on one
// line: a long label (the French ones above all) pushed the whole settings
// card past the right edge of a phone held upright.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Switch {
    id: control
    Layout.fillWidth: true
    contentItem: Text {
        text: control.text
        font: control.font
        wrapMode: Text.Wrap
        verticalAlignment: Text.AlignVCenter
        color: control.enabled ? window.theme.ink : window.theme.muted
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
    }
}
