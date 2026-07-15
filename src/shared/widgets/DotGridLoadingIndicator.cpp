// SPDX-License-Identifier: MPL-2.0

// DotGridLoadingIndicator.cpp
#include "DotGridLoadingIndicator.h"

#include <QPainter>
#include <QSequentialAnimationGroup>
#include <QPropertyAnimation>
#include <QShowEvent>
#include <QHideEvent>

namespace ruwa::ui::widgets {

namespace {
const int GRID_SIZE = 3;
// Path: top-right [2,0] -> left -> down -> right -> up 1 -> center [1,1]
// (col, row) - col 0=left, 2=right; row 0=top, 2=bottom
const QPointF PATH[] = {
    { 2, 0 }, // top-right
    { 1, 0 }, // left
    { 0, 0 }, // top-left
    { 0, 1 }, // down
    { 0, 2 }, // bottom-left
    { 1, 2 }, // right
    { 2, 2 }, // bottom-right
    { 2, 1 }, // up 1
    { 1, 1 } // center
};
const int PATH_LEN = 9;
const int FADE_IN_MS = 200;
const int ANIMATION_MS = 1000;
const int COOLDOWN_MS = 500;
const qreal TRAIL_LENGTH = 2.5;
const qreal FADE_IN_RANGE = 0.2; // progress -0.2 to 0 = first dot fades in
} // namespace

DotGridLoadingIndicator::DotGridLoadingIndicator(QWidget* parent)
    : QWidget(parent)
    , m_accentColor(251, 248, 239)
    , m_dotColor(120, 120, 120)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    m_fadeInAnimation = new QPropertyAnimation(this, "progress");
    m_fadeInAnimation->setDuration(FADE_IN_MS);
    m_fadeInAnimation->setStartValue(-FADE_IN_RANGE);
    m_fadeInAnimation->setEndValue(0.0);
    m_fadeInAnimation->setEasingCurve(QEasingCurve::Linear);

    m_pathAnimation = new QPropertyAnimation(this, "progress");
    m_pathAnimation->setDuration(ANIMATION_MS);
    m_pathAnimation->setStartValue(0.0);
    m_pathAnimation->setEndValue(1.0);
    m_pathAnimation->setEasingCurve(QEasingCurve::Linear);

    m_fadeAnimation = new QPropertyAnimation(this, "progress");
    m_fadeAnimation->setDuration(COOLDOWN_MS);
    m_fadeAnimation->setStartValue(1.0);
    m_fadeAnimation->setEndValue(1.2);
    m_fadeAnimation->setEasingCurve(QEasingCurve::Linear);

    m_loopGroup = new QSequentialAnimationGroup(this);
    m_loopGroup->addAnimation(m_fadeInAnimation);
    m_loopGroup->addAnimation(m_pathAnimation);
    m_loopGroup->addAnimation(m_fadeAnimation);
    m_loopGroup->setLoopCount(-1);
}

DotGridLoadingIndicator::~DotGridLoadingIndicator()
{
    stop();
}

void DotGridLoadingIndicator::setProgress(qreal p)
{
    if (qFuzzyCompare(m_progress, p))
        return;
    m_progress = p;
    update();
}

void DotGridLoadingIndicator::setAccentColor(const QColor& color)
{
    if (m_accentColor == color)
        return;
    m_accentColor = color;
    update();
}

void DotGridLoadingIndicator::start()
{
    if (m_running)
        return;
    m_running = true;
    m_loopGroup->start();
}

void DotGridLoadingIndicator::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_loopGroup->stop();
    if (isVisible())
        setProgress(-FADE_IN_RANGE);
}

void DotGridLoadingIndicator::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const int w = width();
    const int h = height();
    const qreal cellW = w / qreal(GRID_SIZE);
    const qreal cellH = h / qreal(GRID_SIZE);
    const qreal dotRadius = qMin(cellW, cellH) * 0.25;

    const qreal t = qBound(-FADE_IN_RANGE, m_progress, 1.2);

    auto activityForDot = [t](int dotIndex) -> qreal {
        if (t < 0.0) {
            // Fade-in: first dot (top-right) smoothly becomes accent
            if (dotIndex == 0)
                return qMax(0.0, 1.0 + t / FADE_IN_RANGE); // -0.2->0, 0->1
            return 0.0;
        }
        if (t > 1.0) {
            // Cooldown: center (index 8) fades out
            if (dotIndex == PATH_LEN - 1)
                return qMax(0.0, 1.0 - (t - 1.0) * 5.0);
            return 0.0;
        }
        // Path phase: trail effect
        const qreal currentPos = t * (PATH_LEN - 1);
        const int seg = qMin(int(currentPos), PATH_LEN - 2);
        const qreal segT = currentPos - seg;

        if (dotIndex == seg + 1 && segT > 0.001) {
            return segT; // approaching
        }
        if (dotIndex <= seg) {
            const qreal dist = currentPos - dotIndex;
            return qMax(0.0, 1.0 - dist / TRAIL_LENGTH);
        }
        return 0.0;
    };

    for (int i = 0; i < PATH_LEN; ++i) {
        const int col = int(PATH[i].x());
        const int row = int(PATH[i].y());
        const qreal cx = (col + 0.5) * cellW;
        const qreal cy = (row + 0.5) * cellH;

        const qreal activity = qBound(0.0, activityForDot(i), 1.0);
        const QColor color
            = QColor(int(m_dotColor.red() + (m_accentColor.red() - m_dotColor.red()) * activity),
                int(m_dotColor.green() + (m_accentColor.green() - m_dotColor.green()) * activity),
                int(m_dotColor.blue() + (m_accentColor.blue() - m_dotColor.blue()) * activity));

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(cx, cy), dotRadius, dotRadius);
    }
}

void DotGridLoadingIndicator::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_running) {
        m_loopGroup->start();
    }
}

void DotGridLoadingIndicator::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_loopGroup->pause();
}

} // namespace ruwa::ui::widgets
