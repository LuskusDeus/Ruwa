// SPDX-License-Identifier: MPL-2.0

#include "LayerTextParagraphEditor.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/NumericInputField.h"
#include "shared/widgets/layout/PropertyRowLayout.h"

#include <memory>
#include <utility>

namespace ruwa::ui::widgets {

using ruwa::core::layers::TextAlignment;
using ruwa::core::layers::TextLayerEdit;
using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeManager;

namespace {

constexpr double kLeadingMinPercent = 10.0;
constexpr double kLeadingMaxPercent = 1000.0;
constexpr double kSpacingMax = 10000.0;

} // namespace

LayerTextParagraphEditor::LayerTextParagraphEditor(QWidget* parent)
    : QWidget(parent)
{
    m_rows = std::make_unique<PropertyRowLayout>(this);

    // --- Alignment ---
    auto& icons = IconProvider::instance();
    m_alignmentSelector = new SegmentedOptionSelector(this);
    m_alignmentSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::IconsOnly);
    m_alignmentSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_alignmentSelector->addOption(tr("Left"),
        icons.getIcon(IconProvider::StandardIcon::AlignTextLeft),
        static_cast<int>(TextAlignment::Left));
    m_alignmentSelector->addOption(tr("Center"),
        icons.getIcon(IconProvider::StandardIcon::AlignTextCenter),
        static_cast<int>(TextAlignment::Center));
    m_alignmentSelector->addOption(tr("Right"),
        icons.getIcon(IconProvider::StandardIcon::AlignTextRight),
        static_cast<int>(TextAlignment::Right));
    m_alignmentSelector->addOption(tr("Justify"),
        icons.getIcon(IconProvider::StandardIcon::AlignTextJustify),
        static_cast<int>(TextAlignment::Justify));
    m_alignmentSelector->setCurrentIndex(static_cast<int>(TextAlignment::Left), false);
    m_rows->addFullWidthRow(m_alignmentSelector);
    connect(m_alignmentSelector, &SegmentedOptionSelector::selectionChanged, this, [this](int) {
        if (m_syncing) {
            return;
        }
        TextLayerEdit edit;
        edit.property = TextLayerEdit::Property::Alignment;
        edit.enumValue = m_alignmentSelector->currentData().toInt();
        emit editRequested(edit);
    });

    // --- Leading, as a percentage of the font's own line height ---
    m_leadingInput = new NumericInputField(this);
    m_leadingInput->setRange(kLeadingMinPercent, kLeadingMaxPercent);
    m_leadingInput->setDecimals(0);
    m_leadingInput->setSingleStep(5.0);
    m_leadingInput->setSuffix(QStringLiteral("%"));
    m_rows->addRow(tr("Leading"), m_leadingInput);

    m_spaceBeforeInput = new NumericInputField(this);
    m_spaceBeforeInput->setRange(0.0, kSpacingMax);
    m_spaceBeforeInput->setDecimals(0);
    m_spaceBeforeInput->setSingleStep(1.0);
    m_spaceBeforeInput->setSuffix(QStringLiteral("px"));
    m_rows->addRow(tr("Space Before"), m_spaceBeforeInput);

    m_spaceAfterInput = new NumericInputField(this);
    m_spaceAfterInput->setRange(0.0, kSpacingMax);
    m_spaceAfterInput->setDecimals(0);
    m_spaceAfterInput->setSingleStep(1.0);
    m_spaceAfterInput->setSuffix(QStringLiteral("px"));
    m_rows->addRow(tr("Space After"), m_spaceAfterInput);

    // Leading is a percentage in the field and a multiplier in the document, so
    // it is the one field with a display scale.
    bindNumericField(
        m_leadingInput, TextLayerEdit::Property::LineHeight, m_leadingDirty,
        [this]() { return m_state.lineHeight; }, 100.0);
    bindNumericField(
        m_spaceBeforeInput, TextLayerEdit::Property::SpaceBefore, m_spaceBeforeDirty,
        [this]() { return m_state.spaceBefore; }, 1.0);
    bindNumericField(
        m_spaceAfterInput, TextLayerEdit::Property::SpaceAfter, m_spaceAfterDirty,
        [this]() { return m_state.spaceAfter; }, 1.0);

    applyTheme();
}

LayerTextParagraphEditor::~LayerTextParagraphEditor() = default;

void LayerTextParagraphEditor::setState(const State& state)
{
    m_state = state;
    m_syncing = true;
    m_alignmentSelector->setCurrentIndex(qBound(0, state.alignment, 3));
    // A field the user is part-way through belongs to them until they commit it.
    const auto push = [](NumericInputField* field, bool& dirtyFlag, qreal value) {
        if (field->hasFocus() && dirtyFlag) {
            return;
        }
        dirtyFlag = false;
        field->setValue(value);
    };
    push(m_leadingInput, m_leadingDirty, state.lineHeight * 100.0);
    push(m_spaceBeforeInput, m_spaceBeforeDirty, state.spaceBefore);
    push(m_spaceAfterInput, m_spaceAfterDirty, state.spaceAfter);
    m_syncing = false;
}

void LayerTextParagraphEditor::bindNumericField(NumericInputField* field,
    TextLayerEdit::Property property, bool& dirtyFlag, std::function<qreal()> current,
    qreal displayScale)
{
    connect(field, &NumericInputField::valueChanged, this,
        [this, property, &dirtyFlag, displayScale](double value) {
            if (m_syncing) {
                return;
            }
            dirtyFlag = true;
            emitNumberEdit(property, value / displayScale, TextLayerEdit::Phase::Live);
        });
    connect(field, &QLineEdit::editingFinished, this,
        [this, field, property, &dirtyFlag, displayScale, current = std::move(current)]() {
            if (m_syncing) {
                return;
            }
            if (!dirtyFlag) {
                // Nothing was typed — either focus passed through, or the field
                // was emptied. The document's value is the answer either way.
                m_syncing = true;
                field->setValue(current() * displayScale);
                m_syncing = false;
                return;
            }
            dirtyFlag = false;
            emitNumberEdit(property, field->value() / displayScale, TextLayerEdit::Phase::Commit);
        });
}

bool LayerTextParagraphEditor::isEditingValue() const
{
    return m_leadingInput->hasFocus() || m_spaceBeforeInput->hasFocus()
        || m_spaceAfterInput->hasFocus();
}

void LayerTextParagraphEditor::emitNumberEdit(
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

PropertyRowLayout* LayerTextParagraphEditor::rowLayout() const
{
    return m_rows.get();
}

void LayerTextParagraphEditor::applyTheme()
{
    auto& tm = ThemeManager::instance();
    const int fieldWidth = tm.scaled(70);
    m_leadingInput->setMinimumWidth(fieldWidth);
    m_spaceBeforeInput->setMinimumWidth(fieldWidth);
    m_spaceAfterInput->setMinimumWidth(fieldWidth);
    update();
}

} // namespace ruwa::ui::widgets
