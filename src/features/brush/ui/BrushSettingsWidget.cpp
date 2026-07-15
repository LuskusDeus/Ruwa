// SPDX-License-Identifier: MPL-2.0

#include "BrushSettingsWidget.h"

#include "features/brush/engine/PixelBrushModule.h"
#include "features/layers/model/BlendModeUtils.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/ImageDropdownSelector.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/inputs/ToggleSwitch.h"

#include <QCoreApplication>
#include <algorithm>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;
using ruwa::core::brushes::PixelBrushModule;
using ruwa::core::brushes::SettingType;

namespace {

// Nonlinear (piecewise-linear) response for the Spacing slider, so the lower
// end of the range gets far more travel. Normalized slider position t in [0,1]
// maps to a spacing value (fraction of brush size) through these knots:
//   t [0.0 .. 0.1]  -> [0.5% .. 1%]   fine float control at the low end
//   t [0.1 .. 0.5]  -> [1%   .. 50%]
//   t [0.5 .. 1.0]  -> [50%  .. 500%]
struct SpacingKnot {
    float t;
    float v;
};
constexpr SpacingKnot kSpacingKnots[] = {
    { 0.0f, ruwa::core::brushes::kBrushSpacingMin }, // 0.5%
    { 0.1f, 0.01f }, // 1%
    { 0.5f, 0.5f }, // 50%
    { 1.0f, ruwa::core::brushes::kBrushSpacingMax }, // 500%
};
constexpr int kSpacingKnotCount = static_cast<int>(std::size(kSpacingKnots));

float spacingPosToValue(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    for (int i = 1; i < kSpacingKnotCount; ++i) {
        const SpacingKnot& a = kSpacingKnots[i - 1];
        const SpacingKnot& b = kSpacingKnots[i];
        if (t <= b.t) {
            const float span = b.t - a.t;
            const float f = span > 0.0f ? (t - a.t) / span : 0.0f;
            return a.v + f * (b.v - a.v);
        }
    }
    return kSpacingKnots[kSpacingKnotCount - 1].v;
}

float spacingValueToPos(float v)
{
    v = std::clamp(v, ruwa::core::brushes::kBrushSpacingMin, ruwa::core::brushes::kBrushSpacingMax);
    for (int i = 1; i < kSpacingKnotCount; ++i) {
        const SpacingKnot& a = kSpacingKnots[i - 1];
        const SpacingKnot& b = kSpacingKnots[i];
        if (v <= b.v) {
            const float span = b.v - a.v;
            const float f = span > 0.0f ? (v - a.v) / span : 0.0f;
            return a.t + f * (b.t - a.t);
        }
    }
    return 1.0f;
}

QPushButton* createStarButton(QWidget* parent, const ruwa::ui::core::ThemeManager& theme)
{
    auto* button = new QPushButton(QStringLiteral("\u2606"), parent);
    button->setObjectName(QStringLiteral("bsw_star"));
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(theme.scaled(16), theme.scaled(16));
    return button;
}

QPushButton* createDynamicsButton(QWidget* parent, const ruwa::ui::core::ThemeManager& theme)
{
    auto* button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("bsw_dynamics"));
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFixedSize(theme.scaled(18), theme.scaled(18));
    button->setToolTip(
        QCoreApplication::translate("BrushSettingsWidget", "Open parameter dynamics"));
    return button;
}

QWidget* createDynamicsPlaceholder(QWidget* parent, const ruwa::ui::core::ThemeManager& theme)
{
    auto* placeholder = new QLabel(parent);
    placeholder->setObjectName(QStringLiteral("bsw_dynamics_placeholder"));
    placeholder->setCursor(Qt::ForbiddenCursor);
    placeholder->setAttribute(Qt::WA_TranslucentBackground);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setFixedSize(theme.scaled(18), theme.scaled(18));
    placeholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    placeholder->setToolTip(
        QCoreApplication::translate("BrushSettingsWidget", "Dynamics are not available"));
    return placeholder;
}

template <typename Row> void connectStarButton(BrushSettingsWidget* owner, Row& row)
{
    QObject::connect(row.starButton, &QPushButton::toggled, owner,
        [owner, btn = row.starButton, key = row.key](bool checked) {
            btn->setText(checked ? QStringLiteral("\u2605") : QStringLiteral("\u2606"));
            if (!owner->property("_bsw_updating").toBool()) {
                emit owner->starToggled(key, checked);
            }
        });
}

template <typename Row> void connectDynamicsButton(BrushSettingsWidget* owner, const Row& row)
{
    QObject::connect(row.dynamicsButton, &QPushButton::clicked, owner,
        [owner, key = row.key, label = row.nameLabel]() {
            emit owner->dynamicsRequested(key, label ? label->text() : key);
        });
}

