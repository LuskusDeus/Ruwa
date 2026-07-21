// SPDX-License-Identifier: MPL-2.0

// CanvasToolStateOverlay.cpp
#include "CanvasToolStateOverlay.h"
#include "features/canvas/ui/CanvasOverlayContextActions.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/Separator.h"
#include "shared/widgets/ToolButton.h"
#include "shared/widgets/inputs/OpacitySliderWidget.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "shared/style/PaintingUtils.h"

#include <QAbstractButton>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStringList>
#include <QStringLiteral>
#include <QTransform>
#include <QVariantList>
#include <QVariantMap>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

constexpr int kControlSpacingBase = 6;
constexpr int kSliderMinWidthBase = 180;
constexpr int kCanvasPlaceholderMinWidthBase = 320;
constexpr int kToolPlaceholderMinWidthBase = 260;
constexpr int kToolPageCount = 18;
constexpr int kParameterSliderHeightBase = 28;
constexpr int kBrushParameterGroupSpacingBase = 10;
constexpr int kSliderSurfaceOpacityPercent = 58;
constexpr int kSliderTrackOpacityPercent = 0;
constexpr int kSliderFillOpacityPercent = 85;
constexpr int kBrushToolPageIndex = 1;
constexpr int kEraserToolPageIndex = 2;
constexpr int kFillToolPageIndex = 3;
constexpr int kEyedropperToolPageIndex = 4;
constexpr int kLassoToolPageIndex = 5;
constexpr int kLassoFillToolPageIndex = 6;
constexpr int kSquareSelectionToolPageIndex = 7;
constexpr int kCircleSelectionToolPageIndex = 8;
constexpr int kRotateViewToolPageIndex = 10;
constexpr int kZoomToolPageIndex = 12;
constexpr int kClassicFillToolPageIndex = 13;
constexpr int kBlurToolPageIndex = 14;
constexpr int kSmudgeToolPageIndex = 16;
constexpr int kLiquifyToolPageIndex = 17;

QSize pageHint(QWidget* page)
{
    if (!page) {
        return QSize();
    }

    if (page->layout()) {
        page->layout()->activate();
    }

    QSize hint = page->sizeHint().expandedTo(page->minimumSizeHint());
    if (!hint.isValid()) {
        hint = page->size();
    }
    return hint;
}

QSize currentPageHint(AnimatedStackedWidget* stack)
{
    if (!stack || !stack->currentWidget()) {
        return QSize();
    }

    return pageHint(stack->currentWidget());
}

workspace::ToolButton* createToolButton(IconProvider::StandardIcon iconType,
    workspace::ToolButton::Mode mode, const QString& tooltip, QWidget* parent = nullptr)
{
    auto* button = new workspace::ToolButton(mode, parent);
    button->setIconType(iconType);
    button->setToolTip(tooltip);
    // No transient press darkening anywhere in this HUD: it flickers against the
    // smooth hover/toggle animations.
    button->setPressFeedbackEnabled(false);
    return button;
}

QWidget* createButtonSection(const QList<IconProvider::StandardIcon>& icons,
    workspace::ToolButton::Mode mode, const QStringList& tooltips, QWidget* parent = nullptr)
{
    auto* section = new QWidget(parent);
    section->setAttribute(Qt::WA_TranslucentBackground);
    section->setAutoFillBackground(false);
    section->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kControlSpacingBase));

    const int count = qMin(icons.size(), tooltips.size());
    for (int i = 0; i < count; ++i) {
        layout->addWidget(createToolButton(icons[i], mode, tooltips[i], section));
    }

    return section;
}

workspace::ToolButton* createRedoToolButton(const QString& tooltip, QWidget* parent = nullptr)
{
    auto* button = new workspace::ToolButton(workspace::ToolButton::Mode::Action, parent);
    button->setToolTip(tooltip);
    button->setPressFeedbackEnabled(false);

    auto& theme = ThemeManager::instance();
    const int iconSize = theme.scaled(20);
    QPixmap redoPixmap = IconProvider::instance().getPixmap(
        IconProvider::StandardIcon::UndoArrow, QSize(iconSize, iconSize));
    if (!redoPixmap.isNull()) {
        redoPixmap = redoPixmap.transformed(QTransform().scale(-1, 1));
        button->setIcon(QIcon(redoPixmap));
    }

    return button;
}

Separator* createSectionSeparator(QWidget* parent = nullptr)
{
    auto* separator = new Separator(parent);
    separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return separator;
}

void applyOverlaySliderStyle(ProgressHandleSlider* slider)
{
    if (!slider) {
        return;
    }

    slider->setBackgroundOpacity(
        kSliderSurfaceOpacityPercent / 100.0, kSliderTrackOpacityPercent / 100.0);
    slider->setProgressFillOpacity(kSliderFillOpacityPercent / 100.0);
}

QWidget* createToolContentPage(const QString& toolName, QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    page->setObjectName(QStringLiteral("tool-content-%1").arg(toolName));

    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kControlSpacingBase));

    auto* actionButton
        = createToolButton(IconProvider::StandardIcon::Edit, workspace::ToolButton::Mode::Action,
            QObject::tr("%1 action placeholder").arg(toolName), page);
    layout->addWidget(actionButton);

    auto* alphaToggle
        = createToolButton(IconProvider::StandardIcon::Alpha, workspace::ToolButton::Mode::Toggle,
            QObject::tr("%1 toggle placeholder").arg(toolName), page);
    layout->addWidget(alphaToggle);

    auto* lockToggle
        = createToolButton(IconProvider::StandardIcon::Lock, workspace::ToolButton::Mode::Toggle,
            QObject::tr("%1 lock placeholder").arg(toolName), page);
    layout->addWidget(lockToggle);

    auto* opacitySlider = new OpacitySliderWidget(page);
    opacitySlider->setToolTip(QObject::tr("%1 opacity placeholder").arg(toolName));
    opacitySlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    opacitySlider->setMinimumWidth(ThemeManager::instance().scaled(kSliderMinWidthBase));
    layout->addWidget(opacitySlider);

    return page;
}

