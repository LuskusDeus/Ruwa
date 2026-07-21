// SPDX-License-Identifier: MPL-2.0

// RecentProjectsWidget.cpp
#include "RecentProjectsWidget.h"
#include "RecentProjectItem.h"
#include "RecentProjectCard.h"
#include "shared/resources/FontFamilyNames.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/SearchBar.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/layout/AnimatedViewSwitcher.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/project/RecentProjectsManager.h"
#include "features/project/ThumbnailCache.h"
#include "shared/resources/IconProvider.h"
#include "shared/i18n/TranslationManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLocale>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPixmap>
#include <QTimer>
#include <QDateTime>
#include <QSettings>

namespace ruwa::ui::widgets {

namespace {
const int BASE_MAIN_SPACING = 16;
constexpr const char* kRecentProjectsViewModeKey = "RecentProjects/ViewMode";
const int BASE_HEADER_HEIGHT = 32;
const int BASE_HEADER_SPACING = 16;
const int BASE_TITLE_FONT_SIZE = 16;
const int BASE_LIST_SPACING = 8;
const int BASE_GRID_H_SPACING = 16;
const int BASE_GRID_V_SPACING = 16;
const int BASE_NO_RESULTS_FONT_SIZE = 10;
const int BASE_EMPTY_STATE_FONT_SIZE = 12;
const int BASE_ASCII_ART_FONT_SIZE = 9;

constexpr const char* kNoRecentProjectsMessage = QT_TRANSLATE_NOOP(
    "ruwa::ui::widgets::RecentProjectsWidget", "It seems you haven't opened any projects recently");

constexpr const char* kNoRecentProjectsAsciiArt
    = "           __..--''``\\--....___   _..,_\n"
      "       _.-'    .-/\";  `        ``<._  ``-+'~=.\n"
      "   _.-' _..--.'_    \\                    `(^) )\n"
      "  ((..-'    (< _     ;_..__               ; `'\n"
      "         `-._,_)'      ``--...____..-'";

QString formatRelativeDate(const QDateTime& dateTime, const char* context)
{
    if (!dateTime.isValid())
        return {};

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 secsAgo = dateTime.secsTo(now);

    if (secsAgo < 0)
        return QCoreApplication::translate(context, "Just now");
    if (secsAgo < 60)
        return QCoreApplication::translate(context, "Just now");
    if (secsAgo < 3600)
        return QCoreApplication::translate(context, "%1 min ago").arg(secsAgo / 60);
    if (secsAgo < 86400)
        return QCoreApplication::translate(context, "%1 hours ago").arg(secsAgo / 3600);

    const int daysAgo = dateTime.date().daysTo(now.date());
    if (daysAgo == 1)
        return QCoreApplication::translate(context, "Yesterday");
    if (daysAgo < 7)
        return QCoreApplication::translate(context, "%1 days ago").arg(daysAgo);
    if (daysAgo < 30)
        return QCoreApplication::translate(context, "%1 weeks ago").arg(daysAgo / 7);
    if (daysAgo < 365)
        return QCoreApplication::translate(context, "%1 months ago").arg(daysAgo / 30);

    return dateTime.date().toString(QLocale().dateFormat(QLocale::ShortFormat));
}

QString formatFileSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes);
}

QFont getAsciiArtFont(int pointSize)
{
    // Prefer known monospace fonts for consistent ASCII art rendering
    const QStringList monoFonts = {
        ruwa::ui::core::FontFamilyNames::Consolas,
        ruwa::ui::core::FontFamilyNames::Monaco,
        ruwa::ui::core::FontFamilyNames::CourierNew,
        QStringLiteral("Liberation Mono"),
        QStringLiteral("DejaVu Sans Mono"),
    };
    for (const QString& name : monoFonts) {
        if (QFontDatabase().hasFamily(name)) {
            QFont f(name);
            f.setPointSize(pointSize);
            f.setStyleHint(QFont::TypeWriter);
            return f;
        }
    }
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(pointSize);
    return f;
}

