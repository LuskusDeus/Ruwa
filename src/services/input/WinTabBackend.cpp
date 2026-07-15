// SPDX-License-Identifier: MPL-2.0

#include "services/input/WinTabBackend.h"

#include <QtCore/qglobal.h>
#include <QPoint>
#include <QPointF>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN

using HCTX = HANDLE;
using WTPKT = UINT;
using FIX32 = DWORD;

constexpr UINT WT_DEFBASE = 0x7FF0;
constexpr UINT WT_PACKET = WT_DEFBASE + 0;
constexpr UINT WT_PROXIMITY = WT_DEFBASE + 5;

constexpr UINT WTI_DEFCONTEXT = 3;
constexpr UINT WTI_DEVICES = 100;
constexpr UINT DVC_NPRESSURE = 15;

constexpr UINT CXO_SYSTEM = 0x0001;
constexpr UINT CXO_MESSAGES = 0x0004;

constexpr WTPKT PK_BUTTONS = 0x0040;
constexpr WTPKT PK_X = 0x0080;
constexpr WTPKT PK_Y = 0x0100;
constexpr WTPKT PK_NORMAL_PRESSURE = 0x0400;

constexpr WTPKT kPacketData = PK_BUTTONS | PK_X | PK_Y | PK_NORMAL_PRESSURE;

struct AXIS {
    LONG axMin;
    LONG axMax;
    UINT axUnits;
    FIX32 axResolution;
};

struct LOGCONTEXTA {
    char lcName[40];
    UINT lcOptions;
    UINT lcStatus;
    UINT lcLocks;
    UINT lcMsgBase;
    UINT lcDevice;
    UINT lcPktRate;
    WTPKT lcPktData;
    WTPKT lcPktMode;
    WTPKT lcMoveMask;
    DWORD lcBtnDnMask;
    DWORD lcBtnUpMask;
    LONG lcInOrgX;
    LONG lcInOrgY;
    LONG lcInOrgZ;
    LONG lcInExtX;
    LONG lcInExtY;
    LONG lcInExtZ;
    LONG lcOutOrgX;
    LONG lcOutOrgY;
    LONG lcOutOrgZ;
    LONG lcOutExtX;
    LONG lcOutExtY;
    LONG lcOutExtZ;
    FIX32 lcSensX;
    FIX32 lcSensY;
    FIX32 lcSensZ;
    BOOL lcSysMode;
    int lcSysOrgX;
    int lcSysOrgY;
    int lcSysExtX;
    int lcSysExtY;
    FIX32 lcSysSensX;
    FIX32 lcSysSensY;
};

struct Packet {
    DWORD pkButtons;
    LONG pkX;
    LONG pkY;
    UINT pkNormalPressure;
};

using WTInfoAFn = UINT(APIENTRY*)(UINT, UINT, LPVOID);
using WTOpenAFn = HCTX(APIENTRY*)(HWND, LOGCONTEXTA*, BOOL);
using WTCloseFn = BOOL(APIENTRY*)(HCTX);
using WTPacketFn = BOOL(APIENTRY*)(HCTX, UINT, LPVOID);
using WTPacketsGetFn = int(APIENTRY*)(HCTX, int, LPVOID);

// Ask WinTab for fixed-point screen coordinates. At low canvas zoom one integer
// screen pixel maps to many document pixels, so preserving subpixel packet
// positions prevents visible stair steps in the stroke path.
constexpr LONG kScreenCoordinateScale = 16;

QPointF screenPointFromPacket(LONG x, LONG y)
{
    return QPointF(static_cast<qreal>(x) / static_cast<qreal>(kScreenCoordinateScale),
        static_cast<qreal>(y) / static_cast<qreal>(kScreenCoordinateScale));
}

#endif

} // namespace

