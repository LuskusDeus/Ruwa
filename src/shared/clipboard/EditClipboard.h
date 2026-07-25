// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   E D I T   C L I P B O A R D
// ==========================================================================

#ifndef RUWA_SHARED_CLIPBOARD_EDITCLIPBOARD_H
#define RUWA_SHARED_CLIPBOARD_EDITCLIPBOARD_H

#include <QElapsedTimer>
#include <QObject>
#include <QRect>

#include <cstdint>
#include <memory>

namespace aether {
class TileGrid;
}

namespace ruwa::shared::clipboard {

/**
 * @brief Process-wide clipboard for the payloads Qt's clipboard cannot carry.
 *
 * Ctrl+C means different things depending on what is focused: a layer, a layer
 * mask, or the pixels inside a canvas selection. The system clipboard can only
 * hold the flattened image, so full-fidelity payloads (tile grids in the
 * document's own pixel format) live here and paste prefers them.
 *
 * Only ONE payload is live at a time — kind() says which — so the last Ctrl+C
 * wins no matter which panel it happened in. Copied LAYERS are the exception:
 * their snapshots stay in the WorkspaceTab that owns them (they are per-document
 * and tied to that tab's undo stack), so this class only records the marker that
 * a layer copy was the most recent one.
 *
 * A copy made in another application invalidates the mask/pixel payloads, so a
 * later paste falls through to the system clipboard instead of resurrecting a
 * stale in-app copy.
 */
class EditClipboard : public QObject {
    Q_OBJECT

public:
    enum class Kind {
        None,
        Layers, ///< Snapshots live in WorkspaceTab; this is the arbitration marker only.
        Mask, ///< A layer mask (alpha coverage grid).
        Pixels ///< Pixels lifted out of a canvas selection.
    };

    struct MaskPayload {
        std::shared_ptr<const aether::TileGrid> grid;
        bool enabled = true;
        bool linked = true;
    };

    struct PixelsPayload {
        /// Tiles in document coordinates — paste puts them back where they were cut from.
        std::shared_ptr<const aether::TileGrid> grid;
        /// Document-space bounding box of the non-transparent copied pixels.
        QRect bounds;
    };

    static EditClipboard& instance();

    Kind kind() const { return m_kind; }
    void clear();

    /// Record that layers were copied/cut (payload stays with the WorkspaceTab).
    void markLayersCopied();

    void setMask(std::shared_ptr<const aether::TileGrid> grid, bool enabled, bool linked);
    /// Null unless kind() == Kind::Mask.
    const MaskPayload* mask() const;

    void setPixels(std::shared_ptr<const aether::TileGrid> grid, const QRect& bounds);
    /// Null unless kind() == Kind::Pixels.
    const PixelsPayload* pixels() const;

    /// Call right before publishing our own image to the system clipboard: the
    /// dataChanged() it triggers must not be mistaken for a foreign copy.
    void noteOwnSystemClipboardWrite();

private:
    EditClipboard();

    void onSystemClipboardChanged();

    Kind m_kind = Kind::None;
    MaskPayload m_mask;
    PixelsPayload m_pixels;

    // Guard window for the clipboard change our own publish causes.
    QElapsedTimer m_ownWriteTimer;
    bool m_ownWritePending = false;
};

} // namespace ruwa::shared::clipboard

#endif // RUWA_SHARED_CLIPBOARD_EDITCLIPBOARD_H
