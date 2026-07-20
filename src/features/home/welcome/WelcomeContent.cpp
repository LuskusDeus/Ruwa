// SPDX-License-Identifier: MPL-2.0

// WelcomeContent.cpp
#include "WelcomeContent.h"
#include "WelcomeBanner.h"
#include "RecentProjectsWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/project/ProjectSerializer.h"
#include "features/project/RecentProjectsManager.h"
#include "features/project/ThumbnailCache.h"
#include "commands/CommandExecutor.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "services/updates/UpdateManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QSettings>
#include <QResizeEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {
const int BASE_MAIN_MARGIN_H = 40;
const int BASE_MAIN_MARGIN_V = 24;
const int BASE_MAIN_SPACING = 24;
const int BASE_TITLE_FONT_SIZE = 26;

QString fallbackRecentProjectName(const QString& filePath)
{
    QFileInfo info(filePath);
    const QString baseName = info.baseName();
    return baseName.isEmpty() ? info.fileName() : baseName;
}

bool renameProjectFile(
    const QString& filePath, const QString& newProjectName, QString* errorMessage)
{
    using namespace ruwa::core::serialization;

    ProjectSerializer serializer;
    ProjectData data;
    if (!serializer.load(filePath, data)) {
        if (errorMessage) {
            *errorMessage = serializer.lastError();
        }
        return false;
    }

    const QString trimmedName = newProjectName.trimmed();
    const QString oldProjectName = data.projectName;
    data.projectName = trimmedName;
    if (data.tabTitle.isEmpty() || data.tabTitle == oldProjectName) {
        data.tabTitle = trimmedName;
    }

    if (!serializer.save(filePath, data)) {
        if (errorMessage) {
            *errorMessage = serializer.lastError();
        }
        return false;
    }

    return true;
}

void showRecentProjectMessage(QWidget* context, const QString& message)
{
    ruwa::ui::widgets::MessagePopupManager::show(context, message,
        QList<ruwa::ui::widgets::MessageButton> {
            { QCoreApplication::translate("WelcomeContent", "OK"), true, []() { } } });
}

class SectionClipWidget : public QWidget {
public:
    explicit SectionClipWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setContentsMargins(0, 0, 0, 0);
        setAutoFillBackground(false);
    }

    void setTrackedChild(QWidget* child)
    {
        m_child = child;
        if (m_child) {
            m_child->setParent(this);
            m_child->move(0, 0);
            m_child->show();

            setSizePolicy(m_child->sizePolicy());
            if (m_child->minimumHeight() > 0) {
                setMinimumHeight(m_child->minimumHeight());
            }
            if (m_child->maximumHeight() < QWIDGETSIZE_MAX) {
                setMaximumHeight(m_child->maximumHeight());
            }
            if (m_child->minimumWidth() > 0) {
                setMinimumWidth(m_child->minimumWidth());
            }
            if (m_child->maximumWidth() < QWIDGETSIZE_MAX) {
                setMaximumWidth(m_child->maximumWidth());
            }
        }
        updateChildGeometry();
    }

    QSize sizeHint() const override
    {
        if (!m_child) {
            return QWidget::sizeHint();
        }

        QSize hint = m_child->sizeHint();
        hint.setWidth(qMax(hint.width(), m_child->minimumWidth()));
        hint.setHeight(qMax(hint.height(), m_child->minimumHeight()));
        if (m_child->maximumWidth() < QWIDGETSIZE_MAX) {
            hint.setWidth(qMin(hint.width(), m_child->maximumWidth()));
        }
        if (m_child->maximumHeight() < QWIDGETSIZE_MAX) {
            hint.setHeight(qMin(hint.height(), m_child->maximumHeight()));
        }
        return hint;
    }

    QSize minimumSizeHint() const override
    {
        if (!m_child) {
            return QWidget::minimumSizeHint();
        }

        QSize hint = m_child->minimumSizeHint();
        hint.setWidth(qMax(hint.width(), m_child->minimumWidth()));
        hint.setHeight(qMax(hint.height(), m_child->minimumHeight()));
        return hint;
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), ruwa::ui::core::ThemeManager::instance().colors().background);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        updateChildGeometry();
    }

private:
    void updateChildGeometry()
    {
        if (!m_child) {
            return;
        }

        const QSize childHint = m_child->sizeHint();
        const int targetWidth = width() > 0 ? width() : childHint.width();
        int targetHeight = height() > 0 ? height() : childHint.height();
        targetHeight = qMax(targetHeight, m_child->minimumHeight());
        if (m_child->maximumHeight() < QWIDGETSIZE_MAX) {
            targetHeight = qMin(targetHeight, m_child->maximumHeight());
        }
        m_child->resize(targetWidth, targetHeight);
    }

    QWidget* m_child = nullptr;
};
} // namespace

WelcomeContent::WelcomeContent(QWidget* parent)
    : HomePageContent(parent)
{
    setupContent();

    // Connect to theme changes
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &WelcomeContent::onThemeChanged);
}

