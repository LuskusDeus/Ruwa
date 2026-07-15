// SPDX-License-Identifier: MPL-2.0

#include "SegmentedOptionSelector.h"

#include "BaseAnimatedButton.h"
#include "BaseStyledPanel.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyle.h"
#include "features/theme/manager/ThemeManager.h"

#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QAbstractAnimation>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QTimer>

namespace ruwa::ui::widgets {

namespace {
constexpr int BASE_SELECTOR_HEIGHT = 32;
constexpr int BASE_TRACK_PADDING = 4;
constexpr int BASE_LAYOUT_SPACING = 2;
constexpr int BASE_ITEM_MIN_WIDTH = 32;
constexpr int BASE_ITEM_PADDING_X = 10;
constexpr int BASE_ICON_SIZE = 16;
constexpr int BASE_ICON_TEXT_GAP = 6;
constexpr int BASE_TEXT_SIZE = 9;
constexpr int BASE_ANIMATION_DURATION = 240;

ruwa::ui::core::WidgetStyle createTrackStyle()
{
    using namespace ruwa::ui::core;

    WidgetStyle style = WidgetStyle::panelStyle();
    style.name = QStringLiteral("SegmentedOptionSelectorTrack");
    style.metrics.fixedHeight = false;
    style.metrics.fixedWidth = false;
    style.metrics.baseCornerRadius = BASE_SELECTOR_HEIGHT / 2;
    style.background.color = ColorSource::OverlayBase;
    style.border.enabled = true;
    style.border.style = BorderStyle::VerticalGradient;
    style.border.topColor = ColorSource::BorderSubtle;
    style.border.bottomColor = ColorSource::BorderSubtleAlpha50;
    style.border.animateOnHover = false;
    style.hover.enabled = false;
    style.content.basePadding
        = QMargins(BASE_TRACK_PADDING, BASE_TRACK_PADDING, BASE_TRACK_PADDING, BASE_TRACK_PADDING);
    return style;
}

ruwa::ui::core::WidgetStyle createSelectionStyle()
{
    using namespace ruwa::ui::core;

    WidgetStyle style = WidgetStyle::panelStyle();
    style.name = QStringLiteral("SegmentedOptionSelectorSelection");
    style.metrics.fixedHeight = false;
    style.metrics.fixedWidth = false;
    style.metrics.baseCornerRadius = (BASE_SELECTOR_HEIGHT - BASE_TRACK_PADDING * 2) / 2;
    style.background.color = ColorSource::Primary;
    style.border.enabled = false;
    style.hover.enabled = false;
    style.content.basePadding = QMargins();
    return style;
}
} // namespace

class SegmentedOptionSelector::OptionButton : public BaseAnimatedButton {
public:
    OptionButton(const Option& option, DisplayMode mode, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_option(option)
        , m_displayMode(mode)
    {
        setCheckable(false);
        setFlat(true);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        updateScaledSizes();
    }

    void setOption(const Option& option)
    {
        m_option = option;
        update();
    }

    void setDisplayMode(DisplayMode mode)
    {
        if (m_displayMode == mode) {
            return;
        }

        m_displayMode = mode;
        updateGeometry();
        update();
    }

    void setSelected(bool selected)
    {
        if (m_selected == selected) {
            return;
        }

        m_selected = selected;
        setActive(selected);
        update();
    }

    void refreshScaledSizes()
    {
        updateScaledSizes();
        updateGeometry();
        update();
    }

    QSize sizeHint() const override
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const DisplayMode effectiveMode = resolvedMode();
        const int height = theme.scaled(BASE_SELECTOR_HEIGHT - BASE_TRACK_PADDING * 2);
        const int iconSize = theme.scaled(BASE_ICON_SIZE);
        const int spacing = theme.scaled(BASE_ICON_TEXT_GAP);
        const int horizontalPadding = theme.scaled(BASE_ITEM_PADDING_X);
        const int minWidth = theme.scaled(BASE_ITEM_MIN_WIDTH);

        int width = minWidth;
        if (effectiveMode == DisplayMode::TextOnly) {
            const QFontMetrics metrics(font());
            width
                = qMax(minWidth, metrics.horizontalAdvance(m_option.text) + horizontalPadding * 2);
        } else if (effectiveMode == DisplayMode::IconsWithText) {
            const QFontMetrics metrics(font());
            width = qMax(minWidth,
                metrics.horizontalAdvance(m_option.text) + iconSize + spacing
                    + horizontalPadding * 2);
        }

        return QSize(width, height);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);

