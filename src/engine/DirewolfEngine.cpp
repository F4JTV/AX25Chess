/*
 * DirewolfEngine.cpp - Qt face of the embedded Dire Wolf modem core.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "DirewolfEngine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPointer>
#include <QThread>

#include "dw_embed.h"

namespace {

// dw_embed.h's callbacks are C; the frame pointer is passed through void*
// in the header of this class so that Qt code never includes Direwolf's
// headers.  The two must agree, and they do because both come from
// dw_embed.h.
DirewolfEngine *g_instance = nullptr;

QString fromC(const char *text)
{
    return text ? QString::fromLocal8Bit(text) : QString();
}

} // namespace


// The core runs here: start, dispatch loop, shutdown.  Results are pushed to
// the engine object on the GUI thread with queued invocations, so the engine
// never touches its own state from this thread.

class DirewolfEngine::Thread : public QThread
{
public:
    Thread(DirewolfEngine *engine, const QString &configFile)
        : QThread(engine), m_engine(engine), m_configFile(configFile)
    {
        setObjectName(QStringLiteral("direwolf-core"));
    }

protected:
    void run() override
    {
        dw_embed_callbacks_t cb{};
        cb.user = m_engine;
        cb.log = [](void *u, dw_embed_log_level_t level, const char *line) {
            DirewolfEngine::cbLog(u, static_cast<int>(level), line);
        };
        cb.frame_received = [](void *u, const dw_embed_frame_t *f) {
            DirewolfEngine::cbFrame(u, f);
        };
        cb.dcd_changed = DirewolfEngine::cbDcd;
        cb.ptt_changed = DirewolfEngine::cbPtt;
        cb.fault = DirewolfEngine::cbFault;

        const QByteArray path = m_configFile.toLocal8Bit();
        char err[600];
        if (dw_embed_start(path.constData(), &cb, err, sizeof(err)) != 0) {
            const QString reason = fromC(err);
            QPointer<DirewolfEngine> engine(m_engine);
            QMetaObject::invokeMethod(m_engine, [engine, reason] {
                if (engine) engine->onThreadStartFailed(reason);
            }, Qt::QueuedConnection);
            return;
        }

        {
            QPointer<DirewolfEngine> engine(m_engine);
            QMetaObject::invokeMethod(m_engine, [engine] {
                if (engine) engine->onThreadStarted();
            }, Qt::QueuedConnection);
        }

        const int rc = dw_embed_run();

        QPointer<DirewolfEngine> engine(m_engine);
        const bool afterFault = (rc != 0);
        QMetaObject::invokeMethod(m_engine, [engine, afterFault] {
            if (engine) engine->onThreadFinished(afterFault);
        }, Qt::QueuedConnection);
    }

private:
    DirewolfEngine *m_engine;
    QString m_configFile;
};


// ---- Construction ---------------------------------------------------------

DirewolfEngine::DirewolfEngine(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<DwReceivedFrame>("DwReceivedFrame");
    qRegisterMetaType<DirewolfEngine::State>("DirewolfEngine::State");
    qRegisterMetaType<DirewolfEngine::LogLevel>("DirewolfEngine::LogLevel");

    if (g_instance) {
        qWarning("DirewolfEngine: a second instance was created; the core is a singleton");
    }
    g_instance = this;
}

DirewolfEngine::~DirewolfEngine()
{
    if (m_thread) {
        dw_embed_stop();
        m_thread->wait(5000);
    }
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QString DirewolfEngine::direwolfVersion()
{
    return fromC(dw_embed_direwolf_version());
}


// ---- Lifecycle -------------------------------------------------------------

void DirewolfEngine::setState(State state)
{
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(state);
}

void DirewolfEngine::start(const QString &configFile)
{
    if (m_state != State::Stopped) {
        emit logMessage(Error, tr("Modem core is %1; wait for it to stop before starting it again")
                                   .arg(m_state == State::Stopping ? tr("stopping") : tr("running")));
        return;
    }
    m_configFile = configFile;
    m_faulted = false;
    setState(State::Starting);
    m_thread = new Thread(this, configFile);
    m_thread->start();
}

void DirewolfEngine::stop()
{
    if (m_state == State::Stopped || m_state == State::Stopping) return;
    setState(State::Stopping);
    dw_embed_stop();
}

void DirewolfEngine::onThreadStarted()
{
    if (m_state == State::Stopping) {
        // stop() was called while the core was still starting.  Repeat the
        // request: dw_embed_start() clears the stop flag on entry, so an
        // early stop() may have been wiped out.
        dw_embed_stop();
        return;
    }
    setState(State::Running);
    emit started();
}

void DirewolfEngine::onThreadStartFailed(const QString &reason)
{
    m_thread->wait();
    m_thread->deleteLater();
    m_thread = nullptr;
    setState(State::Stopped);
    emit startFailed(reason);
}

void DirewolfEngine::onThreadFinished(bool afterFault)
{
    m_thread->wait();
    m_thread->deleteLater();
    m_thread = nullptr;
    setState(State::Stopped);
    emit stopped(afterFault || m_faulted);
}


// ---- Transmit ----------------------------------------------------------------

bool DirewolfEngine::transmit(int chan, const QByteArray &frame, bool highPriority)
{
    if (!isRunning()) return false;
    return dw_embed_transmit(chan,
                             reinterpret_cast<const unsigned char *>(frame.constData()),
                             frame.size(), highPriority ? 1 : 0) == 0;
}

int DirewolfEngine::clearTxQueue(int chan)
{
    return dw_embed_tx_queue_clear(chan);
}

void DirewolfEngine::setChannelParams(int chan, int txdelay, int persist, int slottime,
                                      int txtail, int fullDuplex)
{
    dw_embed_set_channel_params(chan, txdelay, persist, slottime, txtail, fullDuplex);
}


// ---- Queries ---------------------------------------------------------------

int DirewolfEngine::txQueueBytes(int chan) const
{
    return dw_embed_tx_queue_bytes(chan);
}

int DirewolfEngine::txQueueFrames(int chan) const
{
    return dw_embed_tx_queue_frames(chan);
}

bool DirewolfEngine::dcd(int chan) const
{
    return dw_embed_dcd(chan) != 0;
}

bool DirewolfEngine::channelIsRadio(int chan) const
{
    return dw_embed_channel_is_radio(chan) != 0;
}

QString DirewolfEngine::channelMycall(int chan) const
{
    return fromC(dw_embed_channel_mycall(chan));
}

QString DirewolfEngine::channelDescription(int chan) const
{
    char buf[100];
    dw_embed_channel_describe(chan, buf, sizeof(buf));
    return fromC(buf);
}

int DirewolfEngine::maxChannels()
{
    return dw_embed_max_channels();
}

QString DirewolfEngine::formatAddresses(const QByteArray &frame)
{
    char buf[200];
    if (dw_embed_format_addrs(reinterpret_cast<const unsigned char *>(frame.constData()),
                              frame.size(), buf, sizeof(buf)) != 0) {
        return QString();
    }
    return fromC(buf);
}


// ---- Callbacks from the core threads ----------------------------------------
//
// Each one copies what it needs and queues a lambda to the GUI thread.  The
// QPointer guards against the engine being destroyed while an event is in
// flight (the destructor waits for the core thread, but log lines can still
// come from a Direwolf thread that is winding down).

void DirewolfEngine::cbLog(void *user, int level, const char *line)
{
    auto *self = static_cast<DirewolfEngine *>(user);
    const QString text = fromC(line);
    const LogLevel lvl = (level >= Info && level <= Debug) ? static_cast<LogLevel>(level) : Info;
    QPointer<DirewolfEngine> engine(self);
    QMetaObject::invokeMethod(self, [engine, lvl, text] {
        if (engine) emit engine->logMessage(lvl, text);
    }, Qt::QueuedConnection);
}

void DirewolfEngine::cbFrame(void *user, const void *frame)
{
    auto *self = static_cast<DirewolfEngine *>(user);
    const auto *f = static_cast<const dw_embed_frame_t *>(frame);

    DwReceivedFrame rx;
    rx.chan = f->chan;
    rx.subchan = f->subchan;
    rx.slice = f->slice;
    rx.frame = QByteArray(reinterpret_cast<const char *>(f->data), f->len);
    rx.audioLevel = f->alevel_rec;
    rx.fec = static_cast<int>(f->fec);
    rx.retries = f->retries;
    rx.spectrum = fromC(f->spectrum);

    QPointer<DirewolfEngine> engine(self);
    QMetaObject::invokeMethod(self, [engine, rx] {
        if (engine) emit engine->frameReceived(rx);
    }, Qt::QueuedConnection);
}

void DirewolfEngine::cbDcd(void *user, int chan, int active)
{
    auto *self = static_cast<DirewolfEngine *>(user);
    QPointer<DirewolfEngine> engine(self);
    QMetaObject::invokeMethod(self, [engine, chan, active] {
        if (engine) emit engine->dcdChanged(chan, active != 0);
    }, Qt::QueuedConnection);
}

void DirewolfEngine::cbPtt(void *user, int chan, int active)
{
    auto *self = static_cast<DirewolfEngine *>(user);
    QPointer<DirewolfEngine> engine(self);
    QMetaObject::invokeMethod(self, [engine, chan, active] {
        if (engine) emit engine->pttChanged(chan, active != 0);
    }, Qt::QueuedConnection);
}

void DirewolfEngine::cbFault(void *user, int status, const char *reason)
{
    auto *self = static_cast<DirewolfEngine *>(user);
    const QString text = fromC(reason);
    QPointer<DirewolfEngine> engine(self);
    QMetaObject::invokeMethod(self, [engine, status, text] {
        if (!engine) return;
        engine->m_faulted = true;
        emit engine->faulted(status, text);
        // The core's loop leaves on its own after a fault; if the fault
        // came from another thread, nudge it.
        dw_embed_stop();
        if (engine->m_state == State::Running || engine->m_state == State::Starting) {
            engine->setState(State::Stopping);
        }
    }, Qt::QueuedConnection);
}
