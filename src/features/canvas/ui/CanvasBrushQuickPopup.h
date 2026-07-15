// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_CANVASBRUSHQUICKPOPUP_H
#define RUWA_UI_WORKSPACE_CANVASBRUSHQUICKPOPUP_H

#include "features/brush/manager/BrushManager.h"

#include <QColor>
#include <QEasingCurve>
#include <QMargins>
#include <QPoint>
#include <QPixmap>
#include <QSize>
#include <QVector>
#include <QWidget>

class QLabel;
class QEvent;
class QGraphicsOpacityEffect;
class QGridLayout;
class QHideEvent;
class QShowEvent;
class QVariantAnimation;
class QVBoxLayout;

namespace ruwa::ui::widgets {

class SmoothScrollArea;

class CanvasBrushQuickPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)

public:
    struct BrushEntry {
        QString id;
        QString name;
        QString packName;
        ruwa::core::brushes::BrushSettingsData settings;
    };

    struct Model {
        BrushEntry currentBrush;
        QString currentPackName;
        QVector<BrushEntry> recentBrushes;
        QColor previewColor = Qt::black;
        qreal previewSizeNorm = 0.5;
        qreal previewOpacityNorm = 1.0;
        qreal brushSizeNormalized = 0.5;
        qreal brushOpacityNormalized = 1.0;
        QString sizeText;
        QString opacityText;
    };

    explicit CanvasBrushQuickPopup(QWidget* parent = nullptr);

    void setModel(const Model& model);
    void showAt(const QPoint& topLeft);
    void hidePopup(bool animate = true);
    void hideImmediate();
    bool isPopupVisible() const { return isVisible(); }
    qreal popupOpacity() const { return m_opacity; }
    void setPopupOpacity(qreal opacity);
    qreal showProgress() const { return m_showProgress; }
    void setShowProgress(qreal progress);

signals:
    void brushSizeChanged(qreal sizeNormalized);
    void brushOpacityChanged(qreal opacityNormalized);
    void recentBrushSelected(const QString& brushId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildRecentBrushes();
    void applyTheme();
    void applyPresentationLayout(qreal progress);
    void capturePresentationSnapshot();
    void setLiveContentVisible(bool visible);
    bool containsGlobalPoint(const QPoint& globalPos) const;
    bool isPopupOrChild(const QWidget* widget) const;
    void startShowAnimation();
    void startHideAnimation();
    void refreshPresentationSnapshot();
    void scheduleSnapshotRefresh();

    Model m_model;
    qreal m_opacity = 0.0;
    qreal m_showProgress = 0.0;
    QSize m_fullSize;
    QMargins m_rootMarginsFull;
    int m_rootSpacingFull = 0;
    int m_previewHeightFull = 0;
    int m_sliderHeightFull = 0;
    int m_recentScrollHeightFull = 0;
    QEasingCurve m_ctxAppearEase;
    qreal m_showStartProgress = 0.0;
    qreal m_hideStartProgress = 1.0;
    bool m_useSnapshotPresentation = false;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_snapshotRefreshScheduled = false;
    int m_shadowMargin = 0;
    QPixmap m_presentationSnapshot;

    QVBoxLayout* m_rootLayout = nullptr;
    QWidget* m_metricsWidget = nullptr;
    QGridLayout* m_metricsLayout = nullptr;
    QLabel* m_titleLabel = nullptr;
    QWidget* m_previewWidget = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QWidget* m_sizeSlider = nullptr;
    QWidget* m_opacitySlider = nullptr;
    QLabel* m_recentLabel = nullptr;
    QWidget* m_separator = nullptr;
    SmoothScrollArea* m_recentScrollArea = nullptr;
    QWidget* m_recentContent = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QVariantAnimation* m_showProgressAnim = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WORKSPACE_CANVASBRUSHQUICKPOPUP_H
