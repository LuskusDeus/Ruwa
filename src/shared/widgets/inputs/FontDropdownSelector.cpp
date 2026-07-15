// SPDX-License-Identifier: MPL-2.0

#include "FontDropdownSelector.h"

#include "features/theme/manager/ThemeManager.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/inputs/SearchBar.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QAbstractItemView>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QHideEvent>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QStyleOption>
#include <QStyledItemDelegate>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace ruwa::ui::widgets {

namespace {

constexpr int kTriggerHeight = 28;
constexpr int kRowHeight = 34;
constexpr int kPopupPadding = 6;
constexpr int kSearchBottomSpacing = 6;
constexpr int kCornerRadius = 8;
constexpr int kPopupOffset = 20;
constexpr int kShowDurationMs = 120;
constexpr int kHideDurationMs = 80;
constexpr int kSlideDurationMs = 200;
constexpr int kPopupMinVisibleHeight = 160;

enum FontRoles { FamilyRole = Qt::UserRole + 1 };

class FontListModel final : public QAbstractListModel {
public:
    explicit FontListModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_families.size();
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_families.size()) {
            return QVariant();
        }

        const QString& family = m_families[index.row()];
        if (role == Qt::DisplayRole || role == FamilyRole) {
            return family;
        }
        return QVariant();
    }

    void setFamilies(QStringList families)
    {
        families.removeAll(QString());
        families.removeDuplicates();
        std::sort(families.begin(), families.end(), [](const QString& a, const QString& b) {
            return QString::localeAwareCompare(a, b) < 0;
        });

        beginResetModel();
        m_families = std::move(families);
        endResetModel();
    }

    QStringList families() const { return m_families; }

private:
    QStringList m_families;
};

class FontFilterProxyModel final : public QSortFilterProxyModel {
public:
    explicit FontFilterProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
        setDynamicSortFilter(true);
    }

    void setFilterText(const QString& text)
    {
        if (m_filterText == text) {
            return;
        }
        m_filterText = text;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        if (m_filterText.isEmpty()) {
            return true;
        }

        const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        const QString family = index.data(FamilyRole).toString();
        return family.contains(m_filterText, Qt::CaseInsensitive);
    }

private:
    QString m_filterText;
};

class FontItemDelegate final : public QStyledItemDelegate {
public:
    explicit FontItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(220, kRowHeight);
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
        const QModelIndex& index) const override
    {
        if (!painter || !index.isValid()) {
            return;
        }

        const QString family = index.data(FamilyRole).toString();
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF rowRect = option.rect.adjusted(2.0, 1.0, -2.0, -1.0);
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const bool hovered = option.state.testFlag(QStyle::State_MouseOver);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);

        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.surfaceElevated());
        painter->drawRoundedRect(rowRect, 5, 5);

        if (hovered) {
            painter->setBrush(colors.overlayHover());
            painter->drawRoundedRect(rowRect, 5, 5);
        }
        if (selected) {
            painter->setBrush(colors.overlay(0.12));
            painter->drawRoundedRect(rowRect, 5, 5);
        }

        const QRect previewRect(
            option.rect.left() + 10, option.rect.top() + 5, 54, option.rect.height() - 10);
        painter->setBrush(colors.overlayBase());
        painter->drawRoundedRect(QRectF(previewRect), 5, 5);

        QFont previewFont(family);
        previewFont.setPointSize(12);
        painter->setFont(previewFont);
        painter->setPen(colors.text);
        painter->drawText(previewRect, Qt::AlignCenter, QStringLiteral("Aa"));

        QFont nameFont(family);
        nameFont.setPointSize(9);
        painter->setFont(nameFont);
        painter->setPen(colors.text);
        const QRect nameRect(option.rect.left() + 74, option.rect.top(), option.rect.width() - 104,
            option.rect.height());
        painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, family);

        if (selected) {
            QPen checkPen(colors.primary, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter->setPen(checkPen);
            const qreal cx = option.rect.right() - 13;
            const qreal cy = option.rect.center().y();
            painter->drawLine(QPointF(cx - 3.3, cy), QPointF(cx - 1.1, cy + 2.7));
            painter->drawLine(QPointF(cx - 1.1, cy + 2.7), QPointF(cx + 3.8, cy - 2.2));
        }

        painter->restore();
    }
};

} // namespace

