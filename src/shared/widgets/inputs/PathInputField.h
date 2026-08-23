// SPDX-License-Identifier: MPL-2.0

// PathInputField.h
#ifndef RUWA_SHARED_WIDGETS_INPUTS_PATHINPUTFIELD_H
#define RUWA_SHARED_WIDGETS_INPUTS_PATHINPUTFIELD_H

#include "shared/resources/IconProvider.h"

#include <QIcon>
#include <QLineEdit>
#include <QString>

class QPropertyAnimation;

namespace ruwa::ui::workspace {
class ToolButton;
}

namespace ruwa::ui::widgets {

/**
 * @brief Capsule-shaped file-path input: leading glyph, typed text, trailing action.
 *
 * The same shape and behaviour as HexColorInput — pill border, soft hover
 * plate, focus-tinted border — with the '#' slot holding an icon instead of a
 * glyph and the copy action replaced by one the caller names (a browse dialog,
 * a reveal-in-explorer). The path stays fully typeable: the trailing button is
 * a shortcut to the dialog, not the only way in.
 *
 * The button is a child of the line edit rather than a neighbour of it, so a
 * caller gets one widget to place instead of a field-plus-button row that has
 * to be kept aligned by hand.
 */
class PathInputField : public QLineEdit {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit PathInputField(QWidget* parent = nullptr);
    ~PathInputField() override;

    /// Glyph in the left slot. Folder by default.
    void setLeadingIcon(ruwa::ui::core::IconProvider::StandardIcon icon);

    /// Icon of the trailing action button, and what it says when hovered.
    void setActionIcon(ruwa::ui::core::IconProvider::StandardIcon icon);
    void setActionToolTip(const QString& text);
    /// Hides the trailing button for a field that has nothing to open.
    void setActionVisible(bool visible);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal p);

signals:
    /// The trailing button was pressed.
    void actionTriggered();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onThemeChanged();

private:
    void startHoverAnimation(bool hovered);
    void updateMargins();
    void updateScaledSize();
    void positionActionButton();
    void applyPalette();
    void rebuildLeadingIcons();
    [[nodiscard]] int iconSlotWidth() const;
    [[nodiscard]] int iconLeftPadding() const;
    [[nodiscard]] int rightPadding() const;

    qreal m_hoverProgress { 0.0 };
    QPropertyAnimation* m_hoverAnimation { nullptr };
    ruwa::ui::workspace::ToolButton* m_actionButton { nullptr };
    bool m_actionVisible { true };

    ruwa::ui::core::IconProvider::StandardIcon m_leadingIconType {
        ruwa::ui::core::IconProvider::StandardIcon::Folder
    };
    /// Two colourings of the same glyph, cross-faded by the hover/focus accent.
    /// Recolouring per frame would rasterise an SVG on every animation tick.
    QIcon m_leadingIconMuted;
    QIcon m_leadingIconActive;

    static constexpr int BaseHeight = 36;
    static constexpr int BaseRightPad = 14;
    static constexpr int BaseIconSlot = 18;
    static constexpr int BaseIconLeftPad = 12;
    static constexpr int BaseIconTextGap = 8;
    static constexpr int BaseActionButtonSize = 26;
    static constexpr int BaseActionIconSize = 15;
    static constexpr int BaseActionRightPad = 5;
    static constexpr int BaseActionTextGap = 5;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_INPUTS_PATHINPUTFIELD_H