void WelcomeContent::setupContent()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // === HEADER: Title ===
    m_headerSection = new SectionClipWidget(this);
    QWidget* headerContent = new QWidget(m_headerSection);
    static_cast<SectionClipWidget*>(m_headerSection)->setTrackedChild(headerContent);
    QHBoxLayout* headerLayout = new QHBoxLayout(headerContent);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(tr("Welcome to Ruwa"), headerContent);
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    m_titleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    mainLayout->addWidget(m_headerSection);

    // === Welcome Banner (fixed height) ===
    QWidget* bannerSection = new SectionClipWidget(this);
    m_banner = new WelcomeBanner(bannerSection);
    static_cast<SectionClipWidget*>(bannerSection)->setTrackedChild(m_banner);
    connect(m_banner, &WelcomeBanner::createProjectClicked, this,
        &WelcomeContent::createProjectRequested);
    connect(
        m_banner, &WelcomeBanner::openProjectClicked, this, &WelcomeContent::openProjectRequested);
    connect(m_banner, &WelcomeBanner::updateActionRequested, this,
        &WelcomeContent::updateSettingsRequested);
    connect(
        m_banner, &WelcomeBanner::updateDismissed, this, &WelcomeContent::rememberUpdateDismissed);
    mainLayout->addWidget(bannerSection);

    // Gate the update banner on the startup version check (current vs. latest
    // GitHub release) and the dismissed state. The banner itself defers the reveal
    // until it is on screen (see WelcomeBanner::showEvent).
    auto* updateMgr = ruwa::services::UpdateManager::instance();
    connect(updateMgr, &ruwa::services::UpdateManager::updateCheckFinished, this,
        [this](bool hasUpdate, const QString&) { maybeShowUpdatePanel(hasUpdate); });
    // Re-emits the cached result if the check already ran this session, otherwise
    // kicks one off; maybeShowUpdatePanel() runs once we have an answer.
    updateMgr->checkForUpdates();

    // === Recent Projects (hosts the search bar in its own header) ===
    QWidget* recentProjectsSection = new SectionClipWidget(this);
    m_recentProjects = new RecentProjectsWidget(recentProjectsSection);
    static_cast<SectionClipWidget*>(recentProjectsSection)->setTrackedChild(m_recentProjects);
    connect(m_recentProjects, &RecentProjectsWidget::projectClicked, this,
        &WelcomeContent::projectSelected);
    connect(m_recentProjects, &RecentProjectsWidget::projectClicked, this,
        &WelcomeContent::openProjectFromRecent);
    connect(m_recentProjects, &RecentProjectsWidget::projectEditRequested, this,
        &WelcomeContent::requestRecentProjectEdit);
    connect(m_recentProjects, &RecentProjectsWidget::projectForgetRequested, this,
        [](const QString& filePath) {
            ruwa::core::serialization::RecentProjectsManager::instance().forgetEntry(filePath);
        });
    connect(m_recentProjects, &RecentProjectsWidget::projectDeleteRequested, this,
        &WelcomeContent::deleteRecentProject);
    mainLayout->addWidget(recentProjectsSection, 1);

    // Load sample projects
    loadRecentProjects();

    // Apply initial scaled sizes
    updateScaledSizes();
}

void WelcomeContent::changeEvent(QEvent* event)
{
    HomePageContent::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void WelcomeContent::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(tr("Welcome to Ruwa"));
}

void WelcomeContent::loadRecentProjects()
{
    using namespace ruwa::core::serialization;

    auto& manager = RecentProjectsManager::instance();

    // Load from persistent storage if not yet loaded
    if (manager.isEmpty()) {
        manager.load();
    }

    // Prune entries whose files no longer exist
    manager.pruneInvalid();

    // Populate widget from manager
    m_recentProjects->reloadFromManager();

    // Auto-refresh when entries change
    connect(&manager, &RecentProjectsManager::entriesChanged, this,
        [this]() { m_recentProjects->reloadFromManager(); });
}

void WelcomeContent::openProjectFromRecent(const QString& filePath)
{
    QTimer::singleShot(0, this, [filePath]() {
        ruwa::core::CommandExecutor::instance().execute("file.open", { { "path", filePath } });
    });
}

void WelcomeContent::requestRecentProjectEdit(const QString& filePath)
{
    using namespace ruwa::core::serialization;

    const RecentProjectEntry entry = RecentProjectsManager::instance().entryForPath(filePath);
    const QString projectName
        = entry.projectName.isEmpty() ? fallbackRecentProjectName(filePath) : entry.projectName;
    const bool previewEnabled = entry.isValid()
        ? entry.previewEnabled
        : RecentProjectsManager::instance().isPreviewEnabled(filePath);
    emit recentProjectEditRequested(filePath, projectName, previewEnabled);
}

