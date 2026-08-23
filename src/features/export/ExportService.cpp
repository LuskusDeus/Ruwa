// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E R V I C E
// ==========================================================================

#include "features/export/ExportService.h"

#include "features/export/ExportEncoder.h"
#include "shared/imaging/ImageResampler.h"

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <utility>

namespace ruwa::core::exporting {

namespace {

using shared::imaging::PixelSurface;

// Progress is split by how long each stage actually takes on a large document:
// resampling dominates, encoding is a single pass over the pixels, writing is
// bounded by the disk. When no resampling is needed its share is skipped and
// the remaining stages stretch to fill the bar.
constexpr qreal kResampleShare = 0.70;
constexpr qreal kEncodeShare = 0.15;

QString stageResampling()
{
    return QCoreApplication::translate("ExportService", "Resampling");
}

QString stagePreparing()
{
    return QCoreApplication::translate("ExportService", "Preparing image");
}

QString stageWriting()
{
    return QCoreApplication::translate("ExportService", "Writing file");
}

} // anonymous namespace

ExportService::ExportService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<ExportResult>();
}

ExportService::~ExportService()
{
    cancel();

    if (m_thread) {
        m_thread->quit();
        // The job polls its cancel flag between bands, so this wait is bounded
        // by one band of resampling or one image write, not by the whole job.
        m_thread->wait();
        delete m_workerContext;
        delete m_thread;
        m_workerContext = nullptr;
        m_thread = nullptr;
    }
}

void ExportService::ensureThread()
{
    if (m_thread) {
        return;
    }

    m_thread = new QThread;
    m_thread->setObjectName(QStringLiteral("RuwaExport"));
    m_workerContext = new QObject;
    m_workerContext->moveToThread(m_thread);
    m_thread->start();
}

void ExportService::cancel()
{
    if (m_cancelFlag) {
        m_cancelFlag->store(true);
    }
}

bool ExportService::start(PixelSurface source, ExportSettings& settings, QString* errorOut)
{
    const auto fail = [errorOut](const QString& message) {
        if (errorOut) {
            *errorOut = message;
        }
        return false;
    };

    if (m_running.load()) {
        return fail(
            QCoreApplication::translate("ExportService", "An export is already in progress."));
    }
    if (source.isNull()) {
        return fail(QCoreApplication::translate(
            "ExportService", "The canvas could not be captured for export."));
    }

    const ExportValidation validation = validate(settings);
    if (!validation.ok) {
        return fail(validation.error);
    }

    // The destination folder is created here rather than in the encoder: a
    // missing folder is a policy decision (do we make one?), not a file
    // operation, and the encoder should stay mechanical.
    QDir destination(settings.directory);
    if (!destination.exists() && !destination.mkpath(QStringLiteral("."))) {
        return fail(QCoreApplication::translate("ExportService",
            "The folder \"%1\" does not exist and could not be created.")
                .arg(QDir::toNativeSeparators(settings.directory)));
    }

    const QString targetPath = settings.absolutePath();
    if (targetPath.isEmpty()) {
        return fail(QCoreApplication::translate("ExportService", "No destination file is set."));
    }

    ensureThread();

    m_cancelFlag = std::make_shared<std::atomic_bool>(false);
    m_running.store(true);

    // Shared rather than moved into the lambda: QMetaObject::invokeMethod's
    // functor overload does not promise support for move-only callables, and a
    // shared_ptr keeps the surface owned by exactly one job either way.
    auto sharedSource = std::make_shared<PixelSurface>(std::move(source));
    auto cancelFlag = m_cancelFlag;
    const ExportSettings jobSettings = settings;
    const QStringList carriedWarnings = validation.warnings;
    QPointer<ExportService> self(this);

    QMetaObject::invokeMethod(m_workerContext, [self, sharedSource, jobSettings, cancelFlag,
                                                   carriedWarnings, targetPath]() {
        const auto report = [self](qreal progress, const QString& stage) {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self, [self, progress, stage]() {
                    if (self) {
                        emit self->progressChanged(progress, stage);
                    }
                },
                Qt::QueuedConnection);
        };

        const auto cancelled = [cancelFlag]() { return cancelFlag->load(); };

        ExportResult result;
        result.path = targetPath;
        result.warnings = carriedWarnings;

        PixelSurface& captured = *sharedSource;
        const bool needsResample = captured.size() != jobSettings.outputSize;

        PixelSurface finalSurface;
        if (needsResample) {
            report(0.0, stageResampling());

            shared::imaging::ResampleOptions options;
            options.filter = jobSettings.resampleFilter;

            shared::imaging::ResampleHooks hooks;
            hooks.shouldCancel = cancelled;
            hooks.onProgress
                = [&report](qreal p) { report(p * kResampleShare, stageResampling()); };

            finalSurface
                = shared::imaging::resample(captured, jobSettings.outputSize, options, hooks);

            if (finalSurface.isNull()) {
                if (cancelled()) {
                    result.cancelled = true;
                } else {
                    result.errorText = QCoreApplication::translate("ExportService",
                        "Not enough memory to resample the image to %1 x %2 px.")
                                           .arg(jobSettings.outputSize.width())
                                           .arg(jobSettings.outputSize.height());
                }
            }
            // The source is not needed past this point and can be a
            // multi-hundred-megabyte buffer; releasing it now halves the peak
            // for the encode that follows.
            captured = PixelSurface();
        } else {
            finalSurface = std::move(captured);
        }

        if (!result.cancelled && result.errorText.isEmpty()) {
            if (cancelled()) {
                result.cancelled = true;
            } else {
                const qreal encodeBase = needsResample ? kResampleShare : 0.0;
                report(encodeBase, stagePreparing());

                // NOTE: for an 8-bit alpha export this image BORROWS
                // finalSurface's buffer, so finalSurface must stay alive and
                // untouched until the write below has returned.
                const QImage image = encoder::toQImage(finalSurface, jobSettings);
                if (image.isNull()) {
                    result.errorText = QCoreApplication::translate(
                        "ExportService", "Not enough memory to prepare the image for writing.");
                } else if (cancelled()) {
                    result.cancelled = true;
                } else {
                    report(encodeBase + kEncodeShare, stageWriting());

                    const encoder::WriteOutcome outcome
                        = encoder::writeImage(image, targetPath, jobSettings);
                    result.ok = outcome.ok;
                    result.errorText = outcome.errorText;
                    result.fileSizeBytes = outcome.fileSizeBytes;
                    result.outputSize = image.size();
                }
            }
        }

        if (result.ok) {
            report(1.0, stageWriting());
        }

        if (!self) {
            return;
        }
        QMetaObject::invokeMethod(
            self, [self, result]() {
                if (!self) {
                    return;
                }
                self->m_running.store(false);
                emit self->finished(result);
            },
            Qt::QueuedConnection);
    });

    emit started();
    return true;
}

} // namespace ruwa::core::exporting
