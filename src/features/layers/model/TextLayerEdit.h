// SPDX-License-Identifier: MPL-2.0

// TextLayerEdit.h
#ifndef RUWA_CORE_LAYERS_TEXTLAYEREDIT_H
#define RUWA_CORE_LAYERS_TEXTLAYEREDIT_H

#include "features/layers/model/LayerData.h"

#include <QColor>
#include <QString>

namespace ruwa::core::layers {

/**
 * @brief One edit from the Character or Paragraph group, as a value.
 *
 * The Layer Properties panel never touches the document: it says what the user
 * asked for and the canvas performs it, the same contract the Position group
 * uses for moves. Carrying the property as data rather than as one signal per
 * control keeps that route a single hop wide instead of thirteen.
 */
struct TextLayerEdit {
    enum class Property {
        FontFamily,
        FontSize,
        Color,
        Bold,
        Italic,
        Underline,
        Strikethrough,
        Tracking,
        Caps,
        Alignment,
        LineHeight,
        SpaceBefore,
        SpaceAfter
    };

    /// Live edits stream while a value is being dragged or typed and are not
    /// undoable on their own; the Commit that closes the run is what lands as a
    /// single step covering the whole interaction.
    /// Cancel abandons an open live run and puts the layer back to where the
    /// run began — a font previewed on hover and then not chosen.
    enum class Phase { Commit, Live, Cancel };

    Property property = Property::FontFamily;
    Phase phase = Phase::Commit;
    QString stringValue;
    QColor colorValue;
    qreal numberValue = 0.0;
    bool boolValue = false;
    int enumValue = 0;

    /// Character properties live on individual characters and so respect the
    /// selection; paragraph properties belong to the whole layer and ignore it,
    /// which is the split Photoshop's two panels draw.
    bool isCharacterProperty() const
    {
        switch (property) {
        case Property::Alignment:
        case Property::LineHeight:
        case Property::SpaceBefore:
        case Property::SpaceAfter:
            return false;
        default:
            break;
        }
        return true;
    }
};

/// Undo-stack label for @p property.
QString textLayerEditLabel(TextLayerEdit::Property property);

/**
 * @brief Applies @p edit to @p textData over the character range
 * [@p from, @p to).
 *
 * An empty range only happens on a layer with no characters yet: the defaults
 * change so the first character typed picks the new value up, and every
 * existing character is frozen into a run first so it keeps exactly the look it
 * had. Callers pass the whole text rather than an empty range when the user has
 * selected nothing — see CanvasPanel::applyTextLayerEdit.
 *
 * A range covering the whole text additionally moves the defaults, so a layer
 * edited that way ends up uniform rather than carrying one run per character
 * forever.
 */
void applyTextLayerEdit(TextLayerData& textData, const TextLayerEdit& edit, int from, int to);

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_TEXTLAYEREDIT_H
