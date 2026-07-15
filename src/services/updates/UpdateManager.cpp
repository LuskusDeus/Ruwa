// SPDX-License-Identifier: MPL-2.0

#include "services/updates/UpdateManager.h"

#include "services/updates/SemanticVersion.h"
#include "services/updates/UpdateSecurity.h"
#include "RuwaBuildConfig.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#endif

namespace ruwa::services {

namespace {

constexpr qint64 kMaximumManifestBytes = 64 * 1024;
constexpr qint64 kMaximumSignatureBytes = 64 * 1024;
constexpr qsizetype kDownloadChunkBytes = 64 * 1024;
constexpr qsizetype kMaximumEncodedCommandCharacters = 8000;

QString updateManifestUrlString()
{
    return QString::fromUtf8(RUWA_UPDATE_MANIFEST_URL).trimmed();
}

QString trustedSignerFingerprint()
{
    return QString::fromUtf8(RUWA_UPDATE_SIGNER_CERT_SHA256).trimmed();
}

QStringList allowedUpdateHosts()
{
    return QString::fromUtf8(RUWA_UPDATE_ALLOWED_HOSTS)
        .split(QLatin1Char(';'), Qt::SkipEmptyParts);
}

qint64 maximumArchiveBytes()
{
    return static_cast<qint64>(RUWA_UPDATE_MAX_ARCHIVE_BYTES);
}

QUrl signatureUrlForManifest(const QUrl& manifestUrl)
{
    QUrl signatureUrl = manifestUrl;
    signatureUrl.setPath(signatureUrl.path() + QStringLiteral(".p7s"));
    return signatureUrl;
}

bool builtInUpdatesEnabled()
{
#if defined(Q_OS_WIN)
    return RUWA_ENABLE_UPDATES != 0
        && isAllowedUpdateUrl(QUrl(updateManifestUrlString()), allowedUpdateHosts())
        && isValidCertificateSha256(trustedSignerFingerprint()) && maximumArchiveBytes() > 0;
#else
    return false;
#endif
}

bool replySucceeded(QNetworkReply* reply)
{
    if (!reply || reply->error() != QNetworkReply::NoError
        || !isAllowedUpdateUrl(reply->url(), allowedUpdateHosts())) {
        return false;
    }
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    return status.isValid() && status.toInt() == 200;
}

bool writeAtomically(const QString& path, const QByteArray& data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

QByteArray sha256ForFile(const QString& path, qint64 expectedSize = -1)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    if (expectedSize >= 0 && file.size() != expectedSize) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(kDownloadChunkBytes);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            return {};
        }
        hash.addData(chunk);
    }
    return hash.result().toHex();
}

QString updateHealthToken()
{
    const quint64 first = QRandomGenerator::system()->generate64();
    const quint64 second = QRandomGenerator::system()->generate64();
    return QStringLiteral("%1%2")
        .arg(first, 16, 16, QLatin1Char('0'))
        .arg(second, 16, 16, QLatin1Char('0'));
}

bool isHealthToken(const QString& token)
{
    if (token.size() != 32) {
        return false;
    }
    for (const QChar ch : token) {
        const QChar lower = ch.toLower();
        const bool digit = ch >= QLatin1Char('0') && ch <= QLatin1Char('9');
        if (!digit && (lower < QLatin1Char('a') || lower > QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

#if defined(Q_OS_WIN)
QString quoteWindowsArgument(const QString& argument)
{
    if (argument.isEmpty()) {
        return QStringLiteral("\"\"");
    }
    bool needsQuotes = false;
    for (const QChar ch : argument) {
        if (ch.isSpace() || ch == QLatin1Char('"')) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return argument;
    }

    QString result = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar ch : argument) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            result += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            result += ch;
            backslashes = 0;
            continue;
        }
        result += QString(backslashes, QLatin1Char('\\'));
        backslashes = 0;
        result += ch;
    }
    result += QString(backslashes * 2, QLatin1Char('\\'));
    result += QLatin1Char('"');
    return result;
}

QString joinWindowsArguments(const QStringList& arguments)
{
    QStringList quoted;
    quoted.reserve(arguments.size());
    for (const QString& argument : arguments) {
        quoted.append(quoteWindowsArgument(argument));
    }
    return quoted.join(QLatin1Char(' '));
}

bool installParentIsWritable()
{
    const QDir installDir(QFileInfo(QCoreApplication::applicationFilePath()).absolutePath());
    QDir parentDir = installDir;
    if (!parentDir.cdUp()) {
        return false;
    }
    const QString probePath = parentDir.absoluteFilePath(
        QStringLiteral("ruwa_update_probe_%1.tmp").arg(QDateTime::currentMSecsSinceEpoch()));
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    probe.close();
    return QFile::remove(probePath);
}

bool startWindowsInstallerProcess(const QStringList& arguments, bool requireElevation)
{
    if (!requireElevation) {
        return QProcess::startDetached(QStringLiteral("powershell.exe"), arguments);
    }
    const std::wstring verb = QStringLiteral("runas").toStdWString();
    const std::wstring file = QStringLiteral("powershell.exe").toStdWString();
    const std::wstring parameters = joinWindowsArguments(arguments).toStdWString();
    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = verb.c_str();
    info.lpFile = file.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    return ShellExecuteExW(&info) == TRUE;
}
#endif

} // namespace

