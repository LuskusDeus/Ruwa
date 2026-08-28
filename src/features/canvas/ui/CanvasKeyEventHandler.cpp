// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   K E Y   E V E N T   H A N D L E R
// ==========================================================================

#include "CanvasKeyEventHandler.h"
#include "CanvasInputHost.h"
#include "CanvasPanel.h"

#include "commands/CommandExecutor.h"
#include "commands/ShortcutManager.h"
#include "features/canvas/CanvasModifierShortcutManager.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QWidget>

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

static bool isTextInputWidget(const QWidget* widget)
{
    for (const QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<const QLineEdit*>(current) || qobject_cast<const QTextEdit*>(current)
            || qobject_cast<const QPlainTextEdit*>(current)
            || qobject_cast<const QAbstractSpinBox*>(current)) {
            return true;
        }
    }
    return false;
}

static bool isTextInputEventTarget(const QObject* watched)
{
    const auto* targetWidget = qobject_cast<const QWidget*>(watched);
    return isTextInputWidget(targetWidget) || isTextInputWidget(QApplication::focusWidget());
}

// The painting tools whose brush size can be dragged with Shift+<eyedropper
// key>+left button (see CanvasMouseInputHandler::isPaintingLikeTool).
static bool isBrushSizeDragTool(ToolId tool)
{
    return tool == ToolId::Brush || tool == ToolId::Eraser || tool == ToolId::Blur
        || tool == ToolId::Smudge || tool == ToolId::Liquify;
}

static bool isBrushAdjustmentCommand(const QString& commandId)
{
    return commandId == QLatin1String("tools.brushSizeDecrease")
        || commandId == QLatin1String("tools.brushSizeIncrease")
        || commandId == QLatin1String("tools.brushOpacityDecrease")
        || commandId == QLatin1String("tools.brushOpacityIncrease");
}

