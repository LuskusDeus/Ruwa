// SPDX-License-Identifier: MPL-2.0

// NewProjectContent.cpp
#include "NewProjectContent.h"
#include "ProjectSettingsField.h"
#include "ProjectPresetCard.h"
#include "CanvasThumbnail.h"
#include "AspectRatioLockButton.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/layout/FlowLayout.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/widgets/BaseStyledPanel.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/style/WidgetStyle.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QSpacerItem>
#include <QVBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QtMath>
#include <numeric>

#include <QMargins>

namespace ruwa::ui::widgets {

namespace {
constexpr int kDefaultProjectWidth = 2048;
constexpr int kDefaultProjectHeight = 2048;

const int BASE_MAIN_MARGIN_H = 36;
const int BASE_MAIN_MARGIN_V = 28;
const int BASE_MAIN_SPACING = 26;
const int BASE_HEADER_SPACING = 14;
const int BASE_TITLE_FONT_SIZE = 22;
const int BASE_CONTENT_SPACING = 32;
const int BASE_LEFT_COLUMN_SPACING = 12;
/// Доп. зазор над «Create Project» (к инпутам размеров), поверх BASE_LEFT_COLUMN_SPACING.
const int BASE_CREATE_BUTTON_TOP_EXTRA = 18;
const int BASE_PRESETS_SPACING = 12;
const int BASE_DIMENSIONS_SPACING = 12;
/// Same as StyledInputField BASE_LABEL_GAP — gap between uppercase label and control.
const int BASE_SETTINGS_FIELD_LABEL_GAP = 6;
const int BASE_FLOW_SPACING_H = 8;
const int BASE_FLOW_SPACING_V = 8;
const int BASE_TAB_SPACING_H = 6;
const int BASE_TAB_SPACING_V = 6;
const int BASE_BUTTON_HEIGHT = 52;
const int BASE_BUTTON_FONT_SIZE = 11;
/// Same step as StyledInputField label (BASE_LABEL_FONT).
const int BASE_SETTINGS_FIELD_LABEL_FONT = 9;
const int MAX_PROJECT_NAME_CHARS = 64;
/// Добавка к внешней высоте превью 16:9 (New Project). Масштабируется через ThemeManager::scaled —
/// правь только это число.
const int BASE_PREVIEW_16X9_OUTER_HEIGHT_EXTRA = 10;

QString settingsSectionLabel(const QString& raw)
{
    const QString t = raw.trimmed();
    return t.isEmpty() ? QString() : t.toUpper();
}

/// Preview shell: column sets outer width; outer height is chosen so the *inset* (thumbnail) area
/// is 16:9. BaseStyledPanel reserves layout margins for the border — naive H = W*9/16 makes the
/// inner cell wider than 16:9.
class AspectRatio16x9Frame final : public QWidget {
public:
    explicit AspectRatio16x9Frame(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(1, 1);

        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
    }

    void setInsetShell(BaseStyledPanel* shell) { m_insetShell = shell; }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override
    {
        const int effW = w > 0 ? w : ruwa::ui::core::ThemeManager::instance().scaled(320);
        return outerHeightForWidth(effW);
    }

    QSize sizeHint() const override
    {
        const int w = width() > 0 ? width() : ruwa::ui::core::ThemeManager::instance().scaled(320);
        return QSize(w, heightForWidth(w));
    }

    QSize minimumSizeHint() const override
    {
        const int w = ruwa::ui::core::ThemeManager::instance().scaled(320);
        return QSize(w, heightForWidth(w));
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        applyOuterHeightForCurrentWidth();
    }

private:
    static int fallbackLayoutInsetPerSide()
    {
        return qCeil(ruwa::ui::core::WidgetStyle::settingsPanelStyle().border.width);
    }

    void insetMargins(int& left, int& top, int& right, int& bottom) const
    {
        const int fb = fallbackLayoutInsetPerSide();
        left = top = right = bottom = fb;
        if (m_insetShell && m_insetShell->layout()) {
            const QMargins mg = m_insetShell->layout()->contentsMargins();
            left = qMax(fb, mg.left());
            top = qMax(fb, mg.top());
            right = qMax(fb, mg.right());
            bottom = qMax(fb, mg.bottom());
        }
    }

    int outerHeightForWidth(int outerW) const
    {
        int l = 0;
        int t = 0;
        int r = 0;
        int b = 0;
        insetMargins(l, t, r, b);
        const int innerW = qMax(1, outerW - l - r);
        const int innerH = qMax(1, qRound(static_cast<qreal>(innerW) * 9.0 / 16.0));
        const int extra
            = ruwa::ui::core::ThemeManager::instance().scaled(BASE_PREVIEW_16X9_OUTER_HEIGHT_EXTRA);
        return innerH + t + b + extra;
    }

    void applyOuterHeightForCurrentWidth()
    {
        const int w = width();
        if (w <= 0) {
            setMinimumHeight(1);
            setMaximumHeight(QWIDGETSIZE_MAX);
            return;
        }
        const int h = outerHeightForWidth(w);
        if (minimumHeight() != h || maximumHeight() != h) {
            setMinimumHeight(h);
            setMaximumHeight(h);
            updateGeometry();
        }
    }

