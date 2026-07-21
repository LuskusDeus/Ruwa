// SPDX-License-Identifier: MPL-2.0

#include "features/home/about/AboutContent.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/BaseStyledPanel.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QColor>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSizePolicy>
#include <QShowEvent>
#include <QSpacerItem>
#include <QStringList>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#if __has_include("RuwaBuildInfo.h")
#include "RuwaBuildInfo.h"
#endif

#ifndef RUWA_BUILD_NUMBER
#define RUWA_BUILD_NUMBER "dev"
#endif

#ifndef RUWA_RELEASE_DATE
#define RUWA_RELEASE_DATE "unknown"
#endif

namespace ruwa::ui::widgets {

namespace {

const int BASE_MAIN_MARGIN_H = 40;
const int BASE_MAIN_MARGIN_V = 30;
const int BASE_MAIN_SPACING = 16;
const int BASE_TITLE_FONT_SIZE = 26;
const int BASE_LOGO_PADDING = 24;
const int BASE_INFO_PADDING_TOP = 24;
const int BASE_INFO_PADDING_RIGHT = 24;
const int BASE_INFO_PADDING_BOTTOM = 24;
const int BASE_LOGO_SIZE = 198;
const int BASE_INFO_BLOCK_MIN_WIDTH = 640;
const int BASE_TITLE_SUBTITLE_GAP = 0;
const int BASE_PRODUCT_TITLE_FONT_SIZE = 32;
const int BASE_PRODUCT_SUBTITLE_FONT_SIZE = 12;
const int BASE_LINKS_TOP_SPACING = 22;
const int BASE_LINKS_SPACING = 8;
const int BASE_LINK_BUTTON_HEIGHT = 36;
const int BASE_LINK_BUTTON_MIN_WIDTH = 95;
const int BASE_LINK_BUTTON_RADIUS = 18;
const int BASE_LINK_BUTTON_ICON_SIZE = 18;
const int BASE_DETAILS_TOP_MARGIN = 10;
const int BASE_DETAILS_SPACING = 56;
const int BASE_DETAILS_COLUMN_SPACING = 10;
const int BASE_SECTION_TITLE_FONT_SIZE = 17;
const int BASE_SECTION_SEPARATOR_TOP_MARGIN = 4;
const int BASE_SECTION_CONTENT_TOP_MARGIN = 16;
const int BASE_TOOL_CARD_SPACING = 10;
const int BASE_TOOL_CARD_VSPACING = 10;
const int BASE_TOOL_CARD_PADDING_V = 10;
const int BASE_TOOL_CARD_PADDING_H = 12;
const int BASE_TOOL_CARD_RADIUS = 6;
const int BASE_TOOL_CARD_TITLE_FONT_SIZE = 10;
const int BASE_TOOL_CARD_DESCRIPTION_FONT_SIZE = 9;
const int BASE_CREDITS_TOP_SPACING = 28;
const int BASE_CREDITS_COLUMN_SPACING = 24;
const int BASE_DEVELOPER_AVATAR_SIZE = 72;
const int BASE_DEVELOPER_AVATAR_TEXT_GAP = 14;
const int BASE_DEVELOPER_TEXT_SPACING = 5;
const int BASE_DEVELOPER_NAME_FONT_SIZE = 12;
const int BASE_DEVELOPER_DESCRIPTION_FONT_SIZE = 10;
const int BASE_TESTERS_TITLE_BOTTOM_SPACING = 8;
const int BASE_TESTER_BUTTON_HEIGHT = 28;
const int BASE_TESTER_CARD_SPACING = 8;
const int BASE_TESTER_CARD_VSPACING = 8;
const int BASE_TESTER_CARD_FONT_SIZE = 10;
const int BASE_ACKNOWLEDGEMENTS_TOP_SPACING = 28;
const int BASE_ACKNOWLEDGEMENTS_BODY_FONT_SIZE = 11;
const int BASE_BUILD_INFO_PADDING_H = 18;
const int BASE_BUILD_INFO_PADDING_V = 14;
const int BASE_BUILD_INFO_ROW_SPACING = 8;
const int BASE_BUILD_INFO_SECTION_SPACING = 12;
const int BASE_BUILD_INFO_LABEL_FONT_SIZE = 10;
const int BASE_BUILD_INFO_MIN_WIDTH = 260;

ruwa::ui::core::WidgetStyle createBuildInfoPanelStyle()
{
    using namespace ruwa::ui::core;

    WidgetStyle style = WidgetStyle::settingsPanelStyle();
    style.name = QStringLiteral("AboutBuildInfoPanel");
    style.background.color = ColorSource::OverlayBase;
    style.background.opacity = 1.0;

    return style;
}

QFrame* createBuiltWithCard(QWidget* parent, QLabel*& titleLabel, QLabel*& descriptionLabel)
{
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("BuiltWithCard"));
    card->setFrameShape(QFrame::NoFrame);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    titleLabel = new QLabel(card);
    descriptionLabel = new QLabel(card);
    titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    descriptionLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);

    return card;
}

QPixmap createCircularPixmap(const QString& resourcePath, const QString& fallbackPath, int size)
{
    QPixmap source(resourcePath);
    if (source.isNull()) {
        source.load(fallbackPath);
    }
    if (source.isNull() || size <= 0) {
        return QPixmap();
    }

    const QPixmap scaled
        = source.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    QPixmap result(size, size);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath clipPath;
    clipPath.addEllipse(QRectF(0, 0, size, size));
    painter.setClipPath(clipPath);

    const int x = (scaled.width() - size) / 2;
    const int y = (scaled.height() - size) / 2;
    painter.drawPixmap(0, 0, scaled.copy(x, y, size, size));

    return result;
}

} // namespace