bool CanvasKeyEventHandler::handleEvent(QObject* watched, QEvent* event)
{
    using ruwa::features::canvas::CanvasModifierAction;
    auto& canvasShortcuts = ruwa::features::canvas::CanvasModifierShortcutManager::instance();
    const int moveContentKey = canvasShortcuts.keyFor(CanvasModifierAction::MoveContent);
    const int panCanvasKey = canvasShortcuts.keyFor(CanvasModifierAction::PanCanvas);
    const bool panCanvasShortcutActive
        = canvasShortcuts.actionForKey(panCanvasKey) == CanvasModifierAction::PanCanvas;

    if (event->type() == QEvent::ShortcutOverride) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ruwa::core::ShortcutManager::instance().shortcutsEnabled() && !ke->isAutoRepeat()
            && (ke->modifiers() & ~Qt::ShiftModifier) == Qt::NoModifier
            && !m_host->temporaryToolHoldActive() && m_host->inputRenderReady()
            && m_host->hasInputFocusOrCursorOverCanvas() && !m_host->isDrawingActive()) {
            auto toolOpt = m_host->inputToolModeForKeyEvent(ke);
            if (toolOpt && !canvasShortcuts.actionForKey(ke->key())) {
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
        }
        if (ke->key() == moveContentKey && !ke->isAutoRepeat()) {
            const auto requiredModifier = [&]() -> Qt::KeyboardModifier {
                switch (moveContentKey) {
                case Qt::Key_Control:
                    return Qt::ControlModifier;
                case Qt::Key_Alt:
                    return Qt::AltModifier;
                case Qt::Key_Shift:
                    return Qt::ShiftModifier;
                default:
                    return Qt::NoModifier;
                }
            }();
            const auto relevantModifiers = ke->modifiers()
                & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
            const bool moveKeyOnly = moveContentKey == Qt::Key_Space
                ? relevantModifiers == Qt::NoModifier
                : relevantModifiers == requiredModifier;
            if (moveKeyOnly && ruwa::core::ShortcutManager::instance().shortcutsEnabled()
                && m_host->inputRenderReady() && m_host->hasInputFocusOrCursorOverCanvas()) {
                const bool blockTempMoveInTransform = m_host->isTransformInputActive();
                const bool blockTempMoveInSelectionInteraction
                    = m_host->isAnySelectionInteractionActive();
                const bool blockTempMoveInDrawing = m_host->isInputDrawingActive();
                if (!blockTempMoveInTransform && !blockTempMoveInSelectionInteraction
                    && !blockTempMoveInDrawing && !m_host->temporaryToolHoldActive()) {
                    auto toolOpt = m_host->inputToolModeForKey(moveContentKey);
                    if (toolOpt && *toolOpt != m_host->currentInputTool()) {
                        m_host->setPendingTemporaryToolKey(moveContentKey, true);
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
        if (ke->key() == Qt::Key_Shift && !ke->isAutoRepeat()
            && m_host->temporaryToolHoldActive()) {
            // The other order of the same combo: the eyedropper key went down
            // first. Drop its hold so Shift+<key>+drag is the size gesture on
            // the real painting tool instead of an eyedropper drag.
            const int eyedropperKey = canvasShortcuts.keyFor(CanvasModifierAction::Eyedropper);
            if (canvasShortcuts.actionForKey(eyedropperKey) == CanvasModifierAction::Eyedropper
                && m_host->temporaryToolHeldKeyIs(eyedropperKey)
                && !m_host->isInputDrawingActive()) {
                m_host->finalizeTemporaryToolHoldForKeyRelease(eyedropperKey);
            }
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
                const QString commandId
                    = ruwa::core::ShortcutManager::instance().commandForKeyEvent(ke);
                if (commandId == QLatin1String("edit.transform")) {
                    ruwa::core::CommandExecutor::instance().execute(commandId);
                    return true;
                }
            }

            // QShortcut uses the key produced by the active layout.  Keep brush adjustments
            // bound to their physical keys as a fallback, but never steal input from an editor.
            // Resolve through ShortcutManager so custom bindings and cleared shortcuts are honored.
            if (physKey != ke->key() && !isTextInputEventTarget(watched)) {
                const QString commandId
                    = ruwa::core::ShortcutManager::instance().commandForKeyEvent(ke);
                if (isBrushAdjustmentCommand(commandId)) {
                    ruwa::core::CommandExecutor::instance().execute(commandId);
                    return true;
                }
            }

            const bool activeSelectionInteraction = m_host->isAnySelectionInteractionActive();
            const bool panWithShift = panCanvasShortcutActive && panCanvasKey != Qt::Key_Shift
                && ke->key() == panCanvasKey && ke->modifiers().testFlag(Qt::ShiftModifier);
            const bool shiftWhilePanHeld = panCanvasShortcutActive && panCanvasKey != Qt::Key_Shift
                && ke->key() == Qt::Key_Shift && m_host->temporaryToolHoldActive()
                && m_host->temporaryToolHeldKeyIs(panCanvasKey)
                && !m_host->temporaryToolShiftSpaceCombo();
            if ((panWithShift || shiftWhilePanHeld) && !ke->isAutoRepeat()
                && !m_host->temporaryToolShiftSpaceCombo() && m_host->inputRenderReady()
                && m_host->hasInputFocusOrCursorOverCanvas() && !m_host->isDrawingActive()) {
                const bool blockInTransform = m_host->isTransformInputActive();
                const bool blockInSelection = m_host->isAnySelectionInteractionActive();
                if (!blockInTransform && !blockInSelection
                    && m_host->currentInputTool() != ToolId::RotateView) {
                    if (panWithShift) {
                        m_host->setPendingTemporaryToolKey(panCanvasKey, true);
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

            if (panCanvasShortcutActive && ke->key() == panCanvasKey && !ke->isAutoRepeat()
                && activeSelectionInteraction) {
                m_host->beginSpaceSelectionMove();
                return true;
            }
            if (panCanvasShortcutActive && ke->key() == panCanvasKey && !ke->isAutoRepeat()
                && m_host->isInputDrawingActive()) {
                m_host->beginSpaceStrokeMove();
                return true;
            }

            const auto modifierAction = canvasShortcuts.actionForKey(ke->key());
            // MoveContent has stricter modifier/context checks and is handled above.
            const bool isTempToolKey
                = modifierAction && modifierAction != CanvasModifierAction::MoveContent;
            const ToolId currentTool = m_host->currentInputTool();
            // Lasso Fill deliberately remains available for the temporary Eyedropper:
            // unlike selection tools, it does not use Alt as a subtract modifier.
            const bool blocksTemporaryEyedropper = currentTool == ToolId::Lasso
                || currentTool == ToolId::SquareSelection || currentTool == ToolId::CircleSelection
                || currentTool == ToolId::MagicWand || currentTool == ToolId::Move;
            const bool blockTempHandInSelectionInteraction
                = modifierAction == CanvasModifierAction::PanCanvas && activeSelectionInteraction;
            const bool blockTempEyedropperInTransform
                = modifierAction == CanvasModifierAction::Eyedropper
                && m_host->isTransformInputActive();
            const bool blockTempEyedropperForTool
                = modifierAction == CanvasModifierAction::Eyedropper && blocksTemporaryEyedropper;
            // Shift+<eyedropper key> is the brush-size drag on the painting
            // tools, so the temporary eyedropper must not steal the tool out
            // from under it.
            const bool blockTempEyedropperForSizeDrag
                = modifierAction == CanvasModifierAction::Eyedropper
                && ke->modifiers().testFlag(Qt::ShiftModifier) && isBrushSizeDragTool(currentTool);
            if (isTempToolKey && !ke->isAutoRepeat() && !blockTempHandInSelectionInteraction
                && !blockTempEyedropperInTransform && !blockTempEyedropperForTool
                && !blockTempEyedropperForSizeDrag && !m_host->temporaryToolHoldActive()
                && m_host->inputRenderReady() && m_host->hasInputFocusOrCursorOverCanvas()) {
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
            if (!ke->isAutoRepeat() && ke->key() == panCanvasKey
                && m_host->isSpaceSelectionMoveActive()) {
                m_host->endSpaceSelectionMove();
                return true;
            }
            if (!ke->isAutoRepeat() && ke->key() == panCanvasKey
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
