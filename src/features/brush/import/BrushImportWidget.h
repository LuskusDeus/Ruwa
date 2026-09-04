// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_BRUSHIMPORTWIDGET_H
#define RUWA_UI_WIDGETS_BRUSHIMPORTWIDGET_H

#include "features/brush/manager/BrushManager.h"
#include "shared/widgets/PresetMenuTypes.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QVBoxLayout;
class QPropertyAnimation;

namespace ruwa::core::brushes {
class BrushPreviewSession;
}

namespace ruwa::ui::widgets {

class CapsuleButton;
class PresetMenuListWidget;
class SegmentedOptionSelector;
class StyledInputField;
class AnimatedComboBox;
class AnimatedStackedWidget;

/// Content for the import overlay. Files are reviewed before any brushes are added.
class BrushImportWidget final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal warningProgress READ warningProgress WRITE setWarningProgress)

public:
    explicit BrushImportWidget(const QStringList& filePaths, QWidget* parent = nullptr);
    QSize sizeHint() const override;
    qreal warningProgress() const { return m_warningProgress; }
    void setWarningProgress(qreal progress);

signals:
    void finished();
    void cancelRequested();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void loadNextFile();
    void importSelected();
    void refreshPresets();
    void updateState();
    void updateTheme();
    void retranslateUi();
    void toggleBrush(int index);
    void updateBrushSelection(int index);
    PresetMenuExtraAction selectionAction(int index) const;
    void requestVisiblePreviews();
    void setWarningVisible(bool visible);

    QStringList m_filePaths;
    int m_fileIndex = -1;
    bool m_loading = false;
    bool m_committing = false;
    QVector<ruwa::core::brushes::BrushData> m_brushes;
    QVector<bool> m_selected;
    QHash<int, ruwa::core::brushes::BrushPreviewSession*> m_previews;

    QVBoxLayout* m_layout = nullptr;
    QVBoxLayout* m_modeLayout = nullptr;
    QVBoxLayout* m_destinationLayout = nullptr;
    QLabel* m_fileLabel = nullptr;
    QLabel* m_status = nullptr;
    PresetMenuListWidget* m_list = nullptr;
    SegmentedOptionSelector* m_mode = nullptr;
    QLabel* m_modeLabel = nullptr;
    QLabel* m_packLabel = nullptr;
    AnimatedStackedWidget* m_destinationStack = nullptr;
    QWidget* m_nameRow = nullptr;
    StyledInputField* m_name = nullptr;
    QLabel* m_warning = nullptr;
    QWidget* m_warningSlot = nullptr;
    QPropertyAnimation* m_warningAnimation = nullptr;
    qreal m_warningProgress = 0.0;
    bool m_warningVisible = false;
    AnimatedComboBox* m_target = nullptr;
    CapsuleButton* m_cancel = nullptr;
    CapsuleButton* m_skip = nullptr;
    CapsuleButton* m_import = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_BRUSHIMPORTWIDGET_H
