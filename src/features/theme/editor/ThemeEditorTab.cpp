// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorTab.h"

#include "features/theme/editor/ThemeEditorAnimationsPreview.h"
#include "features/theme/editor/ThemeEditorSidebar.h"
#include "features/theme/editor/ThemeEditorThemesPreview.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/inputs/FontDropdownSelector.h"
#include "shared/widgets/inputs/NumericInputField.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/widgets/layout/PropertyRowLayout.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QButtonGroup>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace ruwa::ui::tabs {

namespace {

constexpr int kPreviewStretch = 30;
constexpr int kSettingsStretch = 70;
constexpr double kMinimumThemeFontSize = 6.0;
constexpr double kMaximumThemeFontSize = 96.0;

constexpr std::array<ruwa::ui::core::ThemeFontRole, 16> kFontSizeRoles {
    ruwa::ui::core::ThemeFontRole::Display,
    ruwa::ui::core::ThemeFontRole::H0,
    ruwa::ui::core::ThemeFontRole::H1,
    ruwa::ui::core::ThemeFontRole::H2,
    ruwa::ui::core::ThemeFontRole::H3,
    ruwa::ui::core::ThemeFontRole::H4,
    ruwa::ui::core::ThemeFontRole::H5,
    ruwa::ui::core::ThemeFontRole::H6,
    ruwa::ui::core::ThemeFontRole::Subtitle,
    ruwa::ui::core::ThemeFontRole::BodyLarge,
    ruwa::ui::core::ThemeFontRole::Label,
    ruwa::ui::core::ThemeFontRole::Body,
    ruwa::ui::core::ThemeFontRole::Small,
    ruwa::ui::core::ThemeFontRole::Caption,
    ruwa::ui::core::ThemeFontRole::Micro,
    ruwa::ui::core::ThemeFontRole::Code,
};

QString& fontFamilyAt(ruwa::ui::core::ThemePreset& preset, std::size_t familyIndex)
{
    Q_ASSERT(familyIndex < 2);
    return familyIndex == 0 ? preset.fonts.titleFont : preset.fonts.uiFont;
}

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
                                             : createAnimationsPreviewPage(m_previewStack));
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
            refreshPreviews();
            syncColorInputs();
            syncFontInputs();
            syncAnimationInputs();
            setDirtyState(false);
        });

    m_previewStack->setCurrentIndexWithoutAnimation(0);
    m_settingsStack->setCurrentIndexWithoutAnimation(0);
    syncColorInputs();
    syncFontInputs();
    syncAnimationInputs();
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

