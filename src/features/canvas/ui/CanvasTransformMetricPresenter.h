// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   T R A N S F O R M   M E T R I C   P R E S E N T E R
// ==========================================================================
// Application-owned QWidget presentation of transform metric facts
// (plan 7.15.1 / 7.27.6). The engine publishes TransformPresentationState —
// snap labels anchored in document space, live drag readout segments and the
// viewport-local drag anchor — and this presenter draws the capsules with the
// shared CanvasMetricLabelOverlay. The engine no longer constructs any
// QWidget, and the presenter never sees the pointer-source interface.
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASTRANSFORMMETRICPRESENTER_H
#define RUWA_UI_WORKSPACE_CANVASTRANSFORMMETRICPRESENTER_H

#include "features/canvas/engine/CanvasEngineTypes.h"

#include <QList>
#include <QObject>

#include <functional>
#include <optional>
#include <vector>

namespace ruwa::ui::widgets {
class CanvasMetricLabelOverlay;
struct MetricSegment;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

class CanvasTransformMetricPresenter final : public QObject {
    Q_OBJECT

public:
    /// @param hostParent QWidget the capsules are parented to (the viewport
    ///     host); lifetime is Qt parent ownership.
    CanvasTransformMetricPresenter(QWidget* hostParent, QObject* parent = nullptr);
    ~CanvasTransformMetricPresenter() override;

    /// Map a document point to viewport-local coordinates (snap labels).
    using DocumentToViewport = std::function<QPointF(const QPointF&)>;
    void setDocumentToViewport(DocumentToViewport mapper);

public slots:
    /// Consume one engine presentation snapshot. Empty state dismisses every
    /// capsule, which is how transform exit hides the presentation.
    void present(const TransformPresentationState& state);

private:
    void presentSnapLabels(const std::vector<TransformMetricPointLabel>& labels);
    void presentDragCapsule(const std::vector<TransformMetricSegment>& segments,
        const std::optional<QPointF>& dragAnchor);
    static ruwa::ui::widgets::MetricSegment toWidgetSegment(const TransformMetricSegment& segment);

    QWidget* m_host = nullptr;
    DocumentToViewport m_documentToViewport;
    std::vector<ruwa::ui::widgets::CanvasMetricLabelOverlay*> m_snapLabels;
    ruwa::ui::widgets::CanvasMetricLabelOverlay* m_dragCapsule = nullptr;
    QList<QPointF> m_lastSnapAnchors;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASTRANSFORMMETRICPRESENTER_H
