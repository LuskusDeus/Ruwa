// SPDX-License-Identifier: MPL-2.0

// CommandPaletteOverlay.cpp
#include "CommandPaletteOverlay.h"
#include "CommandPalette.h"
#include "commands/ShortcutManager.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>

namespace ruwa::ui::widgets {

CommandPaletteOverlay::CommandPaletteOverlay(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupAnimations();

    // Start fully hidden
    QWidget::hide();
    m_dimProgress = 0.0;
}

CommandPaletteOverlay::~CommandPaletteOverlay()
{
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void CommandPaletteOverlay::setupUI()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    // Create command palette as child
    m_palette = new CommandPalette(this);

    connect(m_palette, &CommandPalette::closeRequested, this, &CommandPaletteOverlay::hidePalette);

    // Track parent resize
    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
        resize(parentWidget()->size());
    }
}

void CommandPaletteOverlay::setupAnimations()
{
    m_dimAnimation = new QPropertyAnimation(this, "dimProgress", this);
    m_dimAnimation->setDuration(AnimationDuration);

    connect(m_dimAnimation, &QPropertyAnimation::finished, this,
        &CommandPaletteOverlay::onDimAnimationFinished);
}

void CommandPaletteOverlay::showPalette()
{
    if (m_isShowing || (isVisible() && !m_isHiding)) {
        // Already showing or visible
        m_palette->focusSearchBar();
        return;
    }

    m_isShowing = true;
    m_isHiding = false;

    // Resize to parent
    if (parentWidget()) {
        resize(parentWidget()->size());
    }

    QWidget::show();
    raise();
    setFocus();

    if (!m_shortcutsBlocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
        m_shortcutsBlocked = true;
    }
    updatePalettePosition();

    // Start dim animation
    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(1.0);
    m_dimAnimation->start();

    // Show palette
    m_palette->showAnimated();
    updatePalettePosition();
    m_palette->refreshGlassBackdropFrom(parentWidget());
}

void CommandPaletteOverlay::hidePalette()
{
    if (m_isHiding || !isVisible()) {
        return;
    }

    m_isHiding = true;
    m_isShowing = false;

    // Hide palette first
    m_palette->hideAnimated();

    // Start dim out
    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(0.0);
    m_dimAnimation->start();
}

bool CommandPaletteOverlay::isActive() const
{
    return isVisible() && !m_isHiding;
}

void CommandPaletteOverlay::setDimProgress(qreal progress)
{
    if (qFuzzyCompare(m_dimProgress, progress))
        return;
    m_dimProgress = progress;
    update();
}

void CommandPaletteOverlay::onPaletteHidden()
{
    // Palette finished hiding, now we can fully hide
    if (!m_isHiding) {
        hidePalette();
    }
}

void CommandPaletteOverlay::onDimAnimationFinished()
{
    if (m_isHiding) {
        m_isHiding = false;
        if (m_shortcutsBlocked) {
            m_shortcutsBlocked = false;
            ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
        }
        QWidget::hide();
        emit hidden();
    } else if (m_isShowing) {
        m_isShowing = false;
        emit shown();
    }
}

void CommandPaletteOverlay::updatePalettePosition()
{
    if (!m_palette)
        return;

    // Center horizontally, position in upper area
    int x = (width() - m_palette->width()) / 2;
    int y = height() / 5; // Upper fifth of screen

    m_palette->move(x, y);
}

void CommandPaletteOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_dimProgress <= 0.001)
        return;

    QPainter painter(this);

    // Semi-transparent black overlay
    int alpha = static_cast<int>(MaxDimOpacity * 255 * m_dimProgress);
    painter.fillRect(rect(), QColor(0, 0, 0, alpha));
}

void CommandPaletteOverlay::mousePressEvent(QMouseEvent* event)
{
    // Click outside palette = close
    if (m_palette && !m_palette->geometry().contains(event->pos())) {
        hidePalette();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void CommandPaletteOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hidePalette();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void CommandPaletteOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePalettePosition();
    if (isVisible() && !m_isHiding) {
        m_palette->refreshGlassBackdropFrom(parentWidget());
    }
}

bool CommandPaletteOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        auto* resizeEvent = static_cast<QResizeEvent*>(event);
        resize(resizeEvent->size());
        updatePalettePosition();
        if (isVisible() && !m_isHiding) {
            m_palette->refreshGlassBackdropFrom(parentWidget());
        }
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace ruwa::ui::widgets
