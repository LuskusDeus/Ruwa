// SPDX-License-Identifier: MPL-2.0

#include "BrushManager.h"
#include "app/Application.h"
#include "features/brush/engine/BrushEngineRegistry.h"
#include "features/brush/engine/PixelBrushModule.h"
#include "features/brush/manager/AbrBrushImporter.h"
#include "BrushPreviewManager.h"
#include "BrushSettingDefs.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ruwa::core::brushes {

namespace {

constexpr int kMaxRecentBrushCount = 20;
constexpr quint32 kBrushFileVersion = 1;
constexpr char kBrushFileMagic[4] = { 'R', 'B', 'F', '\0' };

const PixelBrushModule& pixelModule()
{
    return static_cast<const PixelBrushModule&>(*BrushEngineRegistry::instance().pixelModule());
}

QVariantMap readEngineSettingsGroup(QSettings& settings)
{
    QVariantMap engineSettings;
    settings.beginGroup(QStringLiteral("engineSettings"));
    const QStringList keys = settings.allKeys();
    for (const QString& key : keys) {
        engineSettings.insert(key, settings.value(key));
    }
    settings.endGroup();
    return engineSettings;
}

void writeEngineSettingsGroup(QSettings& settings, const QVariantMap& engineSettings)
{
    settings.remove(QStringLiteral("engineSettings"));
    settings.beginGroup(QStringLiteral("engineSettings"));
    for (auto it = engineSettings.cbegin(); it != engineSettings.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
}

QVariantMap readLegacyPixelSettings(QSettings& settings)
{
    BrushSettingsData legacy;
    legacy.hardness = settings.value(QStringLiteral("hardness"), legacy.hardness).toFloat();
    legacy.spacing = settings.value(QStringLiteral("spacing"), legacy.spacing).toFloat();
    legacy.flow = settings.value(QStringLiteral("flow"), legacy.flow).toFloat();
    legacy.flowBlendMode
        = settings.value(QStringLiteral("flowBlendMode"), legacy.flowBlendMode).toInt();
    legacy.roundness = settings.value(QStringLiteral("roundness"), legacy.roundness).toFloat();
    legacy.angle = settings.value(QStringLiteral("angle"), legacy.angle).toFloat();
    legacy.sizePressureEnabled
        = settings.value(QStringLiteral("sizePressureEnabled"), legacy.sizePressureEnabled)
              .toBool();
    legacy.opacityPressureEnabled
        = settings.value(QStringLiteral("opacityPressureEnabled"), legacy.opacityPressureEnabled)
              .toBool();
    legacy.brushFeather
        = settings.value(QStringLiteral("brushFeather"), legacy.brushFeather).toBool();
    legacy.opacityPressureMin
        = settings.value(QStringLiteral("opacityPressureMin"), legacy.opacityPressureMin).toFloat();
    legacy.opacityPressureMax
        = settings.value(QStringLiteral("opacityPressureMax"), legacy.opacityPressureMax).toFloat();
    legacy.sizePressureMin
        = settings.value(QStringLiteral("sizePressureMin"), legacy.sizePressureMin).toFloat();
    legacy.sizePressureMax
        = settings.value(QStringLiteral("sizePressureMax"), legacy.sizePressureMax).toFloat();
    legacy.flowPressureMin
        = settings.value(QStringLiteral("flowPressureMin"), legacy.flowPressureMin).toFloat();
    legacy.flowPressureMax
        = settings.value(QStringLiteral("flowPressureMax"), legacy.flowPressureMax).toFloat();
    legacy.textureType = settings.value(QStringLiteral("textureType"), legacy.textureType).toInt();
    legacy.textureAmount
        = settings.value(QStringLiteral("textureAmount"), legacy.textureAmount).toFloat();
    legacy.textureScale
        = settings.value(QStringLiteral("textureScale"), legacy.textureScale).toFloat();
    legacy.textureContrast
        = settings.value(QStringLiteral("textureContrast"), legacy.textureContrast).toFloat();
    legacy.textureDepth
        = settings.value(QStringLiteral("textureDepth"), legacy.textureDepth).toFloat();
    legacy.textureBlend
        = settings.value(QStringLiteral("textureBlend"), legacy.textureBlend).toFloat();
    legacy.textureEdgeBoost
        = settings.value(QStringLiteral("textureEdgeBoost"), legacy.textureEdgeBoost).toFloat();
    legacy.dabType = settings.value(QStringLiteral("dabType"), legacy.dabType).toInt();
    legacy.postCorrection
        = settings.value(QStringLiteral("smoothing"), legacy.postCorrection).toFloat();
    legacy.stabilization
        = settings.value(QStringLiteral("stabilizer"), legacy.stabilization).toFloat();
    legacy.startTaper = settings.value(QStringLiteral("startTaper"), legacy.startTaper).toFloat();
    legacy.endTaper = settings.value(QStringLiteral("endTaper"), legacy.endTaper).toFloat();
    legacy.wetMix = settings.value(QStringLiteral("wetMix"), legacy.wetMix).toFloat();

    if (!settings.contains(QStringLiteral("opacityPressureMin"))
        && settings.contains(QStringLiteral("opacityPressure"))) {
        const float strength = std::clamp(
            settings.value(QStringLiteral("opacityPressure"), 1.0).toFloat(), 0.0f, 1.0f);
        legacy.opacityPressureMin = 1.0f - strength;
        legacy.opacityPressureMax = 1.0f;
    }
    if (!settings.contains(QStringLiteral("sizePressureMin"))
        && settings.contains(QStringLiteral("sizePressure"))) {
        const float strength
            = std::clamp(settings.value(QStringLiteral("sizePressure"), 1.0).toFloat(), 0.0f, 1.0f);
        legacy.sizePressureMin = 1.0f - strength;
        legacy.sizePressureMax = 1.0f;
    }
    if (!settings.contains(QStringLiteral("flowPressureMin"))
        && settings.contains(QStringLiteral("flowPressure"))) {
        const float strength
            = std::clamp(settings.value(QStringLiteral("flowPressure"), 0.0).toFloat(), 0.0f, 1.0f);
        legacy.flowPressureMin = 1.0f - strength;
        legacy.flowPressureMax = 1.0f;
    }

    return pixelModule().normalizeSettings(PixelBrushModule::settingsToVariantMap(legacy));
}

void syncCompatibilitySettings(BrushData& brush)
{
    if (brush.engineId == QLatin1String(kPixelBrushEngineId)) {
        brush.settings = PixelBrushModule::settingsFromVariantMap(brush.engineSettings);
    } else {
        brush.settings = {};
    }
}

void normalizeBrushData(BrushData& brush)
{
    if (brush.engineId.isEmpty()) {
        brush.engineId = QLatin1String(kPixelBrushEngineId);
    }

    if (const auto* module = BrushEngineRegistry::instance().module(brush.engineId)) {
        brush.engineSettings = module->upgradeSettings(brush.engineVersion, brush.engineSettings);
        brush.engineSettings = module->normalizeSettings(brush.engineSettings);
        brush.engineVersion = module->currentVersion();
    }

    syncCompatibilitySettings(brush);
}

void setError(QString* errorMessage, const QString& message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
}

QStringList canonicalStarredKeys(const QStringList& keys)
{
    QSet<QString> uniqueKeys;
    for (const QString& key : keys) {
        if (!key.isEmpty()) {
            uniqueKeys.insert(key);
        }
    }

    QStringList result(uniqueKeys.begin(), uniqueKeys.end());
    result.sort();
    return result;
}

QStringList canonicalStarredKeys(const QSet<QString>& keys)
{
    return canonicalStarredKeys(QStringList(keys.begin(), keys.end()));
}

// The custom dab tip is stored in live engine settings as an absolute
// filesystem path (dab.customImage). That path is used only to read the image
// while exporting; it must never be serialized into a portable pack. The image
// bytes are embedded and restored into an app-owned path on import.
QJsonObject embeddedDabImageObject(const QVariantMap& engineSettings)
{
    const QString path = engineSettings.value(QStringLiteral("dab.customImage")).toString();
    if (path.isEmpty()) {
        return {};
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        return {};
    }

    QJsonObject object;
    object.insert(QStringLiteral("fileName"), QFileInfo(path).fileName());
    object.insert(QStringLiteral("data"), QString::fromLatin1(bytes.toBase64()));
    return object;
}

// Decodes an embedded dab image and writes it into the app's brush-assets
// folder, returning the absolute path of the materialized file (or an empty
// string on failure). Each import gets its own folder so repeated imports never
// clobber each other.
QString materializeEmbeddedDabImage(const QJsonObject& dabImage)
{
    const QByteArray data
        = QByteArray::fromBase64(dabImage.value(QStringLiteral("data")).toString().toLatin1());
    if (data.isEmpty()) {
        return {};
    }

    QString fileName = QFileInfo(dabImage.value(QStringLiteral("fileName")).toString()).fileName();
    if (fileName.trimmed().isEmpty()) {
        fileName = QStringLiteral("dab.png");
    }

    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        return {};
    }
    const QString targetDir = QDir(appDataPath)
                                  .filePath(QStringLiteral("brush-assets/imported/%1")
                                          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(targetDir)) {
        return {};
    }

    const QString imagePath = QDir(targetDir).absoluteFilePath(fileName);
    QFile out(imagePath);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) {
        return {};
    }
    out.close();
    return QFileInfo(imagePath).absoluteFilePath();
}

