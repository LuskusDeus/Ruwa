// SPDX-License-Identifier: MPL-2.0

#include "DockPanelContextMenu.h"

#include "features/brush/ui/BrushesPanel.h"
#include "features/layers/ui/LayersPanel.h"
#include "features/tools/ToolsPanel.h"
#include "shell/docking/widgets/DockPanel.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/BaseStyledWidget.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/HorizontalSeparator.h"
#include "features/theme/manager/ThemeManager.h"

#include <QGridLayout>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {

constexpr int kIconColumn = 0;
constexpr int kLabelColumn = 2;
constexpr int kControlColumn = 4;

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
}

void DockPanelContextMenu::applyChrome()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    for (QLabel* sectionLabel :
        { m_sectionLabel, m_toolsSectionLabel, m_layerButtonsSectionLabel }) {
        if (sectionLabel) {
            QFont f = theme.font(ruwa::ui::core::ThemeFontRole::Label, QFont::DemiBold);
            f.setCapitalization(QFont::AllUppercase);
            f.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.8));
            sectionLabel->setFont(f);
            QPalette pal = sectionLabel->palette();
            pal.setColor(QPalette::WindowText, colors.textMuted);
            sectionLabel->setPalette(pal);
        }
    }

    if (m_hudSizeSlider) {
        m_hudSizeSlider->setFixedHeight(theme.scaled(22));
        m_hudSizeSlider->setMinimumWidth(theme.scaled(180));
        m_hudSizeLabel->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
        QPalette palette = m_hudSizeLabel->palette();
        palette.setColor(QPalette::WindowText, colors.textMuted);
        m_hudSizeLabel->setPalette(palette);
        m_sepBeforeBrushes->setMargins(theme.scaled(4), theme.scaled(4));
    }

    const int rr = theme.scaled(4);
    const QString hoverBg = colors.surfaceHover().name(QColor::HexArgb);
    const QString sheet = QStringLiteral(
        "QWidget#dockCtxToggleRow { border-radius: %1px; background: transparent; }"
        "QWidget#dockCtxToggleRow:hover { background: %2; }")
                              .arg(rr)
                              .arg(hoverBg);

    for (const BehaviorToggleRowDesc& br : m_toggleRows) {
        if (br.rowWidget) {
            br.rowWidget->setStyleSheet(sheet);
        }
    }

    updateToggleRowsChrome();
    updateControlColumns();
    // Re-measure after fonts and column constraints change, including a live
    // theme-scale change while the menu is open.
    setContentMargins(theme.scaled(QMargins(6, 6, 6, 6)));
}

void DockPanelContextMenu::updateToggleRowsChrome()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    auto& icons = ruwa::ui::core::IconProvider::instance();
    const int iconPx = theme.scaled(16);

    for (const BehaviorToggleRowDesc& br : m_toggleRows) {
        if (!br.iconLabel || !br.textLabel || !br.toggle) {
            continue;
        }
        const bool rowActive = br.toggle->isEnabled();
        const QColor fg = rowActive ? colors.textMuted : colors.textDisabled();
        br.textLabel->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body));
        br.iconLabel->setFixedSize(iconPx, iconPx);
        QPalette pal = br.textLabel->palette();
        pal.setColor(QPalette::WindowText, fg);
        br.textLabel->setPalette(pal);
        br.iconLabel->setPixmap(icons.getColoredIcon(br.iconKind, fg).pixmap(iconPx, iconPx));
    }
}

