// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H E S   P A N E L
// ==========================================================================

#include "features/brush/ui/BrushesPanel.h"

#include "features/brush/manager/BrushManager.h"
#include "features/brush/ui/BrushPackListSection.h"
#include "features/brush/ui/BrushesPanelContent.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QPainter>
#include <QSignalBlocker>
#include <QTimer>

namespace ruwa::ui::workspace {

namespace {

using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeFontRole;
using ruwa::ui::core::ThemeManager;
using ruwa::ui::core::WidgetStyleManager;

constexpr auto kFavoritesFilterId = "__favorites_filter__";
constexpr auto kAllFilterId = "__all_filter__";

QString translatedFilterText(const QString& text)
{
    if (text.isEmpty()) {
        return text;
    }
    return QCoreApplication::translate("QObject", text.toUtf8().constData());
}

class BrushFilterButton final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit BrushFilterButton(const QString& text, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_text(text)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setHoverDuration(130);
        setActiveDuration(180);

        refreshTypography();
        connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
            refreshTypography();
            update();
        });
    }

    void setSelected(bool selected) { setActive(selected); }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto& colors = WidgetStyleManager::instance().colors();
        const QRectF buttonRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = ThemeManager::instance().scaled(6);
        const qreal active = activeProgress();

        painter.setPen(Qt::NoPen);
        if (hoverProgress() > 0.001 && active < 0.999) {
            QColor hoverFill = colors.overlay(0.06);
            hoverFill.setAlphaF(hoverFill.alphaF() * hoverProgress() * (1.0 - active));
            painter.setBrush(hoverFill);
            painter.drawRoundedRect(buttonRect, radius, radius);
        }

        if (active > 0.001) {
            QColor activeFill = colors.primary;
            activeFill.setAlphaF(activeFill.alphaF() * active);
            painter.setBrush(activeFill);
            painter.drawRoundedRect(buttonRect, radius, radius);
        }

        painter.setFont(font());
        const QColor idleText
            = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress() * 0.32);
        const QColor textColor = ThemeColors::interpolate(idleText, colors.textOnPrimary(), active);
        painter.setPen(textColor);
        painter.drawText(rect(), Qt::AlignCenter, translatedFilterText(m_text));
    }

private:
    void refreshTypography()
    {
        const auto& theme = ThemeManager::instance();
        const QFont buttonFont = theme.font(ThemeFontRole::Small, QFont::Medium);
        setFont(buttonFont);
        const int width = QFontMetrics(buttonFont).horizontalAdvance(translatedFilterText(m_text))
            + theme.scaled(18);
        setFixedSize(qMax(theme.scaled(34), width), theme.scaled(24));
    }

    QString m_text;
};

class BrushFilterSeparator final : public QWidget {
public:
    explicit BrushFilterSeparator(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(ThemeManager::instance().scaled(7), ThemeManager::instance().scaled(24));
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        QColor line = WidgetStyleManager::instance().colors().borderSubtle();
        line.setAlphaF(line.alphaF() * 0.72);
        painter.setPen(QPen(line, 1.0));
        painter.drawLine(QPointF(width() * 0.5, ThemeManager::instance().scaled(5)),
            QPointF(width() * 0.5, height() - ThemeManager::instance().scaled(5)));
    }
};

int singleBrushMinimumPanelWidth(int buttonBaseSize)
{
    auto& theme = ruwa::ui::core::ThemeManager::instance();

    // One brush button, the content's outer margins, the 12 px scrollbar,
    // and the panel's 1 px frame. The brush flow has no additional insets.
    return theme.scaled(buttonBaseSize) + (theme.scaled(kBrushesPanelContentMargin) * 2) + 12 + 2;
}

} // namespace

BrushesPanel::BrushesPanel(QWidget* parent)
    : DockPanel(tr("Brushes"), parent)
{
    setTranslatableTitle(QT_TR_NOOP("Brushes"));
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::Brushpack);
    setMinimumPanelSize(singleBrushMinimumPanelWidth(brushButtonBaseSize()), 180);
    setPreferredPanelSize(280, 340);
    setClosable(true);
    setFloatable(true);
    setMovable(true);
}

BrushesPanel::~BrushesPanel() = default;

int BrushesPanel::brushButtonBaseSize() const
{
    return qRound(kBrushListButtonBaseSize * m_hudSize / 100.0);
}

void BrushesPanel::setHudSize(int percent)
{
    const int clamped = qBound(kMinimumHudSize, percent, kMaximumHudSize);
    if (m_hudSize == clamped) {
        return;
    }

    m_hudSize = clamped;
    setMinimumPanelSize(singleBrushMinimumPanelWidth(brushButtonBaseSize()), 180);
    if (m_contentWidget) {
        m_contentWidget->setBrushButtonBaseSize(brushButtonBaseSize());
    }
    emit panelStateChanged();
}

void BrushesPanel::setCanvasPanel(CanvasPanel* canvasPanel)
{
    if (m_canvasPanel == canvasPanel) {
        return;
    }

    m_canvasPanel = canvasPanel;
    if (m_contentWidget) {
        m_contentWidget->setCanvasPanel(canvasPanel);
    }
}

void BrushesPanel::openBrushEditorForBrush(const QString& brushId)
{
    if (!m_contentWidget || brushId.isEmpty()) {
        return;
    }

    const QString presetId
        = ruwa::core::brushes::BrushManager::instance().presetIdForBrush(brushId);
    if (!presetId.isEmpty()) {
        m_contentWidget->openBrushEditor(presetId, brushId);
    }
}

void BrushesPanel::prepareVisiblePreviews()
{
    if (m_contentWidget) {
        m_contentWidget->prepareVisiblePreviews();
    }
}

bool BrushesPanel::visiblePreviewsReady() const
{
    return !m_contentWidget || m_contentWidget->visiblePreviewsReady();
}

QWidget* BrushesPanel::createContent()
{
    m_contentWidget = new BrushesPanelContent(this);
    m_contentWidget->setBrushButtonBaseSize(brushButtonBaseSize());
    m_contentWidget->setCanvasPanel(m_canvasPanel);
    connect(m_contentWidget, &BrushesPanelContent::stateChanged, this,
        &BrushesPanel::panelStateChanged);
    connect(m_contentWidget, &BrushesPanelContent::visiblePreviewStateChanged, this,
        &BrushesPanel::visiblePreviewStateChanged);
    connect(m_contentWidget, &BrushesPanelContent::packFiltersChanged, this,
        [this](const QStringList& packIds, const QStringList& packNames) {
            rebuildFilterButtons(packIds, packNames);
        });

    m_packFilterIds = m_contentWidget->packFilterIds();
    m_packFilterNames = m_contentWidget->packFilterNames();
    setupFilterBar();

    if (!m_pendingPanelState.isEmpty()) {
        m_contentWidget->restoreState(m_pendingPanelState);
    }
    return m_contentWidget;
}

void BrushesPanel::onThemeChanged()
{
    DockPanel::onThemeChanged();
    setMinimumPanelSize(singleBrushMinimumPanelWidth(brushButtonBaseSize()), 180);
    if (m_filterScrollArea && !m_filterBarInitializing) {
        m_filterScrollArea->setFixedHeight(ThemeManager::instance().scaled(26));
        if (m_filterLayout) {
            m_filterLayout->setSpacing(ThemeManager::instance().scaled(3));
        }
        rebuildFilterButtons(m_packFilterIds, m_packFilterNames);
    }
    if (m_contentWidget) {
        m_contentWidget->update();
    }
}

QJsonObject BrushesPanel::savePanelState() const
{
    QJsonObject state = m_contentWidget ? m_contentWidget->saveState() : m_pendingPanelState;
    state[QStringLiteral("hudSize")] = m_hudSize;
    return state;
}

void BrushesPanel::restorePanelState(const QJsonObject& state)
{
    m_pendingPanelState = state;
    // Restoring a layout must not schedule a new save through the public setter.
    const QSignalBlocker blocker(this);
    setHudSize(state.value(QStringLiteral("hudSize")).toInt(kDefaultHudSize));
    if (m_contentWidget) {
        m_contentWidget->restoreState(state);
    }
}

void BrushesPanel::setupFilterBar()
{
    auto* filterBar = new QWidget(this);
    filterBar->setAttribute(Qt::WA_TranslucentBackground);
    auto* filterBarLayout = new QHBoxLayout(filterBar);
    filterBarLayout->setContentsMargins(0, 0, 0, 0);
    filterBarLayout->setSpacing(0);

    m_filterScrollArea = new widgets::SmoothScrollArea(filterBar);
    m_filterScrollArea->setOrientation(Qt::Horizontal);
    m_filterScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_filterScrollArea->setFillBackground(false);
    m_filterScrollArea->setFixedHeight(ThemeManager::instance().scaled(26));
    filterBarLayout->addWidget(m_filterScrollArea);

    m_filterContent = new QWidget(m_filterScrollArea);
    m_filterContent->setAttribute(Qt::WA_TranslucentBackground);
    m_filterContent->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_filterLayout = new QHBoxLayout(m_filterContent);
    m_filterLayout->setContentsMargins(0, 0, 0, 0);
    m_filterLayout->setSpacing(ThemeManager::instance().scaled(3));
    m_filterLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_filterScrollArea->setWidget(m_filterContent);

    setSubtitleContentMargins(6, 4, 6, 4);
    setSubtitleContentSpacing(0);
    m_filterBarInitializing = true;
    setSubtitleWidget(filterBar);
    m_filterBarInitializing = false;

    if (m_activeFilterId.isEmpty()) {
        m_activeFilterId = QLatin1String(kAllFilterId);
    }
    rebuildFilterButtons(m_packFilterIds, m_packFilterNames);
}

