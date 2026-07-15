// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   K E Y   E V E N T   H A N D L E R
// ==========================================================================

#include "CanvasKeyEventHandler.h"
#include "CanvasInputHost.h"
#include "CanvasPanel.h"

#include "commands/CommandExecutor.h"
#include "commands/ShortcutManager.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>

namespace ruwa::ui::workspace {

CanvasKeyEventHandler::CanvasKeyEventHandler(CanvasPanel* panel)
    : m_host(panel)
{
}

static int resolvePhysicalKey(const QKeyEvent* ke)
{
    const int key = ke->key();
    const int physical
        = ruwa::core::ShortcutManager::qtKeyFromNativeVirtualKey(ke->nativeVirtualKey());
    return (physical != 0) ? physical : key;
}

bool CanvasKeyEventHandler::handleEvent(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ruwa::core::ShortcutManager::instance().shortcutsEnabled() && !ke->isAutoRepeat()
            && (ke->modifiers() & ~Qt::ShiftModifier) == Qt::NoModifier
            && !m_host->temporaryToolHoldActive() && m_host->inputGlWidget()
            && m_host->hasInputFocusOrCursorOverCanvas() && !m_host->isDrawingActive()) {
            auto toolOpt = m_host->inputToolModeForKeyEvent(ke);
            if (toolOpt && ke->key() != Qt::Key_Space && ke->key() != Qt::Key_Alt) {
                m_host->setPendingTemporaryToolKey(resolvePhysicalKey(ke), false);
            }
        }
    } else if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Shift || ke->key() == Qt::Key_Alt) && !ke->isAutoRepeat()) {
            m_host->updateSelectionActionPopup();
        }
        if (ke->key() == Qt::Key_Control && !ke->isAutoRepeat()) {
            m_host->setCtrlModifierPressed(true);
            const bool ctrlOnly = (ke->modifiers()
                                      & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier
                                          | Qt::MetaModifier))
                == Qt::ControlModifier;
            if (ctrlOnly && ruwa::core::ShortcutManager::instance().shortcutsEnabled()
                && m_host->inputGlWidget() && m_host->hasInputFocusOrCursorOverCanvas()) {
                const bool blockTempMoveInTransform = m_host->isTransformInputActive();
                const bool blockTempMoveInSelectionInteraction
                    = m_host->isAnySelectionInteractionActive();
                const bool blockTempMoveInDrawing = m_host->isInputDrawingActive();
                if (!blockTempMoveInTransform && !blockTempMoveInSelectionInteraction
                    && !blockTempMoveInDrawing && !m_host->temporaryToolHoldActive()) {
                    auto toolOpt = m_host->inputToolModeForKey(Qt::Key_Control);
                    if (toolOpt && *toolOpt != m_host->currentInputTool()) {
                        m_host->setPendingTemporaryToolKey(Qt::Key_Control, true);
                        const QString cmdId = m_host->commandIdForInputToolMode(*toolOpt);
                        if (!cmdId.isEmpty()) {
                            ruwa::core::CommandExecutor::instance().execute(cmdId);
                        }
                        m_host->clearPendingTemporaryToolKey();
                    }
                }
            }
        }
        if (ke->key() == Qt::Key_Alt && !ke->isAutoRepeat()) {
            m_host->setAltModifierPressed(true);
        }

        const bool paletteBlocksKeys = !ruwa::core::ShortcutManager::instance().shortcutsEnabled();
        if (!paletteBlocksKeys) {
            const auto mods = ke->modifiers();
            const bool ctrlOnly = (mods
                                      & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier
                                          | Qt::MetaModifier))
                == Qt::ControlModifier;
            const int physKey = resolvePhysicalKey(ke);
            if (physKey == Qt::Key_Z && ctrlOnly && !ke->isAutoRepeat()) {
                m_host->noteUndoForTemporaryMoveTool();
            }
            if (physKey == Qt::Key_T && ctrlOnly && !ke->isAutoRepeat()
                && m_host->isCursorOverCanvas()) {
                ruwa::core::CommandExecutor::instance().execute(QStringLiteral("edit.transform"));
                return true;
            }

            const bool noMods = (mods
                                    & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier
                                        | Qt::MetaModifier))
                == Qt::NoModifier;
            const bool shiftOnly = (mods
                                       & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier
                                           | Qt::MetaModifier))
                == Qt::ShiftModifier;
            QString brushCmd;
            if (physKey == Qt::Key_BracketLeft) {
                if (noMods) {
                    brushCmd = QStringLiteral("tools.brushSizeDecrease");
                } else if (shiftOnly) {
                    brushCmd = QStringLiteral("tools.brushOpacityDecrease");
                }
            } else if (physKey == Qt::Key_BracketRight) {
                if (noMods) {
                    brushCmd = QStringLiteral("tools.brushSizeIncrease");
                } else if (shiftOnly) {
                    brushCmd = QStringLiteral("tools.brushOpacityIncrease");
                }
            }
            if (!brushCmd.isEmpty()) {
                ruwa::core::CommandExecutor::instance().execute(brushCmd);
                return true;
            }

            const bool activeSelectionInteraction = m_host->isAnySelectionInteractionActive();
            const bool spaceWithShift
                = ke->key() == Qt::Key_Space && ke->modifiers().testFlag(Qt::ShiftModifier);
            const bool shiftWhileSpaceHeld = ke->key() == Qt::Key_Shift
                && m_host->temporaryToolHoldActive()
                && m_host->temporaryToolHeldKeyIs(Qt::Key_Space)
                && !m_host->temporaryToolShiftSpaceCombo();
            if ((spaceWithShift || shiftWhileSpaceHeld) && !ke->isAutoRepeat()
                && !m_host->temporaryToolShiftSpaceCombo() && m_host->inputGlWidget()
                && m_host->hasInputFocusOrCursorOverCanvas() && !m_host->isDrawingActive()) {
                const bool blockInTransform = m_host->isTransformInputActive();
                const bool blockInSelection = m_host->isAnySelectionInteractionActive();
                if (!blockInTransform && !blockInSelection
                    && m_host->currentInputTool() != CanvasInputHost::ToolMode::RotateView) {
                    if (spaceWithShift) {
                        m_host->setPendingTemporaryToolKey(Qt::Key_Space, true);
                        ruwa::core::CommandExecutor::instance().execute(
                            QStringLiteral("tools.rotate-view"));
                        m_host->clearPendingTemporaryToolKey();
                        m_host->setTemporaryToolShiftSpaceCombo(true);
                    } else {
                        m_host->setTemporaryToolShiftSpaceCombo(true);
                        ruwa::core::CommandExecutor::instance().execute(
                            QStringLiteral("tools.rotate-view"));
                    }
                    return true;
                }
            }

            if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat() && activeSelectionInteraction) {
                m_host->beginSpaceSelectionMove();
                return true;
            }
            if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat()
                && m_host->isInputDrawingActive()) {
                m_host->beginSpaceStrokeMove();
                return true;
            }

            const bool isTempToolKey = (ke->key() == Qt::Key_Space || ke->key() == Qt::Key_Alt);
            const CanvasInputHost::ToolMode currentTool = m_host->currentInputTool();
            const bool selectionToolActive = currentTool == CanvasInputHost::ToolMode::Lasso
                || currentTool == CanvasInputHost::ToolMode::LassoFill
                || currentTool == CanvasInputHost::ToolMode::SquareSelection
                || currentTool == CanvasInputHost::ToolMode::CircleSelection
                || currentTool == CanvasInputHost::ToolMode::Move;
            const bool blockTempHandInSelectionInteraction
                = (ke->key() == Qt::Key_Space) && activeSelectionInteraction;
            const bool blockTempEyedropperInTransform
                = (ke->key() == Qt::Key_Alt) && m_host->isTransformInputActive();
            const bool blockTempEyedropperInSelection
                = (ke->key() == Qt::Key_Alt) && selectionToolActive;
            if (isTempToolKey && !ke->isAutoRepeat() && !blockTempHandInSelectionInteraction
                && !blockTempEyedropperInTransform && !blockTempEyedropperInSelection
                && !m_host->temporaryToolHoldActive() && m_host->inputGlWidget()
                && m_host->hasInputFocusOrCursorOverCanvas()) {
                auto toolOpt = m_host->inputToolModeForKey(ke->key());
                if (toolOpt && *toolOpt != currentTool) {
                    m_host->setPendingTemporaryToolKey(ke->key(), true);
                    const QString cmdId = m_host->commandIdForInputToolMode(*toolOpt);
                    if (!cmdId.isEmpty()) {
                        ruwa::core::CommandExecutor::instance().execute(cmdId);
                    }
                    m_host->clearPendingTemporaryToolKey();
                    return true;
                }
            }
        }
    } else if (event->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if ((ke->key() == Qt::Key_Shift || ke->key() == Qt::Key_Alt) && !ke->isAutoRepeat()) {
            m_host->updateSelectionActionPopup();
        }
        if (ke->key() == Qt::Key_Control && !ke->isAutoRepeat()) {
            m_host->setCtrlModifierPressed(false);
        }
        if (ke->key() == Qt::Key_Alt && !ke->isAutoRepeat()) {
            m_host->setAltModifierPressed(false);
        }
        if (ruwa::core::ShortcutManager::instance().shortcutsEnabled()) {
            if (!ke->isAutoRepeat() && ke->key() == Qt::Key_Space
                && m_host->isSpaceSelectionMoveActive()) {
                m_host->endSpaceSelectionMove();
                return true;
            }
            if (!ke->isAutoRepeat() && ke->key() == Qt::Key_Space
                && m_host->isSpaceStrokeMoveActive()) {
                m_host->endSpaceStrokeMove();
                return true;
            }
            if (!ke->isAutoRepeat() && m_host->temporaryToolHoldActive()) {
                const int physKey = resolvePhysicalKey(ke);
                if (m_host->finalizeTemporaryToolHoldForKeyRelease(physKey)) {
                    if (physKey == Qt::Key_Space || physKey == Qt::Key_Shift
                        || physKey == Qt::Key_Alt || physKey == Qt::Key_Control) {
                        return true;
                    }
                }
            }
        }
    } else if (event->type() == QEvent::ApplicationDeactivate
        || event->type() == QEvent::WindowDeactivate) {
        m_host->setCtrlModifierPressed(false);
        m_host->setAltModifierPressed(false);
        m_host->clearPendingTemporaryToolKey();
        m_host->endDrawingOnAppDeactivate();
        if (m_host->isSpaceSelectionMoveActive()) {
            m_host->endSpaceSelectionMove();
        }
        if (m_host->isSpaceStrokeMoveActive()) {
            m_host->endSpaceStrokeMove();
        }
        if (m_host->temporaryToolHoldActive()) {
            m_host->endTemporaryTool();
        }
        m_host->updateInputCursorPosition(QCursor::pos());
    } else if (event->type() == QEvent::ApplicationActivate
        || event->type() == QEvent::WindowActivate) {
        const auto mods = QApplication::keyboardModifiers();
        m_host->setCtrlModifierPressed(mods.testFlag(Qt::ControlModifier));
        m_host->setAltModifierPressed(mods.testFlag(Qt::AltModifier));
        m_host->updateInputCursorPosition(QCursor::pos());
    }
    return false;
}

} // namespace ruwa::ui::workspace
