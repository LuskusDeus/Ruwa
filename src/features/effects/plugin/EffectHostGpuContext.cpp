// SPDX-License-Identifier: MPL-2.0

#include "features/effects/plugin/EffectHostGpuContext.h"

#include "shared/rendering/GLShaderProgram.h"
#include "shared/rendering/GLTextureFactory.h"

#include <QByteArray>
#include <QDebug>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace ruwa::core::effects::plugin {

namespace {

constexpr std::size_t kMinShaderSourceStruct = offsetof(RuwaEffectShaderSource, cache_key)
    + sizeof(((RuwaEffectShaderSource*) nullptr)->cache_key);

bool validTextureFormat(RuwaEffectTextureFormat format)
{
    return format >= RUWA_EFFECT_FORMAT_RGBA8 && format <= RUWA_EFFECT_FORMAT_RGBA32F;
}

bool validSamplerFilter(RuwaEffectSamplerFilter filter)
{
    return filter == RUWA_EFFECT_FILTER_NEAREST || filter == RUWA_EFFECT_FILTER_LINEAR;
}

bool validSamplerWrap(RuwaEffectSamplerWrap wrap)
{
    return wrap >= RUWA_EFFECT_WRAP_CLAMP_TO_EDGE && wrap <= RUWA_EFFECT_WRAP_MIRRORED;
}

bool validImageAccess(RuwaEffectImageAccess access)
{
    return access >= RUWA_EFFECT_IMAGE_READ_ONLY && access <= RUWA_EFFECT_IMAGE_READ_WRITE;
}

bool validShaderSource(const RuwaEffectShaderSource* source, RuwaEffectShaderStage expectedStage)
{
    return source && source->struct_size >= kMinShaderSourceStruct && source->stage == expectedStage
        && source->source
        && source->source_length <= static_cast<uint32_t>(std::numeric_limits<int>::max());
}

GLenum internalFormatFor(RuwaEffectTextureFormat format)
{
    switch (format) {
    case RUWA_EFFECT_FORMAT_RGBA16F:
        return GL_RGBA16F;
    case RUWA_EFFECT_FORMAT_RGBA32F:
        return GL_RGBA32F;
    case RUWA_EFFECT_FORMAT_RGBA8:
    default:
        return GL_RGBA8;
    }
}

GLenum pixelTypeFor(RuwaEffectTextureFormat format)
{
    switch (format) {
    case RUWA_EFFECT_FORMAT_RGBA16F:
        return GL_HALF_FLOAT;
    case RUWA_EFFECT_FORMAT_RGBA32F:
        return GL_FLOAT;
    case RUWA_EFFECT_FORMAT_RGBA8:
    default:
        return GL_UNSIGNED_BYTE;
    }
}

GLenum filterFor(RuwaEffectSamplerFilter filter)
{
    return filter == RUWA_EFFECT_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
}

GLenum wrapFor(RuwaEffectSamplerWrap wrap)
{
    switch (wrap) {
    case RUWA_EFFECT_WRAP_REPEAT:
        return GL_REPEAT;
    case RUWA_EFFECT_WRAP_MIRRORED:
        return GL_MIRRORED_REPEAT;
    case RUWA_EFFECT_WRAP_CLAMP_TO_EDGE:
    default:
        return GL_CLAMP_TO_EDGE;
    }
}

GLenum imageAccessFor(RuwaEffectImageAccess access)
{
    switch (access) {
    case RUWA_EFFECT_IMAGE_READ_ONLY:
        return GL_READ_ONLY;
    case RUWA_EFFECT_IMAGE_READ_WRITE:
        return GL_READ_WRITE;
    case RUWA_EFFECT_IMAGE_WRITE_ONLY:
    default:
        return GL_WRITE_ONLY;
    }
}

GLbitfield barrierBitsFor(uint32_t bits)
{
    GLbitfield out = 0;
    if (bits & RUWA_EFFECT_BARRIER_TEXTURE_FETCH) {
        out |= GL_TEXTURE_FETCH_BARRIER_BIT;
    }
    if (bits & RUWA_EFFECT_BARRIER_FRAMEBUFFER) {
        out |= GL_FRAMEBUFFER_BARRIER_BIT;
    }
    if (bits & RUWA_EFFECT_BARRIER_SHADER_IMAGE_ACCESS) {
        out |= GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
    }
    return out;
}

QString shaderSourceString(const RuwaEffectShaderSource* src)
{
    if (!src || !src->source) {
        return {};
    }
    const int length = src->source_length > 0 ? static_cast<int>(src->source_length) : -1;
    return QString::fromUtf8(src->source, length);
}

} // namespace

