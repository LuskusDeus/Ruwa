// SPDX-License-Identifier: MPL-2.0

// UpdatesActionButton.cpp
#include "features/settings/UpdatesActionButton.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPropertyAnimation>
#include <QStackedWidget>

namespace ruwa::ui::widgets {

namespace {
const int BASE_MIN_WIDTH = 196;
const int BASE_HEIGHT = 48;
const int BASE_PAD_H = 22;
const int BASE_BORDER_RADIUS = 24;
const int BASE_FONT_SIZE = 10;
const int BASE_ICON_SIZE = 16;
const int BASE_LOADING_SIZE = 20;
const int BASE_ICON_TEXT_SPACING = 8;
} // namespace

UpdatesActionButton::UpdatesActionButton(QWidget* parent)
    : BaseAnimatedButton(parent)
{
    setCheckable(false);
    setCursor(Qt::PointingHandCursor);
    setText(QString()); // We use our own text label
    setIcon(QIcon()); // We use our own icon label
    setHoverDuration(220);

    m_contentLayout = new QHBoxLayout(this);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);
    m_contentLayout->setSpacing(0);

    m_contentLayout->addStretch();

    // Icon/loading container (stacked - show one at a time)
    m_iconStack = new QStackedWidget(this);
    m_iconStack->setAttribute(Qt::WA_TranslucentBackground);
    m_iconStack->setFixedSize(BASE_LOADING_SIZE, BASE_LOADING_SIZE);

    m_iconLabel = new QLabel(m_iconStack);
    m_iconLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setScaledContents(true);
    m_iconStack->addWidget(m_iconLabel);

    m_loadingIndicator = new DotGridLoadingIndicator(m_iconStack);
    m_loadingIndicator->setFixedSize(BASE_LOADING_SIZE, BASE_LOADING_SIZE);
    m_iconStack->addWidget(m_loadingIndicator);

    m_iconStack->setCurrentWidget(m_iconLabel);
    m_contentLayout->addWidget(m_iconStack);

    m_textLabel = new QLabel(this);
    m_textLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_contentLayout->addWidget(m_textLabel);

    m_contentLayout->addStretch();

    m_pressAnimation = new QPropertyAnimation(this, "pressProgress", this);
    m_pressAnimation->setDuration(140);
    m_pressAnimation->setEasingCurve(QEasingCurve::OutCubic);

    updateContent();
    updateScaledSizes();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &UpdatesActionButton::onThemeChanged);
}

void UpdatesActionButton::setPressProgress(qreal progress)
{
    const qreal bounded = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_pressProgress, bounded)) {
        return;
    }

    m_pressProgress = bounded;
    update();
}

void UpdatesActionButton::setState(UpdateState state)
{
    if (m_state == state)
        return;
    m_state = state;
    updateContent();
}

void UpdatesActionButton::updateContent()
{
    auto& icons = ruwa::ui::core::IconProvider::instance();
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int iconSize = theme.scaled(BASE_ICON_SIZE);

    const bool isAccent
        = (m_state == UpdateState::UpdateAvailable || m_state == UpdateState::ReadyToRestart);
    m_textLabel->setStyleSheet(QString("QLabel { color: %1; background: transparent; }")
            .arg(isAccent ? colors.textOnPrimary().name() : colors.textMuted.name()));

    switch (m_state) {
    case UpdateState::UpToDate:
        m_textLabel->setText(tr("Up to date"));
        m_loadingIndicator->stop();
        m_iconStack->setCurrentWidget(m_iconLabel);
        setEnabled(false);
        setCursor(Qt::ArrowCursor);
        break;
    case UpdateState::Downloading:
        m_textLabel->setText(tr("Downloading"));
        updateLoadingIndicator();
        m_loadingIndicator->start();
        m_iconStack->setCurrentWidget(m_loadingIndicator);
        setEnabled(false);
        setCursor(Qt::ArrowCursor);
        break;
    case UpdateState::UpdateAvailable:
        m_textLabel->setText(tr("Download update"));
        m_loadingIndicator->stop();
        m_iconStack->setCurrentWidget(m_iconLabel);
        setEnabled(true);
        setCursor(Qt::PointingHandCursor);
        break;
    case UpdateState::ReadyToRestart:
        m_textLabel->setText(tr("Restart to apply update"));
        m_loadingIndicator->stop();
        m_iconStack->setCurrentWidget(m_iconLabel);
        setEnabled(true);
        setCursor(Qt::PointingHandCursor);
        break;
    }

    auto setStateIcon = [&](ruwa::ui::core::IconProvider::StandardIcon icon, const QColor& color) {
        const QPixmap pm = icons.getIcon(icon).pixmap(iconSize, iconSize);
        if (pm.isNull()) {
            return;
        }
        m_iconLabel->setPixmap(ruwa::ui::painting::tintedPixmap(pm, color));
        m_iconLabel->setFixedSize(iconSize, iconSize);
    };

    if (m_state == UpdateState::UpToDate) {
        setStateIcon(ruwa::ui::core::IconProvider::StandardIcon::Confirm, colors.textMuted);
    } else if (m_state == UpdateState::UpdateAvailable) {
        setStateIcon(ruwa::ui::core::IconProvider::StandardIcon::ArrowDown, colors.textOnPrimary());
    } else if (m_state == UpdateState::ReadyToRestart) {
        setStateIcon(ruwa::ui::core::IconProvider::StandardIcon::Confirm, colors.textOnPrimary());
    }

    updateScaledSizes();
    update();
}

