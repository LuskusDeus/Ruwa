// SPDX-License-Identifier: MPL-2.0

// CapsuleButton.cpp
#include "CapsuleButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/DotGridLoadingIndicator.h"

#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QFontMetrics>
#include <QEnterEvent>
#include <QResizeEvent>

namespace ruwa::ui::widgets {

namespace {
const int BASE_TAB_PAD_H = 14;
const int BASE_TAB_PAD_V = 6;
const int BASE_ACTION_PAD_H = 24;
const int BASE_ACTION_PAD_V = 13;
const int BASE_HINT_SPACING = 8; // px between main text and hint text
const int BASE_BANNER_HEIGHT = 48;
const int BASE_BANNER_PAD_H = 22;
const int BASE_BANNER_FONT_SIZE = 10;
const int BASE_BANNER_ICON_SIZE = 16;
const int BASE_BANNER_ICON_GAP = 8;
const int BASE_BANNER_LOADING_SIZE = 18;
} // namespace

CapsuleButton::CapsuleButton(const QString& text, Variant variant, QWidget* parent)
    : QPushButton(text, parent)
    , m_variant(variant)
{
    setFlat(true);
    setCheckable(variant == Variant::Tab);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    if (variant == Variant::Tab) {
        setAutoDefault(false);
        setDefault(false);
        setStyleSheet(
            QStringLiteral("QPushButton { background: transparent; border: none; padding: 0; }"));
    }

    setupAnimations();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &CapsuleButton::onThemeChanged);

    if (isBannerVariant()) {
        updateBannerScaledSizes();
    }
}

CapsuleButton::~CapsuleButton()
{
    delete m_hoverAnimation;
    delete m_checkAnimation;
}

void CapsuleButton::setupAnimations()
{
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress");
    m_hoverAnimation->setDuration(180);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    if (m_variant == Variant::Tab) {
        m_checkAnimation = new QPropertyAnimation(this, "checkProgress");
        m_checkAnimation->setDuration(240);
        m_checkAnimation->setEasingCurve(QEasingCurve::InOutCubic);
        connect(this, &QPushButton::toggled, this, [this](bool on) { startCheckAnimation(on); });
    }
}

void CapsuleButton::setHintText(const QString& hint)
{
    if (m_hintText == hint)
        return;
    m_hintText = hint;
    update();
    updateGeometry();
}

void CapsuleButton::syncSizeToText()
{
    if (isBannerVariant()) {
        updateBannerScaledSizes();
    } else {
        updateGeometry();
    }
}

void CapsuleButton::setSizeScale(qreal scale)
{
    const qreal boundedScale = qBound(0.5, scale, 1.5);
    if (qFuzzyCompare(m_sizeScale, boundedScale)) {
        return;
    }
    m_sizeScale = boundedScale;
    syncSizeToText();
    update();
}

void CapsuleButton::setBaseMinimumWidth(int width)
{
    const int boundedWidth = qMax(0, width);
    if (m_baseMinimumWidth == boundedWidth) {
        return;
    }
    m_baseMinimumWidth = boundedWidth;
    syncSizeToText();
    update();
}

void CapsuleButton::setBannerBaseHeight(int basePx)
{
    const int bounded = qMax(0, basePx);
    if (m_bannerBaseHeight == bounded) {
        return;
    }
    m_bannerBaseHeight = bounded;
    if (isBannerVariant()) {
        updateBannerScaledSizes();
    }
    update();
}

void CapsuleButton::setLightBannerContext(bool enabled)
{
    if (m_lightBanner == enabled) {
        return;
    }
    m_lightBanner = enabled;
    update();
}

void CapsuleButton::setSecondaryIdleShadowAlpha(int alpha)
{
    const int boundedAlpha = qBound(0, alpha, 255);
    if (m_secondaryIdleShadowAlpha == boundedAlpha) {
        return;
    }
    m_secondaryIdleShadowAlpha = boundedAlpha;
    update();
}

void CapsuleButton::setSecondaryIdleFillAlpha(int alpha)
{
    const int boundedAlpha = qBound(0, alpha, 255);
    if (m_secondaryIdleFillAlpha == boundedAlpha) {
        return;
    }
    m_secondaryIdleFillAlpha = boundedAlpha;
    update();
}

