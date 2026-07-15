// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   G L   P R O G R A M   B I N A R Y   C A C H E
// ==========================================================================

#ifndef RUWA_SHARED_RENDERING_GLPROGRAMBINARYCACHE_H
#define RUWA_SHARED_RENDERING_GLPROGRAMBINARYCACHE_H

#include "shared/types/Result.h"

#include <QOpenGLFunctions_4_5_Core>
#include <QString>

namespace aether {

class GLProgramBinaryCache {
public:
    explicit GLProgramBinaryCache(QOpenGLFunctions_4_5_Core* gl);

    static QString cacheDirectoryPath();
    static bool clearPersistentCache(QString* errorMessage = nullptr);

    Result<GLuint> loadOrCreateGraphicsProgram(
        const QString& cacheKey, const QString& vertexSource, const QString& fragmentSource);
    Result<GLuint> loadOrCreateComputeProgram(
        const QString& cacheKey, const QString& computeSource);

private:
    struct CacheContextInfo {
        QString appVersion;
        QString vendor;
        QString renderer;
        QString version;
    };

    QString cacheFilePath(const QString& cacheKey, const QString& programType,
        const QString& sourceFingerprint) const;
    QString computeSourceFingerprint(const QString& programType, const QString& firstSource,
        const QString& secondSource = QString()) const;
    CacheContextInfo queryContextInfo() const;
    bool isProgramBinarySupported() const;

    Result<GLuint> tryLoadProgramBinary(const QString& logLabel, const QString& filePath) const;
    Result<void> saveProgramBinary(
        const QString& logLabel, const QString& filePath, GLuint program) const;

    Result<GLuint> createGraphicsProgram(const QString& logLabel, const QString& filePath,
        const QString& vertexSource, const QString& fragmentSource);
    Result<GLuint> createComputeProgram(
        const QString& logLabel, const QString& filePath, const QString& computeSource);

    Result<GLuint> compileShader(GLenum type, const QString& source) const;
    Result<GLuint> linkProgram(
        const QString& logLabel, const GLuint* shaders, int shaderCount) const;

private:
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
};

} // namespace aether

#endif // RUWA_SHARED_RENDERING_GLPROGRAMBINARYCACHE_H
