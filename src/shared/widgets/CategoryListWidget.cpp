// SPDX-License-Identifier: MPL-2.0

// CategoryListWidget.cpp
#include "CategoryListWidget.h"
#include "CategoryListItem.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "features/theme/manager/ThemeManager.h"

#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QTimer>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

// =============================================================================
// CategoryHeader Implementation
// =============================================================================

CategoryHeader::CategoryHeader(const QString& title, QWidget* parent)
    : QWidget(parent)
    , m_title(title)
{
    setCursor(Qt::PointingHandCursor);

    m_expandAnimation = new QPropertyAnimation(this, "expandProgress", this);
    m_expandAnimation->setDuration(ANIMATION_DURATION);
    m_expandAnimation->setEasingCurve(QEasingCurve::OutCubic);

    updateScaledSize();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &CategoryHeader::onThemeChanged);
}

void CategoryHeader::setTitle(const QString& title)
{
    if (m_title != title) {
        m_title = title;
        update();
    }
}

void CategoryHeader::setExpanded(bool expanded, bool animated)
{
    if (m_isExpanded != expanded) {
        m_isExpanded = expanded;

        if (animated) {
            m_expandAnimation->stop();
            m_expandAnimation->setStartValue(m_expandProgress);
            m_expandAnimation->setEndValue(expanded ? 1.0 : 0.0);
            m_expandAnimation->start();
        } else {
            m_expandProgress = expanded ? 1.0 : 0.0;
            update();
        }

        emit toggled(expanded);
    }
}

void CategoryHeader::setExpandProgress(qreal progress)
{
    if (!qFuzzyCompare(m_expandProgress, progress)) {
        m_expandProgress = progress;
        update();
    }
}

void CategoryHeader::updateScaledSize()
{
    const auto& theme = ThemeManager::instance();
    setFixedHeight(theme.scaled(BASE_HEIGHT));
}

void CategoryHeader::onThemeChanged()
{
    updateScaledSize();
    update();
}

void CategoryHeader::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    int padding = theme.scaled(BASE_PADDING);

    // Background on hover
    if (m_isHovered) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceHover());
        painter.drawRoundedRect(rect().adjusted(2, 0, -2, 0), 4, 4);
    }

    // Expand/collapse arrow
    int arrowSize = theme.scaled(10);
    QRect arrowRect(padding, (height() - arrowSize) / 2, arrowSize, arrowSize);

    painter.save();
    painter.translate(arrowRect.center());
    // Rotate based on expand progress: 0 = pointing right, 1 = pointing down
    painter.rotate(90 * m_expandProgress);
    painter.translate(-arrowRect.center());

    QPainterPath arrowPath;
    QPointF center = arrowRect.center();
    int halfSize = arrowSize / 2 - 1;

    // Triangle pointing right
    arrowPath.moveTo(center.x() - halfSize / 2, center.y() - halfSize);
    arrowPath.lineTo(center.x() + halfSize / 2, center.y());
    arrowPath.lineTo(center.x() - halfSize / 2, center.y() + halfSize);
    arrowPath.closeSubpath();

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.textMuted);
    painter.drawPath(arrowPath);
    painter.restore();

    // Title text
    QFont titleFont = font();
    titleFont.setPointSize(theme.scaledFontSize(9));
    titleFont.setBold(true);
    titleFont.setCapitalization(QFont::AllUppercase);
    painter.setFont(titleFont);

    painter.setPen(colors.textMuted);

    QRect textRect = rect().adjusted(padding + arrowSize + padding, 0, -padding, 0);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_title);

    // === Separator line from title to right edge ===
    QFontMetrics fm(titleFont);
    int textWidth = fm.horizontalAdvance(m_title);
    int lineStartX = textRect.left() + textWidth + padding;
    int lineY = rect().center().y();

    if (lineStartX < rect().right() - padding) {
        QColor lineColor = colors.border;
        lineColor.setAlphaF(0.3);
        painter.setPen(QPen(lineColor, 1));
        painter.drawLine(lineStartX, lineY, rect().right() - padding, lineY);
    }
}

void CategoryHeader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isPressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void CategoryHeader::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isPressed) {
        m_isPressed = false;
        if (rect().contains(event->pos())) {
            setExpanded(!m_isExpanded);
        }
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void CategoryHeader::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);
    m_isHovered = true;
    update();
}

void CategoryHeader::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);
    m_isHovered = false;
    m_isPressed = false;
    update();
}

