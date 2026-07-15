// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/TextEditingController.h"

#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/rendering/TextRetainedPayloadBuilder.h"
#include "features/canvas/overlays/TextEditOverlayGL.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/canvas/ui/CanvasCursorManager.h"
#include "features/canvas/ui/TextFormattingPopup.h"
#include "features/color/ColorPicker.h"
#include "features/color/ColorPickerOverlay.h"
#include "features/layers/model/LayerModel.h"
#include "commands/ShortcutManager.h"
#include "shared/undo/LayerAddCommand.h"
#include "shared/undo/TextLayerContentCommand.h"

#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QPlainTextEdit>
#include <QRectF>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <utility>

namespace ruwa::ui::workspace {
namespace {

bool isCommitShortcut(const QKeyEvent* event)
{
    return event && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && event->modifiers().testFlag(Qt::ControlModifier);
}

bool isTextLayerEditable(const ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layer->isText() || !layer->textData || !layer->visible || layer->locked) {
        return false;
    }
    for (auto* parent = layer->parent; parent; parent = parent->parent) {
        if (!parent->visible || parent->locked) {
            return false;
        }
    }
    return true;
}

bool rectsNearlyEqual(const aether::Rect& a, const aether::Rect& b)
{
    constexpr float eps = 0.001f;
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps
        && std::abs(a.width - b.width) < eps && std::abs(a.height - b.height) < eps;
}

std::array<aether::Vector2, 4> transformedRectCorners(
    const aether::TransformState& transform, const aether::Rect& rect)
{
    return { transform.transformPoint({ rect.left(), rect.top() }),
        transform.transformPoint({ rect.right(), rect.top() }),
        transform.transformPoint({ rect.right(), rect.bottom() }),
        transform.transformPoint({ rect.left(), rect.bottom() }) };
}

aether::Vector2 sampleDeformTransformForSource(
    const aether::TransformState& transform, const aether::Vector2& source)
{
    if (!transform.hasDeformMesh() || transform.contentBounds.width <= 0.001f
        || transform.contentBounds.height <= 0.001f) {
        return transform.transformPoint(source);
    }

    const float u = (source.x - transform.contentBounds.left()) / transform.contentBounds.width;
    const float v = (source.y - transform.contentBounds.top()) / transform.contentBounds.height;
    return transform.evaluateBSplineSurface(u, v);
}

aether::TransformState rebaseTextTransformToBounds(
    const aether::TransformState& current, const aether::Rect& sourceBounds)
{
    if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
        return current;
    }

    aether::TransformState rebased = current;
    if (rebased.contentBounds.width <= 0.0f || rebased.contentBounds.height <= 0.0f) {
        rebased.contentBounds = sourceBounds;
        rebased.pivot = sourceBounds.center();
        return rebased;
    }
    if (rectsNearlyEqual(rebased.contentBounds, sourceBounds)) {
        return rebased;
    }

    const aether::TransformState old = rebased;
    if (old.hasDeformMesh()) {
        auto mesh = *old.deformMesh;
        mesh.vertices.clear();
        mesh.vertices.reserve(static_cast<size_t>(mesh.rows * mesh.cols));
        for (int row = 0; row < mesh.rows; ++row) {
            const float v = mesh.rows > 1
                ? static_cast<float>(row) / static_cast<float>(mesh.rows - 1)
                : 0.5f;
            for (int col = 0; col < mesh.cols; ++col) {
                const float u = mesh.cols > 1
                    ? static_cast<float>(col) / static_cast<float>(mesh.cols - 1)
                    : 0.5f;
                const aether::Vector2 source { sourceBounds.left() + u * sourceBounds.width,
                    sourceBounds.top() + v * sourceBounds.height };
                mesh.vertices.push_back({ source, sampleDeformTransformForSource(old, source) });
            }
        }
        rebased.contentBounds = sourceBounds;
        rebased.pivot = sourceBounds.center();
        rebased.translation = { 0.0f, 0.0f };
        rebased.rotation = 0.0f;
        rebased.scale = { 1.0f, 1.0f };
        rebased.freeCorners.reset();
        rebased.deformMesh = std::move(mesh);
        return rebased;
    }

    if (old.hasFreeQuad()) {
        rebased.contentBounds = sourceBounds;
        rebased.pivot = sourceBounds.center();
        rebased.freeCorners = transformedRectCorners(old, sourceBounds);
        return rebased;
    }

