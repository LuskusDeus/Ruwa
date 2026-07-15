// SPDX-License-Identifier: MPL-2.0

#include "features/brush/editor/BrushEditorLayoutWidget.h"

#include "features/brush/editor/BrushEditorLayoutParts.h"
#include "features/brush/editor/BrushEditorParameterOverlay.h"

#include "features/brush/engine/BrushEngineRegistry.h"
#include "features/brush/ui/BrushSettingsWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/utils/FileDialogMemory.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/PresetMenuListWidget.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/ImageDropdownSelector.h"
#include "shared/widgets/inputs/StyledInputField.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/widgets/layout/FlowLayout.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <optional>

namespace ruwa::ui::windows {

using namespace ruwa::ui::core;
using namespace ruwa::ui::windows::layout_internal;

namespace {

constexpr int kLibraryAddPackActionId = 1;
constexpr int kLibraryRemovePackActionId = 2;
constexpr int kLibraryAddBrushActionId = 3;
constexpr int kLibraryImportPackActionId = 4;
constexpr int kLibraryImportIntoPackActionId = 5;
constexpr int kLibraryExportPackActionId = 6;
constexpr int kLibraryExportBrushActionId = 7;
constexpr int kBrushEditorLibraryPreviewFrameIntervalMs = 33;

QString brushImportFileFilter()
{
    return QCoreApplication::translate("BrushEditorLayoutWidget",
        "Brush Files (*.rbf *.abr);;Ruwa Brushes (*.rbf);;Photoshop Brushes (*.abr)");
}

QString brushExportFileFilter()
{
    return QCoreApplication::translate("BrushEditorLayoutWidget", "Ruwa Brushes (*.rbf)");
}

void makeWidgetTransparent(QWidget* widget)
{
    if (!widget) {
        return;
    }

    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setAttribute(Qt::WA_NoSystemBackground);
    widget->setAutoFillBackground(false);
}

QString ensureBrushFileExtension(const QString& filePath)
{
    if (filePath.trimmed().isEmpty()) {
        return {};
    }

    const QFileInfo info(filePath);
    if (info.suffix().compare(QStringLiteral("rbf"), Qt::CaseInsensitive) == 0) {
        return filePath;
    }
    return filePath + QStringLiteral(".rbf");
}

QString safeBrushFileName(const QString& name)
{
    QString safe = name.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    return safe.isEmpty() ? QStringLiteral("brushes") : safe;
}

QString packLibraryKey(const QString& presetId)
{
    return QStringLiteral("pack:%1").arg(presetId);
}

QString brushLibraryKey(const QString& brushId)
{
    return QStringLiteral("brush:%1").arg(brushId);
}

bool extractLibraryKeyId(const QVariant& data, const QString& prefix, QString* outId = nullptr)
{
    const QString value = data.toString();
    if (!value.startsWith(prefix)) {
        return false;
    }

    if (outId) {
        *outId = value.mid(prefix.size());
    }
    return true;
}

QString translateLibraryText(const QString& text)
{
    return QCoreApplication::translate("QObject", text.toUtf8().constData());
}

QImage makeCircleDabPreviewImage(int size)
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::white);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QRectF(size * 0.18, size * 0.18, size * 0.64, size * 0.64));
    return image;
}

QImage dabPreviewImageForIndex(int index)
{
    if (index <= 0) {
        return makeCircleDabPreviewImage(72);
    }

    const QImage image(QStringLiteral(":/brushes/%1").arg(index));
    return image.isNull() ? makeCircleDabPreviewImage(72) : image;
}

QString dabPresetLabel(int dabType)
{
    if (dabType <= 0) {
        return QCoreApplication::translate("BrushEditorLayoutWidget", "Circle");
    }
    return QCoreApplication::translate("BrushEditorLayoutWidget", "Preset %1").arg(dabType);
}

widgets::ImageUploadCardSelection builtInDabSelection(int dabType)
{
    widgets::ImageUploadCardSelection selection;
    if (dabType <= 0) {
        return selection;
    }

    selection.displayName = dabPresetLabel(dabType);
    selection.previewImage = dabPreviewImageForIndex(dabType);
    selection.metadata = {
        QStringLiteral("%1 x %2 px")
            .arg(selection.previewImage.width())
            .arg(selection.previewImage.height()),
        QCoreApplication::translate("BrushEditorLayoutWidget", "Built-in"),
    };
    return selection;
}

void applyAspectFitDabScale(
    const QString& imagePath, ruwa::core::brushes::BrushSettingsData& settings)
{
    const QImage image(imagePath);
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return;
    }

    const float width = static_cast<float>(image.width());
    const float height = static_cast<float>(image.height());
    if (width >= height) {
        settings.dabXScale = 1.0f;
        settings.dabYScale = std::clamp(height / width, 0.0f, 1.0f);
    } else {
        settings.dabXScale = std::clamp(width / height, 0.0f, 1.0f);
        settings.dabYScale = 1.0f;
    }
}

QVector<qreal> evenlySpacedTicks(qreal minValue, qreal maxValue, int segments)
{
    QVector<qreal> ticks;
    if (qFuzzyCompare(minValue + 1.0, maxValue + 1.0)) {
        ticks.append(minValue);
        return ticks;
    }
    if (segments <= 0) {
        ticks.append(minValue);
        ticks.append(maxValue);
        return ticks;
    }

    ticks.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const qreal t = static_cast<qreal>(i) / static_cast<qreal>(segments);
        ticks.append(minValue + (maxValue - minValue) * t);
    }
    return ticks;
}

ruwa::core::brushes::BrushDynamicsSlot defaultDynamicsSlotForSetting(const QString& settingKey)
{
    ruwa::core::brushes::BrushDynamicsSlot slot;
    slot.setting
        = ruwa::core::brushes::brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    if (!ruwa::core::brushes::supportsBrushDynamicsSetting(slot.setting)) {
        return slot;
    }

    auto& binding = slot.binding(ruwa::core::brushes::BrushInputSourceKey::TabletPressure);
    binding.setting = slot.setting;
    binding.source = ruwa::core::brushes::BrushInputSourceKey::TabletPressure;
    binding.mode = ruwa::core::brushes::defaultBrushDynamicsBlendMode(slot.setting);
    if (slot.setting == ruwa::core::brushes::BrushDynamicsSettingKey::ShapeAngle
        || slot.setting == ruwa::core::brushes::BrushDynamicsSettingKey::ColorHue) {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 0.0f, 0.65f },
        };
    } else {
        binding.curve.points = {
            { 0.0f, 0.0f, 0.65f },
            { 1.0f, 1.0f, 0.65f },
        };
    }
    binding.curve.normalize(binding.setting, binding.mode);

    return slot;
}

float bindingEndpointValue(
    const ruwa::core::brushes::BrushDynamicsBinding& binding, float inputValue)
{
    if (binding.curve.empty()) {
        return (binding.mode == ruwa::core::brushes::BrushDynamicsBlendMode::Add) ? 0.0f : 1.0f;
    }
    return binding.curve.evaluate(inputValue,
        (binding.mode == ruwa::core::brushes::BrushDynamicsBlendMode::Add) ? 0.0f : 1.0f,
        binding.setting, binding.mode);
}

ruwa::core::brushes::BrushDynamicsBinding legacyPressureBindingForSetting(
    const ruwa::core::brushes::BrushSettingsData& settings,
    ruwa::core::brushes::BrushDynamicsSettingKey setting)
{
    ruwa::core::brushes::BrushDynamicsBinding binding;
    binding.setting = setting;
    binding.source = ruwa::core::brushes::BrushInputSourceKey::TabletPressure;
    binding.mode = ruwa::core::brushes::BrushDynamicsBlendMode::Multiply;

    float minValue = 1.0f;
    float maxValue = 1.0f;
    bool enabled = false;
    bool hasLegacyState = false;

    switch (setting) {
    case ruwa::core::brushes::BrushDynamicsSettingKey::RadiusMultiplier:
        minValue = settings.sizePressureMin;
        maxValue = settings.sizePressureMax;
        enabled = settings.sizePressureEnabled;
        hasLegacyState = enabled || minValue != 1.0f || maxValue != 1.0f;
        break;
    case ruwa::core::brushes::BrushDynamicsSettingKey::OpacityMultiplier:
        minValue = settings.opacityPressureMin;
        maxValue = settings.opacityPressureMax;
        enabled = settings.opacityPressureEnabled;
        hasLegacyState = enabled || minValue != 1.0f || maxValue != 1.0f;
        break;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeFlow:
        minValue = settings.flowPressureMin;
        maxValue = settings.flowPressureMax;
        enabled = minValue < 0.999f || maxValue < 0.999f;
        hasLegacyState = enabled;
        break;
    default:
        break;
    }

    if (!hasLegacyState) {
        return binding;
    }

    binding.enabled = enabled;
    binding.curve.points = {
        { 0.0f, minValue, 0.65f },
        { 1.0f, maxValue, 0.65f },
    };
    binding.curve.normalize(setting, binding.mode);
    return binding;
}

bool hasEffectiveDynamics(const ruwa::core::brushes::BrushSettingsData& settings,
    ruwa::core::brushes::BrushDynamicsSettingKey setting)
{
    if (!ruwa::core::brushes::supportsBrushDynamicsSetting(setting)) {
        return false;
    }

    if (settings.dynamics.slotForSetting(setting).hasActiveBindings()) {
        return true;
    }

    switch (setting) {
    case ruwa::core::brushes::BrushDynamicsSettingKey::RadiusMultiplier:
        return settings.sizePressureEnabled;
    case ruwa::core::brushes::BrushDynamicsSettingKey::OpacityMultiplier:
        return settings.opacityPressureEnabled;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeFlow:
        return settings.flowPressureMin < 0.999f || settings.flowPressureMax < 0.999f;
    default:
        return false;
    }
}

qreal baseValueForDynamicsSetting(const ruwa::core::brushes::BrushSettingsData& settings,
    ruwa::core::brushes::BrushDynamicsSettingKey setting)
{
    switch (setting) {
    case ruwa::core::brushes::BrushDynamicsSettingKey::RadiusMultiplier:
        return 1.0;
    case ruwa::core::brushes::BrushDynamicsSettingKey::OpacityMultiplier:
        return 1.0;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeFlow:
        return settings.flow;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeHardness:
        return settings.hardness;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeSpacing:
        return settings.spacing;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeRoundness:
        return settings.roundness;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeAngle:
        return settings.angle;
    case ruwa::core::brushes::BrushDynamicsSettingKey::TextureAmount:
        return settings.textureAmount;
    case ruwa::core::brushes::BrushDynamicsSettingKey::TextureScale:
        return settings.textureScale;
    case ruwa::core::brushes::BrushDynamicsSettingKey::TextureContrast:
        return settings.textureContrast;
    case ruwa::core::brushes::BrushDynamicsSettingKey::TextureDepth:
        return settings.textureDepth;
    case ruwa::core::brushes::BrushDynamicsSettingKey::TextureBlend:
        return settings.textureBlend;
    case ruwa::core::brushes::BrushDynamicsSettingKey::TextureEdgeBoost:
        return settings.textureEdgeBoost;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ColorHue:
        return settings.colorHue;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ColorLightness:
        return settings.colorLightness;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ColorSaturation:
        return settings.colorSaturation;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ScatterPosition:
        return settings.scatterPosition;
    case ruwa::core::brushes::BrushDynamicsSettingKey::StrokePostCorrection:
        return settings.postCorrection;
    case ruwa::core::brushes::BrushDynamicsSettingKey::StrokeStabilization:
        return settings.stabilization;
    case ruwa::core::brushes::BrushDynamicsSettingKey::StrokeStartTaper:
        return settings.startTaper;
    case ruwa::core::brushes::BrushDynamicsSettingKey::StrokeEndTaper:
        return settings.endTaper;
    case ruwa::core::brushes::BrushDynamicsSettingKey::StrokeStartCorrectionLength:
        return settings.startCorrectionLength;
    case ruwa::core::brushes::BrushDynamicsSettingKey::StrokeEndCorrectionLength:
        return settings.endCorrectionLength;
    case ruwa::core::brushes::BrushDynamicsSettingKey::None:
    case ruwa::core::brushes::BrushDynamicsSettingKey::Count:
        break;
    }
    return 1.0;
}

std::optional<ruwa::core::brushes::BrushSettingDef> findSettingDef(
    const ruwa::core::brushes::BrushEngineDescriptor& descriptor, const QString& settingKey)
{
    for (const auto& tab : descriptor.settingsTabs) {
        for (const auto& def : tab.settings) {
            if (def.key != nullptr && settingKey == QLatin1String(def.key)) {
                return def;
            }
        }
    }
    return std::nullopt;
}