void DockPanelContextMenu::updateControlColumns()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const bool sharedColumns = !m_brushesPanel.isNull();
    const auto isBehaviorRow = [this](const BehaviorToggleRowDesc& row) {
        return row.toggle == m_movableToggle || row.toggle == m_dockableToggle
            || row.toggle == m_resizableToggle;
    };

    const auto naturalTextWidth = [](const QLabel* label) {
        // Measure the text, not the previous fixed column width.
        return label->fontMetrics().size(Qt::TextSingleLine, label->text()).width();
    };
    int labelWidth = naturalTextWidth(m_hudSizeLabel);
    for (const BehaviorToggleRowDesc& row : m_toggleRows) {
        if (isBehaviorRow(row)) {
            labelWidth = qMax(labelWidth, naturalTextWidth(row.textLabel));
        }
    }

    const auto setColumns = [&theme, labelWidth](QGridLayout* grid, QLabel* label, bool shared) {
        grid->setContentsMargins(theme.scaled(QMargins(10, 5, 10, 5)));
        grid->setSpacing(0);
        grid->setColumnMinimumWidth(kIconColumn, theme.scaled(16));
        grid->setColumnMinimumWidth(1, theme.scaled(6));
        grid->setColumnMinimumWidth(kLabelColumn, shared ? labelWidth : 0);
        grid->setColumnMinimumWidth(3, theme.scaled(shared ? 12 : 6));
        grid->setColumnMinimumWidth(kControlColumn, shared ? theme.scaled(180) : 0);
        grid->setColumnStretch(kLabelColumn, shared ? 0 : 1);
        grid->setColumnStretch(kControlColumn, shared ? 1 : 0);
        if (shared) {
            label->setFixedWidth(labelWidth);
        } else {
            label->setMinimumWidth(0);
            label->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    };

    for (const BehaviorToggleRowDesc& row : m_toggleRows) {
        auto* grid = static_cast<QGridLayout*>(row.rowWidget->layout());
        setColumns(grid, row.textLabel, sharedColumns && isBehaviorRow(row));
    }
    // HUD Size spans the unused icon and label columns, starting at the row's
    // left inset while preserving the shared boundary before the slider.
    auto* hudGrid = static_cast<QGridLayout*>(m_brushesSectionHost->layout());
    setColumns(hudGrid, m_hudSizeLabel, true);
    m_hudSizeLabel->setFixedWidth(hudGrid->columnMinimumWidth(kIconColumn)
        + hudGrid->columnMinimumWidth(1) + hudGrid->columnMinimumWidth(kLabelColumn));
}

