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
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QEvent>
#include <QJsonArray>
#include <QLabel>
#include <QRect>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace ruwa::ui::workspace {

using ruwa::core::brushes::BrushData;
using ruwa::core::brushes::BrushManager;
using ruwa::core::brushes::BrushPresetData;

namespace {
constexpr int kPanelStateVersion = 3;
constexpr auto kFavoritesSectionId = "__brush_favorites__";
constexpr auto kFavoritesPageKey = "__favorites_filter__";
constexpr auto kAllPageKey = "__all_filter__";
} // namespace

QSet<QString> BrushesPanelContent::s_collapsedPackIds;
QVector<BrushesPanelContent*> BrushesPanelContent::s_instances;
bool BrushesPanelContent::s_collapsedStateLoaded = false;

BrushesPanelContent::BrushesPanelContent(QWidget* parent)
    : QWidget(parent)
{
    s_instances.append(this);

    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("background: transparent;"));

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_pageStack = new widgets::AnimatedStackedWidget(this);
    m_pageStack->setSlideOrientation(widgets::AnimatedStackedWidget::SlideOrientation::Horizontal);
    m_pageStack->setAnimationDuration(250);
    m_pageStack->setAnimationEasing(QEasingCurve::InOutCubic);
    m_pageStack->setInterruptEasing(QEasingCurve::OutCubic);
    m_pageStack->setSuspendLayoutDuringAnimation(true);
    m_pageStack->setAttribute(Qt::WA_TranslucentBackground, true);
    m_pageStack->setStyleSheet(QStringLiteral("background: transparent;"));
    rootLayout->addWidget(m_pageStack);

    auto& manager = BrushManager::instance();
    connect(&manager, &BrushManager::presetCreated, this, &BrushesPanelContent::queueReload);
    connect(&manager, &BrushManager::presetRemoved, this, &BrushesPanelContent::queueReload);
    connect(
        &manager, &BrushManager::presetRenamed, this, &BrushesPanelContent::onManagerPresetRenamed);
    connect(&manager, &BrushManager::brushCreated, this, &BrushesPanelContent::queueReload);
    connect(&manager, &BrushManager::brushRemoved, this,
        [this](const QString&, const QString& brushId) {
            ruwa::core::SettingsManager::instance().setBrushFavorite(brushId, false);
            queueReload();
        });
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
            // Invalidate and repaint every visible copy of the live row.
            // Editor updates arrive on every slider change, so this path must
            // stay cheap and avoid rebuilding any page.
            for (FilterPage& page : m_filterPages) {
                if (auto* section = page.sections.value(presetId, nullptr)) {
                    section->updateBrushSettings(brushId, settings);
                }
                if (auto* favorites
                    = page.sections.value(QLatin1String(kFavoritesSectionId), nullptr)) {
                    favorites->updateBrushSettings(brushId, settings);
                }
            }
        });

    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::brushDisplayColorChanged, this,
        [this](const QString& brushId, int colorIndex) {
            for (BrushListPackData& pack : m_packs) {
                for (BrushListBrushData& brush : pack.brushes) {
                    if (brush.id == brushId) {
                        brush.displayColorIndex = colorIndex;
                        for (FilterPage& page : m_filterPages) {
                            if (auto* section = page.sections.value(pack.id, nullptr)) {
                                section->updateBrushDisplayColorIndex(brushId, colorIndex);
                            }
                            if (auto* favorites = page.sections.value(
                                    QLatin1String(kFavoritesSectionId), nullptr)) {
                                favorites->updateBrushDisplayColorIndex(brushId, colorIndex);
                            }
                        }
                        return;
                    }
                }
            }
        });

    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::brushFavoriteChanged, this,
        [this](const QString& brushId, bool) {
            QString sourcePackId;
            for (const BrushListPackData& pack : std::as_const(m_packs)) {
                for (const BrushListBrushData& brush : pack.brushes) {
                    if (brush.id == brushId) {
                        sourcePackId = pack.id;
                        break;
                    }
                }
                if (!sourcePackId.isEmpty()) {
                    break;
                }
            }

            for (FilterPage& page : m_filterPages) {
                if (auto* section = page.sections.value(sourcePackId, nullptr)) {
                    section->updateBrushFavorite(brushId);
                }
                if (auto* favorites
                    = page.sections.value(QLatin1String(kFavoritesSectionId), nullptr)) {
                    favorites->updateBrushFavorite(brushId);
                }
            }

            auto favoritePageIt = m_filterPages.find(QLatin1String(kFavoritesPageKey));
            if (favoritePageIt != m_filterPages.end() && favoritePageIt->built) {
                rebuildPage(QLatin1String(kFavoritesPageKey));
            }
        });

    // Visibility-gated: deferred for hidden (background-tab) instances; flushed
    // on activation via WorkspaceTab.
    ruwa::ui::core::ThemeManager::instance().registerThemeHandler(
        this, [this]() { onThemeChanged(); });

    onThemeChanged();
    reloadFromManager();
}

