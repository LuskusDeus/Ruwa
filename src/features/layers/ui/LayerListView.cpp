// SPDX-License-Identifier: MPL-2.0

// LayerListView.cpp
#include "LayerListView.h"
#include "LayerRowWidget.h"
#include "app/TabletToMouseEventFilter.h"
#include "shared/widgets/reorderlist/AnimatedListLayout.h"
#include "shared/widgets/reorderlist/ListDragDrop.h"

#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"

#include <QPixmap>
#include <QImage>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QApplication>
#include <QTimer>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QCursor>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <limits>

namespace ruwa::ui::widgets {

using namespace ruwa::core::layers;

namespace {

constexpr auto kUiDragActiveProperty = "ruwa_ui_drag_active";

void setUiDragActive(bool active)
{
    if (qApp) {
        qApp->setProperty(kUiDragActiveProperty, active);
        if (active) {
            qApp->setOverrideCursor(Qt::SizeAllCursor);
        } else if (const QCursor* cursor = qApp->overrideCursor();
            cursor && cursor->shape() == Qt::SizeAllCursor) {
            qApp->restoreOverrideCursor();
        }
    }
}

int scaledRowHeight(LayerRowWidget* row)
{
    if (!row)
        return 0;
    return ruwa::ui::core::ThemeManager::instance().scaled(row->effectiveRowHeight());
}

void prepareMaskedRowAnimation(LayerRowWidget* row, qreal opacity = 1.0)
{
    if (!row)
        return;
    row->setFixedHeight(scaledRowHeight(row));
    row->setRowOpacity(opacity);
}

void setRowVisibleMask(LayerRowWidget* row, int visibleHeight)
{
    if (!row)
        return;

    const int width = qMax(0, row->width());
    const int fullHeight = qMax(row->height(), scaledRowHeight(row));
    const int clampedHeight = qBound(0, visibleHeight, fullHeight);

    row->setFixedHeight(fullHeight);

    if (clampedHeight >= fullHeight) {
        row->clearMask();
        return;
    }

    row->setMask(QRegion(0, 0, width, clampedHeight));
}

int currentRowVisibleHeight(LayerRowWidget* row)
{
    if (!row)
        return 0;

    const int fullHeight = qMax(row->height(), scaledRowHeight(row));
    if (row->rowOpacity() <= 0.001) {
        return 0;
    }

    const QRegion mask = row->mask();
    if (mask.isEmpty()) {
        return fullHeight;
    }

    return qBound(0, mask.boundingRect().height(), fullHeight);
}

int groupRevealDurationMs(int animatedSpanPx)
{
    return qBound(180, qRound(110.0 + qMax(0, animatedSpanPx) * 0.85), 520);
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

LayerListView::LayerListView(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);

    // Main layout: just holds the scroll area
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Scroll area
    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    mainLayout->addWidget(m_scrollArea);

    // Content widget (inside scroll area)
    m_contentWidget = new QWidget();
    m_contentWidget->setObjectName("layer_list_content");
    m_contentWidget->setAttribute(Qt::WA_TranslucentBackground);
    m_contentWidget->installEventFilter(this);
    m_scrollArea->setWidget(m_contentWidget);

    // Layout manager
    m_layout = new AnimatedListLayout(m_contentWidget, this);
    connect(m_layout, &AnimatedListLayout::contentHeightChanged, this,
        &LayerListView::onContentHeightChanged);

    // Drag & drop
    m_dragDrop = new ListDragDrop(m_contentWidget, m_layout, this);
    connect(m_dragDrop, &ListDragDrop::dragCompleted, this, &LayerListView::onDragCompleted);
    connect(m_dragDrop, &ListDragDrop::dragCancelled, this, &LayerListView::onDragCancelled);
    connect(m_dragDrop, &ListDragDrop::sourceRowCollapseRequested, this,
        &LayerListView::onSourceRowCollapseRequested);
    connect(m_dragDrop, &ListDragDrop::ghostSettled, this, &LayerListView::onGhostSettled);
    connect(
        m_dragDrop, &ListDragDrop::multiDragCompleted, this, &LayerListView::onMultiDragCompleted);
    connect(m_scrollArea, &SmoothScrollArea::scrolled, this, [this](int) {
        if (m_dragDrop && m_dragDrop->isDragging()) {
            m_dragDrop->refreshDropTarget(QCursor::pos());
        }
    });

    // Tree-list configuration: rows nest, so feed indent metrics and a resolver
    // that clamps the drop index (Background stays pinned at the bottom) and
    // derives nesting depth from the pointer X and neighbouring rows.
    m_dragDrop->setIndentMetrics(LayerRowWidget::kIndentPerLevel, LayerRowWidget::kPad);
    m_dragDrop->setFallbackRowHeight(LayerRowWidget::kRowHeight);
    m_dragDrop->setDropResolver(
        [this](int rawIndex, int suggestedDepth,
            const QSet<LayerId>& draggedIds) -> ListDragDrop::DropResolution {
            using namespace ruwa::core::layers;

            int newIndex = rawIndex;

            // Background is fixed at the bottom: insertion after it is not allowed.
            int backgroundFlatIndex = -1;
            for (int i = 0; i < m_layout->entryCount(); ++i) {
                auto* w = static_cast<LayerRowWidget*>(m_layout->rowWidgetAtIndex(i));
                if (w && w->layerData() && w->layerData()->isBackground()) {
                    backgroundFlatIndex = i;
                    break;
                }
            }
            if (backgroundFlatIndex >= 0) {
                newIndex = qMin(newIndex, backgroundFlatIndex);
            }

            // Determine max allowed depth based on the row ABOVE the insertion point
            int maxDepth = 0;
            const int aboveIdx = newIndex - 1;
            const LayerData* aboveData = nullptr;
            if (aboveIdx >= 0) {
                auto* aboveWidget
                    = static_cast<LayerRowWidget*>(m_layout->rowWidgetAtIndex(aboveIdx));
                if (aboveWidget && aboveWidget->layerData()) {
                    aboveData = aboveWidget->layerData();
                    maxDepth = aboveData->depth;
                    // Only expanded groups accept drops "into" themselves.
                    if (aboveData->isGroup() && aboveData->expanded) {
                        maxDepth = aboveData->depth + 1;
                    }
                }
            }

            const LayerData* belowData = nullptr;
            if (newIndex >= 0 && newIndex < m_layout->entryCount()) {
                auto* belowWidget
                    = static_cast<LayerRowWidget*>(m_layout->rowWidgetAtIndex(newIndex));
                if (belowWidget && belowWidget->layerData()) {
                    belowData = belowWidget->layerData();
                }
            }

            int newDepth = qBound(0, suggestedDepth, maxDepth);

            // Inside a contiguous sibling run the insertion is unambiguously between
            // layers of the same parent. X-based outdent is only meaningful at group
            // boundaries; in the middle it can make the visual gap disagree with the
            // final model move. Keep the old outdent affordance for the dragged last
            // child itself: while dragging it, the insert point can still be reported
            // between the previous sibling and the source row.
            const bool aboveAcceptsChildDrop = aboveData && aboveData->isGroup()
                && aboveData->expanded && !draggedIds.contains(aboveData->id);
            const auto isDraggedLastSibling = [&draggedIds](const LayerData* data) {
                return data && draggedIds.contains(data->id) && data->parent
                    && data->nextSibling() == nullptr;
            };
            const bool draggedLastSiblingBoundary
                = isDraggedLastSibling(aboveData) || isDraggedLastSibling(belowData);
            if (aboveAcceptsChildDrop) {
                newDepth = aboveData->depth + 1;
            } else if (!draggedLastSiblingBoundary && aboveData && belowData
                && aboveData->parent == belowData->parent && aboveData->depth == belowData->depth) {
                newDepth = aboveData->depth;
            }

            return { newIndex, newDepth };
        });

    m_clipPreviewIndicator = new DropIndicatorWidget(m_contentWidget);
    m_clipPreviewIndicator->setIndentMetrics(LayerRowWidget::kIndentPerLevel, LayerRowWidget::kPad);
    m_clipPreviewIndicator->setStyle(DropIndicatorWidget::Style::ClippingBlock);
    m_clipPreviewIndicator->setIndicatorIcon(ruwa::ui::core::IconProvider::instance().getPixmap(
        ruwa::ui::core::IconProvider::StandardIcon::ArrowDown, QSize(12, 12)));
    m_clipPreviewIndicator->setFixedWidth(m_contentWidget->width());
    m_clipPreviewIndicator->hide();

    m_thumbnailBatchTimer.setSingleShot(true);
    connect(&m_thumbnailBatchTimer, &QTimer::timeout, this, &LayerListView::processThumbnailBatch);
    m_thumbnailRenderWatcher = new QFutureWatcher<QImage>(this);
    connect(m_thumbnailRenderWatcher, &QFutureWatcher<QImage>::finished, this,
        &LayerListView::applyThumbnailRenderResult);

    setupMouseTracking();
}

LayerListView::~LayerListView()
{
    if (m_thumbnailRenderWatcher) {
        m_thumbnailRenderWatcher->waitForFinished();
    }
    // Clean up pool
    qDeleteAll(m_rowPool);
    m_rowPool.clear();
}

// ============================================================================
// Model
// ============================================================================

void LayerListView::setModel(LayerModel* model)
{
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
        if (m_model->selectionManager()) {
            disconnect(m_model->selectionManager(), nullptr, this, nullptr);
        }
    }

    m_model = model;
    m_thumbnailBatchTimer.stop();
    m_thumbnailQueue.clear();
    m_thumbnailQueuedIds.clear();
    m_thumbnailGenerations.clear();
    m_thumbnailRenderingLayerId = LayerId();
    m_thumbnailRenderingGeneration = 0;

    if (m_model) {
        connect(m_model, &LayerModel::layersChanged, this, &LayerListView::onLayersChanged);
        connect(m_model, &LayerModel::layerAboutToBeRemoved, this,
            &LayerListView::onLayerAboutToBeRemoved);
        connect(m_model, &LayerModel::selectionChanged, this, &LayerListView::onSelectionChanged);
        connect(m_model, &LayerModel::groupExpansionChanged, this,
            &LayerListView::onGroupExpansionChanged);
        connect(m_model, &LayerModel::layerDataChanged, this, [this](const LayerId& id) {
            LayerData* changedLayer = m_model->layerById(id);
            if (changedLayer && changedLayer->thumbnailDirty) {
                invalidateThumbnails(QList<LayerId> { id });
            }
            // Refresh specific row data
            auto it = m_rowMap.find(id);
            if (it != m_rowMap.end() && it.value()) {
                if (changedLayer) {
                    // Re-apply data so row-level animations/state (clip offset, depth, etc.)
                    // react to property changes, not just repaint.
                    it.value()->setLayerData(changedLayer);
                } else {
                    it.value()->update();
                }
            }

            if (changedLayer && changedLayer->hasChildren()) {
                QList<LayerData*> descendants;
                changedLayer->flatten(descendants, false);
                for (LayerData* descendant : descendants) {
                    if (!descendant) {
                        continue;
                    }
                    auto descendantIt = m_rowMap.find(descendant->id);
                    if (descendantIt != m_rowMap.end() && descendantIt.value()) {
                        descendantIt.value()->setLayerData(descendant);
                    }
                }
            }
        });

        // Connect selection manager for per-layer updates
        auto* sm = m_model->selectionManager();
        connect(sm, &LayerSelectionManager::layerSelectionStateChanged, this,
            &LayerListView::onLayerSelectionStateChanged);
    }

    rebuildRowWidgets();
}

void LayerListView::setCanvasSize(const QSize& size)
{
    setDisplayFrame(QRect(0, 0, size.width(), size.height()));
}

void LayerListView::setDisplayFrame(const QRect& frame)
{
    if (m_displayFrame == frame)
        return;
    m_displayFrame = frame;

    QList<LayerId> idsToRefresh;
    for (auto* row : m_activeRows) {
        if (!row) {
            continue;
        }
        row->setDisplayFrame(m_displayFrame);
        if (LayerData* data = row->layerData()) {
            idsToRefresh.append(data->id);
        }
    }
    invalidateThumbnails(idsToRefresh, false);
}

void LayerListView::refreshThumbnailPreviews()
{
    invalidateVisibleThumbnails();
}

void LayerListView::setThumbnailLoadingMode(bool active)
{
    if (m_thumbnailLoadingMode == active) {
        return;
    }

    m_thumbnailLoadingMode = active;
    for (auto* row : m_activeRows) {
        if (!row) {
            continue;
        }
        row->setThumbnailLoading(shouldShowThumbnailLoading(row->layerData()));
    }
}

void LayerListView::setForcedThumbnailLoadingLayer(const LayerId& id)
{
    if (m_forcedThumbnailLoadingLayerId == id) {
        return;
    }

    const LayerId previousId = m_forcedThumbnailLoadingLayerId;
    m_forcedThumbnailLoadingLayerId = id;

    const auto updateRowLoadingState = [this](const LayerId& layerId) {
        if (layerId.isNull()) {
            return;
        }
        auto it = m_rowMap.find(layerId);
        if (it == m_rowMap.end() || !it.value()) {
            return;
        }
        LayerRowWidget* row = it.value();
        row->setThumbnailLoading(shouldShowThumbnailLoading(row->layerData()));
        row->update();
    };

    updateRowLoadingState(previousId);
    updateRowLoadingState(m_forcedThumbnailLoadingLayerId);

    if (!m_forcedThumbnailLoadingLayerId.isNull()) {
        enqueueThumbnail(m_forcedThumbnailLoadingLayerId, true);
        scheduleThumbnailBatch();
    }
}

void LayerListView::invalidateVisibleThumbnails()
{
    QList<LayerId> ids;
    ids.reserve(m_activeRows.size());
    for (auto* row : m_activeRows) {
        if (!row || !row->isVisible() || !isRowInViewport(row)) {
            continue;
        }
        if (LayerData* data = row->layerData()) {
            ids.append(data->id);
        }
    }
    invalidateThumbnails(ids);
}

void LayerListView::invalidateThumbnails(const QList<LayerId>& ids, bool prioritize)
{
    if (!m_model || ids.isEmpty()) {
        return;
    }

    for (const LayerId& id : ids) {
        if (id.isNull()) {
            continue;
        }
        LayerData* data = m_model->layerById(id);
        if (!data) {
            continue;
        }

        data->thumbnailDirty = !data->isGroup() && !data->isAdjustment();
        data->maskThumbnailDirty = true;
        if (data->thumbnailDirty) {
            m_thumbnailGenerations[id] = m_nextThumbnailGeneration++;
        }
        if (m_thumbnailLoadingMode && data->thumbnailDirty) {
            data->thumbnail = QPixmap();
        }

        auto it = m_rowMap.find(id);
        if (it != m_rowMap.end() && it.value()) {
            it.value()->setThumbnailLoading(shouldShowThumbnailLoading(data));
            it.value()->update();
        }

        if (data->thumbnailDirty) {
            enqueueThumbnail(id, prioritize);
        }
    }
    scheduleThumbnailBatch();
}

// ============================================================================
// Resize
// ============================================================================

void LayerListView::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);

    // Re-layout all rows with new container width
    if (!m_activeRows.isEmpty() && m_contentWidget) {
        int w = m_contentWidget->width();
        for (auto* row : m_activeRows) {
            if (row)
                row->resize(w, row->height());
        }
        // Sync content height in case the layout's content height
        // was already correct but the widget was stale
        syncContentHeight();
    }

    if (m_clipPreviewIndicator && m_contentWidget) {
        m_clipPreviewIndicator->setFixedWidth(m_contentWidget->width());
    }
}

// ============================================================================
// Event Filter — forward mouse to DragDrop during drag
// ============================================================================

bool LayerListView::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_contentWidget && event->type() == QEvent::Resize) {
        if (!m_activeRows.isEmpty()) {
            const int w = m_contentWidget->width();
            for (auto* row : m_activeRows) {
                if (row) {
                    row->resize(w, row->height());
                }
            }
        }

        if (m_clipPreviewIndicator) {
            m_clipPreviewIndicator->setFixedWidth(m_contentWidget->width());
        }
    }

    // Eye swipe mode — track mouse across rows to toggle visibility
    if (m_eyeSwiping) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            QPoint globalPos = me->globalPosition().toPoint();
            LayerRowWidget* row = rowAtGlobalPos(globalPos);
            if (row && row->layerData()) {
                LayerId id = row->layerId();
                if (!m_eyeSwipedIds.contains(id)) {
                    m_eyeSwipedIds.insert(id);
                    // Set visibility to match swipe direction
                    bool targetVisible = m_eyeSwipeToVisible;
                    if (row->layerData()->visible != targetVisible) {
                        emit layerVisibilityToggled(id);
                    }
                }
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            m_eyeSwiping = false;
            m_eyeSwipedIds.clear();
            qApp->removeEventFilter(this);
            m_clipPreviewFilterInstalled = false;
            return true;
        }
    }

    // Alt preview for clipping masks (inactive while dragging)
    if (!m_eyeSwiping && !m_dragActive && m_dragDrop && !m_dragDrop->isDragging()) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            updateClipPreviewAt(me->globalPosition().toPoint());
        } else if (event->type() == QEvent::KeyRelease) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Alt) {
                clearClipPreview();
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (obj != m_contentWidget) {
                return QWidget::eventFilter(obj, event);
            }
            auto* me = static_cast<QMouseEvent*>(event);
            // Re-evaluate target at release position so clicks in row spacing
            // can still resolve a valid clipping base layer.
            updateClipPreviewAt(me->globalPosition().toPoint());
            if (me->button() == Qt::LeftButton && (me->modifiers() & Qt::AltModifier)
                && m_clipPreviewActive && !m_clipPreviewBaseLayerId.isNull()) {
                emit clipSelectionRequested(m_clipPreviewBaseLayerId);
                me->accept();
                return true;
            }
        } else if (event->type() == QEvent::Leave) {
            clearClipPreview();
        }
    }

    // Drag mode
    if (m_dragActive && m_dragDrop) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_dragDrop->updateDrag(me->globalPosition().toPoint());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease
            && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            auto* me = static_cast<QMouseEvent*>(event);
            // Stop forwarding mouse events BEFORE endDrag starts settle animation.
            // Otherwise mouse moves during settle keep calling updateDrag(),
            // pulling the ghost back toward the cursor.
            m_dragActive = false;
            setUiDragActive(false);
            qApp->removeEventFilter(this);
            m_clipPreviewFilterInstalled = false;
            const bool copyDropRequested = m_copyDragActive || (me->modifiers() & Qt::AltModifier);
            if (copyDropRequested) {
                m_copyDragActive = true;
                m_settlingDrag = true;
                m_dragDrop->setCopyMode(true);
            }
            m_dragDrop->endDrag(me->globalPosition().toPoint());
            if (m_copyDragActive) {
                animateCopyDragSourceRestore();
            }
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                m_dragDrop->cancelDrag();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ============================================================================
// Model Slots
// ============================================================================

void LayerListView::onLayerAboutToBeRemoved(const LayerId& id)
{
    m_thumbnailQueue.removeAll(id);
    m_thumbnailQueuedIds.remove(id);
    m_thumbnailGenerations.remove(id);

    // Clear m_data FIRST so no code path (including render) can hit use-after-free.
    // Snapshot is taken before clear for removal animation; if render fails/skips, we still have
    // null.
    auto it = m_rowMap.find(id);
    if (it == m_rowMap.end() || !it.value())
        return;
    LayerRowWidget* row = it.value();

    // Snapshot while data still valid (for removal animation)
    if (row->width() > 0 && row->height() > 0) {
        QPixmap snapshot(row->size() * row->devicePixelRatioF());
        snapshot.setDevicePixelRatio(row->devicePixelRatioF());
        snapshot.fill(Qt::transparent);
        row->render(&snapshot);
        row->setRemovalSnapshot(snapshot);
    }
    row->setLayerData(nullptr);
}

void LayerListView::onLayersChanged()
{
    clearClipPreview();
    if (m_rebuilding)
        return;

    // If model changes arrive while "new layer insert" animation is active,
    // stop it first so stale animation callbacks can't fight with fresh layout.
    if (m_creationAnimating) {
        if (m_creationAnim) {
            disconnect(m_creationAnim, nullptr, this, nullptr);
            m_creationAnim->stop();
            m_creationAnim->deleteLater();
            m_creationAnim = nullptr;
        }
        m_creationAnimating = false;

        for (auto* row : m_activeRows) {
            if (!row)
                continue;
            row->clearMask();
            row->setRowOpacity(1.0);
            row->show();
            row->setFixedHeight(scaledRowHeight(row));
        }
    }

    if (m_groupAnimating || m_multiDragAnimating || m_deferCopyDragRebuild) {
        m_rebuildPending = true;
        return;
    }
    m_rebuildPending = false;

    // If delete animation is running, merge new removals or cancel and rebuild.
    // Handles rapid delete spam and undo-of-remove (layers added back).
    if (m_deleteAnimating) {
        QSet<LayerId> newIds;
        for (LayerData* layer : m_model->flattenedLayers()) {
            if (layer)
                newIds.insert(layer->id);
        }
        QSet<LayerId> mergedRemoved;
        for (auto it = m_rowMap.begin(); it != m_rowMap.end(); ++it) {
            if (!newIds.contains(it.key()))
                mergedRemoved.insert(it.key());
        }
        if (!mergedRemoved.isEmpty()) {
            animateLayerRemoval(mergedRemoved);
        } else {
            // Layers were added (e.g. undo of remove) — stop delete anim and rebuild
            if (m_deleteAnim) {
                disconnect(m_deleteAnim, nullptr, this, nullptr);
                m_deleteAnim->stop();
                m_deleteAnim->deleteLater();
                m_deleteAnim = nullptr;
            }
            m_deleteAnimating = false;
            m_skipNextAnimation = true;
            rebuildRowWidgets();
            syncContentHeight();
        }
        return;
    }

    // Detect removed layers for delete animation
    QSet<LayerId> newIds;
    for (LayerData* layer : m_model->flattenedLayers()) {
        if (layer)
            newIds.insert(layer->id);
    }
    QSet<LayerId> removedIds;
    for (auto it = m_rowMap.begin(); it != m_rowMap.end(); ++it) {
        if (!newIds.contains(it.key())) {
            removedIds.insert(it.key());
        }
    }

    if (!removedIds.isEmpty()) {
        animateLayerRemoval(removedIds);
        return;
    }

    rebuildRowWidgets();
}

void LayerListView::onSelectionChanged(const LayerId& primaryId)
{
    Q_UNUSED(primaryId);
    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }
    syncSelectionState();
    clearClipPreview();
}

void LayerListView::onLayerSelectionStateChanged(const LayerId& id, bool selected)
{
    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }
    auto it = m_rowMap.find(id);
    if (it != m_rowMap.end() && it.value()) {
        if (m_settlingDrag || m_multiDragAnimating) {
            it.value()->setSelectedImmediate(selected);
        } else {
            it.value()->setSelected(selected);
        }
    }
}

void LayerListView::onGroupExpansionChanged(const LayerId& id, bool expanded)
{
    if (expanded) {
        animateGroupExpand(id);
    } else {
        animateGroupCollapse(id);
    }
}

// ============================================================================
// Row Slots
// ============================================================================

void LayerListView::onRowClicked(const LayerId& id, Qt::KeyboardModifiers mods)
{
    // Block clicks during drag settle animation
    if (m_settlingDrag)
        return;

    // Close right-expand menus on other rows when interacting with any layer
    for (auto* row : m_activeRows) {
        if (row->layerId() != id) {
            row->closeRightExpandMenu();
        }
    }

    if ((mods & Qt::AltModifier) && !m_dragActive && m_dragDrop && !m_dragDrop->isDragging()) {
        int insertIdx = -1;
        LayerId baseId;
        if (resolveClipPreviewTarget(id, &insertIdx, &baseId)) {
            emit clipSelectionRequested(baseId);
            return;
        }
    }

    emit layerSelected(id, mods);
}

void LayerListView::onRowThumbnailCtrlClicked(const LayerId& id)
{
    if (m_settlingDrag || !m_model)
        return;

    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }
    emit layerContentSelectionRequested(id);
}

void LayerListView::onRowTextEditRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive || !m_model)
        return;

    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }
    emit layerTextEditRequested(id);
}

void LayerListView::onRowDoubleClicked(const LayerId& id)
{
    Q_UNUSED(id);
    // Double click starts rename, handled by LayerRowWidget itself
}

void LayerListView::onRowExpandToggled(const LayerId& id)
{
    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }
    emit layerExpandToggled(id);
}

void LayerListView::onRowVisibilityToggled(const LayerId& id)
{
    emit layerVisibilityToggled(id);
}

void LayerListView::onRowAlphaLockClicked(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }
    emit layerAlphaLockClicked(id);
}

void LayerListView::onRowLockClicked(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }
    emit layerLockClicked(id);
}

void LayerListView::onRowClipSwipeRequested(const LayerId& id, bool leftToRight)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit clipSwipeRequested(id, leftToRight);
}

void LayerListView::onRowRightExpandDuplicateClicked(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerDuplicateRequested(id);
}

void LayerListView::onRowRightExpandDeleteClicked(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerDeleteRequested(id);
}

void LayerListView::onRowQuickClippingMaskRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerQuickClippingMaskRequested(id);
}

void LayerListView::onRowClearPixelsRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerClearPixelsRequested(id);
}

void LayerListView::onRowRasterizeSmartRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerRasterizeSmartRequested(id);
}

void LayerListView::onRowApplyMaskRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerApplyMaskRequested(id);
}

void LayerListView::onRowInvertMaskRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerInvertMaskRequested(id);
}

void LayerListView::onRowApplyEffectsRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerApplyEffectsRequested(id);
}

void LayerListView::onRowToggleAlphaLockRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerToggleAlphaLockRequested(id);
}

void LayerListView::onRowToggleLockRequested(const LayerId& id)
{
    if (m_settlingDrag || m_dragActive)
        return;
    emit layerToggleLockRequested(id);
}

void LayerListView::onRowEyePressed(const LayerId& id, bool wasVisible)
{
    if (m_settlingDrag || m_dragActive)
        return;

    // Toggle visibility immediately on press
    emit layerVisibilityToggled(id);

    // Enter eye-swipe mode
    m_eyeSwiping = true;
    m_eyeSwipeToVisible = !wasVisible; // opposite of what it was
    m_eyeSwipedIds.clear();
    m_eyeSwipedIds.insert(id); // Already toggled this one

    qApp->installEventFilter(this);
    ruwa::TabletToMouseEventFilter::ensureRunsFirst(qApp);
}

void LayerListView::onRowDragInitiated(const LayerId& id, const QPoint& globalPos)
{
    if (!m_model || m_dragDrop->isDragging())
        return;

    // Block new drags while settle animation is playing or layout is animating
    if (m_settlingDrag || m_layout->isAnimating())
        return;

    for (auto* row : m_activeRows) {
        row->closeRightExpandMenu();
    }

    clearClipPreview();
    m_copyDragActive = (QApplication::keyboardModifiers() & Qt::AltModifier);

    auto it = m_rowMap.find(id);
    if (it == m_rowMap.end())
        return;
    auto* sourceLayer = m_model->layerById(id);
    if (!sourceLayer || sourceLayer->isBackground())
        return;
    if (sourceLayer->locked)
        return;

    // Get all selected IDs for multi-drag
    const auto& selectedIds = m_model->selectedLayerIds();

    // If the dragged layer is not selected, select it first
    QSet<LayerId> dragSet;
    if (selectedIds.contains(id)) {
        dragSet = selectedIds;
    } else {
        dragSet.insert(id);
    }

    // Background layer is fixed; locked layers cannot be moved.
    for (auto itId = dragSet.begin(); itId != dragSet.end();) {
        auto* layer = m_model->layerById(*itId);
        if (!layer || layer->isBackground() || layer->locked) {
            itId = dragSet.erase(itId);
        } else {
            ++itId;
        }
    }
    if (dragSet.isEmpty())
        return;

    // If a selected layer already rides inside another selected ancestor,
    // treat it as part of that ancestor, not as a separate drag root.
    for (auto itId = dragSet.begin(); itId != dragSet.end();) {
        if (*itId == id) {
            ++itId;
            continue;
        }

        auto* layer = m_model->layerById(*itId);
        bool hasSelectedAncestor = false;
        for (LayerData* parent = layer ? layer->parent : nullptr; parent; parent = parent->parent) {
            if (dragSet.contains(parent->id)) {
                hasSelectedAncestor = true;
                break;
            }
        }

        if (hasSelectedAncestor) {
            itId = dragSet.erase(itId);
        } else {
            ++itId;
        }
    }
    if (dragSet.isEmpty())
        return;

    // Collect visible descendants for ALL dragged layers that are expanded groups
    QSet<LayerId> descendantIds;
    for (const LayerId& dragId : dragSet) {
        auto* layer = m_model->layerById(dragId);
        if (layer && layer->isGroup() && layer->expanded) {
            QList<LayerData*> descendants;
            layer->flatten(descendants, true); // respect sub-group expansion
            for (auto* d : descendants) {
                descendantIds.insert(d->id);
            }
        }
    }

    // Mark all dragged rows (all selected + all descendants)
    QSet<LayerId> allDragIds = dragSet;
    allDragIds.unite(descendantIds);
    for (const LayerId& dragId : allDragIds) {
        auto rowIt = m_rowMap.find(dragId);
        if (rowIt != m_rowMap.end() && rowIt.value()) {
            rowIt.value()->setDragging(true);
        }
    }

    m_dragDrop->startDrag(id, dragSet, it.value(), globalPos, descendantIds, m_copyDragActive);

    // Install app-level event filter to capture all mouse events during drag
    m_dragActive = true;
    setUiDragActive(true);
    qApp->installEventFilter(this);
    ruwa::TabletToMouseEventFilter::ensureRunsFirst(qApp);
}

void LayerListView::onRowRenameFinished(const LayerId& id, const QString& newName)
{
    emit layerRenamed(id, newName);
}

// ============================================================================
// Drag Slots
// ============================================================================

void LayerListView::onDragCompleted(const LayerId& movedId, int dropInsertIndex)
{
    // Legacy path - no longer the primary completion mechanism.
    m_dragActive = false;
    setUiDragActive(false);
    qApp->removeEventFilter(this);

    for (auto* row : m_activeRows) {
        row->setDragging(false);
    }

    emit layerDragDropped(movedId, dropInsertIndex, 0);
}

void LayerListView::onDragCancelled()
{
    clearClipPreview();
    m_dragActive = false;
    setUiDragActive(false);
    m_settlingDrag = false;
    m_multiDragAnimating = false;
    m_deferCopyDragRebuild = false;
    m_copyDragActive = false;
    qApp->removeEventFilter(this);
    m_clipPreviewFilterInstalled = false;

    // Stop any collapse animation
    if (m_collapseAnim) {
        m_collapseAnim->stop();
        m_collapseAnim = nullptr;
    }

    if (m_copyRestoreAnim) {
        m_copyRestoreAnim->stop();
        m_copyRestoreAnim->deleteLater();
        m_copyRestoreAnim = nullptr;
    }

    // Stop any multi-drag animation
    if (m_multiDragAnim) {
        m_multiDragAnim->stop();
        m_multiDragAnim->deleteLater();
        m_multiDragAnim = nullptr;
    }

    // Clear drag-end layout state
    m_layout->clearDragEndState();

    for (auto* row : m_activeRows) {
        row->setDragging(false);
        row->setRowOpacity(1.0);
        // Restore correct height based on layer type
        auto& tm = ruwa::ui::core::ThemeManager::instance();
        int scaledH = tm.scaled(row->effectiveRowHeight());
        row->setFixedHeight(scaledH);
        row->clearMask();
        row->show();
    }

    // Force rebuild to get clean state + correct content height
    m_skipNextAnimation = true;
    rebuildRowWidgets();
}

void LayerListView::onSourceRowCollapseRequested(const LayerId& sourceId)
{
    // Block new interactions during the settle phase
    m_settlingDrag = true;

    if (m_copyDragActive) {
        if (m_layout && m_dragDrop && m_dragDrop->dropInsertIndex() >= 0
            && m_layout->dragEndGapY() < 0) {
            const QSet<LayerId> sourceIds = m_dragDrop->allExcludeIds();
            m_layout->applyCopyDragEndState(
                m_dragDrop->dropInsertIndex(), draggedRowsGapHeight(sourceIds));
        }
        return;
    }

    // Find the source row widget and animate its height to 0
    auto it = m_rowMap.find(sourceId);
    if (it == m_rowMap.end() || !it.value())
        return;

    LayerRowWidget* sourceRow = it.value();

    // Collect all rows to collapse (source + descendants)
    QList<LayerRowWidget*> rowsToCollapse;
    rowsToCollapse.append(sourceRow);

    if (m_dragDrop) {
        QSet<LayerId> excludeIds = m_dragDrop->allExcludeIds();
        for (const LayerId& descId : excludeIds) {
            if (descId == sourceId)
                continue; // already added
            auto descIt = m_rowMap.find(descId);
            if (descIt != m_rowMap.end() && descIt.value()) {
                rowsToCollapse.append(descIt.value());
            }
        }
    }

    animateSourceRowCollapse(rowsToCollapse);
}

void LayerListView::onGhostSettled(const LayerId& movedId, int dropInsertIndex, int targetDepth)
{
    clearClipPreview();

    m_dragActive = false;
    setUiDragActive(false);
    qApp->removeEventFilter(this);
    m_clipPreviewFilterInstalled = false;

    // Stop collapse animation if still running
    if (m_collapseAnim) {
        m_collapseAnim->stop();
        m_collapseAnim = nullptr;
    }

    if (m_copyRestoreAnim) {
        m_copyRestoreAnim->stop();
        m_copyRestoreAnim->deleteLater();
        m_copyRestoreAnim = nullptr;
        if (m_copyDragActive && m_dragDrop) {
            const QSet<LayerId> sourceIds = m_dragDrop->allExcludeIds();
            for (auto* row : m_activeRows) {
                if (row && sourceIds.contains(row->layerId())) {
                    row->setRowOpacity(1.0);
                }
            }
        }
    }

    // Reset all drag states
    for (auto* row : m_activeRows) {
        row->setDragging(false);
    }

    if (m_copyDragActive) {
        m_copyDragActive = false;
        m_multiDragAnimating = false;
        m_multiDragPendingSourceId = LayerId();
        m_multiDragPendingDragIds.clear();
        m_multiDragPendingDropIdx = -1;
        m_layout->clearDragEndState();
        m_skipNextAnimation = true;
        m_rebuildPending = true;
        m_deferCopyDragRebuild = true;
        emit layerDragCopyDropped(movedId, dropInsertIndex, targetDepth);
        m_deferCopyDragRebuild = false;

        if (m_dropRejected) {
            m_dropRejected = false;
            m_rebuildPending = false;
            if (m_dragDrop)
                m_dragDrop->animateGhostFadeOut();
            m_settlingDrag = false;
            return;
        }

        if (m_rebuildPending) {
            m_rebuildPending = false;
            rebuildRowWidgets();
        }
        syncContentHeight();

        if (m_dragDrop) {
            QTimer::singleShot(0, this, [this]() {
                if (m_dragDrop)
                    m_dragDrop->destroyGhost();
            });
        }

        m_settlingDrag = false;
        return;
    }

    // ---- Multi-drag path: ghost has morphed into cards, now commit + expand ----
    if (m_multiDragAnimating && m_multiDragPendingDropIdx >= 0) {
        LayerId srcId = m_multiDragPendingSourceId;
        QSet<LayerId> dragIds = m_multiDragPendingDragIds;
        int capturedDropIdx = m_multiDragPendingDropIdx;
        int capturedDepth = m_multiDragPendingDepth;

        // Clear pending data
        m_multiDragPendingSourceId = LayerId();
        m_multiDragPendingDragIds.clear();
        m_multiDragPendingDropIdx = -1;

        // Clear drag-end layout state
        m_layout->clearDragEndState();

        // Save positions BEFORE model change for expand animation
        QHash<LayerId, int> positionsBeforeMove;
        for (auto* row : m_activeRows) {
            if (!dragIds.contains(row->layerId())) {
                positionsBeforeMove[row->layerId()] = row->y();
            }
        }
        int contentHBeforeMove = m_contentWidget->height();

        // Commit the move.
        // Keep m_multiDragAnimating = true so onLayersChanged doesn't rebuild;
        // we'll rebuild manually below.
        m_skipNextAnimation = true;
        m_rebuildPending = true;
        emit layerDragDropped(srcId, capturedDropIdx, capturedDepth);

        if (m_dropRejected) {
            m_dropRejected = false;
            m_multiDragAnimating = false;
            m_rebuildPending = false;
            m_layout->clearDragEndState();
            if (m_dragDrop)
                m_dragDrop->animateGhostFadeOut();
            return;
        }

        // Now do rebuild ourselves
        m_multiDragAnimating = false;
        m_rebuildPending = false;
        rebuildRowWidgets();

        // Phase 3: expand rows at new positions
        animateMultiDragExpand(dragIds, positionsBeforeMove, contentHBeforeMove);

        // Destroy ghost after rows are shown — avoids flicker gap (ghost gone, rows not yet
        // painted)
        if (m_dragDrop) {
            QTimer::singleShot(0, this, [this]() {
                if (m_dragDrop)
                    m_dragDrop->destroyGhost();
            });
        }
        return;
    }

    // ---- Single-drag path (unchanged) ----
    // Ghost has arrived at target position with full opacity.
    // Now commit the move in the model — this will rebuild the list
    // and the new row will seamlessly replace the ghost.

    // Collect all IDs to place instantly (moved layer + its descendants)
    QSet<LayerId> allMovedIds;
    allMovedIds.insert(movedId);
    if (m_dragDrop) {
        allMovedIds.unite(m_dragDrop->allExcludeIds());
    }

    // Clear the drag-end layout state before rebuild
    m_layout->clearDragEndState();

    // Mark all moved layers to be placed instantly (no slide animation)
    // so they appear at their new positions seamlessly replacing the ghost
    m_instantPlaceIds.unite(allMovedIds);

    // Skip ALL animation on the next rebuild
    m_skipNextAnimation = true;

    // Emit the drag dropped signal (this triggers model update in LayersPanel)
    m_rebuildPending = true;
    emit layerDragDropped(movedId, dropInsertIndex, targetDepth);

    if (m_dropRejected) {
        m_dropRejected = false;
        m_rebuildPending = false;
        m_instantPlaceIds.clear();
        if (m_dragDrop)
            m_dragDrop->animateGhostFadeOut();
        m_settlingDrag = false;
        return;
    }

    // If the model didn't change (same-position drop), force rebuild
    if (m_rebuildPending) {
        m_rebuildPending = false;
        rebuildRowWidgets();
    }

    // Ensure content height is correct after drag completion
    syncContentHeight();

    // Destroy ghost after rows are shown — avoids flicker (ghost gone, layer not yet painted)
    if (m_dragDrop) {
        QTimer::singleShot(0, this, [this]() {
            if (m_dragDrop)
                m_dragDrop->destroyGhost();
        });
    }

    m_settlingDrag = false;
}

