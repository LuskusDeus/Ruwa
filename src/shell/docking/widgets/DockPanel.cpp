// SPDX-License-Identifier: MPL-2.0

// DockPanel.cpp
#include "DockPanel.h"
#include "DockPanelTitleBar.h"
#include "shell/docking/core/DockManager.h"
#include "shell/docking/core/DockFloatingContainer.h"
#include "shell/docking/core/DockContainerWidget.h"
#include "features/theme/manager/ThemeManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QRegion>
#include <QTimer>
#include <QVariantAnimation>
#include <QEasingCurve>

namespace ruwa::ui::docking {

namespace {

constexpr qreal kDockPanelBorderWidth = 1.0;
constexpr qreal kDockPanelOuterInset = 0.5;
constexpr int kDockPanelContentInset = 1;

QPainterPath buildRoundedPanelPath(const QRectF& rect, const DockPanel::CornerRadii& radii)
{
    if (rect.isEmpty()) {
        return QPainterPath();
    }

    const qreal maxHorizontalRadius = rect.width() / 2.0;
    const qreal maxVerticalRadius = rect.height() / 2.0;
    const qreal topLeft
        = qBound<qreal>(0.0, radii.topLeft, qMin(maxHorizontalRadius, maxVerticalRadius));
    const qreal topRight
        = qBound<qreal>(0.0, radii.topRight, qMin(maxHorizontalRadius, maxVerticalRadius));
    const qreal bottomRight
        = qBound<qreal>(0.0, radii.bottomRight, qMin(maxHorizontalRadius, maxVerticalRadius));
    const qreal bottomLeft
        = qBound<qreal>(0.0, radii.bottomLeft, qMin(maxHorizontalRadius, maxVerticalRadius));

    QPainterPath path;
    path.moveTo(rect.left() + topLeft, rect.top());
    path.lineTo(rect.right() - topRight, rect.top());
    if (topRight > 0.0) {
        path.quadTo(rect.right(), rect.top(), rect.right(), rect.top() + topRight);
    } else {
        path.lineTo(rect.right(), rect.top());
    }

    path.lineTo(rect.right(), rect.bottom() - bottomRight);
    if (bottomRight > 0.0) {
        path.quadTo(rect.right(), rect.bottom(), rect.right() - bottomRight, rect.bottom());
    } else {
        path.lineTo(rect.right(), rect.bottom());
    }

    path.lineTo(rect.left() + bottomLeft, rect.bottom());
    if (bottomLeft > 0.0) {
        path.quadTo(rect.left(), rect.bottom(), rect.left(), rect.bottom() - bottomLeft);
    } else {
        path.lineTo(rect.left(), rect.bottom());
    }

    path.lineTo(rect.left(), rect.top() + topLeft);
    if (topLeft > 0.0) {
        path.quadTo(rect.left(), rect.top(), rect.left() + topLeft, rect.top());
    } else {
        path.lineTo(rect.left(), rect.top());
    }

    path.closeSubpath();
    return path;
}

class DockPanelBorderOverlay final : public QWidget {
public:
    explicit DockPanelBorderOverlay(DockPanel* panel)
        : QWidget(panel)
        , m_panel(panel)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        if (!m_panel) {
            return;
        }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF panelRect(kDockPanelOuterInset, kDockPanelOuterInset,
            width() - (kDockPanelOuterInset * 2.0), height() - (kDockPanelOuterInset * 2.0));
        const QPainterPath panelPath = buildRoundedPanelPath(panelRect, m_panel->cornerRadii());

        QColor borderColor = ruwa::ui::core::ThemeManager::instance().colors().border;
        borderColor = borderColor.darker(133); // 25% darker overlay border
        QPen borderPen(borderColor);
        borderPen.setWidthF(kDockPanelBorderWidth);
        borderPen.setCosmetic(true);
        borderPen.setJoinStyle(Qt::RoundJoin);
        p.setPen(borderPen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(panelPath);
    }

private:
    DockPanel* m_panel = nullptr;
};

class DockPanelContentTransition final : public QWidget {
public:
    explicit DockPanelContentTransition(DockPanel* panel)
        : QWidget(panel)
        , m_panel(panel)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        if (!m_panel || width() <= 0 || height() <= 0) {
            return;
        }

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

