// SPDX-License-Identifier: MPL-2.0

#include "features/canvas/rendering/GLRetainedRenderer.h"

#include "shared/rendering/GLShaderProgram.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace aether {

namespace {

constexpr char kRetainedVertexShader[] = R"(
#version 450 core
void main() {
    vec2 pos;
    if (gl_VertexID == 0) {
        pos = vec2(-1.0, -1.0);
    } else if (gl_VertexID == 1) {
        pos = vec2(3.0, -1.0);
    } else {
        pos = vec2(-1.0, 3.0);
    }
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

constexpr char kRetainedFragmentShader[] = R"(
#version 450 core
layout(std430, binding = 0) readonly buffer PolygonPointBuffer {
    vec2 points[];
} uPolygonPoints;

uniform vec2 uTileOrigin;
uniform vec4 uWorldBounds;
uniform int uPointOffset;
uniform int uPointCount;
uniform vec4 uColorPremul;

out vec4 fragColor;

bool pointInPolygon(vec2 p) {
    bool inside = false;
    for (int i = 0, j = uPointCount - 1; i < uPointCount; j = i++) {
        vec2 a = uPolygonPoints.points[uPointOffset + i];
        vec2 b = uPolygonPoints.points[uPointOffset + j];
        bool crosses = ((a.y > p.y) != (b.y > p.y))
            && (p.x < ((b.x - a.x) * (p.y - a.y) / (b.y - a.y + 0.0000001)) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }
    return inside;
}

void main() {
    vec2 worldPos = uTileOrigin + gl_FragCoord.xy;
    if (worldPos.x < uWorldBounds.x || worldPos.x > uWorldBounds.z
        || worldPos.y < uWorldBounds.y || worldPos.y > uWorldBounds.w) {
        discard;
    }
    if (!pointInPolygon(worldPos)) {
        discard;
    }
    fragColor = uColorPremul;
}
)";

Rect primitiveTileBounds(const TileKey& key)
{
    float tileX = 0.0f;
    float tileY = 0.0f;
    tileWorldOrigin(key, tileX, tileY);
    return Rect { tileX, tileY, static_cast<float>(TILE_SIZE), static_cast<float>(TILE_SIZE) };
}

bool payloadHasGlyphRuns(const RetainedRenderPayload& payload)
{
    for (const RetainedPrimitive& primitive : payload.primitives) {
        if (primitive.type == RetainedPrimitiveType::GlyphRun) {
            return true;
        }
    }
    return false;
}

QPointF mapGlyphPointToTile(const TransformState& transform, const Vector2& glyphOrigin,
    const QPainterPath::Element& element, float tileX, float tileY)
{
    const Vector2 mapped = transform.transformPoint({ glyphOrigin.x + static_cast<float>(element.x),
        glyphOrigin.y + static_cast<float>(element.y) });
    return QPointF(mapped.x - tileX, mapped.y - tileY);
}

QPainterPath transformGlyphPathAffine(const QPainterPath& glyphPath, const RetainedGlyphRun& run,
    int glyphIndex, float tileX, float tileY)
{
    QPainterPath result;
    if (glyphIndex < 0 || glyphIndex >= static_cast<int>(run.sourcePositions.size())) {
        return result;
    }

    const Vector2 glyphOrigin = run.sourcePositions[static_cast<size_t>(glyphIndex)];
    const int elementCount = glyphPath.elementCount();
    for (int i = 0; i < elementCount; ++i) {
        const auto element = glyphPath.elementAt(i);
        if (element.isMoveTo()) {
            result.moveTo(mapGlyphPointToTile(run.transform, glyphOrigin, element, tileX, tileY));
        } else if (element.isLineTo()) {
            result.lineTo(mapGlyphPointToTile(run.transform, glyphOrigin, element, tileX, tileY));
        } else if (element.isCurveTo() && i + 2 < elementCount) {
            const auto c1 = element;
            const auto c2 = glyphPath.elementAt(i + 1);
            const auto end = glyphPath.elementAt(i + 2);
            result.cubicTo(mapGlyphPointToTile(run.transform, glyphOrigin, c1, tileX, tileY),
                mapGlyphPointToTile(run.transform, glyphOrigin, c2, tileX, tileY),
                mapGlyphPointToTile(run.transform, glyphOrigin, end, tileX, tileY));
            i += 2;
        }
    }
    result.setFillRule(glyphPath.fillRule());
    return result;
}

// Measure flatness after the layer transform. Unlike QPainterPath::toFillPolygons(), this keeps
// enlarged Free Transform and Warp contours within a sub-pixel error in document space.
constexpr qreal kWarpedGlyphFlatness = 0.25;
constexpr int kWarpedGlyphMaxSubdivisionDepth = 18;

QPointF mapGlyphSourcePointToTile(const TransformState& transform, const Vector2& glyphOrigin,
    const QPointF& point, float tileX, float tileY)
{
    const Vector2 mapped = transform.transformPoint({ glyphOrigin.x + static_cast<float>(point.x()),
        glyphOrigin.y + static_cast<float>(point.y()) });
    return QPointF(mapped.x - tileX, mapped.y - tileY);
}

qreal pointToSegmentDistanceSquared(const QPointF& point, const QPointF& start, const QPointF& end)
{
    const QPointF segment = end - start;
    const qreal lengthSquared = QPointF::dotProduct(segment, segment);
    if (lengthSquared <= std::numeric_limits<qreal>::epsilon()) {
        const QPointF delta = point - start;
        return QPointF::dotProduct(delta, delta);
    }

    const qreal projection = std::clamp(
        QPointF::dotProduct(point - start, segment) / lengthSquared, qreal { 0 }, qreal { 1 });
    const QPointF delta = point - (start + segment * projection);
    return QPointF::dotProduct(delta, delta);
}

QPointF interpolateLine(const QPointF& start, const QPointF& end, qreal t)
{
    return start * (1.0 - t) + end * t;
}

QPointF evaluateCubic(const QPointF& start, const QPointF& control1, const QPointF& control2,
    const QPointF& end, qreal t)
{
    const qreal oneMinusT = 1.0 - t;
    return start * (oneMinusT * oneMinusT * oneMinusT)
        + control1 * (3.0 * oneMinusT * oneMinusT * t) + control2 * (3.0 * oneMinusT * t * t)
        + end * (t * t * t);
}

template <typename SourcePointAt>
void appendAdaptivelyMappedSegment(QPainterPath& result, const TransformState& transform,
    const Vector2& glyphOrigin, const SourcePointAt& sourcePointAt, qreal fromT, qreal toT,
    const QPointF& mappedStart, const QPointF& mappedEnd, float tileX, float tileY, int depth)
{
    const qreal interval = toT - fromT;
    const qreal sampleT1 = fromT + interval * 0.25;
    const qreal sampleT2 = fromT + interval * 0.5;
    const qreal sampleT3 = fromT + interval * 0.75;
    const QPointF mapped1
        = mapGlyphSourcePointToTile(transform, glyphOrigin, sourcePointAt(sampleT1), tileX, tileY);
    const QPointF mapped2
        = mapGlyphSourcePointToTile(transform, glyphOrigin, sourcePointAt(sampleT2), tileX, tileY);
    const QPointF mapped3
        = mapGlyphSourcePointToTile(transform, glyphOrigin, sourcePointAt(sampleT3), tileX, tileY);
    const qreal flatnessSquared = kWarpedGlyphFlatness * kWarpedGlyphFlatness;
    const qreal errorSquared
        = std::max({ pointToSegmentDistanceSquared(mapped1, mappedStart, mappedEnd),
            pointToSegmentDistanceSquared(mapped2, mappedStart, mappedEnd),
            pointToSegmentDistanceSquared(mapped3, mappedStart, mappedEnd) });

    if (errorSquared <= flatnessSquared || depth >= kWarpedGlyphMaxSubdivisionDepth) {
        result.lineTo(mappedEnd);
        return;
    }

    const qreal middleT = fromT + interval * 0.5;
    appendAdaptivelyMappedSegment(result, transform, glyphOrigin, sourcePointAt, fromT, middleT,
        mappedStart, mapped2, tileX, tileY, depth + 1);
    appendAdaptivelyMappedSegment(result, transform, glyphOrigin, sourcePointAt, middleT, toT,
        mapped2, mappedEnd, tileX, tileY, depth + 1);
}

QPainterPath transformGlyphPathAdaptive(const QPainterPath& glyphPath, const RetainedGlyphRun& run,
    int glyphIndex, float tileX, float tileY)
{
    QPainterPath result;
    if (glyphIndex < 0 || glyphIndex >= static_cast<int>(run.sourcePositions.size())) {
        return result;
    }

    const Vector2 glyphOrigin = run.sourcePositions[static_cast<size_t>(glyphIndex)];
    QPointF sourceCurrent;
    const int elementCount = glyphPath.elementCount();
    for (int i = 0; i < elementCount; ++i) {
        const auto element = glyphPath.elementAt(i);
        const QPointF sourcePoint(element.x, element.y);
        if (element.isMoveTo()) {
            sourceCurrent = sourcePoint;
            result.moveTo(
                mapGlyphSourcePointToTile(run.transform, glyphOrigin, sourceCurrent, tileX, tileY));
        } else if (element.isLineTo()) {
            const QPointF sourceStart = sourceCurrent;
            const QPointF mappedStart = result.currentPosition();
            const QPointF mappedEnd
                = mapGlyphSourcePointToTile(run.transform, glyphOrigin, sourcePoint, tileX, tileY);
            const auto sourcePointAt = [sourceStart, sourcePoint](qreal t) {
                return interpolateLine(sourceStart, sourcePoint, t);
            };
            appendAdaptivelyMappedSegment(result, run.transform, glyphOrigin, sourcePointAt, 0.0,
                1.0, mappedStart, mappedEnd, tileX, tileY, 0);
            sourceCurrent = sourcePoint;
        } else if (element.isCurveTo() && i + 2 < elementCount) {
            const QPointF sourceStart = sourceCurrent;
            const QPointF control1(element.x, element.y);
            const auto control2Element = glyphPath.elementAt(i + 1);
            const auto endElement = glyphPath.elementAt(i + 2);
            const QPointF control2(control2Element.x, control2Element.y);
            const QPointF sourceEnd(endElement.x, endElement.y);
            const QPointF mappedStart = result.currentPosition();
            const QPointF mappedEnd
                = mapGlyphSourcePointToTile(run.transform, glyphOrigin, sourceEnd, tileX, tileY);
            const auto sourcePointAt = [sourceStart, control1, control2, sourceEnd](qreal t) {
                return evaluateCubic(sourceStart, control1, control2, sourceEnd, t);
            };
            appendAdaptivelyMappedSegment(result, run.transform, glyphOrigin, sourcePointAt, 0.0,
                1.0, mappedStart, mappedEnd, tileX, tileY, 0);
            sourceCurrent = sourceEnd;
            i += 2;
        }
    }
    result.setFillRule(glyphPath.fillRule());
    return result;
}

QPainterPath transformGlyphPathForTile(const QPainterPath& glyphPath, const RetainedGlyphRun& run,
    int glyphIndex, float tileX, float tileY)
{
    if (run.transform.hasFreeQuad() || run.transform.hasDeformMesh()) {
        return transformGlyphPathAdaptive(glyphPath, run, glyphIndex, tileX, tileY);
    }
    return transformGlyphPathAffine(glyphPath, run, glyphIndex, tileX, tileY);
}

QColor retainedColor(const Color& color)
{
    return QColor::fromRgbF(std::clamp(color.r, 0.0f, 1.0f), std::clamp(color.g, 0.0f, 1.0f),
        std::clamp(color.b, 0.0f, 1.0f), std::clamp(color.a, 0.0f, 1.0f));
}

QColor retainedRunColor(const RetainedGlyphRun& run)
{
    return retainedColor(run.color);
}

QPolygonF retainedPolygonToTile(const RetainedPrimitive& primitive, float tileX, float tileY)
{
    QPolygonF polygon;
    polygon.reserve(static_cast<int>(primitive.points.size()));
    for (const Vector2& point : primitive.points) {
        polygon.append(QPointF(point.x - tileX, point.y - tileY));
    }
    return polygon;
}

} // namespace

GLRetainedRenderer::GLRetainedRenderer(QOpenGLFunctions_4_5_Core* gl)
    : m_gl(gl)
{
}

GLRetainedRenderer::~GLRetainedRenderer()
{
    shutdown();
}

Result<void> GLRetainedRenderer::initialize()
{
    if (m_initialized) {
        return Result<void>::ok();
    }

    m_program = std::make_unique<GLShaderProgram>(m_gl);
    auto shaderResult = m_program->loadFromSource(
        QString::fromLatin1(kRetainedVertexShader), QString::fromLatin1(kRetainedFragmentShader));
    if (!shaderResult) {
        return shaderResult;
    }

    m_gl->glGenFramebuffers(1, &m_fbo);
    m_gl->glGenVertexArrays(1, &m_vao);
    m_gl->glGenBuffers(1, &m_pointBuffer);
    if (!m_fbo || !m_vao || !m_pointBuffer) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed, "Failed to create retained renderer objects" };
    }

    ensureRenderTarget();
    if (!m_texture) {
        shutdown();
        return { ErrorCode::PipelineCreationFailed, "Failed to create retained renderer texture" };
    }

    m_initialized = true;
    return Result<void>::ok();
}

