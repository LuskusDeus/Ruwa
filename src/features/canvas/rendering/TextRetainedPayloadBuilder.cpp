// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/TextRetainedPayloadBuilder.h"

#include "features/layers/model/LayerData.h"

#include <QColor>
#include <QFont>
#include <QGlyphRun>
#include <QPainterPath>
#include <QRawFont>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextLayout>
#include <QTextOption>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>

namespace aether {

namespace {

// Effectively "never wrap": the layout has no text frame, so a line ends only
// where the text says it does. It used to be 4096, which a single line can now
// exceed on its own — the Character group tops out at a 2000px font, and a
// wrapped line would silently change both the line count and the box the
// alignment measures against.
constexpr qreal kTextLayoutWrapWidth = 1000000.0;
constexpr float kBoundsPadding = 4.0f;

using ruwa::core::layers::TextAlignment;
using ruwa::core::layers::TextCaps;
using ruwa::core::layers::TextLayerData;
using ruwa::core::layers::TextStyleRun;

struct EffectiveStyle {
    QString fontFamily;
    qreal fontSize = 48.0;
    QColor color = QColor(0, 0, 0);
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    qreal tracking = 0.0;
    TextCaps caps = TextCaps::None;

    bool operator==(const EffectiveStyle& other) const
    {
        return fontFamily == other.fontFamily && qFuzzyCompare(fontSize, other.fontSize)
            && color.rgba() == other.color.rgba() && bold == other.bold && italic == other.italic
            && underline == other.underline && strikethrough == other.strikethrough
            && qFuzzyCompare(tracking, other.tracking) && caps == other.caps;
    }
};

/// Tracking is stored in Photoshop units (thousandths of an em); Qt wants an
/// absolute pixel amount, which is what makes it scale with the font size.
qreal letterSpacingPixels(qreal fontSize, qreal tracking)
{
    return qMax<qreal>(1.0, fontSize) * tracking / 1000.0;
}

QFont::Capitalization toQtCapitalization(TextCaps caps)
{
    switch (caps) {
    case TextCaps::AllCaps:
        return QFont::AllUppercase;
    case TextCaps::SmallCaps:
        return QFont::SmallCaps;
    case TextCaps::None:
        break;
    }
    return QFont::MixedCase;
}

Color toRetainedColor(const QColor& color)
{
    return { static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()), static_cast<float>(color.alphaF()) };
}

Rect rectFromQRectF(const QRectF& rect)
{
    return { static_cast<float>(rect.x()), static_cast<float>(rect.y()),
        static_cast<float>(rect.width()), static_cast<float>(rect.height()) };
}

void expandRect(Rect& rect, const Rect& other)
{
    if (other.width <= 0.0f || other.height <= 0.0f) {
        return;
    }
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        rect = other;
        return;
    }

    const float left = std::min(rect.left(), other.left());
    const float top = std::min(rect.top(), other.top());
    const float right = std::max(rect.right(), other.right());
    const float bottom = std::max(rect.bottom(), other.bottom());
    rect = { left, top, right - left, bottom - top };
}

Rect paddedRect(Rect rect)
{
    if (rect.width <= 0.0f || rect.height <= 0.0f) {
        return rect;
    }
    rect.x -= kBoundsPadding;
    rect.y -= kBoundsPadding;
    rect.width += kBoundsPadding * 2.0f;
    rect.height += kBoundsPadding * 2.0f;
    return rect;
}

QString styleRunsKey(const QList<TextStyleRun>& runs)
{
    QString key;
    for (const auto& run : runs) {
        key += QStringLiteral("|sr:%1,%2,%3,%4,%5")
                   .arg(run.start)
                   .arg(run.length)
                   .arg(run.fontFamily)
                   .arg(QString::number(run.fontSize, 'f', 3))
                   .arg(QString::number(run.color.rgba(), 16));
        key += QStringLiteral(",%1,%2,%3,%4,%5,%6")
                   .arg(run.bold ? 1 : 0)
                   .arg(run.italic ? 1 : 0)
                   .arg(run.underline ? 1 : 0)
                   .arg(run.strikethrough ? 1 : 0)
                   .arg(QString::number(run.tracking, 'f', 3))
                   .arg(static_cast<int>(run.caps));
    }
    return key;
}

