// SPDX-License-Identifier: MPL-2.0

// ColorPanel.cpp
#include "ColorPanel.h"
#include "features/color/ColorChannelSlidersWidget.h"
#include "features/color/ColorPicker.h"
#include "features/color/RecentColorsPersistence.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/theme/manager/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QStyleOption>
#include <QStringList>
#include <QtMath>
#include <utility>

namespace ruwa::ui::workspace {

namespace {

QString colorDebugString(const QColor& color)
{
    return QStringLiteral("%1(0x%2)")
        .arg(color.name(QColor::HexArgb),
            QString::number(color.rgba(), 16).rightJustified(8, QLatin1Char('0')));
}

QString panelStateDebugString(const ColorPanel* panel)
{
    return QStringLiteral("{fg=%1 bg=%2 slot=%3}")
        .arg(colorDebugString(panel->foregroundColor()), colorDebugString(panel->backgroundColor()),
            panel->isEditingForeground() ? QStringLiteral("fg") : QStringLiteral("bg"));
}

} // namespace

// === ColorPreviewWidget implementation ===
ColorPreviewWidget::ColorPreviewWidget(bool isForeground, QWidget* parent)
    : QWidget(parent)
    , m_isForeground(isForeground)
{
    setFixedSize(40, 40);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ColorPreviewWidget::setColor(const QColor& color)
{
    if (m_color != color) {
        m_color = color;
        update();
    }
}

void ColorPreviewWidget::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        update();
    }
}

void ColorPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Шахматный фон для прозрачности
    if (m_color.alpha() < 255) {
        drawCheckerboard(painter, rect());
    }

    // Цветной прямоугольник
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_color);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);

    // Граница
    const auto& colors = ruwa::ui::core::WidgetStyleManager::instance().colors();
    QColor borderColor = m_active ? colors.primary : colors.border;
    int borderWidth = m_active ? 2 : 1;

    painter.setPen(QPen(borderColor, borderWidth));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4, 4);

    // Метка FG/BG
    if (m_isForeground) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "FG");
    } else {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignCenter, "BG");
    }
}

void ColorPreviewWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_isForeground);
    }
    QWidget::mousePressEvent(event);
}

void ColorPreviewWidget::drawCheckerboard(QPainter& painter, const QRect& rect)
{
    const int size = 8;
    QColor dark(128, 128, 128);
    QColor light(192, 192, 192);

    for (int y = rect.top(); y < rect.bottom(); y += size) {
        for (int x = rect.left(); x < rect.right(); x += size) {
            bool isDark = ((x / size) % 2) != ((y / size) % 2);
            painter.fillRect(x, y, size, size, isDark ? dark : light);
        }
    }
}

// === ColorPanel implementation ===
ColorPanel::ColorPanel(QWidget* parent)
    : DockPanel(tr("Color"), parent)
{
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::ColorPanel);
    setMinimumPanelSize(170, 170);
    setPreferredPanelSize(320, 240);
    setClosable(true);
    setFloatable(true);
    setMovable(true);
    setTitleInteractiveWidgetsVisibleWhenFloating(true);
}

ColorPanel::~ColorPanel() = default;

void ColorPanel::setForegroundColor(const QColor& color)
{
    const bool changed = (m_foregroundColor != color);
    m_foregroundColor = color;
    updateColorPreview();

    if (m_colorPicker && !m_updating) {
        m_updating = true;
        m_colorPicker->setForegroundColor(color);
        m_updating = false;
    }

    if (changed) {
        emit foregroundColorChanged(color);
    }
    if (m_editingForeground) {
        emit activeColorChanged(color);
    }
}

void ColorPanel::setBackgroundColor(const QColor& color)
{
    if (m_backgroundColor == color)
        return;

    m_backgroundColor = color;
    updateColorPreview();

    if (m_colorPicker && !m_updating) {
        m_updating = true;
        m_colorPicker->setBackgroundColor(color);
        m_updating = false;
    }

    emit backgroundColorChanged(color);
    if (!m_editingForeground) {
        emit activeColorChanged(color);
    }
}

