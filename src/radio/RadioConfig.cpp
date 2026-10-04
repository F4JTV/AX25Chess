/*
 * RadioConfig.cpp - the station, the embedded modem and channel access.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "RadioConfig.h"

#include <QDir>
#include <QStandardPaths>

QStringList StationConfig::digipeaters() const
{
    QStringList out;
    for (const QString &p : path.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString t = p.trimmed().toUpper();
        if (!t.isEmpty()) out << t;
    }
    return out;
}

QJsonObject StationConfig::toJson() const
{
    return {{QStringLiteral("callsign"), callsign}, {QStringLiteral("peer"), peer}, {QStringLiteral("path"), path}};
}

StationConfig StationConfig::fromJson(const QJsonObject &json)
{
    StationConfig c;
    c.callsign = json.value(QStringLiteral("callsign")).toString(c.callsign).trimmed().toUpper();
    c.peer = json.value(QStringLiteral("peer")).toString().trimmed().toUpper();
    c.path = json.value(QStringLiteral("path")).toString().trimmed().toUpper();
    return c;
}

QJsonObject ModemConfig::toJson() const
{
    return {{QStringLiteral("auto_start"), autoStart},
            {QStringLiteral("echo_log"), echoLog},
            {QStringLiteral("own_file"), ownFile},
            {QStringLiteral("config_file"), configFile},
            {QStringLiteral("channel"), channel},
            {QStringLiteral("audio_in"), audioIn},
            {QStringLiteral("audio_out"), audioOut},
            {QStringLiteral("sample_rate"), sampleRate},
            {QStringLiteral("speed"), speed},
            {QStringLiteral("ptt"), ptt},
            {QStringLiteral("ptt_device"), pttDevice},
            {QStringLiteral("gpio"), gpio},
            {QStringLiteral("txdelay"), txdelay},
            {QStringLiteral("txtail"), txtail},
            {QStringLiteral("persistence"), persistence},
            {QStringLiteral("slottime"), slottime},
            {QStringLiteral("full_duplex"), fullDuplex}};
}

ModemConfig ModemConfig::fromJson(const QJsonObject &json)
{
    ModemConfig c;
    c.autoStart = json.value(QStringLiteral("auto_start")).toBool(c.autoStart);
    c.echoLog = json.value(QStringLiteral("echo_log")).toBool(c.echoLog);
    c.ownFile = json.value(QStringLiteral("own_file")).toBool(c.ownFile);
    c.configFile = json.value(QStringLiteral("config_file")).toString(c.configFile);
    c.channel = qBound(0, json.value(QStringLiteral("channel")).toInt(c.channel), 15);
    c.audioIn = json.value(QStringLiteral("audio_in")).toString(c.audioIn).trimmed();
    c.audioOut = json.value(QStringLiteral("audio_out")).toString(c.audioOut).trimmed();
    if (c.audioIn.isEmpty()) c.audioIn = QStringLiteral("default");
    if (c.audioOut.isEmpty()) c.audioOut = c.audioIn;
    c.sampleRate = json.value(QStringLiteral("sample_rate")).toInt(c.sampleRate);
    if (c.sampleRate != 22050 && c.sampleRate != 44100 && c.sampleRate != 48000) c.sampleRate = 44100;
    c.speed = json.value(QStringLiteral("speed")).toInt(c.speed);
    if (c.speed != 300 && c.speed != 1200 && c.speed != 9600) c.speed = 1200;
    c.ptt = json.value(QStringLiteral("ptt")).toString(c.ptt);
    if (c.ptt != QLatin1String("rts") && c.ptt != QLatin1String("dtr") && c.ptt != QLatin1String("cm108")) {
        c.ptt = QStringLiteral("none");
    }
    c.pttDevice = json.value(QStringLiteral("ptt_device")).toString().trimmed();
    c.gpio = qBound(1, json.value(QStringLiteral("gpio")).toInt(c.gpio), 8);
    c.txdelay = qBound(0, json.value(QStringLiteral("txdelay")).toInt(c.txdelay), 255);
    c.txtail = qBound(0, json.value(QStringLiteral("txtail")).toInt(c.txtail), 255);
    c.persistence = qBound(0, json.value(QStringLiteral("persistence")).toInt(c.persistence), 255);
    c.slottime = qBound(0, json.value(QStringLiteral("slottime")).toInt(c.slottime), 255);
    c.fullDuplex = json.value(QStringLiteral("full_duplex")).toBool(c.fullDuplex);
    return c;
}

QJsonObject ChannelConfig::toJson() const
{
    return {{QStringLiteral("use_dcd"), useDcd},
            {QStringLiteral("rx_hold_off_ms"), rxHoldOffMs},
            {QStringLiteral("inter_frame_gap_ms"), interFrameGapMs},
            {QStringLiteral("random_jitter_ms"), randomJitterMs},
            {QStringLiteral("max_defer_seconds"), maxDeferSeconds},
            {QStringLiteral("wait_for_txbuf_empty"), waitForTxbufEmpty},
            {QStringLiteral("treat_own_echo_as_busy"), treatOwnEchoAsBusy}};
}

ChannelConfig ChannelConfig::fromJson(const QJsonObject &json)
{
    ChannelConfig c;
    c.useDcd = json.value(QStringLiteral("use_dcd")).toBool(c.useDcd);
    c.rxHoldOffMs = qMax(0, json.value(QStringLiteral("rx_hold_off_ms")).toInt(c.rxHoldOffMs));
    c.interFrameGapMs = qMax(0, json.value(QStringLiteral("inter_frame_gap_ms")).toInt(c.interFrameGapMs));
    c.randomJitterMs = qMax(0, json.value(QStringLiteral("random_jitter_ms")).toInt(c.randomJitterMs));
    c.maxDeferSeconds = qMax(0, json.value(QStringLiteral("max_defer_seconds")).toInt(c.maxDeferSeconds));
    c.waitForTxbufEmpty = json.value(QStringLiteral("wait_for_txbuf_empty")).toBool(c.waitForTxbufEmpty);
    c.treatOwnEchoAsBusy = json.value(QStringLiteral("treat_own_echo_as_busy")).toBool(c.treatOwnEchoAsBusy);
    return c;
}

QString RadioConfig::configDir()
{
    // No organisation name is set, so this is ~/.config/AX25Chess on Linux,
    // %LOCALAPPDATA%\AX25Chess on Windows, the private files on Android.
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}