QString transformKey(const TransformState& transform)
{
    QString key = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11")
                      .arg(QString::number(transform.contentBounds.x, 'f', 3),
                          QString::number(transform.contentBounds.y, 'f', 3),
                          QString::number(transform.contentBounds.width, 'f', 3),
                          QString::number(transform.contentBounds.height, 'f', 3),
                          QString::number(transform.translation.x, 'f', 3),
                          QString::number(transform.translation.y, 'f', 3),
                          QString::number(transform.rotation, 'f', 3),
                          QString::number(transform.scale.x, 'f', 3),
                          QString::number(transform.scale.y, 'f', 3),
                          QString::number(transform.pivot.x, 'f', 3),
                          QString::number(transform.pivot.y, 'f', 3));

    if (transform.freeCorners.has_value()) {
        for (const auto& corner : *transform.freeCorners) {
            key += QStringLiteral("|c:%1,%2")
                       .arg(QString::number(corner.x, 'f', 3), QString::number(corner.y, 'f', 3));
        }
    }
    if (transform.deformMesh.has_value()) {
        key += QStringLiteral("|m:%1,%2")
                   .arg(transform.deformMesh->rows)
                   .arg(transform.deformMesh->cols);
        for (const auto& vertex : transform.deformMesh->vertices) {
            key += QStringLiteral("|v:%1,%2")
                       .arg(QString::number(vertex.target.x, 'f', 3),
                           QString::number(vertex.target.y, 'f', 3));
        }
    }
    return key;
}

uint64_t revisionFromKey(const QString& key)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return static_cast<uint64_t>(qHash(key, size_t { 0 }));
#else
    return static_cast<uint64_t>(qHash(key));
#endif
}

EffectiveStyle defaultStyle(const TextLayerData& textData)
{
    EffectiveStyle style;
    style.fontFamily = textData.fontFamily;
    style.fontSize = qMax<qreal>(1.0, textData.fontSize);
    style.color = textData.color;
    style.strikethrough = textData.strikethrough;
    style.tracking = textData.tracking;
    style.caps = textData.caps;
    return style;
}

EffectiveStyle styleAt(const TextLayerData& textData, int index)
{
    EffectiveStyle style = defaultStyle(textData);
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

std::unique_ptr<QTextLayout> createTextLayout(const TextLayerData& textData, const QString& text)
{
    const EffectiveStyle base = defaultStyle(textData);
    QFont baseFont(base.fontFamily);
    baseFont.setPointSizeF(base.fontSize);
    baseFont.setBold(base.bold);
    baseFont.setItalic(base.italic);
    baseFont.setUnderline(base.underline);
    baseFont.setStrikeOut(base.strikethrough);
    baseFont.setCapitalization(toQtCapitalization(base.caps));
    if (!qFuzzyIsNull(base.tracking)) {
        baseFont.setLetterSpacing(
            QFont::AbsoluteSpacing, letterSpacingPixels(base.fontSize, base.tracking));
    }

    QString layoutText = text;
    layoutText.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    layoutText.replace(QChar('\r'), QChar('\n'));
    layoutText.replace(QChar('\n'), QChar(QChar::LineSeparator));

    auto layout = std::make_unique<QTextLayout>(layoutText, baseFont);
    QTextOption option;
    option.setWrapMode(QTextOption::WordWrap);
    option.setAlignment(Qt::AlignLeft);
    layout->setTextOption(option);

    QList<QTextLayout::FormatRange> formats;
    formats.reserve(textData.styleRuns.size());
    const int textLength = static_cast<int>(layoutText.size());
    for (const TextStyleRun& run : textData.styleRuns) {
        const int start = std::clamp(run.start, 0, textLength);
        const int end = std::clamp(run.start + run.length, 0, textLength);
        if (end <= start) {
            continue;
        }

        QTextCharFormat format;
        if (!run.fontFamily.isEmpty()) {
            format.setFontFamilies(QStringList { run.fontFamily });
        }
        format.setFontPointSize(qMax<qreal>(1.0, run.fontSize));
        format.setFontWeight(run.bold ? QFont::Bold : QFont::Normal);
        format.setFontItalic(run.italic);
        format.setFontUnderline(run.underline);
        // The strike-out line is drawn as its own primitive (the glyph path
        // renderer only ever sees glyphs), but the format still carries it so
        // any metric Qt derives from it stays consistent.
        format.setFontStrikeOut(run.strikethrough);
        format.setFontCapitalization(toQtCapitalization(run.caps));
        format.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        format.setFontLetterSpacing(letterSpacingPixels(run.fontSize, run.tracking));
        format.setForeground(run.color);

        QTextLayout::FormatRange range;
        range.start = start;
        range.length = end - start;
        range.format = format;
        formats.append(range);
    }
    layout->setFormats(formats);
    return layout;
}

qreal alignedLineX(TextAlignment alignment, qreal lineWidth, qreal maxWidth)
{
    switch (alignment) {
    case TextAlignment::Center:
        return (maxWidth - lineWidth) * 0.5;
    case TextAlignment::Right:
        return maxWidth - lineWidth;
    case TextAlignment::Left:
    case TextAlignment::Justify:
        break;
    }
    return 0.0;
}

QRectF lineSourceRect(const QTextLine& line)
{
    QRectF rect = line.naturalTextRect();
    if (rect.width() <= 0.0) {
        rect.setWidth(1.0);
    }
    if (rect.height() <= 0.0) {
        rect.setHeight(line.height());
    }
    return rect;
}

Rect glyphRunOutlineRect(const QGlyphRun& run)
{
    Rect bounds {};
    const QRawFont rawFont = run.rawFont();
    const auto glyphs = run.glyphIndexes();
    const auto positions = run.positions();
    const int count = std::min(glyphs.size(), positions.size());
    if (rawFont.isValid()) {
        for (int i = 0; i < count; ++i) {
            const QPainterPath path = rawFont.pathForGlyph(glyphs[i]);
            const QRectF pathBounds = path.boundingRect();
            if (!pathBounds.isValid() || pathBounds.width() <= 0.0 || pathBounds.height() <= 0.0) {
                continue;
            }
            const QPointF& pos = positions[i];
            expandRect(bounds, rectFromQRectF(pathBounds.translated(pos)));
        }
    }

    if (bounds.width > 0.0f && bounds.height > 0.0f) {
        return bounds;
    }

    const QRectF rect = run.boundingRect();
    if (rect.isValid() && rect.width() > 0.0 && rect.height() > 0.0) {
        return rectFromQRectF(rect);
    }

    for (const QPointF& pos : positions) {
        expandRect(
            bounds, { static_cast<float>(pos.x()), static_cast<float>(pos.y()), 1.0f, 1.0f });
    }
    return bounds;
}

Rect lineVisualRect(const QTextLine& line)
{
    Rect bounds = rectFromQRectF(lineSourceRect(line));
    const auto glyphRuns = line.glyphRuns();
    for (const QGlyphRun& run : glyphRuns) {
        expandRect(bounds, glyphRunOutlineRect(run));
    }
    return bounds;
}

Rect layoutText(QTextLayout& layout, const TextLayerData& textData)
{
    const qreal lineHeightScale = qMax<qreal>(0.1, textData.lineHeight);
    const qreal spaceBefore = qMax<qreal>(0.0, textData.spaceBefore);
    const qreal spaceAfter = qMax<qreal>(0.0, textData.spaceAfter);
    const bool justify = textData.alignment == TextAlignment::Justify;

    // There is no text frame: the box every line is aligned inside is the
    // widest line, so the widths have to be measured before anything can be
    // placed.
    const auto runLayout = [&layout](qreal wrapWidth, bool justified) {
        QTextOption option = layout.textOption();
        option.setAlignment(justified ? Qt::AlignJustify : Qt::AlignLeft);
        layout.setTextOption(option);

        qreal widest = 1.0;
        layout.beginLayout();
        while (true) {
            QTextLine line = layout.createLine();
            if (!line.isValid()) {
                break;
            }
            line.setLineWidth(wrapWidth);
            widest = qMax(widest, line.naturalTextWidth());
        }
        layout.endLayout();
        return widest;
    };

    const qreal boxWidth = runLayout(kTextLayoutWrapWidth, /*justified=*/false);
    if (justify) {
        // Justification is Qt's to distribute, and it only does it once the
        // line knows the width it has to fill. The last line of the layout
        // stays flush left, which is Photoshop's "justify last left".
        layout.clearLayout();
        runLayout(boxWidth, /*justified=*/true);
    }

    Rect bounds {};
    qreal y = 0.0;
    for (int i = 0; i < layout.lineCount(); ++i) {
        QTextLine line = layout.lineAt(i);
        y += spaceBefore;
        // Justified lines were already spread to the box by Qt; re-placing them
        // by their natural width would undo exactly that.
        const qreal x = justify ? 0.0 : alignedLineX(textData.alignment, line.naturalTextWidth(),
                            boxWidth);
        line.setPosition(QPointF(x, y));
        y += line.height() * lineHeightScale + spaceAfter;
        expandRect(bounds, lineVisualRect(line));
    }
    return paddedRect(bounds);
}

TextLayoutGeometry collectTextLayoutGeometry(QTextLayout& layout, const TextLayerData& textData)
{
    TextLayoutGeometry geometry;
    geometry.sourceBounds = layoutText(layout, textData);
    geometry.lines.reserve(static_cast<size_t>(layout.lineCount()));
    for (int i = 0; i < layout.lineCount(); ++i) {
        const QTextLine line = layout.lineAt(i);
        const QRectF rect = lineSourceRect(line);
        geometry.lines.push_back({ line.textStart(), line.textLength(),
            static_cast<float>(rect.x()), static_cast<float>(rect.y()),
            static_cast<float>(rect.width()), static_cast<float>(rect.height()) });
    }
    return geometry;
}

Rect boundsForGlyphRun(const QGlyphRun& run)
{
    return paddedRect(glyphRunOutlineRect(run));
}

Rect transformRectAABB(const TransformState& transform, const Rect& source)
{
    Rect bounds {};
    const Vector2 points[4] = { { source.left(), source.top() }, { source.right(), source.top() },
        { source.right(), source.bottom() }, { source.left(), source.bottom() } };
    for (const Vector2& point : points) {
        const Vector2 mapped = transform.transformPoint(point);
        expandRect(bounds, { mapped.x, mapped.y, 1.0f, 1.0f });
    }
    return bounds;
}

/**
 * @brief The underline or strike-out bar under/through [@p start, @p end).
 *
 * Both rules are drawn rather than left to the font because the glyph renderer
 * only ever walks glyph paths; the only difference between them is where the
 * bar sits, so they share one builder.
 */
RetainedPrimitive textRulePrimitiveForRange(const QTextLine& line, const TransformState& transform,
    int start, int end, const EffectiveStyle& style, bool strikeOut)
{
    RetainedPrimitive primitive;
    primitive.type = RetainedPrimitiveType::FilledPolygon;
    if (!(strikeOut ? style.strikethrough : style.underline) || end <= start) {
        return primitive;
    }

    // cursorToX() is already in layout space: it starts from the line's own x
    // and the alignment offset Qt derives from the text option. Adding
    // line.position().x() on top counts the alignment twice, which is invisible
    // while the text is left aligned and slides the bar right the moment it is
    // not.
    const qreal x1 = line.cursorToX(start);
    const qreal x2 = line.cursorToX(end);
    const float left = static_cast<float>(std::min(x1, x2));
    const float right = static_cast<float>(std::max(x1, x2));
    if (right - left < 0.5f) {
        // The range advances nothing — an empty line, or a bare line separator.
        // Widening it to a minimum would put a stub of a rule under a line that
        // has no text, which centred alignment then parks in mid air.
        return primitive;
    }
    const float width = right - left;
    const float thickness = std::max(1.0f, static_cast<float>(style.fontSize * 0.055));
    // Underline sits just below the baseline; the strike-out rides a bit above
    // the middle of the x-height, the same place the font's own would.
    const float y = strikeOut
        ? static_cast<float>(line.position().y() + line.ascent() - line.ascent() * 0.28)
        : static_cast<float>(line.position().y() + line.ascent() + thickness);
    const Rect sourceRect { left, y, width, thickness };

    const Vector2 p1 = transform.transformPoint({ sourceRect.left(), sourceRect.top() });
    const Vector2 p2 = transform.transformPoint({ sourceRect.right(), sourceRect.top() });
    const Vector2 p3 = transform.transformPoint({ sourceRect.right(), sourceRect.bottom() });
    const Vector2 p4 = transform.transformPoint({ sourceRect.left(), sourceRect.bottom() });
    primitive.points = { p1, p2, p3, p4 };
    primitive.color = toRetainedColor(style.color);
    primitive.worldBounds = retainedPolygonBounds(primitive.points);
    return primitive;
}

void normalizeTextTransform(ruwa::core::layers::LayerData* layer, const Rect& sourceBounds)
{
    if (!layer || !layer->textData || sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
        return;
    }
    auto& transform = layer->textData->transform;
    if (transform.contentBounds.width <= 0.0f || transform.contentBounds.height <= 0.0f) {
        transform.contentBounds = sourceBounds;
        transform.pivot = sourceBounds.center();
    }
}

QString effectiveText(const TextLayerData& textData)
{
    return textData.text.isEmpty() ? QStringLiteral("Text") : textData.text;
}

} // namespace

