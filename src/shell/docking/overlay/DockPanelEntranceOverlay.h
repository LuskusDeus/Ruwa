// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_DOCKING_OVERLAY_DOCKPANELENTRANCEOVERLAY_H
#define RUWA_UI_DOCKING_OVERLAY_DOCKPANELENTRANCEOVERLAY_H

#include <QColor>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QWidget>

class QVariantAnimation;

namespace ruwa::ui::docking {

class DockContainerWidget;
class DockPanel;

/**
 * Paint-only presentation layer for the initial workspace appearance.
 *
 * The real dock panels remain visible, fully laid out, and at their final
 * geometry for the overlay's entire lifetime. Up to four composite snapshots
 * represent everything docked above, below, left, and right of the stationary
 * panel. At progress 0 their destination corridors are covered with the dock
 * background. At progress 1 the snapshots and real panels coincide exactly,
 * so removing the overlay is seamless.
 */
class DockPanelEntranceOverlay final : public QWidget {
    Q_OBJECT

public:
    DockPanelEntranceOverlay(DockContainerWidget* container, DockPanel* stationaryPanel,
        const QColor& backgroundColor);
    ~DockPanelEntranceOverlay() override;

    bool isReady() const { return !m_items.isEmpty(); }
    void start(int durationMs);
    void cancel();

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    enum class EntranceEdge { Left, Right, Top, Bottom };

    struct Item {
        QRect targetRect;
        QPoint startOffset;
        QPixmap snapshot;
    };

    QPixmap captureSideSnapshot(
        const QRect& targetRect, const QList<QPointer<DockPanel>>& panels) const;
    EntranceEdge edgeFor(const QRect& panelRect, const QRect& stationaryRect) const;
    QPoint startOffsetFor(EntranceEdge edge, const QRect& targetRect) const;

    QPointer<DockContainerWidget> m_container;
    QColor m_backgroundColor;
    QList<Item> m_items;
    QVariantAnimation* m_animation = nullptr;
    qreal m_progress = 0.0;
    bool m_started = false;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_OVERLAY_DOCKPANELENTRANCEOVERLAY_H
