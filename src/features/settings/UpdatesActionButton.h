// SPDX-License-Identifier: MPL-2.0

// UpdatesActionButton.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_UPDATESACTIONBUTTON_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_UPDATESACTIONBUTTON_H

#include "shared/widgets/BaseAnimatedButton.h"
#include "features/settings/UpdatesSettingsWidget.h"

class QLabel;
class QHBoxLayout;
class QStackedWidget;

namespace ruwa::ui::widgets {

class DotGridLoadingIndicator;

/**
 * @brief Updates action button with 4 states
 *
 * - UpToDate: gray, inactive, check icon
 * - Downloading: gray, "Downloading" + loading indicator
 * - UpdateAvailable: accent, click to download
 * - ReadyToRestart: accent, click to restart
 */
class UpdatesActionButton : public BaseAnimatedButton {
    Q_OBJECT

public:
    explicit UpdatesActionButton(QWidget* parent = nullptr);
    ~UpdatesActionButton() override = default;

    void setState(UpdateState state);
    UpdateState state() const { return m_state; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateContent();
    void updateScaledSizes();
    void updateLoadingIndicator();

private slots:
    void onThemeChanged();

private:
    UpdateState m_state { UpdateState::UpToDate };
    QHBoxLayout* m_contentLayout { nullptr };
    QStackedWidget* m_iconStack { nullptr };
    QLabel* m_iconLabel { nullptr };
    QLabel* m_textLabel { nullptr };
    DotGridLoadingIndicator* m_loadingIndicator { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_SETTINGS_UPDATESACTIONBUTTON_H
