// SPDX-License-Identifier: MPL-2.0

// LayerTextCharacterEditor.h
#ifndef RUWA_UI_WIDGETS_LAYERTEXTCHARACTEREDITOR_H
#define RUWA_UI_WIDGETS_LAYERTEXTCHARACTEREDITOR_H

#include "features/layers/model/TextLayerEdit.h"

#include <QColor>
#include <QString>
#include <QWidget>

#include <functional>
#include <memory>
#include <optional>

namespace ruwa::ui::widgets {

class AssetToggleButton;
class ColorInputButton;
class FontDropdownSelector;
class NumericInputField;
class PropertyRowLayout;
class SegmentedOptionSelector;

/**
 * @brief Photoshop's Character panel for a Ruwa text layer: the attributes that
 * belong to individual characters rather than to the paragraph.
 *
 * Like LayerPositionEditor the widget never touches the document. It shows the
 * values it is given and emits what the user asked for; the owning panel routes
 * that to the canvas, which resolves it against the text caret. A value that is
 * not the same for every character in scope is shown blank, which is how
 * Photoshop reports a mixed selection.
 */
class LayerTextCharacterEditor : public QWidget {
    Q_OBJECT

public:
    explicit LayerTextCharacterEditor(QWidget* parent = nullptr);
    // Out of line: PropertyRowLayout is only forward declared here.
    ~LayerTextCharacterEditor() override;

    /// Everything the group displays, as read off the layer for the characters
    /// currently in scope. A nullopt field means "mixed".
    struct State {
        std::optional<QString> fontFamily;
        std::optional<qreal> fontSize;
        std::optional<QColor> color;
        std::optional<bool> bold;
        std::optional<bool> italic;
        std::optional<bool> underline;
        std::optional<bool> strikethrough;
        std::optional<qreal> tracking;
        std::optional<int> caps; ///< ruwa::core::layers::TextCaps
    };

    void setState(const State& state);

    /// True while a numeric field has focus, so the owner can leave a
    /// half-typed value alone when the document reports new ones.
    bool isEditingValue() const;

    /// True when @p widget is this group's colour swatch — the input a colour
    /// picker opened from here is serving.
    bool ownsColorInput(const QWidget* widget) const;

    /// Whether the shared colour picker is currently open over that swatch. The
    /// picker streams a colour per frame while it is dragged, so those values go
    /// out as one live run rather than as an undo step each; the host closes the
    /// run when the picker goes away.
    void setColorPickerOpen(bool open);

    void applyTheme();
    /// The group's two-column row layout, so the owning panel can line its
    /// caption column up with the other groups'.
    PropertyRowLayout* rowLayout() const;

signals:
    /// The user changed one attribute. The panel forwards it unchanged.
    void editRequested(const ruwa::core::layers::TextLayerEdit& edit);
    /// The colour swatch was clicked; the host owns the picker overlay.
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);

private:
    void emitNumberEdit(ruwa::core::layers::TextLayerEdit::Property property, qreal value,
        ruwa::core::layers::TextLayerEdit::Phase phase);
    /// Wires one numeric field: every valid value streams out live, and Enter or
    /// focus-out closes the run. A field the user never actually changed emits
    /// nothing and is restored from the document instead — leaving it empty is
    /// how you ask for the current value back, not for whatever number the
    /// field happened to be holding.
    void bindNumericField(NumericInputField* field,
        ruwa::core::layers::TextLayerEdit::Property property, bool& dirtyFlag,
        std::function<std::optional<qreal>()> currentValue);
    void emitToggleEdit(ruwa::core::layers::TextLayerEdit::Property property, bool value);
    /// Writes @p on into both the check state and the visual state, without the
    /// button reporting it as a user toggle.
    void syncToggle(AssetToggleButton* button, bool on);
    /// Reflects @p value in @p field, blanking it when the characters disagree.
    void setFieldValue(
        NumericInputField* field, const std::optional<qreal>& value, bool& dirtyFlag);

private:
    std::unique_ptr<PropertyRowLayout> m_rows;
    FontDropdownSelector* m_fontDropdown = nullptr;
    NumericInputField* m_sizeInput = nullptr;
    NumericInputField* m_trackingInput = nullptr;
    ColorInputButton* m_colorInput = nullptr;
    bool m_colorPickerOpen = false;
    AssetToggleButton* m_boldButton = nullptr;
    AssetToggleButton* m_italicButton = nullptr;
    AssetToggleButton* m_underlineButton = nullptr;
    AssetToggleButton* m_strikethroughButton = nullptr;
    AssetToggleButton* m_allCapsButton = nullptr;
    AssetToggleButton* m_smallCapsButton = nullptr;

    /// Set while pushing document values into the controls, so their own
    /// signals are not mistaken for user edits.
    bool m_syncing = false;
    /// Set once the user has typed or nudged the field since it was last
    /// written from the document.
    bool m_sizeDirty = false;
    bool m_trackingDirty = false;
    State m_state;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_LAYERTEXTCHARACTEREDITOR_H
