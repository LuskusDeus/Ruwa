// SPDX-License-Identifier: MPL-2.0

// CanvasToolStateOverlay.h
#ifndef RUWA_UI_WIDGETS_WORKSPACE_CANVASTOOLSTATEOVERLAY_H
#define RUWA_UI_WIDGETS_WORKSPACE_CANVASTOOLSTATEOVERLAY_H

#include "shell/context-menu/IContextMenuProvider.h"
#include "shared/rendering/CanvasBackdropSource.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QSize>
#include <QWidget>

class QMouseEvent;
class QAbstractButton;
class QButtonGroup;
class QLabel;
class QString;
class QWidget;

namespace ruwa::ui::widgets {

class AnimatedStackedWidget;
class ProgressHandleSlider;
class Separator;

/**
 * @brief Horizontal canvas HUD strip for canvas/tool state.
 *
 * Uses nested AnimatedStackedWidget instances:
 * - outer stack switches full toolbar content by canvas state
 * - inner stack switches the middle section by selected tool
 *
 * Width follows the active content, height matches the joystick zoom panel.
 */
class CanvasToolStateOverlay : public QWidget, public IContextMenuProvider {
    Q_OBJECT
    Q_PROPERTY(int animatedWidth READ animatedWidth WRITE setAnimatedWidth)

public:
    explicit CanvasToolStateOverlay(QWidget* parent = nullptr);
    ~CanvasToolStateOverlay() override;

    void setCanvasPageIndex(int index);
    int canvasPageIndex() const;
    void setCanvasPlaceholderText(const QString& text);
    void setCanvasFlipStates(bool horizontal, bool vertical);
    void setBrushEraserMode(bool enabled);
    void setUndoAvailable(bool available);
    void setRedoAvailable(bool available);
    void setToolPageParameterValues(int pageIndex, qreal hardness, qreal flow);
    void setToolPageIntensityValue(int pageIndex, qreal intensity);
    void setToolPageWetMixValue(int pageIndex, qreal wetMix);
    void setToolPageLiquifyMode(int mode);

    static constexpr int kLiquifyModeCount = 5;
    void setToolPageStabilizationValue(int pageIndex, qreal stabilization);
    void setCanvasResizeInfo(const QSize& oldSize, const QSize& newSize);

    void setToolPageIndex(int index);
    int toolPageIndex() const;

    /// Source coordinating this widget's same-frame GPU blur region.
    void setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source);

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override { return ContextMenuType::SimpleActions; }
    QVariantMap contextMenuContext() const override;

signals:
    void sizeChanged(const QSize& size);
    void undoRequested();
    void redoRequested();
    void copyCanvasRequested();
    void canvasFlipHorizontalRequested(bool checked);
    void canvasFlipVerticalRequested(bool checked);
    void brushEraserModeToggled(bool enabled);
    void brushHardnessChanged(qreal hardness);
    void brushFlowChanged(qreal flow);
    void blurIntensityChanged(qreal intensity);
    void smudgeIntensityChanged(qreal intensity);
    void smudgeWetMixChanged(qreal wetMix);
    void liquifyStrengthChanged(qreal strength);
    void liquifyModeChanged(int mode);
    void lassoStabilizationChanged(qreal stabilization);
    void lassoFillStabilizationChanged(qreal stabilization);

protected:
    void paintEvent(QPaintEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onThemeChanged();

private:
    void setupUi();
    void applyThemeMetrics();
    void syncStackSizes();
    void updateOverlaySize();
    QSize toolPageNaturalSize(int index) const;
    QSize interactiveNaturalSize(int toolIndex) const;
    QSize canvasPageNaturalSize(int index) const;
    void updateVisibleContentGeometry(int contentWidth, int contentHeight);
    void updateInteractivePageLayout(int contentWidth, int contentHeight);
    int animatedWidth() const;
    void setAnimatedWidth(int width);
    void animateOverlayWidth(int targetWidth);
    void drawBackground(QPainter& painter);
    void connectParameterSlider(ProgressHandleSlider* slider, QElapsedTimer& timer,
        void (CanvasToolStateOverlay::*changeSignal)(qreal));
    bool shouldEmitParameterSliderUpdate(QElapsedTimer& timer, bool force);
    static qreal sliderValueToUnit(int value);

    // Match CanvasStylusJoystickContainerWidget zoom panel (anonymous kPanelHeight, kHandleHeight,
    // …)
    static constexpr int kMinimumContentHeightBase = 36;
    static constexpr int kPanelPaddingBase = 6;
    // Capsule end-caps eat into the strip's corners. Pad the left/right edges
    // wider than the vertical padding so the rounded ends clear the icons.
    static constexpr int kPanelHorizontalPaddingBase = 14;
    static constexpr int kSectionSpacingBase = 6;
    static constexpr int kSectionSeparatorHeightBase = 24;
    static constexpr int kSliderEmitIntervalMs = 8;

    AnimatedStackedWidget* m_canvasModeStack = nullptr;
    QWidget* m_interactivePage = nullptr;
    QWidget* m_leftSection = nullptr;
    QWidget* m_rightSection = nullptr;
    QWidget* m_toolViewport = nullptr;
    AnimatedStackedWidget* m_toolContentStack = nullptr;
    Separator* m_leftSeparator = nullptr;
    Separator* m_rightSeparator = nullptr;
    QLabel* m_canvasPlaceholderLabel = nullptr;
    QAbstractButton* m_flipHorizontalButton = nullptr;
    QAbstractButton* m_flipVerticalButton = nullptr;
    QAbstractButton* m_undoButton = nullptr;
    QAbstractButton* m_redoButton = nullptr;
    QAbstractButton* m_copyCanvasButton = nullptr;
    QAbstractButton* m_brushEraserToggleButton = nullptr;
    ProgressHandleSlider* m_brushHardnessSlider = nullptr;
    ProgressHandleSlider* m_brushFlowSlider = nullptr;
    ProgressHandleSlider* m_eraserHardnessSlider = nullptr;
    ProgressHandleSlider* m_eraserFlowSlider = nullptr;
    ProgressHandleSlider* m_blurIntensitySlider = nullptr;
    ProgressHandleSlider* m_smudgeIntensitySlider = nullptr;
    ProgressHandleSlider* m_smudgeWetMixSlider = nullptr;
    ProgressHandleSlider* m_liquifyStrengthSlider = nullptr;
    QButtonGroup* m_liquifyModeGroup = nullptr;
    QAbstractButton* m_liquifyModeButtons[kLiquifyModeCount] = {};
    ProgressHandleSlider* m_lassoStabilizationSlider = nullptr;
    ProgressHandleSlider* m_lassoFillStabilizationSlider = nullptr;
    QElapsedTimer m_brushHardnessSliderEmitTimer;
    QElapsedTimer m_brushFlowSliderEmitTimer;
    QElapsedTimer m_eraserHardnessSliderEmitTimer;
    QElapsedTimer m_eraserFlowSliderEmitTimer;
    QElapsedTimer m_blurIntensitySliderEmitTimer;
    QElapsedTimer m_smudgeIntensitySliderEmitTimer;
    QElapsedTimer m_smudgeWetMixSliderEmitTimer;
    QElapsedTimer m_liquifyStrengthSliderEmitTimer;
    QElapsedTimer m_lassoStabilizationSliderEmitTimer;
    QElapsedTimer m_lassoFillStabilizationSliderEmitTimer;
    QLabel* m_canvasResizeOldSizeValueLabel = nullptr;
    QLabel* m_canvasResizeNewSizeValueLabel = nullptr;
    QSize m_lastAppliedSize;
    QPropertyAnimation* m_widthAnimation = nullptr;
    qreal m_widthAnimationAnchorCenterX = 0.0;
    QSize m_pendingAnimatedSize;
    int m_canvasPageSizeIndexOverride = -1;
    int m_toolPageSizeIndexOverride = -1;

    // Backdrop-blur source (non-owning; nulled on source destruction).
    ruwa::shared::rendering::ICanvasBackdropSource* m_backdropSource = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_CANVASTOOLSTATEOVERLAY_H
