// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   E X P O R T   S E T T I N G S   P A N E L
// ==========================================================================

#include "ExportSettingsPanel.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QEasingCurve>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <cmath>

namespace ruwa::ui::workspace {

namespace {

constexpr int kCornerRadius = 12;
constexpr int kBorderWidth = 1;
constexpr int kPanelPadding = 16;
constexpr int kExitButtonSize = 28;
constexpr int kExitIconSize = 16;
constexpr int kTitleIconSize = 18;
constexpr int kSectionSpacing = 16;
constexpr int kLabelSpacing = 6;
constexpr int kSliderHeight = 32;
constexpr int kExportButtonBaseH = 36;
constexpr int kFormatStackHeight = 118;
constexpr int kFooterTopSpacing = 12;
constexpr qreal kFooterDividerHeight = 1.5;

// ---------------------------------------------------------------------------
// Exit button — small rounded-square icon button (ChevronLeft).
// ---------------------------------------------------------------------------
class ExitButton final : public ruwa::ui::widgets::BaseAnimatedButton {
public:
    explicit ExitButton(QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setFixedSize(kExitButtonSize, kExitButtonSize);
        setCheckable(false);
        setFocusPolicy(Qt::NoFocus);
        updateIcon();
        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, &ExitButton::updateIcon);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr qreal radius = 6.0;

        // Hover / press overlay
        const qreal hover = hoverProgress();
        if (hover > 0.001 || isPressed()) {
            QColor bg = colors.overlay(isPressed() ? 0.12 : 0.08 * hover);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, radius, radius);
        }

        // Icon
        if (!m_icon.isNull()) {
            const QSize drawSize(kExitIconSize, kExitIconSize);
            const QPixmap pm = m_icon.pixmap(drawSize);
            const QPoint pos((width() - pm.width()) / 2, (height() - pm.height()) / 2);
            p.drawPixmap(pos, pm);
        }
    }

private:
    void updateIcon()
    {
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        m_icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
            ruwa::ui::core::IconProvider::StandardIcon::Close, colors.textMuted);
        update();
    }

    QIcon m_icon;
};

class FooterDivider final : public QWidget {
public:
    explicit FooterDivider(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(8);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const qreal y = (height() - kFooterDividerHeight) * 0.5;
        const QRectF lineRect(0.0, y, width(), kFooterDividerHeight);

        p.setPen(Qt::NoPen);
        p.setBrush(colors.border);
        p.drawRoundedRect(lineRect, kFooterDividerHeight * 0.5, kFooterDividerHeight * 0.5);
    }
};

// ---------------------------------------------------------------------------
// Section label helper
// ---------------------------------------------------------------------------
QLabel* makeSectionLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const auto& fonts = colors.fonts;
    label->setFont(fonts.getUIFont(8));
    label->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.textMuted.name()));
    return label;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// ExportSettingsPanel
// ---------------------------------------------------------------------------

ExportSettingsPanel::ExportSettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    buildUI();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ExportSettingsPanel::onThemeChanged);
}

void ExportSettingsPanel::setExportFrame(const QRect& frame)
{
    m_exportFrame = frame;
    updateExportSizeLabels();
    updateEstimatedSizeLabel();
}
// ---------------------------------------------------------------------------
// UI construction
// ---------------------------------------------------------------------------

