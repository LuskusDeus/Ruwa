// SPDX-License-Identifier: MPL-2.0

// LayoutPresetsPopup.h
#ifndef RUWA_UI_WIDGETS_TOPBAR_LAYOUTPRESETSPOPUP_H
#define RUWA_UI_WIDGETS_TOPBAR_LAYOUTPRESETSPOPUP_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QRect>

#include "shell/docking/state/DockLayoutPreset.h"

class QGraphicsOpacityEffect;
class QVBoxLayout;

namespace ruwa::ui::widgets {

class PresetMenuListWidget;

/**
 * @brief Dock layout presets: built-in + user-saved (rename/delete like theme editor list).
 */
class LayoutPresetsPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)
    Q_PROPERTY(int displayHeight READ displayHeight WRITE setDisplayHeight)

public:
    explicit LayoutPresetsPopup(QWidget* parent = nullptr);
    ~LayoutPresetsPopup() override;

    void showBelow(QWidget* anchor, bool slideFromTop = true);
    void hidePopup();
    void forceHide();

    void retranslateUi();

    bool isPopupVisible() const { return m_isVisible; }
    bool isHiding() const { return m_isHiding; }

    qreal popupOpacity() const { return m_opacity; }
    void setPopupOpacity(qreal opacity);

    int displayHeight() const { return m_displayHeight; }
    void setDisplayHeight(int h);

signals:
    void presetChosen(const ruwa::ui::docking::DockLayoutPreset& preset);
    /// User clicked "+" — MainWindow should capture layout from active @ref WorkspaceTab.
    void newPresetFromCurrentRequested();
    void exportCurrentLayoutRequested();
    void importLayoutRequested();
    void aboutToHide();
    void hidden();
    void shown();
    void contentChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void refreshPresets();
    void applyListChrome();
    QPoint calculatePosition(QWidget* anchor) const;
    void ensureHeightAnim();
    /// Visible painted body, excluding the soft-shadow padding (invalid while too short).
    QRectF attachedBodyRect() const;

private:
    PresetMenuListWidget* m_presetList = nullptr;
    QVBoxLayout* m_outerLayout = nullptr;

    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    QPropertyAnimation* m_heightAnim = nullptr;
    qreal m_opacity = 0.0;
    int m_displayHeight = 0;
    int m_targetHeight = 0;
    bool m_isVisible = false;
    bool m_isHiding = false;
    bool m_isRefreshing = false;
    bool m_isAnimatingHeight = false;

    static constexpr int SHOW_DURATION = 120;
    static constexpr int SLIDE_DURATION = 200;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_TOPBAR_LAYOUTPRESETSPOPUP_H
