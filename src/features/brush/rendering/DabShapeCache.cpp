// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   D A B   S H A P E   C A C H E
// ==========================================================================

#include "features/brush/rendering/DabShapeCache.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <cmath>

namespace aether {

namespace {

constexpr float kDistanceInf = 1.0e20f;

void distanceTransform1D(const std::vector<float>& input, int count, std::vector<float>& output)
{
    std::vector<int> v(static_cast<size_t>(count));
    std::vector<float> z(static_cast<size_t>(count) + 1u);

    int k = 0;
    v[0] = 0;
    z[0] = -kDistanceInf;
    z[1] = kDistanceInf;

    for (int q = 1; q < count; ++q) {
        float s = 0.0f;
        while (true) {
            const int vk = v[k];
            s = ((input[static_cast<size_t>(q)] + static_cast<float>(q * q))
                    - (input[static_cast<size_t>(vk)] + static_cast<float>(vk * vk)))
                / static_cast<float>(2 * (q - vk));
            if (s > z[static_cast<size_t>(k)] || k == 0) {
                break;
            }
            --k;
        }
        if (s <= z[static_cast<size_t>(k)]) {
            s = -kDistanceInf;
        } else {
            ++k;
        }
        v[static_cast<size_t>(k)] = q;
        z[static_cast<size_t>(k)] = s;
        z[static_cast<size_t>(k + 1)] = kDistanceInf;
    }

    k = 0;
    for (int q = 0; q < count; ++q) {
        while (z[static_cast<size_t>(k + 1)] < static_cast<float>(q)) {
            ++k;
        }
        const float delta = static_cast<float>(q - v[static_cast<size_t>(k)]);
        output[static_cast<size_t>(q)]
            = delta * delta + input[static_cast<size_t>(v[static_cast<size_t>(k)])];
    }
}

std::vector<float> buildDistanceToMask(
    const std::vector<uint8_t>& alpha, int width, int height, bool featureInside)
{
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<float> out(pixelCount, 0.0f);
    if (alpha.empty() || width <= 0 || height <= 0) {
        return out;
    }

    std::vector<float> rowInput(static_cast<size_t>(width));
    std::vector<float> rowOutput(static_cast<size_t>(width));
    std::vector<float> temp(pixelCount, 0.0f);

    for (int y = 0; y < height; ++y) {
        const size_t rowBase = static_cast<size_t>(y) * static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            const bool inside = alpha[rowBase + static_cast<size_t>(x)] > 0;
            rowInput[static_cast<size_t>(x)] = (inside == featureInside) ? 0.0f : kDistanceInf;
        }
        distanceTransform1D(rowInput, width, rowOutput);
        for (int x = 0; x < width; ++x) {
            temp[rowBase + static_cast<size_t>(x)] = rowOutput[static_cast<size_t>(x)];
        }
    }

    std::vector<float> colInput(static_cast<size_t>(height));
    std::vector<float> colOutput(static_cast<size_t>(height));

    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            colInput[static_cast<size_t>(y)]
                = temp[static_cast<size_t>(y) * static_cast<size_t>(width)
                    + static_cast<size_t>(x)];
        }
        distanceTransform1D(colInput, height, colOutput);
        for (int y = 0; y < height; ++y) {
            const size_t idx
                = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            out[idx] = std::sqrt(std::max(colOutput[static_cast<size_t>(y)], 0.0f));
        }
    }

    return out;
}

std::vector<uint8_t> buildEdgeDistanceField(
    const std::vector<uint8_t>& alpha, int width, int height)
{
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<uint8_t> out(pixelCount, 0);
    if (alpha.empty() || width <= 0 || height <= 0) {
        return out;
    }

    const std::vector<float> distanceToInside = buildDistanceToMask(alpha, width, height, true);
    const std::vector<float> distanceToOutside = buildDistanceToMask(alpha, width, height, false);
    const float texelScale = 2.0f / static_cast<float>(std::max(width, height));

    for (size_t idx = 0; idx < pixelCount; ++idx) {
        const bool inside = alpha[idx] > 0;
        const float distancePx = inside ? distanceToOutside[idx] : distanceToInside[idx];
        const float distanceNorm = std::clamp(distancePx * texelScale, 0.0f, 1.0f);
        out[idx] = static_cast<uint8_t>(std::round(distanceNorm * 255.0f));
    }

    return out;
}

} // namespace

DabShapeCache& DabShapeCache::instance()
{
    static DabShapeCache cache;
    return cache;
}

void DabShapeCache::loadFromResources()
{
    if (m_loaded)
        return;

    for (int i = kMinType; i <= kMaxType; ++i) {
        const QString path = QStringLiteral(":/brushes/%1").arg(i);
        QImage img(path);
        if (img.isNull())
            continue;

        img = img.convertToFormat(QImage::Format_ARGB32);
        const int w = img.width();
        const int h = img.height();
        if (w <= 0 || h <= 0)
            continue;

        std::vector<uint8_t> alpha(static_cast<size_t>(w * h));
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) {
                alpha[static_cast<size_t>(y * w + x)] = static_cast<uint8_t>(qAlpha(row[x]));
            }
        }

        const int idx = i - kMinType;
        m_shapes[idx].alpha = std::move(alpha);
        m_shapes[idx].edgeDistance = buildEdgeDistanceField(m_shapes[idx].alpha, w, h);
        m_shapes[idx].width = w;
        m_shapes[idx].height = h;
    }
    m_loaded = true;
}