EffectHostGpuContext::EffectHostGpuContext(
    QOpenGLFunctions_4_5_Core* gl, QString sharedVertexSource)
    : m_gl(gl)
    , m_sharedVertexSource(std::move(sharedVertexSource))
{
}

EffectHostGpuContext::~EffectHostGpuContext()
{
    // Defensive: destroy_pass should have freed everything already, but never
    // leak GL objects if a plugin forgot one.
    for (HostTexture* texture : m_pluginTextures) {
        if (m_gl && texture->id) {
            GLuint id = texture->id;
            m_gl->glDeleteTextures(1, &id);
        }
        delete texture;
    }
    for (HostSampler* sampler : m_samplers) {
        if (m_gl && sampler->id) {
            GLuint id = sampler->id;
            m_gl->glDeleteSamplers(1, &id);
        }
        delete sampler;
    }
    for (HostPipeline* pipeline : m_pipelines) {
        delete pipeline; // GLShaderProgram destructor releases the GL program
    }
}

void EffectHostGpuContext::beginInvocation(const aether::GLLayerEffectRenderContext* renderCtx)
{
    m_renderCtx = renderCtx;
    m_boundProgram = nullptr;
    m_invocationCursor = 0;
}

void EffectHostGpuContext::endInvocation()
{
    m_renderCtx = nullptr;
    m_boundProgram = nullptr;
}

RuwaEffectTexture EffectHostGpuContext::wrapExternal(GLuint id, uint32_t width, uint32_t height)
{
    if (m_invocationCursor >= m_invocationWrappers.size()) {
        m_invocationWrappers.push_back(std::make_unique<HostTexture>());
    }
    HostTexture* texture = m_invocationWrappers[m_invocationCursor++].get();
    texture->id = id;
    texture->width = width;
    texture->height = height;
    texture->format = RUWA_EFFECT_FORMAT_RGBA8;
    texture->pluginOwned = false;
    texture->owner = this;
    return reinterpret_cast<RuwaEffectTexture>(texture);
}

HostTexture* EffectHostGpuContext::asTexture(RuwaEffectTexture texture) const
{
    if (!texture) {
        return nullptr;
    }
    auto* candidate = reinterpret_cast<HostTexture*>(texture);
    if (m_pluginTextures.contains(candidate)) {
        return candidate;
    }
    for (std::size_t i = 0; i < m_invocationCursor && i < m_invocationWrappers.size(); ++i) {
        if (m_invocationWrappers[i].get() == candidate) {
            return candidate;
        }
    }
    // Compare pointer values against host-owned registries before dereferencing.
    // This safely rejects foreign, stale and fabricated handles.
    qWarning("Ruwa effect plugin: invalid texture handle for this pass/context");
    return nullptr;
}

HostSampler* EffectHostGpuContext::asSampler(RuwaEffectSampler sampler) const
{
    if (!sampler) {
        return nullptr;
    }
    auto* candidate = reinterpret_cast<HostSampler*>(sampler);
    if (!m_samplers.contains(candidate)) {
        qWarning("Ruwa effect plugin: invalid sampler handle for this pass/context");
        return nullptr;
    }
    return candidate;
}

HostPipeline* EffectHostGpuContext::asPipeline(void* pipeline) const
{
    if (!pipeline) {
        return nullptr;
    }
    auto* candidate = reinterpret_cast<HostPipeline*>(pipeline);
    if (!m_pipelines.contains(candidate)) {
        qWarning("Ruwa effect plugin: invalid pipeline handle for this pass/context");
        return nullptr;
    }
    return candidate;
}

GLuint EffectHostGpuContext::resolve(RuwaEffectTexture texture) const
{
    HostTexture* t = asTexture(texture);
    return t ? t->id : 0;
}

