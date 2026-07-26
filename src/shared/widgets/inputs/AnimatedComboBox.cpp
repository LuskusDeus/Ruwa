// SPDX-License-Identifier: MPL-2.0

#include "AnimatedComboBox.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/layout/FlowLayout.h"
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
#include <QtMath>
#include <functional>
#include <limits>
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
constexpr int kPreviewGridSpacing = 8;

Qt::WindowFlags popupWindowFlags()
{
    return Qt::Widget;
}

QImage tintedPreview(const AnimatedComboItem& item, const QColor& fallbackColor)
{
    if (item.previewImage.isNull()) {
        return {};
    }
    if (!item.tintPreview) {
        return item.previewImage;
    }

    QImage tinted(item.previewImage.size(), QImage::Format_ARGB32_Premultiplied);
    tinted.fill(Qt::transparent);
    QPainter painter(&tinted);
    painter.drawImage(0, 0, item.previewImage);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), item.previewTint.isValid() ? item.previewTint : fallbackColor);
    return tinted;
}
} // namespace

class ComboPopupCategoryWidget final : public QWidget {
public:
    explicit ComboPopupCategoryWidget(const QString& text, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_text(text)
    {
        setFixedHeight(18);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing);
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        painter.fillRect(rect(), colors.surfaceElevated());

        QColor textColor = colors.textMuted;
        textColor.setAlpha(180);
        painter.setPen(textColor);
        QFont categoryFont = font();
        categoryFont.setPointSize(8);
        categoryFont.setBold(true);
        painter.setFont(categoryFont);
        painter.drawText(
            rect().adjusted(10, 0, -10, 0), Qt::AlignLeft | Qt::AlignVCenter, m_text.toUpper());
    }

private:
    QString m_text;
};

class ComboPopupSeparatorWidget final : public QWidget {
public:
    explicit ComboPopupSeparatorWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(kSeparatorHeight);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_OpaquePaintEvent);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        painter.fillRect(rect(), colors.surfaceElevated());
        painter.fillRect(
            QRect(10, rect().center().y(), qMax(0, width() - 20), 1), colors.borderSubtle());
    }
};

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

