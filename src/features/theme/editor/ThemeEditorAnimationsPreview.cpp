// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorAnimationsPreview.h"

#include "features/brush/manager/BrushManager.h"
#include "features/brush/ui/BrushSettingsPanel.h"
#include "features/brush/ui/BrushesPanel.h"
#include "features/settings/BaseSettingsWidget.h"
#include "features/settings/SettingsChoice.h"
#include "features/settings/SettingsComboBox.h"
#include "features/settings/SettingsToggle.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemePreset.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/SidebarButton.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/ToggleSwitch.h"
#include "shell/docking/widgets/DockGroupHeader.h"
#include "shell/docking/widgets/DockPanelTitleBar.h"

#include <QEasingCurve>
#include <QEvent>
#include <QHideEvent>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTimer>

#include <cmath>

namespace ruwa::ui::widgets {

namespace {

using ruwa::ui::core::IconProvider;

constexpr int kFrameIntervalMs = 16;
constexpr qreal kCycleSeconds = 10.0;

// Loop keyframes, in seconds of one cycle. The cursor rests to the right of the
// list, walks it down, comes back up to select the first entry, returns to rest,
// then dives straight to the last entry and leaves without stopping again.
constexpr qreal kDescendStart = 0.80;
constexpr qreal kDescendEnd = 3.40;
constexpr qreal kClimbStart = kDescendEnd;
constexpr qreal kClimbEnd = 4.60;
constexpr qreal kFirstClickTime = 4.75;
constexpr qreal kReturnStart = 5.10;
constexpr qreal kReturnEnd = 6.00;
constexpr qreal kDiveStart = 6.60;
constexpr qreal kDiveEnd = 8.10;
constexpr qreal kSecondClickTime = 8.20;
constexpr qreal kLeaveStart = 8.50;
constexpr qreal kLeaveEnd = 9.50;
constexpr qreal kClickPulseSeconds = 0.45;

// The settings column runs its own loop: one control operates every two
// seconds, in the order dropdown, toggle, switcher, and undoes itself two
// seconds later.
constexpr qreal kSettingsCycleSeconds = 12.0;
constexpr qreal kDropdownOpenAt = 0.0;
constexpr qreal kDropdownCloseAt = 2.0;
constexpr qreal kToggleOnAt = 4.0;
constexpr qreal kToggleOffAt = 6.0;
constexpr qreal kSwitcherOnAt = 8.0;
constexpr qreal kSwitcherOffAt = 10.0;

// Authored durations of the controls being imitated, copied from the widgets
// themselves (AnimatedComboBox popup fade, ToggleSwitch thumb, segmented
// indicator) so the preview ages with them.
constexpr int kPopupShowMs = 120;
constexpr int kPopupHideMs = 80;
constexpr int kThumbMs = 200;
constexpr int kSegmentedMs = 240;

constexpr int kListLeftMargin = 40;
constexpr int kListWidth = 220;
constexpr int kItemGap = 8;
constexpr int kDividerSpacing = 12;
constexpr int kSettingsRightMargin = 48;
constexpr int kSettingsWidth = 420;
constexpr int kSettingsMinWidth = 260;
constexpr int kSettingsRowGap = 10;
// The grouped dock swaps members every three seconds, so its loop is six.
constexpr qreal kGroupCycleSeconds = 6.0;
constexpr qreal kGroupSwapAt = 3.0;
// Same slide the real DockGroupHost plays when a group swaps members.
constexpr int kGroupSlideMs = 350;
constexpr int kGroupWidth = 430;
constexpr int kGroupMinWidth = 320;
constexpr int kGroupTopMargin = 54;
/// The group is drawn zoomed: it reads a little larger than life, and since the
/// banner height does not follow, more of the panel runs off the bottom edge.
constexpr qreal kGroupScale = 1.12;
/// Fraction of the panel body the banner shows; the rest runs off the bottom.
constexpr qreal kGroupVisibleFraction = 1.0 / 3.0;

constexpr int kPopupMaxHeight = 150;
constexpr int kPopupGap = 4;
constexpr int kPopupSlide = 12;
constexpr int kCursorSize = 26;
constexpr qreal kCursorHotspot = 0.09;

// Authored response of the entry states to the cursor. These are the only two
// timings the edited preset governs: the walk itself is a fixed 10 s so that
// changing the multiplier never rescales the scene the user is reading.
constexpr qreal kHoverTauMs = 110.0;
constexpr qreal kActiveTauMs = 150.0;

QPixmap tintedPixmap(const QPixmap& source, const QColor& color)
{
    if (source.isNull()) {
        return source;
    }

    QPixmap tinted(source.size());
    tinted.setDevicePixelRatio(source.devicePixelRatio());
    tinted.fill(Qt::transparent);

    QPainter painter(&tinted);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();
    return tinted;
}

qreal easeOutCubic(qreal progress)
{
    static const QEasingCurve curve(QEasingCurve::OutCubic);
    return curve.valueForProgress(qBound(0.0, progress, 1.0));
}

qreal easeInOutCubic(qreal progress)
{
    static const QEasingCurve curve(QEasingCurve::InOutCubic);
    return curve.valueForProgress(qBound(0.0, progress, 1.0));
}

void makeWidgetTreePassive(QWidget* root)
{
    if (!root) {
        return;
    }

    root->setFocusPolicy(Qt::NoFocus);
    root->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    const auto children = root->findChildren<QWidget*>();
    for (QWidget* child : children) {
        child->setFocusPolicy(Qt::NoFocus);
        child->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void applyPresentationTheme(
    QWidget* row, const ruwa::ui::core::ThemeColors& colors, const QPalette& palette)
{
    if (!row) {
        return;
    }

    row->setPalette(palette);
    QFont rowFont = row->font();
    rowFont.setFamily(colors.fonts.uiFont);
    row->setFont(rowFont);

    const auto children = row->findChildren<QWidget*>();
    for (QWidget* child : children) {
        QFont childFont = child->font();
        childFont.setFamily(colors.fonts.uiFont);
        child->setFont(childFont);
    }
    const auto labels = row->findChildren<QLabel*>();
    for (QLabel* label : labels) {
        label->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name()));
    }
}

} // namespace

ThemeEditorAnimationsPreview::ThemeEditorAnimationsPreview(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_StyledBackground, false);

