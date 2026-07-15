// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S E L E C T I O N   S I Z E   O V E R L A Y
// ==========================================================================

#include "CanvasSelectionSizeOverlay.h"

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

#include <algorithm>

namespace ruwa::ui::widgets {

namespace {

constexpr int kHorizontalPaddingBase = 13;
constexpr int kVerticalPaddingBase = 5;
constexpr int kMinimumWidthBase = 62;
constexpr int kAnchorGapBase = 8;
constexpr int kEdgeMarginBase = 6;

} // namespace

CanvasSelectionSizeOverlay::CanvasSelectionSizeOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_label = new QLabel(this);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setObjectName(QStringLiteral("canvasSelectionSizeLabel"));

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

CanvasSelectionSizeOverlay::~CanvasSelectionSizeOverlay() = default;

void CanvasSelectionSizeOverlay::present(
    int widthPx, int heightPx, const QRectF& selectionRectPanel)
{
    if (m_label) {
        m_label->setText(
            QStringLiteral("%1 × %2").arg(std::max(0, widthPx)).arg(std::max(0, heightPx)));
    }
    adjustSize();
    moveNearRect(selectionRectPanel);

    show();
    raise();

    const bool fadingIn = m_fadeAnimation && m_fadeAnimation->state() == QAbstractAnimation::Running
        && qFuzzyCompare(m_fadeAnimation->endValue().toReal(), 1.0);
    if (!fadingIn && (!m_opacityEffect || m_opacityEffect->opacity() < 1.0)) {
        fadeTo(1.0, kFadeInDurationMs);
    }
}

void CanvasSelectionSizeOverlay::dismiss()
{
    if (!isVisible()) {
        return;
    }
    fadeTo(0.0, kFadeOutDurationMs);
}

void CanvasSelectionSizeOverlay::hideImmediately()
{
    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
    }
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(0.0);
    }
    hide();
}

void CanvasSelectionSizeOverlay::moveNearRect(const QRectF& selectionRectPanel)
{
    QWidget* parent = parentWidget();
    if (!parent) {
        return;
    }

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(kAnchorGapBase);
    const int margin = theme.scaled(kEdgeMarginBase);

    // Anchor to the selection's bottom-right corner: right edge aligned, sitting
    // just below the rectangle. Flip above the corner when it would clip the
    // bottom of the canvas, then clamp fully inside the content widget.
    int x = qRound(selectionRectPanel.right()) - width();
    int y = qRound(selectionRectPanel.bottom()) + gap;
    if (y + height() + margin > parent->height()) {
        y = qRound(selectionRectPanel.top()) - height() - gap;
    }

    x = qBound(margin, x, qMax(margin, parent->width() - width() - margin));
    y = qBound(margin, y, qMax(margin, parent->height() - height() - margin));
    move(x, y);
}

void CanvasSelectionSizeOverlay::paintEvent(QPaintEvent* event)
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

    // Capsule (pill) shape, matching CanvasZoomInfoOverlay / HexColorInput.
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

void CanvasSelectionSizeOverlay::applyTheme()
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
            "QLabel#canvasSelectionSizeLabel { background: transparent; color: rgb(%1, %2, %3); }")
                .arg(colors.text.red())
                .arg(colors.text.green())
                .arg(colors.text.blue()));
    }

    adjustSize();
    update();
}

void CanvasSelectionSizeOverlay::fadeTo(qreal opacity, int durationMs)
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

} // namespace ruwa::ui::widgets
