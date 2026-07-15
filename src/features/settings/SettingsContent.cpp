// SPDX-License-Identifier: MPL-2.0

// SettingsContent.cpp
#include "features/settings/SettingsContent.h"
#include "features/settings/SettingsCategory.h"
#include "features/settings/SettingsToggle.h"
#include "features/settings/SettingsChoice.h"
#include "features/settings/SettingsComboBox.h"
#include "features/theme/editor/ThemeSelectorWidget.h"
#include "features/home/welcome/WelcomeBannerSelectorWidget.h"
#include "features/settings/ShortcutsNavigatorWidget.h"
#include "features/settings/UpdatesSettingsWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/inputs/SearchBar.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "features/settings/BaseSettingsWidget.h"
#include "features/settings/SettingsManager.h"
#include "features/brush/manager/BrushManager.h"
#include "services/updates/UpdateManager.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeColors.h"
#include "app/Application.h"
#include "shared/style/PaintingUtils.h"
#include "shell/main-window/MainWindow.h"

#include <QCoreApplication>
#include <QEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

namespace ruwa::ui::widgets {

namespace {

const int BASE_RESET_BTN_HEIGHT = 40;
const int BASE_RESET_BTN_WIDTH = 130;
const int BASE_RESET_ICON_SIZE = 16;
const int BASE_RESET_SPACING = 8;
const int BASE_RESET_FONT_SIZE = 9;

/**
 * @brief Reset settings button (BaseAnimatedButton with UndoArrow icon)
 */
class ResetSettingsButton : public BaseAnimatedButton {
public:
    explicit ResetSettingsButton(const QString& text, const QIcon& icon, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_icon(icon)
    {
        setText(text);
        setCheckable(false);
        setCursor(Qt::PointingHandCursor);

        updateScaledSizes();

        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this,
            &ResetSettingsButton::onThemeChanged);
    }

    void updateScaledSizes()
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        setFixedHeight(theme.scaled(BASE_RESET_BTN_HEIGHT));
        setFixedWidth(theme.scaled(BASE_RESET_BTN_WIDTH));

        QFont font = this->font();
        font.setPointSize(theme.scaledFontSize(BASE_RESET_FONT_SIZE));
        setFont(font);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();

        // Visuals mirror HexColorInput (color panel): capsule pill, surfaceAlt
        // resting fill, soft surfaceElevated hover plate, gradient pill border.
        const QRectF r(this->rect());
        const qreal pillR = qMax(0.0, r.height() * 0.5 - 0.5);
        const QRectF fillRect = r.adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal fillR = qMax(0.0, pillR - 1.0);

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceAlt);
        painter.drawRoundedRect(fillRect, fillR, fillR);

        if (hoverProgress() > 0.001) {
            QColor plate = colors.surfaceElevated();
            plate.setAlpha(qBound(0, qRound(hoverProgress() * 90), 255));
            painter.setBrush(plate);
            painter.drawRoundedRect(fillRect, fillR, fillR);
        }

