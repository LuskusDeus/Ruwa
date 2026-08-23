// SPDX-License-Identifier: MPL-2.0

#include "PropertyRowLayout.h"

#include <QGridLayout>
#include <QLabel>
#include <QWidget>

namespace ruwa::ui::widgets {

namespace {

constexpr int kHorizontalSpacing = 8;
constexpr int kVerticalSpacing = 8;

} // namespace

PropertyRowLayout::PropertyRowLayout(QWidget* host)
    : m_host(host)
{
    m_grid = new QGridLayout(host);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(kHorizontalSpacing);
    m_grid->setVerticalSpacing(kVerticalSpacing);
    // Only the editor column takes the slack, so the captions stay put while
    // the panel is resized.
    m_grid->setColumnStretch(0, 0);
    m_grid->setColumnStretch(1, 1);
}

QLabel* PropertyRowLayout::addRow(
    const QString& caption, QWidget* field, Qt::Alignment fieldAlignment)
{
    auto* label = new QLabel(caption, m_host);
    // The caption column is sized by the group, not by the label, so the label
    // must not insist on its own width.
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_grid->addWidget(label, m_row, 0);
    m_grid->addWidget(field, m_row, 1, fieldAlignment);
    ++m_row;
    m_labels.append(label);
    return label;
}

void PropertyRowLayout::addFullWidthRow(QWidget* widget)
{
    m_grid->addWidget(widget, m_row, 0, 1, 2);
    ++m_row;
}

int PropertyRowLayout::naturalLabelWidth() const
{
    int widest = 0;
    for (const QLabel* label : m_labels) {
        widest = qMax(widest, label->sizeHint().width());
    }
    return widest;
}

void PropertyRowLayout::setLabelColumnWidth(int width)
{
    const int applied = qMax(width, naturalLabelWidth());
    m_grid->setColumnMinimumWidth(0, applied);
    // The column minimum alone leaves each label at its own width inside the
    // cell, which is invisible for left-aligned text but matters the moment a
    // caption is elided or centred.
    for (QLabel* label : m_labels) {
        label->setFixedWidth(applied);
    }
}

void PropertyRowLayout::refreshLabelMetrics()
{
    for (QLabel* label : m_labels) {
        label->setMinimumWidth(0);
        label->setMaximumWidth(QWIDGETSIZE_MAX);
        label->updateGeometry();
    }
    m_grid->setColumnMinimumWidth(0, 0);
}

void alignPropertyColumns(const QList<PropertyRowLayout*>& layouts)
{
    int widest = 0;
    for (PropertyRowLayout* layout : layouts) {
        if (!layout) {
            continue;
        }
        layout->refreshLabelMetrics();
        widest = qMax(widest, layout->naturalLabelWidth());
    }
    for (PropertyRowLayout* layout : layouts) {
        if (layout) {
            layout->setLabelColumnWidth(widest);
        }
    }
}

} // namespace ruwa::ui::widgets
