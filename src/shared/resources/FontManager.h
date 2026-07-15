// SPDX-License-Identifier: MPL-2.0

// FontManager.h
#ifndef RUWA_UI_CORE_RESOURCES_FONTMANAGER_H
#define RUWA_UI_CORE_RESOURCES_FONTMANAGER_H

#include "FontFamilyNames.h"

#include <QObject>
#include <QFont>
#include <QString>
#include <QMap>

namespace ruwa::ui::core {

/**
 * @brief Manages custom fonts for the application
 *
 * Features:
 * - Load custom fonts from resources
 * - Provide semantic font types (UI, Code, Title, etc.)
 * - Font fallback system
 * - Weight and size variants
 */
class FontManager : public QObject {
    Q_OBJECT

public:
    /// Semantic font types
    enum class FontType {
        UI, ///< Standard UI text
        UIBold, ///< Bold UI text
        Title, ///< Large titles (Instrument Serif for Obsidian/Graphite)
        Subtitle, ///< Subtitles
        Code, ///< Monospace code font
        Small, ///< Small UI text
        Caption ///< Very small text
    };

    /// Font weight
    enum class FontWeight {
        Light = QFont::Light,
        Normal = QFont::Normal,
        Medium = QFont::Medium,
        SemiBold = QFont::DemiBold,
        Bold = QFont::Bold,
        ExtraBold = QFont::ExtraBold
    };

    static FontManager& instance();

    /// Initialize and load custom fonts
    void initialize();

    /// Get font by semantic type
    QFont getFont(FontType type) const;

    /// Get custom font with specific size
    QFont getFont(FontType type, int pointSize) const;

    /// Get custom font with size and weight
    QFont getFont(FontType type, int pointSize, FontWeight weight) const;

    /// Get code font with italic option (for comments, emphasis)
    QFont getCodeFont(
        int pointSize = 9, FontWeight weight = FontWeight::Normal, bool italic = false) const;

    /// Get title font with custom size and weight
    QFont getTitleFont(int pointSize = 16, FontWeight weight = FontWeight::Bold) const;

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
    void applyToApplication();

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
