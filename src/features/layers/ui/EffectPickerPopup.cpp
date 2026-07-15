// SPDX-License-Identifier: MPL-2.0

#include "EffectPickerPopup.h"

#include "features/effects/LayerEffectRegistry.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/inputs/SearchBar.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shell/top-bar/OverlayContainer.h"

#include <QApplication>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <functional>

namespace ruwa::ui::workspace {

using ruwa::core::effects::EffectCatalogCategory;
using ruwa::core::effects::EffectCatalogEntry;
using ruwa::core::effects::LayerEffectRegistry;
using ruwa::ui::core::ThemeManager;
using ruwa::ui::widgets::OverlayContainer;
using ruwa::ui::widgets::SearchBar;
using ruwa::ui::widgets::SmoothScrollArea;

namespace {
constexpr int kShadowMargin = 16;
constexpr int kPopupWidth = 268;
constexpr int kMinContentHeight = 220;
constexpr int kMaxContentHeight = 460;
constexpr qreal kContentHeightScreenFraction = 0.5;
constexpr int kRowHeight = 30;
constexpr int kHeaderHeight = 30;
constexpr int kCardRadius = 12;

// Folder expand/collapse height tween — same feel as BrushPackListSection's
// content-height animation (duration scales with distance, clamped).
constexpr int kFolderAnimMinMs = 170;
constexpr int kFolderAnimMaxMs = 320;
constexpr qreal kFolderAnimMsPerPixel = 0.85;
} // namespace

// ============================================================================
// EffectPickerRow — one selectable (or placeholder) effect leaf.
// ============================================================================
class EffectPickerRow : public QWidget {
public:
    EffectPickerRow(const EffectCatalogEntry& entry, QWidget* parent)
        : QWidget(parent)
        , m_typeId(entry.typeId)
        , m_name(entry.displayName)
        , m_implemented(entry.implemented)
    {
        setFixedHeight(kRowHeight);
        setMouseTracking(true);
        setCursor(m_implemented ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setAttribute(Qt::WA_Hover, true);

        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(140);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_hoverProgress = v.toReal();
            update();
        });
    }

    const QString& typeId() const { return m_typeId; }
    const QString& name() const { return m_name; }
    bool implemented() const { return m_implemented; }

    std::function<void(const QString&)> onActivated;

protected:
    void enterEvent(QEnterEvent*) override { animateHoverTo(1.0); }
    void leaveEvent(QEvent*) override { animateHoverTo(0.0); }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && m_implemented && rect().contains(e->pos())
            && onActivated) {
            onActivated(m_typeId);
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        const auto& c = ThemeManager::instance().colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect r = rect().adjusted(2, 1, -2, -1);
        if (m_implemented && m_hoverProgress > 0.001) {
            QColor hoverBg = c.overlayHover();
            hoverBg.setAlphaF(hoverBg.alphaF() * m_hoverProgress);
            p.setPen(Qt::NoPen);
            p.setBrush(hoverBg);
            p.drawRoundedRect(r, 6, 6);
        }

        QColor textColor = m_implemented ? c.text : c.textDisabled();
        p.setPen(textColor);
        QFont f = c.fonts.getUIFont();
        p.setFont(f);
        const QRect textRect = r.adjusted(20, 0, -8, 0);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
            QFontMetrics(f).elidedText(m_name, Qt::ElideRight, textRect.width()));
    }

private:
    void animateHoverTo(qreal target)
    {
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverProgress);
        m_hoverAnim->setEndValue(target);
        m_hoverAnim->start();
    }

    QString m_typeId;
    QString m_name;
    bool m_implemented = false;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation* m_hoverAnim = nullptr;
};

// ============================================================================
// EffectPickerHeader — a collapsible category folder header.
// ============================================================================
class EffectPickerHeader : public QWidget {
public:
    EffectPickerHeader(const QString& name, int count, QWidget* parent)
        : QWidget(parent)
        , m_name(name)
        , m_count(count)
    {
        setFixedHeight(kHeaderHeight);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);

