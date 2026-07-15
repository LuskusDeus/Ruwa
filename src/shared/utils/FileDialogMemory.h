// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A R E D   |   F I L E   D I A L O G   M E M O R Y
// ==========================================================================
//
// Thin wrappers around QFileDialog that remember the last-used directory on a
// per-category basis. Each logical "source" of file dialogs (projects, brushes,
// themes, canvas exports, ...) passes its own category string, so opening a
// brush export dialog does not reposition the project-open dialog and vice
// versa. The remembered directory is persisted in QSettings and only used as
// the initial location when it still exists on disk.

#ifndef RUWA_SHARED_FILE_DIALOG_MEMORY_H
#define RUWA_SHARED_FILE_DIALOG_MEMORY_H

#include <QString>
#include <QStringList>

class QWidget;

namespace ruwa::shared::filedialog {

// Well-known dialog categories. Plain strings are also accepted; these exist to
// avoid typos and document the set of sources in one place.
namespace category {
inline constexpr auto kProject = "project";
inline constexpr auto kBrush = "brush";
inline constexpr auto kBrushDab = "brush.dab";
inline constexpr auto kTheme = "theme";
inline constexpr auto kShortcuts = "shortcuts";
inline constexpr auto kLayout = "layout";
inline constexpr auto kCanvasExport = "canvas.export";
inline constexpr auto kImageImport = "image.import";
inline constexpr auto kImage = "image";
} // namespace category

// Returns the remembered directory for the category, or an empty string when
// nothing is stored or the stored directory no longer exists.
QString lastDirectory(const QString& category);

// Records the directory containing `chosenPath` as the last-used location for
// the category. No-op for empty input.
void rememberFromPath(const QString& category, const QString& chosenPath);

// QFileDialog wrappers. `suggestedName` may be a bare file name (combined with
// the remembered directory) or a full path (respected as-is). On success the
// chosen directory is remembered for the category.
QString getSaveFileName(QWidget* parent, const QString& category, const QString& caption,
    const QString& suggestedName, const QString& filter, QString* selectedFilter = nullptr);

QString getOpenFileName(QWidget* parent, const QString& category, const QString& caption,
    const QString& filter, QString* selectedFilter = nullptr);

QStringList getOpenFileNames(QWidget* parent, const QString& category, const QString& caption,
    const QString& filter, QString* selectedFilter = nullptr);

} // namespace ruwa::shared::filedialog

#endif // RUWA_SHARED_FILE_DIALOG_MEMORY_H