bool hasEffectiveDynamics(const BrushSettingsWidget::BrushSettingsData& data,
    const ruwa::core::brushes::BrushDynamicTargetDef& target)
{
    if (!target.isValid()) {
        return false;
    }

    if (data.dynamics.slotForSetting(target.setting).hasActiveBindings()) {
        return true;
    }

    switch (target.setting) {
    case ruwa::core::brushes::BrushDynamicsSettingKey::RadiusMultiplier:
        return data.sizePressureEnabled;
    case ruwa::core::brushes::BrushDynamicsSettingKey::OpacityMultiplier:
        return data.opacityPressureEnabled;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeFlow:
        return data.flowPressureMin < 0.999f || data.flowPressureMax < 0.999f;
    default:
        return false;
    }
}

QImage makeCircleDabPreviewImage(int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);

    const QRectF circleRect(size * 0.18, size * 0.18, size * 0.64, size * 0.64);
    painter.drawEllipse(circleRect);
    return image;
}

QImage dabPreviewImageForIndex(int index)
{
    if (index <= 0) {
        return makeCircleDabPreviewImage(72);
    }

    const QString path = QStringLiteral(":/brushes/%1").arg(index);
    const QImage image(path);
    return image.isNull() ? makeCircleDabPreviewImage(72) : image;
}

void populateBrushBlendModeCombo(AnimatedComboBox* combo)
{
    if (!combo) {
        return;
    }

    for (const auto& category : ruwa::core::layers::blendModeCategoryDefs()) {
        combo->addCategory(QCoreApplication::translate("LayersPanel", category.label));
        for (const auto mode : category.modes) {
            combo->addItem(ruwa::core::layers::blendModeDisplayName(mode, "ruwa::core::brushes"),
                static_cast<int>(mode));
        }
    }
}

} // namespace

int BrushSettingsWidget::ComboBoxRow::currentIndex() const
{
    if (imageSelector) {
        bool ok = false;
        const int value = imageSelector->currentData().toInt(&ok);
        return ok ? value : imageSelector->currentIndex();
    }
    if (combo) {
        bool ok = false;
        const int value = combo->currentData().toInt(&ok);
        return ok ? value : combo->currentIndex();
    }
    return -1;
}

void BrushSettingsWidget::ComboBoxRow::setCurrentIndex(int index)
{
    if (imageSelector) {
        const int itemIndex = imageSelector->findIndexByData(index);
        imageSelector->setCurrentIndex(itemIndex >= 0 ? itemIndex : index);
    } else if (combo) {
        const int itemIndex = combo->findIndexByData(index);
        combo->setCurrentIndex(itemIndex >= 0 ? itemIndex : index);
    }
}

QWidget* BrushSettingsWidget::ComboBoxRow::inputWidget() const
{
    if (imageSelector) {
        return imageSelector;
    }
    return combo;
}

void BrushSettingsWidget::ComboBoxRow::setInputEnabled(bool enabled)
{
    if (imageSelector) {
        imageSelector->setEnabled(enabled);
    }
    if (combo) {
        combo->setEnabled(enabled);
    }
}

BrushSettingsWidget::BrushSettingsWidget(
    const QVector<BrushSettingDef>& defs, QWidget* parent, bool starMode)
    : QWidget(parent)
    , m_starMode(starMode)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setProperty("_bsw_updating", false);

    m_layout = new QVBoxLayout(this);
    auto& theme = ThemeManager::instance();
    m_layout->setContentsMargins(
        theme.scaled(2), theme.scaled(2), theme.scaled(2), theme.scaled(2));
    m_layout->setSpacing(theme.scaled(8));

    buildRows(defs);
    applyStyles();

    // Visibility-gated: re-applies stylesheets. Deferred for hidden (background)
    // instances; flushed on activation via WorkspaceTab.
    ThemeManager::instance().registerThemeHandler(this, [this]() { onThemeChanged(); });
}

float BrushSettingsWidget::sliderPosToValue(const SliderRow& row, int pos) const
{
    if (row.nonlinear) {
        const int steps = qRound((row.max - row.min) / row.step);
        const float t = steps > 0 ? static_cast<float>(pos) / static_cast<float>(steps) : 0.0f;
        return spacingPosToValue(t);
    }
    return row.min + pos * row.step;
}

int BrushSettingsWidget::valueToSliderPos(const SliderRow& row, float value) const
{
    if (row.nonlinear) {
        const int steps = qRound((row.max - row.min) / row.step);
        return qRound(spacingValueToPos(value) * static_cast<float>(steps));
    }
    return qRound((value - row.min) / row.step);
}

