// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E R V I C E
// ==========================================================================
//
//   Everything that happens to an exported image after the GPU is done with it.
//
//   THE SPLIT. Capturing pixels needs the GL context, which lives on the GUI
//   thread, so the capture cannot move off it. Resampling, depth conversion,
//   encoding and disk I/O need nothing but memory — and for a large document
//   they are the slow part, seconds of it. The service therefore starts where
//   the GL work ends: the caller hands over a finished PixelSurface, by value,
//   and from that moment nothing the service touches is shared with the GUI
//   thread. That is what makes the asynchrony safe rather than merely fast.
//
//   The service runs one job at a time on its own QThread — deliberately not
//   the global thread pool, because the resampler saturates the pool from
//   inside the job and a job that both occupies and waits on the same pool is
//   how deadlocks are written.
//
//   Progress and completion are delivered as signals on the thread that owns
//   the service, so a UI can connect to them directly.
//

#ifndef RUWA_CORE_EXPORTING_EXPORTSERVICE_H
#define RUWA_CORE_EXPORTING_EXPORTSERVICE_H

#include "features/export/ExportSettings.h"
#include "shared/imaging/PixelSurface.h"

#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

class QThread;

namespace ruwa::core::exporting {

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    QString path;
    QString errorText;
    /// Non-fatal adjustments the export made (depth clamped, alpha matted).
    /// Present even when ok is true — they are things the user should know,
    /// not reasons the export failed.
    QStringList warnings;
    qint64 fileSizeBytes = 0;
    QSize outputSize;
};

class ExportService : public QObject {
    Q_OBJECT

public:
    explicit ExportService(QObject* parent = nullptr);
    ~ExportService() override;

    [[nodiscard]] bool isRunning() const { return m_running.load(); }

    /// Hand off a captured surface and begin. `source` is consumed.
    ///
    /// Returns false without emitting anything when a job is already running or
    /// the settings cannot produce a file; `errorOut` then holds the reason, so
    /// a rejected export reports itself the same way a failed one does.
    /// `settings` is validated and clamped in place, so the caller can read
    /// back what will actually be written.
    bool start(shared::imaging::PixelSurface source, ExportSettings& settings,
        QString* errorOut = nullptr);

    /// Ask the running job to stop. Returns immediately; the job ends at its
    /// next cancellation point and reports a cancelled ExportResult. A file is
    /// never left half-written — see ExportEncoder's atomic write.
    void cancel();

signals:
    void started();
    /// `progress` is 0..1 across the whole job; `stage` is a translated,
    /// user-facing phase name.
    void progressChanged(qreal progress, const QString& stage);
    void finished(const ruwa::core::exporting::ExportResult& result);

private:
    void ensureThread();

    QThread* m_thread = nullptr;
    QObject* m_workerContext = nullptr;
    std::shared_ptr<std::atomic_bool> m_cancelFlag;
    std::atomic_bool m_running { false };
};

} // namespace ruwa::core::exporting

Q_DECLARE_METATYPE(ruwa::core::exporting::ExportResult)

#endif // RUWA_CORE_EXPORTING_EXPORTSERVICE_H
