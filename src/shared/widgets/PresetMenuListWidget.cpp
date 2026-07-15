// SPDX-License-Identifier: MPL-2.0

// PresetMenuListWidget.cpp
#include "PresetMenuListWidget.h"
#include "PresetListRowWidget.h"
#include "ListCollapseAnimator.h"
#include "shared/widgets/BaseAnimatedButton.h"
#include "shared/widgets/inputs/SearchBar.h"
#include "shared/widgets/layout/SmoothScrollArea.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/style/PaintingUtils.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>
#include <QSet>
#include <QSizePolicy>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>
#include <optional>

namespace ruwa::ui::widgets {

using ruwa::ui::core::IconProvider;
using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;

namespace {
constexpr int kBasePopupMargin = 12;
constexpr int kBaseEmbeddedMargin = 4;
constexpr int kBaseHeaderSpacing = 10;
constexpr int kBaseRowSpacing = 4;
constexpr int kBaseScrollMinHeight = 120;
/// Taller list chrome in floating popup mode (e.g. layout presets).
constexpr int kBasePopupScrollMinHeight = 320;
constexpr int kPanelRadius = 18;
constexpr int kBaseHeaderButtonSize = 32;
constexpr int kBaseHeaderButtonIconSize = 14;
constexpr int kBaseDividerActionButtonSize = 18;
constexpr int kBaseSearchHeight = 34;
constexpr int kBuiltinImportActionId = -101;
constexpr int kBuiltinExportActionId = -102;
constexpr int kBuiltinDividerRenameActionId = -103;

class PresetHeaderToolButton final : public BaseAnimatedButton {
public:
    PresetHeaderToolButton(QWidget* parent, int actionId, IconProvider::StandardIcon iconKind,
        const QString& fallbackText, bool primaryFilled)
        : BaseAnimatedButton(parent)
        , m_actionId(actionId)
        , m_iconKind(iconKind)
        , m_fallbackText(fallbackText)
        , m_primaryFilled(primaryFilled)
    {
        setCheckable(false);
        setText(QString());
        setIcon(QIcon());
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setHoverDuration(180);
        setActiveDuration(120);
        setFlat(true);
        setAutoDefault(false);
        setDefault(false);
    }

    int actionId() const { return m_actionId; }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        ThemeManager& tm = ThemeManager::instance();
        const auto& colors = tm.colors();
        const int radius = qMax(6, tm.scaled(8));
        const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);

        if (m_primaryFilled) {
            QColor bg = colors.primary;
            if (hoverProgress() > 0.0 && !isPressed()) {
                bg = ThemeColors::adjustBrightness(colors.primary, 1.0 + hoverProgress() * 0.08);
            }
            bg.setAlpha(255);
            if (isPressed()) {
                bg = colors.primaryPressed();
            }

            painter.setPen(Qt::NoPen);
            painter.setBrush(bg);
            painter.drawRoundedRect(rect, radius, radius);

            ruwa::ui::painting::drawGradientBorder(painter, rect, radius,
                ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.2 : 0.9),
                ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 1.1 : 0.95));

            if (isPressed()) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(colors.shadow(25));
                painter.drawRoundedRect(rect, radius, radius);
            }

            paintIcon(painter, tm, colors.textOnPrimary());
        } else {
            // Ghost import/export: don't animate border color on the same rect as a fading fill —
            // Qt's AA + semi-transparent interior causes the stroke to shimmer. Fixed cosmetic
            // outline on the outer path; hover/press only inside an inset rect.
            const int innerRadius = qMax(1, radius - 1);
            const QRectF innerRect = rect.adjusted(1.0, 1.0, -1.0, -1.0);

            if (hoverProgress() > 0.0) {
                QColor hover = colors.surfaceHover();
                hover.setAlphaF(hover.alphaF() * hoverProgress());
                painter.setPen(Qt::NoPen);
                painter.setBrush(hover);
                painter.drawRoundedRect(innerRect, innerRadius, innerRadius);
            }
            if (isPressed()) {
                QColor press = colors.primaryPressed();
                press.setAlphaF(0.35);
                painter.setPen(Qt::NoPen);
                painter.setBrush(press);
                painter.drawRoundedRect(innerRect, innerRadius, innerRadius);
            }

            QPen borderPen(colors.borderSubtle(), 1);
            borderPen.setCosmetic(true);
            borderPen.setJoinStyle(Qt::RoundJoin);
            borderPen.setCapStyle(Qt::RoundCap);
            painter.setPen(borderPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect, radius, radius);

            const QColor iconColor
                = ThemeColors::interpolate(colors.textMuted, colors.text, hoverProgress());
            paintIcon(painter, tm, iconColor);
        }
    }

private:
    void paintIcon(QPainter& painter, ThemeManager& tm, const QColor& iconColor)
    {
        const int iconSize = tm.scaled(kBaseHeaderButtonIconSize);

        const QPixmap px
            = IconProvider::instance().getPixmap(m_iconKind, QSize(iconSize, iconSize));
        if (!px.isNull()) {
            const QPixmap colored = ruwa::ui::painting::tintedPixmap(px, iconColor);
            const qreal dpr = colored.devicePixelRatio();
            const int w = qRound(colored.width() / dpr);
            const int h = qRound(colored.height() / dpr);
            painter.drawPixmap((width() - w) / 2, (height() - h) / 2, colored);
            return;
        }

        QFont f = painter.font();
        f.setPixelSize(qMax(10, iconSize - 2));
        f.setWeight(QFont::DemiBold);
        painter.setFont(f);
        painter.setPen(iconColor);
        painter.drawText(rect(), Qt::AlignCenter, m_fallbackText);
    }

    int m_actionId = 0;
    IconProvider::StandardIcon m_iconKind = IconProvider::StandardIcon::BasicFile;
    QString m_fallbackText;
    bool m_primaryFilled = false;
};

QString itemSearchText(const PresetMenuItem& item)
{
    if (item.isDivider) {
        return QString();
    }
    QStringList fields;
    fields << item.title << item.subtitle << item.badgeText << item.searchText;
    return fields.join(QLatin1Char(' ')).trimmed();
}

class PresetListDivider final : public QWidget, public IContextMenuProvider {
public:
    explicit PresetListDivider(const PresetMenuItem& item, QWidget* parent)
        : QWidget(parent)
        , m_item(item)
    {
        setAttribute(Qt::WA_TranslucentBackground);

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        m_titleLabel = new QLabel(item.title, this);
        m_titleLabel->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        layout->addWidget(m_titleLabel, 0, Qt::AlignVCenter);

        layout->addStretch(1);

        if (!item.extraActions.isEmpty()) {
            const PresetMenuExtraAction& action = item.extraActions.first();
            m_actionId = action.id;
            m_actionButton = new DividerActionButton(action.icon, action.dangerHover, this);
            m_actionButton->setCursor(Qt::PointingHandCursor);
            m_actionButton->setToolTip(action.text.isEmpty() ? item.title : action.text);
            connect(m_actionButton, &QAbstractButton::clicked, this, [this]() {
                if (m_onActionTriggered && m_actionId != 0) {
                    m_onActionTriggered(m_actionId);
                }
            });
            layout->addWidget(m_actionButton, 0, Qt::AlignVCenter);
        }

        applyTheme();
        setFixedHeight(ThemeManager::instance().scaled(24));
    }

