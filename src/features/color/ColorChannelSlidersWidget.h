// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COLORCHANNELSLIDERSWIDGET_H
#define RUWA_UI_WIDGETS_COLORCHANNELSLIDERSWIDGET_H

#include <QColor>
#include <QString>
#include <QWidget>

#include <array>

class QEvent;
class QFocusEvent;
class QKeyEvent;
class QLinearGradient;
class QMouseEvent;
class QPaintEvent;
class QTabletEvent;
class QVariantAnimation;

namespace ruwa::ui::widgets {

/**
 * Compact three-channel color control used by ColorPanel.
 *
 * The widget paints and handles all three sliders as one control so the rows
 * can share a compact outer plate while retaining independent hover and input.
 */
class ColorChannelSlidersWidget : public QWidget {
    Q_OBJECT

public:
    enum class Model {
        HSV,
        RGB,
    };
    Q_ENUM(Model)

    explicit ColorChannelSlidersWidget(Model model, QWidget* parent = nullptr);

    Model model() const { return m_model; }
    void setModel(Model model);

    QColor color() const;
    void setColor(const QColor& color);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void colorChanged(const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    QRectF outerRect() const;
    QRectF channelRect(int index) const;
    QRectF valueTrackRect(int index) const;
    int channelAt(const QPointF& position) const;
    int maximumForChannel(int index) const;
    int valueFromPosition(int index, const QPointF& position) const;
    qreal valueRatio(int index) const;
    QString channelLabel(int index) const;
    QString channelValueText(int index) const;
    QLinearGradient channelGradient(int index, const QRectF& rect) const;
    void setChannelValue(int index, int value, bool notify);
    void updateValueFromPosition(int index, const QPointF& position);
    void setHoveredChannel(int index);
    void animateHover(int index, qreal target);
    void onThemeChanged();

    Model m_model = Model::HSV;
    std::array<int, 3> m_values { 0, 0, 0 };
    int m_alpha = 255;
    int m_hoveredChannel = -1;
    int m_activeChannel = -1;
    int m_focusedChannel = 0;
    bool m_tabletDragActive = false;
    std::array<qreal, 3> m_hoverProgress { 0.0, 0.0, 0.0 };
    std::array<QVariantAnimation*, 3> m_hoverAnimations { nullptr, nullptr, nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COLORCHANNELSLIDERSWIDGET_H
