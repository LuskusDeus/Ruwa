// SPDX-License-Identifier: MPL-2.0

// LayersPanel.cpp
#include "LayersPanel.h"

#include "features/layers/model/BlendModeUtils.h"
#include "shared/clipboard/EditClipboard.h"
#include "shared/tiles/TileGridClone.h"
#include "shared/undo/AddMaskCommand.h"
#include "shared/undo/RemoveMaskCommand.h"
#include "shared/undo/SetMaskCommand.h"
#include "shared/undo/LayerAddCommand.h"
#include "shared/undo/LayerPropertyCommand.h"
#include "shared/undo/LayerRemoveCommand.h"
#include "shared/undo/UndoManager.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/OpacitySliderWidget.h"
#include "shared/widgets/layout/AnimatedFlowWidget.h"
#include "shared/widgets/overlays/ToolTipController.h"
#include "shared/widgets/reorderlist/ListDragDrop.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "shared/tiles/TilePixelAccess.h"
#include "shared/style/AnimationPolicy.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QCursor>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <QEvent>
#include <QImage>
#include <QWidget>
#include <QTimer>
#include <QSet>
#include <QList>
#include <QtConcurrent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::workspace {

using namespace ruwa::core::layers;

namespace {

constexpr int kGroupInsertAnimationMs = 300;

struct GroupedLayerMoveRecord {
    LayerId layerId;
    LayerId oldParentId;
    int oldIndex = -1;
    int newIndex = -1;
};

// One layer moved by a drag & drop reorder: where it came from, and the exact
// index handed to moveLayer() when it landed.
struct LayerReorderRecord {
    LayerId layerId;
    LayerId oldParentId;
    int oldIndex = -1;
    int newIndex = -1;
};

struct MergeDownCandidate {
    LayerId sourceId;
    LayerId targetId;
    LayerId parentId;
    int sourceIndex = -1;
    int targetIndex = -1;

    bool isValid() const
    {
        return !sourceId.isNull() && !targetId.isNull() && sourceIndex >= 0 && targetIndex >= 0;
    }
};

int rootLayerIndex(const LayerModel& model, const LayerData* layer)
{
    if (!layer) {
        return -1;
    }

    const auto& roots = model.rootLayers();
    for (int i = 0; i < roots.size(); ++i) {
        if (roots[i].get() == layer) {
            return i;
        }
    }
    return -1;
}

QList<LayerData*> collectGroupableSelectionRoots(const LayerModel& model)
{
    const auto selectedIds = model.selectedLayerIds();
    if (selectedIds.isEmpty()) {
        return {};
    }

    const QList<LayerData*> flat = model.allLayersFlattened();
    QList<LayerData*> selectedRoots;
    for (LayerData* layer : flat) {
        if (!layer || layer->isBackground() || !selectedIds.contains(layer->id)) {
            continue;
        }

        bool hasSelectedAncestor = false;
        for (LayerData* ancestor = layer->parent; ancestor; ancestor = ancestor->parent) {
            if (selectedIds.contains(ancestor->id)) {
                hasSelectedAncestor = true;
                break;
            }
        }

        if (!hasSelectedAncestor) {
            selectedRoots.append(layer);
        }
    }

    if (selectedRoots.isEmpty()) {
        return {};
    }

    LayerData* firstRoot = selectedRoots.first();
    QList<LayerData*> block;

    if (firstRoot->parent) {
        const auto& siblings = firstRoot->parent->children;
        const int startIndex = firstRoot->indexInParent();
        for (int i = startIndex; i < siblings.size(); ++i) {
            LayerData* sibling = siblings[i].get();
            if (selectedIds.contains(sibling->id)) {
                block.append(sibling);
                continue;
            }

            if (!block.isEmpty()) {
                break;
            }
        }
    } else {
        const auto& roots = model.rootLayers();
        const int startIndex = rootLayerIndex(model, firstRoot);
        for (int i = startIndex; i >= 0 && i < roots.size(); ++i) {
            LayerData* sibling = roots[i].get();
            if (!sibling || sibling->isBackground()) {
                if (!block.isEmpty()) {
                    break;
                }
                continue;
            }

            if (selectedIds.contains(sibling->id)) {
                block.append(sibling);
                continue;
            }

            if (!block.isEmpty()) {
                break;
            }
        }
    }

    return block.isEmpty() ? QList<LayerData*> { firstRoot } : block;
}

qint64 packTileKey(const aether::TileKey& key)
{
    return (static_cast<qint64>(static_cast<quint32>(key.x)) << 32) | static_cast<quint32>(key.y);
}

aether::TileKey unpackTileKey(qint64 packedKey)
{
    return { static_cast<qint32>(static_cast<quint32>(packedKey >> 32)),
        static_cast<qint32>(static_cast<quint32>(packedKey & 0xffffffffu)) };
}

using Float3 = std::array<float, 3>;
using Float4 = std::array<float, 4>;

constexpr Float3 kBlendLumaWeights { 0.299f, 0.587f, 0.114f };

float clampUnit(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float blendLuminance(const Float3& color)
{
    return color[0] * kBlendLumaWeights[0] + color[1] * kBlendLumaWeights[1]
        + color[2] * kBlendLumaWeights[2];
}

float blendSaturation(const Float3& color)
{
    return std::max({ color[0], color[1], color[2] }) - std::min({ color[0], color[1], color[2] });
}

float hueToRgb(float p, float q, float t)
{
    if (t < 0.0f)
        t += 1.0f;
    if (t > 1.0f)
        t -= 1.0f;
    if (t < 1.0f / 6.0f)
        return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f)
        return q;
    if (t < 2.0f / 3.0f)
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
}

Float3 rgbToHsl(const Float3& color)
{
    const float cMax = std::max({ color[0], color[1], color[2] });
    const float cMin = std::min({ color[0], color[1], color[2] });
    const float delta = cMax - cMin;
    float hue = 0.0f;
    float saturation = 0.0f;
    const float lightness = 0.5f * (cMax + cMin);

    if (delta > 0.00001f) {
        saturation = (lightness > 0.5f) ? delta / (2.0f - cMax - cMin)
                                        : delta / std::max(cMax + cMin, 0.00001f);

        if (cMax == color[0]) {
            hue = (color[1] - color[2]) / delta + ((color[1] < color[2]) ? 6.0f : 0.0f);
        } else if (cMax == color[1]) {
            hue = (color[2] - color[0]) / delta + 2.0f;
        } else {
            hue = (color[0] - color[1]) / delta + 4.0f;
        }
        hue /= 6.0f;
    }

    return { hue, saturation, lightness };
}

Float3 hslToRgb(const Float3& hsl)
{
    const float hue = hsl[0] - std::floor(hsl[0]);
    const float saturation = clampUnit(hsl[1]);
    const float lightness = clampUnit(hsl[2]);

    if (saturation <= 0.00001f) {
        return { lightness, lightness, lightness };
    }

    const float q = (lightness < 0.5f) ? lightness * (1.0f + saturation)
                                       : lightness + saturation - lightness * saturation;
    const float p = 2.0f * lightness - q;

    return { hueToRgb(p, q, hue + 1.0f / 3.0f), hueToRgb(p, q, hue),
        hueToRgb(p, q, hue - 1.0f / 3.0f) };
}

Float3 blendColorDodge(const Float3& base, const Float3& src)
{
    Float3 result {};
    for (int i = 0; i < 3; ++i) {
        result[i]
            = (src[i] >= 1.0f) ? 1.0f : std::min(1.0f, base[i] / std::max(1.0f - src[i], 0.00001f));
    }
    return result;
}

float blendColorDodgeComponent(float base, float src)
{
    return (src >= 1.0f) ? 1.0f : std::min(1.0f, base / std::max(1.0f - src, 0.00001f));
}

Float3 blendColorBurn(const Float3& base, const Float3& src)
{
    Float3 result {};
    for (int i = 0; i < 3; ++i) {
        result[i] = (src[i] <= 0.0f)
            ? 0.0f
            : std::max(0.0f, 1.0f - (1.0f - base[i]) / std::max(src[i], 0.00001f));
    }
    return result;
}

float blendColorBurnComponent(float base, float src)
{
    return (src <= 0.0f) ? 0.0f : std::max(0.0f, 1.0f - (1.0f - base) / std::max(src, 0.00001f));
}

Float3 clipColor(const Float3& color)
{
    Float3 result = color;
    const float lum = blendLuminance(result);
    const float minComponent = std::min({ result[0], result[1], result[2] });
    const float maxComponent = std::max({ result[0], result[1], result[2] });

    if (minComponent < 0.0f) {
        const float scale = lum / std::max(lum - minComponent, 0.00001f);
        for (float& channel : result) {
            channel = lum + (channel - lum) * scale;
        }
    }
    if (maxComponent > 1.0f) {
        const float scale = (1.0f - lum) / std::max(maxComponent - lum, 0.00001f);
        for (float& channel : result) {
            channel = lum + (channel - lum) * scale;
        }
    }

    for (float& channel : result) {
        channel = clampUnit(channel);
    }
    return result;
}

Float3 setColorLuminance(const Float3& color, float luminance)
{
    Float3 shifted = color;
    const float delta = luminance - blendLuminance(color);
    for (float& channel : shifted) {
        channel += delta;
    }
    return clipColor(shifted);
}

Float3 setColorSaturation(const Float3& color, float saturation)
{
    const int minIndex
        = static_cast<int>(std::min_element(color.begin(), color.end()) - color.begin());
    const int maxIndex
        = static_cast<int>(std::max_element(color.begin(), color.end()) - color.begin());

    if (color[maxIndex] <= color[minIndex]) {
        return { 0.0f, 0.0f, 0.0f };
    }

    const int midIndex = 3 - minIndex - maxIndex;
    Float3 result { 0.0f, 0.0f, 0.0f };
    result[maxIndex] = saturation;
    result[midIndex]
        = ((color[midIndex] - color[minIndex]) * saturation) / (color[maxIndex] - color[minIndex]);
    return { clampUnit(result[0]), clampUnit(result[1]), clampUnit(result[2]) };
}

float blendVividLightComponent(float base, float src)
{
    return (src < 0.5f) ? blendColorBurnComponent(base, clampUnit(2.0f * src))
                        : blendColorDodgeComponent(base, clampUnit(2.0f * (src - 0.5f)));
}

Float3 blendModeColor(const Float3& base, const Float3& src, BlendMode mode)
{
    auto overlayComponent = [](float cb, float cs) {
        return (cb < 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
    };
    auto hardLightComponent = [](float cb, float cs) {
        return (cs < 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
    };
    auto softLightComponent = [](float cb, float cs) {
        const float d = (cb <= 0.25f) ? ((16.0f * cb - 12.0f) * cb + 4.0f) * cb : std::sqrt(cb);
        return (cs <= 0.5f) ? cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb)
                            : cb + (2.0f * cs - 1.0f) * (d - cb);
    };

    switch (mode) {
    case BlendMode::Normal:
    case BlendMode::Dissolve:
        return src;
    case BlendMode::Multiply:
        return { base[0] * src[0], base[1] * src[1], base[2] * src[2] };
    case BlendMode::Screen:
        return { base[0] + src[0] - base[0] * src[0], base[1] + src[1] - base[1] * src[1],
            base[2] + src[2] - base[2] * src[2] };
    case BlendMode::Overlay:
        return { overlayComponent(base[0], src[0]), overlayComponent(base[1], src[1]),
            overlayComponent(base[2], src[2]) };
    case BlendMode::SoftLight:
        return { softLightComponent(base[0], src[0]), softLightComponent(base[1], src[1]),
            softLightComponent(base[2], src[2]) };
    case BlendMode::HardLight:
        return { hardLightComponent(base[0], src[0]), hardLightComponent(base[1], src[1]),
            hardLightComponent(base[2], src[2]) };
    case BlendMode::ColorDodge:
        return blendColorDodge(base, src);
    case BlendMode::ColorBurn:
        return blendColorBurn(base, src);
    case BlendMode::Darken:
        return { std::min(base[0], src[0]), std::min(base[1], src[1]), std::min(base[2], src[2]) };
    case BlendMode::Lighten:
        return { std::max(base[0], src[0]), std::max(base[1], src[1]), std::max(base[2], src[2]) };
    case BlendMode::Difference:
        return { std::fabs(base[0] - src[0]), std::fabs(base[1] - src[1]),
            std::fabs(base[2] - src[2]) };
    case BlendMode::Exclusion:
        return { base[0] + src[0] - 2.0f * base[0] * src[0],
            base[1] + src[1] - 2.0f * base[1] * src[1],
            base[2] + src[2] - 2.0f * base[2] * src[2] };
    case BlendMode::LinearBurn:
        return { std::max(0.0f, base[0] + src[0] - 1.0f), std::max(0.0f, base[1] + src[1] - 1.0f),
            std::max(0.0f, base[2] + src[2] - 1.0f) };
    case BlendMode::DarkerColor:
        return (blendLuminance(src) <= blendLuminance(base)) ? src : base;
    case BlendMode::LinearDodge:
        return { std::min(1.0f, base[0] + src[0]), std::min(1.0f, base[1] + src[1]),
            std::min(1.0f, base[2] + src[2]) };
    case BlendMode::LighterColor:
        return (blendLuminance(src) >= blendLuminance(base)) ? src : base;
    case BlendMode::VividLight: {
        return { blendVividLightComponent(base[0], src[0]),
            blendVividLightComponent(base[1], src[1]), blendVividLightComponent(base[2], src[2]) };
    }
    case BlendMode::LinearLight:
        return { clampUnit(base[0] + 2.0f * src[0] - 1.0f),
            clampUnit(base[1] + 2.0f * src[1] - 1.0f), clampUnit(base[2] + 2.0f * src[2] - 1.0f) };
    case BlendMode::PinLight:
        return { (src[0] < 0.5f) ? std::min(base[0], 2.0f * src[0])
                                 : std::max(base[0], 2.0f * (src[0] - 0.5f)),
            (src[1] < 0.5f) ? std::min(base[1], 2.0f * src[1])
                            : std::max(base[1], 2.0f * (src[1] - 0.5f)),
            (src[2] < 0.5f) ? std::min(base[2], 2.0f * src[2])
                            : std::max(base[2], 2.0f * (src[2] - 0.5f)) };
    case BlendMode::HardMix: {
        const Float3 vivid = blendModeColor(base, src, BlendMode::VividLight);
        return { vivid[0] >= 0.5f ? 1.0f : 0.0f, vivid[1] >= 0.5f ? 1.0f : 0.0f,
            vivid[2] >= 0.5f ? 1.0f : 0.0f };
    }
    case BlendMode::Subtract:
        return { std::max(0.0f, base[0] - src[0]), std::max(0.0f, base[1] - src[1]),
            std::max(0.0f, base[2] - src[2]) };
    case BlendMode::Divide:
        return { (src[0] <= 0.0f) ? 1.0f : std::min(1.0f, base[0] / std::max(src[0], 0.001f)),
            (src[1] <= 0.0f) ? 1.0f : std::min(1.0f, base[1] / std::max(src[1], 0.001f)),
            (src[2] <= 0.0f) ? 1.0f : std::min(1.0f, base[2] / std::max(src[2], 0.001f)) };
    case BlendMode::Hue: {
        if (blendSaturation(src) <= 0.00001f) {
            return base;
        }
        return setColorLuminance(
            setColorSaturation(src, blendSaturation(base)), blendLuminance(base));
    }
    case BlendMode::Saturation: {
        return setColorLuminance(
            setColorSaturation(base, blendSaturation(src)), blendLuminance(base));
    }
    case BlendMode::Color: {
        return setColorLuminance(src, blendLuminance(base));
    }
    case BlendMode::Luminosity: {
        return setColorLuminance(base, blendLuminance(src));
    }
    }

    return src;
}

Float4 premultipliedPixelToFloat4(QRgb pixel)
{
    return { qRed(pixel) / 255.0f, qGreen(pixel) / 255.0f, qBlue(pixel) / 255.0f,
        qAlpha(pixel) / 255.0f };
}

QRgb float4ToPremultipliedPixel(const Float4& color)
{
    const float alpha = clampUnit(color[3]);
    const float red = std::clamp(color[0], 0.0f, alpha);
    const float green = std::clamp(color[1], 0.0f, alpha);
    const float blue = std::clamp(color[2], 0.0f, alpha);

    return qRgba(static_cast<int>(std::lround(red * 255.0f)),
        static_cast<int>(std::lround(green * 255.0f)), static_cast<int>(std::lround(blue * 255.0f)),
        static_cast<int>(std::lround(alpha * 255.0f)));
}

Float3 unpremultiplyColor(const Float4& color)
{
    if (color[3] <= 0.0f) {
        return { 0.0f, 0.0f, 0.0f };
    }

    return { color[0] / color[3], color[1] / color[3], color[2] / color[3] };
}

quint32 hashPixel(int x, int y)
{
    quint32 hash = static_cast<quint32>(x) * 1597334677u + static_cast<quint32>(y) * 3812015801u
        + 2246822519u;
    hash ^= hash >> 16;
    hash *= 2246822519u;
    hash ^= hash >> 13;
    hash *= 3266489917u;
    hash ^= hash >> 16;
    return hash;
}

float dissolveAlpha(float combinedAlpha, int x, int y)
{
    if (combinedAlpha <= 0.0f) {
        return 0.0f;
    }
    if (combinedAlpha >= 1.0f) {
        return 1.0f;
    }

    const float randomValue = static_cast<float>(hashPixel(x, y) & 0x00ffffffu) / 16777215.0f;
    return (randomValue <= combinedAlpha) ? 1.0f : 0.0f;
}

Float4 blendPremultipliedPixel(const Float4& basePremul, const Float4& srcPremul, BlendMode mode,
    float opacity, int worldX, int worldY)
{
    const float ab = clampUnit(basePremul[3]);
    const float asRaw = clampUnit(srcPremul[3]);
    const float combinedAlpha = asRaw * clampUnit(opacity);
    const float as = (mode == BlendMode::Dissolve) ? dissolveAlpha(combinedAlpha, worldX, worldY)
                                                   : combinedAlpha;

    if (as <= 0.0f) {
        return basePremul;
    }

    const Float3 Cb = unpremultiplyColor(basePremul);
    const Float3 Cs = unpremultiplyColor(srcPremul);
    const Float3 B = blendModeColor(Cb, Cs, mode);
    const float ao = as + ab * (1.0f - as);

    Float4 out { as * (1.0f - ab) * Cs[0] + as * ab * B[0] + (1.0f - as) * ab * Cb[0],
        as * (1.0f - ab) * Cs[1] + as * ab * B[1] + (1.0f - as) * ab * Cb[1],
        as * (1.0f - ab) * Cs[2] + as * ab * B[2] + (1.0f - as) * ab * Cb[2], ao };

    out[0] = std::clamp(out[0], 0.0f, out[3]);
    out[1] = std::clamp(out[1], 0.0f, out[3]);
    out[2] = std::clamp(out[2], 0.0f, out[3]);
    return out;
}

void blendImageOntoTarget(QImage& target, const QImage& source, BlendMode mode, qreal opacity,
    const aether::TileKey& tileKey)
{
    if (target.isNull() || source.isNull()) {
        return;
    }

    const int width = std::min(target.width(), source.width());
    const int height = std::min(target.height(), source.height());
    const float clampedOpacity = clampUnit(static_cast<float>(opacity));
    const int worldOriginX = static_cast<int>(tileKey.x * static_cast<qint32>(aether::TILE_SIZE));
    const int worldOriginY = static_cast<int>(tileKey.y * static_cast<qint32>(aether::TILE_SIZE));

    for (int y = 0; y < height; ++y) {
        auto* dstRow = reinterpret_cast<QRgb*>(target.scanLine(y));
        const auto* srcRow = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const Float4 basePremul = premultipliedPixelToFloat4(dstRow[x]);
            const Float4 srcPremul = premultipliedPixelToFloat4(srcRow[x]);
            const Float4 blended = blendPremultipliedPixel(
                basePremul, srcPremul, mode, clampedOpacity, worldOriginX + x, worldOriginY + y);
            dstRow[x] = float4ToPremultipliedPixel(blended);
        }
    }
}

QImage tileToImage(const aether::TileData* tile)
{
    QImage image(static_cast<int>(aether::TILE_SIZE), static_cast<int>(aether::TILE_SIZE),
        QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    if (!tile) {
        return image;
    }

    // Format-aware read: each content grid may be RGBA8 / RGBA16F / RGBA32F.
    // Route every pixel through the normalized premultiplied-float accessor,
    // which also resolves solid tiles (their const pixels() reads as
    // transparent), and pack it into the 8-bit premultiplied QImage with correct
    // R,G,B,A channel order. A raw memcpy here
    // would (a) mis-size/truncate 16F/32F buffers and (b) swap R/B against
    // QImage's ARGB32 byte order. NOTE: this merge path is inherently 8-bit
    // (QImage is ARGB32), so higher-precision tiles are quantized to 8-bit on
    // merge — acceptable (no corruption), but not lossless.
    for (uint32_t y = 0; y < aether::TILE_SIZE; ++y) {
        auto* dstRow = reinterpret_cast<QRgb*>(image.scanLine(static_cast<int>(y)));
        for (uint32_t x = 0; x < aether::TILE_SIZE; ++x) {
            float rgba[4];
            aether::readTilePixelF(*tile, x, y, rgba); // premultiplied
            dstRow[x]
                = qRgba(static_cast<int>(std::lround(std::clamp(rgba[0], 0.0f, 1.0f) * 255.0f)),
                    static_cast<int>(std::lround(std::clamp(rgba[1], 0.0f, 1.0f) * 255.0f)),
                    static_cast<int>(std::lround(std::clamp(rgba[2], 0.0f, 1.0f) * 255.0f)),
                    static_cast<int>(std::lround(std::clamp(rgba[3], 0.0f, 1.0f) * 255.0f)));
        }
    }

    return image;
}

bool isFullyTransparent(const QImage& image)
{
    if (image.isNull()) {
        return true;
    }

    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const quint32*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (row[x] != 0u) {
                return false;
            }
        }
    }

