// SPDX-License-Identifier: MPL-2.0

#include "LayerPropertiesPanel.h"

#include "features/layers/model/LayerModel.h"
#include "features/layers/ui/LayerIdentityHeader.h"
#include "features/layers/ui/LayerPositionEditor.h"
#include "features/layers/ui/LayerTextCharacterEditor.h"
#include "features/layers/ui/LayerTextParagraphEditor.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/layout/CollapsibleSection.h"
#include "shared/widgets/layout/PropertyRowLayout.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shell/docking/widgets/DockPanelTitleBar.h"
// The transform system already resolves a layer's pixel-tight document bounds
// for every layer kind; the panel reads the position through that instead of
// growing a second answer to the same question.
#include "features/transform/TransformGeometry.h"

#include <optional>

#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace ruwa::ui::workspace {

using namespace ruwa::core::layers;
using namespace ruwa::ui::widgets;

namespace {

/**
 * @brief Accumulates the document-space box of everything @p layer draws.
 *
 * The per-layer answer comes from the transform system's resolver: pixel-tight
 * tile bounds for a raster layer, the transformed AABB for smart and text
 * layers. Only the walk is written out here, because the transform system's own
 * walk also gates on editability — it skips hidden, locked and background
 * layers, which is right for transforming them and wrong for reading a position
 * they still have.
 *
 * Hidden descendants of a group are skipped: they draw nothing, so letting them
 * stretch the group's box would report a position nothing occupies. The layer
 * the user selected is measured whether or not it is visible.
 */
void accumulateContentBounds(const LayerData* layer, bool isSelectedLayer, aether::Rect& bounds)
{
    if (!layer || (!isSelectedLayer && !layer->visible)) {
        return;
    }

    if (const std::optional<aether::Rect> own = aether::transformBoundsForLayer(layer)) {
        bounds = aether::unionTransformBounds(bounds, *own);
    }
    // Groups resolve to nothing of their own; they are the union of what is
    // inside them.
    for (const auto& child : layer->children) {
        accumulateContentBounds(child.get(), /*isSelectedLayer=*/false, bounds);
    }
}

std::optional<aether::Rect> layerContentBounds(const LayerData* layer)
{
    aether::Rect bounds {};
    accumulateContentBounds(layer, /*isSelectedLayer=*/true, bounds);
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return std::nullopt;
    }
    return bounds;
}

/// Where a content box of this size would sit if aligned to anchor cell
/// @p index: flush against the document edge for an outer cell, centred on the
/// axis for a middle one.
QPointF anchoredOrigin(int index, const aether::Rect& bounds, const QSize& canvasSize)
{
    const auto axisOrigin = [](int cell, double freeSpace) {
        if (cell == 0) {
            return 0.0;
        }
        return cell == 1 ? freeSpace * 0.5 : freeSpace;
    };
    return QPointF(axisOrigin(index % 3, canvasSize.width() - static_cast<double>(bounds.width)),
        axisOrigin(index / 3, canvasSize.height() - static_cast<double>(bounds.height)));
}

/// The anchor cell the content currently occupies, or -1 when it sits somewhere
/// none of the nine describes. Half a pixel of slack, because centring on an
/// odd amount of free space lands on a half-pixel that the transform pipeline
/// then snaps — without the slack, clicking an anchor could fail to light up
/// the very cell that was just applied.
int anchorIndexForOrigin(const QPointF& origin, const aether::Rect& bounds, const QSize& canvasSize)
{
    constexpr double kTolerance = 0.5;
    for (int index = 0; index < 9; ++index) {
        const QPointF target = anchoredOrigin(index, bounds, canvasSize);
        if (qAbs(target.x() - origin.x()) <= kTolerance
            && qAbs(target.y() - origin.y()) <= kTolerance) {
            return index;
        }
    }
    return -1;
}

} // namespace

LayerPropertiesPanel::LayerPropertiesPanel(QWidget* parent)
    : DockPanel(tr("Layer Properties"), parent)
{
    setTranslatableTitle(QT_TR_NOOP("Layer Properties"));
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::Settings);
    setMinimumPanelSize(200, 150);
    setPreferredPanelSize(260, 200);
    setClosable(true);
    setFloatable(true);
    setMovable(true);
}

LayerPropertiesPanel::~LayerPropertiesPanel() = default;

void LayerPropertiesPanel::setLayerModel(LayerModel* model)
{
    if (m_layerModel == model) {
        return;
    }

    if (m_layerModel) {
        disconnect(m_layerModel, nullptr, this, nullptr);
    }
    m_layerModel = model;
    if (m_layerModel) {
        connect(m_layerModel, &LayerModel::selectionChanged, this,
            [this](const LayerId&) { refreshUi(); });
        connect(m_layerModel, &LayerModel::layerDataChanged, this,
            [this](const LayerId&) { refreshUi(); });
        connect(m_layerModel, &LayerModel::layersChanged, this, &LayerPropertiesPanel::refreshUi);
    }
    refreshUi();
}

int LayerPropertiesPanel::headerBandHeight() const
{
    // Two title bars tall. Read from the bar itself so a theme or chrome change
    // to the title height carries into the header instead of drifting from it.
    const int barHeight = titleBar() ? titleBar()->barHeight() : 19;
    return qMax(1, barHeight) * 2;
}

QWidget* LayerPropertiesPanel::createContent()
{
    // --- Subtitle: layer identity (type glyph + editable name) ---
    m_identityHeader = new LayerIdentityHeader();
    m_identityHeader->setBandHeight(headerBandHeight());
    connect(m_identityHeader, &LayerIdentityHeader::renameRequested, this,
        &LayerPropertiesPanel::onHeaderRenameRequested);
    setSubtitleContentMargins(0, 0, 0, 0);
    setSubtitleContentSpacing(0);
    setSubtitleWidget(m_identityHeader);

    // The groups stack downwards without limit — a collapsible section keeps its
    // content at natural height — so the column has to scroll rather than
    // squeeze the groups into whatever the panel happens to be.
    m_rootWidget = new QWidget();
    m_rootWidget->setObjectName(QStringLiteral("layerPropertiesRoot"));
    m_rootWidget->setAttribute(Qt::WA_StyledBackground, true);

    auto* rootLayout = new QVBoxLayout(m_rootWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scrollArea = new SmoothScrollArea(m_rootWidget);
    m_scrollArea->setFillBackground(false);
    m_scrollArea->setScrollBarTransparentTrack(true);
    m_scrollArea->setScrollBarAlwaysReserved(false);
    m_scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));
    rootLayout->addWidget(m_scrollArea);
    if (auto* viewport = m_scrollArea->viewport()) {
        viewport->setAttribute(Qt::WA_TranslucentBackground, true);
        viewport->setAutoFillBackground(false);
        viewport->setStyleSheet(QStringLiteral("background: transparent;"));
    }

    m_contentWidget = new QWidget(m_scrollArea);
    m_contentWidget->setObjectName(QStringLiteral("layerPropertiesContent"));
    m_contentWidget->setAttribute(Qt::WA_TranslucentBackground, true);
    m_contentWidget->setAutoFillBackground(false);

    m_contentLayout = new QVBoxLayout(m_contentWidget);
    m_contentLayout->setContentsMargins(10, 10, 10, 10);
    m_contentLayout->setSpacing(6);
    m_scrollArea->setWidget(m_contentWidget);

    m_hintLabel = new QLabel(tr("Select a layer to edit its properties."), m_contentWidget);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_contentLayout->addWidget(m_hintLabel);

    // --- Group: position ---
    m_positionEditor = new LayerPositionEditor();
    connect(m_positionEditor, &LayerPositionEditor::positionEdited, this,
        &LayerPropertiesPanel::onPositionEdited);
    connect(m_positionEditor, &LayerPositionEditor::anchorApplied, this,
        &LayerPropertiesPanel::onAnchorApplied);
    m_positionGroup = addGroup(tr("Position"), m_positionEditor);

    // --- Group: background fill ---
    auto* backgroundContent = new QWidget();
    auto* sectionLayout = new QVBoxLayout(backgroundContent);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(8);

    ColorInputButtonOptions colorOptions;
    colorOptions.boldLabel = false;
    colorOptions.hoverStrength = 0.06;
    colorOptions.baseHeight = 34;

    m_backgroundColorInput = new ColorInputButton(
        tr("Background Color"), QColor(255, 255, 255), colorOptions, backgroundContent);
    connect(m_backgroundColorInput, &ColorInputButton::colorPickerRequested, this,
        &LayerPropertiesPanel::onBackgroundColorRequested);
    connect(
        m_backgroundColorInput, &ColorInputButton::colorChanged, this, [this](const QColor& color) {
            if (m_syncingUi || !m_layerModel) {
                return;
            }
            auto* layer = m_layerModel->selectedLayer();
            if (!layer || !layer->isBackground()) {
                return;
            }
            m_layerModel->setLayerBackgroundColor(layer->id, color);
        });
    sectionLayout->addWidget(m_backgroundColorInput);

    auto* transparentRow = new QWidget(backgroundContent);
    auto* transparentLayout = new QHBoxLayout(transparentRow);
    transparentLayout->setContentsMargins(0, 0, 0, 0);
    transparentLayout->setSpacing(8);
    auto* transparentLabel = new QLabel(tr("Transparent"), transparentRow);
    transparentLayout->addWidget(transparentLabel);
    transparentLayout->addStretch(1);
    m_transparentSwitch = new ToggleSwitch(transparentRow);
    connect(m_transparentSwitch, &ToggleSwitch::toggled, this,
        &LayerPropertiesPanel::onTransparentToggled);
    transparentLayout->addWidget(m_transparentSwitch);
    sectionLayout->addWidget(transparentRow);

    m_backgroundGroup = addGroup(tr("Background"), backgroundContent);

    // --- Groups: text (Character over Paragraph, as in Photoshop) ---
    m_characterEditor = new LayerTextCharacterEditor();
    connect(m_characterEditor, &LayerTextCharacterEditor::editRequested, this,
        &LayerPropertiesPanel::textLayerEditRequested);
    connect(m_characterEditor, &LayerTextCharacterEditor::colorPickerRequested, this,
        &LayerPropertiesPanel::colorPickerRequested);
    m_characterGroup = addGroup(tr("Character"), m_characterEditor);

    m_paragraphEditor = new LayerTextParagraphEditor();
    connect(m_paragraphEditor, &LayerTextParagraphEditor::editRequested, this,
        &LayerPropertiesPanel::textLayerEditRequested);
    m_paragraphGroup = addGroup(tr("Paragraph"), m_paragraphEditor);

    m_contentLayout->addStretch(1);

    onThemeChanged();
    refreshUi();
    return m_rootWidget;
}

