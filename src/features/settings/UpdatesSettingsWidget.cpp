// SPDX-License-Identifier: MPL-2.0

// UpdatesSettingsWidget.cpp
#include "features/settings/UpdatesSettingsWidget.h"
#include "features/home/welcome/WelcomeBannerButton.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyle.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/inputs/ProgressSlider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/ResourceManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {
const int BASE_HEIGHT = 238;
const int BASE_MIN_WIDTH = 400;
const int BASE_LAYOUT_PADDING = 32;
const int BASE_LAYOUT_SPACING = 8;
const int BASE_BUTTON_SPACING = 12;
const int BASE_TITLE_FONT_SIZE = 26;
const int BASE_SUBTITLE_FONT_SIZE = 13;
const int BASE_BORDER_RADIUS = 8;
const int BASE_TEXT_RIGHT_INSET = 88;
const int BASE_SUBTITLE_MAX_WIDTH = 470;
const int BASE_SUBTITLE_LINES = 2;
const int BASE_TEXT_BLOCK_HEIGHT = 86;
const int BASE_ACTION_BLOCK_HEIGHT = 48;
const int BASE_PROGRESS_WIDTH_DIVISOR = 2;
const int BASE_PROGRESS_LABEL_GAP = 12;
const int BASE_PROGRESS_VALUE_FONT_SIZE = 11;
const int BASE_SECONDARY_DEFAULT_MIN_WIDTH = 168;
const int BASE_RECHECK_MIN_WIDTH = 0;
constexpr qreal BACKGROUND_FADE_START = 1.0 / 3.0;
constexpr qreal BACKGROUND_FADE_END = 2.0 / 3.0;

QColor compositeOver(const QColor& foreground, const QColor& background)
{
    const qreal fa = foreground.alphaF();
    const qreal ba = background.alphaF() * (1.0 - fa);
    const qreal outAlpha = fa + ba;
    if (outAlpha <= 0.0) {
        return QColor(0, 0, 0, 0);
    }

    const auto blendChannel
        = [fa, ba, outAlpha](int fg, int bg) { return qRound((fg * fa + bg * ba) / outAlpha); };

    return QColor(blendChannel(foreground.red(), background.red()),
        blendChannel(foreground.green(), background.green()),
        blendChannel(foreground.blue(), background.blue()), qRound(outAlpha * 255.0));
}

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

QColor opaqueColor(QColor color)
{
    color.setAlpha(255);
    return color;
}

QImage buildThemedBannerImage(
    const QPixmap& source, const ruwa::ui::core::ThemeColors& colors, const QColor& panelBackground)
{
    QImage input = source.toImage().convertToFormat(QImage::Format_ARGB32);
    QImage output(input.size(), QImage::Format_ARGB32_Premultiplied);
    output.fill(Qt::transparent);

    const QColor shadow = opaqueColor(ruwa::ui::core::ThemeColors::interpolate(
        panelBackground, colors.surfaceElevated(), colors.isDark ? 0.55 : 0.35));
    const QColor mid = opaqueColor(ruwa::ui::core::ThemeColors::interpolate(
        panelBackground, colors.primary, colors.isDark ? 0.18 : 0.12));
    const QColor highlight
        = opaqueColor(ruwa::ui::core::ThemeColors::interpolate(colors.text, colors.primary, 0.25));

    for (int y = 0; y < input.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(input.constScanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(output.scanLine(y));
        for (int x = 0; x < input.width(); ++x) {
            const int alpha = qAlpha(src[x]);
            if (alpha == 0) {
                continue;
            }

            const qreal luma = qGray(src[x]) / 255.0;
            QColor mapped;
            if (luma < 0.52) {
                mapped = ruwa::ui::core::ThemeColors::interpolate(
                    panelBackground, shadow, luma / 0.52);
            } else {
                mapped = ruwa::ui::core::ThemeColors::interpolate(
                    mid, highlight, (luma - 0.52) / 0.48);
            }
            mapped.setAlpha(alpha);
            dst[x] = qPremultiply(mapped.rgba());
        }
    }

    return output;
}

QImage buildPixelNoiseFadeOverlay(
    const QSize& logicalSize, qreal dpr, const QColor& panelBackground)
{
    const int pixelWidth = qMax(1, qRound(logicalSize.width() * dpr));
    const int pixelHeight = qMax(1, qRound(logicalSize.height() * dpr));

    QImage overlay(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    overlay.fill(Qt::transparent);
    overlay.setDevicePixelRatio(dpr);
    const QRgb premultipliedPanelBackground = qPremultiply(panelBackground.rgba());

    const int fadeStartPx = qRound(pixelWidth * BACKGROUND_FADE_START);
    const int fadeEndPx = qRound(pixelWidth * BACKGROUND_FADE_END);
    const int fadeSpanPx = qMax(1, fadeEndPx - fadeStartPx);

    for (int y = 0; y < pixelHeight; ++y) {
        QRgb* scanLine = reinterpret_cast<QRgb*>(overlay.scanLine(y));
        for (int x = 0; x < pixelWidth; ++x) {
            qreal coverage = 0.0;
            if (x <= fadeStartPx) {
                coverage = 1.0;
            } else if (x < fadeEndPx) {
                coverage = 1.0 - qreal(x - fadeStartPx) / fadeSpanPx;
            }

            const qreal threshold = pixelNoise01(x, y);
            if (coverage <= threshold) {
                continue;
            }

            scanLine[x] = premultipliedPanelBackground;
        }
    }

    return overlay;
}
} // namespace

UpdatesSettingsWidget::UpdatesSettingsWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    loadBackgroundImage();
    setupUI();

    updateScaledSizes();
    applyState();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &UpdatesSettingsWidget::onThemeChanged);
}