void GLRetainedRenderer::shutdown()
{
    if (m_texture) {
        m_gl->glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_pointBuffer) {
        m_gl->glDeleteBuffers(1, &m_pointBuffer);
        m_pointBuffer = 0;
    }
    if (m_vao) {
        m_gl->glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_fbo) {
        m_gl->glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }

    m_cachedPayload = nullptr;
    m_cachedRevision = 0;
    m_cachedPrimitives.clear();
    m_program.reset();
    m_initialized = false;
}

GLuint GLRetainedRenderer::renderPayloadTile(
    const RetainedRenderPayload& payload, const TileKey& key)
{
    if (!m_initialized || payload.empty() || !retainedPayloadIntersectsTile(payload, key)) {
        return 0;
    }

    ensureRenderTarget();
    if (payloadHasGlyphRuns(payload)) {
        return renderGlyphPayloadTile(payload, key) ? m_texture : 0;
    }

    uploadPayloadIfNeeded(payload);
    clearRenderTarget();

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    m_gl->glViewport(0, 0, TILE_SIZE, TILE_SIZE);

    m_gl->glBindVertexArray(m_vao);
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_pointBuffer);

    m_program->use();
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    m_gl->glDisable(GL_DEPTH_TEST);

    float tileX = 0.0f;
    float tileY = 0.0f;
    tileWorldOrigin(key, tileX, tileY);
    const Rect tileBounds = primitiveTileBounds(key);
    m_program->setUniform("uTileOrigin", tileX, tileY);

    for (size_t primitiveIndex = 0; primitiveIndex < payload.primitives.size(); ++primitiveIndex) {
        const RetainedPrimitive& primitive = payload.primitives[primitiveIndex];
        const CachedPrimitive& cachedPrimitive = m_cachedPrimitives[primitiveIndex];
        if (primitive.type != RetainedPrimitiveType::FilledPolygon || cachedPrimitive.pointCount < 3
            || !primitive.worldBounds.intersects(tileBounds)) {
            continue;
        }

        const float alpha = std::clamp(primitive.color.a, 0.0f, 1.0f);
        const float r = std::clamp(primitive.color.r, 0.0f, 1.0f) * alpha;
        const float g = std::clamp(primitive.color.g, 0.0f, 1.0f) * alpha;
        const float b = std::clamp(primitive.color.b, 0.0f, 1.0f) * alpha;
        m_program->setUniform("uColorPremul", r, g, b, alpha);
        m_program->setUniform("uWorldBounds", primitive.worldBounds.x, primitive.worldBounds.y,
            primitive.worldBounds.x + primitive.worldBounds.width,
            primitive.worldBounds.y + primitive.worldBounds.height);
        m_program->setUniform("uPointOffset", static_cast<int>(cachedPrimitive.pointOffset));
        m_program->setUniform("uPointCount", static_cast<int>(cachedPrimitive.pointCount));
        m_gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    m_gl->glDisable(GL_BLEND);
    m_gl->glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    m_gl->glBindVertexArray(0);
    return m_texture;
}

