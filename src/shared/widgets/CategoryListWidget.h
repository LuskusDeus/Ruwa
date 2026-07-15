// SPDX-License-Identifier: MPL-2.0

// CategoryListWidget.h
#ifndef RUWA_UI_WIDGETS_COMMON_CATEGORYLISTWIDGET_H
#define RUWA_UI_WIDGETS_COMMON_CATEGORYLISTWIDGET_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QVector>
#include <QVariant>
#include <functional>

namespace ruwa::ui::widgets {

class CategoryListItem;
class SmoothScrollArea;

/**
 * @brief Category header widget for CategoryListWidget
 */
class CategoryHeader : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal expandProgress READ expandProgress WRITE setExpandProgress)

public:
    explicit CategoryHeader(const QString& title, QWidget* parent = nullptr);

    QString title() const { return m_title; }
    void setTitle(const QString& title);

    bool isExpanded() const { return m_isExpanded; }
    void setExpanded(bool expanded, bool animated = true);

    qreal expandProgress() const { return m_expandProgress; }
    void setExpandProgress(qreal progress);

signals:
    void toggled(bool expanded);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void updateScaledSize();

private slots:
    void onThemeChanged();

private:
    QString m_title;
    bool m_isExpanded = true;
    bool m_isHovered = false;
    bool m_isPressed = false;
    qreal m_expandProgress = 1.0;

    QPropertyAnimation* m_expandAnimation = nullptr;

    static constexpr int BASE_HEIGHT = 28;
    static constexpr int BASE_PADDING = 8;
    static constexpr int ANIMATION_DURATION = 200;
};

/**
 * @brief Item data structure for CategoryListWidget
 */
struct CategoryListItemData {
    QString text;
    QVariant data;
    bool deletable = true;
    bool editable = true; // Can be renamed via double-click
    bool isFavorite = false; // Show in ThemeSelectorWidget
};

/**
 * @brief Category data structure
 */
struct CategoryData {
    QString title;
    QVector<CategoryListItemData> items;
    bool expanded = true;
    bool itemsDeletable = true; // Whether items in this category can be deleted
};

/**
 * @brief List widget with collapsible categories
 *
 * Features:
 * - Multiple categories with headers
 * - Smooth scroll area
 * - Animated collapse/expand
 * - Single selection across all categories
 * - Delete button per item (configurable per category)
 * - Theme-aware styling
 */
class CategoryListWidget : public QWidget {
    Q_OBJECT

public:
    explicit CategoryListWidget(QWidget* parent = nullptr);
    ~CategoryListWidget() override;

    // === Category Management ===

    /// Add a new category
    void addCategory(const QString& title, bool itemsDeletable = true);

    /// Add item to category
    void addItem(
        int categoryIndex, const QString& text, const QVariant& data, bool deletable = true);

    /// Clear all categories and items
    void clear();

    /// Rebuild from data
    void setCategories(const QVector<CategoryData>& categories);

    // === Selection ===

    /// Get selected item data (invalid QVariant if nothing selected)
    QVariant selectedData() const;

    /// Select item by data
    void selectByData(const QVariant& data);

    /// Clear selection
    void clearSelection();

    // === Active Item (for currently applied theme) ===

    /// Get active item data (invalid QVariant if no active item)
    QVariant activeData() const;

    /// Set active item by data
    void setActiveByData(const QVariant& data);

    /// Clear active state
    void clearActive();

    // === Category State ===

    /// Expand/collapse category
    void setCategoryExpanded(int categoryIndex, bool expanded);
    bool isCategoryExpanded(int categoryIndex) const;

signals:
    /// Emitted when an item is clicked
    void itemClicked(const QVariant& data);

    /// Emitted when selection changes
    void selectionChanged(const QVariant& data);

    /// Emitted when delete is requested for an item
    void deleteRequested(const QVariant& data);

    /// Emitted when item text is changed
    void itemTextChanged(const QVariant& data, const QString& newText);

    /// Emitted when item favorite state is toggled
    void itemFavoriteToggled(const QVariant& data, bool isFavorite);

private:
    void setupUI();
    void rebuildContent();
    void updateScaledSizes();
    void selectItem(CategoryListItem* item);

private slots:
    void onThemeChanged();
    void onCategoryToggled(int categoryIndex, bool expanded);
    void onItemClicked(CategoryListItem* item);
    void onItemDeleteRequested(CategoryListItem* item);

private:
    struct CategoryWidgets {
        CategoryHeader* header = nullptr;
        QWidget* itemsContainer = nullptr;
        QVector<CategoryListItem*> items;
    };

    SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;

    QVector<CategoryData> m_categories;
    QVector<CategoryWidgets> m_categoryWidgets;

    CategoryListItem* m_selectedItem = nullptr;
    CategoryListItem* m_activeItem = nullptr; // Currently applied theme

    // Base sizes
    static constexpr int BASE_SPACING = 2;
    static constexpr int BASE_CATEGORY_SPACING = 8;
    static constexpr int BASE_CONTENT_MARGIN = 4;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_CATEGORYLISTWIDGET_H
