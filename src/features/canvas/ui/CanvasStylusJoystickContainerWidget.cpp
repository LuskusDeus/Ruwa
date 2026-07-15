// SPDX-License-Identifier: MPL-2.0

#include "CanvasStylusJoystickContainerWidget.h"
#include "features/canvas/ui/CanvasOverlayContextActions.h"
#include "CanvasStylusJoystickWidget.h"
#include "ZoomFitIconButton.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyleManager.h"
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QVariantAnimation>
#include <QResizeEvent>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QStringLiteral>
#include <QVariantList>
#include <QVariantMap>
#include <QtMath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

constexpr int kZoomSliderMin = 0;
constexpr int kZoomSliderMax = 1000;
constexpr int kPanelHeight = 40;
constexpr int kHandleHeight = 16; // Extra height for drag handle (like BrushControlOverlay)
constexpr int kZoomButtonSize = 28;
constexpr int kPanelPadding = 6;
constexpr int kSliderMinHeight = 28;
constexpr int kHandleLineWidth = 40;
constexpr int kHandleLineHeight = 3;
constexpr int kSwapAnimMs = 160; ///< Duration of the joystick/panel reorder slide.
constexpr int kSliderSurfaceOpacityPercent = 58;
constexpr int kSliderTrackOpacityPercent = 0;
constexpr int kSliderFillOpacityPercent = 85;

} // namespace

qreal CanvasStylusJoystickContainerWidget::sliderValueToZoom(int value) const
{
    const qreal ratio
        = static_cast<qreal>(value - kZoomSliderMin) / (kZoomSliderMax - kZoomSliderMin);
    const qreal logMin = std::log(m_minZoom);
    const qreal logMax = std::log(m_maxZoom);
    return std::exp(logMin + ratio * (logMax - logMin));
}

int CanvasStylusJoystickContainerWidget::zoomToSliderValue(qreal zoom) const
{
    const qreal clamped = qBound(m_minZoom, zoom, m_maxZoom);
    const qreal logMin = std::log(m_minZoom);
    const qreal logMax = std::log(m_maxZoom);
    const qreal logZoom = std::log(clamped);
    const qreal ratio = (logZoom - logMin) / (logMax - logMin);
    return kZoomSliderMin + qRound(ratio * (kZoomSliderMax - kZoomSliderMin));
}

// Zoom panel container — rounded corners with mask, handle at top (like BrushControlOverlay)
class ZoomPanelWidget : public QWidget {
public:
    explicit ZoomPanelWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setMouseTracking(true);
        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, [this]() { update(); });
    }

    void setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source)
    {
        if (m_backdropSource == source) {
            return;
        }
        m_backdropSource = source;
        update();
    }

protected:
    void mouseMoveEvent(QMouseEvent* event) override
    {
        updateCursorForPosition(event->pos());
        QWidget::mouseMoveEvent(event);
    }

    void enterEvent(QEnterEvent* event) override
    {
        updateCursorForPosition(event->position().toPoint());
        QWidget::enterEvent(event);
    }

    void moveEvent(QMoveEvent* event) override
    {
        QWidget::moveEvent(event);
        // Re-sample frost content at the new position (joystick/panel swap slide).
        if (m_backdropSource) {
            update();
        }
    }

    void leaveEvent(QEvent* event) override
    {
        unsetCursor();
        QWidget::leaveEvent(event);
    }

