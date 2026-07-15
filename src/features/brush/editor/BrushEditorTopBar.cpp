// SPDX-License-Identifier: MPL-2.0

#include "features/brush/editor/BrushEditorTopBar.h"

#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shell/top-bar/TopBar.h"

#include <QHBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSizePolicy>

namespace ruwa::ui::windows {

using namespace ruwa::ui::core;
using ruwa::ui::widgets::LogoButton;
using ruwa::ui::widgets::WindowControlButton;

BrushEditorTopBar::BrushEditorTopBar(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_Hover);
    setAutoFillBackground(false);
    setMouseTracking(true);

    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setSpacing(0);

    m_logoButton = new LogoButton(this);
    m_logoButton->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_logoButton->setCursor(Qt::ArrowCursor);

    m_titleContainer = new QWidget(this);
    m_titleContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_titleContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* titleLayout = new QHBoxLayout(m_titleContainer);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(0);

    m_editorLabel = new QLabel(this);
    m_editorLabel->setText(tr("Brush Editor"));
    m_editorLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    m_titleLabel->setMinimumWidth(0);
    m_titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    titleLayout->addWidget(m_editorLabel);
    titleLayout->addWidget(m_titleLabel, 1);

    m_closeButton = new WindowControlButton(WindowControlButton::Type::Close, this);
    m_closeButton->setToolTip(tr("Close"));
    connect(m_closeButton, &QPushButton::clicked, this, &BrushEditorTopBar::closeRequested);

    m_mainLayout->addWidget(m_logoButton, 0, Qt::AlignVCenter);
    m_mainLayout->addWidget(m_titleContainer, 1, Qt::AlignVCenter);
    m_mainLayout->addWidget(m_closeButton, 0, Qt::AlignVCenter);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &BrushEditorTopBar::onThemeChanged);

    onThemeChanged();
    updateTitleText();
}

void BrushEditorTopBar::setBrushName(const QString& brushName)
{
    if (m_brushName == brushName) {
        return;
    }
    m_brushName = brushName;
    updateTitleText();
}

QWidget* BrushEditorTopBar::closeButton() const
{
    return m_closeButton;
}

void BrushEditorTopBar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        m_editorLabel->setText(tr("Brush Editor"));
        m_closeButton->setToolTip(tr("Close"));
        updateTitleText();
    }
}

void BrushEditorTopBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const int vi = visualInsetPx();

    if (width() <= 2 * vi + 2 || height() <= vi + 2) {
        painter.fillRect(rect(), colors.surface);
        return;
    }

    const QRectF innerRect(
        vi + 0.5, vi + 0.5, qreal(width() - 2 * vi - 1), qreal(height() - vi - 1));

    QPainterPath shape;
    shape.addRoundedRect(innerRect, theme.scaled(10), theme.scaled(10));

    QLinearGradient gradient(0, vi, 0, height());
    gradient.setColorAt(0, colors.surface);
    gradient.setColorAt(1, ThemeColors::adjustBrightness(colors.surface, 100.0 / 102));

    painter.fillRect(rect(), colors.surface);
    painter.fillPath(shape, gradient);

    QPen outline(colors.border, 1.0);
    outline.setCosmetic(true);
    outline.setJoinStyle(Qt::RoundJoin);
    outline.setCapStyle(Qt::RoundCap);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(shape);
}

void BrushEditorTopBar::onThemeChanged()
{
    updateScaledSizes();
    updateStyles();
    update();
}

int BrushEditorTopBar::visualInsetPx() const
{
    return ThemeManager::instance().scaled(BaseVisualInset);
}

void BrushEditorTopBar::updateTitleText()
{
    const QString brushName = m_brushName.trimmed();
    m_titleLabel->setText(brushName.isEmpty() ? tr("Brush") : brushName);
}

void BrushEditorTopBar::updateScaledSizes()
{
    auto& theme = ThemeManager::instance();
    const int g = visualInsetPx();
    const int bottomInset = 1;
    const int contentH = theme.scaled(BaseHeight);

    setFixedHeight(contentH + g + bottomInset);
    m_mainLayout->setContentsMargins(g + theme.scaled(BaseSidePadding), g, g, bottomInset);
    m_mainLayout->setSpacing(theme.scaled(6));
    m_logoButton->setFixedSize(theme.scaled(28), theme.scaled(28));
    m_closeButton->setFixedSize(theme.scaled(BaseCloseButtonWidth), contentH);

    if (auto* titleLayout = qobject_cast<QHBoxLayout*>(m_titleContainer->layout())) {
        titleLayout->setSpacing(theme.scaled(BaseTitleGap));
    }

    QFont editorFont = m_editorLabel->font();
    editorFont.setPixelSize(theme.scaled(11));
    editorFont.setWeight(QFont::DemiBold);
    m_editorLabel->setFont(editorFont);

    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(theme.scaled(11));
    titleFont.setWeight(QFont::Medium);
    m_titleLabel->setFont(titleFont);
}

void BrushEditorTopBar::updateStyles()
{
    const auto& colors = ThemeManager::instance().colors();

    m_titleContainer->setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
    m_editorLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.text.name(QColor::HexArgb)));
    m_titleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(colors.textMuted.name(QColor::HexArgb)));
}

} // namespace ruwa::ui::windows
