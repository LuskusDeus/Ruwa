// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S   P A N E L
// ==========================================================================
//
//   Shell, state flow and derived values. The section widgets themselves are
//   built in ExportSettingsPanelSections.cpp.
//

#include "ExportSettingsPanel.h"
#include "ExportSettingsPanelInternal.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/utils/FileDialogMemory.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/inputs/NumericInputField.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/inputs/PathInputField.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRegion>
#include <QStandardPaths>
#include <QStringList>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace ruwa::ui::workspace {

namespace exporting = ruwa::core::exporting;

namespace {

/// A property row is a caption plus a field, so hiding it means hiding both
/// halves — there is no single row widget to toggle.
void setRowVisible(QLabel* caption, QWidget* field, bool visible)
{
    if (caption) {
        caption->setVisible(visible);
    }
    if (field) {
        field->setVisible(visible);
    }
}

/// Save-dialog filter with the current format first, so the dialog opens on the
/// format the panel is set to instead of whichever one happens to be listed
/// first.
QString destinationFilter(exporting::ExportFormat current)
{
    const auto entry = [](exporting::ExportFormat format) {
        const QString name = exporting::formatDisplayName(format);
        const QString suffix = QString::fromLatin1(exporting::formatCapabilities(format).suffix);
        return QStringLiteral("%1 (*.%2)").arg(name, suffix);
    };

    QStringList filters { entry(current) };
    for (const exporting::ExportFormat format : { exporting::ExportFormat::Png,
             exporting::ExportFormat::Jpeg, exporting::ExportFormat::WebP }) {
        if (format != current) {
            filters << entry(format);
        }
    }
    return filters.join(QStringLiteral(";;"));
}

} // anonymous namespace

// ---------------------------------------------------------------------------
//   L I F E C Y C L E
// ---------------------------------------------------------------------------

ExportSettingsPanel::ExportSettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);

    m_settings.loadPreferences();
    if (m_settings.directory.trimmed().isEmpty()) {
        m_settings.directory
            = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }

    buildUI();
    syncControlsFromSettings();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ExportSettingsPanel::onThemeChanged);
}

// ---------------------------------------------------------------------------
//   I N P U T S   F R O M   T H E   O W N E R
// ---------------------------------------------------------------------------

void ExportSettingsPanel::setExportFrame(const QRect& frame)
{
    m_exportFrame = frame;

    // The maxima depend on where the frame starts, so they are re-derived here
    // rather than fixed once at build time.
    applyFrameFieldRanges();

    // The frame can change from the canvas handles at any time, including while
    // the user is mid-drag on them. Writing the fields must therefore not read
    // back as an edit.
    m_syncing = true;
    if (m_widthField) {
        m_widthField->setValue(frame.width());
    }
    if (m_heightField) {
        m_heightField->setValue(frame.height());
    }
    m_syncing = false;

    recomputeOutputSize();
}

void ExportSettingsPanel::setDefaultExportFrame(const QRect& frame)
{
    m_defaultExportFrame = frame;
    if (m_resetFrameButton) {
        m_resetFrameButton->setEnabled(frame.isValid() && !frame.isEmpty());
    }
}

void ExportSettingsPanel::setFrameSizeLimit(const QSize& maxSize)
{
    const QSize normalized = (maxSize.width() > 0 && maxSize.height() > 0) ? maxSize : QSize();
    if (m_frameSizeLimit == normalized) {
        return;
    }
    m_frameSizeLimit = normalized;
    applyFrameFieldRanges();
}