        m_expandAnim = new QVariantAnimation(this);
        m_expandAnim->setDuration(190);
        m_expandAnim->setEasingCurve(QEasingCurve::InOutCubic);
        connect(m_expandAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_expandProgress = v.toReal();
            update();
        });

        m_hoverAnim = new QVariantAnimation(this);
        m_hoverAnim->setDuration(140);
        m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_hoverProgress = v.toReal();
            update();
        });
    }

    bool expanded() const { return m_expanded; }
    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded) {
            return;
        }
        m_expanded = expanded;
        m_expandAnim->stop();
        m_expandAnim->setStartValue(m_expandProgress);
        m_expandAnim->setEndValue(m_expanded ? 1.0 : 0.0);
        m_expandAnim->start();
        if (onToggled) {
            onToggled(m_expanded);
        }
    }

    std::function<void(bool)> onToggled;

protected:
    void enterEvent(QEnterEvent*) override { animateHoverTo(1.0); }
    void leaveEvent(QEvent*) override { animateHoverTo(0.0); }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
            setExpanded(!m_expanded);
        }
    }

    void paintEvent(QPaintEvent*) override
    {
        const auto& c = ThemeManager::instance().colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect r = rect().adjusted(2, 1, -2, -1);
        if (m_hoverProgress > 0.001) {
            QColor hoverBg = c.overlayBase();
            hoverBg.setAlphaF(hoverBg.alphaF() * m_hoverProgress);
            p.setPen(Qt::NoPen);
            p.setBrush(hoverBg);
            p.drawRoundedRect(r, 6, 6);
        }

        // Chevron: a right-pointing arrow that rotates 90° into a down-pointing
        // one as m_expandProgress goes 0→1 (same technique as the brush panel's
        // PackHeaderButton), instead of snapping between two fixed shapes.
        const QPointF center(r.left() + 12, r.center().y());
        p.save();
        p.translate(center);
        p.rotate(90.0 * m_expandProgress);
        QPainterPath chevron;
        chevron.moveTo(-2, -4);
        chevron.lineTo(3, 0);
        chevron.lineTo(-2, 4);
        QPen chevronPen(c.textMuted, 1.6);
        chevronPen.setCapStyle(Qt::RoundCap);
        chevronPen.setJoinStyle(Qt::RoundJoin);
        p.setPen(chevronPen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(chevron);
        p.restore();

        QFont f = c.fonts.getUIFont();
        f.setWeight(QFont::DemiBold);
        p.setFont(f);
        p.setPen(c.text);
        p.drawText(r.adjusted(24, 0, -30, 0), Qt::AlignVCenter | Qt::AlignLeft, m_name);

        p.setPen(c.textMuted);
        p.setFont(c.fonts.getUIFont());
        p.drawText(
            r.adjusted(0, 0, -8, 0), Qt::AlignVCenter | Qt::AlignRight, QString::number(m_count));
    }

private:
    void animateHoverTo(qreal target)
    {
        m_hoverAnim->stop();
        m_hoverAnim->setStartValue(m_hoverProgress);
        m_hoverAnim->setEndValue(target);
        m_hoverAnim->start();
    }

    QString m_name;
    int m_count = 0;
    bool m_expanded = false; // folders start collapsed
    qreal m_expandProgress = 0.0;
    QVariantAnimation* m_expandAnim = nullptr;
    qreal m_hoverProgress = 0.0;
    QVariantAnimation* m_hoverAnim = nullptr;
};

