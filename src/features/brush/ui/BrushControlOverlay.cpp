// SPDX-License-Identifier: MPL-2.0

// BrushControlOverlay.cpp
#include "BrushControlOverlay.h"
#include "features/canvas/ui/CanvasOverlayContextActions.h"
#include "BrushPackOverlay.h"
#include "BrushSliderPreviewWidget.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/ToolButton.h"
#include "features/brush/manager/BrushPreviewManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"

#include <QAbstractButton>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QLinearGradient>
#include <QCursor>
#include <QStringLiteral>
#include <QVariantList>
#include <QVariantMap>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

// ============================================================================
// Construction
// ============================================================================

BrushControlOverlay::BrushControlOverlay(QWidget* parent)
    : QWidget(parent)
{
    setupWidget();
    updateSize();
    connectSignals();
    setupBrushPackOverlay();
    setupSliderPreviewWidget();
}

BrushControlOverlay::~BrushControlOverlay() = default;

void BrushControlOverlay::setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source)
{
    if (m_backdropSource == source) {
        return;
    }
    m_backdropSource = source;
    update();
}

QVariantMap BrushControlOverlay::contextMenuContext() const
{
    using ruwa::ui::canvas_overlay::CanvasOverlayContextActionId;

    QVariantList actions;
    QVariantMap hide;
    hide.insert(
        QStringLiteral("id"), static_cast<int>(CanvasOverlayContextActionId::HideBrushControl));
    hide.insert(QStringLiteral("text"), tr("Hide widget"));
    hide.insert(QStringLiteral("danger"), false);
    hide.insert(QStringLiteral("standardIcon"),
        static_cast<int>(IconProvider::StandardIcon::EyeDeactivated));
    actions.append(hide);

    QVariantMap ctx;
    ctx.insert(QStringLiteral("simpleActions"), actions);
    return ctx;
}

void BrushControlOverlay::setupWidget()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setTabletTracking(true);

    m_sizeSlider = new ProgressHandleSlider(this);
    m_sizeSlider->setOrientation(Qt::Vertical);
    m_sizeSlider->setRange(0, 100);
    m_sizeSlider->setValue(qRound(m_brushSize * 100.0));
    m_sizeSlider->setShowValueText(false);
    m_sizeSlider->setBackgroundOpacity(
        SliderSurfaceOpacityPercent / 100.0, SliderTrackOpacityPercent / 100.0);
    m_sizeSlider->setProgressFillOpacity(SliderFillOpacityPercent / 100.0);
    m_sizeSlider->setCursor(Qt::PointingHandCursor);

    m_opacitySlider = new ProgressHandleSlider(this);
    m_opacitySlider->setOrientation(Qt::Vertical);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(qRound(m_brushOpacity * 100.0));
    m_opacitySlider->setShowValueText(false);
    m_opacitySlider->setBackgroundOpacity(
        SliderSurfaceOpacityPercent / 100.0, SliderTrackOpacityPercent / 100.0);
    m_opacitySlider->setProgressFillOpacity(SliderFillOpacityPercent / 100.0);
    m_opacitySlider->setCursor(Qt::PointingHandCursor);

    m_brushPackButton
        = new ruwa::ui::workspace::ToolButton(ruwa::ui::workspace::ToolButton::Mode::Action, this);
    m_brushPackButton->setIconType(IconProvider::StandardIcon::Brushpack);
    m_brushPackButton->setBaseSquareSize(BaseButtonSize, BaseIconSize);
    m_brushPackButton->setChromeStyle(ruwa::ui::workspace::ToolButton::ChromeStyle::Surface);
    m_brushPackButton->setChromeOpacity(ButtonChromeOpacityPercent / 100.0);
    m_brushPackButton->setBorderVisible(true);
    m_brushPackButton->setMutedNormalIcon(true);
}

QSize BrushControlOverlay::computedOverlaySize() const
{
    auto& theme = ThemeManager::instance();
    const int pad = theme.scaled(BasePadding);
    const int sliderW = theme.scaled(BaseSliderWidth);
    const int w = pad + sliderW + pad;

    const int handleH = theme.scaled(BaseHandleHeight);
    const int sliderH = theme.scaled(BaseSliderHeight);
    const int spacing = theme.scaled(BaseSliderSpacing);
    const int btnH = theme.scaled(BaseButtonSize);
    const int h
        = pad + handleH + theme.scaled(4) + sliderH + spacing + btnH + spacing + sliderH + pad;

    return QSize(w, h);
}

void BrushControlOverlay::updateSize()
{
    setFixedSize(computedOverlaySize());
    updateSliderGeometries();
}

