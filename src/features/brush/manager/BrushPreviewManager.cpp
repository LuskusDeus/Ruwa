// SPDX-License-Identifier: MPL-2.0

// BrushPreviewManager.cpp
#include "BrushPreviewManager.h"

#include "features/brush/engine/BrushEngineRegistry.h"
#include "features/brush/engine/PixelBrushModule.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QtConcurrent>

namespace ruwa::core::brushes {

namespace {

// Disk cache lives under the OS cache location (persists across normal launches;
// only wiped on factory reset). Keeps a hard cap on the number of files so a
// huge imported pack rendered across many themes/sizes can't grow unbounded.
constexpr int kMaxDiskPreviewFiles = 4000;

QString brushPreviewCacheDir()
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (baseDir.isEmpty()) {
        baseDir = QDir::tempPath() + QStringLiteral("/ruwa-cache");
    }
    const QString dir = QDir::cleanPath(baseDir + QStringLiteral("/brush-previews"));
    QDir().mkpath(dir);
    return dir;
}

QString brushPreviewDiskPath(const QString& key)
{
    const QByteArray hash = QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256);
    const QString hex = QString::fromLatin1(hash.toHex());
    return brushPreviewCacheDir() + QLatin1Char('/') + hex + QStringLiteral(".png");
}

QImage loadPreviewFromDisk(const QString& key)
{
    const QString path = brushPreviewDiskPath(key);
    if (!QFileInfo::exists(path)) {
        return {};
    }
    QImage image;
    if (!image.load(path, "PNG")) {
        return {};
    }
    return image;
}

void savePreviewToDisk(const QString& key, const QImage& image)
{
    if (key.isEmpty() || image.isNull()) {
        return;
    }
    // QSaveFile commits atomically, so a concurrent reader never sees a torn
    // file even when two render workers produce the same key at once.
    QSaveFile file(brushPreviewDiskPath(key));
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    if (!image.save(&file, "PNG")) {
        file.cancelWriting();
        return;
    }
    file.commit();
}

void pruneBrushPreviewDiskCache(int maxFiles)
{
    QDir dir(brushPreviewCacheDir());
    // QDir::Time orders newest-first; everything past the cap is the oldest.
    const QFileInfoList files
        = dir.entryInfoList(QStringList { QStringLiteral("*.png") }, QDir::Files, QDir::Time);
    for (int i = maxFiles; i < files.size(); ++i) {
        QFile::remove(files.at(i).absoluteFilePath());
    }
}

QString joinKeyParts(std::initializer_list<QString> parts)
{
    QStringList list;
    list.reserve(static_cast<qsizetype>(parts.size()));
    for (const QString& part : parts) {
        list.push_back(part);
    }
    return list.join(QLatin1Char('_'));
}

QString dynamicsBindingsCacheKey(const BrushDynamicsModel& dynamics)
{
    QStringList parts;
    for (const auto& slotItem : dynamics.settingSlots) {
        const char* settingKey = brushDynamicsSettingKeyName(slotItem.setting);
        if (!settingKey) {
            continue;
        }

        for (const auto& binding : slotItem.bindings) {
            const char* sourceKey = brushInputSourceKeyName(binding.source);
            const char* modeKey = brushDynamicsBlendModeName(binding.mode);
            if (!sourceKey || !modeKey) {
                continue;
            }

            parts.push_back(QString::fromLatin1(settingKey));
            parts.push_back(QString::fromLatin1(sourceKey));
            parts.push_back(QString::fromLatin1(modeKey));
            parts.push_back(QString::number(binding.enabled ? 1 : 0));
            if (binding.source == BrushInputSourceKey::Time) {
                const char* endActionKey = brushTimeEndActionName(binding.endAction);
                parts.push_back(QString::number(binding.durationSec, 'f', 3));
                parts.push_back(QString::fromLatin1(endActionKey ? endActionKey : "stop"));
            }
            for (const auto& point : binding.curve.points) {
                parts.push_back(QString::number(point.x, 'f', 3));
                parts.push_back(QString::number(point.y, 'f', 3));
                parts.push_back(QString::number(point.smoothness, 'f', 3));
            }
            parts.push_back(QStringLiteral("end"));
        }
    }
    return parts.join(QLatin1Char('_'));
}

} // namespace

BrushPreviewSession::BrushPreviewSession(Kind kind, QObject* parent)
    : QObject(parent)
    , m_kind(kind)
{
    m_dispatchTimer.setSingleShot(true);
    connect(&m_dispatchTimer, &QTimer::timeout, this, &BrushPreviewSession::dispatchRender);

    m_renderWatcher = new QFutureWatcher<QImage>(this);
    connect(m_renderWatcher, &QFutureWatcher<QImage>::finished, this,
        &BrushPreviewSession::handleRenderFinished);

    connect(&BrushPreviewManager::instance(), &BrushPreviewManager::cacheInvalidated, this,
        &BrushPreviewSession::handleCacheInvalidated);
}