// ============================================================================
// EffectPickerPopup
// ============================================================================
EffectPickerPopup::EffectPickerPopup(QWidget* parent)
    : QWidget(parent)
{
    // Overlay child (no window flags) — the OverlayContainer hosts and layers it.
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(kPopupWidth + 2 * kShadowMargin);
    hide();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(
        kShadowMargin + 8, kShadowMargin + 8, kShadowMargin + 8, kShadowMargin + 8);
    root->setSpacing(8);

    m_search = new SearchBar(this);
    m_search->setPlaceholder(tr("Search effects..."));
    m_search->setClearButtonEnabled(true);
    m_search->setBarHeight(34);
    m_search->setMinimumBarWidth(0);
    root->addWidget(m_search);

    m_scroll = new SmoothScrollArea(this);
    m_scroll->setFillBackground(false);
    m_scroll->setScrollBarTransparentTrack(true);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    root->addWidget(m_scroll, 1);

    m_listContent = new QWidget(m_scroll);
    m_listContent->setAttribute(Qt::WA_TranslucentBackground);
    m_listLayout = new QVBoxLayout(m_listContent);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(2);
    m_listLayout->addStretch(1);
    m_scroll->setWidget(m_listContent);

    connect(m_search, &SearchBar::textChanged, this,
        [this](const QString& text) { applyFilter(text); });

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_opacityAnim = new QPropertyAnimation(this, "popupOpacity", this);
    m_opacityAnim->setDuration(150);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
}

EffectPickerPopup::~EffectPickerPopup()
{
    if (m_appFilterInstalled) {
        qApp->removeEventFilter(this);
    }
    if (m_overlay) {
        m_overlay->unregisterGenericPopup(this);
    }
}

void EffectPickerPopup::rebuild()
{
    // The catalog is static; build the folder tree once and reuse it. Theme
    // changes are picked up live in the child paintEvents, so no rebuild needed.
    if (!m_sections.isEmpty()) {
        return;
    }

    const QList<EffectCatalogCategory> catalog = LayerEffectRegistry::instance().catalog();
    int insertAt = 0; // keep the trailing stretch last
    for (const EffectCatalogCategory& cat : catalog) {
        Section section;
        section.header = new EffectPickerHeader(cat.name, cat.entries.size(), m_listContent);

        section.body = new QWidget(m_listContent);
        section.body->setAttribute(Qt::WA_TranslucentBackground);
        // The body stays visible forever from here on — reveal/hide is done
        // purely by tweening its height (0 = clipped away), never by hiding it
        // outright, so the rows underneath can animate smoothly into view.
        auto* bodyLayout = new QVBoxLayout(section.body);
        bodyLayout->setContentsMargins(6, 0, 0, 2);
        bodyLayout->setSpacing(1);

        for (const EffectCatalogEntry& entry : cat.entries) {
            auto* row = new EffectPickerRow(entry, section.body);
            row->onActivated = [this](const QString& typeId) {
                emit effectChosen(typeId);
                hidePopup();
            };
            bodyLayout->addWidget(row);
            section.rows.append(row);
        }

        section.heightAnim = new QVariantAnimation(section.body);
        section.heightAnim->setEasingCurve(QEasingCurve::InOutCubic);
        QWidget* body = section.body;
        connect(section.heightAnim, &QVariantAnimation::valueChanged, body,
            [this, body](const QVariant& v) {
                body->setFixedHeight(v.toInt());
                m_scroll->refreshScrollGeometry();
            });
        // Snap to the collapsed-by-default state now that all rows exist (their
        // heights feed the body's sizeHint(), which the expand animation targets).
        setSectionBodyExpanded(body, section.heightAnim, section.header->expanded(), false);

        QVariantAnimation* heightAnim = section.heightAnim;
        section.header->onToggled = [this, body, heightAnim](bool expanded) {
            if (!m_filtering) {
                setSectionBodyExpanded(body, heightAnim, expanded, true);
            }
        };

        m_listLayout->insertWidget(insertAt++, section.header);
        m_listLayout->insertWidget(insertAt++, section.body);
        m_sections.append(section);
    }
    m_scroll->refreshScrollGeometry();
}

