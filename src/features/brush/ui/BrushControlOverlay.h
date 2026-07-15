// SPDX-License-Identifier: MPL-2.0

// BrushControlOverlay.h
#ifndef RUWA_UI_WIDGETS_WORKSPACE_BRUSHCONTROLOVERLAY_H
#define RUWA_UI_WIDGETS_WORKSPACE_BRUSHCONTROLOVERLAY_H

#include "features/brush/manager/BrushSettings.h"
#include "shell/context-menu/IContextMenuProvider.h"
#include "shared/rendering/CanvasBackdropSource.h"

#include <QWidget>
#include <QColor>
#include <QElapsedTimer>
#include <QSize>

namespace ruwa::ui::widgets {

class BrushPackOverlay;
class BrushSliderPreviewWidget;
class ProgressHandleSlider;

} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

class ToolButton;

}

namespace ruwa::ui::widgets {

/**
 * @brief Draggable overlay widget for brush controls (Procreate-style)
 *
 * Layout (top to bottom):
 * - Handle (rounded line) for dragging
 * - Size slider (vertical)
 * - Brushpack button (opens brush selector overlay)
 * - Opacity slider (vertical)
 */
class BrushControlOverlay : public QWidget, public IContextMenuProvider {
    Q_OBJECT

public:
    explicit BrushControlOverlay(QWidget* parent = nullptr);
    ~BrushControlOverlay() override;

    /// Get/Set brush size (0.0 - 1.0)
    qreal brushSize() const { return m_brushSize; }
    void setBrushSize(qreal size);

    /// Get/Set brush opacity (0.0 - 1.0)
    qreal brushOpacity() const { return m_brushOpacity; }
    void setBrushOpacity(qreal opacity);

    /// Set brush color for preview (default: black)
    void setBrushColor(const QColor& color);
    QColor brushColor() const { return m_brushColor; }

    /// Set brush settings for preview (hardness, flow, etc.)
    void setBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings);
    const ruwa::core::brushes::BrushSettingsData& brushSettings() const { return m_brushSettings; }

    /// Access the brush pack overlay
    BrushPackOverlay* brushPackOverlay() const { return m_brushPackOverlay; }

    /// Source for the frosted-glass backdrop (canvas viewport blur). Null = solid
    /// fallback. The overlay paints the backdrop itself (single painter, no GL/Qt
    /// positional desync while dragged).
    void setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source);

    /// Canvas size for brush radius scaling (MaxBrush = 1500 * (1 - exp(-0.0003 * S)))
    void setCanvasSize(const QSize& size);
    QSize canvasSize() const { return m_canvasSize; }
    void setHasFiniteDocumentBounds(bool hasFiniteDocumentBounds);
    bool hasFiniteDocumentBounds() const { return m_hasFiniteDocumentBounds; }

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override { return ContextMenuType::SimpleActions; }
    QVariantMap contextMenuContext() const override;

signals:
    void brushSizeChanged(qreal size);
    void brushOpacityChanged(qreal opacity);
    void positionChanged(const QPoint& pos);
    void dragFinished();
    void brushPackRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onThemeChanged();

private:
    bool shouldEmitSliderUpdate(QElapsedTimer& timer, bool force);

private:
    void setupWidget();
    void updateSize();
    void connectSignals();
    void setupBrushPackOverlay();
    void setupSliderPreviewWidget();

    // Geometry helpers
    QRectF handleRect() const;
    QRectF sizeSliderRect() const;
    QRectF brushPackButtonRect() const;
    QRectF opacitySliderRect() const;
    // Hit testing
    enum class DragMode {
        None,
        Widget, // Dragging the whole widget
    };
    DragMode hitTest(const QPoint& pos) const;
    void handleDrag(const QPoint& globalPos);
    void updateSliderGeometries();
    /// Matches per-segment scaling in sizeSliderRect / opacitySliderRect (avoids rounding vs
    /// scaled(sum)).
    QSize computedOverlaySize() const;

    // Drawing
    void drawBackground(QPainter& painter);
    void drawHandle(QPainter& painter, const QRectF& rect);

private:
    // Brush state
    qreal m_brushSize = 0.5; // 0.0 - 1.0
    QSize m_canvasSize;
    bool m_hasFiniteDocumentBounds = true;
    qreal m_brushOpacity = 1.0; // 0.0 - 1.0
    QColor m_brushColor = Qt::black;
    ruwa::core::brushes::BrushSettingsData m_brushSettings;

    // Interaction
    DragMode m_dragMode = DragMode::None;
    QPoint m_dragStartPos;
    QPoint m_widgetStartPos;

    // Brush pack overlay
    BrushPackOverlay* m_brushPackOverlay = nullptr;
    ruwa::ui::workspace::ToolButton* m_brushPackButton = nullptr;

    // Frosted-glass backdrop source (non-owning; nulled on source destruction).
    ruwa::shared::rendering::ICanvasBackdropSource* m_backdropSource = nullptr;

    // Slider drag preview (real-time brush preview)
    BrushSliderPreviewWidget* m_sliderPreviewWidget = nullptr;
    ProgressHandleSlider* m_sizeSlider = nullptr;
    ProgressHandleSlider* m_opacitySlider = nullptr;
    QElapsedTimer m_sizeSliderEmitTimer;
    QElapsedTimer m_opacitySliderEmitTimer;

    // Layout constants (base values, scaled by theme)
    static constexpr int BasePadding = 8;
    static constexpr int BaseHandleHeight = 8;
    static constexpr int BaseHandleLineWidth = 20;
    static constexpr int BaseHandleLineHeight = 3;
    static constexpr int BaseSliderWidth = 28;
    static constexpr int BaseSliderHeight = 140;
    static constexpr int BaseSliderSpacing = 6;
    static constexpr int BaseCursorHeight = 6;
    static constexpr int BaseCornerRadius = 10;
    static constexpr int BaseButtonSize = 28;
    static constexpr int BaseIconSize = 16;
    static constexpr int SliderEmitIntervalMs = 8;
    static constexpr int SliderSurfaceOpacityPercent = 58;
    static constexpr int SliderTrackOpacityPercent = 0;
    static constexpr int SliderFillOpacityPercent = 85;
    static constexpr int ButtonChromeOpacityPercent = 58;

    // Calculated dimensions (vertical layout)
    static constexpr int BaseWidth = BasePadding + BaseSliderWidth + BasePadding;
    static constexpr int BaseHeight = BasePadding + BaseHandleHeight + 4 + BaseSliderHeight
        + BaseSliderSpacing + BaseButtonSize // brushpack button
        + BaseSliderSpacing + BaseSliderHeight + BasePadding;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_BRUSHCONTROLOVERLAY_H
