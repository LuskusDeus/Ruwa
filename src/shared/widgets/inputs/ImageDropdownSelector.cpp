// SPDX-License-Identifier: MPL-2.0

#include "ImageDropdownSelector.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/layout/FlowLayout.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <functional>
#include <utility>

namespace ruwa::ui::widgets {

namespace {

constexpr int kPopupPadding = 8;
constexpr int kPopupSpacing = 8;
constexpr int kCornerRadius = 8;
constexpr int kPopupOffset = 20;
constexpr int kShowDurationMs = 120;
constexpr int kHideDurationMs = 80;
constexpr int kSlideDurationMs = 200;
constexpr int kPopupMinVisibleHeight = 160;

QImage tintedPreview(const ImageDropdownItem& item, const QColor& fallbackColor)
{
    if (item.previewImage.isNull()) {
        return QImage();
    }
    if (!item.tintPreview) {
        return item.previewImage;
    }

    QImage tinted(item.previewImage.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);
    QPainter p(&tinted);
    p.drawImage(0, 0, item.previewImage);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(tinted.rect(), item.previewTint.isValid() ? item.previewTint : fallbackColor);
    return tinted;
}

class ImageOptionCardButton final : public BaseAnimatedButton {
public:
    ImageOptionCardButton(
        const ImageDropdownItem& item, int index, const QSize& cardSize, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_item(item)
        , m_index(index)
    {
        setFixedSize(cardSize);
        setEnabled(item.enabled);
        setCheckable(false);
        setCursor(item.enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    int index() const { return m_index; }
    void setSelected(bool selected) { setActive(selected); }
    void setOnHovered(std::function<void(int)> cb) { m_onHovered = std::move(cb); }

protected:
    void enterEvent(QEnterEvent* event) override
    {
        BaseAnimatedButton::enterEvent(event);
        if (m_onHovered) {
            m_onHovered(m_index);
        }
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF cardRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        QColor bg = colors.overlayBase();
        if (hoverProgress() > 0.0) {
            bg = ruwa::ui::core::ThemeColors::interpolate(
                bg, colors.overlayHover(), hoverProgress() * 0.35);
        }
        if (isPressed()) {
            bg = ruwa::ui::core::ThemeColors::interpolate(bg, colors.overlay(0.18), 0.7);
        }
        if (activeProgress() > 0.0) {
            bg = ruwa::ui::core::ThemeColors::interpolate(
                bg, colors.overlay(0.18), activeProgress());
        }

        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(cardRect, 12, 12);

        QColor topBorder = ruwa::ui::core::ThemeColors::interpolate(
            colors.borderSubtle(), colors.primary, qMax(hoverProgress() * 0.45, activeProgress()));
        QColor bottomBorder = colors.borderSubtle();
        bottomBorder.setAlpha(bottomBorder.alpha() / 2);
        ruwa::ui::painting::drawGradientBorder(p, cardRect, 12, topBorder, bottomBorder);

        const int previewHeight = qMax(30, height() - 40);
        const QRectF previewRect = cardRect.adjusted(8, 8, -8, -(height() - previewHeight - 8));
        const QRectF labelRect = QRectF(cardRect.left() + 10, previewRect.bottom() + 8,
            cardRect.width() - 20, cardRect.bottom() - previewRect.bottom() - 12);

        p.setBrush(colors.surfaceElevated());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(previewRect, 9, 9);

        const QImage preview = tintedPreview(m_item, colors.text);
        if (!preview.isNull()) {
            const QImage scaled = preview.scaled(
                previewRect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPointF origin(previewRect.center().x() - scaled.width() / 2.0,
                previewRect.center().y() - scaled.height() / 2.0);
            p.drawImage(origin, scaled);
        }

        QFont titleFont = font();
        titleFont.setPixelSize(qMax(10, titleFont.pixelSize()));
        titleFont.setWeight(activeProgress() > 0.5 ? QFont::DemiBold : QFont::Medium);
        p.setFont(titleFont);
        p.setPen(isEnabled() ? colors.text : colors.textDisabled());
        p.drawText(
            labelRect.toRect(), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, m_item.text);

        if (!m_item.subtitle.isEmpty()) {
            QFont subFont = titleFont;
            subFont.setPixelSize(qMax(9, titleFont.pixelSize() - 1));
            subFont.setWeight(QFont::Normal);
            p.setFont(subFont);
            p.setPen(colors.textMuted);
            p.drawText(labelRect.adjusted(0, 14, 0, 0).toRect(),
                Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, m_item.subtitle);
        }

        if (activeProgress() > 0.0) {
            p.save();
            p.setOpacity(activeProgress());
            p.setBrush(colors.primary);
            p.setPen(Qt::NoPen);
            p.drawEllipse(QRectF(cardRect.right() - 18, cardRect.top() + 8, 10, 10));
            p.restore();
        }
    }

private:
    ImageDropdownItem m_item;
    int m_index = -1;
    std::function<void(int)> m_onHovered;
};

} // namespace

class ImageDropdownPopup final : public QWidget {
public:
    explicit ImageDropdownPopup(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setWindowFlags(Qt::Widget);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();

        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(kPopupPadding, kPopupPadding, kPopupPadding, kPopupPadding);
        m_scrollArea = new SmoothScrollArea(this);
        m_scrollArea->setFillBackground(false);
        m_scrollArea->setScrollBarTransparentTrack(true);
        m_scrollArea->setContentWidthFixedToViewport(true);
        m_layout->addWidget(m_scrollArea);

        m_contentWidget = new QWidget();
        m_contentWidget->setAttribute(Qt::WA_TranslucentBackground);
        m_flowLayout = new FlowLayout(m_contentWidget, 0, kPopupSpacing, kPopupSpacing);
        m_scrollArea->setWidget(m_contentWidget);

        m_opacityEffect = new QGraphicsOpacityEffect(this);
        m_opacityEffect->setOpacity(0.0);
        setGraphicsEffect(m_opacityEffect);

        m_opacityAnim = new QVariantAnimation(this);
        m_opacityAnim->setDuration(kShowDurationMs);
        m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(
            m_opacityAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
                m_popupOpacity = qBound(0.0, value.toReal(), 1.0);
                m_opacityEffect->setOpacity(m_popupOpacity);
            });

        m_posAnim = new QPropertyAnimation(this, "pos", this);
        m_posAnim->setDuration(kSlideDurationMs);
        m_posAnim->setEasingCurve(QEasingCurve::OutCubic);
    }

    void setItems(const QList<ImageDropdownItem>& items, int selectedIndex, int minWidth,
        int columns, const QSize& cardSize)
    {
        m_items = items;
        m_selectedIndex = selectedIndex;
        m_hoveredIndex = -1;
        m_columns = qMax(1, columns);
        m_cardSize = cardSize;
        m_buttons.clear();

        while (QLayoutItem* item = m_flowLayout->takeAt(0)) {
            delete item->widget();
            delete item;
        }

        for (int i = 0; i < m_items.size(); ++i) {
            auto* button = new ImageOptionCardButton(m_items[i], i, m_cardSize, m_contentWidget);
            button->setSelected(i == m_selectedIndex);
            button->setOnHovered([this](int hovered) {
                m_hoveredIndex = hovered;
                if (m_onHoveredIndexChanged) {
                    m_onHoveredIndexChanged(hovered);
                }
            });
            connect(button, &QPushButton::clicked, this, [this, i]() {
                if (m_onItemActivated) {
                    m_onItemActivated(i);
                }
            });
            m_flowLayout->addWidget(button);
            m_buttons.append(button);
        }

        const int visibleCardsHint = qBound(1, m_items.size(), 4);
        const int preferredContentWidth
            = visibleCardsHint * m_cardSize.width() + qMax(0, visibleCardsHint - 1) * kPopupSpacing;
        m_popupWidth = qMax(minWidth, preferredContentWidth + kPopupPadding * 2);
        applySizeConstraints(m_popupWidth, 100000);
    }

    void setSelectedIndex(int index)
    {
        m_selectedIndex = index;
        for (ImageOptionCardButton* button : std::as_const(m_buttons)) {
            button->setSelected(button->index() == index);
        }
        ensureSelectedVisible();
    }

    int preferredHeight() const { return m_contentHeight + kPopupPadding * 2; }
    int preferredWidth() const { return m_popupWidth; }
    void setOnItemActivated(std::function<void(int)> cb) { m_onItemActivated = std::move(cb); }
    void setOnPopupHidden(std::function<void()> cb) { m_onPopupHidden = std::move(cb); }
    void setOnHoveredIndexChanged(std::function<void(int)> cb)
    {
        m_onHoveredIndexChanged = std::move(cb);
    }
    int hoveredIndex() const { return m_hoveredIndex; }
    int currentColumnCount() const
    {
        const int contentWidth = qMax(1, width() - kPopupPadding * 2);
        const int slotWidth = qMax(1, m_cardSize.width() + kPopupSpacing);
        return qMax(1, (contentWidth + kPopupSpacing) / slotWidth);
    }
    bool containsGlobal(const QPoint& globalPos) const
    {
        return isVisible() && QRect(mapToGlobal(QPoint(0, 0)), size()).contains(globalPos);
    }

    void applyHeightConstraint(int maxHeight) { applySizeConstraints(m_popupWidth, maxHeight); }

    void applySizeConstraints(int maxWidth, int maxHeight)
    {
        const int targetWidth = qMax(kPopupPadding * 2 + 1, qMin(m_popupWidth, maxWidth));
        const int contentWidth = qMax(1, targetWidth - kPopupPadding * 2);
        m_contentWidget->setFixedWidth(contentWidth);
        m_contentWidget->updateGeometry();
        m_contentHeight = contentHeightForWidth(contentWidth);
        const int targetHeight
            = qMax(kPopupPadding * 2 + 1, qMin(m_contentHeight + kPopupPadding * 2, maxHeight));
        m_scrollArea->setFixedHeight(qMax(1, targetHeight - kPopupPadding * 2));
        m_scrollArea->refreshScrollGeometry();
        setFixedSize(targetWidth, targetHeight);
        ensureSelectedVisible();
    }

    void showAnimated(const QPoint& targetPos)
    {
        disconnect(m_opacityAnim, &QVariantAnimation::finished, this, nullptr);
        const QPoint startPos(targetPos.x(), targetPos.y() - kPopupOffset);
        show();
        raise();
        move(startPos);
        m_opacityAnim->stop();
        m_posAnim->stop();
        m_opacityAnim->setDuration(kShowDurationMs);
        m_opacityAnim->setStartValue(m_popupOpacity);
        m_opacityAnim->setEndValue(1.0);
        m_opacityAnim->start();
        m_posAnim->setDuration(kSlideDurationMs);
        m_posAnim->setStartValue(startPos);
        m_posAnim->setEndValue(targetPos);
        m_posAnim->start();
    }

    void hideAnimated()
    {
        m_posAnim->stop();
        m_opacityAnim->stop();
        disconnect(m_opacityAnim, &QVariantAnimation::finished, this, nullptr);
        connect(m_opacityAnim, &QVariantAnimation::finished, this, [this]() {
            hide();
            if (m_onPopupHidden) {
                m_onPopupHidden();
            }
        });
        m_opacityAnim->setDuration(kHideDurationMs);
        m_opacityAnim->setStartValue(m_popupOpacity);
        m_opacityAnim->setEndValue(0.0);
        m_opacityAnim->start();
        m_posAnim->setDuration(kHideDurationMs);
        m_posAnim->setStartValue(pos());
        m_posAnim->setEndValue(QPoint(pos().x(), pos().y() - (kPopupOffset / 2)));
        m_posAnim->start();
    }

    void forceHide()
    {
        m_posAnim->stop();
        m_opacityAnim->stop();
        m_popupOpacity = 0.0;
        m_opacityEffect->setOpacity(0.0);
        hide();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(Qt::NoPen);
        p.setBrush(colors.surfaceElevated());
        p.drawRoundedRect(rect, kCornerRadius, kCornerRadius);
        ruwa::ui::painting::drawGradientBorder(
            p, rect, kCornerRadius, colors.borderSubtle(), colors.borderSubtle());
    }

private:
    void ensureSelectedVisible()
    {
        if (!m_scrollArea) {
            return;
        }
        for (ImageOptionCardButton* button : std::as_const(m_buttons)) {
            if (button->index() != m_selectedIndex) {
                continue;
            }
            const int top = button->y();
            const int bottom = top + button->height();
            const int scrollValue = m_scrollArea->scrollValue();
            const int viewportHeight
                = m_scrollArea->viewport() ? m_scrollArea->viewport()->height() : 0;
            if (top < scrollValue) {
                m_scrollArea->scrollTo(top, false);
            } else if (bottom > scrollValue + viewportHeight) {
                m_scrollArea->scrollTo(bottom - viewportHeight, false);
            }
            return;
        }
    }

    int contentHeightForWidth(int contentWidth) const
    {
        if (!m_flowLayout) {
            return qMax(1, m_cardSize.height());
        }
        return qMax(m_cardSize.height(), m_flowLayout->heightForWidth(contentWidth));
    }

    QList<ImageDropdownItem> m_items;
    QList<ImageOptionCardButton*> m_buttons;
    QVBoxLayout* m_layout = nullptr;
    SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    FlowLayout* m_flowLayout = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QVariantAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    std::function<void(int)> m_onItemActivated;
    std::function<void()> m_onPopupHidden;
    std::function<void(int)> m_onHoveredIndexChanged;
    QSize m_cardSize;
    int m_popupWidth = 0;
    int m_contentHeight = 0;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    int m_columns = 2;
    qreal m_popupOpacity = 0.0;
};

ImageDropdownSelector::ImageDropdownSelector(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setFixedHeight(30);
    setCursor(Qt::PointingHandCursor);

    m_popup = new ImageDropdownPopup(window());
    m_popup->setProperty("ruwa_owner_combo", QVariant::fromValue(static_cast<QWidget*>(this)));
    m_popup->setOnItemActivated([this](int index) {
        const bool changed = (m_currentIndex != index);
        setCurrentIndex(index);
        closePopup();
        emit activated(index);
        if (changed)
            emit currentIndexChanged(index);
    });
    m_popup->setOnHoveredIndexChanged([this](int index) { emit itemHovered(index); });
    m_popup->setOnPopupHidden([this]() {
        animateArrowTo(0.0);
        if (!underMouse()) {
            animateHoverTo(0.0);
        }
        emit popupHidden();
    });

    m_hoverAnim = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnim->setDuration(170);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_pressAnim = new QPropertyAnimation(this, "pressProgress", this);
    m_pressAnim->setDuration(90);
    m_pressAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_arrowAnim = new QPropertyAnimation(this, "arrowProgress", this);
    m_arrowAnim->setDuration(160);
    m_arrowAnim->setEasingCurve(QEasingCurve::OutCubic);
}

ImageDropdownSelector::~ImageDropdownSelector()
{
    disconnectScrollAreaPositionUpdates();
    qApp->removeEventFilter(this);
    if (m_popup) {
        m_popup->forceHide();
        m_popup->deleteLater();
    }
}

void ImageDropdownSelector::addItem(const ImageDropdownItem& item)
{
    m_items.append(item);
    if (m_currentIndex < 0 && item.enabled)
        m_currentIndex = m_items.size() - 1;
    if (isPopupActive())
        syncPopupItems();
    update();
}
void ImageDropdownSelector::clear()
{
    m_items.clear();
    m_currentIndex = -1;
    closePopup();
    update();
}
QString ImageDropdownSelector::currentText() const
{
    return (m_currentIndex >= 0 && m_currentIndex < m_items.size()) ? m_items[m_currentIndex].text
                                                                    : QString();
}
QVariant ImageDropdownSelector::currentData() const
{
    return itemData(m_currentIndex);
}
QVariant ImageDropdownSelector::itemData(int index) const
{
    return (index >= 0 && index < m_items.size()) ? m_items[index].userData : QVariant();
}
void ImageDropdownSelector::setPlaceholderText(const QString& text)
{
    m_placeholderText = text;
    update();
}
void ImageDropdownSelector::setPopupMinWidth(int width)
{
    m_popupMinWidth = qMax(180, width);
}
void ImageDropdownSelector::setPopupColumns(int columns)
{
    m_popupColumns = qMax(1, columns);
}
void ImageDropdownSelector::setPopupCardSize(const QSize& size)
{
    m_popupCardSize = QSize(qMax(96, size.width()), qMax(80, size.height()));
}
void ImageDropdownSelector::setPopupMaxHeight(int height)
{
    m_popupMaxHeight = qMax(kPopupMinVisibleHeight, height);
}
void ImageDropdownSelector::setHoverProgress(qreal progress)
{
    m_hoverProgress = qBound(0.0, progress, 1.0);
    update();
}
void ImageDropdownSelector::setPressProgress(qreal progress)
{
    m_pressProgress = qBound(0.0, progress, 1.0);
    update();
}
void ImageDropdownSelector::setArrowProgress(qreal progress)
{
    m_arrowProgress = qBound(0.0, progress, 1.0);
    update();
}

int ImageDropdownSelector::findIndexByData(const QVariant& userData) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].enabled && m_items[i].userData == userData)
            return i;
    return -1;
}

void ImageDropdownSelector::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size() || !m_items[index].enabled || m_currentIndex == index)
        return;
    m_currentIndex = index;
    if (isPopupActive())
        m_popup->setSelectedIndex(m_currentIndex);
    update();
}

