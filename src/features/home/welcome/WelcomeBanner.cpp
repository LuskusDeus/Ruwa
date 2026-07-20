// SPDX-License-Identifier: MPL-2.0

// WelcomeBanner.cpp
#include "WelcomeBanner.h"
#include "WelcomeBannerButton.h"
#include "WelcomeBannerImageCatalog.h"
#include "WelcomeUpdatePanel.h"
#include "features/settings/SettingsManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"
#include "shared/resources/ResourceManager.h"

#include <QCoreApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QFileInfo>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QHideEvent>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

namespace ruwa::ui::widgets {

namespace {
const int BASE_HEIGHT = 320;
const int BASE_MIN_WIDTH = 400;
const int BASE_LAYOUT_PADDING = 40;
const int BASE_LAYOUT_SPACING = 16;
const int BASE_BUTTON_SPACING = 12;
const int BASE_TITLE_FONT_SIZE = 32;
const int BASE_SUBTITLE_FONT_SIZE = 14;
const int BASE_BORDER_RADIUS = 12;
const int BASE_OPEN_BUTTON_BLUR_PAD = 10;
const int OPEN_BUTTON_BLUR_DOWNSCALE = 4;

// Update-panel split
const int BASE_SPLIT_GAP = 16;
const int BASE_PANEL_MIN_WIDTH = 240;
const int SPLIT_ANIM_DURATION = 420;
// Delay before the reveal so it doesn't run underneath the home-tab entrance
// animation (sections slide in over ~420ms + stagger). Measured from the first
// time the banner is actually shown on screen.
const int SPLIT_REVEAL_DELAY = 650;
constexpr qreal PANEL_WIDTH_FRACTION = 1.0 / 3.0;

int panelTargetWidth(int bannerWidth, const ruwa::ui::core::ThemeManager& theme)
{
    return qMax(theme.scaled(BASE_PANEL_MIN_WIDTH), qRound(bannerWidth * PANEL_WIDTH_FRACTION));
}

bool keysEqual(const QString& a, const QString& b)
{
    if (a == b) {
        return true;
    }
    if (a.startsWith(QLatin1String(":/")) || b.startsWith(QLatin1String(":/"))) {
        return false;
    }
    const QFileInfo fa(a);
    const QFileInfo fb(b);
    const QString ca = fa.canonicalFilePath();
    const QString cb = fb.canonicalFilePath();
    if (!ca.isEmpty() && !cb.isEmpty()) {
        return ca == cb;
    }
    return fa.absoluteFilePath() == fb.absoluteFilePath();
}

bool isLightBannerForKey(const QString& key, const QPixmap& pm)
{
    if (key
        == ruwa::ui::core::ResourceManager::instance().getBuiltInImagePath(
            ruwa::ui::core::ResourceManager::BuiltInImage::WelcomeBannerAlt)) {
        return true;
    }
    if (pm.isNull()) {
        return false;
    }
    QImage img = pm.toImage().convertToFormat(QImage::Format_RGB32);
    if (img.isNull()) {
        return false;
    }
    img = img.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    qint64 sum = 0;
    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const QRgb c = img.pixel(x, y);
            sum += qRed(c) + qGreen(c) + qBlue(c);
        }
    }
    const int n = w * h * 3;
    return n > 0 && (sum / n) > 180;
}