    BaseStyledPanel* m_insetShell = nullptr;
};
} // namespace

NewProjectContent::NewProjectContent(QWidget* parent)
    : HomePageContent(parent)
{
    setupContent();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &NewProjectContent::onThemeChanged);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, [this]() { retranslateUi(); });
}

void NewProjectContent::setupContent()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));

    // Header: title only (ratio badge removed — shown inside the canvas thumbnail)
    QWidget* headerRow = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(headerRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(theme.scaled(BASE_HEADER_SPACING));

    m_titleLabel = new QLabel(tr("New Project"), headerRow);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();
    mainLayout->addWidget(headerRow);

    QHBoxLayout* contentLayout = new QHBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(theme.scaled(BASE_CONTENT_SPACING));

    // ── Left column ──
    QWidget* leftColumn = new QWidget(this);
    leftColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(theme.scaled(BASE_LEFT_COLUMN_SPACING));

    auto* previewFrame = new AspectRatio16x9Frame(leftColumn);
    QVBoxLayout* previewFrameLayout = new QVBoxLayout(previewFrame);
    previewFrameLayout->setContentsMargins(0, 0, 0, 0);
    previewFrameLayout->setSpacing(0);

    BaseStyledPanel* previewShell
        = new BaseStyledPanel(ruwa::ui::core::WidgetStyle::settingsPanelStyle(), previewFrame);
    previewShell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewShell->setHoverEnabled(false);

    QVBoxLayout* previewLayout = new QVBoxLayout(previewShell);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(0);

    m_canvasThumbnail = new CanvasThumbnail(QSize(320, 180), previewShell);
    m_canvasThumbnail->setDimensions(kDefaultProjectWidth, kDefaultProjectHeight);
    previewLayout->addWidget(m_canvasThumbnail, 1);

    previewFrameLayout->addWidget(previewShell, 1);
    previewFrame->setInsetShell(previewShell);
    leftLayout->addWidget(previewFrame);

    createSettingsPanel(leftColumn);
    leftLayout->addWidget(m_projectNameField);

    m_canvasBoundsSection = new QWidget(leftColumn);
    QVBoxLayout* canvasBoundsLayout = new QVBoxLayout(m_canvasBoundsSection);
    canvasBoundsLayout->setContentsMargins(0, 0, 0, 0);
    canvasBoundsLayout->setSpacing(theme.scaled(BASE_SETTINGS_FIELD_LABEL_GAP));

    m_canvasBoundsTitleLabel
        = new QLabel(settingsSectionLabel(tr("Canvas")), m_canvasBoundsSection);
    m_canvasBoundsTitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_canvasBoundsTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    canvasBoundsLayout->addWidget(m_canvasBoundsTitleLabel);

    m_canvasBoundsSelector = new SegmentedOptionSelector(m_canvasBoundsSection);
    {
        SegmentedOptionSelector::Option classicOpt;
        classicOpt.text = tr("Classic canvas");
        SegmentedOptionSelector::Option infiniteOpt;
        infiniteOpt.text = tr("Infinite canvas");
        m_canvasBoundsSelector->setOptions({ classicOpt, infiniteOpt });
    }
    m_canvasBoundsSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    m_canvasBoundsSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_canvasBoundsSelector->setCurrentIndex(0, false);
    connect(m_canvasBoundsSelector, &SegmentedOptionSelector::selectionChanged, this,
        &NewProjectContent::onCanvasBoundsSelectionChanged);
    canvasBoundsLayout->addWidget(m_canvasBoundsSelector);
    if (m_canvasThumbnail) {
        m_canvasThumbnail->setInfiniteCanvasEnabled(false);
    }

    leftLayout->addWidget(m_canvasBoundsSection);

    // ── Color depth (per-document tile pixel format: 8 / 16 / 32-bit float) ──
    m_bitDepthSection = new QWidget(leftColumn);
    QVBoxLayout* bitDepthLayout = new QVBoxLayout(m_bitDepthSection);
    bitDepthLayout->setContentsMargins(0, 0, 0, 0);
    bitDepthLayout->setSpacing(theme.scaled(BASE_SETTINGS_FIELD_LABEL_GAP));

    m_bitDepthTitleLabel = new QLabel(settingsSectionLabel(tr("Color Depth")), m_bitDepthSection);
    m_bitDepthTitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_bitDepthTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    bitDepthLayout->addWidget(m_bitDepthTitleLabel);

    m_bitDepthSelector = new SegmentedOptionSelector(m_bitDepthSection);
    {
        SegmentedOptionSelector::Option opt8;
        opt8.text = tr("8-bit");
        SegmentedOptionSelector::Option opt16;
        opt16.text = tr("16-bit");
        SegmentedOptionSelector::Option opt32;
        opt32.text = tr("32-bit float");
        m_bitDepthSelector->setOptions({ opt8, opt16, opt32 });
    }
    m_bitDepthSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    m_bitDepthSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // enum order RGBA8=0 / RGBA16F=1 / RGBA32F=2 maps 1:1 to the option index.
    m_bitDepthSelector->setCurrentIndex(static_cast<int>(aether::kDefaultTileFormat), false);
    connect(m_bitDepthSelector, &SegmentedOptionSelector::selectionChanged, this,
        [this](int) { updateMemoryLabel(); });
    bitDepthLayout->addWidget(m_bitDepthSelector);

    leftLayout->addWidget(m_bitDepthSection);

    syncDimensionFieldsEnabledState();
    QTimer::singleShot(0, this, [this]() { syncLockColumnLayout(); });

    QHBoxLayout* dimensionsLayout = new QHBoxLayout();
    dimensionsLayout->setContentsMargins(0, 0, 0, 0);
    dimensionsLayout->setSpacing(theme.scaled(BASE_DIMENSIONS_SPACING));
    dimensionsLayout->addWidget(m_widthField, 0, Qt::AlignTop);

    // Lock sits in a column with stretch above so it lines up with the spinbox row,
    // not vertically centered against label+field (cf. reference link icon flex-end).
    m_lockColumn = new QWidget(leftColumn);
    // Fixed height is set in syncLockColumnLayout() to match the dimension fields — Expanding here
    // inflated the whole dimensions row and the left column layout (giant gap above Create).
    m_lockColumn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_lockColumnLayout = new QVBoxLayout(m_lockColumn);
    m_lockColumnLayout->setContentsMargins(0, 0, 0, 0);
    m_lockColumnLayout->setSpacing(0);
    m_lockColumnTopSpacer = new QSpacerItem(0, 1, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_lockColumnLayout->addItem(m_lockColumnTopSpacer);
    m_lockColumnLayout->addWidget(m_aspectLockButton, 0, Qt::AlignHCenter);
    m_lockColumnLayout->addStretch(1);
    dimensionsLayout->addWidget(m_lockColumn, 0, Qt::AlignTop);

    dimensionsLayout->addWidget(m_heightField, 0, Qt::AlignTop);
    leftLayout->addLayout(dimensionsLayout);

    m_backgroundColorSection = new QWidget(leftColumn);
    QVBoxLayout* backgroundColorLayout = new QVBoxLayout(m_backgroundColorSection);
    backgroundColorLayout->setContentsMargins(0, 0, 0, 0);
    backgroundColorLayout->setSpacing(theme.scaled(BASE_SETTINGS_FIELD_LABEL_GAP));

    m_backgroundColorTitleLabel
        = new QLabel(settingsSectionLabel(tr("Background Color")), m_backgroundColorSection);
    m_backgroundColorTitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_backgroundColorTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    backgroundColorLayout->addWidget(m_backgroundColorTitleLabel);

    ColorInputButtonOptions backgroundOptions;
    backgroundOptions.boldLabel = false;
    backgroundOptions.showLabel = false;
    backgroundOptions.showHex = true;
    backgroundOptions.boxedStyle = true;
    backgroundOptions.baseHeight = 36;

    m_backgroundColorInput = new ColorInputButton(
        QString(), m_backgroundColor, backgroundOptions, m_backgroundColorSection);
    m_backgroundColorInput->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_backgroundColorInput, &ColorInputButton::colorChanged, this,
        [this](const QColor& color) { m_backgroundColor = color; });
    connect(m_backgroundColorInput, &ColorInputButton::colorPickerRequested, this,
        [this](const QColor& color) { emit colorPickerRequested(color, m_backgroundColorInput); });
    backgroundColorLayout->addWidget(m_backgroundColorInput);
    leftLayout->addWidget(m_backgroundColorSection);

    leftLayout->addSpacing(theme.scaled(BASE_CREATE_BUTTON_TOP_EXTRA));

    // Action row: capsule create button (hint text embedded)
    QWidget* actionRow = new QWidget(leftColumn);
    actionRow->setAttribute(Qt::WA_TranslucentBackground);
    QHBoxLayout* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 0, 0, 0);
    actionLayout->setSpacing(0);

    m_createButton
        = new CapsuleButton(tr("Create Project"), CapsuleButton::Variant::Action, actionRow);
    m_createButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_createButton->setFixedHeight(theme.scaled(BASE_BUTTON_HEIGHT));
    connect(m_createButton, &QPushButton::clicked, this, [this]() {
        emit projectCreateRequested(projectName(), canvasSize(), infiniteCanvasEnabled(),
            colorMode(), backgroundColor(), tileFormat());
    });

    actionLayout->addWidget(m_createButton);
    actionLayout->addStretch();

    leftLayout->addWidget(actionRow);
    leftLayout->addStretch();

    contentLayout->addWidget(leftColumn, 42);

    createPresets();
    contentLayout->addWidget(m_presetsPanel, 58);

    mainLayout->addLayout(contentLayout, 1);

    updateScaledSizes();
    updateThemeColors();
    if (m_canvasThumbnail) {
        m_canvasThumbnail->setProjectName(projectName());
    }
    updateMemoryLabel();

    // One-time: pre-select the preset matching the default canvas size (2048x2048 == Quick Sketch).
    // Applied only here at construction — deliberately NOT a persistent "dims match preset ->
    // select" rule, so editing W/H later still clears the selection as usual.
    const QSize defaultSize(kDefaultProjectWidth, kDefaultProjectHeight);
    for (auto it = m_presetCards.cbegin(); it != m_presetCards.cend(); ++it) {
        if (it.value() && it.value()->dimensions() == defaultSize) {
            onPresetSelected(it.key());
            break;
        }
    }
}

