// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_IMAGEIMPORTSELECTIONOVERLAY_H
#define RUWA_UI_WORKSPACE_IMAGEIMPORTSELECTIONOVERLAY_H

#include <QImage>
#include <QVector>
#include <QStringList>
#include <QtGlobal>
#include <QWidget>

class QLabel;
class QEvent;
class QGraphicsOpacityEffect;
class QKeyEvent;
class QMouseEvent;
class QObject;
class QPropertyAnimation;

namespace ruwa::ui::widgets {
class BaseAnimatedButton;
class CapsuleButton;
class SmoothScrollArea;
class FlowLayout;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

class ImageImportPreviewTile;

enum class ImageImportMode : int { SmartLayer, BoardLayer };

class ImageImportSelectionOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal overlayOpacity READ overlayOpacity WRITE setOverlayOpacity)
    Q_PROPERTY(qreal panelOffset READ panelOffset WRITE setPanelOffset)

public:
    explicit ImageImportSelectionOverlay(QWidget* parent = nullptr);

    void showForFiles(const QStringList& filePaths);
    void showForClipboardImage(const QImage& image);
    void showForSingleImage(const QImage& image, const QString& title);
    void hideOverlay();
    bool isOverlayVisible() const;
    qreal overlayOpacity() const { return m_overlayOpacity; }
    void setOverlayOpacity(qreal opacity);
    qreal panelOffset() const { return m_panelOffset; }
    void setPanelOffset(qreal offset);

signals:
    void importRequested(const QStringList& filePaths, ImageImportMode mode);
    void singleImageImportRequested(const QImage& image, ImageImportMode mode);
    void cancelled();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void startShowAnimation();
    void startHideAnimation();
    void hideImmediate();
    void clearGallery();
    void showOverlayPanel();
    void resetClipboardImportState();
    void queuePreviewLoad(ImageImportPreviewTile* tile, const QString& filePath);
    void updateButtonIcon();
    void populateList(const QStringList& filePaths);
    void updatePanelGeometry();
    void updateImportButtonState();
    QStringList selectedFilePaths() const;
    void setClipboardImagePreview(const QImage& image);
    void updateStyles();

private:
    QWidget* m_panel = nullptr;
    QLabel* m_titleLabel = nullptr;
    ruwa::ui::widgets::SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_galleryWidget = nullptr;
    ruwa::ui::widgets::FlowLayout* m_galleryLayout = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_importSmartButton = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_importBoardButton = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnimation = nullptr;
    QPropertyAnimation* m_panelOffsetAnimation = nullptr;
    QVector<ImageImportPreviewTile*> m_tiles;
    ImageImportPreviewTile* m_clipboardTile = nullptr;
    QImage m_pendingClipboardImage;
    qreal m_overlayOpacity = 0.0;
    qreal m_panelOffset = 0.0;
    quint64 m_previewLoadGeneration = 0;
    bool m_isHiding = false;
    bool m_clipboardImportActive = false;

    static constexpr int SHOW_DURATION = 150;
    static constexpr int HIDE_DURATION = 140;
    static constexpr int PANEL_SLIDE_OFFSET = 18;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_IMAGEIMPORTSELECTIONOVERLAY_H
