// SPDX-License-Identifier: MPL-2.0

// ColorPicker.cpp
#include "ColorPicker.h"
#include "ColorSlotSwitchWidget.h"
#include "shared/widgets/BaseStyledWidget.h"
#include "shared/widgets/inputs/HexColorInput.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QBrush>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QtMath>
#include <QGraphicsOpacityEffect>
#include <QGraphicsEffect>
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QTransform>
#include <QApplication>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QTabletEvent>
#include <cmath>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

// Display-only effect that cross-fades the whole picker between its normal colors
// and a luminance grayscale. `amount` 0 = untouched, 1 = fully grayscale; values
// in between are a per-pixel lerp toward each pixel's luminance, which is exactly
// how the painted color would read on a mask. No Q_OBJECT: the strength is driven
// externally by a QVariantAnimation, so the class needs no meta-object.
class GrayscaleEffect : public QGraphicsEffect {
public:
    explicit GrayscaleEffect(QObject* parent = nullptr)
        : QGraphicsEffect(parent)
    {
    }

    qreal amount() const { return m_amount; }

    void setAmount(qreal amount)
    {
        amount = qBound(0.0, amount, 1.0);
        if (qFuzzyCompare(m_amount + 1.0, amount + 1.0)) {
            return;
        }
        m_amount = amount;
        update(); // repaint the source through draw()
    }

protected:
    void draw(QPainter* painter) override
    {
        if (m_amount <= 0.0) {
            drawSource(painter);
            return;
        }

        QPoint offset;
        const QPixmap pixmap = sourcePixmap(Qt::DeviceCoordinates, &offset);
        if (pixmap.isNull()) {
            drawSource(painter);
            return;
        }

        // High-DPI scaling is disabled app-wide (devicePixelRatio == 1), so the
        // device pixmap maps 1:1 to logical pixels and we can blit it back with an
        // identity transform.
        QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        const qreal a = m_amount;
        const int h = image.height();
        const int w = image.width();
        for (int y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const QRgb p = line[x];
                const int alpha = qAlpha(p);
                if (alpha == 0) {
                    continue;
                }
                const int r = qRed(p);
                const int g = qGreen(p);
                const int b = qBlue(p);
                // Rec. 601 luma — perceptual brightness == mask reveal strength.
                const int gray = qRound(0.299 * r + 0.587 * g + 0.114 * b);
                const int nr = r + qRound((gray - r) * a);
                const int ng = g + qRound((gray - g) * a);
                const int nb = b + qRound((gray - b) * a);
                line[x] = qRgba(nr, ng, nb, alpha);
            }
        }

        const QTransform restore = painter->worldTransform();
        painter->setWorldTransform(QTransform());
        painter->drawImage(offset, image);
        painter->setWorldTransform(restore);
    }

private:
    qreal m_amount = 0.0;
};

QString colorDebugString(const QColor& color)
{
    return QStringLiteral("%1(0x%2)")
        .arg(color.name(QColor::HexArgb),
            QString::number(color.rgba(), 16).rightJustified(8, QLatin1Char('0')));
}

void drawSoftShadow(
    QPainter& painter, const QRectF& panelRect, qreal radius, int spread, const QColor& shadowColor)
{
    if (spread <= 0) {
        return;
    }

    const qreal edgeOpacity = qBound(0.0, shadowColor.alphaF(), 1.0);
    const qreal peak = edgeOpacity * 3.0 / spread;

    painter.save();
    painter.setPen(Qt::NoPen);
    for (int i = spread; i >= 1; --i) {
        const qreal t = static_cast<qreal>(i) / spread;
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

} // namespace

ColorPicker::ColorPicker(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupAnimations();
    connectSignals();

    setColor(Qt::white);
    updateSize();

    // Start hidden (unless setEmbeddedMode(true) is called, e.g. by ColorPanel)
    if (!m_embeddedMode) {
        QWidget::hide();
        m_showProgress = 0.0;
    }
}

ColorPicker::~ColorPicker() = default;

void ColorPicker::setMaskEditMode(bool active)
{
    if (m_maskEditMode == active) {
        return;
    }
    m_maskEditMode = active;

    // Create the effect + its driver lazily, so users who never paint on a mask
    // never put the picker on the (slightly slower) graphics-effect render path.
    if (!m_maskGrayscaleEffect) {
        auto* effect = new GrayscaleEffect(this);
        effect->setEnabled(false);
        setGraphicsEffect(effect); // widget takes ownership
        m_maskGrayscaleEffect = effect;

        m_maskGrayscaleAnim = new QVariantAnimation(this);
        m_maskGrayscaleAnim->setDuration(MaskGrayscaleDuration);
        m_maskGrayscaleAnim->setEasingCurve(QEasingCurve::InOutCubic);
        connect(m_maskGrayscaleAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                if (m_maskGrayscaleEffect) {
                    static_cast<GrayscaleEffect*>(m_maskGrayscaleEffect)->setAmount(value.toReal());
                }
            });
        connect(m_maskGrayscaleAnim, &QVariantAnimation::finished, this, [this]() {
            // Once fully faded back to color, leave the effect path entirely.
            auto* effect = static_cast<GrayscaleEffect*>(m_maskGrayscaleEffect);
            if (effect && !m_maskEditMode && effect->amount() <= 0.0) {
                effect->setEnabled(false);
            }
        });
    }

    auto* effect = static_cast<GrayscaleEffect*>(m_maskGrayscaleEffect);
    m_maskGrayscaleAnim->stop();
    effect->setEnabled(true); // render through the effect for the whole transition
    m_maskGrayscaleAnim->setStartValue(effect->amount());
    m_maskGrayscaleAnim->setEndValue(active ? 1.0 : 0.0);
    m_maskGrayscaleAnim->start();
}

void ColorPicker::setupUI()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);

    auto& theme = ThemeManager::instance();

    // Hex input
    m_hexInput = new HexColorInput(this);

    // Opacity effect for hex input animation
    auto* hexOpacity = new QGraphicsOpacityEffect(m_hexInput);
    m_hexInput->setGraphicsEffect(hexOpacity);
    hexOpacity->setOpacity(0.0);

    connect(m_hexInput, &QLineEdit::editingFinished, this, &ColorPicker::onHexEditingFinished);

    m_colorSlotSwitch = new ColorSlotSwitchWidget(this);
    m_colorSlotSwitch->setFixedSize(theme.scaled(ColorSwitchWidth), theme.scaled(HexInputHeight));
    m_colorSlotSwitch->setVisible(false);
    connect(m_colorSlotSwitch, &ColorSlotSwitchWidget::activeSlotChanged, this,
        &ColorPicker::setActiveColorSlot);
    connect(m_colorSlotSwitch, &ColorSlotSwitchWidget::swapRequested, this, [this]() {
        if (!m_dualColorMode)
            return;
        emit swapColorsRequested();
    });
}

void ColorPicker::setupAnimations()
{
    m_showAnimation = new QPropertyAnimation(this, "showProgress", this);
    m_showAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_showAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            onHideAnimationFinished();
        } else if (m_isShowing) {
            onShowAnimationFinished();
        }
    });

    // Slide animation for mode transitions
    m_slideAnimation = new QPropertyAnimation(this, "slideProgress", this);
    m_slideAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_slideAnimation->setDuration(SlideDuration);
    connect(m_slideAnimation, &QPropertyAnimation::finished, this, [this]() {
        m_slideSnapshot = QPixmap();
        m_slideNewSnapshot = QPixmap();
        m_slideProgress = 1.0;
        if (m_hexInput)
            m_hexInput->show();
        if (m_colorSlotSwitch && m_dualColorMode)
            m_colorSlotSwitch->show();
        update();
    });

    m_recentHoverAnimation = new QPropertyAnimation(this, "recentHoverProgress", this);
    m_recentHoverAnimation->setDuration(160);
    m_recentHoverAnimation->setEasingCurve(QEasingCurve::OutCubic);

    m_recentLayoutAnimation = new QPropertyAnimation(this, "recentLayoutProgress", this);
    m_recentLayoutAnimation->setDuration(RecentColorsLayoutDuration);
    m_recentLayoutAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_recentLayoutAnimation, &QPropertyAnimation::finished, this, [this]() {
        m_recentPreviousVisibleCount = m_recentTargetVisibleCount;
        m_recentLayoutProgress = 1.0;
        update();
    });

    m_closeHoverAnimation = new QPropertyAnimation(this, "closeHoverProgress", this);
    m_closeHoverAnimation->setDuration(140);
    m_closeHoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void ColorPicker::connectSignals()
{
    // Visibility-gated: deferred for hidden (background-tab) instances; flushed
    // on activation via WorkspaceTab.
    ThemeManager::instance().registerThemeHandler(this, [this]() { onThemeChanged(); });
}

void ColorPicker::showAnimated()
{
    if (m_isShowing || (isVisible() && !m_isHiding)) {
        return;
    }

    m_isShowing = true;
    m_isHiding = false;

    QWidget::show();
    raise();
    setFocus();

    m_showAnimation->stop();
    m_showAnimation->setDuration(ShowDuration);
    m_showAnimation->setStartValue(m_showProgress);
    m_showAnimation->setEndValue(1.0);
    m_showAnimation->start();
}

void ColorPicker::hideAnimated()
{
    if (m_isHiding || !isVisible()) {
        return;
    }

    m_isHiding = true;
    m_isShowing = false;

    m_showAnimation->stop();
    m_showAnimation->setDuration(HideDuration);
    m_showAnimation->setStartValue(m_showProgress);
    m_showAnimation->setEndValue(0.0);
    m_showAnimation->start();
}

