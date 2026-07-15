// SPDX-License-Identifier: MPL-2.0

// HomePageContent.cpp
#include "HomePageContent.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPainter>

namespace ruwa::ui::widgets {

HomePageContent::HomePageContent(QWidget* parent)
    : QWidget(parent)
{
    // setupContent() will be called by derived classes after their construction
}

void HomePageContent::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // Draw content background
    painter.fillRect(rect(), colors.background);
}

} // namespace ruwa::ui::widgets