void UpdatesActionButton::updateLoadingIndicator()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    m_loadingIndicator->setAccentColor(colors.primary);
}

void UpdatesActionButton::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    QFont font = m_textLabel->font();
    font.setPointSize(theme.scaledFontSize(BASE_FONT_SIZE));
    font.setBold(true);
    m_textLabel->setFont(font);

    const int loadingSize = theme.scaled(BASE_LOADING_SIZE);
    const int iconSize = theme.scaled(BASE_ICON_SIZE);
    const int contentPadding = theme.scaled(BASE_PAD_H);
    const int iconSpacing = theme.scaled(BASE_ICON_TEXT_SPACING);
    const int buttonHeight = theme.scaled(BASE_HEIGHT);
    const int minWidth = theme.scaled(BASE_MIN_WIDTH);

    m_loadingIndicator->setFixedSize(loadingSize, loadingSize);
    m_iconStack->setFixedSize(loadingSize, loadingSize);
    m_iconLabel->setFixedSize(iconSize, iconSize);
    m_contentLayout->setContentsMargins(contentPadding, 0, contentPadding, 0);
    m_contentLayout->setSpacing(iconSpacing);

    const int iconBlockWidth = loadingSize;
    const int textWidth = QFontMetrics(font).horizontalAdvance(m_textLabel->text());
    const int layoutSpacingCount = 3;
    const int buttonWidth = qMax(minWidth,
        contentPadding * 2 + iconBlockWidth + textWidth + iconSpacing * layoutSpacingCount);

    setFixedSize(buttonWidth, buttonHeight);
}

void UpdatesActionButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal borderRadius
        = qMax<qreal>(theme.scaled(BASE_BORDER_RADIUS), rect.height() * 0.5 - 0.5);
    const QRectF fillRect = rect.adjusted(1.0, 1.0, -1.0, -1.0);
    const qreal fillRadius = qMax<qreal>(0.0, fillRect.height() * 0.5);

    const bool isAccent
        = (m_state == UpdateState::UpdateAvailable || m_state == UpdateState::ReadyToRestart);
    const qreal hover = isEnabled() ? hoverProgress() : 0.0;

    if (isAccent) {
        QColor bgColor = colors.primary;
        if (hover > 0.0) {
            bgColor
                = ruwa::ui::core::ThemeColors::adjustBrightness(colors.primary, 1.0 + hover * 0.08);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(fillRect, fillRadius, fillRadius);

        ruwa::ui::painting::drawGradientBorder(painter, rect, borderRadius,
            ruwa::ui::core::ThemeColors::adjustBrightness(colors.background, 7.7),
            ruwa::ui::core::ThemeColors::adjustBrightness(colors.background, 6.2));

        if (m_pressProgress > 0.0) {
            painter.setPen(Qt::NoPen);
            QColor pressOverlay = colors.shadow(25);
            pressOverlay.setAlpha(qRound(pressOverlay.alpha() * m_pressProgress));
            painter.setBrush(pressOverlay);
            painter.drawRoundedRect(fillRect, fillRadius, fillRadius);
        }
    } else {
        painter.setPen(Qt::NoPen);
        QColor baseFill = colors.overlayBase();
        if (m_state == UpdateState::Downloading) {
            baseFill = ruwa::ui::core::ThemeColors::interpolate(
                baseFill, colors.surfaceElevated(), 0.32);
        }
        painter.setBrush(baseFill);
        painter.drawRoundedRect(fillRect, fillRadius, fillRadius);

        if (hover > 0.001) {
            QColor hoverPlate = colors.surfaceElevated();
            hoverPlate.setAlpha(qRound(hoverPlate.alpha() * hover * 0.6));
            painter.setBrush(hoverPlate);
            painter.drawRoundedRect(fillRect, fillRadius, fillRadius);
        }

        {
            QColor borderTop = ruwa::ui::core::ThemeColors::interpolate(
                colors.borderSubtle(), colors.borderSubtleHover(), hover);
            ruwa::ui::painting::drawGradientBorder(painter, rect, borderRadius, borderTop,
                ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
        }

        if (m_pressProgress > 0.0) {
            painter.setPen(Qt::NoPen);
            QColor pressOverlay = colors.shadow(18);
            pressOverlay.setAlpha(qRound(pressOverlay.alpha() * m_pressProgress));
            painter.setBrush(pressOverlay);
            painter.drawRoundedRect(fillRect, fillRadius, fillRadius);
        }
    }
}

void UpdatesActionButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        startPressAnimation(true);
    }

    BaseAnimatedButton::mousePressEvent(event);
}

void UpdatesActionButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        startPressAnimation(false);
    }

    BaseAnimatedButton::mouseReleaseEvent(event);
}

void UpdatesActionButton::startPressAnimation(bool pressed)
{
    if (!m_pressAnimation) {
        return;
    }

    m_pressAnimation->stop();
    m_pressAnimation->setDuration(pressed ? 90 : 170);
    m_pressAnimation->setStartValue(m_pressProgress);
    m_pressAnimation->setEndValue(pressed ? 1.0 : 0.0);
    m_pressAnimation->start();
}

void UpdatesActionButton::onThemeChanged()
{
    updateScaledSizes();
    updateContent();
    updateLoadingIndicator();
    update();
}

} // namespace ruwa::ui::widgets
