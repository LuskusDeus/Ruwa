// SPDX-License-Identifier: MPL-2.0

// ShortcutManagerTab.cpp
#include "features/settings/shortcuts/ShortcutManagerTab.h"
#include "features/settings/shortcuts/ShortcutRowWidget.h"
#include "commands/CommandRegistry.h"
#include "shared/i18n/CommandLocalization.h"
#include "commands/ShortcutManager.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "features/settings/shortcuts/CategoryItemWidget.h"
#include "features/settings/shortcuts/PresetImportDropZone.h"
#include "features/settings/shortcuts/PresetItemWidget.h"
#include "features/settings/shortcuts/ShortcutPresetStore.h"
#include "shared/widgets/inputs/SearchBar.h"
#include "shared/widgets/CapsuleButton.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "features/settings/SettingsCategory.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"
#include "shared/utils/FileDialogMemory.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QVBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPen>
#include <QSet>
#include <QSizePolicy>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

namespace ruwa::ui::tabs {

using namespace ruwa::ui::core;
using namespace ruwa::ui::widgets;

namespace {
const int BASE_MAIN_MARGIN_H = 40;
const int BASE_MAIN_MARGIN_V = 30;
const int BASE_MAIN_SPACING = 16;
const int BASE_HEADER_SPACING = 16;
const int BASE_SEARCH_BAR_WIDTH = 250;
const int BASE_SCROLL_SPACING = 24;
const int BASE_TITLE_FONT_SIZE = 18;
const int BASE_SECTION_HEADER_FONT_SIZE = 12;
const int BASE_SECTION_HEADER_SPACING = 8;
const int BASE_SHORTCUTS_TITLE_FONT_SIZE = 22;
const int BASE_SHORTCUTS_META_FONT_SIZE = 10;
const int BASE_SHORTCUTS_HEADER_GAP = 10;
const int BASE_DIVIDER_VSPACE_TOP = 6;
const int BASE_DIVIDER_VSPACE_BOTTOM = 8;

QIcon makePlusIcon(int basePx)
{
    const int px = qMax(8, basePx);
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal stroke = qMax(1.5, px / 8.0);
    p.setPen(QPen(Qt::white, stroke, Qt::SolidLine, Qt::RoundCap));
    const qreal m = px * 0.22;
    p.drawLine(QPointF(px / 2.0, m), QPointF(px / 2.0, px - m));
    p.drawLine(QPointF(m, px / 2.0), QPointF(px - m, px / 2.0));
    return QIcon(pm);
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

ShortcutManagerTab::ShortcutManagerTab(QWidget* parent)
    : ruwa::core::BaseTab(parent)
{
}

ShortcutManagerTab::ShortcutManagerTab(const QUuid& id, QWidget* parent)
    : ruwa::core::BaseTab(id, parent)
{
}

void ShortcutManagerTab::onApplyThemeRefresh(std::function<void()> finished, bool showLoading)
{
    Q_UNUSED(showLoading);
    const QUuid keepId = id();
    QWidget* parentWidgetPtr = parentWidget();
    recreateForThemeRefresh(
        [keepId, parentWidgetPtr]() -> ruwa::core::BaseTab* {
            return new ShortcutManagerTab(keepId, parentWidgetPtr);
        },
        std::move(finished));
}

ShortcutManagerTab::~ShortcutManagerTab()
{
    auto& sm = ruwa::core::ShortcutManager::instance();
    disconnect(&sm, &ruwa::core::ShortcutManager::shortcutChanged, this,
        &ShortcutManagerTab::onShortcutManagerChanged);
}

void ShortcutManagerTab::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    // Отступы 16:9 и область под заголовком должны иметь цвет background, а не surface
    QPainter painter(this);
    painter.fillRect(rect(), ThemeManager::instance().colors().background);
}

void ShortcutManagerTab::resizeEvent(QResizeEvent* event)
{
    ruwa::core::BaseTab::resizeEvent(event);

    if (!layout()) {
        return;
    }

    applyAspectRatioMargins();
}

void ShortcutManagerTab::applyAspectRatioMargins()
{
    if (!m_mainLayout) {
        return;
    }

    auto& theme = ThemeManager::instance();
    const int marginH = theme.scaled(BASE_MAIN_MARGIN_H);
    const int marginV = theme.scaled(BASE_MAIN_MARGIN_V);

    const float targetAspectRatio = 14.0f / 9.0f;
    QSize currentSize = size();
    int targetWidth = static_cast<int>(currentSize.height() * targetAspectRatio);
    int sideMargin = 0;

    if (currentSize.width() > targetWidth) {
        sideMargin = (currentSize.width() - targetWidth) / 2;
    }

    m_mainLayout->setContentsMargins(marginH + sideMargin, marginV, marginH + sideMargin, marginV);
}

void ShortcutManagerTab::onInitialize()
{
    createLayout();
}

// ============================================================================
// Layout
// ============================================================================

void ShortcutManagerTab::createLayout()
{
    auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    m_mainLayout = new QVBoxLayout(this);

    // === Header (title + search, same as SettingsContent) ===
    m_headerLayout = new QHBoxLayout();

    m_titleLabel = new QLabel(tr("Keyboard Shortcuts"), this);
    m_titleLabel->setFont(colors.fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    m_headerLayout->addWidget(m_titleLabel);

    m_headerLayout->addStretch();

    m_searchBar = new SearchBar(this);
    m_searchBar->setClickOutsideClearsFocus(true);
    m_searchBar->setPlaceholder(tr("Search shortcuts..."));
    m_searchBar->setFixedWidth(theme.scaled(BASE_SEARCH_BAR_WIDTH));
    connect(m_searchBar, &SearchBar::textChanged, this, &ShortcutManagerTab::onSearchTextChanged);
    m_headerLayout->addWidget(m_searchBar);

    m_headerLayout->setSpacing(theme.scaled(BASE_HEADER_SPACING));
    m_mainLayout->addLayout(m_headerLayout);

    // === Create categories with shortcut rows ===
    createCategories();

    // === Shortcuts body: AnimatedStackedWidget with one scroll page per item ===
    m_categoryStack = new AnimatedStackedWidget(this);
    m_categoryStack->setSlideOrientation(AnimatedStackedWidget::SlideOrientation::Vertical);

    auto buildPage = [&](QWidget* body) {
        auto* page = new SmoothScrollArea(m_categoryStack);
        page->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        page->setScrollBarAlwaysReserved(true);
        page->setScrollBarMargin(4);

        auto* content = new QWidget();
        content->setAutoFillBackground(true);
        QPalette pal = content->palette();
        pal.setColor(QPalette::Window, colors.background);
        content->setPalette(pal);

        auto* pageLayout = new QVBoxLayout(content);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(theme.scaled(BASE_SCROLL_SPACING));
        pageLayout->addWidget(body);
        pageLayout->addStretch();

        page->setWidget(content);
        m_categoryStack->addWidget(page);
        m_categoryPages.append(page);
    };

    // Page 0: "All shortcuts" — duplicate rows for every command, grouped by category.
    auto* allBody = new QWidget(this);
    auto* allLayout = new QVBoxLayout(allBody);
    allLayout->setContentsMargins(0, 0, 0, 0);
    allLayout->setSpacing(theme.scaled(BASE_SCROLL_SPACING));
    {
        auto& registry = ruwa::core::CommandRegistry::instance();
        auto& shortcuts = ruwa::core::ShortcutManager::instance();
        auto& loc = ruwa::i18n::CommandLocalization::instance();
        for (int i = 0; i < m_categories.size(); ++i) {
            auto* group = new SettingsCategory(m_categoryDisplayNames[i], allBody);
            const QIcon& catIcon = m_itemIcons.value(i + 1);
            if (!catIcon.isNull()) {
                group->setIcon(catIcon);
            }
            const auto cmds = registry.commandsInCategory(m_categorySourceNames[i]);
            for (auto* cmd : cmds) {
                const auto& info = cmd->info();
                const QString title
                    = loc.title(info.id).isEmpty() ? info.title : loc.title(info.id);
                const QString desc = loc.description(info.id).isEmpty() ? info.description
                                                                        : loc.description(info.id);
                auto* row = new ShortcutRowWidget(info.id, title, desc,
                    shortcuts.shortcutFor(info.id), shortcuts.defaultShortcutFor(info.id), allBody);
                connect(row, &ShortcutRowWidget::shortcutChanged, this,
                    &ShortcutManagerTab::onShortcutChanged);
                connect(row, &ShortcutRowWidget::resetRequested, this,
                    &ShortcutManagerTab::onResetRequested);
                group->addSettingsWidget(row);
                m_rowWidgets.insert(info.id, row);
            }
            allLayout->addWidget(group);
            m_allShortcutGroups.append(group);
        }
    }
    buildPage(allBody);

    // Pages 1..N: real categories.
    for (auto* category : m_categories) {
        buildPage(category);
    }

    // === Categories body: list of CategoryItemWidget ===
    auto* categoriesBody = new QWidget(this);
    auto* categoriesBodyLayout = new QVBoxLayout(categoriesBody);
    categoriesBodyLayout->setContentsMargins(0, 0, 0, 0);
    categoriesBodyLayout->setSpacing(theme.scaled(8));

    for (int i = 0; i < m_itemTitles.size(); ++i) {
        auto* item = new CategoryItemWidget(categoriesBody);
        item->setTitle(m_itemTitles[i]);
        item->setSubtitle(m_itemSubtitles[i]);
        item->setIcon(m_itemIcons[i]);
        item->setCount(m_itemCounts[i]);
        const int index = i;
        connect(
            item, &CategoryItemWidget::clicked, this, [this, index] { onCategorySelected(index); });
        categoriesBodyLayout->addWidget(item);
        m_categoryItems.append(item);
    }
    categoriesBodyLayout->addStretch();

    // === Content split: presets (20%) | categories (15%) | shortcuts (65%) ===
    m_contentLayout = new QHBoxLayout();
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));

