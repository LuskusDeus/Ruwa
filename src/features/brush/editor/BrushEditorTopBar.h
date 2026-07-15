// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORTOPBAR_H
#define RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORTOPBAR_H

#include <QList>
#include <QWidget>

class QHBoxLayout;
class QEvent;
class QLabel;

namespace ruwa::ui::widgets {
class LogoButton;
class WindowControlButton;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::windows {

class BrushEditorTopBar : public QWidget {
    Q_OBJECT

public:
    explicit BrushEditorTopBar(QWidget* parent = nullptr);
    ~BrushEditorTopBar() override = default;

    void setBrushName(const QString& brushName);
    QString brushName() const { return m_brushName; }

    QWidget* closeButton() const;

    /** Extra widgets QWindowKit should treat as client hit targets (e.g. gutter bands). Brush
     * editor has none. */
    QList<QWidget*> qwkExtraHitTestWidgets() const { return {}; }

signals:
    void closeRequested();

protected:
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onThemeChanged();

private:
    int visualInsetPx() const;
    void updateTitleText();
    void updateScaledSizes();
    void updateStyles();

private:
    QHBoxLayout* m_mainLayout = nullptr;
    widgets::LogoButton* m_logoButton = nullptr;
    QWidget* m_titleContainer = nullptr;
    QLabel* m_editorLabel = nullptr;
    QLabel* m_titleLabel = nullptr;
    widgets::WindowControlButton* m_closeButton = nullptr;
    QString m_brushName;

    static constexpr int BaseHeight = 32;
    static constexpr int BaseVisualInset = 6;
    static constexpr int BaseSidePadding = 10;
    static constexpr int BaseTitleGap = 8;
    static constexpr int BaseCloseButtonWidth = 46;
};

} // namespace ruwa::ui::windows

#endif // RUWA_UI_WINDOWS_BRUSHEDITOR_BRUSHEDITORTOPBAR_H