        {
            QColor borderTop = ruwa::ui::core::ThemeColors::interpolate(
                colors.borderSubtle(), colors.borderSubtleHover(), hoverProgress());
            ruwa::ui::painting::drawGradientBorder(painter, r.adjusted(0.5, 0.5, -0.5, -0.5), pillR,
                borderTop,
                ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
        }

        if (isPressed()) {
            painter.setPen(Qt::NoPen);
            QColor pressOverlay = colors.shadow(20);
            painter.setBrush(pressOverlay);
            painter.drawRoundedRect(fillRect, fillR, fillR);
        }

        QColor textColor = ruwa::ui::core::ThemeColors::interpolate(
            colors.textMuted, colors.text, hoverProgress());

        const int iconSize = theme.scaled(BASE_RESET_ICON_SIZE);
        const int spacing = theme.scaled(BASE_RESET_SPACING);

        const bool hasIcon = !m_icon.isNull();
        const int textWidth = fontMetrics().horizontalAdvance(text());
        const int contentWidth = textWidth + (hasIcon ? iconSize + spacing : 0);
        int currentX = qRound((width() - contentWidth) / 2.0);

        if (hasIcon) {
            QRect iconRect(currentX, (height() - iconSize) / 2, iconSize, iconSize);
            const QPixmap iconPixmap = m_icon.pixmap(iconSize, iconSize, QIcon::Active);
            painter.drawPixmap(iconRect, ruwa::ui::painting::tintedPixmap(iconPixmap, textColor));
            currentX += iconSize + spacing;
        }

        painter.setPen(textColor);
        QRect textRect(currentX, 0, width() - currentX, height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text());
    }

private slots:
    void onThemeChanged()
    {
        updateScaledSizes();
        update();
    }

private:
    QIcon m_icon;
};

const int BASE_MAIN_MARGIN_H = 40;
const int BASE_MAIN_MARGIN_V = 30;
const int BASE_MAIN_SPACING = 16;
const int BASE_HEADER_SPACING = 16;
const int BASE_TITLE_FONT_SIZE = 26;
const int BASE_SEARCH_BAR_WIDTH = 250;
const int BASE_SCROLL_SPACING = 24;
const int BASE_SCROLL_OFFSET = 20;
const int UPDATE_RECHECK_MIN_VISIBLE_MS = 3000;
} // anonymous namespace

SettingsContent::SettingsContent(QWidget* parent)
    : HomePageContent(parent)
{
    setupContent();

    // Load settings
    loadSettings();

    // Apply initial scaled sizes
    updateScaledSizes();

    // Connect to theme changes
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &SettingsContent::onThemeChanged);
}

void SettingsContent::setupContent()
{
    m_mainLayout = new QVBoxLayout(this);
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    // Header
    m_headerLayout = new QHBoxLayout();
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_titleLabel = new QLabel(tr("Settings"), this);
    QFont titleFont = colors.fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE));
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    m_headerLayout->addWidget(m_titleLabel);
    m_headerLayout->addStretch();

    // Search
    m_searchBar = new SearchBar(this);
    m_searchBar->setClickOutsideClearsFocus(true);
    m_searchBar->setPlaceholder(tr("Search settings..."));
    connect(m_searchBar, &SearchBar::textChanged, this, &SettingsContent::onSearchTextChanged);
    m_headerLayout->addWidget(m_searchBar);
    m_mainLayout->addLayout(m_headerLayout);

    // Scroll Area
    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setScrollBarMargin(4);

    m_scrollContent = new QWidget();
    m_scrollContent->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollContent->setAutoFillBackground(false);

    m_scrollLayout = new QVBoxLayout(m_scrollContent);
    m_scrollLayout->setContentsMargins(0, 0, 0, 0);
    m_scrollLayout->setAlignment(Qt::AlignTop);

    createUpdatesCategory();
    createAppearanceCategory();
    createEditorCategory();
    createShortcutsCategory();
    createPerformanceCategory();

    for (SettingsCategory* category : m_categories) {
        m_scrollLayout->addWidget(category);
    }

    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    m_resetButton = new ResetSettingsButton(tr("Reset all"),
        icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::UndoArrow), this);
    connect(m_resetButton, &BaseAnimatedButton::clicked, this,
        &SettingsContent::onResetSettingsClicked);
    m_scrollLayout->addWidget(m_resetButton, 0, Qt::AlignHCenter);
    // SmoothScrollArea keeps content at least viewport-high, so a trailing stretch
    // must absorb spare height locally instead of redistributing it across categories.
    m_scrollLayout->addStretch();

    m_scrollArea->setWidget(m_scrollContent);
    m_mainLayout->addWidget(m_scrollArea, 1);
}

