// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/selection/SelectionMaskOps.h"
#include "features/layers/model/LayerData.h"

#include <catch2/catch_test_macros.hpp>
#include <QGuiApplication>

using ruwa::core::layers::LayerData;
using ruwa::core::layers::LayerType;

namespace {

// LayerData owns QPixmaps; no windows or GL context are needed for these tests.
struct SelectionContentTargetsTest {
    int argc = 1;
    char name[32] = "selection-content-targets-test";
    char* argv[2] = { name, nullptr };
    QGuiApplication application { argc, argv };
};

std::shared_ptr<LayerData> makeLayer(LayerType type)
{
    auto layer = std::make_shared<LayerData>();
    layer->id = QUuid::createUuid();
    layer->type = type;
    if (type == LayerType::Raster) {
        layer->tileGrid = std::make_unique<aether::TileGrid>();
    }
    return layer;
}

} // namespace

TEST_CASE_METHOD(SelectionContentTargetsTest,
    "Selection content edits include every selected layer regardless of primary order")
{
    auto raster = makeLayer(LayerType::Raster);
    auto text = makeLayer(LayerType::Text);
    auto smart = makeLayer(LayerType::Smart);

    // An empty raster still needs to receive a fill, while text and smart layers
    // must remain in the target set so the caller can offer rasterization.
    REQUIRE(raster->tileGrid->empty());
    for (const bool editMasks : { false, true }) {
        const auto targets = aether::selectionContentEditTargets(
            { raster.get(), text.get(), smart.get() }, editMasks);
        REQUIRE(targets.size() == 3);
        REQUIRE(targets.contains(raster->id));
        REQUIRE(targets.contains(text->id));
        REQUIRE(targets.contains(smart->id));

        const auto reversed = aether::selectionContentEditTargets(
            { smart.get(), text.get(), raster.get() }, editMasks);
        REQUIRE(reversed.size() == targets.size());
        for (const auto& id : targets) {
            REQUIRE(reversed.contains(id));
        }
    }
}

TEST_CASE_METHOD(SelectionContentTargetsTest,
    "Selected collapsed groups expand recursively without editing descendants twice")
{
    auto group = makeLayer(LayerType::Group);
    auto nested = makeLayer(LayerType::Group);
    auto raster = makeLayer(LayerType::Raster);
    auto text = makeLayer(LayerType::Text);
    auto smart = makeLayer(LayerType::Smart);
    group->expanded = false;
    nested->expanded = false;
    group->addChild(raster);
    group->addChild(nested);
    nested->addChild(text);
    nested->addChild(smart);

    for (const bool editMasks : { false, true }) {
        const auto targets = aether::selectionContentEditTargets({ group.get() }, editMasks);
        REQUIRE(targets.size() == 3);
        REQUIRE(targets.contains(raster->id));
        REQUIRE(targets.contains(text->id));
        REQUIRE(targets.contains(smart->id));

        const auto overlapping = aether::selectionContentEditTargets(
            { text.get(), group.get(), smart.get(), nested.get(), raster.get() }, editMasks);
        REQUIRE(overlapping.size() == targets.size());
        for (const auto& id : targets) {
            REQUIRE(overlapping.count(id) == 1);
        }
    }
}

TEST_CASE_METHOD(SelectionContentTargetsTest,
    "Selection content edits respect hidden and locked ancestors even for explicit descendants")
{
    auto group = makeLayer(LayerType::Group);
    auto raster = makeLayer(LayerType::Raster);
    auto text = makeLayer(LayerType::Text);
    auto lockedGroup = makeLayer(LayerType::Group);
    auto smart = makeLayer(LayerType::Smart);
    auto hidden = makeLayer(LayerType::Raster);
    group->addChild(raster);
    group->addChild(text);
    group->addChild(lockedGroup);
    group->addChild(hidden);
    lockedGroup->addChild(smart);
    text->locked = true;
    lockedGroup->locked = true;
    hidden->visible = false;

    for (const bool editMasks : { false, true }) {
        const auto targets = aether::selectionContentEditTargets(
            { group.get(), text.get(), smart.get(), hidden.get() }, editMasks);
        REQUIRE(targets == QList<QUuid> { raster->id });
    }

    group->visible = false;
    REQUIRE(aether::selectionContentEditTargets({ raster.get() }).isEmpty());
    group->visible = true;
    group->locked = true;
    REQUIRE(aether::selectionContentEditTargets({ raster.get() }).isEmpty());
}

TEST_CASE_METHOD(SelectionContentTargetsTest,
    "Selection content edits ignore unsupported layers and preserve fill mask targeting")
{
    auto background = makeLayer(LayerType::Background);
    auto adjustment = makeLayer(LayerType::Adjustment);
    auto group = makeLayer(LayerType::Group);
    auto missingGrid = makeLayer(LayerType::Raster);
    missingGrid->tileGrid.reset();
    REQUIRE(aether::selectionContentEditTargets(
        { nullptr, background.get(), adjustment.get(), group.get(), missingGrid.get() })
            .isEmpty());

    // Masks are ordinary grids: filling a mask must not require raster content.
    auto smart = makeLayer(LayerType::Smart);
    smart->ensureMask();
    smart->maskEditActive = true;
    REQUIRE(aether::selectionContentEditTargets({ smart.get() }, /*editMasks=*/true)
        == QList<QUuid> { smart->id });
    REQUIRE(smart->isSmart());
    REQUIRE(smart->maskIsEditTarget());
}
