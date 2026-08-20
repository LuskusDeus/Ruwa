// SPDX-License-Identifier: MPL-2.0

// LayerData.h
#ifndef RUWA_CORE_LAYERS_LAYERDATA_H
#define RUWA_CORE_LAYERS_LAYERDATA_H

#include <QString>
#include <QRegularExpression>
#include <QUuid>
#include <QPixmap>
#include <QColor>
#include <QList>
#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <memory>
#include <optional>

#include "shared/tiles/TileGrid.h"
#include "features/canvas/rendering/RetainedRenderPayload.h"
#include "features/effects/LayerEffectTypes.h"
#include "features/layers/model/SmartContent.h"
#include "features/transform/TransformState.h"

namespace ruwa::core::layers {

/**
 * @brief Unique identifier for layers
 */
using LayerId = QUuid;

/**
 * @brief Layer blend modes
 */
enum class BlendMode {
    Normal,
    Multiply,
    Screen,
    Overlay,
    SoftLight,
    HardLight,
    ColorDodge,
    ColorBurn,
    Darken,
    Lighten,
    Difference,
    Exclusion,
    Dissolve,
    LinearBurn,
    DarkerColor,
    LinearDodge,
    LighterColor,
    VividLight,
    LinearLight,
    PinLight,
    HardMix,
    Subtract,
    Divide,
    Hue,
    Saturation,
    Color,
    Luminosity
};

/**
 * @brief How a group's children interact with layers outside the group.
 *
 * Isolated matches Photoshop's Normal group semantics: children are first
 * composited against transparency and the finished group is then blended into
 * its parent. PassThrough lets children blend directly with the parent's
 * backdrop.
 */
enum class GroupCompositingMode { Isolated = 0, PassThrough = 1 };

/**
 * @brief Layer type discriminator
 */
enum class LayerType {
    Raster = 0, // Regular pixel layer
    Group = 1, // Group/folder containing other layers
    Adjustment = 2, // Adjustment layer
    Vector = 3, // Vector layer
    Mask = 4, // Mask layer
    Background = 5, // Special bottom layer
    Smart = 6, // Smart pixel layer (isolated raster payload)
    Board = 7, // Visual-only imported layer, rendered outside document bounds
    Text = 8 // Editable text model layer
};

enum class TextAlignment { Left = 0, Center = 1, Right = 2, Justify = 3 };

/// Photoshop's All Caps / Small Caps toggles, which are exclusive: turning one
/// on turns the other off, so one enum expresses the pair better than two
/// booleans that could disagree.
enum class TextCaps { None = 0, AllCaps = 1, SmallCaps = 2 };

struct TextStyleRun {
    int start = 0;
    int length = 0;
    QString fontFamily;
    qreal fontSize = 48.0;
    QColor color = QColor(0, 0, 0);
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    /// Tracking in Photoshop units: thousandths of an em, so the same number
    /// means the same visual spacing at any font size.
    qreal tracking = 0.0;
    TextCaps caps = TextCaps::None;

    bool operator==(const TextStyleRun& other) const
    {
        return start == other.start && length == other.length && fontFamily == other.fontFamily
            && qFuzzyCompare(fontSize, other.fontSize) && color.rgba() == other.color.rgba()
            && bold == other.bold && italic == other.italic && underline == other.underline
            && strikethrough == other.strikethrough && qFuzzyCompare(tracking, other.tracking)
            && caps == other.caps;
    }

    bool operator!=(const TextStyleRun& other) const { return !(*this == other); }
};

enum class TextStyleEffect { Bold, Italic, Underline, Strikethrough };

struct TextCharStyle {
    QString fontFamily;
    qreal fontSize = 48.0;
    QColor color = QColor(0, 0, 0);
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    qreal tracking = 0.0;
    TextCaps caps = TextCaps::None;

    bool operator==(const TextCharStyle& other) const
    {
        return fontFamily == other.fontFamily && qFuzzyCompare(fontSize, other.fontSize)
            && color.rgba() == other.color.rgba() && bold == other.bold && italic == other.italic
            && underline == other.underline && strikethrough == other.strikethrough
            && qFuzzyCompare(tracking, other.tracking) && caps == other.caps;
    }

    bool operator!=(const TextCharStyle& other) const { return !(*this == other); }
};

struct TextLayerData {
    QString text;
    QString fontFamily = QStringLiteral("Arial");
    qreal fontSize = 48.0;
    QColor color = QColor(0, 0, 0);
    TextAlignment alignment = TextAlignment::Left;
    /// Leading as a multiple of the font's natural line height. Photoshop's
    /// "Auto" leading is this same idea expressed as a percentage, which is how
    /// the Character group presents it.
    qreal lineHeight = 1.2;
    bool strikethrough = false;
    qreal tracking = 0.0;
    TextCaps caps = TextCaps::None;
    /// Paragraph spacing in document pixels at the layer's unscaled size. Every
    /// text line is its own paragraph here — the layout does not wrap, so a
    /// newline is the only paragraph break there is.
    qreal spaceBefore = 0.0;
    qreal spaceAfter = 0.0;
    QList<TextStyleRun> styleRuns;
    aether::TransformState transform;
};

/**
 * @brief Everything about a text layer other than its characters and their
 * per-character overrides: the layer-wide defaults plus the paragraph
 * settings.
 *
 * Snapshotted as one value because that is what an undo step of a Character or
 * Paragraph edit has to restore — the run list alone cannot describe a change
 * to the defaults a newly typed character would inherit.
 */
struct TextLayerTypography {
    QString fontFamily = QStringLiteral("Arial");
    qreal fontSize = 48.0;
    QColor color = QColor(0, 0, 0);
    TextAlignment alignment = TextAlignment::Left;
    qreal lineHeight = 1.2;
    bool strikethrough = false;
    qreal tracking = 0.0;
    TextCaps caps = TextCaps::None;
    qreal spaceBefore = 0.0;
    qreal spaceAfter = 0.0;

