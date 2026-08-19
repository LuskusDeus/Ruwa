// SPDX-License-Identifier: MPL-2.0

// LayerPreviewPopup.cpp
#include "features/layers/ui/LayerPreviewPopup.h"

#include "features/layers/model/BlendModeUtils.h"
#include "features/layers/ui/LayerRowWidget.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyleManager.h"

#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStringList>
#include <QVariantAnimation>
#include <QtMath>

namespace ruwa::ui::widgets {

using ruwa::core::layers::BlendMode;
using ruwa::core::layers::LayerData;
using ruwa::core::layers::LayerType;
using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeFontRole;
using ruwa::ui::core::ThemeManager;

namespace {

constexpr int kPanelRadius = 12;
constexpr int kShadowMargin = 14;
constexpr int kPadding = 13;
constexpr int kPreviewGap = 15;
constexpr int kHeaderGap = 9;
constexpr int kSubtitleGap = 3;
constexpr int kRowSpacing = 5;
constexpr int kLabelValueGap = 14;
constexpr int kPanelMinHeight = 128;
constexpr int kPreviewMinWidth = 92;
constexpr int kPreviewMaxWidth = 300;
constexpr int kInfoMinWidth = 116;
constexpr int kInfoMaxWidth = 300;
constexpr int kCheckerSize = 8;
constexpr int kAnchorGap = 10;
constexpr int kScreenMargin = 8;
constexpr int kBlurRadius = 26;
constexpr int kShowDurationMs = 165;
constexpr int kHideDurationMs = 115;
constexpr int kMoveDurationMs = 190;
constexpr int kContentFadeDurationMs = 170;
constexpr qreal kInitialScale = 0.955;
constexpr int kInitialSlideDistance = 7;
constexpr qreal kPanelEdgeInset = 0.75;
constexpr qreal kMinPreviewAspect = 0.35;
constexpr qreal kMaxPreviewAspect = 3.0;

/// The one shared popup window. Parentless top-level, dropped on aboutToQuit.
QPointer<LayerPreviewPopup> g_popup;

bool animationsEnabled()
{
    return ruwa::ui::core::WidgetStyleManager::instance().animationsEnabled();
}

qreal devicePixelRatioFor(const QWidget* widget)
{
    if (widget) {
        return widget->devicePixelRatioF();
    }
    const QScreen* screen = QGuiApplication::primaryScreen();
    return screen ? screen->devicePixelRatio() : qreal(1.0);
}

int boundedCoordinate(int desired, int extent, int minimum, int maximum)
{
    const int greatestStart = maximum - extent + 1;
    if (greatestStart < minimum) {
        return minimum;
    }
    return qBound(minimum, desired, greatestStart);
}

/// Drop-shadow tint taken from the palette instead of a fixed black: a light
/// theme needs a lighter, weaker shadow than a dark one or the popup reads as
/// stamped onto the panel. @p proximity is 0 at the outermost ring, 1 at the
/// panel edge.
QColor panelShadowColor(const ThemeColors& colors, qreal proximity)
{
    QColor tint = ThemeColors::adjustBrightness(colors.background, colors.isDark ? 0.25 : 0.55);
    const qreal peak = colors.isDark ? 13.0 : 9.0;
    tint.setAlpha(qRound((0.25 + qBound<qreal>(0.0, proximity, 1.0) * 0.75) * peak));
    return tint;
}

/// True for layers the row draws as an icon instead of a raster preview.
bool usesIconPreview(const LayerData* data)
{
    return data && (data->isGroup() || data->isAdjustment());
}

QString layerTypeLabel(const LayerData* data)
{
    if (!data) {
        return {};
    }
    // Free function: no tr(). The context must be spelled out as a literal at
    // every call — lupdate cannot follow it through a const char* variable, and
    // a missed string silently stays English.
    switch (data->type) {
    case LayerType::Group:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Group");
    case LayerType::Smart:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Smart");
    case LayerType::Board:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Board");
    case LayerType::Text:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Text");
    case LayerType::Adjustment:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Adjustment");
    case LayerType::Vector:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Vector");
    case LayerType::Mask:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Mask");
    case LayerType::Background:
        return QCoreApplication::translate("ruwa::ui::widgets::LayerPreviewPopup", "Background");
    case LayerType::Raster:
        break;
    }
    return {};
}

} // namespace

// ============================================================================
// Construction / singleton
// ============================================================================

LayerPreviewPopup::LayerPreviewPopup()
    : QWidget(nullptr,
          Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus
              | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
    , m_presentationAnimation(new QVariantAnimation(this))
    , m_geometryAnimation(new QVariantAnimation(this))
    , m_contentFadeAnimation(new QVariantAnimation(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::NoFocus);
    setObjectName(QStringLiteral("RuwaLayerPreviewPopup"));

    connect(m_presentationAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) {
            m_presentationProgress = qBound(0.0, value.toReal(), 1.0);
            setWindowOpacity(m_presentationProgress);
            update();
        });
    connect(m_presentationAnimation, &QVariantAnimation::finished, this, [this] {
        if (!m_hiding) {
            return;
        }
        m_hiding = false;
        QWidget::hide();
        setWindowOpacity(1.0);
        m_backdrop = {};
        m_previousContent = {};
    });

    connect(m_geometryAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { setGeometry(value.toRect()); });

    connect(m_contentFadeAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) {
            m_contentFadeProgress = qBound(0.0, value.toReal(), 1.0);
            update();
        });
    connect(m_contentFadeAnimation, &QVariantAnimation::finished, this, [this] {
        m_previousContent = {};
        m_contentFadeProgress = 1.0;
        update();
    });

