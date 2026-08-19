// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorTab.h"

#include "features/theme/editor/ThemeEditorSidebar.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace ruwa::ui::tabs {

namespace {

constexpr int kPageCount = 3;
constexpr int kPreviewStretch = 30;
constexpr int kSettingsStretch = 70;

int contentSideMarginForSize(const QSize& size)
{
    constexpr float targetAspectRatio = 14.0f / 9.0f;
    const int targetWidth = static_cast<int>(size.height() * targetAspectRatio);
    return size.width() > targetWidth ? (size.width() - targetWidth) / 2 : 0;
}

} // namespace

ThemeEditorTab::ThemeEditorTab(QWidget* parent)
    : BaseTab(parent)
{
}

ThemeEditorTab::~ThemeEditorTab() = default;

void ThemeEditorTab::selectThemeById(const QUuid& id)
{
    if (m_sidebar) {
        m_sidebar->setEditingThemeById(id);
        m_sidebar->setActiveSection(ruwa::ui::widgets::ThemeEditorSidebar::Section::Themes);
    } else {
        m_pendingThemeId = id;
    }
}

void ThemeEditorTab::onInitialize()
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setupUi();

    connect(&ruwa::ui::core::ThemeManager::instance(),
        &ruwa::ui::core::ThemeManager::themeChanged, this, &ThemeEditorTab::updateThemeColors);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this,
        &ThemeEditorTab::retranslateUi);

    updateContentSideMargins();
    updateScaledSizes();
    updateThemeColors();
}

void ThemeEditorTab::changeEvent(QEvent* event)
{
    BaseTab::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void ThemeEditorTab::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), ruwa::ui::core::ThemeManager::instance().colors().background);
}

void ThemeEditorTab::resizeEvent(QResizeEvent* event)
{
    BaseTab::resizeEvent(event);
    updateContentSideMargins();
}

void ThemeEditorTab::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_previewStack = new ruwa::ui::widgets::AnimatedStackedWidget(this);
    m_previewStack->setAnimationDuration(350);
    m_previewStack->setAnimationEasing(QEasingCurve::InOutCubic);
    rootLayout->addWidget(m_previewStack, kPreviewStretch);

    m_sectionDivider = new QWidget(this);
    m_sectionDivider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rootLayout->addWidget(m_sectionDivider);

    m_settingsFrame = new QWidget(this);
    auto* settingsLayout = new QHBoxLayout(m_settingsFrame);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(0);
    rootLayout->addWidget(m_settingsFrame, kSettingsStretch);

    m_sidebar = new ruwa::ui::widgets::ThemeEditorSidebar(m_settingsFrame);
    settingsLayout->addWidget(m_sidebar);

    m_settingsStack = new ruwa::ui::widgets::AnimatedStackedWidget(m_settingsFrame);
    m_settingsStack->setAnimationDuration(350);
    m_settingsStack->setAnimationEasing(QEasingCurve::InOutCubic);
    settingsLayout->addWidget(m_settingsStack);

    for (int index = 0; index < kPageCount; ++index) {
        m_previewStack->addWidget(createPreviewPlaceholder(m_previewStack));
        m_settingsStack->addWidget(createSettingsPlaceholder(m_settingsStack));
    }

    connect(m_sidebar, &ruwa::ui::widgets::ThemeEditorSidebar::sectionChanged, this,
        [this](ruwa::ui::widgets::ThemeEditorSidebar::Section section) {
            const int index = ruwa::ui::widgets::ThemeEditorSidebar::sectionToIndex(section);
            m_previewStack->setCurrentIndex(index);
            m_settingsStack->setCurrentIndex(index);
        });
    connect(m_sidebar, &ruwa::ui::widgets::ThemeEditorSidebar::editingThemeChanged, this,
        [this](const ruwa::ui::core::ThemePreset& preset) { m_editingTheme = preset; });

    m_previewStack->setCurrentIndexWithoutAnimation(0);
    m_settingsStack->setCurrentIndexWithoutAnimation(0);
    m_editingTheme = m_sidebar->editingTheme();
    if (!m_pendingThemeId.isNull()) {
        m_sidebar->setEditingThemeById(m_pendingThemeId);
        m_pendingThemeId = QUuid();
    }
    m_sidebar->setActiveSection(ruwa::ui::widgets::ThemeEditorSidebar::Section::Themes);
    retranslateUi();
}

QWidget* ThemeEditorTab::createPreviewPlaceholder(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setAutoFillBackground(true);
    auto* layout = new QVBoxLayout(page);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    layout->setContentsMargins(theme.scaled(24), theme.scaled(24), theme.scaled(24),
        theme.scaled(24));
    layout->setSpacing(theme.scaled(8));
    layout->addStretch();

    const int index = m_previewStack->count();
    auto* title = new QLabel(page);
    title->setAlignment(Qt::AlignCenter);
    QFont titleFont = title->font();
    titleFont.setPointSize(theme.scaledFontSize(16));
    titleFont.setBold(true);
    title->setFont(titleFont);
    m_previewTitles[static_cast<std::size_t>(index)] = title;
    layout->addWidget(title);

    auto* description = new QLabel(page);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    QFont descriptionFont = description->font();
    descriptionFont.setPointSize(theme.scaledFontSize(9));
    description->setFont(descriptionFont);
    m_previewDescriptions[static_cast<std::size_t>(index)] = description;
    layout->addWidget(description);

    layout->addStretch();
    return page;
}