        const int capRadius = qMax(0, m_panel->baseCornerRadius() + 6);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Solid title-coloured fillets in the two top corners. They round the
        // bottom of the title bar smoothly into the content below as full filled
        // shapes (surfaceAlt), never an outlined arc. Built manually with the full
        // capRadius — buildRoundedPanelPath() would clamp it to (strip height)/2
        // and visibly shrink the curve.
        const qreal w = width();
        const qreal r = qMin<qreal>(capRadius, w / 2.0);
        if (r > 0.0) {
            QPainterPath fillets;

            fillets.moveTo(r, 0.0);
            fillets.quadTo(0.0, 0.0, 0.0, r);
            fillets.lineTo(0.0, 0.0);
            fillets.closeSubpath();

            fillets.moveTo(w - r, 0.0);
            fillets.quadTo(w, 0.0, w, r);
            fillets.lineTo(w, 0.0);
            fillets.closeSubpath();

            painter.fillPath(fillets, colors.surfaceAlt);
        }

        // Divider between title bar and content: only the straight segment between
        // the corners. The rounded corners are left as the filled fillets above so
        // they read as solid title-coloured shapes rather than a stroked outline.
        const qreal straightLeft = qMin<qreal>(r, w / 2.0);
        const qreal straightRight = qMax<qreal>(w - r, w / 2.0);
        if (straightRight > straightLeft) {
            QPen seamPen(colors.border);
            seamPen.setWidthF(kDockPanelBorderWidth);
            seamPen.setCosmetic(true);
            painter.setPen(seamPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(straightLeft, 0.0), QPointF(straightRight, 0.0));
        }
    }

private:
    DockPanel* m_panel = nullptr;
};

} // namespace

DockPanel::DockPanel(const QString& title, QWidget* parent)
    : QFrame(parent)
    , m_id(generatePanelId())
    , m_title(title)
    , m_persistentKey(title)
    , m_theme(this)
{
    setObjectName(title);
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);

    setupUI();
    setupDockingAnimation();

    // Connect to theme changes. handleThemeChanged() defers the heavy work while
    // this panel is hidden (i.e. lives in a background workspace tab), so a theme
    // change does not re-style every panel of every open project at once.
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &DockPanel::handleThemeChanged);

    applyTheme();
}

DockPanel::~DockPanel()
{
    // Stop animation safely
    if (m_dockingAnimation) {
        m_dockingAnimation->disconnect();
        m_dockingAnimation->stop();
        m_dockingAnimation->deleteLater();
        m_dockingAnimation = nullptr;
    }
}

// ============================================================================
// Title & Icon
// ============================================================================

void DockPanel::setTitle(const QString& title)
{
    if (m_title != title) {
        m_title = title;
        setObjectName(title);

        if (m_titleBar) {
            m_titleBar->updateTitle();
        }

        emit titleChanged(title);
    }
}

void DockPanel::setPersistentKey(const QString& key)
{
    const QString normalized = key.trimmed();
    if (!normalized.isEmpty()) {
        m_persistentKey = normalized;
    }
}

void DockPanel::setIcon(const QIcon& icon)
{
    m_iconType = std::nullopt;
    m_icon = icon;

    if (m_titleBar) {
        m_titleBar->updateIcon();
    }

    emit iconChanged(icon);
}

void DockPanel::setIconType(ruwa::ui::core::IconProvider::StandardIcon iconType)
{
    m_iconType = iconType;
    m_icon = QIcon();

    if (m_titleBar) {
        m_titleBar->updateIcon();
    }

    emit iconChanged(QIcon());
}

// ============================================================================
// Subtitle
// ============================================================================

void DockPanel::setSubtitleWidget(QWidget* widget)
{
    QVBoxLayout* lay = m_bodyLayout;
    if (!lay)
        return;

    // Remove previous subtitle
    if (m_subtitleContainer) {
        lay->removeWidget(m_subtitleContainer);
        m_subtitleContainer->deleteLater();
        m_subtitleContainer = nullptr;
        m_subtitleContent = nullptr;
    }

    if (widget) {
        // Create container that visually extends the title bar
        m_subtitleContainer = new QWidget(this);
        auto* containerLayout = new QVBoxLayout(m_subtitleContainer);
        containerLayout->setContentsMargins(0, 0, 0, 0);
        containerLayout->setSpacing(0);

        widget->setParent(m_subtitleContainer);
        containerLayout->addWidget(widget);
        m_subtitleContent = widget;
        applySubtitleContentLayoutOptions();

        // Insert right after the title bar. The title transition is now an overlay
        // and no longer occupies a layout slot.
        lay->insertWidget(1, m_subtitleContainer);
    }

    applyTheme();
    updateContentTransitionGeometry();
}

