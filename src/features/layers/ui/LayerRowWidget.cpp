// SPDX-License-Identifier: MPL-2.0

// LayerRowWidget.cpp
#include "LayerRowWidget.h"

#include "features/layers/ui/LayerListView.h"
#include "features/canvas/rendering/TextRetainedPayloadBuilder.h"
#include "features/layers/model/BlendModeUtils.h"
#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TilePixelAccess.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/DisplayColorPalette.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/theme/manager/ThemeColors.h"

#include <array>
#include <QCoreApplication>
#include <QGraphicsOpacityEffect>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTextOption>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QEnterEvent>
#include <QVariantList>
#include <QVariantMap>
#include <QApplication>
#include <QCursor>
#include <QImage>
#include <QtMath>
#include <cmath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;
using namespace ruwa::core::layers;

namespace {

class RightExpandActionButton final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit RightExpandActionButton(
        ruwa::ui::core::IconProvider::StandardIcon iconType, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_iconType(iconType)
    {
        setMinimumSize(1, 1);
        setHoverDuration(150);
        setActiveDuration(200);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& c = ruwa::ui::core::ThemeManager::instance().colors();
        QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = 4.0;

        // Gradient border
        QLinearGradient borderGrad(r.left(), r.top(), r.right(), r.bottom());
        QColor borderTop = c.border;
        borderTop.setAlphaF(0.5);
        QColor borderBottom = c.border;
        borderBottom.setAlphaF(0.2);
        if (hoverProgress() > 0.0) {
            QColor hTop = c.primary;
            hTop.setAlphaF(0.4);
            QColor hBottom = c.primary;
            hBottom.setAlphaF(0.15);
            borderTop = ruwa::ui::core::ThemeColors::interpolate(borderTop, hTop, hoverProgress());
            borderBottom
                = ruwa::ui::core::ThemeColors::interpolate(borderBottom, hBottom, hoverProgress());
        }
        borderGrad.setColorAt(0.0, borderTop);
        borderGrad.setColorAt(1.0, borderBottom);
        p.setPen(QPen(QBrush(borderGrad), 1.0));
        p.setBrush(c.surfaceAlt);
        p.drawRoundedRect(r, radius, radius);

        // Hover overlay
        if (hoverProgress() > 0.0) {
            QColor hover = c.surfaceHover();
            hover.setAlphaF(hover.alphaF() * hoverProgress());
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius - 0.5, radius - 0.5);
        }
        // Press overlay
        if (isPressed()) {
            QColor press = c.primaryPressed();
            press.setAlphaF(0.35);
            p.setPen(Qt::NoPen);
            p.setBrush(press);
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), radius - 0.5, radius - 0.5);
        }

        // Icon
        QColor iconColor
            = ruwa::ui::core::ThemeColors::interpolate(c.textMuted, c.text, hoverProgress());
        const int iconSz = qMax(12, ruwa::ui::core::ThemeManager::instance().scaled(14));
        QPixmap icon
            = ruwa::ui::core::IconProvider::instance().getPixmap(m_iconType, QSize(iconSz, iconSz));
        if (!icon.isNull()) {
            QPixmap tinted(icon.size());
            tinted.fill(Qt::transparent);
            QPainter ip(&tinted);
            ip.drawPixmap(0, 0, icon);
            ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
            ip.fillRect(tinted.rect(), iconColor);
            ip.end();
            const int dx = (width() - iconSz) / 2;
            const int dy = (height() - iconSz) / 2;
            p.drawPixmap(dx, dy, tinted);
        }
    }

private:
    ruwa::ui::core::IconProvider::StandardIcon m_iconType;
};

constexpr float kMinThumbAspect = 9.0f / 21.0f;
constexpr float kMaxThumbAspect = 21.0f / 9.0f;
constexpr float kContentAwareThumbAspect = 16.0f / 9.0f;

QColor layerRowAccentColor(const LayerData* data)
{
    if (!data) {
        return {};
    }

    const int colorIndex = qBound(0, static_cast<int>(data->displayColorIndex),
        static_cast<int>(LayerData::kMaxDisplayColorIndex));
    if (colorIndex <= static_cast<int>(LayerData::kBaseDisplayColorIndex)) {
        return {};
    }

    return displayAccentColor(colorIndex);
}

QString blendModeName(BlendMode mode)
{
    const char* ctx = "ruwa::ui::widgets::LayerRowWidget";
    return blendModeDisplayName(mode, ctx);
}

bool usesContentAwareThumbnailFrame(const QRect& displayFrame)
{
    return displayFrame.width() <= 0 || displayFrame.height() <= 0;
}

QRect rectFromAetherRect(const aether::Rect& rect)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return {};
    }

    const int left = static_cast<int>(std::floor(rect.x));
    const int top = static_cast<int>(std::floor(rect.y));
    const int right = static_cast<int>(std::ceil(rect.x + rect.width));
    const int bottom = static_cast<int>(std::ceil(rect.y + rect.height));
    return QRect(left, top, qMax(1, right - left), qMax(1, bottom - top));
}

QRect expandFrameToAspect(const QRect& frame, float aspect)
{
    if (frame.width() <= 0 || frame.height() <= 0 || aspect <= 0.0f) {
        return frame;
    }

    const QRectF src(frame);
    const QPointF center = src.center();
    qreal width = src.width();
    qreal height = src.height();
    const qreal currentAspect = width / height;

    if (currentAspect < aspect) {
        width = height * aspect;
    } else {
        height = width / aspect;
    }

    const qreal left = center.x() - width * 0.5;
    const qreal top = center.y() - height * 0.5;
    const int roundedLeft = static_cast<int>(std::floor(left));
    const int roundedTop = static_cast<int>(std::floor(top));
    const int roundedRight = static_cast<int>(std::ceil(left + width));
    const int roundedBottom = static_cast<int>(std::ceil(top + height));
    return QRect(roundedLeft, roundedTop, qMax(1, roundedRight - roundedLeft),
        qMax(1, roundedBottom - roundedTop));
}

QRect effectiveContentFrame(const LayerData* data)
{
    if (!data) {
        return {};
    }

    if (data->isText() && data->textData) {
        aether::TransformState state = data->textData->transform;
        if (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f) {
            state.contentBounds = aether::computeTextLayoutSourceBounds(*data->textData);
            state.pivot = state.contentBounds.center();
        }
        const aether::Rect transformedBounds
            = state.isIdentity() ? state.contentBounds : state.transformedAABB();
        return rectFromAetherRect(transformedBounds);
    }

    if (const aether::TileGrid* grid = data->pixelGrid(); grid && !grid->empty()) {
        if (data->isIsolatedPixelLayer()) {
            aether::TransformState state = data->smartTransform;
            if (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f) {
                state.contentBounds = aether::TransformState::computeContentBounds(*grid);
                state.pivot = state.contentBounds.center();
            }

            if (data->isBoard()) {
                return rectFromAetherRect(state.contentBounds);
            }

            const aether::Rect transformedBounds
                = state.isIdentity() ? state.contentBounds : state.transformedAABB();
            return rectFromAetherRect(transformedBounds);
        }

        return rectFromAetherRect(aether::TransformState::computeContentBounds(*grid));
    }

    if (data->isIsolatedPixelLayer()) {
        const auto& bounds = data->smartTransform.contentBounds;
        if (bounds.width > 0.0f && bounds.height > 0.0f) {
            if (data->isBoard()) {
                return rectFromAetherRect(bounds);
            }
            const aether::Rect transformedBounds = data->smartTransform.isIdentity()
                ? bounds
                : data->smartTransform.transformedAABB();
            return rectFromAetherRect(transformedBounds);
        }
    }

    return {};
}

QRect resolvedPreviewFrame(const LayerData* data, const QRect& displayFrame)
{
    if (displayFrame.width() > 0 && displayFrame.height() > 0) {
        return displayFrame;
    }

    const QRect contentFrame = effectiveContentFrame(data);
    if (!contentFrame.isEmpty()) {
        return expandFrameToAspect(contentFrame, kContentAwareThumbAspect);
    }

    return QRect(0, 0, 512, qMax(1, qRound(512.0f / kContentAwareThumbAspect)));
}

float thumbnailAspectRatio(const LayerData* data, const QRect& displayFrame)
{
    if (usesContentAwareThumbnailFrame(displayFrame)) {
        return kContentAwareThumbAspect;
    }

    float aspect = 1.0f;
    const QRect previewFrame = resolvedPreviewFrame(data, displayFrame);
    if (previewFrame.width() > 0 && previewFrame.height() > 0) {
        aspect
            = static_cast<float>(previewFrame.width()) / static_cast<float>(previewFrame.height());
    } else if (data && !data->thumbnail.isNull() && data->thumbnail.height() > 0) {
        aspect = static_cast<float>(data->thumbnail.width())
            / static_cast<float>(data->thumbnail.height());
    }
    return qBound(kMinThumbAspect, aspect, kMaxThumbAspect);
}

qreal hiddenBlend(qreal visibleProgress, qreal hiddenFactor)
{
    return hiddenFactor + (1.0 - hiddenFactor) * qBound<qreal>(0.0, visibleProgress, 1.0);
}

int selectionIndicatorWidthPx()
{
    return qMax(3, ThemeManager::instance().scaled(3));
}

int selectionIndicatorInsetPx()
{
    return qMax(4, ThemeManager::instance().scaled(6));
}

int selectionShiftMaxPx(qreal clipIndicatorProgress)
{
    int shift = selectionIndicatorInsetPx() + selectionIndicatorWidthPx()
        - ThemeManager::instance().scaled(LayerRowWidget::kPad);
    if (clipIndicatorProgress > 0.0) {
        shift += qRound(ThemeManager::instance().scaled(LayerRowWidget::kGap)
            * qBound<qreal>(0.0, clipIndicatorProgress, 1.0));
    }
    return qMax(0, shift);
}

bool isEffectivelyVisible(const LayerData* data)
{
    if (!data || !data->visible) {
        return false;
    }

    for (const LayerData* current = data->parent; current; current = current->parent) {
        if (!current->visible) {
            return false;
        }
    }
    return true;
}

struct PremultipliedColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

aether::TransformState thumbnailTransformState(const LayerData* data, const QRect& previewFrame)
{
    aether::TransformState state = data ? data->smartTransform : aether::TransformState {};
    if (!data || !data->isIsolatedPixelLayer()) {
        return state;
    }

    const aether::TileGrid* grid = data->pixelGrid();
    if (grid && (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f)) {
        state.contentBounds = aether::TransformState::computeContentBounds(*grid);
        state.pivot = state.contentBounds.center();
    }

    if (!data->isBoard()) {
        return state;
    }

    state.reset();
    state.pivot = state.contentBounds.center();
    state.translation.x = static_cast<float>(previewFrame.center().x()) + 0.5f - state.pivot.x;
    state.translation.y = static_cast<float>(previewFrame.center().y()) + 0.5f - state.pivot.y;
    return state;
}

PremultipliedColor sampleLayerPixel(const LayerData* data, int x, int y, const QRect& previewFrame)
{
    PremultipliedColor out;
    if (!data) {
        return out;
    }

    const aether::TileGrid* grid = data->pixelGrid();
    if (grid) {
        int sampleX = x;
        int sampleY = y;

        const aether::TransformState state = thumbnailTransformState(data, previewFrame);
        if (data->isIsolatedPixelLayer() && !state.isIdentity()) {
            aether::Vector2 src;
            if (!state.tryInverseTransformPoint(
                    { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f }, src)) {
                return out;
            }
            sampleX = static_cast<int>(std::floor(src.x));
            sampleY = static_cast<int>(std::floor(src.y));
        }

        const int tileX = static_cast<int>(
            std::floor(static_cast<float>(sampleX) / static_cast<float>(aether::TILE_SIZE)));
        const int tileY = static_cast<int>(
            std::floor(static_cast<float>(sampleY) / static_cast<float>(aether::TILE_SIZE)));
        const int localX = sampleX - tileX * static_cast<int>(aether::TILE_SIZE);
        const int localY = sampleY - tileY * static_cast<int>(aether::TILE_SIZE);

        if (localX >= 0 && localY >= 0 && localX < static_cast<int>(aether::TILE_SIZE)
            && localY < static_cast<int>(aether::TILE_SIZE)) {
            const aether::TileData* tile = grid->getTile(aether::TileKey { tileX, tileY });
            if (tile) {
                // CONTENT grids follow the document format (RGBA8/16F/32F); read
                // via the format-aware accessor and quantize to the 8-bit premult
                // thumbnail. Solid tiles keep no pixel buffer, so the const
                // accessor would read zeros — take their packed color directly.
                float f[4];
                if (tile->isSolid()) {
                    uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
                    tile->solidColor(sr, sg, sb, sa);
                    f[0] = static_cast<float>(sr) / 255.0f;
                    f[1] = static_cast<float>(sg) / 255.0f;
                    f[2] = static_cast<float>(sb) / 255.0f;
                    f[3] = static_cast<float>(sa) / 255.0f;
                } else {
                    aether::readTilePixelF(
                        *tile, static_cast<uint32_t>(localX), static_cast<uint32_t>(localY), f);
                }
                out.r = f[0];
                out.g = f[1];
                out.b = f[2];
                out.a = f[3];
            }
        }
    }

    // Background is a non-raster logical layer. If it has no explicit pixel payload yet,
    // use its configured fill so thumbnail still reflects actual layer output.
    if (data->isBackground() && out.a <= 0.0f && !data->backgroundTransparent) {
        const QColor bg = data->backgroundColor;
        const float a = static_cast<float>(bg.alphaF());
        out.a = a;
        out.r = static_cast<float>(bg.redF()) * a;
        out.g = static_cast<float>(bg.greenF()) * a;
        out.b = static_cast<float>(bg.blueF()) * a;
    }

    const float opacity = qBound(0.0f, static_cast<float>(data->opacity), 1.0f);
    out.r *= opacity;
    out.g *= opacity;
    out.b *= opacity;
    out.a *= opacity;
    return out;
}

