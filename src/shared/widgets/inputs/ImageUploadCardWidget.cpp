// SPDX-License-Identifier: MPL-2.0

#include "ImageUploadCardWidget.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/utils/FileDialogMemory.h"
#include "shared/widgets/CapsuleButton.h"

#include <QCoreApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QList>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

constexpr int kCardRadius = 10;
constexpr int kBaseCardHeight = 92;
constexpr int kBaseOuterMargin = 1;
constexpr int kBaseOuterSpacing = 0;
constexpr int kBaseEmptyPaddingX = 16;
constexpr int kBaseEmptyPaddingY = 12;
constexpr int kBaseLoadedPadding = 10;
constexpr int kBaseInnerSpacing = 10;
constexpr int kBaseEmptyTextSpacing = 2;
constexpr int kBaseActionSpacing = 6;
constexpr int kBasePreviewWidth = 84;
constexpr int kBaseIconSize = 24;
constexpr int kBaseTitlePixelSize = 12;
constexpr int kBaseBodyPixelSize = 11;
constexpr int kBaseMetaPixelSize = 10;
constexpr int kBaseButtonHeight = 24;

QString defaultDialogFilter()
{
    return QCoreApplication::translate("ImageUploadCardWidget",
        "Image Files (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp *.gif *.psd)");
}

QStringList defaultAcceptedSuffixes()
{
    return {
        QStringLiteral("png"),
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("bmp"),
        QStringLiteral("tif"),
        QStringLiteral("tiff"),
        QStringLiteral("webp"),
        QStringLiteral("gif"),
        QStringLiteral("psd"),
    };
}

QString humanFileSize(qint64 bytes)
{
    if (bytes <= 0) {
        return QString();
    }

    static const QStringList units = {
        QStringLiteral("B"),
        QStringLiteral("KB"),
        QStringLiteral("MB"),
        QStringLiteral("GB"),
    };

    double size = static_cast<double>(bytes);
    int unitIndex = 0;
    while (size >= 1024.0 && unitIndex < units.size() - 1) {
        size /= 1024.0;
        ++unitIndex;
    }

    const int decimals = (size >= 100.0 || unitIndex == 0) ? 0 : 1;
    return QStringLiteral("%1 %2").arg(size, 0, 'f', decimals).arg(units[unitIndex]);
}

QString imageKindLabel(const QImage& image)
{
    if (image.isNull()) {
        return QString();
    }
    if (image.isGrayscale()) {
        return QCoreApplication::translate("ImageUploadCardWidget", "Grayscale");
    }
    return image.hasAlphaChannel() ? QCoreApplication::translate("ImageUploadCardWidget", "RGBA")
                                   : QCoreApplication::translate("ImageUploadCardWidget", "RGB");
}

QImage sourceAlphaTintedPreviewImage(const QImage& image, const QColor& color)
{
    if (image.isNull()) {
        return QImage();
    }

    QImage tinted(image.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawImage(0, 0, image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    return tinted;
}

QImage luminanceAlphaTintedPreviewImage(const QImage& image, const QColor& color)
{
    if (image.isNull()) {
        return QImage();
    }

    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    QImage tinted(source.size(), QImage::Format_ARGB32);
    tinted.fill(Qt::transparent);

    const int colorAlpha = color.alpha();
    for (int y = 0; y < source.height(); ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(source.constScanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(tinted.scanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            const QRgb pixel = src[x];
            const qreal luminance
                = 0.299 * qRed(pixel) + 0.587 * qGreen(pixel) + 0.114 * qBlue(pixel);
            const int maskAlpha = qRound(luminance * (qAlpha(pixel) / 255.0));
            const int alpha = (maskAlpha * colorAlpha) / 255;
            dst[x] = qRgba(color.red(), color.green(), color.blue(), alpha);
        }
    }

    return tinted;
}

QImage tintedPreviewImage(
    const QImage& image, const QColor& color, ImageUploadCardWidget::PreviewTintMode mode)
{
    switch (mode) {
    case ImageUploadCardWidget::PreviewTintMode::LuminanceAlpha:
        return luminanceAlphaTintedPreviewImage(image, color);
    case ImageUploadCardWidget::PreviewTintMode::SourceAlpha:
    default:
        return sourceAlphaTintedPreviewImage(image, color);
    }
}

} // namespace

ImageUploadCardWidget::ImageUploadCardWidget(QWidget* parent)
    : QWidget(parent)
    , m_emptyTitle(QCoreApplication::translate("ImageUploadCardWidget", "No image loaded"))
    , m_emptyDescription(
          QCoreApplication::translate("ImageUploadCardWidget", "Click or drop an image here"))
    , m_browseText(QCoreApplication::translate("ImageUploadCardWidget", "Replace"))
    , m_removeText(QCoreApplication::translate("ImageUploadCardWidget", "Remove"))
    , m_dialogTitle(QCoreApplication::translate("ImageUploadCardWidget", "Select Image"))
    , m_dialogFilter(defaultDialogFilter())
    , m_acceptedSuffixes(defaultAcceptedSuffixes())
{
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    buildUi();
    updateScaledSizes();
    updateUi();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateScaledSizes();
        updateStyles();
        updatePreviewPixmap();
        update();
    });
}

