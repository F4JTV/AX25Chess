/*
 * GameStore.cpp - games started and not finished, one file each.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "GameStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>

QJsonObject SavedGame::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("gid"), gid);
    o.insert(QStringLiteral("call"), myCall);
    o.insert(QStringLiteral("peer"), peerCall);
    o.insert(QStringLiteral("color"), color);
    o.insert(QStringLiteral("moves"), QJsonArray::fromStringList(moves));
    o.insert(QStringLiteral("nonce"), double(nonce));
    o.insert(QStringLiteral("peer_nonce"), peerNonce ? QJsonValue(double(*peerNonce)) : QJsonValue());
    o.insert(QStringLiteral("seq"), seq);
    o.insert(QStringLiteral("created"), created);
    o.insert(QStringLiteral("updated"), updated);
    return o;
}

SavedGame SavedGame::fromJson(const QJsonObject &json, const QString &path)
{
    SavedGame g;
    g.gid = json.value(QStringLiteral("gid")).toVariant().toString();
    if (g.gid.isEmpty()) g.gid = QStringLiteral("0000");
    g.myCall = json.value(QStringLiteral("call")).toString();
    g.peerCall = json.value(QStringLiteral("peer")).toString();
    g.color = json.value(QStringLiteral("color")).toString();
    if (g.color != QLatin1String("B")) g.color = QStringLiteral("W");
    for (const QJsonValue &v : json.value(QStringLiteral("moves")).toArray()) g.moves << v.toVariant().toString();
    g.nonce = quint32(json.value(QStringLiteral("nonce")).toDouble(0));
    const QJsonValue pn = json.value(QStringLiteral("peer_nonce"));
    if (pn.isDouble()) g.peerNonce = quint32(pn.toDouble());
    g.seq = json.value(QStringLiteral("seq")).toInt(0);
    g.created = json.value(QStringLiteral("created")).toDouble(0);
    g.updated = json.value(QStringLiteral("updated")).toDouble(0);
    g.path = path;
    return g;
}

GameStore::GameStore(const QString &directory)
    : m_dir(directory.isEmpty() ? defaultDirectory() : directory)
{
}

QString GameStore::defaultDirectory()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("games"));
}

QString GameStore::fileName(const QString &gid, const QString &peer) const
{
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9_-]"));
    QString safeGid = gid.isEmpty() ? QStringLiteral("0000") : gid;
    QString safePeer = peer.isEmpty() ? QStringLiteral("UNKNOWN") : peer.toUpper();
    safeGid.replace(unsafe, QStringLiteral("_"));
    safePeer.replace(unsafe, QStringLiteral("_"));
    return QDir(m_dir).filePath(QStringLiteral("%1-%2.json").arg(safeGid, safePeer));
}

std::optional<SavedGame> GameStore::read(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("moves")).toArray().isEmpty() && o.value(QStringLiteral("gid")).toString().isEmpty()) {
        return std::nullopt;
    }
    SavedGame g = SavedGame::fromJson(o, path);
    if (g.updated <= 0) g.updated = QFileInfo(path).lastModified().toMSecsSinceEpoch() / 1000.0;
    return g;
}

QList<SavedGame> GameStore::list() const
{
    QList<SavedGame> games;
    const QDir dir(m_dir);
    if (!dir.exists()) return games;
    for (const QString &name : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        if (auto g = read(dir.filePath(name))) games << *g;
    }
    std::sort(games.begin(), games.end(), [](const SavedGame &a, const SavedGame &b) { return a.updated > b.updated; });
    return games;
}

std::optional<SavedGame> GameStore::find(const QString &gid, const QString &peer) const
{
    const QString path = fileName(gid, peer);
    return QFileInfo(path).isFile() ? read(path) : std::nullopt;
}

QString GameStore::save(const SavedGame &gameIn)
{
    SavedGame game = gameIn;
    const QString path = fileName(game.gid, game.peerCall);
    const double now = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    const std::optional<SavedGame> existing = QFileInfo(path).isFile() ? read(path) : std::nullopt;
    game.created = (existing && existing->created > 0) ? existing->created : (game.created > 0 ? game.created : now);
    game.updated = now;
    if (!QDir().mkpath(m_dir)) return QString();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return QString();
    file.write(QJsonDocument(game.toJson()).toJson(QJsonDocument::Indented));
    return file.commit() ? path : QString();
}

bool GameStore::remove(const QString &gid, const QString &peer)
{
    return QFile::remove(fileName(gid, peer));
}

bool GameStore::remove(const SavedGame &game)
{
    if (!game.path.isEmpty()) return QFile::remove(game.path);
    return remove(game.gid, game.peerCall);
}

int GameStore::importLegacy(const QString &legacyDirectory)
{
    const QDir legacy(legacyDirectory);
    if (!legacy.exists()) return 0;
    int imported = 0;
    for (const QString &name : legacy.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        const std::optional<SavedGame> g = read(legacy.filePath(name));
        if (!g || g->moves.isEmpty()) continue;
        if (QFileInfo::exists(fileName(g->gid, g->peerCall))) continue;
        if (!QDir().mkpath(m_dir)) break;
        if (QFile::copy(legacy.filePath(name), fileName(g->gid, g->peerCall))) imported++;
    }
    return imported;
}
