// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorTab.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/widgets/inputs/ColorInputButton.h"
#include "features/theme/editor/DetailedThemePreview.h"
#include "shared/widgets/BaseStyledPanel.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/PresetMenuListWidget.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/utils/FileDialogMemory.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QUuid>
#include <QTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <algorithm> // For std::find_if

namespace ruwa::ui::tabs {

using namespace ruwa::core;
using namespace ruwa::ui::core;
using namespace ruwa::ui::widgets;

// =============================================================================
// Constants
// =============================================================================

namespace {
// Layout proportions
const int LEFT_PANEL_STRETCH = 35;
const int RIGHT_PANEL_STRETCH = 65;
const int THEME_LIST_HEIGHT_PERCENT = 30;
const int COLOR_EDITOR_HEIGHT_PERCENT = 70;

// Theme list panel
const int BASE_LIST_MARGIN = 12;
const int BASE_LIST_SPACING = 8;
const int BASE_LIST_HEADER_FONT_SIZE = 13;
const int BASE_LIST_HEADER_HEIGHT = 36;

// Color editor panel
const int BASE_EDITOR_MARGIN = 12;
const int BASE_EDITOR_SPACING = 8;
const int BASE_EDITOR_HEADER_FONT_SIZE = 13;
const int BASE_CATEGORY_FONT_SIZE = 11;
const int BASE_CATEGORY_MARGIN_TOP = 12;

// Preview panel
const int BASE_PREVIEW_MARGIN = 16;
const int BASE_PREVIEW_HEADER_FONT_SIZE = 13;
const int BASE_PREVIEW_MIN_SIZE = 300;

// Buttons
const int BASE_BUTTON_HEIGHT = 28;
const int BASE_NEW_BUTTON_WIDTH = 70;

/// PresetMenuListWidget::extraActionTriggered — star toggle on theme rows
constexpr int kThemeListFavoriteActionId = 1;
constexpr int kThemeListNewActionId = 2;

PresetMenuExtraAction makeThemeFavoriteRowAction(bool isFavorite)
{
    PresetMenuExtraAction a;
    a.id = kThemeListFavoriteActionId;
    a.useStarToggle = true;
    a.checked = isFavorite;
    return a;
}

bool confirmThemeAction(QWidget* context, const QString& title, const QString& message)
{
    const QString prompt = title.isEmpty() ? message : QStringLiteral("%1\n%2").arg(title, message);
    return ruwa::ui::widgets::MessagePopupManager::showBlocking(
        context, prompt, QObject::tr("Yes"), QObject::tr("No"), 360, true);
}

void showThemeInfo(QWidget* context, const QString& title, const QString& message)
{
    const QString text = title.isEmpty() ? message : QStringLiteral("%1\n%2").arg(title, message);
    ruwa::ui::widgets::MessagePopupManager::show(
        context, text, { { QObject::tr("OK"), true, []() { } } }, 360);
}

void showThemeCopySuggestion(QWidget* context, const QString& title, const QString& message,
    const std::function<void()>& createThemeCallback)
{
    const QString text = title.isEmpty() ? message : QStringLiteral("%1\n%2").arg(title, message);
    ruwa::ui::widgets::MessagePopupManager::show(context, text,
        { { QObject::tr("OK"), false, []() { } },
            { QObject::tr("New theme"), true, createThemeCallback } },
        360);
}

static QJsonObject themePresetToJsonObject(const ThemePreset& p)
{
    QJsonObject o;
    o["id"] = p.id.toString(QUuid::WithoutBraces);
    o["name"] = p.name;
    o["description"] = p.description;
    o["isDark"] = p.isDark;
    o["isFavorite"] = p.isFavorite;

    o["primary"] = p.primary.name(QColor::HexArgb);
    o["background"] = p.background.name(QColor::HexArgb);
    o["surface"] = p.surface.name(QColor::HexArgb);
    o["surfaceAlt"] = p.surfaceAlt.name(QColor::HexArgb);
    o["border"] = p.border.name(QColor::HexArgb);
    o["accent"] = p.accent.name(QColor::HexArgb);
    o["text"] = p.text.name(QColor::HexArgb);
    o["textMuted"] = p.textMuted.name(QColor::HexArgb);
    o["textOnPrimary"] = p.textOnPrimary.name(QColor::HexArgb);
    o["overlayColor"] = p.overlayColor.name(QColor::HexArgb);
    o["success"] = p.success.name(QColor::HexArgb);
    o["warning"] = p.warning.name(QColor::HexArgb);
    o["error"] = p.error.name(QColor::HexArgb);
    o["info"] = p.info.name(QColor::HexArgb);

    QJsonObject f;
    f["ui"] = p.fonts.uiFont;
    f["code"] = p.fonts.codeFont;
    f["title"] = p.fonts.titleFont;
    f["uiSize"] = p.fonts.uiSize;
    f["codeSize"] = p.fonts.codeSize;
    f["titleSize"] = p.fonts.titleSize;
    o["fonts"] = f;

    return o;
}

static bool jsonToThemePreset(const QJsonObject& root, ThemePreset& out, QString* errorOut)
{
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("ruwa-theme-preset")) {
        *errorOut = QObject::tr("Invalid file format.");
        return false;
    }

