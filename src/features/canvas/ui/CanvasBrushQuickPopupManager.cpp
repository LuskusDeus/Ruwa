// SPDX-License-Identifier: MPL-2.0

#include "CanvasBrushQuickPopupManager.h"

#include "CanvasBrushQuickPopup.h"
#include "CanvasCursorManager.h"
#include "CanvasPanel.h"

#include "features/brush/manager/BrushManager.h"
#include "features/brush/ui/BrushSizeCurve.h"

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QtMath>

#include <optional>

namespace ruwa::ui::workspace {

namespace {

std::optional<ruwa::ui::widgets::CanvasBrushQuickPopup::Model> buildPopupModel(
    const CanvasPanel* panel)
{
    if (!panel) {
        return std::nullopt;
    }

    auto& brushManager = ruwa::core::brushes::BrushManager::instance();
    const QString currentBrushId = panel->selectedBrushIdForCurrentContext();
    if (currentBrushId.isEmpty()) {
        return std::nullopt;
    }

    const auto currentBrush = brushManager.brushData(currentBrushId);
    if (!currentBrush.has_value()) {
        return std::nullopt;
    }

    ruwa::ui::widgets::CanvasBrushQuickPopup::Model model;
    model.currentBrush.id = currentBrush->id;
    model.currentBrush.name = currentBrush->name;
    model.currentBrush.settings = currentBrush->settings;
    for (const auto& preset : brushManager.presets()) {
        if (preset.id == currentBrush->presetId) {
            model.currentPackName = preset.name;
            break;
        }
    }

    QColor previewColor = panel->currentBrushColor();
    previewColor.setAlpha(255);
    model.previewColor = previewColor;
    model.previewOpacityNorm = panel->brushOpacityNormalized();
    model.brushSizeNormalized = panel->brushSizeNormalized();
    model.brushOpacityNormalized = panel->brushOpacityNormalized();
    model.previewSizeNorm = ruwa::ui::widgets::brushPreviewSizeNormalizedForCanvasMode(
        panel->brushSizeNormalized(), panel->canvasSize().width(), panel->canvasSize().height(),
        panel->hasFiniteDocumentBounds());

    const float radiusPx = ruwa::ui::widgets::brushRadiusFromNormalizedSizeForCanvasMode(
        panel->brushSizeNormalized(), panel->canvasSize().width(), panel->canvasSize().height(),
        panel->hasFiniteDocumentBounds());
    model.sizeText = ruwa::ui::widgets::CanvasBrushQuickPopup::tr("%1 px").arg(qRound(radiusPx));
    model.opacityText = ruwa::ui::widgets::CanvasBrushQuickPopup::tr("%1%").arg(
        qRound(model.brushOpacityNormalized * 100.0));

    QHash<QString, QString> presetNameById;
    for (const auto& preset : brushManager.presets()) {
        presetNameById.insert(preset.id, preset.name);
    }

    const QVector<ruwa::core::brushes::BrushData> recentBrushes = brushManager.recentBrushes(20);
    model.recentBrushes.reserve(10);
    for (const auto& brush : recentBrushes) {
        if (brush.id == currentBrushId) {
            continue;
        }

        ruwa::ui::widgets::CanvasBrushQuickPopup::BrushEntry entry;
        entry.id = brush.id;
        entry.name = brush.name;
        entry.packName = presetNameById.value(brush.presetId);
        entry.settings = brush.settings;
        model.recentBrushes.append(entry);
        if (model.recentBrushes.size() >= 10) {
            break;
        }
    }

    return model;
}

} // namespace

CanvasBrushQuickPopupManager::CanvasBrushQuickPopupManager(CanvasPanel* panel)
    : m_panel(panel)
{
}

void CanvasBrushQuickPopupManager::ensureBrushQuickPopup()
{
    if (!m_panel || m_panel->m_brushQuickPopup || !m_panel->m_contentWidget) {
        return;
    }

    m_panel->m_brushQuickPopup
        = new ruwa::ui::widgets::CanvasBrushQuickPopup(m_panel->m_contentWidget);
    m_panel->m_brushQuickPopup->hide();
    m_panel->m_brushQuickPopup->raise();

    if (m_panel->m_cursorManager) {
        m_panel->m_cursorManager->addCursorExclusionWidget(m_panel->m_brushQuickPopup);
    }

    QObject::connect(m_panel->m_brushQuickPopup,
        &ruwa::ui::widgets::CanvasBrushQuickPopup::brushSizeChanged, m_panel,
        [this](qreal sizeNormalized) {
            if (m_panel) {
                m_panel->setBrushSizeNormalized(sizeNormalized);
            }
        });

    QObject::connect(m_panel->m_brushQuickPopup,
        &ruwa::ui::widgets::CanvasBrushQuickPopup::brushOpacityChanged, m_panel,
        [this](qreal opacityNormalized) {
            if (m_panel) {
                m_panel->setBrushOpacityNormalized(opacityNormalized);
            }
        });

    QObject::connect(m_panel->m_brushQuickPopup,
        &ruwa::ui::widgets::CanvasBrushQuickPopup::recentBrushSelected, m_panel,
        [this](const QString& brushId) {
            if (!m_panel) {
                return;
            }
            m_panel->selectBrushForCurrentContext(brushId);
            hideBrushQuickPopup();
        });
}

void CanvasBrushQuickPopupManager::showBrushQuickPopup(const QPoint& globalPos)
{
    ensureBrushQuickPopup();
    if (!m_panel || !m_panel->m_brushQuickPopup || !m_panel->m_contentWidget) {
        return;
    }

    const auto model = buildPopupModel(m_panel);
    if (!model.has_value()) {
        hideBrushQuickPopup();
        return;
    }

    m_panel->m_brushQuickPopup->setModel(*model);
    const QPoint localPos = m_panel->m_contentWidget->mapFromGlobal(globalPos);
    m_panel->m_brushQuickPopup->showAt(localPos + QPoint(12, 12));
}

void CanvasBrushQuickPopupManager::hideBrushQuickPopup()
{
    if (m_panel && m_panel->m_brushQuickPopup) {
        m_panel->m_brushQuickPopup->hidePopup();
    }
}

void CanvasBrushQuickPopupManager::refreshBrushQuickPopup()
{
    if (!m_panel || !m_panel->m_brushQuickPopup || !m_panel->m_brushQuickPopup->isPopupVisible()) {
        return;
    }

    const auto model = buildPopupModel(m_panel);
    if (!model.has_value()) {
        hideBrushQuickPopup();
        return;
    }

    m_panel->m_brushQuickPopup->setModel(*model);
}

bool CanvasBrushQuickPopupManager::isBrushQuickPopupVisible() const
{
    return m_panel && m_panel->m_brushQuickPopup && m_panel->m_brushQuickPopup->isPopupVisible();
}

} // namespace ruwa::ui::workspace
