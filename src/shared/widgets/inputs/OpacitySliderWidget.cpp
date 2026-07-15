// SPDX-License-Identifier: MPL-2.0

#include "OpacitySliderWidget.h"
#include "ProgressHandleSlider.h"

#include "features/theme/manager/ThemeManager.h"

#include <QHBoxLayout>

namespace ruwa::ui::widgets {

OpacitySliderWidget::OpacitySliderWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_slider = new ProgressHandleSlider(this);
    m_slider->setRange(0, 100);
    m_slider->setValue(100);
    m_slider->setShowValueText(true);
    m_slider->setValueDisplayMode(ProgressHandleSlider::ValueDisplayMode::Percent);
    m_slider->setValueTextPrefix(QString());
    m_slider->setValueTextSuffix("%");
    m_slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(m_slider, 1);

    connect(m_slider, &ProgressHandleSlider::valueChanged, this,
        &OpacitySliderWidget::onSliderValueChanged);
    connect(m_slider, &ProgressHandleSlider::sliderPressed, this,
        [this]() { emit opacityDragStarted(m_slider->value() / 100.0); });
    connect(m_slider, &ProgressHandleSlider::sliderReleased, this,
        [this]() { emit opacityCommitted(m_slider->value() / 100.0); });
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &OpacitySliderWidget::onThemeChanged);

    applyTheme();
}

void OpacitySliderWidget::setOpacity(qreal opacity)
{
    const int value = qRound(qBound(0.0, opacity, 1.0) * 100.0);
    if (m_slider->value() == value) {
        return;
    }
    m_slider->setValue(value);
}

qreal OpacitySliderWidget::opacity() const
{
    return m_slider->value() / 100.0;
}

void OpacitySliderWidget::onSliderValueChanged(int value)
{
    emit opacityChanged(value / 100.0);
}

void OpacitySliderWidget::onThemeChanged()
{
    applyTheme();
}

void OpacitySliderWidget::applyTheme()
{
    auto& tm = ruwa::ui::core::ThemeManager::instance();

    m_slider->setFixedHeight(tm.scaled(24));
}

} // namespace ruwa::ui::widgets
