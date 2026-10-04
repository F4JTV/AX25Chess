/*
 * AndroidUi.cpp - what the mobile interface asks of the Android system.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AndroidUi.h"

#include <QString>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>

void androidui::hideSystemBars()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) return;
        QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) return;
        if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
            QJniObject controller = window.callObjectMethod("getInsetsController", "()Landroid/view/WindowInsetsController;");
            if (!controller.isValid()) return;
            const jint statusBars = QJniObject::callStaticMethod<jint>("android/view/WindowInsets$Type", "statusBars", "()I");
            const jint navigationBars = QJniObject::callStaticMethod<jint>("android/view/WindowInsets$Type", "navigationBars", "()I");
            // BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            controller.callMethod<void>("setSystemBarsBehavior", "(I)V", static_cast<jint>(2));
            controller.callMethod<void>("hide", "(I)V", static_cast<jint>(statusBars | navigationBars));
        } else {
            // Before Android 11: the old visibility flags (immersive sticky,
            // hide navigation, fullscreen, layout stable/hide/fullscreen).
            QJniObject decor = window.callObjectMethod("getDecorView", "()Landroid/view/View;");
            if (!decor.isValid()) return;
            const jint flags = 0x00001000 | 0x00000004 | 0x00000002 | 0x00000100 | 0x00000200 | 0x00000400;
            decor.callMethod<void>("setSystemUiVisibility", "(I)V", flags);
        }
    });
}

bool androidui::installDeviceServices()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return false;
    QJniObject::callStaticMethod<void>("org/ax25chess/DeviceServices", "setContext", "(Landroid/content/Context;)V", context.object());
    return true;
}

bool androidui::locationEnabled()
{
    return QJniObject::callStaticMethod<jboolean>("org/ax25chess/DeviceServices", "locationEnabled", "()Z");
}

void androidui::openLocationSettings()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject::callStaticMethod<void>("org/ax25chess/DeviceServices", "openLocationSettings", "()V");
    });
}

void androidui::keepScreenOn(bool on)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([on] {
        QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) return;
        QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) return;
        const jint FLAG_KEEP_SCREEN_ON = 0x00000080;
        window.callMethod<void>(on ? "addFlags" : "clearFlags", "(I)V", FLAG_KEEP_SCREEN_ON);
    });
}

void androidui::startKeepAlive()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject context = QNativeInterface::QAndroidApplication::context();
        if (!context.isValid()) return;
        QJniObject::callStaticMethod<void>("org/ax25chess/KeepAliveService", "start", "(Landroid/content/Context;)V", context.object());
    });
}

void androidui::stopKeepAlive()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return;
    QJniObject::callStaticMethod<void>("org/ax25chess/KeepAliveService", "stop", "(Landroid/content/Context;)V", context.object());
}

QString androidui::keepAliveStatus()
{
    const QJniObject status = QJniObject::callStaticObjectMethod("org/ax25chess/KeepAliveService", "status", "()Ljava/lang/String;");
    return status.isValid() ? status.toString() : QStringLiteral("unknown");
}

bool androidui::ignoringBatteryOptimizations()
{
    return QJniObject::callStaticMethod<jboolean>("org/ax25chess/DeviceServices", "ignoringBatteryOptimizations", "()Z");
}

void androidui::requestIgnoreBatteryOptimizations()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject::callStaticMethod<void>("org/ax25chess/DeviceServices", "requestIgnoreBatteryOptimizations", "()V");
    });
}

#else
void androidui::hideSystemBars() {}
void androidui::keepScreenOn(bool) {}
void androidui::startKeepAlive() {}
void androidui::stopKeepAlive() {}
QString androidui::keepAliveStatus() { return QStringLiteral("not on Android"); }
bool androidui::ignoringBatteryOptimizations() { return true; }
void androidui::requestIgnoreBatteryOptimizations() {}
bool androidui::installDeviceServices() { return false; }
bool androidui::locationEnabled() { return true; }
void androidui::openLocationSettings() {}
#endif
