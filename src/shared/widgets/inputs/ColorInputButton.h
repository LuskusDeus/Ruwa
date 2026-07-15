// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_COLORINPUTBUTTON_H
#define RUWA_UI_WIDGETS_COMMON_COLORINPUTBUTTON_H

#include "shared/widgets/BaseStyledWidget.h"
#include <QColor>
#include <QPropertyAnimation>
#include <QString>

namespace ruwa::ui::widgets {

struct ColorInputButtonOptions {
    bool boldLabel = true;
    bool showLabel = true;
    bool showHex = true;
    bool boxedStyle = false;
    /// Pill/capsule frame matching the Color-panel hex input: surfaceAlt resting
    /// plate, soft surfaceElevated hover, gradient capsule border. Takes
    /// precedence over boxedStyle when set.
    bool capsuleStyle = false;
    /// Request an opacity slider in the shared color-picker popup opened from this
    /// input (the color it stores carries an alpha channel).
    bool alphaEnabled = false;
    qreal hoverStrength = 0.10; // 0..1 multiplier for accent alpha
    int baseHeight = 40;
};

class ColorInputButton : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    Q_PROPERTY(qreal hoverAlpha READ hoverAlpha WRITE setHoverAlpha)

public:
    explicit ColorInputButton(
        const QString& label, const QColor& initialColor, QWidget* parent = nullptr);
    explicit ColorInputButton(const QString& label, const QColor& initialColor,
        const ColorInputButtonOptions& options, QWidget* parent = nullptr);

    QColor color() const { return m_color; }
    void setColor(const QColor& color);
    QString label() const { return m_label; }

    qreal hoverAlpha() const { return m_hoverAlpha; }
    void setHoverAlpha(qreal alpha);

    void setOptions(const ColorInputButtonOptions& options);
    const ColorInputButtonOptions& options() const { return m_options; }

    /// Whether the picker opened from this input should offer an alpha slider.
    bool alphaEnabled() const { return m_options.alphaEnabled; }

    void setLabel(const QString& label)
    {
        m_label = label;
        update();
    }

    void clearHover();

signals:
    void colorChanged(const QColor& color);
    void colorPickerRequested(const QColor& initialColor);

protected:
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onThemeChanged();

private:
    void updateScaledSize();

private:
    QString m_label;
    QColor m_color;
    ColorInputButtonOptions m_options;
    qreal m_hoverAlpha { 0.0 };
    QPropertyAnimation* m_hoverAnimation { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_COLORINPUTBUTTON_H
