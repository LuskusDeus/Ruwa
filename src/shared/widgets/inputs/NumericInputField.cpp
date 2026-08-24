// SPDX-License-Identifier: MPL-2.0

// NumericInputField.cpp
#include "NumericInputField.h"
#include "shared/style/AnimationPolicy.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QCursor>
#include <QDoubleValidator>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QWindow>

#include <cmath>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

namespace {
/// Authored hover fade; the animation policy scales it at each transition.
constexpr int kHoverAnimationMs = 180;
} // namespace

NumericInputField::NumericInputField(QWidget* parent)
    : QLineEdit(parent)
{
    setAttribute(Qt::WA_Hover);
    setFrame(false);
    setAlignment(Qt::AlignCenter);

    m_validator = new QDoubleValidator(m_minimum, m_maximum, m_decimals, this);
    m_validator->setNotation(QDoubleValidator::StandardNotation);
    setValidator(m_validator);

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &NumericInputField::onThemeChanged);
    connect(this, &QLineEdit::textEdited, this, &NumericInputField::onTextEdited);
    connect(this, &QLineEdit::editingFinished, this, &NumericInputField::onEditingFinished);

    applyPalette();
    updateMargins();
    updateScrubCursor();
    setText(formatValue(m_value));
}

NumericInputField::~NumericInputField() = default;

void NumericInputField::setRange(double minimum, double maximum)
{
    if (minimum > maximum) {
        std::swap(minimum, maximum);
    }
    m_minimum = minimum;
    m_maximum = maximum;
    m_validator->setRange(m_minimum, m_maximum, m_decimals);
    applyValue(m_value, /*reformatText=*/true);
}

void NumericInputField::setSingleStep(double step)
{
    m_step = step > 0.0 ? step : 1.0;
}

void NumericInputField::setDecimals(int decimals)
{
    m_decimals = qMax(0, decimals);
    m_validator->setDecimals(m_decimals);
    applyValue(m_value, /*reformatText=*/true);
}

void NumericInputField::setValue(double value)
{
    applyValue(value, /*reformatText=*/true);
}

void NumericInputField::setSuffix(const QString& suffix)
{
    if (m_suffix == suffix) {
        return;
    }
    m_suffix = suffix;
    updateMargins();
    update();
}

void NumericInputField::setPrefix(const QString& prefix)
{
    if (m_prefix == prefix) {
        return;
    }
    m_prefix = prefix;
    updateMargins();
    update();
}

void NumericInputField::setHoverProgress(qreal p)
{
    m_hoverProgress = qBound(0.0, p, 1.0);
    update();
}

QSize NumericInputField::sizeHint() const
{
    const QSize base = QLineEdit::sizeHint();
    return QSize(base.width(), ruwa::ui::core::ThemeManager::instance().scaled(BaseHeight));
}

void NumericInputField::applyValue(double value, bool reformatText)
{
    const double clamped = qBound(m_minimum, value, m_maximum);
    const double scale = std::pow(10.0, m_decimals);
    const double rounded = std::round(clamped * scale) / scale;

    const bool changed = !qFuzzyCompare(m_value + 1.0, rounded + 1.0);
    m_value = rounded;

    if (reformatText) {
        const QSignalBlocker blocker(this);
        setText(formatValue(m_value));
    }
    if (changed) {
        emit valueChanged(m_value);
    }
}

QString NumericInputField::formatValue(double value) const
{
    return QString::number(value, 'f', m_decimals);
}

void NumericInputField::onTextEdited(const QString& text)
{
    bool ok = false;
    const double parsed = text.toDouble(&ok);
    if (!ok) {
        return; // not a complete number yet ("-", "", "1.") — wait for more input
    }
    applyValue(parsed, /*reformatText=*/false);
}

void NumericInputField::nudge(double delta)
{
    applyValue(m_value + delta, /*reformatText=*/true);
}

double NumericInputField::stepMultiplier(Qt::KeyboardModifiers modifiers)
{
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        return 10.0;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        return 0.1;
    }
    return 1.0;
}

bool NumericInputField::isRelativePointerEvent(const QMouseEvent* event)
{
    if (!event) {
        return false;
    }
    if (event->source() != Qt::MouseEventNotSynthesized) {
        return false; // synthesized from a tablet or touch contact
    }
    const auto deviceType = event->deviceType();
    return deviceType == QInputDevice::DeviceType::Mouse
        || deviceType == QInputDevice::DeviceType::TouchPad
        || deviceType == QInputDevice::DeviceType::Unknown;
}

