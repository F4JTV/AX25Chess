/*
 * ListModels.h - the lists the interface shows: logs, moves, saved games.
 *
 * Rows are inserted and removed where they change, never by resetting the
 * model: a reset sends a list back to its top and loses the reader's place
 * (a lesson of AX25Chat's station and log lists).
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "GameStore.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QString>

class LogModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { TimeRole = Qt::UserRole + 1, LevelRole, TextRole };

    explicit LogModel(int maxRows = 3000, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    void append(const QString &level, const QString &text);
    Q_INVOKABLE void clear();
    // Plain text, one line per row, for an export or the clipboard.
    Q_INVOKABLE QString plainText() const;

signals:
    void countChanged();

private:
    struct Row { QDateTime time; QString level; QString text; };
    QList<Row> m_rows;
    int m_max;
};

class MoveListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role { NumberRole = Qt::UserRole + 1, SideRole, SanRole, UidRole, SquareRole, ByPeerRole };

    struct Row
    {
        int number = 1;          // full move number
        bool white = true;       // which side played it
        QString san;
        QString uid;
        QString square;          // "29 (e4)"
        bool byPeer = false;
    };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    void append(const Row &row);
    void setRows(const QList<Row> &rows);
    void retranslate();          // the side column follows the language

signals:
    void countChanged();

private:
    QList<Row> m_rows;
};

class GameListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int waiting READ waiting NOTIFY countChanged)

public:
    enum Role { GidRole = Qt::UserRole + 1, PeerRole, MyCallRole, ColourRole, ProgressRole, MyTurnRole,
                AgeRole, CurrentRole };

    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_games.size()); }
    int waiting() const;
    void setGames(const QList<SavedGame> &games, const QString &currentGid);
    const SavedGame *game(int row) const { return row >= 0 && row < m_games.size() ? &m_games[row] : nullptr; }

    // "just now", "12 min ago", "3 h ago", "2 d ago", or a date.
    static QString ageText(double updated, double now);

signals:
    void countChanged();

private:
    QList<SavedGame> m_games;
    QString m_current;
};
