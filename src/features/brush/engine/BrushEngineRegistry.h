// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_CORE_BRUSHENGINE_BRUSHENGINEREGISTRY_H
#define RUWA_CORE_BRUSHENGINE_BRUSHENGINEREGISTRY_H

#include "features/brush/engine/BrushModule.h"

#include <QHash>
#include <QVector>
#include <memory>
#include <vector>

namespace ruwa::core::brushes {

class BrushEngineRegistry {
public:
    static BrushEngineRegistry& instance();

    const IBrushEngineModule* module(const BrushEngineId& engineId) const;
    const IBrushEngineModule* moduleOrPixelFallback(const BrushEngineId& engineId) const;
    const IBrushEngineModule* pixelModule() const;
    QVector<const IBrushEngineModule*> modules() const;

private:
    BrushEngineRegistry();
    void registerBuiltIn(std::unique_ptr<IBrushEngineModule> module);

private:
    std::vector<std::unique_ptr<IBrushEngineModule>> m_modules;
    QHash<BrushEngineId, const IBrushEngineModule*> m_lookup;
};

} // namespace ruwa::core::brushes

#endif // RUWA_CORE_BRUSHENGINE_BRUSHENGINEREGISTRY_H
