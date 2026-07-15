// SPDX-License-Identifier: MPL-2.0

// PresetMenuListWidget.h
#ifndef RUWA_UI_WIDGETS_PRESETMENULISTWIDGET_H
#define RUWA_UI_WIDGETS_PRESETMENULISTWIDGET_H

#include "PresetMenuTypes.h"
#include "BaseAnimatedButton.h"
#include "shell/context-menu/IContextMenuProvider.h"

#include <QLabel>
#include <QImage>
#include <QPixmap>
#include <QVector>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

class QEvent;
class QHBoxLayout;
class QPaintEvent;

namespace ruwa::ui::widgets {

class PresetListRowWidget;
class SmoothScrollArea;
class ListCollapseAnimator;
class SearchBar;

/**
 * @brief Reusable preset list: import/export actions + smooth-scrolling rows
 * with rename and delete controls. Use setPopupStyle(true) for floating menu /
 * picker chrome; false for embedding in a panel (e.g. theme editor).
 */
class PresetMenuListWidget : public QWidget, public ruwa::ui::widgets::IContextMenuProvider {
    Q_OBJECT

public:
    explicit PresetMenuListWidget(QWidget* parent = nullptr);
    ~PresetMenuListWidget() override;

    void setPopupStyle(bool popup);
    bool isPopupStyle() const { return m_popupStyle; }
    /// When false, the popup-style panel/glass backdrop is NOT painted (and no blur
    /// snapshot is taken) — the list stays fully transparent so an outer container can
    /// supply its own background (e.g. an attached-to-TopBar popup). Default true.
    void setPopupPanelPainted(bool painted);
    bool isPopupPanelPainted() const { return m_popupPanelPainted; }
    void setEmbeddedChromeTransparent(bool transparent);
    bool embeddedChromeTransparent() const { return m_embeddedChromeTransparent; }
    void refreshGlassBackdropFrom(QWidget* source);

    /// Optional heading above the toolbar (hidden when empty).
    void setTitleText(const QString& text);
    QString titleText() const;

    void setHeaderActions(const QVector<PresetMenuHeaderAction>& actions);
    QVector<PresetMenuHeaderAction> headerActions() const { return m_headerActions; }
    void setFooterAction(const PresetMenuHeaderAction& action);
    void setContextMenuEnabled(bool enabled);
    bool isContextMenuEnabled() const { return m_contextMenuEnabled; }

    void setItems(const QVector<PresetMenuItem>& items);
    QVector<PresetMenuItem> items() const { return m_items; }
    QVector<QVariant> visibleItemUserData(int preloadMargin = 0) const;

    /// Update extra actions for one row without recreating widgets (keeps scroll and selection
    /// animation state).
    bool setExtraActionsForItem(
        const QVariant& userData, const QVector<PresetMenuExtraAction>& actions);
    bool setPreviewImageForItem(const QVariant& userData, const QImage& image);
    /// Update the title of one row without recreating widgets (keeps scroll,
    /// selection state and previews). Returns false if no row matches.
    bool setTitleForItem(const QVariant& userData, const QString& title);
    /// Update the subtitle of one row in place (keeps scroll, selection and
    /// previews). Returns false if no row matches.
    bool setSubtitleForItem(const QVariant& userData, const QString& subtitle);

    void setSelectedUserData(const QVariant& data);
    QVariant selectedUserData() const { return m_selectedData; }

    void setActiveUserData(const QVariant& data);
    QVariant activeUserData() const { return m_activeData; }

    void setImportExportVisible(bool visible);
    bool isImportExportVisible() const { return m_importExportVisible; }

    void setSearchEnabled(bool enabled);
    bool isSearchEnabled() const { return m_searchEnabled; }

    void setSearchPlaceholderText(const QString& text);
    QString searchPlaceholderText() const;

    void setEmptyStateTexts(const QString& title, const QString& description);
    QString searchText() const;

    /// Maximum height of the scroll area (0 = grow with content up to parent).
    void setScrollMaximumHeight(int height);
    int scrollMaximumHeight() const { return m_scrollMaxHeight; }

    SmoothScrollArea* scrollArea() const { return m_scrollArea; }

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;
    void handleContextMenuAction(int actionId) override;

signals:
    void itemClicked(const QVariant& userData);
    void itemRenamed(const QVariant& userData, const QString& newTitle);
    void deleteRequested(const QVariant& userData);
    /// Emitted for PresetMenuItem::extraActions (see PresetMenuExtraAction::id).
    void extraActionTriggered(const QVariant& userData, int actionId);
    void headerActionTriggered(int actionId);
    void importClicked();
    void exportClicked();
    void searchTextChanged(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void rebuildRows();
    void rebuildHeaderActions();
    void rebuildFooterAction();
    void applyContentMargins();
    void applyLayerChromeTransparency(bool transparent);
    void updateSectionVisibility();
    void updateSelectionVisuals();
    bool itemMatchesFilter(const PresetMenuItem& item) const;
    /// Try to apply \p newItems as a pure removal (a subsequence of the current
    /// items) by smoothly collapsing the removed rows instead of rebuilding the
    /// whole list. Returns false when the change isn't a clean removal (additions,
    /// reorder, active search, …), leaving the caller to do a full rebuild.
    bool tryAnimatedRemoval(const QVector<PresetMenuItem>& newItems);
    QWidget* widgetForItem(const PresetMenuItem& item) const;

private slots:
    void onThemeChanged();

private:
    bool m_popupStyle = false;
    bool m_popupPanelPainted = true;
    bool m_embeddedChromeTransparent = false;
    bool m_importExportVisible = true;
    bool m_searchEnabled = true;
    bool m_contextMenuEnabled = false;
    int m_scrollMaxHeight = 0;

    QVector<PresetMenuItem> m_items;
    QVector<PresetMenuHeaderAction> m_headerActions;
    PresetMenuHeaderAction m_footerAction;
    QVariant m_selectedData;
    QVariant m_activeData;

    QWidget* m_headerWidget = nullptr;
    QWidget* m_headerTopRow = nullptr;
    QLabel* m_titleLabel = nullptr;
    QWidget* m_headerActionsWidget = nullptr;
    QHBoxLayout* m_headerActionsLayout = nullptr;
    BaseAnimatedButton* m_importBtn = nullptr;
    BaseAnimatedButton* m_exportBtn = nullptr;
    QVector<BaseAnimatedButton*> m_headerToolButtons;
    QWidget* m_footerHost = nullptr;
    BaseAnimatedButton* m_footerButton = nullptr;
    SearchBar* m_searchBar = nullptr;
    QString m_searchPlaceholderText;
    QLabel* m_emptyIconLabel = nullptr;
    QLabel* m_emptyTitleLabel = nullptr;
    QLabel* m_emptyDescriptionLabel = nullptr;
    QWidget* m_emptyState = nullptr;
    SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_listContent = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QPixmap m_glassBackdrop;

    ListCollapseAnimator* m_collapseAnimator = nullptr;

    QVector<PresetListRowWidget*> m_rows;
};

} // namespace ruwa::ui::widgets

#endif