namespace ruwa::services::input {

struct WinTabBackend::Data {
#ifdef Q_OS_WIN
    HMODULE module = nullptr;
    WTInfoAFn wtInfoA = nullptr;
    WTOpenAFn wtOpenA = nullptr;
    WTCloseFn wtClose = nullptr;
    WTPacketFn wtPacket = nullptr;
    WTPacketsGetFn wtPacketsGet = nullptr;
    HCTX context = nullptr;
    HWND hwnd = nullptr;
#endif
    bool available = false;
    bool penDown = false;
    bool penEngaged = false; // hysteresis flag: true while pen is in contact
    float pressure = 0.0f;
    float smoothedPressure = 0.0f;
    int rawPressure = 0;
    int maxPressure = 0;
    QPointF globalPos;
    Qt::MouseButtons buttons = Qt::NoButton;
    bool inProximity = false;
    quint64 packetSerial = 0;
    QString details = QStringLiteral("WinTab32.dll not loaded");
    std::vector<WinTabBackend::PenSample> pendingPackets;
};

WinTabBackend::WinTabBackend()
    : m_data(new Data())
{
#ifndef Q_OS_WIN
    m_data->details = QStringLiteral("Only available on Windows");
#endif
}

bool WinTabBackend::ensureLoaded()
{
#ifdef Q_OS_WIN
    if (m_data->module) {
        return m_data->available;
    }

    m_data->module = LoadLibraryA("Wintab32.dll");
    if (!m_data->module) {
        m_data->details = QStringLiteral("WinTab32.dll not found");
        return false;
    }

    m_data->wtInfoA = reinterpret_cast<WTInfoAFn>(GetProcAddress(m_data->module, "WTInfoA"));
    m_data->wtOpenA = reinterpret_cast<WTOpenAFn>(GetProcAddress(m_data->module, "WTOpenA"));
    m_data->wtClose = reinterpret_cast<WTCloseFn>(GetProcAddress(m_data->module, "WTClose"));
    m_data->wtPacket = reinterpret_cast<WTPacketFn>(GetProcAddress(m_data->module, "WTPacket"));
    m_data->wtPacketsGet
        = reinterpret_cast<WTPacketsGetFn>(GetProcAddress(m_data->module, "WTPacketsGet"));

    m_data->available = m_data->wtInfoA && m_data->wtOpenA && m_data->wtClose && m_data->wtPacket;
    if (!m_data->available) {
        m_data->details = QStringLiteral("Incomplete WinTab API exports");
    } else {
        m_data->details = QStringLiteral("Driver loaded");
    }
    return m_data->available;
#else
    return false;
#endif
}

WinTabBackend::~WinTabBackend()
{
    detach();

#ifdef Q_OS_WIN
    if (m_data->module) {
        FreeLibrary(m_data->module);
        m_data->module = nullptr;
    }
#endif

    delete m_data;
}

bool WinTabBackend::attach(void* hwnd)
{
#ifndef Q_OS_WIN
    Q_UNUSED(hwnd);
    return false;
#else
    if (!hwnd) {
        return false;
    }

    if (!ensureLoaded()) {
        return false;
    }

    HWND window = reinterpret_cast<HWND>(hwnd);
    if (m_data->context && m_data->hwnd == window) {
        return true;
    }

    detach();

    LOGCONTEXTA context {};
    if (m_data->wtInfoA(WTI_DEFCONTEXT, 0, &context) == 0) {
        setDetails(QStringLiteral("WTInfoA(WTI_DEFCONTEXT) failed"));
        return false;
    }

    context.lcOptions |= CXO_MESSAGES;
    context.lcOptions &= ~CXO_SYSTEM;
    context.lcPktData = kPacketData;
    context.lcPktMode = 0;
    context.lcMoveMask = kPacketData;
    context.lcBtnUpMask = 0xFFFFFFFFu;
    context.lcBtnDnMask = 0xFFFFFFFFu;
    context.lcMsgBase = WT_DEFBASE;
    context.lcOutOrgX = GetSystemMetrics(SM_XVIRTUALSCREEN) * kScreenCoordinateScale;
    context.lcOutOrgY = GetSystemMetrics(SM_YVIRTUALSCREEN) * kScreenCoordinateScale;
    context.lcOutExtX = GetSystemMetrics(SM_CXVIRTUALSCREEN) * kScreenCoordinateScale;
    context.lcOutExtY = -GetSystemMetrics(SM_CYVIRTUALSCREEN) * kScreenCoordinateScale;

    m_data->context = m_data->wtOpenA(window, &context, TRUE);
    if (!m_data->context) {
        setDetails(QStringLiteral("WTOpenA failed"));
        return false;
    }

    AXIS pressureAxis {};
    if (m_data->wtInfoA(WTI_DEVICES + context.lcDevice, DVC_NPRESSURE, &pressureAxis) > 0) {
        m_data->maxPressure = qMax(1, static_cast<int>(pressureAxis.axMax));
    } else {
        m_data->maxPressure = 1023;
    }

    m_data->hwnd = window;
    setDetails(QStringLiteral("Context attached"));
    return true;
#endif
}

void WinTabBackend::detach()
{
#ifdef Q_OS_WIN
    if (m_data->wtClose && m_data->context) {
        m_data->wtClose(m_data->context);
    }
    m_data->context = nullptr;
    m_data->hwnd = nullptr;
#endif
    m_data->penDown = false;
    m_data->penEngaged = false;
    m_data->pressure = 0.0f;
    m_data->smoothedPressure = 0.0f;
    m_data->rawPressure = 0;
    m_data->buttons = Qt::NoButton;
    m_data->inProximity = false;
    m_data->pendingPackets.clear();
}

bool WinTabBackend::handleNativeEvent(void* message)
{
#ifndef Q_OS_WIN
    Q_UNUSED(message);
    return false;
#else
    if (!m_data->context || !message) {
        return false;
    }

    auto* msg = static_cast<MSG*>(message);
    if (msg->message == WT_PACKET && reinterpret_cast<HCTX>(msg->lParam) == m_data->context) {
        // Drain ALL buffered packets from WinTab's internal queue, not just the one referenced
        // by this WT_PACKET message. This recovers intermediate positions that would otherwise
        // be lost when WT_PACKET messages are dropped from the Windows message queue.
        constexpr int kDrainChunkSize = 64;

        // Hysteresis threshold: once the pen lifts (pressure == 0), require pressure
        // to exceed this value before re-engaging.  This filters digitiser noise that
        // otherwise creates phantom pen-down events (tiny dots) at the end of strokes.
        const int reEngageThreshold = qMax(3, m_data->maxPressure / 512);

        if (m_data->wtPacketsGet) {
            bool receivedPackets = false;
            int count = 0;
            do {
                Packet packets[kDrainChunkSize];
                count = m_data->wtPacketsGet(m_data->context, kDrainChunkSize, packets);
                if (count <= 0) {
                    break;
                }

                receivedPackets = true;
                for (int i = 0; i < count; ++i) {
                    const Packet& p = packets[i];
                    const int rawPressure = static_cast<int>(p.pkNormalPressure);
                    const float rawNorm = qBound(
                        0.0f, static_cast<float>(rawPressure) / qMax(1, m_data->maxPressure), 1.0f);
                    const WORD buttonMask = LOWORD(p.pkButtons);
                    Qt::MouseButtons buttons = Qt::NoButton;
                    if (buttonMask & 0x0001u)
                        buttons |= Qt::LeftButton;
                    if (buttonMask & 0x0002u)
                        buttons |= Qt::RightButton;
                    if (buttonMask & 0x0004u)
                        buttons |= Qt::MiddleButton;

                    // Apply hysteresis: while engaged, stay engaged until pressure
                    // drops to zero; while disengaged, require exceeding the threshold.
                    if (m_data->penEngaged) {
                        if (rawPressure > 0) {
                            buttons |= Qt::LeftButton;
                        } else {
                            m_data->penEngaged = false;
                        }
                    } else {
                        if (rawPressure > reEngageThreshold) {
                            m_data->penEngaged = true;
                            buttons |= Qt::LeftButton;
                        }
                    }

                    // Light EMA smoothing to suppress pressure spikes from the
                    // digitiser (especially visible as thickness bumps at stroke
                    // end).  On first contact, seed with the raw value (no lag).
                    float pressure;
                    if (!m_data->penEngaged && m_data->smoothedPressure == 0.0f) {
                        pressure = rawNorm;
                    } else if (m_data->smoothedPressure == 0.0f && rawNorm > 0.0f) {
                        pressure = rawNorm; // first sample of a new stroke
                    } else {
                        constexpr float kAlpha = 0.65f;
                        pressure = kAlpha * rawNorm + (1.0f - kAlpha) * m_data->smoothedPressure;
                    }
                    m_data->smoothedPressure = pressure;

                    PenSample sample;
                    sample.globalPos = screenPointFromPacket(p.pkX, p.pkY);
                    sample.pressure = pressure;
                    sample.buttons = buttons;
                    m_data->pendingPackets.push_back(sample);

                    // Keep current-state fields in sync (last packet wins, for snapshot readers)
                    m_data->rawPressure = rawPressure;
                    m_data->pressure = pressure;
                    m_data->globalPos = sample.globalPos;
                    m_data->buttons = buttons;
                    m_data->penDown = buttons.testFlag(Qt::LeftButton);
                    m_data->inProximity = true;
                    ++m_data->packetSerial;
                }

                // WTPacketsGet returns at most one chunk. Keep polling while the
                // buffer was filled so a burst larger than one chunk is not split across
                // stale WT_PACKET notifications in the Windows message queue.
            } while (count == kDrainChunkSize);

            if (receivedPackets) {
                setDetails(QStringLiteral("Receiving WT_PACKET"));
                return true;
            }
        }

        // Fallback: WTPacketsGet not available — read the single packet by serial number
        Packet packet {};
        if (!m_data->wtPacket(m_data->context, static_cast<UINT>(msg->wParam), &packet)) {
            setDetails(QStringLiteral("WTPacket failed"));
            return false;
        }
        m_data->rawPressure = static_cast<int>(packet.pkNormalPressure);
        const float rawNorm = qBound(
            0.0f, static_cast<float>(m_data->rawPressure) / qMax(1, m_data->maxPressure), 1.0f);
        m_data->globalPos = screenPointFromPacket(packet.pkX, packet.pkY);
        const WORD buttonMask = LOWORD(packet.pkButtons);
        Qt::MouseButtons buttons = Qt::NoButton;
        if (buttonMask & 0x0001u)
            buttons |= Qt::LeftButton;
        if (buttonMask & 0x0002u)
            buttons |= Qt::RightButton;
        if (buttonMask & 0x0004u)
            buttons |= Qt::MiddleButton;

        if (m_data->penEngaged) {
            if (m_data->rawPressure > 0) {
                buttons |= Qt::LeftButton;
            } else {
                m_data->penEngaged = false;
            }
        } else {
            if (m_data->rawPressure > reEngageThreshold) {
                m_data->penEngaged = true;
                buttons |= Qt::LeftButton;
            }
        }

        // EMA pressure smoothing (same as batch path)
        if (m_data->smoothedPressure == 0.0f && rawNorm > 0.0f) {
            m_data->pressure = rawNorm;
        } else {
            constexpr float kAlpha = 0.65f;
            m_data->pressure = kAlpha * rawNorm + (1.0f - kAlpha) * m_data->smoothedPressure;
        }
        m_data->smoothedPressure = m_data->pressure;

        m_data->buttons = buttons;
        m_data->penDown = buttons.testFlag(Qt::LeftButton);
        m_data->inProximity = true;
        ++m_data->packetSerial;

        PenSample sample;
        sample.globalPos = m_data->globalPos;
        sample.pressure = m_data->pressure;
        sample.buttons = m_data->buttons;
        m_data->pendingPackets.push_back(sample);

        setDetails(QStringLiteral("Receiving WT_PACKET"));
        return true;
    }

    if (msg->message == WT_PROXIMITY && reinterpret_cast<HCTX>(msg->wParam) == m_data->context) {
        // WinTab stores proximity flags in lParam; it is not a context handle.
        // LOWORD != 0 means entering this context, LOWORD == 0 means leaving it.
        const bool enteringContext = LOWORD(msg->lParam) != 0;

        if (enteringContext) {
            m_data->penDown = false;
            m_data->penEngaged = false;
            m_data->pressure = 0.0f;
            m_data->smoothedPressure = 0.0f;
            m_data->rawPressure = 0;
            m_data->buttons = Qt::NoButton;
            m_data->inProximity = true;
            setDetails(QStringLiteral("WT_PROXIMITY enter"));
            return true;
        }

        // Some drivers send proximity-leave before a final zero-pressure packet.
        // Put a terminal sample through the normal dispatch path so the active Qt
        // widget/canvas always receives its MouseButtonRelease.
        PenSample terminalSample;
        terminalSample.globalPos = m_data->globalPos;
        terminalSample.pressure = 0.0f;
        terminalSample.buttons = Qt::NoButton;
        m_data->pendingPackets.push_back(terminalSample);

        m_data->penDown = false;
        m_data->penEngaged = false;
        m_data->pressure = 0.0f;
        m_data->smoothedPressure = 0.0f;
        m_data->rawPressure = 0;
        m_data->buttons = Qt::NoButton;
        m_data->inProximity = false;
        ++m_data->packetSerial;
        setDetails(QStringLiteral("WT_PROXIMITY leave"));
        return true;
    }

    return false;
#endif
}

std::vector<WinTabBackend::PenSample> WinTabBackend::drainPendingPackets()
{
    std::vector<PenSample> result;
    result.swap(m_data->pendingPackets);
    return result;
}

bool WinTabBackend::isAvailable() const
{
    return m_data->available;
}

bool WinTabBackend::isAttached() const
{
#ifdef Q_OS_WIN
    return m_data->context != nullptr;
#else
    return false;
#endif
}

bool WinTabBackend::penDown() const
{
    return m_data->penDown;
}

float WinTabBackend::pressure() const
{
    return m_data->pressure;
}

int WinTabBackend::rawPressure() const
{
    return m_data->rawPressure;
}

QPointF WinTabBackend::globalPos() const
{
    return m_data->globalPos;
}

Qt::MouseButtons WinTabBackend::buttons() const
{
    return m_data->buttons;
}

bool WinTabBackend::inProximity() const
{
    return m_data->inProximity;
}

quint64 WinTabBackend::packetSerial() const
{
    return m_data->packetSerial;
}

QString WinTabBackend::details() const
{
    return m_data->details;
}

void WinTabBackend::setDetails(const QString& details)
{
    m_data->details = details;
}

} // namespace ruwa::services::input
