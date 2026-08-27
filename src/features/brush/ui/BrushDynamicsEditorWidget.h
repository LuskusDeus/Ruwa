// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_BRUSH_BRUSHDYNAMICSEDITORWIDGET_H
#define RUWA_UI_WIDGETS_BRUSH_BRUSHDYNAMICSEDITORWIDGET_H

#include "features/brush/manager/BrushSettingDefs.h"
#include "shared/widgets/inputs/CurveEditorWidget.h"

#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QFrame;

namespace ruwa::ui::widgets {

class ToggleSwitch;
class AnimatedStackedWidget;
class SegmentedOptionSelector;
class AnimatedComboBox;
class ProgressHandleSlider;

/**
 * Editor for the parameter dynamics of a single brush setting: input source
 * column plus the per-source page (enable, blend mode, curve / random range).
 *
 * The widget owns no chrome, so it can be embedded in the brush editor's modal
 * overlay as well as in the brush settings panel popup.
 */
class BrushDynamicsEditorWidget : public QWidget {
    Q_OBJECT

public:
    using BrushDynamicsBinding = ruwa::core::brushes::BrushDynamicsBinding;
    using BrushDynamicsSlot = ruwa::core::brushes::BrushDynamicsSlot;
    using BrushDynamicTargetDef = ruwa::core::brushes::BrushDynamicTargetDef;
    using BrushInputSourceKey = ruwa::core::brushes::BrushInputSourceKey;
    using BrushDynamicsSettingKey = ruwa::core::brushes::BrushDynamicsSettingKey;
    using BrushDynamicsBlendMode = ruwa::core::brushes::BrushDynamicsBlendMode;
    using BrushTimeEndAction = ruwa::core::brushes::BrushTimeEndAction;
    using BrushSettingsData = ruwa::core::brushes::BrushSettingsData;

    struct CurveAxesConfig {
        CurveEditorWidget::AxisDisplaySpec horizontalAxis { 0.0, 1.0, 100.0, 0, QStringLiteral("%"),
            {}, true };
        CurveEditorWidget::AxisDisplaySpec verticalAxis { 0.0, 1.0, 100.0, 0, QString(), {}, true };
    };

    explicit BrushDynamicsEditorWidget(QWidget* parent = nullptr);

    /// Axes for the setting as currently configured on the brush: the value axis
    /// tops out at the setting's own value.
    static CurveAxesConfig curveAxesConfigForSetting(
        const QString& engineId, const BrushSettingsData& settings, const QString& settingKey);

    void setTarget(const QString& settingKey, const BrushDynamicsSlot& slot,
        const BrushDynamicTargetDef& targetDef, CurveAxesConfig curveAxesConfig);
    void setCurveAxesConfig(CurveAxesConfig curveAxesConfig);

    QString settingKey() const;
    BrushDynamicsSlot slot() const;
    BrushInputSourceKey activeSource() const;
    void setActiveSource(BrushInputSourceKey source);
    void resetActiveSourceBinding();

    /// Compact metrics for the panel popup; the modal overlay uses the roomy
    /// default.
    void setCompact(bool compact);
    bool isCompact() const;

signals:
    void slotChanged(const QString& settingKey, const BrushDynamicsSlot& slot);
    void activeSourceChanged(BrushInputSourceKey source);
    void editingFinished();

private:
    static BrushDynamicsBinding defaultTimeBinding(BrushDynamicsSettingKey setting);
    static BrushDynamicsBinding defaultRandomBinding(BrushDynamicsSettingKey setting);
    static BrushDynamicsBinding defaultStrokeDirectionBinding(BrushDynamicsSettingKey setting);
    static BrushDynamicsBinding defaultPenTiltBinding(BrushDynamicsSettingKey setting);
    static BrushDynamicsBinding defaultStrokeSpeedBinding(BrushDynamicsSettingKey setting);
    static ruwa::core::brushes::BrushDynamicsInputFilter defaultInputFilter();
    BrushDynamicsBinding displayBinding(BrushDynamicsBinding binding) const;
    CurveAxesConfig curveAxesConfigForBinding(const BrushDynamicsBinding& binding) const;
    int randomRangeSliderFactor() const;
    int sliderValueFromRandomRangeValue(float value) const;
    float randomRangeValueFromSliderValue(int sliderValue) const;
    QString formatRandomRange(const ruwa::core::brushes::BrushDynamicsRandomRange& range) const;
    ToggleSwitch* activeToggle() const;
    SegmentedOptionSelector* activeModeSelector() const;
    CurveEditorWidget* activeCurveEditor() const;
    bool isSourceAvailable(BrushInputSourceKey source) const;
    BrushInputSourceKey fallbackSource() const;
    int sourcePageIndex(BrushInputSourceKey source) const;
    BrushDynamicsBinding currentBinding() const;
    BrushDynamicsBinding defaultBindingForSource(BrushInputSourceKey source) const;
    void storeCurrentBinding(const BrushDynamicsBinding& binding, bool emitSlotChanged = true);
    void storeInputFilter(
        const ruwa::core::brushes::BrushDynamicsInputFilter& filter, bool emitSlotChanged = true);
    void syncEditorFromCurrentBinding();
    void updateModeSelector();
    void updateTexts();
    void updateSourceButtons();
    void updateStyles();

