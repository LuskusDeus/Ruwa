// SPDX-License-Identifier: MPL-2.0

#include "EffectCard.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/inputs/NumericInputField.h"
#include "shared/widgets/inputs/PositionInputField.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shell/context-menu/ContextMenuSystem.h"

#include <QApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVBoxLayout>

namespace ruwa::ui::workspace {

using namespace ruwa::core::effects;
using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeManager;

namespace {
constexpr int kCardRadius = 8;

// Drag-source dim, matching LayerRowWidget's 0.35 factor so the effects panel
// reads the same as the layers panel while dragging.
constexpr qreal kDragDimOpacity = 0.35;

// Fixed parameter grid: every param row is exactly kParamRowUnitHeight tall —
// or a whole multiple of it (2x-5x), reserved for a future control that needs
// more vertical room than the current compact editors — and split into a
// flexible name column (left) + a value column that is always exactly
// kParamValueColumnWidth wide, so every editor's left edge lines up under the
// one above it regardless of which control type it is.
//
// The row height is pinned to exactly the tallest current editor (the colour
// capsule) with NO padding baked in — the gap between rows lives entirely in
// kParamRowGap (inserted between rows, not inside them). Stacking both used to
// double the visible gap: a few px of vertical-centering slack inside each
// fixed-height row, PLUS the inter-row spacing on top of that.
constexpr int kColorEditorHeight = 32;
constexpr int kParamRowUnitHeight = kColorEditorHeight;
constexpr int kParamRowGap = 2;
constexpr int kHeaderToParamsGap = 8;
constexpr int kParamValueColumnWidth = 140;

// Overflow-menu action ids (routed back via handleContextMenuAction()).
enum CardAction {
    ActionMoveUp = 1,
    ActionMoveDown,
    ActionTogglePreview,
    ActionDuplicate,
    ActionReset,
    ActionDelete
};

/// 2x3 dotted drag handle, purely decorative for now.
class GripHandle : public QWidget {
public:
    explicit GripHandle(QWidget* parent)
        : QWidget(parent)
    {
        setFixedSize(12, 20);
        setCursor(Qt::OpenHandCursor);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& c = ThemeManager::instance().colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(c.textMuted);
        const int cx0 = 3;
        const int cx1 = 8;
        const int y0 = 5;
        for (int col = 0; col < 2; ++col) {
            for (int row = 0; row < 3; ++row) {
                const int x = (col == 0 ? cx0 : cx1);
                const int y = y0 + row * 5;
                p.drawEllipse(QPointF(x, y), 1.1, 1.1);
            }
        }
    }
};
} // namespace

EffectCard::EffectCard(
    const LayerEffectDescriptor& descriptor, const LayerEffectState& state, QWidget* parent)
    : ruwa::ui::widgets::ReorderableRowWidget(parent)
    , m_descriptor(descriptor)
    , m_state(state)
    , m_instanceId(state.instanceId)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 10);
    // Gaps are inserted explicitly (see below) rather than via a uniform
    // spacing, so the header keeps its own breathing room while param rows
    // pack tightly against each other.
    root->setSpacing(0);

    buildHeader(root);
    root->addSpacing(ThemeManager::instance().scaled(kHeaderToParamsGap));
    buildParams(root);

    // Re-tint the theme-coloured icons when the palette changes.
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        refreshEyeIcon();
        refreshMenuIcon();
        update();
    });
}

