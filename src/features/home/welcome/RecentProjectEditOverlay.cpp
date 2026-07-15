// SPDX-License-Identifier: MPL-2.0

#include "RecentProjectEditOverlay.h"

#include "commands/ShortcutManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/inputs/ToggleSwitch.h"

#include <QAbstractButton>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

namespace {
const int BASE_CARD_WIDTH = 440;
const int BASE_CARD_PADDING = 20;
const int BASE_CARD_RADIUS = 14;
const int BASE_CARD_SPACING = 14;
const int BASE_TITLE_FONT_SIZE = 14;
const int BASE_BODY_FONT_SIZE = 9;
const int BASE_CAPTION_FONT_SIZE = 8;
const int BASE_INPUT_HEIGHT = 34;
const int BASE_TOGGLE_ICON_SIZE = 16;
const int BASE_BUTTON_HEIGHT = 32;
const int BASE_BUTTON_WIDTH = 96;
} // namespace

RecentProjectEditOverlay::RecentProjectEditOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setFocusPolicy(Qt::StrongFocus);
    hide();

    if (parentWidget()) {
        parentWidget()->installEventFilter(this);
        setGeometry(parentWidget()->rect());
    }

    buildUi();
    setupAnimations();
    applyChrome();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &RecentProjectEditOverlay::applyChrome);
}

RecentProjectEditOverlay::~RecentProjectEditOverlay()
{
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
}

void RecentProjectEditOverlay::showForProject(
    const QString& filePath, const QString& projectName, bool previewEnabled)
{
    m_filePath = filePath;
    m_nameEdit->setText(projectName);
    {
        const QSignalBlocker blocker(m_previewSwitch);
        m_previewSwitch->setChecked(previewEnabled, ToggleSwitch::TransitionMode::Instant);
    }

    updateSaveButtonState();
    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }

    if (m_isHiding) {
        if (m_dimAnimation)
            m_dimAnimation->stop();
        if (m_cardOpacityAnim)
            m_cardOpacityAnim->stop();
        if (m_cardPosAnim)
            m_cardPosAnim->stop();
        m_isHiding = false;
    }

    if (isVisible() && !m_isHiding) {
        m_isShowing = false;
        updateCardPosition();
        if (m_cardOpacityEffect) {
            m_cardOpacityEffect->setOpacity(1.0);
        }
        raise();
    } else {
        m_isShowing = true;
        updateCardPosition();
        QWidget::show();
        raise();

        const QPoint targetPos = cardTargetPosition();
        const QPoint startPos = targetPos + QPoint(0, SlideOffset);
        m_card->move(startPos);
        if (m_cardOpacityEffect) {
            m_cardOpacityEffect->setOpacity(0.0);
        }

        if (m_dimAnimation) {
            m_dimAnimation->stop();
            m_dimAnimation->setEasingCurve(QEasingCurve::OutCubic);
            m_dimAnimation->setStartValue(m_dimProgress);
            m_dimAnimation->setEndValue(1.0);
            m_dimAnimation->start();
        }

        if (m_cardOpacityAnim) {
            m_cardOpacityAnim->stop();
            m_cardOpacityAnim->setDuration(CardAnimationDuration);
            m_cardOpacityAnim->setStartValue(0.0);
            m_cardOpacityAnim->setEndValue(1.0);
            m_cardOpacityAnim->start();
        }

        if (m_cardPosAnim) {
            m_cardPosAnim->stop();
            m_cardPosAnim->setDuration(CardAnimationDuration);
            m_cardPosAnim->setStartValue(startPos);
            m_cardPosAnim->setEndValue(targetPos);
            m_cardPosAnim->start();
        }
    }

    if (!m_shortcutsBlocked) {
        ruwa::core::ShortcutManager::instance().pushShortcutsDisabled();
        m_shortcutsBlocked = true;
    }
    activateWindow();
    setFocus(Qt::ActiveWindowFocusReason);
    m_nameEdit->setFocus(Qt::ActiveWindowFocusReason);
    m_nameEdit->selectAll();
}