    QWidget* m_sourcesColumn = nullptr;
    AnimatedStackedWidget* m_editorStack = nullptr;
    QWidget* m_filterPage = nullptr;
    QWidget* m_pressurePage = nullptr;
    QWidget* m_timePage = nullptr;
    QWidget* m_randomPage = nullptr;
    QWidget* m_directionPage = nullptr;
    QWidget* m_tiltPage = nullptr;
    QWidget* m_speedPage = nullptr;
    QLabel* m_filterSectionLabel = nullptr;
    QLabel* m_sourcesLabel = nullptr;
    QLabel* m_filterStrengthLabel = nullptr;
    QLabel* m_filterCurveLabel = nullptr;
    QLabel* m_pressureLabel = nullptr;
    QLabel* m_modeLabel = nullptr;
    QLabel* m_timeEnabledLabel = nullptr;
    QLabel* m_timeModeLabel = nullptr;
    QLabel* m_timeDurationLabel = nullptr;
    QLabel* m_timeEndActionLabel = nullptr;
    QLabel* m_randomEnabledLabel = nullptr;
    QLabel* m_randomModeLabel = nullptr;
    QLabel* m_randomRangeLabel = nullptr;
    QLabel* m_directionEnabledLabel = nullptr;
    QLabel* m_directionModeLabel = nullptr;
    QLabel* m_tiltEnabledLabel = nullptr;
    QLabel* m_tiltModeLabel = nullptr;
    QLabel* m_speedEnabledLabel = nullptr;
    QLabel* m_speedModeLabel = nullptr;
    QPushButton* m_filterButton = nullptr;
    QPushButton* m_tabletPressureButton = nullptr;
    QPushButton* m_timeButton = nullptr;
    QPushButton* m_randomButton = nullptr;
    QPushButton* m_directionButton = nullptr;
    QPushButton* m_tiltButton = nullptr;
    QPushButton* m_speedButton = nullptr;
    ToggleSwitch* m_pressureToggle = nullptr;
    SegmentedOptionSelector* m_modeSelector = nullptr;
    CurveEditorWidget* m_curveEditor = nullptr;
    ToggleSwitch* m_timeToggle = nullptr;
    SegmentedOptionSelector* m_timeModeSelector = nullptr;
    CurveEditorWidget* m_timeCurveEditor = nullptr;
    ProgressHandleSlider* m_timeDurationSlider = nullptr;
    AnimatedComboBox* m_timeEndActionCombo = nullptr;
    ToggleSwitch* m_randomToggle = nullptr;
    SegmentedOptionSelector* m_randomModeSelector = nullptr;
    ProgressHandleSlider* m_randomRangeSlider = nullptr;
    ToggleSwitch* m_directionToggle = nullptr;
    SegmentedOptionSelector* m_directionModeSelector = nullptr;
    CurveEditorWidget* m_directionCurveEditor = nullptr;
    ToggleSwitch* m_tiltToggle = nullptr;
    SegmentedOptionSelector* m_tiltModeSelector = nullptr;
    CurveEditorWidget* m_tiltCurveEditor = nullptr;
    ToggleSwitch* m_speedToggle = nullptr;
    SegmentedOptionSelector* m_speedModeSelector = nullptr;
    CurveEditorWidget* m_speedCurveEditor = nullptr;
    CurveEditorWidget* m_filterCurveEditor = nullptr;
    ProgressHandleSlider* m_filterStrengthSlider = nullptr;
    QString m_settingKey;
    BrushDynamicsSlot m_slot;
    BrushDynamicTargetDef m_targetDef;
    CurveAxesConfig m_curveAxesConfig;
    BrushInputSourceKey m_activeSource = BrushInputSourceKey::TabletPressure;
    QVector<BrushDynamicsBlendMode> m_modeOptions;
    bool m_compact = false;
    bool m_syncingModeSelector = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_BRUSH_BRUSHDYNAMICSEDITORWIDGET_H