BrushEditorParameterOverlay::CurveAxesConfig curveAxesConfigForSetting(
    const ruwa::core::brushes::BrushData& brush,
    const ruwa::core::brushes::BrushSettingsData& settings, const QString& settingKey)
{
    BrushEditorParameterOverlay::CurveAxesConfig config;
    config.horizontalAxis
        = { 0.0, 1.0, 100.0, 0, QStringLiteral("%"), evenlySpacedTicks(0.0, 1.0, 4), true };

    const auto dynamicsKey
        = ruwa::core::brushes::brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    const qreal baseValue = qMax<qreal>(0.0, baseValueForDynamicsSetting(settings, dynamicsKey));

    const auto* module = ruwa::core::brushes::BrushEngineRegistry::instance().moduleOrPixelFallback(
        brush.engineId);
    if (!module) {
        config.verticalAxis.maxValue = baseValue;
        config.verticalAxis.tickValues = evenlySpacedTicks(0.0, baseValue, 4);
        return config;
    }

    const auto def = findSettingDef(module->descriptor(), settingKey);
    if (!def.has_value()) {
        config.verticalAxis.maxValue = baseValue;
        config.verticalAxis.tickValues = evenlySpacedTicks(0.0, baseValue, 4);
        return config;
    }

    QString suffix;
    const QString defSuffix = def->suffix ? QLatin1String(def->suffix) : QString();
    if (!defSuffix.isEmpty() && defSuffix != QStringLiteral("%")) {
        suffix = defSuffix;
    }

    config.verticalAxis.minValue = 0.0;
    config.verticalAxis.maxValue = baseValue;
    config.verticalAxis.displayScale = def->displayScale;
    config.verticalAxis.displayDecimals = def->displayDecimals;
    config.verticalAxis.suffix = suffix;
    config.verticalAxis.tickValues = evenlySpacedTicks(0.0, baseValue, 4);
    config.verticalAxis.visible = true;
    return config;
}

void syncLegacyPressureState(ruwa::core::brushes::BrushSettingsData& settings,
    const ruwa::core::brushes::BrushDynamicsSlot& slot)
{
    const auto& binding = slot.binding(ruwa::core::brushes::BrushInputSourceKey::TabletPressure);
    const bool mirrorable = binding.isActive()
        && binding.mode == ruwa::core::brushes::BrushDynamicsBlendMode::Multiply;
    const float minValue = mirrorable ? bindingEndpointValue(binding, 0.0f) : 1.0f;
    const float maxValue = mirrorable ? bindingEndpointValue(binding, 1.0f) : 1.0f;

    switch (slot.setting) {
    case ruwa::core::brushes::BrushDynamicsSettingKey::RadiusMultiplier:
        settings.sizePressureEnabled = mirrorable;
        settings.sizePressureMin = minValue;
        settings.sizePressureMax = maxValue;
        break;
    case ruwa::core::brushes::BrushDynamicsSettingKey::OpacityMultiplier:
        settings.opacityPressureEnabled = mirrorable;
        settings.opacityPressureMin = minValue;
        settings.opacityPressureMax = maxValue;
        break;
    case ruwa::core::brushes::BrushDynamicsSettingKey::ShapeFlow:
        settings.flowPressureMin = minValue;
        settings.flowPressureMax = maxValue;
        break;
    default:
        break;
    }
}

} // namespace

// =========================================================================
//  Constructor & public API
// =========================================================================

BrushEditorLayoutWidget::BrushEditorLayoutWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("brush_editor_layout_root"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(false);

    qApp->installEventFilter(this);

    setupUI();
    populateSettingsTabs();
    populateSettingsPages();
    setupSignals();
    loadDataFromManager();
    onThemeChanged();
}

BrushEditorLayoutWidget::~BrushEditorLayoutWidget()
{
    flushPendingParameterDynamicsCommit();
}

void BrushEditorLayoutWidget::setSelection(const QString& presetId, const QString& brushId)
{
    QString resolvedPresetId = presetId;
    if (!brushId.isEmpty()) {
        const QString presetIdForBrush = BrushManager::instance().presetIdForBrush(brushId);
        if (!presetIdForBrush.isEmpty()) {
            resolvedPresetId = presetIdForBrush;
        }
    }

    if (resolvedPresetId == m_selectedPresetId && brushId == m_selectedBrushId) {
        return;
    }

    flushPendingParameterDynamicsCommit();
    clearPendingParameterDynamicsUpdates();
    loadDataFromManager();
    // emitSignal=false: this is called by external sync (e.g. BrushPackPanel)
    // and must not fire brushSelectionChanged back; that would cause a feedback loop.
    selectPresetInternal(resolvedPresetId, brushId,
        /*emitSignal*/ false,
        /*allowFirstBrushFallback*/ false);
}

QString BrushEditorLayoutWidget::selectedBrushName() const
{
    return selectedBrushData().name;
}

