// SPDX-License-Identifier: MPL-2.0

#include "DockPanelContextMenu.h"

#include "shell/docking/widgets/DockPanel.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/BaseStyledWidget.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/HorizontalSeparator.h"
#include "features/theme/manager/ThemeManager.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {

class BehaviorToggleRow final : public QWidget {
public:
    explicit BehaviorToggleRow(QWidget* parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("dockCtxToggleRow"));
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
    }

    void setToggleTarget(ToggleSwitch* toggle) { m_toggle = toggle; }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !m_toggle || !m_toggle->isEnabled()) {
            QWidget::mousePressEvent(event);
            return;
        }
        m_toggle->toggle();
    }

private:
    ToggleSwitch* m_toggle = nullptr;
};

} // namespace

DockPanelContextMenu::DockPanelContextMenu(QWidget* parent)
    : StandardContextMenu(parent)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    setContentMargins(theme.scaled(QMargins(6, 6, 6, 6)));
    // Same as StandardContextMenu / TabContextMenu outer column (not theme.scaled — matches base
    // ctor).
    contentLayout()->setSpacing(4);

    buildUi();
    applyChrome();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &DockPanelContextMenu::applyChrome);

    updateMenuSize();
}

void DockPanelContextMenu::applyChrome()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (m_sectionLabel) {
        QFont f = colors.fonts.getUIFont(theme.scaledFontSize(10));
        f.setWeight(QFont::DemiBold);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.8));
        m_sectionLabel->setFont(f);
        QPalette pal = m_sectionLabel->palette();
        pal.setColor(QPalette::WindowText, colors.textMuted);
        m_sectionLabel->setPalette(pal);
    }

    const int rr = theme.scaled(4);
    const QString hoverBg = colors.surfaceHover().name(QColor::HexArgb);
    const QString sheet = QStringLiteral(
        "QWidget#dockCtxToggleRow { border-radius: %1px; background: transparent; }"
        "QWidget#dockCtxToggleRow:hover { background: %2; }")
                              .arg(rr)
                              .arg(hoverBg);

    for (const BehaviorToggleRowDesc& br : m_behaviorToggleRows) {
        if (br.rowWidget) {
            br.rowWidget->setStyleSheet(sheet);
        }
    }

    updateBehaviorToggleRowsChrome();
}

void DockPanelContextMenu::updateBehaviorToggleRowsChrome()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    auto& icons = ruwa::ui::core::IconProvider::instance();
    const int iconPx = theme.scaled(16);

    for (const BehaviorToggleRowDesc& br : m_behaviorToggleRows) {
        if (!br.iconLabel || !br.textLabel || !br.toggle) {
            continue;
        }
        const bool rowActive = br.toggle->isEnabled();
        const QColor fg = rowActive ? colors.textMuted : colors.textDisabled();
        QPalette pal = br.textLabel->palette();
        pal.setColor(QPalette::WindowText, fg);
        br.textLabel->setPalette(pal);
        br.iconLabel->setPixmap(icons.getColoredIcon(br.iconKind, fg).pixmap(iconPx, iconPx));
    }
}

QWidget* DockPanelContextMenu::addBehaviorToggleRow(QVBoxLayout* column,
    ruwa::ui::core::IconProvider::StandardIcon iconKind, const QString& text,
    ToggleSwitch*& outToggle)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    QWidget* columnHost = column->parentWidget();
    auto* row = new BehaviorToggleRow(columnHost ? columnHost : contentWidget());
    row->setAttribute(Qt::WA_TranslucentBackground);

    auto* rowLayout = new QHBoxLayout(row);
    // Horizontal inset matches StandardContextMenuAction basePadding (10), scaled.
    rowLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(5), theme.scaled(10), theme.scaled(5));
    rowLayout->setSpacing(theme.scaled(6));

    const int iconPx = theme.scaled(16);

    auto* iconLabel = new QLabel(row);
    iconLabel->setFixedSize(iconPx, iconPx);
    iconLabel->setScaledContents(false);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* label = new QLabel(text, row);
    // Match StandardContextMenuAction: defaultButtonStyle baseFontSize 9, regular weight
    QFont lf = colors.fonts.getUIFont(theme.scaledFontSize(9));
    label->setFont(lf);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);

    outToggle = new ToggleSwitch(row);
    {
        // Between compact and full default (52×28); scales with UI like other menu chrome.
        auto& st = outToggle->style();
        st.metrics.baseWidth = 40;
        st.metrics.baseHeight = 20;
        st.metrics.baseCornerRadius = 10;
        outToggle->applyStyleChanges();
    }
    row->setToggleTarget(outToggle);

    rowLayout->addWidget(iconLabel, 0, Qt::AlignVCenter);
    rowLayout->addWidget(label, 1, Qt::AlignVCenter);
    rowLayout->addWidget(outToggle, 0, Qt::AlignVCenter);

    BehaviorToggleRowDesc desc;
    desc.rowWidget = row;
    desc.iconLabel = iconLabel;
    desc.textLabel = label;
    desc.toggle = outToggle;
    desc.iconKind = iconKind;
    m_behaviorToggleRows.append(desc);

    column->addWidget(row);
    return row;
}

