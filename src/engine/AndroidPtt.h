/*
 * AndroidPtt.h - the USB PTT backend of the core on Android, through JNI.
 *
 * ptt_android.c asks for a device to be opened and for the line to be keyed;
 * this class forwards each request to org.ax25chess.UsbPtt (android/src),
 * which talks to the USB host API: a CM108 sound card's GPIO through a HID
 * output report, or the RTS/DTR line of a USB serial adapter through the
 * adapter's own control request. Installed with install() before the
 * session starts; on other platforms it does nothing.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace androidptt {

// Register the backend with the core.  Returns false when not on Android.
bool install();

// Devices the host can see right now, for a settings page: one line each.
QStringList devices();

} // namespace androidptt
