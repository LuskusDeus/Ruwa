// SPDX-License-Identifier: MPL-2.0

#include "services/input/StylusInputTrace.h"

#include <QtGlobal>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

namespace ruwa::services::input::trace {

namespace {

// A tablet reports 200-266 times a second, so an unbounded trace is a way to
// fill a disk while the user is only trying to reproduce a bug. Stop writing
// well before that matters; a fault that needs more than this much context is
// not going to be read by a human anyway.
constexpr qint64 kMaxTraceBytes = 32ll * 1024 * 1024;

QString resolveFilePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) {
        base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    }
    QDir().mkpath(base);
    return QDir(base).absoluteFilePath(QStringLiteral("wintab-trace.log"));
}

} // namespace

bool enabled()
{
    static const bool value = qEnvironmentVariable("RUWA_WINTAB_TRACE") == QLatin1String("1");
    return value;
}

QString filePath()
{
    static const QString path = resolveFilePath();
    return path;
}

void write(const QString& line)
{
    if (!enabled()) {
        return;
    }

    // WinTab messages and the input manager both run on the GUI thread, which is
    // the only caller, so the stream needs no lock.
    static QFile file;
    static QTextStream stream;
    static bool finished = false;

    if (finished) {
        return;
    }

    if (!file.isOpen()) {
        file.setFileName(filePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            finished = true;
            return;
        }
        stream.setDevice(&file);
        stream << "\n==== trace opened " << QDateTime::currentDateTime().toString(Qt::ISODate)
               << " ====\n";
    }

    stream << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")) << ' ' << line
           << '\n';
    // Flushed per line on purpose: the interesting traces end with the user
    // giving up and killing a window whose pointer they cannot control, and a
    // buffered tail would be exactly the part that is lost.
    stream.flush();

    if (file.size() >= kMaxTraceBytes) {
        stream << "==== size limit reached, tracing stopped ====\n";
        stream.flush();
        file.close();
        finished = true;
    }
}

} // namespace ruwa::services::input::trace
