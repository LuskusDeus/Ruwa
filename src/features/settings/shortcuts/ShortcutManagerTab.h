// SPDX-License-Identifier: MPL-2.0

// ShortcutManagerTab.h
#ifndef RUWA_UI_TABS_CONTENT_SHORTCUTMANAGERTAB_H
#define RUWA_UI_TABS_CONTENT_SHORTCUTMANAGERTAB_H

#include "shell/tab-system/BaseTab.h"

#include <QKeySequence>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QHash>
#include <QIcon>
#include <QMultiHash>
#include <QStringList>
#include <QUuid>
#include <QVector>

class QLabel;
class QVBoxLayout;
class QHBoxLayout;

namespace ruwa::ui::widgets {
class AnimatedStackedWidget;
class CapsuleButton;
class CategoryItemWidget;
class PresetImportDropZone;
class PresetItemWidget;
class SearchBar;
class SmoothScrollArea;
class SettingsCategory;
class ShortcutRowWidget;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::tabs {

/**
 * @brief Tab for editing keyboard shortcuts
 *
 * Displays all commands grouped by category using SettingsCategory widgets.
 * Each shortcut is shown as a styled ShortcutRowWidget with inline editing.
 * Matches the visual style of the Settings page.
 */
class ShortcutManagerTab : public ruwa::core::BaseTab {
    Q_OBJECT

public:
    explicit ShortcutManagerTab(QWidget* parent = nullptr);
    ~ShortcutManagerTab() override;

    ruwa::core::BaseTab::TabType type() const override { return TabType::Custom; }
    QString title() const override { return tr("Keyboard Shortcuts"); }
    QString tabKindLabel() const override { return tr("Keyboard Shortcuts"); }

    bool wantsThemeLoadingScreen() const override { return true; }

protected:
    void onInitialize() override;
    void onApplyThemeRefresh(std::function<void()> finished, bool showLoading) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// Internal ctor preserving the tab id across a theme-refresh rebuild.
    explicit ShortcutManagerTab(const QUuid& id, QWidget* parent);

private slots:
    void onSearchTextChanged(const QString& text);
    void onShortcutChanged(const QString& commandId, const QKeySequence& newShortcut);
    void onResetRequested(const QString& commandId);
    void onShortcutManagerChanged(const QString& commandId, const QKeySequence& newShortcut);
    void onCategorySelected(int index);
    void onResetSectionClicked();
    void onNewPresetClicked();
    void onPresetSelected(int index);
    void onPresetDeleteRequested(int index);
    void onPresetExportRequested(int index);
    void onPresetRenamed(int index, const QString& newName);
    void onPresetStoreChanged();
    void onPresetImportClicked();
    void onPresetFileDropped(const QString& path);
    void onThemeChanged();

private:
    void createLayout();
    void createCategories();
    void applySearchFilter();
    void updateScaledSizes();
    void updateThemeColors();
    void applyAspectRatioMargins();
    void updateShortcutsHeaderMeta();
    QStringList commandIdsForItem(int itemIndex) const;

private:
    // Header
    QLabel* m_titleLabel { nullptr };
    ruwa::ui::widgets::SearchBar* m_searchBar { nullptr };
    QVBoxLayout* m_mainLayout { nullptr };
    QHBoxLayout* m_headerLayout { nullptr };

    // Content split (presets | categories | shortcuts)
    QHBoxLayout* m_contentLayout { nullptr };
    QWidget* m_presetsPanel { nullptr };
    QWidget* m_categoriesPanel { nullptr };
    QWidget* m_shortcutsPanel { nullptr };
    QLabel* m_presetsHeaderLabel { nullptr };
    QLabel* m_categoriesHeaderLabel { nullptr };
    QLabel* m_shortcutsHeaderLabel { nullptr };
    QLabel* m_shortcutsMetaLabel { nullptr };
    ruwa::ui::widgets::CapsuleButton* m_resetSectionButton { nullptr };
    QWidget* m_shortcutsDivider { nullptr };

    // Presets panel
    ruwa::ui::widgets::CapsuleButton* m_newPresetButton { nullptr };
    QVBoxLayout* m_presetsListLayout { nullptr };
    ruwa::ui::widgets::PresetImportDropZone* m_importDropZone { nullptr };
    QVector<ruwa::ui::widgets::PresetItemWidget*> m_presetItems;
    QVector<QUuid> m_presetIds;
    QUuid m_selectedPresetId;

    void rebuildPresetsList();
    void applyPreset(const QUuid& presetId);
    QHash<QString, QKeySequence> captureCurrentCustomBindings() const;
    int countBoundShortcutsInPreset(const QUuid& presetId) const;
    void exportPresetToFile(const QUuid& presetId);
    void importPresetFromFile(const QString& path);

    // Shortcuts content (one scroll page per item, switched via stacked widget).
    // Index 0 is the synthetic "All shortcuts" item; indices 1..N are real categories.
    ruwa::ui::widgets::AnimatedStackedWidget* m_categoryStack { nullptr };
    QVector<ruwa::ui::widgets::SmoothScrollArea*> m_categoryPages;

    // Category selector items (in the Categories panel), aligned with stack pages.
    QVector<ruwa::ui::widgets::CategoryItemWidget*> m_categoryItems;
    QStringList m_itemTitles;
    QStringList m_itemSubtitles;
    QVector<QIcon> m_itemIcons;
    QVector<int> m_itemCounts;
    int m_selectedItemIndex { -1 };

    // Real categories (without the "All" entry)
    QVector<ruwa::ui::widgets::SettingsCategory*> m_categories;
    QVector<ruwa::ui::widgets::SettingsCategory*> m_allShortcutGroups;
    QStringList m_categoryDisplayNames;
    QStringList m_categorySourceNames;
    QMultiHash<QString, ruwa::ui::widgets::ShortcutRowWidget*> m_rowWidgets; // commandId -> rows

    QString m_searchText;
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_CONTENT_SHORTCUTMANAGERTAB_H
