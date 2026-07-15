// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_EFFECTS_LAYEREFFECTREGISTRY_H
#define RUWA_CORE_EFFECTS_LAYEREFFECTREGISTRY_H

#include "features/effects/LayerEffectTypes.h"

#include <QHash>
#include <QList>
#include <QSize>

#include <optional>

namespace ruwa::core::effects {

class LayerEffectRegistry {
public:
    static LayerEffectRegistry& instance();

    bool registerDescriptor(const LayerEffectDescriptor& descriptor);
    /// Removes a previously registered descriptor. Returns true if one was
    /// removed. Used to roll back a partially-committed plugin registration.
    bool unregisterDescriptor(const QString& typeId);
    bool contains(const QString& typeId) const;
    const LayerEffectDescriptor* descriptor(const QString& typeId) const;
    QList<LayerEffectDescriptor> descriptors() const;

    /// Ordered folder tree for the add-effect popup: registered effects grouped
    /// by their descriptor category, interleaved with planned placeholders so the
    /// picker shows the full taxonomy. Empty categories are omitted.
    QList<EffectCatalogCategory> catalog() const;

    /// canvasSize is nullopt for an infinite (unbounded) canvas, in which case
    /// params with a defaultBinding fall back to their literal defaultValue.
    LayerEffectState createState(
        const QString& typeId, const std::optional<QSize>& canvasSize = std::nullopt) const;
    LayerEffectState normalizeState(const LayerEffectState& state) const;

private:
    LayerEffectRegistry();

private:
    QHash<QString, LayerEffectDescriptor> m_descriptors;
};

class AutoLayerEffectRegistration {
public:
    explicit AutoLayerEffectRegistration(const LayerEffectDescriptor& descriptor);

    bool registered() const { return m_registered; }

private:
    bool m_registered = false;
};

} // namespace ruwa::core::effects

#endif // RUWA_CORE_EFFECTS_LAYEREFFECTREGISTRY_H
