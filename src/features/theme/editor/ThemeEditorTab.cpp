// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorTab.h"

#include "features/theme/editor/ThemeEditorSidebar.h"
#include "features/theme/editor/ThemeEditorThemesPreview.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseStyledPanel.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace ruwa::ui::tabs {

namespace {

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

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ThemeEditorTab::updateThemeColors);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, &ThemeEditorTab::retranslateUi);

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

    m_settingsFrame = new QWidget(this);
    auto* settingsLayout = new QHBoxLayout(m_settingsFrame);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(0);
    rootLayout->addWidget(m_settingsFrame, kSettingsStretch);

    m_sidebar = new ruwa::ui::widgets::ThemeEditorSidebar(m_settingsFrame);
    settingsLayout->addWidget(m_sidebar);
    m_editingTheme = m_sidebar->editingTheme();
    m_savedTheme = m_editingTheme;

    m_settingsStack = new ruwa::ui::widgets::AnimatedStackedWidget(m_settingsFrame);
    m_settingsStack->setAnimationDuration(350);
    m_settingsStack->setAnimationEasing(QEasingCurve::InOutCubic);
    settingsLayout->addWidget(m_settingsStack);

    const auto sectionDefinitions = settingsSectionDefinitions();
    for (std::size_t index = 0; index < SectionCount; ++index) {
        m_previewStack->addWidget(index == 0 ? createThemesPreviewPage(m_previewStack)
                                             : createPreviewPlaceholder(m_previewStack));
        m_settingsStack->addWidget(
            createSettingsSection(sectionDefinitions[index], index, m_settingsStack));
    }

    connect(m_sidebar, &ruwa::ui::widgets::ThemeEditorSidebar::sectionChanged, this,
        [this](ruwa::ui::widgets::ThemeEditorSidebar::Section section) {
            const int index = ruwa::ui::widgets::ThemeEditorSidebar::sectionToIndex(section);
            m_previewStack->setCurrentIndex(index);
            m_settingsStack->setCurrentIndex(index);
        });
    connect(m_sidebar, &ruwa::ui::widgets::ThemeEditorSidebar::editingThemeChanged, this,
        [this](const ruwa::ui::core::ThemePreset& preset) {
            m_editingTheme = preset;
            m_savedTheme = preset;
            if (m_themesPreview) {
                m_themesPreview->setPreset(m_editingTheme);
            }
            syncColorInputs();
            setDirtyState(false);
        });

    m_previewStack->setCurrentIndexWithoutAnimation(0);
    m_settingsStack->setCurrentIndexWithoutAnimation(0);
    syncColorInputs();
    if (!m_pendingThemeId.isNull()) {
        m_sidebar->setEditingThemeById(m_pendingThemeId);
        m_pendingThemeId = QUuid();
    }
    m_sidebar->setActiveSection(ruwa::ui::widgets::ThemeEditorSidebar::Section::Themes);
    retranslateUi();
}

QWidget* ThemeEditorTab::createThemesPreviewPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);

    auto* pageLayout = new QVBoxLayout(page);
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    pageLayout->setContentsMargins(
        theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
    pageLayout->setSpacing(0);

    m_themesPreview = new ruwa::ui::widgets::ThemeEditorThemesPreview(page);
    m_themesPreview->setPreset(m_editingTheme);
    pageLayout->addWidget(m_themesPreview);
    return page;
}

