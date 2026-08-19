// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorThemesPreview.h"

#include "features/home/welcome/WelcomeBannerButton.h"
#include "features/layers/ui/LayerEffectsPanel.h"
#include "features/layers/ui/LayersPanel.h"
#include "features/settings/SettingsChoice.h"
#include "features/settings/SettingsComboBox.h"
#include "features/settings/SettingsToggle.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/inputs/FontDropdownSelector.h"
#include "shell/docking/widgets/DockGroupHeader.h"
#include "shell/docking/widgets/DockPanelTitleBar.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>

#include <utility>

namespace ruwa::ui::widgets {

namespace {

constexpr int kWelcomeLayoutPadding = 40;
constexpr int kWelcomeLayoutSpacing = 16;
constexpr int kWelcomeButtonSpacing = 12;
constexpr int kExampleWidth = 450;
constexpr int kExamplePopupHeight = 160;
constexpr int kExamplePopupTopClip = 23;
constexpr int kExamplePopupBottomGap = 8;
constexpr int kExampleRowSpacing = 8;
constexpr int kBottomSettingVisibleHeight = 24;

QPixmap previewThumbnail()
{
    QPixmap thumbnail(56, 40);
    thumbnail.fill(Qt::white);
    return thumbnail;
}

void makeWidgetTreePassive(QWidget* root)
{
    if (!root) {
        return;
    }

    root->setFocusPolicy(Qt::NoFocus);
    root->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    const auto children = root->findChildren<QWidget*>();
    for (QWidget* child : children) {
        child->setFocusPolicy(Qt::NoFocus);
        child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void applySettingsPresentationTheme(
    QWidget* panel, const ruwa::ui::core::ThemeColors& colors, const QPalette& palette)
{
    if (!panel) {
        return;
    }

    panel->setPalette(palette);
    QFont panelFont = panel->font();
    panelFont.setFamily(colors.fonts.uiFont);
    panel->setFont(panelFont);

    const auto children = panel->findChildren<QWidget*>();
    for (QWidget* child : children) {
        QFont childFont = child->font();
        childFont.setFamily(colors.fonts.uiFont);
        child->setFont(childFont);
    }
    const auto labels = panel->findChildren<QLabel*>();
    for (QLabel* label : labels) {
        label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name()));
    }
}

} // namespace

ThemeEditorThemesPreview::ThemeEditorThemesPreview(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_StyledBackground, false);

    m_previewColors = ruwa::ui::core::ThemeManager::instance().colors();
    setupLeftPreview();
    setupWidgetExamples();
    setupDockPreview();
    populatePreviewLayers();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ThemeEditorThemesPreview::updateTheme);

    updateTheme();
}

ThemeEditorThemesPreview::~ThemeEditorThemesPreview() = default;

void ThemeEditorThemesPreview::setPreset(const ruwa::ui::core::ThemePreset& preset)
{
    m_previewColors = ruwa::ui::core::ThemeManager::colorsForPreset(preset);
    updateTheme();
}

void ThemeEditorThemesPreview::setupLeftPreview()
{
    m_leftContent = new QWidget(this);
    m_leftContent->setAttribute(Qt::WA_TranslucentBackground);

    auto* contentLayout = new QVBoxLayout(m_leftContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(
        ruwa::ui::core::ThemeManager::instance().scaled(kWelcomeLayoutSpacing));

    m_titleLabel = new QLabel(m_leftContent);
    m_titleLabel->setAttribute(Qt::WA_TranslucentBackground);
    contentLayout->addWidget(m_titleLabel);

    m_subtitleLabel = new QLabel(m_leftContent);
    m_subtitleLabel->setAttribute(Qt::WA_TranslucentBackground);
    contentLayout->addWidget(m_subtitleLabel);
    contentLayout->addStretch();

    auto* buttonContainer = new QWidget(m_leftContent);
    buttonContainer->setAttribute(Qt::WA_TranslucentBackground);
    auto* buttonLayout = new QHBoxLayout(buttonContainer);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(
        ruwa::ui::core::ThemeManager::instance().scaled(kWelcomeButtonSpacing));

    m_primaryButton = new WelcomeBannerButton(
        QString(), WelcomeBannerButton::ButtonStyle::Primary, buttonContainer);
    m_primaryButton->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::FileNew));
    buttonLayout->addWidget(m_primaryButton);

    m_secondaryButton = new WelcomeBannerButton(
        QString(), WelcomeBannerButton::ButtonStyle::Secondary, buttonContainer);
    m_secondaryButton->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::OpenedFolder));
    m_secondaryButton->setSecondaryIdleShadowAlpha(16);
    buttonLayout->addWidget(m_secondaryButton);
    buttonLayout->addStretch();

    contentLayout->addWidget(buttonContainer);
    retranslatePreview();
    m_leftContent->show();
}

