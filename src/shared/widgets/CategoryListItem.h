// SPDX-License-Identifier: MPL-2.0

// CategoryListItem.h
#ifndef RUWA_UI_WIDGETS_COMMON_CATEGORYLISTITEM_H
#define RUWA_UI_WIDGETS_COMMON_CATEGORYLISTITEM_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QVariant>

class QLineEdit;

namespace ruwa::ui::widgets {

/**
 * @brief Single item in CategoryListWidget
 *
 * Features:
 * - Smooth hover animation
 * - Optional delete button (icon placeholder)
 * - Selected state with primary color highlight
 * - Stores arbitrary data via QVariant
 */
class CategoryListItem : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal selectionProgress READ selectionProgress WRITE setSelectionProgress)

public:
    explicit CategoryListItem(const QString& text, QWidget* parent = nullptr);
    ~CategoryListItem() override;

    // Text
    QString text() const { return m_text; }
    void setText(const QString& text);

    // Selection
    bool isSelected() const { return m_isSelected; }
    void setSelected(bool selected);

    // Active state (for currently applied theme)
    bool isActive() const { return m_isActive; }
    void setActive(bool active);

    // Delete button visibility
    bool isDeletable() const { return m_isDeletable; }
    void setDeletable(bool deletable);

    // Favorite (star) button
    bool isFavorite() const { return m_isFavorite; }
    void setFavorite(bool favorite);

    // Editing support (can be disabled for built-in items)
    bool isEditable() const { return m_isEditable; }
    void setEditable(bool editable);

    bool isEditing() const { return m_isEditing; }
    void startEditing();
    void finishEditing(bool accept = true);

    // Custom data storage
    QVariant data() const { return m_data; }
    void setData(const QVariant& data) { m_data = data; }

    // Animation properties
    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal selectionProgress() const { return m_selectionProgress; }
    void setSelectionProgress(qreal progress);

signals:
    void clicked();
    void deleteRequested();
    void textChanged(const QString& newText);
    void favoriteToggled(bool isFavorite);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void setupAnimations();
    void updateScaledSize();
    QRect deleteButtonRect() const;
    QRect favoriteButtonRect() const;
    bool isOverDeleteButton(const QPoint& pos) const;
    bool isOverFavoriteButton(const QPoint& pos) const;

private slots:
    void onThemeChanged();

private:
    QString m_text;
    QVariant m_data;

    bool m_isSelected = false;
    bool m_isActive = false; // For currently applied theme indicator
    bool m_isDeletable = false;
    bool m_isEditable = true; // Can be renamed via double-click
    bool m_isFavorite = false; // Show in ThemeSelectorWidget (star button)
    bool m_isHovered = false;
    bool m_isPressed = false;
    bool m_deleteHovered = false;
    bool m_deletePressed = false;
    bool m_favoriteHovered = false; // Star button hover
    bool m_favoritePressed = false; // Star button pressed
    bool m_isEditing = false; // Editing mode flag

    qreal m_hoverProgress = 0.0;
    qreal m_selectionProgress = 0.0;

    QPropertyAnimation* m_hoverAnimation = nullptr;
    QPropertyAnimation* m_selectionAnimation = nullptr;

    // Editing widgets
    QLineEdit* m_editor = nullptr;

    // Base sizes (before scaling)
    static constexpr int BASE_HEIGHT = 36;
    static constexpr int BASE_PADDING_H = 12;
    static constexpr int BASE_PADDING_V = 6;
    static constexpr int BASE_DELETE_BUTTON_SIZE = 20;
    static constexpr int BASE_FAVORITE_BUTTON_SIZE = 20;
    static constexpr int BASE_BUTTON_SPACING = 4; // Space between favorite and delete buttons
    static constexpr int BASE_CORNER_RADIUS = 6;
    static constexpr int BASE_ACTIVE_INDICATOR_SIZE = 4;
    static constexpr int BASE_ACTIVE_INDICATOR_OFFSET = 8;
    static constexpr int ANIMATION_DURATION = 150;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_CATEGORYLISTITEM_H
