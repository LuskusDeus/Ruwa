// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_BRUSHEDITOR_LAYOUT_BRUSHEDITORLAYOUTWIDGET_H
#define RUWA_UI_WINDOWS_BRUSHEDITOR_LAYOUT_BRUSHEDITORLAYOUTWIDGET_H

#include "features/brush/manager/BrushManager.h"
#include "features/brush/manager/BrushPreviewManager.h"
#include "features/brush/manager/BrushSettingDefs.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/inputs/ImageUploadCardWidget.h"

#include <QHash>
#include <QImage>
#include <QLabel>
#include <QString>
#include <QVector>
#include <QWidget>

class QLabel;
class QPushButton;
class QButtonGroup;
class QHBoxLayout;
class QVBoxLayout;
class QEvent;

namespace ruwa::ui::widgets {
class SmoothScrollArea;
class AnimatedStackedWidget;
class BrushSettingsWidget;
class SegmentedOptionSelector;
class ImageDropdownSelector;
class StyledInputField;
class FlowLayout;
class PresetMenuListWidget;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::windows {

class BrushEditorParameterOverlay;

class BrushEditorLayoutWidget : public QWidget {
    Q_OBJECT

public:
    explicit BrushEditorLayoutWidget(QWidget* parent = nullptr);
    ~BrushEditorLayoutWidget() override;

    void setSelection(const QString& presetId, const QString& brushId);
    QString selectedPresetId() const { return m_selectedPresetId; }
    QString selectedBrushId() const { return m_selectedBrushId; }
    QString selectedBrushName() const;

signals:
    void selectedBrushNameChanged(const QString& brushName);
    void brushSelectionChanged(const QString& presetId, const QString& brushId);

private slots:
    void onThemeChanged();
    void onSettingsTabClicked(int index);
    void onSettingsPageChanged(int index);
    void onPageSettingChanged();
    void onBrushNameTextChanged(const QString& text);
    void onBrushNameEditingFinished();
    void onResetClicked();
    void onSaveClicked();

    void onAddPackClicked();
    void onRemovePackClicked();
    void onAddBrushClicked();
    void onRemoveBrushClicked();
    void onImportPackClicked();

private:
    using BrushPresetData = ruwa::core::brushes::BrushPresetData;
    using BrushData = ruwa::core::brushes::BrushData;
    using BrushTabDef = ruwa::core::brushes::BrushTabDef;
    using BrushSettingsData = ruwa::core::brushes::BrushSettingsData;
    using BrushDynamicsSlot = ruwa::core::brushes::BrushDynamicsSlot;
    using BrushDynamicTargetDef = ruwa::core::brushes::BrushDynamicTargetDef;
    using BrushPreviewSession = ruwa::core::brushes::BrushPreviewSession;
    using BrushManager = ruwa::core::brushes::BrushManager;

    void setupUI();
    void setupSignals();
    void populateSettingsTabs();
    void populateSettingsPages();
    QWidget* createCustomDabPage(const BrushTabDef& tabDef, const QSet<QString>& starredKeys);
    QWidget* createDabPlaceholderPage(ruwa::ui::core::IconProvider::StandardIcon iconType,
        const QString& title, const QString& description, QWidget* parent) const;
    void loadDataFromManager();
    void rebuildLibraryList();
    void importBrushFileIntoPack(const QString& presetId);
    void exportBrushToFile(const QString& brushId);
    void exportPackToFile(const QString& presetId);

    void selectPresetInternal(const QString& presetId, const QString& preferredBrushId = QString(),
        bool emitSignal = true, bool allowFirstBrushFallback = false);
    void selectBrushInternal(const QString& brushId, bool emitSignal = true);

    QVector<BrushData> currentPresetBrushes() const;
    BrushData selectedBrushData() const;
    void updateLibrarySelection();
    void updateLibraryPreview(const QString& brushId, const BrushSettingsData& settings);
    void queueLibraryPreviewRequest(const QString& brushId, const BrushSettingsData& settings);
    void queueLibraryPreviewRequests(const QVector<QPair<QString, BrushSettingsData>>& requests);
    void processQueuedLibraryPreviewRequests();
    void scheduleVisibleLibraryPreviewRequests();
    ruwa::core::brushes::BrushPreviewSpec makeBrushListPreviewSpec(
        const BrushSettingsData& settings) const;
    BrushPreviewSession* ensureLibraryPreviewSession(const QString& brushId);
    void pruneLibraryPreviewSessions(const QSet<QString>& activeBrushIds);
    int currentLibraryScroll() const;
    void restoreLibraryScroll(int value);
    void updateToolbarState();
    void updateScaledSizes();
    void updatePreview();
    void updateDabControls();
    void updateDabModeStackHeight();
    void updateStyles();
    void syncSettingsStackHeight();
    int settingsPageHeight(int index);
    void applySettingsStackHeight(int height);
    void updateActionIcons();
    void commitBrushNameFromInput();
    void showParameterDynamicsOverlay(const QString& settingKey, const QString& settingLabel);
    void updateOpenParameterDynamicsOverlayAxes();
    BrushDynamicsSlot dynamicsSlotForSetting(const QString& settingKey) const;
    BrushDynamicTargetDef dynamicsTargetForSetting(const QString& settingKey) const;
    bool applyDynamicsSlotForSetting(const QString& settingKey, const BrushDynamicsSlot& slot);

