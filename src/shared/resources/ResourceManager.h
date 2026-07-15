// SPDX-License-Identifier: MPL-2.0

// ResourceManager.h
#ifndef RUWA_UI_CORE_RESOURCES_RESOURCEMANAGER_H
#define RUWA_UI_CORE_RESOURCES_RESOURCEMANAGER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QIcon>
#include <QPixmap>
#include <functional>

namespace ruwa::ui::core {

/**
 * @brief Central manager for application resources
 *
 * Provides centralized access to:
 * - Icons (with theme variants)
 * - Images
 * - Resource paths
 * - Resource validation
 */
class ResourceManager : public QObject {
    Q_OBJECT

public:
    /// Resource categories
    enum class Category { Icons, Images, Fonts, Styles, Brushes };

    /// Named built-in UI images used outside feature code.
    enum class BuiltInImage { WelcomeBanner, WelcomeBannerAlt, UpdateBanner, UpdateMessageBanner };

    static ResourceManager& instance();

    /// Initialize resource system
    void initialize();

    /// Get resource path by category and name
    QString getResourcePath(Category category, const QString& name) const;

    /// Check if resource exists
    bool resourceExists(Category category, const QString& name) const;

    /// Get all resources in a category
    QStringList getResourceList(Category category) const;

    /// Resolve a built-in UI image to its resource path.
    QString getBuiltInImagePath(BuiltInImage image) const;

    /// Built-in banner images available on the welcome screen.
    QStringList getWelcomeBannerBuiltinImageKeys() const;

    /// Default fixed banner key for welcome screen settings.
    QString defaultWelcomeBannerKey() const;

signals:
    /// Emitted when resources are reloaded
    void resourcesReloaded();

private:
    ResourceManager();
    ~ResourceManager() override;

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    QString getCategoryPath(Category category) const;
    void scanResources();

private:
    QMap<Category, QStringList> m_resourceCache;
    bool m_initialized { false };
};

} // namespace ruwa::ui::core

#endif // RUWA_UI_CORE_RESOURCES_RESOURCEMANAGER_H
