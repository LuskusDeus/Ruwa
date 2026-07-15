// SPDX-License-Identifier: MPL-2.0

// WelcomeUpdatePanel.cpp
#include "WelcomeUpdatePanel.h"
#include "WelcomeBannerButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/ResourceManager.h"
#include "shared/style/PaintingUtils.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace ruwa::ui::widgets {

namespace {
const int BASE_LAYOUT_PADDING = 24;
const int BASE_LAYOUT_SPACING = 8;
const int BASE_BUTTON_SPACING = 8;
const int BASE_EYEBROW_FONT_SIZE = 10;
const int BASE_TITLE_FONT_SIZE = 19;
const int BASE_DESC_FONT_SIZE = 12;
const int BASE_BORDER_RADIUS = 12;
const int BACKDROP_BLUR_RADIUS = 25;
constexpr qreal BUTTON_SIZE_SCALE = 0.85;
/// How visible the blurred banner image is behind the gradient (0..1).
constexpr qreal BACKDROP_OPACITY = 0.5;
/// The gradient is kept but weakened to half strength.
constexpr qreal GRADIENT_OPACITY = 0.5;

// Per-pixel hash noise for ordered-free dithering — breaks up the smooth-gradient
// banding that 8-bit quantisation would otherwise produce, while staying below
// the perceptual threshold.
quint32 pixelNoiseHash(int x, int y)
{
    quint32 h = static_cast<quint32>(x) * 374761393u + static_cast<quint32>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

qreal pixelNoise01(int x, int y)
{
    return qreal(pixelNoiseHash(x, y) & 0x00ffffffu) / qreal(0x01000000u);
}

/// Vertical gradient rendered to a dithered ARGB32 image (straight alpha), so it
/// composites with smooth, band-free transitions.
QImage buildDitheredVerticalGradient(const QSize& logicalSize, qreal dpr, const QColor& topColor,
    const QColor& bottomColor, int alpha)
{
    const int W = qMax(1, qRound(logicalSize.width() * dpr));
    const int H = qMax(1, qRound(logicalSize.height() * dpr));

    QImage img(W, H, QImage::Format_ARGB32);
    img.setDevicePixelRatio(dpr);

    const qreal tr = topColor.red(), tg = topColor.green(), tb = topColor.blue();
    const qreal br = bottomColor.red(), bg = bottomColor.green(), bb = bottomColor.blue();
    const int a = qBound(0, alpha, 255);

    for (int y = 0; y < H; ++y) {
        const qreal t = (H > 1) ? qreal(y) / qreal(H - 1) : 0.0;
        const qreal rExact = tr + (br - tr) * t;
        const qreal gExact = tg + (bg - tg) * t;
        const qreal bExact = tb + (bb - tb) * t;

        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < W; ++x) {
            const qreal n = pixelNoise01(x, y) - 0.5; // [-0.5, 0.5]
            const int r = qBound(0, qRound(rExact + n), 255);
            const int g = qBound(0, qRound(gExact + n), 255);
            const int b = qBound(0, qRound(bExact + n), 255);
            line[x] = qRgba(r, g, b, a);
        }
    }

    return img;
}
} // namespace

WelcomeUpdatePanel::WelcomeUpdatePanel(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    loadBackgroundImage();
    setupUI();
    updateScaledSizes();
    applyThemeColors();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &WelcomeUpdatePanel::onThemeChanged);
}

void WelcomeUpdatePanel::setupUI()
{
    const char* ctx = metaObject()->className();
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    m_eyebrowLabel = new QLabel(QCoreApplication::translate(ctx, "UPDATE AVAILABLE"), this);
    m_eyebrowLabel->setAttribute(Qt::WA_TranslucentBackground);
    QFont eyebrowFont
        = theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_EYEBROW_FONT_SIZE));
    eyebrowFont.setWeight(QFont::DemiBold);
    eyebrowFont.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1));
    m_eyebrowLabel->setFont(eyebrowFont);
    m_mainLayout->addWidget(m_eyebrowLabel);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_titleLabel->setWordWrap(true);
    QFont titleFont = theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE));
    m_titleLabel->setFont(titleFont);
    m_mainLayout->addWidget(m_titleLabel);

    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setFont(
        theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_DESC_FONT_SIZE)));
    m_mainLayout->addWidget(m_descriptionLabel);

    m_mainLayout->addStretch();

    m_buttonContainer = new QWidget(this);
    m_buttonContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_buttonLayout = new QHBoxLayout(m_buttonContainer);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);

    m_updateButton = new WelcomeBannerButton(QCoreApplication::translate(ctx, "Update"),
        WelcomeBannerButton::ButtonStyle::Primary, m_buttonContainer);
    m_updateButton->setSizeScale(BUTTON_SIZE_SCALE);
    connect(
        m_updateButton, &WelcomeBannerButton::clicked, this, &WelcomeUpdatePanel::updateRequested);
    m_buttonLayout->addWidget(m_updateButton);

    m_dismissButton = new WelcomeBannerButton(QCoreApplication::translate(ctx, "No thanks"),
        WelcomeBannerButton::ButtonStyle::Secondary, m_buttonContainer);
    m_dismissButton->setSizeScale(BUTTON_SIZE_SCALE);
    m_dismissButton->setSecondaryIdleShadowAlpha(16);
    connect(m_dismissButton, &WelcomeBannerButton::clicked, this, &WelcomeUpdatePanel::dismissed);
    m_buttonLayout->addWidget(m_dismissButton);

    m_buttonLayout->addStretch();

    m_mainLayout->addWidget(m_buttonContainer);

    retranslateUi();
}

