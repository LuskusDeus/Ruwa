// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_BRUSH_BRUSHDYNAMICSPOPUP_H
#define RUWA_UI_WIDGETS_BRUSH_BRUSHDYNAMICSPOPUP_H

#include "features/brush/ui/BrushDynamicsEditorWidget.h"

#include <QPoint>
#include <QPointer>
#include <QString>
#include <QWidget>

class QEvent;
class QGraphicsOpacityEffect;
class QKeyEvent;
class QLabel;
class QObject;
class QPushButton;
class QVariantAnimation;

namespace ruwa::ui::widgets {

class CapsuleButton;

/**
 * Floating parameter-dynamics editor opened from a curve button in the brush
 * settings panel. Same editor as the brush editor overlay, in a popup frame
 * anchored to the button that opened it.
 */
class BrushDynamicsPopup : public QWidget {
    Q_OBJECT

public:
    using BrushDynamicsSlot = ruwa::core::brushes::BrushDynamicsSlot;
    using BrushDynamicTargetDef = ruwa::core::brushes::BrushDynamicTargetDef;
    using CurveAxesConfig = BrushDynamicsEditorWidget::CurveAxesConfig;

    explicit BrushDynamicsPopup(QWidget* parent = nullptr);

    void showForSetting(const QString& settingKey, const QString& settingLabel,
        const BrushDynamicsSlot& slot, const BrushDynamicTargetDef& targetDef,
        CurveAxesConfig curveAxesConfig, QWidget* anchor, const QPoint& anchorGlobalPos);
    void hidePopup();
    bool isPopupVisible() const;
    QString settingKey() const;
    /// The widget that opened the popup, if it is still visible. Used by the
    /// caller to decide whether a repeat click on the same button should
    /// toggle the popup closed instead of re-showing it.
    QWidget* anchorWidget() const;
    void setCurveAxesConfig(CurveAxesConfig curveAxesConfig);

signals:
    void slotChanged(const QString& settingKey, const BrushDynamicsSlot& slot);
    void editingFinished();
    void closed();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updatePosition(const QPoint& anchorGlobalPos);
    void applyShowProgress(qreal progress);
    void updateStyles();

    BrushDynamicsEditorWidget* m_editor = nullptr;
    QLabel* m_titleLabel = nullptr;
    CapsuleButton* m_resetButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QVariantAnimation* m_showAnimation = nullptr;
    QPointer<QWidget> m_host;
    QPointer<QWidget> m_anchor;
    QPoint m_targetPos;
    bool m_closing = false;
    bool m_filtersInstalled = false;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_BRUSH_BRUSHDYNAMICSPOPUP_H
