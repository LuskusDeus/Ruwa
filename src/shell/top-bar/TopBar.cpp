// SPDX-License-Identifier: MPL-2.0

// TopBar.cpp
#include "TopBar.h"
#include "LayoutPresetsPopup.h"
#include "MenuPopup.h"
#include "MessagePopup.h"
#include "OverlayContainer.h"
#include "commands/CommandExecutor.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "shell/tab-system/WorkspaceTab.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/resources/IconProvider.h"

#include <QCoreApplication>
#include "shared/widgets/Separator.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QTimer>
#include <QApplication>
#include <QCursor>
#include <QString>
#include <QWindow>
#include <QResizeEvent>
namespace ruwa::ui::widgets {

namespace {
/// Base outer gutter (logical px); actual inset is theme-scaled for HiDPI parity with bar content.
constexpr int kTopBarVisualInsetBase = 6;

/// Hover/press background insets (logical px); hit-testing unchanged. Vertical matches bar air;
/// horizontal uses a larger value on the side that touches the top bar panel edge (logo left, close
/// right).
constexpr int kTopBarControlHoverBgInsetVBase = 3;
constexpr int kTopBarControlHoverBgInsetHInnerBase = 3;
/// Toward top bar panel edge (logo left, close right).
constexpr int kTopBarControlHoverBgInsetHEdgeBase = 6;
/// Minimize / maximize: symmetric horizontal, between inner and edge.
constexpr int kTopBarControlHoverBgInsetHMidBase = 5;
constexpr int kTopBarLogoLeftPaddingBase = 4;
constexpr qreal kMessagePopupGlowContourPhase = 0.68;

QWidget* deepestVisibleChildAt(QWidget* parent, const QPoint& posInParent)
{
    if (!parent || !parent->isVisible()) {
        return nullptr;
    }
    if (!parent->rect().contains(posInParent)) {
        return nullptr;
    }
    QWidget* w = parent;
    QPoint p = posInParent;
    for (int i = 0; i < 64; ++i) {
        QWidget* c = w->childAt(p);
        if (!c || !c->isVisible()) {
            return w;
        }
        p = c->mapFrom(w, p);
        w = c;
    }
    return w;
}
} // namespace

/// QWK registers these with setHitTestVisible. They must receive real mouse hits (no
/// WA_TransparentForMouseEvents — that combination crashed QWK on Windows). Events are mapped
/// to TopBar coordinates and handled by tryForwardFrameGutterMouseEvent (no sendEvent → gutter).
class TopBarGutterBand : public QWidget {
public:
    explicit TopBarGutterBand(TopBar* bar)
        : QWidget(bar)
        , m_bar(bar)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setAttribute(Qt::WA_Hover, true);
    }

protected:
    void mousePressEvent(QMouseEvent* e) override
    {
        if (m_bar->routeMarginMouseFromGutterBand(this, e)) {
            e->accept();
        } else {
            QWidget::mousePressEvent(e);
        }
    }
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (m_bar->routeMarginMouseFromGutterBand(this, e)) {
            e->accept();
        } else {
            QWidget::mouseMoveEvent(e);
        }
    }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        m_bar->routeMarginReleaseFromGutterBand(this, e);
        e->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (m_bar->routeMarginMouseFromGutterBand(this, e)) {
            e->accept();
        } else {
            QWidget::mouseDoubleClickEvent(e);
        }
    }

private:
    TopBar* m_bar = nullptr;
};

// ============================================================================
// LogoButton Implementation
// ============================================================================

LogoButton::LogoButton(QWidget* parent)
    : ruwa::ui::workspace::ToolButton(ruwa::ui::workspace::ToolButton::Mode::Action, parent)
{
    setBaseSquareSize(28, 26);
    setChromeStyle(ruwa::ui::workspace::ToolButton::ChromeStyle::Overlay);
    setChromeInsets(kTopBarControlHoverBgInsetHEdgeBase, kTopBarControlHoverBgInsetVBase,
        kTopBarControlHoverBgInsetHInnerBase, kTopBarControlHoverBgInsetVBase);
    setColorizeIcon(false);
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::TransparentLogoIcon);
}

// ============================================================================
// WindowControlButton Implementation
// ============================================================================

WindowControlButton::WindowControlButton(Type type, QWidget* parent)
    : BaseAnimatedButton(parent)
    , m_type(type)
{
    setCheckable(false);
    setFocusPolicy(Qt::NoFocus);
    setFlat(true);
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    setHoverDuration(140);
    setFixedSize(46, 32);
}

void WindowControlButton::setMaximized(bool maximized)
{
    if (m_isMaximized != maximized) {
        m_isMaximized = maximized;
        update();
    }
}

void WindowControlButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int v = theme.scaled(kTopBarControlHoverBgInsetVBase);
    const int hIn = theme.scaled(kTopBarControlHoverBgInsetHInnerBase);
    const int hEdge = theme.scaled(kTopBarControlHoverBgInsetHEdgeBase);
    const int hMid = theme.scaled(kTopBarControlHoverBgInsetHMidBase);
    int left = hMid;
    int right = hMid;
    if (m_type == Type::Close) {
        right = hEdge;
    }
    const QRectF r = QRectF(rect()).adjusted(left + 0.5, v + 0.5, -right - 0.5, -v - 0.5);
    const qreal radius
        = qMax(2.0, qMin(qreal(theme.scaled(6)), qMin(r.width(), r.height()) / 2.0 - 0.5));
    const qreal hp = hoverProgress();

    painter.setPen(Qt::NoPen);

    if (m_type == Type::Close) {
        const QColor red(232, 17, 35);
        const QColor redHover(232, 17, 35, 230);
        if (isPressed()) {
            painter.setBrush(red);
            painter.drawRoundedRect(r, radius, radius);
        } else if (hp > 0.001) {
            QColor c = redHover;
            c.setAlphaF(c.alphaF() * hp);
            painter.setBrush(c);
            painter.drawRoundedRect(r, radius, radius);
        }
    } else {
        const QColor hoverOverlay = colors.overlay(0.06);
        const QColor pressedOverlay = colors.overlay(0.10);
        if (hp > 0.001) {
            QColor h = hoverOverlay;
            h.setAlphaF(h.alphaF() * hp);
            painter.setBrush(h);
            painter.drawRoundedRect(r, radius, radius);
        }
        if (isPressed()) {
            painter.setBrush(pressedOverlay);
            painter.drawRoundedRect(r, radius, radius);
        }
    }

    const qreal closeHot
        = (m_type == Type::Close) ? qBound(0.0, qMax(hp, isPressed() ? 1.0 : 0.0), 1.0) : 0.0;
    QColor iconColor = colors.textMuted;
    if (closeHot > 0.001) {
        iconColor = QColor(qRound(iconColor.red() * (1.0 - closeHot) + 255 * closeHot),
            qRound(iconColor.green() * (1.0 - closeHot) + 255 * closeHot),
            qRound(iconColor.blue() * (1.0 - closeHot) + 255 * closeHot));
    }

    // Draw icon via IconProvider (theme-aware assets)
    QIcon icon;
    switch (m_type) {
    case Type::Minimize:
        icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
            ruwa::ui::core::IconProvider::StandardIcon::Minimize, iconColor);
        break;
    case Type::Maximize:
        if (m_isMaximized) {
            icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
                ruwa::ui::core::IconProvider::StandardIcon::MinimizeWindow, iconColor);
        } else {
            icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
                ruwa::ui::core::IconProvider::StandardIcon::Maximize, iconColor);
        }
        break;
    case Type::Close:
        icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
            ruwa::ui::core::IconProvider::StandardIcon::Close, iconColor);
        break;
    }

    const int iconSize = theme.scaled(12);
    const QRect iconRect((width() - iconSize) / 2, (height() - iconSize) / 2, iconSize, iconSize);

    if (!icon.isNull()) {
        icon.paint(&painter, iconRect);
        return;
    }

    // Fallback to vector strokes if resource icon is unavailable.
    QRectF fallbackRect(0, 0, 10, 10);
    fallbackRect.moveCenter(rect().center());
    switch (m_type) {
    case Type::Minimize:
        drawMinimizeIcon(painter, fallbackRect, iconColor);
        break;
    case Type::Maximize:
        if (m_isMaximized) {
            drawRestoreIcon(painter, fallbackRect, iconColor);
        } else {
            drawMaximizeIcon(painter, fallbackRect, iconColor);
        }
        break;
    case Type::Close:
        drawCloseIcon(painter, fallbackRect, iconColor);
        break;
    }
}