void WelcomeUpdatePanel::setUpdateVersion(const QString& version)
{
    if (m_updateVersion == version) {
        return;
    }
    m_updateVersion = version;
    retranslateUi();
}

void WelcomeUpdatePanel::setReleaseDescription(const QString& description)
{
    if (m_releaseDescription == description) {
        return;
    }
    m_releaseDescription = description;
    retranslateUi();
}

void WelcomeUpdatePanel::loadBackgroundImage()
{
    m_backgroundImage = QPixmap(ruwa::ui::core::ResourceManager::instance().getBuiltInImagePath(
        ruwa::ui::core::ResourceManager::BuiltInImage::UpdateMessageBanner));
    rebuildBlurredBackdrop();
    update();
}

void WelcomeUpdatePanel::rebuildBlurredBackdrop()
{
    if (m_backgroundImage.isNull() || width() <= 0 || height() <= 0) {
        m_blurredBackdrop = QPixmap();
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    // Scale to fill the panel (same right-anchored expand as the banner) then blur.
    QPixmap scaled = m_backgroundImage.scaled(
        size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_blurredBackdrop
        = ruwa::ui::painting::blurSnapshotPixmap(scaled, theme.scaled(BACKDROP_BLUR_RADIUS));
}

void WelcomeUpdatePanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (event->size() != event->oldSize()) {
        rebuildBlurredBackdrop();
    }
}

void WelcomeUpdatePanel::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void WelcomeUpdatePanel::retranslateUi()
{
    const char* ctx = metaObject()->className();

    if (m_eyebrowLabel) {
        m_eyebrowLabel->setText(QCoreApplication::translate(ctx, "UPDATE AVAILABLE"));
    }
    if (m_titleLabel) {
        m_titleLabel->setText(m_updateVersion.isEmpty()
                ? QCoreApplication::translate(ctx, "A new version of Ruwa is ready")
                : QCoreApplication::translate(ctx, "Ruwa %1 is ready").arg(m_updateVersion));
    }
    if (m_descriptionLabel) {
        m_descriptionLabel->setText(m_releaseDescription.isEmpty()
                ? QCoreApplication::translate(ctx, "Update to get the latest features and fixes.")
                : m_releaseDescription);
    }
    if (m_updateButton) {
        m_updateButton->setText(QCoreApplication::translate(ctx, "Update"));
        m_updateButton->syncSizeToText();
    }
    if (m_dismissButton) {
        m_dismissButton->setText(QCoreApplication::translate(ctx, "No thanks"));
        m_dismissButton->syncSizeToText();
    }
}

void WelcomeUpdatePanel::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    const int padding = theme.scaled(BASE_LAYOUT_PADDING);
    if (m_mainLayout) {
        m_mainLayout->setContentsMargins(padding, padding, padding, padding);
        m_mainLayout->setSpacing(theme.scaled(BASE_LAYOUT_SPACING));
    }
    if (m_buttonLayout) {
        m_buttonLayout->setSpacing(theme.scaled(BASE_BUTTON_SPACING));
    }

    if (m_eyebrowLabel) {
        QFont eyebrowFont
            = theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_EYEBROW_FONT_SIZE));
        eyebrowFont.setWeight(QFont::DemiBold);
        eyebrowFont.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1));
        m_eyebrowLabel->setFont(eyebrowFont);
    }
    if (m_titleLabel) {
        m_titleLabel->setFont(
            theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    }
    if (m_descriptionLabel) {
        m_descriptionLabel->setFont(
            theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_DESC_FONT_SIZE)));
    }
}

void WelcomeUpdatePanel::applyThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    if (m_eyebrowLabel) {
        m_eyebrowLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.primary.name()));
    }
    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name()));
    }
    if (m_descriptionLabel) {
        m_descriptionLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.textMuted.name()));
    }
}

void WelcomeUpdatePanel::onThemeChanged()
{
    updateScaledSizes();
    applyThemeColors();
    rebuildBlurredBackdrop();
    update();
}

void WelcomeUpdatePanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int radius = theme.scaled(BASE_BORDER_RADIUS);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), radius, radius);
    painter.setClipPath(clip);

    using TC = ruwa::ui::core::ThemeColors;

    // Base fill so the card stays opaque where the image is faint / absent.
    const QColor base = colors.isDark ? colors.surface : colors.surfaceElevated();
    painter.fillRect(rect(), base);

    // Barely-visible blurred banner image behind everything.
    if (!m_blurredBackdrop.isNull()) {
        painter.setOpacity(BACKDROP_OPACITY);
        const QPixmap& bd = m_blurredBackdrop;
        const int x = width() - bd.width();
        const int y = (height() - bd.height()) / 2;
        painter.drawPixmap(x, y, bd);
        painter.setOpacity(1.0);
    }

    // Primary-tinted gradient (bright at top), kept but weakened to ~50%.
    // Rendered as a dithered image so the subtle top→bottom transition doesn't
    // band when quantised to 8-bit and composited over the blurred backdrop.
    const QColor top
        = TC::interpolate(colors.surfaceElevated(), colors.primary, colors.isDark ? 0.10 : 0.07);
    const QColor bottom = colors.isDark ? colors.surface : colors.surfaceElevated();
    const QImage gradient = buildDitheredVerticalGradient(
        size(), devicePixelRatioF(), top, bottom, qRound(GRADIENT_OPACITY * 255.0));
    painter.drawImage(QPoint(0, 0), gradient);

    painter.setClipping(false);
    ruwa::ui::painting::drawGradientBorder(painter, QRectF(rect()), radius,
        colors.borderSubtleHover(),
        TC::withAlpha(colors.borderSubtle(), colors.borderSubtle().alpha() / 2));
}

} // namespace ruwa::ui::widgets
