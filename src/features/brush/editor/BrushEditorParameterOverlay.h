// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORPARAMETEROVERLAY_H
#define RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORPARAMETEROVERLAY_H

#include "features/brush/manager/BrushSettingDefs.h"
#include "shared/widgets/inputs/CurveEditorWidget.h"

#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QObject;
class QPaintEvent;
class QGraphicsOpacityEffect;
class QPushButton;
class QResizeEvent;
class QVariantAnimation;

namespace ruwa::ui::widgets {
class ToggleSwitch;
class AnimatedStackedWidget;
class SegmentedOptionSelector;
class AnimatedComboBox;
class CapsuleButton;
class CurveEditorWidget;
class ProgressHandleSlider;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::windows {

class BrushEditorParameterOverlay : public QWidget {
    Q_OBJECT

public:
    using BrushDynamicsBinding = ruwa::core::brushes::BrushDynamicsBinding;
    using BrushDynamicsSlot = ruwa::core::brushes::BrushDynamicsSlot;
    using BrushDynamicTargetDef = ruwa::core::brushes::BrushDynamicTargetDef;
    using BrushInputSourceKey = ruwa::core::brushes::BrushInputSourceKey;
    using BrushDynamicsSettingKey = ruwa::core::brushes::BrushDynamicsSettingKey;
    using BrushDynamicsBlendMode = ruwa::core::brushes::BrushDynamicsBlendMode;
    using BrushTimeEndAction = ruwa::core::brushes::BrushTimeEndAction;
    struct CurveAxesConfig {
        ruwa::ui::widgets::CurveEditorWidget::AxisDisplaySpec horizontalAxis { 0.0, 1.0, 100.0, 0,
            QStringLiteral("%"), {}, true };
        ruwa::ui::widgets::CurveEditorWidget::AxisDisplaySpec verticalAxis { 0.0, 1.0, 100.0, 0,
            QString(), {}, true };
    };

    explicit BrushEditorParameterOverlay(QWidget* parent = nullptr);
    ~BrushEditorParameterOverlay() override;

    void showOverlay(const QString& settingKey, const QString& settingLabel,
        const BrushDynamicsSlot& slot, const BrushDynamicTargetDef& targetDef);
    void showOverlay(const QString& settingKey, const QString& settingLabel,
        const BrushDynamicsSlot& slot, const BrushDynamicTargetDef& targetDef,
        CurveAxesConfig curveAxesConfig);
    void hideOverlay();
    bool isActive() const;
    QString settingKey() const;
    BrushInputSourceKey activeSource() const;
    void setActiveSource(BrushInputSourceKey source);
    void setCurveAxesConfig(CurveAxesConfig curveAxesConfig);

signals:
    void slotChanged(const QString& settingKey, const BrushDynamicsSlot& slot);
    void activeSourceChanged(BrushInputSourceKey source);
    void editingFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    static BrushDynamicsBinding defaultTimeBinding(BrushDynamicsSettingKey setting);
    static BrushDynamicsBinding defaultRandomBinding(BrushDynamicsSettingKey setting);
    static BrushDynamicsBinding defaultStrokeDirectionBinding(BrushDynamicsSettingKey setting);
    BrushDynamicsBinding displayBinding(BrushDynamicsBinding binding) const;
    CurveAxesConfig curveAxesConfigForBinding(const BrushDynamicsBinding& binding) const;
    int randomAmountSliderFactor() const;
    int sliderValueFromRandomAmount(float amount) const;
    float randomAmountFromSliderValue(int sliderValue) const;
    QString formatRandomAmount(float amount) const;
    widgets::ToggleSwitch* activeToggle() const;
    widgets::SegmentedOptionSelector* activeModeSelector() const;
    widgets::CurveEditorWidget* activeCurveEditor() const;
    bool isSourceAvailable(BrushInputSourceKey source) const;
    bool isShapeAngleStrokeDirection() const;
    BrushInputSourceKey fallbackSource() const;
    int sourcePageIndex(BrushInputSourceKey source) const;
    BrushDynamicsBinding currentBinding() const;
    BrushDynamicsBinding defaultBindingForSource(BrushInputSourceKey source) const;
    void resetActiveSourceBinding();
    void storeCurrentBinding(const BrushDynamicsBinding& binding, bool emitSlotChanged = true);
    void syncEditorFromCurrentBinding();
    void updateModeSelector();
    void updateTexts();
    void updatePanelGeometry();
    void updatePanelPresentation();
    void updateSourceButtons();
    void updateStyles();