void drawBlurredTintedPlate(QImage& target, const QWidget* targetWidget,
    const QWidget* sourceWidget, int blurPad, const QColor& tintTop, const QColor& tintBottom)
{
    if (!targetWidget || !sourceWidget || !sourceWidget->isVisible() || target.isNull()) {
        return;
    }

    const QRect targetBounds(QPoint(0, 0), target.size());
    const QRect plateRect(sourceWidget->mapTo(targetWidget, QPoint(0, 0)), sourceWidget->size());
    if (!targetBounds.intersects(plateRect)) {
        return;
    }

    const QRect sampledRect
        = plateRect.adjusted(-blurPad, -blurPad, blurPad, blurPad).intersected(targetBounds);
    if (sampledRect.isEmpty()) {
        return;
    }

    const QImage sampled = target.copy(sampledRect);
    const QSize blurSize(qMax(1, sampled.width() / OPEN_BUTTON_BLUR_DOWNSCALE),
        qMax(1, sampled.height() / OPEN_BUTTON_BLUR_DOWNSCALE));
    const QImage blurred
        = sampled.scaled(blurSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
              .scaled(sampled.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clipPath;
    const QRectF plate(plateRect.adjusted(1, 1, -1, -1));
    const qreal radius = plate.height() * 0.5;
    clipPath.addRoundedRect(plate, radius, radius);
    painter.setClipPath(clipPath);

    const QRect sourceRect(plateRect.topLeft() - sampledRect.topLeft(), plateRect.size());
    painter.drawImage(QRectF(plateRect), blurred, QRectF(sourceRect));

    QLinearGradient tint(plate.topLeft(), plate.bottomLeft());
    tint.setColorAt(0.0, tintTop);
    tint.setColorAt(1.0, tintBottom);
    painter.fillPath(clipPath, tint);
}
} // namespace

WelcomeBanner::WelcomeBanner(QWidget* parent)
    : QWidget(parent)
{
    loadBackgroundImage();
    setupUI();

    m_splitAnimation = new QPropertyAnimation(this, "splitProgress", this);
    m_splitAnimation->setDuration(SPLIT_ANIM_DURATION);
    m_splitAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_revealTimer = new QTimer(this);
    m_revealTimer->setSingleShot(true);
    m_revealTimer->setInterval(SPLIT_REVEAL_DELAY);
    connect(m_revealTimer, &QTimer::timeout, this, [this]() {
        if (m_updatePanelRequested && !m_updatePanelRevealed && isVisible()) {
            m_updatePanelRevealed = true;
            animateSplitTo(1.0);
        }
    });

    // Apply initial scaled sizes
    updateScaledSizes();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &WelcomeBanner::onThemeChanged);

    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::welcomeBannerBackgroundSettingsChanged, this,
        &WelcomeBanner::reloadFromSettings);
}

void WelcomeBanner::reloadFromSettings()
{
    loadBackgroundImage();
    applyBackgroundUiHints();
    update();
}

void WelcomeBanner::loadBackgroundImage()
{
    const auto& app = ruwa::core::SettingsManager::instance().settings();

    QStringList pool;
    // Add built-in banners except hidden easter egg
    const QStringList builtins = welcomeBannerBuiltinImageKeys();
    for (const QString& key : builtins) {
        if (key != QLatin1String(":/images/Banner1April")) {
            pool.append(key);
        }
    }

    for (const QString& p : app.appearance.welcomeBannerCustomPaths) {
        if (p.startsWith(QLatin1String(":/"))) {
            continue;
        }
        const QString abs = QFileInfo(p).absoluteFilePath();
        if (!abs.isEmpty() && QFileInfo::exists(abs)) {
            pool.append(abs);
        }
    }

    if (pool.isEmpty()) {
        m_backgroundImage = QPixmap();
        m_backgroundCropNorm = QRectF(0.0, 0.0, 1.0, 1.0);
        m_lightBackgroundImage = false;
        return;
    }

    QString chosen;
    if (app.appearance.welcomeBannerRandomize) {
        chosen = pool[QRandomGenerator::global()->bounded(pool.size())];
        ruwa::core::SettingsManager::instance().syncWelcomeBannerDisplayedImageKey(chosen);
    } else {
        QString fixed = app.appearance.welcomeBannerFixedKey;
        if (fixed.isEmpty()) {
            fixed = welcomeBannerDefaultFixedKey();
        }
        // Easter egg banners are excluded from the pool but must still be loadable when fixed
        const QStringList allBuiltins = welcomeBannerBuiltinImageKeys();
        if (!pool.contains(fixed) && allBuiltins.contains(fixed)) {
            chosen = fixed;
        } else {
            bool found = false;
            for (const QString& k : pool) {
                if (keysEqual(k, fixed)) {
                    chosen = k;
                    found = true;
                    break;
                }
            }
            if (!found) {
                chosen = pool.first();
            }
        }
    }

    m_backgroundImage = QPixmap(chosen);
    // Custom images may carry a user-chosen crop region; built-ins have none (full rect).
    m_backgroundCropNorm = ruwa::core::SettingsManager::instance().welcomeBannerCropFor(chosen);
    m_lightBackgroundImage = isLightBannerForKey(chosen, m_backgroundImage);
}