    bool operator==(const TextLayerTypography& other) const
    {
        return fontFamily == other.fontFamily && qFuzzyCompare(fontSize, other.fontSize)
            && color.rgba() == other.color.rgba() && alignment == other.alignment
            && qFuzzyCompare(lineHeight, other.lineHeight)
            && strikethrough == other.strikethrough && qFuzzyCompare(tracking, other.tracking)
            && caps == other.caps && qFuzzyCompare(spaceBefore, other.spaceBefore)
            && qFuzzyCompare(spaceAfter, other.spaceAfter);
    }

    bool operator!=(const TextLayerTypography& other) const { return !(*this == other); }
};

inline TextLayerTypography captureTextTypography(const TextLayerData& textData)
{
    return { textData.fontFamily, textData.fontSize, textData.color, textData.alignment,
        textData.lineHeight, textData.strikethrough, textData.tracking, textData.caps,
        textData.spaceBefore, textData.spaceAfter };
}

inline void applyTextTypography(TextLayerData& textData, const TextLayerTypography& typography)
{
    textData.fontFamily = typography.fontFamily;
    textData.fontSize = typography.fontSize;
    textData.color = typography.color;
    textData.alignment = typography.alignment;
    textData.lineHeight = typography.lineHeight;
    textData.strikethrough = typography.strikethrough;
    textData.tracking = typography.tracking;
    textData.caps = typography.caps;
    textData.spaceBefore = typography.spaceBefore;
    textData.spaceAfter = typography.spaceAfter;
}

inline TextCharStyle defaultTextCharStyle(const TextLayerData& textData)
{
    TextCharStyle style;
    style.fontFamily = textData.fontFamily;
    style.fontSize = qMax<qreal>(1.0, textData.fontSize);
    style.color = textData.color;
    style.strikethrough = textData.strikethrough;
    style.tracking = textData.tracking;
    style.caps = textData.caps;
    return style;
}

inline TextCharStyle textCharStyleAt(const TextLayerData& textData, int index)
{
    TextCharStyle style = defaultTextCharStyle(textData);
    for (const TextStyleRun& run : textData.styleRuns) {
        const int start = std::max(0, run.start);
        const int end = start + std::max(0, run.length);
        if (index >= start && index < end) {
            if (!run.fontFamily.isEmpty()) {
                style.fontFamily = run.fontFamily;
            }
            style.fontSize = qMax<qreal>(1.0, run.fontSize);
            style.color = run.color;
            style.bold = run.bold;
            style.italic = run.italic;
            style.underline = run.underline;
            style.strikethrough = run.strikethrough;
            style.tracking = run.tracking;
            style.caps = run.caps;
        }
    }
    return style;
}

inline bool textStyleEffectValue(const TextCharStyle& style, TextStyleEffect effect)
{
    switch (effect) {
    case TextStyleEffect::Bold:
        return style.bold;
    case TextStyleEffect::Italic:
        return style.italic;
    case TextStyleEffect::Underline:
        return style.underline;
    case TextStyleEffect::Strikethrough:
        return style.strikethrough;
    }
    return false;
}

inline void setTextStyleEffectValue(TextCharStyle& style, TextStyleEffect effect, bool value)
{
    switch (effect) {
    case TextStyleEffect::Bold:
        style.bold = value;
        return;
    case TextStyleEffect::Italic:
        style.italic = value;
        return;
    case TextStyleEffect::Underline:
        style.underline = value;
        return;
    case TextStyleEffect::Strikethrough:
        style.strikethrough = value;
        return;
    }
}

inline QVector<TextCharStyle> textCharacterStyles(const TextLayerData& textData)
{
    QVector<TextCharStyle> styles;
    styles.reserve(textData.text.size());
    for (int i = 0; i < textData.text.size(); ++i) {
        styles.append(textCharStyleAt(textData, i));
    }
    return styles;
}

inline void rebuildTextStyleRuns(TextLayerData& textData, const QVector<TextCharStyle>& styles)
{
    textData.styleRuns.clear();
    const TextCharStyle base = defaultTextCharStyle(textData);
    const int count = std::min(textData.text.size(), styles.size());
    int i = 0;
    while (i < count) {
        const TextCharStyle style = styles[i];
        int end = i + 1;
        while (end < count && styles[end] == style) {
            ++end;
        }
        if (style != base) {
            textData.styleRuns.append({ i, end - i, style.fontFamily, style.fontSize, style.color,
                style.bold, style.italic, style.underline, style.strikethrough, style.tracking,
                style.caps });
        }
        i = end;
    }
}

inline void replaceTextPreservingCharacterStyles(TextLayerData& textData, const QString& newText)
{
    const QString oldText = textData.text;
    if (oldText == newText) {
        return;
    }

    QVector<TextCharStyle> oldStyles = textCharacterStyles(textData);
    int prefix = 0;
    const int minLength = std::min(oldText.size(), newText.size());
    while (prefix < minLength && oldText.at(prefix) == newText.at(prefix)) {
        ++prefix;
    }

    int suffix = 0;
    while (suffix < oldText.size() - prefix && suffix < newText.size() - prefix
        && oldText.at(oldText.size() - 1 - suffix) == newText.at(newText.size() - 1 - suffix)) {
        ++suffix;
    }

    const int oldChangeEnd = oldText.size() - suffix;
    const int newInsertLength = newText.size() - prefix - suffix;
    const TextCharStyle insertedStyle = oldStyles.isEmpty()
        ? defaultTextCharStyle(textData)
        : textCharStyleAt(
              textData, qBound(0, prefix > 0 ? prefix - 1 : prefix, oldText.size() - 1));

    QVector<TextCharStyle> newStyles;
    newStyles.reserve(newText.size());
    for (int i = 0; i < prefix && i < oldStyles.size(); ++i) {
        newStyles.append(oldStyles[i]);
    }
    for (int i = 0; i < newInsertLength; ++i) {
        newStyles.append(insertedStyle);
    }
    for (int i = oldChangeEnd; i < oldStyles.size(); ++i) {
        newStyles.append(oldStyles[i]);
    }

    textData.text = newText;
    rebuildTextStyleRuns(textData, newStyles);
}

inline bool selectedTextEffectEnabled(
    const TextLayerData& textData, int selectionStart, int selectionEnd, TextStyleEffect effect)
{
    const int from = qBound(0, std::min(selectionStart, selectionEnd), textData.text.size());
    const int to = qBound(0, std::max(selectionStart, selectionEnd), textData.text.size());
    if (from >= to) {
        return false;
    }
    for (int i = from; i < to; ++i) {
        if (!textStyleEffectValue(textCharStyleAt(textData, i), effect)) {
            return false;
        }
    }
    return true;
}

inline void applyTextEffectToRange(TextLayerData& textData, int selectionStart, int selectionEnd,
    TextStyleEffect effect, bool value)
{
    const int from = qBound(0, std::min(selectionStart, selectionEnd), textData.text.size());
    const int to = qBound(0, std::max(selectionStart, selectionEnd), textData.text.size());
    if (from >= to) {
        return;
    }

    QVector<TextCharStyle> styles = textCharacterStyles(textData);
    for (int i = from; i < to && i < styles.size(); ++i) {
        setTextStyleEffectValue(styles[i], effect, value);
    }
    rebuildTextStyleRuns(textData, styles);
}

inline void applyTextFontFamilyToRange(
    TextLayerData& textData, int selectionStart, int selectionEnd, const QString& fontFamily)
{
    const int from = qBound(0, std::min(selectionStart, selectionEnd), textData.text.size());
    const int to = qBound(0, std::max(selectionStart, selectionEnd), textData.text.size());
    if (from >= to || fontFamily.isEmpty()) {
        return;
    }

    QVector<TextCharStyle> styles = textCharacterStyles(textData);
    for (int i = from; i < to && i < styles.size(); ++i) {
        styles[i].fontFamily = fontFamily;
    }
    rebuildTextStyleRuns(textData, styles);
}

inline void applyTextColorToRange(
    TextLayerData& textData, int selectionStart, int selectionEnd, const QColor& color)
{
    const int from = qBound(0, std::min(selectionStart, selectionEnd), textData.text.size());
    const int to = qBound(0, std::max(selectionStart, selectionEnd), textData.text.size());
    if (from >= to || !color.isValid()) {
        return;
    }

    QVector<TextCharStyle> styles = textCharacterStyles(textData);
    for (int i = from; i < to && i < styles.size(); ++i) {
        styles[i].color = color;
    }
    rebuildTextStyleRuns(textData, styles);
}

inline void applyTextFontSizeToRange(
    TextLayerData& textData, int selectionStart, int selectionEnd, qreal fontSize)
{
    const int from = qBound(0, std::min(selectionStart, selectionEnd), textData.text.size());
    const int to = qBound(0, std::max(selectionStart, selectionEnd), textData.text.size());
    if (from >= to) {
        return;
    }

    QVector<TextCharStyle> styles = textCharacterStyles(textData);
    for (int i = from; i < to && i < styles.size(); ++i) {
        styles[i].fontSize = qMax<qreal>(1.0, fontSize);
    }
    rebuildTextStyleRuns(textData, styles);
}

inline void applyTextTrackingToRange(
    TextLayerData& textData, int selectionStart, int selectionEnd, qreal tracking)
{
    const int from = qBound(0, std::min(selectionStart, selectionEnd), textData.text.size());
    const int to = qBound(0, std::max(selectionStart, selectionEnd), textData.text.size());
    if (from >= to) {
        return;
    }

    QVector<TextCharStyle> styles = textCharacterStyles(textData);
    for (int i = from; i < to && i < styles.size(); ++i) {
        styles[i].tracking = tracking;
    }
    rebuildTextStyleRuns(textData, styles);
}

inline void applyTextCapsToRange(
    TextLayerData& textData, int selectionStart, int selectionEnd, TextCaps caps)
{
    const int from = qBound(0, std::min(selectionStart, selectionEnd), textData.text.size());
    const int to = qBound(0, std::max(selectionStart, selectionEnd), textData.text.size());
    if (from >= to) {
        return;
    }

    QVector<TextCharStyle> styles = textCharacterStyles(textData);
    for (int i = from; i < to && i < styles.size(); ++i) {
        styles[i].caps = caps;
    }
    rebuildTextStyleRuns(textData, styles);
}

/**
 * @brief The value @p reader returns for every character in the range, or
 * nullopt when they disagree.
 *
 * The Character group needs this for every numeric field it shows: a mixed
 * selection must read as blank rather than as whichever value happened to be
 * first, which is the same rule Photoshop's character panel follows.
 */
template <typename Reader>
inline auto uniformTextStyleValue(
    const TextLayerData& textData, int selectionStart, int selectionEnd, Reader reader)
    -> std::optional<decltype(reader(TextCharStyle {}))>
{
    using Value = decltype(reader(TextCharStyle {}));
    const int size = static_cast<int>(textData.text.size());
    int from = qBound(0, std::min(selectionStart, selectionEnd), size);
    int to = qBound(0, std::max(selectionStart, selectionEnd), size);
    if (from >= to) {
        // No selection: the layer's own defaults are what a new character would
        // get, so that is the honest answer rather than "mixed".
        if (size == 0) {
            return reader(defaultTextCharStyle(textData));
        }
        from = 0;
        to = size;
    }

    const Value first = reader(textCharStyleAt(textData, from));
    for (int i = from + 1; i < to; ++i) {
        if (!(reader(textCharStyleAt(textData, i)) == first)) {
            return std::nullopt;
        }
    }
    return first;
}

/**
 * @brief Data structure for a single layer
 *
 * Any layer can have children (not just groups).
 * Provides rich navigation API for UI implementation.
 */
struct LayerData : public std::enable_shared_from_this<LayerData> {
    // === Identity ===
    LayerId id = QUuid::createUuid();
    QString name = "Layer";
    LayerType type = LayerType::Raster;
    static constexpr int kMaxNameLength = 256;