void CapsuleButton::setSecondaryRestingFillAlt(bool enabled)
{
    if (m_secondaryRestingFillAlt == enabled) {
        return;
    }
    m_secondaryRestingFillAlt = enabled;
    update();
}

void CapsuleButton::setPrimaryBorderVisible(bool visible)
{
    if (m_primaryBorderVisible == visible) {
        return;
    }
    m_primaryBorderVisible = visible;
    update();
}

void CapsuleButton::setTrailingLoadingVisible(bool visible)
{
    if (m_trailingLoadingVisible == visible) {
        return;
    }

    m_trailingLoadingVisible = visible;
    if (visible) {
        ensureTrailingLoadingIndicator();
        m_trailingLoadingIndicator->show();
        m_trailingLoadingIndicator->raise();
        m_trailingLoadingIndicator->start();
    } else if (m_trailingLoadingIndicator) {
        m_trailingLoadingIndicator->stop();
        m_trailingLoadingIndicator->hide();
    }

    syncSizeToText();
    updateTrailingLoadingIndicator();
    update();
}

void CapsuleButton::setHoverProgress(qreal p)
{
    m_hoverProgress = p;
    update();
}

void CapsuleButton::setCheckProgress(qreal p)
{
    m_checkProgress = qBound(0.0, p, 1.0);
    update();
}

bool CapsuleButton::isBannerVariant() const
{
    return m_variant == Variant::Primary || m_variant == Variant::Secondary;
}

void CapsuleButton::updateBannerScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    QFont f = font();
    f.setPointSize(qMax(1, qRound(theme.scaledFontSize(BASE_BANNER_FONT_SIZE) * m_sizeScale)));
    f.setBold(true);
    setFont(f);

    const int baseH = m_bannerBaseHeight > 0 ? m_bannerBaseHeight : BASE_BANNER_HEIGHT;
    const int h = qMax(1, qRound(theme.scaled(baseH) * m_sizeScale));
    const int padH = qMax(1, qRound(theme.scaled(BASE_BANNER_PAD_H) * m_sizeScale));
    const int iconSz = qMax(1, qRound(theme.scaled(BASE_BANNER_ICON_SIZE) * m_sizeScale));
    const int iconGap = qMax(1, qRound(theme.scaled(BASE_BANNER_ICON_GAP) * m_sizeScale));

    QFontMetrics fm(f);
    int w = padH * 2 + fm.horizontalAdvance(text());
    if (!icon().isNull()) {
        w += iconSz + iconGap;
    }
    if (m_trailingLoadingVisible) {
        w += qMax(1, theme.scaled(BASE_BANNER_LOADING_SIZE)) + iconGap;
    }
    w = qMax(w, qRound(theme.scaled(m_baseMinimumWidth) * m_sizeScale));
    setFixedSize(w, h);
    updateTrailingLoadingIndicator();
}

void CapsuleButton::ensureTrailingLoadingIndicator()
{
    if (m_trailingLoadingIndicator) {
        return;
    }

    m_trailingLoadingIndicator = new DotGridLoadingIndicator(this);
    m_trailingLoadingIndicator->hide();
    updateTrailingLoadingIndicator();
}

void CapsuleButton::updateTrailingLoadingIndicator()
{
    if (!m_trailingLoadingIndicator) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int loadingSize = qMax(1, qRound(theme.scaled(BASE_BANNER_LOADING_SIZE) * m_sizeScale));
    const int gap = qMax(1, qRound(theme.scaled(BASE_BANNER_ICON_GAP) * m_sizeScale));
    const int iconSize = qMax(1, qRound(theme.scaled(BASE_BANNER_ICON_SIZE) * m_sizeScale));
    const int iconGap = gap;

    QFontMetrics fm(font());
    int contentWidth = fm.horizontalAdvance(text()) + gap + loadingSize;
    if (!icon().isNull()) {
        contentWidth += iconSize + iconGap;
    }

    int contentX = (width() - contentWidth) / 2;
    if (!icon().isNull()) {
        contentX += iconSize + iconGap;
    }
    contentX += fm.horizontalAdvance(text()) + gap;

    m_trailingLoadingIndicator->setAccentColor(colors.primary);
    m_trailingLoadingIndicator->setFixedSize(loadingSize, loadingSize);
    m_trailingLoadingIndicator->move(contentX, (height() - loadingSize) / 2);
}

