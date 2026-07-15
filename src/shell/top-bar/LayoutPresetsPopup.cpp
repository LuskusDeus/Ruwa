// SPDX-License-Identifier: MPL-2.0

// LayoutPresetsPopup.cpp
#include "LayoutPresetsPopup.h"
#include "OverlayContainer.h"

#include "shell/docking/state/DockLayoutPresetStore.h"
#include "shared/widgets/PresetMenuListWidget.h"
#include "shared/widgets/PresetMenuTypes.h"
#include "shared/style/PaintingUtils.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"
#include "shell/top-bar/MessagePopupManager.h"

#include <QApplication>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {

constexpr int kLayoutListNewActionId = 1;

int windowEdgeMarginPx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(12);
}

// Attached-popup soft-shadow / flare extents (scaled), shared with MenuPopup/MessagePopup.
int attachedShadowExtentPx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(
        ruwa::ui::painting::kAttachedShadowExtentBase);
}
int attachedShadowSideExtentPx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(
        ruwa::ui::painting::kAttachedShadowSideExtentBase);
}
int attachedOuterCornerRadiusPx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(
        ruwa::ui::painting::kAttachedOuterCornerRadiusBase);
}

bool isCanvasPanelNode(const QJsonObject& node)
{
    const QString panelId = node.value(QStringLiteral("panelId")).toString().toLower();
    const QString panelTitle = node.value(QStringLiteral("panelTitle")).toString().toLower();
    return panelId == QLatin1String("canvas") || panelTitle.contains(QLatin1String("canvas"));
}

QColor layoutPreviewColor(bool canvas)
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    QColor color = canvas
        ? ruwa::ui::core::ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.18 : 1.0)
        : colors.textMuted;
    color.setAlpha(canvas ? (colors.isDark ? 92 : 78) : (colors.isDark ? 58 : 48));
    return color;
}

void drawLayoutPreviewNode(QPainter& painter, const QJsonObject& node, const QRectF& rect)
{
    if (rect.width() < 2.0 || rect.height() < 2.0) {
        return;
    }

    const QString type = node.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("leaf")) {
        const bool canvas = isCanvasPanelNode(node);
        const qreal radius = qMin<qreal>(3.0, qMin(rect.width(), rect.height()) / 7.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(layoutPreviewColor(canvas));
        painter.drawRoundedRect(rect, radius, radius);
        return;
    }

    if (type != QLatin1String("split")) {
        return;
    }

    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    if (children.isEmpty()) {
        return;
    }

    const bool horizontal
        = node.value(QStringLiteral("direction")).toString() != QLatin1String("vertical");
    const QJsonArray sizes = node.value(QStringLiteral("sizes")).toArray();
    QVector<qreal> weights;
    weights.reserve(children.size());

    qreal totalWeight = 0.0;
    for (int i = 0; i < children.size(); ++i) {
        const qreal weight = (i < sizes.size()) ? qMax(0.0, sizes.at(i).toDouble()) : 0.0;
        weights.append(weight);
        totalWeight += weight;
    }
    if (totalWeight <= 0.0) {
        totalWeight = children.size();
        weights.fill(1.0);
    }

    const qreal gap = 3.0;
    const qreal span = horizontal ? rect.width() : rect.height();
    const qreal available = qMax<qreal>(0.0, span - gap * (children.size() - 1));
    qreal cursor = horizontal ? rect.left() : rect.top();

    for (int i = 0; i < children.size(); ++i) {
        const qreal childSpan = (i == children.size() - 1)
            ? ((horizontal ? rect.right() : rect.bottom()) - cursor)
            : qMax<qreal>(0.0, available * weights.at(i) / totalWeight);
        QRectF childRect = horizontal ? QRectF(cursor, rect.top(), childSpan, rect.height())
                                      : QRectF(rect.left(), cursor, rect.width(), childSpan);
        childRect = childRect.adjusted(0.0, 0.0, -0.25, -0.25);
        drawLayoutPreviewNode(painter, children.at(i).toObject(), childRect);
        cursor += childSpan + gap;
    }
}

