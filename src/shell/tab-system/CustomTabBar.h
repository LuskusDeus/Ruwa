// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_TABS_CUSTOMTABBAR_H
#define RUWA_UI_TABS_CUSTOMTABBAR_H

#include <QWidget>
#include <QResizeEvent>
#include <QShowEvent>
#include <QList>
#include <QHash>
#include <QIcon>
#include <QPointer>
#include <QString>
#include <QUuid>
#include <QVariantAnimation>
#include "shell/context-menu/IContextMenuProvider.h"

namespace ruwa::core {
class TabManager;
class BaseTab;
} // namespace ruwa::core

namespace ruwa::ui::widgets {
class DragGhostWidget;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::tabs {

/**
 * @brief Custom tab bar with slash-separated tabs
 *
 * This is a PASSIVE VIEW - it reflects TabManager state.
 * All navigation logic goes through TabManager.
 *
 * Style:
 * - [icon] name [x] / [icon] name [x] / ...
 * - Click to switch tabs
 * - Click (x) to request close
 */
class CustomTabBar : public QWidget, public ruwa::ui::widgets::IContextMenuProvider {
    Q_OBJECT

public:
    explicit CustomTabBar(QWidget* parent = nullptr);
    ~CustomTabBar() override;

    /// Connect to TabManager
    void setTabManager(ruwa::core::TabManager* manager);

    int heightHint() const { return 36; }

