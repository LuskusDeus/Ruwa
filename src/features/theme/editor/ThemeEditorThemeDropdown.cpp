// SPDX-License-Identifier: MPL-2.0

#include "ThemeEditorThemeDropdown.h"

#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemePresetJson.h"
#include "shared/i18n/TranslationManager.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"
#include "shared/utils/FileDialogMemory.h"
#include "shared/widgets/CapsuleButton.h"
#include "shared/widgets/PresetMenuListWidget.h"
#include "shared/widgets/ToolButton.h"
#include "shell/top-bar/MessagePopupManager.h"
#include "shell/top-bar/OverlayContainer.h"

#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QFile>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSet>
#include <QSize>
#include <QSizePolicy>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <functional>

namespace ruwa::ui::widgets {

namespace {

constexpr int kPopupBaseWidth = 650;
constexpr int kPopupBaseHeight = 400;
constexpr int kPopupShadow = 16;
constexpr int kPopupRadius = 12;
constexpr int kPopupMargin = 12;
constexpr int kActionsBaseWidth = 190;
constexpr int kDetailedActionBaseWidth = 178;

QVector<QColor> previewColors(const ruwa::ui::core::ThemePreset& preset)
{
    return { preset.background, preset.surface, preset.primary, preset.accent };
}

} // namespace

class ThemeEditorThemePopup final : public QWidget {
public:
    explicit ThemeEditorThemePopup(ThemeEditorThemeDropdown* owner)
        : QWidget(owner)
        , m_owner(owner)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        hide();

        m_rootLayout = new QHBoxLayout(this);
        m_rootLayout->setSpacing(0);

        m_themeList = new PresetMenuListWidget(this);
        m_themeList->setPopupStyle(false);
        m_themeList->setPopupPanelPainted(false);
        m_themeList->setEmbeddedChromeTransparent(true);
        m_themeList->setImportExportVisible(false);
        m_themeList->setContextMenuEnabled(false);
        m_themeList->setSearchEnabled(true);
        m_rootLayout->addWidget(m_themeList, 1);

        m_actionsPanel = new QWidget(this);
        m_actionsLayout = new QVBoxLayout(m_actionsPanel);
        m_actionsLayout->setContentsMargins(0, 0, 0, 0);
        m_actionsLayout->setSpacing(0);
        m_rootLayout->addWidget(m_actionsPanel);

        m_actionsTitle = new QLabel(m_actionsPanel);
        QFont titleFont = m_actionsTitle->font();
        titleFont.setBold(true);
        m_actionsTitle->setFont(titleFont);
        m_actionsLayout->addWidget(m_actionsTitle);

        m_newButton = createDetailedButton(ruwa::ui::core::IconProvider::StandardIcon::FileNew,
            CapsuleButton::Variant::Primary, m_actionsPanel);
        m_importButton = createDetailedButton(ruwa::ui::core::IconProvider::StandardIcon::Import,
            CapsuleButton::Variant::Secondary, m_actionsPanel);
        m_exportButton = createDetailedButton(ruwa::ui::core::IconProvider::StandardIcon::Export,
            CapsuleButton::Variant::Secondary, m_actionsPanel);

        m_actionsLayout->addWidget(m_newButton);
        m_actionsLayout->addWidget(m_importButton);
        m_actionsLayout->addWidget(m_exportButton);
        m_actionsLayout->addStretch();

        auto* bottomActions = new QHBoxLayout();
        bottomActions->setContentsMargins(0, 0, 0, 0);
        bottomActions->setSpacing(0);
        m_deleteButton
            = createIconButton(ruwa::ui::core::IconProvider::StandardIcon::Trash, m_actionsPanel);
        m_saveAsButton
            = createIconButton(ruwa::ui::core::IconProvider::StandardIcon::Save, m_actionsPanel);
        bottomActions->addWidget(m_deleteButton);
        bottomActions->addStretch();
        bottomActions->addWidget(m_saveAsButton);
        m_actionsLayout->addLayout(bottomActions);

