// SPDX-License-Identifier: MPL-2.0

// ShortcutKeycapRenderer.h
// Shared rendering and layout for keyboard shortcut keycaps.
#ifndef RUWA_UI_WIDGETS_SHORTCUTKEYCAPRENDERER_H
#define RUWA_UI_WIDGETS_SHORTCUTKEYCAPRENDERER_H

#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QFontMetricsF>
#include <QFont>
#include <QKeyCombination>
#include <QKeySequence>
#include <QPainter>
#include <QSizeF>
#include <QVector>
#include <QtMath>

#include <array>

namespace ruwa::ui::widgets {

/**
 * Shared visual representation of a keyboard shortcut.
 *
 * The renderer intentionally has no QWidget lifetime or input handling: shortcut
 * hints are also painted by delegates and popup widgets. Keeping their layout
 * and paint code here lets every surface use the same keycap treatment.
 */
class ShortcutKeycapRenderer final {
public:
    enum class SizeVariant { Compact, Regular };

    static QSizeF contentSize(const QKeySequence& shortcut, SizeVariant sizeVariant)
    {
        return layoutFor(shortcut, sizeVariant).size;
    }

    static QSizeF contentSize(const QString& shortcutText, SizeVariant sizeVariant)
    {
        return layoutFor(shortcutText, sizeVariant).size;
    }

    static void paint(QPainter& painter, const QRectF& availableRect, const QKeySequence& shortcut,
        Qt::Alignment alignment, SizeVariant sizeVariant, const QColor& textColor = QColor(),
        bool emphasized = false)
    {
        paintLayout(painter, availableRect, layoutFor(shortcut, sizeVariant), alignment, textColor,
            emphasized);
    }

    static void paint(QPainter& painter, const QRectF& availableRect, const QString& shortcutText,
        Qt::Alignment alignment, SizeVariant sizeVariant, const QColor& textColor = QColor(),
        bool emphasized = false)
    {
        paintLayout(painter, availableRect, layoutFor(shortcutText, sizeVariant), alignment,
            textColor, emphasized);
    }

    /**
     * Size and paint for a caption shown in place of keys ("Click to assign",
     * "Press shortcut..."). Bypasses shortcut parsing: QKeySequence::fromString()
     * happily turns arbitrary prose into a single Qt::Key_unknown chord, which is
     * not empty but renders to nothing, so a parsed caption would silently vanish.
     */
    static QSizeF labelSize(const QString& text, SizeVariant sizeVariant)
    {
        return labelLayout(text, sizeVariant).size;
    }

    static void paintLabel(QPainter& painter, const QRectF& availableRect, const QString& text,
        Qt::Alignment alignment, SizeVariant sizeVariant, const QColor& textColor = QColor(),
        bool emphasized = false)
    {
        paintLayout(painter, availableRect, labelLayout(text, sizeVariant), alignment, textColor,
            emphasized);
    }

    /// Whether @p shortcut has anything to draw: a stored sequence can carry
    /// Qt::Key_unknown, which yields no keycaps at all.
    static bool isRenderable(const QKeySequence& shortcut)
    {
        if (shortcut.isEmpty()) {
            return false;
        }
        for (int chordIndex = 0; chordIndex < shortcut.count(); ++chordIndex) {
            const QKeyCombination chord = shortcut[chordIndex];
            if (chord.key() == Qt::Key_unknown || nativeKeyText(chord.key()).isEmpty()) {
                return false;
            }
        }
        return true;
    }

private:
    struct Part {
        QString text;
        qreal width = 0.0;
        bool keycap = false;
    };

    struct Layout {
        QVector<Part> parts;
        QSizeF size;
        QFont font;
        qreal spacing = 0.0;
        qreal keycapHeight = 0.0;
        qreal keycapRadius = 0.0;
        qreal keycapDepth = 0.0;
    };

    static QString nativeKeyText(Qt::Key key)
    {
        return QKeySequence(QKeyCombination(Qt::NoModifier, key))
            .toString(QKeySequence::NativeText)
            .trimmed();
    }

