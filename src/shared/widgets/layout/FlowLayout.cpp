// SPDX-License-Identifier: MPL-2.0

// FlowLayout.cpp
#include "FlowLayout.h"
#include <QWidget>

namespace ruwa::ui::widgets {

FlowLayout::FlowLayout(QWidget* parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent)
    , m_hSpace(hSpacing)
    , m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing)
    , m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    QLayoutItem* item;
    while ((item = takeAt(0))) {
        delete item;
    }
}

void FlowLayout::addItem(QLayoutItem* item)
{
    m_itemList.append(item);
}

int FlowLayout::horizontalSpacing() const
{
    if (m_hSpace >= 0) {
        return m_hSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
    }
}

int FlowLayout::verticalSpacing() const
{
    if (m_vSpace >= 0) {
        return m_vSpace;
    } else {
        return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
    }
}

int FlowLayout::count() const
{
    return m_itemList.size();
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return m_itemList.value(index);
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_itemList.size()) {
        return m_itemList.takeAt(index);
    }
    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    int height = doLayout(QRect(0, 0, width, 0), true);
    return height;
}

void FlowLayout::setMaxColumns(int columns)
{
    m_maxColumns = qMax(0, columns);
    invalidate();
}

void FlowLayout::setGeometry(const QRect& rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);

    // Notify parent that our size hint changed
    if (QWidget* parent = parentWidget()) {
        parent->updateGeometry();
    }
}

QSize FlowLayout::sizeHint() const
{
    // Use parent widget width if available
    QWidget* parent = parentWidget();
    if (parent) {
        int width = parent->width();
        const QMargins margins = contentsMargins();
        width -= margins.left() + margins.right();

        int height = heightForWidth(width);
        return QSize(
            width + margins.left() + margins.right(), height + margins.top() + margins.bottom());
    }
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    const QMargins margins = contentsMargins();

    if (m_maxColumns > 0) {
        int maxMinW = 0;
        for (const QLayoutItem* item : m_itemList) {
            maxMinW = qMax(maxMinW, item->minimumSize().width());
        }
        const int cols = m_maxColumns;
        int spaceX = horizontalSpacing();
        if (spaceX < 0 && parentWidget()) {
            spaceX = parentWidget()->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
        }
        if (spaceX < 0)
            spaceX = 0;

        int w = cols * maxMinW;
        if (cols > 1)
            w += (cols - 1) * spaceX;
        w += margins.left() + margins.right();

        int h = heightForWidth(w);
        h += margins.top() + margins.bottom();
        return QSize(w, h);
    }

    QSize size;
    for (const QLayoutItem* item : m_itemList) {
        size = size.expandedTo(item->minimumSize());
    }

    size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    return size;
}

int FlowLayout::doLayout(const QRect& rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);

    if (m_maxColumns > 0) {
        int spaceX = horizontalSpacing();
        if (spaceX == -1) {
            if (QWidget* pw = parentWidget()) {
                spaceX = pw->style()->layoutSpacing(
                    QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
            }
            if (spaceX == -1)
                spaceX = 0;
        }

        int spaceY = verticalSpacing();
        if (spaceY == -1) {
            if (QWidget* pw = parentWidget()) {
                spaceY = pw->style()->layoutSpacing(
                    QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
            }
            if (spaceY == -1)
                spaceY = 0;
        }

        const int cols = m_maxColumns;
        const int totalHSpacing = (cols > 1) ? (cols - 1) * spaceX : 0;
        const int cellW = qMax(1, (effectiveRect.width() - totalHSpacing) / cols);

        int rowTop = effectiveRect.y();
        int col = 0;
        int x = effectiveRect.x();
        int curRowH = 0;

        for (int i = 0; i < m_itemList.size(); ++i) {
            QLayoutItem* item = m_itemList[i];
            QWidget* wid = item->widget();

            int itemH;
            if (wid && wid->hasHeightForWidth()) {
                itemH = wid->heightForWidth(cellW);
            } else {
                const QSize sh = item->sizeHint();
                itemH = sh.height();
                if (sh.width() > 0 && cellW != sh.width()) {
                    itemH
                        = qMax(itemH, qRound(static_cast<qreal>(sh.height()) * cellW / sh.width()));
                }
            }
            itemH = qMax(itemH, item->minimumSize().height());

            if (!testOnly) {
                item->setGeometry(QRect(x, rowTop, cellW, itemH));
            }

            curRowH = qMax(curRowH, itemH);
            ++col;

            const bool more = (i + 1 < m_itemList.size());
            if (col >= cols) {
                rowTop += curRowH;
                if (more)
                    rowTop += spaceY;
                curRowH = 0;
                col = 0;
                x = effectiveRect.x();
            } else {
                x += cellW + spaceX;
            }
        }

        return rowTop + curRowH - rect.y() + bottom;
    }

    int x = effectiveRect.x();
    int y = effectiveRect.y();
    int lineHeight = 0;

    for (QLayoutItem* item : m_itemList) {
        const QWidget* wid = item->widget();
        int spaceX = horizontalSpacing();
        if (spaceX == -1) {
            spaceX = wid->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
        }

        int spaceY = verticalSpacing();
        if (spaceY == -1) {
            spaceY = wid->style()->layoutSpacing(
                QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);
        }

        int nextX = x + item->sizeHint().width() + spaceX;
        if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
            x = effectiveRect.x();
            y = y + lineHeight + spaceY;
            nextX = x + item->sizeHint().width() + spaceX;
            lineHeight = 0;
        }

        if (!testOnly) {
            item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));
        }

        x = nextX;
        lineHeight = qMax(lineHeight, item->sizeHint().height());
    }

    return y + lineHeight - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject* parent = this->parent();
    if (!parent) {
        return -1;
    } else if (parent->isWidgetType()) {
        QWidget* pw = static_cast<QWidget*>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    } else {
        return static_cast<QLayout*>(parent)->spacing();
    }
}

} // namespace ruwa::ui::widgets
