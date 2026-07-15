// SPDX-License-Identifier: MPL-2.0

// StyledInputField.cpp
#include "StyledInputField.h"
#include "features/theme/manager/ThemeManager.h"

#include <QVBoxLayout>
#include <QLineEdit>
#include <QIntValidator>
#include <QValidator>
#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QFocusEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QSizePolicy>

namespace ruwa::ui::widgets {

namespace {
// Reference: project-panel.html — .field (boxed)
const int BASE_LABEL_GAP = 6;
const int BASE_BOX_PAD_V = 10;
const int BASE_BOX_PAD_V_NUMBER = 7; ///< Tighter box for compact numeric fields (New Project, etc.)
const int BASE_BOX_PAD_H = 14;
const int BASE_BORDER_RADIUS = 8;
// Base sizes follow ThemeManager::scaledFontSize "UI steps" (same ballpark as other panels).
const int BASE_LABEL_FONT = 9;
const int BASE_BOX_INPUT_FONT = 10;
const int BASE_ARROW_SIZE = 4;
const int BASE_ARROW_OFFSET = 14;

QString boxedLabelText(const QString& raw)
{
    const QString t = raw.trimmed();
    return t.isEmpty() ? QString() : t.toUpper();
}
} // namespace

// ============================================================================
// Construction
// ============================================================================

StyledInputField::StyledInputField(const QString& label, FieldType type, QWidget* parent)
    : QWidget(parent)
    , m_labelText(label)
    , m_type(type)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);

    setupUI(type);
    setupAnimations();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &StyledInputField::onThemeChanged);
}

StyledInputField::~StyledInputField()
{
    delete m_focusAnimation;
    delete m_hoverAnimation;
}

