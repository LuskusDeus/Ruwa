// SPDX-License-Identifier: MPL-2.0

// DockFloatingContainer.cpp
#include "DockFloatingContainer.h"
#include "DockContainerWidget.h"
#include "shell/docking/widgets/DockPanel.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>
#include <QVariantAnimation>
#include <QEasingCurve>

namespace ruwa::ui::docking {

namespace {

constexpr qreal kFloatingBorderWidth = 1.0;
constexpr qreal kFloatingOuterInset = 0.5;
constexpr int kFloatingContentInset = 2;
constexpr qreal kFloatingPanelCornerRadius = 6.0;

constexpr int kFloatingHMargins = 2 * kFloatingContentInset;
constexpr int kFloatingVMargins = 2 * kFloatingContentInset;

inline int floatingMinContainerWidth(const PanelSizeHints& h)
{
    return h.minWidth + kFloatingHMargins;
}

inline int floatingMinContainerHeight(const PanelSizeHints& h)
{
    return h.minHeight + kFloatingVMargins;
}

inline int floatingMaxContainerWidth(const PanelSizeHints& h)
{
    return qMax(floatingMinContainerWidth(h), h.maxWidth + kFloatingHMargins);
}

inline int floatingMaxContainerHeight(const PanelSizeHints& h)
{
    return qMax(floatingMinContainerHeight(h), h.maxHeight + kFloatingVMargins);
}

/// Outer size of the floating frame: hints.min* refer to the DockPanel client area inside margins.
inline QSize floatingContainerSizeForPanel(const DockPanel* panel)
{
    if (!panel) {
        return {};
    }
    const PanelSizeHints h = panel->sizeHints();
    const QSize eff = panel->effectiveFloatingSize();

    const int minW = floatingMinContainerWidth(h);
    const int minH = floatingMinContainerHeight(h);

    int w;
    if (h.userFloatingWidth > 0) {
        w = qMax(minW, eff.width());
    } else {
        w = qMax(minW, eff.width() + kFloatingHMargins);
    }

    int ht;
    if (h.userFloatingHeight > 0) {
        ht = qMax(minH, eff.height());
    } else {
        ht = qMax(minH, eff.height() + kFloatingVMargins);
    }

    w = qBound(minW, w, floatingMaxContainerWidth(h));
    ht = qBound(minH, ht, floatingMaxContainerHeight(h));
    return QSize(w, ht);
}

} // namespace

QSize DockFloatingContainer::outerSizeForPanel(const DockPanel* panel)
{
    return floatingContainerSizeForPanel(panel);
}

DockFloatingContainer::DockFloatingContainer(DockContainerWidget* container, DockPanel* panel)
    : QFrame(container)
    , m_container(container)
    , m_panel(panel)
{
    setupUI();
    setupAnimation();
}

DockFloatingContainer::~DockFloatingContainer()
{
    if (m_panel) {
        m_panel->setOverlayAnimationSuspended(false);
    }

    // Stop animation safely
    if (m_appearanceAnimation) {
        m_appearanceAnimation->disconnect();
        m_appearanceAnimation->stop();
        m_appearanceAnimation->deleteLater();
        m_appearanceAnimation = nullptr;
    }

    // Panel is handled by dockPanel/dockPanelRelativeTo before container deletion
    // Just clear our reference
    m_panel = nullptr;
}

// ============================================================================
// Position & Size
// ============================================================================

void DockFloatingContainer::moveTo(const QPoint& pos)
{
    move(pos);
    constrainToParent();
    emit moved(this->pos());
}

void DockFloatingContainer::moveBy(int dx, int dy)
{
    moveTo(pos() + QPoint(dx, dy));
}

void DockFloatingContainer::resizeTo(const QSize& size)
{
    QSize newSize = size;

    // Respect panel constraints (hints are panel interior; layout adds kFloatingContentInset each
    // side)
    if (m_panel) {
        const PanelSizeHints hints = m_panel->sizeHints();
        newSize.setWidth(qBound(
            floatingMinContainerWidth(hints), newSize.width(), floatingMaxContainerWidth(hints)));
        newSize.setHeight(qBound(floatingMinContainerHeight(hints), newSize.height(),
            floatingMaxContainerHeight(hints)));
    }

    resize(newSize);
    constrainToParent();
    emit resized(this->size());
}

void DockFloatingContainer::startDrag(const QPoint& globalPos)
{
    // If animation is running, just update cursor position - don't reset drag state
    if (m_animatingAppearance) {
        QPoint localPos = parentWidget() ? parentWidget()->mapFromGlobal(globalPos) : globalPos;
        updateAppearanceAnimation(localPos);
        return;
    }

    m_dragging = true;
    m_dragStartPos = globalPos;
    m_dragStartGeom = pos();
    raise();
    emit dragStarted();
}

void DockFloatingContainer::updateDrag(const QPoint& globalPos)
{
    if (!m_dragging)
        return;

    // During appearance animation, update cursor position for animation
    if (m_animatingAppearance) {
        QPoint localPos = parentWidget() ? parentWidget()->mapFromGlobal(globalPos) : globalPos;
        updateAppearanceAnimation(localPos);
        return;
    }

    // Normal drag
    QPoint delta = globalPos - m_dragStartPos;
    moveTo(m_dragStartGeom + delta);
}

void DockFloatingContainer::endDrag()
{
    // Don't end drag while appearance animation is running
    if (m_animatingAppearance) {
        return;
    }

    if (!m_dragging)
        return;

    m_dragging = false;
    emit dragFinished();
}

// ============================================================================
// Animation
// ============================================================================

void DockFloatingContainer::animateAppearance(
    const QRect& sourceGeom, const QPoint& cursorPos, int duration)
{
    if (!m_panel || !m_appearanceAnimation)
        return;

    // Stop any running animation
    if (m_appearanceAnimation->state() == QAbstractAnimation::Running) {
        m_panel->setOverlayAnimationSuspended(false);
        m_appearanceAnimation->stop();
    }

    m_animTargetSize = floatingContainerSizeForPanel(m_panel);
    m_animStartSize = sourceGeom.size();

    // Anchor point: the point that should follow the cursor
    // Start: center-top of source geometry
    // End: cursor position
    m_animStartAnchor = QPoint(sourceGeom.x() + sourceGeom.width() / 2,
        sourceGeom.y() + 14 // Title bar area
    );

    // Current cursor position in local coords
    m_lastCursorPos = cursorPos;

    // Set initial geometry
    resize(m_animStartSize);
    move(m_animStartAnchor.x() - width() / 2, m_animStartAnchor.y() - 14);

    // Determine duration
    int actualDuration = (duration > 0) ? duration : m_animationDuration;

    // Start animation
    m_animatingAppearance = true;
    m_dragging = true; // Enable drag during animation
    m_panel->setOverlayAnimationSuspended(true);
    m_appearanceAnimation->setDuration(actualDuration);
    m_appearanceAnimation->setCurrentTime(0);
    m_appearanceAnimation->start();
}

void DockFloatingContainer::updateAppearanceAnimation(const QPoint& cursorPos)
{
    if (!m_animatingAppearance)
        return;

    m_lastCursorPos = cursorPos;

    // Recalculate position based on current cursor and animation progress
    double progress
        = m_appearanceAnimation ? m_appearanceAnimation->currentValue().toDouble() : 1.0;
    applyAnimationFrame(progress);
}

void DockFloatingContainer::onAppearanceAnimationValueChanged(const QVariant& value)
{
    if (!m_animatingAppearance)
        return;
    applyAnimationFrame(value.toDouble());
}

void DockFloatingContainer::applyAnimationFrame(double progress)
{
    // Interpolate size
    int w = m_animStartSize.width()
        + qRound((m_animTargetSize.width() - m_animStartSize.width()) * progress);
    int h = m_animStartSize.height()
        + qRound((m_animTargetSize.height() - m_animStartSize.height()) * progress);
    resize(w, h);

    // Interpolate anchor point from start anchor to current cursor position
    int anchorX
        = m_animStartAnchor.x() + qRound((m_lastCursorPos.x() - m_animStartAnchor.x()) * progress);
    int anchorY
        = m_animStartAnchor.y() + qRound((m_lastCursorPos.y() - m_animStartAnchor.y()) * progress);

    // Position container so anchor is at calculated position
    // Anchor is at center-top of container (with 14px offset for title bar)
    move(anchorX - w / 2, anchorY - 14);
}

void DockFloatingContainer::onAppearanceAnimationFinished()
{
    m_animatingAppearance = false;

    // Apply final state
    resize(m_animTargetSize);
    move(m_lastCursorPos.x() - width() / 2, m_lastCursorPos.y() - 14);
    constrainToParent();

    // Setup for normal drag operation
    m_dragStartPos = QCursor::pos();
    m_dragStartGeom = pos();

    if (m_panel) {
        m_panel->setOverlayAnimationSuspended(false);
    }

    emit appearanceAnimationFinished();
}

// ============================================================================
// Resize
// ============================================================================

void DockFloatingContainer::setResizeMargin(int margin)
{
    m_resizeMargin = margin;
}

ResizeEdge DockFloatingContainer::resizeEdgeAt(const QPoint& localPos) const
{
    if (!m_panel || !m_panel->isResizable()) {
        return ResizeEdge::None;
    }

    int x = localPos.x();
    int y = localPos.y();
    int w = width();
    int h = height();
    int m = m_resizeMargin;

    ResizeEdge edge = ResizeEdge::None;

    // Check edges
    if (x < m) {
        edge = static_cast<ResizeEdge>(static_cast<int>(edge) | static_cast<int>(ResizeEdge::Left));
    } else if (x >= w - m) {
        edge
            = static_cast<ResizeEdge>(static_cast<int>(edge) | static_cast<int>(ResizeEdge::Right));
    }

    if (y < m) {
        edge = static_cast<ResizeEdge>(static_cast<int>(edge) | static_cast<int>(ResizeEdge::Top));
    } else if (y >= h - m) {
        edge = static_cast<ResizeEdge>(
            static_cast<int>(edge) | static_cast<int>(ResizeEdge::Bottom));
    }

    return edge;
}

// ============================================================================
// Theme
// ============================================================================

void DockFloatingContainer::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    m_shadowColor = colors.shadow(80);
    m_borderColor = colors.border;
    m_panelSurfaceColor = colors.surface;