BrushPreviewSession::~BrushPreviewSession() = default;

void BrushPreviewSession::setDispatchIntervalMs(int intervalMs)
{
    m_dispatchIntervalMs = qMax(0, intervalMs);
    if (m_hasPendingSpec) {
        armDispatchTimer();
    }
}

BrushPreviewSpec BrushPreviewSession::normalizeSpec(const BrushPreviewSpec& spec) const
{
    return BrushPreviewManager::instance().normalizeSpec(m_kind, spec);
}

bool BrushPreviewSession::hasImageFor(const BrushPreviewSpec& spec) const
{
    if (!m_hasAppliedSpec || m_image.isNull()) {
        return false;
    }

    const BrushPreviewSpec normalizedSpec = normalizeSpec(spec);
    const QString key = BrushPreviewManager::instance().cacheKey(m_kind, normalizedSpec);
    return key == m_appliedKey;
}

void BrushPreviewSession::request(const BrushPreviewSpec& spec)
{
    BrushPreviewManager& manager = BrushPreviewManager::instance();
    const BrushPreviewSpec normalizedSpec = normalizeSpec(spec);
    if (normalizedSpec.size.isEmpty()) {
        clear();
        return;
    }

    const QString key = manager.cacheKey(m_kind, normalizedSpec);
    if (m_hasRequestedSpec && m_requestedKey == key) {
        const bool hasAppliedImage = m_hasAppliedSpec && m_appliedKey == key && !m_image.isNull();
        const bool hasQueuedRender = (m_hasPendingSpec && m_pendingKey == key)
            || (m_hasInFlightSpec && m_inFlightKey == key);
        if (hasAppliedImage || hasQueuedRender) {
            return;
        }
    }

    m_requestedSpec = normalizedSpec;
    m_requestedKey = key;
    m_hasRequestedSpec = true;
    ++m_requestGeneration;

    QImage cachedImage;
    if (manager.findCachedImage(m_kind, normalizedSpec, key, &cachedImage)) {
        m_hasPendingSpec = false;
        m_pendingGeneration = 0;
        queueImageApply(normalizedSpec, key, cachedImage, m_requestGeneration);
        return;
    }

    m_pendingSpec = normalizedSpec;
    m_pendingKey = key;
    m_pendingGeneration = m_requestGeneration;
    m_hasPendingSpec = true;
    armDispatchTimer();
}

void BrushPreviewSession::clear()
{
    const bool hadVisibleState = m_hasRequestedSpec || !m_image.isNull();

    ++m_requestGeneration;
    m_hasRequestedSpec = false;
    m_hasPendingSpec = false;
    m_hasInFlightSpec = false;
    m_hasAppliedSpec = false;
    m_pendingGeneration = 0;
    m_inFlightGeneration = 0;

    m_requestedKey.clear();
    m_pendingKey.clear();
    m_inFlightKey.clear();
    m_appliedKey.clear();

    m_requestedSpec = BrushPreviewSpec {};
    m_pendingSpec = BrushPreviewSpec {};
    m_inFlightSpec = BrushPreviewSpec {};
    m_appliedSpec = BrushPreviewSpec {};

    m_dispatchTimer.stop();
    m_image = QImage();

    if (hadVisibleState) {
        emit imageChanged();
    }
}

void BrushPreviewSession::armDispatchTimer()
{
    if (!m_hasPendingSpec || !m_renderWatcher || m_renderWatcher->isRunning()) {
        return;
    }

    const int delayMs = m_lastDispatch.isValid()
        ? qMax(0, m_dispatchIntervalMs - static_cast<int>(m_lastDispatch.elapsed()))
        : 0;
    if (!m_dispatchTimer.isActive() || m_dispatchTimer.remainingTime() > delayMs) {
        m_dispatchTimer.start(delayMs);
    }
}

void BrushPreviewSession::dispatchRender()
{
    if (!m_renderWatcher || m_renderWatcher->isRunning() || !m_hasPendingSpec) {
        return;
    }

    m_inFlightSpec = m_pendingSpec;
    m_inFlightKey = m_pendingKey;
    m_inFlightGeneration = m_pendingGeneration;
    m_hasInFlightSpec = true;
    m_hasPendingSpec = false;
    m_pendingGeneration = 0;
    m_pendingKey.clear();
    m_lastDispatch.restart();

    const BrushPreviewSpec specCopy = m_inFlightSpec;
    const Kind kind = m_kind;
    m_renderWatcher->setFuture(QtConcurrent::run([specCopy, kind]() -> QImage {
        return BrushPreviewManager::instance().renderPreview(kind, specCopy);
    }));
}