void BrushControlOverlay::connectSignals()
{
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &BrushControlOverlay::onThemeChanged);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
        []() { ruwa::core::brushes::BrushPreviewManager::instance().invalidateCache(); });

    connect(m_brushPackButton, &QAbstractButton::clicked, this, [this]() {
        emit brushPackRequested();
        if (m_brushPackOverlay) {
            m_brushPackOverlay->showPanel(this);
        }
    });

    connect(m_sizeSlider, &ProgressHandleSlider::valueChanged, this, [this](int value) {
        const qreal size = qBound(0.0, value / 100.0, 1.0);
        if (!qFuzzyCompare(m_brushSize, size)) {
            m_brushSize = size;
            update();
        }
        if (shouldEmitSliderUpdate(m_sizeSliderEmitTimer, false)) {
            emit brushSizeChanged(m_brushSize);
        }
        if (m_sliderPreviewWidget) {
            m_sliderPreviewWidget->setSliderType(BrushSliderPreviewWidget::SliderType::Size);
            m_sliderPreviewWidget->setBrushSettings(m_brushSettings);
            m_sliderPreviewWidget->setBrushSize(m_brushSize);
            m_sliderPreviewWidget->setBrushOpacity(m_brushOpacity);
            m_sliderPreviewWidget->setBrushColor(m_brushColor);
            m_sliderPreviewWidget->updatePositionFromCursor(QCursor::pos());
        }
    });
    connect(m_sizeSlider, &ProgressHandleSlider::sliderPressed, this, [this]() {
        m_sizeSliderEmitTimer.invalidate();
        if (!m_sliderPreviewWidget)
            return;
        m_sliderPreviewWidget->setSliderType(BrushSliderPreviewWidget::SliderType::Size);
        m_sliderPreviewWidget->setBrushSettings(m_brushSettings);
        m_sliderPreviewWidget->setBrushSize(m_brushSize);
        m_sliderPreviewWidget->setBrushOpacity(m_brushOpacity);
        m_sliderPreviewWidget->setBrushColor(m_brushColor);
        m_sliderPreviewWidget->updatePositionFromCursor(QCursor::pos());
        m_sliderPreviewWidget->raise();
        m_sliderPreviewWidget->showAnimated();
    });
    connect(m_sizeSlider, &ProgressHandleSlider::sliderReleased, this, [this]() {
        if (shouldEmitSliderUpdate(m_sizeSliderEmitTimer, true)) {
            emit brushSizeChanged(m_brushSize);
        }
        if (m_sliderPreviewWidget) {
            m_sliderPreviewWidget->hideAnimated();
        }
    });

    connect(m_opacitySlider, &ProgressHandleSlider::valueChanged, this, [this](int value) {
        const qreal opacity = qBound(0.0, value / 100.0, 1.0);
        if (!qFuzzyCompare(m_brushOpacity, opacity)) {
            m_brushOpacity = opacity;
            update();
        }
        if (shouldEmitSliderUpdate(m_opacitySliderEmitTimer, false)) {
            emit brushOpacityChanged(m_brushOpacity);
        }
        if (m_sliderPreviewWidget) {
            m_sliderPreviewWidget->setSliderType(BrushSliderPreviewWidget::SliderType::Opacity);
            m_sliderPreviewWidget->setBrushSettings(m_brushSettings);
            m_sliderPreviewWidget->setBrushSize(m_brushSize);
            m_sliderPreviewWidget->setBrushOpacity(m_brushOpacity);
            m_sliderPreviewWidget->setBrushColor(m_brushColor);
            m_sliderPreviewWidget->updatePositionFromCursor(QCursor::pos());
        }
    });
    connect(m_opacitySlider, &ProgressHandleSlider::sliderPressed, this, [this]() {
        m_opacitySliderEmitTimer.invalidate();
        if (!m_sliderPreviewWidget)
            return;
        m_sliderPreviewWidget->setSliderType(BrushSliderPreviewWidget::SliderType::Opacity);
        m_sliderPreviewWidget->setBrushSettings(m_brushSettings);
        m_sliderPreviewWidget->setBrushSize(m_brushSize);
        m_sliderPreviewWidget->setBrushOpacity(m_brushOpacity);
        m_sliderPreviewWidget->setBrushColor(m_brushColor);
        m_sliderPreviewWidget->updatePositionFromCursor(QCursor::pos());
        m_sliderPreviewWidget->raise();
        m_sliderPreviewWidget->showAnimated();
    });
    connect(m_opacitySlider, &ProgressHandleSlider::sliderReleased, this, [this]() {
        if (shouldEmitSliderUpdate(m_opacitySliderEmitTimer, true)) {
            emit brushOpacityChanged(m_brushOpacity);
        }
        if (m_sliderPreviewWidget) {
            m_sliderPreviewWidget->hideAnimated();
        }
    });
}