void ThemeEditorThemesPreview::setupWidgetExamples()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    m_widgetExamples = new QWidget(this);
    m_widgetExamples->setAttribute(Qt::WA_TranslucentBackground);

    auto* examplesLayout = new QVBoxLayout(m_widgetExamples);
    examplesLayout->setContentsMargins(
        0, kExamplePopupHeight - kExamplePopupTopClip + theme.scaled(kExamplePopupBottomGap), 0, 0);
    examplesLayout->setSpacing(0);

    m_fontDropdown = new FontDropdownSelector(m_widgetExamples);
    m_fontDropdown->setFontFamilies(QFontDatabase::families());
    m_fontDropdown->setPopupMinWidth(theme.scaled(kExampleWidth));
    m_fontDropdown->setPopupMaxHeight(kExamplePopupHeight);
    m_fontDropdown->hide();

    m_toggleSetting = new SettingsToggle(QString(), QString(), true, m_widgetExamples);
    examplesLayout->addWidget(m_toggleSetting);
    examplesLayout->addSpacing(theme.scaled(kExampleRowSpacing));

    m_switcherSetting
        = new SettingsChoice(QString(), QString(), { tr("Soft"), tr("Hard") }, 0, m_widgetExamples);
    examplesLayout->addWidget(m_switcherSetting);
    examplesLayout->addStretch();

    // This real settings row deliberately extends below the preview. The
    // parent widget clips both the panel and its closed dropdown at the banner edge.
    m_dropdownSetting = new SettingsComboBox(QString(), QString(),
        { tr("sRGB"), tr("Display P3"), tr("Adobe RGB") }, 0, m_widgetExamples);
    m_dropdownSetting->show();

    retranslatePreview();
    m_widgetExamples->show();
}

void ThemeEditorThemesPreview::setupDockPreview()
{
    m_layersPanel = new ruwa::ui::workspace::LayersPanel(this);
    m_layerEffectsPanel = new ruwa::ui::workspace::LayerEffectsPanel(this);

    m_layersPanel->setGrouped(true);
    m_layerEffectsPanel->setGrouped(true);
    m_layerEffectsPanel->hide();

    m_groupHeader = new ruwa::ui::docking::DockGroupHeader(this);
    m_groupHeader->setPanels({ m_layersPanel, m_layerEffectsPanel });
    m_groupHeader->setCurrentPanel(m_layersPanel);
    m_groupHeader->setCornerRadius(m_layersPanel->baseCornerRadius());

    m_groupHeader->show();
    m_layersPanel->show();
    makePreviewPassive();
}

void ThemeEditorThemesPreview::populatePreviewLayers()
{
    if (!m_layersPanel) {
        return;
    }

    auto* model = m_layersPanel->layerModel();
    auto* light = model->createLayer(tr("Light"));
    auto* color = model->createLayer(tr("Color"));
    auto* sketch = model->createLayer(tr("Sketch"));
    auto* background = model->createLayer(tr("Background"));

    if (light) {
        light->thumbnail = previewThumbnail();
        light->thumbnailDirty = false;
    }
    if (color) {
        color->thumbnail = previewThumbnail();
        color->thumbnailDirty = false;
        color->opacity = 0.72;
    }
    if (sketch) {
        sketch->thumbnail = previewThumbnail();
        sketch->thumbnailDirty = false;
        sketch->blendMode = ruwa::core::layers::BlendMode::Multiply;
    }
    if (background) {
        background->thumbnail = previewThumbnail();
        background->thumbnailDirty = false;
        background->locked = true;
    }

    if (color) {
        model->setSelectedLayer(color->id);
    }
    m_layersPanel->setCanvasSize(QSize(1280, 960));
    m_layersPanel->setDisplayFrame(QRect(0, 0, 1280, 960));
    m_layersPanel->refreshLayers();
}

