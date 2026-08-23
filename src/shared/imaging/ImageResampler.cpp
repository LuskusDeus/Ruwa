// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   I M A G E   R E S A M P L E R
// ==========================================================================

#include "shared/imaging/ImageResampler.h"

#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cmath>
#include <new>
#include <vector>

namespace ruwa::shared::imaging {

namespace {

// ---------------------------------------------------------------------------
//   K E R N E L S
// ---------------------------------------------------------------------------

constexpr float kPi = 3.14159265358979323846f;

/// Mitchell-Netravali family. Catmull-Rom is (B=0, C=0.5); the filter the UI
/// calls "Mitchell" is the (1/3, 1/3) member the paper recommends.
float mitchellFamily(float x, float B, float C)
{
    x = std::fabs(x);
    const float x2 = x * x;
    const float x3 = x2 * x;

    if (x < 1.0f) {
        return ((12.0f - 9.0f * B - 6.0f * C) * x3 + (-18.0f + 12.0f * B + 6.0f * C) * x2
                   + (6.0f - 2.0f * B))
            / 6.0f;
    }
    if (x < 2.0f) {
        return ((-B - 6.0f * C) * x3 + (6.0f * B + 30.0f * C) * x2 + (-12.0f * B - 48.0f * C) * x
                   + (8.0f * B + 24.0f * C))
            / 6.0f;
    }
    return 0.0f;
}

float sincFn(float x)
{
    if (std::fabs(x) < 1e-6f) {
        return 1.0f;
    }
    const float px = kPi * x;
    return std::sin(px) / px;
}

float kernelValue(ResampleFilter filter, float t)
{
    switch (filter) {
    case ResampleFilter::Nearest:
        return std::fabs(t) <= 0.5f ? 1.0f : 0.0f;
    case ResampleFilter::Bilinear: {
        const float x = std::fabs(t);
        return x < 1.0f ? 1.0f - x : 0.0f;
    }
    case ResampleFilter::Bicubic:
        return mitchellFamily(t, 0.0f, 0.5f);
    case ResampleFilter::Mitchell:
        return mitchellFamily(t, 1.0f / 3.0f, 1.0f / 3.0f);
    case ResampleFilter::Lanczos3: {
        const float x = std::fabs(t);
        return x < 3.0f ? sincFn(x) * sincFn(x / 3.0f) : 0.0f;
    }
    }
    return 0.0f;
}

float kernelRadius(ResampleFilter filter)
{
    switch (filter) {
    case ResampleFilter::Nearest:
        return 0.5f;
    case ResampleFilter::Bilinear:
        return 1.0f;
    case ResampleFilter::Bicubic:
    case ResampleFilter::Mitchell:
        return 2.0f;
    case ResampleFilter::Lanczos3:
        return 3.0f;
    }
    return 1.0f;
}

/// Whether the kernel support scales up when minifying. See the note on
/// ResampleFilter::Nearest for why exactly one filter answers no.
bool kernelWidensOnMinify(ResampleFilter filter)
{
    return filter != ResampleFilter::Nearest;
}

// ---------------------------------------------------------------------------
//   W E I G H T   T A B L E
// ---------------------------------------------------------------------------
//
//   One entry per OUTPUT sample: the clamped source indices it reads and the
//   normalized weights it reads them with. Padded to a fixed stride so a tap
//   lookup is arithmetic rather than an indirection.

struct AxisWeights {
    int dstCount = 0;
    int maxTaps = 0;
    std::vector<int> tapCount;
    std::vector<int> index; ///< dstCount * maxTaps, non-decreasing within a row
    std::vector<float> weight; ///< dstCount * maxTaps

