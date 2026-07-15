// SPDX-License-Identifier: MPL-2.0

// UpdateMessageOverlay.cpp
#include "UpdateMessageOverlay.h"
#include "commands/ShortcutManager.h"
#include "features/home/welcome/WelcomeBannerButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/ResourceManager.h"
#include "shared/style/PaintingUtils.h"
#include "RuwaBuildConfig.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QEvent>
#include <QResizeEvent>
#include <QGraphicsOpacityEffect>
#include <QGraphicsBlurEffect>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QImage>
#include <QPalette>
#include <QRegion>
#include <QSizePolicy>
#include <QCoreApplication>
#include <QApplication>
#include <QFontMetrics>
#include <QWindow>
#include <QtMath>
#include <QRgba64>

namespace ruwa::ui::widgets {

namespace {

// 16:9 aspect ratio, ~900x506
constexpr int CardWidth = 900;
constexpr int CardHeight = 506;
constexpr int CardRadius = 12;
constexpr int CardPadding = 24;
constexpr int TextAreaPadding = 32;
constexpr int ImageAreaWidth = 380; // 3:4 for height 506 (380 = 506*3/4)
constexpr int ImagePadding = 8;
constexpr int GlassBlurRadius = 45;
constexpr int DateFontSize = 8;
constexpr int VersionFontSize = 54;
constexpr int StatusFontSize = 8;
constexpr int HeroTitleFontSize = 17;
constexpr int BodyFontSize = 10;
constexpr int BadgeFontSize = 8;
constexpr int BadgeMinWidth = 76;
constexpr int BadgePaddingH = 8;
constexpr int BadgeColumnExtraWidth = 4;
constexpr int ButtonSpacing = 12;

enum class ChangelogBadge { New, Updated, BugFix, Improved, Deprecated };

QString displayReleaseVersion()
{
    QString version = QApplication::applicationVersion().trimmed();
    if (version.isEmpty()) {
        version = QStringLiteral(RUWA_APPLICATION_VERSION);
    }

    const int suffixIndex = version.indexOf(QLatin1Char('-'));
    if (suffixIndex > 0) {
        version = version.left(suffixIndex);
    }
    return version;
}

QColor darkenedPrimary(const ruwa::ui::core::ThemeColors& colors)
{
    return ruwa::ui::core::ThemeColors::adjustBrightness(
        colors.primary, colors.isDark ? 0.72 : 0.58);
}

QString changelogBadgeText(ChangelogBadge type)
{
    switch (type) {
    case ChangelogBadge::New:
        return QCoreApplication::translate("UpdateMessageOverlay", "NEW");
    case ChangelogBadge::Updated:
        return QCoreApplication::translate("UpdateMessageOverlay", "UPDATED");
    case ChangelogBadge::BugFix:
        return QCoreApplication::translate("UpdateMessageOverlay", "FIXED");
    case ChangelogBadge::Improved:
        return QCoreApplication::translate("UpdateMessageOverlay", "IMPROVED");
    case ChangelogBadge::Deprecated:
        return QCoreApplication::translate("UpdateMessageOverlay", "REMOVED");
    }

    return {};
}

int releaseBadgeColumnWidth(
    const ruwa::ui::core::ThemeColors& colors, const ruwa::ui::core::ThemeManager& theme)
{
    QFont badgeFont = colors.fonts.getUIFont(theme.scaledFontSize(BadgeFontSize));
    badgeFont.setWeight(QFont::DemiBold);

    const QFontMetrics metrics(badgeFont);
    int maxTextWidth = 0;
    for (ChangelogBadge type : {
             ChangelogBadge::New,
             ChangelogBadge::Updated,
             ChangelogBadge::BugFix,
             ChangelogBadge::Improved,
             ChangelogBadge::Deprecated,
         }) {
        maxTextWidth = qMax(maxTextWidth, metrics.horizontalAdvance(changelogBadgeText(type)));
    }

    const int badgeWidth
        = maxTextWidth + theme.scaled(BadgePaddingH) * 2 + theme.scaled(BadgeColumnExtraWidth);
    return qMax(theme.scaled(BadgeMinWidth), badgeWidth);
}

QPixmap blurSnapshotPixmap(const QPixmap& source, int radius)
{
    if (source.isNull() || radius <= 0) {
        return source;
    }

    const qreal dpr = source.devicePixelRatio();
    const QSize logicalSize(
        qMax(1, qRound(source.width() / dpr)), qMax(1, qRound(source.height() / dpr)));

    // Mild downscale to make QGraphicsBlurEffect cheap and to soften the result
    // a little extra. Aggressive downsampling (the previous approach) destroys
    // detail and produces colour-averaged smears when parts of the snapshot are
    // transparent.
    constexpr int downsample = 2;
    const QSize smallLogical(
        qMax(1, logicalSize.width() / downsample), qMax(1, logicalSize.height() / downsample));
    const QSize smallDevice(smallLogical.width() * dpr, smallLogical.height() * dpr);

    QImage downscaled = source.toImage()
                            .convertToFormat(QImage::Format_ARGB32_Premultiplied)
                            .scaled(smallDevice, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    downscaled.setDevicePixelRatio(1.0);

    // Pad by edge replication so QGraphicsBlurEffect doesn't darken the border:
    // the effect samples beyond the pixmap and treats those pixels as
    // transparent, producing a dark fringe. We render edge slices around the
    // padded image, blur the larger image, then crop the inner region.
    const qreal effectiveRadius = qMax(1.0, qreal(radius) / downsample);
    const int pad = qBound(2, qCeil(effectiveRadius * 2.0), 64);
    const QSize paddedSize(smallDevice.width() + pad * 2, smallDevice.height() + pad * 2);

    QImage padded(paddedSize, QImage::Format_ARGB32_Premultiplied);
    padded.fill(Qt::transparent);
    {
        QPainter p(&padded);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        // Stretch the downscaled image to fill the padded canvas first — gives
        // a soft, colour-matched background everywhere, including the pad.
        p.drawImage(QRect(QPoint(0, 0), paddedSize), downscaled);
        // Then draw the sharp copy at the centre so the inner area is correct.
        p.drawImage(QPoint(pad, pad), downscaled);
    }

    QGraphicsScene scene;
    auto* item = new QGraphicsPixmapItem(QPixmap::fromImage(padded));
    auto* effect = new QGraphicsBlurEffect;
    effect->setBlurRadius(effectiveRadius);
    effect->setBlurHints(QGraphicsBlurEffect::QualityHint);
    item->setGraphicsEffect(effect);
    scene.addItem(item);

    QImage blurredPadded(paddedSize, QImage::Format_ARGB32_Premultiplied);
    blurredPadded.fill(Qt::transparent);
    {
        QPainter p(&blurredPadded);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRectF target(0, 0, paddedSize.width(), paddedSize.height());
        scene.render(&p, target, target);
    }

    QImage cropped = blurredPadded.copy(pad, pad, smallDevice.width(), smallDevice.height());

    QImage upscaled = cropped.scaled(QSize(logicalSize.width() * dpr, logicalSize.height() * dpr),
        Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    QPixmap result = QPixmap::fromImage(upscaled);
    result.setDevicePixelRatio(dpr);
    return result;
}

// Apply an antialiased rounded-rect clip to a pixmap. We do this once per
// snapshot refresh instead of using QWidget::setMask on the card: setMask
// uses a 1-bit QRegion which produces visibly stair-stepped corners, while
// painter-based clipping with the Antialiasing hint gives smooth edges.
QPixmap applyRoundedCornerMask(const QPixmap& source, int radius)
{
    if (source.isNull() || radius <= 0) {
        return source;
    }

    const qreal dpr = source.devicePixelRatio();
    QPixmap result(source.size());
    result.setDevicePixelRatio(dpr);
    result.fill(Qt::transparent);

    QPainter p(&result);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QSizeF logicalSize(source.width() / dpr, source.height() / dpr);
    QPainterPath path;
    path.addRoundedRect(QRectF(QPointF(0, 0), logicalSize), radius, radius);
    p.setClipPath(path);
    p.drawPixmap(QPoint(0, 0), source);
    return result;
}

// 4×4 Bayer threshold matrix for ordered dithering during 16→8 quantisation.
// Used to break up the visible bands that appear when low-alpha gradients are
// composited at 8 bit/channel. Amplitude is exactly ±½ LSB at 8 bit, so the
// pattern is below perceptual threshold but the banding-prone transitions get
// scattered onto two adjacent quantisation levels instead of forming stripes.
constexpr int kBayer4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

class ScreenBlendImageWidget : public QWidget {
public:
    explicit ScreenBlendImageWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setPixmap(const QPixmap& pixmap)
    {
        m_pixmap = pixmap;
        rebuildDimPixmap();
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        if (m_pixmap.isNull()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(rect(), m_dimPixmap);
        painter.setCompositionMode(QPainter::CompositionMode_Screen);
        painter.drawPixmap(rect(), m_pixmap);
    }

private:
    void rebuildDimPixmap()
    {
        if (m_pixmap.isNull()) {
            m_dimPixmap = {};
            return;
        }

        m_dimPixmap = QPixmap(m_pixmap.size());
        m_dimPixmap.setDevicePixelRatio(m_pixmap.devicePixelRatio());
        m_dimPixmap.fill(QColor(0, 0, 0, 10));

        QPainter maskPainter(&m_dimPixmap);
        maskPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        maskPainter.drawPixmap(QPoint(0, 0), m_pixmap);
    }

    QPixmap m_pixmap;
    QPixmap m_dimPixmap;
};

class UpdateMessageGlassTintLayer : public QWidget {
public:
    explicit UpdateMessageGlassTintLayer(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Antialiased rounded-corner clip so the tint and dithered fill don't
        // bleed into the corners with hard edges.
        const int cornerRadius = theme.scaled(CardRadius);
        QPainterPath cornerClip;
        cornerClip.addRoundedRect(QRectF(rect()), cornerRadius, cornerRadius);
        painter.setClipPath(cornerClip);

        QColor baseTint = colors.isDark ? QColor(2, 3, 5, 198) : QColor(240, 244, 250, 188);
        QColor accentTint = colors.primary;
        accentTint.setAlpha(colors.isDark ? 22 : 32);

        // Render the gradient stack at 16 bit/channel so the low-alpha
        // sheen and vignette don't accumulate 8-bit rounding bands.
        const qreal dpr = devicePixelRatioF();
        const QSize devSize(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));

        QImage hiPrec(devSize, QImage::Format_RGBA64_Premultiplied);
        hiPrec.setDevicePixelRatio(dpr);
        hiPrec.fill(Qt::transparent);
        {
            QPainter p(&hiPrec);
            p.fillRect(rect(), baseTint);

            QLinearGradient sheen(rect().topLeft(), rect().bottomRight());
            sheen.setColorAt(0.0, accentTint);
            sheen.setColorAt(0.48, QColor(255, 255, 255, colors.isDark ? 11 : 30));
            sheen.setColorAt(1.0, QColor(0, 0, 0, colors.isDark ? 45 : 18));
            p.fillRect(rect(), sheen);

            QRadialGradient vignette(rect().center(), qMax(width(), height()) * 0.72);
            vignette.setColorAt(0.0, QColor(255, 255, 255, colors.isDark ? 3 : 13));
            vignette.setColorAt(1.0, QColor(0, 0, 0, colors.isDark ? 88 : 43));
            p.fillRect(rect(), vignette);
        }

        // 16→8 conversion with Bayer ordered dither.
        QImage out(devSize, QImage::Format_ARGB32_Premultiplied);
        out.setDevicePixelRatio(dpr);
        const int W = devSize.width();
        const int H = devSize.height();
        for (int y = 0; y < H; ++y) {
            const QRgba64* src = reinterpret_cast<const QRgba64*>(hiPrec.constScanLine(y));
            QRgb* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
            const int* bayerRow = kBayer4[y & 3];
            for (int x = 0; x < W; ++x) {
                const int off = bayerRow[x & 3] * 16; // 0..240, in 16-bit space
                const QRgba64 s = src[x];
                const int r = qMin(255, (int(s.red()) + off) / 257);
                const int g = qMin(255, (int(s.green()) + off) / 257);
                const int b = qMin(255, (int(s.blue()) + off) / 257);
                const int a = qMin(255, (int(s.alpha()) + off) / 257);
                dst[x] = qRgba(r, g, b, a);
            }
        }

        painter.drawImage(QPoint(0, 0), out);

        // Drop the rounded clip before drawing the border so its antialiased
        // edge isn't shaved by the clip path it's drawn along.
        painter.setClipping(false);
        ruwa::ui::painting::drawGradientBorder(painter, rect(), cornerRadius,
            colors.borderSubtleHover(),
            ruwa::ui::core::ThemeColors::withAlpha(
                colors.borderSubtle(), colors.borderSubtle().alpha() / 2));
    }
};

class VersionGradientLabel : public QLabel {
public:
    explicit VersionGradientLabel(QWidget* parent = nullptr)
        : QLabel(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);

        QLinearGradient gradient(0, 0, 0, height());
        gradient.setColorAt(0.0, colors.text);
        gradient.setColorAt(1.0, darkenedPrimary(colors));

        QPen pen;
        pen.setBrush(gradient);
        painter.setPen(pen);
        painter.setFont(font());
        painter.drawText(rect(), alignment(), text());
    }
};

void addReleaseHighlightRow(QWidget* parent, QVBoxLayout* layout,
    const ruwa::ui::core::ThemeColors& colors, const ruwa::ui::core::ThemeManager& theme,
    ChangelogBadge type, const QString& text)
{
    QColor markerColor = darkenedPrimary(colors);
    QString badgeText;

    switch (type) {
    case ChangelogBadge::New:
        badgeText = changelogBadgeText(type);
        markerColor = darkenedPrimary(colors);
        break;
    case ChangelogBadge::Updated:
        badgeText = changelogBadgeText(type);
        markerColor = colors.info;
        break;
    case ChangelogBadge::BugFix:
        badgeText = changelogBadgeText(type);
        markerColor = colors.success;
        break;
    case ChangelogBadge::Improved:
        badgeText = changelogBadgeText(type);
        markerColor = darkenedPrimary(colors);
        break;
    case ChangelogBadge::Deprecated:
        badgeText = changelogBadgeText(type);
        markerColor = colors.error;
        break;
    }

    auto* row = new QWidget(parent);
    row->setAttribute(Qt::WA_TranslucentBackground);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(theme.scaled(22));

    auto* badgeBox = new QWidget(row);
    badgeBox->setAttribute(Qt::WA_TranslucentBackground);
    auto* badgeLayout = new QVBoxLayout(badgeBox);
    badgeLayout->setContentsMargins(0, 0, 0, 0);
    badgeLayout->setSpacing(0);

    auto* badge = new QLabel(badgeBox);
    QFont badgeFont = colors.fonts.getUIFont(theme.scaledFontSize(BadgeFontSize));
    badgeFont.setWeight(QFont::DemiBold);
    badge->setFont(badgeFont);
    badge->setText(badgeText);
    badge->setAlignment(Qt::AlignCenter);
    badge->setStyleSheet(QStringLiteral("QLabel {"
                                        " background: transparent;"
                                        " color: %1;"
                                        " border: 1px solid %1;"
                                        " border-radius: %2px;"
                                        " padding: %3px %4px;"
                                        "}")
            .arg(markerColor.name())
            .arg(theme.scaled(10))
            .arg(theme.scaled(3))
            .arg(theme.scaled(BadgePaddingH)));
    badge->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    badge->adjustSize();
    badgeBox->setFixedWidth(releaseBadgeColumnWidth(colors, theme));

    badgeLayout->addWidget(badge, 0, Qt::AlignHCenter);
    badgeLayout->addStretch();

    auto* textLabel = new QLabel(row);
    textLabel->setWordWrap(true);
    textLabel->setText(text);
    textLabel->setFont(colors.fonts.getUIFont(theme.scaledFontSize(BodyFontSize)));
    textLabel->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; color: %1; }").arg(colors.text.name()));
    textLabel->setTextInteractionFlags(Qt::NoTextInteraction);

    rowLayout->addWidget(badgeBox, 0, Qt::AlignTop);
    rowLayout->addWidget(textLabel, 1, Qt::AlignTop);

    layout->addWidget(row);
}

class UpdateMessageCard : public QWidget {
public:
    explicit UpdateMessageCard(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);