void DockPanel::setSubtitleContentMargins(const QMargins& margins)
{
    if (m_subtitleContentMargins == margins) {
        return;
    }

    m_subtitleContentMargins = margins;
    applySubtitleContentLayoutOptions();
}

void DockPanel::setSubtitleContentMargins(int left, int top, int right, int bottom)
{
    setSubtitleContentMargins(QMargins(left, top, right, bottom));
}

void DockPanel::setSubtitleContentSpacing(int spacing)
{
    const int normalizedSpacing = spacing < 0 ? 0 : spacing;
    if (m_subtitleContentSpacing == normalizedSpacing) {
        return;
    }

    m_subtitleContentSpacing = normalizedSpacing;
    applySubtitleContentLayoutOptions();
}

void DockPanel::setSubtitleBackground(const QColor& color)
{
    m_subtitleBg = color;

    // Restyle directly (don't call applyTheme — it triggers onThemeChanged
    // which may call setSubtitleBackground again, causing infinite recursion).
    if (m_subtitleContainer) {
        const auto& c = colors();
        const QColor bg = m_subtitleBg.isValid() ? m_subtitleBg : c.surface;
        m_subtitleContainer->setStyleSheet(QString("background: %1;").arg(bg.name()));
    }
}

void DockPanel::setTitleLeadingWidget(QWidget* widget)
{
    if (m_titleBar) {
        m_titleBar->setLeadingWidget(widget);
    } else if (widget) {
        widget->deleteLater();
    }
}

QWidget* DockPanel::titleLeadingWidget() const
{
    return m_titleBar ? m_titleBar->leadingWidget() : nullptr;
}

void DockPanel::setTitleTrailingWidget(QWidget* widget)
{
    if (m_titleBar) {
        m_titleBar->setTrailingWidget(widget);
    } else if (widget) {
        widget->deleteLater();
    }
}

QWidget* DockPanel::titleTrailingWidget() const
{
    return m_titleBar ? m_titleBar->trailingWidget() : nullptr;
}

void DockPanel::setTitleInteractiveWidgetsVisibleWhenFloating(bool visible)
{
    if (m_titleBar) {
        m_titleBar->setInteractiveWidgetsVisibleWhenFloating(visible);
    }
}

bool DockPanel::titleInteractiveWidgetsVisibleWhenFloating() const
{
    return m_titleBar && m_titleBar->interactiveWidgetsVisibleWhenFloating();
}

void DockPanel::applySubtitleContentLayoutOptions()
{
    if (!m_subtitleContent || !m_subtitleContent->layout()) {
        return;
    }

    auto* contentLayout = m_subtitleContent->layout();
    if (m_subtitleContentMargins.has_value()) {
        contentLayout->setContentsMargins(*m_subtitleContentMargins);
    }
    if (m_subtitleContentSpacing.has_value()) {
        contentLayout->setSpacing(*m_subtitleContentSpacing);
    }
}

// ============================================================================
// Features
// ============================================================================

void DockPanel::setFeatures(PanelFeatures features)
{
    if (m_features != features) {
        m_features = features;

        if (m_titleBar) {
            m_titleBar->updateButtons();
        }

        emit featuresChanged(features);
    }
}

void DockPanel::setClosable(bool closable)
{
    PanelFeatures f = m_features;
    f.setFlag(PanelFeature::Closable, closable);
    setFeatures(f);
}

void DockPanel::setMovable(bool movable)
{
    PanelFeatures f = m_features;
    f.setFlag(PanelFeature::Movable, movable);
    setFeatures(f);
}

void DockPanel::setFloatable(bool floatable)
{
    PanelFeatures f = m_features;
    f.setFlag(PanelFeature::Floatable, floatable);
    setFeatures(f);
}

void DockPanel::setResizable(bool resizable)
{
    PanelFeatures f = m_features;
    f.setFlag(PanelFeature::Resizable, resizable);
    setFeatures(f);
}

void DockPanel::setDockable(bool dockable)
{
    PanelFeatures f = m_features;
    f.setFlag(PanelFeature::Dockable, dockable);
    setFeatures(f);
}

void DockPanel::setTitleBarVisible(bool visible)
{
    if (m_titleBarVisible == visible) {
        return;
    }

    m_titleBarVisible = visible;
    if (m_titleBar) {
        m_titleBar->setVisible(visible);
    }
    if (m_contentTransition) {
        m_contentTransition->setVisible(visible);
    }
    updateContentTransitionGeometry();
    updateOverlayVisibility();

    // Panels without title bar cannot be dragged by design.
    if (!visible && isMovable()) {
        setMovable(false);
    }
}

// ============================================================================
// Size Hints
// ============================================================================

void DockPanel::setSizeHints(const PanelSizeHints& hints)
{
    m_sizeHints = hints;
    updateGeometry();
}

void DockPanel::setMinimumPanelSize(int width, int height)
{
    m_sizeHints.minWidth = width;
    m_sizeHints.minHeight = height;
    setMinimumSize(width, height);
}

void DockPanel::setMaximumPanelSize(int width, int height)
{
    m_sizeHints.maxWidth = width;
    m_sizeHints.maxHeight = height;
    setMaximumSize(width, height);
}

void DockPanel::setPreferredPanelSize(int width, int height)
{
    m_sizeHints.prefWidth = width;
    m_sizeHints.prefHeight = height;
    updateGeometry();
}

void DockPanel::setUserHorizontalDockedWidth(int width)
{
    if (m_sizeHints.userHorizontalDockedWidth != width) {
        m_sizeHints.userHorizontalDockedWidth = width;
        emit userHorizontalDockedWidthChanged(width);
    }
}

void DockPanel::setUserVerticalDockedHeight(int height)
{
    if (m_sizeHints.userVerticalDockedHeight != height) {
        m_sizeHints.userVerticalDockedHeight = height;
        emit userVerticalDockedHeightChanged(height);
    }
}

void DockPanel::setUserDockedSize(int width, int height)
{
    // Legacy method - sets both directions
    bool changed = false;
    if (m_sizeHints.userHorizontalDockedWidth != width) {
        m_sizeHints.userHorizontalDockedWidth = width;
        changed = true;
    }
    if (m_sizeHints.userVerticalDockedHeight != height) {
        m_sizeHints.userVerticalDockedHeight = height;
        changed = true;
    }
    if (changed) {
        emit userDockedSizeChanged(width, height);
    }
}

void DockPanel::setUserFloatingSize(int width, int height)
{
    if (m_sizeHints.userFloatingWidth != width || m_sizeHints.userFloatingHeight != height) {
        m_sizeHints.userFloatingWidth = width;
        m_sizeHints.userFloatingHeight = height;
        emit userFloatingSizeChanged(width, height);
    }
}

int DockPanel::effectiveHorizontalDockedWidth() const
{
    return m_sizeHints.effectiveHorizontalDockedWidth();
}

int DockPanel::effectiveVerticalDockedHeight() const
{
    return m_sizeHints.effectiveVerticalDockedHeight();
}

QSize DockPanel::effectiveDockedSize() const
{
    return QSize(m_sizeHints.effectiveDockedWidth(), m_sizeHints.effectiveDockedHeight());
}

QSize DockPanel::effectiveFloatingSize() const
{
    return QSize(m_sizeHints.effectiveFloatingWidth(), m_sizeHints.effectiveFloatingHeight());
}

QSize DockPanel::minimumSizeHint() const
{
    return QSize(m_sizeHints.minWidth, m_sizeHints.minHeight);
}

QSize DockPanel::sizeHint() const
{
    return QSize(m_sizeHints.prefWidth, m_sizeHints.prefHeight);
}

QJsonObject DockPanel::savePanelState() const
{
    return QJsonObject();
}

void DockPanel::restorePanelState(const QJsonObject& state)
{
    Q_UNUSED(state);
}

// ============================================================================
// Actions
// ============================================================================

void DockPanel::toggleFloating()
{
    if (isFloating()) {
        emit dockRequested();
    } else {
        emit floatRequested();
    }
}

void DockPanel::closePanel()
{
    emit closeRequested();
}

void DockPanel::raise()
{
    QFrame::raise();

    if (m_floatingContainer) {
        m_floatingContainer->raise();
    }
}

// ============================================================================
// Theme
// ============================================================================

const ruwa::ui::core::ThemeColors& DockPanel::colors() const
{
    return ruwa::ui::core::ThemeManager::instance().colors();
}

void DockPanel::onThemeChanged()
{
    // Subclasses can override
}

