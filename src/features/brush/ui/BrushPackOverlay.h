// SPDX-License-Identifier: MPL-2.0

// BrushPackOverlay.h
#ifndef RUWA_UI_WIDGETS_WORKSPACE_BRUSHPACKOVERLAY_H
#define RUWA_UI_WIDGETS_WORKSPACE_BRUSHPACKOVERLAY_H

#include <QObject>
#include <QPoint>
#include <QSize>
#include <QPropertyAnimation>
#include <QPointer>
#include <QRect>

class QWidget;

namespace ruwa::ui::widgets {

class BrushPackPanel;

/**
 * @brief Controller for BrushPackPanel with click-outside-to-close
 *
 * Opens with slide animation from the side of BrushControlOverlay.
 * Uses qApp event filter to detect clicks outside panel.
 */
class BrushPackOverlay : public QObject {
    Q_OBJECT
    Q_PROPERTY(QPoint panelPos READ panelPos WRITE setPanelPos)

public:
    explicit BrushPackOverlay(QWidget* container);
    ~BrushPackOverlay() override;

    void showPanel(QWidget* sourceWidget = nullptr, const QPoint* slideFromPos = nullptr);
    void hidePanel();
    void forceHide();
    bool isActive() const;
    bool canShowPanel() const;

    /// True if user has moved the panel; it should not close on canvas click.
    bool isUserMoved() const { return m_userMovedPanel; }

    BrushPackPanel* panel() const { return m_panel; }
    QWidget* sourceWidget() const { return m_sourceWidget; }

    QPoint panelPos() const;
    void setPanelPos(const QPoint& pos);

    /// Called when source widget (e.g. BrushControlOverlay) moves; updates panel position.
    void onSourceWidgetMoved(QWidget* sourceWidget);

signals:
    void shown();
    void hidden();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupPanel();
    void setupAnimations();
    QPoint calculatePanelPosition(
        QWidget* sourceWidget, const QSize& panelSizeOverride = QSize()) const;
    QPoint clampPanelToContainer(const QPoint& pos) const;
    void animatePanelTo(const QPoint& targetPos);
    bool isPanelOrChild(QWidget* widget) const;
    bool isPanelAuxiliaryWidget(QWidget* widget) const;
    bool isSourceOrChild(QWidget* widget) const;
    bool isComboPopupOfPanel(QWidget* widget) const;

private:
    QWidget* m_container = nullptr;
    BrushPackPanel* m_panel = nullptr;
    QPointer<QWidget> m_sourceWidget;

    bool m_isShowing = false;
    bool m_isHiding = false;

    /// When true, user has moved the panel; it won't close on canvas click.
    bool m_userMovedPanel = false;

    QPropertyAnimation* m_posAnimation = nullptr;

    QRect m_lastContainerGeometry; ///< For resize snap logic when m_userMovedPanel

    static constexpr int OffsetFromSource = 8;
    static constexpr int PanelEdgeMargin
        = 6; ///< Min distance from container edge (matches BrushPackPanel drag)
    static constexpr int EdgeSnapThreshold
        = 0; ///< Max gap to consider panel "at edge" for snap-to-boundary (0 = must be flush)
    static constexpr int SlideOffset = 20;
    static constexpr int PositionAnimationDuration = 280;
    static constexpr int MinContainerMargin = 20; ///< Min free space around panel
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_BRUSHPACKOVERLAY_H
