// SPDX-License-Identifier: MPL-2.0

// CommandInputWidget.cpp
#include "CommandInputWidget.h"
#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/ShortcutKeycapRenderer.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_MIN_WIDTH = 140;
const int BASE_HEIGHT = 36;
const int BASE_MIN_WIDTH_COMPACT = 100;
const int BASE_HEIGHT_COMPACT = 28;
const int BASE_OUTER_RADIUS = 9;
const int BASE_OUTER_PADDING_H = 8;
const int BASE_HOVER_PADDING_V = 5;

} // namespace

CommandInputWidget::CommandInputWidget(QWidget* parent, SizeVariant sizeVariant)
    : BaseStyledWidget("PanelButton", parent)
    , m_sizeVariant(sizeVariant)
{
    style().background.enabled = false;
    style().border.enabled = false;
    style().hover.enabled = false;
    style().hoverGlow.enabled = false;
    style().press.enabled = false;
    style().animations.hoverDuration = 180;
    style().animations.hoverEasingIn = QEasingCurve::OutCubic;
    style().animations.hoverEasingOut = QEasingCurve::InOutCubic;
    setMouseTracking(true);

    updateSizes();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateSizes();
        update();
    });

    connect(&ruwa::core::ShortcutManager::instance(), &ruwa::core::ShortcutManager::shortcutChanged,
        this, [this](const QString& cmdId, const QKeySequence&) {
            if (cmdId == m_commandId) {
                updateDisplayedShortcut();
            }
        });
}

CommandInputWidget::~CommandInputWidget()
{
    stopRecording();
}

void CommandInputWidget::updateSizes()
{
    auto& theme = ThemeManager::instance();
    // Width is driven by sizeHint()/minimumSizeHint() so the widget grows to
    // fit 3+ keycaps instead of clipping; only the height is fixed here.
    setFixedHeight(
        theme.scaled(m_sizeVariant == SizeVariant::Compact ? BASE_HEIGHT_COMPACT : BASE_HEIGHT));
    updateGeometry();
}

void CommandInputWidget::setCommandId(const QString& commandId)
{
    if (m_commandId == commandId) {
        updateDisplayedShortcut();
        return;
    }
    m_commandId = commandId;
    updateDisplayedShortcut();
}

void CommandInputWidget::setKeySequence(const QKeySequence& seq)
{
    if (m_keySequence == seq)
        return;
    m_keySequence = seq;
    updateGeometry();
    update();
}

void CommandInputWidget::updateDisplayedShortcut()
{
    if (m_commandId.isEmpty()) {
        m_keySequence = QKeySequence();
    } else {
        m_keySequence = ruwa::core::ShortcutManager::instance().shortcutFor(m_commandId);
    }
    updateGeometry();
    update();
}

qreal CommandInputWidget::naturalContentWidth() const
{
    const auto sizeVariant = m_sizeVariant == SizeVariant::Compact
        ? ShortcutKeycapRenderer::SizeVariant::Compact
        : ShortcutKeycapRenderer::SizeVariant::Regular;
    if (m_recording) {
        return ShortcutKeycapRenderer::labelSize(tr("Press shortcut..."), sizeVariant).width();
    }
    if (!ShortcutKeycapRenderer::isRenderable(m_keySequence)) {
        return ShortcutKeycapRenderer::labelSize(tr("Click to assign"), sizeVariant).width();
    }
    return ShortcutKeycapRenderer::contentSize(m_keySequence, sizeVariant).width();
}

QSize CommandInputWidget::sizeHint() const
{
    auto& theme = ThemeManager::instance();
    const int height
        = theme.scaled(m_sizeVariant == SizeVariant::Compact ? BASE_HEIGHT_COMPACT : BASE_HEIGHT);
    const int minWidth = theme.scaled(
        m_sizeVariant == SizeVariant::Compact ? BASE_MIN_WIDTH_COMPACT : BASE_MIN_WIDTH);
    const int outerPadding = theme.scaled(BASE_OUTER_PADDING_H);
    const int natural = qRound(naturalContentWidth()) + outerPadding * 2;
    return QSize(qMax(minWidth, natural), height);
}

QSize CommandInputWidget::minimumSizeHint() const
{
    return sizeHint();
}

QRectF CommandInputWidget::keyGroupRect(const QRectF& rect) const
{
    auto& theme = ThemeManager::instance();
    const int outerPadding = theme.scaled(BASE_OUTER_PADDING_H);
    const QRectF contentRect = rect.adjusted(outerPadding, 0, -outerPadding, 0);
    const QSizeF contentSize(qMin(naturalContentWidth(), contentRect.width()),
        m_sizeVariant == SizeVariant::Compact ? theme.scaled(21) : theme.scaled(28));
    return QRectF(contentRect.right() - contentSize.width(),
        rect.center().y() - contentSize.height() / 2.0, contentSize.width(), contentSize.height());
}

void CommandInputWidget::startRecording()
{
    if (m_recording)
        return;

    m_recording = true;
    qApp->installEventFilter(this);

    emit recordingStarted();
    updateGeometry();
    update();
}

void CommandInputWidget::stopRecording()
{
    if (!m_recording)
        return;

    m_recording = false;
    qApp->removeEventFilter(this);

    emit recordingStopped();
    updateGeometry();
    update();
}