void EffectPickerPopup::setSectionBodyExpanded(
    QWidget* body, QVariantAnimation* anim, bool expanded, bool animated)
{
    if (!body || !anim) {
        return;
    }
    anim->stop();

    const int targetHeight = expanded ? body->sizeHint().height() : 0;
    if (!animated) {
        body->setFixedHeight(targetHeight);
        m_scroll->refreshScrollGeometry();
        return;
    }

    const int currentHeight = body->height();
    if (currentHeight == targetHeight) {
        return;
    }

    const int duration = qBound(kFolderAnimMinMs,
        qRound(qAbs(targetHeight - currentHeight) * kFolderAnimMsPerPixel), kFolderAnimMaxMs);
    anim->setDuration(duration);
    anim->setStartValue(currentHeight);
    anim->setEndValue(targetHeight);
    anim->start();
}

void EffectPickerPopup::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();
    m_filtering = !needle.isEmpty();

    for (const Section& s : m_sections) {
        int visibleRows = 0;
        for (EffectPickerRow* row : s.rows) {
            const bool match
                = needle.isEmpty() || row->name().contains(needle, Qt::CaseInsensitive);
            row->setVisible(match);
            if (match) {
                ++visibleRows;
            }
        }
        // While filtering, force folders open so matches are always visible.
        // Filter edits snap instantly — only the manual header-click toggle animates.
        const bool expanded = m_filtering ? true : s.header->expanded();
        setSectionBodyExpanded(s.body, s.heightAnim, expanded && visibleRows > 0, false);
        s.header->setVisible(visibleRows > 0);
    }
    m_scroll->refreshScrollGeometry();
}

int EffectPickerPopup::screenBasedContentHeight() const
{
    QScreen* screen = m_anchor
        ? QApplication::screenAt(m_anchor->mapToGlobal(m_anchor->rect().center()))
        : nullptr;
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    const int screenH = screen ? screen->availableGeometry().height() : kMaxContentHeight;
    return qBound(
        kMinContentHeight, qRound(screenH * kContentHeightScreenFraction), kMaxContentHeight);
}

QPoint EffectPickerPopup::calculateTargetPos()
{
    m_placedAbove = false;
    if (!m_overlay || !m_anchor) {
        return QPoint();
    }
    // Below the anchor, card left edge aligned to the anchor left edge.
    QPoint belowGlobal = m_anchor->mapToGlobal(QPoint(0, m_anchor->height() + 4));
    QPoint pos = m_overlay->mapFromGlobal(belowGlobal) - QPoint(kShadowMargin + 8, kShadowMargin);

    const QRect avail = m_overlay->rect();
    pos.setX(qBound(avail.left(), pos.x(), qMax(avail.left(), avail.right() - width() + 1)));

    if (pos.y() + height() > avail.bottom()) {
        // Flip above the anchor when there is no room below.
        const int aboveTop = m_overlay->mapFromGlobal(m_anchor->mapToGlobal(QPoint(0, 0))).y();
        pos.setY(qMax(avail.top(), aboveTop - height() + kShadowMargin - 4));
        m_placedAbove = true;
    }
    return pos;
}

void EffectPickerPopup::popupUnder(QWidget* anchor)
{
    if (!anchor) {
        return;
    }
    m_anchor = anchor;
    m_overlay = OverlayContainer::instance(anchor->window());
    if (!m_overlay) {
        return;
    }
    m_overlay->registerGenericPopup(this); // reparents onto the overlay (idempotent)

    rebuild();
    m_search->clear();
    applyFilter(QString());

    // Height is fixed per-screen (not derived from content): opening/collapsing
    // folders never resizes the popup, overflow scrolls within it instead.
    const int chrome = 2 * (kShadowMargin + 8) + m_search->sizeHint().height() + 8;
    const int contentH = screenBasedContentHeight();
    resize(width(), contentH + chrome);

    // Refresh the overlay to the full current window geometry BEFORE mapping the
    // anchor into overlay coordinates. The overlay is created early (by the top
    // bar) with a stale startup size; without this refresh the very first popup
    // clamps against that stale rect and lands far away (e.g. the opposite half).
    m_isVisible = true;
    m_isHiding = false;
    m_overlay->showOverlay();

    m_targetPos = calculateTargetPos();
    move(m_targetPos);

    // Publish the overlay hit mask (local coords). Keep the drop shadow on three
    // sides but clip the edge that faces the anchor by the shadow margin, so the
    // invisible margin never covers the Add button (which sits just past that edge)
    // and the button keeps its hover/cursor feedback.
    QRect maskLocal = rect();
    if (m_placedAbove) {
        maskLocal.setBottom(maskLocal.bottom() - kShadowMargin);
    } else {
        maskLocal.setTop(maskLocal.top() + kShadowMargin);
    }
    setProperty("ruwaOverlayMaskRect", maskLocal);

    show();
    raise();
    m_overlay->refreshGenericPopups();

    startShowAnimation();

    if (!m_appFilterInstalled) {
        qApp->installEventFilter(this);
        m_appFilterInstalled = true;
    }
    m_search->setFocus();
}