class FontDropdownPopup final : public QWidget {
public:
    explicit FontDropdownPopup(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setWindowFlags(Qt::Widget);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        hide();

        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(kPopupPadding, kPopupPadding, kPopupPadding, kPopupPadding);
        m_layout->setSpacing(kSearchBottomSpacing);

        m_searchBar = new SearchBar(this);
        m_searchBar->setPlaceholder(tr("Search font..."));
        m_searchBar->setMinimumWidth(1);
        m_searchBar->setFixedHeight(34);
        m_layout->addWidget(m_searchBar);

        m_model = new FontListModel(this);
        m_proxyModel = new FontFilterProxyModel(this);
        m_proxyModel->setSourceModel(m_model);

        m_listView = new QListView(this);
        m_listView->setModel(m_proxyModel);
        m_listView->setItemDelegate(new FontItemDelegate(m_listView));
        m_listView->setFrameShape(QFrame::NoFrame);
        m_listView->setMouseTracking(true);
        m_listView->setUniformItemSizes(true);
        m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
        m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_listView->setAttribute(Qt::WA_TranslucentBackground);
        m_listView->viewport()->setAttribute(Qt::WA_TranslucentBackground);
        m_layout->addWidget(m_listView);

        m_opacityEffect = new QGraphicsOpacityEffect(this);
        m_opacityEffect->setOpacity(0.0);
        setGraphicsEffect(m_opacityEffect);

        m_opacityAnim = new QVariantAnimation(this);
        m_opacityAnim->setDuration(kShowDurationMs);
        m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(
            m_opacityAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
                m_popupOpacity = qBound(0.0, value.toReal(), 1.0);
                m_opacityEffect->setOpacity(m_popupOpacity);
            });

        m_posAnim = new QPropertyAnimation(this, "pos", this);
        m_posAnim->setDuration(kSlideDurationMs);
        m_posAnim->setEasingCurve(QEasingCurve::OutCubic);

        connect(m_searchBar, &SearchBar::textChanged, this, [this](const QString& text) {
            m_proxyModel->setFilterText(text);
            selectFamily(m_selectedFamily, false);
            updateHeightForCurrentFilter();
        });
        connect(
            m_searchBar, &SearchBar::searchRequested, this, [this]() { activateCurrentOrFirst(); });
        connect(m_listView, &QListView::clicked, this,
            [this](const QModelIndex& index) { activateIndex(index); });
        connect(m_listView, &QListView::activated, this,
            [this](const QModelIndex& index) { activateIndex(index); });

