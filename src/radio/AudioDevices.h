/*
 * AudioDevices.h - the sound cards the modem can use, for the settings.
 *
 * Each entry pairs a label for the operator with the value ADEVICE expects
 * on this platform: an ALSA name on Linux (by card id, stable across
 * reboots, unlike card numbers), a device number on Windows (Dire Wolf's
 * own convention), "default" on Android, where the system routes audio to a
 * USB sound card on its own.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QList>
#include <QString>

struct AudioDevice
{
    QString value;      // what goes into ADEVICE
    QString label;      // what the operator sees
};

namespace audiodevices {

QList<AudioDevice> inputs();
QList<AudioDevice> outputs();

// Linux: the cards of /proc/asound/cards, given its text (tests).
QList<AudioDevice> parseAsoundCards(const QString &text);

} // namespace audiodevices