QString textRetainedPayloadKey(const ruwa::core::layers::LayerData* layer)
{
    const auto* textData = layer ? layer->textData.get() : nullptr;
    if (!textData) {
        return {};
    }
    return textRetainedPayloadKey(layer, textData->transform);
}

QString textRetainedPayloadKey(
    const ruwa::core::layers::LayerData* layer, const TransformState& transformOverride)
{
    const auto* textData = layer ? layer->textData.get() : nullptr;
    if (!textData) {
        return {};
    }

    // Every field the layout reads has to be in the key: the payload cache is
    // keyed on it alone, so a value missing here shows as stale glyphs.
    const QString paragraphKey
        = QStringLiteral("|p:%1,%2|c:%3,%4,%5")
              .arg(QString::number(textData->spaceBefore, 'f', 3),
                  QString::number(textData->spaceAfter, 'f', 3),
                  QString::number(textData->strikethrough ? 1 : 0),
                  QString::number(textData->tracking, 'f', 3),
                  QString::number(static_cast<int>(textData->caps)));

    return QStringLiteral("text-v3|%1|%2|%3|%4|%5|%6%7%8|%9")
        .arg(textData->text, textData->fontFamily, QString::number(textData->fontSize, 'f', 3),
            QString::number(textData->color.rgba(), 16),
            QString::number(static_cast<int>(textData->alignment)),
            QString::number(textData->lineHeight, 'f', 3), paragraphKey,
            styleRunsKey(textData->styleRuns), transformKey(transformOverride));
}