CollapsibleSection* LayerPropertiesPanel::addGroup(const QString& title, QWidget* content)
{
    auto* group = new CollapsibleSection(title, m_contentWidget);
    group->setContentWidget(content);
    // Groups are added before the trailing stretch, so appending is enough
    // while createContent() builds the column top to bottom.
    m_contentLayout->addWidget(group);
    return group;
}

void LayerPropertiesPanel::onThemeChanged()
{
    applyTheme();
}

void LayerPropertiesPanel::refreshUi()
{
    if (m_identityHeader) {
        m_identityHeader->setLayer(m_layerModel ? m_layerModel->selectedLayer() : nullptr);
    }

    if (!m_contentWidget) {
        return;
    }

    auto* selected = m_layerModel ? m_layerModel->selectedLayer() : nullptr;

    // Without a layer there is nothing any group can edit, so the panel falls
    // back to the hint alone rather than showing empty controls.
    if (!selected) {
        if (m_positionGroup)
            m_positionGroup->hide();
        if (m_backgroundGroup)
            m_backgroundGroup->hide();
        if (m_characterGroup)
            m_characterGroup->hide();
        if (m_paragraphGroup)
            m_paragraphGroup->hide();
        if (m_hintLabel) {
            m_hintLabel->setText(m_layerModel ? tr("Select a layer to edit its properties.")
                                              : tr("Layer model is not connected."));
            m_hintLabel->show();
        }
        return;
    }

    if (m_hintLabel) {
        m_hintLabel->hide();
    }
    if (m_positionGroup) {
        m_positionGroup->show();
    }
    refreshPositionFromContent();

    // Character and Paragraph belong to text layers only.
    const bool isText = selected->isText() && selected->textData != nullptr;
    if (m_characterGroup) {
        m_characterGroup->setVisible(isText);
    }
    if (m_paragraphGroup) {
        m_paragraphGroup->setVisible(isText);
    }
    if (isText) {
        refreshTextGroups();
    }

    // Canvas fill belongs to the Background layer only; every other type hides
    // that group instead of showing controls that would do nothing.
    const bool isBackground = selected->isBackground();
    if (m_backgroundGroup) {
        m_backgroundGroup->setVisible(isBackground);
    }
    if (!isBackground) {
        return;
    }

    m_syncingUi = true;
    if (m_backgroundColorInput) {
        m_backgroundColorInput->setColor(selected->backgroundColor);
        m_backgroundColorInput->setEnabled(!selected->backgroundTransparent);
    }
    if (m_transparentSwitch) {
        QSignalBlocker blocker(m_transparentSwitch);
        m_transparentSwitch->setChecked(
            selected->backgroundTransparent, ToggleSwitch::TransitionMode::Instant);
    }
    m_syncingUi = false;
}

void LayerPropertiesPanel::setCanvasSizeProvider(std::function<std::optional<QSize>()> provider)
{
    m_canvasSizeProvider = std::move(provider);
}

