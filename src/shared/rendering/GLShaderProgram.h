// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   S H A D E R   P R O G R A M
// ==========================================================================

#ifndef AETHER_ENGINE_OPENGL_GLSHADERPROGRAM_H
#define AETHER_ENGINE_OPENGL_GLSHADERPROGRAM_H

#include "shared/types/Result.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QString>

#include <array>
#include <string>
#include <unordered_map>

namespace aether {

/**
 * @brief OpenGL shader program wrapper
 *
 * Handles loading, compiling, and linking GLSL shaders.
 * Provides uniform setters for common types.
 */
class GLShaderProgram {
public:
    GLShaderProgram(QOpenGLFunctions_4_5_Core* gl);
    ~GLShaderProgram();

    // Non-copyable
    GLShaderProgram(const GLShaderProgram&) = delete;
    GLShaderProgram& operator=(const GLShaderProgram&) = delete;

    // Move semantics
    GLShaderProgram(GLShaderProgram&& other) noexcept;
    GLShaderProgram& operator=(GLShaderProgram&& other) noexcept;

    // Loading
    Result<void> loadFromFiles(
        const QString& vertPath, const QString& fragPath, const QString& cacheKey = QString());
    Result<void> loadFromSource(
        const QString& vertSource, const QString& fragSource, const QString& cacheKey = QString());
    Result<void> loadComputeFromFile(
        const QString& computePath, const QString& cacheKey = QString());
    Result<void> loadComputeFromSource(
        const QString& computeSource, const QString& cacheKey = QString());

    // Usage
    void use();
    void release();
    bool isValid() const { return m_program != 0; }
    GLuint handle() const { return m_program; }

    // Uniform setters
    void setUniform(const char* name, int value);
    void setUniform(const char* name, int x, int y); // for ivec2
    void setUniform(const char* name, float value);
    void setUniform(const char* name, float x, float y);
    void setUniform(const char* name, float x, float y, float z);
    void setUniform(const char* name, float x, float y, float z, float w);
    void setUniform(const char* name, const std::array<float, 2>& vec);
    void setUniform(const char* name, const std::array<float, 4>& vec);
    void setUniform(const char* name, const std::array<float, 16>& mat);
    /// Uploads `count` vec2 elements into a `vec2 name[]` uniform array;
    /// `values` is tightly packed x0,y0,x1,y1,...
    void setUniformVec2Array(const char* name, const float* values, int count);

private:
    Result<QString> readShaderFile(const QString& path, const QString& stageName) const;
    GLint getUniformLocation(const char* name);
    void destroy();

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    GLuint m_program = 0;
    std::unordered_map<std::string, GLint> m_uniformLocationCache;
};

} // namespace aether

#endif // AETHER_ENGINE_OPENGL_GLSHADERPROGRAM_H
