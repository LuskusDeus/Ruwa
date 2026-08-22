// SPDX-License-Identifier: MPL-2.0

// ToolsPanel.cpp
#include "ToolsPanel.h"
#include "ToolGroupPopup.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/layers/ui/LayersPanel.h"
#include "shared/widgets/ToolButton.h"
#include "shared/widgets/layout/AnimatedFlowWidget.h"
#include "shared/widgets/overlays/ToolTipController.h"
#include "shared/widgets/reorderlist/ListDragDrop.h"
#include "shell/context-menu/ContextMenuSystem.h"

#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/style/AnimationPolicy.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QJsonArray>
#include <QList>
#include <QButtonGroup>
#include <QTimer>
#include <QElapsedTimer>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QSizePolicy>
#include <QVariantAnimation>
#include <QtGlobal>
#include <array>
#include <functional>
#include <optional>
#include <utility>

namespace ruwa::ui::workspace {

using namespace ruwa::ui::core;

namespace {
const int BASE_PANEL_HEADER_HEIGHT = 19; // DockPanelTitleBar default
// Mirrors DockPanel::contentPadding(): the sides sit a step under the top so
// they do not read wider than the band under the title bar's fillets.
const int BASE_MARGIN_V = 8;
const int BASE_MARGIN_H = 6;
const int BASE_SPACING = 6;
const int BASE_TOOL_BUTTON_SIZE = 36; // ToolButton's default square
const int BASE_PANEL_EDGE_SLACK = 4; // panel inset + border, both sides
// One button plus its margins, so the smallest panel is exactly one column
// (or one row) wide. Derived rather than written out: the margins moved once
// already, and a stale literal here would leave dead space beside the column.
const int BASE_PANEL_MIN_WIDTH
    = BASE_TOOL_BUTTON_SIZE + (BASE_MARGIN_H * 2) + BASE_PANEL_EDGE_SLACK;
const int BASE_PANEL_MIN_HEIGHT
    = BASE_TOOL_BUTTON_SIZE + (BASE_MARGIN_V * 2) + BASE_PANEL_EDGE_SLACK;
const int GROUP_HOLD_DELAY_MS = 350;
const int PANEL_STATE_VERSION = 2;

struct ToolPresentation {
    ToolId tool;
    IconProvider::StandardIcon icon;
    const char* tooltip;
    const char* commandId;
};

constexpr std::array kToolPresentations {
    ToolPresentation { ToolId::Hand, IconProvider::StandardIcon::Hand,
        QT_TRANSLATE_NOOP("ToolsPanel", "Hand"), "tools.hand" },
    ToolPresentation { ToolId::Brush, IconProvider::StandardIcon::Brush,
        QT_TRANSLATE_NOOP("ToolsPanel", "Brush"), "tools.brush" },
    ToolPresentation { ToolId::Eraser, IconProvider::StandardIcon::Eraser,
        QT_TRANSLATE_NOOP("ToolsPanel", "Eraser"), "tools.eraser" },
    ToolPresentation { ToolId::Fill, IconProvider::StandardIcon::SmartFillColor,
        QT_TRANSLATE_NOOP("ToolsPanel", "Fill"), "tools.fill" },
    ToolPresentation { ToolId::ClassicFill, IconProvider::StandardIcon::FillColor,
        QT_TRANSLATE_NOOP("ToolsPanel", "Classic Fill"), "tools.classic-fill" },
    ToolPresentation { ToolId::Eyedropper, IconProvider::StandardIcon::Eyedropper,
        QT_TRANSLATE_NOOP("ToolsPanel", "Eyedropper"), "tools.eyedropper" },
    ToolPresentation { ToolId::Lasso, IconProvider::StandardIcon::Lasso,
        QT_TRANSLATE_NOOP("ToolsPanel", "Lasso"), "tools.lasso" },
    ToolPresentation { ToolId::LassoFill, IconProvider::StandardIcon::LassoFill,
        QT_TRANSLATE_NOOP("ToolsPanel", "Lasso Fill"), "tools.lasso-fill" },
    ToolPresentation { ToolId::SquareSelection, IconProvider::StandardIcon::SquareSelection,
        QT_TRANSLATE_NOOP("ToolsPanel", "Square Selection"), "tools.square-selection" },
    ToolPresentation { ToolId::CircleSelection, IconProvider::StandardIcon::CircleSelection,
        QT_TRANSLATE_NOOP("ToolsPanel", "Circle Selection"), "tools.circle-selection" },
    ToolPresentation { ToolId::Move, IconProvider::StandardIcon::Move,
        QT_TRANSLATE_NOOP("ToolsPanel", "Move"), "tools.move" },
    ToolPresentation { ToolId::RotateView, IconProvider::StandardIcon::RotateView,
        QT_TRANSLATE_NOOP("ToolsPanel", "Rotate View"), "tools.rotate-view" },
    ToolPresentation { ToolId::CanvasResize, IconProvider::StandardIcon::Crop,
        QT_TRANSLATE_NOOP("ToolsPanel", "Canvas Resize"), "tools.canvas-resize" },
    ToolPresentation { ToolId::Zoom, IconProvider::StandardIcon::Zoom,
        QT_TRANSLATE_NOOP("ToolsPanel", "Zoom"), "tools.zoom" },
    ToolPresentation { ToolId::Blur, IconProvider::StandardIcon::Blur,
        QT_TRANSLATE_NOOP("ToolsPanel", "Blur"), "tools.blur" },
    ToolPresentation { ToolId::Smudge, IconProvider::StandardIcon::Smudge,
        QT_TRANSLATE_NOOP("ToolsPanel", "Smudge"), "tools.smudge" },
    ToolPresentation { ToolId::Liquify, IconProvider::StandardIcon::Liquify,
        QT_TRANSLATE_NOOP("ToolsPanel", "Liquify"), "tools.liquify" },
    ToolPresentation { ToolId::Text, IconProvider::StandardIcon::Text,
        QT_TRANSLATE_NOOP("ToolsPanel", "Text"), "tools.text" },
    ToolPresentation { ToolId::MagicWand, IconProvider::StandardIcon::MagicWand,
        QT_TRANSLATE_NOOP("ToolsPanel", "Magic Wand"), "tools.magic-wand" },
};

constexpr bool hasCompleteToolPresentationSet()
{
    if (kToolPresentations.size() != kToolIds.size()) {
        return false;
    }
    for (const ToolId tool : kToolIds) {
        int matches = 0;
        for (const ToolPresentation& presentation : kToolPresentations) {
            matches += presentation.tool == tool ? 1 : 0;
        }
        if (matches != 1) {
            return false;
        }
    }
    return true;
}
static_assert(hasCompleteToolPresentationSet());

const ToolPresentation& presentationForTool(ToolId tool)
{
    for (const ToolPresentation& presentation : kToolPresentations) {
        if (presentation.tool == tool) {
            return presentation;
        }
    }
    return kToolPresentations.front();
}

/// Tooltips are declared with QT_TRANSLATE_NOOP("ToolsPanel", ...), so they have
/// to be looked up in that same context. tr() inside the class would resolve to
/// "ruwa::ui::workspace::ToolsPanel" instead and never find the translation.
QString translateToolTooltip(const char* tooltip)
{
    return QCoreApplication::translate("ToolsPanel", tooltip);
}

bool isGroupRepresentative(ToolId tool)
{
    return tool == ToolId::Blur || tool == ToolId::Fill || tool == ToolId::Lasso
        || tool == ToolId::SquareSelection;
}

ToolId groupRepresentativeForTool(ToolId tool)
{
    switch (tool) {
    case ToolId::Smudge:
    case ToolId::Liquify:
        return ToolId::Blur;
    case ToolId::ClassicFill:
        return ToolId::Fill;
    case ToolId::LassoFill:
        return ToolId::Lasso;
    case ToolId::CircleSelection:
        return ToolId::SquareSelection;
    default:
        return tool;
    }
}

QList<ToolId> groupMembers(ToolId representative)
{
    switch (representative) {
    case ToolId::Blur:
        return { ToolId::Blur, ToolId::Smudge, ToolId::Liquify };
    case ToolId::Fill:
        return { ToolId::Fill, ToolId::ClassicFill };
    case ToolId::Lasso:
        return { ToolId::Lasso, ToolId::LassoFill };
    case ToolId::SquareSelection:
        return { ToolId::SquareSelection, ToolId::CircleSelection };
    default:
        return { representative };
    }
}

ToolGroupPopup::Side resolvePopupSide(const QWidget* toolsPanel, const CanvasPanel* canvasPanel)
{
    if (!toolsPanel || !canvasPanel || !toolsPanel->isVisible() || !canvasPanel->isVisible()) {
        return ToolGroupPopup::Side::Right;
    }

    const QRect toolsRect(toolsPanel->mapToGlobal(QPoint(0, 0)), toolsPanel->size());
    const QRect canvasRect(canvasPanel->mapToGlobal(QPoint(0, 0)), canvasPanel->size());

    if (toolsRect.right() <= canvasRect.left()) {
        return ToolGroupPopup::Side::Right;
    }
    if (toolsRect.left() >= canvasRect.right()) {
        return ToolGroupPopup::Side::Left;
    }
    if (toolsRect.bottom() <= canvasRect.top()) {
        return ToolGroupPopup::Side::Bottom;
    }
    if (toolsRect.top() >= canvasRect.bottom()) {
        return ToolGroupPopup::Side::Top;
    }

    const QPoint toolsCenter = toolsRect.center();
    const QPoint canvasCenter = canvasRect.center();
    const int dx = toolsCenter.x() - canvasCenter.x();
    const int dy = toolsCenter.y() - canvasCenter.y();
    const int absDx = dx >= 0 ? dx : -dx;
    const int absDy = dy >= 0 ? dy : -dy;

    if (absDx >= absDy) {
        return dx <= 0 ? ToolGroupPopup::Side::Right : ToolGroupPopup::Side::Left;
    }

    return dy <= 0 ? ToolGroupPopup::Side::Bottom : ToolGroupPopup::Side::Top;
}

ToolGroupPopup::LayoutMode resolvePopupLayoutMode(const QWidget* toolsPanel)
{
    if (!toolsPanel || !toolsPanel->isVisible()) {
        return ToolGroupPopup::LayoutMode::Vertical;
    }

    return toolsPanel->width() > toolsPanel->height() ? ToolGroupPopup::LayoutMode::Horizontal
                                                      : ToolGroupPopup::LayoutMode::Vertical;
}

class GroupToolButton final : public ToolButton {
public:
    explicit GroupToolButton(QWidget* parent = nullptr)
        : ToolButton(parent)
    {
        m_holdTimer.setSingleShot(true);
        m_holdTimer.setInterval(GROUP_HOLD_DELAY_MS);
        QObject::connect(&m_holdTimer, &QTimer::timeout, this, [this]() {
            m_popupTriggered = true;
            if (m_popupCallback) {
                m_popupCallback();
            }
        });
    }

