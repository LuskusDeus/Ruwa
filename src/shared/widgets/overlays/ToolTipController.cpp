// SPDX-License-Identifier: MPL-2.0

// ToolTipController.cpp
#include "shared/widgets/overlays/ToolTipController.h"

#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/ShortcutKeycapRenderer.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/style/AnimationPolicy.h"

#include <QAbstractTextDocumentLayout>
#include <QCursor>
#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QPainter>
#include <QPalette>
#include <QScreen>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QToolTip>
#include <QVariantAnimation>
#include <QWidget>
#include <QtMath>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::widgets {

namespace {

constexpr int kCornerRadius = 8;
constexpr int kHorizontalPadding = 11;
constexpr int kVerticalPadding = 7;
constexpr int kShadowMargin = 8;
constexpr int kMaximumTextWidth = 360;
constexpr int kMinimumTextWidth = 48;
constexpr int kBlurRadius = 24;
constexpr int kPanelGapX = 8;
constexpr int kPanelGapY = 10;
constexpr int kPlacementMargin = 6;
constexpr int kShowDurationMs = 155;
constexpr int kHideDurationMs = 110;
constexpr qreal kInitialScale = 0.965;
constexpr qreal kPanelEdgeInset = 0.75;
constexpr int kInitialSlideDistance = 3;
constexpr int kDefaultVisibleDurationMs = 10000;
constexpr int kMaximumVisibleDurationMs = 30000;
constexpr int kShortcutGap = 12;
constexpr int kShortcutRowGap = 7;
constexpr auto kShortcutCommandProperty = "ruwaToolTipShortcutCommand";
constexpr auto kShortcutVisibleProperty = "ruwaToolTipShortcutVisible";

QRect globalWidgetRect(const QWidget* widget)
{
    if (!widget) {
        return {};
    }
    return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size());
}

int boundedCoordinate(int desired, int extent, int minimum, int maximum)
{
    const int greatestStart = maximum - extent + 1;
    if (greatestStart < minimum) {
        return minimum;
    }
    return qBound(minimum, desired, greatestStart);
}

int defaultVisibleDuration(const QString& text)
{
    const int additionalCharacters = qMax(0, text.size() - 100);
    return qMin(kMaximumVisibleDurationMs, kDefaultVisibleDurationMs + additionalCharacters * 40);
}

} // namespace

class CustomToolTipWindow final : public QWidget {
public:
    explicit CustomToolTipWindow(QWidget* parent = nullptr)
        : QWidget(parent,
              Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus
                  | Qt::WindowTransparentForInput | Qt::NoDropShadowWindowHint)
        , m_presentationAnimation(new QVariantAnimation(this))
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAutoFillBackground(false);
        setFocusPolicy(Qt::NoFocus);
        setObjectName(QStringLiteral("RuwaToolTip"));

