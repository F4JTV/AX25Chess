/*
 * AndroidUi.h - what the mobile interface asks of the Android system.
 *
 * Full screen: the status and navigation bars are hidden, and come back
 * transiently on a swipe from the edge, the same behaviour RemoteRig and
 * FT891Remote have.  Here it is done from C++ through the activity's
 * window, so the application needs no activity subclass and no manifest
 * of its own.  A no-op on other platforms.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QString>

namespace androidui {

// Hide the system bars.  Called at start and whenever the application
// comes back to the foreground: the bars return after a swipe or a switch
// to another application, and would otherwise stay.
void hideSystemBars();

// Hand the application context to DeviceServices.java; false off Android.
bool installDeviceServices();
// Whether location is switched on at device level (true off Android).
bool locationEnabled();
// The system's location settings screen.
void openLocationSettings();

// Keep the screen on while the application is in front (the window flag
// Android provides for that, no permission needed); false clears it.
void keepScreenOn(bool on);

// The foreground service that keeps the process alive off screen
// (KeepAliveService.java); no-ops off Android.
void startKeepAlive();
void stopKeepAlive();
// What the service reports: "running, types N", "failed: ...", "not running".
QString keepAliveStatus();
// Doze exemption (DeviceServices.java): whether granted, and the system's request dialog.
bool ignoringBatteryOptimizations();
void requestIgnoreBatteryOptimizations();

} // namespace androidui
