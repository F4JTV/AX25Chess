/*
 * RadioConfig.h - the station, the embedded modem and channel access.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

// Who we are and whom we play.
struct StationConfig
{
    QString callsign = QStringLiteral("N0CALL");     // with the SSID, e.g. N0CALL-7
    QString peer;                                    // the correspondent, same form
    QString path;                                    // digipeaters, comma separated

    QStringList digipeaters() const;
    QJsonObject toJson() const;
    static StationConfig fromJson(const QJsonObject &json);
};

// The embedded modem.  Its direwolf.conf is either generated from these
// choices (the default, and the only way on a phone) or a file of the
// operator's own.
struct ModemConfig
{
    bool autoStart = true;           // start the modem with the application
    bool echoLog = true;             // the modem's own messages in the modem log
    bool ownFile = false;            // use configFile as it is
    QString configFile;              // the operator's direwolf.conf when ownFile
    int channel = 0;                 // CHANNEL we transmit and listen on

    // What the generated file says.
    QString audioIn = QStringLiteral("default");
    QString audioOut = QStringLiteral("default");
    int sampleRate = 44100;
    int speed = 1200;                // MODEM: 300, 1200 or 9600
    QString ptt = QStringLiteral("none");   // none (VOX), rts, dtr, cm108
    QString pttDevice;               // serial port for rts/dtr; optional HID path for cm108
    int gpio = 3;                    // CM108 GPIO pin
    int txdelay = 30;                // * 10 ms
    int txtail = 5;                  // * 10 ms
    int persistence = 63;            // transmit probability (p + 1) / 256
    int slottime = 10;               // * 10 ms
    bool fullDuplex = false;

    QJsonObject toJson() const;
    static ModemConfig fromJson(const QJsonObject &json);
};

// Frequency-busy logic used before releasing a frame to the modem.
struct ChannelConfig
{
    bool useDcd = true;              // the modem's carrier detect counts as busy
    int rxHoldOffMs = 1500;          // quiet time required after the last activity
    int interFrameGapMs = 700;       // spacing between our own frames
    int randomJitterMs = 400;        // extra random delay, avoids collisions
    int maxDeferSeconds = 120;       // give up after this long, 0 = never
    bool waitForTxbufEmpty = true;   // wait for the modem's transmit queue to drain
    bool treatOwnEchoAsBusy = false;

    QJsonObject toJson() const;
    static ChannelConfig fromJson(const QJsonObject &json);
};

struct RadioConfig
{
    StationConfig station;
    ModemConfig modem;
    ChannelConfig channel;

    QString myCall() const { return station.callsign.trimmed().toUpper(); }

    // The settings folder: config.json, the generated direwolf.conf.
    static QString configDir();
};
