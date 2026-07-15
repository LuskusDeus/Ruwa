// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S P L A S H   S C R E E N   I M P L E M E N T A T I O N
// ======================================================================================

#include "SplashScreen.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/FontFamilyNames.h"
#include "shared/resources/IconProvider.h"

#include <QApplication>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QtMath>
#include <QGraphicsOpacityEffect>

namespace ruwa::ui::windows {

namespace {

void ensureSplashFontsOnce()
{
    static bool done = false;
    if (done) {
        return;
    }
    done = true;
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/InstrumentSerif-Regular"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans18pt-Regular"));
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DMSans18pt-SemiBold"));
}

} // namespace

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    ensureSplashFontsOnce();

    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QRect screenRect = screen->availableGeometry();
        setGeometry(screenRect);

        m_contentRect = QRectF((screenRect.width() - SPLASH_WIDTH) / 2.0,
            (screenRect.height() - SPLASH_HEIGHT) / 2.0, SPLASH_WIDTH, SPLASH_HEIGHT);
        m_animatedRect = m_contentRect;
    }

    m_appearProgress = 0.0;
    m_isAppearing = true;
    m_statusText = QStringLiteral("Initializing...");

    QPixmap logo = ui::core::IconProvider::instance().getApplicationLogoPixmap();
    if (!logo.isNull()) {
        m_logoPixmap = logo.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    updateColors();
}

void SplashScreen::updateColors()
{
    update();
}

void SplashScreen::setStatus(const QString& message)
{
    m_statusText = message;
    update();
}

void SplashScreen::setProgress(int percentage)
{
    m_targetProgress = qBound(0, percentage, 100);

    if (!m_progressAnim) {
        m_progressAnim = new QPropertyAnimation(this, "progressDisplay", this);
        m_progressAnim->setDuration(380);
        m_progressAnim->setEasingCurve(QEasingCurve::OutCubic);
    }

    m_progressAnim->stop();
    m_progressAnim->setStartValue(m_progressDisplay);
    m_progressAnim->setEndValue(static_cast<qreal>(m_targetProgress));
    m_progressAnim->start();
}

void SplashScreen::setProgressDisplay(qreal value)
{
    m_progressDisplay = qBound(0.0, value, 100.0);
    update();
}

void SplashScreen::setAppearProgress(qreal progress)
{
    m_appearProgress = qBound(0.0, progress, 1.0);
    update();
}

void SplashScreen::setContentOpacity(qreal opacity)
{
    m_contentOpacity = qBound(0.0, opacity, 1.0);
    update();
}

void SplashScreen::setForegroundOpacity(qreal opacity)
{
    m_foregroundOpacity = qBound(0.0, opacity, 1.0);
    update();
}

