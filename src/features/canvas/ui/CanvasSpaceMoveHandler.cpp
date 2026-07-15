// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S P A C E   M O V E   H A N D L E R
// ==========================================================================

#include "CanvasSpaceMoveHandler.h"
#include "CanvasPanel.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/ui/CanvasCursorManager.h"

#include <QCursor>
#include <Qt>

namespace ruwa::ui::workspace {

CanvasSpaceMoveHandler::CanvasSpaceMoveHandler(CanvasPanel* panel)
    : m_panel(panel)
{
}

void CanvasSpaceMoveHandler::beginSpaceSelectionMove()
{
    if (!m_panel->isAnySelectionInteractionActive())
        return;
    m_panel->m_spaceSelectionMoveActive = true;
    m_panel->m_spaceSelectionMoveLastGlobalPos = m_panel->m_cursorManager
        ? m_panel->m_cursorManager->activeCursorPosition()
        : QCursor::pos();
    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->setRequestedCursor(Qt::SizeAllCursor);
        m_panel->m_cursorManager->updateCursorPosition(m_panel->m_spaceSelectionMoveLastGlobalPos);
    }
}

void CanvasSpaceMoveHandler::moveActiveSelectionWithSpace(const QPoint& globalPos)
{
    if (!m_panel->m_spaceSelectionMoveActive || !m_panel->m_glWidget)
        return;
    if (globalPos == m_panel->m_spaceSelectionMoveLastGlobalPos)
        return;

    const aether::Vector2 prevWorld
        = m_panel->mapToWorld(m_panel->m_spaceSelectionMoveLastGlobalPos);
    const aether::Vector2 currWorld = m_panel->mapToWorld(globalPos);
    const qreal dx = static_cast<qreal>(currWorld.x - prevWorld.x);
    const qreal dy = static_cast<qreal>(currWorld.y - prevWorld.y);

    if (m_panel->m_isLassoSelecting || m_panel->m_isRectSelecting || m_panel->m_isCircleSelecting) {
        m_panel->m_glWidget->translateActiveSelection(
            static_cast<float>(dx), static_cast<float>(dy));
    }

    if (m_panel->m_canvasResizeController
        && m_panel->m_canvasResizeController->isSelectingOrMoving()) {
        m_panel->m_canvasResizeController->translateSelection(dx, dy);
    }

    m_panel->m_spaceSelectionMoveLastGlobalPos = globalPos;
}

void CanvasSpaceMoveHandler::endSpaceSelectionMove()
{
    m_panel->m_spaceSelectionMoveActive = false;
    m_panel->updateToolCursor();
    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->refreshCursorPosition();
    }
}

void CanvasSpaceMoveHandler::beginSpaceStrokeMove()
{
    if (!m_panel->m_isDrawing || !m_panel->m_glWidget)
        return;
    m_panel->m_spaceStrokeMoveActive = true;
    m_panel->m_spaceStrokeMoveLastGlobalPos = m_panel->m_cursorManager
        ? m_panel->m_cursorManager->activeCursorPosition()
        : QCursor::pos();
    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->setRequestedCursor(Qt::SizeAllCursor);
        m_panel->m_cursorManager->updateCursorPosition(m_panel->m_spaceStrokeMoveLastGlobalPos);
    }
}

void CanvasSpaceMoveHandler::moveActiveStrokeWithSpace(const QPoint& globalPos)
{
    if (!m_panel->m_spaceStrokeMoveActive || !m_panel->m_glWidget || !m_panel->m_isDrawing)
        return;
    if (globalPos == m_panel->m_spaceStrokeMoveLastGlobalPos)
        return;

    const aether::Vector2 prevWorld = m_panel->mapToWorld(m_panel->m_spaceStrokeMoveLastGlobalPos);
    const aether::Vector2 currWorld = m_panel->mapToWorld(globalPos);
    const float dx = currWorld.x - prevWorld.x;
    const float dy = currWorld.y - prevWorld.y;
    m_panel->m_glWidget->translateActiveStroke(dx, dy);

    m_panel->m_spaceStrokeMoveLastGlobalPos = globalPos;
}

void CanvasSpaceMoveHandler::endSpaceStrokeMove()
{
    m_panel->m_spaceStrokeMoveActive = false;
    m_panel->updateToolCursor();
    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->refreshCursorPosition();
    }
}

} // namespace ruwa::ui::workspace
