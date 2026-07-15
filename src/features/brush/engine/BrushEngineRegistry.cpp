// SPDX-License-Identifier: MPL-2.0

#include "features/brush/engine/BrushEngineRegistry.h"

#include "features/brush/engine/PixelBrushModule.h"

namespace ruwa::core::brushes {

BrushEngineRegistry& BrushEngineRegistry::instance()
{
    static BrushEngineRegistry registry;
    return registry;
}

const IBrushEngineModule* BrushEngineRegistry::module(const BrushEngineId& engineId) const
{
    const auto it = m_lookup.constFind(engineId);
    return it == m_lookup.cend() ? nullptr : it.value();
}

const IBrushEngineModule* BrushEngineRegistry::moduleOrPixelFallback(
    const BrushEngineId& engineId) const
{
    if (const auto* found = module(engineId); found) {
        return found;
    }
    return pixelModule();
}

const IBrushEngineModule* BrushEngineRegistry::pixelModule() const
{
    return module(QLatin1String(kPixelBrushEngineId));
}

QVector<const IBrushEngineModule*> BrushEngineRegistry::modules() const
{
    QVector<const IBrushEngineModule*> out;
    out.reserve(m_modules.size());
    for (const auto& modulePtr : m_modules) {
        out.append(modulePtr.get());
    }
    return out;
}

BrushEngineRegistry::BrushEngineRegistry()
{
    registerBuiltIn(std::make_unique<PixelBrushModule>());
}

void BrushEngineRegistry::registerBuiltIn(std::unique_ptr<IBrushEngineModule> module)
{
    if (!module) {
        return;
    }

    const BrushEngineDescriptor descriptor = module->descriptor();
    if (descriptor.id.isEmpty() || m_lookup.contains(descriptor.id)) {
        return;
    }

    const IBrushEngineModule* rawPtr = module.get();
    m_modules.push_back(std::move(module));
    m_lookup.insert(descriptor.id, rawPtr);
}

} // namespace ruwa::core::brushes
