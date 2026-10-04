/*
 * Board.cpp - chess rules with stable piece identifiers.
 *
 * Port of chess_rules.py.  Where the Python code iterates over its piece
 * dictionary, this code iterates over m_pieces, which is filled in the same
 * insertion order: the legal move lists come out in the same order, and so
 * does everything derived from them.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "Board.h"

#include <QStringList>
#include <cstdlib>
#include <algorithm>

namespace chess {

namespace {

const char Files[] = "abcdefgh";

const int KnightDeltas[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
const int KingDeltas[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
const int BishopDirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
const int RookDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
const int QueenDirs[8][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};

char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }
char upper(char c) { return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c; }

} // namespace

QString squareName(int sq)
{
    if (sq < 0 || sq > 63) return QString();
    return QString(QChar(Files[fileOf(sq)])) + QString::number(rankOf(sq) + 1);
}

int squareFromName(const QString &nameIn)
{
    const QString name = nameIn.trimmed().toLower();
    if (name.size() != 2) return -1;
    const int file = name.at(0).unicode() - 'a';
    const int rank = name.at(1).unicode() - '1';
    if (!onBoard(file, rank)) return -1;
    return makeSquare(file, rank);
}

int squareFromNumber(int number)
{
    return (number >= 1 && number <= 64) ? number - 1 : -1;
}

// =====================================================================
//  Set-up
// =====================================================================

Board::Board()
{
    setInitialPosition();
}

void Board::clear()
{
    m_pieces.clear();
    m_squares.fill(-1);
    m_uidIndex.clear();
    m_turn = White;
    m_castling = 0;
    m_ep = -1;
    m_halfmove = 0;
    m_fullmove = 1;
    m_stack.clear();
    m_moves.clear();
    m_repetitions.clear();
}

void Board::place(const QString &uid, char color, char kind, int sq)
{
    Piece p;
    p.uid = uid;
    p.color = color;
    p.kind = kind;
    p.square = sq;
    p.bornKind = kind;
    m_uidIndex.insert(uid, int(m_pieces.size()));
    m_squares[sq] = int(m_pieces.size());
    m_pieces.push_back(p);
}

void Board::setInitialPosition()
{
    clear();
    const struct { char kind; int index; } back[8] = {
        {Rook, 1}, {Knight, 1}, {Bishop, 1}, {Queen, 1}, {King, 1}, {Bishop, 2}, {Knight, 2}, {Rook, 2}};
    // Same insertion order as the Python dictionary: White then Black for
    // each file of the back rank, then the pawns file by file.
    for (int f = 0; f < 8; f++) {
        const QString suffix = QString(QChar(back[f].kind)) + QString::number(back[f].index);
        place(QStringLiteral("W") + suffix, White, back[f].kind, makeSquare(f, 0));
        place(QStringLiteral("B") + suffix, Black, back[f].kind, makeSquare(f, 7));
    }
    for (int f = 0; f < 8; f++) {
        place(QStringLiteral("WP%1").arg(f + 1), White, Pawn, makeSquare(f, 1));
        place(QStringLiteral("BP%1").arg(f + 1), Black, Pawn, makeSquare(f, 6));
    }
    m_turn = White;
    m_castling = CastleK | CastleQ | Castlek | Castleq;
    m_repetitions.insert(positionKey(), 1);
}

bool Board::setFen(const QString &fen)
{
    const QStringList parts = fen.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 4) return false;
    clear();
    QHash<QString, int> counters;
    int rank = 7;
    int file = 0;
    for (const QChar qc : parts.at(0)) {
        const char ch = char(qc.unicode());
        if (ch == '/') {
            rank--;
            file = 0;
        } else if (ch >= '1' && ch <= '8') {
            file += ch - '0';
        } else {
            const char color = (ch >= 'A' && ch <= 'Z') ? White : Black;
            const char kind = upper(ch);
            if (!QByteArray("PNBRQK").contains(kind) || !onBoard(file, rank)) return false;
            const QString key = QString(QChar(color)) + QChar(kind);
            const int n = counters.value(key, 0) + 1;
            counters.insert(key, n);
            place(key + QString::number(n), color, kind, makeSquare(file, rank));
            file++;
        }
    }
    m_turn = parts.at(1) == QLatin1String("w") ? White : Black;
    m_castling = 0;
    if (parts.at(2) != QLatin1String("-")) {
        for (const QChar c : parts.at(2)) {
            if (c == QLatin1Char('K')) m_castling |= CastleK;
            else if (c == QLatin1Char('Q')) m_castling |= CastleQ;
            else if (c == QLatin1Char('k')) m_castling |= Castlek;
            else if (c == QLatin1Char('q')) m_castling |= Castleq;
        }
    }
    m_ep = parts.at(3) == QLatin1String("-") ? -1 : squareFromName(parts.at(3));
    m_halfmove = parts.size() > 4 ? parts.at(4).toInt() : 0;
    m_fullmove = parts.size() > 5 ? parts.at(5).toInt() : 1;
    m_repetitions.insert(positionKey(), 1);
    return true;
}

QString Board::fen() const
{
    QStringList rows;
    for (int r = 7; r >= 0; r--) {
        QString row;
        int empty = 0;
        for (int f = 0; f < 8; f++) {
            const Piece *p = pieceAt(makeSquare(f, r));
            if (!p) {
                empty++;
                continue;
            }
            if (empty) {
                row += QString::number(empty);
                empty = 0;
            }
            row += QChar(p->color == White ? p->kind : lower(p->kind));
        }
        if (empty) row += QString::number(empty);
        rows << row;
    }
    QString castle;
    if (m_castling & CastleK) castle += QLatin1Char('K');
    if (m_castling & CastleQ) castle += QLatin1Char('Q');
    if (m_castling & Castlek) castle += QLatin1Char('k');
    if (m_castling & Castleq) castle += QLatin1Char('q');
    if (castle.isEmpty()) castle = QStringLiteral("-");
    return QStringLiteral("%1 %2 %3 %4 %5 %6")
        .arg(rows.join(QLatin1Char('/')), m_turn == White ? QStringLiteral("w") : QStringLiteral("b"), castle,
             m_ep >= 0 ? squareName(m_ep) : QStringLiteral("-"))
        .arg(m_halfmove)
        .arg(m_fullmove);
}

// =====================================================================
//  Access
// =====================================================================

const Piece *Board::pieceAt(int sq) const
{
    if (sq < 0 || sq > 63) return nullptr;
    const int i = m_squares[sq];
    return i >= 0 ? &m_pieces[i] : nullptr;
}

int Board::pieceIndex(const QString &uid) const
{
    return m_uidIndex.value(uid, -1);
}

int Board::kingSquare(char color) const
{
    for (const Piece &p : m_pieces) {
        if (p.color == color && p.kind == King && p.alive()) return p.square;
    }
    return -1;
}

QList<const Piece *> Board::captured(char color) const
{
    QList<const Piece *> out;
    for (const Piece &p : m_pieces) {
        if (p.color == color && !p.alive()) out << &p;
    }
    return out;
}

// =====================================================================
//  Attacks
// =====================================================================

bool Board::isAttacked(int sq, char byColor) const
{
    const int f0 = fileOf(sq), r0 = rankOf(sq);

    // Pawns: the direction they come from.
    const int d = byColor == White ? -1 : 1;
    for (int df : {-1, 1}) {
        const int f = f0 + df, r = r0 + d;
        if (onBoard(f, r)) {
            const Piece *p = pieceAt(makeSquare(f, r));
            if (p && p->color == byColor && p->kind == Pawn) return true;
        }
    }
    for (const auto &delta : KnightDeltas) {
        const int f = f0 + delta[0], r = r0 + delta[1];
        if (onBoard(f, r)) {
            const Piece *p = pieceAt(makeSquare(f, r));
            if (p && p->color == byColor && p->kind == Knight) return true;
        }
    }
    for (const auto &delta : KingDeltas) {
        const int f = f0 + delta[0], r = r0 + delta[1];
        if (onBoard(f, r)) {
            const Piece *p = pieceAt(makeSquare(f, r));
            if (p && p->color == byColor && p->kind == King) return true;
        }
    }
    // Sliders.
    for (const auto &dir : BishopDirs) {
        int f = f0 + dir[0], r = r0 + dir[1];
        while (onBoard(f, r)) {
            const Piece *p = pieceAt(makeSquare(f, r));
            if (p) {
                if (p->color == byColor && (p->kind == Bishop || p->kind == Queen)) return true;
                break;
            }
            f += dir[0];
            r += dir[1];
        }
    }
    for (const auto &dir : RookDirs) {
        int f = f0 + dir[0], r = r0 + dir[1];
        while (onBoard(f, r)) {
            const Piece *p = pieceAt(makeSquare(f, r));
            if (p) {
                if (p->color == byColor && (p->kind == Rook || p->kind == Queen)) return true;
                break;
            }
            f += dir[0];
            r += dir[1];
        }
    }
    return false;
}

bool Board::inCheck(char color) const
{
    const int ks = kingSquare(color);
    return ks >= 0 && isAttacked(ks, opponent(color));
}

// =====================================================================
//  Generation
// =====================================================================

void Board::pseudoMoves(char color, QList<Move> &out) const
{
    for (int pi = 0; pi < int(m_pieces.size()); pi++) {
        const Piece &p = m_pieces[pi];
        if (!p.alive() || p.color != color) continue;
        const int f0 = fileOf(p.square), r0 = rankOf(p.square);

        if (p.kind == Pawn) {
            const int step = color == White ? 1 : -1;
            const int startRank = color == White ? 1 : 6;
            const int lastRank = color == White ? 7 : 0;

            const int r = r0 + step;
            if (onBoard(f0, r) && m_squares[makeSquare(f0, r)] < 0) {
                addPawn(out, pi, makeSquare(f0, r), lastRank);
                const int r2 = r0 + 2 * step;
                if (r0 == startRank && m_squares[makeSquare(f0, r2)] < 0) {
                    Move m;
                    m.piece = pi;
                    m.from = p.square;
                    m.to = makeSquare(f0, r2);
                    out << m;
                }
            }
            for (int df : {-1, 1}) {
                const int f = f0 + df, rr = r0 + step;
                if (!onBoard(f, rr)) continue;
                const int tsq = makeSquare(f, rr);
                const int ti = m_squares[tsq];
                if (ti >= 0 && m_pieces[ti].color != color) {
                    addPawn(out, pi, tsq, lastRank, ti, tsq);
                } else if (m_ep >= 0 && tsq == m_ep) {
                    const int capSq = makeSquare(f, r0);
                    const int ci = m_squares[capSq];
                    if (ci >= 0 && m_pieces[ci].color != color && m_pieces[ci].kind == Pawn) {
                        Move m;
                        m.piece = pi;
                        m.from = p.square;
                        m.to = tsq;
                        m.captured = ci;
                        m.capturedSquare = capSq;
                        m.enPassant = true;
                        out << m;
                    }
                }
            }
        } else if (p.kind == Knight) {
            addSteps(out, pi, KnightDeltas, 8);
        } else if (p.kind == King) {
            addSteps(out, pi, KingDeltas, 8);
            addCastles(out, pi);
        } else if (p.kind == Bishop) {
            addSlides(out, pi, BishopDirs, 4);
        } else if (p.kind == Rook) {
            addSlides(out, pi, RookDirs, 4);
        } else {
            // Python: BISHOP_DIRS + ROOK_DIRS, in that order.
            addSlides(out, pi, QueenDirs, 8);
        }
    }
}

void Board::addPawn(QList<Move> &out, int pi, int to, int lastRank, int capPiece, int capSquare) const
{
    Move m;
    m.piece = pi;
    m.from = m_pieces[pi].square;
    m.to = to;
    m.captured = capPiece;
    m.capturedSquare = capSquare;
    if (rankOf(to) == lastRank) {
        for (char promo : {Queen, Rook, Bishop, Knight}) {
            m.promo = promo;
            out << m;
        }
    } else {
        out << m;
    }
}

void Board::addSteps(QList<Move> &out, int pi, const int (*deltas)[2], int n) const
{
    const Piece &p = m_pieces[pi];
    const int f0 = fileOf(p.square), r0 = rankOf(p.square);
    for (int i = 0; i < n; i++) {
        const int f = f0 + deltas[i][0], r = r0 + deltas[i][1];
        if (!onBoard(f, r)) continue;
        const int tsq = makeSquare(f, r);
        const int ti = m_squares[tsq];
        if (ti < 0 || m_pieces[ti].color != p.color) {
            Move m;
            m.piece = pi;
            m.from = p.square;
            m.to = tsq;
            if (ti >= 0) {
                m.captured = ti;
                m.capturedSquare = tsq;
            }
            out << m;
        }
    }
}

void Board::addSlides(QList<Move> &out, int pi, const int (*dirs)[2], int n) const
{
    const Piece &p = m_pieces[pi];
    const int f0 = fileOf(p.square), r0 = rankOf(p.square);
    for (int i = 0; i < n; i++) {
        int f = f0 + dirs[i][0], r = r0 + dirs[i][1];
        while (onBoard(f, r)) {
            const int tsq = makeSquare(f, r);
            const int ti = m_squares[tsq];
            Move m;
            m.piece = pi;
            m.from = p.square;
            m.to = tsq;
            if (ti < 0) {
                out << m;
            } else {
                if (m_pieces[ti].color != p.color) {
                    m.captured = ti;
                    m.capturedSquare = tsq;
                    out << m;
                }
                break;
            }
            f += dirs[i][0];
            r += dirs[i][1];
        }
    }
}

void Board::addCastles(QList<Move> &out, int pi) const
{
    const Piece &king = m_pieces[pi];
    const char color = king.color;
    const char opp = opponent(color);
    const int rank = color == White ? 0 : 7;
    const int home = makeSquare(4, rank);
    if (king.square != home) return;
    if (isAttacked(home, opp)) return;
    const int shortRight = color == White ? CastleK : Castlek;
    const int longRight = color == White ? CastleQ : Castleq;

    if (m_castling & shortRight) {
        const int fSq = makeSquare(5, rank), gSq = makeSquare(6, rank), hSq = makeSquare(7, rank);
        const int ri = m_squares[hSq];
        if (ri >= 0 && m_pieces[ri].kind == Rook && m_pieces[ri].color == color && m_squares[fSq] < 0
            && m_squares[gSq] < 0 && !isAttacked(fSq, opp) && !isAttacked(gSq, opp)) {
            Move m;
            m.piece = pi;
            m.from = home;
            m.to = gSq;
            m.castle = 'K';
            m.rook = ri;
            m.rookFrom = hSq;
            m.rookTo = fSq;
            out << m;
        }
    }
    if (m_castling & longRight) {
        const int dSq = makeSquare(3, rank), cSq = makeSquare(2, rank), bSq = makeSquare(1, rank),
                  aSq = makeSquare(0, rank);
        const int ri = m_squares[aSq];
        if (ri >= 0 && m_pieces[ri].kind == Rook && m_pieces[ri].color == color && m_squares[dSq] < 0
            && m_squares[cSq] < 0 && m_squares[bSq] < 0 && !isAttacked(dSq, opp) && !isAttacked(cSq, opp)) {
            Move m;
            m.piece = pi;
            m.from = home;
            m.to = cSq;
            m.castle = 'Q';
            m.rook = ri;
            m.rookFrom = aSq;
            m.rookTo = dSq;
            out << m;
        }
    }
}

QList<Move> Board::legalMoves(char color) const
{
    QList<Move> pseudo;
    pseudoMoves(color, pseudo);
    QList<Move> legal;
    for (const Move &m : pseudo) {
        apply(m);
        if (!inCheck(color)) legal << m;
        revert();
    }
    return legal;
}

QList<Move> Board::legalMovesFrom(int sq) const
{
    QList<Move> out;
    for (const Move &m : legalMoves()) {
        if (m.from == sq) out << m;
    }
    return out;
}

bool Board::findMove(const QString &uid, int to, char promo, Move *out) const
{
    const int pi = pieceIndex(uid);
    if (pi < 0) return false;
    const QList<Move> all = legalMoves();
    const Move *first = nullptr;
    for (const Move &m : all) {
        if (m.piece != pi || m.to != to) continue;
        if (promo) {
            if (m.promo == promo) {
                if (out) *out = m;
                return true;
            }
            continue;
        }
        if (!first) first = &m;
    }
    if (promo || !first) return false;
    if (out) *out = *first;
    return true;
}

// =====================================================================
//  Making and unmaking moves
// =====================================================================

void Board::apply(const Move &m) const
{
    Piece &p = m_pieces[m.piece];
    Undo undo;
    undo.move = m;
    undo.castling = m_castling;
    undo.ep = m_ep;
    undo.halfmove = m_halfmove;
    undo.fullmove = m_fullmove;
    undo.prevKind = p.kind;
    m_stack.push_back(undo);

    if (m.captured >= 0) {
        Piece &cap = m_pieces[m.captured];
        m_squares[cap.square] = -1;
        cap.square = -1;
    }
    m_squares[m.from] = -1;
    m_squares[m.to] = m.piece;
    p.square = m.to;
    if (m.promo) p.kind = m.promo;

    if (m.castle) {
        Piece &rook = m_pieces[m.rook];
        m_squares[m.rookFrom] = -1;
        m_squares[m.rookTo] = m.rook;
        rook.square = m.rookTo;
    }

    // Castling rights.
    if (p.kind == King || undo.prevKind == King) {
        m_castling &= p.color == White ? ~(CastleK | CastleQ) : ~(Castlek | Castleq);
    }
    const struct { int sq; int right; } corners[4] = {{0, CastleQ}, {7, CastleK}, {56, Castleq}, {63, Castlek}};
    for (const auto &c : corners) {
        if (m.from == c.sq || m.to == c.sq) m_castling &= ~c.right;
    }

    // En passant square.
    m_ep = -1;
    if (undo.prevKind == Pawn && std::abs(rankOf(m.to) - rankOf(m.from)) == 2) {
        m_ep = makeSquare(fileOf(m.from), (rankOf(m.from) + rankOf(m.to)) / 2);
    }

    if (undo.prevKind == Pawn || m.captured >= 0) m_halfmove = 0;
    else m_halfmove++;
    if (m_turn == Black) m_fullmove++;
    m_turn = opponent(m_turn);
}

void Board::revert() const
{
    const Undo undo = m_stack.back();
    m_stack.pop_back();
    const Move &m = undo.move;
    Piece &p = m_pieces[m.piece];

    if (m.castle) {
        Piece &rook = m_pieces[m.rook];
        m_squares[m.rookTo] = -1;
        m_squares[m.rookFrom] = m.rook;
        rook.square = m.rookFrom;
    }
    p.kind = undo.prevKind;
    m_squares[m.to] = -1;
    m_squares[m.from] = m.piece;
    p.square = m.from;

    if (m.captured >= 0) {
        Piece &cap = m_pieces[m.captured];
        cap.square = m.capturedSquare;
        m_squares[m.capturedSquare] = m.captured;
    }
    m_castling = undo.castling;
    m_ep = undo.ep;
    m_halfmove = undo.halfmove;
    m_fullmove = undo.fullmove;
    m_turn = opponent(m_turn);
}

void Board::push(const Move &m)
{
    apply(m);
    m_moves << m;
    const QString key = positionKey();
    m_repetitions.insert(key, m_repetitions.value(key, 0) + 1);
}

bool Board::pop(Move *out)
{
    if (m_moves.isEmpty()) return false;
    const QString key = positionKey();
    m_repetitions.insert(key, std::max(0, m_repetitions.value(key, 1) - 1));
    const Move m = m_moves.takeLast();
    revert();
    if (out) *out = m;
    return true;
}

// =====================================================================
//  State
// =====================================================================

QString Board::positionKey() const
{
    QString key;
    key.reserve(64 + 12);
    for (int sq = 0; sq < 64; sq++) {
        const int i = m_squares[sq];
        if (i < 0) {
            key += QLatin1Char('.');
        } else {
            const Piece &p = m_pieces[i];
            key += QChar(p.color == White ? p.kind : lower(p.kind));
        }
    }
    // Python: "|".join([..., turn, "".join(sorted(castling)), ep or "-"]).
    // sorted() puts upper case first: K Q k q.
    key += QLatin1Char('|');
    key += QChar(m_turn);
    key += QLatin1Char('|');
    if (m_castling & CastleK) key += QLatin1Char('K');
    if (m_castling & CastleQ) key += QLatin1Char('Q');
    if (m_castling & Castlek) key += QLatin1Char('k');
    if (m_castling & Castleq) key += QLatin1Char('q');
    key += QLatin1Char('|');
    key += m_ep >= 0 ? QString::number(m_ep) : QStringLiteral("-");
    return key;
}

bool Board::insufficientMaterial() const
{
    QList<const Piece *> alive;
    for (const Piece &p : m_pieces) {
        if (p.alive()) alive << &p;
    }
    int kings = 0, knights = 0, bishops = 0;
    for (const Piece *p : alive) {
        if (p->kind == King) kings++;
        else if (p->kind == Knight) knights++;
        else if (p->kind == Bishop) bishops++;
    }
    if (alive.size() == 2 && kings == 2) return true;
    if (alive.size() == 3 && (knights > 0 || bishops > 0)) return true;
    if (alive.size() == 4) {
        QList<const Piece *> b;
        for (const Piece *p : alive) {
            if (p->kind == Bishop) b << p;
        }
        if (b.size() == 2 && b.at(0)->color != b.at(1)->color) {
            const int c0 = (fileOf(b.at(0)->square) + rankOf(b.at(0)->square)) & 1;
            const int c1 = (fileOf(b.at(1)->square) + rankOf(b.at(1)->square)) & 1;
            if (c0 == c1) return true;
        }
    }
    return false;
}

Board::Status Board::status() const
{
    if (legalMoves().isEmpty()) {
        return inCheck() ? Status::Checkmate : Status::Stalemate;
    }
    if (m_halfmove >= 100) return Status::FiftyMoves;
    if (m_repetitions.value(positionKey(), 0) >= 3) return Status::Repetition;
    if (insufficientMaterial()) return Status::InsufficientMaterial;
    return Status::Playing;
}

QString Board::san(const Move &m) const
{
    QString base;
    if (m.castle == 'K') {
        base = QStringLiteral("O-O");
    } else if (m.castle == 'Q') {
        base = QStringLiteral("O-O-O");
    } else {
        const Piece &p = m_pieces[m.piece];
        if (p.kind == Pawn) {
            if (m.captured >= 0) base = QString(QChar(Files[fileOf(m.from)])) + QLatin1Char('x');
            base += squareName(m.to);
        } else {
            QString disamb;
            for (const Move &other : legalMoves(p.color)) {
                if (other.piece != m.piece && other.to == m.to && m_pieces[other.piece].kind == p.kind) {
                    if (fileOf(other.from) != fileOf(m.from)) disamb = QString(QChar(Files[fileOf(m.from)]));
                    else disamb = QString::number(rankOf(m.from) + 1);
                    break;
                }
            }
            base = QString(QChar(p.kind)) + disamb + (m.captured >= 0 ? QStringLiteral("x") : QString())
                   + squareName(m.to);
        }
        if (m.promo) base += QLatin1Char('=') + QString(QChar(m.promo));
    }
    apply(m);
    if (inCheck()) base += legalMoves().isEmpty() ? QLatin1Char('#') : QLatin1Char('+');
    revert();
    return base;
}

quint64 perft(Board &board, int depth)
{
    if (depth == 0) return 1;
    quint64 total = 0;
    for (const Move &m : board.legalMoves()) {
        board.apply(m);
        total += perft(board, depth - 1);
        board.revert();
    }
    return total;
}

} // namespace chess
