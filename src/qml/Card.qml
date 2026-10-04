// Card.qml - a raised panel (Material elevation, a drop shadow where the
// renderer can draw one).  Never put a TapHandler on what lies inside: the
// Pane takes the press first.  Use MouseArea or controls.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material

Pane {
    Material.elevation: window.elev(2)
    Material.background: window.theme.panel
    padding: 12
}