        m_document.setDocumentMargin(0.0);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        option.setAlignment(Qt::AlignLeft);
        m_document.setDefaultTextOption(option);

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
            m_glassBackdrop = {};
        });
    }

    QSize prepare(const QString& text, const QKeySequence& shortcut, int maximumTextWidth)
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();

        m_horizontalPadding = theme.scaled(kHorizontalPadding);
        m_verticalPadding = theme.scaled(kVerticalPadding);
        m_shadowMargin = theme.scaled(kShadowMargin);

        QFont font = theme.font(ruwa::ui::core::ThemeFontRole::Body);
        m_document.setDefaultFont(font);
        m_document.setDefaultStyleSheet(
            QStringLiteral("body { color: %1; } a { color: %2; text-decoration: none; }")
                .arg(colors.text.name(QColor::HexRgb), colors.primary.name(QColor::HexRgb)));

        if (Qt::mightBeRichText(text)) {
            m_document.setHtml(text);
        } else {
            m_document.setPlainText(text);
        }
        QString accessibleText = m_document.toPlainText();
        const QString nativeShortcut = shortcut.toString(QKeySequence::NativeText);
        if (!nativeShortcut.isEmpty()) {
            accessibleText += QStringLiteral(", %1").arg(nativeShortcut);
        }
        setAccessibleName(accessibleText);

        prepareShortcut(shortcut);

        m_document.setTextWidth(-1.0);
        const qreal naturalWidth = qMax<qreal>(1.0, m_document.idealWidth());
        const qreal shortcutWidth = m_shortcutSize.width();
        const qreal horizontalGap = m_shortcutSize.isEmpty() ? 0.0 : theme.scaled(kShortcutGap);
        const bool placeShortcutBesideText = !m_shortcutSize.isEmpty()
            && naturalWidth + horizontalGap + shortcutWidth <= maximumTextWidth;
        const qreal textWidth = qBound<qreal>(1.0, naturalWidth,
            placeShortcutBesideText ? maximumTextWidth - horizontalGap - shortcutWidth
                                    : maximumTextWidth);
        m_document.setTextWidth(textWidth);

        const QSizeF documentSize = m_document.size();
        qreal contentWidth = documentSize.width();
        qreal contentHeight = documentSize.height();
        m_documentOrigin = QPointF(m_horizontalPadding, m_verticalPadding);
        m_shortcutOrigin = {};

        if (placeShortcutBesideText) {
            contentWidth += horizontalGap + shortcutWidth;
            contentHeight = qMax(contentHeight, m_shortcutSize.height());
            m_documentOrigin.setY(
                m_verticalPadding + (contentHeight - documentSize.height()) * 0.5);
            m_shortcutOrigin = QPointF(m_horizontalPadding + documentSize.width() + horizontalGap,
                m_verticalPadding + (contentHeight - m_shortcutSize.height()) * 0.5);
        } else if (!m_shortcutSize.isEmpty()) {
            const qreal verticalGap = theme.scaled(kShortcutRowGap);
            contentWidth = qMax(contentWidth, shortcutWidth);
            contentHeight += verticalGap + m_shortcutSize.height();
            m_shortcutOrigin = QPointF(
                m_horizontalPadding, m_verticalPadding + documentSize.height() + verticalGap);
        }

        const QSize panelSize(qCeil(contentWidth) + m_horizontalPadding * 2,
            qCeil(contentHeight) + m_verticalPadding * 2);
        m_panelRect = QRectF(QPointF(m_shadowMargin, m_shadowMargin), QSizeF(panelSize));

        const QSize windowSize(
            panelSize.width() + m_shadowMargin * 2, panelSize.height() + m_shadowMargin * 2);
        setFixedSize(windowSize);
        return windowSize;
    }

    QRect panelRect() const { return m_panelRect.toAlignedRect(); }

    void setGlassBackdrop(const QPixmap& backdrop)
    {
        m_glassBackdrop = backdrop;
        update();
    }

    void showAt(const QPoint& globalPosition, bool animated)
    {
        m_presentationAnimation->stop();
        m_hiding = false;
        move(globalPosition);

        m_presentationProgress = animated ? 0.0 : 1.0;
        setWindowOpacity(m_presentationProgress);
        QWidget::show();
        raise();

        if (animated) {
            m_presentationAnimation->setDuration(anim::duration(kShowDurationMs));
            m_presentationAnimation->setEasingCurve(QEasingCurve::OutCubic);
            m_presentationAnimation->setStartValue(0.0);
            m_presentationAnimation->setEndValue(1.0);
            m_presentationAnimation->start();
        }
    }

    void hideAnimated(bool animated)
    {
        if (!isVisible()) {
            return;
        }
        if (!animated) {
            hideImmediately();
            return;
        }

        m_presentationAnimation->stop();
        m_hiding = true;
        m_presentationAnimation->setDuration(anim::duration(kHideDurationMs));
        m_presentationAnimation->setEasingCurve(QEasingCurve::InCubic);
        m_presentationAnimation->setStartValue(m_presentationProgress);
        m_presentationAnimation->setEndValue(0.0);
        m_presentationAnimation->start();
    }

    void hideImmediately()
    {
        m_presentationAnimation->stop();
        m_hiding = false;
        QWidget::hide();
        setWindowOpacity(1.0);
        m_presentationProgress = 0.0;
        m_glassBackdrop = {};
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const auto& colors = theme.colors();

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        // Animate the already-rendered glass as one surface. This keeps the
        // expensive backdrop blur out of the animation loop.
        const qreal scale = kInitialScale + (1.0 - kInitialScale) * m_presentationProgress;
        const qreal slide = -theme.scaled(kInitialSlideDistance) * (1.0 - m_presentationProgress);
        const QPointF pivot = m_panelRect.center();
        painter.translate(0.0, slide);
        painter.translate(pivot);
        painter.scale(scale, scale);
        painter.translate(-pivot);

        // A small layered shadow avoids relying on the platform tooltip shadow,
        // which is inconsistent for translucent frameless windows.
        const int shadowStep = qMax(1, m_shadowMargin / 4);
        for (int spread = m_shadowMargin; spread >= shadowStep; spread -= shadowStep) {
            const qreal proximity = 1.0 - qreal(spread) / qMax(1, m_shadowMargin);
            QColor shadow = colors.shadow(qRound((4.0 + proximity * 8.0)));
            painter.setPen(Qt::NoPen);
            painter.setBrush(shadow);
            painter.drawRoundedRect(m_panelRect.adjusted(-spread, -spread, spread, spread),
                theme.scaled(kCornerRadius) + spread, theme.scaled(kCornerRadius) + spread);
        }

        painter.save();
        painter.translate(m_panelRect.topLeft());
        // Keep the antialiased outer coverage inside the raster bounds. Drawing
        // directly on the integer edge can clip the last coverage sample and
        // make an otherwise rounded corner read as a sharp half-pixel fringe.
        const QRectF localPanel
            = QRectF(QPointF(0, 0), m_panelRect.size())
                  .adjusted(kPanelEdgeInset, kPanelEdgeInset, -kPanelEdgeInset, -kPanelEdgeInset);

        QColor borderTop = colors.borderLight();
        borderTop.setAlpha(colors.isDark ? 118 : 132);
        QColor borderBottom = colors.borderDark();
        borderBottom.setAlpha(colors.isDark ? 82 : 96);

        ruwa::ui::painting::drawTonedGlassPanel(painter, localPanel, theme.scaled(kCornerRadius),
            m_panelRect.size(), m_glassBackdrop, colors.surfaceElevated(), colors.primary,
            colors.isDark, borderTop, borderBottom, 1.0, false);

        painter.save();
        painter.translate(m_documentOrigin);
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, colors.text);
        context.palette.setColor(QPalette::WindowText, colors.text);
        context.palette.setColor(QPalette::Link, colors.primary);
        context.palette.setColor(QPalette::LinkVisited, colors.primary);
        m_document.documentLayout()->draw(&painter, context);
        painter.restore();

        paintShortcut(painter);
        painter.restore();
    }