QWidget* ThemeEditorTab::createAnimationsPreviewPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);

    auto* pageLayout = new QVBoxLayout(page);
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    pageLayout->setContentsMargins(
        theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
    pageLayout->setSpacing(0);

    m_animationsPreview = new ruwa::ui::widgets::ThemeEditorAnimationsPreview(page);
    m_animationsPreview->setPreset(m_editingTheme);
    pageLayout->addWidget(m_animationsPreview);
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
        QWidget* settingsContent = nullptr;
        if (settingsPage == SettingsPage::ThemeColors) {
            settingsContent = createColorsSettingsPage(sectionUi.contentStack);
        } else if (settingsPage == SettingsPage::ThemeFont) {
            settingsContent = createFontSettingsPage(sectionUi.contentStack);
        } else if (settingsPage == SettingsPage::Animations) {
            settingsContent = createAnimationsSettingsPage(sectionUi.contentStack);
        } else {
            settingsContent = createSettingsPlaceholder(settingsPage, sectionUi.contentStack);
        }
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

QWidget* ThemeEditorTab::createFontSettingsPage(QWidget* parent)
{
    auto* scrollArea = new ruwa::ui::widgets::SmoothScrollArea(parent);
    scrollArea->setFillBackground(false);
    scrollArea->setScrollBarTransparentTrack(true);
    scrollArea->setScrollBarMargin(ruwa::ui::core::ThemeManager::instance().scaled(4));

    auto* content = new QWidget();
    content->setAttribute(Qt::WA_TranslucentBackground);
    auto* columnsLayout = new QHBoxLayout(content);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    columnsLayout->setSpacing(ruwa::ui::core::ThemeManager::instance().scaled(16));

    for (std::size_t index = 0; index < FontCategoryCount; ++index) {
        columnsLayout->addWidget(createFontCategory(index, content), 1);
    }

    scrollArea->setWidget(content);
    return scrollArea;
}

QWidget* ThemeEditorTab::createAnimationsSettingsPage(QWidget* parent)
{
    auto* scrollArea = new ruwa::ui::widgets::SmoothScrollArea(parent);
    scrollArea->setFillBackground(false);
    scrollArea->setScrollBarTransparentTrack(true);
    scrollArea->setScrollBarMargin(ruwa::ui::core::ThemeManager::instance().scaled(4));

    auto* content = new QWidget();
    content->setAttribute(Qt::WA_TranslucentBackground);
    auto* columnsLayout = new QHBoxLayout(content);
    columnsLayout->setContentsMargins(0, 0, 0, 0);
    columnsLayout->setSpacing(ruwa::ui::core::ThemeManager::instance().scaled(16));

    for (std::size_t index = 0; index < AnimationCategoryCount; ++index) {
        columnsLayout->addWidget(createAnimationCategory(index, content), 1);
    }

    scrollArea->setWidget(content);
    return scrollArea;
}

QWidget* ThemeEditorTab::createAnimationCategory(std::size_t categoryIndex, QWidget* parent)
{
    Q_ASSERT(categoryIndex < AnimationCategoryCount);

    auto* category = new QWidget(parent);
    category->setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QVBoxLayout(category);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme.scaled(8));

    auto* title = new QLabel(category);
    title->setFont(theme.font(ruwa::ui::core::ThemeFontRole::BodyLarge, QFont::Bold));
    m_animationCategoryTitles[categoryIndex] = title;
    layout->addWidget(title);

    auto* rowsHost = new QWidget(category);
    rowsHost->setAttribute(Qt::WA_TranslucentBackground);
    auto rows = std::make_unique<ruwa::ui::widgets::PropertyRowLayout>(rowsHost);

    if (categoryIndex == 0) {
        m_animationsToggle = new ruwa::ui::widgets::ToggleSwitch(rowsHost);
        m_animationSettingLabels[0] = rows->addRow(QString(), m_animationsToggle);
        connect(m_animationsToggle, &ruwa::ui::widgets::ToggleSwitch::toggled, this,
            [this](bool enabled) {
                if (m_syncingAnimationInputs) {
                    return;
                }
                m_editingTheme.animations.enabled = enabled;
                refreshPreviews();
                updateDirtyState();
            });

        m_animationSpeedInput = new ruwa::ui::widgets::NumericInputField(rowsHost);
        m_animationSpeedInput->setRange(ruwa::ui::core::WidgetStyleManager::kMinAnimationSpeed,
            ruwa::ui::core::WidgetStyleManager::kMaxAnimationSpeed);
        m_animationSpeedInput->setSingleStep(0.1);
        m_animationSpeedInput->setDecimals(1);
        m_animationSpeedInput->setSuffix(QStringLiteral("×"));
        m_animationSpeedInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_animationSettingLabels[2] = rows->addRow(QString(), m_animationSpeedInput);
        connect(m_animationSpeedInput, &ruwa::ui::widgets::NumericInputField::valueChanged, this,
            [this](double speed) {
                if (m_syncingAnimationInputs) {
                    return;
                }
                m_editingTheme.animations.speed = speed;
                refreshPreviews();
                updateDirtyState();
            });
    } else {
        m_canvasAnimationsToggle = new ruwa::ui::widgets::ToggleSwitch(rowsHost);
        m_animationSettingLabels[1] = rows->addRow(QString(), m_canvasAnimationsToggle);
        connect(m_canvasAnimationsToggle, &ruwa::ui::widgets::ToggleSwitch::toggled, this,
            [this](bool enabled) {
                if (m_syncingAnimationInputs) {
                    return;
                }
                m_editingTheme.animations.canvasEnabled = enabled;
                updateDirtyState();
            });
    }

    m_animationPropertyLayouts[categoryIndex] = std::move(rows);
    layout->addWidget(rowsHost);
    layout->addStretch();
    return category;
}

