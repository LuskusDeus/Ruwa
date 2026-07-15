// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   B R U S H   P A C K   L I S T   S E C T I O N
// ==========================================================================

#include "features/brush/ui/BrushPackListSection.h"

#include "features/brush/manager/BrushPreviewManager.h"
#include "features/settings/SettingsManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/DisplayColorPalette.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/layout/AnimatedFlowWidget.h"
#include "shell/context-menu/IContextMenuProvider.h"

#include <QLatin1String>
#include <QColor>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QVariantAnimation>
#include <QVariantList>
#include <QVBoxLayout>
#include <functional>

namespace ruwa::ui::workspace {

using namespace ruwa::ui::core;

namespace {

constexpr int kBrushButtonWidth = 108;
constexpr int kBrushButtonHeight = 36;
constexpr int kExpandAnimationMinMs = 170;
constexpr int kExpandAnimationMaxMs = 320;
constexpr qreal kExpandAnimationMsPerPixel = 0.85;
constexpr auto kKeySimpleColorActions = "simpleColorActions";
constexpr auto kKeyChecked = "checked";
constexpr auto kKeyColorRgba = "colorRgba";
constexpr auto kKeySeparatorBefore = "separatorBefore";

enum BrushRowContextAction : int {
    CtxOpenInEditor = 1,
    CtxBrushColorBase = 100,
};

bool isBrushColorAction(int actionId)
{
    return actionId >= CtxBrushColorBase
        && actionId < CtxBrushColorBase + static_cast<int>(displayColorPalette().size());
}

int brushColorIndexForAction(int actionId)
{
    return qBound(0, actionId - CtxBrushColorBase, maxDisplayColorIndex());
}

QString translatedBrushText(const QString& text)
{
    if (text.isEmpty()) {
        return text;
    }
    return QCoreApplication::translate("QObject", text.toUtf8().constData());
}

class PackHeaderButton final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit PackHeaderButton(QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(ThemeManager::instance().scaled(28));
        setHoverDuration(150);
        setActiveDuration(190);

        m_expandAnimation = new QVariantAnimation(this);
        m_expandAnimation->setDuration(220);
        m_expandAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_expandAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                m_expandProgress = value.toReal();
                update();
            });
    }

    void setTitle(const QString& title)
    {
        m_title = title;
        update();
    }

    void setExpanded(bool expanded, bool animated)
    {
        const qreal target = expanded ? 1.0 : 0.0;
        if (!animated) {
            m_expandAnimation->stop();
            m_expandProgress = target;
            update();
            return;
        }

        m_expandAnimation->setDuration(190);
        m_expandAnimation->setEasingCurve(QEasingCurve::InOutCubic);
        m_expandAnimation->stop();
        m_expandAnimation->setStartValue(m_expandProgress);
        m_expandAnimation->setEndValue(target);
        m_expandAnimation->start();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = WidgetStyleManager::instance().colors();
        const QRectF outerRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = ThemeManager::instance().scaled(7);

        QColor fillColor = ThemeColors::withAlpha(colors.surfaceAlt, 0);
        fillColor
            = ThemeColors::interpolate(fillColor, colors.surfaceHover(), hoverProgress() * 0.18);
        fillColor = ThemeColors::interpolate(fillColor, colors.primary, activeProgress() * 0.04);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillColor);
        painter.drawRoundedRect(outerRect, radius, radius);

        const int leftPadding = ThemeManager::instance().scaled(10);
        const int rightPadding = ThemeManager::instance().scaled(10);
        const int arrowSize = ThemeManager::instance().scaled(10);
        const int arrowAreaWidth = arrowSize + ThemeManager::instance().scaled(4);
        const int gapWidth = ThemeManager::instance().scaled(8);

        QFont titleFont = painter.font();
        titleFont.setPixelSize(ThemeManager::instance().scaled(11));
        titleFont.setWeight(activeProgress() > 0.5 ? QFont::Medium : QFont::Normal);
        painter.setFont(titleFont);

        const QString titleText = translatedBrushText(m_title);
        const int textWidth
            = qMin(painter.fontMetrics().horizontalAdvance(titleText), qMax(0, width() / 2));

        QRect textRect(leftPadding, 0,
            qMax(0, width() - leftPadding - rightPadding - arrowAreaWidth - gapWidth * 2),
            height());
        textRect.setWidth(qMin(textRect.width(), textWidth + ThemeManager::instance().scaled(6)));

        painter.setPen(ThemeColors::interpolate(colors.textMuted, colors.text,
            0.38 + activeProgress() * 0.34 + hoverProgress() * 0.18));
        painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
            painter.fontMetrics().elidedText(titleText, Qt::ElideRight, textRect.width()));

        const int lineStartX = textRect.right() + gapWidth;
        const int lineEndX = width() - rightPadding - arrowAreaWidth - gapWidth;
        if (lineEndX > lineStartX) {
            QColor lineColor = ThemeColors::interpolate(colors.borderSubtle(),
                colors.borderSubtleHover(), hoverProgress() * 0.45 + activeProgress() * 0.2);
            lineColor.setAlphaF(lineColor.alphaF() * (0.55 + hoverProgress() * 0.2));
            painter.setPen(QPen(lineColor, 1.0));
            painter.drawLine(
                QPointF(lineStartX, height() * 0.5), QPointF(lineEndX, height() * 0.5));
        }

        const QPointF arrowCenter(width() - rightPadding - arrowSize * 0.5, height() * 0.5);
        painter.save();
        painter.translate(arrowCenter);
        painter.rotate(90.0 * m_expandProgress);
        painter.translate(-arrowCenter);
        QPen arrowPen(ThemeColors::interpolate(colors.textMuted, colors.text,
                          0.28 + activeProgress() * 0.48 + hoverProgress() * 0.16),
            ThemeManager::instance().scaled(1.4));
        arrowPen.setCapStyle(Qt::RoundCap);
        arrowPen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(arrowPen);
        painter.drawLine(
            QPointF(arrowCenter.x() - arrowSize * 0.35, arrowCenter.y() - arrowSize * 0.35),
            QPointF(arrowCenter.x() + arrowSize * 0.05, arrowCenter.y()));
        painter.drawLine(QPointF(arrowCenter.x() + arrowSize * 0.05, arrowCenter.y()),
            QPointF(arrowCenter.x() - arrowSize * 0.35, arrowCenter.y() + arrowSize * 0.35));
        painter.restore();
    }

