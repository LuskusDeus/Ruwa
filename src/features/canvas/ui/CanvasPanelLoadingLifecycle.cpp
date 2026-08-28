// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   L O A D I N G
// ==========================================================================

#include "CanvasPanel.h"
#include "features/canvas/engine/CanvasEngineSession.h"

#include "CanvasOverlayLayoutManager.h"
#include "CanvasCursorManager.h"
#include "CanvasPanelHelpers.h"
#include "features/canvas/scene/ZoomLimits.h"
#include "features/canvas/engine/CanvasEngineTypes.h"
#include "features/brush/ui/BrushControlOverlay.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "features/canvas/ui/CanvasToolStateOverlay.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "shared/style/AnimationPolicy.h"

#include <QAbstractAnimation>
#include <QCursor>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

#include <algorithm>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::workspace {

void CanvasPanel::scheduleNewProjectAppearanceAnimation()
{
    m_deferLoadingOverlayHideUntilAppearanceAnimation = false;
    m_playNewProjectAppearanceAnimation = true;
    m_loadingAppearanceAnimationActive = true;
    m_loadingAppearanceAnimationRunning = false;
    setCursorManagerSuppressedByLoading(true);
    syncToolStateOverlayContent();
    playNewProjectAppearanceAnimationIfScheduled();
}

void CanvasPanel::setDeferredAppearanceAnimation(bool deferred)
{
    m_deferLoadingOverlayHideUntilAppearanceAnimation = deferred;
    m_loadingAppearanceAnimationRunning = false;
    if (deferred) {
        m_loadingAppearanceAnimationActive = true;
        setCursorManagerSuppressedByLoading(true);
        syncToolStateOverlayContent();
    }
}

void CanvasPanel::setLoadingOverlayDecorationsVisible(bool visible)
{
    m_loadingOverlayDecorationsVisible = visible;

    if (!m_loadingOverlay) {
        return;
    }

    if (m_loadingOverlayFadeAnimation) {
        m_loadingOverlayFadeAnimation->stop();
        m_loadingOverlayFadeAnimation->deleteLater();
        m_loadingOverlayFadeAnimation = nullptr;
    }

    if (m_loadingOverlayOpacity) {
        m_loadingOverlayOpacity->setOpacity(1.0);
    }

    if (m_loadingIndicator) {
        m_loadingIndicator->setVisible(visible);
    }
    if (m_loadingTitleLabel) {
        m_loadingTitleLabel->setVisible(visible);
    }
    if (m_loadingStatusLabel) {
        m_loadingStatusLabel->setVisible(visible);
    }

    if (visible) {
        if (m_loadingIndicator && !m_loadingIndicator->isRunning()) {
            m_loadingIndicator->start();
        }
    } else {
        if (m_loadingIndicator && m_loadingIndicator->isRunning()) {
            m_loadingIndicator->stop();
        }
    }
}

bool CanvasPanel::playNewProjectAppearanceAnimationIfScheduled()
{
    if (!m_playNewProjectAppearanceAnimation || !isRenderContentReady()) {
        return false;
    }
    m_playNewProjectAppearanceAnimation = false;

    QTimer::singleShot(50, this, [this]() {
        if (!m_engineBinding) {
            return;
        }
        auto& view = m_engineBinding->session().view();
        const QRect displayFrame = effectiveDisplayFrame();
        const int fitWidth = qMax(1, displayFrame.width());
        const int fitHeight = qMax(1, displayFrame.height());
        const QPointF displayCenter(
            static_cast<qreal>(displayFrame.center().x()) + 0.5,
            static_cast<qreal>(displayFrame.center().y()) + 0.5);
        const QSizeF vpSize = view.viewportExtent();

        if (vpSize.width() <= 100.0 || vpSize.height() <= 100.0) {
            completeLoadingAppearanceAnimation();
            fadeOutLoadingOverlay();
            return;
        }

        fadeOutLoadingOverlay();

        view.centerCameraOn(displayCenter);
        view.setZoomLimits(0.001, view.maxZoom());
        view.setZoom(0.001);
        emit zoomChanged(view.zoom());
        const float maxBrush = ruwa::ui::widgets::maxBrushRadiusForCanvasMode(
            fitWidth, fitHeight, hasFiniteDocumentBounds());
        const auto [minZoom, maxZoomComputed] = ruwa::core::canvas::computeZoomLimits(
            static_cast<int>(vpSize.width()), static_cast<int>(vpSize.height()), maxBrush);
        (void) maxZoomComputed;
        const qreal startZoom = std::clamp(
            static_cast<qreal>(minZoom) * 3.0, 0.001, view.maxZoom());
        view.centerCameraOn(displayCenter);
        m_loadingAppearanceAnimationRunning = true;
        view.setZoomSmooth(startZoom);
        emit zoomChanged(view.zoom());
        requestRender();
    });
    return true;
}