QWidget* ThemeEditorTab::createSettingsPlaceholder(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    layout->setContentsMargins(theme.scaled(48), theme.scaled(48), theme.scaled(48),
        theme.scaled(48));
    layout->setSpacing(theme.scaled(10));
    layout->addStretch();

    const int index = m_settingsStack->count();
    auto* title = new QLabel(page);
    title->setAlignment(Qt::AlignCenter);
    QFont titleFont = title->font();
    titleFont.setPointSize(theme.scaledFontSize(18));
    titleFont.setBold(true);
    title->setFont(titleFont);
    m_settingsTitles[static_cast<std::size_t>(index)] = title;
    layout->addWidget(title);

    auto* description = new QLabel(page);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    QFont descriptionFont = description->font();
    descriptionFont.setPointSize(theme.scaledFontSize(10));
    description->setFont(descriptionFont);
    m_settingsDescriptions[static_cast<std::size_t>(index)] = description;
    layout->addWidget(description);

    layout->addStretch();
    return page;
}

void ThemeEditorTab::retranslateUi()
{
    const std::array<QString, kPageCount> previewTitles {
        tr("Theme Preview"), tr("Interface Preview"), tr("Canvas Preview")
    };
    const std::array<QString, kPageCount> settingsTitles {
        tr("Themes"), tr("Interface"), tr("Canvas")
    };
    const std::array<QString, kPageCount> settingsDescriptions {
        tr("Theme presets and color settings will be available here."),
        tr("Interface appearance settings will be available here."),
        tr("Canvas appearance settings will be available here.")
    };

    for (int index = 0; index < kPageCount; ++index) {
        const auto arrayIndex = static_cast<std::size_t>(index);
        m_previewTitles[arrayIndex]->setText(previewTitles[arrayIndex]);
        m_previewDescriptions[arrayIndex]->setText(tr("Preview placeholder"));
        m_settingsTitles[arrayIndex]->setText(settingsTitles[arrayIndex]);
        m_settingsDescriptions[arrayIndex]->setText(settingsDescriptions[arrayIndex]);
    }
}

void ThemeEditorTab::updateContentSideMargins()
{
    if (!layout()) {
        return;
    }

    const int sideMargin = contentSideMarginForSize(size());
    layout()->setContentsMargins(sideMargin, 0, sideMargin, 0);
}

void ThemeEditorTab::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    updateScaledSizes();

    for (QLabel* title : m_previewTitles) {
        title->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
        QWidget* page = title->parentWidget();
        QPalette palette = page->palette();
        palette.setColor(QPalette::Window, colors.surface);
        page->setPalette(palette);
    }
    for (QLabel* description : m_previewDescriptions) {
        description->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
    }
    for (QLabel* title : m_settingsTitles) {
        title->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
    }
    for (QLabel* description : m_settingsDescriptions) {
        description->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
    }

    if (m_sectionDivider) {
        m_sectionDivider->setAutoFillBackground(true);
        QPalette palette = m_sectionDivider->palette();
        palette.setColor(QPalette::Window, colors.border);
        m_sectionDivider->setPalette(palette);
    }

    update();
    if (m_settingsFrame) {
        m_settingsFrame->update();
    }
}

void ThemeEditorTab::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    if (m_sectionDivider) {
        m_sectionDivider->setFixedHeight(qMax(1, theme.scaled(1)));
    }

    for (int index = 0; index < kPageCount; ++index) {
        QLabel* title = m_previewTitles[static_cast<std::size_t>(index)];
        QLabel* description = m_previewDescriptions[static_cast<std::size_t>(index)];
        if (!title || !description) {
            continue;
        }

        if (QWidget* page = title->parentWidget(); page && page->layout()) {
            page->layout()->setContentsMargins(theme.scaled(24), theme.scaled(24),
                theme.scaled(24), theme.scaled(24));
            page->layout()->setSpacing(theme.scaled(8));
        }

        QFont titleFont = title->font();
        titleFont.setPointSize(theme.scaledFontSize(16));
        titleFont.setBold(true);
        title->setFont(titleFont);

        QFont descriptionFont = description->font();
        descriptionFont.setPointSize(theme.scaledFontSize(9));
        description->setFont(descriptionFont);
    }

    for (int index = 0; index < kPageCount; ++index) {
        QLabel* title = m_settingsTitles[static_cast<std::size_t>(index)];
        QLabel* description = m_settingsDescriptions[static_cast<std::size_t>(index)];
        if (!title || !description) {
            continue;
        }

        if (QWidget* page = title->parentWidget(); page && page->layout()) {
            page->layout()->setContentsMargins(theme.scaled(48), theme.scaled(48),
                theme.scaled(48), theme.scaled(48));
            page->layout()->setSpacing(theme.scaled(10));
        }

        QFont titleFont = title->font();
        titleFont.setPointSize(theme.scaledFontSize(18));
        titleFont.setBold(true);
        title->setFont(titleFont);

        QFont descriptionFont = description->font();
        descriptionFont.setPointSize(theme.scaledFontSize(10));
        description->setFont(descriptionFont);
    }
}

} // namespace ruwa::ui::tabs