aether::TransformState normalizedTextTransform(const LayerData* data)
{
    aether::TransformState state
        = data && data->textData ? data->textData->transform : aether::TransformState {};
    if (data && data->textData
        && (state.contentBounds.width <= 0.0f || state.contentBounds.height <= 0.0f)) {
        state.contentBounds = aether::computeTextLayoutSourceBounds(*data->textData);
        state.pivot = state.contentBounds.center();
    }
    return state;
}

Qt::Alignment qtTextAlignment(TextAlignment alignment)
{
    switch (alignment) {
    case TextAlignment::Center:
        return Qt::AlignHCenter | Qt::AlignTop;
    case TextAlignment::Right:
        return Qt::AlignRight | Qt::AlignTop;
    case TextAlignment::Justify:
        return Qt::AlignJustify | Qt::AlignTop;
    case TextAlignment::Left:
        break;
    }
    return Qt::AlignLeft | Qt::AlignTop;
}

QImage textLayerPreviewImage(
    const LayerData* data, const QRect& displayFrame, const QSize& targetSize)
{
    if (!data || !data->textData || targetSize.width() <= 0 || targetSize.height() <= 0
        || resolvedPreviewFrame(data, displayFrame).isEmpty()) {
        return {};
    }

    QImage img(targetSize, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    const QRect previewFrame = resolvedPreviewFrame(data, displayFrame);
    const qreal sx = static_cast<qreal>(targetSize.width()) / qMax(1, previewFrame.width());
    const qreal sy = static_cast<qreal>(targetSize.height()) / qMax(1, previewFrame.height());
    const aether::TransformState state = normalizedTextTransform(data);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setOpacity(qBound<qreal>(0.0, data->opacity, 1.0));

    painter.translate(-static_cast<qreal>(previewFrame.left()) * sx,
        -static_cast<qreal>(previewFrame.top()) * sy);
    painter.scale(sx, sy);

    if (!state.hasFreeQuad() && !state.hasDeformMesh()) {
        painter.translate(state.pivot.x + state.translation.x, state.pivot.y + state.translation.y);
        painter.rotate(qRadiansToDegrees(static_cast<qreal>(state.rotation)));
        painter.scale(state.scale.x, state.scale.y);
        painter.translate(-state.pivot.x, -state.pivot.y);
    }

    QFont font(data->textData->fontFamily);
    font.setPointSizeF(qMax<qreal>(1.0, data->textData->fontSize));
    painter.setFont(font);
    painter.setPen(data->textData->color);

    QTextOption option(qtTextAlignment(data->textData->alignment));
    option.setWrapMode(QTextOption::WordWrap);

    const QString previewText
        = data->textData->text.isEmpty() ? QStringLiteral("Text") : data->textData->text;
    const aether::Rect& bounds = state.contentBounds;
    painter.drawText(QRectF(bounds.x, bounds.y, bounds.width, bounds.height), previewText, option);
    return img;
}

QImage layerPreviewImage(const LayerData* data, const QRect& displayFrame, const QSize& targetSize)
{
    if (!data || targetSize.width() <= 0 || targetSize.height() <= 0
        || resolvedPreviewFrame(data, displayFrame).isEmpty()) {
        return {};
    }

    if (data->isText()) {
        return textLayerPreviewImage(data, displayFrame, targetSize);
    }

    QImage img(targetSize, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    const QRect previewFrame = resolvedPreviewFrame(data, displayFrame);
    const int canvasW = previewFrame.width();
    const int canvasH = previewFrame.height();

    for (int y = 0; y < targetSize.height(); ++y) {
        const int srcY
            = previewFrame.top() + qBound(0, (y * canvasH) / targetSize.height(), canvasH - 1);

        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < targetSize.width(); ++x) {
            const int srcX
                = previewFrame.left() + qBound(0, (x * canvasW) / targetSize.width(), canvasW - 1);
            const PremultipliedColor premult = sampleLayerPixel(data, srcX, srcY, previewFrame);
            if (premult.a <= 0.0f) {
                row[x] = qRgba(0, 0, 0, 0);
                continue;
            }

            const int a = qBound(0, static_cast<int>(qRound(premult.a * 255.0f)), 255);
            const int r
                = qBound(0, static_cast<int>(qRound((premult.r / premult.a) * 255.0f)), 255);
            const int g
                = qBound(0, static_cast<int>(qRound((premult.g / premult.a) * 255.0f)), 255);
            const int b
                = qBound(0, static_cast<int>(qRound((premult.b / premult.a) * 255.0f)), 255);
            row[x] = qRgba(r, g, b, a);
        }
    }

    return img;
}

// Render a layer's mask as a grayscale "reveal" preview (white = visible,
// black = hidden), matching the compositor: reveal = lum(premult rgb) + (1 - a).
QImage maskPreviewImage(const LayerData* data, const QRect& displayFrame, const QSize& targetSize)
{
    if (!data || !data->hasMask() || targetSize.width() <= 0 || targetSize.height() <= 0
        || resolvedPreviewFrame(data, displayFrame).isEmpty()) {
        return {};
    }

    const aether::TileGrid* mask = data->maskTileGrid();
    const QRect previewFrame = resolvedPreviewFrame(data, displayFrame);
    const int canvasW = previewFrame.width();
    const int canvasH = previewFrame.height();
    const int tileSize = static_cast<int>(aether::TILE_SIZE);

    // Reveal of every absent tile = the grid's default-fill background (e.g. a
    // hide-all mask shows a black thumbnail base). Transparent default → 1.0.
    float defaultReveal = 1.0f;
    if (mask) {
        uint8_t dr = 0, dg = 0, db = 0, da = 0;
        mask->defaultFill(dr, dg, db, da);
        const float dlum = (0.299f * dr + 0.587f * dg + 0.114f * db) / 255.0f;
        defaultReveal = qBound(0.0f, dlum + (1.0f - static_cast<float>(da) / 255.0f), 1.0f);
    }

    QImage img(targetSize, QImage::Format_ARGB32);
    for (int y = 0; y < targetSize.height(); ++y) {
        const int srcY
            = previewFrame.top() + qBound(0, (y * canvasH) / targetSize.height(), canvasH - 1);
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < targetSize.width(); ++x) {
            const int srcX
                = previewFrame.left() + qBound(0, (x * canvasW) / targetSize.width(), canvasW - 1);

            float reveal = defaultReveal; // absent tile → grid background reveal
            if (mask) {
                const int tileX = static_cast<int>(std::floor(static_cast<float>(srcX) / tileSize));
                const int tileY = static_cast<int>(std::floor(static_cast<float>(srcY) / tileSize));
                const int localX = srcX - tileX * tileSize;
                const int localY = srcY - tileY * tileSize;
                const aether::TileData* tile = mask->getTile(aether::TileKey { tileX, tileY });
                if (tile && localX >= 0 && localY >= 0 && localX < tileSize && localY < tileSize) {
                    uint8_t pr = 0, pg = 0, pb = 0, a = 0;
                    tile->getPixel(static_cast<uint32_t>(localX), static_cast<uint32_t>(localY), pr,
                        pg, pb, a);
                    const float lum = (0.299f * pr + 0.587f * pg + 0.114f * pb) / 255.0f;
                    reveal = qBound(0.0f, lum + (1.0f - static_cast<float>(a) / 255.0f), 1.0f);
                }
            }
            const int v = qBound(0, static_cast<int>(qRound(reveal * 255.0f)), 255);
            row[x] = qRgba(v, v, v, 255);
        }
    }
    return img;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

LayerRowWidget::LayerRowWidget(QWidget* parent)
    : ReorderableRowWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    // Height will be set in setLayerData based on type
    setFixedHeight(kRowHeight);

    m_hoverAnim = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnim->setDuration(180);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_selectionAnim = new QPropertyAnimation(this, "selectionProgress", this);
    m_selectionAnim->setDuration(200);
    m_selectionAnim->setEasingCurve(QEasingCurve::InOutCubic);

    m_primaryAnim = new QPropertyAnimation(this, "primaryProgress", this);
    m_primaryAnim->setDuration(200);
    m_primaryAnim->setEasingCurve(QEasingCurve::InOutCubic);

    m_expandAnim = new QPropertyAnimation(this, "expandRotation", this);
    m_expandAnim->setDuration(200);
    m_expandAnim->setEasingCurve(QEasingCurve::InOutCubic);

    m_opacityAnim = new QPropertyAnimation(this, "visibilityProgress", this);
    m_opacityAnim->setDuration(250);
    m_opacityAnim->setEasingCurve(QEasingCurve::InOutCubic);

    m_effectiveOpacityAnim = new QPropertyAnimation(this, "effectiveVisibilityProgress", this);
    m_effectiveOpacityAnim->setDuration(250);
    m_effectiveOpacityAnim->setEasingCurve(QEasingCurve::InOutCubic);

    m_clipOffsetAnim = new QPropertyAnimation(this, "clipOffsetProgress", this);
    m_clipOffsetAnim->setDuration(200);
    m_clipOffsetAnim->setEasingCurve(QEasingCurve::InOutCubic);

    m_thumbnailCtrlGlowAnim = new QPropertyAnimation(this, "thumbnailCtrlGlowProgress", this);
    m_thumbnailCtrlGlowAnim->setDuration(170);
    m_thumbnailCtrlGlowAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_thumbnailClickFlashAnim = new QPropertyAnimation(this, "thumbnailClickFlashProgress", this);
    m_thumbnailClickFlashAnim->setDuration(150);
    m_thumbnailClickFlashAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_childDisclosureAnim = new QPropertyAnimation(this, "childDisclosureProgress", this);
    m_childDisclosureAnim->setDuration(200);
    m_childDisclosureAnim->setEasingCurve(QEasingCurve::InOutCubic);

    m_maskRevealAnim = new QPropertyAnimation(this, "maskRevealProgress", this);
    m_maskRevealAnim->setDuration(220);
    m_maskRevealAnim->setEasingCurve(QEasingCurve::InOutCubic);

    using Icon = ruwa::ui::core::IconProvider::StandardIcon;
    m_rightExpandBtn1 = new RightExpandActionButton(Icon::Edit, this);
    m_rightExpandBtn2 = new RightExpandActionButton(Icon::Duplicate, this);
    m_rightExpandBtn3 = new RightExpandActionButton(Icon::Trash, this);
    if (m_rightExpandBtn1) {
        m_rightExpandBtn1->setGraphicsEffect(new QGraphicsOpacityEffect(m_rightExpandBtn1));
        m_rightExpandBtn1->hide();
    }
    if (m_rightExpandBtn2) {
        m_rightExpandBtn2->setGraphicsEffect(new QGraphicsOpacityEffect(m_rightExpandBtn2));
        m_rightExpandBtn2->hide();
    }
    if (m_rightExpandBtn3) {
        m_rightExpandBtn3->setGraphicsEffect(new QGraphicsOpacityEffect(m_rightExpandBtn3));
        m_rightExpandBtn3->hide();
    }

    m_thumbnailLoadingIndicator = new DotGridLoadingIndicator(this);
    if (m_thumbnailLoadingIndicator) {
        m_thumbnailLoadingIndicator->setFixedSize(14, 14);
        m_thumbnailLoadingIndicator->hide();
    }

    qApp->installEventFilter(this);
}

LayerRowWidget::~LayerRowWidget()
{
    qApp->removeEventFilter(this);
}

// ============================================================================
// Data
// ============================================================================

void LayerRowWidget::setLayerData(LayerData* data)
{
    const bool hadData = (m_data != nullptr);
    const bool dataReassigned = (m_data != data);
    m_data = data;
    // The mask preview cache is per-row; drop it only when the row is bound to a
    // different layer. Keeping it across same-layer refreshes lets the mask
    // thumbnail fade out smoothly after the mask is applied/removed (its source
    // tiles are already gone by then). Content edits invalidate via maskThumbnailDirty.
    if (dataReassigned) {
        m_maskThumbCache = QPixmap();
    }
    if (m_data) {
        if (m_data->isBackground() && m_rightExpandProgress > 0.0) {
            closeRightExpandMenu();
        }
        m_removalSnapshot = QPixmap();
        m_expandRotation = m_data->expanded ? 90.0 : 0.0;
        // Keep row height in the same scaled coordinate space as layout/animations.
        setFixedHeight(ThemeManager::instance().scaled(effectiveRowHeight()));

        int newDepth = m_data->depth;
        if (m_lastKnownDepth >= 0 && m_lastKnownDepth != newDepth) {
            animateIndent(m_lastKnownDepth, newDepth);
        } else {
            m_animatedDepth = newDepth;
        }
        m_lastKnownDepth = newDepth;

        const bool hasChildren = m_data->hasChildren();
        if (hadData && m_lastKnownHasChildren != hasChildren) {
            animateChildDisclosure(hasChildren);
        } else {
            if (m_childDisclosureAnim) {
                m_childDisclosureAnim->stop();
            }
            m_childDisclosureProgress = hasChildren ? 1.0 : 0.0;
        }
        m_lastKnownHasChildren = hasChildren;

        // Animate the second (mask) thumbnail sliding/fading in and out so adding
        // or applying a mask doesn't pop the row layout. Only animate on a same-
        // layer refresh; a fresh binding snaps to the resting state.
        const bool hasMask = m_data->hasMask() && !m_data->isGroup();
        if (hadData && !dataReassigned && m_lastKnownHasMask != hasMask) {
            animateMaskReveal(hasMask);
        } else {
            if (m_maskRevealAnim) {
                m_maskRevealAnim->stop();
            }
            m_maskRevealProgress = hasMask ? 1.0 : 0.0;
        }
        m_lastKnownHasMask = hasMask;

        const bool clipped = m_data->clippedToBelow;
        if (m_lastKnownClipped != clipped) {
            animateClipOffset(clipped);
        } else {
            m_clipOffsetProgress = clipped ? 1.0 : 0.0;
        }
        m_lastKnownClipped = clipped;

        const bool visible = m_data->visible;
        const bool effectivelyVisible = isEffectivelyVisible(m_data);
        if (!hadData) {
            if (m_opacityAnim) {
                m_opacityAnim->stop();
            }
            m_visibilityProgress = visible ? 1.0 : 0.0;
            if (m_effectiveOpacityAnim) {
                m_effectiveOpacityAnim->stop();
            }
            m_effectiveVisibilityProgress = effectivelyVisible ? 1.0 : 0.0;
        } else if (m_lastKnownVisible != visible) {
            animateVisibility(visible);
        } else {
            m_visibilityProgress = visible ? 1.0 : 0.0;
        }
        m_lastKnownVisible = visible;

        if (hadData && m_lastKnownEffectiveVisible != effectivelyVisible) {
            animateEffectiveVisibility(effectivelyVisible);
        } else {
            m_effectiveVisibilityProgress = effectivelyVisible ? 1.0 : 0.0;
        }
        m_lastKnownEffectiveVisible = effectivelyVisible;
    } else {
        m_animatedDepth = 0;
        m_lastKnownDepth = -1;
        m_clipOffsetProgress = 0.0;
        m_lastKnownClipped = false;
        m_visibilityProgress = 1.0;
        m_effectiveVisibilityProgress = 1.0;
        m_lastKnownVisible = true;
        m_lastKnownEffectiveVisible = true;
        if (m_childDisclosureAnim) {
            m_childDisclosureAnim->stop();
        }
        m_childDisclosureProgress = 0.0;
        m_lastKnownHasChildren = false;
        if (m_maskRevealAnim) {
            m_maskRevealAnim->stop();
        }
        m_maskRevealProgress = 0.0;
        m_lastKnownHasMask = false;
    }
    if (!m_data || m_data->isGroup() || m_data->isAdjustment() || !m_data->thumbnailDirty) {
        setThumbnailLoading(false);
    } else {
        updateThumbnailLoadingIndicator();
    }
    update();
}

