// SPDX-License-Identifier: MPL-2.0

#include "TextFormattingPopup.h"

#include "shared/resources/IconProvider.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/inputs/FontDropdownSelector.h"
#include "shared/style/PaintingUtils.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QColor>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPen>
#include <QPointer>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QTabletEvent>

#include <cmath>

namespace ruwa::ui::widgets {
namespace {

constexpr int kCornerRadius = 8;
constexpr int kSectionHeight = 40;
constexpr int kSectionPadding = 6;
constexpr int kIconSize = 16;
constexpr int kButtonSize = 28;
constexpr int kFontDropdownWidth = 178;
constexpr int kShowDuration = 120;
constexpr int kHideDuration = 200;
constexpr int kSlideOffset = 14;

class PopupActionButton final : public BaseAnimatedButton {
public:
    explicit PopupActionButton(
        QWidget* opacityProvider, QWidget* parent = nullptr, bool swatchButton = false)
        : BaseAnimatedButton(parent)
        , m_opacityProvider(opacityProvider)
        , m_isSwatchButton(swatchButton)
    {
        setFixedSize(kButtonSize, kButtonSize);
        setCheckable(false);
        setFocusPolicy(Qt::NoFocus);
        setIconSize(QSize(kIconSize, kIconSize));
        setProperty("popup_swatch_color", QColor(0, 0, 0, 255));
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
        const qreal active = activeProgress();
        constexpr qreal radius = 4.0;

        if (m_isSwatchButton) {
            QColor swatch = property("popup_swatch_color").value<QColor>();
            if (!swatch.isValid()) {
                swatch = QColor(0, 0, 0, 255);
            }
            p.setPen(QPen(colors.borderSubtle(), 1.0));
            p.setBrush(swatch);
            p.drawRoundedRect(r, radius, radius);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::transparent);
            p.drawRoundedRect(r, radius, radius);
        }

        if (active > 0.001) {
            QColor activeColor = colors.accentDim(72);
            activeColor.setAlphaF(activeColor.alphaF() * active);
            p.setBrush(activeColor);
            p.drawRoundedRect(r, radius, radius);
        }
        if (hover > 0.001) {
            QColor hoverColor = hoverOverlay;
            hoverColor.setAlphaF(hoverColor.alphaF() * hover);
            p.setBrush(hoverColor);
            p.drawRoundedRect(r, radius, radius);
        }
        if (isPressed()) {
            p.setBrush(pressedOverlay);
            p.drawRoundedRect(r, radius, radius);
        }
        if (active > 0.001) {
            QColor borderColor = colors.accent;
            borderColor.setAlphaF(0.55 * active);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(borderColor, 1.0));
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

} // namespace

TextFormattingPopup::TextFormattingPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setTabletTracking(true);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_section = createSection(this);
    rootLayout->addWidget(m_section);

    auto* sectionLayout = new QHBoxLayout(m_section);
    sectionLayout->setContentsMargins(
        kSectionPadding, kSectionPadding, kSectionPadding, kSectionPadding);
    sectionLayout->setSpacing(4);

    m_fontDropdown = new FontDropdownSelector(m_section);
    m_fontDropdown->setFixedWidth(kFontDropdownWidth);
    m_fontDropdown->setPopupMinWidth(300);
    m_fontDropdown->setPopupMaxHeight(360);
    m_fontDropdown->setPlaceholderText(tr("Font"));
    m_fontDropdown->setToolTip(tr("Font family"));
    m_fontDropdown->setOpacityProvider(this);
    sectionLayout->addWidget(m_fontDropdown);
    connect(m_fontDropdown, &FontDropdownSelector::activated, this,
        &TextFormattingPopup::fontFamilyActivated);

    m_boldButton = new PopupActionButton(this, m_section);
    m_boldButton->setToolTip(tr("Bold"));
    sectionLayout->addWidget(m_boldButton);
    connect(m_boldButton, &QPushButton::clicked, this, &TextFormattingPopup::boldClicked);

    m_italicButton = new PopupActionButton(this, m_section);
    m_italicButton->setToolTip(tr("Italic"));
    sectionLayout->addWidget(m_italicButton);
    connect(m_italicButton, &QPushButton::clicked, this, &TextFormattingPopup::italicClicked);

    m_underlineButton = new PopupActionButton(this, m_section);
    m_underlineButton->setToolTip(tr("Underline"));
    sectionLayout->addWidget(m_underlineButton);
    connect(m_underlineButton, &QPushButton::clicked, this, &TextFormattingPopup::underlineClicked);

