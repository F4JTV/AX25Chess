/*
 * DirewolfEngine.h - Qt face of the embedded Dire Wolf modem core.
 *
 * One instance per process (the core keeps its state in statics).  The
 * object lives in the GUI thread; the core runs on a QThread owned by the
 * engine.  Every signal is emitted from the GUI thread, so slots can touch
 * widgets directly.  The C callbacks arrive on the core's threads and are
 * marshalled here with queued invocations.
 *
 * Lifecycle:
 *
 *   Stopped --start()--> Starting --(core up)--> Running --stop()--> Stopping --> Stopped
 *                            |                      |
 *                      startFailed()           faulted() -> Stopping -> Stopped
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>

class QThread;

/* A frame that passed the FCS or FEC check. */
struct DwReceivedFrame
{
    int chan = 0;
    int subchan = 0;         // demodulator that won, -1 for DTMF
    int slice = 0;           // slicer that won
    QByteArray frame;        // raw AX.25, no FCS
    int audioLevel = 0;      // percent of full scale
    int fec = 0;             // 0 none, 1 FX.25, 2 IL2P
    int retries = 0;         // bits or bytes fixed
    QString spectrum;        // multi-decoder "spectrum" string
};
Q_DECLARE_METATYPE(DwReceivedFrame)

class DirewolfEngine : public QObject
{
    Q_OBJECT

public:
    enum class State { Stopped, Starting, Running, Stopping };
    Q_ENUM(State)

    enum LogLevel { Info = 0, Error, Rec, Decoded, Xmit, Debug };
    Q_ENUM(LogLevel)

    explicit DirewolfEngine(QObject *parent = nullptr);
    ~DirewolfEngine() override;

    State state() const { return m_state; }
    bool isRunning() const { return m_state == State::Running; }
    QString configFile() const { return m_configFile; }

    // Version of the Direwolf sources compiled in, e.g. "1.8.0".
    static QString direwolfVersion();

    // ---- Queries, valid while running -------------------------------------
    int txQueueBytes(int chan) const;      // what the KISS "TXBUF:" query used to answer
    int txQueueFrames(int chan) const;
    bool dcd(int chan) const;              // the modem's own carrier sense, right now
    bool channelIsRadio(int chan) const;
    QString channelMycall(int chan) const; // MYCALL from the configuration file
    QString channelDescription(int chan) const;
    static int maxChannels();

    // "SRC>DST,DIGI*:" for a raw frame, empty if it does not parse.
    static QString formatAddresses(const QByteArray &frame);

public slots:
    // Parse the direwolf.conf, open the sound card, start decoding.
    // Emits started() or startFailed().
    void start(const QString &configFile);

    // Stop the core.  Emits stopped() when its threads have finished.
    void stop();

    // Queue a raw AX.25 frame (no FCS).  The core applies its own
    // p-persistence channel access before keying the transmitter.
    bool transmit(int chan, const QByteArray &frame, bool highPriority = false);

    // Discard everything waiting for the channel.  Returns frames dropped.
    int clearTxQueue(int chan);

    // KISS parameters 1-5.  Pass -1 to leave one unchanged.
    void setChannelParams(int chan, int txdelay, int persist, int slottime,
                          int txtail, int fullDuplex);

signals:
    void stateChanged(DirewolfEngine::State state);
    void started();
    void startFailed(const QString &reason);
    void stopped(bool afterFault);

    void frameReceived(const DwReceivedFrame &frame);
    void dcdChanged(int chan, bool active);
    void pttChanged(int chan, bool active);
    void logMessage(DirewolfEngine::LogLevel level, const QString &line);

    // The core hit a condition stand-alone Direwolf exits on.  stopped(true)
    // follows once its threads are down; start() may then be called again.
    void faulted(int status, const QString &reason);

private:
    class Thread;
    friend class Thread;

    void setState(State state);
    void onThreadStarted();
    void onThreadStartFailed(const QString &reason);
    void onThreadFinished(bool afterFault);

    // C callbacks (core threads) -> queued to the GUI thread
    static void cbLog(void *user, int level, const char *line);
    static void cbFrame(void *user, const void *frame);
    static void cbDcd(void *user, int chan, int active);
    static void cbPtt(void *user, int chan, int active);
    static void cbFault(void *user, int status, const char *reason);

    State m_state = State::Stopped;
    QString m_configFile;
    Thread *m_thread = nullptr;
    bool m_faulted = false;
};