bool WelcomeBanner::effectiveLightBannerUi() const
{
    const auto& app = ruwa::core::SettingsManager::instance().settings();
    bool v = m_lightBackgroundImage;
    if (app.appearance.welcomeBannerTextColorMode == 1) {
        v = !v;
    }
    return v;
}

void WelcomeBanner::applyBackgroundUiHints()
{
    const bool lightUi = effectiveLightBannerUi();
    if (m_createButton) {
        m_createButton->setLightBannerContext(lightUi);
    }
    if (m_openButton) {
        m_openButton->setLightBannerContext(lightUi);
    }
    applyBannerLabelColors();
}

void WelcomeBanner::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    m_titleLabel
        = new QLabel(tr("Digital Painting Reimagined"), this);
    m_titleLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_titleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    m_mainLayout->addWidget(m_titleLabel);

    m_subtitleLabel
        = new QLabel(tr("Free, open-source, and limitless."), this);
    m_subtitleLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_subtitleLabel->setFont(
        theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_SUBTITLE_FONT_SIZE)));
    m_mainLayout->addWidget(m_subtitleLabel);

    m_mainLayout->addStretch();

    m_buttonContainer = new QWidget(this);
    m_buttonContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_buttonLayout = new QHBoxLayout(m_buttonContainer);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);

    m_createButton = new WelcomeBannerButton(tr("Create Project"),
        WelcomeBannerButton::ButtonStyle::Primary, m_buttonContainer);
    m_createButton->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::FileNew));
    connect(
        m_createButton, &WelcomeBannerButton::clicked, this, &WelcomeBanner::createProjectClicked);
    m_buttonLayout->addWidget(m_createButton);

    m_openButton = new WelcomeBannerButton(tr("Open Project"),
        WelcomeBannerButton::ButtonStyle::Secondary, m_buttonContainer);
    m_openButton->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::OpenedFolder));
    m_openButton->setSecondaryIdleShadowAlpha(16);
    connect(m_openButton, &WelcomeBannerButton::clicked, this, &WelcomeBanner::openProjectClicked);
    m_buttonLayout->addWidget(m_openButton);

    m_buttonLayout->addStretch();

    m_mainLayout->addWidget(m_buttonContainer);

    // Update-available side panel (hidden until showUpdatePanel()).
    m_updatePanel = new WelcomeUpdatePanel(this);
    m_updatePanel->hide();
    connect(m_updatePanel, &WelcomeUpdatePanel::updateRequested, this,
        &WelcomeBanner::updateActionRequested);
    connect(m_updatePanel, &WelcomeUpdatePanel::dismissed, this, [this]() {
        emit updateDismissed();
        hideUpdatePanel();
    });

    applyBackgroundUiHints();
}

void WelcomeBanner::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void WelcomeBanner::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(tr("Digital Painting Reimagined"));
    if (m_subtitleLabel)
        m_subtitleLabel->setText(
            tr("Free, open-source, and limitless."));
    if (m_createButton) {
        m_createButton->setText(tr("Create Project"));
        m_createButton->syncSizeToText();
    }
    if (m_openButton) {
        m_openButton->setText(tr("Open Project"));
        m_openButton->syncSizeToText();
    }
}

void WelcomeBanner::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    // Widget size
    setFixedHeight(theme.scaled(BASE_HEIGHT));
    setMinimumWidth(theme.scaled(BASE_MIN_WIDTH));

    const int padding = theme.scaled(BASE_LAYOUT_PADDING);
    const int spacing = theme.scaled(BASE_LAYOUT_SPACING);
    if (m_mainLayout) {
        m_mainLayout->setContentsMargins(padding, padding, padding, padding);
        m_mainLayout->setSpacing(spacing);
    }

    if (m_buttonLayout) {
        m_buttonLayout->setSpacing(theme.scaled(BASE_BUTTON_SPACING));
    }

    if (m_titleLabel) {
        m_titleLabel->setFont(
            theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    }

    if (m_subtitleLabel) {
        m_subtitleLabel->setFont(
            theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_SUBTITLE_FONT_SIZE)));
    }

    updateSplitLayout();
}