QImage makeLayoutPreviewImage(const ruwa::ui::docking::DockLayoutPreset& preset)
{
    constexpr int W = 160;
    constexpr int H = 90;
    QImage image(QSize(W, H), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF content(6.0, 6.0, W - 12.0, H - 12.0);
    const QJsonObject root = preset.layoutTree.value(QStringLiteral("root")).toObject();
    if (!root.isEmpty()) {
        drawLayoutPreviewNode(painter, root, content);
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(layoutPreviewColor(true));
        painter.drawRoundedRect(content, 3.0, 3.0);
    }

    return image;
}

} // namespace

LayoutPresetsPopup::LayoutPresetsPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setMouseTracking(true);
    hide();

    m_opacityAnim = new QPropertyAnimation(this, "popupOpacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_posAnim = new QPropertyAnimation(this, "pos", this);
    m_posAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        m_posAnim, &QPropertyAnimation::valueChanged, this, [this]() { emit contentChanged(); });

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(0, 0, 0, 0);
    m_outerLayout->setSpacing(0);

    m_presetList = new PresetMenuListWidget(this);
    m_presetList->setPopupStyle(true);
    // This popup paints its own attached-to-TopBar chrome; the list stays transparent
    // and supplies no glass-blur backdrop.
    m_presetList->setPopupPanelPainted(false);
    m_presetList->setImportExportVisible(true);
    m_presetList->setSearchEnabled(false);
    applyListChrome();
    m_outerLayout->addWidget(m_presetList);

    connect(m_presetList, &PresetMenuListWidget::itemClicked, this, [this](const QVariant& data) {
        const QUuid id = QUuid(data.toString());
        const auto opt = ruwa::ui::docking::DockLayoutPresetStore::instance().presetById(id);
        if (opt) {
            emit presetChosen(*opt);
            hidePopup();
        }
    });

    connect(m_presetList, &PresetMenuListWidget::headerActionTriggered, this, [this](int actionId) {
        if (actionId == kLayoutListNewActionId) {
            emit newPresetFromCurrentRequested();
        }
    });

    connect(m_presetList, &PresetMenuListWidget::exportClicked, this, [this]() {
        emit exportCurrentLayoutRequested();
        hidePopup();
    });

    connect(m_presetList, &PresetMenuListWidget::importClicked, this, [this]() {
        emit importLayoutRequested();
        hidePopup();
    });

    connect(m_presetList, &PresetMenuListWidget::itemRenamed, this,
        [this](const QVariant& data, const QString& newText) {
            const QUuid id = QUuid(data.toString());
            const QString trimmed = newText.trimmed();
            if (trimmed.isEmpty()) {
                refreshPresets();
                return;
            }
            const auto opt = ruwa::ui::docking::DockLayoutPresetStore::instance().presetById(id);
            if (!opt || opt->isBuiltIn) {
                return;
            }
            ruwa::ui::docking::DockLayoutPreset updated = *opt;
            updated.name = trimmed;
            ruwa::ui::docking::DockLayoutPresetStore::instance().updateCustomPreset(updated);
        });

    connect(
        m_presetList, &PresetMenuListWidget::deleteRequested, this, [this](const QVariant& data) {
            const QUuid id = QUuid(data.toString());
            const auto opt = ruwa::ui::docking::DockLayoutPresetStore::instance().presetById(id);
            if (!opt || opt->isBuiltIn) {
                return;
            }
            const QString msg = tr("Delete layout \"%1\"?").arg(opt->name);
            if (!MessagePopupManager::showBlocking(this, msg, tr("Yes"), tr("No"), 360, true)) {
                return;
            }
            ruwa::ui::docking::DockLayoutPresetStore::instance().removeCustomPreset(id);
        });

    connect(&ruwa::ui::docking::DockLayoutPresetStore::instance(),
        &ruwa::ui::docking::DockLayoutPresetStore::changed, this, [this]() { refreshPresets(); });

    refreshPresets();
}

