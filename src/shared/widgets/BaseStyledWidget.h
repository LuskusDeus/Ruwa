// SPDX-License-Identifier: MPL-2.0

// BaseStyledWidget.h
#ifndef RUWA_UI_WIDGETS_COMMON_BASESTYLEDWIDGET_H
#define RUWA_UI_WIDGETS_COMMON_BASESTYLEDWIDGET_H

#include "shared/style/WidgetStyleManager.h"

#include <QPushButton>
#include <QPropertyAnimation>

class QPainter;
class QEnterEvent;
class QEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Base class for all styled widgets in Ruwa
 *
 * Implements layer-based rendering system:
 *   0. Background layer
 *   1. Border layer (inactive)
 *   2. Hover overlay layer
 *   3. Hover glow layer (optional)
 *   4. Active background layer
 *   5. Active border layer
 *   6. Press overlay layer
 *   7. Content layer (text, icons - customizable)
 *
 * Features:
 *   - Automatic theme integration
 *   - Animation management (hover, active, glow)
 *   - Respects global settings (animations on/off, etc.)
 *   - Scalable metrics
 *
 * Derived classes can:
 *   - Override drawContentLayer() for custom content
 *   - Override any drawXxxLayer() for full customization
 *   - Add extra layers via drawCustomLayers()
 */
class BaseStyledWidget : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal activeProgress READ activeProgress WRITE setActiveProgress)
    Q_PROPERTY(qreal glowSize READ glowSize WRITE setGlowSize)

public:
    /**
     * @brief Construct with style name
     * @param styleName Name of registered style (e.g., "SidebarButton")
     * @param parent Parent widget
     */
    explicit BaseStyledWidget(const QString& styleName, QWidget* parent = nullptr);

    /**
     * @brief Construct with explicit style
     * @param style Style to use (copied, can be modified)
     * @param parent Parent widget
     */
    explicit BaseStyledWidget(const ruwa::ui::core::WidgetStyle& style, QWidget* parent = nullptr);

    ~BaseStyledWidget() override;

    // ========================================================================
    // Style Access
    // ========================================================================

    /// Get current style (const)
    const ruwa::ui::core::WidgetStyle& style() const { return m_style; }

    /// Get mutable style for modifications
    ruwa::ui::core::WidgetStyle& style() { return m_style; }

    /// Apply style changes (call after modifying style())
    void applyStyleChanges();

    /// Set completely new style
    void setStyle(const ruwa::ui::core::WidgetStyle& style);
    void setStyle(const QString& styleName);

    // ========================================================================
    // State Control
    // ========================================================================

    /// Set active/selected state. When \a animated is false, snaps immediately (no transition).
    void setActive(bool active, bool animated = true);
    bool isActive() const { return m_isActive; }

    /// Check if currently pressed
    bool isPressed() const { return m_isPressed; }

    // ========================================================================
    // Animation Properties
    // ========================================================================

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal activeProgress() const { return m_activeProgress; }
    void setActiveProgress(qreal progress);

    qreal glowSize() const { return m_glowSize; }
    void setGlowSize(qreal size);

protected:
    // ========================================================================
    // Layer Drawing (override for customization)
    // ========================================================================

    /// Main paint event - orchestrates layer drawing
    void paintEvent(QPaintEvent* event) override;

    /// Layer 0: Background fill
    virtual void drawBackgroundLayer(QPainter& painter, const QRectF& rect);

    /// Layer 1: Border (inactive state)
    virtual void drawBorderLayer(QPainter& painter, const QRectF& rect);

    /// Layer 2: Hover overlay
    virtual void drawHoverLayer(QPainter& painter, const QRectF& rect);

    /// Layer 3: Hover glow (radial gradient)
    virtual void drawHoverGlowLayer(QPainter& painter, const QRectF& rect);

    /// Layer 4: Active background
    virtual void drawActiveBackgroundLayer(QPainter& painter, const QRectF& rect);

    /// Layer 5: Active border
    virtual void drawActiveBorderLayer(QPainter& painter, const QRectF& rect);

    /// Layer 6: Press overlay
    virtual void drawPressLayer(QPainter& painter, const QRectF& rect);

    /// Layer 7: Content (text, icon) - override for custom content
    virtual void drawContentLayer(QPainter& painter, const QRectF& rect);

    /// Hook for additional custom layers (called after content)
    virtual void drawCustomLayers(QPainter& painter, const QRectF& rect);

    // ========================================================================
    // Content Drawing Helpers
    // ========================================================================

    /// Get interpolated text color based on hover/active progress
    QColor currentTextColor() const;

    /// Get interpolated secondary text color
    QColor currentSecondaryTextColor() const;

    /// Colorize icon pixmap to match color
    QPixmap colorizeIcon(const QPixmap& source, const QColor& color) const;

    /// Get scaled corner radius
    int cornerRadius() const;

    /// Get scaled content padding
    QMargins contentPadding() const;

    // ========================================================================
    // Event Handlers
    // ========================================================================

    void changeEvent(QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onThemeChanged();
    void onGlobalSettingsChanged();

private:
    void initialize();
    void setupAnimations();
    void updateSizeFromStyle();
    void connectSignals();

    /// Start hover animation
    void animateHover(bool hovered);

    /// Start active animation
    void animateActive(bool active);

    /// Start glow size animation
    void animateGlow(bool show);

private:
    // Style
    ruwa::ui::core::WidgetStyle m_style;

    // State
    bool m_isActive = false;
    bool m_isPressed = false;

    // Animation progress values
    qreal m_hoverProgress = 0.0;
    qreal m_activeProgress = 0.0;
    qreal m_glowSize = 0.0;

    // Animations
    QPropertyAnimation* m_hoverAnimation = nullptr;
    QPropertyAnimation* m_activeAnimation = nullptr;
    QPropertyAnimation* m_glowAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_BASESTYLEDWIDGET_H
