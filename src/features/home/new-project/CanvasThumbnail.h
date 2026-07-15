// SPDX-License-Identifier: MPL-2.0

// CanvasThumbnail.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_CANVASTHUMBNAIL_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_CANVASTHUMBNAIL_H

#include "shared/widgets/BaseStyledPanel.h"
#include <QRectF>
#include <QSize>
#include <QSizeF>

class QPropertyAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Canvas dimension preview widget
 *
 * Layout:
 *   - Preview area (aspect ratio rect, centered in content)
 *   - Text overlay centered in preview: project name, W x H, ratio
 */
class CanvasThumbnail : public BaseStyledPanel {
    Q_OBJECT
    Q_PROPERTY(qreal morphProgress READ morphProgress WRITE setMorphProgress)
    Q_PROPERTY(qreal metadataProgress READ metadataProgress WRITE setMetadataProgress)

public:
    explicit CanvasThumbnail(const QSize& widgetSize, QWidget* parent = nullptr);
    ~CanvasThumbnail() override;

    void setDimensions(const QSize& dimensions);
    void setDimensions(int width, int height);
    QSize dimensions() const { return m_targetDimensions; }

    void setProjectName(const QString& name);
    QString projectName() const { return m_projectName; }
    void clearProjectName() { setProjectName(QString()); }

    void setInfiniteCanvasEnabled(bool enabled);
    bool infiniteCanvasEnabled() const { return m_infiniteCanvasEnabled; }

    qreal morphProgress() const { return m_morphProgress; }
    void setMorphProgress(qreal progress);
    qreal metadataProgress() const { return m_metadataProgress; }
    void setMetadataProgress(qreal progress);

    QSize sizeHint() const override;

protected:
    void drawBackgroundLayer(QPainter& painter, const QRectF& rect) override;
    void drawBorderLayer(QPainter& painter, const QRectF& rect) override;
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;

private:
    void setupMorphAnimation();
    void setupMetadataAnimation();
    void applyScaledSizeConstraints();
    void startMorphAnimation();
    void startMetadataAnimation();
    QRectF computeGhostRect(const QRectF& contentArea) const;
    QSizeF interpolateSize(const QSizeF& from, const QSizeF& to, qreal t) const;
    void drawGhostRect(QPainter& painter, const QRectF& ghostRect) const;
    void drawCornerMarks(QPainter& painter, const QRectF& ghostRect) const;
    void drawCenteredText(QPainter& painter, const QRectF& ghostRect) const;
    QString ratioString(const QSize& size) const;

private:
    QSize m_targetDimensions { 1920, 1080 };
    QSize m_previousDimensions { 1920, 1080 };
    QSize m_baseWidgetSize;
    QString m_projectName;
    bool m_infiniteCanvasEnabled = false;

    qreal m_morphProgress = 1.0;
    qreal m_metadataProgress = 1.0;
    QPropertyAnimation* m_morphAnimation = nullptr;
    QPropertyAnimation* m_metadataAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_NEWPROJECT_CANVASTHUMBNAIL_H
