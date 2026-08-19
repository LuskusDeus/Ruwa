// SPDX-License-Identifier: MPL-2.0

// StyledInputField.cpp
#include "StyledInputField.h"
#include "shared/style/AnimationPolicy.h"
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
#include <QLocale>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

namespace {
/// Authored durations; the animation policy scales them at each transition.
constexpr int kFocusAnimationMs = 250;
constexpr int kHoverAnimationMs = 200;
} // namespace

namespace {
// Reference: project-panel.html — .field (boxed)
const int BASE_LABEL_GAP = 6;
const int BASE_BOX_PAD_V = 10;
const int BASE_BOX_PAD_V_NUMBER = 7; ///< Tighter box for compact numeric fields (New Project, etc.)
const int BASE_BOX_PAD_H = 14;
const int BASE_BORDER_RADIUS = 8;
// Compact density: the box hugs the text rather than framing it, for fields that
// live in a toolbar or panel header instead of a form.
const int BASE_COMPACT_PAD_V = 3;
const int BASE_COMPACT_PAD_H = 8;
const int BASE_COMPACT_BORDER_RADIUS = 6;
/// Extra height added to the font's own line box, per density.
const int BASE_ROW_EXTRA = 8;
const int BASE_COMPACT_ROW_EXTRA = 2;
/// Floor under the editable row, per density.
const int BASE_ROW_MIN = 22;
const int BASE_COMPACT_ROW_MIN = 16;
// Leading glyph inside the box (see setLeadingIcon).
const int BASE_LEADING_ICON = 14;
const int BASE_LEADING_GAP = 6;
const int BASE_ARROW_SIZE = 4;
const int BASE_ARROW_OFFSET = 14;

QString boxedLabelText(const QString& raw)
{
    const QString t = raw.trimmed();
    return t.isEmpty() ? QString() : t.toUpper();
}

// ----------------------------------------------------------------------------
// Number text
//
// QIntValidator reads its input through the locale, so with the default number
// options it happily accepts a group separator — "3 000" pasted from a browser,
// Excel or the Windows calculator (U+00A0 in Russian, "," in English). The value
// was then read back with QString::toInt(), which is C-locale and rejects that
// same string: the field showed a number nobody could parse, no valueChanged was
// emitted, and every consumer (thumbnail, memory hint, aspect lock) kept the old
// size while the document was created from the fallback minimum.
//
// The text is therefore normalised to bare digits before anything looks at it,
// and the validator is handed a locale that rejects separators outright.
// ----------------------------------------------------------------------------

/// Strip everything a locale may legally sprinkle between digits: any kind of
/// space (U+00A0 and the narrow spaces included, QChar::isSpace covers them),
/// the apostrophe groupings, and the locale's own group separator. When
/// \a cursorPos is given it is moved along with the text it points into.
QString normalizeIntegerText(const QString& raw, const QLocale& locale, int* cursorPos = nullptr)
{
    const QString group = locale.groupSeparator();
    const QChar groupChar = (group.size() == 1) ? group.at(0) : QChar();

    const int cursor = cursorPos ? *cursorPos : 0;
    int movedCursor = cursor;

    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        const QChar ch = raw.at(i);
        const bool separator = ch.isSpace() || ch == u'\''
            || ch == u'’' // right single quotation mark, de-CH grouping
            || (!groupChar.isNull() && ch == groupChar);
        if (separator) {
            if (i < cursor)
                --movedCursor;
            continue;
        }
        out.append(ch);
    }

    if (cursorPos)
        *cursorPos = qBound(0, movedCursor, out.size());
    return out;
}

/// Read \a raw as an integer the same way the validator accepted it. Tries the
/// field locale first (so localized digits still work), then the C locale.
int parseIntegerText(const QString& raw, const QLocale& locale, bool* ok)
{
    const QString normalized = normalizeIntegerText(raw, locale);
    if (normalized.isEmpty()) {
        if (ok)
            *ok = false;
        return 0;
    }

    bool parsed = false;
    int value = locale.toInt(normalized, &parsed);
    if (!parsed)
        value = normalized.toInt(&parsed);

    if (ok)
        *ok = parsed;
    return parsed ? value : 0;
}

/// Locale used for number fields: the system one, minus the group separator that
/// made a pasted "3 000" pass validation and then fail to parse.
QLocale strictNumberLocale()
{
    QLocale locale;
    locale.setNumberOptions(locale.numberOptions() | QLocale::RejectGroupSeparator);
    return locale;
}

/// QIntValidator that rewrites separators out of the input before judging it.
/// QLineEdit adopts the string the validator hands back, so a pasted "3 000"
/// lands in the field as "3000" instead of being silently dropped whole.
class IntegerFieldValidator : public QIntValidator {
public:
    IntegerFieldValidator(int minimum, int maximum, QObject* parent)
        : QIntValidator(minimum, maximum, parent)
    {
        setLocale(strictNumberLocale());
    }

