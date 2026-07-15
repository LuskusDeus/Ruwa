// SPDX-License-Identifier: MPL-2.0

#include "CurveEditorWidget.h"

#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace ruwa::ui::widgets {

namespace {

using Point = CurveEditorWidget::Point;

constexpr qreal kPointRadius = 5.0;
constexpr qreal kHitPadding = 8.0;
constexpr qreal kMinPointSpacing = 0.045;
constexpr qreal kPointHaloRadius = 3.0;
constexpr qreal kPlotPadding = 3.0;
constexpr qreal kAxisGap = 8.0;
constexpr qreal kAxisLeftWidth = 38.0;
constexpr qreal kAxisBottomHeight = 18.0;

QVector<qreal> axisTicks(const CurveEditorWidget::AxisDisplaySpec& spec)
{
    if (!spec.tickValues.isEmpty()) {
        return spec.tickValues;
    }
    return { spec.minValue, spec.maxValue };
}

QVector<Point> defaultPoints(qreal minValue, qreal maxValue)
{
    const qreal span = qMax<qreal>(0.0001, maxValue - minValue);
    return {
        { 0.0, minValue + span * 0.08, 0.35 },
        { 0.24, minValue + span * 0.22, 0.72 },
        { 0.68, minValue + span * 0.82, 0.72 },
        { 1.0, minValue + span * 0.94, 0.35 },
    };
}

qreal clampToRange(qreal value, qreal minValue, qreal maxValue)
{
    return qBound(minValue, value, maxValue);
}

QVector<Point> sanitizePoints(QVector<Point> points, qreal minValue, qreal maxValue)
{
    if (points.size() < 2) {
        points = defaultPoints(minValue, maxValue);
    }

    std::sort(
        points.begin(), points.end(), [](const Point& a, const Point& b) { return a.x < b.x; });

    for (Point& point : points) {
        point.x = clampToRange(point.x, 0.0, 1.0);
        point.y = clampToRange(point.y, minValue, maxValue);
        point.smoothness = clampToRange(point.smoothness, 0.0, 1.0);
    }

    points.front().x = 0.0;
    points.back().x = 1.0;

    for (int i = 1; i < points.size() - 1; ++i) {
        const qreal minX = points[i - 1].x + 0.001;
        const qreal maxX = points[i + 1].x - 0.001;
        points[i].x = qBound(minX, points[i].x, maxX);
    }

    return points;
}

} // namespace

using Point = CurveEditorWidget::Point;

class CurveEditorPlot final : public QWidget {
public:
    using AxisDisplaySpec = CurveEditorWidget::AxisDisplaySpec;

