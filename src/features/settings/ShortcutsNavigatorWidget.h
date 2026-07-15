// SPDX-License-Identifier: MPL-2.0

// ShortcutsNavigatorWidget.h
#ifndef RUWA_UI_WIDGETS_SETTINGS_SHORTCUTSNAVIGATORWIDGET_H
#define RUWA_UI_WIDGETS_SETTINGS_SHORTCUTSNAVIGATORWIDGET_H

#include "features/settings/BaseSettingsWidget.h"

#include <QStringList>
#include <QVector>

class QEvent;
class QHBoxLayout;
class QLabel;
class QShowEvent;
class QVBoxLayout;
class QWidget;

namespace ruwa::ui::widgets {

class CommandInputWidget;
class WelcomeBannerButton;

/**
 * @brief Keyboard shortcuts navigator widget
 *
 * A settings row that opens the keyboard shortcuts editor tab.
 * Matches the layout of other settings (label, description, control).
 */
class ShortcutsNavigatorWidget : public BaseSettingsWidget {
    Q_OBJECT

public:
    explicit ShortcutsNavigatorWidget(QWidget* parent = nullptr);
    ~ShortcutsNavigatorWidget() override = default;

signals:
    void clicked();

protected:
    void setupContent() override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

public:
    void retranslateUi();

private slots:
    void updateButtonIcon();

private:
    void updateMinimumHeight();
    void updateShortcutRows();
    void clearShortcutRows();
    void rebuildShortcutRows(const QStringList& shortcutIds);
    void applyScaledLayoutMargins();

    void updateThemeColors() override;
    WelcomeBannerButton* m_openButton = nullptr;
    QWidget* m_shortcutListContainer = nullptr;
    QWidget* m_bottomRow = nullptr;
    QVBoxLayout* m_shortcutListLayout = nullptr;
    QHBoxLayout* m_bottomLayout = nullptr;
    QVector<QWidget*> m_shortcutRows;
    QVector<QWidget*> m_shortcutSeparators;
    QVector<QLabel*> m_shortcutLabels;
    QVector<CommandInputWidget*> m_commandInputs;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SETTINGS_SHORTCUTSNAVIGATORWIDGET_H
