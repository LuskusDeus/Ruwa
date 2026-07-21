// SPDX-License-Identifier: MPL-2.0

// WelcomeBannerSelectorWidget.cpp
#include "WelcomeBannerSelectorWidget.h"
#include "WelcomeBannerPreviewWidget.h"
#include "WelcomeBannerAddImageWidget.h"
#include "WelcomeBannerCropOverlay.h"
#include "WelcomeBannerImageCatalog.h"
#include "features/settings/SettingsManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/utils/FileDialogMemory.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QSizePolicy>
namespace ruwa::ui::widgets {

namespace {
const int BASE_PREVIEW_MARGIN_TOP = 4;
const int BASE_PREVIEW_SPACING = 8;
const int BASE_SEPARATOR_MARGIN_H = 12;
const int BASE_SEPARATOR_FONT_SIZE = 9;
const int BASE_DIVIDER_MARGIN_TOP = 8;
const int BASE_DIVIDER_MARGIN_BOTTOM = 6;
// Match ShortcutsNavigatorWidget::ShortcutsSeparatorLine
const int BASE_DIVIDER_LINE_HEIGHT = 1;
const int BASE_DIVIDER_SEPARATOR_LINE = 5;
const int MODE_ROW_TOP_AFTER_DIVIDER = 6;
const int BASE_MODE_SPACING = 10;
const int BASE_TEXT_COLOR_ROW_MARGIN_TOP = 12;

/**
 * Same look as ShortcutsNavigatorWidget::ShortcutsSeparatorLine (non-bottom variant).
 */
class BannerSectionDividerWidget : public QWidget {
public:
    explicit BannerSectionDividerWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    QSize sizeHint() const override
    {
        const int line
            = ruwa::ui::core::ThemeManager::instance().scaled(BASE_DIVIDER_SEPARATOR_LINE);
        return { 200, line };
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int lineH = qMax(1, theme.scaled(BASE_DIVIDER_LINE_HEIGHT));
        const int radius = lineH / 2;
        const QColor lineColor = theme.colors().border;

        const int y = (height() - lineH) / 2;
        const QRectF lineRect(0, y, width(), lineH);

        painter.setPen(Qt::NoPen);
        painter.setBrush(lineColor);
        painter.drawRoundedRect(lineRect, radius, radius);
    }
};

bool keysEqual(const QString& a, const QString& b)
{
    if (a == b) {
        return true;
    }
    if (a.startsWith(QLatin1String(":/")) || b.startsWith(QLatin1String(":/"))) {
        return false;
    }
    const QFileInfo fa(a);
    const QFileInfo fb(b);
    const QString ca = fa.canonicalFilePath();
    const QString cb = fb.canonicalFilePath();
    if (!ca.isEmpty() && !cb.isEmpty()) {
        return ca == cb;
    }
    return fa.absoluteFilePath() == fb.absoluteFilePath();
}

QString normalizeCustomPath(const QString& path)
{
    return QFileInfo(path).absoluteFilePath();
}

QStringList filterExistingCustomPaths(const QStringList& paths)
{
    QStringList out;
    out.reserve(paths.size());
    for (const QString& p : paths) {
        if (p.startsWith(QLatin1String(":/"))) {
            continue;
        }
        const QString abs = normalizeCustomPath(p);
        if (!abs.isEmpty() && QFileInfo::exists(abs)) {
            out.append(abs);
        }
    }
    return out;
}
} // namespace

WelcomeBannerSelectorWidget::WelcomeBannerSelectorWidget(QWidget* parent)
    : BaseSettingsWidget(QString(), QString(), parent)
{
    setLabel(tr("Welcome banner"));
    setDescription(
        tr("Background for the welcome screen. Use built-in art or your own images; adjust text "
           "contrast below."));

    setupContent();

    updateScaledSizes();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            updateScaledSizes();
            updateThemeColors();
            if (m_addTile) {
                m_addTile->update();
            }
            for (WelcomeBannerPreviewWidget* p : m_previews) {
                if (p) {
                    p->update();
                }
            }
            if (m_sectionDivider) {
                m_sectionDivider->update();
            }
        });

    // Queued: applying settings emits synchronously; rebuilding here in the same stack as
    // QFileDialog return can delete the "Add image" tile while focus/events still target it (crash
    // on Windows).
    connect(
        &ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::welcomeBannerBackgroundSettingsChanged, this,
        [this]() { loadFromSettings(); }, Qt::QueuedConnection);

    connect(&ruwa::core::SettingsManager::instance(),
        &ruwa::core::SettingsManager::welcomeBannerDisplayedImageKeyChanged, this,
        [this](const QString& key) {
            if (!m_randomize) {
                return;
            }
            m_selectedKey = key.isEmpty() ? welcomeBannerDefaultFixedKey() : key;
            syncSelectionVisuals();
        });
}