    const aether::Vector2 oldPivot = rebased.pivot;
    const aether::Vector2 newPivot = sourceBounds.center();
    const float dpx = newPivot.x - oldPivot.x;
    const float dpy = newPivot.y - oldPivot.y;
    const float sdx = dpx * rebased.scale.x;
    const float sdy = dpy * rebased.scale.y;
    const float cosR = std::cos(rebased.rotation);
    const float sinR = std::sin(rebased.rotation);
    rebased.translation.x += (sdx * cosR - sdy * sinR) - dpx;
    rebased.translation.y += (sdx * sinR + sdy * cosR) - dpy;
    rebased.contentBounds = sourceBounds;
    rebased.pivot = newPivot;
    return rebased;
}

} // namespace

TextEditingController::TextEditingController(CanvasPanel* panel)
    : QObject(panel)
    , m_panel(panel)
{
    m_caretBlinkTimer = new QTimer(this);
    m_caretBlinkTimer->setInterval(500);
    connect(m_caretBlinkTimer, &QTimer::timeout, this, [this]() {
        if (!m_active) {
            return;
        }
        m_caretVisible = !m_caretVisible;
        updateOverlayState();
    });
    m_caretBlinkTimer->start();
}

TextEditingController::~TextEditingController()
{
    releaseShortcuts();
    clearOverlay();
}

bool TextEditingController::isEditorEventTarget(QObject* watched) const
{
    return watched == m_editor || (m_editor && watched == m_editor->viewport());
}

bool TextEditingController::isFormattingPopupWidget(const QWidget* widget) const
{
    if (!widget) {
        return false;
    }
    if (m_formattingPopup
        && (widget == m_formattingPopup || m_formattingPopup->isAncestorOf(widget))) {
        return true;
    }
    auto* picker = m_textColorPickerOverlay ? m_textColorPickerOverlay->picker() : nullptr;
    return picker && (widget == picker || picker->isAncestorOf(widget));
}

void TextEditingController::ensureEditor()
{
    if (m_editor || !m_panel) {
        return;
    }

    m_editor = new QPlainTextEdit(m_panel);
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_editor->setUndoRedoEnabled(true);
    m_editor->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    m_editor->setAttribute(Qt::WA_DontShowOnScreen, true);
    m_editor->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background: transparent; color: transparent; border: 0; }"));
    m_editor->setGeometry(0, 0, 1, 1);
    m_editor->hide();
    m_editor->installEventFilter(this);
    m_editor->viewport()->installEventFilter(this);
    connect(
        m_editor, &QPlainTextEdit::textChanged, this, &TextEditingController::onEditorTextChanged);
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() { showCaret(); });
}

bool TextEditingController::startExistingLayer(
    const ruwa::core::layers::LayerId& layerId, const aether::Vector2& cursorWorldPos)
{
    if (!m_panel || !m_panel->m_layerModel) {
        return false;
    }
    auto* layer = m_panel->m_layerModel->layerById(layerId);
    if (!isTextLayerEditable(layer)) {
        return false;
    }
    if (m_active && m_layerId == layerId) {
        setCursorFromWorld(cursorWorldPos);
        return true;
    }
    commit();
    m_panel->m_layerModel->setSelectedLayer(layerId);
    beginSession(layerId, false, layer->textData->text, cursorWorldPos);
    return true;
}

bool TextEditingController::startExistingLayer(const ruwa::core::layers::LayerId& layerId)
{
    if (!m_panel || !m_panel->m_layerModel) {
        return false;
    }
    auto* layer = m_panel->m_layerModel->layerById(layerId);
    if (!isTextLayerEditable(layer)) {
        return false;
    }

    const aether::TransformState transform = normalizedTextTransform(layer);
    if (!startExistingLayer(layerId, transform.transformPoint(transform.contentBounds.center()))) {
        return false;
    }

    if (m_editor) {
        QTextCursor cursor = m_editor->textCursor();
        cursor.setPosition(0);
        cursor.setPosition(m_editor->toPlainText().size(), QTextCursor::KeepAnchor);
        m_editor->setTextCursor(cursor);
        showCaret();
        updateOverlayState();
    }
    return true;
}

