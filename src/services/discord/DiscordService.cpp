// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   |   D I S C O R D   S E R V I C E   I M P L E M E N T A T I O N
// ======================================================================================
//
//   Rich Presence over Discord's local IPC socket, implemented with Qt only.
//   No third-party Discord SDK and no proprietary binary are involved.
//
//   Wire format (little-endian): each frame is
//       int32 opcode | int32 payload-length | UTF-8 JSON payload
//   Opcodes: 0 Handshake, 1 Frame, 2 Close, 3 Ping, 4 Pong.
//   Handshake -> Discord replies with a DISPATCH/READY frame -> we send
//   SET_ACTIVITY frames whenever the presence changes.
// ======================================================================================

#include "services/discord/DiscordService.h"

#include <QCoreApplication>

#ifndef RUWA_WITH_DISCORD
#define RUWA_WITH_DISCORD 0
#endif

#if RUWA_WITH_DISCORD
#include <QByteArray>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLocalSocket>
#include <QUuid>
#include <QtEndian>
#endif

namespace ruwa::services {

#if RUWA_WITH_DISCORD

namespace {

// Ruwa's registered Discord application id (used only to identify the app to a
// locally running Discord client; it is not a secret).
constexpr quint64 kRuwaDiscordClientId = 1477323405727633511ULL;

// Discord exposes up to ten local IPC endpoints; scan them in order.
constexpr int kMaxPipeIndex = 9;
constexpr int kReconnectIntervalMs = 15000;

constexpr qint32 kOpHandshake = 0;
constexpr qint32 kOpFrame = 1;
constexpr qint32 kOpPing = 3;
constexpr qint32 kOpPong = 4;

const char* pipeName(int index)
{
    static QByteArray storage;
    storage = QByteArrayLiteral("discord-ipc-") + QByteArray::number(index);
    return storage.constData();
}

} // namespace

struct DiscordService::Impl {
    QLocalSocket* socket = nullptr;
    QByteArray readBuffer;
    int pipeIndex = 0;
    bool handshakeReady = false;

    // App start time (elapsed counts from here, never resets).
    QDateTime appStartTime;
    bool onProjectTab = false;

    // Latest presence to publish once the connection is ready.
    QString details;
    QString state;
    bool cleared = false;
};

#else

struct DiscordService::Impl { };

#endif

DiscordService::DiscordService(QObject* parent)
    : QObject(parent)
    , m_impl(new Impl)
{
}

DiscordService::~DiscordService()
{
    shutdown();
    delete m_impl;
    m_impl = nullptr;
}

DiscordService* DiscordService::instance()
{
    static DiscordService* s_instance = nullptr;
    if (!s_instance) {
        s_instance = new DiscordService(qApp);
    }
    return s_instance;
}

void DiscordService::initialize()
{
#if RUWA_WITH_DISCORD
    if (!m_enabled || m_initialized) {
        return;
    }

    m_initialized = true;
    m_impl->appStartTime = QDateTime::currentDateTime();
    m_impl->details = QStringLiteral("Free raster editor for connoisseurs of aesthetics");
    m_impl->state
        = m_impl->onProjectTab ? QStringLiteral("Working on project") : QStringLiteral("Idle");
    m_impl->cleared = false;

    // Retry connecting in the background so presence appears if Discord starts
    // after Ruwa, and reappears if Discord restarts.
    m_reconnectTimer = new QTimer(this);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (m_enabled && m_initialized && !m_impl->socket) {
            connectToDiscord();
        }
    });
    m_reconnectTimer->start(kReconnectIntervalMs);

    connect(qApp, &QCoreApplication::aboutToQuit, this, &DiscordService::shutdown,
        Qt::UniqueConnection);

    connectToDiscord();
#endif
}

void DiscordService::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;
    if (m_enabled) {
        initialize();
    } else {
        shutdown();
    }
    emit enabledChanged(m_enabled);
}

void DiscordService::connectToDiscord()
{
#if RUWA_WITH_DISCORD
    if (!m_enabled || !m_initialized || m_impl->socket) {
        return;
    }

    m_impl->handshakeReady = false;
    m_impl->readBuffer.clear();

    m_impl->socket = new QLocalSocket(this);
    connect(m_impl->socket, &QLocalSocket::connected, this, &DiscordService::onConnected);
    connect(m_impl->socket, &QLocalSocket::readyRead, this, &DiscordService::onReadyRead);
    connect(m_impl->socket, &QLocalSocket::disconnected, this, &DiscordService::onDisconnected);
    connect(
        m_impl->socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
            // Named endpoint for this index is absent; try the next one, then give
            // up until the reconnect timer fires again.
            if (m_impl->socket && m_impl->pipeIndex < kMaxPipeIndex) {
                m_impl->pipeIndex++;
                QLocalSocket* dead = m_impl->socket;
                m_impl->socket = nullptr;
                dead->deleteLater();
                connectToDiscord();
            } else {
                onDisconnected();
            }
        });

    m_impl->socket->connectToServer(QString::fromLatin1(pipeName(m_impl->pipeIndex)));
#endif
}

#if RUWA_WITH_DISCORD
namespace {

QByteArray buildFrame(qint32 opcode, const QByteArray& payload)
{
    QByteArray frame;
    frame.resize(8);
    qToLittleEndian<qint32>(opcode, frame.data());
    qToLittleEndian<qint32>(static_cast<qint32>(payload.size()), frame.data() + 4);
    frame.append(payload);
    return frame;
}

} // namespace
#endif

