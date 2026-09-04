// SPDX-License-Identifier: MPL-2.0

#include "BrushImportWidget.h"

#include "features/brush/manager/BrushPreviewManager.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/WidgetStyleManager.h"
#include "shared/style/AnimationPolicy.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/PresetMenuListWidget.h"
#include "shared/widgets/SegmentedOptionSelector.h"
#include "shared/widgets/inputs/AnimatedComboBox.h"
#include "shared/widgets/inputs/StyledInputField.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/layout/AnimatedStackedWidget.h"

#include <QEvent>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace ruwa::ui::widgets {

using namespace ruwa::core::brushes;
using namespace ruwa::ui::core;

BrushImportWidget::BrushImportWidget(const QStringList& filePaths, QWidget* parent)
    : QWidget(parent)
    , m_filePaths(filePaths)
{
    setObjectName(QStringLiteral("brush_import_content"));
    setAttribute(Qt::WA_TranslucentBackground);
    m_layout = new QVBoxLayout(this);
    m_fileLabel = new QLabel(this);
    m_fileLabel->setTextFormat(Qt::PlainText);
    m_fileLabel->setWordWrap(true);
    m_layout->addWidget(m_fileLabel);

    m_list = new PresetMenuListWidget(this);
    m_list->setEmbeddedChromeTransparent(true);
    m_list->setImportExportVisible(false);
    m_list->setSearchEnabled(false);
    m_list->setContextMenuEnabled(false);
    m_list->setSelectionEnabled(false);
    m_layout->addWidget(m_list, 1);

    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_layout->addWidget(m_status);

    m_modeLayout = new QVBoxLayout();
    m_modeLabel = new QLabel(this);
    m_modeLayout->addWidget(m_modeLabel, 0, Qt::AlignHCenter);
    m_mode = new SegmentedOptionSelector(this);
    m_mode->addOption(QString());
    m_mode->addOption(QString());
    m_mode->setCurrentIndex(0, false);
    m_modeLayout->addWidget(m_mode, 0, Qt::AlignHCenter);
    m_layout->addLayout(m_modeLayout);

    m_destinationLayout = new QVBoxLayout();
    m_packLabel = new QLabel(this);
    m_destinationLayout->addWidget(m_packLabel);
    m_destinationStack = new AnimatedStackedWidget(this);
    m_destinationStack->setSlideOrientation(AnimatedStackedWidget::SlideOrientation::Horizontal);
    m_destinationStack->setAnimationDuration(200);
    m_destinationStack->setAnimationEasing(QEasingCurve::OutCubic);
    m_destinationLayout->addWidget(m_destinationStack);
    m_layout->addLayout(m_destinationLayout);

    m_nameRow = new QWidget(this);
    auto* nameLayout = new QHBoxLayout(m_nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->setSpacing(0);
    m_name = new StyledInputField(QString(), StyledInputField::FieldType::Text, m_nameRow);
    m_name->setLeadingIcon(IconProvider::StandardIcon::Brushpack);
    m_warningSlot = new QWidget(m_nameRow);
    m_warningSlot->setFixedWidth(0);
    m_warning = new QLabel(m_warningSlot);
    m_warningAnimation = new QPropertyAnimation(this, "warningProgress", this);
    m_warningAnimation->setEasingCurve(QEasingCurve::OutCubic);
    nameLayout->addWidget(m_name, 1);
    nameLayout->addWidget(m_warningSlot);
    m_destinationStack->addWidget(m_nameRow);

    auto* targetPage = new QWidget(this);
    auto* targetLayout = new QVBoxLayout(targetPage);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    m_target = new AnimatedComboBox(targetPage);
    targetLayout->addWidget(m_target);
    m_destinationStack->addWidget(targetPage);
    m_destinationStack->setCurrentIndexWithoutAnimation(0);

    auto* buttons = new QHBoxLayout();
    m_cancel = new CapsuleButton(QString(), CapsuleButton::Variant::Secondary, this);
    m_skip = new CapsuleButton(QString(), CapsuleButton::Variant::Secondary, this);
    m_import = new CapsuleButton(QString(), CapsuleButton::Variant::Primary, this);
    for (auto* button : { m_cancel, m_skip, m_import }) {
        button->setSizeScale(0.85);
        button->setBaseMinimumWidth(104);
    }
    m_import->setDefault(true);
    buttons->addWidget(m_cancel);
    buttons->addStretch();
    buttons->addWidget(m_skip);
    buttons->addWidget(m_import);
    m_layout->addLayout(buttons);

    connect(m_cancel, &QPushButton::clicked, this, &BrushImportWidget::cancelRequested);
    connect(m_skip, &QPushButton::clicked, this, &BrushImportWidget::loadNextFile);
    connect(m_import, &QPushButton::clicked, this, &BrushImportWidget::importSelected);
    connect(m_name, &StyledInputField::textChanged, this, &BrushImportWidget::updateState);
    connect(
        m_mode, &SegmentedOptionSelector::selectionChanged, this, &BrushImportWidget::updateState);
    connect(
        m_target, &AnimatedComboBox::currentIndexChanged, this, &BrushImportWidget::updateState);
    connect(m_list, &PresetMenuListWidget::itemClicked, this,
        [this](const QVariant& data) { toggleBrush(data.toInt()); });
    connect(m_list, &PresetMenuListWidget::extraActionTriggered, this,
        [this](const QVariant& data, int) { toggleBrush(data.toInt()); });
    connect(m_list, &PresetMenuListWidget::headerActionTriggered, this, [this](int id) {
        for (int i = 0; i < m_selected.size(); ++i) {
            m_selected[i] = id == 1;
            updateBrushSelection(i);
        }
        updateState();
    });
    connect(m_list->scrollArea(), &SmoothScrollArea::scrolled, this,
        &BrushImportWidget::requestVisiblePreviews);
    auto& manager = BrushManager::instance();
    connect(&manager, &BrushManager::presetCreated, this, &BrushImportWidget::refreshPresets);
    connect(&manager, &BrushManager::presetRemoved, this, &BrushImportWidget::refreshPresets);
    connect(&manager, &BrushManager::presetRenamed, this, &BrushImportWidget::refreshPresets);
    connect(&manager, &BrushManager::dataReset, this, &BrushImportWidget::refreshPresets);
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &BrushImportWidget::updateTheme);

    refreshPresets();
    retranslateUi();
    updateTheme();
    QTimer::singleShot(0, this, &BrushImportWidget::loadNextFile);
}