QWidget* DockPanelContextMenu::createToggleRow(QWidget* parent,
    ruwa::ui::core::IconProvider::StandardIcon iconKind, const QString& text,
    ToggleSwitch*& outToggle)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    auto* row = new BehaviorToggleRow(parent ? parent : contentWidget());
    row->setAttribute(Qt::WA_TranslucentBackground);
    row->setAccessibleName(text);

    auto* rowLayout = new QGridLayout(row);
    // Horizontal inset matches StandardContextMenuAction basePadding (10), scaled.
    rowLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(5), theme.scaled(10), theme.scaled(5));
    rowLayout->setSpacing(0);
    rowLayout->setColumnMinimumWidth(1, theme.scaled(6));
    rowLayout->setColumnMinimumWidth(3, theme.scaled(6));
    rowLayout->setColumnStretch(kLabelColumn, 1);

    const int iconPx = theme.scaled(16);

    auto* iconLabel = new QLabel(row);
    iconLabel->setFixedSize(iconPx, iconPx);
    iconLabel->setScaledContents(false);
    iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto* label = new QLabel(text, row);
    // Match StandardContextMenuAction: defaultButtonStyle Body role, regular weight.
    QFont lf = theme.font(ruwa::ui::core::ThemeFontRole::Body);
    label->setFont(lf);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);

    outToggle = new ToggleSwitch(row);
    outToggle->setAccessibleName(text);
    {
        // Between compact and full default (52×28); scales with UI like other menu chrome.
        auto& st = outToggle->style();
        st.metrics.baseWidth = 40;
        st.metrics.baseHeight = 20;
        st.metrics.baseCornerRadius = 10;
        outToggle->applyStyleChanges();
    }
    row->setToggleTarget(outToggle);

    rowLayout->addWidget(iconLabel, 0, kIconColumn, Qt::AlignVCenter);
    rowLayout->addWidget(label, 0, kLabelColumn, Qt::AlignVCenter);
    rowLayout->addWidget(outToggle, 0, kControlColumn, Qt::AlignRight | Qt::AlignVCenter);

    BehaviorToggleRowDesc desc;
    desc.rowWidget = row;
    desc.iconLabel = iconLabel;
    desc.textLabel = label;
    desc.toggle = outToggle;
    desc.iconKind = iconKind;
    m_toggleRows.append(desc);
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

    behaviorLayout->addWidget(createToggleRow(behaviorColumn,
        ruwa::ui::core::IconProvider::StandardIcon::Move, tr("Movable"), m_movableToggle));
    behaviorLayout->addWidget(createToggleRow(behaviorColumn,
        ruwa::ui::core::IconProvider::StandardIcon::DockLayout, tr("Dockable"), m_dockableToggle));
    behaviorLayout->addWidget(createToggleRow(behaviorColumn,
        ruwa::ui::core::IconProvider::StandardIcon::Resize, tr("Resizable"), m_resizableToggle));

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

    // Sits right under "Detach from Layout": both answer "get this panel out of
    // where it currently is", one to a window, one to its own layout cell.
    m_ungroupAction = addStandardMenuActionRow(
        icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::LayoutSwitch),
        tr("Ungroup Panel"), false);
    connect(m_ungroupAction, &BaseStyledWidget::clicked, this, [this]() {
        if (m_panel) {
            m_panel->ungroupPanel();
        }
        hideAnimated();
    });

    using BrushesPanel = ruwa::ui::workspace::BrushesPanel;
    m_sepBeforeBrushes = new HorizontalSeparator(contentWidget());
    contentLayout()->addWidget(m_sepBeforeBrushes);
    m_brushesSectionHost = new QWidget(contentWidget());
    m_brushesSectionHost->setAttribute(Qt::WA_TranslucentBackground);
    auto* brushesLayout = new QGridLayout(m_brushesSectionHost);
    m_hudSizeLabel = new QLabel(tr("HUD Size"), m_brushesSectionHost);
    m_hudSizeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    brushesLayout->addWidget(m_hudSizeLabel, 0, kIconColumn, 1, kLabelColumn - kIconColumn + 1,
        Qt::AlignLeft | Qt::AlignVCenter);

    m_hudSizeSlider = new ProgressHandleSlider(m_brushesSectionHost);
    m_hudSizeSlider->setAccessibleName(tr("HUD Size"));
    m_hudSizeSlider->setRange(BrushesPanel::kMinimumHudSize, BrushesPanel::kMaximumHudSize);
    m_hudSizeSlider->setValueDisplayMode(ProgressHandleSlider::ValueDisplayMode::RawValue);
    m_hudSizeSlider->setValueTextSuffix(QStringLiteral("%"));
    m_hudSizeSlider->setValue(BrushesPanel::kDefaultHudSize);
    brushesLayout->addWidget(m_hudSizeSlider, 0, kControlColumn);
    contentLayout()->addWidget(m_brushesSectionHost);
    m_brushesSectionHost->hide();
    m_sepBeforeBrushes->hide();

    connect(m_hudSizeSlider, &ProgressHandleSlider::valueChanged, this, [this](int value) {
        if (m_brushesPanel) {
            m_brushesPanel->setHudSize(value);
        }
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

    m_sepBeforeTools = new HorizontalSeparator(contentWidget());
    m_sepBeforeTools->setMargins(theme.scaled(4), theme.scaled(4));
    contentLayout()->addWidget(m_sepBeforeTools);

    m_toolsSectionHost = new QWidget(contentWidget());
    m_toolsSectionHost->setAttribute(Qt::WA_TranslucentBackground);
    auto* toolsSectionLayout = new QVBoxLayout(m_toolsSectionHost);
    toolsSectionLayout->setContentsMargins(0, 0, 0, 0);
    toolsSectionLayout->setSpacing(theme.scaled(2));

    auto* toolsHeader = new QWidget(m_toolsSectionHost);
    toolsHeader->setAttribute(Qt::WA_TranslucentBackground);
    auto* toolsHeaderLayout = new QVBoxLayout(toolsHeader);
    toolsHeaderLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(4));
    toolsHeaderLayout->setSpacing(0);
    m_toolsSectionLabel = new QLabel(tr("Visible tools"), toolsHeader);
    toolsHeaderLayout->addWidget(m_toolsSectionLabel);
    toolsSectionLayout->addWidget(toolsHeader);

    auto* toolsGridHost = new QWidget(m_toolsSectionHost);
    toolsGridHost->setAttribute(Qt::WA_TranslucentBackground);
    auto* toolsGrid = new QGridLayout(toolsGridHost);
    toolsGrid->setContentsMargins(0, 0, 0, 0);
    toolsGrid->setHorizontalSpacing(theme.scaled(4));
    toolsGrid->setVerticalSpacing(theme.scaled(2));
    toolsGrid->setColumnStretch(0, 1);
    toolsGrid->setColumnStretch(1, 1);

    const QList<ruwa::ui::workspace::ToolId> tools
        = ruwa::ui::workspace::ToolsPanel::configurableTools();
    const int rowsPerColumn = (tools.size() + 1) / 2;
    for (int index = 0; index < tools.size(); ++index) {
        const ruwa::ui::workspace::ToolId tool = tools[index];
        ToggleSwitch* toggle = nullptr;
        QWidget* row
            = createToggleRow(toolsGridHost, ruwa::ui::workspace::ToolsPanel::toolIconType(tool),
                ruwa::ui::workspace::ToolsPanel::toolDisplayName(tool), toggle);
        const int column = index / rowsPerColumn;
        const int rowIndex = index % rowsPerColumn;
        toolsGrid->addWidget(row, rowIndex, column);
        m_toolToggles.append({ tool, toggle });

        connect(toggle, &ToggleSwitch::toggled, this, [this, tool](bool visible) {
            if (m_toolsPanel) {
                m_toolsPanel->setToolVisible(tool, visible);
            }
        });
    }
    toolsSectionLayout->addWidget(toolsGridHost);
    contentLayout()->addWidget(m_toolsSectionHost);

    // --- Layers panel: same show/hide switches for its toolbar buttons ---
    m_sepBeforeLayerButtons = new HorizontalSeparator(contentWidget());
    m_sepBeforeLayerButtons->setMargins(theme.scaled(4), theme.scaled(4));
    contentLayout()->addWidget(m_sepBeforeLayerButtons);

    m_layerButtonsSectionHost = new QWidget(contentWidget());
    m_layerButtonsSectionHost->setAttribute(Qt::WA_TranslucentBackground);
    auto* layerButtonsSectionLayout = new QVBoxLayout(m_layerButtonsSectionHost);
    layerButtonsSectionLayout->setContentsMargins(0, 0, 0, 0);
    layerButtonsSectionLayout->setSpacing(theme.scaled(2));

    auto* layerButtonsHeader = new QWidget(m_layerButtonsSectionHost);
    layerButtonsHeader->setAttribute(Qt::WA_TranslucentBackground);
    auto* layerButtonsHeaderLayout = new QVBoxLayout(layerButtonsHeader);
    layerButtonsHeaderLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(4));
    layerButtonsHeaderLayout->setSpacing(0);
    m_layerButtonsSectionLabel = new QLabel(tr("Visible buttons"), layerButtonsHeader);
    layerButtonsHeaderLayout->addWidget(m_layerButtonsSectionLabel);
    layerButtonsSectionLayout->addWidget(layerButtonsHeader);

    auto* layerButtonsColumn = new QWidget(m_layerButtonsSectionHost);
    layerButtonsColumn->setAttribute(Qt::WA_TranslucentBackground);
    auto* layerButtonsColumnLayout = new QVBoxLayout(layerButtonsColumn);
    layerButtonsColumnLayout->setContentsMargins(0, 0, 0, 0);
    layerButtonsColumnLayout->setSpacing(theme.scaled(2));

    using LayersPanel = ruwa::ui::workspace::LayersPanel;
    for (const LayersPanel::ToolbarItem item : LayersPanel::configurableToolbarItems()) {
        ToggleSwitch* toggle = nullptr;
        QWidget* row = createToggleRow(layerButtonsColumn, LayersPanel::toolbarItemIconType(item),
            LayersPanel::toolbarItemDisplayName(item), toggle);
        layerButtonsColumnLayout->addWidget(row);
        m_layerButtonToggles.append({ item, toggle });

        connect(toggle, &ToggleSwitch::toggled, this, [this, item](bool visible) {
            if (m_layersPanel) {
                m_layersPanel->setToolbarItemVisible(item, visible);
            }
        });
    }
    layerButtonsSectionLayout->addWidget(layerButtonsColumn);
    contentLayout()->addWidget(m_layerButtonsSectionHost);

    m_sepBeforeTools->hide();
    m_toolsSectionHost->hide();
    m_sepBeforeLayerButtons->hide();
    m_layerButtonsSectionHost->hide();
}