void ColorPanel::applyColorState(
    const QColor& foreground, const QColor& background, bool isForeground)
{
    m_foregroundColor = foreground;
    m_backgroundColor = background;
    m_editingForeground = isForeground;

    if (!m_colorPicker) {
        return;
    }

    const QSignalBlocker blocker(m_colorPicker);
    const bool wasUpdating = m_updating;
    m_updating = true;
    m_colorPicker->setActiveColorSlot(m_editingForeground);
    m_colorPicker->setForegroundColor(m_foregroundColor);
    m_colorPicker->setBackgroundColor(m_backgroundColor);
    m_updating = wasUpdating;
    updateChannelSliders();
}

void ColorPanel::setActiveColorSlot(bool isForeground)
{
    if (m_editingForeground == isForeground) {
        return;
    }

    m_editingForeground = isForeground;
    updatePickerFromCurrentColor();
    emit activeColorSlotChanged(m_editingForeground);
    emit activeColorChanged(activeColor());
}

void ColorPanel::swapForegroundBackgroundColors()
{
    std::swap(m_foregroundColor, m_backgroundColor);
    updateColorPreview();
    emit foregroundColorChanged(m_foregroundColor);
    emit backgroundColorChanged(m_backgroundColor);
    emit activeColorChanged(activeColor());
}

void ColorPanel::resetForegroundBackgroundColors()
{
    const QColor foreground = Qt::black;
    const QColor background = Qt::white;
    const bool foregroundChanged = (m_foregroundColor != foreground);
    const bool backgroundChanged = (m_backgroundColor != background);

    m_foregroundColor = foreground;
    m_backgroundColor = background;
    updateColorPreview();

    if (foregroundChanged) {
        emit foregroundColorChanged(m_foregroundColor);
    }
    if (backgroundChanged) {
        emit backgroundColorChanged(m_backgroundColor);
    }
    if (foregroundChanged || backgroundChanged) {
        emit activeColorChanged(activeColor());
    }
}

QWidget* ColorPanel::createContent()
{
    m_contentWidget = new QWidget();
    // Контент растягивается по ширине и высоте, чтобы заполнить панель
    m_contentWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto& theme = ruwa::ui::core::ThemeManager::instance();

    // Keep the picker and channel controls in one minimum-sized vertical flow.
    // If the panel is shorter than the flow, the outer content widget clips its
    // bottom instead of moving the fixed channel stack over the picker.
    auto* viewportLayout = new QVBoxLayout(m_contentWidget);
    viewportLayout->setContentsMargins(0, 0, 0, 0);
    viewportLayout->setSpacing(0);

    auto* flowContent = new QWidget(m_contentWidget);
    flowContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* layout = new QVBoxLayout(flowContent);
    layout->setContentsMargins(theme.scaled(4), theme.scaled(4), theme.scaled(4), theme.scaled(4));
    layout->setSpacing(theme.scaled(6));
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    viewportLayout->addWidget(flowContent);

    // === Integrated ColorPicker (embedded mode): stretches with panel size ===
    m_colorPicker = new ruwa::ui::widgets::ColorPicker(flowContent);
    m_colorPicker->setEmbeddedMode(true);
    m_colorPicker->setDualColorModeEnabled(true);
    m_colorPicker->setRecentColorsEnabled(true);
    m_colorPicker->setModeSwitcherVisible(false); // hosted in interactive title
    m_colorPicker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_colorPicker->setMinimumHeight(theme.scaled(140));
    m_colorPicker->setPickerMode(
        static_cast<ruwa::ui::widgets::ColorPicker::PickerMode>(m_pickerMode));
    m_colorPicker->setForegroundColor(m_foregroundColor);
    m_colorPicker->setBackgroundColor(m_backgroundColor);
    m_colorPicker->setActiveColorSlot(m_editingForeground);
    connect(m_colorPicker, &ruwa::ui::widgets::ColorPicker::colorChanged, this,
        &ColorPanel::onColorPicked);
    connect(m_colorPicker, &ruwa::ui::widgets::ColorPicker::recentColorsChanged, this,
        &ColorPanel::recentColorsChanged);
    m_colorPicker->setRecentColors(RecentColorsPersistence::load());
    connect(m_colorPicker, &ruwa::ui::widgets::ColorPicker::pickerModeChanged, this,
        [this](ruwa::ui::widgets::ColorPicker::PickerMode mode) {
            m_pickerMode = static_cast<int>(mode);
            updateModeSwitcherButtons();
            emit pickerModeChanged(m_pickerMode);
        });
    connect(m_colorPicker, &ruwa::ui::widgets::ColorPicker::activeColorSlotChanged, this,
        &ColorPanel::setActiveColorSlot);
    connect(m_colorPicker, &ruwa::ui::widgets::ColorPicker::swapColorsRequested, this,
        &ColorPanel::swapForegroundBackgroundColors);
    layout->addWidget(m_colorPicker, 1);

    // Independent channel controls below the picker.
    setupChannelSection(layout);

    // Mode switcher in dock title bar
    setupModeSwitcher();

    // Carry over the grayscale (mask edit) state if it was set before the content
    // existed (e.g. the panel was re-docked while a mask was the paint target).
    if (m_maskEditMode) {
        m_colorPicker->setMaskEditMode(true);
    }

    updateColorPreview();
    updateStyles();

    return m_contentWidget;
}