bool BrushEditorLayoutWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress && m_brushNameInput) {
        auto* clickedWidget = qobject_cast<QWidget*>(watched);
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (clickedWidget && (clickedWidget == this || isAncestorOf(clickedWidget))) {
            const QPoint localPos
                = m_brushNameInput->mapFromGlobal(mouseEvent->globalPosition().toPoint());
            if (!m_brushNameInput->rect().contains(localPos)) {
                m_brushNameInput->clearInputFocus();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

// =========================================================================
//  Signals
// =========================================================================

void BrushEditorLayoutWidget::setupSignals()
{
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &BrushEditorLayoutWidget::onThemeChanged);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, [this]() {
            if (!m_libraryList) {
                return;
            }

            m_libraryList->setTitleText(tr("Brush Library"));
            m_libraryList->setSearchPlaceholderText(tr("Search brushes"));
            m_libraryList->setEmptyStateTexts(
                tr("No brushes found"), tr("Try a different search or create a new brush."));

            QVector<widgets::PresetMenuHeaderAction> actions;

            widgets::PresetMenuHeaderAction addPackAction;
            addPackAction.id = kLibraryAddPackActionId;
            addPackAction.icon = IconProvider::StandardIcon::Folder;
            addPackAction.toolTip = tr("Add Pack");
            addPackAction.text = tr("Add Pack");
            addPackAction.accent = true;
            actions.append(addPackAction);

            widgets::PresetMenuHeaderAction addBrushAction;
            addBrushAction.id = kLibraryAddBrushActionId;
            addBrushAction.icon = IconProvider::StandardIcon::FileNew;
            addBrushAction.toolTip = tr("Add Brush");
            addBrushAction.text = tr("Add Brush");
            actions.append(addBrushAction);

            widgets::PresetMenuHeaderAction importPackAction;
            importPackAction.id = kLibraryImportPackActionId;
            importPackAction.icon = IconProvider::StandardIcon::Import;
            importPackAction.toolTip = tr("Import Pack");
            importPackAction.text = tr("Import Pack");
            actions.append(importPackAction);

            m_libraryList->setHeaderActions(actions);
            rebuildLibraryList();
        });
    connect(m_settingsTabGroup, &QButtonGroup::idClicked, this,
        &BrushEditorLayoutWidget::onSettingsTabClicked);
    connect(m_settingsStack, &widgets::AnimatedStackedWidget::currentChanged, this,
        &BrushEditorLayoutWidget::onSettingsPageChanged);
    connect(m_brushNameInput, &widgets::StyledInputField::textChanged, this,
        &BrushEditorLayoutWidget::onBrushNameTextChanged);
    if (auto* lineEdit = m_brushNameInput->findChild<QLineEdit*>()) {
        connect(lineEdit, &QLineEdit::editingFinished, this,
            &BrushEditorLayoutWidget::onBrushNameEditingFinished);
    }

    for (auto* page : m_settingsPages) {
        connect(page, &widgets::BrushSettingsWidget::settingChanged, this,
            &BrushEditorLayoutWidget::onPageSettingChanged);
        connect(page, &widgets::BrushSettingsWidget::starToggled, this,
            [this](const QString& key, bool starred) {
                if (m_selectedBrushId.isEmpty())
                    return;
                BrushManager::instance().setSettingStarred(m_selectedBrushId, key, starred);
            });
        connect(page, &widgets::BrushSettingsWidget::dynamicsRequested, this,
            [this](const QString& key, const QString& label) {
                if (m_selectedBrushId.isEmpty()) {
                    return;
                }
                showParameterDynamicsOverlay(key, label);
            });
    }

    connect(m_libraryList, &widgets::PresetMenuListWidget::itemClicked, this,
        [this](const QVariant& userData) {
            QString id;
            if (extractLibraryKeyId(userData, QStringLiteral("pack:"), &id)) {
                selectPresetInternal(id, QString(), false, false);
                return;
            }

            if (!extractLibraryKeyId(userData, QStringLiteral("brush:"), &id)) {
                return;
            }

            const QString presetId = BrushManager::instance().presetIdForBrush(id);
            if (!presetId.isEmpty() && presetId != m_selectedPresetId) {
                m_selectedPresetId = presetId;
            }
            selectBrushInternal(id, true);
        });
    connect(m_libraryList, &widgets::PresetMenuListWidget::itemRenamed, this,
        [this](const QVariant& userData, const QString& newName) {
            QString presetId;
            if (!extractLibraryKeyId(userData, QStringLiteral("pack:"), &presetId)
                || presetId.isEmpty()) {
                return;
            }
            const int savedScroll = currentLibraryScroll();
            BrushManager::instance().renamePreset(presetId, newName);
            restoreLibraryScroll(savedScroll);
        });
    connect(m_libraryList, &widgets::PresetMenuListWidget::deleteRequested, this,
        [this](const QVariant& userData) {
            QString brushId;
            if (!extractLibraryKeyId(userData, QStringLiteral("brush:"), &brushId)) {
                return;
            }
            const QString presetId = BrushManager::instance().presetIdForBrush(brushId);
            if (!presetId.isEmpty() && presetId != m_selectedPresetId) {
                m_selectedPresetId = presetId;
            }
            if (brushId != m_selectedBrushId) {
                m_selectedBrushId = brushId;
            }
            onRemoveBrushClicked();
        });
    connect(m_libraryList, &widgets::PresetMenuListWidget::extraActionTriggered, this,
        [this](const QVariant& userData, int actionId) {
            QString brushId;
            if (extractLibraryKeyId(userData, QStringLiteral("brush:"), &brushId)) {
                if (actionId == kLibraryExportBrushActionId) {
                    exportBrushToFile(brushId);
                }
                return;
            }

            QString presetId;
            if (!extractLibraryKeyId(userData, QStringLiteral("pack:"), &presetId)
                || presetId.isEmpty()) {
                return;
            }
            if (actionId == kLibraryImportIntoPackActionId) {
                importBrushFileIntoPack(presetId);
                return;
            }
            if (actionId == kLibraryExportPackActionId) {
                exportPackToFile(presetId);
                return;
            }
            if (actionId != kLibraryRemovePackActionId) {
                return;
            }

            // Defer the removal to the next event-loop tick. removePreset()
            // synchronously emits presetRemoved -> loadDataFromManager(),
            // which (when the pack holds the selected brush) tears down the
            // selected brush's editor UI and rebuilds the library list,
            // deleting the very pack-divider widget whose trash-button click
            // we are still inside. Running that re-entrantly from within the
            // divider's clicked emission crashes the editor; a zero-delay
            // timer runs it cleanly after this click has fully unwound.
            QTimer::singleShot(0, this, [this, presetId]() {
                const int savedScroll = currentLibraryScroll();
                const bool removingSelected = (presetId == m_selectedPresetId);
                if (!BrushManager::instance().removePreset(presetId)) {
                    return;
                }

                m_expandedPacks.remove(presetId);
                if (removingSelected && !m_presets.isEmpty()) {
                    selectPresetInternal(m_presets.first().id, QString(), false, false);
                }
                restoreLibraryScroll(savedScroll);
            });
        });
    connect(m_libraryList, &widgets::PresetMenuListWidget::headerActionTriggered, this,
        [this](int actionId) {
            if (actionId == kLibraryAddPackActionId) {
                onAddPackClicked();
            } else if (actionId == kLibraryAddBrushActionId) {
                onAddBrushClicked();
            } else if (actionId == kLibraryImportPackActionId) {
                onImportPackClicked();
            }
        });
    if (m_libraryList->scrollArea()) {
        connect(m_libraryList->scrollArea(), &widgets::SmoothScrollArea::scrolled, this,
            [this](int) { scheduleVisibleLibraryPreviewRequests(); });
    }
    connect(m_resetButton, &QPushButton::clicked, this, &BrushEditorLayoutWidget::onResetClicked);
    connect(m_saveButton, &QPushButton::clicked, this, &BrushEditorLayoutWidget::onSaveClicked);

    connect(&BrushManager::instance(), &BrushManager::presetCreated, this, [this](const QString&) {
        if (m_managerReloadSuppressed)
            return;
        loadDataFromManager();
    });
    connect(&BrushManager::instance(), &BrushManager::presetRemoved, this, [this](const QString&) {
        if (m_managerReloadSuppressed)
            return;
        loadDataFromManager();
    });
    connect(&BrushManager::instance(), &BrushManager::brushCreated, this,
        [this](const QString&, const QString&) {
            if (m_managerReloadSuppressed)
                return;
            loadDataFromManager();
        });
    connect(&BrushManager::instance(), &BrushManager::brushRemoved, this,
        [this](const QString&, const QString&) {
            if (m_managerReloadSuppressed)
                return;
            loadDataFromManager();
        });
    connect(&BrushManager::instance(), &BrushManager::presetRenamed, this,
        [this](const QString& presetId, const QString& newName) {
            // Keep the local cache in sync with the new name.
            bool known = false;
            for (auto& preset : m_presets) {
                if (preset.id == presetId) {
                    preset.name = newName;
                    known = true;
                    break;
                }
            }

            // Update the pack divider title and the child brushes' subtitles
            // in place. A full rebuildLibraryList() would recreate every row,
            // replaying the selected row's selection animation and re-queueing
            // all previews — wasteful and visually jarring for a rename.
            const QString displayName = translateLibraryText(newName);
            bool updatedInPlace = known && m_libraryList
                && m_libraryList->setTitleForItem(packLibraryKey(presetId), displayName);
            if (updatedInPlace) {
                const auto brushes = BrushManager::instance().brushesForPreset(presetId);
                for (const auto& brush : brushes) {
                    m_libraryList->setSubtitleForItem(brushLibraryKey(brush.id), displayName);
                }
                // Empty packs render a placeholder row keyed by the pack key
                // whose subtitle is the pack name — refresh it too.
                if (brushes.isEmpty()) {
                    m_libraryList->setSubtitleForItem(packLibraryKey(presetId), displayName);
                }
            } else {
                rebuildLibraryList();
            }
        });
    connect(&BrushManager::instance(), &BrushManager::brushRenamed, this,
        [this](const QString& brushId, const QString& newName) {
            // A rename only changes one row's label — update it in place
            // instead of rebuilding the whole library list (which would
            // recreate every row and re-schedule all previews on each
            // keystroke, making name editing feel laggy).
            if (!m_libraryList
                || !m_libraryList->setTitleForItem(
                    brushLibraryKey(brushId), translateLibraryText(newName))) {
                rebuildLibraryList();
            }
            if (brushId == m_selectedBrushId) {
                if (m_brushNameInput && !m_brushNameEditInFlight) {
                    m_brushNameEditInFlight = true;
                    m_brushNameInput->setText(newName);
                    m_brushNameEditInFlight = false;
                }
                emit selectedBrushNameChanged(newName);
            }
        });
    connect(&BrushManager::instance(), &BrushManager::brushSettingsUpdated, this,
        [this](const QString& presetId, const QString& brushId, const BrushSettingsData& settings) {
            Q_UNUSED(presetId);
            m_libraryPreviewSettingsByBrush.insert(brushId, settings);
            if (m_localSettingsEditInFlight && brushId == m_selectedBrushId) {
                QVector<QPair<QString, BrushSettingsData>> requests;
                requests.append(qMakePair(brushId, settings));
                queueLibraryPreviewRequests(requests);
            } else {
                updateLibraryPreview(brushId, settings);
            }
            if (brushId == m_selectedBrushId) {
                if (m_localSettingsEditInFlight) {
                    m_currentSettings = settings;
                    return;
                }
                m_currentSettings = settings;
                updateOpenParameterDynamicsOverlayAxes();
                distributeSettings();
                updatePreview();
            }
        });
    connect(&BrushManager::instance(), &BrushManager::starredSettingsChanged, this,
        [this](const QString& brushId) {
            if (brushId != m_selectedBrushId)
                return;
            const QSet<QString> starred
                = BrushManager::instance().starredSettings(m_selectedBrushId);
            for (auto* page : m_settingsPages)
                page->setStarredKeys(starred);
        });
    connect(m_parameterOverlay, &BrushEditorParameterOverlay::slotChanged, this,
        [this](const QString& settingKey, const BrushDynamicsSlot& slot) {
            if (m_selectedBrushId.isEmpty()) {
                return;
            }
            const auto dynamicsKey = ruwa::core::brushes::brushDynamicsSettingKeyFromSettingKey(
                settingKey.toStdString());
            const bool hadActiveBinding = hasEffectiveDynamics(m_currentSettings, dynamicsKey);
            if (!applyDynamicsSlotForSetting(settingKey, slot)) {
                return;
            }
            const bool hasActiveBinding = hasEffectiveDynamics(m_currentSettings, dynamicsKey);
            if (hadActiveBinding != hasActiveBinding) {
                distributeSettings();
            }
            scheduleParameterDynamicsPreviewUpdate();
            scheduleParameterDynamicsCommit();
        });
    connect(m_parameterOverlay, &BrushEditorParameterOverlay::editingFinished, this, [this]() {
        flushPendingParameterDynamicsCommit();
        if (m_parameterDynamicsPreviewPending) {
            m_parameterDynamicsPreviewPending = false;
            updatePreview();
        }
    });
}

void BrushEditorLayoutWidget::onPageSettingChanged()
{
    if (m_selectedBrushId.isEmpty())
        return;
    auto* page = qobject_cast<widgets::BrushSettingsWidget*>(sender());
    if (page)
        page->applyTo(m_currentSettings);
    updateOpenParameterDynamicsOverlayAxes();
    clearPendingParameterDynamicsUpdates();
    m_localSettingsEditInFlight = true;
    BrushManager::instance().updateBrushSettings(m_selectedBrushId, m_currentSettings);
    m_localSettingsEditInFlight = false;
    updatePreview();
}

void BrushEditorLayoutWidget::onBrushNameTextChanged(const QString& text)
{
    if (m_brushNameEditInFlight || m_selectedBrushId.isEmpty())
        return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    const auto selected = selectedBrushData();
    if (selected.id.isEmpty() || selected.name == trimmed)
        return;
    // Guard the rename so the brushRenamed echo doesn't call setText() on the
    // input the user is actively editing — that would reset the caret to the
    // end on every keystroke. The list/title still update live (those paths
    // run regardless of the flag); only the redundant self-setText is skipped.
    m_brushNameEditInFlight = true;
    BrushManager::instance().renameBrush(m_selectedBrushId, trimmed);
    m_brushNameEditInFlight = false;
}

void BrushEditorLayoutWidget::onBrushNameEditingFinished()
{
    if (m_brushNameEditInFlight)
        return;
    commitBrushNameFromInput();
}

void BrushEditorLayoutWidget::onResetClicked()
{
    if (m_selectedBrushId.isEmpty())
        return;
    clearPendingParameterDynamicsUpdates();
    m_dabSessionSelections.remove(m_selectedBrushId);
    m_currentSettings = BrushSettingsData {};
    distributeSettings();
    m_localSettingsEditInFlight = true;
    BrushManager::instance().updateBrushSettings(m_selectedBrushId, m_currentSettings);
    m_localSettingsEditInFlight = false;
    updatePreview();
}

void BrushEditorLayoutWidget::onSaveClicked()
{
    if (m_selectedBrushId.isEmpty())
        return;
    commitBrushNameFromInput();
    flushPendingParameterDynamicsCommit();
    clearPendingParameterDynamicsUpdates();
    BrushManager::instance().updateBrushSettings(m_selectedBrushId, m_currentSettings);
}

void BrushEditorLayoutWidget::commitBrushNameFromInput()
{
    if (!m_brushNameInput || m_selectedBrushId.isEmpty())
        return;

    const auto selected = selectedBrushData();
    if (selected.id.isEmpty())
        return;

    const QString trimmed = m_brushNameInput->text().trimmed();
    if (trimmed.isEmpty()) {
        m_brushNameEditInFlight = true;
        m_brushNameInput->setText(selected.name);
        m_brushNameEditInFlight = false;
        return;
    }

    if (trimmed != selected.name) {
        BrushManager::instance().renameBrush(m_selectedBrushId, trimmed);
    } else if (trimmed != m_brushNameInput->text()) {
        m_brushNameEditInFlight = true;
        m_brushNameInput->setText(trimmed);
        m_brushNameEditInFlight = false;
    }
}

void BrushEditorLayoutWidget::updateDabControls()
{
    const QScopedValueRollback<bool> updatingDabControlsGuard(m_updatingDabControls, true);

    const bool hasBrush = !m_selectedBrushId.isEmpty();
    if (m_dabModeSelector) {
        m_dabModeSelector->setEnabled(hasBrush);
    }
    if (m_dabModeStack) {
        m_dabModeStack->setEnabled(hasBrush);
    }
    if (m_dabPresetSelector) {
        m_dabPresetSelector->setEnabled(hasBrush);
    }

    auto applySubcategoryEnabled = [this](bool enabled) {
        if (m_dabTransformSection) {
            m_dabTransformSection->setEnabled(enabled);
        }
        if (m_dabImageSection) {
            m_dabImageSection->setEnabled(enabled);
        }
    };

    if (!m_dabImageUpload) {
        applySubcategoryEnabled(false);
        return;
    }

    m_dabImageUpload->setEnabled(hasBrush);

    if (!hasBrush) {
        m_dabImageUpload->setSelection(widgets::ImageUploadCardSelection {});
        if (m_dabPresetSelector) {
            if (m_dabPresetSelector->count() > 0) {
                m_dabPresetSelector->setCurrentIndex(0);
            }
        }
        applySubcategoryEnabled(false);
        return;
    }

    if (m_dabSessionSelections.contains(m_selectedBrushId)) {
        m_dabImageUpload->setSelection(m_dabSessionSelections.value(m_selectedBrushId));
        if (m_dabPresetSelector) {
            m_dabPresetSelector->clearCurrentSelection();
        }
        applySubcategoryEnabled(m_dabImageUpload->hasSelection());
        return;
    }

    if (!m_currentSettings.dabCustomImagePath.isEmpty()) {
        m_dabImageUpload->loadFromFile(m_currentSettings.dabCustomImagePath);
        m_dabSessionSelections.insert(m_selectedBrushId, m_dabImageUpload->selection());
        if (m_dabPresetSelector) {
            m_dabPresetSelector->clearCurrentSelection();
        }
        applySubcategoryEnabled(m_dabImageUpload->hasSelection());
        return;
    }

    if (m_dabPresetSelector) {
        if (m_dabPresetSelector->count() > 0) {
            m_dabPresetSelector->setCurrentIndex(
                qBound(0, m_currentSettings.dabType, m_dabPresetSelector->count() - 1));
        }
    }

    if (m_currentSettings.dabType > 0) {
        m_dabImageUpload->setSelection(builtInDabSelection(m_currentSettings.dabType));
    } else {
        m_dabImageUpload->setSelection(widgets::ImageUploadCardSelection {});
    }
    applySubcategoryEnabled(m_dabImageUpload->hasSelection());
}

void BrushEditorLayoutWidget::distributeSettings()
{
    for (auto* page : m_settingsPages)
        page->setSettings(m_currentSettings);
    updateDabControls();
}

void BrushEditorLayoutWidget::showParameterDynamicsOverlay(
    const QString& settingKey, const QString& settingLabel)
{
    if (!m_parameterOverlay) {
        return;
    }

    flushPendingParameterDynamicsCommit();
    m_parameterOverlay->showOverlay(settingKey, settingLabel, dynamicsSlotForSetting(settingKey),
        dynamicsTargetForSetting(settingKey),
        curveAxesConfigForSetting(selectedBrushData(), m_currentSettings, settingKey));
}

void BrushEditorLayoutWidget::updateOpenParameterDynamicsOverlayAxes()
{
    if (!m_parameterOverlay || !m_parameterOverlay->isActive()) {
        return;
    }

    const QString settingKey = m_parameterOverlay->settingKey();
    if (settingKey.isEmpty()) {
        return;
    }

    m_parameterOverlay->setCurveAxesConfig(
        curveAxesConfigForSetting(selectedBrushData(), m_currentSettings, settingKey));
}

BrushEditorLayoutWidget::BrushDynamicsSlot BrushEditorLayoutWidget::dynamicsSlotForSetting(
    const QString& settingKey) const
{
    const auto dynamicsKey
        = ruwa::core::brushes::brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    if (!ruwa::core::brushes::supportsBrushDynamicsSetting(dynamicsKey)) {
        return {};
    }

    auto slot = m_currentSettings.dynamics.slotForSetting(dynamicsKey);
    if (slot.hasStoredBindings()) {
        return slot;
    }

    const auto legacyBinding = legacyPressureBindingForSetting(m_currentSettings, dynamicsKey);
    if (legacyBinding.hasStoredCurve()) {
        slot.binding(ruwa::core::brushes::BrushInputSourceKey::TabletPressure) = legacyBinding;
        return slot;
    }

    auto fallbackSlot = defaultDynamicsSlotForSetting(settingKey);
    fallbackSlot.binding(ruwa::core::brushes::BrushInputSourceKey::TabletPressure).enabled
        = slot.binding(ruwa::core::brushes::BrushInputSourceKey::TabletPressure).enabled;
    return fallbackSlot;
}

BrushEditorLayoutWidget::BrushDynamicTargetDef BrushEditorLayoutWidget::dynamicsTargetForSetting(
    const QString& settingKey) const
{
    const auto dynamicsKey
        = ruwa::core::brushes::brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    const auto* module = ruwa::core::brushes::BrushEngineRegistry::instance().moduleOrPixelFallback(
        selectedBrushData().engineId);
    const auto fallbackTarget = [&]() -> BrushDynamicTargetDef {
        if (!ruwa::core::brushes::supportsBrushDynamicsSetting(dynamicsKey)) {
            return {};
        }
        if (dynamicsKey == ruwa::core::brushes::BrushDynamicsSettingKey::ShapeAngle) {
            return ruwa::core::brushes::pressureTimeRandomDynamicsTarget(dynamicsKey,
                { ruwa::core::brushes::BrushDynamicsBlendMode::Add,
                    ruwa::core::brushes::BrushDynamicsBlendMode::Override });
        }
        return ruwa::core::brushes::pressureTimeRandomDynamicsTarget(dynamicsKey);
    };
    if (!module) {
        return fallbackTarget();
    }

    const auto def = findSettingDef(module->descriptor(), settingKey);
    if (def.has_value()) {
        return def->dynamicTarget;
    }
    return fallbackTarget();
}

bool BrushEditorLayoutWidget::applyDynamicsSlotForSetting(
    const QString& settingKey, const BrushDynamicsSlot& slot)
{
    const auto dynamicsKey
        = ruwa::core::brushes::brushDynamicsSettingKeyFromSettingKey(settingKey.toStdString());
    if (!ruwa::core::brushes::supportsBrushDynamicsSetting(dynamicsKey)) {
        return false;
    }

    auto normalizedSlot = slot;
    normalizedSlot.setting = dynamicsKey;
    for (std::size_t sourceIndex = 0; sourceIndex < normalizedSlot.bindings.size(); ++sourceIndex) {
        auto& binding = normalizedSlot.bindings[sourceIndex];
        binding.setting = dynamicsKey;
        binding.source = ruwa::core::brushes::brushInputSourceFromIndex(sourceIndex);
        if (binding.source == ruwa::core::brushes::BrushInputSourceKey::RandomValue) {
            binding.mode = ruwa::core::brushes::BrushDynamicsBlendMode::Add;
            if (binding.enabled || binding.hasStoredCurve()) {
                ruwa::core::brushes::setBrushDynamicsRandomAmount(
                    binding, ruwa::core::brushes::brushDynamicsRandomAmount(binding));
            }
        } else if (binding.source == ruwa::core::brushes::BrushInputSourceKey::StrokeDirection
            && dynamicsKey == ruwa::core::brushes::BrushDynamicsSettingKey::ShapeAngle) {
            binding.mode = ruwa::core::brushes::BrushDynamicsBlendMode::Override;
        } else {
            binding.mode
                = ruwa::core::brushes::normalizeBrushDynamicsBlendMode(dynamicsKey, binding.mode);
        }
        binding.durationSec
            = ruwa::core::brushes::clampBrushTimeDurationSeconds(binding.durationSec);
        if (binding.endAction == ruwa::core::brushes::BrushTimeEndAction::Count) {
            binding.endAction = ruwa::core::brushes::BrushTimeEndAction::Stop;
        }
        binding.curve.normalize(binding.setting, binding.mode);
    }
    bool overrideClaimed = false;
    for (auto& binding : normalizedSlot.bindings) {
        if (binding.isActive()
            && binding.mode == ruwa::core::brushes::BrushDynamicsBlendMode::Override) {
            if (overrideClaimed) {
                binding.enabled = false;
            } else {
                overrideClaimed = true;
            }
        }
    }

    m_currentSettings.dynamics.slotForSetting(dynamicsKey) = normalizedSlot;
    syncLegacyPressureState(m_currentSettings, normalizedSlot);
    return true;
}

// =========================================================================
//  UI setup
// =========================================================================

void BrushEditorLayoutWidget::setupUI()
{
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    m_leftPanel = new QWidget(this);
    m_leftPanel->setObjectName(QStringLiteral("brush_editor_left_panel"));
    m_leftPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_leftLayout = new QVBoxLayout(m_leftPanel);
    m_leftLayout->setContentsMargins(0, 0, 0, 0);
    m_leftLayout->setSpacing(0);

    m_libraryList = new widgets::PresetMenuListWidget(m_leftPanel);
    m_libraryList->setPopupStyle(false);
    m_libraryList->setEmbeddedChromeTransparent(true);
    m_libraryList->setImportExportVisible(false);
    m_libraryList->setContextMenuEnabled(true);
    m_libraryList->setTitleText(tr("Brush Library"));
    m_libraryList->setSearchPlaceholderText(tr("Search brushes"));
    m_libraryList->setEmptyStateTexts(
        tr("No brushes found"), tr("Try a different search or create a new brush."));

    QVector<widgets::PresetMenuHeaderAction> libraryActions;

    widgets::PresetMenuHeaderAction addPackAction;
    addPackAction.id = kLibraryAddPackActionId;
    addPackAction.icon = IconProvider::StandardIcon::Folder;
    addPackAction.toolTip = tr("Add Pack");
    addPackAction.text = tr("Add Pack");
    addPackAction.accent = true;
    libraryActions.append(addPackAction);

    widgets::PresetMenuHeaderAction addBrushAction;
    addBrushAction.id = kLibraryAddBrushActionId;
    addBrushAction.icon = IconProvider::StandardIcon::FileNew;
    addBrushAction.toolTip = tr("Add Brush");
    addBrushAction.text = tr("Add Brush");
    libraryActions.append(addBrushAction);

    widgets::PresetMenuHeaderAction importPackAction;
    importPackAction.id = kLibraryImportPackActionId;
    importPackAction.icon = IconProvider::StandardIcon::Import;
    importPackAction.toolTip = tr("Import Pack");
    importPackAction.text = tr("Import Pack");
    libraryActions.append(importPackAction);

    m_libraryList->setHeaderActions(libraryActions);

    m_leftLayout->addWidget(m_libraryList, 1);

    m_mainPanel = new QWidget(this);
    m_mainPanel->setObjectName(QStringLiteral("brush_editor_main_panel"));
    m_mainPanel->setAttribute(Qt::WA_StyledBackground, true);
    m_mainPanelLayout = new QVBoxLayout(m_mainPanel);
    m_mainPanelLayout->setContentsMargins(12, 10, 12, 10);
    m_mainPanelLayout->setSpacing(10);

    m_editorHeader = new QWidget(m_mainPanel);
    m_editorHeader->setObjectName(QStringLiteral("brush_editor_editor_header"));
    makeWidgetTransparent(m_editorHeader);
    m_editorHeaderLayout = new QHBoxLayout(m_editorHeader);
    m_editorHeaderLayout->setContentsMargins(0, 0, 0, 0);
    m_editorHeaderLayout->setSpacing(8);

    m_brushNameInput = new widgets::StyledInputField(
        QString(), widgets::StyledInputField::FieldType::Text, m_editorHeader);
    makeWidgetTransparent(m_brushNameInput);
    m_brushNameInput->setLabel(QString());
    m_brushNameInput->setPlaceholder(tr("Brush name"));
    m_brushNameInput->setMaxLength(96);

    auto* resetButton = new widgets::CapsuleButton(
        tr("Reset"), widgets::CapsuleButton::Variant::Secondary, m_editorHeader);
    resetButton->setBaseMinimumWidth(84);
    resetButton->setBannerBaseHeight(30);
    resetButton->syncSizeToText();
    m_resetButton = resetButton;

    auto* saveButton = new widgets::CapsuleButton(
        tr("Save"), widgets::CapsuleButton::Variant::Primary, m_editorHeader);
    saveButton->setBaseMinimumWidth(92);
    saveButton->setBannerBaseHeight(30);
    saveButton->syncSizeToText();
    m_saveButton = saveButton;

    m_editorHeaderLayout->addWidget(m_brushNameInput, 1);
    m_editorHeaderLayout->addWidget(m_resetButton, 0);
    m_editorHeaderLayout->addWidget(m_saveButton, 0);

    m_previewBlock = new QWidget(m_mainPanel);
    m_previewBlock->setObjectName(QStringLiteral("brush_editor_preview_block"));
    m_previewBlock->setAttribute(Qt::WA_StyledBackground, true);
    m_previewLayout = new QVBoxLayout(m_previewBlock);
    m_previewLayout->setContentsMargins(14, 8, 10, 10);
    m_previewLayout->setSpacing(4);

    m_previewTitleLabel = new QLabel(tr("Brush Preview"), m_previewBlock);
    m_previewTitleLabel->setObjectName(QStringLiteral("brush_editor_section_title"));
    m_previewCaptionLabel = nullptr;

    m_previewFramesRow = new QWidget(m_previewBlock);
    makeWidgetTransparent(m_previewFramesRow);
    auto* previewRowLayout = new QHBoxLayout(m_previewFramesRow);
    previewRowLayout->setContentsMargins(0, 0, 0, 0);
    previewRowLayout->setSpacing(ThemeManager::instance().scaled(6));

    m_previewFrame = new PreviewCanvas(m_previewFramesRow);
    m_previewFrame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_previewFrameDot = new DotPreviewCanvas(m_previewFramesRow);
    previewRowLayout->addWidget(m_previewFrame, 1);
    previewRowLayout->addWidget(m_previewFrameDot, 0);

    m_previewLayout->addWidget(m_previewTitleLabel);
    m_previewLayout->addWidget(m_previewFramesRow);

    m_tabsBar = new QWidget(m_mainPanel);
    m_tabsBar->setObjectName(QStringLiteral("brush_editor_tabs_bar"));
    m_tabsBar->setAttribute(Qt::WA_TranslucentBackground);
    m_tabsLayout = new widgets::FlowLayout(m_tabsBar, 0, 6, 6);
    m_tabsLayout->setMaxColumns(0);

    m_settingsTabGroup = new QButtonGroup(this);
    m_settingsTabGroup->setExclusive(true);

    m_settingsScrollArea = new widgets::SmoothScrollArea(m_mainPanel);
    m_settingsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_settingsScrollArea->setAttribute(Qt::WA_TranslucentBackground);
    m_settingsScrollArea->setAutoFillBackground(false);
    m_settingsScrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    m_settingsScrollContent = new QWidget();
    m_settingsScrollContent->setAttribute(Qt::WA_TranslucentBackground);
    m_settingsScrollLayout = new QVBoxLayout(m_settingsScrollContent);
    m_settingsScrollLayout->setContentsMargins(0, 0, 0, 0);
    m_settingsScrollLayout->setSpacing(0);
    m_settingsScrollLayout->setAlignment(Qt::AlignTop);

    m_settingsStack = new widgets::AnimatedStackedWidget(m_settingsScrollContent);
    m_settingsStack->setAnimationDuration(230);
    m_settingsStack->setAnimationEasing(QEasingCurve::InOutCubic);
    m_settingsStack->setSlideOrientation(
        widgets::AnimatedStackedWidget::SlideOrientation::Horizontal);

    m_settingsScrollLayout->addWidget(m_settingsStack);
    m_settingsScrollLayout->addStretch();
    m_settingsScrollArea->setWidget(m_settingsScrollContent);

    m_mainPanelLayout->addWidget(m_editorHeader);
    m_mainPanelLayout->addWidget(m_previewBlock);
    m_mainPanelLayout->addWidget(m_tabsBar);
    m_mainPanelLayout->addWidget(m_settingsScrollArea, 1);

    m_mainLayout->addWidget(m_leftPanel);
    m_mainLayout->addWidget(m_mainPanel, 1);

    m_parameterOverlay = new BrushEditorParameterOverlay(this);
    m_parameterOverlay->hide();
}

void BrushEditorLayoutWidget::populateSettingsTabs()
{
    const auto tabs = ruwa::core::brushes::BrushEngineRegistry::instance()
                          .pixelModule()
                          ->descriptor()
                          .settingsTabs;
    for (int i = 0; i < tabs.size(); ++i) {
        auto* tabButton = new widgets::CapsuleButton(
            QCoreApplication::translate("ruwa::core::brushes", tabs[i].label),
            widgets::CapsuleButton::Variant::Tab, m_tabsBar);
        tabButton->setCursor(Qt::PointingHandCursor);
        tabButton->setFocusPolicy(Qt::NoFocus);
        tabButton->setCheckable(true);
        tabButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_settingsTabGroup->addButton(tabButton, i);
        m_tabsLayout->addWidget(tabButton);
        m_tabButtons.append(tabButton);
    }
    if (!m_tabButtons.isEmpty())
        m_tabButtons.first()->setChecked(true);
}

QWidget* BrushEditorLayoutWidget::createCustomDabPage(
    const BrushTabDef& tabDef, const QSet<QString>& starredKeys)
{
    Q_UNUSED(starredKeys);

    auto* page = new QWidget(m_settingsStack);
    page->setObjectName(QStringLiteral("brush_editor_settings_page"));
    page->setAttribute(Qt::WA_TranslucentBackground);

    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(8);

    auto* titleLabel
        = new QLabel(QCoreApplication::translate("ruwa::core::brushes", tabDef.label), page);
    titleLabel->setObjectName(QStringLiteral("brush_editor_section_title"));
    pageLayout->addWidget(titleLabel);

    auto* descLabel
        = new QLabel(QCoreApplication::translate("ruwa::core::brushes", tabDef.description), page);
    descLabel->setObjectName(QStringLiteral("brush_editor_section_caption"));
    pageLayout->addWidget(descLabel);

    QVector<widgets::SegmentedOptionSelector::Option> modeOptions;
    modeOptions.append({ tr("Image"), QIcon(), 0 });
    modeOptions.append({ tr("Procedural"), QIcon(), 1 });
    modeOptions.append({ tr("Text"), QIcon(), 2 });

    auto* modeRow = new QWidget(page);
    modeRow->setAttribute(Qt::WA_TranslucentBackground);
    auto* modeRowLayout = new QHBoxLayout(modeRow);
    modeRowLayout->setContentsMargins(0, 0, 0, 0);
    modeRowLayout->setSpacing(0);

    m_dabModeSelector = new widgets::SegmentedOptionSelector(modeOptions, modeRow);
    m_dabModeSelector->setDisplayMode(widgets::SegmentedOptionSelector::DisplayMode::TextOnly);
    m_dabModeSelector->setCurrentIndex(0, false);
    modeRowLayout->addStretch();
    modeRowLayout->addWidget(m_dabModeSelector);
    modeRowLayout->addStretch();
    pageLayout->addWidget(modeRow);

    m_dabModeStack = new widgets::AnimatedStackedWidget(page);
    m_dabModeStack->setAnimationDuration(230);
    m_dabModeStack->setAnimationEasing(QEasingCurve::InOutCubic);
    m_dabModeStack->setSlideOrientation(
        widgets::AnimatedStackedWidget::SlideOrientation::Horizontal);
    m_dabModeStack->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    pageLayout->addWidget(m_dabModeStack);
    pageLayout->addStretch();

    auto* imagePage = new QWidget(m_dabModeStack);
    imagePage->setAttribute(Qt::WA_TranslucentBackground);
    imagePage->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* imageLayout = new QVBoxLayout(imagePage);
    imageLayout->setContentsMargins(0, 0, 0, 0);
    imageLayout->setSpacing(8);
    imageLayout->setSizeConstraint(QLayout::SetMinimumSize);

    m_dabImageUpload = new widgets::ImageUploadCardWidget(imagePage);
    m_dabImageUpload->setDialogTitle(tr("Select Dab Image"));
    m_dabImageUpload->setDirectoryMemoryCategory(
        QString::fromLatin1(ruwa::shared::filedialog::category::kBrushDab));
    m_dabImageUpload->setEmptyStateTexts(
        tr("No image loaded"), tr("Click to load a dab image or drop one here"));
    m_dabImageUpload->setPreviewFrameVisible(false);
    m_dabImageUpload->setPreviewTintEnabled(true);
    m_dabImageUpload->setPreviewTintMode(
        widgets::ImageUploadCardWidget::PreviewTintMode::LuminanceAlpha);

    auto* presetColumn = new QWidget();
    presetColumn->setAttribute(Qt::WA_TranslucentBackground);
    auto* presetColumnLayout = new QHBoxLayout(presetColumn);
    presetColumnLayout->setContentsMargins(8, 0, 0, 0);
    presetColumnLayout->setSpacing(8);

    m_dabPresetHintLabel = new QLabel(tr("or"), presetColumn);
    m_dabPresetHintLabel->setObjectName(QStringLiteral("brush_editor_dab_preset_hint"));
    m_dabPresetHintLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    presetColumnLayout->addWidget(m_dabPresetHintLabel, 0, Qt::AlignVCenter);

    m_dabPresetSelector = new widgets::ImageDropdownSelector(presetColumn);
    m_dabPresetSelector->setPlaceholderText(tr("Circle"));
    m_dabPresetSelector->setPopupMinWidth(ThemeManager::instance().scaled(318));
    m_dabPresetSelector->setPopupColumns(2);
    m_dabPresetSelector->setPopupCardSize(
        QSize(ThemeManager::instance().scaled(142), ThemeManager::instance().scaled(104)));
    m_dabPresetSelector->setPopupMaxHeight(ThemeManager::instance().scaled(368));
    for (int i = 0; i <= 5; ++i) {
        widgets::ImageDropdownItem item;
        item.text = dabPresetLabel(i);
        item.userData = i;
        item.previewImage = dabPreviewImageForIndex(i);
        item.tintPreview = true;
        m_dabPresetSelector->addItem(item);
    }
    m_dabPresetSelector->setCurrentIndex(0);
    presetColumnLayout->addWidget(m_dabPresetSelector, 0, Qt::AlignVCenter);
    m_dabImageUpload->setFooterWidget(presetColumn);
    imageLayout->addWidget(m_dabImageUpload);

    auto makeSubcategoryTitle = [imagePage](const QString& text) {
        auto* label = new QLabel(text, imagePage);
        label->setObjectName(QStringLiteral("brush_editor_section_title"));
        return label;
    };

    using ruwa::core::brushes::BrushDynamicTargetDef;
    using ruwa::core::brushes::segmentedDef;
    using ruwa::core::brushes::sliderDef;

    QVector<ruwa::core::brushes::BrushSettingDef> transformDefs;
    transformDefs.append(sliderDef("dab.xScale", QT_TR_NOOP("X Scale"), 1.0f, 0.0f, 1.0f, 0.01f,
        100, 0, "%", BrushDynamicTargetDef {}));
    transformDefs.append(sliderDef("dab.yScale", QT_TR_NOOP("Y Scale"), 1.0f, 0.0f, 1.0f, 0.01f,
        100, 0, "%", BrushDynamicTargetDef {}));
    transformDefs.append(sliderDef("dab.rotation", QT_TR_NOOP("Rotation"), 0.0f, 0.0f, 360.0f, 1.0f,
        1, 0, "\u00B0", BrushDynamicTargetDef {}));

    m_dabTransformSection = new QWidget(imagePage);
    m_dabTransformSection->setAttribute(Qt::WA_TranslucentBackground);
    m_dabTransformSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* transformLayout = new QVBoxLayout(m_dabTransformSection);
    transformLayout->setContentsMargins(0, 0, 0, 0);
    transformLayout->setSpacing(6);
    transformLayout->addWidget(makeSubcategoryTitle(tr("Transform")));
    m_dabTransformWidget = new widgets::BrushSettingsWidget(transformDefs, m_dabTransformSection,
        /*starMode=*/false);
    m_dabTransformWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    transformLayout->addWidget(m_dabTransformWidget);
    imageLayout->addWidget(m_dabTransformSection);

    QVector<ruwa::core::brushes::BrushSettingDef> imageDefs;
    imageDefs.append(sliderDef("dab.threshold", QT_TR_NOOP("Threshold"), 0.5f, 0.0f, 1.0f, 0.01f,
        100, 0, "%", BrushDynamicTargetDef {}));
    imageDefs.append(sliderDef("dab.compression", QT_TR_NOOP("Compression"), 1.0f, 0.0f, 1.0f,
        0.01f, 100, 0, "%", BrushDynamicTargetDef {}));
    imageDefs.append(segmentedDef("dab.interpolation", QT_TR_NOOP("Interpolation"), 0,
        { QT_TR_NOOP("Bilinear"), QT_TR_NOOP("Nearest") }));

    m_dabImageSection = new QWidget(imagePage);
    m_dabImageSection->setAttribute(Qt::WA_TranslucentBackground);
    m_dabImageSection->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto* imageSectionLayout = new QVBoxLayout(m_dabImageSection);
    imageSectionLayout->setContentsMargins(0, 0, 0, 0);
    imageSectionLayout->setSpacing(6);
    imageSectionLayout->addWidget(makeSubcategoryTitle(tr("Image")));
    m_dabImageWidget = new widgets::BrushSettingsWidget(imageDefs, m_dabImageSection,
        /*starMode=*/false);
    m_dabImageWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    imageSectionLayout->addWidget(m_dabImageWidget);
    imageLayout->addWidget(m_dabImageSection);

    connect(m_dabTransformWidget, &widgets::BrushSettingsWidget::settingChanged, this,
        &BrushEditorLayoutWidget::onPageSettingChanged);
    connect(m_dabImageWidget, &widgets::BrushSettingsWidget::settingChanged, this,
        &BrushEditorLayoutWidget::onPageSettingChanged);
    m_settingsPages.append(m_dabTransformWidget);
    m_settingsPages.append(m_dabImageWidget);

    imageLayout->addStretch();
    m_dabModeStack->addWidget(imagePage);

    m_dabModeStack->addWidget(createDabPlaceholderPage(IconProvider::StandardIcon::Brush,
        tr("Coming in a future version"),
        tr("Procedural dab generation will be available in upcoming releases of Ruwa."),
        m_dabModeStack));
    m_dabModeStack->addWidget(
        createDabPlaceholderPage(IconProvider::StandardIcon::Text, tr("Coming in a future version"),
            tr("Text-based dab rendering will be available in upcoming releases of Ruwa."),
            m_dabModeStack));
    m_dabModeStack->setCurrentIndexWithoutAnimation(0);
    updateDabModeStackHeight();

    connect(m_dabModeSelector, &widgets::SegmentedOptionSelector::selectionChanged, this,
        [this](int index) {
            if (!m_dabModeStack) {
                return;
            }
            m_dabModeStack->setCurrentIndex(index);
        });
    connect(
        m_dabModeStack, &widgets::AnimatedStackedWidget::currentChanged, this, [this](int index) {
            if (m_dabModeSelector && m_dabModeSelector->currentIndex() != index) {
                m_dabModeSelector->setCurrentIndex(index, false);
            }
            syncSettingsStackHeight();
        });
    connect(m_dabImageUpload, &widgets::ImageUploadCardWidget::fileLoaded, this,
        [this](const QString& path) {
            if (m_updatingDabControls) {
                return;
            }
            if (m_selectedBrushId.isEmpty() || !m_dabImageUpload) {
                return;
            }
            m_dabSessionSelections.insert(m_selectedBrushId, m_dabImageUpload->selection());
            const bool changed = (m_currentSettings.dabType != 0)
                || (m_currentSettings.dabCustomImagePath != path);
            m_currentSettings.dabType = 0;
            m_currentSettings.dabCustomImagePath = path;
            const float previousXScale = m_currentSettings.dabXScale;
            const float previousYScale = m_currentSettings.dabYScale;
            applyAspectFitDabScale(path, m_currentSettings);
            const bool scaleChanged
                = (std::abs(previousXScale - m_currentSettings.dabXScale) > 0.0001f)
                || (std::abs(previousYScale - m_currentSettings.dabYScale) > 0.0001f);
            if (m_dabTransformWidget && scaleChanged) {
                m_dabTransformWidget->setSettings(m_currentSettings);
            }
            if (changed || scaleChanged) {
                m_localSettingsEditInFlight = true;
                BrushManager::instance().updateBrushSettings(m_selectedBrushId, m_currentSettings);
                m_localSettingsEditInFlight = false;
                updatePreview();
                updateLibraryPreview(m_selectedBrushId, m_currentSettings);
            }
            updateDabControls();
            syncSettingsStackHeight();
        });
    connect(m_dabImageUpload, &widgets::ImageUploadCardWidget::selectionCleared, this, [this]() {
        if (m_updatingDabControls) {
            return;
        }
        if (m_selectedBrushId.isEmpty()) {
            updateDabControls();
            return;
        }

        m_dabSessionSelections.remove(m_selectedBrushId);

        if (m_currentSettings.dabType == 0 && m_currentSettings.dabCustomImagePath.isEmpty()) {
            updateDabControls();
            return;
        }

        m_currentSettings.dabType = 0;
        m_currentSettings.dabCustomImagePath.clear();
        m_localSettingsEditInFlight = true;
        BrushManager::instance().updateBrushSettings(m_selectedBrushId, m_currentSettings);
        m_localSettingsEditInFlight = false;
        updatePreview();
        updateLibraryPreview(m_selectedBrushId, m_currentSettings);
        updateDabControls();
    });
    connect(m_dabPresetSelector, &widgets::ImageDropdownSelector::currentIndexChanged, this,
        [this](int index) {
            if (m_updatingDabControls) {
                return;
            }
            if (m_selectedBrushId.isEmpty() || !m_dabPresetSelector || index < 0) {
                return;
            }

            const int selectedDabType = m_dabPresetSelector->itemData(index).toInt();
            m_dabSessionSelections.remove(m_selectedBrushId);
            const bool changed = (m_currentSettings.dabType != selectedDabType)
                || !m_currentSettings.dabCustomImagePath.isEmpty();
            m_currentSettings.dabType = selectedDabType;
            m_currentSettings.dabCustomImagePath.clear();
            if (changed) {
                m_localSettingsEditInFlight = true;
                BrushManager::instance().updateBrushSettings(m_selectedBrushId, m_currentSettings);
                m_localSettingsEditInFlight = false;
                updatePreview();
                updateLibraryPreview(m_selectedBrushId, m_currentSettings);
            }
            updateDabControls();
        });

    return page;
}

QWidget* BrushEditorLayoutWidget::createDabPlaceholderPage(IconProvider::StandardIcon iconType,
    const QString& title, const QString& description, QWidget* parent) const
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 12, 0, 12);
    layout->setSpacing(10);
    layout->addStretch();

    auto* iconLabel = new QLabel(page);
    iconLabel->setObjectName(QStringLiteral("brush_editor_placeholder_icon"));
    iconLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    iconLabel->setProperty("brush_editor_icon_variant", static_cast<int>(iconType));
    layout->addWidget(iconLabel);

    auto* titleLabel = new QLabel(title, page);
    titleLabel->setObjectName(QStringLiteral("brush_editor_placeholder_title"));
    titleLabel->setAlignment(Qt::AlignHCenter);
    layout->addWidget(titleLabel);

    auto* descLabel = new QLabel(description, page);
    descLabel->setObjectName(QStringLiteral("brush_editor_placeholder_caption"));
    descLabel->setAlignment(Qt::AlignHCenter);
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);
    layout->addStretch();

    return page;
}

