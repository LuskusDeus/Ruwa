// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCATEGORY_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCATEGORY_H

#include <QIcon>
#include <QString>
#include <QVector>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QVBoxLayout;

namespace ruwa::ui::widgets {

/**
 * @brief Category container for settings-like widgets.
 *
 * Features:
 * - Optional icon and title header
 * - Always-expanded content area
 * - Lightweight container for grouping rows/panels
 */
class SettingsCategory : public QWidget {
    Q_OBJECT

public:
    explicit SettingsCategory(const QString& title, QWidget* parent = nullptr);
    ~SettingsCategory() override = default;

    void setIcon(const QIcon& icon);
    void addSettingsWidget(QWidget* widget);

    QString title() const { return m_title; }
    void setTitle(const QString& title);

    void setHeaderVisible(bool visible);
    bool isHeaderVisible() const { return m_headerVisible; }

    QVector<QWidget*> settingsWidgets() const { return m_settingsWidgets; }

private:
    void setupUI(const QString& title);
    void updateScaledSizes();
    void updateThemeColors();
    void updateIconDisplay();

private slots:
    void onThemeChanged();

private:
    QString m_title;
    QIcon m_icon;
    bool m_headerVisible { true };

    QWidget* m_headerWidget { nullptr };
    QLabel* m_iconLabel { nullptr };
    QLabel* m_titleLabel { nullptr };
    QWidget* m_contentWidget { nullptr };
    QVBoxLayout* m_mainLayout { nullptr };
    QHBoxLayout* m_headerLayout { nullptr };
    QVBoxLayout* m_contentLayout { nullptr };

    QVector<QWidget*> m_settingsWidgets;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_SETTINGSCATEGORY_H