void EffectCard::buildHeader(QVBoxLayout* root)
{
    const auto& c = ThemeManager::instance().colors();

    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_grip = new GripHandle(this);
    m_grip->installEventFilter(this);
    header->addWidget(m_grip);

    m_titleLabel = new QLabel(
        m_descriptor.displayName.isEmpty() ? m_descriptor.typeId : m_descriptor.displayName, this);
    QFont titleFont = c.fonts.getUIFont();
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    header->addWidget(m_titleLabel, 1);

    const QString iconButtonStyle
        = QStringLiteral("QPushButton { background: transparent; border: none; border-radius: 5px; "
                         "color: %1; padding: 0; }"
                         "QPushButton:hover { background: %2; }")
              .arg(c.textMuted.name(), c.surfaceHover().name());

    m_eyeButton = new QPushButton(this);
    m_eyeButton->setFixedSize(24, 24);
    m_eyeButton->setCursor(Qt::PointingHandCursor);
    m_eyeButton->setStyleSheet(iconButtonStyle);
    m_eyeButton->setToolTip(tr("Toggle effect (bypass)"));
    refreshEyeIcon();
    connect(m_eyeButton, &QPushButton::clicked, this, [this]() {
        m_state.enabled = !m_state.enabled;
        refreshEyeIcon();
        emit enabledToggled(m_state.enabled);
    });
    header->addWidget(m_eyeButton);

    m_menuButton = new QPushButton(this);
    m_menuButton->setFixedSize(24, 24);
    m_menuButton->setCursor(Qt::PointingHandCursor);
    m_menuButton->setStyleSheet(iconButtonStyle);
    m_menuButton->setToolTip(tr("More"));
    refreshMenuIcon();
    connect(m_menuButton, &QPushButton::clicked, this, &EffectCard::showOverflowMenu);
    header->addWidget(m_menuButton);

    root->addLayout(header);
}

void EffectCard::refreshEyeIcon()
{
    if (!m_eyeButton) {
        return;
    }
    const auto& c = ThemeManager::instance().colors();
    const auto icon = m_state.enabled ? IconProvider::StandardIcon::Eye
                                      : IconProvider::StandardIcon::EyeDeactivated;
    // Tint to the theme text colour; muted when the effect is bypassed.
    const QColor tint = m_state.enabled ? c.text : c.textMuted;
    m_eyeButton->setIcon(IconProvider::instance().getColoredIcon(icon, tint));
    m_eyeButton->setIconSize(QSize(15, 15));
}

void EffectCard::refreshMenuIcon()
{
    if (!m_menuButton) {
        return;
    }
    const auto& c = ThemeManager::instance().colors();
    m_menuButton->setIcon(
        IconProvider::instance().getColoredIcon(IconProvider::StandardIcon::Dots, c.textMuted));
    m_menuButton->setIconSize(QSize(15, 15));
}

void EffectCard::buildParams(QVBoxLayout* root)
{
    bool first = true;
    QSet<QString> consumedKeys;
    for (const EffectParamDefinition& param : m_descriptor.params) {
        if (consumedKeys.contains(param.key)) {
            continue;
        }
        if (!first) {
            root->addSpacing(ThemeManager::instance().scaled(kParamRowGap));
        }
        first = false;

        // A Real param tagged as the X half of a position pair renders together
        // with its Y partner as one PositionInputField row instead of two
        // separate number rows — find the partner by matching pair key.
        if (param.type == EffectParamType::Real && !param.positionPairKey.isEmpty()
            && param.positionAxis == EffectParamPositionAxis::X) {
            const EffectParamDefinition* yParam = nullptr;
            for (const EffectParamDefinition& candidate : m_descriptor.params) {
                if (candidate.type == EffectParamType::Real
                    && candidate.positionPairKey == param.positionPairKey
                    && candidate.positionAxis == EffectParamPositionAxis::Y) {
                    yParam = &candidate;
                    break;
                }
            }
            if (yParam) {
                addPositionParamEditor(root, param, *yParam);
                consumedKeys.insert(param.key);
                consumedKeys.insert(yParam->key);
                continue;
            }
        }

        addParamEditor(root, param);
        consumedKeys.insert(param.key);
    }
}

QVariant EffectCard::paramValue(const EffectParamDefinition& param) const
{
    return m_state.params.contains(param.key) ? m_state.params.value(param.key)
                                              : param.defaultValue;
}

