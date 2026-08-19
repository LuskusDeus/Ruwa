// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_CORE_THEME_THEMEPRESETJSON_H
#define RUWA_UI_CORE_THEME_THEMEPRESETJSON_H

#include "features/theme/manager/ThemePreset.h"

#include <QJsonObject>

namespace ruwa::ui::core::theme_preset_json {

QJsonObject toDocumentObject(const ThemePreset& preset);
bool fromDocumentObject(
    const QJsonObject& document, ThemePreset& preset, QString* errorMessage = nullptr);

} // namespace ruwa::ui::core::theme_preset_json

#endif // RUWA_UI_CORE_THEME_THEMEPRESETJSON_H