void WindowControlButton::drawMinimizeIcon(
    QPainter& painter, const QRectF& rect, const QColor& color)
{
    painter.setPen(QPen(color, 1.0));
    painter.drawLine(
        QPointF(rect.left(), rect.center().y()), QPointF(rect.right(), rect.center().y()));
}

void WindowControlButton::drawMaximizeIcon(
    QPainter& painter, const QRectF& rect, const QColor& color)
{
    painter.setPen(QPen(color, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect);
}

void WindowControlButton::drawRestoreIcon(
    QPainter& painter, const QRectF& rect, const QColor& color)
{
    painter.setPen(QPen(color, 1.0));
    painter.setBrush(Qt::NoBrush);

    // Back window (offset up-right)
    QRectF backRect = rect.adjusted(2, -2, 2, -2);

    // Draw only visible parts of back window (top and right edges)
    painter.drawLine(backRect.topLeft(), backRect.topRight());
    painter.drawLine(backRect.topRight(), backRect.bottomRight());
    painter.drawLine(backRect.topLeft(), QPointF(backRect.left(), rect.top()));
    painter.drawLine(QPointF(rect.right(), backRect.bottom()), backRect.bottomRight());

    // Front window
    QRectF frontRect = rect.adjusted(0, 2, -2, 0);
    painter.drawRect(frontRect);
}

void WindowControlButton::drawCloseIcon(QPainter& painter, const QRectF& rect, const QColor& color)
{
    painter.setPen(QPen(color, 1.0));
    painter.drawLine(rect.topLeft(), rect.bottomRight());
    painter.drawLine(rect.topRight(), rect.bottomLeft());
}

// ============================================================================
// LayoutSwitchBarButton Implementation
// ============================================================================

LayoutSwitchBarButton::LayoutSwitchBarButton(QWidget* parent)
    : ruwa::ui::workspace::ToolButton(ruwa::ui::workspace::ToolButton::Mode::Action, parent)
{
    setFocusPolicy(Qt::NoFocus);
    setHoverDuration(140);
    setBaseSize(46, 32, 12);
    setChromeStyle(ruwa::ui::workspace::ToolButton::ChromeStyle::Overlay);
    setChromeInsets(kTopBarControlHoverBgInsetHMidBase, kTopBarControlHoverBgInsetVBase,
        kTopBarControlHoverBgInsetHMidBase, kTopBarControlHoverBgInsetVBase);
    setMutedNormalIcon(true);
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::LayoutSwitch);
}

namespace {

bool gutterWidgetUsesCenteredButtonCoords(const QWidget* w)
{
    return qobject_cast<const LogoButton*>(w) || qobject_cast<const MenuButton*>(w)
        || qobject_cast<const WindowControlButton*>(w)
        || qobject_cast<const LayoutSwitchBarButton*>(w);
}

/// These widgets emit clicked() on release only if pos is inside rect(); QWK/gutter forwarding
/// often supplies wrong global/local pairs, so mapFromGlobal / QCursor::pos() can still miss.
void gutterSnapSynthMouseToWidgetCenter(
    QWidget* w, QEvent::Type typ, QPoint* wLocal, QPoint* global)
{
    if (!w || !wLocal || !global || !gutterWidgetUsesCenteredButtonCoords(w)) {
        return;
    }
    if (typ != QEvent::MouseButtonPress && typ != QEvent::MouseButtonRelease
        && typ != QEvent::MouseButtonDblClick) {
        return;
    }
    *wLocal = w->rect().center();
    *global = w->mapToGlobal(*wLocal);
}

} // namespace

// ============================================================================
// TopBar Implementation
// ============================================================================

TopBar::TopBar(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover, true);

    setupUI();

    m_topGutterBand = new TopBarGutterBand(this);
    m_leftGutterBand = new TopBarGutterBand(this);
    m_rightGutterBand = new TopBarGutterBand(this);
    updateGutterBandGeometries();

    m_gutterClearPoll = new QTimer(this);
    m_gutterClearPoll->setInterval(32);
    connect(m_gutterClearPoll, &QTimer::timeout, this, &TopBar::onGutterHoverPollTimeout);

    m_leaveCloseTimer = new QTimer(this);
    m_leaveCloseTimer->setSingleShot(true);
    connect(m_leaveCloseTimer, &QTimer::timeout, this, &TopBar::onLeaveCloseTimer);

    // Connect to theme changes for scaling support
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &TopBar::onThemeChanged);
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, &TopBar::onLanguageChanged);
}

void TopBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const qreal cornerRadius = theme.scaled(10);
    const int vi = visualInsetPx();

    // Layout uses bottom margin 0; paint panel flush to bottom (only top/side vi inset).
    if (width() <= 2 * vi + 2 || height() <= vi + 2) {
        painter.fillRect(rect(), colors.surface);
        return;
    }

    const QRectF innerRect(
        vi + 0.5, vi + 0.5, qreal(width() - 2 * vi - 1), qreal(height() - vi - 1));
    QPainterPath shape;
    shape.addRoundedRect(innerRect, cornerRadius, cornerRadius);

    QLinearGradient gradient(0, vi, 0, height());
    gradient.setColorAt(0, colors.surface);
    gradient.setColorAt(
        1, ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 100.0 / 102));

    painter.fillRect(rect(), colors.background);
    painter.fillPath(shape, gradient);

    QPen outline(colors.border, 1.0);
    outline.setCosmetic(true);
    outline.setJoinStyle(Qt::RoundJoin);
    outline.setCapStyle(Qt::RoundCap);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);

    // Message popup border glow: continue from popup outer corners along TopBar bottom outline.
    // The popup attachment seam itself is internal and must stay unlit.
    if (m_borderGlowProgress > 0.001 && m_messagePopupGlowRect.isValid()) {
        const qreal popupLeft
            = qBound(innerRect.left(), m_messagePopupGlowRect.left(), innerRect.right());
        const qreal popupRight
            = qBound(innerRect.left(), m_messagePopupGlowRect.right(), innerRect.right());
        if (popupRight > popupLeft) {
            const qreal topBarProgress = qBound(0.0,
                (m_borderGlowProgress - kMessagePopupGlowContourPhase)
                    / (1.0 - kMessagePopupGlowContourPhase),
                1.0);
            if (topBarProgress <= 0.001) {
                painter.setClipping(false);
                return;
            }

            const int alpha = static_cast<int>((1.0 - m_borderGlowProgress) * 220);
            QColor accentColor = ruwa::ui::core::ThemeColors::withAlpha(colors.primary, alpha);

            QPen glowPen;
            glowPen.setColor(accentColor);
            glowPen.setWidthF(2.0);
            glowPen.setCosmetic(true);
            glowPen.setCapStyle(Qt::RoundCap);

            const qreal y = innerRect.bottom();
            const qreal leftSpan = qMax(0.0, popupLeft - innerRect.left());
            const qreal rightSpan = qMax(0.0, innerRect.right() - popupRight);

            QPainterPath topBarGlowPath;
            if (leftSpan > 0.5) {
                topBarGlowPath.moveTo(popupLeft, y);
                topBarGlowPath.lineTo(popupLeft - leftSpan * topBarProgress, y);
            }
            if (rightSpan > 0.5) {
                topBarGlowPath.moveTo(popupRight, y);
                topBarGlowPath.lineTo(popupRight + rightSpan * topBarProgress, y);
            }

            painter.setClipPath(shape);
            painter.setPen(glowPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(topBarGlowPath);
            painter.setClipping(false);
        }
    }
}