QWidget* createBrushParameterRow(const QString& labelText, const QString& toolName,
    const QString& parameterName, ProgressHandleSlider** sliderOut, QWidget* parent = nullptr)
{
    auto* row = new QWidget(parent);
    row->setAttribute(Qt::WA_TranslucentBackground);
    row->setAutoFillBackground(false);
    row->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kControlSpacingBase));

    auto* label = new QLabel(labelText, row);
    label->setObjectName(QStringLiteral("parameter-label"));
    label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    label->setAttribute(Qt::WA_TranslucentBackground);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    layout->addWidget(label);

    auto* slider = new ProgressHandleSlider(row);
    slider->setOrientation(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(100);
    slider->setShowValueText(true);
    slider->setValueDisplayMode(ProgressHandleSlider::ValueDisplayMode::Percent);
    slider->setValueTextPrefix(QString());
    slider->setValueTextSuffix("%");
    slider->setToolTip(QObject::tr("%1 %2").arg(toolName, parameterName));
    applyOverlaySliderStyle(slider);
    slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    slider->setMinimumWidth(ThemeManager::instance().scaled(kSliderMinWidthBase));
    slider->setFixedHeight(ThemeManager::instance().scaled(kParameterSliderHeightBase));
    layout->addWidget(slider);

    if (sliderOut) {
        *sliderOut = slider;
    }

    return row;
}

QWidget* createBrushSettingsPage(const QString& toolName, ProgressHandleSlider** hardnessSliderOut,
    ProgressHandleSlider** flowSliderOut, QAbstractButton** eraserToggleOut = nullptr,
    QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    page->setObjectName(QStringLiteral("tool-content-%1").arg(toolName));

    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kBrushParameterGroupSpacingBase));

    if (eraserToggleOut) {
        auto* eraserToggle = createToolButton(IconProvider::StandardIcon::ZeroAlphaBrush,
            workspace::ToolButton::Mode::Toggle, QObject::tr("Erase with the current brush"), page);
        layout->addWidget(eraserToggle);
        *eraserToggleOut = eraserToggle;
    }

    layout->addWidget(createBrushParameterRow(
        QObject::tr("Hardness:"), toolName, QObject::tr("hardness"), hardnessSliderOut, page));
    layout->addWidget(createBrushParameterRow(
        QObject::tr("Flow:"), toolName, QObject::tr("flow"), flowSliderOut, page));

    return page;
}

QWidget* createSmudgeSettingsPage(const QString& toolName,
    ProgressHandleSlider** intensitySliderOut, ProgressHandleSlider** wetMixSliderOut,
    QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    page->setObjectName(QStringLiteral("tool-content-%1").arg(toolName));

    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kBrushParameterGroupSpacingBase));

    // Intensity = how strongly each dab deposits the carry buffer onto
    // the canvas (= TileBrush flow / shader uBrushAlpha).
    layout->addWidget(createBrushParameterRow(
        QObject::tr("Intensity:"), toolName, QObject::tr("intensity"), intensitySliderOut, page));
    // Pickup controls how much crossed canvas color is loaded for the next
    // dab. Transport remains controlled independently by Intensity.
    layout->addWidget(createBrushParameterRow(
        QObject::tr("Pickup:"), toolName, QObject::tr("color pickup"), wetMixSliderOut, page));

    return page;
}

QWidget* createSingleParameterPage(const QString& toolName, const QString& labelText,
    const QString& parameterName, ProgressHandleSlider** sliderOut, QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    page->setObjectName(QStringLiteral("tool-content-%1").arg(toolName));

    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kControlSpacingBase));
    layout->addWidget(createBrushParameterRow(labelText, toolName, parameterName, sliderOut, page));

    return page;
}

QWidget* createLiquifySettingsPage(const QString& toolName,
    ProgressHandleSlider** strengthSliderOut, QAbstractButton** modeButtonsOut,
    QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    page->setObjectName(QStringLiteral("tool-content-%1").arg(toolName));

    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kBrushParameterGroupSpacingBase));

    // Mode buttons (exclusive selection is wired by the overlay). Icons are
    // placeholders — to be replaced later.
    const IconProvider::StandardIcon icons[CanvasToolStateOverlay::kLiquifyModeCount] = {
        IconProvider::StandardIcon::LiquifyPush, // Push
        IconProvider::StandardIcon::LiquifyTwirlCW, // Twirl CW
        IconProvider::StandardIcon::LiquifyTwirlCCW, // Twirl CCW
        IconProvider::StandardIcon::LiquifyGrow, // Bloat
        IconProvider::StandardIcon::LiquifyShrink, // Pucker
    };
    const QString tips[CanvasToolStateOverlay::kLiquifyModeCount] = {
        QObject::tr("Push"),
        QObject::tr("Twirl clockwise"),
        QObject::tr("Twirl counter-clockwise"),
        QObject::tr("Bloat"),
        QObject::tr("Pucker"),
    };

    auto* buttonRow = new QWidget(page);
    buttonRow->setAttribute(Qt::WA_TranslucentBackground);
    buttonRow->setAutoFillBackground(false);
    buttonRow->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(ThemeManager::instance().scaled(kControlSpacingBase));
    for (int i = 0; i < CanvasToolStateOverlay::kLiquifyModeCount; ++i) {
        auto* btn
            = createToolButton(icons[i], workspace::ToolButton::Mode::Toggle, tips[i], buttonRow);
        btn->setCheckable(true);
        buttonLayout->addWidget(btn);
        if (modeButtonsOut) {
            modeButtonsOut[i] = btn;
        }
    }
    layout->addWidget(buttonRow);

    layout->addWidget(createBrushParameterRow(
        QObject::tr("Strength:"), toolName, QObject::tr("strength"), strengthSliderOut, page));
    return page;
}

QWidget* createCanvasResizeInfoPage(
    QLabel** oldSizeValueLabelOut, QLabel** newSizeValueLabelOut, QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ThemeManager::instance().scaled(kBrushParameterGroupSpacingBase));

    auto createInfoPair = [page, layout](const QString& title, QLabel** valueLabelOut) {
        auto* titleLabel = new QLabel(title + QStringLiteral(":"), page);
        titleLabel->setObjectName(QStringLiteral("canvas-resize-info-title"));
        titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleLabel->setAttribute(Qt::WA_TranslucentBackground);
        layout->addWidget(titleLabel);

        auto* valueLabel = new QLabel(QStringLiteral("0 x 0"), page);
        valueLabel->setObjectName(QStringLiteral("canvas-resize-info-value"));
        valueLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        valueLabel->setAttribute(Qt::WA_TranslucentBackground);
        layout->addWidget(valueLabel);

        if (valueLabelOut) {
            *valueLabelOut = valueLabel;
        }
    };

    createInfoPair(QObject::tr("Old canvas size"), oldSizeValueLabelOut);
    createInfoPair(QObject::tr("New canvas size"), newSizeValueLabelOut);
    return page;
}

