// SPDX-License-Identifier: MPL-2.0

// ScopedProfiler.h
//
// Lightweight, header-only, aggregating profiler for hunting per-frame cost.
//
// It does NOT print per call. Each named zone accumulates count / total / avg / max.
//
// Usage:
//   #include "shared/types/ScopedProfiler.h"
//
//   void hotFunction() {
//       RUWA_PROFILE_ZONE("hotFunction");      // times this scope
//       ...
//   }
//
//   // measure cadence between repeated calls (e.g. animation ticks):
//   RUWA_PROFILE_FRAME(m_frameTimer, "frame interval");
//
//   RUWA_PROFILE_RESET();
//
// Turn the whole thing off (zero overhead) by setting RUWA_PROFILING to 0.

#ifndef RUWA_SHARED_TYPES_SCOPEDPROFILER_H
#define RUWA_SHARED_TYPES_SCOPEDPROFILER_H

#ifndef RUWA_PROFILING
#define RUWA_PROFILING 1
#endif

#if RUWA_PROFILING

#include <QElapsedTimer>
#include <QHash>
#include <QString>
#include <QtGlobal>
#include <limits>

namespace ruwa::diag {

class ProfileRegistry {
public:
    struct Stat {
        quint64 count = 0;
        qint64 totalNs = 0;
        qint64 maxNs = 0;
        qint64 minNs = std::numeric_limits<qint64>::max();
    };

    static ProfileRegistry& instance()
    {
        static ProfileRegistry registry;
        return registry;
    }

    // Keyed by the string-literal pointer itself: zone names must be literals,
    // which makes lookup a pointer hash (no string compare in the hot path).
    void add(const char* name, qint64 ns)
    {
        Stat& s = m_stats[name];
        ++s.count;
        s.totalNs += ns;
        if (ns > s.maxNs)
            s.maxNs = ns;
        if (ns < s.minNs)
            s.minNs = ns;
    }

    void report(const QString& title) { Q_UNUSED(title); }

    void reset() { m_stats.clear(); }

private:
    QHash<const char*, Stat> m_stats;
};

// RAII timer for a lexical scope.
class ScopedZone {
public:
    explicit ScopedZone(const char* name)
        : m_name(name)
    {
        m_timer.start();
    }
    ~ScopedZone() { ProfileRegistry::instance().add(m_name, m_timer.nsecsElapsed()); }

    ScopedZone(const ScopedZone&) = delete;
    ScopedZone& operator=(const ScopedZone&) = delete;

private:
    const char* m_name;
    QElapsedTimer m_timer;
};

// Records the wall-clock interval BETWEEN successive calls. The first call only
// arms the timer (no sample). Use it to measure real frame cadence: if this is
// far above ~16700 us while your scoped zones are cheap, the cost is elsewhere
// (almost always widget repaint), not in the instrumented code.
class FrameTimer {
public:
    void tick(const char* name)
    {
        if (m_armed) {
            ProfileRegistry::instance().add(name, m_timer.nsecsElapsed());
        }
        m_timer.restart();
        m_armed = true;
    }
    void reset() { m_armed = false; }

private:
    QElapsedTimer m_timer;
    bool m_armed = false;
};

} // namespace ruwa::diag

#define RUWA_PROFILE_CONCAT_(a, b) a##b
#define RUWA_PROFILE_CONCAT(a, b) RUWA_PROFILE_CONCAT_(a, b)

#define RUWA_PROFILE_ZONE(name)                                                                    \
    ::ruwa::diag::ScopedZone RUWA_PROFILE_CONCAT(ruwa_zone_, __LINE__)(name)

#define RUWA_PROFILE_FRAME(frameTimer, name) (frameTimer).tick(name)
#define RUWA_PROFILE_FRAME_RESET(frameTimer) (frameTimer).reset()

#define RUWA_PROFILE_REPORT(title) ::ruwa::diag::ProfileRegistry::instance().report(title)
#define RUWA_PROFILE_RESET() ::ruwa::diag::ProfileRegistry::instance().reset()

#else // RUWA_PROFILING == 0

#define RUWA_PROFILE_ZONE(name) ((void) 0)
#define RUWA_PROFILE_FRAME(frameTimer, name) ((void) 0)
#define RUWA_PROFILE_FRAME_RESET(frameTimer) ((void) 0)
#define RUWA_PROFILE_REPORT(title) ((void) 0)
#define RUWA_PROFILE_RESET() ((void) 0)

#endif // RUWA_PROFILING

#endif // RUWA_SHARED_TYPES_SCOPEDPROFILER_H
