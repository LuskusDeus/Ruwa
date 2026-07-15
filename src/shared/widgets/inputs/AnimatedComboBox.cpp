// SPDX-License-Identifier: MPL-2.0

#include "AnimatedComboBox.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/style/PaintingUtils.h"

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
#include <QVariant>
#include <QStyleOption>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <functional>
#include <utility>

namespace ruwa::ui::widgets {

namespace {

constexpr int kRowHeight = 28;
constexpr int kSeparatorHeight = 8;
constexpr int kPopupPadding = 4;
constexpr int kShowDurationMs = 120;
constexpr int kHideDurationMs = 80;
constexpr int kSlideDurationMs = 200;
constexpr int kPopupOffset = 20;
constexpr int kCornerRadius = 8;
constexpr int kPopupMinVisibleHeight = 120;

Qt::WindowFlags popupWindowFlags()
{
    return Qt::Widget;
}
} // namespace

class ComboPopupItemButton final : public BaseAnimatedButton {
public:
    ComboPopupItemButton(const AnimatedComboItem& item, int index, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_item(item)
        , m_index(index)
    {
        setFixedHeight(kRowHeight);
        setEnabled(item.enabled);
        setCheckable(false);
        setCursor(item.enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    int index() const { return m_index; }
    void setSelected(bool selected)
    {
        if (m_selected == selected)
            return;
        m_selected = selected;
        update();
    }
    bool selected() const { return m_selected; }
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
        const QRectF r = rect().adjusted(2.0, 1.0, -2.0, -1.0);

        // Keep each row fully painted to avoid transparent-to-black artifacts
        // when the popup fades with QGraphicsOpacityEffect.
        p.setPen(Qt::NoPen);
        p.setBrush(colors.surfaceElevated());
        p.drawRoundedRect(r, 5, 5);

        if (hoverProgress() > 0.0) {
            QColor hover = colors.overlayHover();
            hover.setAlphaF(hover.alphaF() * hoverProgress());
            p.setPen(Qt::NoPen);
            p.setBrush(hover);
            p.drawRoundedRect(r, 5, 5);
        }
        if (isPressed()) {
            QColor press = colors.overlay(0.12);
            p.setPen(Qt::NoPen);
            p.setBrush(press);
            p.drawRoundedRect(r, 5, 5);
        }
        if (m_selected) {
            QColor active = colors.overlay(0.08);
            p.setPen(Qt::NoPen);
            p.setBrush(active);
            p.drawRoundedRect(r, 5, 5);
        }

        // Keep text color stable to avoid a darkening flash when hover fades out.
        QColor textColor = isEnabled() ? colors.text : colors.textDisabled();

        int x = 12;
        if (!m_item.icon.isNull()) {
            QPixmap src = m_item.icon.pixmap(14, 14);
            if (!src.isNull()) {
                QPixmap colored(src.size());
                colored.fill(Qt::transparent);
                QPainter iconPainter(&colored);
                iconPainter.drawPixmap(0, 0, src);
                iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                iconPainter.fillRect(colored.rect(), textColor);
                iconPainter.end();
                p.drawPixmap(x, (height() - 14) / 2, colored);
            }
            x += 18;
        }

        QFont f = font();
        f.setPointSize(9);
        p.setFont(f);
        p.setPen(textColor);
        p.drawText(
            QRect(x, 0, width() - x - 24, height()), Qt::AlignVCenter | Qt::AlignLeft, m_item.text);

        if (m_selected) {
            QPen checkPen(colors.primary, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(checkPen);
            const qreal cx = width() - 12;
            const qreal cy = height() * 0.5;
            p.drawLine(QPointF(cx - 3.3, cy), QPointF(cx - 1.1, cy + 2.7));
            p.drawLine(QPointF(cx - 1.1, cy + 2.7), QPointF(cx + 3.8, cy - 2.2));
        }
    }

private:
    AnimatedComboItem m_item;
    int m_index = -1;
    bool m_selected = false;
    std::function<void(int)> m_onHovered;
};

class AnimatedComboPopup final : public QWidget {
public:
    explicit AnimatedComboPopup(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setWindowFlags(popupWindowFlags());
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        hide();

        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(kPopupPadding, kPopupPadding, kPopupPadding, kPopupPadding);
        m_layout->setSpacing(0);

        m_scrollArea = new SmoothScrollArea(this);
        m_scrollArea->setFillBackground(false);
        m_scrollArea->setScrollBarTransparentTrack(true);
        m_scrollArea->setContentWidthFixedToViewport(true);
        m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_layout->addWidget(m_scrollArea);

        m_contentWidget = new QWidget();
        m_contentWidget->setAttribute(Qt::WA_TranslucentBackground);
        m_contentLayout = new QVBoxLayout(m_contentWidget);
        m_contentLayout->setContentsMargins(0, 0, 0, 0);
        m_contentLayout->setSpacing(0);
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

    void setItems(const QList<AnimatedComboItem>& items, int selectedIndex, int minWidth,
        const QFont& comboFont)
    {
        m_items = items;
        m_selectedIndex = selectedIndex;
        setHoveredIndex(-1);

        m_itemButtons.clear();
        m_separators.clear();
        m_categories.clear();

        while (QLayoutItem* li = m_contentLayout->takeAt(0)) {
            if (QWidget* widget = li->widget()) {
                delete widget;
            }
            delete li;
        }

        QFontMetrics fm(comboFont);
        int widthHint = minWidth;
        int heightHint = 0;

        for (int i = 0; i < m_items.size(); ++i) {
            const auto& item = m_items[i];
            if (item.separator) {
                auto* sep = new QWidget(m_contentWidget);
                sep->setFixedHeight(kSeparatorHeight);
                sep->setAttribute(Qt::WA_TransparentForMouseEvents);
                m_contentLayout->addWidget(sep);
                m_separators.append(sep);
                heightHint += kSeparatorHeight;
                continue;
            }
            if (item.category) {
                auto* category = new QWidget(m_contentWidget);
                category->setFixedHeight(18);
                category->setAttribute(Qt::WA_TransparentForMouseEvents);
                category->setProperty("comboCategoryText", item.text);
                m_contentLayout->addWidget(category);
                m_categories.append(category);
                heightHint += 18;
                continue;
            }

            auto* btn = new ComboPopupItemButton(item, i, m_contentWidget);
            btn->setFont(comboFont);
            btn->setSelected(i == m_selectedIndex);
            connect(btn, &QPushButton::clicked, this, [this, i]() {
                if (m_onItemActivated) {
                    m_onItemActivated(i);
                }
            });
            connect(btn, &QPushButton::pressed, this, [this, i]() { setHoveredIndex(i); });
            btn->setOnHovered([this](int hovered) { setHoveredIndex(hovered); });
            m_contentLayout->addWidget(btn);
            m_itemButtons.append(btn);

            int contentWidth = 20 + fm.horizontalAdvance(item.text) + (item.icon.isNull() ? 0 : 18);
            widthHint = qMax(widthHint, contentWidth + 28);
            heightHint += kRowHeight;
        }

        m_popupWidth = qMax(widthHint, minWidth);
        m_contentHeight = qMax(20, heightHint);
        applyHeightConstraint(m_contentHeight + kPopupPadding * 2);
    }

    void setOnItemActivated(std::function<void(int)> cb) { m_onItemActivated = std::move(cb); }
    void setOnPopupHidden(std::function<void()> cb) { m_onPopupHidden = std::move(cb); }
    void setOnHoveredIndexChanged(std::function<void(int)> cb)
    {
        m_onHoveredIndexChanged = std::move(cb);
    }

    void setSelectedIndex(int index)
    {
        m_selectedIndex = index;
        for (ComboPopupItemButton* btn : std::as_const(m_itemButtons)) {
            btn->setSelected(btn->index() == index);
        }
        ensureSelectedVisible();
        update();
    }

    int preferredHeight() const { return m_contentHeight + kPopupPadding * 2; }

    void applyHeightConstraint(int maxHeight)
    {
        const int targetHeight = qMax(
            kPopupPadding * 2 + 1, qMin(preferredHeight(), qMax(maxHeight, kPopupPadding * 2 + 1)));
        const int viewportHeight = qMax(1, targetHeight - kPopupPadding * 2);

        if (m_scrollArea) {
            m_scrollArea->setFixedHeight(viewportHeight);
            m_scrollArea->refreshScrollGeometry();
        }

        setFixedSize(m_popupWidth, targetHeight);
        ensureSelectedVisible();
    }

    void showAnimated(const QPoint& targetPos)
    {
        m_isVisible = true;
        m_isHiding = false;
        disconnect(m_opacityAnim, &QVariantAnimation::finished, this, nullptr);

        const QPoint startPos(targetPos.x(), targetPos.y() - kPopupOffset);
        m_posAnim->stop();
        move(startPos);
        show();
        raise();

        m_opacityAnim->stop();
        if (m_popupOpacity <= 0.0) {
            m_popupOpacity = 0.0;
            m_opacityEffect->setOpacity(0.0);
        }
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
        if (!m_isVisible || m_isHiding)
            return;
        m_isVisible = false;
        m_isHiding = true;

        m_posAnim->stop();
        m_opacityAnim->stop();
        disconnect(m_opacityAnim, &QVariantAnimation::finished, this, nullptr);
        connect(m_opacityAnim, &QVariantAnimation::finished, this, [this]() {
            m_isHiding = false;
            hide();
            if (m_onPopupHidden)
                m_onPopupHidden();
        });

        m_opacityAnim->setDuration(kHideDurationMs);
        m_opacityAnim->setStartValue(m_popupOpacity);
        m_opacityAnim->setEndValue(0.0);
        m_opacityAnim->start();

        const QPoint currentPos = pos();
        const QPoint hidePos(currentPos.x(), currentPos.y() - (kPopupOffset / 2));
        m_posAnim->setDuration(kHideDurationMs);
        m_posAnim->setStartValue(currentPos);
        m_posAnim->setEndValue(hidePos);
        m_posAnim->start();
    }

    void forceHide()
    {
        m_isVisible = false;
        m_isHiding = false;
        m_posAnim->stop();
        m_opacityAnim->stop();
        m_popupOpacity = 0.0;
        m_opacityEffect->setOpacity(0.0);
        hide();
    }

    bool containsGlobal(const QPoint& globalPos) const
    {
        return isVisible() && QRect(mapToGlobal(QPoint(0, 0)), size()).contains(globalPos);
    }

    int hoveredIndex() const { return m_hoveredIndex; }

protected:
    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        setHoveredIndex(-1);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);

        // BaseStyledPanel-like background/border to keep UI consistency.
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceElevated());
        painter.drawRoundedRect(rect, kCornerRadius, kCornerRadius);

        const QRectF borderRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath borderPath;
        borderPath.addRoundedRect(borderRect, kCornerRadius - 0.5, kCornerRadius - 0.5);

        // Border: BorderSubtle / BorderSubtleAlpha50 (like BaseStyledPanel, no hover on popup
        // frame)
        QLinearGradient gradient(borderRect.topLeft(), borderRect.bottomLeft());
        QColor topBorder = colors.borderSubtle();
        QColor bottomBorder = topBorder;
        bottomBorder.setAlpha(bottomBorder.alpha() / 2);
        gradient.setColorAt(0.0, topBorder);
        gradient.setColorAt(1.0, bottomBorder);

        QPen pen;
        pen.setBrush(gradient);
        pen.setWidthF(1.0);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(borderPath);

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.borderSubtle());
        for (QWidget* sep : std::as_const(m_separators)) {
            const QRect sr(sep->mapTo(this, QPoint(0, 0)), sep->size());
            painter.drawRect(QRect(sr.left() + 10, sr.center().y(), sr.width() - 20, 1));
        }

        QColor categoryColor = colors.textMuted;
        categoryColor.setAlpha(180);
        painter.setPen(categoryColor);
        QFont categoryFont = font();
        categoryFont.setPointSize(8);
        categoryFont.setBold(true);
        painter.setFont(categoryFont);
        for (QWidget* category : std::as_const(m_categories)) {
            const QRect cr(category->mapTo(this, QPoint(0, 0)), category->size());
            const QString text = category->property("comboCategoryText").toString();
            painter.drawText(QRect(cr.left() + 10, cr.top(), cr.width() - 20, cr.height()),
                Qt::AlignLeft | Qt::AlignVCenter, text.toUpper());
        }
    }

private:
    void setHoveredIndex(int index)
    {
        if (m_hoveredIndex == index) {
            return;
        }
        m_hoveredIndex = index;
        if (m_onHoveredIndexChanged) {
            m_onHoveredIndexChanged(m_hoveredIndex);
        }
    }

private:
    void ensureSelectedVisible()
    {
        if (!m_scrollArea) {
            return;
        }

        ComboPopupItemButton* selectedButton = nullptr;
        for (ComboPopupItemButton* btn : std::as_const(m_itemButtons)) {
            if (btn->index() == m_selectedIndex) {
                selectedButton = btn;
                break;
            }
        }

        if (!selectedButton) {
            return;
        }

        const int top = selectedButton->y();
        const int bottom = top + selectedButton->height();
        const int scrollValue = m_scrollArea->scrollValue();
        const int viewportHeight
            = m_scrollArea->viewport() ? m_scrollArea->viewport()->height() : 0;

        if (viewportHeight <= 0) {
            return;
        }

        if (top < scrollValue) {
            m_scrollArea->scrollTo(top, false);
        } else if (bottom > scrollValue + viewportHeight) {
            m_scrollArea->scrollTo(bottom - viewportHeight, false);
        }
    }

private:
    QList<AnimatedComboItem> m_items;
    QList<ComboPopupItemButton*> m_itemButtons;
    QList<QWidget*> m_separators;
    QList<QWidget*> m_categories;
    QVBoxLayout* m_layout = nullptr;
    SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    int m_popupWidth = 180;
    int m_contentHeight = 0;
    bool m_isVisible = false;
    bool m_isHiding = false;
    qreal m_popupOpacity = 0.0;

    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QVariantAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    std::function<void(int)> m_onItemActivated;
    std::function<void()> m_onPopupHidden;
    std::function<void(int)> m_onHoveredIndexChanged;
};

AnimatedComboBox::AnimatedComboBox(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setFixedHeight(24);
    setCursor(Qt::PointingHandCursor);

    m_popup = new AnimatedComboPopup(window());
    m_popup->setProperty("ruwa_owner_combo", QVariant::fromValue(static_cast<QWidget*>(this)));
    m_popup->hide();

    m_popup->setOnItemActivated([this](int index) {
        const bool changed = (m_currentIndex != index);
        setCurrentIndex(index);
        closePopup();
        emit activated(index);
        if (changed) {
            emit currentIndexChanged(index);
        }
    });
    m_popup->setOnHoveredIndexChanged([this](int index) { emit itemHovered(index); });
    m_popup->setOnPopupHidden([this]() { emit popupHidden(); });

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

AnimatedComboBox::~AnimatedComboBox()
{
    disconnectScrollAreaPositionUpdates();
    qApp->removeEventFilter(this);
    if (m_popup) {
        m_popup->forceHide();
        m_popup->deleteLater();
        m_popup = nullptr;
    }
}

void AnimatedComboBox::addItem(const QString& text, const QVariant& userData, const QIcon& icon)
{
    AnimatedComboItem item;
    item.text = text;
    item.userData = userData;
    item.icon = icon;
    addItem(item);
}

void AnimatedComboBox::addItem(const AnimatedComboItem& item)
{
    m_items.append(item);
    if (m_currentIndex < 0 && !item.separator && !item.category && item.enabled) {
        m_currentIndex = m_items.size() - 1;
    }
    if (isPopupActive()) {
        syncPopupItems();
    }
    update();
}

void AnimatedComboBox::addCategory(const QString& text)
{
    AnimatedComboItem item;
    item.text = text;
    item.enabled = false;
    item.category = true;
    m_items.append(item);
    if (isPopupActive()) {
        syncPopupItems();
    }
}

void AnimatedComboBox::addSeparator()
{
    AnimatedComboItem item;
    item.separator = true;
    item.enabled = false;
    m_items.append(item);
    if (isPopupActive()) {
        syncPopupItems();
    }
}

void AnimatedComboBox::clear()
{
    m_items.clear();
    m_currentIndex = -1;
    closePopup();
    update();
}

void AnimatedComboBox::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    const auto& item = m_items[index];
    if (item.separator || item.category || !item.enabled) {
        return;
    }
    if (m_currentIndex == index) {
        return;
    }
    m_currentIndex = index;
    if (isPopupActive()) {
        m_popup->setSelectedIndex(m_currentIndex);
    }
    update();
}

QString AnimatedComboBox::currentText() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return QString();
    }
    return m_items[m_currentIndex].text;
}

