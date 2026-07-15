// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   L A S S O   F I L L   O V E R L A Y
// ==========================================================================

#ifndef RUWA_CORE_SELECTION_LASSOFILLOVERLAY_H
#define RUWA_CORE_SELECTION_LASSOFILLOVERLAY_H

#include "shared/types/Result.h"
#include "shared/types/Types.h"
#include "features/canvas/scene/Viewport.h"

#include <QOpenGLFunctions_4_5_Core>

#include <array>
#include <vector>

namespace aether {

/**
 * @brief Renders a filled polygon preview (real-time Lasso Fill tool).
 * No path outline — only solid fill with current color.
 */
class LassoFillOverlay {
public:
    explicit LassoFillOverlay(QOpenGLFunctions_4_5_Core* gl);
    ~LassoFillOverlay();

    LassoFillOverlay(const LassoFillOverlay&) = delete;
    LassoFillOverlay& operator=(const LassoFillOverlay&) = delete;

    Result<void> initialize();
    void shutdown();

    void render(const Viewport& viewport, const std::vector<Vector2>& polygon, int canvasWidth,
        int canvasHeight, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
        const std::array<float, 16>* viewProjectionContent = nullptr);

    bool isInitialized() const { return m_initialized; }

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;

    GLuint m_shaderProgram = 0;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;

    GLint m_locMVP = -1;
    GLint m_locColor = -1;

    bool m_initialized = false;

    std::vector<float> m_vertices;
};

} // namespace aether

#endif // RUWA_CORE_SELECTION_LASSOFILLOVERLAY_H
