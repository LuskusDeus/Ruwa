// SPDX-License-Identifier: MPL-2.0

// RecentProjectsWidget.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTSWIDGET_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTSWIDGET_H

#include <QWidget>
#include <QVector>
#include <QString>
#include <QPixmap>

class QEvent;
class QVBoxLayout;
class QGridLayout;
class QLabel;
class QHBoxLayout;

namespace ruwa::ui::widgets {

class RecentProjectItem;
class RecentProjectCard;
class SegmentedOptionSelector;
class SmoothScrollArea;
class AnimatedViewSwitcher;
class SearchBar;

/**
 * @brief Widget displaying recent projects as list or grid
 *
 * Features:
 * - Two view modes: List and Grid
 * - Animated swipe transition between modes
 * - Header: title, search bar (fills remaining width), view selector
 * - Smooth scrolling if many projects
 * - DPI-aware scaling
 */
class RecentProjectsWidget : public QWidget {
    Q_OBJECT

public:
    enum class ViewMode { List, Grid };

    explicit RecentProjectsWidget(QWidget* parent = nullptr);
    ~RecentProjectsWidget() override = default;

    /// Add a recent project
    void addProject(const QString& name, const QString& filePath, const QString& lastModified,
        const QPixmap& thumbnail = QPixmap());

    /// Clear all projects
    void clearProjects();

    /// Reload projects from RecentProjectsManager
    void reloadFromManager();

    /// Filter projects by search query
    void filterProjects(const QString& query);

    /// Set view mode (with animation)
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_viewMode; }

signals:
    void projectClicked(const QString& filePath);
    void projectEditRequested(const QString& filePath);
    void projectForgetRequested(const QString& filePath);
    void projectDeleteRequested(const QString& filePath);

protected:
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void retranslateUi();
    void updateEmptyStateMinHeight();
    struct ProjectData {
        QString name;
        QString filePath;
        QString lastModified;
        QString fileSize;
        QPixmap thumbnail;
        QPixmap icon;
        bool previewEnabled = true;
    };

    void setupUI();
    void rebuildListView();
    void rebuildGridView();
    void updateScaledSizes();
    void updateThemeColors();
    QVector<ProjectData> getFilteredData() const;

private slots:
    void onThemeChanged();
    void onViewModeChanged(ViewMode mode);

private:
    QVBoxLayout* m_mainLayout { nullptr };
    QWidget* m_headerWidget { nullptr };
    QLabel* m_titleLabel { nullptr };
    SearchBar* m_searchBar { nullptr };
    SegmentedOptionSelector* m_viewSelector { nullptr };

    // Animated view switcher
    AnimatedViewSwitcher* m_viewSwitcher { nullptr };

    // List view components
    SmoothScrollArea* m_listScrollArea { nullptr };
    QWidget* m_listContent { nullptr };
    QVBoxLayout* m_listLayout { nullptr };
    QVector<RecentProjectItem*> m_projectItems;

    // Grid view components
    SmoothScrollArea* m_gridScrollArea { nullptr };
    QWidget* m_gridContent { nullptr };
    QGridLayout* m_gridLayout { nullptr };
    QVector<RecentProjectCard*> m_projectCards;

    // Data
    QVector<ProjectData> m_projectsData;

    ViewMode m_viewMode { ViewMode::List };
    QString m_searchQuery;

    static constexpr int GRID_COLUMNS = 4;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTSWIDGET_H
