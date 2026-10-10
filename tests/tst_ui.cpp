/*
 * tst_ui.cpp - the interface under real pointer events, and the catalog.
 *
 *  - Every string the sources hand to tr() or qsTr() has its French.
 *  - Main.qml loads without a warning, in the wide and the phone layout.
 *  - A game against a peer session: the moves are played by clicking the
 *    squares of the board, through whatever lies over it.  A panel that
 *    swallowed the press (AX25Chat's Pane/TapHandler regression) fails here.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AppController.h"
#include "BoardView.h"
#include "Catalog.h"
#include "GameSession.h"

#include <QDirIterator>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>
#include <QJsonObject>
#include <functional>

namespace {

QStringList sourceStrings()
{
    // tr("..."), qsTr("..."), translate("Context", "...").  Adjacent C++
    // literals are joined, as the compiler joins them.
    static const QRegularExpression call(QString::fromUtf8(
        R"re((?:\bqsTr|\btr|translate\(\s*"[^"]*"\s*,)\s*\(?\s*((?:"(?:[^"\\]|\\.)*"\s*)+))re"));
    static const QRegularExpression literal(QString::fromUtf8(R"re("((?:[^"\\]|\\.)*)")re"));
    QStringList out;
    for (const QString &dir : {QStringLiteral("src/app"), QStringLiteral("src/qml"), QStringLiteral("src/chess"),
                               QStringLiteral("src/radio"), QStringLiteral("src/engine")}) {
        QDirIterator it(QStringLiteral(AX25CHESS_SOURCE_DIR "/") + dir,
                        {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.qml")}, QDir::Files);
        while (it.hasNext()) {
            QFile f(it.next());
            if (!f.open(QIODevice::ReadOnly)) continue;
            const QString text = QString::fromUtf8(f.readAll());
            auto m = call.globalMatch(text);
            while (m.hasNext()) {
                QString s;
                auto parts = literal.globalMatch(m.next().captured(1));
                while (parts.hasNext()) s += parts.next().captured(1);
                s.replace(QString::fromUtf8("\\\""), QStringLiteral("\""));
                s.replace(QString::fromUtf8("\\n"), QStringLiteral("\n"));
                s.replace(QString::fromUtf8("\\u2022"), QString(QChar(0x2022)));
                if (!out.contains(s)) out << s;
            }
        }
    }
    return out;
}

struct Geometry
{
    qreal ox, oy, cell;
};

// The same arithmetic as BoardView::metrics().
Geometry boardGeometry(QQuickItem *board)
{
    const qreal w = board->width(), h = board->height();
    const qreal margin = std::max(16.0, std::min(w, h) * 0.045);
    const qreal cell = std::floor((std::min(w, h) - 2 * margin) / 8);
    return {std::floor((w - cell * 8) / 2), std::floor((h - cell * 8) / 2), cell};
}

QPoint squareCentre(QQuickItem *board, const QString &name, bool flipped)
{
    const int sq = chess::squareFromName(name);
    const Geometry g = boardGeometry(board);
    const int f = chess::fileOf(sq), r = chess::rankOf(sq);
    const int col = flipped ? 7 - f : f;
    const int row = flipped ? r : 7 - r;
    const QPointF local(g.ox + (col + 0.5) * g.cell, g.oy + (row + 0.5) * g.cell);
    return board->mapToScene(local).toPoint();
}

} // namespace

class TestUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QQuickStyle::setStyle(QStringLiteral("Material"));
        qmlRegisterType<BoardView>("AX25Chess.Board", 1, 0, "BoardView");
        BoardView::loadPieceFont();
    }

    void everyStringIsTranslated()
    {
        const QStringList strings = sourceStrings();
        QVERIFY2(strings.size() > 150, qPrintable(QStringLiteral("only %1 strings found").arg(strings.size())));
        QStringList missing;
        for (const QString &s : strings) {
            if (!Catalog::french().contains(s)) missing << s;
        }
        QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("%1 untranslated:\n  ").arg(missing.size())
                                               + missing.join(QStringLiteral("\n  "))));
        // Placeholders survive the translation.
        for (auto it = Catalog::french().begin(); it != Catalog::french().end(); ++it) {
            for (int n = 1; n <= 9; n++) {
                const QString ph = QStringLiteral("%%1").arg(n);
                QVERIFY2(it.key().contains(ph) == it.value().contains(ph), qPrintable(it.key()));
            }
        }
    }

    // Every control of every page stays inside a phone screen held
    // upright, in French (the longer texts), with the phone-only sections.
    void settingsFitOnAPhone_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("360 dp") << 360;
        QTest::newRow("412 dp") << 412;
    }

    void settingsFitOnAPhone()
    {
        QFETCH(int, width);
        qputenv("AX25CHESS_PLATFORM", "android");
        AppConfig config;
        config.radio.modem.autoStart = false;
        config.radio.modem.ptt = QStringLiteral("cm108");
        config.ui.language = QStringLiteral("fr");
        AppController app(config);
        QQmlApplicationEngine engine;
        app.setRetranslateHook([&engine] { engine.retranslate(); });
        app.applyLanguage();
        engine.rootContext()->setContextProperty(QStringLiteral("app"), &app);
        engine.load(QUrl::fromLocalFile(QStringLiteral(AX25CHESS_QML_DIR "/Main.qml")));
        qunsetenv("AX25CHESS_PLATFORM");
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->setVisibility(QWindow::Windowed);
        window->resize(width, 800);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QStringList overflowing;
        // Every page, the settings above all.
        for (int page = 0; page < 5; page++) {
        window->setProperty("pageIndex", page);
        QTest::qWait(400);
        const qreal limit = window->width() + 0.5;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item->isVisible() || item->opacity() == 0) return;
            const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
            const QString text = item->property("text").toString();
            const bool control = item->inherits("QQuickControl") || item->inherits("QQuickText");
            if (control && r.width() > 0 && (r.right() > limit || r.left() < -0.5)) {
                overflowing << QStringLiteral("%1 \"%2\" x %3..%4")
                                   .arg(QString::fromLatin1(item->metaObject()->className()), text.left(50))
                                   .arg(r.left(), 0, 'f', 0).arg(r.right(), 0, 'f', 0);
                if (qEnvironmentVariableIsSet("AX25CHESS_DEBUG_LAYOUT")) {
                    for (QQuickItem *a = item->parentItem(); a; a = a->parentItem()) {
                        overflowing << QStringLiteral("      in %1 w=%2 implicit=%3")
                                           .arg(QString::fromLatin1(a->metaObject()->className()))
                                           .arg(a->width()).arg(a->implicitWidth());
                    }
                }
            }
            for (QQuickItem *child : item->childItems()) walk(child);
        };
        walk(window->contentItem());
        }
        overflowing.removeDuplicates();
        QVERIFY2(overflowing.isEmpty(), qPrintable(QStringLiteral("window %1 wide:\n  ").arg(window->width())
                                                   + overflowing.join(QStringLiteral("\n  "))));
    }

    void oldTxtailDefaultIsRaised()
    {
        AppConfig old = AppConfig::fromJson({{"modem", QJsonObject{{"txtail", 5}}}});
        QCOMPARE(old.radio.modem.txtail, 10);
        QVERIFY(old.txtailRaised);
        AppConfig chosen = AppConfig::fromJson({{"modem", QJsonObject{{"txtail", 7}}}});
        QCOMPARE(chosen.radio.modem.txtail, 7);
        AppConfig current = AppConfig::fromJson({{"schema", 2}, {"modem", QJsonObject{{"txtail", 5}}}});
        QCOMPARE(current.radio.modem.txtail, 5);
        QVERIFY(!current.txtailRaised);
        QCOMPARE(AppConfig::fromJson(old.toJson()).radio.modem.txtail, 10);
    }

    void playByClicking_data()
    {
        QTest::addColumn<QSize>("size");
        QTest::addColumn<QString>("language");
        QTest::newRow("wide, English") << QSize(1240, 800) << QStringLiteral("en");
        QTest::newRow("phone, French") << QSize(412, 860) << QStringLiteral("fr");
    }

    void playByClicking()
    {
        QFETCH(QSize, size);
        QFETCH(QString, language);
        QTemporaryDir dir;
        AppConfig config;
        config.radio.station.callsign = QStringLiteral("N0CALL");
        config.radio.station.peer = QStringLiteral("N0CALL-2");
        config.radio.modem.autoStart = false;
        config.ui.language = language;
        AppController app(config);
        QQmlApplicationEngine engine;
        app.setRetranslateHook([&engine] { engine.retranslate(); });
        app.applyLanguage();
        QStringList warnings;
        connect(&engine, &QQmlApplicationEngine::warnings, this, [&warnings](const QList<QQmlError> &list) {
            for (const QQmlError &e : list) warnings << e.toString();
        });
        engine.rootContext()->setContextProperty(QStringLiteral("app"), &app);
        engine.load(QUrl::fromLocalFile(QStringLiteral(AX25CHESS_QML_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        window->setVisibility(QWindow::Windowed);
        window->resize(size);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTest::qWait(300);

        // The peer, joined to the application through the encoded frames.
        GameSession peer(QStringLiteral("N0CALL-2"), QStringLiteral("N0CALL"));
        peer.setRandomSeed(7);
        connect(&peer, &GameSession::sendFrame, &peer, [&app](const chs::Frame &f) {
            const QByteArray info = f.encode();
            const QString src = f.src;
            QTimer::singleShot(0, &app, [&app, src, info] { app.receive(src, info); });
        });
        connect(&app, &AppController::frameOut, &peer, [&peer](const QString &src, const QString &, const QByteArray &info) {
            QTimer::singleShot(0, &peer, [&peer, src, info] { peer.feed(info, src, 0); });
        });
        peer.invite(0);
        QTRY_COMPARE(app.gameState(), QStringLiteral("playing"));
        QTRY_VERIFY(peer.state() == GameSession::State::Playing);
        QTRY_VERIFY(!peer.pending());

        auto *board = window->findChild<QQuickItem *>(QStringLiteral("boardView"));
        QVERIFY(board);
        QTRY_VERIFY(board->width() > 100);
        const bool flipped = app.boardFlipped();
        QCOMPARE(flipped, app.myColor() == QStringLiteral("B"));

        if (app.myColor() == QStringLiteral("B")) {
            QVERIFY(peer.playLocal(QStringLiteral("WP4"), chess::squareFromName(QStringLiteral("d4")), 0, 0));
            QTRY_COMPARE(app.currentBoard().plyCount(), 1);
            QTRY_VERIFY(app.myTurn());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, squareCentre(board, "d7", flipped));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, squareCentre(board, "d5", flipped));
            QTRY_COMPARE(peer.board().plyCount(), 2);
        } else {
            QTRY_VERIFY(app.myTurn());
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, squareCentre(board, "e2", flipped));
            QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, squareCentre(board, "e4", flipped));
            QTRY_COMPARE(peer.board().plyCount(), 1);
        }
        QCOMPARE(chs::positionHash(peer.board()), app.positionHash());
        QTRY_COMPARE(app.moves()->property("count").toInt(), peer.board().plyCount());
        // The move was saved.
        QTRY_COMPARE(app.savedCount(), 1);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QStringLiteral("\n"))));

        // AX25CHESS_TEST_SHOTS=<folder>: the window as the test left it, in
        // each theme, for a look at the result.
        const QString shots = qEnvironmentVariable("AX25CHESS_TEST_SHOTS");
        if (!shots.isEmpty()) {
            for (const QString theme : {QStringLiteral("dark"), QStringLiteral("light"), QStringLiteral("red"), QStringLiteral("amber")}) {
                QVariantMap map = app.config();
                QVariantMap ui = map.value(QStringLiteral("ui")).toMap();
                ui.insert(QStringLiteral("theme"), theme);
                map.insert(QStringLiteral("ui"), ui);
                app.saveConfig(map);
                QTest::qWait(400);
                window->grabWindow().save(QDir(shots).filePath(QStringLiteral("%1-%2-%3.png").arg(size.width()).arg(language, theme)));
            }
        }
        app.deleteGame(0);
    }
};

QTEST_MAIN(TestUi)
#include "tst_ui.moc"
