// SPDX-License-Identifier: MPL-2.0

// BrushSliderPreviewWidget.h
#ifndef RUWA_UI_WIDGETS_WORKSPACE_BRUSHSLIDERPREVIEWWIDGET_H
#define RUWA_UI_WIDGETS_WORKSPACE_BRUSHSLIDERPREVIEWWIDGET_H

#include "features/brush/manager/BrushSettings.h"

#include <QWidget>
#include <QSize>
#include <QPropertyAnimation>
#include <QPointer>

namespace ruwa::core::brushes {
class BrushPreviewSession;
}

namespace ruwa::ui::widgets {

/**
 * @brief Small floating widget for real-time brush preview during slider drag.
 *
 * Shows square dot preview (like BrushItem in BrushPackPanel) and label below.
 * Follows cursor on Y axis. Smooth fade in/out like BrushPackOverlay.
 */
class BrushSliderPreviewWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)
    Q_PROPERTY(QPoint slidePos READ slidePos WRITE setSlidePos)

public:
    enum class SliderType { Size, Opacity };

    explicit BrushSliderPreviewWidget(QWidget* parent = nullptr);
    ~BrushSliderPreviewWidget() override;

    void setSliderType(SliderType type);
    void setBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings);
    void setBrushSize(qreal size);
    void setBrushOpacity(qreal opacity);
    void setBrushColor(const QColor& color);

    void showAnimated();
    void hideAnimated();
    bool isAnimating() const { return m_isShowing || m_isHiding; }

    qreal showProgress() const { return m_showProgress; }
    void setShowProgress(qreal progress);

    QPoint slidePos() const { return m_slidePos; }
    void setSlidePos(const QPoint& pos);

    /// Anchor widget (e.g. BrushControlOverlay) — position left/right like BrushPackPanel
    void setAnchorWidget(QWidget* widget) { m_anchorWidget = widget; }

    /// Canvas size for brush radius scaling (label "N px" and preview)
    void setCanvasSize(const QSize& size);
    void setHasFiniteDocumentBounds(bool hasFiniteDocumentBounds);

    /// Update position: X from anchor (left/right), Y follows cursor. During show animation
    /// updates animation end value so cursor-follow works smoothly.
    void updatePositionFromCursor(const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onThemeChanged();
    void onShowAnimationFinished();
    void onHideAnimationFinished();

private:
    void setupAnimations();
    void updateLabelText();
    void updateSize();
    QPoint calculatePosition(const QPoint& globalPos) const;
    void drawBackground(QPainter& painter);
    void drawPreview(QPainter& painter, const QRectF& rect);
    void drawLabel(QPainter& painter, const QRectF& rect);

    QSize m_canvasSize;
    bool m_hasFiniteDocumentBounds = true;
    SliderType m_sliderType = SliderType::Size;
    ruwa::core::brushes::BrushSettingsData m_brushSettings;
    qreal m_brushSize = 0.5;
    qreal m_brushOpacity = 1.0;
    QColor m_brushColor = Qt::black;
    QString m_labelText;

    qreal m_showProgress = 0.0;
    QPoint m_slidePos;
    bool m_isShowing = false;
    bool m_isHiding = false;
    QPropertyAnimation* m_showAnimation = nullptr;
    QPropertyAnimation* m_slideAnimation = nullptr;
    QPointer<QWidget> m_anchorWidget;
    ruwa::core::brushes::BrushPreviewSession* m_previewSession = nullptr;

    static constexpr int PreviewSize = 80;
    static constexpr int LabelHeight = 13; // compact text area
    static constexpr int Padding = 8; // widget padding (preview, sides)
    static constexpr int GapPreviewToText = 4; // 4px from bottom of preview to text
    static constexpr int GapTextToBottom = 4; // 4px from text to bottom of panel
    static constexpr int PreviewCornerRadius = 8;
    static constexpr int CornerRadius = 16;
    static constexpr int ShowDuration = 200;
    static constexpr int HideDuration = 150;
    static constexpr int SlideOffset = 24;
    static constexpr int OffsetFromSource = 8;
    static constexpr int MinContainerMargin = 20;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_BRUSHSLIDERPREVIEWWIDGET_H