void NewProjectContent::changeEvent(QEvent* event)
{
    HomePageContent::changeEvent(event);
    // Retranslation is driven by TranslationManager::languageChanged so it runs after
    // installTranslator (including async Russian load), not only on queued LanguageChange.
}

void NewProjectContent::resizeEvent(QResizeEvent* event)
{
    HomePageContent::resizeEvent(event);
    syncLockColumnLayout();
}

void NewProjectContent::showEvent(QShowEvent* event)
{
    HomePageContent::showEvent(event);
    syncLockColumnLayout();
}

void NewProjectContent::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(tr("New Project"));
    if (m_projectNameField) {
        m_projectNameField->setLabel(tr("Name"));
        m_projectNameField->setPlaceholder(tr("Untitled Project"));
        QString cur = m_projectNameField->text();
        if (cur == "Untitled Project" || cur.isEmpty())
            m_projectNameField->setText(tr("Untitled Project"));
    }
    if (m_widthField)
        m_widthField->setLabel(tr("Width"));
    if (m_heightField)
        m_heightField->setLabel(tr("Height"));
    if (m_backgroundColorTitleLabel) {
        m_backgroundColorTitleLabel->setText(settingsSectionLabel(tr("Background Color")));
    }
    if (m_canvasBoundsTitleLabel) {
        m_canvasBoundsTitleLabel->setText(settingsSectionLabel(tr("Canvas")));
    }
    if (m_canvasBoundsSelector) {
        m_canvasBoundsSelector->setOptionText(0, tr("Classic canvas"));
        m_canvasBoundsSelector->setOptionText(1, tr("Infinite canvas"));
    }
    if (m_bitDepthTitleLabel) {
        m_bitDepthTitleLabel->setText(settingsSectionLabel(tr("Color Depth")));
    }
    if (m_bitDepthSelector) {
        m_bitDepthSelector->setOptionText(0, tr("8-bit"));
        m_bitDepthSelector->setOptionText(1, tr("16-bit"));
        m_bitDepthSelector->setOptionText(2, tr("32-bit float"));
    }
    if (m_createButton)
        m_createButton->setText(tr("Create Project"));

    const QStringList categories = { tr("Basics"), tr("Screen"), tr("Illustration"),
        tr("Comics & Manga"), tr("Print"), tr("Covers & Posters"), tr("Pixel Art") };
    for (int i = 0; i < m_categoryButtons.size() && i < categories.size(); ++i)
        m_categoryButtons[i]->setText(categories[i]);

    const QList<ProjectPresetCard*> presetCardList = m_presetCards.values();
    for (ProjectPresetCard* card : presetCardList) {
        if (card) {
            card->update();
        }
    }

    QTimer::singleShot(0, this, [this]() { syncLockColumnLayout(); });
}