void ColorPanel::onColorPicked(const QColor& newColor)
{
    if (m_updating) {
        return;
    }

    QColor finalColor = newColor;
    // Если новый цвет имеет alpha < 255 (напр. из недавних) — используем его альфу.
    // Иначе (выбор в SV-квадрате, hue) сохраняем текущую альфу — там нет контроля прозрачности.
    if (newColor.alpha() >= 255) {
        const int alpha
            = m_editingForeground ? m_foregroundColor.alpha() : m_backgroundColor.alpha();
        finalColor.setAlpha(alpha);
    }

    if (m_editingForeground) {
        setForegroundColor(finalColor);
    } else {
        setBackgroundColor(finalColor);
    }
}

void ColorPanel::addColorToRecent(const QColor& color)
{
    if (m_colorPicker) {
        m_colorPicker->addColorToRecent(color);
    }
}

void ColorPanel::setRecentColors(const QVector<QColor>& colors)
{
    if (m_colorPicker) {
        m_colorPicker->setRecentColors(colors);
    }
}

QVector<QColor> ColorPanel::recentColors() const
{
    return m_colorPicker ? m_colorPicker->recentColors() : QVector<QColor>();
}

void ColorPanel::setMaskEditMode(bool active)
{
    if (m_maskEditMode == active) {
        return;
    }
    m_maskEditMode = active;
    if (m_colorPicker) {
        m_colorPicker->setMaskEditMode(active);
    }
}

void ColorPanel::updateColorPreview()
{
    if (m_colorPicker) {
        const QSignalBlocker blocker(m_colorPicker);
        const bool wasUpdating = m_updating;
        m_updating = true;
        m_colorPicker->setForegroundColor(m_foregroundColor);
        m_colorPicker->setBackgroundColor(m_backgroundColor);
        m_colorPicker->setActiveColorSlot(m_editingForeground);
        m_updating = wasUpdating;
    }

    updateChannelSliders();
}

void ColorPanel::updatePickerFromCurrentColor()
{
    if (m_updating) {
        return;
    }

    if (m_colorPicker) {
        m_updating = true;
        const QColor color = activeColor();
        m_colorPicker->setActiveColorSlot(m_editingForeground);
        m_colorPicker->setColor(color);
        m_updating = false;
    }

    updateChannelSliders();
}

