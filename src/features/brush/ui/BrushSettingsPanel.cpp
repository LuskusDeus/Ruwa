// SPDX-License-Identifier: MPL-2.0

#include "BrushSettingsPanel.h"

#include "BrushSettingsWidget.h"
#include "features/brush/editor/BrushEditorLayoutParts.h"
#include "features/brush/engine/BrushEngineRegistry.h"
#include "features/brush/manager/BrushManager.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseStyledPanel.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/ToolButton.h"

#include <QCoreApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayoutItem>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QTimer>
#include <QVBoxLayout>

#include <optional>

namespace ruwa::ui::workspace {

using ruwa::core::brushes::BrushEngineRegistry;
using ruwa::core::brushes::BrushManager;
using ruwa::core::brushes::BrushSettingDef;
using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeManager;
using ruwa::ui::core::WidgetStyleManager;
using ruwa::ui::widgets::BaseStyledPanel;
using ruwa::ui::widgets::BrushSettingsWidget;
using ruwa::ui::widgets::SmoothScrollArea;
using ruwa::ui::windows::layout_internal::DotPreviewCanvas;

namespace {

IconProvider::StandardIcon iconForTab(const QString& tabId)
{
    if (tabId == QLatin1String("shape")) {
        return IconProvider::StandardIcon::Brush;
    }
    if (tabId == QLatin1String("dynamics")) {
        return IconProvider::StandardIcon::Performance;
    }
    if (tabId == QLatin1String("texture")) {
        return IconProvider::StandardIcon::Appearance;
    }
    if (tabId == QLatin1String("scatter")) {
        return IconProvider::StandardIcon::Lasso;
    }
    if (tabId == QLatin1String("stroke")) {
        return IconProvider::StandardIcon::Pencil;
    }
    return IconProvider::StandardIcon::Settings;
}

} // namespace

BrushSettingsPanel::BrushSettingsPanel(QWidget* parent)
    : DockPanel(tr("Brush Settings"), parent)
{
    setTranslatableTitle(QT_TR_NOOP("Brush Settings"));
    setIconType(IconProvider::StandardIcon::Settings);
    setMinimumPanelSize(220, 160);
    setPreferredPanelSize(300, 360);
    setClosable(true);
    setFloatable(true);
    setMovable(true);

    auto& manager = BrushManager::instance();
    connect(&manager, &BrushManager::starredSettingsChanged, this, [this](const QString& brushId) {
        if (brushId == m_currentBrushId) {
            rebuildSettings();
        }
    });
    connect(&manager, &BrushManager::brushSettingsUpdated, this,
        [this](const QString&, const QString& brushId, const BrushSettingsData& settings) {
            if (brushId != m_currentBrushId) {
                return;
            }
            m_currentSettings = settings;
            refreshSectionValues(settings);
        });
    connect(&manager, &BrushManager::brushRenamed, this,
        [this](const QString& brushId, const QString&) {
            if (brushId == m_currentBrushId) {
                updateHeader();
            }
        });
    connect(&manager, &BrushManager::presetRenamed, this,
        [this](const QString& presetId, const QString&) {
            if (!m_currentBrushId.isEmpty()
                && presetId == BrushManager::instance().presetIdForBrush(m_currentBrushId)) {
                updateHeader();
            }
        });
    connect(&manager, &BrushManager::brushRemoved, this,
        [this](const QString&, const QString& brushId) {
            if (brushId == m_currentBrushId) {
                setCurrentBrush(
                    m_canvasPanel ? m_canvasPanel->selectedBrushIdForCurrentContext() : QString());
            }
        });
    connect(&manager, &BrushManager::dataReset, this, [this]() {
        m_currentBrushId
            = m_canvasPanel ? m_canvasPanel->selectedBrushIdForCurrentContext() : QString();
        m_currentSettings = {};
        rebuildSettings();
    });
}

BrushSettingsPanel::~BrushSettingsPanel() = default;

CanvasPanel* BrushSettingsPanel::canvasPanel() const
{
    return m_canvasPanel.data();
}

void BrushSettingsPanel::setCanvasPanel(CanvasPanel* canvasPanel)
{
    if (m_canvasPanel == canvasPanel) {
        return;
    }
    if (m_canvasPanel) {
        disconnect(m_canvasPanel.data(), nullptr, this, nullptr);
    }

    m_canvasPanel = canvasPanel;
    if (m_canvasPanel) {
        connect(m_canvasPanel.data(), &CanvasPanel::brushSelectionContextChanged, this,
            [this](CanvasPanel::ToolMode, const QString& brushId) { setCurrentBrush(brushId); });
        connect(m_canvasPanel.data(), &QObject::destroyed, this, [this]() {
            m_canvasPanel = nullptr;
            setCurrentBrush({});
        });
    }

    setCurrentBrush(m_canvasPanel ? m_canvasPanel->selectedBrushIdForCurrentContext() : QString());
}

QWidget* BrushSettingsPanel::createContent()
{
    auto& theme = ThemeManager::instance();

    m_contentWidget = new QWidget();
    m_contentWidget->setAttribute(Qt::WA_TranslucentBackground);

    auto* rootLayout = new QVBoxLayout(m_contentWidget);
    rootLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(10));
    rootLayout->setSpacing(theme.scaled(6));

