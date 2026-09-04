// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_IMAGEIMPORTSELECTIONOVERLAY_H
#define RUWA_UI_WORKSPACE_IMAGEIMPORTSELECTIONOVERLAY_H

#include "shared/widgets/overlays/ContentOverlay.h"

#include <QImage>
#include <QVector>
#include <QStringList>
#include <QtGlobal>
#include <QWidget>

class QLabel;

namespace ruwa::ui::widgets {
class BaseAnimatedButton;
class CapsuleButton;
class SmoothScrollArea;
class FlowLayout;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

class ImageImportPreviewTile;

enum class ImageImportMode : int { SmartLayer, BoardLayer };

class ImageImportSelectionOverlay : public ruwa::ui::widgets::ContentOverlay {
    Q_OBJECT

public:
    explicit ImageImportSelectionOverlay(QWidget* parent = nullptr);

    void showForFiles(const QStringList& filePaths);
    void showForClipboardImage(const QImage& image);
    void showForSingleImage(const QImage& image, const QString& title);

signals:
    void importRequested(const QStringList& filePaths, ImageImportMode mode);
    void singleImageImportRequested(const QImage& image, ImageImportMode mode);

private:
    void clearGallery();
    void showOverlayPanel();
    void resetClipboardImportState();
    void queuePreviewLoad(ImageImportPreviewTile* tile, const QString& filePath);
    void updateButtonIcon();
    void populateList(const QStringList& filePaths);
    void updateImportButtonState();
    QStringList selectedFilePaths() const;
    void setClipboardImagePreview(const QImage& image);
    void updateStyles();

private:
    QLabel* m_titleLabel = nullptr;
    ruwa::ui::widgets::SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_galleryWidget = nullptr;
    ruwa::ui::widgets::FlowLayout* m_galleryLayout = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_importSmartButton = nullptr;
    ruwa::ui::widgets::CapsuleButton* m_importBoardButton = nullptr;
    QVector<ImageImportPreviewTile*> m_tiles;
    ImageImportPreviewTile* m_clipboardTile = nullptr;
    QImage m_pendingClipboardImage;
    quint64 m_previewLoadGeneration = 0;
    bool m_clipboardImportActive = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_IMAGEIMPORTSELECTIONOVERLAY_H
