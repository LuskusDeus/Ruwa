// SPDX-License-Identifier: MPL-2.0

#include "CanvasBrushQuickPopup.h"

#include "features/brush/manager/BrushPreviewManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/PresetListRowWidget.h"
#include "shared/widgets/PresetMenuTypes.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTabletEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <functional>

namespace ruwa::ui::widgets {

using ruwa::core::brushes::BrushPreviewManager;
using ruwa::core::brushes::BrushPreviewSession;
using ruwa::core::brushes::BrushPreviewSpec;
using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;
using ruwa::ui::core::WidgetStyleManager;
using ruwa::ui::widgets::ProgressHandleSlider;

namespace {

constexpr int kShowDurationMs = 200;
constexpr int kHideDurationMs = 154;

QString translatedBrushText(const QString& text)
{
    if (text.isEmpty()) {
        return text;
    }
    return QCoreApplication::translate("QObject", text.toUtf8().constData());
}

// Soft drop shadow drawn as a stack of filled rounded rects expanding outward
// from the panel. Per-layer alpha follows a quadratic falloff that vanishes at
// the outer edge, so the overlap accumulates into a halo that is dense flush
// against the panel and dissolves smoothly with no visible boundary. Cheap and
// animation-safe (QGraphicsDropShadowEffect is avoided project-wide because it
// collapses widgets when combined with size animations). The panel fill is
// painted on top afterwards, masking the inner part of the stack.
void drawSoftShadow(
    QPainter& painter, const QRectF& panelRect, qreal radius, int spread, const QColor& shadowColor)
{
    if (spread <= 0) {
        return;
    }

    // shadowColor.alpha() is the target overall darkness flush against the panel.
    // Distribute it across the stack: a_i = peak * (1 - i/spread)^2, with peak
    // chosen so the layers sum to that target (integral of (1-t)^2 ~ spread/3).
    const qreal edgeOpacity = qBound(0.0, shadowColor.alphaF(), 1.0);
    const qreal peak = edgeOpacity * 3.0 / spread;

    painter.save();
    painter.setPen(Qt::NoPen);
    for (int i = spread; i >= 1; --i) {
        const qreal t = static_cast<qreal>(i) / spread; // ~0 near panel -> 1 at outer edge
        const qreal falloff = (1.0 - t) * (1.0 - t);
        const int layerAlpha = qRound(peak * falloff * 255.0);
        if (layerAlpha <= 0) {
            continue;
        }
        QColor layerColor = shadowColor;
        layerColor.setAlpha(qBound(1, layerAlpha, 255));
        painter.setBrush(layerColor);

        const QRectF layerRect = panelRect.adjusted(-i, -i, i, i);
        painter.drawRoundedRect(layerRect, radius + i, radius + i);
    }
    painter.restore();
}

bool areBrushEntryListsEqual(const QVector<CanvasBrushQuickPopup::BrushEntry>& lhs,
    const QVector<CanvasBrushQuickPopup::BrushEntry>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    for (int i = 0; i < lhs.size(); ++i) {
        if (lhs[i].id != rhs[i].id) {
            return false;
        }
    }

    return true;
}

class CurrentBrushPreviewWidget final : public QWidget {
public:
    explicit CurrentBrushPreviewWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);

        auto& previewManager = BrushPreviewManager::instance();
        m_strokeSession = previewManager.createSession(BrushPreviewSession::Kind::Stroke, this);
        m_dotSession = previewManager.createSession(BrushPreviewSession::Kind::Dot, this);
        connect(m_strokeSession, &BrushPreviewSession::imageChanged, this,
            &CurrentBrushPreviewWidget::onPreviewImageChanged);
        connect(m_dotSession, &BrushPreviewSession::imageChanged, this,
            &CurrentBrushPreviewWidget::onPreviewImageChanged);
    }

    void setOnImageReady(std::function<void()> callback) { m_onImageReady = std::move(callback); }