    return true;
}

void storeImageInTileGrid(aether::TileGrid* grid, const aether::TileKey& key, const QImage& image)
{
    if (!grid) {
        return;
    }

    if (image.isNull() || isFullyTransparent(image)) {
        grid->removeTile(key);
        return;
    }

    auto& tile = grid->getOrCreateTile(key); // stamped to grid->format()
    // Format-aware write: unpack the 8-bit premultiplied QImage and route each
    // pixel through the accessor so it lands in the tile's actual format with
    // correct channel order (mirrors tileToImage's read — a raw memcpy would
    // truncate wide buffers and re-introduce the R/B swap).
    const aether::TilePixelFormat fmt = tile.format();
    uint8_t* dst = tile.pixels(); // materializes/allocates in the tile's format
    for (uint32_t y = 0; y < aether::TILE_SIZE; ++y) {
        const auto* srcRow
            = reinterpret_cast<const QRgb*>(image.constScanLine(static_cast<int>(y)));
        for (uint32_t x = 0; x < aether::TILE_SIZE; ++x) {
            const QRgb px = srcRow[x];
            constexpr float inv = 1.0f / 255.0f;
            const float rgba[4]
                = { qRed(px) * inv, qGreen(px) * inv, qBlue(px) * inv, qAlpha(px) * inv };
            aether::writeTilePixelF(dst, fmt, x, y, rgba);
        }
    }
    tile.markDirty();
}

qint64 estimateLayerSnapshotMemory(const LayerData* layer)
{
    if (!layer) {
        return 0;
    }

    qint64 size = sizeof(LayerData);
    if (const auto* grid = layer->pixelGrid()) {
        size += static_cast<qint64>(grid->tileCount())
            * (sizeof(aether::TileKey) + aether::TILE_BYTE_SIZE + 64);
    }
    for (const auto& child : layer->children) {
        size += estimateLayerSnapshotMemory(child.get());
    }
    return size;
}

LayerData* layerBelowSibling(const LayerModel& model, LayerData* layer)
{
    if (!layer || layer->isBackground()) {
        return nullptr;
    }

    if (layer->parent) {
        const int index = layer->indexInParent();
        if (index < 0 || index >= layer->parent->childCount() - 1) {
            return nullptr;
        }
        return layer->parent->children[index + 1].get();
    }

    const auto& roots = model.rootLayers();
    const int index = rootLayerIndex(model, layer);
    if (index < 0 || index >= roots.size() - 1) {
        return nullptr;
    }
    return roots[index + 1].get();
}

bool hasClippedLayerDirectlyAbove(const LayerModel& model, const LayerId& sourceId)
{
    const QList<LayerData*> flat = model.allLayersFlattened();
    for (int i = 1; i < flat.size(); ++i) {
        if (flat[i] && flat[i]->id == sourceId) {
            const LayerData* above = flat[i - 1];
            return above && above->clippedToBelow;
        }
    }

    return false;
}

// Layer kinds a merge can consume. A smart layer qualifies because merge
// rasterizes it first (Photoshop does the same silently). A Board layer does
// not: it lives outside document bounds and is composed by its own surface, so
// folding it into document pixels is a conversion the user must ask for.
bool isMergeableLayerType(const LayerData* layer)
{
    return layer && (layer->isRaster() || layer->isSmart());
}

MergeDownCandidate mergeDownCandidateForSelection(const LayerModel& model)
{
    MergeDownCandidate candidate;
    if (model.selectionCount() != 1) {
        return candidate;
    }

    auto* source = model.selectedLayer();
    auto* target = layerBelowSibling(model, source);
    if (!source || !target) {
        return candidate;
    }

    if (!isMergeableLayerType(source) || !isMergeableLayerType(target) || source->hasChildren()
        || target->hasChildren() || source->locked || target->locked || source->clippedToBelow
        || target->clippedToBelow || hasClippedLayerDirectlyAbove(model, source->id)) {
        return candidate;
    }

    candidate.sourceId = source->id;
    candidate.targetId = target->id;
    candidate.parentId = source->parent ? source->parent->id : LayerId();
    candidate.sourceIndex
        = source->parent ? source->indexInParent() : rootLayerIndex(model, source);
    candidate.targetIndex
        = target->parent ? target->indexInParent() : rootLayerIndex(model, target);
    return candidate;
}

bool applyMergeDown(LayerModel& model, const LayerId& sourceId, const LayerId& targetId)
{
    auto* source = model.layerById(sourceId);
    auto* target = model.layerById(targetId);
    auto* sourceGrid = source ? source->pixelGrid() : nullptr;
    auto* targetGrid = target ? target->pixelGrid() : nullptr;
    if (!source || !target || !sourceGrid || !targetGrid) {
        return false;
    }

    QSet<qint64> packedKeys;
    for (const auto& [key, tile] : sourceGrid->tiles()) {
        Q_UNUSED(tile);
        packedKeys.insert(packTileKey(key));
    }
    for (const auto& [key, tile] : targetGrid->tiles()) {
        Q_UNUSED(tile);
        packedKeys.insert(packTileKey(key));
    }

    const qreal opacity = qBound<qreal>(0.0, source->opacity, 1.0);

    for (qint64 packedKey : packedKeys) {
        const aether::TileKey tileKey = unpackTileKey(packedKey);
        QImage merged = tileToImage(targetGrid->getTile(tileKey));
        blendImageOntoTarget(
            merged, tileToImage(sourceGrid->getTile(tileKey)), source->blendMode, opacity, tileKey);

        storeImageInTileGrid(targetGrid, tileKey, merged);
    }

    targetGrid->pruneEmpty();
    model.notifyLayerDataChanged(targetId);
    model.removeLayer(sourceId);
    model.setSelectedLayer(targetId);
    return true;
}

// A layer that can participate in a flat raster merge (Merge Visible / Merge Selected).
// Mirrors the constraints applied to Merge Down: a mergeable kind, no children,
// not locked, not part of a clipping stack.
bool isSimpleMergeableLayer(const LayerModel& model, const LayerData* layer)
{
    return isMergeableLayerType(layer) && !layer->hasChildren() && !layer->locked
        && !layer->clippedToBelow && !hasClippedLayerDirectlyAbove(model, layer->id);
}

// Composite an ordered (top -> bottom) list of layers into the bottom-most one,
// remove the others, and return the kept (target) layer id. The kept layer keeps its
// own blend mode / opacity (it stays the base of whatever sits below it); the upper
// layers' blend mode and opacity are baked into the result.
LayerId applyMergeLayers(LayerModel& model, const QList<LayerId>& orderedTopToBottom)
{
    if (orderedTopToBottom.size() < 2) {
        return LayerId();
    }

    const LayerId targetId = orderedTopToBottom.last();
    auto* target = model.layerById(targetId);
    auto* targetGrid = target ? target->pixelGrid() : nullptr;
    if (!targetGrid) {
        return LayerId();
    }

    // Precompute per-layer compositing inputs on the calling thread; worker threads
    // must never touch the LayerModel (a QObject). The last entry is the base (target).
    struct MergeSource {
        const aether::TileGrid* grid = nullptr;
        BlendMode blendMode = BlendMode::Normal;
        qreal opacity = 1.0;
    };
    QList<MergeSource> sources;
    sources.reserve(orderedTopToBottom.size());
    for (const LayerId& id : orderedTopToBottom) {
        auto* layer = model.layerById(id);
        sources.append(
            { layer ? layer->pixelGrid() : nullptr, layer ? layer->blendMode : BlendMode::Normal,
                layer ? qBound<qreal>(0.0, layer->opacity, 1.0) : 1.0 });
    }
    const int sourceCount = sources.size();

    // Collect every tile touched by any layer in the set.
    QSet<qint64> packedKeys;
    for (const MergeSource& src : sources) {
        if (!src.grid) {
            continue;
        }
        for (const auto& [key, tile] : src.grid->tiles()) {
            Q_UNUSED(tile);
            packedKeys.insert(packTileKey(key));
        }
    }

    std::vector<aether::TileKey> tileKeys;
    tileKeys.reserve(packedKeys.size());
    for (qint64 packedKey : packedKeys) {
        tileKeys.push_back(unpackTileKey(packedKey));
    }

    // Composite each tile in parallel. Tiles are independent and this phase is
    // read-only over every grid, so each worker writes a distinct result slot with
    // no synchronisation. The actual grid mutation happens single-threaded below.
    std::vector<QImage> mergedTiles(tileKeys.size());
    std::vector<std::size_t> indices(tileKeys.size());
    for (std::size_t i = 0; i < tileKeys.size(); ++i) {
        indices[i] = i;
    }

    QtConcurrent::blockingMap(indices, [&](std::size_t i) {
        const aether::TileKey& tileKey = tileKeys[i];
        // Start from the bottom-most layer (the base), then composite each higher
        // layer over it, going from just-above-base upward to the top.
        QImage merged = tileToImage(sources[sourceCount - 1].grid
                ? sources[sourceCount - 1].grid->getTile(tileKey)
                : nullptr);
        for (int s = sourceCount - 2; s >= 0; --s) {
            const aether::TileGrid* srcGrid = sources[s].grid;
            if (!srcGrid) {
                continue;
            }
            blendImageOntoTarget(merged, tileToImage(srcGrid->getTile(tileKey)),
                sources[s].blendMode, sources[s].opacity, tileKey);
        }
        mergedTiles[i] = std::move(merged);
    });

    for (std::size_t i = 0; i < tileKeys.size(); ++i) {
        storeImageInTileGrid(targetGrid, tileKeys[i], mergedTiles[i]);
    }

    targetGrid->pruneEmpty();
    // Update the kept layer first so it already shows the combined result while the
    // merged-away layers fade out.
    model.notifyLayerDataChanged(targetId);

    // Remove the merged-away layers in a single batch so the list view receives one
    // layersChanged and plays one clean removal animation (mirrors deleteSelectedLayers).
    // A per-layer removeLayer() loop would emit layersChanged repeatedly and restart the
    // animation mid-flight, making the rows pop out instead of animating.
    QList<LayerId> idsToRemove;
    idsToRemove.reserve(orderedTopToBottom.size() - 1);
    for (int i = 0; i < orderedTopToBottom.size() - 1; ++i) {
        idsToRemove.append(orderedTopToBottom[i]);
    }
    model.removeLayers(idsToRemove);
    model.setSelectedLayer(targetId);
    return targetId;
}

// Baking a mask goes through the canvas, which refuses to edit a hidden or
// locked layer. Checked up front so a merge either prepares everything or
// touches nothing — a half-prepared abort would leave a rasterized layer behind.
bool mergeBlockedByUnbakeableMask(const LayerModel& model, const QList<LayerId>& ids)
{
    for (const LayerId& id : ids) {
        const auto* layer = model.layerById(id);
        if (layer && layer->hasMask() && (!layer->visible || layer->locked)) {
            return true;
        }
    }
    return false;
}

// Opens an undo transaction for a whole operation and closes it on every exit
// path, including the early returns that abort a merge.
class UndoTransactionScope {
public:
    UndoTransactionScope(const LayersPanel::CanvasLayerOps& ops, const QString& text)
    {
        if (ops.beginUndoTransaction && ops.endUndoTransaction) {
            ops.beginUndoTransaction(text);
            m_end = ops.endUndoTransaction;
        }
    }

    ~UndoTransactionScope()
    {
        if (m_end) {
            m_end();
        }
    }

    UndoTransactionScope(const UndoTransactionScope&) = delete;
    UndoTransactionScope& operator=(const UndoTransactionScope&) = delete;

private:
    std::function<void()> m_end;
};

// Selected mergeable layers, ordered top -> bottom. Valid only when every selected
// layer is a simple mergeable sibling under one common parent; returns <2 entries
// otherwise.
QList<LayerData*> collectMergeSelectedLayers(const LayerModel& model)
{
    const QSet<LayerId>& selectedIds = model.selectedLayerIds();
    if (selectedIds.size() < 2) {
        return {};
    }

    QList<LayerData*> result;
    LayerData* commonParent = nullptr;
    bool first = true;
    for (LayerData* layer : model.allLayersFlattened()) { // flattened == top -> bottom
        if (!layer || !selectedIds.contains(layer->id)) {
            continue;
        }
        if (!isSimpleMergeableLayer(model, layer)) {
            return {}; // a non-mergeable layer is selected -> abort
        }
        if (first) {
            commonParent = layer->parent;
            first = false;
        } else if (layer->parent != commonParent) {
            return {}; // selection spans multiple parents -> abort
        }
        result.append(layer);
    }

    return result.size() >= 2 ? result : QList<LayerData*> {};
}

// Root-level visible mergeable layers, ordered top -> bottom. Non-mergeable visible
// layers (groups, locked, clipped) are skipped rather than aborting the operation.
QList<LayerData*> collectMergeVisibleLayers(const LayerModel& model)
{
    QList<LayerData*> result;
    for (const auto& root : model.rootLayers()) {
        LayerData* layer = root.get();
        if (!layer || !layer->visible) {
            continue;
        }
        if (!isSimpleMergeableLayer(model, layer)) {
            continue;
        }
        result.append(layer);
    }

    return result.size() >= 2 ? result : QList<LayerData*> {};
}

void restoreLayerSnapshot(LayerModel& model, const std::shared_ptr<LayerData>& snapshot,
    const LayerId& parentId, int index)
{
    auto clone = LayerModel::cloneLayerTree(snapshot.get(), true);
    if (!clone) {
        return;
    }

    if (parentId.isNull()) {
        model.addLayer(clone, index);
        return;
    }

    if (auto* parent = model.layerById(parentId)) {
        model.addLayerTo(clone, parent, index);
    } else {
        model.addLayer(clone, index);
    }
}

