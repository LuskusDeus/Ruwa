// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   L O C A L I Z A T I O N
// ======================================================================================

#include "CommandLocalization.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
namespace ruwa::i18n {

CommandLocalization& CommandLocalization::instance()
{
    static CommandLocalization instance;
    return instance;
}

CommandLocalization::CommandLocalization()
    : QObject(nullptr)
{
}

void CommandLocalization::load(const QString& languageCode)
{
    m_currentLanguage = languageCode.trimmed().toLower();
    m_entries.clear();
    m_categoryNames.clear();

    if (m_currentLanguage.isEmpty() || m_currentLanguage == QStringLiteral("en")) {
        emit localizationsChanged();
        return;
    }

    const QString baseName = QStringLiteral("commands_%1").arg(m_currentLanguage);
    const QString jsonFileName = baseName + QStringLiteral(".json");

    // 1. Resource: :/i18n/commands_ru.json
    const QString resourcePath = QStringLiteral(":/i18n/") + jsonFileName;
    if (loadFromResource(resourcePath)) {
        emit localizationsChanged();
        return;
    }

    // 2. translations/commands_ru.json
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList searchPaths = {
        appDir + QStringLiteral("/translations/") + jsonFileName,
        appDir + QStringLiteral("/") + jsonFileName,
    };

    // 3. Build/source translations dir (for development)
    QDir projectDir(appDir);
    if (projectDir.cdUp()) {
        searchPaths << projectDir.absolutePath() + QStringLiteral("/translations/") + jsonFileName;
    }

    for (const QString& path : searchPaths) {
        if (loadFromFile(path)) {
            emit localizationsChanged();
            return;
        }
    }

    emit localizationsChanged();
}

bool CommandLocalization::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();

    // Load category name mappings if present
    if (root.contains(QStringLiteral("categories"))
        && root[QStringLiteral("categories")].isObject()) {
        QJsonObject cats = root[QStringLiteral("categories")].toObject();
        for (auto cit = cats.begin(); cit != cats.end(); ++cit) {
            QString trans = cit.value().toString().trimmed();
            if (!trans.isEmpty()) {
                m_categoryNames.insert(cit.key(), trans);
            }
        }
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const QString cmdId = it.key();
        if (cmdId == QStringLiteral("categories"))
            continue;
        QJsonValue val = it.value();
        if (!val.isObject())
            continue;

        QJsonObject obj = val.toObject();
        CommandLocalizationEntry entry;

        if (obj.contains(QStringLiteral("title"))) {
            entry.title = obj[QStringLiteral("title")].toString();
        }
        if (obj.contains(QStringLiteral("category"))) {
            entry.category = obj[QStringLiteral("category")].toString();
        }
        if (obj.contains(QStringLiteral("description"))) {
            entry.description = obj[QStringLiteral("description")].toString();
        }
        if (obj.contains(QStringLiteral("aliases"))) {
            QJsonArray arr = obj[QStringLiteral("aliases")].toArray();
            for (const QJsonValue& v : arr) {
                QString a = v.toString().trimmed();
                if (!a.isEmpty()) {
                    entry.aliases.append(a);
                }
            }
        }

        if (!entry.title.isEmpty() || !entry.category.isEmpty() || !entry.description.isEmpty()
            || !entry.aliases.isEmpty()) {
            m_entries.insert(cmdId, entry);
        }
    }

    return !m_entries.isEmpty() || !m_categoryNames.isEmpty();
}

bool CommandLocalization::loadFromResource(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();

    if (root.contains(QStringLiteral("categories"))
        && root[QStringLiteral("categories")].isObject()) {
        QJsonObject cats = root[QStringLiteral("categories")].toObject();
        for (auto cit = cats.begin(); cit != cats.end(); ++cit) {
            QString trans = cit.value().toString().trimmed();
            if (!trans.isEmpty()) {
                m_categoryNames.insert(cit.key(), trans);
            }
        }
    }

    for (auto it = root.begin(); it != root.end(); ++it) {
        const QString cmdId = it.key();
        if (cmdId == QStringLiteral("categories"))
            continue;
        QJsonValue val = it.value();
        if (!val.isObject())
            continue;

        QJsonObject obj = val.toObject();
        CommandLocalizationEntry entry;

        if (obj.contains(QStringLiteral("title"))) {
            entry.title = obj[QStringLiteral("title")].toString();
        }
        if (obj.contains(QStringLiteral("category"))) {
            entry.category = obj[QStringLiteral("category")].toString();
        }
        if (obj.contains(QStringLiteral("description"))) {
            entry.description = obj[QStringLiteral("description")].toString();
        }
        if (obj.contains(QStringLiteral("aliases"))) {
            QJsonArray arr = obj[QStringLiteral("aliases")].toArray();
            for (const QJsonValue& v : arr) {
                QString a = v.toString().trimmed();
                if (!a.isEmpty()) {
                    entry.aliases.append(a);
                }
            }
        }

        if (!entry.title.isEmpty() || !entry.category.isEmpty() || !entry.description.isEmpty()
            || !entry.aliases.isEmpty()) {
            m_entries.insert(cmdId, entry);
        }
    }

    return !m_entries.isEmpty() || !m_categoryNames.isEmpty();
}

QString CommandLocalization::title(const QString& commandId) const
{
    auto it = m_entries.find(commandId);
    return (it != m_entries.end()) ? it->title : QString();
}

QString CommandLocalization::category(const QString& commandId) const
{
    auto it = m_entries.find(commandId);
    return (it != m_entries.end()) ? it->category : QString();
}

QStringList CommandLocalization::aliases(const QString& commandId) const
{
    auto it = m_entries.find(commandId);
    return (it != m_entries.end()) ? it->aliases : QStringList();
}

QString CommandLocalization::description(const QString& commandId) const
{
    auto it = m_entries.find(commandId);
    return (it != m_entries.end()) ? it->description : QString();
}

QString CommandLocalization::categoryDisplayName(const QString& sourceCategory) const
{
    auto it = m_categoryNames.find(sourceCategory);
    return (it != m_categoryNames.end()) ? it.value() : QString();
}

bool CommandLocalization::hasLocalization(const QString& commandId) const
{
    return m_entries.contains(commandId);
}

} // namespace ruwa::i18n
