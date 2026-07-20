// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerCropOverlay.cpp
#include "WelcomeBannerCropOverlay.h"
#include "WelcomeBanner.h"
#include "WelcomeBannerButton.h"
#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/style/PaintingUtils.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

namespace ruwa::ui::widgets {

namespace {
constexpr int DimAnimationDuration = 200;
constexpr int CardAnimationDuration = 260;
constexpr int SlideOffset = 24;
constexpr qreal MaxDimOpacity = 0.55;

constexpr int CardRadius = 12;
constexpr int CardMargin = 22;
constexpr int CardSpacing = 16;
constexpr int TitleFontSize = 13;
constexpr int HintFontSize = 9;
constexpr int ButtonSpacing = 12;

// Used only when no live WelcomeBanner can be measured (settings opened standalone).
// Representative widescreen banner: ~1100 wide over the fixed ~320 height.
constexpr double kFallbackBannerAspect = 1100.0 / 320.0;

constexpr int BaseHandleSize = 12; // visual handle square (scaled)
constexpr int BaseHandleHit = 18; // hit radius for grabbing a corner (scaled)
constexpr int BaseMinCropPx = 48; // minimum crop edge in display pixels (scaled)
} // namespace

// ============================================================================
// CropAreaWidget — draws the image and an aspect-locked, draggable crop rect.
// Plain QWidget (no signals); the overlay reads normalizedCrop() on confirm.
// ============================================================================
class CropAreaWidget : public QWidget {
public:
    explicit CropAreaWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        setCursor(Qt::ArrowCursor);
    }

    void setImage(const QPixmap& image)
    {
        m_image = image;
        initCrop();
        update();
    }

    void setAspect(double aspect)
    {
        m_aspect = (aspect > 0.01) ? aspect : 1.0;
        initCrop();
        update();
    }

    /// Crop rect normalized to [0,1] of the source image.
    QRectF normalizedCrop() const { return m_cropNorm; }

    /// Seed the selection from a stored crop. Ignored if invalid or full-image.
    void setNormalizedCrop(const QRectF& n)
    {
        if (!n.isValid() || n.isEmpty()) {
            return;
        }
        // A full-image rect means "no crop" — keep the computed centered default.
        if (qFuzzyIsNull(n.x()) && qFuzzyIsNull(n.y()) && qFuzzyCompare(n.width(), 1.0)
            && qFuzzyCompare(n.height(), 1.0)) {
            return;
        }
        QRectF r = n.intersected(QRectF(0, 0, 1, 1));
        if (r.width() <= 0 || r.height() <= 0) {
            return;
        }
        m_cropNorm = r;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_image.isNull()) {
            return;
        }
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRectF fit = imageDisplayRect();
        const int imgRadius = ruwa::ui::core::ThemeManager::instance().scaled(6);

        // Image (rounded), letterboxed inside the area.
        painter.save();
        QPainterPath imgClip;
        imgClip.addRoundedRect(fit, imgRadius, imgRadius);
        painter.setClipPath(imgClip);
        painter.drawPixmap(fit, m_image, QRectF(m_image.rect()));
        painter.restore();

        const QRectF crop = cropDisplayRect();

        // Dim everything outside the crop (within the image rect only).
        painter.save();
        painter.setClipPath(imgClip);
        QPainterPath outside;
        outside.addRect(fit);
        QPainterPath inside;
        inside.addRect(crop);
        painter.fillPath(outside.subtracted(inside), QColor(0, 0, 0, 130));
        painter.restore();

        // Rule-of-thirds grid inside the crop.
        QPen gridPen(QColor(255, 255, 255, 60), 1.0);
        painter.setPen(gridPen);
        for (int i = 1; i <= 2; ++i) {
            const qreal x = crop.left() + crop.width() * i / 3.0;
            const qreal y = crop.top() + crop.height() * i / 3.0;
            painter.drawLine(QPointF(x, crop.top()), QPointF(x, crop.bottom()));
            painter.drawLine(QPointF(crop.left(), y), QPointF(crop.right(), y));
        }

        // Crop border.
        QPen borderPen(colors.primary, 1.6);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(crop);

        // Ghost banner buttons so the user sees the action buttons may cover the image.
        drawButtonGhosts(painter, crop);

