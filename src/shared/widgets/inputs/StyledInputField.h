// SPDX-License-Identifier: MPL-2.0

// StyledInputField.h
#ifndef RUWA_SHARED_WIDGETS_INPUTS_STYLEDINPUTFIELD_H
#define RUWA_SHARED_WIDGETS_INPUTS_STYLEDINPUTFIELD_H

#include "shared/resources/IconProvider.h"

#include <QLocale>
#include <QPixmap>
#include <QString>
#include <QVariant>
#include <QWidget>

class QLineEdit;
class QComboBox;
class QPropertyAnimation;
class QLabel;
class QEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Common themed input field
 *
 * Text / Number / Dropdown: optional label above (uppercase, muted), filled rounded field
 * (padding, radius, hover/focus ring). Text uses QLineEdit; Number uses QLineEdit + int validator.
 */
class StyledInputField : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal focusProgress READ focusProgress WRITE setFocusProgress)
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    enum class FieldType { Text, Number, Dropdown };

    /**
     * @brief How much air the box puts around its text.
     *
     * Comfortable is the form-field default (New Project, brush editor).
     * Compact hugs the text instead, for fields that sit in a toolbar or a
     * panel header where a full-size form field would dominate the strip.
     */
    enum class Density { Comfortable, Compact };

    explicit StyledInputField(const QString& label, FieldType type, QWidget* parent = nullptr);
    ~StyledInputField() override;

    void setDensity(Density density);
    Density density() const { return m_density; }

    /**
     * @brief Show a glyph inside the box, ahead of the text.
     *
     * The slot is reserved in the box's left padding, so the icon is part of the
     * field rather than a neighbour of it — same arrangement as HexColorInput's
     * "#" prefix. It is decorative: never selectable, never part of the text.
     * Re-tinted automatically on a theme change.
     */
    void setLeadingIcon(ruwa::ui::core::IconProvider::StandardIcon icon);
    void clearLeadingIcon();
    bool hasLeadingIcon() const { return m_hasLeadingIcon; }

    void setText(const QString& text);
    QString text() const;

    void setValue(int value);
    /// Number fields: the current value, clamped to the range. Text that does not
    /// parse (empty, half-typed, pasted with a group separator) yields the last
    /// value that did — never the range minimum.
    int value() const;
    /// Number fields: true when the visible text parses and sits inside the range.
    bool hasValidValue() const;

    void addItem(const QString& text, const QVariant& userData = QVariant());
    void addItems(const QStringList& texts);
    void clear();

    QString currentText() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

    void setPlaceholder(const QString& placeholder);
    /// QLineEdit only; no-op if this field has no line edit.
    void setMaxLength(int maxLength);
    void setLabel(const QString& label);
    QString labelText() const { return m_labelText; }

    void setRange(int min, int max);

    qreal focusProgress() const { return m_focusProgress; }
    void setFocusProgress(qreal progress);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    void clearInputFocus();

    /// Pixels from top of widget to top of boxed input (label + gap). 0 if no box / no label.
    int boxedContentTopInset() const;
    /// Height of the rounded input box; 0 if no box.
    int boxedInputHeight() const;
    /// Y of the boxed input within this widget (after layout); aligns companions to the box, not
    /// the label.
    int boxedInputTopY() const;

signals:
    void textChanged(const QString& text);
    void valueChanged(int value);
    void currentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI(FieldType type);
    void setupAnimations();
    void startFocusAnimation(bool focused);
    void startHoverAnimation(bool hovered);
    QWidget* inputWidget() const;
    void updateScaledSizes();
    void updateThemeColors();
    /// Number fields: rewrite the text to the canonical form of the committed
    /// value when focus leaves, so what is shown is always what value() returns.
    void commitNumericFixup();

    // Box metrics, density-dependent and already theme-scaled.
    int boxPaddingV() const;
    int boxPaddingH() const;
    int boxBorderRadius() const;
    /// Width the leading glyph claims inside the box's left padding, gap
    /// included. Zero when there is no leading icon.
    int leadingSlotWidth() const;
    void rebuildLeadingPixmap();

private slots:
    void onThemeChanged();

private:
    QString m_labelText;
    FieldType m_type;
    Density m_density { Density::Comfortable };

    bool m_hasLeadingIcon { false };
    ruwa::ui::core::IconProvider::StandardIcon m_leadingIconType {};
    QPixmap m_leadingPixmap;

    QLineEdit* m_lineEdit { nullptr };
    QComboBox* m_comboBox { nullptr };
    QLabel* m_label { nullptr };

    int m_intMin { 1 };
    int m_intMax { 99999 };
    /// Last value that parsed and validated — what an unparsable field falls back to.
    int m_lastValidValue { 1 };
    /// Locale the number text is read with; the same one the validator uses.
    QLocale m_numberLocale;

    qreal m_focusProgress { 0.0 };
    qreal m_hoverProgress { 0.0 };

    QPropertyAnimation* m_focusAnimation { nullptr };
    QPropertyAnimation* m_hoverAnimation { nullptr };

    QWidget* m_inputContainer { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_INPUTS_STYLEDINPUTFIELD_H
