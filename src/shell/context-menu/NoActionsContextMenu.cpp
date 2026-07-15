// SPDX-License-Identifier: MPL-2.0

#include "NoActionsContextMenu.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QPainter>
#include <QFontMetrics>

namespace ruwa::ui::widgets {

NoActionsContextMenu::NoActionsContextMenu(QWidget* parent)
    : BaseContextMenu(parent)
{
    updateLayoutMetrics();
}

QRect NoActionsContextMenu::contentRect() const
{
    return m_contentRect;
}

void NoActionsContextMenu::drawContent(QPainter& painter)
{
    using namespace ruwa::ui::core;

    auto& styleMgr = WidgetStyleManager::instance();
    const auto& colors = styleMgr.colors();

    const QColor bgColor = colors.surface;
    const QColor borderColor = colors.border;

    paintMenuDropShadow(painter, m_contentRect, qreal(CornerRadius));

    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(bgColor);
    painter.drawRoundedRect(m_contentRect, CornerRadius, CornerRadius);

    painter.setPen(colors.textMuted);
    const QString line = tr("(No actions for this)");
    painter.drawText(m_contentRect, Qt::AlignCenter, line);

    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(m_contentRect, CornerRadius, CornerRadius);
}

void NoActionsContextMenu::onContextChanged()
{
    updateLayoutMetrics();
    update();
}

void NoActionsContextMenu::applyPresentationLayout(qreal progress)
{
    if (m_sizeFull.isEmpty()) {
        return;
    }
    const qreal u = 0.92 + 0.08 * progress;
    if (progress >= 1.0 - 1e-5) {
        setFixedSize(m_sizeFull);
        m_contentRect = m_contentRectFull;
        return;
    }
    const int w = qMax(1, qRound(m_sizeFull.width() * u));
    const int h = qMax(1, qRound(m_sizeFull.height() * u));
    setFixedSize(w, h);
    const QRect& fr = m_contentRectFull;
    m_contentRect = QRect(qRound(fr.x() * u), qRound(fr.y() * u), qMax(1, qRound(fr.width() * u)),
        qMax(1, qRound(fr.height() * u)));
}

void NoActionsContextMenu::updateLayoutMetrics()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int padH = theme.scaled(14);
    const int padV = theme.scaled(10);
    const int shadowGutter = theme.scaled(14);
    const int shadowBottom = theme.scaled(26);

    QFontMetrics fm(font());
    const QString line = tr("(No actions for this)");
    const int textW = fm.horizontalAdvance(line);
    const int textH = fm.height();

    const int panelW = textW + 2 * padH;
    const int panelH = textH + 2 * padV;

    setFixedSize(panelW + 2 * shadowGutter, panelH + shadowGutter + shadowBottom);
    m_contentRect = QRect(shadowGutter, shadowGutter, panelW, panelH);
    m_contentRectFull = m_contentRect;
    m_sizeFull = size();
    updateSlideMetrics();
    applyPresentationLayout(showProgress());
    syncVisualPosition();
}

} // namespace ruwa::ui::widgets
