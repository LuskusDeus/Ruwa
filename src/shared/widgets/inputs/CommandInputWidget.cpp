// SPDX-License-Identifier: MPL-2.0

// CommandInputWidget.cpp
#include "CommandInputWidget.h"
#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QKeyCombination>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVector>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_MIN_WIDTH = 140;
const int BASE_HEIGHT = 36;
const int BASE_MIN_WIDTH_COMPACT = 100;
const int BASE_HEIGHT_COMPACT = 28;
const int BASE_OUTER_RADIUS = 9;
const int BASE_OUTER_PADDING_H = 8;
const int BASE_KEYCAP_PADDING_H = 10;
const int BASE_KEYCAP_PADDING_H_COMPACT = 8;
const int BASE_KEYCAP_HEIGHT = 28;
const int BASE_KEYCAP_HEIGHT_COMPACT = 23;
const int BASE_KEYCAP_RADIUS = 6;
const int BASE_KEYCAP_DEPTH = 4;
const int BASE_KEYCAP_DEPTH_COMPACT = 3;
const int BASE_PLUS_GAP = 5;
const int BASE_PLUS_FONT_SIZE = 8;
const int BASE_HOVER_PADDING_V = 5;

void drawKeycapFrame(QPainter& painter, const QRectF& rect, qreal radius, qreal bottomDepth,
    const QColor& fill, const QColor& borderTop, const QColor& borderBottom)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawRoundedRect(rect, radius, radius);

    const qreal depth = qBound<qreal>(0.0, bottomDepth, rect.height() * 0.35);
    if (depth > 0.5) {
        QColor bottomFill = ThemeColors::interpolate(fill, borderBottom, 0.55);
        bottomFill.setAlpha(qMax(bottomFill.alpha(), borderBottom.alpha()));

        painter.save();
        painter.setClipRect(QRectF(rect.left(), rect.bottom() - depth, rect.width(), depth + 1.0));
        painter.setBrush(bottomFill);
        painter.drawRoundedRect(rect, radius, radius);
        painter.restore();
    }

    const qreal borderWidth = 1.0;
    const QRectF outerRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    const QRectF innerRect
        = outerRect.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
    const qreal outerRadius = qMax<qreal>(0.0, radius - 0.5);
    const qreal innerRadius = qMax<qreal>(0.0, outerRadius - borderWidth);

    QPainterPath outerPath;
    outerPath.addRoundedRect(outerRect, outerRadius, outerRadius);

    QPainterPath innerPath;
    if (innerRect.width() > 0 && innerRect.height() > 0) {
        innerPath.addRoundedRect(innerRect, innerRadius, innerRadius);
    }

    QLinearGradient gradient(outerRect.topLeft(), outerRect.bottomLeft());
    gradient.setColorAt(0.0, borderTop);
    gradient.setColorAt(1.0, borderBottom);

    painter.setBrush(gradient);
    painter.drawPath(outerPath.subtracted(innerPath));

    if (depth > 0.5) {
        QPainterPath bottomClip;
        bottomClip.addRect(
            QRectF(outerRect.left(), outerRect.bottom() - depth, outerRect.width(), depth + 1.0));

        QColor bottomAccent = borderBottom;
        bottomAccent.setAlpha(qMin(255, qRound(bottomAccent.alpha() * 1.65)));
        QLinearGradient bottomGradient(outerRect.topLeft(), outerRect.bottomLeft());
        bottomGradient.setColorAt(0.0, borderBottom);
        bottomGradient.setColorAt(1.0, bottomAccent);

        painter.setBrush(bottomGradient);
        painter.drawPath(outerPath.intersected(bottomClip));
    }
}
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

QStringList CommandInputWidget::displayedKeyParts() const
{
    if (m_keySequence.isEmpty()) {
        return {};
    }

    const QKeyCombination combination = m_keySequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    const Qt::Key key = combination.key();

    QStringList parts;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        parts << QStringLiteral("Ctrl");
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        parts << QStringLiteral("Shift");
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        parts << QStringLiteral("Alt");
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        parts << QStringLiteral("Meta");
    }

    if (key != Qt::Key_unknown && key != Qt::Key_Control && key != Qt::Key_Shift
        && key != Qt::Key_Alt && key != Qt::Key_Meta) {
        QString keyText = QKeySequence(int(key)).toString(QKeySequence::NativeText).trimmed();
        if (keyText.isEmpty()) {
            keyText = QKeySequence(int(key)).toString(QKeySequence::PortableText).trimmed();
        }
        if (!keyText.isEmpty()) {
            parts << keyText;
        }
    }

    if (!parts.isEmpty()) {
        return parts;
    }

    const QString fallbackText = m_keySequence.toString(QKeySequence::NativeText).trimmed();
    if (fallbackText.isEmpty()) {
        return {};
    }
    return { fallbackText };
}

QStringList CommandInputWidget::currentParts() const
{
    if (m_recording) {
        return { tr("Press shortcut...") };
    }
    QStringList parts = displayedKeyParts();
    if (parts.isEmpty()) {
        parts = { tr("Click to assign") };
    }
    return parts;
}

