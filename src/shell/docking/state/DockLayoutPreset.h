// SPDX-License-Identifier: MPL-2.0

// DockLayoutPreset.h
#ifndef RUWA_UI_DOCKING_STATE_DOCKLAYOUTPRESET_H
#define RUWA_UI_DOCKING_STATE_DOCKLAYOUTPRESET_H

#include "shell/docking/DockTypes.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointF>
#include <QString>
#include <QUuid>
#include <QVector>
#include <initializer_list>

namespace ruwa::ui::docking {

/**
 * @brief Describes a panel placement in a layout
 */
struct PanelPlacement {
    QString panelId; ///< Panel identifier (by title or id)
    DockPosition position; ///< Where to place
    QString relativeTo; ///< Panel ID to place relative to (empty = container)
    float sizeRatio = 0.5f; ///< Size ratio (0.0-1.0) relative to split

    /// Create placement at container edge
    static PanelPlacement atEdge(const QString& panelId, DockPosition pos, float ratio = 0.5f)
    {
        return { panelId, pos, QString(), ratio };
    }

    /// Create placement relative to another panel
    static PanelPlacement relativeToPanel(
        const QString& panelId, const QString& relativeToId, DockPosition pos, float ratio = 0.5f)
    {
        return { panelId, pos, relativeToId, ratio };
    }

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["panelId"] = panelId;
        obj["position"] = static_cast<int>(position);
        obj["relativeTo"] = relativeTo;
        obj["sizeRatio"] = sizeRatio;
        return obj;
    }

    static PanelPlacement fromJson(const QJsonObject& obj)
    {
        PanelPlacement p;
        p.panelId = obj["panelId"].toString();
        p.position = static_cast<DockPosition>(obj["position"].toInt(0));
        p.relativeTo = obj["relativeTo"].toString();
        p.sizeRatio = static_cast<float>(obj["sizeRatio"].toDouble(0.5));
        return p;
    }
};

/**
 * @brief Defines a complete dock layout preset
 *
 * Presets may use coarse @a placements or an exact @a layoutTree snapshot.
 * User presets can additionally capture live workspace state via @a dockState.
 */
struct DockLayoutPreset {
    // === Identification ===
    QUuid id; ///< Unique identifier
    QString name; ///< Display name
    QString description; ///< Short UI description
    bool isBuiltIn = true; ///< Built-in vs user-created

    // === Layout Definition ===
    QVector<PanelPlacement> placements; ///< Built-in: apply in order
    QJsonObject layoutTree; ///< User: new layout system snapshot
    QJsonArray floating; ///< User: floating windows (serializer format)
    QByteArray dockState; ///< Full serialized workspace dock state for exact restore
    QPointF brushOverlayPosNormalized = QPointF(-1.0, -1.0);
    QPointF toolStateOverlayPosNormalized = QPointF(-1.0, -1.0);
    QPointF stylusJoystickPosNormalized = QPointF(-1.0, -1.0);
    bool hasBrushOverlayPos = false;
    bool hasToolStateOverlayPos = false;
    bool hasStylusJoystickPos = false;
    bool stylusJoystickAbovePanel = true;
    bool joystickVisible = true;
    bool brushControlVisible = true;
    bool toolStateOverlayVisible = true;

    bool usesLayoutTree() const { return !layoutTree.isEmpty(); }
    bool hasSerializedDockState() const { return !dockState.isEmpty(); }

    // === Serialization ===

    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["id"] = id.toString();
        obj["name"] = name;
        obj["description"] = description;
        obj["isBuiltIn"] = isBuiltIn;

        QJsonArray placementsArray;
        for (const auto& p : placements) {
            QJsonObject pObj;
            pObj["panelId"] = p.panelId;
            pObj["position"] = static_cast<int>(p.position);
            pObj["relativeTo"] = p.relativeTo;
            pObj["sizeRatio"] = p.sizeRatio;
            placementsArray.append(pObj);
        }
        obj["placements"] = placementsArray;

        if (!layoutTree.isEmpty()) {
            obj["layoutTree"] = layoutTree;
        }
        if (!floating.isEmpty()) {
            obj["floating"] = floating;
        }
        if (!dockState.isEmpty()) {
            obj["dockState"] = QString::fromLatin1(dockState.toBase64(QByteArray::Base64Encoding));
        }
        if (hasBrushOverlayPos) {
            obj["brushOverlayPosNormalizedX"] = brushOverlayPosNormalized.x();
            obj["brushOverlayPosNormalizedY"] = brushOverlayPosNormalized.y();
        }
        if (hasToolStateOverlayPos) {
            obj["toolStateOverlayPosNormalizedX"] = toolStateOverlayPosNormalized.x();
            obj["toolStateOverlayPosNormalizedY"] = toolStateOverlayPosNormalized.y();
        }
        if (hasStylusJoystickPos) {
            obj["stylusJoystickPosNormalizedX"] = stylusJoystickPosNormalized.x();
            obj["stylusJoystickPosNormalizedY"] = stylusJoystickPosNormalized.y();
        }
        obj["stylusJoystickAbovePanel"] = stylusJoystickAbovePanel;
        obj["joystickVisible"] = joystickVisible;
        obj["brushControlVisible"] = brushControlVisible;
        obj["toolStateOverlayVisible"] = toolStateOverlayVisible;

