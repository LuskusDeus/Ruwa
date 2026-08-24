// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S P L A S H   S C R E E N   I M P L E M E N T A T I O N
// ======================================================================================

#include "SplashScreen.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/Credits.h"
#include "shared/resources/FontFamilyNames.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/AnimationPolicy.h"
#include "shell/main-window/WindowSetupCoordinator.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QStringList>
#include <QTimer>
#include <QWindow>
#include <QtMath>

#include <cmath>
#include <QGraphicsOpacityEffect>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::windows {

namespace {

/// Card corner radius at full size.
constexpr qreal kCardRadius = 28.0;

/// Padding between the card edge and its contents.
constexpr qreal kContentMargin = 24.0;

/// The header image sits closer to the edge than the rest of the content.
constexpr qreal kImageMargin = 16.0;

/// Corner radius of the inset header image.
constexpr qreal kImageRadius = 18.0;

/// Height of the wordmark row: the title and the version pill match it.
constexpr qreal kHeaderHeight = 28.0;

/// The logo runs a touch taller than the row, to sit level with the wordmark's
/// capital letters rather than with its em box.
constexpr qreal kLogoSize = 32.0;

/// Drop shadow: how far it reaches past the card, how far it is pushed down, and
/// its darkest alpha right at the card's edge.
constexpr qreal kShadowExtent = 56.0;
constexpr qreal kShadowOffsetY = 16.0;
constexpr qreal kShadowAlpha = 0.22;

/// 8x8 ordered dither thresholds, spanning one 8-bit step. A shadow this faint
/// covers only ~50 alpha levels over ~56px, so straight rounding lays down
/// visible concentric rings; jittering each pixel by a sub-step amount before
/// it is quantised trades those rings for noise below the visible threshold.
constexpr qreal kBayer8[64] = {
    0 / 64.0,
    32 / 64.0,
    8 / 64.0,
    40 / 64.0,
    2 / 64.0,
    34 / 64.0,
    10 / 64.0,
    42 / 64.0,
    48 / 64.0,
    16 / 64.0,
    56 / 64.0,
    24 / 64.0,
    50 / 64.0,
    18 / 64.0,
    58 / 64.0,
    26 / 64.0,
    12 / 64.0,
    44 / 64.0,
    4 / 64.0,
    36 / 64.0,
    14 / 64.0,
    46 / 64.0,
    6 / 64.0,
    38 / 64.0,
    60 / 64.0,
    28 / 64.0,
    52 / 64.0,
    20 / 64.0,
    62 / 64.0,
    30 / 64.0,
    54 / 64.0,
    22 / 64.0,
    3 / 64.0,
    35 / 64.0,
    11 / 64.0,
    43 / 64.0,
    1 / 64.0,
    33 / 64.0,
    9 / 64.0,
    41 / 64.0,
    51 / 64.0,
    19 / 64.0,
    59 / 64.0,
    27 / 64.0,
    49 / 64.0,
    17 / 64.0,
    57 / 64.0,
    25 / 64.0,
    15 / 64.0,
    47 / 64.0,
    7 / 64.0,
    39 / 64.0,
    13 / 64.0,
    45 / 64.0,
    5 / 64.0,
    37 / 64.0,
    63 / 64.0,
    31 / 64.0,
    55 / 64.0,
    23 / 64.0,
    61 / 64.0,
    29 / 64.0,
    53 / 64.0,
    21 / 64.0,
};

/// Paints `fn` into an offscreen layer covering `rect` and blits it once at `opacity`.
/// Setting the opacity on the painter instead would fade every element separately, so
/// the card's own background showed through the header image and the text sitting on
/// top of it - the group has to be composited flat first, then faded as a whole.
template <typename PaintFn>
void paintLayer(QPainter& painter, const QRectF& rect, qreal opacity, PaintFn&& fn)
{
    if (opacity >= 0.999) {
        fn(painter);
        return;
    }

    const QRect deviceRect = rect.toAlignedRect();
    if (deviceRect.isEmpty()) {
        return;
    }

    const qreal dpr = painter.device() ? painter.device()->devicePixelRatioF() : 1.0;
    QImage layer(QSize(qRound(deviceRect.width() * dpr), qRound(deviceRect.height() * dpr)),
        QImage::Format_ARGB32_Premultiplied);
    if (layer.isNull()) {
        fn(painter);
        return;
    }
    layer.setDevicePixelRatio(dpr);
    layer.fill(Qt::transparent);

    {
        QPainter layerPainter(&layer);
        layerPainter.setRenderHint(QPainter::Antialiasing);
        layerPainter.setRenderHint(QPainter::SmoothPixmapTransform);
        layerPainter.translate(-deviceRect.topLeft());
        fn(layerPainter);
    }

    const qreal previousOpacity = painter.opacity();
    painter.setOpacity(opacity);
    painter.drawImage(deviceRect.topLeft(), layer);
    painter.setOpacity(previousOpacity);
}

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
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);

    ensureSplashFontsOnce();

    QScreen* screen = WindowSetupCoordinator::savedScreen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (screen) {
        const QRect screenRect = screen->availableGeometry();
        setGeometry(screenRect);

        // Create the native surface here, at that geometry, and pin it once more
        // afterwards. A frameless translucent window that is first placed by show()
        // gets positioned by the window manager, which puts it on the primary display
        // and made the startup animation ignore the remembered monitor.
        createWinId();
        if (QWindow* handle = windowHandle()) {
            handle->setGeometry(screenRect);
        }

        m_contentRect = QRectF((screenRect.width() - SPLASH_WIDTH) / 2.0,
            (screenRect.height() - SPLASH_HEIGHT) / 2.0, SPLASH_WIDTH, SPLASH_HEIGHT);
        m_animatedRect = m_contentRect;
    }

    m_appearProgress = 0.0;
    m_isAppearing = true;
    m_statusText = QStringLiteral("Initializing...");

    // Pre-scaled once: the banner is far wider than the card and paintEvent runs on
    // every progress tick, so rescaling 1800px of source per frame is not worth it.
    const qreal logoDpr = screen ? screen->devicePixelRatio() : 1.0;
    // The opaque logo, not IconProvider::getApplicationLogoPixmap(): that one prefers
    // the transparent variant.
    QPixmap logo = ui::core::IconProvider::instance().getPixmap(
        ui::core::IconProvider::StandardIcon::OpaqueLogoIcon,
        QSize(qRound(kLogoSize * logoDpr), qRound(kLogoSize * logoDpr)));
    if (!logo.isNull()) {
        m_logoPixmap = logo;
        m_logoPixmap.setDevicePixelRatio(logoDpr);
    }

    QPixmap header(QStringLiteral(":/images/SplashScreenImage"));
    if (!header.isNull()) {
        const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
        m_headerImage = header.scaledToWidth(
            qRound((SPLASH_WIDTH - 2 * kImageMargin) * dpr), Qt::SmoothTransformation);
        m_headerImage.setDevicePixelRatio(dpr);
    }

    buildShadowImage();

    updateColors();
}