void DockPanel::setTranslatableTitle(const char* sourceText)
{
    m_titleSource = sourceText;
    if (m_titleSource) {
        // Use the most-derived class name as the translation context so the
        // string resolves against the same context lupdate extracted it under
        // (the subclass where tr()/QT_TR_NOOP appears).
        setTitle(QCoreApplication::translate(metaObject()->className(), m_titleSource));
    }
}

void DockPanel::retranslateUi()
{
    if (m_titleSource) {
        setTitle(QCoreApplication::translate(metaObject()->className(), m_titleSource));
    }
}

void DockPanel::changeEvent(QEvent* event)
{
    QFrame::changeEvent(event);
    if (event && event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void DockPanel::handleThemeChanged()
{
    // Defer while hidden (background tab). isVisible() is false whenever any
    // ancestor is hidden, so a panel in a non-active workspace tab will skip the
    // expensive restyle now and pick it up via flushPendingTheme() on activation.
    if (!isVisible()) {
        m_themePending = true;
        return;
    }
    m_themePending = false;
    applyTheme();
}

void DockPanel::flushPendingTheme()
{
    if (!m_themePending) {
        return;
    }
    m_themePending = false;
    applyTheme();
}

void DockPanel::applyTheme()
{
    const auto& c = colors();

    // Update panel style based on current state
    updatePanelStyle();

    if (m_titleBar) {
        m_titleBar->applyTheme(c);
        m_titleBar->setDrawBottomBorder(false);
    }

    // Style subtitle container
    if (m_subtitleContainer) {
        const QColor bg = m_subtitleBg.isValid() ? m_subtitleBg : c.surface;
        m_subtitleContainer->setStyleSheet(QString("background: %1;").arg(bg.name()));
    }

    if (m_borderOverlay) {
        m_borderOverlay->update();
    }

    if (m_contentTransition) {
        m_contentTransition->update();
    }

    updateContentTransitionGeometry();
    updateOverlayVisibility();

    onThemeChanged();
}

void DockPanel::updatePanelStyle()
{
    // Calculate and apply corner radii
    CornerRadii newRadii = calculateCornerRadii();

    // Only update stylesheet if radii changed
    if (newRadii != m_cornerRadii) {
        m_cornerRadii = newRadii;
    }

    updateBodyMask();
    if (m_borderOverlay) {
        m_borderOverlay->update();
    }
    update();
    updateOverlayVisibility();
}

void DockPanel::updateCornerRadii()
{
    CornerRadii newRadii = calculateCornerRadii();
    if (newRadii != m_cornerRadii) {
        m_cornerRadii = newRadii;
        updatePanelStyle();
    }
}

DockPanel::CornerRadii DockPanel::calculateCornerRadii() const
{
    CornerRadii radii;

    // With the new design (container padding + gaps between panels),
    // all panels have rounded corners on all sides since they don't
    // touch the container edges or each other directly.
    radii.topLeft = m_baseCornerRadius;
    radii.topRight = m_baseCornerRadius;
    radii.bottomLeft = m_baseCornerRadius;
    radii.bottomRight = m_baseCornerRadius;

    return radii;
}

// ============================================================================
// Events
// ============================================================================

void DockPanel::showEvent(QShowEvent* event)
{
    QFrame::showEvent(event);
    ensureContentCreated();

    // Update corner radii on first show
    updateCornerRadii();
    updateContentTransitionGeometry();
    updateOverlayVisibility();
}

void DockPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF panelRect(kDockPanelOuterInset, kDockPanelOuterInset,
        width() - (kDockPanelOuterInset * 2.0), height() - (kDockPanelOuterInset * 2.0));
    const QPainterPath panelPath = buildRoundedPanelPath(panelRect, m_cornerRadii);

    p.setPen(Qt::NoPen);
    p.setBrush(colors().surface);
    p.drawPath(panelPath);
}

void DockPanel::resizeEvent(QResizeEvent* event)
{
    QFrame::resizeEvent(event);

    // Update corner radii when size changes (position in container may have changed)
    if (isDocked()) {
        updateCornerRadii();
    }

    updateBodyMask();
    updateContentTransitionGeometry();
    updateOverlayVisibility();

    if (m_borderOverlay) {
        m_borderOverlay->setGeometry(rect());
        m_borderOverlay->raise();
    }
}

void DockPanel::moveEvent(QMoveEvent* event)
{
    QFrame::moveEvent(event);

    // Update corner radii when position changes
    if (isDocked()) {
        updateCornerRadii();
    }
}

// ============================================================================
// Private
// ============================================================================

void DockPanel::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kDockPanelContentInset, kDockPanelContentInset,
        kDockPanelContentInset, kDockPanelContentInset);
    layout->setSpacing(0);

    m_bodyContainer = new QWidget(this);
    m_bodyContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_bodyContainer->setAutoFillBackground(false);
    layout->addWidget(m_bodyContainer);

    m_bodyLayout = new QVBoxLayout(m_bodyContainer);
    m_bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_bodyLayout->setSpacing(0);

    m_borderOverlay = new DockPanelBorderOverlay(this);
    m_borderOverlay->setGeometry(rect());
    m_borderOverlay->raise();

    // Title bar
    m_titleBar = new DockPanelTitleBar(this);
    m_bodyLayout->addWidget(m_titleBar);
    m_titleBar->setVisible(m_titleBarVisible);

    m_contentTransition = new DockPanelContentTransition(this);
    m_contentTransition->setParent(m_bodyContainer);
    m_contentTransition->setVisible(m_titleBarVisible);

    // Content will be added in ensureContentCreated()
    updateContentTransitionGeometry();
    updateOverlayVisibility();
}