QJsonObject brushToJsonObject(const BrushData& brush, const QStringList& starredKeys)
{
    const QJsonObject dabImage = embeddedDabImageObject(brush.engineSettings);
    QVariantMap portableEngineSettings = brush.engineSettings;
    const QString customImagePath
        = portableEngineSettings.value(QStringLiteral("dab.customImage")).toString();
    if (QDir::isAbsolutePath(customImagePath)) {
        portableEngineSettings.insert(QStringLiteral("dab.customImage"), QString());
    }

    QJsonObject object;
    object.insert(QStringLiteral("name"), brush.name);
    object.insert(QStringLiteral("iconPath"), brush.iconPath);
    object.insert(QStringLiteral("engineId"), brush.engineId);
    object.insert(QStringLiteral("engineVersion"), brush.engineVersion);
    object.insert(
        QStringLiteral("engineSettings"), QJsonObject::fromVariantMap(portableEngineSettings));

    if (!dabImage.isEmpty()) {
        object.insert(QStringLiteral("dabImage"), dabImage);
    }

    // Starred ("fav") settings are a per-brush UI preference kept outside
    // engineSettings; embed them so an imported pack keeps the author's pinned
    // controls instead of falling back to local defaults.
    object.insert(QStringLiteral("starred"), QJsonArray::fromStringList(starredKeys));
    return object;
}

