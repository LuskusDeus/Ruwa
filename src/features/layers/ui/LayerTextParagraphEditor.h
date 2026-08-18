// SPDX-License-Identifier: MPL-2.0

// LayerTextParagraphEditor.h
#ifndef RUWA_UI_WIDGETS_LAYERTEXTPARAGRAPHEDITOR_H
#define RUWA_UI_WIDGETS_LAYERTEXTPARAGRAPHEDITOR_H

#include "features/layers/model/TextLayerEdit.h"

#include <QWidget>

#include <functional>
#include <memory>

namespace ruwa::ui::widgets {

class NumericInputField;
class PropertyRowLayout;
class SegmentedOptionSelector;

/**
 * @brief Photoshop's Paragraph panel for a Ruwa text layer: the attributes that
 * belong to whole lines rather than to individual characters.
 *
 * Leading lives here rather than beside the font size, unlike Photoshop, for
 * the same reason the group exists: Ruwa stores one line height per layer, so
 * showing it as a character attribute would promise a per-character setting
 * that does not exist. It is entered as a percentage of the font's natural line
 * height — the same number Photoshop's auto-leading percentage means.
 *
 * The widget never touches the document; it emits what the user asked for and
 * waits to be told the result.
 */
class LayerTextParagraphEditor : public QWidget {
    Q_OBJECT

public:
    explicit LayerTextParagraphEditor(QWidget* parent = nullptr);
    // Out of line: PropertyRowLayout is only forward declared here.
    ~LayerTextParagraphEditor() override;

    struct State {
        int alignment = 0; ///< ruwa::core::layers::TextAlignment
        qreal lineHeight = 1.2; ///< multiplier; shown as a percentage
        qreal spaceBefore = 0.0;
        qreal spaceAfter = 0.0;
    };

    void setState(const State& state);
    bool isEditingValue() const;
    void applyTheme();
    /// The group's two-column row layout, so the owning panel can line its
    /// caption column up with the other groups'.
    PropertyRowLayout* rowLayout() const;

signals:
    void editRequested(const ruwa::core::layers::TextLayerEdit& edit);

private:
    void emitNumberEdit(ruwa::core::layers::TextLayerEdit::Property property, qreal value,
        ruwa::core::layers::TextLayerEdit::Phase phase);
    /// Wires one numeric field: values stream out live while they change, Enter
    /// or focus-out closes the run, and a field the user never touched is
    /// restored from the document rather than committed.
    void bindNumericField(NumericInputField* field,
        ruwa::core::layers::TextLayerEdit::Property property, bool& dirtyFlag,
        std::function<qreal()> currentValue, qreal displayScale);

private:
    std::unique_ptr<PropertyRowLayout> m_rows;
    SegmentedOptionSelector* m_alignmentSelector = nullptr;
    NumericInputField* m_leadingInput = nullptr;
    NumericInputField* m_spaceBeforeInput = nullptr;
    NumericInputField* m_spaceAfterInput = nullptr;

    bool m_syncing = false;
    /// Set once the user has typed or nudged the field since it was last written
    /// from the document.
    bool m_leadingDirty = false;
    bool m_spaceBeforeDirty = false;
    bool m_spaceAfterDirty = false;
    State m_state;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_LAYERTEXTPARAGRAPHEDITOR_H
