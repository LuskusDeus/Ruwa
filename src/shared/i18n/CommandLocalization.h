// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   L O C A L I Z A T I O N
// ======================================================================================
//   File        : CommandLocalization.h
//   Description : Loads command translations (title, category, aliases) from JSON
//                 per language. Enables searching commands in localized language
//                 (e.g. "бсп" for "Быстрое создание проекта").
// ======================================================================================

#ifndef RUWA_SHARED_I18N_COMMANDLOCALIZATION_H
#define RUWA_SHARED_I18N_COMMANDLOCALIZATION_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QHash>

namespace ruwa::i18n {

/**
 * @brief Stores localized strings for a single command
 */
struct CommandLocalizationEntry {
    QString title;
    QString category;
    QString description;
    QStringList aliases;
};

/**
 * @brief Loads and provides command translations from JSON files
 *
 * JSON format (translations/commands_XX.json):
 * {
 *   "file.new": {
 *     "title": "Новый проект",
 *     "category": "Файл",
 *     "aliases": ["нп", "создать", "fnp"]
 *   }
 *   ...
 * }
 *
 * Load commands_ru.json for Russian, commands_en.json for English (optional).
 * When no translation exists, callers use source CommandInfo from command.
 */
class CommandLocalization : public QObject {
    Q_OBJECT

public:
    static CommandLocalization& instance();

    /// Load translations for language code (e.g. "ru", "en")
    /// Call when language changes. For "en" or missing file, clears overlay.
    void load(const QString& languageCode);

    /// Get localized title for command ID. Empty = use source.
    QString title(const QString& commandId) const;

    /// Get localized category. Empty = use source.
    QString category(const QString& commandId) const;

    /// Get localized aliases. Empty = use source.
    QStringList aliases(const QString& commandId) const;

    /// Get localized description. Empty = use source.
    QString description(const QString& commandId) const;

    /// Get localized display name for a category (e.g. "File" -> "Файл")
    QString categoryDisplayName(const QString& sourceCategory) const;

    /// Whether we have any localization for this command
    bool hasLocalization(const QString& commandId) const;

signals:
    void localizationsChanged();

private:
    CommandLocalization();
    ~CommandLocalization() override = default;

    bool loadFromFile(const QString& path);
    bool loadFromResource(const QString& path);

    QHash<QString, CommandLocalizationEntry> m_entries;
    QHash<QString, QString> m_categoryNames;
    QString m_currentLanguage;
};

} // namespace ruwa::i18n

#endif // RUWA_SHARED_I18N_COMMANDLOCALIZATION_H