    m_previewColors = ruwa::ui::core::ThemeManager::instance().colors();

    setupList();
    setupSettings();
    setupGroup();

    m_frameTimer = new QTimer(this);
    m_frameTimer->setInterval(kFrameIntervalMs);
    connect(m_frameTimer, &QTimer::timeout, this, &ThemeEditorAnimationsPreview::onTick);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ThemeEditorAnimationsPreview::updateTheme);

    updateTheme();
}

ThemeEditorAnimationsPreview::~ThemeEditorAnimationsPreview() = default;

void ThemeEditorAnimationsPreview::setPreset(const ruwa::ui::core::ThemePreset& preset)
{
    m_previewColors = ruwa::ui::core::ThemeManager::colorsForPreset(preset);
    m_animationsEnabled = preset.animations.enabled;
    m_animationSpeed = qBound(ruwa::ui::core::WidgetStyleManager::kMinAnimationSpeed,
        preset.animations.speed, ruwa::ui::core::WidgetStyleManager::kMaxAnimationSpeed);
    updateTheme();
}

void ThemeEditorAnimationsPreview::setupList()
{
    for (std::size_t index = 0; index < ItemCount; ++index) {
        auto* button = new SidebarButton(QString(), QIcon(), this);
        button->setFocusPolicy(Qt::NoFocus);
        button->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        button->show();
        m_items[index].button = button;
    }
    retranslatePreview();
}

void ThemeEditorAnimationsPreview::setupSettings()
{
    // Scenery rows first, then the three the loop operates, then more scenery:
    // the column is taller than the banner, so both ends stay clipped.
    m_settings[0].widget = new SettingsToggle(QString(), QString(), true, this);
    m_settings[1].widget
        = new SettingsChoice(QString(), QString(), { QString(), QString() }, 0, this);
    m_dropdownRow
        = new SettingsComboBox(QString(), QString(), { QString(), QString(), QString() }, 0, this);
    m_settings[DropdownRow].widget = m_dropdownRow;
    m_toggleRow = new SettingsToggle(QString(), QString(), false, this);
    m_settings[ToggleRow].widget = m_toggleRow;
    m_switcherRow = new SettingsChoice(QString(), QString(), { QString(), QString() }, 0, this);
    m_settings[SwitcherRow].widget = m_switcherRow;
    m_settings[5].widget = new SettingsToggle(QString(), QString(), true, this);
    m_settings[6].widget
        = new SettingsComboBox(QString(), QString(), { QString(), QString() }, 0, this);

    for (SettingRow& row : m_settings) {
        if (row.widget) {
            row.widget->show();
        }
    }

    // The rows wrap their controls; reach them once and drive them by hand, so
    // none of their own animations (which read the applied theme) ever runs.
    m_dropdownCombo = m_dropdownRow->findChild<AnimatedComboBox*>();
    m_toggleSwitch = m_toggleRow->findChild<ToggleSwitch*>();
    m_switcher = m_switcherRow->findChild<SegmentedOptionSelector*>();

    // Park the operated controls in their "off" state; the loop takes over from
    // the next frame and never routes through their own animations again.
    if (m_toggleSwitch) {
        m_toggleSwitch->setCheckedInstant(false);
    }
    if (m_switcher) {
        m_switcher->setCurrentIndex(0, false);
    }

    retranslatePreview();
    makeWidgetTreePassive(this);
}

void ThemeEditorAnimationsPreview::setupGroup()
{
    m_brushesPanel = new ruwa::ui::workspace::BrushesPanel(this);
    m_brushSettingsPanel = new ruwa::ui::workspace::BrushSettingsPanel(this);

    m_brushesPanel->setGrouped(true);
    m_brushSettingsPanel->setGrouped(true);

    m_groupHeader = new ruwa::ui::docking::DockGroupHeader(this);
    m_groupHeader->setPanels({ m_brushesPanel, m_brushSettingsPanel });
    m_groupHeader->setCurrentPanel(m_brushesPanel);
    m_groupHeader->setCornerRadius(m_brushesPanel->baseCornerRadius());

    m_groupHeader->show();
    m_brushesPanel->show();
    m_brushSettingsPanel->show();

    // Brush previews arrive after their panel is built, and the snapshot taken
    // before they land would keep showing empty tiles forever.
    const auto invalidate = [this]() {
        m_panelSnapshotsDirty = true;
        update();
    };
    connect(m_brushesPanel, &ruwa::ui::workspace::BrushesPanel::visiblePreviewStateChanged, this,
        invalidate);
    connect(
        m_brushesPanel, &ruwa::ui::workspace::BrushesPanel::panelStateChanged, this, invalidate);

    makeWidgetTreePassive(this);
}