void ImageUploadCardWidget::setEmptyStateTexts(const QString& title, const QString& description)
{
    m_emptyTitle = title;
    m_emptyDescription = description;
    updateUi();
}

void ImageUploadCardWidget::setActionTexts(const QString& browseText, const QString& removeText)
{
    m_browseText = browseText;
    m_removeText = removeText;
    updateUi();
}

void ImageUploadCardWidget::setDialogTitle(const QString& title)
{
    m_dialogTitle = title;
}

void ImageUploadCardWidget::setDialogFilter(const QString& filter)
{
    m_dialogFilter = filter;
}

void ImageUploadCardWidget::setAcceptedSuffixes(const QStringList& suffixes)
{
    m_acceptedSuffixes.clear();
    for (const QString& suffix : suffixes) {
        const QString normalized = suffix.trimmed().toLower();
        if (!normalized.isEmpty()) {
            m_acceptedSuffixes.append(normalized);
        }
    }
    m_acceptedSuffixesExplicit = true;
}

void ImageUploadCardWidget::setBrowseEnabled(bool enabled)
{
    if (m_browseEnabled == enabled) {
        return;
    }

    m_browseEnabled = enabled;
    setCursor(m_browseEnabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    updateUi();
}

void ImageUploadCardWidget::setFooterWidget(QWidget* widget)
{
    if (m_footerWidget == widget) {
        return;
    }

    if (m_footerWidget) {
        m_footerLayout->removeWidget(m_footerWidget);
        m_footerWidget->deleteLater();
    }

    m_footerWidget = widget;
    if (m_footerWidget) {
        m_footerWidget->setParent(m_footerHost);
        m_footerLayout->addWidget(m_footerWidget);
        m_footerHost->setVisible(true);
    } else {
        m_footerHost->setVisible(false);
    }

    updateGeometry();
}

void ImageUploadCardWidget::setPreviewFrameVisible(bool visible)
{
    if (m_previewFrameVisible == visible) {
        return;
    }

    m_previewFrameVisible = visible;
    updateStyles();
}

void ImageUploadCardWidget::setPreviewTintEnabled(bool enabled)
{
    if (m_previewTintEnabled == enabled) {
        return;
    }

    m_previewTintEnabled = enabled;
    updatePreviewPixmap();
}

void ImageUploadCardWidget::setPreviewTintMode(PreviewTintMode mode)
{
    if (m_previewTintMode == mode) {
        return;
    }

    m_previewTintMode = mode;
    updatePreviewPixmap();
}

void ImageUploadCardWidget::setSelection(const ImageUploadCardSelection& selection)
{
    applySelection(selection, true, false);
}

bool ImageUploadCardWidget::loadFromFile(const QString& filePath)
{
    const ImageUploadCardSelection selection = selectionFromFile(filePath);
    if (selection.isEmpty()) {
        emit loadFailed(filePath);
        return false;
    }

    applySelection(selection, true, true);
    return true;
}

void ImageUploadCardWidget::clearSelection()
{
    if (!hasSelection()) {
        return;
    }

    applySelection(ImageUploadCardSelection {}, true, false);
    emit selectionCleared();
}

void ImageUploadCardWidget::browseForImage()
{
    if (!m_browseEnabled) {
        return;
    }

    const QString filePath = ruwa::shared::filedialog::getOpenFileName(
        this, m_dirMemoryCategory, m_dialogTitle, m_dialogFilter);
    if (filePath.isEmpty()) {
        return;
    }

    loadFromFile(filePath);
}

void ImageUploadCardWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& theme = ThemeManager::instance();
    const auto& colors = WidgetStyleManager::instance().colors();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QColor fill = colors.overlayBase();
    if (m_dragActive) {
        fill = ThemeColors::interpolate(fill, colors.primary, 0.14);
    }
    QColor border = m_dragActive ? colors.primary : colors.borderSubtle();

    painter.setPen(QPen(border, 1.0));
    painter.setBrush(fill);
    painter.drawRoundedRect(rect, theme.scaled(kCardRadius), theme.scaled(kCardRadius));
}

