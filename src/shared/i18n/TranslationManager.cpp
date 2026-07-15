// SPDX-License-Identifier: MPL-2.0

// TranslationManager.cpp
#include "TranslationManager.h"
#include "features/settings/SettingsManager.h"
#include "shared/i18n/CommandLocalization.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QMetaObject>
#include <QTranslator>
#include <QWidget>
#include <QtConcurrent>

namespace ruwa::ui::core {

TranslationManager::TranslationManager()
    : QObject(nullptr)
{
}

TranslationManager::~TranslationManager()
{
    if (m_translator && QCoreApplication::instance()) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }
}

TranslationManager& TranslationManager::instance()
{
    static TranslationManager instance;
    return instance;
}

QVector<TranslationManager::LanguageInfo> TranslationManager::availableLanguages() const
{
    return {
        { "en", "English" },
        { "ru", "Русский" },
    };
}

QString TranslationManager::findTranslationFile(const QString& baseName) const
{
    const QString fileName = baseName + ".qm";

    // 1. Embedded resources: :/i18n/<name>.qm (with alias)
    const QString resourcePath = ":/i18n/" + fileName;
    if (QFile::exists(resourcePath)) {
        return resourcePath;
    }

    // 1b. Embedded resources: :/i18n/translations/<name>.qm (without alias)
    const QString resourcePathAlt = ":/i18n/translations/" + fileName;
    if (QFile::exists(resourcePathAlt)) {
        return resourcePathAlt;
    }

    // 2. Build directory: <appDir>/translations/
    const QString appDir = QCoreApplication::applicationDirPath();
    QString path = appDir + "/translations/" + fileName;
    if (QFile::exists(path)) {
        return path;
    }

    // 3. Same directory as executable
    path = appDir + "/" + fileName;
    if (QFile::exists(path)) {
        return path;
    }

    return QString();
}

bool TranslationManager::loadLanguage(const QString& code)
{
    if (code == "en" || code.isEmpty()) {
        // English is the source language - remove translator
        if (m_translator && QCoreApplication::instance()) {
            QCoreApplication::removeTranslator(m_translator);
            delete m_translator;
            m_translator = nullptr;
        }
        m_currentLanguage = "en";
        return true;
    }

    const QString cachedKey = code.toLower();
    if (!m_translationCache.contains(cachedKey)) {
        const QString baseName = "ruwa_" + code;
        const QString filePath = findTranslationFile(baseName);
        if (filePath.isEmpty()) {
            return false;
        }

        const QByteArray data = readTranslationData(filePath);
        if (data.isEmpty()) {
            return false;
        }
        m_translationCache.insert(cachedKey, data);
        return applyLanguageData(code, data, filePath);
    }

    return applyLanguageData(code, m_translationCache.value(cachedKey), QStringLiteral("<cache>"));
}

void TranslationManager::initialize()
{
    if (m_initialized) {
        return;
    }

    // Load saved language from settings (SettingsManager must be ready)
    const auto& appSettings = ruwa::core::SettingsManager::instance().settings();
    QString savedLang = appSettings.appearance.language;

    loadLanguage(savedLang);
    ruwa::i18n::CommandLocalization::instance().load(m_currentLanguage);
    m_initialized = true;
}

bool TranslationManager::setLanguage(const QString& code)
{
    const QString normalizedCode
        = code.trimmed().toLower().isEmpty() ? QStringLiteral("en") : code.trimmed().toLower();
    const quint64 token = ++m_languageRequestToken;

    if (normalizedCode == m_currentLanguage) {
        return true;
    }

    if (normalizedCode == "en") {
        if (!loadLanguage(normalizedCode)) {
            return false;
        }

        // Persist to settings (async — disk I/O off the UI thread)
        ruwa::core::SettingsManager::instance().setLanguage(m_currentLanguage);
        ruwa::core::SettingsManager::instance().saveAsync();
        ruwa::i18n::CommandLocalization::instance().load(m_currentLanguage);
        emit languageChanged();
        postLanguageChangeEvents();
        return true;
    }

    (void) QtConcurrent::run([this, normalizedCode, token]() {
        const QString baseName = "ruwa_" + normalizedCode;
        const QString filePath = findTranslationFile(baseName);
        QByteArray translationData;
        if (!filePath.isEmpty()) {
            translationData = readTranslationData(filePath);
        }

        QMetaObject::invokeMethod(
            this,
            [this, normalizedCode, token, filePath, translationData]() {
                if (token != m_languageRequestToken.load()) {
                    return; // Outdated request
                }

                if (translationData.isEmpty()) {
                    return;
                }

                m_translationCache.insert(normalizedCode, translationData);
                if (!applyLanguageData(normalizedCode, translationData, filePath)) {
                    return;
                }

                // Persist to settings (async — disk I/O off the UI thread)
                ruwa::core::SettingsManager::instance().setLanguage(m_currentLanguage);
                ruwa::core::SettingsManager::instance().saveAsync();
                ruwa::i18n::CommandLocalization::instance().load(m_currentLanguage);
                emit languageChanged();
                postLanguageChangeEvents();
            },
            Qt::QueuedConnection);
    });

    return true;
}

QByteArray TranslationManager::readTranslationData(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool TranslationManager::applyLanguageData(
    const QString& code, const QByteArray& translationData, const QString& sourcePath)
{
    auto* translator = new QTranslator();
    if (!translator->load(
            reinterpret_cast<const uchar*>(translationData.constData()), translationData.size())) {
        delete translator;
        return false;
    }

    if (m_translator && QCoreApplication::instance()) {
        QCoreApplication::removeTranslator(m_translator);
        delete m_translator;
    }

    m_translator = translator;
    m_currentLanguage = code;
    if (QCoreApplication::instance()) {
        QCoreApplication::installTranslator(m_translator);
    }

    return true;
}

void TranslationManager::postLanguageChangeEvents() const
{
    // Qt only delivers LanguageChange to top-level widgets by default; child widgets
    // (settings, home content, preset cards, etc.) must receive it to retranslate.
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget* w : widgets) {
        if (w) {
            QCoreApplication::postEvent(w, new QEvent(QEvent::LanguageChange));
        }
    }
}

} // namespace ruwa::ui::core
