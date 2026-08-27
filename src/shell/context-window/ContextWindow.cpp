// SPDX-License-Identifier: MPL-2.0

#include "shell/context-window/ContextWindow.h"

#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/style/WidgetStyleManager.h"
#include "shell/context-menu/ContextMenuSystem.h"
#include "shell/top-bar/TopBar.h"

#include <QWKWidgets/widgetwindowagent.h>

#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QEasingCurve>
#include <QEvent>
#include <QGridLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

namespace ruwa::ui::windows {

namespace {

namespace anim = ruwa::ui::core::anim;
using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeFontRole;
using ruwa::ui::core::ThemeManager;
using ruwa::ui::widgets::WindowControlButton;

class ContextWindowTopBar final : public QWidget {
public:
    explicit ContextWindowTopBar(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);

        m_layout = new QGridLayout(this);
        m_layout->setSpacing(0);

        m_leftReserve = new QWidget(this);
        m_leftReserve->setAttribute(Qt::WA_TransparentForMouseEvents);

        m_titleLabel = new QLabel(title, this);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        m_closeButton = new WindowControlButton(WindowControlButton::Type::Close, this);
        m_closeButton->setToolTip(QWidget::tr("Close"));

        m_layout->addWidget(m_leftReserve, 0, 0);
        m_layout->addWidget(m_titleLabel, 0, 1);
        m_layout->addWidget(m_closeButton, 0, 2);
        m_layout->setColumnStretch(1, 1);

        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
            [this]() { updateTheme(); });
        updateTheme();
    }

    QAbstractButton* closeButton() const { return m_closeButton; }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& theme = ThemeManager::instance();
        const auto& colors = theme.colors();
        const int inset = theme.scaled(BaseVisualInset);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        painter.fillRect(rect(), colors.surface);
        if (width() <= 2 * inset + 2 || height() <= 2 * inset + 2) {
            return;
        }

        const QRectF plaqueRect(inset + 0.5, inset + 0.5, qreal(width() - 2 * inset - 1),
            qreal(height() - 2 * inset - 1));

        QPainterPath plaque;
        plaque.addRoundedRect(
            plaqueRect, theme.scaled(BaseCornerRadius), theme.scaled(BaseCornerRadius));

        QLinearGradient gradient(0, inset, 0, height() - inset);
        gradient.setColorAt(0, colors.surfaceAlt);
        gradient.setColorAt(1, ThemeColors::adjustBrightness(colors.surfaceAlt, 100.0 / 102));
        painter.fillPath(plaque, gradient);

        QPen outline(colors.border, 1.0);
        outline.setCosmetic(true);
        outline.setJoinStyle(Qt::RoundJoin);
        outline.setCapStyle(Qt::RoundCap);
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(plaque);
    }

private:
    void updateTheme()
    {
        auto& theme = ThemeManager::instance();
        const int inset = theme.scaled(BaseVisualInset);
        const int contentHeight = theme.scaled(BaseContentHeight);
        const int buttonWidth = theme.scaled(40);

        setFixedHeight(contentHeight + 2 * inset);
        m_layout->setContentsMargins(inset, inset, inset, inset);
        m_leftReserve->setFixedWidth(buttonWidth);
        m_closeButton->setFixedSize(buttonWidth, contentHeight);
        m_titleLabel->setFont(theme.font(ThemeFontRole::Small, QFont::DemiBold));
        m_titleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(theme.colors().text.name(QColor::HexArgb)));
        updateGeometry();
        update();
    }

    QGridLayout* m_layout = nullptr;
    QWidget* m_leftReserve = nullptr;
    QLabel* m_titleLabel = nullptr;
    WindowControlButton* m_closeButton = nullptr;

    static constexpr int BaseContentHeight = 26;
    static constexpr int BaseVisualInset = 5;
    static constexpr int BaseCornerRadius = 8;
};

} // namespace