void WelcomeBannerSelectorWidget::setupContent()
{
    m_previewContainer = new QWidget(this);
    m_previewContainer->setAttribute(Qt::WA_TranslucentBackground);
    // Shrink to thumbnail row only — no empty strip after "Add image" when custom paths are removed
    m_previewContainer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    mainLayout()->addWidget(m_previewContainer);

    m_dividerWrap = new QWidget(this);
    m_dividerWrap->setAttribute(Qt::WA_TranslucentBackground);
    m_dividerWrap->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* dividerOuter = new QVBoxLayout(m_dividerWrap);
    dividerOuter->setContentsMargins(0, 0, 0, 0);
    dividerOuter->setSpacing(0);
    m_sectionDivider = new BannerSectionDividerWidget(m_dividerWrap);
    dividerOuter->addWidget(m_sectionDivider);
    mainLayout()->addWidget(m_dividerWrap);

    QVector<SegmentedOptionSelector::Option> modeOptions;
    modeOptions.append({ QString(), QIcon(), 0 });
    modeOptions.append({ QString(), QIcon(), 1 });

    m_modeRowHost = new QWidget(this);
    m_modeRowHost->setAttribute(Qt::WA_TranslucentBackground);
    m_modeRowHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* modeHostLayout = new QHBoxLayout(m_modeRowHost);
    modeHostLayout->setContentsMargins(0, 0, 0, 0);

    m_modeLabel = new QLabel(m_modeRowHost);
    m_modeLabel->setAttribute(Qt::WA_TranslucentBackground);

    m_modeSelector = new SegmentedOptionSelector(modeOptions, m_modeRowHost);
    m_modeSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    m_modeSelector->setCurrentIndex(m_randomize ? 0 : 1, false);

    connect(m_modeSelector, &SegmentedOptionSelector::selectionChanged, this,
        [this](int index) { onModeIndexChanged(index); });

    modeHostLayout->addWidget(m_modeLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    modeHostLayout->addStretch(1);
    modeHostLayout->addWidget(m_modeSelector, 0, Qt::AlignVCenter);

    mainLayout()->addWidget(m_modeRowHost);

    QVector<SegmentedOptionSelector::Option> textColorOptions;
    textColorOptions.append({ QString(), QIcon(), 0 });
    textColorOptions.append({ QString(), QIcon(), 1 });

    m_textColorRowHost = new QWidget(this);
    m_textColorRowHost->setAttribute(Qt::WA_TranslucentBackground);
    m_textColorRowHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* textColorHostLayout = new QHBoxLayout(m_textColorRowHost);
    textColorHostLayout->setContentsMargins(0, 0, 0, 0);

    m_textColorLabel = new QLabel(m_textColorRowHost);
    m_textColorLabel->setAttribute(Qt::WA_TranslucentBackground);

    m_textColorSelector = new SegmentedOptionSelector(textColorOptions, m_textColorRowHost);
    m_textColorSelector->setDisplayMode(SegmentedOptionSelector::DisplayMode::TextOnly);
    m_textColorSelector->setCurrentIndex(m_textColorMode, false);

    connect(m_textColorSelector, &SegmentedOptionSelector::selectionChanged, this,
        &WelcomeBannerSelectorWidget::onTextColorModeChanged);

    textColorHostLayout->addWidget(m_textColorLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    textColorHostLayout->addStretch(1);
    textColorHostLayout->addWidget(m_textColorSelector, 0, Qt::AlignVCenter);

    mainLayout()->addWidget(m_textColorRowHost);

    retranslateUi();
    rebuildPreviews();

    reinstallBannerRootGridLayout();
}

void WelcomeBannerSelectorWidget::reinstallBannerRootGridLayout()
{
    auto* oldRow = qobject_cast<QHBoxLayout*>(m_mainLayout);
    if (!oldRow) {
        return;
    }

    const QMargins mg = oldRow->contentsMargins();
    const int hSpacing = oldRow->spacing();

    oldRow->removeWidget(m_textContainer);
    oldRow->removeWidget(m_controlContainer);
    delete oldRow;

    m_controlLayout->removeWidget(m_previewContainer);
    m_controlLayout->removeWidget(m_dividerWrap);
    m_controlLayout->removeWidget(m_modeRowHost);
    m_controlLayout->removeWidget(m_textColorRowHost);

    m_controlContainer->hide();

    auto* grid = new QGridLayout(this);
    m_mainLayout = grid;
    grid->setContentsMargins(mg);
    grid->setHorizontalSpacing(hSpacing);
    grid->setVerticalSpacing(0);

    grid->addWidget(m_textContainer, 0, 0, Qt::AlignVCenter | Qt::AlignLeft);
    grid->addWidget(m_previewContainer, 0, 1, Qt::AlignVCenter | Qt::AlignLeft);
    grid->addWidget(m_dividerWrap, 1, 0, 1, 2);
    grid->addWidget(m_modeRowHost, 2, 0, 1, 2, Qt::AlignTop);
    grid->addWidget(m_textColorRowHost, 3, 0, 1, 2, Qt::AlignTop);

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 0);
}

void WelcomeBannerSelectorWidget::rebuildPreviews()
{
    if (m_previewContainer) {
        if (QWidget* fw = QApplication::focusWidget()) {
            if (m_previewContainer->isAncestorOf(fw)) {
                setFocus(Qt::OtherFocusReason);
            }
        }
    }

    for (WelcomeBannerPreviewWidget* p : m_previews) {
        delete p;
    }
    m_previews.clear();

    if (m_separatorLabel) {
        delete m_separatorLabel;
        m_separatorLabel = nullptr;
    }

    if (m_addTile) {
        delete m_addTile;
        m_addTile = nullptr;
    }

    if (m_previewContainer->layout()) {
        QLayout* oldLayout = m_previewContainer->layout();
        QLayoutItem* item;
        while ((item = oldLayout->takeAt(0)) != nullptr) {
            delete item;
        }
        delete oldLayout;
        m_previewLayout = nullptr;
    }

    m_previewLayout = new QHBoxLayout(m_previewContainer);
    m_previewLayout->setContentsMargins(0, 0, 0, 0);
    m_previewLayout->setSizeConstraint(QLayout::SetFixedSize);

    const QStringList builtins = welcomeBannerBuiltinImageKeys();
    for (const QString& key : builtins) {
        // Skip hidden easter egg banner
        if (key == QLatin1String(":/images/Banner1April")) {
            continue;
        }
        auto* preview = new WelcomeBannerPreviewWidget(key, m_previewContainer);
        connect(preview, &WelcomeBannerPreviewWidget::imageClicked, this,
            &WelcomeBannerSelectorWidget::onPreviewClicked);
        m_previews.append(preview);
        m_previewLayout->addWidget(preview);
    }

    for (const QString& path : m_customPaths) {
        auto* preview = new WelcomeBannerPreviewWidget(path, m_previewContainer);
        connect(preview, &WelcomeBannerPreviewWidget::imageClicked, this,
            &WelcomeBannerSelectorWidget::onPreviewClicked);
        connect(preview, &WelcomeBannerPreviewWidget::customImageDeleteRequested, this,
            &WelcomeBannerSelectorWidget::removeCustomBannerImage);
        connect(preview, &WelcomeBannerPreviewWidget::customImageEditCropRequested, this,
            &WelcomeBannerSelectorWidget::editCustomBannerImageCrop);
        m_previews.append(preview);
        m_previewLayout->addWidget(preview);
    }

    m_separatorLabel = new QLabel(tr("or"), m_previewContainer);
    m_separatorLabel->setAlignment(Qt::AlignCenter);
    m_separatorLabel->setAttribute(Qt::WA_TranslucentBackground);
    m_previewLayout->addWidget(m_separatorLabel);

    m_addTile = new WelcomeBannerAddImageWidget(m_previewContainer);
    connect(m_addTile, &WelcomeBannerAddImageWidget::clicked, this,
        &WelcomeBannerSelectorWidget::onAddImageClicked);
    m_previewLayout->addWidget(m_addTile);

    syncSelectionVisuals();
    updateScaledSizes();
    refreshLayoutGeometry();

    m_previewRowCustomPaths = m_customPaths;
}

void WelcomeBannerSelectorWidget::syncSelectionVisuals()
{
    for (WelcomeBannerPreviewWidget* p : m_previews) {
        if (!p) {
            continue;
        }
        p->setSelected(keysEqual(p->imageKey(), m_selectedKey));
    }
}

void WelcomeBannerSelectorWidget::onPreviewClicked(const QString& key)
{
    m_selectedKey = key;
    m_randomize = false;
    if (m_modeSelector) {
        m_modeSelector->blockSignals(true);
        m_modeSelector->setCurrentIndex(1, false);
        m_modeSelector->blockSignals(false);
    }
    syncSelectionVisuals();

    auto& settings = ruwa::core::SettingsManager::instance();
    settings.setWelcomeBannerFixedKeyDisablingRandomize(m_selectedKey);
    settings.save();
}

void WelcomeBannerSelectorWidget::onModeIndexChanged(int index)
{
    const bool randomize = (index == 0);
    if (m_randomize == randomize) {
        return;
    }
    m_randomize = randomize;

    auto& settings = ruwa::core::SettingsManager::instance();
    settings.setWelcomeBannerRandomize(m_randomize);
    settings.save();
}

void WelcomeBannerSelectorWidget::onTextColorModeChanged(int index)
{
    if (index != 0 && index != 1) {
        return;
    }
    if (m_textColorMode == index) {
        return;
    }
    m_textColorMode = index;

    auto& settings = ruwa::core::SettingsManager::instance();
    settings.setWelcomeBannerTextColorMode(index);
    settings.save();
}

void WelcomeBannerSelectorWidget::removeCustomBannerImage(const QString& path)
{
    const QString abs = normalizeCustomPath(path);
    if (abs.isEmpty() || abs.startsWith(QLatin1String(":/"))) {
        return;
    }
    int removeIndex = -1;
    for (int i = 0; i < m_customPaths.size(); ++i) {
        if (keysEqual(m_customPaths[i], abs)) {
            removeIndex = i;
            break;
        }
    }
    if (removeIndex < 0) {
        return;
    }

    m_customPaths.removeAt(removeIndex);

    QString newFixedKey = m_selectedKey;
    if (keysEqual(m_selectedKey, abs)) {
        newFixedKey = welcomeBannerDefaultFixedKey();
    }

    auto& settings = ruwa::core::SettingsManager::instance();
    settings.setWelcomeBannerCustomCrop(abs, QRectF(0.0, 0.0, 1.0, 1.0)); // clear stored crop
    settings.setWelcomeBannerCustomPathsAndFixedKey(m_customPaths, newFixedKey);
    settings.save();
}

void WelcomeBannerSelectorWidget::onAddImageClicked()
{
    const QString filter = tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)");
    const QStringList files = ruwa::shared::filedialog::getOpenFileNames(
        this, ruwa::shared::filedialog::category::kImage, tr("Add welcome banner image"), filter);

    if (files.isEmpty()) {
        return;
    }

    // Queue valid, non-duplicate images; each gets its own crop overlay in turn.
    for (const QString& f : files) {
        const QString abs = normalizeCustomPath(f);
        if (abs.isEmpty() || !QFileInfo::exists(abs)) {
            continue;
        }
        bool duplicate = false;
        for (const QString& existing : m_customPaths) {
            if (keysEqual(existing, abs)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && !m_pendingCropQueue.contains(abs)) {
            m_pendingCropQueue.append(abs);
        }
    }

    processNextPendingCrop();
}

void WelcomeBannerSelectorWidget::processNextPendingCrop()
{
    // One overlay at a time; bail if one is still up or nothing is queued.
    if (m_cropOverlay || m_pendingCropQueue.isEmpty()) {
        return;
    }

    const QString path = m_pendingCropQueue.takeFirst();

    QWidget* host = window() ? window() : this;
    auto* overlay = new WelcomeBannerCropOverlay(path, host);
    if (!overlay->hasValidImage()) {
        overlay->deleteLater();
        processNextPendingCrop();
        return;
    }
    m_cropOverlay = overlay;

    connect(overlay, &WelcomeBannerCropOverlay::cropConfirmed, this,
        [this, overlay, path](const QRectF& norm) {
            bool present = false;
            for (const QString& existing : m_customPaths) {
                if (keysEqual(existing, path)) {
                    present = true;
                    break;
                }
            }
            if (!present) {
                m_customPaths.append(path);
            }
            m_selectedKey = path;

            auto& settings = ruwa::core::SettingsManager::instance();
            settings.setWelcomeBannerCustomCrop(path, norm);
            settings.setWelcomeBannerCustomPathsAndFixedKey(m_customPaths, m_selectedKey);
            settings.save();
            // Preview rebuild runs from loadFromSettings() via the queued
            // welcomeBannerBackgroundSettingsChanged signal.

            overlay->deleteLater();
            m_cropOverlay = nullptr;
            processNextPendingCrop();
        });

    connect(overlay, &WelcomeBannerCropOverlay::cancelled, this, [this, overlay]() {
        overlay->deleteLater();
        m_cropOverlay = nullptr;
        processNextPendingCrop();
    });

    overlay->showOverlay();
}

void WelcomeBannerSelectorWidget::editCustomBannerImageCrop(const QString& path)
{
    // One overlay at a time (don't collide with an in-progress import queue).
    if (m_cropOverlay) {
        return;
    }
    const QString abs = normalizeCustomPath(path);
    if (abs.isEmpty() || abs.startsWith(QLatin1String(":/")) || !QFileInfo::exists(abs)) {
        return;
    }

    QWidget* host = window() ? window() : this;
    auto* overlay = new WelcomeBannerCropOverlay(abs, host);
    if (!overlay->hasValidImage()) {
        overlay->deleteLater();
        return;
    }
    m_cropOverlay = overlay;

    auto& settings = ruwa::core::SettingsManager::instance();
    overlay->setInitialCrop(settings.welcomeBannerCropFor(abs));

    connect(overlay, &WelcomeBannerCropOverlay::cropConfirmed, this,
        [this, overlay, abs](const QRectF& norm) {
            auto& s = ruwa::core::SettingsManager::instance();
            s.setWelcomeBannerCustomCropNotifying(abs, norm);
            s.save();
            overlay->deleteLater();
            m_cropOverlay = nullptr;
        });

    connect(overlay, &WelcomeBannerCropOverlay::cancelled, this, [this, overlay]() {
        overlay->deleteLater();
        m_cropOverlay = nullptr;
    });

    overlay->showOverlay();
}

void WelcomeBannerSelectorWidget::loadFromSettings()
{
    const auto& app = ruwa::core::SettingsManager::instance().settings();
    QStringList filtered = filterExistingCustomPaths(app.appearance.welcomeBannerCustomPaths);
    if (filtered != app.appearance.welcomeBannerCustomPaths) {
        auto& sm = ruwa::core::SettingsManager::instance();
        sm.setWelcomeBannerCustomPaths(filtered);
        sm.save();
    }
    m_customPaths = filtered;

    m_randomize = app.appearance.welcomeBannerRandomize;
    m_textColorMode = app.appearance.welcomeBannerTextColorMode;

    // Build pool of selectable banners, excluding hidden easter egg
    QStringList pool;
    const QStringList builtins = welcomeBannerBuiltinImageKeys();
    for (const QString& key : builtins) {
        if (key != QLatin1String(":/images/Banner1April")) {
            pool.append(key);
        }
    }
    pool.append(m_customPaths);

    m_selectedKey = app.appearance.welcomeBannerFixedKey;
    if (m_selectedKey.isEmpty()) {
        m_selectedKey = welcomeBannerDefaultFixedKey();
    }
    bool found = false;
    for (const QString& k : pool) {
        if (keysEqual(k, m_selectedKey)) {
            m_selectedKey = k;
            found = true;
            break;
        }
    }
    if (!found && !pool.isEmpty()) {
        m_selectedKey = pool.first();
    }

    if (m_modeSelector) {
        m_modeSelector->blockSignals(true);
        m_modeSelector->setCurrentIndex(m_randomize ? 0 : 1, false);
        m_modeSelector->blockSignals(false);
    }

    if (m_textColorSelector) {
        m_textColorSelector->blockSignals(true);
        m_textColorSelector->setCurrentIndex(m_textColorMode, false);
        m_textColorSelector->blockSignals(false);
    }

    // Cannot compare to previous m_customPaths: remove/add already updated it before save, so the
    // signal would incorrectly skip rebuild. Compare to the pool the preview row was built for.
    const bool sameCustomPool = !m_previews.isEmpty() && (filtered == m_previewRowCustomPaths);
    if (sameCustomPool) {
        syncSelectionVisuals();
        updateScaledSizes();
        refreshLayoutGeometry();
        return;
    }

    rebuildPreviews();
}

void WelcomeBannerSelectorWidget::setSelectedImageKey(const QString& key)
{
    m_selectedKey = key.isEmpty() ? welcomeBannerDefaultFixedKey() : key;
    syncSelectionVisuals();
}

void WelcomeBannerSelectorWidget::setRandomize(bool randomize)
{
    m_randomize = randomize;
    if (m_modeSelector) {
        m_modeSelector->blockSignals(true);
        m_modeSelector->setCurrentIndex(m_randomize ? 0 : 1, false);
        m_modeSelector->blockSignals(false);
    }
}

void WelcomeBannerSelectorWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void WelcomeBannerSelectorWidget::retranslateUi()
{
    setLabel(tr("Welcome banner"));
    setDescription(
        tr("Background for the welcome screen. Use built-in art or your own images; adjust text "
           "contrast below."));

    if (m_separatorLabel) {
        m_separatorLabel->setText(tr("or"));
    }

    if (m_modeLabel) {
        m_modeLabel->setText(tr("Image selection"));
    }

    if (m_modeSelector && m_modeSelector->optionCount() >= 2) {
        m_modeSelector->setOptionText(0, tr("Random"));
        m_modeSelector->setOptionText(1, tr("Fixed"));
    }

    if (m_textColorLabel) {
        m_textColorLabel->setText(tr("Banner text"));
    }
    if (m_textColorSelector && m_textColorSelector->optionCount() >= 2) {
        m_textColorSelector->setOptionText(0, tr("Basic"));
        m_textColorSelector->setOptionText(1, tr("Inverted"));
    }

    if (m_addTile) {
        m_addTile->update();
    }

    updateScaledSizes();
}

void WelcomeBannerSelectorWidget::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    if (m_textLayout) {
        m_textLayout->setContentsMargins(0, 0, 0, 0);
    }

    if (m_previewLayout) {
        const int marginTop = theme.scaled(BASE_PREVIEW_MARGIN_TOP);
        m_previewLayout->setContentsMargins(0, marginTop, 0, 0);
        m_previewLayout->setSpacing(theme.scaled(BASE_PREVIEW_SPACING));
    }
    if (m_previewContainer) {
        m_previewContainer->setFixedHeight(m_previewContainer->sizeHint().height());
    }

    if (m_separatorLabel) {
        const int marginH = theme.scaled(BASE_SEPARATOR_MARGIN_H);
        m_separatorLabel->setContentsMargins(marginH, 0, marginH, 0);

        QFont separatorFont = m_separatorLabel->font();
        separatorFont.setPointSize(theme.scaledFontSize(BASE_SEPARATOR_FONT_SIZE));
        m_separatorLabel->setFont(separatorFont);

        const auto& colors = theme.colors();
        m_separatorLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; }").arg(colors.textMuted.name()));
    }

    if (m_dividerWrap && m_dividerWrap->layout()) {
        const int divTop = theme.scaled(BASE_DIVIDER_MARGIN_TOP);
        const int divBottom = theme.scaled(BASE_DIVIDER_MARGIN_BOTTOM);
        m_dividerWrap->layout()->setContentsMargins(0, divTop, 0, divBottom);
    }

    if (m_sectionDivider) {
        m_sectionDivider->setFixedHeight(theme.scaled(BASE_DIVIDER_SEPARATOR_LINE));
    }
    if (m_dividerWrap) {
        m_dividerWrap->setFixedHeight(m_dividerWrap->sizeHint().height());
    }

    if (m_modeRowHost) {
        if (auto* modeHostLayout = qobject_cast<QHBoxLayout*>(m_modeRowHost->layout())) {
            modeHostLayout->setSpacing(theme.scaled(BASE_MODE_SPACING));
            modeHostLayout->setContentsMargins(0, theme.scaled(MODE_ROW_TOP_AFTER_DIVIDER), 0, 0);
        }
        m_modeRowHost->setFixedHeight(m_modeRowHost->sizeHint().height());
    }

    if (m_textColorRowHost) {
        if (auto* textColorHostLayout = qobject_cast<QHBoxLayout*>(m_textColorRowHost->layout())) {
            textColorHostLayout->setSpacing(theme.scaled(BASE_MODE_SPACING));
            textColorHostLayout->setContentsMargins(
                0, theme.scaled(BASE_TEXT_COLOR_ROW_MARGIN_TOP), 0, 0);
        }
        m_textColorRowHost->setFixedHeight(m_textColorRowHost->sizeHint().height());
    }

    if (m_modeLabel) {
        QFont f = m_modeLabel->font();
        f.setBold(true);
        f.setPointSize(theme.scaledFontSize(style().content.baseFontSize));
        m_modeLabel->setFont(f);
        const auto& colors = theme.colors();
        m_modeLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name()));
    }

    if (m_textColorLabel) {
        QFont tf = m_textColorLabel->font();
        tf.setBold(true);
        tf.setPointSize(theme.scaledFontSize(style().content.baseFontSize));
        m_textColorLabel->setFont(tf);
        const auto& colors = theme.colors();
        m_textColorLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name()));
    }

    // Use the full row layout (label + controls), same as ThemeSelectorWidget — not mainLayout()
    // which is only m_controlLayout; otherwise height ignores the text column and clips previews.
    if (m_mainLayout) {
        m_mainLayout->invalidate();
        m_mainLayout->activate();
    }

    updateGeometry();
    refreshLayoutGeometry();
}

void WelcomeBannerSelectorWidget::updateThemeColors()
{
    updateScaledSizes();
    BaseSettingsWidget::updateThemeColors();
}

} // namespace ruwa::ui::widgets