void LayerPropertiesPanel::setTextSelectionProvider(
    std::function<std::optional<std::pair<int, int>>()> provider)
{
    m_textSelectionProvider = std::move(provider);
}

// ============================================================================
//   T E X T   G R O U P S
// ============================================================================

bool LayerPropertiesPanel::ownsTextColorInput(const QWidget* widget) const
{
    return m_characterEditor && m_characterEditor->ownsColorInput(widget);
}

void LayerPropertiesPanel::setTextColorPickerOpen(bool open)
{
    if (m_characterEditor) {
        m_characterEditor->setColorPickerOpen(open);
    }
}

void LayerPropertiesPanel::refreshTextGroups()
{
    if (!m_characterEditor || !m_paragraphEditor) {
        return;
    }
    auto* selected = m_layerModel ? m_layerModel->selectedLayer() : nullptr;
    if (!selected || !selected->isText() || !selected->textData) {
        return;
    }

    const TextLayerData& textData = *selected->textData;

    // Inside an open editing session the groups describe the selected
    // characters, exactly like the popup they replace; outside one there is no
    // caret, so they describe the layer.
    const std::optional<std::pair<int, int>> selection
        = m_textSelectionProvider ? m_textSelectionProvider() : std::nullopt;
    const std::pair<int, int> range
        = selection.value_or(std::pair<int, int> { 0, static_cast<int>(textData.text.size()) });

    LayerTextCharacterEditor::State state;
    const auto uniform = [&](auto reader) {
        return uniformTextStyleValue(textData, range.first, range.second, reader);
    };
    state.fontFamily = uniform([](const TextCharStyle& style) { return style.fontFamily; });
    state.fontSize = uniform([](const TextCharStyle& style) { return style.fontSize; });
    state.color = uniform([](const TextCharStyle& style) { return style.color; });
    state.bold = uniform([](const TextCharStyle& style) { return style.bold; });
    state.italic = uniform([](const TextCharStyle& style) { return style.italic; });
    state.underline = uniform([](const TextCharStyle& style) { return style.underline; });
    state.strikethrough = uniform([](const TextCharStyle& style) { return style.strikethrough; });
    state.tracking = uniform([](const TextCharStyle& style) { return style.tracking; });
    if (const auto caps = uniform([](const TextCharStyle& style) { return style.caps; })) {
        state.caps = static_cast<int>(*caps);
    }
    m_characterEditor->setState(state);

    LayerTextParagraphEditor::State paragraph;
    paragraph.alignment = static_cast<int>(textData.alignment);
    paragraph.lineHeight = textData.lineHeight;
    paragraph.spaceBefore = textData.spaceBefore;
    paragraph.spaceAfter = textData.spaceAfter;
    m_paragraphEditor->setState(paragraph);
}

void LayerPropertiesPanel::refreshPositionFromContent()
{
    syncPositionFromContent(/*force=*/false);
}

void LayerPropertiesPanel::syncPositionFromContent(bool force)
{
    if (!m_positionEditor) {
        return;
    }

    auto* selected = m_layerModel ? m_layerModel->selectedLayer() : nullptr;
    const LayerId selectedId = selected ? selected->id : LayerId();
    if (m_shownContentOriginLayer != selectedId) {
        m_shownContentOriginLayer = selectedId;
        m_hasShownContentOrigin = false;
    }

    const std::optional<aether::Rect> bounds = layerContentBounds(selected);

    m_positionEditor->setContentAvailable(bounds.has_value());
    if (!bounds.has_value()) {
        m_hasShownContentOrigin = false;
        m_positionEditor->setAnchorIndex(-1);
        return;
    }

    // The layer's position is the top-left of what it actually draws.
    const QPointF origin(bounds->left(), bounds->top());

    // A cell lights up only while the content genuinely sits in it — an
    // arbitrary position matches none of the nine, and the grid says so.
    const std::optional<QSize> canvasSize
        = m_canvasSizeProvider ? m_canvasSizeProvider() : std::nullopt;
    m_positionEditor->setAnchorIndex(
        canvasSize.has_value() ? anchorIndexForOrigin(origin, *bounds, *canvasSize) : -1);

    const bool moved = !m_hasShownContentOrigin || origin != m_shownContentOrigin;

    // The cache is the reference the next edit measures its delta against, so it
    // tracks the document even when the fields are not refreshed below.
    m_shownContentOrigin = origin;
    m_hasShownContentOrigin = true;

    if (!moved && !force) {
        return; // content changed, its bounds did not — nothing to restate
    }
    // A field the user is typing in belongs to them until they commit it, the
    // same rule the identity header applies to a half-typed layer name.
    if (!force && m_positionEditor->isEditingCoordinates()) {
        return;
    }
    m_positionEditor->setPosition(origin);
}

