/*
 * ChannelManager.h - hold outgoing traffic until the frequency is clear.
 *
 * Taken from AX25Chat, where it ported channel.py.  The requirement: nothing is
 * transmitted while another station is on the air; a message typed during
 * someone else's transmission waits and goes out once the channel falls
 * silent.  It is enforced at two levels.
 *
 * 1. Inside the modem.  Direwolf's xmit.c performs real carrier sense with
 *    p-persistence (PERSIST and SLOTTIME): with full duplex off it will not
 *    key the transmitter while its data carrier detect is asserted.  This
 *    is the layer that actually prevents doubling.
 *
 * 2. Here, the visible deferral.  A KISS host (the Python versions) could
 *    only infer occupancy from frames after they had been demodulated.  With
 *    the modem in-process the manager sees the modem's DCD directly through
 *    setDcd(): a carrier counts as busy the moment it appears, including
 *    noise and collisions that never become a valid frame.  The quiet
 *    period after the last activity (rxHoldOffMs) still applies, because
 *    bursts come in groups: a transmission, its digipeated repeats, an
 *    answering station.  Then a random back-off (randomJitterMs) so two
 *    stations waiting on the same burst do not transmit together, and the
 *    frame goes to the modem.  The modem's own transmit queue is read with
 *    txQueueBytes() instead of the KISS TXBUF poll, so the next frame is
 *    held until the previous one has genuinely left.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "AX25Frame.h"
#include "RadioConfig.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

#include <deque>
#include <functional>

class QTimer;

class ChannelManager : public QObject
{
    Q_OBJECT

public:
    // What the manager currently believes the frequency is doing.
    enum class State {
        Clear,          // quiet, nothing pending
        RxBusy,         // carrier present, or another station heard very recently
        Deferred,       // we have traffic waiting for a clear channel
        Transmitting,
        Offline         // modem not running
    };
    Q_ENUM(State)

    // One frame waiting to be transmitted.
    struct OutgoingItem
    {
        int id = 0;
        AX25Frame frame;
        QString text;
        QString kind = QStringLiteral("chat");   // chat, beacon, position, message, ack
        int part = 1;
        int total = 1;
        qint64 queuedAtMs = 0;

        QString describe() const;
    };

    // Called with the encoded frame; returns true if the modem accepted it.
    using SendFunction = std::function<bool(const QByteArray &)>;
    // Octets still in the modem's transmit queue, or -1 if unknown.
    using TxQueueFunction = std::function<int()>;
    // Monotonic milliseconds; replaceable for tests.
    using Clock = std::function<qint64()>;

    explicit ChannelManager(const RadioConfig &config, SendFunction send,
                            TxQueueFunction txQueue = {}, QObject *parent = nullptr);

    static QString stateLabel(State state);
    // Name of the colour a state should use; the UI resolves it against the
    // desktop palette.
    static QString stateColourKey(State state);

    // ---- lifecycle -----------------------------------------------------------
    void start();
    void stop();
    void setOnline(bool online);            // the modem came up / went down
    void reloadConfig(const RadioConfig &config);
    void setClock(Clock clock);             // tests

    // ---- what the modem reports ---------------------------------------------
    // A frame was demodulated: the channel was occupied a moment ago.  Frames
    // from our own callsign are ignored unless treatOwnEchoAsBusy is set.
    void noteRx(const AX25Frame *frame = nullptr);
    void setDcd(bool active);               // the modem's carrier detect
    void setPtt(bool active);               // our own transmitter keyed

    bool channelBusy() const { return !busyReason().isEmpty(); }
    QString busyReason() const;             // empty when clear
    QString nextItemWaitReason() const { return busyReason(); }

    // ---- queue -------------------------------------------------------------------
    OutgoingItem enqueue(const AX25Frame &frame, const QString &text,
                         const QString &kind = QStringLiteral("chat"), int part = 1, int total = 1);
    QList<OutgoingItem> enqueueMany(const QList<AX25Frame> &frames, const QStringList &texts,
                                    const QString &kind = QStringLiteral("chat"));
    int clearQueue();                       // discard everything, returns count
    int dropPending(const QString &kind);   // silently remove one kind (stale beacons)
    QList<OutgoingItem> pendingItems() const;
    int pending() const { return static_cast<int>(m_queue.size()); }
    State state() const { return m_state; }

    // The manager's clock, monotonic milliseconds, for ageing queue items.
    qint64 clockNow() const { return now(); }

    // One pass of the decision loop.  The timer calls it every 100 ms; tests
    // call it directly.
    void tick();

signals:
    void stateChanged(ChannelManager::State state);
    void queueChanged(int count);
    void itemSent(const ChannelManager::OutgoingItem &item);
    void itemDropped(const ChannelManager::OutgoingItem &item, const QString &reason);
    void txComplete();                      // transmit queue drained
    void logMessage(const QString &level, const QString &message);

private:
    qint64 now() const { return m_clock(); }
    void transmitNext();
    void expireStaleItems(qint64 now);
    void pickJitter();
    void setState(State state);
    bool rxHoldOffActive(qint64 now) const;

    RadioConfig m_config;
    SendFunction m_send;
    TxQueueFunction m_txQueue;
    Clock m_clock;

    std::deque<OutgoingItem> m_queue;
    int m_nextId = 1;
    State m_state = State::Offline;
    bool m_online = false;

    bool m_dcd = false;
    bool m_ptt = false;
    // A frame handed to the modem whose keying has not been reported yet:
    // the next frame waits, or the modem would bundle both in one keying.
    bool m_awaitingPtt = false;
    qint64 m_handedOffMs = 0;
    qint64 m_lastActivityMs = 0;            // last frame or carrier, ms
    qint64 m_lastTxMs = 0;
    qint64 m_clearSinceMs = 0;
    int m_jitterMs = 0;
    bool m_awaitingDrain = false;

    QTimer *m_tickTimer = nullptr;
};

Q_DECLARE_METATYPE(ChannelManager::OutgoingItem)