void ThemeEditorAnimationsPreview::retranslatePreview()
{
    const std::array<QString, ItemCount> labels { tr("Home"), tr("New Project"), tr("Brushes"),
        tr("Appearance"), tr("Settings"), tr("About") };
    const std::array<IconProvider::StandardIcon, ItemCount> icons {
        IconProvider::StandardIcon::Home, IconProvider::StandardIcon::FileNew,
        IconProvider::StandardIcon::Brush, IconProvider::StandardIcon::Appearance,
        IconProvider::StandardIcon::Settings, IconProvider::StandardIcon::TransparentLogoIcon
    };

    auto& iconProvider = IconProvider::instance();
    for (std::size_t index = 0; index < ItemCount; ++index) {
        if (!m_items[index].button) {
            continue;
        }
        m_items[index].button->setText(labels[index]);
        m_items[index].button->setIcon(iconProvider.getIcon(icons[index]));
    }

    const std::array<QString, SettingCount> rowLabels { tr("Smooth scrolling"),
        tr("Preview quality"), tr("Color profile"), tr("Use pen pressure"), tr("Stroke mode"),
        tr("Autosave"), tr("Thumbnail size") };
    for (std::size_t index = 0; index < SettingCount; ++index) {
        if (auto* row = qobject_cast<BaseSettingsWidget*>(m_settings[index].widget)) {
            row->setLabel(rowLabels[index]);
        }
    }
    if (auto* quality = qobject_cast<SettingsChoice*>(m_settings[1].widget)) {
        quality->retranslateUi(rowLabels[1], QString(), { tr("Balanced"), tr("Sharp") });
    }
    if (m_dropdownRow) {
        m_dropdownRow->setOptions({ tr("sRGB"), tr("Display P3"), tr("Adobe RGB") });
        m_dropdownRow->setSelectedIndex(0);
    }
    if (m_switcherRow) {
        m_switcherRow->retranslateUi(rowLabels[SwitcherRow], QString(), { tr("Soft"), tr("Hard") });
    }
    if (auto* thumbnails = qobject_cast<SettingsComboBox*>(m_settings[6].widget)) {
        thumbnails->setOptions({ tr("Medium"), tr("Large") });
        thumbnails->setSelectedIndex(0);
    }
}

void ThemeEditorAnimationsPreview::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslatePreview();
        update();
    }
}

void ThemeEditorAnimationsPreview::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    updateLayout();
    resetLoopState();
    m_clock.restart();
    m_frameTimer->start();

    // DockPanel builds its real content lazily from its own showEvent. Once
    // those controls exist, settle them and re-take the snapshots.
    QTimer::singleShot(0, this, [this]() {
        if (m_brushesPanel) {
            m_brushesPanel->prepareVisiblePreviews();
        }
        if (m_brushSettingsPanel) {
            // With no canvas there is no selection to follow, so stand one of
            // the stock brushes in for the one the user would have picked.
            auto& brushes = ruwa::core::brushes::BrushManager::instance();
            const auto presets = brushes.presets();
            if (!presets.isEmpty()) {
                const auto stock = brushes.brushesForPreset(presets.first().id);
                if (!stock.isEmpty()) {
                    m_brushSettingsPanel->showPresentationBrush(stock.first().id);
                }
            }
            m_brushSettingsPanel->prepareVisiblePreview();
            m_brushSettingsPanel->preparePresentationSnapshot();
        }
        updateTheme();
        makeWidgetTreePassive(this);
    });
}

void ThemeEditorAnimationsPreview::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_frameTimer->stop();
}

void ThemeEditorAnimationsPreview::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLayout();
}

void ThemeEditorAnimationsPreview::updateTheme()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    const int cursorSize = qMax(8, theme.scaled(kCursorSize));
    const QPixmap source = IconProvider::instance().getPixmap(
        QStringLiteral("CursorPointer"), QSize(cursorSize, cursorSize));
    m_cursorPixmap = tintedPixmap(source, m_previewColors.text);

    // The settings rows carry their own labels and palettes, so the edited
    // preset has to be pushed into them; the sidebar buttons resolve everything
    // from ThemeManager while they render under the colour override.
    const QPalette previewPalette
        = ruwa::ui::core::ThemeManager::paletteForColors(m_previewColors, palette());
    for (const SettingRow& row : m_settings) {
        applyPresentationTheme(row.widget, m_previewColors, previewPalette);
    }

    if (m_groupHeader) {
        m_groupHeader->applyTheme(m_previewColors);
        m_groupHeader->setPalette(previewPalette);
    }
    for (ruwa::ui::docking::DockPanel* panel :
        { static_cast<ruwa::ui::docking::DockPanel*>(m_brushesPanel),
            static_cast<ruwa::ui::docking::DockPanel*>(m_brushSettingsPanel) }) {
        if (!panel) {
            continue;
        }
        panel->setSubtitleBackground(m_previewColors.surface);
        if (panel->titleBar()) {
            panel->titleBar()->applyTheme(m_previewColors);
        }
        panel->setPalette(previewPalette);
    }
    m_panelSnapshotsDirty = true;

    updateLayout();
    update();
}

