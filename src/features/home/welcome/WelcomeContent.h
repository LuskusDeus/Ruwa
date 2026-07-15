// SPDX-License-Identifier: MPL-2.0

// WelcomeContent.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMECONTENT_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMECONTENT_H

#include "../HomePageContent.h"

class QLabel;
class QEvent;

namespace ruwa::ui::widgets {

class WelcomeBanner;
class RecentProjectsWidget;

/**
 * @brief Welcome/Home content panel
 *
 * Layout:
 * - Header: Title
 * - WelcomeBanner (with margins and rounded corners)
 * - RecentProjectsWidget (with internal scroll; hosts the search bar in its header)
 */
class WelcomeContent : public HomePageContent {
    Q_OBJECT

public:
    explicit WelcomeContent(QWidget* parent = nullptr);
    ~WelcomeContent() override = default;

    QString title() const override { return tr("Welcome"); }

    /// Startup sections in visual top-to-bottom order.
    QList<QWidget*> startupSections() const;

    /// Get widgets for fade-in animation (top to bottom order)
    QList<QWidget*> getAnimatableWidgets() const override;

    int bannerTopY() const;

signals:
    void createProjectRequested();
    void openProjectRequested();
    void projectSelected(const QString& filePath);
    void recentProjectEditRequested(
        const QString& filePath, const QString& projectName, bool previewEnabled);
    void bannerTopYChanged(int y);
    /// Update banner "Update" pressed — host should open the Updates settings.
    void updateSettingsRequested();

protected:
    void setupContent() override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void retranslateUi();
    void loadRecentProjects();
    void updateScaledSizes();
    void updateThemeColors();
    void openProjectFromRecent(const QString& filePath);
    void requestRecentProjectEdit(const QString& filePath);
    void deleteRecentProject(const QString& filePath);
    /// Reveal the update banner only when a newer release is available and the user
    /// hasn't already dismissed that version.
    void maybeShowUpdatePanel(bool hasUpdate);
    /// Persist that the user declined the available update ("No thanks").
    void rememberUpdateDismissed();

private slots:
    void onThemeChanged();

public slots:
    void applyRecentProjectChanges(
        const QString& filePath, const QString& projectName, bool previewEnabled);

private:
    QLabel* m_titleLabel { nullptr };
    QWidget* m_headerSection { nullptr };
    WelcomeBanner* m_banner { nullptr };
    RecentProjectsWidget* m_recentProjects { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMECONTENT_H