void TopBar::setupUI()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setSpacing(0);

    // === Left: Menu buttons ===
    m_buttonContainer = new QWidget(this);
    m_buttonContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_buttonContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    createMenuButtons();
    m_mainLayout->addWidget(m_buttonContainer);

    // Spacer before separator (will be sized in updateScaledSizes)
    m_mainLayout->addSpacing(8);

    // === Separator ===
    m_separator = new ruwa::ui::widgets::Separator(this);
    m_separator->setFixedWidth(1);
    m_mainLayout->addWidget(m_separator);

    // Spacer after separator (will be sized in updateScaledSizes)
    m_mainLayout->addSpacing(8);

    // === Center: Tab bar (expands) ===
    m_tabBarContainer = new QWidget(this);
    m_tabBarContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_tabBarContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_mainLayout->addWidget(m_tabBarContainer, 1);

    // === Right: Window controls ===
    createWindowControls();
    m_mainLayout->addWidget(m_windowControlsContainer);

    // Apply initial scaled sizes
    updateScaledSizes();
}

void TopBar::createMenuButtons()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    QHBoxLayout* btnLayout = new QHBoxLayout(m_buttonContainer);
    btnLayout->setContentsMargins(theme.scaled(kTopBarLogoLeftPaddingBase), 0, 0, 0);
    btnLayout->setSpacing(2);

    // Logo button
    m_logoBtn = new LogoButton(m_buttonContainer);
    connect(m_logoBtn, &QPushButton::clicked, this, &TopBar::homeRequested);
    btnLayout->addWidget(m_logoBtn);

    btnLayout->addSpacing(2); // Minimal gap after logo

    m_fileBtn = createMenuButton(tr("File"));
    m_editBtn = createMenuButton(tr("Edit"));
    m_viewBtn = createMenuButton(tr("View"));
    m_helpBtn = createMenuButton(tr("Help"));

    btnLayout->addWidget(m_fileBtn);
    btnLayout->addWidget(m_editBtn);
    btnLayout->addWidget(m_viewBtn);
    btnLayout->addWidget(m_helpBtn);
}

void TopBar::createWindowControls()
{
    m_windowControlsContainer = new QWidget(this);
    m_windowControlsContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_windowControlsContainer->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    QHBoxLayout* layout = new QHBoxLayout(m_windowControlsContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ruwa::ui::core::ThemeManager::instance().scaled(4));

    m_layoutSwitchBtn = new LayoutSwitchBarButton(m_windowControlsContainer);
    m_layoutSwitchBtn->setToolTip(
        QCoreApplication::translate(metaObject()->className(), "Workspace layout"));
    m_windowControlsSeparator = new ruwa::ui::widgets::Separator(m_windowControlsContainer);
    m_windowControlsSeparator->setFixedWidth(1);

    m_minimizeBtn
        = new WindowControlButton(WindowControlButton::Type::Minimize, m_windowControlsContainer);
    m_maximizeBtn
        = new WindowControlButton(WindowControlButton::Type::Maximize, m_windowControlsContainer);
    m_closeBtn
        = new WindowControlButton(WindowControlButton::Type::Close, m_windowControlsContainer);

    layout->addWidget(m_layoutSwitchBtn);
    layout->addWidget(m_windowControlsSeparator);
    layout->addWidget(m_minimizeBtn);
    layout->addWidget(m_maximizeBtn);
    layout->addWidget(m_closeBtn);

    // Connect signals (QPushButton::clicked — release inside hit area)
    connect(m_layoutSwitchBtn, &QPushButton::clicked, this, &TopBar::onLayoutSwitchClicked);
    connect(m_minimizeBtn, &QPushButton::clicked, this, &TopBar::minimizeRequested);
    connect(m_maximizeBtn, &QPushButton::clicked, this, &TopBar::maximizeRequested);
    connect(m_closeBtn, &QPushButton::clicked, this, &TopBar::closeRequested);
}

MenuButton* TopBar::createMenuButton(const QString& text)
{
    MenuButton* btn = new MenuButton(text, m_buttonContainer);

    // Show menu on hover (immediately, no click required)
    connect(btn, &MenuButton::hoverEntered, this, [this, btn]() {
        if (btn->popup()) {
            showPopupForButton(btn);
        }
        armMenuCloseWatchdog();
    });

    return btn;
}

void TopBar::onWindowStateChanged(Qt::WindowState state)
{
    m_gutterPressGrab.clear();
    clearGutterSyntheticHover();
    bool maximized = (state == Qt::WindowMaximized);
    m_maximizeBtn->setMaximized(maximized);
    update();
}

void TopBar::setPanelsVisibilityState(bool toolsVisible, bool brushesVisible, bool layersVisible,
    bool layerPropertiesVisible, bool layerEffectsVisible, bool colorVisible, bool navigatorVisible)
{
    m_panelsToolsVisible = toolsVisible;
    m_panelsBrushesVisible = brushesVisible;
    m_panelsLayersVisible = layersVisible;
    m_panelsLayerPropertiesVisible = layerPropertiesVisible;
    m_panelsLayerEffectsVisible = layerEffectsVisible;
    m_panelsColorVisible = colorVisible;
    m_panelsNavigatorVisible = navigatorVisible;
}

void TopBar::setCanvasWidgetsVisibilityState(const CanvasWidgetVisibility& visibility)
{
    m_canvasWidgets = visibility;
}

void TopBar::initOverlay(QWidget* centralWidget)
{
    if (!centralWidget)
        return;

    m_menuContainer = centralWidget;
    QWidget* mainWindow = centralWidget->window();

    auto* overlay = OverlayContainer::instance(mainWindow);
    if (overlay) {
        overlay->setExclusionWidget(this);
    }

    m_menuPopup = new MenuPopup(centralWidget);
    if (overlay) {
        overlay->registerPopup(m_menuPopup);
    }

    m_layoutPresetsPopup = new LayoutPresetsPopup(centralWidget);
    if (overlay) {
        overlay->registerLayoutPresetsPopup(m_layoutPresetsPopup);
    }
    connect(m_layoutPresetsPopup, &LayoutPresetsPopup::presetChosen, this,
        &TopBar::dockLayoutPresetChosen);
    connect(m_layoutPresetsPopup, &LayoutPresetsPopup::newPresetFromCurrentRequested, this,
        &TopBar::dockLayoutNewPresetFromCurrentRequested);
    connect(m_layoutPresetsPopup, &LayoutPresetsPopup::exportCurrentLayoutRequested, this,
        &TopBar::dockLayoutExportRequested);
    connect(m_layoutPresetsPopup, &LayoutPresetsPopup::importLayoutRequested, this,
        &TopBar::dockLayoutImportRequested);
    connect(
        m_layoutPresetsPopup, &LayoutPresetsPopup::hidden, this, &TopBar::syncChromeOverlayState);

    m_fileBtn->setPopup(m_menuPopup);
    m_editBtn->setPopup(m_menuPopup);
    m_viewBtn->setPopup(m_menuPopup);
    m_helpBtn->setPopup(m_menuPopup);

    setupFileMenu();
    setupEditMenu();
    setupViewMenu();
    setupHelpMenu();

    connect(m_menuPopup, &MenuPopup::hidden, this, &TopBar::onPopupHidden);
    connect(m_menuPopup, &MenuPopup::mouseLeft, this, [this]() { armMenuCloseWatchdog(300); });

    if (MessagePopup* msgPopup = overlay ? overlay->messagePopup() : nullptr) {
        connect(msgPopup, &MessagePopup::shown, this, &TopBar::startMessagePopupBorderGlow);
        connect(msgPopup, &MessagePopup::contentChanged, this,
            [this, msgPopup]() { setBorderGlowProgress(msgPopup->borderGlowProgress()); });
    }

    updateOverlayMessageAnchor();
}