// ============================================================================
// Multi-Drag Animation
// ============================================================================

void LayerListView::onMultiDragCompleted(
    const LayerId& sourceId, const QSet<LayerId>& draggedIds, int dropInsertIndex, int targetDepth)
{
    clearClipPreview();
    m_dragActive = false;
    setUiDragActive(false);
    qApp->removeEventFilter(this);
    m_clipPreviewFilterInstalled = false;
    m_settlingDrag = true;

    // Stop collapse animation if running from single-drag path
    if (m_collapseAnim) {
        m_collapseAnim->stop();
        m_collapseAnim = nullptr;
    }

    // Block rebuilds during animation
    m_multiDragAnimating = true;

    // Collect all IDs (dragged + their descendants) for complete set
    QSet<LayerId> allDragIds = draggedIds;
    if (m_dragDrop) {
        allDragIds.unite(m_dragDrop->allExcludeIds());
    }

    // Snapshot dragged rows BEFORE collapse.
    // Temporarily disable drag state so rows render at full opacity.
    if (m_dragDrop) {
        QList<QPixmap> snapshots;
        QList<int> sourceYs;
        for (auto* row : m_activeRows) {
            if (!allDragIds.contains(row->layerId()))
                continue;
            if (!row->isVisible())
                continue;

            row->setDragging(false);
            row->setRowOpacity(1.0);

            QPixmap snap(row->size() * row->devicePixelRatioF());
            snap.setDevicePixelRatio(row->devicePixelRatioF());
            snap.fill(Qt::transparent);
            row->render(&snap);

            row->setDragging(true);

            snapshots.append(snap);
            sourceYs.append(row->y());
        }
        if (!snapshots.isEmpty()) {
            m_dragDrop->feedMultiDragSnapshots(snapshots, sourceYs);
        }
    }

    // Apply layout with gap immediately so ghost can fly right away (no delay).
    // Alt-drag copies layers, so the originals stay in the layout.
    if (m_copyDragActive) {
        if (m_layout->dragEndGapY() < 0) {
            m_layout->applyCopyDragEndState(dropInsertIndex, draggedRowsGapHeight(allDragIds));
        }
    } else {
        m_layout->applyDragEndState(allDragIds, dropInsertIndex);
    }

    // Store pending data for onGhostSettled (ghost settle may finish before collapse)
    m_multiDragPendingSourceId = sourceId;
    m_multiDragPendingDragIds = allDragIds;
    m_multiDragPendingDropIdx = dropInsertIndex;
    m_multiDragPendingDepth = targetDepth;

    // Start ghost fly immediately (parallel with collapse — no 200ms delay)
    bool immediateCommit = false;
    if (m_dragDrop) {
        auto& tm = ruwa::ui::core::ThemeManager::instance();
        int rowH = tm.scaled(LayerRowWidget::kRowHeight);
        int space = tm.scaled(m_layout->rowSpacing());
        immediateCommit = m_dragDrop->animateMultiGhostSettle(
            0, rowH, space, true /* layout already applied */);
    }

    // Phase 1: collapse dragged rows — skip when immediate commit (all layers already in position)
    if (!immediateCommit && !m_copyDragActive) {
        animateMultiDragCollapse(allDragIds, sourceId, dropInsertIndex, targetDepth, true);
    }
}

void LayerListView::animateMultiDragCollapse(const QSet<LayerId>& allDragIds,
    const LayerId& sourceId, int dropInsertIndex, int targetDepth, bool layoutHandlesSlide)
{
    if (m_multiDragAnim) {
        m_multiDragAnim->stop();
        m_multiDragAnim->deleteLater();
        m_multiDragAnim = nullptr;
    }

    auto& tm = ruwa::ui::core::ThemeManager::instance();

    // Collect row widgets to collapse and rows that need to slide
    struct CollapseRowInfo {
        LayerRowWidget* row;
        int startH;
        int startY;
    };
    QList<CollapseRowInfo> collapseRows;

    struct SlideRowInfo {
        LayerRowWidget* row;
        int startY;
        int endY;
    };
    QList<SlideRowInfo> slideRows;

    // Save pre-collapse positions for expand phase
    QHash<LayerId, int> prePositions;
    for (auto* row : m_activeRows) {
        prePositions[row->layerId()] = row->y();
    }
    int preContentH = m_contentWidget->height();

    // Calculate how much space each dragged row takes
    // and where gaps will close
    int spacing = tm.scaled(m_layout->rowSpacing());

    // Identify collapse rows and calculate total removed space
    // Track cumulative removed space above each non-dragged row
    QList<QPair<LayerRowWidget*, int>> rowOrder; // (row, cumulativeRemoved)
    int cumulativeRemoved = 0;

    for (auto* row : m_activeRows) {
        LayerId rid = row->layerId();
        if (allDragIds.contains(rid)) {
            int h = row->height();
            collapseRows.append({ row, h, row->y() });
            cumulativeRemoved += h + spacing;
        } else {
            if (cumulativeRemoved > 0) {
                slideRows.append({ row, row->y(), row->y() - cumulativeRemoved });
            }
        }
    }

    int newContentH = qMax(0, preContentH - cumulativeRemoved);

    // Build animation
    m_multiDragAnim = new QParallelAnimationGroup(this);

    // Collapse: shrink height + fade out for each dragged row
    for (auto& info : collapseRows) {
        auto* row = info.row;

        // Opacity fade
        auto* opAnim = new QPropertyAnimation(row, "rowOpacity", m_multiDragAnim);
        opAnim->setDuration(300);
        opAnim->setEasingCurve(QEasingCurve::InOutCubic);
        opAnim->setStartValue(row->rowOpacity());
        opAnim->setEndValue(0.0);
        m_multiDragAnim->addAnimation(opAnim);

        // Height shrink
        auto* hAnim = new QVariantAnimation(m_multiDragAnim);
        hAnim->setDuration(300);
        hAnim->setEasingCurve(QEasingCurve::InOutCubic);
        hAnim->setStartValue(info.startH);
        hAnim->setEndValue(0);
        connect(hAnim, &QVariantAnimation::valueChanged, row, [row](const QVariant& v) {
            int h = qMax(0, v.toInt());
            row->setFixedHeight(qMax(1, h));
            row->setMask(QRegion(0, 0, row->width(), h));
        });
        m_multiDragAnim->addAnimation(hAnim);
    }

    // Slide and content height: only when layout hasn't been applied yet
    if (!layoutHandlesSlide) {
        for (auto& info : slideRows) {
            auto* posAnim = new QPropertyAnimation(info.row, "pos", m_multiDragAnim);
            posAnim->setDuration(300);
            posAnim->setEasingCurve(QEasingCurve::InOutCubic);
            posAnim->setStartValue(QPoint(0, info.startY));
            posAnim->setEndValue(QPoint(0, info.endY));
            m_multiDragAnim->addAnimation(posAnim);
        }

        auto* contentAnim = new QVariantAnimation(m_multiDragAnim);
        contentAnim->setDuration(300);
        contentAnim->setEasingCurve(QEasingCurve::InOutCubic);
        contentAnim->setStartValue(preContentH);
        contentAnim->setEndValue(newContentH);
        connect(contentAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& v) { m_contentWidget->setFixedHeight(v.toInt()); });
        m_multiDragAnim->addAnimation(contentAnim);
    }

    QSet<LayerId> capturedDragIds = allDragIds;

    connect(m_multiDragAnim, &QParallelAnimationGroup::finished, this, [this, capturedDragIds]() {
        m_multiDragAnim = nullptr;

        // Hide collapsed rows
        for (auto* row : m_activeRows) {
            if (capturedDragIds.contains(row->layerId())) {
                row->hide();
            }
        }

        // Reset drag state on all rows
        for (auto* row : m_activeRows) {
            row->setDragging(false);
        }

        // Ghost settle already started in onMultiDragCompleted (parallel)
        // Pending data already set; onGhostSettled will fire when morph completes
    });

    m_multiDragAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void LayerListView::animateMultiDragExpand(
    const QSet<LayerId>& movedIds, const QHash<LayerId, int>& prePositions, int preContentH)
{
    if (m_multiDragAnim) {
        m_multiDragAnim->stop();
        m_multiDragAnim->deleteLater();
        m_multiDragAnim = nullptr;
    }

    m_multiDragAnimating = true;

    auto& tm = ruwa::ui::core::ThemeManager::instance();
    int spacing = tm.scaled(m_layout->rowSpacing());

    // Collect newly placed rows (the moved ones) and existing rows that shifted
    struct ExpandRowInfo {
        LayerRowWidget* row;
        int finalY;
        int targetH;
    };
    QList<ExpandRowInfo> expandRows;

    struct SlideRowInfo {
        LayerRowWidget* row;
        int startY;
        int finalY;
    };
    QList<SlideRowInfo> slideRows;

    for (auto* row : m_activeRows) {
        LayerId rid = row->layerId();
        if (movedIds.contains(rid)) {
            int targetH = tm.scaled(row->effectiveRowHeight());
            expandRows.append({ row, row->y(), targetH });
        } else if (prePositions.contains(rid)) {
            int oldY = prePositions[rid];
            int newY = row->y();
            if (oldY != newY) {
                slideRows.append({ row, oldY, newY });
            }
        }
    }

    if (expandRows.isEmpty()) {
        // Nothing to animate — just clean up
        m_multiDragAnimating = false;
        m_settlingDrag = false;
        syncContentHeight();
        return;
    }

    int newContentH = m_layout->contentHeight();

    // Place moved rows at full height and opacity immediately (seamless transition)
    for (auto& info : expandRows) {
        info.row->setFixedHeight(info.targetH);
        info.row->clearMask();
        info.row->setRowOpacity(1.0);
        info.row->show();
    }

    // Move shifted rows back to pre-move positions
    for (auto& info : slideRows) {
        info.row->move(0, info.startY);
    }

    // Content at final height (no height expansion animation)
    m_contentWidget->setFixedHeight(newContentH);

    // Build animation
    m_multiDragAnim = new QParallelAnimationGroup(this);

    // Slide shifted rows to their new positions
    for (auto& info : slideRows) {
        auto* posAnim = new QPropertyAnimation(info.row, "pos", m_multiDragAnim);
        posAnim->setDuration(300);
        posAnim->setEasingCurve(QEasingCurve::InOutCubic);
        posAnim->setStartValue(QPoint(0, info.startY));
        posAnim->setEndValue(QPoint(0, info.finalY));
        m_multiDragAnim->addAnimation(posAnim);
    }

    connect(m_multiDragAnim, &QParallelAnimationGroup::finished, this, [this]() {
        m_multiDragAnimating = false;
        m_multiDragAnim = nullptr;
        m_settlingDrag = false;

        // Clear masks and ensure full opacity (no rebuild — avoids flicker)
        for (auto* row : m_activeRows) {
            row->clearMask();
            row->setRowOpacity(1.0);
        }

        syncContentHeight();
    });

    m_multiDragAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// Content Height
// ============================================================================

void LayerListView::onContentHeightChanged(int newHeight)
{
    // During group/creation/delete animation, we manage content height ourselves
    if (m_groupAnimating || m_creationAnimating || m_deleteAnimating)
        return;

    // Always stop any running height animation first to avoid conflicts
    if (m_heightAnim && m_heightAnim->state() == QAbstractAnimation::Running) {
        m_heightAnim->stop();
    }

    int currentHeight = m_contentWidget->height();
    m_targetContentHeight = newHeight;

    const bool animateCopyGrowth = m_copyDragActive && m_settlingDrag && newHeight > currentHeight;

    if (newHeight >= currentHeight && !animateCopyGrowth) {
        // Growing: set immediately to avoid clipping bottom layers
        m_contentWidget->setFixedHeight(newHeight);
    } else {
        // Shrinking always animates. Copy-drag growth also animates so the
        // insertion space opens at the same pace as the row layout.
        if (!m_heightAnim) {
            m_heightAnim = new QVariantAnimation(this);
            m_heightAnim->setDuration(300);
            m_heightAnim->setEasingCurve(QEasingCurve::InOutCubic);
            connect(m_heightAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v) { m_contentWidget->setFixedHeight(v.toInt()); });
            connect(m_heightAnim, &QVariantAnimation::finished, this, [this]() {
                // Ensure we ended at the correct target
                m_contentWidget->setFixedHeight(m_targetContentHeight);
            });
        }

        m_heightAnim->setStartValue(currentHeight);
        m_heightAnim->setEndValue(newHeight);
        m_heightAnim->start();
    }
}

