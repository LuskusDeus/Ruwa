// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_PANELS_LAYERPROPERTIESPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_LAYERPROPERTIESPANEL_H

#include "features/layers/model/TextLayerEdit.h"
#include "features/layers/ui/LayerPositionEditor.h"
#include "shell/docking/widgets/DockPanel.h"

#include <QColor>
#include <QPointF>
#include <QSize>
#include <QUuid>

#include <functional>
#include <optional>
#include <utility>

namespace ruwa::core::layers {
class LayerModel;
// Mirrors the alias in LayerData.h so this header stays free of the layer model.
using LayerId = QUuid;
} // namespace ruwa::core::layers

namespace ruwa::ui::widgets {
class CollapsibleSection;
class ColorInputButton;
class LayerIdentityHeader;
class SmoothScrollArea;
class LayerTextCharacterEditor;
class LayerTextParagraphEditor;
class ToggleSwitch;
} // namespace ruwa::ui::widgets

class QLabel;
class QVBoxLayout;
class QWidget;

namespace ruwa::ui::workspace {

class LayerPropertiesPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT

public:
    explicit LayerPropertiesPanel(QWidget* parent = nullptr);
    ~LayerPropertiesPanel() override;
    void setLayerModel(ruwa::core::layers::LayerModel* model);
    /// Document size the anchor grid aligns content against. Returns nullopt on
    /// an infinite canvas, where there is no box to anchor to — the anchor cells
    /// then do nothing rather than aligning to an arbitrary rectangle.
    void setCanvasSizeProvider(std::function<std::optional<QSize>()> provider);
    /// Characters selected in the canvas's open text editing session, as
    /// [from, to). Nullopt when no session is open on the selected layer, which
    /// is what makes the Character group show the whole layer instead.
    void setTextSelectionProvider(
        std::function<std::optional<std::pair<int, int>>()> provider);

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;

public slots:
    /// Re-reads the Character and Paragraph groups off the selected layer. The
    /// canvas calls it whenever the caret or the selection moves.
    void refreshTextGroups();

    /// Re-reads the selected layer's content bounds and updates the position
    /// group. Cheap when nothing moved, but it walks the layer's tiles, so the
    /// owner should call it when an edit settles rather than every frame.
    void refreshPositionFromContent();

private slots:
    void refreshUi();
    void onBackgroundColorRequested(const QColor& initialColor);
    void onTransparentToggled(bool checked);
    void onHeaderRenameRequested(const ruwa::core::layers::LayerId& id, const QString& name);
    void onPositionEdited(const QPointF& origin);
    void onAnchorApplied(int anchorIndex, ruwa::ui::widgets::LayerPositionEditor::Axis axis);

signals:
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);
    /// The selected layer's content should be translated by @p delta document
    /// pixels. The panel does not own the pixels, so the host performs the move
    /// and the panel waits to be told the result — the same contract the
    /// identity header uses for renames.
    void moveContentRequested(const QPointF& delta);
    /// One Character / Paragraph edit; the host performs it on the canvas so
    /// the panel stays out of the document, exactly like a move.
    void textLayerEditRequested(const ruwa::core::layers::TextLayerEdit& edit);

private:
    void applyTheme();
    /// @param force writes the value into the fields even while one has focus,
    /// used right after a move so the display cannot keep a value the document
    /// refused.
    void syncPositionFromContent(bool force);
    void requestMove(const QPointF& delta);
    /// Band height of the identity header: two panel title bars tall.
    int headerBandHeight() const;
    /// Builds one collapsible group and appends it to the content column.
    ruwa::ui::widgets::CollapsibleSection* addGroup(const QString& title, QWidget* content);

private:
    ruwa::core::layers::LayerModel* m_layerModel = nullptr;
    QWidget* m_rootWidget = nullptr;
    ruwa::ui::widgets::SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    ruwa::ui::widgets::LayerIdentityHeader* m_identityHeader = nullptr;
    QLabel* m_hintLabel = nullptr;
    ruwa::ui::widgets::CollapsibleSection* m_positionGroup = nullptr;
    ruwa::ui::widgets::LayerPositionEditor* m_positionEditor = nullptr;
    ruwa::ui::widgets::CollapsibleSection* m_backgroundGroup = nullptr;
    ruwa::ui::widgets::CollapsibleSection* m_characterGroup = nullptr;
    ruwa::ui::widgets::LayerTextCharacterEditor* m_characterEditor = nullptr;
    ruwa::ui::widgets::CollapsibleSection* m_paragraphGroup = nullptr;
    ruwa::ui::widgets::LayerTextParagraphEditor* m_paragraphEditor = nullptr;
    std::function<std::optional<std::pair<int, int>>()> m_textSelectionProvider;
    /// Last origin written into the position fields, and whether there was one.
    /// Kept so a content notification that did not move the bounds leaves the
    /// fields untouched.
    QPointF m_shownContentOrigin;
    bool m_hasShownContentOrigin = false;
    /// Layer the shown origin belongs to; a switch invalidates the cache even
    /// when the new layer happens to sit at the same coordinates.
    ruwa::core::layers::LayerId m_shownContentOriginLayer;
    std::function<std::optional<QSize>()> m_canvasSizeProvider;
    ruwa::ui::widgets::ColorInputButton* m_backgroundColorInput = nullptr;
    ruwa::ui::widgets::ToggleSwitch* m_transparentSwitch = nullptr;
    bool m_syncingUi = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_PANELS_LAYERPROPERTIESPANEL_H
