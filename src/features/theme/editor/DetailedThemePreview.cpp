// SPDX-License-Identifier: MPL-2.0

#include "DetailedThemePreview.h"
#include "shared/i18n/TranslationManager.h"

#include <QPainterPath>
#include <QResizeEvent>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
// --- Base Geometry Constants (will be scaled) ---
const int BASE_HERO_WIDTH = 560;
const int BASE_HERO_HEIGHT = 220;

const int LIST_WIDTH_PERCENT = 92;
const int LIST_OFFSET_Y = 190;
const int BASE_LIST_HEIGHT = 250;

const int BASE_PADDING = 28;

// Minimum size for proper display
const int MIN_PREVIEW_WIDTH = 600;
const int MIN_PREVIEW_HEIGHT = 500;

// --- Helper to create gradient pens ---
QPen createGradientPen(const QRect& rect, const QColor& baseColor)
{
    QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
    grad.setColorAt(0.0, baseColor.lighter(160));
    grad.setColorAt(1.0, baseColor);
    return QPen(QBrush(grad), 1.0);
}
} // namespace

DetailedThemePreview::DetailedThemePreview(QWidget* parent)
    : QWidget(parent)
{
    m_theme = ThemePreset::obsidian();
    setMinimumSize(MIN_PREVIEW_WIDTH, MIN_PREVIEW_HEIGHT);
    calculateScale();

    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, [this]() { update(); });
}

void DetailedThemePreview::setPreviewTheme(const ThemePreset& preset)
{
    m_theme = preset;
    update();
}

void DetailedThemePreview::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    calculateScale();
}

void DetailedThemePreview::calculateScale()
{
    // Calculate scale based on available space
    // Take minimum scale between width and height
    qreal scaleX = qreal(width()) / (BASE_HERO_WIDTH + 200); // +200 for margins and pointers
    qreal scaleY = qreal(height()) / (BASE_HERO_HEIGHT + BASE_LIST_HEIGHT + 200);

    m_scale = qMin(scaleX, scaleY);

    // Clamp min and max scale
    m_scale = qBound(0.5, m_scale, 1.5);
}

void DetailedThemePreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Create rounded clipping path for the entire widget
    int panelRadius = scaled(16);
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect(), panelRadius, panelRadius);
    p.setClipPath(clipPath);

    // 1. Background (Canvas)
    p.fillRect(rect(), m_theme.background);
    drawDotGrid(p, rect());

    // --- Layout Calculation with scaling ---
    QPoint center = rect().center();

    // Hero Rect
    int heroWidth = scaled(BASE_HERO_WIDTH);
    int heroHeight = scaled(BASE_HERO_HEIGHT);
    QRect heroRect(0, 0, heroWidth, heroHeight);
    heroRect.moveCenter(center - scaled(QPoint(0, 50)));

    // List Rect
    int listWidth = (heroWidth * LIST_WIDTH_PERCENT) / 100;
    int listX = heroRect.center().x() - (listWidth / 2);
    int listY = heroRect.top() + scaled(LIST_OFFSET_Y);
    int listHeight = scaled(BASE_LIST_HEIGHT);
    QRect listRect(listX, listY, listWidth, listHeight);

    // --- Draw UI Layers ---

    // Layer 1: Hero Card
    drawHeroCard(p, heroRect);

    // Shadow cast by List onto Hero/Bg
    QRect shadowRect = listRect.adjusted(0, scaled(8), 0, scaled(24));
    QLinearGradient shadowGrad(shadowRect.topLeft(), shadowRect.bottomLeft());
    shadowGrad.setColorAt(0.0, QColor(0, 0, 0, 40));
    shadowGrad.setColorAt(1.0, Qt::transparent);
    p.setPen(Qt::NoPen);
    p.setBrush(shadowGrad);
    p.drawRoundedRect(shadowRect, scaled(12), scaled(12));

    // Layer 2: Floating List
    drawFloatingListPanel(p, listRect);

    // --- Layer 3: Design Pointers (Overlay) ---
    int padding = scaled(BASE_PADDING);

    // 1. Pointer -> Background (Label only, centered at top)
    QPoint bgLabelPos(rect().center().x(), scaled(40));
    drawPointer(p, bgLabelPos, tr("Background"), QPoint(0, 0), PointerType::LabelOnly);

    // 2. Pointer -> Surface (Hero Card top edge)
    QPoint surfaceTarget = heroRect.topLeft() + QPoint(heroWidth / 2, scaled(10));
    drawPointer(p, surfaceTarget, tr("Surface"), scaled(QPoint(0, -50)), PointerType::Full);

    // 3. Pointer -> Text (Main Title)
    QPoint titlePos = heroRect.topLeft() + QPoint(padding + scaled(370), padding + scaled(65));
    drawPointer(p, titlePos, tr("Text Main"), scaled(QPoint(210, -30)), PointerType::Full);

    // 4. Pointer -> Primary (Create Project Button)
    QPoint btnPos = heroRect.topLeft() + QPoint(padding + scaled(10), padding + scaled(140));
    drawPointer(p, btnPos, tr("Primary"), scaled(QPoint(-120, -20)), PointerType::Full);

    // 5. Pointer -> Surface Alt (List Panel)
    QPoint listPos = listRect.topLeft() + QPoint(scaled(10), scaled(40));
    drawPointer(p, listPos, tr("Surface Alt"), scaled(QPoint(-120, 20)), PointerType::Full);

    // 6. Pointer -> Border (Line ending on border, no dot)
    QPoint borderTarget = listRect.bottomRight();
    drawPointer(p, borderTarget, tr("Border"), scaled(QPoint(60, 20)), PointerType::LineOnly);
}

