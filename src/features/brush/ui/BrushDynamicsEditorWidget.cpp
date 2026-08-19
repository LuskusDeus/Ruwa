// SPDX-License-Identifier: MPL-2.0

#include "features/brush/ui/BrushDynamicsEditorWidget.h"

#include "features/brush/engine/BrushEngineRegistry.h"
#include "features/brush/manager/BrushDynamicsSlotUtils.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/CurveEditorWidget.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

#include <optional>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

constexpr int kTimeDurationSliderFactor = 10;

void makeWidgetTransparent(QWidget* widget)
{
    if (!widget) {
        return;
    }

    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setAttribute(Qt::WA_NoSystemBackground);
    widget->setAutoFillBackground(false);
}

int sliderValueFromTimeDuration(float durationSec)
{
    const float clamped = ruwa::core::brushes::clampBrushTimeDurationSeconds(durationSec);
    return qRound(clamped * static_cast<float>(kTimeDurationSliderFactor));
}

float timeDurationFromSliderValue(int sliderValue)
{
    const float duration
        = static_cast<float>(sliderValue) / static_cast<float>(kTimeDurationSliderFactor);
    return ruwa::core::brushes::clampBrushTimeDurationSeconds(duration);
}

QString formatTimeDurationLabel(float durationSec)
{
    const float clamped = ruwa::core::brushes::clampBrushTimeDurationSeconds(durationSec);
    return QLocale().toString(clamped, 'f', 1);
}

QVector<qreal> evenlySpacedTicks(qreal minValue, qreal maxValue, int segments)
{
    QVector<qreal> ticks;
    if (segments <= 0) {
        ticks.append(minValue);
        ticks.append(maxValue);
        return ticks;
    }
    ticks.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const qreal t = static_cast<qreal>(i) / static_cast<qreal>(segments);
        ticks.append(minValue + (maxValue - minValue) * t);
    }
    return ticks;
}

class BrushDynamicsSourceButton final : public BaseAnimatedButton {
public:
    enum class SourceIcon {
        Pressure,
        Time,
        Random,
        Direction,
    };

    explicit BrushDynamicsSourceButton(const QString& text, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setText(text);
        setFocusPolicy(Qt::NoFocus);
        setHoverDuration(220);
        setActiveDuration(260);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setSourceIcon(SourceIcon icon)
    {
        if (m_icon == icon) {
            return;
        }
        m_icon = icon;
        update();
    }

    void setSourceAvailable(bool available)
    {
        if (m_available == available) {
            return;
        }
        m_available = available;
        setCursor(m_available ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
        update();
    }

    void setSuppressedByOverride(bool suppressed)
    {
        if (m_suppressedByOverride == suppressed) {
            return;
        }
        m_suppressedByOverride = suppressed;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (!m_available) {
            event->accept();
            return;
        }
        BaseAnimatedButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!m_available) {
            event->accept();
            return;
        }
        BaseAnimatedButton::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = theme.scaled(5);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        if (!m_available) {
            painter.setOpacity(0.46);
        }

        QColor activeFill = colors.isDark ? colors.text : colors.primary;
        if (colors.isDark) {
            activeFill.setAlpha(232);
        }

        QColor fill = colors.overlay(0.0);
        fill = ThemeColors::interpolate(fill, colors.overlay(0.07), hoverProgress());
        fill = ThemeColors::interpolate(fill, activeFill, activeProgress());

        QColor activeText = colors.isDark ? colors.surfaceElevated() : colors.textOnPrimary();
        QColor textColor = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress());
        textColor = ThemeColors::interpolate(textColor, activeText, activeProgress());

        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(r, radius, radius);

        painter.setFont(theme.font(ThemeFontRole::Small, isActive() ? QFont::Bold : QFont::Normal));
        painter.setPen(textColor);

        const int iconSize = theme.scaled(12);
        const int iconLeft = theme.scaled(10);
        const QRectF iconRect(iconLeft, (height() - iconSize) * 0.5, iconSize, iconSize);
        drawSourceIcon(painter, iconRect, textColor);

        const int rightMargin = theme.scaled(10);
        const int warningWidth = m_suppressedByOverride ? theme.scaled(10) : 0;
        painter.drawText(rect().adjusted(theme.scaled(29), 0, -(rightMargin + warningWidth), 0),
            Qt::AlignLeft | Qt::AlignVCenter, text());
        if (m_suppressedByOverride) {
            painter.drawText(rect().adjusted(0, 0, -rightMargin, 0),
                Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("!"));
        }
    }

private:
    void drawSourceIcon(QPainter& painter, const QRectF& rect, const QColor& color) const
    {
        const QString iconName = m_available ? iconResourceName() : QStringLiteral("NotAvailable");
        IconProvider::instance().getColoredIcon(iconName, color).paint(&painter, rect.toRect());
    }

    QString iconResourceName() const
    {
        switch (m_icon) {
        case SourceIcon::Pressure:
            return QStringLiteral("PenPressure");
        case SourceIcon::Time:
            return QStringLiteral("Time");
        case SourceIcon::Random:
            return QStringLiteral("Random");
        case SourceIcon::Direction:
            return QStringLiteral("Direction");
        }
        return QString();
    }

private:
    SourceIcon m_icon = SourceIcon::Pressure;
    bool m_available = true;
    bool m_suppressedByOverride = false;
};

} // namespace

BrushDynamicsEditorWidget::CurveAxesConfig BrushDynamicsEditorWidget::curveAxesConfigForSetting(
    const QString& engineId, const BrushSettingsData& settings, const QString& settingKey)
{
    CurveAxesConfig config;
    config.horizontalAxis
        = { 0.0, 1.0, 100.0, 0, QStringLiteral("%"), evenlySpacedTicks(0.0, 1.0, 4), true };

    const auto dynamicsKey
        = ruwa::core::brushes::brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    const qreal baseValue
        = qMax<qreal>(0.0, ruwa::core::brushes::baseValueForDynamicsSetting(settings, dynamicsKey));

    const auto* module
        = ruwa::core::brushes::BrushEngineRegistry::instance().moduleOrPixelFallback(engineId);
    std::optional<ruwa::core::brushes::BrushSettingDef> def;
    if (module) {
        def = ruwa::core::brushes::findSettingDef(module->descriptor(), settingKey);
    }
    if (!def.has_value()) {
        config.verticalAxis.maxValue = baseValue;
        config.verticalAxis.tickValues = evenlySpacedTicks(0.0, baseValue, 4);
        return config;
    }

    QString suffix;
    const QString defSuffix = def->suffix ? QLatin1String(def->suffix) : QString();
    if (!defSuffix.isEmpty() && defSuffix != QStringLiteral("%")) {
        suffix = defSuffix;
    }

    config.verticalAxis.minValue = 0.0;
    config.verticalAxis.maxValue = baseValue;
    config.verticalAxis.displayScale = def->displayScale;
    config.verticalAxis.displayDecimals = def->displayDecimals;
    config.verticalAxis.suffix = suffix;
    config.verticalAxis.tickValues = evenlySpacedTicks(0.0, baseValue, 4);
    config.verticalAxis.visible = true;
    return config;
}

