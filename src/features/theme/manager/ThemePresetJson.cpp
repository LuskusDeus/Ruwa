// SPDX-License-Identifier: MPL-2.0

#include "ThemePresetJson.h"

#include <QCoreApplication>

namespace ruwa::ui::core::theme_preset_json {

namespace {

QString translated(const char* text)
{
    return QCoreApplication::translate("ThemePresetJson", text);
}

QJsonObject fontSizesToJson(const ThemeFontSizes& sizes)
{
    QJsonObject object;
    object[QStringLiteral("display")] = sizes.display;
    object[QStringLiteral("h0")] = sizes.h0;
    object[QStringLiteral("h1")] = sizes.h1;
    object[QStringLiteral("h2")] = sizes.h2;
    object[QStringLiteral("h3")] = sizes.h3;
    object[QStringLiteral("h4")] = sizes.h4;
    object[QStringLiteral("h5")] = sizes.h5;
    object[QStringLiteral("h6")] = sizes.h6;
    object[QStringLiteral("bodyLarge")] = sizes.bodyLarge;
    object[QStringLiteral("label")] = sizes.label;
    object[QStringLiteral("body")] = sizes.body;
    object[QStringLiteral("small")] = sizes.small;
    object[QStringLiteral("caption")] = sizes.caption;
    object[QStringLiteral("code")] = sizes.code;
    return object;
}

ThemeFontSizes fontSizesFromJson(const QJsonObject& fonts)
{
    const ThemeFontSizes fallback
        = ThemeFontSizes::fromLegacy(fonts.value(QStringLiteral("uiSize")).toInt(9),
            fonts.value(QStringLiteral("codeSize")).toInt(9),
            fonts.value(QStringLiteral("titleSize")).toInt(16));
    const QJsonObject object = fonts.value(QStringLiteral("sizes")).toObject();
    if (object.isEmpty()) {
        return fallback;
    }

    ThemeFontSizes sizes;
    sizes.display = object.value(QStringLiteral("display")).toInt(fallback.display);
    sizes.h0 = object.value(QStringLiteral("h0")).toInt(fallback.h0);
    sizes.h1 = object.value(QStringLiteral("h1")).toInt(fallback.h1);
    sizes.h2 = object.value(QStringLiteral("h2")).toInt(fallback.h2);
    sizes.h3 = object.value(QStringLiteral("h3")).toInt(fallback.h3);
    sizes.h4 = object.value(QStringLiteral("h4")).toInt(fallback.h4);
    sizes.h5 = object.value(QStringLiteral("h5")).toInt(fallback.h5);
    sizes.h6 = object.value(QStringLiteral("h6")).toInt(fallback.h6);
    sizes.bodyLarge = object.value(QStringLiteral("bodyLarge")).toInt(fallback.bodyLarge);
    sizes.label = object.value(QStringLiteral("label")).toInt(fallback.label);
    sizes.body = object.value(QStringLiteral("body")).toInt(fallback.body);
    sizes.small = object.value(QStringLiteral("small")).toInt(fallback.small);
    sizes.caption = object.value(QStringLiteral("caption")).toInt(fallback.caption);
    sizes.code = object.value(QStringLiteral("code")).toInt(fallback.code);
    sizes.normalize();
    return sizes;
}

QJsonObject presetToJson(const ThemePreset& preset)
{
    QJsonObject object;
    object[QStringLiteral("id")] = preset.id.toString(QUuid::WithoutBraces);
    object[QStringLiteral("name")] = preset.name;
    object[QStringLiteral("description")] = preset.description;
    object[QStringLiteral("isDark")] = preset.isDark;
    object[QStringLiteral("isFavorite")] = preset.isFavorite;

    object[QStringLiteral("primary")] = preset.primary.name(QColor::HexArgb);
    object[QStringLiteral("background")] = preset.background.name(QColor::HexArgb);
    object[QStringLiteral("surface")] = preset.surface.name(QColor::HexArgb);
    object[QStringLiteral("surfaceAlt")] = preset.surfaceAlt.name(QColor::HexArgb);
    object[QStringLiteral("border")] = preset.border.name(QColor::HexArgb);
    object[QStringLiteral("accent")] = preset.accent.name(QColor::HexArgb);
    object[QStringLiteral("text")] = preset.text.name(QColor::HexArgb);
    object[QStringLiteral("textMuted")] = preset.textMuted.name(QColor::HexArgb);
    object[QStringLiteral("textOnPrimary")] = preset.textOnPrimary.name(QColor::HexArgb);
    object[QStringLiteral("overlayColor")] = preset.overlayColor.name(QColor::HexArgb);
    object[QStringLiteral("success")] = preset.success.name(QColor::HexArgb);
    object[QStringLiteral("warning")] = preset.warning.name(QColor::HexArgb);
    object[QStringLiteral("error")] = preset.error.name(QColor::HexArgb);
    object[QStringLiteral("info")] = preset.info.name(QColor::HexArgb);

    QJsonObject fonts;
    fonts[QStringLiteral("ui")] = preset.fonts.uiFont;
    fonts[QStringLiteral("code")] = preset.fonts.codeFont;
    fonts[QStringLiteral("title")] = preset.fonts.titleFont;
    fonts[QStringLiteral("sizes")] = fontSizesToJson(preset.fonts.sizes);
    // Legacy anchors keep exported presets useful in older Ruwa versions.
    fonts[QStringLiteral("uiSize")] = preset.fonts.sizes.body;
    fonts[QStringLiteral("codeSize")] = preset.fonts.sizes.code;
    fonts[QStringLiteral("titleSize")] = preset.fonts.sizes.h4;
    object[QStringLiteral("fonts")] = fonts;

    return object;
}

bool readRequiredColor(
    const QJsonObject& object, const QString& key, QColor& color, QString* errorMessage)
{
    color = QColor(object.value(key).toString());
    if (color.isValid()) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = translated("Invalid color value: %1").arg(key);
    }
    return false;
}

} // namespace

