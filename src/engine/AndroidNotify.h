/*
 * AndroidNotify.h - sounds and system notifications on Android, through JNI.
 *
 * The Qt Widgets beep does nothing on Android and a fresh installation has
 * no sound file, so the alerts go through org.ax25chess.Notifications: a
 * ToneGenerator beep, and a system notification when traffic arrives while
 * the application is in the background. No-ops elsewhere.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace androidnotify {

// Hand the application context to the Java side; false off Android.
bool install();
// Ask for the notification permission (Android 13+); asynchronous.
void requestPermission();
void beep();
void show(const QString &title, const QString &text);
bool available();

} // namespace androidnotify