    auto makeSectionHeader = [&](const QString& text) {
        auto* label = new QLabel(text, this);
        QFont f = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SECTION_HEADER_FONT_SIZE));
        f.setWeight(QFont::DemiBold);
        label->setFont(f);
        label->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
        return label;
    };

    auto makePanel = [&](QLabel*& headerOut, const QString& headerText, QWidget* body) {
        auto* panel = new QWidget(this);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(theme.scaled(BASE_SECTION_HEADER_SPACING));
        headerOut = makeSectionHeader(headerText);
        layout->addWidget(headerOut);
        layout->addWidget(body, 1);
        return panel;
    };

    // === Presets panel: header (label + "New" button) over a vertical list of cards ===
    m_presetsPanel = new QWidget(this);
    auto* presetsPanelLayout = new QVBoxLayout(m_presetsPanel);
    presetsPanelLayout->setContentsMargins(0, 0, 0, 0);
    presetsPanelLayout->setSpacing(theme.scaled(BASE_SECTION_HEADER_SPACING));

    auto* presetsHeaderRow = new QHBoxLayout();
    presetsHeaderRow->setContentsMargins(0, 0, 0, 0);
    presetsHeaderRow->setSpacing(theme.scaled(8));

    m_presetsHeaderLabel = makeSectionHeader(tr("Presets"));

    // Primary (banner) variant — Action variant doesn't render icons and ignores setSizeScale.
    m_newPresetButton
        = new CapsuleButton(tr("New"), CapsuleButton::Variant::Primary, m_presetsPanel);
    // Keep readable text but reduce height. Default min-width (168) keeps the
    // capsule visually elongated rather than squeezed around the short label.
    m_newPresetButton->setSizeScale(0.78);
    m_newPresetButton->setIcon(makePlusIcon(theme.scaled(14)));
    connect(
        m_newPresetButton, &QPushButton::clicked, this, &ShortcutManagerTab::onNewPresetClicked);

    presetsHeaderRow->addWidget(m_presetsHeaderLabel, 0, Qt::AlignVCenter);
    presetsHeaderRow->addStretch();
    presetsHeaderRow->addWidget(m_newPresetButton, 0, Qt::AlignVCenter);

    auto* presetsBody = new QWidget(m_presetsPanel);
    m_presetsListLayout = new QVBoxLayout(presetsBody);
    m_presetsListLayout->setContentsMargins(0, 0, 0, 0);
    m_presetsListLayout->setSpacing(theme.scaled(8));

    m_importDropZone = new PresetImportDropZone(m_presetsPanel);
    connect(m_importDropZone, &PresetImportDropZone::clicked, this,
        &ShortcutManagerTab::onPresetImportClicked);
    connect(m_importDropZone, &PresetImportDropZone::fileDropped, this,
        &ShortcutManagerTab::onPresetFileDropped);

    presetsPanelLayout->addLayout(presetsHeaderRow);
    presetsPanelLayout->addWidget(presetsBody, 1);
    presetsPanelLayout->addWidget(m_importDropZone, 0);

    // Initial selection = Default built-in preset.
    m_selectedPresetId
        = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance().defaultPreset().id;
    rebuildPresetsList();

    connect(&ruwa::features::settings::shortcuts::ShortcutPresetStore::instance(),
        &ruwa::features::settings::shortcuts::ShortcutPresetStore::changed, this,
        &ShortcutManagerTab::onPresetStoreChanged);

    m_categoriesPanel = makePanel(m_categoriesHeaderLabel, tr("Categories"), categoriesBody);

    // === Shortcuts panel: title + meta + reset button, divider, body ===
    m_shortcutsPanel = new QWidget(this);
    auto* shortcutsPanelLayout = new QVBoxLayout(m_shortcutsPanel);
    shortcutsPanelLayout->setContentsMargins(0, 0, 0, 0);
    shortcutsPanelLayout->setSpacing(0);

    auto* shortcutsHeaderRow = new QHBoxLayout();
    shortcutsHeaderRow->setContentsMargins(0, 0, 0, 0);
    shortcutsHeaderRow->setSpacing(theme.scaled(BASE_SHORTCUTS_HEADER_GAP));

    m_shortcutsHeaderLabel = new QLabel(m_shortcutsPanel);
    {
        QFont f = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SHORTCUTS_TITLE_FONT_SIZE));
        f.setWeight(QFont::DemiBold);
        m_shortcutsHeaderLabel->setFont(f);
    }
    m_shortcutsHeaderLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));

    m_shortcutsMetaLabel = new QLabel(m_shortcutsPanel);
    QFont metaFont = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SHORTCUTS_META_FONT_SIZE));
    m_shortcutsMetaLabel->setFont(metaFont);
    m_shortcutsMetaLabel->setStyleSheet(
        QString("QLabel { color: %1; }").arg(colors.textMuted.name()));

    m_resetSectionButton = new CapsuleButton(
        tr("Reset section"), CapsuleButton::Variant::Secondary, m_shortcutsPanel);
    m_resetSectionButton->setBaseMinimumWidth(0);
    m_resetSectionButton->setIcon(theme.icons().getIcon(IconProvider::StandardIcon::UndoArrow));
    m_resetSectionButton->setSizeScale(0.78);
    m_resetSectionButton->syncSizeToText();
    connect(m_resetSectionButton, &QPushButton::clicked, this,
        &ShortcutManagerTab::onResetSectionClicked);

    shortcutsHeaderRow->addWidget(m_shortcutsHeaderLabel, 0, Qt::AlignVCenter);
    shortcutsHeaderRow->addWidget(m_shortcutsMetaLabel, 0, Qt::AlignVCenter);
    shortcutsHeaderRow->addStretch();
    shortcutsHeaderRow->addWidget(m_resetSectionButton, 0, Qt::AlignVCenter);

    m_shortcutsDivider = new QWidget(m_shortcutsPanel);
    m_shortcutsDivider->setFixedHeight(1);
    m_shortcutsDivider->setAutoFillBackground(true);
    {
        QPalette pal = m_shortcutsDivider->palette();
        pal.setColor(QPalette::Window, colors.border);
        m_shortcutsDivider->setPalette(pal);
    }

    shortcutsPanelLayout->addLayout(shortcutsHeaderRow);
    shortcutsPanelLayout->addSpacing(theme.scaled(BASE_DIVIDER_VSPACE_TOP));
    shortcutsPanelLayout->addWidget(m_shortcutsDivider);
    shortcutsPanelLayout->addSpacing(theme.scaled(BASE_DIVIDER_VSPACE_BOTTOM));
    shortcutsPanelLayout->addWidget(m_categoryStack, 1);

    if (!m_categoryItems.isEmpty()) {
        m_selectedItemIndex = 0;
        m_categoryItems.first()->setSelected(true);
        m_categoryStack->setCurrentIndexWithoutAnimation(0);
        m_shortcutsHeaderLabel->setText(m_itemTitles.first());
        updateShortcutsHeaderMeta();
    }

    m_contentLayout->addWidget(m_presetsPanel, 20);
    m_contentLayout->addWidget(m_categoriesPanel, 15);
    m_contentLayout->addWidget(m_shortcutsPanel, 65);

    m_mainLayout->addLayout(m_contentLayout, 1);

    // === Apply initial sizes and theme ===
    updateScaledSizes();
    updateThemeColors();

    // === Connect signals ===
    connect(&ruwa::core::ShortcutManager::instance(), &ruwa::core::ShortcutManager::shortcutChanged,
        this, &ShortcutManagerTab::onShortcutManagerChanged);

    connect(&theme, &ThemeManager::themeChanged, this, &ShortcutManagerTab::onThemeChanged);
}

