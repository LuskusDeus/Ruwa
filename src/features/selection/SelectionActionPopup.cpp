// SPDX-License-Identifier: MPL-2.0

#include "SelectionActionPopup.h"

#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/style/PaintingUtils.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QPointer>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QTabletEvent>
#include <QPushButton>
#include <cmath>

namespace ruwa::ui::widgets {

namespace {
constexpr int kCornerRadius = 8;
constexpr int kSectionHeight = 40;
constexpr int kSectionPadding = 6;
constexpr int kSectionSpacing = 8;
constexpr int kIconSize = 16;
constexpr int kButtonSize = 28;

class PopupActionButton final : public BaseAnimatedButton {
public:
    explicit PopupActionButton(
        bool swatchButton, QWidget* opacityProvider, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_opacityProvider(opacityProvider)
        , m_isSwatchButton(swatchButton)
    {
        setFixedSize(kButtonSize, kButtonSize);
        setCheckable(false);
        setFocusPolicy(Qt::NoFocus);
        setIconSize(QSize(kIconSize, kIconSize));
        setProperty("popup_swatch_color", QColor(255, 255, 255, 255));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_opacityProvider) {
            p.setOpacity(qBound(0.0, m_opacityProvider->property("popupOpacity").toReal(), 1.0));
        }
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);

        const QColor hoverOverlay = colors.overlay(0.08);
        const QColor pressedOverlay = colors.overlay(0.12);
        const qreal hover = hoverProgress();
        const qreal radius = 4.0;

        if (m_isSwatchButton) {
            QColor swatch = property("popup_swatch_color").value<QColor>();
            if (!swatch.isValid()) {
                swatch = QColor(255, 255, 255, 255);
            }
            p.setPen(QPen(colors.borderSubtle(), 1.0));
            p.setBrush(swatch);
            p.drawRoundedRect(r, radius, radius);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::transparent);
            p.drawRoundedRect(r, radius, radius);
        }

        if (hover > 0.001) {
            QColor hoverColor = hoverOverlay;
            hoverColor.setAlphaF(hoverColor.alphaF() * hover);
            p.setPen(Qt::NoPen);
            p.setBrush(hoverColor);
            p.drawRoundedRect(r, radius, radius);
        }
        if (isPressed()) {
            p.setPen(Qt::NoPen);
            p.setBrush(pressedOverlay);
            p.drawRoundedRect(r, radius, radius);
        }

        if (!icon().isNull()) {
            const QSize drawSize = iconSize().isValid() ? iconSize() : QSize(kIconSize, kIconSize);
            const QPixmap pm = icon().pixmap(drawSize);
            const QPoint pos((width() - pm.width()) / 2, (height() - pm.height()) / 2);
            p.drawPixmap(pos, pm);
        }
    }

private:
    QPointer<QWidget> m_opacityProvider;
    bool m_isSwatchButton = false;
};

class PopupSeparator final : public QWidget {
public:
    explicit PopupSeparator(QWidget* opacityProvider, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_opacityProvider(opacityProvider)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(1, 18);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        if (m_opacityProvider) {
            p.setOpacity(qBound(0.0, m_opacityProvider->property("popupOpacity").toReal(), 1.0));
        }
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        p.fillRect(rect(), colors.borderSubtle());
    }

private:
    QPointer<QWidget> m_opacityProvider;
};
} // namespace

