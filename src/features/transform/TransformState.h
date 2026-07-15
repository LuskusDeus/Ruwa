// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T R A N S F O R M   S T A T E
// ==========================================================================

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMSTATE_H
#define RUWA_CORE_TRANSFORM_TRANSFORMSTATE_H

#include "shared/types/Types.h"
#include "shared/tiles/TileTypes.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileData.h"
#include "shared/tiles/TilePixelAccess.h" // format-aware alpha test

#include <array>
#include <cmath>
#include <algorithm>
#include <functional>
#include <limits>
#include <optional>
#include <vector>

namespace aether {

// ==========================================================================
//   H A N D L E   T Y P E S
// ==========================================================================

enum class TransformHandle {
    None,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
    Move, // Inside the bounding box
    Rotate, // Outside the bounding box
    DeformPoint
};

/// Result of hit-testing the transform frame (classic mode splits corner scale vs rotation icon).
struct TransformHitResult {
    TransformHandle handle = TransformHandle::None;
    /// Classic + corner: true only if the offset rotation glyph was hit (drag rotates). False =
    /// vertex/L scale.
    bool classicCornerRotationAffordance = false;
};

// ==========================================================================
//   T R A N S F O R M   S T A T E
// ==========================================================================

struct TransformState {
    // B-spline FFD lattice dimensions (default and limits)
    static constexpr int DEFORM_MESH_COLS = 4;
    static constexpr int DEFORM_MESH_ROWS = 4;
    static constexpr int BSPLINE_MAX_DIM = 8;
    static constexpr int BSPLINE_MAX_KNOTS = BSPLINE_MAX_DIM + 4;
    static constexpr int BSPLINE_MAX_CONTROL_POINTS = BSPLINE_MAX_DIM * BSPLINE_MAX_DIM;
    static constexpr int BSPLINE_DEGREE = 3;

    // Forward-rasterized deform tessellation density. The B-spline surface
    // is tessellated into a DEFORM_TESS_DENSITY x DEFORM_TESS_DENSITY quad
    // grid (each quad = 2 triangles). 32 gives ~2K triangles and is smooth
    // for typical deformations (the underlying surface is C^2-continuous);
    // bump for extreme curvature if visible faceting appears.
    static constexpr int DEFORM_TESS_DENSITY = 32;

    struct DeformVertex {
        Vector2 source {};
        Vector2 target {};
    };

    struct DeformMesh {
        int rows = DEFORM_MESH_ROWS; // control points per column
        int cols = DEFORM_MESH_COLS; // control points per row
        std::vector<DeformVertex> vertices; // size = rows * cols, row-major

        bool isValidVertexIndex(int index) const
        {
            return index >= 0 && index < static_cast<int>(vertices.size());
        }

        Vector2 targetAt(int col, int row) const
        {
            return vertices[static_cast<size_t>(row * cols + col)].target;
        }

        Vector2 sourceAt(int col, int row) const
        {
            return vertices[static_cast<size_t>(row * cols + col)].source;
        }

        /// Interior control points (not on the boundary edges) are derived
        /// automatically from the boundary and are neither shown nor draggable.
        bool isInteriorIndex(int index) const
        {
            if (index < 0 || index >= static_cast<int>(vertices.size())) {
                return false;
            }
            const int row = index / cols;
            const int col = index % cols;
            return row > 0 && row < rows - 1 && col > 0 && col < cols - 1;
        }

        bool isBoundaryIndex(int index) const
        {
            return isValidVertexIndex(index) && !isInteriorIndex(index);
        }

        /// Least-squares affine fit of the boundary control points
        /// (source -> target). Used to derive a smooth base position for the
        /// interior control points that follows the boundary.
        struct AffineFit {
            double ax = 1, ay = 0, ac = 0; // X = ax*x + ay*y + ac
            double dx = 0, dy = 1, dc = 0; // Y = dx*x + dy*y + dc
            bool ok = false;
            Vector2 apply(const Vector2& p) const
            {
                return Vector2 { static_cast<float>(ax * p.x + ay * p.y + ac),
                    static_cast<float>(dx * p.x + dy * p.y + dc) };
            }
        };

        AffineFit fitBoundaryAffine() const
        {
            AffineFit f;
            if (rows < 3 || cols < 3) {
                return f;
            }
            double m00 = 0, m01 = 0, m02 = 0, m11 = 0, m12 = 0, m22 = 0;
            double bx0 = 0, bx1 = 0, bx2 = 0, by0 = 0, by1 = 0, by2 = 0;
            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                if (isInteriorIndex(i)) {
                    continue;
                }
                const Vector2& s = vertices[static_cast<size_t>(i)].source;
                const Vector2& t = vertices[static_cast<size_t>(i)].target;
                const double x = s.x, y = s.y;
                m00 += x * x;
                m01 += x * y;
                m02 += x;
                m11 += y * y;
                m12 += y;
                m22 += 1.0;
                bx0 += x * t.x;
                bx1 += y * t.x;
                bx2 += t.x;
                by0 += x * t.y;
                by1 += y * t.y;
                by2 += t.y;
            }
            const double c00 = m11 * m22 - m12 * m12;
            const double c01 = m12 * m02 - m01 * m22;
            const double c02 = m01 * m12 - m11 * m02;
            const double det = m00 * c00 + m01 * c01 + m02 * c02;
            if (std::abs(det) < 1e-9) {
                return f; // degenerate
            }
            const double inv = 1.0 / det;
            const double i00 = c00 * inv, i01 = c01 * inv, i02 = c02 * inv;
            const double i11 = (m00 * m22 - m02 * m02) * inv;
            const double i12 = (m01 * m02 - m00 * m12) * inv;
            const double i22 = (m00 * m11 - m01 * m01) * inv;
            auto solve = [&](double r0, double r1, double r2, double& a, double& b, double& c) {
                a = i00 * r0 + i01 * r1 + i02 * r2;
                b = i01 * r0 + i11 * r1 + i12 * r2;
                c = i02 * r0 + i12 * r1 + i22 * r2;
            };
            solve(bx0, bx1, bx2, f.ax, f.ay, f.ac);
            solve(by0, by1, by2, f.dx, f.dy, f.dc);
            f.ok = true;
            return f;
        }

        /// Boundary-derived "base" position of an interior control point: the
        /// affine fit of the boundary plus an inverse-distance-weighted residual
        /// of the boundary points. This is the smooth Coons-like position the
        /// interior would take with no independent (region-drag) offset.
        Vector2 derivedInteriorTarget(int index, const AffineFit& f) const
        {
            const Vector2 src = vertices[static_cast<size_t>(index)].source;
            const Vector2 base = f.apply(src);
            double wsum = 0.0, rx = 0.0, ry = 0.0;
            for (int i = 0; i < static_cast<int>(vertices.size()); ++i) {
                if (isInteriorIndex(i)) {
                    continue;
                }
                const Vector2& bs = vertices[static_cast<size_t>(i)].source;
                const double ddx = bs.x - src.x;
                const double ddy = bs.y - src.y;
                const double w = 1.0 / (ddx * ddx + ddy * ddy + 1e-3);
                const Vector2 ba = f.apply(bs);
                rx += w * (vertices[static_cast<size_t>(i)].target.x - ba.x);
                ry += w * (vertices[static_cast<size_t>(i)].target.y - ba.y);
                wsum += w;
            }
            if (wsum > 1e-12) {
                rx /= wsum;
                ry /= wsum;
            }
            return { base.x + static_cast<float>(rx), base.y + static_cast<float>(ry) };
        }

        /// Reset the interior control points to their boundary-derived base
        /// (drops any independent interior offset). Used at mode entry and as a
        /// fallback; smooth boundary-follow with offset preservation is handled
        /// by the controller during drags.
        void recomputeInteriorFromBoundary()
        {
            const AffineFit f = fitBoundaryAffine();
            if (!f.ok) {
                return;
            }
            for (int row = 1; row < rows - 1; ++row) {
                for (int col = 1; col < cols - 1; ++col) {
                    const int idx = row * cols + col;
                    vertices[static_cast<size_t>(idx)].target = derivedInteriorTarget(idx, f);
                }
            }
        }
    };