void ImageUploadCardWidget::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);

    if (!m_browseEnabled || event->button() != Qt::LeftButton) {
        return;
    }

    QWidget* clickedChild = childAt(event->position().toPoint());
    if (clickedChild == m_browseButton || clickedChild == m_removeButton) {
        return;
    }

    browseForImage();
}

void ImageUploadCardWidget::dragEnterEvent(QDragEnterEvent* event)
{
    const QString filePath = firstAcceptedLocalFile(event->mimeData()->urls());
    if (filePath.isEmpty()) {
        event->ignore();
        return;
    }

    m_dragActive = true;
    update();
    event->acceptProposedAction();
}

void ImageUploadCardWidget::dragMoveEvent(QDragMoveEvent* event)
{
    const QString filePath = firstAcceptedLocalFile(event->mimeData()->urls());
    if (filePath.isEmpty()) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
}

void ImageUploadCardWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    m_dragActive = false;
    update();
}

void ImageUploadCardWidget::dropEvent(QDropEvent* event)
{
    const QString filePath = firstAcceptedLocalFile(event->mimeData()->urls());
    m_dragActive = false;
    update();

    if (filePath.isEmpty()) {
        event->ignore();
        return;
    }

    if (loadFromFile(filePath)) {
        event->acceptProposedAction();
    } else {
        event->ignore();
    }
}

void ImageUploadCardWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        updateUi();
    }
}

void ImageUploadCardWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreviewPixmap();
}