    connect(
        &ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] { dismiss(false); });
}

LayerPreviewPopup::~LayerPreviewPopup()
{
    setWatching(false);
}

LayerPreviewPopup* LayerPreviewPopup::instance()
{
    if (!g_popup) {
        g_popup = new LayerPreviewPopup();
        // Destroy it before the platform integration goes away; a deferred
        // delete would not be processed this late.
        QObject::connect(
            qApp, &QCoreApplication::aboutToQuit, g_popup.data(), [] { delete g_popup.data(); });
    }
    return g_popup.data();
}

// ============================================================================
// Public entry points
// ============================================================================

void LayerPreviewPopup::showPreview(QWidget* source, const LayerData* data,
    const QRect& displayFrame, bool maskTarget, const QRect& anchorGlobalRect)
{
    if (!source || !data || anchorGlobalRect.isEmpty()) {
        return;
    }
    instance()->present(source, data, displayFrame, maskTarget, anchorGlobalRect);
}

void LayerPreviewPopup::hidePreview(const QWidget* source)
{
    if (!isShowingFor(source)) {
        return;
    }
    g_popup->dismiss(animationsEnabled());
}

void LayerPreviewPopup::hideAny()
{
    // Never instantiate just to hide — this also runs during teardown.
    if (!g_popup || !g_popup->isVisible()) {
        return;
    }
    g_popup->dismiss(animationsEnabled());
}

bool LayerPreviewPopup::isShowingFor(const QWidget* source)
{
    const LayerPreviewPopup* popup = g_popup.data();
    return source && popup && popup->m_source == source && popup->isVisible() && !popup->m_hiding;
}

// ============================================================================
// Presentation
// ============================================================================