    void setActionHandler(std::function<void(int)> handler)
    {
        m_onActionTriggered = std::move(handler);
    }

    void setContextMenuEnabled(bool enabled) { m_contextMenuEnabled = enabled; }

    void setRenamable(bool renamable) { m_renamable = renamable; }

    void setRenameHandler(std::function<void(const QString&)> handler)
    {
        m_onRenamed = std::move(handler);
    }

    QVariant userData() const { return m_item.userData; }

    void setTitleText(const QString& title)
    {
        m_item.title = title;
        if (m_titleLabel) {
            m_titleLabel->setText(title);
        }
    }

    // Seamlessly swap the title label for an inline editor positioned exactly
    // over the label, styled to be visually indistinguishable from it (same
    // font, color, no frame, zero margins) so renaming looks in-place.
    void startEditing()
    {
        if (!m_renamable || !m_titleLabel || m_editor) {
            return;
        }

        const auto& theme = ThemeManager::instance();
        const auto& colors = theme.colors();

        m_editor = new QLineEdit(m_titleLabel->text(), this);
        m_editor->setFrame(false);
        // QLineEdit reserves an intrinsic ~2px horizontal / 1px vertical text
        // margin that a QLabel does not. Cancel it with negative text margins so
        // the glyphs land on exactly the same pixels as the label.
        m_editor->setTextMargins(-2, -1, 0, 0);
        m_editor->setContentsMargins(0, 0, 0, 0);
        m_editor->setFont(m_titleLabel->font());
        m_editor->setAttribute(Qt::WA_MacShowFocusRect, false);
        m_editor->setStyleSheet(QStringLiteral("QLineEdit { background: transparent; border: none; "
                                               "padding: 0; margin: 0; color: %1; "
                                               "selection-background-color: %2; }")
                .arg(colors.textMuted.name(QColor::HexArgb))
                .arg(colors.primary.name(QColor::HexArgb)));

        // Reuse the label's exact rect (its layout-centered y and height) so the
        // text doesn't jump vertically; only widen rightward toward the action
        // button (or the right margin) to give room to type.
        const QRect labelGeom = m_titleLabel->geometry();
        int right = width() - theme.scaled(4);
        if (m_actionButton && m_actionButton->isVisible()) {
            right = m_actionButton->x() - theme.scaled(6);
        }
        m_editor->setGeometry(labelGeom.x(), labelGeom.y(),
            qMax(theme.scaled(20), right - labelGeom.x()), labelGeom.height());

        m_titleLabel->hide();
        m_editor->installEventFilter(this);
        // App-level filter so a click anywhere outside the editor commits the
        // rename — editingFinished alone misses clicks on non-focusable areas
        // (canvas, list background) that never steal focus from the editor.
        qApp->installEventFilter(this);
        connect(m_editor, &QLineEdit::editingFinished, this, [this]() { finishEditing(true); });

        m_editor->show();
        m_editor->setFocus();
        m_editor->selectAll();
    }

    void applyTheme()
    {
        const auto& theme = ThemeManager::instance();
        const auto& colors = theme.colors();
        const int actionSize = theme.scaled(kBaseDividerActionButtonSize);

        if (layout()) {
            layout()->setContentsMargins(theme.scaled(4), 0, theme.scaled(4), 0);
            layout()->setSpacing(theme.scaled(6));
        }

        if (m_titleLabel) {
            QFont font = m_titleLabel->font();
            font.setPixelSize(theme.scaled(10));
            font.setWeight(QFont::DemiBold);
            m_titleLabel->setFont(font);
            m_titleLabel->setStyleSheet(
                QStringLiteral("QLabel { color: %1; background: transparent; }")
                    .arg(colors.textMuted.name(QColor::HexArgb)));
        }

        if (m_actionButton) {
            m_actionButton->setFixedSize(actionSize, actionSize);
            m_actionButton->update();
        }

        setFixedHeight(qMax(theme.scaled(20), actionSize));
        update();
    }

    ContextMenuType contextMenuType() const override
    {
        return contextMenuContext().isEmpty() ? ContextMenuType::None
                                              : ContextMenuType::SimpleActions;
    }

    QVariantMap contextMenuContext() const override
    {
        if (!m_contextMenuEnabled || (m_item.extraActions.isEmpty() && !m_renamable)) {
            return {};
        }

        QVariantList actions;
        if (m_renamable) {
            QVariantMap renameRow;
            renameRow.insert(QStringLiteral("id"), kBuiltinDividerRenameActionId);
            renameRow.insert(QStringLiteral("text"), tr("Rename"));
            renameRow.insert(
                QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Edit));
            actions.append(renameRow);
        }
        for (const PresetMenuExtraAction& action : m_item.extraActions) {
            if (action.id == 0 || action.text.trimmed().isEmpty()) {
                continue;
            }

            QVariantMap row;
            row.insert(QStringLiteral("id"), action.id);
            row.insert(QStringLiteral("text"), action.text);
            row.insert(QStringLiteral("danger"), action.dangerHover);
            row.insert(QStringLiteral("checked"), action.checked);
            if (!action.checked) {
                row.insert(QStringLiteral("standardIcon"), static_cast<int>(action.icon));
            }
            actions.append(row);
        }

