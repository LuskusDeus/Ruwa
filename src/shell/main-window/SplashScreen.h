// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   S P L A S H   S C R E E N
// ======================================================================================
//   File        : SplashScreen.h
//   Description : Startup splash screen with loading progress
// ======================================================================================

#ifndef RUWA_UI_WINDOWS_SPLASHSCREEN_H
#define RUWA_UI_WINDOWS_SPLASHSCREEN_H

#include <QWidget>
#include <QString>
#include <QImage>
#include <QPixmap>

class QPropertyAnimation;

namespace ruwa::ui::windows {

/**
 * @brief Splash screen shown during application startup
 *
 * Reference-style card: border, corner marks, wordmark, primary version pill,
 * status line, real loading progress bar. Foreground fades out before expand.
 */
class SplashScreen : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal appearProgress READ appearProgress WRITE setAppearProgress)
    Q_PROPERTY(qreal contentOpacity READ contentOpacity WRITE setContentOpacity)
    Q_PROPERTY(qreal foregroundOpacity READ foregroundOpacity WRITE setForegroundOpacity)
    Q_PROPERTY(qreal progressDisplay READ progressDisplay WRITE setProgressDisplay)

public:
    explicit SplashScreen(QWidget* parent = nullptr);
    ~SplashScreen() override = default;

    void setStatus(const QString& message);
    void setProgress(int percentage);

    void fadeOut(int durationMs = 300);

    void animateAppearance(int durationMs = 400);

    /// Fades out chrome (border, marks, content, progress), then expands the panel.
    void expandToMainWindow(int durationMs = 500);

    qreal appearProgress() const { return m_appearProgress; }
    void setAppearProgress(qreal progress);

    qreal contentOpacity() const { return m_contentOpacity; }
    void setContentOpacity(qreal opacity);

    qreal foregroundOpacity() const { return m_foregroundOpacity; }
    void setForegroundOpacity(qreal opacity);

    qreal progressDisplay() const { return m_progressDisplay; }
    void setProgressDisplay(qreal value);

signals:
    void expansionFinished();
    void appearanceFinished();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateColors();
    void buildShadowImage();
    void paintShadow(QPainter& painter, const QRectF& drawRect) const;
    void paintInterior(QPainter& painter) const;
    void paintCardForeground(QPainter& painter, const QRectF& drawRect) const;
    void startRectExpansion(int durationMs);

private:
    QImage m_shadowImage;
    QPixmap m_headerImage;
    QPixmap m_logoPixmap;

    QString m_statusText;
    int m_targetProgress = 0;
    qreal m_progressDisplay = 0;
    QPropertyAnimation* m_progressAnim = nullptr;

    qreal m_appearProgress = 0.0;
    bool m_isAppearing = false;

    qreal m_contentOpacity = 1.0;
    qreal m_foregroundOpacity = 1.0;

    bool m_isExpanding = false;
    bool m_hasExpanded = false;
    QRectF m_contentRect;
    QRectF m_animatedRect;
    QRectF m_startLocalRect;
    QRectF m_targetLocalRect;

    int m_pendingExpandDurationMs = 500;
    bool m_expandRequested = false;

    // expandToMainWindow() arriving mid-appearance is remembered here and replayed once
    // appearanceFinished fires, instead of being silently dropped (a dropped request meant
    // expansionFinished would never come and the splash would never close).
    bool m_expandDeferred = false;
    int m_deferredExpandDurationMs = 500;

    static constexpr int SPLASH_WIDTH = 800;
    static constexpr int SPLASH_HEIGHT = 500;
    static constexpr qreal APPEAR_START_SCALE = 0.92;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_SPLASHSCREEN_H
