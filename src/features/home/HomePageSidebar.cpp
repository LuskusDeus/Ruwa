// SPDX-License-Identifier: MPL-2.0

// HomePageSidebar.cpp
#include "HomePageSidebar.h"
#include "SidebarButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/resources/IconProvider.h"

#include <QCoreApplication>
#include <QPainter>
#include <QEvent>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {

class SidebarDivider final : public QWidget {
public:
    explicit SidebarDivider(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    QSize sizeHint() const override
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        return { 0, qMax(1, theme.scaled(1)) };
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, false);

        QColor dividerColor = ruwa::ui::core::ThemeManager::instance().colors().border;
        dividerColor.setAlpha(110);

        const int lineHeight = qMax(1, ruwa::ui::core::ThemeManager::instance().scaled(1));
        const int y = (height() - lineHeight) / 2;
        painter.fillRect(QRect(0, y, width(), lineHeight), dividerColor);
    }
};

} // namespace

HomePageSidebar::HomePageSidebar(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    // Connect to theme changes
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &HomePageSidebar::onThemeChanged);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, [this]() { retranslateUi(); });
}

void HomePageSidebar::setActiveSection(Section section)
{
    if (m_activeSection == section) {
        return;
    }

    // Deactivate previous button
    if (m_buttons.contains(m_activeSection)) {
        m_buttons[m_activeSection]->setActive(false);
    }

    // Activate new button
    m_activeSection = section;
    if (m_buttons.contains(section)) {
        m_buttons[section]->setActive(true);
    }

    emit sectionChanged(section);
}

void HomePageSidebar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Retranslation via TranslationManager::languageChanged (after installTranslator).
}

void HomePageSidebar::retranslateUi()
{
    if (m_buttons.contains(Section::Home))
        m_buttons[Section::Home]->setText(tr("Home"));
    if (m_buttons.contains(Section::NewProject))
        m_buttons[Section::NewProject]->setText(tr("New Project"));
    if (m_buttons.contains(Section::Settings))
        m_buttons[Section::Settings]->setText(tr("Settings"));
    if (m_buttons.contains(Section::About))
        m_buttons[Section::About]->setText(tr("About"));
}

void HomePageSidebar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // Draw sidebar background - same as main content area
    painter.fillRect(rect(), colors.background);
}

void HomePageSidebar::setupUI()
{
    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(0);

    // Apply initial scaled sizes
    updateScaledSizes();

    // Navigation buttons
    createNavigationButtons();

    // Initial active section will be set by parent (HomePageTab)
}

void HomePageSidebar::createNavigationButtons()
{
    // Get icons from IconProvider
    auto& iconProvider = ruwa::ui::core::ThemeManager::instance().icons();

    QIcon homeIcon = iconProvider.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Home);
    QIcon newProjectIcon
        = iconProvider.getIcon(ruwa::ui::core::IconProvider::StandardIcon::FileNew);
    QIcon settingsIcon = iconProvider.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Settings);
    QIcon aboutIcon
        = iconProvider.getIcon(ruwa::ui::core::IconProvider::StandardIcon::TransparentLogoIcon);

    // Create buttons with icons (use translate for correct context)
    SidebarButton* homeBtn = new SidebarButton(tr("Home"), homeIcon);
    SidebarButton* newProjectBtn
        = new SidebarButton(tr("New Project"), newProjectIcon);
    SidebarButton* settingsBtn
        = new SidebarButton(tr("Settings"), settingsIcon);
    SidebarButton* aboutBtn
        = new SidebarButton(tr("About"), aboutIcon);

    m_buttons[Section::Home] = homeBtn;
    m_buttons[Section::NewProject] = newProjectBtn;
    m_buttons[Section::Settings] = settingsBtn;
    m_buttons[Section::About] = aboutBtn;

    // Connect signals
    connect(homeBtn, &QPushButton::clicked, this, [this]() { onButtonClicked(Section::Home); });
    connect(newProjectBtn, &QPushButton::clicked, this,
        [this]() { onButtonClicked(Section::NewProject); });
    connect(
        settingsBtn, &QPushButton::clicked, this, [this]() { onButtonClicked(Section::Settings); });
    connect(aboutBtn, &QPushButton::clicked, this, [this]() { onButtonClicked(Section::About); });

    auto* aboutDivider = new SidebarDivider(this);

    // Layout: Home, New Project, Settings, divider, About
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    m_layout->addWidget(homeBtn);
    m_layout->addSpacing(theme.scaled(8));
    m_layout->addWidget(newProjectBtn);
    m_layout->addSpacing(theme.scaled(8));
    m_layout->addWidget(settingsBtn);
    m_layout->addSpacing(theme.scaled(12));
    m_layout->addWidget(aboutDivider);
    m_layout->addSpacing(theme.scaled(12));
    m_layout->addWidget(aboutBtn);
    m_layout->addStretch();
}

void HomePageSidebar::onButtonClicked(Section section)
{
    setActiveSection(section);
}

void HomePageSidebar::updateThemeColors()
{
    // Force repaint for background color
    update();
}

void HomePageSidebar::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    setFixedWidth(theme.scaled(220));
    m_layout->setContentsMargins(theme.scaled(8), m_topMarginPx, theme.scaled(8), theme.scaled(20));
}

void HomePageSidebar::setTopMarginPx(int px)
{
    if (m_topMarginPx == px) {
        return;
    }
    m_topMarginPx = px;
    updateScaledSizes();
}

void HomePageSidebar::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

QList<QWidget*> HomePageSidebar::getButtonsInOrder() const
{
    QList<QWidget*> result;
    // Order: Home, NewProject, Settings, About (top to bottom)
    if (m_buttons.contains(Section::Home))
        result.append(m_buttons[Section::Home]);
    if (m_buttons.contains(Section::NewProject))
        result.append(m_buttons[Section::NewProject]);
    if (m_buttons.contains(Section::Settings))
        result.append(m_buttons[Section::Settings]);
    if (m_buttons.contains(Section::About))
        result.append(m_buttons[Section::About]);
    return result;
}

} // namespace ruwa::ui::widgets