private:
    void prepareShortcut(const QKeySequence& shortcut)
    {
        m_shortcut = shortcut;
        m_shortcutSize = ShortcutKeycapRenderer::contentSize(
            shortcut, ShortcutKeycapRenderer::SizeVariant::Compact);
    }

    void paintShortcut(QPainter& painter)
    {
        if (m_shortcutSize.isEmpty()) {
            return;
        }
        ShortcutKeycapRenderer::paint(painter, QRectF(m_shortcutOrigin, m_shortcutSize), m_shortcut,
            Qt::AlignLeft | Qt::AlignVCenter, ShortcutKeycapRenderer::SizeVariant::Compact);
    }

    QTextDocument m_document;
    QVariantAnimation* m_presentationAnimation = nullptr;
    QPixmap m_glassBackdrop;
    QRectF m_panelRect;
    QKeySequence m_shortcut;
    QPointF m_documentOrigin;
    QPointF m_shortcutOrigin;
    QSizeF m_shortcutSize;
    qreal m_presentationProgress = 0.0;
    bool m_hiding = false;
    int m_horizontalPadding = 0;
    int m_verticalPadding = 0;
    int m_shadowMargin = 0;
};

ToolTipController::ToolTipController(QObject* parent)
    : QObject(parent)
    , m_toolTip(new CustomToolTipWindow)
    , m_hideTimer(new QTimer(this))
{
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this] { hideTip(); });
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this] { hideTip(); });
    connect(&ruwa::core::ShortcutManager::instance(), &ruwa::core::ShortcutManager::shortcutChanged,
        this, [this](const QString& commandId, const QKeySequence&) {
            if (m_source && m_source->property(kShortcutCommandProperty).toString() == commandId) {
                hideTip();
            }
        });
}