    void setPopupCallback(std::function<void()> callback) { m_popupCallback = std::move(callback); }
    void cancelPopupGesture()
    {
        m_holdTimer.stop();
        m_popupTriggered = false;
        m_suppressContextMenuEvent = false;
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::RightButton) {
            m_holdTimer.stop();
            m_popupTriggered = true;
            m_suppressContextMenuEvent = true;
            if (m_popupCallback) {
                m_popupCallback();
            }
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton) {
            m_popupTriggered = false;
            m_holdTimer.start();
        }

        ToolButton::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_holdTimer.stop();
            if (m_popupTriggered) {
                m_popupTriggered = false;
                setDown(false);
                setHovered(rect().contains(mapFromGlobal(QCursor::pos())));
                update();
                event->accept();
                return;
            }
        }

        ToolButton::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_holdTimer.stop();
            m_popupTriggered = true;
            setDown(false);
            update();
            if (m_popupCallback) {
                m_popupCallback();
            }
            event->accept();
            return;
        }

        ToolButton::mouseDoubleClickEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (m_suppressContextMenuEvent) {
            m_suppressContextMenuEvent = false;
            event->accept();
            return;
        }

        m_holdTimer.stop();
        m_popupTriggered = true;
        setDown(false);
        update();
        if (m_popupCallback) {
            m_popupCallback();
        }
        event->accept();
    }