    explicit CurveEditorPlot(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_points(defaultPoints(0.0, 1.0))
        , m_selectedIndex(1)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setProperty("ruwaContextMenuSystemBypass", true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    QVector<Point> points() const { return m_points; }

    void setPoints(const QVector<Point>& points)
    {
        m_points = sanitizePoints(points, m_valueMin, m_valueMax);
        if (m_selectedIndex >= 0) {
            m_selectedIndex = qBound(0, m_selectedIndex, m_points.size() - 1);
        }
        update();
    }

    void setValueRange(qreal minValue, qreal maxValue)
    {
        const qreal boundedMin = qMin(minValue, maxValue);
        const qreal boundedMax = qMax<qreal>(boundedMin + 0.0001, qMax(minValue, maxValue));
        if (qFuzzyCompare(m_valueMin, boundedMin) && qFuzzyCompare(m_valueMax, boundedMax)) {
            return;
        }
        m_valueMin = boundedMin;
        m_valueMax = boundedMax;
        m_points = sanitizePoints(m_points, m_valueMin, m_valueMax);
        update();
    }

    qreal valueMax() const { return m_valueMax; }
    void setHorizontalAxisDisplay(const AxisDisplaySpec& spec)
    {
        m_horizontalAxisDisplay = spec;
        update();
    }

    AxisDisplaySpec horizontalAxisDisplay() const { return m_horizontalAxisDisplay; }

    void setVerticalAxisDisplay(const AxisDisplaySpec& spec)
    {
        m_verticalAxisDisplay = spec;
        update();
    }

    AxisDisplaySpec verticalAxisDisplay() const { return m_verticalAxisDisplay; }

    int selectedPointIndex() const { return m_selectedIndex; }

    void setSelectedPointIndex(int index)
    {
        const int bounded = (index >= 0 && index < m_points.size()) ? index : -1;
        if (m_selectedIndex == bounded) {
            return;
        }
        m_selectedIndex = bounded;
        update();
        if (selectionChanged) {
            selectionChanged(m_selectedIndex);
        }
    }

    std::function<void()> pointsChanged;
    std::function<void()> editingFinished;
    std::function<void(int)> selectionChanged;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& colors = core::WidgetStyleManager::instance().colors();
        auto& theme = core::ThemeManager::instance();
        const QRectF plot = plotRect();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const qreal radius = theme.scaled(12.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceAlt);
        painter.drawRoundedRect(plot, radius, radius);

        painter.setPen(QPen(colors.borderSubtleHover(), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(plot.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

        drawAxisLabels(painter, plot);

        painter.save();
        QPainterPath plotClipPath;
        plotClipPath.addRoundedRect(plot.adjusted(1.0, 1.0, -1.0, -1.0),
            qMax<qreal>(0.0, radius - 1.0), qMax<qreal>(0.0, radius - 1.0));
        painter.setClipPath(plotClipPath);

        QColor gridColor = colors.borderSubtle();
        gridColor.setAlpha(colors.isDark ? 28 : 18);
        painter.setPen(QPen(gridColor, 1.0, Qt::DashLine));
        for (int i = 1; i < 4; ++i) {
            const qreal x = plot.left() + plot.width() * (static_cast<qreal>(i) / 4.0);
            const qreal y = plot.top() + plot.height() * (static_cast<qreal>(i) / 4.0);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        const QVector<QPointF> samples = sampledPolyline(plot);
        if (!samples.isEmpty()) {
            QPainterPath fillPath;
            fillPath.moveTo(QPointF(plot.left(), plot.bottom()));
            fillPath.lineTo(samples.front());
            for (const QPointF& sample : samples) {
                fillPath.lineTo(sample);
            }
            fillPath.lineTo(QPointF(plot.right(), plot.bottom()));
            fillPath.closeSubpath();

            QColor fillColor = colors.primary;
            fillColor.setAlpha(colors.isDark ? 30 : 22);
            painter.fillPath(fillPath, fillColor);

            QPainterPath curvePath;
            curvePath.moveTo(samples.front());
            for (int i = 1; i < samples.size(); ++i) {
                curvePath.lineTo(samples[i]);
            }

            painter.setPen(QPen(colors.primaryHover(), theme.scaled(2.2), Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin));
            painter.drawPath(curvePath);
        }

        painter.restore();

        for (int i = 0; i < m_points.size(); ++i) {
            const QPointF pointPos = pointToPixel(m_points[i], plot);
            const bool isSelected = (i == m_selectedIndex);
            const bool isHovered = (i == m_hoveredIndex);
            const qreal pointRadius = theme.scaled(kPointRadius + (isSelected ? 1.0 : 0.0));
            const QRectF pointRect(pointPos.x() - pointRadius, pointPos.y() - pointRadius,
                pointRadius * 2.0, pointRadius * 2.0);

            if (isSelected || isHovered) {
                QColor halo = colors.primary;
                halo.setAlpha(isSelected ? 95 : 55);
                painter.setPen(Qt::NoPen);
                painter.setBrush(halo);
                const qreal haloRadius = theme.scaled(kPointHaloRadius);
                painter.drawEllipse(
                    pointRect.adjusted(-haloRadius, -haloRadius, haloRadius, haloRadius));
            }

            painter.setPen(
                QPen(isSelected ? colors.primaryHover() : colors.surfaceElevated(), 1.0));
            painter.setBrush(isSelected ? colors.textOnPrimary() : colors.text);
            painter.drawEllipse(pointRect);
        }
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !plotRect().contains(event->position())) {
            QWidget::mouseDoubleClickEvent(event);
            return;
        }

        if (hitTest(event->position()) >= 0) {
            event->accept();
            return;
        }

        addPoint(event->position());
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const int hitIndex = hitTest(event->position());
            setFocus(Qt::MouseFocusReason);
            setSelectedPointIndex(hitIndex);
            if (hitIndex >= 0) {
                m_draggingIndex = hitIndex;
                m_dragging = true;
            }
            event->accept();
            return;
        }

        if (event->button() == Qt::RightButton) {
            const int hitIndex = hitTest(event->position());
            if (removePointAt(hitIndex)) {
                m_suppressNextContextMenu = true;
                event->accept();
                return;
            }
        }

        QWidget::mousePressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        if (m_suppressNextContextMenu) {
            m_suppressNextContextMenu = false;
            event->accept();
            return;
        }

        if (event->reason() == QContextMenuEvent::Mouse) {
            const int hitIndex = hitTest(event->pos());
            if (removePointAt(hitIndex)) {
                event->accept();
                return;
            }
        }

        QWidget::contextMenuEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_dragging && m_draggingIndex >= 0) {
            movePoint(m_draggingIndex, event->position());
            event->accept();
            return;
        }

        const int hovered = hitTest(event->position());
        if (hovered != m_hoveredIndex) {
            m_hoveredIndex = hovered;
            update();
        }

        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            const bool wasDragging = m_dragging;
            m_dragging = false;
            m_draggingIndex = -1;
            if (wasDragging && editingFinished) {
                editingFinished();
            }
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hoveredIndex = -1;
        update();
        QWidget::leaveEvent(event);
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
            && m_selectedIndex > 0 && m_selectedIndex < m_points.size() - 1) {
            if (removePointAt(m_selectedIndex)) {
                event->accept();
                return;
            }
        }

        QWidget::keyPressEvent(event);
    }

private:
    QRectF plotRect() const
    {
        auto& theme = core::ThemeManager::instance();
        const qreal pointRadius = theme.scaled(kPointRadius + 1.0);
        const qreal outerRadius
            = pointRadius + theme.scaled(kPointHaloRadius) + theme.scaled(kPlotPadding);
        const qreal topInset = qMax<qreal>(theme.scaled(6), outerRadius);
        const qreal rightInset = qMax<qreal>(theme.scaled(6), outerRadius);
        const qreal leftInset
            = qMax<qreal>(theme.scaled(kAxisLeftWidth), outerRadius) + theme.scaled(kAxisGap);
        const qreal bottomInset
            = qMax<qreal>(theme.scaled(kAxisBottomHeight), outerRadius) + theme.scaled(kAxisGap);
        return QRectF(rect()).adjusted(leftInset, topInset, -rightInset, -bottomInset);
    }

    QPointF pointToPixel(const Point& point, const QRectF& plot) const
    {
        return QPointF(plot.left() + point.x * plot.width(),
            plot.bottom()
                - ((point.y - m_valueMin) / qMax<qreal>(0.0001, m_valueMax - m_valueMin))
                    * plot.height());
    }

    QPointF pixelToNormalized(const QPointF& position) const
    {
        const QRectF plot = plotRect();
        if (plot.width() <= 1.0 || plot.height() <= 1.0) {
            return {};
        }

        const qreal x = clampToRange((position.x() - plot.left()) / plot.width(), 0.0, 1.0);
        const qreal normalizedY
            = clampToRange((plot.bottom() - position.y()) / plot.height(), 0.0, 1.0);
        const qreal y = m_valueMin + normalizedY * (m_valueMax - m_valueMin);
        return QPointF(x, y);
    }

    QString formatAxisValue(const AxisDisplaySpec& spec, qreal value) const
    {
        const qreal displayValue = value * spec.displayScale;
        return QStringLiteral("%1%2")
            .arg(QLocale().toString(displayValue, 'f', spec.displayDecimals))
            .arg(spec.suffix);
    }

    void drawAxisLabels(QPainter& painter, const QRectF& plot) const
    {
        auto& theme = core::ThemeManager::instance();
        const auto& colors = core::WidgetStyleManager::instance().colors();

        QFont axisFont = font();
        axisFont.setPixelSize(theme.scaled(9));
        painter.setFont(axisFont);
        const QFontMetrics axisMetrics(axisFont);
        painter.setPen(colors.textMuted);

        if (m_verticalAxisDisplay.visible) {
            const QRectF verticalLabelRect(
                0.0, plot.top(), plot.left() - theme.scaled(kAxisGap), plot.height());
            const qreal verticalRange = qMax<qreal>(
                0.0001, m_verticalAxisDisplay.maxValue - m_verticalAxisDisplay.minValue);
            const QVector<qreal> ticks = axisTicks(m_verticalAxisDisplay);
            for (const qreal tick : ticks) {
                const qreal normalized
                    = qBound(0.0, (tick - m_verticalAxisDisplay.minValue) / verticalRange, 1.0);
                const qreal y = plot.bottom() - normalized * plot.height();
                painter.drawText(QRectF(verticalLabelRect.left(), y - axisMetrics.height() * 0.5,
                                     verticalLabelRect.width(), axisMetrics.height()),
                    Qt::AlignRight | Qt::AlignVCenter,
                    formatAxisValue(m_verticalAxisDisplay, tick));
            }
        }

        if (m_horizontalAxisDisplay.visible) {
            const QRectF horizontalLabelRect(plot.left(), plot.bottom() + theme.scaled(kAxisGap),
                plot.width(), height() - plot.bottom() - theme.scaled(kAxisGap));
            const qreal horizontalRange = qMax<qreal>(
                0.0001, m_horizontalAxisDisplay.maxValue - m_horizontalAxisDisplay.minValue);
            const QVector<qreal> ticks = axisTicks(m_horizontalAxisDisplay);
            for (int i = 0; i < ticks.size(); ++i) {
                const qreal tick = ticks[i];
                const qreal normalized
                    = qBound(0.0, (tick - m_horizontalAxisDisplay.minValue) / horizontalRange, 1.0);
                const qreal x = plot.left() + normalized * plot.width();
                const QString text = formatAxisValue(m_horizontalAxisDisplay, tick);
                const int textWidth = axisMetrics.horizontalAdvance(text) + theme.scaled(8);

                QRectF textRect(x - textWidth * 0.5, horizontalLabelRect.top(), textWidth,
                    horizontalLabelRect.height());
                if (i == 0) {
                    textRect.moveLeft(horizontalLabelRect.left());
                } else if (i == ticks.size() - 1) {
                    textRect.moveRight(horizontalLabelRect.right());
                }

                painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, text);
            }
        }
    }