void CanvasPanel::completeLoadingAppearanceAnimation()
{
    m_loadingAppearanceAnimationRunning = false;
    m_loadingAppearanceAnimationActive = false;
    applyZoomLimits();
    setCursorManagerSuppressedByLoading(false);
    syncToolStateOverlayContent();
    updateCursorManagerOverlay();
    updateToolCursor();
}

void CanvasPanel::updateLoadingOverlayGeometry()
{
    if (m_loadingOverlay && m_contentWidget) {
        m_loadingOverlay->setGeometry(m_contentWidget->rect());
    }
}

void CanvasPanel::fadeOutLoadingOverlay()
{
    if (!m_loadingOverlay || !m_loadingOverlayOpacity)
        return;

    m_loadingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // Nothing to fade with animations off — drop the overlay outright rather
    // than run a zero-length fade whose finished() handler would have to tear
    // down the very animation that is emitting it.
    if (!anim::enabled()) {
        hideLoadingOverlayImmediately();
        return;
    }

    if (m_loadingOverlayFadeAnimation) {
        m_loadingOverlayFadeAnimation->stop();
        m_loadingOverlayFadeAnimation->deleteLater();
        m_loadingOverlayFadeAnimation = nullptr;
    }

    m_loadingOverlayFadeAnimation
        = new QPropertyAnimation(m_loadingOverlayOpacity, "opacity", this);
    m_loadingOverlayFadeAnimation->setDuration(anim::duration(400));
    m_loadingOverlayFadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_loadingOverlayFadeAnimation->setStartValue(m_loadingOverlayOpacity->opacity());
    m_loadingOverlayFadeAnimation->setEndValue(0.0);
    connect(m_loadingOverlayFadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        hideLoadingOverlayImmediately();
        if (m_loadingOverlayFadeAnimation) {
            m_loadingOverlayFadeAnimation->deleteLater();
            m_loadingOverlayFadeAnimation = nullptr;
        }
    });
    m_loadingOverlayFadeAnimation->start();
}