// ============================================================================
// Content Height Sync Helpers
// ============================================================================

void LayerListView::syncContentHeight()
{
    // Force content widget height to match layout's calculated height.
    // Call after animations finish to ensure correct state.
    int layoutHeight = m_layout->contentHeight();
    if (m_heightAnim && m_heightAnim->state() == QAbstractAnimation::Running) {
        m_heightAnim->stop();
    }
    m_targetContentHeight = layoutHeight;
    m_contentWidget->setFixedHeight(layoutHeight);
}

void LayerListView::forceContentHeightImmediate(int height)
{
    if (m_heightAnim && m_heightAnim->state() == QAbstractAnimation::Running) {
        m_heightAnim->stop();
    }
    m_targetContentHeight = height;
    m_contentWidget->setFixedHeight(height);
}

// ============================================================================
// Rebuild
// ============================================================================

void LayerListView::rebuildRowWidgets()
{
    m_rebuilding = true;

    if (!m_model) {
        // Recycle all active rows
        for (auto* row : m_activeRows) {
            recycleRow(row);
        }
        m_activeRows.clear();
        m_rowMap.clear();
        m_layout->updateLayout({}, false);
        syncContentHeight();
        m_rebuilding = false;
        return;
    }

    // Save pre-rebuild state for insert animation
    QHash<LayerId, int> prePositions;
    int preContentH = m_contentWidget->height();
    bool wantAnim = !m_skipNextAnimation && m_insertAnimationsEnabled;

    if (wantAnim) {
        for (auto* row : m_activeRows) {
            if (row)
                prePositions[row->layerId()] = row->y();
        }
    }

    QList<LayerData*> flatList = m_model->flattenedLayers();

    // Build a set of needed IDs
    QSet<LayerId> neededIds;
    for (auto* layer : flatList) {
        neededIds.insert(layer->id);
    }

    // Recycle rows that are no longer needed
    QList<LayerRowWidget*> toRecycle;
    for (auto it = m_rowMap.begin(); it != m_rowMap.end();) {
        if (!neededIds.contains(it.key())) {
            toRecycle.append(it.value());
            it = m_rowMap.erase(it);
        } else {
            ++it;
        }
    }
    for (auto* row : toRecycle) {
        m_activeRows.removeOne(row);
        recycleRow(row);
    }

    // Build new active list, reusing existing rows where possible
    m_activeRows.clear();
    QList<QPair<LayerId, ReorderableRowWidget*>> layoutEntries;
    QSet<LayerId> newEntryIds;

    for (auto* layer : flatList) {
        LayerRowWidget* row = nullptr;

        auto it = m_rowMap.find(layer->id);
        if (it != m_rowMap.end()) {
            row = it.value();
        } else {
            row = getOrCreateRow();
            m_rowMap[layer->id] = row;
            connectRowSignals(row);
            newEntryIds.insert(layer->id);
        }

        row->setLayerData(layer);
        row->setThumbnailLoading(shouldShowThumbnailLoading(layer));
        m_activeRows.append(row);
        layoutEntries.append({ layer->id, row });
    }

    // Update layout with animation
    QSet<LayerId> instantIds = m_instantPlaceIds;
    m_instantPlaceIds.clear();
    m_skipNextAnimation = false;

    // If we have new entries and animation is wanted, do insert expand animation
    if (wantAnim && !newEntryIds.isEmpty() && instantIds.isEmpty() && !m_groupAnimating
        && !m_creationAnimating && !m_settlingDrag) {
        // Place everything at final positions instantly (no animation)
        m_layout->updateLayout(layoutEntries, false, {}, {});
        syncSelectionState();
        // Ensure content height is set before animation starts
        // (the animation will manage it from here)
        forceContentHeightImmediate(m_layout->contentHeight());
        m_rebuilding = false;
        animateNewLayerInsert(newEntryIds, prePositions, preContentH);
        return;
    }

    bool animate = wantAnim;
    m_layout->updateLayout(layoutEntries, animate, newEntryIds, instantIds);

    // Sync selection
    syncSelectionState();

    // Keep immediate sync only for growth/equal heights.
    // For shrink we rely on onContentHeightChanged() animation to avoid
    // clipping the last visible row during delete/collapse transitions.
    if (m_layout->contentHeight() >= m_contentWidget->height()) {
        syncContentHeight();
    }

    m_rebuilding = false;
    queueThumbnailsForActiveRows();
}

void LayerListView::syncSelectionState()
{
    if (!m_model)
        return;

    const auto& selectedIds = m_model->selectedLayerIds();
    LayerId primaryId = m_model->selectedLayerId();

    // During drag settle: set selection immediately to avoid flicker from selection animation
    bool immediate = m_settlingDrag || m_multiDragAnimating;

    for (auto* row : m_activeRows) {
        LayerId id = row->layerId();
        if (immediate) {
            row->setSelectedImmediate(selectedIds.contains(id));
            row->setPrimaryImmediate(id == primaryId);
        } else {
            row->setSelected(selectedIds.contains(id));
            row->setPrimary(id == primaryId);
        }
    }
}

void LayerListView::scheduleThumbnailBatch()
{
    if (!m_thumbnailQueue.isEmpty() && !m_thumbnailBatchTimer.isActive()) {
        m_thumbnailBatchTimer.start(0);
    }
}

void LayerListView::processThumbnailBatch()
{
    if (m_rebuilding) {
        scheduleThumbnailBatch();
        return;
    }
    if (m_thumbnailRenderWatcher && m_thumbnailRenderWatcher->isRunning()) {
        return;
    }

    while (!m_thumbnailQueue.isEmpty()) {
        const LayerId id = m_thumbnailQueue.takeFirst();
        m_thumbnailQueuedIds.remove(id);

        if (!m_model) {
            continue;
        }

        LayerData* data = m_model->layerById(id);
        auto rowIt = m_rowMap.find(id);
        LayerRowWidget* row = rowIt != m_rowMap.end() ? rowIt.value() : nullptr;
        if (!data || !row) {
            continue;
        }

        if (!data->thumbnailDirty || data->isAdjustment()) {
            row->setThumbnailLoading(shouldShowThumbnailLoading(data));
            row->update();
            continue;
        }

        const QSize targetSize = LayerRowWidget::thumbnailTargetSize(data, m_displayFrame);
        if (targetSize.width() <= 0 || targetSize.height() <= 0) {
            continue;
        }

        auto snapshot = LayerModel::cloneLayerTree(data, true);
        if (!snapshot) {
            continue;
        }

        if (!m_thumbnailGenerations.contains(id)) {
            m_thumbnailGenerations[id] = m_nextThumbnailGeneration++;
        }

        m_thumbnailRenderingLayerId = id;
        m_thumbnailRenderingGeneration = m_thumbnailGenerations.value(id);
        const QRect displayFrame = m_displayFrame;
        m_thumbnailRenderWatcher->setFuture(QtConcurrent::run(
            [snapshot = std::move(snapshot), displayFrame, targetSize]() -> QImage {
                return LayerRowWidget::buildThumbnailImage(
                    snapshot.get(), displayFrame, targetSize);
            }));
        return;
    }
}

void LayerListView::enqueueThumbnail(const LayerId& id, bool prioritize)
{
    if (id.isNull()) {
        return;
    }
    if (!m_thumbnailGenerations.contains(id)) {
        m_thumbnailGenerations[id] = m_nextThumbnailGeneration++;
    }

    if (m_thumbnailQueuedIds.contains(id)) {
        if (!prioritize) {
            return;
        }
        m_thumbnailQueue.removeAll(id);
    } else {
        m_thumbnailQueuedIds.insert(id);
    }

    if (prioritize) {
        m_thumbnailQueue.prepend(id);
    } else {
        m_thumbnailQueue.append(id);
    }
}

void LayerListView::queueThumbnailsForActiveRows()
{
    for (auto* row : m_activeRows) {
        if (!row) {
            continue;
        }
        LayerData* data = row->layerData();
        if (!data || !data->thumbnailDirty || data->isAdjustment()) {
            row->setThumbnailLoading(shouldShowThumbnailLoading(data));
            continue;
        }
        enqueueThumbnail(data->id, isRowInViewport(row));
        row->setThumbnailLoading(shouldShowThumbnailLoading(data));
    }
    scheduleThumbnailBatch();
}

void LayerListView::applyThumbnailRenderResult()
{
    if (!m_thumbnailRenderWatcher) {
        return;
    }

    const LayerId id = m_thumbnailRenderingLayerId;
    const quint64 generation = m_thumbnailRenderingGeneration;
    m_thumbnailRenderingLayerId = LayerId();
    m_thumbnailRenderingGeneration = 0;

    const QImage image = m_thumbnailRenderWatcher->result();
    if (!m_model || id.isNull() || m_thumbnailGenerations.value(id, 0) != generation) {
        scheduleThumbnailBatch();
        return;
    }

    LayerData* data = m_model->layerById(id);
    auto rowIt = m_rowMap.find(id);
    LayerRowWidget* row = rowIt != m_rowMap.end() ? rowIt.value() : nullptr;
    if (!data || !row || !data->thumbnailDirty) {
        scheduleThumbnailBatch();
        return;
    }

    data->thumbnail = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    data->thumbnailDirty = false;
    row->setThumbnailLoading(shouldShowThumbnailLoading(data));
    row->update();
    scheduleThumbnailBatch();
}

