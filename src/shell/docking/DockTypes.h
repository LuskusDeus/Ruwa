// SPDX-License-Identifier: MPL-2.0

// DockTypes.h
#ifndef RUWA_UI_DOCKING_DOCKTYPES_H
#define RUWA_UI_DOCKING_DOCKTYPES_H

#include <QFlags>
#include <QString>
#include <QUuid>

namespace ruwa::ui::docking {

// Forward declarations
class DockManager;
class DockContainerWidget;
class DockPanel;
class DockPanelTitleBar;
class DockFloatingContainer;
class DockOverlay;
class DockCompassWidget;
class DropZoneIndicator;

/**
 * @brief Dock position within container or relative to other panel
 */
enum class DockPosition {
    None = 0,
    Left,
    Right,
    Top,
    Bottom,
    Center // For tabbed/stacked panels (future use)
};

/**
 * @brief Drop zone areas for drag & drop
 */
enum class DropZone {
    None = 0,

    // Container edges (outer)
    OuterLeft = 0x0001,
    OuterRight = 0x0002,
    OuterTop = 0x0004,
    OuterBottom = 0x0008,

    // Panel-relative (inner)
    InnerLeft = 0x0010,
    InnerRight = 0x0020,
    InnerTop = 0x0040,
    InnerBottom = 0x0080,

    // Center: drop into the hovered panel, forming (or joining) a tab group
    InnerCenter = 0x0100,

    // Group header strip: explicit tab insertion at a known index
    GroupHeader = 0x0200,

    // Masks
    OuterMask = OuterLeft | OuterRight | OuterTop | OuterBottom,
    InnerMask = InnerLeft | InnerRight | InnerTop | InnerBottom | InnerCenter | GroupHeader,
    GroupMask = InnerCenter | GroupHeader,
    AllMask = OuterMask | InnerMask
};

Q_DECLARE_FLAGS(DropZones, DropZone)
Q_DECLARE_OPERATORS_FOR_FLAGS(DropZones)

/**
 * @brief Panel state
 */
enum class PanelState {
    Docked, // Docked in container
    Floating, // Floating inside main window
    Hidden // Hidden (closed but can be restored)
};

/**
 * @brief Resize handle position for floating containers
 */
enum class ResizeEdge {
    None = 0,
    Left = 0x01,
    Right = 0x02,
    Top = 0x04,
    Bottom = 0x08,
    TopLeft = Top | Left,
    TopRight = Top | Right,
    BottomLeft = Bottom | Left,
    BottomRight = Bottom | Right
};

Q_DECLARE_FLAGS(ResizeEdges, ResizeEdge)
Q_DECLARE_OPERATORS_FOR_FLAGS(ResizeEdges)

/**
 * @brief Panel features/capabilities
 */
enum class PanelFeature {
    None = 0,
    Closable = 0x01,
    Movable = 0x02,
    Floatable = 0x04,
    Resizable = 0x08,
    Dockable = 0x10,
    /// May share a layout cell with other panels as a tab group.
    /// NOTE: added after features were already persisted as a raw int
    /// (legacy Default == 0x1F). DockStateSerializer upgrades legacy values,
    /// see kLegacyPanelFeatureDefault below.
    Groupable = 0x20,

    Default = Closable | Movable | Floatable | Resizable | Dockable | Groupable,
    All = Default
};

Q_DECLARE_FLAGS(PanelFeatures, PanelFeature)
Q_DECLARE_OPERATORS_FOR_FLAGS(PanelFeatures)

/// Value of PanelFeature::Default before Groupable existed. Serialized states
/// written by older builds store this; restoring one must not silently make
/// every panel un-groupable.
constexpr int kLegacyPanelFeatureDefault = 0x1F;

/// Upgrade a persisted feature int: anything saved as the old "all features"
/// value gains Groupable, everything else is taken literally.
inline PanelFeatures panelFeaturesFromPersistedInt(int value)
{
    if (value == kLegacyPanelFeatureDefault) {
        return PanelFeature::Default;
    }
    return PanelFeatures(QFlag(value));
}

/**
 * @brief Size constraints for panels
 */
struct PanelSizeHints {
    int minWidth = 100;
    int minHeight = 100;
    int maxWidth = 16777215; // QWIDGETSIZE_MAX
    int maxHeight = 16777215;
    int prefWidth = 250;
    int prefHeight = 300;

    // User-set sizes for HORIZONTAL positioning (Left/Right dock)
    // Width is the significant dimension when docked horizontally
    // -1 means "not set, use default behavior"
    int userHorizontalDockedWidth = -1;

    // User-set sizes for VERTICAL positioning (Top/Bottom dock)
    // Height is the significant dimension when docked vertically
    int userVerticalDockedHeight = -1;

    // User-set sizes for floating state
    int userFloatingWidth = -1;
    int userFloatingHeight = -1;

    // Legacy compatibility - maps to horizontal width
    int userDockedWidth() const { return userHorizontalDockedWidth; }
    int userDockedHeight() const { return userVerticalDockedHeight; }

    /**
     * @brief Get effective width for horizontal docking (Left/Right)
     * Returns userHorizontalDockedWidth if set, otherwise prefWidth
     */
    int effectiveHorizontalDockedWidth() const
    {
        return (userHorizontalDockedWidth > 0) ? userHorizontalDockedWidth : prefWidth;
    }

