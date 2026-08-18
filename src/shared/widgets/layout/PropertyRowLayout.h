// SPDX-License-Identifier: MPL-2.0

// PropertyRowLayout.h
#ifndef RUWA_SHARED_WIDGETS_LAYOUT_PROPERTYROWLAYOUT_H
#define RUWA_SHARED_WIDGETS_LAYOUT_PROPERTYROWLAYOUT_H

#include <QList>
#include <QString>

class QGridLayout;
class QLabel;
class QWidget;

namespace ruwa::ui::widgets {

/**
 * @brief Two-column property rows: captions in column 0, editors in column 1.
 *
 * A panel is usually several groups deep, and each group left to itself sizes
 * its caption column to its own longest word — so "Size" and "Space Before"
 * push their fields to different x positions and the panel reads as two
 * unrelated forms. The layout therefore separates the caption column's
 * *natural* width from the width it is *given*, and \ref alignPropertyColumns
 * hands every participating group the widest of them.
 *
 * Not a widget: it manages a QGridLayout on a host widget, so a group's content
 * stays one widget deep and CollapsibleSection keeps measuring what it already
 * measures.
 */
class PropertyRowLayout {
public:
    /// Installs the grid on @p host. The host must not already have a layout.
    explicit PropertyRowLayout(QWidget* host);

    /// Caption + editor. Returns the caption label, for callers that restyle it.
    QLabel* addRow(const QString& caption, QWidget* field);
    /// A control that spans both columns (a dropdown, a swatch, a button row).
    void addFullWidthRow(QWidget* widget);

    /// Widest caption in this layout, before any shared width is applied.
    int naturalLabelWidth() const;
    /// Width the caption column is held at. Ignored when narrower than the
    /// natural width, so a shared column can only ever widen a group.
    void setLabelColumnWidth(int width);

    /// Re-measures the captions after a font or translation change.
    void refreshLabelMetrics();

    QGridLayout* grid() const { return m_grid; }

private:
    QWidget* m_host = nullptr;
    QGridLayout* m_grid = nullptr;
    QList<QLabel*> m_labels;
    int m_row = 0;
};

/// Gives every layout in @p layouts one caption column, as wide as the widest
/// caption among them. Call it after building the groups and again whenever the
/// theme or the language changes.
void alignPropertyColumns(const QList<PropertyRowLayout*>& layouts);

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_LAYOUT_PROPERTYROWLAYOUT_H
