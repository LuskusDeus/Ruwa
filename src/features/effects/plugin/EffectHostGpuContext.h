// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTHOSTGPUCONTEXT_H
#define RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTHOSTGPUCONTEXT_H

// Host-side backing for the SDK's RuwaEffectGpuContext. Translates the plain-C
// host GPU command API (RuwaEffectGpuApi) into real OpenGL / GLShaderProgram
// calls, driven by the current GLLayerEffectRenderContext of an in-flight pass.
// One instance exists per (plugin effect x GL context), matching the SDK's
// "one pass instance per (typeId x context)" rule.

#include "features/effects/GLLayerEffectRenderRegistry.h" // GLLayerEffectRenderContext

#include <ruwa/effect/ruwa_effect_sdk.h>

#include <QOpenGLFunctions_4_5_Core>
#include <QString>

#include <cstdint>
#include <memory>
#include <unordered_set>
#include <vector>

namespace aether {
class GLShaderProgram;
}

namespace ruwa::core::effects::plugin {

class EffectHostGpuContext;

// A persistent (plugin-owned via create_texture) or transient (host-owned,
// per-invocation) texture handle. `owner` lets the host reject a handle used
// from a different pass / context in O(1).
struct HostTexture {
    GLuint id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    RuwaEffectTextureFormat format = RUWA_EFFECT_FORMAT_RGBA8;
    bool pluginOwned = false;
    EffectHostGpuContext* owner = nullptr;
};

struct HostSampler {
    GLuint id = 0;
    EffectHostGpuContext* owner = nullptr;
};

struct HostPipeline {
    std::unique_ptr<aether::GLShaderProgram> program;
    bool compute = false;
    EffectHostGpuContext* owner = nullptr;
};

class EffectHostGpuContext {
public:
    EffectHostGpuContext(QOpenGLFunctions_4_5_Core* gl, QString sharedVertexSource);
    ~EffectHostGpuContext();

    EffectHostGpuContext(const EffectHostGpuContext&) = delete;
    EffectHostGpuContext& operator=(const EffectHostGpuContext&) = delete;

    /// The opaque handle handed to plugin callbacks.
    RuwaEffectGpuContext handle() { return reinterpret_cast<RuwaEffectGpuContext>(this); }

    /// Bracket one render_pass call. Between these, [C] command calls are legal.
    void beginInvocation(const aether::GLLayerEffectRenderContext* renderCtx);
    void endInvocation();

    /// Wrap a renderer-owned GLuint as a transient handle valid for the current
    /// invocation (source / target / alpha / backdrop inputs). Recycled per call.
    RuwaEffectTexture wrapExternal(GLuint id, uint32_t width, uint32_t height);
    /// Map any handle this context owns back to its GL id (0 for null/foreign).
    GLuint resolve(RuwaEffectTexture texture) const;

    // --- RuwaEffectGpuApi implementation (invoked by the C trampolines) ---
    RuwaEffectTexture createTexture(
        uint32_t width, uint32_t height, RuwaEffectTextureFormat format);
    void destroyTexture(RuwaEffectTexture texture);
    uint32_t textureWidth(RuwaEffectTexture texture) const;
    uint32_t textureHeight(RuwaEffectTexture texture) const;

    RuwaEffectSampler createSampler(RuwaEffectSamplerFilter minFilter,
        RuwaEffectSamplerFilter magFilter, RuwaEffectSamplerWrap wrap);
    void destroySampler(RuwaEffectSampler sampler);

    RuwaEffectPipeline createGraphicsPipeline(const RuwaEffectShaderSource* fragment);
    void destroyGraphicsPipeline(RuwaEffectPipeline pipeline);
    RuwaEffectComputePipeline createComputePipeline(const RuwaEffectShaderSource* compute);
    void destroyComputePipeline(RuwaEffectComputePipeline pipeline);

    RuwaEffectTexture allocScratch(RuwaEffectTextureFormat format);

    void beginRenderPass(RuwaEffectTexture target);
    void bindGraphicsPipeline(RuwaEffectPipeline pipeline);
    void bindTexture(uint32_t unit, RuwaEffectTexture texture, RuwaEffectSampler sampler);
    void drawFullscreen();
    RuwaBool beginRoiScissor(int32_t expandX, int32_t expandY);
    void endRoiScissor(RuwaBool scissorActive);

    void bindComputePipeline(RuwaEffectComputePipeline pipeline);
    void bindImageTexture(uint32_t unit, RuwaEffectTexture texture, RuwaEffectImageAccess access,
        RuwaEffectTextureFormat format);
    void dispatchCompute(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ);
    void memoryBarrier(uint32_t barrierBits);

    void setUniformInt(const char* name, int32_t v);
    void setUniformInt2(const char* name, int32_t x, int32_t y);
    void setUniformFloat(const char* name, float v);
    void setUniformFloat2(const char* name, float x, float y);
    void setUniformFloat3(const char* name, float x, float y, float z);
    void setUniformFloat4(const char* name, float x, float y, float z, float w);
    void setUniformVec2Array(const char* name, const float* values, uint32_t count);

    void blit(RuwaEffectTexture src, RuwaEffectTexture dst);

private:
    HostTexture* asTexture(RuwaEffectTexture texture) const;
    HostSampler* asSampler(RuwaEffectSampler sampler) const;
    HostPipeline* asPipeline(void* pipeline) const;

    QOpenGLFunctions_4_5_Core* m_gl = nullptr;
    QString m_sharedVertexSource;
    const aether::GLLayerEffectRenderContext* m_renderCtx = nullptr;
    aether::GLShaderProgram* m_boundProgram = nullptr;

    std::unordered_set<HostTexture*> m_pluginTextures;
    std::unordered_set<HostSampler*> m_samplers;
    std::unordered_set<HostPipeline*> m_pipelines;
    // Recycled per invocation for wrapping the renderer's input/scratch GLuints.
    std::vector<std::unique_ptr<HostTexture>> m_invocationWrappers;
    std::size_t m_invocationCursor = 0;
};

/// The single static RuwaEffectGpuApi function table (shared by all contexts;
/// every function dispatches through its RuwaEffectGpuContext argument).
const RuwaEffectGpuApi* gpuApiTable();

} // namespace ruwa::core::effects::plugin

#endif // RUWA_FEATURES_EFFECTS_PLUGIN_EFFECTHOSTGPUCONTEXT_H
