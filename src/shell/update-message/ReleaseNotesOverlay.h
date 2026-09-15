// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_UPDATEMESSAGE_RELEASENOTESOVERLAY_H
#define RUWA_UI_WIDGETS_UPDATEMESSAGE_RELEASENOTESOVERLAY_H

#include <QElapsedTimer>
#include <QPropertyAnimation>
#include <QWidget>

#include <memory>

class QEvent;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QGraphicsOpacityEffect;
class QResizeEvent;
class QVBoxLayout;

namespace ruwa::ui::workspace {
class ToolButton;
}

namespace ruwa::ui::widgets {

class AnimatedStackedWidget;
class ReleaseNotesModel;
class SmoothScrollArea;

/**
 * @brief Release notes overlay with a release navigator and lazy detail pages
 */
class ReleaseNotesOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal dimProgress READ dimProgress WRITE setDimProgress)

public:
    explicit ReleaseNotesOverlay(QWidget* parent = nullptr);
    ~ReleaseNotesOverlay() override;

    void showOverlay();
    void hideOverlay(bool bypassCooldown = false);
    bool isActive() const;

    qreal dimProgress() const { return m_dimProgress; }
    void setDimProgress(qreal progress);

signals:
    void shown();
    void hidden();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private slots:
    void onCloseRequested();
    void onDimAnimationFinished();
    void onCardHideAnimationFinished();

private:
    void setupUi();
    void setupAnimations();
    void rebuildEntries();
    void clearEntries();
    void selectEntry(int index, bool animate);
    void ensureEntryPage(int index);
    void updateCardPosition();
    void updateTexts();
    void updateTheme();
    void refreshCardBackdrop();
    QPoint cardTargetPosition() const;

private:
    std::unique_ptr<ReleaseNotesModel> m_model;
    QWidget* m_card { nullptr };
    QLabel* m_titleLabel { nullptr };
    SmoothScrollArea* m_releaseNavigation { nullptr };
    QWidget* m_releaseNavigationContent { nullptr };
    AnimatedStackedWidget* m_releaseStack { nullptr };
    QWidget* m_navigationDivider { nullptr };
    ruwa::ui::workspace::ToolButton* m_closeButton { nullptr };
    QVBoxLayout* m_cardLayout { nullptr };
    QVBoxLayout* m_releaseNavigationLayout { nullptr };
    QHBoxLayout* m_contentLayout { nullptr };
    QGraphicsOpacityEffect* m_cardOpacityEffect { nullptr };
    QPropertyAnimation* m_dimAnimation { nullptr };
    QPropertyAnimation* m_cardOpacityAnim { nullptr };
    QPropertyAnimation* m_cardPosAnim { nullptr };
    qreal m_dimProgress { 0.0 };
    bool m_isShowing { false };
    bool m_isHiding { false };
    bool m_shortcutsBlocked { false };
    bool m_entriesBuilt { false };
    int m_selectedEntry { 0 };
    QElapsedTimer m_dismissCooldownTimer;

    static constexpr int DimAnimationDuration = 180;
    static constexpr int CardAnimationDuration = 200;
    static constexpr int SlideOffset = 20;
    static constexpr qreal MaxDimOpacity = 0.5;
    static constexpr int DismissCooldownMs = 150;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_UPDATEMESSAGE_RELEASENOTESOVERLAY_H