bool ColorPicker::isActive() const
{
    return isVisible() && !m_isHiding;
}

bool ColorPicker::containsVisiblePopupPoint(const QPoint& pos) const
{
    if (m_embeddedMode) {
        return rect().contains(pos);
    }
    return popupPanelRect().contains(pos);
}

int ColorPicker::popupShadowMarginPixels() const
{
    return qRound(popupShadowMargin());
}

int ColorPicker::embeddedPreferredHeight() const
{
    auto& theme = ThemeManager::instance();
    const int pad = 0;
    const int modeH = m_modeSwitcherVisible
        ? static_cast<int>(theme.scaled(ModeSwitcherBtnSize) + theme.scaled(ModeSwitcherBottomGap))
        : 0;
    const int squareSize = theme.scaled(SVSquareSize);
    const int hexH = theme.scaled(HexInputHeight);
    const int hexSpacing = theme.scaled(HexInputSpacing);
    int h = pad + modeH + squareSize + hexSpacing + hexH + pad;
    if (m_recentColorsEnabled) {
        const int rcGap = theme.scaled(RecentColorsTopGap);
        const int rcSquare = theme.scaled(RecentColorsSquareSize);
        h += rcGap + rcSquare;
    }
    return h;
}

// ============================================================================
// Mode management
// ============================================================================

void ColorPicker::setPickerMode(PickerMode mode)
{
    if (m_pickerMode == mode)
        return;

    int oldIndex = static_cast<int>(m_pickerMode);
    int newIndex = static_cast<int>(mode);

    const bool canSlide = m_embeddedMode && isVisible() && width() > 0 && height() > 0;

    // Capture OLD snapshot (includes child widgets like hex input) before mode change
    if (canSlide) {
        m_slideAnimation->stop();
        m_slideSnapshot = grab();
        m_slideDirection = (newIndex > oldIndex) ? 1 : -1;
    }

    m_pickerMode = mode;
    m_dragMode = DragMode::None;
    updateSize();
    emit pickerModeChanged(mode);

    // Capture NEW snapshot after layout updated, then hide child widgets so they
    // don't render over the animation. They are re-shown on slide finish.
    if (canSlide) {
        m_slideNewSnapshot = grab();
        if (m_hexInput)
            m_hexInput->hide();
        if (m_colorSlotSwitch)
            m_colorSlotSwitch->hide();
        m_slideProgress = 0.0;
        m_slideAnimation->setStartValue(0.0);
        m_slideAnimation->setEndValue(1.0);
        m_slideAnimation->start();
    }
    update();
}

// ============================================================================
// Dual color mode
// ============================================================================

void ColorPicker::setDualColorModeEnabled(bool enabled)
{
    if (m_dualColorMode == enabled)
        return;
    m_dualColorMode = enabled;
    updateSize();
    updateDualModeGeometry();
    updateDualModeControls();
    update();
}

void ColorPicker::setForegroundColor(const QColor& color)
{
    const bool changed = (m_foregroundColor != color);
    m_foregroundColor = color;
    if (m_dualColorMode && m_editingForeground) {
        setColor(color);
    } else {
        if (!changed)
            return;
        updateDualModeControls();
    }
}

void ColorPicker::setBackgroundColor(const QColor& color)
{
    const bool changed = (m_backgroundColor != color);
    m_backgroundColor = color;
    if (m_dualColorMode && !m_editingForeground) {
        setColor(color);
    } else {
        if (!changed)
            return;
        updateDualModeControls();
    }
}

void ColorPicker::setActiveColorSlot(bool isForeground)
{
    if (m_editingForeground == isForeground) {
        return;
    }
    m_editingForeground = isForeground;
    emit activeColorSlotChanged(m_editingForeground);
    setColor(m_editingForeground ? m_foregroundColor : m_backgroundColor);
    updateDualModeControls();
}

void ColorPicker::setRecentColorsEnabled(bool enabled)
{
    if (m_recentColorsEnabled == enabled)
        return;
    m_recentColorsEnabled = enabled;
    updateSize();
    updateRecentColorsVisibility(isVisible());
    update();
}

void ColorPicker::setRecentColorsMaxCount(int count)
{
    m_recentColorsMaxCount = qBound(1, count, 50);
    while (m_recentColors.size() > m_recentColorsMaxCount) {
        m_recentColors.removeLast();
    }
    updateRecentColorsVisibility(isVisible());
    update();
}

void ColorPicker::setEmbeddedMode(bool embedded)
{
    if (m_embeddedMode == embedded)
        return;
    m_embeddedMode = embedded;
    if (embedded) {
        setAttribute(Qt::WA_TranslucentBackground, false);
        setShowProgress(1.0);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        QWidget::show();
        if (m_hexInput) {
            m_hexInput->setGraphicsEffect(nullptr);
        }
    } else {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (m_hexInput) {
            auto* hexOpacity = new QGraphicsOpacityEffect(m_hexInput);
            hexOpacity->setOpacity(m_showProgress);
            m_hexInput->setGraphicsEffect(hexOpacity);
        }
    }
    updateSize();
}

void ColorPicker::setShowProgress(qreal progress)
{
    if (qFuzzyCompare(m_showProgress, progress))
        return;
    m_showProgress = progress;

    if (m_hexInput) {
        if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(m_hexInput->graphicsEffect())) {
            effect->setOpacity(progress);
        }
    }

    update();
}

void ColorPicker::setSlideProgress(qreal progress)
{
    if (qFuzzyCompare(m_slideProgress, progress))
        return;
    m_slideProgress = progress;
    update();
}

void ColorPicker::setRecentHoverProgress(qreal progress)
{
    if (qFuzzyCompare(m_recentHoverProgress, progress))
        return;
    m_recentHoverProgress = progress;
    update();
}

void ColorPicker::setRecentLayoutProgress(qreal progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_recentLayoutProgress + 1.0, progress + 1.0))
        return;
    m_recentLayoutProgress = progress;
    update();
}

void ColorPicker::setCloseHoverProgress(qreal progress)
{
    if (qFuzzyCompare(m_closeHoverProgress, progress))
        return;
    m_closeHoverProgress = qBound(0.0, progress, 1.0);
    update(popupCloseButtonRect().toAlignedRect());
}

void ColorPicker::onShowAnimationFinished()
{
    m_isShowing = false;
}

void ColorPicker::onHideAnimationFinished()
{
    m_isHiding = false;
    QWidget::hide();
}

void ColorPicker::onThemeChanged()
{
    updateSize();
    updateDualModeControls();
    update();
}

void ColorPicker::updateSize()
{
    auto& theme = ThemeManager::instance();
    if (m_embeddedMode) {
        int minW = theme.scaled(96);
        const int fixedH = embeddedPreferredHeight();
        setMinimumWidth(minW);
        setMinimumHeight(fixedH);
        setMaximumHeight(QWIDGETSIZE_MAX);

        if (width() > 0 && m_hexInput) {
            positionHexInput();
        }
        updateDualModeGeometry();
    } else {
        const int shadow = theme.scaled(PopupShadowMargin);
        const int alphaExtra = qRound(alphaBarExtent());
        const int w = theme.scaled(PickerWidth) + shadow * 2;
        const int h = theme.scaled(PickerHeight) + alphaExtra + shadow * 2;
        setFixedSize(w, h);

        const int pad = theme.scaled(OverlayPadding);
        const int headerBlockHeight = theme.scaled(PopupHeaderHeight + PopupHeaderBottomGap);
        const int squareHeight = theme.scaled(OverlaySVHeight);
        const QRectF panel = popupPanelRect();
        const int inputX = qRound(panel.left()) + pad;
        const int inputY = qRound(panel.top()) + pad + headerBlockHeight + squareHeight
            + theme.scaled(HueBarSpacing) + theme.scaled(HueBarHeight) + alphaExtra
            + theme.scaled(HexInputSpacing);
        m_hexInput->setGeometry(
            inputX, inputY, theme.scaled(OverlaySVWidth), theme.scaled(HexInputHeight));
        updateDualModeGeometry();
    }
}

void ColorPicker::setAlphaSliderEnabled(bool enabled)
{
    if (m_alphaSliderEnabled == enabled) {
        return;
    }
    m_alphaSliderEnabled = enabled;
    if (!enabled) {
        // Disabling forces the color opaque again (matches the no-alpha default).
        m_alpha = 1.0;
        if (m_currentColor.alpha() < 255) {
            m_currentColor.setAlpha(255);
        }
    } else {
        m_alpha = m_currentColor.alphaF();
    }
    updateSize(); // popup grows/shrinks by one bar
    update();
}

void ColorPicker::positionHexInput()
{
    if (!m_embeddedMode || !m_hexInput)
        return;

    auto& theme = ThemeManager::instance();
    int hexSpacing = theme.scaled(HexInputSpacing);
    int hexH = theme.scaled(HexInputHeight);

    qreal upperBottom;
    int inputX;
    int inputW;

    if (m_pickerMode == PickerMode::Classic) {
        QRectF square = svSquareRect();
        QRectF hue = hueBarRect();
        upperBottom = square.bottom();
        if (m_recentColorsEnabled) {
            QRectF rc = recentColorsSectionRect();
            if (!rc.isEmpty())
                upperBottom = rc.bottom();
        }
        inputX = static_cast<int>(square.left());
        inputW = static_cast<int>(hue.right() - square.left());
    } else {
        QRectF area = ringAreaRect();
        upperBottom = area.bottom();
        if (m_recentColorsEnabled) {
            QRectF rc = recentColorsSectionRect();
            if (!rc.isEmpty())
                upperBottom = rc.bottom();
        }
        inputX = 0;
        inputW = width();
    }

    int inputY = static_cast<int>(upperBottom + hexSpacing);

    if (m_dualColorMode && m_colorSlotSwitch) {
        inputX += m_colorSlotSwitch->width() + theme.scaled(ColorSwitchGap);
        inputW -= m_colorSlotSwitch->width() + theme.scaled(ColorSwitchGap);
    }
    m_hexInput->setGeometry(inputX, inputY, qMax(60, inputW), hexH);
}

