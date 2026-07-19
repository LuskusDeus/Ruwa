// SPDX-License-Identifier: MPL-2.0

#include "services/input/StylusDebugService.h"
#include "services/input/WinTabBackend.h"

#include <QtGlobal>

namespace ruwa::services::input {

StylusDebugService::StylusDebugService(QObject* parent)
    : QObject(parent)
    , m_winTabBackend(new WinTabBackend())
{
}

StylusDebugService::~StylusDebugService()
{
    delete m_winTabBackend;
    m_winTabBackend = nullptr;
}

StylusDebugService* StylusDebugService::instance()
{
    static StylusDebugService service;
    return &service;
}

void StylusDebugService::attachToWindow(void* hwnd)
{
    const Snapshot before = snapshot();
    m_winTabBackend->attach(hwnd);
    emitStateChangedIfNeeded(before);
}

void StylusDebugService::detachFromWindow()
{
    const Snapshot before = snapshot();
    m_winTabBackend->detach();
    emitStateChangedIfNeeded(before);
}

void StylusDebugService::handleNativeEvent(void* message)
{
    const Snapshot before = snapshot();
    m_winTabBackend->handleNativeEvent(message);
    emitStateChangedIfNeeded(before);
}

std::vector<WinTabBackend::PenSample> StylusDebugService::drainWinTabQueue()
{
    return m_winTabBackend->drainPendingPackets();
}

void StylusDebugService::updateQtTabletState(
    float qtPressure, float effectivePressure, bool fromTablet, const QString& eventType)
{
    const Snapshot before = snapshot();
    m_qtPressure = qBound(0.0f, qtPressure, 1.0f);
    m_effectivePressure = qBound(0.0f, effectivePressure, 1.0f);
    m_lastEventFromTablet = fromTablet;
    m_lastEventType = eventType;
    emitStateChangedIfNeeded(before);
}

StylusDebugService::Snapshot StylusDebugService::snapshot() const
{
    Snapshot state;
    state.winTabAvailable = m_winTabBackend->isAvailable();
    state.winTabAttached = m_winTabBackend->isAttached();
    state.winTabPenDown = m_winTabBackend->penDown();
    state.winTabPressure = m_winTabBackend->pressure();
    state.winTabRawPressure = m_winTabBackend->rawPressure();
    state.winTabGlobalPos = m_winTabBackend->globalPos();
    state.winTabButtons = m_winTabBackend->buttons();
    state.winTabInProximity = m_winTabBackend->inProximity();
    state.winTabPacketSerial = m_winTabBackend->packetSerial();
    state.winTabQueueOverflowCount = m_winTabBackend->queueOverflowCount();
    state.winTabDetails = m_winTabBackend->details();
    state.qtPressure = m_qtPressure;
    state.effectivePressure = m_effectivePressure;
    state.lastEventFromTablet = m_lastEventFromTablet;
    state.lastEventType = m_lastEventType;
    return state;
}

float StylusDebugService::effectivePressureOrFallback(float qtPressure) const
{
    if (m_winTabBackend->isAttached()) {
        const float winTabPressure = m_winTabBackend->pressure();
        if (m_winTabBackend->penDown() || m_winTabBackend->rawPressure() > 0) {
            return winTabPressure;
        }
    }

    return qBound(0.0f, qtPressure, 1.0f);
}

void StylusDebugService::emitStateChangedIfNeeded(const Snapshot& before)
{
    const Snapshot after = snapshot();
    if (before.winTabAvailable != after.winTabAvailable
        || before.winTabAttached != after.winTabAttached
        || before.winTabPenDown != after.winTabPenDown
        || before.winTabPressure != after.winTabPressure
        || before.winTabRawPressure != after.winTabRawPressure
        || before.winTabGlobalPos != after.winTabGlobalPos
        || before.winTabButtons != after.winTabButtons
        || before.winTabInProximity != after.winTabInProximity
        || before.winTabPacketSerial != after.winTabPacketSerial
        || before.winTabQueueOverflowCount != after.winTabQueueOverflowCount
        || before.winTabDetails != after.winTabDetails || before.qtPressure != after.qtPressure
        || before.effectivePressure != after.effectivePressure
        || before.lastEventFromTablet != after.lastEventFromTablet
        || before.lastEventType != after.lastEventType) {
        emit stateChanged();
    }
}

} // namespace ruwa::services::input