void LayerPreviewPopup::present(QWidget* source, const LayerData* data, const QRect& displayFrame,
    bool maskTarget, const QRect& anchorGlobalRect)
{
    const bool wasVisible = isVisible() && !m_hiding;
    if (wasVisible && m_source == source && m_layerId == data->id && m_maskTarget == maskTarget) {
        // Same thumbnail — the pointer just moved inside it.
        return;
    }

    m_source = source;
    m_layerId = data->id;
    m_maskTarget = maskTarget;

    QSize panelSize;
    QPixmap content = buildContentPixmap(data, displayFrame, maskTarget, panelSize);
    if (content.isNull() || panelSize.isEmpty()) {
        dismiss(false);
        return;
    }

    const auto& theme = ThemeManager::instance();
    m_shadowMargin = theme.scaled(kShadowMargin);

    const QRect globalPanel = placePanel(anchorGlobalRect, panelSize, source);
    const QRect windowRect
        = globalPanel.adjusted(-m_shadowMargin, -m_shadowMargin, m_shadowMargin, m_shadowMargin);

    captureBackdrop(source, globalPanel);

    m_geometryAnimation->stop();
    m_contentFadeAnimation->stop();

    if (wasVisible) {
        m_previousContent = m_content;
        m_content = content;
        m_contentFadeProgress = 0.0;

        if (animationsEnabled()) {
            m_geometryAnimation->setDuration(kMoveDurationMs);
            m_geometryAnimation->setEasingCurve(QEasingCurve::OutCubic);
            m_geometryAnimation->setStartValue(geometry());
            m_geometryAnimation->setEndValue(windowRect);
            m_geometryAnimation->start();

            m_contentFadeAnimation->setDuration(kContentFadeDurationMs);
            m_contentFadeAnimation->setEasingCurve(QEasingCurve::InOutCubic);
            m_contentFadeAnimation->setStartValue(0.0);
            m_contentFadeAnimation->setEndValue(1.0);
            m_contentFadeAnimation->start();
        } else {
            setGeometry(windowRect);
            m_previousContent = {};
            m_contentFadeProgress = 1.0;
        }
        update();
        setWatching(true);
        return;
    }

    m_previousContent = {};
    m_content = content;
    m_contentFadeProgress = 1.0;

    // Re-showing while the fade-out is still running picks up where it left off
    // instead of blinking back to zero.
    const bool resuming = isVisible() && m_hiding;
    m_presentationAnimation->stop();
    m_hiding = false;
    setGeometry(windowRect);

    const bool animated = animationsEnabled();
    m_presentationProgress = animated ? (resuming ? m_presentationProgress : 0.0) : 1.0;
    setWindowOpacity(m_presentationProgress);
    QWidget::show();
    raise();

    if (animated) {
        m_presentationAnimation->setDuration(kShowDurationMs);
        m_presentationAnimation->setEasingCurve(QEasingCurve::OutCubic);
        m_presentationAnimation->setStartValue(m_presentationProgress);
        m_presentationAnimation->setEndValue(1.0);
        m_presentationAnimation->start();
    }

    setWatching(true);
}

void LayerPreviewPopup::dismiss(bool animated)
{
    setWatching(false);
    m_geometryAnimation->stop();
    m_contentFadeAnimation->stop();
    m_previousContent = {};
    m_contentFadeProgress = 1.0;
    m_source = nullptr;
    m_layerId = QUuid();

    if (!isVisible()) {
        m_hiding = false;
        return;
    }

    if (!animated) {
        m_presentationAnimation->stop();
        m_hiding = false;
        QWidget::hide();
        setWindowOpacity(1.0);
        m_presentationProgress = 0.0;
        m_backdrop = {};
        return;
    }

    m_presentationAnimation->stop();
    m_hiding = true;
    m_presentationAnimation->setDuration(kHideDurationMs);
    m_presentationAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_presentationAnimation->setStartValue(m_presentationProgress);
    m_presentationAnimation->setEndValue(0.0);
    m_presentationAnimation->start();
}

// ============================================================================
// Placement
// ============================================================================

