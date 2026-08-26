// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H E S   P A N E L   C O N T E N T
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_BRUSHESPANELCONTENT_H
#define RUWA_UI_WORKSPACE_BRUSHESPANELCONTENT_H

#include "features/brush/ui/BrushPackListSection.h"

#include <QJsonObject>
#include <QHash>
#include <QPixmap>
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
class DragGhostWidget;
class SmoothScrollArea;
} // namespace ruwa::ui::widgets

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
    void openBrushEditor(const QString& packId, const QString& brushId);
    void prepareVisiblePreviews();
    bool visiblePreviewsReady() const;

signals:
    void brushSelected(const QString& brushId);
    void stateChanged();
    void packFiltersChanged(const QStringList& packIds, const QStringList& packNames);
    void visiblePreviewStateChanged();

private slots:
    void queueReload();
    void onManagerBrushRenamed(const QString& brushId, const QString& newName);
    void onManagerPresetRenamed(const QString& presetId, const QString& newName);
    void onManagerBrushMoved(const QString& sourcePackId, const QString& targetPackId,
        const QString& brushId, int targetIndex);
    void onFavoriteBrushOrderChanged(const QStringList& brushIds);
    void onSectionToggled(const QString& packId, bool expanded);
    void onBrushActivated(const QString& packId, const QString& brushId);
    void onBrushEditorRequested(const QString& packId, const QString& brushId);
    void onBrushDeleteRequested(const QString& packId, const QString& brushId);
    void startBrushDrag(
        const QString& packId, const QString& brushId, QWidget* row, const QPoint& globalPos);
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
    void addPackSection(const QString& pageKey, FilterPage& page, const BrushListPackData& pack,
        bool forceExpanded = false);
    void clearPage(FilterPage& page);
    void ensureSelection();
    void syncSelectionToSections();
    void refreshScrollGeometry(const QString& pageKey);
    void refreshAllScrollGeometry();
    void scheduleScrollRestore(const QString& pageKey);
    void applyPendingScrollRestore(const QString& pageKey);
    void notifyStateChanged();
    QString brushNameForSelection(const QString& packId, const QString& brushId) const;
    /// Push the shared collapsed set onto this instance's live sections.
    void applySharedCollapsedState();
    /// Record a user toggle and mirror it onto every other panel instance.
    void setPackCollapsed(const QString& packId, bool collapsed);
    void updateBrushDrag(const QPoint& globalPos);
    void finishBrushDrag(bool accepted, const QPoint& globalPos);
    void clearBrushDropTarget(bool restoreExpansion);
    void cleanupBrushDrag();
    QPoint brushGhostTargetPosition(const QPoint& globalPos) const;
    BrushPackListSection* brushDropSectionAt(const QPoint& globalPos) const;
    bool updateCachedBrushMove(const QString& sourcePackId, const QString& targetPackId,
        const QString& brushId, int targetIndex, BrushListBrushData* movedBrush);
    void applyBrushMoveToBuiltSections(const QString& sourcePackId, const QString& targetPackId,
        const QString& brushId, int targetIndex, const BrushListBrushData& movedBrush);

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
    QString m_selectedBrushId;
    QString m_draggedBrushId;
    QString m_dragSourcePackId;
    QString m_dragTargetPackId;
    QPixmap m_dragSnapshot;
    QPointer<QWidget> m_draggedBrushRow;
    QPointer<BrushPackListSection> m_dragTargetSection;
    QPointer<ruwa::ui::widgets::DragGhostWidget> m_dragGhost;
    QPoint m_dragOffset;
    QPoint m_dragSourceGhostPosition;
    int m_dragTargetIndex = -1;
    bool m_dragTargetWasExpanded = true;
    bool m_brushDragActive = false;
    bool m_brushDragSettling = false;
    bool m_dragCursorOverride = false;
    bool m_reloadQueued = false;
    bool m_restoringState = false;
    ViewMode m_viewMode = ViewMode::All;
    QString m_viewPackId;

    /// Packs the user explicitly collapsed. Absence means expanded, so a pack
    /// the panel has never seen (first run, a freshly created or imported pack)
    /// opens by default and no reload can silently forget an expanded pack.
    ///
    /// Shared by every instance: each workspace tab owns its own panel, yet all
    /// of them persist into one settings key. Per-instance sets would make the
    /// last tab to save overwrite the choice made in another one.
    static QSet<QString> s_collapsedPackIds;
    /// Live instances, so a toggle in one tab reaches the sections of the rest.
    static QVector<BrushesPanelContent*> s_instances;
    /// The first restore of a session seeds the shared set from settings; later
    /// ones (a tab opened afterwards, a dock layout preset) must not push a
    /// stale on-disk value over the live state.
    static bool s_collapsedStateLoaded;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_BRUSHESPANELCONTENT_H