void ExportSettingsPanel::buildUI()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const auto& fonts = colors.fonts;

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kPanelPadding, kPanelPadding, kPanelPadding, kPanelPadding);
    root->setSpacing(0);

    // ── Header ─────────────────────────────────────────────────────────────
    auto* header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);

    m_titleIconLabel = new QLabel(this);
    m_titleIconLabel->setFixedSize(kTitleIconSize, kExitButtonSize);
    m_titleIconLabel->setAlignment(Qt::AlignCenter);
    m_titleIconLabel->setStyleSheet(QStringLiteral("background: transparent;"));

    m_titleLabel = new QLabel(tr("Export image"), this);
    QFont titleFont = fonts.getUIFont(10);
    titleFont.setWeight(QFont::Medium);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.text.name()));
    m_titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_titleLabel->setFixedHeight(kExitButtonSize);

    auto* exitBtn = new ExitButton(this);
    connect(exitBtn, &ExitButton::clicked, this, &ExportSettingsPanel::exitRequested);

    header->addWidget(m_titleIconLabel, 0, Qt::AlignVCenter);
    header->addWidget(m_titleLabel, 1, Qt::AlignVCenter);
    header->addWidget(exitBtn, 0, Qt::AlignVCenter);
    updateHeaderIcon();

    root->addLayout(header);
    root->addSpacing(kPanelPadding);

    // ── Format selector ────────────────────────────────────────────────────
    m_formatSelector = new ruwa::ui::widgets::SegmentedOptionSelector(this);
    m_formatSelector->addOption("PNG");
    m_formatSelector->addOption("JPEG");
    m_formatSelector->addOption("WebP");
    m_formatSelector->setCurrentIndex(0, false);
    connect(m_formatSelector, &ruwa::ui::widgets::SegmentedOptionSelector::selectionChanged, this,
        &ExportSettingsPanel::onFormatChanged);
    root->addWidget(m_formatSelector, 0, Qt::AlignHCenter);

    root->addSpacing(kSectionSpacing);

    // ── JPEG quality ───────────────────────────────────────────────────────
    m_formatStack = new ruwa::ui::widgets::AnimatedStackedWidget(this);
    m_formatStack->setAttribute(Qt::WA_TranslucentBackground, true);
    m_formatStack->setAutoFillBackground(false);
    m_formatStack->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    m_formatStack->setFixedHeight(kFormatStackHeight);
    m_formatStack->setAnimationDuration(220);
    m_formatStack->setAnimationEasing(QEasingCurve::OutCubic);
    m_formatStack->setSlideOrientation(
        ruwa::ui::widgets::AnimatedStackedWidget::SlideOrientation::Horizontal);
    m_formatStack->addWidget(createFormatPage(false));
    m_formatStack->addWidget(createFormatPage(true));
    m_formatStack->addWidget(createFormatPage(false));
    m_formatStack->setCurrentIndexWithoutAnimation(0);
    root->addWidget(m_formatStack);

    // ── Canvas size info ───────────────────────────────────────────────────
    // ── Spacer ─────────────────────────────────────────────────────────────
    root->addStretch(1);
    root->addWidget(new FooterDivider(this));
    root->addSpacing(kFooterTopSpacing);

    auto* footer = new QHBoxLayout();
    footer->setContentsMargins(0, 0, 0, 0);
    footer->setSpacing(12);

    auto* estimatedSizeBlock = new QWidget(this);
    estimatedSizeBlock->setAttribute(Qt::WA_TranslucentBackground, true);
    auto* estimatedSizeLayout = new QVBoxLayout(estimatedSizeBlock);
    estimatedSizeLayout->setContentsMargins(0, 0, 0, 0);
    estimatedSizeLayout->setSpacing(3);

    m_estimatedSizeTitleLabel = makeSectionLabel(tr("EST. SIZE"), estimatedSizeBlock);
    QFont estimatedTitleFont = fonts.getUIFont(8);
    estimatedTitleFont.setWeight(QFont::Medium);
    m_estimatedSizeTitleLabel->setFont(estimatedTitleFont);

    m_estimatedSizeLabel = new QLabel(QStringLiteral("--"), estimatedSizeBlock);
    QFont estimatedSizeFont = fonts.getUIFont(14);
    estimatedSizeFont.setWeight(QFont::DemiBold);
    m_estimatedSizeLabel->setFont(estimatedSizeFont);
    m_estimatedSizeLabel->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.text.name()));

    estimatedSizeLayout->addWidget(m_estimatedSizeTitleLabel);
    estimatedSizeLayout->addWidget(m_estimatedSizeLabel);

    // ── Export button ──────────────────────────────────────────────────────
    m_exportButton = new ruwa::ui::widgets::CapsuleButton(
        tr("Export"), ruwa::ui::widgets::CapsuleButton::Variant::Primary, this);
    m_exportButton->setBaseMinimumWidth(180);
    m_exportButton->setBannerBaseHeight(kExportButtonBaseH);
    updateExportButtonIcon();
    m_exportButton->syncSizeToText();
    connect(m_exportButton, &QPushButton::clicked, this, &ExportSettingsPanel::onExportClicked);
    footer->addWidget(estimatedSizeBlock, 0, Qt::AlignLeft | Qt::AlignVCenter);
    footer->addStretch(1);
    footer->addWidget(m_exportButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    root->addLayout(footer);

    updateExportSizeLabels();
    updateEstimatedSizeLabel();
}