    int hitTest(const QPointF& position) const
    {
        const QRectF plot = plotRect();
        const qreal radius = core::ThemeManager::instance().scaled(kPointRadius + kHitPadding);
        for (int i = m_points.size() - 1; i >= 0; --i) {
            if (QLineF(position, pointToPixel(m_points[i], plot)).length() <= radius) {
                return i;
            }
        }
        return -1;
    }

    bool removePointAt(int index)
    {
        if (index <= 0 || index >= m_points.size() - 1) {
            return false;
        }

        m_points.removeAt(index);
        m_hoveredIndex = -1;
        m_dragging = false;
        m_draggingIndex = -1;
        setSelectedPointIndex(qMin(index, m_points.size() - 1));
        if (pointsChanged) {
            pointsChanged();
        }
        if (editingFinished) {
            editingFinished();
        }
        update();
        return true;
    }

    void addPoint(const QPointF& position)
    {
        Point point;
        const QPointF normalized = pixelToNormalized(position);
        point.x = normalized.x();
        point.y = normalized.y();
        point.smoothness = 0.65;

        int insertIndex = 1;
        while (insertIndex < m_points.size() && m_points[insertIndex].x < point.x) {
            ++insertIndex;
        }

        const qreal minX = m_points[insertIndex - 1].x + kMinPointSpacing;
        const qreal maxX = m_points[insertIndex].x - kMinPointSpacing;
        if (maxX <= minX) {
            return;
        }

        point.x = qBound(minX, point.x, maxX);
        m_points.insert(insertIndex, point);
        movePoint(insertIndex, position, false);
        setSelectedPointIndex(insertIndex);
        m_draggingIndex = insertIndex;
        m_dragging = true;
        m_hoveredIndex = insertIndex;
        if (pointsChanged) {
            pointsChanged();
        }
        update();
    }

