// SPDX-License-Identifier: MPL-2.0

#include "features/brush/editor/BrushEditorWindow.h"
#include "features/brush/editor/BrushEditorTopBar.h"
#include "features/brush/editor/BrushEditorLayoutWidget.h"

#include "features/theme/manager/ThemeManager.h"
#include "shell/context-menu/ContextMenuSystem.h"
#include "shared/style/WidgetStyleManager.h"
#include "commands/ShortcutManager.h"

#include <QWKWidgets/widgetwindowagent.h>

#include <QVBoxLayout>
#include <QPainter>
#include <QApplication>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>

namespace ruwa::ui::windows {

using namespace ruwa::ui::core;

BrushEditorWindow::BrushEditorWindow(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_DontCreateNativeAncestors, true);
    setWindowFlag(Qt::Window, true);
    setWindowModality(Qt::NonModal);
    setObjectName(QStringLiteral("ruwa_brush_editor_window"));
    setCursor(Qt::ArrowCursor);

    setupUI();
    setupWindowAgent();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &BrushEditorWindow::onThemeChanged);

    // While editing any text field inside this window, the workspace's global
    // ApplicationShortcuts (tools, space, Ctrl/Alt combos, …) would otherwise
    // steal the keystrokes. Disable them whenever focus lands on a text input
    // that belongs to this window, and re-enable as soon as focus leaves.
    connect(qApp, &QApplication::focusChanged, this,
        [this](QWidget*, QWidget* now) { updateShortcutBlocking(now); });

    onThemeChanged();
}

BrushEditorWindow::~BrushEditorWindow()
{
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void BrushEditorWindow::updateShortcutBlocking(QWidget* focusWidget)
{
    const bool isTextInput = focusWidget
        && (qobject_cast<QLineEdit*>(focusWidget) || qobject_cast<QTextEdit*>(focusWidget)
            || qobject_cast<QPlainTextEdit*>(focusWidget)
            || qobject_cast<QAbstractSpinBox*>(focusWidget));
    const bool shouldBlock = isTextInput && isAncestorOf(focusWidget);

    if (shouldBlock == m_shortcutsBlocked) {
        return;
    }
    m_shortcutsBlocked = shouldBlock;
    if (shouldBlock) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
    } else {
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void BrushEditorWindow::setBrushName(const QString& brushName)
{
    if (m_brushName == brushName) {
        return;
    }
    m_brushName = brushName;
    if (m_topBar) {
        m_topBar->setBrushName(brushName);
    }
}

void BrushEditorWindow::setSelection(const QString& presetId, const QString& brushId)
{
    if (!m_layoutWidget) {
        return;
    }
    m_layoutWidget->setSelection(presetId, brushId);
}

void BrushEditorWindow::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    const auto& colors = WidgetStyleManager::instance().colors();
    painter.fillRect(rect(), colors.surface);
}

void BrushEditorWindow::onThemeChanged()
{
    applyWindowEffects();
    updateScaledSizes();
    updateContentStyles();
    update();
}

void BrushEditorWindow::setupUI()
{
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(1, 1, 1, 1);
    m_mainLayout->setSpacing(0);

    m_topBar = new BrushEditorTopBar(this);
    connect(m_topBar, &BrushEditorTopBar::closeRequested, this, &QWidget::close);
    m_mainLayout->addWidget(m_topBar);

    m_content = new QWidget(this);
    m_content->setObjectName(QStringLiteral("brush_editor_content"));

    auto* contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_layoutWidget = new BrushEditorLayoutWidget(m_content);
    connect(m_layoutWidget, &BrushEditorLayoutWidget::selectedBrushNameChanged, this,
        &BrushEditorWindow::setBrushName);
    connect(m_layoutWidget, &BrushEditorLayoutWidget::brushSelectionChanged, this,
        &BrushEditorWindow::brushSelectionChanged);
    contentLayout->addWidget(m_layoutWidget, 1);

    m_mainLayout->addWidget(m_content, 1);

    ruwa::ui::widgets::ContextMenuSystem::instance().installOn(m_content);
}

void BrushEditorWindow::setupWindowAgent()
{
    m_windowAgent = new QWK::WidgetWindowAgent(this);
    m_windowAgent->setup(this);
    m_windowAgent->setTitleBar(m_topBar);
    m_windowAgent->setSystemButton(QWK::WidgetWindowAgent::Close, m_topBar->closeButton());
    m_windowAgent->setHitTestVisible(m_topBar->closeButton(), true);
    for (QWidget* w : m_topBar->qwkExtraHitTestWidgets()) {
        if (w) {
            m_windowAgent->setHitTestVisible(w, true);
        }
    }
    applyWindowEffects();
}

void BrushEditorWindow::applyWindowEffects()
{
    if (!m_windowAgent) {
        return;
    }

#ifdef Q_OS_WIN
    const auto& colors = WidgetStyleManager::instance().colors();
    m_windowAgent->setWindowAttribute(QStringLiteral("dark-mode"), colors.isDark);

    // Unset every optional effect first (recommended by QWindowKit examples).
    m_windowAgent->setWindowAttribute(QStringLiteral("mica-alt"), false);
    m_windowAgent->setWindowAttribute(QStringLiteral("mica"), false);
    m_windowAgent->setWindowAttribute(QStringLiteral("acrylic-material"), false);
    m_windowAgent->setWindowAttribute(QStringLiteral("dwm-blur"), false);

    bool enabled = m_windowAgent->setWindowAttribute(QStringLiteral("mica-alt"), true);
    if (!enabled)
        enabled = m_windowAgent->setWindowAttribute(QStringLiteral("mica"), true);
    if (!enabled)
        enabled = m_windowAgent->setWindowAttribute(QStringLiteral("acrylic-material"), true);
    if (!enabled) {
        m_windowAgent->setWindowAttribute(QStringLiteral("dwm-blur"), true);
    }
#endif
}

void BrushEditorWindow::updateScaledSizes()
{
    auto& theme = ThemeManager::instance();

    const QSize desiredSize(theme.scaled(BaseWidth), theme.scaled(BaseHeight));
    if (!isVisible()) {
        resize(desiredSize);
    } else {
        setMinimumSize(theme.scaled(480), theme.scaled(380));
    }
}

void BrushEditorWindow::updateContentStyles()
{
    const auto& colors = WidgetStyleManager::instance().colors();

    m_content->setStyleSheet(QStringLiteral("QWidget#brush_editor_content { background: %1; }")
            .arg(colors.surface.name(QColor::HexArgb)));
}

} // namespace ruwa::ui::windows