QImage GLRetainedRenderer::renderPayloadTileImage(
    const RetainedRenderPayload& payload, const TileKey& key)
{
    if (payload.empty() || !retainedPayloadIntersectsTile(payload, key)) {
        return {};
    }

    QImage image(static_cast<int>(TILE_SIZE), static_cast<int>(TILE_SIZE),
        QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);

    float tileX = 0.0f;
    float tileY = 0.0f;
    tileWorldOrigin(key, tileX, tileY);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const Rect tileBounds = primitiveTileBounds(key);
    bool drewAnyPath = false;
    for (const RetainedPrimitive& primitive : payload.primitives) {
        if (primitive.type == RetainedPrimitiveType::FilledPolygon) {
            if (primitive.points.size() < 3 || !primitive.worldBounds.intersects(tileBounds)) {
                continue;
            }
            const QPolygonF polygon = retainedPolygonToTile(primitive, tileX, tileY);
            if (polygon.isEmpty() || !polygon.boundingRect().intersects(image.rect())) {
                continue;
            }
            painter.setBrush(retainedColor(primitive.color));
            painter.drawPolygon(polygon);
            drewAnyPath = true;
            continue;
        }
        if (primitive.type != RetainedPrimitiveType::GlyphRun) {
            continue;
        }
        for (const RetainedGlyphRun& run : primitive.glyphRuns) {
            if (run.empty() || !run.rawFont.isValid()) {
                continue;
            }

            painter.setBrush(retainedRunColor(run));
            const int count = std::min(static_cast<int>(run.glyphIndexes.size()),
                static_cast<int>(run.sourcePositions.size()));
            for (int i = 0; i < count; ++i) {
                const QPainterPath glyphPath
                    = run.rawFont.pathForGlyph(run.glyphIndexes[static_cast<size_t>(i)]);
                if (glyphPath.isEmpty()) {
                    continue;
                }

                const QPainterPath tilePath
                    = transformGlyphPathForTile(glyphPath, run, i, tileX, tileY);
                if (tilePath.isEmpty() || !tilePath.boundingRect().intersects(image.rect())) {
                    continue;
                }
                painter.drawPath(tilePath);
                drewAnyPath = true;
            }
        }
    }

    painter.end();
    return drewAnyPath ? image : QImage {};
}

