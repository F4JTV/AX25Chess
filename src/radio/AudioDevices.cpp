/*
 * AudioDevices.cpp - the sound cards the modem can use, for the settings.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AudioDevices.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

namespace audiodevices {

static AudioDevice systemDefault()
{
    return {QStringLiteral("default"), QCoreApplication::translate("AudioDevices", "System default")};
}

QList<AudioDevice> parseAsoundCards(const QString &text)
{
    // " 1 [Device         ]: USB-Audio - USB Audio Device"
    static const QRegularExpression line(QStringLiteral("^\\s*(\\d+)\\s+\\[([^\\]]+)\\]\\s*:\\s*(.*)$"));
    QList<AudioDevice> out;
    for (const QString &l : text.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = line.match(l);
        if (!m.hasMatch()) continue;
        const QString id = m.captured(2).trimmed();
        QString description = m.captured(3).trimmed();
        const int dash = description.indexOf(QStringLiteral(" - "));
        if (dash >= 0) description = description.mid(dash + 3).trimmed();
        const QString value = QStringLiteral("plughw:CARD=%1,DEV=0").arg(id);
        out << AudioDevice{value, QStringLiteral("%1  (%2)").arg(description, value)};
    }
    return out;
}

#ifdef Q_OS_WIN
static QList<AudioDevice> windowsDevices(bool input)
{
    QList<AudioDevice> out;
    const UINT n = input ? waveInGetNumDevs() : waveOutGetNumDevs();
    for (UINT i = 0; i < n && i < 100; i++) {
        QString name;
        if (input) {
            WAVEINCAPSW caps;
            if (waveInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
            name = QString::fromWCharArray(caps.szPname);
        } else {
            WAVEOUTCAPSW caps;
            if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
            name = QString::fromWCharArray(caps.szPname);
        }
        out << AudioDevice{QString::number(i), QStringLiteral("%1: %2").arg(i).arg(name)};
    }
    return out;
}
#endif

static QList<AudioDevice> devices(bool input)
{
    QList<AudioDevice> out{systemDefault()};
#if defined(Q_OS_WIN)
    out << windowsDevices(input);
#elif defined(Q_OS_ANDROID)
    Q_UNUSED(input);
#elif defined(Q_OS_LINUX)
    Q_UNUSED(input);
    QFile cards(QStringLiteral("/proc/asound/cards"));
    if (cards.open(QIODevice::ReadOnly | QIODevice::Text)) out << parseAsoundCards(QString::fromUtf8(cards.readAll()));
#else
    Q_UNUSED(input);
#endif
    return out;
}

QList<AudioDevice> inputs()
{
    return devices(true);
}

QList<AudioDevice> outputs()
{
    return devices(false);
}

} // namespace audiodevices