QString NewProjectContent::projectName() const
{
    return m_projectNameField ? m_projectNameField->text() : tr("Untitled Project");
}

QSize NewProjectContent::canvasSize() const
{
    int width = m_widthField ? m_widthField->value() : kDefaultProjectWidth;
    int height = m_heightField ? m_heightField->value() : kDefaultProjectHeight;
    return QSize(width, height);
}

bool NewProjectContent::infiniteCanvasEnabled() const
{
    return m_canvasBoundsSelector && m_canvasBoundsSelector->currentIndex() == 1;
}

QString NewProjectContent::colorMode() const
{
    return tr("RGB Color");
}

QColor NewProjectContent::backgroundColor() const
{
    return m_backgroundColorInput ? m_backgroundColorInput->color() : m_backgroundColor;
}

aether::TilePixelFormat NewProjectContent::tileFormat() const
{
    const int idx = m_bitDepthSelector ? m_bitDepthSelector->currentIndex()
                                       : static_cast<int>(aether::kDefaultTileFormat);
    switch (idx) {
    case 0:
        return aether::TilePixelFormat::RGBA8;
    case 1:
        return aether::TilePixelFormat::RGBA16F;
    case 2:
        return aether::TilePixelFormat::RGBA32F;
    }
    return aether::kDefaultTileFormat;
}

