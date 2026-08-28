// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/CanvasViewportHost.h"

#include "features/canvas/ui/CanvasPanel.h"

#include <QWidget>

namespace ruwa::ui::workspace {

bool isViewportHostWidget(const QWidget* widget)
{
    if (!widget) {
        return false;
    }
    for (const QWidget* current = widget; current; current = current->parentWidget()) {
        auto* mutableCurrent = const_cast<QWidget*>(current);
        if (auto* panel = qobject_cast<CanvasPanel*>(mutableCurrent)) {
            return panel->viewportHostWidget() == widget;
        }
    }
    return false;
}

QWidget* viewportHostForWidget(const QWidget* widget)
{
    if (!widget) {
        return nullptr;
    }
    for (const QWidget* current = widget; current; current = current->parentWidget()) {
        if (isViewportHostWidget(current)) {
            return const_cast<QWidget*>(current);
        }
    }
    return nullptr;
}

} // namespace ruwa::ui::workspace
