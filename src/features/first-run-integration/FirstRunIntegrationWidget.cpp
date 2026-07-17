// SPDX-License-Identifier: MPL-2.0

#include "features/first-run-integration/FirstRunIntegrationWidget.h"

#include "app/Application.h"
#include "features/home/welcome/WelcomeBannerButton.h"
#include "features/settings/SettingsManager.h"
#include "features/settings/SettingsChoice.h"
#include "features/settings/SettingsToggle.h"
#include "features/theme/editor/ThemeSelectorWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/overlays/WidgetFadeInOverlay.h"
#include "shell/top-bar/MessagePopupManager.h"

#include <QColor>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSize>
#include <QSizePolicy>
#include <QString>
#include <QTimer>
#include <QVBoxLayout>

namespace ruwa::ui::first_run_integration {

namespace {

constexpr int kBaseContentSideMargin = 64;
constexpr int kContentTopMargin = 48;
constexpr int kContentBottomMargin = 64;
constexpr int kContentSpacing = 24;
constexpr int kSettingsSectionMinimumHeight = 180;
constexpr int kFinishMinimumHeight = 320;
constexpr int kAppearanceDelayMs = 800;
constexpr int kAppearanceDurationMs = 933;
constexpr int kImageSectionMargin = 15;
constexpr qreal kImageSectionCornerRadius = 20.0;
constexpr int kHeroLogoSize = 72;
constexpr int kHeroGlassBlurRadius = 10;
constexpr qreal kHeroGlassCornerRadius = 24.0;
constexpr int kCustomizationScrollDurationMs = 900;

int autoSaveIndexForMinutes(int minutes)
{
    switch (minutes) {
    case 0:
        return 0;
    case 2:
        return 1;
    case 10:
        return 3;
    case 5:
    default:
        return 2;
    }
}

int undoMemoryIndexForMegabytes(int megabytes)
{
    switch (megabytes) {
    case 300:
        return 0;
    case 1024:
        return 1;
    case 8192:
        return 3;
    case 3072:
    default:
        return 2;
    }
}

class FirstRunIntegrationImageSection final : public QFrame {
public:
    explicit FirstRunIntegrationImageSection(
        const QString& objectName, const QString& imageResource, QWidget* parent = nullptr)
        : QFrame(parent)
        , m_backgroundImage(imageResource)
    {
        setObjectName(objectName);
        setFrameShape(QFrame::NoFrame);
        setMinimumHeight(1);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

    QPixmap createBlurredBackdrop(const QRect& area, int blurRadius) const
    {
        if (area.isEmpty()) {
            return {};
        }

        const qreal dpr = devicePixelRatioF();
        const QSize deviceSize(
            qMax(1, qRound(area.width() * dpr)), qMax(1, qRound(area.height() * dpr)));
        QPixmap snapshot(deviceSize);
        snapshot.setDevicePixelRatio(dpr);
        snapshot.fill(Qt::transparent);

        QPainter painter(&snapshot);
        painter.translate(-area.topLeft());
        drawBackground(painter);
        painter.end();

        return ruwa::ui::painting::blurSnapshotPixmap(snapshot, blurRadius);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        drawBackground(painter);
    }

private:
    void drawBackground(QPainter& painter) const
    {
        painter.fillRect(rect(), ruwa::ui::core::ThemeManager::instance().colors().background);

        if (m_backgroundImage.isNull() || width() <= 0 || height() <= 0) {
            return;
        }

        const QRectF imageRect
            = QRectF(rect()).adjusted(kImageSectionMargin, kImageSectionMargin,
                -kImageSectionMargin, -kImageSectionMargin);
        if (imageRect.isEmpty()) {
            return;
        }

        const qreal targetAspect = imageRect.width() / imageRect.height();
        const qreal imageAspect
            = static_cast<qreal>(m_backgroundImage.width()) / m_backgroundImage.height();
        QRectF sourceRect(m_backgroundImage.rect());

        if (imageAspect > targetAspect) {
            const qreal sourceWidth = m_backgroundImage.height() * targetAspect;
            sourceRect.setLeft((m_backgroundImage.width() - sourceWidth) / 2.0);
            sourceRect.setWidth(sourceWidth);
        } else if (imageAspect < targetAspect) {
            const qreal sourceHeight = m_backgroundImage.width() / targetAspect;
            sourceRect.setTop((m_backgroundImage.height() - sourceHeight) / 2.0);
            sourceRect.setHeight(sourceHeight);
        }

        QPainterPath clipPath;
        clipPath.addRoundedRect(
            imageRect, kImageSectionCornerRadius, kImageSectionCornerRadius);
        painter.setClipPath(clipPath);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(imageRect, m_backgroundImage, sourceRect);
    }

    QPixmap m_backgroundImage;
};

class FirstRunHeroGlassPanel final : public QWidget {
public:
    explicit FirstRunHeroGlassPanel(
        FirstRunIntegrationImageSection* hero, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_hero(hero)
    {
        setObjectName(QStringLiteral("FirstRunHeroGlassPanel"));
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setMaximumWidth(720);

        if (m_hero) {
            m_hero->installEventFilter(this);
        }
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize
            || event->type() == QEvent::Show
            || event->type() == QEvent::DevicePixelRatioChange) {
            m_blurredBackdrop = {};
        }
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        ensureBackdrop();

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ruwa::ui::painting::drawTonedGlassPanel(painter, QRectF(rect()),
            theme.scaled(kHeroGlassCornerRadius), QSizeF(size()), m_blurredBackdrop,
            colors.surface, colors.primary, colors.isDark, colors.borderSubtleHover(),
            ruwa::ui::core::ThemeColors::withAlpha(
                colors.borderSubtle(), colors.borderSubtle().alpha() / 2),
            0.0);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_hero
            && (event->type() == QEvent::Resize || event->type() == QEvent::StyleChange
                || event->type() == QEvent::PaletteChange)) {
            m_blurredBackdrop = {};
            update();
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void ensureBackdrop()
    {
        if (!m_hero) {
            m_blurredBackdrop = {};
            return;
        }

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const QRect sampleGeometry = geometry();
        const QRgb backgroundRgba = theme.colors().background.rgba();
        const qreal dpr = devicePixelRatioF();
        if (!m_blurredBackdrop.isNull() && m_cachedHeroSize == m_hero->size()
            && m_cachedGeometry == sampleGeometry && m_cachedBackgroundRgba == backgroundRgba
            && qFuzzyCompare(m_cachedDpr, dpr)) {
            return;
        }

        m_cachedHeroSize = m_hero->size();
        m_cachedGeometry = sampleGeometry;
        m_cachedBackgroundRgba = backgroundRgba;
        m_cachedDpr = dpr;
        m_blurredBackdrop = m_hero->createBlurredBackdrop(
            sampleGeometry, theme.scaled(kHeroGlassBlurRadius));
    }

private:
    FirstRunIntegrationImageSection* m_hero { nullptr };
    QPixmap m_blurredBackdrop;
    QSize m_cachedHeroSize;
    QRect m_cachedGeometry;
    QRgb m_cachedBackgroundRgba { 0 };
    qreal m_cachedDpr { 0.0 };
};

QFrame* createSection(QWidget* parent, const QString& objectName, int minimumHeight)
{
    auto* section = new QFrame(parent);
    section->setObjectName(objectName);
    section->setFrameShape(QFrame::NoFrame);
    section->setMinimumHeight(minimumHeight);
    section->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return section;
}

QLabel* createSectionTitle(QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(QStringLiteral("FirstRunSectionTitle"));
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

} // namespace

FirstRunIntegrationWidget::FirstRunIntegrationWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    retranslateUi();
    updateTheme();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &FirstRunIntegrationWidget::updateTheme);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this,
        &FirstRunIntegrationWidget::retranslateUi);
}

void FirstRunIntegrationWidget::setContentSideMargin(int margin)
{
    const int normalizedMargin = qMax(0, margin);
    if (m_contentSideMargin == normalizedMargin) {
        return;
    }

    m_contentSideMargin = normalizedMargin;
    updateContentMargins();
}

void FirstRunIntegrationWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

bool FirstRunIntegrationWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (m_scrollArea && watched == m_scrollArea->viewport() && event->type() == QEvent::Resize) {
        updateHeroHeight();
    }
    return QWidget::eventFilter(watched, event);
}

