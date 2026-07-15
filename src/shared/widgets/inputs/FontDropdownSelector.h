// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_COMMON_FONTDROPDOWNSELECTOR_H
#define RUWA_UI_WIDGETS_COMMON_FONTDROPDOWNSELECTOR_H

#include <QList>
#include <QPointer>
#include <QMetaObject>
#include <QString>
#include <QStringList>
#include <QWidget>

class QEnterEvent;
class QEvent;
class QFocusEvent;
class QHideEvent;
class QKeyEvent;
class QMouseEvent;
class QObject;
class QPaintEvent;
class QPropertyAnimation;

namespace ruwa::ui::widgets {

class FontDropdownPopup;

class FontDropdownSelector : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal pressProgress READ pressProgress WRITE setPressProgress)
    Q_PROPERTY(qreal arrowProgress READ arrowProgress WRITE setArrowProgress)

public:
    explicit FontDropdownSelector(QWidget* parent = nullptr);
    ~FontDropdownSelector() override;

    QString currentFamily() const { return m_currentFamily; }
    void setCurrentFamily(const QString& family);

    void setFontFamilies(const QStringList& families);
    QStringList fontFamilies() const;

    void setPlaceholderText(const QString& text);
    QString placeholderText() const { return m_placeholderText; }

    void setPopupMinWidth(int width);
    int popupMinWidth() const { return m_popupMinWidth; }

    void setPopupMaxHeight(int height);
    int popupMaxHeight() const { return m_popupMaxHeight; }

    void setOpacityProvider(QWidget* provider);

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal pressProgress() const { return m_pressProgress; }
    void setPressProgress(qreal progress);

    qreal arrowProgress() const { return m_arrowProgress; }
    void setArrowProgress(qreal progress);

signals:
    void currentFamilyChanged(const QString& family);
    void activated(const QString& family);
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
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensureFontsLoaded();
    void openPopup();
    void closePopup();
    void togglePopup();
    void syncPopupState();
    void updatePopupPosition();
    void connectScrollAreaPositionUpdates();
    void disconnectScrollAreaPositionUpdates();
    void animateHoverTo(qreal target);
    void animateArrowTo(qreal target);
    bool isPopupActive() const;
    qreal inheritedOpacity() const;

private:
    QStringList m_families;
    QString m_currentFamily;
    QString m_placeholderText = QStringLiteral("Font");
    int m_popupMinWidth = 280;
    int m_popupMaxHeight = 360;
    bool m_fontsLoaded = false;

    FontDropdownPopup* m_popup = nullptr;
    QPointer<QWidget> m_opacityProvider;
    QList<QMetaObject::Connection> m_scrollAreaPositionConnections;

    qreal m_hoverProgress = 0.0;
    qreal m_pressProgress = 0.0;
    qreal m_arrowProgress = 0.0;

    QPropertyAnimation* m_hoverAnim = nullptr;
    QPropertyAnimation* m_pressAnim = nullptr;
    QPropertyAnimation* m_arrowAnim = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_FONTDROPDOWNSELECTOR_H