    // ---- B-spline evaluation helpers ----

    /// Compute clamped uniform knot value for index i given n control points.
    static float bsplineKnot(int i, int n)
    {
        if (i <= BSPLINE_DEGREE)
            return 0.0f;
        if (i >= n)
            return 1.0f;
        return static_cast<float>(i - BSPLINE_DEGREE) / static_cast<float>(n - BSPLINE_DEGREE);
    }

    /// Fill a knot vector for n control points (size must be >= n + BSPLINE_DEGREE + 1).
    static void computeClampedKnots(int n, float* knots)
    {
        for (int i = 0; i <= n + BSPLINE_DEGREE; ++i) {
            knots[i] = bsplineKnot(i, n);
        }
    }

    /// Find span index k such that knots[k] <= u < knots[k+1], clamped for degree 3.
    static int findSpan(float u, int n, const float* knots)
    {
        if (u >= 1.0f)
            return n - 1;
        if (u <= 0.0f)
            return BSPLINE_DEGREE;
        for (int k = BSPLINE_DEGREE; k < n; ++k) {
            if (u < knots[k + 1])
                return k;
        }
        return n - 1;
    }

    /// Compute 4 cubic B-spline basis function values at parameter u in given span.
    /// Returns N[0..3] = N_{span-3}(u) .. N_{span}(u) using Piegl & Tiller A2.2.
    static std::array<float, 4> basisFunctions(float u, int span, const float* knots)
    {
        float N[4];
        float left[4], right[4];
        N[0] = 1.0f;
        for (int j = 1; j <= BSPLINE_DEGREE; ++j) {
            left[j] = u - knots[span + 1 - j];
            right[j] = knots[span + j] - u;
            float saved = 0.0f;
            for (int r = 0; r < j; ++r) {
                float denom = right[r + 1] + left[j - r];
                float temp = (denom > 1e-10f) ? N[r] / denom : 0.0f;
                N[r] = saved + right[r + 1] * temp;
                saved = left[j - r] * temp;
            }
            N[j] = saved;
        }
        return { N[0], N[1], N[2], N[3] };
    }

    /// Basis values and their derivatives at parameter u. Used by the CPU
    /// inverse-surface helpers below for hit-testing and inverse transforms.
    struct BSplineBasis {
        std::array<float, 4> N {};
        std::array<float, 4> dN {};
    };

    /// Compute cubic B-spline basis functions AND their first derivatives.
    static BSplineBasis basisFunctionsAndDerivatives(float u, int span, const float* knots)
    {
        BSplineBasis result {};
        float ndu[4][4] = {};
        float left[4], right[4];
        ndu[0][0] = 1.0f;
        for (int j = 1; j <= BSPLINE_DEGREE; ++j) {
            left[j] = u - knots[span + 1 - j];
            right[j] = knots[span + j] - u;
            float saved = 0.0f;
            for (int r = 0; r < j; ++r) {
                ndu[j][r] = right[r + 1] + left[j - r];
                float temp = (ndu[j][r] > 1e-10f) ? ndu[r][j - 1] / ndu[j][r] : 0.0f;
                ndu[r][j] = saved + right[r + 1] * temp;
                saved = left[j - r] * temp;
            }
            ndu[j][j] = saved;
        }
        for (int r = 0; r <= BSPLINE_DEGREE; ++r) {
            result.N[r] = ndu[r][BSPLINE_DEGREE];
        }
        float N2_0 = ndu[0][2], N2_1 = ndu[1][2], N2_2 = ndu[2][2];
        float d0 = knots[span + 1] - knots[span - 2];
        float d1 = knots[span + 2] - knots[span - 1];
        float d2 = knots[span + 3] - knots[span];
        float a0 = (d0 > 1e-10f) ? N2_0 / d0 : 0.0f;
        float a1 = (d1 > 1e-10f) ? N2_1 / d1 : 0.0f;
        float a2 = (d2 > 1e-10f) ? N2_2 / d2 : 0.0f;
        result.dN[0] = 3.0f * (-a0);
        result.dN[1] = 3.0f * (a0 - a1);
        result.dN[2] = 3.0f * (a1 - a2);
        result.dN[3] = 3.0f * (a2);
        return result;
    }

    static float triangleArea2(const Vector2& a, const Vector2& b, const Vector2& c)
    {
        return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    }

    /// Evaluate the B-spline surface at (u, v) ∈ [0,1]² using the deformMesh lattice targets.
    Vector2 evaluateBSplineSurface(float u, float v) const
    {
        const auto& mesh = *deformMesh;
        float kU[BSPLINE_MAX_KNOTS], kV[BSPLINE_MAX_KNOTS];
        computeClampedKnots(mesh.cols, kU);
        computeClampedKnots(mesh.rows, kV);
        int spanU = findSpan(u, mesh.cols, kU);
        int spanV = findSpan(v, mesh.rows, kV);
        auto Nu = basisFunctions(u, spanU, kU);
        auto Nv = basisFunctions(v, spanV, kV);
        Vector2 result { 0, 0 };
        for (int jv = 0; jv < 4; ++jv) {
            int row = spanV - 3 + jv;
            for (int iu = 0; iu < 4; ++iu) {
                int col = spanU - 3 + iu;
                const auto& cp = mesh.vertices[static_cast<size_t>(row * mesh.cols + col)].target;
                float w = Nu[static_cast<size_t>(iu)] * Nv[static_cast<size_t>(jv)];
                result.x += w * cp.x;
                result.y += w * cp.y;
            }
        }
        return result;
    }

    /// Fill `outWeights` (sized to vertices.size()) with the B-spline basis
    /// influence Nu(u)*Nv(v) of each control point at parameter (u, v). This is
    /// how much each control point governs the surface point under (u, v); used
    /// to deform smartly when dragging inside the region (Photoshop Warp). The
    /// weights live in parameter space, so they behave correctly even on a
    /// heavily distorted mesh. Weights are zero for control points outside the
    /// local span and sum to 1 over all control points.
    void computeDeformRegionWeights(float u, float v, std::vector<float>& outWeights) const
    {
        const auto& mesh = *deformMesh;
        outWeights.assign(mesh.vertices.size(), 0.0f);
        float kU[BSPLINE_MAX_KNOTS], kV[BSPLINE_MAX_KNOTS];
        computeClampedKnots(mesh.cols, kU);
        computeClampedKnots(mesh.rows, kV);
        int spanU = findSpan(u, mesh.cols, kU);
        int spanV = findSpan(v, mesh.rows, kV);
        auto Nu = basisFunctions(u, spanU, kU);
        auto Nv = basisFunctions(v, spanV, kV);
        for (int jv = 0; jv < 4; ++jv) {
            int row = spanV - 3 + jv;
            for (int iu = 0; iu < 4; ++iu) {
                int col = spanU - 3 + iu;
                outWeights[static_cast<size_t>(row * mesh.cols + col)]
                    = Nu[static_cast<size_t>(iu)] * Nv[static_cast<size_t>(jv)];
            }
        }
    }