void LayerRowWidget::setRemovalSnapshot(const QPixmap& pixmap)
{
    m_removalSnapshot = pixmap;
    update();
}

void LayerRowWidget::setCanvasSize(const QSize& size)
{
    setDisplayFrame(QRect(0, 0, size.width(), size.height()));
}

void LayerRowWidget::setDisplayFrame(const QRect& frame)
{
    if (m_displayFrame == frame)
        return;
    m_displayFrame = frame;
    updateThumbnailLoadingIndicator();
    update();
}

LayerId LayerRowWidget::layerId() const
{
    return m_data ? m_data->id : LayerId();
}

int LayerRowWidget::effectiveRowHeight() const
{
    return heightForData(m_data);
}

int LayerRowWidget::heightForData(const LayerData* data)
{
    if (!data)
        return kRowHeight;
    const int base = data->isGroup() ? kGroupRowHeight : kRowHeight;
    return data->isBackground() ? (base + kBackgroundTopInset) : base;
}

QSize LayerRowWidget::thumbnailTargetSize(const LayerData* data, const QRect& displayFrame)
{
    auto& tm = ThemeManager::instance();
    const bool isGroup = data && data->isGroup();
    const int thumbH = tm.scaled(isGroup ? kGroupThumbSize : kThumbSize);
    if (thumbH <= 0) {
        return {};
    }
    if (isGroup) {
        return QSize(thumbH, thumbH);
    }

    if (usesContentAwareThumbnailFrame(displayFrame)) {
        return QSize(
            qMax(1, qRound(static_cast<float>(thumbH) * kContentAwareThumbAspect)), thumbH);
    }

    const float aspect = thumbnailAspectRatio(data, displayFrame);
    const int thumbW = qMax(1, qRound(static_cast<float>(thumbH) * aspect));
    return QSize(thumbW, thumbH);
}

QImage LayerRowWidget::buildThumbnailImage(
    const LayerData* data, const QRect& displayFrame, const QSize& targetSize)
{
    return layerPreviewImage(data, displayFrame, targetSize);
}

QPixmap LayerRowWidget::buildThumbnailPixmap(
    const LayerData* data, const QRect& displayFrame, const QSize& targetSize)
{
    const QImage preview = buildThumbnailImage(data, displayFrame, targetSize);
    return preview.isNull() ? QPixmap() : QPixmap::fromImage(preview);
}

void LayerRowWidget::setThumbnailLoading(bool loading)
{
    if (m_thumbnailLoading == loading) {
        updateThumbnailLoadingIndicator();
        return;
    }
    m_thumbnailLoading = loading;
    updateThumbnailLoadingIndicator();
    update();
}

// ============================================================================
// State
// ============================================================================

void LayerRowWidget::setSelected(bool s)
{
    if (m_selected == s)
        return;
    m_selected = s;
    animateSelection(s);
}

void LayerRowWidget::setSelectedImmediate(bool s)
{
    if (m_selected == s && qFuzzyCompare(m_selectionProgress, s ? 1.0 : 0.0))
        return;
    m_selected = s;
    if (m_selectionAnim)
        m_selectionAnim->stop();
    m_selectionProgress = s ? 1.0 : 0.0;
    if (!m_selected) {
        if (m_primaryAnim)
            m_primaryAnim->stop();
        m_primaryProgress = 0.0;
    }
    update();
}

void LayerRowWidget::setPrimary(bool p)
{
    if (m_primary == p)
        return;
    m_primary = p;
    animatePrimary(p);
}

void LayerRowWidget::setPrimaryImmediate(bool p)
{
    if (m_primary == p && qFuzzyCompare(m_primaryProgress, p ? 1.0 : 0.0))
        return;
    m_primary = p;
    if (m_primaryAnim)
        m_primaryAnim->stop();
    m_primaryProgress = p ? 1.0 : 0.0;
    update();
}

void LayerRowWidget::setDragging(bool d)
{
    m_dragging = d;
    if (d) {
        // Clear hover immediately so snapshot and settle don't show hover→no-hover jump
        if (m_hoverAnim)
            m_hoverAnim->stop();
        m_hoveredZone = HitZone::None;
        setHoverProgress(0.0);
    }
    update();
}

// ============================================================================
// Animation Properties
// ============================================================================

void LayerRowWidget::setHoverProgress(qreal v)
{
    if (qFuzzyCompare(m_hoverProgress, v))
        return;
    m_hoverProgress = v;
    update();
}

void LayerRowWidget::setSelectionProgress(qreal v)
{
    if (qFuzzyCompare(m_selectionProgress, v))
        return;
    m_selectionProgress = v;
    update();
}

void LayerRowWidget::setPrimaryProgress(qreal v)
{
    if (qFuzzyCompare(m_primaryProgress, v))
        return;
    m_primaryProgress = v;
    update();
}

void LayerRowWidget::setExpandRotation(qreal v)
{
    if (qFuzzyCompare(m_expandRotation, v))
        return;
    m_expandRotation = v;
    update();
}

void LayerRowWidget::setAnimatedDepth(qreal v)
{
    if (qFuzzyCompare(m_animatedDepth, v))
        return;
    m_animatedDepth = v;
    update();
}

void LayerRowWidget::setClipOffsetProgress(qreal v)
{
    if (qFuzzyCompare(m_clipOffsetProgress, v))
        return;
    m_clipOffsetProgress = v;
    update();
}

void LayerRowWidget::setRightExpandProgress(qreal v)
{
    if (qFuzzyCompare(m_rightExpandProgress, v))
        return;
    m_rightExpandProgress = v;
    updateRightExpandButtonsGeometry();
    update();
}

void LayerRowWidget::setVisibilityProgress(qreal v)
{
    if (qFuzzyCompare(m_visibilityProgress, v))
        return;
    m_visibilityProgress = v;
    update();
}

void LayerRowWidget::setEffectiveVisibilityProgress(qreal v)
{
    if (qFuzzyCompare(m_effectiveVisibilityProgress, v))
        return;
    m_effectiveVisibilityProgress = v;
    update();
}

void LayerRowWidget::setThumbnailCtrlGlowProgress(qreal v)
{
    if (qFuzzyCompare(m_thumbnailCtrlGlowProgress, v))
        return;
    m_thumbnailCtrlGlowProgress = v;
    update();
}

void LayerRowWidget::setThumbnailClickFlashProgress(qreal v)
{
    if (qFuzzyCompare(m_thumbnailClickFlashProgress, v))
        return;
    m_thumbnailClickFlashProgress = v;
    update();
}

void LayerRowWidget::setChildDisclosureProgress(qreal v)
{
    const qreal clamped = qBound<qreal>(0.0, v, 1.0);
    if (qFuzzyCompare(m_childDisclosureProgress, clamped))
        return;
    m_childDisclosureProgress = clamped;
    updateThumbnailLoadingIndicator();
    syncRenameEditorPresentation();
    update();
}

void LayerRowWidget::setMaskRevealProgress(qreal v)
{
    const qreal clamped = qBound<qreal>(0.0, v, 1.0);
    if (qFuzzyCompare(m_maskRevealProgress, clamped))
        return;
    m_maskRevealProgress = clamped;
    // The name (and inline rename editor) anchors to the right of the mask slot,
    // so keep them in sync as the slot grows/shrinks.
    syncRenameEditorPresentation();
    update();
}

// ============================================================================
// Rename
// ============================================================================

void LayerRowWidget::startRename()
{
    if (!m_data || m_renameEdit)
        return;

    m_renameEdit = new QLineEdit(this);
    m_renameEdit->setText(m_data->name);
    m_renameEdit->setMaxLength(LayerData::kMaxNameLength);
    // Avoid Qt's default line-edit menu on RMB; our app-wide menu would stack with it.
    m_renameEdit->setContextMenuPolicy(Qt::NoContextMenu);
    syncRenameEditorPresentation();
    m_renameEdit->show();
    m_renameEdit->setFocus();
    m_renameEdit->selectAll();

    connect(m_renameEdit, &QLineEdit::editingFinished, this, &LayerRowWidget::commitRename);
}

void LayerRowWidget::commitRename()
{
    if (!m_renameEdit || !m_data)
        return;

    QString newName = LayerData::clampedName(m_renameEdit->text().trimmed());
    if (newName.isEmpty())
        newName = m_data->name;

    m_renameEdit->deleteLater();
    m_renameEdit = nullptr;

    if (newName != m_data->name) {
        emit renameFinished(m_data->id, newName);
    }
    update();
}

void LayerRowWidget::cancelRename()
{
    if (!m_renameEdit)
        return;
    m_renameEdit->deleteLater();
    m_renameEdit = nullptr;
    update();
}

// ============================================================================
// Hit Test
// ============================================================================

LayerRowWidget::HitZone LayerRowWidget::hitTest(const QPoint& pos) const
{
    if (!m_data)
        return HitZone::None;

    const QList<QRect> expandRects = rightExpandButtonRects();
    if (expandRects.size() >= 3) {
        if (expandRects[0].contains(pos))
            return HitZone::RightExpandBtn1;
        if (expandRects[1].contains(pos))
            return HitZone::RightExpandBtn2;
        if (expandRects[2].contains(pos))
            return HitZone::RightExpandBtn3;
    }
    if (eyeRect().contains(pos))
        return HitZone::EyeBtn;
    if (hasAlphaLockIcon() && alphaLockRect().contains(pos))
        return HitZone::AlphaLockBtn;
    if (hasLockIcon() && lockRect().contains(pos))
        return HitZone::LockBtn;
    if (m_data->hasChildren() && expandArrowRect().contains(pos))
        return HitZone::ExpandArrow;
    if (thumbnailRect().contains(pos))
        return HitZone::Thumbnail;
    if (hasMaskThumbnail() && maskThumbnailRect().contains(pos))
        return HitZone::MaskThumbnail;
    if (nameRect().contains(pos))
        return HitZone::Name;

    return HitZone::Background;
}

// ============================================================================
// Geometry Helpers
// ============================================================================

bool LayerRowWidget::shouldShiftContentForSelection() const
{
    return m_data && m_data->depth <= 0;
}

int LayerRowWidget::selectionContentOffset() const
{
    if (!shouldShiftContentForSelection()) {
        return 0;
    }

    const int maxOffset = selectionShiftMaxPx(m_clipOffsetProgress);
    return qRound(static_cast<qreal>(maxOffset) * qBound<qreal>(0.0, m_selectionProgress, 1.0));
}

int LayerRowWidget::indentStartX() const
{
    return ThemeManager::instance().scaled(kPad) + selectionContentOffset();
}

int LayerRowWidget::indentWidth() const
{
    return qRound(m_animatedDepth * ThemeManager::instance().scaled(kIndentPerLevel));
}

int LayerRowWidget::clipContentOffset() const
{
    return qRound(m_clipOffsetProgress * ThemeManager::instance().scaled(kClipIndent));
}

QRect LayerRowWidget::eyeRect() const
{
    auto& tm = ThemeManager::instance();
    int sz = tm.scaled(kEyeSize);
    const QRect content = contentRect();
    const int rightInset = tm.scaled(kPad + kGap);
    int x = content.right() - rightInset - sz;
    int y = content.top() + (content.height() - sz) / 2;
    return QRect(x, y, sz, sz);
}

QRect LayerRowWidget::expandArrowRect() const
{
    auto& tm = ThemeManager::instance();
    const QRect content = contentRect();
    const int leftInset = tm.scaled(kGap);
    int x = indentStartX() + indentWidth() + clipContentOffset() + leftInset;
    int sz = tm.scaled(kExpandSize);
    int y = content.top() + (content.height() - sz) / 2;
    return QRect(x, y, sz, sz);
}