    m_colorButton = new PopupActionButton(this, m_section, true);
    m_colorButton->setToolTip(tr("Text color"));
    sectionLayout->addWidget(m_colorButton);
    connect(m_colorButton, &QPushButton::clicked, this,
        [this]() { emit textColorClicked(m_colorButton); });

    m_opacityAnim = new QPropertyAnimation(this, "popupOpacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_slideAnim = new QPropertyAnimation(this, "slideOffset", this);
    m_slideAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_anchorXAnim = new QPropertyAnimation(this, "anchorX", this);
    m_anchorXAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_anchorYAnim = new QPropertyAnimation(this, "anchorY", this);
    m_anchorYAnim->setEasingCurve(QEasingCurve::OutCubic);

    updateIcons();
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            updateIcons();
            update();
        });
    hide();
}

QWidget* TextFormattingPopup::createSection(QWidget* parent) const
{
    auto* section = new QWidget(parent);
    section->setAttribute(Qt::WA_TranslucentBackground);
    section->setAttribute(Qt::WA_NoSystemBackground);
    section->setFixedHeight(kSectionHeight);
    return section;
}

void TextFormattingPopup::showAt(const QPoint& topLeft, bool animateShow, bool animateMove)
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
        setSlideOffset(animateShow ? kSlideOffset : 0.0);
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
            m_anchorXAnim->setDuration(kShowDuration);
            m_anchorXAnim->setStartValue(m_anchorX);
            m_anchorXAnim->setEndValue(targetX);
            m_anchorXAnim->start();
        } else {
            setAnchorX(targetX);
        }
        if (m_anchorYAnim->state() == QAbstractAnimation::Running) {
            m_anchorYAnim->setEndValue(targetY);
        } else if (dy > 0.5) {
            m_anchorYAnim->setDuration(kShowDuration);
            m_anchorYAnim->setStartValue(m_anchorY);
            m_anchorYAnim->setEndValue(targetY);
            m_anchorYAnim->start();
        } else {
            setAnchorY(targetY);
        }
    } else {
        m_anchorXAnim->stop();
        m_anchorYAnim->stop();
        setAnchorX(static_cast<qreal>(targetTopLeft.x()));
        setAnchorY(static_cast<qreal>(targetTopLeft.y()));
    }
    updateWidgetPosition();
    raise();

    if (wasHiding) {
        startShowAnimation(false);
    }
}

void TextFormattingPopup::hideAnimated()
{
    if (!isVisible() || m_isHiding) {
        return;
    }
    startHideAnimation();
}

void TextFormattingPopup::hideImmediate()
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

void TextFormattingPopup::setPopupOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    update();
    if (m_fontDropdown)
        m_fontDropdown->update();
    if (m_boldButton)
        m_boldButton->update();
    if (m_italicButton)
        m_italicButton->update();
    if (m_underlineButton)
        m_underlineButton->update();
    if (m_colorButton)
        m_colorButton->update();
}

void TextFormattingPopup::setEffectStates(bool bold, bool italic, bool underline)
{
    if (m_boldButton) {
        m_boldButton->setActive(bold);
    }
    if (m_italicButton) {
        m_italicButton->setActive(italic);
    }
    if (m_underlineButton) {
        m_underlineButton->setActive(underline);
    }
}

void TextFormattingPopup::setCurrentFontFamily(const QString& family)
{
    if (m_fontDropdown) {
        m_fontDropdown->setCurrentFamily(family);
    }
}

void TextFormattingPopup::setTextColor(const QColor& color)
{
    if (!color.isValid() || color == m_textColor) {
        return;
    }
    m_textColor = color;
    if (m_colorButton) {
        m_colorButton->setProperty("popup_swatch_color", m_textColor);
        m_colorButton->update();
    }
}

void TextFormattingPopup::setSlideOffset(qreal offset)
{
    m_slideOffset = offset;
    updateWidgetPosition();
}

void TextFormattingPopup::setAnchorX(qreal x)
{
    m_anchorX = x;
    updateWidgetPosition();
}

void TextFormattingPopup::setAnchorY(qreal y)
{
    m_anchorY = y;
    updateWidgetPosition();
}

void TextFormattingPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_opacity);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QRectF sectionRect = m_section->geometry().adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.surfaceElevated());
    painter.drawRoundedRect(sectionRect, kCornerRadius, kCornerRadius);

    QColor borderTop = colors.borderSubtle();
    ruwa::ui::painting::drawGradientBorder(painter, sectionRect, kCornerRadius, borderTop,
        ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
}

