// SPDX-License-Identifier: MPL-2.0

// BrushPackPanel.cpp
#include "BrushPackPanel.h"
#include "features/brush/engine/BrushEngineRegistry.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"
#include "features/brush/ui/BrushSettingsWidget.h"
#include "features/brush/manager/BrushSettingDefs.h"
#include "features/brush/manager/BrushPreviewManager.h"
#include "features/brush/editor/BrushEditorWindow.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/resources/FontFamilyNames.h"
#include "shared/resources/IconProvider.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/style/PaintingUtils.h"

#include <QCoreApplication>
#include <QSignalBlocker>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMouseEvent>
#include <QGraphicsOpacityEffect>
#include <QCursor>
#include <QAbstractAnimation>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

QString translateBrushOrPackName(const QString& name)
{
    if (name.isEmpty())
        return name;
    return QCoreApplication::translate("QObject", name.toUtf8().constData());
}

QString compactPresetLabel(const QString& name)
{
    const QString translated = translateBrushOrPackName(name).trimmed();
    QString shortLabel;
    shortLabel.reserve(3);
    for (const QChar ch : translated) {
        if (ch.isSpace()) {
            continue;
        }
        shortLabel.append(ch);
        if (shortLabel.size() >= 3) {
            break;
        }
    }
    if (shortLabel.isEmpty()) {
        shortLabel = translated.left(3);
    }
    return shortLabel.toUpper();
}

void detachAndDeleteWidget(QWidget* widget, QLayout* layout = nullptr)
{
    if (!widget) {
        return;
    }

    if (layout) {
        layout->removeWidget(widget);
    }

    widget->hide();
    widget->setParent(nullptr);
    widget->deleteLater();
}

class BrushEditorOpenButton : public BaseAnimatedButton {
public:
    explicit BrushEditorOpenButton(const QString& text, QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setText(text);
        setHoverDuration(180);
        setActiveDuration(170);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
    }

    void setIconPixmap(const QPixmap& icon)
    {
        m_icon = icon;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();

        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = r.height() / 2.0;
        const bool enabled = isEnabled();
        const qreal hover = enabled ? hoverProgress() : 0.0;

        QColor borderColor
            = ThemeColors::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), hover);
        if (!enabled) {
            borderColor = ThemeColors::withAlpha(borderColor, colors.isDark ? 75 : 110);
        }
        if (borderColor.alphaF() > 0.02) {
            QPen borderPen(borderColor);
            borderPen.setWidthF(1.0);
            borderPen.setCosmetic(true);
            painter.setPen(borderPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(r, radius, radius);
        }

        QColor inactiveText = ThemeColors::interpolate(colors.textMuted, colors.text, hover);
        QColor textColor = enabled ? inactiveText : colors.textDisabled();

        QFont f = painter.font();
        f.setPixelSize(theme.scaled(11));
        f.setWeight(QFont::DemiBold);
        painter.setFont(f);

        const int iconSize = theme.scaled(13);
        const int contentGap = theme.scaled(7);
        const int sidePad = theme.scaled(12);
        const QString textValue = text();
        const int textW = painter.fontMetrics().horizontalAdvance(textValue);
        const bool hasIcon = !m_icon.isNull();
        const int iconW = hasIcon ? iconSize : 0;
        const int gapW = hasIcon ? contentGap : 0;
        const int contentW = iconW + gapW + textW;
        int contentStartX = (width() - contentW) / 2;
        contentStartX = qMax(contentStartX, static_cast<int>(r.left()) + sidePad);

        int textStartX = contentStartX;
        if (hasIcon) {
            QPixmap scaled
                = m_icon.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QPixmap colored(scaled.size());
            colored.fill(Qt::transparent);
            {
                QPainter iconPainter(&colored);
                iconPainter.drawPixmap(0, 0, scaled);
                iconPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                iconPainter.fillRect(colored.rect(), textColor);
            }
            const int iconX = contentStartX;
            const int iconY = (height() - scaled.height()) / 2;
            painter.drawPixmap(iconX, iconY, colored);
            textStartX = iconX + iconSize + contentGap;
        }

        painter.setPen(textColor);
        QRect textRect(textStartX, 0, width() - textStartX - sidePad, height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
            painter.fontMetrics().elidedText(textValue, Qt::ElideRight, textRect.width()));

        if (enabled && isPressed()) {
            painter.setPen(Qt::NoPen);
            QColor overlay = colors.overlay(0.04);
            painter.setBrush(overlay);
            painter.drawRoundedRect(r, radius, radius);
        }
    }

private:
    QPixmap m_icon;
};

} // namespace

// BrushItem
// ============================================================================

BrushItem::BrushItem(const BrushData& data, QWidget* parent)
    : QWidget(parent)
    , m_data(data)
{
    auto& theme = ThemeManager::instance();
    setFixedHeight(theme.scaled(ItemHeight));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground);

    m_hoverAnim = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnim->setDuration(150);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_activeAnim = new QPropertyAnimation(this, "activeProgress", this);
    m_activeAnim->setDuration(200);
    m_activeAnim->setEasingCurve(QEasingCurve::InOutCubic);

    auto& previewManager = ruwa::core::brushes::BrushPreviewManager::instance();
    m_strokePreviewSession = previewManager.createSession(BrushPreviewSession::Kind::Stroke, this);
    m_dotPreviewSession = previewManager.createSession(BrushPreviewSession::Kind::Dot, this);
    connect(m_strokePreviewSession, &BrushPreviewSession::imageChanged, this,
        QOverload<>::of(&BrushItem::update));
    connect(m_dotPreviewSession, &BrushPreviewSession::imageChanged, this,
        QOverload<>::of(&BrushItem::update));

    m_nameEditor = new QLineEdit(this);
    m_nameEditor->hide();
    m_nameEditor->setFrame(false);
    m_nameEditor->setText(m_data.name);
    m_nameEditor->setPlaceholderText(tr("Brush name"));
    m_nameEditor->setFocusPolicy(Qt::StrongFocus);
    m_nameEditor->setStyleSheet("QLineEdit {"
                                "  background: transparent;"
                                "  border: 1px solid transparent;"
                                "  border-radius: 4px;"
                                "  padding: 0 4px;"
                                "}"
                                "QLineEdit:focus {"
                                "  background: transparent;"
                                "  border: 1px solid transparent;"
                                "}");
    connect(m_nameEditor, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_data.name = text;
        emit nameEdited(m_data.id, text);
        update();
    });
    connect(m_nameEditor, &QLineEdit::editingFinished, this, [this]() { finishRename(); });
}

BrushItem::~BrushItem() = default;

void BrushItem::invalidatePreviewCache()
{
    update();
}

void BrushItem::setSelected(bool selected)
{
    if (m_selected == selected)
        return;
    m_selected = selected;

    m_activeAnim->stop();
    m_activeAnim->setStartValue(m_activeProgress);
    m_activeAnim->setEndValue(selected ? 1.0 : 0.0);
    m_activeAnim->start();
}

void BrushItem::setName(const QString& name)
{
    m_data.name = name;
    if (m_nameEditor && !m_nameEditor->hasFocus()) {
        m_nameEditor->setText(name);
    }
    update();
}

void BrushItem::setSettings(const BrushSettingsData& settings)
{
    m_data.settings = settings;
    invalidatePreviewCache();
    update();
}

void BrushItem::startRename()
{
    if (!m_nameEditor)
        return;
    m_isEditing = true;
    ensureEditorGeometry();
    m_nameEditor->setText(m_data.name);
    m_nameEditor->show();
    m_nameEditor->raise();
    m_nameEditor->setFocus();
    m_nameEditor->selectAll();
    update();
}

void BrushItem::setHoverProgress(qreal v)
{
    if (qFuzzyCompare(m_hoverProgress, v))
        return;
    m_hoverProgress = v;
    update();
}

void BrushItem::setActiveProgress(qreal v)
{
    if (qFuzzyCompare(m_activeProgress, v))
        return;
    m_activeProgress = v;
    update();
}