    const QJsonObject p = root.value(QStringLiteral("preset")).toObject();
    if (p.isEmpty()) {
        *errorOut = QObject::tr("Missing preset data.");
        return false;
    }

    const QString name = p.value(QStringLiteral("name")).toString().trimmed();
    if (name.isEmpty()) {
        *errorOut = QObject::tr("Preset has no name.");
        return false;
    }

    out = ThemePreset {};
    out.id = QUuid::createUuid();
    out.name = name;
    out.description = p.value(QStringLiteral("description")).toString();
    out.isBuiltIn = false;
    out.isDark = p.value(QStringLiteral("isDark")).toBool(true);
    out.isFavorite = p.value(QStringLiteral("isFavorite")).toBool(false);

    out.primary = QColor(p.value(QStringLiteral("primary")).toString());
    out.background = QColor(p.value(QStringLiteral("background")).toString());
    out.surface = QColor(p.value(QStringLiteral("surface")).toString());
    out.surfaceAlt = QColor(p.value(QStringLiteral("surfaceAlt")).toString());
    out.border = QColor(p.value(QStringLiteral("border")).toString());
    out.accent = QColor(p.value(QStringLiteral("accent")).toString());
    if (!out.accent.isValid()) {
        out.accent = QColor(124, 92, 252);
    }
    out.text = QColor(p.value(QStringLiteral("text")).toString());
    out.textMuted = QColor(p.value(QStringLiteral("textMuted")).toString());
    out.textOnPrimary = QColor(p.value(QStringLiteral("textOnPrimary")).toString());
    out.overlayColor = QColor(p.value(QStringLiteral("overlayColor")).toString());
    out.success = QColor(p.value(QStringLiteral("success")).toString());
    out.warning = QColor(p.value(QStringLiteral("warning")).toString());
    out.error = QColor(p.value(QStringLiteral("error")).toString());
    out.info = QColor(p.value(QStringLiteral("info")).toString());

    const QJsonObject f = p.value(QStringLiteral("fonts")).toObject();
    if (!f.isEmpty()) {
        out.fonts.uiFont = f.value(QStringLiteral("ui")).toString(out.fonts.uiFont);
        out.fonts.codeFont = f.value(QStringLiteral("code")).toString(out.fonts.codeFont);
        out.fonts.titleFont = f.value(QStringLiteral("title")).toString(out.fonts.titleFont);
        out.fonts.uiSize = f.value(QStringLiteral("uiSize")).toInt(out.fonts.uiSize);
        out.fonts.codeSize = f.value(QStringLiteral("codeSize")).toInt(out.fonts.codeSize);
        out.fonts.titleSize = f.value(QStringLiteral("titleSize")).toInt(out.fonts.titleSize);
    }
    out.fonts.uiFont = FontFamilyNames::migrateLegacyFamilyName(out.fonts.uiFont);
    out.fonts.codeFont = FontFamilyNames::migrateLegacyFamilyName(out.fonts.codeFont);
    out.fonts.titleFont = FontFamilyNames::migrateLegacyFamilyName(out.fonts.titleFont);

    return true;
}
} // namespace

// =============================================================================
// Construction
// =============================================================================

ThemeEditorTab::ThemeEditorTab(QWidget* parent)
    : BaseTab(parent)
{
}

ThemeEditorTab::ThemeEditorTab(const QUuid& id, QWidget* parent)
    : BaseTab(id, parent)
{
}

ThemeEditorTab::~ThemeEditorTab() = default;

void ThemeEditorTab::onApplyThemeRefresh(std::function<void()> finished, bool showLoading)
{
    Q_UNUSED(showLoading);
    // Full rebuild: a fresh instance re-runs every sub-widget constructor against
    // the new theme (repolish alone can't fix widgets that cache colours or set
    // explicit palettes). A fresh ThemeEditorTab re-selects the current applied
    // theme in onInitialize(), so no extra state needs carrying over.
    const QUuid keepId = id();
    QWidget* parentWidgetPtr = parentWidget();
    recreateForThemeRefresh(
        [keepId, parentWidgetPtr]() -> ruwa::core::BaseTab* {
            return new ThemeEditorTab(keepId, parentWidgetPtr);
        },
        std::move(finished));
}

void ThemeEditorTab::resizeEvent(QResizeEvent* event)
{
    ruwa::core::BaseTab::resizeEvent(event);

    if (!layout()) {
        return;
    }

    const float targetAspectRatio = 16.0f / 9.0f;
    QSize currentSize = size();

    // Вычисляем целевую ширину при текущей высоте для пропорции 16:9
    int targetWidth = static_cast<int>(currentSize.height() * targetAspectRatio);

    int sideMargin = 0;

    // Если окно шире целевого (например, UltraWide) — добавляем отступы слева и справа
    if (currentSize.width() > targetWidth) {
        sideMargin = (currentSize.width() - targetWidth) / 2;
    }

    layout()->setContentsMargins(sideMargin, 0, sideMargin, 0);
}

void ThemeEditorTab::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    // Область отступов 16:9 заливаем цветом background
    QPainter painter(this);
    painter.fillRect(rect(), ThemeManager::instance().colors().background);
}

