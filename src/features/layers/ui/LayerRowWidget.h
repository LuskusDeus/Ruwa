// SPDX-License-Identifier: MPL-2.0

// LayerRowWidget.h
#ifndef RUWA_UI_WIDGETS_LAYERSYSTEM_LAYERROWWIDGET_H
#define RUWA_UI_WIDGETS_LAYERSYSTEM_LAYERROWWIDGET_H

#include "features/layers/model/LayerData.h"
#include "shell/context-menu/IContextMenuProvider.h"
#include "shared/widgets/reorderlist/ReorderableRowWidget.h"

#include <QWidget>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QPropertyAnimation>
#include <QLineEdit>
#include <QPixmap>
#include <QList>
#include <QPointer>
#include <QRect>
#include <QSize>

namespace ruwa::ui::widgets {
class BaseAnimatedButton;
class DotGridLoadingIndicator;

/**
 * @brief Single layer row widget with fully custom painting.
 *
 * Layout (left to right):
 *   [pad][indent][expand_arrow][gap][thumbnail][gap][name...][flex][eye][gap][lock][pad]
 *
 * Features:
 *   - Animated hover, selection, expand arrow rotation
 *   - Inline rename on double-click
 *   - Per-zone hover tracking
 *   - Drag initiation detection
 */
class LayerRowWidget : public ReorderableRowWidget, public IContextMenuProvider {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal selectionProgress READ selectionProgress WRITE setSelectionProgress)
    Q_PROPERTY(qreal primaryProgress READ primaryProgress WRITE setPrimaryProgress)
    Q_PROPERTY(qreal expandRotation READ expandRotation WRITE setExpandRotation)
    Q_PROPERTY(qreal animatedDepth READ animatedDepth WRITE setAnimatedDepth)
    Q_PROPERTY(qreal clipOffsetProgress READ clipOffsetProgress WRITE setClipOffsetProgress)
    Q_PROPERTY(qreal rightExpandProgress READ rightExpandProgress WRITE setRightExpandProgress)
    Q_PROPERTY(qreal visibilityProgress READ visibilityProgress WRITE setVisibilityProgress)
    Q_PROPERTY(qreal effectiveVisibilityProgress READ effectiveVisibilityProgress WRITE
            setEffectiveVisibilityProgress)
    Q_PROPERTY(qreal thumbnailCtrlGlowProgress READ thumbnailCtrlGlowProgress WRITE
            setThumbnailCtrlGlowProgress)
    Q_PROPERTY(qreal thumbnailClickFlashProgress READ thumbnailClickFlashProgress WRITE
            setThumbnailClickFlashProgress)
    Q_PROPERTY(
        qreal childDisclosureProgress READ childDisclosureProgress WRITE setChildDisclosureProgress)
    Q_PROPERTY(qreal maskRevealProgress READ maskRevealProgress WRITE setMaskRevealProgress)

public:
    static constexpr int kRowHeight = 44;
    static constexpr int kGroupRowHeight = 36;
    static constexpr int kIndentPerLevel = 20;
    static constexpr int kExpandSize = 14;
    static constexpr int kThumbSize = 28;
    static constexpr int kGroupThumbSize = 24;
    static constexpr int kEyeSize = 18;
    static constexpr int kClipBadgeSize = 12;
    static constexpr int kClipIndent = 14;
    static constexpr int kGap = 5;
    static constexpr int kPad = 6;
    static constexpr int kRadius = 6;
    static constexpr int kDragThreshold = 5;
    static constexpr int kSwipeMinDistance = 48; // Swipe must be much longer than drag threshold
    static constexpr int kRightExpandMax = 96; // Max right-side offset pixels when expanded
    static constexpr int kRightExpandButtonGap = 4; // Gap between buttons
    static constexpr int kBackgroundTopInset = 12;

    explicit LayerRowWidget(QWidget* parent = nullptr);
    ~LayerRowWidget() override;

