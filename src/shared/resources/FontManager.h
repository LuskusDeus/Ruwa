// SPDX-License-Identifier: MPL-2.0

// FontManager.h
#ifndef RUWA_UI_CORE_RESOURCES_FONTMANAGER_H
#define RUWA_UI_CORE_RESOURCES_FONTMANAGER_H

#include "FontFamilyNames.h"

#include <QObject>
#include <QString>
#include <QMap>

namespace ruwa::ui::core {

/**
 * @brief Manages custom fonts for the application
 *
 * Features:
 * - Load custom fonts from resources
 * - Font fallback system
 * - Apply the active theme's default UI font to QApplication
 */
class FontManager : public QObject {
    Q_OBJECT

public:
    static FontManager& instance();

    /// Initialize and load custom fonts
    void initialize();

    /// Get UI font family name
    QString getUIFontFamily() const { return m_uiFontFamily; }

    /// Get code font family name
    QString getCodeFontFamily() const { return m_codeFontFamily; }

    /// Get title font family name
    QString getTitleFontFamily() const { return m_titleFontFamily; }

    /// Set custom UI font family
    void setUIFontFamily(const QString& family);

    /// Set custom code font family
    void setCodeFontFamily(const QString& family);

    /// Set custom title font family
    void setTitleFontFamily(const QString& family);

    /// Apply fonts to application (sets default application font)
    void applyToApplication(int pointSize);

signals:
    void fontsChanged();

private:
    FontManager();
    ~FontManager() override;

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;

    bool loadFont(const QString& resourcePath, QString& outFamily);
    void loadCustomFonts();
    void initializeDefaultFonts();

private:
    QString m_uiFontFamily { FontFamilyNames::SegoeUI }; ///< Default UI font
    QString m_codeFontFamily { FontFamilyNames::Consolas }; ///< Default code font
    QString m_titleFontFamily { FontFamilyNames::SegoeUI }; ///< Default title font
    QMap<QString, int> m_loadedFontIds; ///< Loaded font database IDs
    bool m_initialized { false };
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_RESOURCES_FONTMANAGER_H
