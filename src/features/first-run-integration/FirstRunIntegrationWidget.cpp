// SPDX-License-Identifier: MPL-2.0

#include "features/first-run-integration/FirstRunIntegrationWidget.h"

#include "features/first-run-integration/FirstRunIntegrationBackgroundWidget.h"
#include "features/settings/SettingsManager.h"
#include "features/home/welcome/WelcomeBannerButton.h"
#include "features/theme/editor/ThemePreviewWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/resources/FontFamilyNames.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"

#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStackedLayout>
#include <QTextOption>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QtGlobal>

#include <utility>

namespace ruwa::ui::first_run_integration {

namespace {

constexpr int kBaseOverlayMargin = 64;
constexpr int kTitleWelcomePixelSize = 112;
constexpr int kTitleProductPixelSize = 128;
constexpr int kTitleLineSpacing = 6;
constexpr int kTitleTopPadding = 2;
constexpr int kTitleBottomPadding = 2;
constexpr int kTitleToBodySpacing = 20;
constexpr int kProductUnderlineOffset = 8;
constexpr int kProductUnderlineWidth = 2;
constexpr int kBodyTextPixelSize = 19;
constexpr int kBodyTextMaxWidth = 700;
constexpr int kIntroStartDelayMs = 1500;
constexpr int kIntroAnimationDurationMs = 620;
constexpr int kIntroNextStartPercent = 20;
constexpr int kIntroSlideDistance = 18;
constexpr int kPageTransitionDurationMs = 620;
constexpr int kNextPageStartPercent = 80;
constexpr int kPageTransitionSlideDistance = 96;
constexpr int kSetupTitlePixelSize = 52;
constexpr int kSetupLabelPixelSize = 17;
constexpr int kSetupDescriptionPixelSize = 14;
constexpr int kSetupActionSlotWidth = 720;

QFont makePixelFont(const QString& family, int pixelSize, QFont::Weight weight = QFont::Normal)
{
    QFont font(family);
    font.setPixelSize(pixelSize);
    font.setWeight(weight);
    return font;
}

qreal normalizedProgress(qreal value)
{
    return qBound(0.0, value, 1.0);
}

QString displayApplicationVersion()
{
    QString version = QApplication::applicationVersion().trimmed();
    const int suffixIndex = version.indexOf(QLatin1Char('-'));
    if (suffixIndex > 0) {
        version = version.left(suffixIndex);
    }
    return version;
}

} // namespace

class FirstRunIntegrationHeroTitleWidget final : public QWidget {
public:
    explicit FirstRunIntegrationHeroTitleWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override { return QSize(metricsWidth(), metricsHeight()); }

    QSize minimumSizeHint() const override { return sizeHint(); }

    void refresh()
    {
        setMinimumSize(sizeHint());
        updateGeometry();
        update();
    }

    void setWelcomeText(const QString& text)
    {
        if (m_welcomeText == text) {
            return;
        }

        m_welcomeText = text;
        refresh();
    }

    void setWelcomeProgress(qreal progress)
    {
        const qreal normalized = normalizedProgress(progress);
        if (qFuzzyCompare(m_welcomeProgress, normalized)) {
            return;
        }

        m_welcomeProgress = normalized;
        update();
    }

    void setProductProgress(qreal progress)
    {
        const qreal normalized = normalizedProgress(progress);
        if (qFuzzyCompare(m_productProgress, normalized)) {
            return;
        }

        m_productProgress = normalized;
        update();
    }

    void setPageOpacity(qreal opacity)
    {
        const qreal normalized = normalizedProgress(opacity);
        if (qFuzzyCompare(m_pageOpacity, normalized)) {
            return;
        }

        m_pageOpacity = normalized;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

        const QFont welcomeFont = makeTitleFont(kTitleWelcomePixelSize);
        const QFont productFont = makeTitleFont(kTitleProductPixelSize);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(colors.text);

        const QFontMetrics welcomeMetrics(welcomeFont);
        const QFontMetrics productMetrics(productFont);
        const QRect welcomeBounds = welcomeMetrics.tightBoundingRect(m_welcomeText);
        const QRect productBounds = productMetrics.tightBoundingRect(productText());

        const int welcomeOffset = -qRound(kIntroSlideDistance * (1.0 - m_welcomeProgress));
        const int productOffset = -qRound(kIntroSlideDistance * (1.0 - m_productProgress));

        int y = kTitleTopPadding - welcomeBounds.top() + welcomeOffset;
        painter.setFont(welcomeFont);
        painter.setOpacity(m_pageOpacity * m_welcomeProgress);
        painter.drawText(QPoint(-welcomeBounds.left(), y), m_welcomeText);

        y = kTitleTopPadding + welcomeBounds.height() + kTitleLineSpacing - productBounds.top()
            + productOffset;
        painter.setFont(productFont);
        painter.setOpacity(m_pageOpacity * m_productProgress);
        painter.drawText(QPoint(-productBounds.left(), y), productText());

        const int underlineY = y + productBounds.bottom() + kProductUnderlineOffset;
        const int underlineStartX
            = -productBounds.left() + productMetrics.horizontalAdvance(QStringLiteral("Ruwa "));
        const int underlineEndX
            = -productBounds.left() + productMetrics.horizontalAdvance(productText());
        painter.setPen(QPen(colors.text, kProductUnderlineWidth, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(underlineStartX, underlineY), QPointF(underlineEndX, underlineY));
    }

private:
    QString productText() const
    {
        return QStringLiteral("Ruwa v%1").arg(displayApplicationVersion());
    }

    QFont makeTitleFont(int pixelSize) const
    {
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        QFont font(colors.fonts.titleFont);
        font.setPixelSize(pixelSize);
        font.setWeight(QFont::Normal);
        return font;
    }

    int metricsWidth() const
    {
        const QFont welcomeFont = makeTitleFont(kTitleWelcomePixelSize);
        const QFont productFont = makeTitleFont(kTitleProductPixelSize);
        const QFontMetrics welcomeMetrics(welcomeFont);
        const QFontMetrics productMetrics(productFont);
        const int welcomeWidth = qMax(welcomeMetrics.horizontalAdvance(m_welcomeText),
            welcomeMetrics.tightBoundingRect(m_welcomeText).width());
        const int productWidth = qMax(productMetrics.horizontalAdvance(productText()),
            productMetrics.tightBoundingRect(productText()).width());
        return qMax(720, qMax(welcomeWidth, productWidth) + kIntroSlideDistance + 16);
    }

    int metricsHeight() const
    {
        const QFont welcomeFont = makeTitleFont(kTitleWelcomePixelSize);
        const QFont productFont = makeTitleFont(kTitleProductPixelSize);
        const QFontMetrics welcomeMetrics(welcomeFont);
        const QFontMetrics productMetrics(productFont);
        return kTitleTopPadding + welcomeMetrics.tightBoundingRect(m_welcomeText).height()
            + kTitleLineSpacing + productMetrics.tightBoundingRect(productText()).height()
            + kProductUnderlineOffset + kProductUnderlineWidth + kTitleBottomPadding;
    }

private:
    QString m_welcomeText { QStringLiteral("Welcome to") };
    qreal m_welcomeProgress { 0.0 };
    qreal m_productProgress { 0.0 };
    qreal m_pageOpacity { 1.0 };
};

class FirstRunIntegrationHeroBodyWidget final : public QWidget {
public:
    explicit FirstRunIntegrationHeroBodyWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        setMaximumWidth(kBodyTextMaxWidth);
    }