DabShapeCache::AlphaGrid DabShapeCache::getAlphaGrid(int dabType)
{
    AlphaGrid out;
    if (dabType < kMinType || dabType > kMaxType)
        return out;

    if (!m_loaded)
        loadFromResources();

    const int idx = dabType - kMinType;
    const Shape& s = m_shapes[idx];
    if (s.alpha.empty())
        return out;

    out.data = s.alpha;
    out.edgeDistance = s.edgeDistance;
    out.width = s.width;
    out.height = s.height;
    return out;
}

void DabShapeCache::getShapeSize(int dabType, int& width, int& height)
{
    width = 0;
    height = 0;
    if (dabType < kMinType || dabType > kMaxType) {
        return;
    }

    if (!m_loaded) {
        loadFromResources();
    }

    const int idx = dabType - kMinType;
    const Shape& s = m_shapes[idx];
    if (s.alpha.empty() || s.width <= 0 || s.height <= 0) {
        return;
    }

    width = s.width;
    height = s.height;
}

GLuint DabShapeCache::getTextureId(QOpenGLFunctions_4_5_Core* gl, int dabType)
{
    if (!gl || dabType < kMinType || dabType > kMaxType)
        return 0;

    if (!m_loaded)
        loadFromResources();

    const int idx = dabType - kMinType;
    Shape& s = m_shapes[idx];
    if (s.alpha.empty())
        return 0;

    if (s.textureId != 0)
        return s.textureId;

    gl->glGenTextures(1, &s.textureId);
    if (s.textureId == 0)
        return 0;

    gl->glBindTexture(GL_TEXTURE_2D, s.textureId);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::vector<uint8_t> textureData(
        static_cast<size_t>(s.width) * static_cast<size_t>(s.height) * 2u, 0);
    for (size_t i = 0; i < s.alpha.size(); ++i) {
        textureData[i * 2u + 0u] = s.alpha[i];
        textureData[i * 2u + 1u] = s.edgeDistance.empty() ? 0 : s.edgeDistance[i];
    }

    GLint prevUnpackAlignment = 4;
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, s.width, s.height, 0, GL_RG, GL_UNSIGNED_BYTE,
        textureData.data());
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);

    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return s.textureId;
}

void DabShapeCache::releaseTextures(QOpenGLFunctions_4_5_Core* gl)
{
    if (!gl)
        return;
    for (int i = 0; i <= kMaxType - kMinType; ++i) {
        if (m_shapes[i].textureId != 0) {
            gl->glDeleteTextures(1, &m_shapes[i].textureId);
            m_shapes[i].textureId = 0;
        }
    }
    for (auto& entry : m_customShapes) {
        if (entry.second.textureId != 0) {
            gl->glDeleteTextures(1, &entry.second.textureId);
            entry.second.textureId = 0;
        }
    }
}

QString DabShapeCache::makeCustomKey(
    const QString& imagePath, float threshold, float compression, int interpolation)
{
    QByteArray raw;
    raw.reserve(imagePath.size() * 2 + 64);
    raw.append(imagePath.toUtf8());
    raw.append('|');
    const QFileInfo info(imagePath);
    raw.append(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    raw.append('|');
    raw.append(QByteArray::number(static_cast<qlonglong>(info.size())));
    raw.append('|');
    raw.append(QByteArray::number(static_cast<double>(threshold), 'g', 6));
    raw.append('|');
    raw.append(QByteArray::number(static_cast<double>(compression), 'g', 6));
    raw.append('|');
    raw.append(QByteArray::number(interpolation));
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toHex());
}

QString DabShapeCache::makeCustomRequestKey(
    const QString& imagePath, float threshold, float compression, int interpolation)
{
    QByteArray raw;
    raw.reserve(imagePath.size() * 2 + 48);
    raw.append(imagePath.toUtf8());
    raw.append('|');
    raw.append(QByteArray::number(static_cast<double>(threshold), 'g', 6));
    raw.append('|');
    raw.append(QByteArray::number(static_cast<double>(compression), 'g', 6));
    raw.append('|');
    raw.append(QByteArray::number(interpolation));
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toHex());
}

