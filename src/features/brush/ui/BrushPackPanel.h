// SPDX-License-Identifier: MPL-2.0

// BrushPackPanel.h
#ifndef RUWA_UI_WIDGETS_WORKSPACE_BRUSHPACKPANEL_H
#define RUWA_UI_WIDGETS_WORKSPACE_BRUSHPACKPANEL_H

#include "shared/widgets/BaseAnimatedButton.h"
#include "features/brush/manager/BrushManager.h"
#include "features/brush/manager/BrushPreviewManager.h"
#include "features/brush/manager/BrushSettings.h"

#include <QWidget>
#include <QPropertyAnimation>
#include <QPoint>
#include <QImage>
#include <QVector>
#include <QString>
#include <QHash>
#include <QPointer>

class QMouseEvent;
class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;

namespace ruwa::ui::windows {
class BrushEditorWindow;
}

namespace ruwa::ui::widgets {

class SmoothScrollArea;
class AnimatedStackedWidget;
class BrushSettingsWidget;
using BrushPresetData = ruwa::core::brushes::BrushPresetData;
using BrushData = ruwa::core::brushes::BrushData;
using BrushSettingsData = ruwa::core::brushes::BrushSettingsData;
using BrushPreviewSession = ruwa::core::brushes::BrushPreviewSession;
using BrushManager = ruwa::core::brushes::BrushManager;

// ============================================================================
// BrushItem — single brush entry in the right-side scroll list
// ============================================================================

class BrushItem : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal activeProgress READ activeProgress WRITE setActiveProgress)

public:
    explicit BrushItem(const BrushData& data, QWidget* parent = nullptr);
    ~BrushItem() override;

    QString brushId() const { return m_data.id; }
    const BrushData& data() const { return m_data; }
    void setName(const QString& name);
    void setSettings(const BrushSettingsData& settings);
    void startRename();

    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal v);

    qreal activeProgress() const { return m_activeProgress; }
    void setActiveProgress(qreal v);

signals:
    void clicked(const QString& brushId);
    void nameEdited(const QString& brushId, const QString& newName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void ensureEditorGeometry();
    void finishRename(bool keepVisible = false);
    void invalidatePreviewCache();

    BrushData m_data;
    bool m_selected = false;
    bool m_pressed = false;
    bool m_isEditing = false;

    qreal m_hoverProgress = 0.0;
    qreal m_activeProgress = 0.0;

    QPropertyAnimation* m_hoverAnim = nullptr;
    QPropertyAnimation* m_activeAnim = nullptr;
    QLineEdit* m_nameEditor = nullptr;
    BrushPreviewSession* m_strokePreviewSession = nullptr;
    BrushPreviewSession* m_dotPreviewSession = nullptr;

    static constexpr int ItemHeight = 86;
    static constexpr int HeaderHeight = 24; ///< Thin top strip with brush name
    static constexpr int ItemCornerRadius = 8;
};

// ============================================================================
// PresetButton — sidebar preset entry using BaseAnimatedButton
// ============================================================================

class PresetButton : public BaseAnimatedButton {
    Q_OBJECT

public:
    explicit PresetButton(const BrushPresetData& data, QWidget* parent = nullptr);
    ~PresetButton() override;

    QString presetId() const { return m_data.id; }
    void setName(const QString& name);
    void startRename();

    void setSelected(bool selected);
    bool isSelected() const { return m_isSelected; }

signals:
    void presetClicked(const QString& presetId);
    void nameEdited(const QString& presetId, const QString& newName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void loadIcon();
    void ensureEditorGeometry();
    void finishRename(bool keepVisible = false);

    BrushPresetData m_data;
    bool m_isSelected = false;
    bool m_isEditing = false;
    QPixmap m_icon;
    QLineEdit* m_nameEditor = nullptr;

    static constexpr int ButtonHeight = 52;
    static constexpr int IconSize = 16;
    static constexpr int InternalPadding = 5;
};

// ============================================================================
// BrushPresetPage — one page inside the stacked widget
// ============================================================================

class BrushPresetPage : public QWidget {
    Q_OBJECT

public:
    explicit BrushPresetPage(const QString& presetId, QWidget* parent = nullptr);
    ~BrushPresetPage() override;

    QString presetId() const { return m_presetId; }