private:
    QTimer m_holdTimer;
    bool m_popupTriggered = false;
    bool m_suppressContextMenuEvent = false;
    std::function<void()> m_popupCallback;
};

const QList<ToolId>& defaultToolOrder()
{
    static const QList<ToolId> order { ToolId::Hand, ToolId::RotateView, ToolId::Zoom,
        ToolId::Brush, ToolId::Eraser, ToolId::Fill, ToolId::Eyedropper, ToolId::Blur, ToolId::Move,
        ToolId::SquareSelection, ToolId::Lasso, ToolId::MagicWand, ToolId::Text,
        ToolId::CanvasResize };
    return order;
}

QList<ToolId> normalizedToolOrder(const QJsonArray& values)
{
    QList<ToolId> result;
    const QList<ToolId>& defaults = defaultToolOrder();
    for (const QJsonValue& value : values) {
        const std::optional<ToolId> tool = toolIdFromPersistentValue(value.toInt(-1));
        if (tool && defaults.contains(*tool) && !result.contains(*tool)) {
            result.append(*tool);
        }
    }
    for (ToolId tool : defaults) {
        if (!result.contains(tool)) {
            result.append(tool);
        }
    }
    return result;
}

QJsonArray serializedToolOrder(const QList<ToolId>& order)
{
    QJsonArray values;
    for (ToolId tool : order) {
        values.append(persistentValueForToolId(tool));
    }
    return values;
}

QList<ToolId> normalizedHiddenTools(const QJsonArray& values)
{
    QList<ToolId> requestedTools;
    for (const QJsonValue& value : values) {
        const std::optional<ToolId> tool = toolIdFromPersistentValue(value.toInt(-1));
        if (tool && !requestedTools.contains(*tool)) {
            requestedTools.append(*tool);
        }
    }

    QList<ToolId> result;
    for (ToolId tool : kToolIds) {
        if (requestedTools.contains(tool)) {
            result.append(tool);
        }
    }
    return result;
}

QJsonArray serializedHiddenTools(const QList<ToolId>& hiddenTools)
{
    QJsonArray values;
    for (ToolId tool : kToolIds) {
        if (hiddenTools.contains(tool)) {
            values.append(persistentValueForToolId(tool));
        }
    }
    return values;
}

bool isToolSwitchProfilingEnabled()
{
    static const bool enabled = qEnvironmentVariableIsSet("RUWA_PROFILE_TOOL_SWITCH");
    return enabled;
}

} // anonymous namespace

ToolsPanel::ToolsPanel(QWidget* parent)
    : DockPanel(tr("Tools"), parent)
{
    setTranslatableTitle(QT_TR_NOOP("Tools"));
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::ToolsPanel);
    setTabletTracking(true);
    auto& mgr = WidgetStyleManager::instance();

    int minWidth = mgr.scaled(BASE_PANEL_MIN_WIDTH);
    int minHeight = mgr.scaled(BASE_PANEL_MIN_HEIGHT) + mgr.scaled(BASE_PANEL_HEADER_HEIGHT);
    setMinimumPanelSize(minWidth, minHeight);
    setPreferredPanelSize(mgr.scaled(BASE_PANEL_MIN_WIDTH), mgr.scaled(250));

    setClosable(false);
    setFloatable(true);
    setMovable(true);

    m_groupSelections[ToolId::Blur] = ToolId::Blur;
    m_groupSelections[ToolId::Fill] = ToolId::Fill;
    m_groupSelections[ToolId::Lasso] = ToolId::Lasso;
    m_groupSelections[ToolId::SquareSelection] = ToolId::SquareSelection;

    m_toolOrder = defaultToolOrder();
}