void BrushSettingsWidget::setSettings(const QVariantMap& data)
{
    m_currentSettings = data;
    m_updating = true;
    setProperty("_bsw_updating", true);

    for (auto& row : m_sliderRows) {
        const float value = m_currentSettings.value(row.key, row.defaultValue).toFloat();
        const int sliderVal = valueToSliderPos(row, value);
        row.slider->setValue(sliderVal);

        const float display = value * static_cast<float>(row.displayScale);
        row.slider->setCustomDisplayText(QStringLiteral("%1%2")
                .arg(static_cast<double>(display), 0, 'f', row.displayDecimals)
                .arg(QLatin1StringView(row.suffix)));
    }

    for (auto& row : m_toggleRows) {
        row.toggle->setChecked(m_currentSettings.value(row.key, row.defaultValue).toBool(),
            ToggleSwitch::TransitionMode::Instant);
    }

    for (auto& row : m_comboRows) {
        if (!row.inputWidget() || row.options.isEmpty()) {
            continue;
        }
        row.setCurrentIndex(qBound(
            0, m_currentSettings.value(row.key, row.defaultValue).toInt(), row.options.size() - 1));
    }

    for (auto& row : m_segmentedRows) {
        if (!row.selector || row.options.isEmpty()) {
            continue;
        }
        row.selector->setCurrentIndex(
            qBound(0, m_currentSettings.value(row.key, row.defaultValue).toInt(),
                row.options.size() - 1),
            false);
    }

    m_updating = false;
    setProperty("_bsw_updating", false);
    updateDependentControls();
    updateDynamicsButtonStates(nullptr);
}

void BrushSettingsWidget::setSettings(const BrushSettingsData& data)
{
    setSettings(PixelBrushModule::settingsToVariantMap(data));
    updateDynamicsButtonStates(&data);
}

void BrushSettingsWidget::applyTo(QVariantMap& data) const
{
    for (const auto& row : m_sliderRows) {
        data.insert(row.key, sliderPosToValue(row, row.slider->value()));
    }

    for (const auto& row : m_toggleRows) {
        data.insert(row.key, row.toggle->isChecked());
    }

    for (const auto& row : m_comboRows) {
        if (row.inputWidget() && !row.options.isEmpty()) {
            data.insert(row.key, qBound(0, row.currentIndex(), row.options.size() - 1));
        }
    }

    for (const auto& row : m_segmentedRows) {
        if (row.selector && !row.options.isEmpty()) {
            data.insert(row.key, qBound(0, row.selector->currentIndex(), row.options.size() - 1));
        }
    }
}

void BrushSettingsWidget::applyTo(BrushSettingsData& data) const
{
    QVariantMap settings = PixelBrushModule::settingsToVariantMap(data);
    applyTo(settings);
    data = PixelBrushModule::settingsFromVariantMap(settings);
}

