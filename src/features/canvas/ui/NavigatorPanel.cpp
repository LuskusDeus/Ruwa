// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   N A V I G A T O R   P A N E L
// ==========================================================================

#include "NavigatorPanel.h"
#include "NavigatorWidget.h"
#include "CanvasPanel.h"
#include "ZoomSliderMapping.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/ToolButton.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ruwa::ui::workspace {
namespace {

constexpr int kControlsHeight = 25;
constexpr int kControlsPadding = 4;
constexpr int kResetButtonSize = 17;
constexpr int kResetButtonIconSize = 10;
constexpr int kSliderMinHeight = 17;
constexpr qreal kSliderFillInset = 2.5;
constexpr int kSliderSurfaceOpacityPercent = 58;
constexpr int kSliderTrackOpacityPercent = 0;
constexpr int kSliderFillOpacityPercent = 85;

bool canControlZoom(const CanvasPanel* panel)
{
    return panel && panel->isGLContentReady() && panel->isInteractionEnabled()
        && !panel->isExportMode();
}

} // namespace

NavigatorPanel::NavigatorPanel(QWidget* parent)
    : DockPanel(tr("Navigator"), parent)
{
    setTranslatableTitle(QT_TR_NOOP("Navigator"));
    setIconType(ruwa::ui::core::IconProvider::StandardIcon::NavigatorPanel);
    setMinimumPanelSize(150, 145);
    setPreferredPanelSize(220, 205);
    setClosable(true);
    setFloatable(true);
    setMovable(true);
}

NavigatorPanel::~NavigatorPanel() = default;

void NavigatorPanel::setCanvasPanel(CanvasPanel* panel)
{
    if (m_canvasPanel == panel)
        return;

    if (m_canvasPanel) {
        disconnect(m_canvasPanel, nullptr, this, nullptr);
    }
    m_canvasPanel = panel;
    m_minZoom = 0.8;
    m_maxZoom = 58.0;
    if (m_navigatorWidget) {
        m_navigatorWidget->setCanvasPanel(panel);
    }

    if (m_canvasPanel) {
        connect(m_canvasPanel, &CanvasPanel::zoomChanged, this,
            &NavigatorPanel::syncZoomSlider, Qt::UniqueConnection);
        connect(m_canvasPanel, &CanvasPanel::zoomLimitsChanged, this,
            &NavigatorPanel::setZoomLimits, Qt::UniqueConnection);
        connect(m_canvasPanel, &CanvasPanel::glContentReady, this,
            &NavigatorPanel::syncZoomControls, Qt::UniqueConnection);
        connect(m_canvasPanel, &CanvasPanel::canvasSizeChanged, this,
            &NavigatorPanel::syncZoomControls, Qt::UniqueConnection);
        connect(m_canvasPanel, &QObject::destroyed, this, [this, panel]() {
            if (m_canvasPanel == panel) {
                m_canvasPanel = nullptr;
                if (m_navigatorWidget) {
                    m_navigatorWidget->setCanvasPanel(nullptr);
                }
                syncZoomControls();
            }
        });

        if (m_canvasPanel->isGLContentReady()
            && !m_canvasPanel->isLoadingAppearanceAnimationActive()) {
            const auto& camera = m_canvasPanel->viewport().camera();
            setZoomLimits(camera.minZoom(), camera.maxZoom());
        }
    }

    syncZoomControls();
}