void ImageDropdownSelector::clearCurrentSelection()
{
    if (m_currentIndex < 0) {
        return;
    }

    m_currentIndex = -1;
    if (isPopupActive()) {
        m_popup->setSelectedIndex(-1);
    }
    update();
}

void ImageDropdownSelector::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QRectF box = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(colors.overlayBase());
    p.drawRoundedRect(box, 8, 8);

    if (m_hoverProgress > 0.001) {
        QColor hover = colors.overlayHover();
        hover.setAlphaF(hover.alphaF() * m_hoverProgress * 0.2);
        p.setBrush(hover);
        p.drawRoundedRect(box, 8, 8);
    }
    if (m_pressProgress > 0.001) {
        QColor press = colors.overlay(0.12);
        press.setAlphaF(press.alphaF() * m_pressProgress);
        p.setBrush(press);
        p.drawRoundedRect(box, 8, 8);
    }

    const QColor topBorder = ruwa::ui::core::ThemeColors::interpolate(
        colors.borderSubtle(), colors.borderSubtleHover(), m_hoverProgress);
    QColor bottomBorder = colors.borderSubtle();
    bottomBorder.setAlpha(bottomBorder.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(p, box, 8, topBorder, bottomBorder);

    const bool hasValue = (m_currentIndex >= 0 && m_currentIndex < m_items.size());
    const QString text = hasValue ? m_items[m_currentIndex].text : m_placeholderText;
    const QColor textBase = hasValue ? colors.textMuted : colors.textDisabled();
    const QColor textColor = ruwa::ui::core::ThemeColors::interpolate(
        textBase, hasValue ? colors.text : colors.textMuted, m_hoverProgress);

    const QRectF previewRect(8, 5, 22, height() - 10);
    if (hasValue) {
        p.setBrush(colors.surfaceElevated());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(previewRect, 6, 6);
        const QImage preview = tintedPreview(m_items[m_currentIndex], textColor);
        if (!preview.isNull()) {
            const QImage scaled = preview.scaled(
                previewRect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QPointF origin(previewRect.center().x() - scaled.width() / 2.0,
                previewRect.center().y() - scaled.height() / 2.0);
            p.drawImage(origin, scaled);
        }
    }

    QFont f = font();
    f.setPointSize(9);
    p.setFont(f);
    p.setPen(textColor);
    p.drawText(QRect(hasValue ? 36 : 10, 0, width() - 58, height()),
        Qt::AlignVCenter | Qt::AlignLeft, text);

    p.save();
    p.translate(width() - 14, height() * 0.5);
    p.rotate(180.0 * m_arrowProgress);
    const QColor arrowColor = ruwa::ui::core::ThemeColors::interpolate(
        colors.textDisabled(), colors.text, qMin(1.0, m_hoverProgress + (m_pressProgress * 0.35)));
    p.setPen(QPen(arrowColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(-3.5, -1.0), QPointF(0.0, 2.2));
    p.drawLine(QPointF(0.0, 2.2), QPointF(3.5, -1.0));
    p.restore();
}

void ImageDropdownSelector::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    animateHoverTo(1.0);
}

void ImageDropdownSelector::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (!isPopupActive()) {
        animateHoverTo(0.0);
    }
}

