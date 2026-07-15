// SPDX-License-Identifier: MPL-2.0

// Docking.h
#ifndef RUWA_UI_DOCKING_H
#define RUWA_UI_DOCKING_H

/**
 * @file Docking.h
 * @brief Main include header for the Ruwa Docking System
 *
 * This custom docking system provides:
 * - Flexible panel docking with unlimited nesting
 * - Floating panels inside the main window
 * - Drag & drop with visual compass indicator
 * - Layout presets and state serialization
 * - Full theme integration
 *
 * Basic usage:
 * @code
 * #include "shell/docking/Docking.h"
 *
 * using namespace ruwa::ui::docking;
 *
 * // Create container and manager
 * auto* container = new DockContainerWidget(parent);
 * auto* manager = new DockManager(this);
 * manager->setContainer(container);
 *
 * // Create and add panels
 * auto* toolsPanel = new ToolsPanel();
 * auto* layersPanel = new LayersPanel();
 *
 * manager->registerPanel(toolsPanel);
 * manager->registerPanel(layersPanel);
 *
 * manager->addPanel(toolsPanel, DockPosition::Left);
 * manager->addPanel(layersPanel, DockPosition::Right);
 *
 * // Apply preset layout
 * DockStateSerializer serializer(manager);
 * serializer.applyPreset(DockLayoutPreset::defaultWorkspace());
 * @endcode
 */

// Types and enums
#include "DockTypes.h"

// Core components
#include "shell/docking/core/DockManager.h"
#include "shell/docking/core/DockContainerWidget.h"
#include "shell/docking/core/DockFloatingContainer.h"

// Widgets
#include "shell/docking/widgets/DockPanel.h"
#include "shell/docking/widgets/DockPanelTitleBar.h"

// Overlay
#include "shell/docking/overlay/DockOverlay.h"
#include "shell/docking/overlay/DockCompassWidget.h"
#include "shell/docking/overlay/DropZoneIndicator.h"
#include "shell/docking/overlay/DockDimOverlay.h"

// State management
#include "shell/docking/state/DockLayoutPreset.h"
#include "shell/docking/state/DockStateSerializer.h"

#endif // RUWA_UI_DOCKING_H
