// SPDX-License-Identifier: MPL-2.0

// MenuPopup.h
#ifndef RUWA_UI_WIDGETS_TOPBAR_MENUPOPUP_H
#define RUWA_UI_WIDGETS_TOPBAR_MENUPOPUP_H

#include <QWidget>
#include <QList>
#include <QPointer>
#include <QPropertyAnimation>
#include <QRect>
#include <QPoint>
#include <functional>

class QVBoxLayout;
class QGraphicsOpacityEffect;
class QTimer;

namespace ruwa::ui::widgets {

/**
 * @brief Menu item data structure
 */
struct MenuItem {
    QString text;
    QString shortcut;
    QIcon icon;
    bool enabled = true;
    bool separator = false;
    std::function<void()> action;
    QList<MenuItem> submenu; // Non-empty = submenu item with arrow

    bool isToggle = false;
    bool checked = false; // Initial/current state for toggles
    std::function<void(bool)> toggleAction; // Called with new state when toggled

    static MenuItem Separator()
    {
        MenuItem item;
        item.separator = true;
        return item;
    }

    bool hasSubmenu() const { return !submenu.isEmpty(); }
};

/**
 * @brief Single menu item widget
 */
class MenuItemWidget : public QWidget {
    Q_OBJECT

public:
    explicit MenuItemWidget(const MenuItem& item, QWidget* parent = nullptr);

    void setHovered(bool hovered);
    bool isHovered() const { return m_isHovered; }
    bool hasSubmenu() const { return m_item.hasSubmenu(); }
    const QList<MenuItem>& submenuItems() const { return m_item.submenu; }

signals:
    void clicked();
    void hovered();
    void submenuHovered(const QList<MenuItem>& submenuItems);

protected:
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    MenuItem m_item;
    bool m_isHovered = false;
    bool m_isPressed = false;
    bool m_checked = false; // For toggle items
};

/**
 * @brief Popup menu widget that appears below TopBar buttons
 */
class MenuPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)
    Q_PROPERTY(int displayHeight READ displayHeight WRITE setDisplayHeight)

public:
    explicit MenuPopup(QWidget* parent = nullptr);
    ~MenuPopup() override;

    void setItems(const QList<MenuItem>& items);

    /// Show submenu to the right of the given widget (anchor)
    void showSubmenu(QWidget* anchor, const QList<MenuItem>& items);
    /// @param immediate when true, hide without animation (e.g. when switching to another submenu)
    void hideSubmenu(bool immediate = false);

    /// Show popup at given position (used for submenus)
    /// @param slideFromLeft when true, animate sliding in from the left (parent menu side)
    void showAt(const QPoint& pos, bool slideFromLeft = true);

    /// Show popup below the given widget (anchor)
    /// @param slideFromTop if true, animate sliding from above (first appearance only)
    void showBelow(QWidget* anchor, bool slideFromTop = false);

    /// Switch to different anchor/items with smooth position animation (like ColorPicker).
    /// Call when popup is already visible and user hovers another menu button.
    void switchTo(QWidget* anchor, const QList<MenuItem>& items);

    /// Hide popup with animation
    void hidePopup();

    /// Force hide immediately without animation (for switching menus)
    void forceHide();

    bool isPopupVisible() const { return m_isVisible; }
    bool isHiding() const { return m_isHiding; }
    bool isSubmenuVisible() const { return m_submenuPopup && m_submenuPopup->isPopupVisible(); }
    MenuPopup* submenuPopup() const { return m_submenuPopup; }

    qreal popupOpacity() const { return m_opacity; }
    void setPopupOpacity(qreal opacity);

    int displayHeight() const { return m_displayHeight; }
    void setDisplayHeight(int h);

signals:
    void aboutToHide();
    void hidden();
    void shown();
    void contentChanged();
    void mouseLeft();
    void itemClicked(int index, bool isToggle); // isToggle: menu stays open

protected:
    void paintEvent(QPaintEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildItems();
    void ensureSubmenuPopup();
    QPoint calculateSubmenuPosition(QWidget* anchor) const;
    void startShowAnimation();
    void startHideAnimation();
    QPoint calculatePosition(QWidget* anchor) const;
    void animateToPosition(const QPoint& targetPos);
    /// Shared by switchTo (top-level) and switchSubmenuTo: rebuild already done,
    /// animate position + height from oldDisplayH to the new target.
    void animateSwitchTo(const QPoint& targetPos, int oldDisplayH);
    /// Smoothly slide/resize the open submenu to a new anchor's items (no hide/show).
    void switchSubmenuTo(QWidget* anchor, const QList<MenuItem>& items);
    void ensurePosAnim();
    void ensureHeightAnim();

    // --- Submenu hover arbitration (diagonal-aim detection + close debounce) ---
    void reconcileSubmenu();
    void commitSubmenuChange(MenuItemWidget* desired);
    bool isMouseAimingAtSubmenu() const;
    QRect submenuGlobalRect() const;
    void startSubmenuAimPoll();
    void stopSubmenuAimPoll();
    /// Visible painted body of the attached (top-level) popup, excluding the
    /// soft-shadow padding. Invalid for submenus (they use a plain rounded rect).
    QRectF attachedBodyRect() const;

private:
    QList<MenuItem> m_items;
    QList<MenuItemWidget*> m_itemWidgets;
    QVBoxLayout* m_layout = nullptr;
    MenuPopup* m_submenuPopup = nullptr;
    QPointer<MenuItemWidget> m_submenuAnchor; // Item whose submenu is currently shown
    QPointer<MenuItemWidget> m_pendingSubmenuAnchor;
    QList<MenuItem> m_pendingSubmenuItems;

    // Currently hovered parent item (drives reconcileSubmenu); QPointer auto-clears
    // when the widget is deleted on rebuild.
    QPointer<MenuItemWidget> m_hoveredItem;
    QTimer* m_submenuAimTimer = nullptr; // Polls cursor while a submenu is open
    QPoint m_lastMousePos;
    QPoint m_prevMousePos;
    qint64 m_submenuCloseArmedAt = -1; // ms timestamp when close debounce started

    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    QPropertyAnimation* m_heightAnim = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    qreal m_opacity = 0.0;
    bool m_isVisible = false;
    bool m_isHiding = false;
    bool m_isRebuilding = false;
    bool m_isAnimatingHeight = false;

    int m_cachedWidth = 0;
    int m_cachedHeight = 0;
    int m_targetHeight = 0;
    int m_displayHeight = 0;

    static constexpr int SHOW_DURATION = 120;
    static constexpr int HIDE_DURATION = 80;
    static constexpr int SLIDE_OFFSET = 20;
    static constexpr int SLIDE_DURATION = 200;
    static constexpr int SUBMENU_GAP = 8; // Gap between parent menu and submenu
    static constexpr int SUBMENU_AIM_POLL_MS = 30; // Cursor sampling cadence
    static constexpr int SUBMENU_CLOSE_DELAY_MS = 130; // Debounce before closing submenu
    static constexpr int SUBMENU_AIM_MIN_MOVE = 3; // Min px move to count as "aiming"

    bool m_isSubmenu = false; // When true, use horizontal slide animations
    bool m_submenuSlideFromLeft = true; // For submenu: slide in from left (show to right of parent)
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_TOPBAR_MENUPOPUP_H