Rect computeTextLayoutSourceBounds(const TextLayerData& textData)
{
    auto layout = createTextLayout(textData, effectiveText(textData));
    return layoutText(*layout, textData);
}

TextLayoutGeometry computeTextLayoutGeometry(const TextLayerData& textData)
{
    const QString text = textData.text.isEmpty() ? QStringLiteral(" ") : textData.text;
    auto layout = createTextLayout(textData, text);
    return collectTextLayoutGeometry(*layout, textData);
}

std::vector<Rect> computeTextSelectionSourceRects(
    const TextLayerData& textData, int selectionStart, int selectionEnd)
{
    const QString text = textData.text.isEmpty() ? QStringLiteral(" ") : textData.text;
    if (selectionStart == selectionEnd || text.isEmpty()) {
        return {};
    }

    const int textSize = static_cast<int>(text.size());
    const int from = std::clamp(std::min(selectionStart, selectionEnd), 0, textSize);
    const int to = std::clamp(std::max(selectionStart, selectionEnd), 0, textSize);
    auto layout = createTextLayout(textData, text);
    layoutText(*layout, textData);

    std::vector<Rect> rects;
    for (int i = 0; i < layout->lineCount(); ++i) {
        const QTextLine line = layout->lineAt(i);
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        const int rangeStart = std::max(from, lineStart);
        const int rangeEnd = std::min(to, lineEnd);
        if (rangeEnd <= rangeStart) {
            continue;
        }

        // Layout space already — see textRulePrimitiveForRange.
        const qreal x1 = line.cursorToX(rangeStart);
        const qreal x2 = line.cursorToX(rangeEnd);
        const qreal left = std::min(x1, x2);
        const qreal right = std::max(x1, x2);
        rects.push_back({ static_cast<float>(left), static_cast<float>(line.position().y()),
            static_cast<float>(std::max<qreal>(1.0, right - left)),
            static_cast<float>(line.height()) });
    }
    return rects;
}

