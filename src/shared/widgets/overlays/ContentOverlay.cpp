// SPDX-License-Identifier: MPL-2.0

#include "ContentOverlay.h"

#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/AnimationPolicy.h"

#include <QApplication>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QVBoxLayout>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

namespace {
constexpr int ShowDuration = 150;
constexpr int HideDuration = 140;
constexpr int PanelSlideOffset = 18;
constexpr int OverlayMargin = 32;
} // namespace

ContentOverlay::ContentOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);
    m_opacityAnimation = new QPropertyAnimation(this, "overlayOpacity", this);
    m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_panelOffsetAnimation = new QPropertyAnimation(this, "panelOffset", this);
    m_panelOffsetAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_opacityAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            hideImmediate();
        }
    });

    m_panel = new QFrame(this);
    m_panel->setObjectName(QStringLiteral("contentOverlayPanel"));
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this]() {
        hideOverlay();
        emit cancelled();
    });
    if (parent) {
        parent->installEventFilter(this);
    }
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            updateStyle();
            updatePanelGeometry();
        });
    hide();
    updateStyle();
}

ContentOverlay::ContentOverlay(QWidget* content, const QString& title, QWidget* parent)
    : ContentOverlay(parent)
{
    m_content = content;
    auto* layout = new QVBoxLayout(m_panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* heading = new QLabel(title, m_panel);
    heading->setObjectName(QStringLiteral("contentOverlayTitle"));
    heading->setTextFormat(Qt::PlainText);
    heading->setAlignment(Qt::AlignCenter);
    layout->addWidget(heading);
    layout->addWidget(content, 1);
    updateStyle();
}

ContentOverlay::~ContentOverlay()
{
    releaseInput();
}

void ContentOverlay::showOverlay()
{
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }
    m_opacityAnimation->stop();
    m_panelOffsetAnimation->stop();
    m_isHiding = false;
    m_panel->setEnabled(true);
    if (!isVisible()) {
        m_previousFocus = QApplication::focusWidget();
        setOverlayOpacity(0.0);
        setPanelOffset(-PanelSlideOffset);
        show();
    }
    if (!m_shortcutsBlocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
        m_shortcutsBlocked = true;
        qApp->installEventFilter(this);
    }
    updatePanelGeometry();
    raise();
    setFocus(Qt::OtherFocusReason);
    m_opacityAnimation->setDuration(anim::duration(ShowDuration));
    m_opacityAnimation->setStartValue(m_overlayOpacity);
    m_opacityAnimation->setEndValue(1.0);
    anim::start(m_opacityAnimation);
    m_panelOffsetAnimation->setDuration(anim::duration(ShowDuration));
    m_panelOffsetAnimation->setStartValue(m_panelOffset);
    m_panelOffsetAnimation->setEndValue(0.0);
    anim::start(m_panelOffsetAnimation);
}

void ContentOverlay::hideOverlay()
{
    if (!isVisible() || m_isHiding) {
        return;
    }
    m_isHiding = true;
    m_panel->setEnabled(false);
    m_opacityAnimation->stop();
    m_panelOffsetAnimation->stop();
    m_panelOffsetAnimation->setDuration(anim::duration(HideDuration));
    m_panelOffsetAnimation->setStartValue(m_panelOffset);
    m_panelOffsetAnimation->setEndValue(-PanelSlideOffset);
    anim::start(m_panelOffsetAnimation);
    // Finish last: with animations disabled the hidden signal is synchronous.
    m_opacityAnimation->setDuration(anim::duration(HideDuration));
    m_opacityAnimation->setStartValue(m_overlayOpacity);
    m_opacityAnimation->setEndValue(0.0);
    anim::start(m_opacityAnimation);
}

void ContentOverlay::setOverlayOpacity(qreal opacity)
{
    m_overlayOpacity = qBound(0.0, opacity, 1.0);
    m_opacityEffect->setOpacity(m_overlayOpacity);
    update();
}

void ContentOverlay::setPanelOffset(qreal offset)
{
    m_panelOffset = offset;
    updatePanelGeometry();
}

void ContentOverlay::updatePanelGeometry()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    QSize preferred
        = m_content ? m_content->sizeHint() + QSize(0, theme.scaled(48)) : QSize(560, 520);
    const QSize minimum = m_content ? m_panel->minimumSizeHint() : QSize(360, 260);
    preferred = preferred.expandedTo(minimum);
    const int targetWidth = qBound(minimum.width(), width() - OverlayMargin * 2, preferred.width());
    const int targetHeight
        = qBound(minimum.height(), height() - OverlayMargin * 2, preferred.height());
    m_panel->setFixedSize(targetWidth, targetHeight);
    m_panel->move(
        (width() - targetWidth) / 2, (height() - targetHeight) / 2 + qRound(m_panelOffset));
}

void ContentOverlay::updateStyle()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    m_panel->setStyleSheet(QStringLiteral(
        "QFrame#contentOverlayPanel { background: %1; border: 1px solid %2; border-radius: 16px; }"
        "QLabel#contentOverlayTitle { color: %3; background: transparent; padding: %4px; }")
            .arg(colors.surface.name(QColor::HexArgb), colors.borderSubtle().name(QColor::HexArgb),
                colors.text.name(QColor::HexArgb), QString::number(theme.scaled(16))));
    if (auto* heading = m_panel->findChild<QLabel*>(QStringLiteral("contentOverlayTitle"))) {
        heading->setFont(theme.font(ruwa::ui::core::ThemeFontRole::H6, QFont::DemiBold));
    }
}

bool ContentOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        setGeometry(parentWidget()->rect());
        updatePanelGeometry();
    }
    if (isVisible() && event->type() == QEvent::Shortcut) {
        // Keep overlay controls/dropdowns usable, while blocking application QShortcuts.
        QObject* owner = watched;
        while (owner && owner != this) {
            owner = owner->parent();
        }
        if (!owner) {
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ContentOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 110));
}

void ContentOverlay::mousePressEvent(QMouseEvent* event)
{
    if (!m_panel->geometry().contains(event->pos())) {
        hideOverlay();
        emit cancelled();
    }
    event->accept();
}

void ContentOverlay::hideImmediate()
{
    m_opacityAnimation->stop();
    m_panelOffsetAnimation->stop();
    m_isHiding = false;
    setOverlayOpacity(0.0);
    setPanelOffset(0.0);
    hide();
}

void ContentOverlay::releaseInput()
{
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        qApp->removeEventFilter(this);
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void ContentOverlay::hideEvent(QHideEvent* event)
{
    if (event->spontaneous()) {
        QWidget::hideEvent(event);
        return;
    }
    releaseInput();
    if (m_previousFocus && m_previousFocus->isVisible()) {
        m_previousFocus->setFocus(Qt::OtherFocusReason);
    }
    QWidget::hideEvent(event);
    emit hidden();
}

} // namespace ruwa::ui::widgets