ToolTipController::~ToolTipController()
{
    delete m_toolTip;
}

void ToolTipController::setShortcutCommand(QWidget* widget, const QString& commandId)
{
    if (!widget) {
        return;
    }
    widget->setProperty(kShortcutCommandProperty, commandId.trimmed());
}

void ToolTipController::setShortcutVisible(QWidget* widget, bool visible)
{
    if (!widget) {
        return;
    }
    widget->setProperty(kShortcutVisibleProperty, visible);
}

bool ToolTipController::isShortcutVisible(const QWidget* widget)
{
    if (!widget) {
        return false;
    }
    const QVariant value = widget->property(kShortcutVisibleProperty);
    return !value.isValid() || value.toBool();
}

QWidget* ToolTipController::toolTipSourceFor(QWidget* widget) const
{
    for (QWidget* candidate = widget; candidate; candidate = candidate->parentWidget()) {
        if (!candidate->toolTip().trimmed().isEmpty()) {
            return candidate;
        }
        if (candidate->isWindow()) {
            break;
        }
    }
    return nullptr;
}

QKeySequence ToolTipController::shortcutFor(const QWidget* widget) const
{
    if (!isShortcutVisible(widget)) {
        return {};
    }

    const QString commandId = widget->property(kShortcutCommandProperty).toString().trimmed();
    if (commandId.isEmpty()) {
        return {};
    }
    return ruwa::core::ShortcutManager::instance().shortcutFor(commandId);
}

