// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   Z O O M   I N F O   O V E R L A Y
// ==========================================================================

#include "CanvasZoomInfoOverlay.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyleManager.h"

#include <QAbstractAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QSizePolicy>
#include <QTimer>
#include <QVariant>

#include <algorithm>

namespace ruwa::ui::widgets {

namespace {

constexpr int kHorizontalPaddingBase = 13;
constexpr int kVerticalPaddingBase = 5;
constexpr int kMinimumWidthBase = 54;

} // namespace

CanvasZoomInfoOverlay::CanvasZoomInfoOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_label = new QLabel(this);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setObjectName(QStringLiteral("canvasZoomInfoLabel"));

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

    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(kHideDelayMs);
    connect(m_hideTimer, &QTimer::timeout, this, [this]() { fadeTo(0.0, kFadeOutDurationMs); });

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() { applyTheme(); });

    applyTheme();
    hide();
}

CanvasZoomInfoOverlay::~CanvasZoomInfoOverlay() = default;

void CanvasZoomInfoOverlay::showZoom(qreal zoom)
{
    setZoom(zoom);
    updateAnchorPosition();
    show();
    raise();
    const bool fadingIn = m_fadeAnimation && m_fadeAnimation->state() == QAbstractAnimation::Running
        && qFuzzyCompare(m_fadeAnimation->endValue().toReal(), 1.0);
    const bool fadingAway = m_fadeAnimation
        && m_fadeAnimation->state() == QAbstractAnimation::Running
        && !qFuzzyCompare(m_fadeAnimation->endValue().toReal(), 1.0);
    if (!fadingIn && (fadingAway || !m_opacityEffect || m_opacityEffect->opacity() < 1.0)) {
        fadeTo(1.0, kFadeInDurationMs);
    }
    if (m_hideTimer) {
        m_hideTimer->start();
    }
}

void CanvasZoomInfoOverlay::setZoom(qreal zoom)
{
    if (m_label) {
        m_label->setText(zoomText(zoom));
    }
    adjustSize();
    updateAnchorPosition();
}

bool CanvasZoomInfoOverlay::isPresentationActive() const
{
    return isVisible() || (m_hideTimer && m_hideTimer->isActive())
        || (m_opacityEffect && m_opacityEffect->opacity() > 0.0);
}

void CanvasZoomInfoOverlay::updateAnchorPosition()
{
    QWidget* parent = parentWidget();
    if (!parent) {
        return;
    }

    const int margin = ruwa::ui::core::ThemeManager::instance().scaled(6);
    const int x = qBound(
        margin, (parent->width() - width()) / 2, qMax(margin, parent->width() - width() - margin));
    const int y = margin;
    move(qMax(0, x), qMax(0, y));
}

void CanvasZoomInfoOverlay::hideImmediately()
{
    if (m_hideTimer) {
        m_hideTimer->stop();
    }
    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
    }
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(0.0);
    }
    hide();
}

void CanvasZoomInfoOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    auto& style = ruwa::ui::core::WidgetStyleManager::instance();

    QColor bgColor = style.colors().surface;
    bgColor.setAlpha(215);
    QColor borderTopColor = style.colors().borderLight();
    borderTopColor.setAlpha(95);
    QColor borderBottomColor = style.colors().borderDark();
    borderBottomColor.setAlpha(95);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Capsule (pill) shape: radius tracks half the height so the ends are fully
    // rounded, matching HexColorInput / CapsuleButton across the app.
    const QRectF r(rect());
    const qreal pillR = qMax(0.0, r.height() * 0.5 - 0.5);
    const QRectF fillRect = r.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal fillR = qMax(0.0, pillR - 1.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(fillRect, fillR, fillR);

    ruwa::ui::painting::drawGradientBorder(
        painter, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTopColor, borderBottomColor);
}

void CanvasZoomInfoOverlay::applyTheme()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int horizontalPadding = theme.scaled(kHorizontalPaddingBase);
    const int verticalPadding = theme.scaled(kVerticalPaddingBase);

    if (auto* boxLayout = qobject_cast<QHBoxLayout*>(layout())) {
        boxLayout->setContentsMargins(
            horizontalPadding, verticalPadding, horizontalPadding, verticalPadding);
    }

    if (m_label) {
        QFont font = colors.fonts.getUIFont(theme.scaledFontSize(10));
        font.setWeight(QFont::DemiBold);
        m_label->setFont(font);
        m_label->setMinimumWidth(theme.scaled(kMinimumWidthBase));
        m_label->setStyleSheet(QStringLiteral(
            "QLabel#canvasZoomInfoLabel { background: transparent; color: rgb(%1, %2, %3); }")
                .arg(colors.text.red())
                .arg(colors.text.green())
                .arg(colors.text.blue()));
    }

    adjustSize();
    updateAnchorPosition();
    update();
}

void CanvasZoomInfoOverlay::fadeTo(qreal opacity, int durationMs)
{
    if (!m_opacityEffect || !m_fadeAnimation) {
        return;
    }

    if (m_fadeAnimation->state() == QAbstractAnimation::Running) {
        m_fadeAnimation->stop();
    }

    m_fadeAnimation->setDuration(durationMs);
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(opacity);
    m_fadeAnimation->start();
}

QString CanvasZoomInfoOverlay::zoomText(qreal zoom) const
{
    const int percent = qMax(1, qRound(std::max<qreal>(0.0, zoom) * 100.0));
    return QStringLiteral("%1%").arg(percent);
}

} // namespace ruwa::ui::widgets