void DockPanelContextMenu::buildUi()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    auto& icons = ruwa::ui::core::IconProvider::instance();

    auto* sectionWrap = new QWidget(contentWidget());
    sectionWrap->setAttribute(Qt::WA_TranslucentBackground);
    auto* sectionLayout = new QVBoxLayout(sectionWrap);
    sectionLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(4));
    sectionLayout->setSpacing(0);

    m_sectionLabel = new QLabel(tr("Behavior"), sectionWrap);
    sectionLayout->addWidget(m_sectionLabel);
    contentLayout()->addWidget(sectionWrap);

    auto* behaviorColumn = new QWidget(contentWidget());
    behaviorColumn->setAttribute(Qt::WA_TranslucentBackground);
    auto* behaviorLayout = new QVBoxLayout(behaviorColumn);
    behaviorLayout->setContentsMargins(0, 0, 0, 0);
    behaviorLayout->setSpacing(theme.scaled(2));
    contentLayout()->addWidget(behaviorColumn);

    addBehaviorToggleRow(behaviorLayout, ruwa::ui::core::IconProvider::StandardIcon::Move,
        tr("Movable"), m_movableToggle);
    addBehaviorToggleRow(behaviorLayout, ruwa::ui::core::IconProvider::StandardIcon::DockLayout,
        tr("Dockable"), m_dockableToggle);
    addBehaviorToggleRow(behaviorLayout, ruwa::ui::core::IconProvider::StandardIcon::Resize,
        tr("Resizable"), m_resizableToggle);

    connect(m_movableToggle, &ToggleSwitch::toggled, this, [this](bool enabled) {
        if (m_panel) {
            m_panel->setMovable(enabled);
        }
    });

    connect(m_resizableToggle, &ToggleSwitch::toggled, this, [this](bool enabled) {
        if (m_panel) {
            m_panel->setResizable(enabled);
        }
    });

    connect(m_dockableToggle, &ToggleSwitch::toggled, this, [this](bool enabled) {
        if (m_panel) {
            m_panel->setDockable(enabled);
        }
    });

    m_sepBeforeFloat = new HorizontalSeparator(contentWidget());
    m_sepBeforeFloat->setMargins(theme.scaled(4), theme.scaled(4));
    contentLayout()->addWidget(m_sepBeforeFloat);

    m_floatAction = addStandardMenuActionRow(QIcon(), QString(), false);
    connect(m_floatAction, &BaseStyledWidget::clicked, this, [this]() {
        if (m_panel) {
            m_panel->toggleFloating();
        }
        hideAnimated();
    });

    m_sepBeforeClose = new HorizontalSeparator(contentWidget());
    m_sepBeforeClose->setMargins(theme.scaled(4), theme.scaled(4));
    contentLayout()->addWidget(m_sepBeforeClose);

    m_closeAction = addStandardMenuActionRow(
        icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Close), tr("Close Panel"), true);
    connect(m_closeAction, &BaseStyledWidget::clicked, this, [this]() {
        if (m_panel) {
            m_panel->closePanel();
        }
        hideAnimated();
    });
}

void DockPanelContextMenu::rebuildStandardMenu()
{
    const QVariantMap ctx = context();
    const quintptr panelPtr
        = static_cast<quintptr>(ctx.value(QStringLiteral("dockPanelPtr")).toULongLong());
    m_panel = reinterpret_cast<ruwa::ui::docking::DockPanel*>(panelPtr);

    const bool hasPanel = !m_panel.isNull();
    const bool isFloating = hasPanel && m_panel->isFloating();

    const QSignalBlocker b1(m_movableToggle);
    const QSignalBlocker b2(m_resizableToggle);
    const QSignalBlocker b3(m_dockableToggle);

    m_movableToggle->setEnabled(hasPanel);
    m_resizableToggle->setEnabled(isFloating);
    m_dockableToggle->setEnabled(isFloating);

    m_movableToggle->setChecked(
        hasPanel && m_panel->isMovable(), ToggleSwitch::TransitionMode::Instant);
    m_resizableToggle->setChecked(
        hasPanel && m_panel->isResizable(), ToggleSwitch::TransitionMode::Instant);
    m_dockableToggle->setChecked(
        hasPanel && m_panel->isDockable(), ToggleSwitch::TransitionMode::Instant);

    auto& icons = ruwa::ui::core::IconProvider::instance();
    if (isFloating) {
        m_floatAction->setIcon(
            icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::DockLayout));
        m_floatAction->setText(tr("Dock to Layout"));
    } else {
        m_floatAction->setIcon(
            icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::DetachPanel));
        m_floatAction->setText(tr("Detach from Layout"));
    }

    const bool canFloat = hasPanel && m_panel->isFloatable();
    m_floatAction->setEnabled(canFloat);

    const bool canClose = hasPanel && m_panel->isClosable();
    m_sepBeforeClose->setVisible(canClose);
    m_closeAction->setVisible(canClose);

    applyChrome();
    updateMenuSize();
}

} // namespace ruwa::ui::widgets