private:
    void updateCursorForPosition(const QPoint& pos)
    {
        const int handleH = ruwa::ui::core::WidgetStyleManager::instance().scaled(kHandleHeight);
        if (pos.y() < handleH) {
            setCursor(Qt::OpenHandCursor);
        } else {
            unsetCursor();
        }
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();
        p.setPen(Qt::NoPen);
        const int radius = ruwa::ui::core::WidgetStyleManager::instance().scaled(6);

        // Frosted-glass backdrop (shared blurred snapshot); solid fallback.
        QPainterPath bgPath;
        bgPath.addRoundedRect(rect(), radius, radius);
        QColor tint = colors.surface;
        tint.setAlpha(ruwa::ui::painting::kFrostTintAlpha);
        if (!ruwa::ui::painting::drawFrostedBackdrop(p, this, m_backdropSource, bgPath, tint)) {
            QColor bg = colors.surface;
            bg.setAlpha(200); // Semi-transparent to blend with canvas
            p.setBrush(bg);
            p.drawRoundedRect(rect(), radius, radius);
        }

        QColor borderTopColor = colors.border;
        QColor borderBottomColor = borderTopColor.darker(110);
        borderTopColor.setAlphaF(borderTopColor.alphaF() * 0.5);
        borderBottomColor.setAlphaF(borderBottomColor.alphaF() * 0.5);
        ruwa::ui::painting::drawGradientBorder(
            p, QRectF(rect()), radius, borderTopColor, borderBottomColor);

        // Draw handle (rounded line) at top, like BrushControlOverlay
        const int handleH = ruwa::ui::core::WidgetStyleManager::instance().scaled(kHandleHeight);
        const int lineW = ruwa::ui::core::WidgetStyleManager::instance().scaled(kHandleLineWidth);
        const int lineH = ruwa::ui::core::WidgetStyleManager::instance().scaled(kHandleLineHeight);
        QRectF lineRect(
            rect().center().x() - lineW / 2.0, handleH / 2.0 - lineH / 2.0, lineW, lineH);
        qreal lineRadius = lineRect.height() / 2.0;
        p.setBrush(colors.textMuted);
        p.drawRoundedRect(lineRect, lineRadius, lineRadius);
    }

private:
    ruwa::shared::rendering::ICanvasBackdropSource* m_backdropSource = nullptr;
};

CanvasStylusJoystickContainerWidget::CanvasStylusJoystickContainerWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setTabletTracking(true);

    m_joystick = new CanvasStylusJoystickWidget(this);

    m_zoomPanel = new ZoomPanelWidget(this);
    m_zoomPanel->installEventFilter(this);

    auto* panelLayout = new QHBoxLayout(m_zoomPanel);
    panelLayout->setContentsMargins(kPanelPadding,
        kPanelPadding + ruwa::ui::core::WidgetStyleManager::instance().scaled(kHandleHeight),
        kPanelPadding, kPanelPadding);
    panelLayout->setSpacing(kPanelPadding);

    auto* zoomBtn = new ZoomFitIconButton(m_zoomPanel);
    connect(zoomBtn, &ZoomFitIconButton::clicked, this,
        &CanvasStylusJoystickContainerWidget::onZoomButtonClicked);
    panelLayout->addWidget(zoomBtn);

    m_zoomSlider = new ProgressHandleSlider(m_zoomPanel);
    m_zoomSlider->setOrientation(Qt::Horizontal);
    m_zoomSlider->setRange(kZoomSliderMin, kZoomSliderMax);
    m_zoomSlider->setValue(zoomToSliderValue(1.0));
    m_zoomSlider->setShowValueText(false);
    m_zoomSlider->setBackgroundOpacity(
        kSliderSurfaceOpacityPercent / 100.0, kSliderTrackOpacityPercent / 100.0);
    m_zoomSlider->setProgressFillOpacity(kSliderFillOpacityPercent / 100.0);
    m_zoomSlider->setCursor(Qt::PointingHandCursor);
    m_zoomSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_zoomSlider->setMinimumHeight(
        ruwa::ui::core::WidgetStyleManager::instance().scaled(kSliderMinHeight));
    connect(m_zoomSlider, &ProgressHandleSlider::valueChanged, this,
        &CanvasStylusJoystickContainerWidget::onZoomSliderValueChanged);
    panelLayout->addWidget(m_zoomSlider, 1);

    const int panelTotalHeight
        = ruwa::ui::core::WidgetStyleManager::instance().scaled(kPanelHeight + kHandleHeight);
    m_zoomPanel->setFixedHeight(panelTotalHeight);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_joystick);
    mainLayout->addWidget(m_zoomPanel);

    const int joystickSize = 250; // Match CanvasStylusJoystickWidget::kWidgetSizePx
    const int totalHeight = joystickSize + panelTotalHeight;
    setFixedSize(joystickSize, totalHeight);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    connect(m_joystick, &CanvasStylusJoystickWidget::panRequested, this,
        &CanvasStylusJoystickContainerWidget::panRequested);
    connect(m_joystick, &CanvasStylusJoystickWidget::rotationRequested, this,
        &CanvasStylusJoystickContainerWidget::rotationRequested);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &CanvasStylusJoystickContainerWidget::onThemeChanged);
}

