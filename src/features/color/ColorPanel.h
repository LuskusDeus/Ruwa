// SPDX-License-Identifier: MPL-2.0

// ColorPanel.h
#ifndef RUWA_UI_WORKSPACE_PANELS_COLORPANEL_H
#define RUWA_UI_WORKSPACE_PANELS_COLORPANEL_H

#include "shell/docking/widgets/DockPanel.h"
#include <QColor>
#include <QVector>
#include <QWidget>

namespace ruwa::ui::widgets {
class AnimatedStackedWidget;
class ColorChannelSlidersWidget;
class ColorPicker;
} // namespace ruwa::ui::widgets

class QPushButton;
class QVBoxLayout;

namespace ruwa::ui::workspace {

// Простой виджет для превью цвета с поддержкой прозрачности
class ColorPreviewWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorPreviewWidget(bool isForeground = false, QWidget* parent = nullptr);

    void setColor(const QColor& color);
    void setActive(bool active);
    QColor color() const { return m_color; }

signals:
    void clicked(bool isForeground);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void drawCheckerboard(QPainter& painter, const QRect& rect);

    QColor m_color = Qt::black;
    bool m_active = false;
    bool m_isForeground = false;
};

/**
@brief Panel for color selection with integrated ColorPicker
*/
class ColorPanel : public ruwa::ui::docking::DockPanel {
    Q_OBJECT
public:
    explicit ColorPanel(QWidget* parent = nullptr);
    ~ColorPanel() override;

    QColor foregroundColor() const { return m_foregroundColor; }
    void setForegroundColor(const QColor& color);

    QColor backgroundColor() const { return m_backgroundColor; }
    void setBackgroundColor(const QColor& color);
    void applyColorState(const QColor& foreground, const QColor& background, bool isForeground);
    void setActiveColorSlot(bool isForeground);
    void swapForegroundBackgroundColors();
    void resetForegroundBackgroundColors();
    bool isEditingForeground() const { return m_editingForeground; }
    QColor activeColor() const
    {
        return m_editingForeground ? m_foregroundColor : m_backgroundColor;
    }

    /// Add color to recent palette (call when user draws with this color)
    void addColorToRecent(const QColor& color);
    void setRecentColors(const QVector<QColor>& colors);
    QVector<QColor> recentColors() const;

    /// Fade the color display to/from grayscale. Enabled while the active paint
    /// target is a layer mask (a mask only reacts to luminance). Purely visual —
    /// the stored foreground/background colors are untouched.
    void setMaskEditMode(bool active);
    bool isMaskEditMode() const { return m_maskEditMode; }

signals:
    void foregroundColorChanged(const QColor& color);
    void backgroundColorChanged(const QColor& color);
    void activeColorSlotChanged(bool isForeground);
    void activeColorChanged(const QColor& color);
    void pickerModeChanged(int mode);
    void channelModeChanged(int mode);
    void recentColorsChanged();

protected:
    QWidget* createContent() override;
    void onThemeChanged() override;
    QJsonObject savePanelState() const override;
    void restorePanelState(const QJsonObject& state) override;

private slots:
    void onColorPicked(const QColor& newColor);

private:
    void updateColorPreview();
    void updatePickerFromCurrentColor();
    void updateStyles();
    void setupModeSwitcher();
    void updateModeSwitcherButtons();
    void setupChannelSection(QVBoxLayout* parentLayout);
    QWidget* createChannelWidget(bool rgbMode);
    void setChannelMode(int mode);
    void updateChannelModeButtons();
    void updateChannelSliders();

private:
    QWidget* m_contentWidget = nullptr;

    // Integrated ColorPicker (embedded mode)
    ruwa::ui::widgets::ColorPicker* m_colorPicker = nullptr;

    // Mode switcher buttons (in interactive title area)
    QVector<QPushButton*> m_modeButtons;
    QVector<QPushButton*> m_channelModeButtons;

    // Independent RGB/HSV channel section below the picker
    ruwa::ui::widgets::AnimatedStackedWidget* m_channelStack = nullptr;
    ruwa::ui::widgets::ColorChannelSlidersWidget* m_rgbChannelSliders = nullptr;
    ruwa::ui::widgets::ColorChannelSlidersWidget* m_hsvChannelSliders = nullptr;

    // Colors
    QColor m_foregroundColor = Qt::black;
    QColor m_backgroundColor = Qt::white;
    bool m_editingForeground = true; // true = FG, false = BG
    int m_pickerMode = 0;
    int m_channelMode = 0; // 0 = HSV, 1 = RGB

    // Grayscale (mask edit) display mode. Remembered so it can be applied to the
    // picker when content is (re)created.
    bool m_maskEditMode = false;

    // Update flag to prevent recursion
    bool m_updating = false;
};

} // namespace ruwa::ui::workspace
#endif // RUWA_UI_WORKSPACE_PANELS_COLORPANEL_H
