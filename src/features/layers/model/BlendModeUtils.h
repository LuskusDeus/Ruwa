// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_LAYERS_BLENDMODEUTILS_H
#define RUWA_CORE_LAYERS_BLENDMODEUTILS_H

#include "features/layers/model/LayerData.h"

#include <QString>
#include <QVector>

namespace ruwa::core::layers {

inline constexpr int kBlendModeCount = static_cast<int>(BlendMode::Luminosity) + 1;

struct BlendModeCategoryDef {
    const char* label;
    QVector<BlendMode> modes;
};

bool isValidBlendModeValue(int value);
BlendMode blendModeFromValue(int value, BlendMode fallback = BlendMode::Normal);
QString blendModeDisplayName(BlendMode mode, const char* context);
const QVector<BlendModeCategoryDef>& blendModeCategoryDefs();

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_BLENDMODEUTILS_H
