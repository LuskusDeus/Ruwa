// SPDX-License-Identifier: MPL-2.0

// ProgressSlider.h
#ifndef RUWA_UI_WIDGETS_COMMON_PROGRESSSLIDER_H
#define RUWA_UI_WIDGETS_COMMON_PROGRESSSLIDER_H

#include <QWidget>

class QVariantAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Simple rounded progress bar with optional percentage text
 *
 * Read-only progress display. Colors from theme.
 * Text is displayed above the slider, right-aligned.
 */
class ProgressSlider : public QWidget {
    Q_OBJECT

public:
    explicit ProgressSlider(QWidget* parent = nullptr);
    ~ProgressSlider() override = default;

    void setRange(int minimum, int maximum);
    void setValue(int value);
    void setShowText(bool show);

    int value() const { return m_value; }
    int minimum() const { return m_minimum; }
    int maximum() const { return m_maximum; }
    bool showText() const { return m_showText; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateScaledSizes();
    void updateThemeColors();

private slots:
    void onThemeChanged();

private:
    int m_minimum { 0 };
    int m_maximum { 100 };
    int m_value { 0 };
    bool m_showText { true };
    qreal m_displayedRatio { 0.0 };
    QVariantAnimation* m_progressAnimation { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_PROGRESSSLIDER_H