void SplashScreen::animateAppearance(int durationMs)
{
    m_isAppearing = true;
    m_appearProgress = 0.0;
    m_contentOpacity = 0.0;
    m_foregroundOpacity = 1.0;

    auto* group = new QParallelAnimationGroup(this);

    auto* scaleAnim = new QPropertyAnimation(this, "appearProgress", this);
    scaleAnim->setDuration(durationMs);
    scaleAnim->setStartValue(0.0);
    scaleAnim->setEndValue(1.0);
    scaleAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto* opacityAnim = new QPropertyAnimation(this, "contentOpacity", this);
    opacityAnim->setDuration(durationMs);
    opacityAnim->setStartValue(0.0);
    opacityAnim->setEndValue(1.0);
    opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    group->addAnimation(scaleAnim);
    group->addAnimation(opacityAnim);

    connect(group, &QParallelAnimationGroup::finished, this, [this]() {
        m_isAppearing = false;
        m_appearProgress = 1.0;
        m_contentOpacity = 1.0;
        m_animatedRect = m_contentRect;
        emit appearanceFinished();
    });

    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::startRectExpansion(int durationMs)
{
    m_startLocalRect = m_contentRect;
    m_targetLocalRect = QRectF(0, 0, width(), height());
    m_animatedRect = m_startLocalRect;

    m_isExpanding = true;
    m_isAppearing = false;

    const int fps = 60;
    const int frameTime = 1000 / fps;
    const int totalFrames = qMax(1, durationMs / frameTime);

    int* currentFrame = new int(0);

    auto* timer = new QTimer(this);
    timer->setInterval(frameTime);

    connect(timer, &QTimer::timeout, this, [this, timer, currentFrame, totalFrames]() {
        (*currentFrame)++;
        qreal progress = qMin(1.0, static_cast<qreal>(*currentFrame) / totalFrames);

        qreal easedProgress;
        if (progress < 0.5) {
            easedProgress = 4.0 * progress * progress * progress;
        } else {
            easedProgress = 1.0 - qPow(-2.0 * progress + 2.0, 3.0) / 2.0;
        }

        qreal x
            = m_startLocalRect.x() + (m_targetLocalRect.x() - m_startLocalRect.x()) * easedProgress;
        qreal y
            = m_startLocalRect.y() + (m_targetLocalRect.y() - m_startLocalRect.y()) * easedProgress;
        qreal w = m_startLocalRect.width()
            + (m_targetLocalRect.width() - m_startLocalRect.width()) * easedProgress;
        qreal h = m_startLocalRect.height()
            + (m_targetLocalRect.height() - m_startLocalRect.height()) * easedProgress;

        m_animatedRect = QRectF(x, y, w, h);
        update();

        if (progress >= 1.0) {
            timer->stop();
            timer->deleteLater();
            delete currentFrame;

            m_isExpanding = false;
            emit expansionFinished();
        }
    });

    timer->start();
}

void SplashScreen::expandToMainWindow(int durationMs)
{
    if (m_isExpanding || m_expandRequested) {
        return;
    }
    if (m_isAppearing) {
        return;
    }

    m_expandRequested = true;
    m_pendingExpandDurationMs = durationMs;

    auto* fadeChrome = new QPropertyAnimation(this, "foregroundOpacity", this);
    fadeChrome->setDuration(400);
    fadeChrome->setStartValue(1.0);
    fadeChrome->setEndValue(0.0);
    fadeChrome->setEasingCurve(QEasingCurve::InCubic);

    connect(fadeChrome, &QPropertyAnimation::finished, this,
        [this]() { startRectExpansion(m_pendingExpandDurationMs); });

    fadeChrome->start(QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::fadeOut(int durationMs)
{
    auto* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);

    auto* animation = new QPropertyAnimation(effect, "opacity", this);
    animation->setDuration(durationMs);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(animation, &QPropertyAnimation::finished, this, &QWidget::close);

    animation->start(QPropertyAnimation::DeleteWhenStopped);
}

void SplashScreen::paintInterior(QPainter& painter) const
{
    const auto& colors = ui::core::ThemeManager::instance().colors();

    const qreal margin = 36;
    qreal x = margin;
    qreal y = margin;

    if (!m_logoPixmap.isNull()) {
        painter.drawPixmap(QRectF(x, y, 40, 40), m_logoPixmap, m_logoPixmap.rect());
        x += 40 + 12;
    }

    QFont titleFont(ruwa::ui::core::FontFamilyNames::InstrumentSerif, 32, QFont::Normal);
    painter.setFont(titleFont);
    painter.setPen(colors.text);

    const QRectF titleRect(x, y, SPLASH_WIDTH - margin - x, 40);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Ruwa"));

    const QString versionRaw = QStringLiteral("v%1").arg(QApplication::applicationVersion());
    const QString versionTag = versionRaw.toUpper();

    QFont badgeFont(ruwa::ui::core::FontFamilyNames::DMSans18pt, 10, QFont::DemiBold);
    badgeFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    painter.setFont(badgeFont);
    QFontMetricsF badgeFm(badgeFont);

    const qreal badgePadH = 10;
    const qreal badgePadV = 3;
    const qreal badgeW = badgeFm.horizontalAdvance(versionTag) + badgePadH * 2;
    const qreal badgeH = badgeFm.height() + badgePadV * 2;
    const qreal badgeX = margin;
    const qreal badgeY = margin + 40 + 6;

    QRectF badgeRect(badgeX, badgeY, badgeW, badgeH);
    const qreal badgeR = badgeH / 2;

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.primary);
    painter.drawRoundedRect(badgeRect, badgeR, badgeR);

    painter.setPen(colors.textOnPrimary());
    painter.drawText(badgeRect, Qt::AlignCenter, versionTag);

    QFont statusFont(ruwa::ui::core::FontFamilyNames::DMSans18pt, 11, QFont::Light);
    painter.setFont(statusFont);
    painter.setPen(colors.textMuted);
    QFontMetricsF statusFm(statusFont);

    const qreal barH = 4;
    const qreal barHMargin = 18;
    const qreal barBottomPad = 20;
    const QRectF trackRect(
        barHMargin, SPLASH_HEIGHT - barBottomPad - barH, SPLASH_WIDTH - 2 * barHMargin, barH);
    const qreal barR = barH / 2.0;

    const qreal statusY = trackRect.top() - 10 - statusFm.height();
    painter.drawText(QRectF(margin, statusY, SPLASH_WIDTH - margin * 2, statusFm.height()),
        Qt::AlignLeft | Qt::AlignVCenter, m_statusText);

    constexpr qreal kCardRadius = 16;
    QPainterPath cardClip;
    cardClip.addRoundedRect(0, 0, SPLASH_WIDTH, SPLASH_HEIGHT, kCardRadius, kCardRadius);

    painter.save();
    painter.setClipPath(cardClip);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.border);
    painter.drawRoundedRect(trackRect, barR, barR);

    const qreal fillW = trackRect.width() * (m_progressDisplay / 100.0);
    if (fillW > 0.05) {
        const qreal capR = qMin(barR, qMax(fillW * 0.5, 0.001));
        QRectF fillRect(trackRect.left(), trackRect.top(), fillW, barH);
        QLinearGradient grad(fillRect.topLeft(), fillRect.topRight());
        grad.setColorAt(0, colors.primary);
        grad.setColorAt(1,
            ui::core::ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 0.82 : 1.12));
        painter.setBrush(grad);
        painter.drawRoundedRect(fillRect, capR, capR);
    }

    painter.restore();
}