void ThemeEditorThemesPreview::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslatePreview();
        if (m_groupHeader) {
            m_groupHeader->refreshContents();
        }
        m_snapshotDirty = true;
        update();
    }
}

void ThemeEditorThemesPreview::retranslatePreview()
{
    if (m_titleLabel) {
        m_titleLabel->setText(
            QCoreApplication::translate("WelcomeBanner", "Digital Painting Reimagined"));
    }
    if (m_subtitleLabel) {
        m_subtitleLabel->setText(
            QCoreApplication::translate("WelcomeBanner", "Free, open-source, and limitless."));
    }
    if (m_primaryButton) {
        m_primaryButton->setText(QCoreApplication::translate("WelcomeBanner", "Create Project"));
        m_primaryButton->syncSizeToText();
    }
    if (m_secondaryButton) {
        m_secondaryButton->setText(QCoreApplication::translate("WelcomeBanner", "Open Project"));
        m_secondaryButton->syncSizeToText();
    }
    if (m_toggleSetting) {
        m_toggleSetting->setLabel(tr("Use pen pressure"));
    }
    if (m_switcherSetting) {
        m_switcherSetting->retranslateUi(tr("Stroke mode"), QString(), { tr("Soft"), tr("Hard") });
    }
    if (m_dropdownSetting) {
        m_dropdownSetting->setLabel(tr("Color profile"));
        m_dropdownSetting->setOptions({ tr("sRGB"), tr("Display P3"), tr("Adobe RGB") });
        m_dropdownSetting->setSelectedIndex(0);
    }
}

void ThemeEditorThemesPreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const qreal radius = theme.scaled(12);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    if (m_snapshotDirty) {
        rebuildSnapshot();
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(m_previewColors.border.darker(133), 1.0));
    painter.setBrush(m_previewColors.background);
    painter.drawRoundedRect(bounds, radius, radius);
    painter.drawPixmap(0, 0, m_snapshot);
}

void ThemeEditorThemesPreview::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreviewGeometry();
}

void ThemeEditorThemesPreview::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updatePreviewGeometry();

    // DockPanel creates its real content lazily from showEvent. Once those real
    // controls exist, apply the edited preset to them and make them passive.
    QTimer::singleShot(0, this, [this]() {
        m_layersPanel->setInsertAnimationsEnabled(false);
        m_layersPanel->preparePresentationSnapshot();
        updateTheme();
        makePreviewPassive();
    });
}