    /// Evaluate B-spline surface and its partial derivatives at (u, v).
    /// Used by inverse-surface helpers below for hit-testing and CPU-side
    /// inverse transforms. Does NOT participate in GPU rendering — the
    /// rendering path is forward-rasterized and never inverts the surface.
    void evaluateBSplineSurfaceWithDerivatives(
        float u, float v, Vector2& S, Vector2& dSdu, Vector2& dSdv) const
    {
        const auto& mesh = *deformMesh;
        float kU[BSPLINE_MAX_KNOTS], kV[BSPLINE_MAX_KNOTS];
        computeClampedKnots(mesh.cols, kU);
        computeClampedKnots(mesh.rows, kV);
        int spanU = findSpan(u, mesh.cols, kU);
        int spanV = findSpan(v, mesh.rows, kV);
        auto bU = basisFunctionsAndDerivatives(u, spanU, kU);
        auto bV = basisFunctionsAndDerivatives(v, spanV, kV);
        S = { 0, 0 };
        dSdu = { 0, 0 };
        dSdv = { 0, 0 };
        for (int jv = 0; jv < 4; ++jv) {
            int row = spanV - 3 + jv;
            for (int iu = 0; iu < 4; ++iu) {
                int col = spanU - 3 + iu;
                const auto& cp = mesh.vertices[static_cast<size_t>(row * mesh.cols + col)].target;
                float wN = bU.N[iu] * bV.N[jv];
                S.x += wN * cp.x;
                S.y += wN * cp.y;
                float wDu = bU.dN[iu] * bV.N[jv];
                dSdu.x += wDu * cp.x;
                dSdu.y += wDu * cp.y;
                float wDv = bU.N[iu] * bV.dN[jv];
                dSdv.x += wDv * cp.x;
                dSdv.y += wDv * cp.y;
            }
        }
    }

    /// Cheap reject: is P inside the AABB of the control-point lattice
    /// (with a 10% margin)? Used as an early-out before the Newton iteration.
    bool pointInControlPointAABB(const Vector2& P) const
    {
        const auto& mesh = *deformMesh;
        if (mesh.vertices.empty())
            return false;
        Vector2 mn = mesh.vertices[0].target;
        Vector2 mx = mesh.vertices[0].target;
        for (size_t i = 1; i < mesh.vertices.size(); ++i) {
            const auto& t = mesh.vertices[i].target;
            mn.x = std::min(mn.x, t.x);
            mn.y = std::min(mn.y, t.y);
            mx.x = std::max(mx.x, t.x);
            mx.y = std::max(mx.y, t.y);
        }
        float pad = std::max(mx.x - mn.x, mx.y - mn.y) * 0.1f + 2.0f;
        return P.x >= mn.x - pad && P.x <= mx.x + pad && P.y >= mn.y - pad && P.y <= mx.y + pad;
    }

    /// Find (u, v) in [0,1]^2 such that S(u,v) ~= P via multi-seed Newton.
    /// Used by hit-testing (TransformController), inverse-transform queries
    /// from layer/text UI, and the TransformApplicator. NOT used by the GPU
    /// rendering path — rendering is forward-rasterized.
    bool inverseBSplineSurfaceRobust(const Vector2& P, float& outU, float& outV) const
    {
        if (!pointInControlPointAABB(P))
            return false;

        constexpr int gridN = 8;
        constexpr int candidateCount = 6;
        constexpr int maxIter = 16;
        constexpr float tol = 0.25f;
        constexpr float maxErrSq = tol * tol * 16.0f;

        std::array<float, candidateCount> seedU {};
        std::array<float, candidateCount> seedV {};
        std::array<float, candidateCount> seedDistSq {};
        seedDistSq.fill(std::numeric_limits<float>::max());

        auto insertSeed = [&](float u, float v, float distSq) {
            for (int i = 0; i < candidateCount; ++i) {
                if (distSq >= seedDistSq[static_cast<size_t>(i)])
                    continue;
                for (int j = candidateCount - 1; j > i; --j) {
                    seedDistSq[static_cast<size_t>(j)] = seedDistSq[static_cast<size_t>(j - 1)];
                    seedU[static_cast<size_t>(j)] = seedU[static_cast<size_t>(j - 1)];
                    seedV[static_cast<size_t>(j)] = seedV[static_cast<size_t>(j - 1)];
                }
                seedDistSq[static_cast<size_t>(i)] = distSq;
                seedU[static_cast<size_t>(i)] = u;
                seedV[static_cast<size_t>(i)] = v;
                break;
            }
        };

        for (int gi = 0; gi <= gridN; ++gi) {
            float gu = static_cast<float>(gi) / gridN;
            for (int gj = 0; gj <= gridN; ++gj) {
                float gv = static_cast<float>(gj) / gridN;
                Vector2 S = evaluateBSplineSurface(gu, gv);
                float dx = P.x - S.x;
                float dy = P.y - S.y;
                insertSeed(gu, gv, dx * dx + dy * dy);
            }
        }

        auto trySeed = [&](float startU, float startV, float& solvedU, float& solvedV, float& errSq,
                           bool& positiveDet) -> bool {
            float u = startU;
            float v = startV;

            for (int iter = 0; iter < maxIter; ++iter) {
                Vector2 S, dSdu, dSdv;
                evaluateBSplineSurfaceWithDerivatives(u, v, S, dSdu, dSdv);
                float rx = P.x - S.x;
                float ry = P.y - S.y;
                float det = dSdu.x * dSdv.y - dSdv.x * dSdu.y;
                if (std::abs(rx) < tol && std::abs(ry) < tol) {
                    solvedU = u;
                    solvedV = v;
                    errSq = rx * rx + ry * ry;
                    positiveDet = det > 0.0f;
                    return true;
                }
                float regDet = det + std::copysign(1e-6f, det + 1e-20f);
                float du = (rx * dSdv.y - ry * dSdv.x) / regDet;
                float dv = (ry * dSdu.x - rx * dSdu.y) / regDet;
                float stepLen = std::abs(du) + std::abs(dv);
                if (stepLen > 0.5f) {
                    float scale = 0.5f / stepLen;
                    du *= scale;
                    dv *= scale;
                }
                u = std::clamp(u + du, 0.0f, 1.0f);
                v = std::clamp(v + dv, 0.0f, 1.0f);
            }

            Vector2 S, dSdu, dSdv;
            evaluateBSplineSurfaceWithDerivatives(u, v, S, dSdu, dSdv);
            float rx = P.x - S.x;
            float ry = P.y - S.y;
            float det = dSdu.x * dSdv.y - dSdv.x * dSdu.y;
            errSq = rx * rx + ry * ry;
            if (errSq < maxErrSq) {
                solvedU = u;
                solvedV = v;
                positiveDet = det > 0.0f;
                return true;
            }
            return false;
        };

        bool foundPositive = false;
        bool foundFallback = false;
        float bestPositiveErrSq = std::numeric_limits<float>::max();
        float bestFallbackErrSq = std::numeric_limits<float>::max();
        float bestPositiveU = 0.5f;
        float bestPositiveV = 0.5f;
        float bestFallbackU = 0.5f;
        float bestFallbackV = 0.5f;

        for (int i = 0; i < candidateCount; ++i) {
            if (!std::isfinite(seedDistSq[static_cast<size_t>(i)]))
                continue;

            float solvedU = 0.5f;
            float solvedV = 0.5f;
            float errSq = std::numeric_limits<float>::max();
            bool positiveDet = false;
            if (!trySeed(seedU[static_cast<size_t>(i)], seedV[static_cast<size_t>(i)], solvedU,
                    solvedV, errSq, positiveDet)) {
                continue;
            }

            if (positiveDet) {
                if (!foundPositive || errSq < bestPositiveErrSq) {
                    foundPositive = true;
                    bestPositiveErrSq = errSq;
                    bestPositiveU = solvedU;
                    bestPositiveV = solvedV;
                }
            } else if (!foundFallback || errSq < bestFallbackErrSq) {
                foundFallback = true;
                bestFallbackErrSq = errSq;
                bestFallbackU = solvedU;
                bestFallbackV = solvedV;
            }
        }

        if (foundPositive) {
            outU = bestPositiveU;
            outV = bestPositiveV;
            return true;
        }
        if (foundFallback) {
            outU = bestFallbackU;
            outV = bestFallbackV;
            return true;
        }
        return false;
    }

    // Original content bounds in world coordinates (before any transform)
    Rect contentBounds;