void SettingsContent::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    if (m_mainLayout) {
        const int marginH = theme.scaled(BASE_MAIN_MARGIN_H);
        const int marginV = theme.scaled(BASE_MAIN_MARGIN_V);
        m_mainLayout->setContentsMargins(marginH, marginV, marginH, marginV);
        m_mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));
    }

    if (m_headerLayout) {
        m_headerLayout->setSpacing(theme.scaled(BASE_HEADER_SPACING));
    }

    if (m_titleLabel) {
        QFont titleFont
            = theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE));
        m_titleLabel->setFont(titleFont);
    }

    if (m_searchBar) {
        m_searchBar->setFixedWidth(theme.scaled(BASE_SEARCH_BAR_WIDTH));
    }

    if (m_scrollLayout) {
        m_scrollLayout->setSpacing(theme.scaled(BASE_SCROLL_SPACING));
    }
}

void SettingsContent::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // Update title color
    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(QString("QLabel { color: %1; }").arg(colors.text.name()));
    }
}

void SettingsContent::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

void SettingsContent::onSearchTextChanged(const QString& text)
{
    if (text.isEmpty()) {
        // No search - do nothing
        return;
    }

    QString searchLower = text.toLower();

    // Search through all categories and settings
    for (SettingsCategory* category : m_categories) {
        // Check category title
        if (category->title().toLower().contains(searchLower)) {
            scrollToSetting(category);
            return;
        }

        // Check settings widgets
        for (QWidget* settingWidget : category->settingsWidgets()) {
            BaseSettingsWidget* baseSetting = qobject_cast<BaseSettingsWidget*>(settingWidget);
            if (baseSetting) {
                // Check label
                if (baseSetting->label().toLower().contains(searchLower)) {
                    scrollToSetting(settingWidget);
                    return;
                }
                // Check description
                if (baseSetting->description().toLower().contains(searchLower)) {
                    scrollToSetting(settingWidget);
                    return;
                }
            }
        }
    }
}

