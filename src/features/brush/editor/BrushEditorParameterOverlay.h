// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORPARAMETEROVERLAY_H
#define RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORPARAMETEROVERLAY_H

#include "features/brush/manager/BrushSettingDefs.h"
#include "features/brush/ui/BrushDynamicsEditorWidget.h"

#include <QRect>
#include <QString>
#include <QWidget>

class QLabel;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QObject;
class QPaintEvent;
class QGraphicsOpacityEffect;
class QHideEvent;
class QPushButton;
class QResizeEvent;
class QShowEvent;
class QVariantAnimation;

namespace ruwa::ui::widgets {
class CapsuleButton;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::windows {

/// Modal overlay chrome (dim, panel, title row) around the shared parameter
/// dynamics editor.
class BrushEditorParameterOverlay : public QWidget {
    Q_OBJECT

public:
    using BrushDynamicsSlot = ruwa::core::brushes::BrushDynamicsSlot;
    using BrushDynamicTargetDef = ruwa::core::brushes::BrushDynamicTargetDef;
    using BrushInputSourceKey = ruwa::core::brushes::BrushInputSourceKey;
    using CurveAxesConfig = ruwa::ui::widgets::BrushDynamicsEditorWidget::CurveAxesConfig;

    explicit BrushEditorParameterOverlay(QWidget* parent = nullptr);
    ~BrushEditorParameterOverlay() override;

    void showOverlay(const QString& settingKey, const QString& settingLabel,
        const BrushDynamicsSlot& slot, const BrushDynamicTargetDef& targetDef);
    void showOverlay(const QString& settingKey, const QString& settingLabel,
        const BrushDynamicsSlot& slot, const BrushDynamicTargetDef& targetDef,
        CurveAxesConfig curveAxesConfig);
    void hideOverlay();
    bool isActive() const;
    QString settingKey() const;
    BrushInputSourceKey activeSource() const;
    void setActiveSource(BrushInputSourceKey source);
    void setCurveAxesConfig(CurveAxesConfig curveAxesConfig);

signals:
    void slotChanged(const QString& settingKey, const BrushDynamicsSlot& slot);
    void activeSourceChanged(BrushInputSourceKey source);
    void editingFinished();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateTexts();
    void updatePanelGeometry();
    void updatePanelPresentation();
    void updateStyles();
    void setShortcutBlocking(bool blocked);

    QWidget* m_panel = nullptr;
    ruwa::ui::widgets::BrushDynamicsEditorWidget* m_editor = nullptr;
    QGraphicsOpacityEffect* m_panelOpacityEffect = nullptr;
    QLabel* m_titleLabel = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_resetButton = nullptr;
    QPushButton* m_closeButton = nullptr;
    QVariantAnimation* m_dimAnimation = nullptr;
    QVariantAnimation* m_panelAnimation = nullptr;
    QString m_settingLabel;
    QRect m_targetPanelRect;
    qreal m_dimProgress = 0.0;
    qreal m_panelProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_shortcutsBlocked = false;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORPARAMETEROVERLAY_H
