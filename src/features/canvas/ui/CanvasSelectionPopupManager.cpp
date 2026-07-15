// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S E L E C T I O N   P O P U P   M A N A G E R
// ==========================================================================

#include "CanvasSelectionPopupManager.h"
#include "CanvasPanel.h"
#include "features/canvas/ui/CanvasCursorManager.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "shared/widgets/overlays/ConfirmationPopup.h"
#include "features/selection/SelectionActionPopup.h"
#include "features/color/ColorPicker.h"
#include "features/color/ColorPickerOverlay.h"

#include <QGuiApplication>
#include <QObject>
#include <QRectF>
#include <Qt>

#include <cmath>

namespace ruwa::ui::workspace {

CanvasSelectionPopupManager::CanvasSelectionPopupManager(CanvasPanel* panel)
    : m_panel(panel)
{
}

void CanvasSelectionPopupManager::ensureSelectionActionPopup()
{
    if (m_panel->m_selectionActionPopup || !m_panel->m_contentWidget) {
        return;
    }

    m_panel->m_selectionActionPopup
        = new ruwa::ui::widgets::SelectionActionPopup(m_panel->m_contentWidget);
    m_panel->m_selectionActionPopup->hide();
    m_panel->m_selectionActionPopup->setFillColor(m_panel->m_selectionFillColor);
    m_panel->m_selectionActionPopup->adjustSize();
    m_panel->m_selectionActionPopup->raise();
    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->addCursorExclusionWidget(m_panel->m_selectionActionPopup);
    }

    if (!m_panel->m_selectionColorPickerOverlay) {
        m_panel->m_selectionColorPickerOverlay
            = new ruwa::ui::widgets::ColorPickerOverlay(m_panel->m_contentWidget);
        if (m_panel->m_cursorManager && m_panel->m_selectionColorPickerOverlay->picker()) {
            m_panel->m_cursorManager->addCursorExclusionWidget(
                m_panel->m_selectionColorPickerOverlay->picker());
        }
        QObject::connect(m_panel->m_selectionColorPickerOverlay,
            &ruwa::ui::widgets::ColorPickerOverlay::colorSelected, m_panel,
            [this](const QColor& color) {
                m_panel->m_selectionFillColor = color;
                if (m_panel->m_selectionActionPopup) {
                    m_panel->m_selectionActionPopup->setFillColor(color);
                }
            });
    }

    QObject::connect(m_panel->m_selectionActionPopup,
        &ruwa::ui::widgets::SelectionActionPopup::fillColorClicked, m_panel,
        [this](QWidget* anchor) {
            if (!m_panel->m_selectionColorPickerOverlay)
                return;
            if (m_panel->m_selectionColorPickerOverlay->isActive()
                && m_panel->m_selectionColorPickerOverlay->sourceButton() == anchor) {
                m_panel->m_selectionColorPickerOverlay->hidePicker();
                return;
            }
            m_panel->m_selectionColorPickerOverlay->showPicker(
                m_panel->m_selectionFillColor, anchor);
        });

    QObject::connect(m_panel->m_selectionActionPopup,
        &ruwa::ui::widgets::SelectionActionPopup::fillRequested, m_panel, [this]() {
            if (!m_panel->m_glWidget)
                return;
            if (m_panel->m_glWidget->fillSelectionWithColor(m_panel->m_selectionFillColor)) {
                m_panel->canvasContentChanged();
            }
            updateSelectionActionPopup(true);
        });

    QObject::connect(m_panel->m_selectionActionPopup,
        &ruwa::ui::widgets::SelectionActionPopup::transformRequested, m_panel, [this]() {
            m_panel->enterTransformMode();
            updateSelectionActionPopup();
        });

    QObject::connect(m_panel->m_selectionActionPopup,
        &ruwa::ui::widgets::SelectionActionPopup::flipVerticalRequested, m_panel, [this]() {
            if (!m_panel->m_glWidget)
                return;
            if (m_panel->m_glWidget->flipSelectionVertically()) {
                updateSelectionActionPopup();
            }
        });

    QObject::connect(m_panel->m_selectionActionPopup,
        &ruwa::ui::widgets::SelectionActionPopup::flipHorizontalRequested, m_panel, [this]() {
            if (!m_panel->m_glWidget)
                return;
            if (m_panel->m_glWidget->flipSelectionHorizontally()) {
                updateSelectionActionPopup();
            }
        });

    QObject::connect(m_panel->m_selectionActionPopup,
        &ruwa::ui::widgets::SelectionActionPopup::deleteRequested, m_panel, [this]() {
            if (!m_panel->m_glWidget)
                return;
            if (m_panel->m_glWidget->clearSelectionContent()) {
                m_panel->canvasContentChanged();
            }
            updateSelectionActionPopup(true);
        });

