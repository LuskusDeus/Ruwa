// SPDX-License-Identifier: MPL-2.0

#include "ImageImportSelectionOverlay.h"

#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/layout/FlowLayout.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QEvent>
#include <QFrame>
#include <QFutureWatcher>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace ruwa::ui::workspace {
namespace {

constexpr int kOverlayHorizontalMargin = 32;
constexpr int kOverlayVerticalMargin = 32;
constexpr int kPanelMaxWidth = 560;
constexpr int kPanelMaxHeight = 520;
constexpr int kPanelMinWidth = 360;
constexpr int kPanelMinHeight = 260;
constexpr int kPanelRadius = 16;
constexpr int kPreviewTileWidth = 176;
constexpr int kPreviewTileHeight = 132;
constexpr int kPreviewImageInset = 8;
constexpr int kPreviewPillHeight = 24;
const QSize kPreviewTargetSize(
    kPreviewTileWidth - kPreviewImageInset * 2, kPreviewTileHeight - kPreviewImageInset * 2);

QImage preparePreviewImage(QImage image)
{
    if (image.isNull() || !kPreviewTargetSize.isValid()) {
        return {};
    }

    image = image.scaled(
        kPreviewTargetSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (image.isNull()) {
        return {};
    }

    const int cropX = std::max(0, (image.width() - kPreviewTargetSize.width()) / 2);
    const int cropY = std::max(0, (image.height() - kPreviewTargetSize.height()) / 2);
    return image.copy(cropX, cropY, kPreviewTargetSize.width(), kPreviewTargetSize.height());
}

QImage loadPreviewImage(const QString& filePath)
{
    if (filePath.isEmpty()) {
        return {};
    }

    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()) {
        const QSize scaledSize
            = sourceSize.scaled(kPreviewTargetSize, Qt::KeepAspectRatioByExpanding);
        if (scaledSize.isValid()) {
            reader.setScaledSize(scaledSize);
        }
    }

    return preparePreviewImage(reader.read());
}

} // namespace