void ThemeEditorTab::onInitialize()
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    createLayout();
    loadThemesList();

    // Select and mark current theme as active
    QUuid currentId = ThemeManager::instance().currentPresetId();
    selectTheme(currentId);

    if (m_themePresetList) {
        m_themePresetList->setActiveUserData(currentId.toString());
    }

    // Apply initial scaling
    updateScaledSizes();
    updateThemeColors();

    // Connect to theme changes
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &ThemeEditorTab::onThemeChanged);

    // Connect to language changes
    connect(&TranslationManager::instance(), &TranslationManager::languageChanged, this,
        &ThemeEditorTab::retranslateUi);

    // Force geometry update after creation
    QTimer::singleShot(0, this, [this]() {
        updateGeometry();
        if (layout()) {
            layout()->invalidate();
            layout()->activate();
        }
    });
}

// =============================================================================
// Layout Creation
// =============================================================================

void ThemeEditorTab::createLayout()
{
    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // === LEFT PANEL (Theme list + Color editor) ===
    QWidget* leftPanel = new QWidget(this);
    leftPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    QVBoxLayout* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // Theme list container (top) - 30%
    m_sidebarContainer = new QWidget(leftPanel);
    m_sidebarContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setupThemeListPanel(m_sidebarContainer);
    leftLayout->addWidget(m_sidebarContainer, THEME_LIST_HEIGHT_PERCENT);

    // Color editor container (bottom) - 70%
    m_propertiesContainer = new QWidget(leftPanel);
    m_propertiesContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setupColorEditorPanel(m_propertiesContainer);
    leftLayout->addWidget(m_propertiesContainer, COLOR_EDITOR_HEIGHT_PERCENT);

    mainLayout->addWidget(leftPanel, LEFT_PANEL_STRETCH);

    // === RIGHT PANEL (Preview) ===
    m_previewContainer = new QWidget(this);
    m_previewContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setupPreviewPanel(m_previewContainer);
    mainLayout->addWidget(m_previewContainer, RIGHT_PANEL_STRETCH);
}

void ThemeEditorTab::setupThemeListPanel(QWidget* container)
{
    auto* layout = new QVBoxLayout(container);

    m_themePresetList = new PresetMenuListWidget(container);
    m_themePresetList->setPopupStyle(false);
    m_themePresetList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_themePresetList->setTitleText(tr("Presets"));
    m_themePresetList->setSearchPlaceholderText(tr("Search themes"));
    m_themePresetList->setEmptyStateTexts(
        tr("No themes found"), tr("Try another search or create a new theme."));

    PresetMenuHeaderAction newThemeAction;
    newThemeAction.id = kThemeListNewActionId;
    newThemeAction.icon = IconProvider::StandardIcon::FileNew;
    newThemeAction.text = QStringLiteral("+");
    newThemeAction.toolTip = tr("New theme");
    newThemeAction.accent = true;
    m_themePresetList->setHeaderActions({ newThemeAction });

    connect(m_themePresetList, &PresetMenuListWidget::itemClicked, this,
        [this](const QVariant& data) { selectTheme(QUuid(data.toString())); });

    connect(m_themePresetList, &PresetMenuListWidget::deleteRequested, this,
        [this](const QVariant& data) {
            QUuid idToDelete = QUuid(data.toString());

            auto presets = ThemeManager::instance().allPresets();
            auto it = std::find_if(presets.begin(), presets.end(),
                [&](const ThemePreset& p) { return p.id == idToDelete; });

            if (it != presets.end() && !it->isBuiltIn) {
                if (confirmThemeAction(this, tr("Delete Theme"),
                        tr("Are you sure you want to delete '%1'?").arg(it->name))) {
                    ThemeManager::instance().removeCustomPreset(idToDelete);
                    loadThemesList();

                    auto remainingPresets = ThemeManager::instance().allPresets();
                    if (!remainingPresets.isEmpty()) {
                        selectTheme(remainingPresets.first().id);
                    }
                }
            }
        });

    connect(m_themePresetList, &PresetMenuListWidget::itemRenamed, this,
        [this](const QVariant& data, const QString& newText) {
            QUuid themeId = QUuid(data.toString());

            auto presets = ThemeManager::instance().allPresets();
            auto it = std::find_if(presets.begin(), presets.end(),
                [&](const ThemePreset& p) { return p.id == themeId; });

            if (it != presets.end() && !it->isBuiltIn) {
                ThemePreset updatedTheme = *it;
                updatedTheme.name = newText;

                ThemeManager::instance().updateCustomPreset(updatedTheme);

                if (m_currentEditingTheme.id == themeId) {
                    m_currentEditingTheme.name = newText;
                }

                loadThemesList();
                selectTheme(themeId);
            }
        });

    connect(m_themePresetList, &PresetMenuListWidget::extraActionTriggered, this,
        [this](const QVariant& data, int actionId) {
            if (actionId != kThemeListFavoriteActionId) {
                return;
            }
            const QUuid themeId = QUuid(data.toString());
            auto presets = ThemeManager::instance().allPresets();
            auto it = std::find_if(presets.begin(), presets.end(),
                [&](const ThemePreset& p) { return p.id == themeId; });
            if (it == presets.end()) {
                return;
            }

            ThemePreset updatedTheme = *it;
            updatedTheme.isFavorite = !updatedTheme.isFavorite;

            if (updatedTheme.isBuiltIn) {
                ThemeManager::instance().updatePresetFavorite(themeId, updatedTheme.isFavorite);
            } else {
                ThemeManager::instance().updateCustomPreset(updatedTheme);
            }

            if (m_currentEditingTheme.id == themeId) {
                m_currentEditingTheme.isFavorite = updatedTheme.isFavorite;
            }

            if (m_themePresetList) {
                m_themePresetList->setExtraActionsForItem(
                    themeId.toString(), { makeThemeFavoriteRowAction(updatedTheme.isFavorite) });
            }
        });

    connect(m_themePresetList, &PresetMenuListWidget::importClicked, this,
        &ThemeEditorTab::importThemePreset);
    connect(m_themePresetList, &PresetMenuListWidget::exportClicked, this,
        &ThemeEditorTab::exportCurrentThemePreset);
    connect(m_themePresetList, &PresetMenuListWidget::headerActionTriggered, this,
        [this](int actionId) {
            if (actionId == kThemeListNewActionId) {
                createNewTheme();
            }
        });

    layout->addWidget(m_themePresetList);
}

