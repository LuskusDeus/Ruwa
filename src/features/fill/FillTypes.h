// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_FILL_FILLTYPES_H
#define RUWA_CORE_FILL_FILLTYPES_H

#include <QUuid>
#include <cstdint>

namespace aether {

struct FillOrigin {
    int x = 0;
    int y = 0;
};

struct FillColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

struct FillCanvasBounds {
    int workOriginX = 0;
    int workOriginY = 0;
    int width = 0;
    int height = 0;
};

} // namespace aether

#endif // RUWA_CORE_FILL_FILLTYPES_H