    // === Hierarchy ===
    LayerData* parent = nullptr;
    QList<std::shared_ptr<LayerData>> children;
    int depth = 0; // Nesting level (0 = root level)

    // === Visual Properties ===
    static constexpr quint8 kBaseDisplayColorIndex = 0;
    static constexpr quint8 kMaxDisplayColorIndex = 8;

    bool visible = true;
    bool locked = false;
    bool alphaLock = false; // Lock transparency (paint only on existing pixels)
    qreal opacity = 1.0; // 0.0 - 1.0
    BlendMode blendMode = BlendMode::Normal;
    GroupCompositingMode groupCompositingMode = GroupCompositingMode::Isolated;
    quint8 displayColorIndex = kBaseDisplayColorIndex; // 0 = base, 1..8 = palette slots
    QColor backgroundColor = QColor(255, 255, 255);
    bool backgroundTransparent = false;
    bool clippedToBelow = false;
    bool nameIsCustom = false;
    QList<ruwa::core::effects::LayerEffectState> effects;
    quint64 effectChainRevision = 0;
    /// Transient UI/rendering state: the effect whose parameters are being
    /// changed continuously. It is deliberately not serialized and does not
    /// participate in effect-chain revisions.
    QUuid liveEditedEffectId;
    QString liveEditedEffectParamKey;
    quint64 liveEffectEditGeneration = 0;