QWidget* createCanvasPlaceholderPage(QLabel** labelOut, QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* label
        = new QLabel(QObject::tr("Parameters for this canvas mode are not available yet."), page);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMinimumWidth(ThemeManager::instance().scaled(kCanvasPlaceholderMinWidthBase));
    label->setAttribute(Qt::WA_TranslucentBackground);
    layout->addWidget(label);

    if (labelOut) {
        *labelOut = label;
    }

    return page;
}

QWidget* createToolPlaceholderPage(QLabel** labelOut, QWidget* parent = nullptr)
{
    auto* page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setAutoFillBackground(false);
    page->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* label
        = new QLabel(QObject::tr("There are no advanced parameters for this tool yet."), page);
    label->setObjectName(QStringLiteral("tool-placeholder-label"));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    label->setMinimumWidth(ThemeManager::instance().scaled(kToolPlaceholderMinWidthBase));
    label->setAttribute(Qt::WA_TranslucentBackground);
    layout->addWidget(label);

    if (labelOut) {
        *labelOut = label;
    }

    return page;
}

} // namespace

CanvasToolStateOverlay::CanvasToolStateOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    setTabletTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_widthAnimation = new QPropertyAnimation(this, "animatedWidth", this);
    m_widthAnimation->setDuration(220);
    m_widthAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_widthAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (!m_pendingAnimatedSize.isValid()) {
            return;
        }
        setFixedSize(m_pendingAnimatedSize);
        syncStackSizes();
        updateGeometry();
        update();
        emit sizeChanged(m_pendingAnimatedSize);
        m_pendingAnimatedSize = QSize();
    });

    setupUi();

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &CanvasToolStateOverlay::onThemeChanged);

    applyThemeMetrics();
}

CanvasToolStateOverlay::~CanvasToolStateOverlay() = default;

QVariantMap CanvasToolStateOverlay::contextMenuContext() const
{
    using ruwa::ui::canvas_overlay::CanvasOverlayContextActionId;

    QVariantList actions;
    QVariantMap hide;
    hide.insert(
        QStringLiteral("id"), static_cast<int>(CanvasOverlayContextActionId::HideToolStateOverlay));
    hide.insert(QStringLiteral("text"), tr("Hide widget"));
    hide.insert(QStringLiteral("danger"), false);
    hide.insert(QStringLiteral("standardIcon"),
        static_cast<int>(IconProvider::StandardIcon::EyeDeactivated));
    actions.append(hide);

    QVariantMap ctx;
    ctx.insert(QStringLiteral("simpleActions"), actions);
    return ctx;
}

void CanvasToolStateOverlay::setCanvasPageIndex(int index)
{
    if (!m_canvasModeStack || index < 0 || index >= m_canvasModeStack->count()) {
        return;
    }

    const int currentIndex = canvasPageIndex();
    if (currentIndex == index) {
        return;
    }

    if (!isVisible() || currentIndex < 0) {
        m_canvasPageSizeIndexOverride = index;
        updateOverlaySize();
        m_canvasModeStack->setCurrentIndex(index);
        return;
    }

    if (m_widthAnimation && m_widthAnimation->state() == QAbstractAnimation::Running) {
        m_widthAnimation->stop();
        m_pendingAnimatedSize = QSize();
    }

    auto& theme = ThemeManager::instance();
    const int sidePadding = theme.scaled(kPanelHorizontalPaddingBase);
    const QSize currentSize = canvasPageNaturalSize(currentIndex);
    const QSize targetSize = canvasPageNaturalSize(index);
    const int stableContentWidth = qMax(1, width() - sidePadding * 2);
    const int stableContentHeight = qMax(currentSize.height(), targetSize.height());

    updateVisibleContentGeometry(stableContentWidth, stableContentHeight);
    m_canvasPageSizeIndexOverride = index;
    m_canvasModeStack->setCurrentIndex(index);
}

int CanvasToolStateOverlay::canvasPageIndex() const
{
    return m_canvasModeStack ? m_canvasModeStack->currentIndex() : -1;
}

void CanvasToolStateOverlay::setCanvasPlaceholderText(const QString& text)
{
    if (m_canvasPlaceholderLabel && m_canvasPlaceholderLabel->text() != text) {
        m_canvasPlaceholderLabel->setText(text);
        syncStackSizes();
        updateOverlaySize();
    }
}

void CanvasToolStateOverlay::setCanvasFlipStates(bool horizontal, bool vertical)
{
    if (m_flipHorizontalButton) {
        const QSignalBlocker blocker(m_flipHorizontalButton);
        m_flipHorizontalButton->setChecked(horizontal);
    }
    if (m_flipVerticalButton) {
        const QSignalBlocker blocker(m_flipVerticalButton);
        m_flipVerticalButton->setChecked(vertical);
    }
}

void CanvasToolStateOverlay::setBrushEraserMode(bool enabled)
{
    if (!m_brushEraserToggleButton) {
        return;
    }
    const QSignalBlocker blocker(m_brushEraserToggleButton);
    m_brushEraserToggleButton->setChecked(enabled);
    // ToolButton drives its "active" highlight from the toggled() signal, which
    // the blocker above suppresses. Sync the visual state explicitly so a
    // programmatic update (e.g. project (re)creation) still shows as active.
    if (auto* animated = qobject_cast<BaseAnimatedButton*>(m_brushEraserToggleButton)) {
        animated->setActive(enabled);
    }
}

void CanvasToolStateOverlay::setUndoAvailable(bool available)
{
    if (m_undoButton) {
        m_undoButton->setEnabled(available);
    }
}

void CanvasToolStateOverlay::setRedoAvailable(bool available)
{
    if (m_redoButton) {
        m_redoButton->setEnabled(available);
    }
}

void CanvasToolStateOverlay::setToolPageParameterValues(int pageIndex, qreal hardness, qreal flow)
{
    const int hardnessValue = qRound(qBound(0.0, hardness, 1.0) * 100.0);
    const int flowValue = qRound(qBound(0.0, flow, 1.0) * 100.0);

    ProgressHandleSlider* hardnessSlider = nullptr;
    ProgressHandleSlider* flowSlider = nullptr;
    if (pageIndex == kBrushToolPageIndex) {
        hardnessSlider = m_brushHardnessSlider;
        flowSlider = m_brushFlowSlider;
    } else if (pageIndex == kEraserToolPageIndex) {
        hardnessSlider = m_eraserHardnessSlider;
        flowSlider = m_eraserFlowSlider;
    } else {
        return;
    }

    const auto syncSlider = [](ProgressHandleSlider* slider, int value) {
        if (!slider) {
            return;
        }
        const QSignalBlocker blocker(slider);
        slider->setValue(value);
    };

    syncSlider(hardnessSlider, hardnessValue);
    syncSlider(flowSlider, flowValue);
}

