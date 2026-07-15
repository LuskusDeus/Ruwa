// SPDX-License-Identifier: MPL-2.0

#include "shared/utils/FileDialogMemory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>

namespace ruwa::shared::filedialog {

namespace {

constexpr auto kSettingsGroup = "FileDialogDirectories";

QString settingsKey(const QString& category)
{
    QString safe = category.trimmed();
    if (safe.isEmpty()) {
        safe = QStringLiteral("default");
    }
    return QStringLiteral("%1/%2").arg(QLatin1String(kSettingsGroup), safe);
}

// Builds the initial path handed to QFileDialog. A suggestion that already
// carries a directory wins (e.g. "Save As" on a project that has a path);
// otherwise the bare name is anchored to the remembered directory.
QString resolveInitialPath(const QString& category, const QString& suggestedName)
{
    if (!suggestedName.isEmpty()) {
        const QFileInfo info(suggestedName);
        const bool carriesDirectory
            = info.isAbsolute() || (!info.path().isEmpty() && info.path() != QStringLiteral("."));
        if (carriesDirectory) {
            return suggestedName;
        }
    }

    const QString dir = lastDirectory(category);
    if (dir.isEmpty()) {
        return suggestedName;
    }
    if (suggestedName.isEmpty()) {
        return dir;
    }
    return QDir(dir).filePath(suggestedName);
}

} // namespace

QString lastDirectory(const QString& category)
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    const QString dir = settings.value(settingsKey(category)).toString();
    if (dir.isEmpty() || !QDir(dir).exists()) {
        return {};
    }
    return dir;
}

void rememberFromPath(const QString& category, const QString& chosenPath)
{
    if (chosenPath.isEmpty()) {
        return;
    }
    const QString dir = QFileInfo(chosenPath).absolutePath();
    if (dir.isEmpty()) {
        return;
    }
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue(settingsKey(category), dir);
}

QString getSaveFileName(QWidget* parent, const QString& category, const QString& caption,
    const QString& suggestedName, const QString& filter, QString* selectedFilter)
{
    const QString initial = resolveInitialPath(category, suggestedName);
    const QString path
        = QFileDialog::getSaveFileName(parent, caption, initial, filter, selectedFilter);
    if (!path.isEmpty()) {
        rememberFromPath(category, path);
    }
    return path;
}

QString getOpenFileName(QWidget* parent, const QString& category, const QString& caption,
    const QString& filter, QString* selectedFilter)
{
    const QString initial = lastDirectory(category);
    const QString path
        = QFileDialog::getOpenFileName(parent, caption, initial, filter, selectedFilter);
    if (!path.isEmpty()) {
        rememberFromPath(category, path);
    }
    return path;
}

QStringList getOpenFileNames(QWidget* parent, const QString& category, const QString& caption,
    const QString& filter, QString* selectedFilter)
{
    const QString initial = lastDirectory(category);
    const QStringList paths
        = QFileDialog::getOpenFileNames(parent, caption, initial, filter, selectedFilter);
    if (!paths.isEmpty()) {
        rememberFromPath(category, paths.first());
    }
    return paths;
}

} // namespace ruwa::shared::filedialog