bool TextEditingController::startNewLayerAt(const aether::Vector2& worldPos)
{
    if (!m_panel || !m_panel->m_layerModel) {
        return false;
    }
    commit();

    auto layer
        = ruwa::core::layers::LayerData::createText(QObject::tr("Text"), QObject::tr("Text"));
    if (!layer || !layer->textData) {
        return false;
    }
    layer->textData->color = m_panel->currentBrushColor();
    layer->textData->transform.translation = { worldPos.x, worldPos.y };

    m_parentId = QUuid();
    m_insertIndex = 0;
    auto* selected = m_panel->m_layerModel->selectedLayer();
    if (selected && selected->isGroup() && selected->expanded) {
        m_parentId = selected->id;
        m_insertIndex = 0;
        m_panel->m_layerModel->addLayerTo(layer, selected, m_insertIndex);
    } else if (selected && selected->parent) {
        m_parentId = selected->parent->id;
        m_insertIndex = selected->indexInParent();
        m_panel->m_layerModel->addLayerTo(layer, selected->parent, m_insertIndex);
    } else {
        const auto& roots = m_panel->m_layerModel->rootLayers();
        m_insertIndex = 0;
        if (selected) {
            for (int i = 0; i < roots.size(); ++i) {
                if (roots[i].get() == selected) {
                    m_insertIndex = i;
                    break;
                }
            }
        }
        m_panel->m_layerModel->addLayer(layer, m_insertIndex);
    }

    const auto layerId = layer->id;
    m_panel->m_layerModel->setSelectedLayer(layerId);
    m_panel->requestRender();
    m_panel->notifyContentChanged();
    beginSession(layerId, true, layer->textData->text, worldPos);

    QTextCursor cursor = m_editor->textCursor();
    cursor.select(QTextCursor::Document);
    m_editor->setTextCursor(cursor);
    updateOverlayState();
    return true;
}

void TextEditingController::beginSession(const ruwa::core::layers::LayerId& layerId,
    bool provisional, const QString& oldText, const aether::Vector2& cursorWorldPos)
{
    ensureEditor();
    if (!m_editor || !m_panel || !m_panel->m_layerModel) {
        return;
    }

    auto* layer = m_panel->m_layerModel->layerById(layerId);
    if (!layer || !layer->textData) {
        return;
    }

    m_active = true;
    m_provisional = provisional;
    m_layerId = layerId;
    m_oldText = oldText;
    m_oldStyleRuns = layer->textData->styleRuns;
    m_oldTransform = layer->textData->transform;
    m_caretVisible = true;
    blockShortcuts();

    m_applyingEditorText = true;
    m_editor->setPlainText(layer->textData->text);
    m_editor->document()->clearUndoRedoStacks();
    m_applyingEditorText = false;

    m_editor->show();
    m_editor->raise();
    m_editor->setFocus(Qt::OtherFocusReason);
    setCursorFromWorld(cursorWorldPos);
    updateOverlayState();
}