void ThemeEditorAnimationsPreview::updateLayout()
{
    if (size().isEmpty()) {
        return;
    }

    updateListLayout();
    updateSettingsLayout();
    updateGroupLayout();
    rebuildPaths();
    update();
}

void ThemeEditorAnimationsPreview::updateListLayout()
{
    if (!m_items[0].button) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int listWidth = theme.scaled(kListWidth);
    const int listLeft = theme.scaled(kListLeftMargin);
    const int itemHeight = qMax(theme.scaled(20), m_items[0].button->sizeHint().height());

    // Real sidebar spacing, with the list hung off the top edge: the first entry
    // is cut in half there, every entry below it sits whole in the banner.
    const int gap = theme.scaled(kItemGap);
    const int dividerThickness = qMax(1, theme.scaled(1));
    // The divider replaces one plain gap with the sidebar's own spacing around it.
    const int dividerBlock = theme.scaled(kDividerSpacing) * 2 + dividerThickness - gap;

    // Keep the real buttons alive and visible for QWidget::render(), but park
    // them beyond the parent's clip so only their rendered copy is displayed.
    const int sourceX = width() + theme.scaled(32);
    int centerY = 0;
    for (std::size_t index = 0; index < ItemCount; ++index) {
        if (index == DividedItem) {
            centerY += dividerBlock;
        }
        m_items[index].target = QRect(listLeft, centerY - itemHeight / 2, listWidth, itemHeight);
        if (m_items[index].button) {
            m_items[index].button->setGeometry(
                sourceX, int(index) * itemHeight, listWidth, itemHeight);
        }
        if (index + 1 == DividedItem) {
            m_dividerY = centerY + itemHeight / 2 + (dividerBlock + gap) / 2;
        }
        centerY += itemHeight + gap;
    }
}

void ThemeEditorAnimationsPreview::updateSettingsLayout()
{
    if (!m_settings[0].widget) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int listRight = theme.scaled(kListLeftMargin) + theme.scaled(kListWidth);
    const int available = qMax(theme.scaled(kSettingsMinWidth),
        width() - listRight - theme.scaled(kSettingsRightMargin) * 2);
    const int columnWidth = qMin(theme.scaled(kSettingsWidth), available);
    const int columnLeft = width() - theme.scaled(kSettingsRightMargin) - columnWidth;
    const int rowGap = theme.scaled(kSettingsRowGap);
    const int sourceX = width() + theme.scaled(32);

    // Lay the column out from zero first, then slide it so the three operated
    // rows sit centred in the banner and the ends fall off both edges.
    int cursorY = 0;
    int parkY = 0;
    for (SettingRow& row : m_settings) {
        if (!row.widget) {
            continue;
        }
        row.widget->setGeometry(sourceX, parkY, columnWidth, row.widget->sizeHint().height());
        if (row.widget->layout()) {
            row.widget->layout()->activate();
        }
        const int rowHeight = row.widget->height();
        row.target = QRect(columnLeft, cursorY, columnWidth, rowHeight);
        cursorY += rowHeight + rowGap;
        parkY += rowHeight + rowGap;
    }

    const QRect first = m_settings[DropdownRow].target;
    const QRect last = m_settings[SwitcherRow].target;
    const int operatedCenter = (first.top() + last.bottom()) / 2;
    const int shift = height() / 2 - operatedCenter;
    for (SettingRow& row : m_settings) {
        row.target.translate(0, shift);
    }

    if (m_dropdownCombo && isVisible()) {
        m_popupSize = m_dropdownCombo->preparePresentationPopup(theme.scaled(kPopupMaxHeight));
    }
}

void ThemeEditorAnimationsPreview::updateGroupLayout()
{
    if (!m_groupHeader || !m_brushesPanel || !m_brushSettingsPanel) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int listRight = theme.scaled(kListLeftMargin) + theme.scaled(kListWidth);
    const int settingsLeft
        = m_settings[DropdownRow].target.isNull() ? width() : m_settings[DropdownRow].target.left();
    const int available = qMax(theme.scaled(kGroupMinWidth), settingsLeft - listRight);
    const int drawnWidth = qMin(qRound(theme.scaled(kGroupWidth) * kGroupScale), available);
    const int sourceWidth = qMax(1, qRound(drawnWidth / kGroupScale));
    const int groupLeft = listRight + (available - drawnWidth) / 2;
    const int groupTop = theme.scaled(kGroupTopMargin);

    // Anchored top-centre: the zoom widens the group around its centre and eats
    // into what fits below, rather than moving where it starts.
    const int sourceHeaderHeight = m_groupHeader->barHeight();
    const int drawnHeaderHeight = qRound(sourceHeaderHeight * kGroupScale);
    const int drawnBodyHeight = qMax(1, height() - groupTop - drawnHeaderHeight);
    const int sourceBodyHeight = qMax(1, qRound(drawnBodyHeight / kGroupScale));
    // Only the top third is inside the banner; the panel itself is three times
    // as tall and runs off the bottom edge.
    const int panelHeight = qRound(sourceBodyHeight / kGroupVisibleFraction);

    m_groupHeaderTarget = QRect(groupLeft, groupTop, drawnWidth, drawnHeaderHeight);
    m_groupBodyTarget = QRect(groupLeft, groupTop + drawnHeaderHeight, drawnWidth, drawnBodyHeight);
    m_groupBodySource = QSize(sourceWidth, sourceBodyHeight);

    const int sourceX = width() + theme.scaled(32);
    m_groupHeader->setGeometry(sourceX, 0, sourceWidth, sourceHeaderHeight);
    m_brushesPanel->setGeometry(sourceX, sourceHeaderHeight, sourceWidth, panelHeight);
    m_brushSettingsPanel->setGeometry(sourceX, sourceHeaderHeight, sourceWidth, panelHeight);

    m_panelSnapshotsDirty = true;
}

