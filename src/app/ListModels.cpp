/*
 * ListModels.cpp - the lists the interface shows: logs, moves, saved games.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ListModels.h"

#include <QCoreApplication>
#include <QLocale>

// ---- LogModel -----------------------------------------------------------

LogModel::LogModel(int maxRows, QObject *parent)
    : QAbstractListModel(parent), m_max(qMax(10, maxRows))
{
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case TimeRole: return r.time.toString(QStringLiteral("HH:mm:ss"));
    case LevelRole: return r.level;
    case TextRole:
    case Qt::DisplayRole: return r.text;
    default: return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {{TimeRole, "time"}, {LevelRole, "level"}, {TextRole, "text"}};
}

void LogModel::append(const QString &level, const QString &text)
{
    if (m_rows.size() >= m_max) {
        const int drop = m_max / 10;
        beginRemoveRows(QModelIndex(), 0, drop - 1);
        m_rows.erase(m_rows.begin(), m_rows.begin() + drop);
        endRemoveRows();
    }
    beginInsertRows(QModelIndex(), int(m_rows.size()), int(m_rows.size()));
    m_rows.append({QDateTime::currentDateTime(), level, text});
    endInsertRows();
    emit countChanged();
}

void LogModel::clear()
{
    if (m_rows.isEmpty()) return;
    beginRemoveRows(QModelIndex(), 0, int(m_rows.size()) - 1);
    m_rows.clear();
    endRemoveRows();
    emit countChanged();
}

QString LogModel::plainText() const
{
    QString out;
    for (const Row &r : m_rows) {
        out += r.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) + QLatin1Char(' ') + r.text + QLatin1Char('\n');
    }
    return out;
}

// ---- MoveListModel --------------------------------------------------------

int MoveListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant MoveListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case NumberRole: return r.number;
    case SideRole:
        return r.white ? QCoreApplication::translate("GameSession", "White")
                       : QCoreApplication::translate("GameSession", "Black");
    case SanRole:
    case Qt::DisplayRole: return r.san;
    case UidRole: return r.uid;
    case SquareRole: return r.square;
    case ByPeerRole: return r.byPeer;
    default: return {};
    }
}

QHash<int, QByteArray> MoveListModel::roleNames() const
{
    return {{NumberRole, "number"}, {SideRole, "side"}, {SanRole, "san"}, {UidRole, "uid"},
            {SquareRole, "square"}, {ByPeerRole, "byPeer"}};
}

void MoveListModel::append(const Row &row)
{
    beginInsertRows(QModelIndex(), int(m_rows.size()), int(m_rows.size()));
    m_rows.append(row);
    endInsertRows();
    emit countChanged();
}

void MoveListModel::setRows(const QList<Row> &rows)
{
    // A whole new history (a resynchronisation, a game resumed): the one
    // case where starting the list again is the right thing.
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}

void MoveListModel::retranslate()
{
    if (!m_rows.isEmpty()) emit dataChanged(index(0), index(int(m_rows.size()) - 1), {SideRole});
}

// ---- GameListModel ------------------------------------------------------------

int GameListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_games.size());
}

QString GameListModel::ageText(double updated, double now)
{
    if (updated <= 0) return QStringLiteral("-");
    const double delta = now - updated;
    if (delta < 90) return QCoreApplication::translate("GameListModel", "just now");
    if (delta < 3600) return QCoreApplication::translate("GameListModel", "%1 min ago").arg(int(delta / 60));
    if (delta < 86400) return QCoreApplication::translate("GameListModel", "%1 h ago").arg(int(delta / 3600));
    if (delta < 7 * 86400) return QCoreApplication::translate("GameListModel", "%1 d ago").arg(int(delta / 86400));
    return QDateTime::fromMSecsSinceEpoch(qint64(updated * 1000)).date().toString(QStringLiteral("yyyy-MM-dd"));
}

QVariant GameListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_games.size()) return {};
    const SavedGame &g = m_games.at(index.row());
    switch (role) {
    case GidRole: return g.gid;
    case PeerRole: return g.peerCall.isEmpty() ? QStringLiteral("-") : g.peerCall;
    case MyCallRole: return g.myCall;
    case ColourRole:
        return g.color == QLatin1String("B") ? QCoreApplication::translate("GameSession", "Black")
                                             : QCoreApplication::translate("GameSession", "White");
    case ProgressRole:
        return QCoreApplication::translate("GameListModel", "move %1, %2 half-moves").arg(g.moveNumber()).arg(g.plyCount());
    case MyTurnRole: return g.myTurn();
    case AgeRole: return ageText(g.updated, QDateTime::currentMSecsSinceEpoch() / 1000.0);
    case CurrentRole: return g.gid == m_current;
    default: return {};
    }
}

QHash<int, QByteArray> GameListModel::roleNames() const
{
    return {{GidRole, "gid"}, {PeerRole, "peer"}, {MyCallRole, "myCall"}, {ColourRole, "colour"},
            {ProgressRole, "progress"}, {MyTurnRole, "myTurn"}, {AgeRole, "age"}, {CurrentRole, "current"}};
}

int GameListModel::waiting() const
{
    int n = 0;
    for (const SavedGame &g : m_games) {
        if (g.myTurn()) n++;
    }
    return n;
}

void GameListModel::setGames(const QList<SavedGame> &games, const QString &currentGid)
{
    beginResetModel();
    m_games = games;
    m_current = currentGid;
    endResetModel();
    emit countChanged();
}