void TextEditingController::setCursorFromWorld(const aether::Vector2& worldPos)
{
    if (!m_active || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    aether::Vector2 sourcePos;
    const auto transform = normalizedTextTransform(layer);
    if (!transform.tryInverseTransformPoint(worldPos, sourcePos)) {
        return;
    }

    QTextCursor cursor = m_editor->textCursor();
    cursor.clearSelection();
    cursor.setPosition(aether::textCursorPositionAtSourcePoint(*layer->textData, sourcePos));
    m_editor->setTextCursor(cursor);
    showCaret();
}

void TextEditingController::extendSelectionToWorld(const aether::Vector2& worldPos)
{
    if (!m_active || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    aether::Vector2 sourcePos;
    const auto transform = normalizedTextTransform(layer);
    if (!transform.tryInverseTransformPoint(worldPos, sourcePos)) {
        return;
    }

    QTextCursor cursor = m_editor->textCursor();
    cursor.setPosition(aether::textCursorPositionAtSourcePoint(*layer->textData, sourcePos),
        QTextCursor::KeepAnchor);
    m_editor->setTextCursor(cursor);
    showCaret();
}

void TextEditingController::ensureEditorHasFocus()
{
    if (!m_active || !m_editor) {
        return;
    }
    if (QApplication::focusWidget() != m_editor) {
        m_editor->setFocus(Qt::OtherFocusReason);
    }
    showCaret();
}

void TextEditingController::refreshFormattingPopup()
{
    updateFormattingPopup(false);
}

void TextEditingController::toggleSelectedEffect(ruwa::core::layers::TextStyleEffect effect)
{
    if (!m_active || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        return;
    }

    const bool targetValue = !selectedEffectEnabled(effect);
    ruwa::core::layers::applyTextEffectToRange(
        *layer->textData, cursor.selectionStart(), cursor.selectionEnd(), effect, targetValue);
    invalidateActiveTextLayer();
    showCaret();
}

void TextEditingController::applySelectedFontFamily(const QString& family)
{
    if (!m_active || family.isEmpty() || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        return;
    }

    ruwa::core::layers::applyTextFontFamilyToRange(
        *layer->textData, cursor.selectionStart(), cursor.selectionEnd(), family);
    invalidateActiveTextLayer();
    showCaret();
}

bool TextEditingController::handleRedirectedKeyPress(QKeyEvent* event)
{
    if (!m_active || !m_editor || !event || event->type() != QEvent::KeyPress) {
        return false;
    }

    ensureEditorHasFocus();
    auto finishHandled = [this]() {
        showCaret();
        return true;
    };

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool ctrlOnly = (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        == Qt::ControlModifier;
    if (ctrlOnly) {
        switch (event->key()) {
        case Qt::Key_A: {
            QTextCursor cursor = m_editor->textCursor();
            cursor.select(QTextCursor::Document);
            m_editor->setTextCursor(cursor);
            return finishHandled();
        }
        case Qt::Key_C:
            m_editor->copy();
            return finishHandled();
        case Qt::Key_X:
            m_editor->cut();
            return finishHandled();
        case Qt::Key_V:
            if (const QClipboard* clipboard = QApplication::clipboard()) {
                m_editor->insertPlainText(clipboard->text());
            }
            return finishHandled();
        case Qt::Key_B:
            toggleSelectedEffect(ruwa::core::layers::TextStyleEffect::Bold);
            return finishHandled();
        case Qt::Key_I:
            toggleSelectedEffect(ruwa::core::layers::TextStyleEffect::Italic);
            return finishHandled();
        case Qt::Key_U:
            toggleSelectedEffect(ruwa::core::layers::TextStyleEffect::Underline);
            return finishHandled();
        case Qt::Key_Z:
            if (modifiers.testFlag(Qt::ShiftModifier)) {
                m_editor->redo();
            } else {
                m_editor->undo();
            }
            return finishHandled();
        case Qt::Key_Y:
            m_editor->redo();
            return finishHandled();
        default:
            return finishHandled();
        }
    }

    if (modifiers.testFlag(Qt::AltModifier) || modifiers.testFlag(Qt::MetaModifier)) {
        return finishHandled();
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        m_editor->insertPlainText(QStringLiteral("\n"));
        return finishHandled();
    }

    if (!event->text().isEmpty()) {
        m_editor->insertPlainText(event->text());
        return finishHandled();
    }

    QTextCursor cursor = m_editor->textCursor();
    switch (event->key()) {
    case Qt::Key_Backspace:
        cursor.deletePreviousChar();
        m_editor->setTextCursor(cursor);
        return finishHandled();
    case Qt::Key_Delete:
        cursor.deleteChar();
        m_editor->setTextCursor(cursor);
        return finishHandled();
    case Qt::Key_Left:
        cursor.movePosition(QTextCursor::Left,
            modifiers.testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                  : QTextCursor::MoveAnchor);
        m_editor->setTextCursor(cursor);
        return finishHandled();
    case Qt::Key_Right:
        cursor.movePosition(QTextCursor::Right,
            modifiers.testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                  : QTextCursor::MoveAnchor);
        m_editor->setTextCursor(cursor);
        return finishHandled();
    case Qt::Key_Up:
        cursor.movePosition(QTextCursor::Up,
            modifiers.testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                  : QTextCursor::MoveAnchor);
        m_editor->setTextCursor(cursor);
        return finishHandled();
    case Qt::Key_Down:
        cursor.movePosition(QTextCursor::Down,
            modifiers.testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                  : QTextCursor::MoveAnchor);
        m_editor->setTextCursor(cursor);
        return finishHandled();
    case Qt::Key_Home:
        cursor.movePosition(QTextCursor::StartOfLine,
            modifiers.testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                  : QTextCursor::MoveAnchor);
        m_editor->setTextCursor(cursor);
        return finishHandled();
    case Qt::Key_End:
        cursor.movePosition(QTextCursor::EndOfLine,
            modifiers.testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                  : QTextCursor::MoveAnchor);
        m_editor->setTextCursor(cursor);
        return finishHandled();
    default:
        return finishHandled();
    }
}

void TextEditingController::onEditorTextChanged()
{
    if (!m_active || m_applyingEditorText || !m_editor) {
        return;
    }
    applyLiveText(m_editor->toPlainText());
}

void TextEditingController::applyLiveText(const QString& text)
{
    if (!m_panel || !m_panel->m_layerModel) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    ruwa::core::layers::replaceTextPreservingCharacterStyles(*layer->textData, text);
    layer->textData->transform = rebaseTextTransformToBounds(
        layer->textData->transform, aether::computeTextLayoutSourceBounds(*layer->textData));
    layer->runtimeRetainedPayload.reset();
    layer->runtimeRetainedPayloadKey.clear();
    m_panel->m_layerModel->notifyLayerDataChanged(m_layerId);
    m_panel->requestRender();
    m_panel->notifyContentChanged();
    updateOverlayState();
}

void TextEditingController::commit()
{
    if (!m_active || m_finishing) {
        return;
    }
    m_finishing = true;

    const auto layerId = m_layerId;
    const bool wasProvisional = m_provisional;
    const QString newText = m_editor ? m_editor->toPlainText() : QString();

    if (m_editor) {
        m_editor->clearFocus();
        m_editor->hide();
    }
    m_active = false;
    m_provisional = false;
    releaseShortcuts();
    clearOverlay(true);

    auto* layer
        = m_panel && m_panel->m_layerModel ? m_panel->m_layerModel->layerById(layerId) : nullptr;
    if (layer && layer->textData && layer->textData->text != newText) {
        layer->textData->text = newText;
    }
    if (m_panel && m_panel->m_layerModel) {
        m_panel->m_layerModel->refreshTextLayerAutoName(layerId);
    }

    const bool styleRunsChanged
        = layer && layer->textData && m_oldStyleRuns != layer->textData->styleRuns;
    if (wasProvisional) {
        pushNewLayerCommand(layer);
    } else if (m_oldText != newText || styleRunsChanged) {
        pushExistingTextCommand(newText);
    } else if (layer && layer->textData) {
        layer->textData->transform = m_oldTransform;
        layer->runtimeRetainedPayload.reset();
        layer->runtimeRetainedPayloadKey.clear();
        if (m_panel && m_panel->m_layerModel) {
            m_panel->m_layerModel->notifyLayerDataChanged(layerId);
        }
    }

    m_oldText.clear();
    m_oldStyleRuns.clear();
    m_oldTransform = {};
    m_layerId = QUuid();
    m_parentId = QUuid();
    m_insertIndex = -1;
    m_finishing = false;
}

void TextEditingController::cancel()
{
    if (!m_active || m_finishing) {
        return;
    }
    m_finishing = true;

    if (m_editor) {
        m_editor->clearFocus();
        m_editor->hide();
    }

    if (m_provisional) {
        removeProvisionalLayer();
    } else {
        restoreOldTextState();
    }

    m_active = false;
    m_provisional = false;
    releaseShortcuts();
    m_oldText.clear();
    m_oldStyleRuns.clear();
    m_oldTransform = {};
    m_layerId = QUuid();
    m_parentId = QUuid();
    m_insertIndex = -1;
    clearOverlay(true);
    m_finishing = false;
}

void TextEditingController::pushExistingTextCommand(const QString& newText)
{
    if (!m_panel || !m_panel->m_layerModel) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }
    if (auto* undo = m_panel->undoManagerOrNull()) {
        undo->push(std::make_unique<aether::TextLayerContentCommand>(
            m_panel->m_layerModel, m_layerId, m_oldText, newText, m_oldStyleRuns,
            layer->textData->styleRuns, m_oldTransform, layer->textData->transform,
            [panel = m_panel]() { panel->requestRender(); },
            [panel = m_panel]() { panel->notifyContentChanged(); }));
    }
}

void TextEditingController::pushNewLayerCommand(ruwa::core::layers::LayerData* layer)
{
    if (!m_panel || !m_panel->m_layerModel || !layer) {
        return;
    }
    if (auto* undo = m_panel->undoManagerOrNull()) {
        if (auto clone = ruwa::core::layers::LayerModel::cloneLayerTree(layer, true)) {
            undo->push(std::make_unique<aether::LayerAddCommand>(
                m_panel->m_layerModel,
                QList<std::shared_ptr<ruwa::core::layers::LayerData>> { clone },
                QList<std::pair<ruwa::core::layers::LayerId, int>> {
                    { m_parentId, m_insertIndex } },
                [panel = m_panel]() { panel->requestRender(); },
                [panel = m_panel]() { panel->notifyContentChanged(); }));
        }
    }
}

void TextEditingController::removeProvisionalLayer()
{
    if (!m_panel || !m_panel->m_layerModel || m_layerId.isNull()) {
        return;
    }
    m_panel->m_layerModel->removeLayer(m_layerId);
    m_panel->requestRender();
    m_panel->notifyContentChanged();
}

void TextEditingController::restoreOldTextState()
{
    if (!m_panel || !m_panel->m_layerModel || m_layerId.isNull()) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    layer->textData->text = m_oldText;
    layer->textData->styleRuns = m_oldStyleRuns;
    layer->textData->transform = m_oldTransform;
    layer->runtimeRetainedPayload.reset();
    layer->runtimeRetainedPayloadKey.clear();
    m_panel->m_layerModel->notifyLayerDataChanged(m_layerId);
    m_panel->requestRender();
    m_panel->notifyContentChanged();
}

bool TextEditingController::selectedEffectEnabled(ruwa::core::layers::TextStyleEffect effect) const
{
    if (!m_active || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return false;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return false;
    }

    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        return false;
    }

    return ruwa::core::layers::selectedTextEffectEnabled(
        *layer->textData, cursor.selectionStart(), cursor.selectionEnd(), effect);
}

QString TextEditingController::selectedUniformFontFamily() const
{
    if (!m_active || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return {};
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return {};
    }

    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        return {};
    }

    const int from = qBound(0, cursor.selectionStart(), layer->textData->text.size());
    const int to = qBound(0, cursor.selectionEnd(), layer->textData->text.size());
    if (from >= to) {
        return {};
    }

    QString family = ruwa::core::layers::textCharStyleAt(*layer->textData, from).fontFamily;
    for (int i = from + 1; i < to; ++i) {
        if (ruwa::core::layers::textCharStyleAt(*layer->textData, i).fontFamily != family) {
            return {};
        }
    }
    return family;
}

