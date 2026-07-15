// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H   C U R S O R   C O N T O U R   B U I L D E R
// ==========================================================================
//   Off-thread generator of the brush-stamp outline used by the GL cursor
//   overlay. Reuses CPU alpha masks from DabShapeCache and applies the same
//   roundness / angle / dab-rotation / xy-scale transforms the dab sampler
//   uses, so the cursor matches what will actually be painted.
//
//   submit() is coalesced via a short QTimer debounce; computation runs on
//   QThreadPool (QtConcurrent::run) and the result is queued back to the
//   owner thread. Stale results (superseded by a newer submit) are dropped.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_RENDERING_BRUSHCURSORCONTOURBUILDER_H
#define RUWA_FEATURES_CANVAS_RENDERING_BRUSHCURSORCONTOURBUILDER_H

#include "shared/types/Types.h"

#include <QObject>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

namespace aether {

class BrushCursorContourBuilder : public QObject {
    Q_OBJECT
public:
    struct Request {
        int dabType = 0;
        float roundness = 1.0f;
        float angleDegrees = 0.0f;
        float dabXScale = 1.0f;
        float dabYScale = 1.0f;
        float dabRotation = 0.0f;
        // For dabType > 0: copy of the CPU alpha mask (raw 0..255, row-major).
        std::vector<uint8_t> alphaMask;
        int maskWidth = 0;
        int maskHeight = 0;
    };

    explicit BrushCursorContourBuilder(QObject* parent = nullptr);
    ~BrushCursorContourBuilder() override;

    void submit(Request request);

signals:
    void contoursReady(std::vector<std::vector<aether::Vector2>> contours);

private slots:
    void onDebounceFired();

private:
    QTimer m_debounce;
    std::mutex m_pendingMutex;
    std::optional<Request> m_pending;
    std::atomic<quint64> m_latestGeneration { 0 };
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_RENDERING_BRUSHCURSORCONTOURBUILDER_H
