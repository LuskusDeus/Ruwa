// SPDX-License-Identifier: MPL-2.0

#include "features/brush/editor/BrushEditorParameterOverlay.h"

#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/CapsuleButton.h"

#include <QCoreApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace ruwa::ui::windows {

using namespace ruwa::ui::core;
using ruwa::ui::widgets::BrushDynamicsEditorWidget;

namespace {

void makeWidgetTransparent(QWidget* widget)
{
    if (!widget) {
        return;
    }

    widget->setAttribute(Qt::WA_TranslucentBackground);
    widget->setAttribute(Qt::WA_NoSystemBackground);
    widget->setAutoFillBackground(false);
}

class BrushEditorOverlayCloseButton final : public widgets::BaseAnimatedButton {
public:
    explicit BrushEditorOverlayCloseButton(QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setHoverDuration(160);
        setActiveDuration(110);
        setFixedSize(28, 28);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();
        const QRectF r = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = theme.scaled(8);

        QColor fill = ThemeColors::withAlpha(colors.surfaceElevated(), 0);
        fill = ThemeColors::interpolate(fill, colors.overlayHover(), hoverProgress());
        if (isPressed()) {
            fill = colors.overlay(0.14);
        }

        QColor iconColor
            = ThemeColors::interpolate(colors.textDisabled(), colors.text, hoverProgress());
        if (isPressed()) {
            iconColor = colors.text;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawRoundedRect(r, radius, radius);

        const int iconSize = qMax(theme.scaled(10), qMin(width(), height()) - theme.scaled(14));
        const QRect iconRect(
            (width() - iconSize) / 2, (height() - iconSize) / 2, iconSize, iconSize);
        IconProvider::instance()
            .getColoredIcon(IconProvider::StandardIcon::Close, iconColor)
            .paint(&painter, iconRect);
    }
};

} // namespace

BrushEditorParameterOverlay::BrushEditorParameterOverlay(QWidget* parent)
    : QWidget(parent)
{
    makeWidgetTransparent(this);
    setFocusPolicy(Qt::StrongFocus);

    m_panel = new QWidget(this);
    m_panel->setObjectName(QStringLiteral("brush_editor_parameter_overlay_panel"));
    m_panel->setAttribute(Qt::WA_StyledBackground, true);
    m_panelOpacityEffect = new QGraphicsOpacityEffect(m_panel);
    m_panelOpacityEffect->setOpacity(0.0);
    m_panel->setGraphicsEffect(m_panelOpacityEffect);

    auto* panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);

    auto* header = new QWidget(m_panel);
    makeWidgetTransparent(header);
    header->setObjectName(QStringLiteral("brush_editor_parameter_overlay_header"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    m_titleLabel = new QLabel(header);
    m_titleLabel->setObjectName(QStringLiteral("brush_editor_parameter_overlay_title"));
    m_titleLabel->setText(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Parameter Dynamics"));

    m_resetButton = new widgets::CapsuleButton(
        QCoreApplication::translate("BrushEditorParameterOverlay", "Reset"),
        widgets::CapsuleButton::Variant::Secondary, header);
    m_resetButton->setBaseMinimumWidth(0);
    m_resetButton->setBannerBaseHeight(36);
    m_resetButton->setIcon(
        ThemeManager::instance().icons().getIcon(IconProvider::StandardIcon::UndoArrow));
    m_resetButton->setSizeScale(0.78);
    m_resetButton->syncSizeToText();
    connect(m_resetButton, &QPushButton::clicked, this, [this]() {
        if (m_editor) {
            m_editor->resetActiveSourceBinding();
        }
    });

    m_closeButton = new BrushEditorOverlayCloseButton(header);
    connect(m_closeButton, &QPushButton::clicked, this, [this]() { hideOverlay(); });

    auto* titleRowLayout = new QHBoxLayout();
    titleRowLayout->setContentsMargins(ThemeManager::instance().scaled(6), 0, 0, 0);
    titleRowLayout->setSpacing(ThemeManager::instance().scaled(8));
    titleRowLayout->addWidget(m_titleLabel, 1);
    titleRowLayout->addWidget(m_resetButton, 0, Qt::AlignVCenter);
    titleRowLayout->addWidget(m_closeButton, 0, Qt::AlignTop);

    headerLayout->addLayout(titleRowLayout);

    m_editor = new BrushDynamicsEditorWidget(m_panel);
    m_editor->setObjectName(QStringLiteral("brush_editor_parameter_overlay_body"));

    panelLayout->addWidget(header);
    panelLayout->addWidget(m_editor, 1);

    connect(m_editor, &BrushDynamicsEditorWidget::slotChanged, this,
        &BrushEditorParameterOverlay::slotChanged);
    connect(m_editor, &BrushDynamicsEditorWidget::activeSourceChanged, this,
        &BrushEditorParameterOverlay::activeSourceChanged);
    connect(m_editor, &BrushDynamicsEditorWidget::editingFinished, this,
        &BrushEditorParameterOverlay::editingFinished);

    m_dimAnimation = new QVariantAnimation(this);
    m_dimAnimation->setDuration(180);
    connect(m_dimAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_dimProgress = value.toReal();
        update();
    });
    connect(m_dimAnimation, &QVariantAnimation::finished, this, [this]() {
        if (m_isHiding) {
            m_isHiding = false;
            QWidget::hide();
        } else if (m_isShowing) {
            m_isShowing = false;
        }
    });

    m_panelAnimation = new QVariantAnimation(this);
    m_panelAnimation->setDuration(180);
    connect(
        m_panelAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_panelProgress = value.toReal();
            updatePanelPresentation();
        });

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        updateStyles();
        updatePanelGeometry();
    });

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
        resize(parentWidget()->size());
    }
    if (QWidget* hostWindow = window(); hostWindow && hostWindow != parentWidget()) {
        hostWindow->installEventFilter(this);
    }

    updateTexts();
    updateStyles();
    updatePanelGeometry();
    QWidget::hide();
}

BrushEditorParameterOverlay::~BrushEditorParameterOverlay()
{
    setShortcutBlocking(false);
}

void BrushEditorParameterOverlay::showOverlay(const QString& settingKey,
    const QString& settingLabel, const BrushDynamicsSlot& slot,
    const BrushDynamicTargetDef& targetDef)
{
    showOverlay(settingKey, settingLabel, slot, targetDef, CurveAxesConfig {});
}

void BrushEditorParameterOverlay::showOverlay(const QString& settingKey,
    const QString& settingLabel, const BrushDynamicsSlot& slot,
    const BrushDynamicTargetDef& targetDef, CurveAxesConfig curveAxesConfig)
{
    const bool wasActive = isActive();
    m_settingLabel = settingLabel;
    if (m_editor) {
        m_editor->setTarget(settingKey, slot, targetDef, curveAxesConfig);
    }

    if (parentWidget()) {
        resize(parentWidget()->size());
    }

    updatePanelGeometry();
    QWidget::show();
    raise();
    setFocus();

    m_isShowing = true;
    m_isHiding = false;
    m_panelAnimation->stop();
    m_dimAnimation->stop();
    m_panelAnimation->setStartValue(wasActive ? m_panelProgress : 0.0);
    m_panelAnimation->setEndValue(1.0);
    m_panelAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_panelAnimation->start();
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(1.0);
    m_dimAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_dimAnimation->start();
}

void BrushEditorParameterOverlay::hideOverlay()
{
    if (!isVisible() || m_isHiding) {
        return;
    }

    emit editingFinished();

    m_isShowing = false;
    m_isHiding = true;
    m_panelAnimation->stop();
    m_dimAnimation->stop();
    m_panelAnimation->setStartValue(m_panelProgress);
    m_panelAnimation->setEndValue(0.0);
    m_panelAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_panelAnimation->start();
    m_dimAnimation->setStartValue(m_dimProgress);
    m_dimAnimation->setEndValue(0.0);
    m_dimAnimation->setEasingCurve(QEasingCurve::InCubic);
    m_dimAnimation->start();
}

bool BrushEditorParameterOverlay::isActive() const
{
    return isVisible() && !m_isHiding;
}

QString BrushEditorParameterOverlay::settingKey() const
{
    return m_editor ? m_editor->settingKey() : QString();
}

BrushEditorParameterOverlay::BrushInputSourceKey BrushEditorParameterOverlay::activeSource() const
{
    return m_editor ? m_editor->activeSource() : BrushInputSourceKey::TabletPressure;
}

void BrushEditorParameterOverlay::setActiveSource(BrushInputSourceKey source)
{
    if (m_editor) {
        m_editor->setActiveSource(source);
    }
}

void BrushEditorParameterOverlay::setCurveAxesConfig(CurveAxesConfig curveAxesConfig)
{
    if (m_editor) {
        m_editor->setCurveAxesConfig(curveAxesConfig);
    }
}

void BrushEditorParameterOverlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    setShortcutBlocking(window() && window()->isActiveWindow());
}

void BrushEditorParameterOverlay::hideEvent(QHideEvent* event)
{
    setShortcutBlocking(false);
    QWidget::hideEvent(event);
}

void BrushEditorParameterOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    if (m_dimProgress <= 0.001) {
        return;
    }

    QPainter painter(this);
    const int alpha = static_cast<int>(0.52 * 255 * m_dimProgress);
    painter.fillRect(rect(), QColor(0, 0, 0, alpha));
}

void BrushEditorParameterOverlay::mousePressEvent(QMouseEvent* event)
{
    if (m_panel && !m_panel->geometry().contains(event->pos())) {
        hideOverlay();
        event->accept();
        return;
    }

    QWidget::mousePressEvent(event);
}

void BrushEditorParameterOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideOverlay();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void BrushEditorParameterOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePanelGeometry();
}

