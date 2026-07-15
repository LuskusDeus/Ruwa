// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerImageCatalog.cpp
#include "WelcomeBannerImageCatalog.h"
#include "shared/resources/ResourceAssetCatalog.h"

namespace ruwa::ui::widgets {

QStringList welcomeBannerBuiltinImageKeys()
{
    return ruwa::ui::core::assets::images::welcomeBannerBuiltInPaths();
}

QString welcomeBannerDefaultFixedKey()
{
    return ruwa::ui::core::assets::images::defaultWelcomeBannerPath();
}

} // namespace ruwa::ui::widgets