void NewProjectContent::createSettingsPanel(QWidget* fieldParent)
{
    m_projectNameField
        = new ProjectSettingsField(tr("Name"), ProjectSettingsField::FieldType::Text, fieldParent);
    m_projectNameField->setMaxLength(MAX_PROJECT_NAME_CHARS);
    m_projectNameField->setPlaceholder(tr("Untitled Project"));
    m_projectNameField->setText(tr("Untitled Project"));
    connect(m_projectNameField, &ProjectSettingsField::textChanged, this, [this]() {
        if (m_canvasThumbnail)
            m_canvasThumbnail->setProjectName(projectName());
    });

    m_widthField = new ProjectSettingsField(
        tr("Width"), ProjectSettingsField::FieldType::Number, fieldParent);
    m_widthField->setRange(1, 100000);
    m_widthField->setValue(kDefaultProjectWidth);
    connect(m_widthField, &ProjectSettingsField::valueChanged, this,
        &NewProjectContent::onWidthChanged);

    m_aspectLockButton = new AspectRatioLockButton(fieldParent);
    connect(
        m_aspectLockButton, &QPushButton::toggled, this, &NewProjectContent::onAspectLockToggled);

    m_heightField = new ProjectSettingsField(
        tr("Height"), ProjectSettingsField::FieldType::Number, fieldParent);
    m_heightField->setRange(1, 100000);
    m_heightField->setValue(kDefaultProjectHeight);
    connect(m_heightField, &ProjectSettingsField::valueChanged, this,
        &NewProjectContent::onHeightChanged);
}

void NewProjectContent::createPresets()
{
    m_presetsPanel = new QWidget(this);

    QVBoxLayout* layout = new QVBoxLayout(m_presetsPanel);
    layout->setContentsMargins(0, 0, 0, 0);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    layout->setSpacing(theme.scaled(BASE_PRESETS_SPACING));

    // Category capsule tabs (no title/hint labels per design)
    QWidget* tabsWidget = new QWidget(m_presetsPanel);
    FlowLayout* tabsLayout
        = new FlowLayout(-1, theme.scaled(BASE_TAB_SPACING_H), theme.scaled(BASE_TAB_SPACING_V));
    tabsLayout->setContentsMargins(0, 0, 0, 0);
    tabsWidget->setLayout(tabsLayout);
    layout->addWidget(tabsWidget);

    SmoothScrollArea* scrollArea = new SmoothScrollArea(m_presetsPanel);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QWidget* scrollContent = new QWidget();
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(0);

    m_presetsStack = new AnimatedStackedWidget(scrollContent);
    m_presetsStack->setSlideOrientation(AnimatedStackedWidget::SlideOrientation::Horizontal);
    m_presetsStack->setAnimationDuration(250);
    m_presetsStack->setAnimationEasing(QEasingCurve::OutCubic);
    m_presetsStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    scrollLayout->addWidget(m_presetsStack);

    // English keys only — UI strings come from QCoreApplication::translate (same context as tr()).
    struct Preset {
        QString nameKey;
        QSize size;
    };
    struct PresetCategory {
        QString nameKey;
        QList<Preset> presets;
    };

    QList<PresetCategory> categories = {
        { QStringLiteral("Basics"),
            {
                { QStringLiteral("Quick Sketch"), QSize(2048, 2048) },
                { QStringLiteral("Square Canvas"), QSize(3000, 3000) },
                { QStringLiteral("Landscape Canvas"), QSize(4000, 3000) },
                { QStringLiteral("Portrait Canvas"), QSize(3000, 4000) },
                { QStringLiteral("Large Square"), QSize(4096, 4096) },
            } },
        { QStringLiteral("Screen"),
            {
                { QStringLiteral("Full HD"), QSize(1920, 1080) },
                { QStringLiteral("QHD"), QSize(2560, 1440) },
                { QStringLiteral("4K UHD"), QSize(3840, 2160) },
                { QStringLiteral("Ultrawide"), QSize(3440, 1440) },
                { QStringLiteral("Vertical Screen"), QSize(2160, 3840) },
            } },
        { QStringLiteral("Illustration"),
            {
                { QStringLiteral("Illustration Portrait"), QSize(4000, 5000) },
                { QStringLiteral("Illustration Large Portrait"), QSize(5000, 7000) },
                { QStringLiteral("Illustration Landscape"), QSize(6000, 4000) },
                { QStringLiteral("Cinematic Matte"), QSize(6000, 3375) },
                { QStringLiteral("Concept Sheet"), QSize(7000, 7000) },
            } },
        { QStringLiteral("Comics & Manga"),
            {
                { QStringLiteral("Manga A4"), QSize(2480, 3508) },
                { QStringLiteral("Manga B5"), QSize(2079, 2953) },
                { QStringLiteral("US Comic Page"), QSize(2550, 3900) },
                { QStringLiteral("Comic Spread"), QSize(5100, 3900) },
                { QStringLiteral("Webtoon Episode"), QSize(1600, 12000) },
            } },
        { QStringLiteral("Print"),
            {
                { QStringLiteral("A5"), QSize(1748, 2480) },
                { QStringLiteral("A4"), QSize(2480, 3508) },
                { QStringLiteral("A3"), QSize(3508, 4961) },
                { QStringLiteral("Letter"), QSize(2550, 3300) },
                { QStringLiteral("Poster A2"), QSize(4961, 7016) },
                { QStringLiteral("Poster A1"), QSize(7016, 9933) },
            } },
        { QStringLiteral("Covers & Posters"),
            {
                { QStringLiteral("Book Cover"), QSize(3000, 4500) },
                { QStringLiteral("Album Cover"), QSize(3000, 3000) },
                { QStringLiteral("Vertical Poster"), QSize(4000, 6000) },
                { QStringLiteral("Landscape Poster"), QSize(6000, 4000) },
                { QStringLiteral("Square Cover"), QSize(4000, 4000) },
            } },
        { QStringLiteral("Pixel Art"),
            {
                { QStringLiteral("Sprite 64"), QSize(64, 64) },
                { QStringLiteral("Sprite 128"), QSize(128, 128) },
                { QStringLiteral("Tile Set 256"), QSize(256, 256) },
                { QStringLiteral("Pixel Scene"), QSize(512, 512) },
                { QStringLiteral("Pixel HD"), QSize(1024, 1024) },
            } },
    };

    m_categoryButtons.clear();
    m_presetCards.clear();
    m_presetCategoryIndices.clear();

    const char* presetCtx = metaObject()->className();

    for (int categoryIndex = 0; categoryIndex < categories.size(); ++categoryIndex) {
        const auto& category = categories[categoryIndex];

        QString categoryTitle;
        {
            const QByteArray catUtf8 = category.nameKey.toUtf8();
            categoryTitle = QCoreApplication::translate(presetCtx, catUtf8.constData());
        }
        CapsuleButton* categoryButton
            = new CapsuleButton(categoryTitle, CapsuleButton::Variant::Tab, tabsWidget);
        connect(categoryButton, &QPushButton::clicked, this,
            [this, categoryIndex]() { setActivePresetCategory(categoryIndex); });
        tabsLayout->addWidget(categoryButton);
        m_categoryButtons.append(categoryButton);

        QWidget* page = new QWidget(m_presetsStack);
        QVBoxLayout* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(0);

        FlowLayout* flowLayout = new FlowLayout(
            -1, theme.scaled(BASE_FLOW_SPACING_H), theme.scaled(BASE_FLOW_SPACING_V));
        flowLayout->setContentsMargins(0, 0, 0, 0);
        flowLayout->setMaxColumns(2);

        for (const auto& preset : category.presets) {
            ProjectPresetCard* card = new ProjectPresetCard(preset.nameKey, preset.size, page);
            connect(card, &ProjectPresetCard::clicked, this,
                [this, key = preset.nameKey]() { onPresetSelected(key); });
            m_presetCards[preset.nameKey] = card;
            m_presetCategoryIndices[preset.nameKey] = categoryIndex;
            flowLayout->addWidget(card);
        }

        pageLayout->addLayout(flowLayout);
        pageLayout->addStretch();
        m_presetsStack->addWidget(page);
    }

    scrollContent->setLayout(scrollLayout);
    scrollArea->setWidget(scrollContent);
    layout->addWidget(scrollArea, 1);

    setActivePresetCategory(0);
}

