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

using Curve = CurveEditorWidget::Curve;
using CurvePoint = CurveEditorWidget::CurvePoint;

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

CurvePoint makePoint(qreal x, qreal y, qreal smoothness)
{
    CurvePoint point;
    point.x = static_cast<float>(x);
    point.y = static_cast<float>(y);
    point.smoothness = static_cast<float>(smoothness);
    return point;
}

Curve defaultCurve(qreal minValue, qreal maxValue)
{
    const qreal span = qMax<qreal>(0.0001, maxValue - minValue);
    Curve curve;
    curve.points = {
        makePoint(0.0, minValue + span * 0.08, 0.35),
        makePoint(0.24, minValue + span * 0.22, 0.72),
        makePoint(0.68, minValue + span * 0.82, 0.72),
        makePoint(1.0, minValue + span * 0.94, 0.35),
    };
    return curve;
}

qreal clampToRange(qreal value, qreal minValue, qreal maxValue)
{
    return qBound(minValue, value, maxValue);
}

Curve sanitizeCurve(Curve curve, qreal minValue, qreal maxValue)
{
    if (curve.points.size() < 2) {
        curve = defaultCurve(minValue, maxValue);
    }

    curve.sortByX();

    for (CurvePoint& point : curve.points) {
        point.x = static_cast<float>(clampToRange(point.x, 0.0, 1.0));
        point.y = static_cast<float>(clampToRange(point.y, minValue, maxValue));
        point.smoothness = static_cast<float>(clampToRange(point.smoothness, 0.0, 1.0));
    }

    curve.points.front().x = 0.0f;
    curve.points.back().x = 1.0f;

    for (std::size_t i = 1; i + 1 < curve.points.size(); ++i) {
        const float minX = curve.points[i - 1].x + 0.001f;
        const float maxX = curve.points[i + 1].x - 0.001f;
        curve.points[i].x = qBound(minX, curve.points[i].x, maxX);
    }

    return curve;
}

} // namespace

class CurveEditorPlot final : public QWidget {
public:
    using AxisDisplaySpec = CurveEditorWidget::AxisDisplaySpec;