QVariant AnimatedComboBox::currentData() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_items.size()) {
        return QVariant();
    }
    return m_items[m_currentIndex].userData;
}

QVariant AnimatedComboBox::itemData(int index) const
{
    if (index < 0 || index >= m_items.size()) {
        return QVariant();
    }
    return m_items[index].userData;
}

int AnimatedComboBox::findIndexByData(const QVariant& userData) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];
        if (item.separator || item.category || !item.enabled) {
            continue;
        }
        if (item.userData == userData) {
            return i;
        }
    }
    return -1;
}

void AnimatedComboBox::setPlaceholderText(const QString& text)
{
    m_placeholderText = text;
    update();
}

void AnimatedComboBox::setPopupMinWidth(int width)
{
    m_popupMinWidth = qMax(120, width);
}

void AnimatedComboBox::setPopupMaxHeight(int height)
{
    m_popupMaxHeight = qMax(kPopupMinVisibleHeight, height);
    if (isPopupActive()) {
        updatePopupPosition();
    }
}

void AnimatedComboBox::setHoverProgress(qreal progress)
{
    const qreal value = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_hoverProgress, value)) {
        return;
    }
    m_hoverProgress = value;
    update();
}

void AnimatedComboBox::setPressProgress(qreal progress)
{
    const qreal value = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_pressProgress, value)) {
        return;
    }
    m_pressProgress = value;
    update();
}