void BrushesPanel::rebuildFilterButtons(const QStringList& packIds, const QStringList& packNames)
{
    m_packFilterIds = packIds;
    m_packFilterNames = packNames;
    if (!m_filterLayout || !m_filterContent || !m_filterScrollArea) {
        return;
    }

    const int previousScrollValue = m_filterScrollArea->scrollValue();
    m_filterButtons.clear();
    while (QLayoutItem* item = m_filterLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }

    const bool activePackStillExists = m_packFilterIds.contains(m_activeFilterId);
    if (m_activeFilterId != QLatin1String(kFavoritesFilterId)
        && m_activeFilterId != QLatin1String(kAllFilterId) && !activePackStillExists) {
        m_activeFilterId = QLatin1String(kAllFilterId);
        if (m_contentWidget) {
            m_contentWidget->showAllPacks();
        }
    }

    auto addFilterButton = [this](const QString& id, const QString& text) {
        auto* button = new BrushFilterButton(text, m_filterContent);
        connect(button, &QAbstractButton::clicked, this, [this, id]() { activateFilter(id); });
        m_filterLayout->addWidget(button);
        m_filterButtons.insert(id, button);
    };

    addFilterButton(QLatin1String(kFavoritesFilterId), tr("Fav"));
    addFilterButton(QLatin1String(kAllFilterId), tr("All"));
    m_filterLayout->addWidget(new BrushFilterSeparator(m_filterContent));

    const int count = qMin(m_packFilterIds.size(), m_packFilterNames.size());
    for (int i = 0; i < count; ++i) {
        addFilterButton(m_packFilterIds[i], m_packFilterNames[i]);
    }

    updateFilterSelection();
    m_filterContent->adjustSize();
    m_filterContent->updateGeometry();
    m_filterScrollArea->refreshScrollGeometry();

    QTimer::singleShot(0, this, [this, previousScrollValue]() {
        if (!m_filterScrollArea) {
            return;
        }
        m_filterScrollArea->refreshScrollGeometry();
        m_filterScrollArea->setScrollValue(previousScrollValue);
        revealActiveFilter();
    });
}

void BrushesPanel::activateFilter(const QString& filterId)
{
    QString resolvedFilterId = filterId;
    if (resolvedFilterId != QLatin1String(kFavoritesFilterId)
        && resolvedFilterId != QLatin1String(kAllFilterId)
        && !m_packFilterIds.contains(resolvedFilterId)) {
        resolvedFilterId = QLatin1String(kAllFilterId);
    }
    if (m_activeFilterId == resolvedFilterId) {
        revealActiveFilter();
        return;
    }

    m_activeFilterId = resolvedFilterId;
    updateFilterSelection();

    if (m_contentWidget) {
        if (m_activeFilterId == QLatin1String(kFavoritesFilterId)) {
            m_contentWidget->showFavoriteBrushes();
        } else if (m_activeFilterId == QLatin1String(kAllFilterId)) {
            m_contentWidget->showAllPacks();
        } else {
            m_contentWidget->showPack(m_activeFilterId);
        }
    }
    revealActiveFilter();
}

void BrushesPanel::updateFilterSelection()
{
    for (auto it = m_filterButtons.begin(); it != m_filterButtons.end(); ++it) {
        static_cast<BrushFilterButton*>(it.value())->setSelected(it.key() == m_activeFilterId);
    }
}

void BrushesPanel::revealActiveFilter()
{
    if (!m_filterScrollArea || !m_filterScrollArea->viewport()) {
        return;
    }
    auto* button
        = static_cast<BrushFilterButton*>(m_filterButtons.value(m_activeFilterId, nullptr));
    if (!button) {
        return;
    }

    const int viewportWidth = m_filterScrollArea->viewport()->width();
    const int currentValue = m_filterScrollArea->scrollValue();
    if (button->x() < currentValue) {
        m_filterScrollArea->scrollTo(button->x());
    } else if (button->geometry().right() > currentValue + viewportWidth) {
        m_filterScrollArea->scrollTo(button->geometry().right() - viewportWidth);
    }
}

} // namespace ruwa::ui::workspace
