/*
 * RadioLink.cpp - the embedded modem and the channel access in front of it.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "RadioLink.h"
#include "AX25Frame.h"
#include "ModemConfigFile.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>

RadioLink::RadioLink(const RadioConfig &config, QObject *parent)
    : QObject(parent), m_config(config)
{
    m_modem = new DirewolfEngine(this);
    m_channel = new ChannelManager(config, [this](const QByteArray &raw) {
        return m_modem->transmit(m_config.modem.channel, raw);
    }, [this] { return m_modem->txQueueBytes(m_config.modem.channel); }, this);

    connect(m_modem, &DirewolfEngine::started, this, &RadioLink::onModemStarted);
    connect(m_modem, &DirewolfEngine::startFailed, this, &RadioLink::onModemStartFailed);
    connect(m_modem, &DirewolfEngine::stopped, this, &RadioLink::onModemStopped);
    connect(m_modem, &DirewolfEngine::faulted, this, [this](int, const QString &reason) { m_lastFault = reason; });
    connect(m_modem, &DirewolfEngine::stateChanged, this, [this](DirewolfEngine::State) { emit modemStateChanged(); });
    connect(m_modem, &DirewolfEngine::logMessage, this, [this](DirewolfEngine::LogLevel level, const QString &line) {
        emit modemLog(int(level), line);
    });
    connect(m_modem, &DirewolfEngine::frameReceived, this, &RadioLink::onFrameReceived);
    connect(m_modem, &DirewolfEngine::dcdChanged, this, [this](int chan, bool active) {
        if (chan != m_config.modem.channel) return;
        m_channel->setDcd(active);
        m_dcd = active;
        emit dcdChanged(active);
    });
    connect(m_modem, &DirewolfEngine::pttChanged, this, [this](int chan, bool active) {
        if (chan != m_config.modem.channel) return;
        m_channel->setPtt(active);
        m_ptt = active;
        emit pttChanged(active);
    });
    connect(m_channel, &ChannelManager::stateChanged, this, [this](ChannelManager::State) { emit channelStateChanged(); });
    connect(m_channel, &ChannelManager::queueChanged, this, [this](int) { emit channelStateChanged(); });
    connect(m_channel, &ChannelManager::itemSent, this, [this](const ChannelManager::OutgoingItem &item) {
        emit frameSent(item.frame.toTnc2(QStringLiteral("ascii")));
    });
    connect(m_channel, &ChannelManager::itemDropped, this,
            [this](const ChannelManager::OutgoingItem &item, const QString &reason) {
        emit logMessage(QStringLiteral("error"), tr("Frame not sent (%1): %2").arg(reason, item.text));
    });
    connect(m_channel, &ChannelManager::logMessage, this, &RadioLink::logMessage);
    m_channel->start();
}

RadioLink::~RadioLink()
{
    m_stationOn = false;
    m_channel->stop();
    // The engine's destructor stops the core and waits for its thread.
}

void RadioLink::setConfig(const RadioConfig &config, bool restartModem)
{
    m_config = config;
    m_channel->reloadConfig(config);
    if (restartModem && (m_modem->state() != DirewolfEngine::State::Stopped)) this->restartModem();
}

QString RadioLink::configFilePath() const
{
    if (m_config.modem.ownFile && !m_config.modem.configFile.isEmpty()) return m_config.modem.configFile;
    return modemconf::userPath();
}

bool RadioLink::prepareConfigFile(QString *error)
{
    if (m_config.modem.ownFile) {
        if (m_config.modem.configFile.isEmpty() || !QFileInfo(m_config.modem.configFile).isFile()) {
            if (error) *error = tr("The modem configuration file %1 does not exist.").arg(m_config.modem.configFile);
            return false;
        }
        return true;
    }
    QString why;
    if (modemconf::writeGenerated(modemconf::userPath(), m_config.myCall(), m_config.modem, &why).isEmpty()) {
        if (error) *error = tr("The modem configuration could not be written: %1").arg(why);
        return false;
    }
    return true;
}

void RadioLink::startModem()
{
    m_stationOn = true;
    if (m_modem->state() != DirewolfEngine::State::Stopped) return;
    QString error;
    if (!prepareConfigFile(&error)) {
        emit logMessage(QStringLiteral("error"), error);
        return;
    }
    emit logMessage(QStringLiteral("info"), tr("Starting the modem with %1").arg(QDir::toNativeSeparators(configFilePath())));
    m_modem->start(configFilePath());
}

void RadioLink::stopModem()
{
    m_stationOn = false;
    m_restartWhenStopped = false;
    m_modem->stop();
}

void RadioLink::restartModem()
{
    m_stationOn = true;
    if (m_modem->state() == DirewolfEngine::State::Stopped) {
        startModem();
        return;
    }
    m_restartWhenStopped = true;
    m_modem->stop();
}

bool RadioLink::sendInfo(const QString &source, const QString &destination, const QStringList &via,
                         const QByteArray &info, const QString &label)
{
    QString error;
    const auto frame = AX25Frame::ui(source, destination, info, via, false, &error);
    if (!frame) {
        emit logMessage(QStringLiteral("error"), tr("Frame not built: %1").arg(error));
        return false;
    }
    m_channel->enqueue(*frame, label, QStringLiteral("game"));
    return true;
}

void RadioLink::onModemStarted()
{
    m_startedAtMs = QDateTime::currentMSecsSinceEpoch();
    const int wanted = m_config.modem.channel;
    m_description = m_modem->channelDescription(wanted);
    if (!m_modem->channelIsRadio(wanted)) {
        QStringList available;
        for (int c = 0; c < DirewolfEngine::maxChannels(); c++) {
            if (m_modem->channelIsRadio(c)) available << QString::number(c);
        }
        emit logMessage(QStringLiteral("error"),
                        tr("The modem configuration does not declare channel %1 (it declares %2): nothing can be sent. "
                           "Add a CHANNEL line or change the channel in Settings.")
                            .arg(wanted)
                            .arg(available.isEmpty() ? tr("none") : available.join(QStringLiteral(", "))));
    }
    const QString mycall = m_modem->channelMycall(wanted);
    emit logMessage(QStringLiteral("info"), tr("Modem started: %1 on channel %2%3.")
                                                .arg(m_description).arg(wanted)
                                                .arg(mycall.isEmpty() ? QString() : QStringLiteral(", MYCALL %1").arg(mycall)));
    m_channel->setOnline(true);
    emit modemStateChanged();
}

void RadioLink::onModemStartFailed(const QString &reason)
{
    emit logMessage(QStringLiteral("error"), tr("The modem could not start: %1").arg(reason));
    emit logMessage(QStringLiteral("warn"), tr("Check the sound card and the PTT in Settings -> Modem."));
    emit modemStateChanged();
}

void RadioLink::onModemStopped(bool afterFault)
{
    m_channel->setOnline(false);
    m_ptt = m_dcd = false;
    emit pttChanged(false);
    emit dcdChanged(false);
    if (afterFault) {
        emit logMessage(QStringLiteral("error"), tr("The modem stopped after a fault%1.")
                                                     .arg(m_lastFault.isEmpty() ? QString() : QStringLiteral(": %1").arg(m_lastFault)));
    } else {
        emit logMessage(QStringLiteral("info"), tr("Modem stopped."));
    }
    emit modemStateChanged();
    if (m_restartWhenStopped) {
        m_restartWhenStopped = false;
        QTimer::singleShot(200, this, &RadioLink::startModem);
        return;
    }
    // The station is meant to be on: a fault (an audio device gone for a
    // moment, say) is not the operator's decision to stop.
    if (afterFault && m_stationOn) {
        if (m_startedAtMs && QDateTime::currentMSecsSinceEpoch() - m_startedAtMs > 120000) m_faultRestartDelayS = 5;
        const int delay = m_faultRestartDelayS;
        m_faultRestartDelayS = std::min(60, m_faultRestartDelayS * 2);
        emit logMessage(QStringLiteral("warn"), tr("The station is on: the modem will be started again in %1 s.").arg(delay));
        QTimer::singleShot(delay * 1000, this, [this] { if (m_stationOn) startModem(); });
    }
}

void RadioLink::onFrameReceived(const DwReceivedFrame &received)
{
    if (received.chan != m_config.modem.channel) return;
    QString error;
    const auto decoded = AX25Frame::decode(received.frame, &error);
    if (!decoded) {
        emit logMessage(QStringLiteral("warn"), tr("Undecodable frame (%1), %2 octets").arg(error).arg(received.frame.size()));
        return;
    }
    const AX25Frame &frame = *decoded;
    // The channel was occupied while this frame was being sent.
    m_channel->noteRx(&frame);
    if (!frame.isUi()) return;
    emit infoReceived(frame.source.toString().toUpper(), frame.destination.toString().toUpper(), frame.info,
                      frame.toTnc2(QStringLiteral("ascii")));
}
