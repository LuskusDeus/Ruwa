// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_CURVEEDITORWIDGET_H
#define RUWA_UI_WIDGETS_COMMON_CURVEEDITORWIDGET_H

#include <QString>
#include <QVector>
#include <QWidget>

namespace ruwa::ui::widgets {

class CurveEditorPlot;

class CurveEditorWidget : public QWidget {
    Q_OBJECT

public:
    struct Point {
        qreal x = 0.0;
        qreal y = 0.0;
        qreal smoothness = 1.0;
    };
    struct AxisDisplaySpec {
        qreal minValue = 0.0;
        qreal maxValue = 1.0;
        qreal displayScale = 1.0;
        int displayDecimals = 0;
        QString suffix;
        QVector<qreal> tickValues;
        bool visible = true;
    };

    explicit CurveEditorWidget(QWidget* parent = nullptr);

    void setPoints(const QVector<Point>& points);
    QVector<Point> points() const;
    void setVerticalRange(qreal maxValue);
    void setVerticalRange(qreal minValue, qreal maxValue);
    qreal verticalRange() const;
    void setHorizontalAxisDisplay(const AxisDisplaySpec& spec);
    AxisDisplaySpec horizontalAxisDisplay() const;
    void setVerticalAxisDisplay(const AxisDisplaySpec& spec);
    AxisDisplaySpec verticalAxisDisplay() const;

    int selectedPointIndex() const;
    void setSelectedPointIndex(int index);

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void pointsChanged();
    void editingFinished();
    void selectedPointChanged(int index);

private slots:
    void onPlotPointsChanged();
    void onPlotSelectionChanged(int index);
    void onThemeChanged();

private:
    void applyStyles();

private:
    CurveEditorPlot* m_plot = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_CURVEEDITORWIDGET_H