void ImageDropdownSelector::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressAnim->stop();
        m_pressAnim->setStartValue(m_pressProgress);
        m_pressAnim->setEndValue(1.0);
        m_pressAnim->start();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ImageDropdownSelector::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressAnim->stop();
        m_pressAnim->setStartValue(m_pressProgress);
        m_pressAnim->setEndValue(0.0);
        m_pressAnim->start();
        if (rect().contains(event->pos())) {
            togglePopup();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ImageDropdownSelector::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        togglePopup();
        event->accept();
        return;
    case Qt::Key_Escape:
        if (isPopupActive()) {
            closePopup();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Right:
    case Qt::Key_Down: {
        const int delta = (event->key() == Qt::Key_Down && isPopupActive())
            ? qMax(1, m_popup->currentColumnCount())
            : 1;
        const int next = nextSelectableIndex(isPopupActive() && m_popup->hoveredIndex() >= 0
                ? m_popup->hoveredIndex()
                : m_currentIndex,
            delta);
        if (next >= 0 && next != m_currentIndex) {
            setCurrentIndex(next);
            emit currentIndexChanged(next);
        }
        event->accept();
        return;
    }
    case Qt::Key_Left:
    case Qt::Key_Up: {
        const int delta = (event->key() == Qt::Key_Up && isPopupActive())
            ? -qMax(1, m_popup->currentColumnCount())
            : -1;
        const int prev = nextSelectableIndex(m_currentIndex, delta);
        if (prev >= 0 && prev != m_currentIndex) {
            setCurrentIndex(prev);
            emit currentIndexChanged(prev);
        }
        event->accept();
        return;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void ImageDropdownSelector::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void ImageDropdownSelector::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    update();
}

bool ImageDropdownSelector::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (!isPopupActive()) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        const bool insideSelf = QRect(mapToGlobal(QPoint(0, 0)), size()).contains(globalPos);
        const bool insidePopup = m_popup && m_popup->containsGlobal(globalPos);
        if (!insideSelf && !insidePopup) {
            closePopup();
        }
    } else if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closePopup();
            return true;
        }
    } else if ((event->type() == QEvent::Resize || event->type() == QEvent::Move)
        && watched == window()) {
        updatePopupPosition();
    }

    return QWidget::eventFilter(watched, event);
}