BrushDynamicsEditorWidget::BrushDynamicsBinding BrushDynamicsEditorWidget::defaultTimeBinding(
    BrushDynamicsSettingKey setting)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::Time;
    binding.mode = ruwa::core::brushes::defaultBrushDynamicsBlendMode(setting, binding.source);
    binding.enabled = false;
    binding.durationSec = 1.0f;
    binding.endAction = BrushTimeEndAction::Stop;
    if (setting == BrushDynamicsSettingKey::ShapeAngle) {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 0.0f, 0.65f },
        };
    } else {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 1.0f, 0.65f },
        };
    }
    binding.curve.normalize(binding.setting, binding.mode);
    return binding;
}

BrushDynamicsEditorWidget::BrushDynamicsBinding BrushDynamicsEditorWidget::defaultRandomBinding(
    BrushDynamicsSettingKey setting)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::RandomValue;
    binding.mode = ruwa::core::brushes::defaultBrushDynamicsBlendMode(setting, binding.source);
    binding.enabled = false;
    ruwa::core::brushes::setBrushDynamicsRandomRange(binding, 0.0f, 0.0f);
    return binding;
}

BrushDynamicsEditorWidget::BrushDynamicsBinding
BrushDynamicsEditorWidget::defaultStrokeDirectionBinding(BrushDynamicsSettingKey setting)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::StrokeDirection;
    binding.mode = ruwa::core::brushes::defaultBrushDynamicsBlendMode(setting, binding.source);
    binding.enabled = false;
    if (setting == BrushDynamicsSettingKey::ShapeAngle) {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 360.0f, 0.65f },
        };
    } else {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 1.0f, 0.65f },
        };
    }
    binding.curve.normalize(binding.setting, binding.mode);
    return binding;
}

BrushDynamicsEditorWidget::BrushDynamicsBinding BrushDynamicsEditorWidget::displayBinding(
    BrushDynamicsBinding binding) const
{
    binding.mode = ruwa::core::brushes::normalizeBrushDynamicsBlendMode(
        binding.setting, binding.source, binding.mode);
    if (binding.source == BrushInputSourceKey::Time) {
        if (!binding.hasStoredCurve()) {
            auto fallback = defaultTimeBinding(binding.setting);
            fallback.enabled = binding.enabled;
            fallback.mode = binding.mode;
            fallback.durationSec = binding.durationSec;
            fallback.endAction = binding.endAction;
            binding = fallback;
        }
        binding.durationSec
            = ruwa::core::brushes::clampBrushTimeDurationSeconds(binding.durationSec);
        if (binding.endAction == BrushTimeEndAction::Count) {
            binding.endAction = BrushTimeEndAction::Stop;
        }
    } else if (binding.source == BrushInputSourceKey::RandomValue) {
        if (!binding.hasStoredCurve()) {
            auto fallback = defaultRandomBinding(binding.setting);
            fallback.enabled = binding.enabled;
            fallback.mode = binding.mode;
            const float neutralValue = binding.mode == BrushDynamicsBlendMode::Add ? 0.0f : 1.0f;
            ruwa::core::brushes::setBrushDynamicsRandomRange(fallback, neutralValue, neutralValue);
            binding = fallback;
        } else {
            const auto range = ruwa::core::brushes::brushDynamicsRandomRange(binding);
            ruwa::core::brushes::setBrushDynamicsRandomRange(binding, range.minimum, range.maximum);
        }
    } else if (binding.source == BrushInputSourceKey::StrokeDirection
        && !binding.hasStoredCurve()) {
        auto fallback = defaultStrokeDirectionBinding(binding.setting);
        fallback.enabled = binding.enabled;
        binding = fallback;
    }
    return binding;
}

BrushDynamicsEditorWidget::CurveAxesConfig BrushDynamicsEditorWidget::curveAxesConfigForBinding(
    const BrushDynamicsBinding& binding) const
{
    auto config = m_curveAxesConfig;
    if (binding.source == BrushInputSourceKey::Time) {
        const qreal duration
            = ruwa::core::brushes::clampBrushTimeDurationSeconds(binding.durationSec);
        config.horizontalAxis.minValue = 0.0;
        config.horizontalAxis.maxValue = duration;
        config.horizontalAxis.displayScale = 1.0;
        config.horizontalAxis.displayDecimals = (duration < 1.0) ? 2 : 1;
        config.horizontalAxis.suffix = QStringLiteral("s");
        config.horizontalAxis.tickValues = evenlySpacedTicks(0.0, duration, 4);
    }
    return config;
}

int BrushDynamicsEditorWidget::randomRangeSliderFactor() const
{
    int factor = qMax(1, qRound(m_curveAxesConfig.verticalAxis.displayScale));
    for (int i = 0; i < m_curveAxesConfig.verticalAxis.displayDecimals; ++i) {
        factor *= 10;
    }
    return factor;
}

int BrushDynamicsEditorWidget::sliderValueFromRandomRangeValue(float value) const
{
    return qRound(value * static_cast<float>(randomRangeSliderFactor()));
}

float BrushDynamicsEditorWidget::randomRangeValueFromSliderValue(int sliderValue) const
{
    return static_cast<float>(sliderValue) / static_cast<float>(randomRangeSliderFactor());
}

QString BrushDynamicsEditorWidget::formatRandomRange(
    const ruwa::core::brushes::BrushDynamicsRandomRange& range) const
{
    const auto& axis = m_curveAxesConfig.verticalAxis;
    QString suffix = axis.suffix;
    if (suffix.isEmpty() && qRound(axis.displayScale) == 100) {
        suffix = QStringLiteral("%");
    }
    const auto formatValue = [&axis, &suffix](float value) {
        return QLocale().toString(value * axis.displayScale, 'f', axis.displayDecimals) + suffix;
    };
    return QStringLiteral("%1 – %2").arg(formatValue(range.minimum), formatValue(range.maximum));
}

ToggleSwitch* BrushDynamicsEditorWidget::activeToggle() const
{
    if (m_activeSource == BrushInputSourceKey::Time) {
        return m_timeToggle;
    }
    if (m_activeSource == BrushInputSourceKey::RandomValue) {
        return m_randomToggle;
    }
    if (m_activeSource == BrushInputSourceKey::StrokeDirection) {
        return m_directionToggle;
    }
    return m_pressureToggle;
}

SegmentedOptionSelector* BrushDynamicsEditorWidget::activeModeSelector() const
{
    if (m_activeSource == BrushInputSourceKey::RandomValue) {
        return m_randomModeSelector;
    }
    if (m_activeSource == BrushInputSourceKey::StrokeDirection) {
        return m_directionModeSelector;
    }
    return (m_activeSource == BrushInputSourceKey::Time) ? m_timeModeSelector : m_modeSelector;
}

