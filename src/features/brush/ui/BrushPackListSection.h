// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H   P A C K   L I S T   S E C T I O N
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_BRUSHPACKLISTSECTION_H
#define RUWA_UI_WORKSPACE_BRUSHPACKLISTSECTION_H

#include "features/brush/manager/BrushManager.h"

#include <QHash>
#include <QPoint>
#include <QPropertyAnimation>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QLabel;
class QEvent;
class QPixmap;
class QResizeEvent;

namespace ruwa::ui::widgets {
class AnimatedFlowWidget;
class SectionHeaderButton;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

inline constexpr int kBrushListButtonBaseSize = 80;

struct BrushListBrushData {
    QString id;
    QString packId;
    QString name;
    ruwa::core::brushes::BrushSettingsData settings;
    int displayColorIndex = 0;
};

struct BrushListPackData {
    QString id;
    QString name;
    QVector<BrushListBrushData> brushes;
};

class BrushPackListSection : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int contentHeight READ contentHeight WRITE setContentHeight)

public:
    explicit BrushPackListSection(QWidget* parent = nullptr);
    ~BrushPackListSection() override;

    void setPackData(const BrushListPackData& pack);
    const BrushListPackData& packData() const { return m_pack; }
    void setBrushButtonBaseSize(int size);

    /// Update the pack's displayed name in place (header repaint only; brush
    /// rows and their previews are untouched).
    void updatePackName(const QString& newName);

    /// Programmatic expand/collapse. Does NOT emit toggled() — that signal
    /// reports a user click only, so restoring or rebuilding a section never
    /// rewrites the panel's persisted expansion state.
    void setExpanded(bool expanded, bool animated = true);
    bool isExpanded() const { return m_expanded; }

    void setSelectedBrushId(const QString& brushId);
    QString selectedBrushId() const { return m_selectedBrushId; }

    void setBrushDragEnabled(bool enabled) { m_brushDragEnabled = enabled; }
    int brushInsertIndexAtGlobal(const QPoint& globalPos, const QString& draggedBrushId = {}) const;
    void showBrushDropPlaceholder(
        const QPixmap& snapshot, int insertIndex, const QString& draggedBrushId = {});
    void commitBrushDropPreview();
    void clearBrushDropPlaceholder();
    QRect brushDropTargetGlobalRect() const;

    /// Incremental row operations used by BrushesPanelContent after the
    /// manager has committed a move. They preserve the existing row widgets
    /// (including preview sessions and animation state).
    bool reorderBrush(const QString& brushId, int targetIndex, bool animate = true);
    void reorderBrushes(const QStringList& brushIds, bool animate = true);
    QWidget* takeBrushRow(
        const QString& brushId, BrushListBrushData* brushData = nullptr, bool animate = true);
    bool insertBrushRow(const BrushListBrushData& brushData, int targetIndex,
        QWidget* existingRow = nullptr, bool animate = true);
    bool updateBrushPackId(const QString& brushId, const QString& packId);

    /// Update cached settings for a single brush row (invalidates its preview).
    /// Returns true if the brush exists in this section.
    bool updateBrushSettings(
        const QString& brushId, const ruwa::core::brushes::BrushSettingsData& settings);
    bool updateBrushDisplayColorIndex(const QString& brushId, int colorIndex);
    bool updateBrushFavorite(const QString& brushId);
    /// Update the displayed name of a single brush row in place (repaint only,
    /// preview untouched). Returns true if the brush exists in this section.
    bool updateBrushName(const QString& brushId, const QString& newName);

    int contentHeight() const { return m_contentHeight; }
    void setContentHeight(int height);
    void prepareVisiblePreviews(QWidget* viewport);
    bool visiblePreviewsReady(QWidget* viewport) const;

signals:
    /// Emitted only when the user clicks the section header.
    void toggled(const QString& packId, bool expanded);
    void brushActivated(const QString& packId, const QString& brushId);
    void brushEditorRequested(const QString& packId, const QString& brushId);
    void brushDeleteRequested(const QString& packId, const QString& brushId);
    void brushDragRequested(
        const QString& packId, const QString& brushId, QWidget* row, const QPoint& globalPos);
    void contentGeometryChanged();
    void visiblePreviewStateChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildBrushRows();
    QWidget* createBrushRow(const BrushListBrushData& brush);
    void configureBrushRow(QWidget* row, const BrushListBrushData& brush);
    void ensureEmptyLabel();
    void removeEmptyLabel();
    void updateExpandedVisualState(bool animated);
    void updateSelectionState();
    void scheduleExpandedHeightRefresh();
    void animateContentHeightTo(int targetHeight);
    int contentAnimationDurationForDelta(int delta) const;
    int expandedContentHeight() const;
    void applyBrushFlowItems(bool animate);

private:
    BrushListPackData m_pack;
    QString m_selectedBrushId;
    bool m_expanded = false;
    int m_contentHeight = 0;
    int m_brushButtonBaseSize = kBrushListButtonBaseSize;

    ruwa::ui::widgets::SectionHeaderButton* m_headerButton = nullptr;
    ruwa::ui::widgets::AnimatedFlowWidget* m_contentContainer = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QHash<QString, QWidget*> m_brushRows;
    QWidget* m_dropPlaceholder = nullptr;
    QString m_dropDraggedBrushId;
    int m_dropInsertIndex = -1;
    QWidget* m_dragCandidateRow = nullptr;
    QPoint m_dragPressPosition;
    bool m_brushDragEnabled = false;
    QPropertyAnimation* m_expandAnimation = nullptr;
    bool m_heightRefreshQueued = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_BRUSHPACKLISTSECTION_H