QRect LayerRowWidget::thumbnailRect() const
{
    auto& tm = ThemeManager::instance();
    const QRect content = contentRect();
    const int baseX = indentStartX() + indentWidth() + clipContentOffset() + tm.scaled(kGap);
    const int arrowSpace = tm.scaled(kExpandSize + kGap);
    const int x = baseX + qRound(arrowSpace * m_childDisclosureProgress);
    bool isGroup = m_data && m_data->isGroup();
    const int maxSize = tm.scaled(isGroup ? kGroupThumbSize : kThumbSize);

    if (isGroup) {
        int y = content.top() + (content.height() - maxSize) / 2;
        return QRect(x, y, maxSize, maxSize);
    }

    const float aspect = thumbnailAspectRatio(m_data, m_displayFrame);
    const int thumbH = maxSize;
    const int thumbW = qMax(1, qRound(static_cast<float>(thumbH) * aspect));

    int y = content.top() + (content.height() - thumbH) / 2;
    return QRect(x, y, thumbW, thumbH);
}

bool LayerRowWidget::hasMaskThumbnail() const
{
    return m_data && m_data->hasMask() && !m_data->isGroup();
}

bool LayerRowWidget::maskSlotActive() const
{
    return hasMaskThumbnail() || m_maskRevealProgress > 0.001;
}

QRect LayerRowWidget::maskThumbnailRect() const
{
    if (!maskSlotActive() || !m_data) {
        return QRect();
    }
    auto& tm = ThemeManager::instance();
    const QRect base = thumbnailRect();
    const int x = base.right() + 1 + tm.scaled(kGap);
    return QRect(x, base.top(), base.width(), base.height());
}

QRect LayerRowWidget::nameRect() const
{
    auto& tm = ThemeManager::instance();
    const QRect content = contentRect();
    // Anchor the name to the right of the layer thumbnail, then push it further
    // right by the animated mask-slot advance so the name slides smoothly as the
    // mask thumbnail reveals/hides instead of jumping a full slot width.
    const QRect baseThumb = thumbnailRect();
    int left = baseThumb.right() + tm.scaled(kGap);
    if (m_maskRevealProgress > 0.001) {
        const int fullAdvance = tm.scaled(kGap) + baseThumb.width();
        left += qRound(m_maskRevealProgress * fullAdvance);
    }
    int right;
    if (hasLockIcon()) {
        right = lockRect().left() - tm.scaled(kGap);
    } else if (hasAlphaLockIcon()) {
        right = alphaLockRect().left() - tm.scaled(kGap);
    } else {
        right = eyeRect().left() - tm.scaled(kGap);
    }
    return QRect(left, content.top(), qMax(0, right - left), content.height());
}

QRect LayerRowWidget::nameTextRect() const
{
    return nameRect();
}

bool LayerRowWidget::hasAlphaLockIcon() const
{
    if (!m_data)
        return false;
    return m_data->isPixelLayer() && m_data->alphaLock;
}

QRect LayerRowWidget::alphaLockRect() const
{
    if (!hasAlphaLockIcon()) {
        return QRect();
    }
    auto& tm = ThemeManager::instance();
    const QRect eye = eyeRect();
    int sz = eye.width();
    int step = tm.scaled(kGap) + sz;
    int x = eye.left() - step; // Alpha is always 1 step left of eye
    return QRect(x, eye.top(), sz, sz);
}

bool LayerRowWidget::hasLockIcon() const
{
    if (!m_data)
        return false;
    return m_data->locked;
}

QRect LayerRowWidget::lockRect() const
{
    if (!hasLockIcon()) {
        return QRect();
    }
    auto& tm = ThemeManager::instance();
    const QRect eye = eyeRect();
    int sz = eye.width();
    int step = tm.scaled(kGap) + sz;
    int x = (hasAlphaLockIcon() ? alphaLockRect().left() : eye.left()) - step;
    return QRect(x, eye.top(), sz, sz);
}

QRect LayerRowWidget::contentRect() const
{
    const int inset = (m_data && m_data->isBackground())
        ? ThemeManager::instance().scaled(kBackgroundTopInset)
        : 0;
    const int rightExpandPx
        = qRound(m_rightExpandProgress * ThemeManager::instance().scaled(kRightExpandMax));
    return rect().adjusted(0, inset, -rightExpandPx, 0);
}

QList<QRect> LayerRowWidget::rightExpandButtonRects() const
{
    QList<QRect> rects;
    if (m_rightExpandProgress <= 0.0)
        return rects;

    auto& tm = ThemeManager::instance();
    const int gap = tm.scaled(kRightExpandButtonGap);
    const int availableWidth = tm.scaled(kRightExpandMax);
    const int fullBtnWidth = (availableWidth - gap * 2) / 3;
    const int btnWidth = qMax(0, qRound(fullBtnWidth * m_rightExpandProgress));
    const QRect content = contentRect();
    const int btnHeight = content.height();

    const int totalWidth = btnWidth * 3 + gap * 2;
    int x = width() - totalWidth;
    const int y = content.top();

    for (int i = 0; i < 3; ++i) {
        rects.append(QRect(x, y, btnWidth, btnHeight));
        x += btnWidth + gap;
    }
    return rects;
}

bool LayerRowWidget::hasTypeBadge() const
{
    if (!m_data)
        return false;
    if (m_data->isGroup())
        return false;
    return m_data->type != LayerType::Raster;
}

QString LayerRowWidget::typeBadgeText() const
{
    if (!hasTypeBadge())
        return {};
    const char* ctx = "ruwa::ui::widgets::LayerRowWidget";
    switch (m_data->type) {
    case LayerType::Smart:
        return QCoreApplication::translate(ctx, "smart");
    case LayerType::Board:
        return QCoreApplication::translate(ctx, "board");
    case LayerType::Adjustment:
        return QCoreApplication::translate(ctx, "adjustment");
    case LayerType::Vector:
        return QCoreApplication::translate(ctx, "vector");
    case LayerType::Mask:
        return QCoreApplication::translate(ctx, "mask");
    case LayerType::Background:
        return QCoreApplication::translate(ctx, "background");
    case LayerType::Text:
        return QCoreApplication::translate(ctx, "text");
    default:
        return QString();
    }
}

QFont LayerRowWidget::nameDisplayFont() const
{
    const auto& c = ThemeManager::instance().colors();
    auto& tm = ThemeManager::instance();

    QFont font;
    font.setFamily(c.fonts.uiFont);
    const bool isGroup = m_data && m_data->isGroup();
    font.setPixelSize(tm.scaledFontSize(isGroup ? 9 : 10));
    font.setBold(isGroup);
    return font;
}

QColor LayerRowWidget::nameDisplayColor() const
{
    const auto& c = ThemeManager::instance().colors();

    QColor textCol;
    if (m_selected && m_primary) {
        textCol = ThemeColors::interpolate(c.text, c.text, m_selectionProgress);
    } else {
        textCol = ThemeColors::interpolate(c.textMuted, c.text, m_hoverProgress * 0.7);
    }

    if (m_data && !m_data->visible) {
        textCol.setAlphaF(textCol.alphaF() * hiddenBlend(m_visibilityProgress, 0.4));
    }

    return textCol;
}

QRect LayerRowWidget::nameDisplayRect() const
{
    const QRect textArea = nameTextRect();
    auto& tm = ThemeManager::instance();

    const QFont font = nameDisplayFont();
    QFont metaMeasureFont = font;
    metaMeasureFont.setBold(false);
    metaMeasureFont.setPixelSize(tm.scaledFontSize(8));

    const int nameLineHeight = QFontMetrics(font).height();
    const int metaLineHeight = QFontMetrics(metaMeasureFont).height();
    const int stackedHeight = nameLineHeight + metaLineHeight;
    const int blockTop = textArea.top() + qMax(0, (textArea.height() - stackedHeight) / 2);

    return QRect(
        textArea.left(), blockTop, textArea.width(), qMin(textArea.height(), nameLineHeight));
}

void LayerRowWidget::syncRenameEditorPresentation()
{
    if (!m_renameEdit) {
        return;
    }

    const QFont font = nameDisplayFont();
    const QColor textColor = nameDisplayColor();
    auto& tm = ThemeManager::instance();
    QRect editRect = nameDisplayRect();
    const int leftInsetCompensation = tm.scaled(1);
    editRect.adjust(-leftInsetCompensation, 0, leftInsetCompensation, 0);

    m_renameEdit->setGeometry(editRect);
    m_renameEdit->setFont(font);
    m_renameEdit->setFrame(false);
    m_renameEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_renameEdit->setTextMargins(0, 0, 0, 0);
    m_renameEdit->setContentsMargins(0, 0, 0, 0);
    m_renameEdit->setStyleSheet(QString("QLineEdit {"
                                        " background: transparent;"
                                        " color: rgba(%1,%2,%3,%4);"
                                        " border: none;"
                                        " padding: 0px;"
                                        " margin: 0px;"
                                        " }")
            .arg(textColor.red())
            .arg(textColor.green())
            .arg(textColor.blue())
            .arg(textColor.alpha()));
}

// ============================================================================
// Animations
// ============================================================================

void LayerRowWidget::animateHover(bool in)
{
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(in ? 1.0 : 0.0);
    m_hoverAnim->start();
}

void LayerRowWidget::animateSelection(bool sel)
{
    m_selectionAnim->stop();
    m_selectionAnim->setStartValue(m_selectionProgress);
    m_selectionAnim->setEndValue(sel ? 1.0 : 0.0);
    m_selectionAnim->start();
}

void LayerRowWidget::animatePrimary(bool primary)
{
    if (!m_primaryAnim) {
        m_primaryProgress = primary ? 1.0 : 0.0;
        update();
        return;
    }
    m_primaryAnim->stop();
    m_primaryAnim->setStartValue(m_primaryProgress);
    m_primaryAnim->setEndValue(primary ? 1.0 : 0.0);
    m_primaryAnim->start();
}

void LayerRowWidget::animateExpand(bool exp)
{
    m_expandAnim->stop();
    m_expandAnim->setStartValue(m_expandRotation);
    m_expandAnim->setEndValue(exp ? 90.0 : 0.0);
    m_expandAnim->start();
}

void LayerRowWidget::animateIndent(int fromDepth, int toDepth)
{
    if (!m_indentAnim) {
        m_indentAnim = new QPropertyAnimation(this, "animatedDepth", this);
        m_indentAnim->setDuration(200);
        m_indentAnim->setEasingCurve(QEasingCurve::InOutCubic);
    }
    m_indentAnim->stop();
    m_indentAnim->setStartValue(qreal(fromDepth));
    m_indentAnim->setEndValue(qreal(toDepth));
    m_indentAnim->start();
}

void LayerRowWidget::animateClipOffset(bool clipped)
{
    if (!m_clipOffsetAnim) {
        return;
    }
    m_clipOffsetAnim->stop();
    m_clipOffsetAnim->setStartValue(m_clipOffsetProgress);
    m_clipOffsetAnim->setEndValue(clipped ? 1.0 : 0.0);
    m_clipOffsetAnim->start();
}

void LayerRowWidget::closeRightExpandMenu()
{
    if (m_rightExpandProgress > 0.0) {
        animateRightExpand(false);
    }
}

void LayerRowWidget::animateRightExpand(bool expand)
{
    if (!m_rightExpandAnim) {
        m_rightExpandAnim = new QPropertyAnimation(this, "rightExpandProgress", this);
        m_rightExpandAnim->setDuration(220);
        m_rightExpandAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
    m_rightExpandAnim->stop();
    m_rightExpandAnim->setStartValue(m_rightExpandProgress);
    m_rightExpandAnim->setEndValue(expand ? 1.0 : 0.0);
    m_rightExpandAnim->start();
}

void LayerRowWidget::animateVisibility(bool visible)
{
    if (!m_opacityAnim) {
        m_visibilityProgress = visible ? 1.0 : 0.0;
        update();
        return;
    }

    m_opacityAnim->stop();
    m_opacityAnim->setStartValue(m_visibilityProgress);
    m_opacityAnim->setEndValue(visible ? 1.0 : 0.0);
    m_opacityAnim->start();
}

void LayerRowWidget::animateEffectiveVisibility(bool visible)
{
    if (!m_effectiveOpacityAnim) {
        m_effectiveVisibilityProgress = visible ? 1.0 : 0.0;
        update();
        return;
    }

    m_effectiveOpacityAnim->stop();
    m_effectiveOpacityAnim->setStartValue(m_effectiveVisibilityProgress);
    m_effectiveOpacityAnim->setEndValue(visible ? 1.0 : 0.0);
    m_effectiveOpacityAnim->start();
}

void LayerRowWidget::animateChildDisclosure(bool hasChildren)
{
    if (!m_childDisclosureAnim) {
        m_childDisclosureProgress = hasChildren ? 1.0 : 0.0;
        update();
        return;
    }

    m_childDisclosureAnim->stop();
    m_childDisclosureAnim->setStartValue(m_childDisclosureProgress);
    m_childDisclosureAnim->setEndValue(hasChildren ? 1.0 : 0.0);
    m_childDisclosureAnim->start();
}

void LayerRowWidget::animateMaskReveal(bool hasMask)
{
    if (!m_maskRevealAnim) {
        m_maskRevealProgress = hasMask ? 1.0 : 0.0;
        update();
        return;
    }

    m_maskRevealAnim->stop();
    m_maskRevealAnim->setStartValue(m_maskRevealProgress);
    m_maskRevealAnim->setEndValue(hasMask ? 1.0 : 0.0);
    m_maskRevealAnim->start();
}

void LayerRowWidget::animateThumbnailCtrlGlow(bool in)
{
    if (!m_thumbnailCtrlGlowAnim) {
        return;
    }
    m_thumbnailCtrlGlowAnim->stop();
    m_thumbnailCtrlGlowAnim->setStartValue(m_thumbnailCtrlGlowProgress);
    m_thumbnailCtrlGlowAnim->setEndValue(in ? 1.0 : 0.0);
    m_thumbnailCtrlGlowAnim->start();
}

void LayerRowWidget::triggerThumbnailClickFlash()
{
    if (!m_thumbnailClickFlashAnim) {
        return;
    }

    m_thumbnailClickFlashAnim->stop();
    setThumbnailClickFlashProgress(1.0);
    m_thumbnailClickFlashAnim->setStartValue(1.0);
    m_thumbnailClickFlashAnim->setEndValue(0.0);
    m_thumbnailClickFlashAnim->start();
}

void LayerRowWidget::updateThumbnailCtrlGlowState()
{
    const bool show
        = underMouse() && m_data && !m_data->isAdjustment() && m_hoveredZone == HitZone::Thumbnail;
    animateThumbnailCtrlGlow(show);
}

// ============================================================================
// Events
// ============================================================================

void LayerRowWidget::enterEvent(QEnterEvent* e)
{
    QWidget::enterEvent(e);
    m_hoveredZone = hitTest(e->position().toPoint());
    animateHover(true);
    updateThumbnailCtrlGlowState();
}

void LayerRowWidget::leaveEvent(QEvent* e)
{
    QWidget::leaveEvent(e);
    m_hoveredZone = HitZone::None;
    animateHover(false);
    animateThumbnailCtrlGlow(false);
}

void LayerRowWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_mousePressed = true;
        m_clipSwipeHandled = false;
        m_eyePressed = false;
        m_alphaLockPressed = false;
        m_pressPos = e->pos();

        HitZone zone = hitTest(e->pos());

        if (zone == HitZone::EyeBtn && m_data) {
            m_eyePressed = true;
            m_mousePressed = false;
            bool wasVisible = m_data->visible;
            emit eyePressed(m_data->id, wasVisible);
        } else if (zone == HitZone::AlphaLockBtn && m_data) {
            m_alphaLockPressed = true;
            m_mousePressed = false;
        } else if (zone == HitZone::LockBtn && m_data) {
            m_lockPressed = true;
            m_mousePressed = false;
        } else if (zone == HitZone::RightExpandBtn1 || zone == HitZone::RightExpandBtn2
            || zone == HitZone::RightExpandBtn3) {
            m_rightExpandPressed = true;
            m_mousePressed = false;
        }
    }
    e->accept();
}