void FirstRunIntegrationWidget::hideEvent(QHideEvent* event)
{
    m_scrollArea->setUserScrollingEnabled(true);

    if (m_appearanceOverlay) {
        m_appearanceOverlay->close();
        m_appearanceOverlay.clear();
    }
    QWidget::hideEvent(event);
}

void FirstRunIntegrationWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateHeroHeight();
}

void FirstRunIntegrationWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Startup briefly shows the main window at zero opacity to create its native
    // handle, then hides it again. Prepare and paint the opaque overlay
    // synchronously while the window is still invisible, so no uncovered
    // content frame can appear when the splash is removed.
    if (!m_appearanceAnimationStarted) {
        ensureAppearanceOverlay();
    }

    // Starting the fade remains deferred: the startup controller restores the
    // main-window opacity later in the same call stack.
    QTimer::singleShot(0, this, [this]() { startAppearanceAnimation(); });
}

void FirstRunIntegrationWidget::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_scrollArea = new ruwa::ui::widgets::SmoothScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("FirstRunScrollArea"));
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setScrollBarMargin(4);
    m_scrollArea->setFillBackground(false);
    m_scrollArea->setScrollBarTransparentTrack(true);
    m_scrollArea->viewport()->installEventFilter(this);

    auto* page = new QWidget(m_scrollArea);
    page->setObjectName(QStringLiteral("FirstRunPage"));
    m_pageLayout = new QVBoxLayout(page);
    m_pageLayout->setContentsMargins(0, 0, 0, 0);
    m_pageLayout->setSpacing(0);

    auto* heroSection = new FirstRunIntegrationImageSection(
        QStringLiteral("FirstRunHeroSection"),
        QStringLiteral(":/images/FirstIntegrationHero"), page);
    m_heroSection = heroSection;
    auto* heroLayout = new QVBoxLayout(m_heroSection);
    heroLayout->setContentsMargins(48, 48, 48, 48);
    heroLayout->setSpacing(0);
    heroLayout->addStretch(1);

    m_heroGlassPanel = new FirstRunHeroGlassPanel(heroSection, m_heroSection);
    auto* heroContentLayout = new QVBoxLayout(m_heroGlassPanel);
    heroContentLayout->setContentsMargins(36, 32, 36, 32);
    heroContentLayout->setSpacing(0);

    m_heroLogo = new QLabel(m_heroGlassPanel);
    m_heroLogo->setObjectName(QStringLiteral("FirstRunHeroLogo"));
    m_heroLogo->setFixedSize(kHeroLogoSize, kHeroLogoSize);
    m_heroLogo->setAlignment(Qt::AlignCenter);
    m_heroTitle = new QLabel(m_heroGlassPanel);
    m_heroTitle->setObjectName(QStringLiteral("FirstRunHeroTitle"));
    m_heroTitle->setAlignment(Qt::AlignCenter);
    m_heroDescription = new QLabel(m_heroGlassPanel);
    m_heroDescription->setObjectName(QStringLiteral("FirstRunBodyText"));
    m_heroDescription->setAlignment(Qt::AlignCenter);
    m_heroDescription->setWordWrap(true);
    m_heroDescription->setMaximumWidth(620);

    m_startCustomizationButton = new ruwa::ui::widgets::WelcomeBannerButton(QString(),
        ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Primary, m_heroGlassPanel);
    m_startCustomizationButton->setPrimaryBorderVisible(false);
    connect(m_startCustomizationButton,
        &ruwa::ui::widgets::WelcomeBannerButton::clicked, this, [this]() {
            if (!m_scrollArea->isUserScrollingEnabled()) {
                return;
            }
            m_scrollArea->scrollTo(m_heroSection->height(),
                kCustomizationScrollDurationMs, QEasingCurve::InOutCubic);
        });

    m_skipCustomizationButton = new ruwa::ui::widgets::WelcomeBannerButton(QString(),
        ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Secondary, m_heroGlassPanel);
    m_skipCustomizationButton->setSecondaryIdleFillAlpha(24);
    connect(m_skipCustomizationButton,
        &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::completedRequested);

    auto* heroActions = new QHBoxLayout();
    heroActions->setContentsMargins(0, 0, 0, 0);
    heroActions->setSpacing(14);
    heroActions->addStretch(1);
    heroActions->addWidget(m_startCustomizationButton);
    heroActions->addWidget(m_skipCustomizationButton);
    heroActions->addStretch(1);

    heroContentLayout->addWidget(m_heroLogo, 0, Qt::AlignHCenter);
    heroContentLayout->addSpacing(28);
    heroContentLayout->addWidget(m_heroTitle);
    heroContentLayout->addSpacing(12);
    heroContentLayout->addWidget(m_heroDescription, 0, Qt::AlignHCenter);
    heroContentLayout->addSpacing(28);
    heroContentLayout->addLayout(heroActions);

    heroLayout->addWidget(m_heroGlassPanel, 0, Qt::AlignCenter);
    heroLayout->addStretch(1);
    m_pageLayout->addWidget(m_heroSection);

    auto* content = new QWidget(page);
    content->setObjectName(QStringLiteral("FirstRunContent"));
    m_contentLayout = new QVBoxLayout(content);
    m_contentLayout->setSpacing(kContentSpacing);
    updateContentMargins();

    auto* appearanceSection
        = createSection(content, QStringLiteral("FirstRunAppearanceSection"),
            kSettingsSectionMinimumHeight);
    auto* appearanceLayout = new QVBoxLayout(appearanceSection);
    appearanceLayout->setContentsMargins(0, 0, 0, 0);
    appearanceLayout->setSpacing(8);

    m_appearanceTitle = createSectionTitle(appearanceSection);
    m_appearanceTitle->setAlignment(Qt::AlignCenter);
    appearanceLayout->addWidget(m_appearanceTitle, 0, Qt::AlignHCenter);
    appearanceLayout->addSpacing(8);

    m_themeSelector
        = new ruwa::ui::widgets::ThemeSelectorWidget(appearanceSection, false);
    connect(m_themeSelector, &ruwa::ui::widgets::ThemeSelectorWidget::themeSelected,
        this, [](const ruwa::ui::core::ThemePreset& preset) {
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setThemeId(preset.id);
            settings.save();
        });
    appearanceLayout->addWidget(m_themeSelector);

    const auto& appearanceSettings
        = ruwa::core::SettingsManager::instance().settings().appearance;

    m_uiScaleChoice = new ruwa::ui::widgets::SettingsChoice(tr("UI Scale"),
        tr("Adjust the size of UI elements"),
        { tr("Small"), tr("Medium"), tr("Large") },
        qBound(0, appearanceSettings.uiScale, 2),
        appearanceSection);
    connect(m_uiScaleChoice, &ruwa::ui::widgets::SettingsChoice::selectionChanged,
        this, [](int value) {
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setUiScale(value);
            settings.save();
        });
    appearanceLayout->addWidget(m_uiScaleChoice);

    m_topBarTabAlignmentChoice = new ruwa::ui::widgets::SettingsChoice(
        tr("Top bar tab alignment"),
        tr("Place the tab strip at the left of the title bar or centered in the free space"),
        { tr("Left"), tr("Center") },
        appearanceSettings.topBarTabAlignment == 1 ? 1 : 0, appearanceSection);
    connect(m_topBarTabAlignmentChoice,
        &ruwa::ui::widgets::SettingsChoice::selectionChanged, this, [](int index) {
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setTopBarTabAlignment(index == 1 ? 1 : 0);
            settings.save();
        });
    appearanceLayout->addWidget(m_topBarTabAlignmentChoice);

    m_contentLayout->addWidget(appearanceSection);

    const auto& editorSettings
        = ruwa::core::SettingsManager::instance().settings().editor;
    auto* editorSection = createSection(
        content, QStringLiteral("FirstRunEditorSection"), kSettingsSectionMinimumHeight);
    auto* editorLayout = new QVBoxLayout(editorSection);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(8);

    m_editorTitle = createSectionTitle(editorSection);
    m_editorTitle->setAlignment(Qt::AlignCenter);
    editorLayout->addWidget(m_editorTitle, 0, Qt::AlignHCenter);
    editorLayout->addSpacing(8);

    m_autoSaveChoice = new ruwa::ui::widgets::SettingsChoice(tr("Auto-Save"),
        tr("Automatically save your work at the selected interval"),
        { tr("Off"), tr("2 min"), tr("5 min"), tr("10 min") },
        autoSaveIndexForMinutes(editorSettings.autoSaveInterval), editorSection);
    connect(m_autoSaveChoice, &ruwa::ui::widgets::SettingsChoice::selectionChanged,
        this, [](int index) {
            static constexpr int intervals[] = { 0, 2, 5, 10 };
            if (index < 0 || index >= 4) {
                return;
            }
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setAutoSaveInterval(intervals[index]);
            settings.save();
        });
    editorLayout->addWidget(m_autoSaveChoice);

    m_quickshapesToggle = new ruwa::ui::widgets::SettingsToggle(tr("Quick Shapes"),
        tr("Hold stroke to morph into straight line, circle, triangle, or square"),
        editorSettings.quickshapesEnabled, editorSection);
    connect(m_quickshapesToggle, &ruwa::ui::widgets::SettingsToggle::toggled,
        this, [](bool checked) {
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setQuickshapesEnabled(checked);
            settings.save();
        });
    editorLayout->addWidget(m_quickshapesToggle);
    m_contentLayout->addWidget(editorSection);

    const auto& performanceSettings
        = ruwa::core::SettingsManager::instance().settings().performance;
    auto* performanceSection = createSection(
        content, QStringLiteral("FirstRunPerformanceSection"), kSettingsSectionMinimumHeight);
    auto* performanceLayout = new QVBoxLayout(performanceSection);
    performanceLayout->setContentsMargins(0, 0, 0, 0);
    performanceLayout->setSpacing(8);

    m_performanceTitle = createSectionTitle(performanceSection);
    m_performanceTitle->setAlignment(Qt::AlignCenter);
    performanceLayout->addWidget(m_performanceTitle, 0, Qt::AlignHCenter);
    performanceLayout->addSpacing(8);

    m_undoMemoryChoice = new ruwa::ui::widgets::SettingsChoice(
        tr("Undo Memory Limit"), tr("Maximum memory available for undo history"),
        { tr("300 MB"), tr("1 GB"), tr("3 GB"), tr("8 GB") },
        undoMemoryIndexForMegabytes(performanceSettings.undoMemoryLimitMb),
        performanceSection);
    connect(m_undoMemoryChoice, &ruwa::ui::widgets::SettingsChoice::selectionChanged,
        this, [](int index) {
            static constexpr int limitsMb[] = { 300, 1024, 3072, 8192 };
            if (index < 0 || index >= 4) {
                return;
            }
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setUndoMemoryLimitMb(limitsMb[index]);
            settings.save();
        });
    performanceLayout->addWidget(m_undoMemoryChoice);

    m_tabletBackendChoice = new ruwa::ui::widgets::SettingsChoice(
        tr("Tablet Input Backend"),
        tr("Choose the stylus input backend. Restart is required to apply this setting."),
        { tr("WinTab (Qt)"), tr("Windows Ink"), tr("WinTab (Ruwa)") },
        qBound(0, performanceSettings.tabletBackend, 2), performanceSection);
    connect(m_tabletBackendChoice,
        &ruwa::ui::widgets::SettingsChoice::selectionChanged, this, [this](int index) {
            if (index < 0 || index > 2) {
                return;
            }

            const bool backendChanged = ruwa::Application::currentTabletBackend() != index;
            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setTabletBackend(index);
            settings.save();

            if (backendChanged) {
                ruwa::ui::widgets::MessagePopupManager::show(this,
                    tr("Tablet backend was changed. Restart is required to apply this setting."),
                    { { tr("Later"), false, []() {} },
                        { tr("Restart now"), true,
                            []() {
                                QTimer::singleShot(220, []() {
                                    if (!ruwa::Application::restart()) { }
                                });
                            } } },
                    420, m_tabletBackendChoice);
            }
        });
    performanceLayout->addWidget(m_tabletBackendChoice);
    m_contentLayout->addWidget(performanceSection);

    auto* finishSection = new FirstRunIntegrationImageSection(
        QStringLiteral("FirstRunFinishSection"),
        QStringLiteral(":/images/FirstIntegrationBottom"), content);
    m_finishSection = finishSection;
    finishSection->setMinimumHeight(kFinishMinimumHeight);
    auto* finishLayout = new QVBoxLayout(finishSection);
    finishLayout->setContentsMargins(48, 48, 48, 48);
    finishLayout->setSpacing(0);
    finishLayout->addStretch(1);

    m_finishDescription = new QLabel(finishSection);
    m_finishDescription->setObjectName(QStringLiteral("FirstRunBodyText"));
    m_finishDescription->setAlignment(Qt::AlignCenter);
    m_finishDescription->setWordWrap(true);
    m_finishDescription->setMaximumWidth(620);
    m_finishButton = new ruwa::ui::widgets::WelcomeBannerButton(QString(),
        ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Primary, finishSection);
    m_finishButton->setPrimaryBorderVisible(false);
    connect(m_finishButton, &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::completedRequested);

    finishLayout->addWidget(m_finishDescription, 0, Qt::AlignHCenter);
    finishLayout->addSpacing(20);
    finishLayout->addWidget(m_finishButton, 0, Qt::AlignHCenter);
    finishLayout->addStretch(1);
    m_contentLayout->addWidget(finishSection);
    m_pageLayout->addWidget(content);

    m_scrollArea->setWidget(page);
    rootLayout->addWidget(m_scrollArea);
    updateHeroHeight();
}

