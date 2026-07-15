// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTPLUGINPASS_H
#define RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTPLUGINPASS_H

// Wraps one plugin effect as an IGLLayerEffectPass so the existing
// GLLayerEffectRenderer drives plugin effects through the same code path as the
// built-in ones. Owns the per-context host GPU backing and the plugin's opaque
// pass instance.

#include "features/effects/GLLayerEffectRenderRegistry.h"

#include <ruwa/effect/ruwa_effect_sdk.h>

#include <QOpenGLFunctions_4_5_Core>
#include <QString>

#include <memory>

namespace ruwa::core::effects::plugin {

class EffectHostGpuContext;

class PluginGLLayerEffectPass final : public aether::IGLLayerEffectPass {
public:
    /// `effect` is the plugin's descriptor; it must outlive this pass (the
    /// plugin DLL stays loaded while any instance exists).
    explicit PluginGLLayerEffectPass(const RuwaEffectDescriptor* effect);
    ~PluginGLLayerEffectPass() override;

    QString typeId() const override { return m_typeId; }

    aether::Result<void> initialize(
        QOpenGLFunctions_4_5_Core* gl, const QString& shaderDir) override;

    GLuint render(const aether::GLLayerEffectRenderContext& context,
        const ruwa::core::effects::LayerEffectState& effectState, GLuint sourceTexture,
        GLuint targetTexture) override;

private:
    const RuwaEffectDescriptor* m_effect = nullptr;
    QString m_typeId;
    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    std::unique_ptr<EffectHostGpuContext> m_gpu;
    void* m_instance = nullptr;
    bool m_failed = false;
};

} // namespace ruwa::core::effects::plugin

#endif // RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTPLUGINPASS_H
