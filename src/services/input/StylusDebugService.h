// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_INPUT_STYLUSDEBUGSERVICE_H
#define RUWA_SERVICES_INPUT_STYLUSDEBUGSERVICE_H

#include "services/input/WinTabBackend.h"

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QtGlobal>
#include <Qt>
#include <vector>

namespace ruwa::services::input {

class StylusDebugService : public QObject {
    Q_OBJECT

public:
    struct Snapshot {
        bool winTabAvailable = false;
        bool winTabAttached = false;
        bool winTabPenDown = false;
        float winTabPressure = 0.0f;
        int winTabRawPressure = 0;
        QPointF winTabGlobalPos;
        Qt::MouseButtons winTabButtons = Qt::NoButton;
        bool winTabInProximity = false;
        quint64 winTabPacketSerial = 0;
        quint64 winTabQueueOverflowCount = 0;
        QString winTabDetails;
        float qtPressure = 0.0f;
        float effectivePressure = 0.0f;
        bool lastEventFromTablet = false;
        QString lastEventType;
    };

    explicit StylusDebugService(QObject* parent = nullptr);
    ~StylusDebugService() override;

    static StylusDebugService* instance();

    void attachToWindow(void* hwnd);
    void detachFromWindow();
    void handleNativeEvent(void* message);
    std::vector<WinTabBackend::PenSample> drainWinTabQueue();
    void updateQtTabletState(
        float qtPressure, float effectivePressure, bool fromTablet, const QString& eventType);
    Snapshot snapshot() const;
    float effectivePressureOrFallback(float qtPressure) const;

signals:
    void stateChanged();

private:
    void emitStateChangedIfNeeded(const Snapshot& before);

    WinTabBackend* m_winTabBackend = nullptr;
    float m_qtPressure = 0.0f;
    float m_effectivePressure = 0.0f;
    bool m_lastEventFromTablet = false;
    QString m_lastEventType;
};

} // namespace ruwa::services::input

#endif // RUWA_SERVICES_INPUT_STYLUSDEBUGSERVICE_H