QColor TextEditingController::selectedTextDisplayColor() const
{
    if (!m_active || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return m_panel ? m_panel->currentBrushColor() : QColor(0, 0, 0, 255);
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return m_panel->currentBrushColor();
    }

    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        return layer->textData->color;
    }

    const int from = qBound(0, cursor.selectionStart(), layer->textData->text.size());
    const int to = qBound(0, cursor.selectionEnd(), layer->textData->text.size());
    if (from >= to) {
        return layer->textData->color;
    }
    return ruwa::core::layers::textCharStyleAt(*layer->textData, from).color;
}

void TextEditingController::applySelectedTextColor(const QColor& color)
{
    if (!m_active || !color.isValid() || !m_panel || !m_panel->m_layerModel || !m_editor) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection()) {
        return;
    }

    ruwa::core::layers::applyTextColorToRange(
        *layer->textData, cursor.selectionStart(), cursor.selectionEnd(), color);
    if (m_formattingPopup) {
        m_formattingPopup->setTextColor(color);
    }
    invalidateActiveTextLayer();
    showCaret();
}

void TextEditingController::invalidateActiveTextLayer()
{
    if (!m_panel || !m_panel->m_layerModel) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        return;
    }

    layer->runtimeRetainedPayload.reset();
    layer->runtimeRetainedPayloadKey.clear();
    m_panel->m_layerModel->notifyLayerDataChanged(m_layerId);
    m_panel->requestRender();
    m_panel->notifyContentChanged();
    updateOverlayState();
}