QWidget* ExportSettingsPanel::createFormatPage(bool includeQualityControls)
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const auto& fonts = colors.fonts;

    auto* page = new QWidget(this);
    page->setAttribute(Qt::WA_TranslucentBackground, true);
    page->setAutoFillBackground(false);
    page->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(kSectionSpacing);
    pageLayout->setAlignment(Qt::AlignTop);

    if (includeQualityControls) {
        auto* qualitySection = new QWidget(page);
        qualitySection->setAttribute(Qt::WA_TranslucentBackground, true);
        qualitySection->setAutoFillBackground(false);
        qualitySection->setStyleSheet(QStringLiteral("background: transparent;"));
        auto* qualityLayout = new QVBoxLayout(qualitySection);
        qualityLayout->setContentsMargins(0, 0, 0, 0);
        qualityLayout->setSpacing(kLabelSpacing);

        m_qualityLabel = makeSectionLabel(tr("Quality"), qualitySection);
        qualityLayout->addWidget(m_qualityLabel);

        m_qualitySlider = new ruwa::ui::widgets::ProgressHandleSlider(qualitySection);
        m_qualitySlider->setRange(1, 100);
        m_qualitySlider->setValue(92);
        m_qualitySlider->setFixedHeight(kSliderHeight);
        m_qualitySlider->setShowValueText(true);
        m_qualitySlider->setValueDisplayMode(
            ruwa::ui::widgets::ProgressHandleSlider::ValueDisplayMode::RawValue);
        m_qualitySlider->setValueTextSuffix("");
        connect(m_qualitySlider, &ruwa::ui::widgets::ProgressHandleSlider::valueChanged, this,
            [this](int) { updateEstimatedSizeLabel(); });
        qualityLayout->addWidget(m_qualitySlider);
        pageLayout->addWidget(qualitySection);
    }

    auto* sizeSection = new QWidget(page);
    sizeSection->setAttribute(Qt::WA_TranslucentBackground, true);
    sizeSection->setAutoFillBackground(false);
    sizeSection->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* sizeLayout = new QVBoxLayout(sizeSection);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(kLabelSpacing);

    auto* sizeTitleLabel = makeSectionLabel(tr("Export size"), sizeSection);
    sizeLayout->addWidget(sizeTitleLabel);

    auto* sizeLabel = new QLabel(QStringLiteral("-- x -- px"), sizeSection);
    QFont sizeFont = fonts.getUIFont(9);
    sizeLabel->setFont(sizeFont);
    sizeLabel->setStyleSheet(
        QString("color: %1; background: transparent;").arg(colors.text.name()));
    sizeLayout->addWidget(sizeLabel);
    m_sizeLabels.append(sizeLabel);

    pageLayout->addWidget(sizeSection);
    return page;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ExportSettingsPanel::onFormatChanged(int index)
{
    if (m_formatStack) {
        m_formatStack->setCurrentIndex(index);
    }
    updateEstimatedSizeLabel();
}

void ExportSettingsPanel::onExportClicked()
{
    const int idx = m_formatSelector ? m_formatSelector->currentIndex() : 0;
    const QString formats[] = { "PNG", "JPEG", "WEBP" };
    const QString format = (idx >= 0 && idx < 3) ? formats[idx] : "PNG";
    const int quality = (m_qualitySlider && idx == 1) ? m_qualitySlider->value() : -1;
    emit exportRequested(format, quality);
}

void ExportSettingsPanel::onThemeChanged()
{
    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    // Re-apply label colors
    if (m_titleLabel) {
        m_titleLabel->setStyleSheet(
            QString("color: %1; background: transparent;").arg(colors.text.name()));
    }
    for (QLabel* label : m_sizeLabels) {
        if (label) {
            label->setStyleSheet(
                QString("color: %1; background: transparent;").arg(colors.text.name()));
        }
    }
    if (m_estimatedSizeTitleLabel) {
        m_estimatedSizeTitleLabel->setStyleSheet(
            QString("color: %1; background: transparent;").arg(colors.textMuted.name()));
    }
    if (m_estimatedSizeLabel) {
        m_estimatedSizeLabel->setStyleSheet(
            QString("color: %1; background: transparent;").arg(colors.text.name()));
    }
    updateHeaderIcon();
    updateExportButtonIcon();
    updateEstimatedSizeLabel();
    update();
}

