// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   G L   P R O G R A M   B I N A R Y   C A C H E
// ==========================================================================

#include "shared/rendering/GLProgramBinaryCache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
namespace aether {

namespace {

constexpr quint32 kCacheMagic = 0x52554249; // RUBI
constexpr quint32 kCacheVersion = 1;

QString cacheBaseDirectory()
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::tempPath() + QStringLiteral("/ruwa-cache");
    }
    const QString cacheDir = QDir::cleanPath(baseDir + QStringLiteral("/gl-program-binaries"));
    QDir().mkpath(cacheDir);
    return cacheDir;
}

const char* shaderTypeName(GLenum type)
{
    switch (type) {
    case GL_VERTEX_SHADER:
        return "Vertex";
    case GL_FRAGMENT_SHADER:
        return "Fragment";
    case GL_COMPUTE_SHADER:
        return "Compute";
    default:
        return "Shader";
    }
}

void removeCacheFileIfPresent(const QString& filePath)
{
    if (!filePath.isEmpty() && QFileInfo::exists(filePath)) {
        QFile::remove(filePath);
    }
}

QString safeGlString(QOpenGLFunctions_4_5_Core* gl, GLenum name)
{
    const auto* value = reinterpret_cast<const char*>(gl->glGetString(name));
    return value ? QString::fromUtf8(value) : QString();
}

} // namespace

GLProgramBinaryCache::GLProgramBinaryCache(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

QString GLProgramBinaryCache::cacheDirectoryPath()
{
    return cacheBaseDirectory();
}

bool GLProgramBinaryCache::clearPersistentCache(QString* errorMessage)
{
    const QString cacheDirPath = cacheDirectoryPath();
    QDir cacheDir(cacheDirPath);
    if (!cacheDir.exists()) {
        return true;
    }

    if (!cacheDir.removeRecursively()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to remove shader cache directory: %1")
                                .arg(QDir::toNativeSeparators(cacheDirPath));
        }
        return false;
    }

    if (!QDir().mkpath(cacheDirPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to recreate shader cache directory: %1")
                                .arg(QDir::toNativeSeparators(cacheDirPath));
        }
        return false;
    }

    return true;
}

Result<GLuint> GLProgramBinaryCache::loadOrCreateGraphicsProgram(
    const QString& cacheKey, const QString& vertexSource, const QString& fragmentSource)
{
    const QString filePath = cacheFilePath(cacheKey, QStringLiteral("graphics"),
        computeSourceFingerprint(QStringLiteral("graphics"), vertexSource, fragmentSource));

    QElapsedTimer timer;
    timer.start();

    if (isProgramBinarySupported()) {
        auto cachedProgram = tryLoadProgramBinary(cacheKey, filePath);
        if (cachedProgram) {
            return cachedProgram;
        }
    }

    auto createdProgram = createGraphicsProgram(cacheKey, filePath, vertexSource, fragmentSource);
    if (createdProgram) { }
    return createdProgram;
}

Result<GLuint> GLProgramBinaryCache::loadOrCreateComputeProgram(
    const QString& cacheKey, const QString& computeSource)
{
    const QString filePath = cacheFilePath(cacheKey, QStringLiteral("compute"),
        computeSourceFingerprint(QStringLiteral("compute"), computeSource));

    QElapsedTimer timer;
    timer.start();

    if (isProgramBinarySupported()) {
        auto cachedProgram = tryLoadProgramBinary(cacheKey, filePath);
        if (cachedProgram) {
            return cachedProgram;
        }
    }

    auto createdProgram = createComputeProgram(cacheKey, filePath, computeSource);
    if (createdProgram) { }
    return createdProgram;
}

QString GLProgramBinaryCache::cacheFilePath(
    const QString& cacheKey, const QString& programType, const QString& sourceFingerprint) const
{
    const CacheContextInfo info = queryContextInfo();
    QByteArray payload;
    payload.append(cacheKey.toUtf8());
    payload.append('\n');
    payload.append(programType.toUtf8());
    payload.append('\n');
    payload.append(sourceFingerprint.toUtf8());
    payload.append('\n');
    payload.append(info.appVersion.toUtf8());
    payload.append('\n');
    payload.append(info.vendor.toUtf8());
    payload.append('\n');
    payload.append(info.renderer.toUtf8());
    payload.append('\n');
    payload.append(info.version.toUtf8());

    const QByteArray digest = QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
    return QDir(cacheBaseDirectory())
        .filePath(QString::fromLatin1(digest.toHex()) + QStringLiteral(".bin"));
}

QString GLProgramBinaryCache::computeSourceFingerprint(
    const QString& programType, const QString& firstSource, const QString& secondSource) const
{
    QByteArray payload;
    payload.append(programType.toUtf8());
    payload.append('\n');
    payload.append(firstSource.toUtf8());
    payload.append('\n');
    payload.append(secondSource.toUtf8());
    return QString::fromLatin1(
        QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
}

GLProgramBinaryCache::CacheContextInfo GLProgramBinaryCache::queryContextInfo() const
{
    CacheContextInfo info;
    info.appVersion = QCoreApplication::applicationVersion();
    info.vendor = safeGlString(m_gl, GL_VENDOR);
    info.renderer = safeGlString(m_gl, GL_RENDERER);
    info.version = safeGlString(m_gl, GL_VERSION);
    return info;
}

bool GLProgramBinaryCache::isProgramBinarySupported() const
{
    if (!m_gl) {
        return false;
    }

    GLint formatCount = 0;
    m_gl->glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formatCount);
    return formatCount > 0;
}

