// SPDX-License-Identifier: MPL-2.0

// ProjectPresetCard.cpp
#include "ProjectPresetCard.h"
#include "shared/style/WidgetStyleManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QSizePolicy>
#include <QWidget>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
// Layout: [ text (name + dims) | gap | aspect preview ]  (base values → scaled)
const int BASE_PREVIEW_COL_WIDTH = 92;
const int BASE_PREVIEW_COL_HEIGHT = 72;
const int BASE_TEXT_PREVIEW_GAP = 12;
const int BASE_NAME_DIM_GAP = 4;
const int BASE_NAME_LINE_HEIGHT = 20;
const int BASE_DIM_LINE_HEIGHT = 16;
const int BASE_PREVIEW_RADIUS = 6;
const int BASE_NAME_FONT_SIZE = 10;
const int BASE_DIM_FONT_SIZE = 9;
const int BASE_MIN_TEXT_WIDTH = 72;
const int BASE_FALLBACK_WIDTH = 200;
const char kNewProjectPresetCtx[] = "ruwa::ui::widgets::NewProjectContent";
} // namespace

void ProjectPresetCard::changeEvent(QEvent* event)
{
    BaseStyledWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        update();
    }
}

ProjectPresetCard::ProjectPresetCard(
    const QString& nameKey, const QSize& dimensions, QWidget* parent)
    : BaseStyledWidget("PresetCard", parent)
    , m_nameKey(nameKey)
    , m_dimensions(dimensions)
{
    setFlat(true);
    // Width comes from FlowLayout (2 columns); PresetCard inherits Card metrics (would fix 210px).
    style().metrics.fixedWidth = false;
    style().metrics.fixedHeight = false;
    applyStyleChanges();
    // BaseStyledWidget::initialize() already ran updateSizeFromStyle() with Card's default
    // fixedWidth/fixedHeight — Qt keeps those min/max until cleared.
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateGeometry();
}

void ProjectPresetCard::drawAspectRatioPreview(QPainter& painter, const QRectF& previewRect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    // Calculate aspect ratio
    qreal aspectRatio = static_cast<qreal>(m_dimensions.width()) / m_dimensions.height();

    // Calculate preview rectangle maintaining aspect ratio
    qreal maxWidth = previewRect.width();
    qreal maxHeight = previewRect.height();
    qreal previewWidth, previewHeight;

    if (aspectRatio > 1.0) {
        previewWidth = maxWidth;
        previewHeight = maxWidth / aspectRatio;
        if (previewHeight > maxHeight) {
            previewHeight = maxHeight;
            previewWidth = maxHeight * aspectRatio;
        }
    } else {
        previewHeight = maxHeight;
        previewWidth = maxHeight * aspectRatio;
        if (previewWidth > maxWidth) {
            previewWidth = maxWidth;
            previewHeight = maxWidth / aspectRatio;
        }
    }

    // Center the preview
    qreal x = previewRect.x() + (previewRect.width() - previewWidth) / 2.0;
    qreal y = previewRect.y() + (previewRect.height() - previewHeight) / 2.0;
    QRectF actualPreview(x, y, previewWidth, previewHeight);

    const int previewRadius = mgr.scaled(BASE_PREVIEW_RADIUS);

    // Preview background - transitions with active state
    QColor inactiveBg = colors.overlay(0.04 + hoverProgress() * 0.06);
    QColor activeBg = ThemeColors::adjustBrightness(colors.background, 1.2);
    QColor previewBg = ThemeColors::interpolate(inactiveBg, activeBg, activeProgress());

    painter.setPen(Qt::NoPen);
    painter.setBrush(previewBg);
    painter.drawRoundedRect(actualPreview, previewRadius, previewRadius);

    // Preview border - transitions with states
    QColor inactiveBorder = ThemeColors::interpolate(
        colors.borderSubtle(), colors.borderSubtleHover(), hoverProgress());
    QColor activeBorder = ThemeColors::adjustBrightness(colors.background, 2.5);
    QColor borderColor = ThemeColors::interpolate(inactiveBorder, activeBorder, activeProgress());

    QPen borderPen(borderColor, 1);
    borderPen.setCosmetic(true);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(
        actualPreview.adjusted(0.5, 0.5, -0.5, -0.5), previewRadius - 0.5, previewRadius - 0.5);
}

