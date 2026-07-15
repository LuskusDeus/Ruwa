// SPDX-License-Identifier: MPL-2.0

// HomePageTab.cpp
#include "HomePageTab.h"
#include "features/home/HomePageSidebar.h"
#include "features/home/about/AboutContent.h"
#include "features/first-run-integration/FirstRunIntegrationWidget.h"
#include "features/home/welcome/RecentProjectEditOverlay.h"
#include "features/home/welcome/WelcomeContent.h"
#include "features/home/new-project/NewProjectContent.h"
#include "features/settings/SettingsContent.h"
#include "features/settings/SettingsManager.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "commands/CommandExecutor.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>

namespace ruwa::ui::tabs {

namespace {

int contentSideMarginForSize(const QSize& size)
{
    const float targetAspectRatio = 14.0f / 9.0f;
    const int targetWidth = static_cast<int>(size.height() * targetAspectRatio);
    if (size.width() <= targetWidth) {
        return 0;
    }

    return (size.width() - targetWidth) / 2;
}

class SidebarClipWidget : public QWidget {
public:
    explicit SidebarClipWidget(QWidget* parent = nullptr)
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
            setFixedWidth(m_child->width() > 0 ? m_child->width() : m_child->sizeHint().width());
        }
        updateChildGeometry();
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

        const int childWidth
            = m_child->width() > 0 ? m_child->width() : m_child->sizeHint().width();
        m_child->resize(childWidth, height());
        if (width() <= 0 && childWidth > 0) {
            setFixedWidth(childWidth);
        }
    }

    QWidget* m_child = nullptr;
};

} // namespace

HomePageTab::HomePageTab(QWidget* parent)
    : ruwa::core::BaseTab(parent)
    , m_theme(this)
    , m_firstRunCompleted(ruwa::core::SettingsManager::instance().isFirstRunIntegrationCompleted())
{
    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::firstRunIntegrationCompletedChanged, this,
        &HomePageTab::onFirstRunCompletedChanged);
}

HomePageTab::~HomePageTab() { }

void HomePageTab::resizeEvent(QResizeEvent* event)
{
    ruwa::core::BaseTab::resizeEvent(event);
    updateContentSideMargins();
}

void HomePageTab::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    // Боковые отступы (letterbox) — цвет background, а не surface
    QPainter painter(this);
    painter.fillRect(rect(), ruwa::ui::core::ThemeManager::instance().colors().background);
}

QIcon HomePageTab::icon() const
{
    // TODO: Return home page icon
    return QIcon();
}

void HomePageTab::onInitialize()
{
    setupUI();
}

void HomePageTab::setupUI()
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_rootStack = new ruwa::ui::widgets::AnimatedStackedWidget(this);
    m_rootStack->setAnimationDuration(350);
    m_rootStack->setAnimationEasing(QEasingCurve::InOutCubic);
    mainLayout->addWidget(m_rootStack);

    m_firstRunContainer = new QWidget(this);
    auto* firstRunLayout = new QVBoxLayout(m_firstRunContainer);
    firstRunLayout->setContentsMargins(0, 0, 0, 0);
    firstRunLayout->setSpacing(0);

    m_firstRunIntegration
        = new ruwa::ui::first_run_integration::FirstRunIntegrationWidget(m_firstRunContainer);
    firstRunLayout->addWidget(m_firstRunIntegration);

    connect(m_firstRunIntegration,
        &ruwa::ui::first_run_integration::FirstRunIntegrationWidget::completedRequested, this,
        &HomePageTab::onFirstRunCompletedRequested);
    connect(m_firstRunIntegration,
        &ruwa::ui::first_run_integration::FirstRunIntegrationWidget::customizeRequested, this,
        &HomePageTab::onFirstRunCustomizeRequested);

    m_homeContainer = new QWidget(this);
    setupHomeContent();

    m_rootStack->addWidget(m_firstRunContainer);
    m_rootStack->addWidget(m_homeContainer);
    updateContentSideMargins();
    updateFirstRunPageState();
}

void HomePageTab::updateContentSideMargins()
{
    const int sideMargin = contentSideMarginForSize(size());

    if (m_homeContainer && m_homeContainer->layout()) {
        m_homeContainer->layout()->setContentsMargins(sideMargin, 0, sideMargin, 0);
    }

    if (m_firstRunIntegration) {
        m_firstRunIntegration->setContentSideMargin(sideMargin);
    }
}

