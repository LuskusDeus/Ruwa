// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_PROGRESSHANDLESLIDER_H
#define RUWA_UI_WIDGETS_COMMON_PROGRESSHANDLESLIDER_H

#include <QWidget>
#include <QString>

class QVariantAnimation;
class QTabletEvent;

namespace ruwa::ui::widgets {

class ProgressHandleSlider : public QWidget {
    Q_OBJECT

public:
    enum class ValueDisplayMode { RawValue, Percent };

    explicit ProgressHandleSlider(QWidget* parent = nullptr);

    void setRange(int minimum, int maximum);
    void setValue(int value);
    void setOrientation(Qt::Orientation orientation);
    void setFillInset(qreal inset);
    void setBackgroundOpacity(qreal surfaceOpacity, qreal trackOpacity);
    void setProgressFillOpacity(qreal opacity);
    void setShowValueText(bool show);
    void setValueDisplayMode(ValueDisplayMode mode);
    void setValueTextPrefix(const QString& prefix);
    void setValueTextSuffix(const QString& suffix);
    void setCustomDisplayText(const QString& text);

    int value() const { return m_value; }
    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }
    Qt::Orientation orientation() const { return m_orientation; }
    qreal fillInset() const { return m_fillInset; }
    qreal surfaceOpacity() const { return m_surfaceOpacity; }
    qreal trackOpacity() const { return m_trackOpacity; }
    qreal progressFillOpacity() const;
    bool showValueText() const { return m_showValueText; }
    ValueDisplayMode valueDisplayMode() const { return m_valueDisplayMode; }
    QString valueTextPrefix() const { return m_valueTextPrefix; }
    QString valueTextSuffix() const { return m_valueTextSuffix; }
    QString customDisplayText() const { return m_customDisplayText; }

signals:
    void valueChanged(int value);
    void sliderPressed();
    void sliderReleased();

protected:
    void paintEvent(QPaintEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onThemeChanged();

private:
    QRectF contentRect() const;
    QRectF trackRect(const QRectF& content) const;
    QRectF progressRect(const QRectF& track) const;
    QRectF handleRect(const QRectF& track, const QRectF& progress) const;
    qreal valueToRatio(int value) const;
    int ratioToValue(qreal ratio) const;
    QString displayText() const;
    void setValueFromPosition(const QPointF& position);

private:
    int m_minimum = 0;
    int m_maximum = 100;
    int m_value = 100;
    bool m_dragging = false;
    bool m_tabletDragActive = false;
    bool m_hovered = false;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation* m_hoverAnimation = nullptr;
    bool m_showValueText = true;
    ValueDisplayMode m_valueDisplayMode = ValueDisplayMode::Percent;
    QString m_valueTextPrefix;
    QString m_valueTextSuffix = "%";
    QString m_customDisplayText;
    Qt::Orientation m_orientation = Qt::Horizontal;
    qreal m_fillInset = -1.0;
    qreal m_surfaceOpacity = 1.0;
    qreal m_trackOpacity = 0.85;
    qreal m_progressStartOpacity = 220.0 / 255.0;
    qreal m_progressFinishOpacity = 180.0 / 255.0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_PROGRESSHANDLESLIDER_H