void BrushItem::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();
    const auto& colors = mgr.colors();

    int radius = theme.scaled(ItemCornerRadius);
    int headerH = theme.scaled(HeaderHeight);
    QRectF outerRect = rect().adjusted(2, 1, -2, -1);

    // === Outer background (BaseStyledWidget-like) ===
    // Background color: surfaceAlt, lighter on hover/active
    QColor bgColor = colors.surfaceAlt;
    if (m_activeProgress > 0.01) {
        bgColor = ThemeColors::interpolate(bgColor,
            ThemeColors::adjustBrightness(bgColor, colors.isDark ? 1.2 : 0.9), m_activeProgress);
    }
    if (m_hoverProgress > 0.01) {
        bgColor = ThemeColors::interpolate(bgColor,
            ThemeColors::adjustBrightness(bgColor, colors.isDark ? 1.1 : 0.95),
            m_hoverProgress * (1.0 - m_activeProgress * 0.5));
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawRoundedRect(outerRect, radius, radius);

    // === Border (gradient like BaseStyledWidget) ===
    {
        QRectF borderRect = outerRect.adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient borderGrad(borderRect.topLeft(), borderRect.bottomLeft());

        QColor topBorder = colors.borderSubtle();
        QColor bottomBorder = colors.borderSubtle();

        // Brighten border on active
        if (m_activeProgress > 0.01) {
            topBorder
                = ThemeColors::interpolate(topBorder, colors.borderSubtleHover(), m_activeProgress);
            bottomBorder
                = ThemeColors::interpolate(bottomBorder, colors.border, m_activeProgress * 0.5);
        }
        // Lighten on hover
        if (m_hoverProgress > 0.01) {
            topBorder = ThemeColors::interpolate(
                topBorder, colors.borderSubtleHover(), m_hoverProgress * 0.6);
        }

        borderGrad.setColorAt(0.0, topBorder);
        borderGrad.setColorAt(1.0, bottomBorder);

        QPen borderPen;
        borderPen.setBrush(borderGrad);
        borderPen.setWidthF(1.0);
        borderPen.setCosmetic(true);

        painter.setPen(borderPen);
        painter.setBrush(Qt::NoBrush);

        QPainterPath borderPath;
        borderPath.addRoundedRect(borderRect, radius - 0.5, radius - 0.5);
        painter.drawPath(borderPath);
    }

    // === Active indicator — primary color left bar ===
    if (m_activeProgress > 0.01) {
        QColor indicatorColor = colors.primary;
        indicatorColor.setAlphaF(m_activeProgress);
        painter.setPen(Qt::NoPen);
        painter.setBrush(indicatorColor);
        int barW = theme.scaled(2);
        qreal barTop = outerRect.top() + outerRect.height() * 0.15;
        qreal barH = outerRect.height() * 0.7;
        painter.drawRoundedRect(QRectF(outerRect.left() + 1, barTop, barW, barH), 1, 1);
    }

    // === Header section (thin top strip with brush name) ===
    QRectF headerRect(outerRect.left(), outerRect.top(), outerRect.width(), headerH);

    // Divider line between header and preview
    {
        QColor divColor = colors.border;
        divColor.setAlpha(60);
        qreal divY = headerRect.bottom();
        painter.setPen(QPen(divColor, 1));
        painter.drawLine(QPointF(outerRect.left() + radius * 0.5, divY),
            QPointF(outerRect.right() - radius * 0.5, divY));
    }

    // Brush name text in header (hidden while inline editor is shown)
    if (!m_isEditing) {
        QColor textColor = ThemeColors::interpolate(
            colors.textMuted, colors.text, qMax(m_hoverProgress * 0.5, m_activeProgress));
        painter.setPen(textColor);
        QFont f = painter.font();
        f.setPixelSize(theme.scaled(10));
        f.setBold(m_activeProgress > 0.5);
        painter.setFont(f);

        QRectF textRect = headerRect.adjusted(theme.scaled(8), 0, -theme.scaled(6), 0);
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
            painter.fontMetrics().elidedText(translateBrushOrPackName(m_data.name), Qt::ElideRight,
                static_cast<int>(textRect.width())));
    }

    // === Preview section (stroke preview left, dot preview right) ===
    int mosaicPad = theme.scaled(7);
    int previewGap = theme.scaled(7);
    QRectF fullPreviewRect(outerRect.left() + mosaicPad, headerRect.bottom() + theme.scaled(6),
        outerRect.width() - 2 * mosaicPad,
        outerRect.bottom() - headerRect.bottom() - theme.scaled(7));
    const int dotSize = qMin(static_cast<int>(fullPreviewRect.width() * 0.30),
        static_cast<int>(fullPreviewRect.height() * 0.94));
    QRectF strokePreviewRect(fullPreviewRect.left(), fullPreviewRect.top(),
        fullPreviewRect.width() - dotSize - previewGap, fullPreviewRect.height());
    QRectF dotPreviewRect(fullPreviewRect.right() - dotSize,
        fullPreviewRect.top() + (fullPreviewRect.height() - dotSize) / 2.0, dotSize, dotSize);
    int mosaicRadius = theme.scaled(6);

    {
        painter.save();

        QPainterPath mosaicClip;
        mosaicClip.addRoundedRect(fullPreviewRect, mosaicRadius, mosaicRadius);
        painter.setClipPath(mosaicClip);

        QColor previewColor = colors.primary;
        const int pw = qMax(16, static_cast<int>(strokePreviewRect.width()));
        const int ph = qMax(16, static_cast<int>(strokePreviewRect.height()));
        const int dotPx = qMax(16, dotSize - 2);
        ruwa::core::brushes::BrushPreviewSpec strokeSpec;
        strokeSpec.settings = m_data.settings;
        strokeSpec.color = previewColor;
        strokeSpec.size = QSize(pw, ph);

        // Stroke preview (left) — per-item cache; rendered asynchronously on first paint
        ruwa::core::brushes::BrushPreviewSpec dotSpec;
        dotSpec.settings = m_data.settings;
        dotSpec.color = previewColor;
        dotSpec.size = QSize(dotPx, dotPx);

        if (m_strokePreviewSession && !m_strokePreviewSession->hasImageFor(strokeSpec)) {
            m_strokePreviewSession->request(strokeSpec);
        }

        if (m_dotPreviewSession && !m_dotPreviewSession->hasImageFor(dotSpec)) {
            m_dotPreviewSession->request(dotSpec);
        }

        const QImage strokePreview
            = m_strokePreviewSession ? m_strokePreviewSession->image() : QImage();
        const QImage dotPreview = m_dotPreviewSession ? m_dotPreviewSession->image() : QImage();
        if (!strokePreview.isNull()) {
            painter.drawImage(strokePreviewRect.toRect(), strokePreview);
        }
        if (!dotPreview.isNull()) {
            painter.drawImage(dotPreviewRect.toRect(), dotPreview);
        }

        painter.restore();
    }

    // === Press darkening ===
    if (m_pressed) {
        QColor pressOverlay = colors.overlay(0.06);
        painter.setPen(Qt::NoPen);
        painter.setBrush(pressOverlay);
        painter.drawRoundedRect(outerRect, radius, radius);
    }
}

void BrushItem::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(1.0);
    m_hoverAnim->start();
}

void BrushItem::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(0.0);
    m_hoverAnim->start();
}

void BrushItem::mousePressEvent(QMouseEvent* event)
{
    if (m_isEditing) {
        QWidget::mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        m_pressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void BrushItem::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_isEditing) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && m_pressed) {
        m_pressed = false;
        if (rect().contains(event->pos())) {
            emit clicked(m_data.id);
        }
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void BrushItem::mouseDoubleClickEvent(QMouseEvent* event)
{
    QWidget::mouseDoubleClickEvent(event);
}

void BrushItem::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (event->size() != event->oldSize()) {
        invalidatePreviewCache();
    }
    ensureEditorGeometry();
}

void BrushItem::ensureEditorGeometry()
{
    if (!m_nameEditor)
        return;
    auto& theme = ThemeManager::instance();
    const int marginLeft = theme.scaled(10);
    const int marginRight = theme.scaled(10);
    const int top = theme.scaled(3);
    const int heightPx = theme.scaled(HeaderHeight - 6);
    m_nameEditor->setGeometry(
        marginLeft, top, qMax(50, width() - marginLeft - marginRight), heightPx);
}

void BrushItem::finishRename(bool keepVisible)
{
    m_isEditing = false;
    if (m_nameEditor && !keepVisible) {
        m_nameEditor->hide();
    }
    update();
}

// ============================================================================
// PresetButton (BaseAnimatedButton)
// ============================================================================

PresetButton::PresetButton(const BrushPresetData& data, QWidget* parent)
    : BaseAnimatedButton(parent)
    , m_data(data)
{
    auto& theme = ThemeManager::instance();
    setFixedHeight(theme.scaled(ButtonHeight));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setHoverDuration(150);
    setActiveDuration(200);

    loadIcon();

    m_nameEditor = new QLineEdit(this);
    m_nameEditor->hide();
    m_nameEditor->setFrame(false);
    m_nameEditor->setText(m_data.name);
    m_nameEditor->setPlaceholderText(tr("Pack name"));
    m_nameEditor->setStyleSheet("QLineEdit {"
                                "  background: transparent;"
                                "  border: 1px solid transparent;"
                                "  border-radius: 4px;"
                                "  padding: 0 4px;"
                                "}"
                                "QLineEdit:focus {"
                                "  background: transparent;"
                                "  border: 1px solid transparent;"
                                "}");
    connect(m_nameEditor, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_data.name = text;
        emit nameEdited(m_data.id, text);
        update();
    });
    connect(m_nameEditor, &QLineEdit::editingFinished, this, [this]() { finishRename(); });
}

PresetButton::~PresetButton() = default;

void PresetButton::setSelected(bool selected)
{
    if (m_isSelected == selected)
        return;
    m_isSelected = selected;
    setActive(selected); // triggers BaseAnimatedButton's activeProgress animation
}

void PresetButton::setName(const QString& name)
{
    m_data.name = name;
    if (m_nameEditor && !m_nameEditor->hasFocus()) {
        m_nameEditor->setText(name);
    }
    update();
}

void PresetButton::startRename()
{
    if (!m_nameEditor)
        return;
    m_isEditing = true;
    ensureEditorGeometry();
    m_nameEditor->setText(m_data.name);
    m_nameEditor->show();
    m_nameEditor->raise();
    m_nameEditor->setFocus();
    m_nameEditor->selectAll();
    update();
}

void PresetButton::loadIcon()
{
    auto& icons = IconProvider::instance();
    auto& theme = ThemeManager::instance();
    int sz = theme.scaled(IconSize);

    if (!m_data.iconPath.isEmpty()) {
        m_icon = QPixmap(m_data.iconPath);
        if (!m_icon.isNull()) {
            m_icon = m_icon.scaled(sz, sz, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    // If no icon loaded, paintEvent will draw an initial letter
}

void PresetButton::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();
    const auto& colors = mgr.colors();

    int radius = theme.scaled(7);
    int pad = theme.scaled(InternalPadding);
    QRectF r = rect().adjusted(pad / 2, 1, -pad / 2, -1);

    int iconSz = theme.scaled(IconSize);
    const bool compactMode = parentWidget() && parentWidget()->property("compactMode").toBool();
    QRectF iconRect(
        r.left() + (r.width() - iconSz) / 2.0, r.top() + theme.scaled(7), iconSz, iconSz);
    QRectF textRect(r.left() + theme.scaled(3), iconRect.bottom() + theme.scaled(5),
        r.width() - theme.scaled(6), r.bottom() - iconRect.bottom() - theme.scaled(8));

    if (compactMode) {
        QColor compactBg = ThemeColors::interpolate(
            colors.overlayBase(), colors.overlayHover(), hoverProgress() * 0.8);
        compactBg = ThemeColors::interpolate(compactBg, colors.surfaceAlt, activeProgress() * 0.75);
        painter.setPen(Qt::NoPen);
        painter.setBrush(compactBg);
        painter.drawRoundedRect(r, radius, radius);
    }

    if (activeProgress() > 0.01) {
        QColor indicatorColor = colors.primary;
        indicatorColor.setAlphaF(activeProgress());
        painter.setPen(Qt::NoPen);
        painter.setBrush(indicatorColor);
        int barW = theme.scaled(2);
        qreal barTop = r.top() + r.height() * 0.15;
        qreal barH = r.height() * 0.7;
        painter.drawRoundedRect(QRectF(r.left() + 1, barTop, barW, barH), 1, 1);
    }

    if (!m_icon.isNull()) {
        QColor iconColor = ThemeColors::interpolate(
            colors.textMuted, colors.text, qMax(hoverProgress() * 0.4, activeProgress()));
        QPixmap colored = m_icon;
        {
            QPainter p(&colored);
            p.setCompositionMode(QPainter::CompositionMode_SourceIn);
            p.fillRect(colored.rect(), iconColor);
        }
        painter.drawPixmap(iconRect.toRect().topLeft(), colored);
    } else {
        QColor circleBg = colors.surfaceAlt;
        painter.setPen(Qt::NoPen);
        painter.setBrush(circleBg);
        painter.drawEllipse(iconRect);

        QColor initColor
            = ThemeColors::interpolate(colors.textMuted, colors.text, activeProgress());
        painter.setPen(initColor);
        QFont f = painter.font();
        f.setPixelSize(theme.scaled(10));
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(
            iconRect, Qt::AlignCenter, translateBrushOrPackName(m_data.name).left(1).toUpper());
    }

    if (!m_isEditing) {
        QColor textColor = ThemeColors::interpolate(
            colors.textMuted, colors.text, qMax(hoverProgress() * 0.5, activeProgress()));
        painter.setPen(textColor);
        QFont textFont = painter.font();
        textFont.setPixelSize(theme.scaled(10));
        textFont.setWeight(activeProgress() > 0.5 ? QFont::DemiBold : QFont::Medium);
        painter.setFont(textFont);
        const QString shortName = compactPresetLabel(m_data.name);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop,
            painter.fontMetrics().elidedText(
                shortName, Qt::ElideRight, static_cast<int>(textRect.width())));
    }

    // Press overlay
    if (isPressed()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.overlay(0.05));
        painter.drawRoundedRect(r, radius, radius);
    }
}

