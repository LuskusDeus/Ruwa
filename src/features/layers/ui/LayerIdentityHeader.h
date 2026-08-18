// SPDX-License-Identifier: MPL-2.0

// LayerIdentityHeader.h
#ifndef RUWA_UI_WIDGETS_LAYERIDENTITYHEADER_H
#define RUWA_UI_WIDGETS_LAYERIDENTITYHEADER_H

#include "features/layers/model/LayerData.h"
#include "shared/resources/IconProvider.h"

#include <QString>
#include <QWidget>

class QLineEdit;

namespace ruwa::ui::widgets {

class StyledInputField;

/**
 * @brief Identity strip for a single layer: type glyph + name field.
 *
 * Built to sit in a DockPanel's subtitle band (setSubtitleWidget), which is why
 * it keeps to one row and takes its height from the caller.
 *
 * The name uses the app's standard StyledInputField — the same widget the brush
 * editor renames a brush with — so hover and focus read exactly like every other
 * text field instead of inventing a highlight of their own.
 *
 * The widget never writes to the model. It reports a finished edit through
 * renameRequested() and waits to be told the new name, so the owner keeps the
 * single path to LayerModel::setLayerName().
 */
class LayerIdentityHeader : public QWidget {
    Q_OBJECT

public:
    explicit LayerIdentityHeader(QWidget* parent = nullptr);
    ~LayerIdentityHeader() override;

    /// The glyph standing for a layer type in the header and anywhere else that
    /// needs one. Group / Adjustment / Mask / Text reuse the icons those types
    /// already carry in the layer list.
    static ruwa::ui::core::IconProvider::StandardIcon iconForLayerType(
        ruwa::core::layers::LayerType type);

    /// Show a layer. Passing nullptr switches to the placeholder state.
    void setLayer(const ruwa::core::layers::LayerData* layer);

    /// Text shown in the empty, disabled field when no layer is set.
    void setPlaceholderText(const QString& text);

    /// Requested height of the band. The field's own metrics win when they need
    /// more room, so a compact request cannot clip the input.
    void setBandHeight(int height);
    int bandHeight() const { return m_requestedBandHeight; }

    /// Re-read fonts, colours and metrics from the theme.
    void applyTheme();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    /// The user finished editing with a name that differs from the current one.
    /// Already trimmed and clamped to LayerData::kMaxNameLength.
    void renameRequested(const ruwa::core::layers::LayerId& id, const QString& name);

private:
    void updateIcon();
    void updateBandHeight();
    void commitName();

private:
    ruwa::core::layers::LayerId m_layerId;
    ruwa::core::layers::LayerType m_layerType = ruwa::core::layers::LayerType::Raster;
    QString m_name;
    bool m_hasLayer = false;
    /// Set while pushing model state into the field, so the field's own signals
    /// cannot be mistaken for user edits.
    bool m_syncingField = false;

    StyledInputField* m_nameField = nullptr;
    /// The field's inner editor. Kept because the focus and key handling below
    /// need the exact widget, not "whatever the field last focused".
    QLineEdit* m_lineEdit = nullptr;

    int m_requestedBandHeight = 38;
    // Unscaled metrics; scaled through ThemeManager on every use.
    int m_horizontalPadding = 8;
    int m_verticalPadding = 4;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_LAYERIDENTITYHEADER_H