void BrushEditorLayoutWidget::populateSettingsPages()
{
    const auto tabs = ruwa::core::brushes::BrushEngineRegistry::instance()
                          .pixelModule()
                          ->descriptor()
                          .settingsTabs;
    const QSet<QString> starred = BrushManager::instance().starredSettings(m_selectedBrushId);
    for (const auto& tabDef : tabs) {
        if (tabDef.id != nullptr && QLatin1String(tabDef.id) == QStringLiteral("dab")) {
            m_settingsStack->addWidget(createCustomDabPage(tabDef, starred));
            continue;
        }

        auto* page = new QWidget(m_settingsStack);
        page->setObjectName(QStringLiteral("brush_editor_settings_page"));
        page->setAttribute(Qt::WA_TranslucentBackground);
        auto* pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->setSpacing(8);
        auto* titleLabel
            = new QLabel(QCoreApplication::translate("ruwa::core::brushes", tabDef.label), page);
        titleLabel->setObjectName(QStringLiteral("brush_editor_section_title"));
        pageLayout->addWidget(titleLabel);
        auto* descLabel = new QLabel(
            QCoreApplication::translate("ruwa::core::brushes", tabDef.description), page);
        descLabel->setObjectName(QStringLiteral("brush_editor_section_caption"));
        pageLayout->addWidget(descLabel);
        auto* settingsWidget
            = new widgets::BrushSettingsWidget(tabDef.settings, page, /*starMode=*/true);
        settingsWidget->setStarredKeys(starred);
        settingsWidget->setEnabled(false);
        pageLayout->addWidget(settingsWidget);
        pageLayout->addStretch();
        m_settingsStack->addWidget(page);
        m_settingsPages.append(settingsWidget);
    }
    m_settingsStack->setCurrentIndexWithoutAnimation(0);
    updateDabControls();
    syncSettingsStackHeight();
}

void BrushEditorLayoutWidget::onSettingsTabClicked(int index)
{
    if (index < 0 || index >= m_settingsStack->count())
        return;

    updateDabModeStackHeight();
    const int targetPageHeight = settingsPageHeight(index);
    applySettingsStackHeight(qMax(m_settingsStack->height(), targetPageHeight));

    m_settingsStack->setCurrentIndex(index);
    m_settingsScrollArea->scrollTo(0, true);
}

void BrushEditorLayoutWidget::onSettingsPageChanged(int index)
{
    for (int i = 0; i < m_tabButtons.size(); ++i) {
        if (m_tabButtons[i] && m_tabButtons[i]->isChecked() != (i == index)) {
            m_tabButtons[i]->setChecked(i == index);
        }
    }
    syncSettingsStackHeight();
}

void BrushEditorLayoutWidget::updateDabModeStackHeight()
{
    if (!m_dabModeStack) {
        return;
    }

    int tallestPageHeight = 0;
    for (int i = 0; i < m_dabModeStack->count(); ++i) {
        QWidget* page = m_dabModeStack->widget(i);
        if (!page) {
            continue;
        }

        if (page->layout()) {
            page->layout()->activate();
        }

        const QSize pageSize = page->sizeHint().expandedTo(page->minimumSizeHint());
        tallestPageHeight = qMax(tallestPageHeight, pageSize.height());
    }

    if (tallestPageHeight <= 0) {
        return;
    }

    m_dabModeStack->setFixedHeight(tallestPageHeight);
    m_dabModeStack->updateGeometry();
}