int ProjectPresetCard::layoutContentHeightForInnerWidth(int innerWidth) const
{
    Q_UNUSED(innerWidth);
    auto& mgr = WidgetStyleManager::instance();
    const int textStack = mgr.scaled(BASE_NAME_LINE_HEIGHT) + mgr.scaled(BASE_NAME_DIM_GAP)
        + mgr.scaled(BASE_DIM_LINE_HEIGHT);
    return qMax(mgr.scaled(BASE_PREVIEW_COL_HEIGHT), textStack);
}

int ProjectPresetCard::heightForWidth(int w) const
{
    if (w <= 0)
        w = WidgetStyleManager::instance().scaled(BASE_FALLBACK_WIDTH);
    const QMargins pad = contentPadding();
    const int innerH = layoutContentHeightForInnerWidth(w - pad.left() - pad.right());
    return pad.top() + innerH + pad.bottom();
}

QSize ProjectPresetCard::sizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();
    const int w = mgr.scaled(BASE_FALLBACK_WIDTH);
    return QSize(w, heightForWidth(w));
}

QSize ProjectPresetCard::minimumSizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();
    const QMargins pad = contentPadding();
    const int innerW = mgr.scaled(BASE_MIN_TEXT_WIDTH) + mgr.scaled(BASE_TEXT_PREVIEW_GAP)
        + mgr.scaled(BASE_PREVIEW_COL_WIDTH);
    const int w = innerW + pad.left() + pad.right();
    return QSize(w, heightForWidth(w));
}

void ProjectPresetCard::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();
    const QMargins pad = contentPadding();
    QRectF content = rect.adjusted(pad.left(), pad.top(), -pad.right(), -pad.bottom());

    const qreal previewW = mgr.scaled(BASE_PREVIEW_COL_WIDTH);
    const qreal previewH = mgr.scaled(BASE_PREVIEW_COL_HEIGHT);
    const qreal hGap = mgr.scaled(BASE_TEXT_PREVIEW_GAP);
    const qreal nameH = mgr.scaled(BASE_NAME_LINE_HEIGHT);
    const qreal dimH = mgr.scaled(BASE_DIM_LINE_HEIGHT);
    const qreal vGapName = mgr.scaled(BASE_NAME_DIM_GAP);

    const qreal textStackH = nameH + vGapName + dimH;
    const qreal rowH = qMax(previewH, textStackH);
    const qreal rowTop = content.top() + (content.height() - rowH) * 0.5;

    const qreal textW = qMax(0.0, content.width() - previewW - hGap);
    const qreal textLeft = content.left();
    const qreal textTop = rowTop + (rowH - textStackH) * 0.5;

    QRectF previewArea(
        content.right() - previewW, rowTop + (rowH - previewH) * 0.5, previewW, previewH);
    drawAspectRatioPreview(painter, previewArea);

    // Text colors - smooth transitions
    QColor inactiveTextPrimary
        = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress());
    QColor activeTextPrimary = colors.textOnPrimary();
    QColor textPrimary
        = ThemeColors::interpolate(inactiveTextPrimary, activeTextPrimary, activeProgress());

    QColor inactiveTextSecondary = colors.textMuted;
    QColor activeTextSecondary = ThemeColors::adjustBrightness(colors.background, 2.5);
    QColor textSecondary
        = ThemeColors::interpolate(inactiveTextSecondary, activeTextSecondary, activeProgress());

    // Name (left)
    painter.setPen(textPrimary);
    QFont nameFont = painter.font();
    nameFont.setPointSize(mgr.scaledFontSize(BASE_NAME_FONT_SIZE));
    nameFont.setBold(true);
    painter.setFont(nameFont);

    QString displayName;
    {
        const QByteArray k = m_nameKey.toUtf8();
        displayName = QCoreApplication::translate(kNewProjectPresetCtx, k.constData());
    }

    QRectF nameRect(textLeft, textTop, textW, nameH);
    painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, displayName);

    // Dimensions (left)
    painter.setPen(textSecondary);
    QFont dimFont = painter.font();
    dimFont.setPointSize(mgr.scaledFontSize(BASE_DIM_FONT_SIZE));
    dimFont.setBold(false);
    painter.setFont(dimFont);

    QString dimensionsText
        = QString("%1 x %2").arg(m_dimensions.width()).arg(m_dimensions.height());
    QRectF dimRect(textLeft, textTop + nameH + vGapName, textW, dimH);
    painter.drawText(dimRect, Qt::AlignLeft | Qt::AlignVCenter, dimensionsText);
}

} // namespace ruwa::ui::widgets