    // Transform parameters (applied relative to pivot)
    Vector2 translation { 0.0f, 0.0f };
    float rotation = 0.0f; // radians
    Vector2 scale { 1.0f, 1.0f };

    // Pivot point in world coordinates (center of contentBounds by default)
    Vector2 pivot { 0.0f, 0.0f };

    // Free-form quad (Ctrl-drag): 4 corners in world space when set.
    // Order: TopLeft(0), TopRight(1), BottomRight(2), BottomLeft(3).
    // When set, transform is defined by this quad instead of translation/rotation/scale.
    std::optional<std::array<Vector2, 4>> freeCorners;

    // Mesh deform control points and quads.
    // Each vertex stores original-space position (`source`) and deformed-space position (`target`).
    // Quads reference vertices clockwise: TopLeft, TopRight, BottomRight, BottomLeft.
    std::optional<DeformMesh> deformMesh;

    // Handle visual size in screen pixels
    static constexpr float HANDLE_SIZE = 8.0f;
    /// Classic: hit radius (px) at the true vertex — proportional scale (L-corner). Must be <
    /// CORNER_ROTATION_HANDLE_OUTSET so the outward rotation glyph remains outside this disk.
    static constexpr float CORNER_SCALE_VERTEX_HIT_RADIUS = 14.0f;
    /// Classic corner rotation: icon/hit shifted outward from the vertex (along center→corner) so
    /// L-brackets stay clickable.
    static constexpr float CORNER_ROTATION_HANDLE_OUTSET = 18.0f;
    /// Half-size in screen pixels for corner rotation icon (classic mode).
    static constexpr float CORNER_ROTATION_AFFORDANCE_HALF = 12.0f;
    /// Half-width (px) of hit zone along segment vertex → affordance: covers L-corner + rotated
    /// icon (wider than small circles).
    static constexpr float CORNER_ROTATION_CAPSULE_RADIUS = 26.0f;
    /// When using corner rotation: strip this many screen pixels from each end of an edge before
    /// edge scale hit-test.
    static constexpr float EDGE_HIT_CORNER_EXCLUSION = 26.0f;
    static constexpr float ROTATION_MARGIN = 20.0f; // Pixels outside box for rotate hit zone
    static constexpr float ROTATION_HANDLE_OFFSET
        = 30.0f; // Pixels from top edge to rotation handle
    static constexpr float OVERLAY_CORNER_LENGTH = 24.0f; // Pixels
    static constexpr float OVERLAY_EDGE_GAP = 8.0f; // Pixels
    static constexpr float OVERLAY_LINE_THICKNESS = 3.0f; // Pixels

    // ---- Reset to identity ----
    void reset()
    {
        translation = { 0.0f, 0.0f };
        rotation = 0.0f;
        scale = { 1.0f, 1.0f };
        freeCorners.reset();
        deformMesh.reset();
    }

    /// Shift transform for canvas resize (origin moved by offsetX, offsetY).
    /// Keeps projected content in the same visual position.
    void shiftForCanvasResize(int offsetX, int offsetY)
    {
        const float dx = -static_cast<float>(offsetX);
        const float dy = -static_cast<float>(offsetY);
        translation.x += dx;
        translation.y += dy;
        if (freeCorners.has_value()) {
            auto corners = *freeCorners;
            for (auto& c : corners) {
                c.x += dx;
                c.y += dy;
            }
            freeCorners = corners;
        }
        if (deformMesh.has_value()) {
            auto mesh = *deformMesh;
            for (auto& vertex : mesh.vertices) {
                vertex.target.x += dx;
                vertex.target.y += dy;
            }
            deformMesh = mesh;
        }
    }

    bool hasFreeQuad() const { return freeCorners.has_value(); }
    bool hasDeformMesh() const { return deformMesh.has_value(); }

    static Vector2 bilinearPoint(const std::array<Vector2, 4>& q, float s, float tN)
    {
        return { (1.0f - s) * (1.0f - tN) * q[0].x + s * (1.0f - tN) * q[1].x + s * tN * q[2].x
                + (1.0f - s) * tN * q[3].x,
            (1.0f - s) * (1.0f - tN) * q[0].y + s * (1.0f - tN) * q[1].y + s * tN * q[2].y
                + (1.0f - s) * tN * q[3].y };
    }

    void initializeDeformMeshFromCurrentTransform()
    {
        DeformMesh mesh {};
        mesh.rows = DEFORM_MESH_ROWS;
        mesh.cols = DEFORM_MESH_COLS;
        mesh.vertices.reserve(mesh.rows * mesh.cols);
        for (int row = 0; row < mesh.rows; ++row) {
            float v = (mesh.rows > 1) ? static_cast<float>(row) / (mesh.rows - 1) : 0.5f;
            for (int col = 0; col < mesh.cols; ++col) {
                float u = (mesh.cols > 1) ? static_cast<float>(col) / (mesh.cols - 1) : 0.5f;
                Vector2 source { contentBounds.left() + u * contentBounds.width,
                    contentBounds.top() + v * contentBounds.height };
                mesh.vertices.push_back({ source, transformPoint(source) });
            }
        }
        mesh.recomputeInteriorFromBoundary();
        deformMesh = std::move(mesh);
        translation = { 0.0f, 0.0f };
        rotation = 0.0f;
        scale = { 1.0f, 1.0f };
        freeCorners.reset();
        pivot = contentBounds.center();
    }

    void collapseDeformMeshToFreeQuad()
    {
        if (!deformMesh.has_value()) {
            return;
        }
        if (isIdentity()) {
            deformMesh.reset();
            translation = { 0.0f, 0.0f };
            rotation = 0.0f;
            scale = { 1.0f, 1.0f };
            freeCorners.reset();
            pivot = contentBounds.center();
            return;
        }
        // Evaluate B-spline surface at the 4 corners of the parameter domain
        freeCorners = std::array<Vector2, 4> { evaluateBSplineSurface(0.0f, 0.0f),
            evaluateBSplineSurface(1.0f, 0.0f), evaluateBSplineSurface(1.0f, 1.0f),
            evaluateBSplineSurface(0.0f, 1.0f) };
        deformMesh.reset();
        translation = { 0.0f, 0.0f };
        rotation = 0.0f;
        scale = { 1.0f, 1.0f };
        pivot = contentBounds.center();
    }

    bool isIdentity() const
    {
        if (deformMesh.has_value()) {
            constexpr float eps = 0.001f;
            for (const auto& vertex : deformMesh->vertices) {
                if (std::abs(vertex.target.x - vertex.source.x) > eps
                    || std::abs(vertex.target.y - vertex.source.y) > eps) {
                    return false;
                }
            }
            return true;
        }
        if (freeCorners.has_value())
            return false;
        return std::abs(translation.x) < 0.001f && std::abs(translation.y) < 0.001f
            && std::abs(rotation) < 0.001f && std::abs(scale.x - 1.0f) < 0.001f
            && std::abs(scale.y - 1.0f) < 0.001f;
    }

