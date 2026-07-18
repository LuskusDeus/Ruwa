// SPDX-License-Identifier: MPL-2.0

#include "ColorChannelSlidersWidget.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabletEvent>
#include <QVariantAnimation>
#include <QtMath>

namespace ruwa::ui::widgets {

namespace {

constexpr int kBaseOuterPadding = 3;
constexpr int kBaseRowHeight = 14;
constexpr int kBaseRowGap = 2;
constexpr int kBaseLabelWidth = 18;
constexpr int kBaseValueWidth = 32;
constexpr int kBaseChipInset = 2;
constexpr int kBaseChipGap = 3;
constexpr int kHoverDuration = 150;

} // namespace

ColorChannelSlidersWidget::ColorChannelSlidersWidget(Model model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setTabletTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    for (int i = 0; i < 3; ++i) {
        auto* animation = new QVariantAnimation(this);
        animation->setDuration(kHoverDuration);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QVariantAnimation::valueChanged, this,
            [this, i](const QVariant& value) {
                m_hoverProgress[i] = qBound(0.0, value.toReal(), 1.0);
                update(channelRect(i).toAlignedRect().adjusted(-1, -1, 1, 1));
            });
        m_hoverAnimations[i] = animation;
    }

    ruwa::ui::core::ThemeManager::instance().registerThemeHandler(
        this, [this]() { onThemeChanged(); });
}

void ColorChannelSlidersWidget::setModel(Model model)
{
    if (m_model == model) {
        return;
    }

    const QColor currentColor = color();
    m_model = model;
    setColor(currentColor);
    update();
}

QColor ColorChannelSlidersWidget::color() const
{
    if (m_model == Model::RGB) {
        return QColor(m_values[0], m_values[1], m_values[2], m_alpha);
    }

    return QColor::fromHsv(m_values[0], qRound(m_values[1] * 255.0 / 100.0),
        qRound(m_values[2] * 255.0 / 100.0), m_alpha);
}

void ColorChannelSlidersWidget::setColor(const QColor& color)
{
    if (!color.isValid()) {
        return;
    }

    m_alpha = color.alpha();
    if (m_model == Model::RGB) {
        m_values = { color.red(), color.green(), color.blue() };
    } else {
        const int hue = color.hsvHue();
        if (hue >= 0) {
            m_values[0] = hue;
        }
        m_values[1] = qRound(color.hsvSaturationF() * 100.0);
        m_values[2] = qRound(color.valueF() * 100.0);
    }
    update();
}

QSize ColorChannelSlidersWidget::sizeHint() const
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int height = theme.scaled(kBaseOuterPadding * 2 + kBaseRowHeight * 3
        + kBaseRowGap * 2);
    return QSize(theme.scaled(180), height);
}

QSize ColorChannelSlidersWidget::minimumSizeHint() const
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    return QSize(theme.scaled(112), sizeHint().height());
}

QRectF ColorChannelSlidersWidget::outerRect() const
{
    return QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
}

QRectF ColorChannelSlidersWidget::channelRect(int index) const
{
    if (index < 0 || index >= 3) {
        return QRectF();
    }

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const qreal padding = theme.scaled(kBaseOuterPadding);
    const qreal rowHeight = theme.scaled(kBaseRowHeight);
    const qreal gap = theme.scaled(kBaseRowGap);
    return QRectF(padding, padding + index * (rowHeight + gap),
        qMax<qreal>(0.0, width() - padding * 2.0), rowHeight);
}

QRectF ColorChannelSlidersWidget::valueTrackRect(int index) const
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const QRectF row = channelRect(index);
    const qreal labelWidth = theme.scaled(kBaseLabelWidth);
    const qreal valueWidth = theme.scaled(kBaseValueWidth);
    const qreal chipInset = theme.scaled(kBaseChipInset);
    const qreal chipGap = theme.scaled(kBaseChipGap);
    return row.adjusted(labelWidth + chipInset + chipGap, chipInset,
        -(valueWidth + chipInset + chipGap), -chipInset);
}