void RecentProjectEditOverlay::hideOverlay()
{
    if (m_isHiding || !isVisible()) {
        return;
    }

    m_isHiding = true;
    m_isShowing = false;

    if (m_dimAnimation) {
        m_dimAnimation->stop();
        m_dimAnimation->setEasingCurve(QEasingCurve::InCubic);
        m_dimAnimation->setStartValue(m_dimProgress);
        m_dimAnimation->setEndValue(0.0);
        m_dimAnimation->start();
    }

    const QPoint currentPos = m_card ? m_card->pos() : QPoint();
    const QPoint endPos = currentPos + QPoint(0, SlideOffset);

    if (m_cardOpacityAnim) {
        m_cardOpacityAnim->stop();
        m_cardOpacityAnim->setDuration(CardAnimationDuration);
        m_cardOpacityAnim->setStartValue(
            m_cardOpacityEffect ? m_cardOpacityEffect->opacity() : 1.0);
        m_cardOpacityAnim->setEndValue(0.0);
        m_cardOpacityAnim->start();
    }

    if (m_cardPosAnim) {
        m_cardPosAnim->stop();
        m_cardPosAnim->setDuration(CardAnimationDuration);
        m_cardPosAnim->setStartValue(currentPos);
        m_cardPosAnim->setEndValue(endPos);
        m_cardPosAnim->start();
    }
}

bool RecentProjectEditOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget()) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
            if (parentWidget()) {
                setGeometry(parentWidget()->rect());
                if (m_cardPosAnim && m_cardPosAnim->state() == QAbstractAnimation::Running) {
                    const QPoint targetPos = cardTargetPosition();
                    if (!m_isHiding) {
                        m_cardPosAnim->setEndValue(targetPos);
                    }
                } else {
                    updateCardPosition();
                }
            }
            break;
        default:
            break;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void RecentProjectEditOverlay::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_dimProgress <= 0.001) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int alpha = static_cast<int>(MaxDimOpacity * 255.0 * m_dimProgress);
    painter.fillRect(rect(), QColor(0, 0, 0, alpha));
}

void RecentProjectEditOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_card && !m_card->geometry().contains(event->pos())) {
        hideOverlay();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void RecentProjectEditOverlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_cardPosAnim && m_cardPosAnim->state() == QAbstractAnimation::Running) {
        const QPoint targetPos = cardTargetPosition();
        if (!m_isHiding) {
            m_cardPosAnim->setEndValue(targetPos);
        }
        return;
    }

    updateCardPosition();
}

void RecentProjectEditOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        hideOverlay();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void RecentProjectEditOverlay::buildUi()
{
    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("recentProjectEditCard"));
    m_cardOpacityEffect = new QGraphicsOpacityEffect(m_card);
    m_cardOpacityEffect->setOpacity(0.0);
    m_card->setGraphicsEffect(m_cardOpacityEffect);

    auto* cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(20, 20, 20, 20);
    cardLayout->setSpacing(14);

    m_titleLabel = new QLabel(tr("Edit Recent Project"), m_card);
    cardLayout->addWidget(m_titleLabel);

    m_captionLabel
        = new QLabel(tr("Changes apply to the recent projects list immediately."), m_card);
    m_captionLabel->setWordWrap(true);
    cardLayout->addWidget(m_captionLabel);

    m_nameLabel = new QLabel(tr("Project Name"), m_card);
    cardLayout->addWidget(m_nameLabel);

    m_nameEdit = new QLineEdit(m_card);
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { updateSaveButtonState(); });
    connect(m_nameEdit, &QLineEdit::returnPressed, this, [this]() {
        if (!m_saveButton || !m_saveButton->isEnabled()) {
            return;
        }
        emit saveRequested(m_filePath, m_nameEdit->text().trimmed(), m_previewSwitch->isChecked());
        hideOverlay();
    });
    cardLayout->addWidget(m_nameEdit);

    auto* previewRow = new QWidget(m_card);
    auto* previewLayout = new QHBoxLayout(previewRow);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->setSpacing(12);

    m_previewIconLabel = new QLabel(previewRow);
    previewLayout->addWidget(m_previewIconLabel, 0, Qt::AlignTop);

    auto* previewTextWrap = new QWidget(previewRow);
    auto* previewTextLayout = new QVBoxLayout(previewTextWrap);
    previewTextLayout->setContentsMargins(0, 0, 0, 0);
    previewTextLayout->setSpacing(2);

    m_previewTitleLabel = new QLabel(tr("Project Preview"), previewTextWrap);
    previewTextLayout->addWidget(m_previewTitleLabel);

    m_previewBodyLabel = new QLabel(
        tr("Turn this off to replace the thumbnail with a hidden-preview placeholder."),
        previewTextWrap);
    m_previewBodyLabel->setWordWrap(true);
    previewTextLayout->addWidget(m_previewBodyLabel);

    previewLayout->addWidget(previewTextWrap, 1);

    m_previewSwitch = new ToggleSwitch(previewRow);
    previewLayout->addWidget(m_previewSwitch, 0, Qt::AlignVCenter);

    cardLayout->addWidget(previewRow);

    auto* buttonsRow = new QWidget(m_card);
    auto* buttonsLayout = new QHBoxLayout(buttonsRow);
    buttonsLayout->setContentsMargins(0, 6, 0, 0);
    buttonsLayout->setSpacing(8);
    buttonsLayout->addStretch();

    m_cancelButton = new CapsuleButton(tr("Cancel"), CapsuleButton::Variant::Secondary, buttonsRow);
    m_cancelButton->setBaseMinimumWidth(BASE_BUTTON_WIDTH);
    m_cancelButton->setBannerBaseHeight(BASE_BUTTON_HEIGHT);
    m_cancelButton->setSizeScale(0.82);
    connect(
        m_cancelButton, &QAbstractButton::clicked, this, &RecentProjectEditOverlay::hideOverlay);
    buttonsLayout->addWidget(m_cancelButton);

    m_saveButton = new CapsuleButton(tr("Save"), CapsuleButton::Variant::Primary, buttonsRow);
    m_saveButton->setBaseMinimumWidth(BASE_BUTTON_WIDTH);
    m_saveButton->setBannerBaseHeight(BASE_BUTTON_HEIGHT);
    m_saveButton->setSizeScale(0.82);
    connect(m_saveButton, &QAbstractButton::clicked, this, [this]() {
        emit saveRequested(m_filePath, m_nameEdit->text().trimmed(), m_previewSwitch->isChecked());
        hideOverlay();
    });
    buttonsLayout->addWidget(m_saveButton);

    cardLayout->addWidget(buttonsRow);
}

