// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S   P A N E L   S E C T I O N S
// ==========================================================================
//
//   Widget construction only. Every control is wired straight to the panel's
//   state-flow methods here and never talks to another control directly, so
//   the order in which things update is decided in one place
//   (ExportSettingsPanel.cpp) rather than emerging from the connect graph.
//

#include "ExportSettingsPanel.h"
#include "ExportSettingsPanelInternal.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/DotGridLoadingIndicator.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/inputs/NumericInputField.h"
#include "shared/widgets/inputs/PathInputField.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/widgets/layout/PropertyRowLayout.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QDir>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace ruwa::ui::workspace {

namespace exporting = ruwa::core::exporting;
namespace metrics = panel_metrics;

namespace {

// ---------------------------------------------------------------------------
//   Exit button — small rounded-square icon button.
// ---------------------------------------------------------------------------
class ExitButton final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit ExitButton(QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setFixedSize(metrics::kExitButtonSize, metrics::kExitButtonSize);
        setCheckable(false);
        setFocusPolicy(Qt::NoFocus);
        updateIcon();
        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, &ExitButton::updateIcon);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal radius = 6.0;

        const qreal hover = hoverProgress();
        if (hover > 0.001) {
            p.setPen(Qt::NoPen);
            p.setBrush(colors.overlay(0.08 * hover));
            p.drawRoundedRect(r, radius, radius);
        }

        if (!m_icon.isNull()) {
            const QPixmap pm = m_icon.pixmap(QSize(metrics::kExitIconSize, metrics::kExitIconSize));
            p.drawPixmap(QPoint((width() - pm.width()) / 2, (height() - pm.height()) / 2), pm);
        }
    }

private:
    void updateIcon()
    {
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        m_icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
            ruwa::ui::core::IconProvider::StandardIcon::Close, colors.textMuted);
        update();
    }

    QIcon m_icon;
};

class FooterDivider final : public QWidget {
public:
    explicit FooterDivider(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(8);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const qreal y = (height() - metrics::kFooterDividerHeight) * 0.5;
        const QRectF lineRect(0.0, y, width(), metrics::kFooterDividerHeight);

        p.setPen(Qt::NoPen);
        p.setBrush(colors.border);
        p.drawRoundedRect(
            lineRect, metrics::kFooterDividerHeight * 0.5, metrics::kFooterDividerHeight * 0.5);
    }
};

QLabel* makeSectionCaption(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    label->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Small, QFont::Medium));
    label->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.textMuted.name()));
    return label;
}

/// A named group: caption on top, a two-column property grid underneath.
/// Returns the group widget; `outRows` receives the grid to add rows to.
QWidget* makeSection(const QString& caption, QWidget* parent, QLabel** outCaption,
    ruwa::ui::widgets::PropertyRowLayout** outRows)
{
    auto* section = new QWidget(parent);
    section->setAttribute(Qt::WA_TranslucentBackground, true);

    auto* layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::kCaptionSpacing);

    QLabel* captionLabel = makeSectionCaption(caption, section);
    layout->addWidget(captionLabel);

    auto* rowHost = new QWidget(section);
    rowHost->setAttribute(Qt::WA_TranslucentBackground, true);
    auto* rows = new ruwa::ui::widgets::PropertyRowLayout(rowHost);
    rows->grid()->setContentsMargins(0, 0, 0, 0);
    rows->grid()->setVerticalSpacing(metrics::kRowSpacing);
    layout->addWidget(rowHost);

    if (outCaption) {
        *outCaption = captionLabel;
    }
    if (outRows) {
        *outRows = rows;
    }
    return section;
}

ruwa::ui::widgets::NumericInputField* makePixelField(QWidget* parent)
{
    auto* field = new ruwa::ui::widgets::NumericInputField(parent);
    field->setDecimals(0);
    field->setRange(1, exporting::kMaxOutputDimension);
    field->setSingleStep(1);
    field->setSuffix(QStringLiteral("px"));
    field->setFixedHeight(metrics::kFieldHeight);
    return field;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
//   R O O T
// ---------------------------------------------------------------------------

ExportSettingsPanel::~ExportSettingsPanel()
{
    qDeleteAll(m_propertyRows);
    m_propertyRows.clear();
}

void ExportSettingsPanel::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(metrics::kPanelPadding, metrics::kPanelPadding, metrics::kPanelPadding,
        metrics::kPanelPadding);
    root->setSpacing(0);

    QWidget* header = buildHeader();
    root->addWidget(header);
    root->addSpacing(metrics::kPanelPadding);

    // The settings do not fit a fixed-height panel any more, so they live in a
    // scroll area and the panel asks for the height they want (preferredHeight).
    m_scrollArea = new ruwa::ui::widgets::SmoothScrollArea(this);
    m_scrollArea->setAttribute(Qt::WA_TranslucentBackground, true);
    m_scrollArea->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollContent->setAttribute(Qt::WA_TranslucentBackground, true);
    auto* contentLayout = new QVBoxLayout(m_scrollContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(metrics::kSectionSpacing);

    // The format is not a setting among settings — it decides what the rest of
    // the panel means, so it sits above everything with no group caption of its
    // own, and switching it slides the whole settings block.
    contentLayout->addWidget(buildFormatSelector(m_scrollContent));
    contentLayout->addWidget(buildSectionStack(m_scrollContent));
    contentLayout->addStretch(1);

    m_scrollArea->setWidget(m_scrollContent);
    root->addWidget(m_scrollArea, 1);

    auto* divider = new FooterDivider(this);
    root->addWidget(divider);
    root->addSpacing(metrics::kFooterTopSpacing);

    QWidget* footer = buildFooter();
    root->addWidget(footer);

    // One caption column across every group, so the fields line up down the
    // whole panel instead of each group finding its own left edge.
    ruwa::ui::widgets::alignPropertyColumns(m_propertyRows);

    // Everything the scroll area is NOT. Measured once, from the widgets rather
    // than from the live geometry, so preferredHeight() is answerable before
    // the panel has ever been laid out.
    m_chromeHeight = 2 * metrics::kPanelPadding + header->sizeHint().height()
        + metrics::kPanelPadding + divider->sizeHint().height() + metrics::kFooterTopSpacing
        + footer->sizeHint().height();

    // Every row is visible at this point, which is the only moment the panel
    // can measure the alternatives against each other without flicker.
    measureReservedContentHeight();
}

// ---------------------------------------------------------------------------
//   H E A D E R
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildHeader()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    auto* header = new QWidget(this);
    header->setAttribute(Qt::WA_TranslucentBackground, true);

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_titleIconLabel = new QLabel(header);
    m_titleIconLabel->setFixedSize(metrics::kTitleIconSize, metrics::kExitButtonSize);
    m_titleIconLabel->setAlignment(Qt::AlignCenter);
    m_titleIconLabel->setStyleSheet(QStringLiteral("background: transparent;"));

    m_titleLabel = new QLabel(tr("Export image"), header);
    m_titleLabel->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Label, QFont::Medium));
    m_titleLabel->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.text.name()));
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setFixedHeight(metrics::kExitButtonSize);

    auto* exitBtn = new ExitButton(header);
    connect(exitBtn, &ExitButton::clicked, this, &ExportSettingsPanel::exitRequested);

    layout->addWidget(m_titleIconLabel, 0, Qt::AlignVCenter);
    layout->addWidget(m_titleLabel, 1, Qt::AlignVCenter);
    layout->addWidget(exitBtn, 0, Qt::AlignVCenter);

    updateHeaderIcon();
    return header;
}

// ---------------------------------------------------------------------------
//   F O R M A T
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildFormatSelector(QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setAttribute(Qt::WA_TranslucentBackground, true);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_formatSelector = new ruwa::ui::widgets::SegmentedOptionSelector(row);
    m_formatSelector->addOption(exporting::formatDisplayName(exporting::ExportFormat::Png));
    m_formatSelector->addOption(exporting::formatDisplayName(exporting::ExportFormat::Jpeg));
    m_formatSelector->addOption(exporting::formatDisplayName(exporting::ExportFormat::WebP));
    connect(m_formatSelector, &ruwa::ui::widgets::SegmentedOptionSelector::selectionChanged, this,
        &ExportSettingsPanel::onFormatChanged);

    // Centred, and outside the stack below: it is the control that drives the
    // slide, so it must be the one thing that does not move with it.
    layout->addStretch(1);
    layout->addWidget(m_formatSelector, 0, Qt::AlignVCenter);
    layout->addStretch(1);

    return row;
}

// ---------------------------------------------------------------------------
//   S E C T I O N   S T A C K
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildSectionStack(QWidget* parent)
{
    m_sectionStack = new ruwa::ui::widgets::AnimatedStackedWidget(parent);
    m_sectionStack->setAttribute(Qt::WA_TranslucentBackground, true);
    m_sectionStack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_sectionStack->setAnimationDuration(260);
    m_sectionStack->setAnimationEasing(QEasingCurve::InOutCubic);
    m_sectionStack->setSlideOrientation(
        ruwa::ui::widgets::AnimatedStackedWidget::SlideOrientation::Horizontal);

    // One page per format, but only ONE set of controls: the live settings
    // block is moved into the incoming page and the outgoing page shows a
    // still of how it looked a moment ago. Three real copies would mean three
    // copies of every value to keep in step, and the pages differ by exactly
    // one row — the one the format itself owns.
    for (int i = 0; i <= static_cast<int>(exporting::ExportFormat::WebP); ++i) {
        auto* page = new QWidget(m_sectionStack);
        page->setAttribute(Qt::WA_TranslucentBackground, true);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);

        auto* ghost = new QLabel(page);
        ghost->setAttribute(Qt::WA_TransparentForMouseEvents);
        ghost->setAttribute(Qt::WA_TranslucentBackground, true);
        ghost->setStyleSheet(QStringLiteral("background: transparent;"));
        ghost->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        ghost->setFixedSize(0, 0);
        pageLayout->addWidget(ghost, 0, Qt::AlignTop);
        pageLayout->addStretch(1);

        m_formatPages.append(page);
        m_formatGhosts.append(ghost);
        m_sectionStack->addWidget(page);
    }

    // Parentless on purpose: a child created directly on a QStackedWidget is
    // not adopted by its QStackedLayout and would hang unmanaged over page 0.
    // moveSectionsToPage() gives it its real parent a few lines down.
    m_sectionsHost = new QWidget;
    m_sectionsHost->setAttribute(Qt::WA_TranslucentBackground, true);
    auto* hostLayout = new QVBoxLayout(m_sectionsHost);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(metrics::kSectionSpacing);

    // Destination first: where the file lands is the decision the user checks
    // before anything else, and it is the one the Export button acts on.
    hostLayout->addWidget(buildDestinationSection(m_sectionsHost));
    hostLayout->addWidget(buildAreaSection(m_sectionsHost));
    hostLayout->addWidget(buildSizeSection(m_sectionsHost));
    hostLayout->addWidget(buildOptionsSection(m_sectionsHost));

    // The preferences are already loaded, so the block starts on the page it
    // belongs to instead of sliding there on the first paint.
    moveSectionsToPage(static_cast<int>(m_settings.format));
    m_sectionStack->setCurrentIndexWithoutAnimation(static_cast<int>(m_settings.format));

    // A still is only worth its memory while it is on screen.
    connect(m_sectionStack, &ruwa::ui::widgets::AnimatedStackedWidget::currentChanged, this,
        [this](int) { clearSectionGhosts(); });

    return m_sectionStack;
}

// ---------------------------------------------------------------------------
//   E X P O R T   A R E A
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildAreaSection(QWidget* parent)
{
    QLabel* caption = nullptr;
    ruwa::ui::widgets::PropertyRowLayout* rows = nullptr;
    QWidget* section = makeSection(tr("EXPORT AREA"), parent, &caption, &rows);
    m_sectionLabels.append(caption);
    m_propertyRows.append(rows);

    m_widthField = makePixelField(section);
    m_heightField = makePixelField(section);

    // Committed on Enter or focus-out, and live while scrubbing. NOT on every
    // valueChanged: that signal also fires per keystroke, so typing "1920"
    // would drag the frame down to 1 px on the first character.
    for (auto* field : { m_widthField, m_heightField }) {
        connect(field, &QLineEdit::editingFinished, this, &ExportSettingsPanel::onFrameFieldEdited);
        connect(field, &ruwa::ui::widgets::NumericInputField::valueChanged, this, [this](double) {
            if ((m_widthField && m_widthField->isScrubbing())
                || (m_heightField && m_heightField->isScrubbing())) {
                onFrameFieldEdited();
            }
        });
    }

    m_sectionLabels.append(rows->addRow(tr("Width"), m_widthField));
    m_sectionLabels.append(rows->addRow(tr("Height"), m_heightField));

    m_resetFrameButton = new ruwa::ui::widgets::CapsuleButton(
        tr("Reset to canvas"), ruwa::ui::widgets::CapsuleButton::Variant::Secondary, section);
    m_resetFrameButton->setBannerBaseHeight(metrics::kFieldHeight);
    m_resetFrameButton->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Reset));
    m_resetFrameButton->syncSizeToText();
    connect(m_resetFrameButton, &QPushButton::clicked, this,
        &ExportSettingsPanel::exportFrameResetRequested);
    rows->addFullWidthRow(m_resetFrameButton);

    return section;
}

// ---------------------------------------------------------------------------
//   S I Z E
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildSizeSection(QWidget* parent)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    QLabel* caption = nullptr;
    ruwa::ui::widgets::PropertyRowLayout* rows = nullptr;
    QWidget* section = makeSection(tr("SIZE"), parent, &caption, &rows);
    m_sectionLabels.append(caption);
    m_propertyRows.append(rows);

    m_scaleField = new ruwa::ui::widgets::NumericInputField(section);
    m_scaleField->setDecimals(1);
    m_scaleField->setRange(1.0, 1000.0);
    m_scaleField->setSingleStep(5.0);
    m_scaleField->setSuffix(QStringLiteral("%"));
    m_scaleField->setValue(100.0);
    m_scaleField->setFixedHeight(metrics::kFieldHeight);
    // Safe on every keystroke, unlike the frame fields: the scale only feeds
    // labels and the output size, nothing outside this panel.
    connect(m_scaleField, &ruwa::ui::widgets::NumericInputField::valueChanged, this,
        &ExportSettingsPanel::onScaleChanged);
    m_sectionLabels.append(rows->addRow(tr("Scale"), m_scaleField));

    m_resampleCombo = new ruwa::ui::widgets::AnimatedComboBox(section);
    m_resampleCombo->setFixedHeight(metrics::kFieldHeight);
    using Filter = ruwa::shared::imaging::ResampleFilter;
    m_resampleCombo->addItem(tr("Nearest"), static_cast<int>(Filter::Nearest));
    m_resampleCombo->addItem(tr("Bilinear"), static_cast<int>(Filter::Bilinear));
    m_resampleCombo->addItem(tr("Bicubic"), static_cast<int>(Filter::Bicubic));
    m_resampleCombo->addItem(tr("Mitchell"), static_cast<int>(Filter::Mitchell));
    m_resampleCombo->addItem(tr("Lanczos"), static_cast<int>(Filter::Lanczos3));
    connect(m_resampleCombo, &ruwa::ui::widgets::AnimatedComboBox::currentIndexChanged, this,
        [this](int index) {
            if (index >= 0) {
                m_settings.resampleFilter = static_cast<Filter>(index);
            }
        });
    m_resampleCaption = rows->addRow(tr("Resampling"), m_resampleCombo);
    m_sectionLabels.append(m_resampleCaption);

    m_outputSizeLabel = new QLabel(QStringLiteral("--"), section);
    m_outputSizeLabel->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
    m_outputSizeLabel->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.text.name()));
    m_sectionLabels.append(rows->addRow(tr("Output"), m_outputSizeLabel));

    return section;
}

// ---------------------------------------------------------------------------
//   O P T I O N S
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildOptionsSection(QWidget* parent)
{
    QLabel* caption = nullptr;
    ruwa::ui::widgets::PropertyRowLayout* rows = nullptr;
    QWidget* section = makeSection(tr("OPTIONS"), parent, &caption, &rows);
    m_sectionLabels.append(caption);
    m_propertyRows.append(rows);

    // Quality and Depth used to be a FORMAT group of their own. With the format
    // selector promoted out of the settings block they are what they always
    // were — one option that happens to belong to the chosen format — and only
    // the row that format supports is ever shown.
    m_qualitySlider = new ruwa::ui::widgets::ProgressHandleSlider(section);
    m_qualitySlider->setRange(1, 100);
    m_qualitySlider->setFixedHeight(metrics::kSliderHeight);
    m_qualitySlider->setShowValueText(true);
    m_qualitySlider->setValueDisplayMode(
        ruwa::ui::widgets::ProgressHandleSlider::ValueDisplayMode::RawValue);
    m_qualitySlider->setValueTextSuffix(QString());
    connect(m_qualitySlider, &ruwa::ui::widgets::ProgressHandleSlider::valueChanged, this,
        [this](int value) {
            m_settings.quality = value;
            updateDerivedLabels();
        });
    m_qualityCaption = rows->addRow(tr("Quality"), m_qualitySlider);
    m_sectionLabels.append(m_qualityCaption);

    m_bitDepthSelector = new ruwa::ui::widgets::SegmentedOptionSelector(section);
    m_bitDepthSelector->addOption(tr("8-bit"));
    m_bitDepthSelector->addOption(tr("16-bit"));
    connect(m_bitDepthSelector, &ruwa::ui::widgets::SegmentedOptionSelector::selectionChanged, this,
        [this](int index) {
            m_settings.bitDepth
                = index == 1 ? exporting::ExportBitDepth::Bit16 : exporting::ExportBitDepth::Bit8;
            updateDerivedLabels();
        });
    // Fixed-size control: without an explicit alignment the grid centres it in
    // the row, which reads as neither a label nor a field.
    m_depthCaption
        = rows->addRow(tr("Depth"), m_bitDepthSelector, Qt::AlignRight | Qt::AlignVCenter);
    m_sectionLabels.append(m_depthCaption);

    m_transparencyToggle = new ruwa::ui::widgets::ToggleSwitch(section);
    connect(m_transparencyToggle, &ruwa::ui::widgets::ToggleSwitch::toggled, this,
        [this](bool checked) {
            m_settings.transparentBackground = checked;
            applyFormatCapabilities();
        });
    m_sectionLabels.append(
        rows->addRow(tr("Transparency"), m_transparencyToggle, Qt::AlignRight | Qt::AlignVCenter));

    // Capsule swatch + hex, sized like the effect-panel colour editors. Left to
    // stretch across the whole row it drew a wide empty pill with the swatch and
    // the hex huddled at its left edge; a fixed width and a right alignment put
    // it where every other control in the panel sits.
    ruwa::ui::widgets::ColorInputButtonOptions matteOptions;
    matteOptions.showLabel = false;
    matteOptions.showHex = true;
    matteOptions.boldLabel = false;
    matteOptions.capsuleStyle = true;
    matteOptions.alphaEnabled = false;
    matteOptions.baseHeight = metrics::kColorSwatchHeight;
    m_matteButton = new ruwa::ui::widgets::ColorInputButton(
        QString(), m_settings.matteColor, matteOptions, section);
    m_matteButton->setFixedWidth(
        ruwa::ui::core::ThemeManager::instance().scaled(metrics::kColorSwatchWidth));
    connect(m_matteButton, &ruwa::ui::widgets::ColorInputButton::colorChanged, this,
        [this](const QColor& color) {
            m_settings.matteColor = color;
            // The matte is part of what gets encoded, so it moves the estimate.
            updateDerivedLabels();
        });
    connect(m_matteButton, &ruwa::ui::widgets::ColorInputButton::colorPickerRequested, this,
        [this](const QColor& color) { emit colorPickerRequested(color, m_matteButton); });
    m_matteCaption = rows->addRow(tr("Matte"), m_matteButton, Qt::AlignRight | Qt::AlignVCenter);
    m_sectionLabels.append(m_matteCaption);

    return section;
}

// ---------------------------------------------------------------------------
//   D E S T I N A T I O N
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildDestinationSection(QWidget* parent)
{
    QLabel* caption = nullptr;
    ruwa::ui::widgets::PropertyRowLayout* rows = nullptr;
    QWidget* section = makeSection(tr("DESTINATION"), parent, &caption, &rows);
    m_sectionLabels.append(caption);
    m_propertyRows.append(rows);

    // One field for the whole destination, and the browse dialog inside it.
    // The folder and the file name are a single decision — the dialog returns
    // them together — and the capsule carries its own action the way the colour
    // panel's hex field carries "copy", so nothing has to be aligned next to it.
    m_destinationField = new ruwa::ui::widgets::PathInputField(section);
    m_destinationField->setLeadingIcon(ruwa::ui::core::IconProvider::StandardIcon::Folder);
    m_destinationField->setActionIcon(ruwa::ui::core::IconProvider::StandardIcon::OpenedFolder);
    m_destinationField->setActionToolTip(tr("Browse..."));
    m_destinationField->setPlaceholderText(tr("Choose where to save"));
    connect(m_destinationField, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_syncing) {
            applyDestinationPath(text);
        }
    });
    connect(m_destinationField, &ruwa::ui::widgets::PathInputField::actionTriggered, this,
        &ExportSettingsPanel::onBrowseClicked);

    rows->addFullWidthRow(m_destinationField);

    return section;
}

// ---------------------------------------------------------------------------
//   F O O T E R
// ---------------------------------------------------------------------------

QWidget* ExportSettingsPanel::buildFooter()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    auto* footer = new QWidget(this);
    footer->setAttribute(Qt::WA_TranslucentBackground, true);

    auto* layout = new QHBoxLayout(footer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* sizeBlock = new QWidget(footer);
    sizeBlock->setAttribute(Qt::WA_TranslucentBackground, true);
    auto* sizeLayout = new QVBoxLayout(sizeBlock);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(3);

    m_estimatedSizeTitleLabel = makeSectionCaption(tr("EST. SIZE"), sizeBlock);
    m_sectionLabels.append(m_estimatedSizeTitleLabel);

    // Value + spinner on one row: the spinner takes over while the very first
    // measurement is still in flight, the label takes over the moment it lands.
    auto* valueRow = new QHBoxLayout;
    valueRow->setContentsMargins(0, 0, 0, 0);
    valueRow->setSpacing(8);

    m_estimatedSizeLabel = new QLabel(QStringLiteral("--"), sizeBlock);
    QFont sizeFont = colors.fonts.getUIFont(theme.fontSize(ruwa::ui::core::ThemeFontRole::H5));
    sizeFont.setWeight(QFont::DemiBold);
    m_estimatedSizeLabel->setFont(sizeFont);
    m_estimatedSizeLabel->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.text.name()));

    m_estimatedSizeIndicator = new ruwa::ui::widgets::DotGridLoadingIndicator(sizeBlock);
    const int indicatorSize = qMax(1, theme.scaled(14));
    m_estimatedSizeIndicator->setFixedSize(indicatorSize, indicatorSize);
    m_estimatedSizeIndicator->setAccentColor(colors.primary);
    m_estimatedSizeIndicator->hide();

    valueRow->addWidget(m_estimatedSizeLabel);
    valueRow->addWidget(m_estimatedSizeIndicator, 0, Qt::AlignVCenter);
    valueRow->addStretch(1);

    sizeLayout->addWidget(m_estimatedSizeTitleLabel);
    sizeLayout->addLayout(valueRow);

    m_exportButton = new ruwa::ui::widgets::CapsuleButton(
        tr("Export"), ruwa::ui::widgets::CapsuleButton::Variant::Primary, footer);
    m_exportButton->setBaseMinimumWidth(150);
    m_exportButton->setBannerBaseHeight(metrics::kExportButtonBaseH);
    updateExportButtonIcon();
    connect(m_exportButton, &QPushButton::clicked, this, &ExportSettingsPanel::onExportClicked);

    layout->addWidget(sizeBlock, 0, Qt::AlignLeft | Qt::AlignVCenter);
    layout->addStretch(1);
    layout->addWidget(m_exportButton, 0, Qt::AlignRight | Qt::AlignVCenter);

    return footer;
}

} // namespace ruwa::ui::workspace
