// SPDX-License-Identifier: MPL-2.0

// CommandPalette.cpp
#include "CommandPalette.h"
#include "shared/widgets/inputs/SearchBar.h"
#include "shared/i18n/TranslationManager.h"
#include "commands/Command.h"
#include "commands/CommandRegistry.h"
#include "shared/i18n/CommandLocalization.h"
#include "commands/CommandExecutor.h"
#include "commands/ShortcutManager.h"
#include "features/project/RecentProjectsManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/SmoothScrollbar.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

CommandPalette::CommandPalette(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    setupUI();
    setupAnimations();
    connectSignals();

    // Initial state - hidden
    m_showProgress = 0.0;
    m_listShowProgress = 0.0;
}

CommandPalette::~CommandPalette()
{
    delete m_hoverTimer;
    delete m_selectionTimer;
}

void CommandPalette::setupUI()
{
    const auto& theme = ThemeManager::instance();

    setFixedWidth(theme.scaled(PaletteWidth));

    // Create search bar
    m_searchBar = new SearchBar(this);
    m_searchBar->setPlaceholder(
        QCoreApplication::translate("ruwa::ui::widgets::CommandPalette", "Type a command..."));
    m_searchBar->setFixedWidth(theme.scaled(PaletteWidth));
    m_searchBar->setFixedHeight(theme.scaled(SearchBarHeight));
    m_searchBar->move(0, 0);

    m_scrollBar = new SmoothScrollBar(Qt::Vertical, this);
    m_scrollBar->setTransparentTrack(true);
    m_scrollBar->hide();

    // Initial size - just search bar
    setFixedHeight(theme.scaled(SearchBarHeight));

    // Hover animation timer
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setInterval(16); // ~60fps
    connect(m_hoverTimer, &QTimer::timeout, this, &CommandPalette::updateHoverAnimation);

    m_selectionTimer = new QTimer(this);
    m_selectionTimer->setInterval(16);
    connect(m_selectionTimer, &QTimer::timeout, this, &CommandPalette::updateSelectionAnimation);
}

void CommandPalette::setupAnimations()
{
    // Show progress (opacity)
    m_showAnimation = new QPropertyAnimation(this, "showProgress", this);
    m_showAnimation->setDuration(ShowDuration);
    m_showAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // List show
    m_listAnimation = new QPropertyAnimation(this, "listShowProgress", this);
    m_listAnimation->setDuration(ListShowDuration);
    m_listAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_scrollAnimation = new QPropertyAnimation(this, "scrollOffset", this);
    m_scrollAnimation->setDuration(ScrollDuration);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_showAnimation, &QPropertyAnimation::finished, this,
        &CommandPalette::onShowAnimationFinished);
}

void CommandPalette::connectSignals()
{
    connect(m_searchBar, &SearchBar::textChanged, this, &CommandPalette::onSearchTextChanged);

    connect(m_scrollBar, &QScrollBar::valueChanged, this, &CommandPalette::onScrollBarValueChanged);

    connect(m_scrollBar, &SmoothScrollBar::stepScrollRequested, this,
        &CommandPalette::onScrollStepRequested);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &CommandPalette::onThemeChanged);

    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, &CommandPalette::retranslateUi);
}

void CommandPalette::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void CommandPalette::retranslateUi()
{
    if (m_searchBar) {
        m_searchBar->setPlaceholder(
            QCoreApplication::translate("ruwa::ui::widgets::CommandPalette", "Type a command..."));
    }
}

void CommandPalette::showAnimated()
{
    if (m_isShowing)
        return;

    m_isShowing = true;
    m_isHiding = false;

    // Reset state
    {
        const QSignalBlocker blocker(m_searchBar);
        m_searchBar->clear();
    }
    m_items.clear();
    m_selectedIndex = -1;
    m_hoveredIndex = -1;
    m_scrollOffset = 0.0;
    m_targetScrollOffset = 0.0;
    m_listShowProgress = 0.0;
    m_itemHoverProgress.clear();
    m_itemSelectionProgress.clear();
    m_scrollAnimation->stop();

    // Reset size to search bar only
    const auto& theme = ThemeManager::instance();
    setFixedHeight(theme.scaled(SearchBarHeight));
    onSearchTextChanged(QString());

    // Start show animation (opacity only)
    m_showAnimation->stop();
    m_showAnimation->setDuration(ShowDuration);
    m_showAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_showAnimation->setStartValue(0.0);
    m_showAnimation->setEndValue(1.0);

    disconnect(m_showAnimation, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_showAnimation, &QPropertyAnimation::finished, this,
        &CommandPalette::onShowAnimationFinished);

    m_showAnimation->start();

    // Focus search bar after small delay
    QTimer::singleShot(50, this, [this]() {
        if (m_isShowing || (!m_isHiding && m_showProgress > 0.5)) {
            focusSearchBar();
        }
    });
}

void CommandPalette::hideAnimated()
{
    if (m_isHiding)
        return;

    m_isHiding = true;
    m_isShowing = false;

    // Hide list first
    m_listAnimation->stop();
    m_listAnimation->setDuration(HideDuration / 2);
    m_listAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_listAnimation->setStartValue(m_listShowProgress);
    m_listAnimation->setEndValue(0.0);
    m_listAnimation->start();

    // Start hide animation (opacity only)
    m_showAnimation->stop();
    m_showAnimation->setDuration(HideDuration);
    m_showAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_showAnimation->setStartValue(m_showProgress);
    m_showAnimation->setEndValue(0.0);

    disconnect(m_showAnimation, &QPropertyAnimation::finished, nullptr, nullptr);
    connect(m_showAnimation, &QPropertyAnimation::finished, this,
        &CommandPalette::onHideAnimationFinished);

    m_showAnimation->start();
}

void CommandPalette::focusSearchBar()
{
    if (m_searchBar) {
        m_searchBar->setFocus();
    }
}

void CommandPalette::refreshGlassBackdropFrom(QWidget* source)
{
    m_glassBackdropSource = source;
    if (!source || rect().isEmpty()) {
        m_glassBackdrop = {};
        update();
        return;
    }

    QWidget* window = source->window();
    if (!window) {
        m_glassBackdrop = {};
        update();
        return;
    }

    const QPoint topLeftInWindow = mapTo(window, QPoint(0, 0));
    const QRect grabRect(topLeftInWindow, size());
    QPixmap snapshot = window->grab(grabRect);
    if (snapshot.isNull()) {
        m_glassBackdrop = {};
        update();
        return;
    }

    const auto& theme = ThemeManager::instance();
    m_glassBackdrop
        = ruwa::ui::painting::blurSnapshotPixmap(snapshot, theme.scaled(GlassBlurRadius));
    update();
}

bool CommandPalette::isActive() const
{
    return m_isShowing || (!m_isHiding && m_showProgress > 0.01);
}

void CommandPalette::setShowProgress(qreal progress)
{
    if (qFuzzyCompare(m_showProgress, progress))
        return;
    m_showProgress = progress;
    update();
}

void CommandPalette::setListShowProgress(qreal progress)
{
    if (qFuzzyCompare(m_listShowProgress, progress))
        return;
    m_listShowProgress = progress;
    updateScrollBar();
    update();
}

void CommandPalette::setScrollOffset(qreal offset)
{
    const qreal clampedOffset = qBound(0.0, offset, m_maxScrollOffset);
    if (qFuzzyCompare(m_scrollOffset, clampedOffset))
        return;

    m_scrollOffset = clampedOffset;

    if (m_scrollBar) {
        const QSignalBlocker blocker(m_scrollBar);
        m_scrollBar->setValue(qRound(m_scrollOffset));
    }

    update();
}

void CommandPalette::onShowAnimationFinished()
{
    if (m_isShowing) {
        m_isShowing = false;
    }
}

void CommandPalette::onHideAnimationFinished()
{
    if (m_isHiding) {
        m_isHiding = false;
        // Clear state
        const QSignalBlocker blocker(m_searchBar);
        m_searchBar->clear();
        m_items.clear();
        m_itemSelectionProgress.clear();
        updateScrollBar();
    }
}

void CommandPalette::onSearchTextChanged(const QString& text)
{
    populateCommands(text);

    // Show/hide list based on content
    bool hasContent = !m_items.isEmpty();

    if (hasContent && m_listShowProgress < 1.0) {
        m_listAnimation->stop();
        m_listAnimation->setDuration(ListShowDuration);
        m_listAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_listAnimation->setStartValue(m_listShowProgress);
        m_listAnimation->setEndValue(1.0);
        m_listAnimation->start();
    } else if (!hasContent && m_listShowProgress > 0.0) {
        m_listAnimation->stop();
        m_listAnimation->setDuration(ListShowDuration / 2);
        m_listAnimation->setEasingCurve(QEasingCurve::InCubic);
        m_listAnimation->setStartValue(m_listShowProgress);
        m_listAnimation->setEndValue(0.0);
        m_listAnimation->start();
    }
}

void CommandPalette::onThemeChanged()
{
    if (m_glassBackdropSource) {
        refreshGlassBackdropFrom(m_glassBackdropSource);
    }
    updateScrollBar();
    update();
}

void CommandPalette::onScrollBarValueChanged(int value)
{
    if (!m_scrollBar)
        return;

    if (m_scrollBar->isDragging()) {
        m_scrollAnimation->stop();
        m_targetScrollOffset = qBound(0.0, qreal(value), m_maxScrollOffset);
        setScrollOffset(m_targetScrollOffset);
        return;
    }

    scrollToOffset(value, true);
}

void CommandPalette::onScrollStepRequested(int delta)
{
    const qreal baseOffset = m_scrollAnimation->state() == QAbstractAnimation::Running
        ? m_targetScrollOffset
        : m_scrollOffset;
    scrollToOffset(baseOffset + delta, true);
}

void CommandPalette::populateCommands(const QString& filter)
{
    m_items.clear();
    m_selectedIndex = -1;
    m_hoveredIndex = -1;
    m_scrollOffset = 0.0;
    m_targetScrollOffset = 0.0;
    m_scrollAnimation->stop();
    m_itemHoverProgress.clear();
    m_itemSelectionProgress.clear();

    const auto& registry = ruwa::core::CommandRegistry::instance();
    const auto& executor = ruwa::core::CommandExecutor::instance();
    auto& loc = ruwa::i18n::CommandLocalization::instance();

    // Argument mode: only when user has pressed space after alias (e.g. "qnp ")
    const QStringList parts = filter.split(QChar::Space, Qt::SkipEmptyParts);
    const QString firstToken = parts.isEmpty() ? QString() : parts.first().trimmed();
    const bool hasSpaceAfterAlias = filter.length() > firstToken.length();

    if (!filter.isEmpty() && !firstToken.isEmpty() && hasSpaceAfterAlias) {
        ruwa::core::Command* argCmd = registry.findCommandByAlias(firstToken);
        if (argCmd && argCmd->showInPalette() && !argCmd->info().arguments.isEmpty()) {
            const auto info = argCmd->info();
            QKeySequence shortcut
                = ruwa::core::ShortcutManager::instance().shortcutFor(argCmd->id());

            // Check argument type: presets (qnp) or recent projects (op)
            bool hasPresets = false;
            bool hasRecentProjects = false;
            for (const auto& arg : info.arguments) {
                if (!arg.suggestionPresets.isEmpty()) {
                    hasPresets = true;
                    break;
                }
                if (arg.useRecentProjects) {
                    hasRecentProjects = true;
                    break;
                }
            }

            if (hasRecentProjects) {
                // Recent projects mode: "op " shows recent projects, filter by name
                QString filterText = filter.mid(firstToken.length()).trimmed().toLower();

                // "Browse..." item first - opens file dialog when no path in args
                CommandItem browseItem;
                browseItem.commandId = argCmd->id();
                browseItem.title = QCoreApplication::translate("CommandPalette", "Browse...");
                browseItem.category = info.category;
                browseItem.shortcut = shortcut.toString(QKeySequence::NativeText);
                browseItem.enabled = executor.canExecute(argCmd->id());
                browseItem.args = QVariantMap(); // Empty = open file dialog
                browseItem.argumentHint.clear();
                m_items.append(browseItem);

                // Recent projects, filter by project name
                const auto& entries
                    = ruwa::core::serialization::RecentProjectsManager::instance().entries();
                for (const auto& entry : entries) {
                    if (!entry.isValid())
                        continue;
                    if (!filterText.isEmpty()
                        && !entry.projectName.toLower().contains(filterText)) {
                        continue;
                    }
                    CommandItem item;
                    item.commandId = argCmd->id();
                    item.title = entry.projectName;
                    item.category = QFileInfo(entry.filePath).fileName();
                    item.shortcut = shortcut.toString(QKeySequence::NativeText);
                    item.enabled = executor.canExecute(argCmd->id());
                    item.args = QVariantMap { { "path", entry.filePath } };
                    item.argumentHint.clear();
                    m_items.append(item);
                }
            } else if (hasPresets) {
                // Preset mode: Custom at top (updates from input), then preset suggestions
                QString filterText = filter.mid(firstToken.length()).trimmed();
                const QStringList tokens = filterText.split(QChar::Space, Qt::SkipEmptyParts);

                // Parse custom width/height/name from input: "W H" or "W H Name of project"
                int customW = 1920, customH = 1080;
                QString customName;
                if (tokens.size() >= 1) {
                    bool ok = false;
                    int v = tokens[0].toInt(&ok);
                    if (ok && v > 0)
                        customW = qBound(1, v, 100000);
                }
                if (tokens.size() >= 2) {
                    bool ok = false;
                    int v = tokens[1].toInt(&ok);
                    if (ok && v > 0)
                        customH = qBound(1, v, 100000);
                }
                if (tokens.size() >= 3) {
                    customName = tokens.mid(2).join(QChar::Space).trimmed();
                }

                // Custom item always first - reflects typed numbers and name
                QVariantMap customArgs;
                customArgs.insert("width", customW);
                customArgs.insert("height", customH);
                if (!customName.isEmpty()) {
                    customArgs.insert("name", customName);
                }
                CommandItem customItem;
                customItem.commandId = argCmd->id();
                customItem.title = QCoreApplication::translate("CommandPalette", "Custom");
                customItem.category = customName.isEmpty()
                    ? QStringLiteral("%1 × %2").arg(customW).arg(customH)
                    : QStringLiteral("%1 × %2 — %3").arg(customW).arg(customH).arg(customName);
                customItem.shortcut = shortcut.toString(QKeySequence::NativeText);
                customItem.enabled = executor.canExecute(argCmd->id());
                customItem.args = customArgs;
                customItem.argumentHint.clear();
                m_items.append(customItem);

                // Preset suggestions, filter by text
                const QString filterLower = filterText.toLower();
                for (const auto& arg : info.arguments) {
                    if (arg.suggestionPresets.isEmpty())
                        continue;

                    for (const auto& preset : arg.suggestionPresets) {
                        if (!preset.displayName.toLower().contains(filterLower)) {
                            continue;
                        }
                        int w = preset.value.value("width", 1920).toInt();
                        int h = preset.value.value("height", 1080).toInt();
                        CommandItem item;
                        item.commandId = argCmd->id();
                        item.title = preset.displayName;
                        item.category = QStringLiteral("%1 × %2").arg(w).arg(h);
                        item.shortcut = shortcut.toString(QKeySequence::NativeText);
                        item.enabled = executor.canExecute(argCmd->id());
                        item.args = preset.value;
                        item.argumentHint.clear();
                        m_items.append(item);
                    }
                    break;
                }
            } else {
                // Legacy: width/height style args
                QVariantMap args;
                QStringList hintParts;
                for (int i = 0; i < info.arguments.size(); ++i) {
                    const auto& arg = info.arguments[i];
                    QString value = (i + 1 < parts.size()) ? parts[i + 1].trimmed() : QString();
                    if (!value.isEmpty()) {
                        args[arg.name] = value;
                        hintParts.append(QStringLiteral("%1: %2").arg(arg.hint, value));
                    } else {
                        hintParts.append(QStringLiteral("%1: %2").arg(arg.hint, arg.placeholder));
                    }
                }
                CommandItem mainItem;
                mainItem.commandId = argCmd->id();
                mainItem.title
                    = loc.title(argCmd->id()).isEmpty() ? info.title : loc.title(argCmd->id());
                mainItem.category = loc.category(argCmd->id()).isEmpty()
                    ? info.category
                    : loc.category(argCmd->id());
                mainItem.shortcut = shortcut.toString(QKeySequence::NativeText);
                mainItem.enabled = executor.canExecute(argCmd->id());
                mainItem.args = args;
                mainItem.argumentHint = hintParts.join(QStringLiteral("  "));
                m_items.append(mainItem);
            }

            m_itemHoverProgress.resize(m_items.size());
            m_itemHoverProgress.fill(0.0);
            m_itemSelectionProgress.resize(m_items.size());
            m_itemSelectionProgress.fill(0.0);
            m_selectedIndex = 0;
            if (!m_itemSelectionProgress.isEmpty()) {
                m_itemSelectionProgress[0] = 1.0;
            }
            updateSize();
            update();
            return;
        }
    }

    // Normal search mode
    QList<ruwa::core::Command*> commands = registry.search(filter);

    if (filter.isEmpty()) {
        std::sort(commands.begin(), commands.end(),
            [&loc](ruwa::core::Command* left, ruwa::core::Command* right) {
                const auto leftInfo = left->info();
                const auto rightInfo = right->info();
                const QString leftTitle
                    = loc.title(left->id()).isEmpty() ? leftInfo.title : loc.title(left->id());
                const QString rightTitle
                    = loc.title(right->id()).isEmpty() ? rightInfo.title : loc.title(right->id());

                const int titleCompare = QString::localeAwareCompare(leftTitle, rightTitle);
                if (titleCompare != 0) {
                    return titleCompare < 0;
                }
                return left->id() < right->id();
            });
    }

    for (ruwa::core::Command* cmd : commands) {
        const auto info = cmd->info();
        QKeySequence shortcut = ruwa::core::ShortcutManager::instance().shortcutFor(cmd->id());

        CommandItem item;
        item.commandId = cmd->id();
        item.title = loc.title(cmd->id()).isEmpty() ? info.title : loc.title(cmd->id());
        item.category = loc.category(cmd->id()).isEmpty() ? info.category : loc.category(cmd->id());
        item.shortcut = shortcut.toString(QKeySequence::NativeText);
        item.enabled = executor.canExecute(cmd->id());

        m_items.append(item);
    }

    // Initialize hover progress for all items
    m_itemHoverProgress.resize(m_items.size());
    m_itemHoverProgress.fill(0.0);
    m_itemSelectionProgress.resize(m_items.size());
    m_itemSelectionProgress.fill(0.0);

    // Select first item
    if (!m_items.isEmpty()) {
        m_selectedIndex = 0;
        m_itemSelectionProgress[0] = 1.0;
    }

    updateSize();
    update();
}

