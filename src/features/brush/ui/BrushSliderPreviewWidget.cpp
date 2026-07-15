// SPDX-License-Identifier: MPL-2.0

// BrushSliderPreviewWidget.cpp
#include "BrushSliderPreviewWidget.h"
#include "features/brush/manager/BrushPreviewManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "features/brush/ui/BrushSizeCurve.h"
#include "shared/style/PaintingUtils.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QGraphicsOpacityEffect>
#include <QCursor>
#include <QAbstractAnimation>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

bool dynamicsBindingsEqual(const ruwa::core::brushes::BrushDynamicsModel& a,
    const ruwa::core::brushes::BrushDynamicsModel& b)
{
    for (std::size_t slotIndex = 0; slotIndex < a.settingSlots.size(); ++slotIndex) {
        const auto& lhsSlot = a.settingSlots[slotIndex];
        const auto& rhsSlot = b.settingSlots[slotIndex];
        if (lhsSlot.setting != rhsSlot.setting) {
            return false;
        }

        for (std::size_t bindingIndex = 0; bindingIndex < lhsSlot.bindings.size(); ++bindingIndex) {
            const auto& lhs = lhsSlot.bindings[bindingIndex];
            const auto& rhs = rhsSlot.bindings[bindingIndex];
            if (lhs.setting != rhs.setting || lhs.source != rhs.source || lhs.mode != rhs.mode
                || lhs.enabled != rhs.enabled
                || lhs.curve.points.size() != rhs.curve.points.size()) {
                return false;
            }

            for (std::size_t pointIndex = 0; pointIndex < lhs.curve.points.size(); ++pointIndex) {
                const auto& lp = lhs.curve.points[pointIndex];
                const auto& rp = rhs.curve.points[pointIndex];
                if (lp.x != rp.x || lp.y != rp.y || lp.smoothness != rp.smoothness) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool brushSettingsEqual(const ruwa::core::brushes::BrushSettingsData& a,
    const ruwa::core::brushes::BrushSettingsData& b)
{
    return a.flowBlendMode == b.flowBlendMode && a.hardness == b.hardness && a.spacing == b.spacing
        && a.flow == b.flow && a.roundness == b.roundness && a.angle == b.angle
        && a.sizePressureEnabled == b.sizePressureEnabled
        && a.opacityPressureEnabled == b.opacityPressureEnabled && a.brushFeather == b.brushFeather
        && a.opacityPressureMin == b.opacityPressureMin
        && a.opacityPressureMax == b.opacityPressureMax && a.sizePressureMin == b.sizePressureMin
        && a.sizePressureMax == b.sizePressureMax && a.flowPressureMin == b.flowPressureMin
        && a.flowPressureMax == b.flowPressureMax && dynamicsBindingsEqual(a.dynamics, b.dynamics)
        && a.textureType == b.textureType && a.textureAmount == b.textureAmount
        && a.textureScale == b.textureScale && a.textureContrast == b.textureContrast
        && a.textureDepth == b.textureDepth && a.textureBlend == b.textureBlend
        && a.textureEdgeBoost == b.textureEdgeBoost && a.scatterPosition == b.scatterPosition
        && a.postCorrection == b.postCorrection && a.stabilization == b.stabilization
        && a.startTaper == b.startTaper && a.endTaper == b.endTaper
        && a.adjustCorrectionBySpeed == b.adjustCorrectionBySpeed
        && a.startCorrectionEnabled == b.startCorrectionEnabled
        && a.startCorrectionLength == b.startCorrectionLength
        && a.endCorrectionEnabled == b.endCorrectionEnabled
        && a.endCorrectionLength == b.endCorrectionLength && a.wetMix == b.wetMix
        && a.colorBlending == b.colorBlending && a.colorLength == b.colorLength
        && a.colorDilution == b.colorDilution && a.colorSpread == b.colorSpread
        && a.colorWetFlow == b.colorWetFlow && a.colorDryRate == b.colorDryRate
        && a.colorBuildup == b.colorBuildup;
}

} // namespace

BrushSliderPreviewWidget::BrushSliderPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAutoFillBackground(false);

    auto* opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(0.0);
    setGraphicsEffect(opacityEffect);

    m_previewSession = ruwa::core::brushes::BrushPreviewManager::instance().createSession(
        ruwa::core::brushes::BrushPreviewSession::Kind::Dot, this);
    connect(m_previewSession, &ruwa::core::brushes::BrushPreviewSession::imageChanged, this,
        QOverload<>::of(&BrushSliderPreviewWidget::update));

    setupAnimations();
    updateSize();
    updateLabelText();

    // Visibility-gated: deferred for hidden (background-tab) instances; flushed
    // on activation via WorkspaceTab.
    ThemeManager::instance().registerThemeHandler(this, [this]() { onThemeChanged(); });
}

BrushSliderPreviewWidget::~BrushSliderPreviewWidget() = default;

void BrushSliderPreviewWidget::setSliderType(SliderType type)
{
    if (m_sliderType == type)
        return;
    m_sliderType = type;
    updateLabelText();
    update();
}

void BrushSliderPreviewWidget::setBrushSettings(
    const ruwa::core::brushes::BrushSettingsData& settings)
{
    if (brushSettingsEqual(m_brushSettings, settings)) {
        return;
    }
    m_brushSettings = settings;
    update();
}

void BrushSliderPreviewWidget::setBrushSize(qreal size)
{
    size = qBound(0.0, size, 1.0);
    if (qFuzzyCompare(m_brushSize, size))
        return;
    m_brushSize = size;
    updateLabelText();
    update();
}

void BrushSliderPreviewWidget::setBrushOpacity(qreal opacity)
{
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_brushOpacity, opacity))
        return;
    m_brushOpacity = opacity;
    updateLabelText();
    update();
}

void BrushSliderPreviewWidget::setBrushColor(const QColor& color)
{
    if (m_brushColor == color)
        return;
    m_brushColor = color;
    update();
}

void BrushSliderPreviewWidget::setCanvasSize(const QSize& size)
{
    if (m_canvasSize == size)
        return;
    m_canvasSize = size;
    updateLabelText();
    update();
}

void BrushSliderPreviewWidget::setHasFiniteDocumentBounds(bool hasFiniteDocumentBounds)
{
    if (m_hasFiniteDocumentBounds == hasFiniteDocumentBounds)
        return;
    m_hasFiniteDocumentBounds = hasFiniteDocumentBounds;
    updateLabelText();
    update();
}

void BrushSliderPreviewWidget::showAnimated()
{
    if (m_isShowing && isVisible())
        return;

    m_isShowing = true;
    m_isHiding = false;

    QPoint targetPos = calculatePosition(QCursor::pos());

    QPoint startPos = targetPos;
    if (m_anchorWidget && parentWidget() && m_anchorWidget->parentWidget() == parentWidget()) {
        QPoint sourceInParent(m_anchorWidget->x(), m_anchorWidget->y());
        if (targetPos.x() < sourceInParent.x()) {
            startPos.setX(targetPos.x() + SlideOffset);
        } else {
            startPos.setX(targetPos.x() - SlideOffset);
        }
    }

    move(startPos);
    m_slidePos = startPos;
    show();
    raise();

    m_slideAnimation->stop();
    m_slideAnimation->setDuration(ShowDuration);
    m_slideAnimation->setStartValue(startPos);
    m_slideAnimation->setEndValue(targetPos);
    m_slideAnimation->start();

    m_showAnimation->stop();
    m_showAnimation->setDuration(ShowDuration);
    m_showAnimation->setStartValue(m_showProgress);
    m_showAnimation->setEndValue(1.0);
    m_showAnimation->start();
}

void BrushSliderPreviewWidget::hideAnimated()
{
    if (m_isHiding || !isVisible())
        return;

    m_isHiding = true;
    m_isShowing = false;

    QPoint startPos = pos();
    QPoint endPos = startPos;
    if (m_anchorWidget && parentWidget() && m_anchorWidget->parentWidget() == parentWidget()) {
        QPoint sourceInParent(m_anchorWidget->x(), m_anchorWidget->y());
        if (startPos.x() < sourceInParent.x()) {
            endPos.setX(startPos.x() + SlideOffset);
        } else {
            endPos.setX(startPos.x() - SlideOffset);
        }
    }

    m_slideAnimation->stop();
    m_slideAnimation->setDuration(HideDuration);
    m_slideAnimation->setStartValue(startPos);
    m_slideAnimation->setEndValue(endPos);
    m_slideAnimation->start();

    m_showAnimation->stop();
    m_showAnimation->setDuration(HideDuration);
    m_showAnimation->setStartValue(m_showProgress);
    m_showAnimation->setEndValue(0.0);
    m_showAnimation->start();
}

void BrushSliderPreviewWidget::setShowProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_showProgress, progress))
        return;
    m_showProgress = progress;

    if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect())) {
        effect->setOpacity(progress);
    }

    update();
}

void BrushSliderPreviewWidget::setSlidePos(const QPoint& pos)
{
    if (m_slidePos == pos)
        return;
    m_slidePos = pos;
    move(pos);
}

QPoint BrushSliderPreviewWidget::calculatePosition(const QPoint& globalPos) const
{
    if (!parentWidget() || !m_anchorWidget || m_anchorWidget->parentWidget() != parentWidget()) {
        return pos();
    }

    int containerWidth = parentWidget()->width();
    int containerHeight = parentWidget()->height();
    int w = width();
    int h = height();

    QPoint sourceInParent = m_anchorWidget->mapTo(parentWidget(), QPoint(0, 0));
    QSize sourceSize = m_anchorWidget->size();

    int x = sourceInParent.x() - w - OffsetFromSource;
    int y = parentWidget()->mapFromGlobal(globalPos).y() - h / 2;

    if (x < MinContainerMargin) {
        x = sourceInParent.x() + sourceSize.width() + OffsetFromSource;
    }

    x = qBound(MinContainerMargin, x, containerWidth - w - MinContainerMargin);
    y = qBound(MinContainerMargin, y, containerHeight - h - MinContainerMargin);

    return QPoint(x, y);
}

void BrushSliderPreviewWidget::updatePositionFromCursor(const QPoint& globalPos)
{
    if (!parentWidget())
        return;

    QPoint targetPos = calculatePosition(globalPos);

    if (m_isShowing && m_slideAnimation->state() == QAbstractAnimation::Running) {
        m_slideAnimation->setEndValue(targetPos);
    } else {
        m_slidePos = targetPos;
        move(targetPos);
    }
}

void BrushSliderPreviewWidget::setupAnimations()
{
    m_showAnimation = new QPropertyAnimation(this, "showProgress", this);
    m_showAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_slideAnimation = new QPropertyAnimation(this, "slidePos", this);
    m_slideAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_showAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            onHideAnimationFinished();
        } else if (m_isShowing) {
            onShowAnimationFinished();
        }
    });
}

void BrushSliderPreviewWidget::updateLabelText()
{
    if (m_sliderType == SliderType::Size) {
        const float radiusPx = brushRadiusFromNormalizedSizeForCanvasMode(
            m_brushSize, m_canvasSize.width(), m_canvasSize.height(), m_hasFiniteDocumentBounds);
        m_labelText = QString::number(qRound(radiusPx)) + QLatin1String(" px");
    } else {
        m_labelText = QString::number(qRound(m_brushOpacity * 100)) + QLatin1String("%");
    }
}

void BrushSliderPreviewWidget::updateSize()
{
    auto& theme = ThemeManager::instance();
    int previewSz = theme.scaled(PreviewSize);
    int labelH = theme.scaled(LabelHeight);
    int pad = theme.scaled(Padding);
    int gap1 = theme.scaled(GapPreviewToText);
    int gap2 = theme.scaled(GapTextToBottom);

    int w = 2 * pad + previewSz;
    int h = pad + previewSz + gap1 + labelH + gap2;

    setFixedSize(w, h);
}

void BrushSliderPreviewWidget::onThemeChanged()
{
    updateSize();
    update();
}

void BrushSliderPreviewWidget::onShowAnimationFinished()
{
    m_isShowing = false;
}

void BrushSliderPreviewWidget::onHideAnimationFinished()
{
    m_isHiding = false;
    QWidget::hide();
}

void BrushSliderPreviewWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_showProgress <= 0.001)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    drawBackground(painter);

    auto& theme = ThemeManager::instance();
    int pad = theme.scaled(Padding);
    int gap1 = theme.scaled(GapPreviewToText);
    int previewSz = theme.scaled(PreviewSize);

    QRectF previewRect(pad, pad, previewSz, previewSz);
    drawPreview(painter, previewRect);

    QRectF labelRect(pad, pad + previewSz + gap1, previewSz, theme.scaled(LabelHeight));
    drawLabel(painter, labelRect);
}

void BrushSliderPreviewWidget::drawBackground(QPainter& painter)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    QColor bgColor = mgr.colors().surface;
    QColor borderTopColor = mgr.colors().border;
    QColor borderBottomColor = borderTopColor.darker(110);
    int radius = theme.scaled(CornerRadius);

    QPainterPath bgPath;
    bgPath.addRoundedRect(rect(), radius, radius);

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawPath(bgPath);

    ruwa::ui::painting::drawGradientBorder(
        painter, rect(), radius, borderTopColor, borderBottomColor);
}

void BrushSliderPreviewWidget::drawPreview(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    int radius = theme.scaled(PreviewCornerRadius);
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, radius, radius);
    painter.save();
    painter.setClipPath(clipPath);

    const int cellSz = theme.scaled(4);
    const auto& colors = ThemeManager::instance().colors();
    const QColor lightCell = ThemeColors::adjustBrightness(colors.canvas(), 1.10);
    const QColor darkCell = ThemeColors::adjustBrightness(colors.canvasGrid(), 1.10);

    painter.setPen(Qt::NoPen);
    for (int y = static_cast<int>(rect.top()); y < static_cast<int>(rect.bottom()); y += cellSz) {
        for (int x = static_cast<int>(rect.left()); x < static_cast<int>(rect.right());
            x += cellSz) {
            bool isLight = (((x / cellSz) + (y / cellSz)) % 2) == 0;
            painter.setBrush(isLight ? lightCell : darkCell);
            painter.drawRect(x, y, cellSz, cellSz);
        }
    }

    int dotPx = qMax(16, static_cast<int>(rect.width()));
    qreal sizeNormForPreview = m_brushSize;
    if (m_sliderType == SliderType::Size) {
        sizeNormForPreview = brushPreviewSizeNormalizedForCanvasMode(
            m_brushSize, m_canvasSize.width(), m_canvasSize.height(), m_hasFiniteDocumentBounds);
    }

    ruwa::core::brushes::BrushPreviewSpec previewSpec;
    previewSpec.settings = m_brushSettings;
    previewSpec.sizeNorm = sizeNormForPreview;
    previewSpec.opacityNorm = m_brushOpacity;
    previewSpec.color = m_brushColor;
    previewSpec.size = QSize(dotPx, dotPx);

    if (m_previewSession && !m_previewSession->hasImageFor(previewSpec)) {
        m_previewSession->request(previewSpec);
    }

    const QImage dotImg = m_previewSession ? m_previewSession->image() : QImage();
    if (!dotImg.isNull()) {
        painter.drawImage(rect.toRect(), dotImg);
    }

    painter.restore();

    QColor borderColor = mgr.colors().border;
    borderColor.setAlpha(80);
    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

void BrushSliderPreviewWidget::drawLabel(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    painter.setPen(mgr.colors().textMuted);
    QFont f = painter.font();
    f.setPixelSize(theme.scaled(11));
    painter.setFont(f);

    painter.drawText(rect, Qt::AlignCenter,
        painter.fontMetrics().elidedText(
            m_labelText, Qt::ElideRight, static_cast<int>(rect.width())));
}

} // namespace ruwa::ui::widgets
