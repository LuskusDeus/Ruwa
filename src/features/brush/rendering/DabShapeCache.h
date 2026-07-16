// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   D A B   S H A P E   C A C H E
// ==========================================================================
//   Loads PNG dab shapes from :/brushes/1.png .. 5.png (dab type 1-5).
//   Type 0 = standard circle (no image).
//   CPU keeps the original alpha mask plus one prefiltered soft-alpha mask.
//   GPU uses the same pair so custom dab hardness remains a single texture sample.

#ifndef AETHER_ENGINE_QT_DABSHAPECACHE_H
#define AETHER_ENGINE_QT_DABSHAPECACHE_H

#include <QOpenGLFunctions_4_5_Core>
#include <QString>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace aether {

class DabShapeCache {
public:
    static DabShapeCache& instance();

    DabShapeCache(const DabShapeCache&) = delete;
    DabShapeCache& operator=(const DabShapeCache&) = delete;

    /// Load shapes from resources. Call once at startup.
    void loadFromResources();

    /// CPU: get source alpha plus its prefiltered soft-alpha mask for dab type 1-5.
    /// data is empty if type invalid or not loaded.
    struct AlphaGrid {
        std::vector<uint8_t> data;
        std::vector<uint8_t> softAlpha;
        int width = 0;
        int height = 0;
    };
    AlphaGrid getAlphaGrid(int dabType);
    void getShapeSize(int dabType, int& width, int& height);

    /// Custom dab: load an image from disk, convert color→alpha with threshold,
    /// downscale by compression, using the requested interpolation (0 = bilinear, 1 = nearest).
    /// Returns an empty grid on failure.
    AlphaGrid getCustomAlphaGrid(
        const QString& imagePath, float threshold, float compression, int interpolation);

    /// GPU: ensure GL dab texture exists for dab type 1-5.
    /// Texture is RG8: R = source alpha, G = prefiltered soft alpha.
    /// Must be called with GL context active.
    GLuint getTextureId(QOpenGLFunctions_4_5_Core* gl, int dabType);

    /// GPU: ensure GL dab texture exists for the given custom image + params.
    /// Returns 0 if the image could not be loaded.
    GLuint getCustomTextureId(QOpenGLFunctions_4_5_Core* gl, const QString& imagePath,
        float threshold, float compression, int interpolation);

    /// GPU: return the cached size of the custom texture for the given key.
    void getCustomShapeSize(const QString& imagePath, float threshold, float compression,
        int interpolation, int& width, int& height);

    /// Release GL textures (call before context destroy).
    void releaseTextures(QOpenGLFunctions_4_5_Core* gl);

private:
    DabShapeCache() = default;
    ~DabShapeCache() = default;

    struct Shape {
        std::vector<uint8_t> alpha;
        std::vector<uint8_t> softAlpha;
        int width = 0;
        int height = 0;
        GLuint textureId = 0;
    };
    static constexpr int kMinType = 1;
    static constexpr int kMaxType = 5;
    Shape m_shapes[kMaxType - kMinType + 1];
    bool m_loaded = false;

    static QString makeCustomKey(
        const QString& imagePath, float threshold, float compression, int interpolation);
    static QString makeCustomRequestKey(
        const QString& imagePath, float threshold, float compression, int interpolation);
    Shape* ensureCustomShapeLoaded(const QString& imagePath, float threshold, float compression,
        int interpolation, bool useFastRequestCache = true);

    std::unordered_map<std::string, Shape> m_customShapes;
    std::unordered_map<std::string, std::string> m_customRequestShapeKeys;
};

} // namespace aether

#endif // AETHER_ENGINE_QT_DABSHAPECACHE_H
