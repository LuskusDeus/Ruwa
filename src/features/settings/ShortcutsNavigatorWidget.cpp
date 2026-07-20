// SPDX-License-Identifier: MPL-2.0

// ShortcutsNavigatorWidget.cpp
#include "features/settings/ShortcutsNavigatorWidget.h"
#include "shared/widgets/inputs/CommandInputWidget.h"
#include "features/home/welcome/WelcomeBannerButton.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/i18n/CommandLocalization.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/style/WidgetStyleManager.h"
#include "commands/CommandRegistry.h"
#include "commands/ShortcutManager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLayoutItem>
#include <QLabel>
#include <QPainter>
#include <QShowEvent>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {
const int BASE_ROW_HEIGHT = 48;
const int BASE_LINE_HEIGHT = 1;
const int BASE_SEPARATOR_LINE = 5;
const int BASE_CONTENT_PADDING_V = 4; // gap between last separator line and the bottom row
const int BASE_ROW_SPACING = 16;
const int MAX_VISIBLE_SHORTCUTS = 5;
const int BASE_OUTER_PADDING_V = 4; // symmetric outer top/bottom padding for this row
const int BASE_BUTTON_BOTTOM_GAP = 8;
} // namespace

// Horizontal separator line with rounded ends (drawn manually)
class ShortcutsSeparatorLine : public QWidget {
public:
    explicit ShortcutsSeparatorLine(QWidget* parent = nullptr, bool isBottomSeparator = false)
        : QWidget(parent)
        , m_isBottom(isBottomSeparator)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    QSize sizeHint() const override
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int line = theme.scaled(BASE_SEPARATOR_LINE);
        if (m_isBottom) {
            const int pad = theme.scaled(BASE_CONTENT_PADDING_V);
            return QSize(0, line + pad); // line + bottom margin only
        }
        return QSize(0, line);
    }

    void setBottomSeparator(bool isBottom)
    {
        if (m_isBottom == isBottom) {
            return;
        }
        m_isBottom = isBottom;
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int lineH = qMax(1, theme.scaled(BASE_LINE_HEIGHT));
        const int radius = lineH / 2;
        const QColor lineColor = theme.colors().border;

        int y = (height() - lineH) / 2;
        if (m_isBottom)
            y = 0; // line at top, pad below
        QRectF lineRect(0, y, width(), lineH);

        painter.setPen(Qt::NoPen);
        painter.setBrush(lineColor);
        painter.drawRoundedRect(lineRect, radius, radius);
    }

private:
    bool m_isBottom = false;
};

static QString shortcutsDescriptionText(int shown)
{
    const int total = ruwa::core::CommandRegistry::instance().allCommands().size();
    return QCoreApplication::translate("ShortcutsNavigatorWidget", "%1 of %2 shortcuts shown")
        .arg(shown)
        .arg(total);
}

ShortcutsNavigatorWidget::ShortcutsNavigatorWidget(QWidget* parent)
    : BaseSettingsWidget(QString(), shortcutsDescriptionText(0), parent)
{
    setupContent();
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ShortcutsNavigatorWidget::updateButtonIcon);
    connect(&ruwa::core::ShortcutManager::instance(), &ruwa::core::ShortcutManager::shortcutUsed,
        this, &ShortcutsNavigatorWidget::updateShortcutRows);

    updateMinimumHeight();
}

void ShortcutsNavigatorWidget::updateMinimumHeight()
{
    if (!m_mainLayout) {
        refreshLayoutGeometry();
        return;
    }

    if (m_shortcutListLayout) {
        m_shortcutListLayout->invalidate();
        m_shortcutListLayout->activate();
    }
    if (m_bottomLayout) {
        m_bottomLayout->invalidate();
        m_bottomLayout->activate();
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const QMargins margins = m_mainLayout->contentsMargins();
    const int rowH = theme.scaled(BASE_ROW_HEIGHT);
    const int line = theme.scaled(BASE_SEPARATOR_LINE);
    const int pad = theme.scaled(BASE_CONTENT_PADDING_V);
    const int visibleShortcuts = m_shortcutRows.size();

    int shortcutsHeight = 0;
    if (visibleShortcuts > 0) {
        shortcutsHeight
            = (visibleShortcuts * rowH) + ((visibleShortcuts - 1) * line) + (line + pad);
    }

    const int bottomRowHeight = m_bottomRow ? m_bottomRow->sizeHint().height() : 0;
    const int totalH = margins.top() + margins.bottom() + shortcutsHeight + bottomRowHeight;
    if (height() != totalH || minimumHeight() != totalH || maximumHeight() != totalH) {
        setFixedHeight(totalH);
    }

    refreshLayoutGeometry();
}

void ShortcutsNavigatorWidget::applyScaledLayoutMargins()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    if (m_mainLayout) {
        const QMargins outerMargins = m_mainLayout->contentsMargins();
        m_mainLayout->setContentsMargins(outerMargins.left(), theme.scaled(BASE_OUTER_PADDING_V),
            outerMargins.right(), theme.scaled(BASE_OUTER_PADDING_V));
    }
    if (auto* controlsLayout = mainLayout()) {
        controlsLayout->setContentsMargins(0, 0, 0, theme.scaled(BASE_BUTTON_BOTTOM_GAP));
    }
}