void AnimatedComboBox::setArrowProgress(qreal progress)
{
    const qreal value = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_arrowProgress, value)) {
        return;
    }
    m_arrowProgress = value;
    update();
}

void AnimatedComboBox::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QRectF box = rect().adjusted(0.5, 0.5, -0.5, -0.5);

    // Base: overlayBase (like ChoiceButton)
    p.setPen(Qt::NoPen);
    p.setBrush(colors.overlayBase());
    p.drawRoundedRect(box, 6, 6);

    // Hover: very subtle brightening — base barely changes, border does the work (like typical
    // widgets)
    if (m_hoverProgress > 0.001) {
        QColor hover = colors.overlayHover();
        hover.setAlphaF(hover.alphaF() * m_hoverProgress * 0.2);
        p.setBrush(hover);
        p.drawRoundedRect(box, 6, 6);
    }
    if (m_pressProgress > 0.001) {
        QColor press = colors.overlay(0.12);
        press.setAlphaF(press.alphaF() * m_pressProgress);
        p.setBrush(press);
        p.drawRoundedRect(box, 6, 6);
    }

    // Border: BorderSubtle / BorderSubtleAlpha50, animate to BorderSubtleHover on hover (like
    // ChoiceButton)
    {
        QColor top = ruwa::ui::core::ThemeColors::interpolate(
            colors.borderSubtle(), colors.borderSubtleHover(), m_hoverProgress);
        QColor bottom = colors.borderSubtle();
        bottom.setAlpha(bottom.alpha() / 2); // BorderSubtleAlpha50
        ruwa::ui::painting::drawGradientBorder(p, box, 6, top, bottom);
    }

    QString text = m_currentIndex >= 0 && m_currentIndex < m_items.size()
        ? m_items[m_currentIndex].text
        : m_placeholderText;
    const bool hasValue = (m_currentIndex >= 0 && m_currentIndex < m_items.size());
    QColor textBase = hasValue ? colors.textMuted : colors.textDisabled();
    QColor textTarget = hasValue ? colors.text : colors.textMuted;
    QColor textColor
        = ruwa::ui::core::ThemeColors::interpolate(textBase, textTarget, m_hoverProgress);
    p.setPen(textColor);
    QFont f = font();
    f.setPointSize(9);
    p.setFont(f);
    p.drawText(QRect(10, 0, width() - 30, height()), Qt::AlignVCenter | Qt::AlignLeft, text);

    p.save();
    const qreal arrowX = width() - 13;
    const qreal arrowY = height() * 0.5;
    p.translate(arrowX, arrowY);
    p.rotate(180.0 * m_arrowProgress);
    QColor arrowColor = ruwa::ui::core::ThemeColors::interpolate(
        colors.textDisabled(), colors.text, qMin(1.0, m_hoverProgress + (m_pressProgress * 0.35)));
    p.setPen(QPen(arrowColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(-3.5, -1.0), QPointF(0.0, 2.2));
    p.drawLine(QPointF(0.0, 2.2), QPointF(3.5, -1.0));
    p.restore();
}