// =============================================================================
// CategoryListWidget Implementation
// =============================================================================

CategoryListWidget::CategoryListWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &CategoryListWidget::onThemeChanged);
}

CategoryListWidget::~CategoryListWidget() = default;

void CategoryListWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_contentWidget = new QWidget();
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* contentLayout = new QVBoxLayout(m_contentWidget);
    contentLayout->setAlignment(Qt::AlignTop);

    m_scrollArea->setWidget(m_contentWidget);
    mainLayout->addWidget(m_scrollArea);

    updateScaledSizes();
}

void CategoryListWidget::updateScaledSizes()
{
    const auto& theme = ThemeManager::instance();

    if (m_contentWidget && m_contentWidget->layout()) {
        int margin = theme.scaled(BASE_CONTENT_MARGIN);
        int spacing = theme.scaled(BASE_CATEGORY_SPACING);
        m_contentWidget->layout()->setContentsMargins(margin, margin, margin, margin);
        m_contentWidget->layout()->setSpacing(spacing);
    }
}

void CategoryListWidget::onThemeChanged()
{
    updateScaledSizes();
    update();
}

void CategoryListWidget::addCategory(const QString& title, bool itemsDeletable)
{
    CategoryData category;
    category.title = title;
    category.itemsDeletable = itemsDeletable;
    m_categories.append(category);

    rebuildContent();
}

void CategoryListWidget::addItem(
    int categoryIndex, const QString& text, const QVariant& data, bool deletable)
{
    if (categoryIndex < 0 || categoryIndex >= m_categories.size())
        return;

    CategoryListItemData itemData;
    itemData.text = text;
    itemData.data = data;
    itemData.deletable = deletable && m_categories[categoryIndex].itemsDeletable;

    m_categories[categoryIndex].items.append(itemData);

    rebuildContent();
}

