// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_SIMPLEACTIONSCONTEXTMENU_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_SIMPLEACTIONSCONTEXTMENU_H

#include "shell/context-menu/BaseContextMenu.h"

#include <QSize>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

/**
 * @brief Compact context menu built from context key \c simpleActions.
 *
 * Row entry: \c QVariantMap with \c id (int), \c text (QString), optional \c danger (bool),
 * optional \c standardIcon (int, \c IconProvider::StandardIcon), optional \c checked (bool),
 * optional \c enabled (bool, default true).
 *
 * Separator: \c QVariantMap with \c separator = true (no \c id; row is non-clickable).
 */
class SimpleActionsContextMenu : public StandardContextMenu {
    Q_OBJECT

public:
    explicit SimpleActionsContextMenu(QWidget* parent = nullptr);

    ContextMenuType menuType() const override { return ContextMenuType::SimpleActions; }

signals:
    void actionTriggered(int actionId);

protected:
    void rebuildStandardMenu() override;
    QSize expandMenuContentHint(const QSize& hint) const override;

private:
    QVBoxLayout* m_actionLayout = nullptr;
};

} // namespace ruwa::ui::widgets

#endif
