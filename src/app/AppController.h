/*
 * AppController.h - what the QML interface sees: the station, the game,
 * the logs, the saved games and the settings.
 *
 * One object, published to QML as "app".  It owns the radio link (the
 * embedded modem), the game session of the moment and the saved games, and
 * turns their events into properties, models and a few signals for the
 * dialogs.
 *
 * This file is part of AX25Chess.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "AppConfig.h"
#include "BoardView.h"
#include "GameSession.h"
#include "GameStore.h"
#include "ListModels.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class RadioLink;
class QTimer;

class AppController : public QObject, public BoardSource
{
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString platform READ platform CONSTANT)
    Q_PROPERTY(QVariantMap config READ config NOTIFY configChanged)
    Q_PROPERTY(bool configured READ configured NOTIFY configChanged)
    Q_PROPERTY(QString language READ language NOTIFY configChanged)

    // ---- the station
    Q_PROPERTY(QString modemState READ modemState NOTIFY stationChanged)
    Q_PROPERTY(bool stationOn READ stationOn NOTIFY stationChanged)
    Q_PROPERTY(bool ptt READ ptt NOTIFY stationChanged)
    Q_PROPERTY(bool dcd READ dcd NOTIFY stationChanged)
    Q_PROPERTY(QString channelState READ channelState NOTIFY stationChanged)
    Q_PROPERTY(QString channelText READ channelText NOTIFY stationChanged)
    Q_PROPERTY(QString modemDescription READ modemDescription NOTIFY stationChanged)

    // ---- the game
    Q_PROPERTY(QString gameState READ gameState NOTIFY gameChanged)
    Q_PROPERTY(QString gid READ gid NOTIFY gameChanged)
    Q_PROPERTY(QString peer READ peer NOTIFY gameChanged)
    Q_PROPERTY(QString myCall READ myCall NOTIFY gameChanged)
    Q_PROPERTY(QString myColor READ myColor NOTIFY gameChanged)
    Q_PROPERTY(QString turnColor READ turnColor NOTIFY gameChanged)
    Q_PROPERTY(bool myTurn READ myTurn NOTIFY gameChanged)
    Q_PROPERTY(QString situation READ situation NOTIFY gameChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY gameChanged)
    Q_PROPERTY(QString turnText READ turnText NOTIFY gameChanged)
    Q_PROPERTY(QString positionHash READ positionHash NOTIFY gameChanged)
    Q_PROPERTY(QString ackText READ ackText NOTIFY ackTextChanged)
    Q_PROPERTY(bool drawOfferPending READ drawOfferPending NOTIFY gameChanged)
    Q_PROPERTY(bool boardFlipped READ boardFlipped NOTIFY boardFlippedChanged)
    Q_PROPERTY(int boardRevision READ boardRevision NOTIFY boardChanged)

    // ---- lists
    Q_PROPERTY(QObject *moves READ moves CONSTANT)
    Q_PROPERTY(QObject *frames READ frames CONSTANT)
    Q_PROPERTY(QObject *modemLog READ modemLog CONSTANT)
    Q_PROPERTY(QObject *chat READ chat CONSTANT)
    Q_PROPERTY(QObject *games READ games CONSTANT)
    Q_PROPERTY(bool unreadChat READ unreadChat NOTIFY unreadChatChanged)
    Q_PROPERTY(int savedCount READ savedCount NOTIFY gamesChanged)
    Q_PROPERTY(int waitingCount READ waitingCount NOTIFY gamesChanged)

    // ---- settings helpers
    Q_PROPERTY(QVariantList audioInputs READ audioInputs NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList audioOutputs READ audioOutputs NOTIFY audioDevicesChanged)
    Q_PROPERTY(QString configFilePath READ configFilePath NOTIFY configChanged)
    Q_PROPERTY(bool flatRendering READ flatRendering CONSTANT)
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT)

public:
    explicit AppController(const AppConfig &config, QObject *parent = nullptr);
    ~AppController() override;

    // Called after a language change, so the QML engine retranslates.
    void setRetranslateHook(std::function<void()> hook) { m_retranslate = std::move(hook); }
    // Installs the translator for the configured language (or the system's).
    void applyLanguage();

    // Command-line overrides, for this run only: never written to config.json.
    void setOverrides(bool noModem, const QString &modemConf);

    // Startup: announces saved games, starts the modem when asked to.
    Q_INVOKABLE void startup();

    QString version() const;
    QString platform() const;
    QVariantMap config() const { return m_config.toMap(); }
    bool configured() const { return m_config.configured; }
    QString language() const;
    const AppConfig &appConfig() const { return m_config; }

    QString modemState() const;
    bool stationOn() const;
    bool ptt() const;
    bool dcd() const;
    QString channelState() const;
    QString channelText() const;
    QString modemDescription() const;

    QString gameState() const;
    QString gid() const;
    QString peer() const;
    QString myCall() const;
    QString myColor() const;
    QString turnColor() const;
    bool myTurn() const;
    QString situation() const;
    QString hint() const;
    QString turnText() const;
    QString positionHash() const;
    QString ackText() const { return m_ackText; }
    bool drawOfferPending() const;
    bool boardFlipped() const { return m_flipped; }
    int boardRevision() const { return m_boardRevision; }

    QObject *moves() { return &m_moves; }
    QObject *frames() { return &m_frames; }
    QObject *modemLog() { return &m_modemLog; }
    QObject *chat() { return &m_chat; }
    QObject *games() { return &m_games; }
    bool unreadChat() const { return m_unreadChat; }
    int savedCount() const { return m_games.count(); }
    int waitingCount() const { return m_games.waiting(); }

    QVariantList audioInputs() const;
    QVariantList audioOutputs() const;
    QString configFilePath() const;
    bool flatRendering() const;
    QString monoFamily() const;

    // ---- BoardSource
    const chess::Board &currentBoard() const override;
    bool boardInteractive() const override { return myTurn(); }
    int lastMoveFrom() const override { return m_lastFrom; }
    int lastMoveTo() const override { return m_lastTo; }

    // ---- actions from the interface ----------------------------------------------
    // Settings: the whole map, as config gave it.  Returns an error, or "".
    Q_INVOKABLE QString saveConfig(const QVariantMap &map);
    Q_INVOKABLE void startStation();
    Q_INVOKABLE void stopStation();
    // Returns why it cannot be done, or "".
    Q_INVOKABLE QString invite();
    Q_INVOKABLE void resync();
    Q_INVOKABLE void offerDraw();
    Q_INVOKABLE void answerDraw(bool accept);
    Q_INVOKABLE void resign();
    Q_INVOKABLE void sendChat(const QString &text);
    Q_INVOKABLE void ping();
    // promo: "", "Q", "R", "B" or "N".
    Q_INVOKABLE bool playMove(const QString &uid, int square, const QString &promo);
    Q_INVOKABLE void flipBoard();
    Q_INVOKABLE void markChatRead();

    // Captured pieces of one colour ("W" or "B"): [{uid, glyph}], and the
    // material edge of the side that took them.
    Q_INVOKABLE QVariantList captured(const QString &color) const;
    Q_INVOKABLE int materialEdge(const QString &color) const;
    Q_INVOKABLE QString pieceGlyph(const QString &kind) const;
    Q_INVOKABLE QString pieceFontFamily() const { return BoardView::pieceFontFamily(); }

    // Saved games.
    Q_INVOKABLE void refreshGames();
    // True when resuming would close a game in progress (ask first).
    Q_INVOKABLE bool resumeNeedsConfirmation(int row) const;
    Q_INVOKABLE QString resumeGame(int row);
    Q_INVOKABLE bool deleteGame(int row);

    // Logs.
    Q_INVOKABLE QString exportLog(const QString &which);

    // The modem's configuration file.
    Q_INVOKABLE QString generatedConfigText(const QVariantMap &map) const;
    Q_INVOKABLE QString readConfigFile(const QString &path) const;
    Q_INVOKABLE QString writeConfigFile(const QString &path, const QString &text);
    Q_INVOKABLE QString createStarterConfig();
    Q_INVOKABLE void refreshAudioDevices() { emit audioDevicesChanged(); }
    Q_INVOKABLE void copyText(const QString &text);

    // A frame as if the modem had decoded it (tests, and the loopback of
    // two stations in one process).
    void receive(const QString &source, const QByteArray &info) { onInfoReceived(source, QString(), info, QString()); }

    // Android.
    Q_INVOKABLE QString backgroundStatus() const;
    Q_INVOKABLE void requestBatteryExemption();

signals:
    void configChanged();
    void stationChanged();
    void gameChanged();
    void boardChanged();
    void boardFlippedChanged();
    void ackTextChanged();
    void unreadChatChanged();
    void gamesChanged();
    void audioDevicesChanged();
    // For the dialogs.
    void announce(const QString &title, const QString &text);
    void drawOfferReceived(const QString &peer);
    void attention();
    // Every CHS-1 frame handed to the radio, encoded.
    void frameOut(const QString &source, const QString &destination, const QByteArray &info);

private:
    double now() const { return m_clock.elapsed() / 1000.0; }
    GameSession *ensureSession();
    void setSession(GameSession *session);
    void wireSession(GameSession *session);
    void log(const QString &level, const QString &text);
    void onInfoReceived(const QString &source, const QString &destination, const QByteArray &info, const QString &monitor);
    void onMoveApplied(const chess::Move &move, bool byPeer);
    void rebuildMoveList();
    void autosave();
    void tick();
    void touchBoard();
    void notifyInBackground(const QString &title, const QString &text);
    MoveListModel::Row rowFor(const chess::Board &after, const chess::Move &m, int ply, bool byPeer) const;

    AppConfig m_config;
    RadioLink *m_radio = nullptr;
    GameStore m_store;
    QPointer<GameSession> m_session;
    chess::Board m_emptyBoard;
    QElapsedTimer m_clock;
    QTimer *m_tick = nullptr;
    QTranslator *m_translator = nullptr;
    std::function<void()> m_retranslate;

    MoveListModel m_moves;
    LogModel m_frames{3000};
    LogModel m_modemLog{2000};
    LogModel m_chat{1000};
    GameListModel m_games;

    QString m_ackText;
    bool m_unreadChat = false;
    bool m_flipped = false;
    bool m_flipFollowsColour = true;
    int m_boardRevision = 0;
    int m_lastFrom = -1;
    int m_lastTo = -1;
    bool m_startupDone = false;
    bool m_noModem = false;
    QString m_modemConfOverride;

    RadioConfig effectiveRadio() const;
};
