// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_ANIMATEDCOMBOBOX_H
#define RUWA_UI_WIDGETS_COMMON_ANIMATEDCOMBOBOX_H

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QList>
#include <QMetaObject>
#include <QSize>
#include <QVariant>
#include <QWidget>

class QPainter;
class QPropertyAnimation;
class QGraphicsOpacityEffect;

namespace ruwa::ui::widgets {

struct AnimatedComboItem {
    QString text;
    QVariant userData;
    QIcon icon;
    QString subtitle;
    QImage previewImage;
    QColor previewTint;
    bool tintPreview = false;
    bool enabled = true;
    bool separator = false;
    bool category = false;
};

class AnimatedComboPopup;

class AnimatedComboBox : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal arrowProgress READ arrowProgress WRITE setArrowProgress)

public:
    enum class PopupPresentation {
        List,
        PreviewGrid,
    };

    explicit AnimatedComboBox(QWidget* parent = nullptr);
    ~AnimatedComboBox() override;

    void addItem(
        const QString& text, const QVariant& userData = QVariant(), const QIcon& icon = QIcon());
    void addItem(const AnimatedComboItem& item);
    void addCategory(const QString& text);
    void addSeparator();
    void clear();

    int count() const { return m_items.size(); }

    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    void clearCurrentSelection();

    QString currentText() const;
    QVariant currentData() const;
    QVariant itemData(int index) const;
    int findIndexByData(const QVariant& userData) const;

    void setPlaceholderText(const QString& text);
    QString placeholderText() const { return m_placeholderText; }

    void setPopupMinWidth(int width);
    int popupMinWidth() const { return m_popupMinWidth; }

    void setPopupMaxHeight(int height);
    int popupMaxHeight() const { return m_popupMaxHeight; }

    void setPopupPresentation(PopupPresentation presentation);
    PopupPresentation popupPresentation() const { return m_popupPresentation; }

    void setPopupColumns(int columns);
    int popupColumns() const { return m_popupColumns; }

    void setPopupCardSize(const QSize& size);
    QSize popupCardSize() const { return m_popupCardSize; }

    void setItemPreviewImage(int index, const QImage& image);

    /** Lay the real popup out without opening an interactive overlay, and draw
     * it into an arbitrary painter. Presentation surfaces (theme previews) use
     * this to show an open dropdown that no one can click. */
    QSize preparePresentationPopup(int maxHeight);
    void renderPresentationPopup(QPainter* painter, const QPoint& target);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal arrowProgress() const { return m_arrowProgress; }
    void setArrowProgress(qreal progress);

signals:
    void currentIndexChanged(int index);
    void activated(int index);
    void itemHovered(int index);
    void popupShown();
    void popupHidden();

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateScaledSizes();
    AnimatedComboPopup* ensurePopup();
    void schedulePopupWarmUp();
    void warmUpPopup();
    int availablePopupWidth() const;
    bool popupContentNeedsRebuild() const;
    void markPopupContentDirty();
    void openPopup();
    void closePopup();
    void togglePopup();
    void syncPopupItems();
    void updatePopupPosition();
    void connectScrollAreaPositionUpdates();
    void disconnectScrollAreaPositionUpdates();
    int firstSelectableIndex() const;
    int nextSelectableIndex(int from, int direction) const;
    void animateHoverTo(qreal target);
    void animateArrowTo(qreal target);
    bool isPopupActive() const;

private:
    QList<AnimatedComboItem> m_items;
    int m_currentIndex = -1;
    QString m_placeholderText = "Select";
    int m_popupMinWidth = 180;
    int m_popupMaxHeight = 360;
    PopupPresentation m_popupPresentation = PopupPresentation::List;
    int m_popupColumns = 2;
    QSize m_popupCardSize = QSize(136, 104);

    AnimatedComboPopup* m_popup = nullptr;
    QList<QMetaObject::Connection> m_scrollAreaPositionConnections;

    // The popup is built and painted once off the click path (see warmUpPopup)
    // and only rebuilt when its content or the widths it was laid out for change.
    bool m_popupWarmUpScheduled = false;
    bool m_popupWarmedUp = false;
    bool m_popupContentDirty = true;
    int m_popupBuiltForComboWidth = -1;
    int m_popupBuiltForAvailableWidth = -1;

    qreal m_hoverProgress = 0.0;
    qreal m_arrowProgress = 0.0;

    QPropertyAnimation* m_hoverAnim = nullptr;
    QPropertyAnimation* m_arrowAnim = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_ANIMATEDCOMBOBOX_H
