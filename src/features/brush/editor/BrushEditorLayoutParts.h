// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_BRUSHEDITOR_LAYOUT_BRUSHEDITORLAYOUTPARTS_H
#define RUWA_UI_WINDOWS_BRUSHEDITOR_LAYOUT_BRUSHEDITORLAYOUTPARTS_H

#include "features/brush/manager/BrushPreviewManager.h"
#include "features/brush/manager/BrushSettings.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QWidget>

#include <functional>

namespace ruwa::ui::windows::layout_internal {

using namespace ruwa::ui::core;

namespace {

constexpr int kBrushEditorPreviewFrameIntervalMs = 33;

}

class AsyncBrushPreviewCanvasBase : public QWidget {
public:
    enum class PreviewKind { Stroke, Dot };

    explicit AsyncBrushPreviewCanvasBase(PreviewKind previewKind, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_previewKind(previewKind)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);

        auto& previewManager = ruwa::core::brushes::BrushPreviewManager::instance();
        const auto sessionKind = m_previewKind == PreviewKind::Dot
            ? ruwa::core::brushes::BrushPreviewSession::Kind::Dot
            : ruwa::core::brushes::BrushPreviewSession::Kind::Stroke;
        m_previewSession = previewManager.createSession(sessionKind, this);
        m_previewSession->setDispatchIntervalMs(kBrushEditorPreviewFrameIntervalMs);
        connect(m_previewSession, &ruwa::core::brushes::BrushPreviewSession::imageChanged, this,
            QOverload<>::of(&AsyncBrushPreviewCanvasBase::update));
    }

    void setBrushData(const ruwa::core::brushes::BrushSettingsData& settings, qreal sizeNorm = 0.5,
        qreal opacityNorm = 1.0, const QColor& color = QColor(0, 0, 0))
    {
        m_hasBrush = true;
        m_settings = settings;
        m_sizeNorm = sizeNorm;
        m_opacityNorm = opacityNorm;
        m_color = color;
        scheduleRender();
    }

    void clearBrushData()
    {
        m_hasBrush = false;
        if (m_previewSession) {
            m_previewSession->clear();
        }
        update();
    }

    void setStrokeSegmentCountHint(int strokeSegmentCountHint)
    {
        const int normalizedHint = qMax(0, strokeSegmentCountHint);
        if (m_strokeSegmentCountHint == normalizedHint) {
            return;
        }

        m_strokeSegmentCountHint = normalizedHint;
        if (!m_hasBrush) {
            return;
        }

        scheduleRender();
    }

protected:
    bool hasBrushData() const { return m_hasBrush; }

    QImage previewImage() const { return m_previewSession ? m_previewSession->image() : QImage(); }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);

        if (!m_hasBrush) {
            return;
        }

        scheduleRender();
    }

private:
    QSize requestedPreviewSize() const
    {
        if (m_previewKind == PreviewKind::Dot) {
            const int previewSize = qMax(32, qMin(width(), height()));
            return QSize(previewSize, previewSize);
        }

        return QSize(qMax(32, width()), qMax(32, height()));
    }

    ruwa::core::brushes::BrushPreviewSpec currentSpec() const
    {
        ruwa::core::brushes::BrushPreviewSpec spec;
        spec.settings = m_settings;
        spec.sizeNorm = m_sizeNorm;
        spec.opacityNorm = m_opacityNorm;
        spec.color = m_color;
        spec.size = requestedPreviewSize();
        spec.strokeSegmentCountHint = m_strokeSegmentCountHint;
        return spec;
    }

    void scheduleRender()
    {
        if (!m_hasBrush || !m_previewSession) {
            return;
        }

        const auto spec = currentSpec();
        if (spec.size.isEmpty()) {
            return;
        }

        if (!m_previewSession->hasImageFor(spec)) {
            m_previewSession->request(spec);
        }
    }

private:
    PreviewKind m_previewKind = PreviewKind::Stroke;
    bool m_hasBrush = false;
    ruwa::core::brushes::BrushSettingsData m_settings;
    qreal m_sizeNorm = 0.5;
    qreal m_opacityNorm = 1.0;
    QColor m_color = Qt::black;
    ruwa::core::brushes::BrushPreviewSession* m_previewSession = nullptr;
    int m_strokeSegmentCountHint = 0;
};