        connect(
            m_themeList, &PresetMenuListWidget::itemClicked, this, [this](const QVariant& data) {
                if (m_owner) {
                    m_owner->setEditingThemeById(QUuid(data.toString()));
                    hidePopup();
                }
            });
        connect(m_newButton, &QPushButton::clicked, this, [this]() {
            if (m_owner) {
                m_owner->createNewTheme();
                hidePopup();
            }
        });
        connect(m_importButton, &QPushButton::clicked, this, [this]() {
            if (m_owner) {
                m_owner->importTheme();
            }
        });
        connect(m_exportButton, &QPushButton::clicked, this, [this]() {
            if (m_owner) {
                m_owner->exportTheme();
            }
        });
        connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
            if (m_owner) {
                m_owner->deleteTheme();
            }
        });
        connect(m_saveAsButton, &QPushButton::clicked, this, [this]() {
            if (m_owner) {
                m_owner->saveThemeAsNew();
                hidePopup();
            }
        });

        m_opacityEffect = new QGraphicsOpacityEffect(this);
        m_opacityEffect->setOpacity(0.0);
        setGraphicsEffect(m_opacityEffect);

        m_opacityAnimation = new QVariantAnimation(this);
        m_opacityAnimation->setDuration(150);
        m_opacityAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_opacityAnimation, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                m_opacity = qBound(0.0, value.toReal(), 1.0);
                m_opacityEffect->setOpacity(m_opacity);
                const int slide = qRound((1.0 - m_opacity) * (m_placedAbove ? 7.0 : -7.0));
                move(m_targetPosition + QPoint(0, slide));
            });

        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, [this]() { updateTheme(); });
        connect(&ruwa::ui::core::TranslationManager::instance(),
            &ruwa::ui::core::TranslationManager::languageChanged, this,
            [this]() { retranslateUi(); });

        updateScaledSizes();
        retranslateUi();
        updateTheme();
    }

    ~ThemeEditorThemePopup() override
    {
        if (m_appFilterInstalled) {
            qApp->removeEventFilter(this);
        }
        if (m_overlay) {
            m_overlay->unregisterGenericPopup(this);
        }
    }

    bool isPopupVisible() const { return m_visible; }

    void refreshThemes(bool animateSelection = true)
    {
        QVector<PresetMenuItem> items;
        const auto presets = ruwa::ui::core::ThemeManager::instance().allPresets();
        items.reserve(presets.size());

        for (const auto& preset : presets) {
            const auto& displayPreset
                = m_owner && m_owner->hasEditingTheme() && m_owner->editingTheme().id == preset.id
                ? m_owner->editingTheme()
                : preset;

            PresetMenuItem item;
            item.title = ruwa::ui::core::ThemePreset::translatedDisplayName(displayPreset);
            item.subtitle = displayPreset.description;
            item.badgeText = displayPreset.isBuiltIn ? ThemeEditorThemeDropdown::tr("Built-in")
                                                     : ThemeEditorThemeDropdown::tr("Custom");
            item.badgeTint = displayPreset.isBuiltIn ? QColor() : displayPreset.primary;
            item.previewColors = previewColors(displayPreset);
            item.previewIcon = ruwa::ui::core::IconProvider::StandardIcon::Appearance;
            item.searchText
                = QStringLiteral("%1 %2 %3").arg(item.title, item.subtitle, item.badgeText);
            item.userData = displayPreset.id.toString();
            item.deletable = false;
            item.renamable = false;
            items.append(item);
        }

        m_themeList->setItems(items);
        syncSelection(animateSelection);
    }

    void syncSelection(bool animateSelection = true)
    {
        if (m_owner && m_owner->hasEditingTheme()) {
            m_themeList->setSelectedUserData(
                m_owner->editingTheme().id.toString(), animateSelection);
        }
        m_themeList->setActiveUserData(
            ruwa::ui::core::ThemeManager::instance().currentPresetId().toString());
        updateActionStates();
    }

    bool popupUnder(QWidget* anchor)
    {
        if (!anchor) {
            return false;
        }

        m_anchor = anchor;
        m_overlay = OverlayContainer::instance(anchor->window());
        if (!m_overlay) {
            return false;
        }
        m_overlay->registerGenericPopup(this);

        updateScaledSizes();
        refreshThemes(false);

        m_visible = true;
        m_hiding = false;

        // showOverlay() synchronizes the overlay with the current window geometry. This must
        // happen before placement is calculated because a newly created overlay may still have
        // the size captured before the main window finished its initial layout.
        m_overlay->showOverlay();
        updatePopupGeometry();

        show();
        raise();
        m_overlay->refreshGenericPopups();

        m_opacityAnimation->stop();
        m_opacityAnimation->setStartValue(m_opacity);
        m_opacityAnimation->setEndValue(1.0);
        disconnect(m_opacityAnimation, &QVariantAnimation::finished, this, nullptr);
        m_opacityAnimation->start();

        if (!m_appFilterInstalled) {
            qApp->installEventFilter(this);
            m_appFilterInstalled = true;
        }
        return true;
    }

    void hidePopup(bool animated = true)
    {
        if (!m_visible || m_hiding) {
            return;
        }

        if (m_appFilterInstalled) {
            qApp->removeEventFilter(this);
            m_appFilterInstalled = false;
        }

        if (!animated) {
            finishHide();
            return;
        }

        m_hiding = true;
        m_opacityAnimation->stop();
        m_opacityAnimation->setStartValue(m_opacity);
        m_opacityAnimation->setEndValue(0.0);
        disconnect(m_opacityAnimation, &QVariantAnimation::finished, this, nullptr);
        connect(m_opacityAnimation, &QVariantAnimation::finished, this, [this]() { finishHide(); });
        m_opacityAnimation->start();
    }

    void retranslateUi()
    {
        m_themeList->setTitleText(ThemeEditorThemeDropdown::tr("Themes"));
        m_themeList->setSearchPlaceholderText(ThemeEditorThemeDropdown::tr("Search themes"));
        m_themeList->setEmptyStateTexts(ThemeEditorThemeDropdown::tr("No themes found"),
            ThemeEditorThemeDropdown::tr("Try a different search."));
        m_actionsTitle->setText(ThemeEditorThemeDropdown::tr("Actions"));
        m_newButton->setText(ThemeEditorThemeDropdown::tr("New theme"));
        m_importButton->setText(ThemeEditorThemeDropdown::tr("Import"));
        m_exportButton->setText(ThemeEditorThemeDropdown::tr("Export"));
        m_deleteButton->setToolTip(ThemeEditorThemeDropdown::tr("Delete theme"));
        m_saveAsButton->setToolTip(ThemeEditorThemeDropdown::tr("Save as new"));
        m_newButton->syncSizeToText();
        m_importButton->syncSizeToText();
        m_exportButton->syncSizeToText();
        refreshThemes();
    }

    void updateTheme()
    {
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        m_actionsTitle->setStyleSheet(QStringLiteral("color: %1;").arg(colors.text.name()));
        refreshThemes();
        if (m_visible) {
            updateScaledSizes();
            updatePopupGeometry();
            if (m_overlay) {
                m_overlay->refreshGenericPopups();
            }
        }
        update();
    }

    void updateScaledSizes()
    {
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int outerMargin = theme.scaled(kPopupShadow + kPopupMargin);
        m_rootLayout->setContentsMargins(outerMargin, outerMargin, outerMargin, outerMargin);
        m_actionsPanel->setFixedWidth(theme.scaled(kActionsBaseWidth));
        m_actionsLayout->setSpacing(theme.scaled(8));
        m_actionsLayout->setContentsMargins(theme.scaled(12), 0, 0, 0);

        QFont titleFont = theme.font(ruwa::ui::core::ThemeFontRole::Label, QFont::Bold);
        m_actionsTitle->setFont(titleFont);
        m_actionsTitle->setFixedHeight(theme.scaled(28));
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (m_visible && !m_hiding
            && (event->type() == QEvent::MouseButtonPress
                || event->type() == QEvent::NonClientAreaMouseButtonPress)) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
            const int shadow = ruwa::ui::core::ThemeManager::instance().scaled(kPopupShadow);
            const QRect popupRect(
                mapToGlobal(QPoint(shadow, shadow)), size() - QSize(shadow * 2, shadow * 2));
            const bool onAnchor = m_anchor
                && QRect(m_anchor->mapToGlobal(QPoint(0, 0)), m_anchor->size())
                       .contains(globalPosition);
            if (!popupRect.contains(globalPosition) && !onAnchor) {
                hidePopup();
            }
        } else if (m_visible && event->type() == QEvent::KeyPress) {
            const auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                hidePopup();
                return true;
            }
        } else if (m_visible
            && (event->type() == QEvent::ApplicationDeactivate
                || event->type() == QEvent::WindowDeactivate)) {
            hidePopup(false);
        } else if (m_visible && watched == m_overlay.data() && event->type() == QEvent::Resize) {
            updatePopupGeometry();
            m_overlay->refreshGenericPopups();
        }

        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int shadow = theme.scaled(kPopupShadow);
        const qreal radius = theme.scaled(qreal(kPopupRadius));
        const QRectF card = QRectF(rect()).adjusted(shadow, shadow, -shadow, -shadow);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        for (int spread = shadow; spread > 0; --spread) {
            const qreal progress = static_cast<qreal>(spread) / shadow;
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, qRound(8.0 * (1.0 - progress) + 2.0)));
            painter.drawRoundedRect(card.adjusted(-spread, -spread + 2, spread, spread + 2),
                radius + spread, radius + spread);
        }

        painter.setPen(QPen(colors.borderLight(), 1));
        painter.setBrush(colors.surfaceElevated());
        painter.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    }

