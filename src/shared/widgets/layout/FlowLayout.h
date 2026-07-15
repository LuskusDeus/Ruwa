// SPDX-License-Identifier: MPL-2.0

// FlowLayout.h
#ifndef RUWA_UI_WIDGETS_COMMON_FLOWLAYOUT_H
#define RUWA_UI_WIDGETS_COMMON_FLOWLAYOUT_H

#include <QLayout>
#include <QRect>
#include <QStyle>

namespace ruwa::ui::widgets {

/**
 * @brief Layout that arranges items in rows, wrapping to new row when needed
 *
 * Automatically wraps items to the next row when there's not enough horizontal space.
 * Perfect for card grids that need to adapt to container width.
 */
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget* parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

    /// When \p columns > 0, items are placed in a grid with that many columns of equal width.
    /// When 0 (default), uses the classic wrap-by-sizeHint flow.
    void setMaxColumns(int columns);

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_itemList;
    int m_hSpace;
    int m_vSpace;
    int m_maxColumns = 0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_FLOWLAYOUT_H
