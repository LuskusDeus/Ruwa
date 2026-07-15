// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P O S I T I O N   P I C K E R   O V E R L A Y
// ==========================================================================

#include "CanvasPositionPickerOverlay.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyleManager.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSizePolicy>

namespace ruwa::ui::widgets {

namespace {
constexpr int kCornerRadiusBase = 6;
constexpr int kHorizontalPaddingBase = 10;
constexpr int kVerticalPaddingBase = 5;
// Offset from the cursor tip so the capsule doesn't sit under the pointer.
constexpr int kCursorOffsetXBase = 16;
constexpr int kCursorOffsetYBase = 20;
} // namespace

CanvasPositionPickerOverlay::CanvasPositionPickerOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_label = new QLabel(this);
    m_label->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setObjectName(QStringLiteral("canvasPositionPickerLabel"));
    m_label->setText(QStringLiteral("X: 0  Y: 0"));

    auto* layout = new QHBoxLayout(this);
    layout->setSpacing(0);
    layout->addWidget(m_label);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() { applyTheme(); });

    applyTheme();
    hide();
}

void CanvasPositionPickerOverlay::setDocumentPosition(const QPointF& pos)
{
    if (m_label) {
        m_label->setText(QStringLiteral("X: %1  Y: %2").arg(qRound(pos.x())).arg(qRound(pos.y())));
    }
    adjustSize();
}

void CanvasPositionPickerOverlay::followCursor(const QPoint& localCursorPos)
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int offsetX = theme.scaled(kCursorOffsetXBase);
    const int offsetY = theme.scaled(kCursorOffsetYBase);

    QWidget* parent = parentWidget();
    const int maxX = parent ? qMax(0, parent->width() - width()) : localCursorPos.x() + offsetX;
    const int maxY = parent ? qMax(0, parent->height() - height()) : localCursorPos.y() + offsetY;

    move(qBound(0, localCursorPos.x() + offsetX, maxX),
        qBound(0, localCursorPos.y() + offsetY, maxY));
}

void CanvasPositionPickerOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    auto& style = ruwa::ui::core::WidgetStyleManager::instance();
    auto& theme = ruwa::ui::core::ThemeManager::instance();

    QColor bgColor = style.colors().surface;
    bgColor.setAlpha(215);
    QColor borderTopColor = style.colors().primary;
    borderTopColor.setAlpha(160);
    QColor borderBottomColor = style.colors().borderDark();
    borderBottomColor.setAlpha(95);

    const int radius = theme.scaled(kCornerRadiusBase);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawPath(path);

    ruwa::ui::painting::drawGradientBorder(
        painter, rect(), radius, borderTopColor, borderBottomColor);
}

void CanvasPositionPickerOverlay::applyTheme()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const int horizontalPadding = theme.scaled(kHorizontalPaddingBase);
    const int verticalPadding = theme.scaled(kVerticalPaddingBase);

    if (auto* boxLayout = qobject_cast<QHBoxLayout*>(layout())) {
        boxLayout->setContentsMargins(
            horizontalPadding, verticalPadding, horizontalPadding, verticalPadding);
    }

    if (m_label) {
        QFont font = colors.fonts.getUIFont(theme.scaledFontSize(10));
        font.setWeight(QFont::DemiBold);
        m_label->setFont(font);
        m_label->setStyleSheet(QStringLiteral(
            "QLabel#canvasPositionPickerLabel { background: transparent; color: rgb(%1, %2, %3); }")
                .arg(colors.text.red())
                .arg(colors.text.green())
                .arg(colors.text.blue()));
    }

    adjustSize();
    update();
}

} // namespace ruwa::ui::widgets