void DockPanel::setState(PanelState state)
{
    if (m_state != state) {
        m_state = state;

        // Update panel style (border-radius depends on state)
        updatePanelStyle();
        updateOverlayVisibility();

        if (m_titleBar) {
            m_titleBar->updateButtons();
            m_titleBar->syncFloatingLayout(state == PanelState::Floating);
        }

        emit stateChanged(state);
    }
}

void DockPanel::setFloatingContainer(DockFloatingContainer* container)
{
    m_floatingContainer = container;
    if (container) {
        setState(PanelState::Floating);
    }
}

void DockPanel::ensureContentCreated()
{
    if (m_contentCreated) {
        return;
    }

    m_content = createContent();

    if (m_content) {
        m_content->setParent(m_bodyContainer ? m_bodyContainer : this);

        if (m_bodyLayout) {
            m_bodyLayout->addWidget(m_content, 1);
        }
    }

    if (m_borderOverlay) {
        m_borderOverlay->raise();
    }

    m_contentCreated = true;
    updateContentTransitionGeometry();
    updateOverlayVisibility();
}

void DockPanel::updateBodyMask()
{
    if (!m_bodyContainer) {
        return;
    }

    const QRect bodyRect = m_bodyContainer->rect();
    if (bodyRect.isEmpty()) {
        m_bodyContainer->clearMask();
        return;
    }

    CornerRadii innerRadii;
    innerRadii.topLeft = qMax(0, static_cast<int>(m_cornerRadii.topLeft - kDockPanelBorderWidth));
    innerRadii.topRight = qMax(0, static_cast<int>(m_cornerRadii.topRight - kDockPanelBorderWidth));
    innerRadii.bottomLeft
        = qMax(0, static_cast<int>(m_cornerRadii.bottomLeft - kDockPanelBorderWidth));
    innerRadii.bottomRight
        = qMax(0, static_cast<int>(m_cornerRadii.bottomRight - kDockPanelBorderWidth));
    QPainterPath path = buildRoundedPanelPath(QRectF(bodyRect), innerRadii);

    const QPolygon maskPolygon = path.toFillPolygon().toPolygon();
    if (maskPolygon.isEmpty()) {
        m_bodyContainer->clearMask();
        return;
    }

    m_bodyContainer->setMask(QRegion(maskPolygon));
}

void DockPanel::updateContentTransitionGeometry()
{
    if (!m_contentTransition || !m_bodyContainer || !m_titleBar) {
        return;
    }

    if (!m_titleBarVisible || !m_titleBar->isVisible()) {
        m_contentTransition->hide();
        return;
    }

    const int transitionHeight = qMax(3, m_baseCornerRadius + 8);
    const QRect titleRect = m_titleBar->geometry();
    const bool titleLayoutReady = titleRect.top() == 0 && titleRect.left() == 0
        && titleRect.width() == m_bodyContainer->width() && titleRect.height() > 0;

    if (!titleLayoutReady) {
        m_contentTransition->hide();
        scheduleContentTransitionGeometryUpdate();
        return;
    }

    const int transitionY = qMax(0, titleRect.bottom());
    const int actualHeight
        = qMin(transitionHeight, qMax(0, m_bodyContainer->height() - transitionY));

    if (actualHeight <= 0) {
        m_contentTransition->hide();
        return;
    }

    m_contentTransition->setGeometry(0, transitionY, m_bodyContainer->width(), actualHeight);
    m_contentTransition->show();
    m_contentTransition->raise();

    if (m_titleBar) {
        m_titleBar->raise();
    }
}