    void setBrushes(const QVector<BrushData>& brushes);
    void updateBrushSettingsFromManager(const QString& brushId, const BrushSettingsData& settings);
    /// Update one brush row's displayed name in place (repaint only, preview
    /// untouched). Returns false if the brush is not on this page.
    bool updateBrushNameFromManager(const QString& brushId, const QString& newName);
    void setSelectedBrushLocally(const QString& brushId);
    void clearSelectedBrushLocally();
    QString selectedBrushId() const { return m_selectedBrushId; }
    BrushSettingsData selectedBrushSettings() const;
    bool hasBrush(const QString& brushId) const;
    void syncLayoutNow();

signals:
    void brushSelectionRequested(const QString& brushId);
    void brushNameEdited(const QString& brushId, const QString& newName);
    void brushSettingsEdited(const QString& brushId, const BrushSettingsData& settings);
    void activeBrushSettingsChanged(const BrushSettingsData& settings);
    /// Raised when the user asks to open the detached brush editor for the
    /// page's current selection. The owning BrushPackPanel handles it; the page
    /// no longer owns an editor window itself (its lifetime is tied to the pack,
    /// which can be deleted out from under an open editor).
    void openEditorRequested(
        const QString& presetId, const QString& brushId, const QString& brushName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void setupUI();
    void openBrushEditor();
    QString selectedBrushName() const;
    void updateBrushEditorButtonState();
    void addBrushItemWidget(const BrushData& brush);
    void refreshBrushListGeometry();
    void rebuildBrushList();
    void drawSettingsPlaceholder(QPainter& painter);
    void rebuildSettingsWidget();
    void connectSettingsWidget();
    void refreshSettingsGeometry();

    QString m_presetId;
    QString m_selectedBrushId;
    QVector<BrushData> m_brushes;
    QHash<QString, BrushItem*> m_brushItems;

    // Brush list
    SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_brushListWidget = nullptr;
    QVBoxLayout* m_brushListLayout = nullptr;

    // Settings section
    QWidget* m_settingsContainer = nullptr;
    QVBoxLayout* m_settingsLayout = nullptr;
    BaseAnimatedButton* m_openBrushEditorButton = nullptr;
    SmoothScrollArea* m_settingsScrollArea = nullptr;
    QWidget* m_settingsScrollContent = nullptr;
    QVBoxLayout* m_settingsScrollLayout = nullptr;
    QVector<BrushSettingsWidget*> m_brushSettingsWidgets;

    // Main
    QVBoxLayout* m_mainLayout = nullptr;

    static constexpr double BrushListRatio = 0.62;
};

// ============================================================================
// BrushPackPanel — the main brush pack widget
// ============================================================================

class BrushPackPanel : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)
    Q_PROPERTY(qreal compactProgress READ compactProgress WRITE setCompactProgress)

public:
    explicit BrushPackPanel(QWidget* parent = nullptr);
    ~BrushPackPanel() override;

    // --- Preset management ---
    void addPreset(const BrushPresetData& preset);
    void removePreset(const QString& id);
    void clearPresets();
    QVector<BrushPresetData> presets() const { return m_presets; }
    void selectPreset(const QString& id);
    QString selectedPresetId() const { return m_selectedPresetId; }

    // --- Brush management ---
    void addBrush(const BrushData& brush);
    void removeBrush(const QString& brushId);
    void selectBrush(const QString& brushId);
    QString selectedBrushId() const;
    BrushSettingsData selectedBrushSettings() const;
    void applySettingsToSelectedBrush(const BrushSettingsData& settings);
    bool ownsAuxiliaryWidget(const QWidget* widget) const;

    // --- Layout ---
    QSize fullPanelSize() const;

    // --- Animation ---
    void showAnimated();
    void hideAnimated();
    bool isActive() const;

    qreal showProgress() const { return m_showProgress; }
    void setShowProgress(qreal progress);

    /// When false, handle drag is disabled (panel follows parent overlay only).
    void setUserMovable(bool movable) { m_userMovable = movable; }
    bool isUserMovable() const { return m_userMovable; }

    /// Compact mode when panel is detached (user moved it). 0 = full, 1 = compact.
    qreal compactProgress() const { return m_compactProgress; }
    void setCompactProgress(qreal progress);
    void setCompactMode(bool compact, bool animate = true);

signals:
    void presetSelected(const QString& presetId);
    void brushSelectionRequested(const QString& brushId);
    void activeBrushSettingsChanged(const BrushSettingsData& settings);
    void positionChanged(const QPoint& pos);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void onThemeChanged();
    void onShowAnimationFinished();
    void onHideAnimationFinished();
    void onPresetClicked(const QString& presetId);
    void onAddPresetClicked();
    void onRemovePresetClicked();
    void onAddBrushClicked();
    void onRemoveBrushClicked();
    void onPresetNameEdited(const QString& presetId, const QString& newName);
    void onBrushNameEdited(const QString& brushId, const QString& newName);
    void onBrushSettingsEdited(const QString& brushId, const BrushSettingsData& settings);