void CommandPalette::executeSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_items.size()) {
        return;
    }

    const CommandItem& item = m_items[m_selectedIndex];
    const QString commandId = item.commandId;
    const QVariantMap args = item.args;

    // Commands with palettePrefillAlias: when selected without args, prefill alias + space
    // instead of executing (so user can type the argument)
    const auto& registry = ruwa::core::CommandRegistry::instance();
    ruwa::core::Command* cmd = registry.command(commandId);
    if (cmd && args.isEmpty()) {
        const QString prefill = cmd->info().palettePrefillAlias;
        if (!prefill.isEmpty()) {
            m_searchBar->setText(prefill + QChar::Space);
            m_searchBar->setFocus();
            return; // Don't close — stay for argument input
        }
    }

    // Request close first
    emit closeRequested();

    // Execute command after a small delay to let animations start
    QTimer::singleShot(50, this,
        [commandId, args]() { ruwa::core::CommandExecutor::instance().execute(commandId, args); });
}

void CommandPalette::selectNext()
{
    if (m_items.isEmpty())
        return;

    m_selectedIndex = (m_selectedIndex + 1) % m_items.size();
    if (!m_selectionTimer->isActive()) {
        m_selectionTimer->start();
    }
    ensureItemVisible(m_selectedIndex);
    update();
}