    // ---- Transform a point from original space to transformed space ----
    Vector2 transformPoint(const Vector2& p) const
    {
        if (deformMesh.has_value()) {
            float u = (contentBounds.width > 0.001f)
                ? std::clamp((p.x - contentBounds.left()) / contentBounds.width, 0.0f, 1.0f)
                : 0.5f;
            float v = (contentBounds.height > 0.001f)
                ? std::clamp((p.y - contentBounds.top()) / contentBounds.height, 0.0f, 1.0f)
                : 0.5f;
            return evaluateBSplineSurface(u, v);
        }
        if (freeCorners.has_value()) {
            const auto& q = *freeCorners;
            float l = contentBounds.left(), r = contentBounds.right();
            float t = contentBounds.top(), b = contentBounds.bottom();
            float s = (r > l) ? (p.x - l) / (r - l) : 0.5f;
            float tN = (b > t) ? (p.y - t) / (b - t) : 0.5f;
            // Bilinear interpolation: P(s,t) = (1-s)(1-t)*Q0 + s(1-t)*Q1 + s*t*Q2 + (1-s)*t*Q3
            float x = (1 - s) * (1 - tN) * q[0].x + s * (1 - tN) * q[1].x + s * tN * q[2].x
                + (1 - s) * tN * q[3].x;
            float y = (1 - s) * (1 - tN) * q[0].y + s * (1 - tN) * q[1].y + s * tN * q[2].y
                + (1 - s) * tN * q[3].y;
            return { x, y };
        }
        // 1. Translate to pivot origin
        float dx = p.x - pivot.x;
        float dy = p.y - pivot.y;

        // 2. Scale
        dx *= scale.x;
        dy *= scale.y;

        // 3. Rotate
        float cosR = std::cos(rotation);
        float sinR = std::sin(rotation);
        float rx = dx * cosR - dy * sinR;
        float ry = dx * sinR + dy * cosR;

        // 4. Translate back + apply translation
        return { rx + pivot.x + translation.x, ry + pivot.y + translation.y };
    }

    // ---- Inverse transform: from transformed space back to original ----
    Vector2 inverseTransformPoint(const Vector2& p) const
    {
        Vector2 mapped {};
        if (tryInverseTransformPoint(p, mapped)) {
            return mapped;
        }
        if (deformMesh.has_value()) {
            return contentBounds.center();
        }
        if (freeCorners.has_value()) {
            const auto& q = *freeCorners;
            float st[2];
            if (!inverseBilinear(p, q, st)) {
                return contentBounds.center();
            }
            float l = contentBounds.left(), r = contentBounds.right();
            float t = contentBounds.top(), b = contentBounds.bottom();
            return { l + st[0] * (r - l), t + st[1] * (b - t) };
        }
        // Reverse translation
        float dx = p.x - pivot.x - translation.x;
        float dy = p.y - pivot.y - translation.y;

        // Reverse rotation
        float cosR = std::cos(-rotation);
        float sinR = std::sin(-rotation);
        float rx = dx * cosR - dy * sinR;
        float ry = dx * sinR + dy * cosR;

        // Reverse scale
        if (std::abs(scale.x) > 0.0001f)
            rx /= scale.x;
        if (std::abs(scale.y) > 0.0001f)
            ry /= scale.y;

        return { rx + pivot.x, ry + pivot.y };
    }

    // ---- Get the 4 corners of the transformed bounding box ----
    std::array<Vector2, 4> transformedCorners() const
    {
        if (deformMesh.has_value()) {
            return { transformPoint({ contentBounds.left(), contentBounds.top() }),
                transformPoint({ contentBounds.right(), contentBounds.top() }),
                transformPoint({ contentBounds.right(), contentBounds.bottom() }),
                transformPoint({ contentBounds.left(), contentBounds.bottom() }) };
        }
        if (freeCorners.has_value())
            return *freeCorners;
        return { transformPoint({ contentBounds.left(), contentBounds.top() }),
            transformPoint({ contentBounds.right(), contentBounds.top() }),
            transformPoint({ contentBounds.right(), contentBounds.bottom() }),
            transformPoint({ contentBounds.left(), contentBounds.bottom() }) };
    }

    // ---- Axis-aligned bounding box of the transformed content ----
    Rect transformedAABB() const
    {
        if (deformMesh.has_value() && !deformMesh->vertices.empty()) {
            float minX = deformMesh->vertices.front().target.x;
            float maxX = deformMesh->vertices.front().target.x;
            float minY = deformMesh->vertices.front().target.y;
            float maxY = deformMesh->vertices.front().target.y;
            for (size_t i = 1; i < deformMesh->vertices.size(); ++i) {
                minX = std::min(minX, deformMesh->vertices[i].target.x);
                maxX = std::max(maxX, deformMesh->vertices[i].target.x);
                minY = std::min(minY, deformMesh->vertices[i].target.y);
                maxY = std::max(maxY, deformMesh->vertices[i].target.y);
            }
            return { minX, minY, maxX - minX, maxY - minY };
        }
        auto corners = transformedCorners();
        float minX = corners[0].x, maxX = corners[0].x;
        float minY = corners[0].y, maxY = corners[0].y;
        for (int i = 1; i < 4; ++i) {
            minX = std::min(minX, corners[i].x);
            maxX = std::max(maxX, corners[i].x);
            minY = std::min(minY, corners[i].y);
            maxY = std::max(maxY, corners[i].y);
        }
        return { minX, minY, maxX - minX, maxY - minY };
    }

    // ---- Rotation handle position (world coordinates) ----
    Vector2 rotationHandlePosition(float screenZoom) const
    {
        Vector2 center;
        Vector2 topCenter;
        if (freeCorners.has_value()) {
            const auto& c = *freeCorners;
            center = { (c[0].x + c[1].x + c[2].x + c[3].x) * 0.25f,
                (c[0].y + c[1].y + c[2].y + c[3].y) * 0.25f };
            topCenter = { (c[0].x + c[1].x) * 0.5f, (c[0].y + c[1].y) * 0.5f };
        } else {
            float cx = (contentBounds.left() + contentBounds.right()) * 0.5f;
            center = transformPoint(contentBounds.center());
            topCenter = transformPoint({ cx, contentBounds.top() });
        }
        // Direction from center outward through top-center
        Vector2 dir = { topCenter.x - center.x, topCenter.y - center.y };
        float dirLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (dirLen > 0.001f) {
            dir.x /= dirLen;
            dir.y /= dirLen;
        } else if (!freeCorners.has_value()) {
            dir = { -std::sin(rotation), -std::cos(rotation) };
        }

        float offset = ROTATION_HANDLE_OFFSET / screenZoom;
        return { topCenter.x + dir.x * offset, topCenter.y + dir.y * offset };
    }

    // ---- Handle positions (in world coordinates, transformed) ----
    Vector2 handlePosition(TransformHandle handle) const
    {
        if (freeCorners.has_value()) {
            const auto& c = *freeCorners;
            switch (handle) {
            case TransformHandle::TopLeft:
                return c[0];
            case TransformHandle::Top:
                return { (c[0].x + c[1].x) * 0.5f, (c[0].y + c[1].y) * 0.5f };
            case TransformHandle::TopRight:
                return c[1];
            case TransformHandle::Right:
                return { (c[1].x + c[2].x) * 0.5f, (c[1].y + c[2].y) * 0.5f };
            case TransformHandle::BottomRight:
                return c[2];
            case TransformHandle::Bottom:
                return { (c[2].x + c[3].x) * 0.5f, (c[2].y + c[3].y) * 0.5f };
            case TransformHandle::BottomLeft:
                return c[3];
            case TransformHandle::Left:
                return { (c[3].x + c[0].x) * 0.5f, (c[3].y + c[0].y) * 0.5f };
            default:
                return { (c[0].x + c[1].x + c[2].x + c[3].x) * 0.25f,
                    (c[0].y + c[1].y + c[2].y + c[3].y) * 0.25f };
            }
        }
        float l = contentBounds.left();
        float r = contentBounds.right();
        float t = contentBounds.top();
        float b = contentBounds.bottom();
        float cx = (l + r) * 0.5f;
        float cy = (t + b) * 0.5f;

        Vector2 pos;
        switch (handle) {
        case TransformHandle::TopLeft:
            pos = { l, t };
            break;
        case TransformHandle::Top:
            pos = { cx, t };
            break;
        case TransformHandle::TopRight:
            pos = { r, t };
            break;
        case TransformHandle::Right:
            pos = { r, cy };
            break;
        case TransformHandle::BottomRight:
            pos = { r, b };
            break;
        case TransformHandle::Bottom:
            pos = { cx, b };
            break;
        case TransformHandle::BottomLeft:
            pos = { l, b };
            break;
        case TransformHandle::Left:
            pos = { l, cy };
            break;
        default:
            pos = { cx, cy };
            break;
        }
        return transformPoint(pos);
    }