void BrushSettingsWidget::buildRows(const QVector<BrushSettingDef>& defs)
{
    auto& theme = ThemeManager::instance();

    for (const auto& def : defs) {
        if (def.type == SettingType::Separator) {
            if (!m_starMode) {
                continue;
            }

            auto* separator = new QWidget(this);
            separator->setObjectName(QStringLiteral("bsw_separator"));
            separator->setAttribute(Qt::WA_StyledBackground, true);
            separator->setFixedHeight(1);
            separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_layout->addWidget(separator);
            m_separatorRows.append(separator);
            continue;
        }

        if (def.type == SettingType::DynamicInfo) {
            if (!m_starMode) {
                continue;
            }

            DynamicInfoRow row;
            row.key = QLatin1String(def.key);
            row.dynamicTarget = def.dynamicTarget;

            auto* container = new QWidget(this);
            container->setAttribute(Qt::WA_TranslucentBackground);
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            hbox->setSpacing(8);

            row.starButton = createStarButton(container, theme);
            hbox->addWidget(row.starButton);
            connectStarButton(this, row);

            if (def.supportsDynamics()) {
                row.dynamicsButton = createDynamicsButton(container, theme);
                hbox->addWidget(row.dynamicsButton);
                connectDynamicsButton(this, row);
            } else {
                row.dynamicsPlaceholder = createDynamicsPlaceholder(container, theme);
                hbox->addWidget(row.dynamicsPlaceholder);
            }

            row.nameLabel = new QLabel(
                QCoreApplication::translate("ruwa::core::brushes", def.label), container);
            row.nameLabel->setObjectName(QStringLiteral("bsw_label"));
            row.infoLabel = new QLabel(QCoreApplication::translate("ruwa::core::brushes",
                                           def.description ? def.description : ""),
                container);
            row.infoLabel->setObjectName(QStringLiteral("bsw_dynamic_info"));
            row.infoLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

            hbox->addWidget(row.nameLabel);
            hbox->addWidget(row.infoLabel, 1);
            m_layout->addWidget(container);

            m_dynamicInfoRows.append(row);
            continue;
        }

        if (def.type == SettingType::Slider) {
            SliderRow row;
            row.key = QLatin1String(def.key);
            row.defaultValue = def.defaultValue;
            row.min = def.min;
            row.max = def.max;
            row.step = def.step;
            row.nonlinear = (row.key == QLatin1String("shape.spacing"));
            row.displayScale = def.displayScale;
            row.displayDecimals = def.displayDecimals;
            row.suffix = def.suffix;
            row.dynamicTarget = def.dynamicTarget;
            row.enabledWhen = def.enabledWhen;

            auto* container = new QWidget(this);
            container->setAttribute(Qt::WA_TranslucentBackground);
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            hbox->setSpacing(8);

            if (m_starMode) {
                row.starButton = createStarButton(container, theme);
                hbox->addWidget(row.starButton);
                connectStarButton(this, row);

                if (def.supportsDynamics()) {
                    row.dynamicsButton = createDynamicsButton(container, theme);
                    hbox->addWidget(row.dynamicsButton);
                    connectDynamicsButton(this, row);
                } else {
                    row.dynamicsPlaceholder = createDynamicsPlaceholder(container, theme);
                    hbox->addWidget(row.dynamicsPlaceholder);
                }
            }

            row.nameLabel = new QLabel(
                QCoreApplication::translate("ruwa::core::brushes", def.label), container);
            row.nameLabel->setObjectName(QStringLiteral("bsw_label"));

            const int steps = qRound((def.max - def.min) / def.step);
            row.slider = new ProgressHandleSlider(container);
            row.slider->setRange(0, steps);
            row.slider->setShowValueText(true);
            row.slider->setValueDisplayMode(ProgressHandleSlider::ValueDisplayMode::RawValue);
            row.slider->setValueTextPrefix(QString());
            row.slider->setValueTextSuffix(QString());

            hbox->addWidget(row.nameLabel);
            hbox->addWidget(row.slider, 1);
            m_layout->addWidget(container);

            connect(row.slider, &ProgressHandleSlider::valueChanged, this,
                &BrushSettingsWidget::onSliderChanged);

            m_sliderRows.append(row);
            continue;
        }

        if (def.type == SettingType::Toggle) {
            ToggleRow row;
            row.key = QLatin1String(def.key);
            row.defaultValue = def.defaultValue;
            row.enabledWhen = def.enabledWhen;

            auto* container = new QWidget(this);
            container->setAttribute(Qt::WA_TranslucentBackground);
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            hbox->setSpacing(8);

            if (m_starMode) {
                row.starButton = createStarButton(container, theme);
                hbox->addWidget(row.starButton);
                connectStarButton(this, row);

                row.dynamicsPlaceholder = createDynamicsPlaceholder(container, theme);
                hbox->addWidget(row.dynamicsPlaceholder);
            }

            row.nameLabel = new QLabel(
                QCoreApplication::translate("ruwa::core::brushes", def.label), container);
            row.nameLabel->setObjectName(QStringLiteral("bsw_label"));
            row.toggle = new ToggleSwitch(container);

            hbox->addWidget(row.nameLabel);
            hbox->addStretch();
            hbox->addWidget(row.toggle);
            m_layout->addWidget(container);

            connect(
                row.toggle, &ToggleSwitch::toggled, this, &BrushSettingsWidget::onToggleChanged);

            m_toggleRows.append(row);
            continue;
        }

        if (def.type == SettingType::ComboBox) {
            ComboBoxRow row;
            row.key = QLatin1String(def.key);
            row.defaultValue = def.defaultValue;
            row.options = def.comboOptions;
            row.enabledWhen = def.enabledWhen;

            auto* container = new QWidget(this);
            container->setAttribute(Qt::WA_TranslucentBackground);
            auto* hbox = new QHBoxLayout(container);
            hbox->setContentsMargins(0, 0, 0, 0);
            hbox->setSpacing(8);

            if (m_starMode) {
                row.starButton = createStarButton(container, theme);
                hbox->addWidget(row.starButton);
                connectStarButton(this, row);

                row.dynamicsPlaceholder = createDynamicsPlaceholder(container, theme);
                hbox->addWidget(row.dynamicsPlaceholder);
            }

            row.nameLabel = new QLabel(
                QCoreApplication::translate("ruwa::core::brushes", def.label), container);
            row.nameLabel->setObjectName(QStringLiteral("bsw_label"));
            const bool useImageSelector = (row.key == QStringLiteral("dab.type"));
            const bool useBlendModeCategories = (row.key == QStringLiteral("stroke.blendMode"));
            if (useImageSelector) {
                row.imageSelector = new ImageDropdownSelector(container);
                row.imageSelector->setPlaceholderText(
                    QCoreApplication::translate("ruwa::core::brushes", "Select"));
                row.imageSelector->setMinimumWidth(theme.scaled(176));
                row.imageSelector->setFixedHeight(theme.scaled(34));
                row.imageSelector->setPopupMinWidth(theme.scaled(318));
                row.imageSelector->setPopupColumns(2);
                row.imageSelector->setPopupCardSize(QSize(theme.scaled(142), theme.scaled(104)));
                row.imageSelector->setPopupMaxHeight(theme.scaled(368));
                for (int i = 0; i < row.options.size(); ++i) {
                    ImageDropdownItem item;
                    item.text = QCoreApplication::translate(
                        "ruwa::core::brushes", row.options[i].toUtf8().constData());
                    item.userData = i;
                    item.previewImage = dabPreviewImageForIndex(i);
                    item.tintPreview = true;
                    row.imageSelector->addItem(item);
                }
            } else {
                row.combo = new AnimatedComboBox(container);
                row.combo->setPlaceholderText(
                    QCoreApplication::translate("ruwa::core::brushes", "Select"));
                row.combo->setMinimumWidth(theme.scaled(140));
                row.combo->setFixedHeight(theme.scaled(24));
                row.combo->setPopupMinWidth(theme.scaled(160));
                row.combo->setPopupMaxHeight(theme.scaled(useBlendModeCategories ? 320 : 360));
                if (useBlendModeCategories) {
                    row.combo->setPopupMinWidth(theme.scaled(240));
                    populateBrushBlendModeCombo(row.combo);
                } else {
                    for (int i = 0; i < row.options.size(); ++i) {
                        row.combo->addItem(QCoreApplication::translate("ruwa::core::brushes",
                                               row.options[i].toUtf8().constData()),
                            i);
                    }
                }
            }

            hbox->addWidget(row.nameLabel);
            hbox->addWidget(row.inputWidget(), 1);
            m_layout->addWidget(container);

            if (row.imageSelector) {
                connect(row.imageSelector, &ImageDropdownSelector::currentIndexChanged, this,
                    &BrushSettingsWidget::onComboChanged);
            } else if (row.combo) {
                connect(row.combo, &AnimatedComboBox::currentIndexChanged, this,
                    &BrushSettingsWidget::onComboChanged);
            }

            m_comboRows.append(row);
            continue;
        }

        SegmentedRow row;
        row.key = QLatin1String(def.key);
        row.defaultValue = def.defaultValue;
        row.options = def.segmentedOptions;
        row.enabledWhen = def.enabledWhen;

        auto* container = new QWidget(this);
        container->setAttribute(Qt::WA_TranslucentBackground);
        auto* hbox = new QHBoxLayout(container);
        hbox->setContentsMargins(0, 0, 0, 0);
        hbox->setSpacing(8);

        if (m_starMode) {
            row.starButton = createStarButton(container, theme);
            hbox->addWidget(row.starButton);
            connectStarButton(this, row);

            row.dynamicsPlaceholder = createDynamicsPlaceholder(container, theme);
            hbox->addWidget(row.dynamicsPlaceholder);
        }

        row.nameLabel
            = new QLabel(QCoreApplication::translate("ruwa::core::brushes", def.label), container);
        row.nameLabel->setObjectName(QStringLiteral("bsw_label"));
        row.selector = new SegmentedOptionSelector(container);
        row.selector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);

        QVector<SegmentedOptionSelector::Option> options;
        options.reserve(row.options.size());
        for (int i = 0; i < row.options.size(); ++i) {
            SegmentedOptionSelector::Option option;
            option.text = QCoreApplication::translate(
                "ruwa::core::brushes", row.options[i].toUtf8().constData());
            option.data = i;
            options.append(option);
        }
        row.selector->setOptions(options);

        hbox->addWidget(row.nameLabel);
        hbox->addWidget(row.selector, 1);
        m_layout->addWidget(container);

        connect(row.selector, &SegmentedOptionSelector::selectionChanged, this,
            &BrushSettingsWidget::onSegmentedChanged);

        m_segmentedRows.append(row);
    }
}