        updateListStyle();
        connect(&ruwa::ui::core::ThemeManager::instance(),
            &ruwa::ui::core::ThemeManager::themeChanged, this, [this]() {
                updateListStyle();
                update();
                if (m_listView) {
                    m_listView->viewport()->update();
                }
            });
    }

    void setFamilies(const QStringList& families)
    {
        m_model->setFamilies(families);
        selectFamily(m_selectedFamily, false);
        updateHeightForCurrentFilter();
    }

    QStringList families() const { return m_model ? m_model->families() : QStringList(); }

    void setSelectedFamily(const QString& family)
    {
        m_selectedFamily = family;
        selectFamily(m_selectedFamily, true);
    }

    void setOnFamilyActivated(std::function<void(QString)> cb)
    {
        m_onFamilyActivated = std::move(cb);
    }

    void setOnPopupHidden(std::function<void()> cb) { m_onPopupHidden = std::move(cb); }

    int preferredHeight() const
    {
        if (!m_proxyModel) {
            return kPopupMinVisibleHeight;
        }
        const int visibleRows = qBound(1, m_proxyModel->rowCount(), 8);
        return kPopupPadding * 2 + 34 + kSearchBottomSpacing + visibleRows * kRowHeight;
    }

    void applySizeConstraints(int maxWidth, int maxHeight)
    {
        m_appliedMaxHeight = maxHeight;
        const int widthValue = qMax(kPopupPadding * 2 + 1, maxWidth);
        const int heightValue = qMax(kPopupMinVisibleHeight, qMin(preferredHeight(), maxHeight));
        const int listHeight = qMax(1, heightValue - kPopupPadding * 2 - 34 - kSearchBottomSpacing);

        m_listView->setFixedHeight(listHeight);
        setFixedSize(widthValue, heightValue);
        selectFamily(m_selectedFamily, true);
    }

    void showAnimated(const QPoint& targetPos)
    {
        m_isVisible = true;
        m_isHiding = false;
        disconnect(m_opacityAnim, &QVariantAnimation::finished, this, nullptr);

        const QPoint startPos(targetPos.x(), targetPos.y() - kPopupOffset);
        m_posAnim->stop();
        move(startPos);
        show();
        raise();

        if (m_searchBar) {
            m_searchBar->clear();
            m_searchBar->setFocus(Qt::PopupFocusReason);
        }

        m_opacityAnim->stop();
        m_opacityAnim->setDuration(kShowDurationMs);
        m_opacityAnim->setStartValue(m_popupOpacity);
        m_opacityAnim->setEndValue(1.0);
        m_opacityAnim->start();

        m_posAnim->setDuration(kSlideDurationMs);
        m_posAnim->setStartValue(startPos);
        m_posAnim->setEndValue(targetPos);
        m_posAnim->start();
    }

    void hideAnimated()
    {
        if (!m_isVisible || m_isHiding) {
            return;
        }
        m_isVisible = false;
        m_isHiding = true;

        m_posAnim->stop();
        m_opacityAnim->stop();
        disconnect(m_opacityAnim, &QVariantAnimation::finished, this, nullptr);
        connect(m_opacityAnim, &QVariantAnimation::finished, this, [this]() {
            m_isHiding = false;
            hide();
            if (m_onPopupHidden) {
                m_onPopupHidden();
            }
        });

        m_opacityAnim->setDuration(kHideDurationMs);
        m_opacityAnim->setStartValue(m_popupOpacity);
        m_opacityAnim->setEndValue(0.0);
        m_opacityAnim->start();

        m_posAnim->setDuration(kHideDurationMs);
        m_posAnim->setStartValue(pos());
        m_posAnim->setEndValue(QPoint(pos().x(), pos().y() - (kPopupOffset / 2)));
        m_posAnim->start();
    }

    void forceHide()
    {
        m_isVisible = false;
        m_isHiding = false;
        m_posAnim->stop();
        m_opacityAnim->stop();
        m_popupOpacity = 0.0;
        m_opacityEffect->setOpacity(0.0);
        hide();
    }

    bool containsGlobal(const QPoint& globalPos) const
    {
        return isVisible() && QRect(mapToGlobal(QPoint(0, 0)), size()).contains(globalPos);
    }

    void moveSelection(int delta)
    {
        if (!m_listView || !m_proxyModel || m_proxyModel->rowCount() <= 0 || delta == 0) {
            return;
        }

        QModelIndex current = m_listView->currentIndex();
        int row = current.isValid() ? current.row() : 0;
        row = qBound(0, row + delta, m_proxyModel->rowCount() - 1);
        current = m_proxyModel->index(row, 0);
        m_listView->setCurrentIndex(current);
        if (m_listView->selectionModel()) {
            m_listView->selectionModel()->select(
                current, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
        m_listView->scrollTo(current, QAbstractItemView::PositionAtCenter);
    }

    void activateCurrentOrFirst()
    {
        if (!m_proxyModel || m_proxyModel->rowCount() <= 0) {
            return;
        }

        QModelIndex index = m_listView->currentIndex();
        if (!index.isValid()) {
            index = m_proxyModel->index(0, 0);
        }
        activateIndex(index);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        const QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surfaceElevated());
        painter.drawRoundedRect(rect, kCornerRadius, kCornerRadius);

        QColor topBorder = colors.borderSubtle();
        QColor bottomBorder = topBorder;
        bottomBorder.setAlpha(bottomBorder.alpha() / 2);
        ruwa::ui::painting::drawGradientBorder(
            painter, rect, kCornerRadius, topBorder, bottomBorder);
    }

private:
    void updateListStyle()
    {
        const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
        if (m_listView) {
            m_listView->setStyleSheet(QString(R"(
                QListView {
                    background: transparent;
                    border: none;
                    outline: none;
                    color: %1;
                }
                QListView::item {
                    background: transparent;
                    border: none;
                }
                QScrollBar:vertical {
                    background: transparent;
                    width: 6px;
                    margin: 2px 0 2px 0;
                }
                QScrollBar::handle:vertical {
                    background: %2;
                    border-radius: 3px;
                    min-height: 24px;
                }
                QScrollBar::add-line:vertical,
                QScrollBar::sub-line:vertical {
                    height: 0;
                }
                QScrollBar::add-page:vertical,
                QScrollBar::sub-page:vertical {
                    background: transparent;
                }
            )")
                    .arg(colors.text.name(), colors.borderSubtleHover().name()));
        }
    }

    void selectFamily(const QString& family, bool scrollToSelection)
    {
        if (!m_listView || !m_proxyModel) {
            return;
        }

        QModelIndex selected;
        for (int row = 0; row < m_proxyModel->rowCount(); ++row) {
            const QModelIndex index = m_proxyModel->index(row, 0);
            if (index.data(FamilyRole).toString() == family) {
                selected = index;
                break;
            }
        }

        if (!selected.isValid() && m_proxyModel->rowCount() > 0) {
            selected = m_proxyModel->index(0, 0);
        }

        if (selected.isValid()) {
            m_listView->setCurrentIndex(selected);
            if (m_listView->selectionModel()) {
                m_listView->selectionModel()->select(
                    selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            }
            if (scrollToSelection) {
                m_listView->scrollTo(selected, QAbstractItemView::PositionAtCenter);
            }
        } else {
            m_listView->clearSelection();
        }
    }

    void activateIndex(const QModelIndex& index)
    {
        if (!index.isValid()) {
            return;
        }

        const QString family = index.data(FamilyRole).toString();
        if (!family.isEmpty() && m_onFamilyActivated) {
            m_onFamilyActivated(family);
        }
    }

    void updateHeightForCurrentFilter()
    {
        if (isVisible()) {
            applySizeConstraints(width(), m_appliedMaxHeight);
        }
    }

private:
    QVBoxLayout* m_layout = nullptr;
    SearchBar* m_searchBar = nullptr;
    QListView* m_listView = nullptr;
    FontListModel* m_model = nullptr;
    FontFilterProxyModel* m_proxyModel = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QVariantAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    QString m_selectedFamily;
    int m_appliedMaxHeight = 100000;
    bool m_isVisible = false;
    bool m_isHiding = false;
    qreal m_popupOpacity = 0.0;
    std::function<void(QString)> m_onFamilyActivated;
    std::function<void()> m_onPopupHidden;
};

FontDropdownSelector::FontDropdownSelector(QWidget* parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setFixedHeight(kTriggerHeight);
    setMinimumWidth(150);
    setCursor(Qt::PointingHandCursor);

    m_popup = new FontDropdownPopup(window());
    m_popup->setProperty("ruwa_owner_combo", QVariant::fromValue(static_cast<QWidget*>(this)));
    m_popup->setOnFamilyActivated([this](QString family) {
        const bool changed = (m_currentFamily != family);
        setCurrentFamily(family);
        closePopup();
        emit activated(family);
        if (changed) {
            emit currentFamilyChanged(family);
        }
    });
    m_popup->setOnPopupHidden([this]() { emit popupHidden(); });

    m_hoverAnim = new QPropertyAnimation(this, "hoverProgress", this);
    m_hoverAnim->setDuration(170);
    m_hoverAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_pressAnim = new QPropertyAnimation(this, "pressProgress", this);
    m_pressAnim->setDuration(90);
    m_pressAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_arrowAnim = new QPropertyAnimation(this, "arrowProgress", this);
    m_arrowAnim->setDuration(160);
    m_arrowAnim->setEasingCurve(QEasingCurve::OutCubic);
}

FontDropdownSelector::~FontDropdownSelector()
{
    disconnectScrollAreaPositionUpdates();
    qApp->removeEventFilter(this);
    if (m_popup) {
        m_popup->forceHide();
        m_popup->deleteLater();
        m_popup = nullptr;
    }
}

void FontDropdownSelector::setCurrentFamily(const QString& family)
{
    if (m_currentFamily == family) {
        return;
    }
    m_currentFamily = family;
    if (m_popup) {
        m_popup->setSelectedFamily(m_currentFamily);
    }
    update();
}

void FontDropdownSelector::setFontFamilies(const QStringList& families)
{
    m_families = families;
    m_fontsLoaded = true;
    if (m_popup) {
        m_popup->setFamilies(m_families);
        m_popup->setSelectedFamily(m_currentFamily);
    }
    update();
}

QStringList FontDropdownSelector::fontFamilies() const
{
    return m_families;
}

void FontDropdownSelector::setPlaceholderText(const QString& text)
{
    m_placeholderText = text;
    update();
}

void FontDropdownSelector::setPopupMinWidth(int width)
{
    m_popupMinWidth = qMax(180, width);
}

void FontDropdownSelector::setPopupMaxHeight(int height)
{
    m_popupMaxHeight = qMax(kPopupMinVisibleHeight, height);
}

void FontDropdownSelector::setOpacityProvider(QWidget* provider)
{
    m_opacityProvider = provider;
    update();
}

void FontDropdownSelector::setHoverProgress(qreal progress)
{
    const qreal value = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_hoverProgress, value)) {
        return;
    }
    m_hoverProgress = value;
    update();
}