void BrushPreviewSession::handleRenderFinished()
{
    if (!m_renderWatcher) {
        return;
    }

    const QImage image = m_renderWatcher->result();
    const BrushPreviewSpec finishedSpec = m_inFlightSpec;
    const QString finishedKey = m_inFlightKey;
    const quint64 finishedGeneration = m_inFlightGeneration;
    m_hasInFlightSpec = false;
    m_inFlightGeneration = 0;
    m_inFlightKey.clear();

    if (!image.isNull()) {
        BrushPreviewManager::instance().storeCachedImage(finishedKey, image);
    }

    // Always show the freshly finished render. Renders are serialized (one in
    // flight at a time, dispatched in order), so this is the most recent frame
    // available. Applying it even when the request has already moved on keeps
    // the preview moving in real time during fast slider drags — lagging by at
    // most one render instead of freezing on a stale frame until the user stops.
    Q_UNUSED(finishedGeneration);
    const bool canApply = !image.isNull() && m_hasRequestedSpec;
    if (canApply) {
        const bool changed = m_appliedKey != finishedKey || m_image != image || !m_hasAppliedSpec;
        m_appliedSpec = finishedSpec;
        m_appliedKey = finishedKey;
        m_hasAppliedSpec = true;
        m_image = image;
        if (changed) {
            emit imageChanged();
        }
    }

    armDispatchTimer();
}

void BrushPreviewSession::handleCacheInvalidated()
{
    if (!m_hasRequestedSpec) {
        return;
    }

    ++m_requestGeneration;
    m_hasAppliedSpec = false;
    m_appliedKey.clear();
    m_pendingSpec = m_requestedSpec;
    m_pendingKey = m_requestedKey;
    m_pendingGeneration = m_requestGeneration;
    m_hasPendingSpec = true;
    armDispatchTimer();
}

void BrushPreviewSession::queueImageApply(
    const BrushPreviewSpec& spec, const QString& key, const QImage& image, quint64 generation)
{
    QMetaObject::invokeMethod(
        this,
        [this, spec, key, image, generation]() {
            const bool canApply = !image.isNull() && m_hasRequestedSpec
                && generation == m_requestGeneration && key == m_requestedKey;
            if (!canApply) {
                return;
            }

            const bool changed = m_appliedKey != key || m_image != image || !m_hasAppliedSpec;
            m_appliedSpec = spec;
            m_appliedKey = key;
            m_hasAppliedSpec = true;
            m_image = image;
            if (changed) {
                emit imageChanged();
            }
        },
        Qt::QueuedConnection);
}

BrushPreviewManager& BrushPreviewManager::instance()
{
    static BrushPreviewManager s_instance;
    return s_instance;
}

BrushPreviewManager::BrushPreviewManager()
    : QObject(nullptr)
{
    // Trim the on-disk cache once at startup, off the GUI thread.
    QtConcurrent::run([]() { pruneBrushPreviewDiskCache(kMaxDiskPreviewFiles); });
}

BrushPreviewSession* BrushPreviewManager::createSession(
    BrushPreviewSession::Kind kind, QObject* parent) const
{
    return new BrushPreviewSession(kind, parent);
}

BrushPreviewSpec BrushPreviewManager::normalizeSpec(
    BrushPreviewSession::Kind kind, const BrushPreviewSpec& spec) const
{
    BrushPreviewSpec normalized = spec;
    normalized.size.setWidth(qMax(0, normalized.size.width()));
    normalized.size.setHeight(qMax(0, normalized.size.height()));
    normalized.strokeSegmentCountHint = qMax(0, normalized.strokeSegmentCountHint);
    if (kind == BrushPreviewSession::Kind::Dot) {
        const int side = qMax(0, qMin(normalized.size.width(), normalized.size.height()));
        normalized.size = QSize(side, side);
        normalized.strokeSegmentCountHint = 0;
    }
    return normalized;
}

