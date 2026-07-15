// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_TABS_EMPTYSTATETAB_H
#define RUWA_UI_TABS_EMPTYSTATETAB_H

#include "shell/tab-system/BaseTab.h"

class QLabel;
class QWidget;

namespace ruwa::ui::tabs {

/**
 * @brief Special "zero" tab shown when there are no active tabs
 *
 * This tab:
 * - Does not appear in the tab bar
 * - Cannot be closed
 * - Cannot be navigated to (only shown when all tabs are closed)
 * - Displays placeholder content
 */
class EmptyStateTab : public ruwa::core::BaseTab {
    Q_OBJECT

public:
    explicit EmptyStateTab(QWidget* parent = nullptr);

    ruwa::core::BaseTab::TabType type() const override { return TabType::EmptyState; }
    QString title() const override { return QString(); }
    QIcon icon() const override { return QIcon(); }

    /// Zero tab can never be closed
    bool canClose() override { return false; }

protected:
    void onInitialize() override;
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* m_asciiWidget = nullptr;
    QLabel* m_hintLabel = nullptr;
};

} // namespace ruwa::ui::tabs

#endif // RUWA_UI_TABS_EMPTYSTATETAB_H