RuwaEffectTexture EffectHostGpuContext::createTexture(
    uint32_t width, uint32_t height, RuwaEffectTextureFormat format)
{
    if (!m_gl || width == 0 || height == 0 || !validTextureFormat(format)) {
        return nullptr;
    }
    aether::TextureParams params;
    params.minFilter = GL_NEAREST;
    params.magFilter = GL_NEAREST;
    params.wrapS = GL_CLAMP_TO_EDGE;
    params.wrapT = GL_CLAMP_TO_EDGE;
    params.internalFormat = internalFormatFor(format);
    params.pixelType = pixelTypeFor(format);
    const GLuint id = aether::createTexture2D(
        m_gl, static_cast<GLsizei>(width), static_cast<GLsizei>(height), params);
    if (!id) {
        return nullptr;
    }
    auto* texture = new HostTexture { id, width, height, format, /*pluginOwned=*/true, this };
    m_pluginTextures.insert(texture);
    return reinterpret_cast<RuwaEffectTexture>(texture);
}

void EffectHostGpuContext::destroyTexture(RuwaEffectTexture texture)
{
    HostTexture* t = asTexture(texture);
    if (!t || !t->pluginOwned) {
        return;
    }
    if (m_gl && t->id) {
        GLuint id = t->id;
        m_gl->glDeleteTextures(1, &id);
    }
    m_pluginTextures.erase(t);
    delete t;
}

uint32_t EffectHostGpuContext::textureWidth(RuwaEffectTexture texture) const
{
    HostTexture* t = asTexture(texture);
    return t ? t->width : 0;
}

uint32_t EffectHostGpuContext::textureHeight(RuwaEffectTexture texture) const
{
    HostTexture* t = asTexture(texture);
    return t ? t->height : 0;
}

RuwaEffectSampler EffectHostGpuContext::createSampler(RuwaEffectSamplerFilter minFilter,
    RuwaEffectSamplerFilter magFilter, RuwaEffectSamplerWrap wrap)
{
    if (!m_gl || !validSamplerFilter(minFilter) || !validSamplerFilter(magFilter)
        || !validSamplerWrap(wrap)) {
        return nullptr;
    }
    GLuint id = 0;
    m_gl->glGenSamplers(1, &id);
    if (!id) {
        return nullptr;
    }
    m_gl->glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, static_cast<GLint>(filterFor(minFilter)));
    m_gl->glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, static_cast<GLint>(filterFor(magFilter)));
    m_gl->glSamplerParameteri(id, GL_TEXTURE_WRAP_S, static_cast<GLint>(wrapFor(wrap)));
    m_gl->glSamplerParameteri(id, GL_TEXTURE_WRAP_T, static_cast<GLint>(wrapFor(wrap)));
    auto* sampler = new HostSampler { id, this };
    m_samplers.insert(sampler);
    return reinterpret_cast<RuwaEffectSampler>(sampler);
}

void EffectHostGpuContext::destroySampler(RuwaEffectSampler sampler)
{
    HostSampler* s = asSampler(sampler);
    if (!s) {
        return;
    }
    if (m_gl && s->id) {
        GLuint id = s->id;
        m_gl->glDeleteSamplers(1, &id);
    }
    m_samplers.erase(s);
    delete s;
}

RuwaEffectPipeline EffectHostGpuContext::createGraphicsPipeline(
    const RuwaEffectShaderSource* fragment)
{
    if (!m_gl || !validShaderSource(fragment, RUWA_EFFECT_STAGE_FRAGMENT)) {
        qWarning("Ruwa effect plugin: invalid fragment shader source descriptor");
        return nullptr;
    }
    auto program = std::make_unique<aether::GLShaderProgram>(m_gl);
    const QString cacheKey
        = fragment->cache_key ? QString::fromUtf8(fragment->cache_key) : QString();
    auto result
        = program->loadFromSource(m_sharedVertexSource, shaderSourceString(fragment), cacheKey);
    if (!result || !program->isValid()) {
        qWarning("Ruwa effect plugin: graphics pipeline '%s' failed to compile",
            fragment->debug_name ? fragment->debug_name : "<unnamed>");
        return nullptr;
    }
    auto* pipeline = new HostPipeline { std::move(program), /*compute=*/false, this };
    m_pipelines.insert(pipeline);
    return reinterpret_cast<RuwaEffectPipeline>(pipeline);
}

