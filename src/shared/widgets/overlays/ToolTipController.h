// SPDX-License-Identifier: MPL-2.0

// ToolTipController.h - Application-wide themed tooltip presentation
#ifndef RUWA_UI_WIDGETS_OVERLAYS_TOOLTIPCONTROLLER_H
#define RUWA_UI_WIDGETS_OVERLAYS_TOOLTIPCONTROLLER_H

#include <QKeySequence>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>

class QEvent;
class QHelpEvent;
class QTimer;
class QWidget;

namespace ruwa::ui::widgets {

class CustomToolTipWindow;

/**
 * Replaces QWidget's native tooltip presentation while preserving the normal
 * setToolTip()/toolTipDuration() API at every call site.
 */
class ToolTipController final : public QObject {
public:
    explicit ToolTipController(QObject* parent = nullptr);
    ~ToolTipController() override;

    /**
     * Associates a tooltip source with the command executed by that widget.
     * The current (possibly user-customized) shortcut is resolved when the
     * tooltip is shown, so callers must not copy shortcut text into toolTip().
     */
    static void setShortcutCommand(QWidget* widget, const QString& commandId);

    /**
     * Enables or disables the shortcut keycaps for one tooltip source.
     * Shortcut display is enabled by default for widgets with a command ID.
     */
    static void setShortcutVisible(QWidget* widget, bool visible);
    static bool isShortcutVisible(const QWidget* widget);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget* toolTipSourceFor(QWidget* widget) const;
    QKeySequence shortcutFor(const QWidget* widget) const;
    void showFor(QWidget* eventTarget, const QHelpEvent& event);
    void hideTip();
    void hideIfPointerLeftSource();

    CustomToolTipWindow* m_toolTip = nullptr;
    QTimer* m_hideTimer = nullptr;
    QPointer<QWidget> m_source;
    QPointer<QWidget> m_eventTarget;
    QMetaObject::Connection m_sourceDestroyedConnection;
    QString m_text;
    QKeySequence m_shortcut;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_OVERLAYS_TOOLTIPCONTROLLER_H
