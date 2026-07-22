// SPDX-License-Identifier: MPL-2.0

// ToolsPanel.cpp
#include "ToolsPanel.h"
#include "ToolGroupPopup.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/layers/ui/LayersPanel.h"
#include "shared/widgets/ToolButton.h"
#include "shell/context-menu/ContextMenuSystem.h"

#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QContextMenuEvent>
#include <QList>
#include <QLayout>
#include <QButtonGroup>
#include <QTimer>
#include <QElapsedTimer>
#include <QCursor>
#include <QEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QtGlobal>
#include <array>
#include <functional>
#include <optional>

namespace ruwa::ui::workspace {

using namespace ruwa::ui::core;

namespace {
const int BASE_PANEL_MIN_WIDTH = 56;
const int BASE_PANEL_MIN_HEIGHT = 56; // content: 1 row + margins
const int BASE_PANEL_HEADER_HEIGHT = 19; // DockPanelTitleBar default
const int BASE_BUTTON_SIZE = 36;
const int BASE_ICON_SIZE = 20;
const int BASE_MARGIN_V = 8;
const int BASE_MARGIN_H = 8;
const int BASE_SPACING = 6;
const int BASE_BORDER_RADIUS = 6;
const int GROUP_HOLD_DELAY_MS = 350;

struct ToolPresentation {
    ToolId tool;
    IconProvider::StandardIcon icon;
    const char* tooltip;
};

constexpr std::array kToolPresentations {
    ToolPresentation { ToolId::Hand, IconProvider::StandardIcon::Hand,
        QT_TRANSLATE_NOOP("ToolsPanel", "Hand (H)") },
    ToolPresentation { ToolId::Brush, IconProvider::StandardIcon::Brush,
        QT_TRANSLATE_NOOP("ToolsPanel", "Brush (B)") },
    ToolPresentation { ToolId::Eraser, IconProvider::StandardIcon::Eraser,
        QT_TRANSLATE_NOOP("ToolsPanel", "Eraser (E)") },
    ToolPresentation { ToolId::Fill, IconProvider::StandardIcon::SmartFillColor,
        QT_TRANSLATE_NOOP("ToolsPanel", "Fill (G)") },
    ToolPresentation { ToolId::ClassicFill, IconProvider::StandardIcon::FillColor,
        QT_TRANSLATE_NOOP("ToolsPanel", "Classic Fill (Shift+G)") },
    ToolPresentation { ToolId::Eyedropper, IconProvider::StandardIcon::Eyedropper,
        QT_TRANSLATE_NOOP("ToolsPanel", "Eyedropper (I)") },
    ToolPresentation { ToolId::Lasso, IconProvider::StandardIcon::Lasso,
        QT_TRANSLATE_NOOP("ToolsPanel", "Lasso (L)") },
    ToolPresentation { ToolId::LassoFill, IconProvider::StandardIcon::LassoFill,
        QT_TRANSLATE_NOOP("ToolsPanel", "Lasso Fill (Shift+L)") },
    ToolPresentation { ToolId::SquareSelection, IconProvider::StandardIcon::SquareSelection,
        QT_TRANSLATE_NOOP("ToolsPanel", "Square Selection (M)") },
    ToolPresentation { ToolId::CircleSelection, IconProvider::StandardIcon::CircleSelection,
        QT_TRANSLATE_NOOP("ToolsPanel", "Circle Selection (O)") },
    ToolPresentation { ToolId::Move, IconProvider::StandardIcon::Move,
        QT_TRANSLATE_NOOP("ToolsPanel", "Move (V)") },
    ToolPresentation { ToolId::RotateView, IconProvider::StandardIcon::RotateView,
        QT_TRANSLATE_NOOP("ToolsPanel", "Rotate View (R)") },
    ToolPresentation { ToolId::CanvasResize, IconProvider::StandardIcon::Crop,
        QT_TRANSLATE_NOOP("ToolsPanel", "Canvas Resize") },
    ToolPresentation { ToolId::Zoom, IconProvider::StandardIcon::Zoom,
        QT_TRANSLATE_NOOP("ToolsPanel", "Zoom (Z)") },
    ToolPresentation { ToolId::Blur, IconProvider::StandardIcon::Blur,
        QT_TRANSLATE_NOOP("ToolsPanel", "Blur") },
    ToolPresentation { ToolId::Smudge, IconProvider::StandardIcon::Smudge,
        QT_TRANSLATE_NOOP("ToolsPanel", "Smudge") },
    ToolPresentation { ToolId::Liquify, IconProvider::StandardIcon::Liquify,
        QT_TRANSLATE_NOOP("ToolsPanel", "Liquify") },
    ToolPresentation { ToolId::Text, IconProvider::StandardIcon::Text,
        QT_TRANSLATE_NOOP("ToolsPanel", "Text (T)") },
    ToolPresentation { ToolId::MagicWand, IconProvider::StandardIcon::MagicWand,
        QT_TRANSLATE_NOOP("ToolsPanel", "Magic Wand (W)") },
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

bool isGroupRepresentative(ToolId tool)
{
    return tool == ToolId::Blur || tool == ToolId::Fill
        || tool == ToolId::Lasso
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
        return { ToolId::Blur, ToolId::Smudge,
            ToolId::Liquify };
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
                m_isPressed = false;
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
            m_isPressed = false;
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
        m_isPressed = false;
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

// --- Adaptive Separator ---
// Draws either horizontal or vertical line depending on orientation
class AdaptiveSeparator : public QWidget {
public:
    enum class SepOrientation { Horizontal, Vertical };

    explicit AdaptiveSeparator(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
        applyOrientation(SepOrientation::Horizontal);
    }

    void setOrientation(SepOrientation orient)
    {
        if (m_orient == orient)
            return;
        applyOrientation(orient);
    }

    void setMarginSize(int m)
    {
        m_margin = m;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        const auto& colors = ThemeManager::instance().colors();
        QColor color = colors.borderDark();

        if (m_orient == SepOrientation::Horizontal) {
            int y = height() / 2;
            int x1 = m_margin;
            int x2 = width() - m_margin;
            if (x2 > x1) {
                painter.setPen(QPen(color, 1));
                painter.drawLine(x1, y, x2, y);
            }
        } else {
            int x = width() / 2;
            int y1 = m_margin;
            int y2 = height() - m_margin;
            if (y2 > y1) {
                painter.setPen(QPen(color, 1));
                painter.drawLine(x, y1, x, y2);
            }
        }
    }

private:
    void applyOrientation(SepOrientation orient)
    {
        m_orient = orient;
        if (m_orient == SepOrientation::Horizontal) {
            setFixedHeight(5);
            setMaximumHeight(5);
            setMinimumHeight(5);
            setMaximumWidth(QWIDGETSIZE_MAX);
            setMinimumWidth(0);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        } else {
            setFixedWidth(5);
            setMaximumWidth(5);
            setMinimumWidth(5);
            setMaximumHeight(QWIDGETSIZE_MAX);
            setMinimumHeight(0);
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        }
        updateGeometry();
        update();
    }

    SepOrientation m_orient = SepOrientation::Horizontal;
    int m_margin = 4;
};

// ToolId groups for layout (separators between groups)
static const QList<QList<ToolId>> TOOL_GROUPS = {
    { ToolId::Hand, ToolId::RotateView, ToolId::Zoom }, // Navigation
    { ToolId::Brush, ToolId::Eraser, ToolId::Fill,
        ToolId::Eyedropper, ToolId::Blur }, // Drawing
    { ToolId::Move, ToolId::SquareSelection,
        ToolId::Lasso, ToolId::MagicWand }, // Selection and movement
    { ToolId::Text, ToolId::CanvasResize } // Other
};

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
    setPreferredPanelSize(mgr.scaled(56), mgr.scaled(250));

    setClosable(false);
    setFloatable(true);
    setMovable(true);

    m_groupSelections[ToolId::Blur] = ToolId::Blur;
    m_groupSelections[ToolId::Fill] = ToolId::Fill;
    m_groupSelections[ToolId::Lasso] = ToolId::Lasso;
    m_groupSelections[ToolId::SquareSelection] = ToolId::SquareSelection;

    m_layoutAnimationTimer = new QTimer(this);
    m_layoutAnimationTimer->setInterval(16);
    connect(m_layoutAnimationTimer, &QTimer::timeout, this, &ToolsPanel::advanceLayoutAnimation);
}

ToolsPanel::~ToolsPanel()
{
    if (m_layoutAnimationTimer) {
        m_layoutAnimationTimer->stop();
    }
    m_layoutTargets.clear();
    m_layoutTrackedWidgets.clear();
    delete m_groupPopup;
}

void ToolsPanel::setRelatedPanels(CanvasPanel* canvasPanel, LayersPanel* layersPanel)
{
    m_canvasPanel = canvasPanel;
    m_layersPanel = layersPanel;
}

void ToolsPanel::preparePresentationSnapshot()
{
    for (const ToolButtonInfo& info : m_toolsData) {
        if (info.button) {
            info.button->finishVisualTransitions();
        }
    }
}

void ToolsPanel::setActiveTool(ToolId tool)
{
    const bool profileToolSwitch = isToolSwitchProfilingEnabled();
    QElapsedTimer timer;
    if (profileToolSwitch) {
        timer.start();
    }

    const ToolId displayTool = displayToolFor(tool);
    if (isGroupRepresentative(displayTool)) {
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
    m_contentWidget = new QWidget();
    m_contentWidget->setTabletTracking(true);
    m_contentWidget->setStyleSheet("background: transparent;");
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

    // Build layout for current orientation
    rebuildLayout();

    QTimer::singleShot(0, this, [this]() {
        if (!m_contentWidget || !m_contentCreated) {
            return;
        }

        updateOrientation(false);
        positionLayout(false);
        m_layoutBoundsInitialized = !m_contentWidget->geometry().isEmpty();
    });

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

void ToolsPanel::rebuildLayout(bool animate)
{
    if (!m_contentWidget)
        return;

    hideToolGroupPopup(true);

    auto& mgr = WidgetStyleManager::instance();
    int sepMargin = mgr.scaled(4);

    // Remove old layout and child containers (but NOT the buttons themselves).
    // The panel now positions direct children manually so geometry can be animated
    // between layout states during resize.
    if (auto* oldLayout = m_contentWidget->layout()) {
        // Reparent all buttons to m_contentWidget directly so they survive layout destruction
        for (auto& info : m_toolsData) {
            info.button->setParent(m_contentWidget);
            info.button->show();
        }
        // Delete all layout items and child containers
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0))) {
            if (QWidget* w = item->widget()) {
                // Only delete container widgets, not buttons
                bool isButton = false;
                for (auto& info : m_toolsData) {
                    if (info.button == w) {
                        isButton = true;
                        break;
                    }
                }
                if (!isButton) {
                    delete w;
                }
            }
            delete item;
        }
        delete oldLayout;
    }

    for (QWidget* separator : m_separators) {
        stopLayoutAnimation(separator);
        delete separator;
    }
    m_separators.clear();

    const bool horizontal = (m_orientation == Orientation::Horizontal);
    for (int g = 1; g < TOOL_GROUPS.size(); ++g) {
        auto* sep = new AdaptiveSeparator(m_contentWidget);
        sep->setOrientation(horizontal ? AdaptiveSeparator::SepOrientation::Vertical
                                       : AdaptiveSeparator::SepOrientation::Horizontal);
        sep->setMarginSize(sepMargin);
        sep->show();
        m_separators.append(sep);
    }

    for (auto& info : m_toolsData) {
        info.button->setParent(m_contentWidget);
        info.button->show();
        info.button->raise();
    }

    positionLayout(animate);
}

void ToolsPanel::positionLayout(bool animate)
{
    if (!m_contentWidget)
        return;

    auto& mgr = WidgetStyleManager::instance();
    const int margin = mgr.scaled(BASE_MARGIN_H);
    const int spacing = mgr.scaled(BASE_SPACING);
    const int separatorThickness = 5;

    const int contentWidth = m_contentWidget->width() > 0 ? m_contentWidget->width() : width();
    int contentHeight = m_contentWidget->height();
    if (contentHeight <= 0) {
        contentHeight = height() - mgr.scaled(BASE_PANEL_HEADER_HEIGHT);
    }

    int separatorIndex = 0;

    if (m_orientation == Orientation::Horizontal) {
        int x = margin;
        const int availableHeight = qMax(0, contentHeight - margin * 2);

        for (int g = 0; g < TOOL_GROUPS.size(); ++g) {
            if (g > 0 && separatorIndex < m_separators.size()) {
                x += spacing;
                setAnimatedGeometry(m_separators[separatorIndex++],
                    QRect(x, margin, separatorThickness, availableHeight), animate);
                x += separatorThickness + spacing;
            }

            int visibleCount = 0;
            int buttonHeight = 0;
            int buttonWidth = 0;
            for (ToolId tool : TOOL_GROUPS[g]) {
                if (m_toolsData.contains(tool)) {
                    const QSize hint = m_toolsData[tool].button->sizeHint();
                    ++visibleCount;
                    buttonHeight = qMax(buttonHeight, hint.height());
                    buttonWidth = qMax(buttonWidth, hint.width());
                }
            }

            int maxRowsPerColumn = 1;
            if (visibleCount > 0 && buttonHeight > 0) {
                maxRowsPerColumn = qMax(1, (availableHeight + spacing) / (buttonHeight + spacing));
                maxRowsPerColumn = qMin(maxRowsPerColumn, visibleCount);
            }

            int visibleIndex = 0;
            for (ToolId tool : TOOL_GROUPS[g]) {
                if (!m_toolsData.contains(tool)) {
                    continue;
                }

                auto& info = m_toolsData[tool];
                const QSize hint = info.button->sizeHint();
                const int row = visibleIndex % maxRowsPerColumn;
                const int column = visibleIndex / maxRowsPerColumn;
                setAnimatedGeometry(info.button,
                    QRect(QPoint(x + column * (buttonWidth + spacing),
                              margin + row * (buttonHeight + spacing)),
                        hint),
                    animate);
                ++visibleIndex;
            }

            if (visibleCount > 0) {
                const int columns = (visibleCount + maxRowsPerColumn - 1) / maxRowsPerColumn;
                x += columns * buttonWidth + qMax(0, columns - 1) * spacing;
            }
        }

        return;
    }

    const int availableWidth = qMax(0, contentWidth - margin * 2);
    const int rightEdge = margin + availableWidth;
    int y = margin;

    for (int g = 0; g < TOOL_GROUPS.size(); ++g) {
        if (g > 0 && separatorIndex < m_separators.size()) {
            y += spacing;
            setAnimatedGeometry(m_separators[separatorIndex++],
                QRect(margin, y, availableWidth, separatorThickness), animate);
            y += separatorThickness + spacing;
        }

        int x = margin;
        int lineHeight = 0;
        bool hasGroupButtons = false;

        for (ToolId tool : TOOL_GROUPS[g]) {
            if (!m_toolsData.contains(tool)) {
                continue;
            }

            auto& info = m_toolsData[tool];
            const QSize hint = info.button->sizeHint();
            if (hasGroupButtons && x + hint.width() > rightEdge) {
                x = margin;
                y += lineHeight + spacing;
                lineHeight = 0;
            }

            setAnimatedGeometry(info.button, QRect(QPoint(x, y), hint), animate);
            x += hint.width() + spacing;
            lineHeight = qMax(lineHeight, hint.height());
            hasGroupButtons = true;
        }

        if (hasGroupButtons) {
            y += lineHeight;
        }
    }
}

void ToolsPanel::setAnimatedGeometry(QWidget* widget, const QRect& target, bool animate)
{
    if (!widget)
        return;

    widget->show();

    if (!animate || !isVisible() || !m_contentWidget || !m_contentWidget->isVisible()) {
        m_layoutTargets.remove(widget);
        widget->setGeometry(target);
        return;
    }

    const QRect current = widget->geometry();
    if (current == target) {
        m_layoutTargets.remove(widget);
        return;
    }

    if (!m_layoutTrackedWidgets.contains(widget)) {
        m_layoutTrackedWidgets.insert(widget);
        QWidget* key = widget;
        connect(widget, &QObject::destroyed, this, [this, key]() { stopLayoutAnimation(key); });
    }

    m_layoutTargets[widget] = target;
    if (m_layoutAnimationTimer && !m_layoutAnimationTimer->isActive()) {
        m_layoutAnimationTimer->start();
    }
}

void ToolsPanel::advanceLayoutAnimation()
{
    if (m_layoutTargets.isEmpty()) {
        if (m_layoutAnimationTimer) {
            m_layoutAnimationTimer->stop();
        }
        return;
    }

    auto advanceValue = [](int current, int target) {
        const int delta = target - current;
        if (qAbs(delta) <= 1) {
            return target;
        }

        int step = qRound(delta * 0.35);
        if (step == 0) {
            step = delta > 0 ? 1 : -1;
        }
        return current + step;
    };

    for (auto it = m_layoutTargets.begin(); it != m_layoutTargets.end();) {
        QWidget* widget = it.key();
        if (!widget) {
            it = m_layoutTargets.erase(it);
            continue;
        }

        const QRect target = it.value();
        const QRect current = widget->geometry();
        const QRect next(advanceValue(current.x(), target.x()),
            advanceValue(current.y(), target.y()), advanceValue(current.width(), target.width()),
            advanceValue(current.height(), target.height()));

        widget->setGeometry(next);

        if (next == target) {
            it = m_layoutTargets.erase(it);
        } else {
            ++it;
        }
    }

    if (m_layoutTargets.isEmpty() && m_layoutAnimationTimer) {
        m_layoutAnimationTimer->stop();
    }
}

void ToolsPanel::stopLayoutAnimation(QWidget* widget)
{
    m_layoutTargets.remove(widget);
    m_layoutTrackedWidgets.remove(widget);
    if (m_layoutTargets.isEmpty() && m_layoutAnimationTimer) {
        m_layoutAnimationTimer->stop();
    }
}

void ToolsPanel::updateOrientation(bool animate)
{
    if (!m_contentWidget || !m_contentCreated)
        return;

    // Use panel size, not content widget: at resizeEvent/content creation time
    // the content may not yet have valid dimensions from the layout.
    const int w = width();
    const int h = height();
    constexpr int kMinDimension = 20;
    if (w < kMinDimension || h < kMinDimension)
        return;

    Orientation newOrient = (w > h) ? Orientation::Horizontal : Orientation::Vertical;

    if (newOrient != m_orientation) {
        m_orientation = newOrient;
        rebuildLayout(animate);
    }
}

bool ToolsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_contentWidget && m_contentCreated) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest: {
            const Orientation previousOrientation = m_orientation;
            updateOrientation(m_layoutBoundsInitialized);

            if (previousOrientation == m_orientation) {
                const bool animate = m_layoutBoundsInitialized && isVisible()
                    && m_contentWidget->isVisible() && !m_contentWidget->geometry().isEmpty();
                positionLayout(animate);
            }
            m_layoutBoundsInitialized = !m_contentWidget->geometry().isEmpty();
            break;
        }
        default:
            break;
        }
    }

    return DockPanel::eventFilter(watched, event);
}