    /**
     * @brief Get effective height for vertical docking (Top/Bottom)
     * Returns userVerticalDockedHeight if set, otherwise prefHeight
     */
    int effectiveVerticalDockedHeight() const
    {
        return (userVerticalDockedHeight > 0) ? userVerticalDockedHeight : prefHeight;
    }

    /**
     * @brief Get effective preferred width for docked state (legacy, uses horizontal)
     */
    int effectiveDockedWidth() const { return effectiveHorizontalDockedWidth(); }

    /**
     * @brief Get effective preferred height for docked state (legacy, uses vertical)
     */
    int effectiveDockedHeight() const { return effectiveVerticalDockedHeight(); }

    /**
     * @brief Get effective preferred width for floating state
     */
    int effectiveFloatingWidth() const
    {
        return (userFloatingWidth > 0) ? userFloatingWidth : prefWidth;
    }

    /**
     * @brief Get effective preferred height for floating state
     */
    int effectiveFloatingHeight() const
    {
        return (userFloatingHeight > 0) ? userFloatingHeight : prefHeight;
    }

    /**
     * @brief Check if user has set custom horizontal docked size
     */
    bool hasUserHorizontalDockedSize() const { return userHorizontalDockedWidth > 0; }

    /**
     * @brief Check if user has set custom vertical docked size
     */
    bool hasUserVerticalDockedSize() const { return userVerticalDockedHeight > 0; }

    /**
     * @brief Check if user has set custom docked size (any direction)
     */
    bool hasUserDockedSize() const
    {
        return hasUserHorizontalDockedSize() || hasUserVerticalDockedSize();
    }

    /**
     * @brief Check if user has set custom floating size
     */
    bool hasUserFloatingSize() const { return userFloatingWidth > 0 || userFloatingHeight > 0; }
};

// ============================================================================
// Tab groups
// ============================================================================

/**
 * @brief Which side of a reference tab a dropped panel is inserted on
 *
 * Drives the direction the incumbent panel slides during the group insertion
 * animation: inserting After pushes it left, Before pushes it right.
 */
enum class GroupInsertSide { Before, After };

/**
 * @brief Default height of the group header strip, in unscaled px
 *
 * Sits directly above the selected member's own title bar (19px), so a grouped
 * panel is this much shorter than a solo one.
 */
constexpr int kDefaultGroupHeaderHeight = 22;

/**
 * @brief Resolved drop description handed from the overlay to the container
 *
 * Replaces the old (zone, targetPanel) pair: a center/header drop additionally
 * needs to know WHERE inside the group the panel lands, which the overlay is
 * the only place that knows (it hit-tests the header tabs).
 */
struct DockDropTarget {
    DropZone zone = DropZone::None;
    /// Panel under the cursor. For a group, the member whose tab was hit
    /// (GroupHeader) or the group's current member (InnerCenter).
    DockPanel* panel = nullptr;
    /// Insertion index within the target group, or -1 for "next to panel".
    int groupInsertIndex = -1;
    GroupInsertSide side = GroupInsertSide::After;

    bool isValid() const { return zone != DropZone::None; }
    bool isGroupDrop() const
    {
        return (static_cast<int>(zone) & static_cast<int>(DropZone::GroupMask)) != 0;
    }
};

/**
 * @brief Unique identifier for dock panels
 */
using DockPanelId = QUuid;

/**
 * @brief Generate new panel ID
 */
inline DockPanelId generatePanelId()
{
    return QUuid::createUuid();
}

/**
 * @brief Convert DockPosition to Qt orientation
 */
inline Qt::Orientation positionToOrientation(DockPosition pos)
{
    switch (pos) {
    case DockPosition::Left:
    case DockPosition::Right:
        return Qt::Horizontal;
    case DockPosition::Top:
    case DockPosition::Bottom:
        return Qt::Vertical;
    default:
        return Qt::Horizontal;
    }
}

/**
 * @brief Check if drop zone is outer (container edge)
 */
inline bool isOuterZone(DropZone zone)
{
    return (static_cast<int>(zone) & static_cast<int>(DropZone::OuterMask)) != 0;
}

/**
 * @brief Check if drop zone is inner (panel-relative)
 */
inline bool isInnerZone(DropZone zone)
{
    return (static_cast<int>(zone) & static_cast<int>(DropZone::InnerMask)) != 0;
}

/**
 * @brief Check if drop zone forms or joins a tab group
 *
 * A subset of isInnerZone(): the group zones are inner zones too, so any
 * dispatch on the zone must test this one FIRST.
 */
inline bool isGroupZone(DropZone zone)
{
    return (static_cast<int>(zone) & static_cast<int>(DropZone::GroupMask)) != 0;
}

/**
 * @brief Convert drop zone to dock position
 */
inline DockPosition zoneToDockPosition(DropZone zone)
{
    switch (zone) {
    case DropZone::OuterLeft:
    case DropZone::InnerLeft:
        return DockPosition::Left;
    case DropZone::OuterRight:
    case DropZone::InnerRight:
        return DockPosition::Right;
    case DropZone::OuterTop:
    case DropZone::InnerTop:
        return DockPosition::Top;
    case DropZone::OuterBottom:
    case DropZone::InnerBottom:
        return DockPosition::Bottom;
    case DropZone::InnerCenter:
    case DropZone::GroupHeader:
        return DockPosition::Center;
    default:
        return DockPosition::None;
    }
}

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_DOCKTYPES_H
