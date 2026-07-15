// SPDX-License-Identifier: MPL-2.0

// PresetItemWidget.cpp
#include "features/settings/shortcuts/PresetItemWidget.h"

#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"

#include <QEnterEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;

namespace {
constexpr int BASE_ROW_HEIGHT = 62;
constexpr int BASE_PADDING_H = 14;
constexpr int BASE_PADDING_V = 10;
constexpr int BASE_TITLE_FONT = 13;
constexpr int BASE_META_FONT = 10;
constexpr int BASE_TITLE_META_GAP = 4;
constexpr int BASE_RADIUS = 10;
constexpr int BASE_ACTION_BTN = 26; ///< circular hit area for inline buttons
constexpr int BASE_ACTION_ICON = 14; ///< glyph size inside the hit area
constexpr int BASE_ACTION_GAP = 4;
constexpr int HOVER_ANIM_MS = 140;
constexpr int SELECTION_ANIM_MS = 220;
} // namespace

PresetItemWidget::PresetItemWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Repaint on theme change (this widget paints theme colours directly).
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() { update(); });
}

PresetItemWidget::~PresetItemWidget() = default;

void PresetItemWidget::setTitle(const QString& title)
{
    if (m_title == title)
        return;
    m_title = title;
    update();
}

void PresetItemWidget::setShortcutCount(int count)
{
    if (m_count == count)
        return;
    m_count = count;
    update();
}

void PresetItemWidget::setKind(Kind kind)
{
    if (m_kind == kind)
        return;
    m_kind = kind;
    update();
}

void PresetItemWidget::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;
    startSelectionAnimation(selected);
}

void PresetItemWidget::setSelectedImmediate(bool selected)
{
    if (m_selectionAnim) {
        m_selectionAnim->stop();
        m_selectionAnim->deleteLater();
        m_selectionAnim = nullptr;
    }
    m_selected = selected;
    m_selectionProgress = selected ? 1.0 : 0.0;
    update();
}

QSize PresetItemWidget::sizeHint() const
{
    const auto& theme = ThemeManager::instance();
    return QSize(0, theme.scaled(BASE_ROW_HEIGHT));
}

QSize PresetItemWidget::minimumSizeHint() const
{
    return sizeHint();
}

int PresetItemWidget::actionsCount() const
{
    return m_kind == Kind::Custom ? 3 : 0;
}

QRect PresetItemWidget::editButtonRect() const
{
    if (actionsCount() == 0)
        return {};
    const auto& theme = ThemeManager::instance();
    const int btn = theme.scaled(BASE_ACTION_BTN);
    const int gap = theme.scaled(BASE_ACTION_GAP);
    const int padH = theme.scaled(BASE_PADDING_H);
    const int xRight = width() - padH;
    // Layout: [edit][gap][export][gap][delete] flushed to the right edge.
    const int xEdit = xRight - btn - gap - btn - gap - btn;
    const int y = (height() - btn) / 2;
    return QRect(xEdit, y, btn, btn);
}

QRect PresetItemWidget::exportButtonRect() const
{
    if (actionsCount() == 0)
        return {};
    const auto& theme = ThemeManager::instance();
    const int btn = theme.scaled(BASE_ACTION_BTN);
    const int gap = theme.scaled(BASE_ACTION_GAP);
    const int padH = theme.scaled(BASE_PADDING_H);
    const int xRight = width() - padH;
    const int xExport = xRight - btn - gap - btn;
    const int y = (height() - btn) / 2;
    return QRect(xExport, y, btn, btn);
}

QRect PresetItemWidget::deleteButtonRect() const
{
    if (actionsCount() == 0)
        return {};
    const auto& theme = ThemeManager::instance();
    const int btn = theme.scaled(BASE_ACTION_BTN);
    const int padH = theme.scaled(BASE_PADDING_H);
    const int xRight = width() - padH;
    const int xDelete = xRight - btn;
    const int y = (height() - btn) / 2;
    return QRect(xDelete, y, btn, btn);
}

void PresetItemWidget::startHoverAnimation(bool entering)
{
    if (m_hoverAnim) {
        m_hoverAnim->stop();
        m_hoverAnim->deleteLater();
        m_hoverAnim = nullptr;
    }
    const qreal target = entering ? 1.0 : 0.0;
    m_hoverAnim = new QVariantAnimation(this);
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(target);
    m_hoverAnim->setDuration(HOVER_ANIM_MS);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_hoverProgress = v.toReal();
        update();
    });
    m_hoverAnim->start();
}