void BrushSettingsWidget::setStarredKeys(const QSet<QString>& keys)
{
    m_updating = true;
    setProperty("_bsw_updating", true);
    auto syncStar = [&keys](auto& rows) {
        for (auto& row : rows) {
            if (!row.starButton) {
                continue;
            }
            const bool starred = keys.contains(row.key);
            row.starButton->setChecked(starred);
            row.starButton->setText(starred ? QStringLiteral("\u2605") : QStringLiteral("\u2606"));
        }
    };

    syncStar(m_sliderRows);
    syncStar(m_toggleRows);
    syncStar(m_comboRows);
    syncStar(m_segmentedRows);
    syncStar(m_dynamicInfoRows);
    m_updating = false;
    setProperty("_bsw_updating", false);
}

void BrushSettingsWidget::onSliderChanged()
{
    auto* changedSlider = qobject_cast<ProgressHandleSlider*>(sender());
    for (auto& row : m_sliderRows) {
        if (changedSlider && row.slider != changedSlider) {
            continue;
        }

        const float value = sliderPosToValue(row, row.slider->value());
        const float display = value * static_cast<float>(row.displayScale);
        row.slider->setCustomDisplayText(QStringLiteral("%1%2")
                .arg(static_cast<double>(display), 0, 'f', row.displayDecimals)
                .arg(QLatin1StringView(row.suffix)));

        if (changedSlider) {
            break;
        }
    }

    updateDependentControls();
    if (!m_updating) {
        emit settingChanged();
    }
}

