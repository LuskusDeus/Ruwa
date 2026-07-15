// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S   P A N E L
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_EXPORTSETTINGSPANEL_H
#define RUWA_UI_WORKSPACE_EXPORTSETTINGSPANEL_H

#include <QRect>
#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

class QLabel;
class QMouseEvent;
class QVBoxLayout;
class QWidget;

namespace ruwa::ui::widgets {
class AnimatedStackedWidget;
class CapsuleButton;
class ProgressHandleSlider;
class SegmentedOptionSelector;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

/// Panel shown on the right side during export mode.
/// Provides format selection, quality control, and export trigger.
class ExportSettingsPanel : public QWidget {
    Q_OBJECT

public:
    explicit ExportSettingsPanel(QWidget* parent = nullptr);

    /// Update the displayed export frame / size (call when entering export mode).
    void setExportFrame(const QRect& frame);

signals:
    /// User clicked the exit / back button.
    void exitRequested();
    /// User clicked Export. @p format is "PNG", "JPEG", or "WEBP".
    /// @p jpegQuality is 0-100 (relevant only for JPEG).
    void exportRequested(const QString& format, int jpegQuality);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void buildUI();
    void onFormatChanged(int index);
    void onExportClicked();
    void onThemeChanged();
    QWidget* createFormatPage(bool includeQualityControls);
    void updateHeaderIcon();
    void updateExportButtonIcon();
    void updateExportSizeLabels();
    void updateEstimatedSizeLabel();
    qint64 estimatedExportByteSize() const;
    QString formatEstimatedSize(qint64 bytes) const;

    QLabel* m_titleIconLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    QList<QLabel*> m_sizeLabels;
    QLabel* m_estimatedSizeTitleLabel = nullptr;
    QLabel* m_estimatedSizeLabel = nullptr;
    ruwa::ui::widgets::SegmentedOptionSelector* m_formatSelector = nullptr;
    ruwa::ui::widgets::AnimatedStackedWidget* m_formatStack = nullptr;
    ruwa::ui::widgets::ProgressHandleSlider* m_qualitySlider = nullptr;
    QLabel* m_qualityLabel = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_exportButton = nullptr;
    QRect m_exportFrame;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_EXPORTSETTINGSPANEL_H