CurveEditorWidget* BrushDynamicsEditorWidget::activeCurveEditor() const
{
    if (m_activeSource == BrushInputSourceKey::RandomValue) {
        return nullptr;
    }
    if (m_activeSource == BrushInputSourceKey::StrokeDirection) {
        return m_directionCurveEditor;
    }
    return (m_activeSource == BrushInputSourceKey::Time) ? m_timeCurveEditor : m_curveEditor;
}

BrushDynamicsEditorWidget::BrushDynamicsEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    makeWidgetTransparent(this);

    auto* bodyLayout = new QHBoxLayout(this);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(ThemeManager::instance().scaled(22));

    m_sourcesColumn = new QWidget(this);
    m_sourcesColumn->setObjectName(QStringLiteral("brush_dynamics_editor_sources_column"));
    m_sourcesColumn->setAttribute(Qt::WA_StyledBackground, true);
    m_sourcesColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* sourcesLayout = new QVBoxLayout(m_sourcesColumn);
    sourcesLayout->setContentsMargins(ThemeManager::instance().scaled(6),
        ThemeManager::instance().scaled(7), ThemeManager::instance().scaled(6),
        ThemeManager::instance().scaled(7));
    sourcesLayout->setSpacing(ThemeManager::instance().scaled(3));

    m_sourcesLabel = new QLabel(m_sourcesColumn);
    m_sourcesLabel->setObjectName(QStringLiteral("brush_dynamics_editor_sources_label"));

    m_tabletPressureButton = new BrushDynamicsSourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Pressure"), m_sourcesColumn);
    m_timeButton = new BrushDynamicsSourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Time"), m_sourcesColumn);
    m_randomButton = new BrushDynamicsSourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Random"), m_sourcesColumn);
    m_directionButton = new BrushDynamicsSourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Direction"), m_sourcesColumn);
    static_cast<BrushDynamicsSourceButton*>(m_tabletPressureButton)
        ->setSourceIcon(BrushDynamicsSourceButton::SourceIcon::Pressure);
    static_cast<BrushDynamicsSourceButton*>(m_timeButton)
        ->setSourceIcon(BrushDynamicsSourceButton::SourceIcon::Time);
    static_cast<BrushDynamicsSourceButton*>(m_randomButton)
        ->setSourceIcon(BrushDynamicsSourceButton::SourceIcon::Random);
    static_cast<BrushDynamicsSourceButton*>(m_directionButton)
        ->setSourceIcon(BrushDynamicsSourceButton::SourceIcon::Direction);

    sourcesLayout->addWidget(m_sourcesLabel);
    sourcesLayout->addSpacing(ThemeManager::instance().scaled(2));
    sourcesLayout->addWidget(m_tabletPressureButton);
    sourcesLayout->addWidget(m_timeButton);
    sourcesLayout->addWidget(m_randomButton);
    sourcesLayout->addWidget(m_directionButton);
    sourcesLayout->addStretch();

    m_editorStack = new AnimatedStackedWidget(this);
    m_editorStack->setObjectName(QStringLiteral("brush_dynamics_editor_stack"));
    makeWidgetTransparent(m_editorStack);
    m_editorStack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_editorStack->setSlideOrientation(AnimatedStackedWidget::SlideOrientation::Vertical);
    m_editorStack->setAnimationDuration(220);
    m_editorStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_pressurePage = new QWidget(m_editorStack);
    m_pressurePage->setObjectName(QStringLiteral("brush_dynamics_editor_pressure_page"));
    makeWidgetTransparent(m_pressurePage);
    m_pressurePage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* editorLayout = new QVBoxLayout(m_pressurePage);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(ThemeManager::instance().scaled(12));

    auto* pressureRow = new QWidget(m_pressurePage);
    makeWidgetTransparent(pressureRow);
    auto* pressureRowLayout = new QHBoxLayout(pressureRow);
    pressureRowLayout->setContentsMargins(0, 0, 0, 0);
    pressureRowLayout->setSpacing(ThemeManager::instance().scaled(10));

    m_pressureLabel = new QLabel(pressureRow);
    m_pressureLabel->setObjectName(QStringLiteral("brush_dynamics_editor_pressure_label"));
    m_pressureToggle = new ToggleSwitch(pressureRow);

    pressureRowLayout->addWidget(m_pressureLabel);
    pressureRowLayout->addStretch();
    pressureRowLayout->addWidget(m_pressureToggle);

    auto* modeRow = new QWidget(m_pressurePage);
    makeWidgetTransparent(modeRow);
    auto* modeRowLayout = new QHBoxLayout(modeRow);
    modeRowLayout->setContentsMargins(0, 0, 0, 0);
    modeRowLayout->setSpacing(ThemeManager::instance().scaled(10));

    m_modeLabel = new QLabel(modeRow);
    m_modeLabel->setObjectName(QStringLiteral("brush_dynamics_editor_mode_label"));
    m_modeSelector = new SegmentedOptionSelector(modeRow);
    m_modeSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);

    modeRowLayout->addWidget(m_modeLabel);
    modeRowLayout->addWidget(m_modeSelector, 1);

    m_curveEditor = new CurveEditorWidget(m_pressurePage);
    m_curveEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editorLayout->addWidget(pressureRow);
    editorLayout->addWidget(modeRow);
    editorLayout->addWidget(m_curveEditor, 0, Qt::AlignTop);
    editorLayout->addStretch(1);

    m_timePage = new QWidget(m_editorStack);
    m_timePage->setObjectName(QStringLiteral("brush_dynamics_editor_time_page"));
    makeWidgetTransparent(m_timePage);
    m_timePage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* timeLayout = new QVBoxLayout(m_timePage);
    timeLayout->setContentsMargins(0, 0, 0, 0);
    timeLayout->setSpacing(ThemeManager::instance().scaled(12));

    auto* timeToggleRow = new QWidget(m_timePage);
    makeWidgetTransparent(timeToggleRow);
    auto* timeToggleLayout = new QHBoxLayout(timeToggleRow);
    timeToggleLayout->setContentsMargins(0, 0, 0, 0);
    timeToggleLayout->setSpacing(ThemeManager::instance().scaled(10));
    m_timeEnabledLabel = new QLabel(timeToggleRow);
    m_timeEnabledLabel->setObjectName(QStringLiteral("brush_dynamics_editor_time_enabled_label"));
    m_timeToggle = new ToggleSwitch(timeToggleRow);
    timeToggleLayout->addWidget(m_timeEnabledLabel);
    timeToggleLayout->addStretch();
    timeToggleLayout->addWidget(m_timeToggle);

    auto* timeModeRow = new QWidget(m_timePage);
    makeWidgetTransparent(timeModeRow);
    auto* timeModeLayout = new QHBoxLayout(timeModeRow);
    timeModeLayout->setContentsMargins(0, 0, 0, 0);
    timeModeLayout->setSpacing(ThemeManager::instance().scaled(10));
    m_timeModeLabel = new QLabel(timeModeRow);
    m_timeModeLabel->setObjectName(QStringLiteral("brush_dynamics_editor_time_mode_label"));
    m_timeModeSelector = new SegmentedOptionSelector(timeModeRow);
    m_timeModeSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    timeModeLayout->addWidget(m_timeModeLabel);
    timeModeLayout->addWidget(m_timeModeSelector, 1);

    auto* timeDurationRow = new QWidget(m_timePage);
    makeWidgetTransparent(timeDurationRow);
    auto* timeDurationLayout = new QHBoxLayout(timeDurationRow);
    timeDurationLayout->setContentsMargins(0, 0, 0, 0);
    timeDurationLayout->setSpacing(ThemeManager::instance().scaled(12));
    m_timeDurationLabel = new QLabel(timeDurationRow);
    m_timeDurationLabel->setObjectName(QStringLiteral("brush_dynamics_editor_time_duration_label"));
    m_timeDurationSlider = new ProgressHandleSlider(timeDurationRow);
    m_timeDurationSlider->setRange(
        sliderValueFromTimeDuration(0.1f), sliderValueFromTimeDuration(10.0f));
    m_timeDurationSlider->setOrientation(Qt::Horizontal);
    m_timeDurationSlider->setShowValueText(true);
    m_timeDurationSlider->setValueDisplayMode(ProgressHandleSlider::ValueDisplayMode::RawValue);
    m_timeDurationSlider->setValueTextPrefix(QString());
    m_timeDurationSlider->setValueTextSuffix(QString());
    timeDurationLayout->addWidget(m_timeDurationLabel, 1);
    timeDurationLayout->addWidget(m_timeDurationSlider, 0);

    auto* timeEndActionRow = new QWidget(m_timePage);
    makeWidgetTransparent(timeEndActionRow);
    auto* timeEndActionLayout = new QHBoxLayout(timeEndActionRow);
    timeEndActionLayout->setContentsMargins(0, 0, 0, 0);
    timeEndActionLayout->setSpacing(ThemeManager::instance().scaled(12));
    m_timeEndActionLabel = new QLabel(timeEndActionRow);
    m_timeEndActionLabel->setObjectName(
        QStringLiteral("brush_dynamics_editor_time_end_action_label"));
    m_timeEndActionCombo = new AnimatedComboBox(timeEndActionRow);
    m_timeEndActionCombo->setPlaceholderText(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Select"));
    m_timeEndActionCombo->addItem(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Stop"),
        static_cast<int>(BrushTimeEndAction::Stop));
    m_timeEndActionCombo->addItem(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Reverse"),
        static_cast<int>(BrushTimeEndAction::Reverse));
    m_timeEndActionCombo->addItem(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Restart"),
        static_cast<int>(BrushTimeEndAction::Restart));
    timeEndActionLayout->addWidget(m_timeEndActionLabel, 1);
    timeEndActionLayout->addWidget(m_timeEndActionCombo, 0);

    m_timeCurveEditor = new CurveEditorWidget(m_timePage);
    m_timeCurveEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    timeLayout->addWidget(timeToggleRow);
    timeLayout->addWidget(timeModeRow);
    timeLayout->addWidget(m_timeCurveEditor, 0, Qt::AlignTop);
    timeLayout->addWidget(timeDurationRow);
    timeLayout->addWidget(timeEndActionRow);
    timeLayout->addStretch(1);

    m_randomPage = new QWidget(m_editorStack);
    m_randomPage->setObjectName(QStringLiteral("brush_dynamics_editor_random_page"));
    makeWidgetTransparent(m_randomPage);
    m_randomPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* randomLayout = new QVBoxLayout(m_randomPage);
    randomLayout->setContentsMargins(0, 0, 0, 0);
    randomLayout->setSpacing(ThemeManager::instance().scaled(12));

    auto* randomToggleRow = new QWidget(m_randomPage);
    makeWidgetTransparent(randomToggleRow);
    auto* randomToggleLayout = new QHBoxLayout(randomToggleRow);
    randomToggleLayout->setContentsMargins(0, 0, 0, 0);
    randomToggleLayout->setSpacing(ThemeManager::instance().scaled(10));
    m_randomEnabledLabel = new QLabel(randomToggleRow);
    m_randomEnabledLabel->setObjectName(
        QStringLiteral("brush_dynamics_editor_random_enabled_label"));
    m_randomToggle = new ToggleSwitch(randomToggleRow);
    randomToggleLayout->addWidget(m_randomEnabledLabel);
    randomToggleLayout->addStretch();
    randomToggleLayout->addWidget(m_randomToggle);

    auto* randomModeRow = new QWidget(m_randomPage);
    makeWidgetTransparent(randomModeRow);
    auto* randomModeLayout = new QHBoxLayout(randomModeRow);
    randomModeLayout->setContentsMargins(0, 0, 0, 0);
    randomModeLayout->setSpacing(ThemeManager::instance().scaled(10));
    m_randomModeLabel = new QLabel(randomModeRow);
    m_randomModeLabel->setObjectName(QStringLiteral("brush_dynamics_editor_random_mode_label"));
    m_randomModeSelector = new SegmentedOptionSelector(randomModeRow);
    m_randomModeSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    randomModeLayout->addWidget(m_randomModeLabel);
    randomModeLayout->addWidget(m_randomModeSelector, 1);

    auto* randomRangeRow = new QWidget(m_randomPage);
    makeWidgetTransparent(randomRangeRow);
    auto* randomRangeLayout = new QHBoxLayout(randomRangeRow);
    randomRangeLayout->setContentsMargins(0, 0, 0, 0);
    randomRangeLayout->setSpacing(ThemeManager::instance().scaled(12));
    m_randomRangeLabel = new QLabel(randomRangeRow);
    m_randomRangeLabel->setObjectName(QStringLiteral("brush_dynamics_editor_random_range_label"));
    m_randomRangeSlider = new ProgressHandleSlider(randomRangeRow);
    m_randomRangeSlider->setRangeMode(true);
    m_randomRangeSlider->setOrientation(Qt::Horizontal);
    m_randomRangeSlider->setShowValueText(true);
    m_randomRangeSlider->setValueDisplayMode(ProgressHandleSlider::ValueDisplayMode::RawValue);
    m_randomRangeSlider->setValueTextPrefix(QString());
    m_randomRangeSlider->setValueTextSuffix(QString());
    randomRangeLayout->addWidget(m_randomRangeLabel, 1);
    randomRangeLayout->addWidget(m_randomRangeSlider, 0);

    randomLayout->addWidget(randomToggleRow);
    randomLayout->addWidget(randomModeRow);
    randomLayout->addWidget(randomRangeRow);
    randomLayout->addStretch(1);

    m_directionPage = new QWidget(m_editorStack);
    m_directionPage->setObjectName(QStringLiteral("brush_dynamics_editor_direction_page"));
    makeWidgetTransparent(m_directionPage);
    m_directionPage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* directionLayout = new QVBoxLayout(m_directionPage);
    directionLayout->setContentsMargins(0, 0, 0, 0);
    directionLayout->setSpacing(ThemeManager::instance().scaled(12));

    auto* directionToggleRow = new QWidget(m_directionPage);
    makeWidgetTransparent(directionToggleRow);
    auto* directionToggleLayout = new QHBoxLayout(directionToggleRow);
    directionToggleLayout->setContentsMargins(0, 0, 0, 0);
    directionToggleLayout->setSpacing(ThemeManager::instance().scaled(10));
    m_directionEnabledLabel = new QLabel(directionToggleRow);
    m_directionEnabledLabel->setObjectName(
        QStringLiteral("brush_dynamics_editor_direction_enabled_label"));
    m_directionToggle = new ToggleSwitch(directionToggleRow);
    directionToggleLayout->addWidget(m_directionEnabledLabel);
    directionToggleLayout->addStretch();
    directionToggleLayout->addWidget(m_directionToggle);

    auto* directionModeRow = new QWidget(m_directionPage);
    makeWidgetTransparent(directionModeRow);
    auto* directionModeLayout = new QHBoxLayout(directionModeRow);
    directionModeLayout->setContentsMargins(0, 0, 0, 0);
    directionModeLayout->setSpacing(ThemeManager::instance().scaled(10));
    m_directionModeLabel = new QLabel(directionModeRow);
    m_directionModeLabel->setObjectName(
        QStringLiteral("brush_dynamics_editor_direction_mode_label"));
    m_directionModeSelector = new SegmentedOptionSelector(directionModeRow);
    m_directionModeSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    directionModeLayout->addWidget(m_directionModeLabel);
    directionModeLayout->addWidget(m_directionModeSelector, 1);

    m_directionCurveEditor = new CurveEditorWidget(m_directionPage);
    m_directionCurveEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    directionLayout->addWidget(directionToggleRow);
    directionLayout->addWidget(directionModeRow);
    directionLayout->addWidget(m_directionCurveEditor, 0, Qt::AlignTop);
    directionLayout->addStretch(1);

    m_editorStack->addWidget(m_pressurePage);
    m_editorStack->addWidget(m_timePage);
    m_editorStack->addWidget(m_randomPage);
    m_editorStack->addWidget(m_directionPage);

    bodyLayout->addWidget(m_sourcesColumn, 0);
    bodyLayout->addWidget(m_editorStack, 1);

    connect(
        &ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() { updateStyles(); });
    connect(m_tabletPressureButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::TabletPressure); });
    connect(m_timeButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::Time); });
    connect(m_randomButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::RandomValue); });
    connect(m_directionButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::StrokeDirection); });

    const auto connectEnabledToggle = [this](ToggleSwitch* toggle) {
        connect(toggle, &ToggleSwitch::toggled, this, [this](bool checked) {
            auto binding = currentBinding();
            binding.enabled = checked;
            storeCurrentBinding(binding);
            updateTexts();
            emit editingFinished();
        });
    };
    connectEnabledToggle(m_pressureToggle);
    connectEnabledToggle(m_timeToggle);
    connectEnabledToggle(m_randomToggle);
    connectEnabledToggle(m_directionToggle);

    const auto connectModeSelector = [this](SegmentedOptionSelector* selector) {
        connect(selector, &SegmentedOptionSelector::selectionChanged, this, [this, selector]() {
            if (m_syncingModeSelector) {
                return;
            }
            auto binding = currentBinding();
            const int index = selector->currentIndex();
            const auto sourceDef = m_targetDef.sourceDef(m_activeSource);
            if (!sourceDef.has_value() || index < 0
                || index >= sourceDef->allowedBlendModes.size()) {
                return;
            }
            binding.mode = sourceDef->allowedBlendModes[index];
            binding.curve.normalize(binding.setting, binding.mode);
            storeCurrentBinding(binding);
            syncEditorFromCurrentBinding();
        });
    };
    connectModeSelector(m_modeSelector);
    connectModeSelector(m_timeModeSelector);
    connectModeSelector(m_randomModeSelector);
    connectModeSelector(m_directionModeSelector);

    const auto connectCurveEditor = [this](CurveEditorWidget* editor) {
        connect(editor, &CurveEditorWidget::pointsChanged, this, [this, editor]() {
            auto binding = currentBinding();
            binding.curve = editor->curve();
            binding.curve.normalize(binding.setting, binding.mode);
            storeCurrentBinding(binding);
        });
        connect(editor, &CurveEditorWidget::editingFinished, this,
            &BrushDynamicsEditorWidget::editingFinished);
    };
    connectCurveEditor(m_curveEditor);
    connectCurveEditor(m_timeCurveEditor);
    connectCurveEditor(m_directionCurveEditor);

    connect(
        m_timeDurationSlider, &ProgressHandleSlider::valueChanged, this, [this](int sliderValue) {
            auto binding = currentBinding();
            binding.durationSec = timeDurationFromSliderValue(sliderValue);
            storeCurrentBinding(binding);
            syncEditorFromCurrentBinding();
        });
    connect(m_timeDurationSlider, &ProgressHandleSlider::sliderReleased, this,
        &BrushDynamicsEditorWidget::editingFinished);
    connect(m_timeEndActionCombo, &AnimatedComboBox::currentIndexChanged, this, [this]() {
        auto binding = currentBinding();
        const int rawAction = m_timeEndActionCombo ? m_timeEndActionCombo->currentData().toInt()
                                                   : static_cast<int>(binding.endAction);
        binding.endAction = static_cast<BrushTimeEndAction>(rawAction);
        storeCurrentBinding(binding);
        syncEditorFromCurrentBinding();
        emit editingFinished();
    });
    connect(m_randomRangeSlider, &ProgressHandleSlider::rangeValuesChanged, this,
        [this](int lowerValue, int upperValue) {
            auto binding = currentBinding();
            ruwa::core::brushes::setBrushDynamicsRandomRange(binding,
                randomRangeValueFromSliderValue(lowerValue),
                randomRangeValueFromSliderValue(upperValue));
            storeCurrentBinding(binding);
            syncEditorFromCurrentBinding();
        });
    connect(m_randomRangeSlider, &ProgressHandleSlider::sliderReleased, this,
        &BrushDynamicsEditorWidget::editingFinished);

    updateTexts();
    updateStyles();
}

