// SPDX-License-Identifier: MPL-2.0

#include "TabContextMenu.h"
#include "shared/widgets/BaseStyledWidget.h"
#include "shared/widgets/HorizontalSeparator.h"
#include "shared/style/WidgetStyle.h"
#include "shared/style/PaintingUtils.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shell/top-bar/TopBar.h"

#include <QApplication>
#include <QGridLayout>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {

struct TabActionDef {
    int id;
    const char* text;
    bool danger;
    ruwa::ui::core::IconProvider::StandardIcon icon;
};

static const QStringList kTabIcons
    = { "Home", "BasicFile", "OpenedFolder", "Edit", "NewFile", "Brush", "Eyedropper", "Text",
          "Zoom", "Find", "List", "Settings", "Appearance", "Performance" };

static const TabActionDef kActions[] = {
    { 0, "Close Tab", false, ruwa::ui::core::IconProvider::StandardIcon::Close },
    { 1, "Close Other Tabs", false, ruwa::ui::core::IconProvider::StandardIcon::List },
    { 2, "Close All Tabs", true, ruwa::ui::core::IconProvider::StandardIcon::Trash },
};

} // namespace

TabContextMenu::TabContextMenu(QWidget* parent)
    : StandardContextMenu(parent)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    setContentMargins(theme.scaled(QMargins(6, 6, 6, 6)));
    buildUi();
    applyStyle();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &TabContextMenu::applyStyle);

    updateMenuSize();
}

void TabContextMenu::buildUi()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();

    auto* sectionWrap = new QWidget(contentWidget());
    sectionWrap->setAttribute(Qt::WA_TranslucentBackground);
    auto* sectionLayout = new QVBoxLayout(sectionWrap);
    sectionLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(4));
    sectionLayout->setSpacing(0);

    m_tabTypeSectionLabel = new QLabel(sectionWrap);
    sectionLayout->addWidget(m_tabTypeSectionLabel);
    contentLayout()->addWidget(sectionWrap);

    m_nameInput = new QLineEdit(contentWidget());
    m_nameInput->setPlaceholderText("Tab name...");
    m_nameInput->setFixedHeight(theme.scaled(30));
    contentLayout()->addWidget(m_nameInput);

    m_iconWidget = new QWidget(contentWidget());
    auto* iconGrid = new QGridLayout(m_iconWidget);
    iconGrid->setContentsMargins(0, 4, 0, 4);
    iconGrid->setHorizontalSpacing(theme.scaled(4));
    iconGrid->setVerticalSpacing(theme.scaled(4));
    contentLayout()->addWidget(m_iconWidget);

    for (int i = 0; i < kTabIcons.size(); ++i) {
        const QString alias = kTabIcons[i];
        auto style = ruwa::ui::core::WidgetStyle::defaultButtonStyle();
        style.name = QStringLiteral("TabMenuIconButton");
        style.metrics.fixedWidth = true;
        style.metrics.fixedHeight = true;
        style.metrics.baseWidth = 30;
        style.metrics.baseHeight = 30;
        style.metrics.baseCornerRadius = 4;
        style.background.color = ruwa::ui::core::ColorSource::Transparent;
        style.border.enabled = false;
        style.hover.enabled = true;
        style.hover.color = ruwa::ui::core::ColorSource::OverlayHover;
        style.activeBackground.enabled = true;
        style.activeBackground.color = ruwa::ui::core::ColorSource::Primary;
        style.activeBackground.bottomShadow = false;
        style.activeBorder.enabled = false;
        style.content.iconPosition = ruwa::ui::core::IconPosition::Center;
        style.content.colorizeIcon = true;
        style.content.baseIconSize = IconSize;
        style.press.enabled = false;

        auto* button = new BaseStyledWidget(style, m_iconWidget);
        button->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(alias));
        button->setToolTip(alias);

        const int row = i / IconsPerRow;
        const int col = i % IconsPerRow;
        iconGrid->addWidget(button, row, col);
        m_iconButtons.insert(alias, button);

        connect(button, &BaseStyledWidget::clicked, this,
            [this, alias]() { selectIcon(alias, true, true); });
    }

    m_separator = new HorizontalSeparator(contentWidget());
    m_separator->setMargins(theme.scaled(4), theme.scaled(4));
    contentLayout()->addWidget(m_separator);

    auto* actionsColumn = new QWidget(contentWidget());
    auto* actionsLayout = new QVBoxLayout(actionsColumn);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(theme.scaled(2));
    contentLayout()->addWidget(actionsColumn);

    auto& icons = ruwa::ui::core::IconProvider::instance();
    for (const auto& action : kActions) {
        auto* button = addStandardMenuActionRow(icons.getIcon(action.icon),
            QString::fromLatin1(action.text), action.danger, actionsLayout);
        m_actionButtons.insert(action.id, button);

        connect(button, &BaseStyledWidget::clicked, this,
            [this, action]() { handleActionTriggered(action.id); });
    }

    connect(m_nameInput, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString newName = text.trimmed();
        if (!newName.isEmpty()) {
            m_tabTitle = newName;
            emit tabRenamed(m_tabId, newName);
        }
    });

    connect(m_nameInput, &QLineEdit::returnPressed, this, [this]() { hideAnimated(); });
}

