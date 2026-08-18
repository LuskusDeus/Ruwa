// SPDX-License-Identifier: MPL-2.0

// CollapsibleSection.h
#ifndef RUWA_SHARED_WIDGETS_LAYOUT_COLLAPSIBLESECTION_H
#define RUWA_SHARED_WIDGETS_LAYOUT_COLLAPSIBLESECTION_H

#include <QMargins>
#include <QString>
#include <QWidget>

class QPropertyAnimation;

namespace ruwa::ui::widgets {

class SectionHeaderButton;

/**
 * @brief A titled group whose content slides open and shut when the header is
 * clicked — the Brushes panel's pack sections generalised to any content
 * widget, so property panels can be grouped without each one re-inventing the
 * header, the height animation and the clipping.
 *
 * The content keeps its natural height at all times and is revealed by growing
 * the clip around it, which is why a collapsing group never squashes the
 * widgets inside it on the way down.
 */
class CollapsibleSection : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int contentHeight READ contentHeight WRITE setContentHeight)

public:
    explicit CollapsibleSection(const QString& title, QWidget* parent = nullptr);

    void setTitle(const QString& title);
    QString title() const;

    /// Takes ownership of @p content. Replacing an existing content widget
    /// deletes the old one.
    void setContentWidget(QWidget* content);
    QWidget* contentWidget() const { return m_content; }

    void setExpanded(bool expanded, bool animated = true);
    bool isExpanded() const { return m_expanded; }

    /// Padding around the content widget, in unscaled pixels.
    void setContentMargins(const QMargins& margins);
    QMargins contentMargins() const { return m_margins; }

    int contentHeight() const { return m_contentHeight; }
    void setContentHeight(int height);

    /// Re-measure the content and, when open, glide to its new height. Call it
    /// after showing or hiding rows inside the content widget.
    void refreshContentHeight(bool animated = true);

signals:
    void toggled(bool expanded);

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void layoutContent();
    int expandedContentHeight() const;
    int contentAreaWidth() const;
    void animateContentHeightTo(int targetHeight);

private:
    SectionHeaderButton* m_header = nullptr;
    QWidget* m_clip = nullptr;
    QWidget* m_content = nullptr;
    QPropertyAnimation* m_expandAnimation = nullptr;

    QMargins m_margins { 0, 6, 0, 2 };
    bool m_expanded = true;
    int m_contentHeight = 0;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_LAYOUT_COLLAPSIBLESECTION_H
