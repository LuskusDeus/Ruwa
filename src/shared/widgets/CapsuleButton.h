// SPDX-License-Identifier: MPL-2.0

// CapsuleButton.h
#ifndef RUWA_SHARED_WIDGETS_CAPSULEBUTTON_H
#define RUWA_SHARED_WIDGETS_CAPSULEBUTTON_H

#include <QPushButton>
#include <QString>

class QPropertyAnimation;
class QResizeEvent;

namespace ruwa::ui::widgets {

class DotGridLoadingIndicator;

/**
 * @brief Pill-shaped animated button
 *
 * Two visual variants:
 * - Tab:    Checkable pill; inactive = border + dim text,
 *           active = primary fill + textOnPrimary (theme accent, same family as segmented
 * controls).
 * - Action: Always solid accent fill + textOnPrimary text.
 *           Supports an optional hint text rendered lighter/smaller after the
 *           main label (e.g. "Create Project  ≈ 7.9 MB").
 */
class CapsuleButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal checkProgress READ checkProgress WRITE setCheckProgress)

public:
    enum class Variant { Tab, Action, Primary, Secondary };

    explicit CapsuleButton(const QString& text, Variant variant, QWidget* parent = nullptr);
    ~CapsuleButton() override;

    /// Secondary hint text rendered after the main label (Action variant only).
    void setHintText(const QString& hint);
    QString hintText() const { return m_hintText; }

    void syncSizeToText();
    void setSizeScale(qreal scale);
    void setBaseMinimumWidth(int width);
    /// Banner variants only: override the base (unscaled) pill height. 0 = use default.
    void setBannerBaseHeight(int basePx);
    void setLightBannerContext(bool enabled);
    void setSecondaryIdleShadowAlpha(int alpha);
    void setSecondaryIdleFillAlpha(int alpha);
    /// Secondary variant: paint an opaque resting plate in the theme's surfaceAlt
    /// colour (read live), so the pill reads like the filled Color-panel hex input
    /// instead of a transparent outline. Default off.
    void setSecondaryRestingFillAlt(bool enabled);
    void setPrimaryBorderVisible(bool visible);
    void setTrailingLoadingVisible(bool visible);
    bool trailingLoadingVisible() const { return m_trailingLoadingVisible; }

    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal p);

    /// Tab variant: 0 = inactive look, 1 = selected look (animated with toggled).
    qreal checkProgress() const { return m_checkProgress; }
    void setCheckProgress(qreal p);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onThemeChanged();

private:
    void setupAnimations();
    void startHoverAnimation(bool hovered);
    void startCheckAnimation(bool checked);
    void updateBannerScaledSizes();
    void ensureTrailingLoadingIndicator();
    void updateTrailingLoadingIndicator();
    bool isBannerVariant() const;

    Variant m_variant;
    QString m_hintText;
    qreal m_hoverProgress { 0.0 };
    qreal m_checkProgress { 0.0 };
    qreal m_sizeScale { 1.0 };
    int m_baseMinimumWidth { 168 };
    int m_bannerBaseHeight { 0 };
    bool m_lightBanner { false };
    int m_secondaryIdleShadowAlpha { 0 };
    int m_secondaryIdleFillAlpha { 0 };
    bool m_secondaryRestingFillAlt { false };
    bool m_primaryBorderVisible { true };
    bool m_trailingLoadingVisible { false };
    DotGridLoadingIndicator* m_trailingLoadingIndicator { nullptr };
    QPropertyAnimation* m_hoverAnimation { nullptr };
    QPropertyAnimation* m_checkAnimation { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_SHARED_WIDGETS_CAPSULEBUTTON_H