void ColorPicker::setColor(const QColor& color)
{
    m_initialColor = color;
    m_currentColor = color;
    if (m_dualColorMode) {
        if (m_editingForeground) {
            m_foregroundColor = color;
        } else {
            m_backgroundColor = color;
        }
    }

    m_alpha = m_alphaSliderEnabled ? color.alphaF() : 1.0;

    qreal newH = color.hueF();
    qreal newS = color.saturationF();
    qreal newV = color.valueF();
    if (newH < 0)
        newH = 0.0;

    const qreal eps = 1e-4;
    if (newV < eps) {
        m_value = 0.0;
    } else if (newS < eps) {
        m_hue = newH;
        m_saturation = newS;
        m_value = newV;
    } else {
        m_hue = newH;
        m_saturation = newS;
        m_value = newV;
    }

    updateHexInput();
    updateDualModeControls();
    update();
}

void ColorPicker::setPopupTitle(const QString& title)
{
    const QString nextTitle = title.trimmed().isEmpty() ? tr("Color") : title.trimmed();
    if (m_popupTitle == nextTitle) {
        return;
    }
    m_popupTitle = nextTitle;
    update();
}

void ColorPicker::updateFromHSV()
{
    m_currentColor = QColor::fromHsvF(m_hue, m_saturation, m_value);
    if (m_alphaSliderEnabled) {
        m_currentColor.setAlphaF(m_alpha);
    }
    if (m_dualColorMode) {
        if (m_editingForeground) {
            m_foregroundColor = m_currentColor;
        } else {
            m_backgroundColor = m_currentColor;
        }
        updateDualModeControls();
    }
    updateHexInput();
    emit colorChanged(m_currentColor);
    update();
}

void ColorPicker::updateHexInput()
{
    if (!m_hexInput) {
        return;
    }
    m_hexInput->setHex(m_currentColor.name(QColor::HexRgb));
}

void ColorPicker::onHexEditingFinished()
{
    QString text = m_hexInput->hexWithHash();

    if (text.length() == 7 || text.length() == 4) {
        QColor color(text);
        if (color.isValid()) {
            m_hexInput->blockSignals(true);

            m_hue = color.hueF();
            m_saturation = color.saturationF();
            m_value = color.valueF();
            if (m_hue < 0)
                m_hue = 0.0;

            m_currentColor = QColor::fromHsvF(m_hue, m_saturation, m_value);
            if (m_alphaSliderEnabled) {
                m_currentColor.setAlphaF(m_alpha);
            }
            if (m_dualColorMode) {
                if (m_editingForeground) {
                    m_foregroundColor = m_currentColor;
                } else {
                    m_backgroundColor = m_currentColor;
                }
                updateDualModeControls();
            }
            emit colorChanged(m_currentColor);
            update();

            m_hexInput->blockSignals(false);
        }
    }
}

// ============================================================================
// Classic mode geometry helpers
// ============================================================================

qreal ColorPicker::effectivePad() const
{
    if (m_embeddedMode) {
        // Reserve just enough room for cursors so they don't clip at the edges.
        return ThemeManager::instance().scaled(SVCursorRadius);
    }
    return ThemeManager::instance().scaled(OverlayPadding);
}

qreal ColorPicker::effectiveSquareSide() const
{
    auto& theme = ThemeManager::instance();
    return theme.scaled(m_embeddedMode ? SVSquareSize : OverlaySVWidth);
}

qreal ColorPicker::effectiveSVHeight() const
{
    auto& theme = ThemeManager::instance();
    return theme.scaled(m_embeddedMode ? SVSquareSize : OverlaySVHeight);
}

qreal ColorPicker::popupShadowMargin() const
{
    if (m_embeddedMode) {
        return 0.0;
    }
    return ThemeManager::instance().scaled(PopupShadowMargin);
}

QRectF ColorPicker::popupPanelRect() const
{
    const qreal sm = popupShadowMargin();
    return QRectF(sm, sm, qMax(0.0, width() - 2.0 * sm), qMax(0.0, height() - 2.0 * sm));
}

QRectF ColorPicker::popupHeaderRect() const
{
    if (m_embeddedMode) {
        return QRectF();
    }

    auto& theme = ThemeManager::instance();
    const QRectF panel = popupPanelRect();
    const qreal pad = theme.scaled(OverlayPadding);
    return QRectF(panel.left() + pad, panel.top() + pad, qMax(0.0, panel.width() - 2.0 * pad),
        theme.scaled(PopupHeaderHeight));
}

QRectF ColorPicker::popupCloseButtonRect() const
{
    const QRectF header = popupHeaderRect();
    if (header.isEmpty()) {
        return QRectF();
    }

    auto& theme = ThemeManager::instance();
    const qreal size = theme.scaled(PopupHeaderCloseSize);
    return QRectF(header.right() - size, header.center().y() - size * 0.5, size, size);
}

QRectF ColorPicker::svSquareRect() const
{
    qreal pad = effectivePad();
    if (m_embeddedMode) {
        auto& theme = ThemeManager::instance();
        const qreal modeH = modeSwitcherTotalHeight();
        const qreal hueGap = theme.scaled(HueBarSpacing);
        const qreal hueW = theme.scaled(HueBarHeight);
        const qreal hexH = theme.scaled(HexInputHeight);
        const qreal hexSpacing = theme.scaled(HexInputSpacing);
        const qreal upperSectionH = qMax(1.0, height() - 2 * pad - modeH - hexSpacing - hexH);
        const qreal rightReserved = hueGap + hueW;
        const qreal availW = qMax(1.0, width() - 2 * pad - rightReserved);
        qreal svH = upperSectionH;
        if (m_recentColorsEnabled) {
            const qreal rcGap = theme.scaled(RecentColorsTopGap);
            const qreal squareSize = theme.scaled(RecentColorsSquareSize);
            const qreal rcRowH = squareSize;
            svH = qMax(1.0, upperSectionH - rcGap - rcRowH);
        }
        return QRectF(pad, pad + modeH, availW, svH);
    }
    const QRectF panel = popupPanelRect();
    const QRectF header = popupHeaderRect();
    const qreal top = header.isEmpty()
        ? panel.top() + pad
        : header.bottom() + ThemeManager::instance().scaled(PopupHeaderBottomGap);
    return QRectF(panel.left() + pad, top, effectiveSquareSide(), effectiveSVHeight());
}

QRectF ColorPicker::hueBarRect() const
{
    auto& theme = ThemeManager::instance();
    QRectF sq = svSquareRect();
    const qreal spacing = theme.scaled(HueBarSpacing);
    const qreal barThickness = theme.scaled(HueBarHeight);
    const qreal pad = effectivePad();
    if (m_embeddedMode) {
        // Match the SV square's vertical extent so the bar never overlaps the
        // recent colors row below it.
        return QRectF(sq.right() + spacing, sq.top(), barThickness, sq.height());
    }
    return QRectF(sq.left(), sq.bottom() + spacing, sq.width(), barThickness);
}

qreal ColorPicker::alphaBarExtent() const
{
    if (!m_alphaSliderEnabled || m_embeddedMode) {
        return 0.0;
    }
    auto& theme = ThemeManager::instance();
    return theme.scaled(HueBarSpacing) + theme.scaled(HueBarHeight);
}

QRectF ColorPicker::alphaBarRect() const
{
    if (!m_alphaSliderEnabled || m_embeddedMode) {
        return QRectF();
    }
    auto& theme = ThemeManager::instance();
    const QRectF hue = hueBarRect();
    const qreal spacing = theme.scaled(HueBarSpacing);
    const qreal barThickness = theme.scaled(HueBarHeight);
    return QRectF(hue.left(), hue.bottom() + spacing, hue.width(), barThickness);
}

// ============================================================================
// Mode switcher
// ============================================================================

void ColorPicker::setModeSwitcherVisible(bool visible)
{
    if (m_modeSwitcherVisible == visible)
        return;
    m_modeSwitcherVisible = visible;
    updateSize();
    update();
}

qreal ColorPicker::modeSwitcherTotalHeight() const
{
    if (!m_embeddedMode || !m_modeSwitcherVisible)
        return 0.0;
    auto& theme = ThemeManager::instance();
    return theme.scaled(ModeSwitcherBtnSize) + theme.scaled(ModeSwitcherBottomGap);
}

QRectF ColorPicker::modeSwitcherButtonRect(int index) const
{
    if (!m_embeddedMode || index < 0 || index > 2)
        return QRectF();
    auto& theme = ThemeManager::instance();
    qreal btnSize = theme.scaled(ModeSwitcherBtnSize);
    qreal gap = theme.scaled(ModeSwitcherGap);
    qreal totalW = 3 * btnSize + 2 * gap;
    qreal startX = (width() - totalW) / 2.0;
    qreal x = startX + index * (btnSize + gap);
    return QRectF(x, 0, btnSize, btnSize);
}

int ColorPicker::hitTestModeSwitcher(const QPoint& pos) const
{
    if (!m_embeddedMode || !m_modeSwitcherVisible)
        return -1;
    for (int i = 0; i < 3; ++i) {
        if (modeSwitcherButtonRect(i).contains(pos))
            return i;
    }
    return -1;
}