void CommandPalette::selectPrevious()
{
    if (m_items.isEmpty())
        return;

    m_selectedIndex = (m_selectedIndex - 1 + m_items.size()) % m_items.size();
    if (!m_selectionTimer->isActive()) {
        m_selectionTimer->start();
    }
    ensureItemVisible(m_selectedIndex);
    update();
}

void CommandPalette::updateSize()
{
    const auto& theme = ThemeManager::instance();

    int searchH = theme.scaled(SearchBarHeight);
    int listMargin = theme.scaled(ListMarginTop);
    int itemH = theme.scaled(ItemHeight);
    int listPadding = theme.scaled(ListPadding);

    if (m_items.isEmpty()) {
        setFixedHeight(searchH);
        m_maxScrollOffset = 0;
        updateScrollBar();
        return;
    }

    int visibleItems = qMin(m_items.size(), MaxVisibleItems);
    int listHeight = visibleItems * itemH + listPadding * 2;
    int totalHeight = searchH + listMargin + listHeight;

    setFixedHeight(totalHeight);

    // Calculate max scroll
    int totalContentHeight = m_items.size() * itemH;
    int viewableHeight = listHeight - listPadding * 2;
    m_maxScrollOffset = qMax(0.0, qreal(totalContentHeight - viewableHeight));
    m_scrollOffset = qBound(0.0, m_scrollOffset, m_maxScrollOffset);
    m_targetScrollOffset = qBound(0.0, m_targetScrollOffset, m_maxScrollOffset);
    updateScrollBar();
}

void CommandPalette::scrollToOffset(qreal offset, bool animated)
{
    m_targetScrollOffset = qBound(0.0, offset, m_maxScrollOffset);

    if (animated && !qFuzzyCompare(m_targetScrollOffset, m_scrollOffset)) {
        m_scrollAnimation->stop();
        m_scrollAnimation->setDuration(ScrollDuration);
        m_scrollAnimation->setStartValue(m_scrollOffset);
        m_scrollAnimation->setEndValue(m_targetScrollOffset);
        m_scrollAnimation->start();
        if (m_scrollBar) {
            m_scrollBar->showAnimated();
        }
        return;
    }

    m_scrollAnimation->stop();
    setScrollOffset(m_targetScrollOffset);
}

void CommandPalette::updateScrollBar()
{
    if (!m_scrollBar)
        return;

    const QSignalBlocker blocker(m_scrollBar);

    if (m_items.isEmpty() || m_maxScrollOffset <= 0.0 || m_listShowProgress <= 0.001) {
        m_scrollBar->hide();
        m_scrollBar->setRange(0, 0);
        return;
    }

    const auto& theme = ThemeManager::instance();
    const QRectF list = listRect();
    const int padding = theme.scaled(ListPadding);
    const int scrollbarWidth = m_scrollBar->width();

    m_scrollBar->setGeometry(qRound(list.right()) - padding - scrollbarWidth,
        qRound(list.top()) + padding, scrollbarWidth, qMax(0, qRound(list.height()) - padding * 2));

    const int viewableHeight = qMax(0, qRound(list.height()) - padding * 2);
    m_scrollBar->setRange(0, qRound(m_maxScrollOffset));
    m_scrollBar->setPageStep(viewableHeight);
    m_scrollBar->setSingleStep(qMax(theme.scaled(20), viewableHeight / 4));
    m_scrollBar->setValue(qRound(m_scrollOffset));
    m_scrollBar->show();
}