QRect LayerPreviewPopup::placePanel(
    const QRect& anchorGlobalRect, const QSize& panelSize, QWidget* source)
{
    const auto& theme = ThemeManager::instance();

    QScreen* screen = QGuiApplication::screenAt(anchorGlobalRect.center());
    if (!screen && source) {
        screen = source->screen();
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    const int margin = theme.scaled(kScreenMargin) + theme.scaled(kShadowMargin);
    QRect bounds = screen ? screen->availableGeometry() : anchorGlobalRect;
    bounds = bounds.adjusted(margin, margin, -margin, -margin);

    const int gap = theme.scaled(kAnchorGap);
    const int leftX = anchorGlobalRect.left() - gap - panelSize.width();
    const int rightX = anchorGlobalRect.right() + 1 + gap;

    // The layers panel is usually docked to a window edge: prefer the side that
    // actually has room, and on a tie the side with more free space.
    bool placeLeft = leftX >= bounds.left();
    const bool fitsRight = rightX + panelSize.width() <= bounds.right() + 1;
    if (placeLeft && fitsRight) {
        placeLeft = (anchorGlobalRect.left() - bounds.left())
            >= (bounds.right() - anchorGlobalRect.right());
    } else if (!placeLeft && !fitsRight) {
        placeLeft = (anchorGlobalRect.left() - bounds.left())
            > (bounds.right() - anchorGlobalRect.right());
    }
    m_placedLeftOfAnchor = placeLeft;

    const int desiredX = placeLeft ? leftX : rightX;
    const int desiredY = anchorGlobalRect.center().y() - panelSize.height() / 2;

    return QRect(
        QPoint(boundedCoordinate(desiredX, panelSize.width(), bounds.left(), bounds.right()),
            boundedCoordinate(desiredY, panelSize.height(), bounds.top(), bounds.bottom())),
        panelSize);
}

void LayerPreviewPopup::captureBackdrop(QWidget* source, const QRect& globalPanelRect)
{
    m_backdrop = {};
    if (!source) {
        return;
    }
    QWidget* window = source->window();
    if (!window) {
        return;
    }

    const QPoint localTopLeft = window->mapFromGlobal(globalPanelRect.topLeft());
    const QRect grabRect(localTopLeft, globalPanelRect.size());
    if (!QRect(QPoint(0, 0), window->size()).contains(grabRect)) {
        return;
    }

    const QPixmap snapshot = window->grab(grabRect);
    if (snapshot.isNull()) {
        return;
    }
    m_backdrop = ruwa::ui::painting::blurSnapshotPixmap(
        snapshot, ThemeManager::instance().scaled(kBlurRadius));
}

// ============================================================================
// Content
// ============================================================================

QVector<LayerPreviewPopup::InfoRow> LayerPreviewPopup::buildInfoRows(
    const LayerData* data, bool maskTarget) const
{
    const auto& colors = ThemeManager::instance().colors();
    const QColor on = colors.text;
    const QColor off = colors.textMuted;

    const auto boolRow = [&](const QString& label, bool value) {
        return InfoRow { label, value ? tr("Yes") : tr("No"), value ? on : off };
    };

    QVector<InfoRow> rows;
    if (!data) {
        return rows;
    }

    if (maskTarget) {
        rows.append(boolRow(tr("Enabled"), data->maskEnabled));
        rows.append(boolRow(tr("Linked"), data->maskLinked));
        rows.append(boolRow(tr("Editing"), data->maskEditActive));
        rows.append(boolRow(tr("Layer Visible"), data->visible));
        return rows;
    }

    rows.append(boolRow(tr("Visible"), data->visible));
    rows.append(boolRow(tr("Locked"), data->locked));
    rows.append(InfoRow {
        tr("Opacity"), tr("%1%").arg(qRound(qBound<qreal>(0.0, data->opacity, 1.0) * 100.0)), on });
    rows.append(InfoRow { tr("Blending Mode"),
        ruwa::core::layers::blendModeDisplayName(
            data->blendMode, "ruwa::ui::widgets::LayerPreviewPopup"),
        on });
    if (!data->isGroup() && !data->isAdjustment()) {
        rows.append(boolRow(tr("Alpha Locked"), data->alphaLock));
    }
    if (data->clippedToBelow) {
        rows.append(boolRow(tr("Clipped"), true));
    }
    if (data->hasMask()) {
        rows.append(InfoRow {
            tr("Mask"), data->maskEnabled ? tr("On") : tr("Off"), data->maskEnabled ? on : off });
    }
    if (!data->effects.isEmpty()) {
        rows.append(InfoRow { tr("Effects"), QString::number(data->effects.size()), on });
    }
    return rows;
}

QPixmap LayerPreviewPopup::renderPreviewImage(
    const LayerData* data, const QRect& displayFrame, bool maskTarget, const QSize& boxSize) const
{
    if (!data || boxSize.isEmpty()) {
        return {};
    }

    const qreal dpr = devicePixelRatioFor(m_source);
    const QSize deviceSize(
        qMax(1, qRound(boxSize.width() * dpr)), qMax(1, qRound(boxSize.height() * dpr)));

    const QImage image = maskTarget
        ? LayerRowWidget::buildMaskPreviewImage(data, displayFrame, deviceSize)
        : LayerRowWidget::buildThumbnailImage(data, displayFrame, deviceSize);
    if (image.isNull()) {
        return {};
    }

    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(dpr);
    return pixmap;
}

QPixmap LayerPreviewPopup::buildContentPixmap(
    const LayerData* data, const QRect& displayFrame, bool maskTarget, QSize& panelSize) const
{
    if (!data) {
        return {};
    }

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    const int pad = theme.scaled(kPadding);
    const int previewGap = theme.scaled(kPreviewGap);
    const int headerGap = theme.scaled(kHeaderGap);
    const int subtitleGap = theme.scaled(kSubtitleGap);
    const int rowSpacing = theme.scaled(kRowSpacing);
    const int labelValueGap = theme.scaled(kLabelValueGap);

    // --- Text ---
    const QFont titleFont = theme.font(ThemeFontRole::Label, QFont::Bold);
    const QFont subtitleFont = theme.font(ThemeFontRole::Caption);
    const QFont rowFont = theme.font(ThemeFontRole::Small);

    const QString title = maskTarget ? tr("%1 — Mask").arg(data->name)
                                     : (data->name.isEmpty() ? tr("Layer") : data->name);

    QStringList subtitleParts;
    const QString typeLabel = layerTypeLabel(data);
    if (!typeLabel.isEmpty()) {
        subtitleParts << typeLabel;
    }
    const QRect sourceFrame = LayerRowWidget::previewSourceFrame(data, displayFrame);
    if (sourceFrame.width() > 0 && sourceFrame.height() > 0) {
        subtitleParts
            << QStringLiteral("%1 × %2").arg(sourceFrame.width()).arg(sourceFrame.height());
    }
    const QString subtitle = subtitleParts.join(QStringLiteral("  ·  "));

    const QFontMetrics titleMetrics(titleFont);
    const QFontMetrics subtitleMetrics(subtitleFont);
    const QFontMetrics rowMetrics(rowFont);

    // --- Right column: title, subtitle, property rows ---
    const QVector<InfoRow> rows = buildInfoRows(data, maskTarget);
    int labelWidth = 0;
    int valueWidth = 0;
    for (const InfoRow& row : rows) {
        labelWidth = qMax(labelWidth, rowMetrics.horizontalAdvance(row.label));
        valueWidth = qMax(valueWidth, rowMetrics.horizontalAdvance(row.value));
    }
    const int rowHeight = rowMetrics.height();
    const int rowsHeight
        = rows.isEmpty() ? 0 : rows.size() * rowHeight + (rows.size() - 1) * rowSpacing;

    int headerHeight = titleMetrics.height();
    if (!subtitle.isEmpty()) {
        headerHeight += subtitleGap + subtitleMetrics.height();
    }
    const int columnHeight = headerHeight + (rows.isEmpty() ? 0 : headerGap + rowsHeight);

    const int naturalInfoWidth = qMax(rows.isEmpty() ? 0 : labelWidth + labelValueGap + valueWidth,
        qMax(titleMetrics.horizontalAdvance(title),
            subtitle.isEmpty() ? 0 : subtitleMetrics.horizontalAdvance(subtitle)));
    const int infoWidth
        = qBound(theme.scaled(kInfoMinWidth), naturalInfoWidth, theme.scaled(kInfoMaxWidth));
    // Values are right-aligned to the column edge; if the natural column got
    // clamped they must give way to the labels rather than overhang the panel.
    valueWidth = qMin(valueWidth, infoWidth);

    // --- Preview: full-bleed left half, its height drives the panel ---
    // Groups/adjustments have no raster of their own, so their layer preview is
    // the same icon the row draws. A mask on such a layer still has real pixels.
    const bool iconPreview = usesIconPreview(data) && !maskTarget;

    const int panelHeight = qMax(columnHeight + pad * 2, theme.scaled(kPanelMinHeight));
    const int previewHeight = panelHeight;

    int previewWidth = previewHeight;
    if (!iconPreview) {
        qreal aspect = 1.0;
        if (sourceFrame.height() > 0) {
            aspect = static_cast<qreal>(sourceFrame.width()) / sourceFrame.height();
        }
        aspect = qBound(kMinPreviewAspect, aspect, kMaxPreviewAspect);
        previewWidth = qBound(theme.scaled(kPreviewMinWidth), qRound(previewHeight * aspect),
            theme.scaled(kPreviewMaxWidth));
    }

    panelSize = QSize(previewWidth + previewGap + infoWidth + pad, panelHeight);

    // --- Raster ---
    const qreal dpr = devicePixelRatioFor(m_source);
    QPixmap canvas(
        QSize(qMax(1, qRound(panelSize.width() * dpr)), qMax(1, qRound(panelSize.height() * dpr))));
    canvas.setDevicePixelRatio(dpr);
    canvas.fill(Qt::transparent);

    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::TextAntialiasing);

    // --- Preview: spans the full panel height on the left, rounded only where
    // it meets the panel's own left corners; the right edge butts against the
    // text column with no outline. ---
    // One pixel of inset keeps the panel's own hairline edge alive around the
    // preview instead of the preview eating it on the left half.
    constexpr int kPreviewInset = 1;
    const QRect previewRect(kPreviewInset, kPreviewInset, previewWidth - kPreviewInset,
        previewHeight - kPreviewInset * 2);
    const qreal previewRadius = qMax(0.0, theme.scaled(kPanelRadius) - qreal(kPreviewInset));
    QPainterPath previewClip;
    previewClip.addRoundedRect(
        QRectF(previewRect).adjusted(0, 0, previewRadius, 0), previewRadius, previewRadius);

    p.save();
    p.setClipRect(previewRect);
    p.setClipPath(previewClip, Qt::IntersectClip);
    if (iconPreview) {
        p.fillRect(previewRect, colors.overlayBase());
        const int iconSide = qRound(previewRect.width() * 0.42);
        const QPixmap icon = ruwa::ui::core::IconProvider::instance().getPixmap(data->isAdjustment()
                ? ruwa::ui::core::IconProvider::StandardIcon::AdjustmentLayer
                : ruwa::ui::core::IconProvider::StandardIcon::Folder,
            QSize(iconSide, iconSide));
        if (!icon.isNull()) {
            QPixmap tinted(icon.size());
            tinted.setDevicePixelRatio(icon.devicePixelRatio());
            tinted.fill(Qt::transparent);
            {
                QPainter ip(&tinted);
                ip.drawPixmap(0, 0, icon);
                ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
                ip.fillRect(tinted.rect(), colors.textMuted);
            }
            const QSize logical = tinted.size() / tinted.devicePixelRatio();
            p.drawPixmap(
                previewRect.center() - QPoint(logical.width() / 2, logical.height() / 2), tinted);
        }
    } else {
        if (maskTarget) {
            p.fillRect(previewRect, colors.surfaceAlt);
        } else {
            // Same transparency indicator as the row thumbnail, at a readable size.
            const QColor checkA = ThemeColors::adjustBrightness(colors.canvas(), 1.10);
            const QColor checkB = ThemeColors::adjustBrightness(colors.canvasGrid(), 1.10);
            p.fillRect(previewRect, checkA);
            const int checkSize = theme.scaled(kCheckerSize);
            for (int y = previewRect.top(); y <= previewRect.bottom(); y += checkSize) {
                for (int x = previewRect.left(); x <= previewRect.right(); x += checkSize) {
                    const bool useA = (((x - previewRect.left()) / checkSize)
                                          + ((y - previewRect.top()) / checkSize))
                            % 2
                        == 0;
                    if (!useA) {
                        p.fillRect(
                            QRect(x, y, checkSize, checkSize).intersected(previewRect), checkB);
                    }
                }
            }
        }

        const QPixmap preview
            = renderPreviewImage(data, displayFrame, maskTarget, previewRect.size());
        if (!preview.isNull()) {
            const QSize logical = preview.size() / preview.devicePixelRatio();
            const QSize fitted = logical.scaled(previewRect.size(), Qt::KeepAspectRatio);
            const QRect target(previewRect.left() + (previewRect.width() - fitted.width()) / 2,
                previewRect.top() + (previewRect.height() - fitted.height()) / 2, fitted.width(),
                fitted.height());
            p.drawPixmap(target, preview);
        }
    }
    p.restore();

    // --- Right column ---
    const int columnLeft = previewWidth + previewGap;
    int y = (panelHeight - columnHeight) / 2;

    p.setFont(titleFont);
    p.setPen(colors.text);
    p.drawText(QRect(columnLeft, y, infoWidth, titleMetrics.height()),
        Qt::AlignLeft | Qt::AlignVCenter,
        titleMetrics.elidedText(title, Qt::ElideMiddle, infoWidth));
    y += titleMetrics.height();

    if (!subtitle.isEmpty()) {
        y += subtitleGap;
        p.setFont(subtitleFont);
        p.setPen(colors.textMuted);
        p.drawText(QRect(columnLeft, y, infoWidth, subtitleMetrics.height()),
            Qt::AlignLeft | Qt::AlignVCenter,
            subtitleMetrics.elidedText(subtitle, Qt::ElideRight, infoWidth));
        y += subtitleMetrics.height();
    }

    if (!rows.isEmpty()) {
        y += headerGap;
        p.setFont(rowFont);
        const int valueLeft = columnLeft + infoWidth - valueWidth;
        for (const InfoRow& row : rows) {
            p.setPen(colors.textMuted);
            p.drawText(
                QRect(columnLeft, y, qMax(0, valueLeft - columnLeft - labelValueGap), rowHeight),
                Qt::AlignLeft | Qt::AlignVCenter,
                rowMetrics.elidedText(
                    row.label, Qt::ElideRight, qMax(0, valueLeft - columnLeft - labelValueGap)));
            p.setPen(row.valueColor);
            p.drawText(QRect(valueLeft, y, valueWidth, rowHeight),
                Qt::AlignRight | Qt::AlignVCenter, row.value);
            y += rowHeight + rowSpacing;
        }
    }

    p.end();
    return canvas;
}