// ============================================================================
//   P O S I T I O N   E D I T S
// ============================================================================

void LayerPropertiesPanel::requestMove(const QPointF& delta)
{
    if (delta.isNull()) {
        return;
    }
    emit moveContentRequested(delta);
    // Whatever the document made of the request is the truth; re-read it even
    // if a field still has focus, so the display cannot keep a value that was
    // clamped or refused.
    syncPositionFromContent(/*force=*/true);
}

void LayerPropertiesPanel::onPositionEdited(const QPointF& origin)
{
    if (!m_hasShownContentOrigin) {
        return; // nothing on this layer to measure a move against
    }
    requestMove(origin - m_shownContentOrigin);
}

void LayerPropertiesPanel::onAnchorApplied(int anchorIndex, LayerPositionEditor::Axis axis)
{
    auto* selected = m_layerModel ? m_layerModel->selectedLayer() : nullptr;
    const std::optional<aether::Rect> bounds = layerContentBounds(selected);
    if (!bounds.has_value()) {
        return;
    }

    const std::optional<QSize> canvasSize
        = m_canvasSizeProvider ? m_canvasSizeProvider() : std::nullopt;
    if (!canvasSize.has_value()) {
        return; // infinite canvas: no box to align against
    }

    const QPointF target = anchoredOrigin(anchorIndex, *bounds, *canvasSize);
    QPointF delta(target.x() - bounds->left(), target.y() - bounds->top());
    // The axis picker scopes the alignment: X or Y leaves the other coordinate
    // exactly where the user had put it.
    if (axis == LayerPositionEditor::Axis::X) {
        delta.setY(0.0);
    } else if (axis == LayerPositionEditor::Axis::Y) {
        delta.setX(0.0);
    }
    requestMove(delta);
}

void LayerPropertiesPanel::onBackgroundColorRequested(const QColor& initialColor)
{
    if (!m_layerModel) {
        return;
    }
    auto* layer = m_layerModel->selectedLayer();
    if (!layer || !layer->isBackground()) {
        return;
    }
    emit colorPickerRequested(initialColor, m_backgroundColorInput);
}

void LayerPropertiesPanel::onTransparentToggled(bool checked)
{
    if (m_syncingUi || !m_layerModel) {
        return;
    }
    auto* layer = m_layerModel->selectedLayer();
    if (!layer || !layer->isBackground()) {
        return;
    }
    m_layerModel->setLayerBackgroundTransparent(layer->id, checked);
    refreshUi();
}

void LayerPropertiesPanel::onHeaderRenameRequested(const LayerId& id, const QString& name)
{
    if (!m_layerModel) {
        return;
    }
    // Same path the layer list's inline rename takes, so both stay equally
    // (un)undoable rather than diverging here.
    m_layerModel->setLayerName(id, name);
}

void LayerPropertiesPanel::applyTheme()
{
    const auto& c = colors();

    if (m_identityHeader) {
        // The header paints c.surface itself; keep the band behind it in step.
        setSubtitleBackground(c.surface);
        m_identityHeader->setBandHeight(headerBandHeight());
        m_identityHeader->applyTheme();
    }

    if (m_positionEditor) {
        m_positionEditor->applyTheme();
    }
    if (m_characterEditor) {
        m_characterEditor->applyTheme();
    }
    if (m_paragraphEditor) {
        m_paragraphEditor->applyTheme();
    }
    // One caption column across the groups: on its own each group would size
    // that column to its own longest caption, and the panel would read as
    // several unrelated forms stacked on top of each other.
    if (m_characterEditor && m_paragraphEditor) {
        alignPropertyColumns({ m_characterEditor->rowLayout(), m_paragraphEditor->rowLayout() });
    }

    if (!m_contentWidget) {
        return;
    }
    // The surface is painted by the root, behind the scroll viewport, so the
    // strip below a short column is filled too. Scoped by object name and to
    // labels: a bare `background:` rule propagates to every descendant, which
    // would paint a plate behind the self-drawn inputs (numeric fields, anchor
    // grid, group headers).
    if (m_rootWidget) {
        m_rootWidget->setStyleSheet(
            QString("QWidget#layerPropertiesRoot { background: %1; }").arg(c.surface.name()));
    }
    m_contentWidget->setStyleSheet(
        QString("QWidget#layerPropertiesContent { background: transparent; }"
                "QWidget#layerPropertiesContent QLabel { background: transparent; color: %1; }")
            .arg(c.textMuted.name()));
}

} // namespace ruwa::ui::workspace