bool BrushControlOverlay::shouldEmitSliderUpdate(QElapsedTimer& timer, bool force)
{
    if (force) {
        if (timer.isValid()) {
            timer.restart();
        } else {
            timer.start();
        }
        return true;
    }

    if (!timer.isValid()) {
        timer.start();
        return true;
    }

    if (timer.elapsed() >= SliderEmitIntervalMs) {
        timer.restart();
        return true;
    }

    return false;
}

void BrushControlOverlay::setupBrushPackOverlay()
{
    if (parentWidget()) {
        m_brushPackOverlay = new BrushPackOverlay(parentWidget());
        connect(this, &BrushControlOverlay::positionChanged, m_brushPackOverlay,
            [this]() { m_brushPackOverlay->onSourceWidgetMoved(this); });
    }
}

void BrushControlOverlay::setupSliderPreviewWidget()
{
    if (parentWidget()) {
        m_sliderPreviewWidget = new BrushSliderPreviewWidget(parentWidget());
        m_sliderPreviewWidget->setAnchorWidget(this);
        m_sliderPreviewWidget->setCanvasSize(m_canvasSize);
        m_sliderPreviewWidget->setHasFiniteDocumentBounds(m_hasFiniteDocumentBounds);
        m_sliderPreviewWidget->hide();
    }
}

void BrushControlOverlay::setBrushSize(qreal size)
{
    size = qBound(0.0, size, 1.0);
    if (qFuzzyCompare(m_brushSize, size))
        return;
    m_brushSize = size;
    if (m_sizeSlider) {
        m_sizeSlider->setValue(qRound(m_brushSize * 100.0));
    }
    emit brushSizeChanged(size);
    update();
}

void BrushControlOverlay::setBrushOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_brushOpacity, opacity))
        return;
    m_brushOpacity = opacity;
    if (m_opacitySlider) {
        m_opacitySlider->setValue(qRound(m_brushOpacity * 100.0));
    }
    emit brushOpacityChanged(opacity);
    update();
}

void BrushControlOverlay::setBrushColor(const QColor& color)
{
    if (m_brushColor == color)
        return;
    m_brushColor = color;
    update();
}

void BrushControlOverlay::setBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings)
{
    m_brushSettings = settings;
    if (m_sliderPreviewWidget) {
        m_sliderPreviewWidget->setBrushSettings(settings);
    }
    update();
}

void BrushControlOverlay::setCanvasSize(const QSize& size)
{
    if (m_canvasSize == size)
        return;
    m_canvasSize = size;
    if (m_sliderPreviewWidget) {
        m_sliderPreviewWidget->setCanvasSize(size);
    }
}

void BrushControlOverlay::setHasFiniteDocumentBounds(bool hasFiniteDocumentBounds)
{
    if (m_hasFiniteDocumentBounds == hasFiniteDocumentBounds)
        return;
    m_hasFiniteDocumentBounds = hasFiniteDocumentBounds;
    if (m_sliderPreviewWidget) {
        m_sliderPreviewWidget->setHasFiniteDocumentBounds(hasFiniteDocumentBounds);
    }
}

// ============================================================================
// Geometry helpers
// ============================================================================

QRectF BrushControlOverlay::handleRect() const
{
    auto& theme = ThemeManager::instance();
    int pad = theme.scaled(BasePadding);
    int handleH = theme.scaled(BaseHandleHeight);
    return QRectF(pad, pad, width() - 2 * pad, handleH);
}

QRectF BrushControlOverlay::sizeSliderRect() const
{
    auto& theme = ThemeManager::instance();
    int pad = theme.scaled(BasePadding);
    int handleH = theme.scaled(BaseHandleHeight);
    int sliderW = theme.scaled(BaseSliderWidth);
    int sliderH = theme.scaled(BaseSliderHeight);

    int startY = pad + handleH + theme.scaled(4);
    int startX = (width() - sliderW) / 2;

    return QRectF(startX, startY, sliderW, sliderH);
}

QRectF BrushControlOverlay::brushPackButtonRect() const
{
    auto& theme = ThemeManager::instance();
    QRectF sizeRect = sizeSliderRect();
    int spacing = theme.scaled(BaseSliderSpacing);
    int btnH = theme.scaled(BaseButtonSize);

    return QRectF(sizeRect.left(), sizeRect.bottom() + spacing, btnH, btnH);
}

QRectF BrushControlOverlay::opacitySliderRect() const
{
    auto& theme = ThemeManager::instance();
    QRectF btnRect = brushPackButtonRect();
    int spacing = theme.scaled(BaseSliderSpacing);
    int sliderW = theme.scaled(BaseSliderWidth);
    int sliderH = theme.scaled(BaseSliderHeight);

    int startX = (width() - sliderW) / 2;
    return QRectF(startX, btnRect.bottom() + spacing, sliderW, sliderH);
}

BrushControlOverlay::DragMode BrushControlOverlay::hitTest(const QPoint& pos) const
{
    if (handleRect().contains(pos))
        return DragMode::Widget;
    return DragMode::None;
}

