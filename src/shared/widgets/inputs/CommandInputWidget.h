// SPDX-License-Identifier: MPL-2.0

// CommandInputWidget.h
#ifndef RUWA_UI_WIDGETS_COMMON_COMMANDINPUTWIDGET_H
#define RUWA_UI_WIDGETS_COMMON_COMMANDINPUTWIDGET_H

#include "shared/widgets/BaseStyledWidget.h"

#include <QKeySequence>
#include <QRectF>
#include <QStringList>

class QObject;
class QEvent;
class QMouseEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Widget for capturing and displaying keyboard shortcuts
 *
 * Click to enter recording mode: the widget intercepts key presses and
 * emits shortcutRecorded(QKeySequence). Click outside to cancel.
 * Displays the current shortcut for the assigned command.
 *
 * Uses BaseStyledWidget for consistent styling.
 */
class CommandInputWidget : public BaseStyledWidget {
    Q_OBJECT

public:
    enum class SizeVariant { Normal, Compact };
    explicit CommandInputWidget(
        QWidget* parent = nullptr, SizeVariant sizeVariant = SizeVariant::Normal);
    ~CommandInputWidget() override;

    bool isRecording() const { return m_recording; }

    /// Associate with a command to display its current shortcut
    void setCommandId(const QString& commandId);
    QString commandId() const { return m_commandId; }

    /// Display this key sequence (e.g. from ShortcutManager)
    void setKeySequence(const QKeySequence& seq);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    /// Emitted when a key combination is captured in recording mode
    void shortcutRecorded(const QKeySequence& shortcut);

    /// Emitted when recording mode starts
    void recordingStarted();

    /// Emitted when recording mode stops
    void recordingStopped();

protected:
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;
    bool hitButton(const QPoint& pos) const override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void startRecording();
    void stopRecording();
    void updateDisplayedShortcut();
    QStringList displayedKeyParts() const;
    QStringList currentParts() const;
    qreal naturalContentWidth() const;
    QRectF keyGroupRect(const QRectF& rect) const;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateSizes();

private:
    bool m_recording = false;
    QString m_commandId;
    QKeySequence m_keySequence;
    SizeVariant m_sizeVariant;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_COMMANDINPUTWIDGET_H