void ShortcutsNavigatorWidget::clearShortcutRows()
{
    if (!m_shortcutListLayout) {
        return;
    }

    while (QLayoutItem* item = m_shortcutListLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }

    m_shortcutRows.clear();
    m_shortcutSeparators.clear();
    m_shortcutLabels.clear();
    m_commandInputs.clear();
}

void ShortcutsNavigatorWidget::rebuildShortcutRows(const QStringList& shortcutIds)
{
    clearShortcutRows();

    if (!m_shortcutListLayout) {
        return;
    }

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    auto& registry = ruwa::core::CommandRegistry::instance();
    auto& shortcuts = ruwa::core::ShortcutManager::instance();
    auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
    const auto& colors = mgr.colors();

    const int visibleShortcuts = qMin(shortcutIds.size(), MAX_VISIBLE_SHORTCUTS);
    const int rowH = theme.scaled(BASE_ROW_HEIGHT);
    const int rowSpacing = theme.scaled(BASE_ROW_SPACING);
    const int line = theme.scaled(BASE_SEPARATOR_LINE);
    const int pad = theme.scaled(BASE_CONTENT_PADDING_V);

    for (int i = 0; i < visibleShortcuts; ++i) {
        const QString cmdId = shortcutIds[i];
        QString title;
        if (auto* cmd = registry.command(cmdId)) {
            const auto info = cmd->info();
            const QString locTitle = ruwa::i18n::CommandLocalization::instance().title(cmdId);
            title = locTitle.isEmpty() ? info.title : locTitle;
        }

        QWidget* shortcutRow = new QWidget(m_shortcutListContainer);
        shortcutRow->setFixedHeight(rowH);
        shortcutRow->setAttribute(Qt::WA_TranslucentBackground);

        QHBoxLayout* shortcutRowLayout = new QHBoxLayout(shortcutRow);
        shortcutRowLayout->setContentsMargins(0, 0, 0, 0);
        shortcutRowLayout->setSpacing(rowSpacing);

        QLabel* titleLabel = new QLabel(title, shortcutRow);
        titleLabel->setAttribute(Qt::WA_TranslucentBackground);
        QFont labelFont = titleLabel->font();
        labelFont.setBold(true);
        labelFont.setPointSize(mgr.scaledFontSize(style().content.baseFontSize));
        titleLabel->setFont(labelFont);
        titleLabel->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.text.name()));

        auto* commandInput
            = new CommandInputWidget(shortcutRow, CommandInputWidget::SizeVariant::Compact);
        commandInput->setCommandId(cmdId);
        commandInput->setKeySequence(shortcuts.shortcutFor(cmdId));

        connect(commandInput, &CommandInputWidget::shortcutRecorded, this,
            [this, commandInput](const QKeySequence& seq) {
                const QString id = commandInput->commandId();
                if (!id.isEmpty()) {
                    ruwa::core::ShortcutManager::instance().setShortcut(id, seq);
                    ruwa::core::ShortcutManager::instance().saveToSettings();
                }
            });

        shortcutRowLayout->addWidget(titleLabel, 1, Qt::AlignVCenter);
        shortcutRowLayout->addWidget(commandInput, 0, Qt::AlignVCenter);

        m_shortcutRows.append(shortcutRow);
        m_shortcutLabels.append(titleLabel);
        m_commandInputs.append(commandInput);
        m_shortcutListLayout->addWidget(shortcutRow);

        const bool isBottomSep = (i == visibleShortcuts - 1);
        auto* sep = new ShortcutsSeparatorLine(m_shortcutListContainer, isBottomSep);
        sep->setFixedHeight(isBottomSep ? (line + pad) : line);
        m_shortcutSeparators.append(sep);
        m_shortcutListLayout->addWidget(sep);
    }
}

void ShortcutsNavigatorWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void ShortcutsNavigatorWidget::showEvent(QShowEvent* event)
{
    BaseSettingsWidget::showEvent(event);
    updateShortcutRows();
}

void ShortcutsNavigatorWidget::updateShortcutRows()
{
    const auto lastUsed = ruwa::core::ShortcutManager::instance().lastUsedShortcuts();
    const int visibleShortcuts = qMin(lastUsed.size(), MAX_VISIBLE_SHORTCUTS);
    rebuildShortcutRows(lastUsed);
    setDescription(shortcutsDescriptionText(visibleShortcuts));
    updateMinimumHeight();
}

void ShortcutsNavigatorWidget::updateThemeColors()
{
    BaseSettingsWidget::updateThemeColors();
    const auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();
    for (QLabel* label : m_shortcutLabels) {
        label->setStyleSheet(
            QString("QLabel { color: %1; background: transparent; }").arg(colors.text.name()));
    }
}

void ShortcutsNavigatorWidget::retranslateUi()
{
    if (m_openButton)
        m_openButton->setText(tr("Customize"));
    updateShortcutRows();
}

void ShortcutsNavigatorWidget::setupContent()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();

    // Hide label - we only show description (shortcut count)
    if (m_labelWidget)
        m_labelWidget->hide();

    // Center description vertically in the text block
    if (m_textLayout)
        m_textLayout->setAlignment(Qt::AlignVCenter);

    m_openButton = new WelcomeBannerButton(tr("Customize"),
        WelcomeBannerButton::ButtonStyle::Primary, this);
    m_openButton->setBannerBaseHeight(36);

    auto& icons = theme.icons();
    m_openButton->setIcon(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Edit));

    connect(m_openButton, &WelcomeBannerButton::clicked, this, &ShortcutsNavigatorWidget::clicked);

    QHBoxLayout* rowLayout = new QHBoxLayout();
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->addStretch();
    rowLayout->addWidget(m_openButton);

    mainLayout()->addLayout(rowLayout);

    // Latest shortcut rows stay above the existing description/customize row.
    auto* rootRow = qobject_cast<QHBoxLayout*>(m_mainLayout);
    if (!rootRow) {
        return;
    }
    rootRow->removeWidget(m_textContainer);
    rootRow->removeWidget(m_controlContainer);

    QWidget* bottomRow = new QWidget(this);
    m_bottomRow = bottomRow;
    bottomRow->setAttribute(Qt::WA_TranslucentBackground);
    bottomRow->setFixedHeight(theme.scaled(BASE_ROW_HEIGHT));
    m_bottomLayout = new QHBoxLayout(bottomRow);
    m_bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_bottomLayout->setSpacing(theme.scaled(BASE_ROW_SPACING));
    m_bottomLayout->addWidget(m_textContainer, 1, Qt::AlignVCenter);
    m_bottomLayout->addWidget(m_controlContainer, 0, Qt::AlignBottom);

    QVBoxLayout* contentVBox = new QVBoxLayout();
    contentVBox->setContentsMargins(0, 0, 0, 0);
    contentVBox->setSpacing(0);

    m_shortcutListContainer = new QWidget(this);
    m_shortcutListContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_shortcutListLayout = new QVBoxLayout(m_shortcutListContainer);
    m_shortcutListLayout->setContentsMargins(0, 0, 0, 0);
    m_shortcutListLayout->setSpacing(0);

    contentVBox->addWidget(m_shortcutListContainer, 0);
    contentVBox->addWidget(bottomRow, 0);

    rootRow->addLayout(contentVBox, 1);

    applyScaledLayoutMargins();
    updateShortcutRows();
}

void ShortcutsNavigatorWidget::updateButtonIcon()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    if (m_openButton) {
        auto& icons = theme.icons();
        m_openButton->setIcon(icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Edit));
    }
    if (m_bottomLayout) {
        m_bottomLayout->setSpacing(theme.scaled(BASE_ROW_SPACING));
    }
    if (m_bottomRow) {
        m_bottomRow->setFixedHeight(theme.scaled(BASE_ROW_HEIGHT));
    }
    applyScaledLayoutMargins();
    updateShortcutRows();
}

} // namespace ruwa::ui::widgets