void DetailedThemePreview::drawDotGrid(QPainter& painter, const QRect& rect)
{
    painter.save();

    QColor dotColor = m_theme.border;
    dotColor.setAlphaF(0.25);
    painter.setBrush(dotColor);
    painter.setPen(Qt::NoPen);

    int step = scaled(28);
    int dotSize = qMax(3, scaled(4)); // Increased from 3 to 4 pixels

    for (int x = 0; x < rect.width(); x += step) {
        for (int y = 0; y < rect.height(); y += step) {
            if ((x / step + y / step) % 2 == 0) {
                painter.drawEllipse(x, y, dotSize, dotSize);
            }
        }
    }
    painter.restore();
}

void DetailedThemePreview::drawHeroCard(QPainter& painter, const QRect& rect)
{
    painter.save();

    int padding = scaled(BASE_PADDING);
    int cornerRadius = scaled(12);

    painter.setBrush(m_theme.surface);
    painter.setPen(createGradientPen(rect, m_theme.border));
    painter.drawRoundedRect(rect, cornerRadius, cornerRadius);

    int x = rect.left() + padding;
    int y = rect.top() + padding;

    // "Welcome to Ruwa"
    QFont font = painter.font();
    font.setPixelSize(scaled(11));
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(m_theme.primary);
    painter.drawText(x, y + scaled(10), tr("Welcome to Ruwa"));

    // Search Stub
    QRect searchRect(rect.right() - scaled(140) - padding, y, scaled(140), scaled(30));
    painter.setPen(createGradientPen(searchRect, m_theme.border));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(searchRect, scaled(6), scaled(6));

    painter.setPen(Qt::NoPen);
    painter.setBrush(m_theme.surfaceAlt);
    painter.drawRoundedRect(
        searchRect.adjusted(scaled(8), scaled(10), -scaled(50), -scaled(10)), scaled(2), scaled(2));

    y += scaled(50);

    // Title
    font.setPixelSize(scaled(24));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(m_theme.text);
    painter.drawText(x, y + scaled(20), tr("Digital Painting Reimagined"));

    y += scaled(36);

    // Subtitle
    font.setPixelSize(scaled(11));
    font.setBold(false);
    painter.setFont(font);
    painter.setPen(m_theme.textMuted);
    painter.drawText(x, y + scaled(10), tr("Free, open-source, and limitless."));

    y += scaled(32);

    // Button
    QRect btnRect(x, y, scaled(130), scaled(36));
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_theme.primary);
    painter.drawRoundedRect(btnRect, scaled(6), scaled(6));

    painter.setPen(m_theme.textOnPrimary);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(btnRect, Qt::AlignCenter, tr("Create Project"));

    painter.restore();
}