CanvasStylusJoystickContainerWidget::~CanvasStylusJoystickContainerWidget() = default;

bool CanvasStylusJoystickContainerWidget::hitTestInteractiveArea(const QPointF& pos) const
{
    const QPoint localPos = pos.toPoint();
    if (!rect().contains(localPos)) {
        return false;
    }

    if (m_joystick && m_joystick->geometry().contains(localPos)) {
        const QPointF joystickPos = QPointF(m_joystick->mapFrom(this, localPos));
        if (m_joystick->hitTestInteractiveArea(joystickPos)) {
            return true;
        }
    }

    if (m_zoomPanel && m_zoomPanel->isVisible()) {
        const QPoint panelPos = m_zoomPanel->mapFrom(this, localPos);
        if (m_zoomPanel->rect().contains(panelPos)) {
            const QRegion mask = m_zoomPanel->mask();
            return mask.isEmpty() || mask.contains(panelPos);
        }
    }

    return false;
}

bool CanvasStylusJoystickContainerWidget::isJoystickInteractionActive() const
{
    return m_joystick && m_joystick->isInteractionActive();
}

QVariantMap CanvasStylusJoystickContainerWidget::contextMenuContext() const
{
    using ruwa::ui::canvas_overlay::CanvasOverlayContextActionId;

    QVariantList actions;
    QVariantMap hide;
    hide.insert(
        QStringLiteral("id"), static_cast<int>(CanvasOverlayContextActionId::HideStylusJoystick));
    hide.insert(QStringLiteral("text"), tr("Hide widget"));
    hide.insert(QStringLiteral("danger"), false);
    hide.insert(QStringLiteral("standardIcon"),
        static_cast<int>(IconProvider::StandardIcon::EyeDeactivated));
    actions.append(hide);

    QVariantMap ctx;
    ctx.insert(QStringLiteral("simpleActions"), actions);
    return ctx;
}

void CanvasStylusJoystickContainerWidget::setJoystickAbovePanel(bool above)
{
    // Cancel any in-flight swap slide and hand geometry back to the layout, so a
    // programmatic order change (restore/preset) applies cleanly and instantly.
    if (m_swapAnim) {
        m_swapAnim->stop(); // DeleteWhenStopped frees it
        m_swapAnim = nullptr;
        if (auto* l = layout()) {
            l->setEnabled(true);
        }
    }
    if (m_joystickAbovePanel == above) {
        return;
    }
    m_joystickAbovePanel = above;
    applyLayoutOrder();
}

void CanvasStylusJoystickContainerWidget::setBackdropSource(
    ruwa::shared::rendering::ICanvasBackdropSource* source)
{
    if (m_backdropSource == source) {
        return;
    }
    m_backdropSource = source;
    // Both painting children paint their own frost from the SAME shared snapshot.
    if (m_joystick) {
        m_joystick->setBackdropSource(source);
    }
    if (m_zoomPanel) {
        // m_zoomPanel is always a ZoomPanelWidget (no Q_OBJECT -> static_cast).
        static_cast<ZoomPanelWidget*>(m_zoomPanel)->setBackdropSource(source);
    }
}

void CanvasStylusJoystickContainerWidget::refreshBackdropContent()
{
    if (m_joystick) {
        m_joystick->update();
    }
    if (m_zoomPanel) {
        m_zoomPanel->update();
    }
}

void CanvasStylusJoystickContainerWidget::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    // The children don't get their own moveEvent when the container relocates
    // (their local position is unchanged), so refresh their backdrop content here
    // — the frost stays locked to each child's chrome regardless (single painter).
    if (m_backdropSource) {
        refreshBackdropContent();
    }
}

void CanvasStylusJoystickContainerWidget::setZoomLimits(qreal minZoom, qreal maxZoom)
{
    if (qFuzzyCompare(m_minZoom, minZoom) && qFuzzyCompare(m_maxZoom, maxZoom)) {
        return;
    }
    m_minZoom = std::max(0.001, minZoom);
    m_maxZoom = std::max(m_minZoom, maxZoom);
}