bool LayerListView::shouldShowThumbnailLoading(const LayerData* data) const
{
    if (!data || data->isGroup() || data->isAdjustment()) {
        return false;
    }

    if (!m_forcedThumbnailLoadingLayerId.isNull() && data->id == m_forcedThumbnailLoadingLayerId) {
        return true;
    }

    return m_thumbnailLoadingMode && data->thumbnailDirty;
}

bool LayerListView::isRowInViewport(const LayerRowWidget* row) const
{
    if (!row || !m_scrollArea || !m_scrollArea->viewport()) {
        return false;
    }

    const int scrollY = m_scrollArea->scrollValue();
    const QRect visibleRect(QPoint(0, scrollY), m_scrollArea->viewport()->size());
    return visibleRect.intersects(row->geometry());
}

// ============================================================================
// Row Pool
// ============================================================================

LayerRowWidget* LayerListView::getOrCreateRow()
{
    LayerRowWidget* row;
    if (!m_rowPool.isEmpty()) {
        row = m_rowPool.takeLast();
        row->setLayerData(nullptr);
        row->setDisplayFrame(m_displayFrame);
        row->setSelectedImmediate(false);
        row->setPrimary(false);
        row->setDragging(false);
        row->setRowOpacity(1.0);
        row->show();
    } else {
        row = new LayerRowWidget(m_contentWidget);
        row->setDisplayFrame(m_displayFrame);
    }
    return row;
}

void LayerListView::recycleRow(LayerRowWidget* row)
{
    if (!row)
        return;
    row->cancelRename();
    row->setRemovalSnapshot(QPixmap());
    row->setLayerData(nullptr);
    row->hide();

    // Disconnect all signals from this row
    disconnect(row, nullptr, this, nullptr);

    m_rowPool.append(row);

    // Limit pool size
    while (m_rowPool.size() > 50) {
        delete m_rowPool.takeFirst();
    }
}

void LayerListView::connectRowSignals(LayerRowWidget* row)
{
    connect(row, &LayerRowWidget::clicked, this, &LayerListView::onRowClicked);
    connect(row, &LayerRowWidget::thumbnailCtrlClicked, this,
        &LayerListView::onRowThumbnailCtrlClicked);
    connect(row, &LayerRowWidget::textEditRequested, this, &LayerListView::onRowTextEditRequested);
    connect(row, &LayerRowWidget::doubleClicked, this, &LayerListView::onRowDoubleClicked);
    connect(row, &LayerRowWidget::expandToggled, this, &LayerListView::onRowExpandToggled);
    connect(row, &LayerRowWidget::visibilityToggled, this, &LayerListView::onRowVisibilityToggled);
    connect(row, &LayerRowWidget::dragInitiated, this, &LayerListView::onRowDragInitiated);
    connect(row, &LayerRowWidget::renameFinished, this, &LayerListView::onRowRenameFinished);
    connect(row, &LayerRowWidget::eyePressed, this, &LayerListView::onRowEyePressed);
    connect(row, &LayerRowWidget::alphaLockClicked, this, &LayerListView::onRowAlphaLockClicked);
    connect(row, &LayerRowWidget::lockClicked, this, &LayerListView::onRowLockClicked);
    connect(
        row, &LayerRowWidget::clipSwipeRequested, this, &LayerListView::onRowClipSwipeRequested);
    connect(row, &LayerRowWidget::rightExpandDuplicateClicked, this,
        &LayerListView::onRowRightExpandDuplicateClicked);
    connect(row, &LayerRowWidget::rightExpandDeleteClicked, this,
        &LayerListView::onRowRightExpandDeleteClicked);
    connect(row, &LayerRowWidget::quickClippingMaskRequested, this,
        &LayerListView::onRowQuickClippingMaskRequested);
    connect(row, &LayerRowWidget::clearLayerPixelsRequested, this,
        &LayerListView::onRowClearPixelsRequested);
    connect(row, &LayerRowWidget::rasterizeSmartLayerRequested, this,
        &LayerListView::onRowRasterizeSmartRequested);
    connect(
        row, &LayerRowWidget::applyMaskRequested, this, &LayerListView::onRowApplyMaskRequested);
    connect(
        row, &LayerRowWidget::invertMaskRequested, this, &LayerListView::onRowInvertMaskRequested);
    connect(row, &LayerRowWidget::applyEffectsRequested, this,
        &LayerListView::onRowApplyEffectsRequested);
    connect(row, &LayerRowWidget::toggleAlphaLockRequested, this,
        &LayerListView::onRowToggleAlphaLockRequested);
    connect(row, &LayerRowWidget::toggleLayerLockRequested, this,
        &LayerListView::onRowToggleLockRequested);
}

// ============================================================================
// Group Collapse Animation
// ============================================================================

void LayerListView::animateGroupCollapse(const LayerId& groupId)
{
    // Cancel any running group animation
    if (m_groupAnim) {
        disconnect(m_groupAnim, nullptr, this, nullptr);
        m_groupAnim->stop();
        m_groupAnim->deleteLater();
        m_groupAnim = nullptr;
    }

    m_groupAnimating = true;

    auto* groupData = m_model ? m_model->layerById(groupId) : nullptr;
    if (!groupData) {
        m_groupAnimating = false;
        rebuildRowWidgets();
        return;
    }

    // Collect all descendant IDs (the model already set expanded=false,
    // but we still have the row widgets from before rebuild)
    QList<LayerData*> descendants;
    groupData->flatten(descendants, false); // get ALL descendants regardless of expansion
    QSet<LayerId> descendantIds;
    for (auto* d : descendants) {
        descendantIds.insert(d->id);
    }

    struct RowState {
        int y = 0;
        int visibleHeight = 0;
        qreal opacity = 1.0;
    };

    QHash<LayerId, RowState> oldStates;
    for (auto* row : m_activeRows) {
        oldStates.insert(
            row->layerId(), { row->y(), currentRowVisibleHeight(row), row->rowOpacity() });
    }
    const int oldContentH = m_contentWidget->height();

    // Find descendant row widgets
    QList<LayerRowWidget*> childRows;
    for (auto* row : m_activeRows) {
        if (descendantIds.contains(row->layerId())) {
            childRows.append(row);
        }
    }

    if (childRows.isEmpty()) {
        m_groupAnimating = false;
        rebuildRowWidgets();
        return;
    }

    std::sort(childRows.begin(), childRows.end(),
        [](LayerRowWidget* a, LayerRowWidget* b) { return a && b ? a->y() < b->y() : a < b; });

    struct MovedRowInfo {
        LayerRowWidget* row = nullptr;
        int oldY = 0;
        int finalY = 0;
    };
    QList<MovedRowInfo> movedRows;

    QList<QPair<LayerId, ReorderableRowWidget*>> layoutEntries;
    for (LayerData* layer : m_model->flattenedLayers()) {
        if (!layer)
            continue;
        LayerRowWidget* row = m_rowMap.value(layer->id);
        if (!row)
            continue;
        layoutEntries.append({ layer->id, row });
    }

    m_layout->updateLayout(layoutEntries, false, {}, {});
    const int newContentH = m_layout->contentHeight();

    for (const auto& entry : layoutEntries) {
        auto* row = static_cast<LayerRowWidget*>(entry.second);
        if (!row)
            continue;

        const LayerId rid = row->layerId();
        const auto oldIt = oldStates.find(rid);
        if (oldIt == oldStates.end())
            continue;

        const int finalY = row->y();
        const int oldY = oldIt->y;
        if (oldY != finalY) {
            movedRows.append({ row, oldY, finalY });
            row->move(0, oldY);
        }
    }

    m_contentWidget->setFixedHeight(oldContentH);

    struct ChildAnimInfo {
        LayerRowWidget* row = nullptr;
        int finalY = 0;
        int targetH = 0;
        int startH = 0;
        qreal startOpacity = 1.0;
    };
    QList<ChildAnimInfo> childInfos;
    int blockTop = std::numeric_limits<int>::max();
    int blockBottom = std::numeric_limits<int>::min();
    int startFront = 0;

    for (auto* row : childRows) {
        const auto oldIt = oldStates.find(row->layerId());
        const int startH
            = (oldIt != oldStates.end()) ? oldIt->visibleHeight : currentRowVisibleHeight(row);
        const qreal startOpacity = (oldIt != oldStates.end()) ? oldIt->opacity : row->rowOpacity();
        const int finalY = row->y();
        const int targetH = scaledRowHeight(row);

        childInfos.append({ row, finalY, targetH, startH, startOpacity });
        blockTop = qMin(blockTop, finalY);
        blockBottom = qMax(blockBottom, finalY + targetH);
        startFront = qMax(startFront, (finalY - blockTop) + startH);

        row->setRowOpacity(startOpacity);
        setRowVisibleMask(row, startH);
    }

    const int revealSpan = qMax(1, blockBottom - blockTop);
    const int durationMs = groupRevealDurationMs(qMin(startFront, revealSpan));

    // Build animation group
    m_groupAnim = new QParallelAnimationGroup(this);

    auto* revealAnim = new QVariantAnimation(m_groupAnim);
    revealAnim->setDuration(durationMs);
    revealAnim->setEasingCurve(QEasingCurve::InOutCubic);
    revealAnim->setStartValue(startFront);
    revealAnim->setEndValue(0);
    connect(revealAnim, &QVariantAnimation::valueChanged, this,
        [childInfos, blockTop](const QVariant& v) {
            const int front = v.toInt();
            QEasingCurve opacityCurve(QEasingCurve::OutCubic);
            for (const auto& info : childInfos) {
                if (!info.row)
                    continue;
                const int localFront = front - (info.finalY - blockTop);
                const int visibleHeight = qBound(0, localFront, info.targetH);
                setRowVisibleMask(info.row, visibleHeight);

                const qreal progress = info.targetH > 0
                    ? qBound(0.0,
                          static_cast<qreal>(visibleHeight) / static_cast<qreal>(info.targetH), 1.0)
                    : 1.0;
                const qreal targetOpacity = opacityCurve.valueForProgress(progress);
                info.row->setRowOpacity(qMin(info.startOpacity, targetOpacity));
            }
        });
    m_groupAnim->addAnimation(revealAnim);

    // Slide rows that remain in the collapsed layout
    for (auto& info : movedRows) {
        auto* posAnim = new QPropertyAnimation(info.row, "pos", m_groupAnim);
        posAnim->setDuration(durationMs);
        posAnim->setEasingCurve(QEasingCurve::InOutCubic);
        posAnim->setStartValue(QPoint(0, info.oldY));
        posAnim->setEndValue(QPoint(0, info.finalY));
        m_groupAnim->addAnimation(posAnim);
    }

    // Content height animation (separate from the normal m_heightAnim)
    auto* contentHeightAnim = new QVariantAnimation(m_groupAnim);
    contentHeightAnim->setDuration(durationMs);
    contentHeightAnim->setEasingCurve(QEasingCurve::InOutCubic);
    contentHeightAnim->setStartValue(oldContentH);
    contentHeightAnim->setEndValue(newContentH);
    connect(contentHeightAnim, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) { m_contentWidget->setFixedHeight(v.toInt()); });
    m_groupAnim->addAnimation(contentHeightAnim);

    // On finish: clean up and rebuild
    connect(m_groupAnim, &QParallelAnimationGroup::finished, this, [this]() {
        m_groupAnimating = false;
        m_groupAnim = nullptr;

        // Clear masks on all rows
        for (auto* row : m_activeRows) {
            row->clearMask();
            row->setRowOpacity(1.0);
        }

        // Rebuild and force-sync content height
        rebuildRowWidgets();
        syncContentHeight();
    });

    m_groupAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void LayerListView::animateGroupExpand(const LayerId& groupId)
{
    // Cancel any running group animation
    if (m_groupAnim) {
        disconnect(m_groupAnim, nullptr, this, nullptr);
        m_groupAnim->stop();
        m_groupAnim->deleteLater();
        m_groupAnim = nullptr;
    }

    m_groupAnimating = true;

    auto* groupData = m_model ? m_model->layerById(groupId) : nullptr;
    if (!groupData) {
        m_groupAnimating = false;
        rebuildRowWidgets();
        return;
    }

    struct RowState {
        int y = 0;
        int visibleHeight = 0;
        qreal opacity = 1.0;
    };

    // Save pre-expand state of existing rows
    QHash<LayerId, RowState> oldStates;
    for (auto* row : m_activeRows) {
        oldStates.insert(
            row->layerId(), { row->y(), currentRowVisibleHeight(row), row->rowOpacity() });
    }
    int oldContentH = m_contentWidget->height();

    // Collect descendant IDs (these will be the NEW rows)
    QList<LayerData*> descendants;
    groupData->flatten(descendants, true); // respect expansion of sub-groups
    QSet<LayerId> descendantIds;
    for (auto* d : descendants) {
        descendantIds.insert(d->id);
    }

    // Do rebuild with no animation — this creates children and places everything at final positions
    m_skipNextAnimation = true;
    rebuildRowWidgets();

    // Now find children and record final positions before we move things around
    QList<LayerRowWidget*> childRows;

    struct MovedRowInfo {
        LayerRowWidget* row;
        int oldY;
        int finalY;
    };
    QList<MovedRowInfo> movedRows;

    for (auto* row : m_activeRows) {
        LayerId rid = row->layerId();
        if (descendantIds.contains(rid)) {
            childRows.append(row);
        } else if (oldStates.contains(rid)) {
            int oldY = oldStates[rid].y;
            int finalY = row->y();
            if (oldY != finalY) {
                movedRows.append({ row, oldY, finalY });
            }
        }
    }

    if (childRows.isEmpty()) {
        m_groupAnimating = false;
        return;
    }

    std::sort(childRows.begin(), childRows.end(),
        [](LayerRowWidget* a, LayerRowWidget* b) { return a && b ? a->y() < b->y() : a < b; });

    int newContentH = m_layout->contentHeight();

    struct ChildAnimInfo {
        LayerRowWidget* row = nullptr;
        int finalY = 0;
        int targetH = 0;
        int startH = 0;
        qreal startOpacity = 0.0;
    };
    QList<ChildAnimInfo> childInfos;
    int blockTop = std::numeric_limits<int>::max();
    int blockBottom = std::numeric_limits<int>::min();
    int startFront = 0;

    // Keep row layout fixed and reveal children via mask.
    for (auto* row : childRows) {
        const auto oldIt = oldStates.find(row->layerId());
        const int startH = (oldIt != oldStates.end()) ? oldIt->visibleHeight : 0;
        const qreal startOpacity = (oldIt != oldStates.end()) ? oldIt->opacity : 0.0;
        const int finalY = row->y();
        const int targetH = scaledRowHeight(row);

        childInfos.append({ row, finalY, targetH, startH, startOpacity });
        blockTop = qMin(blockTop, finalY);
        blockBottom = qMax(blockBottom, finalY + targetH);
        if (startH > 0) {
            startFront = qMax(startFront, (finalY - blockTop) + startH);
        }

        prepareMaskedRowAnimation(row, startOpacity);
        setRowVisibleMask(row, startH);
    }

    const int revealSpan = qMax(1, blockBottom - blockTop);
    const int durationMs = groupRevealDurationMs(revealSpan - startFront);

    // Move below-rows back to their old positions (we'll animate them to finalY)
    for (auto& info : movedRows) {
        info.row->move(0, info.oldY);
    }

    // Set content height to old value (will animate to new)
    m_contentWidget->setFixedHeight(oldContentH);

    // Build animation group
    m_groupAnim = new QParallelAnimationGroup(this);

    auto* revealAnim = new QVariantAnimation(m_groupAnim);
    revealAnim->setDuration(durationMs);
    revealAnim->setEasingCurve(QEasingCurve::InOutCubic);
    revealAnim->setStartValue(startFront);
    revealAnim->setEndValue(revealSpan);
    connect(revealAnim, &QVariantAnimation::valueChanged, this,
        [childInfos, blockTop](const QVariant& v) {
            const int front = v.toInt();
            QEasingCurve opacityCurve(QEasingCurve::OutCubic);
            for (const auto& info : childInfos) {
                if (!info.row)
                    continue;
                const int localFront = front - (info.finalY - blockTop);
                const int visibleHeight = qBound(0, localFront, info.targetH);
                setRowVisibleMask(info.row, visibleHeight);

                const qreal progress = info.targetH > 0
                    ? qBound(0.0,
                          static_cast<qreal>(visibleHeight) / static_cast<qreal>(info.targetH), 1.0)
                    : 1.0;
                const qreal targetOpacity = opacityCurve.valueForProgress(progress);
                info.row->setRowOpacity(qMax(info.startOpacity, targetOpacity));
            }
        });
    m_groupAnim->addAnimation(revealAnim);

    // Slide moved rows from old to final positions
    for (auto& info : movedRows) {
        auto* posAnim = new QPropertyAnimation(info.row, "pos", m_groupAnim);
        posAnim->setDuration(durationMs);
        posAnim->setEasingCurve(QEasingCurve::InOutCubic);
        posAnim->setStartValue(QPoint(0, info.oldY));
        posAnim->setEndValue(QPoint(0, info.finalY));
        m_groupAnim->addAnimation(posAnim);
    }

    // Content height animation (parented to group anim)
    auto* contentHeightAnim = new QVariantAnimation(m_groupAnim);
    contentHeightAnim->setDuration(durationMs);
    contentHeightAnim->setEasingCurve(QEasingCurve::InOutCubic);
    contentHeightAnim->setStartValue(oldContentH);
    contentHeightAnim->setEndValue(newContentH);
    connect(contentHeightAnim, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) { m_contentWidget->setFixedHeight(v.toInt()); });
    m_groupAnim->addAnimation(contentHeightAnim);

    // On finish: clean up
    connect(m_groupAnim, &QParallelAnimationGroup::finished, this, [this]() {
        m_groupAnimating = false;
        m_groupAnim = nullptr;

        // Clear masks
        for (auto* row : m_activeRows) {
            row->clearMask();
        }

        // Final rebuild to ensure clean state
        m_skipNextAnimation = true;
        rebuildRowWidgets();
        syncContentHeight();
    });

    m_groupAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// Source Row Collapse (during drag drop)
// ============================================================================

void LayerListView::animateSourceRowCollapse(const QList<LayerRowWidget*>& rows)
{
    if (rows.isEmpty())
        return;

    if (m_collapseAnim) {
        m_collapseAnim->stop();
    }

    m_collapseAnim = new QVariantAnimation(this);
    m_collapseAnim->setDuration(300);
    m_collapseAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_collapseAnim->setStartValue(1.0);
    m_collapseAnim->setEndValue(0.0);

    QList<QPointer<LayerRowWidget>> ptrs;
    for (auto* r : rows) {
        if (r)
            ptrs.append(r);
    }

    connect(m_collapseAnim, &QVariantAnimation::valueChanged, this, [ptrs](const QVariant& v) {
        qreal opacity = v.toReal();
        for (auto& p : ptrs) {
            if (p)
                p->setRowOpacity(opacity);
        }
    });

    connect(m_collapseAnim, &QVariantAnimation::finished, this, [ptrs]() {
        for (auto& p : ptrs) {
            if (p)
                p->hide();
        }
    });

    m_collapseAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void LayerListView::animateCopyDragSourceRestore()
{
    if (!m_dragDrop)
        return;

    if (m_copyRestoreAnim) {
        m_copyRestoreAnim->stop();
        m_copyRestoreAnim->deleteLater();
        m_copyRestoreAnim = nullptr;
    }

    const QSet<LayerId> sourceIds = m_dragDrop->allExcludeIds();
    if (sourceIds.isEmpty())
        return;

    auto* group = new QParallelAnimationGroup(this);
    QList<QPointer<LayerRowWidget>> restoredRows;

    for (auto* row : m_activeRows) {
        if (!row || !sourceIds.contains(row->layerId()))
            continue;

        restoredRows.append(row);
        row->setRowOpacity(qBound<qreal>(0.0, row->rowOpacity() * 0.35, 1.0));
        row->setSelected(false);
        row->setPrimary(false);
        row->setDragging(false);
        row->show();

        auto* opAnim = new QPropertyAnimation(row, "rowOpacity", group);
        opAnim->setDuration(220);
        opAnim->setEasingCurve(QEasingCurve::InOutCubic);
        opAnim->setStartValue(row->rowOpacity());
        opAnim->setEndValue(1.0);
        group->addAnimation(opAnim);
    }

    if (group->animationCount() == 0) {
        group->deleteLater();
        return;
    }

    m_copyRestoreAnim = group;
    connect(group, &QParallelAnimationGroup::finished, this, [this, restoredRows]() {
        m_copyRestoreAnim = nullptr;
        for (const auto& row : restoredRows) {
            if (row) {
                row->setDragging(false);
                row->setRowOpacity(1.0);
            }
        }
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

int LayerListView::draggedRowsGapHeight(const QSet<LayerId>& ids) const
{
    if (ids.isEmpty()) {
        return LayerRowWidget::kRowHeight;
    }

    int totalHeight = 0;
    int rowCount = 0;
    for (auto* row : m_activeRows) {
        if (!row || !ids.contains(row->layerId())) {
            continue;
        }
        totalHeight += row->effectiveRowHeight();
        ++rowCount;
    }

    if (rowCount > 1 && m_layout) {
        totalHeight += (rowCount - 1) * m_layout->rowSpacing();
    }

    return totalHeight > 0 ? totalHeight : LayerRowWidget::kRowHeight;
}

// ============================================================================
// Eye Swipe Helper
// ============================================================================

LayerRowWidget* LayerListView::rowAtGlobalPos(const QPoint& globalPos) const
{
    for (auto* row : m_activeRows) {
        if (!row || !row->isVisible())
            continue;
        QPoint localPos = row->mapFromGlobal(globalPos);
        if (row->rect().contains(localPos)) {
            return row;
        }
    }
    return nullptr;
}

bool LayerListView::resolveClipPreviewTarget(
    const LayerId& hoveredId, int* outInsertIndex, LayerId* outBaseLayerId) const
{
    if (!m_model || hoveredId.isNull()) {
        return false;
    }

    const auto& selectedIds = m_model->selectedLayerIds();
    if (selectedIds.isEmpty()) {
        return false;
    }

    const QList<LayerData*> flat = m_model->flattenedLayers();
    if (flat.isEmpty()) {
        return false;
    }

    int minSelected = std::numeric_limits<int>::max();
    int maxSelected = -1;
    QSet<LayerId> visibleSelected;

    for (int i = 0; i < flat.size(); ++i) {
        if (!flat[i])
            continue;
        const LayerId id = flat[i]->id;
        if (selectedIds.contains(id)) {
            if (flat[i]->isBackground()) {
                return false;
            }
            visibleSelected.insert(id);
            minSelected = qMin(minSelected, i);
            maxSelected = qMax(maxSelected, i);
        }
    }

    if (visibleSelected.isEmpty() || maxSelected < 0) {
        return false;
    }

    // Selection must form one contiguous block in visible list.
    for (int i = minSelected; i <= maxSelected; ++i) {
        if (!flat[i] || !visibleSelected.contains(flat[i]->id)) {
            return false;
        }
    }

    const int firstBelowSelected = maxSelected + 1;
    if (firstBelowSelected >= flat.size()) {
        return false;
    }

    // Resolve actual base layer by skipping clipped chain below selection.
    int baseIndex = firstBelowSelected;
    while (baseIndex < flat.size()) {
        LayerData* candidate = flat[baseIndex];
        if (!candidate) {
            return false;
        }
        if (candidate->isBackground()) {
            return false;
        }
        if (!candidate->clippedToBelow) {
            break;
        }
        ++baseIndex;
    }

    if (baseIndex >= flat.size()) {
        return false;
    }

    LayerData* baseLayer = flat[baseIndex];
    if (!baseLayer || baseLayer->isBackground()) {
        return false;
    }

    // Hovering any layer in clipped chain is valid, but preview anchors to base.
    int hoveredIndex = -1;
    for (int i = firstBelowSelected; i <= baseIndex; ++i) {
        if (flat[i] && flat[i]->id == hoveredId) {
            hoveredIndex = i;
            break;
        }
    }
    if (hoveredIndex < 0) {
        return false;
    }

    if (outInsertIndex) {
        *outInsertIndex = baseIndex;
    }
    if (outBaseLayerId) {
        *outBaseLayerId = baseLayer->id;
    }
    return true;
}

void LayerListView::updateClipPreviewAt(const QPoint& globalPos)
{
    const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    if (!(mods & Qt::AltModifier) || m_settlingDrag || !m_layout || !m_model) {
        clearClipPreview();
        return;
    }

    if (!m_contentWidget) {
        clearClipPreview();
        return;
    }

    // Do not keep preview visible when pointer is outside the list content.
    const QPoint contentLocalPos = m_contentWidget->mapFromGlobal(globalPos);
    if (!m_contentWidget->rect().contains(contentLocalPos)) {
        clearClipPreview();
        return;
    }

    LayerRowWidget* hoveredRow = rowAtGlobalPos(globalPos);
    LayerId hoveredId;
    int hoveredDepth = 0;

    if (hoveredRow && hoveredRow->layerData()) {
        hoveredId = hoveredRow->layerId();
        hoveredDepth = hoveredRow->layerData()->depth;
    } else {
        // Fallback for pointer in spacing between rows:
        // resolve by insertion index at Y.
        QPoint localPos = m_contentWidget->mapFromGlobal(globalPos);
        int candidateIndex = m_layout->dropInsertIndexAtY(localPos.y());
        if (LayerData* candidate = m_model->layerAtFlatIndex(candidateIndex)) {
            hoveredId = candidate->id;
            hoveredDepth = candidate->depth;
        }
    }

    if (hoveredId.isNull()) {
        clearClipPreview();
        return;
    }

    int insertIndex = -1;
    LayerId baseLayerId;
    if (!resolveClipPreviewTarget(hoveredId, &insertIndex, &baseLayerId)) {
        clearClipPreview();
        return;
    }

    if (m_clipPreviewIndicator && m_contentWidget) {
        m_clipPreviewIndicator->setFixedWidth(m_contentWidget->width());
        qreal rowTop = m_layout->targetYForIndex(insertIndex);
        int rowH = m_layout->scaledRowHeightAtIndex(insertIndex);
        int indicatorY = qRound(rowTop) + qMax(0, (rowH - m_clipPreviewIndicator->height()) / 2);
        m_clipPreviewIndicator->move(0, indicatorY);
        m_clipPreviewIndicator->setIndentLevel(hoveredDepth);
        m_clipPreviewIndicator->setIndicatorOpacity(1.0);
        m_clipPreviewIndicator->show();
        m_clipPreviewIndicator->raise();
    }

    setCursor(Qt::SizeVerCursor);
    m_contentWidget->setCursor(Qt::SizeVerCursor);
    m_clipPreviewActive = true;
    m_clipPreviewInsertIndex = insertIndex;
    m_clipPreviewBaseLayerId = baseLayerId;

    if (!m_clipPreviewFilterInstalled) {
        qApp->installEventFilter(this);
        ruwa::TabletToMouseEventFilter::ensureRunsFirst(qApp);
        m_clipPreviewFilterInstalled = true;
    }
}

void LayerListView::clearClipPreview()
{
    if (m_clipPreviewIndicator) {
        m_clipPreviewIndicator->hide();
    }
    unsetCursor();
    if (m_contentWidget) {
        m_contentWidget->unsetCursor();
    }

    m_clipPreviewActive = false;
    m_clipPreviewInsertIndex = -1;
    m_clipPreviewBaseLayerId = LayerId();

    if (m_clipPreviewFilterInstalled && !m_dragActive && !m_eyeSwiping) {
        qApp->removeEventFilter(this);
        m_clipPreviewFilterInstalled = false;
    }
}

// ============================================================================
// Mouse Tracking for Drag
// ============================================================================

void LayerListView::setupMouseTracking()
{
    m_contentWidget->installEventFilter(this);
    m_contentWidget->setMouseTracking(true);
}

// ============================================================================
// New Layer Insert Animation
// ============================================================================

void LayerListView::animateNewLayerInsert(
    const QSet<LayerId>& newIds, const QHash<LayerId, int>& oldPositions, int oldContentH)
{
    if (newIds.isEmpty())
        return;

    // Cancel any running creation animation
    if (m_creationAnim) {
        m_creationAnim->stop();
        m_creationAnim->deleteLater();
        m_creationAnim = nullptr;
    }

    m_creationAnimating = true;

    int spacing = ruwa::ui::core::ThemeManager::instance().scaled(m_layout->rowSpacing());

    // Collect new rows and their final positions
    struct NewRowInfo {
        LayerRowWidget* row;
        int finalY;
        int targetH;
    };
    QList<NewRowInfo> newRows;

    for (auto* row : m_activeRows) {
        if (newIds.contains(row->layerId())) {
            int targetH = scaledRowHeight(row);
            newRows.append({ row, row->y(), targetH });
        }
    }

    if (newRows.isEmpty()) {
        m_creationAnimating = false;
        return;
    }

    // Sort new rows by Y position (topmost first)
    std::sort(newRows.begin(), newRows.end(),
        [](const NewRowInfo& a, const NewRowInfo& b) { return a.finalY < b.finalY; });

    // Build sorted list of new row Y + height for offset calculation
    struct NewSlot {
        int y;
        int space;
    };
    QList<NewSlot> newSlots;
    for (auto& nr : newRows) {
        newSlots.append({ nr.finalY, nr.targetH + spacing });
    }

    // Find rows that existed before and need to move
    struct MovedRowInfo {
        LayerRowWidget* row;
        int oldY;
        int finalY;
    };
    QList<MovedRowInfo> movedRows;

    for (auto* row : m_activeRows) {
        LayerId rid = row->layerId();
        if (newIds.contains(rid))
            continue;

        int finalY = row->y();

        if (oldPositions.contains(rid)) {
            int oldY = oldPositions[rid];
            if (oldY != finalY) {
                movedRows.append({ row, oldY, finalY });
            }
        } else {
            // Row existed but wasn't in oldPositions (shouldn't happen normally)
            // Calculate how much new-row space is above it
            int offset = 0;
            for (auto& ns : newSlots) {
                if (ns.y < finalY)
                    offset += ns.space;
            }
            if (offset > 0) {
                movedRows.append({ row, finalY - offset, finalY });
            }
        }
    }

    int newContentH = m_layout->contentHeight();

    // Keep row layout fixed and reveal new rows via mask.
    for (auto& nr : newRows) {
        prepareMaskedRowAnimation(nr.row, 0.0);
        setRowVisibleMask(nr.row, 0);
    }

    // Move existing rows back to their old positions
    for (auto& info : movedRows) {
        info.row->move(0, info.oldY);
    }

    // Set content height to old value
    m_contentWidget->setFixedHeight(oldContentH);

    // Build animation group
    m_creationAnim = new QParallelAnimationGroup(this);

    // Grow + fade in each new row
    for (auto& nr : newRows) {
        // Opacity fade in
        auto* opAnim = new QPropertyAnimation(nr.row, "rowOpacity", m_creationAnim);
        opAnim->setDuration(300);
        opAnim->setEasingCurve(QEasingCurve::InOutCubic);
        opAnim->setStartValue(0.0);
        opAnim->setEndValue(1.0);
        m_creationAnim->addAnimation(opAnim);

        // Height grow
        auto* hAnim = new QVariantAnimation(m_creationAnim);
        hAnim->setDuration(300);
        hAnim->setEasingCurve(QEasingCurve::InOutCubic);
        hAnim->setStartValue(0);
        hAnim->setEndValue(nr.targetH);
        connect(hAnim, &QVariantAnimation::valueChanged, nr.row,
            [row = nr.row](const QVariant& v) { setRowVisibleMask(row, v.toInt()); });
        m_creationAnim->addAnimation(hAnim);
    }

    // Slide moved rows from old to final positions
    for (auto& info : movedRows) {
        auto* posAnim = new QPropertyAnimation(info.row, "pos", m_creationAnim);
        posAnim->setDuration(300);
        posAnim->setEasingCurve(QEasingCurve::InOutCubic);
        posAnim->setStartValue(QPoint(0, info.oldY));
        posAnim->setEndValue(QPoint(0, info.finalY));
        m_creationAnim->addAnimation(posAnim);
    }

    // Content height animation
    auto* contentAnim = new QVariantAnimation(m_creationAnim);
    contentAnim->setDuration(300);
    contentAnim->setEasingCurve(QEasingCurve::InOutCubic);
    contentAnim->setStartValue(oldContentH);
    contentAnim->setEndValue(newContentH);
    connect(contentAnim, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) { m_contentWidget->setFixedHeight(v.toInt()); });
    m_creationAnim->addAnimation(contentAnim);

    // On finish: clean up
    connect(m_creationAnim, &QParallelAnimationGroup::finished, this, [this]() {
        m_creationAnimating = false;
        m_creationAnim = nullptr;

        // Clear masks
        for (auto* row : m_activeRows) {
            row->clearMask();
        }

        // Final rebuild to ensure clean state
        m_skipNextAnimation = true;
        rebuildRowWidgets();
        syncContentHeight();
    });

    m_creationAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// Layer Removal Animation
// ============================================================================

void LayerListView::animateLayerRemoval(const QSet<LayerId>& removedIds)
{
    if (m_deleteAnim) {
        disconnect(m_deleteAnim, nullptr, this, nullptr);
        m_deleteAnim->stop();
        m_deleteAnim->deleteLater();
        m_deleteAnim = nullptr;
    }

    m_deleteAnimating = true;

    auto& tm = ruwa::ui::core::ThemeManager::instance();

    struct RemoveRowInfo {
        LayerRowWidget* row;
        int startH;
        int startY;
    };
    QList<RemoveRowInfo> removeRows;

    QList<QPair<LayerId, ReorderableRowWidget*>> layoutEntries;
    for (LayerData* layer : m_model->flattenedLayers()) {
        if (!layer)
            continue;
        LayerRowWidget* row = m_rowMap.value(layer->id);
        if (!row)
            continue;
        layoutEntries.append({ layer->id, row });
    }

    // Collect removed rows. Snapshot + null already set in onLayerAboutToBeRemoved.
    // For clear() (no layerAboutToBeRemoved), set null + empty snapshot to avoid UAF.
    for (const LayerId& rid : removedIds) {
        auto it = m_rowMap.find(rid);
        if (it == m_rowMap.end() || !it.value())
            continue;
        LayerRowWidget* row = it.value();

        row->setLayerData(nullptr);
        // Snapshot already set in onLayerAboutToBeRemoved. For clear() it stays empty.

        int h = row->height();
        removeRows.append({ row, h, row->y() });
    }

    // Update layout for remaining rows (animates them to new positions)
    // Layout will emit contentHeightChanged for height animation
    m_layout->updateLayout(layoutEntries, true, {}, {});

    // Build delete animation: shrink + fade for each removed row
    m_deleteAnim = new QParallelAnimationGroup(this);

    for (auto& info : removeRows) {
        auto* row = info.row;

        auto* opAnim = new QPropertyAnimation(row, "rowOpacity", m_deleteAnim);
        opAnim->setDuration(300);
        opAnim->setEasingCurve(QEasingCurve::InOutCubic);
        opAnim->setStartValue(row->rowOpacity());
        opAnim->setEndValue(0.0);
        m_deleteAnim->addAnimation(opAnim);

        auto* hAnim = new QVariantAnimation(m_deleteAnim);
        hAnim->setDuration(300);
        hAnim->setEasingCurve(QEasingCurve::InOutCubic);
        hAnim->setStartValue(info.startH);
        hAnim->setEndValue(0);
        connect(hAnim, &QVariantAnimation::valueChanged, row,
            [row](const QVariant& v) { setRowVisibleMask(row, v.toInt()); });
        m_deleteAnim->addAnimation(hAnim);
    }

    QSet<LayerRowWidget*> capturedRemovedRows;
    for (auto& info : removeRows) {
        capturedRemovedRows.insert(info.row);
    }

    connect(m_deleteAnim, &QParallelAnimationGroup::finished, this, [this, capturedRemovedRows]() {
        m_deleteAnim = nullptr;
        m_deleteAnimating = false;

        for (auto* row : capturedRemovedRows) {
            if (row) {
                row->setRemovalSnapshot(QPixmap());
                row->hide();
                row->clearMask();
                row->setRowOpacity(1.0);
            }
        }

        m_skipNextAnimation = true;
        rebuildRowWidgets();
        syncContentHeight();
    });

    m_deleteAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace ruwa::ui::widgets
