/*
 * BoardView.h - the chessboard, painted.
 *
 * The board is treated as the patch panel of a radio workshop: every square
 * carries its network number (1 to 64) and every piece its identifier,
 * stamped like a serial number - the two quantities that go on the air.
 * The interface shows the protocol rather than hiding it.
 *
 * The colours come from the QML theme (the colors map), the position from a
 * BoardSource (the application controller).  A tap selects a piece of the
 * side to move and shows its legal squares; a second tap on one of them
 * emits moveChosen().  Tapping the king and then its own rook castles, as
 * on most chess servers.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "Board.h"

#include <QColor>
#include <QHash>
#include <QQuickPaintedItem>
#include <QVariantMap>

// What the view needs from whoever owns the game.
class BoardSource
{
public:
    virtual ~BoardSource() = default;
    virtual const chess::Board &currentBoard() const = 0;
    virtual bool boardInteractive() const = 0;   // the operator may move now
    virtual int lastMoveFrom() const = 0;        // -1 when there is none
    virtual int lastMoveTo() const = 0;
};

class BoardView : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVariantMap colors READ colors WRITE setColors NOTIFY colorsChanged)
    Q_PROPERTY(bool flipped READ flipped WRITE setFlipped NOTIFY flippedChanged)
    Q_PROPERTY(bool showUids READ showUids WRITE setShowUids NOTIFY showUidsChanged)
    Q_PROPERTY(bool showNumbers READ showNumbers WRITE setShowNumbers NOTIFY showNumbersChanged)
    Q_PROPERTY(int selectedSquare READ selectedSquare NOTIFY selectionChanged)
    Q_PROPERTY(qreal boardSide READ boardSide NOTIFY geometryChanged)

public:
    explicit BoardView(QQuickItem *parent = nullptr);

    // The family name of the piece font compiled into the program.
    static QString pieceFontFamily();
    static void loadPieceFont();

    QObject *source() const { return m_sourceObject; }
    void setSource(QObject *source);
    QVariantMap colors() const { return m_colorMap; }
    void setColors(const QVariantMap &colors);
    bool flipped() const { return m_flipped; }
    void setFlipped(bool flipped);
    bool showUids() const { return m_showUids; }
    void setShowUids(bool on);
    bool showNumbers() const { return m_showNumbers; }
    void setShowNumbers(bool on);
    int selectedSquare() const { return m_selected; }
    qreal boardSide() const { return metrics().cell * 8; }

    // Interaction, from a MouseArea in QML (one code path for mouse and touch).
    Q_INVOKABLE void pressAt(qreal x, qreal y);
    Q_INVOKABLE void hoverAt(qreal x, qreal y);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE int squareAt(qreal x, qreal y) const;
    // "29 (e4)  WP5" for the status line; empty off the board.
    Q_INVOKABLE QString describeSquare(int square) const;

    void paint(QPainter *painter) override;

signals:
    void sourceChanged();
    void colorsChanged();
    void flippedChanged();
    void showUidsChanged();
    void showNumbersChanged();
    void selectionChanged();
    void geometryChanged();
    // A legal destination was chosen.  promotion: the operator must pick
    // the piece (QML asks, then calls the controller).
    void moveChosen(const QString &uid, int square, bool promotion, bool white);
    void hovered(int square);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct Metrics { qreal ox; qreal oy; qreal cell; qreal margin; };
    Metrics metrics() const;
    QRectF squareRect(int sq) const;
    QColor color(const char *key, const QColor &fallback = QColor(Qt::magenta)) const;
    const chess::Board *board() const;
    void drawPiece(QPainter *p, const QRectF &r, const chess::Piece &piece, qreal cell) const;

    QObject *m_sourceObject = nullptr;
    BoardSource *m_source = nullptr;
    QVariantMap m_colorMap;
    QHash<QString, QColor> m_colors;
    bool m_flipped = false;
    bool m_showUids = true;
    bool m_showNumbers = true;
    int m_selected = -1;
    int m_hover = -1;
    QHash<int, QList<chess::Move>> m_targets;
};
