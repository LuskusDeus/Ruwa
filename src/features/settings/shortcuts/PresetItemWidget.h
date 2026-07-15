// SPDX-License-Identifier: MPL-2.0

// PresetItemWidget.h
#ifndef RUWA_UI_TABS_CONTENT_PRESETITEMWIDGET_H
#define RUWA_UI_TABS_CONTENT_PRESETITEMWIDGET_H

#include <QRect>
#include <QString>
#include <QWidget>

class QVariantAnimation;
class QLineEdit;
class QPaintEvent;
class QMouseEvent;
class QEnterEvent;
class QKeyEvent;
class QResizeEvent;

namespace ruwa::ui::widgets {

/**
 * @brief A card in the Presets panel.
 *
 * Custom-painted, like the layouts popup items. Built-in cards show a lock
 * icon trailing the title. Custom cards reveal inline Edit and Trash icon
 * buttons on the right when hovered; the Edit button starts inline rename via
 * an overlaid QLineEdit.
 */
class PresetItemWidget : public QWidget {
    Q_OBJECT

public:
    enum class Kind { BuiltIn, Custom };

    explicit PresetItemWidget(QWidget* parent = nullptr);
    ~PresetItemWidget() override;

    void setTitle(const QString& title);
    void setShortcutCount(int count);
    void setKind(Kind kind);

    bool isSelected() const { return m_selected; }
    void setSelected(bool selected);
    /// Set selection state without playing the transition animation.
    void setSelectedImmediate(bool selected);

    /// Start inline rename programmatically (custom presets only).
    void startRenameEditing();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void clicked();
    /// User finished editing the title with a non-empty, changed value.
    void renamed(const QString& newName);
    /// User pressed the trash button (custom presets only).
    void deleteRequested();
    /// User pressed the export button (custom presets only).
    void exportRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void startHoverAnimation(bool entering);
    void startSelectionAnimation(bool selecting);
    void startButtonHoverAnimation(QVariantAnimation*& anim, qreal* progress, bool entering);

    int actionsCount() const;
    QRect editButtonRect() const;
    QRect exportButtonRect() const;
    QRect deleteButtonRect() const;
    void updateButtonHoverStates(const QPoint& pos);

    void finishEditing(bool accept);
    void positionEditor();

    QString m_title;
    int m_count { 0 };
    Kind m_kind { Kind::BuiltIn };

    bool m_selected { false };
    qreal m_hoverProgress { 0.0 };
    qreal m_selectionProgress { 0.0 };

    bool m_editing { false };
    QLineEdit* m_editor { nullptr };

    qreal m_editHoverProgress { 0.0 };
    qreal m_exportHoverProgress { 0.0 };
    qreal m_deleteHoverProgress { 0.0 };

    QVariantAnimation* m_hoverAnim { nullptr };
    QVariantAnimation* m_selectionAnim { nullptr };
    QVariantAnimation* m_editHoverAnim { nullptr };
    QVariantAnimation* m_exportHoverAnim { nullptr };
    QVariantAnimation* m_deleteHoverAnim { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_TABS_CONTENT_PRESETITEMWIDGET_H