QWidget* ThemeEditorTab::createPreviewPlaceholder(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    auto* pageLayout = new QVBoxLayout(page);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    pageLayout->setContentsMargins(
        theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
    pageLayout->setSpacing(0);

    auto previewStyle = ruwa::ui::core::WidgetStyle::settingsPanelStyle();
    previewStyle.metrics.baseCornerRadius = 12;
    previewStyle.background.color = ruwa::ui::core::ColorSource::Surface;
    auto* previewPanel = new ruwa::ui::widgets::BaseStyledPanel(previewStyle, page);
    previewPanel->setHoverEnabled(false);
    auto* contentLayout = new QVBoxLayout(previewPanel);
    contentLayout->setContentsMargins(
        theme.scaled(24), theme.scaled(24), theme.scaled(24), theme.scaled(24));
    contentLayout->setSpacing(theme.scaled(8));
    contentLayout->addStretch();

    const int index = m_previewStack->count();
    auto* title = new QLabel(previewPanel);
    title->setAlignment(Qt::AlignCenter);
    QFont titleFont = theme.font(ruwa::ui::core::ThemeFontRole::H4, QFont::Bold);
    title->setFont(titleFont);
    m_previewTitles[static_cast<std::size_t>(index)] = title;
    contentLayout->addWidget(title);

    auto* description = new QLabel(previewPanel);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    QFont descriptionFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);
    description->setFont(descriptionFont);
    m_previewDescriptions[static_cast<std::size_t>(index)] = description;
    contentLayout->addWidget(description);

    contentLayout->addStretch();
    pageLayout->addWidget(previewPanel);
    return page;
}

QWidget* ThemeEditorTab::createSettingsSection(
    const SettingsSectionDefinition& definition, std::size_t sectionIndex, QWidget* parent)
{
    Q_ASSERT(sectionIndex < SectionCount);
    Q_ASSERT(!definition.pages.isEmpty());
    Q_ASSERT(definition.hasSubTabs || definition.pages.size() == 1);

    auto* section = new QWidget(parent);
    auto* sectionLayout = new QVBoxLayout(section);
    auto& sectionUi = m_settingsSections[sectionIndex];
    sectionUi.container = section;
    sectionUi.pages = definition.pages;

    if (definition.hasSubTabs) {
        sectionUi.headerRow = new QWidget(section);
        sectionUi.headerRow->setAttribute(Qt::WA_TranslucentBackground);
        auto* headerLayout = new QHBoxLayout(sectionUi.headerRow);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        headerLayout->setSpacing(8);

        sectionUi.tabsBar = new QWidget(sectionUi.headerRow);
        sectionUi.tabsBar->setAttribute(Qt::WA_TranslucentBackground);
        sectionUi.tabsBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto* tabsLayout = new QHBoxLayout(sectionUi.tabsBar);
        tabsLayout->setContentsMargins(0, 0, 0, 0);
        tabsLayout->setSpacing(6);
        tabsLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

        sectionUi.tabGroup = new QButtonGroup(section);
        sectionUi.tabGroup->setExclusive(true);

        for (int index = 0; index < definition.pages.size(); ++index) {
            auto* tabButton = new ruwa::ui::widgets::CapsuleButton(
                QString(), ruwa::ui::widgets::CapsuleButton::Variant::Tab, sectionUi.tabsBar);
            tabButton->setFocusPolicy(Qt::NoFocus);
            tabButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            sectionUi.tabGroup->addButton(tabButton, index);
            tabsLayout->addWidget(tabButton);
            sectionUi.tabButtons.append(tabButton);
        }

        if (!sectionUi.tabButtons.isEmpty()) {
            sectionUi.tabButtons.first()->setChecked(true);
        }

        sectionUi.applyButton = new ruwa::ui::widgets::CapsuleButton(
            tr("Apply"), ruwa::ui::widgets::CapsuleButton::Variant::Secondary, sectionUi.headerRow);
        sectionUi.applyButton->setBaseMinimumWidth(84);
        sectionUi.applyButton->setBannerBaseHeight(36);
        connect(
            sectionUi.applyButton, &QPushButton::clicked, this, &ThemeEditorTab::applyEditingTheme);

        sectionUi.saveButton = new ruwa::ui::widgets::CapsuleButton(saveButtonText(),
            ruwa::ui::widgets::CapsuleButton::Variant::Primary, sectionUi.headerRow);
        sectionUi.saveButton->setBaseMinimumWidth(84);
        sectionUi.saveButton->setBannerBaseHeight(36);
        sectionUi.saveButton->setDisabledTextTone(
            ruwa::ui::widgets::CapsuleButton::DisabledTextTone::Dark);
        sectionUi.saveButton->setEnabled(false);
        connect(
            sectionUi.saveButton, &QPushButton::clicked, this, &ThemeEditorTab::saveEditingTheme);

        headerLayout->addWidget(sectionUi.tabsBar, 1);
        headerLayout->addWidget(sectionUi.applyButton);
        headerLayout->addWidget(sectionUi.saveButton);
        sectionLayout->addWidget(sectionUi.headerRow);
    }

    sectionUi.contentStack = new ruwa::ui::widgets::AnimatedStackedWidget(section);
    sectionUi.contentStack->setAnimationDuration(230);
    sectionUi.contentStack->setAnimationEasing(QEasingCurve::InOutCubic);
    sectionUi.contentStack->setSlideOrientation(
        ruwa::ui::widgets::AnimatedStackedWidget::SlideOrientation::Horizontal);

    for (SettingsPage settingsPage : definition.pages) {
        QWidget* settingsContent = settingsPage == SettingsPage::ThemeColors
            ? createColorsSettingsPage(sectionUi.contentStack)
            : createSettingsPlaceholder(settingsPage, sectionUi.contentStack);
        sectionUi.contentStack->addWidget(settingsContent);
    }
    sectionUi.contentStack->setCurrentIndexWithoutAnimation(0);
    sectionLayout->addWidget(sectionUi.contentStack, 1);

    if (sectionUi.tabGroup) {
        connect(
            sectionUi.tabGroup, &QButtonGroup::idClicked, this, [this, sectionIndex](int index) {
                auto& selectedSection = m_settingsSections[sectionIndex];
                if (index >= 0 && index < selectedSection.contentStack->count()) {
                    selectedSection.contentStack->setCurrentIndex(index);
                }
            });
        connect(sectionUi.contentStack, &ruwa::ui::widgets::AnimatedStackedWidget::currentChanged,
            this, [this, sectionIndex](int index) {
                auto& selectedSection = m_settingsSections[sectionIndex];
                for (int buttonIndex = 0; buttonIndex < selectedSection.tabButtons.size();
                    ++buttonIndex) {
                    auto* button = selectedSection.tabButtons[buttonIndex];
                    if (button && button->isChecked() != (buttonIndex == index)) {
                        button->setChecked(buttonIndex == index);
                    }
                }
            });
    }

    return section;
}

