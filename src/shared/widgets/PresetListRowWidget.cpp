// SPDX-License-Identifier: MPL-2.0

// PresetListRowWidget.cpp
#include "PresetListRowWidget.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"

#include <QCursor>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
constexpr qreal kPi = 3.14159265358979323846;
constexpr int kContextActionSelect = -1;
constexpr int kContextActionRename = -2;
constexpr int kContextActionDelete = -3;

QColor blend(const QColor& from, const QColor& to, qreal t)
{
    return ruwa::ui::core::ThemeColors::interpolate(from, to, std::clamp(t, 0.0, 1.0));
}

} // namespace

PresetListRowWidget::PresetListRowWidget(const PresetMenuItem& item, QWidget* parent)
    : QWidget(parent)
    , m_item(item)
{
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    setupAnimations();
    updateScaledSize();
    setExtraActions(item.extraActions);
    updateEditorStyle();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &PresetListRowWidget::onThemeChanged);
}

PresetListRowWidget::~PresetListRowWidget() = default;

void PresetListRowWidget::setupAnimations()
{
    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnimation->setDuration(ANIMATION_DURATION);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_selectionAnimation = new QPropertyAnimation(this, "selectionProgress", this);
    m_selectionAnimation->setDuration(ANIMATION_DURATION + 50);
    m_selectionAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_pressAnimation = new QPropertyAnimation(this, "pressProgress", this);
    m_pressAnimation->setDuration(110);
    m_pressAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_renameHoverAnimation = new QPropertyAnimation(this, "renameHoverProgress", this);
    m_renameHoverAnimation->setDuration(140);
    m_renameHoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_deleteHoverAnimation = new QPropertyAnimation(this, "deleteHoverProgress", this);
    m_deleteHoverAnimation->setDuration(140);
    m_deleteHoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void PresetListRowWidget::updateScaledSize()
{
    setFixedHeight(ThemeManager::instance().scaled(BASE_HEIGHT));
}

void PresetListRowWidget::markLayoutDirty()
{
    m_layoutDirty = true;
}

void PresetListRowWidget::ensureActionLayout() const
{
    if (!m_layoutDirty) {
        return;
    }

    m_layoutDirty = false;
    m_layoutRename = {};
    m_layoutDelete = {};
    m_layoutExtras.resize(m_extraActions.size());

    const auto& theme = ThemeManager::instance();
    const int btn = theme.scaled(BASE_ICON_BTN);
    const int gap = theme.scaled(BASE_BTN_GAP);
    const int pad = m_popupChromeStyle ? theme.scaled(4) : horizontalPaddingPx() + theme.scaled(2);
    const int y = (height() - btn) / 2;
    int right = width() - pad;

    auto place = [&](QRect& out) {
        out = QRect(right - btn, y, btn, btn);
        right -= btn + gap;
    };

    if (m_isDeletable) {
        place(m_layoutDelete);
    }
    if (m_isRenamable) {
        place(m_layoutRename);
    }
    for (int i = m_extraActions.size() - 1; i >= 0; --i) {
        place(m_layoutExtras[i]);
    }
}

void PresetListRowWidget::setItem(const PresetMenuItem& item)
{
    m_item = item;
    setExtraActions(item.extraActions);
    markLayoutDirty();
    update();
}

void PresetListRowWidget::setText(const QString& text)
{
    if (m_item.title != text) {
        m_item.title = text;
        update();
    }
}

void PresetListRowWidget::setSubtitle(const QString& text)
{
    if (m_item.subtitle != text) {
        m_item.subtitle = text;
        update();
    }
}

void PresetListRowWidget::setBadgeText(const QString& text)
{
    if (m_item.badgeText != text) {
        m_item.badgeText = text;
        update();
    }
}

void PresetListRowWidget::setBadgeTint(const QColor& tint)
{
    if (m_item.badgeTint != tint) {
        m_item.badgeTint = tint;
        update();
    }
}

void PresetListRowWidget::setPreviewColors(const QVector<QColor>& colors)
{
    if (m_item.previewColors != colors) {
        m_item.previewColors = colors;
        update();
    }
}

void PresetListRowWidget::setPreviewImage(const QImage& image)
{
    if (m_item.previewImage.cacheKey() != image.cacheKey()) {
        m_item.previewImage = image;
        update();
    }
}

void PresetListRowWidget::setPreviewIcon(IconProvider::StandardIcon icon)
{
    if (m_item.previewIcon != icon) {
        m_item.previewIcon = icon;
        update();
    }
}

void PresetListRowWidget::setSelected(bool selected)
{
    if (m_isSelected != selected) {
        m_isSelected = selected;
        m_selectionAnimation->stop();
        m_selectionAnimation->setStartValue(m_selectionProgress);
        m_selectionAnimation->setEndValue(selected ? 1.0 : 0.0);
        m_selectionAnimation->start();
    }
}

void PresetListRowWidget::setActive(bool active)
{
    if (m_isActive != active) {
        m_isActive = active;
        markLayoutDirty();
        syncActionButtons();
        update();
    }
}

void PresetListRowWidget::setDeletable(bool deletable)
{
    if (m_isDeletable != deletable) {
        m_isDeletable = deletable;
        markLayoutDirty();
        syncActionButtons();
        update();
    }
}

void PresetListRowWidget::setRenamable(bool renamable)
{
    if (m_isRenamable != renamable) {
        m_isRenamable = renamable;
        if (!renamable && m_isEditing) {
            finishEditing(false);
        }
        markLayoutDirty();
        syncActionButtons();
        update();
    }
}

void PresetListRowWidget::setExtraActions(QVector<PresetMenuExtraAction> actions)
{
    m_extraActions = std::move(actions);
    m_item.extraActions = m_extraActions;
    m_extraHovered = QVector<bool>(m_extraActions.size(), false);
    m_extraPressed = QVector<bool>(m_extraActions.size(), false);
    markLayoutDirty();
    syncActionButtons();
    update();
}

void PresetListRowWidget::setContextMenuEnabled(bool enabled)
{
    m_contextMenuEnabled = enabled;
}

void PresetListRowWidget::setPopupChromeStyle(bool enabled)
{
    if (m_popupChromeStyle == enabled) {
        return;
    }

    m_popupChromeStyle = enabled;
    markLayoutDirty();
    syncActionButtons();
    update();
}

void PresetListRowWidget::setHoverProgress(qreal progress)
{
    if (!qFuzzyCompare(m_hoverProgress, progress)) {
        m_hoverProgress = progress;
        update();
    }
}

void PresetListRowWidget::setSelectionProgress(qreal progress)
{
    if (!qFuzzyCompare(m_selectionProgress, progress)) {
        m_selectionProgress = progress;
        update();
    }
}

void PresetListRowWidget::setPressProgress(qreal progress)
{
    if (!qFuzzyCompare(m_pressProgress, progress)) {
        m_pressProgress = progress;
        update();
    }
}

void PresetListRowWidget::setRenameHoverProgress(qreal progress)
{
    const qreal clamped = qBound(0.0, progress, 1.0);
    if (!qFuzzyCompare(m_renameHoverProgress, clamped)) {
        m_renameHoverProgress = clamped;
        update();
    }
}

void PresetListRowWidget::setDeleteHoverProgress(qreal progress)
{
    const qreal clamped = qBound(0.0, progress, 1.0);
    if (!qFuzzyCompare(m_deleteHoverProgress, clamped)) {
        m_deleteHoverProgress = clamped;
        update();
    }
}

ContextMenuType PresetListRowWidget::contextMenuType() const
{
    return contextMenuContext().isEmpty() ? ContextMenuType::None : ContextMenuType::SimpleActions;
}

QVariantMap PresetListRowWidget::contextMenuContext() const
{
    if (!m_contextMenuEnabled || m_isEditing) {
        return {};
    }

    QVariantList actions;

    if (!m_isSelected) {
        QVariantMap selectAction;
        selectAction.insert(QStringLiteral("id"), kContextActionSelect);
        selectAction.insert(QStringLiteral("text"), tr("Select"));
        selectAction.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Confirm));
        actions.append(selectAction);
    }

    if (m_isRenamable) {
        QVariantMap renameAction;
        renameAction.insert(QStringLiteral("id"), kContextActionRename);
        renameAction.insert(QStringLiteral("text"), tr("Rename"));
        renameAction.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Edit));
        actions.append(renameAction);
    }

    if (m_isDeletable) {
        if (!actions.isEmpty()) {
            actions.append(QVariantMap { { QStringLiteral("separator"), true } });
        }

        QVariantMap deleteAction;
        deleteAction.insert(QStringLiteral("id"), kContextActionDelete);
        deleteAction.insert(QStringLiteral("text"), tr("Delete"));
        deleteAction.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Trash));
        deleteAction.insert(QStringLiteral("danger"), true);
        actions.append(deleteAction);
    }

    bool extraGroupStarted = false;
    for (const PresetMenuExtraAction& action : m_extraActions) {
        if (action.id == 0 || action.text.trimmed().isEmpty()) {
            continue;
        }

        if (!extraGroupStarted && !actions.isEmpty()) {
            actions.append(QVariantMap { { QStringLiteral("separator"), true } });
        }
        extraGroupStarted = true;

        QVariantMap extraAction;
        extraAction.insert(QStringLiteral("id"), action.id);
        extraAction.insert(QStringLiteral("text"), action.text);
        extraAction.insert(QStringLiteral("danger"), action.dangerHover);
        extraAction.insert(QStringLiteral("checked"), action.checked);
        if (!action.checked) {
            extraAction.insert(QStringLiteral("standardIcon"), static_cast<int>(action.icon));
        }
        actions.append(extraAction);
    }

    if (actions.isEmpty()) {
        return {};
    }

    return { { QStringLiteral("simpleActions"), actions } };
}

void PresetListRowWidget::handleContextMenuAction(int actionId)
{
    switch (actionId) {
    case kContextActionSelect:
        emit clicked();
        break;
    case kContextActionRename:
        startEditing();
        break;
    case kContextActionDelete:
        emit deleteRequested();
        break;
    default:
        emit extraActionTriggered(actionId);
        break;
    }
}

int PresetListRowWidget::horizontalPaddingPx() const
{
    const auto& theme = ThemeManager::instance();
    return m_popupChromeStyle ? theme.scaled(4) : theme.scaled(BASE_PADDING_H);
}

int PresetListRowWidget::contentLeftInset() const
{
    const auto& theme = ThemeManager::instance();
    int inset = horizontalPaddingPx();

    if (!m_item.fillPreviewBackground) {
        inset += previewSizePx().width() + theme.scaled(BASE_PREVIEW_GAP);
    }

    return inset;
}

int PresetListRowWidget::rightReservedWidth() const
{
    ensureActionLayout();
    const auto& theme = ThemeManager::instance();
    const int pad = m_popupChromeStyle ? theme.scaled(4) : horizontalPaddingPx() + theme.scaled(2);
    const int btn = theme.scaled(BASE_ICON_BTN);
    const int gap = theme.scaled(BASE_BTN_GAP);
    const int n = (m_isDeletable ? 1 : 0) + (m_isRenamable ? 1 : 0) + m_extraActions.size();
    int actionsWidth = 0;
    if (n > 0) {
        actionsWidth = pad + n * btn + (n - 1) * gap;
    }

    const bool showBadge = !m_item.badgeText.trimmed().isEmpty();
    int badgeWidth = 0;
    if (showBadge) {
        QFont badgeFont = font();
        badgeFont.setPixelSize(theme.scaled(10));
        const QFontMetrics fm(badgeFont);
        badgeWidth = theme.scaled(BASE_BADGE_HPAD * 2) + fm.horizontalAdvance(m_item.badgeText);
    }

    const int reserved = std::max(actionsWidth, showBadge ? pad + badgeWidth : 0);
    return reserved > 0 ? reserved : pad;
}