    m_headerCard = new BaseStyledPanel(QStringLiteral("SettingsPanel"), m_contentWidget);
    m_headerCard->setHoverEnabled(false);
    m_headerCard->setMinimumWidth(0);
    m_headerCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* headerLayout = new QHBoxLayout(m_headerCard);
    headerLayout->setContentsMargins(
        theme.scaled(9), theme.scaled(8), theme.scaled(8), theme.scaled(8));
    headerLayout->setSpacing(theme.scaled(8));

    m_dabPreview = new DotPreviewCanvas(m_headerCard);
    m_dabPreview->setFixedSize(theme.scaled(30), theme.scaled(30));

    auto* headerText = new QWidget(m_headerCard);
    headerText->setAttribute(Qt::WA_TranslucentBackground);
    headerText->setMinimumWidth(0);
    headerText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* headerTextLayout = new QVBoxLayout(headerText);
    headerTextLayout->setContentsMargins(0, 0, 0, 0);
    headerTextLayout->setSpacing(0);

    m_headerCaptionLabel = new QLabel(tr("Brush pack"), headerText);
    m_headerCaptionLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_headerCaptionLabel->setMinimumWidth(0);
    m_headerCaptionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    m_brushNameLabel = new QLabel(headerText);
    m_brushNameLabel->setObjectName(QStringLiteral("brushSettingsPanelBrushName"));
    m_brushNameLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_brushNameLabel->setMinimumWidth(0);
    m_brushNameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    headerTextLayout->addWidget(m_headerCaptionLabel);
    headerTextLayout->addWidget(m_brushNameLabel);

    m_openEditorButton = new ToolButton(ToolButton::Mode::Action, m_headerCard);
    m_openEditorButton->setIconType(IconProvider::StandardIcon::Pencil);
    m_openEditorButton->setBaseSquareSize(30, 14);
    m_openEditorButton->setChromeStyle(ToolButton::ChromeStyle::PrimaryHover);
    m_openEditorButton->setBorderVisible(false);
    m_openEditorButton->setMutedNormalIcon(true);
    m_openEditorButton->setFocusPolicy(Qt::NoFocus);
    m_openEditorButton->setToolTip(tr("Open Brush Editor"));
    m_openEditorButton->setAccessibleName(tr("Open Brush Editor"));
    connect(m_openEditorButton, &QPushButton::clicked, this, [this]() {
        if (!m_currentBrushId.isEmpty()) {
            emit brushEditorRequested(m_currentBrushId);
        }
    });

