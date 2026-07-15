// SPDX-License-Identifier: MPL-2.0

// NumericInputField.cpp
#include "NumericInputField.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QDoubleValidator>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QWheelEvent>

#include <cmath>

namespace ruwa::ui::widgets {

NumericInputField::NumericInputField(QWidget* parent)
    : QLineEdit(parent)
{
    setAttribute(Qt::WA_Hover);
    setFrame(false);
    setAlignment(Qt::AlignCenter);

    m_validator = new QDoubleValidator(m_minimum, m_maximum, m_decimals, this);
    m_validator->setNotation(QDoubleValidator::StandardNotation);
    setValidator(m_validator);

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(180);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &NumericInputField::onThemeChanged);
    connect(this, &QLineEdit::textEdited, this, &NumericInputField::onTextEdited);
    connect(this, &QLineEdit::editingFinished, this, &NumericInputField::onEditingFinished);

    applyPalette();
    updateMargins();
    setText(formatValue(m_value));
}

NumericInputField::~NumericInputField() = default;

void NumericInputField::setRange(double minimum, double maximum)
{
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    m_minimum = minimum;
    m_maximum = maximum;
    m_validator->setRange(m_minimum, m_maximum, m_decimals);
    applyValue(m_value, /*reformatText=*/true);
}

void NumericInputField::setSingleStep(double step)
{
    m_step = step > 0.0 ? step : 1.0;
}

void NumericInputField::setDecimals(int decimals)
{
    m_decimals = qMax(0, decimals);
    m_validator->setDecimals(m_decimals);
    applyValue(m_value, /*reformatText=*/true);
}

void NumericInputField::setValue(double value)
{
    applyValue(value, /*reformatText=*/true);
}

void NumericInputField::setSuffix(const QString& suffix)
{
    if (m_suffix == suffix) {
        return;
    }
    m_suffix = suffix;
    updateMargins();
    update();
}

void NumericInputField::setHoverProgress(qreal p)
{
    m_hoverProgress = qBound(0.0, p, 1.0);
    update();
}

QSize NumericInputField::sizeHint() const
{
    const QSize base = QLineEdit::sizeHint();
    return QSize(base.width(), ruwa::ui::core::ThemeManager::instance().scaled(BaseHeight));
}

void NumericInputField::applyValue(double value, bool reformatText)
{
    const double clamped = qBound(m_minimum, value, m_maximum);
    const double scale = std::pow(10.0, m_decimals);
    const double rounded = std::round(clamped * scale) / scale;

    const bool changed = !qFuzzyCompare(m_value + 1.0, rounded + 1.0);
    m_value = rounded;

    if (reformatText) {
        const QSignalBlocker blocker(this);
        setText(formatValue(m_value));
    }
    if (changed) {
        emit valueChanged(m_value);
    }
}

QString NumericInputField::formatValue(double value) const
{
    return QString::number(value, 'f', m_decimals);
}

void NumericInputField::onTextEdited(const QString& text)
{
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok) {
        return; // not a complete number yet ("-", "", "1.") — wait for more input
    }
    applyValue(parsed, /*reformatText=*/false);
}

void NumericInputField::nudge(double delta)
{
    applyValue(m_value + delta, /*reformatText=*/true);
}

void NumericInputField::onEditingFinished()
{
    applyValue(m_value, /*reformatText=*/true);
}

void NumericInputField::onThemeChanged()
{
    applyPalette();
    updateMargins();
    update();
}

void NumericInputField::startHoverAnimation(bool hovered)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

void NumericInputField::enterEvent(QEnterEvent* event)
{
    QLineEdit::enterEvent(event);
    startHoverAnimation(true);
}

void NumericInputField::leaveEvent(QEvent* event)
{
    QLineEdit::leaveEvent(event);
    startHoverAnimation(false);
}

void NumericInputField::wheelEvent(QWheelEvent* event)
{
    // Gated on focus so scrolling the panel this field sits in never nudges
    // its value by accident — the user must click in first.
    if (!hasFocus()) {
        QLineEdit::wheelEvent(event);
        return;
    }
    const bool big = event->modifiers().testFlag(Qt::ShiftModifier);
    const double dir = event->angleDelta().y() > 0 ? 1.0 : -1.0;
    nudge(dir * m_step * (big ? 10.0 : 1.0));
    event->accept();
}

void NumericInputField::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        const bool big = event->modifiers().testFlag(Qt::ShiftModifier);
        const double dir = event->key() == Qt::Key_Up ? 1.0 : -1.0;
        nudge(dir * m_step * (big ? 10.0 : 1.0));
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void NumericInputField::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    updateMargins();
}

int NumericInputField::suffixSlotWidth() const
{
    if (m_suffix.isEmpty()) {
        return 0;
    }
    const QFontMetrics fm(font());
    return fm.horizontalAdvance(m_suffix)
        + ruwa::ui::core::ThemeManager::instance().scaled(BaseSuffixGap);
}

void NumericInputField::updateMargins()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int side = theme.scaled(BaseSidePad);
    setTextMargins(side, 0, side + suffixSlotWidth(), 0);
}

void NumericInputField::applyPalette()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Text, colors.text);
    pal.setColor(QPalette::Highlight, colors.primary);
    pal.setColor(QPalette::HighlightedText, colors.textOnPrimary());
    setPalette(pal);
    setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));
}

void NumericInputField::paintEvent(QPaintEvent* event)
{
    using TC = ruwa::ui::core::ThemeColors;
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(rect());
    const qreal pillR = qMax(0.0, r.height() * 0.5 - 0.5);
    const QRectF fillRect = r.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal fillR = qMax(0.0, pillR - 1.0);

    // Resting background (matches HexColorInput / the Color-panel hex input).
    p.setPen(Qt::NoPen);
    p.setBrush(colors.surfaceAlt);
    p.drawRoundedRect(fillRect, fillR, fillR);

    if (m_hoverProgress > 0.001) {
        QColor plate = colors.surfaceElevated();
        plate.setAlpha(qBound(0, qRound(m_hoverProgress * 90), 255));
        p.setPen(Qt::NoPen);
        p.setBrush(plate);
        p.drawRoundedRect(fillRect, fillR, fillR);
    }

    const qreal accent = qMax<qreal>(m_hoverProgress, hasFocus() ? 1.0 : 0.0);
    QColor borderTop = TC::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), accent);
    QColor borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(
        p, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);

    p.end();

    // Let QLineEdit paint the text/cursor on top.
    QLineEdit::paintEvent(event);

    if (!m_suffix.isEmpty()) {
        QPainter overlay(this);
        overlay.setRenderHint(QPainter::Antialiasing);
        const QColor suffixColor = TC::interpolate(colors.textMuted, colors.text, accent);
        overlay.setPen(suffixColor);
        overlay.setFont(font());
        const int side = ruwa::ui::core::ThemeManager::instance().scaled(BaseSidePad);
        const QRect suffixRect(width() - side - suffixSlotWidth(), 0, suffixSlotWidth(), height());
        overlay.drawText(suffixRect, Qt::AlignVCenter | Qt::AlignRight, m_suffix);
    }
}

} // namespace ruwa::ui::widgets