    QObject::connect(m_panel->m_selectionActionPopup,
        &ruwa::ui::widgets::SelectionActionPopup::dismissRequested, m_panel,
        [this]() { dismissSelectionActionPopupUntilSelectionReset(); });
}

void CanvasSelectionPopupManager::ensureConfirmationPopup()
{
    if (m_panel->m_confirmationPopup || !m_panel->m_contentWidget) {
        return;
    }

    m_panel->m_confirmationPopup
        = new ruwa::ui::widgets::ConfirmationPopup(m_panel->m_contentWidget);
    m_panel->m_confirmationPopup->hide();
    m_panel->m_confirmationPopup->adjustSize();
    m_panel->m_confirmationPopup->raise();
    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->addCursorExclusionWidget(m_panel->m_confirmationPopup);
    }

    QObject::connect(m_panel->m_confirmationPopup, &ruwa::ui::widgets::ConfirmationPopup::confirmed,
        m_panel, [this]() {
            if (m_panel->m_glWidget && m_panel->m_glWidget->isTransformActive()) {
                m_panel->confirmTransform();
            } else if (m_panel->m_canvasResizeController
                && m_panel->m_canvasResizeController->isActive()) {
                m_panel->m_canvasResizeController->applySelection();
            }
        });
    QObject::connect(m_panel->m_confirmationPopup, &ruwa::ui::widgets::ConfirmationPopup::cancelled,
        m_panel, [this]() {
            if (m_panel->m_glWidget && m_panel->m_glWidget->isTransformActive()) {
                m_panel->cancelTransform();
            } else if (m_panel->m_canvasResizeController
                && m_panel->m_canvasResizeController->isActive()) {
                m_panel->m_canvasResizeController->clearOverlay();
            }
        });
}

void CanvasSelectionPopupManager::updateSelectionActionPopup(bool forceShow)
{
    updateConfirmationPopup();

    if (!m_panel->m_contentWidget || !m_panel->m_glWidget
        || !m_panel->m_glWidget->isInitialized()) {
        if (m_panel->m_selectionActionPopup) {
            m_panel->m_selectionActionPopup->hideImmediate();
        }
        return;
    }

    const bool hasSelection = m_panel->m_glWidget->hasSelectionMask();
    if (!hasSelection) {
        m_panel->m_selectionActionPopupDismissed = false;
        m_panel->m_selectionPopupWorldCenterValid = false;
        if (m_panel->m_selectionColorPickerOverlay
            && m_panel->m_selectionColorPickerOverlay->isActive()) {
            m_panel->m_selectionColorPickerOverlay->hidePicker();
        }
        if (m_panel->m_selectionActionPopup) {
            m_panel->m_selectionActionPopup->hideAnimated();
        }
        return;
    }

    if (m_panel->m_glWidget->isTransformActive()
        && !m_panel->m_glWidget->isAutoApplyingTransform()) {
        if (m_panel->m_selectionColorPickerOverlay
            && m_panel->m_selectionColorPickerOverlay->isActive()) {
            m_panel->m_selectionColorPickerOverlay->hidePicker();
        }
        if (m_panel->m_selectionActionPopup) {
            m_panel->m_selectionActionPopup->hideAnimated();
        }
        return;
    }

    const auto mods = QGuiApplication::keyboardModifiers();
    const bool hidePopupByModifier
        = mods.testFlag(Qt::ShiftModifier) || mods.testFlag(Qt::AltModifier);
    if (hidePopupByModifier) {
        if (m_panel->m_selectionColorPickerOverlay
            && m_panel->m_selectionColorPickerOverlay->isActive()) {
            m_panel->m_selectionColorPickerOverlay->hidePicker();
        }
        if (m_panel->m_selectionActionPopup) {
            m_panel->m_selectionActionPopup->hideAnimated();
        }
        return;
    }

    ensureSelectionActionPopup();
    if (!m_panel->m_selectionActionPopup) {
        return;
    }
    if (m_panel->m_selectionActionPopupDismissed && !forceShow) {
        if (m_panel->m_selectionColorPickerOverlay
            && m_panel->m_selectionColorPickerOverlay->isActive()) {
            m_panel->m_selectionColorPickerOverlay->hidePicker();
        }
        m_panel->m_selectionActionPopup->hideAnimated();
        return;
    }

    if (m_panel->m_glWidget->isAutoApplyingTransform()
        && m_panel->m_selectionActionPopup->isPopupVisible()) {
        return;
    }

    const QRectF selectionRect = m_panel->activeSelectionRectInWidget();
    if (selectionRect.isEmpty()) {
        m_panel->m_selectionActionPopup->hideAnimated();
        return;
    }
    constexpr int kVerticalOffset = 10;
    const int popupWidth = qMax(m_panel->m_selectionActionPopup->width(),
        m_panel->m_selectionActionPopup->sizeHint().width());
    const int popupHeight = qMax(m_panel->m_selectionActionPopup->height(),
        m_panel->m_selectionActionPopup->sizeHint().height());
    const int targetX = static_cast<int>(std::round(selectionRect.center().x() - popupWidth * 0.5));
    const int targetY = static_cast<int>(std::round(selectionRect.bottom() + kVerticalOffset));
    const int clampedX
        = qBound(8, targetX, qMax(8, m_panel->m_contentWidget->width() - popupWidth - 8));
    const int clampedY
        = qBound(8, targetY, qMax(8, m_panel->m_contentWidget->height() - popupHeight - 8));
    const auto& camera = m_panel->m_glWidget->viewport().camera();
    const bool cameraNavigating = m_panel->m_isPanning || m_panel->m_isZoomDragging
        || m_panel->m_isRotatingView || camera.isAnimating() || camera.isFitToViewAnimating();
    const bool animateShow
        = !m_panel->m_selectionActionPopup->isPopupVisible() && !cameraNavigating;
    const bool hasTargetDelta = qAbs(clampedX - m_panel->m_selectionActionPopup->x()) > 1
        || qAbs(clampedY - m_panel->m_selectionActionPopup->y()) > 1;
    const bool clampedByBounds = (targetX != clampedX) || (targetY != clampedY);
    const bool animateMove
        = !animateShow && !cameraNavigating && !clampedByBounds && hasTargetDelta;
    m_panel->m_selectionActionPopup->showAt(QPoint(clampedX, clampedY), animateShow, animateMove);
}

