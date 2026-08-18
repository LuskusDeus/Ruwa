// SPDX-License-Identifier: MPL-2.0

#include "RecentProjectPresetsManager.h"

#include <QCoreApplication>
#include <QSettings>
#include <QUuid>

namespace ruwa::core::serialization {

namespace {
constexpr auto kSettingsArray = "RecentProjectPresets";

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

RecentProjectPresetsManager& RecentProjectPresetsManager::instance()
{
    static RecentProjectPresetsManager manager;
    return manager;
}

RecentProjectPresetsManager::RecentProjectPresetsManager()
{
    load();
}

void RecentProjectPresetsManager::addEntry(const QString& projectName, const QSize& canvasSize,
    bool infiniteCanvasEnabled, const QString& colorMode, const QColor& backgroundColor,
    aether::TilePixelFormat tileFormat)
{
    RecentProjectPresetEntry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.projectName = projectName.trimmed();
    if (entry.projectName.isEmpty()) {
        entry.projectName = QCoreApplication::translate(
            "ruwa::ui::widgets::NewProjectContent", "Untitled Project");
    }
    entry.canvasSize = canvasSize;
    entry.infiniteCanvasEnabled = infiniteCanvasEnabled;
    entry.colorMode = colorMode;
    entry.backgroundColor = backgroundColor.isValid() ? backgroundColor : QColor(Qt::white);
    entry.tileFormat = tileFormat;
    entry.createdAt = QDateTime::currentDateTime();

    if (!entry.isValid()) {
        return;
    }

    m_entries.prepend(entry);
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

        if (entry.isValid()) {
            m_entries.append(entry);
        }
    }
    settings.endArray();
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
    }
    settings.endArray();
}

} // namespace ruwa::core::serialization