LayoutPresetsPopup::~LayoutPresetsPopup() = default;

void LayoutPresetsPopup::applyListChrome()
{
    m_presetList->setHeaderActions({});
    PresetMenuHeaderAction newAction;
    newAction.id = kLayoutListNewActionId;
    newAction.icon = ruwa::ui::core::IconProvider::StandardIcon::FileNew;
    newAction.text = tr("New from current");
    newAction.toolTip = tr("Save current layout as preset");
    newAction.accent = true;
    m_presetList->setFooterAction(newAction);
    m_presetList->setTitleText(tr("Layouts"));
    m_presetList->setEmptyStateTexts(
        tr("No layouts"), tr("Create a layout from the current workspace."));
}

void LayoutPresetsPopup::refreshPresets()
{
    m_isRefreshing = true;

    // Reserve side + bottom padding for the soft shadow and the top-edge flare so the
    // list content sits inside the visible attached body.
    const int sidePad = attachedShadowSideExtentPx() + attachedOuterCornerRadiusPx();
    const int bottomPad = attachedShadowExtentPx();
    m_outerLayout->setContentsMargins(sidePad, 0, sidePad, bottomPad);

    QVector<PresetMenuItem> items;
    const QVector<ruwa::ui::docking::DockLayoutPreset> all
        = ruwa::ui::docking::DockLayoutPresetStore::instance().allPresets();
    items.reserve(all.size() + 1);
    for (int i = 0; i < all.size(); ++i) {
        const auto& preset = all[i];
        PresetMenuItem mi;
        mi.title = preset.name;
        mi.previewIcon = ruwa::ui::core::IconProvider::StandardIcon::DockLayout;
        mi.previewImage = makeLayoutPreviewImage(preset);
        mi.previewWide = true;
        mi.previewFrameless = true;
        mi.userData = preset.id.toString();
        mi.deletable = !preset.isBuiltIn;
        mi.renamable = !preset.isBuiltIn;
        if (preset.isBuiltIn) {
            mi.hasTitleTrailingIcon = true;
            mi.titleTrailingIcon = ruwa::ui::core::IconProvider::StandardIcon::Lock;
        }
        items.append(mi);
    }

    m_presetList->setItems(items);
    m_presetList->setMinimumWidth(ruwa::ui::core::ThemeManager::instance().scaled(280));

    // Pin the list to its natural height so the reveal/collapse clips it from the seam
    // instead of squeezing the scroll area (which would flash a scrollbar mid-animation).
    m_presetList->adjustSize();
    m_presetList->setFixedHeight(m_presetList->sizeHint().height());

    adjustSize();
    const QSize sh = sizeHint();
    if (sh.isValid()) {
        setFixedWidth(sh.width());
        m_targetHeight = sh.height();
        // Don't clobber an in-flight reveal/collapse — only snap to full height
        // when not animating.
        if (!m_isAnimatingHeight) {
            m_displayHeight = m_targetHeight;
            setFixedHeight(m_targetHeight);
        }
    }

    m_isRefreshing = false;
}

void LayoutPresetsPopup::setPopupOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);

    if (qFuzzyCompare(m_opacity, 1.0)) {
        if (m_opacityEffect) {
            setGraphicsEffect(nullptr);
            m_opacityEffect = nullptr;
        }
    } else {
        if (!m_opacityEffect) {
            m_opacityEffect = new QGraphicsOpacityEffect(this);
            setGraphicsEffect(m_opacityEffect);
        }
        m_opacityEffect->setOpacity(m_opacity);
    }
    update();

    if (qFuzzyIsNull(m_opacity) && !m_isVisible && !m_isHiding) {
        hide();
        emit hidden();
    }
}

