// SPDX-License-Identifier: MPL-2.0

// Separator.h
#ifndef RUWA_UI_WIDGETS_COMMON_SEPARATOR_H
#define RUWA_UI_WIDGETS_COMMON_SEPARATOR_H

#include <QWidget>

namespace ruwa::ui::widgets {

/**
 * @brief Custom separator widget with crisp appearance
 *
 * A vertical separator designed for modern, clean UI.
 * Designed to work seamlessly with TopBar for a compact appearance.
 *
 * Features:
 * - Crisp 2px width (no antialiasing artifacts)
 * - Optional gradient fade effect
 * - Minimal padding for compact layouts (6px total width)
 * - Pixel-perfect rendering by default
 */
class Separator : public QWidget {
    Q_OBJECT

public:
    explicit Separator(QWidget* parent = nullptr);
    ~Separator() override = default;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// Set whether to use gradient effect
    void setGradientEnabled(bool enabled)
    {
        m_gradientEnabled = enabled;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    static constexpr int WIDTH = 2; // Optimal width for crisp rendering
    static constexpr int MARGIN = 8; // Vertical margin
    static constexpr int PADDING = 4; // Horizontal padding (total width = 2 + 4 = 6px)

    bool m_gradientEnabled { false }; // Disabled by default for crisp look
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_SEPARATOR_H