void CanvasToolStateOverlay::setToolPageIntensityValue(int pageIndex, qreal intensity)
{
    ProgressHandleSlider* slider = nullptr;
    if (pageIndex == kBlurToolPageIndex) {
        slider = m_blurIntensitySlider;
    } else if (pageIndex == kSmudgeToolPageIndex) {
        slider = m_smudgeIntensitySlider;
    } else if (pageIndex == kLiquifyToolPageIndex) {
        slider = m_liquifyStrengthSlider;
    } else {
        return;
    }

    if (!slider) {
        return;
    }

    const QSignalBlocker blocker(slider);
    slider->setValue(qRound(qBound(0.0, intensity, 1.0) * 100.0));
}

void CanvasToolStateOverlay::setToolPageWetMixValue(int pageIndex, qreal wetMix)
{
    if (pageIndex != kSmudgeToolPageIndex || !m_smudgeWetMixSlider) {
        return;
    }
    const QSignalBlocker blocker(m_smudgeWetMixSlider);
    m_smudgeWetMixSlider->setValue(qRound(qBound(0.0, wetMix, 1.0) * 100.0));
}

void CanvasToolStateOverlay::setToolPageLiquifyMode(int mode)
{
    if (mode < 0 || mode >= kLiquifyModeCount || !m_liquifyModeButtons[mode]) {
        return;
    }
    const QSignalBlocker blocker(m_liquifyModeGroup);
    m_liquifyModeButtons[mode]->setChecked(true);
}

void CanvasToolStateOverlay::setToolPageStabilizationValue(int pageIndex, qreal stabilization)
{
    ProgressHandleSlider* slider = nullptr;
    if (pageIndex == kLassoToolPageIndex) {
        slider = m_lassoStabilizationSlider;
    } else if (pageIndex == kLassoFillToolPageIndex) {
        slider = m_lassoFillStabilizationSlider;
    } else {
        return;
    }

    if (!slider) {
        return;
    }

    const QSignalBlocker blocker(slider);
    slider->setValue(qRound(qBound(0.0, stabilization, 1.0) * 100.0));
}

void CanvasToolStateOverlay::setCanvasResizeInfo(const QSize& oldSize, const QSize& newSize)
{
    const auto formatSize = [](const QSize& size) {
        return QStringLiteral("%1 x %2").arg(qMax(0, size.width())).arg(qMax(0, size.height()));
    };

    const QString oldText = formatSize(oldSize);
    const QString newText = formatSize(newSize);

    if (m_canvasResizeOldSizeValueLabel) {
        m_canvasResizeOldSizeValueLabel->setText(oldText);
    }
    if (m_canvasResizeNewSizeValueLabel) {
        m_canvasResizeNewSizeValueLabel->setText(newText);
    }

    syncStackSizes();
    updateOverlaySize();
    update();
}

void CanvasToolStateOverlay::setToolPageIndex(int index)
{
    if (!m_toolContentStack || index < 0 || index >= m_toolContentStack->count()) {
        return;
    }

    const QSize currentSize = toolPageNaturalSize(toolPageIndex());
    const QSize targetSize = toolPageNaturalSize(index);
    m_toolPageSizeIndexOverride = index;
    m_toolContentStack->setFixedSize(currentSize.expandedTo(targetSize));
    updateVisibleContentGeometry(
        qMax(1, width() - ThemeManager::instance().scaled(kPanelHorizontalPaddingBase) * 2),
        qMax(1, height() - ThemeManager::instance().scaled(kPanelPaddingBase) * 2));
    updateOverlaySize();
    m_toolContentStack->setCurrentIndex(index);
}

int CanvasToolStateOverlay::toolPageIndex() const
{
    return m_toolContentStack ? m_toolContentStack->currentIndex() : -1;
}

void CanvasToolStateOverlay::connectParameterSlider(ProgressHandleSlider* slider,
    QElapsedTimer& timer, void (CanvasToolStateOverlay::*changeSignal)(qreal))
{
    if (!slider) {
        return;
    }

    auto* timerPtr = &timer;
    connect(slider, &ProgressHandleSlider::sliderPressed, this,
        [timerPtr]() { timerPtr->invalidate(); });
    connect(slider, &ProgressHandleSlider::valueChanged, this,
        [this, timerPtr, changeSignal](int value) {
            if (shouldEmitParameterSliderUpdate(*timerPtr, false)) {
                (this->*changeSignal)(sliderValueToUnit(value));
            }
        });
    connect(slider, &ProgressHandleSlider::sliderReleased, this,
        [this, slider, timerPtr, changeSignal]() {
            if (shouldEmitParameterSliderUpdate(*timerPtr, true)) {
                (this->*changeSignal)(sliderValueToUnit(slider->value()));
            }
        });
}

bool CanvasToolStateOverlay::shouldEmitParameterSliderUpdate(QElapsedTimer& timer, bool force)
{
    if (force) {
        if (timer.isValid()) {
            timer.restart();
        } else {
            timer.start();
        }
        return true;
    }

    if (!timer.isValid()) {
        timer.start();
        return true;
    }

    if (timer.elapsed() >= kSliderEmitIntervalMs) {
        timer.restart();
        return true;
    }

    return false;
}

qreal CanvasToolStateOverlay::sliderValueToUnit(int value)
{
    return qBound(0.0, value / 100.0, 1.0);
}