void ColorPicker::drawModeSwitcher(QPainter& painter)
{
    if (!m_embeddedMode || !m_modeSwitcherVisible)
        return;

    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    for (int i = 0; i < 3; ++i) {
        QRectF btn = modeSwitcherButtonRect(i);
        if (btn.isEmpty())
            continue;

        PickerMode mode = static_cast<PickerMode>(i);
        bool active = (m_pickerMode == mode);

        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        qreal r = theme.scaled(4);

        // Button background for active mode
        if (active) {
            painter.setPen(Qt::NoPen);
            QColor bg = mgr.colors().primary;
            bg.setAlpha(40);
            painter.setBrush(bg);
            painter.drawRoundedRect(btn, r, r);
        }

        // Icon
        QColor iconColor = active ? mgr.colors().primary : mgr.colors().textMuted;
        qreal penW = theme.scaled(1.2);
        painter.setPen(QPen(iconColor, penW));
        painter.setBrush(Qt::NoBrush);

        qreal iconInset = btn.width() * 0.22;
        QRectF iconArea = btn.adjusted(iconInset, iconInset, -iconInset, -iconInset);

        switch (mode) {
        case PickerMode::Classic: {
            // Draw a small square (SV area) + thin bar (hue bar)
            qreal barW = iconArea.width() * 0.15;
            qreal gap = theme.scaled(1.5);
            QRectF sq(
                iconArea.left(), iconArea.top(), iconArea.width() - barW - gap, iconArea.height());
            QRectF bar(sq.right() + gap, iconArea.top(), barW, iconArea.height());
            painter.drawRect(sq);
            painter.drawRect(bar);
            break;
        }
        case PickerMode::Triangle: {
            // Circle with triangle inside
            qreal cr = iconArea.width() / 2.0;
            QPointF cc = iconArea.center();
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
        case PickerMode::Square: {
            // Circle with square inside
            qreal cr = iconArea.width() / 2.0;
            QPointF cc = iconArea.center();
            painter.drawEllipse(cc, cr, cr);
            qreal half = cr * 0.5;
            painter.drawRect(QRectF(cc.x() - half, cc.y() - half, half * 2, half * 2));
            break;
        }
        }

        painter.restore();
    }
}

// ============================================================================
// Ring-based mode geometry
// ============================================================================

QRectF ColorPicker::ringAreaRect() const
{
    if (!m_embeddedMode)
        return QRectF();

    auto& theme = ThemeManager::instance();
    const qreal pad = effectivePad();
    qreal top = modeSwitcherTotalHeight() + pad;
    qreal hexH = theme.scaled(HexInputHeight);
    qreal hexSpacing = theme.scaled(HexInputSpacing);
    qreal areaH = qMax(1.0, static_cast<qreal>(height()) - top - hexSpacing - hexH - pad);
    if (m_recentColorsEnabled) {
        qreal rcGap = theme.scaled(RecentColorsTopGap);
        qreal rcH = theme.scaled(RecentColorsSquareSize);
        areaH -= (rcGap + rcH);
    }
    return QRectF(pad, top, qMax(1.0, width() - 2 * pad), qMax(1.0, areaH));
}

QPointF ColorPicker::ringCenter() const
{
    QRectF area = ringAreaRect();
    return QPointF(area.left() + area.width() / 2.0, area.top() + area.height() / 2.0);
}

qreal ColorPicker::ringOuterRadius() const
{
    QRectF area = ringAreaRect();
    return qMin(area.width(), area.height()) / 2.0;
}

qreal ColorPicker::ringInnerRadius() const
{
    auto& theme = ThemeManager::instance();
    return qMax(1.0, ringOuterRadius() - theme.scaled(RingThickness));
}

// ============================================================================
// Hue ring drawing and interaction
// ============================================================================

void ColorPicker::drawHueRing(QPainter& painter)
{
    QPointF center = ringCenter();
    qreal outerR = ringOuterRadius();
    qreal innerR = ringInnerRadius();

    if (outerR <= 0 || innerR <= 0)
        return;

    // Ring path using OddEven fill rule
    QPainterPath ringPath;
    ringPath.setFillRule(Qt::OddEvenFill);
    ringPath.addEllipse(center, outerR, outerR);
    ringPath.addEllipse(center, innerR, innerR);

    // Conical gradient: hue=0 (red) at top, increasing counterclockwise
    QConicalGradient gradient(center, 90);
    for (int i = 0; i <= 6; ++i) {
        qreal t = qreal(i) / 6.0;
        gradient.setColorAt(t, QColor::fromHsvF(qMin(t, 0.9999), 1.0, 1.0));
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawPath(ringPath);

    // Borders
    auto& mgr = WidgetStyleManager::instance();
    painter.setPen(QPen(mgr.colors().border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, outerR, outerR);
    painter.drawEllipse(center, innerR, innerR);
    painter.restore();
}

void ColorPicker::drawRingHueCursor(QPainter& painter)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    QPointF center = ringCenter();
    qreal midR = (ringOuterRadius() + ringInnerRadius()) / 2.0;
    qreal ringW = ringOuterRadius() - ringInnerRadius();

    // Position on ring for current hue
    qreal cx = center.x() - midR * qSin(m_hue * 2.0 * M_PI);
    qreal cy = center.y() - midR * qCos(m_hue * 2.0 * M_PI);

    qreal cursorR = ringW / 2.0 + theme.scaled(2);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // White outline + fill with the hue color
    painter.setPen(QPen(Qt::white, theme.scaled(2)));
    painter.setBrush(QColor::fromHsvF(m_hue, 1.0, 1.0));
    painter.drawEllipse(QPointF(cx, cy), cursorR, cursorR);

    // Primary color accent ring
    painter.setPen(QPen(mgr.colors().primary, theme.scaled(1)));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(QPointF(cx, cy), cursorR + theme.scaled(1), cursorR + theme.scaled(1));
    painter.restore();
}

void ColorPicker::handleHueRingDrag(const QPoint& pos)
{
    QPointF center = ringCenter();
    qreal dx = pos.x() - center.x();
    qreal dy = center.y() - pos.y(); // invert y for math coords
    qreal theta = qAtan2(dy, dx); // angle from east, CCW positive
    qreal h = (theta - M_PI / 2.0) / (2.0 * M_PI);
    h = std::fmod(h + 1.0, 1.0);
    if (h < 0)
        h += 1.0;

    m_hue = qBound(0.0, h, 1.0);
    updateFromHSV();
}

// ============================================================================
// Triangle mode
// ============================================================================

QPolygonF ColorPicker::trianglePolygon() const
{
    auto& theme = ThemeManager::instance();
    QPointF center = ringCenter();
    qreal r = ringInnerRadius() - theme.scaled(RingInnerGap);
    if (r <= 0)
        return QPolygonF();

    // Fixed triangle (no rotation):
    //   Top vertex    = black  (V=0)
    //   Bottom-left   = white  (S=0, V=1)
    //   Bottom-right  = accent (S=1, V=1)
    // Equilateral triangle inscribed in circle of radius r, apex at top.
    const qreal sqrt3_2 = qSqrt(3.0) / 2.0;

    QPointF vb(center.x(), center.y() - r); // black (top)
    QPointF vw(center.x() - r * sqrt3_2, center.y() + r * 0.5); // white (bottom-left)
    QPointF vc(center.x() + r * sqrt3_2, center.y() + r * 0.5); // accent (bottom-right)

    // Order: [0]=accent(color), [1]=white, [2]=black
    // This preserves the barycentric mapping used by ensureTriangleCache / handleTriangleSVDrag.
    return QPolygonF({ vc, vw, vb });
}

void ColorPicker::ensureTriangleCache()
{
    QPolygonF tri = trianglePolygon();
    if (tri.size() < 3)
        return;

    QRectF bounds = tri.boundingRect().adjusted(-1, -1, 1, 1);
    QSize size(qCeil(bounds.width()), qCeil(bounds.height()));

    if (!m_triangleCache.isNull() && m_triangleCache.size() == size
        && qAbs(m_triangleCacheHue - m_hue) < 1e-6 && m_triangleCacheBounds == bounds) {
        return;
    }

    if (size.isEmpty())
        return;

    m_triangleCache = QImage(size, QImage::Format_RGB32);

    QPointF a = tri[0] - bounds.topLeft(); // color vertex
    QPointF b = tri[1] - bounds.topLeft(); // white vertex
    QPointF c = tri[2] - bounds.topLeft(); // black vertex

    // Precompute barycentric coordinate factors
    // u = weight of C (black), v = weight of B (white), w = weight of A (color)
    double v0x = c.x() - a.x(), v0y = c.y() - a.y();
    double v1x = b.x() - a.x(), v1y = b.y() - a.y();
    double d00 = v0x * v0x + v0y * v0y;
    double d01 = v0x * v1x + v0y * v1y;
    double d11 = v1x * v1x + v1y * v1y;
    double denom = d00 * d11 - d01 * d01;
    if (qAbs(denom) < 1e-12)
        return;
    double invDenom = 1.0 / denom;

    for (int y = 0; y < size.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(m_triangleCache.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            double v2x = x + 0.5 - a.x(), v2y = y + 0.5 - a.y();
            double d02 = v0x * v2x + v0y * v2y;
            double d12 = v1x * v2x + v1y * v2y;

            double u = (d11 * d02 - d01 * d12) * invDenom; // black weight
            double v = (d00 * d12 - d01 * d02) * invDenom; // white weight
            double w = 1.0 - u - v; // color weight

            // Clamp to triangle (for pixels outside, this produces the nearest valid color)
            u = qMax(0.0, u);
            v = qMax(0.0, v);
            w = qMax(0.0, w);
            double sum = u + v + w;
            if (sum > 0) {
                u /= sum;
                v /= sum;
                w /= sum;
            }

            double val = qBound(0.0, 1.0 - u, 1.0);
            double sat = (val > 1e-6) ? qBound(0.0, w / val, 1.0) : 0.0;

            line[x] = QColor::fromHsvF(m_hue, sat, val).rgb();
        }
    }

    m_triangleCacheHue = m_hue;
    m_triangleCacheBounds = bounds;
}

void ColorPicker::drawColorTriangle(QPainter& painter)
{
    QPolygonF tri = trianglePolygon();
    if (tri.size() < 3)
        return;

    ensureTriangleCache();
    if (m_triangleCache.isNull())
        return;

    QRectF bounds = tri.boundingRect().adjusted(-1, -1, 1, 1);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // Clip to triangle for clean anti-aliased edges
    QPainterPath clipPath;
    clipPath.addPolygon(tri);
    clipPath.closeSubpath();
    painter.setClipPath(clipPath);
    painter.drawImage(bounds.topLeft(), m_triangleCache);
    painter.restore();

    // Border
    auto& mgr = WidgetStyleManager::instance();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(mgr.colors().border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(tri);
    painter.restore();
}

QPointF ColorPicker::svToTrianglePos(qreal s, qreal v) const
{
    QPolygonF tri = trianglePolygon();
    if (tri.size() < 3)
        return QPointF();

    QPointF vc = tri[0]; // color: S=1, V=1
    QPointF vw = tri[1]; // white: S=0, V=1
    QPointF vb = tri[2]; // black: V=0

    // Barycentric weights from S,V
    qreal w_c = s * v; // color weight
    qreal w_w = v * (1.0 - s); // white weight
    qreal w_b = 1.0 - v; // black weight

    return QPointF(
        w_c * vc.x() + w_w * vw.x() + w_b * vb.x(), w_c * vc.y() + w_w * vw.y() + w_b * vb.y());
}

void ColorPicker::drawTriangleSVCursor(QPainter& painter)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    QPointF pos = svToTrianglePos(m_saturation, m_value);
    qreal radius = theme.scaled(SVCursorRadius);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(mgr.colors().primary);
    painter.drawEllipse(pos, radius, radius);
    painter.restore();
}

void ColorPicker::handleTriangleSVDrag(const QPoint& pos)
{
    QPolygonF tri = trianglePolygon();
    if (tri.size() < 3)
        return;

    QPointF a = tri[0]; // color
    QPointF b = tri[1]; // white
    QPointF c = tri[2]; // black

    // Compute barycentric coordinates
    double v0x = c.x() - a.x(), v0y = c.y() - a.y();
    double v1x = b.x() - a.x(), v1y = b.y() - a.y();
    double d00 = v0x * v0x + v0y * v0y;
    double d01 = v0x * v1x + v0y * v1y;
    double d11 = v1x * v1x + v1y * v1y;
    double denom = d00 * d11 - d01 * d01;
    if (qAbs(denom) < 1e-12)
        return;
    double invDenom = 1.0 / denom;

    double v2x = pos.x() - a.x(), v2y = pos.y() - a.y();
    double d02 = v0x * v2x + v0y * v2y;
    double d12 = v1x * v2x + v1y * v2y;

    double u = (d11 * d02 - d01 * d12) * invDenom; // black weight
    double v = (d00 * d12 - d01 * d02) * invDenom; // white weight
    double w = 1.0 - u - v; // color weight

    // Clamp to triangle
    u = qMax(0.0, u);
    v = qMax(0.0, v);
    w = qMax(0.0, w);
    double sum = u + v + w;
    if (sum > 0) {
        u /= sum;
        v /= sum;
        w /= sum;
    }

    m_value = qBound(0.0, 1.0 - u, 1.0);
    m_saturation = (m_value > 1e-6) ? qBound(0.0, w / m_value, 1.0) : 0.0;

    updateFromHSV();
}

// ============================================================================
// Square-in-ring mode
// ============================================================================

QRectF ColorPicker::squareInRingRect() const
{
    auto& theme = ThemeManager::instance();
    QPointF center = ringCenter();
    qreal innerR = ringInnerRadius() - theme.scaled(RingInnerGap);
    if (innerR <= 0)
        return QRectF();

    // Inscribe square in inner circle: half-side = innerR / sqrt(2)
    qreal halfSide = innerR / qSqrt(2.0);
    return QRectF(center.x() - halfSide, center.y() - halfSide, halfSide * 2, halfSide * 2);
}

void ColorPicker::drawSquareInRing(QPainter& painter)
{
    QRectF rect = squareInRingRect();
    if (rect.isEmpty())
        return;

    // Reuse SV square drawing with the ring-inscribed rect
    drawSVSquare(painter, rect);
}

void ColorPicker::drawSquareInRingSVCursor(QPainter& painter)
{
    QRectF rect = squareInRingRect();
    if (rect.isEmpty())
        return;
    drawSVCursor(painter, rect);
}

void ColorPicker::handleSquareInRingSVDrag(const QPoint& pos)
{
    QRectF rect = squareInRingRect();
    if (rect.isEmpty())
        return;

    m_saturation = qBound(0.0, (pos.x() - rect.left()) / rect.width(), 1.0);
    m_value = qBound(0.0, 1.0 - (pos.y() - rect.top()) / rect.height(), 1.0);
    updateFromHSV();
}

// ============================================================================
// Hit testing (ring modes)
// ============================================================================

ColorPicker::DragMode ColorPicker::hitTestRingMode(const QPoint& pos) const
{
    QPointF center = ringCenter();
    qreal dx = pos.x() - center.x();
    qreal dy = pos.y() - center.y();
    qreal dist = qSqrt(dx * dx + dy * dy);

    qreal innerR = ringInnerRadius();
    qreal outerR = ringOuterRadius();

    if (dist >= innerR && dist <= outerR) {
        return DragMode::HueRing;
    }

    if (m_pickerMode == PickerMode::Triangle) {
        QPolygonF tri = trianglePolygon();
        if (tri.containsPoint(QPointF(pos), Qt::OddEvenFill)) {
            return DragMode::TriangleSV;
        }
    } else if (m_pickerMode == PickerMode::Square) {
        QRectF sq = squareInRingRect();
        if (sq.contains(pos)) {
            return DragMode::SquareSV;
        }
    }

    return DragMode::None;
}

// ============================================================================
// Recent colors
// ============================================================================

int ColorPicker::effectiveRecentColorsMaxCount() const
{
    if (!m_recentColorsEnabled || !m_embeddedMode)
        return 1;
    auto& theme = ThemeManager::instance();
    const qreal gap = theme.scaled(RecentColorsGap);
    const qreal baseSquareSize = theme.scaled(RecentColorsSquareSize);

    qreal containerW;
    if (m_pickerMode == PickerMode::Classic) {
        if (m_embeddedMode) {
            const qreal pad = effectivePad();
            containerW = qMax<qreal>(0, width() - 2 * pad);
        } else {
            containerW = svSquareRect().width();
        }
    } else {
        containerW = width();
    }

    if (containerW <= 0 || baseSquareSize + gap <= 0)
        return 1;
    const int n = qMax(1,
        qMin(
            m_recentColorsMaxCount, static_cast<int>((containerW + gap) / (baseSquareSize + gap))));
    return n;
}

int ColorPicker::targetRecentColorsVisibleCount() const
{
    if (!m_recentColorsEnabled) {
        return 0;
    }
    return qMin(m_recentColors.size(), effectiveRecentColorsMaxCount());
}

QRectF ColorPicker::recentColorsSectionRect() const
{
    if (!m_recentColorsEnabled || !m_embeddedMode)
        return QRectF();
    auto& theme = ThemeManager::instance();

    const qreal gap = theme.scaled(RecentColorsGap);
    const qreal squareSize = theme.scaled(RecentColorsSquareSize);
    const int maxCount = effectiveRecentColorsMaxCount();
    const int count = qMin(m_recentColors.size(), maxCount);
    const qreal totalW = count > 0 ? count * squareSize + (count - 1) * gap : squareSize;

    qreal topRef;
    qreal leftRef;
    if (m_pickerMode == PickerMode::Classic) {
        QRectF svGradient = svSquareRect();
        topRef = svGradient.bottom();
        leftRef = m_embeddedMode ? effectivePad() : svGradient.left();
    } else {
        QRectF area = ringAreaRect();
        topRef = area.bottom();
        leftRef = 0;
    }

    const qreal topGap = theme.scaled(RecentColorsTopGap);
    return QRectF(leftRef, topRef + topGap, totalW, squareSize);
}

QRectF ColorPicker::recentColorSquareRect(int index) const
{
    if (index < 0 || index >= targetRecentColorsVisibleCount())
        return QRectF();
    return recentColorSquareBaseRect(index);
}

QRectF ColorPicker::recentColorSquareBaseRect(int index) const
{
    if (index < 0 || index >= m_recentColors.size())
        return QRectF();
    auto& theme = ThemeManager::instance();
    QRectF section = recentColorsSectionRect();
    const qreal squareSize = theme.scaled(RecentColorsSquareSize);
    const qreal gap = theme.scaled(RecentColorsGap);
    const qreal x = section.left() + index * (squareSize + gap);
    return QRectF(x, section.top(), squareSize, squareSize);
}

qreal ColorPicker::recentColorVisibility(int index) const
{
    if (index < 0 || index >= m_recentColors.size()) {
        return 0.0;
    }

    const int previous = qMin(m_recentPreviousVisibleCount, m_recentColors.size());
    const int target = qMin(m_recentTargetVisibleCount, m_recentColors.size());
    const qreal progress = qBound(0.0, m_recentLayoutProgress, 1.0);

    if (previous == target) {
        return index < target ? 1.0 : 0.0;
    }
    if (target > previous) {
        if (index < previous)
            return 1.0;
        if (index < target)
            return progress;
        return 0.0;
    }

    if (index < target)
        return 1.0;
    if (index < previous)
        return 1.0 - progress;
    return 0.0;
}

void ColorPicker::updateRecentColorsVisibility(bool animated)
{
    const int nextCount = targetRecentColorsVisibleCount();
    const int currentTarget = m_recentTargetVisibleCount;

    if (!animated || !m_embeddedMode || !isVisible()) {
        if (m_recentLayoutAnimation) {
            m_recentLayoutAnimation->stop();
        }
        m_recentPreviousVisibleCount = nextCount;
        m_recentTargetVisibleCount = nextCount;
        m_recentLayoutProgress = 1.0;
        return;
    }

    if (nextCount == currentTarget) {
        return;
    }

    if (m_recentLayoutAnimation) {
        m_recentLayoutAnimation->stop();
    }
    m_recentPreviousVisibleCount = currentTarget;
    m_recentTargetVisibleCount = nextCount;
    m_recentLayoutProgress = 0.0;

    if (m_hoveredRecentIndex >= nextCount) {
        m_hoveredRecentIndex = -1;
        if (m_recentHoverAnimation) {
            m_recentHoverAnimation->stop();
            m_recentHoverAnimation->setStartValue(m_recentHoverProgress);
            m_recentHoverAnimation->setEndValue(0.0);
            m_recentHoverAnimation->start();
        }
    }

    if (m_recentLayoutAnimation) {
        m_recentLayoutAnimation->setStartValue(0.0);
        m_recentLayoutAnimation->setEndValue(1.0);
        m_recentLayoutAnimation->start();
    } else {
        setRecentLayoutProgress(1.0);
    }
}

int ColorPicker::hitTestRecentColor(const QPoint& pos) const
{
    if (!m_recentColorsEnabled)
        return -1;
    const int maxCount = targetRecentColorsVisibleCount();
    for (int i = 0; i < maxCount; ++i) {
        if (recentColorSquareRect(i).contains(pos))
            return i;
    }
    return -1;
}

void ColorPicker::addColorToRecent(const QColor& color)
{
    if (!m_recentColorsEnabled)
        return;
    QColor normalized = color.toRgb();
    int existing = -1;
    for (int i = 0; i < m_recentColors.size(); ++i) {
        if (m_recentColors[i].toRgb() == normalized) {
            existing = i;
            break;
        }
    }
    if (existing >= 0) {
        moveRecentColorToTop(existing);
        return;
    }
    m_recentColors.prepend(normalized);
    const int maxCount = m_recentColorsMaxCount;
    while (m_recentColors.size() > maxCount) {
        m_recentColors.removeLast();
    }
    updateRecentColorsVisibility(isVisible());
    emit recentColorsChanged();
    update();
}

void ColorPicker::moveRecentColorToTop(int index)
{
    if (index <= 0 || index >= m_recentColors.size())
        return;
    m_recentColors.move(index, 0);
    updateRecentColorsVisibility(false);
    emit recentColorsChanged();
    update();
}

void ColorPicker::setRecentColors(const QVector<QColor>& colors)
{
    m_recentColors.clear();
    for (const QColor& c : colors) {
        if (m_recentColors.size() >= m_recentColorsMaxCount)
            break;
        m_recentColors.append(c.toRgb());
    }
    updateRecentColorsVisibility(isVisible());
    update();
}

// ============================================================================
// Resize and layout
// ============================================================================

void ColorPicker::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_embeddedMode && m_hexInput) {
        positionHexInput();
        updateDualModeGeometry();
        updateRecentColorsVisibility(isVisible());
    }
}