void LayerRowWidget::mouseMoveEvent(QMouseEvent* e)
{
    // If release happened outside this widget, don't keep stale pressed state.
    if (!(e->buttons() & Qt::LeftButton)) {
        m_mousePressed = false;
        m_eyePressed = false;
        m_alphaLockPressed = false;
        m_lockPressed = false;
        m_rightExpandPressed = false;
    }

    // Update hover zone
    HitZone newZone = hitTest(e->pos());
    if (newZone != m_hoveredZone) {
        m_hoveredZone = newZone;
        update();
    }
    updateThumbnailCtrlGlowState();

    // Swipe / drag detection (skip if eye, alpha lock, or lock was pressed; skip if layer is
    // locked)
    if (m_mousePressed && !m_eyePressed && !m_alphaLockPressed && !m_lockPressed && m_data
        && !m_data->locked && !m_clipSwipeHandled) {
        const QPoint delta = e->pos() - m_pressPos;
        const int dx = delta.x();
        const int dy = delta.y();
        const int absDx = qAbs(dx);
        const int absDy = qAbs(dy);

        if (absDx >= kDragThreshold || absDy >= kDragThreshold) {
            // Swipe: long horizontal, clearly horizontal (drag favored for diagonals)
            const bool isLongHorizontal = absDx >= kSwipeMinDistance && absDx > 2 * absDy;
            const bool couldBecomeSwipe = absDx > 2 * absDy && absDx < kSwipeMinDistance;

            if (isLongHorizontal) {
                m_mousePressed = false;
                m_clipSwipeHandled = true;
                const bool pressOnRightHalf = m_pressPos.x() >= width() / 2;
                if (pressOnRightHalf && !m_data->isBackground()) {
                    // Right half: swipe right-to-left = expand offset, left-to-right = collapse
                    // Background layer has no right-expand menu
                    animateRightExpand(dx < 0);
                } else if (!pressOnRightHalf && !m_data->isBackground()) {
                    // Left half: clip/unclip
                    emit clipSwipeRequested(m_data->id, dx > 0);
                }
            } else if (!couldBecomeSwipe) {
                // Not waiting for more horizontal — trigger drag
                m_mousePressed = false;
                emit dragInitiated(m_data->id, mapToGlobal(e->pos()));
            }
            // else: horizontal but not long enough yet — keep tracking, don't decide
        }
    }

    e->accept();
}

void LayerRowWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_data) {
        m_mousePressed = false;
        m_eyePressed = false;
        m_alphaLockPressed = false;
        m_lockPressed = false;
        m_rightExpandPressed = false;
        return;
    }

    if (m_rightExpandPressed) {
        m_rightExpandPressed = false;
        HitZone zone = hitTest(e->pos());
        if (zone == HitZone::RightExpandBtn1 && m_data) {
            emit thumbnailCtrlClicked(m_data->id);
        } else if (zone == HitZone::RightExpandBtn2 && m_data) {
            emit rightExpandDuplicateClicked(m_data->id);
        } else if (zone == HitZone::RightExpandBtn3 && m_data) {
            emit rightExpandDeleteClicked(m_data->id);
        }
        closeRightExpandMenu();
        e->accept();
        return;
    }

    if (m_eyePressed) {
        m_eyePressed = false;
        e->accept();
        return;
    }

    if (m_alphaLockPressed) {
        m_alphaLockPressed = false;
        if (hitTest(e->pos()) == HitZone::AlphaLockBtn) {
            emit alphaLockClicked(m_data->id);
        }
        e->accept();
        return;
    }

    if (m_lockPressed) {
        m_lockPressed = false;
        if (hitTest(e->pos()) == HitZone::LockBtn) {
            emit lockClicked(m_data->id);
        }
        e->accept();
        return;
    }

    if (!m_mousePressed)
        return;
    m_mousePressed = false;

    if (m_clipSwipeHandled) {
        m_clipSwipeHandled = false;
        e->accept();
        return;
    }

    HitZone zone = hitTest(e->pos());

    switch (zone) {
    case HitZone::ExpandArrow:
        animateExpand(!m_data->expanded);
        emit expandToggled(m_data->id);
        break;
    case HitZone::EyeBtn:
        // Eye press is handled on press, not release
        break;
    default:
        closeRightExpandMenu();
        if (zone == HitZone::Thumbnail && !m_data->isAdjustment()
            && (e->modifiers() & Qt::ControlModifier)) {
            triggerThumbnailClickFlash();
            emit thumbnailCtrlClicked(m_data->id);
            break;
        }
        // Clicking either preview of a masked layer selects where to paint
        // (layer pixels vs the mask) and selects the layer.
        if (hasMaskThumbnail() && (zone == HitZone::Thumbnail || zone == HitZone::MaskThumbnail)
            && !(e->modifiers() & Qt::ControlModifier)) {
            m_data->maskEditActive = (zone == HitZone::MaskThumbnail);
            update();
            emit clicked(m_data->id, e->modifiers());
            break;
        }
        emit clicked(m_data->id, e->modifiers());
        break;
    }

    e->accept();
}

void LayerRowWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (!m_data || e->button() != Qt::LeftButton)
        return;
    if (e->modifiers() & (Qt::AltModifier | Qt::ControlModifier)) {
        // Alt/Control modifiers are reserved for layer-list interactions.
        e->accept();
        return;
    }

    HitZone zone = hitTest(e->pos());
    if (zone == HitZone::EyeBtn) {
        emit eyePressed(m_data->id, m_data->visible);
        e->accept();
        return;
    }
    if (zone == HitZone::ExpandArrow) {
        animateExpand(!m_data->expanded);
        emit expandToggled(m_data->id);
        e->accept();
        return;
    }
    if (zone == HitZone::Name) {
        startRename();
    } else if (zone == HitZone::Thumbnail && m_data->isText()) {
        emit textEditRequested(m_data->id);
    }
    e->accept();
}

void LayerRowWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    updateRightExpandButtonsGeometry();
    updateThumbnailLoadingIndicator();
    syncRenameEditorPresentation();
}

bool LayerRowWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == qApp) {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            const QPoint localPos = mapFromGlobal(QCursor::pos());
            if (rect().contains(localPos)) {
                m_hoveredZone = hitTest(localPos);
            }
            updateThumbnailCtrlGlowState();
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

// ============================================================================
// Paint
// ============================================================================

void LayerRowWidget::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    if (!m_data && m_removalSnapshot.isNull())
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_rowOpacity * hiddenBlend(m_effectiveVisibilityProgress, 0.62));

    if (m_dragging) {
        p.setOpacity(m_rowOpacity * hiddenBlend(m_effectiveVisibilityProgress, 0.62) * 0.35);
    }

    if (!m_data && !m_removalSnapshot.isNull()) {
        p.drawPixmap(0, 0, m_removalSnapshot);
        return;
    }

    const QRect content = contentRect();
    QRectF r = QRectF(content).adjusted(1, 1, -1, -1);

    drawBackground(p, r);
    drawSelectionHighlight(p, r);
    drawHoverOverlay(p, r);
    drawDisplayColorBorderAccent(p, r);
    drawClippingHandle(p);
    drawIndentLines(p);
    drawExpandArrow(p, expandArrowRect());
    drawThumbnail(p, thumbnailRect());
    if (maskSlotActive()) {
        const qreal maskF = qBound<qreal>(0.0, m_maskRevealProgress, 1.0);
        const QRect maskRect = maskThumbnailRect();

        // Unfold the mask thumbnail horizontally from its left edge while fading,
        // so it grows out of / collapses into the layer thumbnail's side.
        p.save();
        p.setOpacity(p.opacity() * maskF);
        if (maskF < 0.999) {
            p.translate(maskRect.left(), 0.0);
            p.scale(maskF, 1.0);
            p.translate(-maskRect.left(), 0.0);
        }
        drawMaskThumbnail(p, maskRect);
        p.restore();

        // Ring around the active paint target (layer pixels vs mask) — only
        // meaningful on the selected layer, which is what painting affects.
        // Drawn outside the unfold transform; fade it with the reveal so it
        // doesn't pop in at full strength while the mask is still appearing.
        if (m_selected && hasMaskThumbnail()) {
            QRect activeRect;
            if (m_data->maskEditActive) {
                // The mask is the active target: grow the ring from the left edge
                // in lockstep with the unfolding mask thumbnail so its width tracks
                // the (still-animating) thumbnail instead of snapping to full size.
                const int w = qMax(1, qRound(maskF * maskRect.width()));
                activeRect = QRect(maskRect.left(), maskRect.top(), w, maskRect.height());
            } else {
                activeRect = thumbnailRect();
            }
            const qreal prevOpacity = p.opacity();
            p.setOpacity(prevOpacity * maskF);
            QColor ring = ThemeManager::instance().colors().primary;
            QPen ringPen(ring, ThemeManager::instance().scaled(2) * 0.9);
            ringPen.setCosmetic(true);
            p.setPen(ringPen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(activeRect).adjusted(-1.0, -1.0, 1.0, 1.0), 4, 4);
            p.setOpacity(prevOpacity);
        }
    }
    drawAlphaLockButton(p, alphaLockRect());
    drawLockButton(p, lockRect());
    drawEyeButton(p, eyeRect());
    drawName(p, nameRect());
    drawMeta(p, nameTextRect());
    drawRightExpandButtons(p);
}

void LayerRowWidget::drawBackground(QPainter& p, const QRectF& r)
{
    const auto& c = ThemeManager::instance().colors();
    const auto& tm = ThemeManager::instance();

    QColor bg = c.surfaceAlt;
    bg.setAlphaF(0.6);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, kRadius, kRadius);
    drawDisplayColorBackgroundAccent(p, r);

    if (m_data && m_data->isBackground()) {
        QColor divider = c.border;
        divider.setAlphaF(0.85);
        const int h = tm.scaled(5);
        const int w = tm.scaled(34);
        const int x = qRound(r.x() + (r.width() - w) / 2.0);
        const int y = tm.scaled(4);
        p.setBrush(divider);
        p.drawRoundedRect(QRectF(x, y, w, h), h * 0.5, h * 0.5);
    }
}