BrushesPanelContent::~BrushesPanelContent()
{
    s_instances.removeAll(this);
}

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
        [this](ToolId, const QString&) { syncSelectionFromCanvas(); });

    syncSelectionFromCanvas();
}

void BrushesPanelContent::reloadFromManager()
{
    m_packs = collectPacks();

    QSet<QString> validPackIds;
    for (const BrushListPackData& pack : m_packs) {
        validPackIds.insert(pack.id);
    }
    // s_collapsedPackIds is deliberately NOT pruned against the current packs:
    // a reload that runs while the manager is mid-reset would otherwise drop
    // entries permanently, and a stale id is inert (it only matters if a pack
    // with that id comes back, in which case the user's choice still applies).
    if (m_viewMode == ViewMode::Pack && !validPackIds.contains(m_viewPackId)) {
        m_viewMode = ViewMode::All;
        m_viewPackId.clear();
    }

    ensureSelection();

    syncFilterPages();
    rebuildBuiltPages();
    ensurePageBuilt(currentPageKey());
    if (auto pageIt = m_filterPages.find(currentPageKey());
        pageIt != m_filterPages.end() && pageIt->container) {
        m_pageStack->setCurrentIndexWithoutAnimation(m_pageStack->indexOf(pageIt->container));
        scheduleScrollRestore(currentPageKey());
    }
    emit packFiltersChanged(packFilterIds(), packFilterNames());
}

QStringList BrushesPanelContent::packFilterIds() const
{
    QStringList ids;
    ids.reserve(m_packs.size());
    for (const BrushListPackData& pack : m_packs) {
        ids.append(pack.id);
    }
    return ids;
}

QStringList BrushesPanelContent::packFilterNames() const
{
    QStringList names;
    names.reserve(m_packs.size());
    for (const BrushListPackData& pack : m_packs) {
        names.append(pack.name);
    }
    return names;
}

void BrushesPanelContent::showAllPacks()
{
    switchToView(ViewMode::All);
}

void BrushesPanelContent::showFavoriteBrushes()
{
    switchToView(ViewMode::Favorites);
}

void BrushesPanelContent::showPack(const QString& packId)
{
    if (packId.isEmpty()) {
        showAllPacks();
        return;
    }
    switchToView(ViewMode::Pack, packId);
}

void BrushesPanelContent::prepareVisiblePreviews()
{
    auto pageIt = m_filterPages.find(currentPageKey());
    if (pageIt == m_filterPages.end() || !pageIt->scrollArea || !pageIt->scrollArea->viewport()) {
        return;
    }

    QWidget* viewport = pageIt->scrollArea->viewport();
    for (BrushPackListSection* section : std::as_const(pageIt->sections)) {
        if (section) {
            section->prepareVisiblePreviews(viewport);
        }
    }
}