class ComboPreviewCardButton final : public BaseAnimatedButton {
public:
    ComboPreviewCardButton(
        const AnimatedComboItem& item, int index, const QSize& cardSize, QWidget* parent = nullptr)
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
    void setOnHovered(std::function<void(int)> callback) { m_onHovered = std::move(callback); }

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

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF cardRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        QColor background = colors.overlayBase();
        if (hoverProgress() > 0.0) {
            background = ruwa::ui::core::ThemeColors::interpolate(
                background, colors.overlayHover(), hoverProgress() * 0.35);
        }
        if (isPressed()) {
            background
                = ruwa::ui::core::ThemeColors::interpolate(background, colors.overlay(0.18), 0.7);
        }
        if (activeProgress() > 0.0) {
            background = ruwa::ui::core::ThemeColors::interpolate(
                background, colors.overlay(0.18), activeProgress());
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(cardRect, 12, 12);

        const QColor topBorder = ruwa::ui::core::ThemeColors::interpolate(
            colors.borderSubtle(), colors.primary, qMax(hoverProgress() * 0.45, activeProgress()));
        QColor bottomBorder = colors.borderSubtle();
        bottomBorder.setAlpha(bottomBorder.alpha() / 2);
        ruwa::ui::painting::drawGradientBorder(painter, cardRect, 12, topBorder, bottomBorder);

        const int previewHeight = qMax(30, height() - 40);
        const QRectF previewRect = cardRect.adjusted(8, 8, -8, -(height() - previewHeight - 8));
        const QRectF labelRect(cardRect.left() + 10, previewRect.bottom() + 8,
            cardRect.width() - 20, cardRect.bottom() - previewRect.bottom() - 12);

        painter.setBrush(colors.surfaceElevated());
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(previewRect, 9, 9);

        const QImage preview = tintedPreview(m_item, colors.text);
        if (!preview.isNull()) {
            const QImage scaled = preview.scaled(
                previewRect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawImage(QPointF(previewRect.center().x() - scaled.width() / 2.0,
                                  previewRect.center().y() - scaled.height() / 2.0),
                scaled);
        }

        QFont titleFont = font();
        titleFont.setPixelSize(qMax(10, titleFont.pixelSize()));
        titleFont.setWeight(activeProgress() > 0.5 ? QFont::DemiBold : QFont::Medium);
        painter.setFont(titleFont);
        painter.setPen(isEnabled() ? colors.text : colors.textDisabled());
        painter.drawText(
            labelRect.toRect(), Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, m_item.text);

        if (!m_item.subtitle.isEmpty()) {
            QFont subtitleFont = titleFont;
            subtitleFont.setPixelSize(qMax(9, titleFont.pixelSize() - 1));
            subtitleFont.setWeight(QFont::Normal);
            painter.setFont(subtitleFont);
            painter.setPen(colors.textMuted);
            painter.drawText(labelRect.adjusted(0, 14, 0, 0).toRect(),
                Qt::AlignLeft | Qt::AlignTop | Qt::TextSingleLine, m_item.subtitle);
        }

        if (activeProgress() > 0.0) {
            painter.save();
            painter.setOpacity(activeProgress());
            painter.setBrush(colors.primary);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(QRectF(cardRect.right() - 18, cardRect.top() + 8, 10, 10));
            painter.restore();
        }
    }

private:
    AnimatedComboItem m_item;
    int m_index = -1;
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
        m_contentLayout->setAlignment(Qt::AlignTop);
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
        const QFont& comboFont, AnimatedComboBox::PopupPresentation presentation, int columns,
        const QSize& cardSize)
    {
        m_items = items;
        m_selectedIndex = selectedIndex;
        m_presentation = presentation;
        m_columns = qMax(1, columns);
        m_cardSize = cardSize;
        setHoveredIndex(-1);

        m_itemButtons.clear();
        m_previewButtons.clear();

        while (QLayoutItem* li = m_contentLayout->takeAt(0)) {
            if (QWidget* widget = li->widget()) {
                delete widget;
            }
            delete li;
        }

        if (m_presentation == AnimatedComboBox::PopupPresentation::PreviewGrid) {
            buildPreviewGrid(comboFont, minWidth);
        } else {
            buildList(comboFont, minWidth);
        }
        m_contentWidget->setFixedWidth(m_popupWidth - kPopupPadding * 2);
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
        for (ComboPreviewCardButton* button : std::as_const(m_previewButtons)) {
            button->setSelected(button->index() == index);
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

        auto applyGeometry = [this, targetHeight, viewportHeight](int popupWidth) {
            setFixedSize(popupWidth, targetHeight);
            if (m_scrollArea) {
                m_scrollArea->setFixedHeight(viewportHeight);
            }
            m_layout->invalidate();
            m_layout->activate();

            if (m_scrollArea) {
                m_scrollArea->refreshScrollGeometry();
                m_scrollArea->finishLayoutTransitions();
            }
        };

        // SmoothScrollArea correctly reserves its scrollbar inside the viewport.
        // Preview cards have a fixed column width, so the popup frame must include
        // that actual reserve to keep the requested number of columns unobstructed.
        applyGeometry(m_popupWidth);
        const int scrollBarReserve
            = m_scrollArea ? qCeil(m_scrollArea->scrollBarReserveExtent()) : 0;
        if (scrollBarReserve > 0) {
            applyGeometry(m_popupWidth + scrollBarReserve);
        }

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
    int currentColumnCount() const
    {
        return m_presentation == AnimatedComboBox::PopupPresentation::PreviewGrid ? m_columns : 1;
    }

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
    }

private:
    QWidget* addCategoryWidget(const QString& text)
    {
        auto* category = new ComboPopupCategoryWidget(text, m_contentWidget);
        m_contentLayout->addWidget(category);
        return category;
    }

    QWidget* addSeparatorWidget()
    {
        auto* separator = new ComboPopupSeparatorWidget(m_contentWidget);
        m_contentLayout->addWidget(separator);
        return separator;
    }

    void buildList(const QFont& comboFont, int minWidth)
    {
        m_contentLayout->setSpacing(0);
        QFontMetrics fontMetrics(comboFont);
        int widthHint = minWidth;
        int heightHint = 0;

        for (int i = 0; i < m_items.size(); ++i) {
            const AnimatedComboItem& item = m_items[i];
            if (item.separator) {
                addSeparatorWidget();
                heightHint += kSeparatorHeight;
                continue;
            }
            if (item.category) {
                addCategoryWidget(item.text);
                heightHint += 18;
                continue;
            }

            auto* button = new ComboPopupItemButton(item, i, m_contentWidget);
            button->setFont(comboFont);
            button->setSelected(i == m_selectedIndex);
            connect(button, &QPushButton::clicked, this, [this, i]() {
                if (m_onItemActivated) {
                    m_onItemActivated(i);
                }
            });
            connect(button, &QPushButton::pressed, this, [this, i]() { setHoveredIndex(i); });
            button->setOnHovered([this](int hovered) { setHoveredIndex(hovered); });
            m_contentLayout->addWidget(button);
            m_itemButtons.append(button);

            const int contentWidth
                = 20 + fontMetrics.horizontalAdvance(item.text) + (item.icon.isNull() ? 0 : 18);
            widthHint = qMax(widthHint, contentWidth + 28);
            heightHint += kRowHeight;
        }

        m_popupWidth = qMax(widthHint, minWidth);
        m_contentHeight = qMax(20, heightHint);
    }

    void buildPreviewGrid(const QFont& comboFont, int minWidth)
    {
        m_contentLayout->setSpacing(6);
        const int gridWidth
            = m_columns * m_cardSize.width() + qMax(0, m_columns - 1) * kPreviewGridSpacing;
        m_popupWidth = qMax(minWidth, gridWidth + kPopupPadding * 2);

        QWidget* section = nullptr;
        FlowLayout* flowLayout = nullptr;
        int sectionItemCount = 0;
        int contentBlockCount = 0;
        int contentHeight = 0;

        auto accountForBlock = [&contentBlockCount, &contentHeight](int height) {
            if (contentBlockCount > 0) {
                contentHeight += 6;
            }
            contentHeight += height;
            ++contentBlockCount;
        };
        auto finishSection = [this, &section, &flowLayout, &sectionItemCount, &accountForBlock]() {
            if (!section) {
                return;
            }

            const int rows = qMax(1, (sectionItemCount + m_columns - 1) / m_columns);
            const int sectionHeight
                = rows * m_cardSize.height() + qMax(0, rows - 1) * kPreviewGridSpacing;
            section->setFixedHeight(sectionHeight);
            accountForBlock(sectionHeight);
            section = nullptr;
            flowLayout = nullptr;
            sectionItemCount = 0;
        };
        auto beginSection = [this, gridWidth, &section, &flowLayout, &sectionItemCount]() {
            section = new QWidget(m_contentWidget);
            section->setAttribute(Qt::WA_TranslucentBackground);
            section->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            section->setFixedWidth(gridWidth);
            flowLayout = new FlowLayout(section, 0, kPreviewGridSpacing, kPreviewGridSpacing);
            m_contentLayout->addWidget(section, 0, Qt::AlignLeft | Qt::AlignTop);
            sectionItemCount = 0;
        };

        for (int i = 0; i < m_items.size(); ++i) {
            const AnimatedComboItem& item = m_items[i];
            if (item.separator) {
                finishSection();
                addSeparatorWidget();
                accountForBlock(kSeparatorHeight);
                continue;
            }
            if (item.category) {
                finishSection();
                addCategoryWidget(item.text);
                accountForBlock(18);
                continue;
            }
            if (!flowLayout) {
                beginSection();
            }

            auto* button = new ComboPreviewCardButton(item, i, m_cardSize, section);
            button->setFont(comboFont);
            button->setSelected(i == m_selectedIndex);
            connect(button, &QPushButton::clicked, this, [this, i]() {
                if (m_onItemActivated) {
                    m_onItemActivated(i);
                }
            });
            connect(button, &QPushButton::pressed, this, [this, i]() { setHoveredIndex(i); });
            button->setOnHovered([this](int hovered) { setHoveredIndex(hovered); });
            flowLayout->addWidget(button);
            m_previewButtons.append(button);
            ++sectionItemCount;
        }

        finishSection();
        m_contentLayout->activate();
        m_contentHeight = qMax(20, contentHeight);
    }

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

        QWidget* selectedButton = nullptr;
        for (ComboPopupItemButton* btn : std::as_const(m_itemButtons)) {
            if (btn->index() == m_selectedIndex) {
                selectedButton = btn;
                break;
            }
        }
        if (!selectedButton) {
            for (ComboPreviewCardButton* button : std::as_const(m_previewButtons)) {
                if (button->index() == m_selectedIndex) {
                    selectedButton = button;
                    break;
                }
            }
        }

        if (!selectedButton) {
            return;
        }

        const int top = selectedButton->mapTo(m_contentWidget, QPoint(0, 0)).y();
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
    QList<ComboPreviewCardButton*> m_previewButtons;
    QVBoxLayout* m_layout = nullptr;
    SmoothScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    int m_selectedIndex = -1;
    int m_hoveredIndex = -1;
    int m_popupWidth = 180;
    int m_contentHeight = 0;
    AnimatedComboBox::PopupPresentation m_presentation = AnimatedComboBox::PopupPresentation::List;
    int m_columns = 1;
    QSize m_cardSize;
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

void AnimatedComboBox::clearCurrentSelection()
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
    const int normalized = qMax(120, width);
    if (m_popupMinWidth == normalized) {
        return;
    }
    m_popupMinWidth = normalized;
    if (isPopupActive()) {
        syncPopupItems();
        updatePopupPosition();
    }
}

void AnimatedComboBox::setPopupMaxHeight(int height)
{
    m_popupMaxHeight = qMax(kPopupMinVisibleHeight, height);
    if (isPopupActive()) {
        updatePopupPosition();
    }
}

void AnimatedComboBox::setPopupPresentation(PopupPresentation presentation)
{
    if (m_popupPresentation == presentation) {
        return;
    }
    m_popupPresentation = presentation;
    if (isPopupActive()) {
        syncPopupItems();
        updatePopupPosition();
    }
    update();
}

void AnimatedComboBox::setPopupColumns(int columns)
{
    const int normalized = qMax(1, columns);
    if (m_popupColumns == normalized) {
        return;
    }
    m_popupColumns = normalized;
    if (isPopupActive()) {
        syncPopupItems();
        updatePopupPosition();
    }
}

void AnimatedComboBox::setPopupCardSize(const QSize& size)
{
    const QSize normalized(qMax(96, size.width()), qMax(80, size.height()));
    if (m_popupCardSize == normalized) {
        return;
    }
    m_popupCardSize = normalized;
    if (isPopupActive()) {
        syncPopupItems();
        updatePopupPosition();
    }
}

void AnimatedComboBox::setItemPreviewImage(int index, const QImage& image)
{
    if (index < 0 || index >= m_items.size() || m_items[index].previewImage == image) {
        return;
    }
    m_items[index].previewImage = image;
    if (isPopupActive()) {
        syncPopupItems();
        updatePopupPosition();
    }
    if (m_currentIndex == index) {
        update();
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

    int textLeft = 10;
    if (hasValue && m_popupPresentation == PopupPresentation::PreviewGrid) {
        const QRectF previewRect(8, 5, 22, height() - 10);
        p.setBrush(colors.surfaceElevated());
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(previewRect, 6, 6);
        const QImage preview = tintedPreview(m_items[m_currentIndex], textColor);
        if (!preview.isNull()) {
            const QImage scaled = preview.scaled(
                previewRect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            p.drawImage(QPointF(previewRect.center().x() - scaled.width() / 2.0,
                            previewRect.center().y() - scaled.height() / 2.0),
                scaled);
        }
        textLeft = 36;
    }

    p.setPen(textColor);
    QFont f = font();
    f.setPointSize(9);
    p.setFont(f);
    p.drawText(QRect(textLeft, 0, width() - textLeft - 22, height()),
        Qt::AlignVCenter | Qt::AlignLeft, text);

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
    case Qt::Key_Right:
    case Qt::Key_Down: {
        const int offset
            = event->key() == Qt::Key_Down && isPopupActive() ? m_popup->currentColumnCount() : 1;
        const int from = isPopupActive() && m_popup->hoveredIndex() >= 0 ? m_popup->hoveredIndex()
                                                                         : m_currentIndex;
        const int next = nextSelectableIndex(from, offset);
        if (next >= 0) {
            setCurrentIndex(next);
            emit currentIndexChanged(next);
        }
        event->accept();
        return;
    }
    case Qt::Key_Left:
    case Qt::Key_Up: {
        const int offset
            = event->key() == Qt::Key_Up && isPopupActive() ? -m_popup->currentColumnCount() : -1;
        const int from = isPopupActive() && m_popup->hoveredIndex() >= 0 ? m_popup->hoveredIndex()
                                                                         : m_currentIndex;
        const int prev = nextSelectableIndex(from, offset);
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
    int effectiveColumns = m_popupColumns;
    int availableWidth = std::numeric_limits<int>::max();
    if (window()) {
        availableWidth = qMax(1, window()->width() - 16);
    }
    if (m_popupPresentation == PopupPresentation::PreviewGrid) {
        while (effectiveColumns > 1
            && effectiveColumns * m_popupCardSize.width()
                    + (effectiveColumns - 1) * kPreviewGridSpacing + kPopupPadding * 2
                > availableWidth) {
            --effectiveColumns;
        }
    }
    const int popupMinimumWidth = m_popupPresentation == PopupPresentation::PreviewGrid
        ? qMin(m_popupMinWidth, availableWidth)
        : qMax(m_popupMinWidth, width());
    m_popup->setItems(m_items, m_currentIndex, popupMinimumWidth, font(), m_popupPresentation,
        effectiveColumns, m_popupCardSize);
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
    if (m_items.isEmpty() || direction == 0) {
        return -1;
    }

    QList<int> selectable;
    selectable.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        const AnimatedComboItem& item = m_items[i];
        if (!item.separator && !item.category && item.enabled) {
            selectable.append(i);
        }
    }
    if (selectable.isEmpty()) {
        return -1;
    }

    int position = selectable.indexOf(from);
    if (position < 0) {
        position = direction > 0 ? -1 : static_cast<int>(selectable.size());
    }
    position = qBound(0, position + direction, static_cast<int>(selectable.size()) - 1);
    return selectable[position];
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