void ThemeEditorAnimationsPreview::rebuildPanelSnapshots()
{
    m_panelSnapshotsDirty = false;
    if (m_groupBodyTarget.isEmpty() || !m_brushesPanel || !m_brushSettingsPanel) {
        m_panelSnapshots = {};
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const std::array<QWidget*, 2> panels { m_brushesPanel, m_brushSettingsPanel };
    auto& manager = ruwa::ui::core::ThemeManager::instance();

    for (std::size_t index = 0; index < panels.size(); ++index) {
        QWidget* panel = panels[index];
        panel->ensurePolished();

        QPixmap snapshot(qMax(1, qRound(m_groupBodyTarget.width() * dpr)),
            qMax(1, qRound(m_groupBodyTarget.height() * dpr)));
        snapshot.setDevicePixelRatio(dpr);
        snapshot.fill(Qt::transparent);

        QPainter snapshotPainter(&snapshot);
        snapshotPainter.setRenderHint(QPainter::Antialiasing);
        // Render the panel through the zoom rather than scaling the finished
        // pixmap: text, icons and borders are drawn at the larger size.
        snapshotPainter.scale(kGroupScale, kGroupScale);
        manager.withColorOverride(m_previewColors, [panel, &snapshotPainter, this]() {
            panel->render(&snapshotPainter, QPoint(),
                QRegion(0, 0, m_groupBodySource.width(), m_groupBodySource.height()),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
        });
        snapshotPainter.end();
        m_panelSnapshots[index] = std::move(snapshot);
    }
}

void ThemeEditorAnimationsPreview::rebuildPaths()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const QRect firstRect = m_items[FirstVisibleItem].target;
    const QRect lastRect = m_items[LastVisibleItem].target;
    const qreal listLeft = m_items[0].target.left();
    const qreal listWidth = m_items[0].target.width();
    const qreal span = qMax(1.0, qreal(height()));

    m_restPoint = QPointF(
        listLeft + listWidth + theme.scaled(52), firstRect.center().y() - theme.scaled(30));
    // Just past the last entry, so the descent sweeps over every one of them.
    m_bottomPoint = QPointF(listLeft + listWidth * 0.50,
        qMin(qreal(height() - theme.scaled(8)), qreal(lastRect.bottom() + theme.scaled(10))));

    const QPointF firstPoint = itemCursorPoint(FirstVisibleItem);
    const QPointF lastPoint = itemCursorPoint(LastVisibleItem);

    // Descend: leave the rest spot, sweep into the list and slide down over
    // every entry.
    QPainterPath descend(m_restPoint);
    descend.cubicTo(QPointF(m_restPoint.x() + theme.scaled(24), m_restPoint.y() + span * 0.18),
        QPointF(listLeft + listWidth * 0.92, m_bottomPoint.y() - span * 0.32), m_bottomPoint);
    m_segments[0] = descend;

    // Climb back to the first entry, bowing towards the left edge of the list.
    QPainterPath climb(m_bottomPoint);
    climb.cubicTo(QPointF(listLeft + listWidth * 0.22, m_bottomPoint.y() - span * 0.34),
        QPointF(listLeft + listWidth * 0.30, firstPoint.y() + span * 0.26), firstPoint);
    m_segments[1] = climb;

    // Back out to the rest spot.
    QPainterPath returnPath(firstPoint);
    returnPath.cubicTo(
        QPointF(firstPoint.x() + theme.scaled(34), firstPoint.y() - theme.scaled(34)),
        QPointF(m_restPoint.x() - theme.scaled(34), m_restPoint.y() + theme.scaled(12)),
        m_restPoint);
    m_segments[2] = returnPath;

    // Dive straight to the last entry.
    QPainterPath dive(m_restPoint);
    dive.cubicTo(QPointF(m_restPoint.x() + theme.scaled(18), m_restPoint.y() + span * 0.26),
        QPointF(lastPoint.x() + listWidth * 0.30, lastPoint.y() - span * 0.24), lastPoint);
    m_segments[3] = dive;

    // Leave without stopping on the way up.
    QPainterPath leave(lastPoint);
    leave.cubicTo(QPointF(lastPoint.x() + listWidth * 0.38, lastPoint.y() - span * 0.22),
        QPointF(m_restPoint.x() - theme.scaled(18), m_restPoint.y() + span * 0.20), m_restPoint);
    m_segments[4] = leave;
}

QPointF ThemeEditorAnimationsPreview::itemCursorPoint(std::size_t index) const
{
    const QRect rect = m_items[qMin(index, ItemCount - 1)].target;
    return QPointF(rect.left() + rect.width() * 0.62, rect.center().y());
}

void ThemeEditorAnimationsPreview::resetLoopState()
{
    m_phase = 0.0;
    m_cycleSeconds = 0.0;
    m_settingsPhase = 0.0;
    m_settingsSeconds = 0.0;
    m_cursorPos = m_restPoint;
    m_clickPulse = 0.0;
    m_dropdownProgress = 0.0;
    m_groupPhase = 0.0;
    m_groupSeconds = 0.0;
    m_groupSlide = 1.0;
    m_currentGroupTab = 0;

    const std::size_t selected = selectedIndexAt(0.0);
    for (std::size_t index = 0; index < ItemCount; ++index) {
        m_items[index].hover = 0.0;
        m_items[index].active = index == selected ? 1.0 : 0.0;
    }
}

void ThemeEditorAnimationsPreview::onTick()
{
    const qreal deltaMs = m_clock.isValid() ? qBound(0.0, qreal(m_clock.restart()), 200.0)
                                            : qreal(kFrameIntervalMs);

    m_phase += deltaMs / (kCycleSeconds * 1000.0);
    m_phase -= std::floor(m_phase);
    m_cycleSeconds = m_phase * kCycleSeconds;

    // The settings column runs on its own clock: 12 s against the walk's 10 s,
    // so the two halves drift instead of marching together.
    m_settingsPhase += deltaMs / (kSettingsCycleSeconds * 1000.0);
    m_settingsPhase -= std::floor(m_settingsPhase);
    m_settingsSeconds = m_settingsPhase * kSettingsCycleSeconds;

    m_groupPhase += deltaMs / (kGroupCycleSeconds * 1000.0);
    m_groupPhase -= std::floor(m_groupPhase);
    m_groupSeconds = m_groupPhase * kGroupCycleSeconds;

    m_cursorPos = cursorPointAt(m_cycleSeconds);
    m_clickPulse = clickPulseAt(m_cycleSeconds);
    advanceProgress(deltaMs);
    advanceSettings();
    advanceGroup();
    update();
}

void ThemeEditorAnimationsPreview::advanceProgress(qreal deltaMs)
{
    const std::size_t selected = selectedIndexAt(m_cycleSeconds);
    const qreal speed = qMax(0.01, m_animationSpeed);
    const qreal hoverAlpha
        = m_animationsEnabled ? 1.0 - std::exp(-deltaMs * speed / kHoverTauMs) : 1.0;
    const qreal activeAlpha
        = m_animationsEnabled ? 1.0 - std::exp(-deltaMs * speed / kActiveTauMs) : 1.0;

    for (std::size_t index = 0; index < ItemCount; ++index) {
        ListItem& item = m_items[index];
        const qreal hoverTarget = item.target.contains(m_cursorPos.toPoint()) ? 1.0 : 0.0;
        const qreal activeTarget = index == selected ? 1.0 : 0.0;
        item.hover += (hoverTarget - item.hover) * hoverAlpha;
        item.active += (activeTarget - item.active) * activeAlpha;

        if (item.button) {
            item.button->setHoverProgress(item.hover);
            item.button->setActiveProgress(item.active);
        }
    }
}

void ThemeEditorAnimationsPreview::advanceSettings()
{
    m_dropdownProgress
        = settingProgress(kDropdownOpenAt, kDropdownCloseAt, kPopupShowMs, kPopupHideMs);
    if (m_dropdownCombo) {
        m_dropdownCombo->setArrowProgress(m_dropdownProgress);
    }

    if (m_toggleSwitch) {
        const qreal thumb = settingProgress(kToggleOnAt, kToggleOffAt, kThumbMs, kThumbMs);
        m_toggleSwitch->setThumbPosition(thumb);
        m_toggleSwitch->setActiveProgress(thumb);
    }

    if (m_switcher) {
        m_switcher->setIndicatorFraction(
            settingProgress(kSwitcherOnAt, kSwitcherOffAt, kSegmentedMs, kSegmentedMs));
    }
}

void ThemeEditorAnimationsPreview::advanceGroup()
{
    if (!m_groupHeader) {
        return;
    }

    const int tab = m_groupSeconds >= kGroupSwapAt ? 1 : 0;
    if (tab != m_currentGroupTab) {
        m_currentGroupTab = tab;
        m_groupHeader->setCurrentPanel(tab == 0
                ? static_cast<ruwa::ui::docking::DockPanel*>(m_brushesPanel)
                : static_cast<ruwa::ui::docking::DockPanel*>(m_brushSettingsPanel));
    }

    if (!m_animationsEnabled) {
        m_groupSlide = 1.0;
        return;
    }

    const qreal edge = tab == 1 ? kGroupSwapAt : 0.0;
    qreal since = m_groupSeconds - edge;
    if (since < 0.0) {
        since += kGroupCycleSeconds;
    }
    const qreal duration = qMax(1.0, kGroupSlideMs / qMax(0.01, m_animationSpeed)) / 1000.0;
    m_groupSlide = easeInOutCubic(since / duration);
}

qreal ThemeEditorAnimationsPreview::settingProgress(
    qreal onSeconds, qreal offSeconds, int authoredOnMs, int authoredOffMs) const
{
    const qreal now = m_settingsSeconds;
    const bool on = now >= onSeconds && now < offSeconds;
    if (!m_animationsEnabled) {
        return on ? 1.0 : 0.0;
    }

    const qreal edge = on ? onSeconds : offSeconds;
    qreal since = now - edge;
    if (since < 0.0) {
        since += kSettingsCycleSeconds;
    }

    const int authoredMs = on ? authoredOnMs : authoredOffMs;
    const qreal duration = qMax(1.0, authoredMs / qMax(0.01, m_animationSpeed)) / 1000.0;
    const qreal progress = easeOutCubic(since / duration);
    return on ? progress : 1.0 - progress;
}

qreal ThemeEditorAnimationsPreview::segmentProgress(
    qreal cycleSeconds, qreal startSeconds, qreal endSeconds) const
{
    const qreal span = qMax(0.001, endSeconds - startSeconds);
    // The cursor is the operator of the scene, not one of the animations being
    // configured, so neither the master switch nor the multiplier touches it.
    return easeInOutCubic((cycleSeconds - startSeconds) / span);
}

QPointF ThemeEditorAnimationsPreview::cursorPointAt(qreal cycleSeconds) const
{
    const auto pointOn = [this](int segment, qreal progress) {
        const QPainterPath& path = m_segments[std::size_t(segment)];
        if (path.isEmpty()) {
            return m_restPoint;
        }
        return path.pointAtPercent(qBound(0.0, progress, 1.0));
    };

    if (cycleSeconds < kDescendStart) {
        return m_restPoint;
    }
    if (cycleSeconds < kDescendEnd) {
        return pointOn(0, segmentProgress(cycleSeconds, kDescendStart, kDescendEnd));
    }
    if (cycleSeconds < kClimbEnd) {
        return pointOn(1, segmentProgress(cycleSeconds, kClimbStart, kClimbEnd));
    }
    if (cycleSeconds < kReturnStart) {
        return itemCursorPoint(FirstVisibleItem);
    }
    if (cycleSeconds < kReturnEnd) {
        return pointOn(2, segmentProgress(cycleSeconds, kReturnStart, kReturnEnd));
    }
    if (cycleSeconds < kDiveStart) {
        return m_restPoint;
    }
    if (cycleSeconds < kDiveEnd) {
        return pointOn(3, segmentProgress(cycleSeconds, kDiveStart, kDiveEnd));
    }
    if (cycleSeconds < kLeaveStart) {
        return itemCursorPoint(LastVisibleItem);
    }
    if (cycleSeconds < kLeaveEnd) {
        return pointOn(4, segmentProgress(cycleSeconds, kLeaveStart, kLeaveEnd));
    }
    return m_restPoint;
}

std::size_t ThemeEditorAnimationsPreview::selectedIndexAt(qreal cycleSeconds) const
{
    // The cycle opens and closes on the last entry, so the loop seam is invisible.
    if (cycleSeconds >= kFirstClickTime && cycleSeconds < kSecondClickTime) {
        return FirstVisibleItem;
    }
    return LastVisibleItem;
}

qreal ThemeEditorAnimationsPreview::clickPulseAt(qreal cycleSeconds) const
{
    qreal pulse = 0.0;
    for (const qreal clickTime : { kFirstClickTime, kSecondClickTime }) {
        const qreal since = cycleSeconds - clickTime;
        if (since >= 0.0 && since < kClickPulseSeconds) {
            pulse = qMax(pulse, 1.0 - since / kClickPulseSeconds);
        }
    }
    return pulse;
}

void ThemeEditorAnimationsPreview::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const qreal radius = theme.scaled(12);
    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    renderSceneLayer();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setPen(QPen(m_previewColors.border.darker(133), 1.0));
    painter.setBrush(m_previewColors.background);
    painter.drawRoundedRect(bounds, radius, radius);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()), radius, radius);
    painter.setClipPath(clip);

    if (!m_listLayer.isNull()) {
        painter.drawPixmap(0, 0, m_listLayer);
    }

    paintCursor(painter);
}