void CanvasPanel::hideLoadingOverlayImmediately()
{
    if (m_loadingOverlayFadeAnimation) {
        m_loadingOverlayFadeAnimation->stop();
        m_loadingOverlayFadeAnimation->deleteLater();
        m_loadingOverlayFadeAnimation = nullptr;
    }
    if (m_loadingOverlay) {
        m_loadingOverlay->hide();
        m_loadingOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    if (m_loadingIndicator && m_loadingIndicator->isRunning()) {
        m_loadingIndicator->stop();
    }
    if (m_loadingOverlayOpacity) {
        m_loadingOverlayOpacity->setOpacity(1.0);
    }
    if (m_brushOverlay && m_canvasWidgets[CanvasWidget::BrushControl]) {
        if (m_contentWidget && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
            if (m_pendingBrushOverlayPositionNormalized.has_value()) {
                setBrushOverlayPositionFromNormalized(*m_pendingBrushOverlayPositionNormalized);
            } else if (m_savedBrushOverlayPosition.has_value()
                && m_savedBrushOverlayPosition->x() >= 0 && m_savedBrushOverlayPosition->y() >= 0) {
                setBrushOverlayPosition(*m_savedBrushOverlayPosition);
            } else {
                if (m_overlayLayoutManager)
                    m_overlayLayoutManager->positionBrushOverlayDefault();
            }
        } else {
            if (m_overlayLayoutManager)
                m_overlayLayoutManager->scheduleInitialBrushOverlayPlacement();
        }
        m_brushOverlay->show();
        m_brushOverlay->raise();

        if (m_brushOverlayOpacity) {
            m_brushOverlayOpacity->setOpacity(0.0);
            auto* fadeAnim = new QPropertyAnimation(m_brushOverlayOpacity, "opacity", this);
            fadeAnim->setDuration(anim::duration(300));
            fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
            fadeAnim->setStartValue(0.0);
            fadeAnim->setEndValue(1.0);
            anim::start(fadeAnim, QAbstractAnimation::DeleteWhenStopped);
        }
    } else if (m_brushOverlay) {
        m_brushOverlay->hide();
    }
    if (m_toolStateOverlay && m_canvasWidgets[CanvasWidget::ToolState]) {
        if (m_contentWidget && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
            if (m_pendingToolStateOverlayPositionNormalized.has_value()) {
                setToolStateOverlayPositionFromNormalized(
                    *m_pendingToolStateOverlayPositionNormalized);
            } else if (m_savedToolStateOverlayPosition.has_value()
                && m_savedToolStateOverlayPosition->x() >= 0
                && m_savedToolStateOverlayPosition->y() >= 0) {
                setToolStateOverlayPosition(*m_savedToolStateOverlayPosition);
            } else if (m_overlayLayoutManager) {
                m_overlayLayoutManager->layoutToolStateOverlay();
            }
        }
        m_toolStateOverlay->show();
        if (m_toolStateOverlayOpacity) {
            m_toolStateOverlayOpacity->setOpacity(0.0);
            auto* fadeAnim = new QPropertyAnimation(m_toolStateOverlayOpacity, "opacity", this);
            fadeAnim->setDuration(anim::duration(300));
            fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
            fadeAnim->setStartValue(0.0);
            fadeAnim->setEndValue(1.0);
            anim::start(fadeAnim, QAbstractAnimation::DeleteWhenStopped);
        }
    } else if (m_toolStateOverlay) {
        m_toolStateOverlay->hide();
    }
    if (m_stylusJoystick && m_stylusJoystickOpacity && m_canvasWidgets[CanvasWidget::Joystick]) {
        if (m_contentWidget && m_contentWidget->width() > 0 && m_contentWidget->height() > 0) {
            if (m_pendingStylusJoystickPositionNormalized.has_value()) {
                setStylusJoystickPositionFromNormalized(*m_pendingStylusJoystickPositionNormalized);
            } else if (m_savedStylusJoystickPosition.has_value()
                && m_savedStylusJoystickPosition->x() >= 0
                && m_savedStylusJoystickPosition->y() >= 0) {
                setStylusJoystickPosition(*m_savedStylusJoystickPosition);
            } else if (m_overlayLayoutManager) {
                m_overlayLayoutManager->positionStylusJoystickDefault();
            }
        }
        m_stylusJoystick->setVisible(true);
        m_stylusJoystickOpacity->setOpacity(0.0);
        auto* fadeAnim = new QPropertyAnimation(m_stylusJoystickOpacity, "opacity", this);
        fadeAnim->setDuration(anim::duration(300));
        fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
        fadeAnim->setStartValue(0.0);
        fadeAnim->setEndValue(1.0);
        anim::start(fadeAnim, QAbstractAnimation::DeleteWhenStopped);
    } else if (m_stylusJoystick) {
        m_stylusJoystick->setVisible(false);
    }

    if (m_brushOverlay && m_brushOverlay->isVisible()) {
        m_brushOverlay->raise();
    }
    if (m_toolStateOverlay && m_toolStateOverlay->isVisible()) {
        m_toolStateOverlay->raise();
    }
    if (m_stylusJoystick && m_stylusJoystick->isVisible()) {
        m_stylusJoystick->raise();
    }
    if (m_engineBinding) {
        m_engineBinding->session().presentation().requestBackdropUpdate();
    }

    if (!m_loadingAppearanceAnimationActive) {
        setCursorManagerSuppressedByLoading(false);
    }
}

void CanvasPanel::setCursorManagerSuppressedByLoading(bool suppressed)
{
    m_cursorManagerSuppressedByLoading = suppressed;
    if (!m_cursorManager) {
        return;
    }

    m_cursorManager->setSuppressed(suppressed);
    if (!suppressed) {
        m_cursorManager->updateCursorPosition(QCursor::pos());
    }
}

} // namespace ruwa::ui::workspace
