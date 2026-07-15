// SPDX-License-Identifier: MPL-2.0

#include "features/effects/plugin/EffectPluginPass.h"

#include "features/effects/plugin/EffectAbiAdapter.h"
#include "features/effects/plugin/EffectHostGpuContext.h"
#include "shared/rendering/GLStateGuard.h"

#include <QFile>

#include <vector>

namespace ruwa::core::effects::plugin {

namespace {

RuwaEffectRegionFrame mapRegion(const ruwa::core::effects::EffectRegionFrame& r)
{
    RuwaEffectRegionFrame out {};
    out.origin_x = r.originX;
    out.origin_y = r.originY;
    out.document_px_per_texel = r.documentPxPerTexel;
    out.valid = r.valid ? RUWA_TRUE : RUWA_FALSE;
    out.use_affine = r.useAffine ? RUWA_TRUE : RUWA_FALSE;
    out.basis_xx = r.basisXx;
    out.basis_xy = r.basisXy;
    out.basis_yx = r.basisYx;
    out.basis_yy = r.basisYy;
    return out;
}

QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

} // namespace

PluginGLLayerEffectPass::PluginGLLayerEffectPass(const RuwaEffectDescriptor* effect)
    : m_effect(effect)
    , m_typeId(effect && effect->type_id ? QString::fromUtf8(effect->type_id) : QString())
{
}

PluginGLLayerEffectPass::~PluginGLLayerEffectPass()
{
    // Runs on the render thread during renderer shutdown (context still current),
    // so the plugin can release its GL resources here.
    if (m_instance && m_effect && m_effect->destroy_pass && m_gpu) {
        try {
            m_effect->destroy_pass(m_instance, m_gpu->handle());
        } catch (...) {
            qWarning("Ruwa effect plugin: destroy_pass threw for '%s'", qPrintable(m_typeId));
        }
    }
    m_instance = nullptr;
    m_gpu.reset();
}

aether::Result<void> PluginGLLayerEffectPass::initialize(
    QOpenGLFunctions_4_5_Core* gl, const QString& shaderDir)
{
    m_gl = gl;
    const QString sharedVertex = readTextFile(shaderDir + QStringLiteral("/composite.vert.glsl"));
    m_gpu = std::make_unique<EffectHostGpuContext>(gl, sharedVertex);

    if (m_effect && m_effect->create_pass) {
        try {
            m_instance = m_effect->create_pass(m_effect->user_data, m_gpu->handle());
        } catch (...) {
            qWarning("Ruwa effect plugin: create_pass threw for '%s'", qPrintable(m_typeId));
        }
    }
    if (!m_instance) {
        // Isolation: a plugin that fails to create its pass is disabled (its
        // renders pass through) but must NOT abort the whole renderer init.
        m_failed = true;
        qWarning("Ruwa effect plugin: create_pass failed for '%s'", qPrintable(m_typeId));
    }
    return aether::Result<void>::ok();
}

GLuint PluginGLLayerEffectPass::render(const aether::GLLayerEffectRenderContext& context,
    const ruwa::core::effects::LayerEffectState& effectState, GLuint sourceTexture,
    GLuint targetTexture)
{
    if (m_failed || !m_instance || !m_gpu || !m_effect || !m_effect->render_pass || !context.gl
        || !sourceTexture) {
        return sourceTexture;
    }

    m_gpu->beginInvocation(&context);

    std::vector<const char*> keys;
    std::vector<RuwaEffectParamValue> values = makeParamValues(m_effect, effectState.params, keys);

    RuwaEffectPassInput input {};
    input.struct_size = sizeof(RuwaEffectPassInput);
    input.output_width = context.outputWidth;
    input.output_height = context.outputHeight;
    input.evaluation_space
        = context.evaluationSpace == ruwa::core::effects::EffectEvaluationSpace::ViewportScreen
        ? RUWA_EFFECT_SPACE_VIEWPORT_SCREEN
        : RUWA_EFFECT_SPACE_DOCUMENT_TILE;
    input.space_scale = context.spaceScale;
    input.region = mapRegion(context.region);
    input.whole_layer_source = context.wholeLayerSource ? RUWA_TRUE : RUWA_FALSE;

    const uint32_t w = context.outputWidth;
    const uint32_t h = context.outputHeight;
    input.source_texture = m_gpu->wrapExternal(sourceTexture, w, h);
    input.target_texture = m_gpu->wrapExternal(targetTexture, w, h);
    input.layer_alpha_texture = context.source.layerAlphaTexture
        ? m_gpu->wrapExternal(context.source.layerAlphaTexture, w, h)
        : nullptr;
    input.backdrop_texture
        = context.backdrop.texture ? m_gpu->wrapExternal(context.backdrop.texture, w, h) : nullptr;

    input.roi_x = context.roiX;
    input.roi_y = context.roiY;
    input.roi_width = context.roiWidth;
    input.roi_height = context.roiHeight;

    input.param_count = static_cast<uint32_t>(keys.size());
    input.param_keys = keys.data();
    input.param_values = values.data();
    input.state_version = effectState.version;

    GLuint out = 0;
    {
        // Restore the framebuffer / viewport / blend the compositor relies on,
        // whatever arbitrary GL state the plugin leaves behind.
        GLFboViewportBlendGuard guard(m_gl);
        try {
            RuwaEffectTexture result = m_effect->render_pass(m_instance, m_gpu->handle(), &input);
            out = m_gpu->resolve(result);
        } catch (...) {
            qWarning("Ruwa effect plugin: render_pass threw for '%s'", qPrintable(m_typeId));
            out = sourceTexture;
        }
        // Defensive: neutralise state a plugin might have left dangling.
        m_gl->glDisable(GL_SCISSOR_TEST);
        m_gl->glBindSampler(0, 0);
        m_gl->glUseProgram(0);
        m_gl->glActiveTexture(GL_TEXTURE0);
    }

    m_gpu->endInvocation();
    return out ? out : sourceTexture;
}

} // namespace ruwa::core::effects::plugin