        m_snapshotLabel = new QLabel(this);
        m_snapshotLabel->setAutoFillBackground(false);
        m_snapshotLabel->setAttribute(Qt::WA_TranslucentBackground);
        m_snapshotLabel->setScaledContents(true);
        m_snapshotLabel->setAlignment(Qt::AlignCenter);
        m_snapshotLabel->lower();

        m_tintLayer = new UpdateMessageGlassTintLayer(this);
        m_tintLayer->raise();
    }

    void refreshBackdropFrom(QWidget* source, const QRect& sourceRect)
    {
        if (!source || sourceRect.isEmpty()) {
            m_snapshotLabel->clear();
            return;
        }

        QWidget* window = source->window();
        if (!window) {
            m_snapshotLabel->clear();
            return;
        }

        // Grabbing the top-level window (rather than calling render() on the
        // intermediate widget) is what makes this work in practice: Qt has a
        // dedicated path for QWidget::grab() on a top-level window that
        // composites every QOpenGLWidget child's framebuffer into the result.
        // Plain QWidget::render() on a non-window widget skips that step, so
        // the canvas comes back transparent and the blur ends up being
        // applied to a mostly-empty image.
        const QPoint topLeftInWindow = source->mapTo(window, sourceRect.topLeft());
        const QRect grabRect(topLeftInWindow, sourceRect.size());

        QPixmap snapshot = window->grab(grabRect);
        if (snapshot.isNull()) {
            m_snapshotLabel->clear();
            return;
        }

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        QPixmap blurred = blurSnapshotPixmap(snapshot, theme.scaled(GlassBlurRadius));
        m_snapshotLabel->setPixmap(applyRoundedCornerMask(blurred, theme.scaled(CardRadius)));
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        // No setMask here: a QRegion mask is 1-bit and stair-steps rounded
        // corners. Each layer (snapshot pixmap, tint layer, surface fill)
        // applies its own antialiased rounded clip so corners stay smooth.

        m_snapshotLabel->setGeometry(rect());
        m_snapshotLabel->lower();
        m_tintLayer->setGeometry(rect());
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        int radius = theme.scaled(CardRadius);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        painter.setPen(Qt::NoPen);
        painter.setBrush(
            ruwa::ui::core::ThemeColors::withAlpha(colors.surface, colors.isDark ? 130 : 160));
        painter.drawRoundedRect(rect(), radius, radius);

        ruwa::ui::painting::drawGradientBorder(painter, rect(), radius, colors.borderSubtleHover(),
            ruwa::ui::core::ThemeColors::withAlpha(
                colors.borderSubtle(), colors.borderSubtle().alpha() / 2));
    }

private:
    QLabel* m_snapshotLabel = nullptr;
    UpdateMessageGlassTintLayer* m_tintLayer = nullptr;
};

} // namespace