ContextWindow::ContextWindow(QWidget* owner, const QString& title, QWidget* contentWidget)
    : QWidget(owner)
    , m_owner(owner)
{
    Q_ASSERT(owner);
    Q_ASSERT(contentWidget);

    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setWindowFlag(Qt::Window, true);
    setWindowModality(Qt::ApplicationModal);
    setObjectName(QStringLiteral("ruwa_context_window"));
    setWindowTitle(title);
    setCursor(Qt::ArrowCursor);

    setupUi(title, contentWidget);
    setupWindowAgent();

    m_transitionAnimation = new QParallelAnimationGroup(this);
    m_slideAnimation = new QPropertyAnimation(this, "pos", m_transitionAnimation);
    m_opacityAnimation = new QPropertyAnimation(this, "windowOpacity", m_transitionAnimation);
    m_transitionAnimation->addAnimation(m_slideAnimation);
    m_transitionAnimation->addAnimation(m_opacityAnimation);
    connect(m_transitionAnimation, &QParallelAnimationGroup::finished, this, [this]() {
        if (!m_closing) {
            return;
        }
        m_closeAllowed = true;
        close();
    });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        applyWindowEffects();
        updateContentSize();
        update();
    });
}

ContextWindow::~ContextWindow()
{
    setShortcutBlocking(false);
}

void ContextWindow::showAnimated()
{
    if (isVisible() || m_closing || !m_owner) {
        return;
    }

    updateContentSize();
    const QPoint start = entrancePosition();
    const QPoint finish = visiblePosition();

    setWindowOpacity(0.0);
    move(start);
    setShortcutBlocking(true);
    show();
    raise();
    activateWindow();

    // A newly created native window has no populated backing store yet. Starting
    // the move in the same event-loop turn as show() lets DWM reveal its default
    // (light) surface for a frame. Paint the complete stationary window first,
    // then begin moving it once that frame has been submitted.
    repaint();
    m_topBar->repaint();
    m_contentWidget->repaint();
    QTimer::singleShot(0, this, [this, start, finish]() {
        if (!isVisible() || m_closing) {
            return;
        }

        m_transitionAnimation->stop();
        m_slideAnimation->setDuration(anim::duration(ShowDuration));
        m_slideAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_slideAnimation->setStartValue(start);
        m_slideAnimation->setEndValue(finish);

        m_opacityAnimation->setDuration(anim::duration(ShowDuration));
        m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_opacityAnimation->setStartValue(0.0);
        m_opacityAnimation->setEndValue(1.0);
        anim::start(m_transitionAnimation);
    });
}

void ContextWindow::dismiss()
{
    if (m_closing) {
        return;
    }
    if (!isVisible()) {
        m_closeAllowed = true;
        close();
        return;
    }

    m_closing = true;
    m_contentWidget->setEnabled(false);
    m_closeButton->setEnabled(false);
    m_transitionAnimation->stop();
    m_slideAnimation->setDuration(anim::duration(HideDuration));
    m_slideAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_slideAnimation->setStartValue(pos());

    QPoint finish = entrancePosition();
    finish.setX(pos().x());
    m_slideAnimation->setEndValue(finish);

    m_opacityAnimation->setDuration(anim::duration(HideDuration));
    m_opacityAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_opacityAnimation->setStartValue(windowOpacity());
    m_opacityAnimation->setEndValue(0.0);
    anim::start(m_transitionAnimation);
}

bool ContextWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Shortcut) {
        // ShortcutManager covers registered commands, but a few application-wide
        // QShortcuts are owned directly by MainWindow. Do not let those bypass
        // modality while this window is active. Shortcuts owned by the supplied
        // content remain available.
        for (QObject* object = watched; object; object = object->parent()) {
            if (object == this) {
                return false;
            }
        }
        return true;
    }

    if (watched == m_owner
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize
            || event->type() == QEvent::WindowStateChange)) {
        QTimer::singleShot(0, this, [this]() {
            if (isVisible() && !m_closing
                && m_transitionAnimation->state() != QAbstractAnimation::Running) {
                move(visiblePosition());
            }
        });
    }
    return QWidget::eventFilter(watched, event);
}

void ContextWindow::closeEvent(QCloseEvent* event)
{
    if (!m_closeAllowed && isVisible()) {
        event->ignore();
        dismiss();
        return;
    }

    setShortcutBlocking(false);
    event->accept();
}

void ContextWindow::hideEvent(QHideEvent* event)
{
    setShortcutBlocking(false);
    QWidget::hideEvent(event);
}

void ContextWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        event->accept();
        dismiss();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ContextWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();
    QPainter painter(this);
    painter.fillRect(rect(), colors.surface);

    QPen border(colors.border, 1.0);
    border.setCosmetic(true);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
}