    headerLayout->addWidget(m_dabPreview, 0, Qt::AlignVCenter);
    headerLayout->addWidget(headerText, 1);
    headerLayout->addWidget(m_openEditorButton, 0, Qt::AlignVCenter);
    rootLayout->addWidget(m_headerCard);

    m_scrollArea = new SmoothScrollArea(m_contentWidget);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollArea->setAutoFillBackground(false);
    m_scrollArea->setFillBackground(false);
    m_scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    m_scrollContent = new QWidget();
    m_scrollContent->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(0, 0, 0, 0);
    m_scrollLayout->setSpacing(0);
    m_scrollLayout->setAlignment(Qt::AlignTop);

    m_scrollArea->setWidget(m_scrollContent);
    rootLayout->addWidget(m_scrollArea, 1);

    rebuildSettings();
    return m_contentWidget;
}

void BrushSettingsPanel::onThemeChanged()
{
    DockPanel::onThemeChanged();
    if (!m_contentWidget) {
        return;
    }

    auto& theme = ThemeManager::instance();
    if (auto* layout = qobject_cast<QVBoxLayout*>(m_contentWidget->layout())) {
        layout->setContentsMargins(
            theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(10));
        layout->setSpacing(theme.scaled(6));
    }
    if (m_headerCard) {
        if (auto* layout = qobject_cast<QHBoxLayout*>(m_headerCard->layout())) {
            layout->setContentsMargins(
                theme.scaled(9), theme.scaled(8), theme.scaled(8), theme.scaled(8));
            layout->setSpacing(theme.scaled(8));
        }
    }
    if (m_dabPreview) {
        m_dabPreview->setFixedSize(theme.scaled(30), theme.scaled(30));
    }
    if (m_openEditorButton) {
        m_openEditorButton->setBaseSquareSize(30, 14);
    }
    rebuildSettings();
}

void BrushSettingsPanel::setCurrentBrush(const QString& brushId)
{
    if (m_currentBrushId == brushId) {
        updateHeader();
        if (brushId.isEmpty()) {
            return;
        }
        auto& manager = BrushManager::instance();
        const auto brush = manager.brushData(brushId);
        const auto settings = manager.brushSettings(brushId);
        if (brush && settings) {
            m_currentSettings = *settings;
            refreshSectionValues(*settings);
        } else {
            rebuildSettings();
        }
        return;
    }

    m_currentBrushId = brushId;
    m_currentSettings = {};
    rebuildSettings();
}