int BrushEditorLayoutWidget::settingsPageHeight(int index)
{
    if (!m_settingsStack || index < 0 || index >= m_settingsStack->count()) {
        return 0;
    }

    QWidget* page = m_settingsStack->widget(index);
    if (!page) {
        return 0;
    }

    if (page->layout()) {
        page->layout()->activate();
    }

    return page->sizeHint().expandedTo(page->minimumSizeHint()).height();
}

void BrushEditorLayoutWidget::applySettingsStackHeight(int height)
{
    if (!m_settingsStack) {
        return;
    }

    const int minHeight = ThemeManager::instance().scaled(300);
    m_settingsStack->setFixedHeight(qMax(height, minHeight));

    if (!m_settingsScrollLayout || !m_settingsScrollContent || !m_settingsScrollArea) {
        return;
    }

    m_settingsScrollLayout->invalidate();
    m_settingsScrollLayout->activate();
    m_settingsScrollContent->adjustSize();
    m_settingsScrollContent->updateGeometry();
    QMetaObject::invokeMethod(m_settingsScrollArea, "updateScrollRange", Qt::QueuedConnection);
}

void BrushEditorLayoutWidget::syncSettingsStackHeight()
{
    if (!m_settingsStack)
        return;
    updateDabModeStackHeight();
    applySettingsStackHeight(settingsPageHeight(m_settingsStack->currentIndex()));
}

// =========================================================================
//  Theme & style
// =========================================================================

void BrushEditorLayoutWidget::onThemeChanged()
{
    updateScaledSizes();
    updateStyles();
    rebuildLibraryList();
    updateLibrarySelection();
    updatePreview();
    syncSettingsStackHeight();
    if (m_libraryList && m_libraryList->scrollArea()) {
        QMetaObject::invokeMethod(
            m_libraryList->scrollArea(), "updateScrollRange", Qt::QueuedConnection);
    }
    QMetaObject::invokeMethod(m_settingsScrollArea, "updateScrollRange", Qt::QueuedConnection);
    update();
}

