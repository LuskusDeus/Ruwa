// SPDX-License-Identifier: MPL-2.0

// UsageTracker.h
#ifndef RUWA_CORE_SETTINGS_USAGETRACKER_H
#define RUWA_CORE_SETTINGS_USAGETRACKER_H

#include <QElapsedTimer>
#include <QObject>

class QTimer;

namespace ruwa::core {

/**
 * @brief Tracks how long the application has been in use, across sessions.
 *
 * The persisted total lives in SettingsManager; this class owns the running session's
 * elapsed time and commits it periodically (so a crash costs at most one interval) and
 * once more on application exit.
 */
class UsageTracker : public QObject {
    Q_OBJECT

public:
    static UsageTracker& instance();

    /// Begin tracking the current session. Safe to call more than once (no-op after the
    /// first call). Commits on a timer and on QCoreApplication::aboutToQuit.
    void start();

    /// Persisted total plus the current session's uncommitted time.
    qint64 totalSeconds() const;

    /// Seconds elapsed in the current session (0 before start()).
    qint64 sessionSeconds() const;

    /// Fold the uncommitted session time into the persisted total right now.
    void commit();

private:
    UsageTracker() = default;
    ~UsageTracker() override = default;

    UsageTracker(const UsageTracker&) = delete;
    UsageTracker& operator=(const UsageTracker&) = delete;

    QElapsedTimer m_sessionTimer;
    QTimer* m_commitTimer { nullptr };
    /// Session milliseconds already folded into the persisted total.
    qint64 m_committedMs { 0 };
    bool m_started { false };
};

} // namespace ruwa::core

#endif // RUWA_CORE_SETTINGS_USAGETRACKER_H