void BrushSettingsPanel::rebuildSettings()
{
    if (!m_scrollLayout || !m_scrollContent) {
        return;
    }

    while (QLayoutItem* item = m_scrollLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    m_sectionWidgets.clear();
    m_emptyLabel = nullptr;
    updateHeader();

    if (m_currentBrushId.isEmpty()) {
        updateEmptyState(tr("Select a brush to edit its favorite settings."));
        refreshScrollGeometry();
        return;
    }

    auto& manager = BrushManager::instance();
    const auto brush = manager.brushData(m_currentBrushId);
    const auto settings = manager.brushSettings(m_currentBrushId);
    if (!brush || !settings) {
        updateEmptyState(tr("The selected brush is unavailable."));
        refreshScrollGeometry();
        return;
    }
    m_currentSettings = *settings;

    const auto* module = BrushEngineRegistry::instance().moduleOrPixelFallback(brush->engineId);
    if (!module) {
        updateEmptyState(tr("This brush engine does not expose editable settings."));
        refreshScrollGeometry();
        return;
    }

    const auto tabs = module->descriptor().settingsTabs;
    const QSet<QString> starred = manager.starredSettings(m_currentBrushId);
    auto& theme = ThemeManager::instance();
    QColor dividerColor = WidgetStyleManager::instance().colors().border;
    dividerColor.setAlpha(90);
    bool firstVisibleSection = true;

    for (const auto& tab : tabs) {
        QVector<BrushSettingDef> tabDefs;
        tabDefs.reserve(tab.settings.size());
        for (const auto& def : tab.settings) {
            if (starred.contains(QLatin1String(def.key))) {
                tabDefs.append(def);
            }
        }
        if (tabDefs.isEmpty()) {
            continue;
        }

        if (!firstVisibleSection) {
            auto* separator = new QWidget(m_scrollContent);
            separator->setFixedHeight(theme.scaled(1));
            separator->setStyleSheet(
                QStringLiteral("background: %1;").arg(dividerColor.name(QColor::HexArgb)));
            m_scrollLayout->addWidget(separator);
            m_scrollLayout->addSpacing(theme.scaled(8));
        }

        auto* categoryHeader = new QWidget(m_scrollContent);
        categoryHeader->setAttribute(Qt::WA_TranslucentBackground);
        auto* categoryLayout = new QHBoxLayout(categoryHeader);
        categoryLayout->setContentsMargins(0, 0, 0, 0);
        categoryLayout->setSpacing(theme.scaled(6));

        auto* categoryIconLabel = new QLabel(categoryHeader);
        const int iconSize = theme.scaled(12);
        const QColor iconTint = WidgetStyleManager::instance().colors().textMuted;
        const auto iconType = iconForTab(QLatin1String(tab.id));
        QPixmap iconPixmap = IconProvider::instance()
                                 .getColoredIcon(iconType, iconTint)
                                 .pixmap(iconSize, iconSize);
        if (iconPixmap.isNull()) {
            iconPixmap = IconProvider::instance().getPixmap(iconType, QSize(iconSize, iconSize));
        }
        categoryIconLabel->setPixmap(iconPixmap);
        categoryIconLabel->setFixedSize(iconSize, iconSize);
        categoryIconLabel->setAlignment(Qt::AlignCenter);

        auto* categoryLabel = new QLabel(
            QCoreApplication::translate("ruwa::core::brushes", tab.label), categoryHeader);
        categoryLabel->setStyleSheet(
            QStringLiteral("color: %1; background: transparent; font-weight: 600;")
                .arg(WidgetStyleManager::instance().colors().textMuted.name(QColor::HexArgb)));
        QFont sectionFont = categoryLabel->font();
        sectionFont.setPixelSize(theme.scaled(10));
        categoryLabel->setFont(sectionFont);

        categoryLayout->addWidget(categoryIconLabel);
        categoryLayout->addWidget(categoryLabel, 1);
        m_scrollLayout->addWidget(categoryHeader);
        m_scrollLayout->addSpacing(theme.scaled(4));

        auto* section = new BrushSettingsWidget(tabDefs, m_scrollContent);
        section->setSettings(m_currentSettings);
        connect(section, &BrushSettingsWidget::settingChanged, this,
            [this, section]() { applySectionEdit(section); });
        m_scrollLayout->addWidget(section);
        m_scrollLayout->addSpacing(theme.scaled(6));
        m_sectionWidgets.append(section);
        firstVisibleSection = false;
    }

    if (m_sectionWidgets.isEmpty()) {
        updateEmptyState(tr("This brush has no favorite settings."));
    } else {
        m_scrollLayout->addStretch();
    }
    refreshScrollGeometry();
}

void BrushSettingsPanel::refreshSectionValues(const BrushSettingsData& settings)
{
    for (auto* section : m_sectionWidgets) {
        if (section) {
            section->setSettings(settings);
        }
    }
    updateDabPreview(&settings);
}

void BrushSettingsPanel::applySectionEdit(BrushSettingsWidget* editedSection)
{
    if (!editedSection || m_currentBrushId.isEmpty()) {
        return;
    }

    auto& manager = BrushManager::instance();
    const auto managedSettings = manager.brushSettings(m_currentBrushId);
    if (!managedSettings) {
        setCurrentBrush({});
        return;
    }

    BrushSettingsData updated = *managedSettings;
    editedSection->applyTo(updated);
    if (!manager.updateBrushSettings(m_currentBrushId, updated)) {
        refreshSectionValues(*managedSettings);
        return;
    }

    const auto appliedSettings = manager.brushSettings(m_currentBrushId);
    m_currentSettings = appliedSettings.value_or(updated);
    refreshSectionValues(m_currentSettings);
    if (m_canvasPanel && m_canvasPanel->selectedBrushIdForCurrentContext() == m_currentBrushId) {
        m_canvasPanel->reapplyCurrentToolState();
    }
}

void BrushSettingsPanel::updateHeader()
{
    if (!m_brushNameLabel || !m_dabPreview || !m_headerCaptionLabel) {
        return;
    }

    QString brushName;
    QString presetId;
    QString presetName;
    std::optional<BrushSettingsData> brushSettings;
    if (!m_currentBrushId.isEmpty()) {
        auto& manager = BrushManager::instance();
        if (const auto brush = manager.brushData(m_currentBrushId)) {
            brushName = QCoreApplication::translate("QObject", brush->name.toUtf8().constData());
            presetId = manager.presetIdForBrush(m_currentBrushId);
            brushSettings = manager.brushSettings(m_currentBrushId);
            for (const auto& preset : manager.presets()) {
                if (preset.id == presetId) {
                    presetName
                        = QCoreApplication::translate("QObject", preset.name.toUtf8().constData());
                    break;
                }
            }
        }
    }

    const bool hasBrush = !brushName.isEmpty() && !presetId.isEmpty() && !presetName.isEmpty();
    m_headerCaptionLabel->setText(hasBrush ? presetName : tr("Brush pack"));
    m_headerCaptionLabel->setToolTip(hasBrush ? presetName : QString());
    m_brushNameLabel->setText(hasBrush ? brushName : tr("No active brush"));
    m_brushNameLabel->setToolTip(brushName);
    if (m_openEditorButton) {
        m_openEditorButton->setEnabled(hasBrush);
    }

    auto& theme = ThemeManager::instance();
    const auto& colors = WidgetStyleManager::instance().colors();

    updateDabPreview(brushSettings ? &*brushSettings : nullptr);

    QFont captionFont = m_headerCaptionLabel->font();
    captionFont.setPixelSize(theme.scaled(9));
    captionFont.setWeight(QFont::Medium);
    m_headerCaptionLabel->setFont(captionFont);
    m_headerCaptionLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(colors.textMuted.name(QColor::HexArgb)));

    QFont nameFont = m_brushNameLabel->font();
    nameFont.setPixelSize(theme.scaled(11));
    nameFont.setWeight(QFont::DemiBold);
    m_brushNameLabel->setFont(nameFont);
    m_brushNameLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg((hasBrush ? colors.text : colors.textMuted).name(QColor::HexArgb)));
}