void CategoryListWidget::clear()
{
    m_selectedItem = nullptr;
    m_categories.clear();
    m_categoryWidgets.clear();

    // Clear content widget
    if (m_contentWidget && m_contentWidget->layout()) {
        QLayoutItem* item;
        while ((item = m_contentWidget->layout()->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }
}

void CategoryListWidget::setCategories(const QVector<CategoryData>& categories)
{
    m_categories = categories;
    rebuildContent();
}

void CategoryListWidget::rebuildContent()
{
    // Store selected and active data to restore after rebuild
    QVariant selectedData;
    QVariant activeData;

    if (m_selectedItem) {
        selectedData = m_selectedItem->data();
    }
    if (m_activeItem) {
        activeData = m_activeItem->data();
    }

    m_selectedItem = nullptr;
    m_activeItem = nullptr;

    // Clear existing widgets
    m_categoryWidgets.clear();
    if (m_contentWidget && m_contentWidget->layout()) {
        QLayoutItem* item;
        while ((item = m_contentWidget->layout()->takeAt(0)) != nullptr) {
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    auto* contentLayout = qobject_cast<QVBoxLayout*>(m_contentWidget->layout());
    if (!contentLayout)
        return;

    const auto& theme = ThemeManager::instance();
    int itemSpacing = theme.scaled(BASE_SPACING);

    // Create widgets for each category
    for (int catIdx = 0; catIdx < m_categories.size(); ++catIdx) {
        const auto& category = m_categories[catIdx];

        CategoryWidgets catWidgets;

        // Category header
        catWidgets.header = new CategoryHeader(category.title, m_contentWidget);
        catWidgets.header->setExpanded(category.expanded, false);

        connect(catWidgets.header, &CategoryHeader::toggled, this,
            [this, catIdx](bool expanded) { onCategoryToggled(catIdx, expanded); });

        contentLayout->addWidget(catWidgets.header);

        // Items container
        catWidgets.itemsContainer = new QWidget(m_contentWidget);
        auto* itemsLayout = new QVBoxLayout(catWidgets.itemsContainer);
        itemsLayout->setContentsMargins(0, 0, 0, 0);
        itemsLayout->setSpacing(itemSpacing);

        // Create items
        for (const auto& itemData : category.items) {
            auto* item = new CategoryListItem(itemData.text, catWidgets.itemsContainer);
            item->setData(itemData.data);
            item->setDeletable(itemData.deletable);
            item->setEditable(itemData.editable); // Set editable flag
            item->setFavorite(itemData.isFavorite); // Set favorite flag

            connect(
                item, &CategoryListItem::clicked, this, [this, item]() { onItemClicked(item); });

            connect(item, &CategoryListItem::deleteRequested, this,
                [this, item]() { onItemDeleteRequested(item); });

            connect(
                item, &CategoryListItem::textChanged, this, [this, item](const QString& newText) {
                    emit itemTextChanged(item->data(), newText);
                });

            connect(item, &CategoryListItem::favoriteToggled, this, [this, item](bool isFavorite) {
                emit itemFavoriteToggled(item->data(), isFavorite);
            });

            itemsLayout->addWidget(item);
            catWidgets.items.append(item);

            // Restore selection
            if (selectedData.isValid() && itemData.data == selectedData) {
                selectItem(item);
            }

            // Restore active state
            if (activeData.isValid() && itemData.data == activeData) {
                m_activeItem = item;
                m_activeItem->setActive(true);
            }
        }

        catWidgets.itemsContainer->setVisible(category.expanded);
        contentLayout->addWidget(catWidgets.itemsContainer);

        m_categoryWidgets.append(catWidgets);
    }

    contentLayout->addStretch();

    // Force layout update
    QTimer::singleShot(0, this, [this]() { m_contentWidget->adjustSize(); });
}

QVariant CategoryListWidget::selectedData() const
{
    if (m_selectedItem) {
        return m_selectedItem->data();
    }
    return QVariant();
}

void CategoryListWidget::selectByData(const QVariant& data)
{
    for (const auto& catWidgets : m_categoryWidgets) {
        for (auto* item : catWidgets.items) {
            if (item->data() == data) {
                selectItem(item);
                return;
            }
        }
    }
}

void CategoryListWidget::clearSelection()
{
    if (m_selectedItem) {
        m_selectedItem->setSelected(false);
        m_selectedItem = nullptr;
        emit selectionChanged(QVariant());
    }
}

QVariant CategoryListWidget::activeData() const
{
    if (m_activeItem) {
        return m_activeItem->data();
    }
    return QVariant();
}

void CategoryListWidget::setActiveByData(const QVariant& data)
{
    // Clear previous active
    if (m_activeItem) {
        m_activeItem->setActive(false);
        m_activeItem = nullptr;
    }

    // Find and set new active
    if (data.isValid()) {
        for (const auto& catWidgets : m_categoryWidgets) {
            for (auto* item : catWidgets.items) {
                if (item->data() == data) {
                    m_activeItem = item;
                    m_activeItem->setActive(true);
                    return;
                }
            }
        }
    }
}

void CategoryListWidget::clearActive()
{
    if (m_activeItem) {
        m_activeItem->setActive(false);
        m_activeItem = nullptr;
    }
}

void CategoryListWidget::setCategoryExpanded(int categoryIndex, bool expanded)
{
    if (categoryIndex < 0 || categoryIndex >= m_categoryWidgets.size())
        return;

    m_categoryWidgets[categoryIndex].header->setExpanded(expanded);
}

bool CategoryListWidget::isCategoryExpanded(int categoryIndex) const
{
    if (categoryIndex < 0 || categoryIndex >= m_categoryWidgets.size())
        return false;

    return m_categoryWidgets[categoryIndex].header->isExpanded();
}

void CategoryListWidget::selectItem(CategoryListItem* item)
{
    if (m_selectedItem == item)
        return;

    // Deselect previous
    if (m_selectedItem) {
        m_selectedItem->setSelected(false);
    }

    // Select new
    m_selectedItem = item;
    if (m_selectedItem) {
        m_selectedItem->setSelected(true);
    }
}

void CategoryListWidget::onCategoryToggled(int categoryIndex, bool expanded)
{
    if (categoryIndex < 0 || categoryIndex >= m_categories.size())
        return;

    m_categories[categoryIndex].expanded = expanded;

    // Animate items container visibility
    if (categoryIndex < m_categoryWidgets.size()) {
        auto* container = m_categoryWidgets[categoryIndex].itemsContainer;
        if (container) {
            container->setVisible(expanded);

            // Force scroll area to update
            QTimer::singleShot(0, this, [this]() { m_contentWidget->adjustSize(); });
        }
    }
}

void CategoryListWidget::onItemClicked(CategoryListItem* item)
{
    selectItem(item);

    emit itemClicked(item->data());
    emit selectionChanged(item->data());
}

void CategoryListWidget::onItemDeleteRequested(CategoryListItem* item)
{
    emit deleteRequested(item->data());
}

} // namespace ruwa::ui::widgets