void EffectHostGpuContext::destroyGraphicsPipeline(RuwaEffectPipeline pipeline)
{
    HostPipeline* p = asPipeline(pipeline);
    if (!p || p->compute) {
        if (p) {
            qWarning("Ruwa effect plugin: compute pipeline passed to graphics destroy");
        }
        return;
    }
    m_pipelines.erase(p);
    delete p;
}

RuwaEffectComputePipeline EffectHostGpuContext::createComputePipeline(
    const RuwaEffectShaderSource* compute)
{
    if (!m_gl || !validShaderSource(compute, RUWA_EFFECT_STAGE_COMPUTE)) {
        qWarning("Ruwa effect plugin: invalid compute shader source descriptor");
        return nullptr;
    }
    auto program = std::make_unique<aether::GLShaderProgram>(m_gl);
    const QString cacheKey = compute->cache_key ? QString::fromUtf8(compute->cache_key) : QString();
    auto result = program->loadComputeFromSource(shaderSourceString(compute), cacheKey);
    if (!result || !program->isValid()) {
        qWarning("Ruwa effect plugin: compute pipeline '%s' failed to compile",
            compute->debug_name ? compute->debug_name : "<unnamed>");
        return nullptr;
    }
    auto* pipeline = new HostPipeline { std::move(program), /*compute=*/true, this };
    m_pipelines.insert(pipeline);
    return reinterpret_cast<RuwaEffectComputePipeline>(pipeline);
}

void EffectHostGpuContext::destroyComputePipeline(RuwaEffectComputePipeline pipeline)
{
    HostPipeline* p = asPipeline(pipeline);
    if (!p || !p->compute) {
        if (p) {
            qWarning("Ruwa effect plugin: graphics pipeline passed to compute destroy");
        }
        return;
    }
    m_pipelines.erase(p);
    delete p;
}

RuwaEffectTexture EffectHostGpuContext::allocScratch(RuwaEffectTextureFormat format)
{
    if (!m_renderCtx || !m_renderCtx->allocateScratchTexture || !validTextureFormat(format)) {
        return nullptr;
    }
    if (format == RUWA_EFFECT_FORMAT_RGBA32F) {
        qWarning("Ruwa effect plugin: RGBA32F transient scratch is unsupported; "
                 "use create_texture for a persistent 32-bit resource");
        return nullptr;
    }
    const GLuint id = m_renderCtx->allocateScratchTexture(format != RUWA_EFFECT_FORMAT_RGBA8);
    if (!id) {
        return nullptr;
    }
    return wrapExternal(id, m_renderCtx->outputWidth, m_renderCtx->outputHeight);
}

void EffectHostGpuContext::beginRenderPass(RuwaEffectTexture target)
{
    HostTexture* t = asTexture(target);
    if (!m_gl || !m_renderCtx || !t) {
        return;
    }
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_renderCtx->fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->id, 0);
    m_gl->glViewport(0, 0, static_cast<GLsizei>(t->width), static_cast<GLsizei>(t->height));
    m_gl->glDisable(GL_BLEND);
}

void EffectHostGpuContext::bindGraphicsPipeline(RuwaEffectPipeline pipeline)
{
    HostPipeline* p = asPipeline(pipeline);
    if (!p || p->compute || !p->program) {
        return;
    }
    p->program->use();
    m_boundProgram = p->program.get();
}

void EffectHostGpuContext::bindTexture(
    uint32_t unit, RuwaEffectTexture texture, RuwaEffectSampler sampler)
{
    if (!m_gl) {
        return;
    }
    HostTexture* t = asTexture(texture);
    HostSampler* s = asSampler(sampler);
    m_gl->glActiveTexture(GL_TEXTURE0 + unit);
    m_gl->glBindTexture(GL_TEXTURE_2D, t ? t->id : 0);
    m_gl->glBindSampler(unit, s ? s->id : 0);
}

void EffectHostGpuContext::drawFullscreen()
{
    if (!m_gl || !m_renderCtx) {
        return;
    }
    m_gl->glBindVertexArray(m_renderCtx->emptyVao);
    m_gl->glDrawArrays(GL_TRIANGLES, 0, 6);
    m_gl->glBindVertexArray(0);
}