UpdateManager::UpdateManager(QObject* parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

UpdateManager::~UpdateManager()
{
    if (m_checkReply) {
        m_checkReply->abort();
    }
    if (m_downloadReply) {
        m_downloadReply->abort();
    }
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
    }
}

UpdateManager* UpdateManager::instance()
{
    static UpdateManager* manager = new UpdateManager(qApp);
    return manager;
}

void UpdateManager::initialize()
{
    if (m_initialized) {
        return;
    }
    m_initialized = true;
    checkForUpdates();
}

void UpdateManager::checkForUpdates()
{
    if (!builtInUpdatesEnabled()) {
        clearLatestUpdate();
        finishUpdateCheck(false, false, QString());
        return;
    }
    if (m_checkReply || m_updateCheckStarted) {
        if (m_updateCheckFinished) {
            emit updateCheckFinished(m_cachedHasUpdate, m_cachedVersionInfo);
        }
        return;
    }
    m_updateCheckStarted = true;
    startCheckRequest(QUrl(updateManifestUrlString()), CheckStage::Manifest);
}

void UpdateManager::recheckForUpdates()
{
    if (m_checkReply) {
        return;
    }
    m_updateCheckStarted = false;
    m_updateCheckFinished = false;
    checkForUpdates();
}

void UpdateManager::startCheckRequest(const QUrl& url, CheckStage stage)
{
    if (!isAllowedUpdateUrl(url, allowedUpdateHosts())) {
        finishUpdateCheck(false, false, QString());
        return;
    }

    m_checkStage = stage;
    m_checkBuffer.clear();
    m_checkLimitExceeded = false;
    QNetworkRequest request(url);
    request.setTransferTimeout(10000);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setRawHeader("User-Agent", "Ruwa-Updater/2.0");
    request.setRawHeader("Accept", stage == CheckStage::Manifest ? "application/json"
                                                                  : "application/pkcs7-signature");

    m_checkReply = m_networkManager->get(request);
    connect(m_checkReply, &QNetworkReply::redirected, this,
        [this](const QUrl& target) {
            if (!m_checkReply) {
                return;
            }
            const QUrl resolved = m_checkReply->url().resolved(target);
            if (isAllowedUpdateUrl(resolved, allowedUpdateHosts())) {
                m_checkReply->redirectAllowed();
            } else {
                m_checkLimitExceeded = true;
                m_checkReply->abort();
            }
        });
    connect(m_checkReply, &QNetworkReply::readyRead, this, &UpdateManager::onCheckReadyRead);
    connect(m_checkReply, &QNetworkReply::finished, this, &UpdateManager::onCheckReply);
}

void UpdateManager::onCheckReadyRead()
{
    if (!m_checkReply) {
        return;
    }
    const qint64 limit
        = m_checkStage == CheckStage::Manifest ? kMaximumManifestBytes : kMaximumSignatureBytes;
    while (m_checkReply->bytesAvailable() > 0) {
        const qint64 remaining = limit - m_checkBuffer.size();
        if (remaining <= 0) {
            m_checkLimitExceeded = true;
            m_checkReply->abort();
            return;
        }
        const QByteArray chunk = m_checkReply->read(qMin<qint64>(remaining + 1, kDownloadChunkBytes));
        if (chunk.size() > remaining) {
            m_checkLimitExceeded = true;
            m_checkReply->abort();
            return;
        }
        m_checkBuffer.append(chunk);
    }
}

