// UpperField.qml - a callsign field: upper case as typed, no predictive
// text (the phone keyboard would otherwise "correct" a callsign).
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls

TextField {
    width: parent ? parent.width : implicitWidth
    inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
    font.family: app.monoFamily
    onTextChanged: {
        var up = text.toUpperCase()
        if (up !== text) { var pos = cursorPosition; text = up; cursorPosition = pos }
    }
}