    // --- Data ---
    void setLayerData(ruwa::core::layers::LayerData* data);
    /** Set pixmap to display when row has no data (e.g. during removal animation) */
    void setRemovalSnapshot(const QPixmap& pixmap);
    const QPixmap& removalSnapshot() const { return m_removalSnapshot; }
    void setCanvasSize(const QSize& size);
    void setDisplayFrame(const QRect& frame);
    ruwa::core::layers::LayerData* layerData() const { return m_data; }
    ruwa::core::layers::LayerId layerId() const;

    // --- ReorderableRowWidget contract ---
    QUuid itemId() const override { return layerId(); }
    int itemDepth() const override { return m_data ? m_data->depth : 0; }
    bool isReorderBlocked() const override { return m_data && m_data->isBackground(); }

    // --- Height ---
    int effectiveRowHeight() const override;
    static int heightForData(const ruwa::core::layers::LayerData* data);
    static QSize thumbnailTargetSize(
        const ruwa::core::layers::LayerData* data, const QRect& displayFrame);
    static QImage buildThumbnailImage(const ruwa::core::layers::LayerData* data,
        const QRect& displayFrame, const QSize& targetSize);
    static QPixmap buildThumbnailPixmap(const ruwa::core::layers::LayerData* data,
        const QRect& displayFrame, const QSize& targetSize);

    // --- State ---
    void setSelected(bool s);
    /** Set selection without animation (for seamless transitions after drag, etc.) */
    void setSelectedImmediate(bool s);
    bool isSelected() const { return m_selected; }
    void setPrimary(bool p);
    void setPrimaryImmediate(bool p);
    bool isPrimary() const { return m_primary; }
    void setDragging(bool d) override;

    // --- Animation props ---
    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal v);
    qreal selectionProgress() const { return m_selectionProgress; }
    void setSelectionProgress(qreal v);
    qreal primaryProgress() const { return m_primaryProgress; }
    void setPrimaryProgress(qreal v);
    qreal expandRotation() const { return m_expandRotation; }
    void setExpandRotation(qreal v);
    qreal animatedDepth() const { return m_animatedDepth; }
    void setAnimatedDepth(qreal v);
    qreal clipOffsetProgress() const { return m_clipOffsetProgress; }
    void setClipOffsetProgress(qreal v);
    qreal rightExpandProgress() const { return m_rightExpandProgress; }
    void setRightExpandProgress(qreal v);
    qreal visibilityProgress() const { return m_visibilityProgress; }
    void setVisibilityProgress(qreal v);
    qreal effectiveVisibilityProgress() const { return m_effectiveVisibilityProgress; }
    void setEffectiveVisibilityProgress(qreal v);
    qreal thumbnailCtrlGlowProgress() const { return m_thumbnailCtrlGlowProgress; }
    void setThumbnailCtrlGlowProgress(qreal v);
    qreal thumbnailClickFlashProgress() const { return m_thumbnailClickFlashProgress; }
    void setThumbnailClickFlashProgress(qreal v);
    qreal childDisclosureProgress() const { return m_childDisclosureProgress; }
    void setChildDisclosureProgress(qreal v);
    qreal maskRevealProgress() const { return m_maskRevealProgress; }
    void setMaskRevealProgress(qreal v);

    /** @brief Closes the right-expand action menu if it is open. */
    void closeRightExpandMenu();
    void setThumbnailLoading(bool loading);
    bool isThumbnailLoading() const { return m_thumbnailLoading; }

    // --- Rename ---
    void startRename();
    void commitRename();
    void cancelRename();
    bool isRenaming() const { return m_renameEdit != nullptr; }

    // --- Hit test ---
    enum class HitZone {
        None,
        ExpandArrow,
        Thumbnail,
        MaskThumbnail,
        Name,
        AlphaLockBtn,
        LockBtn,
        EyeBtn,
        RightExpandBtn1,
        RightExpandBtn2,
        RightExpandBtn3,
        Background
    };
    HitZone hitTest(const QPoint& pos) const;

    /// Select this row as if left-clicked (no modifiers) before showing context menu.
    void prepareContextMenuInteraction();

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;

