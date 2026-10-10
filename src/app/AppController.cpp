/*
 * AppController.cpp - what the QML interface sees.
 *
 * The game logic follows main_window.py of the Python version: one session
 * at a time, created by an invitation or by the first frame heard, saved
 * after every half-move, removed from the store when the game ends.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "AppController.h"
#include "AndroidNotify.h"
#include "AndroidUi.h"
#include "AudioDevices.h"
#include "Catalog.h"
#include "ModemConfigFile.h"
#include "RadioLink.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

using chess::Board;
using chess::Move;

namespace {

QString levelOf(GameSession::Level level)
{
    switch (level) {
    case GameSession::Level::Info: return QStringLiteral("info");
    case GameSession::Level::Warn: return QStringLiteral("warn");
    case GameSession::Level::Error: return QStringLiteral("error");
    case GameSession::Level::Tx: return QStringLiteral("tx");
    case GameSession::Level::Rx: return QStringLiteral("rx");
    }
    return QStringLiteral("info");
}

int pieceValue(char kind)
{
    switch (kind) {
    case chess::Pawn: return 1;
    case chess::Knight:
    case chess::Bishop: return 3;
    case chess::Rook: return 5;
    case chess::Queen: return 9;
    default: return 0;
    }
}

} // namespace

AppController::AppController(const AppConfig &config, QObject *parent)
    : QObject(parent), m_config(config)
{
    m_clock.start();
    m_radio = new RadioLink(m_config.radio, this);
    connect(m_radio, &RadioLink::infoReceived, this, &AppController::onInfoReceived);
    connect(m_radio, &RadioLink::logMessage, this, &AppController::log);
    connect(m_radio, &RadioLink::modemLog, this, [this](int level, const QString &lineIn) {
        const QString line = lineIn.trimmed();
        if (line.isEmpty()) return;
        // 0 info, 1 error, 2 received, 3 decoded, 4 transmitted, 5 debug.
        static const char *names[] = {"info", "error", "rx", "info", "tx", "debug"};
        if (level == 5) return;
        // The console too (a terminal on the desktop, logcat on Android):
        // what the modem said last survives a crash of the program.
        qInfo().noquote() << "modem:" << line;
        if (level != 1 && !m_config.radio.modem.echoLog) return;
        m_modemLog.append(QString::fromLatin1(names[qBound(0, level, 5)]), line);
    });
    connect(m_radio, &RadioLink::modemStateChanged, this, &AppController::stationChanged);
    connect(m_radio, &RadioLink::channelStateChanged, this, &AppController::stationChanged);
    connect(m_radio, &RadioLink::pttChanged, this, &AppController::stationChanged);
    connect(m_radio, &RadioLink::dcdChanged, this, &AppController::stationChanged);

    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &AppController::tick);
    m_tick->start();

#ifndef Q_OS_ANDROID
    // The games of the Python version, once.
    const int imported = m_store.importLegacy(QDir::home().filePath(QStringLiteral(".ax25chess/parties")));
    if (imported > 0) log(QStringLiteral("info"), tr("%1 game(s) of the previous version imported").arg(imported));
#endif
    refreshGames();
}

AppController::~AppController()
{
    m_tick->stop();
    if (m_session) m_session->disconnect(this);
}

void AppController::setOverrides(bool noModem, const QString &modemConf)
{
    m_noModem = noModem;
    m_modemConfOverride = modemConf;
    m_radio->setConfig(effectiveRadio(), false);
}

RadioConfig AppController::effectiveRadio() const
{
    RadioConfig r = m_config.radio;
    if (!m_modemConfOverride.isEmpty()) {
        r.modem.ownFile = true;
        r.modem.configFile = m_modemConfOverride;
    }
    return r;
}

void AppController::startup()
{
    if (m_startupDone) return;
    m_startupDone = true;
    if (m_games.count() > 0) {
        const int waiting = m_games.waiting();
        log(QStringLiteral("info"), waiting > 0
            ? tr("%1 saved game(s), %2 waiting for your move: see Saved games").arg(m_games.count()).arg(waiting)
            : tr("%1 saved game(s): see Saved games").arg(m_games.count()));
    }
    if (!m_config.configured) {
        log(QStringLiteral("warn"), tr("First run: enter your callsign and your correspondent's in Settings, "
                                       "then the sound card and PTT of the modem."));
    }
    if (m_config.txtailRaised) {
        log(QStringLiteral("warn"), tr("TXTAIL raised from 50 to 100 ms, the new default: with 50 ms a phone cut the end of "
                                       "its frames. Saving the settings keeps it."));
    }
    if (m_config.radio.modem.autoStart && !m_noModem) startStation();
}

QString AppController::version() const
{
    return QStringLiteral(AX25CHESS_VERSION);
}

QString AppController::platform() const
{
    // Tests and screenshots: the interface as another platform lays it out.
    const QString forced = qEnvironmentVariable("AX25CHESS_PLATFORM");
    if (!forced.isEmpty()) return forced;
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

QString AppController::language() const
{
    if (!m_config.ui.language.isEmpty()) return m_config.ui.language;
    return QLocale::system().language() == QLocale::French ? QStringLiteral("fr") : QStringLiteral("en");
}

void AppController::applyLanguage()
{
    if (language() == QLatin1String("fr")) {
        if (!m_translator) {
            m_translator = new Catalog(this);
            QCoreApplication::installTranslator(m_translator);
        }
    } else if (m_translator) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
    if (m_retranslate) m_retranslate();
    m_moves.retranslate();
    refreshGames();
    emit configChanged();
    emit stationChanged();
    emit gameChanged();
}

QString AppController::monoFamily() const
{
    // "monospace" is a fontconfig alias: it resolves on Linux only, and
    // Windows would fall back to a proportional font.
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

void AppController::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

bool AppController::flatRendering() const
{
    // Material's drop shadows need a hardware renderer; the software one
    // behind headless tests and screenshots cannot draw them.
    return qEnvironmentVariable("QT_QUICK_BACKEND") == QLatin1String("software");
}

// =====================================================================
//  Station
// =====================================================================

QString AppController::modemState() const
{
    switch (m_radio->modemState()) {
    case DirewolfEngine::State::Starting: return QStringLiteral("starting");
    case DirewolfEngine::State::Running: return QStringLiteral("running");
    case DirewolfEngine::State::Stopping: return QStringLiteral("stopping");
    case DirewolfEngine::State::Stopped: break;
    }
    return QStringLiteral("stopped");
}

bool AppController::stationOn() const { return m_radio->stationOn(); }
bool AppController::ptt() const { return m_radio->ptt(); }
bool AppController::dcd() const { return m_radio->dcd(); }
QString AppController::modemDescription() const { return m_radio->modemDescription(); }

QString AppController::channelState() const
{
    switch (m_radio->channelState()) {
    case ChannelManager::State::Clear: return QStringLiteral("clear");
    case ChannelManager::State::RxBusy: return QStringLiteral("busy");
    case ChannelManager::State::Deferred: return QStringLiteral("deferred");
    case ChannelManager::State::Transmitting: return QStringLiteral("transmitting");
    case ChannelManager::State::Offline: break;
    }
    return QStringLiteral("offline");
}

QString AppController::channelText() const
{
    QString text = ChannelManager::stateLabel(m_radio->channelState());
    const int pending = m_radio->pendingFrames();
    if (pending > 0) text += QStringLiteral(" - ") + tr("%1 frame(s) waiting").arg(pending);
    return text;
}

void AppController::startStation()
{
    m_radio->startModem();
    androidui::keepScreenOn(m_config.ui.keepScreenOn);
    if (m_config.ui.keepRunning) androidui::startKeepAlive();
    emit stationChanged();
}

void AppController::stopStation()
{
    m_radio->stopModem();
    androidui::stopKeepAlive();
    emit stationChanged();
}

QVariantList AppController::audioInputs() const
{
    QVariantList out;
    for (const AudioDevice &d : audiodevices::inputs()) out << QVariantMap{{"value", d.value}, {"label", d.label}};
    return out;
}

QVariantList AppController::audioOutputs() const
{
    QVariantList out;
    for (const AudioDevice &d : audiodevices::outputs()) out << QVariantMap{{"value", d.value}, {"label", d.label}};
    return out;
}

QString AppController::configFilePath() const
{
    return QDir::toNativeSeparators(m_radio->configFilePath());
}

// =====================================================================
//  Settings
// =====================================================================

QString AppController::saveConfig(const QVariantMap &map)
{
    AppConfig next = AppConfig::fromMap(map);
    next.ui.windowWidth = m_config.ui.windowWidth;
    next.ui.windowHeight = m_config.ui.windowHeight;
    QString error;
    if (!next.save(QString(), &error)) return tr("The settings could not be saved: %1").arg(error);
    next.configured = true;

    const bool modemChanged = next.radio.modem.toJson() != m_config.radio.modem.toJson()
                              || next.radio.myCall() != m_config.radio.myCall();
    const bool languageChanged = next.ui.language != m_config.ui.language;
    m_config = next;
    m_radio->setConfig(effectiveRadio(), modemChanged);
    if (modemChanged && m_radio->modemState() != DirewolfEngine::State::Stopped) {
        log(QStringLiteral("info"), tr("Modem settings changed: restarting the modem"));
    }
    androidui::keepScreenOn(m_config.ui.keepScreenOn);
    if (m_radio->stationOn()) {
        if (m_config.ui.keepRunning) androidui::startKeepAlive();
        else androidui::stopKeepAlive();
    }
    if (languageChanged) applyLanguage();
    emit configChanged();
    emit gameChanged();
    return QString();
}

QString AppController::generatedConfigText(const QVariantMap &map) const
{
    const AppConfig c = AppConfig::fromMap(map);
    return modemconf::generatedText(c.radio.myCall(), c.radio.modem);
}

QString AppController::readConfigFile(const QString &path) const
{
    QFile file(QDir::fromNativeSeparators(path));
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}

QString AppController::writeConfigFile(const QString &path, const QString &text)
{
    QSaveFile file(QDir::fromNativeSeparators(path));
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    file.write(text.toUtf8());
    if (!file.commit()) return file.errorString();
    log(QStringLiteral("info"), tr("Modem configuration written: %1").arg(path));
    if (m_radio->modemState() != DirewolfEngine::State::Stopped
        && QDir::fromNativeSeparators(path) == QDir::fromNativeSeparators(m_radio->configFilePath())) {
        m_radio->restartModem();
    }
    return QString();
}

QString AppController::createStarterConfig()
{
    QString error;
    const QString path = modemconf::writeStarter(QDir(RadioConfig::configDir()).filePath(QStringLiteral("my-direwolf.conf")),
                                                 m_config.radio.myCall(), &error);
    if (path.isEmpty()) {
        log(QStringLiteral("error"), tr("The starter configuration could not be written: %1").arg(error));
        return QString();
    }
    return QDir::toNativeSeparators(path);
}

QString AppController::backgroundStatus() const
{
    QString text = androidui::keepAliveStatus();
    if (!androidui::ignoringBatteryOptimizations()) {
        text += QStringLiteral(" - ") + tr("battery optimisation is on: Android may cut the station off screen");
    }
    return text;
}

void AppController::requestBatteryExemption()
{
    androidui::requestIgnoreBatteryOptimizations();
}

// =====================================================================
//  Game
// =====================================================================

const Board &AppController::currentBoard() const
{
    return m_session ? m_session->board() : m_emptyBoard;
}

GameSession *AppController::ensureSession()
{
    const QString call = m_config.radio.myCall();
    const QString peer = m_config.radio.station.peer.trimmed().toUpper();
    if (m_session) {
        // A game in progress keeps its session whatever the settings say;
        // an idle or finished one follows the settings.
        const bool busy = m_session->state() == GameSession::State::Playing
                          || m_session->state() == GameSession::State::Handshake;
        if (busy || (m_session->myCall() == call && m_session->peerCall() == peer)) return m_session;
    }
    setSession(new GameSession(call, peer, this));
    return m_session;
}

void AppController::setSession(GameSession *session)
{
    if (m_session) {
        m_session->disconnect(this);
        m_session->deleteLater();
    }
    m_session = session;
    m_lastFrom = m_lastTo = -1;
    m_flipFollowsColour = true;
    wireSession(session);
    rebuildMoveList();
    touchBoard();
    emit gameChanged();
}

void AppController::wireSession(GameSession *s)
{
    s->setRetrySeconds(m_config.game.retrySeconds);
    connect(s, &GameSession::sendFrame, this, [this](const chs::Frame &f) {
        const bool on = m_radio->modemRunning();
        emit frameOut(f.src, f.dst, f.encode());
        m_radio->sendInfo(f.src, f.dst, m_config.radio.station.digipeaters(), f.encode(),
                          QStringLiteral("%1 seq=%2").arg(f.type).arg(f.seq));
        log(on ? QStringLiteral("tx") : QStringLiteral("error"),
            on ? QStringLiteral("> %1").arg(f.text())
               : tr("> %1  (the modem is off: queued until it runs)").arg(f.text()));
    });
    connect(s, &GameSession::logMessage, this, [this](GameSession::Level level, const QString &text) {
        log(levelOf(level), text);
    });
    connect(s, &GameSession::stateChanged, this, [this] {
        if (m_flipFollowsColour) {
            const bool flip = m_session && m_session->myColor() == chess::Black;
            if (flip != m_flipped) {
                m_flipped = flip;
                emit boardFlippedChanged();
            }
        }
        emit gameChanged();
    });
    connect(s, &GameSession::moveApplied, this, &AppController::onMoveApplied);
    connect(s, &GameSession::historyReset, this, [this] {
        rebuildMoveList();
        touchBoard();
        autosave();
    });
    connect(s, &GameSession::chatReceived, this, [this](const QString &who, const QString &text) {
        const bool mine = m_session && who.compare(m_session->myCall(), Qt::CaseInsensitive) == 0;
        m_chat.append(mine ? QStringLiteral("me") : QStringLiteral("peer"), QStringLiteral("%1: %2").arg(who, text));
        if (!mine) {
            if (!m_unreadChat) {
                m_unreadChat = true;
                emit unreadChatChanged();
            }
            notifyInBackground(tr("Message from %1").arg(who), text);
        }
    });
    connect(s, &GameSession::gameOver, this, [this](const QString &, const QString &text) {
        log(QStringLiteral("info"), tr("Game over: %1").arg(text));
        if (m_session) m_store.remove(m_session->gid(), m_session->peerCall());
        refreshGames();
        notifyInBackground(tr("Game over"), text);
        // Shown once the frame that ended the game has been handled.
        QTimer::singleShot(0, this, [this, text] { emit announce(tr("Game over"), text); });
    });
    connect(s, &GameSession::drawOffered, this, [this] {
        const QString peer = m_session ? m_session->peerCall() : QString();
        notifyInBackground(tr("Draw offer"), tr("%1 offers a draw").arg(peer));
        QTimer::singleShot(0, this, [this, peer] { emit drawOfferReceived(peer); });
    });
}

void AppController::log(const QString &level, const QString &text)
{
    m_frames.append(level, text);
}

void AppController::onInfoReceived(const QString &source, const QString &destination, const QByteArray &info,
                                   const QString &monitor)
{
    Q_UNUSED(destination);
    Q_UNUSED(monitor);
    // Only CHS-1 frames concern the game; other traffic on the frequency
    // is the modem log's business.
    if (!info.startsWith("CHS1|")) return;
    ensureSession()->feed(info, source, now());
}

MoveListModel::Row AppController::rowFor(const Board &after, const Move &m, int ply, bool byPeer) const
{
    MoveListModel::Row row;
    row.number = (ply + 1) / 2;
    row.white = ply % 2 == 1;
    row.san = m.san;
    row.uid = after.uidOf(m);
    row.square = QStringLiteral("%1 (%2)").arg(chess::squareNumber(m.to)).arg(chess::squareName(m.to));
    row.byPeer = byPeer;
    return row;
}

void AppController::onMoveApplied(const Move &move, bool byPeer)
{
    if (!m_session) return;
    const Board &b = m_session->board();
    m_moves.append(rowFor(b, move, b.plyCount(), byPeer));
    m_lastFrom = move.from;
    m_lastTo = move.to;
    touchBoard();
    autosave();
    if (byPeer) notifyInBackground(tr("Move from %1").arg(m_session->peerCall()), move.san);
}

void AppController::rebuildMoveList()
{
    QList<MoveListModel::Row> rows;
    if (m_session) {
        const Board &b = m_session->board();
        const QList<Move> &moves = b.moves();
        for (int i = 0; i < moves.size(); i++) {
            const bool white = i % 2 == 0;
            const bool byPeer = m_session->myColor() != 0 && (white ? chess::White : chess::Black) != m_session->myColor();
            rows << rowFor(b, moves.at(i), i + 1, byPeer);
        }
        if (!moves.isEmpty()) {
            m_lastFrom = moves.last().from;
            m_lastTo = moves.last().to;
        } else {
            m_lastFrom = m_lastTo = -1;
        }
    }
    m_moves.setRows(rows);
}

void AppController::touchBoard()
{
    m_boardRevision++;
    emit boardChanged();
}

void AppController::autosave()
{
    if (!m_session || m_session->state() != GameSession::State::Playing) return;
    SavedGame g;
    g.gid = m_session->gid();
    g.myCall = m_session->myCall();
    g.peerCall = m_session->peerCall();
    g.color = m_session->myColor() == chess::Black ? QStringLiteral("B") : QStringLiteral("W");
    for (const Move &m : m_session->board().moves()) g.moves << chs::compactMove(m_session->board(), m);
    g.nonce = m_session->myNonce();
    g.peerNonce = m_session->peerNonce();
    g.seq = m_session->seq();
    if (m_store.save(g).isEmpty()) log(QStringLiteral("error"), tr("The game could not be saved in %1").arg(m_store.directory()));
    refreshGames();
}

void AppController::tick()
{
    if (m_session) {
        m_session->setRetrySeconds(m_config.game.retrySeconds);
        m_session->tick(now());
    }
    QString ack;
    if (m_session && m_session->pending()) {
        const GameSession::Outbound *ob = m_session->pending();
        ack = tr("waiting for ACK %1 seq=%2 (%3/%4)").arg(ob->frame.type).arg(ob->frame.seq).arg(ob->attempts).arg(chs::MaxAttempts);
    }
    if (ack != m_ackText) {
        m_ackText = ack;
        emit ackTextChanged();
    }
    // Busy reasons change inside a channel state (hold-off, jitter).
    emit stationChanged();
}

void AppController::notifyInBackground(const QString &title, const QString &text)
{
    emit attention();
    if (QGuiApplication::applicationState() != Qt::ApplicationActive) androidnotify::show(title, text);
}

QString AppController::gameState() const
{
    if (!m_session) return QStringLiteral("idle");
    switch (m_session->state()) {
    case GameSession::State::Handshake: return QStringLiteral("handshake");
    case GameSession::State::Playing: return QStringLiteral("playing");
    case GameSession::State::Over: return QStringLiteral("over");
    case GameSession::State::Idle: break;
    }
    return QStringLiteral("idle");
}

QString AppController::gid() const { return m_session && m_session->state() != GameSession::State::Idle ? m_session->gid() : QString(); }
QString AppController::peer() const { return m_session ? m_session->peerCall() : m_config.radio.station.peer; }
QString AppController::myCall() const { return m_session ? m_session->myCall() : m_config.radio.myCall(); }

QString AppController::myColor() const
{
    if (!m_session || !m_session->myColor()) return QString();
    return QString(QChar(m_session->myColor()));
}

QString AppController::turnColor() const
{
    return QString(QChar(currentBoard().turn()));
}

bool AppController::myTurn() const
{
    return m_session && m_session->myTurn();
}

bool AppController::drawOfferPending() const
{
    return m_session && m_session->drawOfferedByPeer();
}

QString AppController::situation() const
{
    if (!m_session) return tr("No game");
    switch (m_session->state()) {
    case GameSession::State::Idle: return tr("No game");
    case GameSession::State::Handshake: return tr("Negotiating colours...");
    case GameSession::State::Over: return m_session->result();
    case GameSession::State::Playing: break;
    }
    return GameSession::statusText(m_session->board().status(), m_session->board().turn());
}

QString AppController::hint() const
{
    if (!m_session || m_session->state() == GameSession::State::Idle) {
        return tr("Set up the station, then send an invitation.");
    }
    switch (m_session->state()) {
    case GameSession::State::Handshake: return tr("Invitation sent, waiting for a reply.");
    case GameSession::State::Over: return m_session->result();
    default: break;
    }
    if (m_session->pending() && m_session->board().turn() != m_session->myColor()) {
        return tr("Move sent, waiting for the radio acknowledgement.");
    }
    if (m_session->myTurn()) return tr("Your move: tap a piece, then its square.");
    if (m_session->board().turn() == m_session->myColor()) return tr("Waiting for the acknowledgement before your move.");
    return tr("Waiting for your correspondent's move.");
}

QString AppController::turnText() const
{
    if (!m_session) return tr("no game");
    const Board &b = m_session->board();
    switch (m_session->state()) {
    case GameSession::State::Playing:
        return tr("%1 to move | move %2 | %3 half-moves")
            .arg(b.turn() == chess::White ? tr("White") : tr("Black"))
            .arg(b.fullmoveNumber())
            .arg(b.plyCount());
    case GameSession::State::Handshake: return tr("negotiating colours...");
    case GameSession::State::Over: return m_session->result();
    case GameSession::State::Idle: break;
    }
    return tr("no game");
}

QString AppController::positionHash() const
{
    return chs::positionHash(currentBoard());
}

QString AppController::invite()
{
    const QString peer = m_config.radio.station.peer.trimmed().toUpper();
    if (peer.isEmpty()) return tr("Enter your correspondent's callsign in Settings.");
    if (peer == m_config.radio.myCall()) return tr("Your correspondent cannot be your own callsign.");
    if (!m_radio->modemRunning()) return tr("Start the station first.");
    auto *s = new GameSession(m_config.radio.myCall(), peer, this);
    setSession(s);
    s->invite(now());
    return QString();
}

void AppController::resync()
{
    if (m_session) m_session->requestSync(now());
}

void AppController::offerDraw()
{
    if (m_session) m_session->offerDraw(now());
}

void AppController::answerDraw(bool accept)
{
    if (m_session) m_session->answerDraw(accept, now());
}

void AppController::resign()
{
    if (m_session) m_session->resign(now());
}

void AppController::sendChat(const QString &textIn)
{
    const QString text = textIn.trimmed();
    if (text.isEmpty()) return;
    if (m_config.radio.station.peer.trimmed().isEmpty() && !m_session) return;
    ensureSession()->sendChat(text, now());
}

void AppController::ping()
{
    ensureSession()->ping(now());
}

bool AppController::playMove(const QString &uid, int square, const QString &promo)
{
    if (!m_session) return false;
    const char p = promo.isEmpty() ? 0 : char(promo.at(0).toUpper().toLatin1());
    return m_session->playLocal(uid, square, p, now());
}

void AppController::flipBoard()
{
    m_flipFollowsColour = false;
    m_flipped = !m_flipped;
    emit boardFlippedChanged();
}

void AppController::markChatRead()
{
    if (!m_unreadChat) return;
    m_unreadChat = false;
    emit unreadChatChanged();
}

QString AppController::pieceGlyph(const QString &kind) const
{
    static const QHash<QString, char32_t> glyphs{{"K", 0x265A}, {"Q", 0x265B}, {"R", 0x265C},
                                                 {"B", 0x265D}, {"N", 0x265E}, {"P", 0x265F}};
    const char32_t c = glyphs.value(kind.toUpper(), 0);
    return c ? QString::fromUcs4(&c, 1) : QString();
}

QVariantList AppController::captured(const QString &color) const
{
    QVariantList out;
    const char c = color == QLatin1String("B") ? chess::Black : chess::White;
    for (const chess::Piece *p : currentBoard().captured(c)) {
        out << QVariantMap{{"uid", p->uid}, {"glyph", pieceGlyph(QString(QChar(p->kind)))}};
    }
    return out;
}

int AppController::materialEdge(const QString &color) const
{
    const char c = color == QLatin1String("B") ? chess::Black : chess::White;
    int mine = 0, theirs = 0;
    for (const chess::Piece *p : currentBoard().captured(c)) mine += pieceValue(p->kind);
    for (const chess::Piece *p : currentBoard().captured(chess::opponent(c))) theirs += pieceValue(p->kind);
    return mine - theirs;
}

// =====================================================================
//  Saved games
// =====================================================================

void AppController::refreshGames()
{
    m_games.setGames(m_store.list(), m_session ? m_session->gid() : QString());
    emit gamesChanged();
}

bool AppController::resumeNeedsConfirmation(int row) const
{
    const SavedGame *g = m_games.game(row);
    return g && m_session && m_session->state() == GameSession::State::Playing && m_session->gid() != g->gid;
}

QString AppController::resumeGame(int row)
{
    const SavedGame *found = m_games.game(row);
    if (!found) return tr("No such game.");
    const SavedGame saved = *found;
    Board board;
    QString bad;
    if (!chs::replay(saved.moves, &board, &bad)) {
        log(QStringLiteral("error"), tr("Game %1: inconsistent history at move %2, not resumed").arg(saved.gid, bad));
        return tr("The history of game %1 is inconsistent and cannot be replayed. You can delete it from the list.")
            .arg(saved.gid);
    }
    auto *s = new GameSession(saved.myCall, saved.peerCall, this);
    setSession(s);
    s->restore(saved.gid, saved.color == QLatin1String("B") ? chess::Black : chess::White, saved.nonce,
               saved.peerNonce, saved.seq, board);

    // The station follows the game it plays.
    if (m_config.radio.station.peer != saved.peerCall || m_config.radio.myCall() != saved.myCall.toUpper()) {
        AppConfig next = m_config;
        next.radio.station.peer = saved.peerCall;
        if (!saved.myCall.isEmpty()) next.radio.station.callsign = saved.myCall.toUpper();
        saveConfig(next.toMap());
    }
    const QString who = s->myTurn() ? tr("your move") : tr("your correspondent's move");
    log(QStringLiteral("info"), tr("Game %1 against %2 resumed at %3 half-moves - %4")
                                    .arg(saved.gid, saved.peerCall).arg(board.plyCount()).arg(who));
    if (s->state() == GameSession::State::Over) {
        log(QStringLiteral("info"), tr("This game is over: %1").arg(s->result()));
        m_store.remove(saved);
    }
    refreshGames();
    return QString();
}

bool AppController::deleteGame(int row)
{
    const SavedGame *g = m_games.game(row);
    if (!g) return false;
    const bool ok = m_store.remove(*g);
    refreshGames();
    return ok;
}

// =====================================================================
//  Logs
// =====================================================================

QString AppController::exportLog(const QString &which)
{
    const LogModel *model = which == QLatin1String("modem") ? &m_modemLog
                            : which == QLatin1String("chat") ? &m_chat : &m_frames;
    const QString text = model->plainText();
#ifdef Q_OS_ANDROID
    // No folder the operator can browse: the clipboard instead.
    QGuiApplication::clipboard()->setText(text);
    return tr("Copied to the clipboard");
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QDir(dir.isEmpty() ? QDir::homePath() : dir)
                             .filePath(QStringLiteral("ax25chess-%1-%2.txt")
                                           .arg(which, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        return tr("Could not write %1").arg(QDir::toNativeSeparators(path));
    }
    log(QStringLiteral("info"), tr("Log saved: %1").arg(QDir::toNativeSeparators(path)));
    return tr("Saved in %1").arg(QDir::toNativeSeparators(path));
#endif
}