QWidget* ThemeEditorTab::createSettingsPlaceholder(SettingsPage settingsPage, QWidget* parent)
{
    auto* scrollArea = new ruwa::ui::widgets::SmoothScrollArea(parent);
    scrollArea->setFillBackground(false);
    scrollArea->setScrollBarTransparentTrack(true);

    auto* content = new QWidget();
    content->setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QVBoxLayout(content);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme.scaled(10));
    layout->addStretch();

    const auto index = static_cast<std::size_t>(settingsPage);
    auto* title = new QLabel(content);
    title->setAlignment(Qt::AlignCenter);
    QFont titleFont = theme.font(ruwa::ui::core::ThemeFontRole::H3, QFont::Bold);
    title->setFont(titleFont);
    m_settingsTitles[index] = title;
    layout->addWidget(title);

    auto* description = new QLabel(content);
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    QFont descriptionFont = theme.font(ruwa::ui::core::ThemeFontRole::Label);
    description->setFont(descriptionFont);
    m_settingsDescriptions[index] = description;
    layout->addWidget(description);

    layout->addStretch();
    scrollArea->setWidget(content);
    return scrollArea;
}

QWidget* ThemeEditorTab::createColorsSettingsPage(QWidget* parent)
{
    auto* scrollArea = new ruwa::ui::widgets::SmoothScrollArea(parent);
    scrollArea->setFillBackground(false);
    scrollArea->setScrollBarTransparentTrack(true);
    scrollArea->setScrollBarMargin(ruwa::ui::core::ThemeManager::instance().scaled(4));

    auto* content = new QWidget();
    content->setAttribute(Qt::WA_TranslucentBackground);
    auto* columnsLayout = new QHBoxLayout(content);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    columnsLayout->setSpacing(ruwa::ui::core::ThemeManager::instance().scaled(8));

    const std::array<QVector<ColorField>, ColorCategoryCount> categories {
        QVector<ColorField> { ColorField::Primary, ColorField::Accent, ColorField::Background,
            ColorField::Surface, ColorField::SurfaceAlt, ColorField::Border, ColorField::Overlay },
        QVector<ColorField> { ColorField::Text, ColorField::TextMuted, ColorField::TextOnPrimary },
        QVector<ColorField> {
            ColorField::Success, ColorField::Warning, ColorField::Error, ColorField::Info }
    };

    for (std::size_t index = 0; index < ColorCategoryCount; ++index) {
        columnsLayout->addWidget(createColorCategory(categories[index], index, content), 1);
    }

    scrollArea->setWidget(content);
    return scrollArea;
}

