// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   O V E R L A Y   L A Y O U T   M A N A G E R
// ==========================================================================

#include "CanvasOverlayLayoutManager.h"
#include "CanvasOverlayLayout.h"
#include "CanvasPanel.h"

#include "features/brush/ui/BrushControlOverlay.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "features/canvas/ui/CanvasToolStateOverlay.h"

#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QWidget>

namespace ruwa::ui::workspace {

namespace {

constexpr int kOverlaySnapThreshold = 16;
constexpr int kOverlayAnimationMs = 160;

} // namespace

CanvasOverlayLayoutManager::CanvasOverlayLayoutManager(CanvasPanel* panel)
    : m_panel(panel)
    , m_engine(std::make_unique<CanvasOverlayLayout>(panel ? panel->m_contentWidget : nullptr))
{
    m_engine->setMargin(kCanvasOverlayEdgeMargin);
    m_engine->setSnapThreshold(kOverlaySnapThreshold);
    m_engine->setAnimationDurationMs(kOverlayAnimationMs);
}

CanvasOverlayLayoutManager::~CanvasOverlayLayoutManager() = default;

void CanvasOverlayLayoutManager::attachOverlays()
{
    if (!m_engine || !m_panel) {
        return;
    }
    m_engine->setContentWidget(m_panel->m_contentWidget);

    using Layout = CanvasOverlayLayout;
    const Layout::Caps draggable = Layout::Draggable | Layout::Solid | Layout::SnapEdges
        | Layout::ResizeTracked | Layout::BoundsClamped;
    const auto registerCanvasWidget
        = [this](QWidget* widget, Layout::Anchor anchor, Layout::Caps caps, int priority) {
              if (!widget) {
                  return;
              }

              // A canvas widget owns its complete rectangle. Its controls may deliberately ignore
              // clicks on their background, but those ignored events must stop at the widget root
              // instead of propagating through the content container into CanvasPanel, where the
              // same press would be interpreted as the beginning of a paint stroke.
              widget->setAttribute(Qt::WA_NoMousePropagation);
              m_engine->registerItem(widget, anchor, caps, priority);
          };

    if (m_panel->m_brushOverlay) {
        registerCanvasWidget(m_panel->m_brushOverlay, Layout::Anchor::CenterLeft, draggable, 50);
        // Adopt a position restored before the widget was registered.
        if (m_panel->m_pendingBrushOverlayPositionNormalized.has_value()) {
            m_engine->setNormalizedPosition(
                m_panel->m_brushOverlay, *m_panel->m_pendingBrushOverlayPositionNormalized);
        }
    }
    if (m_panel->m_stylusJoystick) {
        // The joystick now evaluates its joystick/panel swap deterministically in
        // handleDrag (on the logical cursor target), so it can use the same trailing
        // drag lag as the other overlays.
        registerCanvasWidget(
            m_panel->m_stylusJoystick, Layout::Anchor::BottomRight, draggable, 50);
        if (m_panel->m_pendingStylusJoystickPositionNormalized.has_value()) {
            m_engine->setNormalizedPosition(
                m_panel->m_stylusJoystick, *m_panel->m_pendingStylusJoystickPositionNormalized);
        }
    }
    // Tool HUD is auto-positioned (top-center) but acts as a solid obstacle so the
    // draggable overlays cannot slide under it.
    if (m_panel->m_toolStateOverlay) {
        registerCanvasWidget(m_panel->m_toolStateOverlay, Layout::Anchor::TopCenter,
            Layout::Solid | Layout::BoundsClamped, 100);
    }
}

void CanvasOverlayLayoutManager::positionBrushOverlayDefault()
{
    if (!m_engine || !m_panel->m_brushOverlay || !m_panel->m_contentWidget) {
        return;
    }
    m_panel->m_brushOverlayUserMoved = false;
    m_engine->placeDefault(m_panel->m_brushOverlay, /*animate*/ false);
    m_panel->m_savedBrushOverlayPosition = m_panel->m_brushOverlay->pos();
}

void CanvasOverlayLayoutManager::positionStylusJoystickDefault()
{
    if (!m_engine || !m_panel->m_stylusJoystick || !m_panel->m_contentWidget) {
        return;
    }
    m_panel->m_stylusJoystickUserMoved = false;
    m_engine->placeDefault(m_panel->m_stylusJoystick, /*animate*/ false);
    m_panel->m_savedStylusJoystickPosition = m_panel->m_stylusJoystick->pos();
    m_panel->m_stylusJoystick->raise();
}

void CanvasOverlayLayoutManager::repositionStylusJoystickOnResize(
    const QSize& newSize, const QSize& oldSize)
{
    Q_UNUSED(newSize);
    Q_UNUSED(oldSize);
    if (!m_engine || !m_panel->m_stylusJoystick || !m_panel->m_contentWidget)
        return;

    if (!m_panel->m_stylusJoystickUserMoved) {
        positionStylusJoystickDefault();
        return;
    }
    m_engine->relayoutItem(m_panel->m_stylusJoystick, /*animate*/ false);
    m_panel->m_savedStylusJoystickPosition = m_panel->m_stylusJoystick->pos();
}

void CanvasOverlayLayoutManager::scheduleInitialBrushOverlayPlacement()
{
    if (!m_panel->m_brushOverlayNeedsInitialPlacement) {
        return;
    }

    QTimer::singleShot(0, m_panel, [this]() {
        if (!m_panel->m_brushOverlayNeedsInitialPlacement || !m_panel->m_brushOverlay
            || !m_panel->m_contentWidget) {
            return;
        }

        if (m_panel->m_contentWidget->width() <= 0 || m_panel->m_contentWidget->height() <= 0) {
            QTimer::singleShot(16, m_panel, [this]() { scheduleInitialBrushOverlayPlacement(); });
            return;
        }

        positionBrushOverlayDefault();
        m_panel->m_brushOverlayNeedsInitialPlacement = false;
    });
}

CanvasOverlayLayoutManager::WidgetHandles CanvasOverlayLayoutManager::handlesFor(
    CanvasWidget widget) const
{
    switch (widget) {
    case CanvasWidget::Joystick:
        return { m_panel->m_stylusJoystick, m_panel->m_stylusJoystickOpacity };
    case CanvasWidget::BrushControl:
        return { m_panel->m_brushOverlay, m_panel->m_brushOverlayOpacity };
    case CanvasWidget::ToolState:
        return { m_panel->m_toolStateOverlay, m_panel->m_toolStateOverlayOpacity };
    }
    return {};
}

void CanvasOverlayLayoutManager::setCanvasWidgetVisible(CanvasWidget widget, bool visible)
{
    if (m_panel->m_canvasWidgets[widget] == visible)
        return;
    m_panel->m_canvasWidgets[widget] = visible;

    const WidgetHandles handles = handlesFor(widget);
    if (!handles.widget)
        return;
    handles.widget->setVisible(visible);
    // Showing again after a fade-out would otherwise keep the faded opacity.
    if (visible && handles.opacity) {
        handles.opacity->setOpacity(1.0);
    }
}

void CanvasOverlayLayoutManager::layoutToolStateOverlay()
{
    // The tool HUD is not user-draggable: it always anchors top-center. As the
    // highest-priority solid it stays put while the draggable overlays avoid it.
    if (!m_engine || !m_panel->m_toolStateOverlay || !m_panel->m_contentWidget) {
        return;
    }
    m_engine->placeDefault(m_panel->m_toolStateOverlay, /*animate*/ false);
}

void CanvasOverlayLayoutManager::repositionToolStateOverlayOnResize(
    const QSize& newSize, const QSize& oldSize)
{
    Q_UNUSED(newSize);
    Q_UNUSED(oldSize);
    layoutToolStateOverlay();
}

void CanvasOverlayLayoutManager::onContentResized(const QSize& newSize, const QSize& oldSize)
{
    Q_UNUSED(oldSize);
    if (m_engine && m_panel->m_brushOverlay && m_panel->m_brushOverlay->isVisible()) {
        // The engine re-resolves the brush overlay from its tracked fraction (or
        // anchor default if never moved) — one call covers restore + user-drag.
        m_engine->relayoutItem(m_panel->m_brushOverlay, /*animate*/ false);
        m_panel->m_savedBrushOverlayPosition = m_panel->m_brushOverlay->pos();
    }
    if (m_engine && m_panel->m_stylusJoystick && m_panel->m_stylusJoystick->isVisible()) {
        if (!m_panel->m_stylusJoystickUserMoved) {
            positionStylusJoystickDefault();
        } else {
            m_engine->relayoutItem(m_panel->m_stylusJoystick, /*animate*/ false);
            m_panel->m_savedStylusJoystickPosition = m_panel->m_stylusJoystick->pos();
        }
    }
    // Tool HUD always re-anchors top-center (not user-draggable).
    layoutToolStateOverlay();
    m_panel->m_lastContentSize = newSize;
}

} // namespace ruwa::ui::workspace