void BrushDynamicsEditorWidget::setTarget(const QString& settingKey, const BrushDynamicsSlot& slot,
    const BrushDynamicTargetDef& targetDef, CurveAxesConfig curveAxesConfig)
{
    m_settingKey = settingKey;
    m_slot = slot;
    m_targetDef = targetDef;
    m_curveAxesConfig = curveAxesConfig;

    setActiveSource(m_activeSource);
    syncEditorFromCurrentBinding();
}

void BrushDynamicsEditorWidget::setCurveAxesConfig(CurveAxesConfig curveAxesConfig)
{
    m_curveAxesConfig = curveAxesConfig;
    if (m_curveEditor) {
        syncEditorFromCurrentBinding();
    }
}

QString BrushDynamicsEditorWidget::settingKey() const
{
    return m_settingKey;
}

BrushDynamicsEditorWidget::BrushDynamicsSlot BrushDynamicsEditorWidget::slot() const
{
    return m_slot;
}

BrushDynamicsEditorWidget::BrushInputSourceKey BrushDynamicsEditorWidget::activeSource() const
{
    return m_activeSource;
}

void BrushDynamicsEditorWidget::setCompact(bool compact)
{
    if (m_compact == compact) {
        return;
    }
    m_compact = compact;
    updateStyles();
}