SelectionActionPopup::SelectionActionPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);
    setTabletTracking(true);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(kSectionSpacing);

    m_firstSection = createSection(this);
    m_secondSection = createSection(this);
    rootLayout->addWidget(m_firstSection);
    rootLayout->addWidget(m_secondSection);

    auto* firstLayout = qobject_cast<QHBoxLayout*>(m_firstSection->layout());
    auto* secondLayout = qobject_cast<QHBoxLayout*>(m_secondSection->layout());

    m_swatchButton = new PopupActionButton(true, this, m_firstSection);
    m_swatchButton->setToolTip(tr("Selection fill color"));
    firstLayout->addWidget(m_swatchButton);

    m_fillButton = new PopupActionButton(false, this, m_firstSection);
    m_fillButton->setToolTip(tr("Fill selected area"));
    firstLayout->addWidget(m_fillButton);

    m_transformButton = new PopupActionButton(false, this, m_secondSection);
    m_transformButton->setToolTip(tr("Transform selected area"));
    secondLayout->addWidget(m_transformButton);

    m_deleteButton = new PopupActionButton(false, this, m_secondSection);
    m_deleteButton->setToolTip(tr("Delete selected area"));
    secondLayout->addWidget(m_deleteButton);

    m_flipVerticalButton = new PopupActionButton(false, this, m_secondSection);
    m_flipVerticalButton->setToolTip(tr("Flip vertically"));
    secondLayout->addWidget(m_flipVerticalButton);

    m_flipHorizontalButton = new PopupActionButton(false, this, m_secondSection);
    m_flipHorizontalButton->setToolTip(tr("Flip horizontally"));
    secondLayout->addWidget(m_flipHorizontalButton);

    m_closeSeparator = new PopupSeparator(this, m_secondSection);
    secondLayout->addWidget(m_closeSeparator);

    m_closeButton = new PopupActionButton(false, this, m_secondSection);
    m_closeButton->setToolTip(tr("Hide selection actions"));
    secondLayout->addWidget(m_closeButton);

    connect(m_swatchButton, &QPushButton::clicked, this,
        [this]() { emit fillColorClicked(m_swatchButton); });
    connect(m_fillButton, &QPushButton::clicked, this, &SelectionActionPopup::fillRequested);
    connect(
        m_transformButton, &QPushButton::clicked, this, &SelectionActionPopup::transformRequested);
    connect(m_deleteButton, &QPushButton::clicked, this, &SelectionActionPopup::deleteRequested);
    connect(m_flipVerticalButton, &QPushButton::clicked, this,
        &SelectionActionPopup::flipVerticalRequested);
    connect(m_flipHorizontalButton, &QPushButton::clicked, this,
        &SelectionActionPopup::flipHorizontalRequested);
    connect(m_closeButton, &QPushButton::clicked, this, &SelectionActionPopup::dismissRequested);

    m_opacityAnim = new QPropertyAnimation(this, "popupOpacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_slideAnim = new QPropertyAnimation(this, "slideOffset", this);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_anchorXAnim = new QPropertyAnimation(this, "anchorX", this);
    m_anchorXAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_anchorYAnim = new QPropertyAnimation(this, "anchorY", this);
    m_anchorYAnim->setEasingCurve(QEasingCurve::OutCubic);

    updateSwatchStyle();
    updateIcons();
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            updateSwatchStyle();
            updateIcons();
            update();
        });
    hide();
}

QWidget* SelectionActionPopup::createSection(QWidget* parent) const
{
    auto* section = new QWidget(parent);
    section->setAttribute(Qt::WA_TranslucentBackground);
    section->setFixedHeight(kSectionHeight);
    auto* layout = new QHBoxLayout(section);
    layout->setContentsMargins(kSectionPadding, kSectionPadding, kSectionPadding, kSectionPadding);
    layout->setSpacing(4);
    return section;
}

void SelectionActionPopup::setFillColor(const QColor& color)
{
    if (!color.isValid() || color == m_fillColor) {
        return;
    }
    m_fillColor = color;
    updateSwatchStyle();
}

