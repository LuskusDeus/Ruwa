// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COLORPICKER_COLORSLOTSWITCHWIDGET_H
#define RUWA_UI_WIDGETS_COLORPICKER_COLORSLOTSWITCHWIDGET_H

#include <QColor>
#include <QRectF>
#include <QWidget>

class QPropertyAnimation;

namespace ruwa::ui::widgets {

class ColorSlotSwitchWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverFg READ hoverFg WRITE setHoverFg)
    Q_PROPERTY(qreal hoverBg READ hoverBg WRITE setHoverBg)
    Q_PROPERTY(qreal hoverSwap READ hoverSwap WRITE setHoverSwap)

public:
    explicit ColorSlotSwitchWidget(QWidget* parent = nullptr);

    void setForegroundColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setActiveForeground(bool isForeground);
    bool isActiveForeground() const { return m_activeForeground; }

    qreal hoverFg() const { return m_hoverFg; }
    void setHoverFg(qreal p);
    qreal hoverBg() const { return m_hoverBg; }
    void setHoverBg(qreal p);
    qreal hoverSwap() const { return m_hoverSwap; }
    void setHoverSwap(qreal p);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void activeSlotChanged(bool isForeground);
    void swapRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class HitTarget { None, Foreground, Background, Swap };

    QRectF foregroundRect() const;
    QRectF backgroundRect() const;
    QRectF swapButtonRect() const;
    HitTarget hitTest(const QPoint& pos) const;

    void updateHoverState(HitTarget target);
    void startAnimation(QPropertyAnimation* anim, qreal target);

    QColor m_foregroundColor = Qt::black;
    QColor m_backgroundColor = Qt::white;
    bool m_activeForeground = true;

    qreal m_hoverFg = 0.0;
    qreal m_hoverBg = 0.0;
    qreal m_hoverSwap = 0.0;

    QPropertyAnimation* m_hoverFgAnim = nullptr;
    QPropertyAnimation* m_hoverBgAnim = nullptr;
    QPropertyAnimation* m_hoverSwapAnim = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COLORPICKER_COLORSLOTSWITCHWIDGET_H