class ImageImportPreviewTile final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit ImageImportPreviewTile(const QString& filePath, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_filePath(filePath)
    {
        setCheckable(false);
        setFixedSize(kPreviewTileWidth, kPreviewTileHeight);
        setToolTip(filePath);
        setHoverDuration(140);
    }

    explicit ImageImportPreviewTile(const QImage& image, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setCheckable(false);
        setFixedSize(kPreviewTileWidth, kPreviewTileHeight);
        setHoverDuration(140);
        setPreviewImage(image);
    }

    QString filePath() const { return m_filePath; }
    bool isSelected() const { return m_selected; }
    void setPreviewImage(const QImage& image)
    {
        const QImage prepared
            = image.size() == kPreviewTargetSize ? image : preparePreviewImage(image);
        m_preview = prepared.isNull() ? QPixmap() : QPixmap::fromImage(prepared);
        update();
    }

    void setSelected(bool selected)
    {
        if (m_selected == selected) {
            return;
        }
        m_selected = selected;
        m_selectionProgress = selected ? 1.0 : 0.0;
        update();
    }

protected:
    void mousePressEvent(QMouseEvent* event) override { QPushButton::mousePressEvent(event); }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        const bool wasPressed = isDown();
        if (wasPressed && rect().contains(event->pos()) && event->button() == Qt::LeftButton) {
            setSelected(!m_selected);
        }
        QPushButton::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const QRectF fillRect = outerRect.adjusted(1.0, 1.0, -1.0, -1.0);
        const QRectF imageRect = fillRect.adjusted(
            kPreviewImageInset, kPreviewImageInset, -kPreviewImageInset, -kPreviewImageInset);
        const qreal hover = hoverProgress();

        QColor fill = colors.surfaceElevated();
        if (hover > 0.001) {
            fill = ruwa::ui::core::ThemeColors::interpolate(fill, colors.surfaceHover(), hover);
        }
        fill = ruwa::ui::core::ThemeColors::interpolate(
            fill, colors.overlay(0.08), m_selectionProgress);

        QColor borderColor = ruwa::ui::core::ThemeColors::interpolate(
            colors.borderSubtle(), colors.primary, m_selectionProgress);
        if (m_selectionProgress < 0.001 && hover > 0.001) {
            borderColor = colors.borderSubtleHover();
        }

        painter.setBrush(fill);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(fillRect, 11.0, 11.0);

        QPainterPath borderPath;
        const QRectF borderRect = outerRect.adjusted(0.5, 0.5, -0.5, -0.5);
        borderPath.addRoundedRect(borderRect, 11.5, 11.5);
        QPen borderPen(borderColor, 1.0);
        borderPen.setCosmetic(true);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(borderPath);

        QPainterPath clipPath;
        clipPath.addRoundedRect(imageRect, 10.0, 10.0);
        painter.save();
        painter.setClipPath(clipPath);
        painter.fillRect(imageRect, colors.background);

        if (!m_preview.isNull()) {
            painter.drawPixmap(imageRect.toRect(), m_preview);
        } else {
            painter.fillRect(imageRect, colors.surface);
            const QPixmap iconPixmap
                = ruwa::ui::core::IconProvider::instance()
                      .getColoredIcon(
                          ruwa::ui::core::IconProvider::StandardIcon::Import, colors.textMuted)
                      .pixmap(QSize(22, 22));
            painter.drawPixmap(qRound(imageRect.center().x() - iconPixmap.width() / 2.0),
                qRound(imageRect.center().y() - iconPixmap.height() / 2.0), iconPixmap);
        }
        painter.restore();

        if (hover > 0.001 || m_selectionProgress > 0.001) {
            QColor overlay = colors.shadow(
                static_cast<int>(18.0 * (0.35 + hover * 0.65 + m_selectionProgress * 0.35)));
            overlay.setAlphaF(
                std::clamp(0.10 + hover * 0.08 + m_selectionProgress * 0.06, 0.0, 0.28));
            painter.setPen(Qt::NoPen);
            painter.setBrush(overlay);
            painter.drawRoundedRect(outerRect, 12.0, 12.0);
        }

        if (m_selectionProgress > 0.001) {
            painter.save();
            painter.setOpacity(m_selectionProgress);

            QFont pillFont = painter.font();
            pillFont.setPointSize(std::max(8, pillFont.pointSize()));
            pillFont.setWeight(QFont::DemiBold);
            painter.setFont(pillFont);

            const QString label = QCoreApplication::translate("ImageImportPreviewTile", "Selected");
            const int iconSize = 12;
            const int gap = 6;
            const int textWidth = painter.fontMetrics().horizontalAdvance(label);
            const int pillWidth = textWidth + iconSize + gap + 22;
            const QRectF pillRect(outerRect.center().x() - pillWidth * 0.5, outerRect.top() + 10,
                pillWidth, kPreviewPillHeight);

            QColor pillFill = colors.shadow(120);
            pillFill.setAlphaF(0.40 + m_selectionProgress * 0.20);
            painter.setPen(QPen(colors.borderSubtle(), 1.0));
            painter.setBrush(pillFill);
            painter.drawRoundedRect(pillRect, kPreviewPillHeight / 2.0, kPreviewPillHeight / 2.0);

            const QPixmap iconPixmap
                = ruwa::ui::core::IconProvider::instance()
                      .getColoredIcon(
                          ruwa::ui::core::IconProvider::StandardIcon::Import, colors.text)
                      .pixmap(QSize(iconSize, iconSize));
            const int contentX = qRound(pillRect.x() + 11);
            const int iconY = qRound(pillRect.center().y() - iconSize / 2.0);
            painter.drawPixmap(contentX, iconY, iconPixmap);

            painter.setPen(colors.text);
            painter.drawText(
                QRectF(contentX + iconSize + gap, pillRect.y(), textWidth + 4, pillRect.height()),
                Qt::AlignVCenter | Qt::AlignLeft, label);

            painter.restore();
        }
    }

private:
    QString m_filePath;
    QPixmap m_preview;
    bool m_selected = true;
    qreal m_selectionProgress = 1.0;
};

ImageImportSelectionOverlay::ImageImportSelectionOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_opacityAnimation = new QPropertyAnimation(this, "overlayOpacity", this);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_panelOffsetAnimation = new QPropertyAnimation(this, "panelOffset", this);
    m_panelOffsetAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_panel = new QFrame(this);
    m_panel->setObjectName(QStringLiteral("imageImportSelectionPanel"));
    m_panel->setAttribute(Qt::WA_StyledBackground, true);

    auto* panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(20, 18, 20, 18);
    panelLayout->setSpacing(14);

    m_titleLabel = new QLabel(tr("Import images"), m_panel);
    m_titleLabel->setObjectName(QStringLiteral("imageImportSelectionTitle"));
    panelLayout->addWidget(m_titleLabel);

    m_scrollArea = new ruwa::ui::widgets::SmoothScrollArea(m_panel);
    m_scrollArea->setObjectName(QStringLiteral("imageImportSelectionScrollArea"));
    m_scrollArea->setFillBackground(false);
    m_scrollArea->setScrollBarTransparentTrack(true);
    m_scrollArea->setContentWidthFixedToViewport(true);
    m_scrollArea->setScrollBarMargin(6);
    m_scrollArea->setAttribute(Qt::WA_TranslucentBackground, true);
    panelLayout->addWidget(m_scrollArea, 1);

    m_galleryWidget = new QWidget();
    m_galleryWidget->setAttribute(Qt::WA_StyledBackground, false);
    m_galleryWidget->setAttribute(Qt::WA_TranslucentBackground, true);
    m_galleryLayout = new ruwa::ui::widgets::FlowLayout(m_galleryWidget, 0, 12, 12);
    m_galleryWidget->setLayout(m_galleryLayout);
    m_scrollArea->setWidget(m_galleryWidget);
    if (m_scrollArea->viewport()) {
        m_scrollArea->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
        m_scrollArea->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    }

    auto* buttonsLayout = new QVBoxLayout();
    buttonsLayout->setContentsMargins(0, 4, 0, 0);
    buttonsLayout->setSpacing(10);
    buttonsLayout->setAlignment(Qt::AlignHCenter);

    m_importSmartButton = new ruwa::ui::widgets::CapsuleButton(
        tr("Import As Smart Layer"), ruwa::ui::widgets::CapsuleButton::Variant::Primary, m_panel);
    m_importSmartButton->setBaseMinimumWidth(250);
    m_importSmartButton->setBannerBaseHeight(42);
    m_importSmartButton->setSizeScale(0.88);
    buttonsLayout->addWidget(m_importSmartButton, 0, Qt::AlignHCenter);

    m_importBoardButton = new ruwa::ui::widgets::CapsuleButton(
        tr("Import As Board Layer"), ruwa::ui::widgets::CapsuleButton::Variant::Primary, m_panel);
    m_importBoardButton->setBaseMinimumWidth(250);
    m_importBoardButton->setBannerBaseHeight(42);
    m_importBoardButton->setSizeScale(0.88);
    buttonsLayout->addWidget(m_importBoardButton, 0, Qt::AlignHCenter);

    panelLayout->addLayout(buttonsLayout);
    connect(m_importSmartButton, &QPushButton::clicked, this, [this]() {
        if (m_clipboardImportActive && !m_pendingClipboardImage.isNull()) {
            const QImage image = m_pendingClipboardImage;
            resetClipboardImportState();
            hideOverlay();
            emit singleImageImportRequested(image, ImageImportMode::SmartLayer);
            return;
        }
        const QStringList filePaths = selectedFilePaths();
        if (filePaths.isEmpty()) {
            return;
        }
        hideOverlay();
        emit importRequested(filePaths, ImageImportMode::SmartLayer);
    });
    connect(m_importBoardButton, &QPushButton::clicked, this, [this]() {
        if (m_clipboardImportActive && !m_pendingClipboardImage.isNull()) {
            const QImage image = m_pendingClipboardImage;
            resetClipboardImportState();
            hideOverlay();
            emit singleImageImportRequested(image, ImageImportMode::BoardLayer);
            return;
        }
        const QStringList filePaths = selectedFilePaths();
        if (filePaths.isEmpty()) {
            return;
        }
        hideOverlay();
        emit importRequested(filePaths, ImageImportMode::BoardLayer);
    });
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            updateStyles();
            updateButtonIcon();
            if (m_importSmartButton) {
                m_importSmartButton->update();
            }
            if (m_importBoardButton) {
                m_importBoardButton->update();
            }
        });
    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]() {
        hideOverlay();
        emit cancelled();
    });

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
    }

    hideImmediate();
    updateStyles();
    updateButtonIcon();
}

