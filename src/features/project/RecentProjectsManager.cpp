// SPDX-License-Identifier: MPL-2.0

// RecentProjectsManager.cpp
#include "RecentProjectsManager.h"

#include <QCoreApplication>
#include <QSettings>
#include <QDir>
#include <QFileInfo>
namespace ruwa::core::serialization {

namespace {
constexpr auto kForgottenPathsKey = "RecentProjects/ForgottenPaths";
constexpr auto kPreviewDisabledPathsKey = "RecentProjects/PreviewDisabledPaths";
} // namespace

RecentProjectsManager& RecentProjectsManager::instance()
{
    static RecentProjectsManager inst;
    return inst;
}

RecentProjectsManager::RecentProjectsManager() { }

// ============================================================================
// Modification
// ============================================================================

void RecentProjectsManager::addEntry(const RecentProjectEntry& entry)
{
    if (!entry.isValid())
        return;

    RecentProjectEntry e = entry;
    e.filePath = normalizePath(e.filePath);
    if (!e.isValid())
        return;

    m_forgottenPaths.remove(e.filePath);
    e.previewEnabled = !m_previewDisabledPaths.contains(e.filePath);

    // Remove old occurrence (dedup by path)
    int existing = indexByPath(e.filePath);
    if (existing >= 0) {
        m_entries.removeAt(existing);
    }

    // Prepend (most recent first)
    if (!e.lastOpened.isValid()) {
        e.lastOpened = QDateTime::currentDateTime();
    }
    m_entries.prepend(e);

    // Trim
    while (m_entries.size() > m_maxEntries) {
        m_entries.removeLast();
    }

    save();
    emit entriesChanged();
}

void RecentProjectsManager::addEntry(const QString& filePath, const QString& projectName,
    const QSize& canvasSize, const QString& tabIconAlias)
{
    const QString normalizedPath = normalizePath(filePath);

    RecentProjectEntry entry;
    entry.filePath = normalizedPath;
    // Use filename when name is empty or generic "Untitled Project"
    if (projectName.isEmpty() || projectName == QStringLiteral("Untitled Project")) {
        entry.projectName = QFileInfo(normalizedPath).baseName();
        if (entry.projectName.isEmpty()) {
            entry.projectName = QFileInfo(normalizedPath).fileName();
        }
    } else {
        entry.projectName = projectName;
    }
    entry.tabIconAlias = tabIconAlias;
    entry.lastOpened = QDateTime::currentDateTime();
    entry.canvasSize = canvasSize;
    addEntry(entry);
}

void RecentProjectsManager::removeEntry(const QString& filePath)
{
    const QString normalizedPath = normalizePath(filePath);
    const int idx = indexByPath(normalizedPath);
    const bool removedFromForgotten = m_forgottenPaths.remove(normalizedPath) > 0;
    const bool removedPreviewState = m_previewDisabledPaths.remove(normalizedPath) > 0;
    if (idx < 0 && !removedFromForgotten && !removedPreviewState)
        return;

    if (idx >= 0) {
        m_entries.removeAt(idx);
    }
    save();
    emit entriesChanged();
}

void RecentProjectsManager::forgetEntry(const QString& filePath)
{
    const QString normalizedPath = normalizePath(filePath);
    const int idx = indexByPath(normalizedPath);
    bool changed = false;

    if (idx >= 0) {
        m_entries.removeAt(idx);
        changed = true;
    }
    if (!normalizedPath.isEmpty() && !m_forgottenPaths.contains(normalizedPath)) {
        m_forgottenPaths.insert(normalizedPath);
        changed = true;
    }

    if (!changed)
        return;

    save();
    emit entriesChanged();
}

void RecentProjectsManager::updateEntryMetadata(
    const QString& filePath, const QString& projectName, bool previewEnabled)
{
    const QString normalizedPath = normalizePath(filePath);
    const int idx = indexByPath(normalizedPath);
    bool changed = false;

    if (previewEnabled) {
        changed = m_previewDisabledPaths.remove(normalizedPath) > 0;
    } else if (!normalizedPath.isEmpty() && !m_previewDisabledPaths.contains(normalizedPath)) {
        m_previewDisabledPaths.insert(normalizedPath);
        changed = true;
    }

    if (idx >= 0) {
        RecentProjectEntry& entry = m_entries[idx];
        const QString trimmedName = projectName.trimmed();
        if (!trimmedName.isEmpty() && entry.projectName != trimmedName) {
            entry.projectName = trimmedName;
            changed = true;
        }
        if (entry.previewEnabled != previewEnabled) {
            entry.previewEnabled = previewEnabled;
            changed = true;
        }
    }

    if (!changed)
        return;

    save();
    emit entriesChanged();
}

RecentProjectEntry RecentProjectsManager::entryForPath(const QString& filePath) const
{
    const QString normalizedPath = normalizePath(filePath);
    const int idx = indexByPath(normalizedPath);
    if (idx >= 0) {
        return m_entries[idx];
    }
    return {};
}

bool RecentProjectsManager::isPreviewEnabled(const QString& filePath) const
{
    return !m_previewDisabledPaths.contains(normalizePath(filePath));
}

void RecentProjectsManager::clear()
{
    if (m_entries.isEmpty() && m_forgottenPaths.isEmpty() && m_previewDisabledPaths.isEmpty())
        return;

    m_entries.clear();
    m_forgottenPaths.clear();
    m_previewDisabledPaths.clear();
    save();
    emit entriesChanged();
}

void RecentProjectsManager::pruneInvalid()
{
    bool changed = false;

    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (!QFileInfo::exists(m_entries[i].filePath)) {
            m_entries.removeAt(i);
            changed = true;
        }
    }