bool readBrushesFromFile(
    const QString& filePath, QVector<BrushData>& brushes, QString* errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(errorMessage, QObject::tr("Cannot open brush file: %1").arg(file.errorString()));
        return false;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setVersion(QDataStream::Qt_6_0);

    char magic[4] = {};
    if (in.readRawData(magic, 4) != 4
        || std::memcmp(magic, kBrushFileMagic, sizeof(kBrushFileMagic)) != 0) {
        setError(errorMessage, QObject::tr("Not a Ruwa brush file."));
        return false;
    }

    quint32 version = 0;
    QByteArray payload;
    in >> version >> payload;
    if (in.status() != QDataStream::Ok) {
        setError(errorMessage, QObject::tr("Cannot read brush file."));
        return false;
    }
    if (version == 0 || version > kBrushFileVersion) {
        setError(errorMessage, QObject::tr("Unsupported brush file version."));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(errorMessage, QObject::tr("Corrupted brush file."));
        return false;
    }

    const QJsonArray array = document.object().value(QStringLiteral("brushes")).toArray();
    if (array.isEmpty()) {
        setError(errorMessage, QObject::tr("Brush file does not contain brushes."));
        return false;
    }

    QVector<BrushData> parsedBrushes;
    parsedBrushes.reserve(array.size());
    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object = value.toObject();
        BrushData brush;
        brush.name = object.value(QStringLiteral("name")).toString(QObject::tr("Brush"));
        brush.iconPath = object.value(QStringLiteral("iconPath")).toString();
        brush.engineId
            = object.value(QStringLiteral("engineId")).toString(QLatin1String(kPixelBrushEngineId));
        brush.engineVersion = object.value(QStringLiteral("engineVersion")).toInt(1);
        brush.engineSettings
            = object.value(QStringLiteral("engineSettings")).toObject().toVariantMap();
        if (brush.engineSettings.isEmpty()
            && brush.engineId == QLatin1String(kPixelBrushEngineId)) {
            brush.engineSettings = pixelModule().defaultSettings();
        }

        // Restore the embedded dab tip (if any) into a local asset file and
        // repoint dab.customImage at it, so the texture survives the trip to a
        // machine that never had the original file.
        const QJsonObject dabImage = object.value(QStringLiteral("dabImage")).toObject();
        if (!dabImage.isEmpty()) {
            const QString materialized = materializeEmbeddedDabImage(dabImage);
            if (!materialized.isEmpty()) {
                brush.engineSettings.insert(QStringLiteral("dab.customImage"), materialized);
            }
        }

        // Keep the author's starred ("fav") setting keys on the brush data so
        // pack and single-brush imports follow the exact same path.
        const QJsonValue starredValue = object.value(QStringLiteral("starred"));
        if (starredValue.isArray()) {
            brush.hasStarredKeys = true;
            const QJsonArray starredArray = starredValue.toArray();
            brush.starredKeys.reserve(starredArray.size());
            for (const QJsonValue& key : starredArray) {
                const QString keyStr = key.toString();
                if (!keyStr.isEmpty()) {
                    brush.starredKeys.append(keyStr);
                }
            }
            brush.starredKeys = canonicalStarredKeys(brush.starredKeys);
        }

        normalizeBrushData(brush);
        parsedBrushes.append(brush);
    }

    if (parsedBrushes.isEmpty()) {
        setError(errorMessage, QObject::tr("Brush file does not contain valid brushes."));
        return false;
    }

    brushes = parsedBrushes;
    return true;
}

bool readBrushesForImport(
    const QString& filePath, QVector<BrushData>& brushes, QString* errorMessage)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QLatin1String("rbf")) {
        return readBrushesFromFile(filePath, brushes, errorMessage);
    }
    if (suffix != QLatin1String("abr")) {
        setError(errorMessage, QObject::tr("Unsupported brush file extension: .%1").arg(suffix));
        return false;
    }

    QVector<AbrImportedTip> tips;
    if (!importAbrBrushTips(filePath, tips, errorMessage)) {
        return false;
    }

    QVector<BrushData> importedBrushes;
    importedBrushes.reserve(tips.size());
    for (const AbrImportedTip& tip : tips) {
        BrushData brush;
        brush.name = tip.name.trimmed().isEmpty() ? QObject::tr("Brush") : tip.name.trimmed();
        brush.engineId = QLatin1String(kPixelBrushEngineId);
        brush.engineVersion = pixelModule().currentVersion();
        QVariantMap settings = pixelModule().defaultSettings();
        for (auto it = tip.settings.cbegin(); it != tip.settings.cend(); ++it) {
            settings.insert(it.key(), it.value());
        }
        settings.insert(QStringLiteral("dab.customImage"), tip.imagePath);
        settings.insert(QStringLiteral("dab.threshold"), 0.0f);
        settings.insert(QStringLiteral("dab.compression"), 1.0f);
        brush.engineSettings = pixelModule().normalizeSettings(settings);
        normalizeBrushData(brush);
        importedBrushes.append(std::move(brush));
    }

    if (importedBrushes.isEmpty()) {
        setError(errorMessage, QObject::tr("ABR file does not contain supported brush tips."));
        return false;
    }

    brushes = std::move(importedBrushes);
    return true;
}

void writeBrushPacksToSettings(const QVector<BrushPresetData>& presets,
    const QHash<QString, QVector<BrushData>>& brushesByPreset)
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.remove(QStringLiteral("BrushPacks"));

    settings.beginWriteArray(QStringLiteral("BrushPacks"), presets.size());
    for (int i = 0; i < presets.size(); ++i) {
        settings.setArrayIndex(i);
        const auto& preset = presets[i];
        settings.setValue(QStringLiteral("id"), preset.id);
        settings.setValue(QStringLiteral("name"), preset.name);
        settings.setValue(QStringLiteral("iconPath"), preset.iconPath);

        const auto brushes = brushesByPreset.value(preset.id);
        settings.beginWriteArray(QStringLiteral("brushes"), brushes.size());
        for (int j = 0; j < brushes.size(); ++j) {
            settings.setArrayIndex(j);
            settings.setValue(QStringLiteral("id"), brushes[j].id);
            settings.setValue(QStringLiteral("name"), brushes[j].name);
            settings.setValue(QStringLiteral("iconPath"), brushes[j].iconPath);
            settings.setValue(QStringLiteral("engineId"), brushes[j].engineId);
            settings.setValue(QStringLiteral("engineVersion"), brushes[j].engineVersion);
            if (brushes[j].hasStarredKeys) {
                settings.setValue(
                    QStringLiteral("starred"), canonicalStarredKeys(brushes[j].starredKeys));
            }
            writeEngineSettingsGroup(settings, brushes[j].engineSettings);
        }
        settings.endArray();
    }
    settings.endArray();
    settings.sync();
}

} // namespace