        return actions.isEmpty() ? QVariantMap {}
                                 : QVariantMap { { QStringLiteral("simpleActions"), actions } };
    }

    void handleContextMenuAction(int actionId) override
    {
        if (actionId == kBuiltinDividerRenameActionId) {
            startEditing();
            return;
        }
        if (m_onActionTriggered && actionId != 0) {
            m_onActionTriggered(actionId);
        }
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const bool overAction = m_actionButton && m_actionButton->isVisible()
            && m_actionButton->geometry().contains(event->pos());
        if (m_renamable && event->button() == Qt::LeftButton && !overAction) {
            startEditing();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (m_editor) {
            if (watched == m_editor && event->type() == QEvent::KeyPress) {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->key() == Qt::Key_Escape) {
                    finishEditing(false);
                    return true;
                }
            } else if (event->type() == QEvent::MouseButtonPress) {
                // Commit on any press that isn't inside the editor. Don't consume
                // the event so the click also does its normal job (selecting a
                // brush, focusing another control, etc.).
                auto* widget = qobject_cast<QWidget*>(watched);
                const bool insideEditor
                    = watched == m_editor || (widget && m_editor->isAncestorOf(widget));
                if (!insideEditor) {
                    finishEditing(true);
                }
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const auto& colors = ThemeManager::instance().colors();
        painter.setPen(QPen(colors.borderSubtle(), 1, Qt::SolidLine, Qt::RoundCap));
        const int y = height() / 2;
        const int pad = ThemeManager::instance().scaled(4);
        int lineStart = pad;
        if (m_titleLabel && !m_titleLabel->text().trimmed().isEmpty()) {
            lineStart = qMax(
                lineStart, m_titleLabel->geometry().right() + ThemeManager::instance().scaled(10));
        }
        int lineEnd = width() - pad;
        if (m_actionButton && m_actionButton->isVisible()) {
            lineEnd = qMin(
                lineEnd, m_actionButton->geometry().left() - ThemeManager::instance().scaled(8));
        }
        lineEnd = qMax(lineStart, lineEnd);
        painter.drawLine(lineStart, y, lineEnd, y);
    }

private:
    void finishEditing(bool accept)
    {
        if (!m_editor) {
            return;
        }

        // Detach first so the editingFinished triggered by hide()/focus-out
        // can't re-enter this method.
        QLineEdit* editor = m_editor;
        m_editor = nullptr;
        editor->removeEventFilter(this);
        qApp->removeEventFilter(this);

        const QString newText = editor->text().trimmed();
        const QString oldText = m_titleLabel ? m_titleLabel->text() : QString();

        if (m_titleLabel) {
            m_titleLabel->show();
        }
        editor->deleteLater();

        if (accept && !newText.isEmpty() && newText != oldText) {
            if (m_titleLabel) {
                m_titleLabel->setText(newText);
            }
            if (m_onRenamed) {
                m_onRenamed(newText);
            }
        }
    }

    class DividerActionButton final : public BaseAnimatedButton {
    public:
        DividerActionButton(IconProvider::StandardIcon icon, bool dangerHover, QWidget* parent)
            : BaseAnimatedButton(parent)
            , m_icon(icon)
            , m_dangerHover(dangerHover)
        {
            setCheckable(false);
            setText(QString());
            setCursor(Qt::PointingHandCursor);
            setFlat(true);
            setFocusPolicy(Qt::NoFocus);
            setHoverDuration(150);
            setActiveDuration(120);
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        }

    protected:
        void paintEvent(QPaintEvent* event) override
        {
            Q_UNUSED(event);

            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);

            const auto& colors = ThemeManager::instance().colors();
            const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
            const int radius = qMax(4, ThemeManager::instance().scaled(6));
            const qreal hp = hoverProgress();

            if (hp > 0.0 || isPressed()) {
                painter.setPen(Qt::NoPen);
                QColor fill = m_dangerHover ? ThemeColors::withAlpha(colors.error,
                                                  isPressed() ? 42 : static_cast<int>(30 * hp))
                                            : ThemeColors::withAlpha(colors.surfaceHover(),
                                                  isPressed() ? 110 : static_cast<int>(80 * hp));
                painter.setBrush(fill);
                painter.drawRoundedRect(rect, radius, radius);
            }

            QColor border = m_dangerHover
                ? ThemeColors::interpolate(
                      colors.borderSubtle(), ThemeColors::withAlpha(colors.error, 110), hp)
                : ThemeColors::interpolate(colors.borderSubtle(), colors.borderSubtleHover(), hp);
            painter.setPen(QPen(border, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(rect, radius, radius);

            QColor iconColor = m_dangerHover
                ? ThemeColors::interpolate(colors.textMuted, colors.error, qMin(1.0, hp * 1.15))
                : ThemeColors::interpolate(colors.textMuted, colors.text, hp * 0.75);
            const int iconSize = qMax(10, ThemeManager::instance().scaled(12));
            const QPixmap pixmap = IconProvider::instance()
                                       .getColoredIcon(m_icon, iconColor)
                                       .pixmap(iconSize, iconSize);
            if (!pixmap.isNull()) {
                painter.drawPixmap(
                    (width() - pixmap.width()) / 2, (height() - pixmap.height()) / 2, pixmap);
            }
        }

    private:
        IconProvider::StandardIcon m_icon = IconProvider::StandardIcon::Trash;
        bool m_dangerHover = false;
    };

    PresetMenuItem m_item;
    QLabel* m_titleLabel = nullptr;
    DividerActionButton* m_actionButton = nullptr;
    int m_actionId = 0;
    std::function<void(int)> m_onActionTriggered;
    bool m_contextMenuEnabled = false;
    bool m_renamable = false;
    QLineEdit* m_editor = nullptr;
    std::function<void(const QString&)> m_onRenamed;
};

class PresetFooterActionButton final : public BaseAnimatedButton {
public:
    explicit PresetFooterActionButton(QWidget* parent = nullptr)
        : BaseAnimatedButton(parent)
    {
        setCheckable(false);
        setText(QString());
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setHoverDuration(160);
        setActiveDuration(120);
        setFlat(true);
        setAutoDefault(false);
        setDefault(false);
    }

    void setButtonText(const QString& text)
    {
        m_text = text;
        update();
    }

    void setButtonIcon(IconProvider::StandardIcon icon)
    {
        m_icon = icon;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);

        const auto& theme = ThemeManager::instance();
        const auto& colors = theme.colors();

        if (QWidget* parent = parentWidget()) {
            parent->render(&painter, -pos(), QRegion(geometry()), QWidget::DrawWindowBackground);
        } else {
            painter.fillRect(rect(), colors.surfaceElevated());
        }

        const QRectF box = rect().adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = box.height() / 2.0;

        QColor bg = colors.primary;
        bg = ThemeColors::adjustBrightness(bg, 1.0 + hoverProgress() * 0.08);
        if (isPressed()) {
            bg = colors.primaryPressed();
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(box, radius, radius);

        QFont f = font();
        f.setPixelSize(theme.scaled(13));
        f.setWeight(QFont::DemiBold);
        painter.setFont(f);
        painter.setPen(colors.textOnPrimary());

        const int iconSize = theme.scaled(14);
        const int gap = theme.scaled(7);
        const int textWidth = painter.fontMetrics().horizontalAdvance(m_text);
        const int contentWidth = iconSize + gap + textWidth;
        int x = (width() - contentWidth) / 2;
        const int y = (height() - iconSize) / 2;

        const QPixmap icon = IconProvider::instance()
                                 .getColoredIcon(m_icon, colors.textOnPrimary())
                                 .pixmap(iconSize, iconSize);
        if (!icon.isNull()) {
            painter.drawPixmap(x, y, icon);
        }
        x += iconSize + gap;

        QRect textRect(x, 0, qMax(0, width() - x), height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_text);
    }

private:
    QString m_text;
    IconProvider::StandardIcon m_icon = IconProvider::StandardIcon::FileNew;
};

void clearLayout(QLayout* layout)
{
    if (!layout) {
        return;
    }

    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* widget = item->widget()) {
            delete widget;
        }
        delete item;
    }
}
} // namespace