void CanvasStylusJoystickContainerWidget::setZoom(qreal zoom)
{
    m_sliderUpdateFromExternal = true;
    const int newValue = zoomToSliderValue(zoom);
    if (m_zoomSlider->value() != newValue) {
        m_zoomSlider->blockSignals(true);
        m_zoomSlider->setValue(newValue);
        m_zoomSlider->blockSignals(false);
    }
    m_sliderUpdateFromExternal = false;
}

void CanvasStylusJoystickContainerWidget::setRotation(qreal radians)
{
    m_joystick->setRingRotation(radians);
}

void CanvasStylusJoystickContainerWidget::onZoomButtonClicked()
{
    emit zoomToFitRequested();
}

void CanvasStylusJoystickContainerWidget::onZoomSliderValueChanged(int value)
{
    if (m_sliderUpdateFromExternal) {
        return;
    }
    const qreal zoom = sliderValueToZoom(value);
    emit zoomChangeRequested(zoom);
}

void CanvasStylusJoystickContainerWidget::onThemeChanged() { }

QRectF CanvasStylusJoystickContainerWidget::handleRect() const
{
    if (!m_zoomPanel)
        return QRectF();
    const int handleH = ruwa::ui::core::WidgetStyleManager::instance().scaled(kHandleHeight);
    return QRectF(m_zoomPanel->x(), m_zoomPanel->y(), m_zoomPanel->width(), handleH);
}

void CanvasStylusJoystickContainerWidget::handleDrag(const QPoint& globalPos)
{
    if (!parentWidget())
        return;
    const QPoint delta = globalPos - m_dragStartPos;
    QPoint target = m_widgetStartPos + delta;
    // Evaluate the joystick/panel swap on the logical cursor target (deterministic,
    // once per mouse event) rather than on the lagging engine-driven visual move.
    target = maybeSwapForTarget(target);
    // The layout engine clamps, applies wall-collision and eases the widget toward
    // this target with a trailing lag; emit the (possibly swap-shifted) target.
    emit positionChanged(target);
}

QPoint CanvasStylusJoystickContainerWidget::maybeSwapForTarget(QPoint target)
{
    if (!parentWidget() || !m_zoomPanel || !m_joystick) {
        return target;
    }
    const int parentCenterY = parentWidget()->height() / 2;
    const int joystickH = m_joystick->height();

    // Drag-handle Y of the *target* in parent coords. Joystick on top → handle (zoom
    // panel top edge) sits a joystickH below the widget top; panel on top → at the top.
    const int handleY = m_joystickAbovePanel ? (target.y() + joystickH) : target.y();

    // Hysteresis around center avoids flicker. On swap the widget shifts by ±joystickH
    // so handleY is preserved, which makes the reverse condition impossible next frame.
    constexpr int threshold = 24;
    bool willSwap = false;
    if (m_joystickAbovePanel && handleY < parentCenterY - threshold) {
        willSwap = true; // handle crossed above center → panel on top
    } else if (!m_joystickAbovePanel && handleY > parentCenterY + threshold) {
        willSwap = true; // handle crossed below center → joystick on top
    }
    if (!willSwap) {
        return target;
    }

    const int deltaY = m_joystickAbovePanel ? joystickH : -joystickH;
    // Shift the logical target so the handle ends under the cursor across the flip,
    // and keep the drag grab offset consistent for subsequent moves. The engine eases
    // the container toward this shifted target while the children animate their swap,
    // so the handle stays roughly under the cursor with no instant jump.
    target.ry() += deltaY;
    m_widgetStartPos.ry() += deltaY;
    m_joystickAbovePanel = !m_joystickAbovePanel;
    animateLayoutSwap();
    return target;
}

void CanvasStylusJoystickContainerWidget::applyLayoutOrder()
{
    auto* mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (!mainLayout || !m_joystick || !m_zoomPanel) {
        return;
    }

    mainLayout->removeWidget(m_joystick);
    mainLayout->removeWidget(m_zoomPanel);
    if (m_joystickAbovePanel) {
        mainLayout->addWidget(m_joystick);
        mainLayout->addWidget(m_zoomPanel);
    } else {
        mainLayout->addWidget(m_zoomPanel);
        mainLayout->addWidget(m_joystick);
    }
}