    [[nodiscard]] int firstSource(int dstIndex) const
    {
        return index[static_cast<size_t>(dstIndex) * maxTaps];
    }
    [[nodiscard]] int lastSource(int dstIndex) const
    {
        return index[static_cast<size_t>(dstIndex) * maxTaps + tapCount[dstIndex] - 1];
    }
};

bool buildAxisWeights(int srcCount, int dstCount, ResampleFilter filter, AxisWeights& out)
{
    if (srcCount <= 0 || dstCount <= 0) {
        return false;
    }

    const double scale = static_cast<double>(dstCount) / static_cast<double>(srcCount);
    // Minifying spreads one output sample over many source samples, so the
    // kernel is stretched by 1/scale and its weights re-normalized. Magnifying
    // leaves the kernel at its natural width.
    const double filterScale = (scale < 1.0 && kernelWidensOnMinify(filter)) ? 1.0 / scale : 1.0;
    const double support = kernelRadius(filter) * filterScale;

    const int maxTaps = std::max(1, static_cast<int>(std::ceil(support * 2.0)) + 2);

    out.dstCount = dstCount;
    out.maxTaps = maxTaps;
    try {
        out.tapCount.assign(static_cast<size_t>(dstCount), 0);
        out.index.assign(static_cast<size_t>(dstCount) * maxTaps, 0);
        out.weight.assign(static_cast<size_t>(dstCount) * maxTaps, 0.0f);
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }

    std::vector<float> raw;
    raw.reserve(static_cast<size_t>(maxTaps));

    for (int i = 0; i < dstCount; ++i) {
        // Center of output sample i expressed in source coordinates, using the
        // pixel-center convention (sample i covers [i, i+1), center i + 0.5).
        const double center = (static_cast<double>(i) + 0.5) / scale - 0.5;

        int first = static_cast<int>(std::ceil(center - support));
        int last = static_cast<int>(std::floor(center + support));
        if (last < first) {
            // Possible for a zero-width support (Nearest landing on an exact
            // .5 boundary). Fall back to the single closest source sample.
            first = last = static_cast<int>(std::lround(center));
        }
        if (last - first + 1 > maxTaps) {
            last = first + maxTaps - 1;
        }

        raw.clear();
        double sum = 0.0;
        for (int j = first; j <= last; ++j) {
            const float w = kernelValue(filter, static_cast<float>((j - center) / filterScale));
            raw.push_back(w);
            sum += w;
        }

        const size_t base = static_cast<size_t>(i) * maxTaps;

        if (sum == 0.0) {
            // Degenerate window (every tap landed on a kernel zero). One
            // nearest sample at full weight keeps the output defined.
            const int nearest = std::clamp(static_cast<int>(std::lround(center)), 0, srcCount - 1);
            out.tapCount[i] = 1;
            out.index[base] = nearest;
            out.weight[base] = 1.0f;
            continue;
        }

        const float invSum = static_cast<float>(1.0 / sum);
        int taps = 0;
        for (int j = first; j <= last; ++j) {
            // Clamp-to-edge: taps off the edge fold onto the border pixel.
            // Because j walks upward and clamping is monotone, the stored
            // indices stay non-decreasing, which firstSource()/lastSource()
            // rely on to report a band source span in O(1).
            out.index[base + taps] = std::clamp(j, 0, srcCount - 1);
            out.weight[base + taps] = raw[static_cast<size_t>(taps)] * invSum;
            ++taps;
        }
        out.tapCount[i] = taps;
    }

    return true;
}

// ---------------------------------------------------------------------------
//   P A S S E S
// ---------------------------------------------------------------------------

void premultiplyRow(float* row, int count)
{
    for (int i = 0; i < count; ++i) {
        float* p = row + static_cast<size_t>(i) * kPixelChannels;
        const float a = p[3];
        p[0] *= a;
        p[1] *= a;
        p[2] *= a;
    }
}

/// Kernel overshoot can push a channel below zero or alpha above one. Neither
/// is representable in any storage this ends up in, and a premultiplied buffer
/// that is handed on as-is still has to satisfy its own invariants.
void sanitizePremultipliedRow(float* row, int count)
{
    for (int i = 0; i < count; ++i) {
        float* p = row + static_cast<size_t>(i) * kPixelChannels;
        p[0] = std::max(0.0f, p[0]);
        p[1] = std::max(0.0f, p[1]);
        p[2] = std::max(0.0f, p[2]);
        p[3] = std::clamp(p[3], 0.0f, 1.0f);
    }
}

void unpremultiplyRow(float* row, int count)
{
    for (int i = 0; i < count; ++i) {
        float* p = row + static_cast<size_t>(i) * kPixelChannels;
        const float a = std::clamp(p[3], 0.0f, 1.0f);
        if (a <= 0.0f) {
            p[0] = 0.0f;
            p[1] = 0.0f;
            p[2] = 0.0f;
            p[3] = 0.0f;
            continue;
        }
        const float inv = 1.0f / a;
        // Kernel overshoot can push a channel slightly negative; a negative
        // color is meaningless in every storage this ends up in.
        p[0] = std::max(0.0f, p[0]) * inv;
        p[1] = std::max(0.0f, p[1]) * inv;
        p[2] = std::max(0.0f, p[2]) * inv;
        p[3] = a;
    }
}

/// One source row in, premultiplied and horizontally filtered out. A source
/// that is already premultiplied skips the first step rather than being divided
/// and re-multiplied. `scratch` must hold src.width() * 4 floats.
void filterRowHorizontally(const PixelSurface& src, int srcRow, const AxisWeights& wx,
    bool premultiplyFirst, float* dst, float* scratch)
{
    src.readRowFloat(srcRow, scratch);
    if (premultiplyFirst) {
        premultiplyRow(scratch, src.width());
    }

    for (int x = 0; x < wx.dstCount; ++x) {
        const size_t base = static_cast<size_t>(x) * wx.maxTaps;
        const int taps = wx.tapCount[x];

        float acc[kPixelChannels] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int t = 0; t < taps; ++t) {
            const float w = wx.weight[base + t];
            const float* p = scratch + static_cast<size_t>(wx.index[base + t]) * kPixelChannels;
            acc[0] += p[0] * w;
            acc[1] += p[1] * w;
            acc[2] += p[2] * w;
            acc[3] += p[3] * w;
        }

        float* out = dst + static_cast<size_t>(x) * kPixelChannels;
        out[0] = acc[0];
        out[1] = acc[1];
        out[2] = acc[2];
        out[3] = acc[3];
    }
}

} // anonymous namespace