void ThemeEditorTab::setupColorEditorPanel(QWidget* container)
{
    auto* mainLayout = new QVBoxLayout(container);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header with title and Save button
    QWidget* headerWidget = new QWidget(container);
    auto* headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_propertiesHeader = new QLabel(tr("Colors"), headerWidget);
    QFont titleFont = m_propertiesHeader->font();
    titleFont.setBold(true);
    m_propertiesHeader->setFont(titleFont);
    headerLayout->addWidget(m_propertiesHeader);
    headerLayout->addStretch();

    m_saveBtn
        = new CapsuleButton(tr("Save && Apply"), CapsuleButton::Variant::Primary, headerWidget);
    m_saveBtn->setBaseMinimumWidth(132);
    m_saveBtn->setBannerBaseHeight(34);
    m_saveBtn->setSizeScale(0.82);
    m_saveBtn->setText(tr("Save && Apply"));
    connect(m_saveBtn, &QPushButton::clicked, this, &ThemeEditorTab::saveCurrentTheme);
    headerLayout->addWidget(m_saveBtn);

    mainLayout->addWidget(headerWidget);

    // Scroll area with color fields
    m_scrollArea = new SmoothScrollArea(container);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_scrollContent = new QWidget();
    m_scrollContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* scrollLayout = new QVBoxLayout(m_scrollContent);
    scrollLayout->setAlignment(Qt::AlignTop);

    createColorFields();

    m_scrollArea->setWidget(m_scrollContent);
    mainLayout->addWidget(m_scrollArea);
}

void ThemeEditorTab::setupPreviewPanel(QWidget* container)
{
    auto* layout = new QVBoxLayout(container);

    // Header
    m_previewHeader = new QLabel(tr("Preview"), container);
    QFont font = m_previewHeader->font();
    font.setBold(true);
    m_previewHeader->setFont(font);
    layout->addWidget(m_previewHeader);

    // Preview widget
    m_preview = new DetailedThemePreview(container);
    m_preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_preview);
}

// =============================================================================
// Color Fields
// =============================================================================

