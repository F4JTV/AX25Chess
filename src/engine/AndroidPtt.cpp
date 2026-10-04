/*
 * AndroidPtt.cpp - the USB PTT backend of the core on Android, through JNI.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AndroidPtt.h"

#include <QStringList>

extern "C" {
#include "dw_embed.h"
}

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

namespace {

const char *const UsbPttClass = "org/ax25chess/UsbPtt";

int backendOpen(void *, int chan, int method, const char *device, int gpio, int line, int line2)
{
    Q_UNUSED(device);
    const jboolean ok = QJniObject::callStaticMethod<jboolean>(UsbPttClass, "open", "(IIIII)Z",
                                                               static_cast<jint>(chan), static_cast<jint>(method),
                                                               static_cast<jint>(gpio), static_cast<jint>(line),
                                                               static_cast<jint>(line2));
    return ok ? 0 : -1;
}

void backendSet(void *, int chan, int on)
{
    QJniObject::callStaticMethod<void>(UsbPttClass, "set", "(IZ)V", static_cast<jint>(chan), static_cast<jboolean>(on != 0));
}

void backendClose(void *)
{
    QJniObject::callStaticMethod<void>(UsbPttClass, "close", "()V");
}

} // namespace

bool androidptt::install()
{
    // The Java side needs a Context for the USB manager and the broadcast
    // receiver; the public native interface hands it over.
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (context.isValid()) {
        QJniObject::callStaticMethod<void>(UsbPttClass, "setContext", "(Landroid/content/Context;)V", context.object());
    }
    dw_embed_ptt_backend_t backend{};
    backend.user = nullptr;
    backend.open = backendOpen;
    backend.set = backendSet;
    backend.close = backendClose;
    dw_embed_set_ptt_backend(&backend);
    return true;
}

QStringList androidptt::devices()
{
    QStringList list;
    const QJniObject result = QJniObject::callStaticObjectMethod(UsbPttClass, "devices", "()Ljava/lang/String;");
    const QString text = result.isValid() ? result.toString() : QString();
    for (const QString &line : text.split('\n')) if (!line.trimmed().isEmpty()) list << line.trimmed();
    return list;
}

#else

bool androidptt::install()
{
    return false;
}

QStringList androidptt::devices()
{
    return {};
}

#endif