void ImageDropdownSelector::openPopup()
{
    if (isPopupActive() || m_items.isEmpty()) {
        return;
    }

    if (m_currentIndex < 0) {
        m_currentIndex = firstSelectableIndex();
    }
    if (!m_popup->parentWidget() || m_popup->parentWidget() != window()) {
        m_popup->setParent(window());
    }

    syncPopupItems();
    updatePopupPosition();
    m_popup->showAnimated(m_popup->pos());
    animateArrowTo(1.0);
    animateHoverTo(1.0);
    connectScrollAreaPositionUpdates();
    qApp->installEventFilter(this);
    if (window()) {
        window()->installEventFilter(this);
    }
    emit popupShown();
}

void ImageDropdownSelector::closePopup()
{
    disconnectScrollAreaPositionUpdates();

    if (!isPopupActive()) {
        return;
    }

    if (window()) {
        window()->removeEventFilter(this);
    }
    qApp->removeEventFilter(this);
    m_popup->hideAnimated();
    animateArrowTo(0.0);
    if (!underMouse()) {
        animateHoverTo(0.0);
    }
}

void ImageDropdownSelector::togglePopup()
{
    if (!isEnabled()) {
        return;
    }
    if (isPopupActive()) {
        closePopup();
    } else {
        openPopup();
    }
}