class PreviewCanvas final : public AsyncBrushPreviewCanvasBase {
public:
    explicit PreviewCanvas(QWidget* parent = nullptr)
        : AsyncBrushPreviewCanvasBase(AsyncBrushPreviewCanvasBase::PreviewKind::Stroke, parent)
    {
        setObjectName(QStringLiteral("brush_editor_preview_canvas"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();
        const QRectF frameRect = rect();
        const qreal radius = theme.scaled(12);
        QPainterPath clipPath;
        clipPath.addRoundedRect(frameRect, radius, radius);

        painter.save();
        painter.setClipPath(clipPath);

        if (hasBrushData()) {
            if (!previewImage().isNull()) {
                painter.drawImage(frameRect.toRect(), previewImage());
            }
        } else {
            QPen strokePen(
                colors.primary, theme.scaled(12), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            const int strokeMargin = theme.scaled(18);
            QPainterPath strokePath;
            strokePath.moveTo(
                frameRect.left() + strokeMargin, frameRect.center().y() + theme.scaled(18));
            strokePath.cubicTo(frameRect.left() + width() * 0.28, frameRect.top() + height() * 0.18,
                frameRect.left() + width() * 0.62, frameRect.bottom() - height() * 0.2,
                frameRect.right() - strokeMargin, frameRect.center().y() - theme.scaled(6));
            painter.setPen(strokePen);
            painter.drawPath(strokePath);
        }

        painter.restore();
    }
};

class DotPreviewCanvas final : public AsyncBrushPreviewCanvasBase {
public:
    explicit DotPreviewCanvas(QWidget* parent = nullptr)
        : AsyncBrushPreviewCanvasBase(AsyncBrushPreviewCanvasBase::PreviewKind::Dot, parent)
    {
        setObjectName(QStringLiteral("brush_editor_dot_preview_canvas"));
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto& theme = ThemeManager::instance();
        const int sz = qMin(width(), height());
        const QRectF frameRect((width() - sz) / 2.0, 0, sz, sz);
        const qreal radius = theme.scaled(12);
        QPainterPath clipPath;
        clipPath.addRoundedRect(frameRect, radius, radius);

        painter.save();
        painter.setClipPath(clipPath);

        if (hasBrushData()) {
            if (!previewImage().isNull()) {
                painter.drawImage(frameRect.toRect(), previewImage());
            }
        }

        painter.restore();
    }
};

class TreePackItem final : public QWidget {
public:
    explicit TreePackItem(
        const QString& presetId, const QString& name, bool expanded, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_presetId(presetId)
        , m_name(name)
        , m_expanded(expanded)
    {
        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();
        setFixedHeight(theme.scaled(28));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);

        m_nameEditor = new QLineEdit(this);
        m_nameEditor->hide();
        m_nameEditor->setFrame(false);
        m_nameEditor->setText(m_name);
        m_nameEditor->setPlaceholderText(QObject::tr("Pack name"));
        m_nameEditor->setFocusPolicy(Qt::StrongFocus);

        QFont editorFont = m_nameEditor->font();
        editorFont.setPixelSize(theme.scaled(10));
        editorFont.setBold(true);
        m_nameEditor->setFont(editorFont);
        m_nameEditor->setStyleSheet(QStringLiteral(
            "QLineEdit { background: transparent; border: none; color: %1; padding: 0 2px; }")
                .arg(colors.text.name(QColor::HexArgb)));

        connect(m_nameEditor, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_name = text;
            if (onNameChanged)
                onNameChanged(text);
            update();
        });
        connect(m_nameEditor, &QLineEdit::editingFinished, this, [this]() { finishRename(); });
    }

    void setExpanded(bool expanded)
    {
        m_expanded = expanded;
        update();
    }
    bool isExpanded() const { return m_expanded; }

    void setSelected(bool selected)
    {
        m_selected = selected;
        update();
    }
    bool isSelected() const { return m_selected; }

    void setName(const QString& name)
    {
        m_name = name;
        if (m_nameEditor && !m_nameEditor->hasFocus()) {
            m_nameEditor->setText(name);
        }
        update();
    }

    QString name() const { return m_name; }
    QString presetId() const { return m_presetId; }

    void startRename()
    {
        if (!m_nameEditor)
            return;
        m_isEditing = true;
        updateEditorGeometry();
        m_nameEditor->setText(m_name);
        m_nameEditor->show();
        m_nameEditor->raise();
        m_nameEditor->setFocus();
        m_nameEditor->selectAll();
        update();
    }

    std::function<void()> onClicked;
    std::function<void()> onExpandToggled;
    std::function<void(const QString&)> onNameChanged;
    std::function<void()> onDoubleClicked;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();

        const QRectF r = rect().adjusted(2, 1, -2, -1);

        QColor bgColor = Qt::transparent;
        if (m_selected) {
            bgColor = ThemeColors::withAlpha(colors.primary, colors.isDark ? 38 : 52);
        } else if (m_hovered) {
            bgColor = colors.overlayHover();
        }
        if (bgColor != Qt::transparent) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(bgColor);
            painter.drawRect(r);
        }

        const int arrowSize = theme.scaled(16);
        const int arrowX = static_cast<int>(r.left()) + theme.scaled(4);
        const int arrowY = (height() - arrowSize) / 2;
        QColor arrowColor = m_selected ? colors.text : colors.textMuted;
        painter.setPen(arrowColor);
        QFont arrowFont = painter.font();
        arrowFont.setPixelSize(theme.scaled(8));
        painter.setFont(arrowFont);
        painter.drawText(QRect(arrowX, arrowY, arrowSize, arrowSize), Qt::AlignCenter,
            m_expanded ? QStringLiteral("\u25BE") : QStringLiteral("\u25B8"));

        if (!m_isEditing) {
            const int textLeft = arrowX + arrowSize + theme.scaled(2);
            QColor textColor
                = m_selected ? colors.text : (m_hovered ? colors.text : colors.textMuted);
            painter.setPen(textColor);
            QFont textFont = painter.font();
            textFont.setPixelSize(theme.scaled(10));
            textFont.setBold(true);
            painter.setFont(textFont);
            const QString displayName = m_name.isEmpty()
                ? m_name
                : QCoreApplication::translate("QObject", m_name.toUtf8().constData());
            QRectF textRect(textLeft, 0, r.right() - textLeft - theme.scaled(4), height());
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                painter.fontMetrics().elidedText(
                    displayName, Qt::ElideRight, static_cast<int>(textRect.width())));
        }
    }

    void enterEvent(QEnterEvent* event) override
    {
        QWidget::enterEvent(event);
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        m_hovered = false;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (m_isEditing) {
            QWidget::mousePressEvent(event);
            return;
        }
        if (event->button() == Qt::LeftButton) {
            m_pressed = true;
            update();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_isEditing) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        if (event->button() == Qt::LeftButton && m_pressed) {
            m_pressed = false;
            if (rect().contains(event->pos())) {
                auto& theme = ThemeManager::instance();
                const int arrowAreaWidth = theme.scaled(22);
                if (event->pos().x() < arrowAreaWidth) {
                    if (onExpandToggled)
                        onExpandToggled();
                } else {
                    if (onClicked)
                        onClicked();
                }
            }
            update();
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            auto& theme = ThemeManager::instance();
            if (event->pos().x() >= theme.scaled(22)) {
                if (onDoubleClicked)
                    onDoubleClicked();
                event->accept();
                return;
            }
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        updateEditorGeometry();
    }

private:
    void finishRename()
    {
        m_isEditing = false;
        if (m_nameEditor)
            m_nameEditor->hide();
        update();
    }

    void updateEditorGeometry()
    {
        if (!m_nameEditor)
            return;
        auto& theme = ThemeManager::instance();
        const int textLeft = theme.scaled(4) + theme.scaled(16) + theme.scaled(2) + 2;
        const int h = qMax(18, height() - theme.scaled(6));
        m_nameEditor->setGeometry(
            textLeft, (height() - h) / 2, qMax(40, width() - textLeft - theme.scaled(4) - 2), h);
    }

    QString m_presetId;
    QString m_name;
    bool m_expanded = true;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_isEditing = false;
    QLineEdit* m_nameEditor = nullptr;
};

