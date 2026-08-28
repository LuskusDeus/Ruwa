// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_CANVASPARAMETEROVERLAYWIDGET_H
#define RUWA_UI_WORKSPACE_CANVASPARAMETEROVERLAYWIDGET_H

#include <QList>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <functional>

class QVariantAnimation;

namespace ruwa::ui::workspace {

/// Runtime form of a circle control. Values are resolved from the owning
/// feature's declarative parameter bindings before they reach this visual.
struct CanvasParameterCircleControl {
    QString id;
    QString valueParamKey;
    QPointF documentCenter;
    qreal documentRadius = 0.0;
    qreal minimumValue = 0.0;
    qreal maximumValue = 0.0;
    qreal stepValue = 1.0;
    bool integralValue = false;
};

/// Passive geometry/animation controller for feature-owned parameter controls.
/// Rendering is delegated to the canvas presentation capability; input stays
/// in CanvasPanel's existing application-level arbiter so these controls have
/// priority over every selected tool without intercepting QWidget events.
class CanvasParameterOverlayWidget final : public QWidget {
public:
    using DocumentToLocalFn = std::function<QPointF(const QPointF&)>;
    using PresentationChangedFn = std::function<void()>;

    struct ScreenCircle {
        QPointF center;
        qreal radius = 0.0;
    };

    explicit CanvasParameterOverlayWidget(QWidget* parent = nullptr);

    void setDocumentToLocalFn(DocumentToLocalFn fn);
    void setPresentationChangedFn(PresentationChangedFn fn);
    void setCircles(const QList<CanvasParameterCircleControl>& circles);
    const QList<CanvasParameterCircleControl>& circles() const { return m_circles; }
    const CanvasParameterCircleControl* circleAt(int index) const;
    int circleIndex(const QString& id) const;
    void setCircleRadius(const QString& id, qreal radius);

    int hitTest(const QPointF& localPosition) const;
    void setHoveredCircle(int index);
    int hoveredCircle() const { return m_hoveredCircle; }
    ScreenCircle screenCircleAt(int index) const;
    qreal hoverProgress(int index) const;

private:
    ScreenCircle screenCircle(const CanvasParameterCircleControl& circle) const;
    void notifyPresentationChanged();

    DocumentToLocalFn m_documentToLocal;
    PresentationChangedFn m_presentationChanged;
    QList<CanvasParameterCircleControl> m_circles;
    int m_hoveredCircle = -1;
    int m_hoverVisualCircle = -1;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation* m_hoverAnimation = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASPARAMETEROVERLAYWIDGET_H
