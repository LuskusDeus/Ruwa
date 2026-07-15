// SPDX-License-Identifier: MPL-2.0

// PresetImportDropZone.cpp
#include "features/settings/shortcuts/PresetImportDropZone.h"

#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QUrl>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;

namespace {
constexpr int BASE_HEIGHT = 56;
constexpr int BASE_RADIUS = 10;
constexpr int BASE_FONT = 12;
constexpr int BASE_ICON = 16;
constexpr int BASE_ICON_GAP = 8;
constexpr int ANIM_MS = 140;
} // namespace

PresetImportDropZone::PresetImportDropZone(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_Hover, true);
    setCursor(Qt::PointingHandCursor);
    setAcceptDrops(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

PresetImportDropZone::~PresetImportDropZone() = default;

QSize PresetImportDropZone::sizeHint() const
{
    return QSize(0, ThemeManager::instance().scaled(BASE_HEIGHT));
}

QSize PresetImportDropZone::minimumSizeHint() const
{
    return sizeHint();
}

void PresetImportDropZone::startHoverAnimation(bool entering)
{
    if (m_hoverAnim) {
        m_hoverAnim->stop();
        m_hoverAnim->deleteLater();
        m_hoverAnim = nullptr;
    }
    m_hoverAnim = new QVariantAnimation(this);
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(entering ? 1.0 : 0.0);
    m_hoverAnim->setDuration(ANIM_MS);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_hoverProgress = v.toReal();
        update();
    });
    m_hoverAnim->start();
}

void PresetImportDropZone::startDragAnimation(bool active)
{
    if (m_dragAnim) {
        m_dragAnim->stop();
        m_dragAnim->deleteLater();
        m_dragAnim = nullptr;
    }
    m_dragAnim = new QVariantAnimation(this);
    m_dragAnim->setStartValue(m_dragProgress);
    m_dragAnim->setEndValue(active ? 1.0 : 0.0);
    m_dragAnim->setDuration(ANIM_MS);
    m_dragAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_dragAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_dragProgress = v.toReal();
        update();
    });
    m_dragAnim->start();
}

void PresetImportDropZone::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    startHoverAnimation(true);
}

void PresetImportDropZone::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    startHoverAnimation(false);
}

void PresetImportDropZone::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

QString PresetImportDropZone::extractDroppedJsonPath(const QDropEvent* event)
{
    const QMimeData* mime = event->mimeData();
    if (!mime || !mime->hasUrls())
        return {};
    const QList<QUrl> urls = mime->urls();
    if (urls.size() != 1)
        return {};
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    if (!info.isFile() || info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) != 0) {
        return {};
    }
    return path;
}

void PresetImportDropZone::dragEnterEvent(QDragEnterEvent* event)
{
    if (!extractDroppedJsonPath(event).isEmpty()) {
        event->acceptProposedAction();
        startDragAnimation(true);
        return;
    }
    event->ignore();
}

void PresetImportDropZone::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    startDragAnimation(false);
}

void PresetImportDropZone::dropEvent(QDropEvent* event)
{
    const QString path = extractDroppedJsonPath(event);
    startDragAnimation(false);
    if (path.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    emit fileDropped(path);
}

void PresetImportDropZone::paintEvent(QPaintEvent* /*event*/)
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = theme.scaled(BASE_RADIUS);
    const qreal active = qBound(0.0, m_hoverProgress * 0.5 + m_dragProgress, 1.0);

    // Background (subtle fill that brightens with hover/drag).
    QColor bg = ThemeColors::interpolate(colors.overlayBase(), colors.overlayHover(), active);
    QRectF itemRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawRoundedRect(itemRect, radius, radius);

    // Dashed border — accent color when something is being dragged in.
    QColor borderColor = ThemeColors::interpolate(colors.borderSubtle(), colors.text, active);
    if (m_dragProgress > 0.0) {
        borderColor = ThemeColors::interpolate(borderColor, colors.text, m_dragProgress);
    }
    QPen pen(borderColor);
    pen.setWidthF(qMax(1.0, theme.scaled(1) * 1.0));
    QVector<qreal> dashes;
    dashes << 4.0 << 3.0;
    pen.setDashPattern(dashes);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(itemRect, radius, radius);

    // Label + icon.
    const QString label = tr("Import preset…");
    const QString hint = tr("or drop a .json file here");

    QFont titleFont = font();
    titleFont.setPixelSize(theme.scaled(BASE_FONT));
    titleFont.setWeight(QFont::DemiBold);
    QFontMetrics titleFm(titleFont);

    QFont hintFont = font();
    hintFont.setPixelSize(theme.scaled(BASE_FONT - 2));
    QFontMetrics hintFm(hintFont);

    const int iconPx = theme.scaled(BASE_ICON);
    const int gap = theme.scaled(BASE_ICON_GAP);

    const int titleW = titleFm.horizontalAdvance(label);
    const int totalRowW = iconPx + gap + titleW;
    const int titleH = titleFm.height();
    const int hintH = hintFm.height();
    const int blockH = titleH + theme.scaled(2) + hintH;

    const int blockTop = (height() - blockH) / 2;
    const int rowLeft = (width() - totalRowW) / 2;

    const QColor titleColor = ThemeColors::interpolate(colors.text, colors.text, active);
    const QColor hintColor = colors.textMuted;

    // Icon — tinted with current text color.
    const QIcon ic = IconProvider::instance().getIcon(IconProvider::StandardIcon::Import);
    if (!ic.isNull()) {
        QPixmap pm = ic.pixmap(QSize(iconPx, iconPx) * devicePixelRatioF());
        pm.setDevicePixelRatio(devicePixelRatioF());
        if (!pm.isNull()) {
            const int iconY = blockTop + (titleH - iconPx) / 2;
            painter.drawPixmap(QRect(rowLeft, iconY, iconPx, iconPx),
                ruwa::ui::painting::tintedPixmap(pm, titleColor));
        }
    }

    painter.setFont(titleFont);
    painter.setPen(titleColor);
    painter.drawText(QRect(rowLeft + iconPx + gap, blockTop, titleW, titleH),
        Qt::AlignLeft | Qt::AlignVCenter, label);

    painter.setFont(hintFont);
    painter.setPen(hintColor);
    const int hintW = hintFm.horizontalAdvance(hint);
    painter.drawText(
        QRect((width() - hintW) / 2, blockTop + titleH + theme.scaled(2), hintW, hintH),
        Qt::AlignCenter, hint);
}

} // namespace ruwa::ui::widgets