// ============================================================================
// Categories
// ============================================================================

void ShortcutManagerTab::createCategories()
{
    auto& registry = ruwa::core::CommandRegistry::instance();
    auto& shortcuts = ruwa::core::ShortcutManager::instance();
    auto& icons = ThemeManager::instance().icons();

    QStringList categoryNames = registry.categories();
    categoryNames.removeAll(QStringLiteral("Easter Eggs"));
    categoryNames.sort(Qt::CaseInsensitive);

    // Map category names to icons (only valid StandardIcon enum values)
    static const QHash<QString, IconProvider::StandardIcon> categoryIcons = {
        { "File", IconProvider::StandardIcon::FileNew },
        { "Edit", IconProvider::StandardIcon::Edit },
        { "View", IconProvider::StandardIcon::Appearance },
        { "Navigation", IconProvider::StandardIcon::Home },
        { "Tab", IconProvider::StandardIcon::BasicFile },
        { "Layers", IconProvider::StandardIcon::Folder },
        { "Transform", IconProvider::StandardIcon::Move },
        { "Tools", IconProvider::StandardIcon::Brush },
    };

    auto& loc = ruwa::i18n::CommandLocalization::instance();
    int totalCount = 0;
    for (const QString& categoryName : categoryNames) {
        const QString displayName = loc.categoryDisplayName(categoryName).isEmpty()
            ? categoryName
            : loc.categoryDisplayName(categoryName);
        auto* category = new SettingsCategory(displayName, this);
        category->setHeaderVisible(false);

        auto iconIt = categoryIcons.find(categoryName);
        QIcon catIcon;
        if (iconIt != categoryIcons.end()) {
            catIcon = icons.getIcon(iconIt.value());
            category->setIcon(catIcon);
        }

        const auto commands = registry.commandsInCategory(categoryName);

        QStringList firstTitles;
        for (auto* cmd : commands) {
            const auto& info = cmd->info();
            const QString title = loc.title(info.id).isEmpty() ? info.title : loc.title(info.id);
            const QString desc
                = loc.description(info.id).isEmpty() ? info.description : loc.description(info.id);
            const QKeySequence currentShortcut = shortcuts.shortcutFor(info.id);
            const QKeySequence defaultShortcut = shortcuts.defaultShortcutFor(info.id);

            auto* row = new ShortcutRowWidget(
                info.id, title, desc, currentShortcut, defaultShortcut, this);

            connect(row, &ShortcutRowWidget::shortcutChanged, this,
                &ShortcutManagerTab::onShortcutChanged);
            connect(row, &ShortcutRowWidget::resetRequested, this,
                &ShortcutManagerTab::onResetRequested);

            category->addSettingsWidget(row);
            m_rowWidgets.insert(info.id, row);

            if (firstTitles.size() < 2) {
                firstTitles << title;
            }
        }

        QString subtitle = firstTitles.join(QStringLiteral(" · "));
        if (commands.size() > firstTitles.size() && !subtitle.isEmpty()) {
            subtitle += QStringLiteral("…");
        }

        m_categories.append(category);
        m_categoryDisplayNames.append(displayName);
        m_categorySourceNames.append(categoryName);
        m_itemTitles.append(displayName);
        m_itemSubtitles.append(subtitle);
        m_itemIcons.append(catIcon);
        m_itemCounts.append(static_cast<int>(commands.size()));
        totalCount += static_cast<int>(commands.size());
    }

    // Prepend synthetic "All shortcuts" entry (index 0).
    m_itemTitles.prepend(tr("All shortcuts"));
    m_itemSubtitles.prepend(tr("Everything in this preset"));
    m_itemIcons.prepend(icons.getIcon(IconProvider::StandardIcon::List));
    m_itemCounts.prepend(totalCount);
}

