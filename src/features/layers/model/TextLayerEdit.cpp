// SPDX-License-Identifier: MPL-2.0

#include "TextLayerEdit.h"

#include <QCoreApplication>

namespace ruwa::core::layers {

namespace {

/// Changes the layer-wide defaults without changing how any existing character
/// looks: the current per-character styles are read first and written back as
/// runs against the new defaults.
template <typename Mutator>
void mutateDefaultsPreservingCharacters(TextLayerData& textData, Mutator mutator)
{
    const QVector<TextCharStyle> styles = textCharacterStyles(textData);
    mutator(textData);
    rebuildTextStyleRuns(textData, styles);
}

TextStyleEffect effectForProperty(TextLayerEdit::Property property)
{
    switch (property) {
    case TextLayerEdit::Property::Italic:
        return TextStyleEffect::Italic;
    case TextLayerEdit::Property::Underline:
        return TextStyleEffect::Underline;
    case TextLayerEdit::Property::Strikethrough:
        return TextStyleEffect::Strikethrough;
    default:
        break;
    }
    return TextStyleEffect::Bold;
}

void applyCharacterEdit(TextLayerData& textData, const TextLayerEdit& edit, int from, int to)
{
    switch (edit.property) {
    case TextLayerEdit::Property::FontFamily:
        applyTextFontFamilyToRange(textData, from, to, edit.stringValue);
        return;
    case TextLayerEdit::Property::FontSize:
        applyTextFontSizeToRange(textData, from, to, edit.numberValue);
        return;
    case TextLayerEdit::Property::Color:
        applyTextColorToRange(textData, from, to, edit.colorValue);
        return;
    case TextLayerEdit::Property::Tracking:
        applyTextTrackingToRange(textData, from, to, edit.numberValue);
        return;
    case TextLayerEdit::Property::Caps:
        applyTextCapsToRange(textData, from, to, static_cast<TextCaps>(edit.enumValue));
        return;
    case TextLayerEdit::Property::Bold:
    case TextLayerEdit::Property::Italic:
    case TextLayerEdit::Property::Underline:
    case TextLayerEdit::Property::Strikethrough:
        applyTextEffectToRange(
            textData, from, to, effectForProperty(edit.property), edit.boolValue);
        return;
    default:
        break;
    }
}

/// The same character property written into the layer's defaults. Bold and
/// italic have no layer-wide default (they only exist per run), so they are the
/// one pair this cannot express.
void applyCharacterDefault(TextLayerData& textData, const TextLayerEdit& edit)
{
    switch (edit.property) {
    case TextLayerEdit::Property::FontFamily:
        if (!edit.stringValue.isEmpty()) {
            textData.fontFamily = edit.stringValue;
        }
        return;
    case TextLayerEdit::Property::FontSize:
        textData.fontSize = qMax<qreal>(1.0, edit.numberValue);
        return;
    case TextLayerEdit::Property::Color:
        if (edit.colorValue.isValid()) {
            textData.color = edit.colorValue;
        }
        return;
    case TextLayerEdit::Property::Tracking:
        textData.tracking = edit.numberValue;
        return;
    case TextLayerEdit::Property::Caps:
        textData.caps = static_cast<TextCaps>(edit.enumValue);
        return;
    case TextLayerEdit::Property::Strikethrough:
        textData.strikethrough = edit.boolValue;
        return;
    default:
        break;
    }
}

void applyParagraphEdit(TextLayerData& textData, const TextLayerEdit& edit)
{
    switch (edit.property) {
    case TextLayerEdit::Property::Alignment:
        textData.alignment = static_cast<TextAlignment>(edit.enumValue);
        return;
    case TextLayerEdit::Property::LineHeight:
        textData.lineHeight = qMax<qreal>(0.1, edit.numberValue);
        return;
    case TextLayerEdit::Property::SpaceBefore:
        textData.spaceBefore = qMax<qreal>(0.0, edit.numberValue);
        return;
    case TextLayerEdit::Property::SpaceAfter:
        textData.spaceAfter = qMax<qreal>(0.0, edit.numberValue);
        return;
    default:
        break;
    }
}

} // namespace

QString textLayerEditLabel(TextLayerEdit::Property property)
{
    switch (property) {
    case TextLayerEdit::Property::FontFamily:
        return QCoreApplication::translate("TextLayerEdit", "Change Font");
    case TextLayerEdit::Property::FontSize:
        return QCoreApplication::translate("TextLayerEdit", "Change Font Size");
    case TextLayerEdit::Property::Color:
        return QCoreApplication::translate("TextLayerEdit", "Change Text Color");
    case TextLayerEdit::Property::Bold:
        return QCoreApplication::translate("TextLayerEdit", "Bold");
    case TextLayerEdit::Property::Italic:
        return QCoreApplication::translate("TextLayerEdit", "Italic");
    case TextLayerEdit::Property::Underline:
        return QCoreApplication::translate("TextLayerEdit", "Underline");
    case TextLayerEdit::Property::Strikethrough:
        return QCoreApplication::translate("TextLayerEdit", "Strikethrough");
    case TextLayerEdit::Property::Tracking:
        return QCoreApplication::translate("TextLayerEdit", "Change Tracking");
    case TextLayerEdit::Property::Caps:
        return QCoreApplication::translate("TextLayerEdit", "Change Capitalization");
    case TextLayerEdit::Property::Alignment:
        return QCoreApplication::translate("TextLayerEdit", "Change Text Alignment");
    case TextLayerEdit::Property::LineHeight:
        return QCoreApplication::translate("TextLayerEdit", "Change Leading");
    case TextLayerEdit::Property::SpaceBefore:
    case TextLayerEdit::Property::SpaceAfter:
        return QCoreApplication::translate("TextLayerEdit", "Change Paragraph Spacing");
    }
    return QCoreApplication::translate("TextLayerEdit", "Edit Text");
}

void applyTextLayerEdit(TextLayerData& textData, const TextLayerEdit& edit, int from, int to)
{
    if (!edit.isCharacterProperty()) {
        applyParagraphEdit(textData, edit);
        return;
    }

    const int size = static_cast<int>(textData.text.size());
    const int start = qBound(0, std::min(from, to), size);
    const int end = qBound(0, std::max(from, to), size);

    if (start >= end) {
        // Caret with nothing selected: only what gets typed next changes.
        mutateDefaultsPreservingCharacters(
            textData, [&edit](TextLayerData& data) { applyCharacterDefault(data, edit); });
        return;
    }

    applyCharacterEdit(textData, edit, start, end);
    if (start == 0 && end == size) {
        // The whole layer was retyped in one attribute, so the defaults follow
        // it and the runs collapse back to nothing.
        const QVector<TextCharStyle> styles = textCharacterStyles(textData);
        applyCharacterDefault(textData, edit);
        rebuildTextStyleRuns(textData, styles);
    }
}

} // namespace ruwa::core::layers
