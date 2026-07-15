// SPDX-License-Identifier: MPL-2.0

// CategoryListItem.cpp
#include "CategoryListItem.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QLineEdit>
#include <cmath> // For M_PI (star drawing)

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

CategoryListItem::CategoryListItem(const QString& text, QWidget* parent)
    : QWidget(parent)
    , m_text(text)
{
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    setupAnimations();
    updateScaledSize();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &CategoryListItem::onThemeChanged);
}

CategoryListItem::~CategoryListItem() = default;

void CategoryListItem::setupAnimations()
{
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(ANIMATION_DURATION);

    m_selectionAnimation = new QPropertyAnimation(this, "selectionProgress", this);
    m_selectionAnimation->setDuration(ANIMATION_DURATION + 50); // Slightly longer for smoother feel
    m_selectionAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void CategoryListItem::updateScaledSize()
{
    const auto& theme = ThemeManager::instance();
    setFixedHeight(theme.scaled(BASE_HEIGHT));
}

void CategoryListItem::setText(const QString& text)
{
    if (m_text != text) {
        m_text = text;
        update();
    }
}

void CategoryListItem::setSelected(bool selected)
{
    if (m_isSelected != selected) {
        m_isSelected = selected;

        // Animate selection
        m_selectionAnimation->stop();
        m_selectionAnimation->setStartValue(m_selectionProgress);
        m_selectionAnimation->setEndValue(selected ? 1.0 : 0.0);
        m_selectionAnimation->start();
    }
}

void CategoryListItem::setActive(bool active)
{
    if (m_isActive != active) {
        m_isActive = active;
        update();
    }
}

void CategoryListItem::setDeletable(bool deletable)
{
    if (m_isDeletable != deletable) {
        m_isDeletable = deletable;
        update();
    }
}

void CategoryListItem::setEditable(bool editable)
{
    if (m_isEditable != editable) {
        m_isEditable = editable;
        // If currently editing and becoming non-editable, finish editing
        if (!editable && m_isEditing) {
            finishEditing(false);
        }
    }
}

void CategoryListItem::setFavorite(bool favorite)
{
    if (m_isFavorite != favorite) {
        m_isFavorite = favorite;
        update();
    }
}

void CategoryListItem::startEditing()
{
    if (m_isEditing)
        return;

    m_isEditing = true;

    // Create line edit if doesn't exist
    if (!m_editor) {
        m_editor = new QLineEdit(this);

        const auto& theme = ThemeManager::instance();
        const auto& colors = theme.colors();

        // Style the editor
        m_editor->setStyleSheet(QString("QLineEdit {"
                                        "  background-color: %1;"
                                        "  border: 1px solid %2;"
                                        "  border-radius: %3px;"
                                        "  padding: 4px 8px;"
                                        "  color: %4;"
                                        "  selection-background-color: %5;"
                                        "}")
                .arg(colors.surface.name())
                .arg(colors.primary.name())
                .arg(theme.scaled(BASE_CORNER_RADIUS))
                .arg(colors.text.name())
                .arg(colors.primary.name()));

        // Connect signals
        connect(m_editor, &QLineEdit::editingFinished, this, [this]() { finishEditing(true); });
    }

    // Setup editor
    m_editor->setText(m_text);
    m_editor->selectAll();

    // Position editor
    const auto& theme = ThemeManager::instance();
    int paddingH = theme.scaled(BASE_PADDING_H);
    int paddingV = theme.scaled(BASE_PADDING_V);

    int rightMargin = paddingH;
    if (m_isDeletable) {
        rightMargin += theme.scaled(BASE_DELETE_BUTTON_SIZE) + theme.scaled(4);
    }

    m_editor->setGeometry(
        paddingH, paddingV, width() - paddingH - rightMargin, height() - paddingV * 2);

    m_editor->show();
    m_editor->setFocus();
}

void CategoryListItem::finishEditing(bool accept)
{
    if (!m_isEditing || !m_editor)
        return;

    m_isEditing = false;

    if (accept) {
        QString newText = m_editor->text().trimmed();
        if (!newText.isEmpty() && newText != m_text) {
            m_text = newText;
            emit textChanged(m_text);
        }
    }

    m_editor->hide();
    update();
}

void CategoryListItem::setHoverProgress(qreal progress)
{
    if (!qFuzzyCompare(m_hoverProgress, progress)) {
        m_hoverProgress = progress;
        update();
    }
}

void CategoryListItem::setSelectionProgress(qreal progress)
{
    if (!qFuzzyCompare(m_selectionProgress, progress)) {
        m_selectionProgress = progress;
        update();
    }
}

QRect CategoryListItem::deleteButtonRect() const
{
    const auto& theme = ThemeManager::instance();
    int btnSize = theme.scaled(BASE_DELETE_BUTTON_SIZE);
    int padding = theme.scaled(BASE_PADDING_H);

    return QRect(width() - padding - btnSize, (height() - btnSize) / 2, btnSize, btnSize);
}

QRect CategoryListItem::favoriteButtonRect() const
{
    const auto& theme = ThemeManager::instance();
    int btnSize = theme.scaled(BASE_FAVORITE_BUTTON_SIZE);
    int padding = theme.scaled(BASE_PADDING_H);
    int spacing = theme.scaled(BASE_BUTTON_SPACING);

    // Calculate position: to the left of delete button (if deletable)
    int rightOffset = padding;
    if (m_isDeletable) {
        // Leave space for delete button (always visible now)
        rightOffset += theme.scaled(BASE_DELETE_BUTTON_SIZE) + spacing;
    }

    return QRect(width() - rightOffset - btnSize, (height() - btnSize) / 2, btnSize, btnSize);
}

bool CategoryListItem::isOverDeleteButton(const QPoint& pos) const
{
    if (!m_isDeletable)
        return false;
    return deleteButtonRect().contains(pos);
}

bool CategoryListItem::isOverFavoriteButton(const QPoint& pos) const
{
    return favoriteButtonRect().contains(pos);
}

void CategoryListItem::onThemeChanged()
{
    updateScaledSize();
    update();
}

void CategoryListItem::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    int radius = theme.scaled(BASE_CORNER_RADIUS);
    int paddingH = theme.scaled(BASE_PADDING_H);
    int paddingV = theme.scaled(BASE_PADDING_V);

    QRectF itemRect = rect().adjusted(2, 1, -2, -1);

    // === Background ===
    // Base: transparent or hover
    QColor baseBg = Qt::transparent;
    if (m_hoverProgress > 0 && m_selectionProgress < 1.0) {
        baseBg = colors.surfaceHover();
        baseBg.setAlphaF(baseBg.alphaF() * m_hoverProgress * (1.0 - m_selectionProgress));
    }

    // Selection: interpolate to primary
    QColor selectedBg = colors.primary;

    // Combine: blend based on selectionProgress
    QColor bgColor;
    if (m_selectionProgress > 0.01) {
        // Interpolate from base to selected
        int r = baseBg.red() + (selectedBg.red() - baseBg.red()) * m_selectionProgress;
        int g = baseBg.green() + (selectedBg.green() - baseBg.green()) * m_selectionProgress;
        int b = baseBg.blue() + (selectedBg.blue() - baseBg.blue()) * m_selectionProgress;
        int a = qMax(baseBg.alpha(), static_cast<int>(selectedBg.alpha() * m_selectionProgress));
        bgColor = QColor(r, g, b, a);
    } else {
        bgColor = baseBg;
    }

    if (bgColor.alpha() > 0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(itemRect, radius, radius);
    }

    // === Text ===
    // Interpolate text color based on selection progress
    QColor normalTextColor = colors.text;
    QColor selectedTextColor = colors.textOnPrimary();

    QColor textColor;
    if (m_selectionProgress > 0.01) {
        int r = normalTextColor.red()
            + (selectedTextColor.red() - normalTextColor.red()) * m_selectionProgress;
        int g = normalTextColor.green()
            + (selectedTextColor.green() - normalTextColor.green()) * m_selectionProgress;
        int b = normalTextColor.blue()
            + (selectedTextColor.blue() - normalTextColor.blue()) * m_selectionProgress;
        textColor = QColor(r, g, b);
    } else {
        textColor = normalTextColor;
    }
    painter.setPen(textColor);

    QFont textFont = font();
    textFont.setPointSize(theme.scaledFontSize(10));
    painter.setFont(textFont);

    // Calculate text rect (leave space for delete button if deletable and active indicator)
    int leftMargin = paddingH;

    // Add space for active indicator if this item is active
    if (m_isActive) {
        leftMargin += theme.scaled(BASE_ACTIVE_INDICATOR_SIZE + BASE_ACTIVE_INDICATOR_OFFSET);
    }

    int rightMargin = paddingH;

    // Space for favorite button (always visible)
    rightMargin += theme.scaled(BASE_FAVORITE_BUTTON_SIZE) + theme.scaled(4);

    // Additional space for delete button if deletable (always visible)
    if (m_isDeletable) {
        rightMargin += theme.scaled(BASE_DELETE_BUTTON_SIZE) + theme.scaled(4);
    }

    QRect textRect(itemRect.left() + leftMargin, itemRect.top() + paddingV,
        itemRect.width() - leftMargin - rightMargin, itemRect.height() - paddingV * 2);

    // Don't draw text if editing
    if (!m_isEditing) {
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

    // === Active indicator (colored dot on the left) ===
    if (m_isActive) {
        int indicatorSize = theme.scaled(BASE_ACTIVE_INDICATOR_SIZE);
        int indicatorX = itemRect.left() + paddingH;
        int indicatorY = itemRect.center().y() - indicatorSize / 2;

        QRect indicatorRect(indicatorX, indicatorY, indicatorSize, indicatorSize);

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.primary);
        painter.drawEllipse(indicatorRect);
    }

    // === Favorite button (star) ===
    // Always visible
    {
        QRect btnRect = favoriteButtonRect();

        // Button background on hover
        if (m_favoriteHovered || m_favoritePressed) {
            QColor btnBg = colors.text;
            if (m_favoritePressed) {
                btnBg = btnBg.darker(110);
            }
            btnBg.setAlphaF(m_favoriteHovered ? 0.15 : 0.25);

            painter.setPen(Qt::NoPen);
            painter.setBrush(btnBg);
            painter.drawRoundedRect(btnRect, radius - 2, radius - 2);
        }

        // Star icon color - same as text color
        QColor normalIconColor = colors.text;
        QColor selectedIconColor = colors.textOnPrimary();

        QColor iconColor;
        if (m_selectionProgress > 0.01) {
            int r = normalIconColor.red()
                + (selectedIconColor.red() - normalIconColor.red()) * m_selectionProgress;
            int g = normalIconColor.green()
                + (selectedIconColor.green() - normalIconColor.green()) * m_selectionProgress;
            int b = normalIconColor.blue()
                + (selectedIconColor.blue() - normalIconColor.blue()) * m_selectionProgress;
            iconColor = QColor(r, g, b);
        } else {
            iconColor = normalIconColor;
        }

        // Draw star (5-pointed star)
        QPointF center = btnRect.center();
        qreal outerRadius = btnRect.width() * 0.4;
        qreal innerRadius = outerRadius * 0.4;

        QPainterPath starPath;
        for (int i = 0; i < 10; ++i) {
            qreal angle = -M_PI / 2 + (i * M_PI / 5); // Start from top
            qreal radius = (i % 2 == 0) ? outerRadius : innerRadius;
            qreal x = center.x() + radius * cos(angle);
            qreal y = center.y() + radius * sin(angle);

            if (i == 0) {
                starPath.moveTo(x, y);
            } else {
                starPath.lineTo(x, y);
            }
        }
        starPath.closeSubpath();

        if (m_isFavorite) {
            // Filled star
            painter.setPen(Qt::NoPen);
            painter.setBrush(iconColor);
            painter.drawPath(starPath);
        } else {
            // Outlined star
            painter.setPen(QPen(iconColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(starPath);
        }
    }

    // === Delete button ===
    if (m_isDeletable) {
        QRect btnRect = deleteButtonRect();

        // Button background on hover
        if (m_deleteHovered || m_deletePressed) {
            QColor btnBg;
            if (m_deletePressed) {
                btnBg = colors.error;
                btnBg = btnBg.darker(110);
            } else {
                btnBg = colors.error;
            }
            btnBg.setAlphaF(m_deleteHovered ? 0.15 : 0.25);

            painter.setPen(Qt::NoPen);
            painter.setBrush(btnBg);
            painter.drawRoundedRect(btnRect, radius - 2, radius - 2);
        }

        // X icon - same color as text
        QColor normalIconColor = colors.text;
        QColor selectedIconColor = colors.textOnPrimary();

        QColor iconColor;
        if (m_selectionProgress > 0.01) {
            int r = normalIconColor.red()
                + (selectedIconColor.red() - normalIconColor.red()) * m_selectionProgress;
            int g = normalIconColor.green()
                + (selectedIconColor.green() - normalIconColor.green()) * m_selectionProgress;
            int b = normalIconColor.blue()
                + (selectedIconColor.blue() - normalIconColor.blue()) * m_selectionProgress;
            iconColor = QColor(r, g, b);
        } else {
            iconColor = normalIconColor;
        }

        if (m_deleteHovered) {
            iconColor = colors.error;
        }

        painter.setPen(QPen(iconColor, 1.5, Qt::SolidLine, Qt::RoundCap));

        int iconMargin = btnRect.width() / 4;
        QRect iconRect = btnRect.adjusted(iconMargin, iconMargin, -iconMargin, -iconMargin);

        painter.drawLine(iconRect.topLeft(), iconRect.bottomRight());
        painter.drawLine(iconRect.topRight(), iconRect.bottomLeft());
    }
}

void CategoryListItem::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);
    m_isHovered = true;

    // Hover animation
    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
}