// ============================================================================
// Search
// ============================================================================

void ShortcutManagerTab::onSearchTextChanged(const QString& text)
{
    m_searchText = text.trimmed().toLower();
    applySearchFilter();
}

void ShortcutManagerTab::applySearchFilter()
{
    const QString& search = m_searchText;

    auto applyToCategory = [&](SettingsCategory* category) {
        if (!category) {
            return;
        }

        bool anyCategoryMatch = false;
        bool categoryNameMatch = category->title().toLower().contains(search);

        for (QWidget* widget : category->settingsWidgets()) {
            auto* row = qobject_cast<ShortcutRowWidget*>(widget);
            if (!row)
                continue;

            bool visible = search.isEmpty() || categoryNameMatch || row->matchesSearch(search);
            row->setVisible(visible);
            if (visible)
                anyCategoryMatch = true;
        }

        // Hide entire category if no rows match
        category->setVisible(search.isEmpty() || anyCategoryMatch);
    };

    for (auto* category : m_categories) {
        applyToCategory(category);
    }
    for (auto* group : m_allShortcutGroups) {
        applyToCategory(group);
    }

    if (!search.isEmpty() && m_selectedItemIndex != 0 && !m_categoryItems.isEmpty()) {
        onCategorySelected(0);
    }
}

// ============================================================================
// Shortcut Editing
// ============================================================================

