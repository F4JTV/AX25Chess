/*
 * GameStore.h - games started and not finished, one file each.
 *
 * Several games can run side by side - natural on the radio, where a game
 * spreads over days and correspondents.  A file holds only the compact move
 * list: replayed from the initial position it rebuilds the position, the
 * piece identifiers, castling rights, en passant and the repetition history,
 * all of which a FEN snapshot would lose.
 *
 * The JSON is the Python version's, field for field, and the files of that
 * version (~/.ax25chess/parties) are imported once on the desktop.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AX25CHESS_GAMESTORE_H
#define AX25CHESS_GAMESTORE_H

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

struct SavedGame
{
    QString gid;
    QString myCall;
    QString peerCall;
    QString color = QStringLiteral("W");    // "W" or "B"
    QStringList moves;                      // compact form, "WP5>29"
    quint32 nonce = 0;
    std::optional<quint32> peerNonce;
    int seq = 0;
    double created = 0.0;                   // seconds since the epoch
    double updated = 0.0;
    QString path;

    int plyCount() const { return int(moves.size()); }
    int moveNumber() const { return plyCount() / 2 + 1; }
    QString sideToMove() const { return plyCount() % 2 == 0 ? QStringLiteral("W") : QStringLiteral("B"); }
    bool myTurn() const { return sideToMove() == color; }

    QJsonObject toJson() const;
    static SavedGame fromJson(const QJsonObject &json, const QString &path = QString());
};

class GameStore
{
public:
    // An empty directory means the default: <application data>/games.
    explicit GameStore(const QString &directory = QString());

    QString directory() const { return m_dir; }
    static QString defaultDirectory();

    // Most recent first.
    QList<SavedGame> list() const;
    std::optional<SavedGame> find(const QString &gid, const QString &peer) const;
    int count() const { return int(list().size()); }

    // Atomic write: never a truncated file.  Returns the path, empty on failure.
    QString save(const SavedGame &game);
    bool remove(const QString &gid, const QString &peer);
    bool remove(const SavedGame &game);

    // Copies the Python version's games (~/.ax25chess/parties/*.json) into
    // this store when they are not there yet.  Returns how many.
    int importLegacy(const QString &legacyDirectory);

    QString fileName(const QString &gid, const QString &peer) const;

private:
    std::optional<SavedGame> read(const QString &path) const;

    QString m_dir;
};

#endif // AX25CHESS_GAMESTORE_H