DabShapeCache::Shape* DabShapeCache::ensureCustomShapeLoaded(const QString& imagePath,
    float threshold, float compression, int interpolation, bool useFastRequestCache)
{
    if (imagePath.isEmpty())
        return nullptr;

    const std::string requestKey
        = makeCustomRequestKey(imagePath, threshold, compression, interpolation).toStdString();
    if (useFastRequestCache) {
        const auto requestIt = m_customRequestShapeKeys.find(requestKey);
        if (requestIt != m_customRequestShapeKeys.end()) {
            auto shapeIt = m_customShapes.find(requestIt->second);
            if (shapeIt != m_customShapes.end()) {
                return &shapeIt->second;
            }
            m_customRequestShapeKeys.erase(requestIt);
        }
    }

    const QString keyStr = makeCustomKey(imagePath, threshold, compression, interpolation);
    const std::string key = keyStr.toStdString();

    auto it = m_customShapes.find(key);
    if (it != m_customShapes.end()) {
        m_customRequestShapeKeys[requestKey] = key;
        return &it->second;
    }

    QImage img(imagePath);
    if (img.isNull())
        return nullptr;

    img = img.convertToFormat(QImage::Format_ARGB32);
    const int srcW = img.width();
    const int srcH = img.height();
    if (srcW <= 0 || srcH <= 0)
        return nullptr;

    constexpr int kMaxCustomDabExtent = 2048;
    const float maxDim = static_cast<float>(std::max(srcW, srcH));
    const float extentCap
        = (maxDim > kMaxCustomDabExtent) ? static_cast<float>(kMaxCustomDabExtent) / maxDim : 1.0f;
    const float compressionFactor = std::clamp(compression, 0.05f, 1.0f) * extentCap;
    const int dstW = std::max(4, static_cast<int>(std::round(srcW * compressionFactor)));
    const int dstH = std::max(4, static_cast<int>(std::round(srcH * compressionFactor)));

    QImage resized = (dstW == srcW && dstH == srcH)
        ? img
        : img.scaled(dstW, dstH, Qt::IgnoreAspectRatio,
              interpolation == 1 ? Qt::FastTransformation : Qt::SmoothTransformation);

    const int w = resized.width();
    const int h = resized.height();
    std::vector<uint8_t> alpha(static_cast<size_t>(w) * static_cast<size_t>(h));

    const float thresh = std::clamp(threshold, 0.0f, 1.0f);
    const float invRange = (thresh >= 1.0f) ? 0.0f : 1.0f / (1.0f - thresh);

    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(resized.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb pixel = row[x];
            const float a = static_cast<float>(qAlpha(pixel)) / 255.0f;
            const float lum
                = (0.299f * qRed(pixel) + 0.587f * qGreen(pixel) + 0.114f * qBlue(pixel)) / 255.0f;
            const float gray = lum * a;
            float mapped = 0.0f;
            if (gray > thresh) {
                mapped = (gray - thresh) * invRange;
            }
            mapped = std::clamp(mapped, 0.0f, 1.0f);
            alpha[static_cast<size_t>(y * w + x)]
                = static_cast<uint8_t>(std::round(mapped * 255.0f));
        }
    }

    Shape shape;
    shape.alpha = std::move(alpha);
    shape.edgeDistance = buildEdgeDistanceField(shape.alpha, w, h);
    shape.width = w;
    shape.height = h;

    auto [inserted, _] = m_customShapes.emplace(key, std::move(shape));
    m_customRequestShapeKeys[requestKey] = key;
    return &inserted->second;
}

DabShapeCache::AlphaGrid DabShapeCache::getCustomAlphaGrid(
    const QString& imagePath, float threshold, float compression, int interpolation)
{
    AlphaGrid out;
    Shape* shape = ensureCustomShapeLoaded(imagePath, threshold, compression, interpolation, false);
    if (!shape || shape->alpha.empty())
        return out;
    out.data = shape->alpha;
    out.edgeDistance = shape->edgeDistance;
    out.width = shape->width;
    out.height = shape->height;
    return out;
}

void DabShapeCache::getCustomShapeSize(const QString& imagePath, float threshold, float compression,
    int interpolation, int& width, int& height)
{
    width = 0;
    height = 0;
    Shape* shape = ensureCustomShapeLoaded(imagePath, threshold, compression, interpolation);
    if (!shape || shape->alpha.empty())
        return;
    width = shape->width;
    height = shape->height;
}

GLuint DabShapeCache::getCustomTextureId(QOpenGLFunctions_4_5_Core* gl, const QString& imagePath,
    float threshold, float compression, int interpolation)
{
    if (!gl)
        return 0;
    Shape* shape = ensureCustomShapeLoaded(imagePath, threshold, compression, interpolation);
    if (!shape || shape->alpha.empty())
        return 0;
    if (shape->textureId != 0)
        return shape->textureId;

    gl->glGenTextures(1, &shape->textureId);
    if (shape->textureId == 0)
        return 0;

    gl->glBindTexture(GL_TEXTURE_2D, shape->textureId);
    const GLint filter = (interpolation == 1) ? GL_NEAREST : GL_LINEAR;
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::vector<uint8_t> textureData(
        static_cast<size_t>(shape->width) * static_cast<size_t>(shape->height) * 2u, 0);
    for (size_t i = 0; i < shape->alpha.size(); ++i) {
        textureData[i * 2u + 0u] = shape->alpha[i];
        textureData[i * 2u + 1u] = shape->edgeDistance.empty() ? 0 : shape->edgeDistance[i];
    }

    GLint prevUnpackAlignment = 4;
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, shape->width, shape->height, 0, GL_RG,
        GL_UNSIGNED_BYTE, textureData.data());
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);

    gl->glBindTexture(GL_TEXTURE_2D, 0);
    return shape->textureId;
}

} // namespace aether
