// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S   P A N E L
// ==========================================================================
//
//   The right-hand panel shown in export mode. It is an EDITOR FOR AN
//   ExportSettings VALUE and nothing else: it does not capture, resample, or
//   write anything, and it does not decide where the export goes — it hands a
//   finished settings object to whoever asked for the export.
//
//   Two things it does not own but does display:
//     - the export FRAME (the document area), which lives on CanvasPanel and is
//       also draggable on the canvas. The width/height fields therefore REQUEST
//       a change and wait to be told the result, rather than writing it
//       locally; that is what keeps the fields and the on-canvas handles from
//       disagreeing.
//     - the format's capabilities, which come from ExportSettings so that the
//       panel and the service cannot develop different ideas about whether
//       JPEG has an alpha channel.
//

#ifndef RUWA_UI_WORKSPACE_EXPORTSETTINGSPANEL_H
#define RUWA_UI_WORKSPACE_EXPORTSETTINGSPANEL_H

#include "features/export/ExportSettings.h"

#include <QColor>
#include <QFutureWatcher>
#include <QImage>
#include <QList>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

#include <optional>
#include <tuple>

class QLabel;
class QMouseEvent;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace ruwa::ui::widgets {
class AnimatedComboBox;
class AnimatedStackedWidget;
class CapsuleButton;
class ColorInputButton;
class DotGridLoadingIndicator;
class NumericInputField;
class PathInputField;
class ProgressHandleSlider;
class PropertyRowLayout;
class SegmentedOptionSelector;
class SmoothScrollArea;
class ToggleSwitch;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

class ExportSettingsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExportSettingsPanel(QWidget* parent = nullptr);
    ~ExportSettingsPanel() override;

    /// The area being exported. Drives the width/height fields and, through
    /// the scale, the output size.
    void setExportFrame(const QRect& frame);

    /// Where "Reset" puts the frame back to (the document bounds, or the
    /// content bounds on an infinite canvas). Only used to enable/disable the
    /// button; the reset itself is performed by the owner.
    void setDefaultExportFrame(const QRect& frame);

    /// Largest frame the width/height fields may ask for. An empty size means
    /// "no limit" (an infinite canvas has no edge to bump into). The owner
    /// clamps the frame anyway; this stops the fields from scrubbing far past
    /// a limit that will only snap them back.
    void setFrameSizeLimit(const QSize& maxSize);

    /// Seeds the file name the first time the panel is opened for a document.
    /// Ignored once the user has typed a name into the destination path.
    void setSuggestedBaseName(const QString& baseName);

    /// Puts the Export button into its busy state for the duration of a job.
    /// The panel does not know a job is running — the owner does — so this is
    /// told to it rather than discovered.
    void setExportInProgress(bool running);

    /// Hands the panel a small preview of what is being exported (the export
    /// frame's content, rendered at reduced resolution). The size estimate is
    /// measured by trial-encoding this sample, so without it the label falls
    /// back to a coarse per-pixel guess. Cheap to call often: the panel
    /// debounces re-measurement itself.
    void setExportContentSample(const QImage& sample);

    /// Height the content wants. The controller sizes the panel to this instead
    /// of a fixed aspect ratio, and the content scrolls when it does not fit.
    [[nodiscard]] int preferredHeight() const;

signals:
    void exitRequested();

    /// preferredHeight() now answers differently — a theme change re-measured
    /// the reserved content height. The panel does not size itself
    /// (ExportModeController places it), so it has to say so rather than just
    /// calling updateGeometry(). Format switching does NOT emit this: the
    /// reserved height already covers every format.
    void preferredHeightChanged();

    /// The width/height fields want the export frame resized (anchored at its
    /// top-left corner). The owner applies it and echoes the result back
    /// through setExportFrame().
    void exportFrameResizeRequested(const QSize& size);

    /// "Reset" was pressed; the owner restores its default frame.
    void exportFrameResetRequested();

    /// The matte swatch was clicked. The shared color picker is opened by the
    /// main window, so the request travels up rather than being served here.
    void colorPickerRequested(const QColor& initialColor, QWidget* sourceButton);