UpdateMessageOverlay::UpdateMessageOverlay(QWidget* parent, QWidget* geometrySource)
    : QWidget(parent)
    , m_geometrySource(geometrySource ? geometrySource : parent)
{
    setupUI();
    setupAnimations();

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
    }
    if (m_geometrySource && m_geometrySource.data() != parentWidget()) {
        m_geometrySource->installEventFilter(this);
    }
    syncOverlayGeometry();

    QWidget::hide();
    m_dimProgress = 0.0;
}

UpdateMessageOverlay::~UpdateMessageOverlay()
{
    if (parentWidget()) {
        parentWidget()->removeEventFilter(this);
    }
    if (m_geometrySource && m_geometrySource.data() != parentWidget()) {
        m_geometrySource->removeEventFilter(this);
    }
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void UpdateMessageOverlay::setupUI()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    m_card = new UpdateMessageCard(this);
    m_card->setFixedSize(theme.scaled(CardWidth), theme.scaled(CardHeight));
    m_card->setMouseTracking(true);

    m_cardOpacityEffect = new QGraphicsOpacityEffect(m_card);
    m_cardOpacityEffect->setOpacity(0.0);
    m_card->setGraphicsEffect(m_cardOpacityEffect);

    auto* mainLayout = new QHBoxLayout(m_card);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === Left: text + button ===
    auto* leftWidget = new QWidget(m_card);
    leftWidget->setAutoFillBackground(false);
    leftWidget->setAttribute(Qt::WA_TranslucentBackground);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(theme.scaled(TextAreaPadding), theme.scaled(TextAreaPadding),
        theme.scaled(TextAreaPadding), theme.scaled(TextAreaPadding));
    leftLayout->setSpacing(theme.scaled(16));

    auto* dateLabel = new QLabel(QStringLiteral("JUNE 14, 2026"), leftWidget);
    QFont dateFont = colors.fonts.getUIFont(theme.scaledFontSize(DateFontSize));
    dateFont.setWeight(QFont::DemiBold);
    dateLabel->setFont(dateFont);
    dateLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.textMuted.name()));
    leftLayout->addWidget(dateLabel);

    auto* heroRow = new QWidget(leftWidget);
    heroRow->setAttribute(Qt::WA_TranslucentBackground);
    auto* heroLayout = new QHBoxLayout(heroRow);
    heroLayout->setContentsMargins(0, 0, 0, 0);
    heroLayout->setSpacing(theme.scaled(20));

    auto* versionLabel = new VersionGradientLabel(heroRow);
    versionLabel->setText(displayReleaseVersion());
    QFont versionFont = colors.fonts.getUIFont(theme.scaledFontSize(VersionFontSize));
    versionFont.setWeight(QFont::Black);
    versionLabel->setFont(versionFont);
    versionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    versionLabel->adjustSize();
    versionLabel->setFixedSize(versionLabel->sizeHint());
    heroLayout->addWidget(versionLabel, 0, Qt::AlignTop);

    auto* heroText = new QWidget(heroRow);
    heroText->setAttribute(Qt::WA_TranslucentBackground);
    auto* heroTextLayout = new QVBoxLayout(heroText);
    heroTextLayout->setContentsMargins(0, theme.scaled(9), 0, 0);
    heroTextLayout->setSpacing(theme.scaled(6));

    auto* statusLabel = new QLabel(
        QCoreApplication::translate("UpdateMessageOverlay", "YOU'RE UP TO DATE!"), heroText);
    QFont statusFont = colors.fonts.getUIFont(theme.scaledFontSize(StatusFontSize));
    statusFont.setWeight(QFont::DemiBold);
    statusLabel->setFont(statusFont);
    statusLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(darkenedPrimary(colors).name()));
    heroTextLayout->addWidget(statusLabel);

    auto* heroTitleLabel = new QLabel(
        QCoreApplication::translate("UpdateMessageOverlay", "Welcome to the latest Ruwa."),
        heroText);
    QFont heroTitleFont = colors.fonts.getUIFont(theme.scaledFontSize(HeroTitleFontSize));
    heroTitleFont.setWeight(QFont::DemiBold);
    heroTitleLabel->setFont(heroTitleFont);
    heroTitleLabel->setWordWrap(true);
    heroTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; color: %1; }").arg(colors.text.name()));
    heroTextLayout->addWidget(heroTitleLabel);
    heroTextLayout->addStretch();

    heroLayout->addWidget(heroText, 1);
    leftLayout->addWidget(heroRow);

    auto* descriptionLabel
        = new QLabel(QCoreApplication::translate("UpdateMessageOverlay",
                         "Ruwa is now open source. The source code and contribution process are "
                         "public on a fresh repository, the release ships alongside a brand-new "
                         "website, and the licensing, security, governance, CI, and release "
                         "infrastructure needed for public development are complete."),
            leftWidget);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setFont(colors.fonts.getUIFont(theme.scaledFontSize(BodyFontSize)));
    descriptionLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.textMuted.name()));
    leftLayout->addWidget(descriptionLabel);

    auto* highlightsWidget = new QWidget(leftWidget);
    highlightsWidget->setAttribute(Qt::WA_TranslucentBackground);
    auto* highlightsLayout = new QVBoxLayout(highlightsWidget);
    highlightsLayout->setContentsMargins(0, 0, 0, 0);
    highlightsLayout->setSpacing(theme.scaled(17));

    addReleaseHighlightRow(highlightsWidget, highlightsLayout, colors, theme, ChangelogBadge::New,
        QCoreApplication::translate("UpdateMessageOverlay",
            "A public source repository at github.com/LuskusDeus/Ruwa and a new project website at "
            "accretion.pro."));
    addReleaseHighlightRow(highlightsWidget, highlightsLayout, colors, theme,
        ChangelogBadge::Updated,
        QCoreApplication::translate("UpdateMessageOverlay",
            "The proprietary Discord Game SDK is replaced by a first-party Discord Rich Presence "
            "implementation over local IPC, using Qt only."));
    addReleaseHighlightRow(highlightsWidget, highlightsLayout, colors, theme,
        ChangelogBadge::Improved,
        QCoreApplication::translate("UpdateMessageOverlay",
            "A full contribution process, resolved licensing, and documented CI and release "
            "infrastructure for public development."));
    addReleaseHighlightRow(highlightsWidget, highlightsLayout, colors, theme,
        ChangelogBadge::BugFix,
        QCoreApplication::translate("UpdateMessageOverlay",
            "The binary installer release now packages and installs correctly, and an "
            "event-handling bug in the Layers panel is fixed."));

    leftLayout->addWidget(highlightsWidget);
    leftLayout->addStretch();

    // Buttons at bottom
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->setSpacing(theme.scaled(ButtonSpacing));

    auto* gotItButton
        = new WelcomeBannerButton(QCoreApplication::translate("UpdateMessageOverlay", "Got it"),
            WelcomeBannerButton::ButtonStyle::Primary, leftWidget);
    gotItButton->setSizeScale(0.9);
    connect(
        gotItButton, &WelcomeBannerButton::clicked, this, &UpdateMessageOverlay::onCardDismissed);
    btnLayout->addWidget(gotItButton);

    auto* changelogButton = new WelcomeBannerButton(
        QCoreApplication::translate("UpdateMessageOverlay", "Read full changelog →"),
        WelcomeBannerButton::ButtonStyle::Secondary, leftWidget);
    changelogButton->setSizeScale(0.9);
    changelogButton->setSecondaryIdleShadowAlpha(16);
    connect(changelogButton, &WelcomeBannerButton::clicked, this,
        &UpdateMessageOverlay::onReleaseNotesRequested);
    btnLayout->addWidget(changelogButton);

    btnLayout->addStretch();
    leftLayout->addLayout(btnLayout);

    mainLayout->addWidget(leftWidget, 1);

    // === Right: image (3:4, upscaled, with padding) ===
    const int padding = theme.scaled(ImagePadding);
    const int imgW = theme.scaled(ImageAreaWidth) - padding * 2;
    const int imgH = theme.scaled(CardHeight) - padding * 2;

    auto* imageContainer = new QWidget(m_card);
    imageContainer->setAutoFillBackground(false);
    imageContainer->setAttribute(Qt::WA_TranslucentBackground);
    auto* imageLayout = new QVBoxLayout(imageContainer);
    imageLayout->setContentsMargins(padding, padding, padding, padding);
    imageLayout->setSpacing(0);

    auto* imageWidget = new ScreenBlendImageWidget(imageContainer);
    imageWidget->setFixedSize(imgW, imgH);

    constexpr int imageCornerRadius = 10;

    QPixmap bannerPixmap(ruwa::ui::core::ResourceManager::instance().getBuiltInImagePath(
        ruwa::ui::core::ResourceManager::BuiltInImage::UpdateMessageBanner));
    if (!bannerPixmap.isNull()) {
        QImage img = bannerPixmap.toImage();
        int w = img.width(), h = img.height();
        int cropW, cropH;
        if (w * 4 > h * 3) {
            cropH = h;
            cropW = h * 3 / 4;
        } else {
            cropW = w;
            cropH = w * 4 / 3;
        }
        int x = (w - cropW) / 2, y = (h - cropH) / 2;
        QImage cropped = img.copy(x, y, cropW, cropH);
        QPixmap scaled = QPixmap::fromImage(
            cropped.scaled(imgW, imgH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));

        const int r = qMin(theme.scaled(imageCornerRadius), qMin(imgW, imgH) / 2);
        QPixmap rounded(scaled.size());
        rounded.fill(Qt::transparent);
        QPainter p(&rounded);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath path;
        path.addRoundedRect(QRect(0, 0, scaled.width(), scaled.height()), r, r);
        p.setClipPath(path);
        p.drawPixmap(0, 0, scaled);
        p.end();

        imageWidget->setPixmap(rounded);
    }

    imageLayout->addWidget(imageWidget, 0, Qt::AlignCenter);
    mainLayout->addWidget(imageContainer, 0);

    syncOverlayGeometry();
}