    void movePoint(int index, const QPointF& position, bool notify = true)
    {
        if (index < 0 || index >= m_points.size()) {
            return;
        }

        const QPointF normalized = pixelToNormalized(position);
        Point& point = m_points[index];
        point.y = normalized.y();

        if (index == 0) {
            point.x = 0.0;
        } else if (index == m_points.size() - 1) {
            point.x = 1.0;
        } else {
            const qreal minX = m_points[index - 1].x + kMinPointSpacing;
            const qreal maxX = m_points[index + 1].x - kMinPointSpacing;
            point.x = (maxX > minX) ? qBound(minX, normalized.x(), maxX) : (minX + maxX) * 0.5;
        }

        update();
        if (notify && pointsChanged) {
            pointsChanged();
        }
    }

    QVector<QPointF> sampledPolyline(const QRectF& plot) const
    {
        QVector<QPointF> samples;
        if (m_points.isEmpty()) {
            return samples;
        }

        const int stepsPerSegment = qMax(18, width() / 28);
        samples.reserve((m_points.size() - 1) * stepsPerSegment + 1);
        for (int i = 0; i < m_points.size() - 1; ++i) {
            for (int step = 0; step < stepsPerSegment; ++step) {
                const qreal t = static_cast<qreal>(step) / static_cast<qreal>(stepsPerSegment);
                samples.append(
                    pointToPixel({ m_points[i].x + (m_points[i + 1].x - m_points[i].x) * t,
                                     evaluateSegment(i, t), 0.0 },
                        plot));
            }
        }
        samples.append(pointToPixel(m_points.back(), plot));
        return samples;
    }

