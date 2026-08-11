// SPDX-License-Identifier: MPL-2.0

// ShortcutRowWidget.h
#ifndef RUWA_UI_WIDGETS_SHORTCUTROWWIDGET_H
#define RUWA_UI_WIDGETS_SHORTCUTROWWIDGET_H

#include "shared/widgets/BaseStyledPanel.h"

#include <QKeySequence>
#include <QPair>
#include <QVector>

class QLabel;
class QHBoxLayout;

namespace ruwa::ui::widgets {

class CapsuleButton;
class CommandInputWidget;
class AnimatedComboBox;

/**
 * @brief A styled row widget representing a single keyboard shortcut
 *
 * Displays command name on the left, shortcut badge + reset button on the right.
 * Click on the shortcut badge to edit. Uses BaseStyledPanel for consistent styling.
 */
class ShortcutRowWidget : public BaseStyledPanel {
    Q_OBJECT

public:
    explicit ShortcutRowWidget(const QString& commandId, const QString& commandTitle,
        const QString& commandDescription, const QKeySequence& shortcut,
        const QKeySequence& defaultShortcut, QWidget* parent = nullptr);
    explicit ShortcutRowWidget(const QString& commandId, const QString& commandTitle,
        const QString& commandDescription, int choice, int defaultChoice,
        const QVector<QPair<QString, int>>& choices, QWidget* parent = nullptr);
    ~ShortcutRowWidget() override;

    QString commandId() const { return m_commandId; }
    QString commandTitle() const { return m_commandTitle; }
    QString commandDescription() const { return m_commandDescription; }

    void setShortcut(const QKeySequence& shortcut);
    QKeySequence shortcut() const { return m_shortcut; }
    void setChoice(int choice);
    int choice() const { return m_choice; }
    void setConflicted(bool conflicted);
    bool isConflicted() const { return m_conflicted; }

    /// Check if this row matches a search query
    bool matchesSearch(const QString& query) const;

signals:
    void shortcutChanged(const QString& commandId, const QKeySequence& newShortcut);
    void choiceChanged(const QString& commandId, int choice);
    void resetRequested(const QString& commandId);

private:
    void setupUI();
    void updateScaledSizes();
    void updateThemeColors();
    void updateWarningIcon();
    void updateResetButtonVisibility();

private slots:
    void onThemeChanged();

private:
    QString m_commandId;
    QString m_commandTitle;
    QString m_commandDescription;
    QKeySequence m_shortcut;
    QKeySequence m_defaultShortcut;
    bool m_usesChoiceInput { false };
    int m_choice { 0 };
    int m_defaultChoice { 0 };
    QVector<QPair<QString, int>> m_choices;
    bool m_conflicted { false };

    // UI elements
    QLabel* m_titleLabel { nullptr };
    QLabel* m_descLabel { nullptr };
    CommandInputWidget* m_commandInput { nullptr };
    AnimatedComboBox* m_choiceInput { nullptr };
    QLabel* m_warningIcon { nullptr };
    CapsuleButton* m_resetButton { nullptr };
    QHBoxLayout* m_layout { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_SHORTCUTROWWIDGET_H
