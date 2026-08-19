// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/CanvasMetricLabelOverlay.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/AnimationPolicy.h"
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
#include <QFontMetrics>
#include <QImage>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QSizePolicy>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {
namespace {
constexpr int kHorizontalPaddingBase = 13;
constexpr int kVerticalPaddingBase = 5;
constexpr int kMinimumWidthBase = 62;
constexpr int kAnchorGapBase = 8;
/// Clear of the cursor glyph itself, so the capsule never sits under the pointer.
constexpr int kCursorGapBase = 22;
constexpr int kEdgeMarginBase = 6;
constexpr int kFadeInDurationMs = 90;
constexpr int kFadeOutDurationMs = 120;
/// Icon box side, tuned against the 10pt DemiBold value next to it.
constexpr int kIconSizeBase = 14;
constexpr int kIconTextGapBase = 5;
constexpr int kSegmentGapBase = 12;
} // namespace

CanvasMetricLabelOverlay::CanvasMetricLabelOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    // One empty text segment: applyTheme() below builds the row of labels.
    m_segments.append(MetricSegment {});

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

void CanvasMetricLabelOverlay::setSegments(const QList<MetricSegment>& segments)
{
    m_segments = segments;
    ensureSegmentCount(static_cast<int>(m_segments.size()));
    for (int i = 0; i < m_segments.size(); ++i) {
        applySegment(m_segmentWidgets[static_cast<size_t>(i)], m_segments[i]);
    }
    adjustSize();
}

void CanvasMetricLabelOverlay::ensureSegmentCount(int count)
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    auto* box = qobject_cast<QHBoxLayout*>(layout());
    if (!box) {
        box = new QHBoxLayout(this);
        box->setSpacing(theme.scaled(kSegmentGapBase));
        const int horizontal = theme.scaled(kHorizontalPaddingBase);
        const int vertical = theme.scaled(kVerticalPaddingBase);
        box->setContentsMargins(horizontal, vertical, horizontal, vertical);
    }

    while (static_cast<int>(m_segmentWidgets.size()) < count) {
        SegmentWidgets widgets;
        // One container per segment: hiding it drops both its labels and the
        // gap around them out of the layout in a single step.
        widgets.container = new QWidget(this);
        widgets.container->setAttribute(Qt::WA_TransparentForMouseEvents);
        // Every widget inside the capsule must opt out of a background
        // explicitly: an app-wide stylesheet is active, so QStyleSheetStyle
        // paints the palette background for anything that does not say
        // otherwise — including a plain container QWidget.
        widgets.container->setObjectName(QStringLiteral("canvasMetricSegment"));
        widgets.container->setStyleSheet(
            QStringLiteral("QWidget#canvasMetricSegment { background: transparent; }"));
        widgets.container->setAttribute(Qt::WA_TranslucentBackground);
        widgets.container->setAutoFillBackground(false);
        auto* inner = new QHBoxLayout(widgets.container);
        inner->setContentsMargins(0, 0, 0, 0);
        inner->setSpacing(theme.scaled(kIconTextGapBase));

        widgets.icon = new QLabel(widgets.container);
        widgets.icon->setAttribute(Qt::WA_TransparentForMouseEvents);
        widgets.icon->setObjectName(QStringLiteral("canvasMetricIcon"));
        widgets.icon->setStyleSheet(
            QStringLiteral("QLabel#canvasMetricIcon { background: transparent; }"));
        widgets.icon->setAttribute(Qt::WA_TranslucentBackground);
        widgets.icon->setAutoFillBackground(false);
        widgets.icon->hide();
        inner->addWidget(widgets.icon);

        widgets.text = new QLabel(widgets.container);
        widgets.text->setAttribute(Qt::WA_TransparentForMouseEvents);
        widgets.text->setAlignment(Qt::AlignCenter);
        widgets.text->setObjectName(QStringLiteral("canvasMetricLabel"));
        widgets.text->setAttribute(Qt::WA_TranslucentBackground);
        widgets.text->setAutoFillBackground(false);
        inner->addWidget(widgets.text);

        box->addWidget(widgets.container);
        applySegmentStyling(widgets);
        m_segmentWidgets.push_back(widgets);
    }

    for (size_t i = 0; i < m_segmentWidgets.size(); ++i) {
        m_segmentWidgets[i].container->setVisible(static_cast<int>(i) < count);
    }
}

void CanvasMetricLabelOverlay::applySegmentStyling(SegmentWidgets& widgets)
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    if (auto* inner = qobject_cast<QHBoxLayout*>(widgets.container->layout())) {
        inner->setSpacing(theme.scaled(kIconTextGapBase));
    }
    widgets.text->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Label, QFont::DemiBold));
    widgets.text->setStyleSheet(
        QStringLiteral("QLabel#canvasMetricLabel { background: transparent; color: rgb(%1, %2, "
                       "%3); }")
            .arg(colors.text.red())
            .arg(colors.text.green())
            .arg(colors.text.blue()));
}