void ColorPicker::updateDualModeGeometry()
{
    if (!m_colorSlotSwitch || !m_hexInput) {
        return;
    }

    if (!m_dualColorMode || !m_embeddedMode) {
        m_colorSlotSwitch->hide();
        return;
    }

    auto& theme = ThemeManager::instance();
    m_colorSlotSwitch->setFixedSize(theme.scaled(ColorSwitchWidth), theme.scaled(HexInputHeight));
    const int switchW = m_colorSlotSwitch->width();
    const int x = m_hexInput->x() - theme.scaled(ColorSwitchGap) - switchW;
    const int y = m_hexInput->y() + (m_hexInput->height() - m_colorSlotSwitch->height()) / 2;
    m_colorSlotSwitch->move(qMax(0, x), y);
    m_colorSlotSwitch->show();
}

void ColorPicker::updateDualModeControls()
{
    if (!m_colorSlotSwitch) {
        return;
    }

    if (!m_dualColorMode || !m_embeddedMode) {
        m_colorSlotSwitch->hide();
        return;
    }

    m_colorSlotSwitch->setForegroundColor(m_foregroundColor);
    m_colorSlotSwitch->setBackgroundColor(m_backgroundColor);
    m_colorSlotSwitch->setActiveForeground(m_editingForeground);
    m_colorSlotSwitch->show();
}