int ColorChannelSlidersWidget::channelAt(const QPointF& position) const
{
    for (int i = 0; i < 3; ++i) {
        if (channelRect(i).contains(position)) {
            return i;
        }
    }
    return -1;
}

int ColorChannelSlidersWidget::maximumForChannel(int index) const
{
    if (m_model == Model::RGB) {
        return 255;
    }
    return index == 0 ? 359 : 100;
}

int ColorChannelSlidersWidget::valueFromPosition(int index, const QPointF& position) const
{
    const QRectF track = valueTrackRect(index);
    if (track.width() <= 0.0) {
        return 0;
    }

    const qreal ratio = qBound(0.0, (position.x() - track.left()) / track.width(), 1.0);
    return qRound(ratio * maximumForChannel(index));
}

qreal ColorChannelSlidersWidget::valueRatio(int index) const
{
    const int maximum = maximumForChannel(index);
    return maximum > 0 ? qBound(0.0, m_values[index] / qreal(maximum), 1.0) : 0.0;
}

QString ColorChannelSlidersWidget::channelLabel(int index) const
{
    static const std::array<QString, 3> rgbLabels {
        QStringLiteral("R"), QStringLiteral("G"), QStringLiteral("B")
    };
    static const std::array<QString, 3> hsvLabels {
        QStringLiteral("H"), QStringLiteral("S"), QStringLiteral("V")
    };
    return m_model == Model::RGB ? rgbLabels[index] : hsvLabels[index];
}

QString ColorChannelSlidersWidget::channelValueText(int index) const
{
    if (m_model == Model::HSV && index == 0) {
        return QStringLiteral("%1°").arg(m_values[index]);
    }
    if (m_model == Model::HSV) {
        return QStringLiteral("%1%").arg(m_values[index]);
    }
    return QString::number(m_values[index]);
}

QLinearGradient ColorChannelSlidersWidget::channelGradient(int index, const QRectF& rect) const
{
    QLinearGradient gradient(rect.topLeft(), rect.topRight());

    if (m_model == Model::RGB) {
        QColor start(m_values[0], m_values[1], m_values[2]);
        QColor end = start;
        start.setRgb(index == 0 ? 0 : start.red(), index == 1 ? 0 : start.green(),
            index == 2 ? 0 : start.blue());
        end.setRgb(index == 0 ? 255 : end.red(), index == 1 ? 255 : end.green(),
            index == 2 ? 255 : end.blue());
        gradient.setColorAt(0.0, start);
        gradient.setColorAt(1.0, end);
        return gradient;
    }

    const int hue = m_values[0];
    const int saturation = qRound(m_values[1] * 255.0 / 100.0);
    if (index == 0) {
        constexpr int hueStops = 6;
        for (int stop = 0; stop <= hueStops; ++stop) {
            const qreal position = stop / qreal(hueStops);
            const int stopHue = qMin(359, qRound(position * 359.0));
            gradient.setColorAt(position, QColor::fromHsv(stopHue, 255, 255));
        }
    } else if (index == 1) {
        gradient.setColorAt(0.0, QColor::fromHsv(hue, 0, 255));
        gradient.setColorAt(1.0, QColor::fromHsv(hue, 255, 255));
    } else {
        gradient.setColorAt(0.0, QColor::fromHsv(hue, saturation, 0));
        gradient.setColorAt(1.0, QColor::fromHsv(hue, saturation, 255));
    }
    return gradient;
}

void ColorChannelSlidersWidget::setChannelValue(int index, int value, bool notify)
{
    if (index < 0 || index >= 3) {
        return;
    }

    const int bounded = qBound(0, value, maximumForChannel(index));
    if (m_values[index] == bounded) {
        return;
    }

    m_values[index] = bounded;
    update();
    if (notify) {
        emit colorChanged(color());
    }
}

void ColorChannelSlidersWidget::updateValueFromPosition(
    int index, const QPointF& position)
{
    setChannelValue(index, valueFromPosition(index, position), true);
}