void ThemeEditorTab::createColorFields()
{
    auto* layout = qobject_cast<QVBoxLayout*>(m_scrollContent->layout());
    if (!layout)
        return;

    const auto& theme = ThemeManager::instance();

    // === Core Colors ===
    auto* coreLabel = new QLabel(tr("Core Colors"), m_scrollContent);
    m_categoryLabels.append(coreLabel);
    layout->addWidget(coreLabel);

    auto* corePanel = new BaseStyledPanel("Panel", m_scrollContent);
    corePanel->setHoverEnabled(false); // Static panel, no hover
    corePanel->style().metrics.fixedHeight = false; // Allow auto height based on content
    corePanel->setMinimumHeight(0);
    corePanel->setMaximumHeight(QWIDGETSIZE_MAX); // Reset fixed height
    auto* coreLayout = new QVBoxLayout(corePanel);
    coreLayout->setContentsMargins(theme.scaled(BASE_EDITOR_MARGIN),
        theme.scaled(BASE_EDITOR_MARGIN), theme.scaled(BASE_EDITOR_MARGIN),
        theme.scaled(BASE_EDITOR_MARGIN));
    coreLayout->setSpacing(theme.scaled(BASE_EDITOR_SPACING));

    m_colorInputs[static_cast<size_t>(ColorField::Primary)]
        = createColorField(ColorField::Primary, tr("Primary / Accent"), corePanel);
    m_colorInputs[static_cast<size_t>(ColorField::Background)]
        = createColorField(ColorField::Background, tr("Background"), corePanel);
    m_colorInputs[static_cast<size_t>(ColorField::Surface)]
        = createColorField(ColorField::Surface, tr("Surface"), corePanel);
    m_colorInputs[static_cast<size_t>(ColorField::SurfaceAlt)]
        = createColorField(ColorField::SurfaceAlt, tr("Surface Alt"), corePanel);
    m_colorInputs[static_cast<size_t>(ColorField::Border)]
        = createColorField(ColorField::Border, tr("Border"), corePanel);

    coreLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Primary)]);
    coreLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Background)]);
    coreLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Surface)]);
    coreLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::SurfaceAlt)]);
    coreLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Border)]);

    layout->addWidget(corePanel);

    // === Text Colors ===
    auto* textLabel = new QLabel(tr("Text Colors"), m_scrollContent);
    m_categoryLabels.append(textLabel);
    layout->addWidget(textLabel);

    auto* textPanel = new BaseStyledPanel("Panel", m_scrollContent);
    textPanel->setHoverEnabled(false); // Static panel, no hover
    textPanel->style().metrics.fixedHeight = false; // Allow auto height based on content
    textPanel->setMinimumHeight(0);
    textPanel->setMaximumHeight(QWIDGETSIZE_MAX); // Reset fixed height
    auto* textLayout = new QVBoxLayout(textPanel);
    textLayout->setContentsMargins(theme.scaled(BASE_EDITOR_MARGIN),
        theme.scaled(BASE_EDITOR_MARGIN), theme.scaled(BASE_EDITOR_MARGIN),
        theme.scaled(BASE_EDITOR_MARGIN));
    textLayout->setSpacing(theme.scaled(BASE_EDITOR_SPACING));

    m_colorInputs[static_cast<size_t>(ColorField::Text)]
        = createColorField(ColorField::Text, tr("Text Main"), textPanel);
    m_colorInputs[static_cast<size_t>(ColorField::TextMuted)]
        = createColorField(ColorField::TextMuted, tr("Text Muted"), textPanel);
    m_colorInputs[static_cast<size_t>(ColorField::TextOnPrimary)]
        = createColorField(ColorField::TextOnPrimary, tr("Text on Primary"), textPanel);

    textLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Text)]);
    textLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::TextMuted)]);
    textLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::TextOnPrimary)]);

    layout->addWidget(textPanel);

    // === Semantic Colors ===
    auto* semanticLabel = new QLabel(tr("Semantic Colors"), m_scrollContent);
    m_categoryLabels.append(semanticLabel);
    layout->addWidget(semanticLabel);

    auto* semanticPanel = new BaseStyledPanel("Panel", m_scrollContent);
    semanticPanel->setHoverEnabled(false); // Static panel, no hover
    semanticPanel->style().metrics.fixedHeight = false; // Allow auto height based on content
    semanticPanel->setMinimumHeight(0);
    semanticPanel->setMaximumHeight(QWIDGETSIZE_MAX); // Reset fixed height
    auto* semanticLayout = new QVBoxLayout(semanticPanel);
    semanticLayout->setContentsMargins(theme.scaled(BASE_EDITOR_MARGIN),
        theme.scaled(BASE_EDITOR_MARGIN), theme.scaled(BASE_EDITOR_MARGIN),
        theme.scaled(BASE_EDITOR_MARGIN));
    semanticLayout->setSpacing(theme.scaled(BASE_EDITOR_SPACING));

    m_colorInputs[static_cast<size_t>(ColorField::Success)]
        = createColorField(ColorField::Success, tr("Success"), semanticPanel);
    m_colorInputs[static_cast<size_t>(ColorField::Warning)]
        = createColorField(ColorField::Warning, tr("Warning"), semanticPanel);
    m_colorInputs[static_cast<size_t>(ColorField::Error)]
        = createColorField(ColorField::Error, tr("Error"), semanticPanel);
    m_colorInputs[static_cast<size_t>(ColorField::Info)]
        = createColorField(ColorField::Info, tr("Info"), semanticPanel);

    semanticLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Success)]);
    semanticLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Warning)]);
    semanticLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Error)]);
    semanticLayout->addWidget(m_colorInputs[static_cast<size_t>(ColorField::Info)]);

    layout->addWidget(semanticPanel);

    layout->addStretch();
}

ColorInputButton* ThemeEditorTab::createColorField(
    ColorField field, const QString& label, QWidget* parent)
{
    auto* btn = new ColorInputButton(label, Qt::black, parent);

    connect(btn, &ColorInputButton::colorChanged, this,
        [this, field](const QColor& color) { onColorFieldChanged(field, color); });

    // ⭐ ВАЖНО: проверяем можно ли редактировать перед открытием пикера
    connect(btn, &ColorInputButton::colorPickerRequested, this, [this, btn](const QColor& color) {
        // Если это built-in тема - показываем ошибку
        if (m_currentEditingTheme.isBuiltIn) {
            showThemeCopySuggestion(this, tr("Edit Theme"),
                tr("Cannot edit built-in themes. Create a copy to customize colors."),
                [this]() { createNewTheme(); });
            return;
        }

        emit colorPickerRequested(color, btn);
    });

    return btn;
}

void ThemeEditorTab::updateColorFieldsFromTheme()
{
    m_isInternalChange = true;

    for (size_t i = 0; i < ColorFieldCount; ++i) {
        if (m_colorInputs[i]) {
            ColorField field = static_cast<ColorField>(i);
            m_colorInputs[i]->setColor(getThemeColorRef(field));
        }
    }

    m_isInternalChange = false;
}