void LayerRowWidget::drawDisplayColorBackgroundAccent(QPainter& p, const QRectF& r)
{
    const QColor accentBase = layerRowAccentColor(m_data);
    if (!accentBase.isValid()) {
        return;
    }

    const auto& c = ThemeManager::instance().colors();
    QColor accent = ThemeColors::interpolate(accentBase, c.text, c.isDark ? 0.08 : 0.16);

    QLinearGradient fillGrad(r.left(), r.center().y(), r.right(), r.center().y());
    QColor stop0 = accent;
    stop0.setAlphaF(0.12);
    QColor stop1 = accent;
    stop1.setAlphaF(0.075);
    QColor stop2 = accent;
    stop2.setAlphaF(0.03);
    QColor stop3 = accent;
    stop3.setAlphaF(0.008);
    QColor stop4 = accent;
    stop4.setAlpha(0);

    fillGrad.setColorAt(0.0, stop0);
    fillGrad.setColorAt(0.16, stop1);
    fillGrad.setColorAt(0.38, stop2);
    fillGrad.setColorAt(0.72, stop3);
    fillGrad.setColorAt(1.0, stop4);

    p.setPen(Qt::NoPen);
    p.setBrush(fillGrad);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

void LayerRowWidget::drawSelectionHighlight(QPainter& p, const QRectF& r)
{
    if (m_selectionProgress <= 0)
        return;

    const auto& c = ThemeManager::instance().colors();
    auto& tm = ThemeManager::instance();
    const qreal primaryMix = qBound<qreal>(0.0, m_primaryProgress, 1.0);

    QColor sel = c.primary;
    sel.setAlphaF((0.12 + 0.06 * primaryMix) * m_selectionProgress);

    p.setPen(Qt::NoPen);
    p.setBrush(sel);
    p.drawRoundedRect(r, kRadius, kRadius);

    const QRect content = contentRect();
    const bool shiftContent = shouldShiftContentForSelection();
    const int verticalInset = qMax(3, tm.scaled(6));
    const int capsuleWidth = selectionIndicatorWidthPx();
    const int capsuleLeft
        = content.left() + (shiftContent ? selectionIndicatorInsetPx() : qMax(1, tm.scaled(2)));
    const int fullCapsuleHeight = qMax(0, content.height() - verticalInset * 2);
    const qreal capsuleHeightFactor = 0.52 + 0.48 * primaryMix;
    const int animatedCapsuleHeight = qRound(static_cast<qreal>(fullCapsuleHeight)
        * capsuleHeightFactor * qBound<qreal>(0.0, m_selectionProgress, 1.0));

    if (capsuleWidth > 0 && animatedCapsuleHeight > 0) {
        QColor capsuleColor = ThemeColors::adjustBrightness(c.primary, c.isDark ? 1.08 : 0.96);
        capsuleColor.setAlphaF((0.74 + 0.18 * primaryMix) * m_selectionProgress);
        const int capsuleTop = content.top() + verticalInset
            + qMax(0, (fullCapsuleHeight - animatedCapsuleHeight) / 2);
        const QRectF capsuleRect(capsuleLeft, capsuleTop, capsuleWidth, animatedCapsuleHeight);
        p.setPen(Qt::NoPen);
        p.setBrush(capsuleColor);
        QPainterPath capsulePath;
        const qreal radius = qMin(capsuleRect.width(), capsuleRect.height()) * 0.5;
        capsulePath.addRoundedRect(capsuleRect, radius, radius);
        p.drawPath(capsulePath);
    }
}

void LayerRowWidget::drawHoverOverlay(QPainter& p, const QRectF& r)
{
    if (m_hoverProgress <= 0)
        return;
    const auto& c = ThemeManager::instance().colors();

    // Match ToolsPanel hover style
    QColor hover = c.surfaceHover();
    hover.setAlphaF(0.24 * m_hoverProgress);
    p.setPen(Qt::NoPen);
    p.setBrush(hover);
    p.drawRoundedRect(r, kRadius, kRadius);
}

void LayerRowWidget::drawDisplayColorBorderAccent(QPainter& p, const QRectF& r)
{
    const QColor accentBase = layerRowAccentColor(m_data);
    if (!accentBase.isValid()) {
        return;
    }

    const auto& c = ThemeManager::instance().colors();
    QColor accent = ThemeColors::adjustBrightness(accentBase, c.isDark ? 1.12 : 0.95);

    QLinearGradient borderGrad(r.left(), r.center().y(), r.right(), r.center().y());
    QColor stop0 = accent;
    stop0.setAlphaF(0.74);
    QColor stop1 = accent;
    stop1.setAlphaF(0.5);
    QColor stop2 = accent;
    stop2.setAlphaF(0.18);
    QColor stop3 = accent;
    stop3.setAlphaF(0.035);
    QColor stop4 = accent;
    stop4.setAlphaF(0.006);
    QColor stop5 = accent;
    stop5.setAlpha(0);

    borderGrad.setColorAt(0.0, stop0);
    borderGrad.setColorAt(0.11, stop1);
    borderGrad.setColorAt(0.28, stop2);
    borderGrad.setColorAt(0.52, stop3);
    borderGrad.setColorAt(0.78, stop4);
    borderGrad.setColorAt(1.0, stop5);

    QPen borderPen(QBrush(borderGrad), 1.0);
    borderPen.setCosmetic(true);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kRadius, kRadius);
}

void LayerRowWidget::drawIndentLines(QPainter& p)
{
    int depthInt = qRound(m_animatedDepth);
    if (depthInt <= 0)
        return;

    auto& tm = ThemeManager::instance();
    const auto& c = tm.colors();
    int startX = indentStartX();
    int indentPer = tm.scaled(kIndentPerLevel);

    QColor lineColor = ThemeColors::withAlpha(c.textMuted, 25);
    QPen pen(lineColor, 1.0);
    pen.setCosmetic(true);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    for (int level = 0; level < depthInt; ++level) {
        int lineX = startX + level * indentPer + indentPer / 2;
        p.drawLine(lineX, 0, lineX, height());
    }
}

