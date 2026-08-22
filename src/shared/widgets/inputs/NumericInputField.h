// SPDX-License-Identifier: MPL-2.0

// NumericInputField.h
#ifndef RUWA_SHARED_WIDGETS_INPUTS_NUMERICINPUTFIELD_H
#define RUWA_SHARED_WIDGETS_INPUTS_NUMERICINPUTFIELD_H

#include <QLineEdit>
#include <QPoint>
#include <QRect>
#include <QString>

class QPropertyAnimation;
class QWheelEvent;
class QMouseEvent;
class QKeyEvent;
class QFocusEvent;
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
 * Shift makes every nudge and drag coarse (10x the step), Ctrl makes it fine
 * (a tenth of it).
 *
 * Dragging horizontally over the field scrubs the value (Photoshop/Blender
 * style): the text itself never moves, the number counts up to the right and
 * down to the left. Reaching the edge of the window teleports the pointer to
 * the far side and the drag continues, so the range a scrub can cover is not
 * limited by how much room the pointer had left when it started. Scrubbing is
 * offered only while the field is *not* being text-edited, so a drag inside a
 * focused field still selects text as usual; a click without a drag focuses
 * the field and selects its contents.
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

    /// Optional label drawn before the number (e.g. "X"). Like the suffix it
    /// sits inside the capsule and outside the editable text, which is what
    /// makes a row of fields read without a separate column of labels.
    void setPrefix(const QString& prefix);
    QString prefix() const { return m_prefix; }

    /// True while a horizontal drag is actively changing the value.
    bool isScrubbing() const { return m_scrubbing; }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal p);

    QSize sizeHint() const override;

signals:
    /// Emitted once a horizontal drag crosses the movement threshold and takes
    /// over from text editing, and again when it lets go. Useful for callers
    /// that want to coalesce the whole drag into a single undo step.
    void scrubbingChanged(bool scrubbing);

    /// Emitted live as the value changes (typing a valid number, arrow-key or
    /// wheel nudges) — the same "continuous while editing" contract as other
    /// value editors in the effects panel; pair with a debounced-undo commit
    /// on the listening side if that matters to the caller.
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent* event) override;
    /// The scrub gesture. Press only arms it — the value does not move until
    /// the pointer travels far enough that the press cannot be read as a click.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    /// QLineEdit only emits editingFinished on focus-out when its validator
    /// accepts the text, so an emptied field would otherwise be left blank and
    /// never reported as finished. This restores the canonical value first.
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;

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
    /// Step multiplier for the modifiers held during a nudge or a scrub:
    /// Shift = coarse (10x), Ctrl = fine (0.1x).
    static double stepMultiplier(Qt::KeyboardModifiers modifiers);
    /// @param anchorGlobalX screen x the drag measures its first delta from.
    void beginScrub(int anchorGlobalX);
    void endScrub();
    /// Screen rect the pointer is kept inside while scrubbing: the window this
    /// field lives in, clamped to the screen holding it.
    QRect scrubWrapBounds() const;
    /// Teleports the pointer to the opposite edge when it reaches one, and
    /// reports whether it did. The drag is unaffected — the warp is folded into
    /// the anchor, so the value neither jumps nor stalls across it.
    bool wrapScrubPointer(int globalX, int globalY);
    /// Theme-scaled BaseScrubWrapInset.
    static int scrubWrapInset();
    /// True for a real mouse or trackpad — the devices whose pointer can be
    /// moved without the hardware immediately putting it back.
    static bool isRelativePointerEvent(const QMouseEvent* event);
    /// I-beam while text editing, horizontal arrows while the drag-to-scrub
    /// gesture is the thing a press would start.
    void updateScrubCursor();
    void applyValue(double value, bool reformatText);
    QString formatValue(double value) const;
    int suffixSlotWidth() const;
    int prefixSlotWidth() const;

    double m_minimum = 0.0;
    double m_maximum = 100.0;
    double m_step = 1.0;
    double m_value = 0.0;
    int m_decimals = 0;
    QString m_suffix;
    QString m_prefix;

    /// Armed on press, promoted to a real scrub once the pointer moves.
    bool m_scrubArmed { false };
    bool m_scrubbing { false };
    /// Qt hands a click-focusable widget the focus *before* delivering the
    /// press that caused it, so hasFocus() cannot tell "already editing" from
    /// "just clicked". This remembers which of the two the press was.
    bool m_focusFromPress { false };
    /// True when the armed press is what pulled focus in, i.e. the field was
    /// not being text-edited and a scrub should hand that focus back.
    bool m_pressTookFocus { false };
    QPoint m_pressPos;
    /// Screen x of the previous scrub event. Global rather than widget-local so
    /// that it survives the pointer being teleported across the window.
    int m_scrubLastGlobalX { 0 };
    /// Screen band the pointer wraps inside, frozen when the drag starts so a
    /// window moving underneath it cannot change the rules mid-gesture.
    QRect m_scrubWrapBounds;
    /// Unrounded value the drag accumulates into, so that sub-step pointer
    /// movement is not thrown away by the display rounding on every event.
    double m_scrubValue { 0.0 };

    qreal m_hoverProgress { 0.0 };
    QPropertyAnimation* m_hoverAnimation { nullptr };
    QDoubleValidator* m_validator { nullptr };

    static constexpr int BaseHeight = 30;
    static constexpr int BaseSidePad = 12;
    static constexpr int BaseSuffixGap = 3;
    /// Travel before a press stops being a click and becomes a scrub.
    static constexpr int BaseScrubThreshold = 4;
    /// How far inside the far edge the pointer reappears after a wrap. Landing
    /// exactly on the edge would re-trigger the wrap on the next pixel of
    /// travel in the same direction.
    static constexpr int BaseScrubWrapInset = 8;
    /// Pointer travel that equals one single step.
    static constexpr int BaseScrubPixelsPerStep = 2;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_INPUTS_NUMERICINPUTFIELD_H
