// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H E S   P A N E L   C O N T E N T
// ==========================================================================

#include "features/brush/ui/BrushesPanelContent.h"

#include "features/brush/editor/BrushEditorWindow.h"
#include "features/brush/manager/BrushManager.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/settings/SettingsManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QEvent>
#include <QJsonArray>
#include <QLabel>
#include <QRect>
#include <QTimer>
#include <QVBoxLayout>

namespace ruwa::ui::workspace {

using ruwa::core::brushes::BrushData;
using ruwa::core::brushes::BrushManager;
using ruwa::core::brushes::BrushPresetData;

BrushesPanelContent::BrushesPanelContent(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background: transparent;"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scrollArea = new widgets::SmoothScrollArea(this);
    m_scrollArea->setFillBackground(false);
    m_scrollArea->setScrollBarTransparentTrack(true);
    m_scrollArea->setScrollBarAlwaysReserved(false);
    m_scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));
    if (m_scrollArea->viewport()) {
        m_scrollArea->viewport()->installEventFilter(this);
        m_scrollArea->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
        m_scrollArea->viewport()->setAutoFillBackground(false);
        m_scrollArea->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    }
    connect(m_scrollArea, &widgets::SmoothScrollArea::scrolled, this,
        [this](int) { notifyStateChanged(); });
    rootLayout->addWidget(m_scrollArea);

    m_scrollContent = new QWidget(m_scrollArea);
    m_scrollContent->setAttribute(Qt::WA_TranslucentBackground, true);
    m_scrollContent->setAutoFillBackground(false);
    m_scrollContent->setStyleSheet(QStringLiteral("background: transparent;"));
    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollArea->setWidget(m_scrollContent);

    auto& manager = BrushManager::instance();
    connect(&manager, &BrushManager::presetCreated, this, &BrushesPanelContent::queueReload);
    connect(&manager, &BrushManager::presetRemoved, this, &BrushesPanelContent::queueReload);
    connect(
        &manager, &BrushManager::presetRenamed, this, &BrushesPanelContent::onManagerPresetRenamed);
    connect(&manager, &BrushManager::brushCreated, this, &BrushesPanelContent::queueReload);
    connect(&manager, &BrushManager::brushRemoved, this, &BrushesPanelContent::queueReload);
    connect(
        &manager, &BrushManager::brushRenamed, this, &BrushesPanelContent::onManagerBrushRenamed);
    connect(&manager, &BrushManager::dataReset, this, &BrushesPanelContent::queueReload);
    connect(&manager, &BrushManager::brushSettingsUpdated, this,
        [this](const QString& presetId, const QString& brushId,
            const ruwa::core::brushes::BrushSettingsData& settings) {
            // Keep the cached pack data in sync so rebuilds (e.g. on theme
            // change) pick up the latest settings.
            for (BrushListPackData& pack : m_packs) {
                if (pack.id != presetId)
                    continue;
                for (BrushListBrushData& brush : pack.brushes) {
                    if (brush.id == brushId) {
                        brush.settings = settings;
                        break;
                    }
                }
                break;
            }
            // Invalidate and repaint the live row button, if the section
            // exists. Live edits in the brush editor land here on every
            // slider change — this path must stay cheap (no full rebuild).
            if (auto* section = m_sections.value(presetId, nullptr)) {
                section->updateBrushSettings(brushId, settings);
            }
        });

    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::brushDisplayColorChanged, this,
        [this](const QString& brushId, int colorIndex) {
            for (BrushListPackData& pack : m_packs) {
                for (BrushListBrushData& brush : pack.brushes) {
                    if (brush.id == brushId) {
                        brush.displayColorIndex = colorIndex;
                        if (auto* section = m_sections.value(pack.id, nullptr)) {
                            section->updateBrushDisplayColorIndex(brushId, colorIndex);
                        }
                        return;
                    }
                }
            }
        });

    // Visibility-gated: deferred for hidden (background-tab) instances; flushed
    // on activation via WorkspaceTab.
    ruwa::ui::core::ThemeManager::instance().registerThemeHandler(
        this, [this]() { onThemeChanged(); });

    onThemeChanged();
    reloadFromManager();
}

BrushesPanelContent::~BrushesPanelContent() = default;

void BrushesPanelContent::setCanvasPanel(CanvasPanel* canvasPanel)
{
    if (m_canvasPanel == canvasPanel) {
        return;
    }

    if (m_canvasPanel) {
        disconnect(m_canvasPanel, nullptr, this, nullptr);
    }

    m_canvasPanel = canvasPanel;
    if (!m_canvasPanel) {
        return;
    }

    connect(m_canvasPanel, &CanvasPanel::toolModeChanged, this,
        [this]() { syncSelectionFromCanvas(); });
    connect(m_canvasPanel, &CanvasPanel::brushSelectionContextChanged, this,
        [this](CanvasPanel::ToolMode, const QString&) { syncSelectionFromCanvas(); });

    syncSelectionFromCanvas();
}

void BrushesPanelContent::reloadFromManager()
{
    m_packs = collectPacks();

    QSet<QString> validPackIds;
    for (const BrushListPackData& pack : m_packs) {
        validPackIds.insert(pack.id);
    }
    m_expandedPackIds.intersect(validPackIds);

    ensureSelection();

    if (m_expandedPackIds.isEmpty() && !m_hasExplicitExpandedState) {
        QString selectedPackId;
        for (const BrushListPackData& pack : m_packs) {
            for (const BrushListBrushData& brush : pack.brushes) {
                if (brush.id == m_selectedBrushId) {
                    selectedPackId = pack.id;
                    break;
                }
            }
            if (!selectedPackId.isEmpty()) {
                break;
            }
        }

        if (!selectedPackId.isEmpty()) {
            m_expandedPackIds.insert(selectedPackId);
        } else {
            for (const BrushListPackData& pack : m_packs) {
                if (!pack.brushes.isEmpty()) {
                    m_expandedPackIds.insert(pack.id);
                    break;
                }
            }
        }
    }

    rebuildSections();
}

QJsonObject BrushesPanelContent::saveState() const
{
    QJsonObject state;
    QJsonArray expandedPacks;
    for (auto it = m_expandedPackIds.begin(); it != m_expandedPackIds.end(); ++it) {
        expandedPacks.append(*it);
    }

    state["expandedPacks"] = expandedPacks;
    state["scrollValue"]
        = m_scrollArea ? m_scrollArea->scrollValue() : qMax(0, m_pendingScrollValue);
    return state;
}

void BrushesPanelContent::restoreState(const QJsonObject& state)
{
    if (state.isEmpty()) {
        return;
    }

    m_restoringState = true;

    if (state.contains("expandedPacks")) {
        QSet<QString> expandedPackIds;
        const QJsonArray expandedPacks = state["expandedPacks"].toArray();
        for (const QJsonValue& value : expandedPacks) {
            const QString packId = value.toString();
            if (!packId.isEmpty()) {
                expandedPackIds.insert(packId);
            }
        }

        m_expandedPackIds = expandedPackIds;
        m_hasExplicitExpandedState = true;
    }

    if (state.contains("scrollValue")) {
        m_pendingScrollValue = qMax(0, state["scrollValue"].toInt());
    }

    reloadFromManager();
    if (m_pendingScrollValue >= 0) {
        scheduleScrollRestore();
    } else {
        m_restoringState = false;
    }
}

void BrushesPanelContent::onManagerBrushRenamed(const QString& brushId, const QString& newName)
{
    // A rename only changes one row's label. Update it in place instead of
    // rebuilding every section (which would recreate all rows and previews on
    // each keystroke). Fall back to a full reload only if the row isn't found.
    bool updated = false;
    for (auto it = m_sections.begin(); it != m_sections.end(); ++it) {
        if (it.value() && it.value()->updateBrushName(brushId, newName)) {
            updated = true;
            break;
        }
    }

    if (!updated) {
        queueReload();
        return;
    }

    // Keep the cached pack data in sync so a later rebuild shows the new name.
    for (BrushListPackData& pack : m_packs) {
        for (BrushListBrushData& brush : pack.brushes) {
            if (brush.id == brushId) {
                brush.name = newName;
                return;
            }
        }
    }
}

void BrushesPanelContent::onManagerPresetRenamed(const QString& presetId, const QString& newName)
{
    // A pack rename only changes one section header. Update it in place instead
    // of reloading every section (which recreates all rows and previews and
    // replays the selected row's selection animation). Fall back to a full
    // reload only if the section isn't present yet.
    auto* section = m_sections.value(presetId, nullptr);
    if (!section) {
        queueReload();
        return;
    }

    section->updatePackName(newName);

    // Keep the cached pack data in sync so a later rebuild shows the new name.
    for (BrushListPackData& pack : m_packs) {
        if (pack.id == presetId) {
            pack.name = newName;
            break;
        }
    }
}

void BrushesPanelContent::queueReload()
{
    if (m_reloadQueued) {
        return;
    }

    m_reloadQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_reloadQueued = false;
        reloadFromManager();
    });
}

