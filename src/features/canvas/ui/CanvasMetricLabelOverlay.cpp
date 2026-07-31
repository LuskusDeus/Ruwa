// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/CanvasMetricLabelOverlay.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyleManager.h"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QFont>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QSizePolicy>

namespace ruwa::ui::widgets {
namespace {
constexpr int kHorizontalPaddingBase = 13;
constexpr int kVerticalPaddingBase = 5;
constexpr int kMinimumWidthBase = 62;
constexpr int kAnchorGapBase = 8;
constexpr int kEdgeMarginBase = 6;
constexpr int kFadeInDurationMs = 90;
constexpr int kFadeOutDurationMs = 120;
} // namespace

CanvasMetricLabelOverlay::CanvasMetricLabelOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_label = new QLabel(this);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setObjectName(QStringLiteral("canvasMetricLabel"));

    auto* layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->addWidget(m_label);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);
    m_fadeAnimation = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_opacityEffect && m_opacityEffect->opacity() <= 0.0) {
            hide();
        }
    });
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() { applyTheme(); });
    applyTheme();
    hide();
}

CanvasMetricLabelOverlay::~CanvasMetricLabelOverlay() = default;

void CanvasMetricLabelOverlay::setMetricText(const QString& text)
{
    m_label->setText(text);
    adjustSize();
}

void CanvasMetricLabelOverlay::presentAtPoint(const QString& text, const QPointF& anchorPanel)
{
    setMetricText(text);
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(kAnchorGapBase);
    moveClamped(qRound(anchorPanel.x() - width() * 0.5), qRound(anchorPanel.y()) + gap);
    show();
    raise();
    fadeTo(1.0, kFadeInDurationMs);
}

void CanvasMetricLabelOverlay::presentNearRect(const QString& text, const QRectF& rectPanel)
{
    setMetricText(text);
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(kAnchorGapBase);
    const int margin = theme.scaled(kEdgeMarginBase);
    int y = qRound(rectPanel.bottom()) + gap;
    if (parentWidget() && y + height() + margin > parentWidget()->height()) {
        y = qRound(rectPanel.top()) - height() - gap;
    }
    moveClamped(qRound(rectPanel.right()) - width(), y);
    show();
    raise();
    fadeTo(1.0, kFadeInDurationMs);
}

void CanvasMetricLabelOverlay::moveClamped(int x, int y)
{
    QWidget* parent = parentWidget();
    if (!parent) {
        return;
    }
    const int margin = ruwa::ui::core::ThemeManager::instance().scaled(kEdgeMarginBase);
    x = qBound(margin, x, qMax(margin, parent->width() - width() - margin));
    y = qBound(margin, y, qMax(margin, parent->height() - height() - margin));
    move(x, y);
}

void CanvasMetricLabelOverlay::dismiss()
{
    if (isVisible()) {
        fadeTo(0.0, kFadeOutDurationMs);
    }
}

void CanvasMetricLabelOverlay::hideImmediately()
{
    m_fadeAnimation->stop();
    m_opacityEffect->setOpacity(0.0);
    hide();
}

void CanvasMetricLabelOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    auto& style = ruwa::ui::core::WidgetStyleManager::instance();
    QColor background = style.colors().surface;
    background.setAlpha(215);
    QColor borderTop = style.colors().borderLight();
    borderTop.setAlpha(95);
    QColor borderBottom = style.colors().borderDark();
    borderBottom.setAlpha(95);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds(rect());
    const qreal radius = qMax(0.0, bounds.height() * 0.5 - 0.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(
        bounds.adjusted(1.0, 1.0, -1.0, -1.0), qMax(0.0, radius - 1.0), qMax(0.0, radius - 1.0));
    ruwa::ui::painting::drawGradientBorder(
        painter, bounds.adjusted(0.5, 0.5, -0.5, -0.5), radius, borderTop, borderBottom);
}

void CanvasMetricLabelOverlay::applyTheme()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    if (auto* box = qobject_cast<QHBoxLayout*>(layout())) {
        const int horizontal = theme.scaled(kHorizontalPaddingBase);
        const int vertical = theme.scaled(kVerticalPaddingBase);
        box->setContentsMargins(horizontal, vertical, horizontal, vertical);
    }
    QFont font = colors.fonts.getUIFont(theme.scaledFontSize(10));
    font.setWeight(QFont::DemiBold);
    m_label->setFont(font);
    m_label->setMinimumWidth(theme.scaled(kMinimumWidthBase));
    m_label->setStyleSheet(QStringLiteral(
        "QLabel#canvasMetricLabel { background: transparent; color: rgb(%1, %2, %3); }")
            .arg(colors.text.red())
            .arg(colors.text.green())
            .arg(colors.text.blue()));
    adjustSize();
    update();
}

void CanvasMetricLabelOverlay::fadeTo(qreal opacity, int durationMs)
{
    if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
        if (qFuzzyCompare(m_fadeAnimation->endValue().toReal(), opacity)) {
            return;
        }
        m_fadeAnimation->stop();
    }
    if (qFuzzyCompare(m_opacityEffect->opacity(), opacity)) {
        return;
    }
    m_fadeAnimation->setDuration(durationMs);
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(opacity);
    m_fadeAnimation->start();
}

} // namespace ruwa::ui::widgets