void ThemeEditorThemesPreview::updatePreviewGeometry()
{
    if (!m_leftContent || !m_widgetExamples || !m_groupHeader || !m_layersPanel) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int availableWidth = qMax(1, width() - theme.scaled(48));
    const int groupWidth = qMin(qMax(theme.scaled(430), qRound(width() * 0.34)), availableWidth);
    const int clippedRight = theme.scaled(8);
    const int groupX = width() - groupWidth + clippedRight;
    const int groupY = theme.scaled(34);
    const int groupHeight = qMax(theme.scaled(520), height() * 2);
    const int headerHeight = m_groupHeader->barHeight();
    const int sourceX = width() + theme.scaled(32);
    const int leftMargin = theme.scaled(kWelcomeLayoutPadding);
    const int examplesGap = theme.scaled(28);
    const int examplesRightGap = theme.scaled(64);
    const int examplesRight = groupX - examplesRightGap;
    const int titleWidth = m_titleLabel->sizeHint().width();
    const int buttonsWidth = m_primaryButton->parentWidget()->sizeHint().width();
    const int leftContentIdealWidth = qMax(theme.scaled(360), qMax(titleWidth, buttonsWidth));
    const int examplesAvailableWidth
        = examplesRight - leftMargin - examplesGap - leftContentIdealWidth;
    const int examplesWidth
        = qMax(theme.scaled(220), qMin(theme.scaled(kExampleWidth), examplesAvailableWidth));
    const int examplesX = examplesRight - examplesWidth;
    const int leftWidth = qMax(0, examplesX - leftMargin - examplesGap);
    const int leftHeight = qMax(1, height() - leftMargin * 2);

    m_leftContentTarget = QRect(leftMargin, leftMargin, leftWidth, leftHeight);
    m_widgetExamplesTarget = QRect(examplesX, 0, examplesWidth, height());
    m_groupHeaderTarget = QRect(groupX, groupY, groupWidth, headerHeight);
    m_layersPanelTarget
        = QRect(groupX, groupY + headerHeight, groupWidth, groupHeight - headerHeight);

    // Keep the real widgets alive and visible for QWidget::render(), but park
    // them beyond the parent's clip so only their themed snapshot is displayed.
    m_groupHeader->setGeometry(sourceX, 0, groupWidth, headerHeight);
    m_layersPanel->setGeometry(sourceX, headerHeight, groupWidth, groupHeight - headerHeight);
    m_leftContent->setGeometry(sourceX, 0, qMax(1, leftWidth), leftHeight);
    m_widgetExamples->setGeometry(
        sourceX, 0, qMax(1, examplesWidth), m_widgetExamplesTarget.height());
    m_fontDropdown->setFixedWidth(qMax(1, examplesWidth));
    m_fontDropdown->setPopupMinWidth(qMax(1, examplesWidth));
    if (m_leftContent->layout()) {
        m_leftContent->layout()->activate();
    }
    if (m_widgetExamples->layout()) {
        m_widgetExamples->layout()->activate();
    }
    const int dropdownHeight = qMax(theme.scaled(52), m_dropdownSetting->sizeHint().height());
    m_dropdownSetting->setGeometry(0,
        m_widgetExamples->height() - theme.scaled(kBottomSettingVisibleHeight),
        qMax(1, examplesWidth), dropdownHeight);
    m_dropdownSetting->raise();

    m_snapshotDirty = true;
    update();
}

