// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/CanvasTransformMetricPresenter.h"

#include "features/canvas/ui/CanvasMetricLabelOverlay.h"

namespace ruwa::ui::workspace {

using ruwa::ui::widgets::CanvasMetricLabelOverlay;
using ruwa::ui::widgets::MetricSegment;

CanvasTransformMetricPresenter::CanvasTransformMetricPresenter(
    QWidget* hostParent, QObject* parent)
    : QObject(parent)
    , m_host(hostParent)
{
}

CanvasTransformMetricPresenter::~CanvasTransformMetricPresenter()
{
    // Capsules are Qt children of the host widget; nothing to delete here.
}

void CanvasTransformMetricPresenter::setDocumentToViewport(DocumentToViewport mapper)
{
    m_documentToViewport = std::move(mapper);
}

void CanvasTransformMetricPresenter::present(const TransformPresentationState& state)
{
    presentSnapLabels(state.snapLabels);
    presentDragCapsule(state.dragSegments, state.dragAnchor);
}

void CanvasTransformMetricPresenter::presentSnapLabels(
    const std::vector<TransformMetricPointLabel>& labels)
{
    while (m_snapLabels.size() < labels.size()) {
        m_snapLabels.push_back(new CanvasMetricLabelOverlay(m_host));
    }

    for (size_t i = 0; i < labels.size(); ++i) {
        QPointF anchorViewport(labels[i].anchorDocument.x(), labels[i].anchorDocument.y());
        if (m_documentToViewport) {
            anchorViewport = m_documentToViewport(labels[i].anchorDocument);
        }
        m_snapLabels[i]->presentAtPoint(labels[i].text, anchorViewport);
    }
    for (size_t i = labels.size(); i < m_snapLabels.size(); ++i) {
        m_snapLabels[i]->dismiss();
    }
}

MetricSegment CanvasTransformMetricPresenter::toWidgetSegment(
    const TransformMetricSegment& segment)
{
    // Kind -> icon/width-template mapping. The icons point their default way
    // (left / down / grow); mirroring flips them against the reading. Width
    // templates reserve the value column so the capsule does not twitch while
    // digits come and go.
    MetricSegment out;
    out.mirrorHorizontally = false;
    out.mirrorVertically = false;
    out.text = segment.text;
    switch (segment.kind) {
    case TransformMetricKind::MoveX:
        out.iconResource = QStringLiteral(":/icons/TransformLeft");
        out.mirrorHorizontally = !segment.negativeDirection;
        out.widthTemplate = QStringLiteral("8888");
        break;
    case TransformMetricKind::MoveY:
        out.iconResource = QStringLiteral(":/icons/TransformDown");
        out.mirrorVertically = segment.negativeDirection;
        out.widthTemplate = QStringLiteral("8888");
        break;
    case TransformMetricKind::Rotation:
        out.iconResource = QStringLiteral(":/icons/TransformRotation");
        out.widthTemplate = QStringLiteral("-888.8°");
        break;
    case TransformMetricKind::Scale:
        out.iconResource = segment.negativeDirection ? QStringLiteral(":/icons/TransformSmaller")
                                                     : QStringLiteral(":/icons/TransformBigger");
        out.widthTemplate = segment.text.contains(QChar(0x00D7))
            ? QStringLiteral("888.8% × 888.8%")
            : QStringLiteral("888.8%");
        break;
    }
    return out;
}

void CanvasTransformMetricPresenter::presentDragCapsule(
    const std::vector<TransformMetricSegment>& segments, const std::optional<QPointF>& dragAnchor)
{
    if (segments.empty() || !dragAnchor.has_value()) {
        if (m_dragCapsule) {
            m_dragCapsule->dismiss();
        }
        return;
    }

    if (!m_dragCapsule) {
        m_dragCapsule = new CanvasMetricLabelOverlay(m_host);
    }

    QList<MetricSegment> widgetSegments;
    widgetSegments.reserve(static_cast<qsizetype>(segments.size()));
    for (const TransformMetricSegment& segment : segments) {
        widgetSegments.append(toWidgetSegment(segment));
    }

    m_dragCapsule->presentAtCursor(widgetSegments, *dragAnchor);
}

} // namespace ruwa::ui::workspace