class CreateGroupedSelectionCommand final : public aether::IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    CreateGroupedSelectionCommand(LayerModel* layerModel, std::shared_ptr<LayerData> groupLayer,
        std::pair<LayerId, int> groupPosition, QList<GroupedLayerMoveRecord> moves,
        RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
        : m_layerModel(layerModel)
        , m_groupLayer(std::move(groupLayer))
        , m_groupPosition(std::move(groupPosition))
        , m_moves(std::move(moves))
        , m_requestRender(std::move(requestRender))
        , m_onContentChanged(std::move(onContentChanged))
    {
    }

    void undo() override
    {
        if (!m_layerModel || !m_groupLayer) {
            return;
        }

        {
            // One clipping check for the whole restore: moving the layers back out
            // one by one would otherwise unclip them the moment a clipped layer
            // leaves the group before its base does.
            LayerModel::ClippingBatch clippingBatch(*m_layerModel);
            for (const auto& move : m_moves) {
                LayerData* oldParent = move.oldParentId.isNull()
                    ? nullptr
                    : m_layerModel->layerById(move.oldParentId);
                m_layerModel->moveLayer(move.layerId, oldParent, move.oldIndex);
            }

            m_layerModel->removeLayer(m_groupLayer->id);
        }

        if (m_requestRender)
            m_requestRender();
        if (m_onContentChanged)
            m_onContentChanged();
    }

    void redo() override
    {
        if (!m_layerModel || !m_groupLayer) {
            return;
        }

        if (m_groupPosition.first.isNull()) {
            m_layerModel->addLayer(m_groupLayer, m_groupPosition.second);
        } else {
            m_layerModel->addLayerTo(m_groupLayer, m_groupPosition.first, m_groupPosition.second);
        }

        LayerData* group = m_layerModel->layerById(m_groupLayer->id);
        QList<LayerId> moveIds;
        moveIds.reserve(m_moves.size());
        for (const auto& move : m_moves) {
            moveIds.append(move.layerId);
        }
        if (group && !moveIds.isEmpty()) {
            m_layerModel->moveLayers(moveIds, group, 0);
        }

        if (group) {
            m_layerModel->setSelectedLayer(group->id);
        }

        if (m_requestRender)
            m_requestRender();
        if (m_onContentChanged)
            m_onContentChanged();
    }

    QString text() const override { return QStringLiteral("Create Group"); }

    qint64 memorySize() const override { return sizeof(CreateGroupedSelectionCommand); }

private:
    LayerModel* m_layerModel = nullptr;
    std::shared_ptr<LayerData> m_groupLayer;
    std::pair<LayerId, int> m_groupPosition;
    QList<GroupedLayerMoveRecord> m_moves;
    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

// Undo for a drag & drop reorder. Without it a drop into a group is invisible to
// the undo stack, so the next undo reaches past it to whatever created the group
// and takes the dropped layers down with the subtree.
class ReorderLayersCommand final : public aether::IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    ReorderLayersCommand(LayerModel* layerModel, LayerId newParentId,
        QList<LayerReorderRecord> records, RequestRenderFn requestRender,
        OnContentChangedFn onContentChanged)
        : m_layerModel(layerModel)
        , m_newParentId(std::move(newParentId))
        , m_records(std::move(records))
        , m_requestRender(std::move(requestRender))
        , m_onContentChanged(std::move(onContentChanged))
    {
    }

    void undo() override
    {
        if (!m_layerModel) {
            return;
        }

        // Restoring in ascending old index makes every recorded slot correct by
        // the time it is used: the siblings that preceded it are already back.
        QList<LayerReorderRecord> ordered = m_records;
        std::stable_sort(ordered.begin(), ordered.end(),
            [](const LayerReorderRecord& a, const LayerReorderRecord& b) {
                return a.oldIndex < b.oldIndex;
            });

        {
            // One clipping check for the whole restore, same as the drop itself.
            LayerModel::ClippingBatch clippingBatch(*m_layerModel);
            for (const auto& rec : ordered) {
                LayerData* oldParent
                    = rec.oldParentId.isNull() ? nullptr : m_layerModel->layerById(rec.oldParentId);
                if (!rec.oldParentId.isNull() && !oldParent) {
                    continue; // old parent is gone — nothing to restore into
                }
                m_layerModel->moveLayer(rec.layerId, oldParent, rec.oldIndex);
            }
        }

        notify();
    }

    void redo() override
    {
        if (!m_layerModel) {
            return;
        }

        LayerData* newParent
            = m_newParentId.isNull() ? nullptr : m_layerModel->layerById(m_newParentId);
        if (!m_newParentId.isNull() && !newParent) {
            return;
        }

        {
            // Replaying the recorded moveLayer() calls in their original order
            // reproduces the drop exactly, since undo() restored the pre-drop tree.
            LayerModel::ClippingBatch clippingBatch(*m_layerModel);
            for (const auto& rec : m_records) {
                m_layerModel->moveLayer(rec.layerId, newParent, rec.newIndex);
            }
        }

        notify();
    }

    QString text() const override { return QStringLiteral("Move Layer"); }

    qint64 memorySize() const override { return sizeof(ReorderLayersCommand); }

private:
    void notify()
    {
        if (m_requestRender)
            m_requestRender();
        if (m_onContentChanged)
            m_onContentChanged();
    }

    LayerModel* m_layerModel = nullptr;
    LayerId m_newParentId;
    QList<LayerReorderRecord> m_records;
    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

class MergeDownUndoCommand final : public aether::IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    MergeDownUndoCommand(LayerModel* layerModel, std::shared_ptr<LayerData> sourceSnapshot,
        std::shared_ptr<LayerData> targetSnapshot, LayerId parentId, int sourceIndex,
        int targetIndex, RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
        : m_layerModel(layerModel)
        , m_sourceSnapshot(std::move(sourceSnapshot))
        , m_targetSnapshot(std::move(targetSnapshot))
        , m_parentId(parentId)
        , m_sourceIndex(sourceIndex)
        , m_targetIndex(targetIndex)
        , m_requestRender(std::move(requestRender))
        , m_onContentChanged(std::move(onContentChanged))
    {
    }

    void undo() override
    {
        if (!m_layerModel || !m_sourceSnapshot || !m_targetSnapshot) {
            return;
        }

        QList<LayerId> idsToRemove;
        if (m_layerModel->contains(m_sourceSnapshot->id)) {
            idsToRemove.append(m_sourceSnapshot->id);
        }
        if (m_layerModel->contains(m_targetSnapshot->id)) {
            idsToRemove.append(m_targetSnapshot->id);
        }
        if (!idsToRemove.isEmpty()) {
            m_layerModel->removeLayers(idsToRemove);
        }

        restoreLayerSnapshot(*m_layerModel, m_sourceSnapshot, m_parentId, m_sourceIndex);
        restoreLayerSnapshot(*m_layerModel, m_targetSnapshot, m_parentId, m_targetIndex);
        m_layerModel->setSelectedLayer(m_sourceSnapshot->id);

        if (m_requestRender)
            m_requestRender();
        if (m_onContentChanged)
            m_onContentChanged();
    }

    void redo() override
    {
        if (!m_layerModel || !m_sourceSnapshot || !m_targetSnapshot) {
            return;
        }

        if (applyMergeDown(*m_layerModel, m_sourceSnapshot->id, m_targetSnapshot->id)) {
            if (m_requestRender)
                m_requestRender();
            if (m_onContentChanged)
                m_onContentChanged();
        }
    }

    QString text() const override { return QStringLiteral("Merge Down"); }

    qint64 memorySize() const override
    {
        return sizeof(MergeDownUndoCommand) + estimateLayerSnapshotMemory(m_sourceSnapshot.get())
            + estimateLayerSnapshotMemory(m_targetSnapshot.get());
    }

private:
    LayerModel* m_layerModel = nullptr;
    std::shared_ptr<LayerData> m_sourceSnapshot;
    std::shared_ptr<LayerData> m_targetSnapshot;
    LayerId m_parentId;
    int m_sourceIndex = -1;
    int m_targetIndex = -1;
    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

// Undoable merge of an arbitrary ordered (top -> bottom) set of layers into the
// bottom-most one. Used by both Merge Visible and Merge Selected.
class MergeLayersUndoCommand final : public aether::IUndoCommand {
public:
    using RequestRenderFn = std::function<void()>;
    using OnContentChangedFn = std::function<void()>;

    struct SnapshotEntry {
        std::shared_ptr<LayerData> snapshot;
        LayerId parentId;
        int index = -1;
    };

    MergeLayersUndoCommand(LayerModel* layerModel, QList<LayerId> orderedTopToBottom,
        QList<SnapshotEntry> snapshots, QString label, RequestRenderFn requestRender,
        OnContentChangedFn onContentChanged)
        : m_layerModel(layerModel)
        , m_orderedTopToBottom(std::move(orderedTopToBottom))
        , m_snapshots(std::move(snapshots))
        , m_label(std::move(label))
        , m_requestRender(std::move(requestRender))
        , m_onContentChanged(std::move(onContentChanged))
    {
    }

    void undo() override
    {
        if (!m_layerModel) {
            return;
        }

        QList<LayerId> idsToRemove;
        for (const auto& entry : m_snapshots) {
            if (entry.snapshot && m_layerModel->contains(entry.snapshot->id)) {
                idsToRemove.append(entry.snapshot->id);
            }
        }
        if (!idsToRemove.isEmpty()) {
            m_layerModel->removeLayers(idsToRemove);
        }

        // Re-insert ascending by index so lower positions are filled first.
        QList<const SnapshotEntry*> ordered;
        ordered.reserve(m_snapshots.size());
        for (const auto& entry : m_snapshots) {
            ordered.append(&entry);
        }
        std::sort(ordered.begin(), ordered.end(),
            [](const SnapshotEntry* a, const SnapshotEntry* b) { return a->index < b->index; });
        for (const SnapshotEntry* entry : ordered) {
            restoreLayerSnapshot(*m_layerModel, entry->snapshot, entry->parentId, entry->index);
        }

        // Restore the original multi-selection.
        bool firstSel = true;
        for (const auto& entry : m_snapshots) {
            if (!entry.snapshot) {
                continue;
            }
            if (firstSel) {
                m_layerModel->setSelectedLayer(entry.snapshot->id);
                firstSel = false;
            } else {
                m_layerModel->addToSelection(entry.snapshot->id);
            }
        }

        if (m_requestRender)
            m_requestRender();
        if (m_onContentChanged)
            m_onContentChanged();
    }

    void redo() override
    {
        if (!m_layerModel) {
            return;
        }
        if (applyMergeLayers(*m_layerModel, m_orderedTopToBottom).isNull()) {
            return;
        }
        if (m_requestRender)
            m_requestRender();
        if (m_onContentChanged)
            m_onContentChanged();
    }

    QString text() const override { return m_label; }

    qint64 memorySize() const override
    {
        qint64 size = sizeof(MergeLayersUndoCommand);
        for (const auto& entry : m_snapshots) {
            size += estimateLayerSnapshotMemory(entry.snapshot.get());
        }
        return size;
    }

private:
    LayerModel* m_layerModel = nullptr;
    QList<LayerId> m_orderedTopToBottom;
    QList<SnapshotEntry> m_snapshots;
    QString m_label;
    RequestRenderFn m_requestRender;
    OnContentChangedFn m_onContentChanged;
};

class LayerStateToggleButton final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit LayerStateToggleButton(const QIcon& icon, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_icon(icon)
    {
        const auto& tm = ruwa::ui::core::ThemeManager::instance();
        const int size = qMax(20, tm.scaled(24));
        setFixedSize(size, size);
        setCheckable(true);
        setHoverDuration(200);
        setActiveDuration(250);
        connect(this, &QAbstractButton::toggled, this, &BaseAnimatedButton::setActive);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const bool disabled = !isEnabled();

        if (isChecked()) {
            QColor active = colors.primary;
            qreal a = 0.35 + 0.25 * activeProgress();
            if (disabled) {
                a *= 0.35;
            }
            active.setAlphaF(a);
            p.setPen(Qt::NoPen);
            p.setBrush(active);
            p.drawRoundedRect(r, 4, 4);
        }
        if (hoverProgress() > 0.0 && !isChecked() && !disabled) {
            QColor hover = colors.surfaceHover();
            hover.setAlphaF(hover.alphaF() * hoverProgress());
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(r, 4, 4);
        }
        // The checked-state animation is the whole answer to a click. An
        // un-eased press wash on top of it only reads as a hard flash.

        QColor iconColor;
        if (disabled) {
            iconColor = colors.textMuted;
            iconColor.setAlphaF(qBound(0.0, iconColor.alphaF() * 0.42, 1.0));
        } else {
            iconColor = ruwa::ui::core::ThemeColors::interpolate(
                colors.textMuted, colors.text, hoverProgress());
            if (isChecked()) {
                iconColor = ruwa::ui::core::ThemeColors::interpolate(
                    colors.text, colors.textOnPrimary(), activeProgress());
            }
        }

        if (!m_icon.isNull()) {
            QPixmap src = m_icon.pixmap(m_iconSize, m_iconSize);
            if (!src.isNull()) {
                QPixmap colored(src.size());
                colored.fill(Qt::transparent);
                QPainter iconPainter(&colored);
                iconPainter.drawPixmap(0, 0, src);
                iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                iconPainter.fillRect(colored.rect(), iconColor);
                iconPainter.end();
                p.drawPixmap((width() - m_iconSize) / 2, (height() - m_iconSize) / 2, colored);
            }
        }
    }

private:
    QIcon m_icon;
    int m_iconSize = qMax(12, ruwa::ui::core::WidgetStyleManager::instance().scaled(14));
};

class LayerToolbarButton final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    enum class Glyph { Add, Duplicate, MergeDown, Folder, Mask, Adjustment, Trash };

    explicit LayerToolbarButton(Glyph glyph, const QIcon& icon, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_glyph(glyph)
        , m_icon(icon)
    {
        const auto& tm = ruwa::ui::core::ThemeManager::instance();
        setFixedSize(qMax(22, tm.scaled(26)), qMax(20, tm.scaled(24)));
        setCheckable(false);
    }

    /// Drops the press/hover state a reorder drag stole from the button, so it
    /// doesn't stay visually stuck once the ghost takes over.
    void cancelPointerInteraction()
    {
        setDown(false);
        setHovered(rect().contains(mapFromGlobal(QCursor::pos())));
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const bool disabled = !isEnabled();

        if (hoverProgress() > 0.0 && !disabled) {
            QColor hover = colors.surfaceHover();
            hover.setAlphaF(hover.alphaF() * hoverProgress());
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(r, 4, 4);
        }
        // No press wash: the hover fade is the only chrome these buttons show.

        QColor iconColor;
        if (disabled) {
            iconColor = colors.textMuted;
            iconColor.setAlphaF(qBound(0.0, iconColor.alphaF() * 0.42, 1.0));
        } else {
            iconColor = ruwa::ui::core::ThemeColors::interpolate(
                colors.textMuted, colors.text, hoverProgress());
        }

        QPen pen(iconColor, 1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);

        const QRectF iconRect(
            (width() - m_iconSize) / 2.0, (height() - m_iconSize) / 2.0, m_iconSize, m_iconSize);
        const qreal l = iconRect.left();
        const qreal t = iconRect.top();
        const qreal w = iconRect.width();
        const qreal h = iconRect.height();

        switch (m_glyph) {
        case Glyph::Add:
            p.drawLine(QPointF(l + w * 0.5, t + h * 0.2), QPointF(l + w * 0.5, t + h * 0.8));
            p.drawLine(QPointF(l + w * 0.2, t + h * 0.5), QPointF(l + w * 0.8, t + h * 0.5));
            break;
        case Glyph::Duplicate:
        case Glyph::MergeDown:
        case Glyph::Folder:
        case Glyph::Mask:
        case Glyph::Adjustment:
        case Glyph::Trash: {
            if (!m_icon.isNull()) {
                QPixmap src = m_icon.pixmap(m_iconSize, m_iconSize);
                if (!src.isNull()) {
                    QPixmap colored(src.size());
                    colored.fill(Qt::transparent);
                    QPainter iconPainter(&colored);
                    iconPainter.drawPixmap(0, 0, src);
                    iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    iconPainter.fillRect(colored.rect(), iconColor);
                    iconPainter.end();
                    p.drawPixmap((width() - m_iconSize) / 2, (height() - m_iconSize) / 2, colored);
                    break;
                }
            }
            // Fallback only for Folder (Trash uses Remove icon from resources)
            if (m_glyph == Glyph::Folder) {
                QPainterPath path;
                path.moveTo(l + w * 0.12, t + h * 0.36);
                path.lineTo(l + w * 0.38, t + h * 0.36);
                path.lineTo(l + w * 0.48, t + h * 0.22);
                path.lineTo(l + w * 0.88, t + h * 0.22);
                path.lineTo(l + w * 0.88, t + h * 0.78);
                path.lineTo(l + w * 0.12, t + h * 0.78);
                path.closeSubpath();
                p.drawPath(path);
            }
            break;
        }
        }
    }

private:
    Glyph m_glyph;
    QIcon m_icon;
    int m_iconSize = qMax(12, ruwa::ui::core::WidgetStyleManager::instance().scaled(14));
};

// Property flag the flow uses to recognise divider items (so it can hide them
// when a wrap would leave them dangling at a row edge).
constexpr char kFlowSeparatorProp[] = "ruwaFlowSeparator";

// Thin vertical divider that matches the resting icon colour (textMuted). It
// reserves a couple of pixels of layout width so the button it precedes is
// physically, not just visually, separated.
class LayerToolbarSeparator final : public QWidget {
public:
    explicit LayerToolbarSeparator(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        const auto& tm = ruwa::ui::core::ThemeManager::instance();
        setFixedSize(qMax(7, tm.scaled(7)), qMax(20, tm.scaled(24)));
        setProperty(kFlowSeparatorProp, true);
        QObject::connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, [this]() { update(); });
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // Same colour the toolbar glyphs paint at rest.
        const QColor c = ruwa::ui::core::ThemeManager::instance().colors().textMuted;
        const qreal x = width() / 2.0;
        const qreal inset = height() * 0.28;
        p.setPen(QPen(c, 1.0));
        p.drawLine(QPointF(x, inset), QPointF(x, height() - inset));
    }
};

} // namespace

// ============================================================================
// Construction
// ============================================================================

LayersPanel::LayersPanel(QWidget* parent)
    : DockPanel(tr("Layers"), parent)
{
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::LayersPanel);
    setMinimumPanelSize(180, 200);
    setPreferredPanelSize(250, 400);

    setClosable(true);
    setFloatable(true);
    setMovable(true);

    m_toolbarOrder = defaultToolbarOrder();

    m_thumbnailRefreshTimer.setSingleShot(true);
    m_thumbnailRefreshTimer.setInterval(33);
    connect(&m_thumbnailRefreshTimer, &QTimer::timeout, this, [this]() {
        if (m_listView) {
            m_listView->invalidateVisibleThumbnails();
        }
    });

    connect(&m_layerModel, &LayerModel::selectionChanged, this,
        [this](const LayerId&) { syncLayerControls(); });
    connect(
        &m_layerModel, &LayerModel::layerDataChanged, this, &LayersPanel::onModelLayerDataChanged);
}

LayersPanel::~LayersPanel()
{
    if (m_toolbarDragActive) {
        qApp->removeEventFilter(this);
    }
    if (m_toolbarDragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_toolbarDragCursorOverride = false;
    }
    if (m_toolbarDragGhost) {
        delete m_toolbarDragGhost.data();
        m_toolbarDragGhost = nullptr;
    }
    if (m_toolbarFlow) {
        m_toolbarFlow->shutdown();
    }
}

// ============================================================================
// Content
// ============================================================================

QWidget* LayersPanel::createContent()
{
    m_contentWidget = new QWidget();
    m_contentWidget->setStyleSheet("background: transparent;");

    auto* mainLayout = new QVBoxLayout(m_contentWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    auto* controlsWidget = new QWidget();
    auto* controlsLayout = new QVBoxLayout(controlsWidget);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(4);

    auto* topRowLayout = new QHBoxLayout();
    topRowLayout->setContentsMargins(0, 0, 0, 0);
    topRowLayout->setSpacing(4);

    m_blendModeCombo = new ruwa::ui::widgets::AnimatedComboBox(controlsWidget);
    populateBlendModeCombo();
    m_blendModeCombo->setFixedHeight(24);
    m_blendModeCombo->setPopupMinWidth(240);
    m_blendModeCombo->setPopupMaxHeight(320);
    m_blendModeCombo->setPlaceholderText(tr("Blend mode"));
    connect(m_blendModeCombo, &ruwa::ui::widgets::AnimatedComboBox::currentIndexChanged, this,
        &LayersPanel::onBlendModeChanged);
    connect(m_blendModeCombo, &ruwa::ui::widgets::AnimatedComboBox::activated, this,
        &LayersPanel::onBlendModeChanged);
    connect(m_blendModeCombo, &ruwa::ui::widgets::AnimatedComboBox::itemHovered, this,
        &LayersPanel::onBlendModeHovered);
    connect(m_blendModeCombo, &ruwa::ui::widgets::AnimatedComboBox::popupShown, this,
        &LayersPanel::onBlendModePopupShown);
    connect(m_blendModeCombo, &ruwa::ui::widgets::AnimatedComboBox::popupHidden, this,
        &LayersPanel::onBlendModePopupHidden);
    topRowLayout->addWidget(m_blendModeCombo, 5);

    m_opacitySlider = new ruwa::ui::widgets::OpacitySliderWidget(controlsWidget);
    connect(m_opacitySlider, &ruwa::ui::widgets::OpacitySliderWidget::opacityChanged, this,
        &LayersPanel::onOpacityChanged);
    connect(m_opacitySlider, &ruwa::ui::widgets::OpacitySliderWidget::opacityDragStarted, this,
        &LayersPanel::onOpacityDragStarted);
    connect(m_opacitySlider, &ruwa::ui::widgets::OpacitySliderWidget::opacityCommitted, this,
        &LayersPanel::onOpacityCommitted);
    topRowLayout->addWidget(m_opacitySlider, 4);

    m_btnAlphaLock
        = new LayerStateToggleButton(ruwa::ui::core::IconProvider::instance().getIcon(
                                         ruwa::ui::core::IconProvider::StandardIcon::Alpha),
            controlsWidget);
    m_btnAlphaLock->setToolTip(tr("Alpha Lock"));
    connect(m_btnAlphaLock, &QAbstractButton::toggled, this, &LayersPanel::onAlphaLockToggled);
    m_btnLock = new LayerStateToggleButton(ruwa::ui::core::IconProvider::instance().getIcon(
                                               ruwa::ui::core::IconProvider::StandardIcon::Lock),
        controlsWidget);
    m_btnLock->setToolTip(tr("Lock"));
    connect(m_btnLock, &QAbstractButton::toggled, this, &LayersPanel::onLockToggled);
    controlsLayout->addLayout(topRowLayout);

    setSubtitleContentMargins(6, 6, 6, 6);
    setSubtitleContentSpacing(4);
    setSubtitleWidget(controlsWidget);

    // Layer list view
    m_listView = new ruwa::ui::widgets::LayerListView(m_contentWidget);
    m_listView->setDisplayFrame(m_displayFrame);
    m_listView->setModel(&m_layerModel);
    m_listView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainLayout->addWidget(m_listView, 1);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::visibleThumbnailStateChanged, this,
        &LayersPanel::visibleThumbnailStateChanged);

    // Connect list view signals
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerSelected, this,
        &LayersPanel::onLayerSelected);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerPaintTargetSelected, this,
        &LayersPanel::onLayerPaintTargetSelected);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerContentSelectionRequested, this,
        &LayersPanel::onLayerContentSelectionRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerMaskSelectionRequested, this,
        &LayersPanel::onLayerMaskSelectionRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerTextEditRequested, this,
        &LayersPanel::onLayerTextEditRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerSmartContentEditRequested, this,
        &LayersPanel::onLayerSmartContentEditRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerExpandToggled, this,
        &LayersPanel::onLayerExpandToggled);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerVisibilityToggled, this,
        &LayersPanel::onLayerVisibilityToggled);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerDragDropped, this,
        &LayersPanel::onLayerDragDropped);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerDragCopyDropped, this,
        &LayersPanel::onLayerDragCopyDropped);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerRenamed, this,
        &LayersPanel::onLayerRenamed);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::clipSelectionRequested, this,
        &LayersPanel::onClipSelectionRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::clipSwipeRequested, this,
        &LayersPanel::onClipSwipeRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerAlphaLockClicked, this,
        &LayersPanel::onLayerAlphaLockClicked);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerLockClicked, this,
        &LayersPanel::onLayerLockClicked);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerDuplicateRequested, this,
        &LayersPanel::onLayerDuplicateRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerNewSmartObjectViaCopyRequested,
        this, &LayersPanel::onLayerNewSmartObjectViaCopyRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerDeleteRequested, this,
        &LayersPanel::onLayerDeleteRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerQuickClippingMaskRequested, this,
        &LayersPanel::onLayerQuickClippingMaskRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerClearPixelsRequested, this,
        &LayersPanel::onLayerClearPixelsRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerRasterizeSmartRequested, this,
        &LayersPanel::onLayerRasterizeSmartRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerConvertToSmartObjectRequested, this,
        &LayersPanel::onLayerConvertToSmartObjectRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerReplaceSmartContentsRequested, this,
        &LayersPanel::onLayerReplaceSmartContentsRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerApplyMaskRequested, this,
        &LayersPanel::onLayerApplyMaskRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerInvertMaskRequested, this,
        &LayersPanel::onLayerInvertMaskRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerApplyEffectsRequested, this,
        &LayersPanel::onLayerApplyEffectsRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerToggleAlphaLockRequested, this,
        &LayersPanel::onLayerToggleAlphaLockRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerToggleLockRequested, this,
        &LayersPanel::onLayerToggleLockRequested);
    connect(m_listView, &ruwa::ui::widgets::LayerListView::layerMultiActionRequested, this,
        &LayersPanel::onLayerMultiActionRequested);

    setupToolbar(controlsWidget);
    syncLayerControls();

    return m_contentWidget;
}

// ============================================================================
// Toolbar
// ============================================================================

void LayersPanel::setupToolbar(QWidget* container)
{
    auto* controlsLayout = qobject_cast<QVBoxLayout*>(container->layout());
    if (!controlsLayout) {
        return;
    }

    auto makeBtn
        = [&](LayerToolbarButton::Glyph glyph, ruwa::ui::core::IconProvider::StandardIcon iconType,
              const QString& tooltip) -> LayerToolbarButton* {
        auto* btn = new LayerToolbarButton(
            glyph, ruwa::ui::core::IconProvider::instance().getIcon(iconType), container);
        btn->setToolTip(tooltip);
        return btn;
    };

    // --- Creation actions: all "add a new layer-thing" ---
    m_btnAdd = makeBtn(LayerToolbarButton::Glyph::Add,
        ruwa::ui::core::IconProvider::StandardIcon::Edit, tr("Add Layer"));
    m_btnAdjustment = makeBtn(LayerToolbarButton::Glyph::Adjustment,
        ruwa::ui::core::IconProvider::StandardIcon::AdjustmentLayer, tr("Add Adjustment Layer"));
    m_btnGroup = makeBtn(LayerToolbarButton::Glyph::Folder,
        ruwa::ui::core::IconProvider::StandardIcon::Folder, tr("Add Group"));
    m_btnMask = makeBtn(LayerToolbarButton::Glyph::Mask,
        ruwa::ui::core::IconProvider::StandardIcon::LayerMask, tr("Add Mask"));

    // --- Operations: act on the existing selection ---
    m_btnDuplicate = makeBtn(LayerToolbarButton::Glyph::Duplicate,
        ruwa::ui::core::IconProvider::StandardIcon::Duplicate, tr("Duplicate Layer"));
    m_btnMergeDown = makeBtn(LayerToolbarButton::Glyph::MergeDown,
        ruwa::ui::core::IconProvider::StandardIcon::ArrowDown, tr("Merge Layers"));
    m_btnDelete = makeBtn(LayerToolbarButton::Glyph::Trash,
        ruwa::ui::core::IconProvider::StandardIcon::Trash, tr("Delete Layer"));

    using ruwa::ui::widgets::ToolTipController;
    ToolTipController::setShortcutCommand(m_btnAdd, QStringLiteral("layers.add"));
    ToolTipController::setShortcutCommand(m_btnAdjustment, QStringLiteral("layers.add-adjustment"));
    ToolTipController::setShortcutCommand(m_btnGroup, QStringLiteral("layers.add-group"));
    ToolTipController::setShortcutCommand(m_btnDuplicate, QStringLiteral("layers.duplicate"));
    ToolTipController::setShortcutCommand(m_btnMergeDown, QStringLiteral("layers.merge-down"));
    ToolTipController::setShortcutCommand(m_btnDelete, QStringLiteral("layers.delete"));

    connect(
        m_btnAdd, &ruwa::ui::widgets::BaseAnimatedButton::clicked, this, &LayersPanel::onAddLayer);
    connect(m_btnAdjustment, &ruwa::ui::widgets::BaseAnimatedButton::clicked, this,
        &LayersPanel::onAddAdjustmentLayer);
    connect(m_btnDuplicate, &ruwa::ui::widgets::BaseAnimatedButton::clicked, this,
        [this]() { duplicateSelectedLayers(); });
    connect(m_btnMergeDown, &ruwa::ui::widgets::BaseAnimatedButton::clicked, this,
        [this]() { performMerge(); });
    connect(m_btnGroup, &ruwa::ui::widgets::BaseAnimatedButton::clicked, this,
        &LayersPanel::onAddGroup);
    connect(
        m_btnMask, &ruwa::ui::widgets::BaseAnimatedButton::clicked, this, &LayersPanel::onAddMask);
    connect(m_btnDelete, &ruwa::ui::widgets::BaseAnimatedButton::clicked, this,
        &LayersPanel::onDeleteLayer);

    // Divider that sets the destructive Delete apart from the operations. It rides
    // along in the reorderable list but cannot itself be picked up.
    m_toolbarSeparator = new LayerToolbarSeparator(container);

    // Adaptive, animated flow: action buttons wrap with the panel width, while
    // the alpha-lock / layer-lock toggles stay pinned to the first row (right).
    m_toolbarFlow = new ruwa::ui::widgets::AnimatedFlowWidget(
        ruwa::ui::widgets::AnimatedFlowWidget::LayoutStyle::PinnedToolbar, container);
    m_toolbarFlow->setFlowSpacing(2, 2);
    m_toolbarFlow->setSeparatorPropertyName(kFlowSeparatorProp);

    // Only the left-hand action block reorders. Pinned toggles stay put and never
    // install the drag filter.
    for (ToolbarItem item : std::as_const(m_toolbarOrder)) {
        QWidget* itemWidget = toolbarItemWidget(item);
        if (itemWidget && item != ToolbarItem::Separator) {
            itemWidget->installEventFilter(this);
        }
    }

    applyToolbarOrder(false);
    controlsLayout->addWidget(m_toolbarFlow);

    applyToolbarTheme();
}

// ============================================================================
// Toolbar reordering (drag & drop)
// ============================================================================

QList<LayersPanel::ToolbarItem> LayersPanel::defaultToolbarOrder()
{
    // Creation group first, then operations on the selection, with a divider
    // isolating the destructive Delete.
    return { ToolbarItem::AddLayer, ToolbarItem::AddAdjustment, ToolbarItem::AddGroup,
        ToolbarItem::AddMask, ToolbarItem::Duplicate, ToolbarItem::Merge, ToolbarItem::Separator,
        ToolbarItem::Delete };
}

QString LayersPanel::toolbarItemKey(ToolbarItem item)
{
    switch (item) {
    case ToolbarItem::AddLayer:
        return QStringLiteral("add");
    case ToolbarItem::AddAdjustment:
        return QStringLiteral("adjustment");
    case ToolbarItem::AddGroup:
        return QStringLiteral("group");
    case ToolbarItem::AddMask:
        return QStringLiteral("mask");
    case ToolbarItem::Duplicate:
        return QStringLiteral("duplicate");
    case ToolbarItem::Merge:
        return QStringLiteral("merge");
    case ToolbarItem::Separator:
        return QStringLiteral("separator");
    case ToolbarItem::Delete:
        return QStringLiteral("delete");
    }
    return {};
}

/// Restores a stored order, dropping unknown/duplicate entries and appending any
/// item the stored order never mentioned (so a newly added button still shows up).
QList<LayersPanel::ToolbarItem> LayersPanel::normalizedToolbarOrder(const QJsonArray& values)
{
    const QList<ToolbarItem> defaults = defaultToolbarOrder();
    QList<ToolbarItem> result;
    for (const QJsonValue& value : values) {
        const QString key = value.toString();
        for (ToolbarItem item : defaults) {
            if (toolbarItemKey(item) == key && !result.contains(item)) {
                result.append(item);
                break;
            }
        }
    }
    for (ToolbarItem item : defaults) {
        if (!result.contains(item)) {
            result.append(item);
        }
    }
    return result;
}

QList<LayersPanel::ToolbarItem> LayersPanel::configurableToolbarItems()
{
    QList<ToolbarItem> items;
    for (ToolbarItem item : defaultToolbarOrder()) {
        if (item != ToolbarItem::Separator) {
            items.append(item);
        }
    }
    return items;
}

QString LayersPanel::toolbarItemDisplayName(ToolbarItem item)
{
    // Same strings the buttons carry as tooltips, so both share one translation.
    switch (item) {
    case ToolbarItem::AddLayer:
        return tr("Add Layer");
    case ToolbarItem::AddAdjustment:
        return tr("Add Adjustment Layer");
    case ToolbarItem::AddGroup:
        return tr("Add Group");
    case ToolbarItem::AddMask:
        return tr("Add Mask");
    case ToolbarItem::Duplicate:
        return tr("Duplicate Layer");
    case ToolbarItem::Merge:
        return tr("Merge Layers");
    case ToolbarItem::Separator:
        return {};
    case ToolbarItem::Delete:
        return tr("Delete Layer");
    }
    return {};
}

ruwa::ui::core::IconProvider::StandardIcon LayersPanel::toolbarItemIconType(ToolbarItem item)
{
    using StandardIcon = ruwa::ui::core::IconProvider::StandardIcon;
    switch (item) {
    case ToolbarItem::AddLayer:
        return StandardIcon::Edit;
    case ToolbarItem::AddAdjustment:
        return StandardIcon::AdjustmentLayer;
    case ToolbarItem::AddGroup:
        return StandardIcon::Folder;
    case ToolbarItem::AddMask:
        return StandardIcon::LayerMask;
    case ToolbarItem::Duplicate:
        return StandardIcon::Duplicate;
    case ToolbarItem::Merge:
        return StandardIcon::ArrowDown;
    case ToolbarItem::Separator:
        return StandardIcon::Dots;
    case ToolbarItem::Delete:
        return StandardIcon::Trash;
    }
    return StandardIcon::Edit;
}

bool LayersPanel::isToolbarItemVisible(ToolbarItem item) const
{
    return !m_hiddenToolbarItems.contains(item);
}

void LayersPanel::setToolbarItemVisible(ToolbarItem item, bool visible)
{
    // The divider is chrome, not a configurable button.
    if (item == ToolbarItem::Separator || isToolbarItemVisible(item) == visible) {
        return;
    }

    if (visible) {
        m_hiddenToolbarItems.removeOne(item);
    } else {
        m_hiddenToolbarItems.append(item);
    }

    applyToolbarOrder(true);
    emit panelStateChanged();
}

/// Restores the hidden set, dropping unknown keys and keeping the canonical order
/// so the stored array stays stable across sessions.
QList<LayersPanel::ToolbarItem> LayersPanel::normalizedHiddenToolbarItems(const QJsonArray& values)
{
    QList<ToolbarItem> requested;
    for (const QJsonValue& value : values) {
        const QString key = value.toString();
        for (ToolbarItem item : configurableToolbarItems()) {
            if (toolbarItemKey(item) == key && !requested.contains(item)) {
                requested.append(item);
                break;
            }
        }
    }

    QList<ToolbarItem> result;
    for (ToolbarItem item : configurableToolbarItems()) {
        if (requested.contains(item)) {
            result.append(item);
        }
    }
    return result;
}

QWidget* LayersPanel::toolbarItemWidget(ToolbarItem item) const
{
    switch (item) {
    case ToolbarItem::AddLayer:
        return m_btnAdd;
    case ToolbarItem::AddAdjustment:
        return m_btnAdjustment;
    case ToolbarItem::AddGroup:
        return m_btnGroup;
    case ToolbarItem::AddMask:
        return m_btnMask;
    case ToolbarItem::Duplicate:
        return m_btnDuplicate;
    case ToolbarItem::Merge:
        return m_btnMergeDown;
    case ToolbarItem::Separator:
        return m_toolbarSeparator;
    case ToolbarItem::Delete:
        return m_btnDelete;
    }
    return nullptr;
}

void LayersPanel::applyToolbarOrder(bool animate)
{
    if (!m_toolbarFlow) {
        return;
    }

    // visibleOrder tracks buttons only: it drives the fade animations, and the
    // divider's visibility belongs to the flow, not to a cross-fade.
    QList<ToolbarItem> visibleOrder;
    QList<QWidget*> flowItems;
    visibleOrder.reserve(m_toolbarOrder.size());
    flowItems.reserve(m_toolbarOrder.size());
    for (int index = 0; index < m_toolbarOrder.size(); ++index) {
        const ToolbarItem item = m_toolbarOrder[index];
        QWidget* itemWidget = toolbarItemWidget(item);
        if (!itemWidget || !isToolbarItemVisible(item)) {
            continue;
        }
        if (item == ToolbarItem::Separator) {
            // A divider with nothing left to separate would dangle at the end of
            // the block (the flow only guards the row-edge case).
            bool hasFollower = false;
            for (int next = index + 1; next < m_toolbarOrder.size(); ++next) {
                if (isToolbarItemVisible(m_toolbarOrder[next])
                    && toolbarItemWidget(m_toolbarOrder[next])) {
                    hasFollower = true;
                    break;
                }
            }
            if (!hasFollower || flowItems.isEmpty()) {
                itemWidget->hide();
                continue;
            }
            flowItems.append(itemWidget);
            continue;
        }
        visibleOrder.append(item);
        flowItems.append(itemWidget);
    }

    const bool shouldAnimate
        = animate && ruwa::ui::core::WidgetStyleManager::instance().animationsEnabled();
    // Pinned to the first row (always on top), never reorderable.
    const QList<QWidget*> pinnedItems { m_btnAlphaLock, m_btnLock };

    if (!shouldAnimate) {
        for (auto it = m_toolbarVisibilityAnimations.begin();
            it != m_toolbarVisibilityAnimations.end(); ++it) {
            if (it.value()) {
                it.value()->stop();
                it.value()->deleteLater();
            }
        }
        m_toolbarVisibilityAnimations.clear();
        m_toolbarFlow->setItems(flowItems, pinnedItems, false);
        for (ToolbarItem item : defaultToolbarOrder()) {
            QWidget* itemWidget = toolbarItemWidget(item);
            // The flow owns the divider's visibility (it drops it at row edges),
            // so forcing it back on here would undo the relayout above.
            if (!itemWidget || item == ToolbarItem::Separator) {
                continue;
            }
            if (itemWidget != m_toolbarDraggedWidget) {
                itemWidget->setGraphicsEffect(nullptr);
            }
            itemWidget->setVisible(visibleOrder.contains(item));
        }
        m_appliedToolbarOrder = visibleOrder;
        return;
    }

    for (ToolbarItem item : std::as_const(m_appliedToolbarOrder)) {
        if (!visibleOrder.contains(item)) {
            animateToolbarItemVisibility(item, false);
        }
    }
    for (ToolbarItem item : std::as_const(visibleOrder)) {
        if (!m_appliedToolbarOrder.contains(item)) {
            animateToolbarItemVisibility(item, true);
        }
    }

    m_toolbarFlow->setItems(flowItems, pinnedItems, true);
    m_appliedToolbarOrder = visibleOrder;
}

void LayersPanel::cancelToolbarItemVisibilityAnimation(ToolbarItem item)
{
    const auto it = m_toolbarVisibilityAnimations.find(item);
    if (it == m_toolbarVisibilityAnimations.end()) {
        return;
    }
    if (it.value()) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    m_toolbarVisibilityAnimations.erase(it);
}

/// Cross-fades a toolbar button in or out; the flow slides the neighbours in the
/// same frame, so hiding a button never snaps the row.
void LayersPanel::animateToolbarItemVisibility(ToolbarItem item, bool visible)
{
    QWidget* itemWidget = toolbarItemWidget(item);
    if (!itemWidget) {
        return;
    }

    qreal startOpacity = visible ? 0.0 : 1.0;
    if (auto* currentEffect = qobject_cast<QGraphicsOpacityEffect*>(itemWidget->graphicsEffect())) {
        startOpacity = currentEffect->opacity();
    }
    cancelToolbarItemVisibilityAnimation(item);
    if (itemWidget != m_toolbarDraggedWidget) {
        itemWidget->setGraphicsEffect(nullptr);
    }

    auto* opacityEffect = new QGraphicsOpacityEffect(itemWidget);
    opacityEffect->setOpacity(startOpacity);
    itemWidget->setGraphicsEffect(opacityEffect);
    itemWidget->show();

    auto* animation = new QVariantAnimation(itemWidget);
    animation->setDuration(anim::duration(160));
    animation->setEasingCurve(visible ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    animation->setStartValue(startOpacity);
    animation->setEndValue(visible ? 1.0 : 0.0);
    m_toolbarVisibilityAnimations.insert(item, animation);

    const QPointer<QWidget> widgetGuard(itemWidget);
    const QPointer<QGraphicsOpacityEffect> effectGuard(opacityEffect);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [widgetGuard, effectGuard](const QVariant& value) {
            if (widgetGuard && effectGuard && widgetGuard->graphicsEffect() == effectGuard) {
                effectGuard->setOpacity(value.toReal());
            }
        });
    connect(animation, &QVariantAnimation::finished, this,
        [this, item, visible, widgetGuard, effectGuard, animation]() {
            if (m_toolbarVisibilityAnimations.value(item).data() != animation) {
                return;
            }
            m_toolbarVisibilityAnimations.remove(item);
            if (widgetGuard && effectGuard && widgetGuard->graphicsEffect() == effectGuard) {
                widgetGuard->setGraphicsEffect(nullptr);
                widgetGuard->setVisible(visible);
            }
            animation->deleteLater();
        });
    animation->start();
}

QJsonObject LayersPanel::savePanelState() const
{
    QJsonArray order;
    for (ToolbarItem item : m_toolbarOrder) {
        order.append(toolbarItemKey(item));
    }
    QJsonArray hidden;
    for (ToolbarItem item : m_hiddenToolbarItems) {
        hidden.append(toolbarItemKey(item));
    }

    QJsonObject state;
    state[QStringLiteral("toolbarOrder")] = order;
    state[QStringLiteral("hiddenToolbarItems")] = hidden;
    return state;
}

void LayersPanel::restorePanelState(const QJsonObject& state)
{
    const QList<ToolbarItem> restoredOrder = state.contains(QStringLiteral("toolbarOrder"))
        ? normalizedToolbarOrder(state.value(QStringLiteral("toolbarOrder")).toArray())
        : defaultToolbarOrder();
    const QList<ToolbarItem> restoredHidden = state.contains(QStringLiteral("hiddenToolbarItems"))
        ? normalizedHiddenToolbarItems(state.value(QStringLiteral("hiddenToolbarItems")).toArray())
        : QList<ToolbarItem> {};
    if (restoredOrder == m_toolbarOrder && restoredHidden == m_hiddenToolbarItems) {
        return;
    }

    m_toolbarOrder = restoredOrder;
    m_hiddenToolbarItems = restoredHidden;
    applyToolbarOrder(false);
}

void LayersPanel::startToolbarDrag(ToolbarItem item, QWidget* itemWidget, const QPoint& globalPos)
{
    if (!itemWidget || !m_toolbarFlow || m_toolbarDragActive || m_toolbarDragSettling
        || !m_toolbarOrder.contains(item)) {
        return;
    }

    if (auto* button = dynamic_cast<LayerToolbarButton*>(itemWidget)) {
        button->cancelPointerInteraction();
    }

    const QPixmap snapshot = itemWidget->grab();
    m_toolbarDragActive = true;
    m_toolbarDraggedItem = item;
    m_toolbarDraggedWidget = itemWidget;
    m_toolbarDragStartOrder = m_toolbarOrder;
    m_toolbarDragOffset = itemWidget->mapToGlobal(QPoint(0, 0)) - globalPos;

    auto* opacityEffect = new QGraphicsOpacityEffect(itemWidget);
    opacityEffect->setOpacity(0.25);
    itemWidget->setGraphicsEffect(opacityEffect);

    QWidget* topLevel = m_toolbarFlow->window();
    m_toolbarDragGhost = new ruwa::ui::widgets::DragGhostWidget(topLevel);
    m_toolbarDragGhost->setSnapshot(snapshot);
    const QPoint sourcePosition = topLevel->mapFromGlobal(itemWidget->mapToGlobal(QPoint(0, 0)))
        - m_toolbarDragGhost->contentTopLeft();
    m_toolbarDragGhost->startFollowing(sourcePosition);
    m_toolbarDragGhost->captureBackdrop(topLevel);
    m_toolbarDragGhost->show();
    m_toolbarDragGhost->raise();
    m_toolbarDragGhost->setFollowTarget(toolbarGhostTargetPosition(globalPos));

    qApp->installEventFilter(this);
    if (!m_toolbarDragCursorOverride) {
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        m_toolbarDragCursorOverride = true;
    }
}

void LayersPanel::updateToolbarDrag(const QPoint& globalPos)
{
    if (!m_toolbarDragActive || !m_toolbarDragGhost || !m_toolbarFlow) {
        return;
    }
    m_toolbarDragGhost->setFollowTarget(toolbarGhostTargetPosition(globalPos));
    const QPoint contentPos = m_toolbarFlow->mapFromGlobal(globalPos);
    // Clamp to the toolbar bounds so the insert position keeps resolving to the
    // nearest slot even once the cursor leaves the toolbar, instead of freezing.
    const QPoint clampedPos(qBound(0, contentPos.x(), qMax(0, m_toolbarFlow->width() - 1)),
        qBound(0, contentPos.y(), qMax(0, m_toolbarFlow->height() - 1)));
    moveDraggedToolbarItemTo(toolbarInsertIndexAt(clampedPos));
}

void LayersPanel::finishToolbarDrag(bool accepted, const QPoint& globalPos)
{
    if (!m_toolbarDragActive) {
        return;
    }
    updateToolbarDrag(globalPos);

    qApp->removeEventFilter(this);
    if (m_toolbarDragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_toolbarDragCursorOverride = false;
    }
    m_toolbarDragActive = false;
    m_toolbarDragSettling = true;

    const bool orderChanged = accepted && m_toolbarOrder != m_toolbarDragStartOrder;
    if (!accepted) {
        m_toolbarOrder = m_toolbarDragStartOrder;
        applyToolbarOrder(true);
    } else if (orderChanged) {
        emit panelStateChanged();
    }

    QPoint targetPosition;
    if (QWidget* draggedWidget = toolbarItemWidget(m_toolbarDraggedItem)) {
        const QRect targetRect = m_toolbarFlow->itemTargetGeometry(draggedWidget);
        QWidget* topLevel = m_toolbarFlow->window();
        targetPosition = topLevel->mapFromGlobal(m_toolbarFlow->mapToGlobal(targetRect.topLeft()));
        if (m_toolbarDragGhost) {
            targetPosition -= m_toolbarDragGhost->contentTopLeft();
        }
    }

    QPointer<LayersPanel> guard(this);
    auto finish = [guard]() {
        if (!guard) {
            return;
        }
        if (guard->m_toolbarDraggedWidget) {
            guard->m_toolbarDraggedWidget->setGraphicsEffect(nullptr);
            if (auto* button = dynamic_cast<LayerToolbarButton*>(guard->m_toolbarDraggedWidget)) {
                button->cancelPointerInteraction();
            }
        }
        if (guard->m_toolbarDragGhost) {
            guard->m_toolbarDragGhost->deleteLater();
            guard->m_toolbarDragGhost = nullptr;
        }
        guard->m_toolbarDraggedWidget = nullptr;
        guard->m_toolbarDragStartOrder.clear();
        guard->m_toolbarDragSettling = false;
        guard->cancelToolbarDragCandidate();
    };

    if (!m_toolbarDragGhost) {
        finish();
        return;
    }
    m_toolbarDragGhost->animateTo(targetPosition,
        accepted ? ruwa::ui::widgets::DragGhostWidget::Transition::Settle
                 : ruwa::ui::widgets::DragGhostWidget::Transition::Return,
        std::move(finish));
}

QPoint LayersPanel::toolbarGhostTargetPosition(const QPoint& globalPos) const
{
    if (!m_toolbarDragGhost || !m_toolbarFlow) {
        return {};
    }
    QWidget* topLevel = m_toolbarFlow->window();
    return topLevel->mapFromGlobal(globalPos + m_toolbarDragOffset)
        - m_toolbarDragGhost->contentTopLeft();
}

/*
 * Like the tools panel, the insertion resolver reads the flow's final target
 * rectangles rather than the transient on-screen ones, so a fast pointer stays
 * stable while the neighbours are still gliding into their new slots.
 */
int LayersPanel::toolbarInsertIndexAt(const QPoint& contentPos) const
{
    struct Placement {
        int stationaryIndex = 0;
        QRect rect;
    };

    QList<Placement> placements;
    int stationaryIndex = 0;
    for (ToolbarItem item : m_toolbarOrder) {
        if (item == m_toolbarDraggedItem) {
            continue;
        }
        QWidget* itemWidget = toolbarItemWidget(item);
        if (!itemWidget) {
            continue;
        }
        // A separator the flow dropped at a row edge is hidden and has no slot.
        const QRect rect = m_toolbarFlow->itemTargetGeometry(itemWidget);
        if (itemWidget->isVisible() && rect.isValid()) {
            placements.append({ stationaryIndex, rect });
        }
        ++stationaryIndex;
    }

    if (placements.isEmpty()) {
        return 0;
    }
    if (contentPos.y() < placements.front().rect.top()) {
        return 0;
    }
    if (contentPos.y() > placements.back().rect.bottom()) {
        return static_cast<int>(placements.size());
    }

    int closestRowCenter = placements.front().rect.center().y();
    int closestDistance = qAbs(contentPos.y() - closestRowCenter);
    for (const Placement& placement : placements) {
        const int center = placement.rect.center().y();
        const int distance = qAbs(contentPos.y() - center);
        if (distance < closestDistance) {
            closestDistance = distance;
            closestRowCenter = center;
        }
    }

    int indexAfterRow = 0;
    for (const Placement& placement : placements) {
        if (placement.rect.center().y() != closestRowCenter) {
            continue;
        }
        if (contentPos.x() < placement.rect.center().x()) {
            return placement.stationaryIndex;
        }
        indexAfterRow = placement.stationaryIndex + 1;
    }
    return indexAfterRow;
}

