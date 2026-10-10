/*
 * tst_radio.cpp - the generated modem configuration, the sound card list,
 * channel access (tests taken from AX25Chat, where they pinned the
 * TXDELAY/keying race), and the modem's lifecycle on ALSA's null device.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AudioDevices.h"
#include "AX25Frame.h"
#include "ChannelManager.h"
#include "ModemConfigFile.h"
#include "RadioConfig.h"
#include "RadioLink.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

struct FakeModem
{
    QList<QByteArray> sent;
    bool accept = true;
    int queued = 0;

    ChannelManager::SendFunction sender()
    {
        return [this](const QByteArray &raw) {
            if (!accept) return false;
            sent.append(raw);
            return true;
        };
    }
    ChannelManager::TxQueueFunction txQueue()
    {
        return [this] { return queued; };
    }
};

AX25Frame frameFrom(const QString &source, const char *text)
{
    auto frame = AX25Frame::ui(source, "CHAT", QByteArray(text));
    Q_ASSERT(frame);
    return *frame;
}

RadioConfig quietConfig()
{
    RadioConfig config;
    config.station.callsign = QStringLiteral("F1ABC-7");
    config.channel.randomJitterMs = 0;       // deterministic
    config.channel.rxHoldOffMs = 1500;
    config.channel.interFrameGapMs = 700;
    config.channel.maxDeferSeconds = 120;
    return config;
}

} // namespace

class TestRadio : public QObject
{
    Q_OBJECT

private slots:
    void generatedConfiguration()
    {
        ModemConfig m;
        QString text = modemconf::generatedText(QStringLiteral("n0call-7"), m);
        QVERIFY(text.contains(QStringLiteral("MYCALL   N0CALL-7\n")));
        QVERIFY(text.contains(QStringLiteral("#ADEVICE default")));      // default device: no ADEVICE line
        QVERIFY(!text.contains(QStringLiteral("\nADEVICE")));
        QVERIFY(text.contains(QStringLiteral("MODEM    1200\n")));
        QVERIFY(text.contains(QStringLiteral("TXDELAY  30\n")));
        QVERIFY(text.contains(QStringLiteral("FULLDUP  OFF\n")));
        QVERIFY(text.contains(QStringLiteral("# No PTT line")));
        QVERIFY(!text.contains(QLatin1Char('%')));                       // every placeholder filled

        m.audioIn = QStringLiteral("plughw:CARD=Device,DEV=0");
        m.audioOut = m.audioIn;
        m.ptt = QStringLiteral("rts");
        m.pttDevice = QStringLiteral("/dev/ttyUSB1");
        m.speed = 9600;
        m.txdelay = 40;
        text = modemconf::generatedText(QStringLiteral("N0CALL"), m);
        QVERIFY(text.contains(QStringLiteral("\nADEVICE  plughw:CARD=Device,DEV=0\n")));
        QVERIFY(text.contains(QStringLiteral("PTT      /dev/ttyUSB1 RTS\n")));
        QVERIFY(text.contains(QStringLiteral("MODEM    9600\n")));
        QVERIFY(text.contains(QStringLiteral("TXDELAY  40\n")));

        m.audioOut = QStringLiteral("1");
        m.ptt = QStringLiteral("cm108");
        m.gpio = 4;
        m.pttDevice.clear();
        text = modemconf::generatedText(QStringLiteral("N0CALL"), m);
        QVERIFY(text.contains(QStringLiteral("\nADEVICE  plughw:CARD=Device,DEV=0 1\n")));
        QVERIFY(text.contains(QStringLiteral("PTT      CM108 4\n")));

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sub/direwolf.conf"));
        QCOMPARE(modemconf::writeGenerated(path, QStringLiteral("N0CALL"), m), path);
        QCOMPARE(modemconf::channelsIn(path), QList<int>{0});
    }

    // TXTAIL 50 ms written by 2.0.3 as its default becomes 100 ms; a value
    // the operator chose, or one in a file of the new schema, stays.
    void oldTxtailDefaultIsRaised()
    {
        QCOMPARE(ModemConfig().txtail, 10);
        QCOMPARE(ModemConfig::fromJson({{"txtail", 5}}).txtail, 5);   // the struct itself keeps what it is given
    }

    void configurationCoercesBadValues()
    {
        QJsonObject o{{"speed", 4800}, {"ptt", "gpio"}, {"gpio", 12}, {"sample_rate", 8000}, {"audio_in", ""}};
        const ModemConfig m = ModemConfig::fromJson(o);
        QCOMPARE(m.speed, 1200);
        QCOMPARE(m.ptt, QStringLiteral("none"));
        QCOMPARE(m.gpio, 8);
        QCOMPARE(m.sampleRate, 44100);
        QCOMPARE(m.audioIn, QStringLiteral("default"));
        const StationConfig s = StationConfig::fromJson({{"callsign", " n0call-7 "}, {"path", "wide1-1, wide2-1"}});
        QCOMPARE(s.callsign, QStringLiteral("N0CALL-7"));
        QCOMPARE(s.digipeaters(), (QStringList{"WIDE1-1", "WIDE2-1"}));
    }

    void alsaCardList()
    {
        const QString cards = QStringLiteral(
            " 0 [PCH            ]: HDA-Intel - HDA Intel PCH\n"
            "                      HDA Intel PCH at 0xf7f10000 irq 31\n"
            " 1 [Device         ]: USB-Audio - USB Audio Device\n"
            "                      C-Media Electronics Inc. USB Audio Device at usb-0000:00:14.0-2, full speed\n");
        const QList<AudioDevice> list = audiodevices::parseAsoundCards(cards);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(1).value, QStringLiteral("plughw:CARD=Device,DEV=0"));
        QVERIFY(list.at(1).label.startsWith(QStringLiteral("USB Audio Device")));
        QCOMPARE(audiodevices::inputs().first().value, QStringLiteral("default"));
    }

    // The core started and stopped several times in one process: the
    // restart path of the patch (threads that end, buffers that are freed).
    void modemRestartsOnTheNullDevice()
    {
#ifndef Q_OS_LINUX
        QSKIP("needs ALSA's null device; elsewhere the default sound card would really transmit");
#endif
        if (!QFileInfo::exists(QStringLiteral(TEST_CONF_DIR "/test-null.conf"))) QSKIP("no test configuration");
        RadioConfig config;
        config.station.callsign = QStringLiteral("N0CALL-1");
        config.modem.ownFile = true;
        config.modem.configFile = QStringLiteral(TEST_CONF_DIR "/test-null.conf");
        RadioLink link(config);
        QStringList errors;
        connect(&link, &RadioLink::logMessage, this, [&errors](const QString &level, const QString &text) {
            if (level == QLatin1String("error")) errors << text;
        });
        for (int round = 0; round < 3; round++) {
            link.startModem();
            QTRY_VERIFY_WITH_TIMEOUT(link.modemRunning() || !errors.isEmpty(), 10000);
            if (!errors.isEmpty() && round == 0) QSKIP(qPrintable(QStringLiteral("no ALSA null device here: ") + errors.join(" | ")));
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join(" | ")));
            // A frame goes out through channel access and the modem.
            QSignalSpy sent(&link, &RadioLink::frameSent);
            QVERIFY(link.sendInfo(QStringLiteral("N0CALL-1"), QStringLiteral("N0CALL-2"), {},
                                  QByteArray("CHS1|ABCD|N0CALL-1|N0CALL-2|1|PING|0001|0000"), QStringLiteral("ping")));
            QTRY_VERIFY_WITH_TIMEOUT(sent.count() == 1, 15000);
            link.stopModem();
            QTRY_VERIFY_WITH_TIMEOUT(link.modemState() == DirewolfEngine::State::Stopped, 10000);
        }
    }

    // ---- channel access, from AX25Chat ------------------------------------------------
    void offlineSendsNothing()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });

        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Offline);
        QCOMPARE(modem.sent.size(), 0);
        QCOMPARE(cm.pending(), 1);
    }

    void clearChannelSendsAtOnce()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        QSignalSpy sent(&cm, &ChannelManager::itemSent);
        QSignalSpy complete(&cm, &ChannelManager::txComplete);

        cm.setOnline(true);
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Clear);

        const auto item = cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello", "chat", 1, 2);
        QCOMPARE(item.describe(), QStringLiteral("hello  [1/2]"));
        clock += 100;
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
        QCOMPARE(sent.count(), 1);
        QCOMPARE(cm.state(), ChannelManager::State::Transmitting);
        QCOMPARE(cm.pending(), 0);

        // Handed over but not keyed yet, then keyed, then unkeyed, then
        // the gap, then clear.
        clock += 100;
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Transmitting);
        QVERIFY(cm.busyReason().contains(QStringLiteral("waiting for the transmitter")));
        cm.setPtt(true);
        clock += 100;
        cm.tick();
        QCOMPARE(cm.busyReason(), QStringLiteral("transmitting"));
        cm.setPtt(false);
        clock += 100;
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Transmitting);   // inter-frame gap
        QCOMPARE(cm.busyReason(), QStringLiteral("inter-frame gap"));
        clock += 700;
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Clear);
        QCOMPARE(complete.count(), 1);
    }

    // Two frames queued together must be two keyings, each with its own
    // TXDELAY: the second waits for the transmitter to key and unkey for
    // the first, even when the modem's own queue already looks empty.
    void secondFrameWaitsForTheFirstKeying()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);
        cm.tick();
        cm.enqueueMany({frameFrom("F1ABC-7", "one"), frameFrom("F1ABC-7", "two")}, {"one", "two"}, "chat");
        clock += 100;
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
        // The modem dequeued it at once; no PTT report yet.
        modem.queued = 0;
        for (int i = 0; i < 10; i++) { clock += 100; cm.tick(); }
        QCOMPARE(modem.sent.size(), 1);
        cm.setPtt(true);
        for (int i = 0; i < 20; i++) { clock += 100; cm.tick(); }
        QCOMPARE(modem.sent.size(), 1);
        cm.setPtt(false);
        clock += 100;
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);                 // the gap
        clock += 700;
        cm.tick();
        QCOMPARE(modem.sent.size(), 2);
    }

    // A modem that never reports its PTT (no feedback at all) does not
    // hold the queue forever.
    void noPttFeedbackTimesOut()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);
        cm.tick();
        cm.enqueueMany({frameFrom("F1ABC-7", "one"), frameFrom("F1ABC-7", "two")}, {"one", "two"}, "chat");
        clock += 100;
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
        clock += 5100;
        cm.tick();
        QCOMPARE(modem.sent.size(), 2);
    }

    void carrierDefersUntilQuiet()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);

        cm.setDcd(true);
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::RxBusy);

        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Deferred);
        QCOMPARE(cm.busyReason(), QStringLiteral("carrier detected"));
        QCOMPARE(modem.sent.size(), 0);

        // Carrier drops: the quiet period starts now.
        clock += 3000;
        cm.setDcd(false);
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Deferred);
        QVERIFY(cm.busyReason().startsWith(QStringLiteral("another station heard")));
        clock += 1400;
        cm.tick();
        QCOMPARE(modem.sent.size(), 0);
        clock += 200;
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
    }

    void receivedFrameStartsHoldOff()
    {
        FakeModem modem;
        RadioConfig config = quietConfig();
        ChannelManager cm(config, modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);

        // Our own echo is not activity...
        const AX25Frame ours = frameFrom("F1ABC-7", "echo");
        cm.noteRx(&ours);
        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);

        // ...another station is.
        clock += 5000;
        modem.queued = 0;
        const AX25Frame theirs = frameFrom("F1XYZ", "hi");
        cm.noteRx(&theirs);
        cm.enqueue(frameFrom("F1ABC-7", "again"), "again");
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Deferred);
        clock += 1500;
        cm.tick();
        QCOMPARE(modem.sent.size(), 2);

        // With treatOwnEchoAsBusy, our own echo counts too.
        config.channel.treatOwnEchoAsBusy = true;
        cm.reloadConfig(config);
        clock += 5000;
        cm.noteRx(&ours);
        cm.enqueue(frameFrom("F1ABC-7", "third"), "third");
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Deferred);
    }

    void dcdCanBeIgnored()
    {
        FakeModem modem;
        RadioConfig config = quietConfig();
        config.channel.useDcd = false;
        ChannelManager cm(config, modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);

        cm.setDcd(true);
        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
    }

    void pttCountsAsTransmitting()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);

        cm.setPtt(true);
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Transmitting);
        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Deferred);
        QCOMPARE(cm.busyReason(), QStringLiteral("transmitting"));
        cm.setPtt(false);
        clock += 700;                    // inter-frame gap after PTT release
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
    }

    void jitterBoundsTheRelease()
    {
        FakeModem modem;
        RadioConfig config = quietConfig();
        config.channel.randomJitterMs = 400;
        ChannelManager cm(config, modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);

        cm.setDcd(true);
        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        cm.setDcd(false);
        clock += 1500;                   // hold-off over, jitter starts here
        cm.tick();
        QVERIFY(modem.sent.size() <= 1);
        clock += 401;
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
    }

    void staleItemsAreAbandoned()
    {
        FakeModem modem;
        RadioConfig config = quietConfig();
        config.channel.maxDeferSeconds = 10;
        ChannelManager cm(config, modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        QSignalSpy dropped(&cm, &ChannelManager::itemDropped);
        cm.setOnline(true);

        cm.setDcd(true);
        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        clock += 9000;
        cm.tick();
        QCOMPARE(dropped.count(), 0);
        clock += 2000;
        cm.tick();
        QCOMPARE(dropped.count(), 1);
        QVERIFY(dropped.at(0).at(1).toString().contains(QStringLiteral("10s")));
        QCOMPARE(cm.pending(), 0);
        QCOMPARE(modem.sent.size(), 0);
    }

    void queueOperations()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        QSignalSpy changed(&cm, &ChannelManager::queueChanged);
        QSignalSpy dropped(&cm, &ChannelManager::itemDropped);

        cm.enqueueMany({frameFrom("F1ABC-7", "a"), frameFrom("F1ABC-7", "b")}, {"a", "b"}, "chat");
        cm.enqueue(frameFrom("F1ABC-7", "beacon"), "beacon", "beacon");
        cm.enqueue(frameFrom("F1ABC-7", "pos"), "pos", "position");
        QCOMPARE(cm.pending(), 4);
        QCOMPARE(cm.pendingItems().at(1).describe(), QStringLiteral("b  [2/2]"));

        QCOMPARE(cm.dropPending("beacon"), 1);
        QCOMPARE(cm.dropPending("beacon"), 0);
        QCOMPARE(cm.pending(), 3);
        QCOMPARE(dropped.count(), 0);              // silent

        QCOMPARE(cm.clearQueue(), 3);
        QCOMPARE(dropped.count(), 3);
        QCOMPARE(dropped.at(0).at(1).toString(), QStringLiteral("cancelled by operator"));
        QCOMPARE(cm.pending(), 0);
        QCOMPARE(changed.last().at(0).toInt(), 0);
    }

    void refusedFrameStaysQueued()
    {
        FakeModem modem;
        modem.accept = false;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        QSignalSpy logged(&cm, &ChannelManager::logMessage);
        cm.setOnline(true);

        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        QCOMPARE(cm.pending(), 1);
        QCOMPARE(cm.state(), ChannelManager::State::Deferred);
        QCOMPARE(logged.count(), 1);

        modem.accept = true;
        clock += 100;
        cm.tick();
        QCOMPARE(cm.pending(), 0);
        QCOMPARE(modem.sent.size(), 1);
    }

    void goingOfflineKeepsTheQueue()
    {
        FakeModem modem;
        ChannelManager cm(quietConfig(), modem.sender(), modem.txQueue());
        qint64 clock = 1000;
        cm.setClock([&clock] { return clock; });
        cm.setOnline(true);
        cm.setDcd(true);
        cm.enqueue(frameFrom("F1ABC-7", "hello"), "hello");
        cm.tick();
        cm.setOnline(false);
        QCOMPARE(cm.state(), ChannelManager::State::Offline);
        QCOMPARE(cm.pending(), 1);
        // Back online: the carrier state is forgotten with the modem, the
        // quiet period after the last activity still applies.
        cm.setOnline(true);
        cm.tick();
        QCOMPARE(cm.state(), ChannelManager::State::Deferred);
        QVERIFY(cm.busyReason().startsWith(QStringLiteral("another station heard")));
        clock += 1500;
        cm.tick();
        QCOMPARE(modem.sent.size(), 1);
    }

    void stateLabels()
    {
        QCOMPARE(ChannelManager::stateLabel(ChannelManager::State::Deferred), QStringLiteral("Waiting for clear channel"));
        QCOMPARE(ChannelManager::stateColourKey(ChannelManager::State::Deferred), QStringLiteral("busy"));
        QCOMPARE(ChannelManager::stateColourKey(ChannelManager::State::Offline), QStringLiteral("offline"));
    }
};

QTEST_MAIN(TestRadio)
#include "tst_radio.moc"
