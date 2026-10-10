/*
 * AppConfig.h - everything the operator sets, in config.json.
 *
 * The radio part (station, modem, channel access) is RadioConfig; the rest
 * belongs to the game and the interface.  The QML side sees the whole as a
 * nested map (AppController::config) and saves it back the same way.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "RadioConfig.h"

#include <QJsonObject>
#include <QString>
#include <QVariantMap>

struct GameConfig
{
    int retrySeconds = 14;           // CHS-1 retransmission delay, 5..120 s
    bool showUids = true;            // piece identifiers on the board
    bool showNumbers = true;         // square numbers on the board

    QJsonObject toJson() const;
    static GameConfig fromJson(const QJsonObject &json);
};

struct UiConfig
{
    QString theme = QStringLiteral("dark");     // dark, light, red, amber
    QString language;                          // "en", "fr", empty = the system's
    bool keepScreenOn = true;                  // Android, while in front
    bool keepRunning = true;                   // Android, foreground service off screen
    int windowWidth = 0;                       // desktop, last size
    int windowHeight = 0;

    QJsonObject toJson() const;
    static UiConfig fromJson(const QJsonObject &json);
};

struct AppConfig
{
    RadioConfig radio;
    GameConfig game;
    UiConfig ui;
    bool configured = false;         // false until the first save
    // config.json written by 2.0.3 or older kept TXTAIL at the old default,
    // 50 ms; it is raised to 100 ms on reading (see RadioConfig.h).
    bool txtailRaised = false;

    QJsonObject toJson() const;
    static AppConfig fromJson(const QJsonObject &json);

    QVariantMap toMap() const { return toJson().toVariantMap(); }
    static AppConfig fromMap(const QVariantMap &map) { return fromJson(QJsonObject::fromVariantMap(map)); }

    static QString configPath();
    // Missing or unreadable file: the defaults, configured == false.
    static AppConfig load(const QString &path = QString());
    bool save(const QString &path = QString(), QString *error = nullptr) const;
};