void EffectCard::setEffectState(const LayerEffectState& state)
{
    // A card is permanently bound to one effect instance and descriptor.
    if (state.instanceId != m_instanceId || state.typeId != m_state.typeId) {
        return;
    }

    m_syncing = true;
    m_state = state;
    refreshEyeIcon();

    QSet<QString> syncedPositionPairs;
    for (const EffectParamDefinition& param : m_descriptor.params) {
        QWidget* editorWidget = m_paramEditorByKey.value(param.key, nullptr);
        if (!editorWidget) {
            continue;
        }

        const QVariant value = paramValue(param);
        switch (param.type) {
        case EffectParamType::Bool:
            if (auto* editor = qobject_cast<ruwa::ui::widgets::ToggleSwitch*>(editorWidget)) {
                editor->setCheckedInstant(value.toBool());
            }
            break;
        case EffectParamType::Int:
            if (auto* editor = qobject_cast<ruwa::ui::widgets::NumericInputField*>(editorWidget)) {
                editor->setValue(value.toInt());
            } else if (auto* editor
                = qobject_cast<ruwa::ui::widgets::ProgressHandleSlider*>(editorWidget)) {
                const int minValue = param.minimumValue.toInt();
                const int step = qMax(1, param.stepValue.isValid() ? param.stepValue.toInt() : 1);
                const int position = qBound(editor->minimum(),
                    qRound((value.toInt() - minValue) / static_cast<double>(step)),
                    editor->maximum());
                editor->setValue(position);
                editor->setCustomDisplayText(QString::number(value.toInt()));
            }
            break;
        case EffectParamType::Real:
            if (!param.positionPairKey.isEmpty()) {
                if (syncedPositionPairs.contains(param.positionPairKey)) {
                    break;
                }
                syncedPositionPairs.insert(param.positionPairKey);

                const EffectParamDefinition* xParam = nullptr;
                const EffectParamDefinition* yParam = nullptr;
                for (const EffectParamDefinition& candidate : m_descriptor.params) {
                    if (candidate.positionPairKey != param.positionPairKey) {
                        continue;
                    }
                    if (candidate.positionAxis == EffectParamPositionAxis::X) {
                        xParam = &candidate;
                    } else if (candidate.positionAxis == EffectParamPositionAxis::Y) {
                        yParam = &candidate;
                    }
                }
                if (xParam && yParam) {
                    if (auto* editor
                        = qobject_cast<ruwa::ui::widgets::PositionInputField*>(editorWidget)) {
                        editor->setPosition(QPointF(
                            paramValue(*xParam).toDouble(), paramValue(*yParam).toDouble()));
                    }
                }
                break;
            }
            if (auto* editor = qobject_cast<ruwa::ui::widgets::NumericInputField*>(editorWidget)) {
                editor->setValue(value.toDouble());
            } else if (auto* editor
                = qobject_cast<ruwa::ui::widgets::ProgressHandleSlider*>(editorWidget)) {
                const double minValue = param.minimumValue.toDouble();
                const double step = param.stepValue.isValid() && param.stepValue.toDouble() > 0.0
                    ? param.stepValue.toDouble()
                    : 0.01;
                const int decimals = step < 1.0 ? 2 : 0;
                const int position = qBound(editor->minimum(),
                    qRound((value.toDouble() - minValue) / step), editor->maximum());
                editor->setValue(position);
                editor->setCustomDisplayText(QString::number(value.toDouble(), 'f', decimals));
            }
            break;
        case EffectParamType::Choice:
            if (auto* editor = qobject_cast<ruwa::ui::widgets::AnimatedComboBox*>(editorWidget)) {
                editor->setCurrentIndex(qMax(0, editor->findIndexByData(value.toString())));
            }
            break;
        case EffectParamType::Color:
            if (auto* editor = qobject_cast<ruwa::ui::widgets::ColorInputButton*>(editorWidget)) {
                QColor color(value.toString().trimmed());
                editor->setColor(color.isValid() ? color : QColor(Qt::black));
            }
            break;
        }
    }

    m_syncing = false;
    update();
}

