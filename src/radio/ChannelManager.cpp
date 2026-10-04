/*
 * ChannelManager.cpp - hold outgoing traffic until the frequency is clear.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ChannelManager.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTimer>

namespace {

// How long a handed-off frame may stay unconfirmed by the transmitter
// before the queue moves on regardless.
constexpr qint64 AwaitPttTimeoutMs = 5000;

constexpr int TickMs = 100;

qint64 monotonicMs()
{
    static QElapsedTimer timer;
    if (!timer.isValid()) timer.start();
    return timer.elapsed();
}

} // namespace


QString ChannelManager::OutgoingItem::describe() const
{
    if (total > 1) return QStringLiteral("%1  [%2/%3]").arg(text).arg(part).arg(total);
    return text;
}

QString ChannelManager::stateLabel(State state)
{
    switch (state) {
        case State::Clear:        return tr("Channel clear");
        case State::RxBusy:       return tr("Channel busy - receiving");
        case State::Deferred:     return tr("Waiting for clear channel");
        case State::Transmitting: return tr("Transmitting");
        case State::Offline:      return tr("Modem offline");
    }
    return QString();
}

QString ChannelManager::stateColourKey(State state)
{
    switch (state) {
        case State::Clear:        return QStringLiteral("clear");
        case State::RxBusy:       return QStringLiteral("busy");
        case State::Deferred:     return QStringLiteral("busy");
        case State::Transmitting: return QStringLiteral("transmitting");
        case State::Offline:      return QStringLiteral("offline");
    }
    return QString();
}


ChannelManager::ChannelManager(const RadioConfig &config, SendFunction send,
                               TxQueueFunction txQueue, QObject *parent)
    : QObject(parent), m_config(config), m_send(std::move(send)), m_txQueue(std::move(txQueue)),
      m_clock(monotonicMs)
{
    qRegisterMetaType<OutgoingItem>("ChannelManager::OutgoingItem");
    qRegisterMetaType<State>("ChannelManager::State");

    m_tickTimer = new QTimer(this);
    m_tickTimer->setInterval(TickMs);
    connect(m_tickTimer, &QTimer::timeout, this, &ChannelManager::tick);
}


// ---- lifecycle ---------------------------------------------------------------

void ChannelManager::start()
{
    m_tickTimer->start();
}

void ChannelManager::stop()
{
    m_tickTimer->stop();
}

void ChannelManager::setOnline(bool online)
{
    m_online = online;
    if (online) {
        m_dcd = false;
        m_ptt = false;
        m_clearSinceMs = now();
        pickJitter();
    } else {
        setState(State::Offline);
    }
}

void ChannelManager::reloadConfig(const RadioConfig &config)
{
    m_config = config;
}

void ChannelManager::setClock(Clock clock)
{
    m_clock = clock ? std::move(clock) : Clock(monotonicMs);
}


// ---- what the modem reports -----------------------------------------------------

void ChannelManager::noteRx(const AX25Frame *frame)
{
    if (frame && !m_config.channel.treatOwnEchoAsBusy) {
        if (frame->source.toString().compare(m_config.myCall(), Qt::CaseInsensitive) == 0) {
            return;     // our own transmission echoed back by a digipeater
        }
    }
    m_lastActivityMs = now();
    m_clearSinceMs = 0;
}

void ChannelManager::setDcd(bool active)
{
    if (!m_config.channel.useDcd) return;
    if (m_dcd == active) return;
    m_dcd = active;
    // Both edges count as activity: the burst is ongoing while the carrier
    // is up, and the quiet period starts when it drops.
    m_lastActivityMs = now();
    m_clearSinceMs = 0;
}

void ChannelManager::setPtt(bool active)
{
    m_ptt = active;
    if (active) {
        m_awaitingPtt = false;
    } else {
        m_lastTxMs = now();
    }
}

bool ChannelManager::rxHoldOffActive(qint64 now) const
{
    const int holdOff = qMax(0, m_config.channel.rxHoldOffMs);
    return m_lastActivityMs && now - m_lastActivityMs < holdOff;
}

QString ChannelManager::busyReason() const
{
    const qint64 t = now();

    if (m_dcd && m_config.channel.useDcd) {
        return tr("carrier detected");
    }

    if (rxHoldOffActive(t)) {
        const int holdOff = qMax(0, m_config.channel.rxHoldOffMs);
        const double remaining = (holdOff - (t - m_lastActivityMs)) / 1000.0;
        return tr("another station heard %1s ago").arg(remaining, 0, 'f', 1);
    }

    if (m_ptt) {
        return tr("transmitting");
    }

    // Between the hand-off and the modem's report that the transmitter is
    // keyed, the frame is neither in the modem's queue nor on the air yet;
    // a second frame handed over now would ride the same keying, with a
    // few flags in place of the TXDELAY.
    if (m_awaitingPtt && t - m_handedOffMs < AwaitPttTimeoutMs) {
        return tr("frame handed to the modem, waiting for the transmitter to key");
    }

    if (m_config.channel.waitForTxbufEmpty && m_txQueue) {
        const int queued = m_txQueue();
        if (queued > 0) {
            return tr("modem still transmitting, %1 octets queued").arg(queued);
        }
    }

    const int gap = qMax(0, m_config.channel.interFrameGapMs);
    if (m_lastTxMs && t - m_lastTxMs < gap) {
        return tr("inter-frame gap");
    }

    return QString();
}


// ---- queue ---------------------------------------------------------------------------

ChannelManager::OutgoingItem ChannelManager::enqueue(const AX25Frame &frame, const QString &text,
                                                     const QString &kind, int part, int total)
{
    OutgoingItem item;
    item.id = m_nextId++;
    item.frame = frame;
    item.text = text;
    item.kind = kind;
    item.part = part;
    item.total = total;
    item.queuedAtMs = now();
    m_queue.push_back(item);
    emit queueChanged(pending());
    return item;
}

QList<ChannelManager::OutgoingItem> ChannelManager::enqueueMany(const QList<AX25Frame> &frames,
                                                                const QStringList &texts,
                                                                const QString &kind)
{
    QList<OutgoingItem> items;
    const int total = static_cast<int>(qMin(frames.size(), texts.size()));
    for (int i = 0; i < total; i++) {
        items.append(enqueue(frames.at(i), texts.at(i), kind, i + 1, total));
    }
    return items;
}

int ChannelManager::clearQueue()
{
    const int count = pending();
    while (!m_queue.empty()) {
        const OutgoingItem item = m_queue.front();
        m_queue.pop_front();
        emit itemDropped(item, tr("cancelled by operator"));
    }
    if (count) emit queueChanged(0);
    return count;
}

int ChannelManager::dropPending(const QString &kind)
{
    if (m_queue.empty()) return 0;
    const auto before = m_queue.size();
    std::deque<OutgoingItem> keep;
    for (const OutgoingItem &item : m_queue) {
        if (item.kind != kind) keep.push_back(item);
    }
    const int removed = static_cast<int>(before - keep.size());
    if (removed) {
        m_queue.swap(keep);
        emit queueChanged(pending());
    }
    return removed;
}

QList<ChannelManager::OutgoingItem> ChannelManager::pendingItems() const
{
    QList<OutgoingItem> items;
    for (const OutgoingItem &item : m_queue) items.append(item);
    return items;
}


// ---- decision loop -------------------------------------------------------------------

void ChannelManager::tick()
{
    if (!m_online) {
        setState(State::Offline);
        return;
    }

    const qint64 t = now();
    expireStaleItems(t);

    const QString busy = busyReason();

    if (busy.isEmpty()) {
        if (!m_clearSinceMs) {
            // The channel has just gone quiet: arm the random back-off so
            // that two stations waiting on the same burst do not collide.
            m_clearSinceMs = t;
            pickJitter();
        }
    } else {
        m_clearSinceMs = 0;
    }

    if (m_queue.empty()) {
        if (!busy.isEmpty() && (m_dcd || rxHoldOffActive(t))) {
            setState(State::RxBusy);
        } else if (!busy.isEmpty()) {
            setState(State::Transmitting);
        } else {
            setState(State::Clear);
            if (m_awaitingDrain) {
                m_awaitingDrain = false;
                emit txComplete();
            }
        }
        return;
    }

    if (!busy.isEmpty()) {
        setState(State::Deferred);
        return;
    }

    if (t - m_clearSinceMs < m_jitterMs) {
        setState(State::Deferred);
        return;
    }

    transmitNext();
}

void ChannelManager::transmitNext()
{
    // A modem that never reported the keying (no PTT feedback at all):
    // do not hold the queue forever.
    if (m_awaitingPtt && now() - m_handedOffMs >= AwaitPttTimeoutMs) m_awaitingPtt = false;
    const OutgoingItem item = m_queue.front();
    const QByteArray raw = item.frame.encode();

    if (raw.size() < ax25::MinAddresses * 7 + 1) {
        m_queue.pop_front();
        emit queueChanged(pending());
        emit itemDropped(item, tr("encoding error: frame too short"));
        return;
    }

    if (!m_send || !m_send(raw)) {
        emit logMessage(QStringLiteral("error"), tr("Modem refused the frame, keeping it queued"));
        setState(State::Deferred);
        return;
    }

    m_queue.pop_front();
    m_lastTxMs = now();
    m_handedOffMs = m_lastTxMs;
    m_awaitingPtt = true;
    m_clearSinceMs = 0;
    m_awaitingDrain = true;
    setState(State::Transmitting);
    emit queueChanged(pending());
    emit itemSent(item);
}

void ChannelManager::expireStaleItems(qint64 now)
{
    const int limit = m_config.channel.maxDeferSeconds;
    if (limit <= 0) return;
    const qint64 deadline = static_cast<qint64>(limit) * 1000;
    while (!m_queue.empty() && now - m_queue.front().queuedAtMs > deadline) {
        const OutgoingItem item = m_queue.front();
        m_queue.pop_front();
        emit itemDropped(item, tr("channel still busy after %1s, message abandoned").arg(limit));
        emit queueChanged(pending());
    }
}

void ChannelManager::pickJitter()
{
    const int span = qMax(0, m_config.channel.randomJitterMs);
    m_jitterMs = span ? static_cast<int>(QRandomGenerator::global()->bounded(span + 1)) : 0;
}

void ChannelManager::setState(State state)
{
    if (state != m_state) {
        m_state = state;
        emit stateChanged(state);
    }
}
