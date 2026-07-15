// SPDX-License-Identifier: MPL-2.0

// DotGridLoadingIndicator.h
#ifndef RUWA_UI_WIDGETS_COMMON_DOTGRIDLOADINGINDICATOR_H
#define RUWA_UI_WIDGETS_COMMON_DOTGRIDLOADINGINDICATOR_H

#include <QWidget>
#include <QColor>

class QPropertyAnimation;
class QSequentialAnimationGroup;

namespace ruwa::ui::widgets {

/**
 * @brief 3x3 dot grid loading indicator
 *
 * Animation path: top-right → left → down → right → up (1 step) → center
 * Duration: 1 sec animation + 0.5 sec cooldown per cycle
 *
 * API: setFixedSize/setMinimumSize for dimensions, setAccentColor for active dot color
 */
class DotGridLoadingIndicator : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)

public:
    explicit DotGridLoadingIndicator(QWidget* parent = nullptr);
    ~DotGridLoadingIndicator() override;

    qreal progress() const { return m_progress; }
    void setProgress(qreal p);

    void setAccentColor(const QColor& color);
    QColor accentColor() const { return m_accentColor; }

    void start();
    void stop();
    bool isRunning() const { return m_running; }

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    qreal m_progress { 0.0 };
    QColor m_accentColor;
    QColor m_dotColor;
    bool m_running { false };
    QPropertyAnimation* m_fadeInAnimation { nullptr };
    QPropertyAnimation* m_pathAnimation { nullptr };
    QPropertyAnimation* m_fadeAnimation { nullptr };
    QSequentialAnimationGroup* m_loopGroup { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_DOTGRIDLOADINGINDICATOR_H
