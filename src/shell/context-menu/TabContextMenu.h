// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_CONTEXTMENU_TABCONTEXTMENU_H
#define RUWA_UI_WIDGETS_CONTEXTMENU_TABCONTEXTMENU_H

#include "shell/context-menu/BaseContextMenu.h"
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QUuid>

class QWidget;

namespace ruwa::ui::widgets {

class HorizontalSeparator;
class BaseStyledWidget;

class TabContextMenu : public StandardContextMenu {
    Q_OBJECT

public:
    explicit TabContextMenu(QWidget* parent = nullptr);

    ContextMenuType menuType() const override { return ContextMenuType::TabBar; }

signals:
    void renameRequested(const QUuid& tabId);
    void changeIconRequested(const QUuid& tabId);
    void closeTabRequested(const QUuid& tabId);
    void closeOtherTabsRequested(const QUuid& tabId);
    void closeAllTabsRequested();

    void tabRenamed(const QUuid& tabId, const QString& newName);
    void tabIconChanged(const QUuid& tabId, const QString& iconAlias);

protected:
    void rebuildStandardMenu() override;
    void showEvent(QShowEvent* event) override;
    QPoint calculateMenuPosition(
        const QPoint& globalPos, const QSize& menuSize, QWidget* sourceWidget) const override;
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
    void selectIcon(const QString& iconAlias, bool emitChange, bool animateSelection = true);
    void handleActionTriggered(int actionId);

private:
    QLabel* m_tabTypeSectionLabel = nullptr;
    QLineEdit* m_nameInput = nullptr;
    QWidget* m_iconWidget = nullptr;
    HorizontalSeparator* m_separator = nullptr;
    QHash<QString, BaseStyledWidget*> m_iconButtons;
    QHash<int, BaseStyledWidget*> m_actionButtons;

    QUuid m_tabId;
    QString m_tabTitle;
    QString m_currentIconAlias;

    static constexpr int IconSize = 16;
    static constexpr int IconsPerRow = 7;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CONTEXTMENU_TABCONTEXTMENU_H
