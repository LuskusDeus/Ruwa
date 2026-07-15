// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WORKSPACE_CANVASBRUSHQUICKPOPUPMANAGER_H
#define RUWA_UI_WORKSPACE_CANVASBRUSHQUICKPOPUPMANAGER_H

class QPoint;

namespace ruwa::ui::workspace {

class CanvasPanel;

class CanvasBrushQuickPopupManager {
public:
    explicit CanvasBrushQuickPopupManager(CanvasPanel* panel);

    void ensureBrushQuickPopup();
    void showBrushQuickPopup(const QPoint& globalPos);
    void hideBrushQuickPopup();
    void refreshBrushQuickPopup();
    bool isBrushQuickPopupVisible() const;

private:
    CanvasPanel* m_panel = nullptr;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASBRUSHQUICKPOPUPMANAGER_H
