// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_TABS_CUSTOMTABBAR_H
#define RUWA_UI_TABS_CUSTOMTABBAR_H

#include <QWidget>
#include <QResizeEvent>
#include <QShowEvent>
#include <QList>
#include <QHash>
#include <QIcon>
#include <QString>
#include <QUuid>
#include <QVariantAnimation>
#include "shell/context-menu/IContextMenuProvider.h"

namespace ruwa::core {
class TabManager;
class BaseTab;
} // namespace ruwa::core

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
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onTabAdded(ruwa::core::BaseTab* tab);
    void onTabReplaced(ruwa::core::BaseTab* oldTab, ruwa::core::BaseTab* newTab);
    void onTabClosing(ruwa::core::BaseTab* tab, int direction);
    void onTabRemoved(const QUuid& tabId);
    void onActiveTabChanged(ruwa::core::BaseTab* newTab, ruwa::core::BaseTab* oldTab);

private:
    struct TabItem {
        QUuid id;
        QString title;
        QIcon icon;
        QString iconAlias; ///< Resource alias (e.g. "BasicFile") for context menu sync
        QRectF rect;
        QRectF closeRect;
        bool closeHovered = false;
        bool isClosing = false; ///< True when fade-out started (prevents double-close)
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
    void bindTabDisplayTitleSignals(ruwa::core::BaseTab* tab);
    void updateLayout();
    [[nodiscard]] qreal computeStripContentWidth() const;
    [[nodiscard]] qreal stripAlignmentTarget() const;
    void refreshStripAlignment(bool animated);
    int tabIndexAt(const QPointF& pos) const;
    bool isCloseButtonAt(int index, const QPointF& pos) const;
    void drawTab(QPainter& painter, const TabItem& item, bool isActive, bool isHovered);
    void drawSeparator(QPainter& painter, qreal x, qreal y, const TabItem& anim);
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

    QHash<QUuid, qreal> m_layoutSlideStartById;
    QVariantAnimation* m_layoutSlideAnim = nullptr;

    /// Whole strip horizontal shift (left vs centered in the widget)
    qreal m_stripAlignOffset = 0.0;
    QVariantAnimation* m_stripAlignAnim = nullptr;
    bool m_initialAlignDone = false;
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_CUSTOMTABBAR_H