    // === Thumbnail ===
    QPixmap thumbnail;
    bool thumbnailDirty = true;

    // === Layer mask ===
    // Raster alpha mask attached to this layer. Coverage is stored in the
    // alpha channel of an RGBA8 tile grid (255 = fully reveals the layer,
    // 0 = fully hides it). Tiles absent from the grid are treated as
    // "reveal all" by the compositor, so a freshly added mask leaves the
    // layer fully visible until the user paints into it.
    // Null when the layer has no mask.
    std::unique_ptr<aether::TileGrid> maskGrid;
    bool maskEnabled = true; // Mask participates in compositing
    bool maskLinked = true; // Mask follows layer transform/move
    bool maskEditActive = false; // UI focus: brush strokes target the mask, not pixels
    QPixmap maskThumbnail;
    bool maskThumbnailDirty = true;

    // === Pixel content ===
    // Holds premultiplied RGBA8 pixel data in a sparse tile grid.
    // Raster layers draw directly into tileGrid.
    // Smart/Board layers keep isolated pixels in smartContent->grid.
    // Non-pixel layers keep both pointers null.
    std::unique_ptr<aether::TileGrid> tileGrid;
    // The smart object's payload — its pixels plus their identity and source.
    // Held by shared_ptr because several layers can show ONE content (instances);
    // smartTransform deliberately stays per-layer so instances can be placed
    // differently. Access the pixels through pixelGrid() / smartGrid().
    std::shared_ptr<SmartContent> smartContent;
    // How many layers OF THIS MODEL currently show `smartContent`: 0 without
    // content, 1 for a lone smart object, >1 for instances. Runtime bookkeeping
    // maintained by LayerModel::refreshSmartInstanceCounts(), read by the layer
    // row's badge. Deliberately not smartContent.use_count(): undo snapshots
    // hold the same shared content, so the raw count would call a lone layer
    // "shared" merely because it appears somewhere in the undo stack.
    int runtimeSmartInstanceCount = 0;
    aether::TransformState smartTransform;
    std::unique_ptr<TextLayerData> textData;
    aether::LayerVisualBackend runtimeVisualBackend = aether::LayerVisualBackend::RasterTiles;
    std::shared_ptr<aether::RetainedRenderPayload> runtimeRetainedPayload;
    QString runtimeRetainedPayloadKey;

    // === State ===
    bool expanded = true; // Are children visible in UI?

    // ========================================================================
    // Factory Methods
    // ========================================================================

    static QString clampedName(const QString& name) { return name.left(kMaxNameLength); }

    // Copying a copy bumps the counter instead of stacking suffixes:
    // "Layer" -> "Layer (copy)" -> "Layer (copy 2)" -> "Layer (copy 3)".
    static QString copiedName(const QString& sourceName)
    {
        static const QRegularExpression copySuffix(
            QStringLiteral(R"(\s*\(copy(?:\s+(\d+))?\)$)"),
            QRegularExpression::CaseInsensitiveOption);

        QString base = sourceName;
        int nextIndex = 1;
        const QRegularExpressionMatch match = copySuffix.match(sourceName);
        if (match.hasMatch()) {
            base = sourceName.left(match.capturedStart());
            const int current = match.captured(1).isEmpty() ? 1 : match.captured(1).toInt();
            // A truncated name may have lost its digits; never go backwards.
            nextIndex = qMax(2, current + 1);
        }

        const QString suffix = (nextIndex <= 1)
            ? QStringLiteral(" (copy)")
            : QStringLiteral(" (copy %1)").arg(nextIndex);
        const int baseLimit = qMax(0, kMaxNameLength - suffix.size());
        return clampedName(base.left(baseLimit) + suffix);
    }

    static std::shared_ptr<LayerData> createLayer(const QString& name = "Layer")
    {
        auto layer = std::make_shared<LayerData>();
        layer->name = clampedName(name);
        layer->type = LayerType::Raster;
        layer->tileGrid = std::make_unique<aether::TileGrid>();
        return layer;
    }

    static std::shared_ptr<LayerData> createSmart(const QString& name = "Smart")
    {
        auto layer = std::make_shared<LayerData>();
        layer->name = clampedName(name);
        layer->type = LayerType::Smart;
        layer->smartContent = std::make_shared<SmartContent>();
        return layer;
    }

