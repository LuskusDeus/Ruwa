// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_INPUT_STYLUSINPUTTRACE_H
#define RUWA_SERVICES_INPUT_STYLUSINPUTTRACE_H

#include <QString>

namespace ruwa::services::input::trace {

/// True when RUWA_WINTAB_TRACE=1 was present in the environment at process start.
/// Every call site is expected to test this first: building the trace strings for
/// a 200-266 Hz packet stream is not free, and the trace exists for reproducing a
/// specific driver's misbehaviour, not for normal runs.
bool enabled();

/// Appends one timestamped line to the trace file. Does nothing unless enabled().
void write(const QString& line);

/// Absolute path of the trace file, so it can be named when asking for it.
QString filePath();

} // namespace ruwa::services::input::trace

#endif // RUWA_SERVICES_INPUT_STYLUSINPUTTRACE_H