    QSize sizeHint() const override
    {
        return QSize(kBodyTextMaxWidth, textHeight(kBodyTextMaxWidth));
    }

    QSize minimumSizeHint() const override { return QSize(420, textHeight(420)); }

    void setText(const QString& text)
    {
        if (m_text == text) {
            return;
        }

        m_text = text;
        refresh();
    }

    void setTextFont(const QFont& font)
    {
        if (m_font == font) {
            return;
        }

        m_font = font;
        refresh();
    }

    void setTextColor(const QColor& color)
    {
        if (m_textColor == color) {
            return;
        }

        m_textColor = color;
        update();
    }

    void setRevealProgress(qreal progress)
    {
        const qreal normalized = normalizedProgress(progress);
        if (qFuzzyCompare(m_revealProgress, normalized)) {
            return;
        }

        m_revealProgress = normalized;
        update();
    }

    void setPageOpacity(qreal opacity)
    {
        const qreal normalized = normalizedProgress(opacity);
        if (qFuzzyCompare(m_pageOpacity, normalized)) {
            return;
        }

        m_pageOpacity = normalized;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setFont(m_font);
        painter.setPen(m_textColor);
        painter.setOpacity(m_pageOpacity * m_revealProgress);

        QTextOption option;
        option.setAlignment(Qt::AlignLeft | Qt::AlignTop);
        option.setWrapMode(QTextOption::WordWrap);

        const int offset = qRound(kIntroSlideDistance * (1.0 - m_revealProgress));
        painter.drawText(QRectF(rect()).translated(0, offset), m_text, option);
    }

private:
    void refresh()
    {
        updateGeometry();
        update();
    }

    int textHeight(int width) const
    {
        const QFontMetrics metrics(m_font);
        const QRect bounds
            = metrics.boundingRect(QRect(0, 0, qMax(1, width), 1000), Qt::TextWordWrap, m_text);
        return bounds.height() + 2;
    }

private:
    QString m_text;
    QFont m_font;
    QColor m_textColor { Qt::white };
    qreal m_revealProgress { 0.0 };
    qreal m_pageOpacity { 1.0 };
};

class FirstRunIntegrationSetupTitleWidget final : public QWidget {
public:
    explicit FirstRunIntegrationSetupTitleWidget(const QString& text, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_text(text)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override { return QSize(textWidth(), textHeight()); }

    QSize minimumSizeHint() const override { return QSize(textWidth(), textHeight()); }

    void setText(const QString& text)
    {
        if (m_text == text) {
            return;
        }

        m_text = text;
        refresh();
    }

    void setTextFont(const QFont& font)
    {
        if (m_font == font) {
            return;
        }

        m_font = font;
        refresh();
    }

    void setTextColor(const QColor& color)
    {
        if (m_textColor == color) {
            return;
        }

        m_textColor = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setFont(m_font);
        painter.setPen(m_textColor);

        const QFontMetrics metrics(m_font);
        const QRect bounds = metrics.tightBoundingRect(m_text);
        painter.drawText(QPoint(-bounds.left(), kTitleTopPadding - bounds.top()), m_text);
    }

private:
    void refresh()
    {
        updateGeometry();
        update();
    }

    int textWidth() const { return QFontMetrics(m_font).horizontalAdvance(m_text) + 8; }

    int textHeight() const
    {
        return kTitleTopPadding + QFontMetrics(m_font).tightBoundingRect(m_text).height()
            + kTitleBottomPadding;
    }

private:
    QString m_text;
    QFont m_font;
    QColor m_textColor { Qt::white };
};

class FirstRunIntegrationThemePreviewWidget final : public ruwa::ui::widgets::ThemePreviewWidget {
public:
    explicit FirstRunIntegrationThemePreviewWidget(
        const ruwa::ui::core::ThemePreset& preset, QWidget* parent = nullptr)
        : ThemePreviewWidget(preset, parent)
    {
    }