void WelcomeBanner::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateSplitLayout();
}

int WelcomeBanner::bannerCardWidth() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(BASE_SPLIT_GAP);
    const int panelW = panelTargetWidth(width(), theme);
    const int cardW = width() - qRound((panelW + gap) * m_splitProgress);
    return qMax(0, cardW);
}

void WelcomeBanner::updateSplitLayout()
{
    if (!m_updatePanel || !m_mainLayout) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int panelW = panelTargetWidth(width(), theme);
    const int cardW = bannerCardWidth();

    // Panel slides in from the right edge so the banner card "opens" to reveal it.
    const int panelX = width() - qRound(panelW * m_splitProgress);
    m_updatePanel->setGeometry(panelX, 0, panelW, height());
    m_updatePanel->setVisible(m_splitProgress > 0.001);

    // Keep the banner's own content (title / buttons) within the left card.
    const int pad = theme.scaled(BASE_LAYOUT_PADDING);
    const int rightInset = qMax(0, width() - cardW);
    m_mainLayout->setContentsMargins(pad, pad, pad + rightInset, pad);

    update();
}

void WelcomeBanner::setSplitProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (m_splitProgress == progress) {
        return;
    }
    m_splitProgress = progress;
    updateSplitLayout();
}

void WelcomeBanner::animateSplitTo(qreal target)
{
    if (!m_splitAnimation) {
        setSplitProgress(target);
        return;
    }
    m_splitAnimation->stop();
    m_splitAnimation->setStartValue(m_splitProgress);
    m_splitAnimation->setEndValue(target);
    m_splitAnimation->start();
}

void WelcomeBanner::startRevealCountdown()
{
    if (!m_updatePanelRequested || m_updatePanelRevealed || !m_revealTimer) {
        return;
    }
    // Restart on each (re)show so the countdown is anchored to the latest time the
    // banner actually appears — not to a premature show that happens before the
    // home-tab entrance animation. The delay lets that entrance settle first.
    m_revealTimer->start();
}

void WelcomeBanner::showUpdatePanel(const QString& version, const QString& description)
{
    m_updatePanelRequested = true;
    m_updatePanelRevealed = false;
    if (m_updatePanel) {
        m_updatePanel->setUpdateVersion(version);
        m_updatePanel->setReleaseDescription(description);
    }

    // If the banner isn't on screen yet, the reveal is (re)started from showEvent().
    if (isVisible()) {
        startRevealCountdown();
    }
}

void WelcomeBanner::hideUpdatePanel()
{
    m_updatePanelRequested = false;
    m_updatePanelRevealed = false;
    if (m_revealTimer) {
        m_revealTimer->stop();
    }
    animateSplitTo(0.0);
}

void WelcomeBanner::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // (Re)anchor the reveal to this show. If the startup controller hid the banner
    // and is now re-showing it with its slide-in, this is the show that counts.
    startRevealCountdown();
}

void WelcomeBanner::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    // While the panel is still pending (user hasn't dismissed it) and hasn't been
    // revealed yet, a hide means something is about to re-show us — e.g. the
    // startup animation controller's hide/show cycle. Reset so the reveal replays
    // cleanly from the next (real) show instead of completing off-screen.
    if (m_updatePanelRequested && !m_updatePanelRevealed) {
        if (m_revealTimer) {
            m_revealTimer->stop();
        }
        if (m_splitAnimation) {
            m_splitAnimation->stop();
        }
        setSplitProgress(0.0);
    }
}