const char* resampleFilterName(ResampleFilter filter)
{
    switch (filter) {
    case ResampleFilter::Nearest:
        return "Nearest";
    case ResampleFilter::Bilinear:
        return "Bilinear";
    case ResampleFilter::Bicubic:
        return "Bicubic";
    case ResampleFilter::Mitchell:
        return "Mitchell";
    case ResampleFilter::Lanczos3:
        return "Lanczos3";
    }
    return "Bicubic";
}

PixelSurface resample(const PixelSurface& src, QSize targetSize, const ResampleOptions& options,
    const ResampleHooks& hooks)
{
    if (src.isNull() || targetSize.width() <= 0 || targetSize.height() <= 0) {
        return {};
    }

    // 1:1 is a copy, not a filter pass. Running Lanczos over an unscaled image
    // would ring its edges for no reason, and "Scale 100%" must be lossless.
    if (targetSize == src.size()) {
        if (hooks.onProgress) {
            hooks.onProgress(1.0);
        }
        return src.convertedTo(src.storage());
    }

    const int srcW = src.width();
    const int srcH = src.height();
    const int dstW = targetSize.width();
    const int dstH = targetSize.height();

    AxisWeights wx;
    AxisWeights wy;
    if (!buildAxisWeights(srcW, dstW, options.filter, wx)
        || !buildAxisWeights(srcH, dstH, options.filter, wy)) {
        return {};
    }

    // Filtering is only correct on premultiplied color. A straight-alpha source
    // is converted on the way in and back on the way out; a premultiplied one
    // passes through untouched and stays premultiplied, which is what lets the
    // whole export pipeline unpremultiply exactly once, in the encoder.
    const bool premultiplyFirst = src.alphaMode() == PixelAlpha::Straight;

    PixelSurface dst = PixelSurface::create(dstW, dstH, src.storage(), src.alphaMode());
    if (dst.isNull()) {
        return {};
    }

    const qsizetype rowFloats = static_cast<qsizetype>(dstW) * kPixelChannels;
    const qsizetype rowBytes = rowFloats * static_cast<qsizetype>(sizeof(float));

    // Band height: as many output rows as fit the scratch budget, capped so a
    // single band stays a reasonable unit of progress and cancellation.
    constexpr int kMaxBandHeight = 256;
    const double srcRowsPerDstRow = static_cast<double>(srcH) / static_cast<double>(dstH);
    const qsizetype budgetRows = std::max<qsizetype>(1, options.scratchBudgetBytes / rowBytes);
    int bandHeight = 1;
    if (budgetRows > wy.maxTaps) {
        const double spare = static_cast<double>(budgetRows - wy.maxTaps);
        bandHeight = 1 + static_cast<int>(std::floor(spare / std::max(1e-9, srcRowsPerDstRow)));
    }
    bandHeight = std::clamp(bandHeight, 1, std::min(kMaxBandHeight, dstH));

    // Exact worst-case number of source rows a band needs live at once, derived
    // from the weight table rather than estimated, so the cache is never
    // undersized — an undersized ring would silently read a row that had
    // already been overwritten.
    int cacheRows = 1;
    for (int y0 = 0; y0 < dstH; y0 += bandHeight) {
        const int y1 = std::min(dstH, y0 + bandHeight);
        const int span = wy.lastSource(y1 - 1) - wy.firstSource(y0) + 1;
        cacheRows = std::max(cacheRows, span);
    }

    std::vector<float> cache;
    try {
        cache.resize(static_cast<size_t>(cacheRows) * static_cast<size_t>(rowFloats));
    } catch (const std::bad_alloc&) {
        return {};
    } catch (const std::length_error&) {
        return {};
    }

    // Absolute source-row index of the newest row present in the ring buffer;
    // -1 means empty. Bands only ever move forward, so eviction is implicit:
    // slot (row % cacheRows) is reused only once that row is out of every
    // remaining band span, which the cacheRows computation above guarantees.
    int cachedThrough = -1;

    std::vector<int> rowsToFilter;
    std::vector<int> bandRows;

    for (int y0 = 0; y0 < dstH; y0 += bandHeight) {
        if (hooks.shouldCancel && hooks.shouldCancel()) {
            return {};
        }

        const int y1 = std::min(dstH, y0 + bandHeight);
        const int srcFrom = wy.firstSource(y0);
        const int srcTo = wy.lastSource(y1 - 1);

        // A non-contiguous jump can only happen on the first band; keeping the
        // check general costs nothing and makes the invariant explicit.
        if (cachedThrough < srcFrom - 1) {
            cachedThrough = srcFrom - 1;
        }

        rowsToFilter.clear();
        for (int r = cachedThrough + 1; r <= srcTo; ++r) {
            rowsToFilter.push_back(r);
        }

        if (!rowsToFilter.empty()) {
            QtConcurrent::blockingMap(rowsToFilter, [&](int r) {
                std::vector<float> scratch(static_cast<size_t>(srcW) * kPixelChannels);
                float* slot = cache.data() + static_cast<size_t>(r % cacheRows) * rowFloats;
                filterRowHorizontally(src, r, wx, premultiplyFirst, slot, scratch.data());
            });
            cachedThrough = srcTo;
        }

        bandRows.clear();
        for (int y = y0; y < y1; ++y) {
            bandRows.push_back(y);
        }

        QtConcurrent::blockingMap(bandRows, [&](int y) {
            std::vector<float> out(static_cast<size_t>(rowFloats), 0.0f);
            const size_t base = static_cast<size_t>(y) * wy.maxTaps;
            const int taps = wy.tapCount[y];

            for (int t = 0; t < taps; ++t) {
                const float w = wy.weight[base + t];
                const float* row = cache.data()
                    + static_cast<size_t>(wy.index[base + t] % cacheRows) * rowFloats;
                for (qsizetype i = 0; i < rowFloats; ++i) {
                    out[static_cast<size_t>(i)] += row[i] * w;
                }
            }

            if (premultiplyFirst) {
                unpremultiplyRow(out.data(), dstW);
            } else {
                sanitizePremultipliedRow(out.data(), dstW);
            }
            dst.writeRowFloat(y, out.data());
        });

        if (hooks.onProgress) {
            hooks.onProgress(static_cast<qreal>(y1) / static_cast<qreal>(dstH));
        }
    }

    return dst;
}

} // namespace ruwa::shared::imaging