ToolsPanel::~ToolsPanel()
{
    if (m_dragActive) {
        qApp->removeEventFilter(this);
    }
    if (m_dragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_dragCursorOverride = false;
    }
    if (m_dragGhost) {
        delete m_dragGhost.data();
        m_dragGhost = nullptr;
    }
    if (m_contentWidget) {
        m_contentWidget->shutdown();
    }
    delete m_groupPopup;
}

void ToolsPanel::setRelatedPanels(CanvasPanel* canvasPanel, LayersPanel* layersPanel)
{
    m_canvasPanel = canvasPanel;
    m_layersPanel = layersPanel;
}

void ToolsPanel::preparePresentationSnapshot()
{
    for (auto it = m_visibilityAnimations.begin(); it != m_visibilityAnimations.end(); ++it) {
        if (it.value()) {
            it.value()->stop();
            it.value()->deleteLater();
        }
    }
    m_visibilityAnimations.clear();

    for (auto it = m_toolsData.begin(); it != m_toolsData.end(); ++it) {
        if (it->button) {
            if (it->button != m_draggedButton) {
                it->button->setGraphicsEffect(nullptr);
            }
            it->button->setVisible(m_appliedToolOrder.contains(it.key()));
            it->button->finishVisualTransitions();
        }
    }
}

QList<ToolId> ToolsPanel::configurableTools()
{
    QList<ToolId> tools;
    tools.reserve(static_cast<qsizetype>(kToolPresentations.size()));
    for (const ToolPresentation& presentation : kToolPresentations) {
        tools.append(presentation.tool);
    }
    return tools;
}

QString ToolsPanel::toolDisplayName(ToolId tool)
{
    return translateToolTooltip(presentationForTool(tool).tooltip);
}

IconProvider::StandardIcon ToolsPanel::toolIconType(ToolId tool)
{
    return presentationForTool(tool).icon;
}

bool ToolsPanel::isToolVisible(ToolId tool) const
{
    return toolIdFromValue(static_cast<int>(tool)).has_value() && !m_hiddenTools.contains(tool);
}

void ToolsPanel::setToolVisible(ToolId tool, bool visible)
{
    if (!toolIdFromValue(static_cast<int>(tool)).has_value() || isToolVisible(tool) == visible) {
        return;
    }

    hideToolGroupPopup(false);
    if (visible) {
        m_hiddenTools.removeOne(tool);
    } else {
        m_hiddenTools.append(tool);
    }

    normalizeGroupSelections();
    updateGroupButtons();
    applyToolOrder(true);
    emit panelStateChanged();
}

QJsonObject ToolsPanel::savePanelState() const
{
    QJsonObject state;
    state[QStringLiteral("version")] = PANEL_STATE_VERSION;
    state[QStringLiteral("toolOrder")] = serializedToolOrder(m_toolOrder);
    state[QStringLiteral("hiddenTools")] = serializedHiddenTools(m_hiddenTools);
    return state;
}

void ToolsPanel::restorePanelState(const QJsonObject& state)
{
    const QList<ToolId> restoredOrder = state.contains(QStringLiteral("toolOrder"))
        ? normalizedToolOrder(state.value(QStringLiteral("toolOrder")).toArray())
        : defaultToolOrder();
    const QList<ToolId> restoredHiddenTools = state.contains(QStringLiteral("hiddenTools"))
        ? normalizedHiddenTools(state.value(QStringLiteral("hiddenTools")).toArray())
        : QList<ToolId> {};
    if (restoredOrder == m_toolOrder && restoredHiddenTools == m_hiddenTools) {
        return;
    }

    m_toolOrder = restoredOrder;
    m_hiddenTools = restoredHiddenTools;
    normalizeGroupSelections();
    updateGroupButtons();
    applyToolOrder(false);
}

void ToolsPanel::setActiveTool(ToolId tool)
{
    const bool profileToolSwitch = isToolSwitchProfilingEnabled();
    QElapsedTimer timer;
    if (profileToolSwitch) {
        timer.start();
    }

    const ToolId displayTool = displayToolFor(tool);
    if (isGroupRepresentative(displayTool) && isToolVisible(tool)) {
        m_groupSelections[displayTool] = tool;
    }

    if (m_currentTool != tool) {
        m_currentTool = tool;
        if (m_toolsData.contains(displayTool)) {
            m_toolsData[displayTool].button->setChecked(true);
        }
        updateGroupButtons();
    } else {
        updateGroupButtons();
    }

    if (profileToolSwitch) { }
}