    void setPreview(const CanvasBrushQuickPopup::BrushEntry& brush, const QColor& color,
        qreal sizeNorm, qreal opacityNorm)
    {
        m_brush = brush;
        m_color = color;
        m_sizeNorm = sizeNorm;
        m_opacityNorm = opacityNorm;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = WidgetStyleManager::instance().colors();
        const auto& theme = ThemeManager::instance();
        const QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = theme.scaled(9);

        const QColor panelBg = colors.surfaceAlt;
        const QColor panelBorder = colors.borderSubtle();

        painter.setPen(QPen(panelBorder, 1.0));
        painter.setBrush(panelBg);
        painter.drawRoundedRect(outerRect, radius, radius);

        const int padding = theme.scaled(8);
        const int gap = theme.scaled(8);
        const int dotSize = qMin(theme.scaled(54), height() - padding * 2);
        const QRectF dotRect(outerRect.right() - padding - dotSize,
            outerRect.center().y() - dotSize * 0.5, dotSize, dotSize);
        const QRectF strokeRect(outerRect.left() + padding, outerRect.top() + padding,
            (dotRect.left() - gap) - (outerRect.left() + padding),
            outerRect.height() - padding * 2);

        BrushPreviewSpec dotSpec;
        dotSpec.settings = m_brush.settings;
        dotSpec.color = colors.primary;
        dotSpec.sizeNorm = m_sizeNorm;
        dotSpec.opacityNorm = m_opacityNorm;
        dotSpec.size = dotRect.size().toSize().expandedTo(QSize(16, 16));

        BrushPreviewSpec strokeSpec = dotSpec;
        strokeSpec.size = strokeRect.size().toSize().expandedTo(QSize(16, 16));

        if (m_dotSession && !m_dotSession->hasImageFor(dotSpec)) {
            m_dotSession->request(dotSpec);
        }
        if (m_strokeSession && !m_strokeSession->hasImageFor(strokeSpec)) {
            m_strokeSession->request(strokeSpec);
        }

        painter.save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(
            outerRect.adjusted(1.0, 1.0, -1.0, -1.0), radius - 1.0, radius - 1.0);
        painter.setClipPath(clipPath);

        const QImage dotPreview = m_dotSession ? m_dotSession->image() : QImage();
        const QImage strokePreview = m_strokeSession ? m_strokeSession->image() : QImage();
        if (!dotPreview.isNull()) {
            painter.drawImage(dotRect.toRect(), dotPreview);
        }
        if (!strokePreview.isNull()) {
            painter.drawImage(strokeRect.toRect(), strokePreview);
        }
        painter.restore();
    }

private:
    void onPreviewImageChanged()
    {
        update();
        if (m_onImageReady) {
            m_onImageReady();
        }
    }

    CanvasBrushQuickPopup::BrushEntry m_brush;
    QColor m_color = Qt::black;
    qreal m_sizeNorm = 0.5;
    qreal m_opacityNorm = 1.0;
    BrushPreviewSession* m_strokeSession = nullptr;
    BrushPreviewSession* m_dotSession = nullptr;
    std::function<void()> m_onImageReady;
};

// Matches the brush editor library list preview spec so recent-brush rows look
// identical to the editor (wide stroke preview filling the row background).
BrushPreviewSpec makeRecentPreviewSpec(const ruwa::core::brushes::BrushSettingsData& settings)
{
    const auto& theme = ThemeManager::instance();
    BrushPreviewSpec spec;
    spec.settings = settings;
    spec.color = WidgetStyleManager::instance().colors().primary;
    spec.size = QSize(theme.scaled(168), theme.scaled(40));
    return spec;
}

} // namespace