void ShortcutManagerTab::onShortcutChanged(
    const QString& commandId, const QKeySequence& newShortcut)
{
    auto& sm = ruwa::core::ShortcutManager::instance();

    // Check for conflicts
    if (sm.isShortcutInUse(newShortcut, commandId) && !newShortcut.isEmpty()) {
        const QString conflicting = sm.commandForShortcut(newShortcut);
        auto* conflictCmd = ruwa::core::CommandRegistry::instance().command(conflicting);
        QString conflictName;
        if (conflictCmd) {
            const QString locTitle = ruwa::i18n::CommandLocalization::instance().title(conflicting);
            conflictName = locTitle.isEmpty() ? conflictCmd->info().title : locTitle;
        } else {
            conflictName = conflicting;
        }

        QMessageBox::warning(this, tr("Shortcut Conflict"),
            tr("This shortcut is already used by \"%1\".\nPlease choose a different shortcut.")
                .arg(conflictName));

        // Revert to current shortcut in all rows for this command
        for (auto* row : m_rowWidgets.values(commandId)) {
            row->setShortcut(sm.shortcutFor(commandId));
        }
        return;
    }

    sm.setShortcut(commandId, newShortcut);
    sm.saveToSettings();
}

void ShortcutManagerTab::onCategorySelected(int index)
{
    if (index < 0 || index >= m_categoryItems.size() || index == m_selectedItemIndex) {
        return;
    }
    if (m_selectedItemIndex >= 0 && m_selectedItemIndex < m_categoryItems.size()) {
        m_categoryItems[m_selectedItemIndex]->setSelected(false);
    }
    m_selectedItemIndex = index;
    m_categoryItems[index]->setSelected(true);

    if (m_categoryStack) {
        m_categoryStack->setCurrentIndex(index);
    }
    if (m_shortcutsHeaderLabel) {
        m_shortcutsHeaderLabel->setText(m_itemTitles[index]);
    }
    updateShortcutsHeaderMeta();
}

QStringList ShortcutManagerTab::commandIdsForItem(int itemIndex) const
{
    auto& registry = ruwa::core::CommandRegistry::instance();
    QStringList ids;
    if (itemIndex <= 0) {
        // "All shortcuts" — every command in every visible category.
        for (const QString& src : m_categorySourceNames) {
            for (auto* cmd : registry.commandsInCategory(src)) {
                ids << cmd->info().id;
            }
        }
    } else {
        const int catIndex = itemIndex - 1;
        if (catIndex >= 0 && catIndex < m_categorySourceNames.size()) {
            for (auto* cmd : registry.commandsInCategory(m_categorySourceNames[catIndex])) {
                ids << cmd->info().id;
            }
        }
    }
    return ids;
}

