// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   A E T H E R   E N G I N E   |   G L   S H A D E R   P R O G R A M
// ==========================================================================

#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLProgramBinaryCache.h"

#include <QFile>
#include <QTextStream>

namespace aether {

// ==========================================================================
//   C O N S T R U C T I O N
// ==========================================================================

GLShaderProgram::GLShaderProgram(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLShaderProgram::~GLShaderProgram()
{
    destroy();
}

GLShaderProgram::GLShaderProgram(GLShaderProgram&& other) noexcept
    : m_gl(other.m_gl)
    , m_program(other.m_program)
    , m_uniformLocationCache(std::move(other.m_uniformLocationCache))
{
    other.m_program = 0;
    other.m_uniformLocationCache.clear();
}

GLShaderProgram& GLShaderProgram::operator=(GLShaderProgram&& other) noexcept
{
    if (this != &other) {
        destroy();
        m_gl = other.m_gl;
        m_program = other.m_program;
        m_uniformLocationCache = std::move(other.m_uniformLocationCache);
        other.m_program = 0;
        other.m_uniformLocationCache.clear();
    }
    return *this;
}

// ==========================================================================
//   L O A D I N G
// ==========================================================================

Result<void> GLShaderProgram::loadFromFiles(
    const QString& vertPath, const QString& fragPath, const QString& cacheKey)
{
    auto vertSource = readShaderFile(vertPath, QStringLiteral("vertex"));
    if (!vertSource) {
        return { vertSource.error().code, vertSource.error().message };
    }

    auto fragSource = readShaderFile(fragPath, QStringLiteral("fragment"));
    if (!fragSource) {
        return { fragSource.error().code, fragSource.error().message };
    }

    QString effectiveCacheKey = cacheKey;
    if (effectiveCacheKey.isEmpty()) {
        effectiveCacheKey = QStringLiteral("file:%1|%2").arg(vertPath, fragPath);
    }

    return loadFromSource(vertSource.value(), fragSource.value(), effectiveCacheKey);
}

Result<void> GLShaderProgram::loadComputeFromFile(
    const QString& computePath, const QString& cacheKey)
{
    auto computeSource = readShaderFile(computePath, QStringLiteral("compute"));
    if (!computeSource) {
        return { computeSource.error().code, computeSource.error().message };
    }

    QString effectiveCacheKey = cacheKey;
    if (effectiveCacheKey.isEmpty()) {
        effectiveCacheKey = QStringLiteral("file:%1").arg(computePath);
    }

    return loadComputeFromSource(computeSource.value(), effectiveCacheKey);
}

Result<void> GLShaderProgram::loadFromSource(
    const QString& vertSource, const QString& fragSource, const QString& cacheKey)
{
    destroy();

    QString effectiveCacheKey = cacheKey;
    if (effectiveCacheKey.isEmpty()) {
        effectiveCacheKey = QStringLiteral("inline-graphics");
    }

    GLProgramBinaryCache cache(m_gl);
    auto result = cache.loadOrCreateGraphicsProgram(effectiveCacheKey, vertSource, fragSource);
    if (!result) {
        return { result.error().code, result.error().message };
    }

    m_program = result.value();
    return Result<void>::ok();
}

Result<void> GLShaderProgram::loadComputeFromSource(
    const QString& computeSource, const QString& cacheKey)
{
    destroy();

    QString effectiveCacheKey = cacheKey;
    if (effectiveCacheKey.isEmpty()) {
        effectiveCacheKey = QStringLiteral("inline-compute");
    }

    GLProgramBinaryCache cache(m_gl);
    auto result = cache.loadOrCreateComputeProgram(effectiveCacheKey, computeSource);
    if (!result) {
        return { result.error().code, result.error().message };
    }

    m_program = result.value();
    return Result<void>::ok();
}

Result<QString> GLShaderProgram::readShaderFile(const QString& path, const QString& stageName) const
{
    QFile shaderFile(path);
    if (!shaderFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return { ErrorCode::ShaderCompilationFailed,
            QString("Failed to open %1 shader: %2").arg(stageName, path).toStdString() };
    }

    return QTextStream(&shaderFile).readAll();
}

// ==========================================================================
//   U S A G E
// ==========================================================================

void GLShaderProgram::use()
{
    if (m_program) {
        m_gl->glUseProgram(m_program);
    }
}

void GLShaderProgram::release()
{
    m_gl->glUseProgram(0);
}

// ==========================================================================
//   U N I F O R M S
// ==========================================================================

GLint GLShaderProgram::getUniformLocation(const char* name)
{
    const std::string key(name ? name : "");
    auto it = m_uniformLocationCache.find(key);
    if (it != m_uniformLocationCache.end()) {
        return it->second;
    }

    const GLint location = m_gl->glGetUniformLocation(m_program, name);
    m_uniformLocationCache.emplace(key, location);
    return location;
}

void GLShaderProgram::setUniform(const char* name, int value)
{
    m_gl->glUniform1i(getUniformLocation(name), value);
}

void GLShaderProgram::setUniform(const char* name, int x, int y)
{
    m_gl->glUniform2i(getUniformLocation(name), x, y);
}

void GLShaderProgram::setUniform(const char* name, float value)
{
    m_gl->glUniform1f(getUniformLocation(name), value);
}

void GLShaderProgram::setUniform(const char* name, float x, float y)
{
    m_gl->glUniform2f(getUniformLocation(name), x, y);
}

void GLShaderProgram::setUniform(const char* name, float x, float y, float z)
{
    m_gl->glUniform3f(getUniformLocation(name), x, y, z);
}

void GLShaderProgram::setUniform(const char* name, float x, float y, float z, float w)
{
    m_gl->glUniform4f(getUniformLocation(name), x, y, z, w);
}

void GLShaderProgram::setUniform(const char* name, const std::array<float, 2>& vec)
{
    m_gl->glUniform2fv(getUniformLocation(name), 1, vec.data());
}

void GLShaderProgram::setUniform(const char* name, const std::array<float, 4>& vec)
{
    m_gl->glUniform4fv(getUniformLocation(name), 1, vec.data());
}

void GLShaderProgram::setUniform(const char* name, const std::array<float, 16>& mat)
{
    m_gl->glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, mat.data());
}

void GLShaderProgram::setUniformVec2Array(const char* name, const float* values, int count)
{
    m_gl->glUniform2fv(getUniformLocation(name), count, values);
}

// ==========================================================================
//   P R I V A T E
// ==========================================================================

void GLShaderProgram::destroy()
{
    m_uniformLocationCache.clear();
    if (m_program && m_gl) {
        m_gl->glDeleteProgram(m_program);
        m_program = 0;
    }
}

} // namespace aether