// ============================================================================
// Painting
// ============================================================================

void LayerPreviewPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_content.isNull()) {
        return;
    }

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF panelRect(m_shadowMargin, m_shadowMargin, qMax(1, width() - m_shadowMargin * 2),
        qMax(1, height() - m_shadowMargin * 2));

    // Entrance transform: scale up around the panel centre and slide in from the
    // anchored thumbnail's side.
    const qreal scale = kInitialScale + (1.0 - kInitialScale) * m_presentationProgress;
    const qreal slide = theme.scaled(kInitialSlideDistance) * (1.0 - m_presentationProgress)
        * (m_placedLeftOfAnchor ? 1.0 : -1.0);
    const QPointF pivot = panelRect.center();
    painter.translate(slide, 0.0);
    painter.translate(pivot);
    painter.scale(scale, scale);
    painter.translate(-pivot);

    const int shadowStep = qMax(1, m_shadowMargin / 5);
    for (int spread = m_shadowMargin; spread >= shadowStep; spread -= shadowStep) {
        const qreal proximity = 1.0 - qreal(spread) / qMax(1, m_shadowMargin);
        painter.setPen(Qt::NoPen);
        painter.setBrush(panelShadowColor(colors, proximity));
        painter.drawRoundedRect(panelRect.adjusted(-spread, -spread, spread, spread),
            theme.scaled(kPanelRadius) + spread, theme.scaled(kPanelRadius) + spread);
    }

    painter.save();
    painter.translate(panelRect.topLeft());

    const QRectF localPanel
        = QRectF(QPointF(0, 0), panelRect.size())
              .adjusted(kPanelEdgeInset, kPanelEdgeInset, -kPanelEdgeInset, -kPanelEdgeInset);

    ruwa::ui::painting::drawTonedGlassPanel(painter, localPanel, theme.scaled(kPanelRadius),
        panelRect.size(), m_backdrop, colors.surfaceElevated(), colors.primary, colors.isDark,
        colors.borderSubtleHover(), colors.borderSubtle(), 1.0, false);

    // The preview bleeds into the panel's left corners, so the content must be
    // clipped by the panel shape — during the resize morph the two cross-fading
    // pixmaps have different sizes and would otherwise poke past the glass.
    QPainterPath panelClip;
    panelClip.addRoundedRect(QRectF(QPointF(0, 0), panelRect.size()), theme.scaled(kPanelRadius),
        theme.scaled(kPanelRadius));
    painter.setClipPath(panelClip);

    // Content cross-fade. Both pixmaps keep their natural size, pinned to the
    // left edge so the full-bleed preview stays put while the panel resizes.
    const auto drawContent = [&](const QPixmap& pixmap, qreal opacity) {
        if (pixmap.isNull() || opacity <= 0.001) {
            return;
        }
        const QSizeF logical = QSizeF(pixmap.size()) / pixmap.devicePixelRatio();
        const QPointF origin(0.0, (panelRect.height() - logical.height()) * 0.5);
        painter.setOpacity(opacity);
        painter.drawPixmap(origin, pixmap);
    };

    drawContent(m_previousContent, 1.0 - m_contentFadeProgress);
    drawContent(m_content, m_contentFadeProgress);
    painter.setOpacity(1.0);

    painter.restore();
}

// ============================================================================
// Dismiss triggers
// ============================================================================

void LayerPreviewPopup::setWatching(bool watching)
{
    if (m_watching == watching) {
        return;
    }
    m_watching = watching;
    if (watching) {
        qApp->installEventFilter(this);
    } else {
        qApp->removeEventFilter(this);
    }
}

bool LayerPreviewPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (!event || watched == this) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
        dismiss(animationsEnabled());
        break;
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::DevicePixelRatioChange:
        if (m_source && watched == m_source->window()) {
            dismiss(false);
        }
        break;
    case QEvent::Hide:
    case QEvent::Close:
    case QEvent::Destroy:
        if (m_source && (watched == m_source || watched == m_source->window())) {
            dismiss(false);
        }
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace ruwa::ui::widgets