void ImageDropdownSelector::syncPopupItems()
{
    m_popup->setItems(
        m_items, m_currentIndex, qMax(m_popupMinWidth, width()), m_popupColumns, m_popupCardSize);
    m_popup->setSelectedIndex(m_currentIndex);
}

void ImageDropdownSelector::updatePopupPosition()
{
    if (!m_popup || !window()) {
        return;
    }

    const QRect windowBounds = window()->rect().adjusted(8, 8, -8, -8);
    if (!windowBounds.isValid()) {
        return;
    }

    const QPoint belowAnchorGlobal = mapToGlobal(QPoint(0, height() + 2));
    const QPoint aboveAnchorGlobal = mapToGlobal(QPoint(0, -2));
    const QPoint belowAnchorLocal = window()->mapFromGlobal(belowAnchorGlobal);
    const QPoint aboveAnchorLocal = window()->mapFromGlobal(aboveAnchorGlobal);
    const int popupPreferredHeight = m_popup->preferredHeight();
    const int availableBelow = qMax(0, windowBounds.bottom() - belowAnchorLocal.y() + 1);
    const int availableAbove = qMax(0, aboveAnchorLocal.y() - windowBounds.top());
    const bool placeBelow
        = (availableBelow >= popupPreferredHeight) || (availableBelow >= availableAbove);
    const int availableHeight = placeBelow ? availableBelow : availableAbove;
    const int availableWidth = qMax(kPopupPadding * 2 + 1, windowBounds.width());
    const int constrainedHeight = qMax(kPopupMinVisibleHeight,
        qMin(popupPreferredHeight, qMin(availableHeight, m_popupMaxHeight)));
    m_popup->applySizeConstraints(availableWidth, constrainedHeight);

    QPoint target = placeBelow
        ? belowAnchorLocal
        : QPoint(aboveAnchorLocal.x(), aboveAnchorLocal.y() - m_popup->height());

    const int maxX = qMax(windowBounds.left(), windowBounds.right() - m_popup->width() + 1);
    target.setX(qBound(windowBounds.left(), target.x(), maxX));

    const int maxY = qMax(windowBounds.top(), windowBounds.bottom() - m_popup->height() + 1);
    target.setY(qBound(windowBounds.top(), target.y(), maxY));

    m_popup->move(target);
}