void NewProjectContent::mousePressEvent(QMouseEvent* event)
{
    clearAllInputFocus();
    HomePageContent::mousePressEvent(event);
}

void NewProjectContent::clearAllInputFocus()
{
    if (m_projectNameField)
        m_projectNameField->clearInputFocus();
    if (m_widthField)
        m_widthField->clearInputFocus();
    if (m_heightField)
        m_heightField->clearInputFocus();
}

void NewProjectContent::onPresetSelected(const QString& presetName)
{
    clearAllInputFocus();

    if (m_canvasBoundsSelector && m_canvasBoundsSelector->currentIndex() != 0) {
        QSignalBlocker blocker(m_canvasBoundsSelector);
        m_canvasBoundsSelector->setCurrentIndex(0, false);
        syncDimensionFieldsEnabledState();
        if (m_canvasThumbnail) {
            m_canvasThumbnail->setInfiniteCanvasEnabled(false);
        }
        QTimer::singleShot(0, this, [this]() { syncLockColumnLayout(); });
    }

    if (!m_selectedPreset.isEmpty() && m_presetCards.contains(m_selectedPreset)) {
        m_presetCards[m_selectedPreset]->setSelected(false);
    }

    m_selectedPreset = presetName;
    if (m_presetCards.contains(presetName)) {
        ProjectPresetCard* card = m_presetCards[presetName];
        card->setSelected(true);

        if (m_presetCategoryIndices.contains(presetName)) {
            setActivePresetCategory(m_presetCategoryIndices.value(presetName));
        }
        updateDimensionsFromPreset(card->dimensions());
        if (m_canvasThumbnail)
            m_canvasThumbnail->setProjectName(projectName());
        updateMemoryLabel();
    }
}