void DockPanel::scheduleContentTransitionGeometryUpdate()
{
    if (m_contentTransitionUpdateQueued || !isVisible() || m_animatingDocking
        || m_overlayAnimationSuspended) {
        return;
    }

    m_contentTransitionUpdateQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_contentTransitionUpdateQueued = false;
        updateContentTransitionGeometry();
        updateOverlayVisibility();
    });
}

void DockPanel::updateOverlayVisibility()
{
    const bool showBorderOverlay = true;
    if (m_borderOverlay) {
        m_borderOverlay->setVisible(showBorderOverlay);
        if (showBorderOverlay) {
            m_borderOverlay->raise();
        }
    }

    const bool showTransition
        = m_titleBarVisible && !m_animatingDocking && !m_overlayAnimationSuspended;
    if (m_contentTransition) {
        if (!showTransition) {
            m_contentTransition->hide();
        } else if (m_contentTransition->width() > 0 && m_contentTransition->height() > 0) {
            m_contentTransition->show();
            m_contentTransition->raise();
            if (m_titleBar) {
                m_titleBar->raise();
            }
        }
    }
}

void DockPanel::setOverlayAnimationSuspended(bool suspended)
{
    if (m_overlayAnimationSuspended == suspended) {
        return;
    }

    m_overlayAnimationSuspended = suspended;
    updateOverlayVisibility();
}

// ============================================================================
// Docking Animation
// ============================================================================

void DockPanel::setupDockingAnimation()
{
    m_dockingAnimation = new QVariantAnimation(this);
    m_dockingAnimation->setStartValue(0.0);
    m_dockingAnimation->setEndValue(1.0);
    m_dockingAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_dockingAnimation, &QVariantAnimation::valueChanged, this,
        &DockPanel::onDockingAnimationValueChanged);
    connect(m_dockingAnimation, &QVariantAnimation::finished, this,
        &DockPanel::onDockingAnimationFinished);
}

void DockPanel::animateDocking(const QRect& sourceGeom, const QRect& targetGeom, int duration)
{
    if (!m_dockingAnimation)
        return;

    // Stop any running animation
    if (m_dockingAnimation->state() == QAbstractAnimation::Running) {
        m_dockingAnimation->stop();
    }

    m_dockingStartGeom = sourceGeom;
    m_dockingTargetGeom = targetGeom;

    // Set initial geometry to source
    setGeometry(sourceGeom);

    // Determine duration
    int actualDuration = (duration > 0) ? duration : m_dockingAnimationDuration;

    // Start animation
    m_animatingDocking = true;
    updateOverlayVisibility();
    m_dockingAnimation->setDuration(actualDuration);
    m_dockingAnimation->setCurrentTime(0);
    m_dockingAnimation->start();
}

void DockPanel::onDockingAnimationValueChanged(const QVariant& value)
{
    if (!m_animatingDocking)
        return;
    applyDockingAnimationFrame(value.toDouble());
}

void DockPanel::applyDockingAnimationFrame(double progress)
{
    // Interpolate geometry from start to target
    int x = m_dockingStartGeom.x()
        + qRound((m_dockingTargetGeom.x() - m_dockingStartGeom.x()) * progress);
    int y = m_dockingStartGeom.y()
        + qRound((m_dockingTargetGeom.y() - m_dockingStartGeom.y()) * progress);
    int w = m_dockingStartGeom.width()
        + qRound((m_dockingTargetGeom.width() - m_dockingStartGeom.width()) * progress);
    int h = m_dockingStartGeom.height()
        + qRound((m_dockingTargetGeom.height() - m_dockingStartGeom.height()) * progress);

    setGeometry(x, y, w, h);
}

void DockPanel::onDockingAnimationFinished()
{
    m_animatingDocking = false;

    // Apply final geometry
    setGeometry(m_dockingTargetGeom);
    updateContentTransitionGeometry();
    updateOverlayVisibility();

    emit dockingAnimationFinished();
}

} // namespace ruwa::ui::docking
