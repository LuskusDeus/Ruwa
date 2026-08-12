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

    static void paint(QPainter& painter, const QRectF& availableRect,
        const QKeySequence& shortcut, Qt::Alignment alignment, SizeVariant sizeVariant,
        const QColor& textColor = QColor(), bool emphasized = false)
    {
        paintLayout(painter, availableRect, layoutFor(shortcut, sizeVariant), alignment,
            textColor, emphasized);
    }

    static void paint(QPainter& painter, const QRectF& availableRect, const QString& shortcutText,
        Qt::Alignment alignment, SizeVariant sizeVariant, const QColor& textColor = QColor(),
        bool emphasized = false)
    {
        paintLayout(painter, availableRect, layoutFor(shortcutText, sizeVariant), alignment,
            textColor, emphasized);
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

    static QKeySequence sequenceFromText(const QString& shortcutText)
    {
        QKeySequence shortcut = QKeySequence::fromString(shortcutText, QKeySequence::NativeText);
        if (shortcut.isEmpty()) {
            shortcut = QKeySequence::fromString(shortcutText, QKeySequence::PortableText);
        }
        return shortcut;
    }

    static Layout baseLayout(SizeVariant sizeVariant)
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        const bool compact = sizeVariant == SizeVariant::Compact;

        Layout layout;
        layout.font = colors.fonts.getCodeFont(theme.scaledFontSize(compact ? 8 : 10));
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
            const int padding = theme.scaled(compact ? 6 : 10);
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

            const QKeyCombination chord = shortcut[chordIndex];
            bool hasPart = false;
            const Qt::KeyboardModifiers modifiers = chord.keyboardModifiers();
            for (const Qt::KeyboardModifier modifier : modifierOrder) {
                if (!modifiers.testFlag(modifier)) {
                    continue;
                }
                const QString modifierText = nativeModifierText(modifier);
                if (modifierText.isEmpty()) {
                    continue;
                }
                if (hasPart) {
                    appendPart(layout, QStringLiteral("+"), false, metrics, sizeVariant);
                }
                appendPart(layout, modifierText, true, metrics, sizeVariant);
                hasPart = true;
            }

            const QString keyText = nativeKeyText(chord.key());
            if (!keyText.isEmpty()) {
                if (hasPart) {
                    appendPart(layout, QStringLiteral("+"), false, metrics, sizeVariant);
                }
                appendPart(layout, keyText, true, metrics, sizeVariant);
            }
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

        Layout layout = baseLayout(sizeVariant);
        if (!shortcutText.trimmed().isEmpty()) {
            appendPart(layout, shortcutText.trimmed(), true, QFontMetricsF(layout.font), sizeVariant);
        }
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

        const QColor keyFill = emphasized
            ? ruwa::ui::core::ThemeColors::withAlpha(colors.primary, colors.isDark ? 32 : 44)
            : ruwa::ui::core::ThemeColors::interpolate(
                  colors.overlayBase(), colors.overlayHover(), 0.75);
        QColor keyBorderTop = emphasized
            ? ruwa::ui::core::ThemeColors::withAlpha(colors.primary, 132)
            : ruwa::ui::core::ThemeColors::interpolate(
                  colors.borderSubtle(), colors.borderSubtleHover(), 0.25);
        QColor keyBorderBottom = keyBorderTop;
        keyBorderBottom.setAlpha(qRound(keyBorderBottom.alpha() * 0.5));
        QColor separatorColor
            = ruwa::ui::core::ThemeColors::interpolate(keyBorderBottom, keyBorderTop, 0.7);
        separatorColor.setAlpha(qMax(separatorColor.alpha(), colors.isDark ? 128 : 112));

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