void ImageImportSelectionOverlay::showForFiles(const QStringList& filePaths)
{
    populateList(filePaths);
    if (m_tiles.isEmpty()) {
        hideOverlay();
        return;
    }

    showOverlayPanel();
}

void ImageImportSelectionOverlay::showForClipboardImage(const QImage& image)
{
    showForSingleImage(image, tr("Paste image"));
}

void ImageImportSelectionOverlay::showForSingleImage(const QImage& image, const QString& title)
{
    clearGallery();
    resetClipboardImportState();
    m_clipboardImportActive = true;

    if (m_titleLabel) {
        m_titleLabel->setText(title.trimmed().isEmpty() ? tr("Import image") : title.trimmed());
    }

    auto* tile = new ImageImportPreviewTile(QString(), m_galleryWidget);
    connect(tile, &QPushButton::clicked, this,
        [this]() { QTimer::singleShot(0, this, [this]() { updateImportButtonState(); }); });
    m_galleryLayout->addWidget(tile);
    m_tiles.append(tile);
    m_clipboardTile = tile;

    setClipboardImagePreview(image);

    if (m_scrollArea) {
        m_scrollArea->refreshScrollGeometry();
    }
    updateImportButtonState();

    showOverlayPanel();
}

void ImageImportSelectionOverlay::showOverlayPanel()
{
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    updatePanelGeometry();

    const bool wasVisible = isVisible();
    m_isHiding = false;

    if (!wasVisible) {
        setOverlayOpacity(0.0);
        setPanelOffset(-PANEL_SLIDE_OFFSET);
        show();
    }

    raise();
    activateWindow();
    setFocus(Qt::OtherFocusReason);
    startShowAnimation();
}

void ImageImportSelectionOverlay::hideOverlay()
{
    if (!isVisible() || m_isHiding) {
        return;
    }
    startHideAnimation();
}

bool ImageImportSelectionOverlay::isOverlayVisible() const
{
    return isVisible();
}

void ImageImportSelectionOverlay::setOverlayOpacity(qreal opacity)
{
    m_overlayOpacity = qBound(0.0, opacity, 1.0);
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(m_overlayOpacity);
    }
    update();
}

void ImageImportSelectionOverlay::setPanelOffset(qreal offset)
{
    m_panelOffset = offset;
    updatePanelGeometry();
}

bool ImageImportSelectionOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            if (parentWidget()) {
                setGeometry(parentWidget()->rect());
            }
            updatePanelGeometry();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ImageImportSelectionOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(0, 0, 0, 110));
}

void ImageImportSelectionOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideOverlay();
        emit cancelled();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void ImageImportSelectionOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_panel->geometry().contains(event->pos())) {
        hideOverlay();
        emit cancelled();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void ImageImportSelectionOverlay::setClipboardImagePreview(const QImage& image)
{
    if (image.isNull()) {
        m_pendingClipboardImage = QImage();
        if (m_clipboardTile) {
            m_clipboardTile->setPreviewImage(QImage());
        }
        updateImportButtonState();
        return;
    }

    m_pendingClipboardImage = image;
    if (m_clipboardTile) {
        m_clipboardTile->setPreviewImage(image);
    }
    updateImportButtonState();
}

void ImageImportSelectionOverlay::queuePreviewLoad(
    ImageImportPreviewTile* tile, const QString& filePath)
{
    if (!tile || filePath.isEmpty()) {
        return;
    }

    const quint64 generation = m_previewLoadGeneration;
    QPointer<ImageImportSelectionOverlay> overlay = this;
    QPointer<ImageImportPreviewTile> tileGuard = tile;
    auto* watcher = new QFutureWatcher<QImage>(this);

    connect(watcher, &QFutureWatcher<QImage>::finished, this,
        [watcher, overlay, tileGuard, generation]() {
            watcher->deleteLater();
            if (!overlay || !tileGuard || overlay->m_previewLoadGeneration != generation) {
                return;
            }

            const QImage preview = watcher->result();
            if (!preview.isNull()) {
                tileGuard->setPreviewImage(preview);
            }
        });

    watcher->setFuture(QtConcurrent::run([filePath]() { return loadPreviewImage(filePath); }));
}

void ImageImportSelectionOverlay::clearGallery()
{
    ++m_previewLoadGeneration;
    m_clipboardTile = nullptr;

    if (m_scrollArea) {
        m_scrollArea->refreshScrollGeometry();
    }
    if (m_galleryLayout) {
        while (QLayoutItem* item = m_galleryLayout->takeAt(0)) {
            if (QWidget* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
    }
    m_tiles.clear();
}

void ImageImportSelectionOverlay::resetClipboardImportState()
{
    m_clipboardImportActive = false;
    m_pendingClipboardImage = QImage();
}

void ImageImportSelectionOverlay::populateList(const QStringList& filePaths)
{
    clearGallery();
    resetClipboardImportState();
    if (m_titleLabel) {
        m_titleLabel->setText(tr("Import images"));
    }

    for (const QString& filePath : filePaths) {
        if (filePath.isEmpty()) {
            continue;
        }

        auto* tile = new ImageImportPreviewTile(filePath, m_galleryWidget);
        connect(tile, &QPushButton::clicked, this,
            [this]() { QTimer::singleShot(0, this, [this]() { updateImportButtonState(); }); });
        m_galleryLayout->addWidget(tile);
        m_tiles.append(tile);
        queuePreviewLoad(tile, filePath);
    }

    if (m_scrollArea) {
        m_scrollArea->refreshScrollGeometry();
    }

    updateImportButtonState();
}

void ImageImportSelectionOverlay::updatePanelGeometry()
{
    if (!m_panel) {
        return;
    }

    const int targetWidth
        = qBound(kPanelMinWidth, width() - kOverlayHorizontalMargin * 2, kPanelMaxWidth);
    const int targetHeight
        = qBound(kPanelMinHeight, height() - kOverlayVerticalMargin * 2, kPanelMaxHeight);

    const QSize panelSize(targetWidth, targetHeight);
    m_panel->setFixedSize(panelSize);
    m_panel->move((width() - panelSize.width()) / 2,
        (height() - panelSize.height()) / 2 + qRound(m_panelOffset));
}

void ImageImportSelectionOverlay::updateImportButtonState()
{
    const bool hasCheckedItems = (m_clipboardImportActive && !m_pendingClipboardImage.isNull())
        || !selectedFilePaths().isEmpty();
    if (m_importSmartButton) {
        m_importSmartButton->setEnabled(hasCheckedItems);
        m_importSmartButton->setToolTip(
            hasCheckedItems ? QString() : tr("Select at least one image"));
    }
    if (m_importBoardButton) {
        m_importBoardButton->setEnabled(hasCheckedItems);
        m_importBoardButton->setToolTip(
            hasCheckedItems ? QString() : tr("Select at least one image"));
    }
}

QStringList ImageImportSelectionOverlay::selectedFilePaths() const
{
    QStringList result;
    for (const ImageImportPreviewTile* tile : m_tiles) {
        if (tile && tile->isSelected() && !tile->filePath().isEmpty()) {
            result.append(tile->filePath());
        }
    }
    return result;
}

void ImageImportSelectionOverlay::updateStyles()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    m_panel->setStyleSheet(QString(R"(
        QFrame#imageImportSelectionPanel {
            background-color: %1;
            border: 1px solid %2;
            border-radius: %3px;
        }
        QLabel#imageImportSelectionTitle {
            color: %4;
            font-size: 16px;
            font-weight: 600;
            background: transparent;
        }
        QWidget#imageImportSelectionScrollArea,
        QWidget#smooth_scroll_viewport {
            background: transparent;
            border: 1px solid %2;
            border-radius: 10px;
        }
    )")
            .arg(colors.surface.name(QColor::HexArgb), colors.borderSubtle().name(QColor::HexArgb),
                QString::number(kPanelRadius), colors.text.name(QColor::HexArgb),
                colors.surfaceElevated().name(QColor::HexArgb)));
}