bool BrushDynamicsEditorWidget::isCompact() const
{
    return m_compact;
}

bool BrushDynamicsEditorWidget::isSourceAvailable(BrushInputSourceKey source) const
{
    const auto sourceDef = m_targetDef.sourceDef(source);
    if (!sourceDef.has_value()) {
        return false;
    }
    return sourceDef->available;
}

BrushDynamicsEditorWidget::BrushInputSourceKey BrushDynamicsEditorWidget::fallbackSource() const
{
    for (const auto& sourceDef : m_targetDef.sources) {
        if (sourceDef.available
            && ruwa::core::brushes::supportsBrushInputSource(sourceDef.source)) {
            return sourceDef.source;
        }
    }
    return BrushInputSourceKey::TabletPressure;
}

int BrushDynamicsEditorWidget::sourcePageIndex(BrushInputSourceKey source) const
{
    switch (source) {
    case BrushInputSourceKey::TabletPressure:
        return 0;
    case BrushInputSourceKey::Time:
        return 1;
    case BrushInputSourceKey::RandomValue:
        return 2;
    case BrushInputSourceKey::StrokeDirection:
        return 3;
    case BrushInputSourceKey::StrokeProgress:
    case BrushInputSourceKey::None:
    case BrushInputSourceKey::Count:
        break;
    }
    return 0;
}