QRect NumericInputField::scrubWrapBounds() const
{
    // The window is the band the pointer wraps inside, but only the part of it
    // that is actually on screen: warping into a half-offscreen window would
    // put the pointer somewhere the compositor immediately clamps back.
    QRect bounds;
    if (const QWidget* top = window()) {
        bounds = top->frameGeometry();
    }

    const QScreen* screen = nullptr;
    if (const QWidget* top = window(); top && top->windowHandle()) {
        screen = top->windowHandle()->screen();
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(bounds.isNull() ? QCursor::pos() : bounds.center());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect available = screen->geometry();
        bounds = bounds.isNull() ? available : bounds.intersected(available);
    }
    return bounds;
}

bool NumericInputField::wrapScrubPointer(int globalX, int globalY)
{
    if (!m_scrubbing || m_scrubWrapBounds.width() <= 4 * scrubWrapInset()) {
        return false; // too narrow to wrap inside without fighting itself
    }

    const int inset = scrubWrapInset();
    const int left = m_scrubWrapBounds.left();
    const int right = m_scrubWrapBounds.right();

    int wrappedX = globalX;
    if (globalX <= left) {
        wrappedX = right - inset;
    } else if (globalX >= right) {
        wrappedX = left + inset;
    } else {
        return false;
    }

    // The anchor moves with the pointer, so the teleport itself contributes no
    // delta: the value keeps counting from exactly where the edge left it.
    m_scrubLastGlobalX = wrappedX;
    QCursor::setPos(wrappedX, qBound(m_scrubWrapBounds.top(), globalY, m_scrubWrapBounds.bottom()));
    return true;
}

int NumericInputField::scrubWrapInset()
{
    return qMax(1, ruwa::ui::core::ThemeManager::instance().scaled(BaseScrubWrapInset));
}

void NumericInputField::beginScrub(int anchorGlobalX)
{
    m_scrubbing = true;
    m_scrubLastGlobalX = anchorGlobalX;
    m_scrubWrapBounds = scrubWrapBounds();
    // The drag owns the value from here on, so it starts from what the field
    // currently shows rather than from a stale accumulator.
    m_scrubValue = m_value;
    deselect();
    if (m_pressTookFocus) {
        // The press pulled focus in before we could tell it was a drag. Hand it
        // back, so the caret stops blinking mid-scrub and the field is left in
        // the state a scrub should leave it: showing a value, not editing one.
        m_pressTookFocus = false;
        clearFocus();
    }
    setCursor(Qt::SizeHorCursor);
    emit scrubbingChanged(true);
}

void NumericInputField::endScrub()
{
    m_scrubbing = false;
    applyValue(m_value, /*reformatText=*/true);
    updateScrubCursor();
    emit scrubbingChanged(false);
    // One drag is one edit: listeners that commit on editingFinished (an undo
    // step, a pixel move) get a single commit for the whole gesture rather than
    // one per mouse move.
    emit editingFinished();
}

void NumericInputField::updateScrubCursor()
{
    const bool editable = isEnabled() && !isReadOnly();
    setCursor(editable && !hasFocus() ? Qt::SizeHorCursor : Qt::IBeamCursor);
}

void NumericInputField::mousePressEvent(QMouseEvent* event)
{
    // Qt focuses a click-focusable widget from QWidgetWindow, *before* the
    // press reaches it, so hasFocus() is already true here even on the very
    // first click. The gesture therefore keys off who owned the focus a moment
    // ago: a field that was already being text-edited keeps "drag = select
    // characters", a field the click just woke up offers the scrub.
    const bool wasEditing = hasFocus() && !m_focusFromPress;
    const bool tookFocus = m_focusFromPress;
    m_focusFromPress = false;

    if (event->button() == Qt::LeftButton && !wasEditing && isEnabled() && !isReadOnly()) {
        m_scrubArmed = true;
        m_scrubbing = false;
        m_pressTookFocus = tookFocus;
        m_pressPos = event->position().toPoint();
        m_scrubLastGlobalX = event->globalPosition().toPoint().x();
        m_scrubValue = m_value;
        event->accept();
        return;
    }
    QLineEdit::mousePressEvent(event);
}