private:
    CapsuleButton* createDetailedButton(ruwa::ui::core::IconProvider::StandardIcon icon,
        CapsuleButton::Variant variant, QWidget* parent)
    {
        auto* button = new CapsuleButton(QString(), variant, parent);
        button->setIcon(ruwa::ui::core::ThemeManager::instance().icons().getIcon(icon));
        button->setBaseMinimumWidth(kDetailedActionBaseWidth);
        button->setBannerBaseHeight(34);
        button->syncSizeToText();
        return button;
    }

    ruwa::ui::workspace::ToolButton* createIconButton(
        ruwa::ui::core::IconProvider::StandardIcon icon, QWidget* parent)
    {
        auto* button = new ruwa::ui::workspace::ToolButton(
            ruwa::ui::workspace::ToolButton::Mode::Action, parent);
        button->setIconType(icon);
        button->setChromeStyle(ruwa::ui::workspace::ToolButton::ChromeStyle::Surface);
        button->setBaseSquareSize(38, 18);
        button->setBorderVisible(true);
        button->setMutedNormalIcon(false);
        return button;
    }

    void updateActionStates()
    {
        const bool hasTheme = m_owner && m_owner->hasEditingTheme();
        m_exportButton->setEnabled(hasTheme);
        m_saveAsButton->setEnabled(hasTheme);
        m_deleteButton->setEnabled(hasTheme && !m_owner->editingTheme().isBuiltIn);
    }

    QPoint calculatePosition()
    {
        m_placedAbove = false;
        if (!m_overlay || !m_anchor) {
            return {};
        }

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const QPoint anchorBottom = m_overlay->mapFromGlobal(
            m_anchor->mapToGlobal(QPoint(m_anchor->width() / 2, m_anchor->height())));
        QPoint position(anchorBottom.x() - width() / 2,
            anchorBottom.y() + theme.scaled(4) - theme.scaled(kPopupShadow));

        const QRect available = m_overlay->rect().adjusted(
            theme.scaled(8), theme.scaled(8), -theme.scaled(8), -theme.scaled(8));
        position.setX(qBound(available.left(), position.x(),
            qMax(available.left(), available.right() - width() + 1)));

        if (position.y() + height() > available.bottom()) {
            const int anchorTop = m_overlay->mapFromGlobal(m_anchor->mapToGlobal(QPoint(0, 0))).y();
            position.setY(anchorTop - height() + theme.scaled(kPopupShadow - 4));
            m_placedAbove = true;
        }
        position.setY(qMax(available.top(), position.y()));
        return position;
    }

    void updatePopupGeometry()
    {
        if (!m_overlay) {
            return;
        }

        const auto& theme = ruwa::ui::core::ThemeManager::instance();
        const int availableWidth = qMax(1, m_overlay->width() - theme.scaled(16));
        const int availableHeight = qMax(1, m_overlay->height() - theme.scaled(16));
        resize(qMin(theme.scaled(kPopupBaseWidth), availableWidth),
            qMin(theme.scaled(kPopupBaseHeight), availableHeight));
        m_targetPosition = calculatePosition();
        move(m_targetPosition);
        const int shadow = theme.scaled(kPopupShadow);
        setProperty("ruwaOverlayMaskRect", rect().adjusted(shadow, shadow, -shadow, -shadow));
    }

    void finishHide()
    {
        m_opacityAnimation->stop();
        m_opacity = 0.0;
        m_opacityEffect->setOpacity(0.0);
        hide();
        m_visible = false;
        m_hiding = false;
        if (m_overlay) {
            m_overlay->refreshGenericPopups();
        }
        if (m_owner) {
            m_owner->setActive(false);
        }
    }

    QPointer<ThemeEditorThemeDropdown> m_owner;
    QPointer<OverlayContainer> m_overlay;
    QPointer<QWidget> m_anchor;
    QHBoxLayout* m_rootLayout { nullptr };
    PresetMenuListWidget* m_themeList { nullptr };
    QWidget* m_actionsPanel { nullptr };
    QVBoxLayout* m_actionsLayout { nullptr };
    QLabel* m_actionsTitle { nullptr };
    CapsuleButton* m_newButton { nullptr };
    CapsuleButton* m_importButton { nullptr };
    CapsuleButton* m_exportButton { nullptr };
    ruwa::ui::workspace::ToolButton* m_deleteButton { nullptr };
    ruwa::ui::workspace::ToolButton* m_saveAsButton { nullptr };
    QGraphicsOpacityEffect* m_opacityEffect { nullptr };
    QVariantAnimation* m_opacityAnimation { nullptr };
    qreal m_opacity { 0.0 };
    bool m_visible { false };
    bool m_hiding { false };
    bool m_placedAbove { false };
    bool m_appFilterInstalled { false };
    QPoint m_targetPosition;
};