void ContextWindow::setupUi(const QString& title, QWidget* contentWidget)
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(1, 1, 1, 1);
    m_mainLayout->setSpacing(0);
    m_mainLayout->setSizeConstraint(QLayout::SetFixedSize);

    auto* topBar = new ContextWindowTopBar(title, this);
    m_topBar = topBar;
    m_closeButton = topBar->closeButton();
    connect(m_closeButton, &QAbstractButton::clicked, this, &ContextWindow::dismiss);
    m_mainLayout->addWidget(m_topBar);

    m_contentWidget = contentWidget;
    m_contentWidget->setParent(this);
    m_mainLayout->addWidget(m_contentWidget);

    ruwa::ui::widgets::ContextMenuSystem::instance().installOn(m_contentWidget);
}

void ContextWindow::setupWindowAgent()
{
    m_windowAgent = new QWK::WidgetWindowAgent(this);
    m_windowAgent->setup(this);
    m_windowAgent->setTitleBar(m_topBar);
    m_windowAgent->setSystemButton(QWK::WidgetWindowAgent::Close, m_closeButton);
    m_windowAgent->setHitTestVisible(m_closeButton, true);
    applyWindowEffects();
}

void ContextWindow::applyWindowEffects()
{
    if (!m_windowAgent) {
        return;
    }

    const auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();
    QPalette windowPalette = palette();
    windowPalette.setColor(QPalette::Window, colors.surface);
    setPalette(windowPalette);
    setAutoFillBackground(true);

#ifdef Q_OS_WIN
    m_windowAgent->setWindowAttribute(QStringLiteral("dark-mode"), colors.isDark);

    // Match BrushEditorWindow's QWindowKit material fallback chain. Keeping the
    // native non-client frame lets DWM retain the real window shadow.
    m_windowAgent->setWindowAttribute(QStringLiteral("mica-alt"), false);
    m_windowAgent->setWindowAttribute(QStringLiteral("mica"), false);
    m_windowAgent->setWindowAttribute(QStringLiteral("acrylic-material"), false);
    m_windowAgent->setWindowAttribute(QStringLiteral("dwm-blur"), false);

    bool enabled = m_windowAgent->setWindowAttribute(QStringLiteral("mica-alt"), true);
    if (!enabled) {
        enabled = m_windowAgent->setWindowAttribute(QStringLiteral("mica"), true);
    }
    if (!enabled) {
        enabled = m_windowAgent->setWindowAttribute(QStringLiteral("acrylic-material"), true);
    }
    if (!enabled) {
        m_windowAgent->setWindowAttribute(QStringLiteral("dwm-blur"), true);
    }
#endif
}

void ContextWindow::setShortcutBlocking(bool blocked)
{
    if (m_shortcutsBlocked == blocked) {
        return;
    }

    m_shortcutsBlocked = blocked;
    if (blocked) {
        qApp->installEventFilter(this);
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
    } else {
        qApp->removeEventFilter(this);
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void ContextWindow::updateContentSize()
{
    m_contentWidget->updateGeometry();
    m_mainLayout->invalidate();
    m_mainLayout->activate();
    adjustSize();
}

QPoint ContextWindow::visiblePosition() const
{
    if (!m_owner) {
        return pos();
    }

    const QRect ownerRect = m_owner->frameGeometry();
    const int inset = ThemeManager::instance().scaled(BaseBottomInset);
    const int x = ownerRect.left() + (ownerRect.width() - width()) / 2;
    const int preferredY = ownerRect.bottom() - height() - inset + 1;
    const int y = qMax(ownerRect.top() + inset, preferredY);
    return QPoint(x, y);
}

QPoint ContextWindow::entrancePosition() const
{
    if (!m_owner) {
        return pos();
    }

    QPoint entrance = visiblePosition();
    entrance.ry() += ThemeManager::instance().scaled(BaseBottomInset);

    // A maximized frameless window can report a frame a few pixels larger than
    // the physical monitor. Keep every popup pixel inside the actual screen so
    // DWM never skips composition of its lower part.
    if (QScreen* screen = m_owner->screen()) {
        const QRect screenRect = screen->geometry();
        const int lowestFullyVisibleY = qMax(screenRect.top(), screenRect.bottom() - height() + 1);
        entrance.setY(qBound(screenRect.top(), entrance.y(), lowestFullyVisibleY));
    }
    return entrance;
}

} // namespace ruwa::ui::windows