void PresetItemWidget::startButtonHoverAnimation(
    QVariantAnimation*& anim, qreal* progress, bool entering)
{
    if (!progress) {
        return;
    }

    if (anim) {
        anim->stop();
        anim->deleteLater();
        anim = nullptr;
    }
    const qreal target = entering ? 1.0 : 0.0;
    anim = new QVariantAnimation(this);
    anim->setStartValue(*progress);
    anim->setEndValue(target);
    anim->setDuration(HOVER_ANIM_MS);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this, [this, progress](const QVariant& v) {
        *progress = v.toReal();
        update();
    });
    anim->start();
}

void PresetItemWidget::updateButtonHoverStates(const QPoint& pos)
{
    if (m_kind != Kind::Custom || m_editing) {
        if (m_editHoverProgress > 0.0)
            startButtonHoverAnimation(m_editHoverAnim, &m_editHoverProgress, false);
        if (m_exportHoverProgress > 0.0)
            startButtonHoverAnimation(m_exportHoverAnim, &m_exportHoverProgress, false);
        if (m_deleteHoverProgress > 0.0)
            startButtonHoverAnimation(m_deleteHoverAnim, &m_deleteHoverProgress, false);
        return;
    }
    const bool overEdit = editButtonRect().contains(pos);
    const bool overExport = exportButtonRect().contains(pos);
    const bool overDelete = deleteButtonRect().contains(pos);
    if (overEdit != (m_editHoverProgress > 0.5))
        startButtonHoverAnimation(m_editHoverAnim, &m_editHoverProgress, overEdit);
    if (overExport != (m_exportHoverProgress > 0.5))
        startButtonHoverAnimation(m_exportHoverAnim, &m_exportHoverProgress, overExport);
    if (overDelete != (m_deleteHoverProgress > 0.5))
        startButtonHoverAnimation(m_deleteHoverAnim, &m_deleteHoverProgress, overDelete);
}

void PresetItemWidget::startSelectionAnimation(bool selecting)
{
    if (m_selectionAnim) {
        m_selectionAnim->stop();
        m_selectionAnim->deleteLater();
        m_selectionAnim = nullptr;
    }
    const qreal target = selecting ? 1.0 : 0.0;
    m_selectionAnim = new QVariantAnimation(this);
    m_selectionAnim->setStartValue(m_selectionProgress);
    m_selectionAnim->setEndValue(target);
    m_selectionAnim->setDuration(SELECTION_ANIM_MS);
    m_selectionAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_selectionAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_selectionProgress = v.toReal();
        update();
    });
    m_selectionAnim->start();
}

void PresetItemWidget::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    startHoverAnimation(true);
}

void PresetItemWidget::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    startHoverAnimation(false);
    updateButtonHoverStates(QPoint(-1, -1));
    setCursor(Qt::PointingHandCursor);
}

void PresetItemWidget::mouseMoveEvent(QMouseEvent* event)
{
    updateButtonHoverStates(event->pos());
    QWidget::mouseMoveEvent(event);
}

void PresetItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_kind == Kind::Custom) {
            if (editButtonRect().contains(event->pos())) {
                startRenameEditing();
                event->accept();
                return;
            }
            if (exportButtonRect().contains(event->pos())) {
                emit exportRequested();
                event->accept();
                return;
            }
            if (deleteButtonRect().contains(event->pos())) {
                emit deleteRequested();
                event->accept();
                return;
            }
        }
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PresetItemWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_editing) {
        positionEditor();
    }
}

void PresetItemWidget::startRenameEditing()
{
    if (m_kind != Kind::Custom || m_editing)
        return;
    m_editing = true;

    if (!m_editor) {
        m_editor = new QLineEdit(this);
        m_editor->installEventFilter(this);
        connect(m_editor, &QLineEdit::editingFinished, this, [this]() {
            // editingFinished fires on focus-out and Enter; commit on both.
            finishEditing(true);
        });
    }
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    QFont f = m_editor->font();
    f.setPixelSize(theme.scaled(BASE_TITLE_FONT));
    f.setWeight(QFont::DemiBold);
    m_editor->setFont(f);
    m_editor->setStyleSheet(
        QStringLiteral("QLineEdit { color: %1; background: %2; border: 1px solid %3; "
                       "border-radius: 4px; padding: 1px 4px; }")
            .arg(colors.text.name())
            .arg(colors.surfaceAlt.name())
            .arg(colors.borderSubtle().name()));
    m_editor->setText(m_title);
    positionEditor();
    m_editor->show();
    m_editor->setFocus(Qt::MouseFocusReason);
    m_editor->selectAll();
    update();
}