        return obj;
    }

    static DockLayoutPreset fromJson(const QJsonObject& obj)
    {
        DockLayoutPreset preset;
        preset.id = QUuid(obj["id"].toString());
        preset.name = obj["name"].toString();
        preset.description = obj["description"].toString();
        preset.isBuiltIn = obj["isBuiltIn"].toBool();

        QJsonArray placementsArray = obj["placements"].toArray();
        for (const auto& val : placementsArray) {
            QJsonObject pObj = val.toObject();
            PanelPlacement p;
            p.panelId = pObj["panelId"].toString();
            p.position = static_cast<DockPosition>(pObj["position"].toInt());
            p.relativeTo = pObj["relativeTo"].toString();
            p.sizeRatio = static_cast<float>(pObj["sizeRatio"].toDouble());
            preset.placements.append(p);
        }

        preset.layoutTree = obj["layoutTree"].toObject();
        preset.floating = obj["floating"].toArray();
        preset.dockState = QByteArray::fromBase64(obj["dockState"].toString().toLatin1());

        if (obj.contains("brushOverlayPosNormalizedX")
            && obj.contains("brushOverlayPosNormalizedY")) {
            preset.brushOverlayPosNormalized
                = QPointF(obj["brushOverlayPosNormalizedX"].toDouble(-1.0),
                    obj["brushOverlayPosNormalizedY"].toDouble(-1.0));
            preset.hasBrushOverlayPos = preset.brushOverlayPosNormalized.x() >= 0.0
                && preset.brushOverlayPosNormalized.y() >= 0.0
                && preset.brushOverlayPosNormalized.x() <= 1.0
                && preset.brushOverlayPosNormalized.y() <= 1.0;
        }
        if (obj.contains("toolStateOverlayPosNormalizedX")
            && obj.contains("toolStateOverlayPosNormalizedY")) {
            preset.toolStateOverlayPosNormalized
                = QPointF(obj["toolStateOverlayPosNormalizedX"].toDouble(-1.0),
                    obj["toolStateOverlayPosNormalizedY"].toDouble(-1.0));
            preset.hasToolStateOverlayPos = preset.toolStateOverlayPosNormalized.x() >= 0.0
                && preset.toolStateOverlayPosNormalized.y() >= 0.0
                && preset.toolStateOverlayPosNormalized.x() <= 1.0
                && preset.toolStateOverlayPosNormalized.y() <= 1.0;
        }
        if (obj.contains("stylusJoystickPosNormalizedX")
            && obj.contains("stylusJoystickPosNormalizedY")) {
            preset.stylusJoystickPosNormalized
                = QPointF(obj["stylusJoystickPosNormalizedX"].toDouble(-1.0),
                    obj["stylusJoystickPosNormalizedY"].toDouble(-1.0));
            preset.hasStylusJoystickPos = preset.stylusJoystickPosNormalized.x() >= 0.0
                && preset.stylusJoystickPosNormalized.y() >= 0.0
                && preset.stylusJoystickPosNormalized.x() <= 1.0
                && preset.stylusJoystickPosNormalized.y() <= 1.0;
        }
        preset.stylusJoystickAbovePanel = obj["stylusJoystickAbovePanel"].toBool(true);
        preset.joystickVisible = obj["joystickVisible"].toBool(true);
        preset.brushControlVisible = obj["brushControlVisible"].toBool(true);
        preset.toolStateOverlayVisible = obj["toolStateOverlayVisible"].toBool(true);

        return preset;
    }

    // === Factory Methods for Built-in Presets ===

    static QJsonObject leafNode(const QString& panelKey, const QString& panelTitle = QString(),
        const QString& anchor = QStringLiteral("none"), int anchoredSize = 0)
    {
        QJsonObject obj;
        obj["type"] = QStringLiteral("leaf");
        obj["panelId"] = panelKey;
        if (!panelTitle.isEmpty()) {
            obj["panelTitle"] = panelTitle;
        }
        obj["anchor"] = anchor;
        obj["anchoredSize"] = anchoredSize;
        return obj;
    }

    static QJsonObject splitNode(const QString& direction,
        std::initializer_list<QJsonObject> children, std::initializer_list<int> sizes,
        const QString& anchor = QStringLiteral("none"), int anchoredSize = 0)
    {
        QJsonObject obj;
        obj["type"] = QStringLiteral("split");
        obj["direction"] = direction;
        obj["anchor"] = anchor;
        obj["anchoredSize"] = anchoredSize;

        QJsonArray childrenArray;
        for (const QJsonObject& child : children) {
            childrenArray.append(child);
        }
        obj["children"] = childrenArray;

        QJsonArray sizesArray;
        for (int size : sizes) {
            sizesArray.append(size);
        }
        obj["sizes"] = sizesArray;
        return obj;
    }

    static DockLayoutPreset presetFromResourceJson(const QString& resourcePath,
        const QString& fallbackId, const QString& displayName, bool builtIn)
    {
        QFile file(resourcePath);
        if (!file.open(QIODevice::ReadOnly)) {
            DockLayoutPreset preset;
            preset.id = QUuid(fallbackId);
            preset.name = displayName;
            preset.isBuiltIn = builtIn;
            return preset;
        }

        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);

        DockLayoutPreset preset;
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            preset = fromJson(doc.object());
        }

        if (!fallbackId.isEmpty()) {
            preset.id = QUuid(fallbackId);
        }
        if (!displayName.trimmed().isEmpty()) {
            preset.name = displayName;
        }
        if (builtIn) {
            preset.description.clear();
        }
        preset.isBuiltIn = builtIn;
        return preset;
    }

    static DockLayoutPreset defaultWorkspace()
    {
        return presetFromResourceJson(QStringLiteral(":/layouts/Base.json"),
            QStringLiteral("{11111111-1111-4000-8000-000000000001}"),
            QCoreApplication::translate("DockLayoutPreset", "Base"), true);
    }

    static QVector<DockLayoutPreset> builtInPresets() { return { defaultWorkspace() }; }
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_STATE_DOCKLAYOUTPRESET_H