void UpdateMessageOverlay::setupAnimations()
{
    m_dimAnimation = new QPropertyAnimation(this, "dimProgress", this);
    m_dimAnimation->setDuration(DimAnimationDuration);

    m_cardOpacityAnim = new QPropertyAnimation(m_cardOpacityEffect, "opacity", this);
    m_cardOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_cardPosAnim = new QPropertyAnimation(m_card, "pos", this);
    m_cardPosAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_dimAnimation, &QPropertyAnimation::finished, this,
        &UpdateMessageOverlay::onDimAnimationFinished);
}

void UpdateMessageOverlay::showMessage()
{
    if (m_isShowing || (isVisible() && !m_isHiding)) {
        return;
    }

    m_isShowing = true;
    m_isHiding = false;

    syncOverlayGeometry();
    refreshCardBackdrop();

    QWidget::show();
    raise();
    setFocus();
    if (m_card) {
        m_card->raise();
    }

    if (!m_shortcutsBlocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
        m_shortcutsBlocked = true;
    }

    QPoint targetPos = cardTargetPosition();
    QPoint startPos = targetPos + QPoint(0, SlideOffset);

    m_card->move(startPos);
    m_cardOpacityEffect->setOpacity(0.0);

    m_dismissCooldownTimer.start();

    // Dim in
    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(1.0);
    m_dimAnimation->start();

    // Card slide + fade in (like MenuPopup / MessagePopup)
    m_cardOpacityAnim->stop();
    m_cardOpacityAnim->setDuration(CardAnimationDuration);
    m_cardOpacityAnim->setStartValue(0.0);
    m_cardOpacityAnim->setEndValue(1.0);
    m_cardOpacityAnim->start();

    m_cardPosAnim->stop();
    m_cardPosAnim->setDuration(CardAnimationDuration);
    m_cardPosAnim->setStartValue(startPos);
    m_cardPosAnim->setEndValue(targetPos);
    m_cardPosAnim->start();
}