QRectF CommandPalette::listRect() const
{
    const auto& theme = ThemeManager::instance();

    int searchH = theme.scaled(SearchBarHeight);
    int listMargin = theme.scaled(ListMarginTop);

    return QRectF(0, searchH + listMargin, width(), height() - searchH - listMargin);
}

QRectF CommandPalette::itemRect(int index) const
{
    if (index < 0 || index >= m_items.size())
        return QRectF();

    const auto& theme = ThemeManager::instance();
    int itemH = theme.scaled(ItemHeight);
    int padding = theme.scaled(ListPadding);
    int scrollbarSpace = theme.scaled(ScrollbarWidth + 8);

    QRectF list = listRect();

    qreal y = list.top() + padding + index * itemH - m_scrollOffset;

    return QRectF(list.left() + padding, y, list.width() - padding * 2 - scrollbarSpace, itemH);
}

int CommandPalette::itemAtPosition(const QPoint& pos) const
{
    QRectF list = listRect();
    if (!list.contains(pos))
        return -1;

    const auto& theme = ThemeManager::instance();
    int itemH = theme.scaled(ItemHeight);
    int padding = theme.scaled(ListPadding);

    qreal relativeY = pos.y() - list.top() - padding + m_scrollOffset;
    int index = static_cast<int>(relativeY / itemH);

    if (index >= 0 && index < m_items.size()) {
        return index;
    }
    return -1;
}

void CommandPalette::ensureItemVisible(int index)
{
    if (index < 0 || index >= m_items.size())
        return;
    if (m_maxScrollOffset <= 0)
        return;

    const auto& theme = ThemeManager::instance();
    int itemH = theme.scaled(ItemHeight);
    int padding = theme.scaled(ListPadding);

    QRectF list = listRect();
    qreal viewableHeight = list.height() - padding * 2;

    qreal itemTop = index * itemH;
    qreal itemBottom = itemTop + itemH;

    if (itemTop < m_scrollOffset) {
        scrollToOffset(itemTop, false);
    } else if (itemBottom > m_scrollOffset + viewableHeight) {
        scrollToOffset(itemBottom - viewableHeight, false);
    }
}

void CommandPalette::updateHoverAnimation()
{
    bool needsUpdate = false;

    for (int i = 0; i < m_itemHoverProgress.size(); ++i) {
        qreal target = (i == m_hoveredIndex) ? 1.0 : 0.0;
        qreal& current = m_itemHoverProgress[i];

        if (!qFuzzyCompare(current, target)) {
            qreal step = (target > current) ? 0.15 : -0.12;
            current = qBound(0.0, current + step, 1.0);
            needsUpdate = true;
        }
    }

    if (needsUpdate) {
        update();
    } else {
        m_hoverTimer->stop();
    }
}

void CommandPalette::updateSelectionAnimation()
{
    bool needsUpdate = false;

    for (int i = 0; i < m_itemSelectionProgress.size(); ++i) {
        const qreal target = (i == m_selectedIndex) ? 1.0 : 0.0;
        qreal& current = m_itemSelectionProgress[i];

        if (!qFuzzyCompare(current, target)) {
            const qreal step = target > current ? SelectionSelectStep : -SelectionUnselectStep;
            current = qBound(0.0, current + step, 1.0);
            needsUpdate = true;
        }
    }

    if (needsUpdate) {
        update();
    } else {
        m_selectionTimer->stop();
    }
}

void CommandPalette::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_showProgress <= 0.001)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Apply show progress opacity
    painter.setOpacity(m_showProgress);

    // Draw search bar background
    drawSearchBarBackground(painter);

    // Draw list if visible
    if (m_listShowProgress > 0.001 && !m_items.isEmpty()) {
        painter.setOpacity(m_showProgress * m_listShowProgress);
        drawList(painter);
    }
}

void CommandPalette::drawSearchBarBackground(QPainter& painter)
{
    const auto& theme = ThemeManager::instance();

    int searchH = theme.scaled(SearchBarHeight);
    int radius = searchH / 2;

    QRectF rect(0, 0, width(), searchH);
    drawGlassPanel(painter, rect, radius, true);
}

