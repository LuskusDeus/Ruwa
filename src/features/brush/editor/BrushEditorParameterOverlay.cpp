// SPDX-License-Identifier: MPL-2.0

#include "features/brush/editor/BrushEditorParameterOverlay.h"

#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/CurveEditorWidget.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"

#include <QCoreApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QLocale>
#include <QStyle>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace ruwa::ui::windows {

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

class BrushEditorOverlayCloseButton final : public widgets::BaseAnimatedButton {
public:
    explicit BrushEditorOverlayCloseButton(QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setHoverDuration(160);
        setActiveDuration(110);
        setFixedSize(28, 28);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = theme.scaled(8);

        QColor fill = ThemeColors::withAlpha(colors.surfaceElevated(), 0);
        fill = ThemeColors::interpolate(fill, colors.overlayHover(), hoverProgress());
        if (isPressed()) {
            fill = colors.overlay(0.14);
        }

        QColor iconColor
            = ThemeColors::interpolate(colors.textDisabled(), colors.text, hoverProgress());
        if (isPressed()) {
            iconColor = colors.text;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(r, radius, radius);

        const int iconSize = qMax(theme.scaled(10), qMin(width(), height()) - theme.scaled(14));
        const QRect iconRect(
            (width() - iconSize) / 2, (height() - iconSize) / 2, iconSize, iconSize);
        IconProvider::instance()
            .getColoredIcon(IconProvider::StandardIcon::Close, iconColor)
            .paint(&painter, iconRect);
    }
};

class BrushEditorOverlaySourceButton final : public widgets::BaseAnimatedButton {
public:
    enum class SourceIcon {
        Pressure,
        Time,
        Random,
        Direction,
    };

    explicit BrushEditorOverlaySourceButton(const QString& text, QWidget* parent = nullptr)
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

        QFont textFont = font();
        textFont.setPixelSize(theme.scaled(10));
        textFont.setBold(isActive());
        painter.setFont(textFont);
        painter.setPen(textColor);

        const int iconSize = theme.scaled(12);
        const int iconLeft = theme.scaled(10);
        const QRectF iconRect(iconLeft, (height() - iconSize) * 0.5, iconSize, iconSize);
        drawSourceIcon(painter, iconRect, textColor);

        painter.drawText(rect().adjusted(theme.scaled(29), 0, -theme.scaled(10), 0),
            Qt::AlignLeft | Qt::AlignVCenter, text());
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
};

QVector<ruwa::ui::widgets::CurveEditorWidget::Point> curveEditorPointsFromBinding(
    const BrushEditorParameterOverlay::BrushDynamicsBinding& binding)
{
    QVector<ruwa::ui::widgets::CurveEditorWidget::Point> points;
    points.reserve(static_cast<qsizetype>(binding.curve.points.size()));
    for (const auto& sourcePoint : binding.curve.points) {
        ruwa::ui::widgets::CurveEditorWidget::Point point;
        point.x = sourcePoint.x;
        point.y = sourcePoint.y;
        point.smoothness = sourcePoint.smoothness;
        points.append(point);
    }
    return points;
}

BrushEditorParameterOverlay::BrushDynamicsBinding bindingFromCurveEditor(
    const BrushEditorParameterOverlay::BrushDynamicsBinding& baseBinding,
    const QVector<ruwa::ui::widgets::CurveEditorWidget::Point>& points)
{
    auto binding = baseBinding;
    binding.curve.points.clear();
    binding.curve.points.reserve(static_cast<std::size_t>(points.size()));
    for (const auto& sourcePoint : points) {
        ruwa::core::brushes::BrushMappingPoint point;
        point.x = static_cast<float>(sourcePoint.x);
        point.y = static_cast<float>(sourcePoint.y);
        point.smoothness = static_cast<float>(sourcePoint.smoothness);
        binding.curve.points.push_back(point);
    }
    binding.curve.normalize(binding.setting, binding.mode);
    return binding;
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

} // namespace

BrushEditorParameterOverlay::BrushDynamicsBinding BrushEditorParameterOverlay::defaultTimeBinding(
    ruwa::core::brushes::BrushDynamicsSettingKey setting)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::Time;
    binding.mode = ruwa::core::brushes::defaultBrushDynamicsBlendMode(setting);
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

BrushEditorParameterOverlay::BrushDynamicsBinding BrushEditorParameterOverlay::defaultRandomBinding(
    ruwa::core::brushes::BrushDynamicsSettingKey setting)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::RandomValue;
    binding.mode = BrushDynamicsBlendMode::Add;
    binding.enabled = false;
    ruwa::core::brushes::setBrushDynamicsRandomAmount(binding, 0.0f);
    return binding;
}

BrushEditorParameterOverlay::BrushDynamicsBinding
BrushEditorParameterOverlay::defaultStrokeDirectionBinding(
    ruwa::core::brushes::BrushDynamicsSettingKey setting)
{
    BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = BrushInputSourceKey::StrokeDirection;
    binding.mode = (setting == BrushDynamicsSettingKey::ShapeAngle)
        ? BrushDynamicsBlendMode::Override
        : ruwa::core::brushes::defaultBrushDynamicsBlendMode(setting);
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

BrushEditorParameterOverlay::BrushDynamicsBinding BrushEditorParameterOverlay::displayBinding(
    BrushDynamicsBinding binding) const
{
    if (binding.source == BrushInputSourceKey::RandomValue) {
        binding.mode = BrushDynamicsBlendMode::Add;
    } else if (binding.source == BrushInputSourceKey::StrokeDirection
        && binding.setting == BrushDynamicsSettingKey::ShapeAngle) {
        binding.mode = BrushDynamicsBlendMode::Override;
    } else {
        binding.mode
            = ruwa::core::brushes::normalizeBrushDynamicsBlendMode(binding.setting, binding.mode);
    }
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
        const float amount = ruwa::core::brushes::brushDynamicsRandomAmount(binding);
        const bool enabled = binding.enabled;
        binding = defaultRandomBinding(binding.setting);
        binding.enabled = enabled;
        ruwa::core::brushes::setBrushDynamicsRandomAmount(binding, amount);
    } else if (binding.source == BrushInputSourceKey::StrokeDirection
        && !binding.hasStoredCurve()) {
        auto fallback = defaultStrokeDirectionBinding(binding.setting);
        fallback.enabled = binding.enabled;
        fallback.mode = binding.mode;
        binding = fallback;
    }
    return binding;
}