void UpdateMessageOverlay::hideMessage(bool bypassCooldown)
{
    if (m_isHiding || !isVisible()) {
        return;
    }
    if (!bypassCooldown && m_dismissCooldownTimer.elapsed() < DismissCooldownMs) {
        return;
    }

    m_isHiding = true;
    m_isShowing = false;

    disconnect(m_cardOpacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_cardOpacityAnim, &QPropertyAnimation::finished, this,
        &UpdateMessageOverlay::onCardHideAnimationFinished);

    // Dim out
    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(0.0);
    m_dimAnimation->start();

    // Card slide down + fade out (exit to bottom)
    QPoint currentPos = m_card->pos();
    QPoint endPos = currentPos + QPoint(0, SlideOffset);

    m_cardOpacityAnim->stop();
    m_cardOpacityAnim->setDuration(CardAnimationDuration);
    m_cardOpacityAnim->setStartValue(m_cardOpacityEffect->opacity());
    m_cardOpacityAnim->setEndValue(0.0);
    m_cardOpacityAnim->start();

    m_cardPosAnim->stop();
    m_cardPosAnim->setDuration(CardAnimationDuration);
    m_cardPosAnim->setStartValue(currentPos);
    m_cardPosAnim->setEndValue(endPos);
    m_cardPosAnim->start();
}

