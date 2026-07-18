// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   D A B   S H A P E   C A C H E
// ==========================================================================

#include "features/brush/rendering/DabShapeCache.h"
#include "shared/tiles/DabShapeFalloff.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <cmath>

namespace aether {

namespace {

void boxBlurHorizontal(
    const std::vector<float>& input, int width, int height, int radius, std::vector<float>& output)
{
    output.assign(input.size(), 0.0f);
    const float invWindow = 1.0f / static_cast<float>(radius * 2 + 1);
    for (int y = 0; y < height; ++y) {
        const size_t row = static_cast<size_t>(y) * static_cast<size_t>(width);
        float sum = 0.0f;
        for (int x = 0; x <= std::min(radius, width - 1); ++x) {
            sum += input[row + static_cast<size_t>(x)];
        }
        for (int x = 0; x < width; ++x) {
            output[row + static_cast<size_t>(x)] = sum * invWindow;
            const int removeX = x - radius;
            const int addX = x + radius + 1;
            if (removeX >= 0) {
                sum -= input[row + static_cast<size_t>(removeX)];
            }
            if (addX < width) {
                sum += input[row + static_cast<size_t>(addX)];
            }
        }
    }
}

void boxBlurVertical(
    const std::vector<float>& input, int width, int height, int radius, std::vector<float>& output)
{
    output.assign(input.size(), 0.0f);
    const float invWindow = 1.0f / static_cast<float>(radius * 2 + 1);
    for (int x = 0; x < width; ++x) {
        float sum = 0.0f;
        for (int y = 0; y <= std::min(radius, height - 1); ++y) {
            sum += input[static_cast<size_t>(y) * static_cast<size_t>(width)
                + static_cast<size_t>(x)];
        }
        for (int y = 0; y < height; ++y) {
            const size_t index
                = static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x);
            output[index] = sum * invWindow;
            const int removeY = y - radius;
            const int addY = y + radius + 1;
            if (removeY >= 0) {
                sum -= input[static_cast<size_t>(removeY) * static_cast<size_t>(width)
                    + static_cast<size_t>(x)];
            }
            if (addY < height) {
                sum += input[static_cast<size_t>(addY) * static_cast<size_t>(width)
                    + static_cast<size_t>(x)];
            }
        }
    }
}

std::vector<uint8_t> buildSoftAlphaMask(const std::vector<uint8_t>& alpha, int width, int height)
{
    if (alpha.empty() || width <= 0 || height <= 0) {
        return {};
    }

    // Three equal box passes closely approximate a Gaussian. Their combined
    // support matches the existing 48-texel hardness feather, while every pass
    // remains linear in the number of pixels and runs only when a dab is loaded.
    constexpr int kBoxPassCount = 3;
    const int radius = std::max(1,
        static_cast<int>(std::round(
            dab_shape_falloff::kSoftEdgeRadiusTexels / static_cast<float>(kBoxPassCount))));

    std::vector<float> current(alpha.size());
    for (size_t i = 0; i < alpha.size(); ++i) {
        current[i] = static_cast<float>(alpha[i]) / 255.0f;
    }

    std::vector<float> horizontal;
    std::vector<float> vertical;
    for (int pass = 0; pass < kBoxPassCount; ++pass) {
        boxBlurHorizontal(current, width, height, radius, horizontal);
        boxBlurVertical(horizontal, width, height, radius, vertical);
        current.swap(vertical);
    }

    std::vector<uint8_t> out(alpha.size());
    for (size_t i = 0; i < current.size(); ++i) {
        out[i] = static_cast<uint8_t>(std::round(std::clamp(current[i], 0.0f, 1.0f) * 255.0f));
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
        m_shapes[idx].softAlpha = buildSoftAlphaMask(m_shapes[idx].alpha, w, h);
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
    out.softAlpha = s.softAlpha;
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
        textureData[i * 2u + 1u] = s.softAlpha.empty() ? s.alpha[i] : s.softAlpha[i];
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
    shape.softAlpha = buildSoftAlphaMask(shape.alpha, w, h);
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
    out.softAlpha = shape->softAlpha;
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
        textureData[i * 2u + 1u] = shape->softAlpha.empty() ? shape->alpha[i] : shape->softAlpha[i];
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
