// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTEDITOVERLAY_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTEDITOVERLAY_H

#include <QPropertyAnimation>
#include <QWidget>

class QEvent;
class QGraphicsOpacityEffect;
class QLabel;
class QLineEdit;

namespace ruwa::ui::widgets {

class CapsuleButton;
class ToggleSwitch;

class RecentProjectEditOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal dimProgress READ dimProgress WRITE setDimProgress)

public:
    explicit RecentProjectEditOverlay(QWidget* parent = nullptr);
    ~RecentProjectEditOverlay() override;

    void showForProject(const QString& filePath, const QString& projectName, bool previewEnabled);
    void hideOverlay();
    bool isActive() const { return isVisible(); }
    qreal dimProgress() const { return m_dimProgress; }
    void setDimProgress(qreal progress);

signals:
    void saveRequested(const QString& filePath, const QString& projectName, bool previewEnabled);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildUi();
    void setupAnimations();
    void applyChrome();
    void updateCardPosition();
    void updateSaveButtonState();
    QPoint cardTargetPosition() const;

private slots:
    void onDimAnimationFinished();
    void onCardHideAnimationFinished();

private:
    QWidget* m_card = nullptr;
    QGraphicsOpacityEffect* m_cardOpacityEffect = nullptr;
    QLabel* m_titleLabel = nullptr;
    QLabel* m_captionLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_previewIconLabel = nullptr;
    QLabel* m_previewTitleLabel = nullptr;
    QLabel* m_previewBodyLabel = nullptr;
    ToggleSwitch* m_previewSwitch = nullptr;
    CapsuleButton* m_cancelButton = nullptr;
    CapsuleButton* m_saveButton = nullptr;

    QString m_filePath;
    qreal m_dimProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_shortcutsBlocked = false;
    QPropertyAnimation* m_dimAnimation = nullptr;
    QPropertyAnimation* m_cardOpacityAnim = nullptr;
    QPropertyAnimation* m_cardPosAnim = nullptr;

    static constexpr int DimAnimationDuration = 180;
    static constexpr int CardAnimationDuration = 200;
    static constexpr int SlideOffset = 20;
    static constexpr qreal MaxDimOpacity = 110.0 / 255.0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_WELCOME_RECENTPROJECTEDITOVERLAY_H