void ThemeEditorThemesPreview::updateTheme()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const QPalette previewPalette
        = ruwa::ui::core::ThemeManager::paletteForColors(m_previewColors, palette());
    const auto previewFont
        = [this, &theme](ruwa::ui::core::ThemeFontRole role, QFont::Weight weight = QFont::Normal) {
              QFont font = m_previewColors.fonts.getFont(
                  role, theme.scaledFontSize(m_previewColors.fonts.sizes.value(role)));
              font.setWeight(weight);
              return font;
          };

    if (m_leftContent) {
        m_leftContent->setPalette(previewPalette);
        if (m_leftContent->layout()) {
            m_leftContent->layout()->setSpacing(theme.scaled(kWelcomeLayoutSpacing));
        }
        if (QWidget* buttonContainer = m_primaryButton->parentWidget();
            buttonContainer && buttonContainer->layout()) {
            buttonContainer->layout()->setSpacing(theme.scaled(kWelcomeButtonSpacing));
        }

        QPalette titlePalette = previewPalette;
        titlePalette.setColor(QPalette::WindowText, m_previewColors.text);
        m_titleLabel->setPalette(titlePalette);
        m_titleLabel->setFont(previewFont(ruwa::ui::core::ThemeFontRole::H0, QFont::Bold));

        QPalette subtitlePalette = previewPalette;
        subtitlePalette.setColor(QPalette::WindowText, m_previewColors.textMuted);
        m_subtitleLabel->setPalette(subtitlePalette);
        m_subtitleLabel->setFont(m_previewColors.fonts.getFont(
            ruwa::ui::core::ThemeFontRole::H5, ruwa::ui::core::ThemeFontFamilyRole::Ui,
            theme.scaledFontSize(
                m_previewColors.fonts.sizes.value(ruwa::ui::core::ThemeFontRole::H5))));

        QFont buttonFont = previewFont(ruwa::ui::core::ThemeFontRole::Label, QFont::Bold);
        m_primaryButton->setFont(buttonFont);
        m_secondaryButton->setFont(buttonFont);
        m_primaryButton->syncSizeToText();
        m_secondaryButton->syncSizeToText();
    }
    if (m_widgetExamples) {
        m_widgetExamples->setPalette(previewPalette);
        const QFont controlFont = previewFont(ruwa::ui::core::ThemeFontRole::Body);
        m_fontDropdown->setFont(controlFont);
        m_fontDropdown->setCurrentFamily(m_previewColors.fonts.uiFont);
        applySettingsPresentationTheme(m_toggleSetting, m_previewColors, previewPalette);
        applySettingsPresentationTheme(m_switcherSetting, m_previewColors, previewPalette);
        applySettingsPresentationTheme(m_dropdownSetting, m_previewColors, previewPalette);
    }
    if (m_groupHeader) {
        m_groupHeader->applyTheme(m_previewColors);
    }
    if (m_layersPanel) {
        m_layersPanel->setSubtitleBackground(m_previewColors.surface);
        if (m_layersPanel->titleBar()) {
            m_layersPanel->titleBar()->applyTheme(m_previewColors);
        }

        m_layersPanel->setPalette(previewPalette);
        m_groupHeader->setPalette(previewPalette);
    }
    updatePreviewGeometry();
    m_snapshotDirty = true;
    update();
}

void ThemeEditorThemesPreview::rebuildSnapshot()
{
    m_snapshotDirty = false;
    if (size().isEmpty() || !m_leftContent || !m_widgetExamples || !m_groupHeader
        || !m_layersPanel) {
        m_snapshot = QPixmap();
        return;
    }

    const qreal dpr = devicePixelRatioF();
    QPixmap snapshot(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
    snapshot.setDevicePixelRatio(dpr);
    snapshot.fill(Qt::transparent);

    m_leftContent->ensurePolished();
    m_widgetExamples->ensurePolished();
    m_groupHeader->ensurePolished();
    m_layersPanel->ensurePolished();
    m_layersPanel->preparePresentationSnapshot();

    QPainter snapshotPainter(&snapshot);
    auto& manager = ruwa::ui::core::ThemeManager::instance();
    manager.withColorOverride(m_previewColors, [this, &snapshotPainter]() {
        if (!m_leftContentTarget.isEmpty()) {
            m_leftContent->render(&snapshotPainter, m_leftContentTarget.topLeft(),
                QRegion(m_leftContent->rect()),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
        }
        if (!m_widgetExamplesTarget.isEmpty()) {
            m_widgetExamples->render(&snapshotPainter, m_widgetExamplesTarget.topLeft(),
                QRegion(m_widgetExamples->rect()),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);

            const QSize popupSize = m_fontDropdown->preparePresentationPopup(kExamplePopupHeight);
            if (!popupSize.isEmpty()) {
                const QPoint popupTarget(m_widgetExamplesTarget.x(), -kExamplePopupTopClip);
                m_fontDropdown->renderPresentationPopup(&snapshotPainter, popupTarget);
            }
        }
        m_groupHeader->render(&snapshotPainter, m_groupHeaderTarget.topLeft(),
            QRegion(m_groupHeader->rect()), QWidget::DrawWindowBackground | QWidget::DrawChildren);
        m_layersPanel->render(&snapshotPainter, m_layersPanelTarget.topLeft(),
            QRegion(m_layersPanel->rect()), QWidget::DrawWindowBackground | QWidget::DrawChildren);
    });
    snapshotPainter.end();
    m_snapshot = std::move(snapshot);
}

void ThemeEditorThemesPreview::makePreviewPassive()
{
    makeWidgetTreePassive(this);
}

} // namespace ruwa::ui::widgets