void BrushDynamicsEditorWidget::setActiveSource(BrushInputSourceKey source)
{
    if (!isSourceAvailable(source)) {
        source = fallbackSource();
    }

    if (m_activeSource == source) {
        updateSourceButtons();
        updateTexts();
        return;
    }

    m_activeSource = source;
    updateSourceButtons();
    updateTexts();
    syncEditorFromCurrentBinding();
    emit activeSourceChanged(m_activeSource);
}

BrushDynamicsEditorWidget::BrushDynamicsBinding BrushDynamicsEditorWidget::currentBinding() const
{
    if (!ruwa::core::brushes::supportsBrushInputSource(m_activeSource)) {
        BrushDynamicsBinding binding;
        binding.setting = m_slot.setting;
        return binding;
    }
    auto binding = m_slot.binding(m_activeSource);
    binding.setting = m_slot.setting;
    binding.source = m_activeSource;
    return displayBinding(binding);
}

BrushDynamicsEditorWidget::BrushDynamicsBinding BrushDynamicsEditorWidget::defaultBindingForSource(
    BrushInputSourceKey source) const
{
    if (source == BrushInputSourceKey::Time) {
        return defaultTimeBinding(m_slot.setting);
    }
    if (source == BrushInputSourceKey::RandomValue) {
        return defaultRandomBinding(m_slot.setting);
    }
    if (source == BrushInputSourceKey::StrokeDirection) {
        return defaultStrokeDirectionBinding(m_slot.setting);
    }

    BrushDynamicsBinding binding;
    binding.setting = m_slot.setting;
    binding.source = BrushInputSourceKey::TabletPressure;
    binding.mode
        = ruwa::core::brushes::defaultBrushDynamicsBlendMode(m_slot.setting, binding.source);
    if (m_slot.setting == BrushDynamicsSettingKey::ShapeAngle
        || m_slot.setting == BrushDynamicsSettingKey::ColorHue) {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 0.0f, 0.65f },
        };
    } else {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 1.0f, 0.65f },
        };
    }
    binding.curve.normalize(binding.setting, binding.mode);
    return binding;
}

void BrushDynamicsEditorWidget::resetActiveSourceBinding()
{
    if (!ruwa::core::brushes::supportsBrushInputSource(m_activeSource)) {
        return;
    }

    auto binding = defaultBindingForSource(m_activeSource);
    binding.source = m_activeSource;
    storeCurrentBinding(binding);
    syncEditorFromCurrentBinding();
    emit editingFinished();
}

void BrushDynamicsEditorWidget::storeCurrentBinding(
    const BrushDynamicsBinding& binding, bool emitSlotChanged)
{
    if (!ruwa::core::brushes::supportsBrushInputSource(binding.source)
        || !ruwa::core::brushes::supportsBrushDynamicsSetting(m_slot.setting)) {
        return;
    }

    auto normalized = binding;
    normalized.setting = m_slot.setting;
    normalized.source = binding.source;
    normalized.mode = ruwa::core::brushes::normalizeBrushDynamicsBlendMode(
        normalized.setting, normalized.source, normalized.mode);
    if (normalized.source == BrushInputSourceKey::RandomValue) {
        const auto range = ruwa::core::brushes::brushDynamicsRandomRange(normalized);
        ruwa::core::brushes::setBrushDynamicsRandomRange(normalized, range.minimum, range.maximum);
    }
    normalized.durationSec
        = ruwa::core::brushes::clampBrushTimeDurationSeconds(normalized.durationSec);
    if (normalized.endAction == BrushTimeEndAction::Count) {
        normalized.endAction = BrushTimeEndAction::Stop;
    }
    normalized.curve.normalize(normalized.setting, normalized.mode);
    m_slot.binding(normalized.source) = normalized;

    if (normalized.mode == BrushDynamicsBlendMode::Override && normalized.isActive()) {
        for (auto& otherBinding : m_slot.bindings) {
            if (otherBinding.source != normalized.source
                && otherBinding.mode == BrushDynamicsBlendMode::Override) {
                otherBinding.enabled = false;
            }
        }
    }

    updateSourceButtons();
    if (emitSlotChanged) {
        emit slotChanged(m_settingKey, m_slot);
    }
}