void BrushesPanelContent::onSectionToggled(const QString& packId, bool expanded)
{
    if (expanded) {
        m_expandedPackIds.insert(packId);
    } else {
        m_expandedPackIds.remove(packId);
    }
    m_hasExplicitExpandedState = true;
    refreshScrollGeometry();
    notifyStateChanged();
}

void BrushesPanelContent::onBrushActivated(const QString& packId, const QString& brushId)
{
    const bool expandedStateChanged = !m_expandedPackIds.contains(packId);
    m_expandedPackIds.insert(packId);
    m_hasExplicitExpandedState = true;

    if (auto* section = m_sections.value(packId, nullptr); section && !section->isExpanded()) {
        section->setExpanded(true, true);
    }

    if (m_canvasPanel) {
        m_canvasPanel->selectBrushForCurrentContext(brushId);
    }
    m_selectedBrushId = brushId;
    syncSelectionToSections();
    emit brushSelected(brushId);
    if (expandedStateChanged) {
        notifyStateChanged();
    }
}

void BrushesPanelContent::onBrushEditorRequested(const QString& packId, const QString& brushId)
{
    onBrushActivated(packId, brushId);
    openBrushEditor(packId, brushId);
}

void BrushesPanelContent::onThemeChanged()
{
    const int outerMargin = ruwa::ui::core::ThemeManager::instance().scaled(8);
    const int spacing = ruwa::ui::core::ThemeManager::instance().scaled(8);
    m_scrollLayout->setContentsMargins(outerMargin, outerMargin, outerMargin, outerMargin);
    m_scrollLayout->setSpacing(spacing);

    rebuildSections();
}

void BrushesPanelContent::syncSelectionFromCanvas()
{
    if (!m_canvasPanel) {
        return;
    }

    m_selectedBrushId = m_canvasPanel->selectedBrushIdForCurrentContext();
    if (!m_selectedBrushId.isEmpty()) {
        for (const BrushListPackData& pack : m_packs) {
            for (const BrushListBrushData& brush : pack.brushes) {
                if (brush.id == m_selectedBrushId) {
                    m_expandedPackIds.insert(pack.id);
                    break;
                }
            }
        }
    }

    ensureSelection();
    syncSelectionToSections();
}

bool BrushesPanelContent::eventFilter(QObject* watched, QEvent* event)
{
    if (m_scrollArea && watched == m_scrollArea->viewport() && m_pendingScrollValue >= 0) {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::LayoutRequest:
            scheduleScrollRestore();
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

QVector<BrushListPackData> BrushesPanelContent::collectPacks() const
{
    QVector<BrushListPackData> packs;

    auto& manager = BrushManager::instance();
    const QVector<BrushPresetData>& presets = manager.presets();
    packs.reserve(presets.size());
    auto& settings = ruwa::core::SettingsManager::instance();

    for (const BrushPresetData& preset : presets) {
        BrushListPackData pack;
        pack.id = preset.id;
        pack.name = preset.name;

        const QVector<BrushData> brushes = manager.brushesForPreset(preset.id);
        pack.brushes.reserve(brushes.size());
        for (const BrushData& brush : brushes) {
            pack.brushes.append({ brush.id, brush.name, brush.settings,
                settings.brushDisplayColorIndex(brush.id) });
        }

        packs.append(pack);
    }

    return packs;
}

void BrushesPanelContent::rebuildSections()
{
    clearSections();

    if (m_packs.isEmpty()) {
        auto* titleLabel = new QLabel(tr("No brush packs"), m_scrollContent);
        auto* hintLabel
            = new QLabel(tr("Create or restore packs to populate this panel."), m_scrollContent);
        titleLabel->setObjectName(QStringLiteral("brushes_panel_empty_title"));
        hintLabel->setObjectName(QStringLiteral("brushes_panel_empty_hint"));
        hintLabel->setWordWrap(true);
        m_scrollLayout->addWidget(titleLabel);
        m_scrollLayout->addWidget(hintLabel);
        m_scrollLayout->addStretch();
        refreshScrollGeometry();
        return;
    }

    for (const BrushListPackData& pack : m_packs) {
        auto* section = new BrushPackListSection(m_scrollContent);
        section->setPackData(pack);
        section->setExpanded(m_expandedPackIds.contains(pack.id), false);
        section->setSelectedBrushId(m_selectedBrushId);

        connect(
            section, &BrushPackListSection::toggled, this, &BrushesPanelContent::onSectionToggled);
        connect(section, &BrushPackListSection::brushActivated, this,
            &BrushesPanelContent::onBrushActivated);
        connect(section, &BrushPackListSection::brushEditorRequested, this,
            &BrushesPanelContent::onBrushEditorRequested);
        connect(section, &BrushPackListSection::contentGeometryChanged, this,
            &BrushesPanelContent::refreshScrollGeometry);

        m_sections.insert(pack.id, section);
        m_scrollLayout->addWidget(section);
    }

    m_scrollLayout->addStretch();
    refreshScrollGeometry();
}

void BrushesPanelContent::clearSections()
{
    m_sections.clear();

    while (QLayoutItem* item = m_scrollLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }
}

void BrushesPanelContent::ensureSelection()
{
    auto containsBrush = [this](const QString& brushId) {
        for (const BrushListPackData& pack : m_packs) {
            for (const BrushListBrushData& brush : pack.brushes) {
                if (brush.id == brushId) {
                    return true;
                }
            }
        }
        return false;
    };

    if (!m_selectedBrushId.isEmpty() && containsBrush(m_selectedBrushId)) {
        return;
    }

    m_selectedBrushId.clear();
}

void BrushesPanelContent::syncSelectionToSections()
{
    for (auto it = m_sections.begin(); it != m_sections.end(); ++it) {
        it.value()->setSelectedBrushId(m_selectedBrushId);
    }
}

void BrushesPanelContent::refreshScrollGeometry()
{
    if (!m_scrollArea || !m_scrollContent) {
        return;
    }

    m_scrollContent->adjustSize();
    m_scrollContent->updateGeometry();
    m_scrollArea->refreshScrollGeometry();
}

void BrushesPanelContent::scheduleScrollRestore()
{
    if (m_pendingScrollValue < 0 || !m_scrollArea || m_scrollRestoreQueued) {
        return;
    }

    m_scrollRestoreQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_scrollRestoreQueued = false;
        applyPendingScrollRestore();
    });
}