    ruwa::ui::widgets::ContextMenuType contextMenuType() const override
    {
        return ruwa::ui::widgets::ContextMenuType::None;
    }
};

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

void FirstRunIntegrationWidget::startPreview()
{
    if (m_background) {
        m_background->setPreviewAnimationRunning(true);
    }
}

void FirstRunIntegrationWidget::setContentSideMargin(int margin)
{
    const int normalized = qMax(0, margin);
    if (m_contentSideMargin == normalized) {
        return;
    }

    m_contentSideMargin = normalized;
    updateOverlayMargins();
}

void FirstRunIntegrationWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    startPreview();
    startIntroAnimations();
}

void FirstRunIntegrationWidget::hideEvent(QHideEvent* event)
{
    stopIntroAnimations();
    stopPageTransitionAnimations();
    if (m_background) {
        m_background->setPreviewAnimationRunning(false);
    }
    QWidget::hideEvent(event);
}

void FirstRunIntegrationWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void FirstRunIntegrationWidget::setupUi()
{
    auto* stack = new QStackedLayout(this);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setStackingMode(QStackedLayout::StackAll);

    m_background = new FirstRunIntegrationBackgroundWidget(this);
    stack->addWidget(m_background);

    auto* overlay = new QWidget(this);
    overlay->setAttribute(Qt::WA_TranslucentBackground);
    auto* pagesLayout = new QStackedLayout(overlay);
    pagesLayout->setContentsMargins(0, 0, 0, 0);
    pagesLayout->setStackingMode(QStackedLayout::StackAll);

    m_welcomePage = new QWidget(overlay);
    m_welcomePage->setAttribute(Qt::WA_TranslucentBackground);

    m_nextPage = new QWidget(overlay);
    m_nextPage->setAttribute(Qt::WA_TranslucentBackground);
    m_nextPageOpacityEffect = new QGraphicsOpacityEffect(m_nextPage);
    m_nextPageOpacityEffect->setOpacity(0.0);
    m_nextPage->setGraphicsEffect(m_nextPageOpacityEffect);
    m_nextPage->hide();

    m_finishPage = new QWidget(overlay);
    m_finishPage->setAttribute(Qt::WA_TranslucentBackground);
    m_finishPageOpacityEffect = new QGraphicsOpacityEffect(m_finishPage);
    m_finishPageOpacityEffect->setOpacity(0.0);
    m_finishPage->setGraphicsEffect(m_finishPageOpacityEffect);
    m_finishPage->hide();

    pagesLayout->addWidget(m_finishPage);
    pagesLayout->addWidget(m_nextPage);
    pagesLayout->addWidget(m_welcomePage);
    pagesLayout->setCurrentWidget(m_welcomePage);

    m_overlayLayout = new QVBoxLayout(m_welcomePage);
    m_overlayLayout->setSpacing(0);
    m_nextPageLayout = new QVBoxLayout(m_nextPage);
    m_nextPageLayout->setSpacing(0);
    m_finishPageLayout = new QVBoxLayout(m_finishPage);
    m_finishPageLayout->setSpacing(0);
    updateOverlayMargins();

    auto* copyColumn = new QVBoxLayout();
    copyColumn->setContentsMargins(0, 0, 0, 0);
    copyColumn->setSpacing(0);

    m_heroTitle = new FirstRunIntegrationHeroTitleWidget(m_welcomePage);

    m_bodyText = new FirstRunIntegrationHeroBodyWidget(m_welcomePage);

    copyColumn->addWidget(m_heroTitle);
    copyColumn->addSpacing(kTitleToBodySpacing);
    copyColumn->addWidget(m_bodyText);

    m_actionsSlot = new QWidget(m_welcomePage);
    m_actionsSlot->setAttribute(Qt::WA_TranslucentBackground);
    m_actionsSlot->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_actionsContainer = new QWidget(m_actionsSlot);
    m_actionsContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_actionsOpacityEffect = new QGraphicsOpacityEffect(m_actionsContainer);
    m_actionsOpacityEffect->setOpacity(0.0);
    m_actionsContainer->setGraphicsEffect(m_actionsOpacityEffect);

    auto* actionsColumn = new QVBoxLayout(m_actionsContainer);
    actionsColumn->setContentsMargins(0, 0, 0, 0);
    actionsColumn->setSpacing(0);

    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(14);

    m_skipSetupButton = new ruwa::ui::widgets::WelcomeBannerButton(QString(),
        ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Secondary, m_actionsContainer);
    m_skipSetupButton->setSecondaryIdleFillAlpha(24);
    connect(m_skipSetupButton, &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::completedRequested);

    m_getStartedButton = new ruwa::ui::widgets::WelcomeBannerButton(QString(),
        ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Primary, m_actionsContainer);
    m_getStartedButton->setPrimaryBorderVisible(false);
    connect(m_getStartedButton, &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::startSetupSectionTransition);

    actionsLayout->addWidget(m_skipSetupButton);
    actionsLayout->addWidget(m_getStartedButton);
    actionsColumn->addLayout(actionsLayout);

    auto* copyRow = new QHBoxLayout();
    copyRow->setContentsMargins(0, 0, 0, 0);
    copyRow->setSpacing(0);
    copyRow->addLayout(copyColumn);
    copyRow->addStretch(1);

    auto* bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(0);
    bottomRow->addStretch(1);
    bottomRow->addWidget(m_actionsSlot);

    m_overlayLayout->addStretch(7);
    m_overlayLayout->addLayout(copyRow);
    m_overlayLayout->addStretch(6);
    m_overlayLayout->addLayout(bottomRow);
    m_overlayLayout->addStretch(2);

    auto* setupColumn = new QVBoxLayout();
    setupColumn->setContentsMargins(0, 0, 0, 0);
    setupColumn->setSpacing(18);

    m_setupTitle = new FirstRunIntegrationSetupTitleWidget(QString(), m_nextPage);

    m_languageLabel = new QLabel(m_nextPage);
    m_languageLabel->setObjectName(QStringLiteral("FirstRunSetupLabel"));
    m_languageLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_languageLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* languageCombo = new ruwa::ui::widgets::AnimatedComboBox(m_nextPage);
    languageCombo->setMinimumWidth(260);
    languageCombo->setPopupMinWidth(260);
    const auto languages = ruwa::ui::core::TranslationManager::instance().availableLanguages();
    if (languages.isEmpty()) {
        languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en"));
        languageCombo->addItem(QStringLiteral("Russian"), QStringLiteral("ru"));
    } else {
        for (const auto& language : languages) {
            languageCombo->addItem(language.name, language.code);
        }
    }
    const int languageIndex = languageCombo->findIndexByData(
        ruwa::ui::core::TranslationManager::instance().currentLanguage());
    languageCombo->setCurrentIndex(languageIndex >= 0 ? languageIndex : 0);
    connect(languageCombo, &ruwa::ui::widgets::AnimatedComboBox::currentIndexChanged, this,
        [languageCombo](int index) {
            const QString code = languageCombo->itemData(index).toString();
            if (!code.isEmpty()) {
                ruwa::ui::core::TranslationManager::instance().setLanguage(code);
            }
        });

    m_themeLabel = new QLabel(m_nextPage);
    m_themeLabel->setObjectName(QStringLiteral("FirstRunSetupLabel"));
    m_themeLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_themeLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_themeDescription = new QLabel(m_nextPage);
    m_themeDescription->setObjectName(QStringLiteral("FirstRunSetupDescription"));
    m_themeDescription->setAttribute(Qt::WA_TranslucentBackground);
    m_themeDescription->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* themeRow = new QHBoxLayout();
    themeRow->setContentsMargins(0, 0, 0, 0);
    themeRow->setSpacing(10);

    const auto builtInThemes = ruwa::ui::core::ThemeManager::instance().builtInPresets();
    const QUuid currentThemeId = ruwa::ui::core::ThemeManager::instance().currentPresetId();
    for (const auto& preset : builtInThemes) {
        auto* preview = new FirstRunIntegrationThemePreviewWidget(preset, m_nextPage);
        preview->setSelected(preset.id == currentThemeId);
        m_setupThemePreviews.append(preview);
        connect(preview, &ruwa::ui::widgets::ThemePreviewWidget::selected, this,
            [this](const ruwa::ui::core::ThemePreset& selectedPreset) {
                for (auto* themePreview : m_setupThemePreviews) {
                    if (themePreview && themePreview->preset().id != selectedPreset.id) {
                        themePreview->setSelected(false);
                    }
                }

                auto& themeManager = ruwa::ui::core::ThemeManager::instance();
                themeManager.applyPreset(selectedPreset);
                auto& settings = ruwa::core::SettingsManager::instance();
                settings.setThemeId(selectedPreset.id);
                settings.save();
            });
        themeRow->addWidget(preview);
    }
    themeRow->addStretch(1);

    auto* setupActionsSlot = new QWidget(m_nextPage);
    setupActionsSlot->setAttribute(Qt::WA_TranslucentBackground);
    setupActionsSlot->setFixedWidth(kSetupActionSlotWidth);

    auto* setupActionsRow = new QHBoxLayout(setupActionsSlot);
    setupActionsRow->setContentsMargins(0, 0, 0, 0);
    setupActionsRow->setSpacing(14);
    setupActionsRow->addStretch(1);

    m_setupBackButton = new ruwa::ui::widgets::WelcomeBannerButton(QStringLiteral("<-"),
        ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Secondary, m_nextPage);
    m_setupBackButton->setBaseMinimumWidth(48);
    m_setupBackButton->setSecondaryIdleFillAlpha(24);
    connect(m_setupBackButton, &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::startWelcomeSectionTransition);

    m_setupContinueButton = new ruwa::ui::widgets::WelcomeBannerButton(
        QString(), ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Primary, m_nextPage);
    m_setupContinueButton->setPrimaryBorderVisible(false);
    connect(m_setupContinueButton, &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::startFinishSectionTransition);

    setupActionsRow->addWidget(m_setupBackButton);
    setupActionsRow->addWidget(m_setupContinueButton);

    setupColumn->addWidget(m_setupTitle);
    setupColumn->addSpacing(8);
    setupColumn->addWidget(m_languageLabel);
    setupColumn->addWidget(languageCombo);
    setupColumn->addSpacing(12);
    setupColumn->addWidget(m_themeLabel);
    setupColumn->addWidget(m_themeDescription);
    setupColumn->addLayout(themeRow);
    auto* setupRow = new QHBoxLayout();
    setupRow->setContentsMargins(0, 0, 0, 0);
    setupRow->setSpacing(0);
    setupRow->addLayout(setupColumn);
    setupRow->addStretch(1);

    m_nextPageLayout->addStretch(5);
    m_nextPageLayout->addLayout(setupRow);
    m_nextPageLayout->addStretch(4);
    auto* setupActionsPageRow = new QHBoxLayout();
    setupActionsPageRow->setContentsMargins(0, 0, 0, 0);
    setupActionsPageRow->setSpacing(0);
    setupActionsPageRow->addWidget(setupActionsSlot);
    setupActionsPageRow->addStretch(1);
    m_nextPageLayout->addLayout(setupActionsPageRow);
    m_nextPageLayout->addStretch(2);

    auto* finishColumn = new QVBoxLayout();
    finishColumn->setContentsMargins(0, 0, 0, 0);
    finishColumn->setSpacing(18);

    m_finishTitle = new FirstRunIntegrationSetupTitleWidget(QString(), m_finishPage);

    m_finishBody = new QLabel(m_finishPage);
    m_finishBody->setObjectName(QStringLiteral("FirstRunSetupDescription"));
    m_finishBody->setAttribute(Qt::WA_TranslucentBackground);
    m_finishBody->setWordWrap(true);
    m_finishBody->setMaximumWidth(720);
    m_finishBody->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    m_finishAlphaBody = new QLabel(m_finishPage);
    m_finishAlphaBody->setObjectName(QStringLiteral("FirstRunSetupDescription"));
    m_finishAlphaBody->setAttribute(Qt::WA_TranslucentBackground);
    m_finishAlphaBody->setWordWrap(true);
    m_finishAlphaBody->setMaximumWidth(720);
    m_finishAlphaBody->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* finishActionsSlot = new QWidget(m_finishPage);
    finishActionsSlot->setAttribute(Qt::WA_TranslucentBackground);
    finishActionsSlot->setFixedWidth(kSetupActionSlotWidth);

    auto* finishActionsRow = new QHBoxLayout(finishActionsSlot);
    finishActionsRow->setContentsMargins(0, 0, 0, 0);
    finishActionsRow->setSpacing(14);
    finishActionsRow->addStretch(1);

    m_finishBackButton = new ruwa::ui::widgets::WelcomeBannerButton(QStringLiteral("<-"),
        ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Secondary, m_finishPage);
    m_finishBackButton->setBaseMinimumWidth(48);
    m_finishBackButton->setSecondaryIdleFillAlpha(24);
    connect(m_finishBackButton, &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::startSetupFromFinishTransition);

    m_startCreatingButton = new ruwa::ui::widgets::WelcomeBannerButton(
        QString(), ruwa::ui::widgets::WelcomeBannerButton::ButtonStyle::Primary, m_finishPage);
    m_startCreatingButton->setPrimaryBorderVisible(false);
    connect(m_startCreatingButton, &ruwa::ui::widgets::WelcomeBannerButton::clicked, this,
        &FirstRunIntegrationWidget::customizeRequested);

    finishActionsRow->addWidget(m_finishBackButton);
    finishActionsRow->addWidget(m_startCreatingButton);

    finishColumn->addWidget(m_finishTitle);
    finishColumn->addSpacing(8);
    finishColumn->addWidget(m_finishBody);
    finishColumn->addWidget(m_finishAlphaBody);
    auto* finishRow = new QHBoxLayout();
    finishRow->setContentsMargins(0, 0, 0, 0);
    finishRow->setSpacing(0);
    finishRow->addLayout(finishColumn);
    finishRow->addStretch(1);

    m_finishPageLayout->addStretch(6);
    m_finishPageLayout->addLayout(finishRow);
    m_finishPageLayout->addStretch(5);
    auto* finishActionsPageRow = new QHBoxLayout();
    finishActionsPageRow->setContentsMargins(0, 0, 0, 0);
    finishActionsPageRow->setSpacing(0);
    finishActionsPageRow->addWidget(finishActionsSlot);
    finishActionsPageRow->addStretch(1);
    m_finishPageLayout->addLayout(finishActionsPageRow);
    m_finishPageLayout->addStretch(2);

    stack->addWidget(overlay);
    stack->setCurrentWidget(overlay);
}

void FirstRunIntegrationWidget::retranslateUi()
{
    if (m_heroTitle) {
        m_heroTitle->setWelcomeText(tr("Welcome to"));
    }
    if (m_bodyText) {
        m_bodyText->setText(
            tr("A next-generation drawing tool built for artists who refuse to compromise. "
               "Precision, freedom, expression - all in one canvas."));
    }
    if (m_setupTitle) {
        m_setupTitle->setText(tr("Setup"));
    }
    if (m_finishTitle) {
        m_finishTitle->setText(tr("All essentials are set"));
    }
    if (m_languageLabel) {
        m_languageLabel->setText(tr("Language"));
    }
    if (m_themeLabel) {
        m_themeLabel->setText(tr("Theme"));
    }
    if (m_themeDescription) {
        m_themeDescription->setText(tr("Built-in themes only"));
    }
    if (m_finishBody) {
        m_finishBody->setText(
            tr("If you want deeper customization, you can always find more options in Settings."));
    }
    if (m_finishAlphaBody) {
        m_finishAlphaBody->setText(tr("Ruwa is still in alpha. Future versions will bring many "
                                      "more setup and personalization options."));
    }

    if (m_skipSetupButton) {
        m_skipSetupButton->setText(tr("Skip setup"));
        m_skipSetupButton->syncSizeToText();
    }
    if (m_getStartedButton) {
        m_getStartedButton->setText(tr("Get started ->"));
        m_getStartedButton->syncSizeToText();
    }
    if (m_setupContinueButton) {
        m_setupContinueButton->setText(tr("Continue"));
        m_setupContinueButton->syncSizeToText();
    }
    if (m_startCreatingButton) {
        m_startCreatingButton->setText(tr("Start creating"));
        m_startCreatingButton->syncSizeToText();
    }

    if (m_actionsSlot && m_actionsContainer) {
        const QSize actionsSize = m_actionsContainer->sizeHint();
        m_actionsSlot->setFixedSize(actionsSize + QSize(0, kIntroSlideDistance));
        m_actionsContainer->resize(actionsSize);
    }
}

void FirstRunIntegrationWidget::updateTheme()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (m_background) {
        m_background->setRevealOverlayColor(colors.background);
        m_background->setBlobColor(colors.primary);
    }

