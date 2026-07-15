// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_ABOUT_ABOUTCONTENT_H
#define RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_ABOUT_ABOUTCONTENT_H

#include "features/home/HomePageContent.h"

#include <QVector>

class QEvent;
class QShowEvent;
class QFrame;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QSpacerItem;
class QVBoxLayout;
class QWidget;

namespace ruwa::ui::widgets {

class BaseStyledPanel;
class CapsuleButton;
class SmoothScrollArea;

class AboutContent : public HomePageContent {
    Q_OBJECT

public:
    explicit AboutContent(QWidget* parent = nullptr);
    ~AboutContent() override = default;

    QString title() const override { return tr("About"); }

protected:
    void setupContent() override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void retranslateUi();
    void updateScaledSizes();
    void updateThemeColors();

private slots:
    void onThemeChanged();

private:
    QVBoxLayout* m_mainLayout { nullptr };
    QWidget* m_headerWidget { nullptr };
    QLabel* m_titleLabel { nullptr };
    BaseStyledPanel* m_aboutPanel { nullptr };
    QHBoxLayout* m_aboutLayout { nullptr };
    SmoothScrollArea* m_detailsScrollArea { nullptr };
    QWidget* m_detailsSection { nullptr };
    QHBoxLayout* m_detailsLayout { nullptr };
    QWidget* m_centerBlock { nullptr };
    QHBoxLayout* m_centerBlockLayout { nullptr };
    QWidget* m_logoContainer { nullptr };
    QLabel* m_logoLabel { nullptr };
    QWidget* m_infoContainer { nullptr };
    QVBoxLayout* m_bannerTextLayout { nullptr };
    QSpacerItem* m_bannerLinksSpacer { nullptr };
    QLabel* m_productTitleLabel { nullptr };
    QLabel* m_productSubtitleLabel { nullptr };
    QWidget* m_linksContainer { nullptr };
    QHBoxLayout* m_linksLayout { nullptr };
    CapsuleButton* m_discordButton { nullptr };
    CapsuleButton* m_websiteButton { nullptr };
    QWidget* m_programInfoContainer { nullptr };
    QLabel* m_programInfoTitleLabel { nullptr };
    QFrame* m_builtWithSeparator { nullptr };
    QWidget* m_toolsCardsContainer { nullptr };
    QGridLayout* m_toolsCardsLayout { nullptr };
    QVector<QLabel*> m_toolCardTitleLabels;
    QVector<QLabel*> m_toolCardDescriptionLabels;
    QLabel* m_creditsTitleLabel { nullptr };
    QFrame* m_creditsSeparator { nullptr };
    QWidget* m_creditsContentContainer { nullptr };
    QLabel* m_developerSectionTitleLabel { nullptr };
    QLabel* m_developerAvatarLabel { nullptr };
    QLabel* m_developerNameLabel { nullptr };
    QLabel* m_developerDescriptionLabel { nullptr };
    QLabel* m_testersTitleLabel { nullptr };
    QWidget* m_testersCardsContainer { nullptr };
    QGridLayout* m_testersCardsLayout { nullptr };
    QVector<CapsuleButton*> m_testerButtons;
    QLabel* m_acknowledgementsTitleLabel { nullptr };
    QFrame* m_acknowledgementsSeparator { nullptr };
    QLabel* m_acknowledgementsBodyLabel { nullptr };
    BaseStyledPanel* m_buildInfoPanel { nullptr };
    QVBoxLayout* m_buildInfoLayout { nullptr };
    QLabel* m_buildDetailsTitleLabel { nullptr };
    QLabel* m_versionCaptionLabel { nullptr };
    QLabel* m_versionValueLabel { nullptr };
    QLabel* m_buildCaptionLabel { nullptr };
    QLabel* m_buildValueLabel { nullptr };
    QLabel* m_releaseCaptionLabel { nullptr };
    QLabel* m_releaseValueLabel { nullptr };
    QLabel* m_environmentTitleLabel { nullptr };
    QLabel* m_platformCaptionLabel { nullptr };
    QLabel* m_platformValueLabel { nullptr };
    QLabel* m_localeCaptionLabel { nullptr };
    QLabel* m_localeValueLabel { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_HOMEPAGE_CONTENT_ABOUT_ABOUTCONTENT_H
