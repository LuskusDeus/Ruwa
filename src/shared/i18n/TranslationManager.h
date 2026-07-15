// SPDX-License-Identifier: MPL-2.0

// TranslationManager.h
#ifndef RUWA_UI_CORE_TRANSLATION_TRANSLATIONMANAGER_H
#define RUWA_UI_CORE_TRANSLATION_TRANSLATIONMANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QHash>
#include <QByteArray>
#include <atomic>

class QTranslator;

namespace ruwa::ui::core {

/**
 * @brief Singleton manager for application translations (i18n)
 *
 * Manages language loading, switching, and persistence.
 * Uses Qt Linguist .ts/.qm files.
 *
 * Usage:
 *   TranslationManager::instance().initialize();
 *   TranslationManager::instance().setLanguage("ru");
 */
class TranslationManager : public QObject {
    Q_OBJECT

public:
    static TranslationManager& instance();

    /// Initialize and load saved language (call before any UI is created)
    void initialize();

    /// Get list of available languages (locale code, display name)
    struct LanguageInfo {
        QString code; // e.g. "en", "ru"
        QString name; // e.g. "English", "Русский"
    };
    QVector<LanguageInfo> availableLanguages() const;

    /// Get current language code
    QString currentLanguage() const { return m_currentLanguage; }

    /// Set language by code. Emits languageChanged() on success.
    /// Returns true if language change request was accepted.
    bool setLanguage(const QString& code);

signals:
    /// Emitted when language changes (widgets should retranslate)
    void languageChanged();

private:
    TranslationManager();
    ~TranslationManager() override;

    TranslationManager(const TranslationManager&) = delete;
    TranslationManager& operator=(const TranslationManager&) = delete;

    /// Search paths for .qm files (build dir, app dir, resources)
    QString findTranslationFile(const QString& baseName) const;

    /// Remove current translator, load new one
    bool loadLanguage(const QString& code);

    /// Load translation file bytes on background thread
    static QByteArray readTranslationData(const QString& filePath);

    /// Apply already-loaded translation bytes in UI thread
    bool applyLanguageData(
        const QString& code, const QByteArray& translationData, const QString& sourcePath);

    /// Queue LanguageChange event to top-level widgets (non-blocking)
    void postLanguageChangeEvents() const;

private:
    QTranslator* m_translator = nullptr;
    QString m_currentLanguage;
    bool m_initialized = false;
    QHash<QString, QByteArray> m_translationCache;
    std::atomic<quint64> m_languageRequestToken { 0 };
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_TRANSLATION_TRANSLATIONMANAGER_H