    // IContextMenuProvider interface
    ruwa::ui::widgets::ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;

public slots:
    // Context menu handlers
    void onRenameRequested(const QUuid& tabId);
    void onChangeIconRequested(const QUuid& tabId);
    void onCloseTabRequested(const QUuid& tabId);
    void onCloseOtherTabsRequested(const QUuid& tabId);
    void onCloseAllTabsRequested();
    /// Show that smart object in its document's breadcrumb slot and focus it.
    void onSmartObjectTabActivated(const QUuid& tabId);
    void onCloseAllSmartObjectTabsRequested(const QUuid& parentTabId);
    void onTabRenamed(const QUuid& tabId, const QString& newName);
    void onTabIconChanged(const QUuid& tabId, const QString& iconAlias);

signals:
    /// Emitted when user clicks close button
    void closeRequested(const QUuid& tabId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTabAdded(ruwa::core::BaseTab* tab);
    void onTabReplaced(ruwa::core::BaseTab* oldTab, ruwa::core::BaseTab* newTab);
    void onTabClosing(ruwa::core::BaseTab* tab, int direction);
    void onTabRemoved(const QUuid& tabId);
    void onActiveTabChanged(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab);
    void onTabOrderChanged();

private:
    struct TabItem {
        QUuid id;
        QString title;
        QIcon icon;
        QString iconAlias; ///< Resource alias (e.g. "BasicFile") for context menu sync
        /// A smart object's contents, shown as a breadcrumb child of its document
        /// tab: no icon, no rename, preceded by ">" instead of "/".
        bool isSmartObject = false;
        QUuid parentTabId; ///< Set for smart object items only.
        QRectF rect;
        QRectF closeRect;
        bool closeHovered = false;
        bool isClosing = false; ///< True when fade-out started (prevents double-close)
        bool isDragSource = false; ///< Dimmed in-place while its shared drag ghost is visible.
        bool contentOwnsCloseConfirmation = false;
        qreal hoverProgress = 0.0;
        qreal opacity = 1.0;
        ///< Extra Y while appearing / disappearing (positive = drawn lower, “below” rest position)
        qreal verticalOffset = 0.0;
        ///< Horizontal slide after a tab is removed (visual X = rect.x() + slideOffsetX)
        qreal slideOffsetX = 0.0;
        ///< Horizontal slide while a tab fades in (drawn with slideOffsetX)
        qreal enterOffsetX = 0.0;
        qreal enterSlideDistance = 0.0;
        qreal fadeOutStartOpacity = 1.0;
        qreal fadeOutStartOffset = 0.0;
        ///< Close (×) fade — same idea as BaseAnimatedButton::hoverProgress
        qreal closeRevealProgress = 0.0;
        QVariantAnimation* hoverAnim = nullptr;
        QVariantAnimation* fadeAnim = nullptr;
        QVariantAnimation* closeRevealAnim = nullptr;
    };

    void rebuildFromManager();
    TabItem makeItem(ruwa::core::BaseTab* tab);
    void destroyItemAnimations(TabItem& item);
    void reindexItems();
    static bool isSmartObjectTab(ruwa::core::BaseTab* tab);
    /// Every open smart object of @p parentTabId, in tab order — including the
    /// ones sharing the breadcrumb slot and therefore not drawn.
    QList<QUuid> smartObjectTabsForParent(const QUuid& parentTabId) const;
    /// Which one currently owns the slot; validates (and repairs) the stored pick.
    QUuid shownSmartObjectTabForParent(const QUuid& parentTabId);
    int itemIndexOfSmartChild(const QUuid& parentTabId) const;
    /// Make the strip agree with shownSmartObjectTabForParent() for that document.
    void syncSmartSlotForParent(const QUuid& parentTabId, bool animated);
    void bindTabDisplayTitleSignals(ruwa::core::BaseTab* tab);
    void updateLayout();
    [[nodiscard]] qreal computeStripContentWidth() const;
    [[nodiscard]] qreal stripAlignmentTarget() const;
    void refreshStripAlignment(bool animated);
    int tabIndexAt(const QPointF& pos) const;
    bool isCloseButtonAt(int index, const QPointF& pos) const;
    void drawTab(QPainter& painter, const TabItem& item, bool isActive, bool isHovered);
    void drawSeparator(
        QPainter& painter, qreal x, qreal y, const TabItem& anim, const QString& glyph,
        qreal opacityFactor = 1.0);
    QUuid rootTabIdForItem(int index) const;
    QList<QUuid> visibleRootOrder() const;
    QList<QUuid> managerOrderForVisibleRoots() const;
    QRectF visualGroupBounds(const QUuid& rootTabId, bool includeVisualOffsets) const;
    int tabInsertIndexAt(const QPoint& localPos) const;
    void applyVisibleRootOrder(const QList<QUuid>& rootOrder, bool animated);
    void moveDraggedGroupTo(int insertIndex);
    void startTabDrag(const QUuid& rootTabId, const QPoint& globalPos);
    void updateTabDrag(const QPoint& globalPos);
    void finishTabDrag(bool accepted, const QPoint& globalPos);
    void cancelTabDragCandidate();
    void resetTabDragState();
    QPoint ghostTargetPosition(const QPoint& globalPos) const;
    void startHoverAnimation(int index, bool hovering);
    void startCloseRevealAnimation(int index, bool reveal);
    void startFadeInAnimation(int index);
    void startFadeOutAnimation(int index);
    void applyTabVisibilityAnimFrame(const QUuid& itemId, qreal raw);
    void runPostRemoveLayoutSlide(const QHash<QUuid, qreal>& visualLeftBeforeRemove);
    void updateScaledSizes();

private slots:
    void onThemeChanged();
    void refreshManagedTabItemTitle();

private:
    ruwa::core::TabManager* m_tabManager = nullptr;
    QList<TabItem> m_items;
    QHash<QUuid, int> m_indexById;
    QUuid m_activeId;
    int m_hoveredIndex = -1;

    /// documentTabId -> the smart object currently drawn in its breadcrumb slot.
    QHash<QUuid, QUuid> m_shownSmartByParent;
    /// smartTabId -> its document tab, kept for every open smart object (drawn or
    /// not) because the tab object is already gone by the time it is removed.
    QHash<QUuid, QUuid> m_smartParentByTab;

    QHash<QUuid, qreal> m_layoutSlideStartById;
    QVariantAnimation* m_layoutSlideAnim = nullptr;

    /// Whole strip horizontal shift (left vs centered in the widget)
    qreal m_stripAlignOffset = 0.0;
    QVariantAnimation* m_stripAlignAnim = nullptr;
    bool m_initialAlignDone = false;

    QUuid m_dragCandidateRootId;
    QUuid m_draggedRootId;
    QList<QUuid> m_dragStartRootOrder;
    QPointer<ruwa::ui::widgets::DragGhostWidget> m_dragGhost;
    QPoint m_dragPressGlobalPosition;
    QPoint m_dragOffset;
    bool m_dragActive = false;
    bool m_dragSettling = false;
    bool m_dragCursorOverride = false;
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_CUSTOMTABBAR_H
