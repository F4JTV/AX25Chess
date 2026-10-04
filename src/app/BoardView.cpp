/*
 * BoardView.cpp - the chessboard, painted.
 *
 * The drawing follows board_widget.py of the Python version.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "BoardView.h"

#include <QFontDatabase>
#include <cmath>
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

using namespace chess;

namespace {
// Solid glyphs for both colours: an even rendering, the colour comes from
// the fill.
const char32_t Solid[6] = {0x265A, 0x265B, 0x265C, 0x265D, 0x265E, 0x265F};   // K Q R B N P
QString glyph(char kind)
{
    const char *kinds = "KQRBNP";
    for (int i = 0; i < 6; i++) {
        if (kinds[i] == kind) return QString::fromUcs4(&Solid[i], 1);
    }
    return QString();
}
QString g_pieceFamily;
} // namespace

void BoardView::loadPieceFont()
{
    if (!g_pieceFamily.isEmpty()) return;
    const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/AX25ChessPieces.ttf"));
    const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
    g_pieceFamily = families.isEmpty() ? QStringLiteral("DejaVu Sans") : families.first();
}

QString BoardView::pieceFontFamily()
{
    loadPieceFont();
    return g_pieceFamily;
}

BoardView::BoardView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    loadPieceFont();
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::NoButton);   // QML's MouseArea does the input
}

void BoardView::setSource(QObject *source)
{
    if (source == m_sourceObject) return;
    if (m_sourceObject) disconnect(m_sourceObject, nullptr, this, nullptr);
    m_sourceObject = source;
    m_source = dynamic_cast<BoardSource *>(source);
    if (m_sourceObject) {
        // Any signal named boardChanged() repaints and drops a stale selection.
        connect(m_sourceObject, SIGNAL(boardChanged()), this, SLOT(clearSelection()));
    }
    clearSelection();
    emit sourceChanged();
}

void BoardView::setColors(const QVariantMap &colors)
{
    m_colorMap = colors;
    m_colors.clear();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const QColor c(it.value().toString());
        if (c.isValid()) m_colors.insert(it.key(), c);
    }
    emit colorsChanged();
    update();
}

void BoardView::setFlipped(bool flipped)
{
    if (flipped == m_flipped) return;
    m_flipped = flipped;
    emit flippedChanged();
    update();
}

void BoardView::setShowUids(bool on)
{
    if (on == m_showUids) return;
    m_showUids = on;
    emit showUidsChanged();
    update();
}

void BoardView::setShowNumbers(bool on)
{
    if (on == m_showNumbers) return;
    m_showNumbers = on;
    emit showNumbersChanged();
    update();
}

QColor BoardView::color(const char *key, const QColor &fallback) const
{
    return m_colors.value(QString::fromLatin1(key), fallback);
}

const Board *BoardView::board() const
{
    return m_source ? &m_source->currentBoard() : nullptr;
}

void BoardView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    emit geometryChanged();
    update();
}

// ---- geometry ------------------------------------------------------------

BoardView::Metrics BoardView::metrics() const
{
    const qreal w = width(), h = height();
    const qreal margin = std::max(16.0, std::min(w, h) * 0.045);
    qreal side = std::min(w, h) - 2 * margin;
    if (side < 8) side = 8;
    const qreal cell = std::floor(side / 8);
    return {std::floor((w - cell * 8) / 2), std::floor((h - cell * 8) / 2), cell, margin};
}

QRectF BoardView::squareRect(int sq) const
{
    const Metrics m = metrics();
    const int f = fileOf(sq), r = rankOf(sq);
    const int col = m_flipped ? 7 - f : f;
    const int row = m_flipped ? r : 7 - r;
    return QRectF(m.ox + col * m.cell, m.oy + row * m.cell, m.cell, m.cell);
}

int BoardView::squareAt(qreal x, qreal y) const
{
    const Metrics m = metrics();
    if (x < m.ox || y < m.oy) return -1;
    const int col = int((x - m.ox) / m.cell);
    const int row = int((y - m.oy) / m.cell);
    if (col < 0 || col > 7 || row < 0 || row > 7) return -1;
    const int f = m_flipped ? 7 - col : col;
    const int r = m_flipped ? row : 7 - row;
    return makeSquare(f, r);
}

QString BoardView::describeSquare(int square) const
{
    if (square < 0 || square > 63) return QString();
    QString text = QStringLiteral("%1 (%2)").arg(squareNumber(square)).arg(squareName(square));
    if (const Board *b = board()) {
        if (const Piece *p = b->pieceAt(square)) text += QStringLiteral("  ") + p->uid;
    }
    return text;
}

// ---- interaction -----------------------------------------------------------

void BoardView::clearSelection()
{
    const bool had = m_selected >= 0 || !m_targets.isEmpty();
    m_selected = -1;
    m_targets.clear();
    if (had) emit selectionChanged();
    update();
}

void BoardView::hoverAt(qreal x, qreal y)
{
    const int sq = (x < 0 || y < 0) ? -1 : squareAt(x, y);
    if (sq == m_hover) return;
    m_hover = sq;
    emit hovered(sq);
    update();
}

void BoardView::pressAt(qreal x, qreal y)
{
    const Board *b = board();
    if (!b || !m_source || !m_source->boardInteractive()) {
        clearSelection();
        return;
    }
    const int sq = squareAt(x, y);
    if (sq < 0) {
        clearSelection();
        return;
    }
    // King selected, then its own rook: castling, if legal.
    if (m_selected >= 0 && !m_targets.contains(sq)) {
        const Piece *sel = b->pieceAt(m_selected);
        const Piece *there = b->pieceAt(sq);
        if (sel && there && sel->kind == King && there->kind == Rook && there->color == sel->color) {
            for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
                for (const Move &m : it.value()) {
                    if (m.castle && m.rookFrom == sq) {
                        const QString uid = sel->uid;
                        const int to = m.to;
                        clearSelection();
                        emit moveChosen(uid, to, false, sel->color == White);
                        return;
                    }
                }
            }
        }
    }
    if (m_selected >= 0 && m_targets.contains(sq)) {
        const QList<Move> moves = m_targets.value(sq);
        bool promotion = false;
        for (const Move &m : moves) promotion = promotion || m.promo;
        const Piece *p = b->pieceAt(m_selected);
        const QString uid = p ? p->uid : QString();
        const bool white = p && p->color == White;
        clearSelection();
        emit moveChosen(uid, sq, promotion, white);
        return;
    }
    const Piece *piece = b->pieceAt(sq);
    if (piece && piece->color == b->turn()) {
        m_selected = sq;
        m_targets.clear();
        for (const Move &m : b->legalMovesFrom(sq)) m_targets[m.to].append(m);
        emit selectionChanged();
        update();
    } else {
        clearSelection();
    }
}

// ---- painting -------------------------------------------------------------

static QColor withAlpha(QColor c, int alpha)
{
    c.setAlpha(alpha);
    return c;
}

void BoardView::paint(QPainter *p)
{
    p->setRenderHint(QPainter::Antialiasing);
    p->setRenderHint(QPainter::TextAntialiasing);
    const Metrics mt = metrics();
    const qreal cell = mt.cell;
    const qreal side = cell * 8;
    const QColor accent = color("accent", QColor(0xE8, 0xA3, 0x3D));
    const bool light = m_colorMap.value(QStringLiteral("light")).toBool();

    // The frame of the panel.
    p->setPen(QPen(color("line"), 1));
    p->setBrush(Qt::NoBrush);
    p->drawRoundedRect(QRectF(mt.ox - 7, mt.oy - 7, side + 14, side + 14), 4, 4);

    const Board *b = board();
    if (!b) return;

    int checkSq = -1;
    if (b->inCheck()) checkSq = b->kingSquare(b->turn());
    const int lastFrom = m_source ? m_source->lastMoveFrom() : -1;
    const int lastTo = m_source ? m_source->lastMoveTo() : -1;
    const int numberAlpha = m_colorMap.value(QStringLiteral("numberAlpha"), 100).toInt();
    QFont numberFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    numberFont.setPixelSize(std::max(7, int(cell * 0.16)));

    for (int sq = 0; sq < 64; sq++) {
        const QRectF r = squareRect(sq);
        const bool lightSq = (fileOf(sq) + rankOf(sq)) % 2 == 1;
        p->fillRect(r, lightSq ? color("lightSquare") : color("darkSquare"));
        if (sq == lastFrom || sq == lastTo) {
            p->fillRect(r, withAlpha(accent, m_colorMap.value(QStringLiteral("lastMoveAlpha"), 74).toInt()));
        }
        if (sq == checkSq) {
            QRadialGradient g(r.center(), r.width() * 0.62);
            g.setColorAt(0.0, withAlpha(color("alert"), 205));
            g.setColorAt(1.0, withAlpha(color("alert"), 0));
            p->fillRect(r, g);
        }
        if (sq == m_selected) {
            p->setPen(QPen(accent, std::max(2.0, cell * 0.05)));
            p->setBrush(Qt::NoBrush);
            p->drawRect(r.adjusted(1.5, 1.5, -1.5, -1.5));
        }
        if (m_showNumbers) {
            const QColor ink = lightSq ? color("numberOnLight", QColor(0, 0, 0)) : color("numberOnDark", QColor(255, 255, 255));
            p->setPen(withAlpha(ink, numberAlpha));
            p->setFont(numberFont);
            p->drawText(r.adjusted(0, 2, -4, 0), Qt::AlignTop | Qt::AlignRight, QString::number(squareNumber(sq)));
        }
    }

    for (int sq = 0; sq < 64; sq++) {
        if (const Piece *piece = b->pieceAt(sq)) drawPiece(p, squareRect(sq), *piece, cell);
    }

    // Legal destinations, over the pieces.
    for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
        const QRectF r = squareRect(it.key());
        bool capture = false;
        for (const Move &m : it.value()) capture = capture || m.isCapture();
        p->setBrush(Qt::NoBrush);
        if (capture) {
            p->setPen(QPen(withAlpha(accent, 235), std::max(2.5, cell * 0.06)));
            p->drawEllipse(r.center(), cell * 0.42, cell * 0.42);
        } else {
            p->setPen(Qt::NoPen);
            p->setBrush(withAlpha(accent, light ? 220 : 190));
            p->drawEllipse(r.center(), cell * 0.13, cell * 0.13);
        }
    }

    if (m_hover >= 0 && m_source && m_source->boardInteractive()) {
        p->setPen(QPen(withAlpha(accent, 150), 1.5));
        p->setBrush(Qt::NoBrush);
        p->drawRect(squareRect(m_hover).adjusted(1, 1, -1, -1));
    }

    // Coordinates.
    QFont coordFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    coordFont.setPixelSize(std::max(9, int(std::min(cell * 0.24, mt.margin * 0.7))));
    p->setFont(coordFont);
    p->setPen(color("muted"));
    const char *files = "abcdefgh";
    for (int i = 0; i < 8; i++) {
        const int f = m_flipped ? 7 - i : i;
        const int r = m_flipped ? i : 7 - i;
        p->drawText(QRectF(mt.ox + i * cell, mt.oy + side + 6, cell, mt.margin - 6), Qt::AlignHCenter | Qt::AlignTop,
                    QString(QChar(files[f])));
        p->drawText(QRectF(mt.ox - mt.margin, mt.oy + i * cell, mt.margin - 8, cell), Qt::AlignVCenter | Qt::AlignRight,
                    QString::number(r + 1));
    }
}

void BoardView::drawPiece(QPainter *p, const QRectF &r, const Piece &piece, qreal cell) const
{
    QFont font(pieceFontFamily());
    font.setPixelSize(std::max(8, int(cell * 0.80)));
    QPainterPath path;
    path.addText(QPointF(0, 0), font, glyph(piece.kind));
    const QRectF br = path.boundingRect();
    path.translate(r.center().x() - br.center().x(), r.center().y() - br.center().y() - cell * 0.03);

    // In full sun it is this outline that sets a white piece apart from a
    // light square; the light theme makes it heavier.
    p->setPen(QPen(color("pieceEdge", QColor(0, 0, 0, 90)), std::max(1.0, cell * 0.035)));
    p->setBrush(piece.color == White ? color("pieceWhite") : color("pieceBlack"));
    p->drawPath(path);

    if (m_showUids) {
        const qreal bw = cell * 0.50, bh = cell * 0.21;
        const QRectF badge(r.left() + cell * 0.04, r.bottom() - bh - cell * 0.04, bw, bh);
        p->setPen(Qt::NoPen);
        p->setBrush(color("badge"));
        p->drawRoundedRect(badge, 3, 3);
        p->setPen(color("badgeInk"));
        QFont f(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        f.setPixelSize(std::max(7, int(cell * 0.15)));
        f.setWeight(QFont::DemiBold);
        p->setFont(f);
        p->drawText(badge, Qt::AlignCenter, piece.uid);
    }
}