PresetMenuListWidget::PresetMenuListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_headerWidget = new QWidget(this);
    auto* headerLayout = new QVBoxLayout(m_headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(0);

    m_headerTopRow = new QWidget(m_headerWidget);
    auto* topRowLayout = new QHBoxLayout(m_headerTopRow);
    topRowLayout->setContentsMargins(0, 0, 0, 0);
    topRowLayout->setSpacing(0);

    m_titleLabel = new QLabel(m_headerTopRow);
    m_titleLabel->hide();
    topRowLayout->addWidget(m_titleLabel, 1);

    m_headerActionsWidget = new QWidget(m_headerTopRow);
    m_headerActionsLayout = new QHBoxLayout(m_headerActionsWidget);
    m_headerActionsLayout->setContentsMargins(0, 0, 0, 0);
    m_headerActionsLayout->setSpacing(0);
    topRowLayout->addWidget(m_headerActionsWidget, 0, Qt::AlignRight);
    headerLayout->addWidget(m_headerTopRow);

    m_searchBar = new SearchBar(m_headerWidget);
    m_searchBar->setClickOutsideClearsFocus(true);
    m_searchBar->setClearButtonEnabled(true);
    m_searchBar->setMinimumWidth(0);
    connect(m_searchBar, &SearchBar::textChanged, this, [this](const QString& text) {
        rebuildRows();
        emit searchTextChanged(text);
    });
    headerLayout->addWidget(m_searchBar);
    outer->addWidget(m_headerWidget);

    m_scrollArea = new SmoothScrollArea(this);
    m_scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_listContent = new QWidget();
    m_listLayout = new QVBoxLayout(m_listContent);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(0);

    m_scrollArea->setWidget(m_listContent);
    outer->addWidget(m_scrollArea, 1);

    m_collapseAnimator = new ListCollapseAnimator(this);
    connect(m_collapseAnimator, &ListCollapseAnimator::stepped, this, [this]() {
        if (m_scrollArea) {
            m_scrollArea->refreshScrollGeometry();
        }
    });

    m_emptyState = new QWidget(this);
    auto* emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(0, 0, 0, 0);
    emptyLayout->setSpacing(0);
    emptyLayout->setAlignment(Qt::AlignCenter);

    m_emptyIconLabel = new QLabel(m_emptyState);
    m_emptyIconLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyIconLabel, 0, Qt::AlignHCenter);

    m_emptyTitleLabel = new QLabel(m_emptyState);
    m_emptyTitleLabel->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(m_emptyTitleLabel, 0, Qt::AlignHCenter);

    m_emptyDescriptionLabel = new QLabel(m_emptyState);
    m_emptyDescriptionLabel->setAlignment(Qt::AlignCenter);
    m_emptyDescriptionLabel->setWordWrap(true);
    emptyLayout->addWidget(m_emptyDescriptionLabel, 0, Qt::AlignHCenter);
    outer->addWidget(m_emptyState, 1);

    m_footerHost = new QWidget(this);
    auto* footerLayout = new QVBoxLayout(m_footerHost);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(0);

    m_footerButton = new PresetFooterActionButton(m_footerHost);
    m_footerButton->hide();
    connect(m_footerButton, &QAbstractButton::clicked, this, [this]() {
        if (m_footerAction.id != 0) {
            emit headerActionTriggered(m_footerAction.id);
        }
    });
    footerLayout->addWidget(m_footerButton);
    outer->addWidget(m_footerHost);

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &PresetMenuListWidget::onThemeChanged);

    applyContentMargins();
    setTitleText(tr("Presets"));
    setEmptyStateTexts(
        tr("No presets found"), tr("Try a different search or create a new preset."));
    setImportExportVisible(true);
    setSearchEnabled(true);
    setSearchPlaceholderText(tr("Search presets"));
    setPopupStyle(false);
    rebuildHeaderActions();
    rebuildRows();
}

PresetMenuListWidget::~PresetMenuListWidget() = default;

namespace {
void setWidgetLayerChrome(QWidget* w, bool translucent)
{
    if (!w) {
        return;
    }
    w->setAttribute(Qt::WA_TranslucentBackground, translucent);
    w->setAttribute(Qt::WA_NoSystemBackground, translucent);
    w->setAutoFillBackground(!translucent);
}
} // namespace

void PresetMenuListWidget::applyLayerChromeTransparency(bool transparent)
{
    setWidgetLayerChrome(m_headerWidget, transparent);
    setWidgetLayerChrome(m_headerTopRow, transparent);
    setWidgetLayerChrome(m_headerActionsWidget, transparent);
    setWidgetLayerChrome(m_listContent, transparent);
    setWidgetLayerChrome(m_emptyState, transparent);
    setWidgetLayerChrome(m_footerHost, transparent);
}

void PresetMenuListWidget::setPopupStyle(bool popup)
{
    if (m_popupStyle == popup) {
        return;
    }
    m_popupStyle = popup;
    const bool transparentChrome = popup || m_embeddedChromeTransparent;
    setAttribute(Qt::WA_TranslucentBackground, transparentChrome);
    setAttribute(Qt::WA_NoSystemBackground, transparentChrome);
    setAutoFillBackground(!transparentChrome);
    if (m_scrollArea) {
        m_scrollArea->setFillBackground(!transparentChrome);
        m_scrollArea->setScrollBarTransparentTrack(transparentChrome);
    }
    applyLayerChromeTransparency(transparentChrome);
    for (PresetListRowWidget* row : std::as_const(m_rows)) {
        if (row) {
            row->setPopupChromeStyle(popup);
        }
    }
    applyContentMargins();
    update();
}

void PresetMenuListWidget::setPopupPanelPainted(bool painted)
{
    if (m_popupPanelPainted == painted) {
        return;
    }
    m_popupPanelPainted = painted;
    if (!painted) {
        m_glassBackdrop = {};
    }
    update();
}

void PresetMenuListWidget::setEmbeddedChromeTransparent(bool transparent)
{
    if (m_embeddedChromeTransparent == transparent) {
        return;
    }

    m_embeddedChromeTransparent = transparent;
    if (m_popupStyle) {
        return;
    }

    setAttribute(Qt::WA_TranslucentBackground, transparent);
    setAttribute(Qt::WA_NoSystemBackground, transparent);
    setAutoFillBackground(!transparent);
    if (m_scrollArea) {
        m_scrollArea->setFillBackground(!transparent);
        m_scrollArea->setScrollBarTransparentTrack(transparent);
    }
    applyLayerChromeTransparency(transparent);
    update();
}