    QWidget* m_panel = nullptr;
    QWidget* m_body = nullptr;
    QWidget* m_sourcesColumn = nullptr;
    ruwa::ui::widgets::AnimatedStackedWidget* m_editorStack = nullptr;
    QWidget* m_pressurePage = nullptr;
    QWidget* m_timePage = nullptr;
    QWidget* m_randomPage = nullptr;
    QWidget* m_directionPage = nullptr;
    QGraphicsOpacityEffect* m_panelOpacityEffect = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_sourcesLabel = nullptr;
    QLabel* m_pressureLabel = nullptr;
    QLabel* m_modeLabel = nullptr;
    QLabel* m_timeEnabledLabel = nullptr;
    QLabel* m_timeModeLabel = nullptr;
    QLabel* m_timeDurationLabel = nullptr;
    QLabel* m_timeEndActionLabel = nullptr;
    QLabel* m_randomEnabledLabel = nullptr;
    QLabel* m_randomAmountLabel = nullptr;
    QLabel* m_directionEnabledLabel = nullptr;
    QLabel* m_directionModeLabel = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_resetButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QPushButton* m_tabletPressureButton = nullptr;
    QPushButton* m_timeButton = nullptr;
    QPushButton* m_randomButton = nullptr;
    QPushButton* m_directionButton = nullptr;
    ruwa::ui::widgets::ToggleSwitch* m_pressureToggle = nullptr;
    ruwa::ui::widgets::SegmentedOptionSelector* m_modeSelector = nullptr;
    ruwa::ui::widgets::CurveEditorWidget* m_curveEditor = nullptr;
    ruwa::ui::widgets::ToggleSwitch* m_timeToggle = nullptr;
    ruwa::ui::widgets::SegmentedOptionSelector* m_timeModeSelector = nullptr;
    ruwa::ui::widgets::CurveEditorWidget* m_timeCurveEditor = nullptr;
    ruwa::ui::widgets::ProgressHandleSlider* m_timeDurationSlider = nullptr;
    ruwa::ui::widgets::AnimatedComboBox* m_timeEndActionCombo = nullptr;
    ruwa::ui::widgets::ToggleSwitch* m_randomToggle = nullptr;
    ruwa::ui::widgets::ProgressHandleSlider* m_randomAmountSlider = nullptr;
    ruwa::ui::widgets::ToggleSwitch* m_directionToggle = nullptr;
    ruwa::ui::widgets::SegmentedOptionSelector* m_directionModeSelector = nullptr;
    ruwa::ui::widgets::CurveEditorWidget* m_directionCurveEditor = nullptr;
    QVariantAnimation* m_dimAnimation = nullptr;
    QVariantAnimation* m_panelAnimation = nullptr;
    QString m_settingKey;
    QString m_settingLabel;
    BrushDynamicsSlot m_slot;
    BrushDynamicTargetDef m_targetDef;
    CurveAxesConfig m_curveAxesConfig;
    QRect m_targetPanelRect;
    qreal m_dimProgress = 0.0;
    qreal m_panelProgress = 0.0;
    BrushInputSourceKey m_activeSource = BrushInputSourceKey::TabletPressure;
    QVector<BrushDynamicsBlendMode> m_modeOptions;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_shortcutsBlocked = false;
    bool m_syncingModeSelector = false;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORPARAMETEROVERLAY_H
