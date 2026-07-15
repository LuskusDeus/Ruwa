// SPDX-License-Identifier: MPL-2.0

#include "SimpleActionsContextMenu.h"

#include "shared/resources/IconProvider.h"
#include "shared/widgets/BaseStyledWidget.h"
#include "features/theme/manager/ThemeManager.h"

#include <QLatin1String>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QtGlobal>
#include <QVariantList>
#include <QVector>
#include <QWidget>
#include <functional>

namespace ruwa::ui::widgets {

namespace {
constexpr auto kKeySimpleActions = "simpleActions";
constexpr auto kKeySimpleColorActions = "simpleColorActions";
constexpr auto kKeyChecked = "checked";
constexpr auto kKeyStandardIcon = "standardIcon";
constexpr auto kKeyColorRgba = "colorRgba";
constexpr auto kKeySeparatorBefore = "separatorBefore";

class SimpleMenuSeparatorLine final : public QWidget {
public:
    explicit SimpleMenuSeparatorLine(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const int h = ruwa::ui::core::ThemeManager::instance().scaled(9);
        setFixedHeight(qMax(1, h));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const QColor c = theme.colors().border;
        const int marginH = theme.scaled(8);
        const int y = height() / 2;
        painter.setPen(QPen(c, 1));
        painter.drawLine(marginH, y, qMax(marginH + 1, width() - marginH), y);
    }
};

struct SimpleMenuColorAction {
    int actionId = 0;
    QColor color;
    bool checked = false;
    bool separatorBefore = false;
};

class SimpleMenuColorStrip final : public QWidget {
public:
    explicit SimpleMenuColorStrip(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(stripHeight());
    }

    void setActions(const QList<SimpleMenuColorAction>& actions)
    {
        m_actions = actions;
        updateGeometry();
        update();
    }

    void setTriggerCallback(std::function<void(int)> callback)
    {
        m_triggerCallback = std::move(callback);
    }

    QSize sizeHint() const override
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        return QSize(qMax(theme.scaled(180), contentWidth()), stripHeight());
    }

    static int stripHeight()
    {
        return qMax(24, ruwa::ui::core::ThemeManager::instance().scaled(28));
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        if (m_actions.isEmpty()) {
            return;
        }

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QVector<ItemGeometry> geometry = itemGeometry();
        for (int i = 0; i < geometry.size(); ++i) {
            const ItemGeometry& item = geometry.at(i);
            if (!item.separatorRect.isNull()) {
                painter.setPen(QPen(colors.borderSubtle(), 1.0));
                painter.drawLine(item.separatorRect.topLeft(), item.separatorRect.bottomLeft());
            }

            if (i == m_hoveredIndex) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colors.overlayHover());
                painter.drawRoundedRect(item.slotRect.adjusted(1, 1, -1, -1),
                    item.slotRect.height() / 2.0, item.slotRect.height() / 2.0);
            }

            QColor border = ruwa::ui::core::ThemeColors::withAlpha(
                colors.overlayColor, colors.isDark ? 44 : 72);
            painter.setPen(QPen(border, 1.0));
            painter.setBrush(m_actions.at(i).color);
            painter.drawEllipse(item.circleRect);

            if (m_actions.at(i).checked) {
                QRect ringRect = item.circleRect.adjusted(-2, -2, 2, 2);
                QColor ringColor = ruwa::ui::core::ThemeColors::withAlpha(
                    colors.text, colors.isDark ? 210 : 170);
                painter.setPen(QPen(ringColor, 1.25));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(ringRect);
            }
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        setHoveredIndex(indexAt(event->position().toPoint()));
        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        setHoveredIndex(-1);
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_pressedIndex = indexAt(event->position().toPoint());
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mouseReleaseEvent(event);
            return;
        }

        const int releasedIndex = indexAt(event->position().toPoint());
        const int triggeredIndex
            = (releasedIndex >= 0 && releasedIndex == m_pressedIndex) ? releasedIndex : -1;
        m_pressedIndex = -1;

        if (triggeredIndex >= 0 && triggeredIndex < m_actions.size() && m_triggerCallback) {
            m_triggerCallback(m_actions.at(triggeredIndex).actionId);
            event->accept();
            return;
        }

        QWidget::mouseReleaseEvent(event);
    }

private:
    struct ItemGeometry {
        QRect slotRect;
        QRect circleRect;
        QRect separatorRect;
    };

    QVector<ItemGeometry> itemGeometry() const
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int centerY = height() / 2;
        const int horizontalInset = qMax(6, theme.scaled(10));
        const int slotSize = qMax(12, theme.scaled(14));
        const int circleSize = qMax(7, theme.scaled(8));
        const int gap = 1;
        const int separatorArea = qMax(6, theme.scaled(8));
        const int separatorHeight = qMax(10, theme.scaled(12));
        int x = horizontalInset;

        QVector<ItemGeometry> geometry;
        geometry.reserve(m_actions.size());

        for (int i = 0; i < m_actions.size(); ++i) {
            ItemGeometry item;
            if (m_actions.at(i).separatorBefore && i > 0) {
                const int separatorX = x + separatorArea / 2;
                item.separatorRect
                    = QRect(separatorX, centerY - separatorHeight / 2, 1, separatorHeight);
                x += separatorArea;
            }

            item.slotRect = QRect(x, centerY - slotSize / 2, slotSize, slotSize);
            item.circleRect = QRect(item.slotRect.center().x() - circleSize / 2,
                centerY - circleSize / 2, circleSize, circleSize);
            geometry.push_back(item);
            x += slotSize + gap;
        }

        return geometry;
    }

    int indexAt(const QPoint& pos) const
    {
        const QVector<ItemGeometry> geometry = itemGeometry();
        for (int i = 0; i < geometry.size(); ++i) {
            if (geometry.at(i).slotRect.adjusted(-1, -1, 1, 1).contains(pos)) {
                return i;
            }
        }
        return -1;
    }

    void setHoveredIndex(int index)
    {
        if (m_hoveredIndex == index) {
            return;
        }
        m_hoveredIndex = index;
        update();
    }

    int contentWidth() const
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int horizontalInset = qMax(6, theme.scaled(10));
        const int slotSize = qMax(12, theme.scaled(14));
        const int gap = 1;
        const int separatorArea = qMax(6, theme.scaled(8));
        int width = horizontalInset * 2;

        for (int i = 0; i < m_actions.size(); ++i) {
            if (m_actions.at(i).separatorBefore && i > 0) {
                width += separatorArea;
            }
            width += slotSize;
            if (i + 1 < m_actions.size()) {
                width += gap;
            }
        }

        return width;
    }

    QList<SimpleMenuColorAction> m_actions;
    std::function<void(int)> m_triggerCallback;
    int m_hoveredIndex = -1;
    int m_pressedIndex = -1;
};
} // namespace

SimpleActionsContextMenu::SimpleActionsContextMenu(QWidget* parent)
    : StandardContextMenu(parent)
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    setContentMargins(theme.scaled(QMargins(6, 6, 6, 6)));
    contentLayout()->setSpacing(4);

    auto* host = new QWidget(contentWidget());
    host->setAttribute(Qt::WA_TranslucentBackground);
    m_actionLayout = new QVBoxLayout(host);
    m_actionLayout->setContentsMargins(0, 0, 0, 0);
    m_actionLayout->setSpacing(theme.scaled(2));
    contentLayout()->addWidget(host);

    updateMenuSize();
}

QSize SimpleActionsContextMenu::expandMenuContentHint(const QSize& hint) const
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int minW = theme.scaled(180);

    // contentWidget()->sizeHint() is unreliable here (nested host + action layout), so derive
    // height from action count and the optional color strip.
    const QVariantList list = context().value(QLatin1String(kKeySimpleActions)).toList();
    const QVariantList colorList = context().value(QLatin1String(kKeySimpleColorActions)).toList();
    int minH = 0;
    if (!list.isEmpty() || !colorList.isEmpty()) {
        const int rowH = theme.scaled(30);
        const int sepH = theme.scaled(9);
        const int vmargins = theme.scaled(6) + theme.scaled(6);
        int contentH = 0;
        int widgetCount = 0;

        for (const QVariant& v : list) {
            const QVariantMap m = v.toMap();
            ++widgetCount;
            if (m.value(QStringLiteral("separator")).toBool()) {
                contentH += sepH;
            } else {
                contentH += rowH;
            }
        }

        if (!colorList.isEmpty()) {
            if (!list.isEmpty()) {
                ++widgetCount;
                contentH += sepH;
            }
            ++widgetCount;
            contentH += SimpleMenuColorStrip::stripHeight();
        }

        const int betweenRows = qMax(0, widgetCount - 1) * theme.scaled(2);
        minH = vmargins + contentH + betweenRows;
    }

    return QSize(qMax(hint.width(), minW), qMax(hint.height(), minH));
}

void SimpleActionsContextMenu::rebuildStandardMenu()
{
    if (!m_actionLayout) {
        return;
    }

    QLayoutItem* it = nullptr;
    while ((it = m_actionLayout->takeAt(0)) != nullptr) {
        delete it->widget();
        delete it;
    }

    const QVariantList list = context().value(QLatin1String(kKeySimpleActions)).toList();
    auto& icons = ruwa::ui::core::IconProvider::instance();

    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("separator")).toBool()) {
            m_actionLayout->addWidget(new SimpleMenuSeparatorLine(m_actionLayout->parentWidget()));
            continue;
        }

        const int actionId = m.value(QStringLiteral("id")).toInt();
        const QString text = m.value(QStringLiteral("text")).toString();
        const bool danger = m.value(QStringLiteral("danger")).toBool();
        const bool checked = m.value(QLatin1String(kKeyChecked)).toBool();

        QIcon icon;
        if (checked) {
            icon = icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Confirm);
        } else if (m.contains(QLatin1String(kKeyStandardIcon))) {
            const int ei = m.value(QLatin1String(kKeyStandardIcon)).toInt();
            icon = icons.getIcon(static_cast<ruwa::ui::core::IconProvider::StandardIcon>(ei));
        }

        BaseStyledWidget* row = addStandardMenuActionRow(icon, text, danger, m_actionLayout);
        const bool enabled = m.value(QStringLiteral("enabled"), true).toBool();
        row->setEnabled(enabled);
        if (enabled) {
            connect(row, &BaseStyledWidget::clicked, this, [this, actionId]() {
                // Tear the menu down synchronously *before* running the action.
                // Some actions (Export/Import Brush, …) open a modal native
                // dialog that spins its own event loop; an animated hide would
                // then freeze mid-fade and leave the menu painted on top of the
                // dialog until it closes. deleteLater() keeps the signal/slot
                // connections alive for the duration of the emit below.
                hide();
                deleteLater();
                emit actionTriggered(actionId);
            });
        }
    }

    const QVariantList colorList = context().value(QLatin1String(kKeySimpleColorActions)).toList();
    if (!colorList.isEmpty()) {
        if (!list.isEmpty()) {
            m_actionLayout->addWidget(new SimpleMenuSeparatorLine(m_actionLayout->parentWidget()));
        }

        QList<SimpleMenuColorAction> colorActions;
        colorActions.reserve(colorList.size());

        for (const QVariant& v : colorList) {
            const QVariantMap m = v.toMap();
            SimpleMenuColorAction action;
            action.actionId = m.value(QStringLiteral("id")).toInt();
            action.color = QColor::fromRgba(
                static_cast<QRgb>(m.value(QLatin1String(kKeyColorRgba)).toUInt()));
            action.checked = m.value(QLatin1String(kKeyChecked)).toBool();
            action.separatorBefore = m.value(QLatin1String(kKeySeparatorBefore)).toBool();
            colorActions.push_back(action);
        }

        auto* strip = new SimpleMenuColorStrip(m_actionLayout->parentWidget());
        strip->setActions(colorActions);
        strip->setTriggerCallback([this](int actionId) {
            hide();
            deleteLater();
            emit actionTriggered(actionId);
        });
        m_actionLayout->addWidget(strip);
    }

    updateMenuSize();
}

} // namespace ruwa::ui::widgets