void TopBar::setupFileMenu()
{
    m_fileItems.clear();
    m_fileItems.append({ tr("New..."), "Ctrl+N", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("file.new", {}); } });
    m_fileItems.append({ tr("Open..."), "Ctrl+O", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("file.open", {}); } });
    m_fileItems.append(MenuItem::Separator());
    m_fileItems.append({ tr("Save"), "Ctrl+S", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("file.save", {}); } });
    m_fileItems.append({ tr("Save As..."), "Ctrl+Shift+S", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("file.saveAs", {}); } });
    m_fileItems.append({ tr("Export..."), "Ctrl+Alt+Shift+S", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("file.export", {}); } });
    m_fileItems.append({ tr("Fast Export as PNG"), "Ctrl+Alt+Shift+P", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("file.fastExportPng", {}); } });
    m_fileItems.append({ tr("Import..."), QString(), QIcon(), true, false,
        [this]() { emit fileImportImagesRequested(); } });
    m_fileItems.append(MenuItem::Separator());
    m_fileItems.append({ tr("Close"), "Ctrl+W", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("tab.close", {}); } });
    m_fileItems.append({ tr("Exit"), "Ctrl+Q", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("file.exit", {}); } });
}

void TopBar::setupEditMenu()
{
    m_editItems.clear();
    m_editItems.append({ tr("Undo"), "Ctrl+Z", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("edit.undo", {}); } });
    m_editItems.append({ tr("Redo"), "Ctrl+Shift+Z", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("edit.redo", {}); } });
    m_editItems.append(MenuItem::Separator());
    m_editItems.append({ tr("Cut"), "Ctrl+X", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("edit.cut", {}); } });
    m_editItems.append({ tr("Copy"), "Ctrl+C", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("edit.copy", {}); } });
    m_editItems.append({ tr("Paste"), "Ctrl+V", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("edit.paste", {}); } });
    m_editItems.append(MenuItem::Separator());
    m_editItems.append({ tr("Preferences..."), "Ctrl+K", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("nav.settings", {}); } });
}

void TopBar::setupViewMenu()
{
    m_viewItems.clear();
    m_viewItems.append({ tr("Zoom In"), "Ctrl++", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("view.zoomIn", {}); } });
    m_viewItems.append({ tr("Zoom Out"), "Ctrl+-", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("view.zoomOut", {}); } });
    m_viewItems.append({ tr("Fit to Window"), "Ctrl+0", QIcon(), true, false,
        []() { ruwa::core::CommandExecutor::instance().execute("view.zoomToFit", {}); } });
    m_viewItems.append(MenuItem::Separator());
    // Panels item is added in viewItemsWithEnabledState() to get fresh toggle state
}

MenuItem TopBar::buildPanelsMenuItem()
{
    MenuItem panelsItem;
    panelsItem.text = tr("Panels");
    panelsItem.enabled = true;

    MenuItem toolsItem;
    toolsItem.text = tr("Tools");
    toolsItem.enabled = true;
    toolsItem.isToggle = true;
    toolsItem.checked = m_panelsToolsVisible;
    toolsItem.toggleAction = [this](bool checked) {
        m_panelsToolsVisible = checked;
        emit panelsToolsVisibilityChanged(checked);
    };

    MenuItem brushesItem;
    brushesItem.text = tr("Brushes");
    brushesItem.enabled = true;
    brushesItem.isToggle = true;
    brushesItem.checked = m_panelsBrushesVisible;
    brushesItem.toggleAction = [this](bool checked) {
        m_panelsBrushesVisible = checked;
        emit panelsBrushesVisibilityChanged(checked);
    };

    MenuItem layersItem;
    layersItem.text = tr("Layers");
    layersItem.enabled = true;
    layersItem.isToggle = true;
    layersItem.checked = m_panelsLayersVisible;
    layersItem.toggleAction = [this](bool checked) {
        m_panelsLayersVisible = checked;
        emit panelsLayersVisibilityChanged(checked);
    };

    MenuItem layerPropsItem;
    layerPropsItem.text = tr("Layer Properties");
    layerPropsItem.enabled = true;
    layerPropsItem.isToggle = true;
    layerPropsItem.checked = m_panelsLayerPropertiesVisible;
    layerPropsItem.toggleAction = [this](bool checked) {
        m_panelsLayerPropertiesVisible = checked;
        emit panelsLayerPropertiesVisibilityChanged(checked);
    };

    MenuItem layerEffectsItem;
    layerEffectsItem.text = tr("Layer Effects");
    layerEffectsItem.enabled = true;
    layerEffectsItem.isToggle = true;
    layerEffectsItem.checked = m_panelsLayerEffectsVisible;
    layerEffectsItem.toggleAction = [this](bool checked) {
        m_panelsLayerEffectsVisible = checked;
        emit panelsLayerEffectsVisibilityChanged(checked);
    };

    MenuItem colorItem;
    colorItem.text = tr("Color");
    colorItem.enabled = true;
    colorItem.isToggle = true;
    colorItem.checked = m_panelsColorVisible;
    colorItem.toggleAction = [this](bool checked) {
        m_panelsColorVisible = checked;
        emit panelsColorVisibilityChanged(checked);
    };

    MenuItem navigatorItem;
    navigatorItem.text = tr("Navigator");
    navigatorItem.enabled = true;
    navigatorItem.isToggle = true;
    navigatorItem.checked = m_panelsNavigatorVisible;
    navigatorItem.toggleAction = [this](bool checked) {
        m_panelsNavigatorVisible = checked;
        emit panelsNavigatorVisibilityChanged(checked);
    };

    panelsItem.submenu = { toolsItem, brushesItem, layersItem, layerPropsItem, layerEffectsItem,
        colorItem, navigatorItem };
    return panelsItem;
}

QString TopBar::canvasWidgetLabel(CanvasWidget widget)
{
    switch (widget) {
    case CanvasWidget::Joystick:
        return tr("Joystick");
    case CanvasWidget::BrushControl:
        return tr("Brush Control");
    case CanvasWidget::ToolState:
        return tr("Tool bar");
    }
    return {};
}

MenuItem TopBar::buildCanvasWidgetsMenuItem()
{
    MenuItem canvasWidgetsItem;
    canvasWidgetsItem.text = tr("Canvas Widgets");
    canvasWidgetsItem.enabled = true;

    for (const CanvasWidget widget : kCanvasWidgets) {
        MenuItem item;
        item.text = canvasWidgetLabel(widget);
        item.enabled = true;
        item.isToggle = true;
        item.checked = m_canvasWidgets[widget];
        item.toggleAction = [this, widget](bool checked) {
            m_canvasWidgets[widget] = checked;
            emit canvasWidgetVisibilityChanged(widget, checked);
        };
        canvasWidgetsItem.submenu.append(item);
    }

    return canvasWidgetsItem;
}

void TopBar::setupHelpMenu()
{
    m_helpItems.clear();
    m_helpItems.append({ tr("Documentation"), "F1", QIcon(), true, false,
        [this]() { emit helpDocumentationRequested(); } });
    m_helpItems.append(MenuItem::Separator());
    m_helpItems.append(
        { tr("About Ruwa"), "", QIcon(), true, false, [this]() { emit helpAboutRequested(); } });
}

MenuButton* TopBar::menuButtonAt(const QPoint& globalPos) const
{
    const QWidget* buttons[] = { m_fileBtn, m_editBtn, m_viewBtn, m_helpBtn };
    for (auto* btn : buttons) {
        if (btn && btn->rect().contains(btn->mapFromGlobal(globalPos)))
            return const_cast<MenuButton*>(static_cast<const MenuButton*>(btn));
    }
    return gutterMenuButtonAt(globalPos);
}

MenuButton* TopBar::gutterMenuButtonAt(const QPoint& globalPos) const
{
    const QPoint localPos = mapFromGlobal(globalPos);
    QPoint edgeMapped;
    if (!mapGutterToContentLocal(localPos, &edgeMapped)) {
        return nullptr;
    }

    QPoint inner;
    if (!computeGutterForwardedInnerPoint(localPos, &inner)) {
        return nullptr;
    }

    QWidget* target = resolveGutterEventTarget(inner);
    while (target) {
        if (target == m_fileBtn || target == m_editBtn || target == m_viewBtn
            || target == m_helpBtn) {
            return qobject_cast<MenuButton*>(target);
        }
        target = target->parentWidget();
    }
    return nullptr;
}