Rect computeTextCaretSourceRect(const TextLayerData& textData, int cursorPosition)
{
    const QString text = textData.text.isEmpty() ? QStringLiteral(" ") : textData.text;
    auto layout = createTextLayout(textData, text);
    layoutText(*layout, textData);

    const int actualTextSize = static_cast<int>(textData.text.size());
    const int pos = std::clamp(cursorPosition, 0, actualTextSize);
    for (int i = 0; i < layout->lineCount(); ++i) {
        const QTextLine line = layout->lineAt(i);
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        const bool lastLine = i == layout->lineCount() - 1;
        if (!lastLine && pos == lineEnd && layout->lineAt(i + 1).textStart() == pos) {
            continue;
        }
        if (pos < lineStart || (pos > lineEnd && !(lastLine && pos == actualTextSize))) {
            continue;
        }

        const qreal x = line.cursorToX(std::clamp(pos, lineStart, lineEnd));
        return { static_cast<float>(x),
            static_cast<float>(line.position().y()), 1.5f, static_cast<float>(line.height()) };
    }
    return {};
}

int textCursorPositionAtSourcePoint(const TextLayerData& textData, const Vector2& sourcePoint)
{
    const QString text = textData.text.isEmpty() ? QStringLiteral(" ") : textData.text;
    auto layout = createTextLayout(textData, text);
    layoutText(*layout, textData);

    int bestPosition = textData.text.size();
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < layout->lineCount(); ++i) {
        const QTextLine line = layout->lineAt(i);
        const QRectF natural = lineSourceRect(line);
        const float dy = sourcePoint.y < natural.top()
            ? static_cast<float>(natural.top() - sourcePoint.y)
            : sourcePoint.y > natural.bottom()
            ? static_cast<float>(sourcePoint.y - natural.bottom())
            : 0.0f;
        if (dy > bestDistance) {
            continue;
        }

        // xToCursor() is the inverse of cursorToX() and takes the same layout
        // space, so the line's own x must not be subtracted first.
        const int textCursor = std::clamp(
            line.xToCursor(sourcePoint.x), 0, static_cast<int>(textData.text.size()));
        bestPosition = textCursor;
        bestDistance = dy;
    }
    return bestPosition;
}

std::shared_ptr<RetainedRenderPayload> buildTextRetainedPayload(
    const ruwa::core::layers::LayerData* layer)
{
    const auto* textData = layer ? layer->textData.get() : nullptr;
    if (!textData) {
        return nullptr;
    }

    const Rect sourceBounds = computeTextLayoutSourceBounds(*textData);
    TransformState transform = textData->transform;
    if (transform.contentBounds.width <= 0.0f || transform.contentBounds.height <= 0.0f) {
        transform.contentBounds = sourceBounds;
        transform.pivot = sourceBounds.center();
    }
    return buildTextRetainedPayload(layer, transform);
}

