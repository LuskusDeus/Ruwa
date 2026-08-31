// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/ui/CanvasFillProgressPopup.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/widgets/DotGridLoadingIndicator.h"

#include <QCoreApplication>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTimer>

#include <algorithm>

namespace ruwa::ui::workspace {

namespace {
// The classic fill has no live preview; the wait popup only earns its
// appearance when the fill takes noticeably longer than an instant. This
// delay is UI policy (plan 7.14.5) — it used to live inside the renderer.
constexpr qint64 kClassicFillWaitPopupDelayMs = 2000;
constexpr int kFillProgressPopupMargin = 8;
constexpr int kFillProgressPopupOffsetY = 18;
} // namespace

CanvasFillProgressPopup::CanvasFillProgressPopup(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 8);
    layout->setSpacing(8);

    m_indicator = new ruwa::ui::widgets::DotGridLoadingIndicator(this);
    m_indicator->setFixedSize(16, 16);
    layout->addWidget(m_indicator, 0, Qt::AlignVCenter);

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_label->setMinimumWidth(400);
    m_label->setMaximumWidth(400);
    layout->addWidget(m_label, 1);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_opacityAnim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_posAnim = new QPropertyAnimation(this, "pos", this);
    m_posAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_waitDelayTimer = new QTimer(this);
    m_waitDelayTimer->setSingleShot(true);
    connect(m_waitDelayTimer, &QTimer::timeout, this, [this]() {
        if (!m_waitPopupVisible) {
            return;
        }
        const QPoint anchor = anchorPointForFillOrigin();
        showProcessingAt(
            anchor, QCoreApplication::translate("OpenGLCanvasWidget", "please wait"), 120);
        updateAnchor();
    });

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            updateTheme();
            updateGeometry();
            update();
        });

    updateTheme();
    hide();
}

void CanvasFillProgressPopup::setDocumentToViewport(DocumentToViewport mapper)
{
    m_documentToViewport = std::move(mapper);
}

void CanvasFillProgressPopup::presentFillActivity(const CanvasFillActivityState& state)
{
    const bool wasWaiting
        = m_lastActivity.waitingForFinalResult && m_lastActivity.phase != CanvasFillPhase::Idle;
    const bool isWaiting = state.waitingForFinalResult && state.phase != CanvasFillPhase::Idle;
    const bool originChanged = state.origin != m_lastActivity.origin;
    m_lastActivity = state;

    if (isWaiting && !wasWaiting) {
        // A new classic fill is waiting for its final result: arm the wait
        // delay. The popup appears only if the fill is still waiting when it
        // fires.
        m_waitPopupVisible = true;
        m_waitDelayTimer->start(static_cast<int>(kClassicFillWaitPopupDelayMs));
    } else if (!isWaiting && (wasWaiting || m_waitPopupVisible)) {
        m_waitPopupVisible = false;
        m_waitDelayTimer->stop();
        hideImmediate();
    } else if (isWaiting && originChanged && isProcessingVisible()) {
        updateAnchor();
    }
}

void CanvasFillProgressPopup::updateAnchor()
{
    if (!m_waitPopupVisible || !isProcessingVisible() || !parentWidget()) {
        return;
    }
    const QPoint anchor = anchorPointForFillOrigin();
    const QSize popupSize = size();
    int x = anchor.x() - popupSize.width() / 2;
    int y = anchor.y() - popupSize.height() - kFillProgressPopupOffsetY;
    x = qBound(kFillProgressPopupMargin, x,
        qMax(kFillProgressPopupMargin,
            parentWidget()->width() - popupSize.width() - kFillProgressPopupMargin));
    y = qBound(kFillProgressPopupMargin, y,
        qMax(kFillProgressPopupMargin,
            parentWidget()->height() - popupSize.height() - kFillProgressPopupMargin));

    if (m_posAnim->state() == QAbstractAnimation::Running) {
        m_posAnim->setEndValue(QPoint(x, y));
    } else if (pos() != QPoint(x, y)) {
        move(x, y);
    }
}

QPoint CanvasFillProgressPopup::anchorPointForFillOrigin() const
{
    if (m_lastActivity.origin && m_documentToViewport) {
        const QPointF viewportPos = m_documentToViewport(*m_lastActivity.origin);
        return QPoint(static_cast<int>(std::round(viewportPos.x())),
            static_cast<int>(std::round(viewportPos.y())));
    }
    return QPoint(kFillProgressPopupMargin, kFillProgressPopupMargin);
}

QPoint CanvasFillProgressPopup::popupTopLeftForAnchor(
    const QPoint& anchorPoint, const QSize& popupSize) const
{
    int x = anchorPoint.x() - popupSize.width() / 2;
    int y = anchorPoint.y() - popupSize.height() - kFillProgressPopupOffsetY;

    if (auto* parent = parentWidget()) {
        x = qBound(kFillProgressPopupMargin, x,
            qMax(kFillProgressPopupMargin,
                parent->width() - popupSize.width() - kFillProgressPopupMargin));
        y = qBound(kFillProgressPopupMargin, y,
            qMax(kFillProgressPopupMargin,
                parent->height() - popupSize.height() - kFillProgressPopupMargin));
    }

    return QPoint(x, y);
}

void CanvasFillProgressPopup::showProcessingAt(
    const QPoint& anchorPoint, const QString& text, int textWidth)
{
    ++m_transitionToken;
    m_processingState = true;
    m_processingTextWidth = std::max(1, textWidth);
    m_label->setText(text);
    m_indicator->show();
    m_indicator->start();
    applyStateSizing();
    updateTheme();
    if (layout()) {
        layout()->activate();
    }
    const QSize targetSize = sizeHint();
    resize(targetSize);
    startShow(popupTopLeftForAnchor(anchorPoint, targetSize));
}

bool CanvasFillProgressPopup::isProcessingVisible() const
{
    return isVisible() && !m_isHiding && m_processingState;
}

void CanvasFillProgressPopup::startShow(const QPoint& topLeft)
{
    const QPoint startPos = topLeft + QPoint(0, 10);

    m_isHiding = false;
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    if (pos() != startPos) {
        move(startPos);
    }
    show();
    raise();

    m_opacityAnim->stop();
    m_opacityAnim->setDuration(ruwa::ui::core::anim::duration(120));
    m_opacityAnim->setStartValue(m_opacityEffect->opacity());
    m_opacityAnim->setEndValue(1.0);

    m_posAnim->stop();
    m_posAnim->setDuration(ruwa::ui::core::anim::duration(120));
    m_posAnim->setStartValue(startPos);
    m_posAnim->setEndValue(topLeft);

    ruwa::ui::core::anim::start(m_opacityAnim);
    ruwa::ui::core::anim::start(m_posAnim);
}

void CanvasFillProgressPopup::startHide()
{
    if (!isVisible() || m_isHiding) {
        return;
    }

    m_isHiding = true;
    m_indicator->stop();

    const QPoint currentPos = pos();

    m_opacityAnim->stop();
    m_opacityAnim->setDuration(ruwa::ui::core::anim::duration(180));
    m_opacityAnim->setStartValue(m_opacityEffect->opacity());
    m_opacityAnim->setEndValue(0.0);

    m_posAnim->stop();
    m_posAnim->setDuration(ruwa::ui::core::anim::duration(180));
    m_posAnim->setStartValue(currentPos);
    m_posAnim->setEndValue(currentPos - QPoint(0, 10));

    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_isHiding) {
            return;
        }
        m_processingState = false;
        m_isHiding = false;
        m_indicator->hide();
        hide();
    });

    // The opacity animation owns the completion (it hides the popup), so
    // start it last: with animations disabled it finishes inside the call.
    ruwa::ui::core::anim::start(m_posAnim);
    ruwa::ui::core::anim::start(m_opacityAnim);
}

void CanvasFillProgressPopup::hideImmediate()
{
    ++m_transitionToken;
    m_processingState = false;
    m_isHiding = false;
    m_indicator->stop();
    m_indicator->hide();
    m_opacityAnim->stop();
    m_posAnim->stop();
    m_opacityEffect->setOpacity(0.0);
    hide();
}

void CanvasFillProgressPopup::updateTheme()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    m_label->setFont(theme.font(ruwa::ui::core::ThemeFontRole::Body, QFont::Medium));
    m_label->setStyleSheet(
        QString("QLabel { background: transparent; color: %1; }").arg(colors.text.name()));

    const int indicatorSize = theme.scaled(m_processingState ? 22 : 16);
    m_indicator->setFixedSize(indicatorSize, indicatorSize);
    m_indicator->setAccentColor(colors.primary);

    const int textWidth = theme.scaled(m_processingState ? m_processingTextWidth : 96);
    m_label->setMinimumWidth(textWidth);
    m_label->setMaximumWidth(textWidth);

    if (auto* boxLayout = qobject_cast<QHBoxLayout*>(this->layout())) {
        const int verticalPadding = m_processingState ? 6 : 8;
        boxLayout->setContentsMargins(theme.scaled(12), theme.scaled(verticalPadding),
            theme.scaled(12), theme.scaled(verticalPadding));
        boxLayout->setSpacing(theme.scaled(8));
    }
}

void CanvasFillProgressPopup::applyStateSizing()
{
    if (m_processingState) {
        m_label->setMinimumWidth(m_processingTextWidth);
        m_label->setMaximumWidth(m_processingTextWidth);
        m_label->setWordWrap(true);
    } else {
        m_label->setMinimumWidth(96);
        m_label->setMaximumWidth(96);
        m_label->setWordWrap(false);
    }
}

} // namespace ruwa::ui::workspace