void BrushDynamicsEditorWidget::syncEditorFromCurrentBinding()
{
    const auto bindingForSource = [this](BrushInputSourceKey source) {
        auto binding = m_slot.binding(source);
        binding.setting = m_slot.setting;
        binding.source = source;
        return displayBinding(binding);
    };
    const auto syncCurveEditor = [this](CurveEditorWidget* editor,
                                     const BrushDynamicsBinding& binding) {
        if (!editor) {
            return;
        }

        const auto axesConfig = curveAxesConfigForBinding(binding);
        auto verticalAxis = axesConfig.verticalAxis;
        const qreal minValue
            = ruwa::core::brushes::brushDynamicsBindingValueMin(binding.setting, binding.mode);
        const qreal maxValue
            = ruwa::core::brushes::brushDynamicsBindingValueMax(binding.setting, binding.mode);
        if (binding.mode == BrushDynamicsBlendMode::Add
            || binding.mode == BrushDynamicsBlendMode::Override) {
            verticalAxis.minValue = minValue;
            verticalAxis.maxValue = maxValue;
            verticalAxis.tickValues = { minValue, (minValue + maxValue) * 0.5, maxValue };
        }

        editor->setVerticalRange(minValue, maxValue);
        editor->setHorizontalAxisDisplay(axesConfig.horizontalAxis);
        editor->setVerticalAxisDisplay(verticalAxis);
        editor->setCurve(binding.curve);
    };

    const auto pressureBinding = bindingForSource(BrushInputSourceKey::TabletPressure);
    const auto timeBinding = bindingForSource(BrushInputSourceKey::Time);
    const auto randomBinding = bindingForSource(BrushInputSourceKey::RandomValue);
    const auto directionBinding = bindingForSource(BrushInputSourceKey::StrokeDirection);
    const auto activeBinding = currentBinding();

    if (m_curveEditor) {
        syncCurveEditor(m_curveEditor, pressureBinding);
    }

    if (m_timeCurveEditor) {
        syncCurveEditor(m_timeCurveEditor, timeBinding);
    }

    if (m_directionCurveEditor) {
        syncCurveEditor(m_directionCurveEditor, directionBinding);
    }

    if (auto* toggle = activeToggle()) {
        const QSignalBlocker blocker(toggle);
        toggle->setChecked(activeBinding.enabled, ToggleSwitch::TransitionMode::Instant);
    }

    if (m_timeDurationSlider) {
        const QSignalBlocker blocker(m_timeDurationSlider);
        m_timeDurationSlider->setValue(sliderValueFromTimeDuration(timeBinding.durationSec));
        m_timeDurationSlider->setCustomDisplayText(
            formatTimeDurationLabel(timeBinding.durationSec));
    }
    if (m_timeEndActionCombo) {
        const QSignalBlocker blocker(m_timeEndActionCombo);
        const int comboIndex
            = m_timeEndActionCombo->findIndexByData(static_cast<int>(timeBinding.endAction));
        if (comboIndex >= 0) {
            m_timeEndActionCombo->setCurrentIndex(comboIndex);
        }
    }
    if (m_randomRangeSlider) {
        const QSignalBlocker blocker(m_randomRangeSlider);
        const float minimum = ruwa::core::brushes::brushDynamicsBindingValueMin(
            randomBinding.setting, randomBinding.mode);
        const float maximum = ruwa::core::brushes::brushDynamicsBindingValueMax(
            randomBinding.setting, randomBinding.mode);
        const auto range = ruwa::core::brushes::brushDynamicsRandomRange(randomBinding);
        m_randomRangeSlider->setRange(
            sliderValueFromRandomRangeValue(minimum), sliderValueFromRandomRangeValue(maximum));
        m_randomRangeSlider->setRangeValues(sliderValueFromRandomRangeValue(range.minimum),
            sliderValueFromRandomRangeValue(range.maximum));
        m_randomRangeSlider->setCustomDisplayText(formatRandomRange(range));
    }

    updateModeSelector();
}

void BrushDynamicsEditorWidget::updateModeSelector()
{
    auto* modeSelector = activeModeSelector();
    QLabel* modeLabel = m_modeLabel;
    if (m_activeSource == BrushInputSourceKey::Time) {
        modeLabel = m_timeModeLabel;
    } else if (m_activeSource == BrushInputSourceKey::RandomValue) {
        modeLabel = m_randomModeLabel;
    } else if (m_activeSource == BrushInputSourceKey::StrokeDirection) {
        modeLabel = m_directionModeLabel;
    }
    if (!modeSelector) {
        if (modeLabel) {
            modeLabel->setVisible(false);
        }
        if (m_activeSource == BrushInputSourceKey::StrokeDirection && m_directionModeSelector) {
            m_directionModeSelector->setVisible(false);
        }
        return;
    }

    const auto sourceDef = m_targetDef.sourceDef(m_activeSource);
    QVector<SegmentedOptionSelector::Option> options;
    QVector<BrushDynamicsBlendMode> optionModes;
    int currentIndex = 0;
    if (sourceDef.has_value()) {
        options.reserve(sourceDef->allowedBlendModes.size());
        optionModes.reserve(sourceDef->allowedBlendModes.size());
        const auto binding = currentBinding();
        for (int i = 0; i < sourceDef->allowedBlendModes.size(); ++i) {
            const auto mode = sourceDef->allowedBlendModes[i];
            optionModes.append(mode);
            SegmentedOptionSelector::Option option;
            switch (mode) {
            case BrushDynamicsBlendMode::Multiply:
                option.text
                    = QCoreApplication::translate("BrushEditorParameterOverlay", "Multiply");
                break;
            case BrushDynamicsBlendMode::Add:
                option.text = QCoreApplication::translate("BrushEditorParameterOverlay", "Add");
                break;
            case BrushDynamicsBlendMode::Override:
                option.text
                    = QCoreApplication::translate("BrushEditorParameterOverlay", "Override");
                break;
            case BrushDynamicsBlendMode::Count:
                option.text = QStringLiteral("?");
                break;
            }
            option.data = static_cast<int>(mode);
            options.append(option);
            if (mode == binding.mode) {
                currentIndex = i;
            }
        }
    }

    QString optionsSignature;
    optionsSignature.reserve(optionModes.size() * 4);
    for (const auto mode : optionModes) {
        if (!optionsSignature.isEmpty()) {
            optionsSignature.append(QLatin1Char(','));
        }
        optionsSignature.append(QString::number(static_cast<int>(mode)));
    }

    m_syncingModeSelector = true;
    const bool optionsChanged = modeSelector->optionCount() != options.size()
        || modeSelector->property("_brushModeOptionsSignature").toString() != optionsSignature;
    if (optionsChanged) {
        modeSelector->setOptions(options);
        modeSelector->setProperty("_brushModeOptionsSignature", optionsSignature);
    }
    m_modeOptions = optionModes;

    if (!options.isEmpty() && modeSelector->currentIndex() != currentIndex) {
        modeSelector->setCurrentIndex(currentIndex, !optionsChanged);
    }
    m_syncingModeSelector = false;
    const bool showModeSelector = options.size() > 1;
    modeSelector->setVisible(showModeSelector);
    if (modeLabel) {
        modeLabel->setVisible(showModeSelector);
    }
}

void BrushDynamicsEditorWidget::updateTexts()
{
    if (m_pressureLabel) {
        m_pressureLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Enabled"));
    }
    if (m_modeLabel) {
        m_modeLabel->setText(QCoreApplication::translate("BrushEditorParameterOverlay", "Mode"));
    }
    if (m_timeEnabledLabel) {
        m_timeEnabledLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Enabled"));
    }
    if (m_timeModeLabel) {
        m_timeModeLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Mode"));
    }
    if (m_sourcesLabel) {
        m_sourcesLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "INPUTS"));
    }
    if (m_tabletPressureButton) {
        m_tabletPressureButton->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Pressure"));
    }
    if (m_timeButton) {
        m_timeButton->setText(QCoreApplication::translate("BrushEditorParameterOverlay", "Time"));
    }
    if (m_randomButton) {
        m_randomButton->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Random"));
    }
    if (m_directionButton) {
        m_directionButton->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Direction"));
    }
    if (m_timeDurationLabel) {
        m_timeDurationLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Duration (sec)"));
    }
    if (m_timeEndActionLabel) {
        m_timeEndActionLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "End Action"));
    }
    if (m_randomEnabledLabel) {
        m_randomEnabledLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Enabled"));
    }
    if (m_randomModeLabel) {
        m_randomModeLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Mode"));
    }
    if (m_randomRangeLabel) {
        m_randomRangeLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Range"));
    }
    if (m_directionEnabledLabel) {
        m_directionEnabledLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Enabled"));
    }
    if (m_directionModeLabel) {
        m_directionModeLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Mode"));
    }
}

