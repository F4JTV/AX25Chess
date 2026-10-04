/*
 * tst_chess.cpp - rules, CHS-1 protocol and saved games.
 *
 * The vectors in tests/vectors/python_vectors.json come from the Python
 * version 1.0.0 (make_vectors.py): the same games must give the same SAN,
 * FEN, fingerprints, compact moves and frames, byte for byte, or the two
 * versions could not play each other.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AX25Frame.h"
#include "Board.h"
#include "GameSession.h"
#include "GameStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

using namespace chess;

namespace {

QJsonObject vectors()
{
    QFile f(QStringLiteral(TEST_VECTORS));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool playUci(Board &b, const QString &uci, Move *played)
{
    const int from = squareFromName(uci.mid(0, 2));
    const int to = squareFromName(uci.mid(2, 2));
    const char promo = uci.size() > 4 ? char(uci.at(4).toUpper().toLatin1()) : 0;
    for (const Move &m : b.legalMoves()) {
        if (m.from == from && m.to == to && m.promo == promo) {
            *played = m;
            played->san = b.san(m);
            b.push(*played);
            return true;
        }
    }
    return false;
}

// Two sessions joined through AX.25 encode/decode, as on the air.  A frame
// can be lost; delivery is synchronous, as in the Python test.
struct Loopback
{
    GameSession *a = nullptr;
    GameSession *b = nullptr;
    double now = 0.0;
    double loss = 0.0;
    int lost = 0;
    int retries = 0;
    QRandomGenerator rng{12345};
    QStringList endA, endB;

    void wire(GameSession *from, GameSession *to)
    {
        QObject::connect(from, &GameSession::sendFrame, from, [this, to](const chs::Frame &frame) {
            const auto ax = AX25Frame::ui(frame.src, frame.dst, frame.encode(), {QStringLiteral("WIDE1-1")});
            QVERIFY(ax.has_value());
            const QByteArray raw = ax->encode();
            if (rng.generateDouble() < loss) {
                lost++;
                return;
            }
            const auto decoded = AX25Frame::decode(raw);
            QVERIFY(decoded.has_value());
            to->feed(decoded->info, decoded->source.toString(), now);
        });
        QObject::connect(from, &GameSession::logMessage, from, [this](GameSession::Level level, const QString &text) {
            if (level == GameSession::Level::Warn && text.startsWith(QLatin1String("Retransmitting"))) retries++;
        });
    }

    void settle(int limit = 500)
    {
        for (int i = 0; i < limit; i++) {
            now += 1.0;
            a->tick(now);
            b->tick(now);
            if (!a->pending() && !b->pending()) return;
        }
    }
};

} // namespace

class TestChess : public QObject
{
    Q_OBJECT

private slots:
    // ---- rules ---------------------------------------------------------------
    void perftPositions_data()
    {
        QTest::addColumn<QString>("fen");
        QTest::addColumn<int>("depth");
        QTest::addColumn<quint64>("nodes");
        const QString initial = QStringLiteral("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        QTest::newRow("initial d3") << initial << 3 << quint64(8902);
        QTest::newRow("initial d4") << initial << 4 << quint64(197281);
        QTest::newRow("kiwipete d3") << QStringLiteral("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1")
                                     << 3 << quint64(97862);
        QTest::newRow("position 3 d4") << QStringLiteral("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1") << 4 << quint64(43238);
        QTest::newRow("position 4 d3") << QStringLiteral("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1")
                                       << 3 << quint64(9467);
        QTest::newRow("position 5 d3") << QStringLiteral("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8")
                                       << 3 << quint64(62379);
    }

    void perftPositions()
    {
        QFETCH(QString, fen);
        QFETCH(int, depth);
        QFETCH(quint64, nodes);
        Board b;
        QVERIFY(b.setFen(fen));
        QCOMPARE(perft(b, depth), nodes);
        QCOMPARE(b.fen(), fen);      // the search leaves the position as it was
    }

    void identifiersSurvivePromotion()
    {
        Board b;
        Move m;
        for (const QString &uci : QStringList{"a2a4", "b7b5", "a4b5", "a7a6", "b5a6", "c8b7", "a6b7", "b8c6", "b7a8q"}) {
            QVERIFY2(playUci(b, uci, &m), qPrintable(uci));
        }
        const int pi = b.pieceIndex(QStringLiteral("WP1"));
        QVERIFY(pi >= 0);
        QCOMPARE(b.pieces()[pi].kind, Queen);
        QCOMPARE(b.pieces()[pi].bornKind, Pawn);
        QCOMPARE(b.pieces()[pi].square, squareFromName(QStringLiteral("a8")));
        QVERIFY(b.pop());
        QCOMPARE(b.pieces()[pi].kind, Pawn);
    }

    // ---- interoperability with the Python version ------------------------------
    void pythonGames_data()
    {
        QTest::addColumn<QJsonArray>("plies");
        QTest::addColumn<QString>("status");
        const QJsonObject games = vectors().value(QStringLiteral("games")).toObject();
        QVERIFY2(!games.isEmpty(), "python_vectors.json missing or empty");
        for (auto it = games.begin(); it != games.end(); ++it) {
            const QJsonObject g = it.value().toObject();
            QTest::newRow(qPrintable(it.key())) << g.value(QStringLiteral("plies")).toArray()
                                                << g.value(QStringLiteral("status")).toString();
        }
    }

    void pythonGames()
    {
        QFETCH(QJsonArray, plies);
        QFETCH(QString, status);
        Board b;
        int ply = 0;
        for (const QJsonValue &v : plies) {
            const QJsonObject p = v.toObject();
            const Board before = b;
            Move m;
            QVERIFY2(playUci(b, p.value(QStringLiteral("uci")).toString(), &m), qPrintable(p.value("uci").toString()));
            QCOMPARE(b.uidOf(m), p.value(QStringLiteral("uid")).toString());
            QCOMPARE(m.san, p.value(QStringLiteral("san")).toString());
            QCOMPARE(b.fen(), p.value(QStringLiteral("fen")).toString());
            QCOMPARE(b.positionKey(), p.value(QStringLiteral("key")).toString());
            const QString hash = chs::positionHash(b);
            QCOMPARE(hash, p.value(QStringLiteral("hash")).toString());
            QCOMPARE(chs::compactMove(b, m), p.value(QStringLiteral("compact")).toString());
            chs::Frame f;
            f.gid = QStringLiteral("3F1A");
            f.src = QStringLiteral("N0CALL");
            f.dst = QStringLiteral("N0CALL-2");
            f.seq = ply + 1;
            f.type = chs::MoveType;
            f.payload = chs::encodeMove(b, m, ply, hash);
            QCOMPARE(f.text(), p.value(QStringLiteral("frame")).toString());
            // The receiving side finds the same move from UID and square.
            Move found;
            QVERIFY(before.findMove(b.uidOf(m), m.to, m.promo, &found));
            QCOMPARE(found.from, m.from);
            ply++;
        }
        const Board::Status st = b.status();
        const QString code = st == Board::Status::Checkmate ? QStringLiteral("checkmate")
                             : st == Board::Status::Stalemate ? QStringLiteral("stalemate")
                             : st == Board::Status::Playing ? QStringLiteral("playing") : QStringLiteral("draw");
        QCOMPARE(code, status);
    }

    void pythonFrames()
    {
        const QJsonObject v = vectors();
        for (const QJsonValue &item : v.value(QStringLiteral("frames")).toArray()) {
            const QJsonObject o = item.toObject();
            chs::Frame f;
            f.gid = o.value("gid").toString();
            f.src = o.value("src").toString();
            f.dst = o.value("dst").toString();
            f.seq = o.value("seq").toInt();
            f.type = o.value("type").toString();
            f.payload = o.value("payload").toString();
            QCOMPARE(f.text(), o.value("text").toString());
            const auto parsed = chs::parseFrame(f.encode());
            QVERIFY(parsed.has_value());
            QCOMPARE(parsed->type, f.type);
            QCOMPARE(parsed->seq, f.seq);
        }
        for (const QJsonValue &item : v.value(QStringLiteral("crc")).toArray()) {
            const QJsonObject o = item.toObject();
            QCOMPARE(int(chs::crc16(o.value("text").toString().toLatin1())), o.value("crc").toInt());
        }
    }

    void malformedFramesAreRejected()
    {
        chs::Frame f;
        f.gid = "ABCD"; f.src = "N0CALL"; f.dst = "N0CALL-2"; f.seq = 3; f.type = chs::Chat; f.payload = "hi";
        QByteArray good = f.encode();
        QVERIFY(chs::parseFrame(good).has_value());
        QByteArray bad = good;
        bad[bad.size() - 1] = bad.at(bad.size() - 1) == '0' ? '1' : '0';
        QVERIFY(!chs::parseFrame(bad).has_value());
        QVERIFY(!chs::parseFrame(good.left(20)).has_value());
        QVERIFY(!chs::parseFrame(QByteArray("CHS1|x\xC3\xA9|y")).has_value());
        QVERIFY(!chs::parseFrame(QByteArray("HELLO WORLD")).has_value());
        // A '|' in a chat survives the trip as part of the payload.
        f.payload = "a|b";
        const auto parsed = chs::parseFrame(f.encode());
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->payload, QStringLiteral("a|b"));
    }

    // ---- protocol, two stations on a loopback -------------------------------------
    void scholarsMateOnACleanChannel()
    {
        GameSession a(QStringLiteral("N0CALL"), QStringLiteral("N0CALL-2"));
        GameSession b(QStringLiteral("N0CALL-2"), QStringLiteral("N0CALL"));
        a.setRandomSeed(1);
        b.setRandomSeed(2);
        Loopback link;
        link.a = &a; link.b = &b;
        link.wire(&a, &b);
        link.wire(&b, &a);
        a.invite(link.now);
        QVERIFY(a.myColor() != 0 && b.myColor() != 0);
        QVERIFY(a.myColor() != b.myColor());
        QCOMPARE(a.gid(), b.gid());
        GameSession *white = a.myColor() == White ? &a : &b;
        GameSession *black = white == &a ? &b : &a;
        const QList<QPair<GameSession *, QPair<QString, QString>>> line{
            {white, {"WP5", "e4"}}, {black, {"BP5", "e5"}}, {white, {"WB2", "c4"}}, {black, {"BN1", "c6"}},
            {white, {"WQ1", "h5"}}, {black, {"BN2", "f6"}}, {white, {"WQ1", "f7"}}};
        for (const auto &step : line) {
            QVERIFY(step.first->playLocal(step.second.first, squareFromName(step.second.second), 0, link.now));
        }
        QCOMPARE(a.board().fen(), b.board().fen());
        QCOMPARE(chs::positionHash(a.board()), chs::positionHash(b.board()));
        QCOMPARE(a.state(), GameSession::State::Over);
        QCOMPARE(b.state(), GameSession::State::Over);
        QCOMPARE(a.resultCode(), QStringLiteral("checkmate"));
    }

    void spanishGameWithThirtyFivePercentLoss()
    {
        GameSession a(QStringLiteral("N0CALL"), QStringLiteral("N0CALL-2"));
        GameSession b(QStringLiteral("N0CALL-2"), QStringLiteral("N0CALL"));
        a.setRandomSeed(11);
        b.setRandomSeed(12);
        Loopback link;
        link.a = &a; link.b = &b;
        link.wire(&a, &b);
        link.wire(&b, &a);
        a.invite(link.now);
        link.loss = 0.35;
        GameSession *white = a.myColor() == White ? &a : &b;
        GameSession *black = white == &a ? &b : &a;
        const QList<QPair<GameSession *, QPair<QString, QString>>> line{
            {white, {"WP5", "e4"}}, {black, {"BP5", "e5"}}, {white, {"WN2", "f3"}}, {black, {"BN1", "c6"}},
            {white, {"WB2", "b5"}}, {black, {"BP1", "a6"}}, {white, {"WB2", "c6"}}, {black, {"BP4", "c6"}}};
        for (const auto &step : line) {
            link.settle();
            QVERIFY(step.first->playLocal(step.second.first, squareFromName(step.second.second), 0, link.now));
        }
        link.settle();
        QVERIFY(link.lost > 0);
        QVERIFY(link.retries > 0);
        QCOMPARE(a.board().fen(), b.board().fen());

        // Resynchronisation after a corruption on one side.
        link.loss = 0.0;
        QVERIFY(b.board().pop());
        QVERIFY(b.board().pop());
        QVERIFY(a.board().fen() != b.board().fen());
        b.requestSync(link.now);
        QCOMPARE(a.board().fen(), b.board().fen());
        QCOMPARE(b.board().plyCount(), 8);
    }

    // The Python 1.0 receiver ignored a second invitation from the same
    // peer: the new session restarts its counter, the HELLO came with the
    // seq of the previous game's HELLO and passed for a duplicate.
    void secondInvitationFromTheSamePeer()
    {
        GameSession b(QStringLiteral("N0CALL-2"), QStringLiteral("N0CALL"));
        b.setRandomSeed(5);
        int accepts = 0;
        connect(&b, &GameSession::sendFrame, &b, [&accepts](const chs::Frame &f) { if (f.type == chs::Accept) accepts++; });
        for (int game = 0; game < 2; game++) {
            GameSession a(QStringLiteral("N0CALL"), QStringLiteral("N0CALL-2"));   // a new session each time, as Python's UI
            a.setRandomSeed(100 + game);
            connect(&a, &GameSession::sendFrame, &a, [&b](const chs::Frame &f) { b.feed(f.encode(), f.src, 0); });
            connect(&b, &GameSession::sendFrame, &a, [&a](const chs::Frame &f) { a.feed(f.encode(), f.src, 0); });
            a.invite(0);
            QCOMPARE(a.state(), GameSession::State::Playing);
            QCOMPARE(b.gid(), a.gid());
            disconnect(&b, nullptr, &a, nullptr);
        }
        QCOMPARE(accepts, 2);
    }

    void duplicateMoveIsAcknowledgedNotReplayed()
    {
        GameSession a(QStringLiteral("N0CALL"), QStringLiteral("N0CALL-2"));
        GameSession b(QStringLiteral("N0CALL-2"), QStringLiteral("N0CALL"));
        a.setRandomSeed(3);
        b.setRandomSeed(4);
        QList<chs::Frame> fromA;
        connect(&a, &GameSession::sendFrame, &a, [&](const chs::Frame &f) { fromA << f; b.feed(f.encode(), f.src, 0); });
        connect(&b, &GameSession::sendFrame, &b, [&](const chs::Frame &f) { a.feed(f.encode(), f.src, 0); });
        a.invite(0);
        GameSession *white = a.myColor() == White ? &a : &b;
        if (white != &a) {
            QVERIFY(b.playLocal("WP5", squareFromName("e4"), 0, 0));
            QVERIFY(a.playLocal("BP5", squareFromName("e5"), 0, 0));
        } else {
            QVERIFY(a.playLocal("WP5", squareFromName("e4"), 0, 0));
        }
        const chs::Frame move = fromA.last().type == chs::MoveType ? fromA.last() : fromA.at(fromA.size() - 2);
        QCOMPARE(move.type, chs::MoveType);
        const int plies = b.board().plyCount();
        b.feed(move.encode(), move.src, 0);      // the ACK was lost, the move came again
        QCOMPARE(b.board().plyCount(), plies);
    }

    void chatOverTheAir()
    {
        GameSession a(QStringLiteral("N0CALL"), QStringLiteral("N0CALL-2"));
        GameSession b(QStringLiteral("N0CALL-2"), QStringLiteral("N0CALL"));
        connect(&a, &GameSession::sendFrame, &a, [&](const chs::Frame &f) { b.feed(f.encode(), f.src, 0); });
        connect(&b, &GameSession::sendFrame, &b, [&](const chs::Frame &f) { a.feed(f.encode(), f.src, 0); });
        a.invite(0);
        QString got;
        connect(&b, &GameSession::chatReceived, &b, [&got](const QString &, const QString &t) { got = t; });
        a.sendChat(QStringLiteral("déjà vu | ok"), 0);
        QCOMPARE(got, QStringLiteral("d?j? vu / ok"));
    }

    // ---- saved games -----------------------------------------------------------------
    void storeRoundTripAndLegacyImport()
    {
        QTemporaryDir dir;
        GameStore store(dir.filePath("games"));
        SavedGame g;
        g.gid = "3F1A";
        g.myCall = "N0CALL";
        g.peerCall = "n0call-2";
        g.color = "B";
        g.moves = QStringList{"WP5>29", "BP5>37", "WN2>22"};
        g.nonce = 0xDEADBEEF;
        g.peerNonce = 42;
        g.seq = 17;
        QVERIFY(!store.save(g).isEmpty());
        QCOMPARE(store.count(), 1);
        const auto back = store.find("3F1A", "N0CALL-2");
        QVERIFY(back.has_value());
        QCOMPARE(back->moves, g.moves);
        QCOMPARE(back->nonce, g.nonce);
        QCOMPARE(*back->peerNonce, 42u);
        QVERIFY(back->myTurn());          // three plies: Black to move
        Board b;
        QVERIFY(chs::replay(back->moves, &b));
        QCOMPARE(b.plyCount(), 3);

        // A file of the Python version, written by json.dumps.
        QDir().mkpath(dir.filePath("legacy"));
        QFile legacy(dir.filePath("legacy/00FF-F1XYZ.json"));
        QVERIFY(legacy.open(QIODevice::WriteOnly));
        legacy.write(R"({"gid": "00FF", "call": "N0CALL", "peer": "F1XYZ", "color": "W",
                         "moves": ["WP5>29"], "nonce": 1234, "peer_nonce": null, "seq": 2,
                         "created": 1759400000.5, "updated": 1759400100.25})");
        legacy.close();
        QCOMPARE(store.importLegacy(dir.filePath("legacy")), 1);
        QCOMPARE(store.importLegacy(dir.filePath("legacy")), 0);
        const auto imported = store.find("00FF", "F1XYZ");
        QVERIFY(imported.has_value());
        QVERIFY(!imported->peerNonce.has_value());
        QVERIFY(store.remove("3F1A", "N0CALL-2"));
        QCOMPARE(store.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestChess)
#include "tst_chess.moc"