        if (!m_selected && hoverProgress() > 0.05) {
            QColor hover = colors.surfaceElevated();
            hover.setAlpha(int(hoverProgress() * 100));
            painter.setPen(Qt::NoPen);
            painter.setBrush(hover);
            const qreal radius = qMax<qreal>(2.0, rect.height() * 0.5);
            painter.drawRoundedRect(rect, radius, radius);
        }

        QColor inactiveColor = ruwa::ui::core::ThemeColors::interpolate(
            colors.textMuted, colors.text, hoverProgress());
        QColor activeColor = colors.textOnPrimary();
        QColor contentColor = ruwa::ui::core::ThemeColors::interpolate(
            inactiveColor, activeColor, activeProgress());

        const DisplayMode mode = resolvedMode();
        const bool drawIcon = (mode == DisplayMode::IconsOnly || mode == DisplayMode::IconsWithText)
            && !m_option.icon.isNull();
        const bool drawText = (mode == DisplayMode::TextOnly || mode == DisplayMode::IconsWithText)
            && !m_option.text.isEmpty();

        if (!drawIcon && !drawText) {
            return;
        }

        QRect contentRect = rect.toRect();
        if (drawIcon && drawText) {
            QPixmap iconPixmap = tintIcon(m_option.icon, m_iconSize, contentColor);
            QFontMetrics metrics(font());
            const QString text
                = metrics.elidedText(m_option.text, Qt::ElideRight, contentRect.width());
            const int textWidth = metrics.horizontalAdvance(text);
            const int totalWidth = m_iconSize + m_iconTextGap + textWidth;
            const int startX = contentRect.left() + (contentRect.width() - totalWidth) / 2;
            const int centerY = contentRect.center().y();

            const QRect iconRect(startX, centerY - m_iconSize / 2, m_iconSize, m_iconSize);
            painter.drawPixmap(iconRect, iconPixmap);

            painter.setPen(contentColor);
            painter.setFont(font());
            QRect textRect(startX + m_iconSize + m_iconTextGap, contentRect.top(), textWidth,
                contentRect.height());
            painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text);
            return;
        }

        if (drawIcon) {
            QPixmap iconPixmap = tintIcon(m_option.icon, m_iconSize, contentColor);
            QRect iconRect((contentRect.width() - m_iconSize) / 2,
                (contentRect.height() - m_iconSize) / 2, m_iconSize, m_iconSize);
            iconRect.translate(contentRect.topLeft());
            painter.drawPixmap(iconRect, iconPixmap);
            return;
        }

        painter.setPen(contentColor);
        painter.setFont(font());
        painter.drawText(contentRect, Qt::AlignCenter, m_option.text);
    }

private:
    DisplayMode resolvedMode() const
    {
        if (m_displayMode != DisplayMode::Auto) {
            return m_displayMode;
        }

        const bool hasIcon = !m_option.icon.isNull();
        const bool hasText = !m_option.text.isEmpty();
        if (hasIcon && hasText) {
            return DisplayMode::IconsWithText;
        }
        if (hasIcon) {
            return DisplayMode::IconsOnly;
        }
        return DisplayMode::TextOnly;
    }

    QPixmap tintIcon(const QIcon& icon, int size, const QColor& tint) const
    {
        QPixmap source = icon.pixmap(size, size, QIcon::Normal);
        if (source.isNull()) {
            return {};
        }

        return ruwa::ui::painting::tintedPixmap(source, tint);
    }

    void updateScaledSizes()
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        QFont f = font();
        f.setPointSize(theme.scaledFontSize(BASE_TEXT_SIZE));
        setFont(f);

        m_iconSize = theme.scaled(BASE_ICON_SIZE);
        m_iconTextGap = theme.scaled(BASE_ICON_TEXT_GAP);
    }

private:
    Option m_option;
    DisplayMode m_displayMode { DisplayMode::Auto };
    bool m_selected { false };
    int m_iconSize { BASE_ICON_SIZE };
    int m_iconTextGap { BASE_ICON_TEXT_GAP };
};

SegmentedOptionSelector::SegmentedOptionSelector(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    updateScaledSizes();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &SegmentedOptionSelector::onThemeChanged);
}

SegmentedOptionSelector::SegmentedOptionSelector(const QVector<Option>& options, QWidget* parent)
    : SegmentedOptionSelector(parent)
{
    setOptions(options);
}