void SelectionActionPopup::showAt(const QPoint& topLeft, bool animateShow, bool animateMove)
{
    QPoint targetTopLeft = topLeft;
    if (QWidget* parent = parentWidget()) {
        const int popupWidth = qMax(width(), sizeHint().width());
        const int popupHeight = qMax(height(), sizeHint().height());
        const int clampedX
            = qBound(8, targetTopLeft.x(), qMax(8, parent->width() - popupWidth - 8));
        const int clampedY
            = qBound(8, targetTopLeft.y(), qMax(8, parent->height() - popupHeight - 8));
        targetTopLeft = QPoint(clampedX, clampedY);
    }

    const bool wasVisible = isVisible();
    const bool wasHiding = m_isHiding;
    m_isVisible = true;
    m_isHiding = false;

    if (!wasVisible) {
        m_anchorXAnim->stop();
        m_anchorYAnim->stop();
        m_anchorX = static_cast<qreal>(targetTopLeft.x());
        m_anchorY = static_cast<qreal>(targetTopLeft.y());
        setSlideOffset(animateShow ? -SLIDE_OFFSET : 0.0);
        updateWidgetPosition();
        show();
        raise();
        startShowAnimation(animateShow);
        return;
    }

    const bool shouldAnimateMove = animateMove || wasHiding;
    if (shouldAnimateMove) {
        const qreal targetX = static_cast<qreal>(targetTopLeft.x());
        const qreal targetY = static_cast<qreal>(targetTopLeft.y());
        const qreal dx = qAbs(targetX - m_anchorX);
        const qreal dy = qAbs(targetY - m_anchorY);
        if (m_anchorXAnim->state() == QAbstractAnimation::Running) {
            m_anchorXAnim->setEndValue(targetX);
        } else if (dx > 0.5) {
            m_anchorXAnim->setDuration(SHOW_DURATION);
            m_anchorXAnim->setStartValue(m_anchorX);
            m_anchorXAnim->setEndValue(targetX);
            m_anchorXAnim->start();
        } else {
            setAnchorX(targetX);
        }
        if (m_anchorYAnim->state() == QAbstractAnimation::Running) {
            m_anchorYAnim->setEndValue(targetY);
        } else if (dy > 0.5) {
            m_anchorYAnim->setDuration(SHOW_DURATION);
            m_anchorYAnim->setStartValue(m_anchorY);
            m_anchorYAnim->setEndValue(targetY);
            m_anchorYAnim->start();
        } else {
            setAnchorY(targetY);
        }
    } else {
        if (m_anchorXAnim->state() == QAbstractAnimation::Running) {
            m_anchorXAnim->stop();
        }
        if (m_anchorYAnim->state() == QAbstractAnimation::Running) {
            m_anchorYAnim->stop();
        }
        setAnchorX(static_cast<qreal>(targetTopLeft.x()));
        setAnchorY(static_cast<qreal>(targetTopLeft.y()));
    }
    updateWidgetPosition();
    raise();

    if (wasHiding) {
        startShowAnimation(false);
    }
}

void SelectionActionPopup::hideAnimated()
{
    if (!isVisible() || m_isHiding) {
        return;
    }
    startHideAnimation();
}

void SelectionActionPopup::hideImmediate()
{
    m_opacityAnim->stop();
    m_slideAnim->stop();
    m_anchorXAnim->stop();
    m_anchorYAnim->stop();
    m_isVisible = false;
    m_isHiding = false;
    if (m_tabletHoveredButton) {
        m_tabletHoveredButton->setHovered(false);
        m_tabletHoveredButton = nullptr;
    }
    setPopupOpacity(0.0);
    setSlideOffset(0.0);
    hide();
}

void SelectionActionPopup::setPopupOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    update();
    const QList<QWidget*> children = findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget* section : children) {
        section->update();
        for (QWidget* w : section->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            w->update();
        }
    }
}

void SelectionActionPopup::setSlideOffset(qreal offset)
{
    m_slideOffset = offset;
    updateWidgetPosition();
}

void SelectionActionPopup::setAnchorX(qreal x)
{
    m_anchorX = x;
    updateWidgetPosition();
}

void SelectionActionPopup::setAnchorY(qreal y)
{
    m_anchorY = y;
    updateWidgetPosition();
}

void SelectionActionPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_opacity);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const auto drawSection = [&painter, &colors](const QWidget* section) {
        QRectF rect = section->geometry().adjusted(0.5, 0.5, -0.5, -0.5);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceElevated());
        painter.drawRoundedRect(rect, kCornerRadius, kCornerRadius);

        {
            QColor borderTop = colors.borderSubtle();
            ruwa::ui::painting::drawGradientBorder(painter, rect, kCornerRadius, borderTop,
                ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
        }
    };

    drawSection(m_firstSection);
    drawSection(m_secondSection);
}

