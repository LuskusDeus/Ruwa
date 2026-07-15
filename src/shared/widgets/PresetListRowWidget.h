// SPDX-License-Identifier: MPL-2.0

// PresetListRowWidget.h
#ifndef RUWA_UI_WIDGETS_PRESETLISTROWWIDGET_H
#define RUWA_UI_WIDGETS_PRESETLISTROWWIDGET_H

#include "PresetMenuTypes.h"
#include "shell/context-menu/IContextMenuProvider.h"

#include <QWidget>
#include <QPropertyAnimation>
#include <QVariant>
#include <QVector>

class QLineEdit;
class QPaintEvent;
class QMouseEvent;
class QEnterEvent;

namespace ruwa::ui::widgets {

/**
 * @brief One row: title, optional extra actions, rename, delete.
 * Action order from the title: … extras → rename → delete (right edge).
 */
class PresetListRowWidget : public QWidget, public ruwa::ui::widgets::IContextMenuProvider {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal selectionProgress READ selectionProgress WRITE setSelectionProgress)
    Q_PROPERTY(qreal pressProgress READ pressProgress WRITE setPressProgress)
    Q_PROPERTY(qreal renameHoverProgress READ renameHoverProgress WRITE setRenameHoverProgress)
    Q_PROPERTY(qreal deleteHoverProgress READ deleteHoverProgress WRITE setDeleteHoverProgress)

public:
    explicit PresetListRowWidget(const PresetMenuItem& item, QWidget* parent = nullptr);
    ~PresetListRowWidget() override;

    /// Logical icon glyph size (before ThemeManager scaling) for rename/delete toolbar glyphs.
    static constexpr int sideActionGlyphBasePx() { return 15; }

    const PresetMenuItem& item() const { return m_item; }
    void setItem(const PresetMenuItem& item);

    QString text() const { return m_item.title; }
    void setText(const QString& text);
    QString subtitle() const { return m_item.subtitle; }
    void setSubtitle(const QString& text);
    QString badgeText() const { return m_item.badgeText; }
    void setBadgeText(const QString& text);
    void setBadgeTint(const QColor& tint);
    void setPreviewColors(const QVector<QColor>& colors);
    void setPreviewImage(const QImage& image);
    void setPreviewIcon(ruwa::ui::core::IconProvider::StandardIcon icon);

    bool isSelected() const { return m_isSelected; }
    void setSelected(bool selected);

    bool isActive() const { return m_isActive; }
    void setActive(bool active);

    bool isDeletable() const { return m_isDeletable; }
    void setDeletable(bool deletable);

    bool isRenamable() const { return m_isRenamable; }
    void setRenamable(bool renamable);

    void setExtraActions(QVector<PresetMenuExtraAction> actions);
    void setContextMenuEnabled(bool enabled);
    bool isContextMenuEnabled() const { return m_contextMenuEnabled; }

    void setPopupChromeStyle(bool enabled);
    bool popupChromeStyle() const { return m_popupChromeStyle; }

    bool isEditing() const { return m_isEditing; }
    void startEditing();

    QVariant userData() const { return m_item.userData; }
    void setUserData(const QVariant& data) { m_item.userData = data; }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal selectionProgress() const { return m_selectionProgress; }
    void setSelectionProgress(qreal progress);

    qreal pressProgress() const { return m_pressProgress; }
    void setPressProgress(qreal progress);

    qreal renameHoverProgress() const { return m_renameHoverProgress; }
    void setRenameHoverProgress(qreal progress);

    qreal deleteHoverProgress() const { return m_deleteHoverProgress; }
    void setDeleteHoverProgress(qreal progress);

    // IContextMenuProvider
    ContextMenuType contextMenuType() const override;
    QVariantMap contextMenuContext() const override;
    void handleContextMenuAction(int actionId) override;

signals:
    void clicked();
    void deleteRequested();
    void renameFinished(const QString& newText);
    /// Emitted when a row extra action (see PresetMenuExtraAction::id) is activated.
    void extraActionTriggered(int actionId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void setupAnimations();
    void updateScaledSize();
    void finishEditing(bool accept);
    void markLayoutDirty();
    void ensureActionLayout() const;
    void updateEditorStyle();
    void syncActionButtons();
    void updateInlineActionHover(const QPoint& pos);
    void clearInlineActionState();

    int horizontalPaddingPx() const;
    int contentLeftInset() const;
    int rightReservedWidth() const;
    int hitExtraIndex(const QPoint& pos) const;
    bool hitRenameAction(const QPoint& pos) const;
    bool hitDeleteAction(const QPoint& pos) const;
    bool isOverAnyAction(const QPoint& pos) const;

    void drawStarToggle(
        QPainter& painter, const QRect& btnRect, bool checked, bool hovered, bool pressed) const;
    QSize previewSizePx() const;
    void drawPreview(QPainter& painter, const QRectF& rect, const QColor& borderColor) const;

private slots:
    void onThemeChanged();

private:
    PresetMenuItem m_item;

    bool m_isSelected = false;
    bool m_isActive = false;
    bool m_isDeletable = false;
    bool m_isRenamable = false;

    QVector<PresetMenuExtraAction> m_extraActions;
    QVector<bool> m_extraHovered;
    QVector<bool> m_extraPressed;
    bool m_contextMenuEnabled = false;

    bool m_isHovered = false;
    bool m_isPressed = false;
    bool m_isEditing = false;

    qreal m_hoverProgress = 0.0;
    qreal m_selectionProgress = 0.0;
    qreal m_pressProgress = 0.0;
    qreal m_renameHoverProgress = 0.0;
    qreal m_deleteHoverProgress = 0.0;

    QPropertyAnimation* m_hoverAnimation = nullptr;
    QPropertyAnimation* m_selectionAnimation = nullptr;
    QPropertyAnimation* m_pressAnimation = nullptr;
    QPropertyAnimation* m_renameHoverAnimation = nullptr;
    QPropertyAnimation* m_deleteHoverAnimation = nullptr;
    QLineEdit* m_editor = nullptr;

    mutable bool m_layoutDirty = true;
    mutable QRect m_layoutRename;
    mutable QRect m_layoutDelete;
    mutable QVector<QRect> m_layoutExtras;
    bool m_renameHovered = false;
    bool m_deleteHovered = false;
    bool m_renamePressed = false;
    bool m_deletePressed = false;
    bool m_popupChromeStyle = false;

    static constexpr int BASE_HEIGHT = 54;
    static constexpr int BASE_PADDING_H = 12;
    static constexpr int BASE_PADDING_V = 8;
    static constexpr int BASE_ICON_BTN = 26;
    static constexpr int BASE_BTN_GAP = 4;
    static constexpr int BASE_CORNER_RADIUS = 9;
    static constexpr int BASE_ACTIVE_BAR_WIDTH = 3;
    static constexpr int BASE_ACTIVE_GAP = 10;
    static constexpr int BASE_PREVIEW_SIZE = 34;
    static constexpr int BASE_PREVIEW_GAP = 12;
    static constexpr int BASE_BADGE_HEIGHT = 18;
    static constexpr int BASE_BADGE_HPAD = 8;
    static constexpr int ANIMATION_DURATION = 150;
};

} // namespace ruwa::ui::widgets

#endif
