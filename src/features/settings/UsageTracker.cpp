// SPDX-License-Identifier: MPL-2.0

#include "features/settings/UsageTracker.h"

#include "features/settings/SettingsManager.h"

#include <QCoreApplication>
#include <QTimer>

namespace ruwa::core {

namespace {

/// How often the running session's time is folded into the persisted total.
constexpr int kCommitIntervalMs = 60 * 1000;

} // namespace

UsageTracker& UsageTracker::instance()
{
    static UsageTracker instance;
    return instance;
}

void UsageTracker::start()
{
    if (m_started) {
        return;
    }

    m_started = true;
    m_committedMs = 0;
    m_sessionTimer.start();

    // Make sure the persisted total is loaded before the first commit adds to it.
    (void) SettingsManager::instance().settings();

    m_commitTimer = new QTimer(this);
    m_commitTimer->setInterval(kCommitIntervalMs);
    connect(m_commitTimer, &QTimer::timeout, this, &UsageTracker::commit);
    m_commitTimer->start();

    if (QCoreApplication* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &UsageTracker::commit);
    }
}

qint64 UsageTracker::sessionSeconds() const
{
    if (!m_started || !m_sessionTimer.isValid()) {
        return 0;
    }

    return m_sessionTimer.elapsed() / 1000;
}

qint64 UsageTracker::totalSeconds() const
{
    const qint64 persisted = SettingsManager::instance().totalUsageSeconds();
    if (!m_started || !m_sessionTimer.isValid()) {
        return persisted;
    }

    // The persisted value already includes m_committedMs of this session.
    return persisted + (m_sessionTimer.elapsed() - m_committedMs) / 1000;
}

void UsageTracker::commit()
{
    if (!m_started || !m_sessionTimer.isValid()) {
        return;
    }

    const qint64 elapsedMs = m_sessionTimer.elapsed();
    const qint64 pendingMs = elapsedMs - m_committedMs;
    const qint64 pendingSeconds = pendingMs / 1000;
    if (pendingSeconds <= 0) {
        return;
    }

    // Keep the sub-second remainder for the next commit instead of dropping it.
    m_committedMs += pendingSeconds * 1000;
    SettingsManager::instance().addUsageSeconds(pendingSeconds);
}

} // namespace ruwa::core
