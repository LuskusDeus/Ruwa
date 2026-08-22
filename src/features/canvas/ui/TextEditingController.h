// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_TEXTEDITINGCONTROLLER_H
#define RUWA_UI_WORKSPACE_TEXTEDITINGCONTROLLER_H

#include "features/layers/model/LayerData.h"
#include "shared/types/Types.h"

#include <QObject>
#include <QColor>
#include <QRectF>
#include <QString>

#include <utility>

class QEvent;
class QFocusEvent;
class QKeyEvent;
class QPlainTextEdit;
class QTimer;
class QWidget;

namespace ruwa::core::layers {
class LayerModel;
}

namespace ruwa::ui::workspace {

class CanvasPanel;

class TextEditingController : public QObject {
public:
    explicit TextEditingController(CanvasPanel* panel);
    ~TextEditingController() override;

    bool isEditing() const { return m_active; }
    ruwa::core::layers::LayerId activeLayerId() const { return m_layerId; }
    bool isEditorEventTarget(QObject* watched) const;

    /// Watches the document the session lives in: a selection change, a hidden
    /// or removed layer, or a layer moved out of reach all end the session, and
    /// any change to the edited layer moves the caret in the same frame.
    void attachLayerModel(ruwa::core::layers::LayerModel* model);

    bool startExistingLayer(
        const ruwa::core::layers::LayerId& layerId, const aether::Vector2& cursorWorldPos);
    bool startExistingLayer(const ruwa::core::layers::LayerId& layerId);
    bool startNewLayerAt(const aether::Vector2& worldPos);
    void setCursorFromWorld(const aether::Vector2& worldPos);
    void extendSelectionToWorld(const aether::Vector2& worldPos);
    void ensureEditorHasFocus();
    bool handleRedirectedKeyPress(QKeyEvent* event);
    /// The caret, the selection or the session itself moved: tells the panel to
    /// re-read the character attributes it is showing.
    void notifyFormattingStateChanged();
    /// Selected characters as [from, to). Equal values mean a bare caret.
    std::pair<int, int> selectionRange() const;
    void toggleSelectedEffect(ruwa::core::layers::TextStyleEffect effect);
    void applySelectedFontFamily(const QString& family);
    void commit();
    void cancel();
    void clearOverlay();

    ruwa::core::layers::LayerData* hitTextLayerAt(const aether::Vector2& worldPos) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensureEditor();
    void onLayerSelectionChanged(const ruwa::core::layers::LayerId& id);
    void onLayerDataChanged(const ruwa::core::layers::LayerId& id);
    void onLayerStructureChanged();
    void onLayerAboutToBeRemoved(const ruwa::core::layers::LayerId& id);
    bool sessionLayerIsAncestorOf(const ruwa::core::layers::LayerId& id) const;
    ruwa::core::layers::LayerModel* layerModel() const;
    /// Ends the session without writing to the model or the undo stack, for
    /// when the layer under it is already gone.
    void discardSession();
    void resetSessionState();
    void syncEditorTextFromLayer(const ruwa::core::layers::LayerData& layer);
    bool handleEditorFocusOut(QFocusEvent* event);
    /// Hides the off-screen editor and, if it still held the keyboard, hands it
    /// back to the canvas instead of leaving the window with no focus at all.
    void releaseEditorFocus();
    void beginSession(const ruwa::core::layers::LayerId& layerId, bool provisional,
        const QString& oldText, const aether::Vector2& cursorWorldPos);
    void onEditorTextChanged();
    void updateOverlayState();
    void applyLiveText(const QString& text);
    void pushExistingTextCommand(const QString& newText);
    void pushNewLayerCommand(ruwa::core::layers::LayerData* layer);
    void removeProvisionalLayer();
    void restoreOldTextState();
    bool selectedEffectEnabled(ruwa::core::layers::TextStyleEffect effect) const;
    void invalidateActiveTextLayer();
    void blockShortcuts();
    void releaseShortcuts();
    void showCaret();
    void setCaretBlinkRunning(bool running);
    /// Pushes the overlay geometry without telling the panel to re-read the
    /// formatting state; the blink tick has nothing new to tell it.
    void pushOverlayState();
    aether::TransformState normalizedTextTransform(
        const ruwa::core::layers::LayerData* layer) const;

    CanvasPanel* m_panel = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    QTimer* m_caretBlinkTimer = nullptr;
    bool m_active = false;
    bool m_provisional = false;
    bool m_finishing = false;
    bool m_applyingEditorText = false;
    bool m_shortcutsBlocked = false;
    bool m_caretVisible = true;
    /// Set while the session itself is writing to the model, so its own
    /// notifications do not come back in as an external change.
    bool m_ignoreModelSignals = false;
    ruwa::core::layers::LayerModel* m_connectedModel = nullptr;
    ruwa::core::layers::LayerId m_layerId;
    ruwa::core::layers::LayerId m_parentId;
    int m_insertIndex = -1;
    QString m_oldText;
    QList<ruwa::core::layers::TextStyleRun> m_oldStyleRuns;
    ruwa::core::layers::TextLayerTypography m_oldTypography;
    aether::TransformState m_oldTransform;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_TEXTEDITINGCONTROLLER_H