void CanvasToolStateOverlay::setupUi()
{
    const QStringList toolNames
        = { tr("Hand"), tr("Brush"), tr("Eraser"), tr("Fill"), tr("Eyedropper"), tr("Lasso"),
              tr("Lasso Fill"), tr("Square Selection"), tr("Circle Selection"), tr("Move"),
              tr("Rotate View"), tr("Canvas Resize"), tr("Zoom"), tr("Classic Fill"), tr("Blur") };

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_canvasModeStack = new AnimatedStackedWidget(this);
    m_canvasModeStack->setAttribute(Qt::WA_TranslucentBackground);
    m_canvasModeStack->setAutoFillBackground(false);
    m_canvasModeStack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_canvasModeStack->setSlideOrientation(AnimatedStackedWidget::SlideOrientation::Vertical);
    m_canvasModeStack->setSuspendLayoutDuringAnimation(true);
    m_canvasModeStack->setAnimationDuration(220);
    m_canvasModeStack->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    rootLayout->addWidget(m_canvasModeStack, 0, Qt::AlignCenter);

    m_interactivePage = new QWidget(m_canvasModeStack);
    m_interactivePage->setAttribute(Qt::WA_TranslucentBackground);
    m_interactivePage->setAutoFillBackground(false);
    m_interactivePage->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_leftSection = new QWidget(m_interactivePage);
    m_leftSection->setAttribute(Qt::WA_TranslucentBackground);
    m_leftSection->setAutoFillBackground(false);
    m_leftSection->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* leftLayout = new QHBoxLayout(m_leftSection);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(ThemeManager::instance().scaled(kControlSpacingBase));

    m_undoButton = createToolButton(IconProvider::StandardIcon::UndoArrow,
        workspace::ToolButton::Mode::Action, tr("Undo"), m_leftSection);
    m_undoButton->setEnabled(false);
    leftLayout->addWidget(m_undoButton);

    m_redoButton = createRedoToolButton(tr("Redo"), m_leftSection);
    m_redoButton->setEnabled(false);
    leftLayout->addWidget(m_redoButton);

    m_flipHorizontalButton = createToolButton(IconProvider::StandardIcon::FlipHorizontal,
        workspace::ToolButton::Mode::Toggle, tr("Mirror canvas horizontally"), m_leftSection);
    leftLayout->addWidget(m_flipHorizontalButton);

    m_flipVerticalButton = createToolButton(IconProvider::StandardIcon::FlipVertical,
        workspace::ToolButton::Mode::Toggle, tr("Mirror canvas vertically"), m_leftSection);
    leftLayout->addWidget(m_flipVerticalButton);

    m_leftSeparator = createSectionSeparator(m_interactivePage);

    m_toolViewport = new QWidget(m_interactivePage);
    m_toolViewport->setAttribute(Qt::WA_TranslucentBackground);
    m_toolViewport->setAutoFillBackground(false);
    m_toolViewport->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_toolContentStack = new AnimatedStackedWidget(m_toolViewport);
    m_toolContentStack->setAttribute(Qt::WA_TranslucentBackground);
    m_toolContentStack->setAutoFillBackground(false);
    m_toolContentStack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_toolContentStack->setSlideOrientation(AnimatedStackedWidget::SlideOrientation::Vertical);
    m_toolContentStack->setPreservePageSize(true);
    m_toolContentStack->setSuspendLayoutDuringAnimation(true);
    m_toolContentStack->setAnimationDuration(220);
    m_toolContentStack->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    for (int i = 0; i < kToolPageCount; ++i) {
        const QString toolName = i < toolNames.size() ? toolNames[i] : tr("Tool");
        if (i == kBrushToolPageIndex) {
            m_toolContentStack->addWidget(createBrushSettingsPage(toolName, &m_brushHardnessSlider,
                &m_brushFlowSlider, &m_brushEraserToggleButton, m_toolContentStack));
        } else if (i == kEraserToolPageIndex) {
            m_toolContentStack->addWidget(createBrushSettingsPage(toolName, &m_eraserHardnessSlider,
                &m_eraserFlowSlider, nullptr, m_toolContentStack));
        } else if (i == kBlurToolPageIndex) {
            m_toolContentStack->addWidget(
                createSingleParameterPage(toolName, QObject::tr("Intensity:"),
                    QObject::tr("intensity"), &m_blurIntensitySlider, m_toolContentStack));
        } else if (i == kSmudgeToolPageIndex) {
            m_toolContentStack->addWidget(createSmudgeSettingsPage(
                toolName, &m_smudgeIntensitySlider, &m_smudgeWetMixSlider, m_toolContentStack));
        } else if (i == kLiquifyToolPageIndex) {
            m_toolContentStack->addWidget(createLiquifySettingsPage(
                toolName, &m_liquifyStrengthSlider, m_liquifyModeButtons, m_toolContentStack));
        } else if (i == kLassoToolPageIndex) {
            m_toolContentStack->addWidget(
                createSingleParameterPage(toolName, QObject::tr("Stabilization:"),
                    QObject::tr("stabilization"), &m_lassoStabilizationSlider, m_toolContentStack));
        } else if (i == kLassoFillToolPageIndex) {
            m_toolContentStack->addWidget(createSingleParameterPage(toolName,
                QObject::tr("Stabilization:"), QObject::tr("stabilization"),
                &m_lassoFillStabilizationSlider, m_toolContentStack));
        } else if (i == 11) {
            m_toolContentStack->addWidget(
                createCanvasResizeInfoPage(&m_canvasResizeOldSizeValueLabel,
                    &m_canvasResizeNewSizeValueLabel, m_toolContentStack));
        } else if (i == kFillToolPageIndex || i == kEyedropperToolPageIndex
            || i == kSquareSelectionToolPageIndex || i == kCircleSelectionToolPageIndex
            || i == kRotateViewToolPageIndex || i == kZoomToolPageIndex
            || i == kClassicFillToolPageIndex) {
            m_toolContentStack->addWidget(createToolPlaceholderPage(nullptr, m_toolContentStack));
        } else {
            m_toolContentStack->addWidget(createToolContentPage(toolName, m_toolContentStack));
        }
    }

    m_rightSeparator = createSectionSeparator(m_interactivePage);

    m_rightSection = new QWidget(m_interactivePage);
    m_rightSection->setAttribute(Qt::WA_TranslucentBackground);
    m_rightSection->setAutoFillBackground(false);
    m_rightSection->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    auto* rightLayout = new QHBoxLayout(m_rightSection);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(ThemeManager::instance().scaled(kControlSpacingBase));

    m_copyCanvasButton = createToolButton(IconProvider::StandardIcon::Camera,
        workspace::ToolButton::Mode::Action, tr("Copy canvas to clipboard"), m_rightSection);
    rightLayout->addWidget(m_copyCanvasButton);

    m_canvasModeStack->addWidget(m_interactivePage);
    m_canvasModeStack->addWidget(
        createCanvasPlaceholderPage(&m_canvasPlaceholderLabel, m_canvasModeStack));

    connect(m_toolContentStack, &AnimatedStackedWidget::currentChanged, this, [this](int) {
        m_toolPageSizeIndexOverride = -1;
        syncStackSizes();
        updateOverlaySize();
    });
    connect(m_canvasModeStack, &AnimatedStackedWidget::currentChanged, this, [this](int) {
        m_canvasPageSizeIndexOverride = -1;
        syncStackSizes();
        updateOverlaySize();
    });
    connect(m_undoButton, &QAbstractButton::clicked, this, [this]() { emit undoRequested(); });
    connect(m_redoButton, &QAbstractButton::clicked, this, [this]() { emit redoRequested(); });
    connect(m_copyCanvasButton, &QAbstractButton::clicked, this,
        [this]() { emit copyCanvasRequested(); });
    connect(m_flipHorizontalButton, &QAbstractButton::toggled, this,
        &CanvasToolStateOverlay::canvasFlipHorizontalRequested);
    connect(m_flipVerticalButton, &QAbstractButton::toggled, this,
        &CanvasToolStateOverlay::canvasFlipVerticalRequested);
    if (m_brushEraserToggleButton) {
        connect(m_brushEraserToggleButton, &QAbstractButton::toggled, this,
            &CanvasToolStateOverlay::brushEraserModeToggled);
    }
    connectParameterSlider(m_brushHardnessSlider, m_brushHardnessSliderEmitTimer,
        &CanvasToolStateOverlay::brushHardnessChanged);
    connectParameterSlider(
        m_brushFlowSlider, m_brushFlowSliderEmitTimer, &CanvasToolStateOverlay::brushFlowChanged);
    connectParameterSlider(m_eraserHardnessSlider, m_eraserHardnessSliderEmitTimer,
        &CanvasToolStateOverlay::brushHardnessChanged);
    connectParameterSlider(
        m_eraserFlowSlider, m_eraserFlowSliderEmitTimer, &CanvasToolStateOverlay::brushFlowChanged);
    connectParameterSlider(m_blurIntensitySlider, m_blurIntensitySliderEmitTimer,
        &CanvasToolStateOverlay::blurIntensityChanged);
    connectParameterSlider(m_smudgeIntensitySlider, m_smudgeIntensitySliderEmitTimer,
        &CanvasToolStateOverlay::smudgeIntensityChanged);
    connectParameterSlider(m_smudgeWetMixSlider, m_smudgeWetMixSliderEmitTimer,
        &CanvasToolStateOverlay::smudgeWetMixChanged);
    connectParameterSlider(m_liquifyStrengthSlider, m_liquifyStrengthSliderEmitTimer,
        &CanvasToolStateOverlay::liquifyStrengthChanged);

    m_liquifyModeGroup = new QButtonGroup(this);
    m_liquifyModeGroup->setExclusive(true);
    for (int i = 0; i < kLiquifyModeCount; ++i) {
        if (m_liquifyModeButtons[i]) {
            m_liquifyModeGroup->addButton(m_liquifyModeButtons[i], i);
        }
    }
    if (m_liquifyModeButtons[0]) {
        m_liquifyModeButtons[0]->setChecked(true);
    }
    connect(m_liquifyModeGroup, &QButtonGroup::idClicked, this,
        [this](int id) { emit liquifyModeChanged(id); });
    connectParameterSlider(m_lassoStabilizationSlider, m_lassoStabilizationSliderEmitTimer,
        &CanvasToolStateOverlay::lassoStabilizationChanged);
    connectParameterSlider(m_lassoFillStabilizationSlider, m_lassoFillStabilizationSliderEmitTimer,
        &CanvasToolStateOverlay::lassoFillStabilizationChanged);

    m_toolContentStack->setCurrentIndex(0);
    m_canvasModeStack->setCurrentIndex(0);
    setCanvasPlaceholderText(tr("Parameters for this canvas mode are not available yet."));
}

