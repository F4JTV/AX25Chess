/*
 * GameSession.cpp - the CHS-1 application protocol.
 *
 * Port of protocol.py; see GameSession.h.  The comments of the original
 * that explain a choice are kept with the code they explain.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "GameSession.h"

#include <QCoreApplication>

using chess::Board;
using chess::Move;

namespace chs {

namespace {
const QChar FieldSep = QLatin1Char('|');
const QChar SubSep = QLatin1Char(';');
} // namespace

bool isReliable(const QString &type)
{
    return type == Hello || type == Accept || type == MoveType || type == Resign || type == DrawOffer
           || type == DrawAccept || type == DrawDecline || type == Chat;
}

quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (const char c : data) {
        crc ^= quint16(quint8(c)) << 8;
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x1021) : quint16(crc << 1);
        }
    }
    return crc;
}

QByteArray toAscii(const QString &text)
{
    QByteArray out;
    out.reserve(text.size());
    for (const QChar c : text) {
        // A surrogate pair is one character in Python and gives one '?'.
        if (c.isLowSurrogate()) continue;
        out.append(c.unicode() < 0x80 ? char(c.unicode()) : '?');
    }
    return out;
}

static QString hex4(quint16 value)
{
    return QStringLiteral("%1").arg(value, 4, 16, QLatin1Char('0')).toUpper();
}

QString positionHash(const Board &board)
{
    const QString key = QStringLiteral("%1|%2|%3").arg(board.positionKey()).arg(board.halfmoveClock()).arg(board.plyCount());
    return hex4(crc16(toAscii(key)));
}

QString Frame::body() const
{
    return QStringList{QString::fromLatin1(Proto), gid, src, dst, QString::number(seq), type, payload}.join(FieldSep);
}

QByteArray Frame::encode() const
{
    const QByteArray b = toAscii(body());
    return b + '|' + hex4(crc16(b)).toLatin1();
}

QStringList Frame::fields() const
{
    return payload.isEmpty() ? QStringList() : payload.split(SubSep);
}

std::optional<Frame> parseFrame(const QByteArray &dataIn)
{
    // Strictly ASCII, as Python's decode("ascii", "strict").
    for (const char c : dataIn) {
        if (quint8(c) >= 0x80) return std::nullopt;
    }
    const QString data = QString::fromLatin1(dataIn).trimmed();
    if (!data.startsWith(QString::fromLatin1(Proto) + FieldSep)) return std::nullopt;
    const QStringList parts = data.split(FieldSep);
    if (parts.size() < 8) return std::nullopt;
    const QString crcText = parts.last();
    const QString body = parts.mid(0, parts.size() - 1).join(FieldSep);
    if (hex4(crc16(body.toLatin1())) != crcText.toUpper()) return std::nullopt;
    bool ok = false;
    const int seq = parts.at(4).trimmed().toInt(&ok);
    if (!ok) return std::nullopt;
    Frame f;
    f.gid = parts.at(1);
    f.src = parts.at(2);
    f.dst = parts.at(3);
    f.seq = seq;
    f.type = parts.at(5);
    f.payload = parts.mid(6, parts.size() - 7).join(FieldSep);   // a '|' in a CHAT survives
    return f;
}

QString encodeMove(const Board &board, const Move &m, int ply, const QString &hash)
{
    return QStringList{board.uidOf(m), QString::number(chess::squareNumber(m.from)),
                       QString::number(chess::squareNumber(m.to)),
                       m.promo ? QString(QChar(m.promo)) : QStringLiteral("-"), QString::number(ply), hash}
        .join(SubSep);
}

QString compactMove(const Board &board, const Move &m)
{
    QString s = QStringLiteral("%1>%2").arg(board.uidOf(m)).arg(chess::squareNumber(m.to));
    if (m.promo) s += QLatin1Char('=') + QString(QChar(m.promo));
    return s;
}

bool parseCompact(const QString &tokenIn, QString *uid, int *number, char *promo)
{
    QString token = tokenIn;
    char p = 0;
    const int eq = token.indexOf(QLatin1Char('='));
    if (eq >= 0) {
        const QString promoText = token.mid(eq + 1);
        token = token.left(eq);
        if (promoText.size() == 1) p = char(promoText.at(0).toLatin1());
        else if (!promoText.isEmpty()) return false;
    }
    const int gt = token.indexOf(QLatin1Char('>'));
    if (gt < 0) return false;
    bool ok = false;
    const int n = token.mid(gt + 1).toInt(&ok);
    if (!ok) return false;
    if (uid) *uid = token.left(gt);
    if (number) *number = n;
    if (promo) *promo = p;
    return true;
}

bool replay(const QStringList &tokens, Board *board, QString *badToken)
{
    Board rebuilt;
    for (const QString &tok : tokens) {
        QString uid;
        int number = 0;
        char promo = 0;
        Move m;
        const int to = parseCompact(tok, &uid, &number, &promo) ? chess::squareFromNumber(number) : -1;
        if (to < 0 || !rebuilt.findMove(uid, to, promo, &m)) {
            if (badToken) *badToken = tok;
            return false;
        }
        m.san = rebuilt.san(m);
        rebuilt.push(m);
    }
    if (board) *board = rebuilt;
    return true;
}

} // namespace chs

using namespace chs;

// =====================================================================
//  Session
// =====================================================================

GameSession::GameSession(const QString &myCall, const QString &peerCall, QObject *parent)
    : QObject(parent), m_myCall(myCall.toUpper()), m_peerCall(peerCall.toUpper()),
      m_rng(QRandomGenerator::securelySeeded())
{
}

bool GameSession::myTurn() const
{
    return m_state == State::Playing && m_myColor != 0 && m_board.turn() == m_myColor && !m_pending;
}

QString GameSession::statusText(Board::Status status, char turn)
{
    switch (status) {
    case Board::Status::Checkmate:
        return turn == chess::White ? tr("Checkmate - Black wins") : tr("Checkmate - White wins");
    case Board::Status::Stalemate: return tr("Stalemate - the game is a draw");
    case Board::Status::FiftyMoves: return tr("Draw by the fifty-move rule");
    case Board::Status::Repetition: return tr("Draw by threefold repetition");
    case Board::Status::InsufficientMaterial: return tr("Draw by insufficient material");
    case Board::Status::Playing: break;
    }
    return tr("Game in progress");
}

static QString statusCode(Board::Status status)
{
    switch (status) {
    case Board::Status::Checkmate: return QStringLiteral("checkmate");
    case Board::Status::Stalemate: return QStringLiteral("stalemate");
    case Board::Status::FiftyMoves: return QStringLiteral("fifty");
    case Board::Status::Repetition: return QStringLiteral("repetition");
    case Board::Status::InsufficientMaterial: return QStringLiteral("material");
    case Board::Status::Playing: break;
    }
    return QStringLiteral("playing");
}

QString GameSession::colourName(char color) const
{
    return color == chess::White ? tr("White") : tr("Black");
}

// ---- helpers -------------------------------------------------------------

int GameSession::nextSeq()
{
    m_seq = (m_seq + 1) % 10000;
    return m_seq;
}

Frame GameSession::make(const QString &type, const QString &payload)
{
    Frame f;
    f.gid = m_gid;
    f.src = m_myCall;
    f.dst = m_peerCall;
    f.seq = nextSeq();
    f.type = type;
    f.payload = payload;
    return f;
}

Frame GameSession::send(const QString &type, const QString &payload, double now, std::optional<bool> reliable)
{
    Outbound ob;
    ob.frame = make(type, payload);
    ob.reliable = reliable.value_or(isReliable(type));
    if (ob.reliable) {
        if (!m_pending) {
            m_pending = ob;
            transmit(*m_pending, now);
        } else {
            m_queue.push_back(ob);
        }
    } else {
        emit sendFrame(ob.frame);
    }
    return ob.frame;
}

void GameSession::transmit(Outbound &ob, double now)
{
    ob.attempts++;
    ob.nextTry = now + m_retrySeconds + m_rng.bounded(RetryJitterSeconds);
    emit sendFrame(ob.frame);
    if (ob.attempts > 1) {
        emit logMessage(Level::Warn, tr("Retransmitting %1 seq=%2 (attempt %3/%4)")
                                         .arg(ob.frame.type).arg(ob.frame.seq).arg(ob.attempts).arg(MaxAttempts));
    }
}

void GameSession::pump(double now)
{
    if (!m_pending && !m_queue.empty()) {
        m_pending = m_queue.front();
        m_queue.pop_front();
        transmit(*m_pending, now);
    }
}

void GameSession::ackDone(int seq, double now)
{
    if (m_pending && m_pending->frame.seq == seq) {
        m_pending.reset();
        pump(now);
    }
}

void GameSession::ack(const Frame &f, double now)
{
    // A reliable frame counts as received only once it is acknowledged: a
    // frame turned down without an ACK (a move out of sequence, a bad
    // fingerprint...) must be handled again when it is repeated, not taken
    // for a duplicate and acknowledged without having been applied.
    if (isReliable(f.type)) {
        m_seen.insert(f.seq, f.type);
        if (m_seen.size() > 200) {
            for (int i = 0; i < 100 && !m_seen.isEmpty(); i++) m_seen.erase(m_seen.begin());
        }
    }
    send(Ack, QStringList{QString::number(f.seq), positionHash(m_board)}.join(QLatin1Char(';')), now, false);
}

// ---- actions -------------------------------------------------------------

void GameSession::invite(double now)
{
    m_gid = QStringLiteral("%1").arg(m_rng.generate() & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper();
    m_myNonce = m_rng.generate();
    m_state = State::Handshake;
    m_invitedByMe = true;
    m_board = Board();
    m_myColor = 0;
    m_peerNonce.reset();
    m_result.clear();
    m_resultCode.clear();
    m_drawOfferedByMe = m_drawOfferedByPeer = false;
    emit historyReset();
    send(Hello, QStringLiteral("%1;1").arg(m_myNonce, 8, 16, QLatin1Char('0')).toUpper(), now);
    emit logMessage(Level::Info, tr("Invitation sent (game %1)").arg(m_gid));
    emit stateChanged();
}

bool GameSession::playLocal(const QString &uid, int to, char promo, double now)
{
    if (m_state != State::Playing || m_board.turn() != m_myColor) {
        emit logMessage(Level::Error, tr("It is not your turn."));
        return false;
    }
    if (m_pending) {
        emit logMessage(Level::Error, tr("Previous move not acknowledged, please wait."));
        return false;
    }
    Move m;
    if (!m_board.findMove(uid, to, promo, &m)) {
        emit logMessage(Level::Error, tr("Illegal move: %1 to square %2").arg(uid).arg(chess::squareNumber(to)));
        return false;
    }
    const int ply = m_board.plyCount();
    m.san = m_board.san(m);
    const QString payloadUid = m_board.uidOf(m);
    m_board.push(m);
    const QString h = positionHash(m_board);
    send(MoveType, encodeMove(m_board, m, ply, h), now);
    emit logMessage(Level::Tx, tr("%1 plays %2  [%3 -> square %4]")
                                   .arg(m_myCall, m.san, payloadUid).arg(chess::squareNumber(to)));
    emit moveApplied(m, false);
    checkEnd();
    emit stateChanged();
    return true;
}

void GameSession::resign(double now)
{
    if (m_state != State::Playing) return;
    send(Resign, QString(), now);
    finish(QStringLiteral("resign"), tr("%1 resigns").arg(m_myCall));
}

void GameSession::offerDraw(double now)
{
    if (m_state == State::Playing && !m_drawOfferedByMe) {
        m_drawOfferedByMe = true;
        send(DrawOffer, QString(), now);
        emit logMessage(Level::Info, tr("Draw offer sent"));
    }
}

void GameSession::answerDraw(bool accept, double now)
{
    if (!m_drawOfferedByPeer) return;
    m_drawOfferedByPeer = false;
    send(accept ? DrawAccept : DrawDecline, QString(), now);
    if (accept) finish(QStringLiteral("draw"), tr("Draw by mutual agreement"));
    else emit logMessage(Level::Info, tr("Draw offer declined"));
    emit stateChanged();
}

void GameSession::sendChat(const QString &text, double now)
{
    QString t = text.left(MaxChatLength);
    t.replace(QLatin1Char('|'), QLatin1Char('/'));
    send(Chat, t, now);
    emit chatReceived(m_myCall, text);
}

void GameSession::requestSync(double now)
{
    m_syncParts.clear();
    m_syncTotal = 0;
    send(SyncRequest, QString::number(m_board.plyCount()), now, false);
    emit logMessage(Level::Info, tr("Resynchronisation requested"));
}

void GameSession::ping(double now)
{
    send(Ping, QStringLiteral("%1").arg(m_rng.generate() & 0xFFFF, 4, 16, QLatin1Char('0')).toUpper(), now, false);
}

void GameSession::restore(const QString &gid, char myColor, quint32 myNonce, std::optional<quint32> peerNonce,
                          int seq, const Board &board)
{
    m_gid = gid;
    m_myColor = myColor;
    m_myNonce = myNonce;
    m_peerNonce = peerNonce;
    m_seq = seq;
    m_board = board;
    m_pending.reset();
    m_queue.clear();
    m_result.clear();
    m_resultCode.clear();
    m_drawOfferedByMe = m_drawOfferedByPeer = false;
    m_state = State::Playing;
    m_invitedByMe = false;
    const Board::Status status = m_board.status();
    if (status != Board::Status::Playing) {
        m_state = State::Over;
        m_resultCode = statusCode(status);
        m_result = statusText(status, m_board.turn());
    }
    emit historyReset();
    emit stateChanged();
}

// ---- clock ---------------------------------------------------------------

void GameSession::tick(double now)
{
    if (!m_pending) {
        pump(now);
        return;
    }
    Outbound &ob = *m_pending;
    if (now < ob.nextTry) return;
    if (ob.attempts >= MaxAttempts) {
        emit logMessage(Level::Error, tr("No acknowledgement for %1 after %2 attempts - link lost?")
                                          .arg(ob.frame.type).arg(MaxAttempts));
        ob.attempts = 0;
        ob.nextTry = now + m_retrySeconds * 4;
        return;
    }
    transmit(ob, now);
}

// ---- receive -------------------------------------------------------------

void GameSession::feed(const QByteArray &info, const QString &srcFromAx25, double now)
{
    Q_UNUSED(srcFromAx25);
    const std::optional<Frame> parsed = parseFrame(info);
    if (!parsed) {
        emit logMessage(Level::Warn, tr("Frame ignored (bad CRC or malformed)"));
        return;
    }
    const Frame &f = *parsed;
    const QString dst = f.dst.toUpper();
    if (dst != m_myCall && dst != QLatin1String("ALL") && dst != QLatin1String("CQ")) {
        // Games between other stations are none of ours.  But the
        // correspondent writing to another callsign means the two stations
        // disagree on this one's (an SSID, a typing slip): dropped in
        // silence, the frame looked like one the radio never decoded.
        if (f.src.toUpper() == m_peerCall) {
            emit logMessage(Level::Warn, tr("Frame from %1 addressed to %2, not to this station (%3) - check the callsigns")
                                             .arg(f.src, f.dst, m_myCall));
        }
        return;
    }
    if (f.src.toUpper() != m_peerCall) {
        emit logMessage(Level::Warn, tr("Frame received from %1, expected peer %2 - ignored").arg(f.src, m_peerCall));
        return;
    }
    if ((m_state == State::Playing || m_state == State::Handshake) && f.gid != m_gid && f.type != Hello) {
        emit logMessage(Level::Warn, tr("Frame from another game (%1) ignored").arg(f.gid));
        return;
    }

    emit logMessage(Level::Rx, QStringLiteral("< %1").arg(f.text()));

    // A HELLO for another game id opens a new sequence space: the inviting
    // station starts its frame counter again, so its HELLO usually carries
    // the same seq as the one of the previous game and would be taken for a
    // duplicate - acknowledged, never answered, the inviter stuck in the
    // handshake.  (The Python version 1.0 has this defect; a retransmitted
    // HELLO of the same game keeps its gid and is still deduplicated.)
    if (f.type == Hello && f.gid != m_gid) {
        // Both stations invited at once: each would accept the other's
        // HELLO and end up in a different game.  While our own HELLO is
        // still unacknowledged, both keep the invitation with the smaller
        // game id; the other HELLO is left unanswered and its sender, on
        // receiving ours, accepts it.  Once ours has been acknowledged, a
        // new HELLO from the peer is a deliberate new invitation.
        const bool ourHelloInFlight = m_state == State::Handshake && m_invitedByMe && m_pending
                                      && m_pending->frame.type == Hello && m_pending->frame.gid == m_gid;
        if (ourHelloInFlight && f.gid.toUInt(nullptr, 16) > m_gid.toUInt(nullptr, 16)) {
            emit logMessage(Level::Info, tr("Crossed invitations: game %1 kept, %2 set aside").arg(m_gid, f.gid));
            return;
        }
        m_seen.clear();
    }

    // Duplicate: acknowledge again without replaying.  This idempotence is
    // what keeps a lost ACK from deadlocking the game.
    if (isReliable(f.type) && m_seen.value(f.seq) == f.type) {
        ack(f, now);
        return;
    }
    if (f.type == Hello) rxHello(f, now);
    else if (f.type == Accept) rxAccept(f, now);
    else if (f.type == MoveType) rxMove(f, now);
    else if (f.type == Ack) rxAck(f, now);
    else if (f.type == SyncRequest) rxSyncRequest(f, now);
    else if (f.type == Sync) rxSync(f, now);
    else if (f.type == Resign) rxResign(f, now);
    else if (f.type == DrawOffer) rxDrawOffer(f, now);
    else if (f.type == DrawAccept) rxDrawAccept(f, now);
    else if (f.type == DrawDecline) rxDrawDecline(f, now);
    else if (f.type == Chat) rxChat(f, now);
    else if (f.type == Ping) rxPing(f, now);
    else if (f.type == Pong) rxPong(f, now);
    else {
        emit logMessage(Level::Warn, tr("Unknown frame type: %1").arg(f.type));
        return;
    }
    emit stateChanged();
}

// ---- handlers ------------------------------------------------------------

static bool parseHex32(const QString &text, quint32 *out)
{
    bool ok = false;
    const qulonglong v = text.toULongLong(&ok, 16);
    if (!ok || v > 0xFFFFFFFFull) return false;
    *out = quint32(v);
    return true;
}

void GameSession::rxHello(const Frame &f, double now)
{
    const QStringList fields = f.fields();
    quint32 nonce = 0;
    if (fields.isEmpty() || !parseHex32(fields.at(0), &nonce)) return;
    m_peerNonce = nonce;
    m_gid = f.gid;
    m_invitedByMe = false;
    // Whatever the previous game left to send (a move or a message waiting
    // for an ACK that will never come: the peer ignores another game's
    // frames) would hold the ACPT behind it for ever.
    m_pending.reset();
    m_queue.clear();
    if (m_myNonce == 0) m_myNonce = m_rng.generate();
    m_board = Board();
    m_result.clear();
    m_resultCode.clear();
    m_drawOfferedByMe = m_drawOfferedByPeer = false;
    m_state = State::Handshake;
    assignColors();
    emit historyReset();
    ack(f, now);
    send(Accept, QStringLiteral("%1;%2").arg(QStringLiteral("%1").arg(m_myNonce, 8, 16, QLatin1Char('0')).toUpper(),
                                             QString(QChar(m_myColor))),
         now);
    m_state = State::Playing;
    emit logMessage(Level::Info, tr("Game %1 accepted - you play %2").arg(m_gid, colourName(m_myColor)));
}

void GameSession::rxAccept(const Frame &f, double now)
{
    const QStringList fields = f.fields();
    quint32 nonce = 0;
    if (fields.isEmpty() || !parseHex32(fields.at(0), &nonce)) return;
    m_peerNonce = nonce;
    assignColors();
    const QString peerClaim = fields.size() > 1 ? fields.at(1) : QString();
    if (!peerClaim.isEmpty() && peerClaim == QString(QChar(m_myColor))) {
        emit logMessage(Level::Error, tr("Colour assignment conflict - send a new invitation"));
        ack(f, now);
        return;
    }
    ack(f, now);
    m_state = State::Playing;
    emit logMessage(Level::Info, tr("Game %1 started - you play %2").arg(m_gid, colourName(m_myColor)));
}

void GameSession::assignColors()
{
    // Deterministic: the larger nonce plays White; an (improbable) tie is
    // settled by the alphabetical order of the callsigns.  Both stations
    // reach the same conclusion without another exchange.
    if (!m_peerNonce) return;
    if (m_myNonce > *m_peerNonce) m_myColor = chess::White;
    else if (m_myNonce < *m_peerNonce) m_myColor = chess::Black;
    else m_myColor = m_myCall < m_peerCall ? chess::White : chess::Black;
}

void GameSession::rxMove(const Frame &f, double now)
{
    const QStringList fields = f.fields();
    if (fields.size() < 6) return;
    const QString uid = fields.at(0);
    const QString promoText = fields.at(3);
    const char promo = (promoText == QLatin1String("-") || promoText.isEmpty()) ? 0 : char(promoText.at(0).toLatin1());
    bool okFrom = false, okTo = false, okPly = false;
    const int fromNum = fields.at(1).toInt(&okFrom);
    const int toNum = fields.at(2).toInt(&okTo);
    const int ply = fields.at(4).toInt(&okPly);
    const int fromSq = chess::squareFromNumber(fromNum);
    const int toSq = chess::squareFromNumber(toNum);
    const QString theirHash = fields.at(5);
    if (!okFrom || !okTo || !okPly || fromSq < 0 || toSq < 0) {
        emit logMessage(Level::Error, tr("Received move could not be read"));
        return;
    }
    if (m_state == State::Over) {
        // The game ended here (a resignation, a mate) before the peer knew:
        // acknowledge, or its retransmissions would never stop.
        emit logMessage(Level::Info, tr("Move received after the end of the game - acknowledged, not played"));
        ack(f, now);
        return;
    }
    if (m_state != State::Playing) {
        emit logMessage(Level::Warn, tr("Move received outside a game - ignored"));
        return;
    }
    const int expected = m_board.plyCount();
    if (ply < expected) {
        emit logMessage(Level::Info, tr("Move %1 already known - acknowledged again").arg(ply));
        ack(f, now);
        return;
    }
    if (ply > expected) {
        emit logMessage(Level::Error, tr("Gap in the sequence (received %1, expected %2)").arg(ply).arg(expected));
        requestSync(now);
        return;
    }
    if (m_board.turn() == m_myColor) {
        emit logMessage(Level::Error, tr("Move received while it is your turn - out of sync"));
        requestSync(now);
        return;
    }
    Move m;
    if (!m_board.findMove(uid, toSq, promo, &m) || m.from != fromSq) {
        emit logMessage(Level::Error, tr("Illegal move received: %1 square %2 - resynchronising").arg(uid).arg(toNum));
        requestSync(now);
        return;
    }
    m.san = m_board.san(m);
    m_board.push(m);
    const QString mine = positionHash(m_board);
    if (mine != theirHash.toUpper()) {
        m_board.pop();
        emit logMessage(Level::Error, tr("Fingerprint mismatch (%1 != %2) - resynchronising").arg(mine, theirHash));
        requestSync(now);
        return;
    }
    ack(f, now);
    emit logMessage(Level::Rx, tr("%1 plays %2  [%3 -> square %4]").arg(f.src, m.san, uid).arg(toNum));
    emit moveApplied(m, true);
    checkEnd();
}

void GameSession::rxAck(const Frame &f, double now)
{
    const QStringList fields = f.fields();
    if (fields.isEmpty()) return;
    bool ok = false;
    const int acked = fields.at(0).toInt(&ok);
    if (!ok) return;
    ackDone(acked, now);
    if (fields.size() > 1 && m_state == State::Playing) {
        const QString theirs = fields.at(1).toUpper();
        const QString mine = positionHash(m_board);
        if (theirs != mine && m_board.turn() != m_myColor) {
            emit logMessage(Level::Warn, tr("Peer fingerprint differs (%1 != %2)").arg(theirs, mine));
        }
    }
}

void GameSession::rxSyncRequest(const Frame &f, double now)
{
    Q_UNUSED(f);
    emit logMessage(Level::Info, tr("The peer is requesting a resynchronisation"));
    QStringList moves;
    // The compact form needs the identifiers, which the board keeps.
    for (const Move &m : m_board.moves()) moves << compactMove(m_board, m);
    QList<QStringList> chunks;
    for (int i = 0; i < moves.size(); i += SyncChunkMoves) chunks << moves.mid(i, SyncChunkMoves);
    if (chunks.isEmpty()) chunks << QStringList();
    const int total = int(chunks.size());
    for (int i = 0; i < total; i++) {
        const QString payload = QStringList{QStringLiteral("%1/%2").arg(i + 1).arg(total), QString::number(moves.size()),
                                            chunks.at(i).join(QLatin1Char(',')), m_gid}
                                    .join(QLatin1Char(';'));
        send(Sync, payload, now, false);
    }
}

void GameSession::rxSync(const Frame &f, double now)
{
    Q_UNUSED(now);
    const QStringList fields = f.fields();
    if (fields.size() < 3) return;
    const QStringList pt = fields.at(0).split(QLatin1Char('/'));
    if (pt.size() != 2) return;
    bool okPart = false, okTotal = false;
    const int part = pt.at(0).toInt(&okPart);
    const int total = pt.at(1).toInt(&okTotal);
    if (!okPart || !okTotal || part < 1 || total < 1 || part > total) return;
    m_syncTotal = total;
    m_syncParts.insert(part, fields.at(2));
    for (int i = 1; i <= total; i++) {
        if (!m_syncParts.contains(i)) {
            emit logMessage(Level::Info, tr("Resynchronising %1/%2").arg(m_syncParts.size()).arg(total));
            return;
        }
    }

    QStringList joined;
    for (int i = 1; i <= total; i++) joined << m_syncParts.value(i);
    QStringList tokens;
    for (const QString &t : joined.join(QLatin1Char(',')).split(QLatin1Char(','))) {
        if (!t.isEmpty()) tokens << t;
    }
    Board rebuilt;
    QString bad;
    if (!replay(tokens, &rebuilt, &bad)) {
        emit logMessage(Level::Error, tr("Received history invalid at move '%1' - cannot resynchronise").arg(bad));
        m_syncParts.clear();
        return;
    }
    // The local history must be a prefix of the received one; anything else
    // is declared irreconcilable rather than papered over.
    QStringList local;
    for (const Move &m : m_board.moves()) local << compactMove(m_board, m);
    if (local != tokens.mid(0, local.size())) {
        emit logMessage(Level::Error, tr("Irreconcilable move histories - a new game must be started"));
        m_syncParts.clear();
        return;
    }
    m_board = rebuilt;
    m_syncParts.clear();
    emit historyReset();
    emit logMessage(Level::Info, tr("Resynchronised on %1 half-moves (fingerprint %2)")
                                     .arg(tokens.size()).arg(positionHash(m_board)));
    checkEnd();
}

void GameSession::rxResign(const Frame &f, double now)
{
    ack(f, now);
    finish(QStringLiteral("resign"), tr("%1 resigns - you win").arg(f.src));
}

void GameSession::rxDrawOffer(const Frame &f, double now)
{
    ack(f, now);
    m_drawOfferedByPeer = true;
    emit drawOffered();
}

void GameSession::rxDrawAccept(const Frame &f, double now)
{
    ack(f, now);
    finish(QStringLiteral("draw"), tr("Draw by mutual agreement"));
}

void GameSession::rxDrawDecline(const Frame &f, double now)
{
    ack(f, now);
    m_drawOfferedByMe = false;
    emit logMessage(Level::Info, tr("Draw offer rejected"));
}

void GameSession::rxChat(const Frame &f, double now)
{
    ack(f, now);
    emit chatReceived(f.src, f.payload);
}

void GameSession::rxPing(const Frame &f, double now)
{
    send(Pong, f.payload, now, false);
}

void GameSession::rxPong(const Frame &f, double now)
{
    Q_UNUSED(f);
    Q_UNUSED(now);
    emit logMessage(Level::Info, tr("Beacon reply received"));
}

// ---- end of game ---------------------------------------------------------

void GameSession::checkEnd()
{
    const Board::Status status = m_board.status();
    if (status != Board::Status::Playing) finish(statusCode(status), statusText(status, m_board.turn()));
}

void GameSession::finish(const QString &code, const QString &label)
{
    m_state = State::Over;
    m_result = label;
    m_resultCode = code;
    emit gameOver(code, label);
    emit stateChanged();
}