void UpdatesSettingsWidget::loadBackgroundImage()
{
    m_backgroundImage = QPixmap(ruwa::ui::core::ResourceManager::instance().getBuiltInImagePath(
        ruwa::ui::core::ResourceManager::BuiltInImage::UpdateBanner));
    invalidateBackgroundCache();
}

void UpdatesSettingsWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);

    // Title and subtitle are drawn directly in the cached pixmap.
    // The layout only manages the stretch + action row at the bottom.
    m_mainLayout->addStretch();

    m_buttonContainer = new QWidget(this);
    m_buttonContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_buttonLayout = new QHBoxLayout(m_buttonContainer);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);

    m_primaryButton = new WelcomeBannerButton(
        QString(), WelcomeBannerButton::ButtonStyle::Primary, m_buttonContainer);
    connect(m_primaryButton, &WelcomeBannerButton::clicked, this, [this]() {
        if (m_updateState == UpdateState::UpToDate) {
            emit releaseNotesClicked();
            return;
        }

        emit updateActionClicked();
    });
    m_buttonLayout->addWidget(m_primaryButton);

    m_secondaryButton = new WelcomeBannerButton(
        QString(), WelcomeBannerButton::ButtonStyle::Secondary, m_buttonContainer);
    connect(m_secondaryButton, &WelcomeBannerButton::clicked, this, [this]() {
        if (m_updateState == UpdateState::UpToDate) {
            if (!m_recheckInProgress) {
                emit updateRecheckClicked();
            }
            return;
        }

        emit whatsNewClicked();
    });
    m_buttonLayout->addWidget(m_secondaryButton);
    m_buttonLayout->addStretch();

    m_mainLayout->addWidget(m_buttonContainer);

    m_progressSlider = new ProgressSlider(this);
    m_progressSlider->setRange(0, 100);
    m_progressSlider->setValue(0);
    m_progressSlider->setShowText(false);
    m_progressSlider->hide();
    m_progressSlider->setAttribute(Qt::WA_TransparentForMouseEvents);

    m_progressValueLabel = new QLabel(this);
    m_progressValueLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_progressValueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_progressValueLabel->setText(QStringLiteral("0%"));
    m_progressValueLabel->hide();
}

void UpdatesSettingsWidget::setUpdateState(UpdateState state)
{
    if (m_updateState == state) {
        return;
    }

    m_updateState = state;
    if (m_updateState != UpdateState::UpToDate) {
        m_recheckInProgress = false;
    }
    applyState();
}

void UpdatesSettingsWidget::setDownloadProgress(int percent)
{
    m_downloadProgress = qBound(0, percent, 100);

    if (m_progressSlider) {
        m_progressSlider->setValue(m_downloadProgress);
    }
    if (m_progressValueLabel) {
        m_progressValueLabel->setText(QString::number(m_downloadProgress) + QStringLiteral("%"));
    }
    if (m_updateState == UpdateState::Downloading) {
        updateProgressSliderPosition();
    }
}

