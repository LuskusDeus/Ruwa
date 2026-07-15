// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_WORKSPACE_BRUSHSETTINGS_BRUSHSETTINGSWIDGET_H
#define RUWA_UI_WIDGETS_WORKSPACE_BRUSHSETTINGS_BRUSHSETTINGSWIDGET_H

#include "features/brush/manager/BrushSettings.h"
#include "features/brush/manager/BrushSettingDefs.h"

#include <QSet>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QWidget>
#include <QVector>

class QLabel;
class QPushButton;
class QVBoxLayout;

namespace ruwa::ui::widgets {

class ToggleSwitch;
class ProgressHandleSlider;
class AnimatedComboBox;
class ImageDropdownSelector;
class SegmentedOptionSelector;

class BrushSettingsWidget : public QWidget {
    Q_OBJECT

public:
    using BrushSettingsData = ruwa::core::brushes::BrushSettingsData;
    using BrushSettingDef = ruwa::core::brushes::BrushSettingDef;

    explicit BrushSettingsWidget(
        const QVector<BrushSettingDef>& defs, QWidget* parent = nullptr, bool starMode = false);
    ~BrushSettingsWidget() override = default;

    void setSettings(const QVariantMap& data);
    void setSettings(const BrushSettingsData& data);

    void applyTo(QVariantMap& data) const;
    void applyTo(BrushSettingsData& data) const;

    void setStarredKeys(const QSet<QString>& keys);

signals:
    void settingChanged();
    void starToggled(const QString& settingKey, bool starred);
    void dynamicsRequested(const QString& settingKey, const QString& settingLabel);

private slots:
    void onSliderChanged();
    void onToggleChanged(bool checked);
    void onComboChanged();
    void onSegmentedChanged();
    void onThemeChanged();

private:
    void buildRows(const QVector<BrushSettingDef>& defs);
    void applyStyles();
    void updateDynamicsButtonStates(const BrushSettingsData* data);
    void updateDependentControls();
    void applyDynamicsButtonStyle(QPushButton* button);
    QVariant currentSettingValue(const QString& key) const;
    bool dependenciesSatisfied(
        const QVector<ruwa::core::brushes::BrushSettingDependency>& dependencies) const;

    struct SliderRow;
    float sliderPosToValue(const SliderRow& row, int pos) const;
    int valueToSliderPos(const SliderRow& row, float value) const;

    struct SliderRow {
        QString key;
        QVariant defaultValue;
        float min = 0.0f;
        float max = 1.0f;
        float step = 0.01f;
        bool nonlinear = false;
        int displayScale = 100;
        int displayDecimals = 0;
        const char* suffix = "%";
        ruwa::core::brushes::BrushDynamicTargetDef dynamicTarget;
        QVector<ruwa::core::brushes::BrushSettingDependency> enabledWhen;
        QPushButton* starButton = nullptr;
        QPushButton* dynamicsButton = nullptr;
        QWidget* dynamicsPlaceholder = nullptr;
        QLabel* nameLabel = nullptr;
        ProgressHandleSlider* slider = nullptr;
    };

    struct ToggleRow {
        QString key;
        QVariant defaultValue;
        QVector<ruwa::core::brushes::BrushSettingDependency> enabledWhen;
        QPushButton* starButton = nullptr;
        QPushButton* dynamicsButton = nullptr;
        QWidget* dynamicsPlaceholder = nullptr;
        QLabel* nameLabel = nullptr;
        ToggleSwitch* toggle = nullptr;
    };

    struct ComboBoxRow {
        QString key;
        QVariant defaultValue;
        QStringList options;
        QVector<ruwa::core::brushes::BrushSettingDependency> enabledWhen;
        QPushButton* starButton = nullptr;
        QPushButton* dynamicsButton = nullptr;
        QWidget* dynamicsPlaceholder = nullptr;
        QLabel* nameLabel = nullptr;
        AnimatedComboBox* combo = nullptr;
        ImageDropdownSelector* imageSelector = nullptr;

        int currentIndex() const;
        void setCurrentIndex(int index);
        QWidget* inputWidget() const;
        void setInputEnabled(bool enabled);
    };

    struct SegmentedRow {
        QString key;
        QVariant defaultValue;
        QStringList options;
        QVector<ruwa::core::brushes::BrushSettingDependency> enabledWhen;
        QPushButton* starButton = nullptr;
        QPushButton* dynamicsButton = nullptr;
        QWidget* dynamicsPlaceholder = nullptr;
        QLabel* nameLabel = nullptr;
        SegmentedOptionSelector* selector = nullptr;
    };

    struct DynamicInfoRow {
        QString key;
        ruwa::core::brushes::BrushDynamicTargetDef dynamicTarget;
        QPushButton* starButton = nullptr;
        QPushButton* dynamicsButton = nullptr;
        QWidget* dynamicsPlaceholder = nullptr;
        QLabel* nameLabel = nullptr;
        QLabel* infoLabel = nullptr;
    };

    QVBoxLayout* m_layout = nullptr;
    QVector<SliderRow> m_sliderRows;
    QVector<ToggleRow> m_toggleRows;
    QVector<ComboBoxRow> m_comboRows;
    QVector<SegmentedRow> m_segmentedRows;
    QVector<DynamicInfoRow> m_dynamicInfoRows;
    QVector<QWidget*> m_separatorRows;
    QVariantMap m_currentSettings;
    bool m_starMode = false;
    bool m_updating = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_BRUSHSETTINGS_BRUSHSETTINGSWIDGET_H
