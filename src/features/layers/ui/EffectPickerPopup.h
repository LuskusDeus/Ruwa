// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_EFFECTPICKERPOPUP_H
#define RUWA_UI_WORKSPACE_EFFECTPICKERPOPUP_H

#include <QPoint>
#include <QPointer>
#include <QVector>
#include <QWidget>

class QVBoxLayout;
class QPropertyAnimation;
class QVariantAnimation;
class QGraphicsOpacityEffect;

namespace ruwa::ui::widgets {
class SmoothScrollArea;
class SearchBar;
class OverlayContainer;
} // namespace ruwa::ui::widgets

namespace ruwa::ui::workspace {

class EffectPickerRow;
class EffectPickerHeader;

/**
 * @brief Floating add-effect picker: search field + collapsible category
 * folders (from LayerEffectRegistry::catalog()).
 *
 * Hosted as a child of the shared OverlayContainer (same system as the top-bar
 * menu/message popups): it layers correctly above the GL canvas, is included in
 * the overlay hit-test mask, and fades/slides on show/hide. An app-level event
 * filter dismisses it on an outside click. Emits effectChosen() with the
 * registered typeId of the picked effect.
 */
class EffectPickerPopup : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal popupOpacity READ popupOpacity WRITE setPopupOpacity)

public:
    explicit EffectPickerPopup(QWidget* parent = nullptr);
    ~EffectPickerPopup() override;

    /// Rebuild the catalog and pop up just beneath \p anchor (clamped to the
    /// overlay). \p anchor also defines the window whose overlay hosts the popup.
    void popupUnder(QWidget* anchor);

    void hidePopup();

    bool isPopupVisible() const { return m_isVisible; }
    bool isHiding() const { return m_isHiding; }

    qreal popupOpacity() const { return m_opacity; }
    void setPopupOpacity(qreal opacity);

signals:
    void effectChosen(const QString& typeId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuild();
    void applyFilter(const QString& text);
    /// Fixed content-area height derived from the anchor's screen (a fraction of
    /// its available height, clamped). Independent of which folders are expanded
    /// or how many rows match a filter — overflow scrolls instead of resizing
    /// the popup, so opening/collapsing folders never changes its size.
    int screenBasedContentHeight() const;
    /// Computes the overlay-space target position and records whether the popup
    /// ended up above the anchor (\ref m_placedAbove), which drives slide direction.
    QPoint calculateTargetPos();
    void startShowAnimation();
    void finishHide();
    /// Smoothly (or instantly, e.g. while filtering) reveal/hide a folder's body
    /// by tweening its height between 0 and its natural (all-visible-rows) size.
    /// Rows keep their real geometry throughout; only the folder's own clipped
    /// height changes, so nothing is ever reparented or hidden outright.
    void setSectionBodyExpanded(
        QWidget* body, QVariantAnimation* anim, bool expanded, bool animated);

    struct Section {
        EffectPickerHeader* header = nullptr;
        QWidget* body = nullptr;
        QVector<EffectPickerRow*> rows;
        QVariantAnimation* heightAnim = nullptr;
    };

    ruwa::ui::widgets::SearchBar* m_search = nullptr;
    ruwa::ui::widgets::SmoothScrollArea* m_scroll = nullptr;
    QWidget* m_listContent = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QVector<Section> m_sections;
    bool m_filtering = false;

    ruwa::ui::widgets::OverlayContainer* m_overlay = nullptr;
    QPointer<QWidget> m_anchor;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    qreal m_opacity = 0.0;
    bool m_isVisible = false;
    bool m_isHiding = false;
    bool m_placedAbove = false;
    bool m_appFilterInstalled = false;
    QPoint m_targetPos;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_EFFECTPICKERPOPUP_H