void PresetItemWidget::positionEditor()
{
    if (!m_editor)
        return;
    const auto& theme = ThemeManager::instance();
    const int padH = theme.scaled(BASE_PADDING_H);

    QFont f = m_editor->font();
    QFontMetrics fm(f);
    const int titleH = fm.height();

    QFont metaFont = font();
    metaFont.setPixelSize(theme.scaled(BASE_META_FONT));
    const int metaH = QFontMetrics(metaFont).height();
    const int gap = theme.scaled(BASE_TITLE_META_GAP);
    const int totalH = titleH + gap + metaH;
    const int textTop = (height() - totalH) / 2;

    const int rightInset = padH; // editing hides the action buttons
    const int w = qMax(20, width() - padH - rightInset);
    m_editor->setGeometry(padH - 4, textTop - 2, w + 4, titleH + 4);
}

void PresetItemWidget::finishEditing(bool accept)
{
    if (!m_editing)
        return;
    m_editing = false;
    QString newName;
    if (m_editor) {
        newName = m_editor->text().trimmed();
        m_editor->hide();
        m_editor->clearFocus();
    }
    if (accept && !newName.isEmpty() && newName != m_title) {
        emit renamed(newName);
    }
    update();
}

bool PresetItemWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_editor && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            finishEditing(false);
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void PresetItemWidget::paintEvent(QPaintEvent* /*event*/)
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF itemRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = theme.scaled(BASE_RADIUS);

    // === Always-on fill (matches SettingsPanel style) ===
    QColor bg
        = ThemeColors::interpolate(colors.overlayBase(), colors.overlayHover(), m_hoverProgress);
    bg = ThemeColors::interpolate(bg, colors.primary, m_selectionProgress);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(itemRect, radius, radius);

    // === Vertical-gradient subtle border ===
    {
        const qreal w = 1.0;
        QRectF outerRect = itemRect.adjusted(0.5, 0.5, -0.5, -0.5);
        QRectF innerRect = outerRect.adjusted(w, w, -w, -w);
        QPainterPath outerPath;
        outerPath.addRoundedRect(outerRect, radius - 0.5, radius - 0.5);
        QPainterPath innerPath;
        innerPath.addRoundedRect(
            innerRect, qMax(0.0, radius - 0.5 - w), qMax(0.0, radius - 0.5 - w));
        QPainterPath ring = outerPath.subtracted(innerPath);

        QColor topColor = colors.borderSubtle();
        QColor bottomColor = colors.borderSubtle();
        bottomColor.setAlpha(bottomColor.alpha() / 2);
        topColor = ThemeColors::interpolate(topColor, colors.primary, m_selectionProgress);
        bottomColor = ThemeColors::interpolate(bottomColor, colors.primary, m_selectionProgress);

        QLinearGradient gradient(outerRect.topLeft(), outerRect.bottomLeft());
        gradient.setColorAt(0.0, topColor);
        gradient.setColorAt(1.0, bottomColor);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawPath(ring);
    }

    // === Text layout ===
    const int paddingH = theme.scaled(BASE_PADDING_H);

    QFont titleFont = font();
    titleFont.setPixelSize(theme.scaled(BASE_TITLE_FONT));
    titleFont.setWeight(QFont::DemiBold);
    QFontMetrics titleFm(titleFont);

    QFont metaFont = font();
    metaFont.setPixelSize(theme.scaled(BASE_META_FONT));
    QFontMetrics metaFm(metaFont);

    const int titleH = titleFm.height();
    const int metaH = metaFm.height();
    const int gap = theme.scaled(BASE_TITLE_META_GAP);
    const int totalH = titleH + gap + metaH;
    const int textTop = (height() - totalH) / 2;

    // Text colours flip to "on primary" when selected (primary-filled background).
    const QColor titleColor
        = ThemeColors::interpolate(colors.text, colors.textOnPrimary(), m_selectionProgress);
    const QColor mutedColor = ThemeColors::interpolate(
        colors.textMuted, ThemeColors::withAlpha(colors.textOnPrimary(), 180), m_selectionProgress);

    // Inline actions are always visible for custom presets — reserve space accordingly.
    const bool showActions = (m_kind == Kind::Custom) && !m_editing;
    const int actionsReserved = (m_kind == Kind::Custom)
        ? (deleteButtonRect().right() - editButtonRect().left() + theme.scaled(BASE_ACTION_GAP))
        : 0;

    const int lockSize = (m_kind == Kind::BuiltIn) ? qRound(titleH * 0.82) : 0;
    const int lockGap = (m_kind == Kind::BuiltIn) ? theme.scaled(6) : 0;

    const int textAreaLeft = paddingH;
    const int textAreaRight = width() - paddingH - actionsReserved;
    const int textAreaWidth = qMax(0, textAreaRight - textAreaLeft);

    // Title (skipped while inline editor is shown — it covers the title slot).
    if (!m_editing) {
        const int titleAvailW = qMax(0, textAreaWidth - lockSize - lockGap);
        const QString elidedTitle = titleFm.elidedText(m_title, Qt::ElideRight, titleAvailW);
        painter.setFont(titleFont);
        painter.setPen(titleColor);
        const QRect titleRect(textAreaLeft, textTop, titleAvailW, titleH);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

        if (m_kind == Kind::BuiltIn) {
            const QIcon lock = IconProvider::instance().getIcon(IconProvider::StandardIcon::Lock);
            if (!lock.isNull()) {
                const int titleAdvance = titleFm.horizontalAdvance(elidedTitle);
                const int lockX = textAreaLeft + titleAdvance + lockGap;
                const int lockY = textTop + (titleH - lockSize) / 2;
                QPixmap pm = lock.pixmap(QSize(lockSize, lockSize) * devicePixelRatioF());
                pm.setDevicePixelRatio(devicePixelRatioF());
                if (!pm.isNull()) {
                    painter.drawPixmap(QRect(lockX, lockY, lockSize, lockSize),
                        ruwa::ui::painting::tintedPixmap(pm, mutedColor));
                }
            }
        }
    }

    // Meta: "N shortcuts" (normal) · "Built-in/Custom preset" (muted)
    painter.setFont(metaFont);
    const QString countWord = (m_count == 1) ? tr("shortcut") : tr("shortcuts");
    const QString countText = QStringLiteral("%1 %2").arg(m_count).arg(countWord);
    const QString sep = QStringLiteral(" · ");
    const QString kindText
        = (m_kind == Kind::BuiltIn) ? tr("Built-in preset") : tr("Custom preset");

    const int countW = metaFm.horizontalAdvance(countText);
    const int sepW = metaFm.horizontalAdvance(sep);
    const int kindW = metaFm.horizontalAdvance(kindText);
    const int metaY = textTop + titleH + gap;

    int x = textAreaLeft;
    painter.setPen(titleColor);
    painter.drawText(QRect(x, metaY, countW, metaH), Qt::AlignLeft | Qt::AlignVCenter, countText);
    x += countW;

    painter.setPen(mutedColor);
    painter.drawText(QRect(x, metaY, sepW, metaH), Qt::AlignLeft | Qt::AlignVCenter, sep);
    x += sepW;

    const int remaining = qMax(0, (textAreaLeft + textAreaWidth) - x);
    painter.drawText(QRect(x, metaY, qMin(kindW, remaining), metaH),
        Qt::AlignLeft | Qt::AlignVCenter, metaFm.elidedText(kindText, Qt::ElideRight, remaining));

    // === Inline action buttons (always visible for custom presets) ===
    if (showActions) {
        const QRect editRect = editButtonRect();
        const QRect exportRect = exportButtonRect();
        const QRect deleteRect = deleteButtonRect();
        const int iconPx = theme.scaled(BASE_ACTION_ICON);

        auto drawAction = [&](const QRect& btnRect, IconProvider::StandardIcon icon, qreal hover,
                              bool danger) {
            // Subtle rounded-square plate that fades in with per-button hover.
            // When the card is selected (inverted = light background), use a dark
            // overlay; otherwise use the theme's normal overlay color.
            if (hover > 0.001) {
                QColor plateColor = ThemeColors::interpolate(
                    colors.overlayColor, colors.background, m_selectionProgress);
                plateColor.setAlpha(qBound(0, qRound(36 * hover), 80));
                painter.setPen(Qt::NoPen);
                painter.setBrush(plateColor);
                const qreal r = theme.scaled(6);
                painter.drawRoundedRect(btnRect, r, r);
            }

            // Icon tint: muted at idle → titleColor (or error for trash) on hover.
            // Mirrors PresetListRowWidget: blend(muted, text, 0.35 + 0.40*hp).
            QColor tint = ThemeColors::interpolate(mutedColor, titleColor, 0.35 + 0.40 * hover);
            if (danger && hover > 0.0) {
                tint = ThemeColors::interpolate(tint, colors.error, hover);
            }

            const QIcon ic = IconProvider::instance().getIcon(icon);
            if (ic.isNull())
                return;
            QPixmap pm = ic.pixmap(QSize(iconPx, iconPx) * devicePixelRatioF());
            pm.setDevicePixelRatio(devicePixelRatioF());
            if (pm.isNull())
                return;
            const QPoint pt = btnRect.center() - QPoint(iconPx / 2, iconPx / 2);
            painter.drawPixmap(pt, ruwa::ui::painting::tintedPixmap(pm, tint));
        };

        drawAction(editRect, IconProvider::StandardIcon::Edit, m_editHoverProgress, false);
        drawAction(exportRect, IconProvider::StandardIcon::Export, m_exportHoverProgress, false);
        drawAction(deleteRect, IconProvider::StandardIcon::Trash, m_deleteHoverProgress, true);
    }
}

} // namespace ruwa::ui::widgets