QWidget* ToolsPanel::createContent()
{
    m_contentWidget = new ruwa::ui::widgets::AnimatedFlowWidget(
        ruwa::ui::widgets::AnimatedFlowWidget::LayoutStyle::UniformWrap);
    // AnimatedFlowWidget defaults to content-height sizing for expandable lists.
    // A dock panel instead owns the available height and must let the flow fill it.
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_contentWidget->setTabletTracking(true);
    m_contentWidget->setStyleSheet("background: transparent;");
    auto& style = WidgetStyleManager::instance();
    m_contentWidget->setContentsMargins(style.scaled(BASE_MARGIN_H), style.scaled(BASE_MARGIN_V),
        style.scaled(BASE_MARGIN_H), style.scaled(BASE_MARGIN_V));
    m_contentWidget->setFlowSpacing(style.scaled(BASE_SPACING), style.scaled(BASE_SPACING));
    m_contentWidget->installEventFilter(this);

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);

    connect(m_buttonGroup, &QButtonGroup::idClicked, this, [this](int id) {
        const std::optional<ToolId> requestedTool = toolIdFromValue(id);
        if (!requestedTool) {
            return;
        }
        const ToolId tool = resolveSelectedTool(*requestedTool);
        if (m_currentTool == tool) {
            updateGroupButtons();
            return;
        }
        emit toolRequested(tool);
    });

    // Create all buttons (without adding to any layout yet)
    addTool(ToolId::Hand);
    addTool(ToolId::Brush);
    addTool(ToolId::Eraser);
    addGroupTool(ToolId::Fill);
    addTool(ToolId::Eyedropper);
    addGroupTool(ToolId::Blur);
    addTool(ToolId::Move);
    addGroupTool(ToolId::SquareSelection);
    addGroupTool(ToolId::Lasso);
    addTool(ToolId::MagicWand);
    addTool(ToolId::Text);
    addTool(ToolId::RotateView);
    addTool(ToolId::CanvasResize);
    addTool(ToolId::Zoom);

    m_contentCreated = true;
    applyToolOrder(false);

    // Default selection
    if (m_toolsData.contains(m_currentTool)) {
        m_toolsData[m_currentTool].button->setChecked(true);
    } else if (m_toolsData.contains(displayToolFor(m_currentTool))) {
        m_toolsData[displayToolFor(m_currentTool)].button->setChecked(true);
    } else if (m_toolsData.contains(ToolId::Brush)) {
        m_toolsData[ToolId::Brush].button->setChecked(true);
        m_currentTool = ToolId::Brush;
    }

    updateIcons();

    return m_contentWidget;
}

void ToolsPanel::applyToolOrder(bool animate)
{
    if (!m_contentWidget || !m_contentCreated) {
        return;
    }
    QList<ToolId> visibleOrder;
    QList<QWidget*> buttons;
    visibleOrder.reserve(m_toolOrder.size());
    buttons.reserve(m_toolOrder.size());
    for (ToolId tool : m_toolOrder) {
        if (m_toolsData.contains(tool) && isDisplayToolVisible(tool)) {
            visibleOrder.append(tool);
            buttons.append(m_toolsData[tool].button);
        }
    }

    const bool shouldAnimate
        = animate && WidgetStyleManager::instance().animationsEnabled() && isVisible();
    if (!shouldAnimate) {
        for (auto it = m_visibilityAnimations.begin(); it != m_visibilityAnimations.end(); ++it) {
            if (it.value()) {
                it.value()->stop();
                it.value()->deleteLater();
            }
        }
        m_visibilityAnimations.clear();
        m_contentWidget->setItems(buttons, {}, false);
        for (auto it = m_toolsData.begin(); it != m_toolsData.end(); ++it) {
            if (it->button != m_draggedButton) {
                it->button->setGraphicsEffect(nullptr);
            }
            it->button->setVisible(visibleOrder.contains(it.key()));
        }
        m_appliedToolOrder = visibleOrder;
        return;
    }

    for (ToolId tool : m_appliedToolOrder) {
        if (!visibleOrder.contains(tool)) {
            animateToolButtonVisibility(tool, false);
        }
    }
    for (ToolId tool : visibleOrder) {
        if (!m_appliedToolOrder.contains(tool)) {
            animateToolButtonVisibility(tool, true);
        }
    }

    m_contentWidget->setItems(buttons, {}, true);
    m_appliedToolOrder = visibleOrder;
}

void ToolsPanel::cancelToolButtonVisibilityAnimation(ToolId displayTool)
{
    const auto it = m_visibilityAnimations.find(displayTool);
    if (it == m_visibilityAnimations.end()) {
        return;
    }
    if (it.value()) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    m_visibilityAnimations.erase(it);
}