void ExportSettingsPanel::applyFrameFieldRanges()
{
    if (!m_widthField || !m_heightField) {
        return;
    }

    // The frame is anchored at its top-left corner, so a frame starting at x can
    // only reach the canvas edge at (limit - x). Without that the field would
    // scrub happily past the edge and be snapped back by the owner on every
    // step, which reads as the number sticking.
    const int originX = qMax(0, m_exportFrame.x());
    const int originY = qMax(0, m_exportFrame.y());
    const int maxWidth = m_frameSizeLimit.isEmpty()
        ? exporting::kMaxOutputDimension
        : qMax(1, m_frameSizeLimit.width() - originX);
    const int maxHeight = m_frameSizeLimit.isEmpty()
        ? exporting::kMaxOutputDimension
        : qMax(1, m_frameSizeLimit.height() - originY);

    // setRange clamps the current value, which would otherwise be reported back
    // out as a fresh resize request.
    const bool wasSyncing = m_syncing;
    m_syncing = true;
    m_widthField->setRange(1, maxWidth);
    m_heightField->setRange(1, maxHeight);
    m_syncing = wasSyncing;
}

void ExportSettingsPanel::setSuggestedBaseName(const QString& baseName)
{
    m_suggestedBaseName = baseName.trimmed();

    // Only seeds an empty name: a name the user typed outlives a document
    // rename, and silently overwriting it would lose their input.
    if (m_settings.baseName.trimmed().isEmpty()) {
        updateDestinationField();
    }
}

void ExportSettingsPanel::setExportInProgress(bool running)
{
    if (!m_exportButton) {
        return;
    }
    m_exportButton->setEnabled(!running);
    m_exportButton->setTrailingLoadingVisible(running);
}

int ExportSettingsPanel::preferredHeight() const
{
    if (!m_scrollContent) {
        return sizeHint().height();
    }

    // The scroll area's own sizeHint is its viewport's and says nothing about
    // the content, so the content height comes from the reserved measurement
    // plus the chrome measured at build time. Reading the live geometry instead
    // would answer 0 before the panel has been laid out, which is exactly when
    // the controller asks.
    const int content = m_reservedContentHeight > 0 ? m_reservedContentHeight
                                                    : m_scrollContent->sizeHint().height();
    return content + m_chromeHeight;
}

// ---------------------------------------------------------------------------
//   S T A T E   F L O W
// ---------------------------------------------------------------------------

void ExportSettingsPanel::syncControlsFromSettings()
{
    m_syncing = true;

    if (m_formatSelector) {
        m_formatSelector->setCurrentIndex(static_cast<int>(m_settings.format), false);
    }
    if (m_qualitySlider) {
        m_qualitySlider->setValue(m_settings.quality);
    }
    if (m_bitDepthSelector) {
        m_bitDepthSelector->setCurrentIndex(
            m_settings.bitDepth == exporting::ExportBitDepth::Bit16 ? 1 : 0, false);
    }
    if (m_resampleCombo) {
        m_resampleCombo->setCurrentIndex(static_cast<int>(m_settings.resampleFilter));
    }
    if (m_transparencyToggle) {
        m_transparencyToggle->setCheckedInstant(m_settings.transparentBackground);
    }
    if (m_matteButton) {
        m_matteButton->setColor(m_settings.matteColor);
    }
    m_syncing = false;

    updateDestinationField();

    applyFormatSelection(static_cast<int>(m_settings.format), /*animate=*/false);
    recomputeOutputSize();
}

void ExportSettingsPanel::recomputeOutputSize()
{
    const double percent = m_scaleField ? m_scaleField->value() : 100.0;
    const double factor = percent / 100.0;

    const QSize frameSize = m_exportFrame.size();
    if (frameSize.isEmpty()) {
        m_settings.outputSize = QSize();
    } else {
        // Rounded, never zero: a 1 px frame at 10% is still one pixel of image,
        // and an output size of 0 would be rejected downstream for a reason the
        // user could not act on.
        m_settings.outputSize
            = QSize(qMax(1, qRound(frameSize.width() * factor)),
                qMax(1, qRound(frameSize.height() * factor)));
    }

    // Resampling only has meaning when the output differs from the source.
    const bool resamples = !frameSize.isEmpty() && m_settings.outputSize != frameSize;
    if (m_resampleCombo) {
        m_resampleCombo->setEnabled(resamples);
    }
    if (m_resampleCaption) {
        m_resampleCaption->setEnabled(resamples);
    }

    updateDerivedLabels();
}

