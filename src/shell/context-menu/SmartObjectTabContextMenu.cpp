// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   S M A R T   O B J E C T   T A B   C O N T E X T   M E N U
// ======================================================================================

#include "SmartObjectTabContextMenu.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyle.h"
#include "shared/widgets/BaseStyledWidget.h"
#include "shared/widgets/HorizontalSeparator.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QLabel>
#include <QVBoxLayout>

#include <utility>

namespace ruwa::ui::widgets {

SmartObjectTabContextMenu::SmartObjectTabContextMenu(QWidget* parent)
    : StandardContextMenu(parent)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    setContentMargins(theme.scaled(QMargins(6, 6, 6, 6)));
    buildUi();
    applyStyle();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &SmartObjectTabContextMenu::applyStyle);

    updateMenuSize();
}

void SmartObjectTabContextMenu::buildUi()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();

    auto* sectionWrap = new QWidget(contentWidget());
    sectionWrap->setAttribute(Qt::WA_TranslucentBackground);
    auto* sectionLayout = new QVBoxLayout(sectionWrap);
    sectionLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(4));
    sectionLayout->setSpacing(0);

    m_sectionLabel = new QLabel(sectionWrap);
    m_sectionLabel->setText(tr("Smart Objects"));
    sectionLayout->addWidget(m_sectionLabel);
    contentLayout()->addWidget(sectionWrap);

    // Fixed-height list: the menu must not grow with the number of open contents.
    m_listArea = new SmoothScrollArea(contentWidget());
    m_listArea->setFixedHeight(theme.scaled(RowHeight * ListVisibleRows));
    m_listArea->setFillBackground(false);
    m_listArea->setScrollBarTransparentTrack(true);

    m_listContent = new QWidget();
    m_listLayout = new QVBoxLayout(m_listContent);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(theme.scaled(2));
    m_listLayout->addStretch(1);
    m_listArea->setWidget(m_listContent);
    contentLayout()->addWidget(m_listArea);

    m_separator = new HorizontalSeparator(contentWidget());
    m_separator->setMargins(theme.scaled(4), theme.scaled(4));
    contentLayout()->addWidget(m_separator);

    auto* actionsColumn = new QWidget(contentWidget());
    auto* actionsLayout = new QVBoxLayout(actionsColumn);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(theme.scaled(2));
    contentLayout()->addWidget(actionsColumn);

    auto& icons = ruwa::ui::core::IconProvider::instance();
    auto* closeThis
        = addStandardMenuActionRow(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Close),
            tr("Close this smart object"), false, actionsLayout);
    connect(closeThis, &BaseStyledWidget::clicked, this, [this]() {
        const QUuid tabId = m_tabId;
        hideAnimated();
        if (!tabId.isNull()) {
            emit closeSmartObjectRequested(tabId);
        }
    });

    auto* closeAll
        = addStandardMenuActionRow(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Trash),
            tr("Close all smart objects"), true, actionsLayout);
    connect(closeAll, &BaseStyledWidget::clicked, this, [this]() {
        const QUuid parentTabId = m_parentTabId;
        hideAnimated();
        if (!parentTabId.isNull()) {
            emit closeAllSmartObjectsRequested(parentTabId);
        }
    });
}

void SmartObjectTabContextMenu::applyStyle()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (m_sectionLabel) {
        QFont sf = theme.font(ruwa::ui::core::ThemeFontRole::Label, QFont::DemiBold);
        sf.setCapitalization(QFont::AllUppercase);
        sf.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.8));
        m_sectionLabel->setFont(sf);
        QPalette sp = m_sectionLabel->palette();
        sp.setColor(QPalette::WindowText, colors.textMuted);
        m_sectionLabel->setPalette(sp);
    }
}

void SmartObjectTabContextMenu::rebuildStandardMenu()
{
    const QVariantMap ctx = context();

    m_tabId = ctx.value(QStringLiteral("tabId")).toUuid();
    m_parentTabId = ctx.value(QStringLiteral("parentTabId")).toUuid();

    rebuildSmartObjectList();
    updateMenuSize();
}

void SmartObjectTabContextMenu::rebuildSmartObjectList()
{
    if (!m_listLayout) {
        return;
    }

    for (auto* row : std::as_const(m_listRows)) {
        m_listLayout->removeWidget(row);
        row->deleteLater();
    }
    m_listRows.clear();

    const QVariantMap ctx = context();
    const QVariantList ids = ctx.value(QStringLiteral("smartTabIds")).toList();
    const QStringList titles = ctx.value(QStringLiteral("smartTabTitles")).toStringList();

    using namespace ruwa::ui::core;
    for (int i = 0; i < ids.size(); ++i) {
        const QUuid tabId = ids.at(i).toUuid();
        if (tabId.isNull()) {
            continue;
        }
        const QString title = i < titles.size() ? titles.at(i) : QString();

        auto style = WidgetStyle::defaultButtonStyle();
        style.name = QStringLiteral("SmartObjectMenuRow");
        style.metrics.fixedHeight = true;
        style.metrics.fixedWidth = false;
        style.metrics.baseHeight = RowHeight;
        style.metrics.baseCornerRadius = 4;
        style.background.color = ColorSource::Transparent;
        style.border.enabled = false;
        style.hover.enabled = true;
        style.hover.color = ColorSource::OverlayHover;
        // The row of the contents currently shown in the strip reads as selected.
        style.activeBackground.enabled = true;
        style.activeBackground.color = ColorSource::Primary;
        style.activeBackground.bottomShadow = false;
        style.activeBorder.enabled = false;
        style.press.enabled = true;
        style.press.color = ColorSource::OverlayHover;
        style.content.iconPosition = IconPosition::None;
        style.content.textAlignment = ContentAlignment::Left;
        style.content.basePadding = { 10, 0, 10, 0 };
        style.content.textColor = ColorSource::Text;
        style.content.textHoverColor = ColorSource::Text;

        auto* row = new BaseStyledWidget(style, m_listContent);
        row->setText(title);
        row->setToolTip(title);
        row->setActive(tabId == m_tabId, false);

        // Before the stretch that keeps short lists top-aligned.
        m_listLayout->insertWidget(m_listRows.size(), row);
        m_listRows.append(row);

        connect(row, &BaseStyledWidget::clicked, this, [this, tabId]() {
            hideAnimated();
            emit smartObjectActivated(tabId);
        });
    }

    if (m_listArea) {
        m_listArea->refreshScrollGeometry();
        m_listArea->scrollTo(0, false);
    }
}

QPoint SmartObjectTabContextMenu::calculateMenuPosition(
    const QPoint& globalPos, const QSize& menuSize, QWidget* sourceWidget) const
{
    return attachedTopBarMenuPosition(globalPos, menuSize, sourceWidget);
}

QSize SmartObjectTabContextMenu::expandMenuContentHint(const QSize& hint) const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    QSize expanded = StandardContextMenu::expandMenuContentHint(hint);
    expanded.setWidth(qMax(expanded.width(), theme.scaled(220)));
    return expanded;
}

} // namespace ruwa::ui::widgets