void FontDropdownSelector::setPressProgress(qreal progress)
{
    const qreal value = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_pressProgress, value)) {
        return;
    }
    m_pressProgress = value;
    update();
}

void FontDropdownSelector::setArrowProgress(qreal progress)
{
    const qreal value = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_arrowProgress, value)) {
        return;
    }
    m_arrowProgress = value;
    update();
}

void FontDropdownSelector::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(inheritedOpacity());

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    const QRectF box = rect().adjusted(0.5, 0.5, -0.5, -0.5);

    p.setPen(Qt::NoPen);
    p.setBrush(colors.overlayBase());
    p.drawRoundedRect(box, 6, 6);

    if (m_hoverProgress > 0.001) {
        QColor hover = colors.overlayHover();
        hover.setAlphaF(hover.alphaF() * m_hoverProgress * 0.2);
        p.setBrush(hover);
        p.drawRoundedRect(box, 6, 6);
    }
    if (m_pressProgress > 0.001) {
        QColor press = colors.overlay(0.12);
        press.setAlphaF(press.alphaF() * m_pressProgress);
        p.setBrush(press);
        p.drawRoundedRect(box, 6, 6);
    }

    QColor top = ruwa::ui::core::ThemeColors::interpolate(
        colors.borderSubtle(), colors.borderSubtleHover(), m_hoverProgress);
    QColor bottom = colors.borderSubtle();
    bottom.setAlpha(bottom.alpha() / 2);
    ruwa::ui::painting::drawGradientBorder(p, box, 6, top, bottom);

    const bool hasValue = !m_currentFamily.isEmpty();
    const QString text = hasValue ? m_currentFamily : m_placeholderText;
    QColor textBase = hasValue ? colors.textMuted : colors.textDisabled();
    QColor textTarget = hasValue ? colors.text : colors.textMuted;
    QColor textColor
        = ruwa::ui::core::ThemeColors::interpolate(textBase, textTarget, m_hoverProgress);

    QFont textFont = hasValue ? QFont(m_currentFamily) : font();
    textFont.setPointSize(9);
    p.setFont(textFont);
    p.setPen(textColor);
    p.drawText(QRect(10, 0, width() - 32, height()),
        Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, text);

    p.save();
    const qreal arrowX = width() - 13;
    const qreal arrowY = height() * 0.5;
    p.translate(arrowX, arrowY);
    p.rotate(180.0 * m_arrowProgress);
    QColor arrowColor = ruwa::ui::core::ThemeColors::interpolate(
        colors.textDisabled(), colors.text, qMin(1.0, m_hoverProgress + (m_pressProgress * 0.35)));
    p.setPen(QPen(arrowColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(QPointF(-3.5, -1.0), QPointF(0.0, 2.2));
    p.drawLine(QPointF(0.0, 2.2), QPointF(3.5, -1.0));
    p.restore();
}

void FontDropdownSelector::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    animateHoverTo(1.0);
}