void LayersPanel::moveDraggedToolbarItemTo(int insertIndex)
{
    QList<ToolbarItem> reordered = m_toolbarOrder;
    if (!reordered.removeOne(m_toolbarDraggedItem)) {
        return;
    }
    reordered.insert(
        qBound(0, insertIndex, static_cast<int>(reordered.size())), m_toolbarDraggedItem);
    if (reordered == m_toolbarOrder) {
        return;
    }
    m_toolbarOrder = reordered;
    applyToolbarOrder(true);
}

void LayersPanel::cancelToolbarDragCandidate()
{
    m_toolbarDragCandidate = nullptr;
    m_toolbarDragPressPosition = {};
}

bool LayersPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (m_toolbarDragActive && m_toolbarDragGhost) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            updateToolbarDrag(mouseEvent->globalPosition().toPoint());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                finishToolbarDrag(true, mouseEvent->globalPosition().toPoint());
                return true;
            }
            break;
        }
        case QEvent::KeyPress: {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                finishToolbarDrag(false, QCursor::pos());
                return true;
            }
            break;
        }
        case QEvent::ApplicationDeactivate:
            finishToolbarDrag(false, QCursor::pos());
            break;
        default:
            break;
        }
    }

    // Candidate tracking is only meaningful before a drag starts; while one runs the
    // block above already consumes the pointer stream (and this filter sits on qApp,
    // so the cast would otherwise run for every event in the application).
    LayerToolbarButton* button
        = m_toolbarDragActive ? nullptr : dynamic_cast<LayerToolbarButton*>(watched);
    if (button) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && !m_toolbarDragSettling) {
                m_toolbarDragCandidate = button;
                m_toolbarDragPressPosition = mouseEvent->position().toPoint();
            }
            break;
        }
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_toolbarDragCandidate == button && (mouseEvent->buttons() & Qt::LeftButton)
                && (mouseEvent->position().toPoint() - m_toolbarDragPressPosition).manhattanLength()
                    >= QApplication::startDragDistance()) {
                for (ToolbarItem item : std::as_const(m_toolbarOrder)) {
                    if (toolbarItemWidget(item) == button) {
                        startToolbarDrag(item, button, mouseEvent->globalPosition().toPoint());
                        return true;
                    }
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease:
        case QEvent::Hide:
            cancelToolbarDragCandidate();
            break;
        default:
            break;
        }
    }

    return DockPanel::eventFilter(watched, event);
}

void LayersPanel::populateBlendModeCombo()
{
    if (!m_blendModeCombo)
        return;

    BlendMode currentMode = BlendMode::Normal;
    if (auto* layer = m_layerModel.selectedLayer()) {
        currentMode = layer->blendMode;
    }

    m_blendModeCombo->clear();
    for (const auto& category : blendModeCategoryDefs()) {
        m_blendModeCombo->addCategory(tr(category.label));
        for (const auto mode : category.modes) {
            m_blendModeCombo->addItem(
                blendModeDisplayName(mode, "LayersPanel"), static_cast<int>(mode));
        }
    }

    const int idx = m_blendModeCombo->findIndexByData(static_cast<int>(currentMode));
    m_blendModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void LayersPanel::retranslateUi()
{
    setTitle(tr("Layers"));
    if (m_blendModeCombo) {
        m_blendModeCombo->setPlaceholderText(tr("Blend mode"));
        populateBlendModeCombo();
    }
    if (m_btnAdd)
        m_btnAdd->setToolTip(tr("Add Layer"));
    if (m_btnAdjustment)
        m_btnAdjustment->setToolTip(tr("Add Adjustment Layer"));
    if (m_btnDuplicate)
        m_btnDuplicate->setToolTip(tr("Duplicate Layer"));
    if (m_btnMergeDown)
        m_btnMergeDown->setToolTip(tr("Merge Layers"));
    if (m_btnGroup)
        m_btnGroup->setToolTip(tr("Add Group"));
    if (m_btnMask)
        m_btnMask->setToolTip(tr("Add Mask"));
    if (m_btnAlphaLock)
        m_btnAlphaLock->setToolTip(tr("Alpha Lock"));
    if (m_btnLock)
        m_btnLock->setToolTip(tr("Lock"));
    if (m_btnDelete)
        m_btnDelete->setToolTip(tr("Delete Layer"));
    syncLayerControls();
}

void LayersPanel::applyToolbarTheme()
{
    // Toolbar buttons paint themselves from the active theme; nothing extra to do
    // here since the adaptive flow container has no themed chrome of its own.
}

void LayersPanel::onThemeChanged()
{
    setSubtitleBackground(colors().surface);
    applyToolbarTheme();
    if (m_listView) {
        m_listView->update();
    }
    syncLayerControls();
}

// ============================================================================
// Public
// ============================================================================

void LayersPanel::refreshLayers()
{
    if (m_listView) {
        m_listView->setModel(&m_layerModel);
    }
}

void LayersPanel::setCanvasSize(const QSize& size)
{
    setDisplayFrame(QRect(0, 0, size.width(), size.height()));
}

void LayersPanel::setDisplayFrame(const QRect& frame)
{
    if (m_displayFrame == frame)
        return;
    m_displayFrame = frame;
    if (m_listView) {
        m_listView->setDisplayFrame(m_displayFrame);
    }
}

void LayersPanel::setInsertAnimationsEnabled(bool enabled)
{
    if (m_listView) {
        m_listView->setInsertAnimationsEnabled(enabled);
    }
}

void LayersPanel::setPushUndoFn(PushUndoFn fn)
{
    m_pushUndoFn = std::move(fn);
}

void LayersPanel::setUndoCallbacks(
    RequestRenderFn requestRender, OnContentChangedFn onContentChanged)
{
    m_requestRenderFn = std::move(requestRender);
    m_onContentChangedFn = std::move(onContentChanged);
}

void LayersPanel::setFillMaskFromSelectionFn(FillMaskFromSelectionFn fn)
{
    m_fillMaskFromSelectionFn = std::move(fn);
}

void LayersPanel::setCanvasLayerOps(CanvasLayerOps ops)
{
    m_canvasLayerOps = std::move(ops);
}

void LayersPanel::scheduleThumbnailRefresh()
{
    if (!m_thumbnailRefreshTimer.isActive()) {
        m_thumbnailRefreshTimer.start();
    }
}

void LayersPanel::invalidateLayerThumbnails(const QList<LayerId>& ids)
{
    for (const LayerId& id : ids) {
        if (auto* layer = m_layerModel.layerById(id)) {
            layer->thumbnail = QPixmap();
            layer->thumbnailDirty = !layer->isGroup();
        }
    }

    if (m_listView) {
        m_listView->invalidateThumbnails(ids);
    }
}

void LayersPanel::setThumbnailLoadingMode(bool active)
{
    if (m_listView) {
        m_listView->setThumbnailLoadingMode(active);
    }
}

bool LayersPanel::visibleThumbnailsReady() const
{
    return !m_listView || m_listView->visibleThumbnailsReady();
}

void LayersPanel::preparePresentationSnapshot()
{
    if (m_listView) {
        m_listView->preparePresentationSnapshot();
    }

    // A snapshot must not capture a half-faded toolbar button.
    for (auto it = m_toolbarVisibilityAnimations.begin(); it != m_toolbarVisibilityAnimations.end();
        ++it) {
        if (it.value()) {
            it.value()->stop();
            it.value()->deleteLater();
        }
    }
    m_toolbarVisibilityAnimations.clear();

    for (ToolbarItem item : defaultToolbarOrder()) {
        QWidget* itemWidget = toolbarItemWidget(item);
        if (!itemWidget || item == ToolbarItem::Separator) {
            continue;
        }
        if (itemWidget != m_toolbarDraggedWidget) {
            itemWidget->setGraphicsEffect(nullptr);
        }
        itemWidget->setVisible(m_appliedToolbarOrder.contains(item));
    }
}

void LayersPanel::setFillProcessingLayer(const LayerId& id)
{
    if (m_listView) {
        m_listView->setForcedThumbnailLoadingLayer(id);
    }
}

LayerData* LayersPanel::selectedLayer() const
{
    return m_layerModel.selectedLayer();
}

void LayersPanel::selectLayer(const LayerId& id)
{
    if (m_layerModel.selectedLayerId() == id && m_layerModel.selectionCount() == 1) {
        return;
    }
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
}

bool LayersPanel::copySelectedLayerSnapshots(QList<std::shared_ptr<LayerData>>* snapshots) const
{
    if (!snapshots) {
        return false;
    }

    snapshots->clear();

    const auto selected = m_layerModel.selectedLayerIds();
    if (selected.isEmpty()) {
        return false;
    }

    const QList<LayerData*> flat = m_layerModel.allLayersFlattened();
    for (LayerData* layer : flat) {
        if (!layer || !selected.contains(layer->id) || layer->isBackground()) {
            continue;
        }
        bool hasSelectedAncestor = false;
        for (LayerData* anc = layer->parent; anc; anc = anc->parent) {
            if (selected.contains(anc->id)) {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (hasSelectedAncestor) {
            continue;
        }

        auto clone = LayerModel::cloneLayerTree(layer, false);
        if (clone) {
            snapshots->append(clone);
        }
    }

    return !snapshots->isEmpty();
}

bool LayersPanel::pasteLayerSnapshots(const QList<std::shared_ptr<LayerData>>& snapshots)
{
    if (snapshots.isEmpty()) {
        return false;
    }

    const bool hasPasteableSnapshot = std::any_of(
        snapshots.cbegin(), snapshots.cend(), [](const std::shared_ptr<LayerData>& snapshot) {
            return snapshot && !snapshot->isBackground();
        });
    if (!hasPasteableSnapshot) {
        return false;
    }
    emit aboutToPerformTransformIncompatibleEdit();

    if (m_listView) {
        m_listView->setFocus(Qt::OtherFocusReason);
    }

    LayerId parentId;
    int insertIndex = 0;
    if (LayerData* selected = m_layerModel.selectedLayer()) {
        if (selected->parent) {
            parentId = selected->parent->id;
            insertIndex = selected->indexInParent();
        } else {
            const auto& roots = m_layerModel.rootLayers();
            for (int i = 0; i < roots.size(); ++i) {
                if (roots[i].get() == selected) {
                    insertIndex = i;
                    break;
                }
            }
        }
    }

    QList<LayerId> addedIds;
    int offset = 0;
    for (const auto& snapshot : snapshots) {
        auto clone = LayerModel::cloneLayerTree(snapshot.get(), false);
        if (!clone || clone->isBackground()) {
            continue;
        }

        const LayerId addedId = clone->id;
        const int position = insertIndex >= 0 ? insertIndex + offset : -1;
        if (parentId.isNull()) {
            m_layerModel.addLayer(clone, position);
        } else {
            m_layerModel.addLayerTo(clone, parentId, position);
        }

        if (m_layerModel.layerById(addedId)) {
            addedIds.append(addedId);
            ++offset;
        }
    }

    if (addedIds.isEmpty()) {
        return false;
    }

    if (m_pushUndoFn) {
        QList<std::shared_ptr<LayerData>> undoClones;
        QList<std::pair<LayerId, int>> positions;
        for (const LayerId& id : addedIds) {
            LayerData* layer = m_layerModel.layerById(id);
            auto clone = LayerModel::cloneLayerTree(layer, true);
            if (!layer || !clone)
                continue;
            undoClones.append(clone);

            LayerId layerParentId;
            int index = -1;
            if (layer->parent) {
                layerParentId = layer->parent->id;
                index = layer->indexInParent();
            } else {
                const auto& roots = m_layerModel.rootLayers();
                for (int i = 0; i < roots.size(); ++i) {
                    if (roots[i].get() == layer) {
                        index = i;
                        break;
                    }
                }
            }
            positions.append({ layerParentId, index });
        }

        if (!undoClones.isEmpty()) {
            auto cmd
                = std::make_unique<aether::LayerAddCommand>(&m_layerModel, std::move(undoClones),
                    std::move(positions), m_requestRenderFn, m_onContentChangedFn);
            m_pushUndoFn(std::move(cmd));
        }
    }

    m_layerModel.setSelectedLayer(addedIds.first());
    for (int i = 1; i < addedIds.size(); ++i) {
        m_layerModel.addToSelection(addedIds[i]);
    }

    syncLayerControls();
    return true;
}

bool LayersPanel::deleteSelectedLayers()
{
    const auto selected = m_layerModel.selectedLayerIds();
    if (selected.isEmpty()) {
        return false;
    }

    if (m_listView) {
        m_listView->setFocus(Qt::OtherFocusReason);
    }

    const QList<LayerData*> flat = m_layerModel.allLayersFlattened();
    QList<LayerData*> removeRoots;
    for (LayerData* layer : flat) {
        if (!layer || !selected.contains(layer->id) || layer->isBackground()) {
            continue;
        }
        bool hasSelectedAncestor = false;
        for (LayerData* anc = layer->parent; anc; anc = anc->parent) {
            if (selected.contains(anc->id)) {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (!hasSelectedAncestor) {
            removeRoots.append(layer);
        }
    }
    if (removeRoots.isEmpty()) {
        return false;
    }

    emit aboutToPerformTransformIncompatibleEdit();

    QList<std::shared_ptr<LayerData>> clones;
    QList<std::pair<LayerId, int>> restorePositions;
    for (LayerData* root : removeRoots) {
        auto clone = LayerModel::cloneLayerTree(root, true);
        if (!clone)
            continue;
        clones.append(clone);
        LayerId parentId;
        int index = -1;
        if (root->parent) {
            parentId = root->parent->id;
            index = root->indexInParent();
        } else {
            const auto& roots = m_layerModel.rootLayers();
            for (int i = 0; i < roots.size(); ++i) {
                if (roots[i].get() == root) {
                    index = i;
                    break;
                }
            }
        }
        restorePositions.append({ parentId, index });
    }

    QList<LayerId> idsToRemove;
    for (LayerData* root : removeRoots) {
        idsToRemove.append(root->id);
    }
    m_layerModel.removeLayers(idsToRemove);
    if (m_listView) {
        m_listView->setFocus(Qt::OtherFocusReason);
    }

    if (m_pushUndoFn && !clones.isEmpty()) {
        auto cmd = std::make_unique<aether::LayerRemoveCommand>(&m_layerModel, std::move(clones),
            std::move(restorePositions), m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }

    syncLayerControls();
    emit deleteLayerRequested();
    return true;
}

bool LayersPanel::duplicateSelectedLayers(bool detachSmartContent)
{
    bool hasDuplicableLayer = false;
    for (const LayerId& id : m_layerModel.selectedLayerIds()) {
        const auto* layer = m_layerModel.layerById(id);
        if (layer && !layer->isBackground()) {
            hasDuplicableLayer = true;
            break;
        }
    }
    if (!hasDuplicableLayer) {
        return false;
    }

    emit aboutToPerformTransformIncompatibleEdit();
    const QList<LayerId> addedIds = m_layerModel.duplicateSelectedLayers();
    if (addedIds.isEmpty()) {
        return false;
    }

    if (detachSmartContent) {
        // "New Smart Object via Copy": the duplicate came out sharing its
        // source's content (that is what makes a plain duplicate an instance),
        // so give it a private copy. Done BEFORE the undo snapshot below, which
        // must describe the already-detached layer.
        for (const LayerId& id : addedIds) {
            if (auto* added = m_layerModel.layerById(id); added && added->smartContent) {
                added->detachSmartContent();
            }
        }
        // duplicateSelectedLayers() emitted layersChanged before the detach, so
        // the counts it refreshed still describe the shared state. The refresh
        // repaints both the copy's row and the source's.
        m_layerModel.refreshSmartInstanceCounts();
    }

    if (m_pushUndoFn) {
        const QList<LayerData*> flat = m_layerModel.allLayersFlattened();
        QList<LayerData*> addRoots;
        for (LayerData* layer : flat) {
            if (!layer || !addedIds.contains(layer->id))
                continue;
            bool hasAddedAncestor = false;
            for (LayerData* anc = layer->parent; anc; anc = anc->parent) {
                if (addedIds.contains(anc->id)) {
                    hasAddedAncestor = true;
                    break;
                }
            }
            if (!hasAddedAncestor) {
                addRoots.append(layer);
            }
        }

        QList<std::shared_ptr<LayerData>> clones;
        QList<std::pair<LayerId, int>> positions;
        for (LayerData* root : addRoots) {
            auto clone = LayerModel::cloneLayerTree(root, true);
            if (!clone)
                continue;
            clones.append(clone);
            LayerId parentId;
            int index = -1;
            if (root->parent) {
                parentId = root->parent->id;
                index = root->indexInParent();
            } else {
                const auto& roots = m_layerModel.rootLayers();
                for (int i = 0; i < roots.size(); ++i) {
                    if (roots[i].get() == root) {
                        index = i;
                        break;
                    }
                }
            }
            positions.append({ parentId, index });
        }

        if (!clones.isEmpty()) {
            auto cmd = std::make_unique<aether::LayerAddCommand>(&m_layerModel, std::move(clones),
                std::move(positions), m_requestRenderFn, m_onContentChangedFn);
            m_pushUndoFn(std::move(cmd));
        }
    }

    syncLayerControls();
    return true;
}

bool LayersPanel::mergeSelectedLayerDown()
{
    const MergeDownCandidate candidate = mergeDownCandidateForSelection(m_layerModel);
    if (!candidate.isValid()) {
        return false;
    }

    const QList<LayerId> participants { candidate.sourceId, candidate.targetId };
    if (mergeBlockedByUnbakeableMask(m_layerModel, participants)) {
        showMergeWarning(hiddenMaskMergeWarning());
        return false;
    }

    emit aboutToPerformTransformIncompatibleEdit();

    // Preparation and merge form one undo step, and redo replays them in this
    // order — a redo that skipped the preparation would composite the smart
    // layer's untransformed, unmasked pixels instead.
    UndoTransactionScope transaction(m_canvasLayerOps, tr("Merge Down"));
    if (!prepareLayersForMerge(participants)) {
        return false;
    }

    // Snapshot AFTER preparation: this command's undo restores the prepared
    // (rasterized, mask-free) layers, and the preparation commands sitting
    // before it in the transaction then walk them back to smart and masked.
    auto* source = m_layerModel.layerById(candidate.sourceId);
    auto* target = m_layerModel.layerById(candidate.targetId);
    auto sourceSnapshot = LayerModel::cloneLayerTree(source, true);
    auto targetSnapshot = LayerModel::cloneLayerTree(target, true);
    if (!sourceSnapshot || !targetSnapshot) {
        return false;
    }

    if (!applyMergeDown(m_layerModel, candidate.sourceId, candidate.targetId)) {
        return false;
    }

    if (m_pushUndoFn) {
        auto cmd = std::make_unique<MergeDownUndoCommand>(&m_layerModel, std::move(sourceSnapshot),
            std::move(targetSnapshot), candidate.parentId, candidate.sourceIndex,
            candidate.targetIndex, m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }

    syncLayerControls();
    return true;
}

bool LayersPanel::applyQuickClippingMask()
{
    const auto& selected = m_layerModel.selectedLayerIds();
    QList<aether::LayerPropertyCommand::Entry> entries;
    for (const LayerId& id : selected) {
        auto* layer = m_layerModel.layerById(id);
        if (!layer || layer->isBackground())
            continue;
        entries.append({ id, layer->clippedToBelow, QVariant() });
    }

    const bool changed = m_layerModel.applyQuickClippingMaskToSelection();
    if (changed && m_pushUndoFn && !entries.isEmpty()) {
        for (auto& e : entries) {
            if (auto* layer = m_layerModel.layerById(e.layerId)) {
                e.newValue = layer->clippedToBelow;
            }
        }
        QList<aether::LayerPropertyCommand::Entry> changedEntries;
        for (const auto& e : entries) {
            if (e.newValue.isValid() && e.oldValue.toBool() != e.newValue.toBool()) {
                changedEntries.append(e);
            }
        }
        if (!changedEntries.isEmpty()) {
            auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
                aether::LayerPropertyCommand::Property::ClippedToBelow, std::move(changedEntries),
                m_requestRenderFn, m_onContentChangedFn);
            m_pushUndoFn(std::move(cmd));
        }
    }
    if (changed) {
        syncLayerControls();
    }
    return changed;
}

bool LayersPanel::toggleSelectedLayerVisibility()
{
    const auto& selected = m_layerModel.selectedLayerIds();
    QList<aether::LayerPropertyCommand::Entry> entries;
    for (const LayerId& id : selected) {
        auto* layer = m_layerModel.layerById(id);
        if (!layer || layer->isBackground())
            continue;
        entries.append({ id, layer->visible, QVariant() });
    }

    const bool changed = m_layerModel.toggleVisibilityForSelection();
    if (changed && m_pushUndoFn && !entries.isEmpty()) {
        for (auto& e : entries) {
            if (auto* layer = m_layerModel.layerById(e.layerId)) {
                e.newValue = layer->visible;
            }
        }
        QList<aether::LayerPropertyCommand::Entry> changedEntries;
        for (const auto& e : entries) {
            if (e.newValue.isValid() && e.oldValue.toBool() != e.newValue.toBool()) {
                changedEntries.append(e);
            }
        }
        if (!changedEntries.isEmpty()) {
            auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
                aether::LayerPropertyCommand::Property::Visible, std::move(changedEntries),
                m_requestRenderFn, m_onContentChangedFn);
            m_pushUndoFn(std::move(cmd));
        }
    }
    if (changed) {
        syncLayerControls();
    }
    return changed;
}

bool LayersPanel::canMergeSelectedLayerDown() const
{
    return mergeDownCandidateForSelection(m_layerModel).isValid();
}

QString LayersPanel::hiddenMaskMergeWarning()
{
    return tr("A hidden or locked layer with a mask can't be merged. Show the layer, "
              "or delete its mask, first.");
}

bool LayersPanel::prepareLayersForMerge(const QList<LayerId>& ids)
{
    for (const LayerId& id : ids) {
        auto* layer = m_layerModel.layerById(id);
        if (!layer) {
            return false;
        }

        if (layer->isSmart()) {
            // Merge composites raw grids, so the smart transform has to be baked
            // into pixels first — otherwise the object would fold in at its
            // content origin, unscaled.
            if (!m_canvasLayerOps.rasterizeLayer || !m_canvasLayerOps.rasterizeLayer(id)) {
                return false;
            }
            layer = m_layerModel.layerById(id);
            if (!layer || !layer->isRaster()) {
                return false;
            }
        }

        if (layer->hasMask()) {
            // Same reason: the merge never reads the mask, so leaving it on the
            // layer would resurrect everything the mask hides.
            if (!m_canvasLayerOps.applyLayerMask || !m_canvasLayerOps.applyLayerMask(id)) {
                return false;
            }
        }
    }
    return true;
}

bool LayersPanel::mergeLayerSet(
    const QList<LayerData*>& orderedTopToBottom, const QString& undoLabel)
{
    if (orderedTopToBottom.size() < 2) {
        return false;
    }

    QList<LayerId> orderedIds;
    QList<MergeLayersUndoCommand::SnapshotEntry> snapshots;
    orderedIds.reserve(orderedTopToBottom.size());
    snapshots.reserve(orderedTopToBottom.size());

    for (LayerData* layer : orderedTopToBottom) {
        if (!layer) {
            return false;
        }
        orderedIds.append(layer->id);
    }

    if (mergeBlockedByUnbakeableMask(m_layerModel, orderedIds)) {
        showMergeWarning(hiddenMaskMergeWarning());
        return false;
    }

    emit aboutToPerformTransformIncompatibleEdit();

    // See mergeSelectedLayerDown for why preparation, snapshots and the merge
    // are ordered this way inside one transaction.
    UndoTransactionScope transaction(m_canvasLayerOps, undoLabel);
    if (!prepareLayersForMerge(orderedIds)) {
        return false;
    }

    for (const LayerId& id : orderedIds) {
        // Re-resolve: preparation replaced layer state (never identity), but the
        // snapshot must be taken from the prepared layer.
        LayerData* layer = m_layerModel.layerById(id);
        if (!layer) {
            return false;
        }
        auto snapshot = LayerModel::cloneLayerTree(layer, true);
        if (!snapshot) {
            return false;
        }
        const LayerId parentId = layer->parent ? layer->parent->id : LayerId();
        const int index
            = layer->parent ? layer->indexInParent() : rootLayerIndex(m_layerModel, layer);
        snapshots.append({ std::move(snapshot), parentId, index });
    }

    if (applyMergeLayers(m_layerModel, orderedIds).isNull()) {
        return false;
    }

    if (m_pushUndoFn) {
        auto cmd = std::make_unique<MergeLayersUndoCommand>(&m_layerModel, orderedIds,
            std::move(snapshots), undoLabel, m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }

    syncLayerControls();
    return true;
}

bool LayersPanel::mergeVisibleLayers()
{
    return mergeLayerSet(collectMergeVisibleLayers(m_layerModel), tr("Merge Visible"));
}

bool LayersPanel::canMergeVisibleLayers() const
{
    return collectMergeVisibleLayers(m_layerModel).size() >= 2;
}

bool LayersPanel::mergeSelectedLayers()
{
    return mergeLayerSet(collectMergeSelectedLayers(m_layerModel), tr("Merge Layers"));
}

bool LayersPanel::canMergeSelectedLayers() const
{
    return collectMergeSelectedLayers(m_layerModel).size() >= 2;
}

void LayersPanel::showMergeWarning(const QString& message)
{
    ruwa::ui::widgets::MessagePopupManager::show(
        this, message, { { tr("OK"), false, []() { } } }, 360);
}

bool LayersPanel::hasMergeIntent() const
{
    const int selCount = m_layerModel.selectionCount();

    if (selCount >= 2) {
        // A merge is offered when the selection is mergeable, or when it contains a
        // layer we should explicitly warn about (Background / Board).
        if (canMergeSelectedLayers()) {
            return true;
        }
        for (LayerData* layer : m_layerModel.selectedLayers()) {
            if (layer && (layer->isBackground() || layer->isBoard())) {
                return true;
            }
        }
        return false;
    }

    if (selCount == 1) {
        LayerData* source = m_layerModel.selectedLayer();
        LayerData* target = layerBelowSibling(m_layerModel, source);
        if (!target) {
            return false;
        }
        if (canMergeSelectedLayerDown()) {
            return true;
        }
        return target->isBackground() || target->isBoard() || (source && source->isBoard());
    }

    return false;
}

bool LayersPanel::performMerge()
{
    const QString backgroundWarning = tr("The Background layer can't be merged.");
    // Smart layers merge fine now (they are rasterized first). A Board lives
    // outside document bounds, so folding it in stays an explicit conversion.
    const QString boardWarning = tr("Board layers can't be merged. Rasterize the layer first.");

    const int selCount = m_layerModel.selectionCount();

    if (selCount >= 2) {
        if (canMergeSelectedLayers()) {
            return mergeSelectedLayers();
        }
        // Mergeable selection is impossible — explain why when a special layer is involved.
        bool hasBackground = false;
        bool hasBoard = false;
        for (LayerData* layer : m_layerModel.selectedLayers()) {
            if (!layer) {
                continue;
            }
            if (layer->isBackground()) {
                hasBackground = true;
            } else if (layer->isBoard()) {
                hasBoard = true;
            }
        }
        if (hasBackground) {
            showMergeWarning(backgroundWarning);
        } else if (hasBoard) {
            showMergeWarning(boardWarning);
        }
        return false;
    }

    if (selCount == 1) {
        if (canMergeSelectedLayerDown()) {
            return mergeSelectedLayerDown();
        }
        LayerData* source = m_layerModel.selectedLayer();
        LayerData* target = layerBelowSibling(m_layerModel, source);
        if (!target) {
            return false;
        }
        if (target->isBackground()) {
            showMergeWarning(backgroundWarning);
        } else if ((source && source->isBoard()) || target->isBoard()) {
            showMergeWarning(boardWarning);
        }
        return false;
    }

    return false;
}

// ============================================================================
// List View Slots
// ============================================================================

void LayersPanel::onLayerSelected(const LayerId& id, Qt::KeyboardModifiers modifiers)
{
    const bool selectionWillChange = (modifiers & (Qt::ControlModifier | Qt::ShiftModifier))
        || m_layerModel.selectedLayerId() != id || m_layerModel.selectionCount() != 1;
    if (selectionWillChange) {
        emit aboutToPerformTransformIncompatibleEdit();
    }

    if (modifiers & Qt::ControlModifier) {
        m_layerModel.toggleSelection(id);
    } else if (modifiers & Qt::ShiftModifier) {
        LayerId primary = m_layerModel.selectedLayerId();
        if (!primary.isNull()) {
            m_layerModel.selectRange(primary, id);
        } else {
            m_layerModel.setSelectedLayer(id);
        }
    } else {
        m_layerModel.setSelectedLayer(id);
    }

    syncLayerControls();

    emit layerSelected(id);
}

void LayersPanel::onLayerPaintTargetSelected(
    const LayerId& id, bool maskTarget, Qt::KeyboardModifiers modifiers)
{
    auto* layer = m_layerModel.layerById(id);
    if (!layer || !layer->hasMask()) {
        onLayerSelected(id, modifiers);
        return;
    }

    const bool selectionWillChange = (modifiers & Qt::ShiftModifier)
        || m_layerModel.selectedLayerId() != id || m_layerModel.selectionCount() != 1;
    const bool paintTargetWillChange = layer->maskEditActive != maskTarget;
    if (selectionWillChange || paintTargetWillChange) {
        emit aboutToPerformTransformIncompatibleEdit();
    }

    layer->maskEditActive = maskTarget;
    if (modifiers & Qt::ShiftModifier) {
        const LayerId primary = m_layerModel.selectedLayerId();
        if (!primary.isNull()) {
            m_layerModel.selectRange(primary, id);
        } else {
            m_layerModel.setSelectedLayer(id);
        }
    } else {
        m_layerModel.setSelectedLayer(id);
    }

    syncLayerControls();
    emit layerSelected(id);
}

void LayersPanel::onLayerContentSelectionRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    if (m_layerModel.selectedLayerId() != id) {
        m_layerModel.setSelectedLayer(id);
    }
    syncLayerControls();
    emit layerContentSelectionRequested(id);
}

void LayersPanel::onLayerMaskSelectionRequested(const LayerId& id)
{
    auto* layer = m_layerModel.layerById(id);
    if (!layer || !layer->hasMask()) {
        return;
    }

    emit aboutToPerformTransformIncompatibleEdit();
    if (m_layerModel.selectedLayerId() != id) {
        m_layerModel.setSelectedLayer(id);
    }
    syncLayerControls();
    emit layerMaskSelectionRequested(id);
}

void LayersPanel::onLayerTextEditRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    if (m_layerModel.selectedLayerId() != id) {
        m_layerModel.setSelectedLayer(id);
    }
    syncLayerControls();
    emit layerTextEditRequested(id);
}

void LayersPanel::onLayerSmartContentEditRequested(const LayerId& id)
{
    // Opening the contents does not touch the document, but it moves the user to
    // another tab — commit anything pending on this one first.
    emit aboutToPerformTransformIncompatibleEdit();
    if (m_layerModel.selectedLayerId() != id) {
        m_layerModel.setSelectedLayer(id);
        syncLayerControls();
    }
    emit layerSmartContentEditRequested(id);
}

void LayersPanel::onLayerDuplicateRequested(const LayerId& id)
{
    if (m_layerModel.selectedLayerId() != id || m_layerModel.selectionCount() != 1) {
        emit aboutToPerformTransformIncompatibleEdit();
    }
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    duplicateSelectedLayers();
}

void LayersPanel::onLayerNewSmartObjectViaCopyRequested(const LayerId& id)
{
    if (m_layerModel.selectedLayerId() != id || m_layerModel.selectionCount() != 1) {
        emit aboutToPerformTransformIncompatibleEdit();
    }
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    duplicateSelectedLayers(/*detachSmartContent=*/true);
}

void LayersPanel::onLayerDeleteRequested(const LayerId& id)
{
    if (m_layerModel.selectedLayerId() != id || m_layerModel.selectionCount() != 1) {
        emit aboutToPerformTransformIncompatibleEdit();
    }
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    deleteSelectedLayers();
}

void LayersPanel::onLayerQuickClippingMaskRequested(const LayerId& id)
{
    if (m_layerModel.selectedLayerId() != id || m_layerModel.selectionCount() != 1) {
        emit aboutToPerformTransformIncompatibleEdit();
    }
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    applyQuickClippingMask();
}

void LayersPanel::onLayerClearPixelsRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    emit layerClearPixelContentRequested(id);
}

void LayersPanel::onLayerRasterizeSmartRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    emit layerRasterizeSmartRequested(id);
}

void LayersPanel::onLayerConvertToSmartObjectRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    emit layerConvertToSmartObjectRequested(id);
}

void LayersPanel::onLayerReplaceSmartContentsRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    emit layerReplaceSmartContentsRequested(id);
}