void SettingsContent::changeEvent(QEvent* event)
{
    HomePageContent::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void SettingsContent::retranslateUi()
{
    const char* ctx = metaObject()->className();
    if (m_titleLabel)
        m_titleLabel->setText(QCoreApplication::translate(ctx, "Settings"));
    if (m_searchBar)
        m_searchBar->setPlaceholder(QCoreApplication::translate(ctx, "Search settings..."));

    // Update categories (order: Updates, Appearance, Editor, Shortcuts, Performance)
    if (m_categories.size() >= 5) {
        m_categories[0]->setTitle(QCoreApplication::translate(ctx, "Updates"));
        m_categories[1]->setTitle(QCoreApplication::translate(ctx, "Appearance"));
        m_categories[2]->setTitle(QCoreApplication::translate(ctx, "Editor"));
        m_categories[3]->setTitle(QCoreApplication::translate(ctx, "Keyboard Shortcuts"));
        m_categories[4]->setTitle(QCoreApplication::translate(ctx, "Performance"));
    }

    if (m_updatesSettingsWidget) {
        m_updatesSettingsWidget->retranslateUi();
    }
    if (m_themeSelector) {
        m_themeSelector->retranslateUi();
    }
    if (m_shortcutsNavigator) {
        m_shortcutsNavigator->retranslateUi();
    }

    // Appearance
    if (m_uiScaleChoice) {
        m_uiScaleChoice->setLabel(QCoreApplication::translate(ctx, "UI Scale"));
        m_uiScaleChoice->setDescription(
            QCoreApplication::translate(ctx, "Adjust the size of UI elements"));
        m_uiScaleChoice->setOptions(
            { QCoreApplication::translate(ctx, "Small"), QCoreApplication::translate(ctx, "Medium"),
                QCoreApplication::translate(ctx, "Large") });
    }
    if (m_languageChoice) {
        m_languageChoice->setLabel(QCoreApplication::translate(ctx, "Language"));
        m_languageChoice->setDescription(QCoreApplication::translate(ctx, "Interface language"));
    }
    if (m_welcomeBannerSelector) {
        m_welcomeBannerSelector->retranslateUi();
    }
    if (m_topBarTabAlignmentChoice) {
        m_topBarTabAlignmentChoice->setLabel(
            QCoreApplication::translate(ctx, "Top bar tab alignment"));
        m_topBarTabAlignmentChoice->setDescription(QCoreApplication::translate(
            ctx, "Place the tab strip at the left of the title bar or centered in the free space"));
        m_topBarTabAlignmentChoice->setOptions({ QCoreApplication::translate(ctx, "Left"),
            QCoreApplication::translate(ctx, "Center") });
    }

    // Editor
    if (m_autoSaveChoice) {
        m_autoSaveChoice->setLabel(QCoreApplication::translate(ctx, "Auto-Save"));
        m_autoSaveChoice->setDescription(QCoreApplication::translate(
            ctx, "Automatically save your work at the selected interval"));
        m_autoSaveChoice->setOptions({ QCoreApplication::translate(ctx, "Off"),
            QCoreApplication::translate(ctx, "2 min"), QCoreApplication::translate(ctx, "5 min"),
            QCoreApplication::translate(ctx, "10 min") });
    }
    if (m_quickshapesToggle) {
        m_quickshapesToggle->setLabel(QCoreApplication::translate(ctx, "Quick Shapes"));
        m_quickshapesToggle->setDescription(QCoreApplication::translate(
            ctx, "Hold stroke to morph into straight line, circle, triangle, or square"));
    }

    // Performance
    if (m_undoMemoryChoice) {
        m_undoMemoryChoice->setLabel(QCoreApplication::translate(ctx, "Undo Memory Limit"));
        m_undoMemoryChoice->setDescription(
            QCoreApplication::translate(ctx, "Maximum memory available for undo history"));
        m_undoMemoryChoice->setOptions({ QCoreApplication::translate(ctx, "300 MB"),
            QCoreApplication::translate(ctx, "1 GB"), QCoreApplication::translate(ctx, "3 GB"),
            QCoreApplication::translate(ctx, "8 GB") });
    }
    if (m_tabletBackendChoice) {
        m_tabletBackendChoice->setLabel(QCoreApplication::translate(ctx, "Tablet Input Backend"));
        m_tabletBackendChoice->setDescription(QCoreApplication::translate(
            ctx, "Choose the stylus input backend. Restart is required to apply this setting."));
        m_tabletBackendChoice->setOptions({ QCoreApplication::translate(ctx, "WinTab (Qt)"),
            QCoreApplication::translate(ctx, "Windows Ink"),
            QCoreApplication::translate(ctx, "WinTab (Ruwa)") });
    }

    if (m_resetButton)
        m_resetButton->setText(QCoreApplication::translate(ctx, "Reset all"));
}

void SettingsContent::onResetSettingsClicked()
{
    MessagePopupManager::show(this,
        tr("Are you sure you want to reset all settings to default? The program will restart. This "
           "cannot be undone."),
        { { tr("Cancel"), false, []() {} },
            { tr("Reset all"), true,
                [this]() {
                    // Defer restart so the current popup can fully finish its hide animation.
                    // This avoids reusing the same MessagePopup instance reentrantly.
                    QTimer::singleShot(220, [this]() {
                        if (!ruwa::Application::restartWithFactoryReset()) { }
                    });
                } } },
        360, m_resetButton);
}

void SettingsContent::scrollToSetting(QWidget* settingWidget)
{
    if (!settingWidget || !m_scrollArea) {
        return;
    }

    // Calculate position of widget relative to scroll area content
    QWidget* scrollContent = m_scrollArea->widget();
    if (!scrollContent) {
        return;
    }

    // Get widget position in scroll content coordinates
    QPoint widgetPos = settingWidget->mapTo(scrollContent, QPoint(0, 0));

    // Scroll to position with some offset from top
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int scrollOffset = theme.scaled(BASE_SCROLL_OFFSET);
    int scrollTo = widgetPos.y() - scrollOffset;
    m_scrollArea->scrollTo(scrollTo, true);
}

void SettingsContent::applyUpdateCheckResult(bool hasUpdate, const QString& versionInfo)
{
    if (!m_updatesSettingsWidget) {
        return;
    }

    auto* updateMgr = ruwa::services::UpdateManager::instance();

    m_updatesSettingsWidget->setUpdateVersion(versionInfo);
    m_updatesSettingsWidget->setReleaseDescription(updateMgr->latestReleaseDescription());

    const QDateTime lastCheckTime = updateMgr->lastUpdateCheckTime();
    if (lastCheckTime.isValid()) {
        const qint64 minutesSinceCheck
            = qMax<qint64>(0, lastCheckTime.secsTo(QDateTime::currentDateTimeUtc()) / 60);
        m_updatesSettingsWidget->setLastCheckedMinutesAgo(static_cast<int>(minutesSinceCheck));
    }

    if (!hasUpdate) {
        m_updatesSettingsWidget->setUpdateState(UpdateState::UpToDate);
        return;
    }

    m_updatesSettingsWidget->setUpdateState(updateMgr->hasPendingDownloadedUpdate()
            ? UpdateState::ReadyToRestart
            : UpdateState::UpdateAvailable);
}

void SettingsContent::finishPendingUpdateRecheck()
{
    if (!m_updateRecheckInProgress || !m_updateRecheckResultReady) {
        return;
    }

    m_updateRecheckInProgress = false;
    m_updateRecheckDelayElapsed = false;
    m_updateRecheckResultReady = false;

    if (m_updatesSettingsWidget) {
        m_updatesSettingsWidget->setRecheckInProgress(false);
    }

    applyUpdateCheckResult(m_pendingUpdateHasUpdate, m_pendingUpdateVersionInfo);
    m_pendingUpdateVersionInfo.clear();
}

void SettingsContent::createUpdatesCategory()
{
    SettingsCategory* category = new SettingsCategory(tr("Updates"), this);

    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    category->setIcon(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::ArrowDown));

    m_updatesSettingsWidget = new UpdatesSettingsWidget(this);
    m_updatesSettingsWidget->setReleaseDescription(QString());
    category->addSettingsWidget(m_updatesSettingsWidget);

    auto* updateMgr = ruwa::services::UpdateManager::instance();
    connect(updateMgr, &ruwa::services::UpdateManager::updateCheckFinished, m_updatesSettingsWidget,
        [this](bool hasUpdate, const QString& versionInfo) {
            if (!m_updatesSettingsWidget) {
                return;
            }

            if (m_updateRecheckInProgress) {
                m_pendingUpdateHasUpdate = hasUpdate;
                m_pendingUpdateVersionInfo = versionInfo;
                m_updateRecheckResultReady = true;
                if (m_updateRecheckDelayElapsed) {
                    finishPendingUpdateRecheck();
                }
                return;
            }

            applyUpdateCheckResult(hasUpdate, versionInfo);
        });
    connect(updateMgr, &ruwa::services::UpdateManager::downloadProgress, m_updatesSettingsWidget,
        &UpdatesSettingsWidget::setDownloadProgress);
    connect(updateMgr, &ruwa::services::UpdateManager::downloadFinished, m_updatesSettingsWidget,
        [this](bool success, const QString&) {
            if (!m_updatesSettingsWidget) {
                return;
            }
            m_updatesSettingsWidget->setUpdateState(
                success ? UpdateState::ReadyToRestart : UpdateState::UpdateAvailable);
        });
    connect(m_updatesSettingsWidget, &UpdatesSettingsWidget::updateActionClicked, this,
        [this, updateMgr]() {
            if (!m_updatesSettingsWidget) {
                return;
            }
            if (m_updatesSettingsWidget->updateState() == UpdateState::UpdateAvailable) {
                m_updatesSettingsWidget->setUpdateState(UpdateState::Downloading);
                updateMgr->downloadUpdate();
            } else if (m_updatesSettingsWidget->updateState() == UpdateState::ReadyToRestart) {
                MessagePopupManager::show(this, tr("Apply update and restart?"),
                    { { tr("Cancel"), false, []() {} },
                        { tr("Restart"), true,
                            []() {
                                QTimer::singleShot(220, []() {
                                    if (!ruwa::Application::restartWithUpdate()) { }
                                });
                            } } },
                    360, m_updatesSettingsWidget);
            }
        });
    connect(m_updatesSettingsWidget, &UpdatesSettingsWidget::releaseNotesClicked, this, [this]() {
        auto* mainWindow = qobject_cast<ruwa::ui::windows::MainWindow*>(window());
        if (!mainWindow) {
            return;
        }

        mainWindow->showReleaseNotesOverlay();
    });
    connect(m_updatesSettingsWidget, &UpdatesSettingsWidget::updateRecheckClicked, this,
        [this, updateMgr]() {
            if (!m_updatesSettingsWidget
                || m_updatesSettingsWidget->updateState() != UpdateState::UpToDate
                || m_updateRecheckInProgress) {
                return;
            }

            m_updateRecheckInProgress = true;
            m_updateRecheckDelayElapsed = false;
            m_updateRecheckResultReady = false;
            m_pendingUpdateHasUpdate = false;
            m_pendingUpdateVersionInfo.clear();
            m_updatesSettingsWidget->setRecheckInProgress(true);

            // Keep the loading state visible for at least 3 seconds to prevent re-check spam.
            QTimer::singleShot(UPDATE_RECHECK_MIN_VISIBLE_MS, this, [this]() {
                m_updateRecheckDelayElapsed = true;
                if (m_updateRecheckResultReady) {
                    finishPendingUpdateRecheck();
                }
            });

            updateMgr->recheckForUpdates();
        });
    updateMgr->checkForUpdates();

    m_categories.append(category);
}

