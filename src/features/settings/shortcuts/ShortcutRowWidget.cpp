// SPDX-License-Identifier: MPL-2.0

// ShortcutRowWidget.cpp
#include "features/settings/shortcuts/ShortcutRowWidget.h"
#include "shared/widgets/inputs/CommandInputWidget.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_ROW_HEIGHT = 52;
const int BASE_ROW_HEIGHT_WITH_DESC = 64;
const int BASE_PADDING_H = 16;
const int BASE_PADDING_V = 10;
const int BASE_SPACING = 8;
const int BASE_DESC_SPACING = 2;
const int BASE_RESET_MARGIN_LEFT = 10;
} // namespace

ShortcutRowWidget::ShortcutRowWidget(const QString& commandId, const QString& commandTitle,
    const QString& commandDescription, const QKeySequence& shortcut,
    const QKeySequence& defaultShortcut, QWidget* parent)
    : BaseStyledPanel("SettingsPanel", parent)
    , m_commandId(commandId)
    , m_commandTitle(commandTitle)
    , m_commandDescription(commandDescription)
    , m_shortcut(shortcut)
    , m_defaultShortcut(defaultShortcut)
{
    setupUI();
    updateScaledSizes();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &ShortcutRowWidget::onThemeChanged);
}

ShortcutRowWidget::~ShortcutRowWidget() { }

void ShortcutRowWidget::setupUI()
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    m_layout = new QHBoxLayout(this);
    m_layout->setSpacing(mgr.scaled(BASE_SPACING));

    // Left side: title + description
    auto* leftLayout = new QVBoxLayout();
    leftLayout->setSpacing(mgr.scaled(BASE_DESC_SPACING));

    m_titleLabel = new QLabel(m_commandTitle, this);
    m_titleLabel->setAttribute(Qt::WA_TranslucentBackground);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(mgr.scaledFontSize(10));
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(
        QString("QLabel { color: %1; background: transparent; }").arg(colors.text.name()));
    leftLayout->addWidget(m_titleLabel);

    if (!m_commandDescription.isEmpty()) {
        m_descLabel = new QLabel(m_commandDescription, this);
        m_descLabel->setAttribute(Qt::WA_TranslucentBackground);
        QFont descFont = m_descLabel->font();
        descFont.setPointSize(mgr.scaledFontSize(8));
        m_descLabel->setFont(descFont);
        m_descLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.textMuted.name()));
        leftLayout->addWidget(m_descLabel);
    }

    m_layout->addLayout(leftLayout, 1);

    // Right side: CommandInputWidget + Reset button
    m_commandInput = new CommandInputWidget(this);
    m_commandInput->setCommandId(m_commandId);
    m_commandInput->setKeySequence(m_shortcut);

    connect(m_commandInput, &CommandInputWidget::shortcutRecorded, this,
        [this](const QKeySequence& seq) { emit shortcutChanged(m_commandId, seq); });

    m_layout->addWidget(m_commandInput, 0, Qt::AlignVCenter);

    m_resetButton = new CapsuleButton(tr("Reset"), CapsuleButton::Variant::Secondary, this);
    m_resetButton->setBaseMinimumWidth(0);
    m_resetButton->setIcon(
        ThemeManager::instance().icons().getIcon(IconProvider::StandardIcon::UndoArrow));
    m_resetButton->setSizeScale(0.68);
    m_resetButton->syncSizeToText();
    connect(
        m_resetButton, &QPushButton::clicked, this, [this]() { emit resetRequested(m_commandId); });

    m_layout->addSpacing(mgr.scaled(BASE_RESET_MARGIN_LEFT));
    m_layout->addWidget(m_resetButton, 0, Qt::AlignVCenter);

    updateResetButtonVisibility();

    // Set fixed height
    const int h = m_commandDescription.isEmpty() ? mgr.scaled(BASE_ROW_HEIGHT)
                                                 : mgr.scaled(BASE_ROW_HEIGHT_WITH_DESC);
    setFixedHeight(h);
}

void ShortcutRowWidget::updateScaledSizes()
{
    auto& mgr = WidgetStyleManager::instance();

    const int padH = mgr.scaled(BASE_PADDING_H);
    const int padV = mgr.scaled(BASE_PADDING_V);
    m_layout->setContentsMargins(padH, padV, padH, padV);
    m_layout->setSpacing(mgr.scaled(BASE_SPACING));

    if (m_titleLabel) {
        QFont f = m_titleLabel->font();
        f.setPointSize(mgr.scaledFontSize(10));
        m_titleLabel->setFont(f);
    }
    if (m_descLabel) {
        QFont f = m_descLabel->font();
        f.setPointSize(mgr.scaledFontSize(8));
        m_descLabel->setFont(f);
    }
    if (m_resetButton) {
        m_resetButton->setIcon(
            ThemeManager::instance().icons().getIcon(IconProvider::StandardIcon::UndoArrow));
        m_resetButton->setSizeScale(0.68);
        m_resetButton->syncSizeToText();
    }

    const int h = m_commandDescription.isEmpty() ? mgr.scaled(BASE_ROW_HEIGHT)
                                                 : mgr.scaled(BASE_ROW_HEIGHT_WITH_DESC);
    setFixedHeight(h);
}

void ShortcutRowWidget::updateThemeColors()
{
    const auto& colors = WidgetStyleManager::instance().colors();

    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.text.name()));
    }
    if (m_descLabel) {
        m_descLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.textMuted.name()));
    }
    if (m_resetButton) {
        m_resetButton->update();
    }
}

void ShortcutRowWidget::setShortcut(const QKeySequence& shortcut)
{
    m_shortcut = shortcut;
    if (m_commandInput) {
        m_commandInput->setKeySequence(shortcut);
    }
    updateResetButtonVisibility();
}

void ShortcutRowWidget::updateResetButtonVisibility()
{
    if (!m_resetButton)
        return;
    const bool canReset = (m_shortcut != m_defaultShortcut);
    m_resetButton->setVisible(canReset);
    m_resetButton->setEnabled(canReset);
}

bool ShortcutRowWidget::matchesSearch(const QString& query) const
{
    if (query.isEmpty())
        return true;
    const QString q = query.toLower();
    return m_commandTitle.toLower().contains(q) || m_commandDescription.toLower().contains(q)
        || m_commandId.toLower().contains(q)
        || m_shortcut.toString(QKeySequence::NativeText).toLower().contains(q);
}

void ShortcutRowWidget::onThemeChanged()
{
    updateScaledSizes();
    updateThemeColors();
}

} // namespace ruwa::ui::widgets
