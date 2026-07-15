// SPDX-License-Identifier: MPL-2.0

// ColorPickerOverlay.h
#ifndef RUWA_UI_WIDGETS_COLORPICKER_COLORPICKEROVERLAY_H
#define RUWA_UI_WIDGETS_COLORPICKER_COLORPICKEROVERLAY_H

#include <QObject>
#include <QPropertyAnimation>
#include <QColor>
#include <QPointer>
#include <QString>

class QWidget;

namespace ruwa::ui::widgets {

class ColorPicker;

/**
 * @brief Controller for ColorPicker with click-outside-to-close
 *
 * Uses qApp event filter to detect clicks outside picker.
 * Picker is a direct child of container widget.
 */
class ColorPickerOverlay : public QObject {
    Q_OBJECT
    Q_PROPERTY(QPoint pickerPos READ pickerPos WRITE setPickerPos)

public:
    explicit ColorPickerOverlay(QWidget* container);
    ~ColorPickerOverlay() override;

    void showPicker(const QColor& initialColor = Qt::white, QWidget* sourceButton = nullptr);
    void hidePicker();
    void forceHide();
    bool isActive() const;

    ColorPicker* picker() const { return m_picker; }
    QWidget* sourceButton() const { return m_sourceButton; }

    QPoint pickerPos() const;
    void setPickerPos(const QPoint& pos);

signals:
    void colorSelected(const QColor& color);
    void canceled();
    void hidden();
    void shown();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onPickerCanceled();

private:
    void setupPicker();
    void setupAnimations();
    QPoint calculatePickerPosition(QWidget* sourceButton) const;
    QString pickerTitleForSource(QWidget* sourceButton) const;
    void animatePickerTo(const QPoint& targetPos);
    bool isColorInputButton(QWidget* widget) const;
    bool isPickerOrChild(QWidget* widget, const QPoint& globalPos) const;

private:
    QWidget* m_container = nullptr;
    ColorPicker* m_picker = nullptr;
    QPointer<QWidget> m_sourceButton;

    bool m_isShowing = false;
    bool m_isHiding = false;

    QPropertyAnimation* m_posAnimation = nullptr;

    static constexpr int OffsetFromButton = 8;
    static constexpr int SlideOffset = 20;
    static constexpr int PositionAnimationDuration = 200;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COLORPICKER_COLORPICKEROVERLAY_H
