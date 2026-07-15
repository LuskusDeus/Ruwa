// SPDX-License-Identifier: MPL-2.0

// SearchBar.h
#ifndef RUWA_UI_WIDGETS_COMMON_SEARCHBAR_H
#define RUWA_UI_WIDGETS_COMMON_SEARCHBAR_H

#include <QIcon>
#include <QString>
#include <QWidget>

class QLineEdit;
class QPropertyAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Capsule search bar with animated focus border.
 *
 * Features:
 * - Animated focus and hover effects
 * - Bright gradient border on focus
 * - Search icon
 * - Theme-aware styling
 */
class SearchBar : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal focusProgress READ focusProgress WRITE setFocusProgress)
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit SearchBar(QWidget* parent = nullptr);
    ~SearchBar() override;

    /// When enabled, a mouse press outside the bar's geometry clears its focus
    /// (useful when the bar lives inside a larger panel rather than a dialog).
    void setClickOutsideClearsFocus(bool enabled);

    /// Set placeholder text
    void setPlaceholder(const QString& text);

    /// Override the capsule height (default 40). The rounded-capsule radius
    /// tracks the height automatically.
    void setBarHeight(int height);

    /// Override the hard minimum width (default 250). Lower it when the bar
    /// lives inside a narrow panel so it can shrink instead of being clipped.
    void setMinimumBarWidth(int width);

    /// Get current search text
    QString text() const;

    /// Set search text (e.g. for prefill)
    void setText(const QString& text);

    /// Clear search text
    void clear();

    /// Show or hide the built-in line edit clear button.
    void setClearButtonEnabled(bool enabled);

    /// Animation properties
    qreal focusProgress() const { return m_focusProgress; }
    void setFocusProgress(qreal progress);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

signals:
    void textChanged(const QString& text);
    void searchRequested(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void setupAnimations();
    void startFocusAnimation(bool focused);
    void startHoverAnimation(bool hovered);
    void updateThemeColors();

private slots:
    void onThemeChanged();
    void onTextChanged(const QString& text);

private:
    QLineEdit* m_lineEdit { nullptr };
    QIcon m_searchIcon;
    bool m_clickOutsideClearsFocus { false };
    bool m_appFilterInstalled { false };

    // Animation state
    qreal m_focusProgress { 0.0 };
    qreal m_hoverProgress { 0.0 };

    // Animations
    QPropertyAnimation* m_focusAnimation { nullptr };
    QPropertyAnimation* m_hoverAnimation { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_SEARCHBAR_H