void SelectionActionPopup::tabletEvent(QTabletEvent* event)
{
    if (!event) {
        return;
    }
    if (event->type() == QEvent::TabletLeaveProximity) {
        if (m_tabletHoveredButton) {
            m_tabletHoveredButton->setHovered(false);
            m_tabletHoveredButton = nullptr;
        }
        event->accept();
        return;
    }
    const QPoint localPos = event->position().toPoint();
    QWidget* target = childAt(localPos);
    auto* button = qobject_cast<BaseAnimatedButton*>(target);
    if (m_tabletHoveredButton != button) {
        if (m_tabletHoveredButton) {
            m_tabletHoveredButton->setHovered(false);
        }
        m_tabletHoveredButton = button;
        if (m_tabletHoveredButton) {
            m_tabletHoveredButton->setHovered(true);
        }
    }

    if (!button) {
        event->ignore();
        return;
    }
    const QPoint buttonPos = button->mapFrom(this, localPos);
    switch (event->type()) {
    case QEvent::TabletPress: {
        QMouseEvent press(QEvent::MouseButtonPress, buttonPos, event->globalPosition(),
            Qt::LeftButton, Qt::LeftButton, event->modifiers());
        QCoreApplication::sendEvent(button, &press);
        event->accept();
        return;
    }
    case QEvent::TabletMove: {
        QMouseEvent move(QEvent::MouseMove, buttonPos, event->globalPosition(), Qt::NoButton,
            event->buttons(), event->modifiers());
        QCoreApplication::sendEvent(button, &move);
        event->accept();
        return;
    }
    case QEvent::TabletRelease: {
        QMouseEvent release(QEvent::MouseButtonRelease, buttonPos, event->globalPosition(),
            Qt::LeftButton, Qt::NoButton, event->modifiers());
        QCoreApplication::sendEvent(button, &release);
        event->accept();
        return;
    }
    default:
        event->ignore();
        return;
    }
}

void SelectionActionPopup::updateSwatchStyle()
{
    if (!m_swatchButton) {
        return;
    }
    m_swatchButton->setProperty("popup_swatch_color", m_fillColor);
    m_swatchButton->update();
}

void SelectionActionPopup::updateIcons()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QColor iconColor = colors.text;
    auto& iconProvider = ruwa::ui::core::IconProvider::instance();
    const auto resolveIcon
        = [&iconProvider, &iconColor](ruwa::ui::core::IconProvider::StandardIcon standard,
              const QStringList& aliasFallbacks) -> QIcon {
        QIcon icon = iconProvider.getColoredIcon(standard, iconColor);
        if (!icon.pixmap(QSize(kIconSize, kIconSize)).isNull()) {
            return icon;
        }
        for (const QString& alias : aliasFallbacks) {
            QIcon aliasIcon = iconProvider.getColoredIcon(alias, iconColor);
            if (!aliasIcon.pixmap(QSize(kIconSize, kIconSize)).isNull()) {
                return aliasIcon;
            }
        }
        return icon;
    };

    m_fillButton->setIcon(resolveIcon(
        ruwa::ui::core::IconProvider::StandardIcon::FillColor, { "FillColor", "fill", "Brush" }));
    // Placeholder icon for transform action.
    m_transformButton->setIcon(resolveIcon(ruwa::ui::core::IconProvider::StandardIcon::CanvasResize,
        { "CanvasResize", "Move", "RotateView" }));
    QIcon deleteIcon = resolveIcon(ruwa::ui::core::IconProvider::StandardIcon::Trash,
        { "trash", "layer-delete", "LayerDelete", "delete" });
    if (deleteIcon.pixmap(QSize(kIconSize, kIconSize)).isNull()) {
        // Match LayersPanel fallback glyph so delete icon is always visible.
        QPixmap fallback(QSize(kIconSize, kIconSize));
        fallback.fill(Qt::transparent);
        QPainter p(&fallback);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(iconColor, 1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal w = static_cast<qreal>(kIconSize);
        const qreal h = static_cast<qreal>(kIconSize);
        p.drawLine(QPointF(w * 0.18, h * 0.3), QPointF(w * 0.82, h * 0.3));
        p.drawRoundedRect(QRectF(w * 0.28, h * 0.3, w * 0.44, h * 0.5), 1.8, 1.8);
        p.drawLine(QPointF(w * 0.38, h * 0.2), QPointF(w * 0.62, h * 0.2));
        p.end();
        deleteIcon = QIcon(fallback);
    }
    m_deleteButton->setIcon(deleteIcon);
    m_flipVerticalButton->setIcon(
        resolveIcon(ruwa::ui::core::IconProvider::StandardIcon::FlipVertical, { "FlipVertical" }));
    m_flipHorizontalButton->setIcon(resolveIcon(
        ruwa::ui::core::IconProvider::StandardIcon::FlipHorizontal, { "FlipHorizontal" }));
    m_closeButton->setIcon(resolveIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Close, { "Close", "cross", "Minimize" }));

    m_fillButton->setIconSize(QSize(kIconSize, kIconSize));
    m_transformButton->setIconSize(QSize(kIconSize, kIconSize));
    m_deleteButton->setIconSize(QSize(kIconSize, kIconSize));
    m_flipVerticalButton->setIconSize(QSize(kIconSize, kIconSize));
    m_flipHorizontalButton->setIconSize(QSize(kIconSize, kIconSize));
    m_closeButton->setIconSize(QSize(kIconSize, kIconSize));

    if (m_closeSeparator) {
        m_closeSeparator->update();
    }
}

