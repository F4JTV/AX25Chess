/*
 * AndroidNotify.cpp - sounds and system notifications on Android, through JNI.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AndroidNotify.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>

namespace {
const char *const NotificationsClass = "org/ax25chess/Notifications";
}

bool androidnotify::install()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return false;
    QJniObject::callStaticMethod<void>(NotificationsClass, "setContext", "(Landroid/content/Context;)V", context.object());
    return true;
}

void androidnotify::requestPermission()
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([] {
        QJniObject::callStaticMethod<void>(NotificationsClass, "requestPermission", "()V");
    });
}

void androidnotify::beep()
{
    QJniObject::callStaticMethod<void>(NotificationsClass, "beep", "()V");
}

void androidnotify::show(const QString &title, const QString &text)
{
    QJniObject::callStaticMethod<void>(NotificationsClass, "show", "(Ljava/lang/String;Ljava/lang/String;)V",
                                       QJniObject::fromString(title).object<jstring>(),
                                       QJniObject::fromString(text).object<jstring>());
}

bool androidnotify::available()
{
    return true;
}

#else

bool androidnotify::install() { return false; }
void androidnotify::requestPermission() {}
void androidnotify::beep() {}
void androidnotify::show(const QString &, const QString &) {}
bool androidnotify::available() { return false; }

#endif
