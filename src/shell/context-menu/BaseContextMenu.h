// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   BASE CONTEXT MENU
// ======================================================================================

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_BASECONTEXTMENU_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_BASECONTEXTMENU_H

#include "shell/context-menu/ContextMenuTypes.h"
#include <QWidget>
#include <QVariantMap>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QMargins>
#include <QRectF>

class QVBoxLayout;
class QPainter;
class QGraphicsOpacityEffect;
class QIcon;

namespace ruwa::ui::widgets {

class BaseStyledWidget;

/**
 * @brief Base class for all context menu overlay widgets
 *
 * Features:
 * - Transparent background with event pass-through
 * - Click-outside-to-close behavior
 * - Automatic positioning near cursor
 * - Fade-in/fade-out animations
 * - Position animation for menu reuse
 * - Theme integration
 * - Only the content area blocks events, rest is transparent
 */
class BaseContextMenu : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)
    Q_PROPERTY(QPoint menuPos READ menuPos WRITE setMenuPos)

public:
    explicit BaseContextMenu(QWidget* parent = nullptr);
    ~BaseContextMenu() override;

    /**
     * @brief Get the menu type (must be implemented by derived classes)
     */
    virtual ContextMenuType menuType() const = 0;

    /**
     * @brief Set context data for the menu
     */
    void setContext(const QVariantMap& context);

    /**
     * @brief Get context data
     */
    QVariantMap context() const { return m_context; }

    /**
     * @brief Show menu at global position (with animation if already visible)
     * @param globalPos Global screen position
     * @param context Context data for the menu
     * @param sourceWidget Widget that triggered the menu (optional, for positioning)
     */
    void showAt(
        const QPoint& globalPos, const QVariantMap& context, QWidget* sourceWidget = nullptr);

    /**
     * @brief Hide menu with animation
     */
    void hideAnimated();

    /**
     * @brief Override hide to stop all animations
     */
    void hide();

    /**
     * @brief Check if menu is active (visible or animating)
     */
    bool isActive() const;

    // Animation properties
    qreal showProgress() const { return m_showProgress; }
    void setShowProgress(qreal progress);

    QPoint menuPos() const;
    void setMenuPos(const QPoint& pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

    /**
     * @brief Get the content rect (area that blocks events)
     * Must be implemented by derived classes
     */
    virtual QRect contentRect() const = 0;

    /**
     * @brief Draw the menu content
     * Must be implemented by derived classes
     */
    virtual void drawContent(QPainter& painter) = 0;

    /// Soft drop shadow under a rounded panel (shared by standard + debug stub menus).
    static void paintMenuDropShadow(QPainter& painter, const QRectF& panelRect, qreal cornerRadius);

    /**
     * @brief Called when context is set
     * Override to react to context changes
     */
    virtual void onContextChanged() { }

protected:
    virtual QPoint calculateMenuPosition(
        const QPoint& globalPos, const QSize& menuSize, QWidget* sourceWidget) const;

    /// Full-size (final) visible panel rect used for anchor-relative placement.
    /// Defaults to the whole widget; StandardContextMenu overrides it to expose the
    /// inner panel rect (which carries the shadow/margin insets) at 100% progress.
    virtual QRect fullContentPanelRect() const;

private:
    bool isClickInsideContent(const QPoint& globalPos) const;
    void animateToPosition(const QPoint& targetPos);
    QPoint calculateAnchoredPosition();

protected:
    void syncVisualPosition();
    void updateSlideMetrics();
    virtual void applyPresentationLayout(qreal progress);
    /// Current 0.92→1.0 grow scale of the content panel (1.0 = none). Overridden
    /// by subclasses that scale their panel during the show animation, so the
    /// anchored-grow pivot compensation can stay in sync.
    virtual qreal presentationContentScale() const;
    virtual qreal presentationSlideDistancePx() const;
    virtual qreal presentationOpacity(qreal progress) const;

private:
    QVariantMap m_context;
    qreal m_showProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_isRepositioning = false;
    QVariantAnimation* m_showProgressAnim = nullptr;
    QPropertyAnimation* m_posAnimation = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPoint m_restPos;
    QRect m_anchorRectGlobal;
    QPoint m_growPivot;
    bool m_hasAnchor = false;
    bool m_placedAbove = false;
    qreal m_slidePx = 6.0;
    QEasingCurve m_ctxAppearEase;
    qreal m_showStartProgress = 0.0;
    qreal m_hideStartProgress = 1.0;

    static constexpr int ShowDurationMs = 200;
    static constexpr int HideDurationMs = 200;
    static constexpr int RepositionDuration = 85; // Очень быстро для snap-like эффекта
};

/**
 * @brief Standard context menu base (A/B style in mockup)
 *
 * Provides:
 * - Unified menu shell drawing
 * - Vertical content layout for rows/sections/widgets
 * - Minimal constraints for typical context actions
 */
class StandardContextMenu : public BaseContextMenu {
    Q_OBJECT

public:
    explicit StandardContextMenu(QWidget* parent = nullptr);
    ~StandardContextMenu() override = default;

protected:
    QRect contentRect() const override;
    void drawContent(QPainter& painter) override;
    void onContextChanged() override;
    QRect fullContentPanelRect() const override { return m_panelFull; }

    QWidget* contentWidget() const { return m_contentWidget; }
    QVBoxLayout* contentLayout() const { return m_contentLayout; }
    void setContentMargins(const QMargins& margins);
    void updateMenuSize();
    void applyPresentationLayout(qreal progress) override;
    qreal presentationContentScale() const override;

    /// Called when context changes; derived classes must update the UI.
    virtual void rebuildStandardMenu() = 0;

    /// Expand the content size hint (default min width 180, min height 90, theme-scaled).
    virtual QSize expandMenuContentHint(const QSize& hint) const;
    virtual bool usesAttachedTopBarSurface() const { return false; }
    QRectF attachedBodyRect() const;

    /**
     * @brief Full-width action row: optional icon (left), label; theme-aligned hover / danger tint.
     * @param addToLayout If non-null, the row is appended there; otherwise to the menu's main
     * column.
     */
    BaseStyledWidget* addStandardMenuActionRow(const QIcon& icon, const QString& text,
        bool danger = false, QVBoxLayout* addToLayout = nullptr);

private:
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QRect m_panelRect;
    QSize m_sizeFull;
    QRect m_panelFull;
    QMargins m_contentMargins = QMargins(6, 6, 6, 6);
};

/**
 * @brief Custom context menu base with almost no visual/API restrictions
 *
 * Use this as a host when a menu needs fully custom widgets/painting.
 */
class CustomContextMenu : public BaseContextMenu {
    Q_OBJECT

public:
    explicit CustomContextMenu(QWidget* parent = nullptr);
    ~CustomContextMenu() override = default;

protected:
    QRect contentRect() const override;
    void drawContent(QPainter& painter) override;
    void onContextChanged() override;

    QWidget* customContentRoot() const { return m_customContentRoot; }
    void setCustomContentRoot(QWidget* rootWidget);
    void updateCustomSize();
    void applyPresentationLayout(qreal progress) override;

    /// Called when context changes; derived classes decide everything.
    virtual void rebuildCustomMenu() = 0;

private:
    QWidget* m_customContentRoot = nullptr;
    QRect m_contentRect;
    QSize m_sizeFull;
    QRect m_contentRectFull;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_BASECONTEXTMENU_H
