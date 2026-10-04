/*
 * ModemConfigFile.h - where the modem's direwolf.conf lives, and a starter
 * file for a fresh installation.
 *
 * From AX25Chat (configuration-file helpers of its direwolf.py).  The
 * process supervision that surrounded them is gone: the modem is inside the
 * program, and direwolf.conf is simply the file it loads.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "RadioConfig.h"

#include <QString>
#include <QStringList>

namespace modemconf {

// direwolf.conf in our own settings folder: the default location.
QString userPath();

// Writability is tested by creating a file, not by inspecting permissions:
// on Windows a directory can report as writable and refuse the write.
bool directoryIsWritable(const QString &directory);

// Write a minimal working direwolf.conf, without overwriting an existing
// one.  Returns the path actually written, which falls back to userPath()
// when the requested directory is read-only; empty with error set on
// failure.
QString writeStarter(const QString &path, const QString &callsign, QString *error = nullptr);

// The direwolf.conf the Modem settings stand for: audio devices and rate,
// MODEM speed, PTT (none = VOX, rts or dtr on a serial adapter, cm108 on a
// USB sound card GPIO) and the channel access parameters.
QString generatedText(const QString &callsign, const ModemConfig &modem);

// Write (or rewrite) it.  Returns the path written, empty with error set on
// failure.
QString writeGenerated(const QString &path, const QString &callsign, const ModemConfig &modem,
                       QString *error = nullptr);

// The serial port a PTT line goes to when none was given.
QString defaultSerialPort();

// Common locations for direwolf.conf, best first; empty when none exists.
QString defaultFile();

// The CHANNEL numbers declared in a direwolf.conf, in file order.
QList<int> channelsIn(const QString &path);

} // namespace modemconf