    explicit CurveEditorPlot(QWidget* parent = nullptr)
        : QWidget(parent)
        , m_curve(defaultCurve(0.0, 1.0))
        , m_selectedIndex(1)
    {
        setFocusPolicy(Qt::StrongFocus);
        setMouseTracking(true);
        setProperty("ruwaContextMenuSystemBypass", true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    Curve curve() const { return m_curve; }

    void setCurve(const Curve& curve)
    {
        m_curve = sanitizeCurve(curve, m_valueMin, m_valueMax);
        if (m_selectedIndex >= 0) {
            m_selectedIndex = qBound(0, m_selectedIndex, pointCount() - 1);
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
        m_curve = sanitizeCurve(m_curve, m_valueMin, m_valueMax);
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
        const int bounded = (index >= 0 && index < pointCount()) ? index : -1;
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

        for (int i = 0; i < pointCount(); ++i) {
            const QPointF pointPos = pointToPixel(pointAt(i), plot);
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
            && m_selectedIndex > 0 && m_selectedIndex < pointCount() - 1) {
            if (removePointAt(m_selectedIndex)) {
                event->accept();
                return;
            }
        }

        QWidget::keyPressEvent(event);
    }

private:
    int pointCount() const { return static_cast<int>(m_curve.points.size()); }

    const CurvePoint& pointAt(int index) const
    {
        return m_curve.points[static_cast<std::size_t>(index)];
    }

    CurvePoint& pointAt(int index) { return m_curve.points[static_cast<std::size_t>(index)]; }

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

    QPointF valueToPixel(qreal x, qreal y, const QRectF& plot) const
    {
        return QPointF(plot.left() + x * plot.width(),
            plot.bottom()
                - ((y - m_valueMin) / qMax<qreal>(0.0001, m_valueMax - m_valueMin))
                    * plot.height());
    }

    QPointF pointToPixel(const CurvePoint& point, const QRectF& plot) const
    {
        return valueToPixel(point.x, point.y, plot);
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

        QFont axisFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);
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
        for (int i = pointCount() - 1; i >= 0; --i) {
            if (QLineF(position, pointToPixel(pointAt(i), plot)).length() <= radius) {
                return i;
            }
        }
        return -1;
    }

    bool removePointAt(int index)
    {
        if (index <= 0 || index >= pointCount() - 1) {
            return false;
        }

        m_curve.points.erase(m_curve.points.begin() + index);
        m_hoveredIndex = -1;
        m_dragging = false;
        m_draggingIndex = -1;
        setSelectedPointIndex(qMin(index, pointCount() - 1));
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
        const QPointF normalized = pixelToNormalized(position);
        CurvePoint point = makePoint(normalized.x(), normalized.y(), 0.65);

        int insertIndex = 1;
        while (insertIndex < pointCount() && pointAt(insertIndex).x < point.x) {
            ++insertIndex;
        }

        const qreal minX = pointAt(insertIndex - 1).x + kMinPointSpacing;
        const qreal maxX = pointAt(insertIndex).x - kMinPointSpacing;
        if (maxX <= minX) {
            return;
        }

        point.x = static_cast<float>(qBound(minX, static_cast<qreal>(point.x), maxX));
        m_curve.points.insert(m_curve.points.begin() + insertIndex, point);
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
        if (index < 0 || index >= pointCount()) {
            return;
        }

        const QPointF normalized = pixelToNormalized(position);
        CurvePoint& point = pointAt(index);
        point.y = static_cast<float>(normalized.y());

        if (index == 0) {
            point.x = 0.0f;
        } else if (index == pointCount() - 1) {
            point.x = 1.0f;
        } else {
            const qreal minX = pointAt(index - 1).x + kMinPointSpacing;
            const qreal maxX = pointAt(index + 1).x - kMinPointSpacing;
            point.x = static_cast<float>(
                (maxX > minX) ? qBound(minX, normalized.x(), maxX) : (minX + maxX) * 0.5);
        }

        update();
        if (notify && pointsChanged) {
            pointsChanged();
        }
    }

    QVector<QPointF> sampledPolyline(const QRectF& plot) const
    {
        QVector<QPointF> samples;
        if (m_curve.points.empty()) {
            return samples;
        }

        const int stepsPerSegment = qMax(18, width() / 28);
        samples.reserve((pointCount() - 1) * stepsPerSegment + 1);
        for (int i = 0; i < pointCount() - 1; ++i) {
            const CurvePoint& p0 = pointAt(i);
            const CurvePoint& p1 = pointAt(i + 1);
            for (int step = 0; step < stepsPerSegment; ++step) {
                const qreal t = static_cast<qreal>(step) / static_cast<qreal>(stepsPerSegment);
                const qreal x = p0.x + (p1.x - p0.x) * t;
                const qreal y = clampToRange(
                    m_curve.evaluateSegment(static_cast<std::size_t>(i), static_cast<float>(t)),
                    m_valueMin, m_valueMax);
                samples.append(valueToPixel(x, y, plot));
            }
        }
        samples.append(pointToPixel(pointAt(pointCount() - 1), plot));
        return samples;
    }

private:
    Curve m_curve;
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

void CurveEditorWidget::setCurve(const Curve& curve)
{
    m_plot->setCurve(curve);
}

CurveEditorWidget::Curve CurveEditorWidget::curve() const
{
    return m_plot->curve();
}

void CurveEditorWidget::setVerticalRange(qreal maxValue)
{
    m_plot->setValueRange(0.0, maxValue);
    m_plot->setCurve(m_plot->curve());
}

void CurveEditorWidget::setVerticalRange(qreal minValue, qreal maxValue)
{
    m_plot->setValueRange(minValue, maxValue);
    m_plot->setCurve(m_plot->curve());
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
