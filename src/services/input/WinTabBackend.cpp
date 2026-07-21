// SPDX-License-Identifier: MPL-2.0

#include "services/input/WinTabBackend.h"

#include <QtCore/qglobal.h>
#include <QPoint>
#include <QPointF>

#include <limits>
#include <unordered_map>

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

struct PressureRange {
    int minimum = 0;
    int maximum = 0;

    bool isValid() const { return maximum > minimum; }
};

#ifdef Q_OS_WIN

using HCTX = HANDLE;
using WTPKT = DWORD;
using FIX32 = DWORD;

constexpr UINT WT_DEFBASE = 0x7FF0;
constexpr UINT WT_PACKET = WT_DEFBASE + 0;
constexpr UINT WT_PROXIMITY = WT_DEFBASE + 5;

constexpr UINT WTI_DEFSYSCTX = 4;
constexpr UINT WTI_INTERFACE = 1;
constexpr UINT WTI_DEVICES = 100;
constexpr UINT IFC_NDEVICES = 4;
constexpr UINT DVC_NCSRTYPES = 3;
constexpr UINT DVC_FIRSTCSR = 4;
constexpr UINT DVC_NPRESSURE = 15;

constexpr UINT CXO_SYSTEM = 0x0001;
constexpr UINT CXO_MESSAGES = 0x0004;

constexpr WTPKT PK_STATUS = 0x0002;
constexpr WTPKT PK_TIME = 0x0004;
constexpr WTPKT PK_CURSOR = 0x0020;
constexpr WTPKT PK_BUTTONS = 0x0040;
constexpr WTPKT PK_X = 0x0080;
constexpr WTPKT PK_Y = 0x0100;
constexpr WTPKT PK_NORMAL_PRESSURE = 0x0400;

constexpr WTPKT kExtendedPacketData
    = PK_STATUS | PK_TIME | PK_CURSOR | PK_BUTTONS | PK_X | PK_Y | PK_NORMAL_PRESSURE;
constexpr WTPKT kExtendedPacketMoveMask = PK_CURSOR | PK_BUTTONS | PK_X | PK_Y | PK_NORMAL_PRESSURE;
constexpr WTPKT kBasicPacketData = PK_BUTTONS | PK_X | PK_Y | PK_NORMAL_PRESSURE;
constexpr UINT TPS_QUEUE_ERR = 0x0002;

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
    UINT pkStatus;
    DWORD pkTime;
    UINT pkCursor;
    DWORD pkButtons;
    LONG pkX;
    LONG pkY;
    UINT pkNormalPressure;
};

struct BasicPacket {
    DWORD pkButtons;
    LONG pkX;
    LONG pkY;
    UINT pkNormalPressure;
};

static_assert(sizeof(LOGCONTEXTA) == 172, "WinTab LOGCONTEXTA ABI mismatch");
static_assert(sizeof(Packet) == 28, "WinTab PACKET ABI mismatch");
static_assert(sizeof(BasicPacket) == 16, "WinTab basic PACKET ABI mismatch");

using WTInfoAFn = UINT(APIENTRY*)(UINT, UINT, LPVOID);
using WTOpenAFn = HCTX(APIENTRY*)(HWND, LOGCONTEXTA*, BOOL);
using WTCloseFn = BOOL(APIENTRY*)(HCTX);
using WTPacketFn = BOOL(APIENTRY*)(HCTX, UINT, LPVOID);
using WTPacketsGetFn = int(APIENTRY*)(HCTX, int, LPVOID);
using WTQueueSizeGetFn = int(APIENTRY*)(HCTX);
using WTQueueSizeSetFn = BOOL(APIENTRY*)(HCTX, int);

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
    WTQueueSizeGetFn wtQueueSizeGet = nullptr;
    WTQueueSizeSetFn wtQueueSizeSet = nullptr;
    HCTX context = nullptr;
    HWND hwnd = nullptr;
    bool extendedPacketData = false;
#endif
    bool available = false;
    bool penDown = false;
    bool penEngaged = false; // hysteresis flag: true while pen is in contact
    float pressure = 0.0f;
    float smoothedPressure = 0.0f;
    int rawPressure = 0;
    PressureRange fallbackPressure;
    std::unordered_map<quint32, PressureRange> pressureByCursor;
    QPointF globalPos;
    Qt::MouseButtons buttons = Qt::NoButton;
    bool inProximity = false;
    quint64 packetSerial = 0;
    quint64 queueOverflowCount = 0;
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
    // Optional exports: absence must not disqualify the driver.
    m_data->wtQueueSizeGet
        = reinterpret_cast<WTQueueSizeGetFn>(GetProcAddress(m_data->module, "WTQueueSizeGet"));
    m_data->wtQueueSizeSet
        = reinterpret_cast<WTQueueSizeSetFn>(GetProcAddress(m_data->module, "WTQueueSizeSet"));

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
    if (m_data->wtInfoA(WTI_DEFSYSCTX, 0, &context) == 0) {
        setDetails(QStringLiteral("WTInfoA(WTI_DEFSYSCTX) failed"));
        return false;
    }

    // Keep the driver's system cursor context active. Ruwa still receives the
    // same full-resolution packets through CXO_MESSAGES, while WinTab remains
    // responsible for system-wide hover and button routing outside our HWND
    // (for example, the Windows taskbar).
    context.lcOptions |= CXO_MESSAGES | CXO_SYSTEM;
    context.lcPktData = kExtendedPacketData;
    context.lcPktMode = 0;
    // PK_TIME changes for every hardware report and must not be in lcMoveMask:
    // including it turns stationary hover into an avoidable message-rate flood.
    context.lcMoveMask = kExtendedPacketMoveMask;
    context.lcBtnUpMask = 0xFFFFFFFFu;
    context.lcBtnDnMask = 0xFFFFFFFFu;
    context.lcMsgBase = WT_DEFBASE;
    context.lcOutOrgX = GetSystemMetrics(SM_XVIRTUALSCREEN) * kScreenCoordinateScale;
    context.lcOutOrgY = GetSystemMetrics(SM_YVIRTUALSCREEN) * kScreenCoordinateScale;
    context.lcOutExtX = GetSystemMetrics(SM_CXVIRTUALSCREEN) * kScreenCoordinateScale;
    context.lcOutExtY = -GetSystemMetrics(SM_CYVIRTUALSCREEN) * kScreenCoordinateScale;

    m_data->context = m_data->wtOpenA(window, &context, TRUE);
    m_data->extendedPacketData = m_data->context != nullptr;
    if (!m_data->context) {
        // PK_STATUS, PK_TIME and PK_CURSOR are standard WinTab fields, but
        // older or incomplete implementations can still reject a context that
        // requests them. Preserve drawing with the original minimum packet
        // contract; only hardware timing and overflow diagnostics are lost.
        context.lcPktData = kBasicPacketData;
        context.lcMoveMask = kBasicPacketData;
        m_data->context = m_data->wtOpenA(window, &context, TRUE);
    }
    if (!m_data->context) {
        setDetails(QStringLiteral("WTOpenA failed"));
        return false;
    }

    // Grow the driver's packet queue. The WinTab default is tiny (the spec
    // suggests around 8 packets, i.e. ~30-60 ms at 133-266 Hz report rates), so
    // any GUI-thread stall longer than that — a vsync-blocked SwapBuffers in a
    // maximized window, one heavy frame — makes the driver silently drop the
    // oldest packets and strokes come out faceted.
    //
    // CAUTION (per the WinTab spec): a failed WTQueueSizeSet leaves the context
    // with NO queue at all — the old queue is destroyed before the new one is
    // allocated, and both WTPacketsGet and WTPacket read from that queue. So a
    // failure here must never be the last call: keep retrying smaller sizes,
    // then the driver's original size, down to 1 packet as the final fallback
    // (the pattern Qt's own WinTab path uses).
    if (m_data->wtQueueSizeSet) {
        const int originalQueueSize
            = m_data->wtQueueSizeGet ? m_data->wtQueueSizeGet(m_data->context) : 0;
        bool queueResized = false;
        for (int queueSize = 512; queueSize >= 32; queueSize /= 2) {
            if (m_data->wtQueueSizeSet(m_data->context, queueSize)) {
                queueResized = true;
                break;
            }
        }
        if (!queueResized && originalQueueSize > 0) {
            queueResized = m_data->wtQueueSizeSet(m_data->context, originalQueueSize);
        }
        if (!queueResized) {
            for (int queueSize = 16; queueSize >= 1; queueSize /= 2) {
                if (m_data->wtQueueSizeSet(m_data->context, queueSize)) {
                    queueResized = true;
                    break;
                }
            }
        }
        if (!queueResized) {
            // The failed WTQueueSizeSet calls destroyed this context's queue.
            // Reopening is the only way to recover the driver's default queue;
            // keeping the old HCTX would leave an attached-looking backend that
            // can never return another packet.
            m_data->wtClose(m_data->context);
            m_data->context = m_data->wtOpenA(window, &context, TRUE);
            if (!m_data->context) {
                setDetails(QStringLiteral("WinTab queue recovery reopen failed"));
                return false;
            }
        }
    }

    m_data->fallbackPressure = {};
    m_data->pressureByCursor.clear();

    const auto pressureRangeForDevice = [this](UINT device) -> PressureRange {
        AXIS pressureAxis {};
        if (m_data->wtInfoA(WTI_DEVICES + device, DVC_NPRESSURE, &pressureAxis) == 0) {
            return {};
        }
        PressureRange range { static_cast<int>(pressureAxis.axMin),
            static_cast<int>(pressureAxis.axMax) };
        return range.isValid() ? range : PressureRange {};
    };

    const UINT virtualDevice = std::numeric_limits<UINT>::max();
    if (context.lcDevice != virtualDevice) {
        m_data->fallbackPressure = pressureRangeForDevice(context.lcDevice);
    } else {
        // The default system context is commonly a virtual device accepting
        // packets from every attached tablet. WTI_DEVICES + UINT(-1) is not a
        // valid information category, so map pkCursor to the owning device's
        // pressure axis instead. This also keeps pressure correct when two
        // tablets with different ranges are connected.
        UINT deviceCount = 0;
        if (m_data->wtInfoA(WTI_INTERFACE, IFC_NDEVICES, &deviceCount) > 0) {
            for (UINT device = 0; device < deviceCount; ++device) {
                const PressureRange range = pressureRangeForDevice(device);
                if (!range.isValid()) {
                    continue;
                }
                if (!m_data->fallbackPressure.isValid()) {
                    m_data->fallbackPressure = range;
                }

                UINT firstCursor = 0;
                UINT cursorCount = 0;
                if (m_data->wtInfoA(WTI_DEVICES + device, DVC_FIRSTCSR, &firstCursor) == 0
                    || m_data->wtInfoA(WTI_DEVICES + device, DVC_NCSRTYPES, &cursorCount) == 0) {
                    continue;
                }
                for (UINT offset = 0; offset < cursorCount; ++offset) {
                    m_data->pressureByCursor[firstCursor + offset] = range;
                }
            }
        }
    }
    if (!m_data->fallbackPressure.isValid()) {
        m_data->fallbackPressure = { 0, 1023 };
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
    m_data->extendedPacketData = false;
#endif
    m_data->penDown = false;
    m_data->penEngaged = false;
    m_data->pressure = 0.0f;
    m_data->smoothedPressure = 0.0f;
    m_data->rawPressure = 0;
    m_data->buttons = Qt::NoButton;
    m_data->inProximity = false;
    m_data->queueOverflowCount = 0;
    m_data->pressureByCursor.clear();
    m_data->fallbackPressure = {};
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

        const auto processPacket = [this](const Packet& packet, bool hasMetadata) {
            const auto rangeIt = hasMetadata ? m_data->pressureByCursor.find(packet.pkCursor)
                                             : m_data->pressureByCursor.end();
            const PressureRange& pressureRange = rangeIt != m_data->pressureByCursor.end()
                ? rangeIt->second
                : m_data->fallbackPressure;
            const int pressureSpan = qMax(1, pressureRange.maximum - pressureRange.minimum);
            const int rawPressure = static_cast<int>(packet.pkNormalPressure);
            const float rawNorm = qBound(0.0f,
                static_cast<float>(rawPressure - pressureRange.minimum)
                    / static_cast<float>(pressureSpan),
                1.0f);

            const WORD buttonMask = LOWORD(packet.pkButtons);
            Qt::MouseButtons buttons = Qt::NoButton;
            if (buttonMask & 0x0001u)
                buttons |= Qt::LeftButton;
            if (buttonMask & 0x0002u)
                buttons |= Qt::RightButton;
            if (buttonMask & 0x0004u)
                buttons |= Qt::MiddleButton;

            const int reEngageThreshold = qMax(3, pressureSpan / 512);
            if (m_data->penEngaged) {
                if (rawPressure > pressureRange.minimum) {
                    buttons |= Qt::LeftButton;
                } else {
                    m_data->penEngaged = false;
                }
            } else if (rawPressure - pressureRange.minimum > reEngageThreshold) {
                m_data->penEngaged = true;
                buttons |= Qt::LeftButton;
            }

            float pressure = 0.0f;
            if (!m_data->penEngaged && rawPressure <= pressureRange.minimum) {
                // Do not carry the previous EMA tail into a new contact while
                // the pen remains in proximity.
                m_data->smoothedPressure = 0.0f;
            } else if (m_data->smoothedPressure == 0.0f && rawNorm > 0.0f) {
                pressure = rawNorm;
                m_data->smoothedPressure = pressure;
            } else {
                constexpr float kAlpha = 0.65f;
                pressure = kAlpha * rawNorm + (1.0f - kAlpha) * m_data->smoothedPressure;
                m_data->smoothedPressure = pressure;
            }

            if (hasMetadata && (packet.pkStatus & TPS_QUEUE_ERR) != 0) {
                ++m_data->queueOverflowCount;
            }

            PenSample sample;
            sample.globalPos = screenPointFromPacket(packet.pkX, packet.pkY);
            sample.pressure = pressure;
            sample.buttons = buttons;
            sample.packetTimeMs = static_cast<quint32>(packet.pkTime);
            sample.hasPacketTime = hasMetadata;
            m_data->pendingPackets.push_back(sample);

            m_data->rawPressure = rawPressure;
            m_data->pressure = pressure;
            m_data->globalPos = sample.globalPos;
            m_data->buttons = buttons;
            m_data->penDown = buttons.testFlag(Qt::LeftButton);
            m_data->inProximity = true;
            ++m_data->packetSerial;
        };

        const auto processBasicPacket = [&processPacket](const BasicPacket& basicPacket) {
            Packet packet {};
            packet.pkButtons = basicPacket.pkButtons;
            packet.pkX = basicPacket.pkX;
            packet.pkY = basicPacket.pkY;
            packet.pkNormalPressure = basicPacket.pkNormalPressure;
            processPacket(packet, false);
        };

        if (m_data->wtPacketsGet) {
            bool receivedPackets = false;
            int count = 0;
            if (m_data->extendedPacketData) {
                do {
                    Packet packets[kDrainChunkSize];
                    count = m_data->wtPacketsGet(m_data->context, kDrainChunkSize, packets);
                    if (count <= 0) {
                        break;
                    }

                    receivedPackets = true;
                    for (int i = 0; i < count; ++i) {
                        processPacket(packets[i], true);
                    }
                } while (count == kDrainChunkSize);
            } else {
                do {
                    BasicPacket packets[kDrainChunkSize];
                    count = m_data->wtPacketsGet(m_data->context, kDrainChunkSize, packets);
                    if (count <= 0) {
                        break;
                    }

                    receivedPackets = true;
                    for (int i = 0; i < count; ++i) {
                        processBasicPacket(packets[i]);
                    }
                } while (count == kDrainChunkSize);
            }

            if (receivedPackets) {
                setDetails(m_data->queueOverflowCount > 0
                        ? QStringLiteral("Receiving WT_PACKET (queue overflows: %1)")
                              .arg(m_data->queueOverflowCount)
                        : QStringLiteral("Receiving WT_PACKET"));
                return true;
            }
        }

        // Fallback: WTPacketsGet not available — read the single packet by serial number
        Packet packet {};
        BasicPacket basicPacket {};
        void* packetData = m_data->extendedPacketData ? static_cast<void*>(&packet)
                                                      : static_cast<void*>(&basicPacket);
        if (!m_data->wtPacket(m_data->context, static_cast<UINT>(msg->wParam), packetData)) {
            // Expected for stale WT_PACKET messages after WTPacketsGet drained
            // the referenced serial and every older packet.
            if (!m_data->wtPacketsGet) {
                setDetails(QStringLiteral("WTPacket failed"));
            }
            return false;
        }
        if (m_data->extendedPacketData) {
            processPacket(packet, true);
        } else {
            processBasicPacket(basicPacket);
        }
        setDetails(m_data->queueOverflowCount > 0
                ? QStringLiteral("Receiving WT_PACKET (queue overflows: %1)")
                      .arg(m_data->queueOverflowCount)
                : QStringLiteral("Receiving WT_PACKET"));
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

quint64 WinTabBackend::queueOverflowCount() const
{
    return m_data->queueOverflowCount;
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
