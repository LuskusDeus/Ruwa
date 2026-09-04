// SPDX-License-Identifier: MPL-2.0

#include "shared/undo/UndoManager.h"

#include <QCoreApplication>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

using aether::UndoManager;

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

namespace {

class TrackedCommand final : public aether::IUndoCommand {
public:
    explicit TrackedCommand(bool& destroyed)
        : m_destroyed(destroyed)
    {
    }
    ~TrackedCommand() override { m_destroyed = true; }
    void undo() override { }
    void redo() override { }
    QString text() const override { return QStringLiteral("Stroke"); }
    qint64 memorySize() const override { return sizeof(*this); }

private:
    bool& m_destroyed;
};

} // namespace

TEST_CASE("Undo manager destruction releases commands without notifying views", "[undo][lifecycle]")
{
    QObject receiver;
    bool commandDestroyed = false;
    auto manager = std::make_unique<UndoManager>();
    SECTION("committed history")
    {
        manager->push(std::make_unique<TrackedCommand>(commandDestroyed));
    }
    SECTION("unfinished transaction")
    {
        manager->beginTransaction(QStringLiteral("Stroke"));
        manager->push(std::make_unique<TrackedCommand>(commandDestroyed));
    }

    int notifications = 0;
    const auto onChanged = [&notifications]() { ++notifications; };
    QObject::connect(manager.get(), &UndoManager::canUndoChanged, &receiver, onChanged);
    QObject::connect(manager.get(), &UndoManager::canRedoChanged, &receiver, onChanged);
    QObject::connect(manager.get(), &UndoManager::undoTextChanged, &receiver, onChanged);
    QObject::connect(manager.get(), &UndoManager::redoTextChanged, &receiver, onChanged);
    QObject::connect(manager.get(), &UndoManager::indexChanged, &receiver, onChanged);
    QObject::connect(manager.get(), &UndoManager::cleanChanged, &receiver, onChanged);
    QObject::connect(manager.get(), &UndoManager::memoryUsageChanged, &receiver, onChanged);
    QObject::connect(manager.get(), &UndoManager::commandApplied, &receiver, onChanged);
    bool destroyedNotified = false;
    QObject::connect(manager.get(), &QObject::destroyed, &receiver,
        [&destroyedNotified]() { destroyedNotified = true; });

    manager.reset();

    REQUIRE(commandDestroyed);
    REQUIRE(notifications == 0);
    REQUIRE(destroyedNotified);
}

TEST_CASE("Explicit history clear still updates views", "[undo][lifecycle]")
{
    QObject receiver;
    bool commandDestroyed = false;
    UndoManager manager;
    manager.push(std::make_unique<TrackedCommand>(commandDestroyed));
    REQUIRE(manager.canUndo());
    REQUIRE_FALSE(manager.isClean());

    int notifications = 0;
    int reportedIndex = -1;
    QObject::connect(&manager, &UndoManager::indexChanged, &receiver, [&](int index) {
        ++notifications;
        reportedIndex = index;
    });

    manager.clear();

    REQUIRE(commandDestroyed);
    REQUIRE(notifications == 1);
    REQUIRE(reportedIndex == 0);
    REQUIRE(manager.count() == 0);
    REQUIRE_FALSE(manager.canUndo());
    REQUIRE(manager.isClean());
}