CanvasBrushQuickPopup::CanvasBrushQuickPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_ctxAppearEase.setType(QEasingCurve::BezierSpline);
    m_ctxAppearEase.addCubicBezierSegment(QPointF(0.16, 1.0), QPointF(0.3, 1.0), QPointF(1.0, 1.0));

    m_showProgressAnim = new QVariantAnimation(this);
    m_showProgressAnim->setEasingCurve(QEasingCurve::Linear);
    connect(
        m_showProgressAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            const qreal t = value.toReal();
            const qreal progress = m_isHiding
                ? m_hideStartProgress * m_ctxAppearEase.valueForProgress(1.0 - t)
                : m_showStartProgress
                    + (1.0 - m_showStartProgress) * m_ctxAppearEase.valueForProgress(t);
            setShowProgress(progress);
        });

    connect(m_showProgressAnim, &QVariantAnimation::finished, this, [this]() {
        if (m_isHiding) {
            hideImmediate();
        } else if (m_isShowing) {
            m_isShowing = false;
            m_useSnapshotPresentation = false;
            setLiveContentVisible(true);
            update();
        }
    });

    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    auto* contentLayout = new QVBoxLayout();
    contentLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->addLayout(contentLayout);

    m_titleLabel = new QLabel(this);
    contentLayout->addWidget(m_titleLabel);

    m_previewWidget = new CurrentBrushPreviewWidget(this);
    static_cast<CurrentBrushPreviewWidget*>(m_previewWidget)->setOnImageReady([this]() {
        scheduleSnapshotRefresh();
    });
    contentLayout->addWidget(m_previewWidget);

    m_metricsWidget = new QWidget(this);
    m_metricsWidget->setAttribute(Qt::WA_TranslucentBackground);
    m_metricsWidget->setAttribute(Qt::WA_NoSystemBackground);
    m_metricsWidget->setAutoFillBackground(false);
    m_metricsLayout = new QGridLayout(m_metricsWidget);
    m_metricsLayout->setContentsMargins(0, 0, 0, 0);
    m_sizeLabel = new QLabel(m_metricsWidget);
    m_opacityLabel = new QLabel(m_metricsWidget);
    m_sizeSlider = new ProgressHandleSlider(m_metricsWidget);
    m_opacitySlider = new ProgressHandleSlider(m_metricsWidget);
    m_metricsLayout->addWidget(m_sizeLabel, 0, 0);
    m_metricsLayout->addWidget(m_sizeSlider, 0, 1);
    m_metricsLayout->addWidget(m_opacityLabel, 1, 0);
    m_metricsLayout->addWidget(m_opacitySlider, 1, 1);
    m_metricsLayout->setColumnStretch(1, 1);
    contentLayout->addWidget(m_metricsWidget);

    m_separator = new QWidget(this);
    contentLayout->addWidget(m_separator);

    m_recentLabel = new QLabel(tr("Recent Brushes"), this);
    contentLayout->addWidget(m_recentLabel);

    m_recentScrollArea = new SmoothScrollArea(this);
    m_recentScrollArea->setFillBackground(false);
    m_recentScrollArea->setScrollBarTransparentTrack(true);
    m_recentScrollArea->setScrollBarAlwaysReserved(false);
    contentLayout->addWidget(m_recentScrollArea);

    m_recentContent = new QWidget(m_recentScrollArea);
    m_recentContent->setAttribute(Qt::WA_TranslucentBackground);
    m_recentContent->setAttribute(Qt::WA_NoSystemBackground);
    m_recentContent->setAutoFillBackground(false);
    auto* recentLayout = new QVBoxLayout(m_recentContent);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(0);
    m_recentScrollArea->setWidget(m_recentContent);

    auto* sizeSlider = static_cast<ProgressHandleSlider*>(m_sizeSlider);
    sizeSlider->setOrientation(Qt::Horizontal);
    sizeSlider->setRange(0, 100);
    sizeSlider->setShowValueText(true);
    sizeSlider->setCursor(Qt::PointingHandCursor);

    auto* opacitySlider = static_cast<ProgressHandleSlider*>(m_opacitySlider);
    opacitySlider->setOrientation(Qt::Horizontal);
    opacitySlider->setRange(0, 100);
    opacitySlider->setShowValueText(true);
    opacitySlider->setCursor(Qt::PointingHandCursor);

    connect(sizeSlider, &ProgressHandleSlider::valueChanged, this,
        [this](int value) { emit brushSizeChanged(qBound(0.0, value / 100.0, 1.0)); });
    connect(opacitySlider, &ProgressHandleSlider::valueChanged, this,
        [this](int value) { emit brushOpacityChanged(qBound(0.0, value / 100.0, 1.0)); });

    connect(
        &ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() { applyTheme(); });

    applyTheme();
    setPopupOpacity(0.0);
    setShowProgress(0.0);
    hide();
}

void CanvasBrushQuickPopup::setModel(const Model& model)
{
    const bool recentBrushesChanged = m_model.currentBrush.id != model.currentBrush.id
        || !areBrushEntryListsEqual(m_model.recentBrushes, model.recentBrushes);

    m_model = model;
    const QString packName = translatedBrushText(model.currentPackName.trimmed());
    const QString brushName = translatedBrushText(model.currentBrush.name);
    if (!packName.isEmpty()) {
        m_titleLabel->setText(QStringLiteral("%1 • %2").arg(packName, brushName));
    } else {
        m_titleLabel->setText(brushName);
    }

    auto* sizeSlider = static_cast<ProgressHandleSlider*>(m_sizeSlider);
    auto* opacitySlider = static_cast<ProgressHandleSlider*>(m_opacitySlider);
    const QSignalBlocker sizeBlocker(sizeSlider);
    const QSignalBlocker opacityBlocker(opacitySlider);
    sizeSlider->setValue(qRound(qBound(0.0, model.brushSizeNormalized, 1.0) * 100.0));
    opacitySlider->setValue(qRound(qBound(0.0, model.brushOpacityNormalized, 1.0) * 100.0));
    sizeSlider->setCustomDisplayText(model.sizeText);
    opacitySlider->setCustomDisplayText(model.opacityText);

    static_cast<CurrentBrushPreviewWidget*>(m_previewWidget)
        ->setPreview(model.currentBrush, model.previewColor, model.previewSizeNorm,
            model.previewOpacityNorm);

    if (recentBrushesChanged) {
        rebuildRecentBrushes();
    }
}

void CanvasBrushQuickPopup::showAt(const QPoint& topLeft)
{
    applyPresentationLayout(1.0);
    if (!m_fullSize.isEmpty()) {
        setFixedSize(m_fullSize);
    }
    adjustSize();
    m_fullSize = size();

    // The widget carries a transparent shadow margin; offset so the visible
    // panel (not the widget bounds) lands at the requested top-left, and clamp
    // the panel — not the shadow — against the parent edges.
    QPoint targetTopLeft = topLeft - QPoint(m_shadowMargin, m_shadowMargin);
    if (QWidget* parent = parentWidget()) {
        const int panelW = m_fullSize.width() - m_shadowMargin * 2;
        const int panelH = m_fullSize.height() - m_shadowMargin * 2;
        const int panelX
            = qBound(8, targetTopLeft.x() + m_shadowMargin, qMax(8, parent->width() - panelW - 8));
        const int panelY
            = qBound(8, targetTopLeft.y() + m_shadowMargin, qMax(8, parent->height() - panelH - 8));
        targetTopLeft = QPoint(panelX - m_shadowMargin, panelY - m_shadowMargin);
    }

    const bool wasVisible = isVisible();
    m_isShowing = true;
    m_isHiding = false;
    move(targetTopLeft);

    if (!wasVisible) {
        startShowAnimation();
        return;
    }

    m_showProgressAnim->stop();
    m_showStartProgress = showProgress();
    m_showProgressAnim->setDuration(kShowDurationMs);
    m_showProgressAnim->setStartValue(0.0);
    m_showProgressAnim->setEndValue(1.0);
    m_showProgressAnim->start();
    raise();
}

void CanvasBrushQuickPopup::hidePopup(bool animate)
{
    if (!isVisible() || m_isHiding) {
        return;
    }

    if (!animate) {
        hideImmediate();
        return;
    }

    startHideAnimation();
}

void CanvasBrushQuickPopup::hideImmediate()
{
    m_showProgressAnim->stop();
    m_isShowing = false;
    m_isHiding = false;
    m_useSnapshotPresentation = false;
    setLiveContentVisible(true);
    setPopupOpacity(0.0);
    setShowProgress(0.0);
    hide();
    if (!m_fullSize.isEmpty()) {
        setFixedSize(m_fullSize);
    }
}

void CanvasBrushQuickPopup::setPopupOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(m_opacity);
    }
    update();
}

void CanvasBrushQuickPopup::setShowProgress(qreal progress)
{
    m_showProgress = qBound(0.0, progress, 1.0);
    setPopupOpacity(m_showProgress);
    applyPresentationLayout(m_showProgress);
    update();
}

void CanvasBrushQuickPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_useSnapshotPresentation && !m_presentationSnapshot.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(rect(), m_presentationSnapshot);
        return;
    }

    const auto& colors = WidgetStyleManager::instance().colors();
    const auto& theme = ThemeManager::instance();
    const qreal sm = m_shadowMargin;
    const QRectF outerRect = rect().adjusted(sm + 0.5, sm + 0.5, -sm - 0.5, -sm - 0.5);
    const qreal radius = theme.scaled(12);

    if (m_shadowMargin > 0) {
        const QColor shadowColor(0, 0, 0, colors.isDark ? 120 : 72);
        drawSoftShadow(painter, outerRect, radius, m_shadowMargin, shadowColor);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.surface);
    painter.drawRoundedRect(outerRect, radius, radius);

    ruwa::ui::painting::drawGradientBorder(
        painter, outerRect, radius, colors.border, colors.border.darker(110));
}