void CommandPalette::drawList(QPainter& painter)
{
    QRectF list = listRect();

    drawListBackground(painter, list);

    // Clip to list area (with padding)
    const auto& theme = ThemeManager::instance();
    int padding = theme.scaled(ListPadding);
    painter.setClipRect(list.adjusted(padding, padding, -padding, -padding));

    // Draw items
    for (int i = 0; i < m_items.size(); ++i) {
        QRectF itemR = itemRect(i);

        // Skip if outside visible area
        if (itemR.bottom() < list.top() || itemR.top() > list.bottom()) {
            continue;
        }

        drawItem(painter, i, itemR);
    }

    painter.setClipping(false);
}

void CommandPalette::drawListBackground(QPainter& painter, const QRectF& rect)
{
    const auto& theme = ThemeManager::instance();
    int radius = theme.scaled(ListRadius);

    drawGlassPanel(painter, rect, radius, false);
}

void CommandPalette::drawGlassPanel(
    QPainter& painter, const QRectF& rect, int radius, bool hoverBorder)
{
    const auto& colors = ThemeManager::instance().colors();
    const QColor borderTop = hoverBorder ? colors.borderSubtleHover() : colors.borderSubtle();
    QSizeF backdropSize(size());
    if (!m_glassBackdrop.isNull()) {
        const qreal dpr = m_glassBackdrop.devicePixelRatio();
        backdropSize = QSizeF(m_glassBackdrop.width() / dpr, m_glassBackdrop.height() / dpr);
    }

    ruwa::ui::painting::drawTonedGlassPanel(painter, rect, radius, backdropSize, m_glassBackdrop,
        colors.surface, colors.primary, colors.isDark, borderTop,
        ThemeColors::withAlpha(colors.borderSubtle(), colors.borderSubtle().alpha() / 2));
}

void CommandPalette::drawItem(QPainter& painter, int index, const QRectF& rect)
{
    if (index < 0 || index >= m_items.size())
        return;

    const auto& item = m_items[index];
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    bool isSelected = (index == m_selectedIndex);
    qreal hoverProg = (index < m_itemHoverProgress.size()) ? m_itemHoverProgress[index] : 0.0;
    qreal selectionProg
        = (index < m_itemSelectionProgress.size()) ? m_itemSelectionProgress[index] : 0.0;

    int radius = theme.scaled(ItemRadius);
    QRectF itemBg = rect.adjusted(2, 1, -2, -1);

    drawSelectionHighlight(painter, itemBg, selectionProg);

    if (hoverProg > 0) {
        QColor hoverColor = colors.isDark ? QColor(255, 255, 255) : colors.primary;
        hoverColor.setAlphaF((colors.isDark ? 0.045 : 0.035) * hoverProg);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hoverColor);
        painter.drawRoundedRect(itemBg, radius, radius);
    }

    // Content area
    int paddingH = theme.scaled(12);
    int paddingV = theme.scaled(8);
    const int contentShift = qRound(theme.scaled(8) * qBound<qreal>(0.0, selectionProg, 1.0));
    QRectF contentRect = rect.adjusted(paddingH + contentShift, paddingV, -paddingH, -paddingV);

    // Text colors
    QColor titleColor = colors.text;
    if (hoverProg > 0) {
        titleColor = ThemeColors::adjustBrightness(colors.text, 1.0 + hoverProg * 0.15);
    }

    QColor categoryColor = colors.textMuted;
    if (selectionProg > 0.0) {
        categoryColor = ThemeColors::withAlpha(
            categoryColor, qMin(255, qRound(categoryColor.alpha() * (1.0 + selectionProg * 0.25))));
    }

    // Title
    QFont titleFont = font();
    titleFont.setPointSize(theme.scaledFontSize(10));
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(titleColor);

    QRectF titleRect = contentRect;
    titleRect.setHeight(contentRect.height() * 0.55);
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignBottom, item.title);

    // Category or argument hint
    QFont categoryFont = font();
    categoryFont.setPointSize(theme.scaledFontSize(8));
    painter.setFont(categoryFont);
    painter.setPen(categoryColor);

    QRectF categoryRect = contentRect;
    categoryRect.setTop(titleRect.bottom() + theme.scaled(2));
    if (!item.argumentHint.isEmpty()) {
        painter.drawText(categoryRect, Qt::AlignLeft | Qt::AlignTop, item.argumentHint);
    } else {
        painter.drawText(categoryRect, Qt::AlignLeft | Qt::AlignTop, item.category);
    }

    // Shortcut badge
    if (!item.shortcut.isEmpty()) {
        QFont shortcutFont = font();
        shortcutFont.setPointSize(theme.scaledFontSize(8));
        painter.setFont(shortcutFont);

        QFontMetrics fm(shortcutFont);
        int badgeW = fm.horizontalAdvance(item.shortcut) + theme.scaled(12);
        int badgeH = theme.scaled(22);

        QRectF badgeRect(
            contentRect.right() - badgeW, contentRect.center().y() - badgeH / 2.0, badgeW, badgeH);

        QColor badgeBg = isSelected
            ? ThemeColors::withAlpha(colors.primary, qRound(42 * qMax<qreal>(selectionProg, 0.35)))
            : colors.surfaceAlt;

        painter.setPen(Qt::NoPen);
        painter.setBrush(badgeBg);
        painter.drawRoundedRect(badgeRect, theme.scaled(4), theme.scaled(4));

        QColor badgeText = isSelected ? colors.text : colors.textMuted;
        painter.setPen(badgeText);
        painter.drawText(badgeRect, Qt::AlignCenter, item.shortcut);
    }
}