void SettingsContent::createAppearanceCategory()
{
    SettingsCategory* category = new SettingsCategory(tr("Appearance"), this);

    // ДОБАВЛЕНО: Получаем иконку и устанавливаем её
    // Используем Transform, так как в IconProvider мы условились, что это Appearance.png
    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    category->setIcon(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Appearance));

    m_themeSelector = new ThemeSelectorWidget();
    connect(m_themeSelector, &ThemeSelectorWidget::themeSelected, this,
        [](const ruwa::ui::core::ThemePreset& preset) {
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setThemeId(preset.id);
            settings.save();
        });

    // Connect custom themes navigator
    connect(m_themeSelector, &ThemeSelectorWidget::customThemesRequested, this,
        &SettingsContent::customThemesRequested);

    category->addSettingsWidget(m_themeSelector);

    m_welcomeBannerSelector = new WelcomeBannerSelectorWidget();
    category->addSettingsWidget(m_welcomeBannerSelector);

    m_uiScaleChoice = new SettingsChoice(tr("UI Scale"), tr("Adjust the size of UI elements"),
        { tr("Small"), tr("Medium"), tr("Large") }, 1);
    connect(m_uiScaleChoice, &SettingsChoice::selectionChanged, this, [](int value) {
        auto& settings = ruwa::core::SettingsManager::instance();
        settings.setUiScale(value);
        settings.save();
    });
    category->addSettingsWidget(m_uiScaleChoice);

    // Language: 0=English, 1=Русский (native names, not translated)
    m_languageChoice = new SettingsComboBox(tr("Language"), tr("Interface language"),
        { QStringLiteral("English"), QStringLiteral("Русский") }, 0);
    connect(m_languageChoice, &SettingsComboBox::selectionChanged, this, [this](int index) {
        const QString codes[] = { "en", "ru" };
        if (index >= 0 && index < 2) {
            ruwa::ui::core::TranslationManager::instance().setLanguage(codes[index]);
            // Note: TranslationManager persists language via SettingsManager::save() internally
            // (sync for "en", in async callback for "ru" etc.)
        }
    });
    category->addSettingsWidget(m_languageChoice);

    m_topBarTabAlignmentChoice = new SettingsChoice(tr("Top bar tab alignment"),
        tr("Place the tab strip at the left of the title bar or centered in the free space"),
        { tr("Left"), tr("Center") }, 0);
    connect(m_topBarTabAlignmentChoice, &SettingsChoice::selectionChanged, this, [](int index) {
        auto& settings = ruwa::core::SettingsManager::instance();
        settings.setTopBarTabAlignment(index == 1 ? 1 : 0);
        settings.save();
    });
    category->addSettingsWidget(m_topBarTabAlignmentChoice);

    m_categories.append(category);
}