BrushManager& BrushManager::instance()
{
    static BrushManager manager;
    return manager;
}

BrushManager::BrushManager()
{
    m_deferredSaveTimer.setSingleShot(true);
    m_deferredSaveTimer.setInterval(140);
    connect(&m_deferredSaveTimer, &QTimer::timeout, this, &BrushManager::flushDeferredSave);
    if (auto* app = QCoreApplication::instance()) {
        connect(app, &QCoreApplication::aboutToQuit, this, &BrushManager::flushDeferredSave,
            Qt::DirectConnection);
    }
}

const QVector<BrushPresetData>& BrushManager::presets()
{
    ensureLoaded();
    return m_presets;
}

QVector<BrushData> BrushManager::brushesForPreset(const QString& presetId)
{
    ensureLoaded();
    return m_brushesByPreset.value(presetId);
}

QString BrushManager::createPreset()
{
    ensureLoaded();

    BrushPresetData preset;
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = QObject::tr("New Pack %1").arg(m_presets.size() + 1);
    m_presets.append(preset);
    m_brushesByPreset[preset.id] = {};
    save();
    emit presetCreated(preset.id);
    return preset.id;
}

bool BrushManager::removePreset(const QString& presetId)
{
    ensureLoaded();
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == presetId) {
            bool starredSettingsChanged = false;
            const auto& removedBrushes = m_brushesByPreset[presetId];
            for (const BrushData& brush : removedBrushes) {
                starredSettingsChanged
                    = m_starredSettingsByBrush.remove(brush.id) > 0 || starredSettingsChanged;
            }
            m_presets.removeAt(i);
            m_brushesByPreset.remove(presetId);
            if (starredSettingsChanged) {
                saveStarredSettings();
            }
            save();
            emit presetRemoved(presetId);
            return true;
        }
    }
    return false;
}

bool BrushManager::renamePreset(const QString& presetId, const QString& newName)
{
    ensureLoaded();
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty())
        return false;

    for (auto& preset : m_presets) {
        if (preset.id == presetId) {
            if (preset.name == trimmed)
                return true;
            preset.name = trimmed;
            save();
            emit presetRenamed(presetId, trimmed);
            return true;
        }
    }
    return false;
}

QString BrushManager::createBrush(const QString& presetId)
{
    ensureLoaded();
    if (presetId.isEmpty())
        return {};

    BrushData brush;
    brush.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    brush.presetId = presetId;
    brush.name = QObject::tr("New Brush");
    brush.engineId = QLatin1String(kPixelBrushEngineId);
    brush.engineVersion = pixelModule().currentVersion();
    brush.engineSettings = pixelModule().defaultSettings();
    brush.starredKeys
        = canonicalStarredKeys(defaultStarredKeys(pixelModule().descriptor().settingsTabs));
    brush.hasStarredKeys = true;
    syncCompatibilitySettings(brush);
    m_brushesByPreset[presetId].append(brush);
    m_starredSettingsByBrush.insert(
        brush.id, QSet<QString>(brush.starredKeys.begin(), brush.starredKeys.end()));
    save();
    emit brushCreated(presetId, brush.id);
    return brush.id;
}

bool BrushManager::removeBrush(const QString& brushId)
{
    ensureLoaded();
    for (auto it = m_brushesByPreset.begin(); it != m_brushesByPreset.end(); ++it) {
        auto& brushes = it.value();
        for (int i = 0; i < brushes.size(); ++i) {
            if (brushes[i].id == brushId) {
                const QString presetId = it.key();
                brushes.removeAt(i);
                if (m_recentBrushesLoaded) {
                    m_recentBrushIds.removeAll(brushId);
                    saveRecentBrushes();
                }
                if (m_starredLoaded) {
                    m_starredSettingsByBrush.remove(brushId);
                    saveStarredSettings();
                }
                save();
                emit brushRemoved(presetId, brushId);
                return true;
            }
        }
    }
    return false;
}

bool BrushManager::renameBrush(const QString& brushId, const QString& newName)
{
    ensureLoaded();
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty())
        return false;

    for (auto it = m_brushesByPreset.begin(); it != m_brushesByPreset.end(); ++it) {
        auto& brushes = it.value();
        for (auto& brush : brushes) {
            if (brush.id == brushId) {
                if (brush.name == trimmed)
                    return true;
                brush.name = trimmed;
                // Renaming happens character-by-character as the user types, so
                // debounce the disk write instead of doing a blocking save on
                // every keystroke (mirrors updateBrushSettings()).
                scheduleDeferredSave();
                emit brushRenamed(brushId, trimmed);
                return true;
            }
        }
    }
    return false;
}

bool BrushManager::updateBrushSettings(const QString& brushId, const BrushSettingsData& settings)
{
    ensureLoaded();
    const BrushSettingsData normalized = PixelBrushModule::normalizeCompatibilitySettings(settings);
    const QVariantMap normalizedEngineSettings = PixelBrushModule::settingsToVariantMap(normalized);
    const QString normalizedEngineId = QLatin1String(kPixelBrushEngineId);
    const int normalizedEngineVersion = pixelModule().currentVersion();

    for (auto it = m_brushesByPreset.begin(); it != m_brushesByPreset.end(); ++it) {
        auto& brushes = it.value();
        for (auto& brush : brushes) {
            if (brush.id == brushId) {
                if (brush.engineId == normalizedEngineId
                    && brush.engineVersion == normalizedEngineVersion
                    && brush.engineSettings == normalizedEngineSettings) {
                    return true;
                }

                brush.engineId = normalizedEngineId;
                brush.engineVersion = normalizedEngineVersion;
                brush.engineSettings = normalizedEngineSettings;
                brush.settings = normalized;
                scheduleDeferredSave();
                emit brushSettingsUpdated(it.key(), brushId, normalized);
                return true;
            }
        }
    }
    return false;
}