void PresetButton::mouseDoubleClickEvent(QMouseEvent* event)
{
    BaseAnimatedButton::mouseDoubleClickEvent(event);
}

void PresetButton::resizeEvent(QResizeEvent* event)
{
    BaseAnimatedButton::resizeEvent(event);
    ensureEditorGeometry();
}

void PresetButton::ensureEditorGeometry()
{
    if (!m_nameEditor)
        return;
    auto& theme = ThemeManager::instance();
    const int h = qMax(18, theme.scaled(20));
    const int x = theme.scaled(6);
    const int w = qMax(34, width() - x * 2);
    const int y = height() - h - theme.scaled(7);
    m_nameEditor->setGeometry(x, y, w, h);
}

void PresetButton::finishRename(bool keepVisible)
{
    m_isEditing = false;
    if (m_nameEditor && !keepVisible) {
        m_nameEditor->hide();
    }
    update();
}

// ============================================================================
// BrushPresetPage
// ============================================================================

BrushPresetPage::BrushPresetPage(const QString& presetId, QWidget* parent)
    : QWidget(parent)
    , m_presetId(presetId)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setupUI();
}

BrushPresetPage::~BrushPresetPage() = default;

void BrushPresetPage::setupUI()
{
    auto& theme = ThemeManager::instance();
    const auto& colors = WidgetStyleManager::instance().colors();

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    auto* brushHeaderRow = new QWidget(this);
    brushHeaderRow->setAttribute(Qt::WA_TranslucentBackground);
    auto* brushHeaderLayout = new QHBoxLayout(brushHeaderRow);
    brushHeaderLayout->setContentsMargins(
        theme.scaled(14), theme.scaled(6), theme.scaled(14), theme.scaled(4));
    brushHeaderLayout->setSpacing(theme.scaled(7));

    auto* brushesHeader = new QLabel(tr("Brushes"), brushHeaderRow);
    brushesHeader->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(colors.text.name(QColor::HexArgb)));
    QFont sectionHeaderFont(FontFamilyNames::InstrumentSerif);
    sectionHeaderFont.setPixelSize(theme.scaled(22));
    sectionHeaderFont.setWeight(QFont::Normal);
    brushesHeader->setFont(sectionHeaderFont);

    auto* openEditorButton = new BrushEditorOpenButton(tr("Brush Editor"), brushHeaderRow);
    QIcon pencilIcon = IconProvider::instance().getIcon(IconProvider::StandardIcon::Pencil);
    if (pencilIcon.isNull()) {
        pencilIcon = IconProvider::instance().getIcon(IconProvider::StandardIcon::Edit);
    }
    openEditorButton->setIconPixmap(pencilIcon.pixmap(theme.scaled(13), theme.scaled(13)));
    openEditorButton->setFixedHeight(theme.scaled(22));
    openEditorButton->setMinimumWidth(theme.scaled(124));
    m_openBrushEditorButton = openEditorButton;
    connect(m_openBrushEditorButton, &QPushButton::clicked, this, [this]() { openBrushEditor(); });

    brushHeaderLayout->addWidget(brushesHeader);
    brushHeaderLayout->addStretch();
    brushHeaderLayout->addWidget(m_openBrushEditorButton);
    m_mainLayout->addWidget(brushHeaderRow);

    // --- Brush list (top section) ---
    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setAttribute(Qt::WA_TranslucentBackground);
    m_scrollArea->setAutoFillBackground(false);
    m_scrollArea->setStyleSheet("background: transparent;");

    m_brushListWidget = new QWidget();
    m_brushListWidget->setAttribute(Qt::WA_TranslucentBackground);
    m_brushListLayout = new QVBoxLayout(m_brushListWidget);
    m_brushListLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(1), theme.scaled(10), theme.scaled(6));
    m_brushListLayout->setSpacing(theme.scaled(4));
    m_brushListLayout->setAlignment(Qt::AlignTop);
    m_brushListLayout->addStretch();

    m_scrollArea->setWidget(m_brushListWidget);

    m_mainLayout->addWidget(m_scrollArea, 62);

    // --- Settings section (bottom, placeholder) ---
    m_settingsContainer = new QWidget(this);
    m_settingsContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_settingsLayout = new QVBoxLayout(m_settingsContainer);
    m_settingsLayout->setContentsMargins(
        theme.scaled(12), theme.scaled(4), theme.scaled(12), theme.scaled(10));
    m_settingsLayout->setSpacing(theme.scaled(3));

    auto* settingsHeaderRow = new QWidget(m_settingsContainer);
    settingsHeaderRow->setAttribute(Qt::WA_TranslucentBackground);
    auto* settingsHeaderLayout = new QHBoxLayout(settingsHeaderRow);
    settingsHeaderLayout->setContentsMargins(theme.scaled(2), 0, theme.scaled(2), theme.scaled(4));
    settingsHeaderLayout->setSpacing(theme.scaled(6));

    QLabel* settingsHeader = new QLabel(tr("Settings"), settingsHeaderRow);
    settingsHeader->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(colors.text.name(QColor::HexArgb)));
    QFont hdrFont(FontFamilyNames::InstrumentSerif);
    hdrFont.setPixelSize(theme.scaled(22));
    hdrFont.setWeight(QFont::Normal);
    settingsHeader->setFont(hdrFont);

    settingsHeaderLayout->addWidget(settingsHeader);
    settingsHeaderLayout->addStretch();
    m_settingsLayout->addWidget(settingsHeaderRow);

    // Scrollable area for settings rows
    m_settingsScrollArea = new SmoothScrollArea(m_settingsContainer);
    m_settingsScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_settingsScrollArea->setAttribute(Qt::WA_TranslucentBackground);
    m_settingsScrollArea->setAutoFillBackground(false);
    m_settingsScrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    m_settingsScrollContent = new QWidget();
    m_settingsScrollContent->setAttribute(Qt::WA_TranslucentBackground);
    m_settingsScrollLayout = new QVBoxLayout(m_settingsScrollContent);
    m_settingsScrollLayout->setContentsMargins(0, 0, 0, 0);
    m_settingsScrollLayout->setSpacing(0);
    m_settingsScrollLayout->setAlignment(Qt::AlignTop);
    m_settingsScrollLayout->addStretch();

    m_settingsScrollArea->setWidget(m_settingsScrollContent);
    m_settingsLayout->addWidget(m_settingsScrollArea, 1);

    rebuildSettingsWidget();

    // Rebuild quick settings only for the brush whose stars changed.
    connect(&BrushManager::instance(), &BrushManager::starredSettingsChanged, this,
        [this](const QString& brushId) {
            if (brushId == m_selectedBrushId) {
                rebuildSettingsWidget();
            }
        });

    updateBrushEditorButtonState();

    m_mainLayout->addWidget(m_settingsContainer, 38);
}

void BrushPresetPage::rebuildSettingsWidget()
{
    auto& theme = ThemeManager::instance();
    QColor dividerColor = WidgetStyleManager::instance().colors().border;
    dividerColor.setAlpha(90);

    // Full clear of scroll content layout
    if (m_settingsScrollLayout) {
        while (QLayoutItem* child = m_settingsScrollLayout->takeAt(0)) {
            if (QWidget* w = child->widget()) {
                detachAndDeleteWidget(w);
            }
            delete child;
        }
    }
    m_brushSettingsWidgets.clear();

    // Collect starred setting definitions for the selected brush.
    const QSet<QString> starred = m_selectedBrushId.isEmpty()
        ? QSet<QString> {}
        : BrushManager::instance().starredSettings(m_selectedBrushId);
    const auto tabs = ruwa::core::brushes::BrushEngineRegistry::instance()
                          .pixelModule()
                          ->descriptor()
                          .settingsTabs;
    bool firstVisibleSection = true;

    for (const auto& tab : tabs) {
        QVector<ruwa::core::brushes::BrushSettingDef> tabDefs;
        tabDefs.reserve(tab.settings.size());
        for (const auto& def : tab.settings) {
            if (starred.contains(QLatin1String(def.key))) {
                tabDefs.append(def);
            }
        }
        if (tabDefs.isEmpty()) {
            continue;
        }

        if (!firstVisibleSection) {
            auto* separator = new QWidget(m_settingsScrollContent);
            separator->setFixedHeight(theme.scaled(1));
            separator->setStyleSheet(
                QStringLiteral("background: %1;").arg(dividerColor.name(QColor::HexArgb)));
            m_settingsScrollLayout->addWidget(separator);
            m_settingsScrollLayout->addSpacing(theme.scaled(8));
        }

        QWidget* categoryHeader = new QWidget(m_settingsScrollContent);
        categoryHeader->setAttribute(Qt::WA_TranslucentBackground);
        auto* categoryLayout = new QHBoxLayout(categoryHeader);
        categoryLayout->setContentsMargins(0, 0, 0, 0);
        categoryLayout->setSpacing(theme.scaled(6));

        IconProvider::StandardIcon categoryIcon = IconProvider::StandardIcon::Settings;
        const QString tabId = QLatin1String(tab.id);
        if (tabId == QLatin1String("shape")) {
            categoryIcon = IconProvider::StandardIcon::Brush;
        } else if (tabId == QLatin1String("dynamics")) {
            categoryIcon = IconProvider::StandardIcon::Performance;
        } else if (tabId == QLatin1String("texture")) {
            categoryIcon = IconProvider::StandardIcon::Appearance;
        } else if (tabId == QLatin1String("scatter")) {
            categoryIcon = IconProvider::StandardIcon::Lasso;
        } else if (tabId == QLatin1String("stroke")) {
            categoryIcon = IconProvider::StandardIcon::Pencil;
        }

        QLabel* categoryIconLabel = new QLabel(categoryHeader);
        const int categoryIconPx = theme.scaled(12);
        const QColor iconTint = WidgetStyleManager::instance().colors().textMuted;
        QIcon rawIcon = IconProvider::instance().getColoredIcon(categoryIcon, iconTint);
        QPixmap iconPixmap = rawIcon.pixmap(categoryIconPx, categoryIconPx);
        if (iconPixmap.isNull()) {
            iconPixmap = IconProvider::instance().getPixmap(
                categoryIcon, QSize(categoryIconPx, categoryIconPx));
        }
        categoryIconLabel->setPixmap(iconPixmap);
        categoryIconLabel->setFixedSize(categoryIconPx, categoryIconPx);
        categoryIconLabel->setAlignment(Qt::AlignCenter);

        auto* categoryLabel = new QLabel(
            QCoreApplication::translate("ruwa::core::brushes", tab.label), categoryHeader);
        categoryLabel->setStyleSheet(
            QStringLiteral("color: %1; background: transparent; font-weight: 600;")
                .arg(WidgetStyleManager::instance().colors().textMuted.name(QColor::HexArgb)));
        QFont sectionFont = categoryLabel->font();
        sectionFont.setPixelSize(theme.scaled(10));
        categoryLabel->setFont(sectionFont);
        categoryLayout->addWidget(categoryIconLabel);
        categoryLayout->addWidget(categoryLabel, 1);
        m_settingsScrollLayout->addWidget(categoryHeader);
        m_settingsScrollLayout->addSpacing(theme.scaled(4));

        auto* sectionWidget = new BrushSettingsWidget(tabDefs, m_settingsScrollContent);
        m_settingsScrollLayout->addWidget(sectionWidget);
        m_brushSettingsWidgets.append(sectionWidget);
        m_settingsScrollLayout->addSpacing(theme.scaled(6));
        firstVisibleSection = false;
    }

    connectSettingsWidget();

    // Apply current brush settings to each visible category widget
    if (!m_selectedBrushId.isEmpty()) {
        for (const auto& brush : m_brushes) {
            if (brush.id == m_selectedBrushId) {
                for (auto* sectionWidget : m_brushSettingsWidgets) {
                    if (sectionWidget) {
                        sectionWidget->setSettings(brush.settings);
                    }
                }
                break;
            }
        }
    }

    m_settingsScrollLayout->addStretch();
    refreshSettingsGeometry();
}