    if (m_bodyText) {
        m_bodyText->setTextFont(
            makePixelFont(ruwa::ui::core::FontFamilyNames::DMSans18pt, kBodyTextPixelSize));
        m_bodyText->setTextColor(colors.textMuted);
    }

    if (m_nextPage || m_finishPage) {
        QFont setupTitleFont(colors.fonts.titleFont);
        setupTitleFont.setPixelSize(kSetupTitlePixelSize);
        setupTitleFont.setWeight(QFont::Normal);

        const QFont setupLabelFont = makePixelFont(
            ruwa::ui::core::FontFamilyNames::DMSans18pt, kSetupLabelPixelSize, QFont::DemiBold);
        const QFont setupDescriptionFont = makePixelFont(
            ruwa::ui::core::FontFamilyNames::DMSans18pt, kSetupDescriptionPixelSize);

        const QVector<QWidget*> setupPages { m_nextPage, m_finishPage };
        for (auto* page : setupPages) {
            if (!page) {
                continue;
            }
            for (auto* label : page->findChildren<QLabel*>(QStringLiteral("FirstRunSetupLabel"))) {
                label->setFont(setupLabelFont);
                label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                        .arg(colors.text.name(QColor::HexArgb)));
            }
            for (auto* label :
                page->findChildren<QLabel*>(QStringLiteral("FirstRunSetupDescription"))) {
                label->setFont(setupDescriptionFont);
                label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                        .arg(colors.textMuted.name(QColor::HexArgb)));
            }
        }

        if (m_setupTitle) {
            m_setupTitle->setTextFont(setupTitleFont);
            m_setupTitle->setTextColor(colors.text);
        }
        if (m_finishTitle) {
            m_finishTitle->setTextFont(setupTitleFont);
            m_finishTitle->setTextColor(colors.text);
        }
    }

    if (m_heroTitle) {
        m_heroTitle->refresh();
    }

    if (m_skipSetupButton) {
        m_skipSetupButton->setSizeScale(0.86);
        m_skipSetupButton->syncSizeToText();
    }

    if (m_getStartedButton) {
        m_getStartedButton->setSizeScale(0.86);
        m_getStartedButton->syncSizeToText();
    }

    if (m_setupBackButton) {
        m_setupBackButton->setSizeScale(0.86);
        m_setupBackButton->syncSizeToText();
    }

    if (m_setupContinueButton) {
        m_setupContinueButton->setSizeScale(0.86);
        m_setupContinueButton->syncSizeToText();
    }

    if (m_finishBackButton) {
        m_finishBackButton->setSizeScale(0.86);
        m_finishBackButton->syncSizeToText();
    }

    if (m_startCreatingButton) {
        m_startCreatingButton->setSizeScale(0.86);
        m_startCreatingButton->syncSizeToText();
    }

    if (m_actionsSlot && m_actionsContainer) {
        const QSize actionsSize = m_actionsContainer->sizeHint();
        m_actionsSlot->setFixedSize(actionsSize + QSize(0, kIntroSlideDistance));
        m_actionsContainer->resize(actionsSize);
        if (!m_actionsFinalPositionKnown) {
            m_actionsContainer->move(0, kIntroSlideDistance);
        }
    }
}