bool BrushManager::exportBrushesToFile(const QString& filePath, const QVector<BrushData>& brushes,
    const QString& packName, QString* errorMessage) const
{
    if (filePath.trimmed().isEmpty()) {
        setError(errorMessage, QObject::tr("Brush file path is empty."));
        return false;
    }
    if (brushes.isEmpty()) {
        setError(errorMessage, QObject::tr("There are no brushes to export."));
        return false;
    }

    QJsonArray array;
    for (const BrushData& brush : brushes) {
        BrushData normalizedBrush = brush;
        normalizeBrushData(normalizedBrush);
        const QStringList starredKeys = normalizedBrush.hasStarredKeys
            ? canonicalStarredKeys(normalizedBrush.starredKeys)
            : canonicalStarredKeys(BrushManager::instance().starredSettings(brush.id));
        array.append(brushToJsonObject(normalizedBrush, starredKeys));
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("ruwa.brushes"));
    root.insert(QStringLiteral("version"), static_cast<int>(kBrushFileVersion));
    root.insert(QStringLiteral("packName"), packName);
    root.insert(QStringLiteral("brushes"), array);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(errorMessage, QObject::tr("Cannot write brush file: %1").arg(file.errorString()));
        return false;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_6_0);
    out.writeRawData(kBrushFileMagic, sizeof(kBrushFileMagic));
    out << kBrushFileVersion << QJsonDocument(root).toJson(QJsonDocument::Compact);

    if (out.status() != QDataStream::Ok || !file.commit()) {
        setError(errorMessage,
            QObject::tr("Cannot finish writing brush file: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

BrushImportResult BrushManager::readBrushFileForImport(const QString& filePath)
{
    BrushImportResult result;
    result.success = readBrushesForImport(filePath, result.brushes, &result.errorMessage);
    return result;
}

bool BrushManager::importBrushesIntoPreset(const QString& filePath, const QString& presetId,
    QStringList* importedBrushIds, QString* errorMessage)
{
    QVector<BrushData> brushes;
    if (!readBrushesForImport(filePath, brushes, errorMessage)) {
        return false;
    }

    return importBrushesIntoPreset(brushes, presetId, importedBrushIds, errorMessage);
}

bool BrushManager::importBrushesIntoPreset(const QVector<BrushData>& brushes,
    const QString& presetId, QStringList* importedBrushIds, QString* errorMessage)
{
    ensureLoaded();
    if (!m_brushesByPreset.contains(presetId)) {
        setError(errorMessage, QObject::tr("Target brush pack does not exist."));
        return false;
    }
    if (brushes.isEmpty()) {
        setError(errorMessage, QObject::tr("Brush file does not contain valid brushes."));
        return false;
    }

    QStringList ids;
    QStringList starredRestoredIds;
    auto& targetBrushes = m_brushesByPreset[presetId];
    const QStringList defaultStarred
        = canonicalStarredKeys(defaultStarredKeys(pixelModule().descriptor().settingsTabs));
    for (const BrushData& sourceBrush : brushes) {
        BrushData brush = sourceBrush;
        brush.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        brush.presetId = presetId;
        if (brush.name.trimmed().isEmpty()) {
            brush.name = QObject::tr("Brush");
        }
        if (!brush.hasStarredKeys) {
            brush.starredKeys = defaultStarred;
            brush.hasStarredKeys = true;
        }
        normalizeBrushData(brush);
        if (stageStarredSettings(brush.id, brush)) {
            starredRestoredIds.append(brush.id);
        }
        ids.append(brush.id);
        targetBrushes.append(brush);
    }

    if (!starredRestoredIds.isEmpty()) {
        saveStarredSettings();
        for (const QString& brushId : starredRestoredIds) {
            emit starredSettingsChanged(brushId);
        }
    }

    saveAsync();
    if (importedBrushIds) {
        *importedBrushIds = ids;
    }
    for (const QString& brushId : ids) {
        emit brushCreated(presetId, brushId);
    }
    return true;
}

QString BrushManager::importBrushFileAsPreset(const QString& filePath, const QString& presetName,
    QStringList* importedBrushIds, QString* errorMessage)
{
    QVector<BrushData> brushes;
    if (!readBrushesForImport(filePath, brushes, errorMessage)) {
        return {};
    }

    return importBrushesAsPreset(brushes, presetName, importedBrushIds, errorMessage);
}

QString BrushManager::importBrushesAsPreset(const QVector<BrushData>& brushes,
    const QString& presetName, QStringList* importedBrushIds, QString* errorMessage)
{
    ensureLoaded();
    if (brushes.isEmpty()) {
        setError(errorMessage, QObject::tr("Brush file does not contain valid brushes."));
        return {};
    }

    BrushPresetData preset;
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name
        = presetName.trimmed().isEmpty() ? QObject::tr("Imported Brushes") : presetName.trimmed();

    QStringList ids;
    QStringList starredRestoredIds;
    QVector<BrushData> importedBrushes;
    importedBrushes.reserve(brushes.size());
    const QStringList defaultStarred
        = canonicalStarredKeys(defaultStarredKeys(pixelModule().descriptor().settingsTabs));
    for (const BrushData& sourceBrush : brushes) {
        BrushData brush = sourceBrush;
        brush.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        brush.presetId = preset.id;
        if (brush.name.trimmed().isEmpty()) {
            brush.name = QObject::tr("Brush");
        }
        if (!brush.hasStarredKeys) {
            brush.starredKeys = defaultStarred;
            brush.hasStarredKeys = true;
        }
        normalizeBrushData(brush);
        if (stageStarredSettings(brush.id, brush)) {
            starredRestoredIds.append(brush.id);
        }
        ids.append(brush.id);
        importedBrushes.append(brush);
    }

    m_presets.append(preset);
    m_brushesByPreset.insert(preset.id, importedBrushes);

    if (!starredRestoredIds.isEmpty()) {
        saveStarredSettings();
        for (const QString& brushId : starredRestoredIds) {
            emit starredSettingsChanged(brushId);
        }
    }

    saveAsync();

    if (importedBrushIds) {
        *importedBrushIds = ids;
    }
    emit presetCreated(preset.id);
    for (const QString& brushId : ids) {
        emit brushCreated(preset.id, brushId);
    }
    return preset.id;
}

std::optional<BrushData> BrushManager::brushData(const QString& brushId)
{
    ensureLoaded();
    if (brushId.isEmpty()) {
        return std::nullopt;
    }

    for (auto it = m_brushesByPreset.cbegin(); it != m_brushesByPreset.cend(); ++it) {
        const auto& brushes = it.value();
        for (const auto& brush : brushes) {
            if (brush.id == brushId) {
                return brush;
            }
        }
    }

    return std::nullopt;
}

std::optional<BrushSettingsData> BrushManager::brushSettings(const QString& brushId)
{
    if (const auto brush = brushData(brushId)) {
        if (brush->engineId != QLatin1String(kPixelBrushEngineId)) {
            return std::nullopt;
        }
        return PixelBrushModule::settingsFromVariantMap(brush->engineSettings);
    }
    return std::nullopt;
}

QVector<BrushData> BrushManager::recentBrushes(int limit)
{
    ensureLoaded();
    ensureRecentBrushesLoaded();

    QVector<QString> prunedIds;
    prunedIds.reserve(m_recentBrushIds.size());

    QVector<BrushData> result;
    if (limit > 0) {
        result.reserve(qMin(limit, m_recentBrushIds.size()));
    }

    for (const QString& brushId : m_recentBrushIds) {
        if (const auto brush = brushData(brushId)) {
            prunedIds.append(brushId);
            if (limit <= 0 || result.size() < limit) {
                result.append(*brush);
            }
        }
    }

    if (prunedIds != m_recentBrushIds) {
        m_recentBrushIds = prunedIds;
        saveRecentBrushes();
    }

    return result;
}

void BrushManager::markBrushAsRecentlyUsed(const QString& brushId)
{
    ensureLoaded();
    ensureRecentBrushesLoaded();

    if (!brushData(brushId).has_value()) {
        return;
    }

    m_recentBrushIds.removeAll(brushId);
    m_recentBrushIds.prepend(brushId);
    if (m_recentBrushIds.size() > kMaxRecentBrushCount) {
        m_recentBrushIds.resize(kMaxRecentBrushCount);
    }
    saveRecentBrushes();
}

QString BrushManager::presetIdForBrush(const QString& brushId)
{
    ensureLoaded();
    if (brushId.isEmpty())
        return {};
    for (auto it = m_brushesByPreset.begin(); it != m_brushesByPreset.end(); ++it) {
        const auto& brushes = it.value();
        for (const auto& brush : brushes) {
            if (brush.id == brushId) {
                return it.key();
            }
        }
    }
    return {};
}

// =========================================================================
//  Starred settings (UI preference, per brush)
// =========================================================================

QSet<QString> BrushManager::starredSettings(const QString& brushId)
{
    const auto tabs = pixelModule().descriptor().settingsTabs;
    if (brushId.isEmpty()) {
        return defaultStarredKeys(tabs);
    }

    ensureLoaded();
    if (!m_starredLoaded) {
        loadStarredSettings();
    }
    if (m_starredSettingsByBrush.contains(brushId)) {
        return m_starredSettingsByBrush.value(brushId);
    }

    for (auto it = m_brushesByPreset.cbegin(); it != m_brushesByPreset.cend(); ++it) {
        for (const BrushData& brush : it.value()) {
            if (brush.id == brushId && brush.hasStarredKeys) {
                const QSet<QString> keys(brush.starredKeys.begin(), brush.starredKeys.end());
                m_starredSettingsByBrush.insert(brushId, keys);
                return keys;
            }
        }
    }

    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    if (settings.contains(QStringLiteral("StarredBrushSettings"))) {
        const QStringList list
            = settings.value(QStringLiteral("StarredBrushSettings")).toStringList();
        return QSet<QString>(list.begin(), list.end());
    }
    return defaultStarredKeys(tabs);
}

bool BrushManager::isSettingStarred(const QString& brushId, const QString& key)
{
    return starredSettings(brushId).contains(key);
}

void BrushManager::setSettingStarred(const QString& brushId, const QString& key, bool starred)
{
    if (brushId.isEmpty()) {
        return;
    }
    if (!m_starredLoaded) {
        loadStarredSettings();
    }

    QSet<QString> starredKeys = starredSettings(brushId);
    const bool changed = starred ? !starredKeys.contains(key) : starredKeys.contains(key);
    if (!changed) {
        return;
    }

    if (starred) {
        starredKeys.insert(key);
    } else {
        starredKeys.remove(key);
    }
    m_starredSettingsByBrush.insert(brushId, starredKeys);

    bool brushUpdated = false;
    for (auto it = m_brushesByPreset.begin();
         it != m_brushesByPreset.end() && !brushUpdated; ++it) {
        for (BrushData& brush : it.value()) {
            if (brush.id == brushId) {
                brush.starredKeys = canonicalStarredKeys(starredKeys);
                brush.hasStarredKeys = true;
                brushUpdated = true;
                break;
            }
        }
    }

    saveStarredSettings();
    scheduleDeferredSave();
    emit starredSettingsChanged(brushId);
}

void BrushManager::loadStarredSettings()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    m_starredSettingsByBrush.clear();

    settings.beginGroup(QStringLiteral("StarredBrushSettingsByBrush"));
    const QStringList brushIds = settings.childGroups();
    for (const QString& brushId : brushIds) {
        settings.beginGroup(brushId);
        const QStringList keys = settings.value(QStringLiteral("keys")).toStringList();
        m_starredSettingsByBrush.insert(brushId, QSet<QString>(keys.begin(), keys.end()));
        settings.endGroup();
    }
    settings.endGroup();
    m_starredLoaded = true;
}

void BrushManager::saveStarredSettings() const
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    settings.remove(QStringLiteral("StarredBrushSettingsByBrush"));
    settings.beginGroup(QStringLiteral("StarredBrushSettingsByBrush"));
    for (auto it = m_starredSettingsByBrush.cbegin(); it != m_starredSettingsByBrush.cend(); ++it) {
        settings.beginGroup(it.key());
        const QStringList keys(it.value().begin(), it.value().end());
        settings.setValue(QStringLiteral("keys"), keys);
        settings.endGroup();
    }
    settings.endGroup();
    settings.sync();
}

bool BrushManager::stageStarredSettings(const QString& brushId, const BrushData& source)
{
    if (!source.hasStarredKeys || brushId.isEmpty()) {
        return false;
    }
    if (!m_starredLoaded) {
        loadStarredSettings();
    }
    const QStringList keys = canonicalStarredKeys(source.starredKeys);
    m_starredSettingsByBrush.insert(brushId, QSet<QString>(keys.begin(), keys.end()));
    return true;
}

void BrushManager::ensureRecentBrushesLoaded()
{
    if (!m_recentBrushesLoaded) {
        loadRecentBrushes();
    }
}

void BrushManager::loadRecentBrushes()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    m_recentBrushIds.clear();
    settings.beginGroup(QStringLiteral("RecentBrushes"));
    const QStringList storedIds = settings.value(QStringLiteral("ids")).toStringList();
    settings.endGroup();

    for (const QString& brushId : storedIds) {
        if (brushId.isEmpty() || m_recentBrushIds.contains(brushId)) {
            continue;
        }
        m_recentBrushIds.append(brushId);
        if (m_recentBrushIds.size() >= kMaxRecentBrushCount) {
            break;
        }
    }

    m_recentBrushesLoaded = true;
}