void PresetListRowWidget::syncActionButtons()
{
    ensureActionLayout();
    if (!m_isHovered || m_isEditing) {
        clearInlineActionState();
    }
}

int PresetListRowWidget::hitExtraIndex(const QPoint& pos) const
{
    if (!m_isHovered && !m_isEditing) {
        return -1;
    }
    ensureActionLayout();
    for (int i = 0; i < m_layoutExtras.size(); ++i) {
        if (m_layoutExtras[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

bool PresetListRowWidget::hitRenameAction(const QPoint& pos) const
{
    if (!m_isHovered || m_isEditing || !m_isRenamable) {
        return false;
    }

    ensureActionLayout();
    return m_layoutRename.contains(pos);
}

bool PresetListRowWidget::hitDeleteAction(const QPoint& pos) const
{
    if (!m_isHovered || m_isEditing || !m_isDeletable) {
        return false;
    }

    ensureActionLayout();
    return m_layoutDelete.contains(pos);
}

bool PresetListRowWidget::isOverAnyAction(const QPoint& pos) const
{
    return hitRenameAction(pos) || hitDeleteAction(pos) || hitExtraIndex(pos) >= 0;
}

void PresetListRowWidget::updateInlineActionHover(const QPoint& pos)
{
    const bool renameHovered = hitRenameAction(pos);
    const bool deleteHovered = hitDeleteAction(pos);
    const int extraIndex = hitExtraIndex(pos);
    bool changed = (renameHovered != m_renameHovered) || (deleteHovered != m_deleteHovered);

    const auto animateHover = [](QPropertyAnimation* anim, qreal current, bool hovered) {
        if (!anim) {
            return;
        }
        anim->stop();
        anim->setStartValue(current);
        anim->setEndValue(hovered ? 1.0 : 0.0);
        anim->start();
    };

    if (renameHovered != m_renameHovered) {
        animateHover(m_renameHoverAnimation, m_renameHoverProgress, renameHovered);
    }
    if (deleteHovered != m_deleteHovered) {
        animateHover(m_deleteHoverAnimation, m_deleteHoverProgress, deleteHovered);
    }

    m_renameHovered = renameHovered;
    m_deleteHovered = deleteHovered;

    for (int i = 0; i < m_extraHovered.size(); ++i) {
        const bool hovered = (i == extraIndex);
        if (hovered != m_extraHovered[i]) {
            m_extraHovered[i] = hovered;
            changed = true;
        }
    }

    if (changed) {
        update();
    }
}

void PresetListRowWidget::clearInlineActionState()
{
    bool changed = m_renameHovered || m_deleteHovered || m_renamePressed || m_deletePressed;

    if (m_renameHovered && m_renameHoverAnimation) {
        m_renameHoverAnimation->stop();
        m_renameHoverAnimation->setStartValue(m_renameHoverProgress);
        m_renameHoverAnimation->setEndValue(0.0);
        m_renameHoverAnimation->start();
    }
    if (m_deleteHovered && m_deleteHoverAnimation) {
        m_deleteHoverAnimation->stop();
        m_deleteHoverAnimation->setStartValue(m_deleteHoverProgress);
        m_deleteHoverAnimation->setEndValue(0.0);
        m_deleteHoverAnimation->start();
    }

    m_renameHovered = false;
    m_deleteHovered = false;
    m_renamePressed = false;
    m_deletePressed = false;

    for (int i = 0; i < m_extraHovered.size(); ++i) {
        if (m_extraHovered[i]) {
            m_extraHovered[i] = false;
            changed = true;
        }
    }

    for (int i = 0; i < m_extraPressed.size(); ++i) {
        if (m_extraPressed[i]) {
            m_extraPressed[i] = false;
            changed = true;
        }
    }

    if (changed) {
        update();
    }
}

void PresetListRowWidget::resizeEvent(QResizeEvent* event)
{
    markLayoutDirty();
    syncActionButtons();
    if (m_isHovered && !m_isEditing) {
        updateInlineActionHover(mapFromGlobal(QCursor::pos()));
    }
    QWidget::resizeEvent(event);
}

void PresetListRowWidget::startEditing()
{
    if (!m_isRenamable || m_isEditing) {
        return;
    }

    m_isEditing = true;

    if (!m_editor) {
        m_editor = new QLineEdit(this);
        updateEditorStyle();

        connect(m_editor, &QLineEdit::editingFinished, this, [this]() { finishEditing(true); });
    }

    m_editor->setText(m_item.title);
    m_editor->selectAll();

    const auto& theme = ThemeManager::instance();
    int paddingV = theme.scaled(BASE_PADDING_V);
    int left = contentLeftInset();

    m_editor->setGeometry(left, paddingV + theme.scaled(2), width() - left - rightReservedWidth(),
        height() - (paddingV + theme.scaled(2)) * 2);

    m_editor->show();
    m_editor->setFocus();
    syncActionButtons();
}

void PresetListRowWidget::finishEditing(bool accept)
{
    if (!m_isEditing || !m_editor) {
        return;
    }

    m_isEditing = false;

    if (accept) {
        QString t = m_editor->text().trimmed();
        if (!t.isEmpty() && t != m_item.title) {
            m_item.title = t;
            emit renameFinished(t);
        }
    }

    m_editor->hide();
    syncActionButtons();
    update();
}

void PresetListRowWidget::onThemeChanged()
{
    updateScaledSize();
    markLayoutDirty();
    updateEditorStyle();
    syncActionButtons();
    update();
}

void PresetListRowWidget::updateEditorStyle()
{
    if (!m_editor) {
        return;
    }

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const int radius = qMax(4, theme.scaled(BASE_CORNER_RADIUS) - 2);
    m_editor->setStyleSheet(QString("QLineEdit {"
                                    "  background-color: %1;"
                                    "  border: 1px solid %2;"
                                    "  border-radius: %3px;"
                                    "  padding: 0 8px;"
                                    "  color: %4;"
                                    "  selection-background-color: %5;"
                                    "}")
            .arg(colors.surface.name(QColor::HexArgb))
            .arg(colors.borderLight().name(QColor::HexArgb))
            .arg(radius)
            .arg(colors.text.name(QColor::HexArgb))
            .arg(colors.primary.name(QColor::HexArgb)));
}

void PresetListRowWidget::drawStarToggle(
    QPainter& painter, const QRect& btnRect, bool checked, bool hovered, bool pressed) const
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    int radius = theme.scaled(BASE_CORNER_RADIUS) - 2;

    if (hovered || pressed) {
        QColor btnBg = colors.text;
        if (pressed) {
            btnBg = btnBg.darker(110);
        }
        btnBg.setAlphaF(hovered ? 0.15 : 0.25);
        painter.setPen(Qt::NoPen);
        painter.setBrush(btnBg);
        painter.drawRoundedRect(btnRect, qMax(2, radius), qMax(2, radius));
    }

    QColor normalIconColor = colors.textMuted;
    QColor selectedIconColor = colors.text;
    QColor iconColor;
    if (m_selectionProgress > 0.01) {
        iconColor = QColor(normalIconColor.red()
                + (selectedIconColor.red() - normalIconColor.red()) * m_selectionProgress,
            normalIconColor.green()
                + (selectedIconColor.green() - normalIconColor.green()) * m_selectionProgress,
            normalIconColor.blue()
                + (selectedIconColor.blue() - normalIconColor.blue()) * m_selectionProgress);
    } else {
        iconColor = normalIconColor;
    }

    QPointF center = btnRect.center();
    qreal outerRadius = btnRect.width() * 0.4;
    qreal innerRadius = outerRadius * 0.4;

    QPainterPath starPath;
    for (int i = 0; i < 10; ++i) {
        qreal angle = -kPi / 2.0 + (i * kPi / 5.0);
        qreal rad = (i % 2 == 0) ? outerRadius : innerRadius;
        qreal x = center.x() + rad * std::cos(angle);
        qreal y = center.y() + rad * std::sin(angle);
        if (i == 0) {
            starPath.moveTo(x, y);
        } else {
            starPath.lineTo(x, y);
        }
    }
    starPath.closeSubpath();

    if (checked) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(iconColor);
        painter.drawPath(starPath);
    } else {
        painter.setPen(QPen(iconColor, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(starPath);
    }
}

QSize PresetListRowWidget::previewSizePx() const
{
    const int h = ThemeManager::instance().scaled(BASE_PREVIEW_SIZE);
    if (!m_item.previewWide) {
        return QSize(h, h);
    }
    return QSize(qRound(h * 16.0 / 9.0), h);
}

void PresetListRowWidget::drawPreview(
    QPainter& painter, const QRectF& rect, const QColor& borderColor) const
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const int radius = m_item.previewWide ? qMax(5, theme.scaled(8))
                                          : qMax(4, theme.scaled(BASE_CORNER_RADIUS) - 1);

    painter.save();

    if (m_item.fillPreviewBackground) {
        QPainterPath clipPath;
        clipPath.addRoundedRect(rect, radius, radius);
        painter.setClipPath(clipPath);

        if (!m_item.previewImage.isNull()) {
            painter.setOpacity(colors.isDark ? 0.9 : 0.82);
            painter.drawImage(rect.toRect(), m_item.previewImage);
            painter.setOpacity(1.0);
        } else if (!m_item.previewColors.isEmpty()) {
            const qreal bandW = rect.width() / qMax(1, m_item.previewColors.size());
            for (int i = 0; i < m_item.previewColors.size(); ++i) {
                painter.fillRect(
                    QRectF(rect.left() + bandW * i, rect.top(), bandW + 1.0, rect.height()),
                    m_item.previewColors[i]);
            }
        } else {
            const int iconSide = qMax(theme.scaled(24), static_cast<int>(rect.height() * 0.72));
            const QColor iconColor = ThemeColors::withAlpha(
                blend(colors.textMuted, colors.text, 0.18), colors.isDark ? 118 : 88);
            QPixmap pm = IconProvider::instance()
                             .getColoredIcon(m_item.previewIcon, iconColor)
                             .pixmap(iconSide, iconSide);
            if (!pm.isNull()) {
                const int x = static_cast<int>(rect.right()) - theme.scaled(16) - pm.width();
                const int y = static_cast<int>(rect.center().y() - pm.height() / 2.0);
                painter.drawPixmap(x, y, pm);
            }
        }

        QLinearGradient fade(rect.topLeft(), rect.topRight());
        QColor fadeStart = colors.surface;
        QColor fadeMid = colors.surface;
        QColor fadeEnd = colors.surface;
        fadeStart.setAlpha(colors.isDark ? 248 : 236);
        fadeMid.setAlpha(colors.isDark ? 188 : 136);
        fadeEnd.setAlpha(0);
        fade.setColorAt(0.0, fadeStart);
        fade.setColorAt(0.34, fadeStart);
        fade.setColorAt(0.72, fadeMid);
        fade.setColorAt(1.0, fadeEnd);
        painter.fillRect(rect, fade);

        painter.restore();
        return;
    }

    if (!m_item.previewFrameless) {
        painter.setPen(QPen(borderColor, 1));
        painter.setBrush(colors.surfaceHover());
        painter.drawRoundedRect(rect, radius, radius);
    } else if (m_item.previewWide) {
        const QRectF containerRect = rect.adjusted(1.0, 1.0, -1.0, -1.0);
        const qreal containerRadius = qMax<qreal>(1.5, theme.scaled(3));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, colors.isDark ? 18 : 22));
        painter.drawRoundedRect(containerRect, containerRadius, containerRadius);
    }

    const QRectF clipRect = m_item.previewFrameless && m_item.previewWide
        ? rect.adjusted(1.0, 1.0, -1.0, -1.0)
        : rect.adjusted(1, 1, -1, -1);
    const qreal clipRadius = m_item.previewFrameless && m_item.previewWide
        ? qMax<qreal>(1.5, theme.scaled(3))
        : qMax<qreal>(3, radius - 1);

    QPainterPath clipPath;
    clipPath.addRoundedRect(clipRect, clipRadius, clipRadius);
    painter.setClipPath(clipPath);

    if (!m_item.previewImage.isNull()) {
        painter.drawImage(rect.toRect(), m_item.previewImage);
    } else if (!m_item.previewColors.isEmpty()) {
        const QVector<QColor>& palette = m_item.previewColors;
        const int paletteSize = static_cast<int>(palette.size());
        const int cols = palette.size() >= 4 ? 2 : 1;
        const int rows = paletteSize >= 4 ? 2 : std::min(3, paletteSize);
        const qreal cellW = rect.width() / cols;
        const qreal cellH = rect.height() / rows;

        for (int i = 0; i < palette.size(); ++i) {
            const int col = cols == 1 ? 0 : i % cols;
            const int row = cols == 1 ? i : i / cols;
            if (row >= rows) {
                break;
            }
            painter.fillRect(QRectF(rect.left() + col * cellW, rect.top() + row * cellH,
                                 cellW + 0.5, cellH + 0.5),
                palette[i]);
        }
    } else {
        const int iconSide = static_cast<int>(std::min(rect.width(), rect.height()) * 0.52);
        QPixmap pm
            = IconProvider::instance()
                  .getColoredIcon(m_item.previewIcon, blend(colors.textMuted, colors.text, 0.35))
                  .pixmap(iconSide, iconSide);
        if (!pm.isNull()) {
            const QPoint iconPt(static_cast<int>(rect.center().x() - pm.width() / 2.0),
                static_cast<int>(rect.center().y() - pm.height() / 2.0));
            painter.drawPixmap(iconPt, pm);
        }
    }

    painter.setClipping(false);

    if (m_item.previewFrameless && m_item.previewWide) {
        const QRectF borderRect = rect.adjusted(1.5, 1.5, -1.5, -1.5);
        const qreal borderRadius = qMax<qreal>(1.0, theme.scaled(3) - 0.5);
        QColor previewBorder = colors.border;
        previewBorder.setAlpha(colors.isDark ? 96 : 112);
        QPen pen(previewBorder, 1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(borderRect, borderRadius, borderRadius);
    }

    painter.restore();
}

void PresetListRowWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    ensureActionLayout();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    int radius = m_popupChromeStyle ? theme.scaled(13) : theme.scaled(BASE_CORNER_RADIUS);
    int paddingH = horizontalPaddingPx();
    int paddingV = theme.scaled(BASE_PADDING_V);
    const QSize previewSize = previewSizePx();
    const int activeBarW = theme.scaled(BASE_ACTIVE_BAR_WIDTH);

    QRectF itemRect = m_popupChromeStyle ? QRectF(rect()).adjusted(0.0, 1.0, 0.0, -1.0)
                                         : QRectF(rect()).adjusted(2.0, 1.0, -2.0, -1.0);
    const QRectF plateRect = itemRect;

    QColor hoverBg = m_popupChromeStyle ? QColor(255, 255, 255, colors.isDark ? 18 : 70)
                                        : colors.surfaceHover();
    if (!m_popupChromeStyle) {
        hoverBg.setAlpha(colors.isDark ? 210 : 235);
    }

    QColor selectedBg = ThemeColors::withAlpha(colors.primary, colors.isDark ? 44 : 34);
    QColor bgColor = blend(
        Qt::transparent, hoverBg, m_popupChromeStyle ? m_hoverProgress : m_hoverProgress * 0.75);
    bgColor = blend(bgColor, selectedBg, m_selectionProgress);
    bgColor = blend(bgColor, colors.shadow(colors.isDark ? 34 : 22), m_pressProgress * 0.32);

    QColor borderColor
        = blend(colors.borderSubtle(), colors.borderSubtleHover(), m_hoverProgress * 0.85);
    borderColor = blend(borderColor,
        ThemeColors::withAlpha(colors.primary, colors.isDark ? 68 : 52), m_selectionProgress);
    if (m_popupChromeStyle) {
        borderColor = ThemeColors::withAlpha(colors.primary, colors.isDark ? 72 : 58);
    }

    if (bgColor.alpha() > 0 && !m_item.fillPreviewBackground) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(plateRect, radius, radius);
    }

    if (m_item.fillPreviewBackground) {
        drawPreview(painter, plateRect.adjusted(1.0, 1.0, -1.0, -1.0), QColor());
        if (bgColor.alpha() > 0) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(bgColor);
            painter.drawRoundedRect(plateRect, radius, radius);
        }
    }

    const qreal selectedTone = qMax(m_selectionProgress, m_isActive ? 1.0 : 0.0);
    if (!m_popupChromeStyle || selectedTone > 0.001) {
        QColor rowBorder = borderColor;
        if (m_popupChromeStyle) {
            rowBorder.setAlphaF(rowBorder.alphaF() * selectedTone);
        }
        painter.setPen(QPen(rowBorder, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(plateRect, radius, radius);
    }

    if (m_isActive) {
        const QRectF activeRect(plateRect.left() + 1.0, plateRect.center().y() - theme.scaled(10),
            activeBarW, theme.scaled(20));
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.primary);
        painter.drawRoundedRect(activeRect, activeBarW / 2.0, activeBarW / 2.0);
    }

    const int leftMargin = contentLeftInset();
    if (!m_item.fillPreviewBackground) {
        const QRectF previewRect(itemRect.left() + paddingH,
            itemRect.center().y() - previewSize.height() / 2.0, previewSize.width(),
            previewSize.height());
        drawPreview(painter, previewRect,
            blend(borderColor, colors.borderLight(), m_selectionProgress * 0.4));
    }

    const int reservedRight = rightReservedWidth();
    QRect textRect(static_cast<int>(itemRect.left()) + leftMargin,
        static_cast<int>(itemRect.top()) + paddingV,
        static_cast<int>(itemRect.width()) - leftMargin - reservedRight,
        static_cast<int>(itemRect.height()) - paddingV * 2);

    const QColor titleColor = blend(colors.text, colors.text, 1.0);
    const QColor subtitleColor = blend(colors.textMuted, colors.text, m_selectionProgress * 0.22);

    const bool hasActions = m_isDeletable || m_isRenamable || !m_extraActions.isEmpty();
    const bool showActions = hasActions && !m_isEditing && (m_isHovered || m_popupChromeStyle);
    const bool showBadge = !showActions && !m_item.badgeText.trimmed().isEmpty();

    if (!m_isEditing) {
        QFont titleFont = font();
        titleFont.setPixelSize(theme.scaled(13));
        titleFont.setWeight(m_isSelected ? QFont::DemiBold : QFont::Medium);
        painter.setFont(titleFont);
        painter.setPen(titleColor);

        QRect titleRect = textRect;
        if (!m_item.subtitle.trimmed().isEmpty()) {
            titleRect.setHeight(theme.scaled(18));
        }
        const int trailingIconSize = m_item.hasTitleTrailingIcon ? theme.scaled(12) : 0;
        const int trailingGap = m_item.hasTitleTrailingIcon ? theme.scaled(5) : 0;
        const int titleTextWidth = qMax(0, titleRect.width() - trailingIconSize - trailingGap);
        const QString titleText
            = painter.fontMetrics().elidedText(m_item.title, Qt::ElideRight, titleTextWidth);
        painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, titleText);

        if (m_item.hasTitleTrailingIcon && titleTextWidth > 0) {
            const int drawnTextWidth = painter.fontMetrics().horizontalAdvance(titleText);
            const int iconX = titleRect.left()
                + qMin(drawnTextWidth + trailingGap, titleRect.width() - trailingIconSize);
            const QRect iconRect(iconX, titleRect.center().y() - trailingIconSize / 2,
                trailingIconSize, trailingIconSize);
            const QColor iconColor
                = ThemeColors::withAlpha(colors.textMuted, colors.isDark ? 150 : 120);
            const QPixmap pm = IconProvider::instance()
                                   .getColoredIcon(m_item.titleTrailingIcon, iconColor)
                                   .pixmap(trailingIconSize, trailingIconSize);
            if (!pm.isNull()) {
                painter.drawPixmap(iconRect.center() - QPoint(pm.width() / 2, pm.height() / 2), pm);
            }
        }

        if (!m_item.subtitle.trimmed().isEmpty()) {
            QFont subtitleFont = font();
            subtitleFont.setPixelSize(theme.scaled(10));
            painter.setFont(subtitleFont);
            painter.setPen(subtitleColor);
            QRect subtitleRect = textRect;
            subtitleRect.setTop(titleRect.bottom() - theme.scaled(1));
            painter.drawText(subtitleRect, Qt::AlignLeft | Qt::AlignVCenter,
                painter.fontMetrics().elidedText(
                    m_item.subtitle, Qt::ElideRight, subtitleRect.width()));
        }
    }

    if (showBadge) {
        QFont badgeFont = font();
        badgeFont.setPixelSize(theme.scaled(10));
        painter.setFont(badgeFont);
        const QFontMetrics badgeFm(badgeFont);
        const int badgeW
            = theme.scaled(BASE_BADGE_HPAD * 2) + badgeFm.horizontalAdvance(m_item.badgeText);
        const int badgeH = theme.scaled(BASE_BADGE_HEIGHT);
        const QRect badgeRect(static_cast<int>(itemRect.right()) - paddingH - badgeW,
            static_cast<int>(itemRect.center().y()) - badgeH / 2, badgeW, badgeH);

        QColor badgeBg = colors.surfaceHover();
        QColor badgeBorder = colors.borderSubtle();
        QColor badgeTextColor = colors.textMuted;
        if (m_item.badgeTint.isValid()) {
            badgeBg = ThemeColors::withAlpha(m_item.badgeTint, colors.isDark ? 32 : 24);
            badgeBorder = ThemeColors::withAlpha(m_item.badgeTint, colors.isDark ? 62 : 48);
            badgeTextColor = m_item.badgeTint;
        }

        painter.setPen(QPen(badgeBorder, 1));
        painter.setBrush(badgeBg);
        painter.drawRoundedRect(badgeRect, badgeH / 2.0, badgeH / 2.0);
        painter.setPen(badgeTextColor);
        painter.drawText(badgeRect, Qt::AlignCenter, m_item.badgeText);
    }

    auto drawIconAction = [&](const QRect& btnRect, qreal hoverLevel, bool pressed, bool danger,
                              IconProvider::StandardIcon icon) {
        const auto& c = ThemeManager::instance().colors();
        int r = qMax(4, ThemeManager::instance().scaled(BASE_CORNER_RADIUS) - 3);
        const qreal hp = qBound<qreal>(0.0, hoverLevel, 1.0);

        if (hp > 0.001 || pressed) {
            const int bgAlpha = pressed ? (danger ? 46 : 40) : qRound((danger ? 34 : 28) * hp);
            QColor btnBg = danger ? ThemeColors::withAlpha(c.error, bgAlpha)
                                  : ThemeColors::withAlpha(c.overlayColor, bgAlpha);
            if (m_popupChromeStyle) {
                painter.setPen(Qt::NoPen);
            } else {
                QColor btnBorder = danger
                    ? ThemeColors::withAlpha(c.error, pressed ? 110 : qRound(96 * hp))
                    : ThemeColors::withAlpha(
                          c.borderSubtleHover(), qRound(c.borderSubtleHover().alpha() * hp));
                painter.setPen(QPen(btnBorder, 1));
            }
            painter.setBrush(btnBg);
            painter.drawRoundedRect(btnRect, qMax(2, r), qMax(2, r));
        }

        QColor normalIcon = c.textMuted;
        QColor iconColor = blend(normalIcon, c.text, 0.35 + hp * 0.40);
        if (danger && hp > 0.0) {
            iconColor = blend(iconColor, c.error, hp);
        }

        const int iconPx
            = ThemeManager::instance().scaled(PresetListRowWidget::sideActionGlyphBasePx());
        QPixmap pm
            = IconProvider::instance().getColoredIcon(icon, iconColor).pixmap(iconPx, iconPx);
        if (!pm.isNull()) {
            const QPoint pt = btnRect.center() - QPoint(pm.width() / 2, pm.height() / 2);
            painter.drawPixmap(pt, pm);
        }
    };

    if (showActions) {
        for (int i = 0; i < m_extraActions.size(); ++i) {
            const PresetMenuExtraAction& ax = m_extraActions[i];
            const QRect& br = m_layoutExtras[i];
            if (ax.useStarToggle) {
                drawStarToggle(painter, br, ax.checked,
                    i < m_extraHovered.size() && m_extraHovered[i],
                    i < m_extraPressed.size() && m_extraPressed[i]);
            } else {
                const qreal hov = (i < m_extraHovered.size() && m_extraHovered[i]) ? 1.0 : 0.0;
                const bool prs = i < m_extraPressed.size() && m_extraPressed[i];
                drawIconAction(br, hov, prs, ax.dangerHover, ax.icon);
            }
        }

        if (m_isRenamable) {
            drawIconAction(m_layoutRename, m_renameHoverProgress, m_renamePressed, false,
                IconProvider::StandardIcon::Edit);
        }
        if (m_isDeletable) {
            drawIconAction(m_layoutDelete, m_deleteHoverProgress, m_deletePressed, true,
                IconProvider::StandardIcon::Trash);
        }
    }

    if (m_pressProgress > 0.0) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(ThemeColors::withAlpha(
            colors.overlayColor, static_cast<int>(10 + 18 * m_pressProgress)));
        painter.drawRoundedRect(plateRect, radius, radius);
    }
}