BrushEditorParameterOverlay::CurveAxesConfig BrushEditorParameterOverlay::curveAxesConfigForBinding(
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

int BrushEditorParameterOverlay::randomAmountSliderFactor() const
{
    int factor = qMax(1, qRound(m_curveAxesConfig.verticalAxis.displayScale));
    for (int i = 0; i < m_curveAxesConfig.verticalAxis.displayDecimals; ++i) {
        factor *= 10;
    }
    return factor;
}

int BrushEditorParameterOverlay::sliderValueFromRandomAmount(float amount) const
{
    return qRound(ruwa::core::brushes::clampRange(amount, 0.0f,
                      ruwa::core::brushes::brushDynamicsRandomAmountMax(m_slot.setting))
        * static_cast<float>(randomAmountSliderFactor()));
}

float BrushEditorParameterOverlay::randomAmountFromSliderValue(int sliderValue) const
{
    return ruwa::core::brushes::clampRange(
        static_cast<float>(sliderValue) / static_cast<float>(randomAmountSliderFactor()), 0.0f,
        ruwa::core::brushes::brushDynamicsRandomAmountMax(m_slot.setting));
}

QString BrushEditorParameterOverlay::formatRandomAmount(float amount) const
{
    const auto& axis = m_curveAxesConfig.verticalAxis;
    QString suffix = axis.suffix;
    if (suffix.isEmpty() && qRound(axis.displayScale) == 100) {
        suffix = QStringLiteral("%");
    }
    return QLocale().toString(amount * axis.displayScale, 'f', axis.displayDecimals) + suffix;
}

widgets::ToggleSwitch* BrushEditorParameterOverlay::activeToggle() const
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

widgets::SegmentedOptionSelector* BrushEditorParameterOverlay::activeModeSelector() const
{
    if (m_activeSource == BrushInputSourceKey::RandomValue) {
        return nullptr;
    }
    if (isShapeAngleStrokeDirection()) {
        return nullptr;
    }
    if (m_activeSource == BrushInputSourceKey::StrokeDirection) {
        return m_directionModeSelector;
    }
    return (m_activeSource == BrushInputSourceKey::Time) ? m_timeModeSelector : m_modeSelector;
}

widgets::CurveEditorWidget* BrushEditorParameterOverlay::activeCurveEditor() const
{
    if (m_activeSource == BrushInputSourceKey::RandomValue) {
        return nullptr;
    }
    if (isShapeAngleStrokeDirection()) {
        return nullptr;
    }
    if (m_activeSource == BrushInputSourceKey::StrokeDirection) {
        return m_directionCurveEditor;
    }
    return (m_activeSource == BrushInputSourceKey::Time) ? m_timeCurveEditor : m_curveEditor;
}

BrushEditorParameterOverlay::BrushEditorParameterOverlay(QWidget* parent)
    : QWidget(parent)
{
    makeWidgetTransparent(this);
    setFocusPolicy(Qt::StrongFocus);

    m_panel = new QWidget(this);
    m_panel->setObjectName(QStringLiteral("brush_editor_parameter_overlay_panel"));
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    m_panelOpacityEffect = new QGraphicsOpacityEffect(m_panel);
    m_panelOpacityEffect->setOpacity(0.0);
    m_panel->setGraphicsEffect(m_panelOpacityEffect);

    auto* panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    auto* header = new QWidget(m_panel);
    makeWidgetTransparent(header);
    header->setObjectName(QStringLiteral("brush_editor_parameter_overlay_header"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    m_titleLabel = new QLabel(header);
    m_titleLabel->setObjectName(QStringLiteral("brush_editor_parameter_overlay_title"));
    m_titleLabel->setText(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Parameter Dynamics"));

    m_resetButton = new widgets::CapsuleButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Reset"),
        widgets::CapsuleButton::Variant::Secondary, header);
    m_resetButton->setBaseMinimumWidth(0);
    m_resetButton->setBannerBaseHeight(36);
    m_resetButton->setIcon(
        ThemeManager::instance().icons().getIcon(IconProvider::StandardIcon::UndoArrow));
    m_resetButton->setSizeScale(0.78);
    m_resetButton->syncSizeToText();
    connect(m_resetButton, &QPushButton::clicked, this, [this]() { resetActiveSourceBinding(); });

    m_closeButton = new BrushEditorOverlayCloseButton(header);
    connect(m_closeButton, &QPushButton::clicked, this, [this]() { hideOverlay(); });

    auto* titleRowLayout = new QHBoxLayout();
    titleRowLayout->setContentsMargins(ThemeManager::instance().scaled(6), 0, 0, 0);
    titleRowLayout->setSpacing(ThemeManager::instance().scaled(8));
    titleRowLayout->addWidget(m_titleLabel, 1);
    titleRowLayout->addWidget(m_resetButton, 0, Qt::AlignVCenter);
    titleRowLayout->addWidget(m_closeButton, 0, Qt::AlignTop);

    headerLayout->addLayout(titleRowLayout);

    m_body = new QWidget(m_panel);
    makeWidgetTransparent(m_body);
    m_body->setObjectName(QStringLiteral("brush_editor_parameter_overlay_body"));
    auto* bodyLayout = new QHBoxLayout(m_body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(ThemeManager::instance().scaled(22));

    m_sourcesColumn = new QWidget(m_body);
    m_sourcesColumn->setObjectName(QStringLiteral("brush_editor_parameter_overlay_sources_column"));
    m_sourcesColumn->setAttribute(Qt::WA_StyledBackground, true);
    m_sourcesColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    auto* sourcesLayout = new QVBoxLayout(m_sourcesColumn);
    sourcesLayout->setContentsMargins(ThemeManager::instance().scaled(6),
        ThemeManager::instance().scaled(7), ThemeManager::instance().scaled(6),
        ThemeManager::instance().scaled(7));
    sourcesLayout->setSpacing(ThemeManager::instance().scaled(3));

    m_sourcesLabel = new QLabel(m_sourcesColumn);
    m_sourcesLabel->setObjectName(QStringLiteral("brush_editor_parameter_overlay_sources_label"));

    m_tabletPressureButton = new BrushEditorOverlaySourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Pressure"), m_sourcesColumn);
    m_timeButton = new BrushEditorOverlaySourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Time"), m_sourcesColumn);
    m_randomButton = new BrushEditorOverlaySourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Random"), m_sourcesColumn);
    m_directionButton = new BrushEditorOverlaySourceButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Direction"), m_sourcesColumn);
    static_cast<BrushEditorOverlaySourceButton*>(m_tabletPressureButton)
        ->setSourceIcon(BrushEditorOverlaySourceButton::SourceIcon::Pressure);
    static_cast<BrushEditorOverlaySourceButton*>(m_timeButton)
        ->setSourceIcon(BrushEditorOverlaySourceButton::SourceIcon::Time);
    static_cast<BrushEditorOverlaySourceButton*>(m_randomButton)
        ->setSourceIcon(BrushEditorOverlaySourceButton::SourceIcon::Random);
    static_cast<BrushEditorOverlaySourceButton*>(m_directionButton)
        ->setSourceIcon(BrushEditorOverlaySourceButton::SourceIcon::Direction);

    sourcesLayout->addWidget(m_sourcesLabel);
    sourcesLayout->addSpacing(ThemeManager::instance().scaled(2));
    sourcesLayout->addWidget(m_tabletPressureButton);
    sourcesLayout->addWidget(m_timeButton);
    sourcesLayout->addWidget(m_randomButton);
    sourcesLayout->addWidget(m_directionButton);
    sourcesLayout->addStretch();

    m_editorStack = new widgets::AnimatedStackedWidget(m_body);
    m_editorStack->setObjectName(QStringLiteral("brush_editor_parameter_overlay_editor_stack"));
    makeWidgetTransparent(m_editorStack);
    m_editorStack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_editorStack->setSlideOrientation(widgets::AnimatedStackedWidget::SlideOrientation::Vertical);
    m_editorStack->setAnimationDuration(220);
    m_editorStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_pressurePage = new QWidget(m_editorStack);
    m_pressurePage->setObjectName(QStringLiteral("brush_editor_parameter_overlay_pressure_page"));
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
    m_pressureLabel->setObjectName(QStringLiteral("brush_editor_parameter_overlay_pressure_label"));
    m_pressureToggle = new widgets::ToggleSwitch(pressureRow);

    pressureRowLayout->addWidget(m_pressureLabel);
    pressureRowLayout->addStretch();
    pressureRowLayout->addWidget(m_pressureToggle);

    auto* modeRow = new QWidget(m_pressurePage);
    makeWidgetTransparent(modeRow);
    auto* modeRowLayout = new QHBoxLayout(modeRow);
    modeRowLayout->setContentsMargins(0, 0, 0, 0);
    modeRowLayout->setSpacing(ThemeManager::instance().scaled(10));

    m_modeLabel = new QLabel(modeRow);
    m_modeLabel->setObjectName(QStringLiteral("brush_editor_parameter_overlay_mode_label"));
    m_modeSelector = new widgets::SegmentedOptionSelector(modeRow);
    m_modeSelector->setDisplayMode(widgets::SegmentedOptionSelector::DisplayMode::TextOnly);

    modeRowLayout->addWidget(m_modeLabel);
    modeRowLayout->addWidget(m_modeSelector, 1);

    m_curveEditor = new widgets::CurveEditorWidget(m_pressurePage);
    m_curveEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    editorLayout->addWidget(pressureRow);
    editorLayout->addWidget(modeRow);
    editorLayout->addWidget(m_curveEditor, 0, Qt::AlignTop);
    editorLayout->addStretch(1);

    m_timePage = new QWidget(m_editorStack);
    m_timePage->setObjectName(QStringLiteral("brush_editor_parameter_overlay_time_page"));
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
    m_timeEnabledLabel->setObjectName(
        QStringLiteral("brush_editor_parameter_overlay_time_enabled_label"));
    m_timeToggle = new widgets::ToggleSwitch(timeToggleRow);
    timeToggleLayout->addWidget(m_timeEnabledLabel);
    timeToggleLayout->addStretch();
    timeToggleLayout->addWidget(m_timeToggle);

    auto* timeModeRow = new QWidget(m_timePage);
    makeWidgetTransparent(timeModeRow);
    auto* timeModeLayout = new QHBoxLayout(timeModeRow);
    timeModeLayout->setContentsMargins(0, 0, 0, 0);
    timeModeLayout->setSpacing(ThemeManager::instance().scaled(10));
    m_timeModeLabel = new QLabel(timeModeRow);
    m_timeModeLabel->setObjectName(
        QStringLiteral("brush_editor_parameter_overlay_time_mode_label"));
    m_timeModeSelector = new widgets::SegmentedOptionSelector(timeModeRow);
    m_timeModeSelector->setDisplayMode(widgets::SegmentedOptionSelector::DisplayMode::TextOnly);
    timeModeLayout->addWidget(m_timeModeLabel);
    timeModeLayout->addWidget(m_timeModeSelector, 1);

    auto* timeDurationRow = new QWidget(m_timePage);
    makeWidgetTransparent(timeDurationRow);
    auto* timeDurationLayout = new QHBoxLayout(timeDurationRow);
    timeDurationLayout->setContentsMargins(0, 0, 0, 0);
    timeDurationLayout->setSpacing(ThemeManager::instance().scaled(12));
    m_timeDurationLabel = new QLabel(timeDurationRow);
    m_timeDurationLabel->setObjectName(
        QStringLiteral("brush_editor_parameter_overlay_time_duration_label"));
    m_timeDurationSlider = new widgets::ProgressHandleSlider(timeDurationRow);
    m_timeDurationSlider->setRange(
        sliderValueFromTimeDuration(0.1f), sliderValueFromTimeDuration(10.0f));
    m_timeDurationSlider->setOrientation(Qt::Horizontal);
    m_timeDurationSlider->setShowValueText(true);
    m_timeDurationSlider->setValueDisplayMode(
        widgets::ProgressHandleSlider::ValueDisplayMode::RawValue);
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
        QStringLiteral("brush_editor_parameter_overlay_time_end_action_label"));
    m_timeEndActionCombo = new widgets::AnimatedComboBox(timeEndActionRow);
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

    m_timeCurveEditor = new widgets::CurveEditorWidget(m_timePage);
    m_timeCurveEditor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    timeLayout->addWidget(timeToggleRow);
    timeLayout->addWidget(timeModeRow);
    timeLayout->addWidget(m_timeCurveEditor, 0, Qt::AlignTop);
    timeLayout->addWidget(timeDurationRow);
    timeLayout->addWidget(timeEndActionRow);
    timeLayout->addStretch(1);

    m_randomPage = new QWidget(m_editorStack);
    m_randomPage->setObjectName(QStringLiteral("brush_editor_parameter_overlay_random_page"));
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
        QStringLiteral("brush_editor_parameter_overlay_random_enabled_label"));
    m_randomToggle = new widgets::ToggleSwitch(randomToggleRow);
    randomToggleLayout->addWidget(m_randomEnabledLabel);
    randomToggleLayout->addStretch();
    randomToggleLayout->addWidget(m_randomToggle);

    auto* randomAmountRow = new QWidget(m_randomPage);
    makeWidgetTransparent(randomAmountRow);
    auto* randomAmountLayout = new QHBoxLayout(randomAmountRow);
    randomAmountLayout->setContentsMargins(0, 0, 0, 0);
    randomAmountLayout->setSpacing(ThemeManager::instance().scaled(12));
    m_randomAmountLabel = new QLabel(randomAmountRow);
    m_randomAmountLabel->setObjectName(
        QStringLiteral("brush_editor_parameter_overlay_random_amount_label"));
    m_randomAmountSlider = new widgets::ProgressHandleSlider(randomAmountRow);
    m_randomAmountSlider->setOrientation(Qt::Horizontal);
    m_randomAmountSlider->setShowValueText(true);
    m_randomAmountSlider->setValueDisplayMode(
        widgets::ProgressHandleSlider::ValueDisplayMode::RawValue);
    m_randomAmountSlider->setValueTextPrefix(QString());
    m_randomAmountSlider->setValueTextSuffix(QString());
    randomAmountLayout->addWidget(m_randomAmountLabel, 1);
    randomAmountLayout->addWidget(m_randomAmountSlider, 0);

    randomLayout->addWidget(randomToggleRow);
    randomLayout->addWidget(randomAmountRow);
    randomLayout->addStretch(1);

    m_directionPage = new QWidget(m_editorStack);
    m_directionPage->setObjectName(QStringLiteral("brush_editor_parameter_overlay_direction_page"));
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
        QStringLiteral("brush_editor_parameter_overlay_direction_enabled_label"));
    m_directionToggle = new widgets::ToggleSwitch(directionToggleRow);
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
        QStringLiteral("brush_editor_parameter_overlay_direction_mode_label"));
    m_directionModeSelector = new widgets::SegmentedOptionSelector(directionModeRow);
    m_directionModeSelector->setDisplayMode(
        widgets::SegmentedOptionSelector::DisplayMode::TextOnly);
    directionModeLayout->addWidget(m_directionModeLabel);
    directionModeLayout->addWidget(m_directionModeSelector, 1);

    m_directionCurveEditor = new widgets::CurveEditorWidget(m_directionPage);
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

    panelLayout->addWidget(header);
    panelLayout->addWidget(m_body, 1);

    m_dimAnimation = new QVariantAnimation(this);
    m_dimAnimation->setDuration(180);
    connect(m_dimAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_dimProgress = value.toReal();
        update();
    });
    connect(m_dimAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_isHiding) {
            m_isHiding = false;
            QWidget::hide();
        } else if (m_isShowing) {
            m_isShowing = false;
        }
    });

    m_panelAnimation = new QVariantAnimation(this);
    m_panelAnimation->setDuration(180);
    connect(
        m_panelAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_panelProgress = value.toReal();
            updatePanelPresentation();
        });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateStyles();
        updatePanelGeometry();
    });
    connect(m_tabletPressureButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::TabletPressure); });
    connect(m_timeButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::Time); });
    connect(m_randomButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::RandomValue); });
    connect(m_directionButton, &QPushButton::clicked, this,
        [this]() { setActiveSource(BrushInputSourceKey::StrokeDirection); });
    connect(m_pressureToggle, &widgets::ToggleSwitch::toggled, this, [this](bool checked) {
        auto binding = currentBinding();
        binding.enabled = checked;
        storeCurrentBinding(binding);
        updateTexts();
        emit editingFinished();
    });
    connect(m_timeToggle, &widgets::ToggleSwitch::toggled, this, [this](bool checked) {
        auto binding = currentBinding();
        binding.enabled = checked;
        storeCurrentBinding(binding);
        updateTexts();
        emit editingFinished();
    });
    connect(m_randomToggle, &widgets::ToggleSwitch::toggled, this, [this](bool checked) {
        auto binding = currentBinding();
        binding.enabled = checked;
        storeCurrentBinding(binding);
        updateTexts();
        emit editingFinished();
    });
    connect(m_directionToggle, &widgets::ToggleSwitch::toggled, this, [this](bool checked) {
        auto binding = currentBinding();
        binding.enabled = checked;
        storeCurrentBinding(binding);
        updateTexts();
        emit editingFinished();
    });
    connect(m_modeSelector, &widgets::SegmentedOptionSelector::selectionChanged, this, [this]() {
        if (m_syncingModeSelector) {
            return;
        }
        auto binding = currentBinding();
        const int index = m_modeSelector ? m_modeSelector->currentIndex() : 0;
        const auto sourceDef = m_targetDef.sourceDef(m_activeSource);
        if (!sourceDef.has_value() || index < 0 || index >= sourceDef->allowedBlendModes.size()) {
            return;
        }
        binding.mode = sourceDef->allowedBlendModes[index];
        binding.curve.normalize(binding.setting, binding.mode);
        storeCurrentBinding(binding);
        syncEditorFromCurrentBinding();
    });
    connect(
        m_timeModeSelector, &widgets::SegmentedOptionSelector::selectionChanged, this, [this]() {
            if (m_syncingModeSelector) {
                return;
            }
            auto binding = currentBinding();
            const int index = m_timeModeSelector ? m_timeModeSelector->currentIndex() : 0;
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
    connect(m_directionModeSelector, &widgets::SegmentedOptionSelector::selectionChanged, this,
        [this]() {
            if (m_syncingModeSelector) {
                return;
            }
            auto binding = currentBinding();
            const int index = m_directionModeSelector ? m_directionModeSelector->currentIndex() : 0;
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
    connect(m_curveEditor, &widgets::CurveEditorWidget::pointsChanged, this, [this]() {
        storeCurrentBinding(bindingFromCurveEditor(currentBinding(), m_curveEditor->points()));
    });
    connect(m_curveEditor, &widgets::CurveEditorWidget::editingFinished, this,
        &BrushEditorParameterOverlay::editingFinished);
    connect(m_timeCurveEditor, &widgets::CurveEditorWidget::pointsChanged, this, [this]() {
        storeCurrentBinding(bindingFromCurveEditor(currentBinding(), m_timeCurveEditor->points()));
    });
    connect(m_timeCurveEditor, &widgets::CurveEditorWidget::editingFinished, this,
        &BrushEditorParameterOverlay::editingFinished);
    connect(m_directionCurveEditor, &widgets::CurveEditorWidget::pointsChanged, this, [this]() {
        storeCurrentBinding(
            bindingFromCurveEditor(currentBinding(), m_directionCurveEditor->points()));
    });
    connect(m_directionCurveEditor, &widgets::CurveEditorWidget::editingFinished, this,
        &BrushEditorParameterOverlay::editingFinished);
    connect(m_timeDurationSlider, &widgets::ProgressHandleSlider::valueChanged, this,
        [this](int sliderValue) {
            auto binding = currentBinding();
            binding.durationSec = timeDurationFromSliderValue(sliderValue);
            storeCurrentBinding(binding);
            syncEditorFromCurrentBinding();
        });
    connect(m_timeDurationSlider, &widgets::ProgressHandleSlider::sliderReleased, this,
        &BrushEditorParameterOverlay::editingFinished);
    connect(m_timeEndActionCombo, &widgets::AnimatedComboBox::currentIndexChanged, this, [this]() {
        auto binding = currentBinding();
        const int rawAction = m_timeEndActionCombo ? m_timeEndActionCombo->currentData().toInt()
                                                   : static_cast<int>(binding.endAction);
        binding.endAction = static_cast<BrushTimeEndAction>(rawAction);
        storeCurrentBinding(binding);
        syncEditorFromCurrentBinding();
        emit editingFinished();
    });
    connect(m_randomAmountSlider, &widgets::ProgressHandleSlider::valueChanged, this,
        [this](int sliderValue) {
            auto binding = currentBinding();
            ruwa::core::brushes::setBrushDynamicsRandomAmount(
                binding, randomAmountFromSliderValue(sliderValue));
            storeCurrentBinding(binding);
            syncEditorFromCurrentBinding();
        });
    connect(m_randomAmountSlider, &widgets::ProgressHandleSlider::sliderReleased, this,
        &BrushEditorParameterOverlay::editingFinished);

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
        resize(parentWidget()->size());
    }
    if (QWidget* hostWindow = window(); hostWindow && hostWindow != parentWidget()) {
        hostWindow->installEventFilter(this);
    }

    updateStyles();
    updatePanelGeometry();
    QWidget::hide();
}

BrushEditorParameterOverlay::~BrushEditorParameterOverlay()
{
    setShortcutBlocking(false);
}

void BrushEditorParameterOverlay::showOverlay(const QString& settingKey,
    const QString& settingLabel, const BrushDynamicsSlot& slot,
    const BrushDynamicTargetDef& targetDef)
{
    showOverlay(settingKey, settingLabel, slot, targetDef, CurveAxesConfig {});
}

void BrushEditorParameterOverlay::showOverlay(const QString& settingKey,
    const QString& settingLabel, const BrushDynamicsSlot& slot,
    const BrushDynamicTargetDef& targetDef, CurveAxesConfig curveAxesConfig)
{
    const bool wasActive = isActive();
    m_settingKey = settingKey;
    m_settingLabel = settingLabel;
    m_slot = slot;
    m_targetDef = targetDef;
    m_curveAxesConfig = curveAxesConfig;

    setActiveSource(m_activeSource);
    syncEditorFromCurrentBinding();

    if (parentWidget()) {
        resize(parentWidget()->size());
    }

    updatePanelGeometry();
    QWidget::show();
    raise();
    setFocus();

    m_isShowing = true;
    m_isHiding = false;
    m_panelAnimation->stop();
    m_dimAnimation->stop();
    m_panelAnimation->setStartValue(wasActive ? m_panelProgress : 0.0);
    m_panelAnimation->setEndValue(1.0);
    m_panelAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_panelAnimation->start();
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(1.0);
    m_dimAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dimAnimation->start();
}

void BrushEditorParameterOverlay::hideOverlay()
{
    if (!isVisible() || m_isHiding) {
        return;
    }

    emit editingFinished();

    m_isShowing = false;
    m_isHiding = true;
    m_panelAnimation->stop();
    m_dimAnimation->stop();
    m_panelAnimation->setStartValue(m_panelProgress);
    m_panelAnimation->setEndValue(0.0);
    m_panelAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_panelAnimation->start();
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(0.0);
    m_dimAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_dimAnimation->start();
}

bool BrushEditorParameterOverlay::isActive() const
{
    return isVisible() && !m_isHiding;
}

QString BrushEditorParameterOverlay::settingKey() const
{
    return m_settingKey;
}

BrushEditorParameterOverlay::BrushInputSourceKey BrushEditorParameterOverlay::activeSource() const
{
    return m_activeSource;
}

void BrushEditorParameterOverlay::setCurveAxesConfig(CurveAxesConfig curveAxesConfig)
{
    m_curveAxesConfig = curveAxesConfig;
    if (m_curveEditor) {
        syncEditorFromCurrentBinding();
    }
}

void BrushEditorParameterOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    setShortcutBlocking(window() && window()->isActiveWindow());
}

void BrushEditorParameterOverlay::hideEvent(QHideEvent* event)
{
    setShortcutBlocking(false);
    QWidget::hideEvent(event);
}

void BrushEditorParameterOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_dimProgress <= 0.001) {
        return;
    }

    QPainter painter(this);
    const int alpha = static_cast<int>(0.52 * 255 * m_dimProgress);
    painter.fillRect(rect(), QColor(0, 0, 0, alpha));
}