void CanvasMetricLabelOverlay::applySegment(SegmentWidgets& widgets, const MetricSegment& segment)
{
    const QString iconKey = segment.iconResource.isEmpty()
        ? QString()
        : segment.iconResource + (segment.mirrorHorizontally ? QStringLiteral("|h") : QString())
            + (segment.mirrorVertically ? QStringLiteral("|v") : QString());
    if (iconKey != widgets.iconKey) {
        widgets.iconKey = iconKey;
        const QPixmap icon = themedIconPixmap(segment);
        widgets.icon->setPixmap(icon);
        widgets.icon->setVisible(!icon.isNull());
    }

    widgets.text->setText(segment.text);
    widgets.text->setMinimumWidth(segment.widthTemplate.isEmpty()
            ? 0
            : widgets.text->fontMetrics().horizontalAdvance(segment.widthTemplate));
}

QPixmap CanvasMetricLabelOverlay::themedIconPixmap(const MetricSegment& segment) const
{
    if (segment.iconResource.isEmpty()) {
        return {};
    }
    QImage source(segment.iconResource);
    if (source.isNull()) {
        return {};
    }
    Qt::Orientations flips;
    if (segment.mirrorHorizontally) {
        flips |= Qt::Horizontal;
    }
    if (segment.mirrorVertically) {
        flips |= Qt::Vertical;
    }
    if (flips) {
        source = source.flipped(flips);
    }

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const qreal dpr = devicePixelRatioF();
    const int side = qMax(1, qRound(theme.scaled(kIconSizeBase) * dpr));
    // Composed in an explicitly ARGB image: a QPixmap takes the window system's
    // backing format, which need not carry alpha — and a SourceIn tint against
    // an opaque destination floods the whole icon box with the tint colour.
    QImage result(source.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_ARGB32_Premultiplied));
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), theme.colors().text);
    painter.end();

    QPixmap pixmap = QPixmap::fromImage(result);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

void CanvasMetricLabelOverlay::presentAtPoint(const QString& text, const QPointF& anchorPanel)
{
    setSegments({ MetricSegment { QString(), false, false, text } });
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(kAnchorGapBase);
    moveClamped(qRound(anchorPanel.x() - width() * 0.5), qRound(anchorPanel.y()) + gap);
    show();
    raise();
    fadeTo(1.0, kFadeInDurationMs);
}

void CanvasMetricLabelOverlay::presentNearRect(const QString& text, const QRectF& rectPanel)
{
    setSegments({ MetricSegment { QString(), false, false, text } });
    positionNearRect(rectPanel);
    show();
    raise();
    fadeTo(1.0, kFadeInDurationMs);
}

void CanvasMetricLabelOverlay::presentAtCursor(
    const QList<MetricSegment>& segments, const QPointF& cursorPanel)
{
    setSegments(segments);
    positionAtCursor(cursorPanel);
    show();
    raise();
    fadeTo(1.0, kFadeInDurationMs);
}

void CanvasMetricLabelOverlay::refreshNearRect(const QRectF& rectPanel)
{
    if (!isVisible()) {
        return;
    }
    positionNearRect(rectPanel);
}

void CanvasMetricLabelOverlay::refreshAtCursor(const QPointF& cursorPanel)
{
    if (!isVisible()) {
        return;
    }
    positionAtCursor(cursorPanel);
}

void CanvasMetricLabelOverlay::positionNearRect(const QRectF& rectPanel)
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(kAnchorGapBase);
    const int margin = theme.scaled(kEdgeMarginBase);
    int y = qRound(rectPanel.bottom()) + gap;
    if (parentWidget() && y + height() + margin > parentWidget()->height()) {
        y = qRound(rectPanel.top()) - height() - gap;
    }
    moveClamped(qRound(rectPanel.right()) - width(), y);
}

void CanvasMetricLabelOverlay::positionAtCursor(const QPointF& cursorPanel)
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(kCursorGapBase);
    const int margin = theme.scaled(kEdgeMarginBase);
    int x = qRound(cursorPanel.x()) + gap;
    if (parentWidget() && x + width() + margin > parentWidget()->width()) {
        x = qRound(cursorPanel.x()) - gap - width();
    }
    moveClamped(x, qRound(cursorPanel.y()) - height() / 2);
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
    ensureSegmentCount(static_cast<int>(m_segments.size()));

    if (auto* box = qobject_cast<QHBoxLayout*>(layout())) {
        const int horizontal = theme.scaled(kHorizontalPaddingBase);
        const int vertical = theme.scaled(kVerticalPaddingBase);
        box->setContentsMargins(horizontal, vertical, horizontal, vertical);
        box->setSpacing(theme.scaled(kSegmentGapBase));
    }

    for (size_t i = 0; i < m_segmentWidgets.size(); ++i) {
        SegmentWidgets& widgets = m_segmentWidgets[i];
        applySegmentStyling(widgets);
        // Force a re-tint: the icon colour and its size both come from the theme.
        widgets.iconKey.clear();
        if (static_cast<int>(i) < m_segments.size()) {
            applySegment(widgets, m_segments[static_cast<int>(i)]);
        }
    }

    setMinimumWidth(theme.scaled(kMinimumWidthBase));
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
    m_fadeAnimation->setDuration(anim::duration(durationMs));
    m_fadeAnimation->setStartValue(m_opacityEffect->opacity());
    m_fadeAnimation->setEndValue(opacity);
    anim::start(m_fadeAnimation);
}

} // namespace ruwa::ui::widgets
