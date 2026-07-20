// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H E S   P A N E L   C O N T E N T
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_BRUSHESPANELCONTENT_H
#define RUWA_UI_WORKSPACE_BRUSHESPANELCONTENT_H

#include "features/brush/ui/BrushPackListSection.h"

#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QVBoxLayout;
class QEvent;

namespace ruwa::ui::widgets {
class AnimatedStackedWidget;
class SmoothScrollArea;
}

namespace ruwa::ui::windows {
class BrushEditorWindow;
}

namespace ruwa::ui::workspace {

class CanvasPanel;

class BrushesPanelContent : public QWidget {
    Q_OBJECT

public:
    explicit BrushesPanelContent(QWidget* parent = nullptr);
    ~BrushesPanelContent() override;

    void setCanvasPanel(CanvasPanel* canvasPanel);
    void reloadFromManager();
    QJsonObject saveState() const;
    void restoreState(const QJsonObject& state);
    QString selectedBrushId() const { return m_selectedBrushId; }
    QStringList packFilterIds() const;
    QStringList packFilterNames() const;
    void showAllPacks();
    void showFavoriteBrushes();
    void showPack(const QString& packId);

signals:
    void brushSelected(const QString& brushId);
    void stateChanged();
    void packFiltersChanged(const QStringList& packIds, const QStringList& packNames);

private slots:
    void queueReload();
    void onManagerBrushRenamed(const QString& brushId, const QString& newName);
    void onManagerPresetRenamed(const QString& presetId, const QString& newName);
    void onSectionToggled(const QString& packId, bool expanded);
    void onBrushActivated(const QString& packId, const QString& brushId);
    void onBrushEditorRequested(const QString& packId, const QString& brushId);
    void onThemeChanged();
    void syncSelectionFromCanvas();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class ViewMode {
        All,
        Favorites,
        Pack,
    };

    struct FilterPage {
        QWidget* container = nullptr;
        widgets::SmoothScrollArea* scrollArea = nullptr;
        QWidget* scrollContent = nullptr;
        QVBoxLayout* scrollLayout = nullptr;
        QHash<QString, BrushPackListSection*> sections;
        bool built = false;
    };

    QVector<BrushListPackData> collectPacks() const;
    void syncFilterPages();
    void createFilterPage(const QString& pageKey, int stackIndex);
    void switchToView(ViewMode viewMode, const QString& packId = {});
    QString currentPageKey() const;
    void ensurePageBuilt(const QString& pageKey);
    void rebuildBuiltPages();
    void rebuildPage(const QString& pageKey);
    void addPackSection(
        const QString& pageKey, FilterPage& page, const BrushListPackData& pack,
        bool forceExpanded = false);
    void clearPage(FilterPage& page);
    void ensureSelection();
    void syncSelectionToSections();
    void refreshScrollGeometry(const QString& pageKey);
    void refreshAllScrollGeometry();
    void scheduleScrollRestore(const QString& pageKey);
    void applyPendingScrollRestore(const QString& pageKey);
    void notifyStateChanged();
    void openBrushEditor(const QString& packId, const QString& brushId);
    QString brushNameForSelection(const QString& packId, const QString& brushId) const;

private:
    widgets::AnimatedStackedWidget* m_pageStack = nullptr;
    CanvasPanel* m_canvasPanel = nullptr;

    QVector<BrushListPackData> m_packs;
    QHash<QString, FilterPage> m_filterPages;
    QHash<QObject*, QString> m_scrollViewportPageKeys;
    QHash<QString, int> m_pageScrollValues;
    QSet<QString> m_pendingScrollRestoreKeys;
    QSet<QString> m_queuedScrollRestoreKeys;
    QPointer<ruwa::ui::windows::BrushEditorWindow> m_brushEditorWindow;
    QSet<QString> m_expandedPackIds;
    QString m_selectedBrushId;
    bool m_reloadQueued = false;
    bool m_hasExplicitExpandedState = false;
    bool m_restoringState = false;
    ViewMode m_viewMode = ViewMode::All;
    QString m_viewPackId;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_BRUSHESPANELCONTENT_H