RuwaBool EffectHostGpuContext::beginRoiScissor(int32_t expandX, int32_t expandY)
{
    if (!m_renderCtx) {
        return RUWA_FALSE;
    }
    return aether::beginRoiScissor(*m_renderCtx, expandX, expandY) ? RUWA_TRUE : RUWA_FALSE;
}

void EffectHostGpuContext::endRoiScissor(RuwaBool scissorActive)
{
    if (!m_renderCtx) {
        return;
    }
    aether::endRoiScissor(*m_renderCtx, scissorActive != RUWA_FALSE);
}

void EffectHostGpuContext::bindComputePipeline(RuwaEffectComputePipeline pipeline)
{
    HostPipeline* p = asPipeline(pipeline);
    if (!p || !p->compute || !p->program) {
        return;
    }
    p->program->use();
    m_boundProgram = p->program.get();
}

void EffectHostGpuContext::bindImageTexture(uint32_t unit, RuwaEffectTexture texture,
    RuwaEffectImageAccess access, RuwaEffectTextureFormat format)
{
    if (!m_gl || !validImageAccess(access) || !validTextureFormat(format)) {
        return;
    }
    HostTexture* t = asTexture(texture);
    if (t && t->pluginOwned && t->format != format) {
        qWarning("Ruwa effect plugin: image binding format does not match texture format");
        return;
    }
    m_gl->glBindImageTexture(
        unit, t ? t->id : 0, 0, GL_FALSE, 0, imageAccessFor(access), internalFormatFor(format));
}

void EffectHostGpuContext::dispatchCompute(uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ)
{
    if (m_gl) {
        m_gl->glDispatchCompute(groupsX, groupsY, groupsZ);
    }
}

void EffectHostGpuContext::memoryBarrier(uint32_t barrierBits)
{
    if (m_gl) {
        m_gl->glMemoryBarrier(barrierBitsFor(barrierBits));
    }
}

void EffectHostGpuContext::setUniformInt(const char* name, int32_t v)
{
    if (m_boundProgram) {
        m_boundProgram->setUniform(name, static_cast<int>(v));
    }
}

void EffectHostGpuContext::setUniformInt2(const char* name, int32_t x, int32_t y)
{
    if (m_boundProgram) {
        m_boundProgram->setUniform(name, static_cast<int>(x), static_cast<int>(y));
    }
}

void EffectHostGpuContext::setUniformFloat(const char* name, float v)
{
    if (m_boundProgram) {
        m_boundProgram->setUniform(name, v);
    }
}

void EffectHostGpuContext::setUniformFloat2(const char* name, float x, float y)
{
    if (m_boundProgram) {
        m_boundProgram->setUniform(name, x, y);
    }
}

void EffectHostGpuContext::setUniformFloat3(const char* name, float x, float y, float z)
{
    if (m_boundProgram) {
        m_boundProgram->setUniform(name, x, y, z);
    }
}

void EffectHostGpuContext::setUniformFloat4(const char* name, float x, float y, float z, float w)
{
    if (m_boundProgram) {
        m_boundProgram->setUniform(name, x, y, z, w);
    }
}

void EffectHostGpuContext::setUniformVec2Array(
    const char* name, const float* values, uint32_t count)
{
    if (m_boundProgram) {
        m_boundProgram->setUniformVec2Array(name, values, static_cast<int>(count));
    }
}

void EffectHostGpuContext::blit(RuwaEffectTexture src, RuwaEffectTexture dst)
{
    HostTexture* s = asTexture(src);
    HostTexture* d = asTexture(dst);
    if (!m_gl || !s || !d || !s->id || !d->id) {
        return;
    }
    const uint32_t w = std::min(s->width, d->width);
    const uint32_t h = std::min(s->height, d->height);
    m_gl->glCopyImageSubData(s->id, GL_TEXTURE_2D, 0, 0, 0, 0, d->id, GL_TEXTURE_2D, 0, 0, 0, 0,
        static_cast<GLsizei>(w), static_cast<GLsizei>(h), 1);
}

// --- C trampolines --------------------------------------------------------