QWidget* ThemeEditorTab::createColorCategory(
    const QVector<ColorField>& fields, std::size_t categoryIndex, QWidget* parent)
{
    auto* category = new QWidget(parent);
    category->setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QVBoxLayout(category);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme.scaled(8));

    auto* title = new QLabel(category);
    QFont titleFont = theme.font(ruwa::ui::core::ThemeFontRole::BodyLarge, QFont::Bold);
    title->setFont(titleFont);
    m_colorCategoryTitles[categoryIndex] = title;
    layout->addWidget(title);

    for (ColorField field : fields) {
        layout->addWidget(createColorInput(field, category));
    }
    layout->addStretch();
    return category;
}

ruwa::ui::widgets::ColorInputButton* ThemeEditorTab::createColorInput(
    ColorField field, QWidget* parent)
{
    auto* input = new ruwa::ui::widgets::ColorInputButton(QString(), editingColor(field), parent);
    input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    connect(input, &ruwa::ui::widgets::ColorInputButton::colorPickerRequested, this,
        [this, input](const QColor& color) { emit colorPickerRequested(color, input); });
    connect(input, &ruwa::ui::widgets::ColorInputButton::colorChanged, this,
        [this, field](const QColor& color) {
            if (!m_syncingColorInputs) {
                editingColor(field) = color;
                if (m_themesPreview) {
                    m_themesPreview->setPreset(m_editingTheme);
                }
                updateDirtyState();
            }
        });

    m_colorInputs[static_cast<std::size_t>(field)] = input;
    return input;
}

std::array<ThemeEditorTab::SettingsSectionDefinition, ThemeEditorTab::SectionCount>
ThemeEditorTab::settingsSectionDefinitions() const
{
    return { SettingsSectionDefinition {
                 true, { SettingsPage::ThemeColors, SettingsPage::ThemeFont } },
        SettingsSectionDefinition { true, { SettingsPage::Interface } },
        SettingsSectionDefinition { true, { SettingsPage::Canvas } } };
}

QString ThemeEditorTab::settingsPageTitle(SettingsPage settingsPage) const
{
    switch (settingsPage) {
    case SettingsPage::ThemeColors:
        return tr("Colors");
    case SettingsPage::ThemeFont:
        return tr("Font");
    case SettingsPage::Interface:
        return tr("Interface");
    case SettingsPage::Canvas:
        return tr("Canvas");
    default:
        return {};
    }
}

QString ThemeEditorTab::settingsPageDescription(SettingsPage settingsPage) const
{
    switch (settingsPage) {
    case SettingsPage::ThemeColors:
        return {};
    case SettingsPage::ThemeFont:
        return tr("Theme font settings will be available here.");
    case SettingsPage::Interface:
        return tr("Interface appearance settings will be available here.");
    case SettingsPage::Canvas:
        return tr("Canvas appearance settings will be available here.");
    default:
        return {};
    }
}

QString ThemeEditorTab::saveButtonText() const
{
    return m_editingTheme.isBuiltIn ? tr("Save as new +") : tr("Save");
}

QString ThemeEditorTab::colorFieldLabel(ColorField field) const
{
    switch (field) {
    case ColorField::Primary:
        return tr("Primary");
    case ColorField::Accent:
        return tr("Accent");
    case ColorField::Background:
        return tr("Background");
    case ColorField::Surface:
        return tr("Surface");
    case ColorField::SurfaceAlt:
        return tr("Surface Alt");
    case ColorField::Border:
        return tr("Border");
    case ColorField::Overlay:
        return tr("Overlay");
    case ColorField::Text:
        return tr("Text");
    case ColorField::TextMuted:
        return tr("Muted Text");
    case ColorField::TextOnPrimary:
        return tr("Text on Primary");
    case ColorField::Success:
        return tr("Success");
    case ColorField::Warning:
        return tr("Warning");
    case ColorField::Error:
        return tr("Error");
    case ColorField::Info:
        return tr("Info");
    default:
        return {};
    }
}

QColor& ThemeEditorTab::editingColor(ColorField field)
{
    switch (field) {
    case ColorField::Primary:
        return m_editingTheme.primary;
    case ColorField::Accent:
        return m_editingTheme.accent;
    case ColorField::Background:
        return m_editingTheme.background;
    case ColorField::Surface:
        return m_editingTheme.surface;
    case ColorField::SurfaceAlt:
        return m_editingTheme.surfaceAlt;
    case ColorField::Border:
        return m_editingTheme.border;
    case ColorField::Overlay:
        return m_editingTheme.overlayColor;
    case ColorField::Text:
        return m_editingTheme.text;
    case ColorField::TextMuted:
        return m_editingTheme.textMuted;
    case ColorField::TextOnPrimary:
        return m_editingTheme.textOnPrimary;
    case ColorField::Success:
        return m_editingTheme.success;
    case ColorField::Warning:
        return m_editingTheme.warning;
    case ColorField::Error:
        return m_editingTheme.error;
    case ColorField::Info:
        return m_editingTheme.info;
    default:
        Q_ASSERT(false);
        return m_editingTheme.primary;
    }
}

const QColor& ThemeEditorTab::savedColor(ColorField field) const
{
    switch (field) {
    case ColorField::Primary:
        return m_savedTheme.primary;
    case ColorField::Accent:
        return m_savedTheme.accent;
    case ColorField::Background:
        return m_savedTheme.background;
    case ColorField::Surface:
        return m_savedTheme.surface;
    case ColorField::SurfaceAlt:
        return m_savedTheme.surfaceAlt;
    case ColorField::Border:
        return m_savedTheme.border;
    case ColorField::Overlay:
        return m_savedTheme.overlayColor;
    case ColorField::Text:
        return m_savedTheme.text;
    case ColorField::TextMuted:
        return m_savedTheme.textMuted;
    case ColorField::TextOnPrimary:
        return m_savedTheme.textOnPrimary;
    case ColorField::Success:
        return m_savedTheme.success;
    case ColorField::Warning:
        return m_savedTheme.warning;
    case ColorField::Error:
        return m_savedTheme.error;
    case ColorField::Info:
        return m_savedTheme.info;
    default:
        Q_ASSERT(false);
        return m_savedTheme.primary;
    }
}

void ThemeEditorTab::syncColorInputs()
{
    m_syncingColorInputs = true;
    for (std::size_t index = 0; index < ColorFieldCount; ++index) {
        if (m_colorInputs[index]) {
            m_colorInputs[index]->setColor(editingColor(static_cast<ColorField>(index)));
        }
    }
    m_syncingColorInputs = false;
}