    /// World position for classic corner rotation UI: offset outward from the true corner vertex.
    Vector2 cornerRotationAffordanceWorld(TransformHandle corner, float screenZoom) const
    {
        Vector2 c = handlePosition(corner);
        auto q = transformedCorners();
        Vector2 center { (q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f,
            (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f };
        Vector2 outward { c.x - center.x, c.y - center.y };
        float len = std::sqrt(outward.x * outward.x + outward.y * outward.y);
        if (len > 1.0e-5f) {
            outward.x /= len;
            outward.y /= len;
        } else {
            outward = { 0.0f, 0.0f };
        }
        const float d = CORNER_ROTATION_HANDLE_OUTSET / screenZoom;
        return { c.x + outward.x * d, c.y + outward.y * d };
    }

    /// Rotation (radians) for corner rotation icon.
    float cornerRotationIconAngleRadians(TransformHandle corner) const
    {
        int idx = 2;
        switch (corner) {
        case TransformHandle::TopLeft:
            idx = 0;
            break;
        case TransformHandle::TopRight:
            idx = 1;
            break;
        case TransformHandle::BottomRight:
            idx = 2;
            break;
        case TransformHandle::BottomLeft:
            idx = 3;
            break;
        default:
            break;
        }
        constexpr float kQuarter = 1.57079632679489661923f;
        // BR (idx 2) stays aligned with frame rotation; +1 step vs previous tuning (+90° on all
        // icons).
        return rotation + static_cast<float>(idx - 1) * kQuarter;
    }

    // ---- Hit test: which handle is at the given world position? ----
    // screenZoom is needed to convert handle sizes from screen to world
    // Classic + useCornersAsRotation: vertex/L first (scale), then edges, then the outward
    // corner-rotation affordance. When the pointer overlaps the classic corner capsule/icon,
    // choose between scale vs rotate by whichever anchor (true vertex vs rotation icon center)
    // is actually closer to the cursor.
    TransformHitResult hitTestDetailed(
        const Vector2& worldPos, float screenZoom, bool useCornersAsRotation) const
    {
        TransformHitResult out;
        float handleWorldSize = HANDLE_SIZE / screenZoom;

        static const TransformHandle cornerHandles[] = { TransformHandle::TopLeft,
            TransformHandle::TopRight, TransformHandle::BottomRight, TransformHandle::BottomLeft };

        if (!useCornersAsRotation) {
            for (auto h : cornerHandles) {
                Vector2 hp = handlePosition(h);
                float dx = worldPos.x - hp.x;
                float dy = worldPos.y - hp.y;
                if (std::abs(dx) <= handleWorldSize && std::abs(dy) <= handleWorldSize) {
                    out.handle = h;
                    return out;
                }
            }
        }

        if (useCornersAsRotation) {
            const float scaleR = CORNER_SCALE_VERTEX_HIT_RADIUS / screenZoom;
            const float scaleR2 = scaleR * scaleR;
            for (auto h : cornerHandles) {
                Vector2 hv = handlePosition(h);
                float dvx = worldPos.x - hv.x;
                float dvy = worldPos.y - hv.y;
                if (dvx * dvx + dvy * dvy <= scaleR2) {
                    out.handle = h;
                    out.classicCornerRotationAffordance = false;
                    return out;
                }
            }
        }

        auto corners = transformedCorners();
        float lineThicknessWorld = OVERLAY_LINE_THICKNESS / screenZoom;
        float cornerLengthWorld = OVERLAY_CORNER_LENGTH / screenZoom;
        float edgeGapWorld = OVERLAY_EDGE_GAP / screenZoom;
        float edgeOffset = cornerLengthWorld + edgeGapWorld;
        float edgeHit = lineThicknessWorld * 0.5f + (4.0f / screenZoom);

        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) % 4;
            Vector2 p0 = corners[i];
            Vector2 p1 = corners[j];
            Vector2 dir = { p1.x - p0.x, p1.y - p0.y };
            float edgeLen = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (edgeLen < 0.0001f)
                continue;
            dir.x /= edgeLen;
            dir.y /= edgeLen;

            float inset0 = edgeOffset;
            float inset1 = edgeOffset;
            if (useCornersAsRotation) {
                const float ex = EDGE_HIT_CORNER_EXCLUSION / screenZoom;
                inset0 = std::min(ex, edgeLen * 0.48f);
                inset1 = inset0;
            }
            if (edgeLen <= inset0 + inset1 + 1.0e-4f) {
                continue;
            }

            Vector2 start = { p0.x + dir.x * inset0, p0.y + dir.y * inset0 };
            Vector2 end = { p1.x - dir.x * inset1, p1.y - dir.y * inset1 };

            float dist = pointSegmentDistance(worldPos, start, end);
            if (dist <= edgeHit) {
                switch (i) {
                case 0:
                    out.handle = TransformHandle::Top;
                    return out;
                case 1:
                    out.handle = TransformHandle::Right;
                    return out;
                case 2:
                    out.handle = TransformHandle::Bottom;
                    return out;
                case 3:
                    out.handle = TransformHandle::Left;
                    return out;
                default:
                    break;
                }
            }
        }

        if (useCornersAsRotation) {
            const float capR = CORNER_ROTATION_CAPSULE_RADIUS / screenZoom;
            const float iconR = (CORNER_ROTATION_AFFORDANCE_HALF * 1.45f + 8.0f) / screenZoom;
            const float iconR2 = iconR * iconR;
            for (auto h : cornerHandles) {
                Vector2 hv = handlePosition(h);
                Vector2 ha = cornerRotationAffordanceWorld(h, screenZoom);
                float dax = worldPos.x - ha.x;
                float day = worldPos.y - ha.y;
                const float iconDist2 = dax * dax + day * day;
                float dvx = worldPos.x - hv.x;
                float dvy = worldPos.y - hv.y;
                const float vertexDist2 = dvx * dvx + dvy * dvy;
                const bool rotationAffordanceHit
                    = pointSegmentDistance(worldPos, hv, ha) <= capR || iconDist2 <= iconR2;
                if (rotationAffordanceHit) {
                    out.handle = h;
                    out.classicCornerRotationAffordance = iconDist2 < vertexDist2;
                    return out;
                }
            }
        }

        if (pointInTransformedRect(worldPos)) {
            out.handle = TransformHandle::Move;
            return out;
        }

        if (!useCornersAsRotation) {
            Vector2 rotHandle = rotationHandlePosition(screenZoom);
            float rdx = worldPos.x - rotHandle.x;
            float rdy = worldPos.y - rotHandle.y;
            float rotHitSize = handleWorldSize * 1.5f;
            if (std::abs(rdx) <= rotHitSize && std::abs(rdy) <= rotHitSize) {
                out.handle = TransformHandle::Rotate;
                return out;
            }
        }

        return out;
    }

    TransformHandle hitTest(
        const Vector2& worldPos, float screenZoom, bool useCornersAsRotation = false) const
    {
        return hitTestDetailed(worldPos, screenZoom, useCornersAsRotation).handle;
    }

    // ---- Point-in-polygon test for the transformed rectangle ----
    bool pointInTransformedRect(const Vector2& p) const
    {
        if (deformMesh.has_value()) {
            Vector2 src {};
            return tryInverseTransformPoint(p, src);
        }
        if (freeCorners.has_value()) {
            const auto& c = *freeCorners;
            int n = 4;
            int crossings = 0;
            for (int i = 0; i < n; ++i) {
                int j = (i + 1) % n;
                float yi = c[i].y - p.y, yj = c[j].y - p.y;
                if ((yi > 0.0f) != (yj > 0.0f)) {
                    float xi = c[i].x - p.x, xj = c[j].x - p.x;
                    float dy = yj - yi;
                    // Ray casting: count only when intersection is to the right of p.
                    // Must multiply by dy to handle both edge directions correctly.
                    if ((xi * dy + (xj - xi) * (-yi)) * dy >= 0.0f)
                        ++crossings;
                }
            }
            return (crossings & 1) != 0;
        }
        Vector2 local = inverseTransformPoint(p);
        return contentBounds.contains(local);
    }