void FontDropdownSelector::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (!isPopupActive()) {
        animateHoverTo(0.0);
    }
}

void FontDropdownSelector::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressAnim->stop();
        m_pressAnim->setStartValue(m_pressProgress);
        m_pressAnim->setEndValue(1.0);
        m_pressAnim->start();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void FontDropdownSelector::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressAnim->stop();
        m_pressAnim->setStartValue(m_pressProgress);
        m_pressAnim->setEndValue(0.0);
        m_pressAnim->start();
        if (rect().contains(event->pos())) {
            togglePopup();
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void FontDropdownSelector::keyPressEvent(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (isPopupActive()) {
            m_popup->activateCurrentOrFirst();
        } else {
            openPopup();
        }
        event->accept();
        return;
    case Qt::Key_Escape:
        if (isPopupActive()) {
            closePopup();
            event->accept();
            return;
        }
        break;
    case Qt::Key_Down:
        if (!isPopupActive()) {
            openPopup();
        } else {
            m_popup->moveSelection(1);
        }
        event->accept();
        return;
    case Qt::Key_Up:
        if (isPopupActive()) {
            m_popup->moveSelection(-1);
            event->accept();
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void FontDropdownSelector::focusInEvent(QFocusEvent* event)
{
    QWidget::focusInEvent(event);
    update();
}

void FontDropdownSelector::focusOutEvent(QFocusEvent* event)
{
    QWidget::focusOutEvent(event);
    update();
}

void FontDropdownSelector::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (!m_popup || !m_popup->isVisible()) {
        return;
    }
    if (window()) {
        window()->removeEventFilter(this);
    }
    qApp->removeEventFilter(this);
    m_popup->forceHide();
    setArrowProgress(0.0);
    animateHoverTo(0.0);
}

bool FontDropdownSelector::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (!isPopupActive()) {
        return QWidget::eventFilter(watched, event);
    }

    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        const bool insideSelf = QRect(mapToGlobal(QPoint(0, 0)), size()).contains(globalPos);
        const bool insidePopup = m_popup && m_popup->containsGlobal(globalPos);
        if (!insideSelf && !insidePopup) {
            closePopup();
        }
    } else if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closePopup();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down) {
            m_popup->moveSelection(1);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Up) {
            m_popup->moveSelection(-1);
            return true;
        }
    } else if ((event->type() == QEvent::Resize || event->type() == QEvent::Move)
        && watched == window()) {
        updatePopupPosition();
    }

    return QWidget::eventFilter(watched, event);
}

void FontDropdownSelector::ensureFontsLoaded()
{
    if (m_fontsLoaded) {
        return;
    }

    setFontFamilies(QFontDatabase::families());
    if (m_currentFamily.isEmpty() && !m_families.isEmpty()) {
        const QString appFamily = QApplication::font().family();
        m_currentFamily = m_families.contains(appFamily) ? appFamily : m_families.first();
    }
    if (m_popup) {
        m_popup->setSelectedFamily(m_currentFamily);
    }
}

void FontDropdownSelector::openPopup()
{
    if (isPopupActive() || !isEnabled()) {
        return;
    }

    ensureFontsLoaded();
    if (m_families.isEmpty()) {
        return;
    }

    if (!m_popup->parentWidget() || m_popup->parentWidget() != window()) {
        m_popup->setParent(window());
    }

    syncPopupState();
    updatePopupPosition();
    m_popup->showAnimated(m_popup->pos());
    animateArrowTo(1.0);
    animateHoverTo(1.0);
    connectScrollAreaPositionUpdates();

    qApp->installEventFilter(this);
    if (window()) {
        window()->installEventFilter(this);
    }

    emit popupShown();
}

void FontDropdownSelector::closePopup()
{
    disconnectScrollAreaPositionUpdates();

    if (!isPopupActive()) {
        return;
    }

    if (window()) {
        window()->removeEventFilter(this);
    }
    qApp->removeEventFilter(this);

    m_popup->hideAnimated();
    animateArrowTo(0.0);
    if (!underMouse()) {
        animateHoverTo(0.0);
    }
}

void FontDropdownSelector::togglePopup()
{
    if (isPopupActive()) {
        closePopup();
    } else {
        openPopup();
    }
}

void FontDropdownSelector::syncPopupState()
{
    if (!m_popup) {
        return;
    }
    m_popup->setFamilies(m_families);
    m_popup->setSelectedFamily(m_currentFamily);
}

