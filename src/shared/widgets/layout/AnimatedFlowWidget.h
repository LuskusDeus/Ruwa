// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   A N I M A T E D   F L O W   W I D G E T
// ============================================================================

#ifndef RUWA_UI_WIDGETS_ANIMATEDFLOWWIDGET_H
#define RUWA_UI_WIDGETS_ANIMATEDFLOWWIDGET_H

#include <QByteArray>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

#include <functional>

namespace ruwa::ui::widgets {

class AnimatedFlowWidget final : public QWidget {
public:
    enum class LayoutStyle {
        UniformWrap,
        PinnedToolbar,
    };

    enum class ItemDisposal {
        Keep,
        DeleteLater,
    };

    explicit AnimatedFlowWidget(LayoutStyle style, QWidget* parent = nullptr);
    ~AnimatedFlowWidget() override;

    void setFlowSpacing(int horizontal, int vertical);
    void setItems(const QList<QWidget*>& flowItems, const QList<QWidget*>& pinnedItems = {});
    void clearItems(ItemDisposal disposal = ItemDisposal::Keep);

    void setHeightCallback(std::function<void(int)> callback);
    void setReflowAnimated(bool animated) { m_reflowAnimated = animated; }
    void setSeparatorPropertyName(QByteArray propertyName);

    int targetHeightForWidth(int width) const;
    void shutdown();

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Placement {
        QPointer<QWidget> widget;
        QRect rect;
        bool snap = false;
    };

    struct AnimatedTarget {
        QPointer<QWidget> widget;
        QRect rect;
    };

    struct TrackedItem {
        QPointer<QWidget> widget;
        QMetaObject::Connection destroyedConnection;
    };

    static QSize itemSize(const QWidget* widget);
    static int advanceValue(int current, int target);

    int rowHeight() const;
    int maxItemWidth() const;
    int pinnedBlockWidth() const;
    int naturalWidth() const;
    int buildPlacements(int width, QList<Placement>* placements) const;
    int buildUniformPlacements(int width, QList<Placement>* placements) const;
    int buildPinnedToolbarPlacements(int width, QList<Placement>* placements) const;

    void relayout(bool animate);
    void setAnimatedGeometry(QWidget* widget, const QRect& target, bool animate);
    void advanceAnimation();

    void trackItem(QWidget* widget);
    void untrackItem(QWidget* widget);
    void disconnectTrackedItems();
    void pruneDeadState();

    int targetIndex(QWidget* widget) const;
    void setTarget(QWidget* widget, const QRect& rect);
    void removeTarget(QWidget* widget);
    bool isHiddenItem(QWidget* widget) const;
    bool removeHiddenItem(QWidget* widget);
    void addHiddenItem(QWidget* widget);

    LayoutStyle m_style;
    QList<QPointer<QWidget>> m_flowItems;
    QList<QPointer<QWidget>> m_pinnedItems;
    QList<AnimatedTarget> m_targets;
    QList<QPointer<QWidget>> m_hiddenItems;
    QList<TrackedItem> m_trackedItems;
    std::function<void(int)> m_heightCallback;
    QByteArray m_separatorPropertyName;
    int m_horizontalSpacing = 2;
    int m_verticalSpacing = 2;
    int m_targetHeight = -1;
    int m_currentHeight = -1;
    bool m_initialized = false;
    bool m_reflowAnimated = true;
    bool m_shuttingDown = false;
    QTimer m_animationTimer;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_ANIMATEDFLOWWIDGET_H