QPixmap loadRecentProjectIcon(
    const QString& projectFilePath, const QString& tabIconAlias, int iconSize)
{
    if (projectFilePath.isEmpty()) {
        return {};
    }

    // Use entry.tabIconAlias only - never load full project on main thread (blocks UI)
    QString iconAlias = tabIconAlias;
    if (iconAlias.isEmpty()) {
        iconAlias = QStringLiteral("Brush");
    }

    QIcon icon = ruwa::ui::core::ThemeManager::instance().icons().getIcon(iconAlias);
    if (icon.isNull()) {
        return {};
    }

    return icon.pixmap(iconSize, iconSize);
}
} // namespace

RecentProjectsWidget::RecentProjectsWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    // Apply initial scaled sizes
    updateScaledSizes();

    // Connect to theme changes
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &RecentProjectsWidget::onThemeChanged);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, [this]() { retranslateUi(); });
}

void RecentProjectsWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    // Retranslation via TranslationManager::languageChanged (after installTranslator).
}

void RecentProjectsWidget::updateEmptyStateMinHeight()
{
    if (!m_projectsData.isEmpty()) {
        return;
    }
    if (m_listScrollArea && m_listContent) {
        if (QWidget* vp = m_listScrollArea->viewport()) {
            int vh = vp->height();
            if (vh > 0) {
                m_listContent->setMinimumHeight(vh);
            }
        }
    }
    if (m_gridScrollArea && m_gridContent) {
        if (QWidget* vp = m_gridScrollArea->viewport()) {
            int vh = vp->height();
            if (vh > 0) {
                m_gridContent->setMinimumHeight(vh);
            }
        }
    }
}

bool RecentProjectsWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Resize) {
        if ((m_listScrollArea && watched == m_listScrollArea->viewport())
            || (m_gridScrollArea && watched == m_gridScrollArea->viewport())) {
            updateEmptyStateMinHeight();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RecentProjectsWidget::retranslateUi()
{
    if (m_titleLabel)
        m_titleLabel->setText(tr("Recent Projects"));
    // Reload to refresh relative dates (formatRelativeDate), "No projects found", and empty state
    // message
    reloadFromManager();
}

void RecentProjectsWidget::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // === HEADER: Title + View Toggle ===
    m_headerWidget = new QWidget(this);
    QHBoxLayout* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    // Title
    m_titleLabel = new QLabel(tr("Recent Projects"), m_headerWidget);
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    m_titleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    headerLayout->addWidget(m_titleLabel);

    headerLayout->addSpacing(theme.scaled(BASE_HEADER_SPACING));

    // Search bar: stretches to fill space between title and view selector.
    // Capsule style matches the non-accented WelcomeBanner button (no fill, soft border).
    m_searchBar = new SearchBar(m_headerWidget);
    m_searchBar->setClickOutsideClearsFocus(true);
    m_searchBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_searchBar, &SearchBar::textChanged, this, &RecentProjectsWidget::filterProjects);
    headerLayout->addWidget(m_searchBar, 1);

    headerLayout->addSpacing(theme.scaled(BASE_HEADER_SPACING));

    // View selector: List / CardView icons from IconProvider
    auto& icons = ruwa::ui::core::IconProvider::instance();
    QIcon listIcon = icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::List);
    QIcon cardViewIcon = icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::CardView);

    QVector<SegmentedOptionSelector::Option> viewOptions;
    viewOptions.append({ QString(), listIcon, int(ViewMode::List) });
    viewOptions.append({ QString(), cardViewIcon, int(ViewMode::Grid) });
    m_viewSelector = new SegmentedOptionSelector(viewOptions, m_headerWidget);
    m_viewSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::IconsOnly);
    connect(m_viewSelector, &SegmentedOptionSelector::selectionChanged, this, [this](int index) {
        ViewMode mode = (index == 0) ? ViewMode::List : ViewMode::Grid;
        onViewModeChanged(mode);
    });
    headerLayout->addWidget(m_viewSelector);

    m_mainLayout->addWidget(m_headerWidget);

    // === ANIMATED VIEW SWITCHER ===
    m_viewSwitcher = new AnimatedViewSwitcher(this);
    m_viewSwitcher->setAnimationDuration(300);
    m_viewSwitcher->setAnimationEasing(QEasingCurve::OutCubic);

    // === LIST VIEW (index 0) ===
    m_listScrollArea = new SmoothScrollArea();
    m_listScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_listContent = new QWidget();
    m_listContent->setObjectName("listContent");
    m_listContent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    m_listLayout = new QVBoxLayout(m_listContent);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setAlignment(Qt::AlignTop);

    m_listScrollArea->setWidget(m_listContent);
    m_listScrollArea->viewport()->installEventFilter(this);

    // === GRID VIEW (index 1) ===
    m_gridScrollArea = new SmoothScrollArea();
    m_gridScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_gridContent = new QWidget();
    m_gridContent->setObjectName("gridContent");
    m_gridContent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    m_gridLayout = new QGridLayout(m_gridContent);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_gridScrollArea->setWidget(m_gridContent);
    m_gridScrollArea->viewport()->installEventFilter(this);

    // Set views in switcher
    m_viewSwitcher->setFirstView(m_listScrollArea);
    m_viewSwitcher->setSecondView(m_gridScrollArea);

    m_mainLayout->addWidget(m_viewSwitcher, 1);

    // Load saved view mode (0 = List, 1 = Grid)
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    const int savedMode = settings.value(kRecentProjectsViewModeKey, 0).toInt();
    const ViewMode mode = (savedMode == 1) ? ViewMode::Grid : ViewMode::List;
    setViewMode(mode);
}