ThemeEditorThemeDropdown::ThemeEditorThemeDropdown(QWidget* parent)
    : BaseAnimatedButton(parent)
{
    setCheckable(false);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setHoverDuration(140);
    setActiveDuration(160);
    updateScaledSize();

    connect(this, &QPushButton::clicked, this, &ThemeEditorThemeDropdown::togglePopup);
    connect(&ruwa::ui::core::ThemeManager::instance(),
        &ruwa::ui::core::ThemeManager::presetsChanged, this,
        &ThemeEditorThemeDropdown::onPresetsChanged);
    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, &ThemeEditorThemeDropdown::onThemeChanged);

    const QUuid currentId = ruwa::ui::core::ThemeManager::instance().currentPresetId();
    if (!setEditingThemeById(currentId)) {
        const auto presets = ruwa::ui::core::ThemeManager::instance().allPresets();
        if (!presets.isEmpty()) {
            setEditingThemeInternal(presets.first(), false);
        }
    }
}

ThemeEditorThemeDropdown::~ThemeEditorThemeDropdown()
{
    delete m_popup.data();
}

bool ThemeEditorThemeDropdown::setEditingThemeById(const QUuid& id)
{
    const auto presets = ruwa::ui::core::ThemeManager::instance().allPresets();
    for (const auto& preset : presets) {
        if (preset.id == id) {
            setEditingThemeInternal(preset, true);
            return true;
        }
    }
    return false;
}

void ThemeEditorThemeDropdown::setEditingTheme(const ruwa::ui::core::ThemePreset& preset)
{
    setEditingThemeInternal(preset, true);
}

ruwa::ui::core::ThemePreset ThemeEditorThemeDropdown::saveEditingTheme(
    const ruwa::ui::core::ThemePreset& preset)
{
    auto savedPreset = preset;
    if (savedPreset.isBuiltIn) {
        savedPreset = createThemeCopy(savedPreset);
    }

    // Keep the dropdown's working copy in sync before ThemeManager emits
    // presetsChanged, so an open popup rebuilds from the saved values.
    setEditingThemeInternal(savedPreset, false);
    m_presetMutationInProgress = true;
    if (preset.isBuiltIn) {
        ruwa::ui::core::ThemeManager::instance().addCustomPreset(savedPreset);
    } else {
        ruwa::ui::core::ThemeManager::instance().updateCustomPreset(savedPreset);
    }
    m_presetMutationInProgress = false;

    emit editingThemeChanged(savedPreset);
    return savedPreset;
}

void ThemeEditorThemeDropdown::setEditingThemeInternal(
    const ruwa::ui::core::ThemePreset& preset, bool notify)
{
    m_editingTheme = preset;
    m_hasEditingTheme = true;
    update();
    if (m_popup) {
        m_popup->syncSelection();
    }
    if (notify) {
        emit editingThemeChanged(m_editingTheme);
    }
}

