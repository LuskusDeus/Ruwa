// SPDX-License-Identifier: MPL-2.0

// DockFloatingContainer.cpp
#include "DockFloatingContainer.h"
#include "DockContainerWidget.h"
#include "shell/docking/widgets/DockPanel.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QGraphicsBlurEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QImage>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QtMath>

namespace ruwa::ui::docking {

namespace {

constexpr qreal kFloatingBorderWidth = 1.0;
constexpr qreal kFloatingOuterInset = 0.5;
constexpr int kFloatingContentInset = 2;
constexpr qreal kFloatingPanelCornerRadius = 6.0;

/// Vertical offset of the floating panel's drop shadow.
constexpr int kFloatingShadowOffsetY = 2;

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

/**
 * @brief Sibling layer painting a pre-blurred drop shadow behind a floating panel.
 *
 * Replaces QGraphicsDropShadowEffect on DockFloatingContainer. Qt invalidates a
 * widget's graphics effect from every descendant repaint
 * (QWidgetPrivate::invalidateGraphicsEffectsRecursively walks the parent chain), so
 * with the effect installed a single dirty pixel anywhere inside a floating panel
 * re-rendered the entire panel into a pixmap and re-ran a CPU Gaussian blur over it.
 * The shadow silhouette only depends on the frame's size, so it is baked once per
 * geometry/theme change and blitted afterwards.
 */
class FloatingShadowLayer final : public QWidget {
public:
    explicit FloatingShadowLayer(QWidget* parent, int blurRadius)
        : QWidget(parent)
        , m_blurRadius(qMax(0, blurRadius))
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setFocusPolicy(Qt::NoFocus);
    }

    int margin() const { return m_blurRadius + kFloatingShadowOffsetY; }

    void setAppearance(const QColor& color, qreal cornerRadius)
    {
        if (m_color == color && qFuzzyCompare(m_cornerRadius + 1.0, cornerRadius + 1.0)) {
            return;
        }
        m_color = color;
        m_cornerRadius = cornerRadius;
        m_cache = QPixmap();
        update();
    }

    /// Wrap @p bodyGeometry (the frame's geometry in the shared parent's coordinates).
    void followBody(const QRect& bodyGeometry)
    {
        const int m = margin();
        const QRect target = bodyGeometry.adjusted(-m, -m, m, m);
        if (target == geometry()) {
            return;
        }
        const bool sizeChanged = target.size() != size();
        setGeometry(target);
        if (sizeChanged) {
            m_cache = QPixmap();
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (!m_color.isValid() || m_color.alpha() <= 0) {
            return;
        }
        ensureCache();
        if (m_cache.isNull()) {
            return;
        }
        QPainter painter(this);
        painter.drawPixmap(0, 0, m_cache);
    }

private:
    void ensureCache()
    {
        const qreal dpr = devicePixelRatioF();
        const QSize deviceSize(qMax(1, qCeil(width() * dpr)), qMax(1, qCeil(height() * dpr)));
        if (!m_cache.isNull() && m_cache.size() == deviceSize
            && qFuzzyCompare(m_cache.devicePixelRatio(), dpr)) {
            return;
        }

        const int m = margin();
        const qreal bodyW = (width() - 2.0 * m) - (kFloatingOuterInset * 2.0);
        const qreal bodyH = (height() - 2.0 * m) - (kFloatingOuterInset * 2.0);
        if (bodyW <= 0.0 || bodyH <= 0.0) {
            m_cache = QPixmap();
            return;
        }

        // Silhouette of the panel body, offset like the old effect, in device pixels.
        QImage silhouette(deviceSize, QImage::Format_ARGB32_Premultiplied);
        silhouette.setDevicePixelRatio(1.0);
        silhouette.fill(Qt::transparent);
        {
            QPainter p(&silhouette);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(m_color);
            const QRectF body((m + kFloatingOuterInset) * dpr,
                (m + kFloatingOuterInset + kFloatingShadowOffsetY) * dpr, bodyW * dpr, bodyH * dpr);
            const qreal r = m_cornerRadius * dpr;
            p.drawRoundedRect(body, r, r);
        }

        if (m_blurRadius <= 0) {
            m_cache = QPixmap::fromImage(silhouette);
            m_cache.setDevicePixelRatio(dpr);
            return;
        }

        QGraphicsScene scene;
        auto* item = new QGraphicsPixmapItem(QPixmap::fromImage(silhouette));
        auto* blur = new QGraphicsBlurEffect;
        blur->setBlurRadius(m_blurRadius * dpr);
        blur->setBlurHints(QGraphicsBlurEffect::QualityHint);
        item->setGraphicsEffect(blur);
        scene.addItem(item);

        QImage blurred(deviceSize, QImage::Format_ARGB32_Premultiplied);
        blurred.setDevicePixelRatio(1.0);
        blurred.fill(Qt::transparent);
        {
            QPainter p(&blurred);
            const QRectF full(0, 0, deviceSize.width(), deviceSize.height());
            scene.render(&p, full, full);
        }

        m_cache = QPixmap::fromImage(blurred);
        m_cache.setDevicePixelRatio(dpr);
    }

    int m_blurRadius = 0;
    QColor m_color;
    qreal m_cornerRadius = kFloatingPanelCornerRadius;
    QPixmap m_cache;
};

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
    setupShadow();
    setupAnimation();
}

DockFloatingContainer::~DockFloatingContainer()
{
    // Parented to the dock container, not to this frame — delete it explicitly.
    if (m_shadowLayer) {
        delete m_shadowLayer.data();
    }

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
// Freeze
// ============================================================================

void DockFloatingContainer::setFrozen(bool frozen)
{
    if (m_frozen == frozen) {
        return;
    }

    if (frozen) {
        if (!m_panel || !isVisible() || m_animatingAppearance || m_dragging || m_resizing) {
            return;
        }
        // A panel the user is typing into must keep its live cursor and repaints.
        if (QWidget* focus = QApplication::focusWidget();
            focus && (focus == m_panel || m_panel->isAncestorOf(focus))) {
            return;
        }

        const QPixmap snapshot = grab();
        if (snapshot.isNull()) {
            return;
        }

        m_frozenSnapshot = snapshot;
        m_frozen = true;

        // Park the panel outside this frame instead of hiding it. hide() would run
        // hideEvent through the whole panel tree (timers, animations, focus, scroll
        // state) on every stroke; a child that lies fully outside the parent rect is
        // simply skipped by the paint traversal, which is all we need. The layout is
        // suspended so it does not drag the panel back or collapse the frame.
        if (QLayout* frameLayout = layout()) {
            frameLayout->setEnabled(false);
        }
        m_parkedPanelPos = m_panel->pos();
        m_panel->move(0, height() + 1);
        update();
        return;
    }

    m_frozen = false;
    m_frozenSnapshot = QPixmap();
    if (m_panel) {
        m_panel->move(m_parkedPanelPos);
    }
    if (QLayout* frameLayout = layout()) {
        frameLayout->setEnabled(true);
        frameLayout->activate();
    }
    update();
}

// ============================================================================
// Theme
// ============================================================================

void DockFloatingContainer::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    m_shadowColor = colors.shadow(80);
    m_borderColor = colors.border;
    m_panelSurfaceColor = colors.surface;

    refreshShadowAppearance();

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

bool DockFloatingContainer::event(QEvent* event)
{
    // Every raise()/stackUnder() path (drag start, click, raiseFloatingContainers)
    // ends here, so the shadow follows the frame without hooking each call site.
    if (event->type() == QEvent::ZOrderChange) {
        syncShadowStacking();
    }

    // Safety net for the freeze: while frozen the live panel is parked outside the
    // frame, so any pointer input aimed at it would land on this frame instead.
    // The dock container also unfreezes as soon as the cursor enters a floating
    // frame, this covers the gap when canvas frames stop arriving at that moment.
    if (m_frozen) {
        switch (event->type()) {
        case QEvent::Enter:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::TabletPress:
        case QEvent::TabletMove:
            setFrozen(false);
            break;
        default:
            break;
        }
    }

    return QFrame::event(event);
}

void DockFloatingContainer::moveEvent(QMoveEvent* event)
{
    QFrame::moveEvent(event);
    syncShadowGeometry();
}

void DockFloatingContainer::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);
    syncShadowGeometry();
}

