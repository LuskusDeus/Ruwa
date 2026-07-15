// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerCropOverlay.h
#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNERCROPOVERLAY_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNERCROPOVERLAY_H

#include <QWidget>
#include <QPixmap>
#include <QPointer>
#include <QRectF>
#include <QString>

class QPropertyAnimation;
class QGraphicsOpacityEffect;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;

namespace ruwa::ui::widgets {

class CropAreaWidget;

/**
 * @brief Modal overlay for choosing which region of a custom image is shown on the
 *        welcome banner. The selection rectangle is locked to the banner's aspect
 *        ratio so "what you select is what you see".
 *
 * Styled and animated like UpdateMessageOverlay (dim backdrop + centered card,
 * slide/fade, shortcut blocking, geometry sync to the parent).
 */
class WelcomeBannerCropOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal dimProgress READ dimProgress WRITE setDimProgress)

public:
    explicit WelcomeBannerCropOverlay(const QString& imagePath, QWidget* parent = nullptr);
    ~WelcomeBannerCropOverlay() override;

    /// Seed the selection with an existing crop (normalized to [0,1]); call before showOverlay().
    /// A full/invalid rect leaves the default centered selection.
    void setInitialCrop(const QRectF& normalizedCrop);

    /// Animate the overlay in. Does nothing if the image could not be loaded.
    void showOverlay();

    bool hasValidImage() const { return !m_image.isNull(); }

    qreal dimProgress() const { return m_dimProgress; }
    void setDimProgress(qreal progress);

signals:
    /// Emitted once with the chosen crop, normalized to [0,1] of the source image.
    void cropConfirmed(const QRectF& normalizedRect);
    /// Emitted when the user cancels (button, Esc, or click outside the card).
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupUI();
    void syncOverlayGeometry();
    QPoint cardTargetPosition() const;
    void updateCardPosition();
    void animateIn();
    void animateOutThen(bool confirmed, const QRectF& norm);
    double resolveBannerAspect() const;

    QString m_imagePath;
    QPixmap m_image;

    QWidget* m_card { nullptr };
    CropAreaWidget* m_cropArea { nullptr };
    QGraphicsOpacityEffect* m_cardOpacityEffect { nullptr };

    QPropertyAnimation* m_dimAnimation { nullptr };
    QPropertyAnimation* m_cardOpacityAnim { nullptr };
    QPropertyAnimation* m_cardPosAnim { nullptr };

    qreal m_dimProgress { 0.0 };
    bool m_closing { false };
    bool m_shortcutsBlocked { false };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_WELCOMEBANNERCROPOVERLAY_H
