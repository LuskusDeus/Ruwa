// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   NO-ACTIONS CONTEXT MENU (placeholder when a zone has no commands yet)
// ======================================================================================

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_NOACTIONSCONTEXTMENU_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_NOACTIONSCONTEXTMENU_H

#include "shell/context-menu/BaseContextMenu.h"

namespace ruwa::ui::widgets {

class NoActionsContextMenu : public BaseContextMenu {
    Q_OBJECT

public:
    explicit NoActionsContextMenu(QWidget* parent = nullptr);

    ContextMenuType menuType() const override { return ContextMenuType::Generic; }

protected:
    QRect contentRect() const override;
    void drawContent(QPainter& painter) override;
    void onContextChanged() override;
    void applyPresentationLayout(qreal progress) override;

private:
    void updateLayoutMetrics();

    QRect m_contentRect;
    QSize m_sizeFull;
    QRect m_contentRectFull;

    static constexpr int CornerRadius = 8;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_NOACTIONSCONTEXTMENU_H