void BrushSettingsWidget::onToggleChanged(bool)
{
    updateDependentControls();
    if (!m_updating) {
        emit settingChanged();
    }
}

void BrushSettingsWidget::onComboChanged()
{
    updateDependentControls();
    if (!m_updating) {
        emit settingChanged();
    }
}

void BrushSettingsWidget::onSegmentedChanged()
{
    updateDependentControls();
    if (!m_updating) {
        emit settingChanged();
    }
}

void BrushSettingsWidget::onThemeChanged()
{
    auto& theme = ThemeManager::instance();
    if (m_layout) {
        m_layout->setContentsMargins(
            theme.scaled(2), theme.scaled(2), theme.scaled(2), theme.scaled(2));
        m_layout->setSpacing(theme.scaled(8));
    }
    applyStyles();
}

QVariant BrushSettingsWidget::currentSettingValue(const QString& key) const
{
    for (const auto& row : m_sliderRows) {
        if (row.key == key && row.slider) {
            return sliderPosToValue(row, row.slider->value());
        }
    }

    for (const auto& row : m_toggleRows) {
        if (row.key == key && row.toggle) {
            return row.toggle->isChecked();
        }
    }

    for (const auto& row : m_comboRows) {
        if (row.key == key && row.inputWidget() && !row.options.isEmpty()) {
            return qBound(0, row.currentIndex(), row.options.size() - 1);
        }
    }

    for (const auto& row : m_segmentedRows) {
        if (row.key == key && row.selector && !row.options.isEmpty()) {
            return qBound(0, row.selector->currentIndex(), row.options.size() - 1);
        }
    }

    return m_currentSettings.value(key);
}

bool BrushSettingsWidget::dependenciesSatisfied(
    const QVector<ruwa::core::brushes::BrushSettingDependency>& dependencies) const
{
    for (const auto& dependency : dependencies) {
        if (!dependency.key) {
            continue;
        }
        if (!dependency.isSatisfied(currentSettingValue(QLatin1String(dependency.key)))) {
            return false;
        }
    }
    return true;
}

void BrushSettingsWidget::updateDependentControls()
{
    auto setSliderRowEnabled = [](SliderRow* row, bool enabled) {
        if (!row) {
            return;
        }
        if (row->nameLabel) {
            row->nameLabel->setEnabled(enabled);
        }
        if (row->slider) {
            row->slider->setEnabled(enabled);
        }
        if (row->dynamicsButton) {
            row->dynamicsButton->setEnabled(enabled);
        }
    };
    auto setToggleRowEnabled = [](ToggleRow* row, bool enabled) {
        if (!row) {
            return;
        }
        if (row->nameLabel) {
            row->nameLabel->setEnabled(enabled);
        }
        if (row->toggle) {
            row->toggle->setEnabled(enabled);
        }
    };
    auto setComboRowEnabled = [](ComboBoxRow* row, bool enabled) {
        if (!row) {
            return;
        }
        if (row->nameLabel) {
            row->nameLabel->setEnabled(enabled);
        }
        row->setInputEnabled(enabled);
    };
    auto setSegmentedRowEnabled = [](SegmentedRow* row, bool enabled) {
        if (!row) {
            return;
        }
        if (row->nameLabel) {
            row->nameLabel->setEnabled(enabled);
        }
        if (row->selector) {
            row->selector->setEnabled(enabled);
        }
    };

    for (auto& row : m_sliderRows) {
        setSliderRowEnabled(&row, dependenciesSatisfied(row.enabledWhen));
    }

    for (auto& row : m_toggleRows) {
        setToggleRowEnabled(&row, dependenciesSatisfied(row.enabledWhen));
    }

    for (auto& row : m_comboRows) {
        setComboRowEnabled(&row, dependenciesSatisfied(row.enabledWhen));
    }

    for (auto& row : m_segmentedRows) {
        setSegmentedRowEnabled(&row, dependenciesSatisfied(row.enabledWhen));
    }
}

