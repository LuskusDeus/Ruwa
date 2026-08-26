// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A D E R   D I R E C T O R Y   R E S O L V E R
// ==========================================================================

#include "shared/rendering/ShaderDirectoryResolver.h"
#include "shared/rendering/RuntimeShaderCatalog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace aether {

Result<QString> resolveRuntimeShaderDirectory()
{
    const QString configuredShaderDir = QDir::cleanPath(QString::fromUtf8(SHADER_DIR));
    const QDir appDir(QCoreApplication::applicationDirPath());

    QString shaderDirName = QFileInfo(configuredShaderDir).fileName();
    if (shaderDirName.isEmpty()) {
        shaderDirName = QStringLiteral("shaders");
    }

    QStringList shaderDirCandidates = { shaderDirName };

    QString capitalizedShaderDir = shaderDirName;
    capitalizedShaderDir[0] = capitalizedShaderDir.at(0).toUpper();
    if (!shaderDirCandidates.contains(capitalizedShaderDir)) {
        shaderDirCandidates.append(capitalizedShaderDir);
    }

    QString lowerCaseShaderDir = shaderDirName;
    lowerCaseShaderDir[0] = lowerCaseShaderDir.at(0).toLower();
    if (!shaderDirCandidates.contains(lowerCaseShaderDir)) {
        shaderDirCandidates.append(lowerCaseShaderDir);
    }

    QString finalShaderDir;
    for (const auto& shaderDirCandidate : shaderDirCandidates) {
        const QString candidatePath = QDir::cleanPath(appDir.absoluteFilePath(shaderDirCandidate));
        if (QFile::exists(QDir(candidatePath).filePath(QStringLiteral("canvas.vert.glsl")))) {
            finalShaderDir = candidatePath;
            break;
        }
    }

    if (finalShaderDir.isEmpty()) {
        finalShaderDir = QDir::cleanPath(appDir.absoluteFilePath(shaderDirCandidates.first()));
    }

    const QDir shaderDir(finalShaderDir);
    QSet<QString> requiredShaderFileSet;
    for (const auto& program : kRuntimeShaderPrograms) {
        if (program.type == RuntimeShaderProgramType::Graphics) {
            requiredShaderFileSet.insert(QString::fromUtf8(
                program.vertexShader.data(), static_cast<qsizetype>(program.vertexShader.size())));
            requiredShaderFileSet.insert(QString::fromUtf8(program.fragmentShader.data(),
                static_cast<qsizetype>(program.fragmentShader.size())));
        } else {
            requiredShaderFileSet.insert(QString::fromUtf8(program.computeShader.data(),
                static_cast<qsizetype>(program.computeShader.size())));
        }
    }

    QStringList requiredShaderFiles = requiredShaderFileSet.values();
    requiredShaderFiles.sort();

    QStringList missingShaderPaths;
    for (const auto& shaderFile : requiredShaderFiles) {
        const QString shaderPath = shaderDir.filePath(shaderFile);
        if (!QFile::exists(shaderPath)) {
            missingShaderPaths.append(QDir::toNativeSeparators(shaderPath));
        }
    }

    if (missingShaderPaths.isEmpty()) {
        return finalShaderDir;
    }

    QStringList expectedShaderDirs;
    for (const auto& shaderDirCandidate : shaderDirCandidates) {
        expectedShaderDirs.append(
            QDir::toNativeSeparators(QDir::cleanPath(appDir.absoluteFilePath(shaderDirCandidate))));
    }

    return { ErrorCode::ShaderCompilationFailed,
        QString("Required shaders were not found in the application root directory.\n"
                "Expected shader folder: %1\n"
                "Missing files:\n%2")
            .arg(expectedShaderDirs.join(QStringLiteral(" or ")),
                missingShaderPaths.join(QLatin1Char('\n')))
            .toStdString() };
}

} // namespace aether