bool BrushesPanelContent::visiblePreviewsReady() const
{
    const auto pageIt = m_filterPages.constFind(currentPageKey());
    if (pageIt == m_filterPages.cend() || !pageIt->scrollArea || !pageIt->scrollArea->viewport()) {
        return true;
    }

    QWidget* viewport = pageIt->scrollArea->viewport();
    for (BrushPackListSection* section : pageIt->sections) {
        if (section && !section->visiblePreviewsReady(viewport)) {
            return false;
        }
    }
    return true;
}

QJsonObject BrushesPanelContent::saveState() const
{
    QJsonObject state;
    state["version"] = kPanelStateVersion;
    QJsonArray collapsedPacks;
    for (auto it = s_collapsedPackIds.cbegin(); it != s_collapsedPackIds.cend(); ++it) {
        collapsedPacks.append(*it);
    }

    state["collapsedPacks"] = collapsedPacks;
    QJsonObject scrollValues;
    for (auto it = m_pageScrollValues.cbegin(); it != m_pageScrollValues.cend(); ++it) {
        scrollValues[it.key()] = qMax(0, it.value());
    }
    for (auto it = m_filterPages.cbegin(); it != m_filterPages.cend(); ++it) {
        if (it->scrollArea && !m_pendingScrollRestoreKeys.contains(it.key())) {
            scrollValues[it.key()] = qMax(0, it->scrollArea->scrollValue());
        }
    }
    state["scrollValues"] = scrollValues;
    return state;
}

void BrushesPanelContent::restoreState(const QJsonObject& state)
{
    if (state.isEmpty()) {
        return;
    }

    m_restoringState = true;

    const int stateVersion = state["version"].toInt(0);
    // Only the first restore of a session seeds the shared set. A tab opened
    // later — or a dock layout preset — carries whatever QSettings held when it
    // was written, which may already be older than the live state.
    if (!s_collapsedStateLoaded) {
        s_collapsedStateLoaded = true;
        s_collapsedPackIds.clear();

        // Every version below 3 stored the *expanded* packs, a list that
        // building a page could rewrite on its own (sections reported a
        // programmatic expand as a user toggle). Such a state cannot be told
        // apart from a real one, so it is dropped once in favour of the
        // all-expanded default; from here on only clicks are recorded.
        if (stateVersion >= 3) {
            const QJsonArray collapsedPacks = state["collapsedPacks"].toArray();
            for (const QJsonValue& value : collapsedPacks) {
                const QString packId = value.toString();
                if (!packId.isEmpty()) {
                    s_collapsedPackIds.insert(packId);
                }
            }
        }
    }

    m_pageScrollValues.clear();
    if (stateVersion >= 2 && state.contains("scrollValues")) {
        const QJsonObject scrollValues = state["scrollValues"].toObject();
        for (auto it = scrollValues.begin(); it != scrollValues.end(); ++it) {
            m_pageScrollValues.insert(it.key(), qMax(0, it.value().toInt()));
        }
    } else if (state.contains("scrollValue")) {
        m_pageScrollValues.insert(
            QLatin1String(kAllPageKey), qMax(0, state["scrollValue"].toInt()));
    }

    reloadFromManager();
    scheduleScrollRestore(currentPageKey());

    // The guard covers this synchronous restore only. It must never outlive it:
    // the scroll position lands asynchronously and may never land at all (a
    // hidden panel gets no geometry, the current page can change first), and a
    // guard stuck on would silently swallow every later state change. The
    // pending-restore key set — not this flag — protects the scroll values.
    m_restoringState = false;
}