    // Update shadow effect
    auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(graphicsEffect());
    if (shadow) {
        shadow->setColor(m_shadowColor);
    }

    setStyleSheet(QString(R"(
        ruwa--ui--docking--DockFloatingContainer {
            background: transparent;
            border: none;
        }
    )"));

    update();
}

// ============================================================================
// Events
// ============================================================================

void DockFloatingContainer::paintEvent(QPaintEvent* event)
{
    if (m_panel) {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Rectangular widget bounds stay opaque unless we clear: otherwise anti-aliased
        // corners composite with a wrong plate over docked panels (canvas often matches by luck).
        p.fillRect(rect(), Qt::transparent);

        if (m_panelSurfaceColor.isValid()) {
            const QRectF br(kFloatingOuterInset, kFloatingOuterInset,
                width() - (kFloatingOuterInset * 2.0), height() - (kFloatingOuterInset * 2.0));
            const qreal r = m_panel->baseCornerRadius() > 0 ? qreal(m_panel->baseCornerRadius())
                                                            : kFloatingPanelCornerRadius;
            QPainterPath path;
            path.addRoundedRect(br, r, r);
            p.fillPath(path, m_panelSurfaceColor);
        }
        QWidget::paintEvent(event);
        return;
    }

    QFrame::paintEvent(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath borderPath;
    const QRectF borderRect(kFloatingOuterInset, kFloatingOuterInset,
        width() - (kFloatingOuterInset * 2.0), height() - (kFloatingOuterInset * 2.0));
    constexpr qreal borderRadius = 5.5;
    borderPath.addRoundedRect(borderRect, borderRadius, borderRadius);

    QPen borderPen(m_borderColor);
    borderPen.setWidthF(kFloatingBorderWidth);
    borderPen.setCosmetic(true);
    borderPen.setJoinStyle(Qt::RoundJoin);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(borderPath);
}

void DockFloatingContainer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        raise();

        ResizeEdge edge = resizeEdgeAt(event->pos());

        if (edge != ResizeEdge::None) {
            // Start resize
            m_resizing = true;
            m_resizeEdge = edge;
            m_resizeStartPos = event->globalPosition().toPoint();
            m_resizeStartGeom = geometry();
        }
    }