void LayoutPresetsPopup::setDisplayHeight(int h)
{
    m_displayHeight = h;
    setFixedHeight(h);
    update();

    if (h == m_targetHeight) {
        m_isAnimatingHeight = false;
    }
    if (isVisible() && m_isVisible) {
        emit contentChanged();
    }
}

void LayoutPresetsPopup::ensureHeightAnim()
{
    if (!m_heightAnim) {
        m_heightAnim = new QPropertyAnimation(this, "displayHeight", this);
        m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
}

QRectF LayoutPresetsPopup::attachedBodyRect() const
{
    const int shadowExtent = attachedShadowExtentPx();
    const int shadowSideExtent = attachedShadowSideExtentPx();
    if (height() <= shadowExtent + 1 || width() <= shadowSideExtent * 2 + 1) {
        return {};
    }
    return QRectF(rect()).adjusted(
        shadowSideExtent, 0.0, -shadowSideExtent - 0.5, -shadowExtent - 0.5);
}

void LayoutPresetsPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    const QRectF body = attachedBodyRect();
    if (!body.isValid()) {
        return;
    }

    const int outerCornerRadius = attachedOuterCornerRadiusPx();
    const int shadowExtent = attachedShadowExtentPx();
    const int shadowSideExtent = attachedShadowSideExtentPx();
    constexpr int radius = ruwa::ui::painting::kAttachedCornerRadius;

    const QPainterPath shape
        = ruwa::ui::painting::attachedPopupPath(body, outerCornerRadius, radius);

    const QRectF shadowBody = body.adjusted(outerCornerRadius, 0.0, -outerCornerRadius, 0.0);
    ruwa::ui::painting::drawAttachedPopupShadow(
        painter, shadowBody, shadowSideExtent, shadowExtent, colors.shadow(255), colors.isDark);

    // Fill (matches MessagePopup / MenuPopup attached surfaces)
    painter.setPen(Qt::NoPen);
    QLinearGradient fillGradient(body.topLeft(), body.bottomLeft());
    fillGradient.setColorAt(0.0, colors.surface);
    fillGradient.setColorAt(
        1.0, ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 100.0 / 102));
    painter.setBrush(fillGradient);
    painter.drawPath(shape);

    // Border (top edge omitted — it merges with the seam)
    const QRectF borderRect = body.adjusted(0.5, 0.0, -0.5, -0.5);
    const QPainterPath borderPath
        = ruwa::ui::painting::attachedPopupBorderPath(borderRect, outerCornerRadius, radius - 0.5);
    QColor borderTop = colors.border;
    QLinearGradient borderGradient(borderRect.topLeft(), borderRect.bottomLeft());
    borderGradient.setColorAt(0.0, borderTop);
    borderGradient.setColorAt(
        1.0, ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
    QPen borderPen;
    borderPen.setBrush(borderGradient);
    borderPen.setWidthF(1.0);
    borderPen.setCosmetic(true);
    borderPen.setCapStyle(Qt::SquareCap);
    borderPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}

QPoint LayoutPresetsPopup::calculatePosition(QWidget* anchor) const
{
    if (!anchor || !parentWidget()) {
        return QPoint();
    }

    QWidget* pw = parentWidget();
    const int margin = windowEdgeMarginPx();
    const int sideInset = attachedShadowSideExtentPx() + attachedOuterCornerRadiusPx();
    const int w = width();

    // Top: glue to the TopBar seam, like MessagePopup / MenuPopup.
    int topY;
    if (auto* oc = qobject_cast<OverlayContainer*>(pw)) {
        topY = oc->messagePopupAnchorY();
    } else {
        topY = pw->mapFromGlobal(anchor->mapToGlobal(QPoint(0, anchor->height()))).y();
    }

    // X: align the visible (narrow) body left edge under the button left edge, then
    // keep the body within the window margins (the transparent shadow pad may overhang).
    QPoint anchorLeft = pw->mapFromGlobal(anchor->mapToGlobal(QPoint(0, 0)));
    QPoint targetPos(anchorLeft.x() - sideInset, topY);

    if (w > 0) {
        const int minX = margin - sideInset;
        const int maxX = qMax(minX, pw->width() - w - margin + sideInset);
        targetPos.setX(qBound(minX, targetPos.x(), maxX));
    }

    QPoint globalPos = pw->mapToGlobal(targetPos);
    QScreen* screen = QApplication::screenAt(globalPos);
    if (screen) {
        QRect screenRect = screen->availableGeometry();
        if (globalPos.x() + w > screenRect.right() - margin) {
            targetPos.setX(targetPos.x() - (globalPos.x() + w - (screenRect.right() - margin)));
        }
        globalPos = pw->mapToGlobal(targetPos);
        if (globalPos.x() < screenRect.left() + margin) {
            targetPos.setX(targetPos.x() + (screenRect.left() + margin - globalPos.x()));
        }
    }
    return targetPos;
}