void BrushesPanelContent::applyPendingScrollRestore()
{
    if (m_pendingScrollValue < 0 || !m_scrollArea) {
        return;
    }

    if (!m_scrollArea->viewport() || m_scrollArea->viewport()->width() <= 0
        || m_scrollArea->viewport()->height() <= 0) {
        return;
    }

    const int scrollValue = m_pendingScrollValue;
    m_pendingScrollValue = -1;

    refreshScrollGeometry();
    m_scrollArea->setScrollValue(scrollValue);
    m_restoringState = false;
}

void BrushesPanelContent::openBrushEditor(const QString& packId, const QString& brushId)
{
    const QString brushName = brushNameForSelection(packId, brushId);
    if (packId.isEmpty() || brushId.isEmpty() || brushName.isEmpty()) {
        return;
    }

    if (!m_brushEditorWindow) {
        m_brushEditorWindow = new ruwa::ui::windows::BrushEditorWindow(this);
        connect(m_brushEditorWindow.data(),
            &ruwa::ui::windows::BrushEditorWindow::brushSelectionChanged, this,
            [this](const QString& editorPackId, const QString& editorBrushId) {
                if (editorPackId.isEmpty() || editorBrushId.isEmpty()) {
                    return;
                }
                onBrushActivated(editorPackId, editorBrushId);
            });
    }

    m_brushEditorWindow->setSelection(packId, brushId);
    m_brushEditorWindow->setBrushName(brushName);

    if (!m_brushEditorWindow->isVisible()) {
        QWidget* owner = window();
        QRect ownerRect = owner ? owner->geometry() : QRect();
        if (owner) {
            const QPoint globalTopLeft = owner->mapToGlobal(QPoint(0, 0));
            ownerRect.moveTopLeft(globalTopLeft);
        }

        const QSize windowSize = m_brushEditorWindow->size();
        const QPoint centeredPos(ownerRect.center().x() - windowSize.width() / 2,
            ownerRect.center().y() - windowSize.height() / 2);
        m_brushEditorWindow->move(centeredPos);
        m_brushEditorWindow->show();
    }

    m_brushEditorWindow->raise();
    m_brushEditorWindow->activateWindow();
}

QString BrushesPanelContent::brushNameForSelection(
    const QString& packId, const QString& brushId) const
{
    for (const BrushListPackData& pack : m_packs) {
        if (pack.id != packId) {
            continue;
        }
        for (const BrushListBrushData& brush : pack.brushes) {
            if (brush.id == brushId) {
                return brush.name;
            }
        }
        return {};
    }
    return {};
}

void BrushesPanelContent::notifyStateChanged()
{
    if (m_restoringState) {
        return;
    }

    emit stateChanged();
}

} // namespace ruwa::ui::workspace