// ============================================================================
// Size
// ============================================================================

QSize CapsuleButton::sizeHint() const
{
    if (isBannerVariant()) {
        return QSize(width(), height());
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    QFontMetrics fm(font());
    const int textW = fm.horizontalAdvance(text());

    int hintW = 0;
    if (m_variant == Variant::Action && !m_hintText.isEmpty()) {
        QFont hf = font();
        hf.setPointSize(qMax(6, font().pointSize() - 1));
        hintW = QFontMetrics(hf).horizontalAdvance(m_hintText) + theme.scaled(BASE_HINT_SPACING);
    }

    const int padH = theme.scaled(m_variant == Variant::Tab ? BASE_TAB_PAD_H : BASE_ACTION_PAD_H);
    const int padV = theme.scaled(m_variant == Variant::Tab ? BASE_TAB_PAD_V : BASE_ACTION_PAD_V);

    return QSize(textW + hintW + padH * 2, fm.height() + padV * 2);
}

QSize CapsuleButton::minimumSizeHint() const
{
    return sizeHint();
}

// ============================================================================
// Events
// ============================================================================

void CapsuleButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    startHoverAnimation(true);
}

void CapsuleButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    startHoverAnimation(false);
}

void CapsuleButton::resizeEvent(QResizeEvent* event)
{
    QPushButton::resizeEvent(event);
    updateTrailingLoadingIndicator();
}

void CapsuleButton::startHoverAnimation(bool hovered)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

void CapsuleButton::startCheckAnimation(bool checked)
{
    if (m_variant != Variant::Tab || !m_checkAnimation)
        return;
    m_checkAnimation->stop();
    m_checkAnimation->setStartValue(m_checkProgress);
    m_checkAnimation->setEndValue(checked ? 1.0 : 0.0);
    m_checkAnimation->start();
}

// ============================================================================
// Paint
// ============================================================================

