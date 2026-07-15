// SPDX-License-Identifier: MPL-2.0

// NumericInputField.h
#ifndef RUWA_SHARED_WIDGETS_INPUTS_NUMERICINPUTFIELD_H
#define RUWA_SHARED_WIDGETS_INPUTS_NUMERICINPUTFIELD_H

#include <QLineEdit>
#include <QString>

class QPropertyAnimation;
class QWheelEvent;
class QKeyEvent;
class QResizeEvent;
class QDoubleValidator;

namespace ruwa::ui::widgets {

/**
 * @brief Capsule-styled numeric text input — no baked-in label, pair it with
 * your own label in a two-column layout (e.g. an effect param row).
 *
 * Visuals match HexColorInput / the Color-panel hex field: pill border + soft
 * hover plate + focus-tinted gradient border. Unlike a range slider, the value
 * is typed directly — a better fit for parameters where a drag range doesn't
 * map to anything meaningful (e.g. an absolute pixel position).
 *
 * Supports integer or decimal ranges (setDecimals(0) for integers). Up/Down
 * arrow keys and the mouse wheel (while focused) nudge by the configured step;
 * holding Shift nudges by 10x that step.
 */
class NumericInputField : public QLineEdit {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)

public:
    explicit NumericInputField(QWidget* parent = nullptr);
    ~NumericInputField() override;

    void setRange(double minimum, double maximum);
    double minimum() const { return m_minimum; }
    double maximum() const { return m_maximum; }

    /// Amount +/- nudges (arrow keys, wheel) change the value by.
    void setSingleStep(double step);
    double singleStep() const { return m_step; }

    /// 0 = integer display/typing; >0 = that many decimal places.
    void setDecimals(int decimals);
    int decimals() const { return m_decimals; }

    void setValue(double value);
    double value() const { return m_value; }

    /// Optional unit drawn after the number (e.g. "px"). Not part of the
    /// editable text, same convention as HexColorInput's '#' glyph.
    void setSuffix(const QString& suffix);
    QString suffix() const { return m_suffix; }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal p);

    QSize sizeHint() const override;

signals:
    /// Emitted live as the value changes (typing a valid number, arrow-key or
    /// wheel nudges) — the same "continuous while editing" contract as other
    /// value editors in the effects panel; pair with a debounced-undo commit
    /// on the listening side if that matters to the caller.
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onThemeChanged();
    void onTextEdited(const QString& text);
    /// Fires on Return/Enter AND on focus-out (QLineEdit's own contract) —
    /// re-stamps the canonical formatted text, discarding a partially-typed or
    /// out-of-range string and snapping back to the last valid clamped value.
    void onEditingFinished();

private:
    void startHoverAnimation(bool hovered);
    void applyPalette();
    void updateMargins();
    void nudge(double delta);
    void applyValue(double value, bool reformatText);
    QString formatValue(double value) const;
    int suffixSlotWidth() const;

    double m_minimum = 0.0;
    double m_maximum = 100.0;
    double m_step = 1.0;
    double m_value = 0.0;
    int m_decimals = 0;
    QString m_suffix;

    qreal m_hoverProgress { 0.0 };
    QPropertyAnimation* m_hoverAnimation { nullptr };
    QDoubleValidator* m_validator { nullptr };

    static constexpr int BaseHeight = 30;
    static constexpr int BaseSidePad = 12;
    static constexpr int BaseSuffixGap = 3;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_INPUTS_NUMERICINPUTFIELD_H
