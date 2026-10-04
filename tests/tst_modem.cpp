/*
 * tst_modem.cpp - a game through the embedded modem, end to end.
 *
 *   tst_modem --print-frames      the peer's frames, one per line, in the
 *                                 monitor format gen_packets reads
 *   tst_modem --conf <file>       the modem on that configuration (audio on
 *                                 standard input), a local game session fed
 *                                 by what it decodes
 *
 * Both modes run the same two sessions with the same seeds: the local one
 * of the second mode reaches the state the first mode predicted, or the
 * test fails.  scripts/run_modem_test.sh joins the two through gen_packets.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "GameSession.h"
#include "RadioLink.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <deque>

namespace {

const QString Local = QStringLiteral("N0CALL");
const QString Peer = QStringLiteral("N0CALL-2");

// The local station plays e2-e4 when it has White, e7-e5 when it has Black.
void playIfMyTurn(GameSession &s)
{
    if (!s.myTurn()) return;
    if (s.myColor() == chess::White && s.board().plyCount() == 0) {
        s.playLocal(QStringLiteral("WP5"), chess::squareFromName(QStringLiteral("e4")), 0, 0);
    } else if (s.myColor() == chess::Black && s.board().plyCount() == 1) {
        s.playLocal(QStringLiteral("BP5"), chess::squareFromName(QStringLiteral("e5")), 0, 0);
    }
}

void peerPlays(GameSession &p)
{
    if (!p.myTurn()) return;
    if (p.myColor() == chess::White && p.board().plyCount() == 0) {
        p.playLocal(QStringLiteral("WP4"), chess::squareFromName(QStringLiteral("d4")), 0, 0);
    } else if (p.myColor() == chess::Black && p.board().plyCount() == 1) {
        p.playLocal(QStringLiteral("BP4"), chess::squareFromName(QStringLiteral("d5")), 0, 0);
    }
}

// Both sessions in memory: the peer's frames are what goes on the air.
// Frames are delivered one at a time, in order, as the radio would: a
// session never receives while it is still handling the previous frame.
QStringList peerFrames(QString *expectedHash, int *expectedPlies)
{
    GameSession local(Local, Peer);
    GameSession peer(Peer, Local);
    local.setRandomSeed(1);
    peer.setRandomSeed(2);
    QStringList air;
    std::deque<std::pair<GameSession *, chs::Frame>> queue;
    QObject::connect(&peer, &GameSession::sendFrame, &peer, [&](const chs::Frame &f) {
        air << QStringLiteral("%1>%2:%3").arg(f.src, f.dst, f.text());
        queue.emplace_back(&local, f);
    });
    QObject::connect(&local, &GameSession::sendFrame, &local, [&](const chs::Frame &f) {
        queue.emplace_back(&peer, f);
    });
    peer.invite(0);
    while (!queue.empty()) {
        auto [to, f] = queue.front();
        queue.pop_front();
        to->feed(f.encode(), f.src, 0);
        playIfMyTurn(local);
        peerPlays(peer);
    }
    *expectedHash = chs::positionHash(local.board());
    *expectedPlies = local.board().plyCount();
    return air;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOption({QStringLiteral("print-frames"), QStringLiteral("print the peer's frames")});
    parser.addOption({QStringLiteral("conf"), QStringLiteral("direwolf.conf"), QStringLiteral("path")});
    parser.process(app);

    QString expectedHash;
    int expectedPlies = 0;
    const QStringList air = peerFrames(&expectedHash, &expectedPlies);
    if (parser.isSet(QStringLiteral("print-frames"))) {
        for (const QString &line : air) std::printf("%s\n", qPrintable(line));
        return 0;
    }

    RadioConfig config;
    config.station.callsign = Local;
    config.modem.ownFile = true;
    config.modem.configFile = parser.value(QStringLiteral("conf"));
    RadioLink link(config);
    GameSession local(Local, Peer);
    local.setRandomSeed(1);
    int decoded = 0;
    QObject::connect(&link, &RadioLink::modemLog, &app, [](int level, const QString &line) {
        if (level == 1) std::fprintf(stderr, "modem: %s\n", qPrintable(line));
    });
    QObject::connect(&link, &RadioLink::logMessage, &app, [](const QString &level, const QString &text) {
        std::fprintf(stderr, "[%s] %s\n", qPrintable(level), qPrintable(text));
    });
    QObject::connect(&link, &RadioLink::infoReceived, &app,
                     [&](const QString &src, const QString &, const QByteArray &info, const QString &monitor) {
        decoded++;
        std::fprintf(stderr, "decoded: %s\n", qPrintable(monitor));
        local.feed(info, src, 0);
        playIfMyTurn(local);
        if (decoded == air.size()) QTimer::singleShot(500, &app, [&] { app.exit(0); });
    });
    QTimer::singleShot(25000, &app, [&] { app.exit(0); });
    link.startModem();
    app.exec();
    link.stopModem();

    const QString hash = chs::positionHash(local.board());
    std::fprintf(stderr, "frames sent %lld, decoded %d; plies %d (expected %d); fingerprint %s (expected %s)\n",
                 static_cast<long long>(air.size()), decoded, local.board().plyCount(), expectedPlies,
                 qPrintable(hash), qPrintable(expectedHash));
    const bool ok = decoded == air.size() && local.state() == GameSession::State::Playing
                    && local.board().plyCount() == expectedPlies && hash == expectedHash;
    std::fprintf(stderr, "%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