void CategoryListItem::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);
    m_isHovered = false;
    m_deleteHovered = false;
    m_deletePressed = false;
    m_favoriteHovered = false;
    m_favoritePressed = false;

    // Hover animation out
    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
}

void CategoryListItem::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (isOverDeleteButton(event->pos())) {
            m_deletePressed = true;
        } else if (isOverFavoriteButton(event->pos())) {
            m_favoritePressed = true;
        } else {
            m_isPressed = true;
        }
        update();
    }
    QWidget::mousePressEvent(event);
}

void CategoryListItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_deletePressed && isOverDeleteButton(event->pos())) {
            emit deleteRequested();
        } else if (m_favoritePressed && isOverFavoriteButton(event->pos())) {
            // Toggle favorite state
            m_isFavorite = !m_isFavorite;
            emit favoriteToggled(m_isFavorite);
        } else if (m_isPressed && rect().contains(event->pos()) && !isOverDeleteButton(event->pos())
            && !isOverFavoriteButton(event->pos())) {
            emit clicked();
        }

        m_isPressed = false;
        m_deletePressed = false;
        m_favoritePressed = false;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void CategoryListItem::mouseMoveEvent(QMouseEvent* event)
{
    bool wasDeleteHovered = m_deleteHovered;
    bool wasFavoriteHovered = m_favoriteHovered;

    m_deleteHovered = isOverDeleteButton(event->pos());
    m_favoriteHovered = isOverFavoriteButton(event->pos());

    if (wasDeleteHovered != m_deleteHovered || wasFavoriteHovered != m_favoriteHovered) {
        setCursor((m_deleteHovered || m_favoriteHovered) ? Qt::PointingHandCursor
                                                         : Qt::PointingHandCursor);
        update();
    }

    QWidget::mouseMoveEvent(event);
}

void CategoryListItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !isOverDeleteButton(event->pos())
        && !isOverFavoriteButton(event->pos()) && m_isEditable) { // Only allow editing if editable
        startEditing();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace ruwa::ui::widgets
