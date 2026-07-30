// SPDX-License-Identifier: MPL-2.0

#include <catch2/catch_test_macros.hpp>

#include "shell/docking/layout/DockSplitNode.h"

#include <memory>

namespace {

using namespace ruwa::ui::docking;

class TestLayoutNode final : public DockLayoutNode {
public:
    explicit TestLayoutNode(const NodeSizeConstraints& constraints)
        : m_constraints(constraints)
    {
    }

    Type type() const override { return Type::Leaf; }

    void setBounds(const QRect& bounds) override { m_bounds = bounds; }

    NodeSizeConstraints sizeConstraints() const override { return m_constraints; }

    QString debugString() const override { return QStringLiteral("TestLayoutNode"); }

private:
    NodeSizeConstraints m_constraints;
};

std::unique_ptr<TestLayoutNode> makeNode(SplitDirection direction, int minimum)
{
    NodeSizeConstraints constraints;
    if (direction == SplitDirection::Horizontal) {
        constraints.minWidth = minimum;
    } else {
        constraints.minHeight = minimum;
    }
    return std::make_unique<TestLayoutNode>(constraints);
}

void populateSplit(DockSplitNode& split, SplitDirection direction, const QList<int>& sizes)
{
    for (int i = 0; i < sizes.size(); ++i) {
        split.addChild(makeNode(direction, 100));
    }
    split.setSizes(sizes);
}

} // namespace

TEST_CASE("Push-through handle drag restores displaced panels when reversed")
{
    SECTION("positive horizontal drag")
    {
        DockSplitNode split(SplitDirection::Horizontal);
        populateSplit(split, SplitDirection::Horizontal, QList<int> { 200, 120, 180 });

        split.beginHandleDrag(0);
        split.handleDrag(0, 50);
        REQUIRE(split.sizes() == (QList<int> { 250, 100, 150 }));

        split.handleDrag(0, -10);
        REQUIRE(split.sizes() == (QList<int> { 240, 100, 160 }));

        split.handleDrag(0, -40);
        REQUIRE(split.sizes() == (QList<int> { 200, 120, 180 }));
        split.endHandleDrag();
    }

    SECTION("negative vertical drag")
    {
        DockSplitNode split(SplitDirection::Vertical);
        populateSplit(split, SplitDirection::Vertical, QList<int> { 180, 120, 200 });

        split.beginHandleDrag(1);
        split.handleDrag(1, -50);
        REQUIRE(split.sizes() == (QList<int> { 150, 100, 250 }));

        split.handleDrag(1, 10);
        REQUIRE(split.sizes() == (QList<int> { 160, 100, 240 }));

        split.handleDrag(1, 40);
        REQUIRE(split.sizes() == (QList<int> { 180, 120, 200 }));
        split.endHandleDrag();
    }
}