void ExportSettingsPanel::updateHeaderIcon()
{
    if (!m_titleIconLabel) {
        return;
    }

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QIcon icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Export, colors.text);
    m_titleIconLabel->setPixmap(icon.pixmap(kTitleIconSize, kTitleIconSize));
}

void ExportSettingsPanel::updateExportButtonIcon()
{
    if (!m_exportButton) {
        return;
    }

    m_exportButton->setIcon(ruwa::ui::core::IconProvider::instance().getIcon(
        ruwa::ui::core::IconProvider::StandardIcon::Export));
    m_exportButton->syncSizeToText();
}

void ExportSettingsPanel::updateExportSizeLabels()
{
    const QSize size = m_exportFrame.size();
    const QString text = (size.width() > 0 && size.height() > 0)
        ? QStringLiteral("%1 %2 %3 px").arg(size.width()).arg(QChar(0x00D7)).arg(size.height())
        : QStringLiteral("-- x -- px");

    for (QLabel* label : m_sizeLabels) {
        if (label) {
            label->setText(text);
        }
    }
}

void ExportSettingsPanel::updateEstimatedSizeLabel()
{
    if (!m_estimatedSizeLabel) {
        return;
    }

    m_estimatedSizeLabel->setText(formatEstimatedSize(estimatedExportByteSize()));
}

qint64 ExportSettingsPanel::estimatedExportByteSize() const
{
    const QSize size = m_exportFrame.size();
    const qint64 pixels = qMax<qint64>(0, static_cast<qint64>(size.width()) * size.height());
    if (pixels <= 0) {
        return 0;
    }

    const int formatIndex = m_formatSelector ? m_formatSelector->currentIndex() : 0;
    const int jpegQuality = m_qualitySlider ? m_qualitySlider->value() : 92;

    double bytesPerPixel = 1.0;
    if (formatIndex == 1) {
        bytesPerPixel = 0.16 + (qBound(1, jpegQuality, 100) / 100.0) * 0.42;
    } else if (formatIndex == 2) {
        bytesPerPixel = 0.34;
    }

    return qMax<qint64>(1, static_cast<qint64>(std::llround(pixels * bytesPerPixel)));
}

QString ExportSettingsPanel::formatEstimatedSize(qint64 bytes) const
{
    if (bytes <= 0) {
        return QStringLiteral("--");
    }

    if (bytes < 1000) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1000 * 1000) {
        return QStringLiteral("%1 KB").arg(QString::number(bytes / 1000.0, 'f', 1));
    }
    if (bytes < 1000LL * 1000LL * 1000LL) {
        return QStringLiteral("%1 MB").arg(QString::number(bytes / 1000000.0, 'f', 1));
    }
    return QStringLiteral("%1 GB").arg(QString::number(bytes / 1000000000.0, 'f', 1));
}

void ExportSettingsPanel::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

void ExportSettingsPanel::mouseMoveEvent(QMouseEvent* event)
{
    event->accept();
}

void ExportSettingsPanel::mouseReleaseEvent(QMouseEvent* event)
{
    event->accept();
}

void ExportSettingsPanel::mouseDoubleClickEvent(QMouseEvent* event)
{
    event->accept();
}

// ---------------------------------------------------------------------------
// Paint: panel background + border
// ---------------------------------------------------------------------------

void ExportSettingsPanel::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    const qreal inset = kBorderWidth * 0.5;
    const QRectF bodyRect = QRectF(rect()).adjusted(inset, inset, -inset, -inset);

    QPainterPath path;
    path.addRoundedRect(bodyRect, kCornerRadius, kCornerRadius);

    painter.fillPath(path, colors.surface);
    painter.setPen(QPen(colors.border, kBorderWidth));
    painter.drawPath(path);
}

} // namespace ruwa::ui::workspace