void BrushSettingsWidget::applyStyles()
{
    const auto& colors = WidgetStyleManager::instance().colors();
    auto& theme = ThemeManager::instance();

    QFont rowFont = font();
    rowFont.setPixelSize(theme.scaled(10));

    const QString labelStyle = QStringLiteral("QLabel { color: %1; background: transparent; }")
                                   .arg(colors.textMuted.name(QColor::HexArgb));
    const QString dynamicInfoStyle
        = QStringLiteral("QLabel { color: %1; background: transparent; }")
              .arg(colors.textDisabled().name(QColor::HexArgb));
    const QString starCheckedColor = colors.primary.name(QColor::HexArgb);
    const QString starUncheckedColor = colors.textDisabled().name(QColor::HexArgb);
    const QString starHoverColor = colors.textMuted.name(QColor::HexArgb);
    const int nameColumnWidth = theme.scaled(134);
    const int valueColumnWidth = theme.scaled(176);

    QFont starFont = font();
    starFont.setPixelSize(theme.scaled(12));

    auto applyStarStyle = [&](QPushButton* btn) {
        if (!btn) {
            return;
        }
        btn->setFont(starFont);
        btn->setFixedSize(theme.scaled(16), theme.scaled(16));
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { border: none; background: transparent; color: %1; padding: 0; }"
            "QPushButton:checked { color: %2; }"
            "QPushButton:hover { color: %3; }"
            "QPushButton:checked:hover { color: %2; }")
                .arg(starUncheckedColor, starCheckedColor, starHoverColor));
    };

    auto applyDynamicsPlaceholderStyle = [&](QWidget* placeholder) {
        if (!placeholder) {
            return;
        }
        const int iconSize = theme.scaled(12);
        placeholder->setFixedSize(theme.scaled(18), theme.scaled(18));
        placeholder->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

        auto* label = qobject_cast<QLabel*>(placeholder);
        if (!label) {
            return;
        }
        label->setPixmap(IconProvider::instance()
                .getColoredIcon(QStringLiteral("NotAvailable"), colors.textDisabled())
                .pixmap(iconSize, iconSize));
    };

    for (auto& row : m_sliderRows) {
        row.nameLabel->setStyleSheet(labelStyle);
        row.nameLabel->setFont(rowFont);
        row.nameLabel->setFixedWidth(nameColumnWidth);
        row.slider->setMinimumSize(valueColumnWidth, theme.scaled(22));
        row.slider->setMaximumHeight(theme.scaled(22));
        row.slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        applyStarStyle(row.starButton);
        applyDynamicsButtonStyle(row.dynamicsButton);
        applyDynamicsPlaceholderStyle(row.dynamicsPlaceholder);
    }

    for (auto& row : m_toggleRows) {
        row.nameLabel->setStyleSheet(labelStyle);
        row.nameLabel->setFont(rowFont);
        row.nameLabel->setFixedWidth(nameColumnWidth);
        row.toggle->setFixedSize(theme.scaled(40), theme.scaled(20));
        applyStarStyle(row.starButton);
        applyDynamicsButtonStyle(row.dynamicsButton);
        applyDynamicsPlaceholderStyle(row.dynamicsPlaceholder);
    }

    for (auto& row : m_comboRows) {
        row.nameLabel->setStyleSheet(labelStyle);
        row.nameLabel->setFont(rowFont);
        row.nameLabel->setFixedWidth(nameColumnWidth);
        if (row.combo) {
            row.combo->setFixedHeight(theme.scaled(24));
            row.combo->setMinimumSize(valueColumnWidth, theme.scaled(22));
            row.combo->setMaximumHeight(theme.scaled(24));
            row.combo->setPopupMaxHeight(
                theme.scaled(row.key == QStringLiteral("stroke.blendMode") ? 320 : 360));
            if (row.key == QStringLiteral("stroke.blendMode")) {
                row.combo->setPopupMinWidth(theme.scaled(240));
            }
        }
        if (row.imageSelector) {
            row.imageSelector->setFixedHeight(theme.scaled(34));
            row.imageSelector->setMinimumSize(valueColumnWidth, theme.scaled(34));
            row.imageSelector->setMaximumHeight(theme.scaled(34));
            row.imageSelector->setPopupMinWidth(theme.scaled(318));
            row.imageSelector->setPopupCardSize(QSize(theme.scaled(142), theme.scaled(104)));
            row.imageSelector->setPopupMaxHeight(theme.scaled(368));
        }
        applyStarStyle(row.starButton);
        applyDynamicsButtonStyle(row.dynamicsButton);
        applyDynamicsPlaceholderStyle(row.dynamicsPlaceholder);
    }

    for (auto& row : m_segmentedRows) {
        row.nameLabel->setStyleSheet(labelStyle);
        row.nameLabel->setFont(rowFont);
        row.nameLabel->setFixedWidth(nameColumnWidth);
        if (row.selector) {
            row.selector->setMinimumSize(valueColumnWidth, theme.scaled(32));
            row.selector->setMaximumHeight(theme.scaled(32));
            row.selector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }
        applyStarStyle(row.starButton);
        applyDynamicsButtonStyle(row.dynamicsButton);
        applyDynamicsPlaceholderStyle(row.dynamicsPlaceholder);
    }

    for (auto& row : m_dynamicInfoRows) {
        row.nameLabel->setStyleSheet(labelStyle);
        row.nameLabel->setFont(rowFont);
        row.nameLabel->setFixedWidth(nameColumnWidth);
        if (row.infoLabel) {
            row.infoLabel->setStyleSheet(dynamicInfoStyle);
            row.infoLabel->setFont(rowFont);
            row.infoLabel->setMinimumWidth(valueColumnWidth);
            row.infoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }
        applyStarStyle(row.starButton);
        applyDynamicsButtonStyle(row.dynamicsButton);
        applyDynamicsPlaceholderStyle(row.dynamicsPlaceholder);
    }

    for (auto* separator : m_separatorRows) {
        if (!separator) {
            continue;
        }
        separator->setFixedHeight(1);
        separator->setStyleSheet(QStringLiteral("background-color: %1; border: none;")
                .arg(colors.borderSubtle().name(QColor::HexArgb)));
    }
}