    bool tryInverseTransformPoint(const Vector2& p, Vector2& out) const
    {
        if (deformMesh.has_value()) {
            float u, v;
            if (!inverseBSplineSurfaceRobust(p, u, v)) {
                return false;
            }
            out = { contentBounds.left() + u * contentBounds.width,
                contentBounds.top() + v * contentBounds.height };
            return true;
        }

        if (freeCorners.has_value()) {
            const auto& q = *freeCorners;
            float st[2];
            if (!inverseBilinear(p, q, st)) {
                return false;
            }
            float l = contentBounds.left(), r = contentBounds.right();
            float t = contentBounds.top(), b = contentBounds.bottom();
            out = { l + st[0] * (r - l), t + st[1] * (b - t) };
            return true;
        }

        float dx = p.x - pivot.x - translation.x;
        float dy = p.y - pivot.y - translation.y;

        float cosR = std::cos(-rotation);
        float sinR = std::sin(-rotation);
        float rx = dx * cosR - dy * sinR;
        float ry = dx * sinR + dy * cosR;

        if (std::abs(scale.x) > 0.0001f)
            rx /= scale.x;
        if (std::abs(scale.y) > 0.0001f)
            ry /= scale.y;

        out = { rx + pivot.x, ry + pivot.y };
        return true;
    }

    std::optional<int> hitTestDeformControlPoint(const Vector2& worldPos, float screenZoom) const
    {
        if (!deformMesh.has_value()) {
            return std::nullopt;
        }
        const float handleWorldSize = HANDLE_SIZE / screenZoom;
        for (size_t i = 0; i < deformMesh->vertices.size(); ++i) {
            // Only boundary points are user-editable; interior points are derived.
            if (deformMesh->isInteriorIndex(static_cast<int>(i))) {
                continue;
            }
            const Vector2 hp = deformMesh->vertices[i].target;
            const float dx = worldPos.x - hp.x;
            const float dy = worldPos.y - hp.y;
            if (std::abs(dx) <= handleWorldSize && std::abs(dy) <= handleWorldSize) {
                return static_cast<int>(i);
            }
        }
        return std::nullopt;
    }

    /// Hit-test the B-spline deformed region: returns true if the point is inside.
    bool hitTestDeformRegion(const Vector2& worldPos) const
    {
        if (!deformMesh.has_value())
            return false;
        float u, v;
        return inverseBSplineSurfaceRobust(worldPos, u, v);
    }

    void offsetDeformMeshPoint(int index, const Vector2& delta)
    {
        if (!deformMesh.has_value() || !deformMesh->isValidVertexIndex(index)) {
            return;
        }
        auto mesh = *deformMesh;
        mesh.vertices[static_cast<size_t>(index)].target.x += delta.x;
        mesh.vertices[static_cast<size_t>(index)].target.y += delta.y;
        mesh.recomputeInteriorFromBoundary();
        deformMesh = mesh;
    }

    void translateDeformMesh(const Vector2& delta)
    {
        if (!deformMesh.has_value()) {
            return;
        }
        auto mesh = *deformMesh;
        for (auto& vertex : mesh.vertices) {
            vertex.target.x += delta.x;
            vertex.target.y += delta.y;
        }
        deformMesh = mesh;
    }

    bool sampleDeformMeshForward(const Vector2& p, Vector2& out) const
    {
        if (!deformMesh.has_value()) {
            return false;
        }
        float u = (contentBounds.width > 0.001f)
            ? std::clamp((p.x - contentBounds.left()) / contentBounds.width, 0.0f, 1.0f)
            : 0.5f;
        float v = (contentBounds.height > 0.001f)
            ? std::clamp((p.y - contentBounds.top()) / contentBounds.height, 0.0f, 1.0f)
            : 0.5f;
        out = evaluateBSplineSurface(u, v);
        return true;
    }