void BrushPresetPage::refreshSettingsGeometry()
{
    if (!m_settingsScrollLayout || !m_settingsScrollContent || !m_settingsScrollArea) {
        return;
    }

    m_settingsScrollLayout->invalidate();
    m_settingsScrollLayout->activate();
    m_settingsScrollContent->adjustSize();
    m_settingsScrollContent->updateGeometry();
    QMetaObject::invokeMethod(m_settingsScrollArea, "updateScrollRange", Qt::QueuedConnection);

    // Run one more pass on next tick, when parent/viewport geometry is finalized.
    QTimer::singleShot(0, this, [this]() {
        if (!m_settingsScrollLayout || !m_settingsScrollContent || !m_settingsScrollArea) {
            return;
        }
        m_settingsScrollLayout->invalidate();
        m_settingsScrollLayout->activate();
        m_settingsScrollContent->adjustSize();
        m_settingsScrollContent->updateGeometry();
        QMetaObject::invokeMethod(m_settingsScrollArea, "updateScrollRange", Qt::QueuedConnection);
    });
}

void BrushPresetPage::connectSettingsWidget()
{
    for (auto* sectionWidget : m_brushSettingsWidgets) {
        if (!sectionWidget) {
            continue;
        }
        connect(sectionWidget, &BrushSettingsWidget::settingChanged, this, [this]() {
            if (m_selectedBrushId.isEmpty()) {
                return;
            }

            BrushSettingsData settings;
            for (const auto& brush : m_brushes) {
                if (brush.id == m_selectedBrushId) {
                    settings = brush.settings;
                    break;
                }
            }
            if (auto* senderWidget = qobject_cast<BrushSettingsWidget*>(sender())) {
                senderWidget->applyTo(settings);
            }

            emit brushSettingsEdited(m_selectedBrushId, settings);
            emit activeBrushSettingsChanged(settings);
        });
    }
}

void BrushPresetPage::setBrushes(const QVector<BrushData>& brushes)
{
    m_brushes = brushes;

    if (!hasBrush(m_selectedBrushId)) {
        m_selectedBrushId.clear();
    }

    rebuildBrushList();
    rebuildSettingsWidget();
    updateBrushEditorButtonState();
    refreshBrushListGeometry();
}

void BrushPresetPage::updateBrushSettingsFromManager(
    const QString& brushId, const BrushSettingsData& settings)
{
    bool updated = false;
    for (auto& brush : m_brushes) {
        if (brush.id == brushId) {
            brush.settings = settings;
            updated = true;
            break;
        }
    }
    if (!updated) {
        return;
    }

    if (BrushItem* item = m_brushItems.value(brushId)) {
        item->setSettings(settings);
    }

    if (brushId == m_selectedBrushId) {
        for (auto* sectionWidget : m_brushSettingsWidgets) {
            if (sectionWidget) {
                sectionWidget->setSettings(settings);
            }
        }
        emit activeBrushSettingsChanged(settings);
    }
}

bool BrushPresetPage::updateBrushNameFromManager(const QString& brushId, const QString& newName)
{
    bool updated = false;
    for (auto& brush : m_brushes) {
        if (brush.id == brushId) {
            brush.name = newName;
            updated = true;
            break;
        }
    }
    if (!updated) {
        return false;
    }

    if (BrushItem* item = m_brushItems.value(brushId)) {
        item->setName(newName);
    }
    return true;
}

void BrushPresetPage::clearSelectedBrushLocally()
{
    m_selectedBrushId.clear();

    for (auto it = m_brushItems.begin(); it != m_brushItems.end(); ++it) {
        if (it.value()) {
            it.value()->setSelected(false);
        }
    }

    if (!m_brushSettingsWidgets.isEmpty()) {
        BrushSettingsData emptySettings;
        for (auto* sectionWidget : m_brushSettingsWidgets) {
            if (!sectionWidget) {
                continue;
            }
            sectionWidget->setSettings(emptySettings);
            sectionWidget->setEnabled(false);
        }
    }
    updateBrushEditorButtonState();
}

void BrushPresetPage::setSelectedBrushLocally(const QString& brushId)
{
    if (brushId.isEmpty()) {
        clearSelectedBrushLocally();
        return;
    }

    if (!hasBrush(brushId)) {
        clearSelectedBrushLocally();
        return;
    }

    if (m_selectedBrushId == brushId) {
        return;
    }
    m_selectedBrushId = brushId;

    for (auto it = m_brushItems.begin(); it != m_brushItems.end(); ++it) {
        if (it.value()) {
            it.value()->setSelected(it.key() == brushId);
        }
    }
    rebuildSettingsWidget();
    if (!m_brushSettingsWidgets.isEmpty()) {
        const BrushSettingsData current = selectedBrushSettings();
        for (auto* sectionWidget : m_brushSettingsWidgets) {
            if (!sectionWidget) {
                continue;
            }
            sectionWidget->setSettings(current);
            sectionWidget->setEnabled(true);
        }
    }
    updateBrushEditorButtonState();
}

bool BrushPresetPage::hasBrush(const QString& brushId) const
{
    if (brushId.isEmpty()) {
        return false;
    }

    for (const auto& brush : m_brushes) {
        if (brush.id == brushId) {
            return true;
        }
    }

    return false;
}

QString BrushPresetPage::selectedBrushName() const
{
    if (m_selectedBrushId.isEmpty()) {
        return {};
    }

    for (const auto& brush : m_brushes) {
        if (brush.id == m_selectedBrushId) {
            return brush.name;
        }
    }
    return {};
}

BrushSettingsData BrushPresetPage::selectedBrushSettings() const
{
    if (m_selectedBrushId.isEmpty()) {
        return {};
    }

    for (const auto& brush : m_brushes) {
        if (brush.id == m_selectedBrushId) {
            return brush.settings;
        }
    }
    return {};
}

void BrushPresetPage::updateBrushEditorButtonState()
{
    if (!m_openBrushEditorButton) {
        return;
    }
    m_openBrushEditorButton->setEnabled(!selectedBrushName().isEmpty());
    for (auto* sectionWidget : m_brushSettingsWidgets) {
        if (sectionWidget) {
            sectionWidget->setEnabled(!m_selectedBrushId.isEmpty());
        }
    }
}

void BrushPresetPage::openBrushEditor()
{
    const QString brushName = selectedBrushName();
    if (brushName.isEmpty() || m_selectedBrushId.isEmpty()) {
        return;
    }

    // The page does not own the editor window — its lifetime is tied to the
    // pack, which can be deleted while the editor is open. Defer to the panel,
    // which owns a single, session-lived window.
    emit openEditorRequested(m_presetId, m_selectedBrushId, brushName);
}

void BrushPresetPage::addBrushItemWidget(const BrushData& brush)
{
    if (m_brushItems.contains(brush.id)) {
        if (BrushItem* existing = m_brushItems.value(brush.id)) {
            existing->setName(brush.name);
        }
        return;
    }

    auto* item = new BrushItem(brush, m_brushListWidget);
    item->setSelected(brush.id == m_selectedBrushId);

    connect(item, &BrushItem::clicked, this,
        [this](const QString& id) { emit brushSelectionRequested(id); });
    connect(item, &BrushItem::nameEdited, this,
        [this](const QString& id, const QString& newName) { emit brushNameEdited(id, newName); });

    int insertIndex = m_brushListLayout->count();
    if (insertIndex > 0 && !m_brushListLayout->itemAt(insertIndex - 1)->widget()) {
        --insertIndex;
    }
    m_brushListLayout->insertWidget(insertIndex, item);
    m_brushItems.insert(brush.id, item);
}

void BrushPresetPage::refreshBrushListGeometry()
{
    if (!m_brushListLayout || !m_brushListWidget || !m_scrollArea) {
        return;
    }

    m_brushListLayout->invalidate();
    m_brushListLayout->activate();
    m_brushListWidget->adjustSize();
    m_brushListWidget->updateGeometry();
    QMetaObject::invokeMethod(m_scrollArea, "updateScrollRange", Qt::QueuedConnection);

    // Run one more pass on next tick, when parent/viewport geometry is finalized.
    QTimer::singleShot(0, this, [this]() {
        if (!m_brushListLayout || !m_brushListWidget || !m_scrollArea) {
            return;
        }
        m_brushListLayout->invalidate();
        m_brushListLayout->activate();
        m_brushListWidget->adjustSize();
        m_brushListWidget->updateGeometry();
        QMetaObject::invokeMethod(m_scrollArea, "updateScrollRange", Qt::QueuedConnection);
    });
}

void BrushPresetPage::rebuildBrushList()
{
    m_brushItems.clear();

    // Full clear and safe re-create.
    while (QLayoutItem* child = m_brushListLayout->takeAt(0)) {
        if (QWidget* w = child->widget()) {
            detachAndDeleteWidget(w);
        }
        delete child;
    }

    // Add fresh items and trailing stretch.
    for (const auto& brush : m_brushes) {
        addBrushItemWidget(brush);
    }
    m_brushListLayout->addStretch();
    updateBrushEditorButtonState();
    refreshBrushListGeometry();
}

void BrushPresetPage::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
}

void BrushPresetPage::syncLayoutNow()
{
    refreshBrushListGeometry();
}

void BrushPresetPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    refreshBrushListGeometry();
}

void BrushPresetPage::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refreshBrushListGeometry();
}

// ============================================================================
// BrushPackPanel
// ============================================================================

BrushPackPanel::BrushPackPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupAnimations();
    connectSignals();
    updateSize();

    // Start hidden
    QWidget::hide();
    m_showProgress = 0.0;

    loadDataFromManager();
}

BrushPackPanel::~BrushPackPanel() = default;

void BrushPackPanel::setupUI()
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoMousePropagation, true);
    setAutoFillBackground(false);
    setMouseTracking(true);

    auto& theme = ThemeManager::instance();

    // Opacity effect for show/hide
    auto* opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(0.0);
    setGraphicsEffect(opacityEffect);

    // Main horizontal layout (top margin for handle)
    m_mainLayout = new QHBoxLayout(this);
    m_mainLayout->setContentsMargins(
        0, theme.scaled(Padding) + theme.scaled(BaseHandleHeight) + theme.scaled(4), 0, 0);
    m_mainLayout->setSpacing(0);

    // === Left sidebar ===
    m_sidebarContainer = new QWidget(this);
    m_sidebarContainer->setFixedWidth(theme.scaled(SidebarWidth));
    m_sidebarContainer->setAttribute(Qt::WA_TranslucentBackground);

    m_sidebarScrollArea = new SmoothScrollArea(m_sidebarContainer);
    m_sidebarScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_sidebarScrollArea->setContentWidthFixedToViewport(false);
    m_sidebarScrollArea->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarScrollArea->setAutoFillBackground(false);
    m_sidebarScrollArea->setStyleSheet("background: transparent;");

    m_sidebarContent = new QWidget();
    m_sidebarContent->setAttribute(Qt::WA_TranslucentBackground);
    m_sidebarLayout = new QVBoxLayout(m_sidebarContent);
    m_sidebarLayout->setContentsMargins(
        theme.scaled(3), theme.scaled(Padding), theme.scaled(3), theme.scaled(5));
    m_sidebarLayout->setSpacing(theme.scaled(4));
    m_sidebarLayout->setAlignment(Qt::AlignTop);
    m_sidebarLayout->addStretch();

    m_sidebarScrollArea->setWidget(m_sidebarContent);

    QVBoxLayout* sidebarContLayout = new QVBoxLayout(m_sidebarContainer);
    sidebarContLayout->setContentsMargins(0, 0, 0, 0);
    sidebarContLayout->setSpacing(theme.scaled(4));
    addControlButtons();
    sidebarContLayout->addLayout(m_sidebarControlsLayout);
    sidebarContLayout->addWidget(m_sidebarScrollArea);

    m_mainLayout->addWidget(m_sidebarContainer);

    // === Right side ===
    m_rightContainer = new QWidget(this);
    m_rightContainer->setAttribute(Qt::WA_TranslucentBackground);
    m_rightContainer->setAutoFillBackground(false);
    auto* rightLayout = new QVBoxLayout(m_rightContainer);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(theme.scaled(4));
    rightLayout->addLayout(m_brushControlsLayout);

    m_stackedWidget = new AnimatedStackedWidget(m_rightContainer);
    m_stackedWidget->setAnimationDuration(250);
    m_stackedWidget->setAnimationEasing(QEasingCurve::InOutCubic);
    m_stackedWidget->setAttribute(Qt::WA_TranslucentBackground);
    rightLayout->addWidget(m_stackedWidget, 1);

    m_mainLayout->addWidget(m_rightContainer, 1);
    applyRightSectionColors();
}

void BrushPackPanel::setupAnimations()
{
    m_showAnimation = new QPropertyAnimation(this, "showProgress", this);
    m_showAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    connect(m_showAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            onHideAnimationFinished();
        } else if (m_isShowing) {
            onShowAnimationFinished();
        }
    });

    m_compactAnimation = new QPropertyAnimation(this, "compactProgress", this);
    m_compactAnimation->setDuration(CompactDuration);
    m_compactAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_compactAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (m_dragMode == DragMode::Widget) {
            m_widgetStartPos = pos();
            m_dragStartPos = QCursor::pos();
        }
    });
}

void BrushPackPanel::connectSignals()
{
    // Visibility-gated: onThemeChanged() rebuilds the preset sidebar (heavy).
    // Deferred for hidden (background-tab) instances; flushed on activation via
    // WorkspaceTab::onApplyThemeRefresh -> ThemeManager::flushThemeHandlers().
    ThemeManager::instance().registerThemeHandler(this, [this]() { onThemeChanged(); });
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
        []() { ruwa::core::brushes::BrushPreviewManager::instance().invalidateCache(); });
    connect(m_addPresetButton, &QPushButton::clicked, this, &BrushPackPanel::onAddPresetClicked);
    connect(
        m_removePresetButton, &QPushButton::clicked, this, &BrushPackPanel::onRemovePresetClicked);
    connect(m_addBrushButton, &QPushButton::clicked, this, &BrushPackPanel::onAddBrushClicked);
    connect(
        m_removeBrushButton, &QPushButton::clicked, this, &BrushPackPanel::onRemoveBrushClicked);

    // Connect to BrushManager signals for cross-widget sync
    auto& manager = BrushManager::instance();
    connect(&manager, &BrushManager::presetCreated, this, &BrushPackPanel::onManagerPresetCreated);
    connect(&manager, &BrushManager::presetRemoved, this, &BrushPackPanel::onManagerPresetRemoved);
    connect(&manager, &BrushManager::presetRenamed, this, &BrushPackPanel::onManagerPresetRenamed);
    connect(&manager, &BrushManager::brushCreated, this, &BrushPackPanel::onManagerBrushCreated);
    connect(&manager, &BrushManager::brushRemoved, this, &BrushPackPanel::onManagerBrushRemoved);
    connect(&manager, &BrushManager::brushRenamed, this, &BrushPackPanel::onManagerBrushRenamed);
    connect(&manager, &BrushManager::brushSettingsUpdated, this,
        &BrushPackPanel::onManagerBrushSettingsUpdated);
    connect(&manager, &BrushManager::dataReset, this, &BrushPackPanel::loadDataFromManager);
    connect(m_stackedWidget, &AnimatedStackedWidget::currentChanged, this, [this](int) {
        if (!m_pageSelectionSyncDeferred) {
            return;
        }
        m_pageSelectionSyncDeferred = false;
        syncPageSelectionsAfterTransition();
    });
    connect(&ruwa::ui::core::TranslationManager::instance(),
        &ruwa::ui::core::TranslationManager::languageChanged, this, [this]() {
            for (PresetButton* btn : m_presetButtons) {
                if (btn)
                    btn->update();
            }
            for (BrushPresetPage* page : m_pages) {
                if (page)
                    page->update();
            }
        });
}

QSize BrushPackPanel::fullPanelSize() const
{
    auto& theme = ThemeManager::instance();
    return QSize(theme.scaled(PanelWidth), theme.scaled(PanelHeight));
}

void BrushPackPanel::updateSize()
{
    applyCompactLayout();
}

void BrushPackPanel::setCompactProgress(qreal progress)
{
    if (qFuzzyCompare(m_compactProgress, progress))
        return;
    m_compactProgress = qBound(0.0, progress, 1.0);
    applyCompactLayout();
}

void BrushPackPanel::setCompactMode(bool compact, bool animate)
{
    const qreal target = compact ? 1.0 : 0.0;
    if (qFuzzyCompare(m_compactProgress, target))
        return;

    if (!animate || !m_compactAnimation) {
        setCompactProgress(target);
        return;
    }

    m_compactAnimation->stop();
    m_compactAnimation->setStartValue(m_compactProgress);
    m_compactAnimation->setEndValue(target);
    m_compactAnimation->start();
}

void BrushPackPanel::applyCompactLayout()
{
    auto& theme = ThemeManager::instance();
    const int fullW = theme.scaled(PanelWidth);
    const int fullH = theme.scaled(PanelHeight);
    const int fullSidebarW = theme.scaled(SidebarWidth);

    const int compactH = static_cast<int>(fullH * CompactHeightFactor);
    const int compactRightW = static_cast<int>((fullW - fullSidebarW) * CompactRightWidthFactor);
    const int compactW = fullSidebarW + compactRightW;

    const int w = static_cast<int>(fullW + (compactW - fullW) * m_compactProgress);
    const int h = static_cast<int>(fullH + (compactH - fullH) * m_compactProgress);

    setFixedSize(w, h);
    m_sidebarContainer->setFixedWidth(fullSidebarW);
    if (m_sidebarContent) {
        m_sidebarContent->setFixedWidth(fullSidebarW);
    }

    // During compact animation while dragging: keep panel center X under cursor.
    // Only when animation is running (resize in progress); handleDrag handles mouse moves.
    if (m_dragMode == DragMode::Widget && m_compactProgress > 0.001 && parentWidget()
        && m_compactAnimation && m_compactAnimation->state() == QAbstractAnimation::Running) {
        const QPoint cursorInParent = parentWidget()->mapFromGlobal(QCursor::pos());
        constexpr int margin = 6;
        const int maxX = parentWidget()->width() - width() - margin;
        const int newX = qBound(margin, cursorInParent.x() - width() / 2, qMax(margin, maxX));
        if (newX != x()) {
            move(newX, y());
            emit positionChanged(pos());
        }
    }
}

void BrushPackPanel::addControlButtons()
{
    auto& theme = ThemeManager::instance();

    m_sidebarControlsLayout = new QHBoxLayout();
    m_sidebarControlsLayout->setContentsMargins(
        theme.scaled(3), theme.scaled(5), theme.scaled(3), 0);
    m_sidebarControlsLayout->setSpacing(theme.scaled(3));

    m_addPresetButton = new QPushButton(tr("+ Pack"), m_sidebarContainer);
    m_removePresetButton = new QPushButton(tr("- Pack"), m_sidebarContainer);
    m_sidebarControlsLayout->addWidget(m_addPresetButton);
    m_sidebarControlsLayout->addWidget(m_removePresetButton);

    m_brushControlsLayout = new QHBoxLayout();
    m_brushControlsLayout->setContentsMargins(
        theme.scaled(10), theme.scaled(6), theme.scaled(10), 0);
    m_brushControlsLayout->setSpacing(theme.scaled(6));
    m_brushControlsLayout->addStretch();

    m_addBrushButton = new QPushButton(tr("+ Brush"), this);
    m_removeBrushButton = new QPushButton(tr("- Brush"), this);
    m_brushControlsLayout->addWidget(m_addBrushButton);
    m_brushControlsLayout->addWidget(m_removeBrushButton);

    const QString buttonStyle = "QPushButton {"
                                "  background: rgba(120,120,120,0.14);"
                                "  border: 1px solid rgba(255,255,255,0.18);"
                                "  border-radius: 5px;"
                                "  padding: 4px 8px;"
                                "}"
                                "QPushButton:hover { background: rgba(120,120,120,0.22); }"
                                "QPushButton:pressed { background: rgba(120,120,120,0.30); }"
                                "QPushButton:disabled { color: rgba(180,180,180,0.5); "
                                "border-color: rgba(255,255,255,0.1); }";

    const QList<QPushButton*> buttons
        = { m_addPresetButton, m_removePresetButton, m_addBrushButton, m_removeBrushButton };
    for (QPushButton* button : buttons) {
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(buttonStyle);
        button->setFixedHeight(theme.scaled(24));
    }

    // CRUD operations for packs/brushes are now centralized in BrushEditorWindow.
    m_addPresetButton->setVisible(false);
    m_removePresetButton->setVisible(false);
    m_addBrushButton->setVisible(false);
    m_removeBrushButton->setVisible(false);
}