namespace {

inline EffectHostGpuContext* ctx(RuwaEffectGpuContext gpu)
{
    return reinterpret_cast<EffectHostGpuContext*>(gpu);
}

RuwaEffectTexture RUWA_EFFECT_CALL tCreateTexture(
    RuwaEffectGpuContext gpu, uint32_t w, uint32_t h, RuwaEffectTextureFormat f)
{
    return gpu ? ctx(gpu)->createTexture(w, h, f) : nullptr;
}
void RUWA_EFFECT_CALL tDestroyTexture(RuwaEffectGpuContext gpu, RuwaEffectTexture t)
{
    if (gpu) {
        ctx(gpu)->destroyTexture(t);
    }
}
uint32_t RUWA_EFFECT_CALL tTextureWidth(RuwaEffectGpuContext gpu, RuwaEffectTexture t)
{
    return gpu ? ctx(gpu)->textureWidth(t) : 0;
}
uint32_t RUWA_EFFECT_CALL tTextureHeight(RuwaEffectGpuContext gpu, RuwaEffectTexture t)
{
    return gpu ? ctx(gpu)->textureHeight(t) : 0;
}
RuwaEffectSampler RUWA_EFFECT_CALL tCreateSampler(RuwaEffectGpuContext gpu,
    RuwaEffectSamplerFilter mn, RuwaEffectSamplerFilter mg, RuwaEffectSamplerWrap wrap)
{
    return gpu ? ctx(gpu)->createSampler(mn, mg, wrap) : nullptr;
}
void RUWA_EFFECT_CALL tDestroySampler(RuwaEffectGpuContext gpu, RuwaEffectSampler s)
{
    if (gpu) {
        ctx(gpu)->destroySampler(s);
    }
}
RuwaEffectPipeline RUWA_EFFECT_CALL tCreateGraphicsPipeline(
    RuwaEffectGpuContext gpu, const RuwaEffectShaderSource* frag)
{
    return gpu ? ctx(gpu)->createGraphicsPipeline(frag) : nullptr;
}
void RUWA_EFFECT_CALL tDestroyGraphicsPipeline(RuwaEffectGpuContext gpu, RuwaEffectPipeline p)
{
    if (gpu) {
        ctx(gpu)->destroyGraphicsPipeline(p);
    }
}
RuwaEffectComputePipeline RUWA_EFFECT_CALL tCreateComputePipeline(
    RuwaEffectGpuContext gpu, const RuwaEffectShaderSource* comp)
{
    return gpu ? ctx(gpu)->createComputePipeline(comp) : nullptr;
}
void RUWA_EFFECT_CALL tDestroyComputePipeline(RuwaEffectGpuContext gpu, RuwaEffectComputePipeline p)
{
    if (gpu) {
        ctx(gpu)->destroyComputePipeline(p);
    }
}
RuwaEffectTexture RUWA_EFFECT_CALL tAllocScratch(
    RuwaEffectGpuContext gpu, RuwaEffectTextureFormat f)
{
    return gpu ? ctx(gpu)->allocScratch(f) : nullptr;
}
void RUWA_EFFECT_CALL tBeginRenderPass(RuwaEffectGpuContext gpu, RuwaEffectTexture target)
{
    if (gpu) {
        ctx(gpu)->beginRenderPass(target);
    }
}
void RUWA_EFFECT_CALL tBindGraphicsPipeline(RuwaEffectGpuContext gpu, RuwaEffectPipeline p)
{
    if (gpu) {
        ctx(gpu)->bindGraphicsPipeline(p);
    }
}
void RUWA_EFFECT_CALL tBindTexture(
    RuwaEffectGpuContext gpu, uint32_t unit, RuwaEffectTexture t, RuwaEffectSampler s)
{
    if (gpu) {
        ctx(gpu)->bindTexture(unit, t, s);
    }
}
void RUWA_EFFECT_CALL tDrawFullscreen(RuwaEffectGpuContext gpu)
{
    if (gpu) {
        ctx(gpu)->drawFullscreen();
    }
}
RuwaBool RUWA_EFFECT_CALL tBeginRoiScissor(RuwaEffectGpuContext gpu, int32_t ex, int32_t ey)
{
    return gpu ? ctx(gpu)->beginRoiScissor(ex, ey) : RUWA_FALSE;
}
void RUWA_EFFECT_CALL tEndRoiScissor(RuwaEffectGpuContext gpu, RuwaBool active)
{
    if (gpu) {
        ctx(gpu)->endRoiScissor(active);
    }
}
void RUWA_EFFECT_CALL tBindComputePipeline(RuwaEffectGpuContext gpu, RuwaEffectComputePipeline p)
{
    if (gpu) {
        ctx(gpu)->bindComputePipeline(p);
    }
}
void RUWA_EFFECT_CALL tBindImageTexture(RuwaEffectGpuContext gpu, uint32_t unit,
    RuwaEffectTexture t, RuwaEffectImageAccess a, RuwaEffectTextureFormat f)
{
    if (gpu) {
        ctx(gpu)->bindImageTexture(unit, t, a, f);
    }
}
void RUWA_EFFECT_CALL tDispatchCompute(
    RuwaEffectGpuContext gpu, uint32_t gx, uint32_t gy, uint32_t gz)
{
    if (gpu) {
        ctx(gpu)->dispatchCompute(gx, gy, gz);
    }
}
void RUWA_EFFECT_CALL tMemoryBarrier(RuwaEffectGpuContext gpu, uint32_t bits)
{
    if (gpu) {
        ctx(gpu)->memoryBarrier(bits);
    }
}
void RUWA_EFFECT_CALL tSetUniformInt(RuwaEffectGpuContext gpu, const char* n, int32_t v)
{
    if (gpu) {
        ctx(gpu)->setUniformInt(n, v);
    }
}
void RUWA_EFFECT_CALL tSetUniformInt2(RuwaEffectGpuContext gpu, const char* n, int32_t x, int32_t y)
{
    if (gpu) {
        ctx(gpu)->setUniformInt2(n, x, y);
    }
}
void RUWA_EFFECT_CALL tSetUniformFloat(RuwaEffectGpuContext gpu, const char* n, float v)
{
    if (gpu) {
        ctx(gpu)->setUniformFloat(n, v);
    }
}
void RUWA_EFFECT_CALL tSetUniformFloat2(RuwaEffectGpuContext gpu, const char* n, float x, float y)
{
    if (gpu) {
        ctx(gpu)->setUniformFloat2(n, x, y);
    }
}
void RUWA_EFFECT_CALL tSetUniformFloat3(
    RuwaEffectGpuContext gpu, const char* n, float x, float y, float z)
{
    if (gpu) {
        ctx(gpu)->setUniformFloat3(n, x, y, z);
    }
}
void RUWA_EFFECT_CALL tSetUniformFloat4(
    RuwaEffectGpuContext gpu, const char* n, float x, float y, float z, float w)
{
    if (gpu) {
        ctx(gpu)->setUniformFloat4(n, x, y, z, w);
    }
}
void RUWA_EFFECT_CALL tSetUniformVec2Array(
    RuwaEffectGpuContext gpu, const char* n, const float* values, uint32_t count)
{
    if (gpu) {
        ctx(gpu)->setUniformVec2Array(n, values, count);
    }
}
void RUWA_EFFECT_CALL tBlit(RuwaEffectGpuContext gpu, RuwaEffectTexture src, RuwaEffectTexture dst)
{
    if (gpu) {
        ctx(gpu)->blit(src, dst);
    }
}

const RuwaEffectGpuApi kGpuApi = {
    sizeof(RuwaEffectGpuApi),
    tCreateTexture,
    tDestroyTexture,
    tTextureWidth,
    tTextureHeight,
    tCreateSampler,
    tDestroySampler,
    tCreateGraphicsPipeline,
    tDestroyGraphicsPipeline,
    tCreateComputePipeline,
    tDestroyComputePipeline,
    tAllocScratch,
    tBeginRenderPass,
    tBindGraphicsPipeline,
    tBindTexture,
    tDrawFullscreen,
    tBeginRoiScissor,
    tEndRoiScissor,
    tBindComputePipeline,
    tBindImageTexture,
    tDispatchCompute,
    tMemoryBarrier,
    tSetUniformInt,
    tSetUniformInt2,
    tSetUniformFloat,
    tSetUniformFloat2,
    tSetUniformFloat3,
    tSetUniformFloat4,
    tSetUniformVec2Array,
    tBlit,
};

} // namespace

const RuwaEffectGpuApi* gpuApiTable()
{
    return &kGpuApi;
}

} // namespace ruwa::core::effects::plugin
