// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORWINDOW_H
#define RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORWINDOW_H

#include <QWidget>

class QVBoxLayout;

namespace QWK {
class WidgetWindowAgent;
}

namespace ruwa::ui::windows {

class BrushEditorTopBar;
class BrushEditorLayoutWidget;

class BrushEditorWindow : public QWidget {
    Q_OBJECT

public:
    explicit BrushEditorWindow(QWidget* parent = nullptr);
    ~BrushEditorWindow() override;

    void setBrushName(const QString& brushName);
    void setSelection(const QString& presetId, const QString& brushId);
    QString brushName() const { return m_brushName; }

signals:
    /// Emitted when the brush selection changes from *inside* the editor
    /// (e.g. the user clicks a different brush in the library list).
    /// Not emitted when selection is set externally via setSelection().
    void brushSelectionChanged(const QString& presetId, const QString& brushId);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onThemeChanged();

private:
    void setupUI();
    void setupWindowAgent();
    /// Disables workspace shortcuts while a text-input descendant of this
    /// window holds focus, so typing a name (space, Ctrl/Alt combos, …) is
    /// not swallowed by global ApplicationShortcuts driving the workspace.
    void updateShortcutBlocking(QWidget* focusWidget);
    void applyWindowEffects();
    void updateScaledSizes();
    void updateContentStyles();

private:
    QVBoxLayout* m_mainLayout = nullptr;
    BrushEditorTopBar* m_topBar = nullptr;
    QWidget* m_content = nullptr;
    BrushEditorLayoutWidget* m_layoutWidget = nullptr;
    QWK::WidgetWindowAgent* m_windowAgent = nullptr;
    QString m_brushName;
    bool m_shortcutsBlocked = false;

    static constexpr int BaseWidth = 960;
    static constexpr int BaseHeight = 720;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORWINDOW_H
