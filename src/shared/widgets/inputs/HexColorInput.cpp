// SPDX-License-Identifier: MPL-2.0

// HexColorInput.cpp
#include "HexColorInput.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/ToolButton.h"

#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QFontMetrics>
#include <QEnterEvent>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QClipboard>
#include <QGuiApplication>
#include <QRegularExpression>

namespace ruwa::ui::widgets {

HexColorInput::HexColorInput(QWidget* parent)
    : QLineEdit(parent)
{
    setAttribute(Qt::WA_Hover);
    setFrame(false);
    setAlignment(Qt::AlignCenter);
    setMaxLength(8); // RRGGBBAA, no '#'
    setPlaceholderText("FFFFFF");
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(180);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_copyButton = new ruwa::ui::workspace::ToolButton(
        ruwa::ui::workspace::ToolButton::Mode::Action, this);
    m_copyButton->setBaseSquareSize(BaseCopyButtonSize, BaseCopyIconSize);
    m_copyButton->setIconType(ruwa::ui::core::IconProvider::StandardIcon::Duplicate);
    m_copyButton->setChromeStyle(ruwa::ui::workspace::ToolButton::ChromeStyle::PrimaryHover);
    m_copyButton->setCircularChrome(true);
    m_copyButton->setBorderVisible(false);
    m_copyButton->setMutedNormalIcon(true);
    m_copyButton->setFocusPolicy(Qt::NoFocus);
    m_copyButton->setToolTip(tr("Copy HEX color"));
    m_copyButton->setAccessibleName(tr("Copy HEX color"));
    m_copyButton->hide();

    connect(m_copyButton, &QAbstractButton::clicked, this, [this]() {
        const QString hex = hexWithHash();
        if (hex.isEmpty())
            return;
        if (auto* clipboard = QGuiApplication::clipboard())
            clipboard->setText(hex);
    });

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &HexColorInput::onThemeChanged);

    applyPalette();
    updateMargins();
}

HexColorInput::~HexColorInput() = default;

void HexColorInput::setHex(const QString& hex)
{
    const QString digits = sanitizeHexDigits(hex);
    if (text() == digits)
        return;
    const QSignalBlocker blocker(this);
    setText(digits);
}

QString HexColorInput::hexWithHash() const
{
    const QString digits = text().trimmed();
    if (digits.isEmpty())
        return QString();
    return QStringLiteral("#") + digits.toUpper();
}

void HexColorInput::setCopyButtonVisible(bool visible)
{
    if (!m_copyButton || m_copyButtonVisible == visible)
        return;

    m_copyButtonVisible = visible;
    m_copyButton->setVisible(visible);
    updateMargins();
    positionCopyButton();
}

void HexColorInput::setHoverProgress(qreal p)
{
    m_hoverProgress = qBound(0.0, p, 1.0);
    update();
}

void HexColorInput::onThemeChanged()
{
    applyPalette();
    updateMargins();
    positionCopyButton();
    update();
}

void HexColorInput::startHoverAnimation(bool hovered)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

void HexColorInput::enterEvent(QEnterEvent* event)
{
    QLineEdit::enterEvent(event);
    startHoverAnimation(true);
}

void HexColorInput::leaveEvent(QEvent* event)
{
    QLineEdit::leaveEvent(event);
    startHoverAnimation(false);
}

void HexColorInput::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    updateMargins();
    positionCopyButton();
}

int HexColorInput::hashSlotWidth() const
{
    return ruwa::ui::core::ThemeManager::instance().scaled(BaseHashSlot);
}

int HexColorInput::hashLeftPadding() const
{
    return ruwa::ui::core::ThemeManager::instance().scaled(BaseHashLeftPad);
}

int HexColorInput::rightPadding() const
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    if (m_copyButtonVisible) {
        return theme.scaled(BaseCopyRightPad + BaseCopyButtonSize + BaseCopyTextGap);
    }
    return theme.scaled(BaseRightPad);
}

void HexColorInput::updateMargins()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(BaseHashTextGap);
    // Reserve left side for the '#' glyph; right padding balances the text area.
    setTextMargins(hashLeftPadding() + hashSlotWidth() + gap, 0, rightPadding(), 0);
}

void HexColorInput::positionCopyButton()
{
    if (!m_copyButton)
        return;

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int rightPad = theme.scaled(BaseCopyRightPad);
    m_copyButton->move(width() - rightPad - m_copyButton->width(),
        (height() - m_copyButton->height()) / 2);
}

void HexColorInput::applyPalette()
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

QString HexColorInput::sanitizeHexDigits(const QString& input) const
{
    QString s = input.trimmed();
    if (s.startsWith('#'))
        s.remove(0, 1);
    // Keep only hex chars
    static const QRegularExpression nonHex("[^0-9a-fA-F]");
    s.remove(nonHex);
    if (s.length() > maxLength())
        s.truncate(maxLength());
    return s.toUpper();
}

void HexColorInput::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Paste)) {
        if (auto* cb = QGuiApplication::clipboard()) {
            const QString cleaned = sanitizeHexDigits(cb->text());
            if (!cleaned.isEmpty()) {
                insert(cleaned);
            }
            event->accept();
            return;
        }
    }
    QLineEdit::keyPressEvent(event);
}

void HexColorInput::paintEvent(QPaintEvent* event)
{
    using TC = ruwa::ui::core::ThemeColors;
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(rect());
    const qreal pillR = qMax(0.0, r.height() * 0.5 - 0.5);
    const QRectF fillRect = r.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal fillR = qMax(0.0, pillR - 1.0);

    // Resting background (matches unselected layer rows).
    p.setPen(Qt::NoPen);
    p.setBrush(colors.surfaceAlt);
    p.drawRoundedRect(fillRect, fillR, fillR);

    // Hover plate (soft surfaceElevated overlay, like CapsuleButton Secondary).
    if (m_hoverProgress > 0.001) {
        QColor plate = colors.surfaceElevated();
        plate.setAlpha(qBound(0, qRound(m_hoverProgress * 90), 255));
        p.setPen(Qt::NoPen);
        p.setBrush(plate);
        p.drawRoundedRect(fillRect, fillR, fillR);
    }

    // Capsule border, transitions on hover and focus.
    const qreal accent = qMax<qreal>(m_hoverProgress, hasFocus() ? 1.0 : 0.0);
    QColor borderTop = TC::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), accent);
    QColor borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(
        p, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);

    p.end();

    // Let QLineEdit paint the text/cursor on top.
    QLineEdit::paintEvent(event);

    // Paint the static '#' suffix.
    QPainter overlay(this);
    overlay.setRenderHint(QPainter::Antialiasing);
    const QColor hashColor = TC::interpolate(colors.textMuted, colors.text, accent);
    overlay.setPen(hashColor);
    QFont hashFont = font();
    overlay.setFont(hashFont);
    const int slot = hashSlotWidth();
    const int leftPad = hashLeftPadding();
    const QRect hashRect(leftPad, 0, slot, height());
    overlay.drawText(hashRect, Qt::AlignCenter, QStringLiteral("#"));
}

} // namespace ruwa::ui::widgets