void FontDropdownSelector::updatePopupPosition()
{
    if (!m_popup || !window()) {
        return;
    }

    const QPoint belowAnchorGlobal = mapToGlobal(QPoint(0, height() + 2));
    const QPoint aboveAnchorGlobal = mapToGlobal(QPoint(0, -2));
    QScreen* screen = QApplication::screenAt(belowAnchorGlobal);
    if (!screen) {
        screen = QApplication::screenAt(aboveAnchorGlobal);
    }

    if (screen) {
        const QRect bounds = screen->availableGeometry();
        const int popupPreferredHeight = m_popup->preferredHeight();
        const int availableBelow = qMax(0, bounds.bottom() - belowAnchorGlobal.y() + 1);
        const int availableAbove = qMax(0, aboveAnchorGlobal.y() - bounds.top());
        const bool placeBelow
            = (availableBelow >= popupPreferredHeight) || (availableBelow >= availableAbove);
        const int availableHeight = placeBelow ? availableBelow : availableAbove;
        const int constrainedHeight = qMax(kPopupMinVisibleHeight,
            qMin(popupPreferredHeight, qMin(availableHeight, m_popupMaxHeight)));
        const int constrainedWidth = qMax(m_popupMinWidth, width());

        m_popup->applySizeConstraints(constrainedWidth, constrainedHeight);

        QPoint target = window()->mapFromGlobal(placeBelow
                ? belowAnchorGlobal
                : QPoint(aboveAnchorGlobal.x(), aboveAnchorGlobal.y() - m_popup->height()));

        const QPoint targetGlobal = window()->mapToGlobal(target);
        if (targetGlobal.x() + m_popup->width() > bounds.right() + 1) {
            target.rx() -= (targetGlobal.x() + m_popup->width() - (bounds.right() + 1));
        }
        if (targetGlobal.x() < bounds.left()) {
            target.rx() += (bounds.left() - targetGlobal.x());
        }

        m_popup->move(target);
        return;
    }

    m_popup->applySizeConstraints(qMax(m_popupMinWidth, width()), m_popupMaxHeight);
    m_popup->move(window()->mapFromGlobal(belowAnchorGlobal));
}

void FontDropdownSelector::connectScrollAreaPositionUpdates()
{
    disconnectScrollAreaPositionUpdates();

    for (QWidget* ancestor = parentWidget(); ancestor; ancestor = ancestor->parentWidget()) {
        auto* scrollArea = qobject_cast<SmoothScrollArea*>(ancestor);
        if (!scrollArea) {
            continue;
        }

        m_scrollAreaPositionConnections.append(
            connect(scrollArea, &SmoothScrollArea::scrolled, this, [this, scrollArea](int) {
                QWidget* viewport = scrollArea->viewport();
                if (!viewport) {
                    closePopup();
                    return;
                }

                const QRect anchorRect(viewport->mapFromGlobal(mapToGlobal(QPoint(0, 0))), size());
                if (!viewport->rect().intersects(anchorRect)) {
                    closePopup();
                    return;
                }

                updatePopupPosition();
            }));
    }
}

void FontDropdownSelector::disconnectScrollAreaPositionUpdates()
{
    for (const QMetaObject::Connection& connection :
        std::as_const(m_scrollAreaPositionConnections)) {
        disconnect(connection);
    }
    m_scrollAreaPositionConnections.clear();
}

void FontDropdownSelector::animateHoverTo(qreal target)
{
    m_hoverAnim->stop();
    m_hoverAnim->setStartValue(m_hoverProgress);
    m_hoverAnim->setEndValue(target);
    m_hoverAnim->start();
}

void FontDropdownSelector::animateArrowTo(qreal target)
{
    m_arrowAnim->stop();
    m_arrowAnim->setStartValue(m_arrowProgress);
    m_arrowAnim->setEndValue(target);
    m_arrowAnim->start();
}

bool FontDropdownSelector::isPopupActive() const
{
    return m_popup && m_popup->isVisible();
}

qreal FontDropdownSelector::inheritedOpacity() const
{
    if (!m_opacityProvider) {
        return 1.0;
    }
    return qBound(0.0, m_opacityProvider->property("popupOpacity").toReal(), 1.0);
}

} // namespace ruwa::ui::widgets