bool CommandInputWidget::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        QWidget* target = QApplication::widgetAt(me->globalPosition().toPoint());
        const bool outsideWidget = !target || (target != this && !isAncestorOf(target));
        const bool outsideKeyGroup
            = target == this && !hitButton(mapFromGlobal(me->globalPosition().toPoint()));
        const bool clickOutside = outsideWidget || outsideKeyGroup;
        if (clickOutside) {
            stopRecording();
        }
        return false;
    }

    if ((event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)
        && m_recording) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const int key = ke->key();

        // Escape cancels recording
        if (key == Qt::Key_Escape) {
            if (event->type() == QEvent::ShortcutOverride) {
                ke->accept();
                return true;
            }
            stopRecording();
            return true;
        }

        // Ignore modifier-only presses (wait for actual key)
        if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
            || key == Qt::Key_Meta) {
            return false;
        }

        // Intercept ShortcutOverride so global shortcuts (Ctrl+S, etc.)
        // don't consume the event before our KeyPress handler sees it.
        if (event->type() == QEvent::ShortcutOverride) {
            ke->accept();
            return true;
        }

        // Store the key by its POSITION on the US layout, never by the character the
        // active layout puts there: with a Cyrillic layout Qt reports Key_Ф where the
        // US layout has F, and a shortcut recorded that way could not be triggered
        // from a Latin layout. ShortcutManager::commandForKeyEvent() resolves incoming
        // events through the same table, so both ends agree on what a physical key is.
        int recordedKey
            = ruwa::core::ShortcutManager::qtKeyFromNativeVirtualKey(ke->nativeVirtualKey());
        if (recordedKey == 0) {
            // Keys outside that table are fine as long as they carry no layout of their
            // own — arrows, F13+, keypad, and everything else Qt reports above
            // Qt::Key_Escape, plus the printable ASCII range. A printable non-ASCII key
            // is nothing but the current layout's character, so refuse to store it and
            // keep waiting instead of writing a binding that can never fire.
            recordedKey = key;
            const bool nonLatinCharacterKey = recordedKey > 0x7F && recordedKey < Qt::Key_Escape;
            if (nonLatinCharacterKey || recordedKey == Qt::Key_unknown) {
                return true;
            }
        }

        const QKeySequence seq(recordedKey | static_cast<int>(ke->modifiers()));
        if (!seq.isEmpty()) {
            emit shortcutRecorded(seq);
            stopRecording();
            return true; // Consume so shortcut doesn't execute
        }
        return false;
    }

    return false;
}

void CommandInputWidget::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    QColor textColor = colors.text;
    if (m_recording) {
        textColor = colors.primary;
    } else if (!isEnabled()) {
        textColor = colors.textDisabled();
    }

    const int outerPadding = theme.scaled(BASE_OUTER_PADDING_H);
    const int hoverPaddingV = theme.scaled(BASE_HOVER_PADDING_V);
    const QRectF groupRect
        = keyGroupRect(rect).adjusted(-outerPadding, -hoverPaddingV, outerPadding, hoverPaddingV);
    const bool pointerOverGroup = hitButton(mapFromGlobal(QCursor::pos()));
    const qreal hover = pointerOverGroup ? qMax(hoverProgress(), isPressed() ? 1.0 : 0.0) : 0.0;
    if (hover > 0.001 || m_recording) {
        QColor hoverBg = m_recording ? colors.primary : QColor(255, 255, 255);
        hoverBg.setAlpha(m_recording ? 36 : qRound((colors.isDark ? 23 : 31) * hover));
        painter.setPen(Qt::NoPen);
        painter.setBrush(hoverBg);
        const qreal radius = theme.scaled(BASE_OUTER_RADIUS);
        painter.drawRoundedRect(groupRect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    const QRectF contentRect = rect.adjusted(outerPadding, 0, -outerPadding, 0);
    const auto sizeVariant = m_sizeVariant == SizeVariant::Compact
        ? ShortcutKeycapRenderer::SizeVariant::Compact
        : ShortcutKeycapRenderer::SizeVariant::Regular;
    if (m_recording) {
        ShortcutKeycapRenderer::paintLabel(painter, contentRect, tr("Press shortcut..."),
            Qt::AlignRight | Qt::AlignVCenter, sizeVariant, textColor, true);
    } else if (!ShortcutKeycapRenderer::isRenderable(m_keySequence)) {
        ShortcutKeycapRenderer::paintLabel(painter, contentRect, tr("Click to assign"),
            Qt::AlignRight | Qt::AlignVCenter, sizeVariant, colors.textMuted);
    } else {
        ShortcutKeycapRenderer::paint(painter, contentRect, m_keySequence,
            Qt::AlignRight | Qt::AlignVCenter, sizeVariant, textColor, hover > 0.0);
    }
}

void CommandInputWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && hitButton(event->pos())) {
        if (m_recording) {
            stopRecording();
        } else {
            startRecording();
        }
        event->accept();
        return;
    }
    BaseStyledWidget::mouseReleaseEvent(event);
}

bool CommandInputWidget::hitButton(const QPoint& pos) const
{
    const auto& theme = ThemeManager::instance();
    return keyGroupRect(QRectF(rect()))
        .adjusted(-theme.scaled(BASE_OUTER_PADDING_H), -theme.scaled(BASE_HOVER_PADDING_V),
            theme.scaled(BASE_OUTER_PADDING_H), theme.scaled(BASE_HOVER_PADDING_V))
        .contains(pos);
}

void CommandInputWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !hitButton(event->pos())) {
        event->ignore();
        return;
    }
    BaseStyledWidget::mousePressEvent(event);
}

void CommandInputWidget::mouseMoveEvent(QMouseEvent* event)
{
    update();
    BaseStyledWidget::mouseMoveEvent(event);
}

void CommandInputWidget::leaveEvent(QEvent* event)
{
    update();
    BaseStyledWidget::leaveEvent(event);
}

} // namespace ruwa::ui::widgets