void PresetMenuListWidget::refreshGlassBackdropFrom(QWidget* source)
{
    if (!m_popupStyle || !m_popupPanelPainted || !source || rect().isEmpty()) {
        m_glassBackdrop = {};
        update();
        return;
    }

    QWidget* window = source->window();
    if (!window) {
        m_glassBackdrop = {};
        update();
        return;
    }

    const QPoint topLeftInWindow = mapTo(window, QPoint(0, 0));
    QPixmap snapshot = window->grab(QRect(topLeftInWindow, size()));
    if (snapshot.isNull()) {
        m_glassBackdrop = {};
        update();
        return;
    }

    const auto& theme = ThemeManager::instance();
    m_glassBackdrop = ruwa::ui::painting::blurSnapshotPixmap(snapshot, theme.scaled(38));
    update();
}

void PresetMenuListWidget::setTitleText(const QString& text)
{
    m_titleLabel->setText(text);
    updateSectionVisibility();
}

QString PresetMenuListWidget::titleText() const
{
    return m_titleLabel->text();
}

void PresetMenuListWidget::setHeaderActions(const QVector<PresetMenuHeaderAction>& actions)
{
    m_headerActions = actions;
    rebuildHeaderActions();
}

void PresetMenuListWidget::setFooterAction(const PresetMenuHeaderAction& action)
{
    m_footerAction = action;
    rebuildFooterAction();
}

void PresetMenuListWidget::setContextMenuEnabled(bool enabled)
{
    if (m_contextMenuEnabled == enabled) {
        return;
    }

    m_contextMenuEnabled = enabled;
    rebuildRows();
}

void PresetMenuListWidget::setItems(const QVector<PresetMenuItem>& items)
{
    if (tryAnimatedRemoval(items)) {
        return;
    }
    m_items = items;
    rebuildRows();
}

QVector<QVariant> PresetMenuListWidget::visibleItemUserData(int preloadMargin) const
{
    QVector<QVariant> result;
    if (!m_scrollArea || !m_scrollArea->viewport() || !m_listContent) {
        return result;
    }

    const int margin = qMax(0, preloadMargin);
    QRect visibleRect(0, m_scrollArea->scrollValue() - margin, m_scrollArea->viewport()->width(),
        m_scrollArea->viewport()->height() + margin * 2);

    for (PresetListRowWidget* row : m_rows) {
        if (!row || !row->isVisible()) {
            continue;
        }
        if (row->geometry().intersects(visibleRect)) {
            result.append(row->userData());
        }
    }

    return result;
}

ContextMenuType PresetMenuListWidget::contextMenuType() const
{
    return contextMenuContext().isEmpty() ? ContextMenuType::None : ContextMenuType::SimpleActions;
}

QVariantMap PresetMenuListWidget::contextMenuContext() const
{
    if (!m_contextMenuEnabled) {
        return {};
    }

    QVariantList actions;

    if (m_importExportVisible) {
        QVariantMap importAction;
        importAction.insert(QStringLiteral("id"), kBuiltinImportActionId);
        importAction.insert(QStringLiteral("text"), tr("Import"));
        importAction.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Import));
        actions.append(importAction);

        QVariantMap exportAction;
        exportAction.insert(QStringLiteral("id"), kBuiltinExportActionId);
        exportAction.insert(QStringLiteral("text"), tr("Export"));
        exportAction.insert(
            QStringLiteral("standardIcon"), static_cast<int>(IconProvider::StandardIcon::Export));
        actions.append(exportAction);
    }

    for (const PresetMenuHeaderAction& action : m_headerActions) {
        if (!action.visible || action.id == 0) {
            continue;
        }

        QVariantMap row;
        row.insert(QStringLiteral("id"), action.id);
        row.insert(QStringLiteral("text"), action.text.isEmpty() ? action.toolTip : action.text);
        row.insert(QStringLiteral("standardIcon"), static_cast<int>(action.icon));
        actions.append(row);
    }

    return actions.isEmpty() ? QVariantMap {}
                             : QVariantMap { { QStringLiteral("simpleActions"), actions } };
}

void PresetMenuListWidget::handleContextMenuAction(int actionId)
{
    if (actionId == kBuiltinImportActionId) {
        emit importClicked();
        return;
    }

    if (actionId == kBuiltinExportActionId) {
        emit exportClicked();
        return;
    }

    emit headerActionTriggered(actionId);
}

bool PresetMenuListWidget::setExtraActionsForItem(
    const QVariant& userData, const QVector<PresetMenuExtraAction>& actions)
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].userData != userData) {
            continue;
        }
        m_items[i].extraActions = actions;

        PresetListRowWidget* row = (i < m_rows.size()) ? m_rows[i] : nullptr;
        if (row && row->userData() == userData) {
            row->setExtraActions(actions);
            return true;
        }
        for (PresetListRowWidget* r : m_rows) {
            if (r && r->userData() == userData) {
                r->setExtraActions(actions);
                return true;
            }
        }
        return true;
    }
    return false;
}

bool PresetMenuListWidget::setPreviewImageForItem(const QVariant& userData, const QImage& image)
{
    for (PresetMenuItem& item : m_items) {
        if (item.userData != userData) {
            continue;
        }
        item.previewImage = image;
        break;
    }

    for (PresetListRowWidget* row : m_rows) {
        if (row && row->userData() == userData) {
            row->setPreviewImage(image);
            return true;
        }
    }

    return false;
}

bool PresetMenuListWidget::setTitleForItem(const QVariant& userData, const QString& title)
{
    for (PresetMenuItem& item : m_items) {
        if (item.userData != userData) {
            continue;
        }
        item.title = title;
        break;
    }

    // Check dividers before rows: a pack key matches its divider here, so it
    // never falls through to the empty-pack placeholder row (which shares the
    // same userData but must keep its own "Empty pack" title). Dividers aren't
    // tracked in m_rows (they hold nullptr placeholders), so locate the widget
    // among the layout's direct children.
    if (m_listContent) {
        const QList<QWidget*> children
            = m_listContent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (auto* divider = dynamic_cast<PresetListDivider*>(child)) {
                if (divider->userData() == userData) {
                    divider->setTitleText(title);
                    return true;
                }
            }
        }
    }

    for (PresetListRowWidget* row : m_rows) {
        if (row && row->userData() == userData) {
            row->setText(title);
            return true;
        }
    }

    return false;
}

bool PresetMenuListWidget::setSubtitleForItem(const QVariant& userData, const QString& subtitle)
{
    for (PresetMenuItem& item : m_items) {
        if (item.userData != userData) {
            continue;
        }
        item.subtitle = subtitle;
        break;
    }

    for (PresetListRowWidget* row : m_rows) {
        if (row && row->userData() == userData) {
            row->setSubtitle(subtitle);
            return true;
        }
    }

    return false;
}

