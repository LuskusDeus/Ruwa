// SPDX-License-Identifier: MPL-2.0

// HorizontalSeparator.h
#ifndef RUWA_UI_WIDGETS_COMMON_HORIZONTALSEPARATOR_H
#define RUWA_UI_WIDGETS_COMMON_HORIZONTALSEPARATOR_H

#include <QWidget>

namespace ruwa::ui::widgets {

/**
 * @brief Custom horizontal separator widget
 *
 * A horizontal separator designed for sidebars and vertical layouts.
 * Features crisp rendering and optional gradient fade effect.
 *
 * Features:
 * - Crisp 1px height (no antialiasing artifacts)
 * - Optional gradient fade effect on sides
 * - Minimal padding for compact layouts
 * - Pixel-perfect rendering by default
 */
class HorizontalSeparator : public QWidget {
    Q_OBJECT

public:
    explicit HorizontalSeparator(QWidget* parent = nullptr);
    ~HorizontalSeparator() override = default;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// Set whether to use gradient effect
    void setGradientEnabled(bool enabled)
    {
        m_gradientEnabled = enabled;
        update();
    }

    /// Set custom margins
    void setMargins(int left, int right)
    {
        m_marginLeft = left;
        m_marginRight = right;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int HEIGHT = 1; // Crisp height
    static constexpr int MARGIN_VERTICAL = 2; // Top/bottom padding

    int m_marginLeft { 0 }; // Left margin
    int m_marginRight { 0 }; // Right margin
    bool m_gradientEnabled { false }; // Disabled by default for crisp look
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_HORIZONTALSEPARATOR_H
