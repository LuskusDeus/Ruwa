// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   S M A R T   O B J E C T   T A B   C O N T E X T   M E N U
// ======================================================================================

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_SMARTOBJECTTABCONTEXTMENU_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_SMARTOBJECTTABCONTEXTMENU_H

#include "shell/context-menu/BaseContextMenu.h"

#include <QList>
#include <QString>
#include <QUuid>

class QLabel;
class QVBoxLayout;
class QWidget;

namespace ruwa::ui::widgets {

class BaseStyledWidget;
class HorizontalSeparator;
class SmoothScrollArea;

/**
 * @brief Right-click menu of a smart object's contents tab.
 *
 * A contents tab has no name and no icon of its own — it is a view of one smart
 * object — so instead of the rename/icon grid of a project tab this menu lists
 * every contents tab open for the SAME document and switches between them. All
 * of them share one slot in the tab strip, so this list is the only way to reach
 * the ones that are not currently shown.
 */
class SmartObjectTabContextMenu : public StandardContextMenu {
    Q_OBJECT

public:
    explicit SmartObjectTabContextMenu(QWidget* parent = nullptr);

    ContextMenuType menuType() const override { return ContextMenuType::SmartObjectTab; }

signals:
    void smartObjectActivated(const QUuid& tabId);
    void closeSmartObjectRequested(const QUuid& tabId);
    void closeAllSmartObjectsRequested(const QUuid& parentTabId);

protected:
    void rebuildStandardMenu() override;
    QPoint calculateMenuPosition(
        const QPoint& globalPos, const QSize& menuSize, QWidget* sourceWidget) const override;
    QSize expandMenuContentHint(const QSize& hint) const override;
    bool usesAttachedTopBarSurface() const override { return true; }
    qreal presentationSlideDistancePx() const override { return 0.0; }
    qreal presentationOpacity(qreal progress) const override
    {
        Q_UNUSED(progress);
        return 1.0;
    }

private:
    void buildUi();
    void applyStyle();
    void rebuildSmartObjectList();

private:
    QLabel* m_sectionLabel = nullptr;
    SmoothScrollArea* m_listArea = nullptr;
    QWidget* m_listContent = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    HorizontalSeparator* m_separator = nullptr;
    QList<BaseStyledWidget*> m_listRows;

    QUuid m_tabId; ///< The contents tab the menu was opened on.
    QUuid m_parentTabId;

    /// Rows visible at once; the section keeps this height whatever the count is.
    static constexpr int ListVisibleRows = 4;
    static constexpr int RowHeight = 30;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_SMARTOBJECTTABCONTEXTMENU_H
