// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O M P O S E R   W I D G E T
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_COMPOSERWIDGET_H
#define RUWA_UI_WORKSPACE_COMPOSERWIDGET_H

#include <QWidget>
#include <QList>
#include <QRect>

class QTimer;

namespace ruwa::ui::workspace {
class CanvasPanel;
class OverviewCache;
} // namespace ruwa::ui::workspace

namespace ruwa::ui::workspace {

/**
 * @brief Widget displaying full canvas overview with viewport rect and pan control
 */
class ComposerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ComposerWidget(QWidget* parent = nullptr);
    ~ComposerWidget() override;

    void setCanvasPanel(CanvasPanel* panel);
    CanvasPanel* canvasPanel() const { return m_canvasPanel; }

    void refreshOverview();
    void refreshThumbnail() { refreshOverview(); }
    void invalidateOverviewTiles(const QList<QPoint>& tilePositions);
    void invalidateAllOverview();

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void scheduleOverviewRefresh();
    void updateOverview();
    QSize targetOverviewSize() const;
    QRectF canvasDisplayRect() const;
    QPointF widgetToWorld(const QPointF& widgetPos) const;
    QPointF worldToWidget(const QPointF& worldPos) const;

    CanvasPanel* m_canvasPanel = nullptr;
    OverviewCache* m_overviewCache = nullptr;
    QRect m_worldFrame;
    QTimer* m_viewportSyncTimer = nullptr;
    QTimer* m_overviewRefreshTimer = nullptr;
    bool m_dragging = false;
    QPoint m_dragStartPos;
    QPointF m_dragStartCameraCenter;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_COMPOSERWIDGET_H