void ShortcutManagerTab::updateShortcutsHeaderMeta()
{
    if (!m_shortcutsMetaLabel || m_selectedItemIndex < 0) {
        return;
    }

    auto& sm = ruwa::core::ShortcutManager::instance();
    const QStringList ids = commandIdsForItem(m_selectedItemIndex);
    int customized = 0;
    for (const QString& id : ids) {
        if (sm.shortcutFor(id) != sm.defaultShortcutFor(id)) {
            ++customized;
        }
    }

    const QString sep = QStringLiteral(" · "); // middle dot
    const QString shortcutWord = ids.size() == 1 ? tr("shortcut") : tr("shortcuts");
    const QString customizedWord = tr("customized");
    const QString meta = QStringLiteral("%1 %2%3%4 %5")
                             .arg(ids.size())
                             .arg(shortcutWord)
                             .arg(sep)
                             .arg(customized)
                             .arg(customizedWord);
    m_shortcutsMetaLabel->setText(meta);

    if (m_resetSectionButton) {
        m_resetSectionButton->setEnabled(customized > 0);
    }
}

// ============================================================================
// Presets
// ============================================================================

QHash<QString, QKeySequence> ShortcutManagerTab::captureCurrentCustomBindings() const
{
    auto& sm = ruwa::core::ShortcutManager::instance();
    QHash<QString, QKeySequence> out;
    for (auto it = m_rowWidgets.constBegin(); it != m_rowWidgets.constEnd(); ++it) {
        const QString& id = it.key();
        if (sm.hasCustomShortcut(id)) {
            out.insert(id, sm.shortcutFor(id));
        }
    }
    return out;
}

void ShortcutManagerTab::applyPreset(const QUuid& presetId)
{
    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    auto preset = store.presetById(presetId);
    if (!preset)
        return;

    auto& sm = ruwa::core::ShortcutManager::instance();

    // Reset every command to its default, then apply preset overrides.
    QStringList allIds;
    allIds.reserve(m_rowWidgets.size());
    for (auto it = m_rowWidgets.constBegin(); it != m_rowWidgets.constEnd(); ++it) {
        if (!allIds.contains(it.key()))
            allIds << it.key();
    }
    for (const QString& id : allIds) {
        sm.resetShortcut(id);
    }
    for (auto it = preset->bindings.constBegin(); it != preset->bindings.constEnd(); ++it) {
        sm.setShortcut(it.key(), it.value());
    }
    sm.saveToSettings();
    updateShortcutsHeaderMeta();
}

void ShortcutManagerTab::rebuildPresetsList()
{
    if (!m_presetsListLayout)
        return;

    // Tear down old items.
    for (auto* item : m_presetItems) {
        m_presetsListLayout->removeWidget(item);
        item->deleteLater();
    }
    m_presetItems.clear();
    m_presetIds.clear();
    // Remove any trailing stretch from a previous build.
    while (m_presetsListLayout->count() > 0) {
        QLayoutItem* it = m_presetsListLayout->takeAt(0);
        delete it;
    }

    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    const auto presets = store.allPresets();

    auto* parentBody = qobject_cast<QWidget*>(m_presetsListLayout->parent());

    bool selectionExists = false;
    for (int i = 0; i < presets.size(); ++i) {
        const auto& p = presets[i];
        auto* card = new PresetItemWidget(parentBody);
        card->setTitle(p.name);
        card->setShortcutCount(countBoundShortcutsInPreset(p.id));
        card->setKind(
            p.isBuiltIn ? PresetItemWidget::Kind::BuiltIn : PresetItemWidget::Kind::Custom);
        // Use immediate-set (no animation) so rebuilds don't re-trigger the
        // selection transition every time and cause flicker.
        card->setSelectedImmediate(p.id == m_selectedPresetId);
        if (p.id == m_selectedPresetId)
            selectionExists = true;

        const int idx = i;
        connect(card, &PresetItemWidget::clicked, this, [this, idx] { onPresetSelected(idx); });
        connect(card, &PresetItemWidget::deleteRequested, this,
            [this, idx] { onPresetDeleteRequested(idx); });
        connect(card, &PresetItemWidget::exportRequested, this,
            [this, idx] { onPresetExportRequested(idx); });
        connect(card, &PresetItemWidget::renamed, this,
            [this, idx](const QString& newName) { onPresetRenamed(idx, newName); });

        m_presetsListLayout->addWidget(card);
        m_presetItems.append(card);
        m_presetIds.append(p.id);
    }
    m_presetsListLayout->addStretch();

    if (!selectionExists && !m_presetIds.isEmpty()) {
        m_selectedPresetId = m_presetIds.first();
        m_presetItems.first()->setSelectedImmediate(true);
    }
}