void BrushEditorParameterOverlay::mousePressEvent(QMouseEvent* event)
{
    if (m_panel && !m_panel->geometry().contains(event->pos())) {
        hideOverlay();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void BrushEditorParameterOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideOverlay();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void BrushEditorParameterOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePanelGeometry();
}

bool BrushEditorParameterOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        auto* resizeEvent = static_cast<QResizeEvent*>(event);
        resize(resizeEvent->size());
        updatePanelGeometry();
    } else if (watched == window()) {
        if (event->type() == QEvent::WindowActivate) {
            setShortcutBlocking(isVisible());
        } else if (event->type() == QEvent::WindowDeactivate) {
            setShortcutBlocking(false);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BrushEditorParameterOverlay::setShortcutBlocking(bool blocked)
{
    if (blocked == m_shortcutsBlocked) {
        return;
    }

    m_shortcutsBlocked = blocked;
    if (blocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
    } else {
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

bool BrushEditorParameterOverlay::isSourceAvailable(BrushInputSourceKey source) const
{
    const auto sourceDef = m_targetDef.sourceDef(source);
    if (!sourceDef.has_value()) {
        return false;
    }
    return sourceDef->available;
}

bool BrushEditorParameterOverlay::isShapeAngleStrokeDirection() const
{
    return m_activeSource == BrushInputSourceKey::StrokeDirection
        && m_slot.setting == BrushDynamicsSettingKey::ShapeAngle;
}

BrushEditorParameterOverlay::BrushInputSourceKey BrushEditorParameterOverlay::fallbackSource() const
{
    for (const auto& sourceDef : m_targetDef.sources) {
        if (sourceDef.available
            && ruwa::core::brushes::supportsBrushInputSource(sourceDef.source)) {
            return sourceDef.source;
        }
    }
    return BrushInputSourceKey::TabletPressure;
}

int BrushEditorParameterOverlay::sourcePageIndex(BrushInputSourceKey source) const
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

void BrushEditorParameterOverlay::setActiveSource(BrushInputSourceKey source)
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

BrushEditorParameterOverlay::BrushDynamicsBinding
BrushEditorParameterOverlay::currentBinding() const
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

BrushEditorParameterOverlay::BrushDynamicsBinding
BrushEditorParameterOverlay::defaultBindingForSource(BrushInputSourceKey source) const
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
    binding.mode = ruwa::core::brushes::defaultBrushDynamicsBlendMode(m_slot.setting);
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

void BrushEditorParameterOverlay::resetActiveSourceBinding()
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

void BrushEditorParameterOverlay::storeCurrentBinding(
    const BrushDynamicsBinding& binding, bool emitSlotChanged)
{
    if (!ruwa::core::brushes::supportsBrushInputSource(binding.source)
        || !ruwa::core::brushes::supportsBrushDynamicsSetting(m_slot.setting)) {
        return;
    }

    auto normalized = binding;
    normalized.setting = m_slot.setting;
    normalized.source = binding.source;
    if (normalized.source == BrushInputSourceKey::RandomValue) {
        normalized.mode = BrushDynamicsBlendMode::Add;
        ruwa::core::brushes::setBrushDynamicsRandomAmount(
            normalized, ruwa::core::brushes::brushDynamicsRandomAmount(normalized));
    } else if (normalized.source == BrushInputSourceKey::StrokeDirection
        && normalized.setting == BrushDynamicsSettingKey::ShapeAngle) {
        normalized.mode = BrushDynamicsBlendMode::Override;
    } else {
        normalized.mode = ruwa::core::brushes::normalizeBrushDynamicsBlendMode(
            normalized.setting, normalized.mode);
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

    if (emitSlotChanged) {
        emit slotChanged(m_settingKey, m_slot);
    }
}

void BrushEditorParameterOverlay::syncEditorFromCurrentBinding()
{
    const auto bindingForSource = [this](BrushInputSourceKey source) {
        auto binding = m_slot.binding(source);
        binding.setting = m_slot.setting;
        binding.source = source;
        return displayBinding(binding);
    };
    const auto syncCurveEditor = [this](widgets::CurveEditorWidget* editor,
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
        editor->setPoints(curveEditorPointsFromBinding(binding));
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
        m_directionCurveEditor->setVisible(!isShapeAngleStrokeDirection());
    }
    if (m_directionModeLabel) {
        m_directionModeLabel->setVisible(!isShapeAngleStrokeDirection());
    }

    if (auto* toggle = activeToggle()) {
        const QSignalBlocker blocker(toggle);
        toggle->setChecked(activeBinding.enabled, widgets::ToggleSwitch::TransitionMode::Instant);
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
    if (m_randomAmountSlider) {
        const QSignalBlocker blocker(m_randomAmountSlider);
        const float amount = ruwa::core::brushes::brushDynamicsRandomAmount(randomBinding);
        m_randomAmountSlider->setRange(0,
            sliderValueFromRandomAmount(
                ruwa::core::brushes::brushDynamicsRandomAmountMax(m_slot.setting)));
        m_randomAmountSlider->setValue(sliderValueFromRandomAmount(amount));
        m_randomAmountSlider->setCustomDisplayText(formatRandomAmount(amount));
    }

    updateModeSelector();
}

void BrushEditorParameterOverlay::updateModeSelector()
{
    auto* modeSelector = activeModeSelector();
    QLabel* modeLabel = m_modeLabel;
    if (m_activeSource == BrushInputSourceKey::Time) {
        modeLabel = m_timeModeLabel;
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
    QVector<widgets::SegmentedOptionSelector::Option> options;
    QVector<BrushDynamicsBlendMode> optionModes;
    int currentIndex = 0;
    if (sourceDef.has_value()) {
        options.reserve(sourceDef->allowedBlendModes.size());
        optionModes.reserve(sourceDef->allowedBlendModes.size());
        const auto binding = currentBinding();
        for (int i = 0; i < sourceDef->allowedBlendModes.size(); ++i) {
            const auto mode = sourceDef->allowedBlendModes[i];
            optionModes.append(mode);
            widgets::SegmentedOptionSelector::Option option;
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

void BrushEditorParameterOverlay::updateTexts()
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
    if (m_resetButton) {
        m_resetButton->setText(QCoreApplication::translate("BrushEditorParameterOverlay", "Reset"));
        m_resetButton->syncSizeToText();
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
    if (m_randomAmountLabel) {
        m_randomAmountLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Amount"));
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

void BrushEditorParameterOverlay::updateSourceButtons()
{
    if (auto* tabletPressureButton
        = static_cast<BrushEditorOverlaySourceButton*>(m_tabletPressureButton)) {
        const bool enabled = m_targetDef.sourceDef(BrushInputSourceKey::TabletPressure).has_value()
            && isSourceAvailable(BrushInputSourceKey::TabletPressure);
        tabletPressureButton->setVisible(true);
        tabletPressureButton->setEnabled(true);
        tabletPressureButton->setSourceAvailable(enabled);
        tabletPressureButton->setActive(
            enabled && m_activeSource == BrushInputSourceKey::TabletPressure);
    }
    if (auto* timeButton = static_cast<BrushEditorOverlaySourceButton*>(m_timeButton)) {
        const bool enabled = m_targetDef.sourceDef(BrushInputSourceKey::Time).has_value()
            && isSourceAvailable(BrushInputSourceKey::Time);
        timeButton->setVisible(true);
        timeButton->setEnabled(true);
        timeButton->setSourceAvailable(enabled);
        timeButton->setActive(enabled && m_activeSource == BrushInputSourceKey::Time);
    }
    if (auto* randomButton = static_cast<BrushEditorOverlaySourceButton*>(m_randomButton)) {
        const bool enabled = m_targetDef.sourceDef(BrushInputSourceKey::RandomValue).has_value()
            && isSourceAvailable(BrushInputSourceKey::RandomValue);
        randomButton->setVisible(true);
        randomButton->setEnabled(true);
        randomButton->setSourceAvailable(enabled);
        randomButton->setActive(enabled && m_activeSource == BrushInputSourceKey::RandomValue);
    }
    if (auto* directionButton = static_cast<BrushEditorOverlaySourceButton*>(m_directionButton)) {
        const bool enabled = m_targetDef.sourceDef(BrushInputSourceKey::StrokeDirection).has_value()
            && isSourceAvailable(BrushInputSourceKey::StrokeDirection);
        directionButton->setVisible(true);
        directionButton->setEnabled(true);
        directionButton->setSourceAvailable(enabled);
        directionButton->setActive(
            enabled && m_activeSource == BrushInputSourceKey::StrokeDirection);
    }
    if (m_sourcesColumn) {
        m_sourcesColumn->setVisible(true);
    }
    if (m_editorStack) {
        m_editorStack->setCurrentIndex(sourcePageIndex(m_activeSource));
    }
}

void BrushEditorParameterOverlay::updatePanelGeometry()
{
    if (!m_panel) {
        return;
    }

    const int horizontalMargin = qMax(ThemeManager::instance().scaled(32), qRound(width() * 0.075));
    const int verticalMargin = qMax(ThemeManager::instance().scaled(28), qRound(height() * 0.075));
    const QSize preferredSize = m_panel->sizeHint().expandedTo(
        QSize(ThemeManager::instance().scaled(560), ThemeManager::instance().scaled(460)));
    const int maxWidth = qMax(ThemeManager::instance().scaled(320), width() - horizontalMargin * 2);
    const int maxHeight = qMax(ThemeManager::instance().scaled(240), height() - verticalMargin * 2);
    const QSize boundedSize(
        qMin(preferredSize.width(), maxWidth), qMin(preferredSize.height(), maxHeight));

    QRect targetRect(QPoint(0, 0), boundedSize);
    targetRect.moveCenter(rect().center());
    targetRect.moveLeft(qMax(horizontalMargin, targetRect.left()));
    targetRect.moveTop(qMax(verticalMargin, targetRect.top()));
    if (targetRect.right() > width() - horizontalMargin) {
        targetRect.moveRight(width() - horizontalMargin);
    }
    if (targetRect.bottom() > height() - verticalMargin) {
        targetRect.moveBottom(height() - verticalMargin);
    }

    m_targetPanelRect = targetRect;
    updatePanelPresentation();
}

void BrushEditorParameterOverlay::updatePanelPresentation()
{
    if (!m_panel) {
        return;
    }

    const int slideOffset = ThemeManager::instance().scaled(18);
    QRect panelRect = m_targetPanelRect.isNull() ? m_panel->geometry() : m_targetPanelRect;
    panelRect.translate(0, qRound((1.0 - m_panelProgress) * slideOffset));
    m_panel->setGeometry(panelRect);
    if (m_panelOpacityEffect) {
        m_panelOpacityEffect->setOpacity(m_panelProgress);
    }
}

void BrushEditorParameterOverlay::updateStyles()
{
    auto& theme = ThemeManager::instance();
    const auto& colors = WidgetStyleManager::instance().colors();
    const int timeControlWidth = theme.scaled(220);

    QFont titleFont = font();
    titleFont.setPixelSize(theme.scaled(13));
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    QFont sectionFont = font();
    sectionFont.setPixelSize(theme.scaled(10));
    sectionFont.setBold(true);
    QFont sourcesHeaderFont = sectionFont;
    sourcesHeaderFont.setPixelSize(theme.scaled(8));
    m_sourcesLabel->setFont(sourcesHeaderFont);
    m_pressureLabel->setFont(sectionFont);
    if (m_modeLabel) {
        m_modeLabel->setFont(sectionFont);
    }
    if (m_timeEnabledLabel) {
        m_timeEnabledLabel->setFont(sectionFont);
    }
    if (m_timeModeLabel) {
        m_timeModeLabel->setFont(sectionFont);
    }
    if (m_timeDurationLabel) {
        m_timeDurationLabel->setFont(sectionFont);
    }
    if (m_timeEndActionLabel) {
        m_timeEndActionLabel->setFont(sectionFont);
    }
    if (m_randomEnabledLabel) {
        m_randomEnabledLabel->setFont(sectionFont);
    }
    if (m_randomAmountLabel) {
        m_randomAmountLabel->setFont(sectionFont);
    }
    if (m_directionEnabledLabel) {
        m_directionEnabledLabel->setFont(sectionFont);
    }
    if (m_directionModeLabel) {
        m_directionModeLabel->setFont(sectionFont);
    }

    const QString panelStyle
        = QStringLiteral("QWidget#brush_editor_parameter_overlay_panel { background: %1; border: "
                         "1px solid %2; border-radius: %3px; }")
              .arg(colors.surfaceElevated().name(QColor::HexArgb),
                  colors.borderSubtleHover().name(QColor::HexArgb),
                  QString::number(theme.scaled(18)));
    m_panel->setStyleSheet(panelStyle);
    const QString sourcesPanelStyle
        = QStringLiteral("QWidget#brush_editor_parameter_overlay_sources_column { background: %1; "
                         "border: 1px solid %2; border-radius: %3px; }")
              .arg(colors.surfaceAlt.name(QColor::HexArgb),
                  colors.borderSubtle().name(QColor::HexArgb), QString::number(theme.scaled(10)));
    m_sourcesColumn->setStyleSheet(sourcesPanelStyle);
    m_titleLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_sourcesLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));
    m_sourcesLabel->setContentsMargins(theme.scaled(4), 0, 0, 0);
    m_pressureLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.text.name(QColor::HexArgb)));
    if (m_modeLabel) {
        m_modeLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }
    if (m_timeEnabledLabel) {
        m_timeEnabledLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }
    if (m_timeModeLabel) {
        m_timeModeLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }
    if (m_timeDurationLabel) {
        m_timeDurationLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
        m_timeDurationLabel->setMinimumWidth(theme.scaled(92));
    }
    if (m_timeEndActionLabel) {
        m_timeEndActionLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
        m_timeEndActionLabel->setMinimumWidth(theme.scaled(92));
    }
    if (m_randomEnabledLabel) {
        m_randomEnabledLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }
    if (m_randomAmountLabel) {
        m_randomAmountLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
        m_randomAmountLabel->setMinimumWidth(theme.scaled(92));
    }
    if (m_directionEnabledLabel) {
        m_directionEnabledLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }
    if (m_directionModeLabel) {
        m_directionModeLabel->setStyleSheet(
            QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }
    if (auto* layout = qobject_cast<QVBoxLayout*>(m_panel->layout())) {
        layout->setContentsMargins(
            theme.scaled(12), theme.scaled(12), theme.scaled(12), theme.scaled(12));
        layout->setSpacing(theme.scaled(12));
    }
    if (auto* headerLayout
        = qobject_cast<QVBoxLayout*>(m_panel->layout()->itemAt(0)->widget()->layout())) {
        headerLayout->setSpacing(0);
    }
    if (auto* titleRowLayout = qobject_cast<QHBoxLayout*>(
            m_panel->layout()->itemAt(0)->widget()->layout()->itemAt(0)->layout())) {
        titleRowLayout->setContentsMargins(theme.scaled(6), 0, 0, 0);
        titleRowLayout->setSpacing(theme.scaled(8));
    }
    if (auto* bodyLayout = qobject_cast<QHBoxLayout*>(m_body->layout())) {
        bodyLayout->setSpacing(theme.scaled(16));
    }
    if (auto* sourcesLayout = qobject_cast<QVBoxLayout*>(m_sourcesColumn->layout())) {
        sourcesLayout->setContentsMargins(
            theme.scaled(6), theme.scaled(7), theme.scaled(6), theme.scaled(7));
        sourcesLayout->setSpacing(theme.scaled(3));
    }
    if (auto* editorLayout = qobject_cast<QVBoxLayout*>(m_pressurePage->layout())) {
        editorLayout->setSpacing(theme.scaled(12));
    }
    if (auto* timeLayout = qobject_cast<QVBoxLayout*>(m_timePage->layout())) {
        timeLayout->setSpacing(theme.scaled(12));
    }
    if (auto* randomLayout = qobject_cast<QVBoxLayout*>(m_randomPage->layout())) {
        randomLayout->setSpacing(theme.scaled(12));
    }
    if (auto* directionLayout = qobject_cast<QVBoxLayout*>(m_directionPage->layout())) {
        directionLayout->setSpacing(theme.scaled(12));
    }
    m_sourcesColumn->setFixedWidth(theme.scaled(178));
    m_sourcesColumn->setMinimumHeight(theme.scaled(336));
    if (m_editorStack) {
        m_editorStack->setMinimumHeight(theme.scaled(330));
    }
    if (m_curveEditor) {
        m_curveEditor->setMinimumHeight(theme.scaled(250));
        m_curveEditor->setMaximumHeight(theme.scaled(250));
    }
    if (m_timeCurveEditor) {
        m_timeCurveEditor->setMinimumHeight(theme.scaled(250));
        m_timeCurveEditor->setMaximumHeight(theme.scaled(250));
    }
    if (m_directionCurveEditor) {
        m_directionCurveEditor->setMinimumHeight(theme.scaled(250));
        m_directionCurveEditor->setMaximumHeight(theme.scaled(250));
    }
    if (m_timeDurationSlider) {
        m_timeDurationSlider->setMinimumHeight(theme.scaled(22));
        m_timeDurationSlider->setMaximumHeight(theme.scaled(22));
        m_timeDurationSlider->setFixedWidth(timeControlWidth);
        m_timeDurationSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if (m_randomAmountSlider) {
        m_randomAmountSlider->setMinimumHeight(theme.scaled(22));
        m_randomAmountSlider->setMaximumHeight(theme.scaled(22));
        m_randomAmountSlider->setFixedWidth(timeControlWidth);
        m_randomAmountSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if (m_timeEndActionCombo) {
        m_timeEndActionCombo->setFixedHeight(theme.scaled(24));
        m_timeEndActionCombo->setFixedWidth(timeControlWidth);
        m_timeEndActionCombo->setPopupMinWidth(timeControlWidth);
        m_timeEndActionCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    if (m_pressureToggle) {
        m_pressureToggle->setFixedSize(theme.scaled(40), theme.scaled(20));
    }
    if (m_timeToggle) {
        m_timeToggle->setFixedSize(theme.scaled(40), theme.scaled(20));
    }
    if (m_randomToggle) {
        m_randomToggle->setFixedSize(theme.scaled(40), theme.scaled(20));
    }
    if (m_directionToggle) {
        m_directionToggle->setFixedSize(theme.scaled(40), theme.scaled(20));
    }
    if (m_tabletPressureButton) {
        m_tabletPressureButton->setMinimumHeight(theme.scaled(22));
        m_tabletPressureButton->setMaximumHeight(theme.scaled(22));
    }
    if (m_timeButton) {
        m_timeButton->setMinimumHeight(theme.scaled(22));
        m_timeButton->setMaximumHeight(theme.scaled(22));
    }
    if (m_randomButton) {
        m_randomButton->setMinimumHeight(theme.scaled(22));
        m_randomButton->setMaximumHeight(theme.scaled(22));
    }
    if (m_directionButton) {
        m_directionButton->setMinimumHeight(theme.scaled(22));
        m_directionButton->setMaximumHeight(theme.scaled(22));
    }
    if (auto* closeButton = static_cast<BrushEditorOverlayCloseButton*>(m_closeButton)) {
        closeButton->setFixedSize(theme.scaled(28), theme.scaled(28));
        closeButton->update();
    }
    if (m_resetButton) {
        m_resetButton->setIcon(theme.icons().getIcon(IconProvider::StandardIcon::UndoArrow));
        m_resetButton->setBannerBaseHeight(36);
        m_resetButton->setSizeScale(0.78);
        m_resetButton->setBaseMinimumWidth(0);
        m_resetButton->syncSizeToText();
    }
    m_panel->style()->unpolish(m_panel);
    m_panel->style()->polish(m_panel);
    m_sourcesColumn->style()->unpolish(m_sourcesColumn);
    m_sourcesColumn->style()->polish(m_sourcesColumn);
    updateSourceButtons();
}

} // namespace ruwa::ui::windows