void SplashScreen::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto& colors = ui::core::ThemeManager::instance().colors();

    QRectF drawRect = m_animatedRect;

    if (m_isAppearing) {
        const qreal scale = APPEAR_START_SCALE + (1.0 - APPEAR_START_SCALE) * m_appearProgress;
        const qreal scaledWidth = SPLASH_WIDTH * scale;
        const qreal scaledHeight = SPLASH_HEIGHT * scale;

        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            QRect screenRect = screen->availableGeometry();
            drawRect = QRectF((screenRect.width() - scaledWidth) / 2.0,
                (screenRect.height() - scaledHeight) / 2.0, scaledWidth, scaledHeight);
        }
    }

    const qreal chromeOpacity = m_isAppearing ? m_contentOpacity : m_foregroundOpacity;

    if (m_isExpanding) {
        qreal expandProgress = 0.0;
        if (qAbs(m_targetLocalRect.width() - m_startLocalRect.width()) > 0.001) {
            expandProgress = (m_animatedRect.width() - m_startLocalRect.width())
                / (m_targetLocalRect.width() - m_startLocalRect.width());
        }
        expandProgress = qBound(0.0, expandProgress, 1.0);
        const qreal radiusF = 16.0 * (1.0 - expandProgress);

        QColor bg = colors.background;
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(drawRect, radiusF, radiusF);
        return;
    }

    QColor bgColor = colors.background;
    if (m_isAppearing) {
        bgColor.setAlphaF(m_contentOpacity);
    }
    const qreal cornerR = 16.0 * drawRect.width() / SPLASH_WIDTH;

    // One path for fill + strokes so AA fill and outline share the same edge (inset stroke
    // vs full fill was letting background fringe show outside the border on corners).
    QPainterPath cardPath;
    cardPath.addRoundedRect(drawRect, cornerR, cornerR);

    painter.fillPath(cardPath, bgColor);

    if (chromeOpacity <= 0.001) {
        return;
    }

    painter.save();
    painter.setOpacity(chromeOpacity);

    QColor borderColor = colors.border;
    QPen outerPen(borderColor, 1);
    outerPen.setCosmetic(true);
    outerPen.setCapStyle(Qt::FlatCap);
    outerPen.setJoinStyle(Qt::MiterJoin);
    painter.setPen(outerPen);
    painter.setBrush(Qt::NoBrush);
    painter.strokePath(cardPath, outerPen);

    QColor highlightColor = ui::core::ThemeColors::withAlpha(colors.borderLight(), 100);
    QPen innerPen(highlightColor, 1);
    innerPen.setCosmetic(true);
    painter.setPen(innerPen);
    const qreal innerR = qMax(0.0, cornerR - 1);
    QPainterPath innerPath;
    innerPath.addRoundedRect(drawRect.adjusted(1.5, 1.5, -1.5, -1.5), innerR, innerR);
    painter.strokePath(innerPath, innerPen);

    const qreal sx = drawRect.width() / SPLASH_WIDTH;
    const qreal sy = drawRect.height() / SPLASH_HEIGHT;
    painter.translate(drawRect.topLeft());
    painter.scale(sx, sy);

    paintInterior(painter);

    painter.restore();
}

} // namespace ruwa::ui::windows
