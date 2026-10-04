/*
 * RadioLink.h - the station on the air: the embedded modem and the channel
 * access in front of it.
 *
 * Owns the DirewolfEngine (Dire Wolf compiled into the program) and the
 * ChannelManager that releases one frame at a time to it.  The lifecycle
 * follows AX25Chat's session: a modem that stops after a fault while the
 * station is meant to be on is started again (5 s, then 10, 20, up to a
 * minute; a start that held for two minutes resets the delay), and a frame
 * handed to the modem holds the queue until its keying has been reported,
 * so every frame gets its full TXDELAY.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "ChannelManager.h"
#include "DirewolfEngine.h"
#include "RadioConfig.h"

#include <QObject>
#include <QString>
#include <QStringList>

class RadioLink : public QObject
{
    Q_OBJECT

public:
    explicit RadioLink(const RadioConfig &config, QObject *parent = nullptr);
    ~RadioLink() override;

    const RadioConfig &config() const { return m_config; }
    // New settings.  The modem is restarted when it runs and its
    // configuration file changed.
    void setConfig(const RadioConfig &config, bool restartModem);

    // The direwolf.conf the modem loads: the operator's own, or the one
    // generated from the settings (written by prepareConfigFile()).
    QString configFilePath() const;
    // Writes the generated file when it is the one in use.  False, with the
    // reason, when it cannot be written or the operator's file is missing.
    bool prepareConfigFile(QString *error = nullptr);

    // ---- the station ---------------------------------------------------------
    void startModem();
    void stopModem();
    void restartModem();
    bool stationOn() const { return m_stationOn; }
    DirewolfEngine::State modemState() const { return m_modem->state(); }
    bool modemRunning() const { return m_modem->isRunning(); }
    ChannelManager::State channelState() const { return m_channel->state(); }
    QString channelBusyReason() const { return m_channel->busyReason(); }
    int pendingFrames() const { return m_channel->pending(); }
    bool ptt() const { return m_ptt; }
    bool dcd() const { return m_dcd; }
    QString modemDescription() const { return m_description; }

    // Queue an UI frame with PID F0.  False, with the reason in the log,
    // when an address is not a legal AX.25 callsign.
    bool sendInfo(const QString &source, const QString &destination, const QStringList &via,
                  const QByteArray &info, const QString &label);
    int clearQueue() { return m_channel->clearQueue(); }

    ChannelManager *channel() const { return m_channel; }
    DirewolfEngine *engine() const { return m_modem; }

signals:
    // An UI frame for us or not: the application decides.
    void infoReceived(const QString &source, const QString &destination, const QByteArray &info,
                      const QString &monitor);
    // A frame left for the modem (the channel was clear).
    void frameSent(const QString &monitor);
    void modemStateChanged();
    void channelStateChanged();
    void pttChanged(bool on);
    void dcdChanged(bool on);
    // level: info, warn, error; text in the current language.
    void logMessage(const QString &level, const QString &text);
    // Dire Wolf's own console, line by line.
    void modemLog(int level, const QString &line);

private:
    void onModemStarted();
    void onModemStartFailed(const QString &reason);
    void onModemStopped(bool afterFault);
    void onFrameReceived(const DwReceivedFrame &received);

    RadioConfig m_config;
    DirewolfEngine *m_modem = nullptr;
    ChannelManager *m_channel = nullptr;
    bool m_stationOn = false;
    bool m_restartWhenStopped = false;
    bool m_ptt = false;
    bool m_dcd = false;
    qint64 m_startedAtMs = 0;
    int m_faultRestartDelayS = 5;
    QString m_lastFault;
    QString m_description;
};