void BrushPackPanel::loadDataFromManager()
{
    const QString preservedPresetId = m_selectedPresetId;
    const QString preservedBrushId = m_selectedBrushId;
    clearPresets();

    auto& manager = BrushManager::instance();
    const auto allPresets = manager.presets();
    for (const auto& preset : allPresets) {
        addPreset(preset);
        syncPresetPageFromManager(preset.id);
    }

    const QString preservedBrushPresetId
        = preservedBrushId.isEmpty() ? QString() : manager.presetIdForBrush(preservedBrushId);
    QString resolvedPresetId = firstPresetId();
    QString resolvedBrushId;

    if (!preservedBrushPresetId.isEmpty()) {
        resolvedPresetId = preservedBrushPresetId;
        resolvedBrushId = preservedBrushId;
    } else if (hasPreset(preservedPresetId)) {
        resolvedPresetId = preservedPresetId;
        resolvedBrushId = resolveBrushIdForPresetSelection(resolvedPresetId, QString(), true);
    } else {
        resolvedBrushId = resolveBrushIdForPresetSelection(resolvedPresetId, QString(), true);
    }

    applyCanonicalSelection(resolvedPresetId, resolvedBrushId, false);
}

void BrushPackPanel::updateControlButtonsState()
{
    const bool hasPreset = !m_selectedPresetId.isEmpty();
    const bool hasBrush = !selectedBrushId().isEmpty();

    if (m_removePresetButton)
        m_removePresetButton->setEnabled(hasPreset);
    if (m_addBrushButton)
        m_addBrushButton->setEnabled(hasPreset);
    if (m_removeBrushButton)
        m_removeBrushButton->setEnabled(hasBrush);
}

void BrushPackPanel::applyRightSectionColors()
{
    if (!m_rightContainer)
        return;

    // Keep right section transparent so panel-level rounded background/border
    // remains visible on the full overlay, including stacked widget area.
    m_rightContainer->setStyleSheet("background: transparent; border: none;");

    if (m_stackedWidget) {
        m_stackedWidget->setStyleSheet("background: transparent; border: none;");
    }
}

bool BrushPackPanel::hasPreset(const QString& presetId) const
{
    if (presetId.isEmpty()) {
        return false;
    }

    for (const auto& preset : m_presets) {
        if (preset.id == presetId) {
            return true;
        }
    }

    return false;
}

QString BrushPackPanel::firstPresetId() const
{
    return m_presets.isEmpty() ? QString() : m_presets.first().id;
}

void BrushPackPanel::applyCanonicalSelection(
    const QString& presetId, const QString& brushId, bool emitPresetSignal)
{
    const bool presetChanged = (m_selectedPresetId != presetId);
    m_selectedPresetId = presetId;
    m_selectedBrushId = brushId;

    for (auto it = m_presetButtons.begin(); it != m_presetButtons.end(); ++it) {
        if (it.value()) {
            it.value()->setSelected(it.key() == m_selectedPresetId);
        }
    }

    BrushPresetPage* targetPage = pageForPreset(m_selectedPresetId);
    if (presetChanged && targetPage && m_stackedWidget) {
        m_pageSelectionSyncDeferred = true;
        m_stackedWidget->setCurrentWidget(targetPage);
    } else {
        syncStackedWidgetToSelectedPreset();
    }
    updateControlButtonsState();

    if (emitPresetSignal && presetChanged && !m_selectedPresetId.isEmpty()) {
        emit presetSelected(m_selectedPresetId);
    }
}

void BrushPackPanel::normalizeCanonicalSelection(bool emitPresetSignal)
{
    const QString resolvedPresetId
        = hasPreset(m_selectedPresetId) ? m_selectedPresetId : firstPresetId();
    const QString resolvedBrushId
        = resolveBrushIdForPresetSelection(resolvedPresetId, m_selectedBrushId, true);
    applyCanonicalSelection(resolvedPresetId, resolvedBrushId, emitPresetSignal);
}

void BrushPackPanel::propagateCanonicalSelection(bool emitBrushSignal)
{
    const QString editorPresetId = m_selectedBrushId.isEmpty()
        ? QString()
        : BrushManager::instance().presetIdForBrush(m_selectedBrushId);
    const QString brushName = brushNameForSelection(editorPresetId, m_selectedBrushId);

    for (auto it = m_pages.begin(); it != m_pages.end(); ++it) {
        BrushPresetPage* page = it.value();
        if (!page) {
            continue;
        }

        if (!editorPresetId.isEmpty() && it.key() == editorPresetId) {
            page->setSelectedBrushLocally(m_selectedBrushId);
        } else {
            page->clearSelectedBrushLocally();
        }
    }

    syncDetachedEditorSelection(editorPresetId, m_selectedBrushId, brushName);

    updateControlButtonsState();

    if (emitBrushSignal && !m_selectedBrushId.isEmpty()) {
        emit brushSelectionRequested(m_selectedBrushId);
    }
}

void BrushPackPanel::syncPageSelectionsAfterTransition()
{
    propagateCanonicalSelection(false);
}

void BrushPackPanel::syncStackedWidgetToSelectedPreset()
{
    if (!m_stackedWidget) {
        return;
    }

    BrushPresetPage* targetPage = pageForPreset(m_selectedPresetId);
    if (!targetPage) {
        if (m_stackedWidget->count() > 0) {
            m_stackedWidget->setCurrentIndexWithoutAnimation(0);
        }
        m_pageSelectionSyncDeferred = false;
        syncPageSelectionsAfterTransition();
        return;
    }

    const int targetIndex = m_stackedWidget->indexOf(targetPage);
    if (targetIndex < 0) {
        return;
    }

    // If AnimatedStackedWidget is already transitioning to this page, do not
    // force a no-animation jump just because canonical selection was echoed
    // back through CanvasPanel during the slide.
    if (m_pageSelectionSyncDeferred && m_stackedWidget->activeIndex() == targetIndex
        && m_stackedWidget->currentWidget() != targetPage) {
        propagateCanonicalSelection(false);
        return;
    }

    if (m_stackedWidget->currentWidget() != targetPage
        || m_stackedWidget->activeIndex() != targetIndex) {
        m_stackedWidget->setCurrentIndexWithoutAnimation(targetIndex);
    }

    m_pageSelectionSyncDeferred = false;
    syncPageSelectionsAfterTransition();
}

// ============================================================================
// Preset management
// ============================================================================

void BrushPackPanel::addPreset(const BrushPresetData& preset)
{
    for (const auto& existing : m_presets) {
        if (existing.id == preset.id) {
            return;
        }
    }

    m_presets.append(preset);

    // Ensure page exists
    ensurePage(preset.id);

    addPresetButton(preset);
    refreshSidebarGeometry();

    updateControlButtonsState();
}

void BrushPackPanel::removePreset(const QString& id)
{
    bool removed = false;
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].id == id) {
            m_presets.removeAt(i);
            removed = true;
            break;
        }
    }
    if (!removed) {
        return;
    }

    removePresetButton(id);

    // Remove page
    if (m_pages.contains(id)) {
        BrushPresetPage* page = m_pages.take(id);
        const int pageIndex = m_stackedWidget ? m_stackedWidget->indexOf(page) : -1;
        if (m_stackedWidget && pageIndex >= 0 && m_stackedWidget->count() > 1) {
            int safeIndex = -1;
            if (BrushPresetPage* selectedPage = pageForPreset(m_selectedPresetId)) {
                const int selectedIndex = m_stackedWidget->indexOf(selectedPage);
                if (selectedIndex >= 0 && selectedIndex != pageIndex) {
                    safeIndex = selectedIndex;
                }
            }
            if (safeIndex < 0) {
                safeIndex = (pageIndex == 0) ? 1 : 0;
            }
            m_stackedWidget->setCurrentIndexWithoutAnimation(safeIndex);
        }
        m_stackedWidget->removeWidget(page);
        detachAndDeleteWidget(page);
    }

    const bool removedSelectedPreset = (m_selectedPresetId == id);
    if (removedSelectedPreset) {
        m_selectedPresetId.clear();
    }
    if (!m_selectedBrushId.isEmpty()
        && BrushManager::instance().presetIdForBrush(m_selectedBrushId).isEmpty()) {
        m_selectedBrushId.clear();
    }

    normalizeCanonicalSelection(removedSelectedPreset);

    refreshSidebarGeometry();
    updateControlButtonsState();
}

void BrushPackPanel::clearPresets()
{
    for (auto it = m_presetButtons.begin(); it != m_presetButtons.end(); ++it) {
        PresetButton* button = it.value();
        if (button) {
            detachAndDeleteWidget(button, m_sidebarLayout);
        }
    }
    m_presetButtons.clear();

    m_presets.clear();
    m_selectedPresetId.clear();
    m_selectedBrushId.clear();

    for (auto* page : m_pages) {
        m_stackedWidget->removeWidget(page);
        detachAndDeleteWidget(page);
    }
    m_pages.clear();

    refreshSidebarGeometry();
    updateControlButtonsState();
}

void BrushPackPanel::selectPreset(const QString& id)
{
    const QString resolvedPresetId = hasPreset(id) ? id : firstPresetId();
    const QString resolvedBrushId
        = resolveBrushIdForPresetSelection(resolvedPresetId, QString(), true);
    applyCanonicalSelection(resolvedPresetId, resolvedBrushId, true);
}

void BrushPackPanel::rebuildPresetSidebar()
{
    // Full clear to rebuild with current theme metrics.
    m_presetButtons.clear();
    while (QLayoutItem* child = m_sidebarLayout->takeAt(0)) {
        if (QWidget* w = child->widget()) {
            detachAndDeleteWidget(w);
        }
        delete child;
    }

    for (const auto& preset : m_presets) {
        addPresetButton(preset);
    }
    m_sidebarLayout->addStretch();
    refreshSidebarGeometry();
    updateControlButtonsState();
}