void CanvasToolStateOverlay::applyThemeMetrics()
{
    auto& theme = ThemeManager::instance();
    auto& colors = WidgetStyleManager::instance().colors();
    const int panelPadding = theme.scaled(kPanelPaddingBase);
    const int sidePadding = theme.scaled(kPanelHorizontalPaddingBase);

    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout())) {
        rootLayout->setContentsMargins(sidePadding, panelPadding, sidePadding, panelPadding);
        rootLayout->setSpacing(0);
    }

    const auto boxLayouts = findChildren<QBoxLayout*>();
    for (QBoxLayout* boxLayout : boxLayouts) {
        if (boxLayout == layout()) {
            continue;
        }
        boxLayout->setSpacing(theme.scaled(kSectionSpacingBase));
    }

    const auto sliders = findChildren<OpacitySliderWidget*>();
    for (OpacitySliderWidget* slider : sliders) {
        slider->setMinimumWidth(theme.scaled(kSliderMinWidthBase));
    }

    const auto parameterSliders = findChildren<ProgressHandleSlider*>();
    for (ProgressHandleSlider* slider : parameterSliders) {
        applyOverlaySliderStyle(slider);
        if (slider->orientation() == Qt::Horizontal) {
            slider->setMinimumWidth(theme.scaled(kSliderMinWidthBase));
            slider->setFixedHeight(theme.scaled(kParameterSliderHeightBase));
        }
    }

    const auto separators = findChildren<Separator*>();
    for (Separator* separator : separators) {
        separator->setFixedHeight(theme.scaled(kSectionSeparatorHeightBase));
    }

    const auto parameterLabels = findChildren<QLabel*>(QStringLiteral("parameter-label"));
    for (QLabel* label : parameterLabels) {
        label->setStyleSheet(QStringLiteral("background: transparent; color: rgb(%1, %2, %3);")
                .arg(colors.text.red())
                .arg(colors.text.green())
                .arg(colors.text.blue()));
    }

    const auto canvasResizeInfoTitles
        = findChildren<QLabel*>(QStringLiteral("canvas-resize-info-title"));
    for (QLabel* label : canvasResizeInfoTitles) {
        label->setStyleSheet(
            QStringLiteral("background: transparent; color: rgba(%1, %2, %3, 190);")
                .arg(colors.textMuted.red())
                .arg(colors.textMuted.green())
                .arg(colors.textMuted.blue()));
    }

    const auto canvasResizeInfoValues
        = findChildren<QLabel*>(QStringLiteral("canvas-resize-info-value"));
    for (QLabel* label : canvasResizeInfoValues) {
        label->setStyleSheet(QStringLiteral("background: transparent; color: rgb(%1, %2, %3);")
                .arg(colors.text.red())
                .arg(colors.text.green())
                .arg(colors.text.blue()));
    }

    const auto toolPlaceholderLabels
        = findChildren<QLabel*>(QStringLiteral("tool-placeholder-label"));
    for (QLabel* label : toolPlaceholderLabels) {
        label->setMinimumWidth(theme.scaled(kToolPlaceholderMinWidthBase));
        label->setStyleSheet(
            QStringLiteral("background: transparent; color: rgba(%1, %2, %3, 190);")
                .arg(colors.textMuted.red())
                .arg(colors.textMuted.green())
                .arg(colors.textMuted.blue()));
    }

    if (m_canvasPlaceholderLabel) {
        m_canvasPlaceholderLabel->setMinimumWidth(theme.scaled(kCanvasPlaceholderMinWidthBase));
        m_canvasPlaceholderLabel->setStyleSheet(
            QStringLiteral("background: transparent; color: rgba(%1, %2, %3, 190);")
                .arg(colors.textMuted.red())
                .arg(colors.textMuted.green())
                .arg(colors.textMuted.blue()));
    }

    syncStackSizes();
    updateOverlaySize();
}

QSize CanvasToolStateOverlay::toolPageNaturalSize(int index) const
{
    if (!m_toolContentStack || index < 0 || index >= m_toolContentStack->count()) {
        const int bodyHeight = ThemeManager::instance().scaled(kMinimumContentHeightBase);
        return QSize(0, bodyHeight);
    }

    auto& theme = ThemeManager::instance();
    const int bodyHeight = theme.scaled(kMinimumContentHeightBase);
    QSize size = pageHint(m_toolContentStack->widget(index));
    size.setHeight(qMax(bodyHeight, size.height()));
    return size;
}

QSize CanvasToolStateOverlay::interactiveNaturalSize(int toolIndex) const
{
    auto& theme = ThemeManager::instance();
    const int bodyHeight = theme.scaled(kMinimumContentHeightBase);
    const int sectionSpacing = theme.scaled(kSectionSpacingBase);

    const QSize leftSize = pageHint(m_leftSection);
    const QSize rightSize = pageHint(m_rightSection);
    const QSize toolSize = toolPageNaturalSize(toolIndex);

    const int separatorWidth = (m_leftSeparator ? m_leftSeparator->width() : 0)
        + (m_rightSeparator ? m_rightSeparator->width() : 0);
    const int totalSpacing = sectionSpacing * 4;
    const int contentWidth
        = leftSize.width() + separatorWidth + rightSize.width() + totalSpacing + toolSize.width();
    const int contentHeight
        = qMax(bodyHeight, qMax(toolSize.height(), qMax(leftSize.height(), rightSize.height())));

    return QSize(contentWidth, contentHeight);
}

QSize CanvasToolStateOverlay::canvasPageNaturalSize(int index) const
{
    auto& theme = ThemeManager::instance();
    const int bodyHeight = theme.scaled(kMinimumContentHeightBase);

    if (!m_canvasModeStack || index < 0 || index >= m_canvasModeStack->count()) {
        return QSize(0, bodyHeight);
    }

    if (m_canvasModeStack->widget(index) == m_interactivePage) {
        const int toolIndex
            = m_toolPageSizeIndexOverride >= 0 ? m_toolPageSizeIndexOverride : toolPageIndex();
        return interactiveNaturalSize(toolIndex);
    }

    QSize size = pageHint(m_canvasModeStack->widget(index));
    size.setHeight(qMax(bodyHeight, size.height()));
    return size;
}

void CanvasToolStateOverlay::updateVisibleContentGeometry(int contentWidth, int contentHeight)
{
    const int visibleWidth = qMax(1, contentWidth);
    const int visibleHeight = qMax(1, contentHeight);

    if (m_canvasModeStack) {
        m_canvasModeStack->setFixedSize(visibleWidth, visibleHeight);
    }

    updateInteractivePageLayout(visibleWidth, visibleHeight);
}

void CanvasToolStateOverlay::updateInteractivePageLayout(int contentWidth, int contentHeight)
{
    if (!m_interactivePage || !m_leftSection || !m_rightSection || !m_toolViewport
        || !m_toolContentStack || !m_leftSeparator || !m_rightSeparator) {
        return;
    }

    auto& theme = ThemeManager::instance();
    const int sectionSpacing = theme.scaled(kSectionSpacingBase);

    const QSize leftSize = pageHint(m_leftSection);
    const QSize rightSize = pageHint(m_rightSection);
    const QSize currentStackSize = m_toolContentStack->size();
    const QSize toolSize = (currentStackSize.width() > 0 && currentStackSize.height() > 0)
        ? currentStackSize
        : toolPageNaturalSize(toolPageIndex());

    const int separatorWidth = m_leftSeparator->width() + m_rightSeparator->width();
    const int fixedWidth
        = leftSize.width() + rightSize.width() + separatorWidth + sectionSpacing * 4;
    const int viewportWidth = qMax(0, contentWidth - fixedWidth);
    const int pageHeight
        = qMax(contentHeight, qMax(toolSize.height(), qMax(leftSize.height(), rightSize.height())));

    m_interactivePage->setFixedSize(contentWidth, pageHeight);

    int x = 0;
    m_leftSection->setGeometry(
        x, (pageHeight - leftSize.height()) / 2, leftSize.width(), leftSize.height());
    x += leftSize.width() + sectionSpacing;

    m_leftSeparator->setGeometry(x, (pageHeight - m_leftSeparator->height()) / 2,
        m_leftSeparator->width(), m_leftSeparator->height());
    x += m_leftSeparator->width() + sectionSpacing;

    m_toolViewport->setGeometry(x, 0, viewportWidth, pageHeight);
    const int toolX = (viewportWidth - toolSize.width()) / 2;
    const int toolY = (pageHeight - toolSize.height()) / 2;
    m_toolContentStack->move(toolX, toolY);
    x += viewportWidth + sectionSpacing;

    m_rightSeparator->setGeometry(x, (pageHeight - m_rightSeparator->height()) / 2,
        m_rightSeparator->width(), m_rightSeparator->height());
    x += m_rightSeparator->width() + sectionSpacing;

    m_rightSection->setGeometry(
        x, (pageHeight - rightSize.height()) / 2, rightSize.width(), rightSize.height());
}

void CanvasToolStateOverlay::syncStackSizes()
{
    auto& theme = ThemeManager::instance();
    const int bodyHeight = theme.scaled(kMinimumContentHeightBase);

    if (m_toolContentStack) {
        const int toolIndex
            = m_toolPageSizeIndexOverride >= 0 ? m_toolPageSizeIndexOverride : toolPageIndex();
        QSize toolSize = toolPageNaturalSize(toolIndex);
        if (m_widthAnimation && m_widthAnimation->state() == QAbstractAnimation::Running) {
            toolSize = toolSize.expandedTo(m_toolContentStack->size());
        }
        m_toolContentStack->setFixedSize(toolSize);
    }

    const int sidePadding = theme.scaled(kPanelHorizontalPaddingBase);
    const QSize naturalCanvasSize = canvasPageNaturalSize(
        m_canvasPageSizeIndexOverride >= 0 ? m_canvasPageSizeIndexOverride : canvasPageIndex());
    const int visibleContentWidth = qMax(
        1, (width() > 0 ? width() : naturalCanvasSize.width() + sidePadding * 2) - sidePadding * 2);
    updateVisibleContentGeometry(visibleContentWidth, qMax(bodyHeight, naturalCanvasSize.height()));
}