void ImageImportSelectionOverlay::updateButtonIcon()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QIcon icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Import, colors.textOnPrimary());
    if (m_importSmartButton) {
        m_importSmartButton->setIcon(icon);
    }
    if (m_importBoardButton) {
        m_importBoardButton->setIcon(icon);
    }
}

void ImageImportSelectionOverlay::startShowAnimation()
{
    if (!m_opacityAnimation || !m_panelOffsetAnimation) {
        return;
    }

    m_opacityAnimation->stop();
    m_panelOffsetAnimation->stop();

    m_opacityAnimation->setDuration(SHOW_DURATION);
    m_opacityAnimation->setStartValue(m_overlayOpacity);
    m_opacityAnimation->setEndValue(1.0);
    m_opacityAnimation->start();

    m_panelOffsetAnimation->setDuration(SHOW_DURATION);
    m_panelOffsetAnimation->setStartValue(m_panelOffset);
    m_panelOffsetAnimation->setEndValue(0.0);
    m_panelOffsetAnimation->start();
}

void ImageImportSelectionOverlay::startHideAnimation()
{
    if (!m_opacityAnimation || !m_panelOffsetAnimation) {
        hideImmediate();
        return;
    }

    m_isHiding = true;
    m_opacityAnimation->stop();
    m_panelOffsetAnimation->stop();

    disconnect(m_opacityAnimation, nullptr, this, nullptr);
    connect(m_opacityAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            hideImmediate();
        }
    });

    m_opacityAnimation->setDuration(HIDE_DURATION);
    m_opacityAnimation->setStartValue(m_overlayOpacity);
    m_opacityAnimation->setEndValue(0.0);
    m_opacityAnimation->start();

    m_panelOffsetAnimation->setDuration(HIDE_DURATION);
    m_panelOffsetAnimation->setStartValue(m_panelOffset);
    m_panelOffsetAnimation->setEndValue(-PANEL_SLIDE_OFFSET);
    m_panelOffsetAnimation->start();
}

void ImageImportSelectionOverlay::hideImmediate()
{
    m_isHiding = false;
    if (m_opacityAnimation) {
        m_opacityAnimation->stop();
    }
    if (m_panelOffsetAnimation) {
        m_panelOffsetAnimation->stop();
    }
    setOverlayOpacity(0.0);
    setPanelOffset(0.0);
    resetClipboardImportState();
    hide();
}

} // namespace ruwa::ui::workspace
