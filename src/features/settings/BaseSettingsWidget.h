// SPDX-License-Identifier: MPL-2.0

// BaseSettingsWidget.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_BASESETTINGSWIDGET_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_BASESETTINGSWIDGET_H

#include "shared/widgets/BaseStyledPanel.h"
#include <QString>

class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class QLayout;
class QWidget;

namespace ruwa::ui::widgets {

/**
 * @brief Abstract base class for settings widgets
 *
 * Uses BaseStyledPanel with "SettingsPanel" style.
 * Provides common structure:
 * - Label and optional description
 * - Content area (implemented by derived classes)
 */
class BaseSettingsWidget : public BaseStyledPanel {
    Q_OBJECT

public:
    explicit BaseSettingsWidget(
        const QString& label, const QString& description = QString(), QWidget* parent = nullptr);
    ~BaseSettingsWidget() override = default;

    QString label() const { return m_label; }
    QString description() const { return m_description; }

    void setLabel(const QString& label);
    void setDescription(const QString& description);

protected:
    /// Override this to add custom content to the layout
    virtual void setupContent() = 0;

    /// Access to control layout for adding widgets (right side of the row)
    QVBoxLayout* mainLayout() { return m_controlLayout; }

    /// Update colors on theme change (can be overridden)
    virtual void updateThemeColors();

    /// Recalculate geometry after internal layout/content changes.
    void refreshLayoutGeometry();

    // We don't override drawContentLayer - use QWidget children instead

private slots:
    void onThemeChanged();

private:
    void setupUI();
    void updateScaledSizes();

protected:
    QString m_label;
    QString m_description;

    QLabel* m_labelWidget = nullptr;
    QLabel* m_descriptionWidget = nullptr;
    QWidget* m_textContainer = nullptr;
    QWidget* m_controlContainer = nullptr;
    QLayout* m_mainLayout = nullptr;
    QVBoxLayout* m_textLayout = nullptr;
    QVBoxLayout* m_controlLayout = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_BASESETTINGSWIDGET_H