void EffectCard::addParamEditor(QVBoxLayout* parentLayout, const EffectParamDefinition& param)
{
    if (!parentLayout || param.key.isEmpty()) {
        return;
    }
    const auto& c = ThemeManager::instance().colors();

    auto* row = new QWidget(this);
    // Fixed grid: every param row is exactly one kParamRowUnitHeight tall (a
    // future editor that needs more vertical room can request a whole multiple
    // of it instead — see the constant's comment).
    row->setFixedHeight(ThemeManager::instance().scaled(kParamRowUnitHeight));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto* label = new QLabel(param.label.isEmpty() ? param.key : param.label, row);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(c.textMuted.name()));
    // Name column: flexible, fills whatever the fixed-width value column leaves.
    rowLayout->addWidget(label, 1, Qt::AlignVCenter);

    // Value column: always exactly this wide, so every editor's left edge lines
    // up under the one above it no matter which control type it is.
    const int valueColumnWidth = ThemeManager::instance().scaled(kParamValueColumnWidth);

    const QVariant value = paramValue(param);
    switch (param.type) {
    case EffectParamType::Bool: {
        // Initial state is set via the ctor before connecting, so no spurious emit.
        auto* editor = new ruwa::ui::widgets::ToggleSwitch(value.toBool(), row);
        m_paramEditorByKey.insert(param.key, editor);
        editor->setFixedSize(
            ThemeManager::instance().scaled(40), ThemeManager::instance().scaled(22));
        connect(editor, &ruwa::ui::widgets::ToggleSwitch::toggled, this,
            [this, key = param.key](bool checked) {
                if (!m_syncing)
                    emit paramChanged(key, checked);
            });
        // The switch is inherently small; right-align it within the same fixed
        // value-column width the other editors fill, so the columns still align.
        auto* cell = new QWidget(row);
        cell->setFixedWidth(valueColumnWidth);
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->addStretch(1);
        cellLayout->addWidget(editor);
        rowLayout->addWidget(cell, 0, Qt::AlignVCenter);
        break;
    }
    case EffectParamType::Int: {
        const int minV = param.minimumValue.toInt();
        const int maxV = param.maximumValue.toInt();
        const int stepV = qMax(1, param.stepValue.isValid() ? param.stepValue.toInt() : 1);

        if (param.preferredEditor == EffectParamEditorHint::NumberField) {
            auto* editor = new ruwa::ui::widgets::NumericInputField(row);
            m_paramEditorByKey.insert(param.key, editor);
            // Same height as the colour capsule — it reads as a peer input,
            // not a slider, so it should look like one.
            editor->setFixedSize(
                valueColumnWidth, ThemeManager::instance().scaled(kColorEditorHeight));
            editor->setDecimals(0);
            editor->setRange(minV, maxV);
            editor->setSingleStep(stepV);
            editor->setValue(value.toInt());
            connect(editor, &ruwa::ui::widgets::NumericInputField::valueChanged, this,
                [this, key = param.key](double v) {
                    if (!m_syncing)
                        emit paramLiveChanged(key, qRound(v));
                });
            connect(editor, &QLineEdit::editingFinished, this, &EffectCard::paramEditFinished);
            rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
            break;
        }

        const int steps = qMax(1, qRound((maxV - minV) / static_cast<double>(stepV)));

        // The slider itself is an integer position [0, steps]; posToValue maps
        // that back to the param's real min/step/max range (same technique as
        // BrushSettingsWidget's linear sliders).
        auto posToValue = [minV, stepV](int pos) { return minV + pos * stepV; };
        auto valueToPos = [minV, stepV, steps](int v) {
            return qBound(0, qRound((v - minV) / static_cast<double>(stepV)), steps);
        };

        auto* editor = new ruwa::ui::widgets::ProgressHandleSlider(row);
        m_paramEditorByKey.insert(param.key, editor);
        editor->setFixedSize(valueColumnWidth, ThemeManager::instance().scaled(22));
        editor->setRange(0, steps);
        editor->setShowValueText(true);
        editor->setValueDisplayMode(
            ruwa::ui::widgets::ProgressHandleSlider::ValueDisplayMode::RawValue);
        editor->setValueTextPrefix(QString());
        editor->setValueTextSuffix(QString());

        const int initialValue = value.toInt();
        editor->setValue(valueToPos(initialValue));
        editor->setCustomDisplayText(QString::number(initialValue));

        connect(editor, &ruwa::ui::widgets::ProgressHandleSlider::valueChanged, this,
            [this, editor, posToValue, key = param.key](int pos) {
                const int v = posToValue(pos);
                editor->setCustomDisplayText(QString::number(v));
                if (!m_syncing)
                    emit paramLiveChanged(key, v);
            });
        connect(editor, &ruwa::ui::widgets::ProgressHandleSlider::sliderReleased, this,
            &EffectCard::paramEditFinished);
        rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
        break;
    }
    case EffectParamType::Real: {
        const double minV = param.minimumValue.toDouble();
        const double maxV = param.maximumValue.toDouble();
        const double stepV = param.stepValue.isValid() && param.stepValue.toDouble() > 0.0
            ? param.stepValue.toDouble()
            : 0.01;
        // Every current Real param uses either a 0.01 (fractional) or a >=1.0
        // (pixel-scale) step; show 2 decimals for the former, none for the latter.
        const int decimals = stepV < 1.0 ? 2 : 0;

        if (param.preferredEditor == EffectParamEditorHint::NumberField) {
            auto* editor = new ruwa::ui::widgets::NumericInputField(row);
            m_paramEditorByKey.insert(param.key, editor);
            // Same height as the colour capsule — it reads as a peer input,
            // not a slider, so it should look like one.
            editor->setFixedSize(
                valueColumnWidth, ThemeManager::instance().scaled(kColorEditorHeight));
            editor->setDecimals(decimals);
            editor->setRange(minV, maxV);
            editor->setSingleStep(stepV);
            editor->setValue(value.toDouble());
            connect(editor, &ruwa::ui::widgets::NumericInputField::valueChanged, this,
                [this, key = param.key](double v) {
                    if (!m_syncing)
                        emit paramLiveChanged(key, v);
                });
            connect(editor, &QLineEdit::editingFinished, this, &EffectCard::paramEditFinished);
            rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
            break;
        }

        const int steps = qMax(1, qRound((maxV - minV) / stepV));

        auto posToValue = [minV, stepV](int pos) { return minV + pos * stepV; };
        auto valueToPos = [minV, stepV, steps](
                              double v) { return qBound(0, qRound((v - minV) / stepV), steps); };

        auto* editor = new ruwa::ui::widgets::ProgressHandleSlider(row);
        m_paramEditorByKey.insert(param.key, editor);
        editor->setFixedSize(valueColumnWidth, ThemeManager::instance().scaled(22));
        editor->setRange(0, steps);
        editor->setShowValueText(true);
        editor->setValueDisplayMode(
            ruwa::ui::widgets::ProgressHandleSlider::ValueDisplayMode::RawValue);
        editor->setValueTextPrefix(QString());
        editor->setValueTextSuffix(QString());

        const double initialValue = value.toDouble();
        editor->setValue(valueToPos(initialValue));
        editor->setCustomDisplayText(QString::number(initialValue, 'f', decimals));

        connect(editor, &ruwa::ui::widgets::ProgressHandleSlider::valueChanged, this,
            [this, editor, posToValue, decimals, key = param.key](int pos) {
                const double v = posToValue(pos);
                editor->setCustomDisplayText(QString::number(v, 'f', decimals));
                if (!m_syncing)
                    emit paramLiveChanged(key, v);
            });
        connect(editor, &ruwa::ui::widgets::ProgressHandleSlider::sliderReleased, this,
            &EffectCard::paramEditFinished);
        rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
        break;
    }
    case EffectParamType::Choice: {
        auto* editor = new ruwa::ui::widgets::AnimatedComboBox(row);
        m_paramEditorByKey.insert(param.key, editor);
        editor->setFixedSize(valueColumnWidth, ThemeManager::instance().scaled(28));
        for (const QString& choice : param.choices) {
            editor->addItem(choice, choice);
        }
        // Populate + select before connecting so the initial index change is silent.
        editor->setCurrentIndex(qMax(0, param.choices.indexOf(value.toString())));
        connect(editor, &ruwa::ui::widgets::AnimatedComboBox::currentIndexChanged, this,
            [this, editor, key = param.key](int) {
                if (!m_syncing)
                    emit paramChanged(key, editor->currentText());
            });
        rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
        break;
    }
    case EffectParamType::Color: {
        // Capsule with a colour swatch + hex, styled like the Color panel input.
        // It is display-only (no typing): clicking anywhere opens the shared
        // colour picker popup, which writes the colour back via colorChanged.
        QColor initial(value.toString().trimmed());
        if (!initial.isValid()) {
            initial = Qt::black;
        }
        ruwa::ui::widgets::ColorInputButtonOptions opts;
        opts.showLabel = false;
        opts.showHex = true;
        opts.capsuleStyle = true;
        opts.boldLabel = false;
        opts.baseHeight = kColorEditorHeight;
        opts.alphaEnabled = true; // effect colours are stored ARGB (e.g. gradient overlay)
        auto* editor = new ruwa::ui::widgets::ColorInputButton(QString(), initial, opts, row);
        m_paramEditorByKey.insert(param.key, editor);
        editor->setFixedWidth(valueColumnWidth); // height is driven by opts.baseHeight
        connect(editor, &ruwa::ui::widgets::ColorInputButton::colorPickerRequested, this,
            [this, editor](const QColor& c) { emit colorPickerRequested(c, editor); });
        connect(editor, &ruwa::ui::widgets::ColorInputButton::colorChanged, this,
            [this, key = param.key](const QColor& c) {
                if (m_syncing)
                    return;
                // Effect renderers parse colours with QColor(text); store the hex in
                // the same #AARRGGBB / #RRGGBB form ColorInputButton displays.
                const QString hex = c.alpha() < 255 ? c.name(QColor::HexArgb).toUpper()
                                                    : c.name(QColor::HexRgb).toUpper();
                emit paramLiveChanged(key, hex);
            });
        rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
        break;
    }
    }
    parentLayout->addWidget(row);
}

