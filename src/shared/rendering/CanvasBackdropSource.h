// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H
#define RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H

namespace ruwa::shared::rendering {

/// Coordinates translucent QWidget chrome with the same-frame GPU backdrop.
class ICanvasBackdropSource {
public:
    virtual ~ICanvasBackdropSource() = default;

    /// True once the GPU backdrop pipeline is ready. Consumers use an opaque
    /// fallback until then.
    virtual bool backdropAvailable() const = 0;

    /// Request a canvas frame after a consumer moved, resized or changed
    /// visibility. Region geometry is sampled immediately before rendering.
    virtual void requestBackdropUpdate() = 0;
};

} // namespace ruwa::shared::rendering

#endif // RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H