void BrushesPanelContent::onManagerBrushRenamed(const QString& brushId, const QString& newName)
{
    // A rename only changes one row's label. Update it in place instead of
    // rebuilding every section (which would recreate all rows and previews on
    // each keystroke). Fall back to a full reload only if the row isn't found.
    QString sourcePackId;
    for (BrushListPackData& pack : m_packs) {
        for (BrushListBrushData& brush : pack.brushes) {
            if (brush.id == brushId) {
                brush.name = newName;
                sourcePackId = pack.id;
                break;
            }
        }
        if (!sourcePackId.isEmpty()) {
            break;
        }
    }

    if (sourcePackId.isEmpty()) {
        queueReload();
        return;
    }

    for (FilterPage& page : m_filterPages) {
        if (auto* section = page.sections.value(sourcePackId, nullptr)) {
            section->updateBrushName(brushId, newName);
        }
        if (auto* favorites = page.sections.value(QLatin1String(kFavoritesSectionId), nullptr)) {
            favorites->updateBrushName(brushId, newName);
        }
    }
}

void BrushesPanelContent::onManagerPresetRenamed(const QString& presetId, const QString& newName)
{
    // A pack rename only changes one section header. Update it in place instead
    // of reloading every section (which recreates all rows and previews and
    // replays the selected row's selection animation). Fall back to a full
    // reload only if the section isn't present yet.
    bool updated = false;
    for (FilterPage& page : m_filterPages) {
        if (auto* section = page.sections.value(presetId, nullptr)) {
            section->updatePackName(newName);
            updated = true;
        }
    }

    // Keep the cached pack data in sync so a later rebuild shows the new name.
    for (BrushListPackData& pack : m_packs) {
        if (pack.id == presetId) {
            pack.name = newName;
            break;
        }
    }
    if (!updated) {
        queueReload();
        return;
    }
    emit packFiltersChanged(packFilterIds(), packFilterNames());
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
    if (packId == QLatin1String(kFavoritesSectionId)) {
        refreshAllScrollGeometry();
        return;
    }

    setPackCollapsed(packId, !expanded);
    refreshAllScrollGeometry();
    notifyStateChanged();
}