void ToolsPanel::animateToolButtonVisibility(ToolId displayTool, bool visible)
{
    if (!m_toolsData.contains(displayTool)) {
        return;
    }

    ToolButton* button = m_toolsData[displayTool].button;
    qreal startOpacity = visible ? 0.0 : 1.0;
    if (auto* currentEffect = qobject_cast<QGraphicsOpacityEffect*>(button->graphicsEffect())) {
        startOpacity = currentEffect->opacity();
    }
    cancelToolButtonVisibilityAnimation(displayTool);
    if (button != m_draggedButton) {
        button->setGraphicsEffect(nullptr);
    }

    auto* opacityEffect = new QGraphicsOpacityEffect(button);
    opacityEffect->setOpacity(startOpacity);
    button->setGraphicsEffect(opacityEffect);
    button->show();

    auto* animation = new QVariantAnimation(button);
    animation->setDuration(anim::duration(160));
    animation->setEasingCurve(visible ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    animation->setStartValue(startOpacity);
    animation->setEndValue(visible ? 1.0 : 0.0);
    m_visibilityAnimations.insert(displayTool, animation);

    const QPointer<ToolButton> buttonGuard(button);
    const QPointer<QGraphicsOpacityEffect> effectGuard(opacityEffect);
    connect(animation, &QVariantAnimation::valueChanged, this,
        [buttonGuard, effectGuard](const QVariant& value) {
            if (buttonGuard && effectGuard && buttonGuard->graphicsEffect() == effectGuard) {
                effectGuard->setOpacity(value.toReal());
            }
        });
    connect(animation, &QVariantAnimation::finished, this,
        [this, displayTool, visible, buttonGuard, effectGuard, animation]() {
            if (m_visibilityAnimations.value(displayTool).data() != animation) {
                return;
            }
            m_visibilityAnimations.remove(displayTool);
            if (buttonGuard && effectGuard && buttonGuard->graphicsEffect() == effectGuard) {
                buttonGuard->setGraphicsEffect(nullptr);
                buttonGuard->setVisible(visible);
            }
            animation->deleteLater();
        });
    animation->start();
}

bool ToolsPanel::isDisplayToolVisible(ToolId displayTool) const
{
    if (!isGroupRepresentative(displayTool)) {
        return isToolVisible(displayTool);
    }
    return !visibleGroupMembers(displayTool).isEmpty();
}

QList<ToolId> ToolsPanel::visibleGroupMembers(ToolId representative) const
{
    QList<ToolId> visibleTools;
    for (ToolId tool : groupMembers(representative)) {
        if (isToolVisible(tool)) {
            visibleTools.append(tool);
        }
    }
    return visibleTools;
}

void ToolsPanel::normalizeGroupSelections()
{
    for (auto it = m_groupSelections.begin(); it != m_groupSelections.end(); ++it) {
        const QList<ToolId> visibleTools = visibleGroupMembers(it.key());
        if (!visibleTools.isEmpty() && !visibleTools.contains(it.value())) {
            it.value() = visibleTools.front();
        }
    }
}

void ToolsPanel::startToolDrag(ToolId tool, ToolButton* button, const QPoint& globalPos)
{
    if (!button || m_dragActive || m_dragSettling || !m_toolOrder.contains(tool)) {
        return;
    }

    hideToolGroupPopup(true);
    if (auto* groupButton = dynamic_cast<GroupToolButton*>(button)) {
        groupButton->cancelPopupGesture();
    }
    button->cancelPointerInteraction();

    const QPixmap snapshot = button->grab();
    m_dragActive = true;
    m_draggedTool = tool;
    m_draggedButton = button;
    m_dragStartOrder = m_toolOrder;
    m_dragOffset = button->mapToGlobal(QPoint(0, 0)) - globalPos;

    auto* opacityEffect = new QGraphicsOpacityEffect(button);
    opacityEffect->setOpacity(0.25);
    button->setGraphicsEffect(opacityEffect);

    QWidget* topLevel = m_contentWidget->window();
    m_dragGhost = new ruwa::ui::widgets::DragGhostWidget(topLevel);
    m_dragGhost->setSnapshot(snapshot);
    const QPoint sourcePosition = topLevel->mapFromGlobal(button->mapToGlobal(QPoint(0, 0)))
        - m_dragGhost->contentTopLeft();
    m_dragGhost->startFollowing(sourcePosition);
    m_dragGhost->captureBackdrop(topLevel);
    m_dragGhost->show();
    m_dragGhost->raise();
    m_dragGhost->setFollowTarget(ghostTargetPosition(globalPos));

    qApp->installEventFilter(this);
    if (!m_dragCursorOverride) {
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        m_dragCursorOverride = true;
    }
}

void ToolsPanel::updateToolDrag(const QPoint& globalPos)
{
    if (!m_dragActive || !m_dragGhost || !m_contentWidget) {
        return;
    }
    m_dragGhost->setFollowTarget(ghostTargetPosition(globalPos));
    const QPoint contentPos = m_contentWidget->mapFromGlobal(globalPos);
    // Clamp to the panel bounds so the insert position keeps resolving to the
    // nearest slot even once the cursor leaves the panel, instead of freezing.
    const QPoint clampedPos(qBound(0, contentPos.x(), qMax(0, m_contentWidget->width() - 1)),
        qBound(0, contentPos.y(), qMax(0, m_contentWidget->height() - 1)));
    moveDraggedToolTo(toolInsertIndexAt(clampedPos));
}

void ToolsPanel::finishToolDrag(bool accepted, const QPoint& globalPos)
{
    if (!m_dragActive) {
        return;
    }
    updateToolDrag(globalPos);

    qApp->removeEventFilter(this);
    if (m_dragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_dragCursorOverride = false;
    }
    m_dragActive = false;
    m_dragSettling = true;

    const bool orderChanged = accepted && m_toolOrder != m_dragStartOrder;
    if (!accepted) {
        m_toolOrder = m_dragStartOrder;
        applyToolOrder(true);
    } else if (orderChanged) {
        emit panelStateChanged();
    }

    const QRect targetRect = m_contentWidget->itemTargetGeometry(m_toolsData[m_draggedTool].button);
    const QPoint targetGlobal = m_contentWidget->mapToGlobal(targetRect.topLeft());
    QWidget* topLevel = m_contentWidget->window();
    QPoint targetPosition = topLevel->mapFromGlobal(targetGlobal);
    if (m_dragGhost) {
        targetPosition -= m_dragGhost->contentTopLeft();
    }

    QPointer<ToolsPanel> guard(this);
    auto finish = [guard]() {
        if (!guard) {
            return;
        }
        if (guard->m_draggedButton) {
            guard->m_draggedButton->setGraphicsEffect(nullptr);
            guard->m_draggedButton->cancelPointerInteraction();
        }
        if (guard->m_dragGhost) {
            guard->m_dragGhost->deleteLater();
            guard->m_dragGhost = nullptr;
        }
        guard->m_draggedButton = nullptr;
        guard->m_dragStartOrder.clear();
        guard->m_dragSettling = false;
        guard->cancelToolDragCandidate();
    };

    if (!m_dragGhost) {
        finish();
        return;
    }
    m_dragGhost->animateTo(targetPosition,
        accepted ? ruwa::ui::widgets::DragGhostWidget::Transition::Settle
                 : ruwa::ui::widgets::DragGhostWidget::Transition::Return,
        std::move(finish));
}

QPoint ToolsPanel::ghostTargetPosition(const QPoint& globalPos) const
{
    if (!m_dragGhost || !m_contentWidget) {
        return {};
    }
    QWidget* topLevel = m_contentWidget->window();
    return topLevel->mapFromGlobal(globalPos + m_dragOffset) - m_dragGhost->contentTopLeft();
}

void ToolsPanel::cancelToolDragCandidate()
{
    m_dragCandidateButton = nullptr;
    m_dragPressPosition = {};
}

/*
 * The insertion resolver intentionally uses the flow's final target rectangles,
 * not the transient on-screen positions. This keeps fast pointer movement stable
 * while neighbouring tools are still gliding to their new slots.
 */
int ToolsPanel::toolInsertIndexAt(const QPoint& contentPos) const
{
    struct Placement {
        int stationaryIndex = 0;
        QRect rect;
    };

    QList<Placement> placements;
    int stationaryIndex = 0;
    for (ToolId tool : m_toolOrder) {
        if (tool == m_draggedTool || !m_toolsData.contains(tool)) {
            continue;
        }
        const QRect rect = m_contentWidget->itemTargetGeometry(m_toolsData[tool].button);
        if (rect.isValid()) {
            placements.append({ stationaryIndex, rect });
        }
        ++stationaryIndex;
    }

    if (placements.isEmpty()) {
        return 0;
    }
    if (contentPos.y() < placements.front().rect.top()) {
        return 0;
    }
    if (contentPos.y() > placements.back().rect.bottom()) {
        return static_cast<int>(placements.size());
    }

    int closestRowCenter = placements.front().rect.center().y();
    int closestDistance = qAbs(contentPos.y() - closestRowCenter);
    for (const Placement& placement : placements) {
        const int center = placement.rect.center().y();
        const int distance = qAbs(contentPos.y() - center);
        if (distance < closestDistance) {
            closestDistance = distance;
            closestRowCenter = center;
        }
    }

    int indexAfterRow = 0;
    for (const Placement& placement : placements) {
        if (placement.rect.center().y() != closestRowCenter) {
            continue;
        }
        if (contentPos.x() < placement.rect.center().x()) {
            return placement.stationaryIndex;
        }
        indexAfterRow = placement.stationaryIndex + 1;
    }
    return indexAfterRow;
}

void ToolsPanel::moveDraggedToolTo(int insertIndex)
{
    QList<ToolId> reordered = m_toolOrder;
    if (!reordered.removeOne(m_draggedTool)) {
        return;
    }
    reordered.insert(qBound(0, insertIndex, static_cast<int>(reordered.size())), m_draggedTool);
    if (reordered == m_toolOrder) {
        return;
    }
    m_toolOrder = reordered;
    applyToolOrder(true);
}

bool ToolsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (m_dragActive && m_dragGhost) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            updateToolDrag(mouseEvent->globalPosition().toPoint());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                finishToolDrag(true, mouseEvent->globalPosition().toPoint());
                return true;
            }
            break;
        }
        case QEvent::KeyPress: {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                finishToolDrag(false, QCursor::pos());
                return true;
            }
            break;
        }
        case QEvent::ApplicationDeactivate:
            finishToolDrag(false, QCursor::pos());
            break;
        default:
            break;
        }
    }

    if (watched == m_contentWidget && m_contentCreated) {
        if (event->type() == QEvent::Resize) {
            hideToolGroupPopup(true);
        }
    }

    ToolButton* button = qobject_cast<ToolButton*>(watched);
    if (button && m_contentCreated) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && !m_dragSettling) {
                m_dragCandidateButton = button;
                m_dragPressPosition = mouseEvent->position().toPoint();
            }
            break;
        }
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (m_dragCandidateButton == button && (mouseEvent->buttons() & Qt::LeftButton)
                && (mouseEvent->position().toPoint() - m_dragPressPosition).manhattanLength()
                    >= QApplication::startDragDistance()) {
                for (auto it = m_toolsData.cbegin(); it != m_toolsData.cend(); ++it) {
                    if (it->button == button) {
                        startToolDrag(it.key(), button, mouseEvent->globalPosition().toPoint());
                        return true;
                    }
                }
            }
            break;
        }
        case QEvent::MouseButtonRelease:
        case QEvent::Hide:
            cancelToolDragCandidate();
            break;
        default:
            break;
        }
    }

    return DockPanel::eventFilter(watched, event);
}