void BrushSettingsWidget::updateDynamicsButtonStates(const BrushSettingsData* data)
{
    for (auto& row : m_sliderRows) {
        if (!row.dynamicsButton) {
            continue;
        }

        bool isActive = false;
        if (data) {
            isActive = hasEffectiveDynamics(*data, row.dynamicTarget);
        }

        row.dynamicsButton->setProperty("dynamicsActive", isActive);
        applyDynamicsButtonStyle(row.dynamicsButton);
    }

    for (auto& row : m_dynamicInfoRows) {
        if (!row.dynamicsButton) {
            continue;
        }

        bool isActive = false;
        if (data) {
            isActive = hasEffectiveDynamics(*data, row.dynamicTarget);
        }

        row.dynamicsButton->setProperty("dynamicsActive", isActive);
        applyDynamicsButtonStyle(row.dynamicsButton);
    }
}

void BrushSettingsWidget::applyDynamicsButtonStyle(QPushButton* button)
{
    if (!button) {
        return;
    }

    const auto& colors = WidgetStyleManager::instance().colors();
    auto& theme = ThemeManager::instance();
    const bool isActive = button->property("dynamicsActive").toBool();
    const int iconSize = theme.scaled(14);

    const QColor normalColor = isActive ? colors.text : colors.textDisabled();
    const QColor hoverColor = isActive ? colors.textMuted : colors.textMuted;
    const QColor pressedColor = isActive ? colors.text : colors.textDisabled();

    QIcon dynamicsIcon;
    dynamicsIcon.addPixmap(IconProvider::instance()
                               .getColoredIcon(IconProvider::StandardIcon::Curve, normalColor)
                               .pixmap(iconSize, iconSize),
        QIcon::Normal, QIcon::Off);
    dynamicsIcon.addPixmap(IconProvider::instance()
                               .getColoredIcon(IconProvider::StandardIcon::Curve, hoverColor)
                               .pixmap(iconSize, iconSize),
        QIcon::Active, QIcon::Off);
    dynamicsIcon.addPixmap(IconProvider::instance()
                               .getColoredIcon(IconProvider::StandardIcon::Curve, pressedColor)
                               .pixmap(iconSize, iconSize),
        QIcon::Selected, QIcon::Off);

    QFont dynamicsFont = font();
    dynamicsFont.setPixelSize(theme.scaled(8));
    dynamicsFont.setBold(true);

    button->setFont(dynamicsFont);
    button->setFixedSize(theme.scaled(18), theme.scaled(18));
    button->setText(QString());
    button->setIcon(dynamicsIcon);
    button->setIconSize(QSize(iconSize, iconSize));
    button->setStyleSheet(QStringLiteral(
        "QPushButton { border: none; background: transparent; color: %1; padding: 0; }"
        "QPushButton:hover { color: %2; }"
        "QPushButton:pressed { color: %3; }")
            .arg(normalColor.name(QColor::HexArgb), hoverColor.name(QColor::HexArgb),
                pressedColor.name(QColor::HexArgb)));
}

} // namespace ruwa::ui::widgets