public slots:
    void onSimpleContextAction(int actionId);

signals:
    void clicked(const ruwa::core::layers::LayerId& id, Qt::KeyboardModifiers mods);
    void thumbnailCtrlClicked(const ruwa::core::layers::LayerId& id);
    void textEditRequested(const ruwa::core::layers::LayerId& id);
    void doubleClicked(const ruwa::core::layers::LayerId& id);
    void expandToggled(const ruwa::core::layers::LayerId& id);
    void visibilityToggled(const ruwa::core::layers::LayerId& id);
    void dragInitiated(const ruwa::core::layers::LayerId& id, const QPoint& globalPos);
    /** @brief Emitted on horizontal swipe: leftToRight=true → clip to this layer, false → unclip
     * (future). */
    void clipSwipeRequested(const ruwa::core::layers::LayerId& id, bool leftToRight);
    void renameFinished(const ruwa::core::layers::LayerId& id, const QString& newName);
    void eyePressed(const ruwa::core::layers::LayerId& id, bool wasVisible);
    void alphaLockClicked(const ruwa::core::layers::LayerId& id);
    void lockClicked(const ruwa::core::layers::LayerId& id);
    void rightExpandDuplicateClicked(const ruwa::core::layers::LayerId& id);
    void rightExpandDeleteClicked(const ruwa::core::layers::LayerId& id);
    void quickClippingMaskRequested(const ruwa::core::layers::LayerId& id);
    void clearLayerPixelsRequested(const ruwa::core::layers::LayerId& id);
    void toggleAlphaLockRequested(const ruwa::core::layers::LayerId& id);
    void toggleLayerLockRequested(const ruwa::core::layers::LayerId& id);
    void rasterizeSmartLayerRequested(const ruwa::core::layers::LayerId& id);
    void applyMaskRequested(const ruwa::core::layers::LayerId& id);
    void invertMaskRequested(const ruwa::core::layers::LayerId& id);
    void applyEffectsRequested(const ruwa::core::layers::LayerId& id);