void BrushEditorLayoutWidget::updateScaledSizes()
{
    auto& theme = ThemeManager::instance();
    if (m_leftPanel) {
        m_leftPanel->setFixedWidth(theme.scaled(BaseLeftPanelWidth));
        m_leftPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }
    if (m_previewFrame) {
        m_previewFrame->setFixedHeight(theme.scaled(BasePreviewHeight));
        m_previewFrame->setMinimumWidth(theme.scaled(60));
    }
    if (m_previewFrameDot) {
        const int h = theme.scaled(BasePreviewHeight);
        m_previewFrameDot->setFixedSize(h, h);
    }
    if (m_leftLayout)
        m_leftLayout->setContentsMargins(theme.scaled(8), theme.scaled(8), 0, theme.scaled(8));
    if (m_leftLayout)
        m_leftLayout->setSpacing(0);
    if (m_mainPanelLayout)
        m_mainPanelLayout->setContentsMargins(
            theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
    if (m_mainPanelLayout)
        m_mainPanelLayout->setSpacing(theme.scaled(8));
    if (m_editorHeaderLayout)
        m_editorHeaderLayout->setSpacing(theme.scaled(8));
    if (m_previewLayout)
        m_previewLayout->setContentsMargins(
            theme.scaled(12), theme.scaled(6), theme.scaled(8), theme.scaled(8));
    if (m_previewLayout)
        m_previewLayout->setSpacing(theme.scaled(4));

    applyScaledFont(m_previewTitleLabel, theme.scaled(11), true);
    if (m_brushNameInput) {
        m_brushNameInput->setMinimumWidth(theme.scaled(180));
        if (auto* lineEdit = m_brushNameInput->findChild<QLineEdit*>()) {
            applyScaledFont(lineEdit, theme.scaled(10), false);
            lineEdit->setFixedHeight(theme.scaled(20));
            if (auto* inputContainer = lineEdit->parentWidget()) {
                inputContainer->setFixedHeight(theme.scaled(30));
                if (auto* inputLayout = qobject_cast<QVBoxLayout*>(inputContainer->layout())) {
                    inputLayout->setContentsMargins(
                        theme.scaled(12), theme.scaled(5), theme.scaled(12), theme.scaled(5));
                    inputLayout->setSpacing(0);
                }
            }
            m_brushNameInput->setFixedHeight(theme.scaled(30));
        }
    }
    for (QPushButton* button : { m_resetButton, m_saveButton }) {
        if (button) {
            applyScaledFont(button, theme.scaled(10), true);
            button->setFixedHeight(theme.scaled(30));
        }
    }
    if (m_resetButton)
        m_resetButton->setMinimumWidth(theme.scaled(84));
    if (m_saveButton)
        m_saveButton->setMinimumWidth(theme.scaled(92));
    for (QPushButton* button : m_tabButtons) {
        applyScaledFont(button, theme.scaled(10), false);
        button->setMinimumHeight(theme.scaled(28));
    }
    if (m_dabModeSelector) {
        m_dabModeSelector->setMinimumWidth(theme.scaled(320));
    }
    if (m_dabImageUpload) {
        m_dabImageUpload->setMinimumWidth(theme.scaled(460));
    }
    if (m_dabPresetHintLabel) {
        applyScaledFont(m_dabPresetHintLabel, theme.scaled(10), false);
    }
    if (m_dabPresetSelector) {
        m_dabPresetSelector->setFixedHeight(theme.scaled(30));
        m_dabPresetSelector->setMinimumWidth(theme.scaled(136));
        m_dabPresetSelector->setMaximumWidth(theme.scaled(136));
        m_dabPresetSelector->setPopupMinWidth(theme.scaled(318));
        m_dabPresetSelector->setPopupCardSize(QSize(theme.scaled(142), theme.scaled(104)));
        m_dabPresetSelector->setPopupMaxHeight(theme.scaled(368));
    }
    updateDabModeStackHeight();
}

void BrushEditorLayoutWidget::updateActionIcons()
{
    const auto& colors = WidgetStyleManager::instance().colors();
    const int iconSize = ThemeManager::instance().scaled(14);

    if (m_resetButton) {
        const QPixmap resetPixmap
            = IconProvider::instance()
                  .getColoredIcon(IconProvider::StandardIcon::UndoArrow, colors.textMuted)
                  .pixmap(iconSize, iconSize);
        m_resetButton->setIcon(QIcon(resetPixmap));
        m_resetButton->setIconSize(QSize(iconSize, iconSize));
    }
    if (m_saveButton) {
        const QPixmap savePixmap
            = IconProvider::instance()
                  .getColoredIcon(IconProvider::StandardIcon::Save, colors.textOnPrimary())
                  .pixmap(iconSize, iconSize);
        m_saveButton->setIcon(QIcon(savePixmap));
        m_saveButton->setIconSize(QSize(iconSize, iconSize));
    }
}

void BrushEditorLayoutWidget::updateStyles()
{
    const auto& colors = WidgetStyleManager::instance().colors();
    QColor divider = colors.border;
    divider.setAlpha(96);

    if (m_leftPanel)
        m_leftPanel->setStyleSheet(
            QStringLiteral("QWidget#brush_editor_left_panel { background: %1; }")
                .arg(colors.surface.name(QColor::HexArgb)));
    if (m_mainPanel)
        m_mainPanel->setStyleSheet(
            QStringLiteral("QWidget#brush_editor_main_panel { background: %1; }")
                .arg(colors.surface.name(QColor::HexArgb)));
    if (m_previewBlock) {
        QColor blockBg = colors.surfaceElevated();
        m_previewBlock->setStyleSheet(
            QStringLiteral("QWidget#brush_editor_preview_block { background: %1; border: 1px solid "
                           "%2; border-radius: 14px; }")
                .arg(blockBg.name(QColor::HexArgb), divider.name(QColor::HexArgb)));
    }

    const QString titleStyle = QStringLiteral("QLabel { color: %1; background: transparent; }")
                                   .arg(colors.text.name(QColor::HexArgb));
    const QString captionStyle = QStringLiteral("QLabel { color: %1; background: transparent; }")
                                     .arg(colors.textMuted.name(QColor::HexArgb));
    for (QLabel* label : { m_previewTitleLabel }) {
        if (label)
            label->setStyleSheet(titleStyle);
    }

    const auto pageTitles
        = m_settingsStack->findChildren<QLabel*>(QStringLiteral("brush_editor_section_title"));
    for (QLabel* label : pageTitles) {
        label->setStyleSheet(titleStyle);
        applyScaledFont(label, ThemeManager::instance().scaled(11), true);
    }
    const auto pageCaptions
        = m_settingsStack->findChildren<QLabel*>(QStringLiteral("brush_editor_section_caption"));
    for (QLabel* label : pageCaptions) {
        label->setStyleSheet(captionStyle);
        applyScaledFont(label, ThemeManager::instance().scaled(10), false);
    }
    const auto placeholderTitles
        = m_settingsStack->findChildren<QLabel*>(QStringLiteral("brush_editor_placeholder_title"));
    for (QLabel* label : placeholderTitles) {
        label->setStyleSheet(titleStyle);
        applyScaledFont(label, ThemeManager::instance().scaled(11), true);
    }
    const auto placeholderCaptions = m_settingsStack->findChildren<QLabel*>(
        QStringLiteral("brush_editor_placeholder_caption"));
    for (QLabel* label : placeholderCaptions) {
        label->setStyleSheet(captionStyle);
        applyScaledFont(label, ThemeManager::instance().scaled(10), false);
    }
    const auto placeholderIcons
        = m_settingsStack->findChildren<QLabel*>(QStringLiteral("brush_editor_placeholder_icon"));
    const int placeholderIconSize = ThemeManager::instance().scaled(28);
    for (QLabel* label : placeholderIcons) {
        const auto iconType = static_cast<IconProvider::StandardIcon>(
            label->property("brush_editor_icon_variant").toInt());
        label->setPixmap(IconProvider::instance()
                .getColoredIcon(iconType, colors.textMuted)
                .pixmap(placeholderIconSize, placeholderIconSize));
    }
    if (m_dabPresetHintLabel) {
        m_dabPresetHintLabel->setStyleSheet(captionStyle);
    }

    updateActionIcons();
}

// =========================================================================
//  Data & library
// =========================================================================

void BrushEditorLayoutWidget::loadDataFromManager()
{
    m_presets = BrushManager::instance().presets();
    rebuildLibraryList();
    if (m_presets.isEmpty()) {
        m_selectedPresetId.clear();
        m_selectedBrushId.clear();
        updateLibrarySelection();
        updateToolbarState();
        m_currentSettings = BrushSettingsData {};
        for (auto* page : m_settingsPages) {
            page->setEnabled(false);
            page->setSettings(m_currentSettings);
        }
        updateDabControls();
        if (m_brushNameInput) {
            m_brushNameEditInFlight = true;
            m_brushNameInput->setText(QString());
            m_brushNameEditInFlight = false;
        }
        updatePreview();
        emit selectedBrushNameChanged(QString());
        return;
    }
    const QString preferredPreset
        = m_selectedPresetId.isEmpty() ? m_presets.first().id : m_selectedPresetId;
    selectPresetInternal(preferredPreset, m_selectedBrushId, false, false);
}

ruwa::core::brushes::BrushPreviewSpec BrushEditorLayoutWidget::makeBrushListPreviewSpec(
    const BrushSettingsData& settings) const
{
    const auto& theme = ThemeManager::instance();
    ruwa::core::brushes::BrushPreviewSpec spec;
    spec.settings = settings;
    spec.color = WidgetStyleManager::instance().colors().primary;
    spec.size = QSize(theme.scaled(168), theme.scaled(40));
    return spec;
}

BrushEditorLayoutWidget::BrushPreviewSession* BrushEditorLayoutWidget::ensureLibraryPreviewSession(
    const QString& brushId)
{
    if (brushId.isEmpty()) {
        return nullptr;
    }

    if (auto* session = m_libraryPreviewSessions.value(brushId, nullptr)) {
        return session;
    }

    auto* session = ruwa::core::brushes::BrushPreviewManager::instance().createSession(
        BrushPreviewSession::Kind::Stroke, this);
    session->setDispatchIntervalMs(kBrushEditorLibraryPreviewFrameIntervalMs);
    connect(session, &BrushPreviewSession::imageChanged, this, [this, brushId, session]() {
        if (!m_libraryList) {
            return;
        }

        const QImage image = session->image();
        if (!image.isNull()) {
            m_libraryList->setPreviewImageForItem(brushLibraryKey(brushId), image);
        }
    });
    m_libraryPreviewSessions.insert(brushId, session);
    return session;
}

void BrushEditorLayoutWidget::pruneLibraryPreviewSessions(const QSet<QString>& activeBrushIds)
{
    auto it = m_libraryPreviewSessions.begin();
    while (it != m_libraryPreviewSessions.end()) {
        if (activeBrushIds.contains(it.key())) {
            ++it;
            continue;
        }

        if (it.value()) {
            it.value()->deleteLater();
        }
        it = m_libraryPreviewSessions.erase(it);
    }
}

void BrushEditorLayoutWidget::rebuildLibraryList()
{
    if (!m_libraryList) {
        return;
    }

    QVector<widgets::PresetMenuItem> items;
    QSet<QString> activeBrushIds;
    m_libraryPreviewSettingsByBrush.clear();
    m_queuedLibraryPreviewRequests.clear();

    for (const auto& preset : m_presets) {
        const auto brushes = BrushManager::instance().brushesForPreset(preset.id);

        widgets::PresetMenuItem dividerItem;
        dividerItem.title = translateLibraryText(preset.name);
        dividerItem.isDivider = true;
        dividerItem.renamable = true;
        dividerItem.userData = packLibraryKey(preset.id);

        widgets::PresetMenuExtraAction removePackAction;
        removePackAction.id = kLibraryRemovePackActionId;
        removePackAction.text = tr("Remove Pack");
        removePackAction.icon = IconProvider::StandardIcon::Trash;
        removePackAction.dangerHover = true;
        dividerItem.extraActions.append(removePackAction);

        widgets::PresetMenuExtraAction importIntoPackAction;
        importIntoPackAction.id = kLibraryImportIntoPackActionId;
        importIntoPackAction.text = tr("Import Brushes Here");
        importIntoPackAction.icon = IconProvider::StandardIcon::Import;
        dividerItem.extraActions.append(importIntoPackAction);

        widgets::PresetMenuExtraAction exportPackAction;
        exportPackAction.id = kLibraryExportPackActionId;
        exportPackAction.text = tr("Export Pack");
        exportPackAction.icon = IconProvider::StandardIcon::Export;
        dividerItem.extraActions.append(exportPackAction);
        items.append(dividerItem);

        if (brushes.isEmpty()) {
            widgets::PresetMenuItem emptyPackItem;
            emptyPackItem.title = tr("Empty pack");
            emptyPackItem.subtitle = translateLibraryText(preset.name);
            emptyPackItem.badgeText = tr("Pack");
            emptyPackItem.previewIcon = IconProvider::StandardIcon::Brushpack;
            emptyPackItem.fillPreviewBackground = true;
            emptyPackItem.userData = packLibraryKey(preset.id);
            emptyPackItem.deletable = false;
            emptyPackItem.renamable = false;
            items.append(emptyPackItem);
            continue;
        }

        for (const auto& brush : brushes) {
            widgets::PresetMenuItem brushItem;
            brushItem.title = translateLibraryText(brush.name);
            brushItem.subtitle = translateLibraryText(preset.name);
            brushItem.previewIcon = IconProvider::StandardIcon::Brush;
            brushItem.fillPreviewBackground = true;
            brushItem.userData = brushLibraryKey(brush.id);
            brushItem.deletable = true;
            brushItem.renamable = false;
            if (auto* session = m_libraryPreviewSessions.value(brush.id, nullptr)) {
                const QImage image = session->image();
                if (!image.isNull()) {
                    brushItem.previewImage = image;
                }
            }

            widgets::PresetMenuExtraAction exportBrushAction;
            exportBrushAction.id = kLibraryExportBrushActionId;
            exportBrushAction.text = tr("Export Brush");
            exportBrushAction.icon = IconProvider::StandardIcon::Export;
            brushItem.extraActions.append(exportBrushAction);

            items.append(brushItem);
            m_libraryPreviewSettingsByBrush.insert(brush.id, brush.settings);
            activeBrushIds.insert(brush.id);
        }
    }

    m_libraryList->setItems(items);
    pruneLibraryPreviewSessions(activeBrushIds);
    if (!m_libraryPreviewRequestsPaused) {
        if (!m_selectedBrushId.isEmpty()
            && m_libraryPreviewSettingsByBrush.contains(m_selectedBrushId)) {
            updateLibraryPreview(
                m_selectedBrushId, m_libraryPreviewSettingsByBrush.value(m_selectedBrushId));
        }
        scheduleVisibleLibraryPreviewRequests();
    }
    updateLibrarySelection();
}

void BrushEditorLayoutWidget::selectPresetInternal(const QString& presetId,
    const QString& preferredBrushId, bool emitSignal, bool allowFirstBrushFallback)
{
    QString resolvedPresetId = presetId;
    bool found = false;
    for (const auto& preset : m_presets) {
        if (preset.id == resolvedPresetId) {
            found = true;
            break;
        }
    }
    if (!found)
        resolvedPresetId = m_presets.isEmpty() ? QString() : m_presets.first().id;
    m_selectedPresetId = resolvedPresetId;
    QString resolvedBrushId;
    const auto brushes = currentPresetBrushes();
    for (const auto& brush : brushes) {
        if (!preferredBrushId.isEmpty() && brush.id == preferredBrushId) {
            resolvedBrushId = brush.id;
            break;
        }
        if (resolvedBrushId.isEmpty() && !m_selectedBrushId.isEmpty()
            && brush.id == m_selectedBrushId)
            resolvedBrushId = brush.id;
    }
    if (resolvedBrushId.isEmpty() && allowFirstBrushFallback && !brushes.isEmpty()) {
        resolvedBrushId = brushes.first().id;
    }
    selectBrushInternal(resolvedBrushId, emitSignal);
    updateToolbarState();
}

void BrushEditorLayoutWidget::selectBrushInternal(const QString& brushId, bool emitSignal)
{
    flushPendingParameterDynamicsCommit();
    clearPendingParameterDynamicsUpdates();
    QString resolvedBrushId;
    if (!brushId.isEmpty()) {
        const auto brushes = currentPresetBrushes();
        for (const auto& brush : brushes) {
            if (brush.id == brushId) {
                resolvedBrushId = brush.id;
                break;
            }
        }
    }
    if (m_parameterOverlay && m_parameterOverlay->isActive()
        && resolvedBrushId != m_selectedBrushId) {
        m_parameterOverlay->hideOverlay();
    }
    m_selectedBrushId = resolvedBrushId;
    const auto selected = selectedBrushData();
    const bool hasBrush = !selected.id.isEmpty();
    const QSet<QString> starred
        = hasBrush ? BrushManager::instance().starredSettings(selected.id) : QSet<QString> {};
    m_currentSettings = hasBrush ? selected.settings : BrushSettingsData {};
    for (auto* page : m_settingsPages) {
        page->setEnabled(hasBrush);
        page->setStarredKeys(starred);
        page->setSettings(m_currentSettings);
    }
    updateDabControls();
    if (m_brushNameInput) {
        m_brushNameEditInFlight = true;
        m_brushNameInput->setText(hasBrush ? selected.name : QString());
        m_brushNameEditInFlight = false;
    }
    updatePreview();
    updateLibrarySelection();
    updateToolbarState();
    syncSettingsStackHeight();
    if (emitSignal) {
        emit selectedBrushNameChanged(hasBrush ? selected.name : QString());
        emit brushSelectionChanged(m_selectedPresetId, m_selectedBrushId);
    }
}

QVector<BrushEditorLayoutWidget::BrushData> BrushEditorLayoutWidget::currentPresetBrushes() const
{
    if (m_selectedPresetId.isEmpty())
        return {};
    return BrushManager::instance().brushesForPreset(m_selectedPresetId);
}

BrushEditorLayoutWidget::BrushData BrushEditorLayoutWidget::selectedBrushData() const
{
    if (m_selectedBrushId.isEmpty() || m_selectedPresetId.isEmpty())
        return {};
    const auto brushes = BrushManager::instance().brushesForPreset(m_selectedPresetId);
    for (const auto& brush : brushes) {
        if (brush.id == m_selectedBrushId)
            return brush;
    }
    return {};
}

void BrushEditorLayoutWidget::updateLibrarySelection()
{
    if (!m_libraryList) {
        return;
    }

    const QString selectedKey = !m_selectedBrushId.isEmpty()
        ? brushLibraryKey(m_selectedBrushId)
        : (!m_selectedPresetId.isEmpty() ? packLibraryKey(m_selectedPresetId) : QString());
    m_libraryList->setSelectedUserData(selectedKey);
    m_libraryList->setActiveUserData(selectedKey);
}

void BrushEditorLayoutWidget::updateLibraryPreview(
    const QString& brushId, const BrushSettingsData& settings)
{
    if (!m_libraryList || brushId.isEmpty()) {
        return;
    }

    auto* session = ensureLibraryPreviewSession(brushId);
    if (!session) {
        return;
    }

    const auto spec = makeBrushListPreviewSpec(settings);
    if (session->hasImageFor(spec)) {
        const QImage currentImage = session->image();
        if (!currentImage.isNull()) {
            m_libraryList->setPreviewImageForItem(brushLibraryKey(brushId), currentImage);
        }
    }

    if (!session->hasImageFor(spec)) {
        session->request(spec);
    }
}

void BrushEditorLayoutWidget::queueLibraryPreviewRequest(
    const QString& brushId, const BrushSettingsData& settings)
{
    if (brushId.isEmpty()) {
        return;
    }

    for (auto& request : m_queuedLibraryPreviewRequests) {
        if (request.first == brushId) {
            request.second = settings;
            return;
        }
    }

    m_queuedLibraryPreviewRequests.append(qMakePair(brushId, settings));
}

void BrushEditorLayoutWidget::queueLibraryPreviewRequests(
    const QVector<QPair<QString, BrushSettingsData>>& requests)
{
    if (requests.isEmpty()) {
        return;
    }

    for (const auto& request : requests) {
        queueLibraryPreviewRequest(request.first, request.second);
    }
    if (m_libraryPreviewQueueScheduled) {
        return;
    }

    m_libraryPreviewQueueScheduled = true;
    QTimer::singleShot(0, this, &BrushEditorLayoutWidget::processQueuedLibraryPreviewRequests);
}

void BrushEditorLayoutWidget::processQueuedLibraryPreviewRequests()
{
    m_libraryPreviewQueueScheduled = false;
    if (m_queuedLibraryPreviewRequests.isEmpty()) {
        return;
    }

    constexpr int kPreviewBatchSize = 8;
    const int count = qMin(kPreviewBatchSize, m_queuedLibraryPreviewRequests.size());
    for (int i = 0; i < count; ++i) {
        const auto request = m_queuedLibraryPreviewRequests.takeFirst();
        updateLibraryPreview(request.first, request.second);
    }

    if (!m_queuedLibraryPreviewRequests.isEmpty()) {
        m_libraryPreviewQueueScheduled = true;
        QTimer::singleShot(50, this, &BrushEditorLayoutWidget::processQueuedLibraryPreviewRequests);
    }
}

void BrushEditorLayoutWidget::scheduleVisibleLibraryPreviewRequests()
{
    if (!m_libraryList || m_libraryPreviewRequestsPaused || m_visibleLibraryPreviewScheduled) {
        return;
    }

    m_visibleLibraryPreviewScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_visibleLibraryPreviewScheduled = false;
        if (!m_libraryList || m_libraryPreviewRequestsPaused) {
            return;
        }

        QVector<QPair<QString, BrushSettingsData>> requests;
        if (!m_selectedBrushId.isEmpty()
            && m_libraryPreviewSettingsByBrush.contains(m_selectedBrushId)) {
            requests.append(qMakePair(
                m_selectedBrushId, m_libraryPreviewSettingsByBrush.value(m_selectedBrushId)));
        }

        const int preloadMargin = ThemeManager::instance().scaled(160);
        const QVector<QVariant> visibleItems = m_libraryList->visibleItemUserData(preloadMargin);
        for (const QVariant& userData : visibleItems) {
            QString brushId;
            if (!extractLibraryKeyId(userData, QStringLiteral("brush:"), &brushId)) {
                continue;
            }
            const auto it = m_libraryPreviewSettingsByBrush.constFind(brushId);
            if (it != m_libraryPreviewSettingsByBrush.cend()) {
                requests.append(qMakePair(brushId, it.value()));
            }
        }

        queueLibraryPreviewRequests(requests);
    });
}