void ThemeEditorTab::updateDirtyState()
{
    bool dirty = false;
    for (std::size_t index = 0; index < ColorFieldCount; ++index) {
        const auto field = static_cast<ColorField>(index);
        if (editingColor(field) != savedColor(field)) {
            dirty = true;
            break;
        }
    }
    setDirtyState(dirty);
}

void ThemeEditorTab::setDirtyState(bool dirty)
{
    setModified(dirty);
    for (auto& section : m_settingsSections) {
        if (section.saveButton) {
            section.saveButton->setText(saveButtonText());
            section.saveButton->syncSizeToText();
            section.saveButton->setEnabled(dirty);
        }
    }
}

void ThemeEditorTab::applyEditingTheme()
{
    if (m_editingTheme.id.isNull()) {
        return;
    }
    if (ruwa::ui::core::ThemeManager::instance().applyPreset(m_editingTheme)) {
        emit themeApplied(m_editingTheme.id);
    }
}

void ThemeEditorTab::saveEditingTheme()
{
    if (!isModified() || !m_sidebar || m_editingTheme.id.isNull()) {
        return;
    }
    m_sidebar->saveEditingTheme(m_editingTheme);
}

void ThemeEditorTab::retranslateUi()
{
    const std::array<QString, SectionCount> previewTitles { tr("Theme Preview"),
        tr("Interface Preview"), tr("Canvas Preview") };

    for (std::size_t index = 0; index < SectionCount; ++index) {
        if (m_previewTitles[index]) {
            m_previewTitles[index]->setText(previewTitles[index]);
        }
        if (m_previewDescriptions[index]) {
            m_previewDescriptions[index]->setText(tr("Preview placeholder"));
        }
    }

    for (std::size_t index = 0; index < SettingsPageCount; ++index) {
        const auto settingsPage = static_cast<SettingsPage>(index);
        if (m_settingsTitles[index]) {
            m_settingsTitles[index]->setText(settingsPageTitle(settingsPage));
        }
        if (m_settingsDescriptions[index]) {
            m_settingsDescriptions[index]->setText(settingsPageDescription(settingsPage));
        }
    }

    const std::array<QString, ColorCategoryCount> categoryTitles { tr("Core Colors"),
        tr("Text Colors"), tr("Semantic Colors") };
    for (std::size_t index = 0; index < ColorCategoryCount; ++index) {
        if (m_colorCategoryTitles[index]) {
            m_colorCategoryTitles[index]->setText(categoryTitles[index]);
        }
    }
    for (std::size_t index = 0; index < ColorFieldCount; ++index) {
        if (m_colorInputs[index]) {
            m_colorInputs[index]->setLabel(colorFieldLabel(static_cast<ColorField>(index)));
        }
    }

    for (auto& section : m_settingsSections) {
        for (int index = 0; index < section.tabButtons.size(); ++index) {
            section.tabButtons[index]->setText(settingsPageTitle(section.pages[index]));
            section.tabButtons[index]->syncSizeToText();
        }
        if (section.applyButton) {
            section.applyButton->setText(tr("Apply"));
            section.applyButton->syncSizeToText();
        }
        if (section.saveButton) {
            section.saveButton->setText(saveButtonText());
            section.saveButton->syncSizeToText();
        }
    }

    updateScaledSizes();
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
        if (title) {
            title->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
        }
    }
    for (QLabel* description : m_previewDescriptions) {
        if (description) {
            description->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
        }
    }
    for (QLabel* title : m_settingsTitles) {
        if (title) {
            title->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
        }
    }
    for (QLabel* description : m_settingsDescriptions) {
        if (description) {
            description->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
        }
    }
    for (QLabel* title : m_colorCategoryTitles) {
        if (title) {
            title->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
        }
    }

    update();
    if (m_settingsFrame) {
        m_settingsFrame->update();
    }
}

