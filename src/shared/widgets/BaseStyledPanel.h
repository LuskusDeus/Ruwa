// SPDX-License-Identifier: MPL-2.0

// BaseStyledPanel.h
#ifndef RUWA_UI_WIDGETS_COMMON_BASESTYLEDPANEL_H
#define RUWA_UI_WIDGETS_COMMON_BASESTYLEDPANEL_H

#include "shared/style/WidgetStyleManager.h"

#include <QWidget>
#include <QPropertyAnimation>

class QPainter;
class QEnterEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Base class for styled panel widgets (non-interactive containers)
 *
 * Similar to BaseStyledWidget but:
 * - No active/pressed states
 * - Optional hover effects
 * - Used for containers like BaseSettingsWidget, cards, thumbnails
 *
 * Layer system (simplified):
 *   0. Background layer
 *   1. Border layer
 *   2. Hover layer (optional)
 *   3. Content layer (override drawContentLayer)
 */
class BaseStyledPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    /**
     * @brief Construct with style name
     */
    explicit BaseStyledPanel(const QString& styleName, QWidget* parent = nullptr);

    /**
     * @brief Construct with explicit style
     */
    explicit BaseStyledPanel(const ruwa::ui::core::WidgetStyle& style, QWidget* parent = nullptr);

    ~BaseStyledPanel() override;

    // Style access
    const ruwa::ui::core::WidgetStyle& style() const { return m_style; }
    ruwa::ui::core::WidgetStyle& style() { return m_style; }
    void applyStyleChanges();
    void setStyle(const ruwa::ui::core::WidgetStyle& style);
    void setStyle(const QString& styleName);

    // Hover animation
    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    // Enable/disable hover effects for this panel
    void setHoverEnabled(bool enabled) { m_hoverEnabled = enabled; }
    bool isHoverEnabled() const { return m_hoverEnabled; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

    // Layer drawing (override for customization)
    virtual void drawBackgroundLayer(QPainter& painter, const QRectF& rect);
    virtual void drawBorderLayer(QPainter& painter, const QRectF& rect);
    virtual void drawHoverLayer(QPainter& painter, const QRectF& rect);
    virtual void drawContentLayer(QPainter& painter, const QRectF& rect);

    // Helpers
    int cornerRadius() const;
    QMargins contentPadding() const;

private slots:
    void onThemeChanged();
    void onGlobalSettingsChanged();

private:
    void initialize();
    void setupAnimations();
    void ensureBorderLayoutMargins();
    void updateSizeFromStyle();
    void connectSignals();

private:
    ruwa::ui::core::WidgetStyle m_style;

    bool m_hoverEnabled = true;
    qreal m_hoverProgress = 0.0;

    QPropertyAnimation* m_hoverAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_BASESTYLEDPANEL_H
