// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H   P A C K   L I S T   S E C T I O N
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_BRUSHPACKLISTSECTION_H
#define RUWA_UI_WORKSPACE_BRUSHPACKLISTSECTION_H

#include "features/brush/manager/BrushManager.h"

#include <QHash>
#include <QPropertyAnimation>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QResizeEvent;

namespace ruwa::ui::widgets {
class AnimatedFlowWidget;
}

namespace ruwa::ui::workspace {

struct BrushListBrushData {
    QString id;
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

    /// Update the pack's displayed name in place (header repaint only; brush
    /// rows and their previews are untouched).
    void updatePackName(const QString& newName);

    void setExpanded(bool expanded, bool animated = true);
    bool isExpanded() const { return m_expanded; }

    void setSelectedBrushId(const QString& brushId);
    QString selectedBrushId() const { return m_selectedBrushId; }

    /// Update cached settings for a single brush row (invalidates its preview).
    /// Returns true if the brush exists in this section.
    bool updateBrushSettings(
        const QString& brushId, const ruwa::core::brushes::BrushSettingsData& settings);
    bool updateBrushDisplayColorIndex(const QString& brushId, int colorIndex);
    /// Update the displayed name of a single brush row in place (repaint only,
    /// preview untouched). Returns true if the brush exists in this section.
    bool updateBrushName(const QString& brushId, const QString& newName);

    int contentHeight() const { return m_contentHeight; }
    void setContentHeight(int height);

signals:
    void toggled(const QString& packId, bool expanded);
    void brushActivated(const QString& packId, const QString& brushId);
    void brushEditorRequested(const QString& packId, const QString& brushId);
    void contentGeometryChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void rebuildBrushRows();
    void updateExpandedVisualState(bool animated);
    void updateSelectionState();
    void scheduleExpandedHeightRefresh();
    void animateContentHeightTo(int targetHeight);
    int contentAnimationDurationForDelta(int delta) const;
    int expandedContentHeight() const;

private:
    BrushListPackData m_pack;
    QString m_selectedBrushId;
    bool m_expanded = false;
    int m_contentHeight = 0;

    QWidget* m_headerButton = nullptr;
    ruwa::ui::widgets::AnimatedFlowWidget* m_contentContainer = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QHash<QString, QWidget*> m_brushRows;
    QPropertyAnimation* m_expandAnimation = nullptr;
    bool m_heightRefreshQueued = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_BRUSHPACKLISTSECTION_H