int CanvasToolStateOverlay::animatedWidth() const
{
    return width();
}

void CanvasToolStateOverlay::setAnimatedWidth(int widthValue)
{
    const int newWidth = qMax(1, widthValue);
    const int currentHeight = height();
    const int newX = qRound(m_widthAnimationAnchorCenterX - newWidth / 2.0);

    setFixedSize(newWidth, currentHeight);
    move(newX, y());
    updateVisibleContentGeometry(
        qMax(1, newWidth - ThemeManager::instance().scaled(kPanelHorizontalPaddingBase) * 2),
        qMax(1, currentHeight - ThemeManager::instance().scaled(kPanelPaddingBase) * 2));
    updateGeometry();
    update();
    if (m_backdropSource) {
        m_backdropSource->requestBackdropUpdate();
    }
}

void CanvasToolStateOverlay::animateOverlayWidth(int targetWidth)
{
    if (!m_widthAnimation) {
        return;
    }

    if (m_widthAnimation->state() == QAbstractAnimation::Running) {
        m_widthAnimation->stop();
    }

    m_widthAnimationAnchorCenterX = x() + width() / 2.0;
    m_pendingAnimatedSize = QSize(targetWidth, height());
    m_widthAnimation->setStartValue(width());
    m_widthAnimation->setEndValue(targetWidth);
    m_widthAnimation->start();
}

void CanvasToolStateOverlay::updateOverlaySize()
{
    auto& theme = ThemeManager::instance();
    const int panelPadding = theme.scaled(kPanelPaddingBase);
    const int sidePadding = theme.scaled(kPanelHorizontalPaddingBase);

    if (layout()) {
        layout()->activate();
    }

    const QSize targetContentSize = canvasPageNaturalSize(
        m_canvasPageSizeIndexOverride >= 0 ? m_canvasPageSizeIndexOverride : canvasPageIndex());
    const int contentWidth = targetContentSize.width();
    const int contentHeight
        = qMax(theme.scaled(kMinimumContentHeightBase), targetContentSize.height());
    const QSize newSize(
        qMax(1, contentWidth + sidePadding * 2), qMax(1, contentHeight + panelPadding * 2));
    m_lastAppliedSize = newSize;

    // A width animation toward this exact target is effectively "already applying"
    // newSize; setAnimatedWidth() keeps the content laid out each tick, so just
    // refresh and let it land.
    const bool animatingToTarget = m_widthAnimation
        && m_widthAnimation->state() == QAbstractAnimation::Running
        && m_pendingAnimatedSize == newSize;
    // Only skip the resize when the widget ACTUALLY has newSize. Trusting the
    // cached size alone is unsafe: the real geometry can diverge from it (e.g. an
    // interrupted width animation in setCanvasPageIndex() leaves the strip at an
    // intermediate width without finalizing to the target). If we early-returned
    // on the stale cache, the strip would stay too narrow and clip the tool
    // content (a tool's sliders get cut off inside m_toolViewport).
    const bool sizeAlreadyApplied = (size() == newSize);

    if (animatingToTarget || sizeAlreadyApplied) {
        updateVisibleContentGeometry(qMax(1, width() - sidePadding * 2), contentHeight);
        update();
        return;
    }

    if (!isVisible() || width() <= 0 || height() != newSize.height()) {
        if (m_widthAnimation && m_widthAnimation->state() == QAbstractAnimation::Running) {
            m_widthAnimation->stop();
        }
        m_pendingAnimatedSize = QSize();
        setFixedSize(newSize);
        updateVisibleContentGeometry(contentWidth, contentHeight);
        updateGeometry();
        update();
        if (m_backdropSource) {
            m_backdropSource->requestBackdropUpdate();
        }
        emit sizeChanged(newSize);
        return;
    }

    animateOverlayWidth(newSize.width());
}

void CanvasToolStateOverlay::drawBackground(QPainter& painter)
{
    auto& mgr = WidgetStyleManager::instance();

    QColor borderColor = mgr.colors().border;
    borderColor.setAlphaF(borderColor.alphaF() * 0.5);

    // Capsule: rounded ends with radius = half the strip height.
    const int radius = height() / 2;

    QPainterPath bgPath;
    bgPath.addRoundedRect(rect(), radius, radius);

    painter.setPen(Qt::NoPen);

    QColor tint = mgr.colors().surface;
    tint.setAlpha(ruwa::ui::painting::kBackdropTintAlpha);
    if (!ruwa::ui::painting::drawBackdropBlurTint(painter, this, m_backdropSource, bgPath, tint)) {
        QColor bgColor = mgr.colors().surface;
        bgColor.setAlpha(200);
        painter.setBrush(bgColor);
        painter.drawPath(bgPath);
    }

    ruwa::ui::painting::drawGradientBorder(painter, rect(), radius, borderColor, borderColor);
}

void CanvasToolStateOverlay::setBackdropSource(
    ruwa::shared::rendering::ICanvasBackdropSource* source)
{
    if (m_backdropSource == source) {
        return;
    }
    m_backdropSource = source;
    update();
}

void CanvasToolStateOverlay::moveEvent(QMoveEvent* event)
{
    QWidget::moveEvent(event);
    // Keep the GPU blur region and QWidget chrome on the same layout tick.
    if (m_backdropSource) {
        m_backdropSource->requestBackdropUpdate();
        update();
    }
}

void CanvasToolStateOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    drawBackground(painter);
}

void CanvasToolStateOverlay::mousePressEvent(QMouseEvent* event)
{
    QWidget::mousePressEvent(event);
}

void CanvasToolStateOverlay::mouseMoveEvent(QMouseEvent* event)
{
    QWidget::mouseMoveEvent(event);
}

void CanvasToolStateOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    QWidget::mouseReleaseEvent(event);
}

void CanvasToolStateOverlay::leaveEvent(QEvent* event)
{
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

void CanvasToolStateOverlay::onThemeChanged()
{
    applyThemeMetrics();
    if (m_backdropSource) {
        m_backdropSource->requestBackdropUpdate();
    }
}

} // namespace ruwa::ui::widgets