void BrushSettingsPanel::updateDabPreview(const BrushSettingsData* settings)
{
    if (!m_dabPreview) {
        return;
    }
    if (!settings || m_currentBrushId.isEmpty()) {
        m_dabPreview->clearBrushData();
        return;
    }

    m_dabPreview->setBrushData(
        *settings, 0.5, 1.0, WidgetStyleManager::instance().colors().primary);
}

void BrushSettingsPanel::updateEmptyState(const QString& text)
{
    m_emptyLabel = new QLabel(text, m_scrollContent);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(WidgetStyleManager::instance().colors().textMuted.name(QColor::HexArgb)));
    m_scrollLayout->addStretch();
    m_scrollLayout->addWidget(m_emptyLabel);
    m_scrollLayout->addStretch();
}

void BrushSettingsPanel::refreshScrollGeometry()
{
    if (!m_scrollArea || !m_scrollContent || !m_scrollLayout) {
        return;
    }

    m_scrollLayout->invalidate();
    m_scrollLayout->activate();
    m_scrollContent->adjustSize();
    m_scrollContent->updateGeometry();
    m_scrollArea->refreshScrollGeometry();
    QTimer::singleShot(0, this, [this]() {
        if (m_scrollArea) {
            m_scrollArea->refreshScrollGeometry();
        }
    });
}

} // namespace ruwa::ui::workspace