void ImageDropdownSelector::connectScrollAreaPositionUpdates()
{
    disconnectScrollAreaPositionUpdates();

    for (QWidget* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        auto* scrollArea = qobject_cast<SmoothScrollArea*>(ancestor);
        if (!scrollArea) {
            continue;
        }

        m_scrollAreaPositionConnections.append(
            connect(scrollArea, &SmoothScrollArea::scrolled, this, [this, scrollArea](int) {
                QWidget* viewport = scrollArea->viewport();
                if (!viewport) {
                    closePopup();
                    return;
                }

                const QRect anchorRect(viewport->mapFromGlobal(mapToGlobal(QPoint(0, 0))), size());
                if (!viewport->rect().intersects(anchorRect)) {
                    closePopup();
                    return;
                }

                updatePopupPosition();
            }));
    }
}

void ImageDropdownSelector::disconnectScrollAreaPositionUpdates()
{
    for (const QMetaObject::Connection& connection :
        std::as_const(m_scrollAreaPositionConnections)) {
        disconnect(connection);
    }
    m_scrollAreaPositionConnections.clear();
}

int ImageDropdownSelector::firstSelectableIndex() const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].enabled) {
            return i;
        }
    }
    return -1;
}

int ImageDropdownSelector::nextSelectableIndex(int from, int delta) const
{
    if (m_items.isEmpty() || delta == 0) {
        return -1;
    }

    const int start = qBound(0, from, m_items.size() - 1);
    int idx = start + delta;
    while (idx >= 0 && idx < m_items.size()) {
        if (m_items[idx].enabled) {
            return idx;
        }
        idx += (delta > 0) ? 1 : -1;
    }
    return -1;
}

void ImageDropdownSelector::animateHoverTo(qreal target)
{
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(target);
    m_hoverAnim->start();
}

void ImageDropdownSelector::animateArrowTo(qreal target)
{
    m_arrowAnim->stop();
    m_arrowAnim->setStartValue(m_arrowProgress);
    m_arrowAnim->setEndValue(target);
    m_arrowAnim->start();
}

bool ImageDropdownSelector::isPopupActive() const
{
    return m_popup && m_popup->isVisible();
}

} // namespace ruwa::ui::widgets
