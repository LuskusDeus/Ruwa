// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   B A C K D R O P   S O U R C E   ( I F A C E )
// ==========================================================================
//   Lets on-canvas overlay widgets paint their own frosted-glass backdrop
//   from a downsampled+blurred snapshot of the canvas viewport. The whole
//   overlay (frost + chrome) is then drawn by a single QPainter, so the blur
//   can never desync positionally from the widget while it is dragged — only
//   the snapshot CONTENT is (invisibly) a few frames stale. See design notes.
// ==========================================================================

#ifndef RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H
#define RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H

#include <QImage>
#include <QRect>
#include <QSize>

namespace ruwa::shared::rendering {

class ICanvasBackdropSource {
public:
    virtual ~ICanvasBackdropSource() = default;

    /// True once at least one blurred snapshot has been produced.
    virtual bool backdropAvailable() const = 0;

    /// Crop the cached blurred snapshot to \a globalRect (global screen coords,
    /// logical px) and scale it to \a targetSize (logical px). Returns a null
    /// image when no snapshot is available yet. The result is already blurred
    /// and oriented top-down, ready to drawImage() at the widget origin.
    virtual QImage sampleBackdrop(const QRect& globalRect, const QSize& targetSize) const = 0;

    /// Reference-count active consumers so the source only spends GPU/readback
    /// budget producing snapshots while at least one overlay needs them.
    virtual void addBackdropConsumer() = 0;
    virtual void removeBackdropConsumer() = 0;
};

} // namespace ruwa::shared::rendering

#endif // RUWA_SHARED_RENDERING_CANVASBACKDROPSOURCE_H