class TreeBrushItem final : public QWidget {
public:
    explicit TreeBrushItem(const QString& brushId, const QString& name, QWidget* parent = nullptr)
        : QWidget(parent)
        , m_brushId(brushId)
        , m_name(name)
    {
        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();
        setFixedHeight(theme.scaled(24));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_TranslucentBackground);

        m_nameEditor = new QLineEdit(this);
        m_nameEditor->hide();
        m_nameEditor->setFrame(false);
        m_nameEditor->setText(m_name);
        m_nameEditor->setPlaceholderText(QObject::tr("Brush name"));
        m_nameEditor->setFocusPolicy(Qt::StrongFocus);

        QFont editorFont = m_nameEditor->font();
        editorFont.setPixelSize(theme.scaled(10));
        m_nameEditor->setFont(editorFont);
        m_nameEditor->setStyleSheet(QStringLiteral(
            "QLineEdit { background: transparent; border: none; color: %1; padding: 0 2px; }")
                .arg(colors.text.name(QColor::HexArgb)));

        connect(m_nameEditor, &QLineEdit::textChanged, this, [this](const QString& text) {
            m_name = text;
            if (onNameChanged)
                onNameChanged(text);
            update();
        });
        connect(m_nameEditor, &QLineEdit::editingFinished, this, [this]() { finishRename(); });
    }

    void setSelected(bool selected)
    {
        m_selected = selected;
        update();
    }
    bool isSelected() const { return m_selected; }

    void setName(const QString& name)
    {
        m_name = name;
        if (m_nameEditor && !m_nameEditor->hasFocus()) {
            m_nameEditor->setText(name);
        }
        update();
    }

    QString name() const { return m_name; }
    QString brushId() const { return m_brushId; }

    void startRename()
    {
        if (!m_nameEditor)
            return;
        m_isEditing = true;
        updateEditorGeometry();
        m_nameEditor->setText(m_name);
        m_nameEditor->show();
        m_nameEditor->raise();
        m_nameEditor->setFocus();
        m_nameEditor->selectAll();
        update();
    }

    std::function<void()> onClicked;
    std::function<void(const QString&)> onNameChanged;
    std::function<void()> onDoubleClicked;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        auto& theme = ThemeManager::instance();
        const auto& colors = WidgetStyleManager::instance().colors();

        const int indent = theme.scaled(22);
        const QRectF r(indent, 1, width() - indent - 2, height() - 2);

        QColor bgColor = Qt::transparent;
        if (m_selected) {
            bgColor = ThemeColors::withAlpha(colors.primary, colors.isDark ? 38 : 52);
        } else if (m_hovered) {
            bgColor = colors.overlayHover();
        }
        if (bgColor != Qt::transparent) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(bgColor);
            painter.drawRect(r);
        }

        if (!m_isEditing) {
            const int textLeft = indent + theme.scaled(8);
            QColor textColor
                = m_selected ? colors.text : (m_hovered ? colors.text : colors.textMuted);
            painter.setPen(textColor);
            QFont textFont = painter.font();
            textFont.setPixelSize(theme.scaled(10));
            textFont.setBold(false);
            painter.setFont(textFont);
            const QString displayName = m_name.isEmpty()
                ? m_name
                : QCoreApplication::translate("QObject", m_name.toUtf8().constData());
            QRectF textRect(textLeft, 0, r.right() - textLeft - theme.scaled(4), height());
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                painter.fontMetrics().elidedText(
                    displayName, Qt::ElideRight, static_cast<int>(textRect.width())));
        }
    }

    void enterEvent(QEnterEvent* event) override
    {
        QWidget::enterEvent(event);
        m_hovered = true;
        update();
    }

    void leaveEvent(QEvent* event) override
    {
        QWidget::leaveEvent(event);
        m_hovered = false;
        update();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (m_isEditing) {
            QWidget::mousePressEvent(event);
            return;
        }
        if (event->button() == Qt::LeftButton) {
            m_pressed = true;
            update();
        }
        QWidget::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (m_isEditing) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        if (event->button() == Qt::LeftButton && m_pressed) {
            m_pressed = false;
            if (rect().contains(event->pos())) {
                if (onClicked)
                    onClicked();
            }
            update();
        }
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton) {
            if (onDoubleClicked)
                onDoubleClicked();
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        updateEditorGeometry();
    }

private:
    void finishRename()
    {
        m_isEditing = false;
        if (m_nameEditor)
            m_nameEditor->hide();
        update();
    }

    void updateEditorGeometry()
    {
        if (!m_nameEditor)
            return;
        auto& theme = ThemeManager::instance();
        const int indent = theme.scaled(22);
        const int textLeft = indent + theme.scaled(6);
        const int h = qMax(18, height() - theme.scaled(4));
        m_nameEditor->setGeometry(
            textLeft, (height() - h) / 2, qMax(40, width() - textLeft - theme.scaled(4) - 2), h);
    }

    QString m_brushId;
    QString m_name;
    bool m_selected = false;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_isEditing = false;
    QLineEdit* m_nameEditor = nullptr;
};

inline void applyScaledFont(QWidget* widget, int pixelSize, bool bold = false)
{
    if (!widget) {
        return;
    }

    QFont font = widget->font();
    font.setPixelSize(pixelSize);
    font.setBold(bold);
    widget->setFont(font);
}

} // namespace ruwa::ui::windows::layout_internal

#endif // RUWA_UI_WINDOWS_BRUSHEDITOR_LAYOUT_BRUSHEDITORLAYOUTPARTS_H