QSize BrushImportWidget::sizeHint() const
{
    return ThemeManager::instance().scaled(QSize(560, 540));
}

void BrushImportWidget::loadNextFile()
{
    if (m_loading) {
        return;
    }
    if (++m_fileIndex >= m_filePaths.size()) {
        m_import->setEnabled(false);
        m_skip->setEnabled(false);
        emit finished();
        return;
    }
    qDeleteAll(m_previews);
    m_previews.clear();
    m_brushes.clear();
    m_selected.clear();
    m_list->setItems({});
    m_loading = true;
    const QString path = m_filePaths.at(m_fileIndex);
    m_name->setText(QFileInfo(path).completeBaseName());
    m_status->setText(tr("Loading brushes…"));
    retranslateUi();
    updateState();

    auto* watcher = new QFutureWatcher<BrushImportResult>(this);
    connect(watcher, &QFutureWatcher<BrushImportResult>::finished, this, [this, watcher]() {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_loading = false;
        m_status->setText(result.success ? QString() : result.errorMessage);
        if (result.success) {
            m_brushes = result.brushes;
            m_selected.fill(true, m_brushes.size());
            m_name->setText(result.packName);
            QVector<PresetMenuItem> items;
            items.reserve(m_brushes.size());
            for (int i = 0; i < m_brushes.size(); ++i) {
                PresetMenuItem item;
                item.title = m_brushes.at(i).name;
                item.subtitle = tr("Included");
                item.userData = i;
                item.previewIcon = IconProvider::StandardIcon::Brush;
                item.fillPreviewBackground = true;
                item.reserveActionArea = true;
                item.deletable = false;
                item.renamable = false;
                item.extraActions = { selectionAction(i) };
                items.append(item);
            }
            m_list->setItems(items);
            m_list->scrollArea()->scrollTo(0, false);
            QTimer::singleShot(0, this, &BrushImportWidget::requestVisiblePreviews);
        }
        updateState();
    });
    watcher->setFuture(
        QtConcurrent::run([path]() { return BrushManager::readBrushFileForImport(path); }));
}

void BrushImportWidget::importSelected()
{
    if (m_loading || m_committing || !m_import->isEnabled()) {
        return;
    }
    QScopedValueRollback<bool> guard(m_committing, true);
    QVector<BrushData> selected;
    for (int i = 0; i < m_brushes.size(); ++i) {
        if (m_selected.at(i)) {
            selected.append(m_brushes.at(i));
        }
    }
    auto& manager = BrushManager::instance();
    QString error;
    const bool imported = m_mode->currentIndex() == 0
        ? !manager.importBrushesAsPreset(selected, m_name->text(), nullptr, &error).isEmpty()
        : manager.importBrushesIntoPreset(
              selected, m_target->currentData().toString(), nullptr, &error);
    if (!imported) {
        m_status->setText(error);
        updateState();
        return;
    }
    loadNextFile();
}

