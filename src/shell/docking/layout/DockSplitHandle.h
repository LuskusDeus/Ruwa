// SPDX-License-Identifier: MPL-2.0

// DockSplitHandle.h
#ifndef RUWA_UI_DOCKING_LAYOUT_SPLIT_HANDLE_H
#define RUWA_UI_DOCKING_LAYOUT_SPLIT_HANDLE_H

#include "shell/docking/DockLayoutTypes.h"
#include "features/theme/manager/ThemeColors.h"

#include <QWidget>

namespace ruwa::ui::docking {

class DockSplitNode;

/**
 * @brief Handle widget for resizing between adjacent layout nodes
 *
 * DockSplitHandle is a thin widget placed between children of a DockSplitNode.
 * When dragged, it emits signals that the DockLayoutRoot uses to update
 * the split node's sizes.
 *
 * Features:
 * - Visual feedback on hover/drag
 * - Cursor changes based on split direction
 * - Theme support for colors
 */
class DockSplitHandle : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Create a split handle
     *
     * @param splitNode The split node this handle belongs to
     * @param handleIndex Index of this handle (0 = between child 0 and 1)
     * @param direction Split direction (determines cursor and drag axis)
     * @param parent Parent widget (usually the container)
     */
    explicit DockSplitHandle(DockSplitNode* splitNode, int handleIndex, SplitDirection direction,
        QWidget* parent = nullptr);

    ~DockSplitHandle() override = default;

    // === Properties ===

    /**
     * @brief Get the split node this handle belongs to
     */
    DockSplitNode* splitNode() const { return m_splitNode; }

    /**
     * @brief Get handle index within split node
     */
    int handleIndex() const { return m_handleIndex; }

    /**
     * @brief Set handle index
     */
    void setHandleIndex(int index) { m_handleIndex = index; }

    /**
     * @brief Get split direction
     */
    SplitDirection direction() const { return m_direction; }

    /**
     * @brief Set split direction
     */
    void setDirection(SplitDirection direction);

    // === Theme ===

    /**
     * @brief Apply theme colors
     */
    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

    /**
     * @brief Set handle colors directly
     */
    void setColors(const QColor& normal, const QColor& hover, const QColor& pressed);

signals:
    /**
     * @brief Emitted when drag starts
     */
    void dragStarted(DockSplitNode* node, int handleIndex);

    /**
     * @brief Emitted during drag
     *
     * @param delta Pixels moved since last event (in split direction)
     */
    void dragMoved(DockSplitNode* node, int handleIndex, int delta);

    /**
     * @brief Emitted when drag ends
     */
    void dragFinished(DockSplitNode* node, int handleIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void updateCursor();
    int dragAxisGlobalPosition() const;

private:
    DockSplitNode* m_splitNode = nullptr;
    int m_handleIndex = 0;
    SplitDirection m_direction = SplitDirection::Horizontal;

    // Interaction state
    bool m_hovered = false;
    bool m_dragging = false;
    int m_lastDragPos = 0;

    // Colors
    QColor m_normalColor { 80, 80, 80 };
    QColor m_hoverColor { 100, 100, 100 };
    QColor m_pressColor { 120, 120, 120 };
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_LAYOUT_SPLIT_HANDLE_H