void CanvasSelectionPopupManager::updateConfirmationPopup()
{
    if (!m_panel->m_contentWidget) {
        if (m_panel->m_confirmationPopup) {
            m_panel->m_confirmationPopup->hideImmediate();
        }
        return;
    }

    QRectF activeRect;
    if (m_panel->m_glWidget && m_panel->m_glWidget->isInitialized()
        && m_panel->m_glWidget->isTransformActive()
        && !m_panel->m_glWidget->isAutoApplyingTransform()
        && !m_panel->m_glWidget->isMoveOnlyTransform()) {
        activeRect = m_panel->activeTransformRectInWidget();
    } else if (m_panel->m_canvasResizeController
        && (m_panel->m_canvasResizeController->isActive()
            || m_panel->m_canvasResizeController->isInteractionActive())) {
        activeRect = m_panel->m_canvasResizeController->activeRectInWidget();
    }

    if (activeRect.isEmpty()) {
        if (m_panel->m_confirmationPopup) {
            m_panel->m_confirmationPopup->hideAnimated();
        }
        return;
    }

    ensureConfirmationPopup();
    if (!m_panel->m_confirmationPopup) {
        return;
    }

    constexpr int kVerticalOffset = 24;
    const int popupWidth = qMax(
        m_panel->m_confirmationPopup->width(), m_panel->m_confirmationPopup->sizeHint().width());
    const int popupHeight = qMax(
        m_panel->m_confirmationPopup->height(), m_panel->m_confirmationPopup->sizeHint().height());
    const int targetX = static_cast<int>(std::round(activeRect.center().x() - popupWidth * 0.5));
    const int targetY = static_cast<int>(std::round(activeRect.bottom() + kVerticalOffset));
    const int clampedX
        = qBound(8, targetX, qMax(8, m_panel->m_contentWidget->width() - popupWidth - 8));
    const int clampedY
        = qBound(8, targetY, qMax(8, m_panel->m_contentWidget->height() - popupHeight - 8));

    const bool animateShow = !m_panel->m_confirmationPopup->isPopupVisible();
    const bool animateMove = false;

    m_panel->m_confirmationPopup->showAt(QPoint(clampedX, clampedY), animateShow, animateMove);
}

void CanvasSelectionPopupManager::dismissSelectionActionPopupUntilSelectionReset()
{
    m_panel->m_selectionActionPopupDismissed = true;
    if (m_panel->m_selectionColorPickerOverlay
        && m_panel->m_selectionColorPickerOverlay->isActive()) {
        m_panel->m_selectionColorPickerOverlay->hidePicker();
    }
    if (m_panel->m_glWidget) {
        m_panel->m_glWidget->clearSelectionMask();
    }
    updateSelectionActionPopup();
}

} // namespace ruwa::ui::workspace