bool TopBar::isMenuOrButton(QWidget* widget) const
{
    if (!widget)
        return false;
    while (widget) {
        if (widget == m_fileBtn || widget == m_editBtn || widget == m_viewBtn
            || widget == m_helpBtn)
            return true;
        if (widget == m_menuPopup)
            return true;
        if (m_menuPopup && m_menuPopup->submenuPopup() && widget == m_menuPopup->submenuPopup())
            return true;
        widget = widget->parentWidget();
    }
    return false;
}

bool TopBar::isMenuPopupSessionOpen() const
{
    if (!m_menuPopup) {
        return false;
    }

    if (m_menuPopup->isVisible() || m_menuPopup->isPopupVisible() || m_menuPopup->isHiding()) {
        return true;
    }

    MenuPopup* submenu = m_menuPopup->submenuPopup();
    return submenu && (submenu->isVisible() || submenu->isPopupVisible() || submenu->isHiding());
}

bool TopBar::isCursorOverMenuChrome(const QPoint& globalPos) const
{
    if (menuButtonAt(globalPos)) {
        return true;
    }
    if (m_menuPopup && m_menuPopup->isVisible()
        && m_menuPopup->rect().contains(m_menuPopup->mapFromGlobal(globalPos))) {
        return true;
    }
    if (m_menuPopup && m_menuPopup->isSubmenuVisible() && m_menuPopup->submenuPopup()
        && m_menuPopup->submenuPopup()->rect().contains(
            m_menuPopup->submenuPopup()->mapFromGlobal(globalPos))) {
        return true;
    }
    return false;
}

void TopBar::armMenuCloseWatchdog(int delayMs)
{
    if (!isMenuPopupSessionOpen()) {
        m_leaveCloseTimer->stop();
        return;
    }
    m_leaveCloseTimer->start(qMax(1, delayMs));
}

bool TopBar::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (m_layoutPresetsPopup && m_layoutPresetsPopup->isPopupVisible()
        && event->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(event);
        QWidget* clicked = QApplication::widgetAt(me->globalPosition().toPoint());
        if (!isLayoutPresetsChromeWidget(clicked)) {
            m_layoutPresetsPopup->hidePopup();
        }
    }

    if (isMenuPopupSessionOpen()) {
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::HoverMove) {
            const QPoint globalPos = QCursor::pos();

            if (MenuButton* btn = menuButtonAt(globalPos)) {
                showPopupForButton(btn);
                armMenuCloseWatchdog();
            } else if (isCursorOverMenuChrome(globalPos)) {
                armMenuCloseWatchdog();
            } else if (!m_leaveCloseTimer->isActive()) {
                // Fallback close path: hide even if popup leaveEvent was missed.
                armMenuCloseWatchdog(220);
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            QWidget* clicked = QApplication::widgetAt(me->globalPosition().toPoint());
            if (!isMenuOrButton(clicked)) {
                hideAllPopups();
            }
        }
    }
    return false;
}

void TopBar::showPopupForButton(MenuButton* button)
{
    if (!button || !button->popup() || !m_menuContainer)
        return;

    if (m_layoutPresetsPopup
        && (m_layoutPresetsPopup->isPopupVisible() || m_layoutPresetsPopup->isHiding())) {
        m_layoutPresetsPopup->forceHide();
    }

    const QList<MenuItem> items = getItemsForButton(button);

    // Switch: menu already visible, hover different button - smooth transition (like ColorPicker)
    if (m_menuPopup->isPopupVisible() && !m_menuPopup->isHiding()) {
        if (m_currentMenuButton == button)
            return;
        m_currentMenuButton = button;
        m_fileBtn->setMenuActive(button == m_fileBtn);
        m_editBtn->setMenuActive(button == m_editBtn);
        m_viewBtn->setMenuActive(button == m_viewBtn);
        m_helpBtn->setMenuActive(button == m_helpBtn);
        m_menuPopup->switchTo(button, items);
        armMenuCloseWatchdog();
        return;
    }

    if (m_menuPopup->isHiding()) {
        m_menuPopup->forceHide();
    }

    m_currentMenuButton = button;
    m_fileBtn->setMenuActive(button == m_fileBtn);
    m_editBtn->setMenuActive(button == m_editBtn);
    m_viewBtn->setMenuActive(button == m_viewBtn);
    m_helpBtn->setMenuActive(button == m_helpBtn);

    bool isFirstShow = !isMenuPopupSessionOpen();
    m_anyMenuOpen = true;
    qApp->installEventFilter(this);

    if (auto* overlay = OverlayContainer::instance(m_menuContainer->window())) {
        overlay->showOverlay();
    }

    m_menuPopup->setItems(items);
    m_menuPopup->showBelow(button, isFirstShow);
    armMenuCloseWatchdog();
}

void TopBar::hideAllPopups()
{
    if (m_layoutPresetsPopup
        && (m_layoutPresetsPopup->isPopupVisible() || m_layoutPresetsPopup->isHiding())) {
        m_layoutPresetsPopup->forceHide();
    }

    if (m_menuPopup) {
        m_menuPopup->hidePopup();
    }

    m_leaveCloseTimer->stop();
    m_anyMenuOpen = false;
    m_currentMenuButton = nullptr;
    updateButtonStates();
    syncChromeOverlayState();
}

void TopBar::onLeaveCloseTimer()
{
    if (!isMenuPopupSessionOpen()) {
        return;
    }

    QPoint globalPos = QCursor::pos();
    if (MenuButton* btn = menuButtonAt(globalPos)) {
        showPopupForButton(btn);
        armMenuCloseWatchdog();
        return;
    }

    // Keep polling while cursor remains inside the menu chrome so a missed leaveEvent
    // cannot leave the popup stuck open indefinitely.
    if (isCursorOverMenuChrome(globalPos)) {
        armMenuCloseWatchdog();
        return;
    }
    hideAllPopups();
}

void TopBar::onPopupHidden()
{
    syncChromeOverlayState();
}

void TopBar::syncChromeOverlayState()
{
    const bool menuUp = isMenuPopupSessionOpen();
    const bool layoutUp = m_layoutPresetsPopup
        && (m_layoutPresetsPopup->isVisible() || m_layoutPresetsPopup->isPopupVisible()
            || m_layoutPresetsPopup->isHiding());

    if (menuUp || layoutUp) {
        return;
    }

    qApp->removeEventFilter(this);
    if (auto* ovl
        = OverlayContainer::instance(m_menuContainer ? m_menuContainer->window() : nullptr)) {
        ovl->hideOverlay();
    }
    m_anyMenuOpen = false;
    m_currentMenuButton = nullptr;
    updateButtonStates();
}

void TopBar::onLayoutSwitchClicked()
{
    if (!m_layoutPresetsPopup || !m_layoutSwitchBtn || !m_layoutSwitchBtn->isEnabled()) {
        return;
    }

    if (m_anyMenuOpen) {
        hideAllPopups();
    }

    if (m_layoutPresetsPopup->isPopupVisible() || m_layoutPresetsPopup->isHiding()) {
        m_layoutPresetsPopup->hidePopup();
        return;
    }

    qApp->installEventFilter(this);
    if (auto* overlay
        = OverlayContainer::instance(m_menuContainer ? m_menuContainer->window() : nullptr)) {
        overlay->showOverlay();
    }
    m_layoutPresetsPopup->showBelow(m_layoutSwitchBtn, true);
}

bool TopBar::isLayoutPresetsChromeWidget(const QWidget* widget) const
{
    const QWidget* w = widget;
    while (w) {
        if (w == m_layoutSwitchBtn) {
            return true;
        }
        if (m_layoutPresetsPopup && w == m_layoutPresetsPopup) {
            return true;
        }
        w = w->parentWidget();
    }
    return false;
}

void TopBar::setBorderGlowProgress(qreal progress)
{
    m_borderGlowProgress = qBound(0.0, progress, 1.0);
    updateMessagePopupGlowGeometry();
    update();
}

