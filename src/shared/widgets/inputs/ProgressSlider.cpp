// SPDX-License-Identifier: MPL-2.0

// ProgressSlider.cpp
#include "ProgressSlider.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

namespace {
const int BASE_TRACK_HEIGHT = 8;
const int BASE_RADIUS = 4;
const int BASE_FONT_SIZE = 9;
const int BASE_TEXT_TRACK_GAP = 2;
const int BASE_PROGRESS_ANIMATION_DURATION = 250;
const int TRACK_DARKEN_FACTOR = 140;
const int FILL_DARKEN_FACTOR = 105;
} // namespace

ProgressSlider::ProgressSlider(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_progressAnimation = new QVariantAnimation(this);
    m_progressAnimation->setDuration(BASE_PROGRESS_ANIMATION_DURATION);
    m_progressAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        m_progressAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_displayedRatio = qBound(0.0, value.toReal(), 1.0);
            update();
        });

    updateScaledSizes();
    updateThemeColors();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ProgressSlider::onThemeChanged);
}

void ProgressSlider::setRange(int minimum, int maximum)
{
    if (minimum > maximum)
        qSwap(minimum, maximum);
    if (m_minimum == minimum && m_maximum == maximum)
        return;
    m_minimum = minimum;
    m_maximum = maximum;
    setValue(qBound(m_minimum, m_value, m_maximum));
}

void ProgressSlider::setValue(int value)
{
    const int v = qBound(m_minimum, value, m_maximum);
    if (m_value == v)
        return;
    m_value = v;

    const qreal range = m_maximum - m_minimum;
    const qreal targetRatio = range > 0 ? qBound(0.0, qreal(v - m_minimum) / range, 1.0) : 0.0;

    m_progressAnimation->stop();
    m_progressAnimation->setStartValue(m_displayedRatio);
    m_progressAnimation->setEndValue(targetRatio);
    m_progressAnimation->start();
}

void ProgressSlider::setShowText(bool show)
{
    if (m_showText == show)
        return;
    m_showText = show;
    updateScaledSizes();
    update();
}

void ProgressSlider::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (width() <= 0 || height() <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int trackH = theme.scaled(BASE_TRACK_HEIGHT);
    const int radius = theme.scaled(BASE_RADIUS);
    const int gap = theme.scaled(BASE_TEXT_TRACK_GAP);

    const qreal ratio = m_displayedRatio;

    const int textH = m_showText ? (theme.scaledFontSize(BASE_FONT_SIZE) + 4) : 0;
    const int trackY = textH + gap;
    QRectF trackRect(0, trackY, width(), trackH);

    // Track (background)
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.surface.darker(TRACK_DARKEN_FACTOR));
    painter.drawRoundedRect(trackRect, radius, radius);

    // Fill (progress) — smooth animated
    if (ratio > 0.001) {
        QRectF fillRect = trackRect.adjusted(0, 0, -(1.0 - ratio) * trackRect.width(), 0);
        painter.setBrush(colors.primary.darker(FILL_DARKEN_FACTOR));
        painter.drawRoundedRect(fillRect, radius, radius);
    }

    // Percentage text above the slider, right-aligned
    if (m_showText && textH > 0) {
        const int pct = qRound(ratio * 100);
        const QString text = QString::number(pct) + QStringLiteral("%");

        QFont font = painter.font();
        font.setPointSize(theme.scaledFontSize(BASE_FONT_SIZE));
        painter.setFont(font);

        painter.setPen(colors.textMuted);
        QRectF textRect(0, 0, width(), textH);
        painter.drawText(textRect, Qt::AlignRight | Qt::AlignBottom, text);
    }
}

void ProgressSlider::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int trackH = theme.scaled(BASE_TRACK_HEIGHT);
    const int gap = theme.scaled(BASE_TEXT_TRACK_GAP);
    const int textH = m_showText ? (theme.scaledFontSize(BASE_FONT_SIZE) + 4) : 0;
    setFixedHeight(textH + gap + trackH);
}

void ProgressSlider::updateThemeColors()
{
    update();
}

void ProgressSlider::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::widgets
