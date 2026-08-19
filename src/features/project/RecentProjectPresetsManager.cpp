// SPDX-License-Identifier: MPL-2.0

#include "RecentProjectPresetsManager.h"

#include <QCoreApplication>
#include <QSettings>
#include <QUuid>

namespace ruwa::core::serialization {

namespace {
constexpr auto kSettingsArray = "RecentProjectPresets";
constexpr auto kNewProjectCtx = "ruwa::ui::widgets::NewProjectContent";
constexpr auto kDefaultProjectName = "Untitled Project";

aether::TilePixelFormat tileFormatFromInt(int value)
{
    switch (value) {
    case static_cast<int>(aether::TilePixelFormat::RGBA16F):
        return aether::TilePixelFormat::RGBA16F;
    case static_cast<int>(aether::TilePixelFormat::RGBA32F):
        return aether::TilePixelFormat::RGBA32F;
    default:
        return aether::TilePixelFormat::RGBA8;
    }
}
} // namespace

bool isDefaultProjectName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    if (trimmed == QLatin1String(kDefaultProjectName)) {
        return true;
    }
    // The name field is pre-filled with the translated placeholder, so a user who never
    // touched it stores that instead of the English literal.
    return trimmed == QCoreApplication::translate(kNewProjectCtx, kDefaultProjectName);
}

RecentProjectPresetsManager& RecentProjectPresetsManager::instance()
{
    static RecentProjectPresetsManager manager;
    return manager;
}

RecentProjectPresetsManager::RecentProjectPresetsManager()
{
    load();
}

QString RecentProjectPresetsManager::findMatchingEntryId(const QSize& canvasSize,
    bool infiniteCanvasEnabled, const QString& colorMode, const QColor& backgroundColor,
    aether::TilePixelFormat tileFormat) const
{
    RecentProjectPresetEntry probe;
    probe.canvasSize = canvasSize;
    probe.infiniteCanvasEnabled = infiniteCanvasEnabled;
    probe.colorMode = colorMode;
    probe.backgroundColor = backgroundColor.isValid() ? backgroundColor : QColor(Qt::white);
    probe.tileFormat = tileFormat;

    for (const RecentProjectPresetEntry& entry : m_entries) {
        if (entry.sameConfiguration(probe)) {
            return entry.id;
        }
    }
    return {};
}

void RecentProjectPresetsManager::addEntry(const QString& projectName, const QSize& canvasSize,
    bool infiniteCanvasEnabled, const QString& colorMode, const QColor& backgroundColor,
    aether::TilePixelFormat tileFormat)
{
    RecentProjectPresetEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.projectName = isDefaultProjectName(projectName) ? QString() : projectName.trimmed();
    entry.canvasSize = canvasSize;
    entry.infiniteCanvasEnabled = infiniteCanvasEnabled;
    entry.colorMode = colorMode;
    entry.backgroundColor = backgroundColor.isValid() ? backgroundColor : QColor(Qt::white);
    entry.tileFormat = tileFormat;
    entry.createdAt = QDateTime::currentDateTime();

    if (!entry.isValid()) {
        return;
    }

    // Same configuration as an existing card: refresh it and float it to the front, so the tab
    // stays a short list of distinct setups instead of a pile of near-identical cards.
    for (int index = 0; index < m_entries.size(); ++index) {
        if (!m_entries[index].sameConfiguration(entry)) {
            continue;
        }

        RecentProjectPresetEntry merged = m_entries.takeAt(index);
        merged.createdAt = entry.createdAt;
        merged.useCount += 1;
        // A real name always wins over the placeholder, and the newest real name wins.
        if (entry.hasCustomName() || !merged.hasCustomName()) {
            merged.projectName = entry.projectName;
        }
        m_entries.prepend(merged);
        save();
        emit entriesChanged();
        return;
    }

    m_entries.prepend(entry);
    while (m_entries.size() > maxEntries()) {
        m_entries.removeLast();
    }
    save();
    emit entriesChanged();
}