void ThemeEditorTab::onColorFieldChanged(ColorField field, const QColor& color)
{
    if (m_isInternalChange)
        return;

    getThemeColorRef(field) = color;

    if (m_preview) {
        m_preview->setPreviewTheme(m_currentEditingTheme);
    }

    // Mark as modified (requires Apply to save/apply)
    setModified(true);
}

QColor& ThemeEditorTab::getThemeColorRef(ColorField field)
{
    switch (field) {
    case ColorField::Primary:
        return m_currentEditingTheme.primary;
    case ColorField::Background:
        return m_currentEditingTheme.background;
    case ColorField::Surface:
        return m_currentEditingTheme.surface;
    case ColorField::SurfaceAlt:
        return m_currentEditingTheme.surfaceAlt;
    case ColorField::Border:
        return m_currentEditingTheme.border;
    case ColorField::Text:
        return m_currentEditingTheme.text;
    case ColorField::TextMuted:
        return m_currentEditingTheme.textMuted;
    case ColorField::TextOnPrimary:
        return m_currentEditingTheme.textOnPrimary;
    case ColorField::Success:
        return m_currentEditingTheme.success;
    case ColorField::Warning:
        return m_currentEditingTheme.warning;
    case ColorField::Error:
        return m_currentEditingTheme.error;
    case ColorField::Info:
        return m_currentEditingTheme.info;
    default:
        return m_currentEditingTheme.primary; // Fallback
    }
}

// =============================================================================
// Scaling & Theming
// =============================================================================

void ThemeEditorTab::updateScaledSizes()
{
    const auto& theme = ThemeManager::instance();

    // Theme list panel
    if (m_sidebarContainer && m_sidebarContainer->layout()) {
        m_sidebarContainer->layout()->setContentsMargins(theme.scaled(BASE_LIST_MARGIN),
            theme.scaled(BASE_LIST_MARGIN), theme.scaled(BASE_LIST_MARGIN),
            theme.scaled(BASE_LIST_MARGIN));
        m_sidebarContainer->layout()->setSpacing(theme.scaled(BASE_LIST_SPACING));
    }

    if (m_sidebarHeader) {
        QFont font = m_sidebarHeader->font();
        font.setPointSize(theme.scaledFontSize(BASE_LIST_HEADER_FONT_SIZE));
        m_sidebarHeader->setFont(font);
    }

    // Color editor panel
    if (m_propertiesContainer && m_propertiesContainer->layout()) {
        m_propertiesContainer->layout()->setContentsMargins(theme.scaled(BASE_EDITOR_MARGIN),
            theme.scaled(BASE_EDITOR_MARGIN), theme.scaled(BASE_EDITOR_MARGIN),
            theme.scaled(BASE_EDITOR_MARGIN));
        m_propertiesContainer->layout()->setSpacing(theme.scaled(BASE_EDITOR_SPACING));
    }

    if (m_propertiesHeader) {
        QFont font = m_propertiesHeader->font();
        font.setPointSize(theme.scaledFontSize(BASE_EDITOR_HEADER_FONT_SIZE));
        m_propertiesHeader->setFont(font);
    }

    if (m_scrollContent && m_scrollContent->layout()) {
        m_scrollContent->layout()->setContentsMargins(theme.scaled(BASE_EDITOR_MARGIN),
            theme.scaled(BASE_EDITOR_MARGIN), theme.scaled(BASE_EDITOR_MARGIN),
            theme.scaled(BASE_EDITOR_MARGIN));
        m_scrollContent->layout()->setSpacing(theme.scaled(BASE_EDITOR_SPACING));
    }

    // Category labels
    for (QLabel* label : m_categoryLabels) {
        if (label) {
            QFont font = label->font();
            font.setPointSize(theme.scaledFontSize(BASE_CATEGORY_FONT_SIZE));
            font.setBold(true);
            label->setFont(font);
        }
    }

    // Preview panel
    if (m_previewContainer && m_previewContainer->layout()) {
        m_previewContainer->layout()->setContentsMargins(theme.scaled(BASE_PREVIEW_MARGIN),
            theme.scaled(BASE_PREVIEW_MARGIN), theme.scaled(BASE_PREVIEW_MARGIN),
            theme.scaled(BASE_PREVIEW_MARGIN));
        m_previewContainer->layout()->setSpacing(theme.scaled(BASE_EDITOR_SPACING));
    }

    if (m_previewHeader) {
        QFont font = m_previewHeader->font();
        font.setPointSize(theme.scaledFontSize(BASE_PREVIEW_HEADER_FONT_SIZE));
        m_previewHeader->setFont(font);
    }

    if (m_preview) {
        m_preview->setMinimumSize(
            theme.scaled(BASE_PREVIEW_MIN_SIZE), theme.scaled(BASE_PREVIEW_MIN_SIZE));
    }
}