void CanvasStylusJoystickContainerWidget::animateLayoutSwap()
{
    auto* lay = layout();
    if (!lay || !m_joystick || !m_zoomPanel) {
        applyLayoutOrder();
        return;
    }

    // Capture the children's current on-screen geometry (mid-flight if a previous
    // swap is still animating) before we touch the layout.
    const QRect jStart = m_joystick->geometry();
    const QRect pStart = m_zoomPanel->geometry();

    if (m_swapAnim) {
        m_swapAnim->stop(); // DeleteWhenStopped frees it
        m_swapAnim = nullptr;
    }

    // Reorder, then let the (re-enabled) layout compute the post-swap geometry.
    applyLayoutOrder();
    lay->setEnabled(true);
    lay->activate();
    const QRect jEnd = m_joystick->geometry();
    const QRect pEnd = m_zoomPanel->geometry();
    if (jStart == jEnd && pStart == pEnd) {
        return;
    }

    // Freeze the layout and drive the children from start to end ourselves.
    lay->setEnabled(false);
    m_joystick->setGeometry(jStart);
    m_zoomPanel->setGeometry(pStart);

    auto lerpRect = [](const QRect& a, const QRect& b, qreal t) {
        return QRect(qRound(a.x() + (b.x() - a.x()) * t), qRound(a.y() + (b.y() - a.y()) * t),
            qRound(a.width() + (b.width() - a.width()) * t),
            qRound(a.height() + (b.height() - a.height()) * t));
    };

    m_swapAnim = new QVariantAnimation(this);
    m_swapAnim->setStartValue(0.0);
    m_swapAnim->setEndValue(1.0);
    m_swapAnim->setDuration(kSwapAnimMs);
    m_swapAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_swapAnim, &QVariantAnimation::valueChanged, this,
        [this, jStart, pStart, jEnd, pEnd, lerpRect](const QVariant& v) {
            const qreal t = v.toReal();
            if (m_joystick)
                m_joystick->setGeometry(lerpRect(jStart, jEnd, t));
            if (m_zoomPanel)
                m_zoomPanel->setGeometry(lerpRect(pStart, pEnd, t));
        });
    connect(m_swapAnim, &QVariantAnimation::finished, this, [this, jEnd, pEnd]() {
        if (m_joystick)
            m_joystick->setGeometry(jEnd);
        if (m_zoomPanel)
            m_zoomPanel->setGeometry(pEnd);
        if (auto* l = layout())
            l->setEnabled(true);
        m_swapAnim = nullptr;
    });
    m_swapAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void CanvasStylusJoystickContainerWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && handleRect().contains(event->pos())) {
        m_dragging = true;
        m_dragStartPos = event->globalPosition().toPoint();
        m_widgetStartPos = pos();
        grabMouse();
        m_zoomPanel->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CanvasStylusJoystickContainerWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        handleDrag(event->globalPosition().toPoint());
        event->accept();
        return;
    }
    if (handleRect().contains(event->pos())) {
        setCursor(Qt::OpenHandCursor);
    } else {
        unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void CanvasStylusJoystickContainerWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        releaseMouse();
        if (handleRect().contains(event->pos())) {
            m_zoomPanel->setCursor(Qt::OpenHandCursor);
        } else {
            m_zoomPanel->unsetCursor();
        }
        // Let the layout engine settle (edge/corner snap, persist position).
        emit dragFinished();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

bool CanvasStylusJoystickContainerWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_zoomPanel || event->type() != QEvent::MouseButtonPress) {
        return QWidget::eventFilter(watched, event);
    }
    const int handleH = ruwa::ui::core::WidgetStyleManager::instance().scaled(kHandleHeight);
    QPoint localPos = static_cast<QMouseEvent*>(event)->pos();
    if (localPos.y() >= handleH) {
        return QWidget::eventFilter(watched, event);
    }
    QPoint containerPos = m_zoomPanel->mapTo(this, localPos);
    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
    QMouseEvent mappedEvent(QEvent::MouseButtonPress, containerPos, mouseEvent->globalPosition(),
        mouseEvent->button(), mouseEvent->buttons(), mouseEvent->modifiers());
    QApplication::sendEvent(this, &mappedEvent);
    return true; // Filter so panel doesn't get it; container has grabMouse for move/release
}

} // namespace ruwa::ui::widgets