void AnimatedComboBox::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    animateHoverTo(1.0);
}

void AnimatedComboBox::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (!isPopupActive()) {
        animateHoverTo(0.0);
    }
}

void AnimatedComboBox::mousePressEvent(QMouseEvent* event)
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

void AnimatedComboBox::mouseReleaseEvent(QMouseEvent* event)
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

void AnimatedComboBox::keyPressEvent(QKeyEvent* event)
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
    case Qt::Key_Down: {
        const int next = isPopupActive()
            ? nextSelectableIndex(
                  m_popup->hoveredIndex() >= 0 ? m_popup->hoveredIndex() : m_currentIndex, +1)
            : nextSelectableIndex(m_currentIndex, +1);
        if (next >= 0) {
            setCurrentIndex(next);
            emit currentIndexChanged(next);
        }
        event->accept();
        return;
    }
    case Qt::Key_Up: {
        const int prev = nextSelectableIndex(m_currentIndex, -1);
        if (prev >= 0) {
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

void AnimatedComboBox::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void AnimatedComboBox::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    update();
}

bool AnimatedComboBox::eventFilter(QObject* watched, QEvent* event)
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
    } else if (event->type() == QEvent::ApplicationDeactivate
        || event->type() == QEvent::WindowDeactivate) {
        closePopup();
    } else if (event->type() == QEvent::ApplicationStateChange
        && QApplication::applicationState() != Qt::ApplicationActive) {
        closePopup();
    } else if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closePopup();
            return true;
        }
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Move) {
        if (window() && watched == window()) {
            updatePopupPosition();
        }
    }

    return QWidget::eventFilter(watched, event);
}

