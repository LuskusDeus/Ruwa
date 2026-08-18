// SPDX-License-Identifier: MPL-2.0

#include "LayerIdentityHeader.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/inputs/StyledInputField.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>

namespace ruwa::ui::widgets {

using ruwa::core::layers::LayerData;
using ruwa::core::layers::LayerType;
using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeManager;

namespace {

void makeWidgetTransparent(QWidget* widget)
{
    if (!widget) {
        return;
    }
    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setAttribute(Qt::WA_NoSystemBackground);
    widget->setAutoFillBackground(false);
}

} // namespace

LayerIdentityHeader::LayerIdentityHeader(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_nameField = new StyledInputField(QString(), StyledInputField::FieldType::Text, this);
    makeWidgetTransparent(m_nameField);
    // A header strip, not a form: the box hugs the name and carries the type
    // glyph inside it, the way the colour panel's hex field carries its "#".
    m_nameField->setDensity(StyledInputField::Density::Compact);
    m_nameField->setMaxLength(LayerData::kMaxNameLength);
    m_nameField->setPlaceholder(tr("No layer selected"));
    m_nameField->setEnabled(false); // the placeholder state, until a layer arrives
    layout->addWidget(m_nameField, 1);

    // Commit on Enter / focus loss, exactly like the brush editor's name field.
    // Escape is caught here because QLineEdit leaves it to the surrounding dialog.
    m_lineEdit = m_nameField->findChild<QLineEdit*>();
    if (m_lineEdit) {
        m_lineEdit->installEventFilter(this);
        connect(m_lineEdit, &QLineEdit::editingFinished, this, &LayerIdentityHeader::commitName);
    }

    applyTheme();
}

LayerIdentityHeader::~LayerIdentityHeader() = default;

IconProvider::StandardIcon LayerIdentityHeader::iconForLayerType(LayerType type)
{
    switch (type) {
    case LayerType::Raster:
        return IconProvider::StandardIcon::LayerRaster;
    case LayerType::Group:
        return IconProvider::StandardIcon::Folder;
    case LayerType::Adjustment:
        return IconProvider::StandardIcon::AdjustmentLayer;
    case LayerType::Vector:
        return IconProvider::StandardIcon::LayerVector;
    case LayerType::Mask:
        return IconProvider::StandardIcon::LayerMask;
    case LayerType::Background:
        return IconProvider::StandardIcon::LayerBackground;
    case LayerType::Smart:
        return IconProvider::StandardIcon::LayerSmart;
    case LayerType::Board:
        return IconProvider::StandardIcon::LayerBoard;
    case LayerType::Text:
        return IconProvider::StandardIcon::Text;
    }
    return IconProvider::StandardIcon::LayerRaster;
}

void LayerIdentityHeader::setLayer(const LayerData* layer)
{
    const bool hasLayer = (layer != nullptr);
    const auto id = hasLayer ? layer->id : ruwa::core::layers::LayerId();
    const auto type = hasLayer ? layer->type : LayerType::Raster;
    const QString name = hasLayer ? layer->name : QString();

    const bool layerChanged = (m_hasLayer != hasLayer) || (m_layerId != id);
    if (!layerChanged && m_layerType == type && m_name == name) {
        return;
    }

    // A field the user is typing in belongs to the layer it was focused on: only
    // a switch to a different layer may overwrite it. Notifications about an
    // unrelated property (visibility, opacity, …) must leave the text alone.
    const bool fieldHasFocus = m_lineEdit && m_lineEdit->hasFocus();

    m_hasLayer = hasLayer;
    m_layerId = id;
    m_layerType = type;
    m_name = name;

    if (m_nameField && (layerChanged || !fieldHasFocus)) {
        m_syncingField = true;
        m_nameField->setText(name);
        m_nameField->setEnabled(hasLayer);
        m_syncingField = false;
    }

    updateIcon();
}

void LayerIdentityHeader::setPlaceholderText(const QString& text)
{
    if (m_nameField) {
        m_nameField->setPlaceholder(text);
    }
}

void LayerIdentityHeader::setBandHeight(int height)
{
    const int clamped = qMax(1, height);
    if (m_requestedBandHeight == clamped) {
        return;
    }
    m_requestedBandHeight = clamped;
    updateBandHeight();
}

void LayerIdentityHeader::applyTheme()
{
    auto& tm = ThemeManager::instance();
    if (auto* box = qobject_cast<QHBoxLayout*>(layout())) {
        const int padH = tm.scaled(m_horizontalPadding);
        const int padV = tm.scaled(m_verticalPadding);
        box->setContentsMargins(padH, padV, padH, padV);
    }

    updateIcon();
    updateBandHeight();
    update();
}

void LayerIdentityHeader::updateBandHeight()
{
    auto& tm = ThemeManager::instance();
    int natural = 0;
    if (m_nameField) {
        // The field fixes its own height from font metrics; asking for less than
        // that would clip the input, so the request is a floor, not a cap.
        natural = m_nameField->sizeHint().height() + 2 * tm.scaled(m_verticalPadding);
    }
    setFixedHeight(qMax(m_requestedBandHeight, natural));
}

void LayerIdentityHeader::updateIcon()
{
    if (!m_nameField) {
        return;
    }
    if (m_hasLayer) {
        m_nameField->setLeadingIcon(iconForLayerType(m_layerType));
    } else {
        m_nameField->clearLeadingIcon();
    }
}

// ============================================================================
//   R E N A M E
// ============================================================================

void LayerIdentityHeader::commitName()
{
    if (m_syncingField || !m_hasLayer || !m_nameField) {
        return;
    }

    const QString newName = LayerData::clampedName(m_nameField->text().trimmed());
    if (newName.isEmpty() || newName == m_name) {
        // Nothing usable typed — put the current name back so the field never
        // shows a state the model does not have.
        m_syncingField = true;
        m_nameField->setText(m_name);
        m_syncingField = false;
        return;
    }

    emit renameRequested(m_layerId, newName);
}

bool LayerIdentityHeader::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress && m_nameField && watched == m_lineEdit) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            m_syncingField = true;
            m_nameField->setText(m_name);
            m_syncingField = false;
            m_nameField->clearInputFocus();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace ruwa::ui::widgets