void ToolTipController::showFor(QWidget* eventTarget, const QHelpEvent& event)
{
    QWidget* source = toolTipSourceFor(eventTarget);
    if (!source) {
        hideTip();
        return;
    }

    const QString text = source->toolTip();
    const QKeySequence shortcut = shortcutFor(source);
    if (m_toolTip->isVisible() && m_source == source && m_text == text && m_shortcut == shortcut) {
        const int requestedDuration = source->toolTipDuration();
        m_hideTimer->start(
            requestedDuration >= 0 ? requestedDuration : defaultVisibleDuration(text));
        return;
    }

    QToolTip::hideText();

    QScreen* screen = QGuiApplication::screenAt(event.globalPos());
    if (!screen) {
        screen = source->screen();
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        hideTip();
        return;
    }

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const QRect screenBounds = screen->availableGeometry().adjusted(theme.scaled(kPlacementMargin),
        theme.scaled(kPlacementMargin), -theme.scaled(kPlacementMargin),
        -theme.scaled(kPlacementMargin));
    const int maximumTextWidth = qMax(theme.scaled(kMinimumTextWidth),
        qMin(theme.scaled(kMaximumTextWidth), screenBounds.width() - theme.scaled(40)));
    const QSize tipSize = m_toolTip->prepare(text, shortcut, maximumTextWidth);
    const QRect localPanel = m_toolTip->panelRect();

    QRect placementBounds = screenBounds;
    QWidget* sourceWindow = source->window();
    const QRect sourceWindowBounds = globalWidgetRect(sourceWindow).intersected(screenBounds);
    if (sourceWindowBounds.width() >= tipSize.width()
        && sourceWindowBounds.height() >= tipSize.height()) {
        placementBounds = sourceWindowBounds;
    }

    // Position the visible glass panel rather than the outer transparent window,
    // whose margins are reserved for the soft shadow.
    QPoint desired = event.globalPos() + QPoint(theme.scaled(kPanelGapX), theme.scaled(kPanelGapY))
        - localPanel.topLeft();
    if (desired.x() + tipSize.width() > placementBounds.right() + 1) {
        desired.setX(event.globalPos().x() - theme.scaled(kPanelGapX) - localPanel.right());
    }
    if (desired.y() + tipSize.height() > placementBounds.bottom() + 1) {
        desired.setY(event.globalPos().y() - theme.scaled(kPanelGapY) - localPanel.bottom());
    }

    desired.setX(boundedCoordinate(
        desired.x(), tipSize.width(), placementBounds.left(), placementBounds.right()));
    desired.setY(boundedCoordinate(
        desired.y(), tipSize.height(), placementBounds.top(), placementBounds.bottom()));

    m_toolTip->setGlassBackdrop({});
    const QRect globalPanel(desired + localPanel.topLeft(), localPanel.size());
    if (sourceWindow) {
        const QPoint localTopLeft = sourceWindow->mapFromGlobal(globalPanel.topLeft());
        const QRect grabRect(localTopLeft, globalPanel.size());
        if (QRect(QPoint(0, 0), sourceWindow->size()).contains(grabRect)) {
            const QPixmap snapshot = sourceWindow->grab(grabRect);
            if (!snapshot.isNull()) {
                m_toolTip->setGlassBackdrop(
                    ruwa::ui::painting::blurSnapshotPixmap(snapshot, theme.scaled(kBlurRadius)));
            }
        }
    }

    if (m_source != source) {
        QObject::disconnect(m_sourceDestroyedConnection);
        m_sourceDestroyedConnection
            = connect(source, &QObject::destroyed, this, [this] { hideTip(); });
    }
    m_source = source;
    m_eventTarget = eventTarget;
    m_text = text;
    m_shortcut = shortcut;

    const bool animated = ruwa::ui::core::WidgetStyleManager::instance().animationsEnabled();
    m_toolTip->showAt(desired, animated);

    const int requestedDuration = source->toolTipDuration();
    m_hideTimer->start(requestedDuration >= 0 ? requestedDuration : defaultVisibleDuration(text));
}

void ToolTipController::hideTip()
{
    m_hideTimer->stop();
    const bool animated = ruwa::ui::core::WidgetStyleManager::instance().animationsEnabled();
    m_toolTip->hideAnimated(animated);
    QObject::disconnect(m_sourceDestroyedConnection);
    m_sourceDestroyedConnection = {};
    m_source = nullptr;
    m_eventTarget = nullptr;
    m_text.clear();
    m_shortcut = {};
}

void ToolTipController::hideIfPointerLeftSource()
{
    QTimer::singleShot(0, this, [this] {
        if (m_source && !globalWidgetRect(m_source).contains(QCursor::pos())) {
            hideTip();
        }
    });
}

bool ToolTipController::eventFilter(QObject* watched, QEvent* event)
{
    if (!event || watched == m_toolTip) {
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::ToolTip) {
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (toolTipSourceFor(widget)) {
                event->accept();
                showFor(widget, *static_cast<QHelpEvent*>(event));
                return true;
            }
            hideTip();
        }
    }

    if (!m_toolTip->isVisible()) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::Leave:
        if (watched == m_eventTarget || watched == m_source) {
            hideIfPointerLeftSource();
        }
        break;
    case QEvent::ToolTipChange:
    case QEvent::DynamicPropertyChange:
        if (watched == m_eventTarget || watched == m_source) {
            hideTip();
        }
        break;
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::DevicePixelRatioChange:
        if (m_source && watched == m_source->window()) {
            hideTip();
        }
        break;
    case QEvent::Hide:
    case QEvent::Close:
    case QEvent::Destroy:
        if (watched == m_eventTarget || watched == m_source
            || (m_source && watched == m_source->window())) {
            hideTip();
        }
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
    case QEvent::TouchBegin:
    case QEvent::WindowDeactivate:
    case QEvent::ApplicationDeactivate:
        hideTip();
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

} // namespace ruwa::ui::widgets