    /// Export was pressed. The settings are already validated-by-construction
    /// as far as the UI can manage; the service validates them again.
    void exportRequested(const ruwa::core::exporting::ExportSettings& settings);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    // --- construction (ExportSettingsPanelSections.cpp) ---
    void buildUI();
    QWidget* buildHeader();
    QWidget* buildFormatSelector(QWidget* parent);
    /// The whole settings block, one page per format. See moveSectionsToPage().
    QWidget* buildSectionStack(QWidget* parent);
    QWidget* buildAreaSection(QWidget* parent);
    QWidget* buildSizeSection(QWidget* parent);
    QWidget* buildOptionsSection(QWidget* parent);
    QWidget* buildDestinationSection(QWidget* parent);
    QWidget* buildFooter();

    // --- state flow ---
    /// Push m_settings into every control without echoing signals back.
    void syncControlsFromSettings();
    /// Recompute the output size from the frame and the scale, then refresh the
    /// derived labels. The single place outputSize is ever written.
    void recomputeOutputSize();
    void applyFormatCapabilities();
    /// Measures the tallest the settings block can ever get — the taller of the
    /// two format-owned rows, with the matte row shown — and remembers it as
    /// the height the panel always asks for. Restores the current visibility
    /// before returning.
    void measureReservedContentHeight();
    void updateDerivedLabels();

    /// Re-derives the width/height maxima from the frame limit and the frame's
    /// own origin: a frame anchored at x can only ever be (limit - x) wide.
    void applyFrameFieldRanges();

    /// Switches format and slides the settings block to that format's page.
    /// @param animate false while seeding the panel from stored preferences,
    /// where there is nothing to slide away from.
    void applyFormatSelection(int index, bool animate);
    /// Freezes the settings block as it looks right now into @p pageIndex's
    /// still, so the outgoing page has something to show once the live widgets
    /// have moved on to the incoming one.
    void captureSectionsGhost(int pageIndex);
    void clearSectionGhosts();
    /// Reparents the one live settings block into a format's page.
    void moveSectionsToPage(int pageIndex);

    void onFormatChanged(int index);
    void onScaleChanged(double percent);
    void onFrameFieldEdited();
    void onBrowseClicked();
    /// Splits a typed or browsed path into directory + base name.
    void applyDestinationPath(const QString& path);
    /// Writes directory + file name back into the single path field.
    void updateDestinationField();
    void onExportClicked();
    void onThemeChanged();

    [[nodiscard]] QString resolvedBaseName() const;

    // --- helpers ---
    void updateHeaderIcon();
    void updateExportButtonIcon();
    [[nodiscard]] static QString formatByteSize(qint64 bytes);

    // --- size estimation ---
    /// Everything a trial encode depends on. A measurement is reused until
    /// this changes.
    using EstimateKey = std::tuple<int, int, int, bool, quint64, int, int>;
    [[nodiscard]] EstimateKey estimateKey() const;

    /// Restarts the debounce timer; call after any input the estimate depends
    /// on has changed.
    void scheduleEstimate();
    /// Runs one trial encode of the sample off the GUI thread. Results from
    /// superseded runs are discarded by generation count, not by cancelling —
    /// QtConcurrent::run cannot be interrupted, only ignored.
    void runEstimate();
    void onEstimateFinished();
    /// The spinner beside the size value. On while a measurement is in flight
    /// and nothing measured exists to show yet.
    void setEstimateLoadingVisible(bool visible);

    /// What a trial-encode job reports back: its generation, so a stale run is
    /// recognizable, the measured size (-1 when the encode failed), and the
    /// settings key it was measured against.
    struct EstimateResult {
        quint64 generation = 0;
        qint64 bytes = -1;
        EstimateKey key;
    };

