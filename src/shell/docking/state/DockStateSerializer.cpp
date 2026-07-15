// SPDX-License-Identifier: MPL-2.0

// DockStateSerializer.cpp
#include "DockStateSerializer.h"
#include "shell/docking/core/DockManager.h"
#include "shell/docking/core/DockContainerWidget.h"
#include "shell/docking/core/DockFloatingContainer.h"
#include "shell/docking/layout/DockLayoutRoot.h"
#include "shell/docking/widgets/DockPanel.h"

#include <QJsonDocument>
#include <QSet>

namespace ruwa::ui::docking {

namespace {

DockPanel* resolvePanel(DockManager* manager, const QString& panelId, const QString& panelTitle)
{
    if (!manager) {
        return nullptr;
    }

    if (!panelId.isEmpty()) {
        const QUuid id = QUuid::fromString(panelId);
        if (!id.isNull()) {
            if (DockPanel* byId = manager->panel(id)) {
                return byId;
            }
        }

        if (DockPanel* byKey = manager->panelByPersistentKey(panelId)) {
            return byKey;
        }
    }

    if (!panelTitle.isEmpty()) {
        if (DockPanel* byTitle = manager->panelByTitle(panelTitle)) {
            return byTitle;
        }
    }

    return nullptr;
}

} // namespace

DockStateSerializer::DockStateSerializer(DockManager* manager)
    : m_manager(manager)
{
}

// ============================================================================
// Save/Load State
// ============================================================================

QJsonObject DockStateSerializer::saveState() const
{
    QJsonObject state;
    state["version"] = StateVersion;

    if (!m_manager || !m_manager->container()) {
        return state;
    }

    auto* container = m_manager->container();

    // Save layout tree structure
    if (container->layoutRoot()) {
        state["layoutTree"] = container->layoutRoot()->toJson();
    }

    // Save floating containers
    state["floating"] = serializeFloatingContainers();

    // Save panel visibility states
    QJsonObject panelStates;
    for (auto* panel : m_manager->panels()) {
        QJsonObject panelObj;
        // Use logical panel state instead of QWidget visibility.
        // QWidget::isVisible() depends on parent/tab visibility and can be false
        // even for a valid docked panel.
        panelObj["visible"] = (panel->state() != PanelState::Hidden);
        panelObj["state"] = static_cast<int>(panel->state());
        panelObj["features"] = static_cast<int>(panel->features());
        const PanelSizeHints hints = panel->sizeHints();
        panelObj["userHorizontalDockedWidth"] = hints.userHorizontalDockedWidth;
        panelObj["userVerticalDockedHeight"] = hints.userVerticalDockedHeight;
        panelObj["userFloatingWidth"] = hints.userFloatingWidth;
        panelObj["userFloatingHeight"] = hints.userFloatingHeight;
        const QJsonObject customState = panel->savePanelState();
        if (!customState.isEmpty()) {
            panelObj["customState"] = customState;
        }
        panelObj["title"] = panel->title();
        panelStates[panel->persistentKey()] = panelObj;
    }
    state["panelStates"] = panelStates;

    return state;
}

bool DockStateSerializer::restoreState(const QJsonObject& state)
{
    if (state.isEmpty() || !m_manager || !m_manager->container()) {
        return false;
    }

    int version = state["version"].toInt();
    if (version > StateVersion) {
        return false; // Incompatible future version
    }

    auto* container = m_manager->container();

    // Normalize runtime state before applying layout:
    // floating panels are temporarily docked, then floating layout is restored.
    for (auto* panel : m_manager->panels()) {
        if (panel && panel->isFloating()) {
            m_manager->dockPanel(panel, DockPosition::Right);
        }
    }

    // Restore user preferred sizes first so subsequent layout operations
    // use user-defined values instead of hardcoded defaults.
    const QJsonObject panelStates = state["panelStates"].toObject();
    for (auto it = panelStates.begin(); it != panelStates.end(); ++it) {
        const QJsonObject panelObj = it.value().toObject();
        const QString storedTitle
            = panelObj.contains("title") ? panelObj["title"].toString() : it.key();
        DockPanel* panel = resolvePanel(m_manager, it.key(), storedTitle);
        if (!panel) {
            continue;
        }
        if (panelObj.contains("features")) {
            panel->setFeatures(static_cast<PanelFeatures>(
                panelObj["features"].toInt(static_cast<int>(PanelFeature::Default))));
        }
        panel->setUserHorizontalDockedWidth(panelObj["userHorizontalDockedWidth"].toInt(-1));
        panel->setUserVerticalDockedHeight(panelObj["userVerticalDockedHeight"].toInt(-1));
        panel->setUserFloatingSize(
            panelObj["userFloatingWidth"].toInt(-1), panelObj["userFloatingHeight"].toInt(-1));
        panel->restorePanelState(panelObj["customState"].toObject());
    }

    // Restore tree-based layout
    if (state.contains("layoutTree") && container->layoutRoot()) {
        const QJsonObject layoutTree = state["layoutTree"].toObject();
        const bool restored = container->layoutRoot()->fromJson(
            layoutTree, [this](const QString& panelId, const QString& panelTitle) -> DockPanel* {
                return resolvePanel(m_manager, panelId, panelTitle);
            });
        if (!restored) {
            return false;
        }
    }

    if (container->layoutRoot()) {
        for (DockPanel* panel : container->layoutRoot()->allPanels()) {
            if (!panel) {
                continue;
            }

            container->restoreDockedPanel(panel);
        }
    }

    // Restore floating containers
    if (state.contains("floating")) {
        deserializeFloatingContainers(state["floating"].toArray());
    }

    container->repairDockLayout();

    // Hide panels that were saved as not visible (layout tree only contains docked panels,
    // floating panels are in floatingContainers - both are visible and must not be closed)
    if (container->layoutRoot()) {
        QSet<DockPanel*> visibleSet;
        for (DockPanel* p : container->layoutRoot()->allPanels()) {
            visibleSet.insert(p);
        }
        for (DockPanel* p : container->floatingPanels()) {
            visibleSet.insert(p);
        }
        for (auto* panel : m_manager->panels()) {
            if (panel && !visibleSet.contains(panel)) {
                m_manager->closePanel(panel);
            }
        }
    }

    return true;
}

QByteArray DockStateSerializer::saveToByteArray() const
{
    QJsonDocument doc(saveState());
    return doc.toJson(QJsonDocument::Compact);
}

bool DockStateSerializer::restoreFromByteArray(const QByteArray& data)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(data, &error);