void ThemeEditorTab::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto* sidebarButtonStyle
        = ruwa::ui::core::WidgetStyleManager::instance().style(QStringLiteral("SidebarButton"));
    Q_ASSERT(sidebarButtonStyle);
    const int settingsTabHeight
        = sidebarButtonStyle ? theme.scaled(sidebarButtonStyle->metrics.baseHeight) : 0;

    if (m_themesPreview) {
        if (QWidget* page = m_themesPreview->parentWidget(); page && page->layout()) {
            page->layout()->setContentsMargins(
                theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
        }
    }

    for (std::size_t index = 0; index < SectionCount; ++index) {
        QLabel* title = m_previewTitles[index];
        QLabel* description = m_previewDescriptions[index];
        if (!title || !description) {
            continue;
        }

        if (QWidget* panel = title->parentWidget(); panel && panel->layout()) {
            panel->layout()->setContentsMargins(
                theme.scaled(24), theme.scaled(24), theme.scaled(24), theme.scaled(24));
            panel->layout()->setSpacing(theme.scaled(8));
            if (QWidget* page = panel->parentWidget(); page && page->layout()) {
                page->layout()->setContentsMargins(
                    theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
            }
        }

        QFont titleFont = theme.font(ruwa::ui::core::ThemeFontRole::H4, QFont::Bold);
        title->setFont(titleFont);

        QFont descriptionFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);
        description->setFont(descriptionFont);
    }

    for (QLabel* title : m_colorCategoryTitles) {
        if (!title) {
            continue;
        }
        QFont font = theme.font(ruwa::ui::core::ThemeFontRole::BodyLarge, QFont::Bold);
        title->setFont(font);

        if (QWidget* category = title->parentWidget(); category && category->layout()) {
            category->layout()->setContentsMargins(0, 0, 0, 0);
            category->layout()->setSpacing(theme.scaled(8));
            if (QWidget* content = category->parentWidget(); content && content->layout()) {
                content->layout()->setSpacing(theme.scaled(8));
            }
        }
    }

    for (auto& section : m_settingsSections) {
        if (section.container && section.container->layout()) {
            section.container->layout()->setContentsMargins(
                theme.scaled(8), 0, theme.scaled(8), theme.scaled(8));
            section.container->layout()->setSpacing(theme.scaled(8));
        }
        if (section.headerRow && section.headerRow->layout()) {
            section.headerRow->layout()->setSpacing(theme.scaled(8));
        }
        if (section.tabsBar && section.tabsBar->layout()) {
            section.tabsBar->layout()->setSpacing(theme.scaled(6));
        }

        for (auto* button : section.tabButtons) {
            QFont font = theme.font(ruwa::ui::core::ThemeFontRole::Label);
            button->setFont(font);

            const QSize naturalSize = button->sizeHint();
            if (settingsTabHeight > 0 && naturalSize.height() > 0) {
                const int proportionalWidth = qRound(qreal(naturalSize.width())
                    * qreal(settingsTabHeight) / qreal(naturalSize.height()));
                button->setFixedSize(proportionalWidth, settingsTabHeight);
            }
        }

        for (auto* button : { section.applyButton, section.saveButton }) {
            if (!button) {
                continue;
            }
            if (sidebarButtonStyle) {
                button->setBannerBaseHeight(sidebarButtonStyle->metrics.baseHeight);
            }
            button->setBaseMinimumWidth(84);
        }
    }

    for (std::size_t index = 0; index < SettingsPageCount; ++index) {
        QLabel* title = m_settingsTitles[index];
        QLabel* description = m_settingsDescriptions[index];
        if (!title || !description) {
            continue;
        }

        if (QWidget* page = title->parentWidget(); page && page->layout()) {
            page->layout()->setContentsMargins(0, 0, 0, 0);
            page->layout()->setSpacing(theme.scaled(10));
        }

        QFont titleFont = theme.font(ruwa::ui::core::ThemeFontRole::H3, QFont::Bold);
        title->setFont(titleFont);

        QFont descriptionFont = theme.font(ruwa::ui::core::ThemeFontRole::Label);
        description->setFont(descriptionFont);
    }
}

} // namespace ruwa::ui::tabs