void StyledInputField::setupAnimations()
{
    m_focusAnimation = new QPropertyAnimation(this, "focusProgress");
    m_focusAnimation->setDuration(250);
    m_focusAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress");
    m_hoverAnimation->setDuration(200);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void StyledInputField::setupUI(FieldType type)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    QString transparentInputStyle = QString(R"(
        QLineEdit, QComboBox {
            background-color: transparent;
            color: %1;
            border: none;
            padding: 0px;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox::down-arrow {
            image: none;
            width: 0px;
            height: 0px;
        }
        QComboBox QAbstractItemView {
            background-color: %2;
            color: %1;
            border: 1px solid %3;
            border-radius: 6px;
            selection-background-color: %3;
            padding: 4px;
        }
    )")
                                        .arg(colors.text.name())
                                        .arg(colors.surface.name())
                                        .arg(colors.overlay(0.06).name(QColor::HexArgb));

    switch (type) {
    case FieldType::Text:
    case FieldType::Number:
    case FieldType::Dropdown: {
        m_label = new QLabel(boxedLabelText(m_labelText), this);
        m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

        m_inputContainer = new QWidget(this);
        m_inputContainer->setAttribute(Qt::WA_TranslucentBackground);
        m_inputContainer->setAttribute(Qt::WA_NoSystemBackground);
        m_inputContainer->setAutoFillBackground(false);
        m_inputContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        QVBoxLayout* containerLayout = new QVBoxLayout(m_inputContainer);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        outerLayout->setSpacing(theme.scaled(BASE_LABEL_GAP));
        outerLayout->addWidget(m_label);
        outerLayout->addWidget(m_inputContainer);

        if (type == FieldType::Dropdown) {
            m_comboBox = new QComboBox(m_inputContainer);
            m_comboBox->setStyleSheet(transparentInputStyle);
            m_comboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_comboBox->installEventFilter(this);
            // No AlignLeft: in a QVBoxLayout it applies a horizontal alignment mask and the
            // control keeps ~sizeHint width — the painted box then looks like a narrow strip.
            containerLayout->addWidget(m_comboBox);
            connect(m_comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                &StyledInputField::currentIndexChanged);
        } else {
            m_lineEdit = new QLineEdit(m_inputContainer);
            m_lineEdit->setStyleSheet(transparentInputStyle);
            m_lineEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_lineEdit->installEventFilter(this);
            containerLayout->addWidget(m_lineEdit);
            if (type == FieldType::Number) {
                m_lineEdit->setValidator(new QIntValidator(m_intMin, m_intMax, m_lineEdit));
                connect(m_lineEdit, &QLineEdit::textChanged, this, [this](const QString& t) {
                    bool ok = false;
                    const int v = t.toInt(&ok);
                    if (ok)
                        emit valueChanged(v);
                });
            } else {
                connect(m_lineEdit, &QLineEdit::textChanged, this, &StyledInputField::textChanged);
            }
        }

        if (m_label->text().isEmpty())
            m_label->hide();

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        break;
    }
    }

    updateThemeColors();
    updateScaledSizes();
}

void StyledInputField::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if ((m_type == FieldType::Text || m_type == FieldType::Number || m_type == FieldType::Dropdown)
        && m_inputContainer) {
        if (m_label) {
            QFont lf = colors.fonts.getUIFont(theme.scaledFontSize(BASE_LABEL_FONT));
            lf.setWeight(QFont::Normal);
            lf.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.5));
            m_label->setFont(lf);
        }

        if (QVBoxLayout* outer = qobject_cast<QVBoxLayout*>(layout()))
            outer->setSpacing(theme.scaled(BASE_LABEL_GAP));

        const int padV = (m_type == FieldType::Number || m_type == FieldType::Text)
            ? theme.scaled(BASE_BOX_PAD_V_NUMBER)
            : theme.scaled(BASE_BOX_PAD_V);
        const int padH = theme.scaled(BASE_BOX_PAD_H);
        const int rightPad
            = m_comboBox ? padH + theme.scaled(BASE_ARROW_OFFSET + BASE_ARROW_SIZE + 2) : padH;

        QWidget* input = inputWidget();
        if (input) {
            QFont f = colors.fonts.getUIFont(theme.scaledFontSize(BASE_BOX_INPUT_FONT));
            f.setWeight(QFont::Normal);
            input->setFont(f);
            input->updateGeometry();
        }

        QVBoxLayout* cl = qobject_cast<QVBoxLayout*>(m_inputContainer->layout());
        if (cl)
            cl->setContentsMargins(padH, padV, rightPad, padV);

        int innerRow = theme.scaled(28);
        if ((m_type == FieldType::Number || m_type == FieldType::Text) && m_lineEdit) {
            const QFontMetrics fm(m_lineEdit->font());
            // Metrics-based height avoids clipping. QLineEdit::sizeHint() is often inflated on
            // Windows (huge “empty” box) — only trust it when close to font metrics.
            const int fromMetrics = fm.height() + theme.scaled(8);
            innerRow = qMax(theme.scaled(22), fromMetrics);
            innerRow = qMax(innerRow, fm.lineSpacing() + theme.scaled(5));
            const int hint = m_lineEdit->sizeHint().height();
            const int hintCeil = fromMetrics + theme.scaled(8);
            if (hint <= hintCeil)
                innerRow = qMax(innerRow, hint);
            m_lineEdit->setFixedHeight(innerRow);
        } else if (m_comboBox) {
            const QFontMetrics fm(m_comboBox->font());
            innerRow = qMax(theme.scaled(28), m_comboBox->sizeHint().height());
            innerRow = qMax(innerRow, fm.lineSpacing() + theme.scaled(6));
            innerRow = qBound(theme.scaled(26), innerRow, theme.scaled(52));
            m_comboBox->setFixedHeight(innerRow);
        }

        const int boxInnerH = padV * 2 + innerRow;
        m_inputContainer->setFixedHeight(boxInnerH);

        if (m_label && m_label->isVisible())
            m_label->adjustSize();

        if (QVBoxLayout* outer = qobject_cast<QVBoxLayout*>(layout())) {
            outer->invalidate();
            outer->activate();
            setFixedHeight(outer->minimumSize().height());
        } else {
            int totalH = boxInnerH;
            if (m_label && m_label->isVisible())
                totalH += theme.scaled(BASE_LABEL_GAP) + m_label->sizeHint().height();
            setFixedHeight(totalH);
        }
    }
}

QWidget* StyledInputField::inputWidget() const
{
    if (m_comboBox)
        return m_comboBox;
    if (m_lineEdit)
        return m_lineEdit;
    return nullptr;
}

int StyledInputField::boxedContentTopInset() const
{
    if (!m_inputContainer)
        return 0;
    if (!m_label || !m_label->isVisible())
        return 0;
    auto* outer = qobject_cast<QVBoxLayout*>(layout());
    if (!outer)
        return 0;
    int lh = qMax(m_label->height(), m_label->sizeHint().height());
    if (lh <= 0) {
        const QFontMetrics fm(m_label->font());
        lh = fm.height();
    }
    return lh + outer->spacing();
}

int StyledInputField::boxedInputHeight() const
{
    if (!m_inputContainer)
        return 0;
    int h = m_inputContainer->height();
    if (h <= 0)
        h = m_inputContainer->minimumHeight();
    if (h <= 0)
        h = m_inputContainer->maximumHeight();
    return h;
}

int StyledInputField::boxedInputTopY() const
{
    return m_inputContainer ? m_inputContainer->y() : 0;
}

void StyledInputField::clearInputFocus()
{
    QWidget* input = inputWidget();
    if (input && input->hasFocus())
        input->clearFocus();
}