    QFrame::mousePressEvent(event);
}

void DockFloatingContainer::mouseMoveEvent(QMouseEvent* event)
{
    if (m_resizing) {
        QPoint globalPos = event->globalPosition().toPoint();
        QPoint delta = globalPos - m_resizeStartPos;
        QRect newGeom = m_resizeStartGeom;

        // Apply resize based on edge
        int edge = static_cast<int>(m_resizeEdge);

        if (edge & static_cast<int>(ResizeEdge::Left)) {
            newGeom.setLeft(newGeom.left() + delta.x());
        }
        if (edge & static_cast<int>(ResizeEdge::Right)) {
            newGeom.setRight(newGeom.right() + delta.x());
        }
        if (edge & static_cast<int>(ResizeEdge::Top)) {
            newGeom.setTop(newGeom.top() + delta.y());
        }
        if (edge & static_cast<int>(ResizeEdge::Bottom)) {
            newGeom.setBottom(newGeom.bottom() + delta.y());
        }

        // Respect minimum outer size (panel minima + layout margins)
        if (m_panel) {
            const PanelSizeHints hints = m_panel->sizeHints();
            const int minW = floatingMinContainerWidth(hints);
            const int minH = floatingMinContainerHeight(hints);
            if (newGeom.width() < minW) {
                if (edge & static_cast<int>(ResizeEdge::Left)) {
                    newGeom.setLeft(newGeom.right() - minW);
                } else {
                    newGeom.setRight(newGeom.left() + minW);
                }
            }
            if (newGeom.height() < minH) {
                if (edge & static_cast<int>(ResizeEdge::Top)) {
                    newGeom.setTop(newGeom.bottom() - minH);
                } else {
                    newGeom.setBottom(newGeom.top() + minH);
                }
            }
            const int maxW = floatingMaxContainerWidth(hints);
            const int maxH = floatingMaxContainerHeight(hints);
            if (newGeom.width() > maxW) {
                if (edge & static_cast<int>(ResizeEdge::Left)) {
                    newGeom.setLeft(newGeom.right() - maxW);
                } else {
                    newGeom.setRight(newGeom.left() + maxW);
                }
            }
            if (newGeom.height() > maxH) {
                if (edge & static_cast<int>(ResizeEdge::Top)) {
                    newGeom.setTop(newGeom.bottom() - maxH);
                } else {
                    newGeom.setBottom(newGeom.top() + maxH);
                }
            }
        }

        setGeometry(newGeom);
        constrainToParent();

    } else if (!m_dragging) {
        // Update cursor based on position
        updateCursor(resizeEdgeAt(event->pos()));
    }

    QFrame::mouseMoveEvent(event);
}

