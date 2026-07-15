// SPDX-License-Identifier: MPL-2.0

// CommandPaletteOverlay.h
#ifndef RUWA_UI_WIDGETS_COMMANDPALETTE_COMMANDPALETTEOVERLAY_H
#define RUWA_UI_WIDGETS_COMMANDPALETTE_COMMANDPALETTEOVERLAY_H

#include <QWidget>
#include <QPropertyAnimation>

namespace ruwa::ui::widgets {

class CommandPalette;

/**
 * @brief Overlay layer for CommandPalette with dimming effect
 *
 * Covers the content area and provides:
 * - Animated dimming of background
 * - Click-outside-to-close behavior
 * - Hosts CommandPalette widget
 */
class CommandPaletteOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal dimProgress READ dimProgress WRITE setDimProgress)

public:
    explicit CommandPaletteOverlay(QWidget* parent = nullptr);
    ~CommandPaletteOverlay() override;

    /// Show overlay with command palette
    void showPalette();

    /// Hide overlay
    void hidePalette();

    /// Check if active
    bool isActive() const;

    /// Access palette
    CommandPalette* palette() const { return m_palette; }

    qreal dimProgress() const { return m_dimProgress; }
    void setDimProgress(qreal progress);

signals:
    void hidden();
    void shown();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void onPaletteHidden();
    void onDimAnimationFinished();

private:
    void setupUI();
    void setupAnimations();
    void updatePalettePosition();

private:
    CommandPalette* m_palette = nullptr;

    qreal m_dimProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_shortcutsBlocked = false;

    QPropertyAnimation* m_dimAnimation = nullptr;

    static constexpr int AnimationDuration = 180;
    static constexpr qreal MaxDimOpacity = 0.5;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMANDPALETTE_COMMANDPALETTEOVERLAY_H