    static std::shared_ptr<LayerData> createBoard(const QString& name = "Board")
    {
        auto layer = std::make_shared<LayerData>();
        layer->name = clampedName(name);
        layer->type = LayerType::Board;
        layer->smartContent = std::make_shared<SmartContent>();
        return layer;
    }

    static QString standardTextLayerName(const QString& text)
    {
        QString title = text.simplified();
        if (title.isEmpty()) {
            return QStringLiteral("Text");
        }
        if (title.size() > 6) {
            title = title.left(6) + QStringLiteral("...");
        }
        return title;
    }

    static std::shared_ptr<LayerData> createText(
        const QString& name = "Text", const QString& text = QString())
    {
        auto layer = std::make_shared<LayerData>();
        layer->name = clampedName(name);
        layer->nameIsCustom
            = (layer->name != QStringLiteral("Text") && layer->name != standardTextLayerName(text));
        layer->type = LayerType::Text;
        layer->textData = std::make_unique<TextLayerData>();
        layer->textData->text = text;
        return layer;
    }

    static std::shared_ptr<LayerData> createGroup(const QString& name = "Group")
    {
        auto group = std::make_shared<LayerData>();
        group->name = clampedName(name);
        group->type = LayerType::Group;
        // Groups have no tile grid
        return group;
    }

    static std::shared_ptr<LayerData> create(LayerType type, const QString& name = "")
    {
        auto layer = std::make_shared<LayerData>();
        layer->type = type;
        layer->name = clampedName(name.isEmpty() ? defaultNameForType(type) : name);
        if (type == LayerType::Raster) {
            layer->tileGrid = std::make_unique<aether::TileGrid>();
        } else if (type == LayerType::Smart || type == LayerType::Board) {
            layer->smartContent = std::make_shared<SmartContent>();
        } else if (type == LayerType::Text) {
            layer->textData = std::make_unique<TextLayerData>();
            layer->nameIsCustom = !name.isEmpty() && layer->name != QStringLiteral("Text");
        }
        return layer;
    }

    static QString defaultNameForType(LayerType type)
    {
        switch (type) {
        case LayerType::Raster:
            return "Layer";
        case LayerType::Group:
            return "Group";
        case LayerType::Adjustment:
            return "Adjustment";
        case LayerType::Vector:
            return "Vector";
        case LayerType::Mask:
            return "Mask";
        case LayerType::Background:
            return "Background";
        case LayerType::Smart:
            return "Smart";
        case LayerType::Board:
            return "Board";
        case LayerType::Text:
            return "Text";
        }
        return "Layer";
    }

    // ========================================================================
    // Type Queries
    // ========================================================================

    bool isGroup() const { return type == LayerType::Group; }
    bool isGroupIsolated() const
    {
        return isGroup() && groupCompositingMode == GroupCompositingMode::Isolated;
    }
    bool isRaster() const { return type == LayerType::Raster; }
    bool isSmart() const { return type == LayerType::Smart; }
    bool isBoard() const { return type == LayerType::Board; }
    bool isText() const { return type == LayerType::Text; }
    bool isIsolatedPixelLayer() const { return isSmart() || isBoard(); }
    bool isPixelLayer() const { return isRaster() || isIsolatedPixelLayer(); }
    bool hasRetainedVisualContent() const
    {
        return runtimeVisualBackend == aether::LayerVisualBackend::RetainedSimpleForms
            && runtimeRetainedPayload && !runtimeRetainedPayload->empty();
    }
    bool isAdjustment() const { return type == LayerType::Adjustment; }
    bool isVector() const { return type == LayerType::Vector; }
    bool isMask() const { return type == LayerType::Mask; }
    bool isBackground() const { return type == LayerType::Background; }
    bool isExportExcluded() const { return isBoard(); }

    // ------------------------------------------------------------------------
    // Mask access
    // ------------------------------------------------------------------------

    /** @brief Does this layer carry a mask grid */
    bool hasMask() const { return maskGrid != nullptr; }

    /** @brief Mask grid (alpha = coverage), or nullptr if no mask */
    aether::TileGrid* maskTileGrid() { return maskGrid.get(); }
    const aether::TileGrid* maskTileGrid() const { return maskGrid.get(); }

    /** @brief Whether the mask currently affects compositing */
    bool maskAffectsCompositing() const { return hasMask() && maskEnabled; }

    /**
     * @brief Can this layer own a layer mask ("Add mask" target).
     *
     * Masks are painted in DOCUMENT space and the compositor gates the layer's
     * finished document-space grid with them, so a smart layer qualifies just
     * like a raster one: its projected grid is what the mask meets. Board layers
     * are composed by a separate surface and groups have no mask pipeline yet,
     * so both stay out.
     */
    bool canHostMask() const { return !isBackground() && (isRaster() || isSmart()); }

    /**
     * @brief Can this layer be wrapped into a smart object ("Convert to Smart
     *        Object" target).
     *
     * A conversion has to be able to name the contents the object will hold.
     * A raster layer IS those pixels, a text layer bakes its glyphs into them,
     * and a group's children become the object's nested document. Everything
     * else is out: the Background is not a free-floating layer, and smart/board
     * layers are already isolated content.
     */
    bool canConvertToSmartObject() const
    {
        return !isBackground() && (isRaster() || isText() || isGroup());
    }

    /**
     * @brief Can this layer's effects be moved into content space?
     *
     * Only a smart object has a content space to move them into: everything else
     * is authored directly in document space, so the flag has no meaning there
     * and is ignored rather than honoured (see LayerEffectState::contentSpace).
     */
    bool supportsContentSpaceEffects() const { return isSmart(); }

