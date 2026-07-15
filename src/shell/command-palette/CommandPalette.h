// SPDX-License-Identifier: MPL-2.0

// CommandPalette.h
#ifndef RUWA_UI_WIDGETS_COMMANDPALETTE_COMMANDPALETTE_H
#define RUWA_UI_WIDGETS_COMMANDPALETTE_COMMANDPALETTE_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QVector>
#include <QHash>
#include <QTimer>
#include <QVariantMap>
#include <QPixmap>
#include <QPointer>

namespace ruwa::core {
class Command;
}

namespace ruwa::ui::widgets {

class SearchBar;
class SmoothScrollBar;

/**
 * @brief Data for a command palette item
 */
struct CommandItem {
    QString commandId;
    QString title;
    QString category;
    QString shortcut;
    bool enabled = true;
    QVariantMap args; // Parsed arguments (when in argument mode)
    QString argumentHint; // Hint text for arguments (e.g. "1920 × 1080")
};

/**
 * @brief Command palette with search and results list
 *
 * Features:
 * - Uses SearchBar for input
 * - Custom rendered results list
 * - Slide + fade animation for search bar
 * - List shows available commands and filters while typing
 * - Smooth item hover animations
 * - Smooth scrollbar for overflowing results
 */
class CommandPalette : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)
    Q_PROPERTY(qreal listShowProgress READ listShowProgress WRITE setListShowProgress)
    Q_PROPERTY(qreal scrollOffset READ scrollOffset WRITE setScrollOffset)

public:
    explicit CommandPalette(QWidget* parent = nullptr);
    ~CommandPalette() override;

    /// Show with animation
    void showAnimated();

    /// Hide with animation
    void hideAnimated();

    /// Focus the search bar
    void focusSearchBar();

    /// Refresh blurred glass backdrop from a widget behind the overlay
    void refreshGlassBackdropFrom(QWidget* source);

    /// Check if visible/animating
    bool isActive() const;

    // Animation properties
    qreal showProgress() const { return m_showProgress; }
    void setShowProgress(qreal progress);

    qreal listShowProgress() const { return m_listShowProgress; }
    void setListShowProgress(qreal progress);

    qreal scrollOffset() const { return m_scrollOffset; }
    void setScrollOffset(qreal offset);

signals:
    /// Emitted when user wants to close (Escape or command executed)
    void closeRequested();

    /// Emitted when a command is selected
    void commandSelected(const QString& commandId);

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onSearchTextChanged(const QString& text);
    void onThemeChanged();
    void onShowAnimationFinished();
    void onHideAnimationFinished();
    void updateHoverAnimation();
    void retranslateUi();
    void onScrollBarValueChanged(int value);
    void onScrollStepRequested(int delta);
    void updateSelectionAnimation();

private:
    void setupUI();
    void setupAnimations();
    void connectSignals();
    void populateCommands(const QString& filter);
    void executeSelected();
    void selectNext();
    void selectPrevious();
    void updateSize();

    // List rendering
    QRectF listRect() const;
    QRectF itemRect(int index) const;
    int itemAtPosition(const QPoint& pos) const;
    void ensureItemVisible(int index);
    void scrollToOffset(qreal offset, bool animated);
    void updateScrollBar();

    void drawSearchBarBackground(QPainter& painter);
    void drawList(QPainter& painter);
    void drawListBackground(QPainter& painter, const QRectF& rect);
    void drawItem(QPainter& painter, int index, const QRectF& rect);
    void drawGlassPanel(QPainter& painter, const QRectF& rect, int radius, bool hoverBorder);
    void drawSelectionHighlight(QPainter& painter, const QRectF& rect, qreal selectionProgress);

private:
    // UI
    SearchBar* m_searchBar = nullptr;
    SmoothScrollBar* m_scrollBar = nullptr;
    QPointer<QWidget> m_glassBackdropSource;
    QPixmap m_glassBackdrop;

    // Items
    QVector<CommandItem> m_items;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;

    // Hover animation per item
    QVector<qreal> m_itemHoverProgress;
    QVector<qreal> m_itemSelectionProgress;
    QTimer* m_hoverTimer = nullptr;
    QTimer* m_selectionTimer = nullptr;

    // Scroll
    qreal m_scrollOffset = 0.0;
    qreal m_targetScrollOffset = 0.0;
    qreal m_maxScrollOffset = 0.0;

    // Animation state
    qreal m_showProgress = 0.0;
    qreal m_listShowProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;

    // Animations
    QPropertyAnimation* m_showAnimation = nullptr;
    QPropertyAnimation* m_listAnimation = nullptr;
    QPropertyAnimation* m_scrollAnimation = nullptr;

    // Layout constants
    static constexpr int PaletteWidth = 500;
    static constexpr int SearchBarHeight = 44;
    static constexpr int ListMarginTop = 8;
    static constexpr int ItemHeight = 52;
    static constexpr int MaxVisibleItems = 8;
    static constexpr int ListPadding = 6;
    static constexpr int ItemRadius = 6;
    static constexpr int ListRadius = 10;
    static constexpr int ScrollbarWidth = 4;

    // Animation
    static constexpr int ShowDuration = 200;
    static constexpr int HideDuration = 150;
    static constexpr int ListShowDuration = 150;
    static constexpr int ScrollDuration = 120;
    static constexpr int WheelItemsPerStep = 9;
    static constexpr qreal WheelPixelMultiplier = 1.67;
    static constexpr int GlassBlurRadius = 38;
    static constexpr qreal SelectionSelectStep = 0.16;
    static constexpr qreal SelectionUnselectStep = 0.12;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMANDPALETTE_COMMANDPALETTE_H