void BrushesPanelContent::onBrushActivated(const QString& packId, const QString& brushId)
{
    const bool expandedStateChanged = s_collapsedPackIds.contains(packId);
    if (expandedStateChanged) {
        setPackCollapsed(packId, false);
    }

    for (FilterPage& page : m_filterPages) {
        if (auto* section = page.sections.value(packId, nullptr);
            section && !section->isExpanded()) {
            section->setExpanded(true, true);
        }
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

void BrushesPanelContent::onBrushDeleteRequested(const QString& packId, const QString& brushId)
{
    if (packId.isEmpty() || brushId.isEmpty()
        || BrushManager::instance().presetIdForBrush(brushId) != packId) {
        return;
    }

    BrushManager::instance().removeBrushOrPreset(brushId);
}

void BrushesPanelContent::onThemeChanged()
{
    const int outerMargin = ruwa::ui::core::ThemeManager::instance().scaled(8);
    const int spacing = ruwa::ui::core::ThemeManager::instance().scaled(8);
    for (FilterPage& page : m_filterPages) {
        if (!page.scrollLayout) {
            continue;
        }
        page.scrollLayout->setContentsMargins(outerMargin, outerMargin, outerMargin, outerMargin);
        page.scrollLayout->setSpacing(spacing);
    }

    rebuildBuiltPages();
}

void BrushesPanelContent::syncSelectionFromCanvas()
{
    if (!m_canvasPanel) {
        return;
    }

    // Note: the owning pack is not force-expanded here. This runs on startup
    // and on every tool switch, so doing so would silently re-open a pack the
    // user collapsed. Only an explicit activation (onBrushActivated) opens one.
    m_selectedBrushId = m_canvasPanel->selectedBrushIdForCurrentContext();

    ensureSelection();
    syncSelectionToSections();
}

bool BrushesPanelContent::eventFilter(QObject* watched, QEvent* event)
{
    const QString pageKey = m_scrollViewportPageKeys.value(watched);
    if (!pageKey.isEmpty() && m_pendingScrollRestoreKeys.contains(pageKey)) {
        switch (event->type()) {
        case QEvent::Show:
        case QEvent::Resize:
        case QEvent::LayoutRequest:
            scheduleScrollRestore(pageKey);
            break;
        default:
            break;
        }
    }

    if (!pageKey.isEmpty()
        && (event->type() == QEvent::Show || event->type() == QEvent::Resize
            || event->type() == QEvent::LayoutRequest)) {
        emit visiblePreviewStateChanged();
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
            pack.brushes.append({ brush.id, preset.id, brush.name, brush.settings,
                settings.brushDisplayColorIndex(brush.id) });
        }

        packs.append(pack);
    }

    return packs;
}

void BrushesPanelContent::syncFilterPages()
{
    QStringList desiredKeys { QLatin1String(kFavoritesPageKey), QLatin1String(kAllPageKey) };
    for (const BrushListPackData& pack : m_packs) {
        desiredKeys.append(pack.id);
    }

    QSet<QString> desiredKeySet;
    for (const QString& pageKey : std::as_const(desiredKeys)) {
        desiredKeySet.insert(pageKey);
    }
    const QStringList savedScrollKeys = m_pageScrollValues.keys();
    for (const QString& pageKey : savedScrollKeys) {
        if (!desiredKeySet.contains(pageKey)) {
            m_pageScrollValues.remove(pageKey);
        }
    }
    const QStringList existingKeys = m_filterPages.keys();
    bool stackStructureChanged = false;
    for (const QString& pageKey : existingKeys) {
        if (desiredKeySet.contains(pageKey)) {
            continue;
        }

        FilterPage page = m_filterPages.take(pageKey);
        if (page.scrollArea) {
            disconnect(page.scrollArea, nullptr, this, nullptr);
            m_scrollViewportPageKeys.remove(page.scrollArea->viewport());
        }
        m_pendingScrollRestoreKeys.remove(pageKey);
        m_queuedScrollRestoreKeys.remove(pageKey);
        m_pageScrollValues.remove(pageKey);
        if (page.container) {
            m_pageStack->removeWidget(page.container);
            page.container->deleteLater();
        }
        stackStructureChanged = true;
    }

    for (int i = 0; i < desiredKeys.size(); ++i) {
        const QString& pageKey = desiredKeys[i];
        if (!m_filterPages.contains(pageKey)) {
            createFilterPage(pageKey, i);
            stackStructureChanged = true;
            continue;
        }

        FilterPage& page = m_filterPages[pageKey];
        const int currentIndex = m_pageStack->indexOf(page.container);
        if (currentIndex != i) {
            m_pageStack->removeWidget(page.container);
            m_pageStack->insertWidget(i, page.container);
            stackStructureChanged = true;
        }
    }

    auto currentIt = m_filterPages.find(currentPageKey());
    if (currentIt != m_filterPages.end() && currentIt->container
        && (stackStructureChanged || m_pageStack->currentWidget() != currentIt->container)) {
        m_pageStack->setCurrentIndexWithoutAnimation(m_pageStack->indexOf(currentIt->container));
    }
}

void BrushesPanelContent::createFilterPage(const QString& pageKey, int stackIndex)
{
    FilterPage page;
    page.container = new QWidget(m_pageStack);
    page.container->setAttribute(Qt::WA_TranslucentBackground, true);
    page.container->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* pageLayout = new QVBoxLayout(page.container);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);

    page.scrollArea = new widgets::SmoothScrollArea(page.container);
    page.scrollArea->setFillBackground(false);
    page.scrollArea->setScrollBarTransparentTrack(true);
    page.scrollArea->setScrollBarAlwaysReserved(false);
    page.scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));
    pageLayout->addWidget(page.scrollArea);

    if (page.scrollArea->viewport()) {
        page.scrollArea->viewport()->installEventFilter(this);
        page.scrollArea->viewport()->setAttribute(Qt::WA_TranslucentBackground, true);
        page.scrollArea->viewport()->setAutoFillBackground(false);
        page.scrollArea->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
        m_scrollViewportPageKeys.insert(page.scrollArea->viewport(), pageKey);
    }

    page.scrollContent = new QWidget(page.scrollArea);
    page.scrollContent->setAttribute(Qt::WA_TranslucentBackground, true);
    page.scrollContent->setAutoFillBackground(false);
    page.scrollContent->setStyleSheet(QStringLiteral("background: transparent;"));
    page.scrollLayout = new QVBoxLayout(page.scrollContent);
    const int outerMargin = ruwa::ui::core::ThemeManager::instance().scaled(8);
    page.scrollLayout->setContentsMargins(outerMargin, outerMargin, outerMargin, outerMargin);
    page.scrollLayout->setSpacing(ruwa::ui::core::ThemeManager::instance().scaled(8));
    page.scrollArea->setWidget(page.scrollContent);

    connect(
        page.scrollArea, &widgets::SmoothScrollArea::scrolled, this, [this, pageKey](int value) {
            if (m_pendingScrollRestoreKeys.contains(pageKey)) {
                return;
            }
            m_pageScrollValues.insert(pageKey, value);
            notifyStateChanged();
        });

    m_pageStack->insertWidget(stackIndex, page.container);
    m_filterPages.insert(pageKey, page);
}