void SelectionActionPopup::startShowAnimation(bool animateSlide)
{
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);

    m_opacityAnim->stop();
    m_opacityAnim->setDuration(SHOW_DURATION);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->start();

    if (m_slideAnim->state() == QAbstractAnimation::Running) {
        m_slideAnim->stop();
    }
    if (animateSlide || qAbs(m_slideOffset) > 0.5) {
        m_slideAnim->setDuration(SHOW_DURATION);
        m_slideAnim->setStartValue(m_slideOffset);
        m_slideAnim->setEndValue(0.0);
        m_slideAnim->start();
    } else {
        setSlideOffset(0.0);
    }
}

void SelectionActionPopup::startHideAnimation()
{
    m_isVisible = false;
    m_isHiding = true;
    if (m_tabletHoveredButton) {
        m_tabletHoveredButton->setHovered(false);
        m_tabletHoveredButton = nullptr;
    }

    m_opacityAnim->stop();
    m_slideAnim->stop();
    m_anchorXAnim->stop();
    m_anchorYAnim->stop();

    m_opacityAnim->setDuration(HIDE_DURATION);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_slideAnim->setDuration(HIDE_DURATION);
    m_slideAnim->setStartValue(m_slideOffset);
    m_slideAnim->setEndValue(-SLIDE_OFFSET);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);

    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_isHiding || m_isVisible) {
            return;
        }
        m_isHiding = false;
        hide();
    });

    m_opacityAnim->start();
    m_slideAnim->start();
}

void SelectionActionPopup::updateWidgetPosition()
{
    int x = static_cast<int>(std::round(m_anchorX));
    int y = static_cast<int>(std::round(m_anchorY + m_slideOffset));

    if (QWidget* parent = parentWidget()) {
        const int popupWidth = qMax(width(), sizeHint().width());
        const int popupHeight = qMax(height(), sizeHint().height());
        x = qBound(8, x, qMax(8, parent->width() - popupWidth - 8));
        y = qBound(8, y, qMax(8, parent->height() - popupHeight - 8));
    }

    move(x, y);
    if (QWidget* parent = parentWidget()) {
        parent->update(geometry());
    }
}

void SelectionActionPopup::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    if (QWidget* parent = parentWidget()) {
        const QRect oldRect(event->oldPos(), size());
        const QRect newRect(event->pos(), size());
        parent->update(oldRect.united(newRect));
    }
}

void SelectionActionPopup::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (QWidget* parent = parentWidget()) {
        const QRect oldRect(pos(), event->oldSize());
        const QRect newRect(pos(), event->size());
        parent->update(oldRect.united(newRect));
    }
}

} // namespace ruwa::ui::widgets