int ShortcutManagerTab::countBoundShortcutsInPreset(const QUuid& presetId) const
{
    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    auto preset = store.presetById(presetId);
    if (!preset)
        return 0;

    auto& sm = ruwa::core::ShortcutManager::instance();
    // Use the universe of commands we know about (those that built rows in the tab).
    int bound = 0;
    QSet<QString> seen;
    for (auto it = m_rowWidgets.constBegin(); it != m_rowWidgets.constEnd(); ++it) {
        const QString& id = it.key();
        if (seen.contains(id))
            continue;
        seen.insert(id);

        QKeySequence effective;
        if (preset->bindings.contains(id)) {
            effective = preset->bindings.value(id);
        } else {
            effective = sm.defaultShortcutFor(id);
        }
        if (!effective.isEmpty())
            ++bound;
    }
    return bound;
}

void ShortcutManagerTab::onPresetSelected(int index)
{
    if (index < 0 || index >= m_presetIds.size())
        return;
    const QUuid id = m_presetIds[index];
    if (id == m_selectedPresetId)
        return;

    for (int i = 0; i < m_presetItems.size(); ++i) {
        m_presetItems[i]->setSelected(i == index);
    }
    m_selectedPresetId = id;
    applyPreset(id);
}

void ShortcutManagerTab::onPresetRenamed(int index, const QString& newName)
{
    if (index < 0 || index >= m_presetIds.size())
        return;
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty())
        return;
    const QUuid id = m_presetIds[index];
    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    auto preset = store.presetById(id);
    if (!preset || preset->isBuiltIn || preset->name == trimmed)
        return;

    ruwa::features::settings::shortcuts::ShortcutPreset updated = *preset;
    updated.name = trimmed;
    store.updateCustomPreset(updated);
}

void ShortcutManagerTab::onPresetDeleteRequested(int index)
{
    if (index < 0 || index >= m_presetIds.size())
        return;
    const QUuid id = m_presetIds[index];
    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    auto preset = store.presetById(id);
    if (!preset || preset->isBuiltIn)
        return;

    const QString msg = tr("Delete preset \"%1\"?").arg(preset->name);
    if (!ruwa::ui::widgets::MessagePopupManager::showBlocking(
            this, msg, tr("Yes"), tr("No"), 360, true)) {
        return;
    }

    const bool wasSelected = (id == m_selectedPresetId);
    store.removeCustomPreset(id);
    if (wasSelected) {
        m_selectedPresetId = store.defaultPreset().id;
        applyPreset(m_selectedPresetId);
    }
    // rebuild happens via the store.changed() signal.
}

void ShortcutManagerTab::onNewPresetClicked()
{
    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    ruwa::features::settings::shortcuts::ShortcutPreset p;
    // Pre-generate the UUID so we can mark it selected BEFORE addCustomPreset
    // emits changed() (which triggers rebuildPresetsList synchronously).
    p.id = QUuid::createUuid();
    p.name = store.suggestUniqueName(tr("Custom"));
    p.bindings = captureCurrentCustomBindings();
    m_selectedPresetId = p.id;
    store.addCustomPreset(p);
}

void ShortcutManagerTab::onPresetStoreChanged()
{
    rebuildPresetsList();
}

void ShortcutManagerTab::onPresetExportRequested(int index)
{
    if (index < 0 || index >= m_presetIds.size())
        return;
    exportPresetToFile(m_presetIds[index]);
}

void ShortcutManagerTab::onPresetImportClicked()
{
    const QString path = ruwa::shared::filedialog::getOpenFileName(this,
        ruwa::shared::filedialog::category::kShortcuts, tr("Import shortcut preset"),
        tr("Ruwa shortcut preset (*.json)"));
    if (path.isEmpty())
        return;
    importPresetFromFile(path);
}

void ShortcutManagerTab::onPresetFileDropped(const QString& path)
{
    importPresetFromFile(path);
}

void ShortcutManagerTab::exportPresetToFile(const QUuid& presetId)
{
    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    auto preset = store.presetById(presetId);
    if (!preset)
        return;

    QString defaultName = preset->name.trimmed();
    if (defaultName.isEmpty())
        defaultName = tr("preset");
    defaultName += QStringLiteral(".json");

    const QString path = ruwa::shared::filedialog::getSaveFileName(this,
        ruwa::shared::filedialog::category::kShortcuts, tr("Export shortcut preset"), defaultName,
        tr("Ruwa shortcut preset (*.json)"));
    if (path.isEmpty())
        return;

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ruwa-shortcut-preset");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("preset")] = preset->toJson();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export Preset"), tr("Could not write file."));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ShortcutManagerTab::importPresetFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Import Preset"), tr("Could not read file."));
        return;
    }

    QJsonParseError parseErr {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        QMessageBox::warning(this, tr("Import Preset"), parseErr.errorString());
        return;
    }
    if (!doc.isObject()) {
        QMessageBox::warning(this, tr("Import Preset"), tr("Invalid file format."));
        return;
    }

    const QJsonObject root = doc.object();
    const QString fmt = root.value(QStringLiteral("format")).toString();
    if (fmt != QStringLiteral("ruwa-shortcut-preset")) {
        QMessageBox::warning(
            this, tr("Import Preset"), tr("This file is not a Ruwa shortcut preset."));
        return;
    }
    const QJsonObject obj = root.value(QStringLiteral("preset")).toObject();
    if (obj.isEmpty()) {
        QMessageBox::warning(this, tr("Import Preset"), tr("Preset payload is missing."));
        return;
    }

    auto imported = ruwa::features::settings::shortcuts::ShortcutPreset::fromJson(obj);

    auto& store = ruwa::features::settings::shortcuts::ShortcutPresetStore::instance();
    // Always assign a fresh UUID to avoid colliding with any existing preset
    // (and to avoid silently overwriting one with the same id).
    imported.id = QUuid::createUuid();
    imported.isBuiltIn = false;

    QString baseName = imported.name.trimmed();
    if (baseName.isEmpty()) {
        baseName = QFileInfo(path).completeBaseName();
    }
    if (baseName.isEmpty()) {
        baseName = tr("Imported");
    }
    imported.name = store.suggestUniqueName(baseName);

    m_selectedPresetId = imported.id;
    store.addCustomPreset(imported);
    applyPreset(imported.id);
}