void SegmentedOptionSelector::setupUI()
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_backgroundPanel = new BaseStyledPanel(createTrackStyle(), this);
    m_backgroundPanel->setHoverEnabled(false);
    m_backgroundPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    mainLayout->addWidget(m_backgroundPanel);

    m_buttonsLayout = new QHBoxLayout(m_backgroundPanel);
    m_buttonsLayout->setContentsMargins(
        BASE_TRACK_PADDING, BASE_TRACK_PADDING, BASE_TRACK_PADDING, BASE_TRACK_PADDING);
    m_buttonsLayout->setSpacing(BASE_LAYOUT_SPACING);

    m_indicatorPanel = new BaseStyledPanel(createSelectionStyle(), m_backgroundPanel);
    m_indicatorPanel->setHoverEnabled(false);
    m_indicatorPanel->hide();

    m_indicatorAnimation = new QPropertyAnimation(m_indicatorPanel, "geometry", this);
    m_indicatorAnimation->setDuration(BASE_ANIMATION_DURATION);
    m_indicatorAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_indicatorPanel->lower();
}

void SegmentedOptionSelector::setOptions(const QVector<Option>& options)
{
    m_options = options;
    if (m_options.isEmpty()) {
        m_currentIndex = -1;
    } else if (m_currentIndex < 0 || m_currentIndex >= m_options.size()) {
        m_currentIndex = 0;
    }

    rebuildButtons();
    updateGeometry();
}

int SegmentedOptionSelector::addOption(const QString& text, const QIcon& icon, const QVariant& data)
{
    Option option;
    option.text = text;
    option.icon = icon;
    option.data = data;
    m_options.append(option);

    if (m_currentIndex < 0) {
        m_currentIndex = 0;
    }

    rebuildButtons();
    updateGeometry();
    return m_options.size() - 1;
}

void SegmentedOptionSelector::clearOptions()
{
    m_options.clear();
    m_currentIndex = -1;
    rebuildButtons();
    updateGeometry();
}

void SegmentedOptionSelector::setCurrentIndex(int index, bool animated)
{
    if (index < 0 || index >= m_options.size()) {
        return;
    }

    if (m_currentIndex == index) {
        refreshButtonStates();
        // Re-sync geometry when idle; don't stop a running indicator animation (callers may echo
        // the same index).
        if (!m_indicatorAnimation || m_indicatorAnimation->state() != QAbstractAnimation::Running) {
            updateIndicatorGeometry(false);
        }
        return;
    }

    m_currentIndex = index;
    refreshButtonStates();
    updateIndicatorGeometry(animated);
    emit selectionChanged(index);
}

QVariant SegmentedOptionSelector::currentData() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_options.size()) {
        return {};
    }
    return m_options[m_currentIndex].data;
}

void SegmentedOptionSelector::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode == mode) {
        return;
    }

    m_displayMode = mode;
    for (auto* button : m_buttons) {
        if (button) {
            button->setDisplayMode(mode);
            button->updateGeometry();
        }
    }
    updateGeometry();
}

void SegmentedOptionSelector::setOptionText(int index, const QString& text)
{
    if (index < 0 || index >= m_options.size()) {
        return;
    }

    m_options[index].text = text;
    if (index < m_buttons.size() && m_buttons[index]) {
        m_buttons[index]->setOption(m_options[index]);
        m_buttons[index]->updateGeometry();
    }
    updateGeometry();
}

void SegmentedOptionSelector::setOptionIcon(int index, const QIcon& icon)
{
    if (index < 0 || index >= m_options.size()) {
        return;
    }

    m_options[index].icon = icon;
    if (index < m_buttons.size() && m_buttons[index]) {
        m_buttons[index]->setOption(m_options[index]);
    }
}

void SegmentedOptionSelector::setOptionData(int index, const QVariant& data)
{
    if (index < 0 || index >= m_options.size()) {
        return;
    }

    m_options[index].data = data;
}

QSize SegmentedOptionSelector::sizeHint() const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int height = theme.scaled(BASE_SELECTOR_HEIGHT);
    if (m_buttons.isEmpty()) {
        return QSize(theme.scaled(96), height);
    }

    const QMargins padding = scaledTrackPadding();
    int width = padding.left() + padding.right();
    width += theme.scaled(BASE_LAYOUT_SPACING) * (m_buttons.size() - 1);
    for (const auto* button : m_buttons) {
        if (button) {
            width += button->sizeHint().width();
        }
    }

    return QSize(width, height);
}

void SegmentedOptionSelector::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateIndicatorGeometry(false);
}