void FirstRunIntegrationWidget::retranslateUi()
{
    m_heroTitle->setText(tr("Welcome to Ruwa"));
    m_heroDescription->setText(tr("Let's tailor everything to the way you create."));
    m_startCustomizationButton->setText(tr("Start customizing"));
    m_startCustomizationButton->syncSizeToText();
    m_skipCustomizationButton->setText(tr("No, thanks"));
    m_skipCustomizationButton->syncSizeToText();
    m_appearanceTitle->setText(tr("Appearance settings"));
    m_uiScaleChoice->retranslateUi(tr("UI Scale"), tr("Adjust the size of UI elements"),
        { tr("Small"), tr("Medium"), tr("Large") });
    m_topBarTabAlignmentChoice->retranslateUi(tr("Top bar tab alignment"),
        tr("Place the tab strip at the left of the title bar or centered in the free space"),
        { tr("Left"), tr("Center") });
    m_editorTitle->setText(tr("Editor"));
    m_autoSaveChoice->retranslateUi(tr("Auto-Save"),
        tr("Automatically save your work at the selected interval"),
        { tr("Off"), tr("2 min"), tr("5 min"), tr("10 min") });
    m_quickshapesToggle->setLabel(tr("Quick Shapes"));
    m_quickshapesToggle->setDescription(
        tr("Hold stroke to morph into straight line, circle, triangle, or square"));
    m_performanceTitle->setText(tr("Performance"));
    m_undoMemoryChoice->retranslateUi(
        tr("Undo Memory Limit"), tr("Maximum memory available for undo history"),
        { tr("300 MB"), tr("1 GB"), tr("3 GB"), tr("8 GB") });
    m_tabletBackendChoice->retranslateUi(tr("Tablet Input Backend"),
        tr("Choose the stylus input backend. Restart is required to apply this setting."),
        { tr("WinTab (Qt)"), tr("Windows Ink"), tr("WinTab (Ruwa)") });
    m_finishDescription->setText(
        tr("You can change all of these settings later in the Settings tab."));
    m_finishButton->setText(tr("Finish personalization"));
    m_finishButton->syncSizeToText();
}