void TextFormattingPopup::tabletEvent(QTabletEvent* event)
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
    auto* fontDropdown = qobject_cast<FontDropdownSelector*>(target);
    if (m_tabletHoveredButton != button) {
        if (m_tabletHoveredButton) {
            m_tabletHoveredButton->setHovered(false);
        }
        m_tabletHoveredButton = button;
        if (m_tabletHoveredButton) {
            m_tabletHoveredButton->setHovered(true);
        }
    }

    QWidget* interactiveTarget
        = button ? static_cast<QWidget*>(button) : static_cast<QWidget*>(fontDropdown);
    if (!interactiveTarget) {
        event->ignore();
        return;
    }

    const QPoint targetPos = interactiveTarget->mapFrom(this, localPos);
    switch (event->type()) {
    case QEvent::TabletPress: {
        QMouseEvent press(QEvent::MouseButtonPress, targetPos, event->globalPosition(),
            Qt::LeftButton, Qt::LeftButton, event->modifiers());
        QCoreApplication::sendEvent(interactiveTarget, &press);
        event->accept();
        return;
    }
    case QEvent::TabletMove: {
        QMouseEvent move(QEvent::MouseMove, targetPos, event->globalPosition(), Qt::NoButton,
            event->buttons(), event->modifiers());
        QCoreApplication::sendEvent(interactiveTarget, &move);
        event->accept();
        return;
    }
    case QEvent::TabletRelease: {
        QMouseEvent release(QEvent::MouseButtonRelease, targetPos, event->globalPosition(),
            Qt::LeftButton, Qt::NoButton, event->modifiers());
        QCoreApplication::sendEvent(interactiveTarget, &release);
        event->accept();
        return;
    }
    default:
        event->ignore();
        return;
    }
}

void TextFormattingPopup::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    if (QWidget* parent = parentWidget()) {
        parent->update(QRect(event->oldPos(), size()).united(QRect(event->pos(), size())));
    }
}

void TextFormattingPopup::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (QWidget* parent = parentWidget()) {
        parent->update(QRect(pos(), event->oldSize()).united(QRect(pos(), event->size())));
    }
}

void TextFormattingPopup::updateIcons()
{
    if (!m_boldButton || !m_italicButton || !m_underlineButton) {
        return;
    }

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QColor iconColor = colors.text;
    auto& iconProvider = ruwa::ui::core::IconProvider::instance();
    const auto resolveIcon
        = [&iconProvider, &iconColor](
              ruwa::ui::core::IconProvider::StandardIcon standard, const QString& alias) -> QIcon {
        QIcon icon = iconProvider.getColoredIcon(standard, iconColor);
        if (!icon.pixmap(QSize(kIconSize, kIconSize)).isNull()) {
            return icon;
        }
        return iconProvider.getColoredIcon(alias, iconColor);
    };

    m_boldButton->setIcon(resolveIcon(ruwa::ui::core::IconProvider::StandardIcon::Bold, "Bold"));
    m_italicButton->setIcon(
        resolveIcon(ruwa::ui::core::IconProvider::StandardIcon::Italic, "Italic"));
    m_underlineButton->setIcon(
        resolveIcon(ruwa::ui::core::IconProvider::StandardIcon::Underline, "Underline"));

    m_boldButton->setIconSize(QSize(kIconSize, kIconSize));
    m_italicButton->setIconSize(QSize(kIconSize, kIconSize));
    m_underlineButton->setIconSize(QSize(kIconSize, kIconSize));
}

void TextFormattingPopup::startShowAnimation(bool animateSlide)
{
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);

    m_opacityAnim->stop();
    m_opacityAnim->setDuration(kShowDuration);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->start();

    m_slideAnim->stop();
    if (animateSlide || qAbs(m_slideOffset) > 0.5) {
        m_slideAnim->setDuration(kShowDuration);
        m_slideAnim->setStartValue(m_slideOffset);
        m_slideAnim->setEndValue(0.0);
        m_slideAnim->start();
    } else {
        setSlideOffset(0.0);
    }
}

void TextFormattingPopup::startHideAnimation()
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

    m_opacityAnim->setDuration(kHideDuration);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_slideAnim->setDuration(kHideDuration);
    m_slideAnim->setStartValue(m_slideOffset);
    m_slideAnim->setEndValue(-kSlideOffset);
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

void TextFormattingPopup::updateWidgetPosition()
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

} // namespace ruwa::ui::widgets