QWidget* ThemeEditorTab::createFontCategory(std::size_t categoryIndex, QWidget* parent)
{
    Q_ASSERT(categoryIndex < FontCategoryCount);

    auto* category = new QWidget(parent);
    category->setAttribute(Qt::WA_TranslucentBackground);
    auto* layout = new QVBoxLayout(category);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(theme.scaled(8));

    auto* title = new QLabel(category);
    title->setFont(theme.font(ruwa::ui::core::ThemeFontRole::BodyLarge, QFont::Bold));
    m_fontCategoryTitles[categoryIndex] = title;
    layout->addWidget(title);

    auto* rowsHost = new QWidget(category);
    rowsHost->setAttribute(Qt::WA_TranslucentBackground);
    auto rows = std::make_unique<ruwa::ui::widgets::PropertyRowLayout>(rowsHost);

    if (categoryIndex == 0) {
        for (std::size_t familyIndex = 0; familyIndex < FontFamilyCount; ++familyIndex) {
            auto* input = createFontFamilyInput(familyIndex, rowsHost);
            m_fontFamilyLabels[familyIndex] = rows->addRow(QString(), input);
        }
    } else {
        const std::size_t firstIndex = categoryIndex == 1 ? 0 : 8;
        const std::size_t lastIndex = categoryIndex == 1 ? 8 : FontSizeFieldCount;
        for (std::size_t sizeIndex = firstIndex; sizeIndex < lastIndex; ++sizeIndex) {
            auto* input = createFontSizeInput(sizeIndex, rowsHost);
            m_fontSizeLabels[sizeIndex] = rows->addRow(QString(), input);
        }
    }

    m_fontPropertyLayouts[categoryIndex] = std::move(rows);
    layout->addWidget(rowsHost);
    layout->addStretch();
    return category;
}

ruwa::ui::widgets::FontDropdownSelector* ThemeEditorTab::createFontFamilyInput(
    std::size_t familyIndex, QWidget* parent)
{
    Q_ASSERT(familyIndex < FontFamilyCount);

    auto* input = new ruwa::ui::widgets::FontDropdownSelector(parent);
    input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    input->setFontFamilies(QFontDatabase::families());
    input->setPopupMaxHeight(ruwa::ui::core::ThemeManager::instance().scaled(320));

    connect(input, &ruwa::ui::widgets::FontDropdownSelector::popupShown, this,
        [this, familyIndex]() {
            m_fontPreviewOriginalFamilies[familyIndex]
                = fontFamilyAt(m_editingTheme, familyIndex);
        });
    connect(input, &ruwa::ui::widgets::FontDropdownSelector::familyPreviewed, this,
        [this, familyIndex](const QString& family) {
            if (m_syncingFontInputs || family.isEmpty()) {
                return;
            }
            fontFamilyAt(m_editingTheme, familyIndex) = family;
            refreshPreviews();
        });
    connect(input, &ruwa::ui::widgets::FontDropdownSelector::previewCancelled, this,
        [this, familyIndex]() {
            if (m_syncingFontInputs || m_fontPreviewOriginalFamilies[familyIndex].isEmpty()) {
                return;
            }
            fontFamilyAt(m_editingTheme, familyIndex)
                = m_fontPreviewOriginalFamilies[familyIndex];
            refreshPreviews();
        });
    connect(input, &ruwa::ui::widgets::FontDropdownSelector::activated, this,
        [this, familyIndex](const QString& family) {
            if (m_syncingFontInputs || family.isEmpty()) {
                return;
            }
            fontFamilyAt(m_editingTheme, familyIndex) = family;
            m_fontPreviewOriginalFamilies[familyIndex] = family;
            refreshPreviews();
            updateDirtyState();
        });

    m_fontFamilyInputs[familyIndex] = input;
    return input;
}

ruwa::ui::widgets::NumericInputField* ThemeEditorTab::createFontSizeInput(
    std::size_t sizeIndex, QWidget* parent)
{
    Q_ASSERT(sizeIndex < FontSizeFieldCount);

    auto* input = new ruwa::ui::widgets::NumericInputField(parent);
    input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    input->setRange(kMinimumThemeFontSize, kMaximumThemeFontSize);
    input->setDecimals(0);
    input->setSingleStep(1.0);
    input->setSuffix(QStringLiteral("pt"));

    connect(input, &ruwa::ui::widgets::NumericInputField::valueChanged, this,
        [this, sizeIndex](double value) {
            if (m_syncingFontInputs) {
                return;
            }
            m_editingTheme.fonts.sizes.setValue(kFontSizeRoles[sizeIndex], qRound(value));
            refreshPreviews();
            updateDirtyState();
        });

    m_fontSizeInputs[sizeIndex] = input;
    return input;
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
                refreshPreviews();
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
        SettingsSectionDefinition { true, { SettingsPage::Animations } } };
}