void BrushImportWidget::refreshPresets()
{
    const QString selectedId = m_target->currentData().toString();
    const QSignalBlocker blocker(m_target);
    m_target->clear();
    for (const auto& preset : BrushManager::instance().presets()) {
        m_target->addItem(preset.name, preset.id,
            IconProvider::instance().getIcon(IconProvider::StandardIcon::Brushpack));
    }
    const int previous = m_target->findIndexByData(selectedId);
    if (previous >= 0) {
        m_target->setCurrentIndex(previous);
    }
    updateState();
}

void BrushImportWidget::updateState()
{
    const bool separate = m_mode->currentIndex() == 0;
    if (isVisible()) {
        m_destinationStack->setCurrentIndex(separate ? 0 : 1);
    } else {
        m_destinationStack->setCurrentIndexWithoutAnimation(separate ? 0 : 1);
    }
    m_mode->setEnabled(!m_loading);
    m_name->setEnabled(!m_loading);
    m_target->setEnabled(!m_loading && m_target->count() > 0);
    m_list->setEnabled(!m_loading);
    const QString name = m_name->text().trimmed();
    const QString unique = BrushManager::instance().suggestUniquePresetName(name);
    setWarningVisible(!m_loading && !name.isEmpty() && name != unique);
    m_warning->setToolTip(QStringLiteral("<qt>%1</qt>")
            .arg(tr("A pack with this name already exists. The new pack will be named “%1”.")
                    .arg(unique)
                    .toHtmlEscaped()));
    int selected = 0;
    for (bool enabled : m_selected) {
        selected += enabled ? 1 : 0;
    }
    m_list->setTitleText(tr("Selected: %1 / %2").arg(selected).arg(m_selected.size()));
    m_import->setEnabled(!m_loading && selected > 0
        && (separate ? !name.isEmpty() : !m_target->currentData().toString().isEmpty()));
    m_skip->setVisible(m_filePaths.size() > 1);
    m_skip->setEnabled(!m_loading && m_fileIndex < m_filePaths.size());
    m_status->setVisible(!m_status->text().isEmpty());
}

void BrushImportWidget::setWarningProgress(qreal progress)
{
    m_warningProgress = qBound(0.0, progress, 1.0);
    const auto& theme = ThemeManager::instance();
    const int width = qRound(theme.scaled(36) * m_warningProgress);
    m_warningSlot->setFixedWidth(width);
    // The slot clips the icon while it slides in and takes room from the input.
    m_warning->move(width - m_warning->width(), 0);
}

void BrushImportWidget::setWarningVisible(bool visible)
{
    if (m_warningVisible == visible) {
        return;
    }
    m_warningVisible = visible;
    m_warningAnimation->stop();
    const qreal target = visible ? 1.0 : 0.0;
    if (!isVisible()) {
        setWarningProgress(target);
        return;
    }
    m_warningAnimation->setDuration(anim::duration(200));
    m_warningAnimation->setStartValue(m_warningProgress);
    m_warningAnimation->setEndValue(target);
    anim::start(m_warningAnimation);
}

PresetMenuExtraAction BrushImportWidget::selectionAction(int index) const
{
    PresetMenuExtraAction action;
    action.id = 1;
    action.text = m_selected.at(index) ? tr("Exclude brush") : tr("Include brush");
    action.icon = IconProvider::StandardIcon::Confirm;
    action.checkable = true;
    action.checked = m_selected.at(index);
    return action;
}

void BrushImportWidget::toggleBrush(int index)
{
    if (index < 0 || index >= m_selected.size()) {
        return;
    }
    m_selected[index] = !m_selected.at(index);
    updateBrushSelection(index);
    updateState();
}

void BrushImportWidget::updateBrushSelection(int index)
{
    m_list->setExtraActionsForItem(index, { selectionAction(index) });
    m_list->setSubtitleForItem(index, m_selected.at(index) ? tr("Included") : tr("Excluded"));
}