void UpdatesSettingsWidget::setUpdateVersion(const QString& version)
{
    if (m_updateVersion == version) {
        return;
    }

    m_updateVersion = version;
    applyState();
}

void UpdatesSettingsWidget::setLastCheckedMinutesAgo(int minutes)
{
    const int boundedMinutes = qMax(0, minutes);
    if (m_lastCheckedMinutesAgo == boundedMinutes) {
        return;
    }

    m_lastCheckedMinutesAgo = boundedMinutes;
    applyState();
}

void UpdatesSettingsWidget::setReleaseDescription(const QString& description)
{
    if (m_releaseDescription == description) {
        return;
    }

    m_releaseDescription = description;
    applyState();
}

void UpdatesSettingsWidget::setRecheckInProgress(bool inProgress)
{
    if (m_recheckInProgress == inProgress) {
        return;
    }

    m_recheckInProgress = inProgress;
    applyState();
}

void UpdatesSettingsWidget::applyState()
{
    const char* ctx = metaObject()->className();
    const QString versionText
        = m_updateVersion.isEmpty() ? QCoreApplication::applicationVersion() : m_updateVersion;

    switch (m_updateState) {
    case UpdateState::UpToDate:
        m_titleText = QCoreApplication::translate(ctx, "You're running the latest version");
        if (m_lastCheckedMinutesAgo == 0) {
            m_descriptionText = QCoreApplication::translate(
                ctx, "Ruwa checks for updates automatically. Checked just now.");
        } else {
            m_descriptionText = QCoreApplication::translate(
                ctx, "Ruwa checks for updates automatically. Last checked %1 minutes ago.")
                                    .arg(m_lastCheckedMinutesAgo);
        }
        m_primaryButton->setText(QCoreApplication::translate(ctx, "View release notes"));
        m_primaryButton->syncSizeToText();
        m_primaryButton->show();
        m_secondaryButton->setBaseMinimumWidth(BASE_RECHECK_MIN_WIDTH);
        m_secondaryButton->setText(QCoreApplication::translate(ctx, "Re-check"));
        m_secondaryButton->setTrailingLoadingVisible(m_recheckInProgress);
        m_secondaryButton->setEnabled(!m_recheckInProgress);
        m_secondaryButton->setCursor(
            m_recheckInProgress ? Qt::ArrowCursor : Qt::PointingHandCursor);
        m_secondaryButton->syncSizeToText();
        m_secondaryButton->show();
        break;
    case UpdateState::Downloading:
        m_titleText = QCoreApplication::translate(ctx, "Installing Ruwa %1").arg(versionText);
        m_descriptionText = QCoreApplication::translate(ctx,
            "Downloading components. You can keep working - we'll let you know when it's ready.");
        m_primaryButton->hide();
        m_secondaryButton->setTrailingLoadingVisible(false);
        m_secondaryButton->hide();
        break;
    case UpdateState::UpdateAvailable:
        m_titleText = QCoreApplication::translate(ctx, "Ruwa %1 is now available").arg(versionText);
        m_descriptionText = m_releaseDescription;
        m_primaryButton->setText(QCoreApplication::translate(ctx, "Update now"));
        m_primaryButton->syncSizeToText();
        m_primaryButton->show();
        m_secondaryButton->setBaseMinimumWidth(BASE_SECONDARY_DEFAULT_MIN_WIDTH);
        m_secondaryButton->setText(QCoreApplication::translate(ctx, "What's new ->"));
        m_secondaryButton->setTrailingLoadingVisible(false);
        m_secondaryButton->setEnabled(true);
        m_secondaryButton->setCursor(Qt::PointingHandCursor);
        m_secondaryButton->syncSizeToText();
        m_secondaryButton->show();
        break;
    case UpdateState::ReadyToRestart:
        m_titleText = QCoreApplication::translate(ctx, "Restart Ruwa to finish updating");
        m_descriptionText = QCoreApplication::translate(
            ctx, "Version %1 is installed. A quick restart is needed to apply the changes.")
                                .arg(versionText);
        m_primaryButton->setText(QCoreApplication::translate(ctx, "Restart now"));
        m_primaryButton->syncSizeToText();
        m_primaryButton->show();
        m_secondaryButton->setTrailingLoadingVisible(false);
        m_secondaryButton->hide();
        break;
    }

    invalidateBackgroundCache();
    update();

    if (m_progressSlider) {
        const bool showProgress = (m_updateState == UpdateState::Downloading);
        m_progressSlider->setVisible(showProgress);
        if (showProgress) {
            m_progressSlider->setValue(m_downloadProgress);
            QTimer::singleShot(0, this, &UpdatesSettingsWidget::updateProgressSliderPosition);
        }
    }
    if (m_progressValueLabel) {
        const bool showProgress = (m_updateState == UpdateState::Downloading);
        m_progressValueLabel->setVisible(showProgress);
        if (showProgress) {
            m_progressValueLabel->setText(
                QString::number(m_downloadProgress) + QStringLiteral("%"));
            QTimer::singleShot(0, this, &UpdatesSettingsWidget::updateProgressSliderPosition);
        }
    }
}

void UpdatesSettingsWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void UpdatesSettingsWidget::retranslateUi()
{
    applyState();
}

void UpdatesSettingsWidget::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    setFixedHeight(theme.scaled(BASE_HEIGHT));
    setMinimumWidth(theme.scaled(BASE_MIN_WIDTH));

    const int padding = theme.scaled(BASE_LAYOUT_PADDING);
    const int spacing = theme.scaled(BASE_LAYOUT_SPACING);
    m_mainLayout->setContentsMargins(padding, padding, padding, padding);
    m_mainLayout->setSpacing(spacing);

    if (m_buttonLayout) {
        m_buttonLayout->setSpacing(theme.scaled(BASE_BUTTON_SPACING));
    }
    if (m_buttonContainer) {
        m_buttonContainer->setFixedHeight(theme.scaled(BASE_ACTION_BLOCK_HEIGHT));
    }
    if (m_progressValueLabel) {
        m_progressValueLabel->setFont(
            theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_PROGRESS_VALUE_FONT_SIZE)));
        m_progressValueLabel->adjustSize();
    }

    invalidateBackgroundCache();
}

void UpdatesSettingsWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (event->size() != event->oldSize()) {
        invalidateBackgroundCache();
    }
    updateProgressSliderPosition();
}

void UpdatesSettingsWidget::updateProgressSliderPosition()
{
    if (!m_progressSlider || !m_progressSlider->isVisible() || !m_progressValueLabel
        || !m_progressValueLabel->isVisible()) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int horizontalPadding = theme.scaled(BASE_LAYOUT_PADDING);
    const int progressLabelGap = theme.scaled(BASE_PROGRESS_LABEL_GAP);
    const QRect actionRect = m_buttonContainer->geometry();
    const int sliderY = actionRect.top() + (actionRect.height() - m_progressSlider->height()) / 2;
    const int availableWidth = qMax(0, width() - horizontalPadding * 2);
    const int sliderWidth = qMax(0, availableWidth / BASE_PROGRESS_WIDTH_DIVISOR);

    m_progressValueLabel->adjustSize();
    const QSize labelSize = m_progressValueLabel->sizeHint();
    const int labelX = horizontalPadding + sliderWidth + progressLabelGap;
    const int labelY = actionRect.top() + (actionRect.height() - labelSize.height()) / 2;

    m_progressSlider->setGeometry(
        horizontalPadding, sliderY, sliderWidth, m_progressSlider->height());
    m_progressValueLabel->setGeometry(labelX, labelY, labelSize.width(), labelSize.height());
}

void UpdatesSettingsWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_cachedBackground.isNull()) {
        rebuildBackgroundCache();
    }

    QPainter painter(this);
    painter.drawPixmap(0, 0, m_cachedBackground);
}

void UpdatesSettingsWidget::updateThemeColors()
{
    if (m_progressValueLabel) {
        m_progressValueLabel->setStyleSheet(QStringLiteral("background: transparent; color: %1;")
                .arg(ruwa::ui::core::ThemeManager::instance().colors().textMuted.name()));
    }
    invalidateBackgroundCache();
    update();
}

void UpdatesSettingsWidget::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

void UpdatesSettingsWidget::invalidateBackgroundCache()
{
    m_cachedBackground = QPixmap();
}