// ============================================================================
// Events
// ============================================================================

bool StyledInputField::eventFilter(QObject* watched, QEvent* event)
{
    if (!isEnabled()) {
        return QWidget::eventFilter(watched, event);
    }

    QWidget* input = inputWidget();
    if (watched == input) {
        switch (event->type()) {
        case QEvent::FocusIn:
            startFocusAnimation(true);
            break;
        case QEvent::FocusOut:
            startFocusAnimation(false);
            break;
        case QEvent::Enter:
            startHoverAnimation(true);
            break;
        case QEvent::Leave:
            startHoverAnimation(false);
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void StyledInputField::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::EnabledChange) {
        m_hoverAnimation->stop();
        m_focusAnimation->stop();
        setHoverProgress(0.0);
        setFocusProgress(0.0);
        updateThemeColors();
        update();
    }
    QWidget::changeEvent(event);
}

void StyledInputField::startFocusAnimation(bool focused)
{
    if (!isEnabled()) {
        m_focusAnimation->stop();
        setFocusProgress(0.0);
        return;
    }
    m_focusAnimation->stop();
    m_focusAnimation->setStartValue(m_focusProgress);
    m_focusAnimation->setEndValue(focused ? 1.0 : 0.0);
    m_focusAnimation->start();
}

void StyledInputField::startHoverAnimation(bool hovered)
{
    if (!isEnabled()) {
        m_hoverAnimation->stop();
        setHoverProgress(0.0);
        return;
    }
    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(hovered ? QEasingCurve::OutCubic : QEasingCurve::InOutCubic);
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

void StyledInputField::setFocusProgress(qreal progress)
{
    m_focusProgress = progress;
    update();
}

void StyledInputField::setHoverProgress(qreal progress)
{
    m_hoverProgress = progress;
    update();
}

// ============================================================================
// Paint
// ============================================================================

void StyledInputField::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (!m_inputContainer)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int borderRadius = theme.scaled(BASE_BORDER_RADIUS);
    const bool fieldEnabled = isEnabled();

    QRectF rect = QRectF(m_inputContainer->geometry()).adjusted(0.5, 0.5, -0.5, -0.5);

    // overlayBase() is very low-alpha; a filled primary “glow” underneath would show through and
    // look like a dirty tint when focused but not hovered — rely on border for focus instead.
    if (fieldEnabled) {
        painter.setBrush(colors.overlayBase());
    } else {
        QColor dimFill = colors.surface;
        dimFill.setAlpha(colors.isDark ? 72 : 110);
        painter.setBrush(dimFill);
    }
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect, borderRadius, borderRadius);

    if (fieldEnabled && m_hoverProgress > 0) {
        QColor hoverBg = colors.surfaceElevated();
        hoverBg.setAlpha(int(m_hoverProgress * 255));
        painter.setBrush(hoverBg);
        painter.drawRoundedRect(rect, borderRadius, borderRadius);
    }

    QColor borderRest = fieldEnabled
        ? ruwa::ui::core::ThemeColors::interpolate(
              colors.borderSubtle(), colors.borderSubtleHover(), m_hoverProgress)
        : ruwa::ui::core::ThemeColors::withAlpha(colors.borderSubtle(), colors.isDark ? 14 : 22);
    QColor borderFocus = colors.primary;
    QColor borderColor = fieldEnabled
        ? ruwa::ui::core::ThemeColors::interpolate(borderRest, borderFocus, m_focusProgress)
        : borderRest;

    QPainterPath borderPath;
    QRectF borderRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    borderPath.addRoundedRect(borderRect, borderRadius - 0.5, borderRadius - 0.5);

    QPen borderPen(borderColor, 1.0 + (fieldEnabled ? m_focusProgress * 0.5 : 0.0));
    borderPen.setCosmetic(true);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);

    if (m_comboBox) {
        painter.save();
        QColor arrowColor = fieldEnabled ? colors.textMuted : colors.textDisabled();
        if (fieldEnabled)
            arrowColor.setAlpha(int(180 + m_hoverProgress * 75));
        painter.setPen(QPen(arrowColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        const int arrowOffset = theme.scaled(BASE_ARROW_OFFSET);
        const int arrowSize = theme.scaled(BASE_ARROW_SIZE);
        qreal arrowX = rect.right() - arrowOffset;
        qreal arrowY = rect.center().y();

        QPainterPath arrowPath;
        arrowPath.moveTo(arrowX - arrowSize, arrowY - arrowSize * 0.5);
        arrowPath.lineTo(arrowX, arrowY + arrowSize * 0.5);
        arrowPath.lineTo(arrowX + arrowSize, arrowY - arrowSize * 0.5);
        painter.drawPath(arrowPath);
        painter.restore();
    }
}

// ============================================================================
// Public API
// ============================================================================

void StyledInputField::setText(const QString& text)
{
    if (m_lineEdit)
        m_lineEdit->setText(text);
}

QString StyledInputField::text() const
{
    return m_lineEdit ? m_lineEdit->text() : QString();
}

void StyledInputField::setValue(int value)
{
    if (m_type == FieldType::Number && m_lineEdit) {
        const QSignalBlocker blocker(m_lineEdit);
        m_lineEdit->setText(QString::number(value));
    }
}

int StyledInputField::value() const
{
    if (m_type == FieldType::Number && m_lineEdit) {
        bool ok = false;
        const int v = m_lineEdit->text().toInt(&ok);
        if (ok)
            return v;
        return m_intMin;
    }
    return 0;
}

void StyledInputField::addItem(const QString& text, const QVariant& userData)
{
    if (m_comboBox)
        m_comboBox->addItem(text, userData);
}

void StyledInputField::addItems(const QStringList& texts)
{
    if (m_comboBox)
        m_comboBox->addItems(texts);
}

void StyledInputField::clear()
{
    if (m_comboBox)
        m_comboBox->clear();
}

QString StyledInputField::currentText() const
{
    return m_comboBox ? m_comboBox->currentText() : QString();
}

int StyledInputField::currentIndex() const
{
    return m_comboBox ? m_comboBox->currentIndex() : -1;
}

void StyledInputField::setCurrentIndex(int index)
{
    if (m_comboBox)
        m_comboBox->setCurrentIndex(index);
}

void StyledInputField::setPlaceholder(const QString& placeholder)
{
    if (m_lineEdit)
        m_lineEdit->setPlaceholderText(placeholder);
}

void StyledInputField::setMaxLength(int maxLength)
{
    if (m_lineEdit)
        m_lineEdit->setMaxLength(maxLength);
}

void StyledInputField::setLabel(const QString& label)
{
    m_labelText = label;
    if (m_label) {
        const QString t = boxedLabelText(label);
        m_label->setText(t);
        m_label->setVisible(!t.isEmpty());
        updateScaledSizes();
    }
    update();
}

void StyledInputField::setRange(int min, int max)
{
    m_intMin = min;
    m_intMax = max;
    if (m_type == FieldType::Number && m_lineEdit) {
        auto* mutValidator = const_cast<QValidator*>(m_lineEdit->validator());
        if (auto* v = qobject_cast<QIntValidator*>(mutValidator))
            v->setRange(min, max);
        else
            m_lineEdit->setValidator(new QIntValidator(min, max, m_lineEdit));
    }
}

// ============================================================================
// Theme
// ============================================================================

void StyledInputField::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const bool en = isEnabled();

    const QString textCol
        = en ? colors.text.name(QColor::HexArgb) : colors.textDisabled().name(QColor::HexArgb);
    const QString mutedCol
        = en ? colors.textMuted.name(QColor::HexArgb) : colors.textDisabled().name(QColor::HexArgb);

    QString transparentInputStyle = QString(R"(
        QLineEdit, QComboBox {
            background-color: transparent;
            color: %1;
            border: none;
            padding: 0px;
        }
        QComboBox::drop-down {
            border: none;
        }
        QComboBox::down-arrow {
            image: none;
            width: 0px;
            height: 0px;
        }
        QComboBox QAbstractItemView {
            background-color: %2;
            color: %1;
            border: 1px solid %3;
            border-radius: 6px;
            selection-background-color: %3;
            padding: 4px;
        }
    )")
                                        .arg(textCol)
                                        .arg(colors.surface.name())
                                        .arg(colors.overlay(0.06).name(QColor::HexArgb));

    if (m_lineEdit) {
        if (m_inputContainer) {
            const QString boxedLineStyle = QString(R"(
                QLineEdit {
                    background-color: transparent;
                    color: %1;
                    border: none;
                    padding: 0px;
                }
                QLineEdit::placeholder {
                    color: %2;
                    font-style: italic;
                }
            )")
                                               .arg(textCol, mutedCol);
            m_lineEdit->setStyleSheet(boxedLineStyle);
        } else {
            m_lineEdit->setStyleSheet(transparentInputStyle);
        }
    }
    if (m_comboBox)
        m_comboBox->setStyleSheet(transparentInputStyle);

    if (m_label) {
        QString sheet = QString("QLabel { color: %1; background: transparent; }").arg(mutedCol);
        m_label->setStyleSheet(sheet);
    }

    update();
}

void StyledInputField::onThemeChanged()
{
    updateThemeColors();
    updateScaledSizes();
}

} // namespace ruwa::ui::widgets