void ThemeEditorTab::updateThemeColors()
{
    const auto& colors = ThemeManager::instance().colors();

    // Update labels
    QString labelStyle = QString("QLabel { color: %1; }").arg(colors.text.name());

    if (m_sidebarHeader) {
        m_sidebarHeader->setStyleSheet(labelStyle);
    }
    if (m_propertiesHeader) {
        m_propertiesHeader->setStyleSheet(labelStyle);
    }
    if (m_previewHeader) {
        m_previewHeader->setStyleSheet(labelStyle);
    }

    for (QLabel* label : m_categoryLabels) {
        if (label) {
            label->setStyleSheet(labelStyle);
        }
    }

    // Theme editor tab should use background everywhere.
    auto applyBackground = [&colors](QWidget* widget) {
        if (!widget)
            return;
        widget->setAutoFillBackground(true);
        QPalette pal = widget->palette();
        pal.setColor(QPalette::Window, colors.background);
        widget->setPalette(pal);
    };

    applyBackground(this);
    applyBackground(m_sidebarContainer);
    applyBackground(m_propertiesContainer);
    applyBackground(m_previewContainer);
}

void ThemeEditorTab::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

// =============================================================================
// Theme Management
// =============================================================================

void ThemeEditorTab::changeEvent(QEvent* event)
{
    BaseTab::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void ThemeEditorTab::retranslateUi()
{
    if (m_propertiesHeader)
        m_propertiesHeader->setText(tr("Colors"));
    if (m_saveBtn) {
        m_saveBtn->setText(tr("Save && Apply"));
        m_saveBtn->syncSizeToText();
    }
    if (m_previewHeader)
        m_previewHeader->setText(tr("Preview"));
    if (m_themePresetList) {
        m_themePresetList->setTitleText(tr("Presets"));
        m_themePresetList->setSearchPlaceholderText(tr("Search themes"));
        m_themePresetList->setEmptyStateTexts(
            tr("No themes found"), tr("Try another search or create a new theme."));

        PresetMenuHeaderAction newThemeAction;
        newThemeAction.id = kThemeListNewActionId;
        newThemeAction.icon = IconProvider::StandardIcon::FileNew;
        newThemeAction.text = QStringLiteral("+");
        newThemeAction.toolTip = tr("New theme");
        newThemeAction.accent = true;
        m_themePresetList->setHeaderActions({ newThemeAction });
    }

    // Category labels: Core Colors, Text Colors, Semantic Colors
    if (m_categoryLabels.size() >= 3) {
        m_categoryLabels[0]->setText(tr("Core Colors"));
        m_categoryLabels[1]->setText(tr("Text Colors"));
        m_categoryLabels[2]->setText(tr("Semantic Colors"));
    }

    // Color field labels
    if (m_colorInputs[static_cast<size_t>(ColorField::Primary)])
        m_colorInputs[static_cast<size_t>(ColorField::Primary)]->setLabel(tr("Primary / Accent"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Background)])
        m_colorInputs[static_cast<size_t>(ColorField::Background)]->setLabel(tr("Background"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Surface)])
        m_colorInputs[static_cast<size_t>(ColorField::Surface)]->setLabel(tr("Surface"));
    if (m_colorInputs[static_cast<size_t>(ColorField::SurfaceAlt)])
        m_colorInputs[static_cast<size_t>(ColorField::SurfaceAlt)]->setLabel(tr("Surface Alt"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Border)])
        m_colorInputs[static_cast<size_t>(ColorField::Border)]->setLabel(tr("Border"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Text)])
        m_colorInputs[static_cast<size_t>(ColorField::Text)]->setLabel(tr("Text Main"));
    if (m_colorInputs[static_cast<size_t>(ColorField::TextMuted)])
        m_colorInputs[static_cast<size_t>(ColorField::TextMuted)]->setLabel(tr("Text Muted"));
    if (m_colorInputs[static_cast<size_t>(ColorField::TextOnPrimary)])
        m_colorInputs[static_cast<size_t>(ColorField::TextOnPrimary)]->setLabel(
            tr("Text on Primary"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Success)])
        m_colorInputs[static_cast<size_t>(ColorField::Success)]->setLabel(tr("Success"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Warning)])
        m_colorInputs[static_cast<size_t>(ColorField::Warning)]->setLabel(tr("Warning"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Error)])
        m_colorInputs[static_cast<size_t>(ColorField::Error)]->setLabel(tr("Error"));
    if (m_colorInputs[static_cast<size_t>(ColorField::Info)])
        m_colorInputs[static_cast<size_t>(ColorField::Info)]->setLabel(tr("Info"));

    loadThemesList();

    if (m_currentEditingTheme.id.isNull() == false) {
        selectTheme(m_currentEditingTheme.id);
    }
}

