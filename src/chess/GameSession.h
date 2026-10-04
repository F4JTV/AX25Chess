/*
 * GameSession.h - the CHS-1 application protocol carried in AX.25 UI frames.
 *
 * The radio channel is slow, half duplex and lossy, so the protocol is
 * stop-and-wait (one reliable frame in flight), with explicit
 * acknowledgements, timed retransmission and a position fingerprint in
 * every move that catches a divergence within one half-move.
 *
 * Frame (plain ASCII, readable in any monitor):
 *
 *     CHS1|<gid>|<src>|<dst>|<seq>|<TYPE>|<payload>|<crc>
 *
 * A move:
 *
 *     MOVE|<UID>;<from>;<to>;<promo>;<ply>;<fingerprint>
 *     e.g. CHS1|3F1A|N0CALL|N0CALL-2|7|MOVE|WP5;13;29;-;0;A34F|725A
 *
 * Port of protocol.py.  Frames, fingerprints and the reaction to every
 * frame type are the same as the Python version's, which this one plays
 * against.  docs/PROTOCOL.md is the specification.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef AX25CHESS_GAMESESSION_H
#define AX25CHESS_GAMESESSION_H

#include "Board.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QRandomGenerator>
#include <QString>
#include <QStringList>
#include <deque>
#include <optional>

namespace chs {

inline const char *Proto = "CHS1";

// Frame types.
inline const QString Hello = QStringLiteral("HELLO");
inline const QString Accept = QStringLiteral("ACPT");
inline const QString MoveType = QStringLiteral("MOVE");
inline const QString Ack = QStringLiteral("ACK");
inline const QString SyncRequest = QStringLiteral("SREQ");
inline const QString Sync = QStringLiteral("SYNC");
inline const QString Resign = QStringLiteral("RSGN");
inline const QString DrawOffer = QStringLiteral("DRWO");
inline const QString DrawAccept = QStringLiteral("DRWA");
inline const QString DrawDecline = QStringLiteral("DRWD");
inline const QString Chat = QStringLiteral("CHAT");
inline const QString Ping = QStringLiteral("PING");
inline const QString Pong = QStringLiteral("PONG");

bool isReliable(const QString &type);

// Default timing, calibrated for 1200 baud VHF.
constexpr double DefaultRetrySeconds = 14.0;
constexpr double RetryJitterSeconds = 3.0;
constexpr int MaxAttempts = 6;
constexpr int SyncChunkMoves = 16;
constexpr int MaxChatLength = 180;

// CRC16-CCITT, polynomial 0x1021, initial value 0xFFFF.
quint16 crc16(const QByteArray &data);

// The text as it goes on the air: ASCII, anything else replaced by '?'
// (Python's encode("ascii", "replace")).
QByteArray toAscii(const QString &text);

// Four hex digits identifying the position, counters included.
QString positionHash(const chess::Board &board);

struct Frame
{
    QString gid;
    QString src;
    QString dst;
    int seq = 0;
    QString type;
    QString payload;

    QString body() const;
    QByteArray encode() const;           // body + "|" + CRC, ASCII
    QString text() const { return QString::fromLatin1(encode()); }
    QStringList fields() const;          // payload split on ';'
};

// Decodes a CHS1 frame; nullopt when malformed or the CRC is wrong.
std::optional<Frame> parseFrame(const QByteArray &data);

// "WP5;13;29;-;0;A34F"
QString encodeMove(const chess::Board &boardBefore, const chess::Move &m, int ply, const QString &hash);
// "WP5>29" or "WP7>64=Q": the compact form of the history.
QString compactMove(const chess::Board &board, const chess::Move &m);
// Parses the compact form; false when it is not one.
bool parseCompact(const QString &token, QString *uid, int *number, char *promo);

// Rebuilds a game from its compact history.  On failure returns false and
// sets badToken to the first move that does not replay.
bool replay(const QStringList &tokens, chess::Board *board, QString *badToken = nullptr);

} // namespace chs

class GameSession : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Handshake, Playing, Over };
    Q_ENUM(State)

    // Levels of the protocol log, as in the Python version.
    enum class Level { Info, Warn, Error, Tx, Rx };
    Q_ENUM(Level)

    struct Outbound
    {
        chs::Frame frame;
        int attempts = 0;
        double nextTry = 0.0;
        bool reliable = true;
    };

    GameSession(const QString &myCall, const QString &peerCall, QObject *parent = nullptr);

    // Deterministic draws for the tests.
    void setRandomSeed(quint32 seed) { m_rng.seed(seed); }
    void setRetrySeconds(double seconds) { m_retrySeconds = seconds; }
    double retrySeconds() const { return m_retrySeconds; }

    // ---- state ------------------------------------------------------------
    QString myCall() const { return m_myCall; }
    QString peerCall() const { return m_peerCall; }
    QString gid() const { return m_gid; }
    State state() const { return m_state; }
    char myColor() const { return m_myColor; }
    const chess::Board &board() const { return m_board; }
    chess::Board &board() { return m_board; }
    int seq() const { return m_seq; }
    quint32 myNonce() const { return m_myNonce; }
    std::optional<quint32> peerNonce() const { return m_peerNonce; }
    const Outbound *pending() const { return m_pending ? &*m_pending : nullptr; }
    QString result() const { return m_result; }
    QString resultCode() const { return m_resultCode; }
    bool drawOfferedByPeer() const { return m_drawOfferedByPeer; }
    bool drawOfferedByMe() const { return m_drawOfferedByMe; }
    bool myTurn() const;

    // Text for a board status, in the current language.
    static QString statusText(chess::Board::Status status, char turn);

    // ---- actions ----------------------------------------------------------
    void invite(double now);
    bool playLocal(const QString &uid, int to, char promo, double now);
    void resign(double now);
    void offerDraw(double now);
    void answerDraw(bool accept, double now);
    void sendChat(const QString &text, double now);
    void requestSync(double now);
    void ping(double now);

    // Restores a saved game: identity, colours, counters and the replayed
    // board.  The state becomes Playing (or Over when the position is final).
    void restore(const QString &gid, char myColor, quint32 myNonce, std::optional<quint32> peerNonce,
                 int seq, const chess::Board &board);

    // ---- clock and receive ---------------------------------------------------
    // Called every second by the application, for retransmissions.
    void tick(double now);
    // An information field received from the radio.
    void feed(const QByteArray &info, const QString &srcFromAx25, double now);

signals:
    void sendFrame(const chs::Frame &frame);
    void logMessage(GameSession::Level level, const QString &text);
    void stateChanged();
    void moveApplied(const chess::Move &move, bool byPeer);
    // The whole history changed at once (resynchronisation, new game).
    void historyReset();
    void chatReceived(const QString &who, const QString &text);
    void gameOver(const QString &code, const QString &text);
    void drawOffered();

private:
    int nextSeq();
    chs::Frame make(const QString &type, const QString &payload);
    chs::Frame send(const QString &type, const QString &payload, double now, std::optional<bool> reliable = {});
    void transmit(Outbound &ob, double now);
    void pump(double now);
    void ackDone(int seq, double now);
    void ack(const chs::Frame &f, double now);
    void assignColors();
    void checkEnd();
    void finish(const QString &code, const QString &label);

    void rxHello(const chs::Frame &f, double now);
    void rxAccept(const chs::Frame &f, double now);
    void rxMove(const chs::Frame &f, double now);
    void rxAck(const chs::Frame &f, double now);
    void rxSyncRequest(const chs::Frame &f, double now);
    void rxSync(const chs::Frame &f, double now);
    void rxResign(const chs::Frame &f, double now);
    void rxDrawOffer(const chs::Frame &f, double now);
    void rxDrawAccept(const chs::Frame &f, double now);
    void rxDrawDecline(const chs::Frame &f, double now);
    void rxChat(const chs::Frame &f, double now);
    void rxPing(const chs::Frame &f, double now);
    void rxPong(const chs::Frame &f, double now);

    QString colourName(char color) const;

    QString m_myCall;
    QString m_peerCall;
    QRandomGenerator m_rng;
    double m_retrySeconds = chs::DefaultRetrySeconds;

    QString m_gid = QStringLiteral("0000");
    State m_state = State::Idle;
    char m_myColor = 0;
    chess::Board m_board;
    int m_seq = 0;
    quint32 m_myNonce = 0;
    std::optional<quint32> m_peerNonce;

    std::optional<Outbound> m_pending;
    std::deque<Outbound> m_queue;
    QMap<int, QString> m_seen;          // received seq -> type, against duplicates
    QString m_result;
    QString m_resultCode;
    bool m_drawOfferedByPeer = false;
    bool m_drawOfferedByMe = false;
    QMap<int, QString> m_syncParts;
    int m_syncTotal = 0;
};

Q_DECLARE_METATYPE(chs::Frame)

#endif // AX25CHESS_GAMESESSION_H