AboutContent::AboutContent(QWidget* parent)
    : HomePageContent(parent)
{
    setupContent();
    updateScaledSizes();
    updateThemeColors();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &AboutContent::onThemeChanged);
}

void AboutContent::setupContent()
{
    m_mainLayout = new QVBoxLayout(this);

    m_headerWidget = new QWidget(this);
    auto* headerLayout = new QHBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(m_headerWidget);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    m_mainLayout->addWidget(m_headerWidget);

    m_aboutPanel = new BaseStyledPanel(ruwa::ui::core::WidgetStyle::settingsPanelStyle(), this);
    m_aboutPanel->setHoverEnabled(false);
    m_aboutPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);

    m_aboutLayout = new QHBoxLayout(m_aboutPanel);
    m_centerBlock = new QWidget(m_aboutPanel);
    m_centerBlock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    m_centerBlockLayout = new QHBoxLayout(m_centerBlock);

    m_logoContainer = new QWidget(m_centerBlock);
    auto* logoLayout = new QVBoxLayout(m_logoContainer);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    logoLayout->setSpacing(0);
    m_logoLabel = new QLabel(m_logoContainer);
    m_logoLabel->setAlignment(Qt::AlignCenter);
    logoLayout->addStretch();
    logoLayout->addWidget(m_logoLabel, 0, Qt::AlignCenter);
    logoLayout->addStretch();

    m_infoContainer = new QWidget(m_centerBlock);
    auto* infoLayout = new QVBoxLayout(m_infoContainer);
    infoLayout->setContentsMargins(0, 0, 0, 0);
    auto* bannerTextGroup = new QWidget(m_infoContainer);
    m_bannerTextLayout = new QVBoxLayout(bannerTextGroup);
    m_bannerTextLayout->setContentsMargins(0, 0, 0, 0);
    m_bannerTextLayout->setSpacing(0);
    m_productTitleLabel = new QLabel(bannerTextGroup);
    m_productSubtitleLabel = new QLabel(bannerTextGroup);
    m_productSubtitleLabel->setWordWrap(true);
    m_bannerTextLayout->addWidget(m_productTitleLabel);
    m_bannerTextLayout->addWidget(m_productSubtitleLabel);

    infoLayout->addStretch();
    infoLayout->addWidget(bannerTextGroup);
    m_bannerLinksSpacer
        = new QSpacerItem(0, BASE_LINKS_TOP_SPACING, QSizePolicy::Minimum, QSizePolicy::Fixed);
    infoLayout->addSpacerItem(m_bannerLinksSpacer);

    m_linksContainer = new QWidget(m_infoContainer);
    m_linksLayout = new QHBoxLayout(m_linksContainer);
    m_linksLayout->setContentsMargins(0, 0, 0, 0);

    m_discordButton
        = new CapsuleButton(QString(), CapsuleButton::Variant::Primary, m_linksContainer);
    m_websiteButton
        = new CapsuleButton(QString(), CapsuleButton::Variant::Primary, m_linksContainer);
    for (CapsuleButton* button : { m_discordButton, m_websiteButton }) {
        button->setBaseMinimumWidth(BASE_LINK_BUTTON_MIN_WIDTH);
        button->setBannerBaseHeight(BASE_LINK_BUTTON_HEIGHT);
        button->setSizeScale(0.88);
    }

    m_discordButton->setCursor(Qt::PointingHandCursor);
    m_websiteButton->setCursor(Qt::PointingHandCursor);

    m_discordButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_websiteButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_linksLayout->addWidget(m_discordButton);
    m_linksLayout->addWidget(m_websiteButton);
    m_linksLayout->addStretch();

    infoLayout->addWidget(m_linksContainer);
    infoLayout->addStretch();

    m_centerBlockLayout->addWidget(m_logoContainer);
    m_centerBlockLayout->addWidget(m_infoContainer);

    m_aboutLayout->addWidget(m_centerBlock, 0, Qt::AlignLeft | Qt::AlignVCenter);
    m_aboutLayout->addStretch();

    connect(m_discordButton, &QPushButton::clicked, this,
        []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://discord.gg/SecuUEhwPd"))); });
    connect(m_websiteButton, &QPushButton::clicked, this,
        []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://accretion.pro"))); });

    m_mainLayout->addWidget(m_aboutPanel);

    m_detailsSection = new QWidget(this);
    m_detailsSection->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_detailsLayout = new QHBoxLayout(m_detailsSection);

    m_programInfoContainer = new QWidget(m_detailsSection);
    auto* programInfoLayout = new QVBoxLayout(m_programInfoContainer);
    programInfoLayout->setContentsMargins(0, 0, 0, 0);
    m_programInfoTitleLabel = new QLabel(m_programInfoContainer);
    m_programInfoTitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_programInfoTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    programInfoLayout->addWidget(m_programInfoTitleLabel);
    programInfoLayout->addSpacing(BASE_SECTION_SEPARATOR_TOP_MARGIN);

    m_builtWithSeparator = new QFrame(m_programInfoContainer);
    m_builtWithSeparator->setObjectName(QStringLiteral("AboutSectionSeparator"));
    m_builtWithSeparator->setFrameShape(QFrame::NoFrame);
    m_builtWithSeparator->setFixedHeight(1);
    m_builtWithSeparator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    programInfoLayout->addWidget(m_builtWithSeparator);
    programInfoLayout->addSpacing(BASE_SECTION_CONTENT_TOP_MARGIN);

    m_toolsCardsContainer = new QWidget(m_programInfoContainer);
    m_toolsCardsLayout = new QGridLayout(m_toolsCardsContainer);
    m_toolsCardsLayout->setContentsMargins(0, 0, 0, 0);
    m_toolsCardsContainer->setLayout(m_toolsCardsLayout);

    constexpr int kToolCardColumns = 3;
    for (int i = 0; i < 9; ++i) {
        QLabel* titleLabel = nullptr;
        QLabel* descriptionLabel = nullptr;
        QFrame* card = createBuiltWithCard(m_toolsCardsContainer, titleLabel, descriptionLabel);
        m_toolCardTitleLabels.append(titleLabel);
        m_toolCardDescriptionLabels.append(descriptionLabel);
        m_toolsCardsLayout->addWidget(card, i / kToolCardColumns, i % kToolCardColumns);
    }

    for (int column = 0; column < kToolCardColumns; ++column) {
        m_toolsCardsLayout->setColumnStretch(column, 1);
    }

    programInfoLayout->addWidget(m_toolsCardsContainer);
    programInfoLayout->addSpacing(BASE_CREDITS_TOP_SPACING);

    m_creditsTitleLabel = new QLabel(m_programInfoContainer);
    m_creditsTitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_creditsTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    programInfoLayout->addWidget(m_creditsTitleLabel);
    programInfoLayout->addSpacing(BASE_SECTION_SEPARATOR_TOP_MARGIN);

    m_creditsSeparator = new QFrame(m_programInfoContainer);
    m_creditsSeparator->setObjectName(QStringLiteral("AboutSectionSeparator"));
    m_creditsSeparator->setFrameShape(QFrame::NoFrame);
    m_creditsSeparator->setFixedHeight(1);
    m_creditsSeparator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    programInfoLayout->addWidget(m_creditsSeparator);
    programInfoLayout->addSpacing(BASE_SECTION_CONTENT_TOP_MARGIN);

    m_creditsContentContainer = new QWidget(m_programInfoContainer);
    auto* creditsLayout = new QHBoxLayout(m_creditsContentContainer);
    creditsLayout->setContentsMargins(0, 0, 0, 0);

    auto* developerSectionContainer = new QWidget(m_creditsContentContainer);
    auto* developerSectionLayout = new QVBoxLayout(developerSectionContainer);
    developerSectionLayout->setContentsMargins(0, 0, 0, 0);
    m_developerSectionTitleLabel = new QLabel(developerSectionContainer);
    m_developerSectionTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    developerSectionLayout->addWidget(m_developerSectionTitleLabel);

    auto* developerContainer = new QWidget(developerSectionContainer);
    auto* developerLayout = new QHBoxLayout(developerContainer);
    developerLayout->setContentsMargins(0, 0, 0, 0);
    m_developerAvatarLabel = new QLabel(developerContainer);
    m_developerAvatarLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    developerLayout->addWidget(m_developerAvatarLabel, 0, Qt::AlignTop);

    auto* developerTextContainer = new QWidget(developerContainer);
    auto* developerTextLayout = new QVBoxLayout(developerTextContainer);
    developerTextLayout->setContentsMargins(0, 0, 0, 0);
    m_developerNameLabel = new QLabel(developerTextContainer);
    m_developerDescriptionLabel = new QLabel(developerTextContainer);
    m_developerDescriptionLabel->setWordWrap(true);
    developerTextLayout->addWidget(m_developerNameLabel);
    developerTextLayout->addWidget(m_developerDescriptionLabel);
    developerTextLayout->addStretch();
    developerLayout->addWidget(developerTextContainer, 1);
    developerSectionLayout->addWidget(developerContainer);

    auto* testersSectionContainer = new QWidget(m_creditsContentContainer);
    auto* testersSectionLayout = new QVBoxLayout(testersSectionContainer);
    testersSectionLayout->setContentsMargins(0, 0, 0, 0);
    m_testersTitleLabel = new QLabel(testersSectionContainer);
    m_testersTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    testersSectionLayout->addWidget(m_testersTitleLabel);

    m_testersCardsContainer = new QWidget(testersSectionContainer);
    m_testersCardsLayout = new QGridLayout(m_testersCardsContainer);
    m_testersCardsLayout->setContentsMargins(0, 0, 0, 0);

    const QStringList testerUrls = { QStringLiteral("https://t.me/moonkaixxxy"),
        QStringLiteral("https://discord.gg/XJXcEXCa8h"),
        QStringLiteral("https://www.twitch.tv/mikko_el"), QStringLiteral("https://t.me/Hipa_aaH"),
        QString(), QStringLiteral("https://steamcommunity.com/profiles/76561199379952595/"),
        QStringLiteral("https://bsky.app/profile/kr0le.bsky.social"), QString(),
        QStringLiteral("https://t.me/Enum_Nektovse") };

    constexpr int kTesterCardColumns = 3;
    for (int i = 0; i < testerUrls.size(); ++i) {
        auto* testerButton
            = new CapsuleButton(QString(), CapsuleButton::Variant::Tab, m_testersCardsContainer);
        testerButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        testerButton->setCursor(testerUrls[i].isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
        testerButton->setAutoExclusive(false);
        if (!testerUrls[i].isEmpty()) {
            const QString testerUrl = testerUrls[i];
            connect(testerButton, &QPushButton::clicked, this, [testerButton, testerUrl]() {
                testerButton->setChecked(true);
                QTimer::singleShot(
                    180, testerButton, [testerButton]() { testerButton->setChecked(false); });
                QDesktopServices::openUrl(QUrl(testerUrl));
            });
        } else {
            testerButton->setCheckable(false);
        }
        m_testerButtons.append(testerButton);
        m_testersCardsLayout->addWidget(
            testerButton, i / kTesterCardColumns, i % kTesterCardColumns);
    }

    for (int column = 0; column < kTesterCardColumns; ++column) {
        m_testersCardsLayout->setColumnStretch(column, 1);
    }

    testersSectionLayout->addWidget(m_testersCardsContainer);

    creditsLayout->addWidget(developerSectionContainer, 3);
    creditsLayout->addWidget(testersSectionContainer, 7);
    programInfoLayout->addWidget(m_creditsContentContainer);
    programInfoLayout->addSpacing(BASE_ACKNOWLEDGEMENTS_TOP_SPACING);

    m_acknowledgementsTitleLabel = new QLabel(m_programInfoContainer);
    m_acknowledgementsTitleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_acknowledgementsTitleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    programInfoLayout->addWidget(m_acknowledgementsTitleLabel);
    programInfoLayout->addSpacing(BASE_SECTION_SEPARATOR_TOP_MARGIN);

    m_acknowledgementsSeparator = new QFrame(m_programInfoContainer);
    m_acknowledgementsSeparator->setObjectName(QStringLiteral("AboutSectionSeparator"));
    m_acknowledgementsSeparator->setFrameShape(QFrame::NoFrame);
    m_acknowledgementsSeparator->setFixedHeight(1);
    m_acknowledgementsSeparator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    programInfoLayout->addWidget(m_acknowledgementsSeparator);
    programInfoLayout->addSpacing(BASE_SECTION_CONTENT_TOP_MARGIN);

    m_acknowledgementsBodyLabel = new QLabel(m_programInfoContainer);
    m_acknowledgementsBodyLabel->setWordWrap(true);
    m_acknowledgementsBodyLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    programInfoLayout->addWidget(m_acknowledgementsBodyLabel);

    m_buildInfoPanel = new BaseStyledPanel(createBuildInfoPanelStyle(), m_detailsSection);
    m_buildInfoPanel->setHoverEnabled(false);
    m_buildInfoPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_buildInfoLayout = new QVBoxLayout(m_buildInfoPanel);

    const auto addSeparator = [this]() {
        auto* separator = new QFrame(m_buildInfoPanel);
        separator->setObjectName(QStringLiteral("BuildInfoSeparator"));
        separator->setFrameShape(QFrame::NoFrame);
        separator->setFixedHeight(1);
        separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_buildInfoLayout->addWidget(separator);
    };

    const auto addInfoRow = [this](QLabel*& captionLabel, QLabel*& valueLabel) {
        auto* row = new QWidget(m_buildInfoPanel);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(8);

        captionLabel = new QLabel(row);
        valueLabel = new QLabel(row);
        captionLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        rowLayout->addWidget(captionLabel);
        rowLayout->addStretch();
        rowLayout->addWidget(valueLabel);
        m_buildInfoLayout->addWidget(row);
    };

    m_buildDetailsTitleLabel = new QLabel(m_buildInfoPanel);
    m_buildInfoLayout->addWidget(m_buildDetailsTitleLabel);
    addSeparator();
    addInfoRow(m_versionCaptionLabel, m_versionValueLabel);
    addSeparator();
    addInfoRow(m_buildCaptionLabel, m_buildValueLabel);
    addSeparator();
    addInfoRow(m_releaseCaptionLabel, m_releaseValueLabel);

    m_buildInfoLayout->addSpacing(BASE_BUILD_INFO_SECTION_SPACING);

    m_environmentTitleLabel = new QLabel(m_buildInfoPanel);
    m_buildInfoLayout->addWidget(m_environmentTitleLabel);
    addSeparator();
    addInfoRow(m_platformCaptionLabel, m_platformValueLabel);
    addSeparator();
    addInfoRow(m_localeCaptionLabel, m_localeValueLabel);

    m_detailsLayout->addWidget(m_programInfoContainer, 3);
    m_detailsLayout->addWidget(m_buildInfoPanel, 2, Qt::AlignTop);

    m_detailsScrollArea = new SmoothScrollArea(this);
    m_detailsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_detailsScrollArea->setScrollBarMargin(4);
    m_detailsScrollArea->setFillBackground(false);

    auto* scrollContent = new QWidget();
    scrollContent->setAttribute(Qt::WA_TranslucentBackground);
    scrollContent->setAutoFillBackground(false);
    auto* scrollContentLayout = new QVBoxLayout(scrollContent);
    scrollContentLayout->setContentsMargins(0, 0, 0, 0);
    scrollContentLayout->setSpacing(0);
    scrollContentLayout->addWidget(m_detailsSection);
    // SmoothScrollArea keeps content at least viewport-high, so a trailing stretch
    // absorbs spare height instead of stretching the details section.
    scrollContentLayout->addStretch();

    m_detailsScrollArea->setWidget(scrollContent);
    m_mainLayout->addWidget(m_detailsScrollArea, 1);

    retranslateUi();
}

void AboutContent::changeEvent(QEvent* event)
{
    HomePageContent::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void AboutContent::showEvent(QShowEvent* event)
{
    HomePageContent::showEvent(event);

    // The details live inside a SmoothScrollArea whose viewport geometry is only final
    // once this page (a hidden QStackedWidget page at construction time) is actually shown.
    // If the scaled metrics / scroll range were computed while hidden, the content gets
    // squeezed to fit the viewport instead of scrolling, with a stale font/layout pass.
    // Re-apply once shown (deferred so the viewport has settled) — this is what a UI
    // re-scale does, and it un-squeezes the section and restores the correct fonts.
    QTimer::singleShot(0, this, [this]() {
        updateScaledSizes();
        if (m_detailsScrollArea) {
            m_detailsScrollArea->refreshScrollGeometry();
        }
    });
}

void AboutContent::retranslateUi()
{
    const QString version = QCoreApplication::applicationVersion();

    m_titleLabel->setText(tr("About"));
    m_productTitleLabel->setText(
        version.isEmpty() ? QStringLiteral("Ruwa") : QStringLiteral("Ruwa %1").arg(version));
    m_productSubtitleLabel->setText(
        tr("Non-destructive raster graphics editor with an infinite canvas."));
    m_discordButton->setText(tr("Discord"));
    m_websiteButton->setText(tr("Website"));
    m_discordButton->syncSizeToText();
    m_websiteButton->syncSizeToText();
    m_programInfoTitleLabel->setText(tr("Built with"));

    const QStringList toolNames = { QStringLiteral("C++23"), QStringLiteral("Qt 6"),
        QStringLiteral("Qt Widgets"), QStringLiteral("OpenGL"), QStringLiteral("QWindowKit"),
        QStringLiteral("Discord RPC"), QStringLiteral("Qt Concurrent"),
        QStringLiteral("Qt Network"), QStringLiteral("CMake") };
    const QStringList toolDescriptions
        = { tr("Core application logic"), tr("Application framework"), tr("Interface widgets"),
              tr("Rendering"), tr("Window integration"), tr("Discord integration"),
              tr("Background tasks"), tr("Network features"), tr("Build system") };

    const int toolCount
        = qMin(static_cast<int>(m_toolCardTitleLabels.size()), static_cast<int>(toolNames.size()));
    for (int i = 0; i < toolCount; ++i) {
        m_toolCardTitleLabels[i]->setText(toolNames[i]);
        m_toolCardDescriptionLabels[i]->setText(toolDescriptions[i]);
    }

    m_creditsTitleLabel->setText(tr("Credits"));
    m_developerSectionTitleLabel->setText(tr("Developer"));
    m_developerNameLabel->setText(QStringLiteral("Luskus Deus"));
    m_developerDescriptionLabel->setText(
        tr("Solo developer responsible for UI, rendering, tools, and core systems."));
    m_testersTitleLabel->setText(tr("Testers"));

    const QStringList testerNames = { QStringLiteral("kaixxxy"), QStringLiteral("Lozar"),
        QStringLiteral("Mikko_el"), QStringLiteral("HipaaaH!~"), QStringLiteral("Ayami"),
        QStringLiteral("Dgan"), QStringLiteral("KrOl"), QStringLiteral("KAMENOV PLUS"),
        QStringLiteral("Enum Nektovse") };

    const int testerCount
        = qMin(static_cast<int>(m_testerButtons.size()), static_cast<int>(testerNames.size()));
    for (int i = 0; i < testerCount; ++i) {
        m_testerButtons[i]->setText(testerNames[i]);
    }

    m_acknowledgementsTitleLabel->setText(tr("Acknowledgements"));
    m_acknowledgementsBodyLabel->setText(tr(
        "I built Ruwa with a strong focus on aesthetics, responsiveness, and overall feel. "
        "Almost everything in the app is made from scratch, from custom message popups to a fully "
        "custom brush engine. "
        "Ruwa already has an infinite canvas and its own widget system, and in the future it is "
        "planned to grow a custom "
        "effects pipeline designed to stand alongside professional photo editors.\n\n"
        "Ruwa uses the Qt framework (version %1), dynamically linked under the GNU Lesser General "
        "Public License v3 "
        "(LGPL-3.0). The LGPL-3.0 and GPL-3.0 license texts are included with Ruwa. Qt is a "
        "trademark of The Qt Company Ltd.")
            .arg(QString::fromLatin1(qVersion())));

    m_buildDetailsTitleLabel->setText(tr("Build Details"));
    m_versionCaptionLabel->setText(tr("Version"));
    m_versionValueLabel->setText(version.isEmpty() ? QStringLiteral("-") : version);
    m_buildCaptionLabel->setText(tr("Build"));
    m_buildValueLabel->setText(QString::fromUtf8(RUWA_BUILD_NUMBER));
    m_releaseCaptionLabel->setText(tr("Released"));
    m_releaseValueLabel->setText(QString::fromUtf8(RUWA_RELEASE_DATE));

    QString platformName = QSysInfo::prettyProductName();
    if (platformName.isEmpty()) {
        platformName = QStringLiteral("%1 %2")
                           .arg(QSysInfo::productType(), QSysInfo::productVersion())
                           .trimmed();
    }

    const QLocale systemLocale = QLocale::system();
    m_environmentTitleLabel->setText(tr("Environment"));
    m_platformCaptionLabel->setText(tr("Platform"));
    m_platformValueLabel->setText(platformName.isEmpty() ? QStringLiteral("-") : platformName);
    m_localeCaptionLabel->setText(tr("Locale"));
    m_localeValueLabel->setText(systemLocale.name());
}

void AboutContent::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    m_mainLayout->setContentsMargins(theme.scaled(BASE_MAIN_MARGIN_H),
        theme.scaled(BASE_MAIN_MARGIN_V), theme.scaled(BASE_MAIN_MARGIN_H),
        theme.scaled(BASE_MAIN_MARGIN_V));
    m_mainLayout->setSpacing(theme.scaled(BASE_MAIN_SPACING));

    if (m_aboutLayout) {
        m_aboutLayout->setContentsMargins(0, 0, 0, 0);
        m_aboutLayout->setSpacing(0);
    }

    if (m_centerBlockLayout) {
        m_centerBlockLayout->setContentsMargins(0, 0, 0, 0);
        m_centerBlockLayout->setSpacing(0);
    }

    if (auto* logoLayout = qobject_cast<QVBoxLayout*>(m_logoContainer->layout())) {
        const int logoPadding = theme.scaled(BASE_LOGO_PADDING);
        logoLayout->setContentsMargins(logoPadding, logoPadding, logoPadding, logoPadding);
    }

    if (auto* infoLayout = qobject_cast<QVBoxLayout*>(m_infoContainer->layout())) {
        infoLayout->setContentsMargins(0, theme.scaled(BASE_INFO_PADDING_TOP),
            theme.scaled(BASE_INFO_PADDING_RIGHT), theme.scaled(BASE_INFO_PADDING_BOTTOM));
        infoLayout->setSpacing(0);
    }

    if (m_bannerTextLayout) {
        m_bannerTextLayout->setSpacing(theme.scaled(BASE_TITLE_SUBTITLE_GAP));
    }

    if (m_bannerLinksSpacer) {
        m_bannerLinksSpacer->changeSize(
            0, theme.scaled(BASE_LINKS_TOP_SPACING), QSizePolicy::Minimum, QSizePolicy::Fixed);
        if (auto* infoLayout = qobject_cast<QVBoxLayout*>(m_infoContainer->layout())) {
            infoLayout->invalidate();
        }
    }

    if (m_linksLayout) {
        m_linksLayout->setSpacing(theme.scaled(BASE_LINKS_SPACING));
    }

    if (m_detailsLayout) {
        m_detailsLayout->setContentsMargins(0, theme.scaled(BASE_DETAILS_TOP_MARGIN), 0, 0);
        m_detailsLayout->setSpacing(theme.scaled(BASE_DETAILS_SPACING));
    }

    m_infoContainer->setMinimumWidth(theme.scaled(BASE_INFO_BLOCK_MIN_WIDTH));

    const int logoSize = theme.scaled(BASE_LOGO_SIZE);
    m_logoLabel->setFixedSize(logoSize, logoSize);
    m_logoLabel->setPixmap(ruwa::ui::core::IconProvider::instance().getApplicationLogoPixmap(
        QSize(logoSize, logoSize)));

    m_titleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE)));
    m_productTitleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_PRODUCT_TITLE_FONT_SIZE)));
    m_productSubtitleLabel->setFont(
        theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_PRODUCT_SUBTITLE_FONT_SIZE)));
    m_programInfoTitleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_SECTION_TITLE_FONT_SIZE)));

    if (auto* programInfoLayout = qobject_cast<QVBoxLayout*>(m_programInfoContainer->layout())) {
        programInfoLayout->setSpacing(0);
    }

    if (m_toolsCardsLayout) {
        m_toolsCardsLayout->setHorizontalSpacing(theme.scaled(BASE_TOOL_CARD_SPACING));
        m_toolsCardsLayout->setVerticalSpacing(theme.scaled(BASE_TOOL_CARD_VSPACING));
    }

    QFont toolCardTitleFont
        = theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_TOOL_CARD_TITLE_FONT_SIZE));
    toolCardTitleFont.setBold(true);
    const QFont toolCardDescriptionFont = theme.colors().fonts.getUIFont(
        theme.scaledFontSize(BASE_TOOL_CARD_DESCRIPTION_FONT_SIZE));

    for (int i = 0; i < m_toolCardTitleLabels.size(); ++i) {
        m_toolCardTitleLabels[i]->setFont(toolCardTitleFont);
        m_toolCardDescriptionLabels[i]->setFont(toolCardDescriptionFont);

        if (auto* card = qobject_cast<QFrame*>(m_toolCardTitleLabels[i]->parentWidget())) {
            if (auto* cardLayout = qobject_cast<QVBoxLayout*>(card->layout())) {
                cardLayout->setContentsMargins(theme.scaled(BASE_TOOL_CARD_PADDING_H),
                    theme.scaled(BASE_TOOL_CARD_PADDING_V), theme.scaled(BASE_TOOL_CARD_PADDING_H),
                    theme.scaled(BASE_TOOL_CARD_PADDING_V));
            }
        }
    }

    m_creditsTitleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_SECTION_TITLE_FONT_SIZE)));

    if (auto* creditsLayout = qobject_cast<QHBoxLayout*>(m_creditsContentContainer->layout())) {
        creditsLayout->setSpacing(theme.scaled(BASE_CREDITS_COLUMN_SPACING));
    }

    if (auto* developerSectionLayout
        = qobject_cast<QVBoxLayout*>(m_developerSectionTitleLabel->parentWidget()->layout())) {
        developerSectionLayout->setSpacing(theme.scaled(BASE_TESTERS_TITLE_BOTTOM_SPACING));
    }
    if (auto* developerLayout
        = qobject_cast<QHBoxLayout*>(m_developerAvatarLabel->parentWidget()->layout())) {
        developerLayout->setSpacing(theme.scaled(BASE_DEVELOPER_AVATAR_TEXT_GAP));
    }
    if (auto* developerTextLayout
        = qobject_cast<QVBoxLayout*>(m_developerNameLabel->parentWidget()->layout())) {
        developerTextLayout->setSpacing(theme.scaled(BASE_DEVELOPER_TEXT_SPACING));
    }
    if (auto* testersSectionLayout
        = qobject_cast<QVBoxLayout*>(m_testersTitleLabel->parentWidget()->layout())) {
        testersSectionLayout->setSpacing(theme.scaled(BASE_TESTERS_TITLE_BOTTOM_SPACING));
    }

    const int avatarSize = theme.scaled(BASE_DEVELOPER_AVATAR_SIZE);
    m_developerAvatarLabel->setFixedSize(avatarSize, avatarSize);
    m_developerAvatarLabel->setPixmap(
        createCircularPixmap(QStringLiteral(":/icons/MyPFP"), QString(), avatarSize));

    QFont developerNameFont
        = theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_DEVELOPER_NAME_FONT_SIZE));
    developerNameFont.setBold(true);
    m_developerNameLabel->setFont(developerNameFont);
    m_developerDescriptionLabel->setFont(
        theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_DEVELOPER_DESCRIPTION_FONT_SIZE)));
    QFont testersTitleFont
        = theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_DEVELOPER_NAME_FONT_SIZE));
    testersTitleFont.setBold(true);
    m_developerSectionTitleLabel->setFont(testersTitleFont);
    m_testersTitleLabel->setFont(testersTitleFont);

    if (m_testersCardsLayout) {
        m_testersCardsLayout->setHorizontalSpacing(theme.scaled(BASE_TESTER_CARD_SPACING));
        m_testersCardsLayout->setVerticalSpacing(theme.scaled(BASE_TESTER_CARD_VSPACING));
    }

    const QFont testerCardFont
        = theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_TESTER_CARD_FONT_SIZE));
    for (CapsuleButton* testerButton : m_testerButtons) {
        testerButton->setFont(testerCardFont);
        testerButton->setFixedHeight(theme.scaled(BASE_TESTER_BUTTON_HEIGHT));
    }

    m_acknowledgementsTitleLabel->setFont(
        theme.colors().fonts.getTitleFont(theme.scaledFontSize(BASE_SECTION_TITLE_FONT_SIZE)));
    m_acknowledgementsBodyLabel->setFont(
        theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_ACKNOWLEDGEMENTS_BODY_FONT_SIZE)));

    if (m_buildInfoLayout) {
        m_buildInfoLayout->setContentsMargins(theme.scaled(BASE_BUILD_INFO_PADDING_H),
            theme.scaled(BASE_BUILD_INFO_PADDING_V), theme.scaled(BASE_BUILD_INFO_PADDING_H),
            theme.scaled(BASE_BUILD_INFO_PADDING_V));
        m_buildInfoLayout->setSpacing(theme.scaled(BASE_BUILD_INFO_ROW_SPACING));
    }

    m_buildInfoPanel->setMinimumWidth(theme.scaled(BASE_BUILD_INFO_MIN_WIDTH));

    QFont buildInfoFont
        = theme.colors().fonts.getUIFont(theme.scaledFontSize(BASE_BUILD_INFO_LABEL_FONT_SIZE));
    QFont buildInfoSectionFont = buildInfoFont;
    buildInfoSectionFont.setBold(true);

    const auto applyBuildInfoFont = [&](QLabel* label) { label->setFont(buildInfoFont); };

    m_buildDetailsTitleLabel->setFont(buildInfoSectionFont);
    applyBuildInfoFont(m_versionCaptionLabel);
    applyBuildInfoFont(m_versionValueLabel);
    applyBuildInfoFont(m_buildCaptionLabel);
    applyBuildInfoFont(m_buildValueLabel);
    applyBuildInfoFont(m_releaseCaptionLabel);
    applyBuildInfoFont(m_releaseValueLabel);
    m_environmentTitleLabel->setFont(buildInfoSectionFont);
    applyBuildInfoFont(m_platformCaptionLabel);
    applyBuildInfoFont(m_platformValueLabel);
    applyBuildInfoFont(m_localeCaptionLabel);
    applyBuildInfoFont(m_localeValueLabel);

    const int sectionSpacing = theme.scaled(BASE_BUILD_INFO_SECTION_SPACING);
    for (int i = 0; i < m_buildInfoLayout->count(); ++i) {
        QLayoutItem* item = m_buildInfoLayout->itemAt(i);
        if (QSpacerItem* spacer = item ? item->spacerItem() : nullptr) {
            spacer->changeSize(0, sectionSpacing, QSizePolicy::Minimum, QSizePolicy::Fixed);
        }
    }
    m_buildInfoLayout->invalidate();

    const QFont linkFont = theme.colors().fonts.getUIFont(theme.scaledFontSize(11));
    const int linkHeight = theme.scaled(BASE_LINK_BUTTON_HEIGHT);
    const int linkMinWidth = theme.scaled(BASE_LINK_BUTTON_MIN_WIDTH);
    const QSize linkIconSize(
        theme.scaled(BASE_LINK_BUTTON_ICON_SIZE), theme.scaled(BASE_LINK_BUTTON_ICON_SIZE));
    const auto applyLinkButtonMetrics = [&](CapsuleButton* button) {
        button->setFont(linkFont);
        button->setBannerBaseHeight(BASE_LINK_BUTTON_HEIGHT);
        button->setBaseMinimumWidth(BASE_LINK_BUTTON_MIN_WIDTH);
        button->setIconSize(linkIconSize);
        button->syncSizeToText();
        button->setMinimumWidth(linkMinWidth);
        button->setFixedHeight(linkHeight);
    };

    applyLinkButtonMetrics(m_discordButton);
    applyLinkButtonMetrics(m_websiteButton);
}

