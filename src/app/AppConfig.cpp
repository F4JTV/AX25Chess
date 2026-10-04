/*
 * AppConfig.cpp - everything the operator sets, in config.json.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AppConfig.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

QJsonObject GameConfig::toJson() const
{
    return {{QStringLiteral("retry_seconds"), retrySeconds},
            {QStringLiteral("show_uids"), showUids},
            {QStringLiteral("show_numbers"), showNumbers}};
}

GameConfig GameConfig::fromJson(const QJsonObject &json)
{
    GameConfig c;
    c.retrySeconds = qBound(5, json.value(QStringLiteral("retry_seconds")).toInt(c.retrySeconds), 120);
    c.showUids = json.value(QStringLiteral("show_uids")).toBool(c.showUids);
    c.showNumbers = json.value(QStringLiteral("show_numbers")).toBool(c.showNumbers);
    return c;
}

QJsonObject UiConfig::toJson() const
{
    return {{QStringLiteral("theme"), theme},
            {QStringLiteral("language"), language},
            {QStringLiteral("keep_screen_on"), keepScreenOn},
            {QStringLiteral("keep_running"), keepRunning},
            {QStringLiteral("window_width"), windowWidth},
            {QStringLiteral("window_height"), windowHeight}};
}

UiConfig UiConfig::fromJson(const QJsonObject &json)
{
    UiConfig c;
    c.theme = json.value(QStringLiteral("theme")).toString(c.theme);
    if (c.theme != QLatin1String("light") && c.theme != QLatin1String("red") && c.theme != QLatin1String("amber")) {
        c.theme = QStringLiteral("dark");
    }
    c.language = json.value(QStringLiteral("language")).toString();
    if (c.language != QLatin1String("en") && c.language != QLatin1String("fr")) c.language.clear();
    c.keepScreenOn = json.value(QStringLiteral("keep_screen_on")).toBool(c.keepScreenOn);
    c.keepRunning = json.value(QStringLiteral("keep_running")).toBool(c.keepRunning);
    c.windowWidth = qMax(0, json.value(QStringLiteral("window_width")).toInt());
    c.windowHeight = qMax(0, json.value(QStringLiteral("window_height")).toInt());
    return c;
}

QJsonObject AppConfig::toJson() const
{
    return {{QStringLiteral("station"), radio.station.toJson()},
            {QStringLiteral("modem"), radio.modem.toJson()},
            {QStringLiteral("channel"), radio.channel.toJson()},
            {QStringLiteral("game"), game.toJson()},
            {QStringLiteral("ui"), ui.toJson()},
            {QStringLiteral("configured"), configured}};
}

AppConfig AppConfig::fromJson(const QJsonObject &json)
{
    AppConfig c;
    c.radio.station = StationConfig::fromJson(json.value(QStringLiteral("station")).toObject());
    c.radio.modem = ModemConfig::fromJson(json.value(QStringLiteral("modem")).toObject());
    c.radio.channel = ChannelConfig::fromJson(json.value(QStringLiteral("channel")).toObject());
    c.game = GameConfig::fromJson(json.value(QStringLiteral("game")).toObject());
    c.ui = UiConfig::fromJson(json.value(QStringLiteral("ui")).toObject());
    c.configured = json.value(QStringLiteral("configured")).toBool(false);
    return c;
}

QString AppConfig::configPath()
{
    return QDir(RadioConfig::configDir()).filePath(QStringLiteral("config.json"));
}

AppConfig AppConfig::load(const QString &pathIn)
{
    const QString path = pathIn.isEmpty() ? configPath() : pathIn;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        AppConfig fresh;
#ifdef Q_OS_ANDROID
        // Oboe resamples, and phones run their audio at 48 kHz.
        fresh.radio.modem.sampleRate = 48000;
#endif
        return fresh;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return fromJson(doc.object());
}

bool AppConfig::save(const QString &pathIn, QString *error) const
{
    const QString path = pathIn.isEmpty() ? configPath() : pathIn;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    AppConfig copy = *this;
    copy.configured = true;
    file.write(QJsonDocument(copy.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}
