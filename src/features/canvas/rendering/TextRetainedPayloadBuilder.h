// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_RENDERING_TEXTRETAINEDPAYLOADBUILDER_H
#define RUWA_FEATURES_CANVAS_RENDERING_TEXTRETAINEDPAYLOADBUILDER_H

#include "features/canvas/rendering/RetainedRenderPayload.h"
#include "features/transform/TransformState.h"

#include <QString>

#include <memory>
#include <vector>

namespace ruwa::core::layers {
struct LayerData;
struct TextLayerData;
} // namespace ruwa::core::layers

namespace aether {

struct TextLayoutLineGeometry {
    int textStart = 0;
    int textLength = 0;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct TextLayoutGeometry {
    Rect sourceBounds;
    std::vector<TextLayoutLineGeometry> lines;
};

QString textRetainedPayloadKey(const ruwa::core::layers::LayerData* layer);
QString textRetainedPayloadKey(
    const ruwa::core::layers::LayerData* layer, const TransformState& transformOverride);
Rect computeTextLayoutSourceBounds(const ruwa::core::layers::TextLayerData& textData);
TextLayoutGeometry computeTextLayoutGeometry(const ruwa::core::layers::TextLayerData& textData);
std::vector<Rect> computeTextSelectionSourceRects(
    const ruwa::core::layers::TextLayerData& textData, int selectionStart, int selectionEnd);
Rect computeTextCaretSourceRect(
    const ruwa::core::layers::TextLayerData& textData, int cursorPosition);
int textCursorPositionAtSourcePoint(
    const ruwa::core::layers::TextLayerData& textData, const Vector2& sourcePoint);
std::shared_ptr<RetainedRenderPayload> buildTextRetainedPayload(
    const ruwa::core::layers::LayerData* layer);
std::shared_ptr<RetainedRenderPayload> buildTextRetainedPayload(
    const ruwa::core::layers::LayerData* layer, const TransformState& transformOverride);
bool ensureTextRetainedPayload(ruwa::core::layers::LayerData* layer);

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_TEXTRETAINEDPAYLOADBUILDER_H