void EffectCard::addPositionParamEditor(QVBoxLayout* parentLayout,
    const EffectParamDefinition& xParam, const EffectParamDefinition& yParam)
{
    if (!parentLayout) {
        return;
    }
    const auto& c = ThemeManager::instance().colors();

    auto* row = new QWidget(this);
    row->setFixedHeight(ThemeManager::instance().scaled(kParamRowUnitHeight));
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(8);

    auto* label = new QLabel(xParam.label.isEmpty() ? xParam.key : xParam.label, row);
    label->setStyleSheet(QStringLiteral("color: %1;").arg(c.textMuted.name()));
    rowLayout->addWidget(label, 1, Qt::AlignVCenter);

    const int valueColumnWidth = ThemeManager::instance().scaled(kParamValueColumnWidth);
    const double stepV = xParam.stepValue.isValid() && xParam.stepValue.toDouble() > 0.0
        ? xParam.stepValue.toDouble()
        : 1.0;
    const int decimals = stepV < 1.0 ? 2 : 0;

    // Capsule with "X: n   Y: n", styled like the Color panel's capsule input.
    // Display-only: clicking it enters on-canvas position-picking mode instead
    // of typing X/Y by hand (see EffectCard::positionPickerRequested).
    auto* editor = new ruwa::ui::widgets::PositionInputField(row);
    m_paramEditorByKey.insert(xParam.key, editor);
    m_paramEditorByKey.insert(yParam.key, editor);
    editor->setFixedSize(valueColumnWidth, ThemeManager::instance().scaled(kColorEditorHeight));
    editor->setDecimals(decimals);
    editor->setPosition(QPointF(paramValue(xParam).toDouble(), paramValue(yParam).toDouble()));

    connect(editor, &ruwa::ui::widgets::PositionInputField::pickRequested, this,
        [this, editor]() { emit positionPickerRequested(editor, editor->position()); });
    connect(editor, &ruwa::ui::widgets::PositionInputField::positionChanged, this,
        [this, xKey = xParam.key, yKey = yParam.key](const QPointF& p) {
            if (m_syncing)
                return;
            emit paramLiveChanged(xKey, p.x());
            emit paramLiveChanged(yKey, p.y());
        });

    rowLayout->addWidget(editor, 0, Qt::AlignVCenter);
    parentLayout->addWidget(row);
}