void TextEditingController::blockShortcuts()
{
    if (m_shortcutsBlocked) {
        return;
    }
    ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
    m_shortcutsBlocked = true;
}

void TextEditingController::releaseShortcuts()
{
    if (!m_shortcutsBlocked) {
        return;
    }
    m_shortcutsBlocked = false;
    ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
}

void TextEditingController::showCaret()
{
    if (!m_active) {
        return;
    }
    m_caretVisible = true;
    if (m_caretBlinkTimer) {
        m_caretBlinkTimer->start();
    }
    updateOverlayState();
}

void TextEditingController::clearOverlay(bool animateFormattingPopup)
{
    hideFormattingPopup(animateFormattingPopup);
    if (!m_panel || !m_panel->m_glWidget) {
        return;
    }
    aether::TextEditOverlayState state;
    m_panel->m_glWidget->setTextEditOverlayState(state);
}

void TextEditingController::ensureFormattingPopup()
{
    if (m_formattingPopup || !m_panel || !m_panel->m_contentWidget) {
        return;
    }

    m_formattingPopup = new ruwa::ui::widgets::TextFormattingPopup(m_panel->m_contentWidget);
    m_formattingPopup->hide();
    m_formattingPopup->adjustSize();
    m_formattingPopup->raise();
    connect(m_formattingPopup, &ruwa::ui::widgets::TextFormattingPopup::fontFamilyActivated, this,
        &TextEditingController::applySelectedFontFamily);
    connect(m_formattingPopup, &ruwa::ui::widgets::TextFormattingPopup::boldClicked, this,
        [this]() { toggleSelectedEffect(ruwa::core::layers::TextStyleEffect::Bold); });
    connect(m_formattingPopup, &ruwa::ui::widgets::TextFormattingPopup::italicClicked, this,
        [this]() { toggleSelectedEffect(ruwa::core::layers::TextStyleEffect::Italic); });
    connect(m_formattingPopup, &ruwa::ui::widgets::TextFormattingPopup::underlineClicked, this,
        [this]() { toggleSelectedEffect(ruwa::core::layers::TextStyleEffect::Underline); });
    connect(m_formattingPopup, &ruwa::ui::widgets::TextFormattingPopup::textColorClicked, this,
        [this](QWidget* anchor) {
            if (!m_textColorPickerOverlay || !anchor) {
                return;
            }
            if (m_textColorPickerOverlay->isActive()
                && m_textColorPickerOverlay->sourceButton() == anchor) {
                m_textColorPickerOverlay->hidePicker();
                return;
            }
            m_textColorPickerOverlay->showPicker(selectedTextDisplayColor(), anchor);
        });
    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->addCursorExclusionWidget(m_formattingPopup);
    }

    if (!m_textColorPickerOverlay) {
        m_textColorPickerOverlay
            = new ruwa::ui::widgets::ColorPickerOverlay(m_panel->m_contentWidget);
        if (m_panel->m_cursorManager && m_textColorPickerOverlay->picker()) {
            m_panel->m_cursorManager->addCursorExclusionWidget(m_textColorPickerOverlay->picker());
        }
        connect(m_textColorPickerOverlay, &ruwa::ui::widgets::ColorPickerOverlay::colorSelected,
            this, &TextEditingController::applySelectedTextColor);
    }
}