void PresetListRowWidget::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);
    m_isHovered = true;
    updateInlineActionHover(mapFromGlobal(QCursor::pos()));
    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(1.0);
    m_hoverAnimation->start();
    syncActionButtons();
}

void PresetListRowWidget::leaveEvent(QEvent* event)
{
    if (rect().contains(mapFromGlobal(QCursor::pos()))) {
        event->ignore();
        return;
    }

    m_isHovered = false;
    clearInlineActionState();
    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(0.0);
    m_hoverAnimation->start();
    syncActionButtons();
}

void PresetListRowWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const int ei = hitExtraIndex(event->pos());
        if (ei >= 0 && ei < m_extraPressed.size()) {
            m_extraPressed[ei] = true;
        } else if (hitRenameAction(event->pos())) {
            m_renamePressed = true;
        } else if (hitDeleteAction(event->pos())) {
            m_deletePressed = true;
        } else {
            m_isPressed = true;
            m_pressAnimation->stop();
            m_pressAnimation->setDuration(90);
            m_pressAnimation->setEasingCurve(QEasingCurve::OutCubic);
            m_pressAnimation->setStartValue(m_pressProgress);
            m_pressAnimation->setEndValue(1.0);
            m_pressAnimation->start();
        }
        update();
    }
    QWidget::mousePressEvent(event);
}

void PresetListRowWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        const int ei = hitExtraIndex(event->pos());
        if (ei >= 0 && ei < m_extraPressed.size() && m_extraPressed[ei]) {
            emit extraActionTriggered(m_extraActions[ei].id);
        } else if (m_renamePressed && hitRenameAction(event->pos())) {
            startEditing();
        } else if (m_deletePressed && hitDeleteAction(event->pos())) {
            emit deleteRequested();
        } else if (m_isPressed && rect().contains(event->pos()) && !isOverAnyAction(event->pos())) {
            emit clicked();
        }
        m_isPressed = false;
        m_renamePressed = false;
        m_deletePressed = false;
        m_pressAnimation->stop();
        m_pressAnimation->setDuration(150);
        m_pressAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_pressAnimation->setStartValue(m_pressProgress);
        m_pressAnimation->setEndValue(0.0);
        m_pressAnimation->start();
        for (int i = 0; i < m_extraPressed.size(); ++i) {
            m_extraPressed[i] = false;
        }
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void PresetListRowWidget::mouseMoveEvent(QMouseEvent* event)
{
    updateInlineActionHover(event->pos());
    QWidget::mouseMoveEvent(event);
}

void PresetListRowWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isRenamable && !isOverAnyAction(event->pos())) {
        startEditing();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

} // namespace ruwa::ui::widgets