    if (changed) {
        save();
        emit entriesChanged();
    }
}

void RecentProjectsManager::setMaxEntries(int max)
{
    m_maxEntries = qBound(1, max, MAX_ENTRIES_LIMIT);

    while (m_entries.size() > m_maxEntries) {
        m_entries.removeLast();
    }
}

// ============================================================================
// Persistence
// ============================================================================

void RecentProjectsManager::load()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    m_entries.clear();
    m_forgottenPaths.clear();
    m_previewDisabledPaths.clear();

    int count = settings.beginReadArray(QStringLiteral("RecentProjects"));
    for (int i = 0; i < count && i < m_maxEntries; ++i) {
        settings.setArrayIndex(i);

        RecentProjectEntry entry;
        entry.filePath = normalizePath(settings.value(QStringLiteral("filePath")).toString());
        entry.projectName = settings.value(QStringLiteral("projectName")).toString();
        entry.tabIconAlias = settings.value(QStringLiteral("tabIconAlias")).toString();
        entry.lastOpened = settings.value(QStringLiteral("lastOpened")).toDateTime();
        entry.canvasSize = QSize(settings.value(QStringLiteral("canvasWidth"), 0).toInt(),
            settings.value(QStringLiteral("canvasHeight"), 0).toInt());
        entry.previewEnabled = settings.value(QStringLiteral("previewEnabled"), true).toBool();

        if (entry.isValid()) {
            if (!entry.previewEnabled) {
                m_previewDisabledPaths.insert(entry.filePath);
            }
            m_entries.append(entry);
        }
    }
    settings.endArray();

    const QStringList forgottenPaths
        = settings.value(QString::fromLatin1(kForgottenPathsKey)).toStringList();
    for (const QString& path : forgottenPaths) {
        const QString normalizedPath = normalizePath(path);
        if (!normalizedPath.isEmpty()) {
            m_forgottenPaths.insert(normalizedPath);
        }
    }

    const QStringList previewDisabledPaths
        = settings.value(QString::fromLatin1(kPreviewDisabledPathsKey)).toStringList();
    for (const QString& path : previewDisabledPaths) {
        const QString normalizedPath = normalizePath(path);
        if (!normalizedPath.isEmpty()) {
            m_previewDisabledPaths.insert(normalizedPath);
        }
    }

    for (int i = m_entries.size() - 1; i >= 0; --i) {
        RecentProjectEntry& entry = m_entries[i];
        if (m_forgottenPaths.contains(entry.filePath)) {
            m_entries.removeAt(i);
            continue;
        }
        entry.previewEnabled = !m_previewDisabledPaths.contains(entry.filePath);
    }

    m_loaded = true;
}

void RecentProjectsManager::notifyThumbnailsUpdated()
{
    emit entriesChanged();
}

void RecentProjectsManager::save()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.beginWriteArray(QStringLiteral("RecentProjects"), m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        settings.setArrayIndex(i);

        const auto& e = m_entries[i];
        settings.setValue(QStringLiteral("filePath"), e.filePath);
        settings.setValue(QStringLiteral("projectName"), e.projectName);
        settings.setValue(QStringLiteral("tabIconAlias"), e.tabIconAlias);
        settings.setValue(QStringLiteral("lastOpened"), e.lastOpened);
        settings.setValue(QStringLiteral("canvasWidth"), e.canvasSize.width());
        settings.setValue(QStringLiteral("canvasHeight"), e.canvasSize.height());
        settings.setValue(QStringLiteral("previewEnabled"), e.previewEnabled);
    }
    settings.endArray();
    settings.setValue(QString::fromLatin1(kForgottenPathsKey), m_forgottenPaths.values());
    settings.setValue(
        QString::fromLatin1(kPreviewDisabledPathsKey), m_previewDisabledPaths.values());
}

// ============================================================================
// Private
// ============================================================================

int RecentProjectsManager::indexByPath(const QString& filePath) const
{
    const QString normalizedPath = normalizePath(filePath);
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].filePath == normalizedPath) {
            return i;
        }
    }
    return -1;
}

QString RecentProjectsManager::normalizePath(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return {};
    }

    QFileInfo info(filePath);
    QString normalized = info.absoluteFilePath();
    if (normalized.isEmpty()) {
        normalized = QDir::cleanPath(filePath);
    }
    return QDir::fromNativeSeparators(normalized);
}

} // namespace ruwa::core::serialization