void UpdatesSettingsWidget::rebuildBackgroundCache()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const auto& styleManager = ruwa::ui::core::WidgetStyleManager::instance();
    const auto settingsPanelStyle = ruwa::ui::core::WidgetStyle::settingsPanelStyle();
    const QSize sz = size();
    if (sz.isEmpty()) {
        m_cachedBackground = QPixmap();
        return;
    }

    const qreal dpr = devicePixelRatioF();
    QPixmap buffer(sz * dpr);
    buffer.setDevicePixelRatio(dpr);
    buffer.fill(colors.background);

    const int borderRadius = theme.scaled(BASE_BORDER_RADIUS);

    QPainter painter(&buffer);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(QPointF(0.0, 0.0), QSizeF(sz)), borderRadius, borderRadius);
    painter.setClipPath(clipPath);

    QColor panelBackground = styleManager.resolveColor(
        settingsPanelStyle.background.color, settingsPanelStyle.background.customColor);
    const qreal backgroundOpacity = qBound(0.0, settingsPanelStyle.background.opacity, 1.0);
    if (backgroundOpacity < 1.0) {
        panelBackground.setAlphaF(panelBackground.alphaF() * backgroundOpacity);
    }
    const QColor effectivePanelBackground = compositeOver(panelBackground, colors.background);
    painter.fillRect(QRect(QPoint(0, 0), sz), effectivePanelBackground);

    if (!m_backgroundImage.isNull()) {
        const QPixmap scaledBg = m_backgroundImage.scaled(
            sz, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        int x = sz.width() - scaledBg.width();
        int y = (sz.height() - scaledBg.height()) / 2;
        if (x < -scaledBg.width() / 2) {
            x = -scaledBg.width() / 2;
        }
        const QImage themedBg = buildThemedBannerImage(scaledBg, colors, effectivePanelBackground);
        painter.drawImage(x, y, themedBg);

        painter.drawImage(
            QPoint(0, 0), buildPixelNoiseFadeOverlay(sz, dpr, effectivePanelBackground));
    }

    const int padding = theme.scaled(BASE_LAYOUT_PADDING);
    const int spacing = theme.scaled(BASE_LAYOUT_SPACING);
    const int textRightInset = theme.scaled(BASE_TEXT_RIGHT_INSET);
    const int textBlockHeight = theme.scaled(BASE_TEXT_BLOCK_HEIGHT);
    const int textWidth = qMax(0, sz.width() - padding * 2 - textRightInset);
    const int subtitleWidth = qMin(textWidth, theme.scaled(BASE_SUBTITLE_MAX_WIDTH));
    const QRect textBlockRect(padding, padding, textWidth, textBlockHeight);

    QFont titleFont = colors.fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE));
    painter.setFont(titleFont);
    painter.setPen(colors.text);
    QFontMetrics titleFm(titleFont);
    const int titleMeasureHeight = qMin(textBlockHeight, titleFm.lineSpacing() * 2);
    const QRect titleMeasureRect(
        textBlockRect.left(), textBlockRect.top(), textBlockRect.width(), titleMeasureHeight);
    const QRect titleRect = titleFm.boundingRect(
        titleMeasureRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_titleText);
    painter.drawText(
        titleMeasureRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_titleText);

    QFont subtitleFont = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SUBTITLE_FONT_SIZE));
    painter.setFont(subtitleFont);
    painter.setPen(colors.textMuted);
    QFontMetrics subtitleFm(subtitleFont);
    const int subtitleY = titleRect.bottom() + spacing + 1;
    const QRect subtitleRect(textBlockRect.left(), subtitleY, subtitleWidth,
        subtitleFm.lineSpacing() * BASE_SUBTITLE_LINES);
    if (!m_descriptionText.isEmpty()) {
        painter.drawText(
            subtitleRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, m_descriptionText);
    }

    if (settingsPanelStyle.border.enabled) {
        const QColor borderTop = styleManager.resolveColor(
            settingsPanelStyle.border.topColor, settingsPanelStyle.border.customTopColor);
        const QColor borderBottom = styleManager.resolveColor(
            settingsPanelStyle.border.bottomColor, settingsPanelStyle.border.customBottomColor);
        ruwa::ui::painting::drawGradientBorder(painter, QRectF(QPointF(0.0, 0.0), QSizeF(sz)),
            borderRadius, borderTop, borderBottom, settingsPanelStyle.border.width);
    }

    painter.end();

    m_cachedBackground = buffer;
}

} // namespace ruwa::ui::widgets
