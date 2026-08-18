// SPDX-License-Identifier: MPL-2.0

// AnchorGridSelector.h
#ifndef RUWA_SHARED_WIDGETS_INPUTS_ANCHORGRIDSELECTOR_H
#define RUWA_SHARED_WIDGETS_INPUTS_ANCHORGRIDSELECTOR_H

#include <QPainterPath>
#include <QWidget>

class QVariantAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief A 3x3 square of cells naming one of nine reference points — the nine
 * corner/edge/centre anchors a piece of content can be aligned to.
 *
 * Cells are indexed row-major from the top-left (0) to the bottom-right (8);
 * anchor() maps the same choice onto a Qt::Alignment for callers that would
 * rather think in alignment flags than indices. Index -1 means no cell is
 * current, which is the honest state whenever the thing being anchored sits
 * somewhere none of the nine cells describes.
 *
 * Clicking reports the cell and nothing more: the highlight is not moved by the
 * click, because it stands for where the content actually ended up, which only
 * the owner can know. The owner sets it via setCurrentIndex() once the action
 * has settled.
 *
 * The widget only reports the chosen anchor — what "align to it" means is the
 * caller's business, so the same picker serves layer positioning, canvas
 * resizing and anything else that anchors content to a box.
 */
class AnchorGridSelector : public QWidget {
    Q_OBJECT

public:
    explicit AnchorGridSelector(QWidget* parent = nullptr);

    /// 0..8, or -1 when no cell describes the current state.
    int currentIndex() const { return m_currentIndex; }
    /// Anything outside 0..8 clears the highlight.
    void setCurrentIndex(int index, bool animated = true);

    /// The current cell as an alignment pair, e.g. Qt::AlignLeft | Qt::AlignTop.
    /// Empty when no cell is current.
    Qt::Alignment anchor() const;
    void setAnchor(Qt::Alignment alignment, bool animated = true);

    static Qt::Alignment alignmentForIndex(int index);
    static int indexForAlignment(Qt::Alignment alignment);

    /// Overall side length of the square, in unscaled pixels. The cells and
    /// gaps are derived from it, so this is the one knob a host needs.
    void setBaseSize(int size);
    int baseSize() const { return m_baseSize; }

    /// Side length in final, already-scaled pixels, for a host that needs the
    /// square to match something it has measured (a neighbouring column of
    /// controls, say). Overrides baseSize until set back to 0.
    void setSideLength(int pixels);
    int sideLength() const { return m_explicitSide; }

    QSize sizeHint() const override;

signals:
    /// The user picked a cell. Also emitted when the already-current cell is
    /// clicked again: aligning content is an action, and repeating it is
    /// meaningful even when the anchor did not move.
    void anchorClicked(int index);
    /// The current cell changed, by click or by setCurrentIndex().
    void anchorChanged(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onThemeChanged();

private:
    /// Cell rect in widget coordinates, or a null rect for an out-of-range index.
    QRectF cellRect(int index) const;
    /// The cell as a path rounded only where it faces the outside of the grid,
    /// so the nine cells together read as one rounded square.
    QPainterPath cellPath(int index) const;
    /// Geometry of the selection highlight for a cell at appear/disappear
    /// progress @p amount (0 = just gone/not yet shown, 1 = fully settled).
    QRectF selectionOverlayRect(int index, qreal amount) const;
    /// @p rect traced with rounding only on the corner(s) of cell @p index that
    /// face the outside of the grid — the shape both the cell's own fill and
    /// its inset selection highlight are built from, so an edge or middle cell
    /// never grows a round corner that has nothing outside it to round away
    /// from.
    QPainterPath outwardRoundedRect(const QRectF& rect, int index, qreal radius) const;
    int indexAtPosition(const QPointF& pos) const;
    void setHoveredIndex(int index);
    /// Retargets every cell's selection-highlight animation towards 1.0 for
    /// @p index (or towards 0.0 for all nine when @p index is -1), each
    /// continuing from wherever it currently sits. The same per-cell approach
    /// setHoveredIndex uses, and for the same reason: a single shared progress
    /// cannot express "this cell is still fading out while that one fades in",
    /// and a change that arrives before the previous one finished would have
    /// nowhere to remember the interrupted cell's mid-fade state.
    void retargetSelectionAnimations(int index, bool animated);
    void updateScaledSize();
    qreal cellSpan() const;

private:
    int m_baseSize = 88;
    int m_explicitSide = 0;
    int m_currentIndex = -1; // nothing highlighted until the owner says otherwise
    int m_hoveredIndex = -1;
    int m_pressedIndex = -1;

    /// Per-cell hover weight. One shared progress cannot express "this cell is
    /// still fading out while that one fades in", so sweeping across the grid
    /// kept restarting the incoming cell from nothing.
    qreal m_cellHover[9] = {};
    QVariantAnimation* m_cellHoverAnimations[9] = {};

    /// Per-cell selection-highlight weight; same reasoning as m_cellHover, and
    /// what lets one cell's highlight shrink away while a different cell's
    /// grows in, independently, even across back-to-back changes.
    qreal m_cellSelection[9] = {};
    QVariantAnimation* m_cellSelectionAnimations[9] = {};
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_INPUTS_ANCHORGRIDSELECTOR_H
