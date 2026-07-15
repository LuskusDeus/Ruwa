// SPDX-License-Identifier: MPL-2.0

#pragma once

#include "features/brush/color/PigmentLut.h"

namespace ruwa::core::brushes {

class PigmentLutResource final {
public:
    [[nodiscard]] static PigmentLut loadBuiltIn();
};

} // namespace ruwa::core::brushes
