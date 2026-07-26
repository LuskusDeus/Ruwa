// SPDX-License-Identifier: MPL-2.0

#include "features/brush/editor/ProceduralTextureEditorWidget.h"

#include "features/brush/engine/PixelBrushModule.h"
#include "features/brush/manager/BrushSettingDefs.h"
#include "features/brush/ui/BrushSettingsWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/tiles/TileBrush.h"
#include "shared/widgets/inputs/ImageDropdownSelector.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <cstring>

namespace ruwa::ui::windows {

using namespace ruwa::ui::core;

namespace {

constexpr int kPreviewCornerRadius = 8;

QImage generateTexturePreview(const ruwa::core::brushes::BrushSettingsData& settings)
{
    auto normalized
        = ruwa::core::brushes::PixelBrushModule::normalizeCompatibilitySettings(settings);
    // The mini-editor previews the generated noise itself. Stroke-application parameters live
    // outside this widget and are neutralized here so they cannot alter the source texture.
    normalized.textureAmount = 1.0f;
    // Keep the preview visually denser than the generated 1:1 texture without changing the
    // brush setting itself. In the generator a larger scale means smaller texture features.
    normalized.textureScale = 1.5f;
    normalized.textureContrast = 0.2f; // 0.5 + contrast * 2.5 == 1.0 (identity)
    normalized.textureDepth = 1.0f;
    normalized.textureBlend = 0.0f;
    normalized.textureEdgeBoost = 0.0f;

    aether::TileBrush brush;
    brush.setBrushSettings(normalized);
    const std::vector<uint8_t>& alpha = brush.proceduralTextureTileAlpha(aether::TileKey { 0, 0 });

    QImage image(static_cast<int>(aether::TILE_SIZE), static_cast<int>(aether::TILE_SIZE),
        QImage::Format_Grayscale8);
    if (image.isNull() || alpha.size() < aether::TILE_SIZE * aether::TILE_SIZE) {
        return {};
    }

    for (uint32_t y = 0; y < aether::TILE_SIZE; ++y) {
        std::memcpy(image.scanLine(static_cast<int>(y)),
            alpha.data() + static_cast<std::size_t>(y) * aether::TILE_SIZE, aether::TILE_SIZE);
    }
    return image;
}

} // namespace

class ProceduralTexturePreviewWidget final : public QWidget {
public:
    explicit ProceduralTexturePreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMinimumSize(0, 0);
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setPreviewImage(const QImage& image)
    {
        if (m_image == image) {
            return;
        }
        m_image = image;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();
        const qreal radius = theme.scaled(kPreviewCornerRadius);
        const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        QPainterPath clipPath;
        clipPath.addRoundedRect(bounds, radius, radius);
        painter.setClipPath(clipPath);
        painter.fillRect(bounds, colors.surfaceElevated());
        if (!m_image.isNull()) {
            QRectF sourceRect(m_image.rect());
            const qreal targetAspect = bounds.width() / qMax<qreal>(1.0, bounds.height());
            const qreal sourceAspect = sourceRect.width() / qMax<qreal>(1.0, sourceRect.height());
            if (sourceAspect > targetAspect) {
                const qreal croppedWidth = sourceRect.height() * targetAspect;
                sourceRect.setLeft(sourceRect.center().x() - croppedWidth * 0.5);
                sourceRect.setWidth(croppedWidth);
            } else if (sourceAspect < targetAspect) {
                const qreal croppedHeight = sourceRect.width() / targetAspect;
                sourceRect.setTop(sourceRect.center().y() - croppedHeight * 0.5);
                sourceRect.setHeight(croppedHeight);
            }
            painter.drawImage(bounds, m_image, sourceRect);
        }
        painter.setClipping(false);

        QPen borderPen(colors.borderSubtle());
        borderPen.setWidthF(1.0);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds, radius, radius);
    }

private:
    QImage m_image;
};