void PresetMenuListWidget::setSelectedUserData(const QVariant& data)
{
    if (m_selectedData == data) {
        return;
    }

    m_selectedData = data;
    updateSelectionVisuals();
}

void PresetMenuListWidget::setActiveUserData(const QVariant& data)
{
    if (m_activeData == data) {
        return;
    }

    m_activeData = data;
    for (PresetListRowWidget* row : m_rows) {
        if (row) {
            row->setActive(row->userData() == m_activeData);
        }
    }
}

void PresetMenuListWidget::setImportExportVisible(bool visible)
{
    m_importExportVisible = visible;
    rebuildHeaderActions();
}

void PresetMenuListWidget::setSearchEnabled(bool enabled)
{
    m_searchEnabled = enabled;
    updateSectionVisibility();
}

void PresetMenuListWidget::setSearchPlaceholderText(const QString& text)
{
    m_searchPlaceholderText = text;
    if (m_searchBar) {
        m_searchBar->setPlaceholder(text);
    }
}

QString PresetMenuListWidget::searchPlaceholderText() const
{
    return m_searchPlaceholderText;
}

void PresetMenuListWidget::setEmptyStateTexts(const QString& title, const QString& description)
{
    if (m_emptyTitleLabel) {
        m_emptyTitleLabel->setText(title);
    }
    if (m_emptyDescriptionLabel) {
        m_emptyDescriptionLabel->setText(description);
    }
}

QString PresetMenuListWidget::searchText() const
{
    return m_searchBar ? m_searchBar->text() : QString();
}

void PresetMenuListWidget::setScrollMaximumHeight(int height)
{
    m_scrollMaxHeight = height;
    const auto& theme = ThemeManager::instance();
    if (height > 0) {
        m_scrollArea->setMaximumHeight(theme.scaled(height));
    } else {
        m_scrollArea->setMaximumHeight(QWIDGETSIZE_MAX);
    }
}

void PresetMenuListWidget::applyContentMargins()
{
    const auto& theme = ThemeManager::instance();
    int m = theme.scaled(m_popupStyle ? kBasePopupMargin : kBaseEmbeddedMargin);
    const int popupListInset = m_popupStyle ? theme.scaled(8) : 0;
    const int popupHeaderInset = m_popupStyle ? qMax(0, m - popupListInset) : 0;

    if (layout()) {
        layout()->setContentsMargins(
            m_popupStyle ? popupListInset : m, m, m_popupStyle ? popupListInset : 0, m);
        layout()->setSpacing(theme.scaled(kBaseHeaderSpacing));
    }

    if (m_headerWidget && m_headerWidget->layout()) {
        m_headerWidget->layout()->setContentsMargins(popupHeaderInset, 0, popupHeaderInset, 0);
        m_headerWidget->layout()->setSpacing(theme.scaled(kBaseHeaderSpacing));
    }

    if (m_headerTopRow && m_headerTopRow->layout()) {
        m_headerTopRow->layout()->setSpacing(theme.scaled(8));
    }

    if (m_headerActionsLayout) {
        m_headerActionsLayout->setSpacing(theme.scaled(4));
    }

    m_scrollArea->setMinimumHeight(
        theme.scaled(m_popupStyle ? kBasePopupScrollMinHeight : kBaseScrollMinHeight));
    if (m_searchBar) {
        m_searchBar->setFixedHeight(theme.scaled(kBaseSearchHeight));
    }
    if (m_footerButton) {
        m_footerButton->setFixedHeight(theme.scaled(38));
    }
    if (m_footerHost && m_footerHost->layout()) {
        m_footerHost->layout()->setContentsMargins(popupHeaderInset, 0, popupHeaderInset, 0);
    }
}

void PresetMenuListWidget::rebuildHeaderActions()
{
    m_headerToolButtons.clear();
    clearLayout(m_headerActionsLayout);
    m_importBtn = nullptr;
    m_exportBtn = nullptr;

    const auto addButton
        = [this](const PresetMenuHeaderAction& action, int fallbackId, const QString& fallbackTip) {
              if (!action.visible) {
                  return;
              }

              const QString tip = action.toolTip.isEmpty() ? fallbackTip : action.toolTip;
              QString fbText = action.text.trimmed();
              if (fbText.isEmpty()) {
                  fbText = fallbackTip.trimmed();
              }
              if (fbText.isEmpty()) {
                  fbText = QStringLiteral("?");
              } else {
                  fbText = fbText.left(1).toUpper();
              }
              auto* button = new PresetHeaderToolButton(
                  m_headerActionsWidget, fallbackId, action.icon, fbText, action.accent);
              button->setToolTip(tip);
              button->setAccessibleName(tip);

              connect(button, &QAbstractButton::clicked, this, [this, fallbackId]() {
                  if (fallbackId == kBuiltinImportActionId) {
                      emit importClicked();
                      return;
                  }
                  if (fallbackId == kBuiltinExportActionId) {
                      emit exportClicked();
                      return;
                  }
                  emit headerActionTriggered(fallbackId);
              });

              m_headerActionsLayout->addWidget(button);
              m_headerToolButtons.append(button);
              if (fallbackId == kBuiltinImportActionId) {
                  m_importBtn = button;
              } else if (fallbackId == kBuiltinExportActionId) {
                  m_exportBtn = button;
              }
          };

    if (m_importExportVisible) {
        PresetMenuHeaderAction importAction;
        importAction.id = kBuiltinImportActionId;
        importAction.icon = IconProvider::StandardIcon::Import;
        importAction.text = QStringLiteral("I");
        addButton(importAction, importAction.id, tr("Import"));

        PresetMenuHeaderAction exportAction;
        exportAction.id = kBuiltinExportActionId;
        exportAction.icon = IconProvider::StandardIcon::Export;
        exportAction.text = QStringLiteral("E");
        addButton(exportAction, exportAction.id, tr("Export"));
    }

    for (const PresetMenuHeaderAction& action : m_headerActions) {
        addButton(action, action.id, action.text);
    }

    updateSectionVisibility();
    onThemeChanged();
}

void PresetMenuListWidget::rebuildFooterAction()
{
    if (!m_footerButton) {
        return;
    }

    const bool visible = m_footerAction.visible && m_footerAction.id != 0;
    m_footerButton->setVisible(visible);
    if (!visible) {
        return;
    }

    QString text = m_footerAction.text.trimmed();
    if (text.isEmpty()) {
        text = m_footerAction.toolTip.trimmed();
    }

    static_cast<PresetFooterActionButton*>(m_footerButton)->setButtonText(text);
    static_cast<PresetFooterActionButton*>(m_footerButton)->setButtonIcon(m_footerAction.icon);
    m_footerButton->setToolTip(m_footerAction.toolTip.isEmpty() ? text : m_footerAction.toolTip);
    m_footerButton->setAccessibleName(text);
    onThemeChanged();
}