void FirstRunIntegrationWidget::ensureAppearanceOverlay()
{
    if (m_appearanceAnimationStarted || m_appearanceOverlay || !isVisible()) {
        return;
    }

    const QColor backgroundColor
        = ruwa::ui::core::ThemeManager::instance().colors().background;
    auto* overlay
        = new ruwa::ui::widgets::WidgetFadeInOverlay(this, backgroundColor);
    m_appearanceOverlay = overlay;
    m_scrollArea->setUserScrollingEnabled(false);
    connect(overlay, &ruwa::ui::widgets::WidgetFadeInOverlay::animationFinished,
        this, [this]() { m_scrollArea->setUserScrollingEnabled(true); });
    connect(overlay, &QObject::destroyed, this, [this, overlay]() {
        if (m_appearanceOverlay == overlay) {
            m_appearanceOverlay.clear();
            m_scrollArea->setUserScrollingEnabled(true);
        }
    });

    overlay->showOverlay();
}

void FirstRunIntegrationWidget::startAppearanceAnimation()
{
    if (m_appearanceAnimationStarted || !isVisible() || !window()->isVisible()
        || window()->windowOpacity() <= 0.0) {
        return;
    }

    ensureAppearanceOverlay();
    if (!m_appearanceOverlay) {
        return;
    }

    m_appearanceAnimationStarted = true;
    m_appearanceOverlay->startAnimation(
        kAppearanceDurationMs, kAppearanceDelayMs, QEasingCurve::InOutCubic);
}

