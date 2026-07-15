// SPDX-License-Identifier: MPL-2.0

// CategoryItemWidget.h
#ifndef RUWA_UI_TABS_CONTENT_CATEGORYITEMWIDGET_H
#define RUWA_UI_TABS_CONTENT_CATEGORYITEMWIDGET_H

#include <QIcon>
#include <QString>
#include <QWidget>

class QVariantAnimation;
class QPaintEvent;
class QMouseEvent;
class QEnterEvent;

namespace ruwa::ui::widgets {

/**
 * @brief A row in the Categories panel: icon | (title + subtitle) | count.
 *
 * Idle: no background, no border.
 * Hover: smooth light tint.
 * Selected: animated colour-inverted fill (background gets text colour,
 *           foreground gets background colour).
 */
class CategoryItemWidget : public QWidget {
    Q_OBJECT

public:
    explicit CategoryItemWidget(QWidget* parent = nullptr);
    ~CategoryItemWidget() override;

    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    void setCount(int count);
    void setIcon(const QIcon& icon);

    bool isSelected() const { return m_selected; }
    void setSelected(bool selected);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void startHoverAnimation(bool entering);
    void startSelectionAnimation(bool selecting);

    QString m_title;
    QString m_subtitle;
    int m_count { 0 };
    QIcon m_icon;

    bool m_selected { false };
    qreal m_hoverProgress { 0.0 };
    qreal m_selectionProgress { 0.0 };

    QVariantAnimation* m_hoverAnim { nullptr };
    QVariantAnimation* m_selectionAnim { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_TABS_CONTENT_CATEGORYITEMWIDGET_H