void DetailedThemePreview::drawFloatingListPanel(QPainter& painter, const QRect& rect)
{
    painter.save();

    int padding = scaled(BASE_PADDING);
    int cornerRadius = scaled(12);

    painter.setBrush(m_theme.surfaceAlt);
    painter.setPen(createGradientPen(rect, m_theme.border));
    painter.drawRoundedRect(rect, cornerRadius, cornerRadius);

    int contentX = rect.left() + padding;
    int contentY = rect.top() + scaled(24);

    // Header
    QFont font = painter.font();
    font.setPixelSize(scaled(9));
    font.setBold(true);
    font.setCapitalization(QFont::AllUppercase);
    painter.setFont(font);
    painter.setPen(m_theme.textMuted);
    painter.drawText(contentX, contentY, tr("Recent Projects"));

    contentY += scaled(20);

    // List Items
    int itemHeight = scaled(30);
    int gap = scaled(12);

    for (int i = 0; i < 4; ++i) {
        QRect itemRect(contentX, contentY, rect.width() - (padding * 2), itemHeight);

        // Hover highlight
        if (i == 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(m_theme.surface);
            painter.drawRoundedRect(itemRect, scaled(4), scaled(4));
        }

        int cy = itemRect.center().y();

        // Icon
        QRect iconRect(itemRect.left() + scaled(8), cy - scaled(5), scaled(10), scaled(10));
        painter.setBrush(i == 0 ? m_theme.text : m_theme.border);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(iconRect, scaled(2), scaled(2));

        // Lines
        int lineWidth = (itemRect.width() * 0.5) + ((i * 50) % 80);
        QRect lineRect(iconRect.right() + scaled(12), cy - scaled(2), lineWidth, scaled(4));

        QColor lineColor = m_theme.text;
        lineColor.setAlphaF(i == 0 ? 0.9 : 0.3);
        painter.setBrush(lineColor);
        painter.drawRoundedRect(lineRect, scaled(2), scaled(2));

        contentY += itemHeight + gap;
    }

    painter.restore();
}

void DetailedThemePreview::drawPointer(
    QPainter& painter, QPoint target, QString label, QPoint offset, PointerType type)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor accent = m_theme.primary.lighter(120);
    QColor lineColor = m_theme.textMuted;
    lineColor.setAlphaF(0.5);

    // Calculate label position
    QPoint labelPos = target + offset;

    // Setup font for label
    QFont font = painter.font();
    font.setPixelSize(scaled(10));
    font.setBold(true);
    painter.setFont(font);

    QFontMetrics fm(font);
    int textWidth = fm.horizontalAdvance(label);
    int textHeight = fm.height();
    int padH = scaled(12);
    int padV = scaled(6);

    // Create label tag rect
    QRect tagRect(0, 0, textWidth + (padH * 2), textHeight + padV);

    switch (type) {
    case PointerType::Full:
        // 1. Draw anchor dot
        painter.setPen(Qt::NoPen);
        painter.setBrush(accent);
        painter.drawEllipse(target, scaled(3), scaled(3));

        // 2. Draw line
        painter.setPen(QPen(lineColor, m_scale));
        painter.drawLine(target, labelPos);

        // 3. Draw label
        tagRect.moveCenter(labelPos);
        break;

    case PointerType::LineOnly:
        // No dot, just line ending at target
        painter.setPen(QPen(lineColor, m_scale));
        painter.drawLine(target, labelPos);

        // Position label at offset end
        tagRect.moveCenter(labelPos);
        break;

    case PointerType::LabelOnly:
        // Only label, centered at target position
        tagRect.moveCenter(target);
        break;
    }

    // Draw label background and text
    painter.setBrush(m_theme.background);
    painter.setPen(createGradientPen(tagRect, m_theme.border));
    painter.drawRoundedRect(tagRect, scaled(6), scaled(6));

    painter.setPen(m_theme.text);
    painter.drawText(tagRect, Qt::AlignCenter, label);

    painter.restore();
}

} // namespace ruwa::ui::widgets