void TabContextMenu::applyStyle()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    if (m_tabTypeSectionLabel) {
        QFont sf = colors.fonts.getUIFont(theme.scaledFontSize(10));
        sf.setWeight(QFont::DemiBold);
        sf.setCapitalization(QFont::AllUppercase);
        sf.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.8));
        m_tabTypeSectionLabel->setFont(sf);
        QPalette sp = m_tabTypeSectionLabel->palette();
        sp.setColor(QPalette::WindowText, colors.textMuted);
        m_tabTypeSectionLabel->setPalette(sp);
    }

    QFont inputFont = theme.colors().fonts.getUIFont(theme.scaledFontSize(9));
    m_nameInput->setFont(inputFont);

    m_nameInput->setStyleSheet(QString("QLineEdit {"
                                       "  background: %1;"
                                       "  border: 1px solid %2;"
                                       "  border-radius: 4px;"
                                       "  color: %3;"
                                       "  padding: 4px 8px;"
                                       "  font-size: %5pt;"
                                       "  selection-background-color: %4;"
                                       "}"
                                       "QLineEdit:focus { border-color: %4; }")
            .arg(colors.overlayBase().name(QColor::HexArgb),
                colors.borderSubtle().name(QColor::HexArgb), colors.text.name(),
                colors.primary.name(), QString::number(theme.scaledFontSize(9))));
}

void TabContextMenu::rebuildStandardMenu()
{
    const QVariantMap ctx = context();

    m_tabId = ctx.value("tabId").toUuid();
    m_tabTitle = ctx.value("tabTitle", "Tab").toString();
    m_currentIconAlias = ctx.value("tabIconAlias", "").toString();

    if (m_tabTypeSectionLabel) {
        const QString kind = ctx.value(QStringLiteral("tabKindLabel")).toString();
        m_tabTypeSectionLabel->setText(kind.isEmpty() ? tr("Tab") : kind);
    }

    m_nameInput->setText(m_tabTitle);
    QTimer::singleShot(0, this, [this]() {
        if (!m_nameInput)
            return;
        m_nameInput->setFocus(Qt::PopupFocusReason);
        m_nameInput->selectAll();
    });

    selectIcon(m_currentIconAlias, false, false);
    updateMenuSize();
}

void TabContextMenu::showEvent(QShowEvent* event)
{
    StandardContextMenu::showEvent(event);

    QTimer::singleShot(0, this, [this]() {
        if (!m_nameInput)
            return;
        activateWindow();
        raise();
        m_nameInput->setFocus(Qt::ActiveWindowFocusReason);
        m_nameInput->selectAll();
    });
}

QPoint TabContextMenu::calculateMenuPosition(
    const QPoint& globalPos, const QSize& menuSize, QWidget* sourceWidget) const
{
    if (!sourceWidget) {
        return StandardContextMenu::calculateMenuPosition(globalPos, menuSize, sourceWidget);
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int shadowSide = theme.scaled(ruwa::ui::painting::kAttachedShadowSideExtentBase);
    const int outerCornerRadius = theme.scaled(ruwa::ui::painting::kAttachedOuterCornerRadiusBase);

    const QRect tabGlobalRect = context().value(QStringLiteral("tabGlobalRect")).toRect();
    const int visibleBodyLeft = tabGlobalRect.isValid()
        ? tabGlobalRect.left()
        : sourceWidget->mapToGlobal(QPoint(0, 0)).x();
    int seamY = sourceWidget->mapToGlobal(QPoint(0, sourceWidget->height())).y() - 1;
    for (QWidget* w = sourceWidget; w; w = w->parentWidget()) {
        if (auto* topBar = qobject_cast<ruwa::ui::widgets::TopBar*>(w)) {
            seamY = topBar->mapToGlobal(QPoint(0, topBar->height())).y() - 1;
            break;
        }
    }

    QPoint pos(visibleBodyLeft - shadowSide - outerCornerRadius, seamY);

    QScreen* screen = QApplication::screenAt(pos);
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    if (screen) {
        const QRect screenRect = screen->availableGeometry();
        if (pos.x() + menuSize.width() > screenRect.right()) {
            pos.setX(screenRect.right() - menuSize.width());
        }
        pos.setX(qMax(pos.x(), screenRect.left()));
        pos.setY(qMax(pos.y(), screenRect.top()));
    }

    return pos;
}

void TabContextMenu::selectIcon(const QString& iconAlias, bool emitChange, bool animateSelection)
{
    if (!m_iconButtons.contains(iconAlias)) {
        for (auto* button : m_iconButtons) {
            button->setActive(false, animateSelection);
        }
        return;
    }

    for (auto it = m_iconButtons.begin(); it != m_iconButtons.end(); ++it) {
        it.value()->setActive(it.key() == iconAlias, animateSelection);
    }

    m_currentIconAlias = iconAlias;
    if (emitChange) {
        emit tabIconChanged(m_tabId, iconAlias);
        emit changeIconRequested(m_tabId);
    }
}

void TabContextMenu::handleActionTriggered(int actionId)
{
    switch (actionId) {
    case 0:
        emit closeTabRequested(m_tabId);
        break;
    case 1:
        emit closeOtherTabsRequested(m_tabId);
        break;
    case 2:
        emit closeAllTabsRequested();
        break;
    default:
        break;
    }

    hideAnimated();
}

} // namespace ruwa::ui::widgets
