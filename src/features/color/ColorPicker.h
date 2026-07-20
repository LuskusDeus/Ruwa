// SPDX-License-Identifier: MPL-2.0

// ColorPicker.h
#ifndef RUWA_UI_WIDGETS_COLORPICKER_COLORPICKER_H
#define RUWA_UI_WIDGETS_COLORPICKER_COLORPICKER_H

#include <QWidget>
#include <QPropertyAnimation>
#include <QColor>
#include <QImage>
#include <QPixmap>
#include <QSize>
#include <QElapsedTimer>
#include <QVector>
#include <QPolygonF>
#include <QString>
#include "features/theme/manager/ThemeManager.h"

class QTabletEvent;

namespace ruwa::ui::widgets {

class BaseStyledWidget;
class ColorSlotSwitchWidget;
class HexColorInput;

/**
 * @brief Advanced color picker widget with HSV selector
 *
 * Features:
 * - Three picker modes: Classic (SV square + hue bar), Triangle (hue ring + SV triangle),
 *   Square (hue ring + SV square)
 * - Hex input field
 * - Color preview (old vs new)
 * - Accept/Cancel buttons
 * - Smooth show/hide animations
 */
class ColorPicker : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)
    Q_PROPERTY(qreal slideProgress READ slideProgress WRITE setSlideProgress)
    Q_PROPERTY(qreal recentHoverProgress READ recentHoverProgress WRITE setRecentHoverProgress)
    Q_PROPERTY(qreal recentLayoutProgress READ recentLayoutProgress WRITE setRecentLayoutProgress)
    Q_PROPERTY(qreal closeHoverProgress READ closeHoverProgress WRITE setCloseHoverProgress)

public:
    enum class PickerMode { Classic, Triangle, Square };
    Q_ENUM(PickerMode)

    explicit ColorPicker(QWidget* parent = nullptr);
    ~ColorPicker() override;

    /// Get current color
    QColor color() const { return m_currentColor; }

    /// Set color (updates all UI elements)
    void setColor(const QColor& color);
    void setPopupTitle(const QString& title);

    /// Show with animation
    void showAnimated();

    /// Hide with animation
    void hideAnimated();

    /// Check if visible/animating
    bool isActive() const;
    bool containsVisiblePopupPoint(const QPoint& pos) const;
    int popupShadowMarginPixels() const;

    /// When true, picker is embedded (e.g. in ColorPanel): no overlay animations, always visible
    void setEmbeddedMode(bool embedded);
    bool isEmbeddedMode() const { return m_embeddedMode; }
    int embeddedPreferredHeight() const;
    void setDualColorModeEnabled(bool enabled);
    bool isDualColorModeEnabled() const { return m_dualColorMode; }
    void setForegroundColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setActiveColorSlot(bool isForeground);
    bool isEditingForeground() const { return m_editingForeground; }

    /// Picker mode (Classic, Triangle, Square). Mode switcher shown only in embedded mode.
    void setPickerMode(PickerMode mode);
    PickerMode pickerMode() const { return m_pickerMode; }

    /// Show/hide built-in mode switcher buttons. Default: true.
    /// Set to false when mode buttons are hosted externally (e.g. in a dock title bar).
    void setModeSwitcherVisible(bool visible);
    bool isModeSwitcherVisible() const { return m_modeSwitcherVisible; }

    /// Recent colors palette (right section with squares). Toggleable.
    void setRecentColorsEnabled(bool enabled);
    bool isRecentColorsEnabled() const { return m_recentColorsEnabled; }
    void setRecentColorsMaxCount(int count);
    int recentColorsMaxCount() const { return m_recentColorsMaxCount; }
    /// Add color to recent palette (call when user draws with this color, not when selecting)
    void addColorToRecent(const QColor& color);
    /// Set full recent colors list (e.g. when restoring from settings)
    void setRecentColors(const QVector<QColor>& colors);
    QVector<QColor> recentColors() const { return m_recentColors; }

    /// Smoothly fade the whole picker to/from a luminance grayscale rendering.
    /// Used while painting on a layer mask, where only brightness matters. The
    /// underlying color state is unchanged — this is a display-only effect.
    void setMaskEditMode(bool active);
    bool isMaskEditMode() const { return m_maskEditMode; }

    /// Show an opacity (alpha) slider below the hue bar (popup mode). Off by
    /// default; enable for inputs that store an alpha channel (e.g. gradient
    /// overlay colors). While disabled the picked color stays fully opaque.
    void setAlphaSliderEnabled(bool enabled);
    bool isAlphaSliderEnabled() const { return m_alphaSliderEnabled; }

    // Animation properties
    qreal showProgress() const { return m_showProgress; }
    void setShowProgress(qreal progress);
    qreal slideProgress() const { return m_slideProgress; }
    void setSlideProgress(qreal progress);
    qreal recentHoverProgress() const { return m_recentHoverProgress; }
    void setRecentHoverProgress(qreal progress);
    qreal recentLayoutProgress() const { return m_recentLayoutProgress; }
    void setRecentLayoutProgress(qreal progress);
    qreal closeHoverProgress() const { return m_closeHoverProgress; }
    void setCloseHoverProgress(qreal progress);

    QSize sizeHint() const override
    {
        if (!m_embeddedMode)
            return QSize(-1, -1);
        return QSize(-1, embeddedPreferredHeight());
    }
    QSize minimumSizeHint() const override
    {
        if (!m_embeddedMode)
            return QWidget::minimumSizeHint();
        auto& theme = ruwa::ui::core::ThemeManager::instance();
        return QSize(theme.scaled(96), embeddedPreferredHeight());
    }