qreal CommandInputWidget::naturalContentWidth() const
{
    auto& theme = ThemeManager::instance();
    auto& mgr = WidgetStyleManager::instance();

    QFont font = this->font();
    const int fontSize = (m_sizeVariant == SizeVariant::Compact)
        ? mgr.scaledFontSize(8)
        : mgr.scaledFontSize(style().content.baseFontSize);
    font.setPointSize(fontSize);
    font.setBold(m_recording);

    const QStringList parts = currentParts();

    const int keycapPaddingH
        = theme.scaled(m_sizeVariant == SizeVariant::Compact ? BASE_KEYCAP_PADDING_H_COMPACT
                                                             : BASE_KEYCAP_PADDING_H);
    const int plusGap = theme.scaled(BASE_PLUS_GAP);

    QFontMetrics fm(font);
    QFont plusFont = font;
    plusFont.setPointSize(mgr.scaledFontSize(BASE_PLUS_FONT_SIZE));
    plusFont.setBold(false);
    QFontMetrics plusFm(plusFont);
    const int plusWidth = plusFm.horizontalAdvance(QStringLiteral("+"));

    qreal totalWidth = 0.0;
    for (const QString& part : parts) {
        totalWidth += fm.horizontalAdvance(part) + keycapPaddingH * 2;
    }
    if (parts.size() > 1) {
        totalWidth += (parts.size() - 1) * (plusWidth + plusGap * 2);
    }
    return totalWidth;
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
    auto& mgr = WidgetStyleManager::instance();

    QFont font = this->font();
    const int fontSize = (m_sizeVariant == SizeVariant::Compact)
        ? mgr.scaledFontSize(8)
        : mgr.scaledFontSize(style().content.baseFontSize);
    font.setPointSize(fontSize);
    font.setBold(m_recording);

    const QStringList parts = currentParts();

    int keycapPaddingH
        = theme.scaled(m_sizeVariant == SizeVariant::Compact ? BASE_KEYCAP_PADDING_H_COMPACT
                                                             : BASE_KEYCAP_PADDING_H);
    const int keycapHeight = theme.scaled(
        m_sizeVariant == SizeVariant::Compact ? BASE_KEYCAP_HEIGHT_COMPACT : BASE_KEYCAP_HEIGHT);
    const int plusGap = theme.scaled(BASE_PLUS_GAP);
    const int outerPadding = theme.scaled(BASE_OUTER_PADDING_H);
    const QRectF contentRect = rect.adjusted(outerPadding, 0, -outerPadding, 0);

    QFontMetrics fm(font);
    QFont plusFont = font;
    plusFont.setPointSize(mgr.scaledFontSize(BASE_PLUS_FONT_SIZE));
    plusFont.setBold(false);
    QFontMetrics plusFm(plusFont);
    const int plusWidth = plusFm.horizontalAdvance(QStringLiteral("+"));

    qreal totalWidth = 0.0;
    for (const QString& part : parts) {
        totalWidth += fm.horizontalAdvance(part) + keycapPaddingH * 2;
    }
    if (parts.size() > 1) {
        totalWidth += (parts.size() - 1) * (plusWidth + plusGap * 2);
    }

    if (totalWidth > contentRect.width() && totalWidth > 0) {
        const qreal scale = qBound<qreal>(0.55, contentRect.width() / totalWidth, 1.0);
        keycapPaddingH = qMax(theme.scaled(4), qRound(keycapPaddingH * scale));

        font.setPointSizeF(qMax<qreal>(6.0, font.pointSizeF() * scale));
        fm = QFontMetrics(font);

        plusFont.setPointSizeF(qMax<qreal>(6.0, plusFont.pointSizeF() * scale));
        plusFm = QFontMetrics(plusFont);

        totalWidth = 0.0;
        for (const QString& part : parts) {
            totalWidth += fm.horizontalAdvance(part) + keycapPaddingH * 2;
        }
        if (parts.size() > 1) {
            totalWidth += (parts.size() - 1)
                * (plusFm.horizontalAdvance(QStringLiteral("+")) + plusGap * 2);
        }
    }

    qreal x = contentRect.right() - totalWidth;
    if (x < contentRect.left()) {
        x = contentRect.left();
    }

    const qreal y = rect.center().y() - keycapHeight / 2.0;
    return QRectF(x, y, qMin<qreal>(totalWidth, contentRect.width()), keycapHeight);
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

        const QKeySequence seq(key | static_cast<int>(ke->modifiers()));
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
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = theme.colors();

    QColor textColor = colors.text;
    if (m_recording) {
        textColor = colors.primary;
    } else if (!isEnabled()) {
        textColor = colors.textDisabled();
    }

    QFont font = painter.font();
    const int fontSize = (m_sizeVariant == SizeVariant::Compact)
        ? mgr.scaledFontSize(8)
        : mgr.scaledFontSize(style().content.baseFontSize);
    font.setPointSize(fontSize);
    font.setBold(m_recording);
    painter.setFont(font);

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

    QStringList parts;
    if (m_recording) {
        parts = { tr("Press shortcut...") };
    } else {
        parts = displayedKeyParts();
        if (parts.isEmpty()) {
            parts = { tr("Click to assign") };
            textColor = colors.textMuted;
        }
    }

    const int keycapHeight = theme.scaled(
        m_sizeVariant == SizeVariant::Compact ? BASE_KEYCAP_HEIGHT_COMPACT : BASE_KEYCAP_HEIGHT);
    int keycapPaddingH
        = theme.scaled(m_sizeVariant == SizeVariant::Compact ? BASE_KEYCAP_PADDING_H_COMPACT
                                                             : BASE_KEYCAP_PADDING_H);
    const int plusGap = theme.scaled(BASE_PLUS_GAP);
    const qreal keyRadius = theme.scaled(BASE_KEYCAP_RADIUS);
    const qreal keyDepth = theme.scaled(
        m_sizeVariant == SizeVariant::Compact ? BASE_KEYCAP_DEPTH_COMPACT : BASE_KEYCAP_DEPTH);
    const QRectF contentRect = rect.adjusted(outerPadding, 0, -outerPadding, 0);
    const qreal keyY = rect.center().y() - keycapHeight / 2.0;

    QFontMetrics fm(font);
    QFont plusFont = font;
    plusFont.setPointSize(mgr.scaledFontSize(BASE_PLUS_FONT_SIZE));
    plusFont.setBold(false);
    QFontMetrics plusFm(plusFont);
    const QString plusText = QStringLiteral("+");
    const int plusWidth = plusFm.horizontalAdvance(plusText);

    QVector<qreal> widths;
    qreal totalWidth = 0.0;
    for (const QString& part : parts) {
        const qreal w = fm.horizontalAdvance(part) + keycapPaddingH * 2;
        widths.append(w);
        totalWidth += w;
    }
    if (parts.size() > 1) {
        totalWidth += (parts.size() - 1) * (plusWidth + plusGap * 2);
    }

    qreal scale = 1.0;
    if (totalWidth > contentRect.width() && totalWidth > 0) {
        scale = qBound<qreal>(0.55, contentRect.width() / totalWidth, 1.0);
        keycapPaddingH = qMax(theme.scaled(4), qRound(keycapPaddingH * scale));
        QFont scaledFont = font;
        scaledFont.setPointSizeF(qMax<qreal>(6.0, font.pointSizeF() * scale));
        font = scaledFont;
        painter.setFont(font);
        fm = QFontMetrics(font);

        QFont scaledPlusFont = plusFont;
        scaledPlusFont.setPointSizeF(qMax<qreal>(6.0, plusFont.pointSizeF() * scale));
        plusFont = scaledPlusFont;
        plusFm = QFontMetrics(plusFont);
        widths.clear();
        totalWidth = 0.0;
        for (const QString& part : parts) {
            const qreal w = fm.horizontalAdvance(part) + keycapPaddingH * 2;
            widths.append(w);
            totalWidth += w;
        }
        if (parts.size() > 1) {
            totalWidth += (parts.size() - 1) * (plusFm.horizontalAdvance(plusText) + plusGap * 2);
        }
    }

    qreal x = contentRect.right() - totalWidth;
    if (x < contentRect.left()) {
        x = contentRect.left();
    }
    const qreal keyState = m_recording ? 1.0 : hover;
    const QColor keyBg = m_recording
        ? ThemeColors::withAlpha(colors.primary, colors.isDark ? 32 : 44)
        : ThemeColors::interpolate(
              colors.overlayBase(), colors.overlayHover(), 0.75 + 0.25 * keyState);
    QColor keyBorderTop = m_recording ? ThemeColors::withAlpha(colors.primary, 132)
                                      : ThemeColors::interpolate(colors.borderSubtle(),
                                            colors.borderSubtleHover(), 0.25 + 0.55 * keyState);
    QColor keyBorderBottom = keyBorderTop;
    keyBorderBottom.setAlpha(qRound(keyBorderBottom.alpha() * 0.5));
    QColor plusColor = ThemeColors::interpolate(keyBorderBottom, keyBorderTop, 0.7);
    plusColor.setAlpha(qMax(plusColor.alpha(), colors.isDark ? 128 : 112));

    for (int i = 0; i < parts.size(); ++i) {
        const QRectF keyRect(x, keyY, widths[i], keycapHeight);

        drawKeycapFrame(
            painter, keyRect, keyRadius, keyDepth, keyBg, keyBorderTop, keyBorderBottom);

        painter.setPen(textColor);
        painter.setFont(font);
        const QRectF textRect = keyRect.adjusted(0, 0, 0, -keyDepth);
        painter.drawText(textRect.toRect(), Qt::AlignCenter, parts[i]);

        x += widths[i];
        if (i < parts.size() - 1) {
            painter.setFont(plusFont);
            painter.setPen(plusColor);
            const QRectF plusRect(
                x + plusGap, rect.top(), plusFm.horizontalAdvance(plusText), rect.height());
            painter.drawText(plusRect.toRect(), Qt::AlignCenter, plusText);
            x += plusFm.horizontalAdvance(plusText) + plusGap * 2;
        }
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
