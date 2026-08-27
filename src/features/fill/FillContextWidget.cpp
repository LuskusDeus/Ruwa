// SPDX-License-Identifier: MPL-2.0

#include "features/fill/FillContextWidget.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/CapsuleButton.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

FillContextWidget::FillContextWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("fill_context_content"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);

    m_layout = new QVBoxLayout(this);

    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setWordWrap(true);
    m_descriptionLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_layout->addWidget(m_descriptionLabel);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(0, 0, 0, 0);

    m_cancelButton = new CapsuleButton(QString(), CapsuleButton::Variant::Secondary, this);
    m_cancelButton->setBaseMinimumWidth(104);
    m_cancelButton->setSizeScale(0.85);

    m_fillButton = new CapsuleButton(QString(), CapsuleButton::Variant::Primary, this);
    m_fillButton->setBaseMinimumWidth(104);
    m_fillButton->setSizeScale(0.85);
    m_fillButton->setDefault(true);

    buttonRow->addStretch(1);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_fillButton);
    m_layout->addLayout(buttonRow);

    connect(m_cancelButton, &QPushButton::clicked, this, &FillContextWidget::cancelRequested);
    connect(m_fillButton, &QPushButton::clicked, this, &FillContextWidget::fillRequested);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &FillContextWidget::updateTheme);

    retranslateUi();
    updateTheme();
}

QSize FillContextWidget::sizeHint() const
{
    return ThemeManager::instance().scaled(QSize(390, 128));
}

QSize FillContextWidget::minimumSizeHint() const
{
    return sizeHint();
}

void FillContextWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void FillContextWidget::retranslateUi()
{
    m_descriptionLabel->setText(tr("Fill the current selection with the active foreground color."));
    m_cancelButton->setText(tr("Cancel"));
    m_fillButton->setText(tr("Fill"));
    m_cancelButton->syncSizeToText();
    m_fillButton->syncSizeToText();
}

void FillContextWidget::updateTheme()
{
    auto& theme = ThemeManager::instance();
    m_layout->setContentsMargins(
        theme.scaled(20), theme.scaled(18), theme.scaled(20), theme.scaled(16));
    m_layout->setSpacing(theme.scaled(18));

    m_descriptionLabel->setFont(theme.font(ThemeFontRole::Body));
    m_descriptionLabel->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(theme.colors().text.name(QColor::HexArgb)));
    updateGeometry();
}

} // namespace ruwa::ui::widgets