void TextEditingController::updateFormattingPopup(bool animateShow)
{
    if (!m_active || !m_panel || !m_panel->m_contentWidget || !m_panel->m_glWidget || !m_editor) {
        hideFormattingPopup(true);
        return;
    }

    const QTextCursor cursor = m_editor->textCursor();
    if (!cursor.hasSelection() || m_panel->toolMode() != CanvasPanel::ToolMode::Text) {
        hideFormattingPopup(true);
        return;
    }

    auto* layer = m_panel->m_layerModel ? m_panel->m_layerModel->layerById(m_layerId) : nullptr;
    if (!layer || !layer->textData) {
        hideFormattingPopup(true);
        return;
    }

    const QRectF textRect = activeTextRectInPanel(normalizedTextTransform(layer));
    if (textRect.isEmpty()) {
        hideFormattingPopup(true);
        return;
    }

    ensureFormattingPopup();
    if (!m_formattingPopup) {
        return;
    }
    m_formattingPopup->setEffectStates(
        selectedEffectEnabled(ruwa::core::layers::TextStyleEffect::Bold),
        selectedEffectEnabled(ruwa::core::layers::TextStyleEffect::Italic),
        selectedEffectEnabled(ruwa::core::layers::TextStyleEffect::Underline));
    m_formattingPopup->setCurrentFontFamily(selectedUniformFontFamily());
    m_formattingPopup->setTextColor(selectedTextDisplayColor());

    constexpr int kVerticalOffset = 10;
    const int popupWidth = qMax(m_formattingPopup->width(), m_formattingPopup->sizeHint().width());
    const int popupHeight
        = qMax(m_formattingPopup->height(), m_formattingPopup->sizeHint().height());
    const int targetX = static_cast<int>(std::round(textRect.center().x() - popupWidth * 0.5));
    const int targetY
        = static_cast<int>(std::round(textRect.top() - popupHeight - kVerticalOffset));
    const int clampedX
        = qBound(8, targetX, qMax(8, m_panel->m_contentWidget->width() - popupWidth - 8));
    const int clampedY
        = qBound(8, targetY, qMax(8, m_panel->m_contentWidget->height() - popupHeight - 8));

    const auto& camera = m_panel->m_glWidget->viewport().camera();
    const bool cameraNavigating = m_panel->m_isPanning || m_panel->m_isZoomDragging
        || m_panel->m_isRotatingView || camera.isAnimating() || camera.isFitToViewAnimating();
    const bool showAnimation
        = animateShow && !m_formattingPopup->isPopupVisible() && !cameraNavigating;
    const bool hasTargetDelta = qAbs(clampedX - m_formattingPopup->x()) > 1
        || qAbs(clampedY - m_formattingPopup->y()) > 1;
    const bool clampedByBounds = (targetX != clampedX) || (targetY != clampedY);
    const bool animateMove
        = !showAnimation && !cameraNavigating && !clampedByBounds && hasTargetDelta;

    m_formattingPopup->showAt(QPoint(clampedX, clampedY), showAnimation, animateMove);
}