void SegmentedOptionSelector::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    // Ensure the indicator uses final geometry after the widget is shown in layouts.
    QTimer::singleShot(0, this, [this]() { updateIndicatorGeometry(false); });
}

void SegmentedOptionSelector::onThemeChanged()
{
    updateScaledSizes();
    updateIndicatorGeometry(false);
}

void SegmentedOptionSelector::rebuildButtons()
{
    if (!m_buttonsLayout) {
        return;
    }

    if (m_indicatorAnimation) {
        m_indicatorAnimation->stop();
    }

    QLayoutItem* child = nullptr;
    while ((child = m_buttonsLayout->takeAt(0)) != nullptr) {
        if (QWidget* w = child->widget()) {
            w->deleteLater();
        }
        delete child;
    }

    m_buttons.clear();

    for (int i = 0; i < m_options.size(); ++i) {
        auto* button = new OptionButton(m_options[i], m_displayMode, m_backgroundPanel);
        connect(button, &QPushButton::clicked, this, [this, i]() { setCurrentIndex(i, true); });
        m_buttonsLayout->addWidget(button);
        m_buttons.append(button);
    }

    refreshButtonStates();
    updateScaledSizes();
    QTimer::singleShot(0, this, [this]() { updateIndicatorGeometry(false); });
}

void SegmentedOptionSelector::refreshButtonStates()
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        if (m_buttons[i]) {
            m_buttons[i]->setSelected(i == m_currentIndex);
        }
    }
}

void SegmentedOptionSelector::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    setFixedHeight(theme.scaled(BASE_SELECTOR_HEIGHT));
    if (m_backgroundPanel) {
        m_backgroundPanel->setFixedHeight(theme.scaled(BASE_SELECTOR_HEIGHT));
        m_backgroundPanel->applyStyleChanges();
    }

    if (m_buttonsLayout) {
        const QMargins padding = scaledTrackPadding();
        m_buttonsLayout->setContentsMargins(padding);
        m_buttonsLayout->setSpacing(theme.scaled(BASE_LAYOUT_SPACING));
    }

    for (auto* button : m_buttons) {
        if (button) {
            button->refreshScaledSizes();
        }
    }

    if (m_indicatorAnimation) {
        m_indicatorAnimation->setDuration(theme.scaled(BASE_ANIMATION_DURATION));
    }

    if (m_indicatorPanel) {
        m_indicatorPanel->applyStyleChanges();
    }
}

void SegmentedOptionSelector::updateIndicatorGeometry(bool animated)
{
    if (!m_indicatorPanel || m_currentIndex < 0 || m_currentIndex >= m_buttons.size()
        || m_buttons.isEmpty()) {
        if (m_indicatorPanel) {
            m_indicatorPanel->hide();
        }
        return;
    }

    if (m_backgroundPanel && m_backgroundPanel->layout()) {
        m_backgroundPanel->layout()->activate();
    }

    const QRect targetRect = indicatorTargetRectForIndex(m_currentIndex);
    if (!targetRect.isValid() || targetRect.width() <= 0 || targetRect.height() <= 0) {
        m_indicatorPanel->hide();
        return;
    }

    if (m_indicatorAnimation) {
        m_indicatorAnimation->stop();
    }

    if (animated && m_indicatorPanel->isVisible()) {
        m_indicatorAnimation->setStartValue(m_indicatorPanel->geometry());
        m_indicatorAnimation->setEndValue(targetRect);
        m_indicatorAnimation->start();
    } else {
        m_indicatorPanel->setGeometry(targetRect);
    }

    m_indicatorPanel->show();
    m_indicatorPanel->lower();
    for (auto* button : m_buttons) {
        if (button) {
            button->raise();
        }
    }
}

QRect SegmentedOptionSelector::indicatorTargetRectForIndex(int index) const
{
    if (index < 0 || index >= m_buttons.size() || !m_buttons[index] || !m_backgroundPanel) {
        return {};
    }

    return m_buttons[index]->geometry();
}

QMargins SegmentedOptionSelector::scaledTrackPadding() const
{
    const QMargins base = m_backgroundPanel
        ? m_backgroundPanel->style().content.basePadding
        : QMargins(BASE_TRACK_PADDING, BASE_TRACK_PADDING, BASE_TRACK_PADDING, BASE_TRACK_PADDING);
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    return QMargins(theme.scaled(base.left()), theme.scaled(base.top()), theme.scaled(base.right()),
        theme.scaled(base.bottom()));
}

} // namespace ruwa::ui::widgets