void AnimatedComboBox::openPopup()
{
    if (isPopupActive() || m_items.isEmpty()) {
        return;
    }

    if (m_currentIndex < 0) {
        m_currentIndex = firstSelectableIndex();
    }

    if (!m_popup->parentWidget() || m_popup->parentWidget() != window()) {
        m_popup->setParent(window(), popupWindowFlags());
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

void AnimatedComboBox::closePopup()
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

void AnimatedComboBox::togglePopup()
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

void AnimatedComboBox::syncPopupItems()
{
    m_popup->setItems(m_items, m_currentIndex, qMax(m_popupMinWidth, width()), font());
    m_popup->setSelectedIndex(m_currentIndex);
}

void AnimatedComboBox::updatePopupPosition()
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
    const int preferredVisibleHeight = qMin(popupPreferredHeight, m_popupMaxHeight);
    const int minimumVisibleHeight = qMin(preferredVisibleHeight, kPopupMinVisibleHeight);
    const int availableBelow = qMax(0, windowBounds.bottom() - belowAnchorLocal.y() + 1);
    const int availableAbove = qMax(0, aboveAnchorLocal.y() - windowBounds.top());
    const bool placeBelow
        = (availableBelow >= minimumVisibleHeight) || (availableBelow >= availableAbove);
    const int availableHeight = placeBelow ? availableBelow : availableAbove;
    const int constrainedHeight = qMax(kPopupPadding * 2 + 1,
        qMin(popupPreferredHeight,
            qMin(qMax(availableHeight, kPopupMinVisibleHeight), m_popupMaxHeight)));

    m_popup->applyHeightConstraint(constrainedHeight);

    QPoint target = placeBelow
        ? belowAnchorLocal
        : QPoint(aboveAnchorLocal.x(), aboveAnchorLocal.y() - m_popup->height());

    const int maxX = qMax(windowBounds.left(), windowBounds.right() - m_popup->width() + 1);
    target.setX(qBound(windowBounds.left(), target.x(), maxX));

    const int maxY = qMax(windowBounds.top(), windowBounds.bottom() - m_popup->height() + 1);
    target.setY(qBound(windowBounds.top(), target.y(), maxY));

    m_popup->move(target);
}

void AnimatedComboBox::connectScrollAreaPositionUpdates()
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

void AnimatedComboBox::disconnectScrollAreaPositionUpdates()
{
    for (const QMetaObject::Connection& connection :
        std::as_const(m_scrollAreaPositionConnections)) {
        disconnect(connection);
    }
    m_scrollAreaPositionConnections.clear();
}

int AnimatedComboBox::firstSelectableIndex() const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (!m_items[i].separator && !m_items[i].category && m_items[i].enabled) {
            return i;
        }
    }
    return -1;
}

int AnimatedComboBox::nextSelectableIndex(int from, int direction) const
{
    if (m_items.isEmpty() || (direction != -1 && direction != 1)) {
        return -1;
    }

    int idx = from;
    if (idx < 0 || idx >= m_items.size()) {
        idx = (direction > 0) ? -1 : m_items.size();
    }

    for (int step = 0; step < m_items.size(); ++step) {
        idx += direction;
        if (idx < 0)
            idx = m_items.size() - 1;
        if (idx >= m_items.size())
            idx = 0;
        const auto& item = m_items[idx];
        if (!item.separator && !item.category && item.enabled) {
            return idx;
        }
    }
    return -1;
}

void AnimatedComboBox::animateHoverTo(qreal target)
{
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(target);
    m_hoverAnim->start();
}

void AnimatedComboBox::animateArrowTo(qreal target)
{
    m_arrowAnim->stop();
    m_arrowAnim->setStartValue(m_arrowProgress);
    m_arrowAnim->setEndValue(target);
    m_arrowAnim->start();
}

bool AnimatedComboBox::isPopupActive() const
{
    return m_popup && m_popup->isVisible();
}

} // namespace ruwa::ui::widgets
