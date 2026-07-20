// SPDX-License-Identifier: MPL-2.0

// SearchBar.cpp
#include "SearchBar.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"

#include <QApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>

namespace ruwa::ui::widgets {

SearchBar::SearchBar(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setupAnimations();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &SearchBar::onThemeChanged);
}

SearchBar::~SearchBar()
{
    if (m_appFilterInstalled) {
        qApp->removeEventFilter(this);
        m_appFilterInstalled = false;
    }
    delete m_focusAnimation;
    delete m_hoverAnimation;
}

void SearchBar::setupAnimations()
{
    m_focusAnimation = new QPropertyAnimation(this, "focusProgress");
    m_focusAnimation->setDuration(250);
    m_focusAnimation->setEasingCurve(QEasingCurve::InOutCubic);

    m_hoverAnimation = new QPropertyAnimation(this, "hoverProgress");
    m_hoverAnimation->setDuration(200);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
}

void SearchBar::setupUI()
{
    setFixedHeight(40);
    setMinimumWidth(250);

    auto& icons = ruwa::ui::core::ThemeManager::instance().icons();
    m_searchIcon = icons.getIcon(ruwa::ui::core::IconProvider::StandardIcon::Find);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(40, 0, 12, 0);
    layout->setSpacing(0);

    m_lineEdit = new QLineEdit(this);
    m_lineEdit->setFrame(false);
    m_lineEdit->setAttribute(Qt::WA_TranslucentBackground);
    m_lineEdit->setPlaceholderText(tr("Search projects..."));
    m_lineEdit->installEventFilter(this);

    QFont font = m_lineEdit->font();
    font.setPointSize(9);
    m_lineEdit->setFont(font);

    layout->addWidget(m_lineEdit);
    setFocusProxy(m_lineEdit);

    connect(m_lineEdit, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);
    connect(m_lineEdit, &QLineEdit::returnPressed, this,
        [this]() { emit searchRequested(m_lineEdit->text()); });

    updateThemeColors();
}

bool SearchBar::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_lineEdit) {
        switch (event->type()) {
        case QEvent::FocusIn:
            startFocusAnimation(true);
            if (m_clickOutsideClearsFocus && !m_appFilterInstalled) {
                qApp->installEventFilter(this);
                m_appFilterInstalled = true;
            }
            break;
        case QEvent::FocusOut:
            startFocusAnimation(false);
            if (m_appFilterInstalled) {
                qApp->removeEventFilter(this);
                m_appFilterInstalled = false;
            }
            break;
        case QEvent::Enter:
            startHoverAnimation(true);
            break;
        case QEvent::Leave:
            startHoverAnimation(false);
            break;
        default:
            break;
        }
    } else if (m_appFilterInstalled
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::NonClientAreaMouseButtonPress)) {
        if (auto* mouseEvent = static_cast<QMouseEvent*>(event)) {
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();
            const QRect ourRect(mapToGlobal(QPoint(0, 0)), size());
            if (!ourRect.contains(globalPos)) {
                clearFocus();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void SearchBar::startFocusAnimation(bool focused)
{
    m_focusAnimation->stop();
    m_focusAnimation->setStartValue(m_focusProgress);
    m_focusAnimation->setEndValue(focused ? 1.0 : 0.0);
    m_focusAnimation->start();
}

void SearchBar::startHoverAnimation(bool hovered)
{
    m_hoverAnimation->stop();
    m_hoverAnimation->setEasingCurve(hovered ? QEasingCurve::OutCubic : QEasingCurve::InOutCubic);
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(hovered ? 1.0 : 0.0);
    m_hoverAnimation->start();
}

void SearchBar::setFocusProgress(qreal progress)
{
    m_focusProgress = progress;
    update();
}

void SearchBar::setHoverProgress(qreal progress)
{
    m_hoverProgress = progress;
    update();
}

void SearchBar::setPlaceholder(const QString& text)
{
    m_customPlaceholder = true;
    if (m_lineEdit) {
        m_lineEdit->setPlaceholderText(text);
    }
}

void SearchBar::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_lineEdit && !m_customPlaceholder) {
        m_lineEdit->setPlaceholderText(tr("Search projects..."));
    }
}

void SearchBar::setBarHeight(int height)
{
    setFixedHeight(qMax(1, height));
    // Keep the search glyph vertically centred; the left text inset already
    // clears the icon at any reasonable height.
    update();
}

void SearchBar::setMinimumBarWidth(int width)
{
    setMinimumWidth(qMax(0, width));
}

QString SearchBar::text() const
{
    return m_lineEdit ? m_lineEdit->text() : QString();
}

void SearchBar::setText(const QString& text)
{
    if (m_lineEdit) {
        m_lineEdit->setText(text);
    }
}

void SearchBar::clear()
{
    if (m_lineEdit) {
        m_lineEdit->clear();
    }
}

void SearchBar::setClearButtonEnabled(bool enabled)
{
    if (m_lineEdit) {
        m_lineEdit->setClearButtonEnabled(enabled);
    }
}

void SearchBar::setClickOutsideClearsFocus(bool enabled)
{
    if (m_clickOutsideClearsFocus == enabled) {
        return;
    }
    m_clickOutsideClearsFocus = enabled;
    if (!enabled && m_appFilterInstalled) {
        qApp->removeEventFilter(this);
        m_appFilterInstalled = false;
    }
    if (enabled && hasFocus() && !m_appFilterInstalled) {
        qApp->installEventFilter(this);
        m_appFilterInstalled = true;
    }
}

void SearchBar::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    using TC = ruwa::ui::core::ThemeColors;
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = rect.height() / 2.0;

    if (m_hoverProgress > 0) {
        QColor hoverBg = colors.surfaceElevated();
        hoverBg.setAlpha(qBound(0, int(m_hoverProgress * 90), 255));
        painter.setPen(Qt::NoPen);
        painter.setBrush(hoverBg);
        painter.drawRoundedRect(rect, radius, radius);
    }

    QColor inactiveTop
        = TC::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), m_hoverProgress);
    QColor inactiveBottom = TC::withAlpha(inactiveTop, inactiveTop.alpha() / 2);

    QColor focusTop = colors.primary;
    focusTop.setAlpha(int(180 + m_hoverProgress * 40));
    QColor focusBottom = colors.primary;
    focusBottom.setAlpha(int(120 + m_hoverProgress * 30));

    const QColor borderTop = TC::interpolate(inactiveTop, focusTop, m_focusProgress);
    const QColor borderBottom = TC::interpolate(inactiveBottom, focusBottom, m_focusProgress);

    ruwa::ui::painting::drawGradientBorder(
        painter, rect, radius, borderTop, borderBottom, 1.0 + m_focusProgress * 0.5);

    if (m_searchIcon.isNull()) {
        return;
    }

    const int iconSize = 16;
    const int iconX = 12;
    const int iconY = (height() - iconSize) / 2;

    QColor iconColor = colors.textMuted;
    iconColor.setAlpha(int(180 + m_hoverProgress * 75));
    if (m_focusProgress > 0) {
        iconColor = TC::interpolate(iconColor, colors.primary, m_focusProgress);
    }

    const QPixmap pixmap = m_searchIcon.pixmap(iconSize, iconSize);
    painter.drawPixmap(iconX, iconY, ruwa::ui::painting::tintedPixmap(pixmap, iconColor));
}

void SearchBar::updateThemeColors()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    if (m_lineEdit) {
        QString style = QString(R"(
            QLineEdit {
                background: transparent;
                border: none;
                color: %1;
                selection-background-color: %2;
                selection-color: %3;
            }
            QLineEdit::placeholder {
                color: %4;
            }
        )")
                            .arg(colors.text.name())
                            .arg(colors.primary.name())
                            .arg(colors.textOnPrimary().name())
                            .arg(colors.textMuted.name());

        m_lineEdit->setStyleSheet(style);
    }

    update();
}

void SearchBar::onThemeChanged()
{
    updateThemeColors();
}

void SearchBar::onTextChanged(const QString& text)
{
    emit textChanged(text);
}

} // namespace ruwa::ui::widgets