void UpdateMessageOverlay::onCardDismissed()
{
    hideMessage(true); // bypass cooldown when user explicitly clicks "Got it"
}

void UpdateMessageOverlay::onReleaseNotesRequested()
{
    emit releaseNotesRequested();
    hideMessage(true);
}

bool UpdateMessageOverlay::isActive() const
{
    return isVisible() && !m_isHiding;
}

void UpdateMessageOverlay::setDimProgress(qreal progress)
{
    if (qFuzzyCompare(m_dimProgress, progress))
        return;
    m_dimProgress = progress;
    update();
}

void UpdateMessageOverlay::onDimAnimationFinished()
{
    if (m_isShowing) {
        m_isShowing = false;
        emit shown();
    }
}

void UpdateMessageOverlay::onCardHideAnimationFinished()
{
    if (m_isHiding) {
        m_isHiding = false;
        if (m_shortcutsBlocked) {
            m_shortcutsBlocked = false;
            ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
        }
        QWidget::hide();
        emit hidden();
    }
}

QPoint UpdateMessageOverlay::cardTargetPosition() const
{
    if (!m_card)
        return QPoint();
    int x = (width() - m_card->width()) / 2;
    int y = (height() - m_card->height()) / 2;
    return QPoint(qMax(0, x), qMax(0, y));
}