void HomePageTab::setupHomeContent()
{
    auto* homeLayout = new QHBoxLayout(m_homeContainer);
    homeLayout->setContentsMargins(0, 0, 0, 0);
    homeLayout->setSpacing(0);

    m_sidebarClip = new SidebarClipWidget(m_homeContainer);
    m_sidebar = new ruwa::ui::widgets::HomePageSidebar(m_sidebarClip);
    static_cast<SidebarClipWidget*>(m_sidebarClip)->setTrackedChild(m_sidebar);
    const int sidebarWidth = m_sidebar->width() > 0
        ? m_sidebar->width()
        : qMax(m_sidebar->minimumWidth(), m_sidebar->sizeHint().width());
    m_sidebarClip->setFixedWidth(sidebarWidth);
    homeLayout->addWidget(m_sidebarClip);

    m_contentStack = new ruwa::ui::widgets::AnimatedStackedWidget(m_homeContainer);
    m_contentStack->setAnimationDuration(350);
    m_contentStack->setAnimationEasing(QEasingCurve::InOutCubic);
    homeLayout->addWidget(m_contentStack);

    m_welcomeContent = new ruwa::ui::widgets::WelcomeContent(m_homeContainer);
    m_newProjectContent = new ruwa::ui::widgets::NewProjectContent(m_homeContainer);
    m_settingsContent = new ruwa::ui::widgets::SettingsContent(m_homeContainer);
    m_aboutContent = new ruwa::ui::widgets::AboutContent(m_homeContainer);
    m_recentProjectEditOverlay = new ruwa::ui::widgets::RecentProjectEditOverlay(this);
    m_recentProjectEditOverlay->hide();

    m_contentStack->addWidget(m_welcomeContent);
    m_contentStack->addWidget(m_newProjectContent);
    m_contentStack->addWidget(m_settingsContent);
    m_contentStack->addWidget(m_aboutContent);

    connect(m_sidebar, &ruwa::ui::widgets::HomePageSidebar::sectionChanged, this,
        &HomePageTab::onSectionChanged);

    connect(m_welcomeContent, &ruwa::ui::widgets::WelcomeContent::bannerTopYChanged, m_sidebar,
        &ruwa::ui::widgets::HomePageSidebar::setTopMarginPx);

    connect(m_welcomeContent, &ruwa::ui::widgets::WelcomeContent::createProjectRequested, this,
        [this]() {
            m_sidebar->setActiveSection(ruwa::ui::widgets::HomePageSidebar::Section::NewProject);
        });
    connect(m_welcomeContent, &ruwa::ui::widgets::WelcomeContent::openProjectRequested, this,
        []() { ruwa::core::CommandExecutor::instance().execute("file.open", {}); });
    connect(m_welcomeContent, &ruwa::ui::widgets::WelcomeContent::updateSettingsRequested, this,
        [this]() {
            m_sidebar->setActiveSection(ruwa::ui::widgets::HomePageSidebar::Section::Settings);
        });
    connect(m_welcomeContent, &ruwa::ui::widgets::WelcomeContent::recentProjectEditRequested, this,
        [this](const QString& filePath, const QString& projectName, bool previewEnabled) {
            if (!m_recentProjectEditOverlay) {
                return;
            }
            m_recentProjectEditOverlay->showForProject(filePath, projectName, previewEnabled);
        });
    connect(m_recentProjectEditOverlay, &ruwa::ui::widgets::RecentProjectEditOverlay::saveRequested,
        m_welcomeContent, &ruwa::ui::widgets::WelcomeContent::applyRecentProjectChanges);

    connect(m_newProjectContent, &ruwa::ui::widgets::NewProjectContent::projectCreateRequested,
        this, &HomePageTab::projectCreateRequested);
    connect(m_newProjectContent, &ruwa::ui::widgets::NewProjectContent::colorPickerRequested, this,
        &HomePageTab::colorPickerRequested);

    connect(m_settingsContent, &ruwa::ui::widgets::SettingsContent::customThemesRequested, this,
        &HomePageTab::themeEditorRequested);
    connect(m_settingsContent, &ruwa::ui::widgets::SettingsContent::shortcutManagerRequested, this,
        &HomePageTab::shortcutManagerRequested);

    m_contentStack->setCurrentWidget(m_welcomeContent);
    m_sidebar->setActiveSection(ruwa::ui::widgets::HomePageSidebar::Section::Home);
}

void HomePageTab::updateFirstRunPageState()
{
    if (!m_rootStack) {
        return;
    }

    m_rootStack->setCurrentWidget(m_firstRunCompleted ? m_homeContainer : m_firstRunContainer);
}

void HomePageTab::onSectionChanged(ruwa::ui::widgets::HomePageSidebar::Section section)
{
    if (m_recentProjectEditOverlay && m_recentProjectEditOverlay->isActive()) {
        m_recentProjectEditOverlay->hideOverlay();
    }

    // Convert section to index and let AnimatedStackedWidget handle the animation
    int index = ruwa::ui::widgets::HomePageSidebar::sectionToIndex(section);
    m_contentStack->setCurrentIndex(index);
}

// ============================================================================
// Navigation Methods (called from commands)
// ============================================================================

void HomePageTab::navigateToWelcome()
{
    navigateToSection(ruwa::ui::widgets::HomePageSidebar::Section::Home);
}

void HomePageTab::navigateToNewProject()
{
    navigateToSection(ruwa::ui::widgets::HomePageSidebar::Section::NewProject);
}

void HomePageTab::navigateToSettings()
{
    navigateToSection(ruwa::ui::widgets::HomePageSidebar::Section::Settings);
}

void HomePageTab::navigateToAbout()
{
    navigateToSection(ruwa::ui::widgets::HomePageSidebar::Section::About);
}

void HomePageTab::navigateToSection(ruwa::ui::widgets::HomePageSidebar::Section section)
{
    if (!m_firstRunCompleted || !m_sidebar) {
        return;
    }

    m_sidebar->setActiveSection(section);
}

void HomePageTab::onFirstRunCompletedChanged(bool completed)
{
    if (m_firstRunCompleted == completed) {
        return;
    }

    if (m_recentProjectEditOverlay && m_recentProjectEditOverlay->isActive()) {
        m_recentProjectEditOverlay->hideOverlay();
    }

    m_firstRunCompleted = completed;
    updateFirstRunPageState();
    emit titleChanged(title());
}

void HomePageTab::onFirstRunCompletedRequested()
{
    ruwa::core::SettingsManager::instance().setFirstRunIntegrationCompleted(true);
}

void HomePageTab::onFirstRunCustomizeRequested()
{
    ruwa::core::SettingsManager::instance().setFirstRunIntegrationCompleted(true);
    navigateToWelcome();
}

} // namespace ruwa::ui::tabs