void ExportSettingsPanel::applyFormatCapabilities()
{
    const exporting::ExportFormatCapabilities caps
        = exporting::formatCapabilities(m_settings.format);

    // No height bookkeeping here any more: the panel reserves room for the
    // tallest format up front (measureReservedContentHeight), so a row coming
    // and going costs empty space at the bottom instead of a resize.
    //
    // Exactly one of these belongs to any format: quality to the lossy pair,
    // bit depth to PNG.
    setRowVisible(m_qualityCaption, m_qualitySlider, caps.supportsQuality);
    setRowVisible(m_depthCaption, m_bitDepthSelector, caps.supports16Bit);

    // A format without alpha does not get to pretend the toggle does anything.
    if (m_transparencyToggle) {
        m_transparencyToggle->setEnabled(caps.supportsAlpha);
    }

    // The matte only ever shows through where alpha is being discarded.
    const bool matteApplies = !caps.supportsAlpha || !m_settings.transparentBackground;
    setRowVisible(m_matteCaption, m_matteButton, matteApplies);

    updateDerivedLabels();
}

void ExportSettingsPanel::measureReservedContentHeight()
{
    if (!m_scrollContent) {
        return;
    }

    const auto shown = [](const QWidget* widget) { return widget && !widget->isHidden(); };
    const auto measure = [this]() {
        if (QLayout* layout = m_scrollContent->layout()) {
            layout->activate();
        }
        return m_scrollContent->sizeHint().height();
    };

    const bool qualityWas = shown(m_qualitySlider);
    const bool depthWas = shown(m_bitDepthSelector);
    const bool matteWas = shown(m_matteButton);

    // The worst case for every format: its own row, plus the matte, which JPEG
    // always shows and the other two show whenever transparency is off.
    setRowVisible(m_matteCaption, m_matteButton, true);

    setRowVisible(m_qualityCaption, m_qualitySlider, true);
    setRowVisible(m_depthCaption, m_bitDepthSelector, false);
    int tallest = measure();

    setRowVisible(m_qualityCaption, m_qualitySlider, false);
    setRowVisible(m_depthCaption, m_bitDepthSelector, true);
    tallest = qMax(tallest, measure());

    setRowVisible(m_qualityCaption, m_qualitySlider, qualityWas);
    setRowVisible(m_depthCaption, m_bitDepthSelector, depthWas);
    setRowVisible(m_matteCaption, m_matteButton, matteWas);
    if (QLayout* layout = m_scrollContent->layout()) {
        layout->activate();
    }

    m_reservedContentHeight = tallest;
}

// ---------------------------------------------------------------------------
//   F O R M A T   S L I D E
// ---------------------------------------------------------------------------

void ExportSettingsPanel::applyFormatSelection(int index, bool animate)
{
    if (index < 0 || index > static_cast<int>(exporting::ExportFormat::WebP)) {
        return;
    }

    const int fromIndex = m_sectionStack ? m_sectionStack->activeIndex() : index;
    const bool slides = animate && m_sectionStack && fromIndex != index;

    // Order matters: the still has to be taken while the controls still show
    // the OLD format, because the very next thing that happens is rewriting
    // them for the new one.
    if (slides) {
        captureSectionsGhost(fromIndex);
    }
    moveSectionsToPage(index);

    m_settings.format = static_cast<exporting::ExportFormat>(index);
    applyFormatCapabilities();

    if (m_sectionStack) {
        if (slides) {
            m_sectionStack->setCurrentIndex(index);
        } else {
            m_sectionStack->setCurrentIndexWithoutAnimation(index);
        }
    }
}

void ExportSettingsPanel::moveSectionsToPage(int pageIndex)
{
    QWidget* page = m_formatPages.value(pageIndex, nullptr);
    if (!page || !m_sectionsHost || m_sectionsHost->parentWidget() == page) {
        return;
    }

    auto* pageLayout = qobject_cast<QVBoxLayout*>(page->layout());
    if (!pageLayout) {
        return;
    }

    // The block is reparented, not rebuilt: every value, every focus and every
    // scroll position it carries stays exactly as the user left it. Index 1 is
    // right after the still and before the trailing stretch — appended it would
    // land under the stretch and be pushed to the bottom of the page.
    pageLayout->insertWidget(1, m_sectionsHost);
    m_sectionsHost->show();
}

void ExportSettingsPanel::captureSectionsGhost(int pageIndex)
{
    QLabel* ghost = m_formatGhosts.value(pageIndex, nullptr);
    if (!ghost || !m_sectionsHost) {
        return;
    }

    const QSize size = m_sectionsHost->size();
    if (size.isEmpty() || !m_sectionsHost->isVisible()) {
        // Nothing on screen to slide away — the switch just cuts.
        ghost->clear();
        ghost->setFixedSize(0, 0);
        return;
    }

    // render() into a transparent image rather than grab(): the sections draw
    // no background of their own, and grab() would hand back whatever was left
    // in the pixmap underneath them.
    const qreal dpr = devicePixelRatioF();
    QImage image(size * dpr, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    m_sectionsHost->render(&image, QPoint(), QRegion(), QWidget::DrawChildren);

    ghost->setPixmap(QPixmap::fromImage(image));
    ghost->setFixedSize(size);
}

void ExportSettingsPanel::clearSectionGhosts()
{
    for (QLabel* ghost : std::as_const(m_formatGhosts)) {
        if (ghost) {
            ghost->clear();
            ghost->setFixedSize(0, 0);
        }
    }
}

void ExportSettingsPanel::updateDerivedLabels()
{
    if (m_outputSizeLabel) {
        const QSize size = m_settings.outputSize;
        m_outputSizeLabel->setText(size.isEmpty()
                ? QStringLiteral("--")
                : QStringLiteral("%1 %2 %3 px")
                      .arg(size.width())
                      .arg(QChar(0x00D7))
                      .arg(size.height()));
    }

    if (m_estimatedSizeLabel) {
        m_estimatedSizeLabel->setText(formatByteSize(estimatedExportByteSize()));
    }
}

QString ExportSettingsPanel::resolvedBaseName() const
{
    const QString typed = m_settings.baseName.trimmed();
    if (!typed.isEmpty()) {
        return typed;
    }
    if (!m_suggestedBaseName.isEmpty()) {
        return m_suggestedBaseName;
    }
    return tr("Untitled");
}

// ---------------------------------------------------------------------------
//   S L O T S
// ---------------------------------------------------------------------------

void ExportSettingsPanel::onFormatChanged(int index)
{
    if (index < 0 || index > static_cast<int>(exporting::ExportFormat::WebP)) {
        return;
    }
    applyFormatSelection(index, /*animate=*/true);

    // Keep the visible path honest about what will be written. The suffix rule
    // lives in ExportSettings::fileName(), so the field is re-rendered from the
    // settings rather than patched here.
    if (!m_syncing) {
        updateDestinationField();
    }
}

void ExportSettingsPanel::onScaleChanged(double)
{
    recomputeOutputSize();
}

void ExportSettingsPanel::onFrameFieldEdited()
{
    if (m_syncing || !m_widthField || !m_heightField) {
        return;
    }

    const QSize requested(qMax(1, static_cast<int>(std::lround(m_widthField->value()))),
        qMax(1, static_cast<int>(std::lround(m_heightField->value()))));
    if (requested == m_exportFrame.size()) {
        return;
    }

    // Requested, not applied: the frame is clamped to the document by its
    // owner, and the fields are corrected when the result comes back.
    emit exportFrameResizeRequested(requested);
}

void ExportSettingsPanel::onBrowseClicked()
{
    exporting::ExportSettings probe = m_settings;
    probe.baseName = resolvedBaseName();
    const QString suggested = probe.absolutePath().isEmpty() ? probe.fileName()
                                                             : probe.absolutePath();

    // A save dialog, not a folder picker: the field it fills holds the whole
    // path, and the dialog is the one place that offers both halves at once.
    const QString chosen = ruwa::shared::filedialog::getSaveFileName(this,
        ruwa::shared::filedialog::category::kCanvasExport, tr("Export image as"), suggested,
        destinationFilter(m_settings.format));
    if (chosen.isEmpty()) {
        return;
    }

    applyDestinationPath(chosen);

    // A name the dialog returned as ".jpg" is a format choice as much as a name,
    // so the format follows it instead of the file being silently re-suffixed.
    const exporting::ExportFormat chosenFormat
        = exporting::formatFromString(QFileInfo(chosen).suffix(), m_settings.format);
    if (chosenFormat != m_settings.format) {
        if (m_formatSelector) {
            m_formatSelector->setCurrentIndex(static_cast<int>(chosenFormat), false);
        }
        applyFormatSelection(static_cast<int>(chosenFormat), /*animate=*/true);
    }

    updateDestinationField();
}

void ExportSettingsPanel::applyDestinationPath(const QString& path)
{
    const QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if (normalized.isEmpty()) {
        m_settings.directory.clear();
        m_settings.baseName.clear();
        updateDerivedLabels();
        return;
    }

    if (!normalized.contains(QLatin1Char('/'))) {
        // A bare name: keep whichever folder is already chosen rather than
        // resolving it against the process's working directory.
        m_settings.baseName = normalized;
        updateDerivedLabels();
        return;
    }

    const QFileInfo info(normalized);
    const QString name = info.fileName();
    if (name.isEmpty()) {
        // Trailing separator - the user is still typing the folder.
        m_settings.directory = normalized;
    } else {
        m_settings.directory = info.path();
        m_settings.baseName = name;
    }
    updateDerivedLabels();
}

void ExportSettingsPanel::updateDestinationField()
{
    if (!m_destinationField) {
        return;
    }

    exporting::ExportSettings probe = m_settings;
    probe.baseName = resolvedBaseName();
    const QString shown = probe.directory.trimmed().isEmpty()
        ? probe.fileName()
        : QDir::toNativeSeparators(QDir(probe.directory).filePath(probe.fileName()));

    m_syncing = true;
    m_destinationField->setText(shown);
    m_syncing = false;
}

void ExportSettingsPanel::onExportClicked()
{
    if (m_destinationField) {
        // The field is editable, so what it shows wins over what was last
        // browsed to.
        applyDestinationPath(m_destinationField->text());
    }
    m_settings.baseName = resolvedBaseName();

    if (m_qualitySlider) {
        m_settings.quality = m_qualitySlider->value();
    }
    if (m_transparencyToggle) {
        m_settings.transparentBackground = m_transparencyToggle->isChecked();
    }
    if (m_matteButton) {
        m_settings.matteColor = m_matteButton->color();
    }

    recomputeOutputSize();
    m_settings.savePreferences();

    emit exportRequested(m_settings);
}

// ---------------------------------------------------------------------------
//   D E R I V E D   V A L U E S
// ---------------------------------------------------------------------------

qint64 ExportSettingsPanel::estimatedExportByteSize() const
{
    const QSize size = m_settings.outputSize;
    const qint64 pixels = qMax<qint64>(0, static_cast<qint64>(size.width()) * size.height());
    if (pixels <= 0) {
        return 0;
    }

    // A rough per-pixel figure, not a measurement. Real compressed size depends
    // on the picture: a flat fill and a photograph at the same resolution can
    // differ by an order of magnitude, and no formula over width/height/quality
    // can know which one this is. The label is titled "EST." for that reason —
    // an exact answer would require actually encoding the image.
    double bytesPerPixel = 1.0;
    switch (m_settings.format) {
    case exporting::ExportFormat::Png:
        bytesPerPixel = m_settings.bitDepth == exporting::ExportBitDepth::Bit16 ? 2.0 : 1.0;
        break;
    case exporting::ExportFormat::Jpeg:
        bytesPerPixel = 0.16 + (qBound(1, m_settings.quality, 100) / 100.0) * 0.42;
        break;
    case exporting::ExportFormat::WebP:
        bytesPerPixel = 0.10 + (qBound(1, m_settings.quality, 100) / 100.0) * 0.30;
        break;
    }

    return qMax<qint64>(1, static_cast<qint64>(std::llround(pixels * bytesPerPixel)));
}

QString ExportSettingsPanel::formatByteSize(qint64 bytes)
{
    if (bytes <= 0) {
        return QStringLiteral("--");
    }
    if (bytes < 1000) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1000 * 1000) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1000.0, 'f', 1));
    }
    if (bytes < 1000LL * 1000LL * 1000LL) {
        return QStringLiteral("%1 MB").arg(QString::number(bytes / 1000000.0, 'f', 1));
    }
    return QStringLiteral("%1 GB").arg(QString::number(bytes / 1000000000.0, 'f', 1));
}