    qreal evaluateSegment(int index, qreal t) const
    {
        const Point& p0 = m_points[index];
        const Point& p1 = m_points[index + 1];
        const qreal dx = qMax<qreal>(0.0001, p1.x - p0.x);
        const qreal startTangent = pchipTangent(index);
        const qreal endTangent = pchipTangent(index + 1);

        const qreal t2 = t * t;
        const qreal t3 = t2 * t;
        const qreal h00 = (2.0 * t3) - (3.0 * t2) + 1.0;
        const qreal h10 = t3 - (2.0 * t2) + t;
        const qreal h01 = (-2.0 * t3) + (3.0 * t2);
        const qreal h11 = t3 - t2;

        return clampToRange(
            h00 * p0.y + h10 * dx * startTangent + h01 * p1.y + h11 * dx * endTangent, m_valueMin,
            m_valueMax);
    }

    qreal pchipTangent(int index) const
    {
        if (m_points.size() <= 2) {
            return simpleSlope(0, 1);
        }
        if (index <= 0) {
            return endpointTangent(0);
        }
        if (index >= m_points.size() - 1) {
            return endpointTangent(m_points.size() - 1);
        }

        const qreal leftSlope = simpleSlope(index - 1, index);
        const qreal rightSlope = simpleSlope(index, index + 1);
        if (slopeSign(leftSlope) != slopeSign(rightSlope)) {
            return 0.0;
        }
        if (slopeSign(leftSlope) == 0) {
            return 0.0;
        }

        const qreal leftDx = segmentWidth(index - 1, index);
        const qreal rightDx = segmentWidth(index, index + 1);
        const qreal w1 = 2.0 * rightDx + leftDx;
        const qreal w2 = rightDx + 2.0 * leftDx;
        return (w1 + w2) / ((w1 / leftSlope) + (w2 / rightSlope));
    }

    qreal endpointTangent(int index) const
    {
        const bool leftEndpoint = index <= 0;
        const int edgeIndex = leftEndpoint ? 0 : m_points.size() - 2;
        const int nextIndex = leftEndpoint ? 1 : m_points.size() - 3;
        const qreal edgeDx = segmentWidth(edgeIndex, edgeIndex + 1);
        const qreal nextDx = segmentWidth(nextIndex, nextIndex + 1);
        const qreal edgeSlope = simpleSlope(edgeIndex, edgeIndex + 1);
        const qreal nextSlope = simpleSlope(nextIndex, nextIndex + 1);
        qreal tangent = ((2.0 * edgeDx + nextDx) * edgeSlope - edgeDx * nextSlope)
            / qMax<qreal>(0.0001, edgeDx + nextDx);

        if (slopeSign(tangent) != slopeSign(edgeSlope)) {
            return 0.0;
        }
        if (slopeSign(edgeSlope) != slopeSign(nextSlope)
            && std::abs(tangent) > std::abs(3.0 * edgeSlope)) {
            return 3.0 * edgeSlope;
        }
        return tangent;
    }

    qreal simpleSlope(int leftIndex, int rightIndex) const
    {
        const Point& left = m_points[leftIndex];
        const Point& right = m_points[rightIndex];
        return (right.y - left.y) / segmentWidth(leftIndex, rightIndex);
    }

    qreal segmentWidth(int leftIndex, int rightIndex) const
    {
        return qMax<qreal>(0.0001, m_points[rightIndex].x - m_points[leftIndex].x);
    }