std::shared_ptr<RetainedRenderPayload> buildTextRetainedPayload(
    const ruwa::core::layers::LayerData* layer, const TransformState& transformOverride)
{
    const auto* textData = layer ? layer->textData.get() : nullptr;
    if (!textData) {
        return nullptr;
    }

    const QString text = effectiveText(*textData);
    auto layout = createTextLayout(*textData, text);
    const Rect sourceBounds = layoutText(*layout, *textData);
    if (sourceBounds.width <= 0.0f || sourceBounds.height <= 0.0f) {
        return nullptr;
    }

    TransformState transform = transformOverride;
    if (transform.contentBounds.width <= 0.0f || transform.contentBounds.height <= 0.0f) {
        transform.contentBounds = sourceBounds;
        transform.pivot = sourceBounds.center();
    }

    auto payload = std::make_shared<RetainedRenderPayload>();
    payload->revision = revisionFromKey(textRetainedPayloadKey(layer, transform));
    payload->sourceBounds = sourceBounds;
    payload->worldBounds = transform.isIdentity() ? sourceBounds : transform.transformedAABB();

    RetainedPrimitive primitive;
    primitive.type = RetainedPrimitiveType::GlyphRun;
    primitive.worldBounds = payload->worldBounds;

    for (int lineIndex = 0; lineIndex < layout->lineCount(); ++lineIndex) {
        const QTextLine line = layout->lineAt(lineIndex);
        const int lineStart = line.textStart();
        const int lineEnd = lineStart + line.textLength();
        for (int pos = lineStart; pos < lineEnd;) {
            const EffectiveStyle style = styleAt(*textData, pos);
            int end = pos + 1;
            while (end < lineEnd && styleAt(*textData, end) == style) {
                ++end;
            }

            const auto glyphRuns = line.glyphRuns(pos, end - pos);
            for (const QGlyphRun& qtRun : glyphRuns) {
                RetainedGlyphRun run;
                QString family = qtRun.rawFont().familyName();
                if (family.isEmpty()) {
                    family = style.fontFamily;
                }
                run.fontFamily = family.toStdString();
                run.fontSize = static_cast<float>(style.fontSize);
                run.rawFont = qtRun.rawFont();
                if (!run.rawFont.isValid()) {
                    QFont fallbackFont(family);
                    fallbackFont.setPointSizeF(style.fontSize);
                    fallbackFont.setBold(style.bold);
                    fallbackFont.setItalic(style.italic);
                    fallbackFont.setUnderline(style.underline);
                    run.rawFont = QRawFont::fromFont(fallbackFont);
                }
                run.transform = transform;
                run.color = toRetainedColor(style.color);
                run.sourceBounds = boundsForGlyphRun(qtRun);
                run.worldBounds = transformRectAABB(transform, run.sourceBounds);
                expandRect(payload->worldBounds, run.worldBounds);
                expandRect(primitive.worldBounds, run.worldBounds);

                const auto glyphs = qtRun.glyphIndexes();
                const auto positions = qtRun.positions();
                const int count = std::min(glyphs.size(), positions.size());
                run.glyphIndexes.reserve(static_cast<size_t>(count));
                run.sourcePositions.reserve(static_cast<size_t>(count));
                run.worldPositions.reserve(static_cast<size_t>(count));
                for (int i = 0; i < count; ++i) {
                    const Vector2 sourcePos { static_cast<float>(positions[i].x()),
                        static_cast<float>(positions[i].y()) };
                    run.glyphIndexes.push_back(static_cast<uint32_t>(glyphs[i]));
                    run.sourcePositions.push_back(sourcePos);
                    run.worldPositions.push_back(transform.transformPoint(sourcePos));
                }
                if (!run.empty()) {
                    primitive.glyphRuns.push_back(std::move(run));
                }
            }
            for (const bool strikeOut : { false, true }) {
                auto rulePrimitive
                    = textRulePrimitiveForRange(line, transform, pos, end, style, strikeOut);
                if (!rulePrimitive.isEmpty()) {
                    expandRect(payload->worldBounds, rulePrimitive.worldBounds);
                    payload->primitives.push_back(std::move(rulePrimitive));
                }
            }
            pos = end;
        }
    }

    if (!primitive.isEmpty()) {
        payload->primitives.push_back(std::move(primitive));
    }
    return payload;
}

bool ensureTextRetainedPayload(ruwa::core::layers::LayerData* layer)
{
    if (!layer || !layer->isText() || !layer->textData) {
        return false;
    }

    const Rect sourceBounds = computeTextLayoutSourceBounds(*layer->textData);
    normalizeTextTransform(layer, sourceBounds);

    const QString key = textRetainedPayloadKey(layer);
    if (!layer->runtimeRetainedPayload || layer->runtimeRetainedPayloadKey != key) {
        layer->runtimeRetainedPayload = buildTextRetainedPayload(layer);
        layer->runtimeRetainedPayloadKey = key;
    }
    layer->runtimeVisualBackend
        = layer->runtimeRetainedPayload && !layer->runtimeRetainedPayload->empty()
        ? LayerVisualBackend::RetainedSimpleForms
        : LayerVisualBackend::RasterTiles;
    return layer->runtimeRetainedPayload && !layer->runtimeRetainedPayload->empty();
}

} // namespace aether