void UpdateManager::onCheckReply()
{
    if (!m_checkReply) {
        return;
    }
    onCheckReadyRead();
    QNetworkReply* reply = m_checkReply;
    m_checkReply = nullptr;
    const CheckStage completedStage = m_checkStage;
    m_checkStage = CheckStage::None;
    const bool succeeded = !m_checkLimitExceeded && replySucceeded(reply) && !m_checkBuffer.isEmpty();
    reply->deleteLater();
    if (!succeeded) {
        clearLatestUpdate();
        finishUpdateCheck(false, false, QString());
        return;
    }

    if (completedStage == CheckStage::Manifest) {
        m_manifestBytes = m_checkBuffer;
        startCheckRequest(signatureUrlForManifest(QUrl(updateManifestUrlString())), CheckStage::Signature);
        return;
    }

    m_signatureBytes = m_checkBuffer;
    QString signatureError;
    if (!verifyDetachedUpdateSignature(
            m_manifestBytes, m_signatureBytes, trustedSignerFingerprint(), &signatureError)) {
        clearLatestUpdate();
        finishUpdateCheck(true, false, QString());
        return;
    }

    QString manifestError;
    const auto manifest
        = UpdateManifest::parse(m_manifestBytes, maximumArchiveBytes(), &manifestError);
    if (!manifest || !isAllowedUpdateUrl(manifest->archiveUrl, allowedUpdateHosts())) {
        clearLatestUpdate();
        finishUpdateCheck(true, false, QString());
        return;
    }

    m_latestManifest = *manifest;
    m_latestReleaseVersion = manifest->version;
    m_latestReleaseDescription = manifest->description;
    m_latestDownloadUrl = manifest->archiveUrl.toString();
    m_latestDownloadFileName = manifest->archiveFileName;
    m_downloadTargetPath = updatePathForFileName(manifest->archiveFileName);

    const bool hasUpdate
        = isVersionNewer(QCoreApplication::applicationVersion(), manifest->version);
    if (!hasUpdate) {
        m_latestDownloadUrl.clear();
        m_latestDownloadFileName.clear();
        m_downloadTargetPath.clear();
        m_downloadVerified = false;
    } else {
        verifyExistingPendingArchive();
    }
    finishUpdateCheck(true, hasUpdate, manifest->version);
}

void UpdateManager::finishUpdateCheck(
    bool networkAvailable, bool hasUpdate, const QString& versionInfo)
{
    m_networkAvailable = networkAvailable;
    m_cachedHasUpdate = hasUpdate;
    m_cachedVersionInfo = versionInfo;
    m_updateCheckStarted = true;
    m_updateCheckFinished = true;
    m_lastUpdateCheckTime = QDateTime::currentDateTimeUtc();
    emit networkTestFinished(networkAvailable);
    emit updateCheckFinished(hasUpdate, versionInfo);
}

void UpdateManager::clearLatestUpdate()
{
    m_latestManifest = {};
    m_latestReleaseVersion.clear();
    m_latestReleaseDescription.clear();
    m_latestDownloadUrl.clear();
    m_latestDownloadFileName.clear();
    m_downloadTargetPath.clear();
    m_downloadVerified = false;
    m_manifestBytes.clear();
    m_signatureBytes.clear();
}