void ShortcutManagerTab::onResetSectionClicked()
{
    if (m_selectedItemIndex < 0) {
        return;
    }
    auto& sm = ruwa::core::ShortcutManager::instance();
    const QStringList ids = commandIdsForItem(m_selectedItemIndex);
    for (const QString& id : ids) {
        sm.resetShortcut(id);
    }
    sm.saveToSettings();
    updateShortcutsHeaderMeta();
}

void ShortcutManagerTab::onResetRequested(const QString& commandId)
{
    ruwa::core::ShortcutManager::instance().resetShortcut(commandId);
    ruwa::core::ShortcutManager::instance().saveToSettings();
}

void ShortcutManagerTab::onShortcutManagerChanged(
    const QString& commandId, const QKeySequence& newShortcut)
{
    for (auto* row : m_rowWidgets.values(commandId)) {
        row->setShortcut(newShortcut);
    }
    updateShortcutsHeaderMeta();
    // Bound counts on preset cards reflect the active shortcuts.
    for (int i = 0; i < m_presetItems.size() && i < m_presetIds.size(); ++i) {
        m_presetItems[i]->setShortcutCount(countBoundShortcutsInPreset(m_presetIds[i]));
    }
}

// ============================================================================
// Theme
// ============================================================================

void ShortcutManagerTab::updateScaledSizes()
{
    const auto& theme = ThemeManager::instance();

    if (m_mainLayout) {
        m_mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));
        applyAspectRatioMargins();
    }

    if (m_headerLayout) {
        m_headerLayout->setSpacing(theme.scaled(BASE_HEADER_SPACING));
    }

    const auto& colors = theme.colors();

    if (m_titleLabel) {
        m_titleLabel->setFont(
            colors.fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    }

    QFont sectionFont = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SECTION_HEADER_FONT_SIZE));
    sectionFont.setWeight(QFont::DemiBold);
    for (QLabel* l : { m_presetsHeaderLabel, m_categoriesHeaderLabel }) {
        if (l)
            l->setFont(sectionFont);
    }

    if (m_shortcutsHeaderLabel) {
        QFont f = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SHORTCUTS_TITLE_FONT_SIZE));
        f.setWeight(QFont::DemiBold);
        m_shortcutsHeaderLabel->setFont(f);
    }
    if (m_shortcutsMetaLabel) {
        m_shortcutsMetaLabel->setFont(
            colors.fonts.getUIFont(theme.scaledFontSize(BASE_SHORTCUTS_META_FONT_SIZE)));
    }

    if (m_searchBar) {
        m_searchBar->setFixedWidth(theme.scaled(BASE_SEARCH_BAR_WIDTH));
    }

    for (auto* page : m_categoryPages) {
        if (auto* content = page->widget()) {
            if (auto* l = qobject_cast<QVBoxLayout*>(content->layout())) {
                l->setSpacing(theme.scaled(BASE_SCROLL_SPACING));
            }
        }
    }
}

void ShortcutManagerTab::updateThemeColors()
{
    const auto& colors = ThemeManager::instance().colors();

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    }

    const QString sectionHeaderStyle = QString("QLabel { color: %1; }").arg(colors.text.name());
    for (QLabel* l : { m_presetsHeaderLabel, m_categoriesHeaderLabel, m_shortcutsHeaderLabel }) {
        if (l)
            l->setStyleSheet(sectionHeaderStyle);
    }
    if (m_shortcutsMetaLabel) {
        m_shortcutsMetaLabel->setStyleSheet(
            QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
    }
    if (m_shortcutsDivider) {
        QPalette pal = m_shortcutsDivider->palette();
        pal.setColor(QPalette::Window, colors.border);
        m_shortcutsDivider->setPalette(pal);
    }

    for (auto* page : m_categoryPages) {
        if (auto* content = page->widget()) {
            QPalette pal = content->palette();
            pal.setColor(QPalette::Window, colors.background);
            content->setPalette(pal);
        }
    }
}

void ShortcutManagerTab::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::tabs