// ============================================================================
// Hit testing and interaction
// ============================================================================

ColorPicker::DragMode ColorPicker::hitTest(const QPoint& pos) const
{
    if (m_embeddedMode && m_pickerMode != PickerMode::Classic) {
        return hitTestRingMode(pos);
    }
    if (svSquareRect().contains(pos))
        return DragMode::SV;
    if (hueBarRect().contains(pos))
        return DragMode::Hue;
    if (m_alphaSliderEnabled && alphaBarRect().contains(pos))
        return DragMode::Alpha;
    return DragMode::None;
}

bool ColorPicker::hitTestPopupClose(const QPoint& pos) const
{
    return !m_embeddedMode && popupCloseButtonRect().contains(pos);
}

void ColorPicker::handleDrag(const QPoint& pos)
{
    switch (m_dragMode) {
    case DragMode::SV: {
        QRectF rect = svSquareRect();
        qreal s = qBound(0.0, (pos.x() - rect.left()) / rect.width(), 1.0);
        qreal v = qBound(0.0, 1.0 - (pos.y() - rect.top()) / rect.height(), 1.0);
        m_saturation = s;
        m_value = v;
        updateFromHSV();
        break;
    }
    case DragMode::Hue: {
        QRectF rect = hueBarRect();
        qreal h = 0.0;
        if (m_embeddedMode) {
            h = qBound(0.0, (pos.y() - rect.top()) / rect.height(), 1.0);
        } else {
            h = qBound(0.0, (pos.x() - rect.left()) / rect.width(), 1.0);
        }
        m_hue = h;
        updateFromHSV();
        break;
    }
    case DragMode::Alpha: {
        QRectF rect = alphaBarRect();
        if (rect.width() > 0.0) {
            m_alpha = qBound(0.0, (pos.x() - rect.left()) / rect.width(), 1.0);
            updateFromHSV();
        }
        break;
    }
    case DragMode::HueRing:
        handleHueRingDrag(pos);
        break;
    case DragMode::TriangleSV:
        handleTriangleSVDrag(pos);
        break;
    case DragMode::SquareSV:
        handleSquareInRingSVDrag(pos);
        break;
    default:
        break;
    }
}

bool ColorPicker::shouldApplyDragUpdate(bool force)
{
    if (force) {
        if (m_dragUpdateTimer.isValid()) {
            m_dragUpdateTimer.restart();
        } else {
            m_dragUpdateTimer.start();
        }
        return true;
    }

    if (!m_dragUpdateTimer.isValid()) {
        m_dragUpdateTimer.start();
        return true;
    }

    if (m_dragUpdateTimer.elapsed() >= DragUpdateIntervalMs) {
        m_dragUpdateTimer.restart();
        return true;
    }

    return false;
}

void ColorPicker::setPopupCloseHovered(bool hovered)
{
    if (m_closeHovered == hovered) {
        return;
    }

    m_closeHovered = hovered;
    if (m_closeHoverAnimation) {
        m_closeHoverAnimation->stop();
        m_closeHoverAnimation->setStartValue(m_closeHoverProgress);
        m_closeHoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
        m_closeHoverAnimation->start();
    } else {
        setCloseHoverProgress(hovered ? 1.0 : 0.0);
    }
}

void ColorPicker::tabletEvent(QTabletEvent* event)
{
    switch (event->type()) {
    case QEvent::TabletPress: {
        if (event->button() == Qt::LeftButton) {
            if (hitTestPopupClose(event->position().toPoint())) {
                emit canceled();
                event->accept();
                return;
            }

            // Mode switcher
            if (m_embeddedMode) {
                int modeIdx = hitTestModeSwitcher(event->position().toPoint());
                if (modeIdx >= 0) {
                    setPickerMode(static_cast<PickerMode>(modeIdx));
                    event->accept();
                    return;
                }
            }
            const int recentIdx = hitTestRecentColor(event->position().toPoint());
            if (recentIdx >= 0) {
                const QColor color = m_recentColors[recentIdx];
                moveRecentColorToTop(recentIdx);
                setColor(color);
                emit colorChanged(color);
                event->accept();
                return;
            }
            m_tabletDragActive = true;
            m_dragMode = hitTest(event->position().toPoint());
            if (m_dragMode != DragMode::None) {
                handleDrag(event->position().toPoint());
                event->accept();
                return;
            }
        }
        break;
    }
    case QEvent::TabletMove:
        if (m_tabletDragActive && m_dragMode != DragMode::None) {
            if (shouldApplyDragUpdate(false)) {
                handleDrag(event->position().toPoint());
            }
            event->accept();
            return;
        }
        break;
    case QEvent::TabletRelease:
        if (m_tabletDragActive && m_dragMode != DragMode::None) {
            handleDrag(event->position().toPoint());
            m_dragMode = DragMode::None;
            m_tabletDragActive = false;
            event->accept();
            return;
        }
        m_tabletDragActive = false;
        break;
    default:
        break;
    }

    QWidget::tabletEvent(event);
}