private:
    QString m_title;
    qreal m_expandProgress = 0.0;
    QVariantAnimation* m_expandAnimation = nullptr;
};

class PackBrushRowButton final : public ruwa::ui::widgets::BaseAnimatedButton,
                                 public ruwa::ui::widgets::IContextMenuProvider {
public:
    explicit PackBrushRowButton(const BrushListBrushData& brush, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
        , m_brush(brush)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(ThemeManager::instance().scaled(kBrushButtonWidth),
            ThemeManager::instance().scaled(kBrushButtonHeight));
        setHoverDuration(130);
        setActiveDuration(170);

        // Previews render asynchronously through the shared manager (background
        // thread + in-memory/disk cache), so scrolling a large pack never blocks
        // the GUI thread on brush-engine renders.
        m_previewSession = ruwa::core::brushes::BrushPreviewManager::instance().createSession(
            ruwa::core::brushes::BrushPreviewSession::Kind::Stroke, this);
        connect(m_previewSession, &ruwa::core::brushes::BrushPreviewSession::imageChanged, this,
            [this]() { update(); });
    }

    QString brushId() const { return m_brush.id; }

    void setOpenEditorCallback(std::function<void(const QString&)> callback)
    {
        m_openEditorCallback = std::move(callback);
    }

    void setBrushDisplayColorIndex(int colorIndex)
    {
        const int clamped = qBound(0, colorIndex, maxDisplayColorIndex());
        if (m_brush.displayColorIndex == clamped) {
            return;
        }

        m_brush.displayColorIndex = clamped;
        update();
    }

    void setBrushSettings(const ruwa::core::brushes::BrushSettingsData& settings)
    {
        m_brush.settings = settings;
        invalidatePreviewCache();
        update();
    }

    void setBrushName(const QString& name)
    {
        if (m_brush.name == name) {
            return;
        }
        // The name only affects the painted label, not the preview, so a
        // repaint is enough — no need to invalidate the preview cache.
        m_brush.name = name;
        update();
    }

    ruwa::ui::widgets::ContextMenuType contextMenuType() const override
    {
        return m_brush.id.isEmpty() ? ruwa::ui::widgets::ContextMenuType::None
                                    : ruwa::ui::widgets::ContextMenuType::SimpleActions;
    }

    QVariantMap contextMenuContext() const override
    {
        QVariantMap ctx;
        if (m_brush.id.isEmpty()) {
            return ctx;
        }

        QVariantList actions;
        QVariantMap openEditor;
        openEditor.insert(QStringLiteral("id"), CtxOpenInEditor);
        openEditor.insert(QStringLiteral("text"),
            QCoreApplication::translate("BrushPackListSection", "Open in editor"));
        openEditor.insert(QStringLiteral("danger"), false);
        openEditor.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Edit));
        actions.append(openEditor);
        ctx.insert(QStringLiteral("simpleActions"), QVariant::fromValue(actions));

        QVariantList colorActions;
        const auto& palette = displayColorPalette();
        for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
            QVariantMap colorAction;
            colorAction.insert(QStringLiteral("id"), CtxBrushColorBase + i);
            colorAction.insert(QLatin1String(kKeyChecked), m_brush.displayColorIndex == i);
            colorAction.insert(
                QLatin1String(kKeyColorRgba), palette[static_cast<size_t>(i)].rgba());
            if (i == 1) {
                colorAction.insert(QLatin1String(kKeySeparatorBefore), true);
            }
            colorActions.append(colorAction);
        }
        ctx.insert(QLatin1String(kKeySimpleColorActions), colorActions);

        return ctx;
    }

    void handleContextMenuAction(int actionId) override
    {
        if (actionId == CtxOpenInEditor) {
            if (m_openEditorCallback && !m_brush.id.isEmpty()) {
                m_openEditorCallback(m_brush.id);
            }
            return;
        }

        if (!isBrushColorAction(actionId) || m_brush.id.isEmpty()) {
            return;
        }

        const int colorIndex = brushColorIndexForAction(actionId);
        ruwa::core::SettingsManager::instance().setBrushDisplayColorIndex(m_brush.id, colorIndex);
        setBrushDisplayColorIndex(colorIndex);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        BaseAnimatedButton::resizeEvent(event);
        invalidatePreviewCache();
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = WidgetStyleManager::instance().colors();
        const QRectF rowRect = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = ThemeManager::instance().scaled(8);

        QColor fillColor = ThemeColors::withAlpha(colors.surfaceAlt, 0);
        fillColor
            = ThemeColors::interpolate(fillColor, colors.surfaceHover(), hoverProgress() * 0.32);
        QColor activeFill = ThemeColors::interpolate(colors.surfaceHover(),
            ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 0.54 : 1.85),
            colors.isDark ? 0.26 : 0.18);
        fillColor = ThemeColors::interpolate(fillColor, activeFill, activeProgress() * 0.92);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fillColor);
        painter.drawRoundedRect(rowRect, radius, radius);
        drawDisplayColorBackgroundAccent(painter, rowRect, radius, colors);

        const QRectF previewRect = rowRect.adjusted(1.0, 1.0, -1.0, -1.0);
        drawPreview(painter, previewRect, radius, colors);
        drawPreviewColorWash(painter, rowRect, radius, colors);
        drawDisplayColorBorderAccent(painter, rowRect, radius, colors);

        const int leftOffset = ThemeManager::instance().scaled(10);
        QRect textRect(leftOffset, ThemeManager::instance().scaled(12),
            width() - leftOffset - ThemeManager::instance().scaled(8),
            height() - ThemeManager::instance().scaled(15));

        QFont textFont = painter.font();
        textFont.setPixelSize(ThemeManager::instance().scaled(10));
        textFont.setWeight(activeProgress() > 0.5 ? QFont::Medium : QFont::Normal);
        painter.setFont(textFont);
        const QColor baseText = ThemeColors::interpolate(colors.textMuted, colors.textMuted, 0.0);
        const QColor hoverText = ThemeColors::interpolate(colors.textMuted, colors.text, 0.22);
        const QColor activeText = ThemeColors::interpolate(colors.textMuted, colors.text, 0.62);
        QColor textColor = ThemeColors::interpolate(baseText, hoverText, hoverProgress());
        textColor = ThemeColors::interpolate(textColor, activeText, activeProgress());
        painter.setPen(textColor);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignBottom,
            painter.fontMetrics().elidedText(
                translatedBrushText(m_brush.name), Qt::ElideRight, textRect.width()));
    }