void ToolsPanel::resizeEvent(QResizeEvent* event)
{
    DockPanel::resizeEvent(event);
    hideToolGroupPopup(true);

    const Orientation previousOrientation = m_orientation;
    updateOrientation(m_layoutBoundsInitialized);

    if (m_contentCreated && previousOrientation == m_orientation) {
        positionLayout(m_layoutBoundsInitialized);
    }
}

void ToolsPanel::addTool(ToolId tool)
{
    const ToolPresentation& presentation = presentationForTool(tool);
    auto* button = new ToolButton(m_contentWidget);
    button->setTabletTracking(true);
    button->setToolTip(tr(presentation.tooltip));
    button->setCheckable(true);
    button->setIconType(presentation.icon);
    button->setCursor(Qt::PointingHandCursor);

    m_buttonGroup->addButton(button, static_cast<int>(tool));
    m_toolsData[tool] = { button, presentation.icon };
}

void ToolsPanel::addGroupTool(ToolId tool)
{
    const ToolPresentation& presentation = presentationForTool(tool);
    auto* button = new GroupToolButton(m_contentWidget);
    button->setTabletTracking(true);
    button->setToolTip(tr(presentation.tooltip));
    button->setCheckable(true);
    button->setIconType(presentation.icon);
    button->setHasGroupIndicator(true);
    button->setCursor(Qt::PointingHandCursor);
    // Tools with variants handle their own right-click (the variant-selection popup).
    // Opt out of the global ContextMenuSystem so its "No actions for this" menu
    // doesn't overlay the variant popup. Regular ToolButtons keep the generic menu.
    button->setProperty("ruwaContextMenuSystemBypass", true);
    button->setPopupCallback([this, tool, button]() { openToolGroupPopup(tool, button); });

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
        positionLayout(false);
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
        info.button->setToolTip(tooltipForTool(currentGroupTool));
        info.button->setIcon(
            ThemeManager::instance().icons().getIcon(iconForTool(currentGroupTool)));
        info.button->setHasGroupIndicator(true);
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
    // A "No actions for this" menu may already be open from a prior right-click on a
    // regular tool. Since the variant popup is triggered by a separate path, dismiss
    // the global context menu so the two don't overlay each other.
    ruwa::ui::widgets::ContextMenuSystem::instance().hideContextMenuAnimated();

    ensureGroupPopup();

    m_groupPopup->setSide(resolvePopupSide(this, m_canvasPanel));
    m_groupPopup->setLayoutMode(resolvePopupLayoutMode(this));

    QList<ToolGroupPopup::Item> items;
    const QList<ToolId> tools = groupMembers(representativeTool);
    for (ToolId tool : tools) {
        items.append(ToolGroupPopup::Item { .tool = tool,
            .iconType = iconForTool(tool),
            .tooltip = tooltipForTool(tool) });
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

QString ToolsPanel::tooltipForTool(ToolId tool) const
{
    return tr(presentationForTool(tool).tooltip);
}

IconProvider::StandardIcon ToolsPanel::iconForTool(ToolId tool) const
{
    return presentationForTool(tool).icon;
}

} // namespace ruwa::ui::workspace
