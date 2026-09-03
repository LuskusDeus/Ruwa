// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/CanvasParameterOverlayWidget.h"

#include <QApplication>
#include <QTransform>
#include <QVariantAnimation>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace ruwa::ui::workspace;

int main(int argc, char* argv[])
{
    // Also applies during Catch's discovery invocation, before CTest properties.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

namespace {

CanvasParameterControl positionControl()
{
    CanvasParameterControl control;
    control.id = QStringLiteral("center");
    control.type = CanvasParameterControlType::Position;
    control.centerXParamKey = QStringLiteral("centerX");
    control.centerYParamKey = QStringLiteral("centerY");
    control.documentCenter = QPointF(100.0, 200.0);
    return control;
}

CanvasParameterControl circleControl()
{
    auto control = positionControl();
    control.id = QStringLiteral("radius");
    control.type = CanvasParameterControlType::Circle;
    control.documentRadius = 40.0;
    return control;
}

} // namespace

TEST_CASE("Position hit area stays fixed through zoom, rotation and mirroring", "[canvas][overlay]")
{
    CanvasParameterOverlayWidget overlay;
    const auto position = positionControl();
    overlay.setControls({ position });
    for (const qreal zoom : { 0.1, 1.0, 8.0 }) {
        QTransform transform;
        transform.translate(300.0, 400.0);
        transform.rotate(73.0);
        transform.scale(-zoom, zoom);
        overlay.setDocumentToLocalFn([transform](const QPointF& p) { return transform.map(p); });
        const auto center = transform.map(position.documentCenter);
        REQUIRE(overlay.screenControlAt(0).center == center);
        REQUIRE(overlay.hitTest(center) == 0);
        REQUIRE(overlay.hitTest(center + QPointF(11.0, 0.0)) == 0);
        REQUIRE(overlay.hitTest(center + QPointF(13.0, 0.0)) == -1);
    }
}

TEST_CASE("Circle hit testing and declaration-order priority are preserved", "[canvas][overlay]")
{
    CanvasParameterOverlayWidget overlay;
    overlay.setDocumentToLocalFn([](const QPointF& p) { return p; });
    auto circle = circleControl();
    const auto position = positionControl();
    overlay.setControls({ circle, position });
    REQUIRE(overlay.hitTest(position.documentCenter) == 1);
    REQUIRE(overlay.hitTest(position.documentCenter + QPointF(40.0, 0.0)) == 0);
    REQUIRE(overlay.hitTest(position.documentCenter + QPointF(20.0, 0.0)) == -1);
    circle.documentRadius = 0.0;
    overlay.setControls({ circle, position });
    REQUIRE(overlay.hitTest(position.documentCenter) == 1);
    overlay.setControls({ position, circle });
    REQUIRE(overlay.hitTest(position.documentCenter) == 1);
}

TEST_CASE(
    "Moving a position updates only controls sharing its parameter bindings", "[canvas][overlay]")
{
    CanvasParameterOverlayWidget overlay;
    auto unrelated = circleControl();
    unrelated.id = QStringLiteral("other");
    unrelated.centerXParamKey = QStringLiteral("otherX");
    unrelated.centerYParamKey = QStringLiteral("otherY");
    overlay.setControls({ circleControl(), positionControl(), unrelated });
    overlay.setControlPosition(QStringLiteral("center"), QPointF(25.0, -30.0));
    REQUIRE(overlay.controlAt(0)->documentCenter == QPointF(25.0, -30.0));
    REQUIRE(overlay.controlAt(1)->documentCenter == QPointF(25.0, -30.0));
    REQUIRE(overlay.controlAt(2)->documentCenter == unrelated.documentCenter);
    REQUIRE(overlay.controlAt(0)->documentRadius == 40.0);
}

TEST_CASE("Position reuses circle hover animation and retains identity across refresh",
    "[canvas][overlay]")
{
    CanvasParameterOverlayWidget overlay;
    const auto position = positionControl();
    const auto circle = circleControl();
    overlay.setControls({ circle, position });
    auto* animation = overlay.findChild<QVariantAnimation*>();
    REQUIRE(animation);
    REQUIRE(animation->duration() == 120);
    overlay.setHoveredControl(1);
    animation->setCurrentTime(animation->duration());
    REQUIRE(overlay.hoverProgress(1) == 1.0);
    REQUIRE(overlay.hoverProgress(0) == 0.0);
    overlay.setControls({ position, circle });
    REQUIRE(overlay.hoveredControl() == 0);
    REQUIRE(overlay.hoverProgress(0) == 1.0);
    overlay.setHoveredControl(-1);
    animation->setCurrentTime(animation->duration());
    REQUIRE(overlay.hoverProgress(0) == 0.0);
    overlay.setHoveredControl(0);
    animation->setCurrentTime(60);
    overlay.setControls({});
    REQUIRE(overlay.hoveredControl() == -1);
    REQUIRE(animation->state() == QAbstractAnimation::Stopped);
    REQUIRE(overlay.isHidden());
}

TEST_CASE("Controls without a coordinate mapping cannot be hit", "[canvas][overlay]")
{
    CanvasParameterOverlayWidget overlay;
    overlay.setControls({ positionControl(), circleControl() });
    REQUIRE(overlay.hitTest(QPointF()) == -1);
    REQUIRE(overlay.controlAt(-1) == nullptr);
    REQUIRE(overlay.controlAt(2) == nullptr);
}