bool BrushEditorParameterOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        auto* resizeEvent = static_cast<QResizeEvent*>(event);
        resize(resizeEvent->size());
        updatePanelGeometry();
    } else if (watched == window()) {
        if (event->type() == QEvent::WindowActivate) {
            setShortcutBlocking(isVisible());
        } else if (event->type() == QEvent::WindowDeactivate) {
            setShortcutBlocking(false);
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BrushEditorParameterOverlay::setShortcutBlocking(bool blocked)
{
    if (blocked == m_shortcutsBlocked) {
        return;
    }

    m_shortcutsBlocked = blocked;
    if (blocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
    } else {
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void BrushEditorParameterOverlay::updateTexts()
{
    if (m_titleLabel) {
        m_titleLabel->setText(
            QCoreApplication::translate("BrushEditorParameterOverlay", "Parameter Dynamics"));
    }
    if (m_resetButton) {
        m_resetButton->setText(QCoreApplication::translate("BrushEditorParameterOverlay", "Reset"));
        m_resetButton->syncSizeToText();
    }
}

void BrushEditorParameterOverlay::updatePanelGeometry()
{
    if (!m_panel) {
        return;
    }

    const int horizontalMargin = qMax(ThemeManager::instance().scaled(32), qRound(width() * 0.075));
    const int verticalMargin = qMax(ThemeManager::instance().scaled(28), qRound(height() * 0.075));
    const QSize preferredSize = m_panel->sizeHint().expandedTo(
        QSize(ThemeManager::instance().scaled(560), ThemeManager::instance().scaled(460)));
    const int maxWidth = qMax(ThemeManager::instance().scaled(320), width() - horizontalMargin * 2);
    const int maxHeight = qMax(ThemeManager::instance().scaled(240), height() - verticalMargin * 2);
    const QSize boundedSize(
        qMin(preferredSize.width(), maxWidth), qMin(preferredSize.height(), maxHeight));

    QRect targetRect(QPoint(0, 0), boundedSize);
    targetRect.moveCenter(rect().center());
    targetRect.moveLeft(qMax(horizontalMargin, targetRect.left()));
    targetRect.moveTop(qMax(verticalMargin, targetRect.top()));
    if (targetRect.right() > width() - horizontalMargin) {
        targetRect.moveRight(width() - horizontalMargin);
    }
    if (targetRect.bottom() > height() - verticalMargin) {
        targetRect.moveBottom(height() - verticalMargin);
    }

    m_targetPanelRect = targetRect;
    updatePanelPresentation();
}

void BrushEditorParameterOverlay::updatePanelPresentation()
{
    if (!m_panel) {
        return;
    }

    const int slideOffset = ThemeManager::instance().scaled(18);
    QRect panelRect = m_targetPanelRect.isNull() ? m_panel->geometry() : m_targetPanelRect;
    panelRect.translate(0, qRound((1.0 - m_panelProgress) * slideOffset));
    m_panel->setGeometry(panelRect);
    if (m_panelOpacityEffect) {
        m_panelOpacityEffect->setOpacity(m_panelProgress);
    }
}

void BrushEditorParameterOverlay::updateStyles()
{
    auto& theme = ThemeManager::instance();
    const auto& colors = WidgetStyleManager::instance().colors();

    m_titleLabel->setFont(theme.font(ThemeFontRole::H6, QFont::Bold));

    const QString panelStyle
        = QStringLiteral("QWidget#brush_editor_parameter_overlay_panel { background: %1; border: "
                         "1px solid %2; border-radius: %3px; }")
              .arg(colors.surfaceElevated().name(QColor::HexArgb),
                  colors.borderSubtleHover().name(QColor::HexArgb),
                  QString::number(theme.scaled(18)));
    m_panel->setStyleSheet(panelStyle);
    m_titleLabel->setStyleSheet(QStringLiteral("QLabel { background: transparent; color: %1; }")
            .arg(colors.text.name(QColor::HexArgb)));

    if (auto* layout = qobject_cast<QVBoxLayout*>(m_panel->layout())) {
        layout->setContentsMargins(
            theme.scaled(12), theme.scaled(12), theme.scaled(12), theme.scaled(12));
        layout->setSpacing(theme.scaled(12));
    }
    if (auto* headerLayout
        = qobject_cast<QVBoxLayout*>(m_panel->layout()->itemAt(0)->widget()->layout())) {
        headerLayout->setSpacing(0);
    }
    if (auto* titleRowLayout = qobject_cast<QHBoxLayout*>(
            m_panel->layout()->itemAt(0)->widget()->layout()->itemAt(0)->layout())) {
        titleRowLayout->setContentsMargins(theme.scaled(6), 0, 0, 0);
        titleRowLayout->setSpacing(theme.scaled(8));
    }
    if (auto* closeButton = static_cast<BrushEditorOverlayCloseButton*>(m_closeButton)) {
        closeButton->setFixedSize(theme.scaled(28), theme.scaled(28));
        closeButton->update();
    }
    if (m_resetButton) {
        m_resetButton->setIcon(theme.icons().getIcon(IconProvider::StandardIcon::UndoArrow));
        m_resetButton->setBannerBaseHeight(36);
        m_resetButton->setSizeScale(0.78);
        m_resetButton->setBaseMinimumWidth(0);
        m_resetButton->syncSizeToText();
    }
    m_panel->style()->unpolish(m_panel);
    m_panel->style()->polish(m_panel);
}

} // namespace ruwa::ui::windows