void FirstRunIntegrationWidget::startIntroAnimations()
{
    stopPageTransitionAnimations();
    m_pageTransitionRunning = false;
    if (m_welcomePage) {
        m_welcomePage->show();
        m_welcomePage->raise();
        m_welcomePage->move(QPoint(0, 0));
    }
    if (m_nextPage) {
        m_nextPage->hide();
        m_nextPage->move(QPoint(0, 0));
    }
    if (m_finishPage) {
        m_finishPage->hide();
        m_finishPage->move(QPoint(0, 0));
    }
    if (m_nextPageOpacityEffect) {
        m_nextPageOpacityEffect->setOpacity(0.0);
    }
    if (m_finishPageOpacityEffect) {
        m_finishPageOpacityEffect->setOpacity(0.0);
    }
    if (m_getStartedButton) {
        m_getStartedButton->setEnabled(true);
    }

    stopIntroAnimations();
    resetIntroAnimationState();
    ++m_introGeneration;

    // Resolve the actions-slot size and settle the overlay layout up front,
    // while the copy is still invisible (opacity 0). Doing this here — instead
    // of inside the delayed buttons step — means any relayout from a late
    // theme/DPI scale change happens before the reveal, so the already-settled
    // hero title can never jump vertically when the buttons appear.
    if (m_actionsSlot && m_actionsContainer) {
        const QSize actionsSize = m_actionsContainer->sizeHint();
        m_actionsSlot->setFixedSize(actionsSize + QSize(0, kIntroSlideDistance));
        m_actionsContainer->resize(actionsSize);
    }
    if (m_overlayLayout) {
        m_overlayLayout->activate();
    }

    const int stepDelay = kIntroAnimationDurationMs * kIntroNextStartPercent / 100;
    animateIntroProgress(kIntroStartDelayMs, [this](qreal progress) {
        if (m_heroTitle) {
            m_heroTitle->setWelcomeProgress(progress);
        }
    });
    animateIntroProgress(kIntroStartDelayMs + stepDelay, [this](qreal progress) {
        if (m_heroTitle) {
            m_heroTitle->setProductProgress(progress);
        }
    });
    animateIntroProgress(kIntroStartDelayMs + stepDelay * 2, [this](qreal progress) {
        if (m_bodyText) {
            m_bodyText->setRevealProgress(progress);
        }
    });
    animateButtonsIntro(kIntroStartDelayMs + stepDelay * 3);
}