void NumericInputField::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_scrubArmed) {
        QLineEdit::mouseMoveEvent(event);
        return;
    }

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int x = event->position().toPoint().x();
    const QPoint globalPos = event->globalPosition().toPoint();

    if (!m_scrubbing) {
        const int threshold = qMax(1, theme.scaled(BaseScrubThreshold));
        const int travel = x - m_pressPos.x();
        if (qAbs(travel) < threshold) {
            event->accept();
            return;
        }
        // Anchor on the point where the threshold was crossed, so crossing it
        // does not hand the value a free jump of one dead zone's worth.
        beginScrub(globalPos.x() - (travel > 0 ? travel - threshold : travel + threshold));
    }

    const double pixelsPerStep = qMax(1, theme.scaled(BaseScrubPixelsPerStep));
    const double travelled = static_cast<double>(globalPos.x() - m_scrubLastGlobalX);
    // A move event queued before a wrap can still arrive after it, carrying the
    // whole width of the window as one jump. Nothing a hand can do produces
    // that, so it is read as the leftover of the teleport and dropped.
    const bool wrapArtifact
        = m_scrubWrapBounds.width() > 0 && qAbs(travelled) > m_scrubWrapBounds.width() / 2.0;
    const double delta = wrapArtifact
        ? 0.0
        : travelled / pixelsPerStep * m_step * stepMultiplier(event->modifiers());
    m_scrubLastGlobalX = globalPos.x();
    // Done after the delta is taken, so the pixel that reached the edge still
    // counts before the pointer is put down on the far side. An absolute device
    // is left alone: a pen reports where it physically is, so moving the cursor
    // out from under it would be undone by its very next packet.
    if (isRelativePointerEvent(event)) {
        wrapScrubPointer(globalPos.x(), globalPos.y());
    }
    // Clamped as it accumulates: dragging past an end must not build up slack
    // that the pointer has to travel back through before the value responds.
    m_scrubValue = qBound(m_minimum, m_scrubValue + delta, m_maximum);
    applyValue(m_scrubValue, /*reformatText=*/true);
    event->accept();
}

void NumericInputField::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_scrubArmed && event->button() == Qt::LeftButton) {
        const bool wasScrubbing = m_scrubbing;
        m_scrubArmed = false;
        m_pressTookFocus = false;
        if (wasScrubbing) {
            endScrub();
        } else {
            // A press that never travelled reads as "I want to type in here".
            setFocus(Qt::MouseFocusReason);
            // The click is spent: from now on this field is being edited, so
            // the next press must not read as a fresh scrub.
            m_focusFromPress = false;
            selectAll();
        }
        event->accept();
        return;
    }
    QLineEdit::mouseReleaseEvent(event);
}

void NumericInputField::focusInEvent(QFocusEvent* event)
{
    // Runs just before the press that caused it — see mousePressEvent.
    m_focusFromPress = event->reason() == Qt::MouseFocusReason;
    QLineEdit::focusInEvent(event);
    updateScrubCursor();
}

void NumericInputField::focusOutEvent(QFocusEvent* event)
{
    // Whether QLineEdit emitted editingFinished on the way out depends on the
    // validator: an empty or half-typed field ("", "-", "1.") is not acceptable
    // input, so it emits nothing and the field is left showing the fragment.
    const bool wasReported = hasAcceptableInput();
    QLineEdit::focusOutEvent(event);
    updateScrubCursor();
    if (wasReported) {
        return;
    }
    applyValue(m_value, /*reformatText=*/true);
    emit editingFinished();
}

void NumericInputField::onEditingFinished()
{
    applyValue(m_value, /*reformatText=*/true);
}

void NumericInputField::onThemeChanged()
{
    applyPalette();
    updateMargins();
    update();
}

void NumericInputField::startHoverAnimation(bool hovered)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->setDuration(anim::duration(kHoverAnimationMs));
    anim::start(m_hoverAnimation);
}

void NumericInputField::enterEvent(QEnterEvent* event)
{
    QLineEdit::enterEvent(event);
    startHoverAnimation(true);
}

void NumericInputField::leaveEvent(QEvent* event)
{
    QLineEdit::leaveEvent(event);
    startHoverAnimation(false);
}