private:
    void drawDisplayColorBackgroundAccent(
        QPainter& painter, const QRectF& rect, qreal radius, const ThemeColors& colors)
    {
        const QColor accentBase = displayAccentColor(m_brush.displayColorIndex);
        if (!accentBase.isValid()) {
            return;
        }

        QColor accent
            = ThemeColors::interpolate(accentBase, colors.text, colors.isDark ? 0.08 : 0.16);
        QLinearGradient fillGrad(rect.left(), rect.center().y(), rect.right(), rect.center().y());
        QColor stop0 = accent;
        stop0.setAlphaF(0.12);
        QColor stop1 = accent;
        stop1.setAlphaF(0.075);
        QColor stop2 = accent;
        stop2.setAlphaF(0.03);
        QColor stop3 = accent;
        stop3.setAlphaF(0.008);
        QColor stop4 = accent;
        stop4.setAlpha(0);

        fillGrad.setColorAt(0.0, stop0);
        fillGrad.setColorAt(0.16, stop1);
        fillGrad.setColorAt(0.38, stop2);
        fillGrad.setColorAt(0.72, stop3);
        fillGrad.setColorAt(1.0, stop4);

        painter.setPen(Qt::NoPen);
        painter.setBrush(fillGrad);
        painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void drawDisplayColorBorderAccent(
        QPainter& painter, const QRectF& rect, qreal radius, const ThemeColors& colors)
    {
        const QColor accentBase = displayAccentColor(m_brush.displayColorIndex);
        if (!accentBase.isValid()) {
            return;
        }

        QColor accent = ThemeColors::adjustBrightness(accentBase, colors.isDark ? 1.12 : 0.95);
        QLinearGradient borderGrad(rect.left(), rect.center().y(), rect.right(), rect.center().y());
        QColor stop0 = accent;
        stop0.setAlphaF(0.74);
        QColor stop1 = accent;
        stop1.setAlphaF(0.5);
        QColor stop2 = accent;
        stop2.setAlphaF(0.18);
        QColor stop3 = accent;
        stop3.setAlphaF(0.035);
        QColor stop4 = accent;
        stop4.setAlphaF(0.006);
        QColor stop5 = accent;
        stop5.setAlpha(0);

        borderGrad.setColorAt(0.0, stop0);
        borderGrad.setColorAt(0.11, stop1);
        borderGrad.setColorAt(0.28, stop2);
        borderGrad.setColorAt(0.52, stop3);
        borderGrad.setColorAt(0.78, stop4);
        borderGrad.setColorAt(1.0, stop5);

        QPen borderPen(QBrush(borderGrad), 1.0);
        borderPen.setCosmetic(true);
        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

    void invalidatePreviewCache()
    {
        m_previewImage = QImage();
        m_previewWidth = 0;
        m_previewHeight = 0;
        m_previewColor = 0;
    }

    void requestPreview(const QColor& previewColor, int width, int height)
    {
        ruwa::core::brushes::BrushPreviewSpec spec;
        spec.settings = m_brush.settings;
        spec.sizeNorm = 0.5;
        spec.opacityNorm = 1.0;
        spec.color = previewColor;
        spec.size = QSize(width, height);

        if (!m_previewSession) {
            return;
        }
        if (!m_previewSession->hasImageFor(spec)) {
            // Kick off (or refresh) the async render. Until it lands we keep
            // drawing the last faded image, so there's no flicker mid-animation.
            m_previewSession->request(spec);
            return;
        }

        const QRgb rgba = previewColor.rgba();
        if (!m_previewImage.isNull() && m_previewWidth == width && m_previewHeight == height
            && m_previewColor == rgba) {
            return;
        }

        // The session image matches the current spec exactly; bake the
        // horizontal alpha fade into a per-row copy so paint stays cheap.
        QImage preview = m_previewSession->image();
        if (preview.isNull()) {
            invalidatePreviewCache();
            return;
        }
        width = preview.width();
        height = preview.height();

        QPainter fadePainter(&preview);
        fadePainter.setRenderHint(QPainter::Antialiasing);
        fadePainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);

        QLinearGradient alphaFade(QPointF(0.0, 0.0), QPointF(width, 0.0));
        QColor leftAlpha(0, 0, 0, 255);
        QColor midAlpha(0, 0, 0, 160);
        QColor rightAlpha(0, 0, 0, 0);
        alphaFade.setColorAt(0.0, leftAlpha);
        alphaFade.setColorAt(0.45, leftAlpha);
        alphaFade.setColorAt(0.78, midAlpha);
        alphaFade.setColorAt(1.0, rightAlpha);
        fadePainter.fillRect(preview.rect(), alphaFade);

        m_previewImage = preview;
        m_previewWidth = width;
        m_previewHeight = height;
        m_previewColor = rgba;
    }

    void drawPreviewColorWash(QPainter& painter, const QRectF& rect, qreal radius,
        const ruwa::ui::core::ThemeColors& colors)
    {
        const QColor accentBase = displayAccentColor(m_brush.displayColorIndex);
        if (!accentBase.isValid()) {
            return;
        }

        QColor accent = ThemeColors::adjustBrightness(accentBase, colors.isDark ? 1.10 : 0.96);
        QLinearGradient wash(rect.topLeft(), rect.topRight());
        QColor stop0 = accent;
        stop0.setAlpha(colors.isDark ? 46 : 37);
        QColor stop1 = accent;
        stop1.setAlpha(colors.isDark ? 27 : 21);
        QColor stop2 = accent;
        stop2.setAlpha(colors.isDark ? 10 : 8);
        QColor stop3 = accent;
        stop3.setAlpha(0);
        wash.setColorAt(0.0, stop0);
        wash.setColorAt(0.20, stop1);
        wash.setColorAt(0.46, stop2);
        wash.setColorAt(0.76, stop3);

        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(wash);
        painter.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        painter.restore();
    }

    void drawPreview(QPainter& painter, const QRectF& rect, qreal radius,
        const ruwa::ui::core::ThemeColors& colors)
    {
        const int previewWidth = qMax(1, static_cast<int>(rect.width()));
        const int previewHeight = qMax(1, static_cast<int>(rect.height()));
        QColor previewColor
            = ThemeColors::interpolate(colors.textMuted, colors.primary, activeProgress());
        previewColor = ThemeColors::interpolate(previewColor,
            ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.18 : 0.94),
            activeProgress() * 0.65);
        requestPreview(previewColor, previewWidth, previewHeight);
        if (m_previewImage.isNull()) {
            return;
        }

        painter.save();
        QPainterPath clipPath;
        clipPath.addRoundedRect(rect, radius, radius);
        painter.setClipPath(clipPath);

        const qreal previewOpacity = (colors.isDark ? 0.46 : 0.40) + hoverProgress() * 0.10
            + activeProgress() * (colors.isDark ? 0.38 : 0.34);
        painter.setOpacity(previewOpacity);
        painter.drawImage(rect.toRect(), m_previewImage);
        painter.setOpacity(1.0);

        QLinearGradient fade(rect.topLeft(), rect.topRight());
        QColor fadeStart = colors.surface;
        QColor fadeMid = colors.surface;
        QColor fadeEnd = colors.surface;
        const int startAlpha
            = qRound((colors.isDark ? 246 : 236) - activeProgress() * (colors.isDark ? 34 : 28));
        const int midAlpha
            = qRound((colors.isDark ? 178 : 138) - activeProgress() * (colors.isDark ? 48 : 40));
        fadeStart.setAlpha(startAlpha);
        fadeMid.setAlpha(midAlpha);
        fadeEnd.setAlpha(0);
        fade.setColorAt(0.0, fadeStart);
        fade.setColorAt(0.30, fadeStart);
        fade.setColorAt(0.72, fadeMid);
        fade.setColorAt(1.0, fadeEnd);
        painter.fillRect(rect, fade);
        painter.restore();
    }

    BrushListBrushData m_brush;
    ruwa::core::brushes::BrushPreviewSession* m_previewSession = nullptr;
    QImage m_previewImage;
    int m_previewWidth = 0;
    int m_previewHeight = 0;
    QRgb m_previewColor = 0;
    std::function<void(const QString&)> m_openEditorCallback;
};

} // namespace