// ---------------------------------------------------------------------------
//   T H E M E   /   P A I N T   /   E V E N T S
// ---------------------------------------------------------------------------

void ExportSettingsPanel::onThemeChanged()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (m_titleLabel) {
        m_titleLabel->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Label, QFont::Medium));
        m_titleLabel->setStyleSheet(
            QString("color: %1; background: transparent;").arg(colors.text.name()));
    }
    for (QLabel* label : m_sectionLabels) {
        if (label) {
            label->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Small, QFont::Medium));
            label->setStyleSheet(
                QString("color: %1; background: transparent;").arg(colors.textMuted.name()));
        }
    }
    if (m_outputSizeLabel) {
        m_outputSizeLabel->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
        m_outputSizeLabel->setStyleSheet(
            QString("color: %1; background: transparent;").arg(colors.text.name()));
    }
    if (m_estimatedSizeLabel) {
        QFont font = colors.fonts.getUIFont(theme.fontSize(ruwa::ui::core::ThemeFontRole::H5));
        font.setWeight(QFont::DemiBold);
        m_estimatedSizeLabel->setFont(font);
        m_estimatedSizeLabel->setStyleSheet(
            QString("color: %1; background: transparent;").arg(colors.text.name()));
    }

    updateHeaderIcon();
    updateExportButtonIcon();
    updateDerivedLabels();

    // Fonts and scaled metrics just changed, so the reserved height is stale.
    // This is the one thing that can still move the panel's height.
    const int previousReserved = m_reservedContentHeight;
    measureReservedContentHeight();
    if (m_reservedContentHeight != previousReserved) {
        emit preferredHeightChanged();
    }

    update();
}

void ExportSettingsPanel::updateHeaderIcon()
{
    if (!m_titleIconLabel) {
        return;
    }
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QIcon icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Export, colors.text);
    m_titleIconLabel->setPixmap(icon.pixmap(panel_metrics::kTitleIconSize, panel_metrics::kTitleIconSize));
}

void ExportSettingsPanel::updateExportButtonIcon()
{
    if (!m_exportButton) {
        return;
    }
    m_exportButton->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Export));
    m_exportButton->syncSizeToText();
}

void ExportSettingsPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    const qreal inset = panel_metrics::kBorderWidth * 0.5;
    const QRectF bodyRect = QRectF(rect()).adjusted(inset, inset, -inset, -inset);

    QPainterPath path;
    path.addRoundedRect(bodyRect, panel_metrics::kCornerRadius, panel_metrics::kCornerRadius);

    painter.fillPath(path, colors.surface);
    painter.setPen(QPen(colors.border, panel_metrics::kBorderWidth));
    painter.drawPath(path);
}

void ExportSettingsPanel::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void ExportSettingsPanel::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}

void ExportSettingsPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}

void ExportSettingsPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

} // namespace ruwa::ui::workspace