void TopBar::startMessagePopupBorderGlow()
{
    updateMessagePopupGlowGeometry();
    setBorderGlowProgress(0.0);
}

void TopBar::updateButtonStates()
{
    bool menuVisible = m_menuPopup && m_menuPopup->isPopupVisible();
    m_fileBtn->setMenuActive(menuVisible && m_currentMenuButton == m_fileBtn);
    m_editBtn->setMenuActive(menuVisible && m_currentMenuButton == m_editBtn);
    m_viewBtn->setMenuActive(menuVisible && m_currentMenuButton == m_viewBtn);
    m_helpBtn->setMenuActive(menuVisible && m_currentMenuButton == m_helpBtn);
}

bool TopBar::isInWorkspace() const
{
    return ruwa::core::CommandExecutor::instance()
               .context()
               .activeTabAs<ruwa::ui::tabs::WorkspaceTab>()
        != nullptr;
}

bool TopBar::isActiveWorkspaceInExportMode() const
{
    auto* workspaceTab = ruwa::core::CommandExecutor::instance()
                             .context()
                             .activeTabAs<ruwa::ui::tabs::WorkspaceTab>();
    if (!workspaceTab) {
        return false;
    }
    auto* canvasPanel = workspaceTab->canvasPanel();
    return canvasPanel && canvasPanel->isExportMode();
}

QList<MenuItem> TopBar::fileItemsWithEnabledState() const
{
    QList<MenuItem> items = m_fileItems;
    const bool inWorkspace = isInWorkspace();
    // Indices: 0=New, 1=Open, 2=sep, 3=Save, 4=Save As, 5=Export, 6=Import, 7=sep, 8=Close, 9=Exit
    if (inWorkspace)
        return items;
    if (items.size() > 6)
        items[6].enabled = false; // Import
    if (items.size() > 5)
        items[5].enabled = false; // Export
    if (items.size() > 4)
        items[4].enabled = false; // Save As
    if (items.size() > 3)
        items[3].enabled = false; // Save
    return items;
}

QList<MenuItem> TopBar::editItemsWithEnabledState() const
{
    QList<MenuItem> items = m_editItems;
    const bool inWorkspace = isInWorkspace();
    // Indices: 0=Undo, 1=Redo, 2=sep, 3=Cut, 4=Copy, 5=Paste, 6=sep, 7=Preferences
    if (inWorkspace)
        return items;
    if (items.size() > 5)
        items[5].enabled = false; // Paste
    if (items.size() > 4)
        items[4].enabled = false; // Copy
    if (items.size() > 3)
        items[3].enabled = false; // Cut
    if (items.size() > 1)
        items[1].enabled = false; // Redo
    if (items.size() > 0)
        items[0].enabled = false; // Undo
    return items;
}

QList<MenuItem> TopBar::viewItemsWithEnabledState()
{
    QList<MenuItem> items = m_viewItems;
    items.append(buildPanelsMenuItem());
    items.append(buildCanvasWidgetsMenuItem());
    const bool inWorkspace = isInWorkspace();
    const bool exportModeActive = isActiveWorkspaceInExportMode();
    // Indices: 0=Zoom In, 1=Zoom Out, 2=Fit to Window, 3=sep, 4=Panels, 5=Canvas widgets
    if (inWorkspace) {
        if (exportModeActive) {
            for (int i = 0; i < items.size() && i <= 2; ++i) {
                if (!items[i].separator) {
                    items[i].enabled = false;
                }
            }
        }
        return items;
    }
    for (int i = 0; i < items.size(); ++i) {
        if (items[i].separator)
            continue;
        if (i <= 2) {
            items[i].enabled = false; // Zoom In, Zoom Out, Fit to Window
        } else if (items[i].hasSubmenu()) {
            items[i].enabled = false;
            for (int j = 0; j < items[i].submenu.size(); ++j) {
                if (!items[i].submenu[j].separator) {
                    items[i].submenu[j].enabled = false;
                }
            }
        }
    }
    return items;
}

QList<MenuItem> TopBar::getItemsForButton(MenuButton* button)
{
    if (button == m_fileBtn)
        return fileItemsWithEnabledState();
    if (button == m_editBtn)
        return editItemsWithEnabledState();
    if (button == m_viewBtn)
        return viewItemsWithEnabledState();
    if (button == m_helpBtn)
        return m_helpItems;
    return {};
}

int TopBar::visualInsetPx() const
{
    return ruwa::ui::core::ThemeManager::instance().scaled(kTopBarVisualInsetBase);
}

void TopBar::setCompactMode(bool compact)
{
    m_compactMode = compact;

    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    const int g = visualInsetPx();
    const int bottomInset = 1;
    // Keep only a tiny bottom reserve so the child controls do not clip on small UI scales.
    if (compact) {
        setFixedHeight(theme.scaled(32) + g + bottomInset);
        m_mainLayout->setContentsMargins(g, g, g, bottomInset);

        if (auto* btnLayout = qobject_cast<QHBoxLayout*>(m_buttonContainer->layout())) {
            btnLayout->setContentsMargins(theme.scaled(kTopBarLogoLeftPaddingBase), 0, 0, 0);
            btnLayout->setSpacing(theme.scaled(1));
        }
    } else {
        setFixedHeight(theme.scaled(36) + g + bottomInset);
        m_mainLayout->setContentsMargins(g, g, g, bottomInset);

        if (auto* btnLayout = qobject_cast<QHBoxLayout*>(m_buttonContainer->layout())) {
            btnLayout->setContentsMargins(theme.scaled(kTopBarLogoLeftPaddingBase), 0, 0, 0);
            btnLayout->setSpacing(theme.scaled(2));
        }
    }

    update();
    updateOverlayMessageAnchor();
}