void CapsuleButton::paintEvent(QPaintEvent*)
{
    using TC = ruwa::ui::core::ThemeColors;
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const bool enabled = isEnabled();
    const qreal hover = enabled ? m_hoverProgress : 0.0;
    const bool pressed = enabled && isDown();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF r(rect());
    const qreal radius = r.height() / 2.0;

    if (isBannerVariant()) {
        using TC = ruwa::ui::core::ThemeColors;

        const qreal pillR = qMax(0.0, r.height() * 0.5 - 0.5);
        const QRectF fillRect = r.adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal fillR = qMax(0.0, pillR - 1.0);
        const bool primary = m_variant == Variant::Primary;

        QColor textPen;
        if (primary) {
            QColor bgColor;
            if (!enabled) {
                bgColor = colors.primaryDisabled();
                textPen = colors.textDisabled();
            } else if (m_lightBanner) {
                if (colors.isDark) {
                    bgColor = colors.background;
                    bgColor = TC::adjustBrightness(bgColor, 1.0 + hover * 0.08);
                    textPen = colors.primary;
                } else {
                    bgColor = colors.text;
                    bgColor = TC::adjustBrightness(bgColor, 1.0 - hover * 0.06);
                    textPen = colors.background;
                }
            } else {
                bgColor = colors.primary;
                bgColor = TC::adjustBrightness(bgColor, 1.0 + hover * 0.08);
                textPen = colors.textOnPrimary();
            }

            const QRectF primaryFillRect = m_primaryBorderVisible ? fillRect : r;
            const qreal primaryFillR = m_primaryBorderVisible ? fillR : pillR;
            p.setPen(Qt::NoPen);
            p.setBrush(bgColor);
            p.drawRoundedRect(primaryFillRect, primaryFillR, primaryFillR);

            if (m_primaryBorderVisible) {
                QColor borderTop;
                QColor borderBottom;
                if (!enabled) {
                    borderTop = colors.borderSubtle();
                    borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
                } else if (m_lightBanner) {
                    borderTop = TC::adjustBrightness(colors.primary, colors.isDark ? 1.18 : 0.92);
                    borderBottom
                        = TC::adjustBrightness(colors.primary, colors.isDark ? 0.82 : 0.75);
                } else {
                    borderTop = TC::adjustBrightness(colors.background, 1.7);
                    borderBottom = TC::adjustBrightness(colors.background, 1.45);
                }
                ruwa::ui::painting::drawGradientBorder(
                    p, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);
            }

            if (pressed) {
                p.setPen(Qt::NoPen);
                p.setBrush(colors.shadow(25));
                p.drawRoundedRect(primaryFillRect, primaryFillR, primaryFillR);
            }
        } else {
            if (enabled && m_secondaryRestingFillAlt) {
                // Filled input-field plate (matches the Color-panel hex input).
                p.setPen(Qt::NoPen);
                p.setBrush(colors.surfaceAlt);
                p.drawRoundedRect(fillRect, fillR, fillR);
            }
            if (m_secondaryIdleShadowAlpha > 0) {
                p.setPen(Qt::NoPen);
                p.setBrush(colors.shadow(m_secondaryIdleShadowAlpha));
                p.drawRoundedRect(fillRect, fillR, fillR);
            }
            if (m_secondaryIdleFillAlpha > 0) {
                QColor idleFill = colors.surfaceElevated();
                idleFill.setAlpha(m_secondaryIdleFillAlpha);
                p.setPen(Qt::NoPen);
                p.setBrush(idleFill);
                p.drawRoundedRect(fillRect, fillR, fillR);
            }
            if (hover > 0.001) {
                QColor plate = m_lightBanner ? QColor(0, 0, 0, qBound(0, qRound(hover * 52), 255))
                                             : colors.surfaceElevated();
                if (!m_lightBanner) {
                    plate.setAlpha(qBound(0, qRound(hover * 90), 255));
                }
                p.setPen(Qt::NoPen);
                p.setBrush(plate);
                p.drawRoundedRect(fillRect, fillR, fillR);
            }

            QColor borderTop;
            QColor borderBottom;
            if (!enabled) {
                borderTop = colors.borderSubtle();
                borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
            } else if (m_lightBanner) {
                const QColor ink = colors.textOnPrimary();
                borderTop
                    = TC::interpolate(TC::withAlpha(ink, 110), TC::withAlpha(ink, 210), hover);
                borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
            } else {
                borderTop
                    = TC::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), hover);
                borderBottom = TC::withAlpha(borderTop, borderTop.alpha() / 2);
            }
            ruwa::ui::painting::drawGradientBorder(
                p, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR, borderTop, borderBottom);

            if (pressed) {
                p.setPen(Qt::NoPen);
                p.setBrush(colors.shadow(20));
                p.drawRoundedRect(fillRect, fillR, fillR);
            }

            textPen = !enabled
                ? colors.textDisabled()
                : (m_lightBanner
                          ? TC::interpolate(TC::adjustBrightness(colors.textOnPrimary(), 1.25),
                                colors.textOnPrimary(), hover)
                          : TC::interpolate(colors.textMuted, colors.text, hover));
        }

        const int iconSize = qMax(1, qRound(theme.scaled(BASE_BANNER_ICON_SIZE) * m_sizeScale));
        const int iconTextSpacing
            = qMax(1, qRound(theme.scaled(BASE_BANNER_ICON_GAP) * m_sizeScale));
        const int loadingSize
            = qMax(1, qRound(theme.scaled(BASE_BANNER_LOADING_SIZE) * m_sizeScale));
        const bool showTrailingLoading = m_trailingLoadingVisible && m_trailingLoadingIndicator;
        const int centerY = int(r.center().y());
        QFontMetrics fm(font());
        const int textWidth = fm.horizontalAdvance(text());
        int contentWidth = textWidth;
        if (!icon().isNull()) {
            contentWidth += iconSize + iconTextSpacing;
        }
        if (showTrailingLoading) {
            contentWidth += loadingSize + iconTextSpacing;
        }

        int contentX = int(r.left()) + (int(r.width()) - contentWidth) / 2;
        if (!icon().isNull()) {
            QPixmap iconPixmap = icon().pixmap(iconSize, iconSize);
            if (!iconPixmap.isNull()) {
                QPainter iconPainter(&iconPixmap);
                iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                iconPainter.fillRect(iconPixmap.rect(), textPen);
                iconPainter.end();
                p.drawPixmap(contentX, centerY - iconSize / 2, iconPixmap);
                contentX += iconSize + iconTextSpacing;
            }
        }

        p.setFont(font());
        p.setPen(textPen);
        p.drawText(QRect(contentX, int(r.top()), textWidth, int(r.height())),
            Qt::AlignVCenter | Qt::AlignLeft, text());

    } else if (m_variant == Variant::Tab) {
        const qreal sel = enabled ? m_checkProgress : 0.0;
        const qreal hov = hover;
        const QRectF rr = r.adjusted(0.5, 0.5, -0.5, -0.5);

        // Fill cross-fades in with selection; border fades out (no hard threshold — avoids 1-frame
        // pop). Same accent semantics as SegmentedOptionSelector / Action variant: primary +
        // textOnPrimary.
        if (sel > 0.001) {
            QColor fill = colors.primary;
            if (hov > 0) {
                fill = TC::adjustBrightness(colors.primary, 1.0 + hov * 0.08);
            }
            const qreal fillOpacity
                = (sel >= 0.999) ? fill.alphaF() : qMin(1.0, fill.alphaF() * sel);
            fill.setAlphaF(fillOpacity);
            p.setBrush(fill);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(r, radius, radius);
        }
        {
            QColor borderColor
                = TC::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), hov);
            const qreal borderA = borderColor.alphaF() * (1.0 - sel);
            if (borderA > 0.02) {
                borderColor.setAlphaF(borderA);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(borderColor, 1.0));
                p.drawRoundedRect(rr, radius, radius);
            }
        }

        const QColor inactiveText
            = enabled ? TC::interpolate(colors.textMuted, colors.text, hov) : colors.textDisabled();
        const QColor textColor = TC::interpolate(inactiveText, colors.textOnPrimary(), sel);
        p.setPen(textColor);
        QFont f = font();
        p.setFont(f);
        p.drawText(r, Qt::AlignCenter, text());

    } else { // Action
        // Primary theme accent (not preset.accent — vivid secondary reserved for other UI)
        QColor bg = enabled ? colors.primary : colors.primaryDisabled();
        if (hover > 0) {
            bg = TC::adjustBrightness(colors.primary, 1.0 + hover * 0.08);
        }
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(r, radius, radius);

        const QColor mainColor = enabled ? colors.textOnPrimary() : colors.textDisabled();
        QColor hintColor = mainColor;
        hintColor.setAlphaF(0.6);

        QFont mainFont = font();
        QFont hintFont = font();
        hintFont.setPointSize(qMax(6, mainFont.pointSize() - 1));

        QFontMetrics mainFM(mainFont);
        QFontMetrics hintFM(hintFont);

        const int mainW = mainFM.horizontalAdvance(text());
        const int hintW = m_hintText.isEmpty() ? 0 : hintFM.horizontalAdvance(m_hintText);
        const int spacing = m_hintText.isEmpty() ? 0 : theme.scaled(BASE_HINT_SPACING);
        const int totalW = mainW + spacing + hintW;

        const qreal startX = (r.width() - totalW) / 2.0;

        // Main text
        p.setFont(mainFont);
        p.setPen(mainColor);
        p.drawText(
            QRectF(startX, r.top(), mainW, r.height()), Qt::AlignVCenter | Qt::AlignLeft, text());

        // Hint text
        if (!m_hintText.isEmpty()) {
            p.setFont(hintFont);
            p.setPen(hintColor);
            p.drawText(QRectF(startX + mainW + spacing, r.top(), hintW, r.height()),
                Qt::AlignVCenter | Qt::AlignLeft, m_hintText);
        }
    }
}

// ============================================================================
// Theme
// ============================================================================

void CapsuleButton::onThemeChanged()
{
    if (isBannerVariant()) {
        updateBannerScaledSizes();
    }
    updateTrailingLoadingIndicator();
    update();
    updateGeometry();
}

} // namespace ruwa::ui::widgets
