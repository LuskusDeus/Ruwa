// SPDX-License-Identifier: MPL-2.0

// HexColorInput.h
#ifndef RUWA_SHARED_WIDGETS_INPUTS_HEXCOLORINPUT_H
#define RUWA_SHARED_WIDGETS_INPUTS_HEXCOLORINPUT_H

#include <QLineEdit>
#include <QColor>

class QPropertyAnimation;
class QMimeData;

namespace ruwa::ui::workspace {
class ToolButton;
}

namespace ruwa::ui::widgets {

/**
 * @brief Capsule-shaped hex color input with a left-aligned "#" prefix.
 *
 * Visuals match CapsuleButton (Secondary): pill border + soft hover plate.
 * The "#" glyph is painted on the left with padding and is NOT part of the
 * editable text — it cannot be selected, deleted, or duplicated by paste.
 *
 * Text content stores hex digits only (e.g. "FFAA00"). Helpers convert
 * to/from the "#RRGGBB(AA)" form expected by callers.
 */
class HexColorInput : public QLineEdit {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit HexColorInput(QWidget* parent = nullptr);
    ~HexColorInput() override;

    /// Set hex (accepts "#RRGGBB", "RRGGBB", "#RGB", "RGB", with optional alpha).
    /// Stores digits only in the underlying QLineEdit.
    void setHex(const QString& hex);

    /// Returns hex with leading '#', e.g. "#FFAA00". Empty if no digits.
    QString hexWithHash() const;

    /// Shows an unobtrusive trailing action that copies the displayed HEX value.
    void setCopyButtonVisible(bool visible);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal p);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onThemeChanged();

private:
    void startHoverAnimation(bool hovered);
    void updateMargins();
    void positionCopyButton();
    void applyPalette();
    int hashSlotWidth() const;
    int hashLeftPadding() const;
    int rightPadding() const;
    QString sanitizeHexDigits(const QString& input) const;

    qreal m_hoverProgress { 0.0 };
    QPropertyAnimation* m_hoverAnimation { nullptr };
    ruwa::ui::workspace::ToolButton* m_copyButton { nullptr };
    bool m_copyButtonVisible { false };

    static constexpr int BaseHeight = 36;
    static constexpr int BaseRightPad = 14;
    static constexpr int BaseHashSlot = 18;
    static constexpr int BaseHashLeftPad = 12;
    static constexpr int BaseHashTextGap = 6;
    static constexpr int BaseCopyButtonSize = 26;
    static constexpr int BaseCopyIconSize = 15;
    static constexpr int BaseCopyRightPad = 5;
    static constexpr int BaseCopyTextGap = 5;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_INPUTS_HEXCOLORINPUT_H