void AboutContent::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    m_titleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; }").arg(colors.text.name(QColor::HexArgb)));
    m_productTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_productSubtitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));
    m_programInfoTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));

    m_builtWithSeparator->setStyleSheet(
        QStringLiteral("QFrame#AboutSectionSeparator { background: %1; border: none; }")
            .arg(colors.borderSubtle().name(QColor::HexArgb)));

    const QString toolCardTitleSheet
        = QStringLiteral("QLabel { color: %1; background: transparent; }")
              .arg(colors.text.name(QColor::HexArgb));
    const QString toolCardDescriptionSheet
        = QStringLiteral("QLabel { color: %1; background: transparent; }")
              .arg(colors.textMuted.name(QColor::HexArgb));
    const QString toolCardSheet
        = QStringLiteral("QFrame#BuiltWithCard {"
                         " background: %1;"
                         " border: 1px solid %2;"
                         " border-radius: %3px;"
                         "}")
              .arg(colors.overlayBase().name(QColor::HexArgb))
              .arg(colors.borderSubtle().name(QColor::HexArgb))
              .arg(ruwa::ui::core::ThemeManager::instance().scaled(BASE_TOOL_CARD_RADIUS));

    for (int i = 0; i < m_toolCardTitleLabels.size(); ++i) {
        m_toolCardTitleLabels[i]->setStyleSheet(toolCardTitleSheet);
        m_toolCardDescriptionLabels[i]->setStyleSheet(toolCardDescriptionSheet);
        if (auto* card = qobject_cast<QFrame*>(m_toolCardTitleLabels[i]->parentWidget())) {
            card->setStyleSheet(toolCardSheet);
        }
    }

    m_creditsTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_creditsSeparator->setStyleSheet(
        QStringLiteral("QFrame#AboutSectionSeparator { background: %1; border: none; }")
            .arg(colors.borderSubtle().name(QColor::HexArgb)));
    m_developerSectionTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_developerNameLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_developerDescriptionLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));
    m_testersTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_acknowledgementsTitleLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_acknowledgementsSeparator->setStyleSheet(
        QStringLiteral("QFrame#AboutSectionSeparator { background: %1; border: none; }")
            .arg(colors.borderSubtle().name(QColor::HexArgb)));
    m_acknowledgementsBodyLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));

    const QString buildInfoTitleSheet
        = QStringLiteral("QLabel { color: %1; background: transparent; }")
              .arg(colors.text.name(QColor::HexArgb));
    const QString buildInfoCaptionSheet
        = QStringLiteral("QLabel { color: %1; background: transparent; }")
              .arg(colors.textMuted.name(QColor::HexArgb));
    const QString buildInfoValueSheet
        = QStringLiteral("QLabel { color: %1; background: transparent; }")
              .arg(colors.text.name(QColor::HexArgb));

    m_buildDetailsTitleLabel->setStyleSheet(buildInfoTitleSheet);
    m_environmentTitleLabel->setStyleSheet(buildInfoTitleSheet);
    m_versionCaptionLabel->setStyleSheet(buildInfoCaptionSheet);
    m_buildCaptionLabel->setStyleSheet(buildInfoCaptionSheet);
    m_releaseCaptionLabel->setStyleSheet(buildInfoCaptionSheet);
    m_platformCaptionLabel->setStyleSheet(buildInfoCaptionSheet);
    m_localeCaptionLabel->setStyleSheet(buildInfoCaptionSheet);
    m_versionValueLabel->setStyleSheet(buildInfoValueSheet);
    m_buildValueLabel->setStyleSheet(buildInfoValueSheet);
    m_releaseValueLabel->setStyleSheet(buildInfoValueSheet);
    m_platformValueLabel->setStyleSheet(buildInfoValueSheet);
    m_localeValueLabel->setStyleSheet(buildInfoValueSheet);

    const QString separatorSheet
        = QStringLiteral("QFrame#BuildInfoSeparator { background: %1; border: none; }")
              .arg(colors.borderSubtle().name(QColor::HexArgb));
    const auto separators
        = m_buildInfoPanel->findChildren<QFrame*>(QStringLiteral("BuildInfoSeparator"));
    for (QFrame* separator : separators) {
        separator->setStyleSheet(separatorSheet);
    }

    m_discordButton->setIcon(ruwa::ui::core::IconProvider::instance().getColoredIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Discord, colors.textOnPrimary()));
    m_websiteButton->setIcon(ruwa::ui::core::IconProvider::instance().getColoredIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Home, colors.textOnPrimary()));
    m_discordButton->syncSizeToText();
    m_websiteButton->syncSizeToText();
}

void AboutContent::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::widgets
