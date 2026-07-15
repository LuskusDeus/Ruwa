// SPDX-License-Identifier: MPL-2.0

#include "features/effects/GLLayerEffectRenderRegistry.h"

#include <utility>

namespace aether {

GLLayerEffectRenderRegistry& GLLayerEffectRenderRegistry::instance()
{
    static GLLayerEffectRenderRegistry registry;
    return registry;
}

bool GLLayerEffectRenderRegistry::registerFactory(const QString& typeId, Factory factory)
{
    if (typeId.isEmpty() || !factory || m_factories.contains(typeId)) {
        return false;
    }

    m_factories.insert(typeId, std::move(factory));
    return true;
}

bool GLLayerEffectRenderRegistry::unregisterFactory(const QString& typeId)
{
    return m_factories.remove(typeId) > 0;
}

bool GLLayerEffectRenderRegistry::contains(const QString& typeId) const
{
    return m_factories.contains(typeId);
}

QList<QString> GLLayerEffectRenderRegistry::typeIds() const
{
    return m_factories.keys();
}

std::unique_ptr<IGLLayerEffectPass> GLLayerEffectRenderRegistry::createPass(
    const QString& typeId) const
{
    const auto it = m_factories.constFind(typeId);
    return it == m_factories.cend() ? nullptr : it.value()();
}

AutoGLLayerEffectPassRegistration::AutoGLLayerEffectPassRegistration(
    const QString& typeId, GLLayerEffectRenderRegistry::Factory factory)
    : m_registered(
          GLLayerEffectRenderRegistry::instance().registerFactory(typeId, std::move(factory)))
{
}

} // namespace aether