    QValidator::State validate(QString& input, int& pos) const override
    {
        QString normalized = normalizeIntegerText(input, locale(), &pos);
        if (normalized != input)
            input = normalized;
        return QIntValidator::validate(input, pos);
    }
};
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
    m_focusAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress");
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
                m_numberLocale = strictNumberLocale();
                m_lineEdit->setValidator(new IntegerFieldValidator(m_intMin, m_intMax, m_lineEdit));
                m_lastValidValue = m_intMin;
                connect(m_lineEdit, &QLineEdit::textChanged, this, [this](const QString& t) {
                    bool ok = false;
                    const int v = parseIntegerText(t, m_numberLocale, &ok);
                    if (!ok)
                        return; // half-typed or unparsable: commitNumericFixup settles it
                    const int clamped = qBound(m_intMin, v, m_intMax);
                    m_lastValidValue = clamped;
                    emit valueChanged(clamped);
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

// ============================================================================
// Density / leading icon
// ============================================================================

int StyledInputField::boxPaddingV() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    if (m_density == Density::Compact) {
        return theme.scaled(BASE_COMPACT_PAD_V);
    }
    return (m_type == FieldType::Number || m_type == FieldType::Text)
        ? theme.scaled(BASE_BOX_PAD_V_NUMBER)
        : theme.scaled(BASE_BOX_PAD_V);
}

int StyledInputField::boxPaddingH() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    return theme.scaled(m_density == Density::Compact ? BASE_COMPACT_PAD_H : BASE_BOX_PAD_H);
}

int StyledInputField::boxBorderRadius() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    return theme.scaled(
        m_density == Density::Compact ? BASE_COMPACT_BORDER_RADIUS : BASE_BORDER_RADIUS);
}

int StyledInputField::leadingSlotWidth() const
{
    // A glyph that did not resolve (icon file not shipped yet) reserves nothing:
    // an empty slot would only push the text off-centre.
    if (!m_hasLeadingIcon || m_leadingPixmap.isNull()) {
        return 0;
    }
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    return theme.scaled(BASE_LEADING_ICON) + theme.scaled(BASE_LEADING_GAP);
}

void StyledInputField::setDensity(Density density)
{
    if (m_density == density) {
        return;
    }
    m_density = density;
    updateScaledSizes();
    update();
}

void StyledInputField::setLeadingIcon(ruwa::ui::core::IconProvider::StandardIcon icon)
{
    if (m_hasLeadingIcon && m_leadingIconType == icon) {
        return;
    }
    // Only the appearance or disappearance of the slot moves the text; swapping
    // one resolved glyph for another leaves the geometry alone.
    const int slotBefore = leadingSlotWidth();
    m_hasLeadingIcon = true;
    m_leadingIconType = icon;
    rebuildLeadingPixmap();
    if (leadingSlotWidth() != slotBefore) {
        updateScaledSizes();
    }
    update();
}

void StyledInputField::clearLeadingIcon()
{
    if (!m_hasLeadingIcon) {
        return;
    }
    const int slotBefore = leadingSlotWidth();
    m_hasLeadingIcon = false;
    m_leadingPixmap = QPixmap();
    if (slotBefore != 0) {
        updateScaledSizes();
    }
    update();
}

void StyledInputField::rebuildLeadingPixmap()
{
    m_leadingPixmap = QPixmap();
    if (!m_hasLeadingIcon) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int size = theme.scaled(BASE_LEADING_ICON);
    const QPixmap source
        = ruwa::ui::core::IconProvider::instance().getPixmap(m_leadingIconType, QSize(size, size));
    if (source.isNull()) {
        return;
    }

    QPixmap tinted(source.size());
    tinted.fill(Qt::transparent);
    QPainter p(&tinted);
    p.drawPixmap(0, 0, source);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(tinted.rect(), theme.colors().textMuted);
    p.end();
    m_leadingPixmap = tinted;
}