void SettingsContent::createEditorCategory()
{
    SettingsCategory* category = new SettingsCategory(tr("Editor"), this);

    // ДОБАВЛЕНО: Иконка редактирования
    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    category->setIcon(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Edit));

    // Auto-save: 0=Off, 1=5min, 2=10min, 3=20min
    m_autoSaveChoice = new SettingsChoice(
        tr("Auto-Save"), tr("Automatically save your work at the selected interval"),
        { tr("Off"), tr("2 min"), tr("5 min"), tr("10 min") },
        2 // default: 5 min
    );
    connect(m_autoSaveChoice, &SettingsChoice::selectionChanged, this, [](int index) {
        auto& settings = ruwa::core::SettingsManager::instance();
        const int minutes[] = { 0, 2, 5, 10 };
        settings.setAutoSaveInterval(minutes[index]);
        settings.save();
    });
    category->addSettingsWidget(m_autoSaveChoice);

    m_quickshapesToggle = new SettingsToggle(tr("Quick Shapes"),
        tr("Hold stroke to morph into straight line, circle, triangle, or square"), true);
    connect(m_quickshapesToggle, &SettingsToggle::toggled, this, [](bool checked) {
        auto& settings = ruwa::core::SettingsManager::instance();
        settings.setQuickshapesEnabled(checked);
        settings.save();
    });
    category->addSettingsWidget(m_quickshapesToggle);

    m_categories.append(category);
}

