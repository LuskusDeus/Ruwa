// SPDX-License-Identifier: MPL-2.0

#include "features/brush/ui/BrushDynamicsPopup.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/widgets/CapsuleButton.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {
constexpr int kShowDurationMs = 140;
constexpr int kShowSlideOffset = 10;
} // namespace

BrushDynamicsPopup::BrushDynamicsPopup(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("brush_dynamics_popup"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    hide();

    auto& theme = ThemeManager::instance();

    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(
        theme.scaled(12), theme.scaled(10), theme.scaled(12), theme.scaled(12));
    rootLayout->setSpacing(theme.scaled(10));

    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(theme.scaled(8));

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("brush_dynamics_popup_title"));

    m_resetButton
        = new CapsuleButton(QCoreApplication::translate("BrushEditorParameterOverlay", "Reset"),
            CapsuleButton::Variant::Secondary, this);
    m_resetButton->setBaseMinimumWidth(0);
    m_resetButton->setBannerBaseHeight(32);
    m_resetButton->setIcon(theme.icons().getIcon(IconProvider::StandardIcon::UndoArrow));
    m_resetButton->setSizeScale(0.72);
    m_resetButton->syncSizeToText();
    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        if (m_editor) {
            m_editor->resetActiveSourceBinding();
        }
    });

    m_closeButton = new QPushButton(this);
    m_closeButton->setObjectName(QStringLiteral("brush_dynamics_popup_close"));
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setFocusPolicy(Qt::NoFocus);
    m_closeButton->setFlat(true);
    connect(m_closeButton, &QPushButton::clicked, this, [this]() { hidePopup(); });

    headerLayout->addWidget(m_titleLabel, 1);
    headerLayout->addWidget(m_resetButton, 0, Qt::AlignVCenter);
    headerLayout->addWidget(m_closeButton, 0, Qt::AlignVCenter);

    m_editor = new BrushDynamicsEditorWidget(this);
    m_editor->setCompact(true);

    rootLayout->addLayout(headerLayout);
    rootLayout->addWidget(m_editor, 1);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_showAnimation = new QVariantAnimation(this);
    m_showAnimation->setDuration(kShowDurationMs);
    m_showAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_showAnimation->setStartValue(0.0);
    m_showAnimation->setEndValue(1.0);
    connect(m_showAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { applyShowProgress(value.toReal()); });
    connect(m_showAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_closing && m_showAnimation->direction() == QAbstractAnimation::Backward) {
            m_closing = false;
            hide();
            m_anchor = nullptr;
            emit editingFinished();
            emit closed();
        }
    });

    connect(
        m_editor, &BrushDynamicsEditorWidget::slotChanged, this, &BrushDynamicsPopup::slotChanged);
    connect(m_editor, &BrushDynamicsEditorWidget::editingFinished, this,
        &BrushDynamicsPopup::editingFinished);
    connect(
        &ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() { updateStyles(); });

    updateStyles();
}

void BrushDynamicsPopup::showForSetting(const QString& settingKey, const QString& settingLabel,
    const BrushDynamicsSlot& slot, const BrushDynamicTargetDef& targetDef,
    CurveAxesConfig curveAxesConfig, QWidget* anchor, const QPoint& anchorGlobalPos)
{
    QWidget* host = anchor ? anchor->window() : window();
    if (host && parentWidget() != host) {
        setParent(host, Qt::Widget);
    }
    if (m_filtersInstalled && m_host && m_host.data() != host) {
        m_host->removeEventFilter(this);
        m_filtersInstalled = false;
    }
    m_host = host;
    m_anchor = anchor;

    if (m_titleLabel) {
        m_titleLabel->setText(settingLabel.isEmpty()
                ? QCoreApplication::translate("BrushEditorParameterOverlay", "Parameter Dynamics")
                : settingLabel);
    }
    if (m_editor) {
        m_editor->setTarget(settingKey, slot, targetDef, curveAxesConfig);
    }

    adjustSize();
    updatePosition(anchorGlobalPos);

    const bool alreadyOpen = isVisible() && !m_closing;
    m_closing = false;
    m_showAnimation->setDirection(QAbstractAnimation::Forward);
    if (alreadyOpen) {
        // The popup is already on screen for a different parameter: just move
        // its content into place without replaying the entrance animation.
        m_showAnimation->stop();
        applyShowProgress(1.0);
    } else {
        // Either fully hidden, or mid fade-out from a previous close: play the
        // entrance animation forward from wherever it currently sits.
        if (!isVisible()) {
            applyShowProgress(0.0);
        }
        show();
        anim::start(m_showAnimation);
    }
    raise();
    setFocus(Qt::PopupFocusReason);

    if (!m_filtersInstalled) {
        qApp->installEventFilter(this);
        if (m_host) {
            m_host->installEventFilter(this);
        }
        m_filtersInstalled = true;
    }
}

void BrushDynamicsPopup::hidePopup()
{
    if (!isVisible() || m_closing) {
        return;
    }

    m_closing = true;
    qApp->removeEventFilter(this);
    if (m_host) {
        m_host->removeEventFilter(this);
    }
    m_filtersInstalled = false;

    m_showAnimation->stop();
    m_showAnimation->setDirection(QAbstractAnimation::Backward);
    anim::start(m_showAnimation);
}

bool BrushDynamicsPopup::isPopupVisible() const
{
    return isVisible();
}

QString BrushDynamicsPopup::settingKey() const
{
    return m_editor ? m_editor->settingKey() : QString();
}

QWidget* BrushDynamicsPopup::anchorWidget() const
{
    return isVisible() ? m_anchor.data() : nullptr;
}

void BrushDynamicsPopup::setCurveAxesConfig(CurveAxesConfig curveAxesConfig)
{
    if (m_editor) {
        m_editor->setCurveAxesConfig(curveAxesConfig);
    }
}

void BrushDynamicsPopup::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hidePopup();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool BrushDynamicsPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (!isVisible()) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        const QRect popupRect(mapToGlobal(QPoint(0, 0)), size());
        const bool insidePopup = popupRect.contains(globalPos);
        const bool onAnchor = m_anchor
            && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size()).contains(globalPos);
        // A press on the anchor button is left for the button's own click
        // handler (fired on release) to interpret as a toggle-close, rather
        // than closed here and immediately re-opened by that same click.
        if (!insidePopup && !onAnchor) {
            hidePopup();
        }
        break;
    }
    case QEvent::ApplicationDeactivate:
    case QEvent::WindowDeactivate:
        hidePopup();
        break;
    case QEvent::Resize:
    case QEvent::Move:
        if (watched == m_host) {
            hidePopup();
        }
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void BrushDynamicsPopup::updatePosition(const QPoint& anchorGlobalPos)
{
    QWidget* host = m_host ? m_host.data() : parentWidget();
    if (!host) {
        return;
    }

    auto& theme = ThemeManager::instance();
    const int margin = theme.scaled(8);
    const QRect hostBounds = host->rect().adjusted(margin, margin, -margin, -margin);
    if (!hostBounds.isValid()) {
        return;
    }

    QPoint target = host->mapFromGlobal(anchorGlobalPos) + QPoint(theme.scaled(10), 0);
    target.setY(target.y() - height() / 2);

    const int maxX = qMax(hostBounds.left(), hostBounds.right() - width() + 1);
    const int maxY = qMax(hostBounds.top(), hostBounds.bottom() - height() + 1);
    target.setX(qBound(hostBounds.left(), target.x(), maxX));
    target.setY(qBound(hostBounds.top(), target.y(), maxY));
    m_targetPos = target;
    move(m_targetPos);
}

void BrushDynamicsPopup::applyShowProgress(qreal progress)
{
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(qBound(0.0, progress, 1.0));
    }
    const int offset = qRound((1.0 - qBound(0.0, progress, 1.0)) * kShowSlideOffset);
    move(m_targetPos + QPoint(0, offset));
}

void BrushDynamicsPopup::updateStyles()
{
    auto& theme = ThemeManager::instance();
    const auto& colors = WidgetStyleManager::instance().colors();

    setStyleSheet(QStringLiteral("QWidget#brush_dynamics_popup { background: %1; border: 1px "
                                 "solid %2; border-radius: %3px; }")
            .arg(colors.surfaceElevated().name(QColor::HexArgb),
                colors.borderSubtleHover().name(QColor::HexArgb),
                QString::number(theme.scaled(14))));

    if (m_titleLabel) {
        m_titleLabel->setFont(theme.font(ThemeFontRole::Body, QFont::Bold));
        m_titleLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }
    if (m_closeButton) {
        const int iconSize = theme.scaled(10);
        m_closeButton->setFixedSize(theme.scaled(22), theme.scaled(22));
        m_closeButton->setIcon(IconProvider::instance().getColoredIcon(
            IconProvider::StandardIcon::Close, colors.textMuted));
        m_closeButton->setIconSize(QSize(iconSize, iconSize));
        m_closeButton->setStyleSheet(
            QStringLiteral("QPushButton { border: none; background: transparent; padding: 0; }"));
    }
    if (m_resetButton) {
        m_resetButton->setIcon(theme.icons().getIcon(IconProvider::StandardIcon::UndoArrow));
        m_resetButton->setBannerBaseHeight(32);
        m_resetButton->setSizeScale(0.72);
        m_resetButton->setBaseMinimumWidth(0);
        m_resetButton->syncSizeToText();
    }
    if (auto* rootLayout = qobject_cast<QVBoxLayout*>(layout())) {
        rootLayout->setContentsMargins(
            theme.scaled(12), theme.scaled(10), theme.scaled(12), theme.scaled(12));
        rootLayout->setSpacing(theme.scaled(10));
    }

    style()->unpolish(this);
    style()->polish(this);
}

} // namespace ruwa::ui::widgets