void DiscordService::onConnected()
{
#if RUWA_WITH_DISCORD
    if (!m_enabled || !m_initialized || !m_impl->socket) {
        return;
    }

    QJsonObject handshake;
    handshake.insert(QStringLiteral("v"), 1);
    handshake.insert(QStringLiteral("client_id"), QString::number(kRuwaDiscordClientId));
    const QByteArray payload = QJsonDocument(handshake).toJson(QJsonDocument::Compact);
    m_impl->socket->write(buildFrame(kOpHandshake, payload));
#endif
}

void DiscordService::onReadyRead()
{
#if RUWA_WITH_DISCORD
    if (!m_enabled || !m_initialized || !m_impl->socket) {
        return;
    }

    m_impl->readBuffer.append(m_impl->socket->readAll());

    // Drain every complete frame currently buffered.
    while (m_impl->readBuffer.size() >= 8) {
        const qint32 opcode = qFromLittleEndian<qint32>(m_impl->readBuffer.constData());
        const qint32 length = qFromLittleEndian<qint32>(m_impl->readBuffer.constData() + 4);
        if (length < 0 || m_impl->readBuffer.size() < 8 + length) {
            break; // wait for the rest of the payload
        }

        const QByteArray payload = m_impl->readBuffer.mid(8, length);
        m_impl->readBuffer.remove(0, 8 + length);

        if (opcode == kOpPing) {
            m_impl->socket->write(buildFrame(kOpPong, payload));
            continue;
        }
        if (opcode == kOpFrame && !m_impl->handshakeReady) {
            // The first frame after the handshake is DISPATCH/READY; from here
            // Discord accepts SET_ACTIVITY.
            m_impl->handshakeReady = true;
            writeActivity();
        }
    }
#endif
}

void DiscordService::onDisconnected()
{
#if RUWA_WITH_DISCORD
    m_impl->handshakeReady = false;
    m_impl->readBuffer.clear();
    m_impl->pipeIndex = 0;
    if (m_impl->socket) {
        m_impl->socket->deleteLater();
        m_impl->socket = nullptr;
    }
    // The reconnect timer will attempt a fresh scan on its next tick.
#endif
}

void DiscordService::writeActivity()
{
#if RUWA_WITH_DISCORD
    if (!m_enabled || !m_initialized || !m_impl->socket || !m_impl->handshakeReady) {
        return;
    }

    QJsonObject args;
    args.insert(QStringLiteral("pid"), static_cast<qint64>(QCoreApplication::applicationPid()));

    if (m_impl->cleared) {
        args.insert(QStringLiteral("activity"), QJsonValue(QJsonValue::Null));
    } else {
        QJsonObject timestamps;
        timestamps.insert(
            QStringLiteral("start"), static_cast<qint64>(m_impl->appStartTime.toSecsSinceEpoch()));

        QJsonObject activity;
        activity.insert(QStringLiteral("details"), m_impl->details);
        activity.insert(QStringLiteral("state"), m_impl->state);
        activity.insert(QStringLiteral("timestamps"), timestamps);
        args.insert(QStringLiteral("activity"), activity);
    }

    QJsonObject command;
    command.insert(QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY"));
    command.insert(QStringLiteral("args"), args);
    command.insert(QStringLiteral("nonce"), QUuid::createUuid().toString(QUuid::WithoutBraces));

    const QByteArray payload = QJsonDocument(command).toJson(QJsonDocument::Compact);
    m_impl->socket->write(buildFrame(kOpFrame, payload));
#endif
}

void DiscordService::shutdown()
{
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
        m_reconnectTimer->deleteLater();
        m_reconnectTimer = nullptr;
    }

#if RUWA_WITH_DISCORD
    if (m_impl->socket) {
        QLocalSocket* socket = m_impl->socket;
        m_impl->socket = nullptr;
        socket->disconnect(this);
        socket->disconnectFromServer();
        socket->deleteLater();
    }
    m_impl->handshakeReady = false;
    m_impl->readBuffer.clear();
#endif

    m_initialized = false;
}

void DiscordService::onActiveTabChanged(ruwa::core::BaseTab* tab)
{
#if RUWA_WITH_DISCORD
    m_impl->onProjectTab = tab && tab->type() == ruwa::core::BaseTab::TabType::Workspace;
    updateActivityFromContext();
#else
    Q_UNUSED(tab);
#endif
}

void DiscordService::updateActivityFromContext()
{
#if RUWA_WITH_DISCORD
    m_impl->cleared = false;
    m_impl->details = QStringLiteral("Free raster editor for connoisseurs of aesthetics");
    m_impl->state
        = m_impl->onProjectTab ? QStringLiteral("Working on project") : QStringLiteral("Idle");
    writeActivity();
#endif
}

void DiscordService::setActivity(const QString& details, const QString& state)
{
#if RUWA_WITH_DISCORD
    m_impl->cleared = false;
    m_impl->details = details;
    m_impl->state = state;
    writeActivity();
#else
    Q_UNUSED(details);
    Q_UNUSED(state);
#endif
}

void DiscordService::clearActivity()
{
#if RUWA_WITH_DISCORD
    m_impl->cleared = true;
    writeActivity();
#endif
}

} // namespace ruwa::services
