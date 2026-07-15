// SPDX-License-Identifier: MPL-2.0

// ToolButton.h
#ifndef RUWA_UI_WORKSPACE_WIDGETS_TOOLBUTTON_H
#define RUWA_UI_WORKSPACE_WIDGETS_TOOLBUTTON_H

#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"

#include <QIcon>

namespace ruwa::ui::workspace {

/**
 * @brief Animated tool button with icon for ToolsPanel
 */
class ToolButton : public ruwa::ui::widgets::BaseAnimatedButton {
    Q_OBJECT
    Q_PROPERTY(qreal enabledProgress READ enabledProgress WRITE setEnabledProgress)

public:
    enum class Mode { Toggle, Action };
    enum class ChromeStyle { Toolbar, Overlay, Surface };

    explicit ToolButton(QWidget* parent = nullptr);
    explicit ToolButton(Mode mode, QWidget* parent = nullptr);
    ~ToolButton() override = default;

    void setIcon(const QIcon& icon);
    void setIconType(ruwa::ui::core::IconProvider::StandardIcon iconType);
    void setBaseSize(int width, int height, int iconSize);
    void setBaseSquareSize(int size, int iconSize);
    void setChromeStyle(ChromeStyle style);
    void setChromeOpacity(qreal opacity);
    void setChromeInsets(int left, int top, int right, int bottom);
    void setBorderVisible(bool visible);
    void setColorizeIcon(bool colorize);
    void setMutedNormalIcon(bool muted);
    /// When disabled, the button skips the transient pressed/darkening overlay
    /// and only shows hover + the active-state animation. Useful for toggles
    /// where the press flash would flicker against the smooth toggle animation.
    void setPressFeedbackEnabled(bool enabled);
    void setHasGroupIndicator(bool hasGroupIndicator);
    bool hasGroupIndicator() const { return m_hasGroupIndicator; }

    // Smoothly fades the icon between its enabled and disabled appearance when
    // the button's enabled state toggles (e.g. undo/redo arrows becoming
    // available), avoiding an abrupt color/opacity jump.
    qreal enabledProgress() const { return m_enabledProgress; }
    void setEnabledProgress(qreal progress);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void updateScaledSize();

private:
    QIcon m_sourceIcon;
    Mode m_mode = Mode::Toggle;
    ChromeStyle m_chromeStyle = ChromeStyle::Toolbar;
    int m_baseWidth = 36;
    int m_baseHeight = 36;
    int m_baseIconSize = 20;
    int m_baseRadius = 6;
    int m_baseInsetLeft = 0;
    int m_baseInsetTop = 0;
    int m_baseInsetRight = 0;
    int m_baseInsetBottom = 0;
    qreal m_chromeOpacity = 1.0;
    int m_iconSize = 20;
    bool m_borderVisible = false;
    bool m_colorizeIcon = true;
    bool m_mutedNormalIcon = false;
    bool m_pressFeedback = true;
    bool m_hasGroupIndicator = false;

    qreal m_enabledProgress = 1.0;
    QPropertyAnimation* m_enabledAnimation = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_WIDGETS_TOOLBUTTON_H