void ImageUploadCardWidget::buildUi()
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    m_stateStack = new QStackedWidget(this);
    m_stateStack->setAttribute(Qt::WA_TranslucentBackground);
    m_rootLayout->addWidget(m_stateStack);

    m_emptyPage = new QWidget(m_stateStack);
    m_emptyPage->setAttribute(Qt::WA_TranslucentBackground);
    auto* emptyLayout = new QHBoxLayout(m_emptyPage);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(0);

    auto* emptyContentWrap = new QWidget(m_emptyPage);
    emptyContentWrap->setAttribute(Qt::WA_TranslucentBackground);
    auto* emptyContentLayout = new QHBoxLayout(emptyContentWrap);
    emptyContentLayout->setContentsMargins(0, 0, 0, 0);
    emptyContentLayout->setSpacing(kBaseInnerSpacing);

    auto* emptyInfoWrap = new QWidget(emptyContentWrap);
    emptyInfoWrap->setAttribute(Qt::WA_TranslucentBackground);
    auto* emptyInfoLayout = new QHBoxLayout(emptyInfoWrap);
    emptyInfoLayout->setContentsMargins(0, 0, 0, 0);
    emptyInfoLayout->setSpacing(kBaseInnerSpacing);

    m_emptyIconLabel = new QLabel(emptyInfoWrap);
    m_emptyIconLabel->setAlignment(Qt::AlignCenter);
    m_emptyIconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* emptyTextWrap = new QWidget(emptyInfoWrap);
    emptyTextWrap->setAttribute(Qt::WA_TranslucentBackground);
    auto* emptyTextLayout = new QVBoxLayout(emptyTextWrap);
    emptyTextLayout->setContentsMargins(0, 0, 0, 0);
    emptyTextLayout->setSpacing(kBaseEmptyTextSpacing);

    m_emptyTitleLabel = new QLabel(emptyTextWrap);
    m_emptyTitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyDescriptionLabel = new QLabel(emptyTextWrap);
    m_emptyDescriptionLabel->setWordWrap(true);
    m_emptyDescriptionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_emptyDescriptionLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

    emptyTextLayout->addWidget(m_emptyTitleLabel);
    emptyTextLayout->addWidget(m_emptyDescriptionLabel);
    emptyInfoLayout->addWidget(m_emptyIconLabel, 0, Qt::AlignVCenter);
    emptyInfoLayout->addWidget(emptyTextWrap, 1, Qt::AlignVCenter);

    m_footerHost = new QWidget(emptyContentWrap);
    m_footerHost->setAttribute(Qt::WA_TranslucentBackground);
    m_footerLayout = new QVBoxLayout(m_footerHost);
    m_footerLayout->setContentsMargins(0, 0, 0, 0);
    m_footerLayout->setSpacing(0);
    m_footerHost->hide();

    emptyContentLayout->addWidget(emptyInfoWrap, 0, Qt::AlignVCenter);
    emptyContentLayout->addWidget(m_footerHost, 0, Qt::AlignVCenter);
    emptyLayout->addStretch(1);
    emptyLayout->addWidget(emptyContentWrap, 0, Qt::AlignVCenter);
    emptyLayout->addStretch(1);
    m_stateStack->addWidget(m_emptyPage);

    m_loadedPage = new QWidget(m_stateStack);
    m_loadedPage->setAttribute(Qt::WA_TranslucentBackground);
    auto* loadedLayout = new QHBoxLayout(m_loadedPage);
    loadedLayout->setContentsMargins(0, 0, 0, 0);

    m_previewFrame = new QWidget(m_loadedPage);
    m_previewFrame->setAttribute(Qt::WA_StyledBackground, true);
    auto* previewLayout = new QVBoxLayout(m_previewFrame);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    m_previewLabel = new QLabel(m_previewFrame);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_previewLabel);

    auto* infoWrap = new QWidget(m_loadedPage);
    infoWrap->setAttribute(Qt::WA_TranslucentBackground);
    auto* infoLayout = new QVBoxLayout(infoWrap);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(infoWrap);
    m_nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_metaLabel = new QLabel(infoWrap);
    m_metaLabel->setWordWrap(true);
    m_metaLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    infoLayout->addWidget(m_nameLabel);
    infoLayout->addWidget(m_metaLabel);
    infoLayout->addStretch(1);

    auto* actionsWrap = new QWidget(m_loadedPage);
    actionsWrap->setAttribute(Qt::WA_TranslucentBackground);
    m_actionsLayout = new QVBoxLayout(actionsWrap);
    m_actionsLayout->setContentsMargins(0, 0, 0, 0);

    m_browseButton
        = new CapsuleButton(m_browseText, CapsuleButton::Variant::Secondary, actionsWrap);
    m_browseButton->setBaseMinimumWidth(72);
    m_browseButton->setBannerBaseHeight(kBaseButtonHeight);
    m_browseButton->setSizeScale(0.72);
    m_browseButton->setCursor(Qt::PointingHandCursor);
    m_browseButton->setFocusPolicy(Qt::NoFocus);
    connect(m_browseButton, &QPushButton::clicked, this, &ImageUploadCardWidget::browseForImage);

    m_removeButton
        = new CapsuleButton(m_removeText, CapsuleButton::Variant::Secondary, actionsWrap);
    m_removeButton->setBaseMinimumWidth(72);
    m_removeButton->setBannerBaseHeight(kBaseButtonHeight);
    m_removeButton->setSizeScale(0.72);
    m_removeButton->setCursor(Qt::PointingHandCursor);
    m_removeButton->setFocusPolicy(Qt::NoFocus);
    connect(m_removeButton, &QPushButton::clicked, this, &ImageUploadCardWidget::clearSelection);

    m_actionsLayout->addStretch(1);
    m_actionsLayout->addWidget(m_browseButton);
    m_actionsLayout->addWidget(m_removeButton);
    m_actionsLayout->addStretch(1);

    loadedLayout->addWidget(m_previewFrame);
    loadedLayout->addWidget(infoWrap, 1);
    loadedLayout->addWidget(actionsWrap);
    m_stateStack->addWidget(m_loadedPage);
}

void ImageUploadCardWidget::updateUi()
{
    m_emptyTitleLabel->setText(m_emptyTitle);
    m_emptyDescriptionLabel->setText(m_emptyDescription);
    m_emptyDescriptionLabel->setVisible(!m_emptyDescription.isEmpty());
    setToolTip(hasSelection() ? QString() : m_emptyDescription);
    m_browseButton->setText(m_browseText);
    m_removeButton->setText(m_removeText);
    m_browseButton->syncSizeToText();
    m_removeButton->syncSizeToText();
    m_browseButton->setVisible(m_browseEnabled);
    m_stateStack->setCurrentWidget(hasSelection() ? m_loadedPage : m_emptyPage);
    if (m_footerHost) {
        m_footerHost->setVisible(m_footerWidget && !hasSelection());
    }

    const QString displayName = !m_selection.displayName.isEmpty()
        ? m_selection.displayName
        : QFileInfo(m_selection.filePath).fileName();
    m_nameLabel->setText(displayName);
    m_metaLabel->setText(m_selection.metadata.join(QStringLiteral(" | ")));
    m_metaLabel->setVisible(!m_selection.metadata.isEmpty());

    updatePreviewPixmap();
    updateStyles();
}