        // Corner handles.
        const qreal hs = ruwa::ui::core::ThemeManager::instance().scaled(BaseHandleSize);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.primary);
        for (const QPointF& c : cornerPoints(crop)) {
            QRectF h(c.x() - hs / 2.0, c.y() - hs / 2.0, hs, hs);
            painter.drawRoundedRect(h, hs * 0.25, hs * 0.25);
        }
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || m_image.isNull()) {
            QWidget::mousePressEvent(event);
            return;
        }
        const QPointF pos = event->position();
        m_drag = hitTest(pos);
        m_pressPos = pos;
        m_pressCrop = cropDisplayRect();
        if (m_drag != Drag::None) {
            event->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_image.isNull()) {
            return;
        }
        const QPointF pos = event->position();

        if (m_drag == Drag::None) {
            // Hover feedback only.
            updateCursor(hitTest(pos));
            return;
        }

        const QRectF fit = imageDisplayRect();
        QRectF crop = m_pressCrop;

        if (m_drag == Drag::Move) {
            const QPointF delta = pos - m_pressPos;
            crop.translate(delta);
            // Clamp inside the image rect.
            qreal nx = qBound(fit.left(), crop.left(), fit.right() - crop.width());
            qreal ny = qBound(fit.top(), crop.top(), fit.bottom() - crop.height());
            crop.moveTo(nx, ny);
        } else {
            crop = resizeFromCorner(m_drag, pos, fit);
        }

        setCropFromDisplay(crop, fit);
        update();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        m_drag = Drag::None;
        updateCursor(hitTest(event->position()));
        QWidget::mouseReleaseEvent(event);
    }

