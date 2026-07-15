// SPDX-License-Identifier: MPL-2.0

// ResourceManager.cpp
#include "ResourceManager.h"
#include "ResourceAssetCatalog.h"

#include <QDir>
#include <QDirIterator>
namespace ruwa::ui::core {

ResourceManager::ResourceManager()
    : QObject(nullptr)
{
}

ResourceManager::~ResourceManager() = default;

ResourceManager& ResourceManager::instance()
{
    static ResourceManager instance;
    return instance;
}

void ResourceManager::initialize()
{
    if (m_initialized) {
        return;
    }

    scanResources();
    m_initialized = true;
}

QString ResourceManager::getResourcePath(Category category, const QString& name) const
{
    QString basePath = getCategoryPath(category);
    return QString("%1/%2").arg(basePath, name);
}

bool ResourceManager::resourceExists(Category category, const QString& name) const
{
    if (!m_resourceCache.contains(category)) {
        return false;
    }

    return m_resourceCache[category].contains(name);
}

QStringList ResourceManager::getResourceList(Category category) const
{
    if (!m_resourceCache.contains(category)) {
        return QStringList();
    }

    return m_resourceCache[category];
}

QString ResourceManager::getBuiltInImagePath(BuiltInImage image) const
{
    switch (image) {
    case BuiltInImage::WelcomeBanner:
        return getResourcePath(Category::Images, assets::images::WelcomeBannerPrimary);
    case BuiltInImage::WelcomeBannerAlt:
        return getResourcePath(Category::Images, assets::images::WelcomeBannerSecondary);
    case BuiltInImage::UpdateBanner:
        return getResourcePath(Category::Images, assets::images::UpdateBanner);
    case BuiltInImage::UpdateMessageBanner:
        return getResourcePath(Category::Images, assets::images::UpdateMessageBanner);
    }

    return QString();
}

QStringList ResourceManager::getWelcomeBannerBuiltinImageKeys() const
{
    return assets::images::welcomeBannerBuiltInPaths();
}

QString ResourceManager::defaultWelcomeBannerKey() const
{
    return assets::images::defaultWelcomeBannerPath();
}

QString ResourceManager::getCategoryPath(Category category) const
{
    switch (category) {
    case Category::Icons:
        return ":/icons";
    case Category::Images:
        return ":/images";
    case Category::Fonts:
        return ":/fonts";
    case Category::Styles:
        return ":/styles";
    case Category::Brushes:
        return ":/brushes";
    default:
        return ":/";
    }
}

void ResourceManager::scanResources()
{
    m_resourceCache.clear();

    // Scan each category
    const QList<Category> categories = { Category::Icons, Category::Images, Category::Fonts,
        Category::Styles, Category::Brushes };

    for (Category category : categories) {
        QString path = getCategoryPath(category);
        QDir dir(path);

        if (!dir.exists()) {
            continue;
        }

        QStringList resources;
        QDirIterator it(path, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString filePath = it.next();
            QFileInfo fileInfo(filePath);

            if (fileInfo.isFile()) {
                // Store relative path from category root
                QString relativePath = filePath.mid(path.length() + 1);
                resources.append(relativePath);
            }
        }

        m_resourceCache[category] = resources;
    }
}

} // namespace ruwa::ui::core
