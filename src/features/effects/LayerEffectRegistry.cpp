// SPDX-License-Identifier: MPL-2.0

#include "features/effects/LayerEffectRegistry.h"

#include <QMap>

#include <algorithm>

namespace ruwa::core::effects {

namespace {

/// Display order of picker folders. Categories not listed here are appended
/// afterwards in first-seen order; uncategorized effects land in "Other".
const QStringList& categoryOrder()
{
    static const QStringList kOrder = {
        QStringLiteral("Blur"),
        QStringLiteral("Sharpen"),
        QStringLiteral("Color Adjust"),
        QStringLiteral("Distort"),
        QStringLiteral("Stylize"),
        QStringLiteral("Texture"),
    };
    return kOrder;
}

/// Planned-but-not-yet-built effects, shown greyed-out in the picker so the
/// taxonomy reads as a complete map. These carry no typeId and cannot be added.
struct PlannedEntry {
    const char* category;
    const char* displayName;
};

const QList<PlannedEntry>& plannedEntries()
{
    static const QList<PlannedEntry> kPlanned = {};
    return kPlanned;
}

} // namespace

LayerEffectRegistry& LayerEffectRegistry::instance()
{
    static LayerEffectRegistry registry;
    return registry;
}

bool LayerEffectRegistry::registerDescriptor(const LayerEffectDescriptor& descriptor)
{
    if (descriptor.typeId.isEmpty() || m_descriptors.contains(descriptor.typeId)) {
        return false;
    }

    m_descriptors.insert(descriptor.typeId, descriptor);
    return true;
}

bool LayerEffectRegistry::unregisterDescriptor(const QString& typeId)
{
    return m_descriptors.remove(typeId) > 0;
}

bool LayerEffectRegistry::contains(const QString& typeId) const
{
    return m_descriptors.contains(typeId);
}

const LayerEffectDescriptor* LayerEffectRegistry::descriptor(const QString& typeId) const
{
    const auto it = m_descriptors.constFind(typeId);
    return it == m_descriptors.cend() ? nullptr : &it.value();
}

QList<LayerEffectDescriptor> LayerEffectRegistry::descriptors() const
{
    QList<LayerEffectDescriptor> out;
    out.reserve(m_descriptors.size());
    for (auto it = m_descriptors.cbegin(); it != m_descriptors.cend(); ++it) {
        out.append(it.value());
    }
    return out;
}

QList<EffectCatalogCategory> LayerEffectRegistry::catalog() const
{
    const QString kOther = QStringLiteral("Other");

    // Preserve display order: known categories first, then any straggler
    // category encountered on a descriptor, then "Other" last.
    QStringList order = categoryOrder();
    QMap<QString, EffectCatalogCategory> byName;
    for (const QString& name : order) {
        byName.insert(name, EffectCatalogCategory { name, {} });
    }

    auto ensureCategory = [&](const QString& rawName) -> EffectCatalogCategory& {
        const QString name = rawName.isEmpty() ? kOther : rawName;
        if (!byName.contains(name)) {
            byName.insert(name, EffectCatalogCategory { name, {} });
            order.append(name);
        }
        return byName[name];
    };

    // Registered (usable) effects, sorted by display name within each category.
    QList<LayerEffectDescriptor> registered = descriptors();
    std::sort(registered.begin(), registered.end(),
        [](const LayerEffectDescriptor& a, const LayerEffectDescriptor& b) {
            return a.displayName.localeAwareCompare(b.displayName) < 0;
        });
    for (const LayerEffectDescriptor& d : registered) {
        if (d.typeId.isEmpty()) {
            continue;
        }
        EffectCatalogEntry entry;
        entry.typeId = d.typeId;
        entry.displayName = d.displayName.isEmpty() ? d.typeId : d.displayName;
        entry.implemented = true;
        ensureCategory(d.category).entries.append(entry);
    }

    // Planned placeholders after the real ones in each folder.
    for (const PlannedEntry& p : plannedEntries()) {
        EffectCatalogEntry entry;
        entry.displayName = QString::fromLatin1(p.displayName);
        entry.implemented = false;
        ensureCategory(QString::fromLatin1(p.category)).entries.append(entry);
    }

    QList<EffectCatalogCategory> out;
    if (order.contains(kOther)) {
        order.removeAll(kOther);
        order.append(kOther); // keep "Other" trailing
    }
    for (const QString& name : order) {
        const EffectCatalogCategory& cat = byName.value(name);
        if (!cat.entries.isEmpty()) {
            out.append(cat);
        }
    }
    return out;
}

LayerEffectState LayerEffectRegistry::createState(
    const QString& typeId, const std::optional<QSize>& canvasSize) const
{
    LayerEffectState state;
    state.typeId = typeId;

    if (const auto* effectDescriptor = descriptor(typeId)) {
        state.version = effectDescriptor->version;
        for (const EffectParamDefinition& param : effectDescriptor->params) {
            if (param.key.isEmpty()) {
                continue;
            }
            QVariant value = param.defaultValue;
            if (canvasSize.has_value()) {
                if (param.defaultBinding == EffectParamDefaultBinding::CanvasWidth) {
                    value = canvasSize->width();
                } else if (param.defaultBinding == EffectParamDefaultBinding::CanvasHeight) {
                    value = canvasSize->height();
                } else if (param.defaultBinding == EffectParamDefaultBinding::CanvasHalfWidth) {
                    value = canvasSize->width() / 2.0;
                } else if (param.defaultBinding == EffectParamDefaultBinding::CanvasHalfHeight) {
                    value = canvasSize->height() / 2.0;
                }
            }
            state.params.insert(param.key, value);
        }
    }

    return state;
}

LayerEffectState LayerEffectRegistry::normalizeState(const LayerEffectState& state) const
{
    LayerEffectState normalized = state;
    if (normalized.instanceId.isNull()) {
        normalized.instanceId = QUuid::createUuid();
    }

    const auto* effectDescriptor = descriptor(normalized.typeId);
    if (!effectDescriptor) {
        return normalized;
    }

    normalized.version = effectDescriptor->version;
    for (const EffectParamDefinition& param : effectDescriptor->params) {
        if (!param.key.isEmpty() && !normalized.params.contains(param.key)) {
            normalized.params.insert(param.key, param.defaultValue);
        }
    }
    return normalized;
}

AutoLayerEffectRegistration::AutoLayerEffectRegistration(const LayerEffectDescriptor& descriptor)
    : m_registered(LayerEffectRegistry::instance().registerDescriptor(descriptor))
{
}

LayerEffectRegistry::LayerEffectRegistry() = default;

} // namespace ruwa::core::effects