BrushPackListSection::BrushPackListSection(QWidget* parent)
    : QWidget(parent)
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(2);

    auto* headerButton = new PackHeaderButton(this);
    m_headerButton = headerButton;
    rootLayout->addWidget(m_headerButton);

    m_contentContainer = new ruwa::ui::widgets::AnimatedFlowWidget(
        ruwa::ui::widgets::AnimatedFlowWidget::LayoutStyle::UniformWrap, this);
    m_contentContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_contentContainer->setMinimumHeight(0);
    m_contentContainer->setMaximumHeight(0);
    m_contentContainer->setContentsMargins(6, 0, 2, 0);
    m_contentContainer->setFlowSpacing(2, 2);
    // While reflowing on a width change, glide the section's content height in
    // lockstep with the wrapping rows — but never fight the expand/collapse
    // QPropertyAnimation, which owns the height during open/close.
    m_contentContainer->setHeightCallback([this](int height) {
        if (!m_expanded)
            return;
        if (m_expandAnimation && m_expandAnimation->state() == QAbstractAnimation::Running)
            return;
        setContentHeight(height);
    });
    rootLayout->addWidget(m_contentContainer);

    m_expandAnimation = new QPropertyAnimation(this, "contentHeight", this);
    m_expandAnimation->setDuration(220);
    m_expandAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    connect(m_expandAnimation, &QPropertyAnimation::valueChanged, this,
        [this](const QVariant&) { emit contentGeometryChanged(); });
    connect(headerButton, &QAbstractButton::clicked, this,
        [this]() { setExpanded(!m_expanded, true); });
}