void NumericInputField::wheelEvent(QWheelEvent* event)
{
    // Gated on focus so scrolling the panel this field sits in never nudges
    // its value by accident — the user must click in first.
    if (!hasFocus()) {
        QLineEdit::wheelEvent(event);
        return;
    }
    const double dir = event->angleDelta().y() > 0 ? 1.0 : -1.0;
    nudge(dir * m_step * stepMultiplier(event->modifiers()));
    event->accept();
}

void NumericInputField::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        const double dir = event->key() == Qt::Key_Up ? 1.0 : -1.0;
        nudge(dir * m_step * stepMultiplier(event->modifiers()));
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void NumericInputField::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    updateMargins();
}

void NumericInputField::changeEvent(QEvent* event)
{
    QLineEdit::changeEvent(event);
    // A field turned read-only or disabled must stop advertising a gesture it
    // no longer offers.
    if (event->type() == QEvent::EnabledChange || event->type() == QEvent::ReadOnlyChange) {
        updateScrubCursor();
    }
}

int NumericInputField::suffixSlotWidth() const
{
    if (m_suffix.isEmpty()) {
        return 0;
    }
    const QFontMetrics fm(font());
    return fm.horizontalAdvance(m_suffix)
        + ruwa::ui::core::ThemeManager::instance().scaled(BaseSuffixGap);
}

int NumericInputField::prefixSlotWidth() const
{
    if (m_prefix.isEmpty()) {
        return 0;
    }
    const QFontMetrics fm(font());
    return fm.horizontalAdvance(m_prefix)
        + ruwa::ui::core::ThemeManager::instance().scaled(BaseSuffixGap);
}

void NumericInputField::updateMargins()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int side = theme.scaled(BaseSidePad);
    setTextMargins(side + prefixSlotWidth(), 0, side + suffixSlotWidth(), 0);
}

void NumericInputField::applyPalette()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Text, colors.text);
    pal.setColor(QPalette::Highlight, colors.primary);
    pal.setColor(QPalette::HighlightedText, colors.textOnPrimary());
    setPalette(pal);
    setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));
}

void NumericInputField::paintEvent(QPaintEvent* event)
{
    using TC = ruwa::ui::core::ThemeColors;
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(rect());
    const qreal pillR = qMax(0.0, r.height() * 0.5 - 0.5);
    const QRectF fillRect = r.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal fillR = qMax(0.0, pillR - 1.0);

    // Resting background (matches HexColorInput / the Color-panel hex input).
    p.setPen(Qt::NoPen);
    p.setBrush(colors.surfaceAlt);
    p.drawRoundedRect(fillRect, fillR, fillR);

    if (m_hoverProgress > 0.001) {
        QColor plate = colors.surfaceElevated();
        plate.setAlpha(qBound(0, qRound(m_hoverProgress * 90), 255));
        p.setPen(Qt::NoPen);
        p.setBrush(plate);
        p.drawRoundedRect(fillRect, fillR, fillR);
    }

    const qreal accent = qMax<qreal>(m_hoverProgress, hasFocus() ? 1.0 : 0.0);
    QColor borderTop = TC::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), accent);
    QColor borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(
        p, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);

    p.end();

    // Let QLineEdit paint the text/cursor on top.
    QLineEdit::paintEvent(event);

    if (m_prefix.isEmpty() && m_suffix.isEmpty()) {
        return;
    }

    // Both glyphs sit outside the editable text, so they are drawn over the
    // line edit rather than being part of its content.
    QPainter overlay(this);
    overlay.setRenderHint(QPainter::Antialiasing);
    overlay.setPen(TC::interpolate(colors.textMuted, colors.text, accent));
    overlay.setFont(font());
    const int side = ruwa::ui::core::ThemeManager::instance().scaled(BaseSidePad);

    if (!m_prefix.isEmpty()) {
        const QRect prefixRect(side, 0, prefixSlotWidth(), height());
        overlay.drawText(prefixRect, Qt::AlignVCenter | Qt::AlignLeft, m_prefix);
    }
    if (!m_suffix.isEmpty()) {
        const QRect suffixRect(width() - side - suffixSlotWidth(), 0, suffixSlotWidth(), height());
        overlay.drawText(suffixRect, Qt::AlignVCenter | Qt::AlignRight, m_suffix);
    }
}

} // namespace ruwa::ui::widgets
