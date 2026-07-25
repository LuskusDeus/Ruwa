// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   E D I T   C L I P B O A R D
// ==========================================================================

#include "shared/clipboard/EditClipboard.h"
#include "shared/tiles/TileGrid.h"

#include <QClipboard>
#include <QGuiApplication>

namespace ruwa::shared::clipboard {

namespace {
// A clipboard change arriving this soon after our own publish is ours.
constexpr qint64 kOwnWriteGraceMs = 1500;
} // namespace

EditClipboard::EditClipboard()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        connect(
            clipboard, &QClipboard::dataChanged, this, &EditClipboard::onSystemClipboardChanged);
    }
}

EditClipboard& EditClipboard::instance()
{
    static EditClipboard clipboard;
    return clipboard;
}

void EditClipboard::clear()
{
    m_kind = Kind::None;
    m_mask = {};
    m_pixels = {};
}

void EditClipboard::markLayersCopied()
{
    clear();
    m_kind = Kind::Layers;
}

void EditClipboard::setMask(std::shared_ptr<const aether::TileGrid> grid, bool enabled, bool linked)
{
    clear();
    if (!grid) {
        return;
    }
    m_mask.grid = std::move(grid);
    m_mask.enabled = enabled;
    m_mask.linked = linked;
    m_kind = Kind::Mask;
}

const EditClipboard::MaskPayload* EditClipboard::mask() const
{
    return m_kind == Kind::Mask && m_mask.grid ? &m_mask : nullptr;
}

void EditClipboard::setPixels(std::shared_ptr<const aether::TileGrid> grid, const QRect& bounds)
{
    clear();
    if (!grid || grid->empty()) {
        return;
    }
    m_pixels.grid = std::move(grid);
    m_pixels.bounds = bounds;
    m_kind = Kind::Pixels;
}

const EditClipboard::PixelsPayload* EditClipboard::pixels() const
{
    return m_kind == Kind::Pixels && m_pixels.grid ? &m_pixels : nullptr;
}

void EditClipboard::noteOwnSystemClipboardWrite()
{
    m_ownWritePending = true;
    m_ownWriteTimer.start();
}

void EditClipboard::onSystemClipboardChanged()
{
    // Publishing the copied pixels as an image makes the system clipboard change
    // too — that echo must not wipe the payload we just stored. Platform paths
    // can emit more than one change per write, so the guard is a short time
    // window rather than a single-shot flag.
    if (m_ownWritePending && m_ownWriteTimer.isValid()
        && m_ownWriteTimer.elapsed() <= kOwnWriteGraceMs) {
        return;
    }
    m_ownWritePending = false;

    // A copy made elsewhere is newer than ours: drop the in-app payloads so paste
    // falls through to the system clipboard. Layer copies keep their historical
    // behavior (the armed WorkspaceTab snapshot outlives foreign copies).
    if (m_kind == Kind::Mask || m_kind == Kind::Pixels) {
        clear();
    }
}

} // namespace ruwa::shared::clipboard
