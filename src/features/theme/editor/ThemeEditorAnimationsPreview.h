// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_THEMEEDITORANIMATIONSPREVIEW_H
#define RUWA_UI_WIDGETS_THEMEEDITORANIMATIONSPREVIEW_H

#include "features/theme/manager/ThemeColors.h"

#include <QElapsedTimer>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QWidget>

#include <array>
#include <cstddef>

class QEvent;
class QHideEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QTimer;

namespace ruwa::ui::core {
struct ThemePreset;
}

namespace ruwa::ui::docking {
class DockGroupHeader;
}

namespace ruwa::ui::workspace {
class BrushSettingsPanel;
class BrushesPanel;
} // namespace ruwa::ui::workspace

namespace ruwa::ui::widgets {

class AnimatedComboBox;
class SegmentedOptionSelector;
class SettingsChoice;
class SettingsComboBox;
class SettingsToggle;
class SidebarButton;
class ToggleSwitch;

/**
 * @brief Looping Animations-page banner driven by the edited preset.
 *
 * The scene is a home-page style navigation list with a synthetic cursor that
 * walks it, hovers its entries and selects one. Every transition is timed from
 * the preset being edited rather than from the applied theme, so the loop shows
 * the pending speed multiplier and master switch as they are changed.
 *
 * The right half runs a second, independent 12 s loop over a settings list:
 * its dropdown, toggle and switcher operate themselves in turn. In the middle
 * a grouped Brushes / Brush settings dock hangs off the bottom edge, showing
 * its top third, and swaps members every three seconds.
 *
 * Every widget in the scene is a real one, parked outside the banner and
 * rendered per frame. Nothing here reacts to the user's pointer: the widgets
 * never see an event, and their hover/selection/thumb progress is written
 * directly from the loop.
 */
class ThemeEditorAnimationsPreview final : public QWidget {
    Q_OBJECT

public:
    explicit ThemeEditorAnimationsPreview(QWidget* parent = nullptr);
    ~ThemeEditorAnimationsPreview() override;

    /// Re-render immediately from the editor's working copy, without applying it globally.
    void setPreset(const ruwa::ui::core::ThemePreset& preset);

protected:
    void changeEvent(QEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    /// One list entry: the real button plus the progress the loop writes into it.
    struct ListItem {
        SidebarButton* button { nullptr };
        QRect target;
        qreal hover { 0.0 };
        qreal active { 0.0 };
    };

    /// One settings row: the real widget and where it is drawn in the banner.
    struct SettingRow {
        QWidget* widget { nullptr };
        QRect target;
    };

    static constexpr std::size_t ItemCount = 6;
    /// Only the first entry is clipped, in half, by the top edge of the banner.
    static constexpr std::size_t FirstVisibleItem = 1;
    static constexpr std::size_t LastVisibleItem = ItemCount - 1;
    /// The list divider sits above this entry, as it does in the home sidebar.
    static constexpr std::size_t DividedItem = ItemCount - 1;

    static constexpr std::size_t SettingCount = 7;
    /// The three rows the second loop operates; the rest are scenery and the
    /// outermost of them are clipped by the top and bottom banner edges.
    static constexpr std::size_t DropdownRow = 2;
    static constexpr std::size_t ToggleRow = 3;
    static constexpr std::size_t SwitcherRow = 4;

    void setupList();
    void setupSettings();
    void setupGroup();
    void retranslatePreview();
    void updateLayout();
    void updateListLayout();
    void updateSettingsLayout();
    void updateGroupLayout();
    void rebuildPanelSnapshots();
    void updateTheme();
    void rebuildPaths();
    void renderSceneLayer();
    void renderSettingsPopup(QPainter& painter);
    void renderGroup(QPainter& painter);
    void resetLoopState();
    void onTick();
    void advanceProgress(qreal deltaMs);
    void advanceSettings();
    void advanceGroup();
    void paintCursor(QPainter& painter);

    /// Progress of a setting that switches on at @p onSeconds and back off at
    /// @p offSeconds, eased over its authored duration. This is where the edited
    /// preset lands: the durations are divided by its speed multiplier, and the
    /// transition is instant while its master switch is off.
    qreal settingProgress(
        qreal onSeconds, qreal offSeconds, int authoredOnMs, int authoredOffMs) const;

    /// Position of the cursor for @p cycleSeconds within the 10 s loop.
    QPointF cursorPointAt(qreal cycleSeconds) const;
    /// Entry selected at @p cycleSeconds; the loop starts and ends on the same one.
    std::size_t selectedIndexAt(qreal cycleSeconds) const;
    /// 0 -> no click, 1 -> the instant of the press, decaying back to 0.
    qreal clickPulseAt(qreal cycleSeconds) const;
    QPointF itemCursorPoint(std::size_t index) const;
    qreal segmentProgress(qreal cycleSeconds, qreal startSeconds, qreal endSeconds) const;

    std::array<ListItem, ItemCount> m_items {};
    std::array<SettingRow, SettingCount> m_settings {};

    // Real controls of the animated rows, reached once and driven directly.
    SettingsComboBox* m_dropdownRow { nullptr };
    SettingsToggle* m_toggleRow { nullptr };
    SettingsChoice* m_switcherRow { nullptr };
    AnimatedComboBox* m_dropdownCombo { nullptr };
    ToggleSwitch* m_toggleSwitch { nullptr };
    SegmentedOptionSelector* m_switcher { nullptr };
    QSize m_popupSize;
    qreal m_dropdownProgress { 0.0 };

    // Centre group. The panel bodies are expensive to render, so each is kept as
    // a snapshot and only the cross-fade between them runs per frame.
    ruwa::ui::docking::DockGroupHeader* m_groupHeader { nullptr };
    ruwa::ui::workspace::BrushesPanel* m_brushesPanel { nullptr };
    ruwa::ui::workspace::BrushSettingsPanel* m_brushSettingsPanel { nullptr };
    QRect m_groupHeaderTarget;
    QRect m_groupBodyTarget;
    /// Logical size the member area is laid out at before the group's own zoom.
    QSize m_groupBodySource;
    std::array<QPixmap, 2> m_panelSnapshots {};
    bool m_panelSnapshotsDirty { true };
    qreal m_groupSlide { 1.0 };
    int m_currentGroupTab { 0 };

    ruwa::ui::core::ThemeColors m_previewColors;
    QPixmap m_cursorPixmap;
    /// The list is rendered off-screen first: QWidget::render() draws nothing
    /// while the parent's own QPainter is active.
    QPixmap m_listLayer;

    // Motion path, one sub-path per travelled segment of the loop.
    std::array<QPainterPath, 5> m_segments {};
    QPointF m_restPoint;
    QPointF m_bottomPoint;

    QTimer* m_frameTimer { nullptr };
    QElapsedTimer m_clock;
    qreal m_phase { 0.0 };
    qreal m_cycleSeconds { 0.0 };
    qreal m_settingsPhase { 0.0 };
    qreal m_settingsSeconds { 0.0 };
    qreal m_groupPhase { 0.0 };
    qreal m_groupSeconds { 0.0 };
    int m_dividerY { 0 };
    QPointF m_cursorPos;
    qreal m_clickPulse { 0.0 };
    qreal m_animationSpeed { 1.0 };
    bool m_animationsEnabled { true };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_THEMEEDITORANIMATIONSPREVIEW_H