private:
    enum class Drag { None, Move, TL, TR, BL, BR };

    QRectF imageDisplayRect() const
    {
        const QRectF area = QRectF(rect());
        if (m_image.isNull() || area.isEmpty()) {
            return area;
        }
        const qreal imgAspect = qreal(m_image.width()) / qreal(m_image.height());
        const qreal areaAspect = area.width() / area.height();
        QSizeF s;
        if (imgAspect >= areaAspect) {
            s = QSizeF(area.width(), area.width() / imgAspect);
        } else {
            s = QSizeF(area.height() * imgAspect, area.height());
        }
        const QPointF tl(area.left() + (area.width() - s.width()) / 2.0,
            area.top() + (area.height() - s.height()) / 2.0);
        return QRectF(tl, s);
    }

    QRectF cropDisplayRect() const
    {
        const QRectF fit = imageDisplayRect();
        return QRectF(fit.left() + m_cropNorm.left() * fit.width(),
            fit.top() + m_cropNorm.top() * fit.height(), m_cropNorm.width() * fit.width(),
            m_cropNorm.height() * fit.height());
    }

    void setCropFromDisplay(const QRectF& crop, const QRectF& fit)
    {
        if (fit.width() <= 0 || fit.height() <= 0) {
            return;
        }
        QRectF n((crop.left() - fit.left()) / fit.width(), (crop.top() - fit.top()) / fit.height(),
            crop.width() / fit.width(), crop.height() / fit.height());
        // Guard against drift outside [0,1].
        n.setLeft(qBound(0.0, n.left(), 1.0));
        n.setTop(qBound(0.0, n.top(), 1.0));
        if (n.right() > 1.0)
            n.moveRight(1.0);
        if (n.bottom() > 1.0)
            n.moveBottom(1.0);
        m_cropNorm = n;
    }

    static QVector<QPointF> cornerPoints(const QRectF& r)
    {
        return { r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight() };
    }

    Drag hitTest(const QPointF& pos) const
    {
        const QRectF crop = cropDisplayRect();
        const qreal hit = ruwa::ui::core::ThemeManager::instance().scaled(BaseHandleHit);
        const QPointF corners[4]
            = { crop.topLeft(), crop.topRight(), crop.bottomLeft(), crop.bottomRight() };
        const Drag kinds[4] = { Drag::TL, Drag::TR, Drag::BL, Drag::BR };
        for (int i = 0; i < 4; ++i) {
            if (QLineF(pos, corners[i]).length() <= hit) {
                return kinds[i];
            }
        }
        if (crop.contains(pos)) {
            return Drag::Move;
        }
        return Drag::None;
    }

    void updateCursor(Drag d)
    {
        switch (d) {
        case Drag::TL:
        case Drag::BR:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Drag::TR:
        case Drag::BL:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Drag::Move:
            setCursor(Qt::SizeAllCursor);
            break;
        default:
            setCursor(Qt::ArrowCursor);
            break;
        }
    }

    // Resize keeping the opposite corner anchored and width/height = m_aspect.
    QRectF resizeFromCorner(Drag corner, const QPointF& mouse, const QRectF& fit) const
    {
        QPointF anchor;
        int sx = 1; // +1 means the dragged corner is to the right of the anchor
        int sy = 1; // +1 means below the anchor
        const QRectF c = m_pressCrop;
        switch (corner) {
        case Drag::BR:
            anchor = c.topLeft();
            sx = 1;
            sy = 1;
            break;
        case Drag::TR:
            anchor = c.bottomLeft();
            sx = 1;
            sy = -1;
            break;
        case Drag::BL:
            anchor = c.topRight();
            sx = -1;
            sy = 1;
            break;
        case Drag::TL:
            anchor = c.bottomRight();
            sx = -1;
            sy = -1;
            break;
        default:
            return c;
        }

        const qreal minW = ruwa::ui::core::ThemeManager::instance().scaled(BaseMinCropPx);
        const qreal maxW = (sx > 0) ? (fit.right() - anchor.x()) : (anchor.x() - fit.left());
        const qreal maxH = (sy > 0) ? (fit.bottom() - anchor.y()) : (anchor.y() - fit.top());

        qreal w = qAbs(mouse.x() - anchor.x());
        w = qBound(minW, w, qMax(minW, maxW));
        qreal h = w / m_aspect;
        if (h > maxH) {
            h = qMax(minW / m_aspect, maxH);
            w = h * m_aspect;
        }

        const qreal x = (sx > 0) ? anchor.x() : anchor.x() - w;
        const qreal y = (sy > 0) ? anchor.y() : anchor.y() - h;
        return QRectF(x, y, w, h);
    }

    // Largest aspect-correct rect centered on the image.
    void initCrop()
    {
        if (m_image.isNull()) {
            m_cropNorm = QRectF(0, 0, 1, 1);
            return;
        }
        const qreal imgAspect = qreal(m_image.width()) / qreal(m_image.height());
        qreal w, h; // normalized
        if (imgAspect >= m_aspect) {
            h = 1.0;
            w = m_aspect / imgAspect;
        } else {
            w = 1.0;
            h = imgAspect / m_aspect;
        }
        m_cropNorm = QRectF((1.0 - w) / 2.0, (1.0 - h) / 2.0, w, h);
    }

    // Draw the two welcome-banner action buttons exactly where they sit on the real
    // banner (bottom-left, inside the content margin): a filled accent button and an
    // outlined+dimmed button. Sizes are proportional to the banner height (320 base).
    void drawButtonGhosts(QPainter& painter, const QRectF& crop)
    {
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

        // Ratios mirror WelcomeBanner + CapsuleButton base metrics over the 320px banner height.
        const qreal h = crop.height() * (48.0 / 320.0); // button height
        if (h < 8.0) {
            return; // crop too small to show meaningful ghosts
        }
        const qreal margin = crop.height() * (40.0 / 320.0); // banner content margin
        const qreal gap = crop.height() * (12.0 / 320.0); // spacing between buttons
        const qreal radius = h / 2.0;

        // No text (it's just visual noise at small sizes): representative capsule widths.
        const qreal primaryW = h * 3.4;
        const qreal secondaryW = h * 3.0;

        const qreal y = crop.bottom() - margin - h;
        const qreal x0 = crop.left() + margin;
        const QRectF primaryRect(x0, y, primaryW, h);
        const QRectF secondaryRect(primaryRect.right() + gap, y, secondaryW, h);

        painter.save();
        QPainterPath clip;
        clip.addRect(crop);
        painter.setClipPath(clip);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(0.92);

        // Primary: accent fill (matches CapsuleButton::Variant::Primary).
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.primary);
        painter.drawRoundedRect(primaryRect, radius, radius);

        // Secondary: dim plate + light outline (matches the banner's blurred-tinted secondary).
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 90));
        painter.drawRoundedRect(secondaryRect, radius, radius);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(255, 255, 255, 150), qMax(1.0, h * 0.045)));
        painter.drawRoundedRect(secondaryRect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

        painter.restore();
    }

    QPixmap m_image;
    double m_aspect { 1.0 };
    QRectF m_cropNorm { 0, 0, 1, 1 };

    Drag m_drag { Drag::None };
    QPointF m_pressPos;
    QRectF m_pressCrop;
};