void TopBar::setLayoutSwitchEnabled(bool enabled)
{
    if (!m_layoutSwitchBtn) {
        return;
    }
    m_layoutSwitchBtn->setEnabled(enabled);
    m_layoutSwitchBtn->setCursor(enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void TopBar::updateOverlayMessageAnchor()
{
    QWidget* win = window();
    if (!win) {
        return;
    }
    OverlayContainer* overlay = OverlayContainer::instance(win);
    if (!overlay) {
        return;
    }
    const QPoint bottomInWin = mapTo(win, QPoint(0, height()));
    overlay->setMessagePopupAnchorY(qMax(0, bottomInWin.y() - 1));
}

void TopBar::updateMessagePopupGlowGeometry()
{
    m_messagePopupGlowRect = {};

    QWidget* win = window();
    if (!win) {
        return;
    }

    OverlayContainer* overlay = OverlayContainer::instance(win);
    MessagePopup* popup = overlay ? overlay->messagePopup() : nullptr;
    if (!popup || !popup->isVisible()) {
        return;
    }

    const QRectF popupBodyRect = popup->bodyRect();
    if (!popupBodyRect.isValid()) {
        return;
    }

    const QPoint popupTopLeft
        = mapFromGlobal(popup->mapToGlobal(popupBodyRect.topLeft().toPoint()));
    m_messagePopupGlowRect = QRectF(QPointF(popupTopLeft),
        QSizeF(popupBodyRect.width(), qMax(qreal(1), popupBodyRect.height())));
}

void TopBar::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    const int g = visualInsetPx();
    const int bottomInset = 1;
    if (m_compactMode) {
        setFixedHeight(theme.scaled(32) + g + bottomInset);
        m_mainLayout->setContentsMargins(g, g, g, bottomInset);
    } else {
        setFixedHeight(theme.scaled(36) + g + bottomInset);
        m_mainLayout->setContentsMargins(g, g, g, bottomInset);
    }

    // Update button container spacing
    if (auto* btnLayout = qobject_cast<QHBoxLayout*>(m_buttonContainer->layout())) {
        btnLayout->setContentsMargins(theme.scaled(kTopBarLogoLeftPaddingBase), 0, 0, 0);
        if (m_compactMode) {
            btnLayout->setSpacing(theme.scaled(1));
        } else {
            btnLayout->setSpacing(theme.scaled(2));
        }
    }

    // Update logo button size
    if (m_logoBtn) {
        int size = theme.scaled(28);
        m_logoBtn->setFixedSize(size, size);
    }

    if (m_layoutSwitchBtn && m_minimizeBtn && m_maximizeBtn && m_closeBtn) {
        const int bw = theme.scaled(46);
        const int bh = theme.scaled(32);
        m_layoutSwitchBtn->setFixedSize(bw, bh);
        m_minimizeBtn->setFixedSize(bw, bh);
        m_maximizeBtn->setFixedSize(bw, bh);
        m_closeBtn->setFixedSize(bw, bh);
    }
    if (m_windowControlsContainer) {
        if (auto* wl = qobject_cast<QHBoxLayout*>(m_windowControlsContainer->layout())) {
            wl->setSpacing(theme.scaled(4));
        }
    }

    // Update spacers in main layout
    // Find and update spacing items
    for (int i = 0; i < m_mainLayout->count(); ++i) {
        QLayoutItem* item = m_mainLayout->itemAt(i);
        if (item && item->spacerItem()) {
            // Update spacer size to scaled value (8px)
            QSpacerItem* spacer = item->spacerItem();
            spacer->changeSize(theme.scaled(8), 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        }
    }

    m_mainLayout->invalidate();
    updateGutterBandGeometries();
    updateOverlayMessageAnchor();
}

QList<QWidget*> TopBar::qwkExtraHitTestWidgets() const
{
    QList<QWidget*> out;
    if (m_topGutterBand) {
        out.append(m_topGutterBand);
    }
    if (m_leftGutterBand) {
        out.append(m_leftGutterBand);
    }
    if (m_rightGutterBand && m_rightGutterBand->isVisible()) {
        out.append(m_rightGutterBand);
    }
    return out;
}

void TopBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateGutterBandGeometries();
    updateOverlayMessageAnchor();
}

bool TopBar::isGutterBandWidget(const QWidget* w) const
{
    if (!w) {
        return false;
    }
    return w == m_topGutterBand || w == m_leftGutterBand || w == m_rightGutterBand;
}

QWidget* TopBar::resolveGutterEventTarget(const QPoint& p) const
{
    // Never use TopBar::childAt here — gutters are direct children and can win hit-testing
    // (and sendEvent → gutter → forward → recursion).
    if (m_windowControlsContainer && m_windowControlsContainer->isVisible()) {
        const QPoint lp = m_windowControlsContainer->mapFrom(this, p);
        if (m_windowControlsContainer->rect().contains(lp)) {
            return deepestVisibleChildAt(m_windowControlsContainer, lp);
        }
    }
    if (m_tabBarContainer && m_tabBarContainer->isVisible()) {
        const QPoint lp = m_tabBarContainer->mapFrom(this, p);
        if (m_tabBarContainer->rect().contains(lp)) {
            return deepestVisibleChildAt(m_tabBarContainer, lp);
        }
    }
    if (m_buttonContainer && m_buttonContainer->isVisible()) {
        const QPoint lp = m_buttonContainer->mapFrom(this, p);
        if (m_buttonContainer->rect().contains(lp)) {
            return deepestVisibleChildAt(m_buttonContainer, lp);
        }
    }
    if (m_separator && m_separator->isVisible()) {
        const QPoint lp = m_separator->mapFrom(this, p);
        if (m_separator->rect().contains(lp)) {
            return m_separator;
        }
    }
    if (m_windowControlsSeparator && m_windowControlsSeparator->isVisible()) {
        const QPoint lp = m_windowControlsSeparator->mapFrom(this, p);
        if (m_windowControlsSeparator->rect().contains(lp)) {
            return m_windowControlsSeparator;
        }
    }
    return nullptr;
}

bool TopBar::computeGutterForwardedInnerPoint(const QPoint& topBarPos, QPoint* outInner) const
{
    const int vi = visualInsetPx();
    const int x0 = topBarPos.x();
    const int y0 = topBarPos.y();
    const int W = width();
    const int H = height();
    if (!outInner || W < vi * 2 + 1 || H < vi + 2) {
        return false;
    }

    if (y0 < vi) {
        int x = qBound(vi, x0, W - 1);
        int y = vi;
        QWidget* tabs = m_tabBarContainer;
        QWidget* menus = m_buttonContainer;
        QRect closeInBar;
        if (m_closeBtn) {
            closeInBar = QRect(m_closeBtn->mapTo(this, QPoint(0, 0)), m_closeBtn->size());
        }
        if (!closeInBar.isEmpty() && x >= closeInBar.left()) {
            y = qBound(vi, closeInBar.center().y(), H - 1);
        } else if (tabs && !tabs->geometry().isEmpty() && !closeInBar.isEmpty()
            && x >= tabs->geometry().left() && x < closeInBar.left()) {
            y = qBound(vi, tabs->geometry().center().y(), H - 1);
        } else if (menus && !menus->geometry().isEmpty()) {
            y = qBound(vi, menus->geometry().center().y(), H - 1);
        } else {
            y = qBound(vi, vi + qMax(1, (H - vi) / 2), H - 1);
        }
        *outInner = QPoint(x, qBound(vi, y, H - 1));
        return true;
    }

    return mapGutterToContentLocal(topBarPos, outInner);
}

bool TopBar::tryForwardFrameGutterMouseEvent(QMouseEvent* e)
{
    // During QApplication::sendEvent to a child, Qt / QWindowKit sometimes synchronously
    // redelivers a mouse event to TopBar (often local/global (0,0)). Treating that as a gutter
    // hit and returning true consumed it without QWidget's default handling and led to abort.
    if (m_inGutterForward) {
        return false;
    }

    QPoint inner;
    if (!computeGutterForwardedInnerPoint(e->pos(), &inner)) {
        return false;
    }

    struct ReentryGuard {
        TopBar* b;
        explicit ReentryGuard(TopBar* bar)
            : b(bar)
        {
            b->m_inGutterForward = true;
        }
        ~ReentryGuard() { b->m_inGutterForward = false; }
    } guard(this);

    const QPoint global = e->globalPosition().toPoint();
    QWidget* w = resolveGutterEventTarget(inner);

    if (!w) {
        armGutterSyntheticHover(nullptr);
        if (e->type() == QEvent::MouseButtonPress && e->button() == Qt::LeftButton) {
            m_gutterPressGrab.clear();
            if (QWidget* tl = window()) {
                if (QWindow* wh = tl->windowHandle()) {
                    wh->startSystemMove();
                }
            }
            e->accept();
        }
        return true;
    }
    if (w->window() != window()) {
        e->accept();
        return true;
    }
    if (isGutterBandWidget(w)) {
        e->accept();
        return true;
    }
    if (!isAncestorOf(w)) {
        e->accept();
        return true;
    }

    armGutterSyntheticHover(w);

    QPoint wLocal = w->mapFromGlobal(global);
    QPoint globalForEvent = global;
    gutterSnapSynthMouseToWidgetCenter(w, e->type(), &wLocal, &globalForEvent);
    QMouseEvent fwd(e->type(), wLocal, globalForEvent, e->button(), e->buttons(), e->modifiers());
    QApplication::sendEvent(w, &fwd);

    if (e->type() == QEvent::MouseButtonPress && e->button() == Qt::LeftButton) {
        m_gutterPressGrab = w;
    }
    e->accept();
    return true;
}

bool TopBar::routeMarginMouseFromGutterBand(TopBarGutterBand* band, QMouseEvent* sourceEvent)
{
    if (!band || !sourceEvent) {
        return false;
    }
    const QPoint barPos = band->mapTo(this, sourceEvent->pos());
    QMouseEvent mapped(sourceEvent->type(), barPos, sourceEvent->globalPosition().toPoint(),
        sourceEvent->button(), sourceEvent->buttons(), sourceEvent->modifiers());
    return tryForwardFrameGutterMouseEvent(&mapped);
}

void TopBar::routeMarginReleaseFromGutterBand(TopBarGutterBand* band, QMouseEvent* sourceEvent)
{
    if (!band || !sourceEvent) {
        return;
    }
    if (m_gutterPressGrab) {
        deliverPendingGutterPressRelease(sourceEvent);
        return;
    }
    routeMarginMouseFromGutterBand(band, sourceEvent);
}

void TopBar::updateGutterBandGeometries()
{
    if (!m_topGutterBand || !m_leftGutterBand || !m_rightGutterBand) {
        return;
    }

    const int vi = visualInsetPx();
    const int W = width();
    const int H = height();
    if (W <= 0 || H <= 0) {
        return;
    }

    m_topGutterBand->setGeometry(0, 0, W, qMin(vi, H));
    m_leftGutterBand->setGeometry(0, vi, vi, qMax(0, H - vi));

    if (m_windowControlsContainer) {
        const int r = m_windowControlsContainer->geometry().right() + 1;
        const int rw = W - r;
        if (rw > 0 && r < W) {
            m_rightGutterBand->setGeometry(r, vi, rw, qMax(0, H - vi));
            m_rightGutterBand->show();
        } else {
            m_rightGutterBand->hide();
        }
    } else {
        m_rightGutterBand->hide();
    }

    m_topGutterBand->raise();
    m_leftGutterBand->raise();
    if (m_rightGutterBand->isVisible()) {
        m_rightGutterBand->raise();
    }
}

bool TopBar::mapGutterToContentLocal(const QPoint& topBarLocal, QPoint* outContentLocal) const
{
    const int vi = visualInsetPx();
    const int x = topBarLocal.x();
    const int y = topBarLocal.y();
    const int W = width();
    const int H = height();
    if (W < vi * 2 + 1 || H < vi + 2) {
        return false;
    }

    if (y < vi) {
        *outContentLocal = QPoint(qBound(vi, x, W - 1), vi);
        return true;
    }
    if (x < vi) {
        *outContentLocal = QPoint(vi, qBound(vi, y, H - 1));
        return true;
    }
    if (x >= W - vi) {
        *outContentLocal = QPoint(W - vi - 1, qBound(vi, y, H - 1));
        return true;
    }
    return false;
}

void TopBar::clearGutterSyntheticHover()
{
    if (m_gutterClearPoll) {
        m_gutterClearPoll->stop();
    }
    QPointer<QWidget> t = m_gutterHoverTarget;
    m_gutterHoverTarget.clear();
    if (!t) {
        return;
    }
    QWidget* win = t->window();
    QWindow* wh = win ? win->windowHandle() : nullptr;
    if (t->isVisible() && wh && wh->isExposed()) {
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(t.data(), &leave);
    }
}

void TopBar::armGutterSyntheticHover(QWidget* w)
{
    if (m_gutterHoverTarget.data() == w) {
        refreshGutterSyntheticHover();
        if (m_gutterClearPoll && !m_gutterClearPoll->isActive()) {
            m_gutterClearPoll->start();
        }
        return;
    }
    clearGutterSyntheticHover();
    if (!w) {
        return;
    }
    m_gutterHoverTarget = w;
    m_gutterClearPoll->start();

    QPointer<QWidget> wp(w);
    QTimer::singleShot(0, this, [this, wp]() {
        if (!wp || m_gutterHoverTarget.data() != wp.data()) {
            return;
        }
        if (!wp->isVisible() || !wp->isEnabled()) {
            return;
        }
        QWidget* win = wp->window();
        QWindow* wh = win ? win->windowHandle() : nullptr;
        if (!wh || !wh->isExposed()) {
            return;
        }
        const QPoint global = QCursor::pos();
        const QPointF lf(wp->mapFromGlobal(global));
        QEnterEvent ee(lf, lf, QPointF(global));
        QApplication::sendEvent(wp.data(), &ee);
        refreshGutterSyntheticHover();
    });
}

void TopBar::refreshGutterSyntheticHover()
{
    auto* button = qobject_cast<BaseAnimatedButton*>(m_gutterHoverTarget.data());
    if (!button || !button->isVisible() || !button->isEnabled()) {
        return;
    }
    button->setHovered(true);
}

void TopBar::onGutterHoverPollTimeout()
{
    if (!m_gutterHoverTarget) {
        m_gutterClearPoll->stop();
        return;
    }
    QWidget* win = window();
    QWindow* wh = win ? win->windowHandle() : nullptr;
    if (!win || !win->isVisible() || !wh || !wh->isExposed()) {
        return;
    }

    const QPoint lp = mapFromGlobal(QCursor::pos());
    QPoint dummy;
    if (mapGutterToContentLocal(lp, &dummy)) {
        refreshGutterSyntheticHover();
        return;
    }

    QWidget* hit = resolveGutterEventTarget(lp);
    while (hit) {
        if (hit == m_gutterHoverTarget.data()) {
            m_gutterClearPoll->stop();
            m_gutterHoverTarget.clear();
            return;
        }
        hit = hit->parentWidget();
    }
    if (childAt(lp) == m_gutterHoverTarget.data()) {
        m_gutterClearPoll->stop();
        m_gutterHoverTarget.clear();
        return;
    }
    clearGutterSyntheticHover();
}

void TopBar::deliverPendingGutterPressRelease(QMouseEvent* event)
{
    if (!m_gutterPressGrab) {
        return;
    }
    // Clear grab before sendEvent: Qt may synchronously redeliver release to TopBar / gutter;
    // with grab still set, mouseReleaseEvent would call deliverPending again → stack overflow.
    const QPointer<QWidget> wp = m_gutterPressGrab;
    m_gutterPressGrab.clear();
    if (wp) {
        QPoint global = QCursor::pos();
        QPoint wLocal = wp->mapFromGlobal(global);
        gutterSnapSynthMouseToWidgetCenter(wp.data(), event->type(), &wLocal, &global);
        QMouseEvent rel(
            event->type(), wLocal, global, event->button(), event->buttons(), event->modifiers());
        QApplication::sendEvent(wp.data(), &rel);
    }
    event->accept();
}

void TopBar::mousePressEvent(QMouseEvent* event)
{
    if (tryForwardFrameGutterMouseEvent(event)) {
        return;
    }
    QWidget::mousePressEvent(event);
}

void TopBar::mouseMoveEvent(QMouseEvent* event)
{
    if (tryForwardFrameGutterMouseEvent(event)) {
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void TopBar::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (tryForwardFrameGutterMouseEvent(event)) {
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TopBar::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_gutterPressGrab) {
        deliverPendingGutterPressRelease(event);
        return;
    }
    if (tryForwardFrameGutterMouseEvent(event)) {
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TopBar::leaveEvent(QEvent* event)
{
    m_gutterPressGrab.clear();
    clearGutterSyntheticHover();
    QWidget::leaveEvent(event);
}

void TopBar::hideEvent(QHideEvent* event)
{
    m_gutterPressGrab.clear();
    clearGutterSyntheticHover();
    QWidget::hideEvent(event);
}

void TopBar::onThemeChanged()
{
    updateScaledSizes();
    update();
}

void TopBar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void TopBar::onLanguageChanged()
{
    retranslateUi();
}

void TopBar::retranslateUi()
{
    // Use metaObject()->className() for correct translation context (handles namespaces)
    const char* ctx = metaObject()->className();
    if (m_fileBtn)
        m_fileBtn->setText(QCoreApplication::translate(ctx, "File"));
    if (m_editBtn)
        m_editBtn->setText(QCoreApplication::translate(ctx, "Edit"));
    if (m_viewBtn)
        m_viewBtn->setText(QCoreApplication::translate(ctx, "View"));
    if (m_helpBtn)
        m_helpBtn->setText(QCoreApplication::translate(ctx, "Help"));
    if (m_layoutSwitchBtn) {
        m_layoutSwitchBtn->setToolTip(QCoreApplication::translate(ctx, "Workspace layout"));
    }
    if (m_layoutPresetsPopup) {
        m_layoutPresetsPopup->retranslateUi();
    }

    setupFileMenu();
    setupEditMenu();
    setupViewMenu();
    setupHelpMenu();
}

} // namespace ruwa::ui::widgets