    static QString nativeModifierText(Qt::KeyboardModifier modifier)
    {
        const QString sentinelKey = nativeKeyText(Qt::Key_F24);
        QString text = QKeySequence(QKeyCombination(modifier, Qt::Key_F24))
                           .toString(QKeySequence::NativeText)
                           .trimmed();
        if (!sentinelKey.isEmpty() && text.endsWith(sentinelKey)) {
            text.chop(sentinelKey.size());
        }
        while (text.endsWith(QLatin1Char('+'))) {
            text.chop(1);
        }
        return text.trimmed();
    }

    /// Parses @p shortcutText, or returns an empty sequence when it is not a
    /// shortcut at all. isRenderable() rather than isEmpty() decides that: an
    /// unparsable string comes back as a non-empty Qt::Key_unknown chord.
    static QKeySequence sequenceFromText(const QString& shortcutText)
    {
        QKeySequence shortcut = QKeySequence::fromString(shortcutText, QKeySequence::NativeText);
        if (!isRenderable(shortcut)) {
            shortcut = QKeySequence::fromString(shortcutText, QKeySequence::PortableText);
        }
        return isRenderable(shortcut) ? shortcut : QKeySequence();
    }

    static Layout baseLayout(SizeVariant sizeVariant)
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        const bool compact = sizeVariant == SizeVariant::Compact;

        Layout layout;
        const auto sizeRole
            = compact ? ruwa::ui::core::ThemeFontRole::Small : ruwa::ui::core::ThemeFontRole::Label;
        layout.font
            = colors.fonts.getFont(ruwa::ui::core::ThemeFontRole::Code, theme.fontSize(sizeRole));
        layout.font.setWeight(QFont::DemiBold);
        layout.spacing = theme.scaled(compact ? 3 : 5);
        layout.keycapHeight = theme.scaled(compact ? 21 : 28);
        layout.keycapRadius = theme.scaled(compact ? 5 : 6);
        layout.keycapDepth = theme.scaled(compact ? 3 : 4);
        return layout;
    }

    static void appendPart(Layout& layout, const QString& text, bool keycap,
        const QFontMetricsF& metrics, SizeVariant sizeVariant)
    {
        if (text.isEmpty()) {
            return;
        }

        qreal width = metrics.horizontalAdvance(text);
        if (keycap) {
            const auto& theme = ruwa::ui::core::ThemeManager::instance();
            const bool compact = sizeVariant == SizeVariant::Compact;
            const int minimumWidth = theme.scaled(compact ? 21 : 28);
            const int padding = theme.scaled(compact ? 5 : 8);
            width = qMax<qreal>(minimumWidth, width + padding * 2);
        }
        layout.parts.append({ text, width, keycap });
    }

    static void finishLayout(Layout& layout)
    {
        qreal width = 0.0;
        for (const Part& part : layout.parts) {
            if (width > 0.0) {
                width += layout.spacing;
            }
            width += part.width;
        }
        layout.size = layout.parts.isEmpty() ? QSizeF() : QSizeF(width, layout.keycapHeight);
    }

    static Layout layoutFor(const QKeySequence& shortcut, SizeVariant sizeVariant)
    {
        Layout layout = baseLayout(sizeVariant);
        const QFontMetricsF metrics(layout.font);
        constexpr std::array modifierOrder { Qt::ControlModifier, Qt::AltModifier,
            Qt::ShiftModifier, Qt::MetaModifier, Qt::KeypadModifier, Qt::GroupSwitchModifier };

        for (int chordIndex = 0; chordIndex < shortcut.count(); ++chordIndex) {
            if (chordIndex > 0) {
                appendPart(layout, QStringLiteral(","), false, metrics, sizeVariant);
            }

            // Keys inside one chord are separated by the gap between caps alone: a "+"
            // glyph plus its two gaps costs more width than it carries meaning.
            const QKeyCombination chord = shortcut[chordIndex];
            const Qt::KeyboardModifiers modifiers = chord.keyboardModifiers();
            for (const Qt::KeyboardModifier modifier : modifierOrder) {
                if (!modifiers.testFlag(modifier)) {
                    continue;
                }
                appendPart(layout, nativeModifierText(modifier), true, metrics, sizeVariant);
            }

            appendPart(layout, nativeKeyText(chord.key()), true, metrics, sizeVariant);
        }

        finishLayout(layout);
        return layout;
    }