void BrushesPanelContent::switchToView(ViewMode viewMode, const QString& packId)
{
    const QString oldPageKey = currentPageKey();
    if (auto oldPageIt = m_filterPages.find(oldPageKey); oldPageIt != m_filterPages.end()
        && oldPageIt->scrollArea && !m_pendingScrollRestoreKeys.contains(oldPageKey)) {
        m_pageScrollValues.insert(oldPageKey, oldPageIt->scrollArea->scrollValue());
    }

    m_viewMode = viewMode;
    m_viewPackId = viewMode == ViewMode::Pack ? packId : QString();
    const QString newPageKey = currentPageKey();
    if (oldPageKey == newPageKey) {
        return;
    }

    auto pageIt = m_filterPages.find(newPageKey);
    if (pageIt == m_filterPages.end()) {
        switchToView(ViewMode::All);
        return;
    }

    ensurePageBuilt(newPageKey);
    m_pageStack->setCurrentWidget(pageIt->container);
    scheduleScrollRestore(newPageKey);
}

QString BrushesPanelContent::currentPageKey() const
{
    switch (m_viewMode) {
    case ViewMode::Favorites:
        return QLatin1String(kFavoritesPageKey);
    case ViewMode::Pack:
        return m_viewPackId;
    case ViewMode::All:
    default:
        return QLatin1String(kAllPageKey);
    }
}

void BrushesPanelContent::ensurePageBuilt(const QString& pageKey)
{
    auto pageIt = m_filterPages.find(pageKey);
    if (pageIt == m_filterPages.end() || pageIt->built) {
        return;
    }
    rebuildPage(pageKey);
}

void BrushesPanelContent::rebuildBuiltPages()
{
    const QStringList pageKeys = m_filterPages.keys();
    for (const QString& pageKey : pageKeys) {
        auto pageIt = m_filterPages.find(pageKey);
        if (pageIt != m_filterPages.end() && pageIt->built) {
            rebuildPage(pageKey);
        }
    }
}