void SettingsContent::createShortcutsCategory()
{
    SettingsCategory* category = new SettingsCategory(tr("Keyboard Shortcuts"), this);

    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    category->setIcon(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Edit));

    m_shortcutsNavigator = new ShortcutsNavigatorWidget();
    connect(m_shortcutsNavigator, &ShortcutsNavigatorWidget::clicked, this,
        &SettingsContent::shortcutManagerRequested);
    category->addSettingsWidget(m_shortcutsNavigator);

    m_categories.append(category);
}

void SettingsContent::createPerformanceCategory()
{
    SettingsCategory* category = new SettingsCategory(tr("Performance"), this);

    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    category->setIcon(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Performance));

    m_undoMemoryChoice = new SettingsChoice(
        tr("Undo Memory Limit"), tr("Maximum memory available for undo history"),
        { tr("300 MB"), tr("1 GB"), tr("3 GB"), tr("8 GB") },
        2 // default: 3 GB
    );
    connect(m_undoMemoryChoice, &SettingsChoice::selectionChanged, this, [](int index) {
        auto& settings = ruwa::core::SettingsManager::instance();
        const int limitsMb[] = { 300, 1024, 3072, 8192 };
        settings.setUndoMemoryLimitMb(limitsMb[index]);
        settings.save();
    });
    category->addSettingsWidget(m_undoMemoryChoice);

    m_tabletBackendChoice = new SettingsChoice(tr("Tablet Input Backend"),
        tr("Choose the stylus input backend. Restart is required to apply this setting."),
        { tr("WinTab (Qt)"), tr("Windows Ink"), tr("WinTab (Ruwa)") }, 0);
    connect(m_tabletBackendChoice, &SettingsChoice::selectionChanged, this, [this](int index) {
        auto& settings = ruwa::core::SettingsManager::instance();
        const auto& current = settings.settings();
        // Compare with the currently active backend, not the saved one
        // This way if the user reverts to the original backend, we won't show a restart message
        const bool backendChanged = ruwa::Application::currentTabletBackend() != index;
        settings.setTabletBackend(index);
        settings.save();

        if (backendChanged) {
            MessagePopupManager::show(this,
                tr("Tablet backend was changed. Restart is required to apply this setting."),
                { { tr("Later"), false, []() {} },
                    { tr("Restart now"), true,
                        []() {
                            // Defer restart so the current popup can fully finish its hide
                            // animation. This avoids reusing the same MessagePopup instance
                            // reentrantly.
                            QTimer::singleShot(220, []() {
                                if (!ruwa::Application::restart()) { }
                            });
                        } } },
                420, m_tabletBackendChoice);
        }
    });
    category->addSettingsWidget(m_tabletBackendChoice);

    m_categories.append(category);
}