void SplashScreen::buildShadowImage()
{
    const int extent = static_cast<int>(kShadowExtent);
    const int imageW = SPLASH_WIDTH + extent * 2;
    const int imageH = SPLASH_HEIGHT + extent * 2;

    m_shadowImage = QImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
    if (m_shadowImage.isNull()) {
        return;
    }
    m_shadowImage.fill(Qt::transparent);

    // Built from the card's signed distance field instead of by blurring a rendered
    // rectangle: the falloff is analytic, so there are no kernel steps to quantise,
    // and it only has to run once.
    const qreal halfW = SPLASH_WIDTH / 2.0;
    const qreal halfH = SPLASH_HEIGHT / 2.0;
    const qreal peak = kShadowAlpha * 255.0;

    for (int y = 0; y < imageH; ++y) {
        auto* line = reinterpret_cast<QRgb*>(m_shadowImage.scanLine(y));
        const qreal py = y + 0.5 - extent - halfH;

        for (int x = 0; x < imageW; ++x) {
            const qreal px = x + 0.5 - extent - halfW;

            const qreal dx = qAbs(px) - (halfW - kCardRadius);
            const qreal dy = qAbs(py) - (halfH - kCardRadius);
            const qreal outsideX = qMax(dx, 0.0);
            const qreal outsideY = qMax(dy, 0.0);
            const qreal distance
                = std::hypot(outsideX, outsideY) + qMin(qMax(dx, dy), 0.0) - kCardRadius;

            qreal falloff = 1.0 - qMax(distance, 0.0) / kShadowExtent;
            if (falloff <= 0.0) {
                continue;
            }
            falloff = qPow(falloff, 2.6);

            const qreal dithered = peak * falloff + kBayer8[(y & 7) * 8 + (x & 7)];
            const int alpha = qBound(0, static_cast<int>(dithered), 255);
            if (alpha == 0) {
                continue;
            }

            // Premultiplied black: only the alpha channel carries the shadow.
            line[x] = qRgba(0, 0, 0, alpha);
        }
    }
}