void RecentProjectEditOverlay::setupAnimations()
{
    m_dimAnimation = new QPropertyAnimation(this, "dimProgress", this);
    m_dimAnimation->setDuration(DimAnimationDuration);

    m_cardOpacityAnim = new QPropertyAnimation(m_cardOpacityEffect, "opacity", this);
    m_cardOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_cardPosAnim = new QPropertyAnimation(m_card, "pos", this);
    m_cardPosAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_dimAnimation, &QPropertyAnimation::finished, this,
        &RecentProjectEditOverlay::onDimAnimationFinished);
    connect(m_cardOpacityAnim, &QPropertyAnimation::finished, this,
        &RecentProjectEditOverlay::onCardHideAnimationFinished);
}

void RecentProjectEditOverlay::applyChrome()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();

    if (parentWidget()) {
        setGeometry(parentWidget()->rect());
    }

    m_card->setFixedWidth(theme.scaled(BASE_CARD_WIDTH));
    m_card->setStyleSheet(QString(R"(
        QWidget#recentProjectEditCard {
            background-color: %1;
            border: 1px solid %2;
            border-radius: %3px;
        }
    )")
            .arg(colors.surfaceElevated().name(QColor::HexArgb))
            .arg(colors.borderSubtleHover().name(QColor::HexArgb))
            .arg(theme.scaled(BASE_CARD_RADIUS)));

    if (auto* layout = qobject_cast<QVBoxLayout*>(m_card->layout())) {
        layout->setContentsMargins(theme.scaled(BASE_CARD_PADDING), theme.scaled(BASE_CARD_PADDING),
            theme.scaled(BASE_CARD_PADDING), theme.scaled(BASE_CARD_PADDING));
        layout->setSpacing(theme.scaled(BASE_CARD_SPACING));
    }

    if (m_titleLabel) {
        QFont f = colors.fonts.getTitleFont(theme.scaledFontSize(BASE_TITLE_FONT_SIZE));
        m_titleLabel->setFont(f);
        QPalette pal = m_titleLabel->palette();
        pal.setColor(QPalette::WindowText, colors.text);
        m_titleLabel->setPalette(pal);
    }

    const auto applyMutedLabel = [&colors, &theme](QLabel* label, int fontSize) {
        if (!label)
            return;
        QFont f = colors.fonts.getUIFont(theme.scaledFontSize(fontSize));
        label->setFont(f);
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, colors.textMuted);
        label->setPalette(pal);
    };

    applyMutedLabel(m_captionLabel, BASE_CAPTION_FONT_SIZE);
    applyMutedLabel(m_previewBodyLabel, BASE_CAPTION_FONT_SIZE);

    if (m_nameLabel) {
        QFont f = colors.fonts.getUIFont(theme.scaledFontSize(BASE_BODY_FONT_SIZE));
        f.setWeight(QFont::DemiBold);
        m_nameLabel->setFont(f);
        QPalette pal = m_nameLabel->palette();
        pal.setColor(QPalette::WindowText, colors.text);
        m_nameLabel->setPalette(pal);
    }

    if (m_previewTitleLabel) {
        QFont f = colors.fonts.getUIFont(theme.scaledFontSize(BASE_BODY_FONT_SIZE));
        f.setWeight(QFont::DemiBold);
        m_previewTitleLabel->setFont(f);
        QPalette pal = m_previewTitleLabel->palette();
        pal.setColor(QPalette::WindowText, colors.text);
        m_previewTitleLabel->setPalette(pal);
    }

    if (m_nameEdit) {
        m_nameEdit->setFixedHeight(theme.scaled(BASE_INPUT_HEIGHT));
        m_nameEdit->setFont(colors.fonts.getUIFont(theme.scaledFontSize(BASE_BODY_FONT_SIZE)));
        m_nameEdit->setStyleSheet(QString("QLineEdit {"
                                          "  background: %1;"
                                          "  border: 1px solid %2;"
                                          "  border-radius: %3px;"
                                          "  color: %4;"
                                          "  padding: 4px 10px;"
                                          "  selection-background-color: %5;"
                                          "}"
                                          "QLineEdit:focus { border-color: %5; }")
                .arg(colors.overlayBase().name(QColor::HexArgb))
                .arg(colors.borderSubtle().name(QColor::HexArgb))
                .arg(theme.scaled(6))
                .arg(colors.text.name())
                .arg(colors.primary.name()));
    }

    if (m_previewIconLabel) {
        const int iconSize = theme.scaled(BASE_TOGGLE_ICON_SIZE);
        m_previewIconLabel->setFixedSize(iconSize, iconSize);
        m_previewIconLabel->setPixmap(ruwa::ui::core::IconProvider::instance()
                .getColoredIcon(
                    ruwa::ui::core::IconProvider::StandardIcon::EyeDeactivated, colors.textMuted)
                .pixmap(iconSize, iconSize));
    }

    if (!isVisible() || (!m_isShowing && !m_isHiding)) {
        updateCardPosition();
    }
}