void CanvasBrushQuickPopup::applyPresentationLayout(qreal progress)
{
    const qreal u = 0.92 + 0.08 * progress;

    if (m_fullSize.isEmpty()) {
        return;
    }

    if (progress >= 1.0 - 1e-5) {
        setFixedSize(m_fullSize);
        return;
    }

    setFixedSize(qMax(1, qRound(m_fullSize.width() * u)), qMax(1, qRound(m_fullSize.height() * u)));
}

void CanvasBrushQuickPopup::capturePresentationSnapshot()
{
    applyPresentationLayout(1.0);
    if (!m_fullSize.isEmpty()) {
        setFixedSize(m_fullSize);
    }
    ensurePolished();
    if (layout()) {
        layout()->activate();
    }

    const qreal dpr = devicePixelRatioF();
    QPixmap snapshot(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
    snapshot.setDevicePixelRatio(dpr);
    snapshot.fill(Qt::transparent);

    const bool hadOpacityEffect = m_opacityEffect != nullptr;
    const bool opacityEffectEnabled = hadOpacityEffect && m_opacityEffect->isEnabled();
    if (hadOpacityEffect) {
        m_opacityEffect->setEnabled(false);
    }

    render(&snapshot, QPoint(), QRegion(), QWidget::DrawWindowBackground | QWidget::DrawChildren);

    if (hadOpacityEffect) {
        m_opacityEffect->setEnabled(opacityEffectEnabled);
        m_opacityEffect->setOpacity(m_opacity);
    }

    m_presentationSnapshot = snapshot;
}

void CanvasBrushQuickPopup::scheduleSnapshotRefresh()
{
    // Brush previews render asynchronously, so on a cold cache the snapshot
    // captured at show time has empty preview areas. Re-bake the snapshot once
    // the freshly rendered previews arrive so they fade in with the popup
    // instead of popping in only after the animation (or on the next open).
    if (!m_isShowing || !m_useSnapshotPresentation || m_snapshotRefreshScheduled) {
        return;
    }
    m_snapshotRefreshScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_snapshotRefreshScheduled = false;
        refreshPresentationSnapshot();
    });
}

void CanvasBrushQuickPopup::refreshPresentationSnapshot()
{
    if (!m_isShowing || !m_useSnapshotPresentation) {
        return;
    }
    setLiveContentVisible(true);
    capturePresentationSnapshot();
    setLiveContentVisible(false);
    applyPresentationLayout(m_showProgress);
    update();
}

void CanvasBrushQuickPopup::setLiveContentVisible(bool visible)
{
    if (m_titleLabel) {
        m_titleLabel->setVisible(visible);
    }
    if (m_previewWidget) {
        m_previewWidget->setVisible(visible);
    }
    if (m_metricsWidget) {
        m_metricsWidget->setVisible(visible);
    }
    if (m_separator) {
        m_separator->setVisible(visible);
    }
    if (m_recentLabel) {
        m_recentLabel->setVisible(visible);
    }
    if (m_recentScrollArea) {
        m_recentScrollArea->setVisible(visible);
    }
}

void CanvasBrushQuickPopup::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (qApp) {
        qApp->installEventFilter(this);
    }
}

void CanvasBrushQuickPopup::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (qApp) {
        qApp->removeEventFilter(this);
    }
}