PresetButton* BrushPackPanel::createPresetButton(const BrushPresetData& preset)
{
    auto* btn = new PresetButton(preset, m_sidebarContent);
    btn->setSelected(preset.id == m_selectedPresetId);

    connect(btn, &PresetButton::clicked, this, [this, id = preset.id]() { onPresetClicked(id); });
    connect(btn, &PresetButton::nameEdited, this, &BrushPackPanel::onPresetNameEdited);
    return btn;
}

void BrushPackPanel::addPresetButton(const BrushPresetData& preset)
{
    if (m_presetButtons.contains(preset.id)) {
        if (PresetButton* existing = m_presetButtons.value(preset.id)) {
            existing->setName(preset.name);
        }
        return;
    }

    PresetButton* button = createPresetButton(preset);
    int insertIndex = m_sidebarLayout->count();
    if (insertIndex > 0 && !m_sidebarLayout->itemAt(insertIndex - 1)->widget()) {
        --insertIndex;
    }
    m_sidebarLayout->insertWidget(insertIndex, button);
    m_presetButtons.insert(preset.id, button);
}

void BrushPackPanel::removePresetButton(const QString& presetId)
{
    PresetButton* button = m_presetButtons.take(presetId);
    if (!button) {
        return;
    }
    detachAndDeleteWidget(button, m_sidebarLayout);
}

void BrushPackPanel::refreshSidebarGeometry()
{
    if (!m_sidebarLayout || !m_sidebarContent || !m_sidebarScrollArea) {
        return;
    }

    m_sidebarLayout->invalidate();
    m_sidebarLayout->activate();
    m_sidebarContent->adjustSize();
    m_sidebarContent->updateGeometry();
    QMetaObject::invokeMethod(m_sidebarScrollArea, "updateScrollRange", Qt::QueuedConnection);

    // Second pass after pending layout updates.
    QTimer::singleShot(0, this, [this]() {
        if (!m_sidebarLayout || !m_sidebarContent || !m_sidebarScrollArea) {
            return;
        }
        m_sidebarLayout->invalidate();
        m_sidebarLayout->activate();
        m_sidebarContent->adjustSize();
        m_sidebarContent->updateGeometry();
        QMetaObject::invokeMethod(m_sidebarScrollArea, "updateScrollRange", Qt::QueuedConnection);
    });
}

void BrushPackPanel::onPresetClicked(const QString& presetId)
{
    const QString previousBrushId = m_selectedBrushId;
    selectPreset(presetId);
    if (!m_selectedBrushId.isEmpty() && m_selectedBrushId != previousBrushId) {
        emit brushSelectionRequested(m_selectedBrushId);
    }
}

void BrushPackPanel::onAddPresetClicked()
{
    auto& manager = BrushManager::instance();
    const QString newPresetId = manager.createPreset();
    const auto allPresets = manager.presets();
    for (const auto& preset : allPresets) {
        if (preset.id == newPresetId) {
            addPreset(preset);
            selectPreset(newPresetId);
            break;
        }
    }
    updateControlButtonsState();
}

void BrushPackPanel::onRemovePresetClicked()
{
    if (m_selectedPresetId.isEmpty())
        return;
    if (BrushManager::instance().removePreset(m_selectedPresetId)) {
        removePreset(m_selectedPresetId);
    }
    updateControlButtonsState();
}

void BrushPackPanel::onAddBrushClicked()
{
    if (m_selectedPresetId.isEmpty())
        return;

    auto& manager = BrushManager::instance();
    const QString brushId = manager.createBrush(m_selectedPresetId);
    if (!brushId.isEmpty()) {
        syncPresetPageFromManager(m_selectedPresetId);
        selectBrush(brushId);
    }
    updateControlButtonsState();
}

void BrushPackPanel::onRemoveBrushClicked()
{
    const QString brushId = selectedBrushId();
    if (brushId.isEmpty())
        return;

    BrushManager::instance().removeBrush(brushId);
    updateControlButtonsState();
}

void BrushPackPanel::onPresetNameEdited(const QString& presetId, const QString& newName)
{
    if (!BrushManager::instance().renamePreset(presetId, newName)) {
        return;
    }
    for (auto& preset : m_presets) {
        if (preset.id == presetId) {
            preset.name = newName;
            break;
        }
    }
}

void BrushPackPanel::onBrushNameEdited(const QString& brushId, const QString& newName)
{
    BrushManager::instance().renameBrush(brushId, newName);
}

void BrushPackPanel::onBrushSettingsEdited(
    const QString& brushId, const BrushSettingsData& settings)
{
    BrushManager::instance().updateBrushSettings(brushId, settings);
}

// ============================================================================
// BrushManager signal handlers (cross-widget sync)
// ============================================================================

void BrushPackPanel::onManagerPresetCreated(const QString& presetId)
{
    // Check if we already have this preset (avoid duplicates from own operations)
    for (const auto& existing : m_presets) {
        if (existing.id == presetId) {
            return;
        }
    }

    auto& manager = BrushManager::instance();
    const auto allPresets = manager.presets();
    for (const auto& preset : allPresets) {
        if (preset.id == presetId) {
            addPreset(preset);
            break;
        }
    }
    normalizeCanonicalSelection(false);
}

void BrushPackPanel::onManagerPresetRemoved(const QString& presetId)
{
    // Check if we have this preset
    bool found = false;
    for (const auto& existing : m_presets) {
        if (existing.id == presetId) {
            found = true;
            break;
        }
    }
    if (!found) {
        return;
    }

    removePreset(presetId);
    updateControlButtonsState();
}

void BrushPackPanel::onManagerPresetRenamed(const QString& presetId, const QString& newName)
{
    for (auto& preset : m_presets) {
        if (preset.id == presetId) {
            preset.name = newName;
            break;
        }
    }
    if (PresetButton* btn = m_presetButtons.value(presetId)) {
        btn->setName(newName);
    }
}

void BrushPackPanel::onManagerBrushCreated(const QString& presetId, const QString& brushId)
{
    Q_UNUSED(brushId);
    syncPresetPageFromManager(presetId);
    normalizeCanonicalSelection(false);
}

void BrushPackPanel::onManagerBrushRemoved(const QString& presetId, const QString& brushId)
{
    removeBrushFromPreset(presetId, brushId);
}

void BrushPackPanel::onManagerBrushRenamed(const QString& brushId, const QString& newName)
{
    const QString presetId = BrushManager::instance().presetIdForBrush(brushId);
    if (!presetId.isEmpty()) {
        // A rename only changes one row's label — update it in place instead of
        // rebuilding the whole preset page (setBrushes() recreates every row and
        // preview, which makes name editing feel laggy). Fall back to a full
        // sync only if the row isn't present yet.
        BrushPresetPage* page = pageForPreset(presetId);
        if (!page || !page->updateBrushNameFromManager(brushId, newName)) {
            syncPresetPageFromManager(presetId);
        }
    }
    propagateCanonicalSelection(false);
}

void BrushPackPanel::onManagerBrushSettingsUpdated(
    const QString& presetId, const QString& brushId, const BrushSettingsData& settings)
{
    BrushPresetPage* page = pageForPreset(presetId);
    if (page) {
        page->updateBrushSettingsFromManager(brushId, settings);
    }
}

// ============================================================================
// Brush management
// ============================================================================

void BrushPackPanel::addBrush(const BrushData& brush)
{
    if (brush.presetId.isEmpty()) {
        return;
    }

    syncPresetPageFromManager(brush.presetId);
    normalizeCanonicalSelection(false);
}

void BrushPackPanel::removeBrush(const QString& brushId)
{
    removeBrushFromPreset(BrushManager::instance().presetIdForBrush(brushId), brushId);
}

void BrushPackPanel::removeBrushFromPreset(const QString& presetId, const QString& brushId)
{
    if (!presetId.isEmpty()) {
        syncPresetPageFromManager(presetId);
    }

    if (m_selectedBrushId == brushId) {
        m_selectedBrushId.clear();
        if (!presetId.isEmpty()) {
            m_selectedPresetId = presetId;
        }
    }

    normalizeCanonicalSelection(false);
}

void BrushPackPanel::selectBrush(const QString& brushId)
{
    const QString resolvedPresetId
        = brushId.isEmpty() ? QString() : BrushManager::instance().presetIdForBrush(brushId);
    QString targetPresetId = resolvedPresetId;
    QString targetBrushId;

    if (!resolvedPresetId.isEmpty()) {
        targetBrushId = resolveBrushIdForPresetSelection(resolvedPresetId, brushId, false);
    } else if (hasPreset(m_selectedPresetId)) {
        targetPresetId = m_selectedPresetId;
    } else {
        targetPresetId = firstPresetId();
    }

    const bool emitPresetSignal
        = (m_selectedPresetId != targetPresetId) && !targetPresetId.isEmpty();
    applyCanonicalSelection(targetPresetId, targetBrushId, emitPresetSignal);

    // (Otherwise the canvas briefly — or finally, if selection is already brushId — applies the
    // wrong brush.)
}

QString BrushPackPanel::brushNameForSelection(const QString& presetId, const QString& brushId) const
{
    if (presetId.isEmpty() || brushId.isEmpty()) {
        return {};
    }

    const auto brushes = BrushManager::instance().brushesForPreset(presetId);
    for (const auto& brush : brushes) {
        if (brush.id == brushId) {
            return brush.name;
        }
    }

    return {};
}

QString BrushPackPanel::resolveBrushIdForPresetSelection(
    const QString& presetId, const QString& preferredBrushId, bool allowFirstBrushFallback) const
{
    if (presetId.isEmpty()) {
        return {};
    }

    const QVector<BrushData> brushes = BrushManager::instance().brushesForPreset(presetId);

    auto containsBrush = [&brushes](const QString& brushId) {
        if (brushId.isEmpty()) {
            return false;
        }

        for (const auto& brush : brushes) {
            if (brush.id == brushId) {
                return true;
            }
        }
        return false;
    };

    if (containsBrush(preferredBrushId)) {
        return preferredBrushId;
    }
    if (allowFirstBrushFallback && !brushes.isEmpty()) {
        return brushes.first().id;
    }

    return {};
}

QString BrushPackPanel::selectedBrushId() const
{
    return m_selectedBrushId;
}

BrushSettingsData BrushPackPanel::selectedBrushSettings() const
{
    if (m_selectedBrushId.isEmpty()) {
        return {};
    }
    if (const auto settings = BrushManager::instance().brushSettings(m_selectedBrushId)) {
        return *settings;
    }
    return {};
}

void BrushPackPanel::applySettingsToSelectedBrush(const BrushSettingsData& settings)
{
    const QString brushId = selectedBrushId();
    if (brushId.isEmpty()) {
        return;
    }

    BrushManager::instance().updateBrushSettings(brushId, settings);
}

// ============================================================================
// Page management
// ============================================================================

BrushPresetPage* BrushPackPanel::pageForPreset(const QString& presetId) const
{
    return m_pages.value(presetId, nullptr);
}

