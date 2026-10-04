// Main.qml - the window: the board and its panels side by side on a wide
// screen, pages under a tab bar on a phone.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window
    visible: true
    width: 1240
    height: 800
    minimumWidth: 360
    minimumHeight: 560
    title: "AX25Chess " + app.version
    // Full screen on the phone (the system bars are hidden by the C++ side
    // as well; Qt shows them again for a window that is not full screen).
    visibility: app.platform === "android" ? Window.FullScreen : Window.AutomaticVisibility

    readonly property bool android: app.platform === "android"
    readonly property bool wide: width >= 900 && width > height

    // ---- themes -----------------------------------------------------------
    // Four looks, chosen in Settings.  Dark: an instrument chassis with the
    // amber of a VFO dial.  Light: for a screen in direct sunlight, darker
    // accent and inks than a mirror of the dark one (sunlight eats mid
    // tones first).  Red: the night, every colour a shade of red so the eyes
    // keep their dark adaptation.  Amber: dim light.
    readonly property var themes: ({
        "dark": { "material": Material.Dark, "light": false,
            "chassis": "#131A21", "panel": "#1B242D", "line": "#2C3944", "ink": "#DCE3E8",
            "muted": "#7C8C99", "accent": "#E8A33D", "onAccent": "#131A21", "ok": "#5FB47E",
            "alert": "#D9534F", "warn": "#D9A441", "tx": "#E8A33D", "rx": "#5FB47E",
            "lightSquare": "#E4DCC9", "darkSquare": "#5C7C6F", "pieceWhite": "#F7F3EA", "pieceBlack": "#1C2430",
            "pieceEdge": "#5A000000", "badge": "#D7131A21", "badgeInk": "#E8A33D",
            "numberOnLight": "#000000", "numberOnDark": "#FFFFFF", "numberAlpha": 100, "lastMoveAlpha": 74,
            "deadWhite": "#E8E2D5", "deadBlack": "#61748A", "deadEdge": "#101820", "tray": "#1B242D" },
        "light": { "material": Material.Light, "light": true,
            "chassis": "#F5F2EA", "panel": "#FFFFFF", "line": "#8F8778", "ink": "#14191E",
            "muted": "#4E555C", "accent": "#8A4F04", "onAccent": "#FFFFFF", "ok": "#1F6B41",
            "alert": "#A3211B", "warn": "#8A5A00", "tx": "#8A4F04", "rx": "#1F6B41",
            "lightSquare": "#F2EAD5", "darkSquare": "#5A8271", "pieceWhite": "#FFFFFF", "pieceBlack": "#101820",
            "pieceEdge": "#A0000000", "badge": "#F0212832", "badgeInk": "#F6C87A",
            "numberOnLight": "#000000", "numberOnDark": "#FFFFFF", "numberAlpha": 140, "lastMoveAlpha": 110,
            "deadWhite": "#FFFFFF", "deadBlack": "#1B2A38", "deadEdge": "#242A30", "tray": "#8C949B" },
        "red": { "material": Material.Dark, "light": false,
            "chassis": "#0A0000", "panel": "#160000", "line": "#3A0A0A", "ink": "#FF4A4A",
            "muted": "#A33A3A", "accent": "#FF2A2A", "onAccent": "#0A0000", "ok": "#FF6B6B",
            "alert": "#FF1A1A", "warn": "#E04040", "tx": "#FF7070", "rx": "#FF4D4D",
            "lightSquare": "#5A1414", "darkSquare": "#2E0808", "pieceWhite": "#FF8080", "pieceBlack": "#120000",
            "pieceEdge": "#FF5050", "badge": "#E0100000", "badgeInk": "#FF5A5A",
            "numberOnLight": "#FF6060", "numberOnDark": "#FF6060", "numberAlpha": 130, "lastMoveAlpha": 90,
            "deadWhite": "#FF8080", "deadBlack": "#7A2020", "deadEdge": "#FF5050", "tray": "#160000" },
        "amber": { "material": Material.Dark, "light": false,
            "chassis": "#100C00", "panel": "#1A1400", "line": "#3D2E00", "ink": "#FFB84D",
            "muted": "#A07A30", "accent": "#FFB000", "onAccent": "#100C00", "ok": "#FFD166",
            "alert": "#FF6A00", "warn": "#E69500", "tx": "#FFD166", "rx": "#FFB000",
            "lightSquare": "#6B5420", "darkSquare": "#3A2C0C", "pieceWhite": "#FFD88A", "pieceBlack": "#140F00",
            "pieceEdge": "#FFB000", "badge": "#E0100C00", "badgeInk": "#FFB000",
            "numberOnLight": "#FFC060", "numberOnDark": "#FFC060", "numberAlpha": 120, "lastMoveAlpha": 90,
            "deadWhite": "#FFD88A", "deadBlack": "#7A5A20", "deadEdge": "#FFB000", "tray": "#1A1400" }
    })
    readonly property string themeName: (app.config.ui && themes[app.config.ui.theme]) ? app.config.ui.theme : "dark"
    readonly property var theme: themes[themeName]
    Material.theme: theme.material
    Material.primary: theme.panel
    Material.accent: theme.accent
    Material.background: theme.chassis
    Material.foreground: theme.ink
    color: theme.chassis

    // Material's elevation where the renderer can draw it.
    function elev(n) { return app.flatRendering ? 0 : n }
    function levelColour(level) {
        if (level === "tx" || level === "me") return theme.tx
        if (level === "rx" || level === "peer") return theme.rx
        if (level === "error") return theme.alert
        if (level === "warn") return theme.warn
        return theme.muted
    }

    property string hoverText: ""
    // The page shown; the tab bars follow it and set it.
    property int pageIndex: 0

    // ---- dialogs -------------------------------------------------------------
    MessageDialog { id: message }
    PromotionDialog { id: promotion }
    GamesDialog { id: gamesDialog }

    function inform(title, text) { message.ask(title, text, false, null) }
    function confirm(title, text, action) { message.ask(title, text, true, action) }
    function invite() {
        var why = app.invite()
        if (why !== "") inform(qsTr("Start a game"), why)
    }
    function askResign() {
        confirm(qsTr("Resign"), qsTr("Do you confirm your resignation?"), function() { app.resign() })
    }
    function chooseMove(uid, square, needsPromotion, white) {
        if (needsPromotion) promotion.ask(uid, square, white)
        else app.playMove(uid, square, "")
    }

    Connections {
        target: app
        function onAnnounce(title, text) { window.inform(title, text) }
        function onDrawOfferReceived(peer) {
            message.ask(qsTr("Draw offer"), qsTr("%1 offers a draw. Do you accept?").arg(peer), true,
                        function() { app.answerDraw(true) }, function() { app.answerDraw(false) })
        }
        function onAttention() { if (!window.android && !window.active) window.alert(0) }
    }

    // ---- header: the front panel -----------------------------------------------
    header: ToolBar {
        Material.background: window.theme.panel
        Material.elevation: window.elev(4)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 4
            spacing: window.wide ? 14 : 8
            // The title gives way first on a narrow screen: it is cut short
            // rather than pushing the Start button off the edge.
            Label {
                text: "AX25CHESS"
                font.family: app.monoFamily
                font.pixelSize: window.wide ? 18 : 16
                font.bold: true
                font.letterSpacing: window.wide ? 3 : 1
                color: window.theme.accent
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Led {
                text: window.wide ? qsTr("MODEM") : "M"
                level: app.modemState === "running" ? "on"
                     : (app.modemState === "starting" || app.modemState === "stopping") ? "warn"
                     : app.stationOn ? "error" : "off"
            }
            Led {
                text: app.ptt ? qsTr("TX") : app.dcd ? qsTr("RX") : (window.wide ? qsTr("CHANNEL") : "C")
                level: app.ptt ? "error" : app.dcd ? "warn" : app.modemState === "running" ? "on" : "off"
            }
            Led {
                text: window.wide ? qsTr("TURN") : "T"
                level: app.myTurn ? "on" : app.gameState === "playing" ? "warn" : "off"
            }
            ToolButton {
                text: app.stationOn ? qsTr("Stop") : qsTr("Start")
                font.bold: true
                Material.foreground: app.stationOn ? window.theme.alert : window.theme.ok
                onClicked: app.stationOn ? app.stopStation() : app.startStation()
            }
        }
    }

    // ---- body -------------------------------------------------------------------
    Loader {
        anchors.fill: parent
        sourceComponent: window.wide ? wideLayout : narrowLayout
    }

    Component {
        id: wideLayout
        RowLayout {
            spacing: 0
            BoardPanel {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 6
            }
            ColumnLayout {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.preferredWidth: 4
                Layout.minimumWidth: 380
                Layout.maximumWidth: 620
                spacing: 0
                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    Material.background: window.theme.chassis
                    currentIndex: Math.max(0, window.pageIndex - 1)
                    TabButton { text: qsTr("Game") }
                    TabButton { text: app.unreadChat ? qsTr("Messages") + " \u2022" : qsTr("Messages") }
                    TabButton { text: qsTr("Frames") }
                    TabButton { text: qsTr("Modem") }
                    TabButton { text: qsTr("Settings") }
                    onCurrentIndexChanged: {
                        window.pageIndex = currentIndex + 1
                        if (currentIndex === 1) app.markChatRead()
                    }
                }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabs.currentIndex
                    GamePage {}
                    ChatPage { active: tabs.currentIndex === 1 }
                    LogPage { which: "frames" }
                    LogPage { which: "modem" }
                    SettingsPage {}
                }
            }
        }
    }

    component PhoneTab: TabButton {
        leftPadding: 2
        rightPadding: 2
    }

    Component {
        id: narrowLayout
        ColumnLayout {
            spacing: 0
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: bar.currentIndex
                BoardPanel { compact: true }
                GamePage {}
                ChatPage { active: bar.currentIndex === 2 }
                LogPage { which: "frames"; selectable: true }
                SettingsPage {}
            }
            TabBar {
                id: bar
                Layout.fillWidth: true
                currentIndex: window.pageIndex
                Material.background: window.theme.panel
                Material.elevation: window.elev(6)
                // Five tabs on a phone width: mixed case, smaller type, little
                // padding, or "Settings" and the French labels get cut.
                font.capitalization: Font.MixedCase
                font.pixelSize: 12
                PhoneTab { text: qsTr("Board") }
                PhoneTab { text: qsTr("Game") }
                PhoneTab { text: app.unreadChat ? qsTr("Chat") + " \u2022" : qsTr("Chat") }
                PhoneTab { text: qsTr("Log") }
                PhoneTab { text: qsTr("Settings") }
                onCurrentIndexChanged: {
                    window.pageIndex = currentIndex
                    if (currentIndex === 2) app.markChatRead()
                }
            }
        }
    }

    // ---- status line (wide screens) ---------------------------------------------
    footer: ToolBar {
        visible: window.wide
        height: visible ? 30 : 0
        Material.background: window.theme.panel
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 18
            Label {
                text: app.turnText
                font.family: app.monoFamily
                font.pixelSize: 12
                color: window.theme.muted
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label { text: app.channelText; font.family: app.monoFamily; font.pixelSize: 12; color: window.theme.muted }
            Label { text: app.ackText; font.family: app.monoFamily; font.pixelSize: 12; color: window.theme.warn }
            Label {
                text: window.hoverText !== "" ? window.hoverText : qsTr("fingerprint %1").arg(app.positionHash)
                font.family: app.monoFamily
                font.pixelSize: 12
                color: window.theme.muted
            }
        }
    }
}