void DockFloatingContainer::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_resizing) {
            m_resizing = false;
            m_resizeEdge = ResizeEdge::None;

            // Save the new size as user's preferred floating size
            if (m_panel) {
                m_panel->setUserFloatingSize(width(), height());
            }

            emit resized(size());
        }
    }

    QFrame::mouseReleaseEvent(event);
}

void DockFloatingContainer::enterEvent(QEnterEvent* event)
{
    QFrame::enterEvent(event);
}

void DockFloatingContainer::leaveEvent(QEvent* event)
{
    if (!m_resizing) {
        setCursor(Qt::ArrowCursor);
    }
    QFrame::leaveEvent(event);
}

// ============================================================================
// Private
// ============================================================================

void DockFloatingContainer::setupUI()
{
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    // Required so pixels outside the rounded fill stay transparent (see paintEvent).
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);

    // Shadow effect
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(m_shadowRadius);
    shadow->setOffset(0, 2);
    shadow->setColor(QColor(0, 0, 0, 80));
    setGraphicsEffect(shadow);

    // Keep the panel away from the anti-aliased rounded border so corners stay visible.
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(
        kFloatingContentInset, kFloatingContentInset, kFloatingContentInset, kFloatingContentInset);
    layout->setSpacing(0);

    if (m_panel) {
        m_panel->setParent(this);
        m_panel->setFloatingContainer(this);
        layout->addWidget(m_panel);

        resize(floatingContainerSizeForPanel(m_panel));
    }
}

void DockFloatingContainer::setupAnimation()
{
    m_appearanceAnimation = new QVariantAnimation(this);
    m_appearanceAnimation->setStartValue(0.0);
    m_appearanceAnimation->setEndValue(1.0);
    m_appearanceAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_appearanceAnimation, &QVariantAnimation::valueChanged, this,
        &DockFloatingContainer::onAppearanceAnimationValueChanged);
    connect(m_appearanceAnimation, &QVariantAnimation::finished, this,
        &DockFloatingContainer::onAppearanceAnimationFinished);
}

void DockFloatingContainer::updateCursor(ResizeEdge edge)
{
    if (!m_panel || !m_panel->isResizable()) {
        setCursor(Qt::ArrowCursor);
        return;
    }

    switch (edge) {
    case ResizeEdge::Left:
    case ResizeEdge::Right:
        setCursor(Qt::SizeHorCursor);
        break;
    case ResizeEdge::Top:
    case ResizeEdge::Bottom:
        setCursor(Qt::SizeVerCursor);
        break;
    case ResizeEdge::TopLeft:
    case ResizeEdge::BottomRight:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case ResizeEdge::TopRight:
    case ResizeEdge::BottomLeft:
        setCursor(Qt::SizeBDiagCursor);
        break;
    default:
        setCursor(Qt::ArrowCursor);
        break;
    }
}

void DockFloatingContainer::constrainToParent()
{
    QRect bounds = parentBounds();
    QRect geom = geometry();

    // Keep at least part visible
    int minVisible = 50;

    if (geom.left() > bounds.right() - minVisible) {
        geom.moveLeft(bounds.right() - minVisible);
    }
    if (geom.right() < bounds.left() + minVisible) {
        geom.moveRight(bounds.left() + minVisible);
    }
    if (geom.top() < bounds.top()) {
        geom.moveTop(bounds.top());
    }
    if (geom.top() > bounds.bottom() - minVisible) {
        geom.moveTop(bounds.bottom() - minVisible);
    }

    if (geom != geometry()) {
        setGeometry(geom);
    }
}

QRect DockFloatingContainer::parentBounds() const
{
    if (parentWidget()) {
        return parentWidget()->rect();
    }
    return QRect();
}

} // namespace ruwa::ui::docking
