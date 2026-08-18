// SPDX-License-Identifier: MPL-2.0

// LayerPositionEditor.h
#ifndef RUWA_UI_WIDGETS_LAYERPOSITIONEDITOR_H
#define RUWA_UI_WIDGETS_LAYERPOSITIONEDITOR_H

#include <QPointF>
#include <QWidget>

namespace ruwa::ui::widgets {

class AnchorGridSelector;
class NumericInputField;
class SegmentedOptionSelector;

/**
 * @brief Position controls for a layer: a nine-cell anchor grid on the left,
 * the axis the anchor applies to and the layer's document coordinates on the
 * right.
 *
 * The axis picker constrains what an anchor click moves — Both centres the
 * content on the chosen cell, X or Y moves it along that axis alone and leaves
 * the other coordinate where the user put it.
 *
 * Like LayerIdentityHeader, the widget never touches the model: it reports what
 * the user asked for and waits to be told the resulting position, so the owning
 * panel keeps the single path into LayerModel.
 */
class LayerPositionEditor : public QWidget {
    Q_OBJECT

public:
    enum class Axis { Both = 0, X = 1, Y = 2 };

    explicit LayerPositionEditor(QWidget* parent = nullptr);

    /// Displayed document-space position of the layer's content. Setting it
    /// does not emit positionEdited() — that signal means "the user typed".
    void setPosition(const QPointF& position);
    QPointF position() const;

    Axis axis() const;
    void setAxis(Axis axis);

    int anchorIndex() const;
    void setAnchorIndex(int index);

    /// False when the layer has nothing to place — an empty raster layer, or a
    /// group with no drawable descendant. The controls go inactive rather than
    /// showing a coordinate that stands for nothing.
    void setContentAvailable(bool available);
    bool isContentAvailable() const { return m_contentAvailable; }

    /// True while a coordinate field has focus. The owner uses it to leave a
    /// half-typed number alone when new values arrive from the document.
    bool isEditingCoordinates() const;

    /// Re-read fonts, colours and metrics from the theme.
    void applyTheme();

signals:
    /// An anchor cell was clicked: align the layer's content to @p anchorIndex
    /// (see AnchorGridSelector) along @p axis.
    void anchorApplied(int anchorIndex, Axis axis);
    /// The user typed or nudged a coordinate.
    void positionEdited(const QPointF& position);

private:
    void syncInputsToAxis();

private:
    AnchorGridSelector* m_anchorGrid = nullptr;
    SegmentedOptionSelector* m_axisSelector = nullptr;
    NumericInputField* m_xInput = nullptr;
    NumericInputField* m_yInput = nullptr;

    /// Set while pushing a position into the fields, so their own signals are
    /// not mistaken for user edits.
    bool m_syncingFields = false;
    bool m_contentAvailable = true;
    /// Last position the fields were known to agree with, so a focus-out that
    /// changed nothing does not read as an edit.
    QPointF m_committedPosition;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_LAYERPOSITIONEDITOR_H
