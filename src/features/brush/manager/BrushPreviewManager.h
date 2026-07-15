// SPDX-License-Identifier: MPL-2.0

// BrushPreviewManager.h
#ifndef RUWA_CORE_BRUSHES_BRUSHPREVIEWMANAGER_H
#define RUWA_CORE_BRUSHES_BRUSHPREVIEWMANAGER_H

#include "BrushSettings.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QTimer>

namespace ruwa::core::brushes {

struct BrushPreviewSpec {
    BrushSettingsData settings;
    qreal sizeNorm = 0.5;
    qreal opacityNorm = 1.0;
    QColor color = Qt::black;
    QSize size;
    int strokeSegmentCountHint = 0;
};

class BrushPreviewSession : public QObject {
    Q_OBJECT

public:
    enum class Kind { Stroke, Dot };

    explicit BrushPreviewSession(Kind kind, QObject* parent = nullptr);
    ~BrushPreviewSession() override;

    Kind kind() const { return m_kind; }

    void setDispatchIntervalMs(int intervalMs);
    int dispatchIntervalMs() const { return m_dispatchIntervalMs; }

    void request(const BrushPreviewSpec& spec);
    void clear();

    QImage image() const { return m_image; }
    bool hasImageFor(const BrushPreviewSpec& spec) const;

signals:
    void imageChanged();

private:
    void armDispatchTimer();
    void dispatchRender();
    void handleRenderFinished();
    void handleCacheInvalidated();
    void queueImageApply(
        const BrushPreviewSpec& spec, const QString& key, const QImage& image, quint64 generation);
    BrushPreviewSpec normalizeSpec(const BrushPreviewSpec& spec) const;

private:
    Kind m_kind = Kind::Stroke;
    int m_dispatchIntervalMs = 0;

    QTimer m_dispatchTimer;
    QFutureWatcher<QImage>* m_renderWatcher = nullptr;
    QElapsedTimer m_lastDispatch;

    quint64 m_requestGeneration = 0;
    quint64 m_pendingGeneration = 0;
    quint64 m_inFlightGeneration = 0;

    BrushPreviewSpec m_requestedSpec;
    BrushPreviewSpec m_pendingSpec;
    BrushPreviewSpec m_inFlightSpec;
    BrushPreviewSpec m_appliedSpec;

    QString m_requestedKey;
    QString m_pendingKey;
    QString m_inFlightKey;
    QString m_appliedKey;

    bool m_hasRequestedSpec = false;
    bool m_hasPendingSpec = false;
    bool m_hasInFlightSpec = false;
    bool m_hasAppliedSpec = false;

    QImage m_image;
};

/**
 * @brief Generates and caches brush preview images for UI (BrushPackPanel, BrushControlOverlay).
 *
 * Uses CPU rendering only and exposes async-only sessions for all UI consumers.
 */
class BrushPreviewManager : public QObject {
    Q_OBJECT

public:
    static BrushPreviewManager& instance();

    BrushPreviewSession* createSession(
        BrushPreviewSession::Kind kind, QObject* parent = nullptr) const;

    /// Invalidate cache when brush settings change globally (e.g. theme)
    void invalidateCache();

signals:
    void cacheInvalidated();

private:
    friend class BrushPreviewSession;

    BrushPreviewManager();
    BrushPreviewSpec normalizeSpec(
        BrushPreviewSession::Kind kind, const BrushPreviewSpec& spec) const;
    QString cacheKey(BrushPreviewSession::Kind kind, const BrushPreviewSpec& spec) const;
    QImage renderPreview(BrushPreviewSession::Kind kind, const BrushPreviewSpec& spec) const;
    bool findCachedImage(BrushPreviewSession::Kind kind, const BrushPreviewSpec& spec,
        const QString& key, QImage* image) const;
    void storeCachedImage(const QString& key, const QImage& image);

    QHash<QString, QImage> m_cache;
    static constexpr int MaxCacheSize = 512;
};

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHES_BRUSHPREVIEWMANAGER_H