void TextEditingController::hideFormattingPopup(bool animated)
{
    if (m_textColorPickerOverlay && m_textColorPickerOverlay->isActive()) {
        m_textColorPickerOverlay->hidePicker();
    }
    if (!m_formattingPopup) {
        return;
    }
    if (animated) {
        m_formattingPopup->hideAnimated();
    } else {
        m_formattingPopup->hideImmediate();
    }
}

QRectF TextEditingController::activeTextRectInPanel(const aether::TransformState& transform) const
{
    if (!m_panel) {
        return QRectF();
    }

    const auto corners = transformedRectCorners(transform, transform.contentBounds);
    const QPointF p1 = m_panel->mapWorldToPanel(corners[0]);
    const QPointF p2 = m_panel->mapWorldToPanel(corners[1]);
    const QPointF p3 = m_panel->mapWorldToPanel(corners[2]);
    const QPointF p4 = m_panel->mapWorldToPanel(corners[3]);

    const qreal minX = qMin(qMin(p1.x(), p2.x()), qMin(p3.x(), p4.x()));
    const qreal maxX = qMax(qMax(p1.x(), p2.x()), qMax(p3.x(), p4.x()));
    const qreal minY = qMin(qMin(p1.y(), p2.y()), qMin(p3.y(), p4.y()));
    const qreal maxY = qMax(qMax(p1.y(), p2.y()), qMax(p3.y(), p4.y()));
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).normalized();
}

aether::TransformState TextEditingController::normalizedTextTransform(
    const ruwa::core::layers::LayerData* layer) const
{
    aether::TransformState transform;
    if (!layer || !layer->textData) {
        return transform;
    }
    transform = layer->textData->transform;
    return rebaseTextTransformToBounds(
        transform, aether::computeTextLayoutSourceBounds(*layer->textData));
}

void TextEditingController::updateOverlayState()
{
    if (!m_active || !m_panel || !m_panel->m_layerModel || !m_panel->m_glWidget || !m_editor) {
        return;
    }
    auto* layer = m_panel->m_layerModel->layerById(m_layerId);
    if (!layer || !layer->textData) {
        clearOverlay();
        return;
    }

    const QTextCursor cursor = m_editor->textCursor();
    aether::TextEditOverlayState state;
    state.active = true;
    state.transform = normalizedTextTransform(layer);
    state.sourceBounds = state.transform.contentBounds;
    state.caretVisible = m_caretVisible && !cursor.hasSelection();
    state.caretSourceRect = aether::computeTextCaretSourceRect(*layer->textData, cursor.position());
    if (cursor.hasSelection()) {
        state.selectionSourceRects = aether::computeTextSelectionSourceRects(
            *layer->textData, cursor.selectionStart(), cursor.selectionEnd());
    }
    m_panel->m_glWidget->setTextEditOverlayState(state);
    updateFormattingPopup();
}

ruwa::core::layers::LayerData* TextEditingController::hitTextLayerAt(
    const aether::Vector2& worldPos) const
{
    if (!m_panel || !m_panel->m_layerModel) {
        return nullptr;
    }
    const QList<ruwa::core::layers::LayerData*> flat = m_panel->m_layerModel->flattenedLayers();
    for (auto* layer : flat) {
        if (!isTextLayerEditable(layer)) {
            continue;
        }
        const aether::TransformState transform = normalizedTextTransform(layer);
        if (transform.pointInTransformedRect(worldPos)) {
            return layer;
        }
    }
    return nullptr;
}

bool TextEditingController::eventFilter(QObject* watched, QEvent* event)
{
    if (!isEditorEventTarget(watched)) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            cancel();
            return true;
        }
        if (isCommitShortcut(keyEvent)) {
            commit();
            return true;
        }
    } else if (event->type() == QEvent::FocusOut && !m_finishing) {
        QWidget* focusWidget = QApplication::focusWidget();
        if (m_panel
            && (focusWidget == m_panel || focusWidget == m_panel->m_contentWidget
                || focusWidget == m_panel->m_glWidget || isFormattingPopupWidget(focusWidget))) {
            return QObject::eventFilter(watched, event);
        }
        commit();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace ruwa::ui::workspace