int BrushEditorLayoutWidget::currentLibraryScroll() const
{
    if (!m_libraryList || !m_libraryList->scrollArea()) {
        return 0;
    }
    return m_libraryList->scrollArea()->scrollValue();
}

void BrushEditorLayoutWidget::restoreLibraryScroll(int value)
{
    if (!m_libraryList || !m_libraryList->scrollArea()) {
        return;
    }

    m_libraryList->scrollArea()->refreshScrollGeometry();
    m_libraryList->scrollArea()->scrollTo(value, false);
    QTimer::singleShot(0, this, [this, value]() {
        if (!m_libraryList || !m_libraryList->scrollArea()) {
            return;
        }
        m_libraryList->scrollArea()->refreshScrollGeometry();
        m_libraryList->scrollArea()->scrollTo(value, false);
    });
}

void BrushEditorLayoutWidget::updatePreview()
{
    auto* previewCanvas = static_cast<layout_internal::PreviewCanvas*>(m_previewFrame);
    auto* dotPreviewCanvas = static_cast<layout_internal::DotPreviewCanvas*>(m_previewFrameDot);
    if (!previewCanvas || !dotPreviewCanvas)
        return;
    if (m_selectedBrushId.isEmpty()) {
        previewCanvas->clearBrushData();
        dotPreviewCanvas->clearBrushData();
        return;
    }
    QColor previewColor = ruwa::ui::core::WidgetStyleManager::instance().colors().primary;
    previewCanvas->setBrushData(m_currentSettings, 0.5, 1.0, previewColor);
    dotPreviewCanvas->setBrushData(m_currentSettings, 0.5, 1.0, previewColor);
}

void BrushEditorLayoutWidget::scheduleParameterDynamicsPreviewUpdate()
{
    m_parameterDynamicsPreviewPending = true;
}

void BrushEditorLayoutWidget::scheduleParameterDynamicsCommit()
{
    m_pendingParameterDynamicsBrushId = m_selectedBrushId;
    m_pendingParameterDynamicsSettings = m_currentSettings;
    m_parameterDynamicsCommitPending = true;
}

void BrushEditorLayoutWidget::flushPendingParameterDynamicsCommit()
{
    if (!m_parameterDynamicsCommitPending || m_pendingParameterDynamicsBrushId.isEmpty()) {
        return;
    }

    const QString brushId = m_pendingParameterDynamicsBrushId;
    const BrushSettingsData settings = m_pendingParameterDynamicsSettings;
    m_parameterDynamicsCommitPending = false;
    m_pendingParameterDynamicsBrushId.clear();

    m_localSettingsEditInFlight = true;
    BrushManager::instance().updateBrushSettings(brushId, settings);
    m_localSettingsEditInFlight = false;
}

void BrushEditorLayoutWidget::clearPendingParameterDynamicsUpdates()
{
    m_parameterDynamicsPreviewPending = false;
    m_parameterDynamicsCommitPending = false;
    m_pendingParameterDynamicsBrushId.clear();
}

void BrushEditorLayoutWidget::updateToolbarState()
{
    const bool hasBrush = !m_selectedBrushId.isEmpty();
    if (m_brushNameInput)
        m_brushNameInput->setEnabled(hasBrush);
    if (m_resetButton)
        m_resetButton->setEnabled(hasBrush);
    if (m_saveButton)
        m_saveButton->setEnabled(hasBrush);
}

void BrushEditorLayoutWidget::onAddPackClicked()
{
    const int savedScroll = currentLibraryScroll();
    const QString newPresetId = BrushManager::instance().createPreset();
    if (newPresetId.isEmpty()) {
        return;
    }

    const QString newBrushId = BrushManager::instance().createBrush(newPresetId);
    selectPresetInternal(newPresetId, newBrushId, true, true);
    // createPreset()/createBrush() each fire a manager reload that rebuilds the
    // library list, and the trailing reselect churns it again; re-pin the scroll
    // so adding a pack doesn't snap the list back to the top.
    restoreLibraryScroll(savedScroll);
}

void BrushEditorLayoutWidget::onRemovePackClicked()
{
    if (m_selectedPresetId.isEmpty())
        return;
    const QString presetId = m_selectedPresetId;
    if (!BrushManager::instance().removePreset(presetId)) {
        return;
    }
    loadDataFromManager();
}

void BrushEditorLayoutWidget::onAddBrushClicked()
{
    if (m_selectedPresetId.isEmpty())
        return;
    const int savedScroll = currentLibraryScroll();
    const QString newBrushId = BrushManager::instance().createBrush(m_selectedPresetId);
    selectPresetInternal(m_selectedPresetId, newBrushId, true, true);
    restoreLibraryScroll(savedScroll);
}

void BrushEditorLayoutWidget::onRemoveBrushClicked()
{
    if (m_selectedBrushId.isEmpty())
        return;
    const int savedScroll = currentLibraryScroll();

    const auto brushes = BrushManager::instance().brushesForPreset(m_selectedPresetId);
    if (brushes.size() <= 1) {
        const QString removedPresetId = m_selectedPresetId;
        if (!BrushManager::instance().removePreset(removedPresetId))
            return;
        loadDataFromManager();
        restoreLibraryScroll(savedScroll);
        return;
    }

    if (!BrushManager::instance().removeBrush(m_selectedBrushId))
        return;
    loadDataFromManager();
    restoreLibraryScroll(savedScroll);
}

void BrushEditorLayoutWidget::onImportPackClicked()
{
    if (m_brushImportInProgress) {
        QMessageBox::information(
            this, tr("Import Brushes"), tr("Brush import is already running."));
        return;
    }

    const QString filePath = ruwa::shared::filedialog::getOpenFileName(this,
        ruwa::shared::filedialog::category::kBrush, tr("Import Brush Pack"),
        brushImportFileFilter());
    if (filePath.isEmpty()) {
        return;
    }

    const QString presetName = QFileInfo(filePath).completeBaseName();
    m_brushImportInProgress = true;
    QApplication::setOverrideCursor(Qt::BusyCursor);

    auto* watcher = new QFutureWatcher<ruwa::core::brushes::BrushImportResult>(this);
    connect(watcher, &QFutureWatcher<ruwa::core::brushes::BrushImportResult>::finished, this,
        [this, watcher, presetName]() {
            QApplication::restoreOverrideCursor();
            m_brushImportInProgress = false;

            const auto result = watcher->result();
            watcher->deleteLater();
            if (!result.success) {
                QMessageBox::warning(this, tr("Import Brushes"), result.errorMessage);
                return;
            }

            QString errorMessage;
            QStringList importedBrushIds;
            QString presetId;
            {
                QScopedValueRollback<bool> reloadGuard(m_managerReloadSuppressed, true);
                presetId = BrushManager::instance().importBrushesAsPreset(
                    result.brushes, presetName, &importedBrushIds, &errorMessage);
            }
            if (presetId.isEmpty()) {
                QMessageBox::warning(this, tr("Import Brushes"), errorMessage);
                return;
            }

            {
                QScopedValueRollback<bool> previewGuard(m_libraryPreviewRequestsPaused, true);
                loadDataFromManager();
            }
            selectPresetInternal(presetId,
                importedBrushIds.isEmpty() ? QString() : importedBrushIds.first(), true, true);
            if (!importedBrushIds.isEmpty()) {
                const auto brush = BrushManager::instance().brushData(importedBrushIds.first());
                if (brush.has_value()) {
                    updateLibraryPreview(importedBrushIds.first(), brush->settings);
                }
            }
            scheduleVisibleLibraryPreviewRequests();
        });
    watcher->setFuture(
        QtConcurrent::run([filePath]() { return BrushManager::readBrushFileForImport(filePath); }));
}

void BrushEditorLayoutWidget::importBrushFileIntoPack(const QString& presetId)
{
    if (presetId.isEmpty()) {
        return;
    }
    if (m_brushImportInProgress) {
        QMessageBox::information(
            this, tr("Import Brushes"), tr("Brush import is already running."));
        return;
    }

    const QString filePath = ruwa::shared::filedialog::getOpenFileName(this,
        ruwa::shared::filedialog::category::kBrush, tr("Import Brushes"), brushImportFileFilter());
    if (filePath.isEmpty()) {
        return;
    }

    const int savedScroll = currentLibraryScroll();
    m_brushImportInProgress = true;
    QApplication::setOverrideCursor(Qt::BusyCursor);

    auto* watcher = new QFutureWatcher<ruwa::core::brushes::BrushImportResult>(this);
    connect(watcher, &QFutureWatcher<ruwa::core::brushes::BrushImportResult>::finished, this,
        [this, watcher, presetId, savedScroll]() {
            QApplication::restoreOverrideCursor();
            m_brushImportInProgress = false;

            const auto result = watcher->result();
            watcher->deleteLater();
            if (!result.success) {
                QMessageBox::warning(this, tr("Import Brushes"), result.errorMessage);
                return;
            }

            QString errorMessage;
            QStringList importedBrushIds;
            bool imported = false;
            {
                QScopedValueRollback<bool> reloadGuard(m_managerReloadSuppressed, true);
                imported = BrushManager::instance().importBrushesIntoPreset(
                    result.brushes, presetId, &importedBrushIds, &errorMessage);
            }
            if (!imported) {
                QMessageBox::warning(this, tr("Import Brushes"), errorMessage);
                return;
            }

            {
                QScopedValueRollback<bool> previewGuard(m_libraryPreviewRequestsPaused, true);
                loadDataFromManager();
            }
            restoreLibraryScroll(savedScroll);
            selectPresetInternal(presetId,
                importedBrushIds.isEmpty() ? QString() : importedBrushIds.first(), true, true);
            if (!importedBrushIds.isEmpty()) {
                const auto brush = BrushManager::instance().brushData(importedBrushIds.first());
                if (brush.has_value()) {
                    updateLibraryPreview(importedBrushIds.first(), brush->settings);
                }
            }
            scheduleVisibleLibraryPreviewRequests();
        });
    watcher->setFuture(
        QtConcurrent::run([filePath]() { return BrushManager::readBrushFileForImport(filePath); }));
}

void BrushEditorLayoutWidget::exportBrushToFile(const QString& brushId)
{
    const auto brush = BrushManager::instance().brushData(brushId);
    if (!brush.has_value()) {
        return;
    }

    const QString selectedPath = ruwa::shared::filedialog::getSaveFileName(this,
        ruwa::shared::filedialog::category::kBrush, tr("Export Brush"),
        safeBrushFileName(brush->name) + QStringLiteral(".rbf"), brushExportFileFilter());
    const QString filePath = ensureBrushFileExtension(selectedPath);
    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!BrushManager::instance().exportBrushesToFile(
            filePath, QVector<BrushData> { *brush }, QString(), &errorMessage)) {
        QMessageBox::warning(this, tr("Export Brush"), errorMessage);
    }
}

void BrushEditorLayoutWidget::exportPackToFile(const QString& presetId)
{
    QString packName;
    for (const BrushPresetData& preset : m_presets) {
        if (preset.id == presetId) {
            packName = preset.name;
            break;
        }
    }

    const QVector<BrushData> brushes = BrushManager::instance().brushesForPreset(presetId);
    if (brushes.isEmpty()) {
        QMessageBox::warning(this, tr("Export Pack"), tr("There are no brushes to export."));
        return;
    }

    const QString selectedPath = ruwa::shared::filedialog::getSaveFileName(this,
        ruwa::shared::filedialog::category::kBrush, tr("Export Pack"),
        safeBrushFileName(packName) + QStringLiteral(".rbf"), brushExportFileFilter());
    const QString filePath = ensureBrushFileExtension(selectedPath);
    if (filePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    if (!BrushManager::instance().exportBrushesToFile(filePath, brushes, packName, &errorMessage)) {
        QMessageBox::warning(this, tr("Export Pack"), errorMessage);
    }
}

} // namespace ruwa::ui::windows