void SettingsContent::loadSettings()
{
    auto& settings = ruwa::core::SettingsManager::instance();
    const auto& current = settings.settings();

    // Block signals during loading to prevent save() being called
    // when we're just restoring saved values

    // Load appearance settings
    // Apply saved theme (in case main.cpp didn't do it)
    if (!current.appearance.themeId.isNull()) {
        auto& themeManager = ruwa::ui::core::ThemeManager::instance();
        QUuid currentTheme = themeManager.currentPresetId();
        if (currentTheme != current.appearance.themeId) {
            themeManager.applyPreset(current.appearance.themeId);
        }
    }

    if (m_themeSelector && !current.appearance.themeId.isNull()) {
        m_themeSelector->blockSignals(true);
        m_themeSelector->setSelectedTheme(current.appearance.themeId);
        m_themeSelector->blockSignals(false);
    }
    if (m_uiScaleChoice) {
        m_uiScaleChoice->blockSignals(true);
        m_uiScaleChoice->setSelectedIndex(current.appearance.uiScale);
        m_uiScaleChoice->blockSignals(false);
    }
    if (m_languageChoice) {
        int langIndex = (current.appearance.language == "ru") ? 1 : 0;
        m_languageChoice->blockSignals(true);
        m_languageChoice->setSelectedIndex(langIndex);
        m_languageChoice->blockSignals(false);
    }
    if (m_topBarTabAlignmentChoice) {
        m_topBarTabAlignmentChoice->blockSignals(true);
        m_topBarTabAlignmentChoice->setSelectedIndex(
            current.appearance.topBarTabAlignment == 1 ? 1 : 0);
        m_topBarTabAlignmentChoice->blockSignals(false);
    }

    if (m_welcomeBannerSelector) {
        m_welcomeBannerSelector->loadFromSettings();
    }

    // Load editor settings (autoSaveInterval: 0->0, 2->1, 5->2, 10->3)
    if (m_quickshapesToggle) {
        m_quickshapesToggle->blockSignals(true);
        m_quickshapesToggle->setChecked(current.editor.quickshapesEnabled);
        m_quickshapesToggle->blockSignals(false);
    }
    if (m_autoSaveChoice) {
        int index = 2;
        if (current.editor.autoSaveInterval == 0)
            index = 0;
        else if (current.editor.autoSaveInterval == 2)
            index = 1;
        else if (current.editor.autoSaveInterval == 5)
            index = 2;
        else if (current.editor.autoSaveInterval == 10)
            index = 3;
        m_autoSaveChoice->blockSignals(true);
        m_autoSaveChoice->setSelectedIndex(index);
        m_autoSaveChoice->blockSignals(false);
    }

    // Load performance settings (undoMemoryLimitMb: 300->0, 1024->1, 3072->2, 8192->3)
    if (m_undoMemoryChoice) {
        int index = 2;
        if (current.performance.undoMemoryLimitMb == 300)
            index = 0;
        else if (current.performance.undoMemoryLimitMb == 1024)
            index = 1;
        else if (current.performance.undoMemoryLimitMb == 3072)
            index = 2;
        else if (current.performance.undoMemoryLimitMb == 8192)
            index = 3;
        m_undoMemoryChoice->blockSignals(true);
        m_undoMemoryChoice->setSelectedIndex(index);
        m_undoMemoryChoice->blockSignals(false);
    }

    if (m_tabletBackendChoice) {
        int index = current.performance.tabletBackend;
        if (index < 0 || index > 2) {
            index = 0;
        }
        m_tabletBackendChoice->blockSignals(true);
        m_tabletBackendChoice->setSelectedIndex(index);
        m_tabletBackendChoice->blockSignals(false);
    }
}

} // namespace ruwa::ui::widgets