void BrushImportWidget::requestVisiblePreviews()
{
    if (!isVisible()) {
        return;
    }
    const auto& theme = ThemeManager::instance();
    for (const auto& data : m_list->visibleItemUserData(theme.scaled(80))) {
        const int index = data.toInt();
        if (index < 0 || index >= m_brushes.size()) {
            continue;
        }
        auto* session = m_previews.value(index, nullptr);
        if (!session) {
            session = BrushPreviewManager::instance().createSession(
                BrushPreviewSession::Kind::Stroke, this);
            m_previews.insert(index, session);
            connect(session, &BrushPreviewSession::imageChanged, this, [this, index, session]() {
                m_list->setPreviewImageForItem(index, session->image());
            });
        }
        BrushPreviewSpec spec;
        spec.settings = m_brushes.at(index).settings;
        spec.color = theme.colors().primary;
        spec.size = theme.scaled(QSize(168, 40));
        session->request(spec);
    }
}

void BrushImportWidget::retranslateUi()
{
    if (m_fileIndex >= 0 && m_fileIndex < m_filePaths.size()) {
        m_fileLabel->setText(tr("%1 (%2 / %3)")
                .arg(QFileInfo(m_filePaths.at(m_fileIndex)).fileName())
                .arg(m_fileIndex + 1)
                .arg(m_filePaths.size()));
    }
    m_mode->setOptionText(0, tr("As a separate pack"));
    m_mode->setOptionText(1, tr("Into an existing pack"));
    m_modeLabel->setText(tr("Import", "import mode heading").toUpper());
    m_mode->setAccessibleName(tr("Import", "import mode heading"));
    m_packLabel->setText(tr("Pack name").toUpper());
    m_name->setAccessibleName(tr("Pack name"));
    m_target->setPlaceholderText(tr("Select a pack"));
    m_target->setAccessibleName(tr("Destination pack"));
    m_cancel->setText(tr("Cancel"));
    m_skip->setText(tr("Skip file"));
    m_import->setText(tr("Import"));
    for (auto* button : { m_cancel, m_skip, m_import }) {
        button->syncSizeToText();
    }
    PresetMenuHeaderAction all;
    all.id = 1;
    all.text = tr("Select all");
    all.icon = IconProvider::StandardIcon::Confirm;
    PresetMenuHeaderAction none;
    none.id = 2;
    none.text = tr("Deselect all");
    none.icon = IconProvider::StandardIcon::Close;
    m_list->setHeaderActions({ all, none });
    m_list->setEmptyStateTexts(
        tr("No brushes"), tr("This file has no brushes available for import."));
    for (int i = 0; i < m_selected.size(); ++i) {
        updateBrushSelection(i);
    }
    updateState();
}

void BrushImportWidget::updateTheme()
{
    auto& theme = ThemeManager::instance();
    m_layout->setContentsMargins(
        theme.scaled(20), theme.scaled(16), theme.scaled(20), theme.scaled(12));
    m_layout->setSpacing(theme.scaled(12));
    m_modeLayout->setSpacing(theme.scaled(6));
    m_destinationLayout->setSpacing(theme.scaled(6));
    m_list->setMinimumHeight(theme.scaled(220));
    m_fileLabel->setFont(theme.font(ThemeFontRole::Body));
    m_fileLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(theme.colors().text.name(QColor::HexArgb)));
    m_status->setFont(theme.font(ThemeFontRole::Body));
    m_status->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
            .arg(theme.colors().textMuted.name(QColor::HexArgb)));
    const auto& style = WidgetStyleManager::instance();
    const int warningSize = style.scaled(18);
    m_warning->setPixmap(IconProvider::instance()
            .getColoredIcon(QStringLiteral("Warning"), style.colors().warning)
            .pixmap(warningSize, warningSize));
    m_warning->setFixedSize(theme.scaled(28), m_name->boxedInputHeight());
    m_warning->setAlignment(Qt::AlignCenter);
    m_warningSlot->setFixedHeight(m_name->boxedInputHeight());
    setWarningProgress(m_warningProgress);
    m_target->setFixedHeight(m_name->boxedInputHeight());
    m_destinationStack->setFixedHeight(m_name->height());
    QFont labelFont = theme.font(ThemeFontRole::Body);
    labelFont.setWeight(QFont::Normal);
    labelFont.setLetterSpacing(QFont::AbsoluteSpacing, theme.scaled(1.5));
    for (auto* label : { m_modeLabel, m_packLabel }) {
        label->setFont(labelFont);
        label->setStyleSheet(QStringLiteral("color: %1; background: transparent;")
                .arg(theme.colors().textMuted.name(QColor::HexArgb)));
    }
    requestVisiblePreviews();
    updateGeometry();
}

void BrushImportWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void BrushImportWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, &BrushImportWidget::requestVisiblePreviews);
}

} // namespace ruwa::ui::widgets