signals:
    void colorChanged(const QColor& color);
    void canceled(); // Emitted on ESC
    void activeColorSlotChanged(bool isForeground);
    void swapColorsRequested();
    void recentColorsChanged();
    void pickerModeChanged(PickerMode mode);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void tabletEvent(QTabletEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onHexTextChanged(const QString& digits);
    void onThemeChanged();
    void onShowAnimationFinished();
    void onHideAnimationFinished();

private:
    void setupUI();
    void setupAnimations();
    void connectSignals();
    void updateFromHSV();
    void updateHexInput();
    void updateSize();

    // Classic mode geometry
    qreal effectivePad() const;
    qreal effectiveSquareSide() const;
    qreal effectiveSVHeight() const;
    qreal popupShadowMargin() const;
    QRectF popupPanelRect() const;
    QRectF popupHeaderRect() const;
    QRectF popupCloseButtonRect() const;
    QRectF svSquareRect() const;
    QRectF hueBarRect() const;
    QRectF alphaBarRect() const; // opacity slider below the hue bar (popup)
    /// Extra vertical room the alpha slider adds to the popup panel (0 if off).
    qreal alphaBarExtent() const;

    // Hit testing
    enum class DragMode { None, SV, Hue, Alpha, HueRing, TriangleSV, SquareSV };
    DragMode hitTest(const QPoint& pos) const;
    bool hitTestPopupClose(const QPoint& pos) const;
    int hitTestRecentColor(const QPoint& pos) const;
    QRectF recentColorsSectionRect() const;
    QRectF recentColorSquareRect(int index) const;
    QRectF recentColorSquareBaseRect(int index) const;
    int effectiveRecentColorsMaxCount() const;
    int targetRecentColorsVisibleCount() const;
    qreal recentColorVisibility(int index) const;
    void updateRecentColorsVisibility(bool animated);
    void moveRecentColorToTop(int index);
    void handleDrag(const QPoint& pos);
    bool shouldApplyDragUpdate(bool force);
    void setPopupCloseHovered(bool hovered);

    // Mode switcher (embedded mode only)
    qreal modeSwitcherTotalHeight() const;
    QRectF modeSwitcherButtonRect(int index) const;
    int hitTestModeSwitcher(const QPoint& pos) const;
    void drawModeSwitcher(QPainter& painter);

    // Ring-based modes (Triangle, Square) geometry
    QRectF ringAreaRect() const;
    QPointF ringCenter() const;
    qreal ringOuterRadius() const;
    qreal ringInnerRadius() const;

    // Hue ring drawing and interaction
    void drawHueRing(QPainter& painter);
    void drawRingHueCursor(QPainter& painter);
    void handleHueRingDrag(const QPoint& pos);

    // Triangle mode
    QPolygonF trianglePolygon() const;
    void drawColorTriangle(QPainter& painter);
    void ensureTriangleCache();
    void drawTriangleSVCursor(QPainter& painter);
    void handleTriangleSVDrag(const QPoint& pos);
    QPointF svToTrianglePos(qreal s, qreal v) const;

    // Square-in-ring mode
    QRectF squareInRingRect() const;
    void drawSquareInRing(QPainter& painter);
    void drawSquareInRingSVCursor(QPainter& painter);
    void handleSquareInRingSVDrag(const QPoint& pos);

    DragMode hitTestRingMode(const QPoint& pos) const;

    // Drawing (classic mode)
    void drawBackground(QPainter& painter);
    void drawSVSquare(QPainter& painter, const QRectF& rect);
    void ensureSVSquareCache(const QSize& size);
    void drawHueBar(QPainter& painter, const QRectF& rect);
    void drawAlphaBar(QPainter& painter, const QRectF& rect);
    void drawSVCursor(QPainter& painter, const QRectF& rect);
    void drawHueCursor(QPainter& painter, const QRectF& rect);
    void drawAlphaCursor(QPainter& painter, const QRectF& rect);
    void drawRecentColors(QPainter& painter);
    void drawPopupHeader(QPainter& painter);
    void drawPickerContent(QPainter& painter);
    void updateDualModeControls();
    void updateDualModeGeometry();
    void positionHexInput();

private:
    // Color state
    QColor m_currentColor;
    QColor m_initialColor;
    qreal m_hue = 0.0; // 0.0 - 1.0
    qreal m_saturation = 1.0; // 0.0 - 1.0
    qreal m_value = 1.0; // 0.0 - 1.0
    qreal m_alpha = 1.0; // 0.0 - 1.0 (only meaningful when the slider is on)
    bool m_alphaSliderEnabled = false;

    // UI elements
    HexColorInput* m_hexInput = nullptr;
    ColorSlotSwitchWidget* m_colorSlotSwitch = nullptr;
    QString m_popupTitle;

    // Interaction
    DragMode m_dragMode = DragMode::None;
    bool m_tabletDragActive = false;
    bool m_closeHovered = false;
    bool m_closePressed = false;
    QElapsedTimer m_dragUpdateTimer;

    // Animation
    qreal m_showProgress = 0.0;
    bool m_isShowing = false;
    bool m_isHiding = false;
    bool m_embeddedMode = false;
    bool m_dualColorMode = false;
    bool m_editingForeground = true;
    QColor m_foregroundColor = Qt::black;
    QColor m_backgroundColor = Qt::white;

    // Picker mode
    PickerMode m_pickerMode = PickerMode::Classic;
    bool m_modeSwitcherVisible = true;

    // Mask-edit grayscale display. The effect lives on this widget and grays the
    // whole picker (SV area, hue, swatches, recents). m_maskGrayscaleAnim drives
    // its strength so the transition is smooth.
    bool m_maskEditMode = false;
    class QGraphicsEffect* m_maskGrayscaleEffect = nullptr;
    QVariantAnimation* m_maskGrayscaleAnim = nullptr;

    // Slide animation for mode transitions
    QPixmap m_slideSnapshot; // old state snapshot
    QPixmap m_slideNewSnapshot; // new state snapshot (child widgets included)
    qreal m_slideProgress = 1.0; // 1.0 = no slide active; 0→1 during transition
    int m_slideDirection = 1; // 1 = new from right, -1 = new from left
    QPropertyAnimation* m_slideAnimation = nullptr;

    // Recent colors palette
    bool m_recentColorsEnabled = false;
    QVector<QColor> m_recentColors;
    int m_recentColorsMaxCount = 50;
    int m_hoveredRecentIndex = -1;
    qreal m_recentHoverProgress = 0.0;
    qreal m_recentLayoutProgress = 1.0;
    int m_recentPreviousVisibleCount = 0;
    int m_recentTargetVisibleCount = 0;
    qreal m_closeHoverProgress = 0.0;
    QPropertyAnimation* m_recentHoverAnimation = nullptr;
    QPropertyAnimation* m_recentLayoutAnimation = nullptr;
    QPropertyAnimation* m_closeHoverAnimation = nullptr;
    QPropertyAnimation* m_showAnimation = nullptr;

    // SV square cache: content depends only on m_hue and size; avoids per-frame pixel loop
    QImage m_svSquareCache;
    qreal m_svSquareCacheHue = -1.0;
    QSize m_svSquareCacheSize;

    // Triangle cache: depends on m_hue and triangle geometry
    QImage m_triangleCache;
    qreal m_triangleCacheHue = -1.0;
    QRectF m_triangleCacheBounds;

    // Layout constants
    static constexpr int Padding = 16;
    static constexpr int OverlayPadding = 12;
    static constexpr int SVSquareSize = 200;
    static constexpr int OverlaySVWidth = 220;
    static constexpr int OverlaySVHeight = 124;
    static constexpr int PopupHeaderHeight = 18;
    static constexpr int PopupHeaderBottomGap = 8;
    static constexpr int PopupHeaderSwatchSize = 14;
    static constexpr int PopupHeaderCloseSize = 18;
    static constexpr int PopupHeaderSpacing = 6;
    static constexpr int HueBarHeight = 12;
    static constexpr int HueBarSpacing = 12;
    static constexpr int SVCursorRadius = 6;
    static constexpr int HueCursorWidth = 6;
    static constexpr int HueCursorOverhang = 6;
    static constexpr int HueCursorDiameter = 14;
    static constexpr int HexInputHeight = 36;
    static constexpr int HexInputSpacing = 10;
    static constexpr int ColorSwitchWidth = 44;
    static constexpr int ColorSwitchGap = 8;
    static constexpr int CornerRadius = 16;
    static constexpr int ColorAreaCornerRadius = 8;
    static constexpr int PopupShadowMargin = 16;
    static constexpr int RecentColorsSquareSize = 14;
    static constexpr int RecentColorsGap = 4; // horizontal gap between squares
    static constexpr int RecentColorsTopGap = 10; // vertical gap from sv square / ring area
    static constexpr int RecentColorsSectionGap = 12;

    // Ring mode constants
    static constexpr int RingThickness = 16;
    static constexpr int RingInnerGap = 3;
    static constexpr int ModeSwitcherBtnSize = 22;
    static constexpr int ModeSwitcherGap = 2;
    static constexpr int ModeSwitcherBottomGap = 8;

    // Calculated dimensions
    // Overlay panel width: Padding + SV area + Padding = 12 + 220 + 12 = 244.
    static constexpr int PickerWidth = 244;
    // Overlay panel height: Padding + Header + Header gap + SV area + Hue gap + Hue bar
    // + Hex gap + Hex input + Padding = 12 + 18 + 8 + 124 + 12 + 12 + 10 + 36 + 12 = 244.
    static constexpr int PickerHeight = 244;

    // Animation
    static constexpr int ShowDuration = 200;
    static constexpr int HideDuration = 150;
    static constexpr int DragUpdateIntervalMs = 8;
    static constexpr int SlideDuration = 250;
    static constexpr int RecentColorsLayoutDuration = 140;
    static constexpr int MaskGrayscaleDuration = 280;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COLORPICKER_COLORPICKER_H
