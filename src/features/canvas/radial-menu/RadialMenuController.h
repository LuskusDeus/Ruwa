// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   R A D I A L   M E N U
// ======================================================================================
//   File        : RadialMenuController.h
//   Description : Binds the radial menu widget to the canvas: resolves the
//                 configured layout into drawable slots, runs commands and
//                 walks nested pages.
// ======================================================================================

#ifndef RUWA_UI_CANVAS_RADIALMENUCONTROLLER_H
#define RUWA_UI_CANVAS_RADIALMENUCONTROLLER_H

#include "RadialMenuModel.h"
#include "RadialMenuWidget.h"

#include <QObject>
#include <QPoint>
#include <QStringList>

namespace ruwa::ui::workspace {

class CanvasPanel;

class RadialMenuController : public QObject {
    Q_OBJECT

public:
    explicit RadialMenuController(CanvasPanel* panel);

    /// Open the root page centred on @p globalPos. @p armReleaseSelect turns
    /// the opening press into a hold gesture (see RadialMenuWidget::showAt).
    void showRadialMenu(const QPoint& globalPos, bool armReleaseSelect = false);
    void hideRadialMenu();
    bool isRadialMenuVisible() const;

private:
    void ensureWidget();

    /// Navigate to @p pageId. @p pushHistory is false when going back.
    /// @p transition and @p pivotIndex are handed straight to the widget, which
    /// animates the swap around that seat (see RadialMenuWidget::setPage).
    void openPage(const QString& pageId, bool pushHistory,
        ruwa::ui::widgets::RadialMenuWidget::PageTransition transition
        = ruwa::ui::widgets::RadialMenuWidget::PageTransition::None,
        int pivotIndex = -1);
    void goBack();
    void handleSlotTriggered(int index);

    /// Resolve stored items into what the widget draws (titles, shortcuts,
    /// icons, enabled state).
    ruwa::ui::widgets::RadialMenuWidget::Page buildPage(
        const ruwa::ui::widgets::RadialMenuPage& page) const;

    /// Page currently on screen, or nullptr when the menu is closed.
    const ruwa::ui::widgets::RadialMenuPage* currentPage() const;

    CanvasPanel* m_panel = nullptr;
    /// Navigation history; the last entry is the page on screen.
    QStringList m_pageStack;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_CANVAS_RADIALMENUCONTROLLER_H