void UpdateManager::downloadUpdate()
{
    if (!builtInUpdatesEnabled() || m_latestDownloadUrl.isEmpty()
        || !isAllowedUpdateUrl(QUrl(m_latestDownloadUrl), allowedUpdateHosts())) {
        emit downloadFinished(false, QStringLiteral("No trusted update is available"));
        return;
    }
    if (m_downloadReply) {
        emit downloadFinished(false, QStringLiteral("An update download is already running"));
        return;
    }

    const QString storageDirectory = updateStorageDirectory();
    if (storageDirectory.isEmpty() || !QDir().mkpath(storageDirectory)) {
        emit downloadFinished(false, QStringLiteral("Failed to create update storage directory"));
        return;
    }
    m_downloadTargetPath = updatePathForFileName(resolvedLatestDownloadFileName());
    if (m_downloadTargetPath.isEmpty()) {
        emit downloadFinished(false, QStringLiteral("Invalid update archive file name"));
        return;
    }

    m_downloadFile = std::make_unique<QSaveFile>(m_downloadTargetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        m_downloadFile.reset();
        emit downloadFinished(false, QStringLiteral("Failed to open update archive for writing"));
        return;
    }
    m_downloadHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_downloadBytes = 0;
    m_downloadFailure.clear();
    m_downloadVerified = false;

    QNetworkRequest request { QUrl(m_latestDownloadUrl) };
    request.setTransferTimeout(0);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setRawHeader("User-Agent", "Ruwa-Updater/2.0");
    m_downloadReply = m_networkManager->get(request);
    connect(m_downloadReply, &QNetworkReply::redirected, this,
        [this](const QUrl& target) {
            if (!m_downloadReply) {
                return;
            }
            const QUrl resolved = m_downloadReply->url().resolved(target);
            if (isAllowedUpdateUrl(resolved, allowedUpdateHosts())) {
                m_downloadReply->redirectAllowed();
            } else {
                m_downloadFailure = QStringLiteral("Update redirected to an untrusted host");
                m_downloadReply->abort();
            }
        });
    connect(m_downloadReply, &QNetworkReply::metaDataChanged, this, [this]() {
        if (!m_downloadReply) {
            return;
        }
        bool ok = false;
        const qint64 contentLength
            = m_downloadReply->header(QNetworkRequest::ContentLengthHeader).toLongLong(&ok);
        if (ok && contentLength != m_latestManifest.archiveSize) {
            m_downloadFailure = QStringLiteral("Update archive size does not match the signed manifest");
            m_downloadReply->abort();
        }
    });
    connect(m_downloadReply, &QNetworkReply::readyRead, this, &UpdateManager::onDownloadReadyRead);
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
        &UpdateManager::onDownloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateManager::onDownloadFinished);
}

void UpdateManager::onDownloadReadyRead()
{
    if (!m_downloadReply || !m_downloadFile || !m_downloadHash) {
        return;
    }
    while (m_downloadReply->bytesAvailable() > 0) {
        const qint64 remaining = m_latestManifest.archiveSize - m_downloadBytes;
        if (remaining <= 0) {
            m_downloadFailure = QStringLiteral("Update archive exceeded its signed size");
            m_downloadReply->abort();
            return;
        }
        const QByteArray chunk
            = m_downloadReply->read(qMin<qint64>(remaining + 1, kDownloadChunkBytes));
        if (chunk.size() > remaining) {
            m_downloadFailure = QStringLiteral("Update archive exceeded its signed size");
            m_downloadReply->abort();
            return;
        }
        if (m_downloadFile->write(chunk) != chunk.size()) {
            m_downloadFailure = QStringLiteral("Failed to save update archive");
            m_downloadReply->abort();
            return;
        }
        m_downloadHash->addData(chunk);
        m_downloadBytes += chunk.size();
    }
}

void UpdateManager::onDownloadProgress(qint64 bytesReceived, qint64)
{
    const qint64 total = m_latestManifest.archiveSize;
    if (total <= 0) {
        emit downloadProgress(0);
        return;
    }
    emit downloadProgress(
        qBound(0, static_cast<int>((qMin(bytesReceived, total) * 100) / total), 100));
}

