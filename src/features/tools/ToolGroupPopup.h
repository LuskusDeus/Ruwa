// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_TOOLGROUPPOPUP_H
#define RUWA_UI_WORKSPACE_TOOLGROUPPOPUP_H

#include "shared/resources/IconProvider.h"

#include <QList>
#include <QPointer>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QBoxLayout;

namespace ruwa::ui::workspace {

class ToolButton;

class ToolGroupPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)

public:
    enum class Side { Right, Left, Bottom, Top };

    enum class LayoutMode { Vertical, Horizontal };

    struct Item {
        int toolId = -1;
        ruwa::ui::core::IconProvider::StandardIcon iconType
            = ruwa::ui::core::IconProvider::StandardIcon::Hand;
        QString tooltip;
    };

    explicit ToolGroupPopup(QWidget* parent = nullptr);
    ~ToolGroupPopup() override;

    void setItems(const QList<Item>& items);
    void setCurrentToolId(int toolId);
    void setSide(Side side);
    void setLayoutMode(LayoutMode mode);
    void showFor(QWidget* anchor, bool animate = true);
    void hideAnimated();
    void hideImmediate();
    bool isPopupVisible() const { return m_isVisible; }

    qreal popupOpacity() const { return m_opacity; }
    void setPopupOpacity(qreal opacity);

signals:
    void toolSelected(int toolId);
    void hidden();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    ToolButton* buttonAtGlobalPos(const QPoint& globalPos) const;
    void updateHoveredButton(const QPoint& globalPos);
    void clearHoveredButton();
    bool triggerHoveredButtonSelection();
    void rebuildButtons();
    QPoint calculateTargetPosition(QWidget* anchor) const;
    void updateIcons();
    void startShowAnimation(bool animateSlide);
    void startHideAnimation();
    void detachGlobalFilters();

private:
    QList<Item> m_items;
    QList<ToolButton*> m_buttons;
    QBoxLayout* m_layout = nullptr;
    QPointer<QWidget> m_anchor;
    QPointer<ToolButton> m_hoveredButton;
    Side m_side = Side::Right;
    LayoutMode m_layoutMode = LayoutMode::Vertical;

    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    qreal m_opacity = 0.0;
    bool m_isVisible = false;
    bool m_isHiding = false;
    bool m_releaseSelectArmed = false;

    static constexpr int kShowDuration = 120;
    static constexpr int kHideDuration = 150;
    static constexpr int kSlideDuration = 180;
    static constexpr int kSlideOffset = 14;
    static constexpr int kPopupGap = 8;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_TOOLGROUPPOPUP_H
