// SPDX-License-Identifier: MPL-2.0

#include "LayerTextCharacterEditor.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/AssetToggleButton.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/inputs/FontDropdownSelector.h"
#include "shared/widgets/inputs/NumericInputField.h"
#include "shared/widgets/layout/PropertyRowLayout.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QSignalBlocker>

#include <memory>
#include <utility>

namespace ruwa::ui::widgets {

using ruwa::core::layers::TextCaps;
using ruwa::core::layers::TextLayerEdit;
using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeManager;

namespace {

constexpr int kToggleBaseSize = 28;
constexpr int kToggleBaseIconSize = 14;
constexpr double kFontSizeMin = 1.0;
constexpr double kFontSizeMax = 2000.0;
// Photoshop's tracking range, in thousandths of an em.
constexpr double kTrackingMin = -1000.0;
constexpr double kTrackingMax = 10000.0;

} // namespace

LayerTextCharacterEditor::LayerTextCharacterEditor(QWidget* parent)
    : QWidget(parent)
{
    m_rows = std::make_unique<PropertyRowLayout>(this);

    // --- Font family ---
    m_fontDropdown = new FontDropdownSelector(this);
    m_fontDropdown->setFontFamilies(QFontDatabase::families());
    m_fontDropdown->setPlaceholderText(tr("Font"));
    m_fontDropdown->setPopupMaxHeight(320);
    m_rows->addFullWidthRow(m_fontDropdown);
    const auto emitFamilyEdit = [this](const QString& family, TextLayerEdit::Phase phase) {
        if (m_syncing || family.isEmpty()) {
            return;
        }
        TextLayerEdit edit;
        edit.property = TextLayerEdit::Property::FontFamily;
        edit.stringValue = family;
        edit.phase = phase;
        emit editRequested(edit);
    };
    connect(m_fontDropdown, &FontDropdownSelector::activated, this,
        [emitFamilyEdit](
            const QString& family) { emitFamilyEdit(family, TextLayerEdit::Phase::Commit); });
    // Hovering a row shows the font on the canvas without choosing it; the run
    // is closed by the activation above, or undone by the cancel below.
    connect(m_fontDropdown, &FontDropdownSelector::familyPreviewed, this,
        [emitFamilyEdit](
            const QString& family) { emitFamilyEdit(family, TextLayerEdit::Phase::Live); });
    connect(m_fontDropdown, &FontDropdownSelector::previewCancelled, this, [this]() {
        if (m_syncing) {
            return;
        }
        TextLayerEdit edit;
        edit.property = TextLayerEdit::Property::FontFamily;
        edit.phase = TextLayerEdit::Phase::Cancel;
        emit editRequested(edit);
    });

    // --- Numeric attributes ---
    m_sizeInput = new NumericInputField(this);
    m_sizeInput->setRange(kFontSizeMin, kFontSizeMax);
    m_sizeInput->setDecimals(0);
    m_sizeInput->setSingleStep(1.0);
    m_sizeInput->setSuffix(QStringLiteral("px"));
    m_rows->addRow(tr("Size"), m_sizeInput);

    m_trackingInput = new NumericInputField(this);
    m_trackingInput->setRange(kTrackingMin, kTrackingMax);
    m_trackingInput->setDecimals(0);
    m_trackingInput->setSingleStep(10.0);
    m_rows->addRow(tr("Tracking"), m_trackingInput);

    // Applied as the value changes, so the canvas follows the number the way it
    // follows a colour drag; the undo step is collapsed on the canvas side and
    // closed by the editingFinished below.
    bindNumericField(m_sizeInput, TextLayerEdit::Property::FontSize, m_sizeDirty,
        [this]() { return m_state.fontSize; });
    bindNumericField(m_trackingInput, TextLayerEdit::Property::Tracking, m_trackingDirty,
        [this]() { return m_state.tracking; });

    // --- Colour ---
    ColorInputButtonOptions colorOptions;
    colorOptions.boldLabel = false;
    colorOptions.hoverStrength = 0.06;
    colorOptions.baseHeight = 34;
    m_colorInput = new ColorInputButton(tr("Text Color"), QColor(0, 0, 0), colorOptions, this);
    m_rows->addFullWidthRow(m_colorInput);
    connect(m_colorInput, &ColorInputButton::colorPickerRequested, this,
        [this](const QColor& color) { emit colorPickerRequested(color, m_colorInput); });
    connect(m_colorInput, &ColorInputButton::colorChanged, this, [this](const QColor& color) {
        if (m_syncing || !color.isValid()) {
            return;
        }
        TextLayerEdit edit;
        edit.property = TextLayerEdit::Property::Color;
        edit.colorValue = color;
        emit editRequested(edit);
    });

    // --- Style toggles ---
    auto* toggleRow = new QWidget(this);
    auto* toggleLayout = new QHBoxLayout(toggleRow);
    toggleLayout->setContentsMargins(0, 0, 0, 0);
    toggleLayout->setSpacing(6);

    const auto addToggle = [&](IconProvider::StandardIcon icon, const QString& tip) {
        auto* button = new AssetToggleButton(toggleRow);
        button->setIconType(icon);
        button->setBaseSize(kToggleBaseSize);
        button->setBaseIconSize(kToggleBaseIconSize);
        button->setToolTip(tip);
        // These controls stand in for the on-canvas popup; taking focus here
        // would pull the caret out of the text being edited.
        button->setFocusPolicy(Qt::NoFocus);
        toggleLayout->addWidget(button);
        return button;
    };

    m_boldButton = addToggle(IconProvider::StandardIcon::Bold, tr("Bold"));
    m_italicButton = addToggle(IconProvider::StandardIcon::Italic, tr("Italic"));
    m_underlineButton = addToggle(IconProvider::StandardIcon::Underline, tr("Underline"));
    m_strikethroughButton
        = addToggle(IconProvider::StandardIcon::Strikethrough, tr("Strikethrough"));
    m_allCapsButton = addToggle(IconProvider::StandardIcon::AllCaps, tr("All Caps"));
    m_smallCapsButton = addToggle(IconProvider::StandardIcon::SmallCaps, tr("Small Caps"));
    toggleLayout->addStretch(1);
    m_rows->addFullWidthRow(toggleRow);

    // The buttons are checkable, so Qt has already flipped them by the time the
    // click is reported — the requested state is what `toggled` carries, not the
    // negation of what the button reads back as.
    const auto connectEffect = [this](AssetToggleButton* button, TextLayerEdit::Property property) {
        connect(button, &QAbstractButton::toggled, this,
            [this, property](bool checked) { emitToggleEdit(property, checked); });
    };
    connectEffect(m_boldButton, TextLayerEdit::Property::Bold);
    connectEffect(m_italicButton, TextLayerEdit::Property::Italic);
    connectEffect(m_underlineButton, TextLayerEdit::Property::Underline);
    connectEffect(m_strikethroughButton, TextLayerEdit::Property::Strikethrough);

    // All Caps and Small Caps are one setting with three states, so turning one
    // on turns the other off rather than leaving both lit.
    const auto connectCaps = [this](AssetToggleButton* button, TextCaps caps) {
        connect(button, &QAbstractButton::toggled, this, [this, caps](bool checked) {
            if (m_syncing) {
                return;
            }
            TextLayerEdit edit;
            edit.property = TextLayerEdit::Property::Caps;
            edit.enumValue = static_cast<int>(checked ? caps : TextCaps::None);
            emit editRequested(edit);
        });
    };
    connectCaps(m_allCapsButton, TextCaps::AllCaps);
    connectCaps(m_smallCapsButton, TextCaps::SmallCaps);

    applyTheme();
}

LayerTextCharacterEditor::~LayerTextCharacterEditor() = default;

void LayerTextCharacterEditor::setState(const State& state)
{
    m_state = state;
    m_syncing = true;

    // While the list is open the preview is driving the document, and writing
    // the previewed family back would move the list's own selection to sit
    // under the cursor.
    if (!m_fontDropdown->isPopupOpen()) {
        m_fontDropdown->setCurrentFamily(state.fontFamily.value_or(QString()));
    }
    setFieldValue(m_sizeInput, state.fontSize, m_sizeDirty);
    setFieldValue(m_trackingInput, state.tracking, m_trackingDirty);
    m_colorInput->setColor(state.color.value_or(QColor(0, 0, 0)));

    // A mixed value reads as off: one click then turns the whole selection on,
    // which is what Photoshop does with a mixed run.
    syncToggle(m_boldButton, state.bold.value_or(false));
    syncToggle(m_italicButton, state.italic.value_or(false));
    syncToggle(m_underlineButton, state.underline.value_or(false));
    syncToggle(m_strikethroughButton, state.strikethrough.value_or(false));
    const int caps = state.caps.value_or(static_cast<int>(TextCaps::None));
    syncToggle(m_allCapsButton, caps == static_cast<int>(TextCaps::AllCaps));
    syncToggle(m_smallCapsButton, caps == static_cast<int>(TextCaps::SmallCaps));

    m_syncing = false;
}

bool LayerTextCharacterEditor::isEditingValue() const
{
    return m_sizeInput->hasFocus() || m_trackingInput->hasFocus();
}

void LayerTextCharacterEditor::syncToggle(AssetToggleButton* button, bool on)
{
    if (!button) {
        return;
    }
    // The check state is what the next click toggles away from, so it has to
    // follow the document — setting only the visual state would leave the two
    // out of step and make every other click a no-op. Blocked so the sync is
    // not mistaken for the user pressing the button; the button's own
    // toggled -> setActive link is blocked with it, hence the explicit call.
    const QSignalBlocker blocker(button);
    button->setChecked(on);
    button->setActive(on);
}

void LayerTextCharacterEditor::bindNumericField(NumericInputField* field,
    TextLayerEdit::Property property, bool& dirtyFlag,
    std::function<std::optional<qreal>()> current)
{
    connect(
        field, &NumericInputField::valueChanged, this, [this, property, &dirtyFlag](double value) {
            if (m_syncing) {
                return;
            }
            dirtyFlag = true;
            emitNumberEdit(property, value, TextLayerEdit::Phase::Live);
        });
    connect(field, &QLineEdit::editingFinished, this,
        [this, field, property, &dirtyFlag, current = std::move(current)]() {
            if (m_syncing) {
                return;
            }
            if (!dirtyFlag) {
                // Nothing was typed — either focus merely passed through, or the
                // field was emptied. Either way the document's value is the
                // answer, so put it back rather than committing a stale number.
                m_syncing = true;
                setFieldValue(field, current(), dirtyFlag);
                m_syncing = false;
                return;
            }
            dirtyFlag = false;
            emitNumberEdit(property, field->value(), TextLayerEdit::Phase::Commit);
        });
}

void LayerTextCharacterEditor::setFieldValue(
    NumericInputField* field, const std::optional<qreal>& value, bool& dirtyFlag)
{
    if (!field) {
        return;
    }
    if (field->hasFocus() && dirtyFlag) {
        return; // a half-typed number belongs to the user until they commit it
    }
    dirtyFlag = false;
    if (value.has_value()) {
        field->setValue(*value);
        return;
    }
    // The characters disagree: show nothing rather than one of the values, so
    // the field cannot be mistaken for a setting that already applies to all of
    // them.
    field->clear();
}

void LayerTextCharacterEditor::emitNumberEdit(
    TextLayerEdit::Property property, qreal value, TextLayerEdit::Phase phase)
{
    if (m_syncing) {
        return;
    }
    TextLayerEdit edit;
    edit.property = property;
    edit.numberValue = value;
    edit.phase = phase;
    emit editRequested(edit);
}

void LayerTextCharacterEditor::emitToggleEdit(TextLayerEdit::Property property, bool value)
{
    if (m_syncing) {
        return;
    }
    TextLayerEdit edit;
    edit.property = property;
    edit.boolValue = value;
    emit editRequested(edit);
}

PropertyRowLayout* LayerTextCharacterEditor::rowLayout() const
{
    return m_rows.get();
}

void LayerTextCharacterEditor::applyTheme()
{
    auto& tm = ThemeManager::instance();
    const int fieldWidth = tm.scaled(70);
    m_sizeInput->setMinimumWidth(fieldWidth);
    m_trackingInput->setMinimumWidth(fieldWidth);
    update();
}

} // namespace ruwa::ui::widgets
