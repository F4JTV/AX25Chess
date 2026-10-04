/*
 * Board.h - chess rules with stable piece identifiers.
 *
 * Every piece carries a unique identifier (UID) that never changes for the
 * whole game, not even on promotion: WP5 promoted to a queen is still WP5.
 * The UID and a square number are what travels on the air.
 *
 * Squares
 * -------
 *   internal index  0..63   index = (rank - 1) * 8 + file,  a1 = 0, h8 = 63
 *   network number  1..64   number = index + 1,             a1 = 1, h8 = 64
 *
 * Identifiers
 * -----------
 *   WR1 WN1 WB1 WQ1 WK1 WB2 WN2 WR2   (rank 1)    WP1 .. WP8 (rank 2)
 *   BR1 BN1 BB1 BQ1 BK1 BB2 BN2 BR2   (rank 8)    BP1 .. BP8 (rank 7)
 *
 * This is a line-for-line port of chess_rules.py from the Python version:
 * move generation order, the position key and therefore the position
 * fingerprint sent in every MOVE frame are identical, so the two versions
 * play against each other.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AX25CHESS_BOARD_H
#define AX25CHESS_BOARD_H

#include <QHash>
#include <QList>
#include <QString>
#include <array>
#include <vector>

namespace chess {

constexpr char White = 'W';
constexpr char Black = 'B';

constexpr char Pawn = 'P';
constexpr char Knight = 'N';
constexpr char Bishop = 'B';
constexpr char Rook = 'R';
constexpr char Queen = 'Q';
constexpr char King = 'K';

inline int rankOf(int sq) { return sq >> 3; }
inline int fileOf(int sq) { return sq & 7; }
inline int makeSquare(int file, int rank) { return rank * 8 + file; }
inline bool onBoard(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
inline int squareNumber(int sq) { return sq + 1; }            // 0..63 -> 1..64
inline char opponent(char color) { return color == White ? Black : White; }

// "e4" for 28.  Empty for an index off the board.
QString squareName(int sq);
// 28 for "e4", -1 when the text is not a square.
int squareFromName(const QString &name);
// 0..63 for a network number 1..64, -1 otherwise.
int squareFromNumber(int number);

struct Piece
{
    QString uid;
    char color = White;
    char kind = Pawn;       // changes on promotion
    int square = -1;        // -1 once captured
    char bornKind = Pawn;   // the original type, for display
    bool alive() const { return square >= 0; }
};

struct Move
{
    int piece = -1;         // index into Board::pieces()
    int from = -1;
    int to = -1;
    char promo = 0;         // 'Q', 'R', 'B', 'N' or 0
    int captured = -1;      // index of the captured piece, -1 if none
    int capturedSquare = -1;
    bool enPassant = false;
    char castle = 0;        // 'K' short, 'Q' long, 0 if not castling
    int rook = -1;
    int rookFrom = -1;
    int rookTo = -1;
    QString san;            // filled by the caller when wanted

    bool isCapture() const { return captured >= 0; }
};

class Board;
quint64 perft(Board &board, int depth);

class Board
{
public:
    enum class Status { Playing, Checkmate, Stalemate, FiftyMoves, Repetition, InsufficientMaterial };

    // The initial position with the 32 standard identifiers.
    Board();

    // A position from FEN, for tests and analysis.  Identifiers are made up
    // from the order the pieces appear in.  Returns false on bad input.
    bool setFen(const QString &fen);
    QString fen() const;

    void setInitialPosition();

    // ---- access -----------------------------------------------------------
    const std::vector<Piece> &pieces() const { return m_pieces; }
    const Piece *pieceAt(int sq) const;
    int pieceIndexAt(int sq) const { return (sq >= 0 && sq < 64) ? m_squares[sq] : -1; }
    int pieceIndex(const QString &uid) const;
    QString uidOf(const Move &m) const { return m.piece >= 0 ? m_pieces[m.piece].uid : QString(); }
    char turn() const { return m_turn; }
    int halfmoveClock() const { return m_halfmove; }
    int fullmoveNumber() const { return m_fullmove; }
    int plyCount() const { return int(m_moves.size()); }
    const QList<Move> &moves() const { return m_moves; }
    int kingSquare(char color) const;
    QList<const Piece *> captured(char color) const;

    // ---- rules ------------------------------------------------------------
    bool isAttacked(int sq, char byColor) const;
    bool inCheck() const { return inCheck(m_turn); }
    bool inCheck(char color) const;

    QList<Move> legalMoves() const { return legalMoves(m_turn); }
    QList<Move> legalMoves(char color) const;
    QList<Move> legalMovesFrom(int sq) const;

    // The legal move a (UID, destination) pair stands for.  Without a
    // promotion piece, the first candidate (the queen for a promotion).
    bool findMove(const QString &uid, int to, char promo, Move *out) const;

    // ---- playing ----------------------------------------------------------
    // Plays a move for good: history and repetition count.
    void push(const Move &m);
    // Takes the last move back; false when there is none.
    bool pop(Move *out = nullptr);

    // Standard algebraic notation, computed BEFORE the move is played.
    QString san(const Move &m) const;

    // ---- state ------------------------------------------------------------
    // Position key for threefold repetition (no counters).  Also the base of
    // the fingerprint in the protocol: keep it byte-identical to Python.
    QString positionKey() const;
    bool insufficientMaterial() const;
    Status status() const;

private:
    friend quint64 perft(Board &board, int depth);

    struct Undo
    {
        Move move;
        int castling = 0;
        int ep = -1;
        int halfmove = 0;
        int fullmove = 1;
        char prevKind = Pawn;
    };

    // Castling rights as bits: K=1 Q=2 k=4 q=8.
    enum : int { CastleK = 1, CastleQ = 2, Castlek = 4, Castleq = 8 };

    void clear();
    void place(const QString &uid, char color, char kind, int sq);
    void pseudoMoves(char color, QList<Move> &out) const;
    void addPawn(QList<Move> &out, int pi, int to, int lastRank, int capPiece = -1, int capSquare = -1) const;
    void addSteps(QList<Move> &out, int pi, const int (*deltas)[2], int n) const;
    void addSlides(QList<Move> &out, int pi, const int (*dirs)[2], int n) const;
    void addCastles(QList<Move> &out, int pi) const;

    // The search mutates and restores; the public API stays const.
    void apply(const Move &m) const;
    void revert() const;

    mutable std::vector<Piece> m_pieces;
    mutable std::array<int, 64> m_squares{};
    mutable char m_turn = White;
    mutable int m_castling = 0;
    mutable int m_ep = -1;
    mutable int m_halfmove = 0;
    mutable int m_fullmove = 1;
    mutable std::vector<Undo> m_stack;
    QList<Move> m_moves;
    QHash<QString, int> m_repetitions;
    QHash<QString, int> m_uidIndex;
};

// Node count, the standard check of a move generator.
quint64 perft(Board &board, int depth);

} // namespace chess

#endif // AX25CHESS_BOARD_H