    ruwa::core::exporting::ExportSettings m_settings;
    QRect m_exportFrame;
    QRect m_defaultExportFrame;
    /// Empty = unbounded. See setFrameSizeLimit().
    QSize m_frameSizeLimit;
    QString m_suggestedBaseName;
    /// Guards the frame-field <-> export-frame round trip. Set while pushing
    /// state into the controls so their change signals do not bounce back out
    /// as a new edit request.
    bool m_syncing = false;
    /// Height of everything outside the scroll area, measured once at build
    /// time. See preferredHeight().
    int m_chromeHeight = 0;
    /// Height the scroll content is given regardless of which format is
    /// selected. Reserved rather than measured live so the panel does not jump
    /// a row taller when the format changes — and so the scroll area either
    /// engages for every format or for none, instead of only for the tallest.
    int m_reservedContentHeight = 0;

    /// Owned outright: PropertyRowLayout is a layout manager, not a QObject, so
    /// it has no parent to clean it up. Kept past construction because a theme
    /// or language change has to re-measure the caption column.
    QList<ruwa::ui::widgets::PropertyRowLayout*> m_propertyRows;

    QLabel* m_titleIconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QList<QLabel*> m_sectionLabels;

    ruwa::ui::widgets::SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;

    ruwa::ui::widgets::SegmentedOptionSelector* m_formatSelector = nullptr;

    /// One page per format. Only ever one of them holds \ref m_sectionsHost;
    /// the others are empty but for their still (\ref m_formatGhosts), which
    /// is what the outgoing page shows for the length of a slide.
    ruwa::ui::widgets::AnimatedStackedWidget* m_sectionStack = nullptr;
    QWidget* m_sectionsHost = nullptr;
    QList<QWidget*> m_formatPages;
    QList<QLabel*> m_formatGhosts;

    /// Quality (JPEG, WebP) and Depth (PNG) are one row each in OPTIONS, and
    /// exactly one of them is visible — captions included, since a hidden row
    /// is two widgets, not one.
    ruwa::ui::widgets::ProgressHandleSlider* m_qualitySlider = nullptr;
    QLabel* m_qualityCaption = nullptr;
    ruwa::ui::widgets::SegmentedOptionSelector* m_bitDepthSelector = nullptr;
    QLabel* m_depthCaption = nullptr;

    ruwa::ui::widgets::NumericInputField* m_widthField = nullptr;
    ruwa::ui::widgets::NumericInputField* m_heightField = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_resetFrameButton = nullptr;

    ruwa::ui::widgets::NumericInputField* m_scaleField = nullptr;
    ruwa::ui::widgets::AnimatedComboBox* m_resampleCombo = nullptr;
    QLabel* m_resampleCaption = nullptr;
    QLabel* m_outputSizeLabel = nullptr;

    ruwa::ui::widgets::ToggleSwitch* m_transparencyToggle = nullptr;
    /// The matte row is caption + swatch in a shared property grid, so hiding
    /// it means hiding both halves — there is no single row widget to toggle.
    QLabel* m_matteCaption = nullptr;
    ruwa::ui::widgets::ColorInputButton* m_matteButton = nullptr;

    /// One field, one answer: folder and file name are the same decision, and
    /// splitting them across two rows made the panel ask twice for a path the
    /// browse dialog returns whole. The dialog lives inside the capsule as its
    /// trailing action, so there is no separate button to line up either.
    ruwa::ui::widgets::PathInputField* m_destinationField = nullptr;

    QLabel* m_estimatedSizeTitleLabel = nullptr;
    QLabel* m_estimatedSizeLabel = nullptr;
    ruwa::ui::widgets::DotGridLoadingIndicator* m_estimatedSizeIndicator = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_exportButton = nullptr;

    // --- size estimation state ---
    /// Reduced-resolution render of the export frame's content, owned by the
    /// estimation job. Straight-alpha RGBA, exactly what the encoder sees.
    QImage m_contentSample;
    /// Last measured estimate (extrapolated to the current output size).
    qint64 m_measuredBytes = 0;
    bool m_hasMeasuredBytes = false;
    /// Settings key the last accepted measurement was made against; empty
    /// until then and whenever a new sample invalidates it.
    std::optional<EstimateKey> m_completedEstimateKey;
    QTimer* m_estimateTimer = nullptr;
    QFutureWatcher<EstimateResult> m_estimateWatcher;
    quint64 m_estimateGeneration = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_EXPORTSETTINGSPANEL_H
