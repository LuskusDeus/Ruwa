// SPDX-License-Identifier: MPL-2.0

#include "LayerPositionEditor.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/AnchorGridSelector.h"
#include "shared/widgets/inputs/NumericInputField.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

using ruwa::ui::core::ThemeManager;

namespace {

// Wide enough for the "X" glyph, a few digits and the "px" unit inside one capsule.
constexpr int kCoordinateInputMinBaseWidth = 76;
constexpr int kColumnSpacing = 8;
constexpr double kCoordinateLimit = 1000000.0;

} // namespace

LayerPositionEditor::LayerPositionEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);

    // --- Left: the nine anchor cells ---
    // Its size is set in applyTheme() from the height of the column beside it,
    // so the two halves of the group always end flush.
    m_anchorGrid = new AnchorGridSelector(this);
    root->addWidget(m_anchorGrid, 0, Qt::AlignTop);

    // --- Right: axis over coordinates ---
    auto* column = new QVBoxLayout();
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(kColumnSpacing);

    m_axisSelector = new SegmentedOptionSelector(this);
    m_axisSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    // The selector defaults to hugging its labels; here it spans the column so
    // the three segments line up with the fields underneath.
    m_axisSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_axisSelector->addOption(tr("Both"), QIcon(), static_cast<int>(Axis::Both));
    m_axisSelector->addOption(tr("X"), QIcon(), static_cast<int>(Axis::X));
    m_axisSelector->addOption(tr("Y"), QIcon(), static_cast<int>(Axis::Y));
    m_axisSelector->setCurrentIndex(static_cast<int>(Axis::Both), false);
    column->addWidget(m_axisSelector);

    // The axis name lives inside the capsule, the way the colour panel's hex
    // field carries its '#', so the fields need no label column beside them.
    const auto makeCoordinateField = [this](const QString& axisGlyph) {
        auto* field = new NumericInputField(this);
        field->setRange(-kCoordinateLimit, kCoordinateLimit);
        field->setDecimals(0);
        field->setSingleStep(1.0);
        field->setPrefix(axisGlyph);
        field->setSuffix(QStringLiteral("px"));
        field->setValue(0.0);
        return field;
    };

    m_xInput = makeCoordinateField(tr("X"));
    m_yInput = makeCoordinateField(tr("Y"));
    column->addWidget(m_xInput);
    column->addWidget(m_yInput);
    column->addStretch(1);

    root->addLayout(column, 1);

    connect(m_anchorGrid, &AnchorGridSelector::anchorClicked, this,
        [this](int index) { emit anchorApplied(index, axis()); });
    connect(m_axisSelector, &SegmentedOptionSelector::selectionChanged, this,
        [this](int) { syncInputsToAxis(); });

    // Committed on Enter or focus-out, not on every keystroke: a coordinate edit
    // moves real pixels through the transform pipeline and lands in undo, so it
    // must fire once per intent rather than once per digit typed.
    const auto onCoordinateCommitted = [this]() {
        if (m_syncingFields) {
            return;
        }
        const QPointF edited = position();
        if (edited == m_committedPosition) {
            return; // focus left a field the user never changed
        }
        m_committedPosition = edited;
        emit positionEdited(edited);
    };
    connect(m_xInput, &QLineEdit::editingFinished, this, onCoordinateCommitted);
    connect(m_yInput, &QLineEdit::editingFinished, this, onCoordinateCommitted);

    // Dragging a coordinate is the one gesture where waiting for the commit
    // reads as broken: the user is pointing at where the content should go, so
    // it has to follow the pointer. Every drag step is therefore reported at
    // once, bracketed by positionDragChanged() so the host can still record the
    // whole gesture as a single undo step. Typing keeps the commit-on-Enter
    // contract — a half-typed "12" on the way to "1200" must move nothing.
    const auto onCoordinateDragged = [this]() {
        if (m_syncingFields || !m_dragging) {
            return;
        }
        const QPointF edited = position();
        if (edited == m_committedPosition) {
            return;
        }
        m_committedPosition = edited;
        emit positionEdited(edited);
    };
    connect(m_xInput, &NumericInputField::valueChanged, this, onCoordinateDragged);
    connect(m_yInput, &NumericInputField::valueChanged, this, onCoordinateDragged);

    // endScrub() emits scrubbingChanged(false) and then editingFinished(), so
    // the trailing commit finds the position already reported and stays quiet.
    const auto onScrubbingChanged = [this](bool scrubbing) {
        if (m_dragging == scrubbing) {
            return;
        }
        m_dragging = scrubbing;
        emit positionDragChanged(scrubbing);
    };
    connect(m_xInput, &NumericInputField::scrubbingChanged, this, onScrubbingChanged);
    connect(m_yInput, &NumericInputField::scrubbingChanged, this, onScrubbingChanged);

    applyTheme();
    syncInputsToAxis();
}

void LayerPositionEditor::setPosition(const QPointF& position)
{
    m_syncingFields = true;
    m_xInput->setValue(position.x());
    m_yInput->setValue(position.y());
    m_syncingFields = false;
    // The fields now agree with the document, so a later focus-out that changed
    // nothing must not read as an edit.
    m_committedPosition = this->position();
}

QPointF LayerPositionEditor::position() const
{
    return QPointF(m_xInput->value(), m_yInput->value());
}

LayerPositionEditor::Axis LayerPositionEditor::axis() const
{
    const int index = qBound(0, m_axisSelector->currentIndex(), 2);
    return static_cast<Axis>(index);
}

void LayerPositionEditor::setAxis(Axis axis)
{
    m_axisSelector->setCurrentIndex(static_cast<int>(axis));
    syncInputsToAxis();
}

int LayerPositionEditor::anchorIndex() const
{
    return m_anchorGrid->currentIndex();
}

void LayerPositionEditor::setAnchorIndex(int index)
{
    m_anchorGrid->setCurrentIndex(index);
}

void LayerPositionEditor::setContentAvailable(bool available)
{
    if (m_contentAvailable == available) {
        return;
    }
    m_contentAvailable = available;
    m_anchorGrid->setEnabled(available);
    m_axisSelector->setEnabled(available);
    syncInputsToAxis();
}

bool LayerPositionEditor::isEditingCoordinates() const
{
    return m_xInput->hasFocus() || m_yInput->hasFocus();
}

void LayerPositionEditor::syncInputsToAxis()
{
    // The axis scopes the whole group, so the coordinate it leaves alone reads
    // as inactive instead of silently accepting edits an anchor click ignores.
    const Axis current = axis();
    m_xInput->setEnabled(m_contentAvailable && current != Axis::Y);
    m_yInput->setEnabled(m_contentAvailable && current != Axis::X);
}

void LayerPositionEditor::applyTheme()
{
    auto& tm = ThemeManager::instance();
    const int minFieldWidth = tm.scaled(kCoordinateInputMinBaseWidth);
    m_xInput->setMinimumWidth(minFieldWidth);
    m_yInput->setMinimumWidth(minFieldWidth);

    // The square is as tall as the three controls beside it, measured rather
    // than guessed at — the capsules take their height from font metrics, so a
    // hard-coded side would drift apart from them on any theme or scale change.
    const int columnHeight = m_axisSelector->sizeHint().height() + m_xInput->sizeHint().height()
        + m_yInput->sizeHint().height() + 2 * kColumnSpacing;
    m_anchorGrid->setSideLength(columnHeight);

    update();
}

} // namespace ruwa::ui::widgets