void ImageUploadCardWidget::updatePreviewPixmap()
{
    if (!m_previewLabel) {
        return;
    }

    if (m_selection.previewImage.isNull()) {
        m_previewLabel->clear();
        return;
    }

    QImage previewImage = m_selection.previewImage;
    if (m_previewTintEnabled) {
        previewImage = tintedPreviewImage(
            previewImage, WidgetStyleManager::instance().colors().primary, m_previewTintMode);
    }

    const QSize target = m_previewLabel->size().expandedTo(QSize(1, 1));
    const QPixmap pixmap = QPixmap::fromImage(
        previewImage.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_previewLabel->setPixmap(pixmap);
}

void ImageUploadCardWidget::updateScaledSizes()
{
    const auto& theme = ThemeManager::instance();
    setFixedHeight(theme.scaled(kBaseCardHeight));

    m_rootLayout->setContentsMargins(theme.scaled(kBaseOuterMargin), theme.scaled(kBaseOuterMargin),
        theme.scaled(kBaseOuterMargin), theme.scaled(kBaseOuterMargin));
    m_rootLayout->setSpacing(theme.scaled(kBaseOuterSpacing));

    if (auto* emptyLayout = qobject_cast<QHBoxLayout*>(m_emptyPage->layout())) {
        emptyLayout->setContentsMargins(theme.scaled(kBaseEmptyPaddingX),
            theme.scaled(kBaseEmptyPaddingY), theme.scaled(kBaseEmptyPaddingX),
            theme.scaled(kBaseEmptyPaddingY));
        emptyLayout->setSpacing(0);
    }
    if (auto* emptyContentLayout
        = qobject_cast<QHBoxLayout*>(m_footerHost->parentWidget()->layout())) {
        emptyContentLayout->setSpacing(theme.scaled(kBaseInnerSpacing));
    }
    if (auto* emptyInfoLayout
        = qobject_cast<QHBoxLayout*>(m_emptyIconLabel->parentWidget()->layout())) {
        emptyInfoLayout->setSpacing(theme.scaled(kBaseInnerSpacing));
    }
    if (auto* emptyTextLayout
        = qobject_cast<QVBoxLayout*>(m_emptyTitleLabel->parentWidget()->layout())) {
        emptyTextLayout->setSpacing(theme.scaled(kBaseEmptyTextSpacing));
    }

    if (auto* loadedLayout = qobject_cast<QHBoxLayout*>(m_loadedPage->layout())) {
        loadedLayout->setContentsMargins(theme.scaled(kBaseLoadedPadding),
            theme.scaled(kBaseLoadedPadding), theme.scaled(kBaseLoadedPadding),
            theme.scaled(kBaseLoadedPadding));
        loadedLayout->setSpacing(theme.scaled(kBaseInnerSpacing));
    }
    if (auto* infoLayout = qobject_cast<QVBoxLayout*>(m_nameLabel->parentWidget()->layout())) {
        infoLayout->setSpacing(theme.scaled(4));
    }
    if (m_actionsLayout) {
        m_actionsLayout->setSpacing(theme.scaled(kBaseActionSpacing));
    }
    if (m_footerLayout) {
        m_footerLayout->setContentsMargins(0, 0, 0, 0);
        m_footerLayout->setSpacing(0);
    }

    const int previewWidth = theme.scaled(kBasePreviewWidth);
    if (m_previewFrame) {
        m_previewFrame->setFixedWidth(previewWidth);
    }

    const int buttonHeight = theme.scaled(kBaseButtonHeight);
    for (CapsuleButton* button : { m_browseButton, m_removeButton }) {
        if (!button) {
            continue;
        }
        QFont buttonFont = button->font();
        buttonFont.setPixelSize(theme.scaled(kBaseMetaPixelSize));
        buttonFont.setWeight(QFont::Medium);
        button->setFont(buttonFont);
        button->setBannerBaseHeight(kBaseButtonHeight);
        button->setBaseMinimumWidth(72);
        button->setSizeScale(0.72);
        button->setFixedHeight(buttonHeight);
    }

    QFont titleFont = m_emptyTitleLabel->font();
    titleFont.setPixelSize(theme.scaled(kBaseTitlePixelSize));
    titleFont.setWeight(QFont::DemiBold);
    m_emptyTitleLabel->setFont(titleFont);
    m_nameLabel->setFont(titleFont);

    QFont bodyFont = m_emptyDescriptionLabel->font();
    bodyFont.setPixelSize(theme.scaled(kBaseBodyPixelSize));
    m_emptyDescriptionLabel->setFont(bodyFont);

    QFont metaFont = m_metaLabel->font();
    metaFont.setPixelSize(theme.scaled(kBaseMetaPixelSize));
    m_metaLabel->setFont(metaFont);

    const int iconSize = theme.scaled(kBaseIconSize);
    if (m_emptyIconLabel) {
        m_emptyIconLabel->setPixmap(IconProvider::instance()
                .getColoredIcon(IconProvider::StandardIcon::Import,
                    WidgetStyleManager::instance().colors().textMuted)
                .pixmap(iconSize, iconSize));
        m_emptyIconLabel->setFixedSize(iconSize, iconSize);
    }

    updatePreviewPixmap();
}

void ImageUploadCardWidget::updateStyles()
{
    const auto& colors = WidgetStyleManager::instance().colors();

    m_emptyTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_emptyDescriptionLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));
    m_nameLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_metaLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));

    if (m_previewFrameVisible) {
        QColor previewBg = colors.surfaceElevated();
        QColor previewBorder = colors.borderSubtle();
        m_previewFrame->setStyleSheet(
            QStringLiteral("QWidget { background: %1; border: 1px solid %2; border-radius: 8px; }")
                .arg(previewBg.name(QColor::HexArgb), previewBorder.name(QColor::HexArgb)));
    } else {
        m_previewFrame->setStyleSheet(QStringLiteral(
            "QWidget { background: transparent; border: none; border-radius: 0px; }"));
    }

    m_browseButton->update();
    m_removeButton->update();
}

