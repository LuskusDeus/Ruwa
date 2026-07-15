// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_CORE_DISPLAYCOLORPALETTE_H
#define RUWA_UI_CORE_DISPLAYCOLORPALETTE_H

#include <QColor>
#include <QtGlobal>
#include <array>

namespace ruwa::ui::core {

inline const std::array<QColor, 9>& displayColorPalette()
{
    static const std::array<QColor, 9> colors = {
        QColor(140, 140, 146),
        QColor(231, 92, 89),
        QColor(241, 140, 70),
        QColor(230, 186, 76),
        QColor(102, 192, 102),
        QColor(69, 184, 170),
        QColor(82, 149, 245),
        QColor(134, 109, 240),
        QColor(210, 103, 191),
    };
    return colors;
}

inline int maxDisplayColorIndex()
{
    return static_cast<int>(displayColorPalette().size()) - 1;
}

inline QColor displayAccentColor(int colorIndex)
{
    colorIndex = qBound(0, colorIndex, maxDisplayColorIndex());
    if (colorIndex <= 0) {
        return {};
    }

    return displayColorPalette()[static_cast<size_t>(colorIndex)];
}

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_DISPLAYCOLORPALETTE_H