bool CanvasBrushQuickPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (!(isVisible() || m_isShowing || m_isHiding) || !event) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
        QWidget* clickedWidget = QApplication::widgetAt(globalPos);

        if (isPopupOrChild(watchedWidget) || isPopupOrChild(clickedWidget)) {
            return QWidget::eventFilter(watched, event);
        }
        if (clickedWidget && clickedWidget->window() && parentWidget()
            && clickedWidget->window() != parentWidget()->window()) {
            return QWidget::eventFilter(watched, event);
        }
        if (containsGlobalPoint(globalPos)) {
            return true;
        }

        if (!m_isHiding) {
            hidePopup();
        }
        break;
    }
    case QEvent::TabletPress: {
        auto* tabletEvent = static_cast<QTabletEvent*>(event);
        const QPoint globalPos = tabletEvent->globalPosition().toPoint();
        QWidget* watchedWidget = qobject_cast<QWidget*>(watched);
        QWidget* clickedWidget = QApplication::widgetAt(globalPos);

        if (isPopupOrChild(watchedWidget) || isPopupOrChild(clickedWidget)) {
            return QWidget::eventFilter(watched, event);
        }
        if (clickedWidget && clickedWidget->window() && parentWidget()
            && clickedWidget->window() != parentWidget()->window()) {
            return QWidget::eventFilter(watched, event);
        }
        if (containsGlobalPoint(globalPos)) {
            return true;
        }

        if (!m_isHiding) {
            hidePopup();
        }
        break;
    }
    case QEvent::KeyPress: {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hidePopup();
            return true;
        }
        break;
    }
    case QEvent::ApplicationDeactivate:
        hideImmediate();
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

bool CanvasBrushQuickPopup::isPopupOrChild(const QWidget* widget) const
{
    return widget && (widget == this || isAncestorOf(widget));
}

void CanvasBrushQuickPopup::startShowAnimation()
{
    m_showProgressAnim->stop();

    setLiveContentVisible(true);
    capturePresentationSnapshot();
    m_useSnapshotPresentation = true;
    setLiveContentVisible(false);
    setShowProgress(0.0);
    show();
    raise();

    m_showStartProgress = showProgress();
    m_showProgressAnim->setDuration(kShowDurationMs);
    m_showProgressAnim->setStartValue(0.0);
    m_showProgressAnim->setEndValue(1.0);
    m_showProgressAnim->start();
}

void CanvasBrushQuickPopup::startHideAnimation()
{
    m_isShowing = false;
    m_isHiding = true;

    m_showProgressAnim->stop();

    setLiveContentVisible(true);
    capturePresentationSnapshot();
    m_useSnapshotPresentation = true;
    setLiveContentVisible(false);

    m_hideStartProgress = showProgress();
    m_showProgressAnim->setDuration(kHideDurationMs);
    m_showProgressAnim->setStartValue(0.0);
    m_showProgressAnim->setEndValue(1.0);
    m_showProgressAnim->start();
}

void CanvasBrushQuickPopup::rebuildRecentBrushes()
{
    auto* recentLayout = qobject_cast<QVBoxLayout*>(m_recentContent->layout());
    if (!recentLayout) {
        return;
    }

    while (QLayoutItem* item = recentLayout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            widget->deleteLater();
        }
        delete item;
    }

    if (m_model.recentBrushes.isEmpty()) {
        auto* emptyLabel = new QLabel(tr("No recent brushes yet."), m_recentContent);
        emptyLabel->setAlignment(Qt::AlignCenter);
        emptyLabel->setObjectName(QStringLiteral("canvas_brush_quick_popup_empty"));
        recentLayout->addWidget(emptyLabel);
        recentLayout->addStretch();
        m_recentScrollArea->refreshScrollGeometry();
        return;
    }

    for (const BrushEntry& brush : m_model.recentBrushes) {
        PresetMenuItem item;
        item.title = translatedBrushText(brush.name);
        item.subtitle = translatedBrushText(brush.packName.trimmed());
        item.previewIcon = ruwa::ui::core::IconProvider::StandardIcon::Brush;
        item.fillPreviewBackground = true;
        item.deletable = false;
        item.renamable = false;
        item.userData = brush.id;

        auto* row = new PresetListRowWidget(item, m_recentContent);
        row->setContextMenuEnabled(false);

        // Each row renders its own brush stroke preview asynchronously, mirroring
        // the brush editor library list. Feed the image in once it is ready and
        // re-bake the show snapshot so previews fade in with the popup.
        auto* session
            = BrushPreviewManager::instance().createSession(BrushPreviewSession::Kind::Stroke, row);
        connect(session, &BrushPreviewSession::imageChanged, this, [this, row, session]() {
            const QImage image = session->image();
            if (!image.isNull()) {
                row->setPreviewImage(image);
            }
            scheduleSnapshotRefresh();
        });
        const BrushPreviewSpec spec = makeRecentPreviewSpec(brush.settings);
        if (session->hasImageFor(spec)) {
            row->setPreviewImage(session->image());
        } else {
            session->request(spec);
        }

        connect(row, &PresetListRowWidget::clicked, this,
            [this, brushId = brush.id]() { emit recentBrushSelected(brushId); });
        recentLayout->addWidget(row);
    }
    recentLayout->addStretch();
    m_recentScrollArea->refreshScrollGeometry();
}

void CanvasBrushQuickPopup::applyTheme()
{
    const auto& colors = ThemeManager::instance().colors();
    const auto& theme = ThemeManager::instance();

    m_shadowMargin = theme.scaled(16);
    setFixedWidth(theme.scaled(356) + m_shadowMargin * 2);
    m_rootMarginsFull
        = QMargins(theme.scaled(10), theme.scaled(8), theme.scaled(10), theme.scaled(10));
    m_rootSpacingFull = theme.scaled(8);
    m_previewHeightFull = theme.scaled(74);
    m_sliderHeightFull = theme.scaled(22);
    m_recentScrollHeightFull = theme.scaled(228);

    if (m_rootLayout) {
        m_rootLayout->setContentsMargins(m_rootMarginsFull
            + QMargins(m_shadowMargin, m_shadowMargin, m_shadowMargin, m_shadowMargin));
        m_rootLayout->setSpacing(m_rootSpacingFull);
    }
    if (m_metricsLayout) {
        m_metricsLayout->setHorizontalSpacing(theme.scaled(10));
        m_metricsLayout->setVerticalSpacing(theme.scaled(10));
    }
    if (m_recentContent && m_recentContent->layout()) {
        // Match the brush editor library list row spacing (kBaseRowSpacing).
        m_recentContent->layout()->setSpacing(theme.scaled(4));
    }

    m_previewWidget->setFixedHeight(m_previewHeightFull);
    if (m_sizeSlider) {
        m_sizeSlider->setFixedHeight(m_sliderHeightFull);
    }
    if (m_opacitySlider) {
        m_opacitySlider->setFixedHeight(m_sliderHeightFull);
    }
    m_separator->setFixedHeight(1);
    m_separator->setStyleSheet(
        QStringLiteral("background:%1;").arg(colors.borderSubtle().name(QColor::HexArgb)));
    m_recentScrollArea->setFixedHeight(m_recentScrollHeightFull);

    const QString titleStyle
        = QStringLiteral("color:%1; font-size:%2px; font-weight:500; background:transparent;")
              .arg(colors.text.name(QColor::HexArgb))
              .arg(theme.scaled(10));
    m_titleLabel->setStyleSheet(titleStyle);

    const QString metaStyle = QStringLiteral("color:%1; font-size:%2px; background:transparent;")
                                  .arg(colors.textMuted.name(QColor::HexArgb))
                                  .arg(theme.scaled(11));
    m_sizeLabel->setText(tr("Size:"));
    m_opacityLabel->setText(tr("Opacity:"));
    m_sizeLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_opacityLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_sizeLabel->setStyleSheet(metaStyle);
    m_opacityLabel->setStyleSheet(metaStyle);
    m_recentLabel->setStyleSheet(metaStyle);
    if (m_metricsWidget) {
        m_metricsWidget->setStyleSheet(QStringLiteral("background:transparent;"));
    }
    if (m_recentContent) {
        m_recentContent->setStyleSheet(QStringLiteral("background:transparent;"));
    }

    m_sizeLabel->adjustSize();
    m_opacityLabel->adjustSize();
    const int labelWidth
        = qMax(m_sizeLabel->sizeHint().width(), m_opacityLabel->sizeHint().width());
    m_sizeLabel->setFixedWidth(labelWidth);
    m_opacityLabel->setFixedWidth(labelWidth);

    rebuildRecentBrushes();
    applyPresentationLayout(showProgress());
}

bool CanvasBrushQuickPopup::containsGlobalPoint(const QPoint& globalPos) const
{
    // Hit-test against the visible panel, excluding the transparent shadow margin.
    const QRect panelRect
        = rect().adjusted(m_shadowMargin, m_shadowMargin, -m_shadowMargin, -m_shadowMargin);
    return QRect(mapToGlobal(panelRect.topLeft()), panelRect.size()).contains(globalPos);
}

} // namespace ruwa::ui::widgets