void ColorPanel::setupChannelSection(QVBoxLayout* parentLayout)
{
    if (!parentLayout) {
        return;
    }

    m_channelStack
        = new ruwa::ui::widgets::AnimatedStackedWidget(parentLayout->parentWidget());
    m_channelStack->setObjectName(QStringLiteral("colorChannelStack"));
    m_channelStack->setSlideOrientation(
        ruwa::ui::widgets::AnimatedStackedWidget::SlideOrientation::Horizontal);
    m_channelStack->setAnimationDuration(250);
    m_channelStack->setSuspendLayoutDuringAnimation(true);
    m_channelStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Stack indices intentionally match m_channelMode: HSV = 0, RGB = 1.
    m_channelStack->addWidget(createChannelWidget(false));
    m_channelStack->addWidget(createChannelWidget(true));
    m_channelStack->setFixedHeight(qMax(
        m_hsvChannelSliders->sizeHint().height(), m_rgbChannelSliders->sizeHint().height()));
    m_channelStack->setCurrentIndexWithoutAnimation(m_channelMode);

    parentLayout->addWidget(m_channelStack);
    updateChannelSliders();
}

QWidget* ColorPanel::createChannelWidget(bool rgbMode)
{
    using ChannelSliders = ruwa::ui::widgets::ColorChannelSlidersWidget;
    const auto model = rgbMode ? ChannelSliders::Model::RGB : ChannelSliders::Model::HSV;
    auto* sliders = new ChannelSliders(model, m_channelStack);
    sliders->setObjectName(
        rgbMode ? QStringLiteral("rgbChannelSliders") : QStringLiteral("hsvChannelSliders"));
    sliders->setAccessibleName(rgbMode ? tr("RGB color channels") : tr("HSV color channels"));
    sliders->setToolTip(sliders->accessibleName());

    connect(sliders, &ChannelSliders::colorChanged, this, [this](const QColor& color) {
        if (!m_updating) {
            onColorPicked(color);
        }
    });

    if (rgbMode) {
        m_rgbChannelSliders = sliders;
    } else {
        m_hsvChannelSliders = sliders;
    }
    return sliders;
}

void ColorPanel::setChannelMode(int mode)
{
    if (mode < 0 || mode > 1) {
        return;
    }

    const bool changed = m_channelMode != mode;
    m_channelMode = mode;
    if (m_channelStack && m_channelStack->activeIndex() != mode) {
        m_channelStack->setCurrentIndex(mode);
    }
    updateChannelModeButtons();
    if (changed) {
        emit channelModeChanged(m_channelMode);
    }
}

void ColorPanel::updateChannelModeButtons()
{
    for (int i = 0; i < m_channelModeButtons.size(); ++i) {
        m_channelModeButtons[i]->setChecked(i == m_channelMode);
        m_channelModeButtons[i]->update();
    }
}

void ColorPanel::updateChannelSliders()
{
    if (!m_rgbChannelSliders || !m_hsvChannelSliders) {
        return;
    }

    const QColor color = activeColor();
    const QSignalBlocker rgbBlocker(m_rgbChannelSliders);
    const QSignalBlocker hsvBlocker(m_hsvChannelSliders);
    m_rgbChannelSliders->setColor(color);
    m_hsvChannelSliders->setColor(color);
}

void ColorPanel::updateStyles()
{
    if (!m_contentWidget)
        return;
    const auto& c = colors();

    // Цвет фона контента
    m_contentWidget->setStyleSheet(QString("background: %1;").arg(c.surface.name()));
}

void ColorPanel::onThemeChanged()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    if (m_colorPicker && m_colorPicker->isEmbeddedMode()) {
        m_colorPicker->setMinimumHeight(theme.scaled(140));
    }
    if (m_channelStack && m_rgbChannelSliders && m_hsvChannelSliders) {
        m_channelStack->setFixedHeight(qMax(m_hsvChannelSliders->sizeHint().height(),
            m_rgbChannelSliders->sizeHint().height()));
    }
    updateModeSwitcherButtons();
    updateChannelModeButtons();
    updateStyles();
    updateColorPreview();
}

QJsonObject ColorPanel::savePanelState() const
{
    QJsonObject state;
    const int pickerMode
        = m_colorPicker ? static_cast<int>(m_colorPicker->pickerMode()) : m_pickerMode;
    state["pickerMode"] = pickerMode;
    state["channelMode"] = m_channelMode;
    return state;
}