void LayersPanel::onLayerApplyMaskRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    emit layerApplyMaskRequested(id);
}

void LayersPanel::onLayerInvertMaskRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    emit layerInvertMaskRequested(id);
}

void LayersPanel::onLayerApplyEffectsRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    syncLayerControls();
    emit layerApplyEffectsRequested(id);
}

void LayersPanel::onLayerToggleAlphaLockRequested(const LayerId& id)
{
    if (m_layerModel.selectedLayerId() != id || m_layerModel.selectionCount() != 1) {
        emit aboutToPerformTransformIncompatibleEdit();
    }
    m_layerModel.setSelectedLayer(id);
    auto* layer = m_layerModel.layerById(id);
    if (!layer || !layer->isPixelLayer()) {
        syncLayerControls();
        return;
    }

    const bool oldVal = layer->alphaLock;
    const bool newVal = !oldVal;
    m_layerModel.setLayerAlphaLock(id, newVal);
    emit layerAlphaLockChanged(id, newVal);

    if (m_pushUndoFn) {
        QList<aether::LayerPropertyCommand::Entry> entries;
        entries.append({ id, oldVal, newVal });
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::AlphaLock, std::move(entries),
            m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
}

void LayersPanel::onLayerToggleLockRequested(const LayerId& id)
{
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setSelectedLayer(id);
    auto* layer = m_layerModel.layerById(id);
    if (!layer || layer->isBackground()) {
        syncLayerControls();
        return;
    }

    const bool oldVal = layer->locked;
    const bool newVal = !oldVal;
    m_layerModel.setLayerLocked(id, newVal);
    emit layerLockChanged(id, newVal);

    if (m_pushUndoFn) {
        QList<aether::LayerPropertyCommand::Entry> entries;
        entries.append({ id, oldVal, newVal });
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::Locked, std::move(entries), m_requestRenderFn,
            m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
}

bool LayersPanel::setSelectionLocked(bool locked)
{
    QList<aether::LayerPropertyCommand::Entry> entries;
    for (const LayerId& id : m_layerModel.selectedLayerIds()) {
        auto* layer = m_layerModel.layerById(id);
        if (layer && !layer->isBackground() && layer->locked != locked) {
            entries.append({ id, layer->locked, locked });
        }
    }
    if (entries.isEmpty()) {
        return false;
    }

    emit aboutToPerformTransformIncompatibleEdit();
    for (const auto& entry : entries) {
        m_layerModel.setLayerLocked(entry.layerId, locked);
        emit layerLockChanged(entry.layerId, locked);
    }
    if (m_pushUndoFn) {
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::Locked, std::move(entries), m_requestRenderFn,
            m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
    return true;
}

bool LayersPanel::setSelectionAlphaLocked(bool alphaLock)
{
    QList<aether::LayerPropertyCommand::Entry> entries;
    for (const LayerId& id : m_layerModel.selectedLayerIds()) {
        auto* layer = m_layerModel.layerById(id);
        if (layer && layer->isRaster() && layer->alphaLock != alphaLock) {
            entries.append({ id, layer->alphaLock, alphaLock });
            m_layerModel.setLayerAlphaLock(id, alphaLock);
            emit layerAlphaLockChanged(id, alphaLock);
        }
    }
    if (entries.isEmpty()) {
        return false;
    }
    if (m_pushUndoFn) {
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::AlphaLock, std::move(entries),
            m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
    return true;
}

bool LayersPanel::convertSelectionToSmartObject()
{
    // Photoshop's semantic: the selection becomes ONE object that holds those
    // layers. They are gathered under a fresh group and the group is what gets
    // converted — its children become the object's nested document roots and the
    // group layer itself turns into the smart layer, so nothing is left behind.
    const QSet<LayerId> selected = m_layerModel.selectedLayerIds();
    QList<LayerData*> roots;
    for (LayerData* layer : m_layerModel.allLayersFlattened()) {
        if (!layer || layer->isBackground() || !selected.contains(layer->id)) {
            continue;
        }
        bool hasSelectedAncestor = false;
        for (LayerData* ancestor = layer->parent; ancestor; ancestor = ancestor->parent) {
            if (selected.contains(ancestor->id)) {
                hasSelectedAncestor = true;
                break;
            }
        }
        if (!hasSelectedAncestor) {
            roots.append(layer);
        }
    }
    if (roots.isEmpty()) {
        return false;
    }

    if (roots.size() == 1) {
        // Nothing to gather. The plain conversion keeps the layer's own identity,
        // and a group converted directly does not end up nested inside another.
        if (!roots.first()->canConvertToSmartObject()) {
            return false;
        }
        onLayerConvertToSmartObjectRequested(roots.first()->id);
        return true;
    }

    emit aboutToPerformTransformIncompatibleEdit();

    // The object lands where the topmost selected layer was, and takes its name —
    // again what Photoshop does.
    LayerData* anchor = roots.first();
    const QString objectName = anchor->name;
    LayerId parentId;
    int insertIndex = 0;
    LayerData* group = nullptr;
    if (anchor->parent) {
        parentId = anchor->parent->id;
        insertIndex = anchor->indexInParent();
        group = m_layerModel.createGroupIn(anchor->parent, objectName, insertIndex);
    } else {
        insertIndex = qMax(0, rootLayerIndex(m_layerModel, anchor));
        group = m_layerModel.createGroup(objectName, insertIndex);
    }
    if (!group) {
        return false;
    }
    const LayerId groupId = group->id;

    // Recorded after the group exists, so the indices describe the stack the undo
    // will actually put the layers back into (the group is removed last).
    QList<GroupedLayerMoveRecord> moveRecords;
    QList<LayerId> moveIds;
    for (int i = 0; i < roots.size(); ++i) {
        LayerData* layer = roots[i];
        moveRecords.append({ layer->id, layer->parent ? layer->parent->id : LayerId(),
            layer->parent ? layer->indexInParent() : rootLayerIndex(m_layerModel, layer), i });
        moveIds.append(layer->id);
    }
    auto groupClone = LayerModel::cloneLayerTree(group, true);

    // Gathering and converting are one step: an undo that stopped halfway would
    // leave the user with a group they never asked for.
    const bool transactional
        = m_canvasLayerOps.beginUndoTransaction && m_canvasLayerOps.endUndoTransaction;
    if (transactional) {
        m_canvasLayerOps.beginUndoTransaction(tr("Convert to Smart Object"));
    }

    // Straight in, no insert animation: the group only lives for as long as the
    // conversion takes to consume it.
    m_layerModel.moveLayers(moveIds, group, 0);
    if (m_pushUndoFn && groupClone) {
        auto cmd = std::make_unique<CreateGroupedSelectionCommand>(&m_layerModel,
            std::move(groupClone), std::pair<LayerId, int> { parentId, insertIndex },
            std::move(moveRecords), m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }

    m_layerModel.setSelectedLayer(groupId);
    emit layerConvertToSmartObjectRequested(groupId);

    if (transactional) {
        m_canvasLayerOps.endUndoTransaction();
    }
    syncLayerControls();
    return true;
}

void LayersPanel::performSelectionCanvasOp(ruwa::ui::widgets::MultiLayerAction action)
{
    using ruwa::ui::widgets::MultiLayerAction;

    // Which layers the operation means something for. Mirrors the enabled rules
    // the context menu used when it built the entry.
    const auto eligible = [action](const LayerData* layer) {
        if (!layer || layer->isBackground()) {
            return false;
        }
        switch (action) {
        case MultiLayerAction::Rasterize:
            return layer->isIsolatedPixelLayer() || layer->isText();
        case MultiLayerAction::ClearPixels:
            return layer->isPixelLayer() && layer->isRaster();
        case MultiLayerAction::ApplyMask:
            return layer->hasMask() && !layer->isGroup() && layer->isRaster();
        case MultiLayerAction::InvertMask:
            return layer->hasMask() && !layer->isGroup() && layer->canHostMask();
        case MultiLayerAction::ApplyEffects:
            return layer->isRaster() && !layer->effects.isEmpty();
        default:
            return false;
        }
    };

    const QSet<LayerId> selected = m_layerModel.selectedLayerIds();
    QList<LayerId> targets;
    for (LayerData* layer : m_layerModel.allLayersFlattened()) {
        if (layer && selected.contains(layer->id) && eligible(layer)) {
            targets.append(layer->id);
        }
    }
    if (targets.isEmpty()) {
        return;
    }

    QString undoLabel;
    switch (action) {
    case MultiLayerAction::Rasterize:
        undoLabel = tr("Rasterize Layers");
        break;
    case MultiLayerAction::ClearPixels:
        undoLabel = tr("Clear Layers");
        break;
    case MultiLayerAction::ApplyMask:
        undoLabel = tr("Apply Masks");
        break;
    case MultiLayerAction::InvertMask:
        undoLabel = tr("Invert Masks");
        break;
    case MultiLayerAction::ApplyEffects:
        undoLabel = tr("Apply Effects");
        break;
    default:
        return;
    }

    emit aboutToPerformTransformIncompatibleEdit();

    // Each per-layer op pushes its own undo command; one transaction collapses
    // them into the single step the user asked for.
    const bool transactional = targets.size() > 1 && m_canvasLayerOps.beginUndoTransaction
        && m_canvasLayerOps.endUndoTransaction;
    if (transactional) {
        m_canvasLayerOps.beginUndoTransaction(undoLabel);
    }

    for (const LayerId& id : targets) {
        // An earlier step can consume a later target — converting a group takes
        // its children out of the model — so every layer is re-checked.
        LayerData* layer = m_layerModel.layerById(id);
        if (!eligible(layer)) {
            continue;
        }
        switch (action) {
        case MultiLayerAction::Rasterize:
            emit layerRasterizeSmartRequested(id);
            break;
        case MultiLayerAction::ClearPixels:
            emit layerClearPixelContentRequested(id);
            break;
        case MultiLayerAction::ApplyMask:
            emit layerApplyMaskRequested(id);
            break;
        case MultiLayerAction::InvertMask:
            emit layerInvertMaskRequested(id);
            break;
        case MultiLayerAction::ApplyEffects:
            emit layerApplyEffectsRequested(id);
            break;
        default:
            break;
        }
    }

    if (transactional) {
        m_canvasLayerOps.endUndoTransaction();
    }
    syncLayerControls();
}

void LayersPanel::onLayerMultiActionRequested(ruwa::ui::widgets::MultiLayerAction action)
{
    using ruwa::ui::widgets::MultiLayerAction;

    switch (action) {
    case MultiLayerAction::Duplicate:
        duplicateSelectedLayers();
        break;
    case MultiLayerAction::Delete:
        deleteSelectedLayers();
        break;
    case MultiLayerAction::Group:
        onAddGroup();
        break;
    case MultiLayerAction::Merge:
        performMerge();
        break;
    case MultiLayerAction::ConvertToSmartObject:
        convertSelectionToSmartObject();
        break;
    case MultiLayerAction::ToggleVisibility:
        toggleSelectedLayerVisibility();
        break;
    case MultiLayerAction::ToggleLock: {
        // Same rule the menu labelled itself with: all locked → unlock, else lock.
        bool allLocked = true;
        for (const LayerData* layer : m_layerModel.selectedLayers()) {
            if (layer && !layer->isBackground() && !layer->locked) {
                allLocked = false;
                break;
            }
        }
        setSelectionLocked(!allLocked);
        break;
    }
    case MultiLayerAction::ToggleAlphaLock: {
        bool allAlphaLocked = true;
        for (const LayerData* layer : m_layerModel.selectedLayers()) {
            if (layer && layer->isRaster() && (layer->isPixelLayer() || layer->isGroup())
                && !layer->alphaLock) {
                allAlphaLocked = false;
                break;
            }
        }
        setSelectionAlphaLocked(!allAlphaLocked);
        break;
    }
    default:
        performSelectionCanvasOp(action);
        break;
    }
}

void LayersPanel::onLayerExpandToggled(const LayerId& id)
{
    auto* layer = m_layerModel.layerById(id);
    if (!layer)
        return;

    const bool oldExpanded = layer->expanded;
    m_layerModel.toggleExpanded(id);

    if (m_pushUndoFn) {
        QList<aether::LayerPropertyCommand::Entry> entries;
        entries.append({ id, oldExpanded, !oldExpanded });
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::Expanded, std::move(entries), m_requestRenderFn,
            m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
}

void LayersPanel::onLayerVisibilityToggled(const LayerId& id)
{
    auto* layer = m_layerModel.layerById(id);
    if (!layer)
        return;

    const bool oldVisible = layer->visible;
    m_layerModel.toggleVisibility(id);
    emit layerVisibilityChanged(id, layer->visible);

    if (m_pushUndoFn) {
        QList<aether::LayerPropertyCommand::Entry> entries;
        entries.append({ id, oldVisible, !oldVisible });
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::Visible, std::move(entries), m_requestRenderFn,
            m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
}

void LayersPanel::onLayerDragDropped(const LayerId& id, int dropInsertIndex, int targetDepth)
{
    m_listView->setDropRejected(false); // Clear stale state

    auto pushReorderUndo = [this](const LayerId& newParentId, QList<LayerReorderRecord> records) {
        if (!m_pushUndoFn || records.isEmpty()) {
            return;
        }
        auto cmd = std::make_unique<ReorderLayersCommand>(&m_layerModel, newParentId,
            std::move(records), m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    };

    auto flat = m_layerModel.flattenedLayers();

    // Collect all selected IDs that should be moved
    auto selectedIds = m_layerModel.selectedLayerIds();
    QList<LayerId> layersToMove;

    if (selectedIds.size() > 1 && selectedIds.contains(id)) {
        // Multi-drag: collect selected layers in visual order,
        // excluding descendants of other selected layers (they move with parent)
        QSet<LayerId> skipIds;
        for (const LayerId& selId : selectedIds) {
            auto* layer = m_layerModel.layerById(selId);
            if (layer) {
                QList<LayerData*> descs;
                layer->flatten(descs, false);
                for (auto* d : descs) {
                    if (selectedIds.contains(d->id) && d->id != selId) {
                        skipIds.insert(d->id); // descendant of another selected
                    }
                }
            }
        }

        // Collect in visual order
        for (auto* layer : flat) {
            if (selectedIds.contains(layer->id) && !skipIds.contains(layer->id)) {
                layersToMove.append(layer->id);
            }
        }
    }

    if (layersToMove.isEmpty()) {
        layersToMove.append(id); // single drag fallback
    }

    // Edge case: insert at very top → root index 0
    if (dropInsertIndex <= 0 || flat.isEmpty()) {
        bool willMove = false;
        for (int i = 0; i < layersToMove.size(); ++i) {
            const auto* layer = m_layerModel.layerById(layersToMove[i]);
            if (layer && (layer->parent || rootLayerIndex(m_layerModel, layer) != i)) {
                willMove = true;
                break;
            }
        }
        if (willMove) {
            emit aboutToPerformTransformIncompatibleEdit();
        }
        QList<LayerReorderRecord> records;
        {
            LayerModel::ClippingBatch clippingBatch(m_layerModel);
            for (int i = 0; i < layersToMove.size(); ++i) {
                auto* sourceLayer = m_layerModel.layerById(layersToMove[i]);
                if (!sourceLayer) {
                    continue;
                }
                LayerReorderRecord rec;
                rec.layerId = layersToMove[i];
                rec.oldParentId = sourceLayer->parent ? sourceLayer->parent->id : LayerId();
                rec.oldIndex = sourceLayer->parent ? sourceLayer->indexInParent()
                                                   : rootLayerIndex(m_layerModel, sourceLayer);
                rec.newIndex = i;
                if (m_layerModel.moveLayer(layersToMove[i], nullptr, i)) {
                    records.append(rec);
                }
            }
        }
        pushReorderUndo(LayerId(), std::move(records));
        return;
    }

    int clampedIdx = qMin(dropInsertIndex, (int) flat.size());
    LayerData* above = flat[clampedIdx - 1];

    LayerData* targetParent = nullptr;
    // anchorLayer: the existing child of targetParent that will immediately
    // precede the first inserted layer.  nullptr means insert at position 0.
    LayerData* anchorLayer = nullptr;

    // Case 1: above is a group and targetDepth goes deeper → enter group
    if (above->isGroup() && targetDepth > above->depth) {
        targetParent = above;
        anchorLayer = nullptr; // insert as the first child directly under the group row
    } else {
        // Case 2: walk up from 'above' to find the ancestor at targetDepth,
        // then insert after that ancestor in its parent's children.
        LayerData* target = above;
        while (target && target->depth > targetDepth && target->parent) {
            target = target->parent;
        }

        if (target && target->parent) {
            targetParent = target->parent;
            anchorLayer = target; // insert after target inside its parent
        } else {
            targetParent = nullptr;
            anchorLayer = target; // root-level anchor (may be nullptr)
        }
    }

    // Skip if the final anchor is still the source. When outdenting the last
    // child, above can be the source row, but resolving targetDepth walks up to
    // the parent group and produces a valid non-source anchor.
    if (layersToMove.size() == 1 && anchorLayer && anchorLayer->id == id) {
        m_listView->setDropRejected(true);
        return;
    }

    // Helper: return the current index of 'layer' inside targetParent (or root).
    auto currentIndexOf = [&](LayerData* layer) -> int {
        if (!layer)
            return -1;
        if (targetParent) {
            return layer->indexInParent();
        }
        const auto& roots = m_layerModel.rootLayers();
        for (int j = 0; j < (int) roots.size(); ++j) {
            if (roots[j].get() == layer)
                return j;
        }
        return -1;
    };

    // Prevent moving any layer into its own descendant or into itself (would create circular ref +
    // crash).
    for (const LayerId& moveId : layersToMove) {
        auto* layer = m_layerModel.layerById(moveId);
        if (!layer || !targetParent)
            continue;
        if (targetParent == layer) {
            m_listView->setDropRejected(true);
            return; // Invalid: moving group into itself
        }
        if (layer->isAncestorOf(targetParent)) {
            m_listView->setDropRejected(true);
            return; // Invalid: moving into own subgroup
        }
    }

    // Move all layers to the target.
    //
    // For each layer we re-read anchorLayer's live index so we stay correct
    // regardless of move direction (up or down) and after each tree mutation.
    //
    // The clipping batch keeps the whole drop atomic for clip validation: a
    // clipped layer entering a group ahead of its base is a transient state, not
    // a reason to drop its clip flag.
    bool anyMoved = false;
    bool transformCommitted = false;
    QList<LayerReorderRecord> records;
    {
        LayerModel::ClippingBatch clippingBatch(m_layerModel);
        for (int i = 0; i < layersToMove.size(); ++i) {
            const LayerId& moveId = layersToMove[i];

            // Base insert position: right after the anchor (or at 0 if no anchor).
            int liveIndex;
            if (anchorLayer) {
                int anchorIdx = currentIndexOf(anchorLayer);
                liveIndex = (anchorIdx >= 0) ? anchorIdx + 1 : 0;
            } else {
                liveIndex = 0;
            }
            // Each subsequent layer is inserted one slot after the previous one.
            liveIndex += i;

            // When the source layer is in the same parent and sits above the
            // insertion point, extracting it shifts later siblings down by 1.
            auto* sourceLayer = m_layerModel.layerById(moveId);
            if (sourceLayer && sourceLayer->parent == targetParent) {
                int srcIdx = currentIndexOf(sourceLayer);
                if (srcIdx >= 0 && srcIdx < liveIndex) {
                    liveIndex--;
                }
            }

            const int sourceIndex = sourceLayer
                ? (sourceLayer->parent ? sourceLayer->indexInParent()
                                       : rootLayerIndex(m_layerModel, sourceLayer))
                : -1;
            if (!transformCommitted && sourceLayer
                && (sourceLayer->parent != targetParent || sourceIndex != liveIndex)) {
                emit aboutToPerformTransformIncompatibleEdit();
                transformCommitted = true;
            }
            LayerReorderRecord rec;
            rec.layerId = moveId;
            rec.oldParentId
                = (sourceLayer && sourceLayer->parent) ? sourceLayer->parent->id : LayerId();
            rec.oldIndex = sourceIndex;
            rec.newIndex = liveIndex;

            if (m_layerModel.moveLayer(moveId, targetParent, liveIndex)) {
                anyMoved = true;
                records.append(rec);
            }
        }
    }

    pushReorderUndo(targetParent ? targetParent->id : LayerId(), std::move(records));

    if (anyMoved) {
        LayerId parentId = targetParent ? targetParent->id : LayerId();
        int anchorIdx = anchorLayer ? currentIndexOf(anchorLayer) : -1;
        emit layerOrderChanged(id, parentId, anchorIdx + 1);
    }
}

void LayersPanel::onLayerDragCopyDropped(const LayerId& id, int dropInsertIndex, int targetDepth)
{
    m_listView->setDropRejected(false);

    auto flat = m_layerModel.flattenedLayers();
    auto selectedIds = m_layerModel.selectedLayerIds();
    QList<LayerId> layersToCopy;

    if (selectedIds.size() > 1 && selectedIds.contains(id)) {
        QSet<LayerId> skipIds;
        for (const LayerId& selId : selectedIds) {
            auto* layer = m_layerModel.layerById(selId);
            if (!layer) {
                continue;
            }
            QList<LayerData*> descs;
            layer->flatten(descs, false);
            for (auto* d : descs) {
                if (d && selectedIds.contains(d->id) && d->id != selId) {
                    skipIds.insert(d->id);
                }
            }
        }

        for (auto* layer : flat) {
            if (layer && selectedIds.contains(layer->id) && !skipIds.contains(layer->id)) {
                layersToCopy.append(layer->id);
            }
        }
    }

    if (layersToCopy.isEmpty()) {
        layersToCopy.append(id);
    }

    for (auto it = layersToCopy.begin(); it != layersToCopy.end();) {
        auto* layer = m_layerModel.layerById(*it);
        if (!layer || layer->isBackground() || layer->locked) {
            it = layersToCopy.erase(it);
        } else {
            ++it;
        }
    }
    if (layersToCopy.isEmpty()) {
        m_listView->setDropRejected(true);
        return;
    }

    LayerData* targetParent = nullptr;
    LayerData* anchorLayer = nullptr;

    if (dropInsertIndex > 0 && !flat.isEmpty()) {
        const int clampedIdx = qMin(dropInsertIndex, static_cast<int>(flat.size()));
        LayerData* above = flat[clampedIdx - 1];

        if (above->isGroup() && targetDepth > above->depth) {
            targetParent = above;
            anchorLayer = nullptr;
        } else {
            LayerData* target = above;
            while (target && target->depth > targetDepth && target->parent) {
                target = target->parent;
            }

            if (target && target->parent) {
                targetParent = target->parent;
                anchorLayer = target;
            } else {
                targetParent = nullptr;
                anchorLayer = target;
            }
        }
    }

    auto currentIndexOf = [&](LayerData* layer) -> int {
        if (!layer)
            return -1;
        if (targetParent) {
            return layer->indexInParent();
        }
        const auto& roots = m_layerModel.rootLayers();
        for (int j = 0; j < static_cast<int>(roots.size()); ++j) {
            if (roots[j].get() == layer)
                return j;
        }
        return -1;
    };

    QList<std::shared_ptr<LayerData>> undoClones;
    QList<std::pair<LayerId, int>> undoPositions;
    QList<LayerId> copiedIds;
    bool transformCommitted = false;

    for (int i = 0; i < layersToCopy.size(); ++i) {
        LayerData* sourceLayer = m_layerModel.layerById(layersToCopy[i]);
        if (!sourceLayer) {
            continue;
        }

        if (!transformCommitted) {
            emit aboutToPerformTransformIncompatibleEdit();
            transformCommitted = true;
        }
        auto copy = LayerModel::cloneLayerTree(sourceLayer, false);
        if (!copy) {
            continue;
        }
        copy->name = LayerData::copiedName(sourceLayer->name);
        copy->nameIsCustom = true;

        int liveIndex;
        if (dropInsertIndex <= 0 || flat.isEmpty()) {
            targetParent = nullptr;
            liveIndex = i;
        } else if (anchorLayer) {
            const int anchorIdx = currentIndexOf(anchorLayer);
            liveIndex = (anchorIdx >= 0) ? anchorIdx + 1 + i : i;
        } else {
            liveIndex = i;
        }

        if (targetParent) {
            m_layerModel.addLayerTo(copy, targetParent, liveIndex);
        } else {
            m_layerModel.addLayer(copy, liveIndex);
        }

        copiedIds.append(copy->id);

        auto undoClone = LayerModel::cloneLayerTree(copy.get(), true);
        if (undoClone) {
            undoClones.append(undoClone);
            LayerId parentId;
            int index = -1;
            if (copy->parent) {
                parentId = copy->parent->id;
                index = copy->indexInParent();
            } else {
                const auto& roots = m_layerModel.rootLayers();
                for (int j = 0; j < roots.size(); ++j) {
                    if (roots[j].get() == copy.get()) {
                        index = j;
                        break;
                    }
                }
            }
            undoPositions.append({ parentId, index });
        }
    }

    if (copiedIds.isEmpty()) {
        m_listView->setDropRejected(true);
        return;
    }

    m_layerModel.setSelectedLayer(copiedIds.first());
    for (int i = 1; i < copiedIds.size(); ++i) {
        m_layerModel.addToSelection(copiedIds[i]);
    }

    if (m_pushUndoFn && !undoClones.isEmpty()) {
        auto cmd = std::make_unique<aether::LayerAddCommand>(&m_layerModel, std::move(undoClones),
            std::move(undoPositions), m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }

    LayerId parentId = targetParent ? targetParent->id : LayerId();
    int anchorIdx = anchorLayer ? currentIndexOf(anchorLayer) : -1;
    emit layerOrderChanged(copiedIds.first(), parentId, anchorIdx + 1);
}

void LayersPanel::onLayerRenamed(const LayerId& id, const QString& newName)
{
    m_layerModel.setLayerName(id, newName);
    if (auto* layer = m_layerModel.layerById(id)) {
        emit layerNameChanged(id, layer->name);
    }
    if (m_layerModel.selectedLayerId() == id) {
        syncLayerControls();
    }
}

void LayersPanel::onClipSwipeRequested(const LayerId& layerId, bool leftToRight)
{
    auto* layer = m_layerModel.layerById(layerId);
    if (!layer || layer->isBackground())
        return;

    QList<aether::LayerPropertyCommand::Entry> entries;
    entries.append({ layerId, layer->clippedToBelow, QVariant() });

    bool anyChanged = false;
    if (leftToRight) {
        anyChanged = m_layerModel.clipLayerToBelow(layerId);
    } else {
        anyChanged = m_layerModel.unclipLayerFromBelow(layerId);
    }

    if (anyChanged && m_pushUndoFn && !entries.isEmpty()) {
        for (auto& e : entries) {
            if (auto* layer = m_layerModel.layerById(e.layerId)) {
                e.newValue = layer->clippedToBelow;
            }
        }
        QList<aether::LayerPropertyCommand::Entry> changedEntries;
        for (const auto& e : entries) {
            if (e.newValue.isValid() && e.oldValue.toBool() != e.newValue.toBool()) {
                changedEntries.append(e);
            }
        }
        if (!changedEntries.isEmpty()) {
            auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
                aether::LayerPropertyCommand::Property::ClippedToBelow, std::move(changedEntries),
                m_requestRenderFn, m_onContentChangedFn);
            m_pushUndoFn(std::move(cmd));
        }
    }
    if (anyChanged) {
        if (m_requestRenderFn) {
            m_requestRenderFn();
        }
        syncLayerControls();
    }
}

void LayersPanel::onClipSelectionRequested(const LayerId& baseLayerId)
{
    auto* baseLayer = m_layerModel.layerById(baseLayerId);
    if (!baseLayer || baseLayer->isBackground())
        return;

    const auto& selected = m_layerModel.selectedLayerIds();
    QList<aether::LayerPropertyCommand::Entry> entries;
    for (const LayerId& id : selected) {
        if (id == baseLayerId)
            continue;
        auto* layer = m_layerModel.layerById(id);
        if (!layer || layer->isBackground())
            continue;
        entries.append({ id, layer->clippedToBelow, QVariant() });
    }

    const bool anyChanged = m_layerModel.applyClippingToSelection(baseLayerId);
    if (anyChanged && m_pushUndoFn && !entries.isEmpty()) {
        for (auto& e : entries) {
            if (auto* layer = m_layerModel.layerById(e.layerId)) {
                e.newValue = layer->clippedToBelow;
            }
        }
        QList<aether::LayerPropertyCommand::Entry> changedEntries;
        for (const auto& e : entries) {
            if (e.newValue.isValid() && e.oldValue.toBool() != e.newValue.toBool()) {
                changedEntries.append(e);
            }
        }
        if (!changedEntries.isEmpty()) {
            auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
                aether::LayerPropertyCommand::Property::ClippedToBelow, std::move(changedEntries),
                m_requestRenderFn, m_onContentChangedFn);
            m_pushUndoFn(std::move(cmd));
        }
    }
    if (anyChanged) {
        syncLayerControls();
    }
}

// ============================================================================
// Toolbar Slots
// ============================================================================

void LayersPanel::onAddLayer()
{
    emit aboutToPerformTransformIncompatibleEdit();

    LayerData* newLayer = nullptr;
    auto* sel = m_layerModel.selectedLayer();
    if (sel) {
        if (sel->isGroup() && sel->expanded) {
            newLayer = m_layerModel.createLayerIn(sel, QString(), 0);
        } else if (sel->parent) {
            int idx = sel->indexInParent();
            newLayer = m_layerModel.createLayerIn(sel->parent, QString(), idx);
        } else {
            const auto& roots = m_layerModel.rootLayers();
            int rootIdx = 0;
            for (int i = 0; i < (int) roots.size(); ++i) {
                if (roots[i].get() == sel) {
                    rootIdx = i;
                    break;
                }
            }
            newLayer = m_layerModel.createLayer(QString(), rootIdx);
        }
    } else {
        newLayer = m_layerModel.createLayer(QString(), 0);
    }
    if (newLayer) {
        m_layerModel.setSelectedLayer(newLayer->id);
        if (m_pushUndoFn) {
            auto clone = LayerModel::cloneLayerTree(newLayer, true);
            if (clone) {
                LayerId parentId;
                int index = -1;
                if (newLayer->parent) {
                    parentId = newLayer->parent->id;
                    index = newLayer->indexInParent();
                } else {
                    const auto& roots = m_layerModel.rootLayers();
                    for (int i = 0; i < roots.size(); ++i) {
                        if (roots[i].get() == newLayer) {
                            index = i;
                            break;
                        }
                    }
                }
                auto cmd = std::make_unique<aether::LayerAddCommand>(&m_layerModel,
                    QList<std::shared_ptr<LayerData>> { clone },
                    QList<std::pair<LayerId, int>> { { parentId, index } }, m_requestRenderFn,
                    m_onContentChangedFn);
                m_pushUndoFn(std::move(cmd));
            }
        }
    }
    emit addLayerRequested();
}

void LayersPanel::onAddAdjustmentLayer()
{
    emit aboutToPerformTransformIncompatibleEdit();

    LayerData* newLayer = nullptr;
    auto* sel = m_layerModel.selectedLayer();
    if (sel) {
        if (sel->isGroup() && sel->expanded) {
            newLayer = m_layerModel.createAdjustmentLayerIn(sel, QString(), 0);
        } else if (sel->parent) {
            int idx = sel->indexInParent();
            newLayer = m_layerModel.createAdjustmentLayerIn(sel->parent, QString(), idx);
        } else {
            const auto& roots = m_layerModel.rootLayers();
            int rootIdx = 0;
            for (int i = 0; i < (int) roots.size(); ++i) {
                if (roots[i].get() == sel) {
                    rootIdx = i;
                    break;
                }
            }
            newLayer = m_layerModel.createAdjustmentLayer(QString(), rootIdx);
        }
    } else {
        newLayer = m_layerModel.createAdjustmentLayer(QString(), 0);
    }
    if (newLayer) {
        m_layerModel.setSelectedLayer(newLayer->id);
        if (m_pushUndoFn) {
            auto clone = LayerModel::cloneLayerTree(newLayer, true);
            if (clone) {
                LayerId parentId;
                int index = -1;
                if (newLayer->parent) {
                    parentId = newLayer->parent->id;
                    index = newLayer->indexInParent();
                } else {
                    const auto& roots = m_layerModel.rootLayers();
                    for (int i = 0; i < roots.size(); ++i) {
                        if (roots[i].get() == newLayer) {
                            index = i;
                            break;
                        }
                    }
                }
                auto cmd = std::make_unique<aether::LayerAddCommand>(&m_layerModel,
                    QList<std::shared_ptr<LayerData>> { clone },
                    QList<std::pair<LayerId, int>> { { parentId, index } }, m_requestRenderFn,
                    m_onContentChangedFn);
                m_pushUndoFn(std::move(cmd));
            }
        }
    }
    emit addLayerRequested();
}

void LayersPanel::onAddGroup()
{
    const QList<LayerData*> groupableLayers = collectGroupableSelectionRoots(m_layerModel);
    emit aboutToPerformTransformIncompatibleEdit();

    LayerData* newGroup = nullptr;
    LayerId parentId;
    int insertIndex = 0;

    if (!groupableLayers.isEmpty()) {
        LayerData* anchor = groupableLayers.first();
        if (anchor->parent) {
            parentId = anchor->parent->id;
            insertIndex = anchor->indexInParent();
            newGroup = m_layerModel.createGroupIn(anchor->parent, tr("Group"), insertIndex);
        } else {
            insertIndex = qMax(0, rootLayerIndex(m_layerModel, anchor));
            newGroup = m_layerModel.createGroup(tr("Group"), insertIndex);
        }
    } else {
        auto* sel = m_layerModel.selectedLayer();
        if (sel && sel->parent) {
            parentId = sel->parent->id;
            insertIndex = sel->indexInParent();
            newGroup = m_layerModel.createGroupIn(sel->parent, tr("Group"), insertIndex);
        } else if (sel) {
            insertIndex = qMax(0, rootLayerIndex(m_layerModel, sel));
            newGroup = m_layerModel.createGroup(tr("Group"), insertIndex);
        } else {
            newGroup = m_layerModel.createGroup(tr("Group"), 0);
        }
    }

    if (newGroup) {
        auto groupClone = LayerModel::cloneLayerTree(newGroup, true);
        QList<GroupedLayerMoveRecord> moveRecords;

        if (groupableLayers.size() > 1) {
            for (int i = 0; i < groupableLayers.size(); ++i) {
                auto* layer = groupableLayers[i];
                if (!layer) {
                    continue;
                }

                moveRecords.append({ layer->id, layer->parent ? layer->parent->id : LayerId(),
                    layer->parent ? layer->indexInParent() : rootLayerIndex(m_layerModel, layer),
                    i });
            }
        }

        m_layerModel.setSelectedLayer(newGroup->id);

        if (!moveRecords.isEmpty()) {
            const LayerId newGroupId = newGroup->id;
            const QList<GroupedLayerMoveRecord> delayedMoves = moveRecords;
            const int insertDelayMs = anim::duration(kGroupInsertAnimationMs);
            QTimer::singleShot(insertDelayMs, this, [this, newGroupId, delayedMoves]() {
                LayerData* liveGroup = m_layerModel.layerById(newGroupId);
                if (!liveGroup) {
                    return;
                }

                QList<LayerId> moveIds;
                moveIds.reserve(delayedMoves.size());
                for (const auto& move : delayedMoves) {
                    moveIds.append(move.layerId);
                }
                m_layerModel.moveLayers(moveIds, liveGroup, 0);

                m_layerModel.setSelectedLayer(newGroupId);
            });
        }

        if (m_pushUndoFn) {
            if (!moveRecords.isEmpty() && groupClone) {
                auto cmd = std::make_unique<CreateGroupedSelectionCommand>(&m_layerModel,
                    std::move(groupClone), std::pair<LayerId, int> { parentId, insertIndex },
                    std::move(moveRecords), m_requestRenderFn, m_onContentChangedFn);
                m_pushUndoFn(std::move(cmd));
            } else if (groupClone) {
                auto cmd = std::make_unique<aether::LayerAddCommand>(&m_layerModel,
                    QList<std::shared_ptr<LayerData>> { groupClone },
                    QList<std::pair<LayerId, int>> { { parentId, insertIndex } }, m_requestRenderFn,
                    m_onContentChangedFn);
                m_pushUndoFn(std::move(cmd));
            }
        }
    }
    emit addGroupRequested();
}

void LayersPanel::onAddMask()
{
    auto* layer = m_layerModel.selectedLayer();
    if (!layer || !layer->canHostMask()) {
        return;
    }

    emit aboutToPerformTransformIncompatibleEdit();

    if (!layer->hasMask()) {
        // Fresh mask becomes the active paint target so it can be tested right
        // away. This is an undoable structural change. If a selection is active,
        // bake it into the mask (inside = visible, outside = hidden) before the
        // command snapshots the initial tiles.
        const bool prevMaskEditActive = layer->maskEditActive;
        auto* maskGrid = layer->ensureMask();
        layer->maskEditActive = true;

        // Alt+Add Mask creates a "hide-all" mask: an opaque-black background so
        // the layer starts fully hidden and the user reveals only what they paint
        // white. Without Alt the background stays reveal-all (transparent), the
        // historical behavior.
        const bool hideAll = (QGuiApplication::keyboardModifiers() & Qt::AltModifier) != 0;
        if (hideAll && maskGrid) {
            maskGrid->setDefaultFill(0, 0, 0, 255);
        }

        if (m_fillMaskFromSelectionFn) {
            m_fillMaskFromSelectionFn(layer->id);
        }

        if (m_pushUndoFn) {
            auto cmd = std::make_unique<aether::AddMaskCommand>(&m_layerModel, layer->id,
                prevMaskEditActive, m_requestRenderFn, m_onContentChangedFn);
            m_pushUndoFn(std::move(cmd));
        }
    } else {
        // Toggle the paint target between the mask and the layer pixels. Pure UI
        // focus state, not part of the undo history.
        layer->maskEditActive = !layer->maskEditActive;
    }

    // Rebuilds the canvas layer stack (to include the mask) and recomposites.
    m_layerModel.notifyLayerDataChanged(layer->id);
    syncLayerControls();
    scheduleThumbnailRefresh();
    if (m_requestRenderFn) {
        m_requestRenderFn();
    }
}

bool LayersPanel::deleteSelectedLayerMask()
{
    auto* layer = m_layerModel.selectedLayer();
    if (!layer || !layer->hasMask()) {
        return false;
    }

    emit aboutToPerformTransformIncompatibleEdit();

    // Snapshot first — the command captures the mask as it stands now.
    std::unique_ptr<aether::RemoveMaskCommand> cmd;
    if (m_pushUndoFn) {
        cmd = std::make_unique<aether::RemoveMaskCommand>(
            &m_layerModel, layer->id, m_requestRenderFn, m_onContentChangedFn);
    }

    layer->clearMask(); // also forces maskEditActive = false

    if (cmd) {
        m_pushUndoFn(std::move(cmd));
    }

    // Rebuilds the canvas layer stack (mask out) and recomposites.
    m_layerModel.notifyLayerDataChanged(layer->id);
    syncLayerControls();
    scheduleThumbnailRefresh();
    if (m_requestRenderFn) {
        m_requestRenderFn();
    }
    if (m_onContentChangedFn) {
        m_onContentChangedFn();
    }
    return true;
}

bool LayersPanel::selectedLayerMaskIsPaintTarget() const
{
    auto* layer = m_layerModel.selectedLayer();
    return layer && layer->hasMask() && layer->maskEditActive;
}

bool LayersPanel::copySelectedLayerMask()
{
    auto* layer = m_layerModel.selectedLayer();
    if (!layer || !layer->hasMask()) {
        return false;
    }

    auto grid = aether::cloneGridWithSolids(*layer->maskTileGrid());
    if (!grid) {
        return false;
    }

    ruwa::shared::clipboard::EditClipboard::instance().setMask(
        std::move(grid), layer->maskEnabled, layer->maskLinked);
    return true;
}

bool LayersPanel::cutSelectedLayerMask()
{
    return copySelectedLayerMask() && deleteSelectedLayerMask();
}

bool LayersPanel::canPasteMaskToSelectedLayer() const
{
    if (!ruwa::shared::clipboard::EditClipboard::instance().mask()) {
        return false;
    }
    // Same targets "Add mask" accepts.
    auto* layer = m_layerModel.selectedLayer();
    return layer && layer->canHostMask();
}

bool LayersPanel::pasteMaskToSelectedLayer()
{
    const auto* payload = ruwa::shared::clipboard::EditClipboard::instance().mask();
    if (!payload || !canPasteMaskToSelectedLayer()) {
        return false;
    }

    aether::SetMaskCommand::MaskState after;
    after.grid = payload->grid;
    after.enabled = payload->enabled;
    after.linked = payload->linked;
    // The pasted mask becomes the paint target so it can be adjusted right away,
    // matching what adding a mask does.
    after.editActive = true;

    emit aboutToPerformTransformIncompatibleEdit();

    auto* layer = m_layerModel.selectedLayer();
    if (!layer) {
        return false;
    }
    aether::SetMaskCommand::MaskState before = aether::SetMaskCommand::captureState(layer);
    aether::SetMaskCommand::applyState(layer, after);

    if (m_pushUndoFn) {
        m_pushUndoFn(std::make_unique<aether::SetMaskCommand>(&m_layerModel, layer->id,
            std::move(before), std::move(after), m_requestRenderFn, m_onContentChangedFn));
    }

    // Rebuilds the canvas layer stack (mask in) and recomposites.
    m_layerModel.notifyLayerDataChanged(layer->id);
    syncLayerControls();
    scheduleThumbnailRefresh();
    if (m_requestRenderFn) {
        m_requestRenderFn();
    }
    if (m_onContentChangedFn) {
        m_onContentChangedFn();
    }
    return true;
}

void LayersPanel::addLayer()
{
    onAddLayer();
}

void LayersPanel::addGroup()
{
    onAddGroup();
}

void LayersPanel::addAdjustmentLayer()
{
    onAddAdjustmentLayer();
}

void LayersPanel::onDeleteLayer()
{
    deleteSelectedLayers();
}

void LayersPanel::onLayerAlphaLockClicked(const LayerId& id)
{
    auto* layer = m_layerModel.layerById(id);
    if (!layer || !layer->isPixelLayer())
        return;

    const bool oldVal = layer->alphaLock;
    m_layerModel.setLayerAlphaLock(id, false);
    emit layerAlphaLockChanged(id, false);

    if (m_pushUndoFn && oldVal) {
        QList<aether::LayerPropertyCommand::Entry> entries;
        entries.append({ id, oldVal, false });
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::AlphaLock, std::move(entries),
            m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
}

void LayersPanel::onLayerLockClicked(const LayerId& id)
{
    auto* layer = m_layerModel.layerById(id);
    if (!layer)
        return;

    const bool oldVal = layer->locked;
    if (!oldVal) {
        return;
    }
    emit aboutToPerformTransformIncompatibleEdit();
    m_layerModel.setLayerLocked(id, false);
    emit layerLockChanged(id, false);

    if (m_pushUndoFn) {
        QList<aether::LayerPropertyCommand::Entry> entries;
        entries.append({ id, oldVal, false });
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::Locked, std::move(entries), m_requestRenderFn,
            m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
}

void LayersPanel::onAlphaLockToggled()
{
    if (m_syncingLayerControls || !m_btnAlphaLock)
        return;

    const bool alphaLock = m_btnAlphaLock->isChecked();
    const auto& selected = m_layerModel.selectedLayerIds();
    QList<aether::LayerPropertyCommand::Entry> entries;

    for (const LayerId& id : selected) {
        auto* layer = m_layerModel.layerById(id);
        if (layer && layer->isRaster() && layer->alphaLock != alphaLock) {
            entries.append({ id, layer->alphaLock, alphaLock });
            m_layerModel.setLayerAlphaLock(id, alphaLock);
            emit layerAlphaLockChanged(id, alphaLock);
        }
    }
    if (m_pushUndoFn && !entries.isEmpty()) {
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::AlphaLock, std::move(entries),
            m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
}

void LayersPanel::onLockToggled()
{
    if (m_syncingLayerControls || !m_btnLock)
        return;

    const bool locked = m_btnLock->isChecked();
    const auto& selected = m_layerModel.selectedLayerIds();
    QList<aether::LayerPropertyCommand::Entry> entries;

    for (const LayerId& id : selected) {
        auto* layer = m_layerModel.layerById(id);
        if (layer && !layer->isBackground() && layer->locked != locked) {
            entries.append({ id, layer->locked, locked });
        }
    }
    if (entries.isEmpty()) {
        return;
    }

    emit aboutToPerformTransformIncompatibleEdit();
    for (const auto& entry : entries) {
        m_layerModel.setLayerLocked(entry.layerId, locked);
        emit layerLockChanged(entry.layerId, locked);
    }
    if (m_pushUndoFn && !entries.isEmpty()) {
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::Locked, std::move(entries), m_requestRenderFn,
            m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
}

void LayersPanel::onBlendModeChanged(int index)
{
    if (m_syncingLayerControls)
        return;
    Q_UNUSED(index);

    auto* layer = m_layerModel.selectedLayer();
    if (!layer)
        return;

    const QVariant selectedData = m_blendModeCombo ? m_blendModeCombo->currentData() : QVariant();
    if (!selectedData.isValid())
        return;

    auto mode = static_cast<BlendMode>(selectedData.toInt());
    m_blendModeSelectionCommitted = true;
    // Use the value from popup opening; the layer may currently hold a hover preview.
    const int oldMode = static_cast<int>(m_blendModeBeforePreview);
    if (oldMode == static_cast<int>(mode)) {
        if (layer->blendMode != mode) {
            m_layerModel.setLayerBlendMode(layer->id, mode);
            emit layerBlendModeChanged(layer->id, mode);
        }
        m_isBlendModePreviewActive = false;
        syncLayerControls();
        return;
    }

    m_layerModel.setLayerBlendMode(layer->id, mode);
    emit layerBlendModeChanged(layer->id, mode);
    m_blendModeBeforePreview = mode;
    m_isBlendModePreviewActive = false;

    if (m_pushUndoFn) {
        QList<aether::LayerPropertyCommand::Entry> entries;
        entries.append({ layer->id, oldMode, static_cast<int>(mode) });
        auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
            aether::LayerPropertyCommand::Property::BlendMode, std::move(entries),
            m_requestRenderFn, m_onContentChangedFn);
        m_pushUndoFn(std::move(cmd));
    }
    syncLayerControls();
}

void LayersPanel::onBlendModeHovered(int index)
{
    if (m_syncingLayerControls || !m_blendModeCombo) {
        return;
    }

    auto* layer = m_layerModel.selectedLayer();
    if (!layer) {
        return;
    }

    if (index < 0) {
        return;
    }

    const QVariant hoveredData = m_blendModeCombo->itemData(index);
    if (!hoveredData.isValid()) {
        return;
    }

    const auto hoveredMode = static_cast<BlendMode>(hoveredData.toInt());
    if (layer->blendMode == hoveredMode) {
        return;
    }

    m_layerModel.setLayerBlendMode(layer->id, hoveredMode);
    emit layerBlendModeChanged(layer->id, hoveredMode);
    m_isBlendModePreviewActive = (hoveredMode != m_blendModeBeforePreview);
}

void LayersPanel::onBlendModePopupShown()
{
    if (auto* layer = m_layerModel.selectedLayer()) {
        m_blendModeBeforePreview = layer->blendMode;
    } else {
        m_blendModeBeforePreview = BlendMode::Normal;
    }
    m_isBlendModePreviewActive = false;
    m_blendModeSelectionCommitted = false;
}

void LayersPanel::onBlendModePopupHidden()
{
    if (m_blendModeSelectionCommitted) {
        m_isBlendModePreviewActive = false;
        m_blendModeSelectionCommitted = false;
        return;
    }

    auto* layer = m_layerModel.selectedLayer();
    if (!layer || !m_blendModeCombo) {
        m_isBlendModePreviewActive = false;
        m_blendModeSelectionCommitted = false;
        return;
    }

    if (layer->blendMode != m_blendModeBeforePreview) {
        m_layerModel.setLayerBlendMode(layer->id, m_blendModeBeforePreview);
        emit layerBlendModeChanged(layer->id, m_blendModeBeforePreview);
    }
    m_isBlendModePreviewActive = false;
    m_blendModeSelectionCommitted = false;
    syncLayerControls();
}

void LayersPanel::onOpacityChanged(qreal opacity)
{
    if (m_syncingLayerControls)
        return;

    auto* layer = m_layerModel.selectedLayer();
    if (!layer)
        return;

    m_layerModel.setLayerOpacity(layer->id, opacity);
    emit layerOpacityChanged(layer->id, opacity);
    syncLayerControls();
}

void LayersPanel::onOpacityDragStarted(qreal opacity)
{
    Q_UNUSED(opacity);
    auto* layer = m_layerModel.selectedLayer();
    if (layer) {
        m_opacityBeforeDrag = layer->opacity;
        emit layerOpacityEditStarted(layer->id);
    }
}

void LayersPanel::onOpacityCommitted(qreal opacity)
{
    if (m_syncingLayerControls)
        return;

    auto* layer = m_layerModel.selectedLayer();
    if (!layer)
        return;

    const bool changed = !qFuzzyCompare(m_opacityBeforeDrag, opacity);
    emit layerOpacityEditFinished(layer->id, changed);

    if (!m_pushUndoFn)
        return;
    if (!changed)
        return;

    QList<aether::LayerPropertyCommand::Entry> entries;
    entries.append({ layer->id, m_opacityBeforeDrag, opacity });
    auto cmd = std::make_unique<aether::LayerPropertyCommand>(&m_layerModel,
        aether::LayerPropertyCommand::Property::Opacity, std::move(entries), m_requestRenderFn,
        m_onContentChangedFn);
    m_pushUndoFn(std::move(cmd));
}

void LayersPanel::onModelLayerDataChanged(const LayerId& id)
{
    if (id == m_layerModel.selectedLayerId()) {
        syncLayerControls();
    }
}

void LayersPanel::syncLayerControls()
{
    if (m_syncingLayerControls)
        return;
    m_syncingLayerControls = true;

    auto* layer = m_layerModel.selectedLayer();
    const bool hasLayer = (layer != nullptr);
    const bool canModifyLayerEntry = hasLayer && !layer->isBackground();

    if (m_blendModeCombo) {
        m_blendModeCombo->setEnabled(hasLayer);
        QSignalBlocker blocker(m_blendModeCombo);
        if (hasLayer) {
            const int idx = m_blendModeCombo->findIndexByData(static_cast<int>(layer->blendMode));
            m_blendModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        } else {
            const int idx = m_blendModeCombo->findIndexByData(static_cast<int>(BlendMode::Normal));
            m_blendModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
    }

    if (m_opacitySlider) {
        m_opacitySlider->setEnabled(hasLayer);
        if (hasLayer) {
            m_opacitySlider->setOpacity(layer->opacity);
        } else {
            m_opacitySlider->setOpacity(1.0);
        }
    }

    if (m_btnAlphaLock) {
        const bool canAlphaLock = hasLayer && layer->isRaster();
        m_btnAlphaLock->setEnabled(canAlphaLock);
        if (hasLayer && layer->isPixelLayer()) {
            QSignalBlocker blocker(m_btnAlphaLock);
            m_btnAlphaLock->setChecked(layer->alphaLock);
            m_btnAlphaLock->setActive(layer->alphaLock);
        } else {
            QSignalBlocker blocker(m_btnAlphaLock);
            m_btnAlphaLock->setChecked(false);
            m_btnAlphaLock->setActive(false);
        }
    }

    if (m_btnLock) {
        m_btnLock->setEnabled(canModifyLayerEntry);
        if (hasLayer) {
            QSignalBlocker blocker(m_btnLock);
            m_btnLock->setChecked(layer->locked);
            m_btnLock->setActive(layer->locked);
        } else {
            QSignalBlocker blocker(m_btnLock);
            m_btnLock->setChecked(false);
            m_btnLock->setActive(false);
        }
    }

    if (m_btnDuplicate) {
        m_btnDuplicate->setEnabled(canModifyLayerEntry);
    }

    if (m_btnMask) {
        // Raster and smart layers can own a mask; see LayerData::canHostMask.
        const bool canMask = canModifyLayerEntry && layer && layer->canHostMask();
        m_btnMask->setEnabled(canMask);
    }

    if (m_btnDelete) {
        m_btnDelete->setEnabled(canModifyLayerEntry);
    }

    if (m_btnMergeDown) {
        m_btnMergeDown->setEnabled(hasMergeIntent());
    }

    m_syncingLayerControls = false;

    // The paint target is the selected layer's mask only while it both exists and
    // is the focused edit target. Notify listeners (the color panel grayscales its
    // display) on transitions only. This runs from every selection / layer-data
    // sync, so it also fires when a row's mask thumbnail toggles the target or when
    // a different layer becomes active.
    const bool maskEditTarget = hasLayer && layer->hasMask() && layer->maskEditActive;
    if (maskEditTarget != m_lastMaskEditTarget) {
        m_lastMaskEditTarget = maskEditTarget;
        emit maskEditTargetChanged(maskEditTarget);
    }
}

// ============================================================================
// Drop Resolution
// ============================================================================

} // namespace ruwa::ui::workspace