void UpdateMessageOverlay::updateCardPosition()
{
    if (!m_card)
        return;
    m_card->move(cardTargetPosition());
}

void UpdateMessageOverlay::refreshCardBackdrop()
{
    if (!m_card) {
        return;
    }

    QWidget* source = m_geometrySource ? m_geometrySource.data() : parentWidget();

    const bool cardWasVisible = m_card->isVisible();
    if (cardWasVisible) {
        m_card->hide();
    }

    static_cast<UpdateMessageCard*>(m_card)->refreshBackdropFrom(source, m_card->geometry());

    if (cardWasVisible) {
        m_card->show();
        m_card->raise();
    }
}

void UpdateMessageOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_dimProgress <= 0.001)
        return;

    QPainter painter(this);

    int alpha = static_cast<int>(MaxDimOpacity * 255 * m_dimProgress);
    painter.fillRect(rect(), QColor(0, 0, 0, alpha));
}

void UpdateMessageOverlay::mousePressEvent(QMouseEvent* event)
{
    if (m_card && !m_card->geometry().contains(event->pos())) {
        hideMessage();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void UpdateMessageOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideMessage();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void UpdateMessageOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCardPosition();
}

void UpdateMessageOverlay::syncOverlayGeometry()
{
    QWidget* parent = parentWidget();
    if (!parent) {
        return;
    }

    QWidget* source = m_geometrySource ? m_geometrySource.data() : parent;
    if (source == parent) {
        setGeometry(parent->rect());
    } else {
        setGeometry(QRect(source->mapTo(parent, QPoint(0, 0)), source->size()));
    }

    updateCardPosition();
}

void UpdateMessageOverlay::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        rebuildCard();
    }
}

void UpdateMessageOverlay::rebuildCard()
{
    const bool overlayActive = isVisible() && !m_isHiding;

    if (m_cardOpacityAnim) {
        m_cardOpacityAnim->stop();
        delete m_cardOpacityAnim;
        m_cardOpacityAnim = nullptr;
    }
    if (m_cardPosAnim) {
        m_cardPosAnim->stop();
        delete m_cardPosAnim;
        m_cardPosAnim = nullptr;
    }

    delete m_card;
    m_card = nullptr;
    m_cardOpacityEffect = nullptr;

    setupUI();

    m_cardOpacityAnim = new QPropertyAnimation(m_cardOpacityEffect, "opacity", this);
    m_cardOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_cardPosAnim = new QPropertyAnimation(m_card, "pos", this);
    m_cardPosAnim->setEasingCurve(QEasingCurve::OutCubic);

    syncOverlayGeometry();

    if (overlayActive) {
        refreshCardBackdrop();
        m_cardOpacityEffect->setOpacity(1.0);
        m_card->move(cardTargetPosition());
        m_card->show();
        m_card->raise();
    }
}

bool UpdateMessageOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() || watched == m_geometrySource.data()) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::LayoutRequest:
            syncOverlayGeometry();
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace ruwa::ui::widgets