void ThemeEditorAnimationsPreview::renderSceneLayer()
{
    if (size().isEmpty()) {
        m_listLayer = QPixmap();
        return;
    }

    const qreal dpr = devicePixelRatioF();
    const QSize pixelSize(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
    if (m_listLayer.size() != pixelSize) {
        m_listLayer = QPixmap(pixelSize);
        m_listLayer.setDevicePixelRatio(dpr);
    }
    m_listLayer.fill(Qt::transparent);

    if (m_panelSnapshotsDirty) {
        rebuildPanelSnapshots();
    }

    QPainter layerPainter(&m_listLayer);
    layerPainter.setRenderHint(QPainter::Antialiasing);
    auto& manager = ruwa::ui::core::ThemeManager::instance();
    manager.withColorOverride(m_previewColors, [this, &layerPainter]() {
        for (const ListItem& item : m_items) {
            if (!item.button) {
                continue;
            }
            item.button->ensurePolished();
            item.button->render(&layerPainter, item.target.topLeft(), QRegion(item.button->rect()),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
        }

        const QRect listRect = m_items[0].target;
        QColor dividerColor = m_previewColors.border;
        dividerColor.setAlpha(110);
        const int dividerThickness = qMax(1, ruwa::ui::core::ThemeManager::instance().scaled(1));
        layerPainter.fillRect(
            QRect(listRect.left(), m_dividerY, listRect.width(), dividerThickness), dividerColor);

        renderGroup(layerPainter);

        for (const SettingRow& row : m_settings) {
            if (!row.widget) {
                continue;
            }
            row.widget->ensurePolished();
            row.widget->render(&layerPainter, row.target.topLeft(), QRegion(row.widget->rect()),
                QWidget::DrawWindowBackground | QWidget::DrawChildren);
        }

        renderSettingsPopup(layerPainter);
    });
}

void ThemeEditorAnimationsPreview::renderGroup(QPainter& painter)
{
    if (!m_groupHeader || m_groupBodyTarget.isEmpty()) {
        return;
    }

    // The members slide sideways past each other, the way DockGroupHost moves
    // them: the incoming one comes from the side its tab sits on, the outgoing
    // one leaves through the opposite edge, both clipped to the member area.
    const qreal progress = qBound(0.0, m_groupSlide, 1.0);
    const int span = m_groupBodyTarget.width();
    const int direction = m_currentGroupTab == 1 ? 1 : -1;
    const std::size_t incoming = static_cast<std::size_t>(m_currentGroupTab);
    const std::size_t outgoing = 1 - incoming;

    painter.save();
    painter.setClipRect(m_groupBodyTarget);
    if (progress < 1.0 && !m_panelSnapshots[outgoing].isNull()) {
        painter.drawPixmap(
            m_groupBodyTarget.topLeft() + QPoint(qRound(-direction * span * progress), 0),
            m_panelSnapshots[outgoing]);
    }
    if (!m_panelSnapshots[incoming].isNull()) {
        painter.drawPixmap(
            m_groupBodyTarget.topLeft() + QPoint(qRound(direction * span * (1.0 - progress)), 0),
            m_panelSnapshots[incoming]);
    }
    painter.restore();

    m_groupHeader->ensurePolished();
    painter.save();
    painter.translate(m_groupHeaderTarget.topLeft());
    painter.scale(kGroupScale, kGroupScale);
    m_groupHeader->render(&painter, QPoint(), QRegion(m_groupHeader->rect()),
        QWidget::DrawWindowBackground | QWidget::DrawChildren);
    painter.restore();
}

void ThemeEditorAnimationsPreview::renderSettingsPopup(QPainter& painter)
{
    if (m_dropdownProgress <= 0.01 || !m_dropdownCombo || m_popupSize.isEmpty()) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const QRect row = m_settings[DropdownRow].target;
    const QPoint comboOrigin = m_dropdownCombo->mapTo(m_dropdownRow, QPoint(0, 0));
    const int comboLeft = row.left() + comboOrigin.x();
    const int comboBottom = row.top() + comboOrigin.y() + m_dropdownCombo->height();

    // Same entrance the real popup plays: it fades in while sliding down into
    // place, so a half-open frame reads as opening rather than as a static list.
    const qreal slide = theme.scaled(kPopupSlide) * (1.0 - m_dropdownProgress);
    const QPoint target(comboLeft, qRound(comboBottom + theme.scaled(kPopupGap) - slide));

    painter.save();
    painter.setOpacity(m_dropdownProgress);
    m_dropdownCombo->renderPresentationPopup(&painter, target);
    painter.restore();
}

void ThemeEditorAnimationsPreview::paintCursor(QPainter& painter)
{
    if (m_cursorPixmap.isNull()) {
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const qreal size = m_cursorPixmap.width() / m_cursorPixmap.devicePixelRatio();
    const QPointF hotspot(m_cursorPos);

    if (m_clickPulse > 0.0) {
        const qreal reveal = 1.0 - m_clickPulse;
        const qreal ringRadius = theme.scaled(5) + theme.scaled(14) * reveal;
        QColor ringColor = m_previewColors.primary;
        ringColor.setAlphaF(ringColor.alphaF() * m_clickPulse * 0.8);
        painter.save();
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(ringColor, qMax(1.0, qreal(theme.scaled(2)))));
        painter.drawEllipse(hotspot, ringRadius, ringRadius);
        painter.restore();
    }

    // The press dip keeps the click readable even when the ring is short-lived.
    const qreal pressScale = 1.0 - 0.10 * m_clickPulse;
    const qreal drawSize = size * pressScale;
    const QPointF topLeft(
        hotspot.x() - drawSize * kCursorHotspot, hotspot.y() - drawSize * kCursorHotspot);
    const QRectF cursorRect(topLeft, QSizeF(drawSize, drawSize));

    // A halo in the banner background keeps the glyph readable over both the
    // plain background and a highlighted entry.
    QColor haloColor = m_previewColors.background;
    haloColor.setAlphaF(0.85);
    const QPixmap halo = tintedPixmap(m_cursorPixmap, haloColor);
    const qreal haloOffset = qMax(1.0, qreal(theme.scaled(1)));
    for (const QPointF& offset : { QPointF(-haloOffset, 0.0), QPointF(haloOffset, 0.0),
             QPointF(0.0, -haloOffset), QPointF(0.0, haloOffset) }) {
        painter.drawPixmap(cursorRect.translated(offset), halo, QRectF(m_cursorPixmap.rect()));
    }
    painter.drawPixmap(cursorRect, m_cursorPixmap, QRectF(m_cursorPixmap.rect()));
}

} // namespace ruwa::ui::widgets