void PresetMenuListWidget::updateSectionVisibility()
{
    const bool hasTitle = !m_titleLabel->text().trimmed().isEmpty();
    const bool hasActions = m_headerActionsLayout && m_headerActionsLayout->count() > 0;
    if (m_titleLabel) {
        m_titleLabel->setVisible(hasTitle);
    }
    if (m_headerTopRow) {
        m_headerTopRow->setVisible(hasTitle || hasActions);
    }
    if (m_searchBar) {
        m_searchBar->setVisible(m_searchEnabled);
    }
    if (m_headerWidget) {
        m_headerWidget->setVisible((hasTitle || hasActions) || m_searchEnabled);
    }
}

bool PresetMenuListWidget::itemMatchesFilter(const PresetMenuItem& item) const
{
    if (!m_searchEnabled || !m_searchBar) {
        return true;
    }

    const QString needle = m_searchBar->text().trimmed().toLower();
    if (needle.isEmpty()) {
        return true;
    }

    return itemSearchText(item).toLower().contains(needle);
}

void PresetMenuListWidget::rebuildRows()
{
    const int savedScroll = m_scrollArea ? m_scrollArea->scrollValue() : 0;

    while (m_listLayout->count() > 0) {
        QLayoutItem* li = m_listLayout->takeAt(0);
        if (li->widget()) {
            li->widget()->deleteLater();
        }
        delete li;
    }
    m_rows.clear();

    const auto& theme = ThemeManager::instance();
    int gap = theme.scaled(kBaseRowSpacing);
    int visibleCount = 0;
    std::optional<PresetMenuItem> pendingDivider;

    const auto addDivider = [this](const PresetMenuItem& item, const auto& handler) {
        auto* div = new PresetListDivider(item, m_listContent);
        div->setActionHandler(handler);
        div->setContextMenuEnabled(m_contextMenuEnabled);
        div->setRenamable(item.renamable);
        div->setRenameHandler([this, dividerUserData = item.userData](const QString& newTitle) {
            for (PresetMenuItem& storedItem : m_items) {
                if (storedItem.userData == dividerUserData) {
                    storedItem.title = newTitle;
                    break;
                }
            }
            emit itemRenamed(dividerUserData, newTitle);
        });
        m_listLayout->addWidget(div);
        m_rows.append(nullptr);
    };

    for (const PresetMenuItem& it : m_items) {
        if (it.isDivider) {
            pendingDivider = it;
            continue;
        }

        if (!itemMatchesFilter(it)) {
            continue;
        }

        if (pendingDivider.has_value()) {
            addDivider(
                *pendingDivider, [this, dividerUserData = pendingDivider->userData](int actionId) {
                    emit extraActionTriggered(dividerUserData, actionId);
                });
            m_listLayout->addSpacing(gap);
            pendingDivider.reset();
        }

        auto* row = new PresetListRowWidget(it, m_listContent);
        row->setPopupChromeStyle(m_popupStyle);
        row->setDeletable(it.deletable);
        row->setRenamable(it.renamable);
        row->setActive(it.userData == m_activeData);
        row->setContextMenuEnabled(m_contextMenuEnabled);

        connect(row, &PresetListRowWidget::clicked, this, [this, row]() {
            m_selectedData = row->userData();
            updateSelectionVisuals();
            emit itemClicked(m_selectedData);
        });
        connect(row, &PresetListRowWidget::renameFinished, this,
            [this, row](const QString& t) { emit itemRenamed(row->userData(), t); });
        connect(row, &PresetListRowWidget::deleteRequested, this,
            [this, row]() { emit deleteRequested(row->userData()); });
        connect(row, &PresetListRowWidget::extraActionTriggered, this,
            [this, row](int actionId) { emit extraActionTriggered(row->userData(), actionId); });

        m_listLayout->addWidget(row);
        m_listLayout->addSpacing(gap);
        m_rows.append(row);
        ++visibleCount;
    }

    m_listLayout->addStretch();
    if (m_emptyState) {
        m_emptyState->setVisible(visibleCount == 0);
    }
    if (m_scrollArea) {
        m_scrollArea->setVisible(visibleCount > 0);
    }

    updateSelectionVisuals();

    if (m_scrollArea) {
        m_scrollArea->refreshScrollGeometry();
        m_scrollArea->scrollTo(savedScroll, false);
        QTimer::singleShot(0, this, [this, savedScroll]() {
            if (!m_scrollArea) {
                return;
            }
            m_scrollArea->refreshScrollGeometry();
            m_scrollArea->scrollTo(savedScroll, false);
        });
    }
}

QWidget* PresetMenuListWidget::widgetForItem(const PresetMenuItem& item) const
{
    if (item.isDivider) {
        if (!m_listContent) {
            return nullptr;
        }
        const QList<QWidget*> children
            = m_listContent->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (auto* divider = dynamic_cast<PresetListDivider*>(child)) {
                if (divider->userData() == item.userData) {
                    return divider;
                }
            }
        }
        return nullptr;
    }

    for (PresetListRowWidget* row : m_rows) {
        if (row && row->userData() == item.userData) {
            return row;
        }
    }
    return nullptr;
}