QString BrushPreviewManager::cacheKey(
    BrushPreviewSession::Kind kind, const BrushPreviewSpec& spec) const
{
    const BrushPreviewSpec normalizedSpec = normalizeSpec(kind, spec);
    const BrushSettingsData& settings = normalizedSpec.settings;
    auto n3 = [](qreal v) { return QString::number(v, 'f', 3); };
    auto n2 = [](qreal v) { return QString::number(v, 'f', 2); };
    return joinKeyParts({
        kind == BrushPreviewSession::Kind::Stroke ? QStringLiteral("stroke")
                                                  : QStringLiteral("dot"),
        QString::number(settings.flowBlendMode),
        n3(settings.hardness),
        n3(settings.flow),
        n3(settings.spacing),
        n3(settings.roundness),
        n2(settings.angle),
        QString::number(settings.sizePressureEnabled ? 1 : 0),
        QString::number(settings.opacityPressureEnabled ? 1 : 0),
        QString::number(settings.brushFeather ? 1 : 0),
        n3(settings.opacityPressureMin),
        n3(settings.opacityPressureMax),
        n3(settings.sizePressureMin),
        n3(settings.sizePressureMax),
        n3(settings.flowPressureMin),
        n3(settings.flowPressureMax),
        dynamicsBindingsCacheKey(settings.dynamics),
        QString::number(settings.textureType),
        n3(settings.textureAmount),
        n3(settings.textureScale),
        n3(settings.textureContrast),
        n3(settings.textureDepth),
        n3(settings.textureBlend),
        n3(settings.textureEdgeBoost),
        n2(settings.colorHue),
        n3(settings.colorLightness),
        n3(settings.colorSaturation),
        QString::number(settings.dabType),
        settings.dabCustomImagePath,
        n3(settings.dabXScale),
        n3(settings.dabYScale),
        n2(settings.dabRotation),
        n3(settings.dabThreshold),
        n3(settings.dabCompression),
        QString::number(settings.dabInterpolation),
        n3(settings.scatterPosition),
        n3(settings.postCorrection),
        n3(settings.stabilization),
        n3(settings.startTaper),
        n3(settings.endTaper),
        QString::number(settings.adjustCorrectionBySpeed ? 1 : 0),
        QString::number(settings.startCorrectionEnabled ? 1 : 0),
        n3(settings.startCorrectionLength),
        QString::number(settings.endCorrectionEnabled ? 1 : 0),
        n3(settings.endCorrectionLength),
        QString::number(settings.strokeBlendMode),
        n3(settings.wetMix),
        n3(settings.colorBlending),
        n3(settings.colorLength),
        n3(settings.colorDilution),
        n3(settings.colorSpread),
        n3(settings.colorWetFlow),
        n3(settings.colorDryRate),
        n3(settings.colorBuildup),
        n3(normalizedSpec.sizeNorm),
        n3(normalizedSpec.opacityNorm),
        QString::number(normalizedSpec.color.rgba(), 16),
        QString::number(normalizedSpec.size.width()),
        QString::number(normalizedSpec.size.height()),
        QString::number(normalizedSpec.strokeSegmentCountHint),
    });
}

QImage BrushPreviewManager::renderPreview(
    BrushPreviewSession::Kind kind, const BrushPreviewSpec& spec) const
{
    const BrushPreviewSpec normalizedSpec = normalizeSpec(kind, spec);
    if (normalizedSpec.size.isEmpty()) {
        return {};
    }

    // This runs on a worker thread (see BrushPreviewSession::dispatchRender), so
    // all disk I/O here stays off the GUI thread: a warm disk cache turns the
    // expensive brush-engine render into a cheap PNG load.
    const QString key = cacheKey(kind, normalizedSpec);
    QImage cached = loadPreviewFromDisk(key);
    if (!cached.isNull()) {
        return cached;
    }

    BrushPreviewRequest request;
    request.engineId = QLatin1String(kPixelBrushEngineId);
    request.engineVersion = kPixelBrushEngineVersion;
    request.settings = PixelBrushModule::settingsToVariantMap(normalizedSpec.settings);
    request.sizeNorm = normalizedSpec.sizeNorm;
    request.opacityNorm = normalizedSpec.opacityNorm;
    request.color = normalizedSpec.color;
    request.width = normalizedSpec.size.width();
    request.height = normalizedSpec.size.height();
    request.strokePreview = (kind == BrushPreviewSession::Kind::Stroke);
    // normalizeSpec() already forces a square size and a zero segment hint for
    // Dot previews, so these fields are correct for both kinds.
    request.strokeSegmentCountHint = normalizedSpec.strokeSegmentCountHint;

    QImage rendered;
    if (const auto* module = BrushEngineRegistry::instance().module(request.engineId)) {
        rendered = module->renderPreview(request);
    }

    if (!rendered.isNull()) {
        savePreviewToDisk(key, rendered);
    }
    return rendered;
}

bool BrushPreviewManager::findCachedImage(BrushPreviewSession::Kind kind,
    const BrushPreviewSpec& spec, const QString& key, QImage* image) const
{
    Q_UNUSED(kind);
    Q_UNUSED(spec);
    if (!image) {
        return false;
    }

    const auto it = m_cache.constFind(key);
    if (it == m_cache.cend()) {
        return false;
    }

    *image = it.value();
    return !image->isNull();
}

void BrushPreviewManager::storeCachedImage(const QString& key, const QImage& image)
{
    if (key.isEmpty() || image.isNull()) {
        return;
    }

    if (m_cache.size() >= MaxCacheSize) {
        m_cache.erase(m_cache.begin());
    }
    m_cache.insert(key, image);
}

void BrushPreviewManager::invalidateCache()
{
    m_cache.clear();
    emit cacheInvalidated();
}

} // namespace ruwa::core::brushes