void BrushesPanelContent::rebuildPage(const QString& pageKey)
{
    auto pageIt = m_filterPages.find(pageKey);
    if (pageIt == m_filterPages.end()) {
        return;
    }

    FilterPage& page = pageIt.value();
    // Only snapshot a scroll position that is actually the user's: while a
    // restore for this page is still pending the area sits at 0, and storing
    // that would erase the value we are about to apply.
    if (page.scrollArea && !m_pendingScrollRestoreKeys.contains(pageKey)) {
        m_pageScrollValues.insert(pageKey, page.scrollArea->scrollValue());
    }
    m_pendingScrollRestoreKeys.insert(pageKey);
    clearPage(page);
    page.built = true;

    auto addEmptyState = [&page](const QString& title, const QString& hint) {
        auto* titleLabel = new QLabel(title, page.scrollContent);
        auto* hintLabel = new QLabel(hint, page.scrollContent);
        titleLabel->setObjectName(QStringLiteral("brushes_panel_empty_title"));
        hintLabel->setObjectName(QStringLiteral("brushes_panel_empty_hint"));
        hintLabel->setWordWrap(true);
        page.scrollLayout->addWidget(titleLabel);
        page.scrollLayout->addWidget(hintLabel);
    };

    if (m_packs.isEmpty()) {
        addEmptyState(tr("No brush packs"), tr("Create or restore packs to populate this panel."));
    } else if (pageKey == QLatin1String(kFavoritesPageKey)) {
        BrushListPackData favorites;
        favorites.id = QLatin1String(kFavoritesSectionId);
        favorites.name = tr("Favorites");
        const QSet<QString> favoriteIds
            = ruwa::core::SettingsManager::instance().favoriteBrushIds();
        for (const BrushListPackData& pack : m_packs) {
            for (const BrushListBrushData& brush : pack.brushes) {
                if (favoriteIds.contains(brush.id)) {
                    favorites.brushes.append(brush);
                }
            }
        }

        if (favorites.brushes.isEmpty()) {
            addEmptyState(
                tr("No favorite brushes"), tr("Use a brush context menu to add it to favorites."));
        } else {
            addPackSection(pageKey, page, favorites, true);
        }
    } else if (pageKey == QLatin1String(kAllPageKey)) {
        for (const BrushListPackData& pack : m_packs) {
            addPackSection(pageKey, page, pack);
        }
    } else {
        for (const BrushListPackData& pack : m_packs) {
            if (pack.id == pageKey) {
                addPackSection(pageKey, page, pack, true);
                break;
            }
        }
    }

    page.scrollLayout->addStretch();
    refreshScrollGeometry(pageKey);
    if (pageKey == currentPageKey()) {
        scheduleScrollRestore(pageKey);
    }
}

void BrushesPanelContent::addPackSection(
    const QString& pageKey, FilterPage& page, const BrushListPackData& pack, bool forceExpanded)
{
    auto* section = new BrushPackListSection(page.scrollContent);
    section->setPackData(pack);
    section->setExpanded(forceExpanded || !s_collapsedPackIds.contains(pack.id), false);
    section->setSelectedBrushId(m_selectedBrushId);

    connect(section, &BrushPackListSection::toggled, this, &BrushesPanelContent::onSectionToggled);
    connect(section, &BrushPackListSection::brushActivated, this,
        &BrushesPanelContent::onBrushActivated);
    connect(section, &BrushPackListSection::brushEditorRequested, this,
        &BrushesPanelContent::onBrushEditorRequested);
    connect(section, &BrushPackListSection::brushDeleteRequested, this,
        &BrushesPanelContent::onBrushDeleteRequested);
    connect(section, &BrushPackListSection::contentGeometryChanged, this,
        [this, pageKey]() { refreshScrollGeometry(pageKey); });
    connect(section, &BrushPackListSection::visiblePreviewStateChanged, this,
        &BrushesPanelContent::visiblePreviewStateChanged);

    page.sections.insert(pack.id, section);
    page.scrollLayout->addWidget(section);
}