void BrushDynamicsEditorWidget::updateSourceButtons()
{
    const auto* overrideBinding = m_slot.activeOverrideBinding();
    const auto isSuppressedByOverride = [this, overrideBinding](BrushInputSourceKey source) {
        return overrideBinding && overrideBinding->source != source
            && m_slot.binding(source).isActive();
    };
    const auto syncButton = [this, &isSuppressedByOverride](
                                QPushButton* rawButton, BrushInputSourceKey source) {
        auto* button = static_cast<BrushDynamicsSourceButton*>(rawButton);
        if (!button) {
            return;
        }
        const bool enabled = m_targetDef.sourceDef(source).has_value() && isSourceAvailable(source);
        button->setVisible(true);
        button->setEnabled(true);
        button->setSourceAvailable(enabled);
        button->setSuppressedByOverride(isSuppressedByOverride(source));
        button->setActive(enabled && m_activeSource == source);
    };

    syncButton(m_tabletPressureButton, BrushInputSourceKey::TabletPressure);
    syncButton(m_timeButton, BrushInputSourceKey::Time);
    syncButton(m_randomButton, BrushInputSourceKey::RandomValue);
    syncButton(m_directionButton, BrushInputSourceKey::StrokeDirection);

    if (m_sourcesColumn) {
        m_sourcesColumn->setVisible(true);
    }
    if (m_editorStack) {
        m_editorStack->setCurrentIndex(sourcePageIndex(m_activeSource));
    }
}

void BrushDynamicsEditorWidget::updateStyles()
{
    auto& theme = ThemeManager::instance();
    const auto& colors = WidgetStyleManager::instance().colors();
    const int timeControlWidth = theme.scaled(m_compact ? 168 : 220);
    const int curveHeight = theme.scaled(m_compact ? 170 : 250);

    const QFont sectionFont = theme.font(ThemeFontRole::Small, QFont::Bold);
    const QFont sourcesHeaderFont = theme.font(ThemeFontRole::Micro, QFont::Bold);
    m_sourcesLabel->setFont(sourcesHeaderFont);

    const QString labelStyle = QStringLiteral("QLabel { background: transparent; color: %1; }")
                                   .arg(colors.text.name(QColor::HexArgb));
    const auto styleLabel = [&labelStyle, &sectionFont](QLabel* label, int minimumWidth = 0) {
        if (!label) {
            return;
        }
        label->setFont(sectionFont);
        label->setStyleSheet(labelStyle);
        if (minimumWidth > 0) {
            label->setMinimumWidth(minimumWidth);
        }
    };

    styleLabel(m_pressureLabel);
    styleLabel(m_modeLabel);
    styleLabel(m_timeEnabledLabel);
    styleLabel(m_timeModeLabel);
    styleLabel(m_timeDurationLabel, theme.scaled(92));
    styleLabel(m_timeEndActionLabel, theme.scaled(92));
    styleLabel(m_randomEnabledLabel);
    styleLabel(m_randomModeLabel);
    styleLabel(m_randomRangeLabel, theme.scaled(92));
    styleLabel(m_directionEnabledLabel);
    styleLabel(m_directionModeLabel);

    const QString sourcesPanelStyle
        = QStringLiteral("QWidget#brush_dynamics_editor_sources_column { background: %1; "
                         "border: 1px solid %2; border-radius: %3px; }")
              .arg(colors.surfaceAlt.name(QColor::HexArgb),
                  colors.borderSubtle().name(QColor::HexArgb), QString::number(theme.scaled(10)));
    m_sourcesColumn->setStyleSheet(sourcesPanelStyle);
    m_sourcesLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));
    m_sourcesLabel->setContentsMargins(theme.scaled(4), 0, 0, 0);

    if (auto* bodyLayout = qobject_cast<QHBoxLayout*>(layout())) {
        bodyLayout->setSpacing(theme.scaled(m_compact ? 12 : 16));
    }
    if (auto* sourcesLayout = qobject_cast<QVBoxLayout*>(m_sourcesColumn->layout())) {
        sourcesLayout->setContentsMargins(
            theme.scaled(6), theme.scaled(7), theme.scaled(6), theme.scaled(7));
        sourcesLayout->setSpacing(theme.scaled(3));
    }
    for (QWidget* page : { m_pressurePage, m_timePage, m_randomPage, m_directionPage }) {
        if (auto* pageLayout = qobject_cast<QVBoxLayout*>(page->layout())) {
            pageLayout->setSpacing(theme.scaled(m_compact ? 10 : 12));
        }
    }

    m_sourcesColumn->setFixedWidth(theme.scaled(m_compact ? 132 : 178));
    m_sourcesColumn->setMinimumHeight(m_compact ? 0 : theme.scaled(336));
    if (m_editorStack) {
        m_editorStack->setMinimumHeight(theme.scaled(m_compact ? 246 : 330));
    }
    for (CurveEditorWidget* editor : { m_curveEditor, m_timeCurveEditor, m_directionCurveEditor }) {
        if (!editor) {
            continue;
        }
        editor->setMinimumHeight(curveHeight);
        editor->setMaximumHeight(curveHeight);
    }
    for (ProgressHandleSlider* slider : { m_timeDurationSlider, m_randomRangeSlider }) {
        if (!slider) {
            continue;
        }
        slider->setMinimumHeight(theme.scaled(22));
        slider->setMaximumHeight(theme.scaled(22));
        slider->setFixedWidth(timeControlWidth);
        slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if (m_timeEndActionCombo) {
        m_timeEndActionCombo->setFixedHeight(theme.scaled(24));
        m_timeEndActionCombo->setFixedWidth(timeControlWidth);
        m_timeEndActionCombo->setPopupMinWidth(timeControlWidth);
        m_timeEndActionCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    for (ToggleSwitch* toggle :
        { m_pressureToggle, m_timeToggle, m_randomToggle, m_directionToggle }) {
        if (toggle) {
            toggle->setFixedSize(theme.scaled(40), theme.scaled(20));
        }
    }
    for (QPushButton* button :
        { m_tabletPressureButton, m_timeButton, m_randomButton, m_directionButton }) {
        if (!button) {
            continue;
        }
        button->setMinimumHeight(theme.scaled(22));
        button->setMaximumHeight(theme.scaled(22));
    }

    m_sourcesColumn->style()->unpolish(m_sourcesColumn);
    m_sourcesColumn->style()->polish(m_sourcesColumn);
    updateSourceButtons();
}

} // namespace ruwa::ui::widgets