void BrushControlOverlay::handleDrag(const QPoint& globalPos)
{
    switch (m_dragMode) {
    case DragMode::Widget: {
        const QPoint delta = globalPos - m_dragStartPos;
        const QPoint newPos = m_widgetStartPos + delta;
        // The layout engine owns clamping, wall-collision and the actual move;
        // emit the raw target so it can resolve against the other overlays.
        emit positionChanged(newPos);
        break;
    }
    default:
        break;
    }
}

void BrushControlOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragMode = hitTest(event->pos());
        if (m_dragMode != DragMode::None) {
            m_dragStartPos = event->globalPosition().toPoint();
            m_widgetStartPos = pos();

            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void BrushControlOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragMode != DragMode::None) {
        QPoint globalPos = event->globalPosition().toPoint();
        if (m_dragMode == DragMode::Widget) {
            handleDrag(globalPos);
        }
        event->accept();
        return;
    }

    // Update cursor
    DragMode mode = hitTest(event->pos());
    if (mode == DragMode::Widget) {
        setCursor(Qt::OpenHandCursor);
    } else if (mode != DragMode::None) {
        setCursor(Qt::PointingHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    QWidget::mouseMoveEvent(event);
}

void BrushControlOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragMode != DragMode::None) {
        DragMode releasedMode = m_dragMode;
        m_dragMode = DragMode::None;

        if (releasedMode == DragMode::Widget) {
            // Let the layout engine settle (edge/corner snap, persist position).
            emit dragFinished();
        }

        update();

        // Update cursor
        DragMode mode = hitTest(event->pos());
        if (mode == DragMode::Widget) {
            setCursor(Qt::OpenHandCursor);
        } else if (mode != DragMode::None) {
            setCursor(Qt::PointingHandCursor);
        } else {
            setCursor(Qt::ArrowCursor);
        }

        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

// ============================================================================
// Drawing
// ============================================================================

void BrushControlOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    drawBackground(painter);
    drawHandle(painter, handleRect());
}

void BrushControlOverlay::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    // The frosted backdrop is sampled at the widget's position, so re-sample on
    // every move (incl. the layout engine's eased drag) to keep its CONTENT
    // correct. The frost stays locked to the chrome regardless (single painter),
    // so there is never positional desync — this only refreshes what shows.
    if (m_backdropSource) {
        update();
    }
}

void BrushControlOverlay::drawBackground(QPainter& painter)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    QColor borderTopColor = mgr.colors().border;
    QColor borderBottomColor = borderTopColor.darker(110);
    borderTopColor.setAlphaF(borderTopColor.alphaF() * 0.5);
    borderBottomColor.setAlphaF(borderBottomColor.alphaF() * 0.5);

    int radius = theme.scaled(BaseCornerRadius);

    QPainterPath bgPath;
    bgPath.addRoundedRect(rect(), radius, radius);

    painter.setPen(Qt::NoPen);

    // Frosted-glass backdrop (shared blurred snapshot); solid fallback until ready.
    QColor tint = mgr.colors().surface;
    tint.setAlpha(ruwa::ui::painting::kFrostTintAlpha);
    if (!ruwa::ui::painting::drawFrostedBackdrop(painter, this, m_backdropSource, bgPath, tint)) {
        QColor bgColor = mgr.colors().surface;
        bgColor.setAlpha(200); // Match stylus joystick / zoom panel overlay
        painter.setBrush(bgColor);
        painter.drawPath(bgPath);
    }

    // Gradient border
    ruwa::ui::painting::drawGradientBorder(
        painter, rect(), radius, borderTopColor, borderBottomColor);
}

void BrushControlOverlay::drawHandle(QPainter& painter, const QRectF& rect)
{
    auto& theme = ThemeManager::instance();
    auto& mgr = WidgetStyleManager::instance();

    QColor handleColor = mgr.colors().textMuted;
    int lineW = theme.scaled(BaseHandleLineWidth);
    int lineH = theme.scaled(BaseHandleLineHeight);

    QRectF lineRect(rect.center().x() - lineW / 2.0, rect.center().y() - lineH / 2.0, lineW, lineH);
    qreal radius = lineRect.height() / 2.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(handleColor);
    painter.drawRoundedRect(lineRect, radius, radius);
}

void BrushControlOverlay::onThemeChanged()
{
    updateSize();
    update();
}

void BrushControlOverlay::updateSliderGeometries()
{
    if (m_sizeSlider) {
        m_sizeSlider->setGeometry(sizeSliderRect().toRect());
    }
    if (m_opacitySlider) {
        m_opacitySlider->setGeometry(opacitySliderRect().toRect());
    }
    if (m_brushPackButton) {
        m_brushPackButton->setGeometry(brushPackButtonRect().toRect());
    }
}

} // namespace ruwa::ui::widgets