void ThemeEditorThemeDropdown::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    const auto& colors = theme.colors();
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = theme.scaled(6.0);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.overlayBase());
    painter.drawRoundedRect(box, radius, radius);

    if (hoverProgress() > 0.001 || activeProgress() > 0.001) {
        QColor hover = colors.overlayHover();
        hover.setAlphaF(hover.alphaF() * qMax(hoverProgress() * 0.2, activeProgress() * 0.35));
        painter.setBrush(hover);
        painter.drawRoundedRect(box, radius, radius);
    }

    QColor topBorder = ruwa::ui::core::ThemeColors::interpolate(
        colors.borderSubtle(), colors.borderSubtleHover(), qMax(hoverProgress(), activeProgress()));
    QColor bottomBorder = colors.borderSubtle();
    bottomBorder.setAlpha(bottomBorder.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(painter, box, radius, topBorder, bottomBorder);

    const int previewSize = theme.scaled(22);
    const QRectF swatch(theme.scaled(8), (height() - previewSize) / 2.0, previewSize, previewSize);
    if (m_hasEditingTheme) {
        const auto swatches = previewColors(m_editingTheme);
        QPainterPath clip;
        clip.addRoundedRect(swatch, theme.scaled(4.0), theme.scaled(4.0));
        painter.save();
        painter.setClipPath(clip);
        const qreal halfWidth = swatch.width() / 2.0;
        const qreal halfHeight = swatch.height() / 2.0;
        painter.fillRect(QRectF(swatch.left(), swatch.top(), halfWidth, halfHeight), swatches[0]);
        painter.fillRect(
            QRectF(swatch.left() + halfWidth, swatch.top(), halfWidth, halfHeight), swatches[1]);
        painter.fillRect(
            QRectF(swatch.left(), swatch.top() + halfHeight, halfWidth, halfHeight), swatches[2]);
        painter.fillRect(
            QRectF(swatch.left() + halfWidth, swatch.top() + halfHeight, halfWidth, halfHeight),
            swatches[3]);
        painter.restore();
        painter.setPen(QPen(colors.borderSubtle(), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(
            swatch.adjusted(0.5, 0.5, -0.5, -0.5), theme.scaled(4.0), theme.scaled(4.0));
    }

    const int textLeft = qRound(swatch.right()) + theme.scaled(10);
    const int arrowSpace = theme.scaled(28);
    const QRect textRect(textLeft, 0, qMax(0, width() - textLeft - arrowSpace), height());
    QFont textFont = theme.font(ruwa::ui::core::ThemeFontRole::Body, QFont::Medium);
    painter.setFont(textFont);
    painter.setPen(m_hasEditingTheme ? colors.text : colors.textMuted);
    const QString name = m_hasEditingTheme
        ? ruwa::ui::core::ThemePreset::translatedDisplayName(m_editingTheme)
        : tr("Select theme");
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
        QFontMetrics(textFont).elidedText(name, Qt::ElideRight, textRect.width()));

    painter.save();
    painter.translate(width() - theme.scaled(13), height() * 0.5);
    painter.rotate(180.0 * activeProgress());
    painter.setPen(
        QPen(colors.textMuted, theme.scaled(1.6), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(
        QPointF(theme.scaled(-3.5), theme.scaled(-1.0)), QPointF(0.0, theme.scaled(2.2)));
    painter.drawLine(
        QPointF(0.0, theme.scaled(2.2)), QPointF(theme.scaled(3.5), theme.scaled(-1.0)));
    painter.restore();
}

void ThemeEditorThemeDropdown::changeEvent(QEvent* event)
{
    BaseAnimatedButton::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        update();
        if (m_popup) {
            m_popup->retranslateUi();
        }
    }
}

ThemeEditorThemePopup* ThemeEditorThemeDropdown::ensurePopup()
{
    if (!m_popup) {
        m_popup = new ThemeEditorThemePopup(this);
    }
    return m_popup;
}

void ThemeEditorThemeDropdown::togglePopup()
{
    ThemeEditorThemePopup* popup = ensurePopup();
    if (popup->isPopupVisible()) {
        popup->hidePopup();
        return;
    }

    if (popup->popupUnder(this)) {
        setActive(true);
    }
}

void ThemeEditorThemeDropdown::onPresetsChanged()
{
    if (m_popup) {
        m_popup->refreshThemes();
    }
    if (m_presetMutationInProgress) {
        return;
    }
    if (!m_hasEditingTheme || !setEditingThemeById(m_editingTheme.id)) {
        const auto presets = ruwa::ui::core::ThemeManager::instance().allPresets();
        if (!presets.isEmpty()) {
            setEditingThemeInternal(presets.first(), true);
        }
    }
}

void ThemeEditorThemeDropdown::onThemeChanged()
{
    updateScaledSize();
    update();
    if (m_popup) {
        m_popup->updateTheme();
    }
}

void ThemeEditorThemeDropdown::createNewTheme()
{
    const auto* activeTheme = ruwa::ui::core::ThemeManager::instance().currentPreset();
    ruwa::ui::core::ThemePreset preset
        = activeTheme ? *activeTheme : ruwa::ui::core::ThemePreset::obsidian();
    preset.id = QUuid::createUuid();
    preset.name = uniqueThemeName(tr("New Theme"));
    preset.description.clear();
    preset.isBuiltIn = false;
    preset.isFavorite = false;

    m_presetMutationInProgress = true;
    ruwa::ui::core::ThemeManager::instance().addCustomPreset(preset);
    m_presetMutationInProgress = false;
    setEditingThemeInternal(preset, true);
}

void ThemeEditorThemeDropdown::importTheme()
{
    if (m_popup) {
        m_popup->hidePopup(false);
    }

    const QString path = ruwa::shared::filedialog::getOpenFileName(this,
        ruwa::shared::filedialog::category::kTheme, tr("Import theme"),
        tr("Ruwa theme preset (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showInfo(tr("Import Theme"), tr("Could not read file."));
        return;
    }

    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        showInfo(tr("Import Theme"), parseError.errorString());
        return;
    }
    if (!document.isObject()) {
        showInfo(tr("Import Theme"), tr("Invalid file format."));
        return;
    }

    QString errorMessage;
    ruwa::ui::core::ThemePreset imported;
    if (!ruwa::ui::core::theme_preset_json::fromDocumentObject(
            document.object(), imported, &errorMessage)) {
        showInfo(tr("Import Theme"), errorMessage);
        return;
    }

    imported.name = uniqueThemeName(imported.name);
    m_presetMutationInProgress = true;
    ruwa::ui::core::ThemeManager::instance().addCustomPreset(imported);
    m_presetMutationInProgress = false;
    setEditingThemeInternal(imported, true);
}

void ThemeEditorThemeDropdown::exportTheme()
{
    if (!m_hasEditingTheme) {
        return;
    }
    if (m_popup) {
        m_popup->hidePopup(false);
    }

    const QString path = ruwa::shared::filedialog::getSaveFileName(this,
        ruwa::shared::filedialog::category::kTheme, tr("Export theme"),
        m_editingTheme.name + QStringLiteral(".json"), tr("Ruwa theme preset (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showInfo(tr("Export Theme"), tr("Could not write file."));
        return;
    }

    const QJsonObject document
        = ruwa::ui::core::theme_preset_json::toDocumentObject(m_editingTheme);
    const QByteArray json = QJsonDocument(document).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.flush()) {
        showInfo(tr("Export Theme"), tr("Could not write file."));
    }
}

void ThemeEditorThemeDropdown::deleteTheme()
{
    if (!m_hasEditingTheme || m_editingTheme.isBuiltIn) {
        return;
    }
    if (m_popup) {
        m_popup->hidePopup(false);
    }

    const QString prompt = tr("Delete Theme") + QStringLiteral("\n")
        + tr("Are you sure you want to delete '%1'?").arg(m_editingTheme.name);
    if (!MessagePopupManager::showBlocking(this, prompt, tr("Delete"), tr("Cancel"), 360, false)) {
        return;
    }

    const auto before = ruwa::ui::core::ThemeManager::instance().allPresets();
    int deletedIndex = 0;
    for (int index = 0; index < before.size(); ++index) {
        if (before[index].id == m_editingTheme.id) {
            deletedIndex = index;
            break;
        }
    }

    m_presetMutationInProgress = true;
    ruwa::ui::core::ThemeManager::instance().removeCustomPreset(m_editingTheme.id);
    m_presetMutationInProgress = false;

    const auto remaining = ruwa::ui::core::ThemeManager::instance().allPresets();
    if (remaining.isEmpty()) {
        m_hasEditingTheme = false;
        update();
        return;
    }
    setEditingThemeInternal(remaining[qMin(deletedIndex, remaining.size() - 1)], true);
}

void ThemeEditorThemeDropdown::saveThemeAsNew()
{
    if (!m_hasEditingTheme) {
        return;
    }

    const auto copy = createThemeCopy(m_editingTheme);
    m_presetMutationInProgress = true;
    ruwa::ui::core::ThemeManager::instance().addCustomPreset(copy);
    m_presetMutationInProgress = false;
    setEditingThemeInternal(copy, true);
}

ruwa::ui::core::ThemePreset ThemeEditorThemeDropdown::createThemeCopy(
    const ruwa::ui::core::ThemePreset& preset) const
{
    auto copy = preset;
    copy.id = QUuid::createUuid();
    copy.name = uniqueThemeName(
        tr("%1 Copy").arg(ruwa::ui::core::ThemePreset::translatedDisplayName(preset)));
    copy.isBuiltIn = false;
    copy.isFavorite = false;
    return copy;
}

QString ThemeEditorThemeDropdown::uniqueThemeName(const QString& requestedName) const
{
    const QString baseName
        = requestedName.trimmed().isEmpty() ? tr("Theme") : requestedName.trimmed();
    QSet<QString> existingNames;
    for (const auto& preset : ruwa::ui::core::ThemeManager::instance().allPresets()) {
        existingNames.insert(preset.name.trimmed().toCaseFolded());
    }
    if (!existingNames.contains(baseName.toCaseFolded())) {
        return baseName;
    }

    for (int suffix = 2;; ++suffix) {
        const QString candidate = QStringLiteral("%1 %2").arg(baseName).arg(suffix);
        if (!existingNames.contains(candidate.toCaseFolded())) {
            return candidate;
        }
    }
}

void ThemeEditorThemeDropdown::showInfo(const QString& title, const QString& message)
{
    const QString text = title.isEmpty() ? message : QStringLiteral("%1\n%2").arg(title, message);
    MessagePopupManager::show(this, text, { { tr("OK"), true, []() { } } }, 360);
}

void ThemeEditorThemeDropdown::updateScaledSize()
{
    setFixedHeight(ruwa::ui::core::ThemeManager::instance().scaled(36));
}

} // namespace ruwa::ui::widgets