QWidget* NavigatorPanel::createContent()
{
    auto* content = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_navigatorWidget = new NavigatorWidget(content);
    m_navigatorWidget->setCanvasPanel(m_canvasPanel);
    contentLayout->addWidget(m_navigatorWidget, 1);

    auto* controls = new QWidget(content);
    auto* controlsLayout = new QHBoxLayout(controls);
    auto& style = ruwa::ui::core::WidgetStyleManager::instance();
    const int padding = style.scaled(kControlsPadding);
    controlsLayout->setContentsMargins(padding, padding, padding, padding);
    controlsLayout->setSpacing(padding);

    m_resetZoomButton = new ToolButton(ToolButton::Mode::Action, controls);
    m_resetZoomButton->setBaseSquareSize(kResetButtonSize, kResetButtonIconSize);
    m_resetZoomButton->setChromeStyle(ToolButton::ChromeStyle::Surface);
    m_resetZoomButton->setBorderVisible(true);
    m_resetZoomButton->setMutedNormalIcon(true);
    m_resetZoomButton->setIconType(ruwa::ui::core::IconProvider::StandardIcon::Reset);
    connect(m_resetZoomButton, &ToolButton::clicked, this, &NavigatorPanel::resetZoom);
    controlsLayout->addWidget(m_resetZoomButton);

    m_zoomSlider = new ruwa::ui::widgets::ProgressHandleSlider(controls);
    m_zoomSlider->setOrientation(Qt::Horizontal);
    m_zoomSlider->setRange(
        ruwa::ui::widgets::zoom_slider::kMinimum, ruwa::ui::widgets::zoom_slider::kMaximum);
    m_zoomSlider->setShowValueText(false);
    m_zoomSlider->setFillInset(style.scaled(kSliderFillInset));
    m_zoomSlider->setBackgroundOpacity(
        kSliderSurfaceOpacityPercent / 100.0, kSliderTrackOpacityPercent / 100.0);
    m_zoomSlider->setProgressFillOpacity(kSliderFillOpacityPercent / 100.0);
    m_zoomSlider->setCursor(Qt::PointingHandCursor);
    m_zoomSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_zoomSlider->setMinimumHeight(style.scaled(kSliderMinHeight));
    connect(m_zoomSlider, &ruwa::ui::widgets::ProgressHandleSlider::valueChanged, this,
        &NavigatorPanel::onZoomSliderValueChanged);
    controlsLayout->addWidget(m_zoomSlider, 1);

    controls->setFixedHeight(style.scaled(kControlsHeight));
    contentLayout->addWidget(controls);

    retranslateUi();
    syncZoomControls();
    return content;
}

void NavigatorPanel::onThemeChanged()
{
    DockPanel::onThemeChanged();
}

void NavigatorPanel::retranslateUi()
{
    DockPanel::retranslateUi();
    if (m_resetZoomButton) {
        m_resetZoomButton->setToolTip(tr("Reset zoom"));
    }
}

void NavigatorPanel::syncZoomControls()
{
    const bool ready = m_canvasPanel && m_canvasPanel->isGLContentReady();
    if (m_zoomSlider) {
        m_zoomSlider->setEnabled(ready);
    }
    if (m_resetZoomButton) {
        m_resetZoomButton->setEnabled(ready);
    }

    if (ready) {
        syncZoomSlider(m_canvasPanel->viewport().camera().zoom());
    } else {
        syncZoomSlider(1.0);
    }
}

void NavigatorPanel::syncZoomSlider(qreal zoom)
{
    if (!m_zoomSlider) {
        return;
    }
    const QSignalBlocker blocker(m_zoomSlider);
    m_zoomSlider->setValue(
        ruwa::ui::widgets::zoom_slider::zoomToValue(zoom, m_minZoom, m_maxZoom));
}

void NavigatorPanel::setZoomLimits(qreal minZoom, qreal maxZoom)
{
    m_minZoom = qMax<qreal>(0.001, minZoom);
    m_maxZoom = qMax(m_minZoom, maxZoom);
    if (m_canvasPanel && m_canvasPanel->isGLContentReady()) {
        syncZoomSlider(m_canvasPanel->viewport().camera().zoom());
    }
}

void NavigatorPanel::onZoomSliderValueChanged(int value)
{
    if (!canControlZoom(m_canvasPanel)) {
        syncZoomControls();
        return;
    }
    const qreal zoom = ruwa::ui::widgets::zoom_slider::valueToZoom(
        value, m_minZoom, m_maxZoom);
    m_canvasPanel->setZoomSmooth(static_cast<float>(zoom));
}

void NavigatorPanel::resetZoom()
{
    if (!canControlZoom(m_canvasPanel)) {
        return;
    }
    m_canvasPanel->setZoomSmooth(1.0f);
}

} // namespace ruwa::ui::workspace