void ColorPicker::mousePressEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        if (hitTestPopupClose(event->pos())) {
            m_closePressed = true;
            setPopupCloseHovered(true);
            update(popupCloseButtonRect().toAlignedRect());
            event->accept();
            return;
        }

        // Mode switcher hit test
        if (m_embeddedMode) {
            int modeIdx = hitTestModeSwitcher(event->pos());
            if (modeIdx >= 0) {
                setPickerMode(static_cast<PickerMode>(modeIdx));
                event->accept();
                return;
            }
        }

        const int recentIdx = hitTestRecentColor(event->pos());
        if (recentIdx >= 0) {
            const QColor color = m_recentColors[recentIdx];
            moveRecentColorToTop(recentIdx);
            setColor(color);
            emit colorChanged(color);
            event->accept();
            return;
        }
        m_dragMode = hitTest(event->pos());
        if (m_dragMode != DragMode::None) {
            handleDrag(event->pos());
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void ColorPicker::mouseMoveEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (m_dragMode != DragMode::None) {
        if (shouldApplyDragUpdate(false)) {
            handleDrag(event->pos());
        }
        event->accept();
        return;
    }
    const bool closeHovered = hitTestPopupClose(event->pos());
    setPopupCloseHovered(closeHovered);

    const int hoveredRecent = hitTestRecentColor(event->pos());
    if (hoveredRecent != m_hoveredRecentIndex) {
        m_hoveredRecentIndex = hoveredRecent;
        if (m_recentHoverAnimation) {
            m_recentHoverAnimation->stop();
            m_recentHoverAnimation->setStartValue(m_recentHoverProgress);
            m_recentHoverAnimation->setEndValue(hoveredRecent >= 0 ? 1.0 : 0.0);
            m_recentHoverAnimation->start();
        }
        update();
    }
    if (closeHovered || hoveredRecent >= 0 || hitTestModeSwitcher(event->pos()) >= 0) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
    QWidget::mouseMoveEvent(event);
}

void ColorPicker::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_tabletDragActive) {
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_closePressed) {
        const bool closeAccepted = hitTestPopupClose(event->pos());
        m_closePressed = false;
        setPopupCloseHovered(closeAccepted);
        update(popupCloseButtonRect().toAlignedRect());
        event->accept();
        if (closeAccepted) {
            emit canceled();
        }
        return;
    }

    if (event->button() == Qt::LeftButton && m_dragMode != DragMode::None) {
        handleDrag(event->pos());
        m_dragMode = DragMode::None;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ColorPicker::leaveEvent(QEvent* event)
{
    unsetCursor();
    if (m_closeHovered || m_closePressed) {
        m_closePressed = false;
        setPopupCloseHovered(false);
    }
    if (m_hoveredRecentIndex != -1) {
        m_hoveredRecentIndex = -1;
        if (m_recentHoverAnimation) {
            m_recentHoverAnimation->stop();
            m_recentHoverAnimation->setStartValue(m_recentHoverProgress);
            m_recentHoverAnimation->setEndValue(0.0);
            m_recentHoverAnimation->start();
        }
        update();
    }
    QWidget::leaveEvent(event);
}

// ============================================================================
// Drawing
// ============================================================================

void ColorPicker::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_showProgress <= 0.001)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setOpacity(m_showProgress);

    drawBackground(painter);

    // Mode switcher (embedded only) — drawn outside the slide area
    if (m_embeddedMode) {
        drawModeSwitcher(painter);
    }

    const bool sliding = m_slideProgress < 1.0 && !m_slideSnapshot.isNull() && m_embeddedMode;

    if (sliding) {
        // Slide the whole picker (including hex input) below the mode switcher row.
        const qreal modeH = modeSwitcherTotalHeight();
        const QRectF slideClip(0, modeH, width(), qMax(1.0, height() - modeH));

        painter.save();
        painter.setClipRect(slideClip);

        const qreal w = width();
        const qreal oldX = -m_slideDirection * m_slideProgress * w;
        const qreal newX = m_slideDirection * (1.0 - m_slideProgress) * w;

        // Old snapshot sliding out
        painter.drawPixmap(QPointF(oldX, 0), m_slideSnapshot);
        // New snapshot sliding in (snapshot already includes child widgets at new positions)
        if (!m_slideNewSnapshot.isNull()) {
            painter.drawPixmap(QPointF(newX, 0), m_slideNewSnapshot);
        } else {
            painter.save();
            painter.translate(newX, 0);
            drawPickerContent(painter);
            painter.restore();
        }

        painter.restore();

    } else {
        drawPickerContent(painter);
    }
}

void ColorPicker::drawPickerContent(QPainter& painter)
{
    if (!m_embeddedMode) {
        drawPopupHeader(painter);
    }

    if (m_pickerMode == PickerMode::Classic || !m_embeddedMode) {
        drawSVSquare(painter, svSquareRect());
        drawHueBar(painter, hueBarRect());
        drawSVCursor(painter, svSquareRect());
        drawHueCursor(painter, hueBarRect());
        if (m_alphaSliderEnabled && !m_embeddedMode) {
            const QRectF alphaRect = alphaBarRect();
            drawAlphaBar(painter, alphaRect);
            drawAlphaCursor(painter, alphaRect);
        }
    } else if (m_pickerMode == PickerMode::Triangle) {
        drawHueRing(painter);
        drawColorTriangle(painter);
        drawTriangleSVCursor(painter);
        drawRingHueCursor(painter);
    } else if (m_pickerMode == PickerMode::Square) {
        drawHueRing(painter);
        drawSquareInRing(painter);
        drawSquareInRingSVCursor(painter);
        drawRingHueCursor(painter);
    }

    if (m_recentColorsEnabled) {
        drawRecentColors(painter);
    }
}

void ColorPicker::drawPopupHeader(QPainter& painter)
{
    const QRectF header = popupHeaderRect();
    if (header.isEmpty()) {
        return;
    }

    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();
    const auto& colors = mgr.colors();
    const qreal swatchSize = theme.scaled(PopupHeaderSwatchSize);
    const qreal spacing = theme.scaled(PopupHeaderSpacing);
    const QRectF swatchRect(
        header.left(), header.center().y() - swatchSize * 0.5, swatchSize, swatchSize);
    const QRectF closeRect = popupCloseButtonRect();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal swatchRadius = theme.scaled(4);

    if (m_currentColor.alpha() < 255) {
        const int checkSize = qMax(2, theme.scaled(3));
        QPainterPath swatchClip;
        swatchClip.addRoundedRect(swatchRect, swatchRadius, swatchRadius);
        painter.save();
        painter.setClipPath(swatchClip);
        for (int y = static_cast<int>(swatchRect.top()); y < static_cast<int>(swatchRect.bottom());
            y += checkSize) {
            for (int x = static_cast<int>(swatchRect.left());
                x < static_cast<int>(swatchRect.right()); x += checkSize) {
                const bool light = ((x / checkSize) + (y / checkSize)) % 2 == 0;
                painter.fillRect(x, y, checkSize, checkSize,
                    light ? QColor(200, 200, 200) : QColor(100, 100, 100));
            }
        }
        painter.restore();
    }

    // Fill and border share a single rounded-rect path so the corners keep one
    // clean antialiased edge instead of two overlapping ones (which smeared).
    const QRectF outlineRect = swatchRect.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal outlineRadius = swatchRadius - 0.5;
    painter.setPen(QPen(colors.border, 1.0));
    painter.setBrush(m_currentColor);
    painter.drawRoundedRect(outlineRect, outlineRadius, outlineRadius);

    const QString title = m_popupTitle.trimmed().isEmpty() ? tr("Color") : m_popupTitle.trimmed();
    QRectF textRect(swatchRect.right() + spacing, header.top(),
        qMax<qreal>(1.0, closeRect.left() - spacing - (swatchRect.right() + spacing)),
        header.height());

    QFont titleFont = painter.font();
    titleFont.setBold(true);
    titleFont.setPixelSize(qMax(8, theme.scaled(10)));
    painter.setFont(titleFont);
    painter.setPen(colors.text);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(titleFont).elidedText(title, Qt::ElideRight, qRound(textRect.width())));

    const qreal closeRadius = theme.scaled(4);
    const qreal closeHover = qBound(0.0, m_closePressed ? 1.0 : m_closeHoverProgress, 1.0);
    if (closeHover > 0.001) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.overlay((m_closePressed ? 0.12 : 0.08) * closeHover));
        painter.drawRoundedRect(closeRect, closeRadius, closeRadius);
    }

    QColor closeColor;
    closeColor.setRedF(
        colors.textMuted.redF() + (colors.text.redF() - colors.textMuted.redF()) * closeHover);
    closeColor.setGreenF(colors.textMuted.greenF()
        + (colors.text.greenF() - colors.textMuted.greenF()) * closeHover);
    closeColor.setBlueF(
        colors.textMuted.blueF() + (colors.text.blueF() - colors.textMuted.blueF()) * closeHover);
    closeColor.setAlphaF(colors.textMuted.alphaF()
        + (colors.text.alphaF() - colors.textMuted.alphaF()) * closeHover);
    const qreal iconInset = theme.scaled(6);
    QPen closePen(closeColor, 1.4);
    closePen.setCosmetic(true);
    closePen.setCapStyle(Qt::RoundCap);
    painter.setPen(closePen);
    painter.drawLine(QPointF(closeRect.left() + iconInset, closeRect.top() + iconInset),
        QPointF(closeRect.right() - iconInset, closeRect.bottom() - iconInset));
    painter.drawLine(QPointF(closeRect.right() - iconInset, closeRect.top() + iconInset),
        QPointF(closeRect.left() + iconInset, closeRect.bottom() - iconInset));

    painter.restore();
}