void GLRetainedRenderer::ensureRenderTarget()
{
    if (m_texture) {
        return;
    }

    m_gl->glGenTextures(1, &m_texture);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_gl->glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, TILE_SIZE, TILE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

void GLRetainedRenderer::clearRenderTarget()
{
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    m_gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    m_gl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
}

void GLRetainedRenderer::uploadPayloadIfNeeded(const RetainedRenderPayload& payload)
{
    if (m_cachedPayload == &payload && m_cachedRevision == payload.revision) {
        return;
    }

    std::vector<Vector2> packedPoints;
    m_cachedPrimitives.clear();
    m_cachedPrimitives.reserve(payload.primitives.size());

    for (const RetainedPrimitive& primitive : payload.primitives) {
        CachedPrimitive cachedPrimitive;
        if (primitive.type == RetainedPrimitiveType::FilledPolygon
            && primitive.points.size() >= 3) {
            cachedPrimitive.pointOffset = static_cast<uint32_t>(packedPoints.size());
            cachedPrimitive.pointCount = static_cast<uint32_t>(primitive.points.size());
            packedPoints.insert(
                packedPoints.end(), primitive.points.begin(), primitive.points.end());
        }
        m_cachedPrimitives.push_back(cachedPrimitive);
    }

    if (packedPoints.empty()) {
        packedPoints.push_back({});
    }

    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_pointBuffer);
    m_gl->glBufferData(GL_SHADER_STORAGE_BUFFER,
        static_cast<GLsizeiptr>(packedPoints.size() * sizeof(Vector2)), packedPoints.data(),
        GL_STREAM_DRAW);
    m_gl->glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_cachedPayload = &payload;
    m_cachedRevision = payload.revision;
}

bool GLRetainedRenderer::renderGlyphPayloadTile(
    const RetainedRenderPayload& payload, const TileKey& key)
{
    const QImage image = renderPayloadTileImage(payload, key);
    if (image.isNull()) {
        return false;
    }

    uploadGlyphImage(image);
    return true;
}

void GLRetainedRenderer::uploadGlyphImage(const QImage& image)
{
    m_gl->glBindTexture(GL_TEXTURE_2D, m_texture);
    m_gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    m_gl->glTexSubImage2D(
        GL_TEXTURE_2D, 0, 0, 0, TILE_SIZE, TILE_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits());
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace aether