void RecentProjectsWidget::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    // Main layout spacing
    if (m_mainLayout) {
        m_mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));
    }

    // Header
    if (m_headerWidget) {
        int headerHeight = theme.scaled(BASE_HEADER_HEIGHT);
        if (m_viewSelector) {
            headerHeight = qMax(headerHeight, m_viewSelector->sizeHint().height());
        }
        if (m_searchBar) {
            // SearchBar uses setFixedHeight internally → minimumHeight() is authoritative.
            headerHeight = qMax(headerHeight, m_searchBar->minimumHeight());
            headerHeight = qMax(headerHeight, m_searchBar->sizeHint().height());
        }
        m_headerWidget->setFixedHeight(headerHeight);
    }

    // Header spacing
    if (m_headerWidget) {
        if (QHBoxLayout* headerLayout = qobject_cast<QHBoxLayout*>(m_headerWidget->layout())) {
            const int spacing = theme.scaled(BASE_HEADER_SPACING);
            for (int i = 0; i < headerLayout->count(); ++i) {
                if (QSpacerItem* spacer = headerLayout->itemAt(i)->spacerItem()) {
                    spacer->changeSize(spacing, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
                }
            }
        }
    }

    // Title font
    if (m_titleLabel) {
        m_titleLabel->setFont(
            theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    }

    // List layout spacing
    if (m_listLayout) {
        m_listLayout->setSpacing(theme.scaled(BASE_LIST_SPACING));
    }

    // Grid layout spacing
    if (m_gridLayout) {
        m_gridLayout->setHorizontalSpacing(theme.scaled(BASE_GRID_H_SPACING));
        m_gridLayout->setVerticalSpacing(theme.scaled(BASE_GRID_V_SPACING));
    }
}

QVector<RecentProjectsWidget::ProjectData> RecentProjectsWidget::getFilteredData() const
{
    QVector<ProjectData> filtered;

    if (m_searchQuery.isEmpty()) {
        return m_projectsData;
    }

    for (const auto& data : m_projectsData) {
        if (data.name.toLower().contains(m_searchQuery)) {
            filtered.append(data);
        }
    }

    return filtered;
}

void RecentProjectsWidget::rebuildListView()
{
    if (!m_listLayout) {
        return;
    }

    // Clear layout items completely (including widgets)
    QLayoutItem* child;
    while ((child = m_listLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->setParent(nullptr);
            child->widget()->deleteLater();
        }
        delete child;
    }

    // Clear our tracking list
    m_projectItems.clear();

    // Create new items
    auto filteredData = getFilteredData();

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (filteredData.isEmpty()) {
        const char* ctx = metaObject()->className();
        if (m_projectsData.isEmpty()) {
            // No recent projects at all: show message + ASCII art centered
            // Remove AlignTop so empty state container can expand to fill space
            m_listLayout->setAlignment(Qt::Alignment());

            QWidget* emptyStateContainer = new QWidget(m_listContent);
            QVBoxLayout* emptyLayout = new QVBoxLayout(emptyStateContainer);
            emptyLayout->setContentsMargins(0, 0, 0, 0);
            emptyLayout->setSpacing(theme.scaled(12));
            emptyLayout->addStretch();

            QLabel* asciiLabel
                = new QLabel(QString::fromUtf8(kNoRecentProjectsAsciiArt), emptyStateContainer);
            asciiLabel->setFont(getAsciiArtFont(theme.scaledFontSize(BASE_ASCII_ART_FONT_SIZE)));
            asciiLabel->setStyleSheet(
                QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
            asciiLabel->setAlignment(Qt::AlignCenter);
            asciiLabel->setWordWrap(false);
            asciiLabel->setTextInteractionFlags(Qt::NoTextInteraction);
            emptyLayout->addWidget(asciiLabel, 0, Qt::AlignHCenter);

            QLabel* msgLabel = new QLabel(
                QCoreApplication::translate(ctx, kNoRecentProjectsMessage), emptyStateContainer);
            QFont msgFont = msgLabel->font();
            msgFont.setPointSize(theme.scaledFontSize(BASE_EMPTY_STATE_FONT_SIZE));
            msgLabel->setFont(msgFont);
            msgLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
            msgLabel->setAlignment(Qt::AlignCenter);
            msgLabel->setWordWrap(true);
            emptyLayout->addWidget(msgLabel, 0, Qt::AlignHCenter);

            emptyLayout->addStretch();
            emptyStateContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            m_listLayout->addWidget(emptyStateContainer, 1);
            emptyStateContainer->show();
            updateEmptyStateMinHeight();
            QTimer::singleShot(0, this, &RecentProjectsWidget::updateEmptyStateMinHeight);
        } else {
            // Search returned no results
            QLabel* noResultsLabel = new QLabel(tr("No projects found"), m_listContent);
            QFont font = noResultsLabel->font();
            font.setPointSize(theme.scaledFontSize(BASE_NO_RESULTS_FONT_SIZE));
            noResultsLabel->setFont(font);
            noResultsLabel->setStyleSheet(
                QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
            noResultsLabel->setAlignment(Qt::AlignCenter);
            m_listLayout->addWidget(noResultsLabel);
            noResultsLabel->show();
            m_listContent->setMinimumHeight(0);
        }
    } else {
        m_listContent->setMinimumHeight(0);
        m_listLayout->setAlignment(Qt::AlignTop);
        for (const auto& data : filteredData) {
            RecentProjectItem* item
                = new RecentProjectItem(data.name, data.filePath, data.lastModified, data.fileSize,
                    data.previewEnabled, data.icon, data.thumbnail, m_listContent);

            connect(item, &RecentProjectItem::clicked, this,
                [this, fp = data.filePath]() { emit projectClicked(fp); });
            connect(item, &RecentProjectItem::editRequested, this,
                [this, fp = data.filePath]() { emit projectEditRequested(fp); });
            connect(item, &RecentProjectItem::forgetRequested, this,
                [this, fp = data.filePath]() { emit projectForgetRequested(fp); });
            connect(item, &RecentProjectItem::deleteRequested, this,
                [this, fp = data.filePath]() { emit projectDeleteRequested(fp); });

            m_projectItems.append(item);
            m_listLayout->addWidget(item);
            item->show();
        }
        m_listLayout->addStretch();
    }

    m_listLayout->activate();
    m_listContent->adjustSize();
    m_listContent->updateGeometry();
    m_listContent->update();
}

void RecentProjectsWidget::rebuildGridView()
{
    if (!m_gridLayout) {
        return;
    }

    // Clear layout items completely (including widgets)
    QLayoutItem* child;
    while ((child = m_gridLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->setParent(nullptr);
            child->widget()->deleteLater();
        }
        delete child;
    }

    // Clear our tracking list
    m_projectCards.clear();

    // Create new cards
    auto filteredData = getFilteredData();

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (filteredData.isEmpty()) {
        const char* ctx = metaObject()->className();
        if (m_projectsData.isEmpty()) {
            // No recent projects at all: show message + ASCII art centered
            // Temporarily remove alignment so empty state can center properly
            m_gridLayout->setAlignment(Qt::Alignment());

            QWidget* emptyStateContainer = new QWidget(m_gridContent);
            QVBoxLayout* emptyLayout = new QVBoxLayout(emptyStateContainer);
            emptyLayout->setContentsMargins(0, 0, 0, 0);
            emptyLayout->setSpacing(theme.scaled(12));
            emptyLayout->addStretch();

            QLabel* asciiLabel
                = new QLabel(QString::fromUtf8(kNoRecentProjectsAsciiArt), emptyStateContainer);
            asciiLabel->setFont(getAsciiArtFont(theme.scaledFontSize(BASE_ASCII_ART_FONT_SIZE)));
            asciiLabel->setStyleSheet(
                QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
            asciiLabel->setAlignment(Qt::AlignCenter);
            asciiLabel->setWordWrap(false);
            asciiLabel->setTextInteractionFlags(Qt::NoTextInteraction);
            emptyLayout->addWidget(asciiLabel, 0, Qt::AlignHCenter);

            QLabel* msgLabel = new QLabel(
                QCoreApplication::translate(ctx, kNoRecentProjectsMessage), emptyStateContainer);
            QFont msgFont = msgLabel->font();
            msgFont.setPointSize(theme.scaledFontSize(BASE_EMPTY_STATE_FONT_SIZE));
            msgLabel->setFont(msgFont);
            msgLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
            msgLabel->setAlignment(Qt::AlignCenter);
            msgLabel->setWordWrap(true);
            emptyLayout->addWidget(msgLabel, 0, Qt::AlignHCenter);

            emptyLayout->addStretch();
            emptyStateContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            m_gridLayout->addWidget(emptyStateContainer, 0, 0, 1, GRID_COLUMNS);
            m_gridLayout->setRowStretch(0, 1);
            emptyStateContainer->show();
            updateEmptyStateMinHeight();
            QTimer::singleShot(0, this, &RecentProjectsWidget::updateEmptyStateMinHeight);
        } else {
            // Search returned no results
            QLabel* noResultsLabel = new QLabel(tr("No projects found"), m_gridContent);
            QFont font = noResultsLabel->font();
            font.setPointSize(theme.scaledFontSize(BASE_NO_RESULTS_FONT_SIZE));
            noResultsLabel->setFont(font);
            noResultsLabel->setStyleSheet(
                QString("QLabel { color: %1; }").arg(colors.textMuted.name()));
            noResultsLabel->setAlignment(Qt::AlignCenter);
            m_gridLayout->addWidget(noResultsLabel, 0, 0, 1, GRID_COLUMNS);
            m_gridLayout->setRowStretch(0, 0);
            noResultsLabel->show();
            m_gridContent->setMinimumHeight(0);
        }
    } else {
        m_gridContent->setMinimumHeight(0);
        m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        for (int r = 0; r < 32; ++r) {
            m_gridLayout->setRowStretch(r, 0);
        }
        int row = 0;
        int col = 0;

        for (const auto& data : filteredData) {
            RecentProjectCard* card = new RecentProjectCard(data.name, data.filePath,
                data.lastModified, data.previewEnabled, data.thumbnail, data.icon, m_gridContent);

            connect(card, &RecentProjectCard::clicked, this,
                [this, fp = data.filePath]() { emit projectClicked(fp); });
            connect(card, &RecentProjectCard::editRequested, this,
                [this, fp = data.filePath]() { emit projectEditRequested(fp); });
            connect(card, &RecentProjectCard::forgetRequested, this,
                [this, fp = data.filePath]() { emit projectForgetRequested(fp); });
            connect(card, &RecentProjectCard::deleteRequested, this,
                [this, fp = data.filePath]() { emit projectDeleteRequested(fp); });

            m_projectCards.append(card);
            m_gridLayout->addWidget(card, row, col);
            card->show();

            col++;
            if (col >= GRID_COLUMNS) {
                col = 0;
                row++;
            }
        }
    }

    m_gridLayout->activate();
    m_gridContent->adjustSize();
    m_gridContent->updateGeometry();
    m_gridContent->update();
}

void RecentProjectsWidget::addProject(const QString& name, const QString& filePath,
    const QString& lastModified, const QPixmap& thumbnail)
{
    ProjectData data;
    data.name = name;
    data.filePath = filePath;
    data.lastModified = lastModified;
    data.fileSize
        = QFileInfo::exists(filePath) ? formatFileSize(QFileInfo(filePath).size()) : QString();
    data.thumbnail = thumbnail.isNull()
        ? ruwa::core::serialization::ThumbnailCache::instance().load(filePath)
        : thumbnail;
    data.icon = loadRecentProjectIcon(
        filePath, QString(), ruwa::ui::core::ThemeManager::instance().scaled(32));
    data.previewEnabled = true;
    m_projectsData.append(data);

    // Rebuild both views
    rebuildListView();
    rebuildGridView();
}

void RecentProjectsWidget::clearProjects()
{
    m_projectsData.clear();

    for (auto* item : m_projectItems) {
        if (item)
            item->deleteLater();
    }
    m_projectItems.clear();

    for (auto* card : m_projectCards) {
        if (card)
            card->deleteLater();
    }
    m_projectCards.clear();
}

void RecentProjectsWidget::reloadFromManager()
{
    using namespace ruwa::core::serialization;

    // Clear existing data
    m_projectsData.clear();

    const auto& entries = RecentProjectsManager::instance().entries();
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int iconSize = theme.scaled(32);
    for (const auto& entry : entries) {
        ProjectData data;
        // Fallback to filename when stored name is empty or generic (e.g. from before we fixed
        // addEntry)
        if (entry.projectName.isEmpty()
            || entry.projectName == QStringLiteral("Untitled Project")) {
            data.name = QFileInfo(entry.filePath).baseName();
            if (data.name.isEmpty()) {
                data.name = QFileInfo(entry.filePath).fileName();
            }
        } else {
            data.name = entry.projectName;
        }
        data.filePath = entry.filePath;
        data.lastModified = formatRelativeDate(entry.lastOpened, metaObject()->className());
        data.fileSize = QFileInfo::exists(entry.filePath)
            ? formatFileSize(QFileInfo(entry.filePath).size())
            : QString();
        data.thumbnail = entry.previewEnabled
            ? ruwa::core::serialization::ThumbnailCache::instance().load(entry.filePath)
            : QPixmap();
        data.icon = loadRecentProjectIcon(entry.filePath, entry.tabIconAlias, iconSize);
        data.previewEnabled = entry.previewEnabled;
        m_projectsData.append(data);
    }

    // Rebuild both views
    rebuildListView();
    rebuildGridView();
}

void RecentProjectsWidget::filterProjects(const QString& query)
{
    m_searchQuery = query.toLower();

    // Rebuild both views with filtered data
    rebuildListView();
    rebuildGridView();

    // Force complete update
    updateGeometry();
    update();

    // Force scroll area to recalculate
    if (m_listScrollArea && m_listScrollArea->widget()) {
        QTimer::singleShot(0, this, [this]() {
            if (m_listScrollArea) {
                m_listScrollArea->widget()->updateGeometry();
            }
            if (m_gridScrollArea) {
                m_gridScrollArea->widget()->updateGeometry();
            }
        });
    }
}

void RecentProjectsWidget::setViewMode(ViewMode mode)
{
    if (m_viewMode == mode) {
        return;
    }

    m_viewMode = mode;
    const int targetIndex = (mode == ViewMode::List) ? 0 : 1;

    // Selector already updated index + started indicator animation on user click.
    // Calling setCurrentIndex again with the same index stops the animation and snaps (see
    // SegmentedOptionSelector).
    if (m_viewSelector && m_viewSelector->currentIndex() != targetIndex) {
        m_viewSelector->setCurrentIndex(targetIndex, true);
    }

    m_viewSwitcher->switchTo(targetIndex);
}

void RecentProjectsWidget::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    }
}

void RecentProjectsWidget::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
    // Refresh selector icons (theme-aware)
    if (m_viewSelector) {
        auto& icons = ruwa::ui::core::IconProvider::instance();
        QIcon listIcon = icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::List);
        QIcon cardViewIcon = icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::CardView);
        m_viewSelector->setOptionIcon(0, listIcon);
        m_viewSelector->setOptionIcon(1, cardViewIcon);
    }
}

void RecentProjectsWidget::onViewModeChanged(ViewMode mode)
{
    setViewMode(mode);

    // Persist view mode (0 = List, 1 = Grid)
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue(kRecentProjectsViewModeKey, (mode == ViewMode::Grid) ? 1 : 0);
}

} // namespace ruwa::ui::widgets