QSize DockPanelContextMenu::expandMenuContentHint(const QSize& hint) const
{
    QSize expanded = StandardContextMenu::expandMenuContentHint(hint);
    if (m_brushesPanel) {
        expanded.setWidth(
            qMax(expanded.width(), ruwa::ui::core::ThemeManager::instance().scaled(340)));
    }
    return expanded;
}

void DockPanelContextMenu::rebuildStandardMenu()
{
    const QVariantMap ctx = context();
    const quintptr panelPtr
        = static_cast<quintptr>(ctx.value(QStringLiteral("dockPanelPtr")).toULongLong());
    m_panel = reinterpret_cast<ruwa::ui::docking::DockPanel*>(panelPtr);
    m_toolsPanel = qobject_cast<ruwa::ui::workspace::ToolsPanel*>(m_panel.data());
    m_layersPanel = qobject_cast<ruwa::ui::workspace::LayersPanel*>(m_panel.data());
    m_brushesPanel = qobject_cast<ruwa::ui::workspace::BrushesPanel*>(m_panel.data());

    const bool hasBrushesPanel = !m_brushesPanel.isNull();
    m_brushesSectionHost->setVisible(hasBrushesPanel);
    m_sepBeforeBrushes->setVisible(hasBrushesPanel);
    {
        const QSignalBlocker blocker(m_hudSizeSlider);
        m_hudSizeSlider->setEnabled(hasBrushesPanel);
        m_hudSizeSlider->setValue(hasBrushesPanel
                ? m_brushesPanel->hudSize()
                : ruwa::ui::workspace::BrushesPanel::kDefaultHudSize);
    }

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

    // Only meaningful for a panel that actually shares a cell right now.
    m_ungroupAction->setVisible(hasPanel && m_panel->isGrouped());

    const bool canClose = hasPanel && m_panel->isClosable();
    m_sepBeforeClose->setVisible(canClose);
    m_closeAction->setVisible(canClose);

    const bool hasToolsPanel = !m_toolsPanel.isNull();
    m_sepBeforeTools->setVisible(hasToolsPanel);
    m_toolsSectionHost->setVisible(hasToolsPanel);
    for (const ToolToggleDesc& desc : m_toolToggles) {
        const QSignalBlocker blocker(desc.toggle);
        desc.toggle->setEnabled(hasToolsPanel);
        desc.toggle->setChecked(hasToolsPanel && m_toolsPanel->isToolVisible(desc.tool),
            ToggleSwitch::TransitionMode::Instant);
    }

    const bool hasLayersPanel = !m_layersPanel.isNull();
    m_sepBeforeLayerButtons->setVisible(hasLayersPanel);
    m_layerButtonsSectionHost->setVisible(hasLayersPanel);
    for (const LayerButtonToggleDesc& desc : m_layerButtonToggles) {
        const QSignalBlocker blocker(desc.toggle);
        desc.toggle->setEnabled(hasLayersPanel);
        desc.toggle->setChecked(hasLayersPanel && m_layersPanel->isToolbarItemVisible(desc.item),
            ToggleSwitch::TransitionMode::Instant);
    }

    applyChrome();
}

} // namespace ruwa::ui::widgets