Result<GLuint> GLProgramBinaryCache::tryLoadProgramBinary(
    const QString& logLabel, const QString& filePath) const
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return { ErrorCode::ShaderCompilationFailed, "cache miss" };
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_0);

    quint32 magic = 0;
    quint32 version = 0;
    quint32 format = 0;
    QByteArray programBinary;
    in >> magic >> version >> format >> programBinary;

    if (in.status() != QDataStream::Ok || magic != kCacheMagic || version != kCacheVersion
        || format == 0 || programBinary.isEmpty()) {
        removeCacheFileIfPresent(filePath);
        return { ErrorCode::ShaderCompilationFailed, "cache header invalid" };
    }

    GLuint program = m_gl->glCreateProgram();
    m_gl->glProgramBinary(
        program, static_cast<GLenum>(format), programBinary.constData(), programBinary.size());

    GLint linked = 0;
    m_gl->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[1024];
        m_gl->glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        m_gl->glDeleteProgram(program);
        removeCacheFileIfPresent(filePath);
        return { ErrorCode::ShaderCompilationFailed, "program binary load failed" };
    }

    return program;
}

Result<void> GLProgramBinaryCache::saveProgramBinary(
    const QString& logLabel, const QString& filePath, GLuint program) const
{
    GLint binaryLength = 0;
    m_gl->glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    if (binaryLength <= 0) {
        return { ErrorCode::ShaderCompilationFailed, "program binary length is zero" };
    }

    QByteArray programBinary(binaryLength, '\0');
    GLenum format = 0;
    GLsizei written = 0;
    m_gl->glGetProgramBinary(program, binaryLength, &written, &format, programBinary.data());
    if (written <= 0 || format == 0) {
        return { ErrorCode::ShaderCompilationFailed, "glGetProgramBinary returned empty result" };
    }

    programBinary.truncate(written);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return { ErrorCode::ShaderCompilationFailed, "failed to open cache file for writing" };
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_0);
    out << kCacheMagic << kCacheVersion << static_cast<quint32>(format) << programBinary;
    if (out.status() != QDataStream::Ok || !file.commit()) {
        return { ErrorCode::ShaderCompilationFailed, "failed to commit cache file" };
    }

    return Result<void>::ok();
}

Result<GLuint> GLProgramBinaryCache::createGraphicsProgram(const QString& logLabel,
    const QString& filePath, const QString& vertexSource, const QString& fragmentSource)
{
    auto vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) {
        return vertexShader;
    }

    auto fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        m_gl->glDeleteShader(vertexShader.value());
        return fragmentShader;
    }

    const GLuint shaders[] = { vertexShader.value(), fragmentShader.value() };
    auto linkedProgram = linkProgram(logLabel, shaders, 2);

    m_gl->glDeleteShader(vertexShader.value());
    m_gl->glDeleteShader(fragmentShader.value());

    if (!linkedProgram) {
        return linkedProgram;
    }

    if (isProgramBinarySupported()) {
        auto saveResult = saveProgramBinary(logLabel, filePath, linkedProgram.value());
        if (!saveResult) { }
    }

    return linkedProgram;
}

Result<GLuint> GLProgramBinaryCache::createComputeProgram(
    const QString& logLabel, const QString& filePath, const QString& computeSource)
{
    auto computeShader = compileShader(GL_COMPUTE_SHADER, computeSource);
    if (!computeShader) {
        return computeShader;
    }

    const GLuint shaders[] = { computeShader.value() };
    auto linkedProgram = linkProgram(logLabel, shaders, 1);
    m_gl->glDeleteShader(computeShader.value());

    if (!linkedProgram) {
        return linkedProgram;
    }

    if (isProgramBinarySupported()) {
        auto saveResult = saveProgramBinary(logLabel, filePath, linkedProgram.value());
        if (!saveResult) { }
    }

    return linkedProgram;
}

Result<GLuint> GLProgramBinaryCache::compileShader(GLenum type, const QString& source) const
{
    GLuint shader = m_gl->glCreateShader(type);
    const QByteArray sourceBytes = source.toUtf8();
    const char* sourcePtr = sourceBytes.constData();
    m_gl->glShaderSource(shader, 1, &sourcePtr, nullptr);
    m_gl->glCompileShader(shader);

    GLint success = 0;
    m_gl->glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success) {
        return shader;
    }

    char infoLog[1024];
    m_gl->glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
    m_gl->glDeleteShader(shader);
    return { ErrorCode::ShaderCompilationFailed,
        std::string(shaderTypeName(type)) + " shader compilation failed: " + infoLog };
}

Result<GLuint> GLProgramBinaryCache::linkProgram(
    const QString& logLabel, const GLuint* shaders, int shaderCount) const
{
    GLuint program = m_gl->glCreateProgram();
    if (isProgramBinarySupported()) {
        m_gl->glProgramParameteri(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    }

    for (int i = 0; i < shaderCount; ++i) {
        m_gl->glAttachShader(program, shaders[i]);
    }

    m_gl->glLinkProgram(program);

    GLint linked = 0;
    m_gl->glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked) {
        return program;
    }

    char infoLog[1024];
    m_gl->glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
    m_gl->glDeleteProgram(program);
    return { ErrorCode::ShaderCompilationFailed,
        std::string("Shader program linking failed: ") + infoLog };
}

} // namespace aether