ProceduralTextureEditorWidget::ProceduralTextureEditorWidget(QWidget* parent)
    : BaseStyledPanel(QStringLiteral("SettingsPanel"), parent)
{
    setHoverEnabled(false);
    setMinimumWidth(0);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* rootLayout = new QHBoxLayout(this);

    m_previewWidget = new ProceduralTexturePreviewWidget(this);
    rootLayout->addWidget(m_previewWidget, 1);

    m_previewRenderWatcher = new QFutureWatcher<QImage>(this);
    connect(m_previewRenderWatcher, &QFutureWatcher<QImage>::finished, this,
        &ProceduralTextureEditorWidget::handlePreviewRenderFinished);

    m_settingsColumn = new QWidget(this);
    m_settingsColumn->setAttribute(Qt::WA_TranslucentBackground);
    m_settingsColumn->setMinimumWidth(0);
    m_settingsColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* settingsLayout = new QVBoxLayout(m_settingsColumn);

    m_typeSelector = new widgets::ImageDropdownSelector(m_settingsColumn);
    m_typeSelector->setMinimumWidth(0);
    m_typeSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_typeSelector->setPopupColumns(3);
    m_typeSelector->setPopupCardSize(
        QSize(ThemeManager::instance().scaled(112), ThemeManager::instance().scaled(88)));
    m_typeSelector->setPopupMaxHeight(ThemeManager::instance().scaled(300));

    const QStringList typeNames {
        QCoreApplication::translate("ruwa::core::brushes", "Pencil Grain"),
        QCoreApplication::translate("ruwa::core::brushes", "Fractal Noise"),
        QCoreApplication::translate("ruwa::core::brushes", "Perlin Noise"),
        QCoreApplication::translate("ruwa::core::brushes", "Dot Grid"),
        QCoreApplication::translate("ruwa::core::brushes", "Parallel Lines"),
        QCoreApplication::translate("ruwa::core::brushes", "Checkerboard"),
    };
    m_typeSelector->addCategory(QCoreApplication::translate("ruwa::core::brushes", "Noise"));
    for (int type = 0; type < typeNames.size(); ++type) {
        if (type == BrushSettingsData::TextureTypeDots) {
            m_typeSelector->addCategory(
                QCoreApplication::translate("ruwa::core::brushes", "Patterns"));
        }
        widgets::ImageDropdownItem item;
        item.text = typeNames[type];
        item.userData = type;
        m_typeSelector->addItem(item);
    }
    settingsLayout->addWidget(m_typeSelector);

    auto* typePreviewWatcher = new QFutureWatcher<QList<QImage>>(this);
    connect(typePreviewWatcher, &QFutureWatcher<QList<QImage>>::finished, this,
        [this, typePreviewWatcher]() {
            const QList<QImage> previews = typePreviewWatcher->result();
            typePreviewWatcher->deleteLater();
            if (!m_typeSelector) {
                return;
            }
            for (int type = 0; type < previews.size(); ++type) {
                const int itemIndex = m_typeSelector->findIndexByData(type);
                if (itemIndex >= 0) {
                    m_typeSelector->setItemPreviewImage(itemIndex, previews[type]);
                }
            }
        });
    typePreviewWatcher->setFuture(QtConcurrent::run([]() {
        QList<QImage> previews;
        constexpr int textureTypeCount
            = static_cast<int>(BrushSettingsData::TextureTypeCheckerboard) + 1;
        previews.reserve(textureTypeCount);
        BrushSettingsData previewSettings;
        for (int type = 0; type < textureTypeCount; ++type) {
            previewSettings.textureType = type;
            previews.append(generateTexturePreview(previewSettings));
        }
        return previews;
    }));

    m_separator = new QWidget(m_settingsColumn);
    m_separator->setFixedHeight(1);
    m_separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    settingsLayout->addWidget(m_separator);

    m_settingsScrollArea = new widgets::SmoothScrollArea(m_settingsColumn);
    m_settingsScrollArea->setFillBackground(false);
    m_settingsScrollArea->setScrollBarTransparentTrack(true);
    m_settingsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_settingsScrollArea->setScrollBarAlwaysReserved(false);
    m_settingsScrollArea->setMinimumSize(0, 0);
    m_settingsScrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_typeSettingsContent = new QWidget();
    m_typeSettingsContent->setAttribute(Qt::WA_TranslucentBackground);
    m_typeSettingsContent->setMinimumWidth(0);
    auto* typeSettingsLayout = new QVBoxLayout(m_typeSettingsContent);
    typeSettingsLayout->setContentsMargins(0, 0, 0, 0);
    typeSettingsLayout->setSpacing(ThemeManager::instance().scaled(6));

    QVector<ruwa::core::brushes::BrushSettingDef> pencilDefs;
    pencilDefs.append(ruwa::core::brushes::sliderDef("texture.pencilDetail",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Detail"), 0.5f, 0.0f, 1.0f));
    pencilDefs.append(ruwa::core::brushes::sliderDef("texture.pencilStreakStrength",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Streak Strength"), 0.3f, 0.0f, 1.0f));

    QVector<ruwa::core::brushes::BrushSettingDef> noiseDefs;
    noiseDefs.append(ruwa::core::brushes::sliderDef("texture.noiseOctaves",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Octaves"), 3.0f, 1.0f, 5.0f, 1.0f, 1, 0, ""));
    noiseDefs.append(ruwa::core::brushes::sliderDef("texture.noiseRoughness",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Roughness"), 0.55f, 0.0f, 1.0f));

    QVector<ruwa::core::brushes::BrushSettingDef> perlinDefs;
    perlinDefs.append(ruwa::core::brushes::sliderDef("texture.perlinOctaves",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Octaves"), 2.0f, 1.0f, 5.0f, 1.0f, 1, 0, ""));
    perlinDefs.append(ruwa::core::brushes::sliderDef("texture.perlinPersistence",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Persistence"), 0.625f, 0.0f, 1.0f));

    QVector<ruwa::core::brushes::BrushSettingDef> dotsDefs;
    dotsDefs.append(ruwa::core::brushes::sliderDef("texture.dotsSpacing",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Point Spacing"), 28.0f, 6.0f, 96.0f, 1.0f, 1, 0,
        " px"));
    dotsDefs.append(ruwa::core::brushes::sliderDef("texture.dotsSize",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Point Size"), 0.3f, 0.05f, 0.9f));
    dotsDefs.append(ruwa::core::brushes::sliderDef("texture.dotsJitter",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Position Jitter"), 0.15f, 0.0f, 1.0f));

    QVector<ruwa::core::brushes::BrushSettingDef> linesDefs;
    linesDefs.append(ruwa::core::brushes::sliderDef("texture.linesSpacing",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Line Spacing"), 24.0f, 4.0f, 96.0f, 1.0f, 1, 0,
        " px"));
    linesDefs.append(ruwa::core::brushes::sliderDef("texture.linesThickness",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Line Thickness"), 0.22f, 0.02f, 0.95f));
    linesDefs.append(ruwa::core::brushes::sliderDef("texture.linesAngle",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Angle"), 45.0f, 0.0f, 180.0f, 1.0f, 1, 0,
        "\u00B0"));

    QVector<ruwa::core::brushes::BrushSettingDef> checkerDefs;
    checkerDefs.append(ruwa::core::brushes::sliderDef("texture.checkerSize",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Cell Size"), 24.0f, 4.0f, 96.0f, 1.0f, 1, 0,
        " px"));
    checkerDefs.append(ruwa::core::brushes::sliderDef("texture.checkerSoftness",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Edge Softness"), 0.06f, 0.0f, 0.45f, 0.01f, 100,
        0, "%"));
    checkerDefs.append(ruwa::core::brushes::sliderDef("texture.checkerRotation",
        QT_TRANSLATE_NOOP("ruwa::core::brushes", "Rotation"), 0.0f, 0.0f, 90.0f, 1.0f, 1, 0,
        "\u00B0"));

    for (const auto& defs :
        { pencilDefs, noiseDefs, perlinDefs, dotsDefs, linesDefs, checkerDefs }) {
        auto* settingsWidget
            = new widgets::BrushSettingsWidget(defs, m_typeSettingsContent, /*starMode=*/false);
        typeSettingsLayout->addWidget(settingsWidget);
        m_typeParameterSettingsWidgets.append(settingsWidget);
        connect(settingsWidget, &widgets::BrushSettingsWidget::settingChanged, this,
            [this, settingsWidget]() {
                settingsWidget->applyTo(m_settings);
                updatePreview();
            });
    }
    typeSettingsLayout->addStretch();
    m_settingsScrollArea->setWidget(m_typeSettingsContent);
    settingsLayout->addWidget(m_settingsScrollArea, 1);

    rootLayout->addWidget(m_settingsColumn, 2);

    connect(m_typeSelector, &widgets::ImageDropdownSelector::currentIndexChanged, this,
        [this](int index) {
            if (!m_typeSelector || index < 0) {
                return;
            }
            const int textureType = m_typeSelector->itemData(index).toInt();
            if (m_settings.textureType == textureType) {
                return;
            }
            m_settings.textureType = textureType;
            setActiveTextureType(m_settings.textureType);
            updatePreview();
            emit textureTypeChanged(textureType);
        });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateLayoutMetrics();
        updateSeparatorStyle();
        update();
    });

    updateLayoutMetrics();
    updateSeparatorStyle();
    setSettings(BrushSettingsData {});
}

void ProceduralTextureEditorWidget::setSettings(const BrushSettingsData& settings)
{
    m_settings = settings;
    if (m_typeSelector) {
        const QSignalBlocker blocker(m_typeSelector);
        const int index = m_typeSelector->findIndexByData(settings.textureType);
        if (index >= 0) {
            m_typeSelector->setCurrentIndex(index);
        }
    }
    for (auto* settingsWidget : m_typeParameterSettingsWidgets) {
        settingsWidget->setSettings(settings);
    }
    setActiveTextureType(settings.textureType);
    updatePreview();
}

void ProceduralTextureEditorWidget::setActiveTextureType(int textureType)
{
    if (m_typeParameterSettingsWidgets.isEmpty()) {
        return;
    }
    const int activeIndex
        = qBound(0, textureType, static_cast<int>(m_typeParameterSettingsWidgets.size()) - 1);
    for (int i = 0; i < m_typeParameterSettingsWidgets.size(); ++i) {
        m_typeParameterSettingsWidgets[i]->setVisible(i == activeIndex);
    }
    if (m_typeSettingsContent) {
        m_typeSettingsContent->adjustSize();
        m_typeSettingsContent->updateGeometry();
    }
    if (m_settingsScrollArea) {
        m_settingsScrollArea->refreshScrollGeometry();
    }
}

void ProceduralTextureEditorWidget::updateLayoutMetrics()
{
    const auto& theme = ThemeManager::instance();
    if (auto* rootLayout = qobject_cast<QHBoxLayout*>(layout())) {
        rootLayout->setContentsMargins(
            theme.scaled(8), theme.scaled(8), theme.scaled(8), theme.scaled(8));
        rootLayout->setSpacing(theme.scaled(8));
    }
    if (m_settingsColumn) {
        if (auto* settingsLayout = qobject_cast<QVBoxLayout*>(m_settingsColumn->layout())) {
            settingsLayout->setContentsMargins(0, 0, 0, 0);
            settingsLayout->setSpacing(theme.scaled(6));
        }
    }
    if (m_typeSelector) {
        m_typeSelector->setPopupCardSize(QSize(theme.scaled(112), theme.scaled(88)));
        m_typeSelector->setPopupMaxHeight(theme.scaled(300));
    }
    if (m_settingsScrollArea) {
        m_settingsScrollArea->setScrollBarMargin(theme.scaled(2));
        m_settingsScrollArea->refreshScrollGeometry();
    }
}

void ProceduralTextureEditorWidget::updateSeparatorStyle()
{
    if (!m_separator) {
        return;
    }
    const QColor color = WidgetStyleManager::instance().colors().borderSubtle();
    m_separator->setStyleSheet(
        QStringLiteral("background-color: %1; border: none;").arg(color.name(QColor::HexArgb)));
}

void ProceduralTextureEditorWidget::updatePreview()
{
    m_pendingPreviewSettings = m_settings;
    m_hasPendingPreviewRender = true;
    dispatchPreviewRender();
}

void ProceduralTextureEditorWidget::dispatchPreviewRender()
{
    if (!m_previewRenderWatcher || m_previewRenderInFlight || !m_hasPendingPreviewRender) {
        return;
    }

    const BrushSettingsData settings = m_pendingPreviewSettings;
    m_hasPendingPreviewRender = false;
    m_previewRenderInFlight = true;
    m_previewRenderWatcher->setFuture(
        QtConcurrent::run([settings]() { return generateTexturePreview(settings); }));
}

void ProceduralTextureEditorWidget::handlePreviewRenderFinished()
{
    if (!m_previewRenderWatcher) {
        return;
    }

    const QImage image = m_previewRenderWatcher->result();
    m_previewRenderInFlight = false;
    if (m_previewWidget && !image.isNull()) {
        m_previewWidget->setPreviewImage(image);
    }
    dispatchPreviewRender();
}

} // namespace ruwa::ui::windows
