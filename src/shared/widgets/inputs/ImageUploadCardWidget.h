// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_IMAGEUPLOADCARDWIDGET_H
#define RUWA_UI_WIDGETS_COMMON_IMAGEUPLOADCARDWIDGET_H

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QEvent;
class QHBoxLayout;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QStackedWidget;
class QWidget;
class QUrl;
class QVBoxLayout;

namespace ruwa::ui::widgets {

class CapsuleButton;

struct ImageUploadCardSelection {
    QString filePath;
    QString displayName;
    QStringList metadata;
    QImage previewImage;

    bool isEmpty() const
    {
        return filePath.isEmpty() && displayName.isEmpty() && metadata.isEmpty()
            && previewImage.isNull();
    }
};

class ImageUploadCardWidget : public QWidget {
    Q_OBJECT

public:
    enum class PreviewTintMode {
        SourceAlpha,
        LuminanceAlpha,
    };

    explicit ImageUploadCardWidget(QWidget* parent = nullptr);
    ~ImageUploadCardWidget() override = default;

    void setEmptyStateTexts(const QString& title, const QString& description);
    void setActionTexts(const QString& browseText, const QString& removeText);
    void setDialogTitle(const QString& title);
    QString dialogTitle() const { return m_dialogTitle; }

    void setDialogFilter(const QString& filter);
    QString dialogFilter() const { return m_dialogFilter; }

    // Category used to remember the browse dialog's last directory (see
    // shared/utils/FileDialogMemory). Defaults to the generic image category.
    void setDirectoryMemoryCategory(const QString& category) { m_dirMemoryCategory = category; }
    QString directoryMemoryCategory() const { return m_dirMemoryCategory; }

    void setAcceptedSuffixes(const QStringList& suffixes);
    QStringList acceptedSuffixes() const { return m_acceptedSuffixes; }

    void setBrowseEnabled(bool enabled);
    bool isBrowseEnabled() const { return m_browseEnabled; }

    void setFooterWidget(QWidget* widget);
    QWidget* footerWidget() const { return m_footerWidget; }

    void setPreviewFrameVisible(bool visible);
    bool isPreviewFrameVisible() const { return m_previewFrameVisible; }

    void setPreviewTintEnabled(bool enabled);
    bool isPreviewTintEnabled() const { return m_previewTintEnabled; }

    void setPreviewTintMode(PreviewTintMode mode);
    PreviewTintMode previewTintMode() const { return m_previewTintMode; }

    void setSelection(const ImageUploadCardSelection& selection);
    ImageUploadCardSelection selection() const { return m_selection; }
    bool hasSelection() const { return !m_selection.isEmpty(); }
    QString selectedFilePath() const { return m_selection.filePath; }

    bool loadFromFile(const QString& filePath);
    void clearSelection();

public slots:
    void browseForImage();

signals:
    void selectionChanged();
    void fileLoaded(const QString& filePath);
    void selectionCleared();
    void loadFailed(const QString& filePath);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void updateUi();
    void updatePreviewPixmap();
    void updateScaledSizes();
    void updateStyles();
    void applySelection(
        const ImageUploadCardSelection& selection, bool emitChanged, bool emitLoadedSignal);
    QString firstAcceptedLocalFile(const QList<QUrl>& urls) const;
    bool isAcceptedFile(const QString& filePath) const;
    ImageUploadCardSelection selectionFromFile(const QString& filePath) const;
    QStringList metadataForFile(const QString& filePath, const QImage& image) const;

private:
    QVBoxLayout* m_rootLayout = nullptr;
    QStackedWidget* m_stateStack = nullptr;
    QWidget* m_footerHost = nullptr;
    QVBoxLayout* m_footerLayout = nullptr;
    QWidget* m_footerWidget = nullptr;

    QWidget* m_emptyPage = nullptr;
    QLabel* m_emptyIconLabel = nullptr;
    QLabel* m_emptyTitleLabel = nullptr;
    QLabel* m_emptyDescriptionLabel = nullptr;

    QWidget* m_loadedPage = nullptr;
    QWidget* m_previewFrame = nullptr;
    QLabel* m_previewLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLabel* m_metaLabel = nullptr;
    QVBoxLayout* m_actionsLayout = nullptr;
    CapsuleButton* m_browseButton = nullptr;
    CapsuleButton* m_removeButton = nullptr;

    QString m_emptyTitle;
    QString m_emptyDescription;
    QString m_browseText;
    QString m_removeText;
    QString m_dialogTitle;
    QString m_dialogFilter;
    QString m_dirMemoryCategory = QStringLiteral("image");
    QStringList m_acceptedSuffixes;
    ImageUploadCardSelection m_selection;
    bool m_browseEnabled = true;
    bool m_dragActive = false;
    bool m_acceptedSuffixesExplicit = false;
    bool m_previewFrameVisible = true;
    bool m_previewTintEnabled = false;
    PreviewTintMode m_previewTintMode = PreviewTintMode::SourceAlpha;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_IMAGEUPLOADCARDWIDGET_H
