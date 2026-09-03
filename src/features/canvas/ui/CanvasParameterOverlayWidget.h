// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_CANVASPARAMETEROVERLAYWIDGET_H
#define RUWA_UI_WORKSPACE_CANVASPARAMETEROVERLAYWIDGET_H

#include "features/canvas/overlays/CursorOverlayState.h"

#include <QList>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <functional>

class QVariantAnimation;

namespace ruwa::ui::workspace {

/// Runtime form of a parameter control. Values are resolved from the owning
/// feature's declarative parameter bindings before they reach this visual.
struct CanvasParameterControl {
    QString id;
    CanvasParameterControlType type = CanvasParameterControlType::Circle;
    QString valueParamKey;
    QString centerXParamKey;
    QString centerYParamKey;
    QPointF documentCenter;
    qreal documentRadius = 0.0;
    qreal minimumValue = 0.0;
    qreal maximumValue = 0.0;
    qreal stepValue = 1.0;
    bool integralValue = false;
    QPointF minimumPosition;
    QPointF maximumPosition;
    QPointF positionStep;
    bool integralX = false;
    bool integralY = false;
};

/// Passive geometry/animation controller for feature-owned parameter controls.
/// Rendering is delegated to the canvas presentation capability; input stays
/// in CanvasPanel's existing application-level arbiter so these controls have
/// priority over every selected tool without intercepting QWidget events.
class CanvasParameterOverlayWidget final : public QWidget {
public:
    using DocumentToLocalFn = std::function<QPointF(const QPointF&)>;
    using PresentationChangedFn = std::function<void()>;

    struct ScreenControl {
        QPointF center;
        qreal radius = 0.0;
    };

    explicit CanvasParameterOverlayWidget(QWidget* parent = nullptr);

    void setDocumentToLocalFn(DocumentToLocalFn fn);
    void setPresentationChangedFn(PresentationChangedFn fn);
    void setControls(const QList<CanvasParameterControl>& controls);
    const QList<CanvasParameterControl>& controls() const { return m_controls; }
    const CanvasParameterControl* controlAt(int index) const;
    int controlIndex(const QString& id) const;
    void setCircleRadius(const QString& id, qreal radius);
    void setControlPosition(const QString& id, const QPointF& position);

    int hitTest(const QPointF& localPosition) const;
    void setHoveredControl(int index);
    int hoveredControl() const { return m_hoveredControl; }
    ScreenControl screenControlAt(int index) const;
    qreal hoverProgress(int index) const;

private:
    ScreenControl screenControl(const CanvasParameterControl& control) const;
    void notifyPresentationChanged();

    DocumentToLocalFn m_documentToLocal;
    PresentationChangedFn m_presentationChanged;
    QList<CanvasParameterControl> m_controls;
    int m_hoveredControl = -1;
    int m_hoverVisualControl = -1;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation* m_hoverAnimation = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASPARAMETEROVERLAYWIDGET_H
