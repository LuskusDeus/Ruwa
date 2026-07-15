// SPDX-License-Identifier: MPL-2.0

// AnimatedListLayout.cpp
#include "shared/widgets/reorderlist/AnimatedListLayout.h"
#include "shared/widgets/reorderlist/ReorderableRowWidget.h"

#include "features/theme/manager/ThemeManager.h"

#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSet>
#include <QtMath>
#include <algorithm>

namespace ruwa::ui::widgets {

// ============================================================================
// Construction
// ============================================================================

AnimatedListLayout::AnimatedListLayout(QWidget* container, QObject* parent)
    : QObject(parent)
    , m_container(container)
{
}

AnimatedListLayout::~AnimatedListLayout()
{
    finishAnimations();
}

int AnimatedListLayout::scaleRow(int h) const
{
    return m_scaleRowHeights ? ruwa::ui::core::ThemeManager::instance().scaled(h) : h;
}

// ============================================================================
// Layout Calculation
// ============================================================================

void AnimatedListLayout::updateLayout(const QList<QPair<QUuid, ReorderableRowWidget*>>& rows,
    bool animate, const QSet<QUuid>& newEntryIds, const QSet<QUuid>& instantPlaceIds)
{
    finishAnimations();

    m_entries.clear();
    m_entries.reserve(rows.size());

    auto& tm = ruwa::ui::core::ThemeManager::instance();
    int scaledSpacing = tm.scaled(m_rowSpacing);

    int y = 0;

    for (int i = 0; i < rows.size(); ++i) {
        RowLayoutEntry entry;
        entry.itemId = rows[i].first;
        entry.widget = rows[i].second;

        // Per-row height from widget
        if (entry.widget) {
            entry.rowHeight = entry.widget->effectiveRowHeight();
        }
        int scaledHeight = scaleRow(entry.rowHeight);

        // Insert gap before this index if needed
        if (m_dropGapIndex >= 0 && i == m_dropGapIndex) {
            y += tm.scaled(m_dropGapHeight) + scaledSpacing;
        }

        entry.targetY = y;
        entry.visible = true;

        bool isNew = newEntryIds.contains(entry.itemId);
        bool isInstant = instantPlaceIds.contains(entry.itemId);

        if (!animate || !entry.widget || isNew || isInstant) {
            entry.currentY = y;
        } else {
            entry.currentY = entry.widget->y();
        }

        m_entries.append(entry);
        y += scaledHeight + scaledSpacing;
    }

    // Gap at end
    if (m_dropGapIndex >= 0 && m_dropGapIndex >= rows.size()) {
        y += tm.scaled(m_dropGapHeight) + scaledSpacing;
    }

    m_contentHeight = y > 0 ? y - scaledSpacing : 0;

    applyPositions(animate, newEntryIds, instantPlaceIds);

    emit contentHeightChanged(m_contentHeight);
}

// ============================================================================
// Drop Gap
// ============================================================================

void AnimatedListLayout::setDropGapIndex(int flatIndex)
{
    if (m_dropGapIndex == flatIndex)
        return;
    m_dropGapIndex = flatIndex;

    auto& tm = ruwa::ui::core::ThemeManager::instance();
    int scaledSpacing = tm.scaled(m_rowSpacing);

    int y = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_dropGapIndex >= 0 && i == m_dropGapIndex) {
            y += tm.scaled(m_dropGapHeight) + scaledSpacing;
        }
        m_entries[i].targetY = y;
        int scaledHeight = scaleRow(m_entries[i].rowHeight);
        y += scaledHeight + scaledSpacing;
    }

    if (m_dropGapIndex >= 0 && m_dropGapIndex >= m_entries.size()) {
        y += tm.scaled(m_dropGapHeight) + scaledSpacing;
    }

    m_contentHeight = y > 0 ? y - scaledSpacing : 0;

    applyPositions(true);
    emit contentHeightChanged(m_contentHeight);
}

// ============================================================================
// Group Clipping
// ============================================================================

void AnimatedListLayout::setGroupClip(const QUuid& groupId, qreal clipBottom, bool expanding)
{
    m_groupClips[groupId] = { clipBottom, expanding };
}

void AnimatedListLayout::clearClips()
{
    m_groupClips.clear();
}

// ============================================================================
// Query
// ============================================================================

int AnimatedListLayout::rowIndexAtY(int y) const
{
    auto& tm = ruwa::ui::core::ThemeManager::instance();

    for (int i = 0; i < m_entries.size(); ++i) {
        int scaledHeight = scaleRow(m_entries[i].rowHeight);
        qreal top = m_entries[i].targetY;
        if (y >= top && y < top + scaledHeight) {
            return i;
        }
    }
    return -1;
}

int AnimatedListLayout::dropInsertIndexAtY(int y) const
{
    auto& tm = ruwa::ui::core::ThemeManager::instance();

    if (m_entries.isEmpty())
        return 0;

    for (int i = 0; i < m_entries.size(); ++i) {
        int scaledHeight = scaleRow(m_entries[i].rowHeight);
        qreal mid = m_entries[i].targetY + scaledHeight * 0.5;
        if (y < mid) {
            return i;
        }
    }

    return m_entries.size();
}

qreal AnimatedListLayout::targetYForIndex(int index) const
{
    if (index < 0 || index >= m_entries.size()) {
        return m_contentHeight;
    }
    return m_entries[index].targetY;
}

ReorderableRowWidget* AnimatedListLayout::rowWidgetAtIndex(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return nullptr;
    return m_entries[index].widget;
}

int AnimatedListLayout::scaledRowHeightAtIndex(int index) const
{
    if (index < 0 || index >= m_entries.size())
        return 0;
    return scaleRow(m_entries[index].rowHeight);
}

// ============================================================================
// Drag End State
// ============================================================================

void AnimatedListLayout::applyDragEndState(const QSet<QUuid>& excludeIds, int dropInsertIndex)
{
    finishAnimations();

    m_excludeIds = excludeIds;

    auto& tm = ruwa::ui::core::ThemeManager::instance();
    int scaledSpacing = tm.scaled(m_rowSpacing);

    // Compute gap height from actual excluded row heights (for multi-item drag)
    int totalGapUnscaled = 0;
    int excludedCount = 0;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (excludeIds.contains(m_entries[i].itemId)) {
            totalGapUnscaled += m_entries[i].rowHeight;
            excludedCount++;
        }
    }
    if (excludedCount > 1) {
        totalGapUnscaled += (excludedCount - 1) * m_rowSpacing;
    }
    int gapHeight
        = (totalGapUnscaled > 0) ? scaleRow(totalGapUnscaled) : tm.scaled(m_dropGapHeight);

    // Count how many excluded entries appear before the drop index
    int excludeBeforeDrop = 0;
    for (int i = 0; i < m_entries.size() && i < dropInsertIndex; ++i) {
        if (m_excludeIds.contains(m_entries[i].itemId)) {
            excludeBeforeDrop++;
        }
    }

    // Calculate adjusted gap index in "without excluded" space
    int adjustedGap = dropInsertIndex - excludeBeforeDrop;

    // Recalculate positions for non-excluded entries
    int y = 0;
    int virtualIndex = 0; // index in "without excluded" list
    m_dragEndGapY = -1;

    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_excludeIds.contains(m_entries[i].itemId)) {
            // Excluded: don't change targetY — widget stays in place,
            // view manages its collapse animation
            continue;
        }

        // Insert gap before this virtual index
        if (adjustedGap >= 0 && virtualIndex == adjustedGap) {
            m_dragEndGapY = y;
            y += gapHeight + scaledSpacing;
        }

        m_entries[i].targetY = y;
        int scaledHeight = scaleRow(m_entries[i].rowHeight);
        y += scaledHeight + scaledSpacing;
        virtualIndex++;
    }

    // Gap at end
    if (adjustedGap >= 0 && virtualIndex <= adjustedGap) {
        m_dragEndGapY = y;
        y += gapHeight + scaledSpacing;
    }

    m_contentHeight = y > 0 ? y - scaledSpacing : 0;

    // Animate non-excluded entries to new positions
    applyPositions(true);

    emit contentHeightChanged(m_contentHeight);
}

void AnimatedListLayout::applyCopyDragEndState(int dropInsertIndex, int gapHeight)
{
    finishAnimations();

    m_excludeIds.clear();

    auto& tm = ruwa::ui::core::ThemeManager::instance();
    const int scaledSpacing = tm.scaled(m_rowSpacing);
    const int scaledGapHeight = tm.scaled(qMax(1, gapHeight));
    const int clampedDropIndex = qBound(0, dropInsertIndex, m_entries.size());

    int y = 0;
    m_dragEndGapY = -1;

    for (int i = 0; i < m_entries.size(); ++i) {
        if (i == clampedDropIndex) {
            m_dragEndGapY = y;
            y += scaledGapHeight + scaledSpacing;
        }

        m_entries[i].targetY = y;
        const int scaledHeight = scaleRow(m_entries[i].rowHeight);
        y += scaledHeight + scaledSpacing;
    }

    if (clampedDropIndex >= m_entries.size()) {
        m_dragEndGapY = y;
        y += scaledGapHeight + scaledSpacing;
    }

    m_contentHeight = y > 0 ? y - scaledSpacing : 0;

    applyPositions(true);

    emit contentHeightChanged(m_contentHeight);
}

void AnimatedListLayout::clearDragEndState()
{
    m_excludeIds.clear();
    m_dragEndGapY = -1;
    m_dropGapIndex = -1;
}

// ============================================================================
// Animation
// ============================================================================

void AnimatedListLayout::applyPositions(
    bool animate, const QSet<QUuid>& newEntryIds, const QSet<QUuid>& instantPlaceIds)
{
    finishAnimations();

    if (!animate || !m_container) {
        // Immediate placement
        for (auto& entry : m_entries) {
            if (!entry.widget)
                continue;
            // Skip excluded entries — view manages them
            if (m_excludeIds.contains(entry.itemId))
                continue;

            int containerW = m_container->width();
            int scaledH = scaleRow(entry.rowHeight);
            // Reset fixed height constraint (collapse animation sets it to 0)
            entry.widget->setFixedHeight(scaledH);
            entry.widget->setGeometry(0, qRound(entry.targetY), containerW, scaledH);
            entry.currentY = entry.targetY;
            entry.widget->clearMask();
            entry.widget->setVisible(entry.visible);
            entry.widget->setRowOpacity(1.0);
        }
        return;
    }

    // Animated placement
    m_animGroup = new QParallelAnimationGroup(this);

    for (auto& entry : m_entries) {
        if (!entry.widget)
            continue;
        // Skip excluded entries — view manages their collapse animation
        if (m_excludeIds.contains(entry.itemId))
            continue;

        int containerW = m_container->width();
        int scaledH = scaleRow(entry.rowHeight);
        entry.widget->setFixedHeight(scaledH);
        entry.widget->resize(containerW, scaledH);

        bool isNew = newEntryIds.contains(entry.itemId);
        bool isInstant = instantPlaceIds.contains(entry.itemId);

        if (isInstant) {
            // Instant placement: no animation, no fade, just appear at target
            entry.widget->move(0, qRound(entry.targetY));
            entry.widget->setRowOpacity(1.0);
            entry.widget->clearMask();
        } else if (isNew) {
            // New entry: place at target immediately, fade in
            entry.widget->move(0, qRound(entry.targetY));
            entry.widget->setRowOpacity(0.0);

            auto* fadeAnim = new QPropertyAnimation(entry.widget, "rowOpacity", m_animGroup);
            fadeAnim->setDuration(m_animDuration);
            fadeAnim->setEasingCurve(QEasingCurve::InOutCubic);
            fadeAnim->setStartValue(0.0);
            fadeAnim->setEndValue(1.0);
            m_animGroup->addAnimation(fadeAnim);
        } else {
            // Existing entry: animate position
            if (qAbs(entry.currentY - entry.targetY) > 0.5) {
                auto* posAnim = new QPropertyAnimation(entry.widget, "pos", m_animGroup);
                posAnim->setDuration(m_animDuration);
                posAnim->setEasingCurve(QEasingCurve::InOutCubic);
                posAnim->setStartValue(QPoint(0, qRound(entry.currentY)));
                posAnim->setEndValue(QPoint(0, qRound(entry.targetY)));
                m_animGroup->addAnimation(posAnim);
            } else {
                entry.widget->move(0, qRound(entry.targetY));
            }
        }

        entry.widget->setVisible(entry.visible);
        entry.currentY = entry.targetY;
    }

    if (m_animGroup->animationCount() > 0) {
        connect(m_animGroup, &QParallelAnimationGroup::finished, this,
            &AnimatedListLayout::layoutAnimationFinished);
        m_animGroup->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        m_animGroup->deleteLater();
        m_animGroup = nullptr;
    }
}

void AnimatedListLayout::finishAnimations()
{
    if (m_animGroup) {
        m_animGroup->stop();
        // Snap widgets to their target positions to ensure consistent state.
        // Skip the excluded entry — it's managed by the drag system.
        for (auto& entry : m_entries) {
            if (entry.widget) {
                if (m_excludeIds.contains(entry.itemId))
                    continue;
                entry.widget->move(0, qRound(entry.targetY));
                entry.currentY = entry.targetY;
            }
        }
        m_animGroup->deleteLater();
        m_animGroup = nullptr;
    }
}

} // namespace ruwa::ui::widgets