    // BrushManager signal handlers (cross-widget sync)
    void onManagerPresetCreated(const QString& presetId);
    void onManagerPresetRemoved(const QString& presetId);
    void onManagerPresetRenamed(const QString& presetId, const QString& newName);
    void onManagerBrushCreated(const QString& presetId, const QString& brushId);
    void onManagerBrushRemoved(const QString& presetId, const QString& brushId);
    void onManagerBrushRenamed(const QString& brushId, const QString& newName);
    void onManagerBrushSettingsUpdated(
        const QString& presetId, const QString& brushId, const BrushSettingsData& settings);

private:
    void setupUI();
    void setupAnimations();
    void connectSignals();
    void updateSize();
    void applyCompactLayout();
    PresetButton* createPresetButton(const BrushPresetData& preset);
    void addPresetButton(const BrushPresetData& preset);
    void removePresetButton(const QString& presetId);
    void refreshSidebarGeometry();
    void rebuildPresetSidebar();
    void loadDataFromManager();
    void addControlButtons();
    void updateControlButtonsState();
    void applyRightSectionColors();
    bool hasPreset(const QString& presetId) const;
    QString firstPresetId() const;
    void applyCanonicalSelection(
        const QString& presetId, const QString& brushId, bool emitPresetSignal);
    void normalizeCanonicalSelection(bool emitPresetSignal = false);
    void propagateCanonicalSelection(bool emitBrushSignal);
    void syncPageSelectionsAfterTransition();
    void removeBrushFromPreset(const QString& presetId, const QString& brushId);
    QString brushNameForSelection(const QString& presetId, const QString& brushId) const;
    QString resolveBrushIdForPresetSelection(const QString& presetId,
        const QString& preferredBrushId, bool allowFirstBrushFallback) const;
    void syncPresetPageFromManager(const QString& presetId);
    void syncStackedWidgetToSelectedPreset();

    // Detached brush editor (single window owned by the panel, not by a page).
    void openBrushEditor(const QString& presetId, const QString& brushId, const QString& brushName);
    void syncDetachedEditorSelection(
        const QString& presetId, const QString& brushId, const QString& brushName);

    // Drawing
    void drawBackground(QPainter& painter);
    void drawHandle(QPainter& painter, const QRectF& rect);
    void drawDivider(QPainter& painter);

    // Handle and drag
    QRectF handleRect() const;
    enum class DragMode { None, Widget };
    DragMode hitTest(const QPoint& pos) const;
    void handleDrag(const QPoint& globalPos);

    // Page management
    BrushPresetPage* pageForPreset(const QString& presetId) const;
    BrushPresetPage* ensurePage(const QString& presetId);

private:
    // Preset data
    QVector<BrushPresetData> m_presets;
    QString m_selectedPresetId;
    QString m_selectedBrushId;

    // Left sidebar
    QWidget* m_sidebarContainer = nullptr;
    QHBoxLayout* m_sidebarControlsLayout = nullptr;
    QPushButton* m_addPresetButton = nullptr;
    QPushButton* m_removePresetButton = nullptr;
    SmoothScrollArea* m_sidebarScrollArea = nullptr;
    QWidget* m_sidebarContent = nullptr;
    QVBoxLayout* m_sidebarLayout = nullptr;
    QHash<QString, PresetButton*> m_presetButtons;

    // Right container controls
    QWidget* m_rightContainer = nullptr;
    QHBoxLayout* m_brushControlsLayout = nullptr;
    QPushButton* m_addBrushButton = nullptr;
    QPushButton* m_removeBrushButton = nullptr;

    // Right: animated stacked widget with per-preset pages
    AnimatedStackedWidget* m_stackedWidget = nullptr;
    QHash<QString, BrushPresetPage*> m_pages;
    bool m_pageSelectionSyncDeferred = false;

    // Single detached editor window, parented to the panel so it survives the
    // deletion of any individual pack page.
    QPointer<ruwa::ui::windows::BrushEditorWindow> m_brushEditorWindow;

    // Main layout
    QHBoxLayout* m_mainLayout = nullptr;

    // Handle drag
    bool m_userMovable = true;
    DragMode m_dragMode = DragMode::None;
    QPoint m_dragStartPos;
    QPoint m_widgetStartPos;

    // Show/hide animation
    qreal m_showProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;
    QPropertyAnimation* m_showAnimation = nullptr;

    // Compact mode (when detached/dragged)
    qreal m_compactProgress = 0.0;
    QPropertyAnimation* m_compactAnimation = nullptr;

    // Layout constants (base values, scaled by theme)
    static constexpr int PanelWidth = 524;
    static constexpr int PanelHeight = 624;
    static constexpr int SidebarWidth = 66;
    static constexpr int Padding = 10;
    static constexpr int BaseHandleHeight = 8;
    static constexpr int BaseHandleLineWidth = 96;
    static constexpr int BaseHandleLineHeight = 3;
    static constexpr int CornerRadius = 12;
    static constexpr int DividerThickness = 1;
    static constexpr int ShowDuration = 240;
    static constexpr int HideDuration = 200;
    static constexpr int CompactDuration = 220;

    // Compact mode dimensions (relative to full)
    static constexpr double CompactHeightFactor = 1.0 / 1.7; // height / 1.7
    static constexpr int CompactSidebarWidth = 36;
    static constexpr double CompactRightWidthFactor = 1.0 / 1.10;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_WORKSPACE_BRUSHPACKPANEL_H
