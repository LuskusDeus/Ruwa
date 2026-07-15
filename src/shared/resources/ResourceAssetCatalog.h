// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_CORE_RESOURCES_RESOURCEASSETCATALOG_H
#define RUWA_UI_CORE_RESOURCES_RESOURCEASSETCATALOG_H

#include <QString>
#include <QStringList>

namespace ruwa::ui::core::assets {

namespace icons {

inline constexpr auto OpaqueLogo = "OpaqueLogoIcon";
inline constexpr auto TransparentLogo = "TransparentLogoIcon";
inline constexpr auto DefaultWorkspaceTab = "Brush";

} // namespace icons

namespace images {

inline constexpr auto WelcomeBannerPrimary = "WelcomeBanner";
inline constexpr auto WelcomeBannerSecondary = "WelcomeBanner2";
inline constexpr auto UpdateBanner = "UpdateBanner";
inline constexpr auto UpdateMessageBanner = "UpdateMessageBanner";
inline constexpr auto BannerApril = "Banner1April";

inline QString defaultWelcomeBannerPath()
{
    return QStringLiteral(":/images/WelcomeBanner");
}

inline QStringList welcomeBannerBuiltInPaths()
{
    return {
        QStringLiteral(":/images/WelcomeBanner"),
        QStringLiteral(":/images/WelcomeBanner2"),
        QStringLiteral(":/images/Banner1April"),
    };
}

} // namespace images

} // namespace ruwa::ui::core::assets

#endif // RUWA_UI_CORE_RESOURCES_RESOURCEASSETCATALOG_H