void BrushManager::saveRecentBrushes() const
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    QStringList ids;
    ids.reserve(m_recentBrushIds.size());
    for (const QString& brushId : m_recentBrushIds) {
        ids.append(brushId);
    }
    settings.beginGroup(QStringLiteral("RecentBrushes"));
    settings.setValue(QStringLiteral("ids"), ids);
    settings.endGroup();
    settings.sync();
}

// =========================================================================

void BrushManager::ensureLoaded()
{
    if (!m_loaded) {
        load();
    }
}

void BrushManager::load()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());

    m_presets.clear();
    m_brushesByPreset.clear();

    const int packCount = settings.beginReadArray(QStringLiteral("BrushPacks"));
    for (int i = 0; i < packCount; ++i) {
        settings.setArrayIndex(i);

        BrushPresetData preset;
        preset.id = settings.value(QStringLiteral("id")).toString();
        preset.name = settings.value(QStringLiteral("name")).toString();
        preset.iconPath = settings.value(QStringLiteral("iconPath")).toString();
        if (preset.id.isEmpty()) {
            preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        if (preset.name.isEmpty()) {
            preset.name = QObject::tr("Pack");
        }

        QVector<BrushData> brushes;
        const int brushCount = settings.beginReadArray(QStringLiteral("brushes"));
        for (int j = 0; j < brushCount; ++j) {
            settings.setArrayIndex(j);
            BrushData brush;
            brush.id = settings.value(QStringLiteral("id")).toString();
            brush.name = settings.value(QStringLiteral("name")).toString();
            brush.iconPath = settings.value(QStringLiteral("iconPath")).toString();
            brush.presetId = preset.id;
            brush.engineId = settings.value(QStringLiteral("engineId")).toString();
            if (brush.engineId.isEmpty()) {
                brush.engineId = QLatin1String(kPixelBrushEngineId);
                brush.engineVersion = 1;
                brush.engineSettings = readLegacyPixelSettings(settings);
            } else {
                brush.engineVersion = settings.value(QStringLiteral("engineVersion"), 1).toInt();
                brush.engineSettings = readEngineSettingsGroup(settings);
            }
            if (settings.contains(QStringLiteral("starred"))) {
                brush.starredKeys = canonicalStarredKeys(
                    settings.value(QStringLiteral("starred")).toStringList());
                brush.hasStarredKeys = true;
            }
            normalizeBrushData(brush);

            if (brush.id.isEmpty()) {
                brush.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            if (brush.name.isEmpty()) {
                brush.name = QObject::tr("Brush");
            }
            brushes.append(brush);
        }
        settings.endArray();

        m_presets.append(preset);
        m_brushesByPreset.insert(preset.id, brushes);
    }
    settings.endArray();

    loadStarredSettings();
    QSet<QString> fallbackStarred;
    if (settings.contains(QStringLiteral("StarredBrushSettings"))) {
        const QStringList legacyKeys
            = settings.value(QStringLiteral("StarredBrushSettings")).toStringList();
        fallbackStarred = QSet<QString>(legacyKeys.begin(), legacyKeys.end());
    } else {
        fallbackStarred = defaultStarredKeys(pixelModule().descriptor().settingsTabs);
    }

    bool brushStorageNeedsStarredMigration = false;
    for (auto it = m_brushesByPreset.begin(); it != m_brushesByPreset.end(); ++it) {
        for (BrushData& brush : it.value()) {
            QSet<QString> keys;
            if (brush.hasStarredKeys) {
                brush.starredKeys = canonicalStarredKeys(brush.starredKeys);
                keys = QSet<QString>(brush.starredKeys.begin(), brush.starredKeys.end());
            } else if (m_starredSettingsByBrush.contains(brush.id)) {
                brushStorageNeedsStarredMigration = true;
                keys = m_starredSettingsByBrush.value(brush.id);
                brush.starredKeys = canonicalStarredKeys(keys);
                brush.hasStarredKeys = true;
            } else {
                brushStorageNeedsStarredMigration = true;
                keys = fallbackStarred;
                brush.starredKeys = canonicalStarredKeys(keys);
                brush.hasStarredKeys = true;
            }
            m_starredSettingsByBrush.insert(brush.id, keys);
        }
    }

    if (m_presets.isEmpty()) {
        loadDefaults();
        save();
    } else if (brushStorageNeedsStarredMigration) {
        save();
    }

    m_loaded = true;
}

void BrushManager::save() const
{
    if (ruwa::Application::isFactoryResetRestartInProgress()) {
        return;
    }

    waitForAsyncSave();
    writeBrushPacksToSettings(m_presets, m_brushesByPreset);
}

void BrushManager::saveAsync() const
{
    if (ruwa::Application::isFactoryResetRestartInProgress()) {
        return;
    }

    waitForAsyncSave();
    const QVector<BrushPresetData> presets = m_presets;
    const QHash<QString, QVector<BrushData>> brushesByPreset = m_brushesByPreset;
    m_asyncSaveFuture = QtConcurrent::run(
        [presets, brushesByPreset]() { writeBrushPacksToSettings(presets, brushesByPreset); });
}

void BrushManager::waitForAsyncSave() const
{
    if (m_asyncSaveFuture.isRunning()) {
        m_asyncSaveFuture.waitForFinished();
    }
}

void BrushManager::scheduleDeferredSave()
{
    m_deferredSavePending = true;
    m_deferredSaveTimer.start();
}

void BrushManager::flushDeferredSave()
{
    if (ruwa::Application::isFactoryResetRestartInProgress()) {
        m_deferredSavePending = false;
        if (m_deferredSaveTimer.isActive()) {
            m_deferredSaveTimer.stop();
        }
        return;
    }

    waitForAsyncSave();
    if (!m_deferredSavePending) {
        return;
    }
    if (m_deferredSaveTimer.isActive()) {
        m_deferredSaveTimer.stop();
    }
    m_deferredSavePending = false;
    save();
}

void BrushManager::resetToDefaults()
{
    ensureLoaded();

    m_presets.clear();
    m_brushesByPreset.clear();
    m_starredSettingsByBrush.clear();
    m_recentBrushIds.clear();
    saveStarredSettings();
    saveRecentBrushes();

    loadDefaults();
    flushDeferredSave();
    save();

    BrushPreviewManager::instance().invalidateCache();
    emit dataReset();
}

void BrushManager::loadDefaults()
{
    // Factory brush packs are data-driven: they ship as .rbf files embedded in
    // the application resources (see resources.qrc, prefix "/standard_brushes").
    // Every sub-directory under the root becomes one brush pack, and each .rbf
    // file inside it contributes its brushes. This keeps the standard presets
    // out of C++ so the pack can be edited just by re-exporting the .rbf files.
    QDir rootDir(QStringLiteral(":/standard_brushes"));

    // Packs are listed alphabetically, except a few well-known packs are pinned
    // to the front so the very first brush (used as the default active brush)
    // stays the familiar "Basic Brush" from the Classic pack.
    static const QStringList kPreferredOrder = {
        QStringLiteral("Classic Brushes"),
    };

    QStringList packDirs
        = rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    std::stable_sort(packDirs.begin(), packDirs.end(), [](const QString& a, const QString& b) {
        const int ia = kPreferredOrder.indexOf(a);
        const int ib = kPreferredOrder.indexOf(b);
        if (ia == ib) {
            return false; // keep the alphabetical order from entryList()
        }
        if (ia == -1)
            return false; // a is not preferred -> after b
        if (ib == -1)
            return true; // b is not preferred -> a first
        return ia < ib;
    });

    for (const QString& packName : packDirs) {
        QDir packDir(rootDir.filePath(packName));
        const QStringList files = packDir.entryList(
            { QStringLiteral("*.rbf") }, QDir::Files, QDir::Name | QDir::IgnoreCase);

        QVector<BrushData> brushes;
        for (const QString& fileName : files) {
            const QString filePath = packDir.filePath(fileName);
            QVector<BrushData> parsed;
            QString error;
            if (!readBrushesForImport(filePath, parsed, &error)) {
                qWarning() << "BrushManager: failed to load default brush" << filePath << "-"
                           << error;
                continue;
            }
            for (BrushData& brush : parsed) {
                brush.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                if (brush.name.trimmed().isEmpty()) {
                    brush.name = QFileInfo(fileName).completeBaseName();
                }
                brushes.append(std::move(brush));
            }
        }

        if (brushes.isEmpty()) {
            continue;
        }

        BrushPresetData preset;
        preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        preset.name = packName;
        for (BrushData& brush : brushes) {
            brush.presetId = preset.id;
        }
        m_presets.append(preset);
        m_brushesByPreset.insert(preset.id, brushes);
    }

    // Safety net: never leave the user with zero packs (e.g. if the resource
    // bundle is missing). An empty starter pack keeps the UI functional.
    if (m_presets.isEmpty()) {
        BrushPresetData preset;
        preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        preset.name = QObject::tr("Brushes");
        m_presets.append(preset);
        m_brushesByPreset.insert(preset.id, {});
    }
}

} // namespace ruwa::core::brushes