    static float pointSegmentDistance(const Vector2& p, const Vector2& a, const Vector2& b)
    {
        Vector2 ab = { b.x - a.x, b.y - a.y };
        Vector2 ap = { p.x - a.x, p.y - a.y };
        float abLenSq = ab.x * ab.x + ab.y * ab.y;
        if (abLenSq < 0.000001f) {
            float dx = p.x - a.x;
            float dy = p.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }
        float t = (ap.x * ab.x + ap.y * ab.y) / abLenSq;
        t = std::clamp(t, 0.0f, 1.0f);
        Vector2 closest = { a.x + ab.x * t, a.y + ab.y * t };
        float dx = p.x - closest.x;
        float dy = p.y - closest.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    /// Check if quad is valid (non-degenerate, convex). Used for soft rejection in UI.
    static bool isQuadValid(const std::array<Vector2, 4>& q, float minArea = 0.001f)
    {
        float area = 0.5f
            * std::abs((q[0].x * q[1].y - q[1].x * q[0].y) + (q[1].x * q[2].y - q[2].x * q[1].y)
                + (q[2].x * q[3].y - q[3].x * q[2].y) + (q[3].x * q[0].y - q[0].x * q[3].y));
        if (area < minArea)
            return false;
        auto cross = [](const Vector2& a, const Vector2& b) { return a.x * b.y - a.y * b.x; };
        Vector2 e0 = { q[1].x - q[0].x, q[1].y - q[0].y };
        Vector2 e1 = { q[2].x - q[1].x, q[2].y - q[1].y };
        Vector2 e2 = { q[3].x - q[2].x, q[3].y - q[2].y };
        Vector2 e3 = { q[0].x - q[3].x, q[0].y - q[3].y };
        float c0 = cross(e0, e1), c1 = cross(e1, e2), c2 = cross(e2, e3), c3 = cross(e3, e0);
        if ((c0 >= 0) != (c1 >= 0) || (c1 >= 0) != (c2 >= 0) || (c2 >= 0) != (c3 >= 0))
            return false;
        return true;
    }

    /// True if quad is self-intersecting (bowtie).
    static bool isQuadBowtie(const std::array<Vector2, 4>& q)
    {
        auto cross = [](const Vector2& a, const Vector2& b) { return a.x * b.y - a.y * b.x; };
        Vector2 e0 = { q[1].x - q[0].x, q[1].y - q[0].y };
        Vector2 e1 = { q[2].x - q[1].x, q[2].y - q[1].y };
        Vector2 e2 = { q[3].x - q[2].x, q[3].y - q[2].y };
        Vector2 e3 = { q[0].x - q[3].x, q[0].y - q[3].y };
        float c0 = cross(e0, e1), c1 = cross(e1, e2), c2 = cross(e2, e3), c3 = cross(e3, e0);
        return (c0 >= 0) != (c1 >= 0) || (c1 >= 0) != (c2 >= 0) || (c2 >= 0) != (c3 >= 0);
    }

    /// Compute 3x3 homography matrix (column-major) mapping world quad -> unit square (s,t).
    static std::array<float, 9> computeQuadToUnitSquareHomography(const std::array<Vector2, 4>& q)
    {
        const bool bowtie = isQuadBowtie(q);
        float u2 = bowtie ? 0.0f : 1.0f;
        float v2 = 1.0f;
        float u3 = bowtie ? 1.0f : 0.0f;
        float v3 = 1.0f;
        float A[8][8];
        float b[8];
        A[0][0] = q[0].x;
        A[0][1] = q[0].y;
        A[0][2] = 1;
        A[0][3] = 0;
        A[0][4] = 0;
        A[0][5] = 0;
        A[0][6] = 0;
        A[0][7] = 0;
        b[0] = 0;
        A[1][0] = 0;
        A[1][1] = 0;
        A[1][2] = 0;
        A[1][3] = q[0].x;
        A[1][4] = q[0].y;
        A[1][5] = 1;
        A[1][6] = 0;
        A[1][7] = 0;
        b[1] = 0;
        A[2][0] = q[1].x;
        A[2][1] = q[1].y;
        A[2][2] = 1;
        A[2][3] = 0;
        A[2][4] = 0;
        A[2][5] = 0;
        A[2][6] = -q[1].x;
        A[2][7] = -q[1].y;
        b[2] = 1;
        A[3][0] = 0;
        A[3][1] = 0;
        A[3][2] = 0;
        A[3][3] = q[1].x;
        A[3][4] = q[1].y;
        A[3][5] = 1;
        A[3][6] = 0;
        A[3][7] = 0;
        b[3] = 0;
        A[4][0] = q[2].x;
        A[4][1] = q[2].y;
        A[4][2] = 1;
        A[4][3] = 0;
        A[4][4] = 0;
        A[4][5] = 0;
        A[4][6] = -q[2].x * u2;
        A[4][7] = -q[2].y * u2;
        b[4] = u2;
        A[5][0] = 0;
        A[5][1] = 0;
        A[5][2] = 0;
        A[5][3] = q[2].x;
        A[5][4] = q[2].y;
        A[5][5] = 1;
        A[5][6] = -q[2].x * v2;
        A[5][7] = -q[2].y * v2;
        b[5] = v2;
        A[6][0] = q[3].x;
        A[6][1] = q[3].y;
        A[6][2] = 1;
        A[6][3] = 0;
        A[6][4] = 0;
        A[6][5] = 0;
        A[6][6] = -q[3].x * u3;
        A[6][7] = -q[3].y * u3;
        b[6] = u3;
        A[7][0] = 0;
        A[7][1] = 0;
        A[7][2] = 0;
        A[7][3] = q[3].x;
        A[7][4] = q[3].y;
        A[7][5] = 1;
        A[7][6] = -q[3].x * v3;
        A[7][7] = -q[3].y * v3;
        b[7] = v3;
        for (int col = 0; col < 8; ++col) {
            int pivot = col;
            for (int r = col + 1; r < 8; ++r)
                if (std::abs(A[r][col]) > std::abs(A[pivot][col]))
                    pivot = r;
            if (std::abs(A[pivot][col]) < 1e-12f) {
                return { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
            }
            for (int k = 0; k < 8; ++k)
                std::swap(A[col][k], A[pivot][k]);
            std::swap(b[col], b[pivot]);
            float inv = 1.0f / A[col][col];
            for (int k = 0; k < 8; ++k)
                A[col][k] *= inv;
            b[col] *= inv;
            for (int r = 0; r < 8; ++r) {
                if (r == col)
                    continue;
                float fac = A[r][col];
                for (int k = 0; k < 8; ++k)
                    A[r][k] -= fac * A[col][k];
                b[r] -= fac * b[col];
            }
        }
        return { b[0], b[3], b[6], b[1], b[4], b[7], b[2], b[5], 1.0f };
    }

    /// Inverse bilinear: find (s,t) in [0,1]^2 such that P = bilinear(s,t) on quad q.
    static bool inverseBilinear(const Vector2& P, const std::array<Vector2, 4>& q, float st[2])
    {
        float Ex = q[1].x - q[0].x, Ey = q[1].y - q[0].y;
        float Fx = q[3].x - q[0].x, Fy = q[3].y - q[0].y;
        float Gx = q[0].x - q[1].x + q[2].x - q[3].x;
        float Gy = q[0].y - q[1].y + q[2].y - q[3].y;
        float hx = P.x - q[0].x, hy = P.y - q[0].y;
        float k2 = Gx * Fy - Gy * Fx;
        float k1 = Ex * Fy - Ey * Fx + hx * Gy - hy * Gx;
        float k0 = hx * Ey - hy * Ex;
        auto tryComputeS = [&](float t_val, float& s_out) -> bool {
            float denomX = Ex + Gx * t_val;
            float denomY = Ey + Gy * t_val;
            if (std::abs(denomX) > std::abs(denomY)) {
                if (std::abs(denomX) < 1e-10f)
                    return false;
                s_out = (hx - Fx * t_val) / denomX;
            } else {
                if (std::abs(denomY) < 1e-10f)
                    return false;
                s_out = (hy - Fy * t_val) / denomY;
            }
            return true;
        };
        constexpr float margin = 0.002f;
        float s, t;
        float disc = k1 * k1 - 4.0f * k0 * k2;
        if (disc < 0.0f)
            return false;
        disc = std::sqrt(disc);
        float signK1 = (k1 >= 0.0f) ? 1.0f : -1.0f;
        float q_stable = -0.5f * (k1 + signK1 * disc);
        float t_candidates[2];
        int nCandidates = 0;
        if (std::abs(k2) > 1e-10f)
            t_candidates[nCandidates++] = q_stable / k2;
        if (std::abs(q_stable) > 1e-10f)
            t_candidates[nCandidates++] = k0 / q_stable;
        if (nCandidates == 0) {
            if (std::abs(k1) < 1e-10f)
                return false;
            t_candidates[nCandidates++] = -k0 / k1;
        }
        bool found = false;
        for (int i = 0; i < nCandidates; ++i) {
            t = t_candidates[i];
            if (t < -margin || t > 1.0f + margin)
                continue;
            if (!tryComputeS(t, s))
                continue;
            if (s < -margin || s > 1.0f + margin)
                continue;
            found = true;
            break;
        }
        if (!found)
            return false;
        st[0] = std::clamp(s, 0.0f, 1.0f);
        st[1] = std::clamp(t, 0.0f, 1.0f);
        return true;
    }

    // ---- Compute content bounds from a TileGrid (pixel-level) ----
    static Rect computeContentBounds(const TileGrid& grid)
    {
        if (grid.empty())
            return {};

        float globalMinX = std::numeric_limits<float>::max();
        float globalMinY = std::numeric_limits<float>::max();
        float globalMaxX = std::numeric_limits<float>::lowest();
        float globalMaxY = std::numeric_limits<float>::lowest();

        bool hasContent = false;

        for (const auto& [key, tile] : grid.tiles()) {
            float tileOriginX = static_cast<float>(key.x) * TILE_SIZE;
            float tileOriginY = static_cast<float>(key.y) * TILE_SIZE;

            // Scan for non-transparent pixels in this tile
            int tileMinX = TILE_SIZE, tileMinY = TILE_SIZE;
            int tileMaxX = -1, tileMaxY = -1;

            const uint8_t* pixels = tile.pixels();
            const TilePixelFormat fmt = tile.format();
            for (uint32_t y = 0; y < TILE_SIZE; ++y) {
                for (uint32_t x = 0; x < TILE_SIZE; ++x) {
                    // Format-aware alpha test (8-bit stride/[idx+3] was wrong for
                    // 16F/32F tiles → mis-scanned buffer → clipped content bounds).
                    if (!tilePixelAlphaIsZero(pixels, fmt, x, y)) {
                        tileMinX = std::min(tileMinX, static_cast<int>(x));
                        tileMinY = std::min(tileMinY, static_cast<int>(y));
                        tileMaxX = std::max(tileMaxX, static_cast<int>(x));
                        tileMaxY = std::max(tileMaxY, static_cast<int>(y));
                    }
                }
            }

            if (tileMaxX >= 0) {
                hasContent = true;
                globalMinX = std::min(globalMinX, tileOriginX + tileMinX);
                globalMinY = std::min(globalMinY, tileOriginY + tileMinY);
                globalMaxX = std::max(globalMaxX, tileOriginX + tileMaxX + 1.0f);
                globalMaxY = std::max(globalMaxY, tileOriginY + tileMaxY + 1.0f);
            }
        }

        if (!hasContent)
            return {};

        return { globalMinX, globalMinY, globalMaxX - globalMinX, globalMaxY - globalMinY };
    }
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMSTATE_H
