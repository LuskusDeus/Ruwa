// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_FILL_FILLALGORITHM_H
#define RUWA_FEATURES_FILL_FILLALGORITHM_H

#include <optional>

namespace ruwa::core::canvas {

/// Algorithm backing a fill operation (plan 7.25.3). Fill-domain value owned
/// by the neutral contract: it describes how the fill computes its result, not
/// which widget renders the preview, so it lives here rather than nested in
/// the canvas renderer widget (Stage 1 decoupling).
enum class CanvasFillAlgorithm { Smart, Classic };

/// Semantic preflight outcome of one fill request (plan 7.6.41). The engine
/// and policy report facts only; the application UI decides whether and how
/// to present a rejection.
enum class CanvasFillRequestStatus {
    Accepted,
    RejectedNoEditableTarget,
    RejectedOutsideSelection,
    RejectedSameColor,
    RejectedRegionTooLarge,
    RejectedNotReady
};

/// Radius facts accompanying a RejectedRegionTooLarge rejection.
struct CanvasFillLimitInfo {
    CanvasFillAlgorithm algorithm = CanvasFillAlgorithm::Smart;
    double estimatedRadiusDocumentPx = 0.0;
    double radiusLimitDocumentPx = 0.0;
};

struct CanvasFillRequestResult {
    CanvasFillRequestStatus status = CanvasFillRequestStatus::RejectedNotReady;
    std::optional<CanvasFillLimitInfo> limit;
};

} // namespace ruwa::core::canvas

namespace aether {

/// Legacy spelling for the Aether implementation internals; new application
/// code uses ruwa::core::canvas::CanvasFillAlgorithm directly.
using FillAlgorithm = ruwa::core::canvas::CanvasFillAlgorithm;

} // namespace aether

#endif // RUWA_FEATURES_FILL_FILLALGORITHM_H