    if (error.error != QJsonParseError::NoError) {
        return false;
    }

    return restoreState(doc.object());
}

// ============================================================================
// Presets
// ============================================================================

bool DockStateSerializer::applyPreset(const DockLayoutPreset& preset)
{
    if (!m_manager || !m_manager->container()) {
        return false;
    }

    auto* container = m_manager->container();
    const bool hadAnimations = container->animationsEnabled();
    container->setAnimationsEnabled(false);

    const bool useTree = preset.usesLayoutTree() && container->layoutRoot();
    const bool usePlacements = !preset.placements.isEmpty();

    if (!useTree && !usePlacements) {
        container->setAnimationsEnabled(hadAnimations);
        return false;
    }

    m_manager->resetLayout();

    bool ok = false;

    if (useTree) {
        auto panelResolver
            = [this](const QString& panelId, const QString& panelTitle) -> DockPanel* {
            return resolvePanel(m_manager, panelId, panelTitle);
        };

        ok = container->layoutRoot()->fromJson(preset.layoutTree, panelResolver);
        if (ok) {
            for (DockPanel* panel : container->layoutRoot()->allPanels()) {
                if (panel) {
                    container->restoreDockedPanel(panel);
                }
            }
            if (!preset.floating.isEmpty()) {
                deserializeFloatingContainers(preset.floating);
            }
            if (container->layoutRoot()) {
                QSet<DockPanel*> visibleSet;
                for (DockPanel* p : container->layoutRoot()->allPanels()) {
                    visibleSet.insert(p);
                }
                for (DockPanel* p : container->floatingPanels()) {
                    visibleSet.insert(p);
                }
                for (auto* panel : m_manager->panels()) {
                    if (panel && !visibleSet.contains(panel)) {
                        m_manager->closePanel(panel);
                    }
                }
            }
            container->repairDockLayout();
        }
    }

    if (!ok && usePlacements) {
        m_manager->resetLayout();
        for (const auto& placement : preset.placements) {
            DockPanel* panel = findPanelForPlacement(placement);
            if (!panel) {
                continue;
            }

            if (placement.relativeTo.isEmpty()) {
                m_manager->addPanel(panel, placement.position);
            } else {
                DockPanel* relativePanel
                    = resolvePanel(m_manager, placement.relativeTo, placement.relativeTo);
                if (relativePanel) {
                    m_manager->addPanelRelativeTo(panel, relativePanel, placement.position);
                } else {
                    m_manager->addPanel(panel, placement.position);
                }
            }
        }
        ok = true;
    }

    container->setAnimationsEnabled(hadAnimations);
    return ok;
}