void ColorPicker::drawBackground(QPainter& painter)
{
    if (m_embeddedMode) {
        return;
    }
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    QColor bgColor = mgr.colors().surface;
    QColor borderColor = mgr.colors().border;
    const qreal radius = theme.scaled(CornerRadius);
    const int shadow = theme.scaled(PopupShadowMargin);
    const QRectF panelRect = popupPanelRect().adjusted(0.5, 0.5, -0.5, -0.5);

    const QColor shadowColor(0, 0, 0, mgr.colors().isDark ? 120 : 72);
    drawSoftShadow(painter, panelRect, radius, shadow, shadowColor);

    QPainterPath mainPath;
    mainPath.addRoundedRect(panelRect, radius, radius);

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawPath(mainPath);

    painter.setPen(QPen(borderColor, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(mainPath);
}

void ColorPicker::ensureSVSquareCache(const QSize& size)
{
    if (size.isEmpty())
        return;

    const qreal hueEps = 1e-6;
    if (m_svSquareCache.size() == size && qAbs(m_svSquareCacheHue - m_hue) < hueEps) {
        return;
    }

    m_svSquareCache = QImage(size, QImage::Format_RGB32);
    const int w = m_svSquareCache.width();
    const int h = m_svSquareCache.height();

    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(m_svSquareCache.scanLine(y));
        const qreal v = 1.0 - qreal(y) / h;
        for (int x = 0; x < w; ++x) {
            const qreal s = qreal(x) / w;
            line[x] = QColor::fromHsvF(m_hue, s, v).rgb();
        }
    }

    m_svSquareCacheHue = m_hue;
    m_svSquareCacheSize = size;
}

void ColorPicker::drawSVSquare(QPainter& painter, const QRectF& rect)
{
    auto& theme = ThemeManager::instance();
    const int cornerRadius = theme.scaled(m_embeddedMode ? 4 : ColorAreaCornerRadius);

    const QSize size = rect.size().toSize();
    ensureSVSquareCache(size);

    if (m_svSquareCache.isNull())
        return;

    painter.save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, cornerRadius, cornerRadius);
    painter.setClipPath(clipPath);
    painter.drawImage(rect, m_svSquareCache);
    painter.restore();

    if (m_embeddedMode) {
        auto& mgr = WidgetStyleManager::instance();
        painter.setPen(QPen(mgr.colors().border, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect, cornerRadius, cornerRadius);
    }
}

void ColorPicker::drawHueBar(QPainter& painter, const QRectF& rect)
{
    auto& theme = ThemeManager::instance();
    const qreal cornerRadius = m_embeddedMode ? theme.scaled(4) : rect.height() * 0.5;

    QLinearGradient gradient(rect.topLeft(), m_embeddedMode ? rect.bottomLeft() : rect.topRight());
    for (int i = 0; i <= 6; ++i) {
        qreal hue = qreal(i) / 6.0;
        gradient.setColorAt(hue, QColor::fromHsvF(hue, 1.0, 1.0));
    }

    painter.save();
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, cornerRadius, cornerRadius);
    painter.setClipPath(clipPath);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRect(rect);
    painter.restore();

    // Border
    auto& mgr = WidgetStyleManager::instance();
    painter.setPen(QPen(mgr.colors().border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, cornerRadius, cornerRadius);
}

void ColorPicker::drawAlphaBar(QPainter& painter, const QRectF& rect)
{
    if (rect.isEmpty()) {
        return;
    }
    auto& mgr = WidgetStyleManager::instance();
    const qreal cornerRadius = rect.height() * 0.5;

    QPainterPath clipPath;
    clipPath.addRoundedRect(rect, cornerRadius, cornerRadius);

    painter.save();
    painter.setClipPath(clipPath);

    // Checkerboard so the transparent end reads clearly as "no opacity".
    const int checkSize = qMax(2, ThemeManager::instance().scaled(4));
    for (int y = static_cast<int>(rect.top()); y < static_cast<int>(rect.bottom());
        y += checkSize) {
        for (int x = static_cast<int>(rect.left()); x < static_cast<int>(rect.right());
            x += checkSize) {
            const bool light = ((x / checkSize) + (y / checkSize)) % 2 == 0;
            painter.fillRect(
                x, y, checkSize, checkSize, light ? QColor(200, 200, 200) : QColor(100, 100, 100));
        }
    }

    // Transparent → opaque gradient of the current RGB.
    QColor opaque = m_currentColor;
    opaque.setAlpha(255);
    QColor clear = opaque;
    clear.setAlpha(0);
    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    gradient.setColorAt(0.0, clear);
    gradient.setColorAt(1.0, opaque);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRect(rect);
    painter.restore();

    painter.setPen(QPen(mgr.colors().border, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(rect, cornerRadius, cornerRadius);
}

void ColorPicker::drawSVCursor(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();
    QColor primary = mgr.colors().primary;

    const qreal x = rect.left() + m_saturation * rect.width();
    const qreal y = rect.top() + (1.0 - m_value) * rect.height();
    const qreal radius = theme.scaled(SVCursorRadius);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (m_embeddedMode) {
        painter.setPen(Qt::NoPen);
    } else {
        painter.setPen(QPen(mgr.colors().surface, theme.scaled(2)));
    }
    painter.setBrush(primary);
    painter.drawEllipse(QPointF(x, y), radius, radius);
    painter.restore();
}

void ColorPicker::drawHueCursor(QPainter& painter, const QRectF& rect)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();
    QColor primary = mgr.colors().primary;

    QRectF r;
    if (m_embeddedMode) {
        const qreal y = rect.top() + m_hue * rect.height();
        const qreal h = theme.scaled(HueCursorWidth);
        const qreal w = rect.width() + theme.scaled(HueCursorOverhang);
        r = QRectF(rect.center().x() - w * 0.5, y - h * 0.5, w, h);
    } else {
        const qreal x = rect.left() + m_hue * rect.width();
        const qreal d = theme.scaled(HueCursorDiameter);
        r = QRectF(x - d * 0.5, rect.center().y() - d * 0.5, d, d);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(primary);
    if (m_embeddedMode) {
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(r, theme.scaled(2), theme.scaled(2));
    } else {
        painter.setPen(QPen(mgr.colors().surface, theme.scaled(2)));
        painter.drawEllipse(r);
    }
    painter.restore();
}

void ColorPicker::drawAlphaCursor(QPainter& painter, const QRectF& rect)
{
    if (rect.isEmpty()) {
        return;
    }
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    const qreal x = rect.left() + m_alpha * rect.width();
    const qreal d = theme.scaled(HueCursorDiameter);
    const QRectF r(x - d * 0.5, rect.center().y() - d * 0.5, d, d);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(mgr.colors().primary);
    painter.setPen(QPen(mgr.colors().surface, theme.scaled(2)));
    painter.drawEllipse(r);
    painter.restore();
}

void ColorPicker::drawRecentColors(QPainter& painter)
{
    auto& theme = ThemeManager::instance();
    const int cornerRadius = theme.scaled(2);
    const int targetCount = targetRecentColorsVisibleCount();
    const int previousCount = qMin(m_recentPreviousVisibleCount, m_recentColors.size());
    const int drawCount = qMin(m_recentColors.size(), qMax(previousCount, targetCount));

    for (int i = 0; i < drawCount; ++i) {
        QRectF r = recentColorSquareBaseRect(i);
        if (r.isEmpty())
            continue;

        const qreal visibility = recentColorVisibility(i);
        if (visibility <= 0.001)
            continue;

        if (visibility < 0.999) {
            const qreal scale = 0.55 + 0.45 * visibility;
            const QPointF center = r.center();
            r.setWidth(r.width() * scale);
            r.setHeight(r.height() * scale);
            r.moveCenter(center);
        }

        const QColor& c = m_recentColors[i];
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(painter.opacity() * visibility);
        QColor drawColor = c;
        if (drawColor.alpha() < 255) {
            drawColor.setAlpha(255);
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(drawColor);
        painter.drawRoundedRect(r, cornerRadius, cornerRadius);
        if (i == m_hoveredRecentIndex && m_recentHoverProgress > 0.001) {
            const int alpha = qBound(0, qRound(90 * m_recentHoverProgress), 255);
            painter.setPen(QPen(QColor(255, 255, 255, alpha), 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), cornerRadius, cornerRadius);
        }

        if (c.alpha() < 255) {
            const int opacityPct = qBound(0, qRound(c.alpha() / 255.0 * 100), 99);
            const QString text = QString::number(opacityPct);
            QFont f = painter.font();
            f.setPixelSize(qMax(6, theme.scaled(8)));
            painter.setFont(f);
            const qreal lum
                = 0.299 * drawColor.red() + 0.587 * drawColor.green() + 0.114 * drawColor.blue();
            painter.setPen(lum < 128 ? Qt::white : Qt::black);
            painter.setBrush(Qt::NoBrush);
            painter.drawText(r, Qt::AlignCenter, text);
        }

        painter.restore();
    }
}

} // namespace ruwa::ui::widgets