QJsonObject toDocumentObject(const ThemePreset& preset)
{
    QJsonObject document;
    document[QStringLiteral("format")] = QStringLiteral("ruwa-theme-preset");
    // The new typography scale is an additive extension. Legacy anchors are
    // still emitted, so the document remains compatible with format v1.
    document[QStringLiteral("version")] = 1;
    document[QStringLiteral("preset")] = presetToJson(preset);
    return document;
}

bool fromDocumentObject(const QJsonObject& document, ThemePreset& preset, QString* errorMessage)
{
    if (document.value(QStringLiteral("format")).toString()
        != QStringLiteral("ruwa-theme-preset")) {
        if (errorMessage) {
            *errorMessage = translated("Invalid file format.");
        }
        return false;
    }

    if (document.value(QStringLiteral("version")).toInt() != 1) {
        if (errorMessage) {
            *errorMessage = translated("Unsupported preset version.");
        }
        return false;
    }

    const QJsonObject object = document.value(QStringLiteral("preset")).toObject();
    if (object.isEmpty()) {
        if (errorMessage) {
            *errorMessage = translated("Missing preset data.");
        }
        return false;
    }

    const QString name = object.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        if (errorMessage) {
            *errorMessage = translated("Preset has no name.");
        }
        return false;
    }

    ThemePreset imported;
    imported.id = QUuid::createUuid();
    imported.name = name;
    imported.description = object.value(QStringLiteral("description")).toString();
    imported.isBuiltIn = false;
    imported.isDark = object.value(QStringLiteral("isDark")).toBool(true);
    imported.isFavorite = object.value(QStringLiteral("isFavorite")).toBool(false);

    if (!readRequiredColor(object, QStringLiteral("primary"), imported.primary, errorMessage)
        || !readRequiredColor(
            object, QStringLiteral("background"), imported.background, errorMessage)
        || !readRequiredColor(object, QStringLiteral("surface"), imported.surface, errorMessage)
        || !readRequiredColor(
            object, QStringLiteral("surfaceAlt"), imported.surfaceAlt, errorMessage)
        || !readRequiredColor(object, QStringLiteral("border"), imported.border, errorMessage)
        || !readRequiredColor(object, QStringLiteral("text"), imported.text, errorMessage)
        || !readRequiredColor(object, QStringLiteral("textMuted"), imported.textMuted, errorMessage)
        || !readRequiredColor(
            object, QStringLiteral("textOnPrimary"), imported.textOnPrimary, errorMessage)
        || !readRequiredColor(
            object, QStringLiteral("overlayColor"), imported.overlayColor, errorMessage)
        || !readRequiredColor(object, QStringLiteral("success"), imported.success, errorMessage)
        || !readRequiredColor(object, QStringLiteral("warning"), imported.warning, errorMessage)
        || !readRequiredColor(object, QStringLiteral("error"), imported.error, errorMessage)
        || !readRequiredColor(object, QStringLiteral("info"), imported.info, errorMessage)) {
        return false;
    }

    imported.accent = QColor(object.value(QStringLiteral("accent")).toString());
    if (!imported.accent.isValid()) {
        imported.accent = QColor(124, 92, 252);
    }

    const QJsonObject fonts = object.value(QStringLiteral("fonts")).toObject();
    if (!fonts.isEmpty()) {
        imported.fonts.uiFont = fonts.value(QStringLiteral("ui")).toString(imported.fonts.uiFont);
        imported.fonts.codeFont
            = fonts.value(QStringLiteral("code")).toString(imported.fonts.codeFont);
        imported.fonts.titleFont
            = fonts.value(QStringLiteral("title")).toString(imported.fonts.titleFont);
        imported.fonts.sizes = fontSizesFromJson(fonts);
    }
    imported.fonts.uiFont = FontFamilyNames::migrateLegacyFamilyName(imported.fonts.uiFont);
    imported.fonts.codeFont = FontFamilyNames::migrateLegacyFamilyName(imported.fonts.codeFont);
    imported.fonts.titleFont = FontFamilyNames::migrateLegacyFamilyName(imported.fonts.titleFont);

    preset = imported;
    return true;
}

} // namespace ruwa::ui::core::theme_preset_json
