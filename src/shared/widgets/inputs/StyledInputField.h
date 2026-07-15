// SPDX-License-Identifier: MPL-2.0

// StyledInputField.h
#ifndef RUWA_SHARED_WIDGETS_INPUTS_STYLEDINPUTFIELD_H
#define RUWA_SHARED_WIDGETS_INPUTS_STYLEDINPUTFIELD_H

#include <QWidget>
#include <QString>
#include <QVariant>

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

    explicit StyledInputField(const QString& label, FieldType type, QWidget* parent = nullptr);
    ~StyledInputField() override;

    void setText(const QString& text);
    QString text() const;

    void setValue(int value);
    int value() const;

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

private slots:
    void onThemeChanged();

private:
    QString m_labelText;
    FieldType m_type;

    QLineEdit* m_lineEdit { nullptr };
    QComboBox* m_comboBox { nullptr };
    QLabel* m_label { nullptr };

    int m_intMin { 1 };
    int m_intMax { 99999 };

    qreal m_focusProgress { 0.0 };
    qreal m_hoverProgress { 0.0 };

    QPropertyAnimation* m_focusAnimation { nullptr };
    QPropertyAnimation* m_hoverAnimation { nullptr };

    QWidget* m_inputContainer { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_INPUTS_STYLEDINPUTFIELD_H
