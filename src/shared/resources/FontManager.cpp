// SPDX-License-Identifier: MPL-2.0

// FontManager.cpp
#include "FontManager.h"
#include "FontFamilyNames.h"

#include <QFontDatabase>
#include <QFile>
#include <QApplication>

namespace ruwa::ui::core {

FontManager::FontManager()
    : QObject(nullptr)
{
}

FontManager::~FontManager() = default;

FontManager& FontManager::instance()
{
    static FontManager instance;
    return instance;
}

void FontManager::initialize()
{
    if (m_initialized) {
        return;
    }

    loadCustomFonts();
    initializeDefaultFonts();

    m_initialized = true;
}

QFont FontManager::getFont(FontType type) const
{
    switch (type) {
    case FontType::UI:
        return QFont(m_uiFontFamily, 9, QFont::Normal);

    case FontType::UIBold:
        return QFont(m_uiFontFamily, 9, QFont::Bold);

    case FontType::Title:
        return QFont(m_titleFontFamily, 16, QFont::Bold);

    case FontType::Subtitle:
        return QFont(m_uiFontFamily, 12, QFont::Medium);

    case FontType::Code:
        return QFont(m_codeFontFamily, 9, QFont::Normal);

    case FontType::Small:
        return QFont(m_uiFontFamily, 8, QFont::Normal);

    case FontType::Caption:
        return QFont(m_uiFontFamily, 7, QFont::Normal);

    default:
        return QFont(m_uiFontFamily, 9, QFont::Normal);
    }
}

QFont FontManager::getFont(FontType type, int pointSize) const
{
    QFont font = getFont(type);
    font.setPointSize(pointSize);
    return font;
}

QFont FontManager::getFont(FontType type, int pointSize, FontWeight weight) const
{
    QFont font = getFont(type, pointSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

QFont FontManager::getCodeFont(int pointSize, FontWeight weight, bool italic) const
{
    QFont font(m_codeFontFamily, pointSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    font.setItalic(italic);
    return font;
}

QFont FontManager::getTitleFont(int pointSize, FontWeight weight) const
{
    QFont font(m_titleFontFamily, pointSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    return font;
}

void FontManager::setUIFontFamily(const QString& family)
{
    if (m_uiFontFamily != family) {
        m_uiFontFamily = family;
        emit fontsChanged();
    }
}

void FontManager::setCodeFontFamily(const QString& family)
{
    if (m_codeFontFamily != family) {
        m_codeFontFamily = family;
        emit fontsChanged();
    }
}

void FontManager::setTitleFontFamily(const QString& family)
{
    if (m_titleFontFamily != family) {
        m_titleFontFamily = family;
        emit fontsChanged();
    }
}

void FontManager::applyToApplication()
{
    // Set default application font
    QFont appFont = getFont(FontType::UI);
    QApplication::setFont(appFont);
}

bool FontManager::loadFont(const QString& resourcePath, QString& outFamily)
{
    if (!QFile::exists(resourcePath)) {
        return false;
    }

    int fontId = QFontDatabase::addApplicationFont(resourcePath);

    if (fontId == -1) {
        return false;
    }

    QStringList families = QFontDatabase::applicationFontFamilies(fontId);

    if (families.isEmpty()) {
        return false;
    }

    outFamily = families.first();
    m_loadedFontIds[resourcePath] = fontId;

    return true;
}

void FontManager::loadCustomFonts()
{
    QString loadedFamily;

    // === JetBrains Mono (Primary monospace) ===
    if (loadFont(":/fonts/JetBrainsMono-Regular", loadedFamily)) {
        m_uiFontFamily = loadedFamily;
        m_codeFontFamily = loadedFamily;
    }

    QString temp;
    loadFont(":/fonts/JetBrainsMono-Light", temp);
    loadFont(":/fonts/JetBrainsMono-Medium", temp);
    loadFont(":/fonts/JetBrainsMono-Bold", temp);
    loadFont(":/fonts/JetBrainsMono-Italic", temp);

    // === Manrope (Luxury theme) ===
    loadFont(":/fonts/Manrope-Regular", temp);
    loadFont(":/fonts/Manrope-Medium", temp);
    loadFont(":/fonts/Manrope-SemiBold", temp);
    loadFont(":/fonts/Manrope-Bold", temp);
    loadFont(":/fonts/Manrope-ExtraLight", temp);
    loadFont(":/fonts/Manrope-Light", temp);
    loadFont(":/fonts/Manrope-ExtraBold", temp);

    // === IBM Plex Sans Condensed (Technical, modern titles) ===
    if (loadFont(":/fonts/IBMPlexSans-Regular", loadedFamily)) {
        m_titleFontFamily = loadedFamily;
    }
    loadFont(":/fonts/IBMPlexSans-Light", temp);
    loadFont(":/fonts/IBMPlexSans-Bold", temp);
    loadFont(":/fonts/IBMPlexSans-Thin", temp);
    // === Comfortaa (Soft, elegant) ===
    loadFont(":/fonts/Comfortaa-Regular", temp);
    loadFont(":/fonts/Comfortaa-Light", temp);
    loadFont(":/fonts/Comfortaa-Medium", temp);
    loadFont(":/fonts/Comfortaa-Bold", temp);
    // === Instrument Serif (Titles for Obsidian/Graphite) ===
    loadFont(":/fonts/InstrumentSerif-Regular", temp);
    loadFont(":/fonts/InstrumentSerif-Italic", temp);
    // === DM Sans 18pt (UI text for Obsidian/Graphite) ===
    loadFont(":/fonts/DMSans18pt-Regular", temp);
    loadFont(":/fonts/DMSans18pt-SemiBold", temp);
}

void FontManager::initializeDefaultFonts()
{
    // If JetBrains Mono wasn't loaded, use system fallbacks for UI
    if (m_uiFontFamily == FontFamilyNames::SegoeUI || m_uiFontFamily.isEmpty()) {
        QStringList fallbacks = { FontFamilyNames::JetBrainsMono, FontFamilyNames::FiraCode,
            FontFamilyNames::Consolas, FontFamilyNames::SFMono, FontFamilyNames::SegoeUI,
            FontFamilyNames::Arial, "sans-serif" };

        for (const QString& family : fallbacks) {
            if (QFontDatabase::hasFamily(family)) {
                m_uiFontFamily = family;
                break;
            }
        }
    }

    // Code font fallback
    if (m_codeFontFamily == FontFamilyNames::Consolas || m_codeFontFamily.isEmpty()) {
        QStringList codeFallbacks = { FontFamilyNames::JetBrainsMono, FontFamilyNames::FiraCode,
            FontFamilyNames::Consolas, FontFamilyNames::SFMono, FontFamilyNames::Monaco,
            FontFamilyNames::CourierNew, "monospace" };

        for (const QString& family : codeFallbacks) {
            if (QFontDatabase::hasFamily(family)) {
                m_codeFontFamily = family;
                break;
            }
        }
    }

    // Title font fallback
    if (m_titleFontFamily.isEmpty() || m_titleFontFamily == FontFamilyNames::SegoeUI) {
        QStringList titleFallbacks = { FontFamilyNames::IBMPlexSansCondensed,
            FontFamilyNames::Manrope, FontFamilyNames::InstrumentSerif, FontFamilyNames::Impact,
            FontFamilyNames::ArialBlack, FontFamilyNames::HelveticaNeue, FontFamilyNames::SegoeUI,
            "sans-serif" };

        for (const QString& family : titleFallbacks) {
            if (QFontDatabase::hasFamily(family)) {
                m_titleFontFamily = family;
                break;
            }
        }
    }
}

} // namespace ruwa::ui::core