void CommandPalette::drawSelectionHighlight(
    QPainter& painter, const QRectF& rect, qreal selectionProgress)
{
    if (selectionProgress <= 0.0)
        return;

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const qreal progress = qBound<qreal>(0.0, selectionProgress, 1.0);

    QColor selectionFill = colors.primary;
    selectionFill.setAlphaF(0.14 * progress);

    painter.setPen(Qt::NoPen);
    painter.setBrush(selectionFill);
    painter.drawRoundedRect(rect, theme.scaled(ItemRadius), theme.scaled(ItemRadius));

    const int verticalInset = qMax(3, theme.scaled(6));
    const int capsuleWidth = qMax(3, theme.scaled(3));
    const int capsuleLeft = qRound(rect.left()) + qMax(4, theme.scaled(6));
    const int fullCapsuleHeight = qMax(0, qRound(rect.height()) - verticalInset * 2);
    const int animatedCapsuleHeight = qRound(static_cast<qreal>(fullCapsuleHeight) * progress);

    if (capsuleWidth <= 0 || animatedCapsuleHeight <= 0) {
        return;
    }

    QColor capsuleColor
        = ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.08 : 0.96);
    capsuleColor.setAlphaF(0.74 * progress);

    const int capsuleTop = qRound(rect.top()) + verticalInset
        + qMax(0, (fullCapsuleHeight - animatedCapsuleHeight) / 2);
    const QRectF capsuleRect(capsuleLeft, capsuleTop, capsuleWidth, animatedCapsuleHeight);
    const qreal capsuleRadius = qMin(capsuleRect.width(), capsuleRect.height()) * 0.5;

    QPainterPath capsulePath;
    capsulePath.addRoundedRect(capsuleRect, capsuleRadius, capsuleRadius);
    painter.setBrush(capsuleColor);
    painter.drawPath(capsulePath);
}

void CommandPalette::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        emit closeRequested();
        event->accept();
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        executeSelected();
        event->accept();
        break;
    case Qt::Key_Up:
        selectPrevious();
        event->accept();
        break;
    case Qt::Key_Down:
        selectNext();
        event->accept();
        break;
    default:
        // Forward to search bar
        if (m_searchBar && !event->text().isEmpty()) {
            m_searchBar->setFocus();
        }
        QWidget::keyPressEvent(event);
    }
}

void CommandPalette::mouseMoveEvent(QMouseEvent* event)
{
    int index = itemAtPosition(event->pos());

    if (index != m_hoveredIndex) {
        m_hoveredIndex = index;

        if (!m_hoverTimer->isActive()) {
            m_hoverTimer->start();
        }
    }

    QWidget::mouseMoveEvent(event);
}

void CommandPalette::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        int index = itemAtPosition(event->pos());
        if (index >= 0 && index < m_items.size()) {
            m_selectedIndex = index;
            update();
            executeSelected();
            event->accept();
            return;
        }
    }

    QWidget::mousePressEvent(event);
}

void CommandPalette::wheelEvent(QWheelEvent* event)
{
    if (m_maxScrollOffset <= 0) {
        event->ignore();
        return;
    }

    qreal delta = 0.0;
    if (!event->pixelDelta().isNull()) {
        delta = -event->pixelDelta().y() * WheelPixelMultiplier;
    } else {
        const auto& theme = ThemeManager::instance();
        delta = (-event->angleDelta().y() / 120.0) * theme.scaled(ItemHeight) * WheelItemsPerStep;
    }

    if (qFuzzyIsNull(delta)) {
        event->accept();
        return;
    }

    const qreal baseOffset = m_scrollAnimation->state() == QAbstractAnimation::Running
        ? m_targetScrollOffset
        : m_scrollOffset;
    scrollToOffset(baseOffset + delta, true);
    event->accept();
}

void CommandPalette::leaveEvent(QEvent* event)
{
    if (m_hoveredIndex >= 0) {
        m_hoveredIndex = -1;
        if (!m_hoverTimer->isActive()) {
            m_hoverTimer->start();
        }
    }
    QWidget::leaveEvent(event);
}

} // namespace ruwa::ui::widgets