void LayoutPresetsPopup::showBelow(QWidget* anchor, bool slideFromTop)
{
    if (!anchor || !parentWidget()) {
        return;
    }

    refreshPresets();

    m_isVisible = true;
    m_displayHeight = m_targetHeight;
    setFixedHeight(m_targetHeight); // full size so calculatePosition clamps with final width

    QPoint targetPos = calculatePosition(anchor);
    move(targetPos);
    setPopupOpacity(m_opacity);
    show();
    raise();

    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    m_opacityAnim->stop();
    m_opacityAnim->setDuration(SHOW_DURATION);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnim->start();

    if (slideFromTop) {
        // Reveal by growing the body downward from the TopBar seam (MessagePopup feel).
        ensureHeightAnim();
        m_isAnimatingHeight = true;
        m_displayHeight = 0;
        setFixedHeight(0);
        m_heightAnim->stop();
        m_heightAnim->setDuration(SLIDE_DURATION);
        m_heightAnim->setStartValue(0);
        m_heightAnim->setEndValue(m_targetHeight);
        m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_heightAnim->start();
    }

    emit shown();
    emit contentChanged();
}

void LayoutPresetsPopup::hidePopup()
{
    if (!m_isVisible) {
        return;
    }

    emit aboutToHide();

    m_isVisible = false;
    m_isHiding = true;

    m_opacityAnim->stop();
    m_posAnim->stop();

    m_opacityAnim->setDuration(SLIDE_DURATION);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    // Retract the body upward into the TopBar seam (mirror of the reveal).
    ensureHeightAnim();
    m_isAnimatingHeight = true;
    m_heightAnim->stop();
    m_heightAnim->setDuration(SLIDE_DURATION);
    m_heightAnim->setStartValue(m_displayHeight > 0 ? m_displayHeight : height());
    m_heightAnim->setEndValue(0);
    m_heightAnim->setEasingCurve(QEasingCurve::InCubic);
    m_heightAnim->start();

    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            m_isHiding = false;
            m_isAnimatingHeight = false;
            if (m_heightAnim)
                m_heightAnim->stop();
            hide();
            emit hidden();
        }
    });

    m_opacityAnim->start();
}

void LayoutPresetsPopup::forceHide()
{
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    m_opacityAnim->stop();
    m_posAnim->stop();
    if (m_heightAnim)
        m_heightAnim->stop();
    m_isVisible = false;
    m_isHiding = false;
    m_isAnimatingHeight = false;
    setPopupOpacity(0.0);
    hide();
    emit hidden();
}

void LayoutPresetsPopup::retranslateUi()
{
    applyListChrome();
    refreshPresets();
}

void LayoutPresetsPopup::resizeEvent(QResizeEvent* event)
{
    if (m_isRefreshing) {
        return;
    }
    QWidget::resizeEvent(event);
    if (isVisible() && m_isVisible && !m_isHiding) {
        emit contentChanged();
    }
}

} // namespace ruwa::ui::widgets