    static Layout layoutFor(const QString& shortcutText, SizeVariant sizeVariant)
    {
        const QKeySequence shortcut = sequenceFromText(shortcutText);
        if (!shortcut.isEmpty()) {
            return layoutFor(shortcut, sizeVariant);
        }
        return labelLayout(shortcutText, sizeVariant);
    }

    /// One keycap holding @p text verbatim.
    static Layout labelLayout(const QString& text, SizeVariant sizeVariant)
    {
        Layout layout = baseLayout(sizeVariant);
        appendPart(layout, text.trimmed(), true, QFontMetricsF(layout.font), sizeVariant);
        finishLayout(layout);
        return layout;
    }

    static void paintLayout(QPainter& painter, const QRectF& availableRect, const Layout& layout,
        Qt::Alignment alignment, const QColor& requestedTextColor, bool emphasized)
    {
        if (layout.parts.isEmpty() || !availableRect.isValid()) {
            return;
        }

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        const QColor textColor = requestedTextColor.isValid() ? requestedTextColor : colors.text;

        qreal x = availableRect.left();
        if (alignment.testFlag(Qt::AlignRight)) {
            x = availableRect.right() - layout.size.width();
        } else if (alignment.testFlag(Qt::AlignHCenter)) {
            x = availableRect.center().x() - layout.size.width() / 2.0;
        }

        qreal y = availableRect.top();
        if (alignment.testFlag(Qt::AlignBottom)) {
            y = availableRect.bottom() - layout.size.height();
        } else if (alignment.testFlag(Qt::AlignVCenter)) {
            y = availableRect.center().y() - layout.size.height() / 2.0;
        }

        // Opaque palette colors rather than a white overlay: the cap sits a step below
        // the surface it is painted on. surfaceAlt is the darkest step still ABOVE the
        // window background — anchoring lower turns the cap into a hole on presets like
        // Obsidian, whose background is already 10/10/10.
        using Colors = ruwa::ui::core::ThemeColors;
        QColor keyFill
            = colors.isDark ? colors.surfaceAlt : Colors::adjustBrightness(colors.surface, 0.96);
        if (emphasized) {
            keyFill = Colors::interpolate(keyFill, colors.primary, colors.isDark ? 0.16 : 0.12);
        }
        keyFill.setAlpha(255);

        // Dark themes lift the top rim towards the text color instead of scaling
        // brightness: a near-black fill scales to itself and leaves the cap edgeless.
        QColor keyBorderTop = emphasized
            ? Colors::interpolate(keyFill, colors.primary, colors.isDark ? 0.45 : 0.35)
            : (colors.isDark ? Colors::interpolate(keyFill, colors.text, 0.20)
                             : Colors::adjustBrightness(keyFill, 0.86));
        QColor keyBorderBottom = colors.isDark
            ? Colors::interpolate(keyFill, colors.background, 0.55)
            : Colors::adjustBrightness(keyFill, 0.86);
        keyBorderTop.setAlpha(255);
        keyBorderBottom.setAlpha(255);

        const QColor separatorColor = Colors::withAlpha(colors.textMuted, 150);

        painter.save();
        painter.setFont(layout.font);
        for (const Part& part : layout.parts) {
            const QRectF partRect(x, y, part.width, layout.keycapHeight);
            if (part.keycap) {
                ruwa::ui::painting::drawKeycapFrame(painter, partRect, layout.keycapRadius,
                    layout.keycapDepth, keyFill, keyBorderTop, keyBorderBottom);
                painter.setPen(textColor);
                painter.drawText(partRect.adjusted(0.0, 0.0, 0.0, -layout.keycapDepth),
                    Qt::AlignCenter, part.text);
            } else {
                painter.setPen(separatorColor);
                painter.drawText(partRect, Qt::AlignCenter, part.text);
            }
            x += part.width + layout.spacing;
        }
        painter.restore();
    }
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SHORTCUTKEYCAPRENDERER_H
