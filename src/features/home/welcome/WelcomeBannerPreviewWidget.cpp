// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerPreviewWidget.cpp
#include "WelcomeBannerPreviewWidget.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"

#include <QPainter>
#include <QPainterPath>
#include <QVariantList>
#include <QVariantMap>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
const int BASE_PADDING = 4;
const int BASE_IMAGE_RADIUS = 3;

enum BannerPreviewMenuAction : int {
    EditCustomImageCrop = 1,
    DeleteCustomImage = 2,
};
} // namespace

bool WelcomeBannerPreviewWidget::isCustomUserImage() const
{
    return !m_imageKey.startsWith(QLatin1String(":/"));
}

ContextMenuType WelcomeBannerPreviewWidget::contextMenuType() const
{
    return isCustomUserImage() ? ContextMenuType::SimpleActions : ContextMenuType::None;
}

QVariantMap WelcomeBannerPreviewWidget::contextMenuContext() const
{
    QVariantList actions;
    actions.append(QVariantMap {
        { QStringLiteral("id"), EditCustomImageCrop },
        { QStringLiteral("text"), tr("Edit crop") },
        { QStringLiteral("danger"), false },
        { QStringLiteral("standardIcon"),
            static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::Crop) },
    });
    actions.append(QVariantMap {
        { QStringLiteral("id"), DeleteCustomImage },
        { QStringLiteral("text"), tr("Delete") },
        { QStringLiteral("danger"), true },
        { QStringLiteral("standardIcon"),
            static_cast<int>(ruwa::ui::core::IconProvider::StandardIcon::Trash) },
    });
    return { { QStringLiteral("simpleActions"), actions } };
}

void WelcomeBannerPreviewWidget::onSimpleContextAction(int actionId)
{
    if (!isCustomUserImage()) {
        return;
    }
    if (actionId == EditCustomImageCrop) {
        emit customImageEditCropRequested(m_imageKey);
    } else if (actionId == DeleteCustomImage) {
        emit customImageDeleteRequested(m_imageKey);
    }
}

WelcomeBannerPreviewWidget::WelcomeBannerPreviewWidget(const QString& imageKey, QWidget* parent)
    : BaseStyledWidget(QStringLiteral("WelcomeBannerPreview"), parent)
    , m_imageKey(imageKey)
{
    setCursor(Qt::PointingHandCursor);
    reloadPixmap();

    connect(this, &QPushButton::clicked, this, [this]() {
        if (!isActive()) {
            setSelected(true);
        }
        emit imageClicked(m_imageKey);
    });
}

QSize WelcomeBannerPreviewWidget::sizeHint() const
{
    auto& mgr = WidgetStyleManager::instance();
    return QSize(mgr.scaled(style().metrics.baseWidth), mgr.scaled(style().metrics.baseHeight));
}

void WelcomeBannerPreviewWidget::reloadPixmap()
{
    m_pixmap = QPixmap(m_imageKey);
    update();
}

void WelcomeBannerPreviewWidget::setSelected(bool selected)
{
    if (isActive() == selected) {
        return;
    }
    setActive(selected);
}

void WelcomeBannerPreviewWidget::drawContentLayer(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    const int padding = mgr.scaled(BASE_PADDING);
    const int imageRadius = mgr.scaled(BASE_IMAGE_RADIUS);

    QRectF imageRect = rect.adjusted(padding, padding, -padding, -padding);

    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_pixmap.isNull()) {
        QColor contentBg = ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 0.85);
        painter.setPen(Qt::NoPen);
        painter.setBrush(contentBg);
        painter.drawRoundedRect(imageRect, imageRadius, imageRadius);
    } else {
        QPixmap scaled = m_pixmap.scaled(
            imageRect.size().toSize(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);

        painter.save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(imageRect, imageRadius, imageRadius);
        painter.setClipPath(clipPath);

        const qreal x = imageRect.center().x() - scaled.width() / 2.0;
        const qreal y = imageRect.center().y() - scaled.height() / 2.0;
        painter.drawPixmap(QPointF(x, y), scaled);
        painter.restore();
    }

    if (activeProgress() > 0.0) {
        painter.save();
        painter.setOpacity(activeProgress());
        const auto& primary = colors.primary;
        QRectF borderRect = imageRect.adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath borderPath;
        borderPath.addRoundedRect(borderRect, imageRadius - 0.5, imageRadius - 0.5);
        QPen pen(primary, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(borderPath);
        painter.restore();
    }
}

} // namespace ruwa::ui::widgets