void RecentProjectPresetsManager::removeEntry(const QString& id)
{
    for (int index = 0; index < m_entries.size(); ++index) {
        if (m_entries[index].id != id) {
            continue;
        }
        m_entries.removeAt(index);
        save();
        emit entriesChanged();
        return;
    }
}

void RecentProjectPresetsManager::clear()
{
    if (m_entries.isEmpty()) {
        return;
    }

    m_entries.clear();
    save();
    emit entriesChanged();
}

bool RecentProjectPresetsManager::normalizeEntries()
{
    bool changed = false;

    for (int index = 0; index < m_entries.size(); ++index) {
        if (!m_entries[index].projectName.isEmpty() && !m_entries[index].hasCustomName()) {
            m_entries[index].projectName.clear();
            changed = true;
        }

        for (int other = index + 1; other < m_entries.size();) {
            if (!m_entries[index].sameConfiguration(m_entries[other])) {
                ++other;
                continue;
            }
            // The list is newest-first, so the earlier entry is the one to keep.
            const RecentProjectPresetEntry duplicate = m_entries.takeAt(other);
            m_entries[index].useCount += qMax(1, duplicate.useCount);
            if (!m_entries[index].hasCustomName() && duplicate.hasCustomName()) {
                m_entries[index].projectName = duplicate.projectName;
            }
            changed = true;
        }
    }

    while (m_entries.size() > maxEntries()) {
        m_entries.removeLast();
        changed = true;
    }

    return changed;
}

void RecentProjectPresetsManager::load()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    m_entries.clear();

    const int count = settings.beginReadArray(QString::fromLatin1(kSettingsArray));
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);

        RecentProjectPresetEntry entry;
        entry.id = settings.value(QStringLiteral("id")).toString();
        entry.projectName = settings.value(QStringLiteral("projectName")).toString();
        entry.canvasSize = QSize(settings.value(QStringLiteral("canvasWidth")).toInt(),
            settings.value(QStringLiteral("canvasHeight")).toInt());
        entry.infiniteCanvasEnabled
            = settings.value(QStringLiteral("infiniteCanvasEnabled"), false).toBool();
        entry.colorMode = settings.value(QStringLiteral("colorMode")).toString();
        entry.backgroundColor
            = settings.value(QStringLiteral("backgroundColor"), QColor(Qt::white)).value<QColor>();
        if (!entry.backgroundColor.isValid()) {
            entry.backgroundColor = QColor(Qt::white);
        }
        entry.tileFormat
            = tileFormatFromInt(settings.value(QStringLiteral("tileFormat"), 0).toInt());
        entry.createdAt = settings.value(QStringLiteral("createdAt")).toDateTime();
        entry.useCount = qMax(1, settings.value(QStringLiteral("useCount"), 1).toInt());

        if (entry.isValid()) {
            m_entries.append(entry);
        }
    }
    settings.endArray();

    // Collapse whatever the pre-dedupe versions accumulated.
    if (normalizeEntries()) {
        save();
    }
}

void RecentProjectPresetsManager::save() const
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.beginWriteArray(QString::fromLatin1(kSettingsArray), m_entries.size());
    for (int index = 0; index < m_entries.size(); ++index) {
        settings.setArrayIndex(index);
        const RecentProjectPresetEntry& entry = m_entries[index];
        settings.setValue(QStringLiteral("id"), entry.id);
        settings.setValue(QStringLiteral("projectName"), entry.projectName);
        settings.setValue(QStringLiteral("canvasWidth"), entry.canvasSize.width());
        settings.setValue(QStringLiteral("canvasHeight"), entry.canvasSize.height());
        settings.setValue(QStringLiteral("infiniteCanvasEnabled"), entry.infiniteCanvasEnabled);
        settings.setValue(QStringLiteral("colorMode"), entry.colorMode);
        settings.setValue(QStringLiteral("backgroundColor"), entry.backgroundColor);
        settings.setValue(QStringLiteral("tileFormat"), static_cast<int>(entry.tileFormat));
        settings.setValue(QStringLiteral("createdAt"), entry.createdAt);
        settings.setValue(QStringLiteral("useCount"), entry.useCount);
    }
    settings.endArray();
}

} // namespace ruwa::core::serialization