void FirstRunIntegrationWidget::updateTheme()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QString style = QStringLiteral(
                              "FirstRunIntegrationWidget, "
                              "QWidget#FirstRunScrollArea, "
                              "QWidget#FirstRunPage, "
                              "QWidget#FirstRunContent {"
                              "  background: %1;"
                              "}"
                              "QFrame#FirstRunHeroSection {"
                              "  background: transparent;"
                              "  border: none;"
                              "  border-radius: 0;"
                              "}"
                              "QFrame#FirstRunAppearanceSection, "
                              "QFrame#FirstRunEditorSection, "
                              "QFrame#FirstRunPerformanceSection, "
                              "QFrame#FirstRunFinishSection {"
                              "  background: transparent;"
                              "  border: none;"
                              "}"
                              "QLabel#FirstRunHeroTitle, "
                              "QLabel#FirstRunSectionTitle {"
                              "  color: %3;"
                              "}"
                              "QLabel#FirstRunBodyText {"
                              "  color: %2;"
                              "}")
                              .arg(colors.background.name(QColor::HexArgb),
                                  colors.textMuted.name(QColor::HexArgb),
                                  colors.text.name(QColor::HexArgb));
    setStyleSheet(style);
    m_heroSection->update();
    m_heroGlassPanel->update();
    m_finishSection->update();

    const QPixmap logo = ruwa::ui::core::ThemeManager::instance().icons()
                             .getApplicationLogoPixmap(QSize(kHeroLogoSize, kHeroLogoSize));
    m_heroLogo->setPixmap(logo);

    QFont heroFont = colors.fonts.getTitleFont();
    heroFont.setPixelSize(48);
    m_heroTitle->setFont(heroFont);

    QFont heroDescriptionFont = colors.fonts.getUIFont();
    heroDescriptionFont.setPixelSize(17);
    m_heroDescription->setFont(heroDescriptionFont);

    QFont finishDescriptionFont = colors.fonts.getUIFont();
    finishDescriptionFont.setPixelSize(14);
    m_finishDescription->setFont(finishDescriptionFont);

    QFont sectionFont = colors.fonts.getTitleFont();
    sectionFont.setPixelSize(22);
    for (QLabel* label : { m_appearanceTitle, m_editorTitle, m_performanceTitle }) {
        label->setFont(sectionFont);
    }
}

void FirstRunIntegrationWidget::updateContentMargins()
{
    if (!m_contentLayout) {
        return;
    }

    const int sideMargin = kBaseContentSideMargin + m_contentSideMargin;
    m_contentLayout->setContentsMargins(
        sideMargin, kContentTopMargin, sideMargin, kContentBottomMargin);
}

void FirstRunIntegrationWidget::updateHeroHeight()
{
    if (!m_scrollArea || !m_heroSection) {
        return;
    }

    const int viewportHeight = m_scrollArea->viewport()->height();
    if (viewportHeight > 0 && m_heroSection->height() != viewportHeight) {
        m_heroSection->setFixedHeight(viewportHeight);
        m_heroGlassPanel->update();
    }
}

} // namespace ruwa::ui::first_run_integration
