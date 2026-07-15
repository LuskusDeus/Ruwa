// SPDX-License-Identifier: MPL-2.0

// ReorderableRowWidget.cpp
#include "shared/widgets/reorderlist/ReorderableRowWidget.h"

namespace ruwa::ui::widgets {

void ReorderableRowWidget::setRowOpacity(qreal v)
{
    if (qFuzzyCompare(m_rowOpacity, v))
        return;
    m_rowOpacity = v;
    update();
}

} // namespace ruwa::ui::widgets
