// SPDX-License-Identifier: MPL-2.0

// ListCollapseAnimator.h
#ifndef RUWA_UI_WIDGETS_LISTCOLLAPSEANIMATOR_H
#define RUWA_UI_WIDGETS_LISTCOLLAPSEANIMATOR_H

#include <QObject>
#include <QVector>
#include <functional>

class QBoxLayout;
class QWidget;

namespace ruwa::ui::widgets {

/**
 * @brief Smoothly collapses a contiguous run of widgets out of a QBoxLayout.
 *
 * Reusable building block for "animation-ready" lists: instead of deleting rows
 * abruptly, snapshot the doomed widgets, drop a shrinking/fading placeholder in
 * their layout slot, and let the box layout slide the neighbours together as the
 * placeholder's height animates to zero. The originals are hidden immediately and
 * deleteLater()'d, so the caller can update its model right away.
 *
 * Several collapses may run concurrently. Drive scroll-geometry refreshes off the
 * \ref stepped signal, which fires once per animation frame.
 */
class ListCollapseAnimator : public QObject {
    Q_OBJECT

public:
    explicit ListCollapseAnimator(QObject* parent = nullptr);
    ~ListCollapseAnimator() override;

    bool isAnimating() const { return !m_active.isEmpty(); }

    /// Default collapse duration when \p durationMs <= 0.
    static constexpr int kDefaultDurationMs = 240;

    /**
     * Collapse layout items in the inclusive index range [startIndex, endIndex].
     * Widget items in the range are hidden and deleteLater()'d; spacer items are
     * removed; a single shrinking, fading snapshot of the widgets takes the slot.
     *
     * @param layout     box layout owning the items (must be vertical for the
     *                   expected visual, but any box layout is accepted).
     * @param content    widget the items are laid out in (snapshot source).
     * @param startIndex first layout item index to collapse.
     * @param endIndex   last layout item index to collapse (inclusive).
     * @param durationMs animation duration; <= 0 uses \ref kDefaultDurationMs.
     * @param onFinished run once the collapse completes (next event loop), or
     *                   synchronously if there is nothing collapsible.
     */
    void collapseRange(QBoxLayout* layout, QWidget* content, int startIndex, int endIndex,
        int durationMs = 0, std::function<void()> onFinished = {});

    /// Snap every in-flight collapse to its end state immediately, running each
    /// onFinished callback. Call before structurally rebuilding the layout.
    void finishAll();

signals:
    /// Emitted on each animation frame (any active collapse), so owners can
    /// refresh dependent geometry (e.g. a scroll area's content size).
    void stepped();

private:
    struct ActiveCollapse;
    void finishCollapse(ActiveCollapse* collapse, bool jumpToEnd);

    QVector<ActiveCollapse*> m_active;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_LISTCOLLAPSEANIMATOR_H