namespace {
/// Card background: app-style toned glass panel + gradient border.
class CropCardFrame : public QWidget {
public:
    explicit CropCardFrame(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();
        const int radius = theme.scaled(CardRadius);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        ruwa::ui::painting::drawTonedGlassPanel(painter, QRectF(rect()), radius, QSizeF(size()),
            QPixmap(), colors.surface, colors.primary, colors.isDark, colors.borderSubtleHover(),
            ruwa::ui::core::ThemeColors::withAlpha(
                colors.borderSubtle(), colors.borderSubtle().alpha() / 2));
    }
};
} // namespace

// ============================================================================
// WelcomeBannerCropOverlay
// ============================================================================
WelcomeBannerCropOverlay::WelcomeBannerCropOverlay(const QString& imagePath, QWidget* parent)
    : QWidget(parent)
    , m_imagePath(imagePath)
    , m_image(imagePath)
{
    setupUI();

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
    }
    syncOverlayGeometry();

    QWidget::hide();
}

WelcomeBannerCropOverlay::~WelcomeBannerCropOverlay()
{
    if (parentWidget()) {
        parentWidget()->removeEventFilter(this);
    }
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void WelcomeBannerCropOverlay::setupUI()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    m_card = new CropCardFrame(this);
    m_cardOpacityEffect = new QGraphicsOpacityEffect(m_card);
    m_cardOpacityEffect->setOpacity(0.0);
    m_card->setGraphicsEffect(m_cardOpacityEffect);

    auto* layout = new QVBoxLayout(m_card);
    const int margin = theme.scaled(CardMargin);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(theme.scaled(CardSpacing));

    auto* title = new QLabel(
        tr("Choose the area to show on the banner"), m_card);
    QFont titleFont = colors.fonts.getUIFont(theme.scaledFontSize(TitleFontSize));
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    title->setStyleSheet(
        QStringLiteral("QLabel { background: transparent; color: %1; }").arg(colors.text.name()));
    layout->addWidget(title);

    auto* hint = new QLabel(
        tr("Drag to move, drag the corners to resize. The frame keeps the banner's shape."),
        m_card);
    hint->setWordWrap(true);
    hint->setFont(colors.fonts.getUIFont(theme.scaledFontSize(HintFontSize)));
    hint->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.textMuted.name()));
    layout->addWidget(hint);

    m_cropArea = new CropAreaWidget(m_card);
    m_cropArea->setAspect(resolveBannerAspect());
    if (!m_image.isNull()) {
        m_cropArea->setImage(m_image);
    }
    layout->addWidget(m_cropArea, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->setSpacing(theme.scaled(ButtonSpacing));
    buttonRow->addStretch();

    auto* cancelButton = new WelcomeBannerButton(tr("Cancel"),
        WelcomeBannerButton::ButtonStyle::Secondary, m_card);
    connect(cancelButton, &WelcomeBannerButton::clicked, this,
        [this]() { animateOutThen(false, QRectF()); });
    buttonRow->addWidget(cancelButton);

    auto* confirmButton = new WelcomeBannerButton(tr("Use this area"),
        WelcomeBannerButton::ButtonStyle::Primary, m_card);
    connect(confirmButton, &WelcomeBannerButton::clicked, this,
        [this]() { animateOutThen(true, m_cropArea ? m_cropArea->normalizedCrop() : QRectF()); });
    buttonRow->addWidget(confirmButton);

    layout->addLayout(buttonRow);

    m_dimAnimation = new QPropertyAnimation(this, "dimProgress", this);
    m_dimAnimation->setDuration(DimAnimationDuration);

    m_cardOpacityAnim = new QPropertyAnimation(m_cardOpacityEffect, "opacity", this);
    m_cardOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_cardOpacityAnim->setDuration(CardAnimationDuration);

    m_cardPosAnim = new QPropertyAnimation(m_card, "pos", this);
    m_cardPosAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_cardPosAnim->setDuration(CardAnimationDuration);
}

void WelcomeBannerCropOverlay::setInitialCrop(const QRectF& normalizedCrop)
{
    if (m_cropArea) {
        m_cropArea->setNormalizedCrop(normalizedCrop);
    }
}