void RecentProjectEditOverlay::updateCardPosition()
{
    if (!m_card) {
        return;
    }

    m_card->adjustSize();
    const QSize hint = m_card->sizeHint();
    const QPoint targetPos = cardTargetPosition();
    m_card->setGeometry(targetPos.x(), targetPos.y(), hint.width(), hint.height());
}

void RecentProjectEditOverlay::updateSaveButtonState()
{
    if (!m_saveButton) {
        return;
    }
    m_saveButton->setEnabled(!m_filePath.isEmpty() && !m_nameEdit->text().trimmed().isEmpty());
}

QPoint RecentProjectEditOverlay::cardTargetPosition() const
{
    if (!m_card) {
        return {};
    }

    const QSize hint = m_card->sizeHint();
    const int x = (width() - hint.width()) / 2;
    const int y = (height() - hint.height()) / 2;
    return QPoint(qMax(0, x), qMax(0, y));
}

void RecentProjectEditOverlay::setDimProgress(qreal progress)
{
    if (qFuzzyCompare(m_dimProgress, progress)) {
        return;
    }

    m_dimProgress = progress;
    update();
}

void RecentProjectEditOverlay::onDimAnimationFinished()
{
    if (m_isShowing) {
        m_isShowing = false;
    }
}

void RecentProjectEditOverlay::onCardHideAnimationFinished()
{
    if (!m_isHiding) {
        return;
    }

    m_isHiding = false;
    if (m_shortcutsBlocked) {
        m_shortcutsBlocked = false;
        ruwa::core::ShortcutManager::instance().popShortcutsDisabled();
    }
    QWidget::hide();
    m_filePath.clear();
}

} // namespace ruwa::ui::widgets