void EffectPickerPopup::startShowAnimation()
{
    m_opacityAnim->stop();
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(1.0);
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    m_opacityAnim->start();
}

void EffectPickerPopup::hidePopup()
{
    if (!m_isVisible || m_isHiding) {
        return;
    }
    m_isHiding = true;

    if (m_appFilterInstalled) {
        qApp->removeEventFilter(this);
        m_appFilterInstalled = false;
    }

    m_opacityAnim->stop();
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(0.0);
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() { finishHide(); });
    m_opacityAnim->start();
}

void EffectPickerPopup::finishHide()
{
    hide();
    m_isVisible = false;
    m_isHiding = false;
    if (m_overlay) {
        m_overlay->refreshGenericPopups();
    }
}

void EffectPickerPopup::setPopupOpacity(qreal opacity)
{
    m_opacity = opacity;
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(opacity);
    }
    // Slide into place a few pixels as it fades in: downward when the popup sits
    // below the anchor, upward when it was flipped above it.
    const qreal dir = m_placedAbove ? 6.0 : -6.0;
    const int slide = static_cast<int>((1.0 - opacity) * dir);
    move(m_targetPos + QPoint(0, slide));
}

void EffectPickerPopup::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_scroll->refreshScrollGeometry();
}

bool EffectPickerPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (m_isVisible && !m_isHiding
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::NonClientAreaMouseButtonPress)) {
        if (auto* me = static_cast<QMouseEvent*>(event)) {
            const QPoint global = me->globalPosition().toPoint();
            const QRect ourRect(mapToGlobal(QPoint(0, 0)), size());
            const bool onAnchor = m_anchor
                && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(global);
            // Re-press on the Add button toggles the popup closed. This must be
            // handled here (not via the button's click): the popup's invisible
            // shadow margin overlaps the button, so the button may never receive
            // the click. Presses elsewhere outside the card also close it.
            if (onAnchor || !ourRect.contains(global)) {
                hidePopup();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EffectPickerPopup::paintEvent(QPaintEvent*)
{
    const auto& c = ThemeManager::instance().colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect card
        = rect().adjusted(kShadowMargin, kShadowMargin, -kShadowMargin, -kShadowMargin);

    // Soft drop shadow.
    for (int i = kShadowMargin; i > 0; --i) {
        const qreal t = static_cast<qreal>(i) / kShadowMargin;
        QColor s(0, 0, 0, static_cast<int>(6 * (1.0 - t) + 2));
        p.setPen(Qt::NoPen);
        p.setBrush(s);
        p.drawRoundedRect(card.adjusted(-i, -i + 2, i, i + 2), kCardRadius + i, kCardRadius + i);
    }

    // Card body + border.
    p.setPen(QPen(c.borderLight(), 1));
    p.setBrush(c.surfaceElevated());
    p.drawRoundedRect(QRectF(card).adjusted(0.5, 0.5, -0.5, -0.5), kCardRadius, kCardRadius);
}

} // namespace ruwa::ui::workspace