protected:
    void paintEvent(QPaintEvent* e) override;
    void enterEvent(QEnterEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Drawing
    void drawBackground(QPainter& p, const QRectF& r);
    void drawDisplayColorBackgroundAccent(QPainter& p, const QRectF& r);
    void drawSelectionHighlight(QPainter& p, const QRectF& r);
    void drawHoverOverlay(QPainter& p, const QRectF& r);
    void drawDisplayColorBorderAccent(QPainter& p, const QRectF& r);
    void drawIndentLines(QPainter& p);
    void drawExpandArrow(QPainter& p, const QRect& r);
    void drawThumbnail(QPainter& p, const QRect& r);
    void drawMaskThumbnail(QPainter& p, const QRect& r);
    void drawName(QPainter& p, const QRect& r);
    void drawMeta(QPainter& p, const QRect& r);
    void drawAlphaLockButton(QPainter& p, const QRect& r);
    void drawLockButton(QPainter& p, const QRect& r);
    void drawClippingHandle(QPainter& p);
    void drawEyeButton(QPainter& p, const QRect& r);

    // Shared helpers for icon buttons
    void drawButtonHoverBg(QPainter& p, const QRect& r, bool hovered);
    static QPixmap tintPixmap(const QPixmap& icon, const QColor& color);
    void drawRightExpandButtons(QPainter& p);

    // Rects
    bool shouldShiftContentForSelection() const;
    int selectionContentOffset() const;
    int indentStartX() const;
    int indentWidth() const;
    int clipContentOffset() const;
    QRect eyeRect() const;
    QRect alphaLockRect() const;
    QRect lockRect() const;
    QRect expandArrowRect() const;
    QRect thumbnailRect() const;
    bool hasMaskThumbnail() const;
    /** True while the mask thumbnail slot should be drawn — either a real mask
     *  is present, or its reveal/hide animation is still in flight. */
    bool maskSlotActive() const;
    QRect maskThumbnailRect() const;
    QRect nameRect() const;
    QRect nameTextRect() const;
    QRect contentRect() const;
    QList<QRect> rightExpandButtonRects() const;
    bool hasTypeBadge() const;
    bool hasAlphaLockIcon() const;
    bool hasLockIcon() const;
    QString typeBadgeText() const;
    QFont nameDisplayFont() const;
    QColor nameDisplayColor() const;
    QRect nameDisplayRect() const;
    void syncRenameEditorPresentation();

    void animateHover(bool in);
    void animateSelection(bool sel);
    void animatePrimary(bool primary);
    void animateExpand(bool exp);
    void animateIndent(int fromDepth, int toDepth);
    void animateClipOffset(bool clipped);
    void animateRightExpand(bool expand);
    void animateVisibility(bool visible);
    void animateEffectiveVisibility(bool visible);
    void animateChildDisclosure(bool hasChildren);
    void animateMaskReveal(bool hasMask);
    void updateRightExpandButtonsGeometry();
    void updateThumbnailLoadingIndicator();
    void animateThumbnailCtrlGlow(bool in);
    void triggerThumbnailClickFlash();
    void updateThumbnailCtrlGlowState();

private:
    ruwa::core::layers::LayerData* m_data = nullptr;
    QPixmap m_removalSnapshot;
    bool m_selected = false;
    bool m_primary = false;
    bool m_dragging = false;
    HitZone m_hoveredZone = HitZone::None;
    bool m_mousePressed = false;
    bool m_clipSwipeHandled = false;
    bool m_eyePressed = false;
    bool m_alphaLockPressed = false;
    bool m_lockPressed = false;
    bool m_rightExpandPressed = false;
    QPoint m_pressPos;

    qreal m_hoverProgress = 0.0;
    qreal m_selectionProgress = 0.0;
    qreal m_primaryProgress = 0.0;
    qreal m_expandRotation = 0.0;
    qreal m_animatedDepth = 0.0;
    qreal m_clipOffsetProgress = 0.0;
    qreal m_rightExpandProgress = 0.0;
    qreal m_visibilityProgress = 1.0;
    qreal m_effectiveVisibilityProgress = 1.0;
    qreal m_thumbnailCtrlGlowProgress = 0.0;
    qreal m_thumbnailClickFlashProgress = 0.0;
    qreal m_childDisclosureProgress = 0.0;
    qreal m_maskRevealProgress = 0.0;
    int m_lastKnownDepth = -1;
    bool m_lastKnownClipped = false;
    bool m_lastKnownVisible = true;
    bool m_lastKnownEffectiveVisible = true;
    bool m_lastKnownHasChildren = false;
    bool m_lastKnownHasMask = false;

    QPropertyAnimation* m_hoverAnim = nullptr;
    QPropertyAnimation* m_selectionAnim = nullptr;
    QPropertyAnimation* m_primaryAnim = nullptr;
    QPropertyAnimation* m_expandAnim = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_effectiveOpacityAnim = nullptr;
    QPropertyAnimation* m_indentAnim = nullptr;
    QPropertyAnimation* m_clipOffsetAnim = nullptr;
    QPropertyAnimation* m_rightExpandAnim = nullptr;
    QPropertyAnimation* m_thumbnailCtrlGlowAnim = nullptr;
    QPropertyAnimation* m_thumbnailClickFlashAnim = nullptr;
    QPropertyAnimation* m_childDisclosureAnim = nullptr;
    QPropertyAnimation* m_maskRevealAnim = nullptr;

    QPointer<QLineEdit> m_renameEdit;
    QRect m_displayFrame;
    QPixmap m_maskThumbCache;

    QPointer<BaseAnimatedButton> m_rightExpandBtn1;
    QPointer<BaseAnimatedButton> m_rightExpandBtn2;
    QPointer<BaseAnimatedButton> m_rightExpandBtn3;
    QPointer<DotGridLoadingIndicator> m_thumbnailLoadingIndicator;
    bool m_thumbnailLoading = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_LAYERSYSTEM_LAYERROWWIDGET_H