void ColorPanel::restorePanelState(const QJsonObject& state)
{
    if (state.isEmpty()) {
        return;
    }

    const int pickerMode = state["pickerMode"].toInt(m_pickerMode);
    if (pickerMode >= 0 && pickerMode <= 2) {
        m_pickerMode = pickerMode;
        if (m_colorPicker) {
            m_colorPicker->setPickerMode(
                static_cast<ruwa::ui::widgets::ColorPicker::PickerMode>(m_pickerMode));
        } else {
            emit pickerModeChanged(m_pickerMode);
        }
    }

    const int channelMode = state["channelMode"].toInt(m_channelMode);
    if (channelMode >= 0 && channelMode <= 1) {
        setChannelMode(channelMode);
    }

    updateModeSwitcherButtons();
    updateChannelModeButtons();
}

// ============================================================================
// PickerModeButton — small animated button that draws a picker mode icon
// ============================================================================

namespace {

constexpr int BASE_BTN_SIZE = 15;
constexpr int BASE_ICON_SIZE = 10;

class ChannelModeButton : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit ChannelModeButton(const QString& iconName, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_iconName(iconName)
    {
        auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
        const int sz = mgr.scaled(BASE_BTN_SIZE);
        setFixedSize(sz, sz);
        setCheckable(true);
        setFlat(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"));
        setHoverDuration(150);
        setActiveDuration(200);
        connect(this, &QAbstractButton::toggled, this, &BaseAnimatedButton::setActive);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QColor normalColor = colors.textMuted;
        const QColor activeColor = colors.primary;
        const auto lerp = [](int a, int b, qreal t) { return a + qRound((b - a) * t); };
        const QColor iconColor(lerp(normalColor.red(), activeColor.red(), activeProgress()),
            lerp(normalColor.green(), activeColor.green(), activeProgress()),
            lerp(normalColor.blue(), activeColor.blue(), activeProgress()));

        auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
        const int iconSize = mgr.scaled(BASE_ICON_SIZE);
        const QRect iconRect(
            (width() - iconSize) / 2, (height() - iconSize) / 2, iconSize, iconSize);
        ruwa::ui::core::IconProvider::instance()
            .getColoredIcon(m_iconName, iconColor)
            .paint(&painter, iconRect);
    }

private:
    QString m_iconName;
};

class PickerModeButton : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit PickerModeButton(int modeIndex, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_modeIndex(modeIndex)
    {
        auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
        const int sz = mgr.scaled(BASE_BTN_SIZE);
        setFixedSize(sz, sz);
        setCheckable(true);
        setFlat(true);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; }"));
        setHoverDuration(150);
        setActiveDuration(200);
        connect(this, &QAbstractButton::toggled, this, &BaseAnimatedButton::setActive);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto& mgr = ruwa::ui::core::WidgetStyleManager::instance();
        const auto& c = ruwa::ui::core::ThemeManager::instance().colors();
        // Icon colour interpolation: textMuted → primary
        QColor normalColor = c.textMuted;
        QColor activeColor = c.primary;
        auto lerp = [](int a, int b, qreal t) { return a + qRound((b - a) * t); };
        QColor iconColor(lerp(normalColor.red(), activeColor.red(), activeProgress()),
            lerp(normalColor.green(), activeColor.green(), activeProgress()),
            lerp(normalColor.blue(), activeColor.blue(), activeProgress()));

        // Draw mode icon
        const int iconSz = mgr.scaled(BASE_ICON_SIZE);
        const qreal ox = (width() - iconSz) / 2.0;
        const qreal oy = (height() - iconSz) / 2.0;
        const qreal inset = iconSz * 0.12;
        const QRectF area(ox + inset, oy + inset, iconSz - 2 * inset, iconSz - 2 * inset);

        const qreal penW = mgr.scaled(1.2);
        painter.setPen(QPen(iconColor, penW));
        painter.setBrush(Qt::NoBrush);

        switch (m_modeIndex) {
        case 0: { // Classic
            qreal barW = area.width() * 0.15;
            qreal gap = mgr.scaled(1.5);
            QRectF sq(area.left(), area.top(), area.width() - barW - gap, area.height());
            QRectF bar(sq.right() + gap, area.top(), barW, area.height());
            painter.drawRect(sq);
            painter.drawRect(bar);
            break;
        }
        case 1: { // Triangle
            qreal cr = area.width() / 2.0;
            QPointF cc = area.center();
            painter.drawEllipse(cc, cr, cr);
            qreal tr = cr * 0.6;
            QPolygonF tri;
            for (int j = 0; j < 3; ++j) {
                qreal angle = -M_PI / 2.0 + j * 2.0 * M_PI / 3.0;
                tri << QPointF(cc.x() + tr * qCos(angle), cc.y() + tr * qSin(angle));
            }
            painter.drawPolygon(tri);
            break;
        }
        case 2: { // Square-in-ring
            qreal cr = area.width() / 2.0;
            QPointF cc = area.center();
            painter.drawEllipse(cc, cr, cr);
            qreal half = cr * 0.5;
            painter.drawRect(QRectF(cc.x() - half, cc.y() - half, half * 2, half * 2));
            break;
        }
        }
    }

private:
    int m_modeIndex = 0;
};

} // anonymous namespace