    int slopeSign(qreal value) const
    {
        if (std::abs(value) <= 0.000001) {
            return 0;
        }
        return value > 0.0 ? 1 : -1;
    }

private:
    QVector<Point> m_points;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    int m_draggingIndex = -1;
    bool m_dragging = false;
    bool m_suppressNextContextMenu = false;
    qreal m_valueMin = 0.0;
    qreal m_valueMax = 1.0;
    AxisDisplaySpec m_horizontalAxisDisplay;
    AxisDisplaySpec m_verticalAxisDisplay { 0.0, 1.0, 100.0, 0, QStringLiteral("%"), {}, true };
};

CurveEditorWidget::CurveEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(core::ThemeManager::instance().scaled(10));

    m_plot = new CurveEditorPlot(this);
    m_plot->setMinimumHeight(core::ThemeManager::instance().scaled(180));
    m_plot->pointsChanged = [this]() { onPlotPointsChanged(); };
    m_plot->editingFinished = [this]() { emit editingFinished(); };
    m_plot->selectionChanged = [this](int index) { onPlotSelectionChanged(index); };

    layout->addWidget(m_plot, 1);
    connect(&core::ThemeManager::instance(), &core::ThemeManager::themeChanged, this,
        &CurveEditorWidget::onThemeChanged);

    setHorizontalAxisDisplay({ 0.0, 1.0, 100.0, 0, QString(), {}, true });
    setVerticalAxisDisplay({ 0.0, 1.0, 100.0, 0, QStringLiteral("%"), {}, true });

    applyStyles();
}

void CurveEditorWidget::setPoints(const QVector<Point>& points)
{
    m_plot->setPoints(points);
}

QVector<CurveEditorWidget::Point> CurveEditorWidget::points() const
{
    return m_plot->points();
}

void CurveEditorWidget::setVerticalRange(qreal maxValue)
{
    m_plot->setValueRange(0.0, maxValue);
    m_plot->setPoints(m_plot->points());
}

void CurveEditorWidget::setVerticalRange(qreal minValue, qreal maxValue)
{
    m_plot->setValueRange(minValue, maxValue);
    m_plot->setPoints(m_plot->points());
}

qreal CurveEditorWidget::verticalRange() const
{
    return m_plot->valueMax();
}

void CurveEditorWidget::setHorizontalAxisDisplay(const AxisDisplaySpec& spec)
{
    m_plot->setHorizontalAxisDisplay(spec);
}

CurveEditorWidget::AxisDisplaySpec CurveEditorWidget::horizontalAxisDisplay() const
{
    return m_plot->horizontalAxisDisplay();
}

void CurveEditorWidget::setVerticalAxisDisplay(const AxisDisplaySpec& spec)
{
    m_plot->setVerticalAxisDisplay(spec);
}

CurveEditorWidget::AxisDisplaySpec CurveEditorWidget::verticalAxisDisplay() const
{
    return m_plot->verticalAxisDisplay();
}

int CurveEditorWidget::selectedPointIndex() const
{
    return m_plot->selectedPointIndex();
}

void CurveEditorWidget::setSelectedPointIndex(int index)
{
    m_plot->setSelectedPointIndex(index);
}

QSize CurveEditorWidget::minimumSizeHint() const
{
    auto& theme = core::ThemeManager::instance();
    return QSize(theme.scaled(320), theme.scaled(236));
}

QSize CurveEditorWidget::sizeHint() const
{
    auto& theme = core::ThemeManager::instance();
    return QSize(theme.scaled(380), theme.scaled(268));
}

void CurveEditorWidget::onPlotPointsChanged()
{
    emit pointsChanged();
}

void CurveEditorWidget::onPlotSelectionChanged(int index)
{
    Q_UNUSED(index);
    emit selectedPointChanged(index);
}

void CurveEditorWidget::onThemeChanged()
{
    if (m_plot) {
        m_plot->setMinimumHeight(core::ThemeManager::instance().scaled(180));
        m_plot->update();
    }
    applyStyles();
}

void CurveEditorWidget::applyStyles()
{
    auto& theme = core::ThemeManager::instance();

    if (auto* layout = qobject_cast<QVBoxLayout*>(this->layout())) {
        layout->setSpacing(theme.scaled(10));
    }
}

} // namespace ruwa::ui::widgets
