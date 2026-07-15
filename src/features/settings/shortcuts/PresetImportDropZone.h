// SPDX-License-Identifier: MPL-2.0

// PresetImportDropZone.h
#ifndef RUWA_FEATURES_SETTINGS_SHORTCUTS_PRESETIMPORTDROPZONE_H
#define RUWA_FEATURES_SETTINGS_SHORTCUTS_PRESETIMPORTDROPZONE_H

#include <QWidget>

class QDragEnterEvent;
class QDragLeaveEvent;
class QDropEvent;
class QMouseEvent;
class QPaintEvent;
class QVariantAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Full-width button + drag&drop zone for importing shortcut preset JSON.
 *
 * Click opens a file dialog (caller handles it via clicked()). Dragging a
 * single .json file over the widget highlights it; dropping emits fileDropped()
 * with the local path.
 */
class PresetImportDropZone : public QWidget {
    Q_OBJECT

public:
    explicit PresetImportDropZone(QWidget* parent = nullptr);
    ~PresetImportDropZone() override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();
    void fileDropped(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void startHoverAnimation(bool entering);
    void startDragAnimation(bool active);
    static QString extractDroppedJsonPath(const QDropEvent* event);

    qreal m_hoverProgress { 0.0 };
    qreal m_dragProgress { 0.0 };
    QVariantAnimation* m_hoverAnim { nullptr };
    QVariantAnimation* m_dragAnim { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_FEATURES_SETTINGS_SHORTCUTS_PRESETIMPORTDROPZONE_H