void BrushesPanelContent::clearPage(FilterPage& page)
{
    page.sections.clear();
    while (QLayoutItem* item = page.scrollLayout->takeAt(0)) {
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

    for (const BrushListPackData& pack : m_packs) {
        if (!pack.brushes.isEmpty()) {
            m_selectedBrushId = pack.brushes.first().id;
            return;
        }
    }

    m_selectedBrushId.clear();
}

void BrushesPanelContent::syncSelectionToSections()
{
    for (FilterPage& page : m_filterPages) {
        for (auto* section : std::as_const(page.sections)) {
            if (section) {
                section->setSelectedBrushId(m_selectedBrushId);
            }
        }
    }
}

void BrushesPanelContent::refreshScrollGeometry(const QString& pageKey)
{
    auto pageIt = m_filterPages.find(pageKey);
    if (pageIt == m_filterPages.end() || !pageIt->scrollArea || !pageIt->scrollContent) {
        return;
    }

    pageIt->scrollContent->adjustSize();
    pageIt->scrollContent->updateGeometry();
    pageIt->scrollArea->refreshScrollGeometry();
}

void BrushesPanelContent::refreshAllScrollGeometry()
{
    const QStringList pageKeys = m_filterPages.keys();
    for (const QString& pageKey : pageKeys) {
        refreshScrollGeometry(pageKey);
    }
}

void BrushesPanelContent::scheduleScrollRestore(const QString& pageKey)
{
    if (!m_pendingScrollRestoreKeys.contains(pageKey)
        || m_queuedScrollRestoreKeys.contains(pageKey)) {
        return;
    }

    m_queuedScrollRestoreKeys.insert(pageKey);
    QTimer::singleShot(0, this, [this, pageKey]() {
        m_queuedScrollRestoreKeys.remove(pageKey);
        applyPendingScrollRestore(pageKey);
    });
}

void BrushesPanelContent::applyPendingScrollRestore(const QString& pageKey)
{
    if (!m_pendingScrollRestoreKeys.contains(pageKey)) {
        return;
    }

    auto pageIt = m_filterPages.find(pageKey);
    if (pageIt == m_filterPages.end() || !pageIt->scrollArea || !pageIt->scrollArea->viewport()) {
        return;
    }
    if (pageIt->scrollArea->viewport()->width() <= 0
        || pageIt->scrollArea->viewport()->height() <= 0) {
        return;
    }

    refreshScrollGeometry(pageKey);
    pageIt->scrollArea->setScrollValue(qMax(0, m_pageScrollValues.value(pageKey, 0)));
    m_pendingScrollRestoreKeys.remove(pageKey);
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

void BrushesPanelContent::setPackCollapsed(const QString& packId, bool collapsed)
{
    if (collapsed) {
        s_collapsedPackIds.insert(packId);
    } else {
        s_collapsedPackIds.remove(packId);
    }
    s_collapsedStateLoaded = true;

    // Mirror the choice onto the other tabs' panels right away. They persist
    // into the same settings key, so leaving them stale would let whichever
    // tab saves last undo this toggle.
    for (BrushesPanelContent* instance : std::as_const(s_instances)) {
        if (instance != this) {
            instance->applySharedCollapsedState();
        }
    }
}

void BrushesPanelContent::applySharedCollapsedState()
{
    for (auto pageIt = m_filterPages.begin(); pageIt != m_filterPages.end(); ++pageIt) {
        const QString& pageKey = pageIt.key();
        const QHash<QString, BrushPackListSection*>& sections = pageIt->sections;
        for (auto it = sections.cbegin(); it != sections.cend(); ++it) {
            BrushPackListSection* section = it.value();
            if (!section || it.key() == QLatin1String(kFavoritesSectionId)) {
                continue;
            }
            // A single-pack page shows its own pack open, exactly as a rebuild
            // through addPackSection() would.
            const bool forceExpanded = pageKey == it.key();
            const bool expanded = forceExpanded || !s_collapsedPackIds.contains(it.key());
            if (section->isExpanded() != expanded) {
                section->setExpanded(expanded, false);
            }
        }
    }
    refreshAllScrollGeometry();
}

void BrushesPanelContent::notifyStateChanged()
{
    if (m_restoringState) {
        return;
    }

    emit stateChanged();
}

} // namespace ruwa::ui::workspace