BrushPackListSection::~BrushPackListSection()
{
    if (m_expandAnimation) {
        m_expandAnimation->stop();
    }
    if (m_contentContainer) {
        m_contentContainer->shutdown();
    }
}

void BrushPackListSection::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (!m_expanded || !m_contentContainer) {
        return;
    }

    if (event->size().width() == event->oldSize().width()) {
        return;
    }

    // The flow widget animates the row buttons and drives the content height
    // through its callback on a width change. Only when the expand/collapse
    // animation currently owns the height do we retarget it to the new width.
    if (m_expandAnimation->state() == QAbstractAnimation::Running) {
        scheduleExpandedHeightRefresh();
    }
}

void BrushPackListSection::setPackData(const BrushListPackData& pack)
{
    m_pack = pack;

    auto* headerButton = static_cast<PackHeaderButton*>(m_headerButton);
    headerButton->setTitle(m_pack.name);
    rebuildBrushRows();
    updateSelectionState();
    setContentHeight(m_expanded ? expandedContentHeight() : 0);
}

void BrushPackListSection::updatePackName(const QString& newName)
{
    m_pack.name = newName;
    static_cast<PackHeaderButton*>(m_headerButton)->setTitle(newName);
}

void BrushPackListSection::setExpanded(bool expanded, bool animated)
{
    if (m_expanded == expanded
        && (!animated || m_expandAnimation->state() != QAbstractAnimation::Running)) {
        updateExpandedVisualState(false);
        return;
    }

    m_expanded = expanded;
    updateExpandedVisualState(animated);
    emit toggled(m_pack.id, m_expanded);
}