void EffectCard::setMoveEnabled(bool canMoveUp, bool canMoveDown)
{
    m_canMoveUp = canMoveUp;
    m_canMoveDown = canMoveDown;
}

void EffectCard::showOverflowMenu()
{
    if (!m_menuButton) {
        return;
    }
    auto& menus = ruwa::ui::widgets::ContextMenuSystem::instance();

    // Re-click on the "⋯" button while its menu is open toggles it closed.
    if (menus.isMenuActiveFor(this)) {
        menus.hideContextMenuAnimated();
        return;
    }

    // Anchor the menu to the button rect so it drops from (or flips above) the
    // trigger adaptively instead of always spawning from a fixed offset.
    const QRect anchor(m_menuButton->mapToGlobal(QPoint(0, 0)), m_menuButton->size());
    QVariantMap ctx = contextMenuContext();
    ctx.insert(QStringLiteral("anchorRect"), anchor);

    const QPoint pos = m_menuButton->mapToGlobal(QPoint(0, m_menuButton->height() + 4));
    menus.showContextMenu(ruwa::ui::widgets::ContextMenuType::SimpleActions, pos, ctx, this);
}

namespace {
QVariantMap simpleAction(int id, const QString& text, IconProvider::StandardIcon icon,
    bool enabled = true, bool danger = false, bool checked = false)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("text"), text);
    m.insert(QStringLiteral("standardIcon"), static_cast<int>(icon));
    m.insert(QStringLiteral("enabled"), enabled);
    if (danger) {
        m.insert(QStringLiteral("danger"), true);
    }
    if (checked) {
        m.insert(QStringLiteral("checked"), true);
    }
    return m;
}
QVariantMap simpleSeparator()
{
    QVariantMap m;
    m.insert(QStringLiteral("separator"), true);
    return m;
}
} // namespace

ruwa::ui::widgets::ContextMenuType EffectCard::contextMenuType() const
{
    return ruwa::ui::widgets::ContextMenuType::SimpleActions;
}

QVariantMap EffectCard::contextMenuContext() const
{
    QVariantList actions;
    actions.append(simpleAction(
        ActionMoveUp, tr("Move Up"), IconProvider::StandardIcon::ArrowUp, m_canMoveUp));
    actions.append(simpleAction(
        ActionMoveDown, tr("Move Down"), IconProvider::StandardIcon::ArrowDown, m_canMoveDown));
    actions.append(simpleSeparator());
    actions.append(simpleAction(ActionTogglePreview, tr("Show in brush preview"),
        IconProvider::StandardIcon::Eye, true, false, m_state.realtimePreviewEnabled));
    actions.append(simpleSeparator());
    actions.append(
        simpleAction(ActionDuplicate, tr("Duplicate"), IconProvider::StandardIcon::Duplicate));
    actions.append(simpleAction(ActionReset, tr("Reset"), IconProvider::StandardIcon::UndoArrow));
    actions.append(simpleSeparator());
    actions.append(simpleAction(
        ActionDelete, tr("Delete"), IconProvider::StandardIcon::Trash, true, /*danger*/ true));

    QVariantMap ctx;
    ctx.insert(QStringLiteral("simpleActions"), actions);
    return ctx;
}

