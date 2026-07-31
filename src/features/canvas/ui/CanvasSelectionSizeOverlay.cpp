// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/CanvasSelectionSizeOverlay.h"

#include <algorithm>

namespace ruwa::ui::widgets {

CanvasSelectionSizeOverlay::CanvasSelectionSizeOverlay(QWidget* parent)
    : CanvasMetricLabelOverlay(parent)
{
}

CanvasSelectionSizeOverlay::~CanvasSelectionSizeOverlay() = default;

void CanvasSelectionSizeOverlay::present(
    int widthPx, int heightPx, const QRectF& selectionRectPanel)
{
    presentNearRect(QStringLiteral("%1 × %2").arg(std::max(0, widthPx)).arg(std::max(0, heightPx)),
        selectionRectPanel);
}

} // namespace ruwa::ui::widgets