void LayerRowWidget::drawExpandArrow(QPainter& p, const QRect& r)
{
    if (!m_data || m_childDisclosureProgress <= 0.001)
        return;

    const auto& c = ThemeManager::instance().colors();
    qreal arrowOpacity = 0.5 + 0.3 * m_hoverProgress;
    if (m_hoveredZone == HitZone::ExpandArrow)
        arrowOpacity = 0.9;
    arrowOpacity *= m_childDisclosureProgress;

    p.save();
    const QRectF rf(r);
    p.translate(rf.center());
    p.scale(m_childDisclosureProgress, m_childDisclosureProgress);
    p.rotate(m_expandRotation);

    // Draw right-pointing arrow (rotates to down when expanded)
    qreal sz = rf.width() * 0.30;
    QPolygonF arrow;
    arrow << QPointF(-sz * 0.5, -sz) << QPointF(sz * 0.5, 0) << QPointF(-sz * 0.5, sz);

    QPen pen(ThemeColors::withAlpha(c.text, int(255 * arrowOpacity)), 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPolyline(arrow);
    p.restore();
}

void LayerRowWidget::drawThumbnail(QPainter& p, const QRect& r)
{
    if (!m_data)
        return;

    const auto& c = ThemeManager::instance().colors();

    QPainterPath thumbClip;
    thumbClip.addRoundedRect(QRectF(r), 3, 3);
    p.save();
    p.setClipPath(thumbClip);

    const bool isAdjustment = m_data->isAdjustment();
    if (!m_data->isGroup() && !isAdjustment) {
        p.setPen(Qt::NoPen);
        if (m_thumbnailLoading) {
            p.setBrush(c.surfaceAlt);
            p.drawRoundedRect(r, 3, 3);
        } else {
            // Base checkerboard under all previews (matches canvas transparency indicator, slightly
            // brightened).
            const QColor checkA = ThemeColors::adjustBrightness(c.canvas(), 1.10);
            const QColor checkB = ThemeColors::adjustBrightness(c.canvasGrid(), 1.10);
            p.setBrush(checkA);
            p.drawRoundedRect(r, 3, 3);

            const int checkSize = 4;
            for (int y = r.top(); y < r.bottom(); y += checkSize) {
                for (int x = r.left(); x < r.right(); x += checkSize) {
                    const bool useA
                        = (((x - r.left()) / checkSize) + ((y - r.top()) / checkSize)) % 2 == 0;
                    p.fillRect(x, y, checkSize, checkSize, useA ? checkA : checkB);
                }
            }
        }
    }

    // Draw type indicators using the same icons as the layer toolbar. Adjustment
    // layers deliberately have no preview: their icon is informational only.
    if (m_data->isGroup() || isAdjustment) {
        QColor iconCol;
        if (isAdjustment) {
            iconCol = c.textMuted;
        } else if (m_selected && m_primary) {
            iconCol = ThemeColors::interpolate(c.text, c.text, m_selectionProgress);
        } else {
            iconCol = ThemeColors::interpolate(c.textMuted, c.text, m_hoverProgress * 0.7);
        }
        if (!m_data->visible) {
            iconCol.setAlphaF(iconCol.alphaF() * hiddenBlend(m_visibilityProgress, 0.4));
        }

        const QSize iconSize = isAdjustment
            ? QSize(qMax(1, qRound(r.width() / 1.5)), qMax(1, qRound(r.height() / 1.5)))
            : r.size();
        QPixmap icon = ruwa::ui::core::IconProvider::instance().getPixmap(isAdjustment
                ? ruwa::ui::core::IconProvider::StandardIcon::AdjustmentLayer
                : ruwa::ui::core::IconProvider::StandardIcon::Folder,
            iconSize);
        if (!icon.isNull()) {
            QPixmap tinted(icon.size());
            tinted.fill(Qt::transparent);
            {
                QPainter ip(&tinted);
                ip.drawPixmap(0, 0, icon);
                ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
                ip.fillRect(tinted.rect(), iconCol);
            }
            const QPoint iconTopLeft = r.center() - QPoint(tinted.width() / 2, tinted.height() / 2);
            p.drawPixmap(iconTopLeft, tinted);
        }
    } else if (!m_thumbnailLoading && !m_data->thumbnail.isNull()) {
        // Keep source aspect ratio when fitting preview image (skip when loading — show only
        // loading indicator)
        QPixmap scaled
            = m_data->thumbnail.scaled(r.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int dx = (r.width() - scaled.width()) / 2;
        const int dy = (r.height() - scaled.height()) / 2;
        p.drawPixmap(r.topLeft() + QPoint(dx, dy), scaled);
    }
    p.restore();

    const qreal glowMix = isAdjustment
        ? 0.0
        : qBound<qreal>(0.0, m_thumbnailCtrlGlowProgress + m_thumbnailClickFlashProgress, 1.0);
    if (glowMix > 0.0) {
        const auto& tm = ThemeManager::instance();

        QColor accentA = ThemeColors::adjustBrightness(c.primary, c.isDark ? 1.10 : 0.96);
        QColor accentB = ThemeColors::adjustBrightness(c.primary, c.isDark ? 1.20 : 1.02);
        accentA.setAlphaF(
            (0.20 * m_thumbnailCtrlGlowProgress) + (0.18 * m_thumbnailClickFlashProgress));
        accentB.setAlphaF(
            (0.34 * m_thumbnailCtrlGlowProgress) + (0.40 * m_thumbnailClickFlashProgress));

        QRectF borderRect = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient glowGrad(borderRect.topLeft(), borderRect.bottomRight());
        glowGrad.setColorAt(0.0, accentA);
        glowGrad.setColorAt(1.0, accentB);

        QPen glowPen;
        glowPen.setBrush(glowGrad);
        glowPen.setWidthF(qMax(1.5, static_cast<qreal>(tm.scaled(2)) * 0.75));
        glowPen.setCosmetic(true);
        p.setPen(glowPen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(borderRect, 3, 3);

        if (m_thumbnailClickFlashProgress > 0.0) {
            QColor flash = c.text;
            flash.setAlphaF(0.16 * m_thumbnailClickFlashProgress);
            p.save();
            p.setClipPath(thumbClip);
            p.fillRect(r, flash);
            p.restore();
        }
    }

    // Thumbnail border (skip for groups — no background)
    if (!m_data->isGroup() && !isAdjustment) {
        QColor thumbBorder = ThemeColors::withAlpha(c.overlayColor, 20);
        p.setPen(QPen(thumbBorder, 0.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(r).adjusted(0.25, 0.25, -0.25, -0.25), 3, 3);
    }
}

void LayerRowWidget::drawMaskThumbnail(QPainter& p, const QRect& r)
{
    if (!maskSlotActive() || r.isEmpty() || !m_data) {
        return;
    }

    const auto& c = ThemeManager::instance().colors();

    // Rebuild the grayscale mask preview only when a real mask is present and its
    // content changed. While the mask is fading out (hasMask() already false) the
    // source tiles are gone, so we keep painting the last cached preview.
    if (hasMaskThumbnail()
        && (m_data->maskThumbnailDirty || m_maskThumbCache.isNull()
            || m_maskThumbCache.size() != r.size())) {
        const QImage img = maskPreviewImage(m_data, m_displayFrame, r.size());
        m_maskThumbCache = img.isNull() ? QPixmap() : QPixmap::fromImage(img);
        m_data->maskThumbnailDirty = false;
    }

    QPainterPath clip;
    clip.addRoundedRect(QRectF(r), 3, 3);
    p.save();
    p.setClipPath(clip);
    if (!m_maskThumbCache.isNull()) {
        p.drawPixmap(r, m_maskThumbCache);
    } else {
        p.fillRect(r, c.surfaceAlt);
    }
    p.restore();

    QColor thumbBorder = ThemeColors::withAlpha(c.overlayColor, 20);
    p.setPen(QPen(thumbBorder, 0.5));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(QRectF(r).adjusted(0.25, 0.25, -0.25, -0.25), 3, 3);
}

void LayerRowWidget::drawName(QPainter& p, const QRect& r)
{
    if (!m_data || m_renameEdit)
        return;

    const QFont font = nameDisplayFont();
    p.setFont(font);
    p.setPen(nameDisplayColor());

    const QRect textRect = nameDisplayRect();

    QString elidedName = p.fontMetrics().elidedText(m_data->name, Qt::ElideRight, textRect.width());
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);
}

void LayerRowWidget::drawButtonHoverBg(QPainter& p, const QRect& r, bool hovered)
{
    if (!hovered)
        return;
    const auto& c = ThemeManager::instance().colors();
    QColor hbg = c.overlayHover();
    p.setPen(Qt::NoPen);
    p.setBrush(hbg);
    p.drawRoundedRect(r.adjusted(-1, -1, 1, 1), 3, 3);
}

QPixmap LayerRowWidget::tintPixmap(const QPixmap& icon, const QColor& color)
{
    QPixmap tinted(icon.size());
    tinted.fill(Qt::transparent);
    {
        QPainter ip(&tinted);
        ip.drawPixmap(0, 0, icon);
        ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
        ip.fillRect(tinted.rect(), color);
    }
    return tinted;
}

void LayerRowWidget::drawAlphaLockButton(QPainter& p, const QRect& r)
{
    if (!hasAlphaLockIcon() || r.isEmpty())
        return;

    bool hovered = (m_hoveredZone == HitZone::AlphaLockBtn);
    drawButtonHoverBg(p, r, hovered);

    qreal opacity = 0.55 + 0.3 * m_hoverProgress;
    if (hovered)
        opacity = qMin(1.0, opacity + 0.25);

    QPixmap icon = ruwa::ui::core::IconProvider::instance().getPixmap(
        ruwa::ui::core::IconProvider::StandardIcon::Alpha, r.size());
    if (!icon.isNull()) {
        QColor iconColor = ThemeManager::instance().colors().text;
        iconColor.setAlphaF(opacity);
        p.drawPixmap(r.topLeft(), tintPixmap(icon, iconColor));
    }
}

void LayerRowWidget::drawLockButton(QPainter& p, const QRect& r)
{
    if (!hasLockIcon() || r.isEmpty())
        return;

    bool hovered = (m_hoveredZone == HitZone::LockBtn);
    drawButtonHoverBg(p, r, hovered);

    qreal opacity = 0.55 + 0.3 * m_hoverProgress;
    if (hovered)
        opacity = qMin(1.0, opacity + 0.25);

    QPixmap icon = ruwa::ui::core::IconProvider::instance().getPixmap(
        ruwa::ui::core::IconProvider::StandardIcon::Lock, r.size());
    if (!icon.isNull()) {
        QColor iconColor = ThemeManager::instance().colors().text;
        iconColor.setAlphaF(opacity);
        p.drawPixmap(r.topLeft(), tintPixmap(icon, iconColor));
    }
}

void LayerRowWidget::drawMeta(QPainter& p, const QRect& r)
{
    if (!m_data)
        return;

    const auto& c = ThemeManager::instance().colors();
    auto& tm = ThemeManager::instance();

    QColor metaCol = ThemeColors::interpolate(c.textMuted, c.textMuted, m_selectionProgress);
    metaCol.setAlphaF((m_selected ? 0.75 : 0.52)
        * (m_data->visible ? 1.0 : hiddenBlend(m_visibilityProgress, 0.6)));

    QFont metaFont = p.font();
    metaFont.setFamily(c.fonts.uiFont);
    metaFont.setPixelSize(tm.scaledFontSize(8));
    p.setFont(metaFont);

    QFont nameMeasureFont = metaFont;
    nameMeasureFont.setPixelSize(tm.scaledFontSize(m_data->isGroup() ? 9 : 10));
    if (m_data->isGroup()) {
        nameMeasureFont.setBold(true);
    }
    const int nameLineHeight = QFontMetrics(nameMeasureFont).height();
    const int metaLineHeight = p.fontMetrics().height();
    const int lineGap = 0;
    const int stackedHeight = nameLineHeight + lineGap + metaLineHeight;
    const int blockTop = r.top() + qMax(0, (r.height() - stackedHeight) / 2);
    const int bottomLineY = blockTop + nameLineHeight + lineGap;
    QRect metaRect(r.left(), bottomLineY, r.width(),
        qMin(qMax(metaLineHeight, 1), qMax(0, r.bottom() - bottomLineY + 1)));

    const QFontMetrics metaMetrics(metaFont);
    const int opacityPercentValue = qRound(qBound(0.0, m_data->opacity, 1.0) * 100.0);
    const QString inlineMetaText
        = QString("%1 · %2%").arg(blendModeName(m_data->blendMode)).arg(opacityPercentValue);
    const QString inlineBadgeText = typeBadgeText();

    if (inlineBadgeText.isEmpty()) {
        p.setPen(metaCol);
        const QString elidedMeta
            = metaMetrics.elidedText(inlineMetaText, Qt::ElideRight, metaRect.width());
        p.drawText(metaRect, Qt::AlignLeft | Qt::AlignVCenter, elidedMeta);
        return;
    }

    QFont inlineBadgeFont = metaFont;
    inlineBadgeFont.setPixelSize(tm.scaledFontSize(6));
    inlineBadgeFont.setBold(true);
    const QFontMetrics inlineBadgeMetrics(inlineBadgeFont);

    const QString separatorText = QStringLiteral("·");
    const int separatorGap = tm.scaled(2);
    const int separatorWidth = metaMetrics.horizontalAdvance(separatorText);
    const int badgeGap = tm.scaled(2);
    const int badgePadX = tm.scaled(4);
    const int badgePadY = 0;
    const int inlineBadgeHeight
        = qMin(qMax(tm.scaled(10), inlineBadgeMetrics.height() + badgePadY * 2),
            qMax(tm.scaled(8), metaRect.height() - tm.scaled(4)));
    const int inlineBadgeWidth
        = inlineBadgeMetrics.horizontalAdvance(inlineBadgeText) + badgePadX * 2;
    const int reservedInlineWidth = separatorGap + separatorWidth + badgeGap + inlineBadgeWidth;

    QRect inlineTextRect = metaRect;
    inlineTextRect.setWidth(qMax(0, metaRect.width() - reservedInlineWidth));

    p.setPen(metaCol);
    const QString elidedMeta
        = metaMetrics.elidedText(inlineMetaText, Qt::ElideRight, inlineTextRect.width());
    p.drawText(inlineTextRect, Qt::AlignLeft | Qt::AlignVCenter, elidedMeta);

    const int drawnMetaWidth
        = qMin(inlineTextRect.width(), metaMetrics.horizontalAdvance(elidedMeta));
    int inlineX = inlineTextRect.left() + drawnMetaWidth + separatorGap;
    const QRect separatorRect(inlineX, metaRect.top(), separatorWidth, metaRect.height());
    p.drawText(separatorRect, Qt::AlignLeft | Qt::AlignVCenter, separatorText);

    inlineX = separatorRect.right() + 1 + badgeGap;
    const int availableInlineBadgeWidth = qMax(0, metaRect.right() - inlineX + 1);
    const QRect inlineBadgeRect(inlineX,
        metaRect.top() + (metaRect.height() - inlineBadgeHeight) / 2,
        qMin(inlineBadgeWidth, availableInlineBadgeWidth), inlineBadgeHeight);
    if (inlineBadgeRect.width() > 0) {
        QColor badgeFill = metaCol;
        badgeFill.setAlphaF(qMin(1.0, metaCol.alphaF() * 0.9));

        const int badgeTextWidth = qMax(0, inlineBadgeRect.width() - badgePadX * 2);
        const QString elidedBadge
            = inlineBadgeMetrics.elidedText(inlineBadgeText, Qt::ElideRight, badgeTextWidth);

        const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
        QImage badgeLayer(QSize(qMax(1, qRound(inlineBadgeRect.width() * dpr)),
                              qMax(1, qRound(inlineBadgeRect.height() * dpr))),
            QImage::Format_ARGB32_Premultiplied);
        badgeLayer.setDevicePixelRatio(dpr);
        badgeLayer.fill(Qt::transparent);

        QPainter badgePainter(&badgeLayer);
        badgePainter.setRenderHint(QPainter::Antialiasing);
        badgePainter.setRenderHint(QPainter::TextAntialiasing);
        badgePainter.setPen(Qt::NoPen);
        badgePainter.setBrush(badgeFill);
        badgePainter.drawRoundedRect(
            QRectF(0, 0, inlineBadgeRect.width(), inlineBadgeRect.height()), tm.scaled(3),
            tm.scaled(3));

        badgePainter.setFont(inlineBadgeFont);
        badgePainter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        badgePainter.setPen(Qt::white);
        badgePainter.drawText(QRect(0, 0, inlineBadgeRect.width(), inlineBadgeRect.height())
                                  .adjusted(badgePadX, 0, -badgePadX, 0),
            Qt::AlignCenter, elidedBadge);
        badgePainter.end();

        p.drawImage(inlineBadgeRect.topLeft(), badgeLayer);
    }
}

void LayerRowWidget::drawEyeButton(QPainter& p, const QRect& r)
{
    if (!m_data)
        return;

    const auto& c = ThemeManager::instance().colors();
    bool hovered = (m_hoveredZone == HitZone::EyeBtn);
    bool visible = m_data->visible;
    const qreal iconBlend = hiddenBlend(m_visibilityProgress, 0.35);

    drawButtonHoverBg(p, r, hovered);

    qreal opacity = visible ? (0.55 + 0.3 * m_hoverProgress) : (0.35 + 0.2 * m_visibilityProgress);
    if (hovered)
        opacity = qMin(1.0, opacity + 0.25);

    const struct {
        ruwa::ui::core::IconProvider::StandardIcon type;
        qreal alpha;
    } eyeIcons[]
        = { { ruwa::ui::core::IconProvider::StandardIcon::Eye, opacity * m_visibilityProgress },
              { ruwa::ui::core::IconProvider::StandardIcon::EyeDeactivated,
                  opacity * 0.85 * (1.0 - m_visibilityProgress + (visible ? 0.0 : 0.15)) } };

    for (const auto& iconSpec : eyeIcons) {
        if (iconSpec.alpha <= 0.001) {
            continue;
        }

        QPixmap icon = ruwa::ui::core::IconProvider::instance().getPixmap(iconSpec.type, r.size());
        if (icon.isNull()) {
            continue;
        }

        QColor iconColor = c.text;
        iconColor.setAlphaF(iconSpec.alpha * iconBlend);
        p.drawPixmap(r.topLeft(), tintPixmap(icon, iconColor));
    }
}

void LayerRowWidget::updateRightExpandButtonsGeometry()
{
    // Buttons are drawn in paintEvent; keep child widgets hidden for future click wiring
    BaseAnimatedButton* buttons[] = { m_rightExpandBtn1, m_rightExpandBtn2, m_rightExpandBtn3 };
    for (int i = 0; i < 3; ++i) {
        if (buttons[i])
            buttons[i]->hide();
    }
}

void LayerRowWidget::updateThumbnailLoadingIndicator()
{
    if (!m_thumbnailLoadingIndicator) {
        return;
    }

    const bool showIndicator
        = m_thumbnailLoading && m_data && !m_data->isGroup() && !m_data->isAdjustment();
    if (!showIndicator) {
        m_thumbnailLoadingIndicator->stop();
        m_thumbnailLoadingIndicator->hide();
        return;
    }

    const QRect thumb = thumbnailRect();
    if (thumb.width() <= 0 || thumb.height() <= 0) {
        m_thumbnailLoadingIndicator->stop();
        m_thumbnailLoadingIndicator->hide();
        return;
    }
    const int indicatorSize = qMax(10, qMin(thumb.width(), thumb.height()) - 8);
    m_thumbnailLoadingIndicator->setFixedSize(indicatorSize, indicatorSize);
    m_thumbnailLoadingIndicator->move(
        thumb.center().x() - indicatorSize / 2, thumb.center().y() - indicatorSize / 2);
    m_thumbnailLoadingIndicator->setAccentColor(ThemeManager::instance().colors().primary);
    m_thumbnailLoadingIndicator->show();
    m_thumbnailLoadingIndicator->raise();
    if (!m_thumbnailLoadingIndicator->isRunning()) {
        m_thumbnailLoadingIndicator->start();
    }
}

void LayerRowWidget::drawRightExpandButtons(QPainter& p)
{
    if (m_rightExpandProgress <= 0.0)
        return;

    const QList<QRect> rects = rightExpandButtonRects();
    if (rects.size() < 3)
        return;

    const auto& c = ThemeManager::instance().colors();
    const auto& tm = ThemeManager::instance();
    const qreal radius = 4.0;
    // Left: select contents, Middle: duplicate, Right: delete
    static const std::array<ruwa::ui::core::IconProvider::StandardIcon, 3> icons
        = { ruwa::ui::core::IconProvider::StandardIcon::SquareSelection,
              ruwa::ui::core::IconProvider::StandardIcon::Duplicate,
              ruwa::ui::core::IconProvider::StandardIcon::Trash };
    static const std::array<HitZone, 3> zones
        = { HitZone::RightExpandBtn1, HitZone::RightExpandBtn2, HitZone::RightExpandBtn3 };

    for (int i = 0; i < 3; ++i) {
        const QRect& r = rects[i];
        const bool hovered = (m_hoveredZone == zones[i]);
        QRectF rf = QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5);

        p.save();
        p.setOpacity(m_rightExpandProgress);

        // Gradient border
        QLinearGradient borderGrad(rf.left(), rf.top(), rf.right(), rf.bottom());
        QColor borderTop = c.border;
        borderTop.setAlphaF(0.5);
        QColor borderBottom = c.border;
        borderBottom.setAlphaF(0.2);
        if (hovered) {
            QColor hTop = c.primary;
            hTop.setAlphaF(0.4);
            QColor hBottom = c.primary;
            hBottom.setAlphaF(0.15);
            borderTop = ruwa::ui::core::ThemeColors::interpolate(borderTop, hTop, m_hoverProgress);
            borderBottom
                = ruwa::ui::core::ThemeColors::interpolate(borderBottom, hBottom, m_hoverProgress);
        }
        borderGrad.setColorAt(0.0, borderTop);
        borderGrad.setColorAt(1.0, borderBottom);
        p.setPen(QPen(QBrush(borderGrad), 1.0));
        p.setBrush(c.surfaceAlt);
        p.drawRoundedRect(rf, radius, radius);

        // Hover overlay
        if (hovered && m_hoverProgress > 0.0) {
            QColor hover = c.surfaceHover();
            hover.setAlphaF(hover.alphaF() * m_hoverProgress);
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(rf.adjusted(1, 1, -1, -1), radius - 0.5, radius - 0.5);
        }

        // Icon
        QColor iconColor = ruwa::ui::core::ThemeColors::interpolate(
            c.textMuted, c.text, hovered ? m_hoverProgress : 0.0);
        const int iconSz = qMax(12, tm.scaled(14));
        QPixmap icon
            = ruwa::ui::core::IconProvider::instance().getPixmap(icons[i], QSize(iconSz, iconSz));
        if (!icon.isNull()) {
            QPixmap tinted(icon.size());
            tinted.fill(Qt::transparent);
            QPainter ip(&tinted);
            ip.drawPixmap(0, 0, icon);
            ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
            ip.fillRect(tinted.rect(), iconColor);
            ip.end();
            const int dx = r.x() + (r.width() - iconSz) / 2;
            const int dy = r.y() + (r.height() - iconSz) / 2;
            p.drawPixmap(dx, dy, tinted);
        }

        p.restore();
    }
}

void LayerRowWidget::drawClippingHandle(QPainter& p)
{
    if (m_clipOffsetProgress <= 0.0 || !m_data || m_data->isBackground()) {
        return;
    }

    auto& tm = ThemeManager::instance();
    const auto& c = ThemeManager::instance().colors();
    const QRect content = contentRect();
    const int indentWidthPx = qRound(m_clipOffsetProgress * tm.scaled(kClipIndent));
    if (indentWidthPx <= 0) {
        return;
    }

    const int handleSize = qMin(indentWidthPx, tm.scaled(kClipBadgeSize + 2));
    const int handleX
        = indentStartX() + indentWidth() + qMax(0, qRound((indentWidthPx - handleSize) * 0.5));
    const int handleY = content.top() + (content.height() - handleSize) / 2;
    const QRect r(handleX, handleY, handleSize, handleSize);
    if (r.width() <= 0 || r.height() <= 0) {
        return;
    }

    QColor bg = c.primary;
    bg.setAlpha(qRound(255.0 * m_clipOffsetProgress));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

    QPixmap icon = IconProvider::instance().getPixmap(
        IconProvider::StandardIcon::ArrowDown, QSize(tm.scaled(18), tm.scaled(18)));
    if (icon.isNull()) {
        return;
    }

    // Trim transparent icon padding so glyph aligns to the geometric center.
    QImage iconImg = icon.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int minX = iconImg.width();
    int minY = iconImg.height();
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < iconImg.height(); ++y) {
        const QRgb* scan = reinterpret_cast<const QRgb*>(iconImg.constScanLine(y));
        for (int x = 0; x < iconImg.width(); ++x) {
            if (qAlpha(scan[x]) > 0) {
                minX = qMin(minX, x);
                minY = qMin(minY, y);
                maxX = qMax(maxX, x);
                maxY = qMax(maxY, y);
            }
        }
    }
    if (maxX >= minX && maxY >= minY) {
        icon = icon.copy(QRect(QPoint(minX, minY), QPoint(maxX, maxY)));
    }

    const int iconInset = qMax(1, qRound(qMin(r.width(), r.height()) * 0.15));
    const int iconSide = qMax(1, qMin(r.width(), r.height()) - iconInset * 2);
    QPixmap scaled = icon.scaled(iconSide, iconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QColor iconColor = c.textOnPrimary();
    iconColor.setAlpha(qRound(255.0 * m_clipOffsetProgress));
    QPixmap tinted(scaled.size());
    tinted.fill(Qt::transparent);
    {
        QPainter ip(&tinted);
        ip.drawPixmap(0, 0, scaled);
        ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
        ip.fillRect(tinted.rect(), iconColor);
    }

    const int iconX = r.x() + (r.width() - tinted.width()) / 2;
    const int iconY = r.y() + (r.height() - tinted.height()) / 2;
    p.drawPixmap(iconX, iconY, tinted);
}

namespace {

ruwa::core::layers::LayerModel* layerModelForLayerRow(const LayerRowWidget* row)
{
    if (!row) {
        return nullptr;
    }
    for (QWidget* w = row->parentWidget(); w; w = w->parentWidget()) {
        if (auto* lv = qobject_cast<LayerListView*>(w)) {
            return lv->model();
        }
    }
    return nullptr;
}

constexpr auto kKeySimpleColorActions = "simpleColorActions";
constexpr auto kKeyChecked = "checked";
constexpr auto kKeyColorRgba = "colorRgba";
constexpr auto kKeySeparatorBefore = "separatorBefore";

enum LayerRowContextAction : int {
    CtxRename = 1,
    CtxDuplicate = 2,
    CtxDelete = 3,
    CtxToggleVisibility = 4,
    CtxQuickClippingMask = 5,
    CtxClearLayer = 6,
    CtxToggleAlphaLock = 7,
    CtxToggleLayerLock = 8,
    CtxRasterizeLayer = 9,
    CtxApplyMask = 10,
    CtxInvertMask = 11,
    CtxApplyEffects = 12,
    CtxLayerColorBase = 100,
};

bool isLayerColorAction(int actionId)
{
    return actionId >= CtxLayerColorBase
        && actionId < CtxLayerColorBase + static_cast<int>(displayColorPalette().size());
}

quint8 layerColorIndexForAction(int actionId)
{
    return static_cast<quint8>(qBound(
        0, actionId - CtxLayerColorBase, static_cast<int>(LayerData::kMaxDisplayColorIndex)));
}

} // namespace

void LayerRowWidget::prepareContextMenuInteraction()
{
    if (!m_data) {
        return;
    }
    emit clicked(m_data->id, Qt::NoModifier);
}

ContextMenuType LayerRowWidget::contextMenuType() const
{
    return m_data ? ContextMenuType::SimpleActions : ContextMenuType::None;
}

QVariantMap LayerRowWidget::contextMenuContext() const
{
    QVariantMap ctx;
    if (!m_data) {
        return ctx;
    }

    QVariantList actions;

    auto appendSep = [&actions]() {
        QVariantMap s;
        s.insert(QStringLiteral("separator"), true);
        actions.append(s);
    };

    QVariantMap rename;
    rename.insert(QStringLiteral("id"), CtxRename);
    rename.insert(QStringLiteral("text"), tr("Rename"));
    rename.insert(QStringLiteral("danger"), false);
    rename.insert(
        QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Edit));
    actions.append(rename);

    if (!m_data->isBackground()) {
        QVariantMap dup;
        dup.insert(QStringLiteral("id"), CtxDuplicate);
        dup.insert(QStringLiteral("text"), tr("Duplicate"));
        dup.insert(QStringLiteral("danger"), false);
        dup.insert(QStringLiteral("standardIcon"),
            static_cast<int>(IconProvider::StandardIcon::Duplicate));
        actions.append(dup);

        QVariantMap del;
        del.insert(QStringLiteral("id"), CtxDelete);
        del.insert(QStringLiteral("text"), tr("Delete"));
        del.insert(QStringLiteral("danger"), true);
        del.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Trash));
        actions.append(del);
    }

    appendSep();

    QVariantMap vis;
    vis.insert(QStringLiteral("id"), CtxToggleVisibility);
    vis.insert(QStringLiteral("text"), m_data->visible ? tr("Hide") : tr("Show"));
    vis.insert(QStringLiteral("danger"), false);
    vis.insert(QStringLiteral("standardIcon"),
        static_cast<int>(m_data->visible ? IconProvider::StandardIcon::EyeDeactivated
                                         : IconProvider::StandardIcon::Eye));
    actions.append(vis);

    appendSep();

    QVariantMap rasterize;
    rasterize.insert(QStringLiteral("id"), CtxRasterizeLayer);
    rasterize.insert(QStringLiteral("text"), tr("Rasterize"));
    rasterize.insert(QStringLiteral("danger"), false);
    rasterize.insert(
        QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Camera));
    rasterize.insert(QStringLiteral("enabled"), m_data->isIsolatedPixelLayer() || m_data->isText());
    actions.append(rasterize);

    // Bake the layer's effect chain into its pixels and clear the chain.
    if (m_data->isRaster() && !m_data->effects.isEmpty()) {
        appendSep();

        QVariantMap applyEffects;
        applyEffects.insert(QStringLiteral("id"), CtxApplyEffects);
        applyEffects.insert(QStringLiteral("text"), tr("Apply all effects"));
        applyEffects.insert(QStringLiteral("danger"), false);
        applyEffects.insert(QStringLiteral("standardIcon"),
            static_cast<int>(IconProvider::StandardIcon::AdjustmentLayer));
        actions.append(applyEffects);
    }

    // Bake a layer mask into its pixels and remove the mask.
    if (m_data->hasMask() && !m_data->isGroup()) {
        appendSep();

        QVariantMap applyMask;
        applyMask.insert(QStringLiteral("id"), CtxApplyMask);
        applyMask.insert(QStringLiteral("text"), tr("Apply mask"));
        applyMask.insert(QStringLiteral("danger"), false);
        applyMask.insert(QStringLiteral("standardIcon"),
            static_cast<int>(IconProvider::StandardIcon::LayerMask));
        applyMask.insert(QStringLiteral("enabled"), m_data->isRaster());
        actions.append(applyMask);

        // Invert the whole mask: reveal <-> hide across every tile and the
        // infinite background.
        QVariantMap invertMask;
        invertMask.insert(QStringLiteral("id"), CtxInvertMask);
        invertMask.insert(QStringLiteral("text"), tr("Invert mask"));
        invertMask.insert(QStringLiteral("danger"), false);
        invertMask.insert(QStringLiteral("standardIcon"),
            static_cast<int>(IconProvider::StandardIcon::LayerMask));
        invertMask.insert(QStringLiteral("enabled"), m_data->isRaster());
        actions.append(invertMask);
    }

    if (!m_data->isBackground()) {
        appendSep();

        QVariantMap clip;
        clip.insert(QStringLiteral("id"), CtxQuickClippingMask);
        clip.insert(QStringLiteral("text"), tr("Clipping mask"));
        clip.insert(QStringLiteral("danger"), false);
        clip.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Crop));
        if (ruwa::core::layers::LayerModel* model = layerModelForLayerRow(this)) {
            clip.insert(
                QStringLiteral("enabled"), model->hasQuickClippingMaskTargetBelow(m_data->id));
        } else {
            clip.insert(QStringLiteral("enabled"), false);
        }
        actions.append(clip);

        if (m_data->isPixelLayer()) {
            QVariantMap clr;
            clr.insert(QStringLiteral("id"), CtxClearLayer);
            clr.insert(QStringLiteral("text"), tr("Clear layer"));
            clr.insert(QStringLiteral("danger"), false);
            clr.insert(QStringLiteral("standardIcon"),
                static_cast<int>(IconProvider::StandardIcon::Eraser));
            clr.insert(QStringLiteral("enabled"), m_data->isRaster());
            actions.append(clr);
        }

        appendSep();

        if (m_data->isPixelLayer() || m_data->isGroup()) {
            QVariantMap alpha;
            alpha.insert(QStringLiteral("id"), CtxToggleAlphaLock);
            alpha.insert(
                QStringLiteral("text"), m_data->alphaLock ? tr("Unlock alpha") : tr("Lock alpha"));
            alpha.insert(QStringLiteral("danger"), false);
            alpha.insert(QStringLiteral("standardIcon"),
                static_cast<int>(IconProvider::StandardIcon::Alpha));
            alpha.insert(QStringLiteral("enabled"), m_data->isRaster());
            actions.append(alpha);
        }

        QVariantMap lock;
        lock.insert(QStringLiteral("id"), CtxToggleLayerLock);
        lock.insert(QStringLiteral("text"), m_data->locked ? tr("Unlock layer") : tr("Lock layer"));
        lock.insert(QStringLiteral("danger"), false);
        lock.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Lock));
        actions.append(lock);
    }

    ctx.insert(QStringLiteral("simpleActions"), QVariant::fromValue(actions));

    QVariantList colorActions;
    const auto& palette = displayColorPalette();
    for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
        QVariantMap colorAction;
        colorAction.insert(QStringLiteral("id"), CtxLayerColorBase + i);
        colorAction.insert(
            QLatin1String(kKeyChecked), m_data->displayColorIndex == static_cast<quint8>(i));
        colorAction.insert(QLatin1String(kKeyColorRgba), palette[static_cast<size_t>(i)].rgba());
        if (i == 1) {
            colorAction.insert(QLatin1String(kKeySeparatorBefore), true);
        }
        colorActions.append(colorAction);
    }
    ctx.insert(QLatin1String(kKeySimpleColorActions), colorActions);

    return ctx;
}