void FirstRunIntegrationWidget::stopIntroAnimations()
{
    ++m_introGeneration;
    for (QVariantAnimation* animation : m_introAnimations) {
        if (!animation) {
            continue;
        }
        animation->stop();
        animation->deleteLater();
    }
    m_introAnimations.clear();
}

void FirstRunIntegrationWidget::resetIntroAnimationState()
{
    if (m_heroTitle) {
        m_heroTitle->setPageOpacity(1.0);
        m_heroTitle->setWelcomeProgress(0.0);
        m_heroTitle->setProductProgress(0.0);
    }

    if (m_bodyText) {
        m_bodyText->setPageOpacity(1.0);
        m_bodyText->setRevealProgress(0.0);
    }

    if (m_actionsOpacityEffect) {
        m_actionsOpacityEffect->setOpacity(0.0);
    }
    if (m_actionsContainer) {
        m_actionsContainer->move(0, kIntroSlideDistance);
    }
}

void FirstRunIntegrationWidget::animateIntroProgress(
    int delayMs, std::function<void(qreal)> applyProgress)
{
    const int generation = m_introGeneration;
    QTimer::singleShot(
        delayMs, this, [this, generation, applyProgress = std::move(applyProgress)]() {
            if (generation != m_introGeneration) {
                return;
            }

            auto* animation = new QVariantAnimation(this);
            m_introAnimations.append(animation);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setDuration(kIntroAnimationDurationMs);
            animation->setEasingCurve(QEasingCurve::OutCubic);

            connect(animation, &QVariantAnimation::valueChanged, this,
                [applyProgress](const QVariant& value) { applyProgress(value.toReal()); });
            connect(animation, &QVariantAnimation::finished, this, [this, animation]() {
                m_introAnimations.removeAll(animation);
                animation->deleteLater();
            });

            applyProgress(0.0);
            animation->start();
        });
}

void FirstRunIntegrationWidget::animateButtonsIntro(int delayMs)
{
    const int generation = m_introGeneration;
    QTimer::singleShot(delayMs, this, [this, generation]() {
        if (generation != m_introGeneration || !m_actionsContainer || !m_actionsOpacityEffect) {
            return;
        }

        // The actions slot was already sized and the layout settled in
        // startIntroAnimations(), so we only drive position/opacity here —
        // no geometry changes that could relayout (and nudge) the hero title.
        const QPoint endPosition(0, kIntroSlideDistance);
        m_actionsFinalPosition = endPosition;
        m_actionsFinalPositionKnown = true;
        const QPoint startPosition(0, 0);
        m_actionsContainer->move(startPosition);
        m_actionsOpacityEffect->setOpacity(0.0);

        auto* animation = new QVariantAnimation(this);
        m_introAnimations.append(animation);
        animation->setStartValue(0.0);
        animation->setEndValue(1.0);
        animation->setDuration(kIntroAnimationDurationMs);
        animation->setEasingCurve(QEasingCurve::OutCubic);

        connect(animation, &QVariantAnimation::valueChanged, this,
            [this, startPosition, endPosition](const QVariant& value) {
                const qreal progress = normalizedProgress(value.toReal());
                if (m_actionsOpacityEffect) {
                    m_actionsOpacityEffect->setOpacity(progress);
                }
                if (m_actionsContainer) {
                    const int y = qRound(
                        startPosition.y() + (endPosition.y() - startPosition.y()) * progress);
                    m_actionsContainer->move(endPosition.x(), y);
                }
            });
        connect(animation, &QVariantAnimation::finished, this, [this, animation, endPosition]() {
            if (m_actionsOpacityEffect) {
                m_actionsOpacityEffect->setOpacity(1.0);
            }
            if (m_actionsContainer) {
                m_actionsContainer->move(endPosition);
            }
            m_introAnimations.removeAll(animation);
            animation->deleteLater();
        });

        animation->start();
    });
}

void FirstRunIntegrationWidget::startSetupSectionTransition()
{
    if (m_pageTransitionRunning || !m_welcomePage || !m_nextPage) {
        return;
    }

    stopIntroAnimations();
    stopPageTransitionAnimations();
    m_pageTransitionRunning = true;
    ++m_pageTransitionGeneration;

    if (m_getStartedButton) {
        m_getStartedButton->setEnabled(false);
    }
    if (m_setupBackButton) {
        m_setupBackButton->setEnabled(false);
    }
    if (m_setupContinueButton) {
        m_setupContinueButton->setEnabled(false);
    }

    m_welcomePageFinalPosition = m_welcomePage->pos();
    m_nextPageFinalPosition = m_welcomePageFinalPosition;
    const QPoint welcomeStart = m_welcomePageFinalPosition;
    const QPoint welcomeEnd = m_welcomePageFinalPosition + QPoint(-kPageTransitionSlideDistance, 0);
    const QPoint nextStart = m_nextPageFinalPosition + QPoint(kPageTransitionSlideDistance, 0);

    m_nextPage->show();
    m_nextPage->raise();
    m_welcomePage->raise();
    m_nextPage->move(nextStart);
    if (m_heroTitle) {
        m_heroTitle->setPageOpacity(1.0);
    }
    if (m_bodyText) {
        m_bodyText->setPageOpacity(1.0);
    }
    if (m_actionsOpacityEffect) {
        m_actionsOpacityEffect->setOpacity(1.0);
    }
    if (m_nextPageOpacityEffect) {
        m_nextPageOpacityEffect->setOpacity(0.0);
    }

    animatePageTransitionProgress(0, [this, welcomeStart, welcomeEnd](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        const qreal opacity = 1.0 - normalized;
        if (m_heroTitle) {
            m_heroTitle->setPageOpacity(opacity);
        }
        if (m_bodyText) {
            m_bodyText->setPageOpacity(opacity);
        }
        if (m_actionsOpacityEffect) {
            m_actionsOpacityEffect->setOpacity(opacity);
        }
        if (m_welcomePage) {
            const int x
                = qRound(welcomeStart.x() + (welcomeEnd.x() - welcomeStart.x()) * normalized);
            m_welcomePage->move(x, welcomeStart.y());
        }
    });

    const int nextPageDelay = kPageTransitionDurationMs * kNextPageStartPercent / 100;
    animatePageTransitionProgress(nextPageDelay, [this, nextStart](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        if (m_nextPageOpacityEffect) {
            m_nextPageOpacityEffect->setOpacity(normalized);
        }
        if (m_nextPage) {
            const int x = qRound(
                nextStart.x() + (m_nextPageFinalPosition.x() - nextStart.x()) * normalized);
            m_nextPage->move(x, m_nextPageFinalPosition.y());
        }

        if (qFuzzyCompare(normalized, 1.0)) {
            if (m_welcomePage) {
                m_welcomePage->hide();
                m_welcomePage->move(m_welcomePageFinalPosition);
            }
            if (m_nextPage) {
                m_nextPage->move(m_nextPageFinalPosition);
                m_nextPage->raise();
            }
            if (m_setupBackButton) {
                m_setupBackButton->setEnabled(true);
            }
            if (m_setupContinueButton) {
                m_setupContinueButton->setEnabled(true);
            }
            m_pageTransitionRunning = false;
        }
    });
}