bool BrushPackPanel::ownsAuxiliaryWidget(const QWidget* widget) const
{
    if (!widget || !m_brushEditorWindow) {
        return false;
    }

    return widget == m_brushEditorWindow
        || m_brushEditorWindow->isAncestorOf(const_cast<QWidget*>(widget));
}

void BrushPackPanel::openBrushEditor(
    const QString& presetId, const QString& brushId, const QString& brushName)
{
    if (presetId.isEmpty() || brushId.isEmpty() || brushName.isEmpty()) {
        return;
    }

    if (!m_brushEditorWindow) {
        // Parent the window to the panel, not to a per-pack page: pages are
        // destroyed when their pack is removed, and a child window would be torn
        // down with them — even mid-edit. The panel lives for the whole session,
        // so the editor's lifetime is now decoupled from any single pack. This
        // is the fix for the intermittent crash on pack deletion.
        m_brushEditorWindow = new ruwa::ui::windows::BrushEditorWindow(this);
        connect(m_brushEditorWindow.data(),
            &ruwa::ui::windows::BrushEditorWindow::brushSelectionChanged, this,
            [this](const QString& /*presetId*/, const QString& editorBrushId) {
                if (editorBrushId.isEmpty()) {
                    return;
                }
                selectBrush(editorBrushId);
                if (!m_selectedBrushId.isEmpty()) {
                    emit brushSelectionRequested(m_selectedBrushId);
                }
            });
    }

    m_brushEditorWindow->setSelection(presetId, brushId);
    m_brushEditorWindow->setBrushName(brushName);

    if (!m_brushEditorWindow->isVisible()) {
        QWidget* owner = window();
        QRect ownerRect = owner ? owner->geometry() : QRect();
        if (owner) {
            const QPoint globalTopLeft = owner->mapToGlobal(QPoint(0, 0));
            ownerRect.moveTopLeft(globalTopLeft);
        }

        const QSize wSize = m_brushEditorWindow->size();
        const QPoint centeredPos(ownerRect.center().x() - wSize.width() / 2,
            ownerRect.center().y() - wSize.height() / 2);
        m_brushEditorWindow->move(centeredPos);
        m_brushEditorWindow->show();
    }

    m_brushEditorWindow->raise();
    m_brushEditorWindow->activateWindow();
}

void BrushPackPanel::syncDetachedEditorSelection(
    const QString& presetId, const QString& brushId, const QString& brushName)
{
    if (!m_brushEditorWindow) {
        return;
    }

    QPointer<BrushPackPanel> self = this;
    QPointer<ruwa::ui::windows::BrushEditorWindow> editor = m_brushEditorWindow;
    QMetaObject::invokeMethod(
        editor.data(),
        [self, editor, presetId, brushId, brushName]() {
            if (!self || !editor) {
                return;
            }

            // Drop stale async updates: only push if this is still the live
            // selection by the time the queued call runs.
            if (self->m_selectedBrushId != brushId) {
                return;
            }

            if (!brushId.isEmpty()
                && BrushManager::instance().presetIdForBrush(brushId) != presetId) {
                return;
            }

            editor->setSelection(presetId, brushId);
            editor->setBrushName(brushName);
        },
        Qt::QueuedConnection);
}

void BrushPackPanel::syncPresetPageFromManager(const QString& presetId)
{
    if (presetId.isEmpty()) {
        return;
    }

    if (BrushPresetPage* page = ensurePage(presetId)) {
        page->setBrushes(BrushManager::instance().brushesForPreset(presetId));
        if (presetId == m_selectedPresetId) {
            page->setSelectedBrushLocally(m_selectedBrushId);
        } else {
            page->clearSelectedBrushLocally();
        }
    }
}

BrushPresetPage* BrushPackPanel::ensurePage(const QString& presetId)
{
    if (m_pages.contains(presetId)) {
        return m_pages[presetId];
    }

    auto* page = new BrushPresetPage(presetId, m_stackedWidget);
    m_stackedWidget->addWidget(page);
    m_pages[presetId] = page;

    connect(page, &BrushPresetPage::brushSelectionRequested, this, [this](const QString& brushId) {
        selectBrush(brushId);
        if (!m_selectedBrushId.isEmpty()) {
            emit brushSelectionRequested(m_selectedBrushId);
        }
    });
    connect(page, &BrushPresetPage::brushNameEdited, this, &BrushPackPanel::onBrushNameEdited);
    connect(
        page, &BrushPresetPage::brushSettingsEdited, this, &BrushPackPanel::onBrushSettingsEdited);
    connect(page, &BrushPresetPage::activeBrushSettingsChanged, this,
        &BrushPackPanel::activeBrushSettingsChanged);
    connect(page, &BrushPresetPage::openEditorRequested, this, &BrushPackPanel::openBrushEditor);

    return page;
}

// ============================================================================
// Show/Hide animation
// ============================================================================

void BrushPackPanel::showAnimated()
{
    if (m_isShowing && isVisible()) {
        return;
    }

    m_isShowing = true;
    m_isHiding = false;

    show();
    raise();
    setFocus();

    m_showAnimation->stop();
    m_showAnimation->setDuration(ShowDuration);
    m_showAnimation->setStartValue(m_showProgress);
    m_showAnimation->setEndValue(1.0);
    m_showAnimation->start();

    // Ensure all list layouts are synced after the panel becomes visible.
    refreshSidebarGeometry();
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (it.value()) {
            it.value()->syncLayoutNow();
        }
    }
}

void BrushPackPanel::hideAnimated()
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

bool BrushPackPanel::isActive() const
{
    return isVisible() && !m_isHiding;
}

void BrushPackPanel::setShowProgress(qreal progress)
{
    if (qFuzzyCompare(m_showProgress, progress))
        return;
    m_showProgress = progress;

    if (auto* effect = qobject_cast<QGraphicsOpacityEffect*>(graphicsEffect())) {
        effect->setOpacity(progress);
    }

    update();
}

void BrushPackPanel::onShowAnimationFinished()
{
    m_isShowing = false;
}

void BrushPackPanel::onHideAnimationFinished()
{
    m_isHiding = false;
    QWidget::hide();
}

void BrushPackPanel::onThemeChanged()
{
    updateSize();
    rebuildPresetSidebar();
    applyRightSectionColors();
    updateControlButtonsState();
    update();
}

// ============================================================================
// Drawing
// ============================================================================

void BrushPackPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_showProgress <= 0.001)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    drawBackground(painter);
    drawHandle(painter, handleRect());
    drawDivider(painter);
}

QRectF BrushPackPanel::handleRect() const
{
    auto& theme = ThemeManager::instance();
    int pad = theme.scaled(Padding);
    int handleH = theme.scaled(BaseHandleHeight);
    return QRectF(pad, pad, width() - 2 * pad, handleH);
}

BrushPackPanel::DragMode BrushPackPanel::hitTest(const QPoint& pos) const
{
    if (!m_userMovable)
        return DragMode::None;
    if (handleRect().contains(pos))
        return DragMode::Widget;
    return DragMode::None;
}

void BrushPackPanel::handleDrag(const QPoint& globalPos)
{
    if (m_dragMode != DragMode::Widget)
        return;

    QPoint delta = globalPos - m_dragStartPos;
    QPoint newPos = m_widgetStartPos + delta;

    if (parentWidget()) {
        constexpr int margin = 6;
        int maxX = parentWidget()->width() - width() - margin;
        int maxY = parentWidget()->height() - height() - margin;

        // Only center X during the compact animation (resize); after that, normal drag
        const bool compactAnimating
            = m_compactAnimation && m_compactAnimation->state() == QAbstractAnimation::Running;
        if (compactAnimating) {
            const QPoint cursorInParent = parentWidget()->mapFromGlobal(globalPos);
            newPos.setX(qBound(margin, cursorInParent.x() - width() / 2, qMax(margin, maxX)));
        }
        newPos.setX(qBound(margin, newPos.x(), maxX));
        newPos.setY(qBound(margin, newPos.y(), maxY));
    }

    if (newPos != pos()) {
        move(newPos);
        emit positionChanged(newPos);
    }
}

void BrushPackPanel::drawHandle(QPainter& painter, const QRectF& rect)
{
    auto& theme = ThemeManager::instance();
    auto& mgr = WidgetStyleManager::instance();

    QColor handleColor = mgr.colors().textMuted;
    int lineW = theme.scaled(BaseHandleLineWidth);
    int lineH = theme.scaled(BaseHandleLineHeight);

    QRectF lineRect(rect.center().x() - lineW / 2.0, rect.center().y() - lineH / 2.0, lineW, lineH);
    qreal radius = lineRect.height() / 2.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(handleColor);
    painter.drawRoundedRect(lineRect, radius, radius);
}

void BrushPackPanel::drawBackground(QPainter& painter)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    // Match BrushControlOverlay: surface background with gradient border
    QColor bgColor = mgr.colors().surface;
    QColor borderTopColor = mgr.colors().border;
    QColor borderBottomColor = borderTopColor.darker(110);
    int radius = theme.scaled(CornerRadius);

    // Background fill
    QPainterPath bgPath;
    bgPath.addRoundedRect(rect(), radius, radius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(bgColor);
    painter.drawPath(bgPath);

    // Gradient border
    ruwa::ui::painting::drawGradientBorder(
        painter, rect(), radius, borderTopColor, borderBottomColor);
}

void BrushPackPanel::drawDivider(QPainter& painter)
{
    auto& mgr = WidgetStyleManager::instance();
    auto& theme = ThemeManager::instance();

    QColor dividerColor = mgr.colors().border;
    dividerColor.setAlpha(80);

    const int sidebarW
        = m_sidebarContainer ? m_sidebarContainer->width() : theme.scaled(SidebarWidth);

    // Vertical divider between sidebar and right panel
    painter.setPen(QPen(dividerColor, DividerThickness));
    painter.drawLine(sidebarW, theme.scaled(Padding), sidebarW, height() - theme.scaled(Padding));
}

void BrushPackPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    refreshSidebarGeometry();
    for (auto it = m_pages.cbegin(); it != m_pages.cend(); ++it) {
        if (it.value()) {
            it.value()->syncLayoutNow();
        }
    }
}

void BrushPackPanel::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragMode = hitTest(event->pos());
        if (m_dragMode == DragMode::Widget) {
            m_dragStartPos = event->globalPosition().toPoint();
            m_widgetStartPos = pos();
            setCompactMode(true);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void BrushPackPanel::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragMode == DragMode::Widget) {
        handleDrag(event->globalPosition().toPoint());
        event->accept();
        return;
    }

    DragMode mode = hitTest(event->pos());
    if (mode == DragMode::Widget) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }

    QWidget::mouseMoveEvent(event);
}

void BrushPackPanel::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragMode == DragMode::Widget) {
        m_dragMode = DragMode::None;

        DragMode mode = hitTest(event->pos());
        setCursor(mode == DragMode::Widget ? Qt::OpenHandCursor : Qt::ArrowCursor);

        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

} // namespace ruwa::ui::widgets
