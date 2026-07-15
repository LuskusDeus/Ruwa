// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_TEXTEDITINGCONTROLLER_H
#define RUWA_UI_WORKSPACE_TEXTEDITINGCONTROLLER_H

#include "features/layers/model/LayerData.h"
#include "shared/types/Types.h"

#include <QObject>
#include <QColor>
#include <QPointer>
#include <QRectF>
#include <QString>

class QEvent;
class QKeyEvent;
class QPlainTextEdit;
class QTimer;
class QWidget;

namespace ruwa::ui::widgets {
class ColorPickerOverlay;
class TextFormattingPopup;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

class CanvasPanel;

class TextEditingController : public QObject {
public:
    explicit TextEditingController(CanvasPanel* panel);
    ~TextEditingController() override;

    bool isEditing() const { return m_active; }
    ruwa::core::layers::LayerId activeLayerId() const { return m_layerId; }
    bool isEditorEventTarget(QObject* watched) const;

    bool startExistingLayer(
        const ruwa::core::layers::LayerId& layerId, const aether::Vector2& cursorWorldPos);
    bool startExistingLayer(const ruwa::core::layers::LayerId& layerId);
    bool startNewLayerAt(const aether::Vector2& worldPos);
    void setCursorFromWorld(const aether::Vector2& worldPos);
    void extendSelectionToWorld(const aether::Vector2& worldPos);
    void ensureEditorHasFocus();
    bool handleRedirectedKeyPress(QKeyEvent* event);
    void refreshFormattingPopup();
    void toggleSelectedEffect(ruwa::core::layers::TextStyleEffect effect);
    void applySelectedFontFamily(const QString& family);
    void commit();
    void cancel();
    void clearOverlay(bool animateFormattingPopup = false);

    ruwa::core::layers::LayerData* hitTextLayerAt(const aether::Vector2& worldPos) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensureEditor();
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
    QString selectedUniformFontFamily() const;
    QColor selectedTextDisplayColor() const;
    void applySelectedTextColor(const QColor& color);
    void invalidateActiveTextLayer();
    void blockShortcuts();
    void releaseShortcuts();
    void showCaret();
    bool isFormattingPopupWidget(const QWidget* widget) const;
    void ensureFormattingPopup();
    void updateFormattingPopup(bool animateShow = true);
    void hideFormattingPopup(bool animated);
    QRectF activeTextRectInPanel(const aether::TransformState& transform) const;
    aether::TransformState normalizedTextTransform(
        const ruwa::core::layers::LayerData* layer) const;

    CanvasPanel* m_panel = nullptr;
    QPlainTextEdit* m_editor = nullptr;
    QPointer<ruwa::ui::widgets::TextFormattingPopup> m_formattingPopup;
    QPointer<ruwa::ui::widgets::ColorPickerOverlay> m_textColorPickerOverlay;
    QTimer* m_caretBlinkTimer = nullptr;
    bool m_active = false;
    bool m_provisional = false;
    bool m_finishing = false;
    bool m_applyingEditorText = false;
    bool m_shortcutsBlocked = false;
    bool m_caretVisible = true;
    ruwa::core::layers::LayerId m_layerId;
    ruwa::core::layers::LayerId m_parentId;
    int m_insertIndex = -1;
    QString m_oldText;
    QList<ruwa::core::layers::TextStyleRun> m_oldStyleRuns;
    aether::TransformState m_oldTransform;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_TEXTEDITINGCONTROLLER_H