void FirstRunIntegrationWidget::startWelcomeSectionTransition()
{
    if (m_pageTransitionRunning || !m_welcomePage || !m_nextPage) {
        return;
    }

    stopIntroAnimations();
    stopPageTransitionAnimations();
    m_pageTransitionRunning = true;
    ++m_pageTransitionGeneration;

    if (m_setupBackButton) {
        m_setupBackButton->setEnabled(false);
    }
    if (m_setupContinueButton) {
        m_setupContinueButton->setEnabled(false);
    }

    m_welcomePageFinalPosition = QPoint(0, 0);
    m_nextPageFinalPosition = m_nextPage->pos();
    const QPoint nextStart = m_nextPageFinalPosition;
    const QPoint nextEnd = m_nextPageFinalPosition + QPoint(kPageTransitionSlideDistance, 0);
    const QPoint welcomeStart
        = m_welcomePageFinalPosition + QPoint(-kPageTransitionSlideDistance, 0);

    if (m_heroTitle) {
        m_heroTitle->setWelcomeProgress(1.0);
        m_heroTitle->setProductProgress(1.0);
        m_heroTitle->setPageOpacity(0.0);
    }
    if (m_bodyText) {
        m_bodyText->setRevealProgress(1.0);
        m_bodyText->setPageOpacity(0.0);
    }
    if (m_actionsOpacityEffect) {
        m_actionsOpacityEffect->setOpacity(0.0);
    }
    if (m_actionsContainer) {
        m_actionsContainer->move(0, kIntroSlideDistance);
    }

    m_welcomePage->show();
    m_welcomePage->move(welcomeStart);
    m_welcomePage->raise();
    m_nextPage->raise();
    if (m_nextPageOpacityEffect) {
        m_nextPageOpacityEffect->setOpacity(1.0);
    }

    animatePageTransitionProgress(0, [this, nextStart, nextEnd](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        if (m_nextPageOpacityEffect) {
            m_nextPageOpacityEffect->setOpacity(1.0 - normalized);
        }
        if (m_nextPage) {
            const int x = qRound(nextStart.x() + (nextEnd.x() - nextStart.x()) * normalized);
            m_nextPage->move(x, nextStart.y());
        }
    });

    const int welcomeDelay = kPageTransitionDurationMs * kNextPageStartPercent / 100;
    animatePageTransitionProgress(welcomeDelay, [this, welcomeStart](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        if (m_heroTitle) {
            m_heroTitle->setPageOpacity(normalized);
        }
        if (m_bodyText) {
            m_bodyText->setPageOpacity(normalized);
        }
        if (m_actionsOpacityEffect) {
            m_actionsOpacityEffect->setOpacity(normalized);
        }
        if (m_welcomePage) {
            const int x = qRound(welcomeStart.x()
                + (m_welcomePageFinalPosition.x() - welcomeStart.x()) * normalized);
            m_welcomePage->move(x, m_welcomePageFinalPosition.y());
        }

        if (qFuzzyCompare(normalized, 1.0)) {
            if (m_nextPage) {
                m_nextPage->hide();
                m_nextPage->move(m_nextPageFinalPosition);
            }
            if (m_welcomePage) {
                m_welcomePage->move(m_welcomePageFinalPosition);
                m_welcomePage->raise();
            }
            if (m_getStartedButton) {
                m_getStartedButton->setEnabled(true);
            }
            m_pageTransitionRunning = false;
        }
    });
}

void FirstRunIntegrationWidget::startFinishSectionTransition()
{
    if (m_pageTransitionRunning || !m_nextPage || !m_finishPage) {
        return;
    }

    stopIntroAnimations();
    stopPageTransitionAnimations();
    m_pageTransitionRunning = true;
    ++m_pageTransitionGeneration;

    if (m_setupBackButton) {
        m_setupBackButton->setEnabled(false);
    }
    if (m_setupContinueButton) {
        m_setupContinueButton->setEnabled(false);
    }
    if (m_finishBackButton) {
        m_finishBackButton->setEnabled(false);
    }
    if (m_startCreatingButton) {
        m_startCreatingButton->setEnabled(false);
    }

    m_nextPageFinalPosition = m_nextPage->pos();
    m_finishPageFinalPosition = m_nextPageFinalPosition;
    const QPoint setupStart = m_nextPageFinalPosition;
    const QPoint setupEnd = m_nextPageFinalPosition + QPoint(-kPageTransitionSlideDistance, 0);
    const QPoint finishStart = m_finishPageFinalPosition + QPoint(kPageTransitionSlideDistance, 0);

    m_finishPage->show();
    m_finishPage->raise();
    m_nextPage->raise();
    m_finishPage->move(finishStart);
    if (m_nextPageOpacityEffect) {
        m_nextPageOpacityEffect->setOpacity(1.0);
    }
    if (m_finishPageOpacityEffect) {
        m_finishPageOpacityEffect->setOpacity(0.0);
    }

    animatePageTransitionProgress(0, [this, setupStart, setupEnd](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        if (m_nextPageOpacityEffect) {
            m_nextPageOpacityEffect->setOpacity(1.0 - normalized);
        }
        if (m_nextPage) {
            const int x = qRound(setupStart.x() + (setupEnd.x() - setupStart.x()) * normalized);
            m_nextPage->move(x, setupStart.y());
        }
    });

    const int finishDelay = kPageTransitionDurationMs * kNextPageStartPercent / 100;
    animatePageTransitionProgress(finishDelay, [this, finishStart](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        if (m_finishPageOpacityEffect) {
            m_finishPageOpacityEffect->setOpacity(normalized);
        }
        if (m_finishPage) {
            const int x = qRound(
                finishStart.x() + (m_finishPageFinalPosition.x() - finishStart.x()) * normalized);
            m_finishPage->move(x, m_finishPageFinalPosition.y());
        }

        if (qFuzzyCompare(normalized, 1.0)) {
            if (m_nextPage) {
                m_nextPage->hide();
                m_nextPage->move(m_nextPageFinalPosition);
            }
            if (m_finishPage) {
                m_finishPage->move(m_finishPageFinalPosition);
                m_finishPage->raise();
            }
            if (m_finishBackButton) {
                m_finishBackButton->setEnabled(true);
            }
            if (m_startCreatingButton) {
                m_startCreatingButton->setEnabled(true);
            }
            m_pageTransitionRunning = false;
        }
    });
}

