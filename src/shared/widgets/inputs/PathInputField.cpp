// SPDX-License-Identifier: MPL-2.0

// PathInputField.cpp
#include "PathInputField.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/ToolButton.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

namespace {
/// Authored hover fade; the animation policy scales it at each transition.
constexpr int kHoverAnimationMs = 180;
} // namespace

PathInputField::PathInputField(QWidget* parent)
    : QLineEdit(parent)
{
    setAttribute(Qt::WA_Hover);
    setFrame(false);
    // Left-aligned, unlike the hex field: a path is read from its start, and a
    // centred one would jitter sideways with every character typed.
    setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_actionButton
        = new ruwa::ui::workspace::ToolButton(ruwa::ui::workspace::ToolButton::Mode::Action, this);
    m_actionButton->setBaseSquareSize(BaseActionButtonSize, BaseActionIconSize);
    m_actionButton->setIconType(ruwa::ui::core::IconProvider::StandardIcon::OpenedFolder);
    m_actionButton->setChromeStyle(ruwa::ui::workspace::ToolButton::ChromeStyle::PrimaryHover);
    m_actionButton->setCircularChrome(true);
    m_actionButton->setBorderVisible(false);
    m_actionButton->setMutedNormalIcon(true);
    m_actionButton->setFocusPolicy(Qt::NoFocus);

    connect(m_actionButton, &QAbstractButton::clicked, this, &PathInputField::actionTriggered);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &PathInputField::onThemeChanged);

    applyPalette();
    rebuildLeadingIcons();
    updateScaledSize();
    updateMargins();
}

PathInputField::~PathInputField() = default;

void PathInputField::setLeadingIcon(ruwa::ui::core::IconProvider::StandardIcon icon)
{
    if (m_leadingIconType == icon) {
        return;
    }
    m_leadingIconType = icon;
    rebuildLeadingIcons();
    update();
}

void PathInputField::setActionIcon(ruwa::ui::core::IconProvider::StandardIcon icon)
{
    if (m_actionButton) {
        m_actionButton->setIconType(icon);
    }
}

void PathInputField::setActionToolTip(const QString& text)
{
    if (m_actionButton) {
        m_actionButton->setToolTip(text);
        m_actionButton->setAccessibleName(text);
    }
}

void PathInputField::setActionVisible(bool visible)
{
    if (!m_actionButton || m_actionVisible == visible) {
        return;
    }
    m_actionVisible = visible;
    m_actionButton->setVisible(visible);
    updateMargins();
    positionActionButton();
}

void PathInputField::setHoverProgress(qreal p)
{
    m_hoverProgress = qBound(0.0, p, 1.0);
    update();
}

void PathInputField::onThemeChanged()
{
    applyPalette();
    rebuildLeadingIcons();
    updateScaledSize();
    updateMargins();
    positionActionButton();
    update();
}

void PathInputField::startHoverAnimation(bool hovered)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->setDuration(anim::duration(kHoverAnimationMs));
    anim::start(m_hoverAnimation);
}

void PathInputField::enterEvent(QEnterEvent* event)
{
    QLineEdit::enterEvent(event);
    startHoverAnimation(true);
}

void PathInputField::leaveEvent(QEvent* event)
{
    QLineEdit::leaveEvent(event);
    startHoverAnimation(false);
}

void PathInputField::resizeEvent(QResizeEvent* event)
{
    QLineEdit::resizeEvent(event);
    updateMargins();
    positionActionButton();
}

int PathInputField::iconSlotWidth() const
{
    return ruwa::ui::core::ThemeManager::instance().scaled(BaseIconSlot);
}

int PathInputField::iconLeftPadding() const
{
    return ruwa::ui::core::ThemeManager::instance().scaled(BaseIconLeftPad);
}

int PathInputField::rightPadding() const
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    if (m_actionVisible) {
        return theme.scaled(BaseActionRightPad + BaseActionButtonSize + BaseActionTextGap);
    }
    return theme.scaled(BaseRightPad);
}

void PathInputField::updateScaledSize()
{
    setFixedHeight(ruwa::ui::core::ThemeManager::instance().scaled(BaseHeight));
}

void PathInputField::updateMargins()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(BaseIconTextGap);
    // Reserve the left slot for the glyph; the right side clears the button.
    setTextMargins(iconLeftPadding() + iconSlotWidth() + gap, 0, rightPadding(), 0);
}

void PathInputField::positionActionButton()
{
    if (!m_actionButton) {
        return;
    }
    const int rightPad = ruwa::ui::core::ThemeManager::instance().scaled(BaseActionRightPad);
    m_actionButton->move(
        width() - rightPad - m_actionButton->width(), (height() - m_actionButton->height()) / 2);
}

void PathInputField::applyPalette()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    QPalette pal = palette();
    pal.setColor(QPalette::Base, Qt::transparent);
    pal.setColor(QPalette::Text, colors.text);
    pal.setColor(QPalette::PlaceholderText, colors.textMuted);
    pal.setColor(QPalette::Highlight, colors.primary);
    pal.setColor(QPalette::HighlightedText, colors.textOnPrimary());
    setPalette(pal);
    setStyleSheet(
        QStringLiteral("QLineEdit { background: transparent; border: none; padding: 0; }"));
}

void PathInputField::rebuildLeadingIcons()
{
    auto& icons = ruwa::ui::core::IconProvider::instance();
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    m_leadingIconMuted = icons.getColoredIcon(m_leadingIconType, colors.textMuted);
    m_leadingIconActive = icons.getColoredIcon(m_leadingIconType, colors.text);
}

void PathInputField::paintEvent(QPaintEvent* event)
{
    using TC = ruwa::ui::core::ThemeColors;
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(rect());
    const qreal pillR = qMax(0.0, r.height() * 0.5 - 0.5);
    const QRectF fillRect = r.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal fillR = qMax(0.0, pillR - 1.0);

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
    const QColor borderTop
        = TC::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), accent);
    const QColor borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(
        p, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);

    p.end();

    // Let QLineEdit paint the text/cursor on top.
    QLineEdit::paintEvent(event);

    // The leading glyph, cross-faded between its muted and active colourings by
    // the same accent that drives the border.
    QPainter overlay(this);
    overlay.setRenderHint(QPainter::Antialiasing);
    overlay.setRenderHint(QPainter::SmoothPixmapTransform);
    const int slot = iconSlotWidth();
    const QRect iconRect(iconLeftPadding(), (height() - slot) / 2, slot, slot);
    m_leadingIconMuted.paint(&overlay, iconRect, Qt::AlignCenter);
    if (accent > 0.001) {
        overlay.setOpacity(accent);
        m_leadingIconActive.paint(&overlay, iconRect, Qt::AlignCenter);
        overlay.setOpacity(1.0);
    }
}

} // namespace ruwa::ui::widgets