void WelcomeContent::applyRecentProjectChanges(
    const QString& filePath, const QString& projectName, bool previewEnabled)
{
    using namespace ruwa::core::serialization;

    const RecentProjectEntry entry = RecentProjectsManager::instance().entryForPath(filePath);
    const QString currentName
        = entry.projectName.isEmpty() ? fallbackRecentProjectName(filePath) : entry.projectName;
    const QString trimmedName = projectName.trimmed();
    if (trimmedName.isEmpty()) {
        showRecentProjectMessage(this, tr("Project name cannot be empty."));
        return;
    }

    if (currentName != trimmedName) {
        QString errorMessage;
        if (!renameProjectFile(filePath, trimmedName, &errorMessage)) {
            showRecentProjectMessage(this, tr("Failed to rename project:\n%1").arg(errorMessage));
            return;
        }
    }

    RecentProjectsManager::instance().updateEntryMetadata(filePath, trimmedName, previewEnabled);
}

void WelcomeContent::deleteRecentProject(const QString& filePath)
{
    using namespace ruwa::core::serialization;

    const QString projectName = fallbackRecentProjectName(filePath);
    const bool confirmed = MessagePopupManager::showBlocking(this,
        tr("Delete \"%1\" from disk? This removes the project file permanently.").arg(projectName),
        tr("Delete"), tr("Cancel"), 360, false);
    if (!confirmed) {
        return;
    }

    if (!QFileInfo::exists(filePath)) {
        RecentProjectsManager::instance().removeEntry(filePath);
        ThumbnailCache::instance().remove(filePath);
        return;
    }

    QFile file(filePath);
    if (!file.remove()) {
        showRecentProjectMessage(
            this, tr("Failed to delete project file:\n%1").arg(file.errorString()));
        return;
    }

    RecentProjectsManager::instance().removeEntry(filePath);
    ThumbnailCache::instance().remove(filePath);
}

void WelcomeContent::maybeShowUpdatePanel(bool hasUpdate)
{
    if (!m_banner) {
        return;
    }

    auto* updateMgr = ruwa::services::UpdateManager::instance();
    const QString latestVersion = updateMgr->latestReleaseVersion();

    // Nothing newer than what we run, or the check yielded no tag — keep it hidden.
    if (!hasUpdate || latestVersion.isEmpty()) {
        m_banner->hideUpdatePanel();
        return;
    }

    // Respect a previous "No thanks": stay hidden until a release newer than the
    // one the user declined appears. An empty/older dismissed version still shows.
    const QString dismissedVersion
        = QSettings().value(QStringLiteral("Updates/dismissedUpdateVersion")).toString();
    if (!dismissedVersion.isEmpty()
        && !ruwa::services::UpdateManager::isVersionNewer(dismissedVersion, latestVersion)) {
        m_banner->hideUpdatePanel();
        return;
    }

    m_banner->showUpdatePanel(latestVersion, updateMgr->latestReleaseDescription());
}

void WelcomeContent::rememberUpdateDismissed()
{
    // Remember the version the user declined. The show-logic (added later) will
    // keep the banner hidden until a release newer than this appears, or until
    // settings are reset. Falls back to the running version when the latest
    // release tag isn't known yet this session.
    QString dismissedVersion = ruwa::services::UpdateManager::instance()->latestReleaseVersion();
    if (dismissedVersion.isEmpty()) {
        dismissedVersion = QApplication::applicationVersion();
    }
    QSettings().setValue(QStringLiteral("Updates/dismissedUpdateVersion"), dismissedVersion);
}

void WelcomeContent::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    // Main layout
    if (QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout())) {
        const int marginH = theme.scaled(BASE_MAIN_MARGIN_H);
        const int marginV = theme.scaled(BASE_MAIN_MARGIN_V);
        mainLayout->setContentsMargins(marginH, marginV, marginH, marginV);
        mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));
    }

    // Title font
    if (m_titleLabel) {
        m_titleLabel->setFont(
            theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    }
}

void WelcomeContent::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // Update title
    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    }
}

void WelcomeContent::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

int WelcomeContent::bannerTopY() const
{
    if (m_banner && m_banner->parentWidget()) {
        return m_banner->parentWidget()->y();
    }
    return 0;
}

void WelcomeContent::resizeEvent(QResizeEvent* event)
{
    HomePageContent::resizeEvent(event);
    QTimer::singleShot(0, this, [this]() { emit bannerTopYChanged(bannerTopY()); });
}

QList<QWidget*> WelcomeContent::startupSections() const
{
    QList<QWidget*> result;

    if (m_headerSection) {
        result.append(m_headerSection);
    }
    if (m_banner && m_banner->parentWidget()) {
        result.append(m_banner->parentWidget());
    }
    if (m_recentProjects && m_recentProjects->parentWidget()) {
        result.append(m_recentProjects->parentWidget());
    }

    return result;
}

QList<QWidget*> WelcomeContent::getAnimatableWidgets() const
{
    QList<QWidget*> result;

    // Order: top to bottom, left to right for same-level widgets
    // 1. Title (top-left of header)
    if (m_titleLabel && m_titleLabel->isVisible()) {
        result.append(m_titleLabel);
    }

    // 2. Banner (below header)
    if (m_banner && m_banner->isVisible()) {
        result.append(m_banner);
    }

    // 3. Recent projects (bottom, hosts the search bar in its header)
    if (m_recentProjects && m_recentProjects->isVisible()) {
        result.append(m_recentProjects);
    }

    return result;
}

} // namespace ruwa::ui::widgets