// ============================================================================
// Mode switcher (interactive title)
// ============================================================================

void ColorPanel::setupModeSwitcher()
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int gap = theme.scaled(2);
    const int hPad = theme.scaled(1);

    auto* pickerContainer = new QWidget();
    pickerContainer->setAttribute(Qt::WA_TranslucentBackground);
    pickerContainer->setAttribute(Qt::WA_NoSystemBackground, true);
    pickerContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    pickerContainer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto* pickerLayout = new QHBoxLayout(pickerContainer);
    pickerLayout->setContentsMargins(hPad, 0, hPad, 0);
    pickerLayout->setSpacing(gap);

    for (int i = 0; i < 3; ++i) {
        auto* btn = new PickerModeButton(i, pickerContainer);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (!m_colorPicker)
                return;
            m_pickerMode = i;
            m_colorPicker->setPickerMode(
                static_cast<ruwa::ui::widgets::ColorPicker::PickerMode>(i));
            updateModeSwitcherButtons();
        });

        m_modeButtons.append(btn);
        pickerLayout->addWidget(btn);
    }

    // Sync when picker mode changes externally
    if (m_colorPicker) {
        connect(m_colorPicker, &ruwa::ui::widgets::ColorPicker::pickerModeChanged, this,
            [this]() { updateModeSwitcherButtons(); });
    }

    auto* channelContainer = new QWidget();
    channelContainer->setAttribute(Qt::WA_TranslucentBackground);
    channelContainer->setAttribute(Qt::WA_NoSystemBackground, true);
    channelContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    channelContainer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto* channelLayout = new QHBoxLayout(channelContainer);
    channelLayout->setContentsMargins(hPad, 0, hPad, 0);
    channelLayout->setSpacing(gap);

    const QStringList channelIcons { QStringLiteral("HSV"), QStringLiteral("RGB") };
    const QStringList channelTooltips { tr("HSV channels"), tr("RGB channels") };
    for (int i = 0; i < channelIcons.size(); ++i) {
        auto* btn = new ChannelModeButton(channelIcons[i], channelContainer);
        btn->setToolTip(channelTooltips[i]);
        btn->setAccessibleName(channelTooltips[i]);
        connect(btn, &QPushButton::clicked, this, [this, i]() { setChannelMode(i); });

        m_channelModeButtons.append(btn);
        channelLayout->addWidget(btn);
    }

    updateModeSwitcherButtons();
    updateChannelModeButtons();
    setTitleLeadingWidget(pickerContainer);
    setTitleTrailingWidget(channelContainer);
}

void ColorPanel::updateModeSwitcherButtons()
{
    if (m_modeButtons.isEmpty())
        return;

    const int currentMode
        = m_colorPicker ? static_cast<int>(m_colorPicker->pickerMode()) : m_pickerMode;

    for (int i = 0; i < m_modeButtons.size(); ++i) {
        m_modeButtons[i]->setChecked(i == currentMode);
        m_modeButtons[i]->update();
    }
}

} // namespace ruwa::ui::workspace
