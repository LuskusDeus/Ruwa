// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_UI_CANVASVIEWPORTHOST_H
#define RUWA_FEATURES_CANVAS_UI_CANVASVIEWPORTHOST_H

class QWidget;

namespace ruwa::ui::workspace {

/// UI-semantic identification of the canvas viewport host.
///
/// The viewport host is the generic QWidget an engine binding hands to a
/// CanvasPanel for layout, hit-testing and focus. Input routing must recognize
/// the canvas through these helpers — never by casting to a concrete rendering
/// engine type.

/// True when @p widget IS the viewport host of its canvas panel.
bool isViewportHostWidget(const QWidget* widget);

/// The viewport host containing @p widget (the widget itself included), or
/// nullptr when @p widget is not over a canvas viewport.
QWidget* viewportHostForWidget(const QWidget* widget);

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_UI_CANVASVIEWPORTHOST_H