void NewProjectContent::onWidthChanged(int width)
{
    clearSelectedPreset();
    syncLockedHeightFromWidth(width);
    if (m_canvasThumbnail)
        m_canvasThumbnail->setProjectName(projectName());
    updateThumbnail();
    updateMemoryLabel();
}

void NewProjectContent::onHeightChanged(int height)
{
    clearSelectedPreset();
    syncLockedWidthFromHeight(height);
    if (m_canvasThumbnail)
        m_canvasThumbnail->setProjectName(projectName());
    updateThumbnail();
    updateMemoryLabel();
}

void NewProjectContent::onAspectLockToggled(bool locked)
{
    if (!locked) {
        m_lockedAspectRatio = 0.0;
        return;
    }
    updateLockedAspectRatio();
}

void NewProjectContent::onCanvasBoundsSelectionChanged(int)
{
    syncDimensionFieldsEnabledState();
    if (m_canvasThumbnail) {
        m_canvasThumbnail->setInfiniteCanvasEnabled(infiniteCanvasEnabled());
    }
    QTimer::singleShot(0, this, [this]() { syncLockColumnLayout(); });
}

void NewProjectContent::syncDimensionFieldsEnabledState()
{
    const bool classic = !m_canvasBoundsSelector || m_canvasBoundsSelector->currentIndex() == 0;
    if (m_widthField)
        m_widthField->setEnabled(classic);
    if (m_heightField)
        m_heightField->setEnabled(classic);
    if (m_aspectLockButton)
        m_aspectLockButton->setEnabled(classic);
}

void NewProjectContent::updateDimensionsFromPreset(const QSize& dimensions)
{
    m_widthField->blockSignals(true);
    m_heightField->blockSignals(true);

    m_widthField->setValue(dimensions.width());
    m_heightField->setValue(dimensions.height());

    m_widthField->blockSignals(false);
    m_heightField->blockSignals(false);

    m_canvasThumbnail->setDimensions(dimensions);
    if (m_aspectLockButton && m_aspectLockButton->isLocked()) {
        updateLockedAspectRatio();
    }
}

void NewProjectContent::updateMemoryLabel()
{
    if (!m_createButton)
        return;

    int w = m_widthField ? m_widthField->value() : 1920;
    int h = m_heightField ? m_heightField->value() : 1080;

    const double bytesPerPixel = static_cast<double>(aether::tileBytesPerPixel(tileFormat()));
    double mb = static_cast<double>(w) * h * bytesPerPixel / (1024.0 * 1024.0);

    QString hint;
    if (mb >= 1000.0)
        hint = QString("\u2248 %1 GB").arg(mb / 1024.0, 0, 'f', 1);
    else if (mb >= 100.0)
        hint = QString("\u2248 %1 MB").arg(mb, 0, 'f', 0);
    else
        hint = QString("\u2248 %1 MB").arg(mb, 0, 'f', 1);

    m_createButton->setHintText(hint);
}

void NewProjectContent::updateThumbnail()
{
    int width = m_widthField->value();
    int height = m_heightField->value();
    m_canvasThumbnail->setDimensions(width, height);
    m_canvasThumbnail->setProjectName(projectName());
}

void NewProjectContent::syncLockedHeightFromWidth(int width)
{
    if (m_syncingLockedDimensions || !m_aspectLockButton || !m_aspectLockButton->isLocked()
        || !m_heightField)
        return;

    if (m_lockedAspectRatio <= 0.0)
        updateLockedAspectRatio();
    if (m_lockedAspectRatio <= 0.0)
        return;

    const int targetHeight = qMax(1, qRound(static_cast<qreal>(width) / m_lockedAspectRatio));
    if (targetHeight == m_heightField->value())
        return;

    m_syncingLockedDimensions = true;
    const QSignalBlocker blocker(m_heightField);
    m_heightField->setValue(targetHeight);
    m_syncingLockedDimensions = false;
}

void NewProjectContent::syncLockedWidthFromHeight(int height)
{
    if (m_syncingLockedDimensions || !m_aspectLockButton || !m_aspectLockButton->isLocked()
        || !m_widthField)
        return;

    if (m_lockedAspectRatio <= 0.0)
        updateLockedAspectRatio();
    if (m_lockedAspectRatio <= 0.0)
        return;

    const int targetWidth = qMax(1, qRound(static_cast<qreal>(height) * m_lockedAspectRatio));
    if (targetWidth == m_widthField->value())
        return;

    m_syncingLockedDimensions = true;
    const QSignalBlocker blocker(m_widthField);
    m_widthField->setValue(targetWidth);
    m_syncingLockedDimensions = false;
}

void NewProjectContent::updateLockedAspectRatio()
{
    if (!m_widthField || !m_heightField) {
        m_lockedAspectRatio = 0.0;
        return;
    }
    const int width = m_widthField->value();
    const int height = m_heightField->value();
    m_lockedAspectRatio
        = (height > 0) ? static_cast<qreal>(width) / static_cast<qreal>(height) : 0.0;
}