void DockFloatingContainer::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    if (m_shadowLayer) {
        refreshShadowAppearance();
        syncShadowGeometry();
        m_shadowLayer->show();
        syncShadowStacking();
    }
}

void DockFloatingContainer::hideEvent(QHideEvent* event)
{
    QFrame::hideEvent(event);
    // Never stay parked behind a stale snapshot across a hide/show cycle.
    setFrozen(false);
    if (m_shadowLayer) {
        m_shadowLayer->hide();
    }
}

void DockFloatingContainer::paintEvent(QPaintEvent* event)
{
    if (m_frozen && !m_frozenSnapshot.isNull()) {
        QPainter p(this);
        p.drawPixmap(0, 0, m_frozenSnapshot);
        return;
    }

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

void DockFloatingContainer::setupShadow()
{
    if (!m_container) {
        return;
    }

    auto* layer = new FloatingShadowLayer(m_container, m_shadowRadius);
    m_shadowLayer = layer;
    layer->setVisible(isVisible());
    refreshShadowAppearance();
    syncShadowGeometry();
    syncShadowStacking();
}

qreal DockFloatingContainer::shadowCornerRadius() const
{
    if (m_panel && m_panel->baseCornerRadius() > 0) {
        return qreal(m_panel->baseCornerRadius());
    }
    return kFloatingPanelCornerRadius;
}

void DockFloatingContainer::refreshShadowAppearance()
{
    if (!m_shadowLayer) {
        return;
    }
    static_cast<FloatingShadowLayer*>(m_shadowLayer.data())
        ->setAppearance(
            m_shadowColor.isValid() ? m_shadowColor : QColor(0, 0, 0, 80), shadowCornerRadius());
}

void DockFloatingContainer::syncShadowGeometry()
{
    if (!m_shadowLayer) {
        return;
    }
    static_cast<FloatingShadowLayer*>(m_shadowLayer.data())->followBody(geometry());
}

void DockFloatingContainer::syncShadowStacking()
{
    if (m_shadowLayer && m_shadowLayer->parentWidget() == parentWidget()) {
        m_shadowLayer->stackUnder(this);
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