void EffectCard::handleContextMenuAction(int actionId)
{
    switch (actionId) {
    case ActionMoveUp:
        emit moveUpRequested();
        break;
    case ActionMoveDown:
        emit moveDownRequested();
        break;
    case ActionTogglePreview:
        m_state.realtimePreviewEnabled = !m_state.realtimePreviewEnabled;
        emit previewToggled(m_state.realtimePreviewEnabled);
        break;
    case ActionDuplicate:
        emit duplicateRequested();
        break;
    case ActionReset:
        emit resetRequested();
        break;
    case ActionDelete:
        emit removeRequested();
        break;
    default:
        break;
    }
}

int EffectCard::effectiveRowHeight() const
{
    // Content-measured height in device pixels. The list layout is told NOT to
    // re-scale row heights (setScaleRowHeights(false)) so this is used verbatim.
    return sizeHint().height();
}

void EffectCard::setDragging(bool dragging)
{
    if (m_dragging == dragging)
        return;
    m_dragging = dragging;
    // Dim the whole card while it is the drag source. The card is a container
    // of live child widgets, so a paint-time opacity only reaches the painted
    // background — a graphics effect dims the entire subtree, matching the
    // layers panel's 35% drag dim. The engine briefly toggles this off/on
    // around rendering the full-brightness ghost snapshot (ListDragDrop).
    if (dragging) {
        auto* dim = new QGraphicsOpacityEffect(this);
        dim->setOpacity(kDragDimOpacity * m_rowOpacity);
        m_dimEffect = dim;
        setGraphicsEffect(dim);
    } else {
        m_dimEffect = nullptr;
        setGraphicsEffect(nullptr);
    }
    update();
}

void EffectCard::setRowOpacity(qreal v)
{
    ReorderableRowWidget::setRowOpacity(v);
    if (m_dimEffect) {
        m_dimEffect->setOpacity(kDragDimOpacity * m_rowOpacity);
    }
}

bool EffectCard::eventFilter(QObject* watched, QEvent* event)
{
    // Drag reorder is initiated from the grip handle only, so param editors and
    // the header buttons stay fully interactive.
    if (watched == m_grip) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                m_gripPressed = true;
                m_dragArmed = false;
                m_pressGlobalPos = me->globalPosition().toPoint();
                return true;
            }
            break;
        }
        case QEvent::MouseMove: {
            if (m_gripPressed && !m_dragArmed) {
                auto* me = static_cast<QMouseEvent*>(event);
                const QPoint gp = me->globalPosition().toPoint();
                if ((gp - m_pressGlobalPos).manhattanLength()
                    >= QApplication::startDragDistance()) {
                    m_dragArmed = true;
                    m_gripPressed = false;
                    emit dragInitiated(m_instanceId, gp);
                }
                return true;
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            m_gripPressed = false;
            m_dragArmed = false;
            break;
        default:
            break;
        }
    }
    return ReorderableRowWidget::eventFilter(watched, event);
}

void EffectCard::paintEvent(QPaintEvent*)
{
    const auto& c = ThemeManager::instance().colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    // Honour the list engine's row-opacity animation (fade-in of new cards).
    // While dragging, the graphics effect from setDragging() already dims the
    // whole card (children included), so don't multiply the background again.
    p.setOpacity(m_dragging ? 1.0 : m_rowOpacity);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.surface);
    p.drawRoundedRect(r, kCardRadius, kCardRadius);
}

} // namespace ruwa::ui::workspace