void ColorChannelSlidersWidget::setHoveredChannel(int index)
{
    if (m_hoveredChannel == index) {
        return;
    }

    m_hoveredChannel = index;
    for (int i = 0; i < 3; ++i) {
        animateHover(i, i == index ? 1.0 : 0.0);
    }
}

void ColorChannelSlidersWidget::animateHover(int index, qreal target)
{
    QVariantAnimation* animation = m_hoverAnimations[index];
    if (!animation) {
        return;
    }
    animation->stop();
    animation->setStartValue(m_hoverProgress[index]);
    animation->setEndValue(target);
    animation->start();
}

void ColorChannelSlidersWidget::onThemeChanged()
{
    updateGeometry();
    update();
}

void ColorChannelSlidersWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const QRectF outer = outerRect();
    const qreal outerRadius = theme.scaled(4.0);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.surfaceAlt);
    painter.drawRoundedRect(outer, outerRadius, outerRadius);
    ruwa::ui::painting::drawGradientBorder(
        painter, outer, outerRadius, colors.borderSubtleHover(), colors.borderSubtle());

    QFont textFont = font();
    textFont.setFamily(colors.fonts.uiFont);
    textFont.setPixelSize(theme.scaledFontSize(8));
    textFont.setWeight(QFont::DemiBold);
    painter.setFont(textFont);

    const qreal chipInset = theme.scaled(kBaseChipInset);
    const qreal labelWidth = theme.scaled(kBaseLabelWidth);
    const qreal valueWidth = theme.scaled(kBaseValueWidth);

    for (int i = 0; i < 3; ++i) {
        const QRectF row = channelRect(i);
        const qreal rowRadius = qMax<qreal>(2.0, theme.scaled(3.0));
        QPainterPath rowPath;
        rowPath.addRoundedRect(row, rowRadius, rowRadius);

        painter.save();
        painter.setClipPath(rowPath);
        painter.fillRect(row, channelGradient(i, row));

        QColor shade = colors.shadow(colors.isDark ? 28 : 12);
        painter.fillRect(row, shade);
        if (m_hoverProgress[i] > 0.001) {
            QColor hover = colors.overlayHover();
            hover.setAlphaF(hover.alphaF() * (0.45 + 0.55 * m_hoverProgress[i]));
            painter.fillRect(row, hover);
        }
        painter.restore();

        QColor rowBorder = colors.borderSubtleHover();
        if (m_hoverProgress[i] > 0.001) {
            QColor accent = colors.primary;
            accent.setAlpha(150);
            rowBorder = ruwa::ui::core::ThemeColors::interpolate(
                rowBorder, accent, m_hoverProgress[i]);
        }
        if (hasFocus() && m_focusedChannel == i) {
            rowBorder = colors.primary;
            rowBorder.setAlpha(185);
        }
        painter.setPen(QPen(rowBorder, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(row.adjusted(0.5, 0.5, -0.5, -0.5),
            qMax<qreal>(1.5, rowRadius - 0.5), qMax<qreal>(1.5, rowRadius - 0.5));

        const QRectF labelChip(row.left() + chipInset, row.top() + chipInset, labelWidth,
            row.height() - chipInset * 2);
        const QRectF valueChip(row.right() - chipInset - valueWidth, row.top() + chipInset,
            valueWidth, row.height() - chipInset * 2);
        QColor chipColor = colors.surface;
        chipColor.setAlpha(colors.isDark ? 218 : 225);
        painter.setPen(Qt::NoPen);
        painter.setBrush(chipColor);
        painter.drawRoundedRect(labelChip, theme.scaled(2.0), theme.scaled(2.0));
        painter.drawRoundedRect(valueChip, theme.scaled(2.0), theme.scaled(2.0));

        const QRectF track = valueTrackRect(i);
        const qreal handleX = track.left() + valueRatio(i) * track.width();
        const QRectF handleShadow(
            handleX - theme.scaled(1.5), row.top() + chipInset, theme.scaled(3.0),
            row.height() - chipInset * 2);
        const QRectF handle(handleX - theme.scaled(0.75), row.top() + chipInset,
            theme.scaled(1.5), row.height() - chipInset * 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.shadow(115));
        painter.drawRoundedRect(handleShadow, theme.scaled(1.0), theme.scaled(1.0));
        painter.setBrush(QColor(255, 255, 255, 225));
        painter.drawRoundedRect(handle, theme.scaled(0.75), theme.scaled(0.75));

        painter.setPen(colors.text);
        painter.drawText(labelChip, Qt::AlignCenter, channelLabel(i));
        painter.drawText(valueChip, Qt::AlignCenter, channelValueText(i));
    }
}

void ColorChannelSlidersWidget::tabletEvent(QTabletEvent* event)
{
    switch (event->type()) {
    case QEvent::TabletPress: {
        const int channel = channelAt(event->position());
        if (channel >= 0) {
            m_tabletDragActive = true;
            m_activeChannel = channel;
            m_focusedChannel = channel;
            setFocus(Qt::OtherFocusReason);
            setHoveredChannel(channel);
            updateValueFromPosition(channel, event->position());
            event->accept();
            return;
        }
        break;
    }
    case QEvent::TabletMove:
        if (m_tabletDragActive && m_activeChannel >= 0) {
            updateValueFromPosition(m_activeChannel, event->position());
            event->accept();
            return;
        }
        break;
    case QEvent::TabletRelease:
        if (m_tabletDragActive) {
            if (m_activeChannel >= 0) {
                updateValueFromPosition(m_activeChannel, event->position());
            }
            m_tabletDragActive = false;
            m_activeChannel = -1;
            setHoveredChannel(channelAt(event->position()));
            event->accept();
            return;
        }
        break;
    default:
        break;
    }

    QWidget::tabletEvent(event);
}

void ColorChannelSlidersWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const int channel = channelAt(event->position());
        if (channel >= 0) {
            m_activeChannel = channel;
            m_focusedChannel = channel;
            setFocus(Qt::MouseFocusReason);
            setHoveredChannel(channel);
            updateValueFromPosition(channel, event->position());
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ColorChannelSlidersWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (m_activeChannel >= 0) {
        updateValueFromPosition(m_activeChannel, event->position());
        event->accept();
        return;
    }

    setHoveredChannel(channelAt(event->position()));
    QWidget::mouseMoveEvent(event);
}

void ColorChannelSlidersWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_activeChannel >= 0) {
        updateValueFromPosition(m_activeChannel, event->position());
        m_activeChannel = -1;
        setHoveredChannel(channelAt(event->position()));
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ColorChannelSlidersWidget::leaveEvent(QEvent* event)
{
    if (m_activeChannel < 0 && !m_tabletDragActive) {
        setHoveredChannel(-1);
    }
    QWidget::leaveEvent(event);
}

void ColorChannelSlidersWidget::keyPressEvent(QKeyEvent* event)
{
    const int index = qBound(0, m_focusedChannel, 2);
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down:
        setChannelValue(index, m_values[index] - 1, true);
        event->accept();
        return;
    case Qt::Key_Right:
    case Qt::Key_Up:
        setChannelValue(index, m_values[index] + 1, true);
        event->accept();
        return;
    case Qt::Key_Home:
        setChannelValue(index, 0, true);
        event->accept();
        return;
    case Qt::Key_End:
        setChannelValue(index, maximumForChannel(index), true);
        event->accept();
        return;
    case Qt::Key_PageUp:
        m_focusedChannel = qMax(0, m_focusedChannel - 1);
        update();
        event->accept();
        return;
    case Qt::Key_PageDown:
        m_focusedChannel = qMin(2, m_focusedChannel + 1);
        update();
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void ColorChannelSlidersWidget::focusInEvent(QFocusEvent* event)
{
    update();
    QWidget::focusInEvent(event);
}

void ColorChannelSlidersWidget::focusOutEvent(QFocusEvent* event)
{
    update();
    QWidget::focusOutEvent(event);
}

} // namespace ruwa::ui::widgets