void ThemeEditorTab::loadThemesList()
{
    if (!m_themePresetList) {
        return;
    }

    const auto presets = ThemeManager::instance().allPresets();
    const QUuid currentAppliedId = ThemeManager::instance().currentPresetId();

    QVector<PresetMenuItem> items;
    items.reserve(presets.size());

    for (const auto& preset : presets) {
        PresetMenuItem it;
        it.title = ThemePreset::translatedDisplayName(preset);
        it.subtitle = preset.description;
        it.badgeText = preset.isBuiltIn ? tr("Built-in") : tr("Custom");
        it.badgeTint = preset.isBuiltIn ? QColor() : preset.primary;
        it.previewColors = { preset.background, preset.surface, preset.primary, preset.border };
        it.previewIcon = IconProvider::StandardIcon::Appearance;
        it.searchText = QStringLiteral("%1 %2 %3").arg(it.title, it.subtitle, it.badgeText);
        it.userData = preset.id.toString();
        it.deletable = !preset.isBuiltIn;
        it.renamable = !preset.isBuiltIn;

        PresetMenuExtraAction favoriteAction;
        favoriteAction.id = kThemeListFavoriteActionId;
        favoriteAction.useStarToggle = true;
        favoriteAction.checked = preset.isFavorite;
        it.extraActions.append(favoriteAction);

        items.append(it);
    }

    m_themePresetList->setItems(items);
    m_themePresetList->setActiveUserData(currentAppliedId.toString());
}

void ThemeEditorTab::selectTheme(const QUuid& id)
{
    auto presets = ThemeManager::instance().allPresets();

    auto it = std::find_if(
        presets.begin(), presets.end(), [&](const ThemePreset& p) { return p.id == id; });

    if (it != presets.end()) {
        m_currentEditingTheme = *it;

        if (m_themePresetList) {
            m_themePresetList->setSelectedUserData(id.toString());
        }

        updateColorFieldsFromTheme();

        if (m_preview) {
            m_preview->setPreviewTheme(m_currentEditingTheme);
        }
    }
}

void ThemeEditorTab::createNewTheme()
{
    ThemePreset newTheme = m_currentEditingTheme;
    newTheme.id = QUuid::createUuid();
    newTheme.name = m_currentEditingTheme.name + tr(" (Copy)");
    newTheme.isBuiltIn = false;

    ThemeManager::instance().addCustomPreset(newTheme);
    loadThemesList();
    selectTheme(newTheme.id);
}

void ThemeEditorTab::saveCurrentTheme()
{
    // For built-in themes: just apply
    if (m_currentEditingTheme.isBuiltIn) {
        ThemeManager::instance().applyPreset(m_currentEditingTheme.id);

        if (m_themePresetList) {
            m_themePresetList->setActiveUserData(m_currentEditingTheme.id.toString());
        }

        // Notify ThemeSelectorWidget
        emit themeApplied(m_currentEditingTheme.id);

        setModified(false);
        return;
    }

    // For custom themes: save AND apply
    const bool updatesActiveTheme
        = ThemeManager::instance().currentPresetId() == m_currentEditingTheme.id;
    ThemeManager::instance().updateCustomPreset(m_currentEditingTheme);
    if (!updatesActiveTheme) {
        ThemeManager::instance().applyPreset(m_currentEditingTheme.id);
    }

    if (m_themePresetList) {
        m_themePresetList->setActiveUserData(m_currentEditingTheme.id.toString());
    }

    // Notify ThemeSelectorWidget
    emit themeApplied(m_currentEditingTheme.id);

    loadThemesList();
    setModified(false);
}

void ThemeEditorTab::exportCurrentThemePreset()
{
    const QString path = ruwa::shared::filedialog::getSaveFileName(this,
        ruwa::shared::filedialog::category::kTheme, tr("Export theme"), QString(),
        tr("Ruwa theme preset (*.json)"));

    if (path.isEmpty()) {
        return;
    }

    QJsonObject root;
    root[QStringLiteral("format")] = QStringLiteral("ruwa-theme-preset");
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("preset")] = themePresetToJsonObject(m_currentEditingTheme);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showThemeInfo(this, tr("Export Theme"), tr("Could not write file."));
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void ThemeEditorTab::importThemePreset()
{
    const QString path = ruwa::shared::filedialog::getOpenFileName(this,
        ruwa::shared::filedialog::category::kTheme, tr("Import theme"),
        tr("Ruwa theme preset (*.json)"));

    if (path.isEmpty()) {
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        showThemeInfo(this, tr("Import Theme"), tr("Could not read file."));
        return;
    }

    QJsonParseError parseErr {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        showThemeInfo(this, tr("Import Theme"), parseErr.errorString());
        return;
    }
    if (!doc.isObject()) {
        showThemeInfo(this, tr("Import Theme"), tr("Invalid file format."));
        return;
    }

    QString err;
    ThemePreset imported;
    if (!jsonToThemePreset(doc.object(), imported, &err)) {
        showThemeInfo(this, tr("Import Theme"), err);
        return;
    }

    ThemeManager::instance().addCustomPreset(imported);
    loadThemesList();
    selectTheme(imported.id);
}

void ThemeEditorTab::deleteCurrentTheme()
{
    if (m_currentEditingTheme.isBuiltIn) {
        showThemeInfo(this, tr("Delete Theme"), tr("Cannot delete built-in themes."));
        return;
    }

    if (!confirmThemeAction(this, tr("Delete Theme"),
            tr("Are you sure you want to delete '%1'?").arg(m_currentEditingTheme.name))) {
        return;
    }

    ThemeManager::instance().removeCustomPreset(m_currentEditingTheme.id);
    loadThemesList();

    // Select first available theme
    auto presets = ThemeManager::instance().allPresets();
    if (!presets.isEmpty()) {
        selectTheme(presets.first().id);
    }
}

} // namespace ruwa::ui::tabs