void UpdateManager::onDownloadFinished()
{
    if (!m_downloadReply) {
        return;
    }
    onDownloadReadyRead();
    QNetworkReply* reply = m_downloadReply;
    m_downloadReply = nullptr;

    QString error = m_downloadFailure;
    if (error.isEmpty() && !replySucceeded(reply)) {
        error = reply->errorString();
    }
    if (error.isEmpty() && m_downloadBytes != m_latestManifest.archiveSize) {
        error = QStringLiteral("Downloaded update archive is incomplete");
    }
    if (error.isEmpty()
        && m_downloadHash->result().toHex() != m_latestManifest.archiveSha256) {
        error = QStringLiteral("Downloaded update archive failed SHA-256 verification");
    }
    if (error.isEmpty() && !m_downloadFile->commit()) {
        error = QStringLiteral("Failed to commit update archive");
    }
    if (!error.isEmpty() && m_downloadFile) {
        m_downloadFile->cancelWriting();
    }

    m_downloadFile.reset();
    m_downloadHash.reset();
    reply->deleteLater();
    if (!error.isEmpty()) {
        m_downloadVerified = false;
        QFile::remove(m_downloadTargetPath);
        emit downloadFinished(false, error);
        return;
    }

    if (!persistPendingMetadata(&error)) {
        m_downloadVerified = false;
        QFile::remove(m_downloadTargetPath);
        emit downloadFinished(false, error);
        return;
    }
    m_downloadVerified = true;
    emit downloadProgress(100);
    emit downloadFinished(true, m_downloadTargetPath);
}

bool UpdateManager::persistPendingMetadata(QString* errorMessage)
{
    if (!writeAtomically(pendingManifestPath(), m_manifestBytes)
        || !writeAtomically(pendingSignaturePath(), m_signatureBytes)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to save signed update metadata");
        }
        QFile::remove(pendingManifestPath());
        QFile::remove(pendingSignaturePath());
        return false;
    }
    return true;
}

bool UpdateManager::verifyExistingPendingArchive()
{
    m_downloadVerified = false;
    if (m_downloadTargetPath.isEmpty()) {
        return false;
    }
    const QByteArray hash
        = sha256ForFile(m_downloadTargetPath, m_latestManifest.archiveSize);
    if (hash.isEmpty() || hash != m_latestManifest.archiveSha256) {
        return false;
    }
    QString error;
    if (!persistPendingMetadata(&error)) {
        return false;
    }
    m_downloadVerified = true;
    return true;
}

bool UpdateManager::hasPendingDownloadedUpdate() const
{
    if (!m_downloadVerified || m_downloadTargetPath.isEmpty()) {
        return false;
    }
    const QFileInfo archive(m_downloadTargetPath);
    return archive.exists() && archive.isFile() && archive.size() == m_latestManifest.archiveSize
        && QFileInfo::exists(pendingManifestPath()) && QFileInfo::exists(pendingSignaturePath());
}

