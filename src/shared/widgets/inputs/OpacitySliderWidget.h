// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_OPACITYSLIDERWIDGET_H
#define RUWA_UI_WIDGETS_COMMON_OPACITYSLIDERWIDGET_H

#include <QWidget>

namespace ruwa::ui::widgets {

class ProgressHandleSlider;

class OpacitySliderWidget : public QWidget {
    Q_OBJECT

public:
    explicit OpacitySliderWidget(QWidget* parent = nullptr);

    void setOpacity(qreal opacity);
    qreal opacity() const;

signals:
    void opacityChanged(qreal opacity);
    /// Emitted when user starts dragging (store for undo)
    void opacityDragStarted(qreal opacity);
    /// Emitted when user releases the slider (for undo - save only on release)
    void opacityCommitted(qreal opacity);

private slots:
    void onSliderValueChanged(int value);
    void onThemeChanged();

private:
    void applyTheme();

private:
    ProgressHandleSlider* m_slider = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_OPACITYSLIDERWIDGET_H
