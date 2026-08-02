// SPDX-License-Identifier: MPL-2.0

// LayerPreviewPopup.h
#ifndef RUWA_FEATURES_LAYERS_UI_LAYERPREVIEWPOPUP_H
#define RUWA_FEATURES_LAYERS_UI_LAYERPREVIEWPOPUP_H

#include "features/layers/model/LayerData.h"

#include <QColor>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QUuid>
#include <QVector>
#include <QWidget>

class QVariantAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Large hover preview for a layer (or its mask) thumbnail.
 *
 * A single shared, input-transparent window — the same glass surface the themed
 * tooltip uses — showing the layer content at a readable size next to a short
 * property summary.
 *
 * Timing deliberately rides on Qt's own tooltip clock: rows request the popup
 * from their QEvent::ToolTip handler, so the familiar wake-up delay and the
 * two second "fall asleep" grace (a short slip off the thumbnail does not
 * restart the wait) come for free.
 *
 * The popup never takes focus and never receives mouse events; it hides itself
 * on any interaction, on scrolling, and when its source row goes away.
 */
class LayerPreviewPopup final : public QWidget {
    Q_OBJECT

public:
    ~LayerPreviewPopup() override;

    /**
     * Shows (or re-targets) the preview for one thumbnail.
     * @param source            Row widget the pointer is hovering.
     * @param data              Layer being previewed. Read synchronously.
     * @param displayFrame      Canvas frame used for row thumbnails.
     * @param maskTarget        True when the mask slot is hovered.
     * @param anchorGlobalRect  Thumbnail rectangle in global coordinates.
     */
    static void showPreview(QWidget* source, const ruwa::core::layers::LayerData* data,
        const QRect& displayFrame, bool maskTarget, const QRect& anchorGlobalRect);

    /** Hides the popup if it is currently owned by @p source. */
    static void hidePreview(const QWidget* source);

    /** Hides the popup whatever its source is. */
    static void hideAny();

    /** True while the popup belongs to @p source (visible or fading in). */
    static bool isShowingFor(const QWidget* source);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct InfoRow {
        QString label;
        QString value;
        QColor valueColor;
    };

    explicit LayerPreviewPopup();

    static LayerPreviewPopup* instance();

    void present(QWidget* source, const ruwa::core::layers::LayerData* data,
        const QRect& displayFrame, bool maskTarget, const QRect& anchorGlobalRect);
    void dismiss(bool animated);

    // --- Content assembly ---
    QVector<InfoRow> buildInfoRows(
        const ruwa::core::layers::LayerData* data, bool maskTarget) const;
    QPixmap renderPreviewImage(const ruwa::core::layers::LayerData* data, const QRect& displayFrame,
        bool maskTarget, const QSize& boxSize) const;
    /// Lays out and rasterizes the whole panel body; reports the panel size it needs.
    QPixmap buildContentPixmap(const ruwa::core::layers::LayerData* data, const QRect& displayFrame,
        bool maskTarget, QSize& panelSize) const;

    QRect placePanel(const QRect& anchorGlobalRect, const QSize& panelSize, QWidget* source);
    void captureBackdrop(QWidget* source, const QRect& globalPanelRect);
    void setWatching(bool watching);

    QPixmap m_content;
    QPixmap m_previousContent;
    QPixmap m_backdrop;
    QVariantAnimation* m_presentationAnimation = nullptr;
    QVariantAnimation* m_geometryAnimation = nullptr;
    QVariantAnimation* m_contentFadeAnimation = nullptr;
    QPointer<QWidget> m_source;
    QUuid m_layerId;
    bool m_maskTarget = false;
    bool m_placedLeftOfAnchor = false;
    bool m_hiding = false;
    bool m_watching = false;
    qreal m_presentationProgress = 0.0;
    qreal m_contentFadeProgress = 1.0;
    int m_shadowMargin = 0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_FEATURES_LAYERS_UI_LAYERPREVIEWPOPUP_H
