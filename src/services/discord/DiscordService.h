// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   |   D I S C O R D   S E R V I C E
// ======================================================================================
//   File        : DiscordService.h
//   Description : Discord Rich Presence integration over the local IPC socket
// ======================================================================================

#ifndef RUWA_SERVICES_DISCORD_DISCORDSERVICE_H
#define RUWA_SERVICES_DISCORD_DISCORDSERVICE_H

#include "shell/tab-system/BaseTab.h"

#include <QObject>
#include <QString>
#include <QTimer>

namespace ruwa::services {

/**
 * @brief Discord Rich Presence service
 *
 * Talks to Discord directly over its local IPC socket (a named pipe on
 * Windows) using Qt only — there is no third-party Discord SDK and no
 * proprietary binary involved, so this ships in public builds. All work runs on
 * the main thread; the socket is asynchronous. If Discord is not running the
 * service stays idle and retries connecting in the background.
 *
 * Shows "Idle" on the Homepage, "Working on project" + elapsed time when on a
 * project. Elapsed time counts from app start and never resets.
 */
class DiscordService : public QObject {
    Q_OBJECT

public:
    explicit DiscordService(QObject* parent = nullptr);
    ~DiscordService() override;

    static DiscordService* instance();

    /// Initialize Discord (call after QApplication exists)
    void initialize();

    /// Enable or disable Rich Presence at runtime. Enabled by default.
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    /// Called when active tab changes. Updates Rich Presence (idle vs working + elapsed).
    void onActiveTabChanged(ruwa::core::BaseTab* tab);

    /// Update Rich Presence activity (legacy / manual)
    void setActivity(const QString& details, const QString& state = QString());

    /// Clear Rich Presence
    void clearActivity();

    bool isInitialized() const { return m_initialized; }

signals:
    void enabledChanged(bool enabled);

private:
    void shutdown();
    void updateActivityFromContext();

    // IPC lifecycle (no-ops when Discord support is compiled out).
    void connectToDiscord();
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void writeActivity();

    struct Impl;
    Impl* m_impl = nullptr;
    bool m_enabled = true;
    bool m_initialized = false;
    QTimer* m_reconnectTimer = nullptr;
};

} // namespace ruwa::services

#endif // RUWA_SERVICES_DISCORD_DISCORDSERVICE_H