QString ThemeEditorTab::settingsPageTitle(SettingsPage settingsPage) const
{
    switch (settingsPage) {
    case SettingsPage::ThemeColors:
        return tr("Colors");
    case SettingsPage::ThemeFont:
        return tr("Font");
    case SettingsPage::Animations:
        return tr("Animations");
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
        return {};
    case SettingsPage::Animations:
        return {};
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

QString ThemeEditorTab::fontFamilyLabel(std::size_t familyIndex) const
{
    Q_ASSERT(familyIndex < FontFamilyCount);
    return familyIndex == 0 ? tr("Headings") : tr("Interface text");
}

QString ThemeEditorTab::fontSizeFieldLabel(std::size_t sizeIndex) const
{
    Q_ASSERT(sizeIndex < FontSizeFieldCount);
    const std::array<QString, FontSizeFieldCount> labels { tr("Display"), tr("H0"), tr("H1"),
        tr("H2"), tr("H3"), tr("H4"), tr("H5"), tr("H6"), tr("Subtitle"),
        tr("Body Large"), tr("Label"), tr("Body"), tr("Small"), tr("Caption"), tr("Micro"),
        tr("Code") };
    return labels[sizeIndex];
}

QString ThemeEditorTab::animationCategoryTitle(std::size_t categoryIndex) const
{
    Q_ASSERT(categoryIndex < AnimationCategoryCount);
    return categoryIndex == 0 ? tr("Interface Motion") : tr("Canvas Motion");
}

QString ThemeEditorTab::animationSettingLabel(std::size_t settingIndex) const
{
    Q_ASSERT(settingIndex < AnimationSettingCount);
    const std::array<QString, AnimationSettingCount> labels {
        tr("Interface animations"), tr("Canvas animations"), tr("Speed multiplier")
    };
    return labels[settingIndex];
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

void ThemeEditorTab::syncFontInputs()
{
    m_syncingFontInputs = true;
    for (std::size_t index = 0; index < FontFamilyCount; ++index) {
        const QString& family = fontFamilyAt(m_editingTheme, index);
        m_fontPreviewOriginalFamilies[index] = family;
        if (m_fontFamilyInputs[index]) {
            m_fontFamilyInputs[index]->setCurrentFamily(family);
        }
    }
    for (std::size_t index = 0; index < FontSizeFieldCount; ++index) {
        if (m_fontSizeInputs[index]) {
            m_fontSizeInputs[index]->setValue(
                m_editingTheme.fonts.sizes.value(kFontSizeRoles[index]));
        }
    }
    m_syncingFontInputs = false;
}

void ThemeEditorTab::syncAnimationInputs()
{
    m_syncingAnimationInputs = true;
    if (m_animationsToggle) {
        m_animationsToggle->setCheckedInstant(m_editingTheme.animations.enabled);
    }
    if (m_canvasAnimationsToggle) {
        m_canvasAnimationsToggle->setCheckedInstant(m_editingTheme.animations.canvasEnabled);
    }
    if (m_animationSpeedInput) {
        m_animationSpeedInput->setValue(m_editingTheme.animations.speed);
    }
    m_syncingAnimationInputs = false;
}

void ThemeEditorTab::refreshPreviews()
{
    if (m_themesPreview) {
        m_themesPreview->setPreset(m_editingTheme);
    }
    if (m_animationsPreview) {
        m_animationsPreview->setPreset(m_editingTheme);
    }
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
    if (!dirty) {
        dirty = m_editingTheme.fonts.uiFont != m_savedTheme.fonts.uiFont
            || m_editingTheme.fonts.titleFont != m_savedTheme.fonts.titleFont
            || m_editingTheme.fonts.codeFont != m_savedTheme.fonts.codeFont
            || m_editingTheme.fonts.sizes != m_savedTheme.fonts.sizes;
    }
    if (!dirty) {
        dirty = m_editingTheme.animations.enabled != m_savedTheme.animations.enabled
            || m_editingTheme.animations.canvasEnabled
                != m_savedTheme.animations.canvasEnabled
            || !qFuzzyCompare(
                m_editingTheme.animations.speed, m_savedTheme.animations.speed);
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

    const std::array<QString, FontCategoryCount> fontCategoryTitles { tr("Font Families"),
        tr("Heading Sizes"), tr("Text Sizes") };
    for (std::size_t index = 0; index < FontCategoryCount; ++index) {
        if (m_fontCategoryTitles[index]) {
            m_fontCategoryTitles[index]->setText(fontCategoryTitles[index]);
        }
    }
    for (std::size_t index = 0; index < FontFamilyCount; ++index) {
        if (m_fontFamilyLabels[index]) {
            m_fontFamilyLabels[index]->setText(fontFamilyLabel(index));
        }
        if (m_fontFamilyInputs[index]) {
            m_fontFamilyInputs[index]->setPlaceholderText(tr("Font"));
        }
    }
    for (std::size_t index = 0; index < FontSizeFieldCount; ++index) {
        if (m_fontSizeLabels[index]) {
            m_fontSizeLabels[index]->setText(fontSizeFieldLabel(index));
        }
    }

    for (std::size_t index = 0; index < AnimationCategoryCount; ++index) {
        if (m_animationCategoryTitles[index]) {
            m_animationCategoryTitles[index]->setText(animationCategoryTitle(index));
        }
    }
    for (std::size_t index = 0; index < AnimationSettingCount; ++index) {
        if (m_animationSettingLabels[index]) {
            m_animationSettingLabels[index]->setText(animationSettingLabel(index));
        }
    }
    for (auto& rows : m_animationPropertyLayouts) {
        if (rows) {
            rows->refreshLabelMetrics();
        }
    }
    for (auto& rows : m_fontPropertyLayouts) {
        if (rows) {
            rows->refreshLabelMetrics();
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
    for (QLabel* title : m_fontCategoryTitles) {
        if (title) {
            title->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
        }
    }
    for (QLabel* label : m_fontFamilyLabels) {
        if (label) {
            label->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
        }
    }
    for (QLabel* label : m_fontSizeLabels) {
        if (label) {
            label->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
        }
    }
    for (QLabel* title : m_animationCategoryTitles) {
        if (title) {
            title->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
        }
    }
    for (QLabel* label : m_animationSettingLabels) {
        if (label) {
            label->setStyleSheet(QStringLiteral("color: %1;").arg(colors.textMuted.name()));
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

    const auto refreshPreviewPageMargins = [&theme](QWidget* preview) {
        if (!preview) {
            return;
        }
        if (QWidget* page = preview->parentWidget(); page && page->layout()) {
            page->layout()->setContentsMargins(
                theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
        }
    };
    refreshPreviewPageMargins(m_themesPreview);
    refreshPreviewPageMargins(m_animationsPreview);

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

    for (QLabel* title : m_fontCategoryTitles) {
        if (!title) {
            continue;
        }
        title->setFont(theme.font(ruwa::ui::core::ThemeFontRole::BodyLarge, QFont::Bold));

        if (QWidget* category = title->parentWidget(); category && category->layout()) {
            category->layout()->setContentsMargins(0, 0, 0, 0);
            category->layout()->setSpacing(theme.scaled(8));
            if (QWidget* content = category->parentWidget(); content && content->layout()) {
                content->layout()->setSpacing(theme.scaled(16));
            }
        }
    }
    for (QLabel* label : m_fontFamilyLabels) {
        if (label) {
            label->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
        }
    }
    for (QLabel* label : m_fontSizeLabels) {
        if (label) {
            label->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
        }
    }
    for (auto& rows : m_fontPropertyLayouts) {
        if (rows) {
            rows->refreshLabelMetrics();
        }
    }

    for (QLabel* title : m_animationCategoryTitles) {
        if (!title) {
            continue;
        }
        title->setFont(theme.font(ruwa::ui::core::ThemeFontRole::BodyLarge, QFont::Bold));

        if (QWidget* category = title->parentWidget(); category && category->layout()) {
            category->layout()->setContentsMargins(0, 0, 0, 0);
            category->layout()->setSpacing(theme.scaled(8));
            if (QWidget* content = category->parentWidget(); content && content->layout()) {
                content->layout()->setSpacing(theme.scaled(16));
            }
        }
    }
    for (QLabel* label : m_animationSettingLabels) {
        if (label) {
            label->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
        }
    }
    for (auto& rows : m_animationPropertyLayouts) {
        if (rows) {
            rows->refreshLabelMetrics();
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