/*
 * Tool construction and variant-popup behaviour stay independent from the
 * reorder lifecycle below.
 */

void ToolsPanel::addTool(ToolId tool)
{
    const ToolPresentation& presentation = presentationForTool(tool);
    auto* button = new ToolButton(m_contentWidget);
    button->setTabletTracking(true);
    button->setToolTip(translateToolTooltip(presentation.tooltip));
    ruwa::ui::widgets::ToolTipController::setShortcutCommand(
        button, QString::fromLatin1(presentation.commandId));
    button->setCheckable(true);
    button->setIconType(presentation.icon);
    button->setCursor(Qt::PointingHandCursor);
    button->installEventFilter(this);

    m_buttonGroup->addButton(button, static_cast<int>(tool));
    m_toolsData[tool] = { button, presentation.icon };
}

void ToolsPanel::addGroupTool(ToolId tool)
{
    const ToolPresentation& presentation = presentationForTool(tool);
    auto* button = new GroupToolButton(m_contentWidget);
    button->setTabletTracking(true);
    button->setToolTip(translateToolTooltip(presentation.tooltip));
    ruwa::ui::widgets::ToolTipController::setShortcutCommand(
        button, QString::fromLatin1(presentation.commandId));
    button->setCheckable(true);
    button->setIconType(presentation.icon);
    button->setHasGroupIndicator(true);
    button->setCursor(Qt::PointingHandCursor);
    button->installEventFilter(this);
    // Tools with variants handle their own right-click (the variant-selection popup).
    // Opt out of the global ContextMenuSystem so its "No actions for this" menu
    // doesn't overlay the variant popup. Regular ToolButtons keep the generic menu.
    button->setProperty("ruwaContextMenuSystemBypass", true);
    button->setPopupCallback([this, tool, button]() {
        cancelToolDragCandidate();
        openToolGroupPopup(tool, button);
    });

    m_buttonGroup->addButton(button, static_cast<int>(tool));
    m_toolsData[tool] = { button, presentation.icon };
}

