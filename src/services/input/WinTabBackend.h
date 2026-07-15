// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_SERVICES_INPUT_WINTABBACKEND_H
#define RUWA_SERVICES_INPUT_WINTABBACKEND_H

#include <QPoint>
#include <QPointF>
#include <QString>
#include <QtGlobal>
#include <Qt>
#include <vector>

namespace ruwa::services::input {

class WinTabBackend {
public:
    struct PenSample {
        QPointF globalPos;
        float pressure = 0.0f;
        Qt::MouseButtons buttons = Qt::NoButton;
    };

    WinTabBackend();
    ~WinTabBackend();

    bool attach(void* hwnd);
    void detach();
    bool handleNativeEvent(void* message);

    /// Returns all packets accumulated since the last call and clears the internal buffer.
    /// Packets are ordered oldest-first.
    std::vector<PenSample> drainPendingPackets();

    bool isAvailable() const;
    bool isAttached() const;
    bool penDown() const;
    float pressure() const;
    int rawPressure() const;
    QPointF globalPos() const;
    Qt::MouseButtons buttons() const;
    bool inProximity() const;
    quint64 packetSerial() const;
    QString details() const;

private:
    bool ensureLoaded();
    void setDetails(const QString& details);

    struct Data;
    Data* m_data = nullptr;
};

} // namespace ruwa::services::input

#endif // RUWA_SERVICES_INPUT_WINTABBACKEND_H