void StyledInputField::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if ((m_type == FieldType::Text || m_type == FieldType::Number || m_type == FieldType::Dropdown)
        && m_inputContainer) {
        if (m_label) {
            QFont lf = theme.font(ruwa::ui::core::ThemeFontRole::Body);
            lf.setWeight(QFont::Normal);
            lf.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.5));
            m_label->setFont(lf);
        }

        if (QVBoxLayout* outer = qobject_cast<QVBoxLayout*>(layout()))
            outer->setSpacing(theme.scaled(BASE_LABEL_GAP));

        const int padV = boxPaddingV();
        const int padH = boxPaddingH();
        // The glyph is carved out of the left padding, so the text starts after it.
        const int leftPad = padH + leadingSlotWidth();
        const int rightPad
            = m_comboBox ? padH + theme.scaled(BASE_ARROW_OFFSET + BASE_ARROW_SIZE + 2) : padH;

        QWidget* input = inputWidget();
        if (input) {
            QFont f = theme.font(ruwa::ui::core::ThemeFontRole::Label);
            f.setWeight(QFont::Normal);
            input->setFont(f);
            input->updateGeometry();
        }

        QVBoxLayout* cl = qobject_cast<QVBoxLayout*>(m_inputContainer->layout());
        if (cl)
            cl->setContentsMargins(leftPad, padV, rightPad, padV);

        const bool compact = (m_density == Density::Compact);
        int innerRow = theme.scaled(28);
        if ((m_type == FieldType::Number || m_type == FieldType::Text) && m_lineEdit) {
            const QFontMetrics fm(m_lineEdit->font());
            // Metrics-based height avoids clipping. QLineEdit::sizeHint() is often inflated on
            // Windows (huge “empty” box) — only trust it when close to font metrics.
            const int fromMetrics
                = fm.height() + theme.scaled(compact ? BASE_COMPACT_ROW_EXTRA : BASE_ROW_EXTRA);
            innerRow
                = qMax(theme.scaled(compact ? BASE_COMPACT_ROW_MIN : BASE_ROW_MIN), fromMetrics);
            innerRow = qMax(innerRow, fm.lineSpacing() + theme.scaled(compact ? 1 : 5));
            // The inflated sizeHint is exactly what a compact field must not
            // inherit — at this density font metrics are the whole budget.
            if (!compact) {
                const int hint = m_lineEdit->sizeHint().height();
                const int hintCeil = fromMetrics + theme.scaled(8);
                if (hint <= hintCeil)
                    innerRow = qMax(innerRow, hint);
            }
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
            // Settle the number before anyone reads it: editingFinished is no use
            // here, QLineEdit withholds it while the validator says Intermediate,
            // which is exactly the empty / out-of-range state that needs fixing.
            commitNumericFixup();
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
    m_focusAnimation->setDuration(anim::duration(kFocusAnimationMs));
    anim::start(m_focusAnimation);
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
    m_hoverAnimation->setDuration(anim::duration(kHoverAnimationMs));
    anim::start(m_hoverAnimation);
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
    const int borderRadius = boxBorderRadius();
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

    if (m_hasLeadingIcon && !m_leadingPixmap.isNull()) {
        // Sits in the slot carved out of the left padding by updateScaledSizes().
        const int slot = theme.scaled(BASE_LEADING_ICON);
        const QRectF iconRect(
            rect.left() + boxPaddingH(), rect.center().y() - slot / 2.0, slot, slot);
        painter.save();
        painter.setOpacity(fieldEnabled ? 1.0 : 0.45);
        painter.drawPixmap(iconRect.toRect(), m_leadingPixmap);
        painter.restore();
    }

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
        const int clamped = qBound(m_intMin, value, m_intMax);
        m_lastValidValue = clamped;
        const QSignalBlocker blocker(m_lineEdit);
        m_lineEdit->setText(QString::number(clamped));
    }
}

int StyledInputField::value() const
{
    if (m_type == FieldType::Number && m_lineEdit) {
        bool ok = false;
        const int v = parseIntegerText(m_lineEdit->text(), m_numberLocale, &ok);
        // Falling back to m_intMin here used to hand a 1-pixel canvas to whoever
        // asked while the field still showed a plausible number. The last value
        // that actually validated is the only honest answer.
        return ok ? qBound(m_intMin, v, m_intMax) : m_lastValidValue;
    }
    return 0;
}

bool StyledInputField::hasValidValue() const
{
    if (m_type != FieldType::Number || !m_lineEdit)
        return false;
    bool ok = false;
    const int v = parseIntegerText(m_lineEdit->text(), m_numberLocale, &ok);
    return ok && v >= m_intMin && v <= m_intMax;
}

void StyledInputField::commitNumericFixup()
{
    if (m_type != FieldType::Number || !m_lineEdit)
        return;

    bool ok = false;
    const int parsed = parseIntegerText(m_lineEdit->text(), m_numberLocale, &ok);
    const int resolved = ok ? qBound(m_intMin, parsed, m_intMax) : m_lastValidValue;

    const bool valueMoved = (resolved != m_lastValidValue);
    m_lastValidValue = resolved;

    const QString canonical = QString::number(resolved);
    if (m_lineEdit->text() != canonical) {
        const QSignalBlocker blocker(m_lineEdit);
        m_lineEdit->setText(canonical);
    }

    if (valueMoved)
        emit valueChanged(resolved);
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
    m_lastValidValue = qBound(min, m_lastValidValue, max);
    if (m_type == FieldType::Number && m_lineEdit) {
        auto* mutValidator = const_cast<QValidator*>(m_lineEdit->validator());
        if (auto* v = qobject_cast<QIntValidator*>(mutValidator))
            v->setRange(min, max);
        else
            m_lineEdit->setValidator(new IntegerFieldValidator(min, max, m_lineEdit));
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
    rebuildLeadingPixmap(); // tint and scale both follow the theme
    updateScaledSizes();
}

} // namespace ruwa::ui::widgets