void BrushPackListSection::setSelectedBrushId(const QString& brushId)
{
    m_selectedBrushId = brushId;
    updateSelectionState();
}

bool BrushPackListSection::updateBrushSettings(
    const QString& brushId, const ruwa::core::brushes::BrushSettingsData& settings)
{
    bool found = false;
    for (auto& brush : m_pack.brushes) {
        if (brush.id == brushId) {
            brush.settings = settings;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    // m_brushRows only contains PackBrushRowButton instances (see rebuildBrushRows).
    if (QWidget* widget = m_brushRows.value(brushId, nullptr)) {
        static_cast<PackBrushRowButton*>(widget)->setBrushSettings(settings);
    }
    return true;
}

bool BrushPackListSection::updateBrushDisplayColorIndex(const QString& brushId, int colorIndex)
{
    bool found = false;
    for (auto& brush : m_pack.brushes) {
        if (brush.id == brushId) {
            brush.displayColorIndex = colorIndex;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    if (QWidget* widget = m_brushRows.value(brushId, nullptr)) {
        static_cast<PackBrushRowButton*>(widget)->setBrushDisplayColorIndex(colorIndex);
    }
    return true;
}

bool BrushPackListSection::updateBrushName(const QString& brushId, const QString& newName)
{
    bool found = false;
    for (auto& brush : m_pack.brushes) {
        if (brush.id == brushId) {
            brush.name = newName;
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }

    if (QWidget* widget = m_brushRows.value(brushId, nullptr)) {
        static_cast<PackBrushRowButton*>(widget)->setBrushName(newName);
    }
    return true;
}

void BrushPackListSection::setContentHeight(int height)
{
    const int clampedHeight = qMax(0, height);
    if (m_contentHeight == clampedHeight) {
        return;
    }

    m_contentHeight = clampedHeight;
    m_contentContainer->setMinimumHeight(clampedHeight);
    m_contentContainer->setMaximumHeight(clampedHeight);
    emit contentGeometryChanged();
}

void BrushPackListSection::rebuildBrushRows()
{
    m_brushRows.clear();
    m_contentContainer->clearItems(
        ruwa::ui::widgets::AnimatedFlowWidget::ItemDisposal::DeleteLater);

    if (m_pack.brushes.isEmpty()) {
        m_emptyLabel = new QLabel(tr("Pack is empty"), m_contentContainer);
        m_emptyLabel->setContentsMargins(12, 6, 12, 6);
        m_emptyLabel->setStyleSheet(QStringLiteral("background: transparent;"));
        m_contentContainer->setItems({ m_emptyLabel });
        return;
    }

    m_emptyLabel = nullptr;
    QList<QWidget*> rows;
    rows.reserve(m_pack.brushes.size());
    for (const BrushListBrushData& brush : m_pack.brushes) {
        auto* rowButton = new PackBrushRowButton(brush, m_contentContainer);
        rowButton->setOpenEditorCallback(
            [this](const QString& brushId) { emit brushEditorRequested(m_pack.id, brushId); });
        connect(rowButton, &QAbstractButton::clicked, this,
            [this, brushId = brush.id]() { emit brushActivated(m_pack.id, brushId); });
        rows.append(rowButton);
        m_brushRows.insert(brush.id, rowButton);
    }
    m_contentContainer->setItems(rows);
}

void BrushPackListSection::updateExpandedVisualState(bool animated)
{
    auto* headerButton = static_cast<PackHeaderButton*>(m_headerButton);
    headerButton->setExpanded(m_expanded, animated);

    // Only an expanded section glides its rows on resize; a collapsed one is
    // clipped to zero height, so snapping its hidden rows is cheaper.
    m_contentContainer->setReflowAnimated(m_expanded);

    const int targetHeight = m_expanded ? expandedContentHeight() : 0;
    if (!animated) {
        m_expandAnimation->stop();
        setContentHeight(targetHeight);
        return;
    }

    animateContentHeightTo(targetHeight);
}

void BrushPackListSection::updateSelectionState()
{
    const bool hasSelectedBrush = m_brushRows.contains(m_selectedBrushId);
    static_cast<PackHeaderButton*>(m_headerButton)->setActive(hasSelectedBrush);

    for (auto it = m_brushRows.begin(); it != m_brushRows.end(); ++it) {
        auto* rowButton = static_cast<PackBrushRowButton*>(it.value());
        rowButton->setActive(it.key() == m_selectedBrushId);
    }
}

void BrushPackListSection::scheduleExpandedHeightRefresh()
{
    if (m_heightRefreshQueued || !m_expanded) {
        return;
    }

    m_heightRefreshQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_heightRefreshQueued = false;

        if (!m_expanded || !m_contentContainer) {
            return;
        }

        const int targetHeight = expandedContentHeight();
        if (m_expandAnimation->state() == QAbstractAnimation::Running) {
            animateContentHeightTo(targetHeight);
        } else {
            setContentHeight(targetHeight);
        }
    });
}

void BrushPackListSection::animateContentHeightTo(int targetHeight)
{
    const int clampedTarget = qMax(0, targetHeight);
    const int currentHeight = m_contentHeight;
    if (currentHeight == clampedTarget) {
        return;
    }

    const int duration = contentAnimationDurationForDelta(qAbs(clampedTarget - currentHeight));
    m_expandAnimation->stop();
    m_expandAnimation->setDuration(duration);
    m_expandAnimation->setStartValue(currentHeight);
    m_expandAnimation->setEndValue(clampedTarget);
    m_expandAnimation->start();
}

int BrushPackListSection::contentAnimationDurationForDelta(int delta) const
{
    return qBound(
        kExpandAnimationMinMs, qRound(delta * kExpandAnimationMsPerPixel), kExpandAnimationMaxMs);
}

int BrushPackListSection::expandedContentHeight() const
{
    if (!m_contentContainer) {
        return 0;
    }

    int availableWidth = m_contentContainer->width();
    if (availableWidth <= 0) {
        availableWidth = width();
    }

    return qMax(0, m_contentContainer->targetHeightForWidth(availableWidth));
}

} // namespace ruwa::ui::workspace