void ImageUploadCardWidget::applySelection(
    const ImageUploadCardSelection& selection, bool emitChanged, bool emitLoadedSignal)
{
    m_selection = selection;
    if (m_selection.displayName.isEmpty() && !m_selection.filePath.isEmpty()) {
        m_selection.displayName = QFileInfo(m_selection.filePath).fileName();
    }

    updateUi();

    if (emitChanged) {
        emit selectionChanged();
    }
    if (emitLoadedSignal && !m_selection.filePath.isEmpty()) {
        emit fileLoaded(m_selection.filePath);
    }
}

QString ImageUploadCardWidget::firstAcceptedLocalFile(const QList<QUrl>& urls) const
{
    for (const QUrl& url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString filePath = url.toLocalFile();
        if (isAcceptedFile(filePath)) {
            return filePath;
        }
    }
    return QString();
}

bool ImageUploadCardWidget::isAcceptedFile(const QString& filePath) const
{
    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    if (m_acceptedSuffixesExplicit && !m_acceptedSuffixes.isEmpty()
        && !m_acceptedSuffixes.contains(info.suffix().toLower())) {
        return false;
    }

    QImageReader reader(filePath);
    return reader.canRead();
}

ImageUploadCardSelection ImageUploadCardWidget::selectionFromFile(const QString& filePath) const
{
    if (!isAcceptedFile(filePath)) {
        return {};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        return {};
    }

    ImageUploadCardSelection selection;
    selection.filePath = QFileInfo(filePath).absoluteFilePath();
    selection.displayName = QFileInfo(filePath).fileName();
    selection.previewImage = image;
    selection.metadata = metadataForFile(filePath, image);
    return selection;
}

QStringList ImageUploadCardWidget::metadataForFile(
    const QString& filePath, const QImage& image) const
{
    QStringList metadata;
    if (!image.isNull()) {
        metadata.append(QStringLiteral("%1 x %2 px").arg(image.width()).arg(image.height()));
        const QString kind = imageKindLabel(image);
        if (!kind.isEmpty()) {
            metadata.append(kind);
        }
    }

    const QString sizeText = humanFileSize(QFileInfo(filePath).size());
    if (!sizeText.isEmpty()) {
        metadata.append(sizeText);
    }
    return metadata;
}

} // namespace ruwa::ui::widgets
