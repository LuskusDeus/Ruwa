// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_UPDATES_UPDATEMANAGER_H
#define RUWA_SERVICES_UPDATES_UPDATEMANAGER_H

#include "services/updates/UpdateManifest.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>

#include <memory>

class QCryptographicHash;
class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;
class QUrl;

namespace ruwa::services {

class UpdateManager : public QObject {
    Q_OBJECT

public:
    explicit UpdateManager(QObject* parent = nullptr);
    ~UpdateManager() override;

    static UpdateManager* instance();

    void initialize();
    bool isNetworkAvailable() const { return m_networkAvailable; }

    void checkForUpdates();
    void recheckForUpdates();
    QDateTime lastUpdateCheckTime() const { return m_lastUpdateCheckTime; }

    QString latestReleaseVersion() const { return m_latestReleaseVersion; }
    QString latestReleaseDescription() const { return m_latestReleaseDescription; }

    void downloadUpdate();
    bool applyUpdateAndRestart();
    bool hasPendingDownloadedUpdate() const;

    static bool isVersionNewer(const QString& currentVersion, const QString& remoteVersion);
    static void acknowledgeSuccessfulUpdateStartup();

signals:
    void networkTestFinished(bool available);
    void updateCheckFinished(bool hasUpdate, const QString& versionInfo);
    void downloadProgress(int percent);
    void downloadFinished(bool success, const QString& pathOrError);

private:
    enum class CheckStage {
        None,
        Manifest,
        Signature,
    };

    QString updateStorageDirectory() const;
    QString updatePathForFileName(const QString& fileName) const;
    QString pendingManifestPath() const;
    QString pendingSignaturePath() const;
    QString resolvedLatestDownloadFileName() const;

    void startCheckRequest(const QUrl& url, CheckStage stage);
    void onCheckReadyRead();
    void onCheckReply();
    void finishUpdateCheck(bool networkAvailable, bool hasUpdate, const QString& versionInfo);
    void clearLatestUpdate();

    void onDownloadReadyRead();
    void onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void onDownloadFinished();
    bool persistPendingMetadata(QString* errorMessage);
    bool verifyExistingPendingArchive();

    QNetworkAccessManager* m_networkManager = nullptr;
    QNetworkReply* m_checkReply = nullptr;
    QNetworkReply* m_downloadReply = nullptr;
    CheckStage m_checkStage = CheckStage::None;
    QByteArray m_checkBuffer;
    QByteArray m_manifestBytes;
    QByteArray m_signatureBytes;
    bool m_checkLimitExceeded = false;

    std::unique_ptr<QSaveFile> m_downloadFile;
    std::unique_ptr<QCryptographicHash> m_downloadHash;
    qint64 m_downloadBytes = 0;
    QString m_downloadFailure;

    bool m_networkAvailable = false;
    bool m_initialized = false;
    bool m_updateCheckStarted = false;
    bool m_updateCheckFinished = false;
    bool m_cachedHasUpdate = false;
    bool m_downloadVerified = false;
    QString m_cachedVersionInfo;
    QDateTime m_lastUpdateCheckTime;
    QString m_latestReleaseVersion;
    QString m_latestReleaseDescription;
    QString m_latestDownloadUrl;
    QString m_latestDownloadFileName;
    QString m_downloadTargetPath;
    UpdateManifest m_latestManifest;
};

} // namespace ruwa::services

#endif // RUWA_SERVICES_UPDATES_UPDATEMANAGER_H