bool UpdateManager::applyUpdateAndRestart()
{
    if (!builtInUpdatesEnabled() || !hasPendingDownloadedUpdate()) {
        return false;
    }

#if defined(Q_OS_WIN)
    QFile scriptResource(QStringLiteral(":/updates/ApplyUpdate.ps1"));
    if (!scriptResource.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QString script = QString::fromUtf8(scriptResource.readAll());
    const QString token = updateHealthToken();
    const QString storageDirectory = updateStorageDirectory();
    const QString markerPath
        = QDir(storageDirectory).absoluteFilePath(QStringLiteral("health-%1.ok").arg(token));
    QFile::remove(markerPath);

    const QString installDirectory
        = QFileInfo(QCoreApplication::applicationFilePath()).absolutePath();
    const QJsonObject configuration {
        { QStringLiteral("pid"), static_cast<double>(GetCurrentProcessId()) },
        { QStringLiteral("archivePath"), m_downloadTargetPath },
        { QStringLiteral("manifestPath"), pendingManifestPath() },
        { QStringLiteral("signaturePath"), pendingSignaturePath() },
        { QStringLiteral("installDirectory"), installDirectory },
        { QStringLiteral("logPath"),
            QDir(storageDirectory).absoluteFilePath(QStringLiteral("last-update.log")) },
        { QStringLiteral("trustedCertificateSha256"), trustedSignerFingerprint().toLower() },
        { QStringLiteral("expectedVersion"), m_latestManifest.version },
        { QStringLiteral("healthToken"), token },
        { QStringLiteral("healthMarkerPath"), markerPath },
    };
    const QByteArray configurationBase64
        = QJsonDocument(configuration).toJson(QJsonDocument::Compact).toBase64();
    const QString configurationMarker = QStringLiteral("__RUWA_CONFIG_BASE64__");
    if (!script.contains(configurationMarker)) {
        return false;
    }
    script.replace(configurationMarker, QString::fromLatin1(configurationBase64));

    const QString installerScriptPath
        = QDir(storageDirectory).absoluteFilePath(QStringLiteral("pending-installer.ps1"));
    const QByteArray installerScriptBytes = script.toUtf8();
    if (!writeAtomically(installerScriptPath, installerScriptBytes)) {
        return false;
    }
    const QString scriptPathBase64
        = QString::fromLatin1(installerScriptPath.toUtf8().toBase64());
    const QString scriptSha256 = QString::fromLatin1(
        QCryptographicHash::hash(installerScriptBytes, QCryptographicHash::Sha256).toHex());
    const QString bootstrap = QStringLiteral(
        "$ErrorActionPreference='Stop';"
        "$p=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('%1'));"
        "$b=[IO.File]::ReadAllBytes($p);"
        "$h=[BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash($b))"
        ".Replace('-','').ToLowerInvariant();"
        "if($h -ne '%2'){throw 'Installer script integrity check failed'};"
        "try{&([ScriptBlock]::Create([Text.Encoding]::UTF8.GetString($b)))}"
        "finally{Remove-Item -LiteralPath $p -Force -ErrorAction SilentlyContinue}")
                                  .arg(scriptPathBase64, scriptSha256);
    const QByteArray utf16Bootstrap(
        reinterpret_cast<const char*>(bootstrap.utf16()), bootstrap.size() * 2);
    const QString encodedCommand = QString::fromLatin1(utf16Bootstrap.toBase64());
    if (encodedCommand.size() > kMaximumEncodedCommandCharacters) {
        QFile::remove(installerScriptPath);
        return false;
    }

    const QStringList arguments { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
        QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"), QStringLiteral("-EncodedCommand"),
        encodedCommand };
    const bool started = startWindowsInstallerProcess(arguments, !installParentIsWritable());
    if (started) {
        qApp->quit();
    } else {
        QFile::remove(installerScriptPath);
    }
    return started;
#else
    return false;
#endif
}

bool UpdateManager::isVersionNewer(const QString& currentVersion, const QString& remoteVersion)
{
    const auto current = SemanticVersion::parse(currentVersion);
    const auto remote = SemanticVersion::parse(remoteVersion);
    return current && remote && remote->comparePrecedence(*current) > 0;
}

void UpdateManager::acknowledgeSuccessfulUpdateStartup()
{
    const QString argumentPrefix = QStringLiteral("--ruwa-update-health=");
    QString token;
    for (const QString& argument : QCoreApplication::arguments()) {
        if (argument.startsWith(argumentPrefix, Qt::CaseInsensitive)) {
            token = argument.mid(argumentPrefix.size());
            break;
        }
    }
    if (!isHealthToken(token)) {
        return;
    }
    const QString storageDirectory = instance()->updateStorageDirectory();
    if (storageDirectory.isEmpty() || !QDir().mkpath(storageDirectory)) {
        return;
    }
    const QString markerPath
        = QDir(storageDirectory).absoluteFilePath(QStringLiteral("health-%1.ok").arg(token));
    writeAtomically(markerPath, QCoreApplication::applicationVersion().toUtf8());
}

QString UpdateManager::updateStorageDirectory() const
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    return basePath.isEmpty() ? QString()
                              : QDir(basePath).absoluteFilePath(QStringLiteral("updates"));
}

QString UpdateManager::updatePathForFileName(const QString& fileName) const
{
    const QString sanitizedName = QFileInfo(fileName).fileName();
    const QString storageDirectory = updateStorageDirectory();
    if (sanitizedName.isEmpty() || storageDirectory.isEmpty()) {
        return {};
    }
    return QDir(storageDirectory).absoluteFilePath(sanitizedName);
}

QString UpdateManager::pendingManifestPath() const
{
    return QDir(updateStorageDirectory()).absoluteFilePath(QStringLiteral("pending-update.json"));
}

QString UpdateManager::pendingSignaturePath() const
{
    return QDir(updateStorageDirectory()).absoluteFilePath(QStringLiteral("pending-update.json.p7s"));
}

QString UpdateManager::resolvedLatestDownloadFileName() const
{
    return m_latestDownloadFileName.isEmpty() ? QUrl(m_latestDownloadUrl).fileName()
                                               : m_latestDownloadFileName;
}

} // namespace ruwa::services