void SplashScreen::paintShadow(QPainter& painter, const QRectF& drawRect) const
{
    if (m_shadowImage.isNull()) {
        return;
    }

    const qreal scale = drawRect.width() / SPLASH_WIDTH;
    const QRectF shadowRect(drawRect.x() - kShadowExtent * scale,
        drawRect.y() - kShadowExtent * scale + kShadowOffsetY * scale,
        m_shadowImage.width() * scale, m_shadowImage.height() * scale);

    painter.drawImage(shadowRect, m_shadowImage);
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
    scaleAnim->setDuration(anim::duration(durationMs));
    scaleAnim->setStartValue(0.0);
    scaleAnim->setEndValue(1.0);
    scaleAnim->setEasingCurve(QEasingCurve::OutCubic);

    auto* opacityAnim = new QPropertyAnimation(this, "contentOpacity", this);
    opacityAnim->setDuration(anim::duration(durationMs));
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

        // Replay an expand request that arrived while we were still appearing. Clear the
        // flag first so this call can't loop back into itself.
        if (m_expandDeferred) {
            const int deferredDurationMs = m_deferredExpandDurationMs;
            m_expandDeferred = false;
            expandToMainWindow(deferredDurationMs);
        }
    });

    anim::start(group, QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::startRectExpansion(int durationMs)
{
    m_startLocalRect = m_contentRect;
    m_targetLocalRect = QRectF(0, 0, width(), height());
    m_animatedRect = m_startLocalRect;

    m_isExpanding = true;
    m_isAppearing = false;

    // The signal this animation ends with is the only thing that closes the
    // splash, so with animations disabled it has to be delivered here rather
    // than left to a timer that would never be worth running.
    if (!anim::enabled()) {
        m_animatedRect = m_targetLocalRect;
        m_hasExpanded = true;
        m_isExpanding = false;
        update();
        emit expansionFinished();
        return;
    }

    const int fps = 60;
    const int frameTime = 1000 / fps;
    // Progress is driven by ELAPSED TIME, not by a frame count. Counting ticks
    // ties the animation's length to how often the GUI thread gets around to
    // this timer: under load it stretches without limit, and the signal this
    // animation ends with is the only thing that ever closes the splash. On
    // time it simply drops frames and still finishes when it should.
    const int totalDurationMs = qMax(1, durationMs);
    // Captured by value, not heap-allocated: QElapsedTimer is a trivially copyable
    // value type and elapsed() is const, so the lambda's own copy stays valid for
    // its whole life. A heap pointer here would leak if the widget (and its child
    // `timer`) were destroyed before progress reached 1.0 — e.g. the splash being
    // force-closed by the startup watchdog while this animation is still running.
    QElapsedTimer clock;
    clock.start();

    auto* timer = new QTimer(this);
    timer->setInterval(frameTime);

    connect(timer, &QTimer::timeout, this, [this, timer, clock, totalDurationMs]() {
        qreal progress
            = qMin(1.0, static_cast<qreal>(clock.elapsed()) / static_cast<qreal>(totalDurationMs));

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

            m_animatedRect = m_targetLocalRect;
            m_hasExpanded = true;
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
        // Can't start the fade/expand chain over the appearance animation's own timers.
        // Remember this request instead of dropping it; it is replayed once appearance
        // finishes (see the QParallelAnimationGroup::finished handler above).
        m_expandDeferred = true;
        m_deferredExpandDurationMs = durationMs;
        return;
    }

    m_expandRequested = true;
    m_pendingExpandDurationMs = durationMs;

    auto* fadeChrome = new QPropertyAnimation(this, "foregroundOpacity", this);
    fadeChrome->setDuration(anim::duration(400));
    fadeChrome->setStartValue(1.0);
    fadeChrome->setEndValue(0.0);
    fadeChrome->setEasingCurve(QEasingCurve::InCubic);

    connect(fadeChrome, &QPropertyAnimation::finished, this,
        [this]() { startRectExpansion(m_pendingExpandDurationMs); });

    anim::start(fadeChrome, QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::fadeOut(int durationMs)
{
    auto* effect = new QGraphicsOpacityEffect(this);
    setGraphicsEffect(effect);

    auto* animation = new QPropertyAnimation(effect, "opacity", this);
    animation->setDuration(anim::duration(durationMs));
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(animation, &QPropertyAnimation::finished, this, &QWidget::close);

    anim::start(animation, QAbstractAnimation::DeleteWhenStopped);
}

void SplashScreen::paintInterior(QPainter& painter) const
{
    const auto& colors = ui::core::ThemeManager::instance().colors();

    const qreal margin = kContentMargin;

    // --- header image, inset and rounded ------------------------------------
    qreal topMargin = margin;
    if (!m_headerImage.isNull()) {
        const qreal imageW = SPLASH_WIDTH - 2 * kImageMargin;
        const qreal imageH
            = imageW * static_cast<qreal>(m_headerImage.height()) / m_headerImage.width();
        const QRectF imageRect(kImageMargin, kImageMargin, imageW, imageH);

        QPainterPath imageClip;
        imageClip.addRoundedRect(imageRect, kImageRadius, kImageRadius);

        painter.save();
        painter.setClipPath(imageClip);
        painter.drawPixmap(imageRect, m_headerImage, QRectF(m_headerImage.rect()));
        painter.restore();

        // Everything else starts below the image, one image inset away from it.
        topMargin = imageRect.bottom() + kImageMargin;
    }

    // Logo, wordmark and version pill share one row height, so the pill's top
    // and bottom line up with the header on the left.
    const qreal headerH = kHeaderHeight;

    // --- version pill, pinned to the right, below the image ------------------
    const QString versionTag
        = QStringLiteral("v%1").arg(QApplication::applicationVersion()).toUpper();

    QFont badgeFont(ruwa::ui::core::FontFamilyNames::DMSans18pt, 10, QFont::DemiBold);
    badgeFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    const QFontMetricsF badgeFm(badgeFont);

    const qreal badgePadH = 10;
    const qreal badgeW = badgeFm.horizontalAdvance(versionTag) + badgePadH * 2;
    const qreal badgeH = qMax(headerH, badgeFm.height() + 6);
    const QRectF badgeRect(
        SPLASH_WIDTH - margin - badgeW, topMargin + (headerH - badgeH) / 2.0, badgeW, badgeH);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.primary);
    painter.drawRoundedRect(badgeRect, badgeH / 2, badgeH / 2);

    painter.setFont(badgeFont);
    painter.setPen(colors.textOnPrimary());
    painter.drawText(badgeRect, Qt::AlignCenter, versionTag);

    // --- logo + wordmark, pinned to the left, below the image ----------------
    qreal x = margin;
    if (!m_logoPixmap.isNull()) {
        const QRectF logoRect(x, topMargin + (headerH - kLogoSize) / 2.0, kLogoSize, kLogoSize);
        painter.drawPixmap(logoRect, m_logoPixmap, QRectF(m_logoPixmap.rect()));
        x += kLogoSize + 10;
    }

    const QString wordmark = QStringLiteral("Accretion Ruwa");
    const qreal titleAvail = qMax(0.0, badgeRect.left() - 16 - x);

    QFont titleFont(ruwa::ui::core::FontFamilyNames::InstrumentSerif, 21, QFont::Normal);
    // The wordmark is longer than the old one-word title, so give the point size
    // room to step down rather than letting the version pill collide with it.
    for (int pt = 21; pt > 12; --pt) {
        titleFont.setPointSize(pt);
        if (QFontMetricsF(titleFont).horizontalAdvance(wordmark) <= titleAvail) {
            break;
        }
    }

    painter.setFont(titleFont);
    painter.setPen(colors.text);
    const QString titleText = QFontMetricsF(titleFont).elidedText(
        wordmark, Qt::ElideRight, static_cast<int>(titleAvail));
    painter.drawText(
        QRectF(x, topMargin, titleAvail, headerH), Qt::AlignLeft | Qt::AlignVCenter, titleText);

    // --- credits, straight under the header ---------------------------------
    QFont creditsFont(ruwa::ui::core::FontFamilyNames::DMSans18pt, 9, QFont::Light);
    const QFontMetricsF creditsFm(creditsFont);

    const QStringList creditLines = { tr("Developer: %1").arg(ui::core::Credits::Developer),
        tr("Testers: %1").arg(ui::core::Credits::testers().join(QStringLiteral(", "))),
        tr("See the \"About\" section for more information.") };

    painter.setFont(creditsFont);
    painter.setPen(colors.textMuted);

    qreal creditY = topMargin + headerH + 20;
    for (const QString& line : creditLines) {
        const QRectF lineRect(margin, creditY, SPLASH_WIDTH - 2 * margin, creditsFm.height());
        painter.drawText(lineRect, Qt::AlignLeft | Qt::AlignVCenter, line);
        creditY += creditsFm.lineSpacing();
    }

    // --- status line, percentage and progress bar ---------------------------
    QFont statusFont(ruwa::ui::core::FontFamilyNames::DMSans18pt, 11, QFont::Light);
    const QFontMetricsF statusFm(statusFont);

    // Digits sit on the cap height, so matching point sizes would make the
    // percentage look bigger than the status line. Scale it down by the status
    // font's x-height/cap-height ratio instead, so the two read the same size.
    QFont percentFont(ruwa::ui::core::FontFamilyNames::DMSans18pt, 11, QFont::DemiBold);
    const qreal capH = qMax(1.0, statusFm.capHeight());
    percentFont.setPointSizeF(qMax(1.0, percentFont.pointSizeF() * (statusFm.xHeight() / capH)));
    const QFontMetricsF percentFm(percentFont);

    const qreal barH = 3;
    const qreal barR = barH / 2.0;
    const qreal barBottomPad = 30;
    const QRectF trackRect(
        margin, SPLASH_HEIGHT - barBottomPad - barH, SPLASH_WIDTH - 2 * margin, barH);

    const qreal rowH = qMax(statusFm.height(), percentFm.height());
    const QRectF textRow(margin, trackRect.top() - 12 - rowH, SPLASH_WIDTH - 2 * margin, rowH);

    // Percentage first: it owns the right edge, the status text gets what is left.
    const QString percentText = QStringLiteral("%1%").arg(m_targetProgress);
    const qreal percentW = percentFm.horizontalAdvance(percentText);

    painter.setFont(percentFont);
    painter.setPen(colors.textMuted);
    painter.drawText(QRectF(textRow.right() - percentW, textRow.top(), percentW, rowH),
        Qt::AlignLeft | Qt::AlignVCenter, percentText);

    // The emitted messages carry a trailing "..." of their own; the splash drops it
    // so the line reads as a label next to the percentage.
    QString status = m_statusText;
    while (status.endsWith(QLatin1Char('.')) || status.endsWith(QChar(0x2026))) {
        status.chop(1);
    }

    painter.setFont(statusFont);
    painter.setPen(colors.textMuted);
    painter.drawText(
        QRectF(textRow.left(), textRow.top(), qMax(0.0, textRow.width() - percentW - 16), rowH),
        Qt::AlignLeft | Qt::AlignVCenter, status);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.border);
    painter.drawRoundedRect(trackRect, barR, barR);

    const qreal fillW = trackRect.width() * (m_progressDisplay / 100.0);
    if (fillW > 0.05) {
        const qreal capR = qMin(barR, qMax(fillW * 0.5, 0.001));
        const QRectF fillRect(trackRect.left(), trackRect.top(), fillW, barH);
        QLinearGradient grad(trackRect.topLeft(), trackRect.topRight());
        grad.setColorAt(0, colors.primary);
        grad.setColorAt(1,
            ui::core::ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 0.82 : 1.12));
        painter.setBrush(grad);
        painter.drawRoundedRect(fillRect, capR, capR);
    }
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

        drawRect = QRectF((width() - scaledWidth) / 2.0, (height() - scaledHeight) / 2.0,
            scaledWidth, scaledHeight);
    }

    if (m_hasExpanded) {
        painter.fillRect(rect(), colors.background);
        return;
    }

    if (m_isExpanding) {
        qreal expandProgress = 0.0;
        if (qAbs(m_targetLocalRect.width() - m_startLocalRect.width()) > 0.001) {
            expandProgress = (m_animatedRect.width() - m_startLocalRect.width())
                / (m_targetLocalRect.width() - m_startLocalRect.width());
        }
        expandProgress = qBound(0.0, expandProgress, 1.0);
        const qreal radiusF = kCardRadius * (1.0 - expandProgress);

        QColor bg = colors.background;
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(drawRect, radiusF, radiusF);
        return;
    }

    const QColor bgColor = colors.background;
    const qreal cornerR = kCardRadius * drawRect.width() / SPLASH_WIDTH;

    // One path for fill + strokes so AA fill and outline share the same edge (inset stroke
    // vs full fill was letting background fringe show outside the border on corners).
    QPainterPath cardPath;
    cardPath.addRoundedRect(drawRect, cornerR, cornerR);

    // Room for the shadow, which reaches well past the card itself.
    const qreal shadowPad = kShadowExtent + kShadowOffsetY + 2;
    const QRectF layerRect = drawRect.adjusted(-shadowPad, -shadowPad, shadowPad, shadowPad);

    if (m_isAppearing) {
        // Shadow, background and content fade in together, as one flat image.
        paintLayer(painter, layerRect, m_contentOpacity, [&](QPainter& p) {
            paintShadow(p, drawRect);
            p.fillPath(cardPath, bgColor);
            paintCardForeground(p, drawRect);
        });
        return;
    }

    // The shadow goes out with the chrome, so it is gone before the card starts
    // morphing into the window and nothing trails behind the growing rectangle.
    if (m_foregroundOpacity > 0.001) {
        painter.save();
        painter.setOpacity(m_foregroundOpacity);
        paintShadow(painter, drawRect);
        painter.restore();
    }

    painter.fillPath(cardPath, bgColor);

    if (m_foregroundOpacity <= 0.001) {
        return;
    }

    // On the way out the card itself stays put and only its contents fade - again as
    // one group, so the elements do not dissolve into each other.
    paintLayer(painter, layerRect, m_foregroundOpacity,
        [&](QPainter& p) { paintCardForeground(p, drawRect); });
}

void SplashScreen::paintCardForeground(QPainter& painter, const QRectF& drawRect) const
{
    painter.save();

    const qreal sx = drawRect.width() / SPLASH_WIDTH;
    const qreal sy = drawRect.height() / SPLASH_HEIGHT;
    painter.translate(drawRect.topLeft());
    painter.scale(sx, sy);

    paintInterior(painter);

    painter.restore();
}

} // namespace ruwa::ui::windows