bool PresetMenuListWidget::tryAnimatedRemoval(const QVector<PresetMenuItem>& newItems)
{
    if (!m_collapseAnimator || !m_listLayout || m_items.isEmpty()) {
        return false;
    }
    // A filtered list shows a subset; mapping items to laid-out widgets is then
    // ambiguous — let the caller do a clean rebuild instead.
    if (m_searchEnabled && m_searchBar && !m_searchBar->text().trimmed().isEmpty()) {
        return false;
    }

    // The same removal often drives two reloads (a model signal *and* an explicit
    // caller refresh). While a collapse is mid-flight, swallow a redundant reload
    // whose item keys already match — otherwise it would cut the animation short.
    const auto keysIdentical
        = [](const QVector<PresetMenuItem>& a, const QVector<PresetMenuItem>& b) {
              if (a.size() != b.size()) {
                  return false;
              }
              for (int i = 0; i < a.size(); ++i) {
                  if (a[i].isDivider != b[i].isDivider || a[i].userData != b[i].userData) {
                      return false;
                  }
              }
              return true;
          };

    // Settle any in-flight collapse so we diff against a stable widget set.
    if (m_collapseAnimator->isAnimating()) {
        if (keysIdentical(m_items, newItems)) {
            return true; // redundant reload mid-collapse — keep the animation playing
        }
        m_collapseAnimator->finishAll();
    }

    // newItems must be an ordered subsequence of m_items (same key + kind):
    // a pure removal. Additions or reordering bail to the full rebuild path.
    const auto sameKey = [](const PresetMenuItem& a, const PresetMenuItem& b) {
        return a.isDivider == b.isDivider && a.userData == b.userData;
    };

    QVector<int> removedIndices;
    int j = 0;
    for (int i = 0; i < m_items.size(); ++i) {
        if (j < newItems.size() && sameKey(m_items[i], newItems[j])) {
            ++j;
        } else {
            removedIndices.append(i);
        }
    }
    if (j != newItems.size() || removedIndices.isEmpty()) {
        return false;
    }

    // Resolve removed items to widgets + layout indices, grouping consecutive
    // removals (e.g. a pack divider plus its brush rows) into one collapsing run.
    struct Run {
        int layoutStart;
        int layoutEnd;
    };
    QVector<Run> runs;
    QSet<QWidget*> removedWidgets;
    QVector<QVariant> removedKeys;
    int prevItemIdx = -2;

    for (int idx : removedIndices) {
        QWidget* w = widgetForItem(m_items[idx]);
        if (!w) {
            return false; // can't snapshot this row safely
        }
        removedKeys.append(m_items[idx].userData);
        const int li = m_listLayout->indexOf(w);
        if (li < 0) {
            return false;
        }
        removedWidgets.insert(w);

        const bool contiguous = (idx == prevItemIdx + 1) && !runs.isEmpty();
        if (contiguous) {
            Run& run = runs.last();
            run.layoutStart = qMin(run.layoutStart, li);
            // +1 absorbs each widget's trailing spacer (rows/dividers are added
            // as widget followed by addSpacing()).
            run.layoutEnd = qMax(run.layoutEnd, li + 1);
        } else {
            runs.append({ li, li + 1 });
        }
        prevItemIdx = idx;
    }
    if (runs.isEmpty()) {
        return false;
    }

    // Commit the new model now and detach the doomed rows from m_rows so any
    // selection/preview update during the animation skips them.
    m_items = newItems;
    for (PresetListRowWidget*& row : m_rows) {
        if (row && removedWidgets.contains(row)) {
            row = nullptr;
        }
    }

    // Drop selection/active state if it pointed at something we're removing, then
    // refresh visuals on the survivors so the highlight is correct *during* the
    // collapse (not only after the trailing rebuild). The caller may immediately
    // set a new selection on top of this.
    for (const QVariant& key : removedKeys) {
        if (m_selectedData == key) {
            m_selectedData = QVariant();
        }
        if (m_activeData == key) {
            m_activeData = QVariant();
        }
    }
    updateSelectionVisuals();

    // Collapse bottom-up so each run's precomputed indices stay valid as lower
    // runs are torn out. A shared counter rebuilds once every run has finished.
    std::sort(runs.begin(), runs.end(),
        [](const Run& a, const Run& b) { return a.layoutStart > b.layoutStart; });

    auto pending = std::make_shared<int>(runs.size());
    auto onRunFinished = [this, pending]() {
        if (--(*pending) == 0) {
            rebuildRows();
        }
    };

    for (const Run& run : runs) {
        m_collapseAnimator->collapseRange(
            m_listLayout, m_listContent, run.layoutStart, run.layoutEnd, 0, onRunFinished);
    }
    return true;
}

void PresetMenuListWidget::updateSelectionVisuals()
{
    for (PresetListRowWidget* row : m_rows) {
        if (!row) {
            continue;
        }
        row->setSelected(row->userData() == m_selectedData);
        row->setActive(row->userData() == m_activeData);
    }
}

void PresetMenuListWidget::paintEvent(QPaintEvent* event)
{
    if (!m_popupStyle || !m_popupPanelPainted) {
        QWidget::paintEvent(event);
        return;
    }

    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ThemeManager::instance().colors();

    QRectF rect = this->rect().adjusted(1.25, 1.25, -1.25, -1.25);
    const qreal radius = ThemeManager::instance().scaled(kPanelRadius);
    QColor borderTop = colors.border;
    borderTop.setAlpha(colors.isDark ? 132 : 118);
    QColor borderBottom = colors.borderDark();
    borderBottom.setAlpha(colors.isDark ? 92 : 78);
    ruwa::ui::painting::drawTonedGlassPanel(painter, rect, radius, QSizeF(size()), m_glassBackdrop,
        colors.surfaceElevated(), colors.primary, colors.isDark, borderTop, borderBottom, 1.5,
        false);
}

void PresetMenuListWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        setSearchPlaceholderText(tr("Search presets"));
        setEmptyStateTexts(
            tr("No presets found"), tr("Try a different search or create a new preset."));
        rebuildHeaderActions();
    }
}

void PresetMenuListWidget::onThemeChanged()
{
    applyContentMargins();
    const auto& colors = ThemeManager::instance().colors();

    if (m_titleLabel) {
        QFont titleFont = colors.fonts.getTitleFont(ThemeManager::instance().scaledFontSize(15));
        titleFont.setWeight(QFont::DemiBold);
        m_titleLabel->setFont(titleFont);
        m_titleLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }

    const int headerBtnSize = ThemeManager::instance().scaled(kBaseHeaderButtonSize);
    for (BaseAnimatedButton* button : m_headerToolButtons) {
        if (button) {
            button->setFixedSize(headerBtnSize, headerBtnSize);
            button->update();
        }
    }

    if (m_emptyIconLabel) {
        const int emptyIconSize = ThemeManager::instance().scaled(22);
        m_emptyIconLabel->setFixedSize(
            ThemeManager::instance().scaled(48), ThemeManager::instance().scaled(48));
        m_emptyIconLabel->setStyleSheet(
            QStringLiteral("QLabel { background: %1; border: 1px solid %2; border-radius: %3px; }")
                .arg(colors.surface.name(QColor::HexArgb))
                .arg(colors.borderSubtle().name(QColor::HexArgb))
                .arg(qMax(8, ThemeManager::instance().scaled(12))));
        m_emptyIconLabel->setPixmap(IconProvider::instance()
                .getColoredIcon(IconProvider::StandardIcon::Find, colors.textMuted)
                .pixmap(emptyIconSize, emptyIconSize));
    }

    if (m_emptyTitleLabel) {
        QFont f = font();
        f.setPixelSize(ThemeManager::instance().scaled(14));
        f.setWeight(QFont::Medium);
        m_emptyTitleLabel->setFont(f);
        m_emptyTitleLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.text.name(QColor::HexArgb)));
    }

    if (m_emptyDescriptionLabel) {
        QFont f = font();
        f.setPixelSize(ThemeManager::instance().scaled(11));
        m_emptyDescriptionLabel->setFont(f);
        m_emptyDescriptionLabel->setStyleSheet(
            QStringLiteral("QLabel { color: %1; background: transparent; }")
                .arg(colors.textMuted.name(QColor::HexArgb)));
        m_emptyDescriptionLabel->setMaximumWidth(ThemeManager::instance().scaled(220));
    }

    if (m_scrollMaxHeight > 0) {
        setScrollMaximumHeight(m_scrollMaxHeight);
    }

    rebuildRows();
    update();
}

} // namespace ruwa::ui::widgets