    void distributeSettings();
    void scheduleParameterDynamicsPreviewUpdate();
    void scheduleParameterDynamicsCommit();
    void flushPendingParameterDynamicsCommit();
    void clearPendingParameterDynamicsUpdates();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QHBoxLayout* m_mainLayout = nullptr;

    QWidget* m_leftPanel = nullptr;
    QVBoxLayout* m_leftLayout = nullptr;
    QWidget* m_mainPanel = nullptr;
    QVBoxLayout* m_mainPanelLayout = nullptr;

    widgets::PresetMenuListWidget* m_libraryList = nullptr;
    QHash<QString, bool> m_expandedPacks;

    QWidget* m_editorHeader = nullptr;
    QHBoxLayout* m_editorHeaderLayout = nullptr;
    widgets::StyledInputField* m_brushNameInput = nullptr;
    QPushButton* m_resetButton = nullptr;
    QPushButton* m_saveButton = nullptr;

    QWidget* m_previewBlock = nullptr;
    QVBoxLayout* m_previewLayout = nullptr;
    QLabel* m_previewTitleLabel = nullptr;
    QLabel* m_previewCaptionLabel = nullptr;
    QWidget* m_previewFramesRow = nullptr;
    QWidget* m_previewFrame = nullptr;
    QWidget* m_previewFrameDot = nullptr;

    QWidget* m_tabsBar = nullptr;
    widgets::FlowLayout* m_tabsLayout = nullptr;
    QButtonGroup* m_settingsTabGroup = nullptr;
    QVector<QPushButton*> m_tabButtons;

    widgets::SmoothScrollArea* m_settingsScrollArea = nullptr;
    QWidget* m_settingsScrollContent = nullptr;
    QVBoxLayout* m_settingsScrollLayout = nullptr;
    widgets::AnimatedStackedWidget* m_settingsStack = nullptr;
    BrushEditorParameterOverlay* m_parameterOverlay = nullptr;

    QVector<widgets::BrushSettingsWidget*> m_settingsPages;
    widgets::SegmentedOptionSelector* m_dabModeSelector = nullptr;
    widgets::AnimatedStackedWidget* m_dabModeStack = nullptr;
    widgets::ImageUploadCardWidget* m_dabImageUpload = nullptr;
    QLabel* m_dabPresetHintLabel = nullptr;
    widgets::ImageDropdownSelector* m_dabPresetSelector = nullptr;
    QHash<QString, widgets::ImageUploadCardSelection> m_dabSessionSelections;
    QWidget* m_dabTransformSection = nullptr;
    widgets::BrushSettingsWidget* m_dabTransformWidget = nullptr;
    QWidget* m_dabImageSection = nullptr;
    widgets::BrushSettingsWidget* m_dabImageWidget = nullptr;

    QVector<BrushPresetData> m_presets;
    QString m_selectedPresetId;
    QString m_selectedBrushId;
    BrushSettingsData m_currentSettings;
    bool m_localSettingsEditInFlight = false;
    bool m_brushNameEditInFlight = false;
    bool m_updatingDabControls = false;
    bool m_brushImportInProgress = false;
    bool m_managerReloadSuppressed = false;
    bool m_libraryPreviewRequestsPaused = false;
    QString m_pendingParameterDynamicsBrushId;
    BrushSettingsData m_pendingParameterDynamicsSettings;
    bool m_parameterDynamicsPreviewPending = false;
    bool m_parameterDynamicsCommitPending = false;
    QHash<QString, BrushPreviewSession*> m_libraryPreviewSessions;
    QHash<QString, BrushSettingsData> m_libraryPreviewSettingsByBrush;
    QVector<QPair<QString, BrushSettingsData>> m_queuedLibraryPreviewRequests;
    bool m_libraryPreviewQueueScheduled = false;
    bool m_visibleLibraryPreviewScheduled = false;

    static constexpr int BaseLeftPanelWidth = 272;
    static constexpr int MaxLeftPanelWidth = 272;
    static constexpr int BasePreviewHeight = 88;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_BRUSHEDITOR_LAYOUT_BRUSHEDITORLAYOUTWIDGET_H