double WelcomeBannerCropOverlay::resolveBannerAspect() const
{
    if (QWidget* top = window()) {
        if (auto* banner = top->findChild<WelcomeBanner*>()) {
            const QSize s = banner->size();
            if (s.width() > 0 && s.height() > 0) {
                return qreal(s.width()) / qreal(s.height());
            }
        }
    }
    return kFallbackBannerAspect;
}

void WelcomeBannerCropOverlay::showOverlay()
{
    if (m_image.isNull()) {
        return;
    }

    syncOverlayGeometry();

    QWidget::show();
    raise();
    setFocus();
    m_card->raise();

    if (!m_shortcutsBlocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
        m_shortcutsBlocked = true;
    }

    animateIn();
}

void WelcomeBannerCropOverlay::animateIn()
{
    const QPoint target = cardTargetPosition();
    const QPoint start = target + QPoint(0, SlideOffset);
    m_card->move(start);
    m_cardOpacityEffect->setOpacity(0.0);

    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(1.0);
    m_dimAnimation->start();

    m_cardOpacityAnim->stop();
    m_cardOpacityAnim->setStartValue(0.0);
    m_cardOpacityAnim->setEndValue(1.0);
    m_cardOpacityAnim->start();

    m_cardPosAnim->stop();
    m_cardPosAnim->setStartValue(start);
    m_cardPosAnim->setEndValue(target);
    m_cardPosAnim->start();
}

void WelcomeBannerCropOverlay::animateOutThen(bool confirmed, const QRectF& norm)
{
    if (m_closing) {
        return;
    }
    m_closing = true;

    disconnect(m_cardOpacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_cardOpacityAnim, &QPropertyAnimation::finished, this, [this, confirmed, norm]() {
        if (m_shortcutsBlocked) {
            m_shortcutsBlocked = false;
            ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
        }
        QWidget::hide();
        if (confirmed) {
            emit cropConfirmed(norm);
        } else {
            emit cancelled();
        }
    });

    const QPoint current = m_card->pos();
    const QPoint end = current + QPoint(0, SlideOffset);

    m_dimAnimation->stop();
    m_dimAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(0.0);
    m_dimAnimation->start();

    m_cardOpacityAnim->stop();
    m_cardOpacityAnim->setStartValue(m_cardOpacityEffect->opacity());
    m_cardOpacityAnim->setEndValue(0.0);
    m_cardOpacityAnim->start();

    m_cardPosAnim->stop();
    m_cardPosAnim->setStartValue(current);
    m_cardPosAnim->setEndValue(end);
    m_cardPosAnim->start();
}

void WelcomeBannerCropOverlay::setDimProgress(qreal progress)
{
    if (qFuzzyCompare(m_dimProgress, progress)) {
        return;
    }
    m_dimProgress = progress;
    update();
}

QPoint WelcomeBannerCropOverlay::cardTargetPosition() const
{
    if (!m_card) {
        return QPoint();
    }
    const int x = (width() - m_card->width()) / 2;
    const int y = (height() - m_card->height()) / 2;
    return QPoint(qMax(0, x), qMax(0, y));
}

void WelcomeBannerCropOverlay::updateCardPosition()
{
    if (!m_card) {
        return;
    }
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const int w = qBound(theme.scaled(360), int(width() * 0.82), theme.scaled(900));
    const int h = qBound(theme.scaled(320), int(height() * 0.82), theme.scaled(640));
    m_card->setFixedSize(qMin(w, width()), qMin(h, height()));
    if (!m_closing) {
        m_card->move(cardTargetPosition());
    }
}

void WelcomeBannerCropOverlay::syncOverlayGeometry()
{
    if (QWidget* parent = parentWidget()) {
        setGeometry(parent->rect());
    }
    updateCardPosition();
}

void WelcomeBannerCropOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateCardPosition();
}

void WelcomeBannerCropOverlay::paintEvent(QPaintEvent*)
{
    if (m_dimProgress <= 0.001) {
        return;
    }
    QPainter painter(this);
    const int alpha = int(MaxDimOpacity * 255 * m_dimProgress);
    painter.fillRect(rect(), QColor(0, 0, 0, alpha));
}

void WelcomeBannerCropOverlay::mousePressEvent(QMouseEvent* event)
{
    if (m_card && !m_card->geometry().contains(event->pos())) {
        animateOutThen(false, QRectF());
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WelcomeBannerCropOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        animateOutThen(false, QRectF());
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool WelcomeBannerCropOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::LayoutRequest:
            syncOverlayGeometry();
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace ruwa::ui::widgets