void LayerRowWidget::onSimpleContextAction(int actionId)
{
    if (!m_data) {
        return;
    }
    if (isLayerColorAction(actionId)) {
        if (auto* model = layerModelForLayerRow(this)) {
            model->setLayerDisplayColorIndex(m_data->id, layerColorIndexForAction(actionId));
        }
        return;
    }
    switch (actionId) {
    case CtxRename:
        startRename();
        break;
    case CtxDuplicate:
        emit rightExpandDuplicateClicked(m_data->id);
        break;
    case CtxDelete:
        emit rightExpandDeleteClicked(m_data->id);
        break;
    case CtxToggleVisibility:
        emit visibilityToggled(m_data->id);
        break;
    case CtxQuickClippingMask:
        emit quickClippingMaskRequested(m_data->id);
        break;
    case CtxClearLayer:
        if (!m_data->isRaster()) {
            break;
        }
        emit clearLayerPixelsRequested(m_data->id);
        break;
    case CtxToggleAlphaLock:
        if (!m_data->isRaster()) {
            break;
        }
        emit toggleAlphaLockRequested(m_data->id);
        break;
    case CtxToggleLayerLock:
        if (m_data->isBackground()) {
            break;
        }
        emit toggleLayerLockRequested(m_data->id);
        break;
    case CtxRasterizeLayer:
        if (!m_data->isIsolatedPixelLayer() && !m_data->isText()) {
            break;
        }
        emit rasterizeSmartLayerRequested(m_data->id);
        break;
    case CtxApplyMask:
        if (!m_data->hasMask() || !m_data->isRaster()) {
            break;
        }
        emit applyMaskRequested(m_data->id);
        break;
    case CtxInvertMask:
        if (!m_data->hasMask() || !m_data->isRaster()) {
            break;
        }
        emit invertMaskRequested(m_data->id);
        break;
    case CtxApplyEffects:
        if (!m_data->isRaster() || m_data->effects.isEmpty()) {
            break;
        }
        emit applyEffectsRequested(m_data->id);
        break;
    default:
        break;
    }
}

} // namespace ruwa::ui::widgets