void FirstRunIntegrationWidget::startSetupFromFinishTransition()
{
    if (m_pageTransitionRunning || !m_nextPage || !m_finishPage) {
        return;
    }

    stopIntroAnimations();
    stopPageTransitionAnimations();
    m_pageTransitionRunning = true;
    ++m_pageTransitionGeneration;

    if (m_finishBackButton) {
        m_finishBackButton->setEnabled(false);
    }
    if (m_startCreatingButton) {
        m_startCreatingButton->setEnabled(false);
    }
    if (m_setupBackButton) {
        m_setupBackButton->setEnabled(false);
    }
    if (m_setupContinueButton) {
        m_setupContinueButton->setEnabled(false);
    }

    m_finishPageFinalPosition = m_finishPage->pos();
    m_nextPageFinalPosition = QPoint(0, 0);
    const QPoint finishStart = m_finishPageFinalPosition;
    const QPoint finishEnd = m_finishPageFinalPosition + QPoint(kPageTransitionSlideDistance, 0);
    const QPoint setupStart = m_nextPageFinalPosition + QPoint(-kPageTransitionSlideDistance, 0);

    m_nextPage->show();
    m_nextPage->move(setupStart);
    m_nextPage->raise();
    m_finishPage->raise();
    if (m_nextPageOpacityEffect) {
        m_nextPageOpacityEffect->setOpacity(0.0);
    }
    if (m_finishPageOpacityEffect) {
        m_finishPageOpacityEffect->setOpacity(1.0);
    }

    animatePageTransitionProgress(0, [this, finishStart, finishEnd](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        if (m_finishPageOpacityEffect) {
            m_finishPageOpacityEffect->setOpacity(1.0 - normalized);
        }
        if (m_finishPage) {
            const int x = qRound(finishStart.x() + (finishEnd.x() - finishStart.x()) * normalized);
            m_finishPage->move(x, finishStart.y());
        }
    });

    const int setupDelay = kPageTransitionDurationMs * kNextPageStartPercent / 100;
    animatePageTransitionProgress(setupDelay, [this, setupStart](qreal progress) {
        const qreal normalized = normalizedProgress(progress);
        if (m_nextPageOpacityEffect) {
            m_nextPageOpacityEffect->setOpacity(normalized);
        }
        if (m_nextPage) {
            const int x = qRound(
                setupStart.x() + (m_nextPageFinalPosition.x() - setupStart.x()) * normalized);
            m_nextPage->move(x, m_nextPageFinalPosition.y());
        }

        if (qFuzzyCompare(normalized, 1.0)) {
            if (m_finishPage) {
                m_finishPage->hide();
                m_finishPage->move(m_finishPageFinalPosition);
            }
            if (m_nextPage) {
                m_nextPage->move(m_nextPageFinalPosition);
                m_nextPage->raise();
            }
            if (m_setupBackButton) {
                m_setupBackButton->setEnabled(true);
            }
            if (m_setupContinueButton) {
                m_setupContinueButton->setEnabled(true);
            }
            m_pageTransitionRunning = false;
        }
    });
}

void FirstRunIntegrationWidget::stopPageTransitionAnimations()
{
    ++m_pageTransitionGeneration;
    for (QVariantAnimation* animation : m_pageTransitionAnimations) {
        if (!animation) {
            continue;
        }
        animation->stop();
        animation->deleteLater();
    }
    m_pageTransitionAnimations.clear();
}

void FirstRunIntegrationWidget::animatePageTransitionProgress(
    int delayMs, std::function<void(qreal)> applyProgress)
{
    const int generation = m_pageTransitionGeneration;
    QTimer::singleShot(
        delayMs, this, [this, generation, applyProgress = std::move(applyProgress)]() {
            if (generation != m_pageTransitionGeneration) {
                return;
            }

            auto* animation = new QVariantAnimation(this);
            m_pageTransitionAnimations.append(animation);
            animation->setStartValue(0.0);
            animation->setEndValue(1.0);
            animation->setDuration(kPageTransitionDurationMs);
            animation->setEasingCurve(QEasingCurve::OutCubic);

            connect(animation, &QVariantAnimation::valueChanged, this,
                [applyProgress](const QVariant& value) { applyProgress(value.toReal()); });
            connect(animation, &QVariantAnimation::finished, this, [this, animation]() {
                m_pageTransitionAnimations.removeAll(animation);
                animation->deleteLater();
            });

            applyProgress(0.0);
            animation->start();
        });
}

void FirstRunIntegrationWidget::updateOverlayMargins()
{
    if (!m_overlayLayout) {
        return;
    }

    const int horizontalMargin = kBaseOverlayMargin + m_contentSideMargin;
    m_overlayLayout->setContentsMargins(
        horizontalMargin, kBaseOverlayMargin, horizontalMargin, kBaseOverlayMargin);
    if (m_nextPageLayout) {
        m_nextPageLayout->setContentsMargins(
            horizontalMargin, kBaseOverlayMargin, horizontalMargin, kBaseOverlayMargin);
    }
    if (m_finishPageLayout) {
        m_finishPageLayout->setContentsMargins(
            horizontalMargin, kBaseOverlayMargin, horizontalMargin, kBaseOverlayMargin);
    }
}

} // namespace ruwa::ui::first_run_integration