void ToolsPanel::onThemeChanged()
{
    updateIcons();
    for (auto& info : m_toolsData) {
        info.button->update();
    }
    if (m_contentCreated) {
        auto& style = WidgetStyleManager::instance();
        m_contentWidget->setContentsMargins(style.scaled(BASE_MARGIN_H),
            style.scaled(BASE_MARGIN_V), style.scaled(BASE_MARGIN_H), style.scaled(BASE_MARGIN_V));
        m_contentWidget->setFlowSpacing(style.scaled(BASE_SPACING), style.scaled(BASE_SPACING));
        applyToolOrder(false);
    }
}

void ToolsPanel::updateIcons()
{
    auto& theme = ThemeManager::instance();

    for (auto it = m_toolsData.begin(); it != m_toolsData.end(); ++it) {
        if (!isGroupRepresentative(it.key())) {
            QIcon sourceIcon = theme.icons().getIcon(it->iconType);
            it->button->setIcon(sourceIcon);
        }
    }
    updateGroupButtons();
}

void ToolsPanel::updateGroupButtons()
{
    for (auto it = m_groupSelections.begin(); it != m_groupSelections.end(); ++it) {
        if (!m_toolsData.contains(it.key())) {
            continue;
        }
        auto& info = m_toolsData[it.key()];
        const ToolId currentGroupTool = it.value();
        const ToolPresentation& presentation = presentationForTool(currentGroupTool);
        const QList<ToolId> visibleTools = visibleGroupMembers(it.key());
        info.button->setToolTip(toolDisplayName(currentGroupTool));
        ruwa::ui::widgets::ToolTipController::setShortcutCommand(
            info.button, QString::fromLatin1(presentation.commandId));
        info.button->setIcon(
            ThemeManager::instance().icons().getIcon(toolIconType(currentGroupTool)));
        info.button->setHasGroupIndicator(!visibleTools.isEmpty());
    }

    if (m_groupPopup && m_groupPopup->isPopupVisible()) {
        m_groupPopup->setActiveTool(m_currentTool);
    }
}

void ToolsPanel::ensureGroupPopup()
{
    if (m_groupPopup) {
        return;
    }

    m_groupPopup = new ToolGroupPopup();
    connect(m_groupPopup, &ToolGroupPopup::toolSelected, this, &ToolsPanel::toolRequested);
}

void ToolsPanel::openToolGroupPopup(ToolId representativeTool, QWidget* anchor)
{
    const QList<ToolId> tools = visibleGroupMembers(representativeTool);
    if (tools.isEmpty()) {
        return;
    }

    // A "No actions for this" menu may already be open from a prior right-click on a
    // regular tool. Since the variant popup is triggered by a separate path, dismiss
    // the global context menu so the two don't overlay each other.
    ruwa::ui::widgets::ContextMenuSystem::instance().hideContextMenuAnimated();

    ensureGroupPopup();

    m_groupPopup->setSide(resolvePopupSide(this, m_canvasPanel));
    m_groupPopup->setLayoutMode(resolvePopupLayoutMode(this));

    QList<ToolGroupPopup::Item> items;
    for (ToolId tool : tools) {
        items.append(ToolGroupPopup::Item {
            .tool = tool,
            .iconType = toolIconType(tool),
            .tooltip = toolDisplayName(tool),
            .commandId = QString::fromLatin1(presentationForTool(tool).commandId),
        });
    }

    m_groupPopup->setItems(items);
    m_groupPopup->setActiveTool(resolveSelectedTool(representativeTool));
    m_groupPopup->showFor(anchor, true);
}

void ToolsPanel::hideToolGroupPopup(bool immediate)
{
    if (!m_groupPopup) {
        return;
    }

    if (immediate) {
        m_groupPopup->hideImmediate();
    } else {
        m_groupPopup->hideAnimated();
    }
}

ToolId ToolsPanel::resolveSelectedTool(ToolId tool) const
{
    const ToolId representative = displayToolFor(tool);
    if (!isGroupRepresentative(representative)) {
        return tool;
    }
    return m_groupSelections.value(representative, representative);
}

ToolId ToolsPanel::displayToolFor(ToolId tool) const
{
    return groupRepresentativeForTool(tool);
}

} // namespace ruwa::ui::workspace