void NewProjectContent::clearSelectedPreset()
{
    if (m_selectedPreset.isEmpty())
        return;
    if (m_presetCards.contains(m_selectedPreset))
        m_presetCards[m_selectedPreset]->setSelected(false);
    m_selectedPreset.clear();
}

void NewProjectContent::setActivePresetCategory(int index)
{
    if (!m_presetsStack || index < 0 || index >= m_categoryButtons.size()
        || index >= m_presetsStack->count())
        return;

    m_presetsStack->setCurrentIndex(index);
    m_presetsStack->updateGeometry();
    if (QWidget* parent = m_presetsStack->parentWidget()) {
        parent->updateGeometry();
        if (parent->layout()) {
            parent->layout()->invalidate();
            parent->layout()->activate();
        }
    }
    for (int i = 0; i < m_categoryButtons.size(); ++i) {
        if (m_categoryButtons[i])
            m_categoryButtons[i]->setChecked(i == index);
    }
}

QString NewProjectContent::formatRatio(const QSize& size) const
{
    if (size.width() <= 0 || size.height() <= 0)
        return QString();
    const int divisor = std::gcd(size.width(), size.height());
    const int rw = size.width() / divisor;
    const int rh = size.height() / divisor;
    if (rw <= 32 && rh <= 32)
        return QString("%1 : %2").arg(rw).arg(rh);
    const qreal ratio = static_cast<qreal>(size.width()) / size.height();
    return ratio >= 1.0 ? QString("%1 : 1").arg(ratio, 0, 'f', 2)
                        : QString("1 : %1").arg(1.0 / ratio, 0, 'f', 2);
}

void NewProjectContent::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(layout())) {
        const int marginH = theme.scaled(BASE_MAIN_MARGIN_H);
        const int marginV = theme.scaled(BASE_MAIN_MARGIN_V);
        mainLayout->setContentsMargins(marginH, marginV, marginH, marginV);
        mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));
    }

    if (m_titleLabel) {
        m_titleLabel->setFont(
            colors.fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    }

    if (m_createButton) {
        m_createButton->setFixedHeight(theme.scaled(BASE_BUTTON_HEIGHT));
        QFont btnFont = colors.fonts.getUIFont(theme.scaledFontSize(BASE_BUTTON_FONT_SIZE));
        btnFont.setBold(true);
        m_createButton->setFont(btnFont);
        m_createButton->updateGeometry();
    }
    if (m_canvasBoundsTitleLabel) {
        QFont labelFont
            = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SETTINGS_FIELD_LABEL_FONT));
        labelFont.setWeight(QFont::Normal);
        labelFont.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.5));
        m_canvasBoundsTitleLabel->setFont(labelFont);
        if (m_bitDepthTitleLabel)
            m_bitDepthTitleLabel->setFont(labelFont);
        if (m_backgroundColorTitleLabel)
            m_backgroundColorTitleLabel->setFont(labelFont);
    } else if (m_backgroundColorTitleLabel) {
        QFont labelFont
            = colors.fonts.getUIFont(theme.scaledFontSize(BASE_SETTINGS_FIELD_LABEL_FONT));
        labelFont.setWeight(QFont::Normal);
        labelFont.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.5));
        m_backgroundColorTitleLabel->setFont(labelFont);
    }

    updateMemoryLabel();

    // Run after StyledInputField / lock button react to the same theme pass (order is undefined).
    QTimer::singleShot(0, this, [this]() { syncLockColumnLayout(); });
}

void NewProjectContent::syncLockColumnLayout()
{
    if (!m_lockColumn || !m_lockColumnTopSpacer || !m_lockColumnLayout || !m_widthField
        || !m_heightField || !m_aspectLockButton)
        return;

    const int fieldH = qMax(m_widthField->height(), m_heightField->height());
    if (fieldH <= 0)
        return;

    m_lockColumn->setFixedHeight(fieldH);

    const int yBox = qMax(m_widthField->boxedInputTopY(), m_heightField->boxedInputTopY());
    const int boxH = qMax(m_widthField->boxedInputHeight(), m_heightField->boxedInputHeight());
    const int btnH = m_aspectLockButton->height();
    const int top = yBox + qMax(0, (boxH - btnH) / 2);

    m_lockColumnTopSpacer->changeSize(0, top, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_lockColumnLayout->invalidate();
    m_lockColumnLayout->activate();
    m_lockColumn->updateGeometry();
}

void NewProjectContent::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.text.name()));
    }
    if (m_canvasBoundsTitleLabel) {
        // Match StyledInputField caption (updateThemeColors): same channel + HexArgb as muted
        // labels.
        m_canvasBoundsTitleLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }")
                .arg(colors.textMuted.name(QColor::HexArgb)));
    }
    if (m_bitDepthTitleLabel) {
        m_bitDepthTitleLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }")
                .arg(colors.textMuted.name(QColor::HexArgb)));
    }
    if (m_backgroundColorTitleLabel) {
        m_backgroundColorTitleLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }")
                .arg(colors.textMuted.name(QColor::HexArgb)));
    }
    updateMemoryLabel();
}

void NewProjectContent::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::widgets
