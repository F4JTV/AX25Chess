// SettingsPage.qml - the station, the game, the modem and the display, in
// sections down one column.  The page edits a copy of the configuration;
// Save hands it back to the application, which restarts the modem when its
// settings changed.
//
// This file is part of AX25Chess.
// SPDX-License-Identifier: GPL-2.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: page
    property var cfg: ({})
    property bool dirty: false

    function load() {
        cfg = JSON.parse(JSON.stringify(app.config))
        dirty = false
        form.active = false
        form.active = true
    }
    function changed() { dirty = true; status.text = "" }
    function save() {
        var why = app.saveConfig(cfg)
        if (why !== "") { window.inform(qsTr("Settings"), why); return }
        dirty = false
        status.text = qsTr("Saved")
    }

    Component.onCompleted: load()
    Connections {
        target: app
        function onConfigChanged() { if (!page.dirty) page.load() }
    }
    ConfEditor { id: editor }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: form.height + 24
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            Loader {
                id: form
                x: 12
                y: 12
                width: flick.width - 24
                sourceComponent: formComponent
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: window.theme.line }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Label { id: status; color: window.theme.ok; Layout.fillWidth: true }
            Button {
                text: qsTr("Revert")
                enabled: page.dirty
                onClicked: page.load()
            }
            AccentButton {
                text: qsTr("Save")
                                enabled: page.dirty
                onClicked: page.save()
            }
        }
    }

    Component {
        id: formComponent
        ColumnLayout {
            spacing: 10
            // Local copies of what decides which fields show.
            property string pttMode: page.cfg.modem.ptt
            property bool ownFile: page.cfg.modem.own_file

            // ---- station ----------------------------------------------------------
            SectionTitle { text: qsTr("Station") }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Field {
                        label: qsTr("Your callsign")
                        UpperField {
                            objectName: "callsignField"
                            text: page.cfg.station.callsign
                            placeholderText: "N0CALL-7"
                            onTextEdited: { page.cfg.station.callsign = text; page.changed() }
                        }
                    }
                    Field {
                        label: qsTr("Correspondent")
                        UpperField {
                            objectName: "peerField"
                            text: page.cfg.station.peer
                            placeholderText: "N0CALL-2"
                            onTextEdited: { page.cfg.station.peer = text; page.changed() }
                        }
                    }
                    Field {
                        label: qsTr("Digipeaters")
                        UpperField {
                            text: page.cfg.station.path
                            placeholderText: qsTr("none, or e.g. WIDE1-1")
                            onTextEdited: { page.cfg.station.path = text; page.changed() }
                        }
                    }
                }
            }

            // ---- game -------------------------------------------------------------
            SectionTitle { text: qsTr("Game") }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Field {
                        label: qsTr("Retransmission delay")
                        SpinBox {
                            from: 5; to: 120
                            value: page.cfg.game.retry_seconds
                            editable: true
                            textFromValue: function(v) { return v + " s" }
                            valueFromText: function(t) { return parseInt(t) }
                            onValueModified: { page.cfg.game.retry_seconds = value; page.changed() }
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: window.theme.muted
                        text: qsTr("How long a move waits for its acknowledgement before it is sent again. Raise it when the frames go through digipeaters.")
                    }
                }
            }

            // ---- modem ------------------------------------------------------------
            SectionTitle { text: qsTr("Modem") }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    WrapSwitch {
                        text: qsTr("Start the station with the application")
                        checked: page.cfg.modem.auto_start
                        onToggled: { page.cfg.modem.auto_start = checked; page.changed() }
                    }
                    Flow {
                        visible: !window.android
                        Layout.fillWidth: true
                        RadioButton {
                            text: qsTr("Settings below")
                            checked: !page.cfg.modem.own_file
                            onToggled: if (checked) { page.cfg.modem.own_file = false; ownFile = false; page.changed() }
                        }
                        RadioButton {
                            text: qsTr("My own direwolf.conf")
                            checked: page.cfg.modem.own_file
                            onToggled: if (checked) { page.cfg.modem.own_file = true; ownFile = true; page.changed() }
                        }
                    }

                    // The operator's own file.
                    ColumnLayout {
                        visible: ownFile && !window.android
                        Layout.fillWidth: true
                        Field {
                            label: qsTr("File")
                            TextField {
                                id: confPath
                                width: parent.width
                                text: page.cfg.modem.config_file
                                font.family: app.monoFamily
                                placeholderText: qsTr("path to direwolf.conf")
                                onTextEdited: { page.cfg.modem.config_file = text; page.changed() }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            WideButton {
                                text: qsTr("Create a starter file")
                                onClicked: {
                                    var p = app.createStarterConfig()
                                    if (p !== "") { confPath.text = p; page.cfg.modem.config_file = p; page.changed() }
                                }
                            }
                            WideButton {
                                text: qsTr("Edit")
                                enabled: confPath.text !== ""
                                onClicked: editor.editFile(confPath.text)
                            }
                        }
                    }

                    // The generated file.
                    ColumnLayout {
                        visible: !ownFile || window.android
                        Layout.fillWidth: true
                        Field {
                            label: qsTr("Sound card in")
                            visible: !window.android
                            ColumnLayout {
                                width: parent.width
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: app.audioInputs
                                    textRole: "label"
                                    currentIndex: -1
                                    displayText: qsTr("Choose...")
                                    onActivated: function(i) { audioIn.text = model[i].value; page.cfg.modem.audio_in = audioIn.text; page.changed() }
                                }
                                TextField {
                                    id: audioIn
                                    Layout.fillWidth: true
                                    text: page.cfg.modem.audio_in
                                    font.family: app.monoFamily
                                    onTextEdited: { page.cfg.modem.audio_in = text; page.changed() }
                                }
                            }
                        }
                        Field {
                            label: qsTr("Sound card out")
                            visible: !window.android
                            ColumnLayout {
                                width: parent.width
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: app.audioOutputs
                                    textRole: "label"
                                    currentIndex: -1
                                    displayText: qsTr("Choose...")
                                    onActivated: function(i) { audioOut.text = model[i].value; page.cfg.modem.audio_out = audioOut.text; page.changed() }
                                }
                                TextField {
                                    id: audioOut
                                    Layout.fillWidth: true
                                    text: page.cfg.modem.audio_out
                                    font.family: app.monoFamily
                                    onTextEdited: { page.cfg.modem.audio_out = text; page.changed() }
                                }
                            }
                        }
                        Label {
                            visible: window.android
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            color: window.theme.muted
                            text: qsTr("The phone's audio: Android switches to a USB sound card or headset by itself when one is plugged in.")
                        }
                        Field {
                            label: qsTr("Sample rate")
                            ComboBox {
                                width: parent.width
                                model: [22050, 44100, 48000]
                                currentIndex: Math.max(0, model.indexOf(page.cfg.modem.sample_rate))
                                displayText: currentText + " Hz"
                                onActivated: function(i) { page.cfg.modem.sample_rate = model[i]; page.changed() }
                            }
                        }
                        Field {
                            label: qsTr("Speed")
                            ComboBox {
                                width: parent.width
                                readonly property var values: [300, 1200, 9600]
                                model: [qsTr("300 baud (HF)"), qsTr("1200 baud (VHF/UHF)"), qsTr("9600 baud")]
                                currentIndex: Math.max(0, values.indexOf(page.cfg.modem.speed))
                                onActivated: function(i) { page.cfg.modem.speed = values[i]; page.changed() }
                            }
                        }
                        Field {
                            label: qsTr("PTT")
                            ComboBox {
                                width: parent.width
                                readonly property var values: ["none", "rts", "dtr", "cm108"]
                                model: [qsTr("None (VOX)"), qsTr("Serial adapter, RTS"), qsTr("Serial adapter, DTR"),
                                        qsTr("USB sound card GPIO (CM108)")]
                                currentIndex: Math.max(0, values.indexOf(page.cfg.modem.ptt))
                                onActivated: function(i) { page.cfg.modem.ptt = values[i]; pttMode = values[i]; page.changed() }
                            }
                        }
                        Field {
                            label: qsTr("Serial port")
                            visible: (pttMode === "rts" || pttMode === "dtr") && !window.android
                            TextField {
                                width: parent.width
                                text: page.cfg.modem.ptt_device
                                font.family: app.monoFamily
                                placeholderText: app.platform === "windows" ? "COM3" : "/dev/ttyUSB0"
                                onTextEdited: { page.cfg.modem.ptt_device = text; page.changed() }
                            }
                        }
                        Field {
                            label: qsTr("GPIO")
                            visible: pttMode === "cm108"
                            SpinBox {
                                from: 1; to: 8
                                value: page.cfg.modem.gpio
                                onValueModified: { page.cfg.modem.gpio = value; page.changed() }
                            }
                        }
                        Label {
                            visible: window.android && pttMode !== "none"
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            font.pixelSize: 12
                            color: window.theme.muted
                            text: qsTr("The USB adapter is found on its own; Android asks once for the permission to use it.")
                        }
                        Field {
                            label: qsTr("TXDELAY")
                            SpinBox {
                                from: 0; to: 1000; stepSize: 10
                                value: page.cfg.modem.txdelay * 10
                                editable: true
                                textFromValue: function(v) { return v + " ms" }
                                valueFromText: function(t) { return parseInt(t) }
                                onValueModified: { page.cfg.modem.txdelay = Math.round(value / 10); page.changed() }
                            }
                        }
                        Field {
                            label: qsTr("TXTAIL")
                            SpinBox {
                                from: 0; to: 500; stepSize: 10
                                value: page.cfg.modem.txtail * 10
                                editable: true
                                textFromValue: function(v) { return v + " ms" }
                                valueFromText: function(t) { return parseInt(t) }
                                onValueModified: { page.cfg.modem.txtail = Math.round(value / 10); page.changed() }
                            }
                        }
                        WideButton {
                            text: qsTr("Show the generated direwolf.conf")
                            flat: true
                            onClicked: editor.showText(qsTr("Generated direwolf.conf"), app.generatedConfigText(page.cfg))
                        }
                    }
                    WrapSwitch {
                        text: qsTr("Show the modem's messages in the Modem log")
                        checked: page.cfg.modem.echo_log
                        onToggled: { page.cfg.modem.echo_log = checked; page.changed() }
                    }
                }
            }

            // ---- display ------------------------------------------------------------
            SectionTitle { text: qsTr("Display") }
            Card {
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    Field {
                        label: qsTr("Theme")
                        ComboBox {
                            width: parent.width
                            readonly property var values: ["dark", "light", "red", "amber"]
                            model: [qsTr("Dark"), qsTr("Light (sunlight)"), qsTr("Red (night)"), qsTr("Amber (dim light)")]
                            currentIndex: Math.max(0, values.indexOf(page.cfg.ui.theme))
                            onActivated: function(i) { page.cfg.ui.theme = values[i]; page.changed() }
                        }
                    }
                    Field {
                        label: qsTr("Language")
                        ComboBox {
                            width: parent.width
                            readonly property var values: ["", "en", "fr"]
                            model: [qsTr("The system's"), "English", "Fran\u00e7ais"]
                            currentIndex: Math.max(0, values.indexOf(page.cfg.ui.language))
                            onActivated: function(i) { page.cfg.ui.language = values[i]; page.changed() }
                        }
                    }
                }
            }

            // ---- phone ----------------------------------------------------------------
            SectionTitle { text: qsTr("Phone"); visible: window.android }
            Card {
                visible: window.android
                Layout.fillWidth: true
                ColumnLayout {
                    anchors.fill: parent
                    WrapSwitch {
                        text: qsTr("Keep the screen on while AX25Chess is in front")
                        checked: page.cfg.ui.keep_screen_on
                        onToggled: { page.cfg.ui.keep_screen_on = checked; page.changed() }
                    }
                    WrapSwitch {
                        text: qsTr("Keep listening with the screen off")
                        checked: page.cfg.ui.keep_running
                        onToggled: { page.cfg.ui.keep_running = checked; page.changed() }
                    }
                    Label {
                        id: background
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: window.theme.muted
                        text: app.backgroundStatus()
                    }
                    WideButton {
                        text: qsTr("Battery optimisation exemption")
                        onClicked: { app.requestBatteryExemption(); background.text = app.backgroundStatus() }
                    }
                }
            }

            // ---- about ----------------------------------------------------------------
            SectionTitle { text: qsTr("About") }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: window.theme.muted
                text: qsTr("AX25Chess %1 - chess over AX.25 packet radio, CHS-1 protocol. The modem is Dire Wolf by John Langner, WB2OSZ, compiled into the program (GPL). Modem configuration in use: %2").arg(app.version).arg(app.configFilePath)
            }
        }
    }
}