DockLayoutPreset DockStateSerializer::createPresetFromCurrent(const QString& name) const
{
    DockLayoutPreset preset;
    preset.id = QUuid::createUuid();
    preset.name = name;
    preset.isBuiltIn = false;

    if (!m_manager || !m_manager->container()) {
        return preset;
    }

    auto* container = m_manager->container();
    if (container->layoutRoot()) {
        preset.layoutTree = container->layoutRoot()->toJson();
        preset.floating = serializeFloatingContainers();
    }

    return preset;
}

// ============================================================================
// Serialization Helpers
// ============================================================================

QJsonObject DockStateSerializer::serializePanel(DockPanel* panel) const
{
    QJsonObject obj;

    if (!panel) {
        return obj;
    }

    obj["id"] = panel->id().toString();
    obj["panelKey"] = panel->persistentKey();
    obj["title"] = panel->title();
    obj["features"] = static_cast<int>(panel->features());

    // Size hints
    PanelSizeHints hints = panel->sizeHints();
    QJsonObject hintsObj;
    hintsObj["minWidth"] = hints.minWidth;
    hintsObj["minHeight"] = hints.minHeight;
    hintsObj["prefWidth"] = hints.prefWidth;
    hintsObj["prefHeight"] = hints.prefHeight;
    obj["sizeHints"] = hintsObj;

    return obj;
}

QJsonArray DockStateSerializer::serializeFloatingContainers() const
{
    QJsonArray arr;

    if (!m_manager || !m_manager->container()) {
        return arr;
    }

    auto* container = m_manager->container();
    const QSize containerSize = container->size();
    const bool canNormalize = containerSize.width() > 0 && containerSize.height() > 0;

    for (auto* floatingContainer : container->floatingContainers()) {
        QJsonObject obj;

        if (auto* panel = floatingContainer->panel()) {
            obj["panelKey"] = panel->persistentKey();
            obj["panelTitle"] = panel->title();
        }

        QRect geom = floatingContainer->geometry();
        QJsonObject geomObj;
        geomObj["x"] = geom.x();
        geomObj["y"] = geom.y();
        geomObj["width"] = geom.width();
        geomObj["height"] = geom.height();
        obj["geometry"] = geomObj;

        if (canNormalize) {
            obj["posNormX"] = static_cast<double>(geom.x()) / containerSize.width();
            obj["posNormY"] = static_cast<double>(geom.y()) / containerSize.height();
        }

        arr.append(obj);
    }

    return arr;
}

// ============================================================================
// Deserialization Helpers
// ============================================================================

bool DockStateSerializer::deserializeFloatingContainers(const QJsonArray& arr)
{
    if (!m_manager || !m_manager->container()) {
        return false;
    }

    auto* container = m_manager->container();
    const QSize containerSize = container->size();

    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();

        const QString panelKey = obj["panelKey"].toString();
        const QString panelTitle = obj["panelTitle"].toString();
        DockPanel* panel = resolvePanel(m_manager, panelKey, panelTitle);

        if (!panel) {
            continue;
        }

        QJsonObject geomObj = obj["geometry"].toObject();
        QSize size(geomObj["width"].toInt(), geomObj["height"].toInt());

        QPoint pos;
        if (containerSize.width() > 0 && containerSize.height() > 0 && obj.contains("posNormX")
            && obj.contains("posNormY")) {
            const qreal nx = obj["posNormX"].toDouble(0);
            const qreal ny = obj["posNormY"].toDouble(0);
            pos = QPoint(qRound(nx * containerSize.width()), qRound(ny * containerSize.height()));
        } else {
            pos = QPoint(geomObj["x"].toInt(), geomObj["y"].toInt());
        }

        m_manager->floatPanel(panel, container->mapToGlobal(pos), true);

        if (auto* floatingContainer = panel->floatingContainer()) {
            if (size.width() > 0 && size.height() > 0) {
                floatingContainer->resizeTo(size);
            } else {
                floatingContainer->resizeTo(DockFloatingContainer::outerSizeForPanel(panel));
            }
        }
    }

    return true;
}

// ============================================================================
// Preset Helpers
// ============================================================================

DockPanel* DockStateSerializer::findPanelForPlacement(const PanelPlacement& placement) const
{
    return resolvePanel(m_manager, placement.panelId, placement.panelId);
}

} // namespace ruwa::ui::docking
