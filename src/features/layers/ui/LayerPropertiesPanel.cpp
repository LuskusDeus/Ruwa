// SPDX-License-Identifier: MPL-2.0

#include "LayerPropertiesPanel.h"

#include "features/layers/model/LayerModel.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/inputs/ToggleSwitch.h"

#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace ruwa::ui::workspace {

using namespace ruwa::core::layers;
using namespace ruwa::ui::widgets;

LayerPropertiesPanel::LayerPropertiesPanel(QWidget* parent)
    : DockPanel(tr("Layer Properties"), parent)
{
    setTranslatableTitle(QT_TR_NOOP("Layer Properties"));
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::Settings);
    setMinimumPanelSize(180, 120);
    setPreferredPanelSize(250, 180);
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

QWidget* LayerPropertiesPanel::createContent()
{
    m_contentWidget = new QWidget();

    auto* layout = new QVBoxLayout(m_contentWidget);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    m_hintLabel = new QLabel("Select the Background layer to edit canvas fill.", m_contentWidget);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_hintLabel);

    m_backgroundSection = new QWidget(m_contentWidget);
    auto* sectionLayout = new QVBoxLayout(m_backgroundSection);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(8);

    ColorInputButtonOptions colorOptions;
    colorOptions.boldLabel = false;
    colorOptions.hoverStrength = 0.06;
    colorOptions.baseHeight = 34;

    m_backgroundColorInput = new ColorInputButton(
        tr("Background Color"), QColor(255, 255, 255), colorOptions, m_backgroundSection);
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

    auto* transparentRow = new QWidget(m_backgroundSection);
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

    layout->addWidget(m_backgroundSection);
    layout->addStretch(1);

    onThemeChanged();
    refreshUi();
    return m_contentWidget;
}

void LayerPropertiesPanel::onThemeChanged()
{
    applyTheme();
}

void LayerPropertiesPanel::refreshUi()
{
    if (!m_contentWidget) {
        return;
    }
    if (!m_layerModel) {
        if (m_backgroundSection)
            m_backgroundSection->hide();
        if (m_hintLabel) {
            m_hintLabel->setText("Layer model is not connected.");
            m_hintLabel->show();
        }
        return;
    }

    auto* selected = m_layerModel->selectedLayer();
    const bool isBackground = (selected && selected->isBackground());

    if (!isBackground) {
        if (m_backgroundSection)
            m_backgroundSection->hide();
        if (m_hintLabel) {
            m_hintLabel->setText("Select the Background layer to edit canvas fill.");
            m_hintLabel->show();
        }
        return;
    }

    if (m_hintLabel) {
        m_hintLabel->hide();
    }
    if (m_backgroundSection) {
        m_backgroundSection->show();
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

void LayerPropertiesPanel::applyTheme()
{
    if (!m_contentWidget) {
        return;
    }
    const auto& c = colors();
    m_contentWidget->setStyleSheet(
        QString("background: %1; color: %2;").arg(c.surface.name(), c.textMuted.name()));
}

} // namespace ruwa::ui::workspace