    /**
     * @brief The effects that run on the object's CONTENT, before its placement.
     *
     * Baked into the projected content by the canvas, which is why the compositor
     * must not see them: it would run them a second time, on the placed result.
     * Always empty for a layer that has no content space of its own.
     */
    QList<ruwa::core::effects::LayerEffectState> contentSpaceEffects() const
    {
        QList<ruwa::core::effects::LayerEffectState> result;
        if (!supportsContentSpaceEffects()) {
            return result;
        }
        for (const auto& effect : effects) {
            if (effect.contentSpace) {
                result.append(effect);
            }
        }
        return result;
    }

    /// The rest of the chain: everything applied to the finished document-space
    /// pixels. The whole chain for any layer without a content space.
    QList<ruwa::core::effects::LayerEffectState> documentSpaceEffects() const
    {
        if (!supportsContentSpaceEffects()) {
            return effects;
        }
        QList<ruwa::core::effects::LayerEffectState> result;
        for (const auto& effect : effects) {
            if (!effect.contentSpace) {
                result.append(effect);
            }
        }
        return result;
    }

    /// Cheap "is there anything to bake into the content" test.
    bool hasContentSpaceEffects() const
    {
        if (!supportsContentSpaceEffects()) {
            return false;
        }
        for (const auto& effect : effects) {
            if (effect.contentSpace && effect.enabled) {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Brush/fill edits currently land in this layer's mask, not its pixels.
     *
     * The mask is a plain RGBA8 grid regardless of the layer type, so edits that
     * target it never need the layer itself to be rasterizable.
     */
    bool maskIsEditTarget() const { return maskEditActive && maskGrid != nullptr; }

    /** @brief Create an empty (reveal-all) mask grid if none exists. Returns the grid. */
    aether::TileGrid* ensureMask()
    {
        if (!maskGrid) {
            maskGrid = std::make_unique<aether::TileGrid>();
            // Masks are alpha COVERAGE, not HDR content — keep them RGBA8
            // regardless of the document tile format. This keeps the whole mask
            // pipeline (8-bit CPU ops, RGBA8 selection temp textures, glCopyImageSubData)
            // working unchanged and avoids dragging masks into the 16F/32F path.
            maskGrid->setFormat(aether::TilePixelFormat::RGBA8);
            maskEnabled = true;
            maskThumbnailDirty = true;
        }
        return maskGrid.get();
    }

    /** @brief Remove the mask entirely. */
    void clearMask()
    {
        maskGrid.reset();
        maskEditActive = false;
        maskThumbnail = QPixmap();
        maskThumbnailDirty = true;
    }

    // ------------------------------------------------------------------------
    // Smart content access
    // ------------------------------------------------------------------------

    /** @brief Does this layer carry a smart content payload with pixels */
    bool hasSmartContent() const { return smartContent && smartContent->grid; }

    /** @brief The smart content's pixel grid (content space), or nullptr */
    aether::TileGrid* smartGrid() { return smartContent ? smartContent->grid.get() : nullptr; }
    const aether::TileGrid* smartGrid() const
    {
        return smartContent ? smartContent->grid.get() : nullptr;
    }

    /**
     * @brief The nested layer stack inside this smart object, or nullptr.
     *
     * Null both when there is no content at all and when the content is flat —
     * a flat content's pixels are the content itself (see SmartContent).
     */
    SmartDocument* smartDocument() { return smartContent ? smartContent->document.get() : nullptr; }
    const SmartDocument* smartDocument() const
    {
        return smartContent ? smartContent->document.get() : nullptr;
    }

    /** @brief Is this layer's content backed by a nested document */
    bool hasSmartDocument() const { return smartContent && smartContent->document != nullptr; }

    /** @brief Create an empty smart content payload if none exists. Never null. */
    SmartContent* ensureSmartContent()
    {
        if (!smartContent) {
            smartContent = std::make_shared<SmartContent>();
        }
        return smartContent.get();
    }

    /** @brief Is this layer's content also shown by another layer (an instance) */
    bool isSmartContentShared() const { return runtimeSmartInstanceCount > 1; }

    /**
     * @brief Give this layer a private copy of its content (copy-on-write).
     *
     * Call before an edit that must NOT reach through to the other instances.
     * The copy keeps the source description but gets a fresh contentId, which is
     * what breaks the link. No-op when the content is not shared.
     *
     * Sharing is counted with use_count() here rather than with
     * runtimeSmartInstanceCount, because correctness must not depend on the UI
     * bookkeeping being up to date: detaching one time too many costs a copy,
     * detaching one time too few corrupts another layer.
     */
    SmartContent* detachSmartContent()
    {
        if (!smartContent) {
            return ensureSmartContent();
        }
        if (smartContent.use_count() > 1) {
            smartContent = smartContent->cloneDetached();
            runtimeSmartInstanceCount = 1;
        }
        return smartContent.get();
    }

    /**
     * @brief Replace THIS layer's pixels, leaving any instances untouched.
     *
     * Installing pixels is a per-layer act, so a shared content is detached
     * first. (A future "Replace Contents" that deliberately updates every
     * instance must write through `smartContent->setGrid()` instead.)
     *
     * A nested document does not follow the detached layer: these pixels are the
     * content now, which is the same rule `SmartContent::setGrid()` follows.
     */
    void setSmartGrid(std::unique_ptr<aether::TileGrid> grid)
    {
        if (smartContent && smartContent.use_count() > 1) {
            smartContent = std::make_shared<SmartContent>();
            runtimeSmartInstanceCount = 1;
        }
        ensureSmartContent()->setGrid(std::move(grid));
    }

    /**
     * @brief Pixel-tight bounds of the smart content in CONTENT space.
     *
     * Empty rect when the layer has no content. Cached inside SmartContent, so
     * this is cheap enough to call per hit-test.
     */
    aether::Rect smartContentBounds() const
    {
        return smartContent ? smartContent->nativeBounds() : aether::Rect {};
    }

    /**
     * @brief Invalidation key for anything derived from this layer's content.
     *        0 when the layer has no smart content.
     */
    quint64 smartContentStamp() const { return smartContent ? smartContent->contentStamp() : 0ull; }

    /**
     * @brief The layer's editable pixels.
     *
     * Raster layers own their grid directly; smart/board layers expose their
     * content grid here so saving, thumbnails, transform bounds and undo memory
     * accounting keep working uniformly. Painting is NOT gated by this — the
     * brush paths check isRaster() — so returning a grid does not make a smart
     * layer paintable.
     */
    aether::TileGrid* pixelGrid()
    {
        if (isRaster())
            return tileGrid.get();
        if (isIsolatedPixelLayer())
            return smartGrid();
        return nullptr;
    }

    const aether::TileGrid* pixelGrid() const
    {
        if (isRaster())
            return tileGrid.get();
        if (isIsolatedPixelLayer())
            return smartGrid();
        return nullptr;
    }

    // ========================================================================
    // Hierarchy Queries
    // ========================================================================

    /** @brief Has any children */
    bool hasChildren() const { return !children.isEmpty(); }

    /** @brief Number of direct children */
    int childCount() const { return children.size(); }

    /** @brief Is this a root layer (no parent) */
    bool isRoot() const { return parent == nullptr; }

    /** @brief Get root ancestor */
    LayerData* root()
    {
        LayerData* current = this;
        while (current->parent) {
            current = current->parent;
        }
        return current;
    }

    const LayerData* root() const
    {
        const LayerData* current = this;
        while (current->parent) {
            current = current->parent;
        }
        return current;
    }

    /** @brief Check if this layer is inside the given layer (at any depth) */
    bool isDescendantOf(const LayerData* ancestor) const
    {
        if (!ancestor)
            return false;
        const LayerData* current = parent;
        while (current) {
            if (current == ancestor)
                return true;
            current = current->parent;
        }
        return false;
    }

    /** @brief Alias for isDescendantOf */
    bool isInsideGroup(const LayerData* group) const { return isDescendantOf(group); }

    /** @brief Check if this layer is ancestor of the given layer */
    bool isAncestorOf(const LayerData* descendant) const
    {
        if (!descendant)
            return false;
        return descendant->isDescendantOf(this);
    }

    // ========================================================================
    // Navigation - Siblings
    // ========================================================================

    /** @brief Get all siblings (including this layer) */
    QList<LayerData*> siblings() const
    {
        QList<LayerData*> result;
        if (parent) {
            for (const auto& child : parent->children) {
                result.append(child.get());
            }
        }
        return result;
    }

    /** @brief Get index within parent's children (or -1) */
    int indexInParent() const
    {
        if (!parent)
            return -1;
        for (int i = 0; i < parent->children.size(); ++i) {
            if (parent->children[i].get() == this) {
                return i;
            }
        }
        return -1;
    }

    /** @brief Get next sibling (or nullptr) */
    LayerData* nextSibling() const
    {
        if (!parent)
            return nullptr;
        int idx = indexInParent();
        if (idx < 0 || idx >= parent->children.size() - 1)
            return nullptr;
        return parent->children[idx + 1].get();
    }

    /** @brief Get previous sibling (or nullptr) */
    LayerData* prevSibling() const
    {
        if (!parent)
            return nullptr;
        int idx = indexInParent();
        if (idx <= 0)
            return nullptr;
        return parent->children[idx - 1].get();
    }

    /** @brief Get first sibling */
    LayerData* firstSibling() const
    {
        if (!parent || parent->children.isEmpty())
            return nullptr;
        return parent->children.first().get();
    }

    /** @brief Get last sibling */
    LayerData* lastSibling() const
    {
        if (!parent || parent->children.isEmpty())
            return nullptr;
        return parent->children.last().get();
    }

    // ========================================================================
    // Navigation - Children
    // ========================================================================

    /** @brief Get first child (or nullptr) */
    LayerData* firstChild() const { return children.isEmpty() ? nullptr : children.first().get(); }

    /** @brief Get last child (or nullptr) */
    LayerData* lastChild() const { return children.isEmpty() ? nullptr : children.last().get(); }

    /** @brief Get child at index (or nullptr) */
    LayerData* childAt(int index) const
    {
        if (index < 0 || index >= children.size())
            return nullptr;
        return children[index].get();
    }

    /** @brief Find child by id (direct children only) */
    LayerData* findChild(const LayerId& childId) const
    {
        for (const auto& child : children) {
            if (child->id == childId)
                return child.get();
        }
        return nullptr;
    }

    /** @brief Find descendant by id (recursive) */
    LayerData* findDescendant(const LayerId& descendantId) const
    {
        for (const auto& child : children) {
            if (child->id == descendantId)
                return child.get();
            if (auto* found = child->findDescendant(descendantId)) {
                return found;
            }
        }
        return nullptr;
    }

    // ========================================================================
    // Navigation - Ancestors
    // ========================================================================

    /** @brief Get list of all ancestors (from immediate parent to root) */
    QList<LayerData*> ancestors() const
    {
        QList<LayerData*> result;
        LayerData* current = parent;
        while (current) {
            result.append(current);
            current = current->parent;
        }
        return result;
    }

    /** @brief Get path from root to this layer (including this) */
    QList<LayerData*> pathFromRoot()
    {
        QList<LayerData*> path = ancestors();
        std::reverse(path.begin(), path.end());
        path.append(this);
        return path;
    }

    /** @brief Get path as string (e.g. "Root/Group1/Layer") */
    QString pathString(const QString& separator = "/") const
    {
        QStringList parts;
        const LayerData* current = this;
        while (current) {
            parts.prepend(current->name);
            current = current->parent;
        }
        return parts.join(separator);
    }

    // ========================================================================
    // Navigation - Tree Traversal (for UI)
    // ========================================================================

    /** @brief Get next layer in visual order (depth-first) */
    LayerData* nextInTree() const
    {
        // First, try first child (if expanded or always if not respecting expansion)
        if (hasChildren() && expanded) {
            return children.first().get();
        }
        // Then try next sibling
        if (auto* next = nextSibling()) {
            return next;
        }
        // Go up and try parent's next sibling
        const LayerData* current = this;
        while (current->parent) {
            if (auto* parentNext = current->parent->nextSibling()) {
                return parentNext;
            }
            current = current->parent;
        }
        return nullptr;
    }

    /** @brief Get previous layer in visual order (depth-first) */
    LayerData* prevInTree() const
    {
        // Try previous sibling's deepest last descendant
        if (auto* prev = prevSibling()) {
            return prev->deepestLastChild();
        }
        // Otherwise return parent
        return parent;
    }

    /** @brief Get the deepest last child (for tree traversal) */
    LayerData* deepestLastChild()
    {
        LayerData* current = this;
        while (current->hasChildren() && current->expanded) {
            current = current->children.last().get();
        }
        return current;
    }

    const LayerData* deepestLastChild() const
    {
        const LayerData* current = this;
        while (current->hasChildren() && current->expanded) {
            current = current->children.last().get();
        }
        return current;
    }

    // ========================================================================
    // Children Modification
    // ========================================================================

    /** @brief Add child at end */
    void addChild(std::shared_ptr<LayerData> child)
    {
        if (!child)
            return;
        child->parent = this;
        child->depth = depth + 1;
        children.append(child);
        child->updateChildrenDepth();
    }

    /** @brief Insert child at index */
    void insertChild(int index, std::shared_ptr<LayerData> child)
    {
        if (!child)
            return;
        child->parent = this;
        child->depth = depth + 1;
        index = qBound(0, index, children.size());
        children.insert(index, child);
        child->updateChildrenDepth();
    }

    /** @brief Remove child (doesn't delete, just removes from list) */
    std::shared_ptr<LayerData> removeChild(LayerData* child)
    {
        for (int i = 0; i < children.size(); ++i) {
            if (children[i].get() == child) {
                auto removed = children[i];
                removed->parent = nullptr;
                children.removeAt(i);
                return removed;
            }
        }
        return nullptr;
    }

    /** @brief Remove child by id */
    std::shared_ptr<LayerData> removeChildById(const LayerId& childId)
    {
        for (int i = 0; i < children.size(); ++i) {
            if (children[i]->id == childId) {
                auto removed = children[i];
                removed->parent = nullptr;
                children.removeAt(i);
                return removed;
            }
        }
        return nullptr;
    }

    /** @brief Remove all children */
    void clearChildren()
    {
        for (auto& child : children) {
            child->parent = nullptr;
        }
        children.clear();
    }

    /** @brief Move child to new index within same parent */
    bool moveChild(int fromIndex, int toIndex)
    {
        if (fromIndex < 0 || fromIndex >= children.size())
            return false;
        if (toIndex < 0 || toIndex >= children.size())
            return false;
        if (fromIndex == toIndex)
            return false;

        auto child = children.takeAt(fromIndex);
        children.insert(toIndex, child);
        return true;
    }

    /** @brief Update depth for all children recursively */
    void updateChildrenDepth()
    {
        for (auto& child : children) {
            child->depth = depth + 1;
            child->updateChildrenDepth();
        }
    }

    // ========================================================================
    // Flattening (for UI lists)
    // ========================================================================

    /** @brief Count all descendants */
    int totalDescendantCount() const
    {
        int count = 0;
        for (const auto& child : children) {
            count += 1 + child->totalDescendantCount();
        }
        return count;
    }

    /** @brief Count visible descendants (respects expansion) */
    int visibleDescendantCount() const
    {
        if (!expanded)
            return 0;
        int count = 0;
        for (const auto& child : children) {
            count += 1 + child->visibleDescendantCount();
        }
        return count;
    }

    /** @brief Flatten to list (depth-first) */
    void flatten(QList<LayerData*>& result, bool respectExpansion = true) const
    {
        for (const auto& child : children) {
            result.append(child.get());
            if (!respectExpansion || child->expanded) {
                child->flatten(result, respectExpansion);
            }
        }
    }
};

// ============================================================================
// Clipping helpers (sibling-list based)
// ============================================================================
//
// A clipped layer and its clip base are always siblings, so both queries work
// on one sibling list. Root layers have no parent pointer, so callers that
// start from a bare LayerData* must obtain the list from LayerModel (see
// LayerModel::clipBaseFor / isHiddenByClipBase).

/**
 * @brief The layer that the sibling at \a index is clipped onto.
 *
 * The clip base is the nearest sibling BELOW (higher index — the lists are
 * ordered top-first) that is not itself clipped. Mirrors
 * LayerModel::inferClipBaseForIndex. Returns nullptr when the layer is not
 * clipped or no base exists.
 */
inline const LayerData* clipBaseInSiblings(
    const QList<std::shared_ptr<LayerData>>& siblings, int index)
{
    if (index < 0 || index >= siblings.size()) {
        return nullptr;
    }
    const LayerData* layer = siblings[index].get();
    if (!layer || !layer->clippedToBelow) {
        return nullptr;
    }
    for (int i = index + 1; i < siblings.size(); ++i) {
        const LayerData* below = siblings[i].get();
        if (!below || below->isBackground()) {
            continue;
        }
        if (!below->clippedToBelow) {
            return below;
        }
    }
    return nullptr;
}

/**
 * @brief Whether the sibling at \a index is contextually hidden by its clip base.
 *
 * A clipping mask is defined by its base: hiding the base takes the whole clip
 * group with it (Photoshop semantics). This is "contextual" hiding, exactly
 * like a group's children — the layer's own `visible` flag stays untouched, so
 * the eye icon stays open and only the rendered result and the row's dimming
 * follow the base.
 */
inline bool isHiddenByClipBaseInSiblings(
    const QList<std::shared_ptr<LayerData>>& siblings, int index)
{
    const LayerData* base = clipBaseInSiblings(siblings, index);
    return base && !base->visible;
}

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_LAYERDATA_H
