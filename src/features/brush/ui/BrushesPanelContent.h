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
#include <QVector>
#include <QWidget>

class QVBoxLayout;
class QEvent;

namespace ruwa::ui::widgets {
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

signals:
    void brushSelected(const QString& brushId);
    void stateChanged();

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
    QVector<BrushListPackData> collectPacks() const;
    void rebuildSections();
    void clearSections();
    void ensureSelection();
    void syncSelectionToSections();
    void refreshScrollGeometry();
    void scheduleScrollRestore();
    void applyPendingScrollRestore();
    void notifyStateChanged();
    void openBrushEditor(const QString& packId, const QString& brushId);
    QString brushNameForSelection(const QString& packId, const QString& brushId) const;

private:
    widgets::SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;
    QVBoxLayout* m_scrollLayout = nullptr;
    CanvasPanel* m_canvasPanel = nullptr;

    QVector<BrushListPackData> m_packs;
    QHash<QString, BrushPackListSection*> m_sections;
    QPointer<ruwa::ui::windows::BrushEditorWindow> m_brushEditorWindow;
    QSet<QString> m_expandedPackIds;
    QString m_selectedBrushId;
    bool m_reloadQueued = false;
    bool m_hasExplicitExpandedState = false;
    int m_pendingScrollValue = -1;
    bool m_scrollRestoreQueued = false;
    bool m_restoringState = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_BRUSHESPANELCONTENT_H