void WelcomeBanner::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int borderRadius = theme.scaled(BASE_BORDER_RADIUS);
    const QSize sz = size();
    const int cardW = qMax(1, bannerCardWidth());

    // Opaque base across the whole widget. When the panel is open, the area to
    // the right of the banner card (gap + panel backdrop) stays plain background
    // so it blends with the host page — only the left card is rounded.
    QImage finalImg(sz, QImage::Format_ARGB32_Premultiplied);
    finalImg.fill(colors.background.rgba());

    // Banner card layer (left region). The background image is always scaled to
    // the full banner size and anchored to its right edge, so shrinking the card
    // crops the image from the right rather than rescaling it.
    QImage card(QSize(cardW, sz.height()), QImage::Format_ARGB32_Premultiplied);
    card.fill(colors.background.rgba());
    {
        QPainter cardPainter(&card);
        cardPainter.setRenderHint(QPainter::Antialiasing);
        cardPainter.setRenderHint(QPainter::SmoothPixmapTransform);

        if (!m_backgroundImage.isNull()) {
            QPixmap sourceBg = m_backgroundImage;
            const QRectF& cn = m_backgroundCropNorm;
            const bool fullCrop = !cn.isValid()
                || (qFuzzyIsNull(cn.x()) && qFuzzyIsNull(cn.y()) && qFuzzyCompare(cn.width(), 1.0)
                    && qFuzzyCompare(cn.height(), 1.0));
            if (!fullCrop) {
                const QRect px(qRound(cn.x() * m_backgroundImage.width()),
                    qRound(cn.y() * m_backgroundImage.height()),
                    qRound(cn.width() * m_backgroundImage.width()),
                    qRound(cn.height() * m_backgroundImage.height()));
                const QRect clamped = px.intersected(m_backgroundImage.rect());
                if (!clamped.isEmpty()) {
                    sourceBg = m_backgroundImage.copy(clamped);
                }
            }

            QPixmap scaledBg
                = sourceBg.scaled(sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int x = width() - scaledBg.width();
            int y = (height() - scaledBg.height()) / 2;
            if (x < -scaledBg.width() / 2) {
                x = -scaledBg.width() / 2;
            }
            cardPainter.drawPixmap(x, y, scaledBg);
        }
        cardPainter.end();
    }

    const int blurPad = theme.scaled(BASE_OPEN_BUTTON_BLUR_PAD);
    const bool lightUi = effectiveLightBannerUi();
    const QColor tintTop = lightUi ? QColor(255, 255, 255, 25) : QColor(24, 34, 58, 70);
    const QColor tintBottom = lightUi ? QColor(255, 255, 255, 25) : QColor(8, 13, 28, 70);
    drawBlurredTintedPlate(card, this, m_openButton, blurPad, tintTop, tintBottom);

    // Round the card with a smooth alpha mask (no clipping artifacts).
    QImage maskBuffer(card.size(), QImage::Format_ARGB32_Premultiplied);
    maskBuffer.fill(Qt::transparent);
    {
        QPainter maskPainter(&maskBuffer);
        maskPainter.setRenderHint(QPainter::Antialiasing);
        QPainterPath maskPath;
        maskPath.addRoundedRect(QRectF(0, 0, cardW, sz.height()), borderRadius, borderRadius);
        maskPainter.fillPath(maskPath, Qt::white);
    }
    {
        QPainter cardPainter(&card);
        cardPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        cardPainter.drawImage(0, 0, maskBuffer);
    }

    {
        QPainter finalPainter(&finalImg);
        finalPainter.drawImage(0, 0, card);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(0, 0, finalImg);
}

void WelcomeBanner::applyBannerLabelColors()
{
    if (!m_titleLabel || !m_subtitleLabel) {
        return;
    }

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    using TC = ruwa::ui::core::ThemeColors;

    if (effectiveLightBannerUi()) {
        const QColor title = colors.textOnPrimary();
        const QColor subtitle = TC::adjustBrightness(title, 2.55);
        m_titleLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }").arg(title.name()));
        m_subtitleLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }").arg(subtitle.name()));
    } else {
        m_titleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name()));
        m_subtitleLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.textMuted.name()));
    }
}

void WelcomeBanner::updateThemeColors()
{
    applyBannerLabelColors();
    update();
}

void WelcomeBanner::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::widgets
