// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_IMAGEDROPDOWNSELECTOR_H
#define RUWA_UI_WIDGETS_COMMON_IMAGEDROPDOWNSELECTOR_H

#include <QColor>
#include <QImage>
#include <QList>
#include <QMetaObject>
#include <QSize>
#include <QVariant>
#include <QWidget>

class QEnterEvent;
class QEvent;
class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QObject;
class QPaintEvent;
class QPropertyAnimation;

namespace ruwa::ui::widgets {

struct ImageDropdownItem {
    QString text;
    QString subtitle;
    QVariant userData;
    QImage previewImage;
    QColor previewTint;
    bool tintPreview = false;
    bool enabled = true;
};

class ImageDropdownPopup;

class ImageDropdownSelector : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal pressProgress READ pressProgress WRITE setPressProgress)
    Q_PROPERTY(qreal arrowProgress READ arrowProgress WRITE setArrowProgress)

public:
    explicit ImageDropdownSelector(QWidget* parent = nullptr);
    ~ImageDropdownSelector() override;

    void addItem(const ImageDropdownItem& item);
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

    void setPopupColumns(int columns);
    int popupColumns() const { return m_popupColumns; }

    void setPopupCardSize(const QSize& size);
    QSize popupCardSize() const { return m_popupCardSize; }

    void setPopupMaxHeight(int height);
    int popupMaxHeight() const { return m_popupMaxHeight; }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal pressProgress() const { return m_pressProgress; }
    void setPressProgress(qreal progress);

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
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void openPopup();
    void closePopup();
    void togglePopup();
    void syncPopupItems();
    void updatePopupPosition();
    void connectScrollAreaPositionUpdates();
    void disconnectScrollAreaPositionUpdates();
    int firstSelectableIndex() const;
    int nextSelectableIndex(int from, int delta) const;
    void animateHoverTo(qreal target);
    void animateArrowTo(qreal target);
    bool isPopupActive() const;

private:
    QList<ImageDropdownItem> m_items;
    int m_currentIndex = -1;
    QString m_placeholderText = QStringLiteral("Select");
    int m_popupMinWidth = 280;
    int m_popupColumns = 2;
    QSize m_popupCardSize = QSize(136, 104);
    int m_popupMaxHeight = 420;

    ImageDropdownPopup* m_popup = nullptr;
    QList<QMetaObject::Connection> m_scrollAreaPositionConnections;
    qreal m_hoverProgress = 0.0;
    qreal m_pressProgress = 0.0;
    qreal m_arrowProgress = 0.0;
    QPropertyAnimation* m_hoverAnim = nullptr;
    QPropertyAnimation* m_pressAnim = nullptr;
    QPropertyAnimation* m_arrowAnim = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_IMAGEDROPDOWNSELECTOR_H
